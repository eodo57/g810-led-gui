/*
  This file is part of g810-led.

  g810-led is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, version 3 of the License.

  g810-led is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with g810-led.  If not, see <https://www.gnu.org/licenses/>.
*/

#include "ScreenCapture.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

#include <sys/stat.h>
#include <unistd.h>

#include <giomm.h>
#include <glibmm.h>

#ifdef HAVE_PIPEWIRE
#include <pipewire/pipewire.h>
#include <spa/param/video/format-utils.h>
#endif

namespace {

const char *portalBus = "org.freedesktop.portal.Desktop";
const char *portalPath = "/org/freedesktop/portal/desktop";
const char *screenCastInterface = "org.freedesktop.portal.ScreenCast";
const char *requestInterface = "org.freedesktop.portal.Request";

// How long to wait for each step. Everything except Start is a local
// round trip; Start is the one that can put a dialog in front of a
// human, so it gets minutes rather than seconds.
const int stepTimeoutMs = 15000;
const int consentTimeoutMs = 180000;

std::string configDir() {
	return Glib::get_user_config_dir() + "/g810-led";
}

std::string tokenPath() {
	return configDir() + "/screen.token";
}

// The portal's restore token: the receipt for "share this screen with
// g810-led", so the dialog only appears the first time. It is not a
// secret in the credential sense, but it is a capability, so it is kept
// out of other users' reach.
std::string loadToken() {
	std::ifstream file(tokenPath());
	if (!file.is_open())
		return std::string();
	std::string token;
	std::getline(file, token);
	return token;
}

void saveToken(const std::string &token) {
	const std::string path = tokenPath();
	if (token.empty()) {
		::unlink(path.c_str());
		return;
	}
	g_mkdir_with_parents(configDir().c_str(), 0700);
	std::ofstream file(path);
	if (!file.is_open())
		return;
	file << token << "\n";
	file.close();
	::chmod(path.c_str(), 0600);
}

#ifdef HAVE_PIPEWIRE

// A per-request token, unique within this process. The portal builds
// the reply object path out of it, and a collision would mean listening
// on someone else's reply.
std::string makeToken(const char *prefix) {
	static std::atomic<unsigned> counter(0);
	char buffer[64];
	std::snprintf(buffer, sizeof(buffer), "%s_%d_%u", prefix, (int)getpid(),
		counter.fetch_add(1));
	return std::string(buffer);
}

// /org/freedesktop/portal/desktop/request/1_42/<token>
std::string requestPathFor(const std::string &sender, const std::string &token) {
	return std::string(portalPath) + "/request/" + sender + "/" + token;
}

std::string senderFragment(const Glib::ustring &uniqueName) {
	std::string sender = uniqueName;
	if (!sender.empty() && sender[0] == ':')
		sender.erase(0, 1);
	std::replace(sender.begin(), sender.end(), '.', '_');
	return sender;
}

std::string variantString(GVariant *dict, const char *key) {
	if (!dict)
		return std::string();
	GVariant *value = g_variant_lookup_value(dict, key, NULL);
	if (!value)
		return std::string();
	std::string result;
	// The portal spec types session_handle as a string, but some
	// backends have sent an object path; both carry a C string.
	if (g_variant_is_of_type(value, G_VARIANT_TYPE_STRING) ||
	    g_variant_is_of_type(value, G_VARIANT_TYPE_OBJECT_PATH))
		result = g_variant_get_string(value, NULL);
	g_variant_unref(value);
	return result;
}

// One portal request: the reply arrives as a signal on its own object,
// so the subscription has to exist before the method is called — a fast
// backend can answer before a later subscribe would have been in place.
class PortalRequest {
	public:
		PortalRequest(const Glib::RefPtr<Gio::DBus::Connection> &bus,
		              const std::string &path) :
				m_bus(bus), m_subscription(0), m_done(false), m_code(0),
				m_results(NULL) {
			subscribe(path);
		}
		~PortalRequest() {
			unsubscribe();
			if (m_results)
				g_variant_unref(m_results);
		}

		// The handle the method actually returned. Modern portals build
		// it from our token, so this is normally a no-op; an older one
		// that chose its own path would otherwise never reach us.
		void rebind(const std::string &path) {
			if (path.empty() || path == m_path || m_done)
				return;
			unsubscribe();
			subscribe(path);
		}

		// Runs a private main loop until the reply arrives, the caller
		// stops, or the deadline passes.
		bool wait(const std::atomic<bool> &run, int timeoutMs,
		          std::string &error) {
			Glib::RefPtr<Glib::MainContext> context =
				Glib::MainContext::get_thread_default();
			m_loop = Glib::MainLoop::create(context, false);
			const std::chrono::steady_clock::time_point deadline =
				std::chrono::steady_clock::now() +
				std::chrono::milliseconds(timeoutMs);
			// Polled rather than woken: stop() only has to clear the
			// flag, which keeps teardown free of a second lock, and a
			// portal that never answers still lets go.
			Glib::RefPtr<Glib::TimeoutSource> ticker =
				Glib::TimeoutSource::create(100);
			ticker->connect([this, &run, deadline]() {
				if (m_done || !run.load() ||
				    std::chrono::steady_clock::now() > deadline) {
					m_loop->quit();
					return false;
				}
				return true;
			});
			ticker->attach(context);
			if (!m_done && run.load())
				m_loop->run();
			ticker->destroy();
			m_loop.reset();

			if (!m_done) {
				if (!run.load())
					error = "cancelled";
				else
					error = "the desktop portal did not answer";
				return false;
			}
			if (m_code == 1) {
				error = "screen sharing was declined";
				return false;
			}
			if (m_code != 0) {
				error = "the desktop portal could not share the screen";
				return false;
			}
			return true;
		}

		// a{sv} of the reply, owned by the request.
		GVariant *results() const { return m_results; }

	private:
		void subscribe(const std::string &path) {
			m_path = path;
			m_subscription = m_bus->signal_subscribe(
				sigc::mem_fun(*this, &PortalRequest::onResponse),
				portalBus, requestInterface, "Response", path);
		}
		void unsubscribe() {
			if (m_subscription) {
				m_bus->signal_unsubscribe(m_subscription);
				m_subscription = 0;
			}
		}
		void onResponse(const Glib::RefPtr<Gio::DBus::Connection>&,
		                const Glib::ustring&, const Glib::ustring&,
		                const Glib::ustring&, const Glib::ustring&,
		                const Glib::VariantContainerBase &parameters) {
			if (m_done)
				return;
			GVariant *tuple = const_cast<GVariant *>(parameters.gobj());
			if (tuple && g_variant_n_children(tuple) >= 2) {
				GVariant *code = g_variant_get_child_value(tuple, 0);
				m_code = g_variant_get_uint32(code);
				g_variant_unref(code);
				m_results = g_variant_get_child_value(tuple, 1);
			}
			m_done = true;
			if (m_loop)
				m_loop->quit();
		}

		Glib::RefPtr<Gio::DBus::Connection> m_bus;
		Glib::RefPtr<Glib::MainLoop> m_loop;
		std::string m_path;
		guint m_subscription;
		bool m_done;
		guint32 m_code;
		GVariant *m_results;
};

#endif  // HAVE_PIPEWIRE

}  // namespace

// ---------------------------------------------------------------------
// The portal handshake.
// ---------------------------------------------------------------------

struct ScreenCapture::Portal {
	Glib::RefPtr<Gio::DBus::Connection> bus;
	Glib::RefPtr<Gio::Cancellable> cancellable;
	std::string sender;
	std::string sessionHandle;
	uint32_t nodeId;
	int fd;
	std::string label;

	Portal() : nodeId(0), fd(-1) {
		cancellable = Gio::Cancellable::create();
	}
	~Portal() { close(); }

	void close() {
		if (fd >= 0) {
			::close(fd);
			fd = -1;
		}
		if (bus && !sessionHandle.empty()) {
			// Politeness: without this the portal keeps the session
			// (and the compositor its capture) until the process dies.
			try {
				std::vector<Glib::VariantBase> empty;
				bus->call_sync(sessionHandle, "org.freedesktop.portal.Session",
					"Close", Glib::VariantContainerBase::create_tuple(empty),
					portalBus, 2000);
			} catch (const Glib::Error &) {
			}
			sessionHandle.clear();
		}
	}

#ifdef HAVE_PIPEWIRE
	bool open(const std::atomic<bool> &run, std::string &error);
#endif
};

// ---------------------------------------------------------------------
// The PipeWire stream.
// ---------------------------------------------------------------------

#ifdef HAVE_PIPEWIRE

struct ScreenCapture::Stream {
	ScreenCapture *owner;
	struct pw_thread_loop *loop;
	struct pw_context *context;
	struct pw_core *core;
	struct pw_stream *stream;
	struct spa_hook listener;
	struct spa_video_info_raw format;
	bool haveFormat;
	bool blueFirst;      // BGRx/BGRA rather than RGBx/RGBA
	bool warnedBuffers;
	std::chrono::steady_clock::time_point lastSample;

	Stream(ScreenCapture *capture) :
			owner(capture), loop(NULL), context(NULL), core(NULL),
			stream(NULL), haveFormat(false), blueFirst(true),
			warnedBuffers(false) {
		std::memset(&listener, 0, sizeof(listener));
		std::memset(&format, 0, sizeof(format));
	}
	~Stream() { close(); }

	bool open(int fd, uint32_t nodeId, std::string &error);
	void close();
	void onFormat(const struct spa_pod *param);
	void onProcess();

	// PipeWire calls back into C, so these are the trampolines. They
	// are members because everything they touch is private to
	// ScreenCapture.
	static void stateChanged(void *data, enum pw_stream_state,
	                         enum pw_stream_state state, const char *error) {
		Stream *self = (Stream *)data;
		if (state == PW_STREAM_STATE_ERROR)
			self->owner->reportStreamError(
				error ? error : "the screen stream failed");
	}
	static void paramChanged(void *data, uint32_t id, const struct spa_pod *param) {
		if (id == SPA_PARAM_Format && param != NULL)
			((Stream *)data)->onFormat(param);
	}
	static void process(void *data) {
		((Stream *)data)->onProcess();
	}
};

bool ScreenCapture::Portal::open(const std::atomic<bool> &run, std::string &error) {
	try {
		bus = Gio::DBus::Connection::get_sync(Gio::DBus::BUS_TYPE_SESSION);
	} catch (const Glib::Error &e) {
		error = "no session bus: " + std::string(e.what());
		return false;
	}
	if (!bus) {
		error = "no session bus";
		return false;
	}
	sender = senderFragment(bus->get_unique_name());

	// 1. CreateSession.
	const std::string sessionToken = makeToken("g810led_session");
	{
		const std::string token = makeToken("g810led");
		PortalRequest request(bus, requestPathFor(sender, token));
		GVariantBuilder options;
		g_variant_builder_init(&options, G_VARIANT_TYPE_VARDICT);
		g_variant_builder_add(&options, "{sv}", "handle_token",
			g_variant_new_string(token.c_str()));
		g_variant_builder_add(&options, "{sv}", "session_handle_token",
			g_variant_new_string(sessionToken.c_str()));
		Glib::VariantContainerBase args(g_variant_new("(a{sv})", &options), false);
		try {
			Glib::VariantContainerBase reply = bus->call_sync(portalPath,
				screenCastInterface, "CreateSession", args, cancellable,
				portalBus, stepTimeoutMs);
			GVariant *handle = g_variant_get_child_value(reply.gobj(), 0);
			request.rebind(g_variant_get_string(handle, NULL));
			g_variant_unref(handle);
		} catch (const Glib::Error &e) {
			error = "the desktop portal refused a screen cast session: " +
				std::string(e.what());
			return false;
		}
		if (!request.wait(run, stepTimeoutMs, error))
			return false;
		sessionHandle = variantString(request.results(), "session_handle");
		if (sessionHandle.empty())
			sessionHandle = std::string(portalPath) + "/session/" + sender +
				"/" + sessionToken;
	}

	// 2. SelectSources. persist_mode 2 plus the stored token is what
	// keeps this from asking again at every start.
	{
		const std::string token = makeToken("g810led");
		const std::string restore = loadToken();
		PortalRequest request(bus, requestPathFor(sender, token));
		GVariantBuilder options;
		g_variant_builder_init(&options, G_VARIANT_TYPE_VARDICT);
		g_variant_builder_add(&options, "{sv}", "handle_token",
			g_variant_new_string(token.c_str()));
		g_variant_builder_add(&options, "{sv}", "types",
			g_variant_new_uint32(1 | 2));      // monitor | window
		g_variant_builder_add(&options, "{sv}", "multiple",
			g_variant_new_boolean(FALSE));
		g_variant_builder_add(&options, "{sv}", "cursor_mode",
			g_variant_new_uint32(1));          // hidden
		g_variant_builder_add(&options, "{sv}", "persist_mode",
			g_variant_new_uint32(2));          // until revoked
		if (!restore.empty())
			g_variant_builder_add(&options, "{sv}", "restore_token",
				g_variant_new_string(restore.c_str()));
		Glib::VariantContainerBase args(g_variant_new("(oa{sv})",
			sessionHandle.c_str(), &options), false);
		try {
			Glib::VariantContainerBase reply = bus->call_sync(portalPath,
				screenCastInterface, "SelectSources", args, cancellable,
				portalBus, stepTimeoutMs);
			GVariant *handle = g_variant_get_child_value(reply.gobj(), 0);
			request.rebind(g_variant_get_string(handle, NULL));
			g_variant_unref(handle);
		} catch (const Glib::Error &e) {
			error = "the desktop portal would not offer a screen: " +
				std::string(e.what());
			return false;
		}
		if (!request.wait(run, stepTimeoutMs, error))
			return false;
	}

	// 3. Start — the step that shows the dialog, unless the restore
	// token covered it.
	{
		const std::string token = makeToken("g810led");
		PortalRequest request(bus, requestPathFor(sender, token));
		GVariantBuilder options;
		g_variant_builder_init(&options, G_VARIANT_TYPE_VARDICT);
		g_variant_builder_add(&options, "{sv}", "handle_token",
			g_variant_new_string(token.c_str()));
		Glib::VariantContainerBase args(g_variant_new("(osa{sv})",
			sessionHandle.c_str(), "", &options), false);
		try {
			Glib::VariantContainerBase reply = bus->call_sync(portalPath,
				screenCastInterface, "Start", args, cancellable,
				portalBus, consentTimeoutMs);
			GVariant *handle = g_variant_get_child_value(reply.gobj(), 0);
			request.rebind(g_variant_get_string(handle, NULL));
			g_variant_unref(handle);
		} catch (const Glib::Error &e) {
			error = "the desktop portal could not start the screen cast: " +
				std::string(e.what());
			return false;
		}
		if (!request.wait(run, consentTimeoutMs, error)) {
			// A token the compositor no longer accepts comes back as a
			// refusal; forgetting it turns the next attempt back into a
			// normal "which screen?" dialog instead of a dead end.
			if (!loadToken().empty())
				saveToken(std::string());
			return false;
		}

		GVariant *results = request.results();
		const std::string restore = variantString(results, "restore_token");
		if (!restore.empty())
			saveToken(restore);

		GVariant *streams = results ?
			g_variant_lookup_value(results, "streams", NULL) : NULL;
		bool found = false;
		if (streams) {
			GVariantIter iter;
			g_variant_iter_init(&iter, streams);
			guint32 node = 0;
			GVariant *props = NULL;
			// multiple=false, so the first stream is the one.
			if (g_variant_iter_next(&iter, "(u@a{sv})", &node, &props)) {
				nodeId = node;
				found = true;
				const std::string id = variantString(props, "id");
				guint32 sourceType = 0;
				if (props)
					g_variant_lookup(props, "source_type", "u", &sourceType);
				label = !id.empty() ? id :
					(sourceType == 2 ? "window" :
					 sourceType == 4 ? "virtual screen" : "screen");
				if (props)
					g_variant_unref(props);
			}
			g_variant_unref(streams);
		}
		if (!found) {
			error = "the desktop portal shared nothing";
			return false;
		}
	}

	// 4. OpenPipeWireRemote — a direct reply carrying the socket.
	{
		std::vector<Glib::VariantBase> nothing;
		GVariantBuilder options;
		g_variant_builder_init(&options, G_VARIANT_TYPE_VARDICT);
		Glib::VariantContainerBase args(g_variant_new("(oa{sv})",
			sessionHandle.c_str(), &options), false);
		Glib::RefPtr<Gio::UnixFDList> outList;
		try {
			Glib::VariantContainerBase reply = bus->call_sync(portalPath,
				screenCastInterface, "OpenPipeWireRemote", args, cancellable,
				Glib::RefPtr<Gio::UnixFDList>(), outList, portalBus,
				stepTimeoutMs);
			int index = 0;
			GVariant *handle = g_variant_get_child_value(reply.gobj(), 0);
			index = g_variant_get_handle(handle);
			g_variant_unref(handle);
			if (!outList || index < 0 || index >= outList->get_length()) {
				error = "the desktop portal returned no PipeWire socket";
				return false;
			}
			fd = outList->get(index);
		} catch (const Glib::Error &e) {
			error = "could not open the PipeWire socket: " + std::string(e.what());
			return false;
		}
		if (fd < 0) {
			error = "the desktop portal returned no PipeWire socket";
			return false;
		}
	}
	return true;
}

bool ScreenCapture::Stream::open(int fd, uint32_t nodeId, std::string &error) {
	static std::once_flag initOnce;
	std::call_once(initOnce, []() { pw_init(NULL, NULL); });

	// pw_context_connect_fd() below owns the descriptor from the moment
	// it is called, including when it fails — but nothing owns it before
	// that, so every path that gives up first has to close it here.
	loop = pw_thread_loop_new("g810-led-screen", NULL);
	if (!loop) {
		::close(fd);
		error = "could not create the PipeWire loop";
		return false;
	}
	context = pw_context_new(pw_thread_loop_get_loop(loop), NULL, 0);
	if (!context) {
		::close(fd);
		error = "could not create the PipeWire context";
		return false;
	}

	pw_thread_loop_lock(loop);
	if (pw_thread_loop_start(loop) < 0) {
		pw_thread_loop_unlock(loop);
		::close(fd);
		error = "could not start the PipeWire loop";
		return false;
	}
	core = pw_context_connect_fd(context, fd, NULL, 0);
	if (!core) {
		pw_thread_loop_unlock(loop);
		error = "could not connect to PipeWire";
		return false;
	}
	stream = pw_stream_new(core, "g810-led screen",
		pw_properties_new(
			PW_KEY_MEDIA_TYPE, "Video",
			PW_KEY_MEDIA_CATEGORY, "Capture",
			PW_KEY_MEDIA_ROLE, "Screen",
			NULL));
	if (!stream) {
		pw_thread_loop_unlock(loop);
		error = "could not create the PipeWire stream";
		return false;
	}
	static const struct pw_stream_events events = {
		PW_VERSION_STREAM_EVENTS,
		NULL,           // destroy
		stateChanged,   // state_changed
		NULL,           // control_info
		NULL,           // io_changed
		paramChanged,   // param_changed
		NULL,           // add_buffer
		NULL,           // remove_buffer
		process,        // process
		NULL,           // drained
		NULL,           // command
		NULL,           // trigger_done
	};
	pw_stream_add_listener(stream, &listener, &events, this);

	uint8_t buffer[1024];
	struct spa_pod_builder builder = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
	// Compound literals are C: in C++ their address cannot be taken, so
	// the ranges the builder wants by pointer are named first.
	struct spa_rectangle sizeDefault = SPA_RECTANGLE(1920, 1080);
	struct spa_rectangle sizeMin = SPA_RECTANGLE(1, 1);
	struct spa_rectangle sizeMax = SPA_RECTANGLE(16384, 16384);
	// 30 is already more than the LEDs can show; asking for the
	// monitor's refresh rate would make the compositor copy four times
	// as many frames for nothing.
	struct spa_fraction rateDefault = SPA_FRACTION(30, 1);
	struct spa_fraction rateMin = SPA_FRACTION(0, 1);
	struct spa_fraction rateMax = SPA_FRACTION(30, 1);
	const struct spa_pod *params[1];
	params[0] = (const struct spa_pod *)spa_pod_builder_add_object(&builder,
		SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat,
		SPA_FORMAT_mediaType,       SPA_POD_Id(SPA_MEDIA_TYPE_video),
		SPA_FORMAT_mediaSubtype,    SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw),
		SPA_FORMAT_VIDEO_format,    SPA_POD_CHOICE_ENUM_Id(5,
						SPA_VIDEO_FORMAT_BGRx, SPA_VIDEO_FORMAT_BGRx,
						SPA_VIDEO_FORMAT_RGBx, SPA_VIDEO_FORMAT_BGRA,
						SPA_VIDEO_FORMAT_RGBA),
		SPA_FORMAT_VIDEO_size,      SPA_POD_CHOICE_RANGE_Rectangle(
						&sizeDefault, &sizeMin, &sizeMax),
		SPA_FORMAT_VIDEO_framerate, SPA_POD_CHOICE_RANGE_Fraction(
						&rateDefault, &rateMin, &rateMax));

	const int result = pw_stream_connect(stream, PW_DIRECTION_INPUT, nodeId,
		(enum pw_stream_flags)(PW_STREAM_FLAG_AUTOCONNECT |
		                       PW_STREAM_FLAG_MAP_BUFFERS),
		params, 1);
	pw_thread_loop_unlock(loop);
	if (result < 0) {
		error = "could not connect to the screen stream";
		return false;
	}
	return true;
}

void ScreenCapture::Stream::close() {
	// Never with the loop locked: stop() waits for the loop thread.
	if (loop)
		pw_thread_loop_stop(loop);
	if (stream) {
		pw_stream_destroy(stream);
		stream = NULL;
	}
	if (core) {
		pw_core_disconnect(core);
		core = NULL;
	}
	if (context) {
		pw_context_destroy(context);
		context = NULL;
	}
	if (loop) {
		pw_thread_loop_destroy(loop);
		loop = NULL;
	}
}

void ScreenCapture::Stream::onFormat(const struct spa_pod *param) {
	uint32_t mediaType = 0, mediaSubtype = 0;
	if (spa_format_parse(param, &mediaType, &mediaSubtype) < 0)
		return;
	if (mediaType != SPA_MEDIA_TYPE_video || mediaSubtype != SPA_MEDIA_SUBTYPE_raw)
		return;
	struct spa_video_info_raw info;
	std::memset(&info, 0, sizeof(info));
	if (spa_format_video_raw_parse(param, &info) < 0)
		return;
	if (info.format != SPA_VIDEO_FORMAT_BGRx && info.format != SPA_VIDEO_FORMAT_RGBx &&
	    info.format != SPA_VIDEO_FORMAT_BGRA && info.format != SPA_VIDEO_FORMAT_RGBA) {
		owner->reportStreamError("the screen is in a pixel format this build "
		                         "cannot read");
		return;
	}
	format = info;
	blueFirst = info.format == SPA_VIDEO_FORMAT_BGRx ||
	            info.format == SPA_VIDEO_FORMAT_BGRA;
	haveFormat = true;
	owner->noteSize(info.size.width, info.size.height);

	// Shared memory only: a dmabuf would have to come back through the
	// GPU to be read, which is a lot of machinery for 100 LEDs. Asking
	// for the crop metadata as well keeps window capture honest, since
	// the compositor pads those frames.
	uint8_t buffer[512];
	struct spa_pod_builder builder = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
	const struct spa_pod *params[2];
	params[0] = (const struct spa_pod *)spa_pod_builder_add_object(&builder,
		SPA_TYPE_OBJECT_ParamBuffers, SPA_PARAM_Buffers,
		SPA_PARAM_BUFFERS_dataType, SPA_POD_CHOICE_FLAGS_Int(
			(1 << SPA_DATA_MemPtr) | (1 << SPA_DATA_MemFd)));
	params[1] = (const struct spa_pod *)spa_pod_builder_add_object(&builder,
		SPA_TYPE_OBJECT_ParamMeta, SPA_PARAM_Meta,
		SPA_PARAM_META_type, SPA_POD_Id(SPA_META_VideoCrop),
		SPA_PARAM_META_size, SPA_POD_Int(sizeof(struct spa_meta_region)));
	pw_stream_update_params(stream, params, 2);
}

void ScreenCapture::Stream::onProcess() {
	struct pw_buffer *held = pw_stream_dequeue_buffer(stream);
	if (!held)
		return;
	// A compositor that ignores the negotiated frame rate would have us
	// reducing frames far faster than the board can show them; the
	// buffer still goes straight back either way.
	const std::chrono::steady_clock::time_point now =
		std::chrono::steady_clock::now();
	const bool tooSoon = std::chrono::duration_cast<std::chrono::milliseconds>(
		now - lastSample).count() < 20;
	struct spa_buffer *buffer = held->buffer;
	if (!tooSoon && haveFormat && buffer->n_datas > 0 && buffer->datas[0].chunk &&
	    buffer->datas[0].chunk->size > 0) {
		struct spa_data &data = buffer->datas[0];
		if (data.data == NULL) {
			if (!warnedBuffers) {
				warnedBuffers = true;
				owner->reportStreamError("the compositor only offers GPU "
					"buffers (dmabuf), which this build cannot read");
			}
		} else {
			int x = 0, y = 0;
			int width = (int)format.size.width;
			int height = (int)format.size.height;
			struct spa_meta_region *crop = (struct spa_meta_region *)
				spa_buffer_find_meta_data(buffer, SPA_META_VideoCrop,
					sizeof(struct spa_meta_region));
			if (crop && spa_meta_region_is_valid(crop)) {
				x = crop->region.position.x;
				y = crop->region.position.y;
				width = (int)crop->region.size.width;
				height = (int)crop->region.size.height;
			}
			int stride = data.chunk->stride;
			if (stride <= 0)
				stride = (int)format.size.width * 4;
			// Trust the buffer, not the metadata: a stride or a crop
			// that does not match what was mapped would otherwise read
			// past the end of it.
			const uint8_t *pixels = (const uint8_t *)data.data + data.chunk->offset;
			const long available = (long)data.maxsize - (long)data.chunk->offset;
			if (available > 0 && width > 0 && height > 0) {
				const int rows = (int)(available / stride);
				if (x < 0) x = 0;
				if (y < 0) y = 0;
				if (y + height > rows)
					height = rows - y;
				const int columns = stride / 4;
				if (x + width > columns)
					width = columns - x;
				if (width > 0 && height > 0) {
					owner->sample(pixels, stride, x, y, width, height, blueFirst);
					lastSample = now;
				}
			}
		}
	}
	pw_stream_queue_buffer(stream, held);
}

#endif  // HAVE_PIPEWIRE

// ---------------------------------------------------------------------
// ScreenCapture itself.
// ---------------------------------------------------------------------

ScreenCapture::Frame::Frame() : valid(false), serial(0) {
	for (int y = 0; y < gridHeight; ++y)
		for (int x = 0; x < gridWidth; ++x)
			cells[y][x][0] = cells[y][x][1] = cells[y][x][2] = 0;
	average[0] = average[1] = average[2] = 0;
}

ScreenCapture::ScreenCapture() : m_portal(NULL), m_stream(NULL) {
	m_run = false;
	m_failed = false;
	m_hasFrame = false;
}

ScreenCapture::~ScreenCapture() {
	stop();
}

bool ScreenCapture::available() {
#ifdef HAVE_PIPEWIRE
	return true;
#else
	return false;
#endif
}

bool ScreenCapture::portalPresent() {
	// Callable before any GTK main loop exists (the daemon path, a test
	// harness): without this the connection cannot even be wrapped.
	Gio::init();
	try {
		Glib::RefPtr<Gio::DBus::Connection> bus =
			Gio::DBus::Connection::get_sync(Gio::DBus::BUS_TYPE_SESSION);
		if (!bus)
			return false;
		std::vector<Glib::VariantBase> arguments;
		arguments.push_back(Glib::Variant<Glib::ustring>::create(
			screenCastInterface));
		arguments.push_back(Glib::Variant<Glib::ustring>::create("version"));
		bus->call_sync(portalPath, "org.freedesktop.DBus.Properties", "Get",
			Glib::VariantContainerBase::create_tuple(arguments), portalBus,
			5000);
		return true;
	} catch (const Glib::Error &) {
		return false;
	}
}

void ScreenCapture::forgetSource() {
	saveToken(std::string());
}

bool ScreenCapture::sourceRemembered() {
	return !loadToken().empty();
}

bool ScreenCapture::isRunning() const {
	return m_run.load();
}

bool ScreenCapture::failed() const {
	return m_failed.load();
}

bool ScreenCapture::hasFrame() const {
	return m_hasFrame.load();
}

std::string ScreenCapture::lastError() const {
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_error;
}

std::string ScreenCapture::sourceName() const {
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_source;
}

ScreenCapture::Frame ScreenCapture::snapshot() const {
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_frame;
}

void ScreenCapture::setError(const std::string &message) {
	std::lock_guard<std::mutex> lock(m_mutex);
	m_error = message;
}

void ScreenCapture::reportStreamError(const std::string &message) {
	setError(message);
	m_failed = true;
}

void ScreenCapture::noteSize(unsigned width, unsigned height) {
	std::lock_guard<std::mutex> lock(m_mutex);
	char suffix[64];
	std::snprintf(suffix, sizeof(suffix), " (%u×%u)", width, height);
	const size_t bracket = m_source.find(" (");
	if (bracket != std::string::npos)
		m_source.erase(bracket);
	m_source += suffix;
}

bool ScreenCapture::start() {
	stop();
	Gio::init();
	setError(std::string());
	m_failed = false;
	m_hasFrame = false;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_frame = Frame();
		m_source.clear();
	}
#ifndef HAVE_PIPEWIRE
	setError("built without screen capture support — install the PipeWire "
	         "development files and rebuild");
	m_failed = true;
	return false;
#else
	m_portal = new Portal();
	m_stream = new Stream(this);
	m_run = true;
	m_worker = std::thread(&ScreenCapture::run, this);
	return true;
#endif
}

void ScreenCapture::stop() {
	m_run = false;
	if (m_portal && m_portal->cancellable)
		m_portal->cancellable->cancel();
	if (m_worker.joinable())
		m_worker.join();
	// Only after the join: the worker owns both until it returns.
	delete m_stream;
	m_stream = NULL;
	delete m_portal;
	m_portal = NULL;
	m_hasFrame = false;
}

void ScreenCapture::run() {
#ifdef HAVE_PIPEWIRE
	Gio::init();
	// Portal replies are signals, and a subscription is delivered to
	// the thread-default context of whoever subscribed — so this thread
	// gets its own, and the window's main loop never sees any of it.
	Glib::RefPtr<Glib::MainContext> context = Glib::MainContext::create();
	context->push_thread_default();

	std::string error;
	const bool opened = m_portal->open(m_run, error);
	context->pop_thread_default();
	if (!opened) {
		if (m_run.load()) {
			if (error == "cancelled")
				error = "screen capture was cancelled";
			else if (!portalPresent())
				error = "this desktop has no screen sharing portal — install "
				        "xdg-desktop-portal and a backend for your desktop";
			setError(error);
			m_failed = true;
		}
		m_run = false;
		return;
	}
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_source = m_portal->label;
	}
	// The stream owns the descriptor from here, including on failure.
	const int fd = m_portal->fd;
	m_portal->fd = -1;
	if (!m_stream->open(fd, m_portal->nodeId, error)) {
		setError(error);
		m_failed = true;
		m_run = false;
		return;
	}
	// PipeWire runs its own thread from here; this one is done.
#endif
}

void ScreenCapture::sample(const uint8_t *pixels, int stride, int originX,
                           int originY, int width, int height, bool blueFirst) {
	Frame frame;
	const int redOffset = blueFirst ? 2 : 0;
	const int blueOffset = blueFirst ? 0 : 2;
	double total[3] = {0, 0, 0};

	for (int cellY = 0; cellY < gridHeight; ++cellY) {
		const int top = originY + (int)((long)cellY * height / gridHeight);
		int bottom = originY + (int)((long)(cellY + 1) * height / gridHeight);
		if (bottom <= top)
			bottom = top + 1;
		// A bounded number of samples per cell, however large the
		// screen: reading every pixel of a 4K frame 30 times a second
		// to drive 100 LEDs would cost more than the rest of the
		// program put together.
		const int stepY = std::max(1, (bottom - top) / 8);

		for (int cellX = 0; cellX < gridWidth; ++cellX) {
			const int left = originX + (int)((long)cellX * width / gridWidth);
			int right = originX + (int)((long)(cellX + 1) * width / gridWidth);
			if (right <= left)
				right = left + 1;
			const int stepX = std::max(1, (right - left) / 8);

			unsigned long sums[3] = {0, 0, 0};
			unsigned long count = 0;
			for (int y = top; y < bottom; y += stepY) {
				const uint8_t *row = pixels + (long)y * stride;
				for (int x = left; x < right; x += stepX) {
					const uint8_t *pixel = row + (long)x * 4;
					sums[0] += pixel[redOffset];
					sums[1] += pixel[1];
					sums[2] += pixel[blueOffset];
					++count;
				}
			}
			if (!count)
				count = 1;
			for (int channel = 0; channel < 3; ++channel) {
				const float value = (float)sums[channel] / (float)count / 255.0f;
				frame.cells[cellY][cellX][channel] = value;
				total[channel] += value;
			}
		}
	}

	const double cells = (double)(gridWidth * gridHeight);
	for (int channel = 0; channel < 3; ++channel)
		frame.average[channel] = (float)(total[channel] / cells);
	frame.valid = true;

	{
		std::lock_guard<std::mutex> lock(m_mutex);
		frame.serial = m_frame.serial + 1;
		m_frame = frame;
	}
	m_hasFrame = true;
}

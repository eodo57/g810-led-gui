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

#include "TrayIcon.h"

#include <giomm.h>

#ifdef HAVE_APPINDICATOR
#include <libayatana-appindicator/app-indicator.h>
#endif

namespace {

// The same stock name as gui/g810-led-gui.desktop, so the tray and the
// application menu show the same thing and nothing has to be installed.
const char *iconName = "preferences-desktop-keyboard";

// Is there a panel out there willing to draw the icon? Asking the
// watcher is the only honest answer: the library will happily create an
// item that nothing ever shows, and a window that hid behind it would
// be unreachable.
bool hostRegistered() {
	Gio::init();
	try {
		Glib::RefPtr<Gio::DBus::Connection> bus =
			Gio::DBus::Connection::get_sync(Gio::DBus::BUS_TYPE_SESSION);
		if (!bus)
			return false;
		std::vector<Glib::VariantBase> arguments;
		arguments.push_back(Glib::Variant<Glib::ustring>::create(
			"org.kde.StatusNotifierWatcher"));
		arguments.push_back(Glib::Variant<Glib::ustring>::create(
			"IsStatusNotifierHostRegistered"));
		Glib::VariantContainerBase reply = bus->call_sync(
			"/StatusNotifierWatcher", "org.freedesktop.DBus.Properties", "Get",
			Glib::VariantContainerBase::create_tuple(arguments),
			"org.kde.StatusNotifierWatcher", 2000);
		GVariant *value = g_variant_get_child_value(reply.gobj(), 0);
		GVariant *inner = g_variant_get_variant(value);
		const bool registered = inner && g_variant_get_boolean(inner);
		if (inner)
			g_variant_unref(inner);
		g_variant_unref(value);
		return registered;
	} catch (const Glib::Error &) {
		// No watcher on the bus at all: no tray.
		return false;
	}
}

}  // namespace

TrayIcon::TrayIcon() : m_indicator(NULL), m_visible(false) {
}

TrayIcon::~TrayIcon() {
	hide();
#ifdef HAVE_APPINDICATOR
	if (m_indicator) {
		g_object_unref(G_OBJECT(m_indicator));
		m_indicator = NULL;
	}
#endif
}

bool TrayIcon::available() {
#ifndef HAVE_APPINDICATOR
	return false;
#else
	// A panel can come and go (a Plasma restart, a logout into a bare
	// WM), so this is re-asked rather than settled once — but not on
	// every call either, since it is a blocking round trip.
	static bool known = false;
	static bool answer = false;
	static gint64 checkedAt = 0;
	const gint64 now = g_get_monotonic_time();
	if (!known || now - checkedAt > 5000000) {
		answer = hostRegistered();
		checkedAt = now;
		known = true;
	}
	return answer;
#endif
}

void TrayIcon::show(Gtk::Menu &menu) {
#ifndef HAVE_APPINDICATOR
	(void)menu;
#else
	if (!m_indicator) {
		m_indicator = app_indicator_new("g810-led-gui", iconName,
			APP_INDICATOR_CATEGORY_HARDWARE);
		if (!m_indicator)
			return;
	}
	AppIndicator *indicator = (AppIndicator *)m_indicator;
	// The desktop draws the menu itself, from a description sent over
	// the bus; the widgets stay ours.
	app_indicator_set_menu(indicator, menu.gobj());
	app_indicator_set_status(indicator, APP_INDICATOR_STATUS_ACTIVE);
	m_visible = true;
#endif
}

void TrayIcon::hide() {
#ifdef HAVE_APPINDICATOR
	if (m_indicator)
		app_indicator_set_status((AppIndicator *)m_indicator,
			APP_INDICATOR_STATUS_PASSIVE);
#endif
	m_visible = false;
}

bool TrayIcon::visible() const {
	return m_visible;
}

void TrayIcon::setTooltip(const std::string &text) {
#ifndef HAVE_APPINDICATOR
	(void)text;
#else
	if (m_indicator)
		app_indicator_set_title((AppIndicator *)m_indicator, text.c_str());
#endif
}

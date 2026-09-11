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

#include "AudioCapture.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

#ifdef HAVE_PULSE
#include <pulse/error.h>
#include <pulse/pulseaudio.h>
#include <pulse/simple.h>
#endif

namespace {

const int sampleRate = 44100;
const size_t fftSize = 1024;   // 43 Hz bins, 23 ms window
const size_t hopSize = 512;    // ~86 analysis frames per second
const float lowFrequency = 40.0f;
const float highFrequency = 16000.0f;
const size_t bassHistorySize = 64;  // ~0.75 s of beat history
// A beat has to stand this far above the recent average, and beats are
// held apart so a loud sustained bass line is not a drum roll (250 ms
// still allows 240 BPM).
const float beatThreshold = 1.6f;
const double beatHoldMs = 250.0;

// In-place iterative radix-2 FFT with a precomputed twiddle table.
// Only fftSize-point transforms are needed here, so the table is built
// once per capture and indexed by stride.
void fft(std::vector<float> &re, std::vector<float> &im,
         const std::vector<float> &cosTable,
         const std::vector<float> &sinTable) {
	const size_t n = re.size();
	for (size_t i = 1, j = 0; i < n; ++i) {
		size_t bit = n >> 1;
		for (; j & bit; bit >>= 1)
			j ^= bit;
		j ^= bit;
		if (i < j) {
			std::swap(re[i], re[j]);
			std::swap(im[i], im[j]);
		}
	}
	for (size_t len = 2; len <= n; len <<= 1) {
		const size_t half = len / 2;
		const size_t stride = n / len;
		for (size_t i = 0; i < n; i += len) {
			for (size_t k = 0; k < half; ++k) {
				const float wr = cosTable[k * stride];
				const float wi = sinTable[k * stride];
				const float ur = re[i + k];
				const float ui = im[i + k];
				const float xr = re[i + k + half];
				const float xi = im[i + k + half];
				const float vr = xr * wr - xi * wi;
				const float vi = xr * wi + xi * wr;
				re[i + k] = ur + vr;
				im[i + k] = ui + vi;
				re[i + k + half] = ur - vr;
				im[i + k + half] = ui - vi;
			}
		}
	}
}

// Magnitude (roughly 0..1 for a full-scale tone) to a 0..1 display
// value on a dB scale — linear magnitudes leave everything but the bass
// line invisible. Band magnitudes span the full range; an RMS level
// varies far less, so it gets a narrower window or the meter would sit
// pinned near the top.
float toNorm(float magnitude, float scale, float rangeDb) {
	const float db = 20.0f * std::log10(magnitude * scale + 1e-9f);
	const float norm = (db + rangeDb) / rangeDb;
	return std::max(0.0f, std::min(1.0f, norm));
}

const float spectrumRangeDb = 60.0f;
const float levelRangeDb = 30.0f;

#ifdef HAVE_PULSE

// One blocking round-trip to the sound server for the source list and
// the default sink/source names. Bounded so an unresponsive server
// cannot hang the UI thread that calls listSources().
struct EnumState {
	std::vector<AudioCapture::Source> sources;
	std::string defaultSink;
	std::string defaultSource;
	int pending;
	bool done;
	bool connected;
	EnumState() : pending(0), done(false), connected(false) {}
};

void sourceInfoCallback(pa_context *, const pa_source_info *info, int eol,
                        void *userdata) {
	EnumState *state = (EnumState *)userdata;
	if (eol) {
		if (--state->pending <= 0)
			state->done = true;
		return;
	}
	if (!info || !info->name)
		return;
	AudioCapture::Source source;
	source.name = info->name;
	source.description = info->description ? info->description : info->name;
	source.monitor = info->monitor_of_sink != PA_INVALID_INDEX;
	state->sources.push_back(source);
}

void serverInfoCallback(pa_context *, const pa_server_info *info,
                        void *userdata) {
	EnumState *state = (EnumState *)userdata;
	if (info) {
		if (info->default_sink_name)
			state->defaultSink = info->default_sink_name;
		if (info->default_source_name)
			state->defaultSource = info->default_source_name;
	}
	if (--state->pending <= 0)
		state->done = true;
}

void contextStateCallback(pa_context *context, void *userdata) {
	EnumState *state = (EnumState *)userdata;
	switch (pa_context_get_state(context)) {
		case PA_CONTEXT_READY: {
			state->connected = true;
			state->pending = 2;
			pa_operation *operation =
				pa_context_get_source_info_list(context, sourceInfoCallback, state);
			if (operation)
				pa_operation_unref(operation);
			operation = pa_context_get_server_info(context, serverInfoCallback, state);
			if (operation)
				pa_operation_unref(operation);
			break;
		}
		case PA_CONTEXT_FAILED:
		case PA_CONTEXT_TERMINATED:
			state->done = true;
			break;
		default:
			break;
	}
}

bool queryServer(EnumState &state) {
	pa_mainloop *loop = pa_mainloop_new();
	if (!loop)
		return false;
	pa_context *context = pa_context_new(pa_mainloop_get_api(loop), "g810-led");
	if (!context) {
		pa_mainloop_free(loop);
		return false;
	}
	pa_context_set_state_callback(context, contextStateCallback, &state);
	bool ok = false;
	if (pa_context_connect(context, NULL, PA_CONTEXT_NOFLAGS, NULL) >= 0) {
		// Non-blocking iterate keeps the 2 s ceiling on an unresponsive
		// server, but the sleep between polls is what the caller actually
		// waits: at 10 ms it made every query cost ~130 ms.
		for (int i = 0; i < 2000 && !state.done; ++i) {
			if (pa_mainloop_iterate(loop, 0, NULL) < 0)
				break;
			if (!state.done)
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		ok = state.connected;
		pa_context_disconnect(context);
	}
	pa_context_unref(context);
	pa_mainloop_free(loop);
	return ok;
}

#endif

}  // namespace

const int AudioCapture::bandCount;

AudioCapture::AudioCapture() :
		m_bassIndex(0),
		m_bassFilled(0),
		m_levelLevel(0),
		m_bassLevel(0),
		m_peak(0.05f),
		m_levelPeak(0.05f),
		m_beats(0),
		m_lastBeatMs(0),
		m_clockMs(0),
		m_stream(NULL) {
	m_run = false;
	m_failed = false;
	m_gain = 100;
	m_smoothing = 45;
	m_autoGain = true;
}

AudioCapture::~AudioCapture() {
	stop();
}

bool AudioCapture::available() {
#ifdef HAVE_PULSE
	return true;
#else
	return false;
#endif
}

AudioCapture::Snapshot AudioCapture::query() {
	Snapshot snapshot;
#ifdef HAVE_PULSE
	EnumState state;
	if (!queryServer(state))
		return snapshot;
	// Monitors first: reacting to what the speakers play is the common
	// case, microphones the exception.
	for (size_t i = 0; i < state.sources.size(); ++i)
		if (state.sources[i].monitor)
			snapshot.sources.push_back(state.sources[i]);
	for (size_t i = 0; i < state.sources.size(); ++i)
		if (!state.sources[i].monitor)
			snapshot.sources.push_back(state.sources[i]);
	// Server-side aliases are resolved by PulseAudio and by PipeWire's
	// PulseAudio server when the real name is unknown.
	snapshot.defaultMonitor = state.defaultSink.empty() ?
		"@DEFAULT_MONITOR@" : state.defaultSink + ".monitor";
	snapshot.defaultInput = state.defaultSource.empty() ?
		"@DEFAULT_SOURCE@" : state.defaultSource;
#endif
	return snapshot;
}

std::vector<AudioCapture::Source> AudioCapture::listSources() {
	return query().sources;
}

std::string AudioCapture::defaultMonitorName() {
	return query().defaultMonitor;
}

std::string AudioCapture::defaultInputName() {
	return query().defaultInput;
}

bool AudioCapture::isRunning() const {
	return m_run.load();
}

bool AudioCapture::failed() const {
	return m_failed.load();
}

std::string AudioCapture::lastError() const {
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_error;
}

void AudioCapture::setError(const std::string &message) {
	std::lock_guard<std::mutex> lock(m_mutex);
	m_error = message;
}

void AudioCapture::setGain(float gain) {
	m_gain = (int)std::max(1.0f, std::min(2000.0f, gain * 100.0f));
}

void AudioCapture::setSmoothing(float amount) {
	m_smoothing = (int)std::max(0.0f, std::min(95.0f, amount * 100.0f));
}

void AudioCapture::setAutoGain(bool enabled) {
	m_autoGain = enabled;
}

AudioCapture::Frame AudioCapture::snapshot() const {
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_frame;
}

bool AudioCapture::start(const std::string &source) {
	stop();
	m_failed = false;
	setError(std::string());
#ifndef HAVE_PULSE
	(void)source;
	setError("built without PulseAudio support — install libpulse "
	         "development files and rebuild");
	m_failed = true;
	return false;
#else
	m_source = source;

	// A name the server does not know is not an error for every
	// implementation — PipeWire's PulseAudio server quietly falls back
	// to the default input, so a stale monitor name would end up
	// recording the microphone. Check the name first (the @…@ aliases
	// are resolved server-side and cannot be checked here).
	if (!m_source.empty() && m_source[0] != '@') {
		std::vector<Source> sources = listSources();
		bool known = false;
		for (size_t i = 0; i < sources.size() && !known; ++i)
			known = sources[i].name == m_source;
		if (!known && !sources.empty()) {
			setError("audio source is gone: " + m_source +
				" — press Refresh and pick another");
			m_failed = true;
			return false;
		}
	}

	m_window.resize(fftSize);
	for (size_t i = 0; i < fftSize; ++i)
		m_window[i] = 0.5f * (1.0f - std::cos(2.0f * (float)M_PI * i /
		                                      (float)(fftSize - 1)));
	m_samples.assign(fftSize, 0.0f);
	m_re.assign(fftSize, 0.0f);
	m_im.assign(fftSize, 0.0f);
	m_cos.resize(fftSize / 2);
	m_sin.resize(fftSize / 2);
	for (size_t i = 0; i < fftSize / 2; ++i) {
		const float angle = -2.0f * (float)M_PI * (float)i / (float)fftSize;
		m_cos[i] = std::cos(angle);
		m_sin[i] = std::sin(angle);
	}

	// Log-spaced bands: linear bins would give the bass one band and
	// the top octave twenty.
	const float binHz = (float)sampleRate / (float)fftSize;
	m_bandStart.resize(bandCount);
	m_bandEnd.resize(bandCount);
	for (int band = 0; band < bandCount; ++band) {
		const float from = lowFrequency * std::pow(highFrequency / lowFrequency,
			(float)band / (float)bandCount);
		const float to = lowFrequency * std::pow(highFrequency / lowFrequency,
			(float)(band + 1) / (float)bandCount);
		int start = (int)std::floor(from / binHz);
		int end = (int)std::ceil(to / binHz) - 1;
		start = std::max(1, std::min((int)fftSize / 2 - 1, start));
		end = std::max(start, std::min((int)fftSize / 2 - 1, end));
		m_bandStart[band] = start;
		m_bandEnd[band] = end;
	}
	m_bandLevel.assign(bandCount, 0.0f);
	m_bassHistory.assign(bassHistorySize, 0.0f);
	m_bassIndex = 0;
	m_bassFilled = 0;
	m_levelLevel = 0;
	m_bassLevel = 0;
	m_peak = 0.05f;
	m_levelPeak = 0.05f;
	m_beats = 0;
	m_lastBeatMs = 0;
	m_clockMs = 0;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_frame = Frame();
	}

	pa_sample_spec spec;
	spec.format = PA_SAMPLE_FLOAT32NE;
	spec.rate = sampleRate;
	spec.channels = 1;  // the server downmixes a stereo monitor for us
	pa_buffer_attr attr;
	attr.maxlength = (uint32_t)-1;
	attr.tlength = (uint32_t)-1;
	attr.prebuf = (uint32_t)-1;
	attr.minreq = (uint32_t)-1;
	attr.fragsize = (uint32_t)(hopSize * sizeof(float));
	int error = 0;
	pa_simple *stream = pa_simple_new(NULL, "g810-led", PA_STREAM_RECORD,
		m_source.empty() ? NULL : m_source.c_str(),
		"keyboard lighting", &spec, NULL, &attr, &error);
	if (!stream) {
		setError(std::string("cannot record from ") +
			(m_source.empty() ? "the default source" : m_source) + ": " +
			pa_strerror(error));
		m_failed = true;
		return false;
	}

	m_stream = stream;
	m_run = true;
	m_worker = std::thread(&AudioCapture::captureLoop, this);
	return true;
#endif
}

void AudioCapture::stop() {
	// The capture thread blocks in pa_simple_read for at most one hop
	// (~12 ms), so clearing the flag is enough to end it promptly.
	m_run = false;
	if (m_worker.joinable())
		m_worker.join();
}

void AudioCapture::captureLoop() {
#ifdef HAVE_PULSE
	pa_simple *stream = (pa_simple *)m_stream;
	std::vector<float> block(hopSize);
	// A named source can go away under us (headset unplugged, sink
	// removed). The server does not fail the read in that case — it
	// quietly moves the stream to another device, which for a monitor
	// stream usually means the microphone. Re-checking the name turns
	// that silent switch into an honest failure. The @…@ aliases are
	// resolved server-side and are expected to follow the default.
	const bool watchSource = !m_source.empty() && m_source[0] != '@';
	const int checkEvery = 400;  // ~5 s at one block per 11.6 ms
	int untilCheck = checkEvery;
	while (m_run.load()) {
		if (watchSource && --untilCheck <= 0) {
			untilCheck = checkEvery;
			std::vector<Source> sources = listSources();
			bool found = false;
			for (size_t i = 0; i < sources.size() && !found; ++i)
				found = sources[i].name == m_source;
			// An empty list means the query failed, not that the source
			// is gone; do not kill the effect over a hiccup.
			if (!found && !sources.empty()) {
				setError("audio source disappeared: " + m_source);
				m_failed = true;
				break;
			}
		}
		int error = 0;
		if (pa_simple_read(stream, &block[0], block.size() * sizeof(float),
		                   &error) < 0) {
			setError(std::string("audio read failed: ") + pa_strerror(error));
			m_failed = true;
			break;
		}
		// Slide the analysis window: drop the oldest hop, append the new
		// one, so consecutive frames overlap by half a window.
		std::memmove(&m_samples[0], &m_samples[hopSize],
			(fftSize - hopSize) * sizeof(float));
		std::memcpy(&m_samples[fftSize - hopSize], &block[0],
			hopSize * sizeof(float));
		analyze();
	}
	pa_simple_free(stream);
	m_stream = NULL;
	m_run = false;
#endif
}

void AudioCapture::analyze() {
	for (size_t i = 0; i < fftSize; ++i) {
		m_re[i] = m_samples[i] * m_window[i];
		m_im[i] = 0.0f;
	}
	fft(m_re, m_im, m_cos, m_sin);

	const float gain = m_gain.load() / 100.0f;
	const float smoothing = m_smoothing.load() / 100.0f;
	const bool autoGain = m_autoGain.load();
	// Fast rise, slower fall: bars should snap to a hit and glide back.
	const float release = 0.5f * (1.0f - smoothing) + 0.02f;
	const float attack = std::min(1.0f, release * 2.0f + 0.4f);
	const float binScale = 1.0f / (fftSize * 0.25f);  // Hann coherent gain

	// Peak magnitude per band, tilted upwards: music loses roughly 3 dB
	// per octave, so an untilted spectrum is a bass bar and nothing else.
	float magnitudes[bandCount];
	float frameMax = 0;
	for (int band = 0; band < bandCount; ++band) {
		float peak = 0;
		for (int bin = m_bandStart[band]; bin <= m_bandEnd[band]; ++bin) {
			const float magnitude = std::sqrt(m_re[bin] * m_re[bin] +
			                                  m_im[bin] * m_im[bin]) * binScale;
			if (magnitude > peak)
				peak = magnitude;
		}
		const float tilt = 1.0f + 2.0f * (float)band / (float)(bandCount - 1);
		magnitudes[band] = peak * tilt;
		if (magnitudes[band] > frameMax)
			frameMax = magnitudes[band];
	}

	// Auto-gain follows the loudest recent band and decays slowly, so a
	// quiet passage opens up without pumping on every transient.
	m_peak = std::max(m_peak * 0.9995f, frameMax);
	m_peak = std::max(m_peak, 0.002f);
	const float scale = gain * (autoGain ? 1.0f / m_peak : 1.0f);

	Frame frame;
	for (int band = 0; band < bandCount; ++band) {
		const float target = toNorm(magnitudes[band], scale, spectrumRangeDb);
		float &value = m_bandLevel[band];
		value += (target - value) * (target > value ? attack : release);
		frame.bands[band] = value;
	}

	// Overall loudness from the newest samples.
	float sum = 0;
	for (size_t i = fftSize - hopSize; i < fftSize; ++i)
		sum += m_samples[i] * m_samples[i];
	const float rms = std::sqrt(sum / (float)hopSize);
	m_levelPeak = std::max(std::max(m_levelPeak * 0.9995f, rms), 0.002f);
	const float levelTarget = toNorm(rms,
		gain * (autoGain ? 1.0f / m_levelPeak : 1.0f), levelRangeDb);
	m_levelLevel += (levelTarget - m_levelLevel) *
		(levelTarget > m_levelLevel ? attack : release);
	frame.level = m_levelLevel;

	// Low-frequency energy drives both the bass reading and the beat
	// detector.
	const float binHz = (float)sampleRate / (float)fftSize;
	const int bassEnd = std::min((int)fftSize / 2 - 1, (int)(150.0f / binHz));
	float bassEnergy = 0;
	int bassBins = 0;
	for (int bin = 1; bin <= bassEnd; ++bin) {
		const float magnitude = std::sqrt(m_re[bin] * m_re[bin] +
		                                  m_im[bin] * m_im[bin]) * binScale;
		bassEnergy += magnitude * magnitude;
		bassBins++;
	}
	if (bassBins > 0)
		bassEnergy /= (float)bassBins;
	const float bassTarget = toNorm(std::sqrt(bassEnergy), scale, spectrumRangeDb);
	m_bassLevel += (bassTarget - m_bassLevel) *
		(bassTarget > m_bassLevel ? attack : release);
	frame.bass = m_bassLevel;

	// A beat is a block whose bass energy stands well above the last
	// ~0.75 s, rate-limited so a sustained note is not a drum roll.
	// Average only the entries actually written: dividing by the full
	// history while it is still filling understates the mean, and every
	// block of the first ~0.75 s would clear the threshold and flash.
	float mean = 0;
	for (size_t i = 0; i < m_bassFilled; ++i)
		mean += m_bassHistory[i];
	if (m_bassFilled > 0)
		mean /= (float)m_bassFilled;
	m_bassHistory[m_bassIndex] = bassEnergy;
	m_bassIndex = (m_bassIndex + 1) % m_bassHistory.size();
	if (m_bassFilled < m_bassHistory.size())
		m_bassFilled++;
	m_clockMs += 1000.0 * (double)hopSize / (double)sampleRate;
	// A beat needs something to stand above, so wait for a little
	// history rather than reporting one on the very first block.
	const bool warmedUp = m_bassFilled >= bassHistorySize / 4;
	if (warmedUp && bassEnergy > mean * beatThreshold && bassEnergy > 1e-7f &&
	    m_clockMs - m_lastBeatMs > beatHoldMs) {
		m_beats++;
		m_lastBeatMs = m_clockMs;
	}
	frame.beats = m_beats;

	std::lock_guard<std::mutex> lock(m_mutex);
	m_frame = frame;
}

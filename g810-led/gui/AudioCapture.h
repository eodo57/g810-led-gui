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

#ifndef AUDIO_CAPTURE
#define AUDIO_CAPTURE

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// Live audio analysis behind the sound-reactive lighting modes.
//
// A worker thread records from a PulseAudio (or PipeWire, through its
// PulseAudio server) source — by default the monitor of the default
// sink, i.e. whatever the speakers are playing — and reduces each block
// to a small snapshot: a log-spaced band spectrum, an overall level, a
// bass level and a beat counter. Renderers poll snapshot() at their own
// frame rate, so a slow or fast frame rate never stalls the capture and
// the capture never stalls a frame.
//
// The class is decoupled from the LED side entirely: it knows nothing
// about keys, which keeps the mapping (AudioPlayer) testable and lets
// the analysis be reused by other effects.
//
// Compiled without libpulse (HAVE_PULSE undefined) everything still
// builds: start() fails and lastError() explains why.
class AudioCapture {
	public:
		// Spectrum resolution. Keyboards have far fewer columns than
		// this, so renderers interpolate; extra bands only cost a few
		// additions per frame.
		static const int bandCount = 32;

		struct Frame {
			float bands[bandCount];  // 0..1, low to high frequency
			float level;             // 0..1, overall loudness
			float bass;              // 0..1, energy below ~150 Hz
			uint32_t beats;          // monotonic beat-onset counter
			Frame() : level(0), bass(0), beats(0) {
				for (int i = 0; i < bandCount; ++i)
					bands[i] = 0;
			}
		};

		struct Source {
			std::string name;         // PulseAudio source name
			std::string description;  // human readable
			bool monitor;             // monitor of an output (loopback)
			Source() : monitor(false) {}
		};

		AudioCapture();
		~AudioCapture();

		// Everything the UI needs about the sound server, from a single
		// round trip — asking for the list and the defaults separately
		// cost two connections and twice the latency.
		struct Snapshot {
			std::vector<Source> sources;
			std::string defaultMonitor;   // "what you hear"
			std::string defaultInput;     // microphone
		};
		static Snapshot query();

		// True when the build has a working audio backend.
		static bool available();
		// Recordable sources, monitors first. Empty if the sound server
		// is unreachable.
		static std::vector<Source> listSources();
		// Monitor source of the current default sink ("what you hear"),
		// falling back to the server's @DEFAULT_MONITOR@ alias.
		static std::string defaultMonitorName();
		// Default recording source (microphone).
		static std::string defaultInputName();

		// source: a name from listSources(), or one of the helpers
		// above. Returns false (and sets lastError) if the source
		// cannot be opened.
		bool start(const std::string &source);
		void stop();
		bool isRunning() const;
		bool failed() const;
		std::string lastError() const;

		void setGain(float gain);         // manual boost, 1.0 = as captured
		void setSmoothing(float amount);  // 0..0.95, envelope release
		void setAutoGain(bool enabled);   // normalize to the loudest recent peak

		// Latest analysis. Cheap enough to call every frame.
		Frame snapshot() const;

	private:
		void captureLoop();
		void analyze();
		void setError(const std::string &message);

		std::vector<float> m_window;    // Hann window
		std::vector<float> m_samples;   // newest fftSize samples
		std::vector<float> m_re;
		std::vector<float> m_im;
		std::vector<float> m_cos;       // twiddles
		std::vector<float> m_sin;
		std::vector<int> m_bandStart;   // first fft bin of each band
		std::vector<int> m_bandEnd;     // last fft bin (inclusive)
		std::vector<float> m_bandLevel; // smoothed 0..1 values
		std::vector<float> m_bassHistory;
		size_t m_bassIndex;
		size_t m_bassFilled;            // history entries written so far
		float m_levelLevel;
		float m_bassLevel;
		float m_peak;                   // auto-gain reference, spectrum
		float m_levelPeak;              // auto-gain reference, loudness
		uint32_t m_beats;
		double m_lastBeatMs;
		double m_clockMs;               // audio clock, no wall time needed

		std::string m_source;
		void *m_stream;                 // pa_simple*, kept opaque so the
		                                // header stays libpulse-free
		std::thread m_worker;
		mutable std::mutex m_mutex;     // guards m_frame and m_error
		Frame m_frame;
		std::string m_error;
		std::atomic<bool> m_run;
		std::atomic<bool> m_failed;
		std::atomic<int> m_gain;        // gain * 100, atomic for the UI thread
		std::atomic<int> m_smoothing;   // smoothing * 100
		std::atomic<bool> m_autoGain;
};

#endif

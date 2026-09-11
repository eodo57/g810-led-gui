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

#ifndef SCREEN_CAPTURE
#define SCREEN_CAPTURE

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

// What is on screen, reduced to a grid small enough to paint a keyboard.
//
// Screen content comes from the desktop portal
// (org.freedesktop.portal.ScreenCast) and then over PipeWire, which is
// the only route that works under Wayland — an X11 grab cannot see
// native Wayland windows. The portal asks the user what to share, and
// remembers the answer through a restore token, so the dialog appears
// once rather than at every start.
//
// Each captured buffer is reduced by strided sampling to a fixed
// gridWidth × gridHeight of averaged colours: a 4K frame is 8 million
// pixels, and reading all of them 25 times a second to drive 100 LEDs
// would be absurd, so each cell averages a bounded number of samples
// instead.
//
// Like AudioCapture this knows nothing about keys — the mapping lives
// in ScreenPlayer — and start() never blocks the caller: the portal
// handshake (which waits for a human) runs on the worker thread, so the
// window stays responsive while the consent dialog is up.
class ScreenCapture {
	public:
		static const int gridWidth = 32;
		static const int gridHeight = 12;

		struct Frame {
			// Row-major from the top-left of the captured area, each
			// cell 0..1 per channel.
			float cells[gridHeight][gridWidth][3];
			float average[3];  // whole-frame mean, for the dominant mode
			bool valid;        // a buffer has actually arrived
			uint32_t serial;   // bumped once per captured buffer
			Frame();
		};

		ScreenCapture();
		~ScreenCapture();

		// True when the build has the portal and PipeWire support.
		static bool available();
		// Whether the desktop offers a ScreenCast portal at all, which
		// is what decides if this can work at run time. Cheap: one
		// property read, with the answer cached.
		static bool portalPresent();
		// Forget the remembered choice of screen, so the next start()
		// asks again.
		static void forgetSource();
		// True once a source has been picked and remembered — i.e. a
		// later start() will not put a dialog up.
		static bool sourceRemembered();

		// Opens the portal and starts streaming. Returns false only for
		// an immediate, local failure; a portal that has to ask the user
		// returns true and reports the outcome through failed() and
		// hasFrame() once the answer arrives.
		bool start();
		void stop();
		bool isRunning() const;
		bool failed() const;
		// A buffer has arrived and snapshot() holds real content. Until
		// then the effect is waiting for the user, or for the first
		// frame from an idle screen.
		bool hasFrame() const;
		std::string lastError() const;
		// What is being captured, for the UI — "Screen (2560×1440)" or
		// the window title the compositor reported.
		std::string sourceName() const;

		// Latest reduced frame. Cheap enough to call every frame.
		Frame snapshot() const;

	private:
		void run();
		void setError(const std::string &message);
		// Called from the PipeWire thread.
		void reportStreamError(const std::string &message);
		void noteSize(unsigned width, unsigned height);
		// Reduce one mapped frame into the grid and publish it.
		void sample(const uint8_t *pixels, int stride, int originX, int originY,
		            int width, int height, bool blueFirst);

		struct Portal;   // D-Bus handshake, defined in the .cpp
		struct Stream;   // PipeWire side, defined in the .cpp
		Portal *m_portal;
		Stream *m_stream;

		std::thread m_worker;
		mutable std::mutex m_mutex;  // guards m_frame, m_error, m_source
		Frame m_frame;
		std::string m_error;
		std::string m_source;
		std::atomic<bool> m_run;
		std::atomic<bool> m_failed;
		std::atomic<bool> m_hasFrame;
};

#endif

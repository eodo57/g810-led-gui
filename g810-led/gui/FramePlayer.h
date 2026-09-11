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

#ifndef FRAME_PLAYER
#define FRAME_PLAYER

#include <gtkmm.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <thread>

#include "../src/classes/Keyboard.h"

// Colour arithmetic the frame-driven effects share.
namespace frames {

	inline float clamp01(float value) {
		return std::max(0.0f, std::min(1.0f, value));
	}

	inline uint8_t toByte(float value) {
		return (uint8_t)std::round(clamp01(value) * 255.0f);
	}

	// Linear mix from under (amount 0) to tint (amount 1).
	inline LedKeyboard::Color blend(const LedKeyboard::Color &under,
	                                const LedKeyboard::Color &tint, float amount) {
		amount = clamp01(amount);
		LedKeyboard::Color color;
		color.red = toByte((under.red * (1.0f - amount) + tint.red * amount) / 255.0f);
		color.green = toByte((under.green * (1.0f - amount) + tint.green * amount) / 255.0f);
		color.blue = toByte((under.blue * (1.0f - amount) + tint.blue * amount) / 255.0f);
		return color;
	}

	inline bool isBlack(const LedKeyboard::Color &color) {
		return color.red == 0 && color.green == 0 && color.blue == 0;
	}

	// A live source moves continuously, so an exact comparison would send
	// a packet for every 1/255 step of a fade. A small deadband cuts that
	// traffic without a visible difference — except at black, which is
	// always sent so keys reliably go dark.
	inline bool worthSending(const LedKeyboard::Color &sent,
	                         const LedKeyboard::Color &want) {
		if (isBlack(want))
			return !isBlack(sent);
		return std::abs((int)sent.red - (int)want.red) > 2 ||
		       std::abs((int)sent.green - (int)want.green) > 2 ||
		       std::abs((int)sent.blue - (int)want.blue) > 2;
	}

}  // namespace frames

// The machinery every live effect driven by a sensor needs, minus the
// sensor and the mapping.
//
// A frame is produced on the thread that drives time — the GTK timer in
// the window, or the loop in runUntilStopped() for a detached daemon —
// and handed to a worker thread that talks to the device, so a slow
// hid_write can never freeze the window. Frames the device could not
// keep up with are merged rather than dropped, so no key is left stale,
// and colours within a small deadband of what was last written are not
// resent at all.
//
// Subclasses supply the sensor and renderFrame(); everything below the
// mapping — threading, merging, diffing, the overlay blend over the
// applied scheme, the timer and the daemon loop — lives here. It grew
// out of AudioPlayer, which had accumulated three fixes that the other
// copies of the same code did not have.
class FramePlayer {
	public:
		typedef std::pair<int, int> Position;  // (row, column in quarter units)
		typedef std::function<bool(const LedKeyboard::KeyValueArray&)> Writer;
		// Called on the thread that drives frames, which is the GTK
		// thread whenever there is a window (the timer in launch()); the
		// daemon path has no main loop and passes no preview. Callers
		// paint straight from it, so a player that ever produced frames
		// on a worker thread would have to marshal them itself.
		typedef std::function<void(const LedKeyboard::KeyValueArray&)> Preview;

		FramePlayer();
		virtual ~FramePlayer();

		bool isRunning() const;
		// Safe from a signal handler: it only touches the atomic, and
		// runUntilStopped() polls it and does the real teardown.
		void requestStop();
		// Drives frames itself, for the daemon path where there is no
		// main loop to hang a timer on.
		void runUntilStopped();
		void stop();

		// The colours the running effect blends over, captured when it
		// started — not whatever the draft holds now.
		const std::map<LedKeyboard::Key, LedKeyboard::Color> &baseColors() const;

		// Blend over the applied scheme instead of over black, so the
		// keys stay readable when the sensor is quiet or dark. Settable
		// while running.
		void setOverlay(bool overlay);
		bool overlay() const;

	protected:
		// One frame's worth of changed keys. Called on the thread driving
		// time; use submit() for each key so the blend, the deadband and
		// the diff stay in one place.
		virtual void renderFrame(LedKeyboard::KeyValueArray &values) = 0;
		// True when the subclass's sensor has died, which ends the effect.
		virtual bool captureFailed() const;
		// Called first thing in stop(), before the worker is joined.
		virtual void stopCapture();

		// Record the mapping and the callbacks and work out the grid
		// extents. Call before starting the sensor: it returns false when
		// the writer or the key map is unusable, and there is no point
		// opening a device for a frame that cannot be drawn.
		//
		// baseIsOnBoard says the keyboard is already showing base, so the
		// first frame only has to write what differs from it — otherwise
		// an effect that starts by blending over the applied scheme
		// rewrites the whole board to the colours it already has.
		bool prepare(const std::map<LedKeyboard::Key, Position> &positions,
		             const std::map<LedKeyboard::Key, LedKeyboard::Color> &base,
		             Writer writer, Preview preview, unsigned intervalMs,
		             bool overlay, bool baseIsOnBoard = false);
		// Start the worker and, when there is a preview, the GTK timer.
		// Call once the sensor is up.
		void launch();

		// Blend tint over this key's base by amount and queue it, unless
		// it is within the deadband of what was last written.
		void submit(LedKeyboard::Key key, const LedKeyboard::Color &tint,
		            float amount, LedKeyboard::KeyValueArray &values);

		const std::map<LedKeyboard::Key, Position> &positions() const;
		int minRow() const;
		int maxRow() const;
		int minCol() const;
		int maxCol() const;
		// Set when the writer callback fails, so a subclass's lastError()
		// can say so: the sensor is fine in that case and reports nothing.
		bool writeFailed() const;

	private:
		bool onTimer();
		void tick();
		void pushFrame(const LedKeyboard::KeyValueArray &values);
		void workerLoop();

		std::map<LedKeyboard::Key, Position> m_positions;
		std::map<LedKeyboard::Key, LedKeyboard::Color> m_base;
		Writer m_writer;
		Preview m_preview;
		std::map<LedKeyboard::Key, LedKeyboard::Color> m_lastSent;
		int m_minRow;
		int m_maxRow;
		int m_minCol;
		int m_maxCol;
		unsigned m_intervalMs;
		sigc::connection m_timer;
		std::thread m_worker;
		std::mutex m_mutex;
		std::condition_variable m_cv;
		// Pending frame as a map so a frame the device could not keep up
		// with is merged into the next one instead of being lost.
		std::map<LedKeyboard::Key, LedKeyboard::Color> m_pending;
		std::atomic<bool> m_run;
		std::atomic<bool> m_hasFrame;
		std::atomic<bool> m_failed;
		std::atomic<bool> m_writeFailed;
		std::atomic<bool> m_overlay;
};

#endif

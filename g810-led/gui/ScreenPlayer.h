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

#ifndef SCREEN_PLAYER
#define SCREEN_PLAYER

#include <map>
#include <string>

#include "../src/classes/Keyboard.h"
#include "FramePlayer.h"
#include "ScreenCapture.h"

// The keyboard as a very low resolution screen.
//
// Modes:
//   mirror    each key takes the colour of the part of the screen above
//             it, so the board becomes a 20-by-6 version of the display
//   dominant  one colour for the whole board, weighted towards the
//             colourful parts of the screen rather than the flat mean —
//             the average of a desktop is grey, which would be a
//             pointless thing to show
//
// Only the mapping lives here; the threading, frame merging, colour
// deadband and overlay blend are FramePlayer's, and the capture itself
// is ScreenCapture's.
class ScreenPlayer : public FramePlayer {
	public:
		enum class Mode { mirror, dominant };

		struct Settings {
			Mode mode;
			// 0 = exactly the colours on screen, 1 = fully saturated and
			// pushed to full brightness. Screens are mostly muted greys
			// and a keyboard showing that looks broken, so the default
			// leans towards colour.
			float boost;
			float smoothing;     // 0..0.95, per-key temporal averaging
			bool overlay;        // blend over the applied scheme
			unsigned intervalMs;
			Settings() :
				mode(Mode::mirror), boost(0.55f), smoothing(0.5f),
				overlay(false), intervalMs(40) {}
		};

		// Everything a detached copy needs to pick the effect up after
		// the window is gone. There is no screen in here on purpose: the
		// desktop remembers which one was shared (the restore token), so
		// the daemon asks the portal for the same one and never puts a
		// dialog up where no window could explain it.
		struct State {
			uint16_t vendorID = 0;
			uint16_t productID = 0;
			std::string serial;
			Settings settings;
			std::map<LedKeyboard::Key, Position> positions;
			std::map<LedKeyboard::Key, LedKeyboard::Color> base;
		};

		ScreenPlayer();
		~ScreenPlayer();

		// Starts the capture, which opens the desktop portal — the user
		// may be asked which screen to share, so frames only begin once
		// they have answered. Returns false for an immediate failure;
		// anything that has to wait for the portal is reported through
		// lastError() later.
		bool start(const Settings &settings,
		           const std::map<LedKeyboard::Key, Position> &positions,
		           const std::map<LedKeyboard::Key, LedKeyboard::Color> &base,
		           Writer writer, Preview preview = Preview());
		void setSettings(const Settings &settings);
		std::string lastError() const;
		// Running, but nothing has arrived yet: either the consent
		// dialog is still up or the first frame has not been sent.
		bool waiting() const;
		// What is being captured, for the UI.
		std::string sourceName() const;

		static bool saveState(const std::string &path, const State &state);
		static bool loadState(const std::string &path, State &state);

	protected:
		void renderFrame(LedKeyboard::KeyValueArray &values) override;
		bool captureFailed() const override;
		void stopCapture() override;

	private:
		struct Rgb { float v[3]; };

		// Saturation and brightness boost, in place.
		void enhance(float rgb[3]) const;
		// The colour of the screen under this key, bilinear so a board
		// with more columns than the grid does not step.
		void sampleAt(const ScreenCapture::Frame &frame, float x, float y,
		              float rgb[3]) const;

		ScreenCapture m_capture;
		Settings m_settings;
		std::map<LedKeyboard::Key, Rgb> m_smoothed;
};

int runScreenDaemon(const std::string &path);

#endif

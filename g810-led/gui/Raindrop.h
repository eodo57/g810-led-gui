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

#ifndef RAINDROP_ANIMATION
#define RAINDROP_ANIMATION

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "../src/classes/Keyboard.h"
#include "FramePlayer.h"

// Raindrop animation driven over the key grid of the on-screen
// keyboard: drops fall along columns with fading trails, splash into
// expanding ripples at the bottom, and everything decays each frame.
// Every frame only the diff against the last written frame is batched
// through the writer callback (one setKeys + one commit), so frames
// with small changes stay cheap.
//
// Only the simulation lives here: the worker thread, the merged
// frames, the colour deadband, the overlay blend and the timer are
// FramePlayer's.
class RaindropAnimation : public FramePlayer {
	public:
		RaindropAnimation();
		~RaindropAnimation();

		// preview runs on the GTK thread. writer runs on a worker thread
		// so hid_write cannot freeze the window. With a base map, drops
		// mix toward color over those key colors.
		void start(const std::map<LedKeyboard::Key, Position> &positions,
		           const LedKeyboard::Color &color, Writer writer,
		           unsigned intervalMs = 100,
		           const std::map<LedKeyboard::Key, LedKeyboard::Color> &base =
		               std::map<LedKeyboard::Key, LedKeyboard::Color>(),
		           Preview preview = Preview());

		struct State {
			uint16_t vendorID = 0;
			uint16_t productID = 0;
			std::string serial;
			LedKeyboard::Color color = {0x44, 0x88, 0xff};
			unsigned intervalMs = 100;
			bool overlay = false;
			std::map<LedKeyboard::Key, Position> positions;
			std::map<LedKeyboard::Key, LedKeyboard::Color> base;
		};

		static bool saveState(const std::string &path, const State &state);
		static bool loadState(const std::string &path, State &state);

	protected:
		void renderFrame(LedKeyboard::KeyValueArray &values) override;
		void stopCapture() override;

	private:
		struct Drop { int col; float y; float speed; };
		struct Ripple { int row; int col; float radius; };

		void addLight(LedKeyboard::Key key, float amount);
		void emitLights(LedKeyboard::KeyValueArray &values);

		LedKeyboard::Color m_color;
		// How lit each key is, 0..1. Keys not in here are at their base
		// colour and are not written at all.
		std::map<LedKeyboard::Key, float> m_amount;
		std::vector<Drop> m_drops;
		std::vector<Ripple> m_ripples;
		int m_frame = 0;
};

int runRainDaemon(const std::string &path);

#endif

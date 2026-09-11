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

#ifndef AUDIO_PLAYER
#define AUDIO_PLAYER

#include <map>
#include <string>

#include "../src/classes/Keyboard.h"
#include "AudioCapture.h"
#include "FramePlayer.h"

// Sound-reactive lighting: turns the AudioCapture spectrum into key
// colors over the key grid of the on-screen keyboard.
//
// Modes:
//   bars    columns are frequency bands, the lit height of a column is
//           that band's level — a spectrum analyzer across the board
//   rainbow same band mapping, but each column keeps a fixed hue and
//           only its brightness dances
//   level   the whole board tracks loudness, color fading low → high
//   beat    a flash on each detected beat, decaying between them
//
// Only the mapping lives here: the threading, the merging of frames the
// device could not keep up with, the colour deadband and the overlay
// blend are all FramePlayer's.
class AudioPlayer : public FramePlayer {
	public:
		enum class Mode { bars, rainbow, level, beat };

		struct Settings {
			Mode mode;
			LedKeyboard::Color lowColor;   // quiet end of the gradient
			LedKeyboard::Color highColor;  // loud end of the gradient
			float gain;                    // 1.0 = as captured
			float smoothing;               // 0..0.95, higher = slower fall
			bool autoGain;
			// Blend the effect over the base colors instead of over
			// black, so the keys stay readable when nothing is playing.
			bool overlay;
			unsigned intervalMs;           // frame interval
			Settings() :
				mode(Mode::bars), gain(1.0f), smoothing(0.45f),
				autoGain(true), overlay(false), intervalMs(40) {
				lowColor.red = 0x00; lowColor.green = 0x66; lowColor.blue = 0xff;
				highColor.red = 0xff; highColor.green = 0x00; highColor.blue = 0x66;
			}
		};

		// Everything the daemon needs to pick the effect up after the
		// window is gone.
		struct State {
			uint16_t vendorID = 0;
			uint16_t productID = 0;
			std::string serial;
			std::string source;
			Settings settings;
			std::map<LedKeyboard::Key, Position> positions;
			std::map<LedKeyboard::Key, LedKeyboard::Color> base;
		};

		AudioPlayer();
		~AudioPlayer();

		// preview runs on the GTK thread, writer on a worker thread.
		// Without a preview no GTK timer is installed and the caller
		// drives frames with runUntilStopped() — that is the daemon
		// path, which has no main loop. Returns false (and sets
		// lastError) when the audio source or the key map is unusable.
		// base holds the key colors the effect blends over when
		// settings.overlay is on; it is captured whether or not overlay
		// is set, so the toggle works while the effect runs.
		bool start(const std::string &source, const Settings &settings,
		           const std::map<LedKeyboard::Key, Position> &positions,
		           const std::map<LedKeyboard::Key, LedKeyboard::Color> &base,
		           Writer writer, Preview preview = Preview());
		void setSettings(const Settings &settings);
		std::string lastError() const;

		// Current loudness, 0..1 — for a UI meter.
		float level() const;

		static bool saveState(const std::string &path, const State &state);
		static bool loadState(const std::string &path, State &state);

	protected:
		void renderFrame(LedKeyboard::KeyValueArray &values) override;
		bool captureFailed() const override;
		void stopCapture() override;

	private:
		LedKeyboard::Color gradient(float t) const;

		AudioCapture m_capture;
		Settings m_settings;
		uint32_t m_lastBeat;
		float m_flash;
};

int runAudioDaemon(const std::string &path);

#endif

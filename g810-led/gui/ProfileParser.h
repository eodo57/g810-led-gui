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

#ifndef PROFILE_PARSER
#define PROFILE_PARSER

#include <chrono>
#include <string>
#include <vector>

#include "../src/classes/Keyboard.h"

// One parsed command from a g810-led profile text file (the format of
// the files in sample_profiles/, as consumed by the CLI's -p/-pp).
struct ProfileCommand {
	enum class Type {
		all,          // a RRGGBB
		group,        // g group RRGGBB
		key,          // k key RRGGBB
		region,       // r region RRGGBB
		mr,           // mr value
		mn,           // mn value
		gkm,          // gkm value
		startupMode,  // sm wave|color
		onBoardMode,  // obm board|software
		fx,           // fx effect target [color] [period]
		commit,       // c
		rain,         // rain RRGGBB [overlay] | rain off
		wave,         // wave pulse|travel sine|triangle|square|saw period min max
		wavePoint,    // wp t y
		waveColor,    // wc t RRGGBB
		audio,        // audio bars|rainbow|level|beat lowRRGGBB highRRGGBB
		              //       gain% smoothing% auto|fixed [overlay|solid]
		              //       [source] | audio off
		screen        // screen mirror|dominant boost% smoothing%
		              //        [overlay|solid] | screen off
	};
	// Defaulted throughout: the struct holds a std::string, so copies
	// are no longer trivial and an unset member would be read.
	Type type = Type::commit;
	LedKeyboard::Key key = LedKeyboard::Key::a;
	LedKeyboard::KeyGroup group = LedKeyboard::KeyGroup::keys;
	uint8_t region = 0;
	uint8_t value = 0;
	bool rainOverlay = false;
	bool waveTravel = false;
	uint8_t waveShape = 0;
	uint8_t waveMin = 15;
	uint8_t waveMax = 100;
	float waveT = 0;
	float waveY = 0;
	uint8_t audioMode = 0;  // 0 bars, 1 rainbow, 2 level, 3 beat
	LedKeyboard::Color audioHighColor = {0xff, 0x00, 0x66};
	uint16_t audioGain = 100;
	uint8_t audioSmoothing = 45;
	bool audioAutoGain = true;
	bool audioOverlay = false;
	std::string audioSource;
	uint8_t screenMode = 0;  // 0 mirror, 1 dominant
	uint8_t screenBoost = 55;
	uint8_t screenSmoothing = 50;
	bool screenOverlay = false;
	LedKeyboard::StartupMode startupMode = LedKeyboard::StartupMode::wave;
	LedKeyboard::OnBoardMode onBoardMode = LedKeyboard::OnBoardMode::board;
	LedKeyboard::NativeEffect effect = LedKeyboard::NativeEffect::off;
	LedKeyboard::NativeEffectPart part = LedKeyboard::NativeEffectPart::all;
	LedKeyboard::NativeEffectStorage storage = LedKeyboard::NativeEffectStorage::none;
	std::chrono::duration<uint16_t, std::milli> period =
		std::chrono::duration<uint16_t, std::milli>(0);
	LedKeyboard::Color color = {0, 0, 0};
};

// Parses profile text. Lines starting with '#' are comments, "var name
// value" defines a variable expanded as $name, and unknown commands are
// ignored — mirroring the CLI's parseProfile. Returns false and fills
// error (with a line number) on the first malformed command; valid
// commands are still returned.
// error holds the first problem (kept for existing callers); when
// allErrors is given, every bad line is appended to it, so a profile with
// several mistakes can be reported in one pass instead of one per load.
bool parseProfileText(const std::string &text,
                      std::vector<ProfileCommand> &commands,
                      std::string &error,
                      std::vector<std::string> *allErrors = NULL);

std::string serializeProfileText(const std::vector<ProfileCommand> &commands);

#endif

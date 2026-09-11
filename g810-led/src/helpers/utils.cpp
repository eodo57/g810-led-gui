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

#include "utils.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iostream>
#include <map>
#include <string>

#include "../classes/Keyboard.h"


namespace {
	
	// These parsers are fed straight from profile files and the command
	// line, so a stray character must be a parse failure, never an
	// exception: std::stoul throws, and nothing up the call chain (the
	// CLI's parseProfile, the GUI's profile loader) catches it.
	bool isHex(const std::string &val) {
		if (val.empty()) return false;
		for (size_t i = 0; i < val.length(); ++i)
			if (!std::isxdigit((unsigned char)val[i])) return false;
		return true;
	}
	
	bool isDecimal(const std::string &val) {
		if (val.empty()) return false;
		for (size_t i = 0; i < val.length(); ++i)
			if (!std::isdigit((unsigned char)val[i])) return false;
		return true;
	}
	
	unsigned long toULong(const std::string &val, int base) {
		return std::strtoul(val.c_str(), NULL, base);
	}
	
}


namespace utils {
	
	std::string getCmdName(std::string cmd) {
		return cmd.substr(cmd.find_last_of("/\\") + 1);
	}



	bool parseStartupMode(std::string val, LedKeyboard::StartupMode &startupMode) {
		if (val == "wave") startupMode = LedKeyboard::StartupMode::wave;
		else if (val == "color") startupMode = LedKeyboard::StartupMode::color;
		else return false;
		return true;
	}

	bool parseOnBoardMode(std::string val, LedKeyboard::OnBoardMode &onBoardMode) {
		if (val == "software") onBoardMode = LedKeyboard::OnBoardMode ::software;
		else if (val == "board") onBoardMode = LedKeyboard::OnBoardMode::board;
		else return false;
		return true;
	}
	
	bool parseNativeEffect(std::string val, LedKeyboard::NativeEffect &nativeEffect) {
		if (val == "color") nativeEffect = LedKeyboard::NativeEffect::color;
		else if (val == "cycle") nativeEffect = LedKeyboard::NativeEffect::cycle;
		else if (val == "breathing") nativeEffect = LedKeyboard::NativeEffect::breathing;
		else if (val == "waves") nativeEffect = LedKeyboard::NativeEffect::waves;
		else if (val == "hwave") nativeEffect = LedKeyboard::NativeEffect::hwave;
		else if (val == "vwave") nativeEffect = LedKeyboard::NativeEffect::vwave;
		else if (val == "cwave") nativeEffect = LedKeyboard::NativeEffect::cwave;
		else if (val == "off") nativeEffect = LedKeyboard::NativeEffect::off;
		else if (val == "ripple") nativeEffect = LedKeyboard::NativeEffect::ripple;
		else return false;
		return true;
	}
	
	bool parseNativeEffectPart(std::string val, LedKeyboard::NativeEffectPart &nativeEffectPart) {
		if (val == "all") nativeEffectPart = LedKeyboard::NativeEffectPart::all;
		else if (val == "keys") nativeEffectPart = LedKeyboard::NativeEffectPart::keys;
		else if (val == "logo") nativeEffectPart = LedKeyboard::NativeEffectPart::logo;
		else return false;
		return true;
	}
	
	bool parseKey(std::string val, LedKeyboard::Key &key) {
		std::transform(val.begin(), val.end(), val.begin(), ::tolower);
		if (val == "logo") key = LedKeyboard::Key::logo;
		else if (val == "logo2") key = LedKeyboard::Key::logo2;
		else if (val == "back_light" || val == "backlight" || val == "light") key = LedKeyboard::Key::backlight;
		else if (val == "game_mode" || val == "gamemode" || val == "game") key = LedKeyboard::Key::game;
		else if (val == "caps_indicator" || val == "capsindicator" || val == "caps") key = LedKeyboard::Key::caps;
		else if (val == "scroll_indicator" || val == "scrollindicator" || val == "scroll") key = LedKeyboard::Key::scroll;
		else if (val == "num_indicator" || val == "numindicator" || val == "num") key = LedKeyboard::Key::num;
		else if (val == "next") key = LedKeyboard::Key::next;
		else if (val == "prev" || val == "previous") key = LedKeyboard::Key::prev;
		else if (val == "stop") key = LedKeyboard::Key::stop;
		else if (val == "play_pause" || val == "playpause" || val == "play") key = LedKeyboard::Key::play;
		else if (val == "mute") key = LedKeyboard::Key::mute;
		else if (val == "a") key = LedKeyboard::Key::a;
		else if (val == "b") key = LedKeyboard::Key::b;
		else if (val == "c") key = LedKeyboard::Key::c;
		else if (val == "d") key = LedKeyboard::Key::d;
		else if (val == "e") key = LedKeyboard::Key::e;
		else if (val == "f") key = LedKeyboard::Key::f;
		else if (val == "g") key = LedKeyboard::Key::g;
		else if (val == "h") key = LedKeyboard::Key::h;
		else if (val == "i") key = LedKeyboard::Key::i;
		else if (val == "j") key = LedKeyboard::Key::j;
		else if (val == "k") key = LedKeyboard::Key::k;
		else if (val == "l") key = LedKeyboard::Key::l;
		else if (val == "m") key = LedKeyboard::Key::m;
		else if (val == "n") key = LedKeyboard::Key::n;
		else if (val == "o") key = LedKeyboard::Key::o;
		else if (val == "p") key = LedKeyboard::Key::p;
		else if (val == "q") key = LedKeyboard::Key::q;
		else if (val == "r") key = LedKeyboard::Key::r;
		else if (val == "s") key = LedKeyboard::Key::s;
		else if (val == "t") key = LedKeyboard::Key::t;
		else if (val == "u") key = LedKeyboard::Key::u;
		else if (val == "v") key = LedKeyboard::Key::v;
		else if (val == "w") key = LedKeyboard::Key::w;
		else if (val == "x") key = LedKeyboard::Key::x;
		else if (val == "z") key = LedKeyboard::Key::z;
		else if (val == "y") key = LedKeyboard::Key::y;
		else if (val == "1" || val == "one") key = LedKeyboard::Key::n1;
		else if (val == "2" || val == "two") key = LedKeyboard::Key::n2;
		else if (val == "3" || val == "three") key = LedKeyboard::Key::n3;
		else if (val == "4" || val == "four") key = LedKeyboard::Key::n4;
		else if (val == "5" || val == "five") key = LedKeyboard::Key::n5;
		else if (val == "6" || val == "six") key = LedKeyboard::Key::n6;
		else if (val == "7" || val == "seven") key = LedKeyboard::Key::n7;
		else if (val == "8" || val == "eight") key = LedKeyboard::Key::n8;
		else if (val == "9" || val == "nine") key = LedKeyboard::Key::n9;
		else if (val == "0" || val == "zero") key = LedKeyboard::Key::n0;
		else if (val == "enter") key = LedKeyboard::Key::enter;
		else if (val == "esc" || val == "escape") key = LedKeyboard::Key::esc;
		else if (val == "back" || val == "backspace") key = LedKeyboard::Key::backspace;
		else if (val == "tab") key = LedKeyboard::Key::tab;
		else if (val == "space") key = LedKeyboard::Key::space;
		else if (val == "tilde" || val == "~") key = LedKeyboard::Key::tilde;
		else if (val == "minus" || val == "-") key = LedKeyboard::Key::minus;
		else if (val == "equal" || val == "=") key = LedKeyboard::Key::equal;
		else if (val == "open_bracket" || val == "[") key = LedKeyboard::Key::open_bracket;
		else if (val == "close_bracket" || val == "]") key = LedKeyboard::Key::close_bracket;
		else if (val == "backslash" || val == "\\") key = LedKeyboard::Key::backslash;
		else if (val == "semicolon" || val == ";") key = LedKeyboard::Key::semicolon;
		else if (val == "quote" || val == "\"") key = LedKeyboard::Key::quote;
		else if (val == "dollar" || val == "$") key = LedKeyboard::Key::dollar;
		else if (val == "comma" || val == ",") key = LedKeyboard::Key::comma;
		else if (val == "period" || val == ".") key = LedKeyboard::Key::period;
		else if (val == "slash" || val == "/") key = LedKeyboard::Key::slash;
		else if (val == "caps_lock" || val == "capslock") key = LedKeyboard::Key::caps_lock;
		else if (val == "f1") key = LedKeyboard::Key::f1;
		else if (val == "f2") key = LedKeyboard::Key::f2;
		else if (val == "f3") key = LedKeyboard::Key::f3;
		else if (val == "f4") key = LedKeyboard::Key::f4;
		else if (val == "f5") key = LedKeyboard::Key::f5;
		else if (val == "f6") key = LedKeyboard::Key::f6;
		else if (val == "f7") key = LedKeyboard::Key::f7;
		else if (val == "f8") key = LedKeyboard::Key::f8;
		else if (val == "f9") key = LedKeyboard::Key::f9;
		else if (val == "f10") key = LedKeyboard::Key::f10;
		else if (val == "f11") key = LedKeyboard::Key::f11;
		else if (val == "f12") key = LedKeyboard::Key::f12;
		else if (val == "print_screen" || val == "printscreen" || val == "printscr" || val == "print")
			key = LedKeyboard::Key::print_screen;
		else if (val == "scroll_lock" || val == "scrolllock") key = LedKeyboard::Key::scroll_lock;
		else if (val == "pause_break" || val == "pausebreak" || val == "pause" || val == "break")
			key = LedKeyboard::Key::pause_break;
		else if (val == "insert" || val == "ins") key = LedKeyboard::Key::insert;
		else if (val == "home") key = LedKeyboard::Key::home;
		else if (val == "page_up" || val == "pageup") key = LedKeyboard::Key::page_up;
		else if (val == "delete" || val == "del") key = LedKeyboard::Key::del;
		else if (val == "end") key = LedKeyboard::Key::end;
		else if (val == "page_down" || val == "pagedown") key = LedKeyboard::Key::page_down;
		else if (val == "arrow_right" || val == "arrowright" || val == "right") key = LedKeyboard::Key::arrow_right;
		else if (val == "arrow_left" || val == "arrowleft" || val == "left") key = LedKeyboard::Key::arrow_left;
		else if (val == "arrow_bottom" || val == "arrowbottom" || val == "bottom") key = LedKeyboard::Key::arrow_bottom;
		else if (val == "arrow_top" || val == "arrowtop" || val == "top") key = LedKeyboard::Key::arrow_top;
		else if (val == "num_lock" || val == "numlock") key = LedKeyboard::Key::num_lock;
		else if (val == "num/" || val == "num_slash" || val == "numslash") key = LedKeyboard::Key::num_slash;
		else if (val == "num*" || val == "num_asterisk" || val == "numasterisk") key = LedKeyboard::Key::num_asterisk;
		else if (val == "num-" || val == "num_minus" || val == "numminus") key = LedKeyboard::Key::num_minus;
		else if (val == "num+" || val == "num_plus" || val == "numplus") key = LedKeyboard::Key::num_plus;
		else if (val == "numenter") key = LedKeyboard::Key::num_enter;
		else if (val == "num1") key = LedKeyboard::Key::num_1;
		else if (val == "num2") key = LedKeyboard::Key::num_2;
		else if (val == "num3") key = LedKeyboard::Key::num_3;
		else if (val == "num4") key = LedKeyboard::Key::num_4;
		else if (val == "num5") key = LedKeyboard::Key::num_5;
		else if (val == "num6") key = LedKeyboard::Key::num_6;
		else if (val == "num7") key = LedKeyboard::Key::num_7;
		else if (val == "num8") key = LedKeyboard::Key::num_8;
		else if (val == "num9") key = LedKeyboard::Key::num_9;
		else if (val == "num0") key = LedKeyboard::Key::num_0;
		else if (val == "num." || val == "num_period" || val == "numperiod") key = LedKeyboard::Key::num_dot;
		else if (val == "intl_backslash" || val == "<") key = LedKeyboard::Key::intl_backslash;
		else if (val == "menu") key = LedKeyboard::Key::menu;
		else if (val == "abnt_slash" || val == "abnt_c1") key = LedKeyboard::Key::abnt_slash;
		else if (val == "ctrl_left" || val == "ctrlleft" || val == "ctrll") key = LedKeyboard::Key::ctrl_left;
		else if (val == "shift_left" || val == "shiftleft" || val == "shiftl") key = LedKeyboard::Key::shift_left;
		else if (val == "alt_left" || val == "altleft" || val == "altl") key = LedKeyboard::Key::alt_left;
		else if (val == "win_left" || val == "winleft" || val == "winl") key = LedKeyboard::Key::win_left;
		else if (val == "meta_left" || val == "metaleft" || val == "metal") key = LedKeyboard::Key::win_left;
		else if (val == "ctrl_right" || val == "ctrlright" || val == "ctrlr") key = LedKeyboard::Key::ctrl_right;
		else if (val == "shift_right" || val == "shiftright" || val == "shiftr") key = LedKeyboard::Key::shift_right;
		else if (val == "alt_right" || val == "altright" || val == "altr" || val == "altgr")
			key = LedKeyboard::Key::alt_right;
		else if (val == "win_right" || val == "winright" || val == "winr") key = LedKeyboard::Key::win_right;
		else if (val == "meta_right" || val == "metaright" || val == "metar") key = LedKeyboard::Key::win_right;
		else if (val == "g1") key = LedKeyboard::Key::g1;
		else if (val == "g2") key = LedKeyboard::Key::g2;
		else if (val == "g3") key = LedKeyboard::Key::g3;
		else if (val == "g4") key = LedKeyboard::Key::g4;
		else if (val == "g5") key = LedKeyboard::Key::g5;
		else if (val == "g6") key = LedKeyboard::Key::g6;
		else if (val == "g7") key = LedKeyboard::Key::g7;
		else if (val == "g8") key = LedKeyboard::Key::g8;
		else if (val == "g9") key = LedKeyboard::Key::g9;
		else return false;
		return true;
	}

	std::string keyName(LedKeyboard::Key key) {
		static const std::map<LedKeyboard::Key, const char *> names = {
			{LedKeyboard::Key::logo, "logo"},
			{LedKeyboard::Key::logo2, "logo2"},
			{LedKeyboard::Key::backlight, "backlight"},
			{LedKeyboard::Key::game, "game"},
			{LedKeyboard::Key::caps, "caps"},
			{LedKeyboard::Key::scroll, "scroll"},
			{LedKeyboard::Key::num, "num"},
			{LedKeyboard::Key::next, "next"},
			{LedKeyboard::Key::prev, "prev"},
			{LedKeyboard::Key::stop, "stop"},
			{LedKeyboard::Key::play, "play"},
			{LedKeyboard::Key::mute, "mute"},
			{LedKeyboard::Key::a, "a"}, {LedKeyboard::Key::b, "b"},
			{LedKeyboard::Key::c, "c"}, {LedKeyboard::Key::d, "d"},
			{LedKeyboard::Key::e, "e"}, {LedKeyboard::Key::f, "f"},
			{LedKeyboard::Key::g, "g"}, {LedKeyboard::Key::h, "h"},
			{LedKeyboard::Key::i, "i"}, {LedKeyboard::Key::j, "j"},
			{LedKeyboard::Key::k, "k"}, {LedKeyboard::Key::l, "l"},
			{LedKeyboard::Key::m, "m"}, {LedKeyboard::Key::n, "n"},
			{LedKeyboard::Key::o, "o"}, {LedKeyboard::Key::p, "p"},
			{LedKeyboard::Key::q, "q"}, {LedKeyboard::Key::r, "r"},
			{LedKeyboard::Key::s, "s"}, {LedKeyboard::Key::t, "t"},
			{LedKeyboard::Key::u, "u"}, {LedKeyboard::Key::v, "v"},
			{LedKeyboard::Key::w, "w"}, {LedKeyboard::Key::x, "x"},
			{LedKeyboard::Key::y, "y"}, {LedKeyboard::Key::z, "z"},
			{LedKeyboard::Key::n1, "1"}, {LedKeyboard::Key::n2, "2"},
			{LedKeyboard::Key::n3, "3"}, {LedKeyboard::Key::n4, "4"},
			{LedKeyboard::Key::n5, "5"}, {LedKeyboard::Key::n6, "6"},
			{LedKeyboard::Key::n7, "7"}, {LedKeyboard::Key::n8, "8"},
			{LedKeyboard::Key::n9, "9"}, {LedKeyboard::Key::n0, "0"},
			{LedKeyboard::Key::enter, "enter"},
			{LedKeyboard::Key::esc, "esc"},
			{LedKeyboard::Key::backspace, "backspace"},
			{LedKeyboard::Key::tab, "tab"},
			{LedKeyboard::Key::space, "space"},
			{LedKeyboard::Key::tilde, "tilde"},
			{LedKeyboard::Key::minus, "minus"},
			{LedKeyboard::Key::equal, "equal"},
			{LedKeyboard::Key::open_bracket, "open_bracket"},
			{LedKeyboard::Key::close_bracket, "close_bracket"},
			{LedKeyboard::Key::backslash, "backslash"},
			{LedKeyboard::Key::semicolon, "semicolon"},
			{LedKeyboard::Key::quote, "quote"},
			{LedKeyboard::Key::dollar, "dollar"},
			{LedKeyboard::Key::comma, "comma"},
			{LedKeyboard::Key::period, "period"},
			{LedKeyboard::Key::slash, "slash"},
			{LedKeyboard::Key::caps_lock, "caps_lock"},
			{LedKeyboard::Key::f1, "f1"}, {LedKeyboard::Key::f2, "f2"},
			{LedKeyboard::Key::f3, "f3"}, {LedKeyboard::Key::f4, "f4"},
			{LedKeyboard::Key::f5, "f5"}, {LedKeyboard::Key::f6, "f6"},
			{LedKeyboard::Key::f7, "f7"}, {LedKeyboard::Key::f8, "f8"},
			{LedKeyboard::Key::f9, "f9"}, {LedKeyboard::Key::f10, "f10"},
			{LedKeyboard::Key::f11, "f11"}, {LedKeyboard::Key::f12, "f12"},
			{LedKeyboard::Key::print_screen, "print_screen"},
			{LedKeyboard::Key::scroll_lock, "scroll_lock"},
			{LedKeyboard::Key::pause_break, "pause_break"},
			{LedKeyboard::Key::insert, "insert"},
			{LedKeyboard::Key::home, "home"},
			{LedKeyboard::Key::page_up, "page_up"},
			{LedKeyboard::Key::del, "del"},
			{LedKeyboard::Key::end, "end"},
			{LedKeyboard::Key::page_down, "page_down"},
			{LedKeyboard::Key::arrow_right, "arrow_right"},
			{LedKeyboard::Key::arrow_left, "arrow_left"},
			{LedKeyboard::Key::arrow_bottom, "arrow_bottom"},
			{LedKeyboard::Key::arrow_top, "arrow_top"},
			{LedKeyboard::Key::num_lock, "num_lock"},
			{LedKeyboard::Key::num_slash, "num_slash"},
			{LedKeyboard::Key::num_asterisk, "num_asterisk"},
			{LedKeyboard::Key::num_minus, "num_minus"},
			{LedKeyboard::Key::num_plus, "num_plus"},
			{LedKeyboard::Key::num_enter, "numenter"},
			{LedKeyboard::Key::num_1, "num1"}, {LedKeyboard::Key::num_2, "num2"},
			{LedKeyboard::Key::num_3, "num3"}, {LedKeyboard::Key::num_4, "num4"},
			{LedKeyboard::Key::num_5, "num5"}, {LedKeyboard::Key::num_6, "num6"},
			{LedKeyboard::Key::num_7, "num7"}, {LedKeyboard::Key::num_8, "num8"},
			{LedKeyboard::Key::num_9, "num9"}, {LedKeyboard::Key::num_0, "num0"},
			{LedKeyboard::Key::num_dot, "num_period"},
			{LedKeyboard::Key::intl_backslash, "intl_backslash"},
			{LedKeyboard::Key::menu, "menu"},
			{LedKeyboard::Key::abnt_slash, "abnt_slash"},
			{LedKeyboard::Key::ctrl_left, "ctrl_left"},
			{LedKeyboard::Key::shift_left, "shift_left"},
			{LedKeyboard::Key::alt_left, "alt_left"},
			{LedKeyboard::Key::win_left, "win_left"},
			{LedKeyboard::Key::ctrl_right, "ctrl_right"},
			{LedKeyboard::Key::shift_right, "shift_right"},
			{LedKeyboard::Key::alt_right, "alt_right"},
			{LedKeyboard::Key::win_right, "win_right"},
			{LedKeyboard::Key::g1, "g1"}, {LedKeyboard::Key::g2, "g2"},
			{LedKeyboard::Key::g3, "g3"}, {LedKeyboard::Key::g4, "g4"},
			{LedKeyboard::Key::g5, "g5"}, {LedKeyboard::Key::g6, "g6"},
			{LedKeyboard::Key::g7, "g7"}, {LedKeyboard::Key::g8, "g8"},
			{LedKeyboard::Key::g9, "g9"}
		};
		std::map<LedKeyboard::Key, const char *>::const_iterator it = names.find(key);
		return it == names.end() ? "" : it->second;
	}
	
	bool parseKeyGroup(std::string val, LedKeyboard::KeyGroup &keyGroup) {
		if (val == "logo") keyGroup = LedKeyboard::KeyGroup::logo;
		else if (val == "indicators") keyGroup = LedKeyboard::KeyGroup::indicators;
		else if (val == "multimedia") keyGroup = LedKeyboard::KeyGroup::multimedia;
		else if (val == "fkeys") keyGroup = LedKeyboard::KeyGroup::fkeys;
		else if (val == "modifiers") keyGroup = LedKeyboard::KeyGroup::modifiers;
		else if (val == "arrows") keyGroup = LedKeyboard::KeyGroup::arrows;
		else if (val == "numeric") keyGroup = LedKeyboard::KeyGroup::numeric;
		else if (val == "functions") keyGroup = LedKeyboard::KeyGroup::functions;
		else if (val == "keys") keyGroup = LedKeyboard::KeyGroup::keys;
		else if (val == "gkeys") keyGroup = LedKeyboard::KeyGroup::gkeys;
		else return false;
		return true;
	}
	
	bool parseColor(std::string val, LedKeyboard::Color &color) {
		if (val.length() == 2) val = val + "0000";  // For G610
		if (val.length() != 6) return false;
		if (!isHex(val)) return false;
		color.red = (uint8_t)toULong(val.substr(0,2), 16);
		color.green = (uint8_t)toULong(val.substr(2,2), 16);
		color.blue = (uint8_t)toULong(val.substr(4,2), 16);
		return true;
	}
	
	bool parsePeriod(std::string val, std::chrono::duration<uint16_t, std::milli> &period) {
		// "<n>ms", "<n>s", or the CLI's two-digit hex byte (in 256 ms units).
		if (!val.empty() && val.back() == 's') {
			bool milli = (val.length() >= 2) && (val[val.length()-2] == 'm');
			std::string digits = val.substr(0, val.length() - (milli ? 2 : 1));
			if (!isDecimal(digits)) return false;
			unsigned long value = toULong(digits, 10);
			if (!milli) {
				if (value > 65) return false;  // would overflow the uint16_t ms
				value *= 1000;
			}
			if (value > 65535) return false;
			period = std::chrono::milliseconds(value);
		} else {
			if (val.length() == 1) val = "0" + val;
			if (val.length() != 2) return false;
			if (!isHex(val)) return false;
			period = std::chrono::milliseconds(toULong(val, 16) << 8);
		}
		return true;
	}
	
	bool parseUInt8(std::string val, uint8_t &uint8) {
		if (val.length() == 1) val = "0" + val;
		if (val.length() != 2) return false;
		if (!isHex(val)) return false;
		uint8 = (uint8_t)toULong(val, 16);
		return true;
	}
	
	bool parseUInt16(std::string val, uint16_t &uint16) {
		if (val.length() == 1) val = "0" + val;
		if (val.length() == 2) val = "0" + val;
		if (val.length() == 3) val = "0" + val;
		if (val.length() != 4) return false;
		if (!isHex(val)) return false;
		uint16 = (uint16_t)toULong(val, 16);
		return true;
	}
	
}

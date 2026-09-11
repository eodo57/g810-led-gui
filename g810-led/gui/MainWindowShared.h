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

#ifndef MAIN_WINDOW_SHARED
#define MAIN_WINDOW_SHARED

#include <gtkmm.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

#include "../src/classes/Keyboard.h"

// The window holds colors as Gdk::RGBA and the device speaks 8-bit RGB,
// so every panel converts between the two. One definition of the rounding
// keeps a color that survives the round trip in one panel from shifting
// by a unit in another.

inline std::string rgbaToHex(const Gdk::RGBA &rgba) {
	char buffer[8];
	std::snprintf(buffer, sizeof(buffer), "%02x%02x%02x",
		(int)std::round(rgba.get_red() * 255.0),
		(int)std::round(rgba.get_green() * 255.0),
		(int)std::round(rgba.get_blue() * 255.0));
	return std::string(buffer);
}

inline int rgbaChannel(const Gdk::RGBA &rgba, int channel) {
	double value = channel == 0 ? rgba.get_red() :
		channel == 1 ? rgba.get_green() : rgba.get_blue();
	return (int)std::round(value * 255.0);
}

inline bool rgbaEqual(const Gdk::RGBA &a, const Gdk::RGBA &b) {
	return rgbaChannel(a, 0) == rgbaChannel(b, 0) &&
	       rgbaChannel(a, 1) == rgbaChannel(b, 1) &&
	       rgbaChannel(a, 2) == rgbaChannel(b, 2);
}

inline Gdk::RGBA solidRGBA(const Gdk::RGBA &rgba) {
	Gdk::RGBA result;
	result.set_rgba(rgba.get_red(), rgba.get_green(), rgba.get_blue(), 1.0);
	return result;
}

inline LedKeyboard::Color toLedColor(const Gdk::RGBA &rgba) {
	LedKeyboard::Color color;
	color.red = (uint8_t)rgbaChannel(rgba, 0);
	color.green = (uint8_t)rgbaChannel(rgba, 1);
	color.blue = (uint8_t)rgbaChannel(rgba, 2);
	return color;
}

inline Gdk::RGBA fromLedColor(const LedKeyboard::Color &color) {
	Gdk::RGBA rgba;
	rgba.set_rgba(color.red / 255.0, color.green / 255.0,
	              color.blue / 255.0, 1.0);
	return rgba;
}

#endif

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

#ifndef KEY_LAYOUT
#define KEY_LAYOUT

#include <string>
#include <vector>

#include "../src/classes/Keyboard.h"

// The shape of a keyboard, in quarter key units (1u = 4), shared by the
// widget grid that lays it out and the scene that extrudes it. One
// quarter unit is 19.05/4 mm, so these numbers are the real thing.
namespace keylayout {

	struct KeySpec {
		const char *name;    // CLI key name ("" = spacer)
		const char *label;   // legend
		int width;           // quarter units
		int height;          // rows
		bool placeholder;    // drawn for looks, not a controllable key
	};

	struct KeyRow { std::vector<KeySpec> keys; };

	// One cap, placed: the layout resolved down to what a renderer
	// needs, with the key names already turned into ids.
	struct Cap {
		LedKeyboard::Key key = LedKeyboard::Key::a;
		bool controllable = false;  // false: a placeholder, drawn but dead
		bool indicator = false;     // a lamp in the plate, not a keycap
		std::string label;
		std::string name;           // the CLI key name, for tooltips
		int row = 0;                // row index
		int column = 0;             // quarter units from the left
		int width = 4;              // quarter units
		int height = 1;             // rows
	};

}

#endif

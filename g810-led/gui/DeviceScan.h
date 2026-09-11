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

#ifndef GUI_DEVICE_SCAN
#define GUI_DEVICE_SCAN

#include <string>
#include <vector>

// What else is in the machine.
//
// This window drives one keyboard. Everything else — the memory, the other
// USB devices, whatever Bluetooth knows about — it could not so much as
// see, which made "is my hardware supported?" a question the application
// had no way to answer.
//
// Everything here is a read. Nothing is written to any bus. That is a
// deliberate limit rather than an unfinished one: lighting the Corsair
// DIMMs would mean raw SMBus writes at 0x58-0x5f, and on an AMD piix4 bus
// that risks hanging the machine, while a write to a wrong address can
// corrupt an SPD EEPROM and cost a stick. Looking costs none of that, and
// looking is what was asked for.
//
// No GTK in here, so the inventory can be run and checked without a
// screen.
namespace devicescan {

	struct Found {
		enum Kind {
			Keyboard,    // the thing this application exists to drive
			Memory,      // a DIMM, read from the SPD the kernel exposes
			Usb,         // a USB/HID device from a maker who ships lighting
			Bluetooth    // whatever BlueZ already knows about
		};

		Kind kind = Usb;
		std::string name;     // "Corsair Vengeance RGB Pro"
		std::string detail;   // "16 GB DDR4 · CMW32GX4M2E3200C16"
		std::string where;    // "DIMM 0 · i2c-6 0x50"
		// True only for what this application can actually drive. The list
		// is mostly things it cannot, and saying so is the point: a list
		// that implies otherwise is worse than no list.
		bool controllable = false;
	};

	// Everything found, keyboards first. Sources that are absent or
	// unreadable contribute nothing and are not errors — a machine with no
	// Bluetooth is a normal machine.
	std::vector<Found> scan();

	// What each source had to say for itself, for the times when the
	// interesting answer is why something is missing.
	struct Notes {
		std::string memory;      // "" when four DIMMs were read
		std::string bluetooth;   // "" when BlueZ answered
	};
	std::vector<Found> scan(Notes &notes);

	const char *kindName(Found::Kind kind);

}

#endif

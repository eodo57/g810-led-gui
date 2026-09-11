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

#include "DeviceScan.h"

#include <giomm.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <map>

#include "../src/classes/Keyboard.h"
#include "../src/helpers/help.h"

#ifdef hidapi
	#include "hidapi/hidapi.h"
#endif

namespace {

std::string trimmed(const std::string &text) {
	size_t first = text.find_first_not_of(" \t\r\n");
	if (first == std::string::npos)
		return std::string();
	size_t last = text.find_last_not_of(" \t\r\n");
	return text.substr(first, last - first + 1);
}

std::string readFile(const std::string &path) {
	std::ifstream file(path);
	if (!file.is_open())
		return std::string();
	std::string text;
	std::getline(file, text);
	return trimmed(text);
}

// --- Memory ---------------------------------------------------------
//
// The kernel already does the hard part. ee1004 (DDR4) and spd5118 (DDR5)
// bind to the SPD EEPROMs and publish them as a plain file, world
// readable, so the sticks can be identified without root, without
// i2c-dev, and without this program going anywhere near the bus itself.

// The first byte of a JEDEC manufacturer ID is a continuation count and
// the second identifies the maker. Only the makers likely to turn up in a
// machine someone is lighting are here; anything else is reported by its
// number rather than guessed at.
const char *memoryMaker(unsigned char id) {
	switch (id) {
		case 0x2c: return "Micron";
		case 0x80: return "Kingston";
		case 0x98: return "Kingston";
		case 0xce: return "Samsung";
		case 0xad: return "SK Hynix";
		case 0x9e: return "Corsair";
		case 0xc1: return "Corsair";
		case 0x8c: return "Crucial";
		case 0x4f: return "G.Skill";
		case 0xcd: return "G.Skill";
		case 0xda: return "TeamGroup";
		case 0x0b: return "Nanya";
		case 0xfe: return "Elpida";
		case 0x1e: return "Corsair";
		default:   return NULL;
	}
}

// DDR4 capacity, from the three bytes that describe the chips and how
// many of them there are. JEDEC gives this as a formula rather than a
// field, so it is spelled out here.
long ddr4Megabytes(const std::string &spd) {
	static const long densityMbit[] = {
		256, 512, 1024, 2048, 4096, 8192, 16384, 32768
	};
	const int densityCode = (unsigned char)spd[4] & 0x0f;
	if (densityCode >= (int)(sizeof(densityMbit) / sizeof(densityMbit[0])))
		return 0;
	const int deviceWidth = 4 << ((unsigned char)spd[12] & 0x07);
	const int ranks = (((unsigned char)spd[12] >> 3) & 0x07) + 1;
	const int busWidth = 8 << ((unsigned char)spd[13] & 0x07);
	if (deviceWidth <= 0 || busWidth <= 0)
		return 0;
	return densityMbit[densityCode] / 8 * (busWidth / deviceWidth) * ranks;
}

std::string humanSize(long megabytes) {
	if (megabytes <= 0)
		return std::string();
	char buffer[32];
	if (megabytes % 1024 == 0)
		std::snprintf(buffer, sizeof(buffer), "%ld GB", megabytes / 1024);
	else
		std::snprintf(buffer, sizeof(buffer), "%ld MB", megabytes);
	return buffer;
}

// The part number as the maker printed it on the label, which is the
// string the owner will recognise.
std::string asciiField(const std::string &spd, size_t from, size_t length) {
	if (spd.size() < from + length)
		return std::string();
	std::string text;
	for (size_t i = from; i < from + length; ++i) {
		const char c = spd[i];
		if (c >= 0x20 && c < 0x7f)
			text += c;
	}
	return trimmed(text);
}

void scanMemory(std::vector<devicescan::Found> &found, std::string &note) {
	DIR *dir = opendir("/sys/bus/i2c/devices");
	if (!dir) {
		note = "This system does not expose an I2C bus, so the memory "
			"cannot be identified.";
		return;
	}
	std::vector<std::string> names;
	while (struct dirent *entry = readdir(dir))
		if (entry->d_name[0] != '.')
			names.push_back(entry->d_name);
	closedir(dir);
	std::sort(names.begin(), names.end());

	int unreadable = 0;
	for (size_t i = 0; i < names.size(); ++i) {
		const std::string base = "/sys/bus/i2c/devices/" + names[i];
		std::ifstream eeprom(base + "/eeprom", std::ios::binary);
		if (!eeprom.is_open())
			continue;
		std::string spd((std::istreambuf_iterator<char>(eeprom)),
			std::istreambuf_iterator<char>());
		if (spd.size() < 4) {
			++unreadable;
			continue;
		}

		// Where on the board, from the address: the SPD of the first DIMM
		// answers at 0x50 and they run up from there.
		std::string where = names[i];
		const size_t dash = names[i].find('-');
		if (dash != std::string::npos && names[i].size() >= dash + 5) {
			const long address = strtol(names[i].substr(dash + 1).c_str(),
				NULL, 16);
			if (address >= 0x50 && address <= 0x57) {
				char slot[48];
				std::snprintf(slot, sizeof(slot), "DIMM %ld · i2c-%s 0x%02lx",
					address - 0x50, names[i].substr(0, dash).c_str(), address);
				where = slot;
			}
		}

		devicescan::Found entry;
		entry.kind = devicescan::Found::Memory;
		entry.where = where;

		const unsigned char type = (unsigned char)spd[2];
		if (type == 0x0c && spd.size() >= 349) {
			// DDR4. The fields worth having are the maker, the part number
			// and the size.
			const unsigned char makerId = (unsigned char)spd[321];
			const char *maker = memoryMaker(makerId);
			const std::string part = asciiField(spd, 329, 20);
			char fallback[32];
			std::snprintf(fallback, sizeof(fallback), "Module 0x%02x", makerId);
			entry.name = maker ? maker : fallback;
			entry.detail = humanSize(ddr4Megabytes(spd));
			entry.detail += entry.detail.empty() ? "DDR4" : " DDR4";
			if (!part.empty())
				entry.detail += " · " + part;
			// The speed is deliberately absent. What the SPD carries is the
			// JEDEC base rate, which on a kit sold as 3200 or 3600 reads
			// 2133 — the advertised speed lives in an XMP profile the BIOS
			// may or may not be using. Printing the base rate beside a part
			// number that says 3200 would look like a fault in this
			// program, and printing the XMP rate would be claiming to know
			// what the BIOS chose. The part number already says what the
			// sticks are.
		} else if (type == 0x0b && spd.size() >= 146) {
			entry.name = "Memory module";
			const std::string part = asciiField(spd, 128, 18);
			entry.detail = part.empty() ? "DDR3" : "DDR3 · " + part;
		} else {
			// Say what is known and stop. A guess dressed up as a reading
			// is worse than an admission.
			char what[64];
			std::snprintf(what, sizeof(what),
				type == 0x12 ? "DDR5 module · not decoded here" :
				"Memory module · SPD type 0x%02x not decoded here", type);
			entry.name = "Memory module";
			entry.detail = what;
		}
		found.push_back(entry);
	}

	if (found.empty() && unreadable == 0)
		note = "No memory SPD is readable. The kernel publishes it through "
			"the ee1004 or spd5118 driver; without one of those loaded "
			"there is nothing to read.";
}

// --- USB ------------------------------------------------------------

const char *usbMaker(unsigned short vendor) {
	switch (vendor) {
		case 0x046d: return "Logitech";
		case 0x1b1c: return "Corsair";
		case 0x1532: return "Razer";
		case 0x1038: return "SteelSeries";
		case 0x0b05: return "ASUS";
		case 0x1462: return "MSI";
		case 0x1e71: return "NZXT";
		case 0x2516: return "Cooler Master";
		case 0x0951: return "HyperX";
		case 0x03f0: return "HP / HyperX";
		case 0x1e7d: return "ROCCAT";
		case 0x04d9: return "Ducky";
		case 0x258a: return "Glorious";
		case 0x31e3: return "Wooting";
		case 0x3633: return "Corsair";
		default:     return NULL;
	}
}

// A receiver is worth naming as one: it is a radio, not the thing being
// lit, and what is paired behind it cannot be listed without asking it
// over HID++ — a conversation this inventory does not have.
bool logitechReceiver(unsigned short product) {
	return product == 0xc52b || product == 0xc532 || product == 0xc534 ||
	       product == 0xc539 || product == 0xc53a || product == 0xc53f ||
	       product == 0xc541 || product == 0xc547 || product == 0xc548;
}

void scanUsb(std::vector<devicescan::Found> &found,
             const std::vector<LedKeyboard::DeviceInfo> &keyboards) {
#ifdef hidapi
	hid_device_info *devices = hid_enumerate(0x0, 0x0);
	// One entry per device, not per interface: a keyboard commonly offers
	// three or four and listing each would say the machine has four
	// keyboards.
	std::map<unsigned, bool> seen;
	for (hid_device_info *device = devices; device; device = device->next) {
		const unsigned key = ((unsigned)device->vendor_id << 16) |
			device->product_id;
		if (seen.count(key))
			continue;
		seen[key] = true;

		const char *maker = usbMaker(device->vendor_id);
		if (!maker)
			continue;   // a mouse from a maker who ships no lighting

		// The keyboards are listed in their own right; do not say them twice.
		bool alreadyListed = false;
		for (size_t i = 0; i < keyboards.size(); ++i)
			if (keyboards[i].vendorID == device->vendor_id &&
			    keyboards[i].productID == device->product_id)
				alreadyListed = true;
		if (alreadyListed)
			continue;

		devicescan::Found entry;
		entry.kind = devicescan::Found::Usb;
		entry.name = maker;
		if (device->product_string) {
			const std::wstring wide(device->product_string);
			entry.name += " " + std::string(wide.begin(), wide.end());
		}
		if (device->vendor_id == 0x046d &&
		    (device->product_id == 0x407c || device->product_id == 0xb354)) {
			// A G915, on its Lightspeed receiver or over Bluetooth. It is
			// not in the drivable table and the reason is worth stating
			// where the user meets it: both connections take every
			// lighting write and the keyboard goes on showing its own
			// on-board preset.
			entry.name = "Logitech G915 keyboard";
			entry.detail = "Recognised, but this application cannot light "
				"it yet — its lighting packets need the wireless device "
				"index, which is not implemented";
		} else if (device->vendor_id == 0x046d &&
		           logitechReceiver(device->product_id))
			entry.detail = "Wireless receiver — what is paired behind it is "
				"not listed here";
		else
			entry.detail = "Made by someone who ships lighting; this "
				"application cannot drive it";
		char where[32];
		std::snprintf(where, sizeof(where), "usb %04x:%04x",
			device->vendor_id, device->product_id);
		entry.where = where;
		found.push_back(entry);
	}
	if (devices)
		hid_free_enumeration(devices);
#else
	(void)found;
	(void)keyboards;
#endif
}

// --- Bluetooth ------------------------------------------------------
//
// BlueZ keeps everything it knows in one object tree, so one call to the
// object manager answers the whole question. Nothing here starts a
// discovery: that is an action rather than a look, and none of these can
// be lit from this program anyway.

void scanBluetooth(std::vector<devicescan::Found> &found, std::string &note) {
	try {
		Glib::RefPtr<Gio::DBus::Connection> bus =
			Gio::DBus::Connection::get_sync(Gio::DBus::BUS_TYPE_SYSTEM);
		if (!bus) {
			note = "No system bus, so Bluetooth could not be asked.";
			return;
		}
		Glib::VariantContainerBase reply = bus->call_sync(
			"/", "org.freedesktop.DBus.ObjectManager", "GetManagedObjects",
			Glib::VariantContainerBase(), "org.bluez",
			2000);

		typedef std::map<Glib::ustring,
			std::map<Glib::ustring, Glib::VariantBase>> Interfaces;
		std::map<Glib::DBusObjectPathString, Interfaces> objects;
		Glib::Variant<std::map<Glib::DBusObjectPathString, Interfaces>> got;
		reply.get_child(got, 0);
		objects = got.get();

		int adapters = 0;
		for (std::map<Glib::DBusObjectPathString, Interfaces>::const_iterator
		     object = objects.begin(); object != objects.end(); ++object) {
			const Interfaces &interfaces = object->second;

			Interfaces::const_iterator adapter =
				interfaces.find("org.bluez.Adapter1");
			if (adapter != interfaces.end()) {
				++adapters;
				devicescan::Found entry;
				entry.kind = devicescan::Found::Bluetooth;
				entry.name = "Bluetooth adapter";
				std::map<Glib::ustring, Glib::VariantBase>::const_iterator
					name = adapter->second.find("Name");
				if (name != adapter->second.end())
					entry.name = "Bluetooth · " +
						Glib::VariantBase::cast_dynamic<
							Glib::Variant<Glib::ustring>>(name->second)
							.get().raw();
				std::map<Glib::ustring, Glib::VariantBase>::const_iterator
					address = adapter->second.find("Address");
				if (address != adapter->second.end())
					entry.where = Glib::VariantBase::cast_dynamic<
						Glib::Variant<Glib::ustring>>(address->second)
						.get().raw();
				entry.detail = "The radio itself";
				found.push_back(entry);
				continue;
			}

			Interfaces::const_iterator device =
				interfaces.find("org.bluez.Device1");
			if (device == interfaces.end())
				continue;
			devicescan::Found entry;
			entry.kind = devicescan::Found::Bluetooth;
			entry.name = "Bluetooth device";
			std::map<Glib::ustring, Glib::VariantBase>::const_iterator field =
				device->second.find("Name");
			if (field != device->second.end())
				entry.name = Glib::VariantBase::cast_dynamic<
					Glib::Variant<Glib::ustring>>(field->second).get().raw();
			field = device->second.find("Address");
			if (field != device->second.end())
				entry.where = Glib::VariantBase::cast_dynamic<
					Glib::Variant<Glib::ustring>>(field->second).get().raw();
			bool connected = false, paired = false;
			field = device->second.find("Connected");
			if (field != device->second.end())
				connected = Glib::VariantBase::cast_dynamic<
					Glib::Variant<bool>>(field->second).get();
			field = device->second.find("Paired");
			if (field != device->second.end())
				paired = Glib::VariantBase::cast_dynamic<
					Glib::Variant<bool>>(field->second).get();
			entry.detail = connected ? "Connected" :
				(paired ? "Paired, not connected" : "Known, not paired");
			found.push_back(entry);
		}
		if (adapters == 0)
			note = "BlueZ is running but this machine has no Bluetooth "
				"adapter.";
	} catch (const Glib::Error &error) {
		// A machine without Bluetooth is a normal machine; so is one where
		// bluetoothd is not running. Neither is a fault to report.
		note = std::string("Bluetooth could not be asked: ") + error.what();
	}
}

}  // namespace

const char *devicescan::kindName(Found::Kind kind) {
	switch (kind) {
		case Found::Keyboard:  return "Keyboards";
		case Found::Memory:    return "Memory";
		case Found::Usb:       return "USB devices";
		case Found::Bluetooth: return "Bluetooth";
	}
	return "Other";
}

std::vector<devicescan::Found> devicescan::scan(Notes &notes) {
	std::vector<Found> found;
	notes = Notes();

	// The keyboards first: they are the reason this window exists, and the
	// only entries that will say yes to being driven.
	LedKeyboard probe;
	const std::vector<LedKeyboard::DeviceInfo> keyboards = probe.listKeyboards();
	std::map<unsigned, bool> listed;
	for (size_t i = 0; i < keyboards.size(); ++i) {
		// One entry per keyboard, not per HID interface. A G512 offers
		// three, and a list that repeated it three times would be
		// reporting the shape of the USB descriptor rather than what is
		// on the desk.
		const unsigned key = ((unsigned)keyboards[i].vendorID << 16) |
			keyboards[i].productID;
		if (listed.count(key))
			continue;
		listed[key] = true;

		Found entry;
		entry.kind = Found::Keyboard;
		// The product string usually carries the maker already ("G915
		// KEYBOARD" from Logitech), so only prefix a maker that adds
		// something rather than manufacturing "Keyboard G915 KEYBOARD".
		entry.name = keyboards[i].product.empty() ?
			std::string("Keyboard") : keyboards[i].product;
		if (!keyboards[i].manufacturer.empty() &&
		    entry.name.find(keyboards[i].manufacturer) == std::string::npos)
			entry.name = keyboards[i].manufacturer + " " + entry.name;
		char where[64];
		std::snprintf(where, sizeof(where), "%s %04x:%04x",
			keyboards[i].bluetooth ? "bluetooth" : "usb",
			keyboards[i].vendorID, keyboards[i].productID);
		entry.where = where;

		// listKeyboards() reports what the bus says and leaves the model
		// blank — that is worked out on open(), which this inventory has
		// no business doing. The same table open() consults answers it
		// here without touching the device.
		LedKeyboard::KeyboardModel model = keyboards[i].model;
		for (size_t k = 0; k < probe.SupportedKeyboards.size(); ++k) {
			if (probe.SupportedKeyboards[k].size() < 3)
				continue;
			if (probe.SupportedKeyboards[k][0] == keyboards[i].vendorID &&
			    probe.SupportedKeyboards[k][1] == keyboards[i].productID) {
				model = (LedKeyboard::KeyboardModel)probe.SupportedKeyboards[k][2];
				break;
			}
		}
		const bool known = model != LedKeyboard::KeyboardModel::unknown &&
			help::getKeyboardFeatures(model) != help::KeyboardFeatures::none;
		// Known is not the same as reachable. Over Bluetooth these
		// keyboards take every lighting write and act on none of them, so
		// a list that called this one controllable would be repeating the
		// keyboard's own lie.
		entry.controllable = known && !keyboards[i].bluetooth;
		if (keyboards[i].bluetooth)
			entry.detail = "Over Bluetooth, which carries typing but not "
				"lighting — connect it by USB or its Lightspeed receiver";
		else
			entry.detail = known ? "This application can light this one" :
				"Found, but this application has no protocol for it";
		found.push_back(entry);
	}

	scanMemory(found, notes.memory);
	scanUsb(found, keyboards);
	scanBluetooth(found, notes.bluetooth);
	return found;
}

std::vector<devicescan::Found> devicescan::scan() {
	Notes ignored;
	return scan(ignored);
}

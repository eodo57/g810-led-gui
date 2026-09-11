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

#include "MainWindow.h"

#include <cctype>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include <unistd.h>

#if defined(hidapi)
	#include <hidapi/hidapi.h>
#endif

#include "MainWindowShared.h"
#include "DeviceScan.h"

// The Device tab: which keyboard this window talks to, what it says when
// there is not one, and the settings that live on the board itself — M/G
// keys, the startup mode and which side controls the lighting.
//
// Most of this file is the first thing a new user sees. There are several
// separate ways to arrive here with nothing to light up — nothing plugged
// in, a keyboard this build does not support, one the system will not let
// us open, and one another program already has — and each needs its own
// sentence, because each has its own fix. Two of them also end by
// themselves, and the panel waits for that rather than making the user
// come back and press a button to be told what it could see for itself.

// USB ids are entered as hex in the manual device fields, so every place
// that shows one to the user has to print hex too — decimal cannot be
// typed back in.
static std::string hexId(uint16_t value) {
	char buffer[8];
	std::snprintf(buffer, sizeof(buffer), "%04x", value);
	return std::string(buffer);
}

namespace {

// The rule that lets a desktop user open the keyboard. `make install`
// writes the first of these (see the makefile's `setup` target); a
// distribution package usually puts its copy in one of the others, and a
// window that told those users to install a rule they already have would
// be sending them off to make a duplicate. The path is quoted at the user
// verbatim, so it has to be one that is really used.
const char *kRulePaths[] = {
	"/etc/udev/rules.d/g810-led.rules",
	"/usr/lib/udev/rules.d/g810-led.rules",
	"/lib/udev/rules.d/g810-led.rules"
};
const char *kRuleWritePath = kRulePaths[0];

// What the second button in the notice does, if there is one.
enum AltAction { altNone = 0, altCopyCommand = 1, altOpenAdvanced = 2 };

// How often the panel looks, while it has nothing to talk to, for the
// thing that would end that. Under the time it takes to plug a keyboard
// in and look back at the screen, and an idle HID enumeration either way.
const int kWatchMs = 1500;

// How often the panel checks that the keyboard it is talking to is still
// on the end of the wire. Unplugging one is silent — nothing tells a
// program its HID device has gone until the next write fails — so a
// window that never looked would go on naming a keyboard that is not
// there, with every control live, until the user pressed something and
// got an error instead of an answer. The check is one stat() of a device
// node that vanishes with the device, so this can be often enough to feel
// immediate and still cost nothing.
const int kAliveMs = 2000;

// What would end this state without the user pressing anything here: a
// keyboard appearing on the USB ports, or a device node that was shut to
// this user becoming ours to open. Everything else needs a person, and
// waiting for it would be the window pretending to work.
enum WatchFor { watchNothing = 0, watchAppear = 1, watchAccess = 2 };

// Everything the panel has to say about why there is no keyboard to talk
// to: the sentence, the command and the button are built together so they
// cannot end up describing different problems.
struct Trouble {
	std::string title;
	std::string body;
	std::string board;      // the short version, over the empty board
	// The strip above the tabs is the window's loudest line. It has room
	// for a few words, and they have to be the same few words this notice
	// is saying at length, or the two of them are arguing.
	std::string strip;
	// The line under it, in two versions. A notice brings the Device tab to
	// the front, so most of the time the reader is looking straight at the
	// long account and being told to go to the Device tab is being pointed
	// at where they already stand. stripHere is what the line says while
	// this tab is in front — the next step rather than the way to it — and
	// stripHint is for every other tab. Empty stripHere means the one
	// sentence serves both.
	std::string stripHint;
	std::string stripHere;
	std::string command;    // empty: no command block
	std::string altLabel;   // empty: no second button
	AltAction alt = altNone;
	WatchFor watch = watchNothing;
	// The keyboard this is about, where there is one: what altOpenAdvanced
	// fills the Advanced fields with, and whose device node watchAccess
	// waits on.
	uint16_t vendorID = 0;
	uint16_t productID = 0;
	std::string serial;
};

// The widgets this panel needs that MainWindow.h has no members for. They
// hang off the window object itself, so they are freed with it instead of
// living in a table this file would have to prune by hand.
struct DeviceUI {
	Gtk::Box *tabBox = nullptr;            // the Device tab's own column
	Gtk::Box *identity = nullptr;
	Gtk::Label *identityName = nullptr;
	// The whole row goes when there is only one keyboard to choose from.
	Gtk::Box *chooserRow = nullptr;
	Gtk::Label *chooserLabel = nullptr;
	// Stands where the name would be while there is no name, so the row of
	// buttons keeps to the right-hand end of the card in every state.
	Gtk::Box *filler = nullptr;
	Gtk::Box *notice = nullptr;
	Gtk::Label *noticeTitle = nullptr;
	Gtk::Label *noticeBody = nullptr;
	Gtk::Box *commandBox = nullptr;
	Gtk::Label *commandLabel = nullptr;
	Gtk::Button *altButton = nullptr;
	Gtk::Expander *manualExpander = nullptr;
	// The rest of the machine: filled after the window is up, and again
	// whenever Rescan is pressed.
	Gtk::Box *inventory = nullptr;
	Gtk::Expander *diagExpander = nullptr;
	Gtk::Label *diagReport = nullptr;
	std::string command;                   // what altCopyCommand copies
	AltAction alt = altNone;
	uint16_t altVendorID = 0;
	uint16_t altProductID = 0;
	// What the strip above the tabs should be saying while a notice is up,
	// and empty whenever it should be left alone. stripHint is whichever
	// of the two second lines suits the tab that is in front.
	std::string stripTitle;
	std::string stripHint;
	std::string hintHere;
	std::string hintAway;
	// The tab the reader was on when a notice pulled them over here, and
	// -1 when they were already here or have moved on since.
	int returnPage = -1;
	// The quiet look for the thing that would end this state on its own.
	sigc::connection watch;
	WatchFor watchFor = watchNothing;
	uint16_t watchVendorID = 0;
	uint16_t watchProductID = 0;
	std::string watchSerial;
	// The other direction: the quiet look, while a keyboard is open, for
	// it going away. Its own connection, because the two waits are for
	// opposite things and each has to be able to end without the other.
	sigc::connection alive;
	std::string aliveNode;
	// Everything that has to happen when there is nothing to talk to. It
	// touches the window's private state, so it is built once, in a
	// member, and kept here: MainWindow.h belongs to the window rather
	// than to this panel and has no member for a fourth way in.
	std::function<void(const Trouble &)> collapse;
	// The same, for starting the wait above once a keyboard is open.
	std::function<void()> watchDevice;
	// Nothing is left ticking away at a window that has gone: this runs
	// while the window is still whole, on its way down.
	~DeviceUI() { watch.disconnect(); alive.disconnect(); }
};

// Has the thing the panel is waiting for happened? Asked once when the
// wait starts, to be sure it is a wait for something, and then once every
// kWatchMs until it has.
bool watchSatisfied(LedKeyboard &kbd, const DeviceUI &ui);

// The notebook a widget is on a page of, and which page that is.
Gtk::Notebook *bookOf(Gtk::Widget *inside, int *page);

void releaseDeviceUI(gpointer data) { delete static_cast<DeviceUI*>(data); }

// The rest of the machine, listed under the keyboard.
//
// Everything in it is a read: the SPD the kernel already publishes, the
// USB descriptors, and whatever BlueZ has been told. Nothing here writes
// to a bus, which is why it can run without root and without the risk
// that lighting a DIMM would carry.
void fillInventory(DeviceUI &ui) {
	if (!ui.inventory)
		return;
	ui.inventory->foreach([&ui](Gtk::Widget &child) {
		ui.inventory->remove(child);
	});

	devicescan::Notes notes;
	const std::vector<devicescan::Found> found = devicescan::scan(notes);

	devicescan::Found::Kind group = devicescan::Found::Keyboard;
	bool started = false, anything = false;
	for (size_t i = 0; i < found.size(); ++i) {
		// The keyboards are the card above this one; saying them twice
		// would make the window look like it had found two.
		if (found[i].kind == devicescan::Found::Keyboard)
			continue;
		anything = true;
		if (!started || found[i].kind != group) {
			group = found[i].kind;
			started = true;
			Gtk::Label *heading = Gtk::manage(new Gtk::Label(
				devicescan::kindName(group)));
			heading->set_xalign(0);
			heading->get_style_context()->add_class("kb-title");
			heading->set_margin_top(i ? 10 : 0);
			ui.inventory->pack_start(*heading, false, false);
		}

		Gtk::Box *row = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 8));
		Gtk::Box *text = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_VERTICAL, 0));
		Gtk::Label *name = Gtk::manage(new Gtk::Label(found[i].name));
		name->set_xalign(0);
		name->set_ellipsize(Pango::ELLIPSIZE_END);
		text->pack_start(*name, false, false);
		if (!found[i].detail.empty()) {
			Gtk::Label *detail = Gtk::manage(new Gtk::Label(found[i].detail));
			detail->set_xalign(0);
			detail->set_line_wrap(true);
			detail->set_line_wrap_mode(Pango::WRAP_WORD_CHAR);
			detail->get_style_context()->add_class("kb-hint");
			text->pack_start(*detail, false, false);
		}
		text->set_hexpand(true);
		row->pack_start(*text, true, true);
		if (!found[i].where.empty()) {
			Gtk::Label *where = Gtk::manage(new Gtk::Label(found[i].where));
			where->set_xalign(1);
			where->set_valign(Gtk::ALIGN_START);
			where->get_style_context()->add_class("kb-hint");
			where->set_selectable(true);
			row->pack_end(*where, false, false);
		}
		ui.inventory->pack_start(*row, false, false);
	}

	// A machine with nothing else in it, or one where nothing could be
	// read, has to say which — an empty box says neither.
	std::string said;
	if (!anything)
		said = "Nothing else found.";
	if (!notes.memory.empty())
		said += (said.empty() ? "" : "\n") + notes.memory;
	if (!notes.bluetooth.empty())
		said += (said.empty() ? "" : "\n") + notes.bluetooth;
	if (!said.empty()) {
		Gtk::Label *note = Gtk::manage(new Gtk::Label(said));
		note->set_xalign(0);
		note->set_line_wrap(true);
		note->set_line_wrap_mode(Pango::WRAP_WORD_CHAR);
		note->get_style_context()->add_class("kb-hint");
		note->set_margin_top(6);
		ui.inventory->pack_start(*note, false, false);
	}
	ui.inventory->show_all();
}

DeviceUI &deviceUI(Gtk::Window *window) {
	static const Glib::Quark quark("g810-led-device-ui");
	DeviceUI *state = static_cast<DeviceUI*>(window->get_data(quark));
	if (!state) {
		state = new DeviceUI();
		window->set_data(quark, state, &releaseDeviceUI);
	}
	return *state;
}

// Set G810_NO_DEVICE to look at the empty-handed states on a machine that
// does have a keyboard plugged in — they are the first thing a new user
// sees and there is otherwise no way to review them. Values:
//   none       the scan finds nothing at all
//   unknown    the scan finds nothing, and reports the keyboard it can
//              see as one this build does not support
//   denied     the keyboard is found, the open is refused, and its device
//              node reports that this user may not touch it
//   busy       the keyboard is found and the open is refused with the
//              node readable, which means something else holds it
// Unset, or any other value, means "behave normally". Clearing it in a
// running process is how the panel's wait for a keyboard to appear, or
// for its device node to open, can be watched happening.
enum class Pretend { off, nothing, unknown, denied, busy };

Pretend pretend() {
	const char *value = std::getenv("G810_NO_DEVICE");
	if (!value || !*value) return Pretend::off;
	if (std::strcmp(value, "unknown") == 0) return Pretend::unknown;
	if (std::strcmp(value, "denied") == 0) return Pretend::denied;
	if (std::strcmp(value, "busy") == 0) return Pretend::busy;
	return Pretend::nothing;
}

const char *modelDisplayName(LedKeyboard::KeyboardModel model) {
	switch (model) {
		case LedKeyboard::KeyboardModel::g213: return "G213";
		case LedKeyboard::KeyboardModel::g410: return "G410";
		case LedKeyboard::KeyboardModel::g413: return "G413";
		case LedKeyboard::KeyboardModel::g512: return "G512";
		case LedKeyboard::KeyboardModel::g513: return "G513";
		case LedKeyboard::KeyboardModel::g610: return "G610";
		case LedKeyboard::KeyboardModel::g810: return "G810";
		case LedKeyboard::KeyboardModel::g815: return "G815";
		case LedKeyboard::KeyboardModel::g910: return "G910";
		case LedKeyboard::KeyboardModel::gpro: return "G\u00a0Pro";
		default: return "";
	}
}

// "G213, G410, … and G Pro", read off the table the scan matches against
// so the sentence cannot promise a model this build does not know.
std::string supportedModels(const std::vector<std::vector<uint16_t>> &table) {
	std::vector<std::string> names;
	for (size_t i = 0; i < table.size(); ++i) {
		if (table[i].size() < 3) continue;
		std::string name =
			modelDisplayName((LedKeyboard::KeyboardModel)table[i][2]);
		if (name.empty()) continue;
		bool seen = false;
		for (size_t j = 0; j < names.size() && !seen; ++j)
			seen = names[j] == name;
		if (!seen) names.push_back(name);
	}
	std::string list;
	for (size_t i = 0; i < names.size(); ++i) {
		if (i) list += (i + 1 == names.size() ? " and " : ", ");
		list += names[i];
	}
	return list;
}

// What this keyboard is called by the person it belongs to. The string it
// reports for itself runs to "G512 RGB MECHANICAL GAMING KEYBOARD", which
// in a paragraph is two lines of shouting where "your G512" would have
// done. The short name is cut out of that string and not read off the
// supported-models table: several ids are driven as a different model
// from the one printed on the case, and prose has to use the name on the
// case. The table is the last resort, for a keyboard that reports no name
// at all.
std::string familiarName(const std::vector<std::vector<uint16_t>> &table,
                         const LedKeyboard::DeviceInfo &device) {
	std::string token;
	for (size_t i = 0; i <= device.product.size(); ++i) {
		const char c = i < device.product.size() ? device.product[i] : ' ';
		if (c != ' ' && c != '\t') {
			token += c;
			continue;
		}
		// A model number and nothing else: a G and three or four digits.
		bool model = token.size() >= 3 && token.size() <= 5 &&
			(token[0] == 'G' || token[0] == 'g');
		for (size_t j = 1; j < token.size() && model; ++j)
			model = std::isdigit((unsigned char)token[j]) != 0;
		if (model) return "Your " + token;
		token.clear();
	}
	if (!device.product.empty()) return "“" + device.product + "”";
	for (size_t i = 0; i < table.size(); ++i) {
		if (table[i].size() < 3) continue;
		if (table[i][0] != device.vendorID || table[i][1] != device.productID)
			continue;
		const std::string named =
			modelDisplayName((LedKeyboard::KeyboardModel)table[i][2]);
		if (!named.empty()) return "Your " + named;
	}
	return "The keyboard";
}

bool isSupported(const std::vector<std::vector<uint16_t>> &table,
                 uint16_t vendorID, uint16_t productID) {
	for (size_t i = 0; i < table.size(); ++i)
		if (table[i].size() >= 2 && table[i][0] == vendorID &&
		    table[i][1] == productID)
			return true;
	return false;
}

// A keyboard that is plugged in but is not one we can drive. Knowing
// whether one exists is what separates "nothing is connected" from "that
// keyboard is not one I support", and those have different fixes.
struct Foreign {
	bool probed = false;    // false: this build cannot tell, so do not claim
	bool found = false;
	int rank = -1;          // how recognisable this one is, see probeForeign
	std::string product;
	std::string manufacturer;
	uint16_t vendorID = 0;
	uint16_t productID = 0;
};

#if defined(hidapi)
std::string fromWide(const wchar_t *wide) {
	if (!wide) return std::string();
	char buffer[256];
	size_t converted = wcstombs(buffer, wide, sizeof(buffer) - 1);
	if (converted == (size_t)-1) return std::string();
	buffer[converted] = '\0';
	return std::string(buffer);
}
#endif

// ignoreTable is the G810_NO_DEVICE=unknown path: report the keyboard that
// is really there as if this build did not know it.
Foreign probeForeign(const std::vector<std::vector<uint16_t>> &table,
                     bool ignoreTable) {
	Foreign result;
#if defined(hidapi)
	result.probed = true;
	if (hid_init() < 0) return result;
	struct hid_device_info *devices = hid_enumerate(0x0, 0x0);
	for (struct hid_device_info *device = devices; device;
	     device = device->next) {
		// Generic Desktop / Keyboard. Every USB keyboard publishes this,
		// including the receivers wireless ones come with.
		if (device->usage_page != 0x01 || device->usage != 0x06) continue;
		if (!ignoreTable &&
		    isSupported(table, device->vendor_id, device->product_id))
			continue;
		// Several devices can claim the keyboard usage — a wireless
		// receiver does, and so does the keyboard behind it. Name the one
		// the user would recognise: something that calls itself a
		// keyboard first, then anything from the vendor we drive.
		const std::string product = fromWide(device->product_string);
		std::string lower = product;
		for (size_t i = 0; i < lower.size(); ++i)
			lower[i] = (char)std::tolower((unsigned char)lower[i]);
		int rank = (lower.find("keyboard") != std::string::npos ? 2 : 0) +
			(device->vendor_id == 0x046d ? 1 : 0);
		if (result.found && rank <= result.rank) continue;
		result.found = true;
		result.rank = rank;
		result.vendorID = device->vendor_id;
		result.productID = device->product_id;
		result.product = product;
		result.manufacturer = fromWide(device->manufacturer_string);
	}
	hid_free_enumeration(devices);
	hid_exit();
#else
	(void)table;
	(void)ignoreTable;
#endif
	return result;
}

// The device node an open would have used, and whether this user may use
// it. A node that exists and is not readable is a permission problem and
// nothing else, which is worth being certain about before saying so.
struct Node {
	// False when this build cannot look at all. "I found no node" and "I
	// cannot see nodes" lead to opposite conclusions — the first means the
	// device has gone, the second means nothing — so they must not share
	// an answer.
	bool probed = false;
	bool known = false;
	bool usable = false;
	std::string path;
};

Node findNode(uint16_t vendorID, uint16_t productID,
              const std::string &serial) {
	Node node;
#if defined(hidapi)
	if (hid_init() < 0) return node;
	node.probed = true;
	struct hid_device_info *devices = hid_enumerate(vendorID, productID);
	for (struct hid_device_info *device = devices; device;
	     device = device->next) {
		// The first match wins, exactly as it does in LedKeyboard::open().
		if (!serial.empty() && fromWide(device->serial_number) != serial)
			continue;
		if (!device->path) continue;
		node.known = true;
		node.path = device->path;
		node.usable = access(node.path.c_str(), R_OK | W_OK) == 0 &&
			pretend() != Pretend::denied;
		break;
	}
	hid_free_enumeration(devices);
	hid_exit();
#else
	(void)vendorID;
	(void)productID;
	(void)serial;
#endif
	return node;
}

bool watchSatisfied(LedKeyboard &kbd, const DeviceUI &ui) {
	// A door that was shut to this user standing open now, or — for the
	// states that have no keyboard at all — anything at all arriving.
	if (ui.watchFor == watchAccess)
		return findNode(ui.watchVendorID, ui.watchProductID,
			ui.watchSerial).usable;
	// G810_NO_DEVICE holds one of these states open to be looked at, so
	// the wait has to be blind in the same way the scan is; otherwise it
	// would end the state the override exists to hold. Every "is it over
	// yet?" goes through here, which is what keeps the two agreeing.
	const Pretend hiding = pretend();
	if (hiding == Pretend::nothing || hiding == Pretend::unknown) return false;
	return !kbd.listKeyboards().empty();
}

// Where the rule is, if it is anywhere. Empty when there is none.
std::string ruleFile() {
	for (size_t i = 0; i < sizeof(kRulePaths) / sizeof(kRulePaths[0]); ++i)
		if (access(kRulePaths[i], F_OK) == 0)
			return kRulePaths[i];
	return std::string();
}

// Does an installed rule already name this product id? "The rule is
// there" and "the rule covers this keyboard" are different answers with
// different fixes, and guessing between them is how a user ends up
// pasting a command that does nothing.
bool ruleCovers(uint16_t productID) {
	const std::string needle = "\"" + hexId(productID) + "\"";
	for (size_t i = 0; i < sizeof(kRulePaths) / sizeof(kRulePaths[0]); ++i) {
		FILE *file = std::fopen(kRulePaths[i], "r");
		if (!file) continue;
		std::string text;
		char buffer[4096];
		size_t got;
		// A rules file is a few hundred bytes; anything past a page or
		// two of them is not one, and is not worth reading to search.
		while (text.size() < 64 * 1024 &&
		       (got = std::fread(buffer, 1, sizeof(buffer), file)) > 0)
			text.append(buffer, got);
		std::fclose(file);
		for (size_t at = 0; at < text.size(); ++at)
			text[at] = (char)std::tolower((unsigned char)text[at]);
		if (text.find(needle) != std::string::npos)
			return true;
	}
	return false;
}

// A path in the middle of a paragraph, in one piece. Pango is entitled to
// break a line at the hyphen in "g810-led" and after each slash, and both
// leave a fragment that reads like a different file. Only for prose: what
// the command block offers to copy is the real thing, ordinary hyphens
// and all.
std::string unbroken(const std::string &path) {
	std::string out;
	for (size_t i = 0; i < path.size(); ++i) {
		// The non-breaking hyphen draws as a hyphen; the word joiner after
		// a slash draws as nothing. Both deny Pango the break.
		if (path[i] == '-') out += "‑";
		else if (path[i] == '/') out += "/⁠";
		else out += path[i];
	}
	return out;
}

// The other way: what unbroken() made, with the marks taken back out.
// They are there for Pango, and a speech engine reading the sentence
// aloud should be given the path a person would type.
std::string plain(const std::string &text) {
	std::string out = text;
	for (size_t at = out.find("‑"); at != std::string::npos;
	     at = out.find("‑", at))
		out.replace(at, 3, "-");
	for (size_t at = out.find("⁠"); at != std::string::npos;
	     at = out.find("⁠", at))
		out.erase(at, 3);
	return out;
}

// Reloading the rules and applying them to the keyboard that is already
// plugged in. The trigger asks for "add" because that is the action the
// rule matches: a plain `udevadm trigger` replays it as "change", the
// rule does not match, and nothing about the permissions changes.
//
// The line breaks are deliberate and go to the clipboard as well. Pango
// is entitled to break a long command wherever it likes — "trigger --"
// and "action=add" on separate lines is what it chose — and a command
// nobody can read is not a fix anyone will run. Breaking it at the "&&"
// puts each step on its own line, which is how a person would have typed
// it, and a shell pastes it back exactly the same.
const char *kReloadRules =
	"sudo udevadm control --reload-rules &&\n"
	"sudo udevadm trigger --action=add";

// The rule this keyboard needs, in the form udev/g810-led.rules uses.
// Appended rather than written over: the file may already hold rules for
// other models, and a fix that throws those away is not a fix.
std::string installRuleCommand(uint16_t vendorID, uint16_t productID) {
	return "echo 'ACTION==\"add\", SUBSYSTEMS==\"usb\", ATTRS{idVendor}==\"" +
		hexId(vendorID) + "\", ATTRS{idProduct}==\"" + hexId(productID) +
		"\", MODE=\"660\", TAG+=\"uaccess\"' |\n" +
		// Its own line, so the path is never the thing that gets broken
		// in half.
		"  sudo tee -a " + kRuleWritePath + " &&\n" + kReloadRules;
}

// --- The notice --------------------------------------------------------

void showConnected(DeviceUI &ui, Gtk::Button &rescan) {
	if (ui.identity) ui.identity->show();
	if (ui.filler) ui.filler->hide();
	if (ui.notice) ui.notice->hide();
	if (ui.altButton) {
		ui.altButton->hide();
		ui.altButton->get_style_context()->remove_class("kb-primary");
	}
	ui.alt = altNone;
	ui.command.clear();
	// The strip above the tabs has a keyboard to name again, so this panel
	// stops speaking for it, and there is nothing left to wait for.
	ui.stripTitle.clear();
	ui.stripHint.clear();
	ui.watch.disconnect();
	ui.watchFor = watchNothing;
	// Nothing on this screen needs the eye any more, so the accent comes
	// off: looking again is an ordinary thing to want, not the way out.
	rescan.get_style_context()->remove_class("kb-primary");
	// A notice brought the reader here from wherever they were working;
	// with the keyboard back there is no reason to keep them, so they go
	// back to it. Only from this tab: a reader who has moved on since has
	// chosen where to be, and moving them again would be the window taking
	// the wheel.
	if (ui.returnPage >= 0 && ui.tabBox) {
		int page = -1;
		Gtk::Notebook *book = bookOf(ui.tabBox, &page);
		if (book && page >= 0 && book->get_current_page() == page &&
		    ui.returnPage < book->get_n_pages())
			book->set_current_page(ui.returnPage);
	}
	ui.returnPage = -1;
}

void showTrouble(DeviceUI &ui, Gtk::Button &rescan, const Trouble &trouble,
                 bool waiting) {
	if (ui.identity) ui.identity->hide();
	if (ui.filler) ui.filler->show();
	if (!ui.notice) return;
	if (ui.noticeTitle)
		ui.noticeTitle->set_markup("<b>" +
			Glib::Markup::escape_text(trouble.title) + "</b>");
	if (ui.noticeBody) ui.noticeBody->set_text(trouble.body);
	ui.command = trouble.command;
	if (ui.commandBox) {
		if (trouble.command.empty()) {
			ui.commandBox->hide();
		} else {
			if (ui.commandLabel) ui.commandLabel->set_text(trouble.command);
			ui.commandBox->show();
		}
	}
	ui.alt = trouble.alt;
	ui.altVendorID = trouble.vendorID;
	ui.altProductID = trouble.productID;
	if (ui.altButton) {
		if (trouble.alt == altNone || trouble.altLabel.empty()) {
			ui.altButton->hide();
		} else {
			ui.altButton->set_label(trouble.altLabel);
			// Beside Rescan, "Copy the command" is 186 unbreakable pixels
			// out of the 302 this tab gets at a 980-wide window, which put
			// both buttons behind a horizontal scrollbar. A label that may
			// break takes two lines there and one line everywhere else. The
			// child only exists once there is a label to hold.
			if (Gtk::Label *words =
			    dynamic_cast<Gtk::Label*>(ui.altButton->get_child())) {
				words->set_line_wrap(true);
				words->set_justify(Gtk::JUSTIFY_CENTER);
			}
			// "Copy the command" says nothing about which command to
			// anyone who cannot see the block above it. GTK hands a
			// tooltip to the screen reader as the control's description,
			// so this is the same sentence for both.
			ui.altButton->set_tooltip_text(trouble.alt == altCopyCommand ?
				"Copy the command shown above, to paste into a terminal" :
				"Fill in the Advanced fields with this keyboard's ids");
			ui.altButton->show();
		}
	}
	// A notice that appears inside a panel announces itself to nobody: its
	// labels are read only by a reader who happens to walk over them. The
	// box carries the whole account as its own name and description, and
	// says it is an alert, so the assistive layer has something to offer
	// and something to say about it.
	if (Glib::RefPtr<Atk::Object> access = ui.notice->get_accessible()) {
		atk_object_set_role(access->gobj(), ATK_ROLE_ALERT);
		access->set_name(trouble.title);
		std::string spoken = plain(trouble.body);
		// The command is read out as part of the account rather than left
		// to be found: it is the fix, and a reader who cannot see the block
		// would otherwise be told to run something nobody named.
		if (!trouble.command.empty())
			spoken += "  The command is: " + trouble.command;
		access->set_description(spoken);
	}
	ui.notice->show();
	// The accent goes on the step the notice has just asked for, and on
	// nothing else. Where there is a command, that is copying it: pressing
	// Rescan before the fix has been applied can only produce this same
	// notice again, and a button that glows must not be a way back to
	// where you started. Where the panel is waiting for the world to
	// change — a keyboard to be plugged in, a rule to take effect — the
	// notice has just said so, and lighting up a button that would only
	// repeat what the panel is already doing would take that back. What is
	// left is the states a person has to end: those get Rescan.
	const bool copyFirst = trouble.alt == altCopyCommand && ui.altButton;
	if (ui.altButton) {
		if (copyFirst) ui.altButton->get_style_context()->add_class("kb-primary");
		else ui.altButton->get_style_context()->remove_class("kb-primary");
	}
	if (copyFirst || waiting)
		rescan.get_style_context()->remove_class("kb-primary");
	else rescan.get_style_context()->add_class("kb-primary");
}

Trouble nothingConnected(const std::vector<std::vector<uint16_t>> &table,
                         const Foreign &foreign) {
	Trouble trouble;
	const std::string models = supportedModels(table);
	// Either way out of these two is a keyboard arriving on a USB port,
	// which this panel can see for itself.
	trouble.watch = watchAppear;
	if (foreign.found) {
		std::string name = foreign.product.empty() ?
			std::string("The keyboard on this computer") :
			"“" + foreign.product + "”";
		std::string maker = foreign.manufacturer.empty() ?
			std::string() : foreign.manufacturer + " ";
		trouble.title = "This keyboard is not one g810-led can light";
		trouble.body = name + " (" + maker + hexId(foreign.vendorID) + ":" +
			hexId(foreign.productID) + ") is plugged in, but its lighting is "
			"not something this program knows how to drive.\n\n"
			"It lights the " + models + ". Plug one of those in and this "
			"window will pick it up.";
		trouble.board = "The keyboard connected here is not one this program\n"
			"can light. The Device tab says which ones it can.";
		trouble.strip = "Keyboard not supported";
		trouble.stripHint = "The Device tab lists the ones this program lights";
		trouble.stripHere = "Plug in one of the models below, or set this one "
			"up by hand";
		// The manual path is the honest second offer: it is what the CLI's
		// -tuk is for, and the only thing that can make an unknown board
		// light up.
		trouble.altLabel = "Set it up by hand…";
		trouble.alt = altOpenAdvanced;
		trouble.vendorID = foreign.vendorID;
		trouble.productID = foreign.productID;
		return trouble;
	}
	trouble.title = foreign.probed ?
		"No keyboard connected" : "No supported keyboard found";
	trouble.body = std::string(foreign.probed ?
		"There is no keyboard on this computer's USB ports." :
		"Nothing this program can light is connected.") +
		"\n\ng810-led lights the " + models + ". Plug one in and this window "
		"will pick it up.";
	trouble.board = "Plug in a keyboard this program can light.\n"
		"This window will pick it up.";
	trouble.strip = trouble.title;
	trouble.stripHint = "Connect a supported keyboard — this window "
		"will pick it up";
	return trouble;
}

// A keyboard that was there and is not now: unplugged while the window
// was using it, or gone between the scan that listed it and the open that
// followed. The same three sentences serve both, because to the person at
// the desk they are the same event.
Trouble vanished(const std::vector<std::vector<uint16_t>> &table,
                 const LedKeyboard::DeviceInfo &device) {
	Trouble trouble;
	trouble.title = "That keyboard is no longer there";
	trouble.body = familiarName(table, device) + " was there a moment ago and "
		"is not now. Plug it back in and this window will pick it up.";
	trouble.board = "It was there a moment ago and is not now.\n"
		"Plug it back in and this window will pick it up.";
	trouble.strip = "Keyboard disconnected";
	trouble.stripHint = "Plug it back in — this window will pick it up";
	trouble.watch = watchAppear;
	return trouble;
}

// Connected, recognised, and still not lightable. The G915 and its kin
// answer over Bluetooth and accept every lighting report sent to them —
// the transport takes it and the firmware drops it, so there is no error
// to notice and nothing happens. Logitech's own software will not light
// them this way either. Saying so is the only honest option: the
// alternative is a target in the chooser that looks like it works.
Trouble overBluetooth(const std::vector<std::vector<uint16_t>> &table,
                      const LedKeyboard::DeviceInfo &device) {
	Trouble trouble;
	const std::string name = familiarName(table, device);
	trouble.title = name + " is connected over Bluetooth";
	trouble.body = "Bluetooth carries the typing but not the lighting. This "
		"keyboard accepts colour commands over it and quietly ignores them, "
		"so nothing this window sends would reach the keys.\n\n"
		"Connect it with its USB cable, or with the Lightspeed receiver it "
		"came with, and it will appear here as something that can be lit.";
	trouble.board = "Bluetooth carries typing, not lighting.\n"
		"Use the USB cable or the Lightspeed receiver.";
	trouble.strip = "Bluetooth cannot carry lighting";
	trouble.stripHint = "Use the USB cable or the Lightspeed receiver";
	trouble.watch = watchAppear;
	return trouble;
}

Trouble cannotOpen(const std::vector<std::vector<uint16_t>> &table,
                   const LedKeyboard::DeviceInfo &device) {
	Trouble trouble;
	const std::string name = familiarName(table, device);
	const Node node = findNode(device.vendorID, device.productID,
		device.serialNumber);
	// Nothing to open. A keyboard can be listed by a scan and gone by the
	// time the open reaches it, and the refusal that comes back is the
	// same one a shut door gives — so the node is what tells them apart,
	// and blaming permissions for an empty USB port would send the user
	// off to fix something that is not broken.
	if (node.probed && !node.known) return vanished(table, device);
	const std::string where = node.known ? unbroken(node.path) :
		std::string("its device file");
	if (node.known && node.usable) {
		// The node is ours to read and write, so permission is not the
		// story. What is left is a guess — hidraw lets several programs
		// hold the same keyboard, so this is rare — and it is said as a
		// guess: what happened, what usually causes it, and the two things
		// worth trying. Nothing this panel can watch for says when the
		// other program has let go, so this one waits for the user.
		trouble.title = "This keyboard would not open";
		trouble.body = name + " is connected and " + where + " is open to "
			"you, so this is not about permissions. Another program is "
			"probably holding the keyboard — other lighting software, or a "
			"second copy of this window. Close it, or unplug the keyboard and "
			"plug it back in, then press Rescan.";
		trouble.board = "The keyboard is there, but it would not open.\n"
			"The Device tab says what usually causes that.";
		trouble.strip = "Keyboard would not open";
		trouble.stripHint = "It is not a permission problem — the Device tab "
			"has the details";
		trouble.stripHere = "Close anything else that lights keyboards, then "
			"press Rescan";
		return trouble;
	}
	// Short enough to stay on one line in a narrow panel, where the lines
	// it saves are the ones that keep the command on screen. It also says
	// what kind of problem this is before the eye reaches the paragraph:
	// nothing here is broken and nothing has to be understood, something
	// has to be granted.
	trouble.title = "This keyboard needs permission";
	trouble.board = "The Device tab says why, and has the one line\n"
		"that grants it.";
	trouble.strip = "Keyboard needs permission";
	trouble.stripHint = "The Device tab has the reason and the fix";
	// No "then press Rescan": the wait armed below sees the node open and
	// picks the keyboard up on its own, and asking for a button press the
	// window does not need is how a fix comes to look like it failed.
	trouble.stripHere = "Run the command below to grant it";
	// The way out is the device node becoming this user's to open, which
	// is a thing this panel can see for itself the moment it happens.
	trouble.watch = watchAccess;
	trouble.vendorID = device.vendorID;
	trouble.productID = device.productID;
	trouble.serial = device.serialNumber;
	// "is connected" rather than "answered the scan": this same sentence is
	// also what the hand-entered ids get, and there is no scan on that path.
	// Kept short on purpose — at the narrowest the window goes, every
	// sentence spent here is a line between the reader and the command.
	trouble.body = name + " is connected, but " + where + " is closed to you. ";
	const std::string rules = ruleFile();
	if (!rules.empty() && ruleCovers(device.productID)) {
		trouble.body += "The udev rule that grants it is installed but has "
			"not taken effect. Unplug it and plug it back in, or run this:";
		trouble.command = kReloadRules;
	} else {
		// The command block below has the real path in it, so the sentence
		// does not need to carry it as well.
		trouble.body += rules.empty() ?
			"Access is granted by a udev rule, and this computer has none. "
				"Run this in a terminal:" :
			"Access is granted by a udev rule, and none of this computer's "
				"rules mention this keyboard. Run this to add one:";
		trouble.command = installRuleCommand(device.vendorID, device.productID);
	}
	trouble.altLabel = "Copy the command";
	trouble.alt = altCopyCommand;
	return trouble;
}

// The notebook this widget is on a page of, and which page that is.
Gtk::Notebook *bookOf(Gtk::Widget *inside, int *page) {
	for (Gtk::Widget *child = inside; child; child = child->get_parent()) {
		Gtk::Notebook *book = dynamic_cast<Gtk::Notebook*>(child->get_parent());
		if (!book) continue;
		if (page) *page = book->page_num(*child);
		return book;
	}
	return nullptr;
}

// The tab this widget is on, brought to the front. A window that opens
// with nothing to light up should be showing the reason, not a colour
// picker with every control dead.
void raiseTab(Gtk::Widget *inside) {
	int page = -1;
	Gtk::Notebook *book = bookOf(inside, &page);
	if (book && page >= 0) book->set_current_page(page);
}

// The report the Diagnostics fold shows and "Copy this report" puts on
// the clipboard: one text, so a bug report can be pasted whole.
std::string deviceReport(LedKeyboard &kbd,
                         const std::vector<LedKeyboard::DeviceInfo> &devices) {
	std::string report;
#ifdef VERSION
	report += std::string("g810-led ") + VERSION + "\n";
#endif
	if (kbd.isOpen()) {
		LedKeyboard::DeviceInfo device = kbd.getCurrentDevice();
		report += "Open: " + device.product + " (" + device.manufacturer + " " +
			hexId(device.vendorID) + ":" + hexId(device.productID) + ")";
		if (!device.serialNumber.empty())
			report += ", serial " + device.serialNumber;
		report += std::string(", driven as a ") +
			modelDisplayName(kbd.getKeyboardModel());
		Node node = findNode(device.vendorID, device.productID,
			device.serialNumber);
		// Its own line: the path is the fact most often quoted back in a
		// bug report, and at the end of a long one it is also the thing
		// the fold wraps in half.
		if (node.known)
			report += "\n  node " + node.path +
				(node.usable ? "" : " (no access)");
		report += "\n";
	} else {
		report += "Open: nothing\n";
	}
	report += "Scan found " + std::to_string(devices.size()) +
		(devices.size() == 1 ? " supported keyboard" : " supported keyboards");
	for (size_t i = 0; i < devices.size(); ++i) {
		report += "\n  " + hexId(devices[i].vendorID) + ":" +
			hexId(devices[i].productID) + "  " + devices[i].product;
		if (!devices[i].serialNumber.empty())
			report += "  serial " + devices[i].serialNumber;
	}
	report += "\n";
	Foreign foreign = probeForeign(kbd.SupportedKeyboards, false);
	if (foreign.found)
		report += "Also seen: " + foreign.product + " (" + foreign.manufacturer +
			" " + hexId(foreign.vendorID) + ":" + hexId(foreign.productID) +
			"), not a model this build knows\n";
	const std::string rules = ruleFile();
	report += "Access rule: " + (rules.empty() ? std::string("not installed") :
		rules);
	// Whether it names the keyboard is only an answer while there is one
	// to name; with nothing found there is nothing to say about it.
	if (!rules.empty() && !devices.empty())
		report += ruleCovers(devices[0].productID) ?
			", covers " + hexId(devices[0].productID) :
			", says nothing about " + hexId(devices[0].productID);
	return report;
}

// Only while the fold is open: it walks every HID device on the machine,
// which is not something a rescan should pay for to fill a hidden label.
void refreshReport(DeviceUI &ui, LedKeyboard &kbd,
                   const std::vector<LedKeyboard::DeviceInfo> &devices) {
	if (!ui.diagReport || !ui.diagExpander || !ui.diagExpander->get_expanded())
		return;
	ui.diagReport->set_text(deviceReport(kbd, devices));
}

// The line under the name: everything else that identifies this keyboard,
// including which supported model it is being driven as — which is the
// answer to "why does the picture not look like my keyboard".
std::string deviceDetail(LedKeyboard &kbd,
                         const LedKeyboard::DeviceInfo &device) {
	std::string detail = device.manufacturer.empty() ?
		std::string("USB") : device.manufacturer;
	detail += " · " + hexId(device.vendorID) + ":" + hexId(device.productID);
	if (!device.serialNumber.empty())
		detail += " · serial " + device.serialNumber;
	const std::string model = modelDisplayName(kbd.getKeyboardModel());
	if (!model.empty()) {
		std::string product = device.product;
		for (size_t i = 0; i < product.size(); ++i)
			product[i] = (char)std::tolower((unsigned char)product[i]);
		std::string needle = model;
		for (size_t i = 0; i < needle.size(); ++i)
			needle[i] = (char)std::tolower((unsigned char)needle[i]);
		// "G Pro" is spelled with a hard space so a line break cannot come
		// between the two halves; the keyboard's own string has a plain one.
		for (size_t at = needle.find("\u00a0"); at != std::string::npos;
		     at = needle.find("\u00a0"))
			needle.replace(at, 2, " ");
		// Only worth saying when it is not already in the name: several of
		// the supported ids report a different model in their own string.
		if (product.find(needle) == std::string::npos)
			detail += " · driven as a " + model;
	}
	return detail;
}

// The diagnostics sit at the foot of the tab, which is what makes the
// space below the cards read as a margin rather than as something
// missing — but it also means opening them grows the page downwards, out
// of sight. Take the reader there. The scroll happens on the next turn of
// the loop, once the new height is known, and holds the adjustment rather
// than the window so a window that closes first is not followed.
void revealFoot(Gtk::Widget *widget) {
	for (Gtk::Widget *w = widget; w; w = w->get_parent()) {
		Gtk::ScrolledWindow *scroll = dynamic_cast<Gtk::ScrolledWindow*>(w);
		if (!scroll) continue;
		Glib::RefPtr<Gtk::Adjustment> down = scroll->get_vadjustment();
		if (down)
			Glib::signal_idle().connect_once(
				[down]() { down->set_value(down->get_upper()); });
		return;
	}
}

Gtk::Label *hintLabel(const std::string &text) {
	Gtk::Label *label = Gtk::manage(new Gtk::Label(text));
	label->set_xalign(0);
	label->set_line_wrap(true);
	label->get_style_context()->add_class("kb-hint");
	return label;
}

// A closed combo asks to be as wide as its widest row, and the rows on
// this tab are sentences: "The keyboard controls its own lighting" alone
// wanted 331px of a column that gets 343 at a 980-wide window, which is
// how this tab came to be read through a horizontal scrollbar. Ellipsizing
// the closed control gives it a floor of a few characters instead. The
// popup keeps its natural width, so the whole of every choice is readable
// in the one place it has to be: the list you pick from.
void fitToColumn(Gtk::ComboBoxText &combo) {
	combo.set_popup_fixed_width(false);
	const std::vector<Gtk::CellRenderer*> cells = combo.get_cells();
	for (size_t i = 0; i < cells.size(); ++i)
		if (Gtk::CellRendererText *text =
		    dynamic_cast<Gtk::CellRendererText*>(cells[i])) {
			text->property_ellipsize() = Pango::ELLIPSIZE_END;
			text->property_width_chars() = 10;
		}
}

// A fold whose title can wrap. GtkExpander takes a plain string as one
// unbreakable line, so "Advanced: unsupported or test keyboard" set a
// 330px floor under the whole tab — a title for a drawer nobody has opened
// deciding how narrow the window may be.
Gtk::Expander *foldNamed(const char *title) {
	Gtk::Expander *fold = Gtk::manage(new Gtk::Expander());
	Gtk::Label *label = Gtk::manage(new Gtk::Label(title));
	label->set_xalign(0);
	label->set_line_wrap(true);
	label->set_line_wrap_mode(Pango::WRAP_WORD_CHAR);
	fold->set_label_widget(*label);
	label->show();
	return fold;
}

// A label and its control on one row, so no combo is left for the user to
// identify by the value that happens to be showing in it. The label is
// also the control's accessible name — set_mnemonic_widget() makes that
// relation whether or not the text carries an underlined letter, and this
// tab is too crowded to hand out a dozen more Alt keys.
//
// The relation was not enough on its own. GTK gives a combo box its
// current value as its accessible *name*, and a name that is set wins
// over a label relation, so "At power-on" announced itself as "Wave
// animation" — the value, twice, and never what it was for. Naming it
// here is the same thing PanelEffects does for the same widget; the two
// files used to reason their way to opposite answers about it.
void addFieldRow(Gtk::Grid *grid, int row, const char *text, Gtk::Widget &field) {
	Gtk::Label *label = Gtk::manage(new Gtk::Label(text));
	label->set_xalign(0);
	label->set_mnemonic_widget(field);
	field.set_hexpand(true);
	if (Glib::RefPtr<Atk::Object> spokenFor = field.get_accessible()) {
		// The colon is punctuation for the eye, not part of the name.
		std::string spoken(text);
		while (!spoken.empty() &&
		       (spoken[spoken.size() - 1] == ':' || spoken[spoken.size() - 1] == ' '))
			spoken.erase(spoken.size() - 1);
		spokenFor->set_name(spoken);
	}
	grid->attach(*label, 0, row, 1, 1);
	grid->attach(field, 1, row, 1, 1);
}

}  // namespace

void MainWindow::buildDeviceSection(Gtk::Box* deviceBox) {
	DeviceUI &ui = deviceUI(this);
	// The card's frame hangs in the Device tab's column: the diagnostics
	// go at the foot of it, and a scan that comes back empty raises it.
	if (Gtk::Widget *frame = deviceBox->get_parent())
		ui.tabBox = dynamic_cast<Gtk::Box*>(frame->get_parent());

	// --- What is connected. The strip at the top of the window names the
	// keyboard; this is the rest of its identity, which is what a bug
	// report needs and what the old "Print info" button printed into a
	// status bar that the next message wiped.
	ui.identity = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_VERTICAL, 2));
	// Bold body, not .kb-heading: the strip above the tabs is the window's
	// one heading, and a second one shouting the same name would be a
	// competition rather than a hierarchy.
	ui.identityName = Gtk::manage(new Gtk::Label());
	ui.identityName->set_xalign(0);
	ui.identityName->set_line_wrap(true);
	ui.identity->pack_start(*ui.identityName, false, false);
	m_deviceLabel.set_xalign(0);
	m_deviceLabel.set_line_wrap(true);
	m_deviceLabel.get_style_context()->add_class("kb-hint");
	ui.identity->pack_start(m_deviceLabel, false, false);
	// It goes in the row of actions below rather than above it: Rescan is
	// what this line is about, and side by side they are one thing — a
	// keyboard and the way to look for it again — instead of two rows with
	// a button stranded at the end of the second.

	// --- Why there is nothing to light up, and what to do about it.
	ui.notice = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_VERTICAL, 6));
	Gtk::Box *titleRow = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 6));
	// Colour is not allowed to be the only thing that marks this as a
	// problem, and an icon that is missing from the theme would draw as a
	// broken image, so it is only added when the theme really has it.
	if (Gtk::IconTheme::get_default()->has_icon("dialog-warning-symbolic")) {
		Gtk::Image *icon = Gtk::manage(new Gtk::Image());
		icon->set_from_icon_name("dialog-warning-symbolic", Gtk::ICON_SIZE_MENU);
		icon->set_valign(Gtk::ALIGN_START);
		icon->set_margin_top(2);
		titleRow->pack_start(*icon, false, false);
	}
	ui.noticeTitle = Gtk::manage(new Gtk::Label());
	ui.noticeTitle->set_xalign(0);
	ui.noticeTitle->set_line_wrap(true);
	titleRow->pack_start(*ui.noticeTitle, true, true);
	ui.notice->pack_start(*titleRow, false, false);
	ui.noticeBody = Gtk::manage(new Gtk::Label());
	ui.noticeBody->set_xalign(0);
	ui.noticeBody->set_line_wrap(true);
	// Prose wraps on a readable measure rather than on whatever width the
	// panel happens to have been dragged to.
	ui.noticeBody->set_max_width_chars(52);
	ui.noticeBody->set_halign(Gtk::ALIGN_START);
	ui.notice->pack_start(*ui.noticeBody, false, false);

	// The exact thing to paste. Shown, not hidden behind the button that
	// copies it: nobody should have to run a command they cannot read.
	ui.commandBox = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_VERTICAL, 0));
	ui.commandBox->get_style_context()->add_class("kb-well");
	ui.commandLabel = Gtk::manage(new Gtk::Label());
	ui.commandLabel->set_xalign(0);
	ui.commandLabel->set_line_wrap(true);
	ui.commandLabel->set_line_wrap_mode(Pango::WRAP_WORD_CHAR);
	ui.commandLabel->set_selectable(true);
	ui.commandLabel->set_margin_top(6);
	ui.commandLabel->set_margin_bottom(6);
	ui.commandLabel->set_margin_start(8);
	ui.commandLabel->set_margin_end(8);
	ui.commandLabel->get_style_context()->add_class("kb-numeric");
	ui.commandBox->pack_start(*ui.commandLabel, false, false);
	ui.notice->pack_start(*ui.commandBox, false, false);
	deviceBox->pack_start(*ui.notice, false, false);

	// --- The one row of actions. Rescan is always in the same place, in
	// the same words the rest of the window points at ("press Rescan in
	// the Device tab"), and wears the accent while it is the way out.
	ui.chooserRow = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 6));
	ui.chooserLabel = Gtk::manage(new Gtk::Label("Keyboard:"));
	ui.chooserLabel->set_mnemonic_widget(m_deviceCombo);
	// Named, for the same reason every other combo in the window is: left
	// to itself this one announces the product string it is showing, so
	// the only combo in the window whose value is already forty
	// characters long said all forty of them twice and never said it was
	// the chooser.
	if (Glib::RefPtr<Atk::Object> spokenFor = m_deviceCombo.get_accessible())
		spokenFor->set_name("Keyboard");
	m_deviceCombo.set_hexpand(true);
	// Product strings run to forty characters. Without an ellipsis the
	// combo's shortest width is that whole string, and the row it is in
	// pushes Rescan off the edge of the window.
	fitToColumn(m_deviceCombo);
	// This row only exists when there are two keyboards to choose between,
	// and two Logitech product strings can be forty characters that differ
	// at the thirtieth. Ellipsized to fit the column, the row that exists
	// to tell them apart would not. Every other combo on this tab chooses
	// between phrases that are already distinct in their first few words.
	m_deviceCombo.signal_changed().connect([this]() {
		m_deviceCombo.set_tooltip_text(m_deviceCombo.get_active_text());
	});
	ui.chooserRow->pack_start(*ui.chooserLabel, false, false);
	ui.chooserRow->pack_start(m_deviceCombo, true, true);
	// Its own row, above the keyboard it is choosing between: sharing one
	// with the name and the buttons left all three squeezed, and the name
	// wrapping into four lines beside a combo that stretched to match.
	// Choose, then read what you chose.
	deviceBox->pack_start(*ui.chooserRow, false, false);
	Gtk::Box *actionRow = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 6));
	// Packed in the order the eye reads them, so Tab reaches them in that
	// order too: what is open, then the second offer, then the button at
	// the right-hand end where every other card in this tab keeps the one
	// that carries its section out.
	//
	// What holds that end is a blank that stands in for the name in the
	// states where there is no name to print — every state where something
	// has gone wrong. Packing the buttons from the other end instead would
	// hold the same corner and cost the focus chain: a box that is filled
	// from both ends hands Tab back to the first of them for ever, and a
	// keyboard-only reader could reach Rescan and nothing after it.
	//
	// The buttons keep to the top of the row: the name beside them wraps to
	// two lines and then to three as the panel narrows, and a button that
	// slides down the card as the text grows is a button that has moved.
	actionRow->pack_start(*ui.identity, true, true);
	ui.filler = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 0));
	ui.filler->set_no_show_all(true);
	actionRow->pack_start(*ui.filler, true, true);
	m_rescanButton.set_valign(Gtk::ALIGN_START);
	m_rescanButton.set_tooltip_text("Look for connected keyboards again, "
		"and for everything else in the machine");
	// Rescan means the whole machine, not only the keyboards: someone who
	// has just plugged something in and pressed it expects the list below
	// to have noticed.
	m_rescanButton.signal_clicked().connect([this]() {
		fillInventory(deviceUI(this));
	});
	ui.identity->set_valign(Gtk::ALIGN_CENTER);
	ui.altButton = Gtk::manage(new Gtk::Button());
	ui.altButton->set_valign(Gtk::ALIGN_START);
	ui.altButton->signal_clicked().connect(
		sigc::mem_fun(*this, &MainWindow::onPrintDevice));
	actionRow->pack_start(*ui.altButton, false, false);
	actionRow->pack_start(m_rescanButton, false, false);
	deviceBox->pack_start(*actionRow, false, false);

	// --- Manual / unsupported device (full -dv/-dp/-ds/-tuk parity).
	// An expert path, and one the "that keyboard is not one I know"
	// notice can open with the ids already filled in.
	Gtk::Box *manualBox = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_VERTICAL, 4));
	manualBox->pack_start(*hintLabel(
		"Drive a keyboard this build does not know by giving its USB ids and "
		"saying which supported model it behaves like. Wrong answers do "
		"nothing worse than leave it dark."), false, false);
	m_manualDeviceCheck.set_label("_Use these settings instead of the scan");
	manualBox->pack_start(m_manualDeviceCheck, false, false);
	Gtk::Grid *idGrid = Gtk::manage(new Gtk::Grid());
	idGrid->set_column_spacing(8);
	idGrid->set_row_spacing(4);
	m_vidEntry.set_placeholder_text("046d");
	m_vidEntry.set_max_length(4);
	m_vidEntry.set_width_chars(6);
	m_pidEntry.set_placeholder_text("c331");
	m_pidEntry.set_max_length(4);
	m_pidEntry.set_width_chars(6);
	// Two fields share one label, so neither can borrow it: a reader would
	// announce both of them as "vendor and product id".
	if (Glib::RefPtr<Atk::Object> access = m_vidEntry.get_accessible())
		access->set_name("Vendor id, four hex digits");
	if (Glib::RefPtr<Atk::Object> access = m_pidEntry.get_accessible())
		access->set_name("Product id, four hex digits");
	m_serialEntry.set_placeholder_text("any");
	// Its label says "Serial number:" and a reader would find it through
	// the relation the row builds, but "any" is the whole meaning of
	// leaving it blank and only the placeholder says so.
	if (Glib::RefPtr<Atk::Object> access = m_serialEntry.get_accessible())
		access->set_name("Serial number, blank to match any");
	Gtk::Box *idRow = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 6));
	idRow->pack_start(m_vidEntry, false, false);
	Gtk::Label *idSep = Gtk::manage(new Gtk::Label(":"));
	idRow->pack_start(*idSep, false, false);
	idRow->pack_start(m_pidEntry, false, false);
	addFieldRow(idGrid, 0, "Vendor and product id:", *idRow);
	addFieldRow(idGrid, 1, "Serial number:", m_serialEntry);
	m_protocolCombo.append("Detect automatically");
	m_protocolCombo.append("Like a G810 / G410 / G512");
	m_protocolCombo.append("Like a G910");
	m_protocolCombo.append("Like a G213 (zones)");
	m_protocolCombo.append("Like a G815");
	m_protocolCombo.set_active(0);
	fitToColumn(m_protocolCombo);
	addFieldRow(idGrid, 2, "Behaves like:", m_protocolCombo);
	manualBox->pack_start(*idGrid, false, false);
	ui.manualExpander = foldNamed("Advanced: unsupported or test keyboard");
	ui.manualExpander->add(*manualBox);
	deviceBox->pack_start(*ui.manualExpander, false, false);

	// --- The rest of the machine. Under the keyboard, because the
	// keyboard is what this window drives and everything below it is
	// something it can only look at.
	if (ui.tabBox) {
		Gtk::Box *box = addSection(ui.tabBox, "Everything else in this machine");
		Gtk::Label *lead = Gtk::manage(new Gtk::Label(
			"Found by reading only — the memory's own SPD, the USB "
			"descriptors, and what Bluetooth already knows. None of it is "
			"written to, and none of it but the keyboard above can be lit "
			"from here."));
		lead->set_xalign(0);
		lead->set_line_wrap(true);
		lead->set_line_wrap_mode(Pango::WRAP_WORD_CHAR);
		lead->get_style_context()->add_class("kb-hint");
		box->pack_start(*lead, false, false);
		ui.inventory = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_VERTICAL, 4));
		ui.inventory->set_margin_top(8);
		box->pack_start(*ui.inventory, false, false);
		// Filled once the window is up rather than while it is being
		// built: the Bluetooth half is a call onto the system bus, and a
		// wedged bluetoothd must not hold the first paint.
		Glib::signal_idle().connect_once([&ui]() { fillInventory(ui); });
	}

	// --- Diagnostics, at the foot of the tab and shut. What was two
	// buttons of the same weight as everything else is now the report
	// they used to print, one fold away and copyable in one piece.
	if (ui.tabBox) {
		Gtk::Box *diagBox = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_VERTICAL, 4));
		ui.diagReport = Gtk::manage(new Gtk::Label());
		ui.diagReport->set_xalign(0);
		ui.diagReport->set_line_wrap(true);
		ui.diagReport->set_line_wrap_mode(Pango::WRAP_WORD_CHAR);
		ui.diagReport->set_selectable(true);
		ui.diagReport->get_style_context()->add_class("kb-hint");
		diagBox->pack_start(*ui.diagReport, false, false);
		Gtk::Box *diagRow = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 6));
		// Kept, not dropped: it is the one button here that has to work
		// when there is no device, which is exactly when a report is
		// worth having.
		m_listKeyboardsButton.set_label("_Copy this report");
		m_listKeyboardsButton.get_style_context()->add_class("kb-quiet");
		diagRow->pack_end(m_listKeyboardsButton, false, false);
		diagBox->pack_start(*diagRow, false, false);
		ui.diagExpander = foldNamed("Diagnostics: what this computer can see");
		ui.diagExpander->get_style_context()->add_class("kb-hint");
		ui.diagExpander->set_margin_start(8);
		ui.diagExpander->set_margin_end(8);
		ui.diagExpander->set_margin_bottom(4);
		// Enough air above it that it reads as the foot of the tab and not
		// as one more card in the stack.
		ui.diagExpander->set_margin_top(8);
		ui.diagExpander->add(*diagBox);
		ui.diagExpander->property_expanded().signal_changed().connect(
			[this]() {
				DeviceUI &state = deviceUI(this);
				refreshReport(state, m_kbd, m_devices);
				if (state.diagExpander && state.diagExpander->get_expanded())
					revealFoot(state.diagExpander);
			});
		// Packed like everything else and then kept last, rather than
		// pinned to the bottom of the column: a pinned fold leaves a hole
		// between it and the cards whenever the tab is shorter than the
		// window, which on a keyboard with no M-keys is most of the time,
		// and a hole in the middle of a page reads as something missing.
		// The cards for the rest of this tab are added after this one, so
		// the place at the foot is taken later — when the tab is first
		// shown, by which time they are all in. (Not on the container's
		// "add": a box that is packed into is never added to, so that
		// signal does not come.)
		ui.tabBox->pack_start(*ui.diagExpander, false, false);
		ui.tabBox->signal_show().connect([this]() {
			DeviceUI &state = deviceUI(this);
			if (state.tabBox && state.diagExpander)
				state.tabBox->reorder_child(*state.diagExpander, -1);
		});
	}

	// What every dead end has in common: no device, no controls, the
	// notice up and the tab that carries it in front.
	ui.collapse = [this](const Trouble &trouble) {
		DeviceUI &state = deviceUI(this);
		m_deviceIsOpen = false;
		m_deviceLabel.set_text("");
		// There is nothing open to lose any more, so the wait for one
		// going away stops here rather than at each of the paths in.
		state.alive.disconnect();
		state.aliveNode.clear();
		invalidateDaemonStatus();
		// Without this the sections that build*Section() hid at startup
		// stay visible (show_all_children re-showed them), so the window
		// advertises G-keys, regions, wave and audio with no device.
		refreshFeatures();
		setControlsEnabled(false);
		updatePendingState();
		// refreshFeatures() writes "No keyboard connected" across the empty
		// board for every missing device. When we know better — one is
		// plugged in and will not open — the two halves of the window must
		// not be telling the user different things.
		if (!trouble.board.empty())
			m_regionHintLabel.set_markup(
				"<span size=\"large\">" +
				Glib::Markup::escape_text(trouble.title) + "</span>\n\n" +
				Glib::Markup::escape_text(trouble.board));
		// The strip above the tabs is written from "is a device open?"
		// alone, so it says "No keyboard connected" even when one is
		// plugged in and the trouble is a door this user may not open.
		// This panel knows which of the states it is in and says so; the
		// line under it is the next step for a reader who has the account
		// in front of them, and the way to the account for one who has not.
		state.stripTitle = trouble.strip;
		state.hintAway = trouble.stripHint;
		state.hintHere = trouble.stripHere.empty() ? trouble.stripHint :
			trouble.stripHere;
		// raiseTab, below, ends with this tab in front whatever was there
		// before, so the line to start from is the one for a reader who is
		// looking at the notice.
		state.stripHint = state.hintHere;
		if (!state.stripTitle.empty()) {
			m_stateDeviceLabel.set_text(state.stripTitle);
			m_stateBoardLabel.set_text(state.stripHint);
		}
		// Plugging a keyboard in, or granting access to the one that is
		// already there, is the user finishing the job the notice asked
		// for. Making them come back and press a button to be told it
		// worked is the window not paying attention. Nothing is open, so
		// looking costs a few milliseconds of a machine that is otherwise
		// idle, and it stops the moment there is something to talk to.
		state.watch.disconnect();
		state.watchFor = trouble.watch;
		state.watchVendorID = trouble.vendorID;
		state.watchProductID = trouble.productID;
		state.watchSerial = trouble.serial;
		// Only worth waiting for something that has not happened yet.
		// Arming a watch on a condition that already holds — a keyboard
		// that is still listed but would not open — would retry the same
		// failed open a second later, and a second after that, for ever.
		if (trouble.watch != watchNothing && !m_manualOverrideActive &&
		    !watchSatisfied(m_kbd, state))
			state.watch = Glib::signal_timeout().connect([this]() -> bool {
				DeviceUI &s = deviceUI(this);
				if (m_kbd.isOpen() || m_manualOverrideActive) return false;
				if (!watchSatisfied(m_kbd, s)) return true;
				// Hand the slot back before the rescan: it may leave a
				// fresh watch here for whatever it finds next, and this
				// one is about to end by returning false.
				s.watch = sigc::connection();
				rescanDevices();
				return false;
			}, kWatchMs);

		// Last, and after the wait is settled: what the notice puts the
		// accent on depends on whether anything here is worth pressing.
		showTrouble(state, m_rescanButton, trouble, state.watch.connected());
		// Where the reader was, so that a keyboard coming back can put them
		// there again. Only the first notice records it: a second one is
		// this tab interrupting itself, and the place worth going back to
		// is still the one before any of it.
		int here = -1;
		Gtk::Notebook *book = bookOf(state.tabBox, &here);
		if (book && state.returnPage < 0 && here >= 0 &&
		    book->get_current_page() != here)
			state.returnPage = book->get_current_page();
		raiseTab(state.tabBox);
		refreshReport(state, m_kbd, m_devices);
	};

	// Called once a keyboard is open, from every path that opens one: keep
	// an eye on it being there, and say so the moment it is not.
	ui.watchDevice = [this]() {
		DeviceUI &state = deviceUI(this);
		state.alive.disconnect();
		if (!m_kbd.isOpen()) return;
		LedKeyboard::DeviceInfo device = m_kbd.getCurrentDevice();
		state.aliveNode = findNode(device.vendorID, device.productID,
			device.serialNumber).path;
		// A build that cannot name the device node has nothing to look at,
		// and a watch that can never see anything is worse than none: it
		// would be a promise to notice.
		if (state.aliveNode.empty()) return;
		state.alive = Glib::signal_timeout().connect([this]() -> bool {
			DeviceUI &s = deviceUI(this);
			if (!m_kbd.isOpen()) { s.alive = sigc::connection(); return false; }
			// The device node goes when the device does, so its absence is
			// the answer and one stat() is the whole question.
			// G810_NO_DEVICE, set while this is running, is the other way
			// in — it is how the unplugging can be watched happening on a
			// desk where the keyboard stays plugged in.
			const Pretend hiding = pretend();
			const bool gone = hiding == Pretend::nothing ||
				hiding == Pretend::unknown ||
				access(s.aliveNode.c_str(), F_OK) != 0;
			if (!gone) return true;
			s.alive = sigc::connection();
			// Named before the handle is let go: the name in the sentence
			// comes off the device this window was talking to, and after
			// the close there is nothing left to ask.
			LedKeyboard::DeviceInfo lost = m_kbd.getCurrentDevice();
			stopAnimationIfRunning();
			m_kbd.close();
			m_deviceIsOpen = false;
			// The list this window is holding is now a list of what used to
			// be there, and the chooser is showing it. Scanning again is
			// what makes the two agree — and it is also what quietly picks
			// the keyboard back up when what really happened was a device
			// arriving again under a new node.
			rescanDevices();
			if (m_kbd.isOpen() || !m_devices.empty()) return false;
			// The scan's own sentence is "no keyboard connected", which is
			// true but is not what happened. This one is.
			Trouble trouble = vanished(m_originalSupported.empty() ?
				m_kbd.SupportedKeyboards : m_originalSupported, lost);
			status(trouble.title);
			if (s.collapse) s.collapse(trouble);
			return false;
		}, kAliveMs);
	};

	// "See the Device tab" is worth saying to a reader who is somewhere
	// else and is an arrow pointing at the reader's own feet to one who is
	// here. The line under the strip's heading follows the tab in front,
	// so it is the next step while the account is on screen and the way to
	// the account while it is not.
	if (Gtk::Notebook *book = bookOf(ui.tabBox, nullptr))
		// Tracked against the label it writes and not merely connected.
		// Taking the window apart destroys that label before it destroys
		// the notebook, and destroying the notebook hides its pages,
		// which raises this signal — so an untracked connection ran this
		// slot through a freed GtkLabel and GTK said so.
		book->signal_switch_page().connect(sigc::track_obj(
			[this](Gtk::Widget *page, guint /*number*/) {
				DeviceUI &state = deviceUI(this);
				if (state.stripTitle.empty() || !state.tabBox) return;
				// By the page rather than by its number: the number is the
				// tab order, which this panel does not own.
				const bool here = page && state.tabBox->is_ancestor(*page);
				const std::string line = here ? state.hintHere : state.hintAway;
				if (line.empty() || line == state.stripHint) return;
				state.stripHint = line;
				m_stateBoardLabel.set_text(line);
			}, m_stateBoardLabel, *book));

	// Anything that redraws the strip — loading a profile is the reachable
	// one with no keyboard open — writes the general "No keyboard
	// connected" back over the particular thing this panel had put there.
	// While a notice is up this panel is the one that knows which state
	// the window is in, so it puts its own sentence back. Comparing before
	// writing is what stops the two of them calling each other for ever.
	m_stateDeviceLabel.property_label().signal_changed().connect([this]() {
		DeviceUI &state = deviceUI(this);
		if (state.stripTitle.empty()) return;
		if (m_stateDeviceLabel.get_text() == state.stripTitle) return;
		m_stateDeviceLabel.set_text(state.stripTitle);
	});
	m_stateBoardLabel.property_label().signal_changed().connect([this]() {
		DeviceUI &state = deviceUI(this);
		if (state.stripTitle.empty()) return;
		if (m_stateBoardLabel.get_text() == state.stripHint) return;
		m_stateBoardLabel.set_text(state.stripHint);
	});

	// show_all_children() runs after this and would light up both halves
	// of the card at once, so the parts that are shown one at a time are
	// taken out of its reach here, with their own contents already shown.
	ui.notice->show_all();
	ui.notice->set_no_show_all(true);
	ui.notice->hide();
	ui.commandBox->set_no_show_all(true);
	ui.identity->show_all();
	ui.identity->set_no_show_all(true);
	ui.altButton->set_no_show_all(true);
	// The chooser is left to show_all_children() and then put right by the
	// scan that follows it in the constructor — a combo that has never been
	// shown at all does not come back cleanly from set_visible().
}

// The clipboard and the Advanced fold, from the one button in the notice
// that is not Rescan. (MainWindow.h has no member for it, so it borrows
// onPrintDevice's slot — the old "Print info" button is gone, and what it
// printed is on the card and in the diagnostics fold instead.)
void MainWindow::onPrintDevice() {
	DeviceUI &ui = deviceUI(this);
	if (ui.alt == altCopyCommand && !ui.command.empty()) {
		Gtk::Clipboard::get()->set_text(ui.command);
		// The step this notice was asking for has been taken, so the accent
		// comes off it. Where the panel is watching for the rule to take
		// effect it will come back on its own, and lighting up a button
		// that does by hand what the window is already doing would take
		// that promise back; where it is not, Rescan is what is left.
		const bool waiting = ui.watch.connected();
		if (ui.altButton)
			ui.altButton->get_style_context()->remove_class("kb-primary");
		if (!waiting)
			m_rescanButton.get_style_context()->add_class("kb-primary");
		status(waiting ?
			"Copied — run it in a terminal and this window will pick the "
				"keyboard up" :
			"Copied — run it in a terminal, then press Rescan");
		return;
	}
	if (ui.alt == altOpenAdvanced) {
		if (ui.altVendorID) m_vidEntry.set_text(hexId(ui.altVendorID));
		if (ui.altProductID) m_pidEntry.set_text(hexId(ui.altProductID));
		if (ui.manualExpander) ui.manualExpander->set_expanded(true);
		m_protocolCombo.grab_focus();
		status("Choose the model it behaves like, then tick the box");
	}
}

// "Copy this report". (MainWindow.h calls the slot onListKeyboards; what
// it lists is now on screen above the button rather than flashed through
// the status bar.)
void MainWindow::onListKeyboards() {
	Gtk::Clipboard::get()->set_text(deviceReport(m_kbd, m_devices));
	status("Device report copied to the clipboard");
}

void MainWindow::buildGKeysSection(Gtk::Box* gKeysBox) {
	if (!m_gKeysFrame) m_gKeysFrame = (Gtk::Frame*)gKeysBox->get_parent();
	gKeysBox->pack_start(*hintLabel(
		"These go straight to the keyboard — there is nothing to send "
		"afterwards."), false, false);
	Gtk::Grid *grid = Gtk::manage(new Gtk::Grid());
	grid->set_column_spacing(8);
	grid->set_row_spacing(4);
	static const char *mrNames[] = { "Off", "On" };
	for (const char *name : mrNames) m_mrKeyCombo.append(name);
	m_mrKeyCombo.set_active(0);
	fitToColumn(m_mrKeyCombo);
	addFieldRow(grid, 0, "MR key light:", m_mrKeyCombo);
	fitToColumn(m_mnKeyCombo);
	addFieldRow(grid, 1, "M-key light:", m_mnKeyCombo);
	static const char *gmodeNames[] = { "Act as the F keys", "Send their own codes" };
	for (const char *name : gmodeNames) m_gKeysModeCombo.append(name);
	m_gKeysModeCombo.set_active(0);
	fitToColumn(m_gKeysModeCombo);
	addFieldRow(grid, 2, "G-keys:", m_gKeysModeCombo);
	gKeysBox->pack_start(*grid, false, false);
	m_applyGKeysButton.set_halign(Gtk::ALIGN_END);
	gKeysBox->pack_start(m_applyGKeysButton, false, false);
	if (m_gKeysFrame) m_gKeysFrame->set_visible(false);
}

void MainWindow::buildStartupSection(Gtk::Box* startupBox) {
	if (!m_startupFrame) m_startupFrame = (Gtk::Frame*)startupBox->get_parent();
	startupBox->pack_start(*hintLabel(
		"What the keyboard shows on its own, before this computer has "
		"said anything to it."), false, false);
	static const char *startupNames[] = { "Wave animation", "Fixed color" };
	for (const char *name : startupNames) m_startupModeCombo.append(name);
	m_startupModeCombo.set_active(0);
	fitToColumn(m_startupModeCombo);
	Gtk::Grid *grid = Gtk::manage(new Gtk::Grid());
	grid->set_column_spacing(8);
	addFieldRow(grid, 0, "At power-on:", m_startupModeCombo);
	startupBox->pack_start(*grid, false, false);
	m_applyStartupButton.set_halign(Gtk::ALIGN_END);
	startupBox->pack_start(m_applyStartupButton, false, false);
	if (m_startupFrame) m_startupFrame->set_visible(false);
}

void MainWindow::buildOnBoardSection(Gtk::Box* onBoardBox) {
	if (!m_onBoardFrame) m_onBoardFrame = (Gtk::Frame*)onBoardBox->get_parent();
	onBoardBox->pack_start(*hintLabel(
		"While the keyboard is in charge it ignores colors sent from here."),
		false, false);
	// "On-board" / "Software" is the protocol's word for it; say what it
	// means for the user instead.
	static const char *onboardNames[] = {
		"The keyboard controls its own lighting",
		"This computer controls the lighting"
	};
	for (const char *name : onboardNames) m_onBoardModeCombo.append(name);
	m_onBoardModeCombo.set_active(0);
	fitToColumn(m_onBoardModeCombo);
	Gtk::Grid *grid = Gtk::manage(new Gtk::Grid());
	grid->set_column_spacing(8);
	addFieldRow(grid, 0, "In charge:", m_onBoardModeCombo);
	onBoardBox->pack_start(*grid, false, false);
	m_applyOnBoardButton.set_halign(Gtk::ALIGN_END);
	onBoardBox->pack_start(m_applyOnBoardButton, false, false);
	if (m_onBoardFrame) m_onBoardFrame->set_visible(false);
}

void MainWindow::rescanDevices() {
	DeviceUI &ui = deviceUI(this);
	stopAnimationIfRunning();
	setBusy(true);
	if (m_originalSupported.empty()) {
		m_originalSupported = m_kbd.SupportedKeyboards;
	}
	m_devices.clear();
	const Pretend hiding = pretend();
	std::vector<LedKeyboard::DeviceInfo> found;
	if (hiding != Pretend::nothing && hiding != Pretend::unknown)
		found = m_kbd.listKeyboards();
	for (LedKeyboard::DeviceInfo device : found) {
		// One entry per HID interface; dedupe by serial number.
		bool duplicate = false;
		for (const LedKeyboard::DeviceInfo &known : m_devices) {
			if (known.vendorID == device.vendorID &&
			    known.productID == device.productID &&
			    (!device.serialNumber.empty() ?
			     known.serialNumber == device.serialNumber :
			     known.serialNumber.empty())) {
				duplicate = true;
				break;
			}
		}
		if (!duplicate)
			m_devices.push_back(device);
	}
	m_ignoreDeviceChange = true;
	m_deviceCombo.remove_all();
	for (const LedKeyboard::DeviceInfo &device : m_devices) {
		std::string label = device.product;
		if (!device.serialNumber.empty())
			label += " (" + device.serialNumber + ")";
		// A keyboard reached over Bluetooth is a keyboard this program
		// cannot light: the writes are accepted by the transport and
		// dropped by the firmware, so it would sit here looking like a
		// working target and do nothing at all. Say which it is in the
		// one place the choice is made.
		if (device.bluetooth)
			label += " — over Bluetooth, cannot be lit";
		m_deviceCombo.append(label);
	}
	if (!m_devices.empty())
		m_deviceCombo.set_active(0);
	m_ignoreDeviceChange = false;
	// One keyboard needs no chooser: the card names it, and a combo with a
	// single row is a control that cannot do anything.
	const bool chooseable = m_devices.size() > 1;
	if (ui.chooserLabel) ui.chooserLabel->set_visible(chooseable);
	if (ui.chooserRow) ui.chooserRow->set_visible(chooseable);
	m_deviceCombo.set_visible(chooseable);
	setBusy(false);
	if (m_devices.empty()) {
		// "Nothing is plugged in" and "that one is not a model I know" are
		// different problems; only the raw HID list can tell them apart.
		Foreign foreign = probeForeign(m_kbd.SupportedKeyboards,
			hiding == Pretend::unknown);
		if (hiding == Pretend::nothing) foreign.found = false;
		Trouble trouble = nothingConnected(m_kbd.SupportedKeyboards, foreign);
		status(trouble.title);
		if (ui.collapse) ui.collapse(trouble);
	} else if (!m_manualOverrideActive) {
		openSelectedDevice();
	}
}

bool MainWindow::openSelectedDevice() {
	DeviceUI &ui = deviceUI(this);
	int index = m_deviceCombo.get_active_row_number();
	if (index < 0 || index >= (int)m_devices.size())
		return false;
	stopAnimationIfRunning();
	LedKeyboard::DeviceInfo device = m_devices[index];
	if (device.bluetooth) {
		// Opening would succeed and every write after it would succeed,
		// and the keyboard would sit there unchanged. Refusing here is
		// what keeps the rest of the window truthful: nothing downstream
		// has to wonder whether its colours arrived.
		Trouble trouble = overBluetooth(m_originalSupported.empty() ?
			m_kbd.SupportedKeyboards : m_originalSupported, device);
		status(trouble.title);
		if (ui.collapse) ui.collapse(trouble);
		return false;
	}
	const Pretend hiding = pretend();
	const bool refuse = hiding == Pretend::denied || hiding == Pretend::busy;
	if (refuse || !m_kbd.open(device.vendorID, device.productID,
	                          device.serialNumber)) {
		if (!refuse && errno == ENODEV) {
			// It was in the list a moment ago: it has been unplugged, or
			// it answers on a different interface now.
			Trouble trouble = vanished(m_originalSupported.empty() ?
				m_kbd.SupportedKeyboards : m_originalSupported, device);
			status(trouble.title);
			if (ui.collapse) ui.collapse(trouble);
			return false;
		}
		// No error bar: the notice this raises is the whole account, and
		// it carries the fix. Two of them, each with its own Rescan
		// button, would be the window arguing with itself.
		// The real table, not whatever the Advanced fold may have put in
		// its place: the keyboard is named from it, and a forced protocol
		// would have it calling this one by another model's name.
		Trouble trouble = cannotOpen(m_originalSupported.empty() ?
			m_kbd.SupportedKeyboards : m_originalSupported, device);
		status(trouble.title);
		if (ui.collapse) ui.collapse(trouble);
		return false;
	}
	m_deviceIsOpen = true;
	m_openVendorID = device.vendorID;
	m_openProductID = device.productID;
	m_openSerial = device.serialNumber;
	invalidateDaemonStatus();
	showConnected(ui, m_rescanButton);
	if (ui.identityName)
		ui.identityName->set_markup("<b>" +
			Glib::Markup::escape_text(device.product) + "</b>");
	m_deviceLabel.set_text(deviceDetail(m_kbd, device));

	// Rebuild the layout and reset the draft when the model changed.
	LedKeyboard::KeyboardModel model = m_kbd.getKeyboardModel();
	bool resetLayout = model != m_layoutModel;
	if (resetLayout) {
		m_layoutModel = model;
		m_features = help::getKeyboardFeatures(model);
		m_keyboardWidget.setLayoutFlags(layoutFlagsFor(m_features, m_kbd.getKeyboardModel()));
		m_appliedColors = m_keyboardWidget.getKeyColors();
		m_regionDraft.clear();
		m_regionApplied.clear();
		m_allKeysDraft = Gdk::RGBA("#000000");
		m_allKeysApplied = Gdk::RGBA("#000000");
		onSelectionChanged();
		// reset non-color tracked state for new device
		m_hasAppliedEffect = false;
		m_hasAppliedGKeys = false;
		m_hasAppliedStartup = false;
		m_hasAppliedOnBoard = false;
	}
	refreshFeatures();
	if (resetLayout)
		restorePreview();
	setControlsEnabled(true);
	updatePendingState();
	refreshReport(ui, m_kbd, m_devices);
	if (ui.watchDevice) ui.watchDevice();
	status("Connected to " + device.product);
	return true;
}

bool MainWindow::ensureOpen() {
	if (m_kbd.isOpen())
		return true;
	if (m_manualOverrideActive) {
		tryManualOpen();
		return m_kbd.isOpen();
	}
	if (m_devices.empty()) {
		status("No keyboard connected — press Rescan in the Device tab");
		raiseTab(deviceUI(this).tabBox);
		return false;
	}
	return openSelectedDevice();
}

void MainWindow::onManualDeviceToggled() {
	m_manualOverrideActive = m_manualDeviceCheck.get_active();
	m_deviceCombo.set_sensitive(!m_manualOverrideActive);
	if (m_manualOverrideActive) {
		tryManualOpen();
	} else {
		if (!m_originalSupported.empty()) m_kbd.SupportedKeyboards = m_originalSupported;
		// Back to the scan, which is what unticking this means. The
		// hand-entered device is closed on the way out: leaving it open
		// behind a panel that has gone back to reporting the scan is how
		// the window ends up driving one keyboard while naming another.
		stopAnimationIfRunning();
		if (m_kbd.isOpen()) m_kbd.close();
		rescanDevices();
	}
}

void MainWindow::onDeviceChanged() {
	// m_rebuilding as well as m_ignoreDeviceChange: opening a device is
	// what calls refreshFeatures, so letting a combo's own churn start
	// another open would re-enter the rebuild that is still running.
	if (!m_ignoreDeviceChange && !m_rebuilding)
		openSelectedDevice();
}

// Marks the field and returns false when it cannot be a USB id, so the
// problem is visible where it is made rather than as a failed open later.
bool MainWindow::validateIdEntry(Gtk::Entry &entry, const char *what) {
	const std::string text = entry.get_text();
	bool ok = !text.empty();
	for (size_t i = 0; i < text.size() && ok; ++i)
		ok = std::isxdigit((unsigned char)text[i]) != 0;
	if (text.empty()) {
		// Empty means "match anything", which is legitimate.
		entry.unset_icon(Gtk::ENTRY_ICON_SECONDARY);
		return true;
	}
	if (ok) {
		entry.unset_icon(Gtk::ENTRY_ICON_SECONDARY);
	} else {
		entry.set_icon_from_icon_name("dialog-error", Gtk::ENTRY_ICON_SECONDARY);
		entry.set_icon_tooltip_text(
			std::string(what) + " is a 4-digit hex number, like 046d",
			Gtk::ENTRY_ICON_SECONDARY);
	}
	return ok;
}

void MainWindow::onManualEntryChanged() {
	bool ok = validateIdEntry(m_vidEntry, "Vendor ID");
	ok = validateIdEntry(m_pidEntry, "Product ID") && ok;
	if (m_manualDeviceCheck.get_active() && ok)
		tryManualOpen();
}

void MainWindow::tryManualOpen() {
	if (!m_manualOverrideActive) return;
	DeviceUI &ui = deviceUI(this);
	stopAnimationIfRunning();

	uint16_t vid = 0, pid = 0;
	std::string ser;
	try {
		std::string vids = m_vidEntry.get_text();
		std::string pids = m_pidEntry.get_text();
		vid = (uint16_t) std::stoul( vids.size()>=2 && vids[1]=='x' ? vids : "0x"+vids , nullptr, 16);
		pid = (uint16_t) std::stoul( pids.size()>=2 && pids[1]=='x' ? pids : "0x"+pids , nullptr, 16);
		ser = m_serialEntry.get_text();
	} catch (...) {
		status("Fill in both ids as 4-digit hex, like 046d and c331");
		return;
	}

	int proto = m_protocolCombo.get_active_row_number();
	if (proto > 0) {
		// Override SupportedKeyboards like CLI -tuk
		LedKeyboard::KeyboardModel forced = LedKeyboard::KeyboardModel::g810;
		switch (proto) {
			case 1: forced = LedKeyboard::KeyboardModel::g810; break;
			case 2: forced = LedKeyboard::KeyboardModel::g910; break;
			case 3: forced = LedKeyboard::KeyboardModel::g213; break;
			case 4: forced = LedKeyboard::KeyboardModel::g815; break;
			default: break;
		}
		m_kbd.SupportedKeyboards = { {vid, pid, (uint16_t)forced} };
	} else if (!m_originalSupported.empty()) {
		m_kbd.SupportedKeyboards = m_originalSupported;
	}

	if (!m_kbd.open(vid, pid, ser)) {
		LedKeyboard::DeviceInfo asked;
		asked.vendorID = vid;
		asked.productID = pid;
		asked.serialNumber = ser;
		Trouble trouble;
		if (errno == ENODEV) {
			trouble.title = "Nothing here answers to " + hexId(vid) + ":" +
				hexId(pid);
			trouble.body = "No device with those ids is plugged in. Open "
				"Diagnostics at the foot of this tab to see what this computer "
				"can see, or turn this off and press Rescan.";
			trouble.board = "No device answers to the ids typed into\n"
				"Advanced on the Device tab.";
			trouble.strip = "Nothing at those ids";
			trouble.stripHint = "No device answers to the ids typed into "
				"Advanced";
		} else {
			trouble = cannotOpen(m_originalSupported.empty() ?
				m_kbd.SupportedKeyboards : m_originalSupported, asked);
		}
		status(trouble.title);
		if (ui.collapse) ui.collapse(trouble);
		if (ui.manualExpander) ui.manualExpander->set_expanded(true);
		return;
	}

	m_deviceIsOpen = true;
	m_openVendorID = vid;
	m_openProductID = pid;
	m_openSerial = ser;
	invalidateDaemonStatus();

	LedKeyboard::KeyboardModel model = m_kbd.getKeyboardModel();
	bool resetLayout = model != m_layoutModel;
	if (resetLayout) {
		m_layoutModel = model;
		m_features = help::getKeyboardFeatures(model);
		m_keyboardWidget.setLayoutFlags(layoutFlagsFor(m_features, m_kbd.getKeyboardModel()));
		m_appliedColors = m_keyboardWidget.getKeyColors();
		m_regionDraft.clear();
		m_regionApplied.clear();
		m_allKeysDraft = Gdk::RGBA("#000000");
		m_allKeysApplied = Gdk::RGBA("#000000");
		onSelectionChanged();
		// reset non-color tracked state for new device
		m_hasAppliedEffect = false;
		m_hasAppliedGKeys = false;
		m_hasAppliedStartup = false;
		m_hasAppliedOnBoard = false;
	}
	refreshFeatures();
	if (resetLayout) restorePreview();
	setControlsEnabled(true);
	updatePendingState();
	LedKeyboard::DeviceInfo open = m_kbd.getCurrentDevice();
	showConnected(ui, m_rescanButton);
	if (ui.identityName)
		ui.identityName->set_markup("<b>" + Glib::Markup::escape_text(
			open.product.empty() ? "Set up by hand" : open.product) + "</b>");
	m_deviceLabel.set_text(deviceDetail(m_kbd, open));
	refreshReport(ui, m_kbd, m_devices);
	if (ui.watchDevice) ui.watchDevice();
	status(std::string("Connected to ") +
		(open.product.empty() ? hexId(vid) + ":" + hexId(pid) : open.product));
}

void MainWindow::onApplyGKeys() {
	if (blockedByAnimation("changing G-key settings"))
		return;
	if (!ensureOpen())
		return;
	int mr = m_mrKeyCombo.get_active_row_number();
	int mn = m_mnKeyCombo.get_active_row_number();
	int mode = m_gKeysModeCombo.get_active_row_number();
	bool ok = true;
	if (mr >= 0 && !m_kbd.setMRKey((uint8_t)mr))
		ok = false;
	if (mn >= 0) {
		uint8_t value = (m_kbd.getKeyboardModel() == LedKeyboard::KeyboardModel::g815) ?
			(uint8_t)(mn + 1) : (uint8_t)mn;
		if (!m_kbd.setMNKey(value))
			ok = false;
	}
	if (mode >= 0 && !m_kbd.setGKeysMode((uint8_t)mode))
		ok = false;
	if (ok) {
		m_hasAppliedGKeys = true;
		m_appliedMR = (mr >= 0 ? (uint8_t)mr : 0);
		uint8_t mnValue = (mn >= 0 ? (uint8_t)mn : 0);
		if (m_kbd.getKeyboardModel() == LedKeyboard::KeyboardModel::g815 && mn >= 0) {
			mnValue = (uint8_t)(mn + 1);
		}
		m_appliedMN = mnValue;
		m_appliedGKeysMode = (mode >= 0 ? (uint8_t)mode : 0);
	}
	if (ok) status("M / G key settings applied");
	else statusError("The keyboard would not take the M / G key settings");
}

void MainWindow::onApplyStartupMode() {
	if (blockedByAnimation("changing the startup mode"))
		return;
	if (!ensureOpen())
		return;
	LedKeyboard::StartupMode mode = m_startupModeCombo.get_active_row_number() == 1 ?
		LedKeyboard::StartupMode::color : LedKeyboard::StartupMode::wave;
	if (!m_kbd.setStartupMode(mode)) {
		statusError("The keyboard would not take the startup mode");
		return;
	}
	m_hasAppliedStartup = true;
	m_appliedStartup = mode;
	status(mode == LedKeyboard::StartupMode::color ?
		"Startup mode applied — it will come up in a fixed color" :
		"Startup mode applied — it will come up with the wave");
}

void MainWindow::onApplyOnBoardMode() {
	if (blockedByAnimation("changing the on-board mode"))
		return;
	if (!ensureOpen())
		return;
	LedKeyboard::OnBoardMode mode = m_onBoardModeCombo.get_active_row_number() == 1 ?
		LedKeyboard::OnBoardMode::software : LedKeyboard::OnBoardMode::board;
	if (!m_kbd.setOnBoardMode(mode)) {
		statusError("The keyboard would not change what controls the lighting");
		return;
	}
	m_hasAppliedOnBoard = true;
	m_appliedOnBoard = mode;
	status(mode == LedKeyboard::OnBoardMode::software ?
		"This computer is in charge of the lighting now" :
		"The keyboard is in charge of its own lighting now");
}

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
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

#include "MainWindowShared.h"

// The On-board effects tab. The keyboard's firmware owns these, so they
// are an instant lane: applying one writes a packet and hands the LEDs
// over until the effect is turned off again.
//
// Which is exactly why this is the one lane whose result you could not
// see before committing it to the hardware, so the panel draws the
// chosen effect on the on-screen board first, through the same frame
// path the live effects use. Nothing in the preview touches the device.
//
// Every word on this tab belongs to one vocabulary, and it is the effect
// lane's: an effect is applied, and the keyboard then runs it on its own.
// Colours are sent, unsent and discarded — that is the other lane, and
// sameWords() in PanelStrip.cpp says why the two are deliberately
// different acts and why only the colour lane's phrasing is rewritten.
// The two may name each other, but no sentence here may use both verbs
// for one act. "Waves — nothing is sent until you press Apply effect" did
// exactly that: the colour lane's verb and this lane's button, in one
// breath, about a single press.

namespace {

// The nine effects, in the order the packet enum and the profile loader
// in MainWindow.cpp both use. Rows may be renamed; they may not be
// reordered or added to without changing those two places as well.
struct EffectInfo {
	const char *name;
	const char *about;
	bool usesColor;
	bool usesPeriod;
	const char *cycleWord;   // what one period is one of
};

const EffectInfo effectTable[] = {
	{ "Fixed color",
	  "Every key holds the color you pick, and nothing moves.",
	  true,  false, NULL },
	{ "Breathing",
	  "One color fades in and out, over and over.",
	  true,  true,  "breath" },
	{ "Color cycle",
	  "The whole keyboard drifts through the rainbow together.",
	  false, true,  "pass" },
	{ "Waves",
	  "A band of rainbow rolls across the board.",
	  false, true,  "pass" },
	{ "Horizontal wave",
	  "A band of rainbow rolls from side to side.",
	  false, true,  "pass" },
	{ "Vertical wave",
	  "A band of rainbow rolls from top to bottom.",
	  false, true,  "pass" },
	{ "Center wave",
	  "A band of rainbow spreads out from the middle of the board.",
	  false, true,  "pass" },
	// The row the session file leaves chosen, so this is the first thing
	// most people read on this tab. It has to say what the tab is for and
	// what the row means — and, since Off is the one row with no effect to
	// hand over, why the Apply effect button below it is switched off.
	//
	// "With no on-board effect" and not "the keyboard has no effect": the
	// keyboard may well be running one at this moment, and the strip says
	// so at the top of the window. This line describes the row, the way the
	// other eight do, and must not be read as a report on the hardware.
	{ "Off",
	  "With no on-board effect, the keyboard just shows the colors you send "
	  "it. There is nothing to apply — choose one of the others to see it "
	  "on the board first.",
	  false, false, NULL },
	// What this does is the firmware's business, and the firmwares differ.
	// On a G512 it floods the board with the colour and twinkles one key
	// at a time; nothing responds to typing, which is the star-like
	// effect Keyboard.cpp warns this group conflicts with on the g410 and
	// g810 — the same protocol this model speaks. Promising rings there
	// was the whole of the trouble: the colour was sent but the row said
	// there was none, so it went out as the picker's red, and a red flood
	// on a red board looked like an effect that had not arrived at all.
	{ "Ripple",
	  "Meant to send a ring of light out from each key you press. Not "
	  "every keyboard's firmware does that — some fill the board with "
	  "this color and twinkle a key instead.",
	  true,  true,  "ring" }
};
const int effectCount = (int)(sizeof(effectTable) / sizeof(effectTable[0]));
const int effectOffRow = 7;
const int effectRippleRow = 8;

// The packet each row asks for, in the same order. Applying reads it, and
// so does the strip's sentence, which has to know whether the picture on
// the board is of the effect the keyboard is already running.
const LedKeyboard::NativeEffect nativeEffects[] = {
	LedKeyboard::NativeEffect::color,
	LedKeyboard::NativeEffect::breathing,
	LedKeyboard::NativeEffect::cycle,
	LedKeyboard::NativeEffect::waves,
	LedKeyboard::NativeEffect::hwave,
	LedKeyboard::NativeEffect::vwave,
	LedKeyboard::NativeEffect::cwave,
	LedKeyboard::NativeEffect::off,
	LedKeyboard::NativeEffect::ripple
};

// Which part of the keyboard an effect runs on, in the order the parts
// list offers them. The packet has no name for "all" — Keyboard.cpp sends
// it as keys and logo one after the other — so the rows are the panel's
// own, and this is where they become the protocol's.
const LedKeyboard::NativeEffectPart nativeParts[] = {
	LedKeyboard::NativeEffectPart::all,
	LedKeyboard::NativeEffectPart::keys,
	LedKeyboard::NativeEffectPart::logo
};
const int partCount = (int)(sizeof(nativeParts) / sizeof(nativeParts[0]));

// Whether the effect running on `part` would touch this key. The logo is
// the only part the firmware can drive separately, so it is the only one
// the picture has to hold back.
bool keyInPart(LedKeyboard::Key key, int part) {
	const bool logo = key == LedKeyboard::Key::logo ||
		key == LedKeyboard::Key::logo2;
	if (part == 1)
		return !logo;
	if (part == 2)
		return logo;
	return true;
}

// Where each control sits in the form grid. onEffectChanged() shows and
// hides whole rows, and finds them through the grid rather than through
// members: the rows are this panel's own furniture, and the window has no
// other use for them.
//
// The order is the order of the questions — what the effect is, what it
// looks like on the screen, and what applying it does to the keyboard —
// so the one switch that writes to the keyboard's own memory sits against
// the button that would write it.
enum FormRow {
	rowEffect = 0,
	rowAbout,
	rowTarget,
	rowColor,
	rowPeriod,
	rowPreview,
	rowStore,
	rowActions
};

const double twoPi = 6.283185307179586;

// --- The preview clock -------------------------------------------------
//
// The panel's own state, and nothing else in the window has any business
// with it: there is one window, one effects panel and one preview, and
// the tick is disconnected the moment the panel leaves the screen —
// another tab, the tray, or quitting.
sigc::connection s_previewTick;
gint64 s_previewStartedAt = 0;
bool s_previewShowing = false;   // this panel is driving the screen board
bool s_panelOnScreen = false;    // the tab is the one being looked at
bool s_animationSeen = false;    // a live effect owned the board last tick
bool s_stillDrawn = false;       // a motionless preview needs one frame only
bool s_captionsSized = false;    // the label column has been measured
int s_previewPart = 0;           // which part the drawn frame covers

// The strip's board line while the preview has the board. See ownCaption:
// s_previewed is the effect the picture is showing and s_caption is the
// last sentence this panel put on that line.
sigc::connection s_captionHeld;
std::string s_previewed;
std::string s_caption;
bool s_writingCaption = false;

// The hex field beside the colour well, and whether the panel is the one
// writing in it.
Gtk::Entry *s_hexEntry = NULL;
bool s_syncingHex = false;

// The cell the Apply effect button sits in, which is what switches it off
// on the Off row. See where it is packed.
Gtk::Box *s_applyCell = NULL;

// The window is a local in main(), so its members are destroyed one by
// one, in reverse order of declaration, while the widget tree is still
// standing. Destroying the period field makes GTK read its entry back
// and announce a value change, which lands in onEffectChanged() — by
// which time the buttons, the players it asks about and the board it
// draws on are already gone. The last of this panel's widgets to be
// built is the first to be destroyed, so its farewell is the earliest
// honest answer to "is there still a window here".
bool s_windowGone = false;

// --- The one line that says what the board is showing ------------------
//
// While the preview draws, the board is a picture of an effect that the
// keyboard has not been given, and the strip's own sentence — "Showing
// your colors" — is then wrong about eight of the nine effects. The
// sentence is written in PanelStrip.cpp from boardStateText(), which knows
// about live effects and about an effect already applied but has no way to
// know that this panel is drawing; the branch belongs there, and adding it
// needs a member on the window to hang it from.
//
// Until it has one: while the preview is running this panel holds that
// line, and hands it straight back when the preview stops. Everything else
// in the window still writes to it whenever it likes — a pending count, a
// device rescan, the animation poll — so instead of repainting over them
// fifteen times a second, the line is watched, and anything written over
// the panel's sentence is put back before it can be drawn.

// "Showing your colors" is a sentence about the keyboard, and it stays
// true while the picture shows something else — so it is kept, and what
// the picture is doing is said in front of it. In front, and not after,
// because this line is ellipsized in a narrow window and the half that
// survives has to be the half that says this is only a preview.
std::string previewCaption(const std::string &effect, std::string board) {
	static const std::string showing = "Showing ";
	if (board.size() > showing.size() &&
	    board.compare(0, showing.size(), showing) == 0)
		board = "the keyboard is still showing " + board.substr(showing.size());
	else
		// Whatever else the strip has to say there — an unplugged keyboard,
		// most of it — is about a state this panel is on its way out of, and
		// the one thing worth keeping in front of it is what the picture is.
		// In this lane's own words: the effect has not been applied, and
		// saying "nothing has been sent" here would borrow the colour
		// lane's verb for it.
		board = "the keyboard has not been given it";
	return "Previewing " + effect + " — " + board;
}

void ownCaption(Gtk::Label &label, const std::string &effect,
                const std::string &board) {
	s_previewed = effect;
	if (!s_captionHeld.connected())
		// Whatever anyone else writes there is the keyboard's own truth and
		// is kept: it is wrapped rather than replaced, so a change in it —
		// a colour painted, an effect applied — still reaches this line.
		s_captionHeld = label.property_label().signal_changed().connect(
			[&label]() {
				if (s_writingCaption || s_previewed.empty())
					return;
				const std::string written = label.get_text().raw();
				if (written == s_caption)
					return;
				s_caption = previewCaption(s_previewed, written);
				s_writingCaption = true;
				label.set_text(s_caption);
				s_writingCaption = false;
			});
	const std::string wanted = previewCaption(effect, board);
	if (label.get_text().raw() == wanted)
		return;
	s_caption = wanted;
	s_writingCaption = true;
	label.set_text(wanted);
	s_writingCaption = false;
}

// Give the line back. The caller then asks the strip to write it again,
// which is what makes the handover a handover rather than a guess at what
// the strip would have said.
bool releaseCaption() {
	if (!s_captionHeld.connected() && s_previewed.empty())
		return false;
	s_captionHeld.disconnect();
	s_previewed.clear();
	s_caption.clear();
	return true;
}

// The last colour anybody meant this effect to have. See the guard in
// buildEffectsSection: a profile can push a colour into the well that
// belongs to an effect with no colour, and this is what goes back.
Gdk::RGBA &lastMeantColor() {
	static Gdk::RGBA color("#ff0000");
	return color;
}

bool isBlack(const Gdk::RGBA &rgba) {
	return rgbaChannel(rgba, 0) == 0 && rgbaChannel(rgba, 1) == 0 &&
		rgbaChannel(rgba, 2) == 0;
}

// What a person types when they have a colour in hand: #rrggbb, rrggbb,
// #rgb, in any case, spaces and all. The Colors tab reads one the same
// way; the two would be one function if this panel owned a place to put
// it. Anything else is refused rather than half-read, so the field can
// say plainly what it wanted.
bool parseHex(const std::string &text, Gdk::RGBA &color) {
	std::string digits;
	for (size_t i = 0; i < text.size(); ++i) {
		const unsigned char c = (unsigned char)text[i];
		if (std::isspace(c) || c == '#')
			continue;
		digits += (char)std::tolower(c);
	}
	if (digits.size() != 3 && digits.size() != 6)
		return false;
	for (size_t i = 0; i < digits.size(); ++i)
		if (!std::isxdigit((unsigned char)digits[i]))
			return false;
	if (digits.size() == 3) {
		std::string expanded;
		for (size_t i = 0; i < 3; ++i)
			expanded += std::string(2, digits[i]);
		digits = expanded;
	}
	const unsigned long value = std::strtoul(digits.c_str(), NULL, 16);
	color.set_rgba(((value >> 16) & 0xff) / 255.0,
	               ((value >> 8) & 0xff) / 255.0,
	               (value & 0xff) / 255.0, 1.0);
	return true;
}

// A colour on the rainbow. `turns` is a position around the wheel in
// whole turns (any value, any sign); `value` is brightness.
LedKeyboard::Color wheelColor(double turns, double value) {
	turns -= std::floor(turns);
	const double h = turns * 6.0;
	const int sector = (int)h % 6;
	const double f = h - std::floor(h);
	double r = 0.0, g = 0.0, b = 0.0;
	switch (sector) {
		case 0: r = 1.0; g = f; break;
		case 1: r = 1.0 - f; g = 1.0; break;
		case 2: g = 1.0; b = f; break;
		case 3: g = 1.0 - f; b = 1.0; break;
		case 4: r = f; b = 1.0; break;
		default: r = 1.0; b = 1.0 - f; break;
	}
	LedKeyboard::Color color;
	color.red = (uint8_t)std::floor(r * value * 255.0 + 0.5);
	color.green = (uint8_t)std::floor(g * value * 255.0 + 0.5);
	color.blue = (uint8_t)std::floor(b * value * 255.0 + 0.5);
	return color;
}

LedKeyboard::Color dimColor(const LedKeyboard::Color &color, double value) {
	LedKeyboard::Color result;
	result.red = (uint8_t)std::floor(color.red * value + 0.5);
	result.green = (uint8_t)std::floor(color.green * value + 0.5);
	result.blue = (uint8_t)std::floor(color.blue * value + 0.5);
	return result;
}

// One frame of an effect over the key grid.
//
// This is a likeness, not the firmware's own animation — the packet
// carries an effect number, not a pattern, so the shapes here are read
// from what each effect is called and what it does on a board. It
// answers "which of these nine do I want", which is the question you
// cannot otherwise ask without writing all nine to the keyboard.
//
// `part` is the row chosen in the parts list: the keys the effect will not
// touch are left out of the frame entirely, so they keep whatever the
// board already had — which is exactly what the firmware does with them.
void renderEffectFrame(int index, int part, double seconds,
                       double periodSeconds,
                       const LedKeyboard::Color &color,
                       const std::map<LedKeyboard::Key,
                           KeyboardWidget::KeyPosition> &positions,
                       LedKeyboard::KeyValueArray &frame) {
	if (index < 0 || index >= effectCount || positions.empty())
		return;
	if (periodSeconds < 0.05)
		periodSeconds = 0.05;

	// Key units, so a ripple comes out round: the grid counts columns in
	// quarters of a keycap and rows in whole ones.
	double minX = 1e9, maxX = -1e9, minY = 1e9, maxY = -1e9;
	for (std::map<LedKeyboard::Key, KeyboardWidget::KeyPosition>::const_iterator
	     it = positions.begin(); it != positions.end(); ++it) {
		const double x = it->second.second / 4.0;
		const double y = it->second.first;
		if (x < minX) minX = x;
		if (x > maxX) maxX = x;
		if (y < minY) minY = y;
		if (y > maxY) maxY = y;
	}
	const double spanX = maxX - minX > 0.01 ? maxX - minX : 1.0;
	const double spanY = maxY - minY > 0.01 ? maxY - minY : 1.0;
	const double reach = std::sqrt(spanX * spanX + spanY * spanY);
	const double phase = seconds / periodSeconds;

	// Ripple: a few rings at a time, each starting from a key somewhere
	// on the board and fading as it spreads.
	struct Ring { double x, y, radius, hue, fade; };
	std::vector<Ring> rings;
	if (index == effectRippleRow) {
		const int ringCount = 3;
		for (int i = 0; i < ringCount; ++i) {
			const double when = phase + (double)i / ringCount;
			const unsigned serial = (unsigned)std::floor(when);
			const double age = when - std::floor(when);
			// Knuth's multiplicative hash: a centre that stays put while
			// its own ring spreads, and is somewhere else next time.
			const size_t pick = (size_t)((serial * 2654435761u) %
				(unsigned)positions.size());
			std::map<LedKeyboard::Key, KeyboardWidget::KeyPosition>::const_iterator
				centre = positions.begin();
			for (size_t step = 0; step < pick; ++step) ++centre;
			Ring ring;
			ring.x = centre->second.second / 4.0;
			ring.y = centre->second.first;
			ring.radius = age * reach;
			ring.hue = serial * 0.618;
			ring.fade = 1.0 - age * age;
			rings.push_back(ring);
		}
	}

	frame.reserve(positions.size());
	for (std::map<LedKeyboard::Key, KeyboardWidget::KeyPosition>::const_iterator
	     it = positions.begin(); it != positions.end(); ++it) {
		if (!keyInPart(it->first, part))
			continue;
		const double x = it->second.second / 4.0;
		const double y = it->second.first;
		const double u = (x - minX) / spanX;   // 0 left,  1 right
		const double v = (y - minY) / spanY;   // 0 top,   1 bottom
		LedKeyboard::KeyValue value;
		value.key = it->first;
		switch (index) {
			case 0:   // fixed color
				value.color = color;
				break;
			case 1:   // breathing
				value.color = dimColor(color,
					0.06 + 0.94 * (0.5 - 0.5 * std::cos(phase * twoPi)));
				break;
			case 2:   // color cycle: the whole board on one hue
				value.color = wheelColor(phase, 1.0);
				break;
			case 3:   // waves: the firmware picks the direction, so this
			          // one rolls corner to corner rather than claim one
				value.color = wheelColor(phase - 0.6 * u - 0.4 * v, 1.0);
				break;
			case 4:   // horizontal wave
				value.color = wheelColor(phase - u, 1.0);
				break;
			case 5:   // vertical wave
				value.color = wheelColor(phase - v, 1.0);
				break;
			case 6: { // center wave
				const double dx = (x - (minX + maxX) / 2.0) / (spanX / 2.0);
				const double dy = (y - (minY + maxY) / 2.0) / (spanY / 2.0);
				value.color = wheelColor(
					phase - 0.5 * std::sqrt(dx * dx + dy * dy), 1.0);
				break;
			}
			case effectRippleRow: { // ripple
				double best = 0.0, hue = 0.0;
				for (size_t r = 0; r < rings.size(); ++r) {
					const double dx = x - rings[r].x;
					const double dy = y - rings[r].y;
					const double edge = (std::sqrt(dx * dx + dy * dy) -
						rings[r].radius) / (0.09 * reach);
					const double lit = std::exp(-edge * edge) * rings[r].fade;
					if (lit > best) { best = lit; hue = rings[r].hue; }
				}
				value.color = wheelColor(hue, best);
				break;
			}
			default:  // off: there is nothing to show but their colours
				value.color.red = value.color.green = value.color.blue = 0;
				break;
		}
		frame.push_back(value);
	}
}

// A row of the form: its caption in the first column and the field in the
// second. A field that does not fill the column gets a line beside it
// saying what the value means, in the space that would otherwise be the
// empty right half of the card.
//
// `unit` is what the number in the field is counted in. It is a separate
// label from the note because it is the only part of that line that
// cannot be guessed from anything else on screen: the note shortens to an
// ellipsis in a narrow window, and "1000" with no unit beside it is a
// different control from "1000 ms".
//
// The caption is the field's label in the accessibility tree too, which
// is what makes the colour well announce itself as "Color".
//
// `extra` is a second control for the same value — the colour's hex — and
// sits against the field, inside the label's reach, because it is the
// same answer typed rather than picked.
void attachRow(Gtk::Grid *form, int row, const char *caption,
               Gtk::Widget &field, bool fills, const char *spoken,
               const char *unit = NULL, Gtk::Widget *extra = NULL) {
	Gtk::Label *label = Gtk::manage(new Gtk::Label());
	label->set_text_with_mnemonic(caption);
	label->set_xalign(0);
	label->set_valign(Gtk::ALIGN_CENTER);
	label->set_mnemonic_widget(field);
	form->attach(*label, 0, row, 1, 1);
	// Left to itself a combo announces its own value and nothing about
	// what it chooses, and a spin button announces nothing at all: the
	// caption has to be said out loud as well as drawn.
	if (Glib::RefPtr<Atk::Object> spokenFor = field.get_accessible())
		spokenFor->set_name(spoken);

	Gtk::Box *cell = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 8));
	cell->set_hexpand(true);
	cell->pack_start(field, fills, fills);
	if (extra)
		cell->pack_start(*extra, false, false);
	if (unit) {
		Gtk::Label *units = Gtk::manage(new Gtk::Label(unit));
		units->set_xalign(0);
		units->set_valign(Gtk::ALIGN_CENTER);
		// The unit belongs to the number, not to the sentence after it.
		units->set_margin_end(6);
		units->get_style_context()->add_class("kb-hint");
		cell->pack_start(*units, false, false);
	}
	if (!fills) {
		Gtk::Label *note = Gtk::manage(new Gtk::Label());
		note->set_xalign(0);
		note->set_valign(Gtk::ALIGN_CENTER);
		note->set_ellipsize(Pango::ELLIPSIZE_END);
		note->get_style_context()->add_class("kb-hint");
		cell->pack_start(*note, true, true);
	}
	form->attach(*cell, 1, row, 1, 1);
}

// The form itself, from any field in it. The panel's widgets belong to
// the window, and the rows they sit in belong to the widget tree, so the
// tree is what the two functions here share instead of a member.
Gtk::Grid *formOf(Gtk::Widget &field) {
	return dynamic_cast<Gtk::Grid*>(field.get_ancestor(Gtk::Grid::get_type()));
}

// The line beside a field: it is the last thing in the field's own row
// box, so the field is the way back to it.
Gtk::Label *noteBeside(Gtk::Widget &field) {
	Gtk::Box *cell = dynamic_cast<Gtk::Box*>(field.get_parent());
	if (!cell)
		return NULL;
	std::vector<Gtk::Widget*> children = cell->get_children();
	return children.size() > 1 ?
		dynamic_cast<Gtk::Label*>(children[children.size() - 1]) : NULL;
}

void setNote(Gtk::Widget &field, const std::string &text) {
	Gtk::Label *note = noteBeside(field);
	if (!note)
		return;
	note->set_text(text);
	note->set_visible(!text.empty());
}

// A grid gives no height to a row whose children are all hidden, which
// is what lets the form show only the parameters the chosen effect
// has, with no hole where the others were.
void showRow(Gtk::Grid *form, int row, bool visible) {
	for (int column = 0; column < 2; ++column)
		if (Gtk::Widget *cell = form->get_child_at(column, row))
			cell->set_visible(visible);
}

// The label column is as wide as the widest caption that can appear in
// it, not as the widest one showing: rows come and go with the chosen
// effect, and fields that slide sideways when they do read as a different
// form each time. A size group cannot do this — the grid gives an
// invisible child no width at all — so the captions are measured, hidden
// ones included, and every one of them is given that width as a floor.
//
// Called once the window is up and once the captions hold the words this
// keyboard's own hardware calls for. A label that has never been on a
// screen is measured in whatever font it was born with rather than the
// window's, and comes out narrower than the words it will have to hold.
void sizeCaptions(Gtk::Grid *form) {
	// GTK measures a hidden widget as nothing, and the two captions that
	// come and go with the effect are exactly the ones that have to be
	// counted, so they are shown for the length of the measurement. The
	// row above them is left alone: whether a keyboard has a part to
	// choose between is settled by the hardware and never changes again,
	// and holding a column open for a word that can never appear is the
	// same empty gap by another name.
	const int varying[] = { rowColor, rowPeriod };
	bool wasShown[2] = { false, false };
	for (int i = 0; i < 2; ++i)
		if (Gtk::Widget *cell = form->get_child_at(0, varying[i])) {
			wasShown[i] = cell->get_visible();
			cell->set_visible(true);
		}
	int gutter = 0;
	for (int row = rowEffect; row <= rowPeriod; ++row) {
		Gtk::Widget *cell = form->get_child_at(0, row);
		if (!cell || cell == form->get_child_at(1, row))
			continue;   // a row that spans both columns has no caption
		int minimum = 0, natural = 0;
		cell->set_size_request(-1, -1);
		cell->get_preferred_width(minimum, natural);
		if (natural > gutter)
			gutter = natural;
	}
	for (int i = 0; i < 2; ++i)
		if (Gtk::Widget *cell = form->get_child_at(0, varying[i]))
			cell->set_visible(wasShown[i]);
	for (int row = rowEffect; row <= rowPeriod; ++row) {
		Gtk::Widget *cell = form->get_child_at(0, row);
		if (cell && cell != form->get_child_at(1, row))
			cell->set_size_request(gutter, -1);
	}
}

// The window rebuilds this list whenever the device changes, in the
// protocol's words: all, keys, logo. Those are the packet's words, not a
// person's, so the panel says what they mean — and says it again after
// every rebuild, which is why this is here and not in the build.
//
// Returns how many parts this keyboard has, which is how the row above
// knows whether there is a choice to offer at all.
int nameTargets(Gtk::ComboBoxText &combo) {
	static const char *names[] = { "Whole keyboard", "Keys only", "Logo only" };
	Glib::RefPtr<Gtk::TreeModel> model = combo.get_model();
	if (!model)
		return 0;
	const int rows = (int)model->children().size();
	if (rows < 1 || rows > 3)
		return rows;
	int row = 0;
	bool named = true;
	for (Gtk::TreeModel::iterator it = model->children().begin();
	     it != model->children().end() && row < rows; ++it, ++row) {
		Glib::ustring text;
		it->get_value(0, text);
		if (text.raw() != names[row]) {
			named = false;
			break;
		}
	}
	if (named)
		return rows;
	// remove_all() takes the rows out one at a time, and taking out the
	// active one makes the combo say it changed — which lands in
	// onEffectChanged, which calls back into here, which clears the same
	// store again while this call is still walking it. That left the
	// outer clear holding a freed iterator, and switching keyboards
	// (which renames the parts) was enough to crash the window. One pass
	// at a time through here is the whole fix.
	static bool renaming = false;
	if (renaming)
		return rows;
	renaming = true;
	const int active = combo.get_active_row_number();
	combo.remove_all();
	for (int i = 0; i < rows; ++i)
		combo.append(names[i]);
	combo.set_active(active >= 0 && active < rows ? active : 0);
	renaming = false;
	return rows;
}

// What to try when the keyboard takes a packet and does not answer. It is
// not a missing keyboard: ensureOpen() has just said the device is there,
// so the failure is one of a keyboard that has stopped listening, and the
// remedy is the one on the Device tab rather than the error bar's own
// button, which is offered only when nothing is open at all.
std::vector<std::string> whatToTry() {
	return std::vector<std::string>(1,
		"It may have been unplugged, or another program may be holding it. "
		"Rescan for keyboards on the Device tab, then try again.");
}

std::string secondsText(double milliseconds, const char *cycleWord) {
	char buffer[96];
	std::snprintf(buffer, sizeof(buffer), "one %s every %.1f s",
		cycleWord ? cycleWord : "pass", milliseconds / 1000.0);
	return std::string(buffer);
}

}  // namespace

void MainWindow::buildEffectsSection(Gtk::Box* effectBox) {
	for (int i = 0; i < effectCount; ++i)
		m_effectCombo.append(effectTable[i].name);
	m_effectCombo.set_active(0);
	m_effectCombo.set_tooltip_text(
		"One of the effects built into the keyboard's own firmware");

	// Seven rows, and at 980x620 the page they sit in is 258 pixels tall:
	// the card wants about 380 of those and the two buttons are what falls
	// off the bottom. It cannot be made to fit — four rows and no prose at
	// all would still be 260 — so what is left is to say that there is more
	// below. A permanent scrollbar was tried here in place of GTK's overlay
	// one, which is invisible until it is touched: it takes enough width
	// off the page to raise a second scrollbar along the bottom, and the
	// sheet draws its steppers as two empty boxes. The sign this window
	// uses everywhere else is the fade the stylesheet paints on the cut
	// edge (undershoot.bottom in Styling.h), and it is the one used here.
	// The fix that would work is not in this file: the action row would
	// have to sit outside the scrolled page, which is makeTab's.
	Gtk::Grid *form = Gtk::manage(new Gtk::Grid());
	form->set_row_spacing(6);
	form->set_column_spacing(12);
	form->set_hexpand(true);
	effectBox->pack_start(*form, false, false);

	attachRow(form, rowEffect, "Effec_t", m_effectCombo, true, "Effect");

	// What the chosen effect does, in the words someone with the keyboard
	// on their desk would use. It sits under the chooser because that is
	// where the question is.
	Gtk::Label *about = Gtk::manage(new Gtk::Label());
	about->set_xalign(0);
	about->set_line_wrap(true);
	about->set_line_wrap_mode(Pango::WRAP_WORD_CHAR);
	about->set_max_width_chars(38);
	about->get_style_context()->add_class("kb-hint");
	form->attach(*about, 0, rowAbout, 2, 1);

	// The parts list takes its natural width, like the two fields under
	// it: the line beside it is what the choice means, and it is the only
	// answer this row gives that stays on the screen.
	nameTargets(m_targetCombo);
	attachRow(form, rowTarget, "R_uns on", m_targetCombo, false,
		"Runs on which part of the keyboard");
	m_targetCombo.set_tooltip_text(
		"Which part of the keyboard the effect runs on");

	m_effectColorButton.set_use_alpha(false);
	m_effectColorButton.set_rgba(lastMeantColor());
	m_effectColorButton.set_tooltip_text("The color this effect uses");
	m_effectColorButton.get_style_context()->add_class("kb-swatch");
	// The same well as the one on the Colors tab, at the same size: a
	// colour is picked the same way everywhere in this window, and a
	// swatch that changed shape from tab to tab would suggest otherwise.
	m_effectColorButton.set_size_request(72, -1);

	// And the same hex beside it, in the same typeable field, for the same
	// reason: someone with a colour in hand should not have to find it in a
	// colour wheel on one tab and be able to paste it on another.
	Gtk::Entry *hex = Gtk::manage(new Gtk::Entry());
	hex->set_width_chars(8);
	hex->set_max_width_chars(8);
	hex->set_max_length(7);
	hex->set_valign(Gtk::ALIGN_CENTER);
	hex->get_style_context()->add_class("kb-numeric");
	hex->set_tooltip_text("The effect color as hex — type one and press Enter");
	if (Glib::RefPtr<Atk::Object> spoken = hex->get_accessible())
		spoken->set_name("Effect color, hex value");
	s_hexEntry = hex;
	attachRow(form, rowColor, "Co_lor", m_effectColorButton, false,
		"Effect color", NULL, hex);

	m_periodSpin.set_numeric(true);
	m_periodSpin.set_digits(0);
	m_periodSpin.set_width_chars(5);   // 65000, the longest period there is
	m_periodSpin.set_tooltip_text("How long one full cycle of the effect takes");
	// The field steps in the unit the line beside it speaks: a tenth of a
	// second per press, a whole second per page. It stepped by 256ms, a
	// quantum of the wave packet's high byte — but every effect that has a
	// period is sent both of its bytes (Keyboard.cpp writes data[9]/[10]
	// for breathing, data[11]/[12] for the cycle, and data[16] over that
	// same low byte for the waves), so the coarse step bought nothing and
	// cost the row the only number on it a person would have typed.
	if (Glib::RefPtr<Gtk::Adjustment> cycle = m_periodSpin.get_adjustment()) {
		cycle->set_lower(100.0);
		cycle->set_step_increment(100.0);
		cycle->set_page_increment(1000.0);
	}
	attachRow(form, rowPeriod, "Cy_cle", m_periodSpin, false,
		"Cycle length in milliseconds", "ms");

	// The preview is a picture, not a promise: say so where it is turned
	// on, so nobody reads a rolling on-screen rainbow as a keyboard that
	// has already been written to. "The board" is what the rest of the
	// window calls the picture and "the keyboard" is the thing on the
	// desk, which matters most in these two lines, where the difference
	// between them is the whole point.
	//
	// The two switches were tried on one line, to spend thirty fewer
	// pixels of a card that does not fit at 980x620. It bought nothing —
	// the card overruns that window by more than thirty either way — and
	// it cost ninety pixels of the narrowest this tab can be, because two
	// checkbox labels side by side cannot be shortened by the window the
	// way an ellipsized note can. A row each, then.
	//
	// Alt+R, and not the V of "Preview": Reset view sits over the board and
	// is on the screen whenever this box is, and MainWindow.cpp turns Open…
	// and Save… down for that same letter for that same reason. Two live
	// claims on one letter make the letter useless.
	Gtk::CheckButton *preview = Gtk::manage(
		new Gtk::CheckButton("P_review it on the board"));
	preview->set_use_underline(true);
	preview->set_active(true);
	preview->set_tooltip_text(
		"Draw the effect on the picture of the keyboard. Nothing reaches "
		"the keyboard itself until you press Apply effect");
	preview->signal_toggled().connect(
		sigc::mem_fun(*this, &MainWindow::onEffectChanged));
	form->attach(*preview, 0, rowPreview, 2, 1);

	m_storeCheck.set_use_underline(true);
	m_storeCheck.set_label("Re_member it on the keyboard");
	// The tooltip is written by onEffectChanged, because what this box
	// remembers depends on which row is chosen: an effect, or — on the Off
	// row, where Turn effect off reads the same box — no effect at all.
	form->attach(m_storeCheck, 0, rowStore, 2, 1);

	// One height for the three fields. Their left edges were already in
	// one column and their boxes were 37, 41 and 44 pixels tall, which the
	// eye reads as three unrelated controls rather than three rows of one
	// form. GTK keeps the group alive for as long as the widgets in it.
	Glib::RefPtr<Gtk::SizeGroup> fieldHeights =
		Gtk::SizeGroup::create(Gtk::SIZE_GROUP_VERTICAL);
	fieldHeights->add_widget(m_effectCombo);
	fieldHeights->add_widget(m_targetCombo);
	fieldHeights->add_widget(m_effectColorButton);
	fieldHeights->add_widget(m_periodSpin);
	fieldHeights->add_widget(*hex);

	// The two acts of this tab, side by side, and neither of them ever
	// renamed. This row used to hold one button that read "Apply effect"
	// or "Turn effect off" depending on the row chosen above it, which
	// made the same pixels two different controls, left the note over the
	// tab promising a button that was not on the screen, and let the label
	// disagree with the tooltip under it. Both are here in every state
	// now; what changes is which of them can be pressed.
	//
	// The middle rung and not the filled one. One button in this window
	// is filled, and it is Send to keyboard, which sits above these tabs
	// and never moves; a tab's own action is the accent as an outline, so
	// that a screen never has two equally loud verbs on it.
	//
	// No frame around the pair. It carried .kb-toolbar, whose border and
	// padding pushed both buttons five pixels to the right of the caption
	// column they sit under — and a toolbar is a strip of tools, which is
	// the Paint/Select pair over the board, not the two commits at the
	// foot of a form. Send and Discard are drawn as a bare pair; so is
	// this.
	Gtk::Box *actions = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 8));
	actions->set_homogeneous(true);
	actions->set_margin_top(8);
	m_applyEffectButton.get_style_context()->add_class("kb-secondary");
	// The label is set once, here, and never again. Alt+A and not the Alt+F
	// the window's own constructor gives it: the menu bar's File has that
	// letter and a window's menu wins the key, and an accelerator
	// advertised on the one action a tab exists for, which opens a menu
	// instead, is worse than none at all.
	m_applyEffectButton.set_label("_Apply effect");
	m_applyEffectButton.set_use_underline(true);
	m_effectOffButton.set_tooltip_text(
		"Stop the effect the keyboard is running and put your colors back");
	// Off is the one row with no effect to hand over, so Apply effect is
	// dead there — and the button's own sensitivity is not this panel's to
	// keep: setControlsEnabled() and updateAnimUI(), in two other files,
	// both write it, and updateAnimUI() has the last word. The cell it
	// sits in is this panel's alone, and a child of an insensitive parent
	// is insensitive whatever its own flag says, so the rule holds without
	// this file having to win an argument fifteen times a second.
	Gtk::Box *applyCell = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL));
	applyCell->pack_start(m_applyEffectButton, true, true);
	s_applyCell = applyCell;
	actions->pack_start(*applyCell, true, true);
	actions->pack_start(m_effectOffButton, true, true);
	form->attach(*actions, 0, rowActions, 2, 1);

	// See s_windowGone. This is the last widget the panel builds, so it is
	// the first one the window takes back, and everything below still
	// answers questions truthfully at the moment it says goodbye. gtkmm
	// wraps no signal for a widget's own destruction, so this is the C
	// one; the handler keeps to statics, which outlive everything.
	g_signal_connect(m_effectOffButton.gobj(), "destroy",
		G_CALLBACK(+[](GtkWidget*, gpointer) {
			s_windowGone = true;
			s_hexEntry = NULL;
			s_applyCell = NULL;
			if (s_previewTick.connected())
				s_previewTick.disconnect();
			releaseCaption();
		}), NULL);

	// The preview follows the panel: no frames for a page nobody is
	// looking at, and the board is the user's own colours again the
	// moment they leave.
	m_effectCombo.signal_map().connect([this]() {
		s_panelOnScreen = true;
		onEffectChanged();
	});
	m_effectCombo.signal_unmap().connect([this]() {
		s_panelOnScreen = false;
		if (s_previewTick.connected())
			s_previewTick.disconnect();
		if (s_previewShowing) {
			s_previewShowing = false;
			if (!s_windowGone)
				clearPreviewState();
		}
		// The board is the user's own colours again, so the sentence about
		// it goes back to the strip that owns it.
		if (releaseCaption() && !s_windowGone)
			updateBoardState();
	});
	// Choosing an effect is an action, and the one thing it does not do is
	// reach the keyboard: the board is showing a picture of it. Say so —
	// but only to someone looking at this tab, so that a profile load or
	// a device rescan cannot talk over the message that belongs to it.
	//
	// What is said is where the keyboard stands, not what to press next:
	// the foot drops a trailing instruction anyway (withoutInstruction in
	// PanelStrip.cpp), and the button that would carry it out is on this
	// tab with its name written on it.
	//
	// The old sentence — "nothing is sent until you press Apply effect" —
	// borrowed the colour lane's verb for this lane's act, and it also said
	// the keyboard was running nothing while it might well have been
	// running this very effect. That is what the middle branch is for.
	m_effectCombo.signal_changed().connect([this]() {
		const int index = m_effectCombo.get_active_row_number();
		if (!s_panelOnScreen || index < 0 || index >= effectCount)
			return;
		const std::string name = effectTable[index].name;
		if (index == effectOffRow)
			// Which is also why the Apply effect button beside it is off.
			status("Off — no effect to apply");
		else if (m_hasAppliedEffect && m_deviceShowsPreview &&
		         nativeEffects[index] == m_appliedEffect)
			status(name + " — the keyboard is already running it");
		else
			status(name + " — the keyboard is not running it yet");
	});
	// The parts list used to be the one control on the tab that answered
	// nothing when you used it. It answers three ways now: the line beside
	// it says what the choice leaves alone, the preview stops lighting the
	// keys the effect will not touch, and the foot of the window says what
	// just changed.
	m_targetCombo.signal_changed().connect([this]() {
		if (s_windowGone)
			return;
		const int part = m_targetCombo.get_active_row_number();
		onEffectChanged();
		if (!s_panelOnScreen || part < 0 || part >= partCount)
			return;
		static const char *said[] = {
			"The effect will run on the whole keyboard",
			"The effect will run on the keys — the logo keeps your colors",
			"The effect will run on the logo — the keys keep your colors"
		};
		status(said[part]);
	});

	// A colour that arrives with an effect that has no colour is not a
	// colour anybody chose. The session file this window writes on the way
	// out ends with `fx off all`, whose colour is {0,0,0}, and
	// stageProfileCommands mirrors every fx line into this well — so
	// without this, every returning user found a black effect colour
	// waiting for them, and picking the first effect in the list painted
	// the whole board black. A good default beats a warning about a bad
	// one. Black chosen by hand, on an effect that does use a colour, is
	// still a choice and is still kept.
	m_effectColorButton.property_rgba().signal_changed().connect([this]() {
		if (s_windowGone)
			return;
		const Gdk::RGBA rgba = m_effectColorButton.get_rgba();
		if (!isBlack(rgba)) {
			lastMeantColor() = rgba;
			return;
		}
		const int index = m_effectCombo.get_active_row_number();
		if (index >= 0 && index < effectCount && effectTable[index].usesColor)
			return;
		m_effectColorButton.set_rgba(lastMeantColor());
	});
	// set_rgba() and set_value() are how a loaded profile reaches these,
	// and neither raises the "the user chose something" signal, so the
	// panel watches the values themselves.
	m_effectColorButton.property_rgba().signal_changed().connect(
		sigc::mem_fun(*this, &MainWindow::onEffectChanged));
	m_periodSpin.signal_value_changed().connect(
		sigc::mem_fun(*this, &MainWindow::onEffectChanged));

	// Committing the hex field: Enter takes it, and so does leaving it,
	// which is what a person expects of something they have just typed.
	// Anything unreadable is put back rather than half-applied.
	const sigc::slot<void> commitHex = [this]() {
		if (s_windowGone || s_syncingHex || !s_hexEntry)
			return;
		const std::string typed = s_hexEntry->get_text().raw();
		Gdk::RGBA color;
		if (parseHex(typed, color)) {
			if (!rgbaEqual(color, m_effectColorButton.get_rgba())) {
				m_effectColorButton.set_rgba(color);
				status("Effect color is now #" + rgbaToHex(color));
			}
			return;
		}
		s_syncingHex = true;
		s_hexEntry->set_text("#" + rgbaToHex(m_effectColorButton.get_rgba()));
		s_syncingHex = false;
		status("\"" + typed + "\" is not a color — type one like #ff8800");
	};
	hex->signal_activate().connect(commitHex);
	hex->signal_focus_out_event().connect(
		[commitHex](GdkEventFocus*) { commitHex(); return false; });

	onEffectChanged();
}

void MainWindow::onApplyEffect() {
	if (blockedByAnimation("applying an effect"))
		return;
	int effectIndex = m_effectCombo.get_active_row_number();
	if (effectIndex < 0) {
		status("Choose an effect first");
		return;
	}
	// The parts list always has a row chosen, and on a keyboard with only
	// one part the row is not even on screen; if a device rebuild has just
	// emptied the list, the whole keyboard is the only reading left.
	int targetIndex = m_targetCombo.get_active_row_number();
	if (targetIndex < 0)
		targetIndex = 0;
	if (!ensureOpen())
		return;
	if (targetIndex >= partCount)
		targetIndex = 0;
	const LedKeyboard::NativeEffectPart part = nativeParts[targetIndex];
	// Only an effect that takes a colour may be handed one. These bytes
	// go out with every effect packet, and the firmware reads them
	// whatever the effect is: a ripple sent with the picker's red came
	// back as a board of solid red, because the keyboard took the colour
	// and dropped the effect. The CLI has always sent zero here — its
	// ripple case parses a period and nothing else (src/main.cpp) — and
	// the two have to agree, since they are the same keyboard.
	//
	// The same for the period, for the same reason: what the table says
	// an effect uses is what it is sent.
	LedKeyboard::Color color = {0x00, 0x00, 0x00};
	if (effectTable[effectIndex].usesColor) {
		// currentColor() is the Brightness slider on intensity-only
		// models and the color picker everywhere else.
		color = has(help::KeyboardFeatures::rgb) ?
			toLedColor(m_effectColorButton.get_rgba()) : currentColor();
	}
	uint16_t periodMs = effectTable[effectIndex].usesPeriod ?
		(uint16_t)m_periodSpin.get_value_as_int() : 0;
	LedKeyboard::NativeEffectStorage storage = m_storeCheck.get_active() ?
		LedKeyboard::NativeEffectStorage::user :
		LedKeyboard::NativeEffectStorage::none;
	if (!m_kbd.setNativeEffect(nativeEffects[effectIndex], part,
	    std::chrono::duration<uint16_t, std::milli>(periodMs), color,
	    storage)) {
		// The Off row's button is dead, so this normally cannot be reached
		// with Off chosen — but the two other files that write that
		// button's sensitivity could light it up for a moment, and "would
		// not start the Off effect" is not a sentence to leave lying about
		// waiting for that moment.
		statusError(nativeEffects[effectIndex] == LedKeyboard::NativeEffect::off ?
			std::string("The keyboard would not turn its effect off") :
			std::string("The keyboard would not start the ") +
				effectTable[effectIndex].name + " effect", whatToTry());
		return;
	}
	m_hasAppliedEffect = true;
	m_appliedEffect = nativeEffects[effectIndex];
	m_appliedEffectPart = part;
	m_appliedEffectPeriod = std::chrono::duration<uint16_t, std::milli>(periodMs);
	m_appliedEffectColor = color;
	m_appliedEffectStorage = storage;
	if (nativeEffects[effectIndex] == LedKeyboard::NativeEffect::off) {
		// Picking "Off" in the combo sends the same firmware command as
		// the Turn effect off button, so it has to behave the same way.
		if (restoreAppliedState())
			status("Effect off — your colors are back");
		else
			status("Effect off");
	} else {
		// The firmware owns the LEDs from here; the draft and
		// m_appliedColors still describe the scheme underneath.
		m_deviceShowsPreview = true;
		// The picture and the keyboard now agree, so the strip's own
		// sentence is the true one again and the panel gives it back.
		onEffectChanged();
		updatePendingState();
		// Where it is running is part of what happened, on a keyboard with
		// more than one part to run it on.
		static const char *where[] = {
			" is running on the keyboard",
			" is running on the keys",
			" is running on the logo"
		};
		status(std::string(effectTable[effectIndex].name) +
			where[targetIndex] +
			" — Turn effect off restores your colors");
	}
}

void MainWindow::onEffectOff() {
	if (blockedByAnimation("changing the effect"))
		return;
	if (!ensureOpen())
		return;
	LedKeyboard::NativeEffectStorage storage = m_storeCheck.get_active() ?
		LedKeyboard::NativeEffectStorage::user :
		LedKeyboard::NativeEffectStorage::none;
	if (!m_kbd.setNativeEffect(LedKeyboard::NativeEffect::off,
	    LedKeyboard::NativeEffectPart::all,
	    std::chrono::duration<uint16_t, std::milli>(0), {0,0,0}, storage)) {
		statusError("The keyboard would not turn its effect off",
			whatToTry());
		return;
	}
	m_hasAppliedEffect = true;
	m_appliedEffect = LedKeyboard::NativeEffect::off;
	m_appliedEffectPart = LedKeyboard::NativeEffectPart::all;
	m_appliedEffectPeriod = std::chrono::duration<uint16_t, std::milli>(0);
	m_appliedEffectColor = {0,0,0};
	m_appliedEffectStorage = storage;
	// The chooser has to agree with the keyboard: leaving it on the
	// effect that is no longer running would leave the panel, the board
	// and the strip telling three different stories. It also stops the
	// preview, so the screen shows the colours the keyboard now has.
	m_effectCombo.set_active(effectOffRow);
	// Switching the effect off leaves the board on whatever the firmware
	// last drew, so put the scheme back.
	if (restoreAppliedState())
		status("Effect off — your colors are back");
	else
		status("Effect off");
}

// Everything the panel shows about the chosen effect: which parameters
// it has, what they mean, and what it looks like. Every control here
// leads back to this one function, and setControlsEnabled calls it last,
// so it has to apply the device gate itself.
void MainWindow::onEffectChanged() {
	// Not while refreshFeatures() is emptying the combos this reads: it
	// would be answering questions about a list that is halfway gone, and
	// the rebuild calls this again the moment it is whole.
	if (m_rebuilding)
		return;
	if (s_windowGone)
		return;
	Gtk::Grid *form = formOf(m_effectCombo);
	if (!form)
		return;
	const int index = m_effectCombo.get_active_row_number();
	const bool known = index >= 0 && index < effectCount;
	const EffectInfo &fx = effectTable[known ? index : 0];
	// Off is the absence of an effect, so it is the one row with nothing to
	// hand the keyboard. Half the panel turns on this.
	const bool turningOff = index == effectOffRow;

	// One lane at a time: while a software animation is driving the board,
	// every control here is dead. A tab that is dead for a reason should
	// give the reason, and where to undo it — and it should look dead,
	// rather than take a press and answer with a refusal.
	const char *animation = activeAnimationName();
	const bool live = m_controlsEnabled && !animation;
	if (Gtk::Label *about = dynamic_cast<Gtk::Label*>(
	    form->get_child_at(0, rowAbout)))
		about->set_text(animation ?
			std::string("The ") + animation + " live effect is running on "
				"this keyboard. Stop it on the Live effects tab to use these." :
			known ? fx.about : "");

	// Every part of this keyboard, in a person's words. A keyboard with no
	// separately lit logo has only one part, and "whole keyboard" and
	// "keys only" would then be two names for the same packet: a choice
	// with nothing on the other side of it is not a choice.
	const bool choiceOfParts = nameTargets(m_targetCombo) > 2;
	showRow(form, rowTarget, choiceOfParts);
	int part = m_targetCombo.get_active_row_number();
	if (!choiceOfParts || part < 0 || part >= partCount)
		part = 0;
	// What the choice leaves alone — the half of it that is not in the
	// name of the row that was chosen.
	static const char *partNotes[] = {
		"both the keys and the logo",
		"the logo keeps your colors",
		"the keys keep your colors"
	};
	setNote(m_targetCombo, choiceOfParts ? partNotes[part] : "");
	m_effectCombo.set_sensitive(live);
	m_targetCombo.set_sensitive(live);
	m_storeCheck.set_sensitive(live &&
		has(help::KeyboardFeatures::userstoredlighting));
	// Apply effect is dead on the Off row — see the action row for why the
	// flag alone will not hold it there. Both are set: this one is what
	// anything reading the button is told, and the cell is what keeps it
	// true after another file has written over it.
	m_applyEffectButton.set_sensitive(live && !turningOff);
	m_effectOffButton.set_sensitive(live);

	// Color applies to fixed color and breathing. On intensity-only
	// keyboards there is no color to pick — onApplyEffect sends the
	// Brightness slider value instead, the way the paint lane does, and
	// saying so beats a dead colour well.
	const bool rgb = has(help::KeyboardFeatures::rgb);
	const bool wantsColor = known && fx.usesColor;
	showRow(form, rowColor, wantsColor);
	m_effectColorButton.set_visible(wantsColor && rgb);
	m_effectColorButton.set_sensitive(live && rgb);
	if (Gtk::Label *caption = dynamic_cast<Gtk::Label*>(
	    form->get_child_at(0, rowColor)))
		caption->set_text_with_mnemonic(rgb ? "Co_lor" : "_Brightness");
	// The caption, the spoken name and the tooltip are three statements
	// about one control, and on a keyboard with no colours to pick they
	// all have to change together.
	if (Glib::RefPtr<Atk::Object> spoken = m_effectColorButton.get_accessible())
		spoken->set_name(rgb ? "Effect color" : "Effect brightness");
	m_effectColorButton.set_tooltip_text(rgb ?
		"The color this effect uses" : "How bright this effect is");
	const Gdk::RGBA rgba = m_effectColorButton.get_rgba();
	// The hex is in the field now, so the line beside it is free to be
	// what it should always have been: quiet, and only there when there is
	// something to say.
	setNote(m_effectColorButton, !rgb ?
		"the brightness set on the Colors tab" :
		isBlack(rgba) ? "black would leave the keyboard dark" : "");
	if (s_hexEntry) {
		s_hexEntry->set_visible(wantsColor && rgb);
		s_hexEntry->set_sensitive(live && rgb);
		// Never over the top of somebody typing in it.
		const std::string shown = "#" + rgbaToHex(rgba);
		if (!s_hexEntry->has_focus() && s_hexEntry->get_text().raw() != shown) {
			s_syncingHex = true;
			s_hexEntry->set_text(shown);
			s_syncingHex = false;
		}
	}

	// Period: breathing, the cycle and wave family, and ripple. Off takes
	// neither. Intensity-only models accept periods too — the CLI's
	// "g610-led -fx breathing keys 40 3000" works — so this must not
	// depend on rgb, or the control would be dead on hardware that
	// supports it.
	const bool wantsPeriod = known && fx.usesPeriod;
	showRow(form, rowPeriod, wantsPeriod);
	m_periodSpin.set_sensitive(live && wantsPeriod);
	setNote(m_periodSpin, secondsText(m_periodSpin.get_value(), fx.cycleWord));

	// Every caption now holds the word this keyboard makes it say, and the
	// window has been up long enough to have lent them its font.
	if (!s_captionsSized && m_effectCombo.get_realized()) {
		s_captionsSized = true;
		sizeCaptions(form);
	}

	// With Off chosen, Apply effect says the same word it always says and
	// is switched off, the description under the chooser says why, and Turn
	// effect off is the live one — the whole of what this tab can do in
	// that state. Neither button is ever relabelled; see the action row.
	if (s_applyCell)
		s_applyCell->set_sensitive(!turningOff);
	// A tooltip is also what a screen reader reads as the button's
	// description, and it is read whether the button can be pressed or
	// not — so the dead state has to account for itself here too.
	m_applyEffectButton.set_tooltip_text(turningOff ?
		"Nothing to apply: Off is the absence of an effect. Choose one in "
		"the Effect list, or press Turn effect off to stop the one the "
		"keyboard is running" :
		"Hand this effect to the keyboard and let it run there on its own");
	// What the keyboard is asked to remember is whatever the button beside
	// this box hands it, which on the Off row is no effect at all. Both
	// readings keep the box's own verb: a tooltip that said "clear" under a
	// label that says "remember" reads as the opposite of the label, even
	// where the two describe one act.
	m_storeCheck.set_tooltip_text(turningOff ?
		"Keep Off in the keyboard's own memory, so the effect it had "
		"remembered is forgotten" :
		"Keep this effect in the keyboard's own memory, so it survives "
		"unplugging it — recall it there with the backlight key and 7");

	// Nothing to preview on for a keyboard with no per-key board drawn,
	// and nothing to preview at all with the effect off: the board is
	// already showing what turning it off will leave.
	const bool canPreview = has(help::KeyboardFeatures::setkey) &&
		!m_keyboardWidget.getKeyPositions().empty();
	// A checkbox that can never be ticked is worse than no checkbox: the
	// keyboards without user-stored lighting simply have no such choice.
	const bool canStore = has(help::KeyboardFeatures::userstoredlighting);
	Gtk::CheckButton *previewCheck = dynamic_cast<Gtk::CheckButton*>(
		form->get_child_at(0, rowPreview));
	showRow(form, rowPreview, canPreview && !turningOff);
	if (previewCheck)
		previewCheck->set_sensitive(live);
	showRow(form, rowStore, canStore);

	// --- the preview itself
	if (s_previewTick.connected())
		s_previewTick.disconnect();
	s_animationSeen = animation != NULL;
	// `live` covers the two states with nothing to draw over: no keyboard,
	// and a software animation already drawing on the board.
	const bool wanted = canPreview && known && !turningOff &&
		s_panelOnScreen && live && previewCheck && previewCheck->get_active();
	if (!wanted && s_previewShowing) {
		s_previewShowing = false;
		if (!animation)
			clearPreviewState();
	}

	// The one line that says what the board is showing, while the board is
	// showing a picture this panel drew. It is not held when the keyboard
	// is already running the very effect being previewed: the strip names
	// that effect itself, and the two lines would be one fact said twice.
	const bool sameAsKeyboard = known && m_hasAppliedEffect &&
		m_deviceShowsPreview && nativeEffects[index] == m_appliedEffect;
	if (wanted && !sameAsKeyboard)
		ownCaption(m_stateBoardLabel, fx.name, boardStateText());
	else if (releaseCaption())
		updateBoardState();

	if (!s_panelOnScreen)
		return;

	// Motion is a preference, not a detail: a desktop with animations
	// turned off gets one still frame of the effect — a quarter of the
	// way in, so it is mid-breath or mid-sweep rather than at zero —
	// which still answers what the effect looks like.
	Glib::RefPtr<Gtk::Settings> settings = Gtk::Settings::get_default();
	const bool moving = wanted && fx.usesPeriod &&
		(!settings || settings->property_gtk_enable_animations().get_value());
	const LedKeyboard::Color color = rgb ?
		toLedColor(m_effectColorButton.get_rgba()) : currentColor();
	const double period = m_periodSpin.get_value() / 1000.0;
	// The keys a narrower part no longer covers are still wearing the last
	// frame drawn over them, and what they are supposed to show is the
	// draft underneath — so the board is put back before the narrower
	// frame goes on.
	if (wanted && s_previewShowing && part != s_previewPart)
		clearPreviewState();
	s_previewPart = part;
	sigc::slot<bool> tick = [this, index, part, color, period, moving]() -> bool {
		if (s_windowGone || !s_panelOnScreen)
			return false;
		// Nothing else tells this panel that a live effect started or
		// stopped, and both change every word and colour on it.
		if ((activeAnimationName() != NULL) != s_animationSeen) {
			onEffectChanged();   // which reconnects this timer, or drops it
			return false;
		}
		// A still preview is one frame; after that the timer is here only
		// to notice a live effect starting.
		if (!s_previewShowing || (!moving && s_stillDrawn))
			return true;
		LedKeyboard::KeyValueArray frame;
		renderEffectFrame(index, part,
			(g_get_monotonic_time() - s_previewStartedAt) / 1000000.0,
			period, color, m_keyboardWidget.getKeyPositions(), frame);
		if (!frame.empty())
			previewFrame(frame);
		s_stillDrawn = true;
		return true;
	};
	if (wanted) {
		s_previewStartedAt = g_get_monotonic_time() -
			(moving ? 0 : (gint64)(period * 250000.0));
		s_previewShowing = true;
		s_stillDrawn = false;
		tick();   // the first frame now, not in a sixteenth of a second
	}
	// Fifteen frames a second while it draws: the board's own coalescer
	// draws no faster than that, and a colour fade is indistinguishable
	// from one at twice the rate. Twice a second when it is only watching
	// for a live effect to end, which costs a cached daemon check.
	s_previewTick = Glib::signal_timeout().connect(tick, moving ? 66 : 500);
}

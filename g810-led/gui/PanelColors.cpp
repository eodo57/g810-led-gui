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

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

#include "../src/helpers/utils.h"
#include "MainWindowShared.h"
#include "Styling.h"

// The Colors tab — the colour you are painting with, the ways to lay it
// down, and the board interactions that stage into the draft.

namespace {

// How many colours the row under the swatch remembers. Past a handful
// "recent" stops meaning recent. Six and not eight because six is what
// fits on one line at the narrowest window this thing supports: the
// seventh wrapped the row and pushed the whole Paint card down the
// moment a person reached for one more colour.
const size_t kRecentColors = 6;

// State that belongs to this tab and to nothing else: the colours you
// have used, the widgets that show them, and the eyedropper's arm. It
// hangs off the window rather than sitting in MainWindow's header
// because no other panel ever reads it, and it is freed with the window.
struct ColorsPanel {
	std::vector<Gdk::RGBA> recent;
	Gtk::Box *recentLine = NULL;
	Gtk::FlowBox *recentRow = NULL;
	// The swatches inside that row, in the order they are shown, so the
	// arrow keys can walk them.
	std::vector<Gtk::Button*> recentButtons;
	Gtk::Entry *hexEntry = NULL;
	Gtk::ToggleButton *eyedropper = NULL;
	// Set up once by buildColorSection: every entry point on this tab
	// needs them and they reach into the window's own widgets, so they
	// are captured there instead of being written out five times.
	// No-ops until then, so nothing that runs early has to check.
	std::function<void(const Gdk::RGBA &)> adopt = [](const Gdk::RGBA&) {};
	std::function<void(const Gdk::RGBA &)> remember = [](const Gdk::RGBA&) {};
	std::function<void(LedKeyboard::Key)> takeFrom = [](LedKeyboard::Key) {};
	std::function<void(LedKeyboard::Key)> selectSameColor =
		[](LedKeyboard::Key) {};
	std::function<void(int, bool)> paintRegion = [](int, bool) {};
	// Work out how many key-group chips fit across the pane and lay them
	// out again if the answer has changed. Both an allocation and a rebuild
	// can make it change, and the two used to be wired up separately —
	// which left the chips laid out for a set of groups that was no longer
	// on screen until something else happened to resize the window.
	std::function<void()> refitGroups = []() {};
	// When the eyedropper last took a colour, so the second half of a
	// double-click on the board is not read as "and now open a chooser".
	gint64 tookAt = 0;
	bool syncing = false;        // writing the hex field, not reading it
	bool tookColor = false;      // the eyedropper went off because it worked
	bool stroking = false;       // a paint drag is in progress
	bool swallowStroke = false;  // …and it already gave up its colour
	bool restoreSelect = false;  // the eyedropper interrupted Select
	bool torn = false;           // the window's controls have started to go
	// The chooser was asked for by name, from the key's own menu, rather
	// than reached by double-clicking the board. The menu offers every
	// act whatever tool is in hand; the gesture follows the tool.
	bool chooserAsked = false;
	// How many keys the foot has last been told about. onSelectionChanged
	// also runs whenever the card is re-gated — a keyboard arriving, a tab
	// being built — and none of those is a selection, so the sentence is
	// spent only when this number really moves. It starts below zero
	// because the first pass happens before the window is on screen, and
	// there is nothing to report about a selection nobody has made yet.
	long spokenSelection = -1;
	// How the key-group block is laid out, and what one chip costs, so a
	// narrow window gets fewer columns instead of a block that runs off
	// the edge.
	int groupColumns = 4;
	int groupChipWidth = 0;
	bool groupRelayoutQueued = false;
};

const Glib::Quark colorsPanelQuark("g810led-colors-panel");

ColorsPanel &colorsPanel(Gtk::Window &window) {
	ColorsPanel *panel =
		static_cast<ColorsPanel*>(window.get_data(colorsPanelQuark));
	if (!panel) {
		panel = new ColorsPanel();
		window.set_data(colorsPanelQuark, panel,
			[](void *data) { delete static_cast<ColorsPanel*>(data); });
	}
	return *panel;
}

// True once the window has started being taken apart. Signals still fire
// while that happens — closing the window with the hex field focused
// hands it a focus-out — and by then the controls such a handler would
// reach for have already been destroyed.
bool goingAway(const ColorsPanel &panel, Gtk::Window &window) {
	return panel.torn || gtk_widget_in_destruction(GTK_WIDGET(window.gobj()));
}

// The window's own controls die before the widget tree they were built
// into, and unparenting a widget from that tree moves the focus — which
// hands the hex field a focus-out to answer with controls that are
// already gone. That is a crash on quit, and gtk_widget_in_destruction()
// cannot see it coming: the window itself is not being destroyed yet,
// only its members. So the panel watches the controls it leans on and
// stops the moment the first of them starts to go.
void stopWhenGone(Gtk::Widget &widget, ColorsPanel &panel) {
	g_signal_connect(widget.gobj(), "destroy",
		G_CALLBACK(+[](GtkWidget*, gpointer data) {
			static_cast<ColorsPanel*>(data)->torn = true;
		}), &panel);
}

// Whether the focus is somewhere inside a container, so that rebuilding
// the container can put it back.
bool isInside(Gtk::Widget *widget, Gtk::Widget &container) {
	for (Gtk::Widget *at = widget; at; at = at->get_parent())
		if (at == &container)
			return true;
	return false;
}

// A flow box wraps every child in a GtkFlowBoxChild, and each of those is
// a focus stop of its own — an unnamed one, standing in front of the
// control it holds. Nothing here is selectable, so they have no job:
// Tab skips them and lands on the button.
void addFlowChild(Gtk::FlowBox &box, Gtk::Widget &child) {
	box.add(child);
	if (Gtk::Widget *wrapper = child.get_parent())
		wrapper->set_can_focus(false);
}

// A row of peers is one stop on the way past, not eight. Tab reaches the
// swatch the focus last rested on and then leaves the row; the arrow keys
// move inside it. Eight remembered colours are worth one press of Tab on
// the way to the paint buttons, not eight.
void focusRecent(ColorsPanel &panel, size_t index) {
	if (index >= panel.recentButtons.size())
		return;
	// Grab first, then take the focus flag off the others: a widget that
	// loses it while it holds the focus hands the focus to the window.
	panel.recentButtons[index]->set_can_focus(true);
	panel.recentButtons[index]->grab_focus();
	for (size_t i = 0; i < panel.recentButtons.size(); ++i)
		if (i != index)
			panel.recentButtons[i]->set_can_focus(false);
	// GTK draws focus rings only while it believes someone is navigating
	// by keyboard, and it learns that from the Tab and arrow keys it
	// handles itself. This row answers its own arrow keys, so it has to
	// say so: without this the focus moves along the row invisibly.
	Gtk::Widget *top = panel.recentButtons[index]->get_toplevel();
	if (Gtk::Window *window = dynamic_cast<Gtk::Window*>(top))
		window->set_focus_visible(true);
}

// "#ff0000" — with the hash, because that is the form a person copies
// into anything else, and the same form the hex field takes back.
std::string hexLabel(const Gdk::RGBA &color) {
	return "#" + rgbaToHex(color);
}

std::string keyCount(size_t count) {
	return std::to_string(count) + (count == 1 ? " key" : " keys");
}

// What to say after painting a set of keys, when some of them may already
// have held that colour. The count in the header only ever rises by the
// number that changed, so the sentence here has to be about that number
// too — "Painted 16 keys" over "3 unsent changes" is the window telling
// two stories about one press.
std::string paintedSaid(size_t changed, size_t asked, const std::string &hex) {
	if (changed == 0)
		return (asked == 1 ? std::string("That key is") :
			keyCount(asked) + " are") + " already " + hex;
	if (changed == asked)
		return "Painted " + keyCount(asked) + " " + hex;
	return "Painted " + keyCount(changed) + " " + hex +
		" — the rest already were";
}


// The status bar is a sentence, so a key is named the way it is printed
// on the keycap and not the way the protocol spells it: "Caps Lock", not
// "caps_lock", and "G" rather than "g".
std::string keyPhrase(LedKeyboard::Key key) {
	const std::string name = utils::keyName(key);
	std::string words;
	bool startOfWord = true;
	for (size_t i = 0; i < name.size(); ++i) {
		if (name[i] == '_') {
			words += ' ';
			startOfWord = true;
			continue;
		}
		words += startOfWord ?
			(char)std::toupper((unsigned char)name[i]) : name[i];
		startOfWord = false;
	}
	return words;
}

// "key G", or "a key" for the handful the name table does not cover —
// either way something a sentence can be built around.
std::string namedKey(LedKeyboard::Key key) {
	const std::string phrase = keyPhrase(key);
	return phrase.empty() ? std::string("a key") : "key " + phrase;
}

// The same phrase where it opens the sentence instead of sitting inside
// one. Every other line in the foot of this window starts with a capital,
// and "key G is already #ff0000" was the one that did not.
std::string openingKey(LedKeyboard::Key key) {
	std::string phrase = namedKey(key);
	if (!phrase.empty())
		phrase[0] = (char)std::toupper((unsigned char)phrase[0]);
	return phrase;
}

// Accepts what a person would type: #rrggbb, rrggbb, #rgb, rgb, in any
// case and with stray spaces. Anything else is refused rather than
// half-read, so the field can say plainly what it wanted.
bool parseHexColor(const std::string &text, Gdk::RGBA &color) {
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

void roundedRect(const Cairo::RefPtr<Cairo::Context> &cr, double x, double y,
                 double width, double height, double radius) {
	radius = std::min(radius, std::min(width, height) / 2.0);
	cr->begin_new_sub_path();
	cr->arc(x + width - radius, y + radius, radius, -M_PI / 2, 0);
	cr->arc(x + width - radius, y + height - radius, radius, 0, M_PI / 2);
	cr->arc(x + radius, y + height - radius, radius, M_PI / 2, M_PI);
	cr->arc(x + radius, y + radius, radius, M_PI, 3 * M_PI / 2);
	cr->close_path();
}

// One remembered colour. Drawn rather than styled: the sheet paints every
// `button` node, so a per-widget provider would have to out-specify it —
// the board draws its keycaps for exactly the same reason. The hairline
// around the fill is what makes #000000 read as a colour and not a hole.
class SwatchButton : public Gtk::Button {
	public:
		explicit SwatchButton(const Gdk::RGBA &color) : m_color(color) {
			get_style_context()->add_class("kb-swatch");
			set_size_request(34, 26);
		}

	protected:
		bool on_draw(const Cairo::RefPtr<Cairo::Context> &cr) override {
			Gtk::Button::on_draw(cr);
			Glib::RefPtr<Gtk::StyleContext> context = get_style_context();
			const Gtk::StateFlags state = context->get_state();
			const Gtk::Border padding = context->get_padding(state);
			const Gtk::Border border = context->get_border(state);
			const double x = border.get_left() + padding.get_left();
			const double y = border.get_top() + padding.get_top();
			const double width = get_allocated_width() - x -
				border.get_right() - padding.get_right();
			const double height = get_allocated_height() - y -
				border.get_bottom() - padding.get_bottom();
			if (width <= 0.0 || height <= 0.0)
				return false;
			// A dead control has to look dead. is_sensitive(), not
			// get_sensitive(): what turns this one off is the whole row
			// going insensitive with the rest of the card, and a child
			// never hears about that in its own flag. The fill is laid
			// over the button's disabled background rather than replacing
			// it, so the colour is still recognisable — just plainly out
			// of reach.
			const bool alive = is_sensitive();
			roundedRect(cr, x, y, width, height, 4.0);
			cr->set_source_rgba(m_color.get_red(), m_color.get_green(),
			                    m_color.get_blue(), alive ? 1.0 : 0.35);
			cr->fill_preserve();
			cr->set_source_rgba(1.0, 1.0, 1.0, alive ? 0.30 : 0.14);
			cr->set_line_width(1.0);
			cr->stroke();
			return false;
		}

	private:
		Gdk::RGBA m_color;
};

// The words for a key group: what the chip says, and how a sentence in
// the status bar names the same set. The code's own names ("fkeys" next
// to "functions", meaning two different things) are not those words.
struct GroupWords { const char *code, *chip, *phrase, *what; };

const GroupWords kGroupWords[] = {
	{"logo", "Logo", "the logo", "the logo lights"},
	{"indicators", "Indicators", "the indicators",
	 "Caps Lock, Num Lock, Scroll Lock, Game mode and the backlight key"},
	{"gkeys", "G keys", "the G keys", "the G keys down the left side"},
	{"fkeys", "F1–F12", "the function row", "the function row, F1 to F12"},
	{"modifiers", "Modifiers", "the modifiers",
	 "Shift, Ctrl, Alt, the Windows keys and Menu"},
	{"multimedia", "Media", "the media keys",
	 "the media keys — play, stop, next, previous and mute"},
	{"arrows", "Arrows", "the arrow keys", "the four arrow keys"},
	{"numeric", "Numpad", "the number pad", "the number pad"},
	{"functions", "Navigation", "the navigation keys",
	 "Esc, Print Screen, Scroll Lock, Pause, Insert, Delete, Home, End "
	 "and Page Up / Down"},
	{"keys", "Typing keys", "the typing keys",
	 "the letters, numbers, punctuation, Tab, Enter, Space and Caps Lock"},
};

const GroupWords &groupWords(const char *code) {
	static const GroupWords fallback = {"", "", "these keys", "these keys"};
	for (size_t i = 0; i < sizeof(kGroupWords) / sizeof(kGroupWords[0]); ++i)
		if (std::string(kGroupWords[i].code) == code)
			return kGroupWords[i];
	return fallback;
}

}  // namespace

void MainWindow::buildColorSection(Gtk::Box* colorBox) {
	ColorsPanel &panel = colorsPanel(*this);

	Gtk::Grid *rgbColorBox = Gtk::manage(new Gtk::Grid());
	rgbColorBox->set_row_spacing(4);
	m_rgbColorBox = rgbColorBox;
	colorBox->pack_start(*m_rgbColorBox, false, false);

	// One small value, shown as one. This was a full-bleed bar — 1160px
	// of red at a wide window, the loudest thing on screen for something
	// that is one colour — and it read as a filled progress bar.
	//
	// The colour, the same colour written out, and the tool that lifts one
	// off the board are one cluster, packed at their own size from the left
	// edge of the card. This was a flow box, so that a squeezed window
	// could drop the tool onto its own line — but a flow box shares the row
	// out among its children instead of letting them keep their own width,
	// which at 1900px left 600 pixels of nothing between the hex and the
	// button. That is the same emptiness the bar had, only quieter.
	Gtk::Box *currentColorBox = Gtk::manage(
		new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 8));
	m_colorButton.set_use_alpha(false);
	m_colorButton.set_rgba(Gdk::RGBA("#ff0000"));
	m_colorButton.set_hexpand(false);
	m_colorButton.set_size_request(72, -1);
	m_colorButton.set_valign(Gtk::ALIGN_CENTER);
	m_colorButton.get_style_context()->add_class("kb-swatch");
	currentColorBox->pack_start(m_colorButton, false, false);

	// The hex is readable and typeable in the same place: someone with a
	// colour in hand should not have to find it in a colour wheel.
	Gtk::Entry *hexEntry = Gtk::manage(new Gtk::Entry());
	hexEntry->set_width_chars(8);
	hexEntry->set_max_width_chars(8);
	hexEntry->set_max_length(7);
	hexEntry->set_valign(Gtk::ALIGN_CENTER);
	hexEntry->get_style_context()->add_class("kb-numeric");
	hexEntry->set_tooltip_text(
		"The current color as hex — type one and press Enter");
	hexEntry->get_accessible()->set_name("Current color, hex value");
	panel.hexEntry = hexEntry;
	currentColorBox->pack_start(*hexEntry, false, false);

	Gtk::ToggleButton *eyedropper = Gtk::manage(new Gtk::ToggleButton());
	Gtk::Box *eyedropperContent = Gtk::manage(
		new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 6));
	if (Gtk::IconTheme::get_default()->has_icon("color-select-symbolic")) {
		Gtk::Image *icon = Gtk::manage(new Gtk::Image());
		icon->set_from_icon_name("color-select-symbolic", Gtk::ICON_SIZE_BUTTON);
		eyedropperContent->pack_start(*icon, false, false);
	}
	// "Take", not "pick": every colour button in this window opens a
	// chooser titled "Pick a color", and this is the other act — lifting a
	// colour that is already on the board. Two acts, two words, used the
	// same way in the menu, the tooltip and the status line.
	Gtk::Label *eyedropperLabel = Gtk::manage(
		new Gtk::Label("Take from _board", true));
	eyedropperLabel->set_mnemonic_widget(*eyedropper);
	// The one thing in the row that can give ground. A row that cannot be
	// squeezed sets the narrowest this tab can ever be, and past that the
	// whole tab slides sideways behind a scrollbar — the button does not
	// get smaller, it goes off the edge. Dragging the divider hard right
	// now shortens these three words instead, which is what the window
	// does everywhere else it runs out of room, and the tooltip and the
	// icon still say what the button is.
	eyedropperLabel->set_ellipsize(Pango::ELLIPSIZE_END);
	eyedropperContent->pack_start(*eyedropperLabel, false, false);
	eyedropper->add(*eyedropperContent);
	eyedropper->set_valign(Gtk::ALIGN_CENTER);
	// Ctrl-click reaches the board's paint handler, so it is only offered
	// where it is true: with the Select tool in hand a click belongs to
	// the selection, and this button is the way in.
	eyedropper->set_tooltip_text("Take the color of a key: press this, then "
		"click a key on the board. While painting, Ctrl-click a key does "
		"the same.");
	eyedropper->get_accessible()->set_name("Take a color from the board");
	panel.eyedropper = eyedropper;
	currentColorBox->pack_start(*eyedropper, false, false);
	m_rgbColorBox->attach(*currentColorBox, 0, 0, 1, 1);

	// Recognition, not recall: the colours you have used are one click
	// away instead of being something to remember and mix again.
	Gtk::Box *recentLine = Gtk::manage(
		new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 8));
	Gtk::Label *recentLabel = Gtk::manage(new Gtk::Label("Recent"));
	recentLabel->set_valign(Gtk::ALIGN_CENTER);
	recentLabel->get_style_context()->add_class("kb-hint");
	recentLine->pack_start(*recentLabel, false, false);
	panel.recentRow = Gtk::manage(new Gtk::FlowBox());
	panel.recentRow->get_accessible()->set_name("Recent colors");
	panel.recentRow->set_selection_mode(Gtk::SELECTION_NONE);
	panel.recentRow->set_homogeneous(true);
	panel.recentRow->set_min_children_per_line(1);
	panel.recentRow->set_max_children_per_line(kRecentColors);
	panel.recentRow->set_column_spacing(4);
	panel.recentRow->set_row_spacing(4);
	// The row keeps its height whether it holds one colour or eight, so
	// nothing below it moves as you work. It is packed at its own size
	// rather than filling the card: a colour you used is a small object,
	// and stretching eight of them across the panel would make each one
	// twice the size of the same colour at three.
	panel.recentRow->set_size_request(-1, 26);
	panel.recentRow->set_halign(Gtk::ALIGN_START);
	recentLine->pack_start(*panel.recentRow, false, false);
	m_rgbColorBox->attach(*recentLine, 0, 1, 1, 1);
	// Until there are two colours to choose between, the row holds only
	// the colour you already have — a stub offering nothing. It appears
	// the first time it has something to offer, which is also the moment
	// it explains itself. Shown by hand, so the window's show_all() does
	// not put the stub back.
	recentLine->show_all();
	recentLine->set_no_show_all(true);
	recentLine->hide();
	panel.recentLine = recentLine;

	panel.remember = [this](const Gdk::RGBA &color) {
		ColorsPanel &panel = colorsPanel(*this);
		const Gdk::RGBA solid = solidRGBA(color);
		if (!panel.recent.empty() && rgbaEqual(panel.recent.front(), solid))
			return;
		for (std::vector<Gdk::RGBA>::iterator it = panel.recent.begin();
		     it != panel.recent.end(); ++it) {
			if (rgbaEqual(*it, solid)) {
				panel.recent.erase(it);
				break;
			}
		}
		panel.recent.insert(panel.recent.begin(), solid);
		if (panel.recent.size() > kRecentColors)
			panel.recent.resize(kRecentColors);
		if (!panel.recentRow)
			return;
		// Rebuilding destroys the very button that was just pressed, and
		// with it the keyboard's place in the window. The colour that was
		// chosen is the one now at the front, so the focus lands there.
		const bool keepFocus = isInside(get_focus(), *panel.recentRow);
		for (Gtk::Widget *child : panel.recentRow->get_children())
			panel.recentRow->remove(*child);
		panel.recentButtons.clear();
		for (size_t i = 0; i < panel.recent.size(); ++i) {
			const Gdk::RGBA swatchColor = panel.recent[i];
			SwatchButton *swatch = Gtk::manage(new SwatchButton(swatchColor));
			const std::string hex = hexLabel(swatchColor);
			// Most recent first, which makes the front one the colour
			// already sitting in the swatch on the line above. It says so
			// rather than leaving the same colour in two places with
			// nothing to explain the repeat.
			//
			// The rest do what choosing the same colour in the chooser
			// does, so they say the same thing that does.
			swatch->set_tooltip_text(i == 0 ?
				hex + " is the color you are using now — press it to paint "
					"the selected keys, if any" :
				"Use " + hex + " again — paints the selected keys, if any");
			swatch->get_accessible()->set_name(i == 0 ?
				"Recent color " + hex + ", in use now" : "Recent color " + hex);
			// Only the first is a tab stop; the arrow keys reach the rest.
			swatch->set_can_focus(i == 0);
			swatch->signal_clicked().connect([this, swatchColor]() {
				colorsPanel(*this).adopt(swatchColor);
				// Same act as choosing it in the chooser, so it does the
				// same thing to a selection.
				onColorChosen();
			});
			const size_t which = i;
			swatch->signal_key_press_event().connect(
				[this, which](GdkEventKey *event) {
					ColorsPanel &panel = colorsPanel(*this);
					const size_t last = panel.recentButtons.empty() ? 0 :
						panel.recentButtons.size() - 1;
					size_t target = which;
					switch (event->keyval) {
						// Up and Down rather than nothing: the row wraps to a
						// second line in a narrow window, and either key then
						// means the neighbour in reading order.
						case GDK_KEY_Left: case GDK_KEY_Up:
						case GDK_KEY_KP_Left: case GDK_KEY_KP_Up:
							target = which > 0 ? which - 1 : 0;
							break;
						case GDK_KEY_Right: case GDK_KEY_Down:
						case GDK_KEY_KP_Right: case GDK_KEY_KP_Down:
							target = std::min(which + 1, last);
							break;
						case GDK_KEY_Home: case GDK_KEY_KP_Home:
							target = 0;
							break;
						case GDK_KEY_End: case GDK_KEY_KP_End:
							target = last;
							break;
						default:
							return false;
					}
					// Swallowed even at the ends of the row: an arrow key
					// that fell through to the flow box would throw the
					// focus out of the row it was aimed inside.
					focusRecent(panel, target);
					return true;
				});
			addFlowChild(*panel.recentRow, *swatch);
			panel.recentButtons.push_back(swatch);
		}
		panel.recentRow->show_all_children();
		panel.recentRow->set_sensitive(m_colorButton.get_sensitive());
		if (panel.recentLine)
			panel.recentLine->set_visible(panel.recent.size() > 1);
		if (keepFocus)
			focusRecent(panel, 0);
	};

	panel.adopt = [this](const Gdk::RGBA &color) {
		ColorsPanel &panel = colorsPanel(*this);
		const Gdk::RGBA solid = solidRGBA(color);
		m_colorButton.set_rgba(solid);
		const std::string hex = hexLabel(solid);
		m_colorButton.set_tooltip_text("Current color " + hex +
			" — click to pick another");
		m_colorButton.get_accessible()->set_name("Current color " + hex);
		if (panel.hexEntry) {
			panel.syncing = true;
			panel.hexEntry->set_text(hex);
			panel.syncing = false;
		}
		panel.remember(solid);
	};

	panel.takeFrom = [this](LedKeyboard::Key key) {
		ColorsPanel &panel = colorsPanel(*this);
		Gdk::RGBA color;
		const bool lit = m_keyboardWidget.getKeyColor(key, color);
		if (!lit)
			color = Gdk::RGBA("#000000");
		panel.adopt(color);
		panel.tookAt = g_get_monotonic_time();
		const std::string phrase = keyPhrase(key);
		status("Took " + hexLabel(color) + (lit ? "" : " (that key is off)") +
			" from " + (phrase.empty() ? std::string("the board") :
				"key " + phrase));
	};

	// Two ways in — a double-click on the board while Select is in hand,
	// and the key's own menu — so the act and the sentence it leaves
	// behind are written once.
	panel.selectSameColor = [this](LedKeyboard::Key key) {
		Gdk::RGBA color;
		if (!m_keyboardWidget.getKeyColor(key, color)) {
			status(openingKey(key) + " has no color of its own yet, "
				"so there is nothing to match");
			return;
		}
		m_keyboardWidget.selectSameColorAs(key);
		status("Selected " +
			keyCount(m_keyboardWidget.getSelectedKeys().size()) + " colored " +
			hexLabel(color));
	};

	// Committing the hex field: Enter takes it, and so does leaving the
	// field, which is what a person expects of something they just typed.
	// Anything unreadable is put back rather than half-applied.
	const sigc::slot<void> commitHex = [this]() {
		ColorsPanel &panel = colorsPanel(*this);
		if (panel.syncing || !panel.hexEntry || goingAway(panel, *this))
			return;
		Gdk::RGBA typed;
		const std::string text = panel.hexEntry->get_text().raw();
		if (parseHexColor(text, typed)) {
			if (!rgbaEqual(typed, currentRGBA())) {
				panel.adopt(typed);
				onColorChosen();
				if (m_keyboardWidget.getSelectedKeys().empty())
					status("Color is now " + hexLabel(typed));
			}
			return;
		}
		panel.syncing = true;
		panel.hexEntry->set_text(hexLabel(currentRGBA()));
		panel.syncing = false;
		status("\"" + text + "\" is not a color — type one like #ff8800");
	};
	hexEntry->signal_activate().connect(commitHex);
	hexEntry->signal_focus_out_event().connect(
		[commitHex](GdkEventFocus*) { commitHex(); return false; });
	// Both of the above answer with the window's own controls, and the
	// status bar is the first of those to be destroyed on the way out.
	stopWhenGone(m_statusbar, panel);
	stopWhenGone(m_colorButton, panel);

	eyedropper->signal_toggled().connect([this]() {
		ColorsPanel &panel = colorsPanel(*this);
		if (goingAway(panel, *this))
			return;
		if (panel.eyedropper->get_active()) {
			// Nothing else can be in hand while this is: a click has to
			// mean "take that colour", not "select that key".
			if (m_keyboardWidget.mode() == KeyboardWidget::SELECT) {
				panel.restoreSelect = true;
				m_paintModeButton.set_active(true);
			}
			panel.tookColor = false;  // it has taken nothing yet
			status("Click a key to take its color — Esc cancels");
			return;
		}
		// Putting the tool down puts back the tool it interrupted. Coming
		// out of this in Paint would leave the next click painting a key
		// the person meant to select — the mode changing under them,
		// because of something they did not ask for.
		if (panel.restoreSelect) {
			panel.restoreSelect = false;
			m_selectModeButton.set_active(true);
		}
		if (!panel.tookColor)
			status("Left the color as " + hexLabel(currentRGBA()));
		panel.tookColor = false;
	});

	// Escape is the way out of anything, so it is the way out of this
	// too — and only of this, while it is armed.
	signal_key_press_event().connect([this](GdkEventKey *event) {
		ColorsPanel &panel = colorsPanel(*this);
		if (event->keyval != GDK_KEY_Escape || !panel.eyedropper ||
		    !panel.eyedropper->get_active())
			return false;
		panel.eyedropper->set_active(false);
		return true;
	}, false);

	// Choosing Select puts the brush down, and the eyedropper with it.
	m_selectModeButton.signal_toggled().connect([this]() {
		ColorsPanel &panel = colorsPanel(*this);
		if (m_selectModeButton.get_active() && panel.eyedropper)
			panel.eyedropper->set_active(false);
	});

	// A drag hands out one colour, not one per key it crosses.
	m_keyboardWidget.signal_stroke_begin().connect([this]() {
		colorsPanel(*this).stroking = true;
	});
	m_keyboardWidget.signal_stroke_end().connect([this]() {
		ColorsPanel &panel = colorsPanel(*this);
		panel.stroking = false;
		panel.swallowStroke = false;
	});

	// setControlsEnabled() only knows about the colour button; everything
	// in this card is that same control by another name, so it follows.
	m_colorButton.property_sensitive().signal_changed().connect([this]() {
		ColorsPanel &panel = colorsPanel(*this);
		if (goingAway(panel, *this))
			return;
		const bool enabled = m_colorButton.get_sensitive();
		if (panel.hexEntry)
			panel.hexEntry->set_sensitive(enabled);
		if (panel.eyedropper) {
			if (!enabled)
				panel.eyedropper->set_active(false);
			panel.eyedropper->set_sensitive(enabled);
		}
		if (panel.recentRow)
			panel.recentRow->set_sensitive(enabled);
	});

	panel.adopt(m_colorButton.get_rgba());

	Gtk::Grid *intensityBox = Gtk::manage(new Gtk::Grid());
	intensityBox->set_row_spacing(4);
	m_intensityBox = intensityBox;
	colorBox->pack_start(*m_intensityBox, false, false);
	m_intensityScale.set_draw_value(true);
	m_intensityScale.set_digits(0);
	m_intensityScale.set_hexpand(true);
	m_intensityScale.get_accessible()->set_name("Brightness, 0 to 255");
	Gtk::Label *brightnessLabel = Gtk::manage(new Gtk::Label("Brightness"));
	brightnessLabel->set_xalign(0);
	brightnessLabel->set_hexpand(true);
	brightnessLabel->get_style_context()->add_class("kb-hint");
	m_intensityBox->attach(*brightnessLabel, 0, 0, 1, 1);
	m_intensityBox->attach(m_intensityScale, 0, 1, 1, 1);

	// A card with nothing in it is worse than no card. refreshFeatures()
	// shows exactly one of these two rows — colour for a per-key board,
	// brightness for a board that only dims — and neither when there is
	// no keyboard at all, which used to leave "Color" as an empty box
	// with a heading on it. The card follows its contents.
	if (Gtk::Frame *frame = dynamic_cast<Gtk::Frame*>(colorBox->get_parent())) {
		const sigc::slot<void> followContents = [this, frame]() {
			// Teardown hides every widget in the tree on its way out; by
			// then the card is not there to follow anything.
			if (goingAway(colorsPanel(*this), *this))
				return;
			frame->set_visible(
				(m_rgbColorBox && m_rgbColorBox->get_visible()) ||
				(m_intensityBox && m_intensityBox->get_visible()));
		};
		m_rgbColorBox->property_visible().signal_changed().connect(
			followContents);
		m_intensityBox->property_visible().signal_changed().connect(
			followContents);
	}
}

void MainWindow::buildRegionsSection(Gtk::Box* regionsBox) {
	if (!m_regionsFrame) m_regionsFrame = (Gtk::Frame*)regionsBox->get_parent();
	Gtk::Box *regionRow = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 4));
	regionRow->set_homogeneous(true);
	m_regionBoxes.clear();

	// On a region keyboard these five wells are the only way to set any
	// colour at all, so they have to work from the keyboard too: Enter
	// paints one, Delete turns it off. One copy of the act, and of the
	// sentence it leaves behind, for the key presses and the mouse both.
	ColorsPanel &panel = colorsPanel(*this);
	panel.paintRegion = [this](int region, bool off) {
		const Gdk::RGBA rgba = off ? Gdk::RGBA("#000000") : currentRGBA();
		stageRegion(region, rgba);
		if (off) {
			status("Turned region " + std::to_string(region) + " off");
			return;
		}
		colorsPanel(*this).remember(rgba);
		status("Painted region " + std::to_string(region) + " " +
			hexLabel(rgba));
	};

	for (int region = 1; region <= 5; region++) {
		Gtk::EventBox *box = Gtk::manage(new Gtk::EventBox());
		Gtk::Label *label = Gtk::manage(new Gtk::Label(std::to_string(region)));
		label->set_xalign(0.5);
		label->set_yalign(0.5);
		box->add(*label);
		// The mouse route is written under the row for everyone to see;
		// the keyboard route has nowhere else to be said, so it is said
		// here — both halves of it, or Delete stays a secret.
		box->set_tooltip_text("Region " + std::to_string(region) +
			" — click to paint it with the current color. With the focus on "
			"it, Enter paints and Delete turns it off.");
		box->get_accessible()->set_name("Region " + std::to_string(region));
		box->get_style_context()->add_class("kb-key");
		// A colour well the height of a line of text reads as a table
		// header. This is the one place the colour itself is the content.
		box->set_size_request(-1, 38);
		box->set_can_focus(true);
		m_regionStyles.push_back(Glib::RefPtr<Gtk::CssProvider>());
		styling::paint(*box, m_regionStyles.back(), Gdk::RGBA("#000000"));
		regionRow->pack_start(*box, true, true);
		box->signal_button_press_event().connect(sigc::bind(
			sigc::mem_fun(*this, &MainWindow::onRegionPress), region));
		// No guard on the device state here: an insensitive well is handed
		// no key presses at all, and the mouse path has none either — the
		// two ways in have to answer the same way.
		box->signal_key_press_event().connect(
			[this, region](GdkEventKey *event) {
				if (event->keyval == GDK_KEY_Return ||
				    event->keyval == GDK_KEY_KP_Enter ||
				    event->keyval == GDK_KEY_space) {
					colorsPanel(*this).paintRegion(region, false);
					return true;
				}
				if (event->keyval == GDK_KEY_Delete ||
				    event->keyval == GDK_KEY_BackSpace) {
					colorsPanel(*this).paintRegion(region, true);
					return true;
				}
				return false;
			});
		// The sheet draws focus on buttons and fields; this is neither, so
		// it draws its own — a well nobody can see the focus on is a well
		// nobody can use without a mouse.
		box->signal_draw().connect([box](
			const Cairo::RefPtr<Cairo::Context> &cr) {
			if (!box->has_visible_focus())
				return false;
			Gtk::Allocation allocation = box->get_allocation();
			box->get_style_context()->render_focus(cr, 2, 2,
				allocation.get_width() - 4, allocation.get_height() - 4);
			return false;
		}, true);
		m_regionBoxes.push_back(box);
	}
	regionsBox->pack_start(*regionRow, false, false);
	Gtk::Label *regionsInstr = Gtk::manage(new Gtk::Label(
		"Click: paint · Right-click: turn off · Double-click: pick a color"));
	regionsInstr->set_xalign(0);
	regionsInstr->get_style_context()->add_class("kb-hint");
	regionsBox->pack_start(*regionsInstr, false, false);
	if (m_regionsFrame) m_regionsFrame->set_visible(false);
}

void MainWindow::buildPaintSection(Gtk::Box* paintBox) {
	// Whole board, then a group, then whatever is selected: three scopes
	// of the same act, widest first, so the block reads as one ladder
	// rather than as a button and then some unrelated chips.
	m_paintAllButton.set_label("Paint all ke_ys");
	m_paintAllButton.set_use_underline(true);
	m_paintAllButton.set_tooltip_text(
		"Paint every key on the keyboard with the current color");
	paintBox->pack_start(m_paintAllButton, false, false);

	// The chips themselves are built by rebuildGroupChips(), which runs
	// again whenever the connected model changes which groups exist.
	// They live inside this flow box because setControlsEnabled() gates
	// them through it.
	m_groupChips.set_selection_mode(Gtk::SELECTION_NONE);
	m_groupChips.set_min_children_per_line(1);
	m_groupChips.set_max_children_per_line(1);
	m_groupChips.set_homogeneous(true);
	m_groupChips.set_hexpand(true);
	paintBox->pack_start(m_groupChips, false, false);

	// How many chips fit across is a question about the width the tab
	// actually has, which is only known once something is allocated — and
	// it has to be asked of the pane, not of anything inside it. A block
	// that is already too wide answers with its own width, and so does the
	// viewport it is in: a viewport inside a scroller is allocated
	// whatever its contents demand and scrolls the difference. Asking
	// either of them is asking the block that does not fit whether it
	// fits, which is how this block came to sit behind a horizontal
	// scrollbar at a 980-wide window and stay there.
	//
	// Asked on every allocation and again after every rebuild: the chips
	// are laid out for the keyboard that is connected, and the answer to
	// "how many fit" is about those chips. A rebuild that changed them
	// without asking again left the block arranged for a set of groups
	// that had gone.
	colorsPanel(*this).refitGroups = [this]() {
		ColorsPanel &panel = colorsPanel(*this);
		if (goingAway(panel, *this) || m_groupChipButtons.empty() ||
		    panel.groupRelayoutQueued)
			return;
		if (panel.groupChipWidth <= 0) {
			// What one chip costs. Asked here rather than as they are
			// built: a widget that is not on screen yet measures as
			// nothing at all.
			for (size_t i = 0; i < m_groupChipButtons.size(); ++i) {
				int minimum = 0, natural = 0;
				m_groupChipButtons[i]->get_preferred_width(minimum, natural);
				panel.groupChipWidth = std::max(panel.groupChipWidth, minimum);
			}
			if (panel.groupChipWidth <= 0)
				return;
		}
		// How far in from the page's edge the block starts — the card's
		// own margins, the same at every width. Read off the allocation
		// rather than asked of the viewport: translate_coordinates wants
		// both widgets realized, and this tab is not realized at all until
		// it is first looked at. On a machine with no keyboard the window
		// opens on the Device tab, so every attempt failed and the chips
		// stayed laid out for a width they never had.
		const Gtk::Allocation mine = m_groupChips.get_allocation();
		if (mine.get_x() < 0 || mine.get_width() < 2)
			return;                      // never laid out; nothing to go on
		int available = mine.get_width();
		for (Gtk::Widget *up = m_groupChips.get_parent(); up; up = up->get_parent())
			if (Gtk::ScrolledWindow *pane =
			    dynamic_cast<Gtk::ScrolledWindow*>(up)) {
				available = pane->get_allocated_width();
				// The bar down the side of a tab taller than the pane is
				// not room the chips can have.
				if (Gtk::Widget *bar = pane->get_vscrollbar())
					if (bar->get_visible())
						available -= bar->get_allocated_width();
				break;
			}
		available -= 2 * mine.get_x();
		int columns = (available + 4) / (panel.groupChipWidth + 4);
		columns = std::max(1, std::min(4, columns));
		if (columns == panel.groupColumns)
			return;
		panel.groupColumns = columns;
		// Never rebuild inside an allocation. Tied to the window, so a
		// queued relayout dies with it rather than running on nothing.
		panel.groupRelayoutQueued = true;
		Glib::signal_idle().connect_once(sigc::track_obj([this]() {
			colorsPanel(*this).groupRelayoutQueued = false;
			rebuildGroupChips();
		}, *this));
	};
	m_groupChips.signal_size_allocate().connect(
		[this](Gtk::Allocation&) { colorsPanel(*this).refitGroups(); });

	Gtk::Separator *rule = Gtk::manage(
		new Gtk::Separator(Gtk::ORIENTATION_HORIZONTAL));
	rule->set_margin_top(8);
	rule->set_margin_bottom(4);
	paintBox->pack_start(*rule, false, false);

	// What is selected, and the way to let go of it, on one line: undoing
	// a selection is not a colour change, so it does not belong in the
	// row with the two that are.
	Gtk::Box *selectionState = Gtk::manage(
		new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 4));
	m_selectionLabel.set_xalign(0);
	m_selectionLabel.set_line_wrap(true);
	m_selectionLabel.get_style_context()->add_class("kb-hint");
	selectionState->pack_start(m_selectionLabel, true, true);
	m_deselectButton.set_tooltip_text(
		"Let go of the selection — the colors stay as they are");
	selectionState->pack_start(m_deselectButton, false, false);
	paintBox->pack_start(*selectionState, false, false);

	// The two that do change colour, as equal peers that each say what
	// they will act on. They no longer look alike either: painting the
	// selection is the act this card exists for, and turning colour off
	// is the one that throws it away.
	// The middle rung: the accent as an outline. Filled belongs to Send
	// to keyboard alone — this button and that one were both #66ccff at
	// once on a painted board, which is two answers to "what do I press
	// next" on one screen. Painting comes first and sending after, and an
	// outline against a fill says which is which.
	m_paintSelectionButton.get_style_context()->add_class("kb-secondary");
	m_clearSelectionButton.get_style_context()->add_class("kb-danger");
	m_paintSelectionButton.set_tooltip_text(
		"Paint the selected keys with the current color");
	m_clearSelectionButton.set_tooltip_text("Turn the selected keys off");
	Gtk::Box *selectionRow = Gtk::manage(
		new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 4));
	selectionRow->set_homogeneous(true);
	selectionRow->pack_start(m_paintSelectionButton, true, true);
	selectionRow->pack_start(m_clearSelectionButton, true, true);
	paintBox->pack_start(*selectionRow, false, false);
	onSelectionChanged();
}

void MainWindow::stageRegion(int region, const Gdk::RGBA &color) {
	pushUndo();
	Gdk::RGBA solid = solidRGBA(color);
	m_regionDraft[region] = solid;
	if (region >= 1 && region <= (int)m_regionBoxes.size())
		paintRegionBox(region, solid);
	updatePendingState();
}

// --- Staging interactions ------------------------------------------------

void MainWindow::onKeyPressed(LedKeyboard::Key key) {
	ColorsPanel &panel = colorsPanel(*this);
	if (panel.swallowStroke)
		return;
	// The eyedropper, armed or held: this click takes a colour instead of
	// laying one down. A drag gives up exactly one, not one per key.
	GdkModifierType modifiers = (GdkModifierType)0;
	const bool ctrlHeld = gtk_get_current_event_state(&modifiers) &&
		(modifiers & GDK_CONTROL_MASK) != 0;
	const bool armed = panel.eyedropper && panel.eyedropper->get_active();
	if ((armed || ctrlHeld) && panel.takeFrom) {
		panel.takeFrom(key);
		if (armed) {
			panel.tookColor = true;
			panel.eyedropper->set_active(false);
		}
		if (panel.stroking)
			panel.swallowStroke = true;
		return;
	}
	const Gdk::RGBA rgba = currentRGBA();
	panel.remember(rgba);
	// A key that already holds this colour is not painted, and saying it
	// was would be the one sentence in the window nothing else agrees
	// with. What happened is that nothing did, which is worth a word of
	// its own: it is otherwise indistinguishable from a click that missed.
	if (!stageKey(key, rgba)) {
		status(openingKey(key) + " is already " + hexLabel(rgba));
		return;
	}
	status("Painted " + namedKey(key) + " " + hexLabel(rgba));
}

void MainWindow::onKeyPick(LedKeyboard::Key key) {
	ColorsPanel &panel = colorsPanel(*this);
	const bool askedForChooser = panel.chooserAsked;
	panel.chooserAsked = false;
	// While the eyedropper is in hand a double-click is still just a
	// pick: asking for a chooser here would be asking twice.
	if (panel.eyedropper && panel.eyedropper->get_active()) {
		panel.takeFrom(key);
		panel.tookColor = true;
		panel.eyedropper->set_active(false);
		return;
	}
	// The second half of that double-click lands here, after the tool has
	// been put down by the first half, so a pick that has only just
	// happened is the same act rather than a request for the chooser.
	int doubleClick = 400;
	if (Glib::RefPtr<Gtk::Settings> settings = Gtk::Settings::get_default())
		doubleClick = settings->property_gtk_double_click_time();
	if (panel.tookAt != 0 &&
	    g_get_monotonic_time() - panel.tookAt < (gint64)doubleClick * 1000) {
		panel.takeFrom(key);
		return;
	}
	// The Select tool promises that nothing is painted while it is in
	// hand, and a chooser that stages a key would break that promise. The
	// gesture does the select-mode thing instead: everything wearing this
	// colour, which is otherwise buried in the right-click menu.
	if (!askedForChooser && m_keyboardWidget.mode() == KeyboardWidget::SELECT) {
		panel.selectSameColor(key);
		return;
	}
	Gdk::RGBA chosen = m_keyboardWidget.getColorAtPress();
	if (!colorpicker::run(this, "Pick a color for " + namedKey(key), chosen))
		return;
	Gdk::RGBA picked = solidRGBA(chosen);
	if (has(help::KeyboardFeatures::intensity)) {
		double value = std::max(std::max(picked.get_red(), picked.get_green()),
		                        picked.get_blue());
		m_intensityScale.set_value(std::round(value * 255.0));
		picked = currentRGBA();
	} else {
		panel.adopt(picked);
	}
	if (!stageKey(key, picked)) {
		status(openingKey(key) + " is already " + hexLabel(picked));
		return;
	}
	status("Painted " + namedKey(key) + " " + hexLabel(picked));
}

// Secondary click: show what can be done to this key instead of making
// the user remember that right-click clears and double-click picks.
void MainWindow::onKeyMenu(LedKeyboard::Key key) {
	m_keyMenu.foreach([this](Gtk::Widget &child) { m_keyMenu.remove(child); });
	const std::string name = keyPhrase(key);

	// The key it is about, first: a menu that opens under the pointer
	// should say what it is aimed at before it says what it can do.
	if (!name.empty()) {
		Gtk::MenuItem *heading = Gtk::manage(new Gtk::MenuItem("Key " + name));
		heading->set_sensitive(false);
		m_keyMenu.append(*heading);
		m_keyMenu.append(*Gtk::manage(new Gtk::SeparatorMenuItem()));
	}

	// The shortcut in brackets is only offered when it is true: Ctrl-click
	// reaches the board's paint handler, which the Select tool does not
	// run, and a double-click there means "everything of this colour".
	const bool painting = m_keyboardWidget.mode() == KeyboardWidget::PAINT;
	// alive is false for an entry that is worth showing — so the menu keeps
	// one shape and one order from one right-click to the next — but that
	// has nothing to act on this time.
	struct Item { std::string label; std::function<void()> action; bool alive; };
	std::vector<Item> items;
	items.push_back({"Paint it " + hexLabel(currentRGBA()),
		[this, key]() {
			// Asked for by name, so the tool in hand does not get to turn
			// it into something else: with the eyedropper armed, the same
			// path would take this key's colour instead of laying one down.
			ColorsPanel &panel = colorsPanel(*this);
			if (panel.eyedropper)
				panel.eyedropper->set_active(false);
			onKeyPressed(key);
		}, true});
	items.push_back({painting ? "Take this color (Ctrl-click)" :
			std::string("Take this color"),
		[this, key]() { colorsPanel(*this).takeFrom(key); }, true});
	items.push_back({painting ? "Pick a color for it… (double-click)" :
			std::string("Pick a color for it…"),
		[this, key]() {
			colorsPanel(*this).chooserAsked = true;
			onKeyPick(key);
		}, true});
	items.push_back({"Turn it off",
		[this, key]() { onKeyCleared(key); }, true});
	items.push_back({std::string(),  // separator
		std::function<void()>(), true});
	items.push_back({"Select this row",
		[this, key]() {
			m_keyboardWidget.selectRowOf(key);
			status("Selected the row " + namedKey(key) + " is in — " +
				keyCount(m_keyboardWidget.getSelectedKeys().size()));
		}, true});
	items.push_back({painting ? "Select every key of this color" :
			std::string("Select every key of this color (double-click)"),
		[this, key]() { colorsPanel(*this).selectSameColor(key); }, true});
	// Offered whether or not there is a selection, and switched off when
	// there is not: an entry that is present but does nothing is the worse
	// of the two, and a menu that changes length between right-clicks
	// moves everything above this line under the pointer.
	items.push_back({"Clear the selection",
		[this]() { onDeselectSelection(); },
		!m_keyboardWidget.getSelectedKeys().empty()});

	for (size_t i = 0; i < items.size(); ++i) {
		if (items[i].label.empty()) {
			m_keyMenu.append(*Gtk::manage(new Gtk::SeparatorMenuItem()));
			continue;
		}
		Gtk::MenuItem *item = Gtk::manage(new Gtk::MenuItem(items[i].label));
		std::function<void()> action = items[i].action;
		item->signal_activate().connect([action]() { action(); });
		item->set_sensitive(items[i].alive);
		m_keyMenu.append(*item);
	}
	m_keyMenu.show_all();
	// Where the menu opens depends on what asked for it. A right-click
	// wants it under the pointer; the Menu key, pressed with the board
	// focused, has no pointer to speak of, and popping up wherever the
	// mouse was last left is how a keyboard user gets a menu somewhere
	// else on the screen — or off it.
	GdkEvent *cause = gtk_get_current_event();
	const bool fromKeys = cause && cause->type == GDK_KEY_PRESS;
	// gtk_get_current_event hands back a copy, not a borrow.
	if (cause)
		gdk_event_free(cause);
	if (fromKeys) {
		m_keyMenu.popup_at_widget(&m_keyboardWidget,
			Gdk::GRAVITY_CENTER, Gdk::GRAVITY_NORTH, NULL);
	} else {
		m_keyMenu.popup_at_pointer(NULL);
	}
}

void MainWindow::onKeyCleared(LedKeyboard::Key key) {
	if (!stageKey(key, Gdk::RGBA("#000000"))) {
		status(openingKey(key) + " is already off");
		return;
	}
	status("Turned " + namedKey(key) + " off");
}

// The arrow keys have landed on a key. Someone driving the board this way
// cannot see where the cursor went, and the ring on the cap is no use to
// them, so this line is their whole view of it: which key, what colour it
// is holding, and whether it is one of the chosen ones. Every other
// answer this window gives is in the same place, in the same voice.
void MainWindow::onCursorMoved(LedKeyboard::Key key) {
	const std::string phrase = keyPhrase(key);
	std::string said = "On " + (phrase.empty() ? std::string("a key") :
		"key " + phrase);
	Gdk::RGBA color;
	if (m_keyboardWidget.getKeyColor(key, color)) {
		const bool lit = color.get_red() > 0 || color.get_green() > 0 ||
			color.get_blue() > 0;
		said += lit ? " — " + hexLabel(color) : " — off";
	}
	const std::vector<LedKeyboard::Key> chosen =
		m_keyboardWidget.getSelectedKeys();
	if (std::find(chosen.begin(), chosen.end(), key) != chosen.end())
		said += ", chosen";
	status(said);
}

// Painting model: choosing a color from the picker immediately paints the
// currently selected keys; without a selection it only sets the active
// color for subsequent clicks.
void MainWindow::onColorChosen() {
	ColorsPanel &panel = colorsPanel(*this);
	const Gdk::RGBA rgba = solidRGBA(m_colorButton.get_rgba());
	panel.adopt(rgba);
	std::vector<LedKeyboard::Key> keys = m_keyboardWidget.getSelectedKeys();
	if (keys.empty()) {
		// It used to say nothing at all here, which left "did that do
		// anything?" as the only reading of a colour that changed
		// nothing on the board.
		status("Color is now " + hexLabel(rgba) +
			" — click keys on the board to paint them");
		return;
	}
	const size_t changed = stageKeys(keys, rgba);
	status(paintedSaid(changed, keys.size(), hexLabel(rgba)));
}

void MainWindow::onPaintAll() {
	Gdk::RGBA rgba = currentRGBA();
	stageAllKeys(rgba);
	colorsPanel(*this).remember(rgba);
	status("Painted all keys " + hexLabel(rgba));
}

void MainWindow::rebuildGroupChips() {
	ColorsPanel &panel = colorsPanel(*this);
	for (Gtk::Widget *child : m_groupChips.get_children())
		m_groupChips.remove(*child);
	m_groupChipButtons.clear();
	// What one chip costs is measured once and remembered, and this is the
	// one moment it can stop being true: a different keyboard offers a
	// different set of groups, and "Typing keys" is half as wide again as
	// "Arrows". Kept stale, the block worked out how many would fit from
	// the width of chips that were no longer there.
	panel.groupChipWidth = 0;
	if (m_groups.empty())
		return;

	// A grid whose every row runs the full width. The chips used to be a
	// flow of content-sized buttons, which left the block ragged and a
	// hole in the last row; here a row of three is three wider chips.
	// Twelve columns because it divides by one, two, three and four.
	Gtk::Grid *grid = Gtk::manage(new Gtk::Grid());
	grid->set_column_spacing(4);
	grid->set_row_spacing(4);
	grid->set_column_homogeneous(true);
	grid->set_hexpand(true);

	const size_t columns = std::min<size_t>(
		std::max(1, panel.groupColumns), m_groups.size());
	const size_t rows = (m_groups.size() + columns - 1) / columns;
	size_t index = 0;
	for (size_t row = 0; row < rows && index < m_groups.size(); ++row) {
		const size_t inRow = std::max<size_t>(1, std::min<size_t>(4,
			m_groups.size() / rows + (row < m_groups.size() % rows ? 1 : 0)));
		const int span = 12 / (int)inRow;
		for (size_t column = 0; column < inRow && index < m_groups.size();
		     ++column, ++index) {
			const GroupWords &words = groupWords(m_groups[index].label);
			Gtk::Button *chip = Gtk::manage(new Gtk::Button(
				*words.chip ? words.chip : m_groups[index].label));
			chip->set_tooltip_text(std::string("Paint ") + words.what +
				" with the current color — hover to see them on the board");
			chip->get_accessible()->set_name(std::string("Paint ") +
				words.phrase);
			chip->set_can_focus(true);
			const size_t which = index;
			chip->signal_clicked().connect(
				[this, which]() { onPaintGroupIndex(which); });
			// Hover shows the group on the board: recognition instead of
			// recalling which keys "navigation" or "modifiers" covers.
			chip->signal_enter_notify_event().connect(
				[this, which](GdkEventCrossing*) {
					previewGroup(which, true); return false; });
			chip->signal_leave_notify_event().connect(
				[this, which](GdkEventCrossing*) {
					previewGroup(which, false); return false; });
			// The card these sit in switches its nine buttons off
			// together when there is no keyboard to send to. Eight of
			// them carry the flag themselves; these were switched off
			// only by way of the box around them, so anything that asked
			// a chip directly — a screen reader's own probe, a test —
			// was told it was live while it was drawn dead and did
			// nothing. Same rule, said the same way.
			chip->set_sensitive(m_groupChips.get_sensitive());
			grid->attach(*chip, (int)column * span, (int)row, span, 1);
			m_groupChipButtons.push_back(chip);
		}
	}
	addFlowChild(m_groupChips, *grid);
	m_groupChips.show_all_children();
	// These chips are not the ones the last fit was worked out for, so it
	// is worked out again — on an idle, because the block has to be laid
	// out before anything can be measured against it.
	Glib::signal_idle().connect_once(sigc::track_obj(
		[this]() { colorsPanel(*this).refitGroups(); }, *this));
}

void MainWindow::previewGroup(size_t index, bool on) {
	// Never fight an animation for the preview layer.
	if (index >= m_groups.size() || rainActive() || !m_controlsEnabled)
		return;
	if (!on) {
		m_keyboardWidget.clearPreview();
		return;
	}
	const Gdk::RGBA rgba = currentRGBA();
	for (LedKeyboard::Key key : LedKeyboard::keysForGroup(m_groups[index].group))
		m_keyboardWidget.setPreviewColor(key, rgba);
}

void MainWindow::onPaintGroupIndex(size_t index) {
	if (index >= m_groups.size())
		return;
	Gdk::RGBA rgba = currentRGBA();
	m_keyboardWidget.clearPreview();
	const std::vector<LedKeyboard::Key> keys =
		LedKeyboard::keysForGroup(m_groups[index].group);
	const size_t changed = stageKeys(keys, rgba);
	colorsPanel(*this).remember(rgba);
	std::string group(groupWords(m_groups[index].label).phrase);
	if (changed == 0) {
		// It opens the sentence here rather than sitting inside one, and
		// every other line in the foot of this window starts with a
		// capital.
		if (!group.empty())
			group[0] = (char)std::toupper((unsigned char)group[0]);
		status(group + " is already " + hexLabel(rgba));
		return;
	}
	status("Painted " + group + " " + hexLabel(rgba));
}

void MainWindow::onSelectionChanged() {
	ColorsPanel &panel = colorsPanel(*this);
	const size_t count = m_keyboardWidget.getSelectedKeys().size();
	const bool any = count > 0;
	const std::string what = keyCount(count);
	// The buttons name what they will act on, so the count is part of the
	// act rather than a number sitting next to three anonymous verbs.
	m_selectionLabel.set_text(any ?
		what + " selected — press Esc to let go" :
		"Nothing selected — drag across the board, or Shift-click keys");
	// Choosing keys is an act, and until now the only place that noticed
	// was this line — which is on the Colors tab, and so invisible to
	// anyone dragging a band across the board with Live effects open. The
	// foot was left holding whatever sentence the previous act had put
	// there, so the window's one always-visible answer went stale exactly
	// when something had just happened.
	//
	// It is the single announcer for both halves of it. The acts that
	// change the selection and then say something better about it —
	// selecting a row, selecting every key of one colour — run after this
	// and write over it, which is the right way round.
	if (panel.spokenSelection >= 0 && (size_t)panel.spokenSelection != count)
		status(any ? "Selected " + what :
			"Selection cleared — the colors are unchanged");
	panel.spokenSelection = (long)count;
	// With nothing to act on the pair say the least they can: the line
	// above is where "nothing selected" belongs, and two long labels
	// would set the width of a row that has nothing to do.
	m_paintSelectionButton.set_label(any ? "Pa_int " + what : "Pa_int");
	// Not "o_ff": the menu bar already answers Alt-F, and a letter that
	// two controls claim reaches neither of them on the first press.
	m_clearSelectionButton.set_label(any ? "_Turn " + what + " off" :
		"_Turn off");
	// The same gate the board itself is behind. These two stage colour,
	// so with no keyboard open — or one that cannot colour single keys —
	// they are as dead as the board is, and a selection that outlived a
	// disconnect does not bring them back to life. Letting go of that
	// selection is not staging anything, so Deselect is always there.
	const bool canPaint = m_controlsEnabled && has(help::KeyboardFeatures::setkey);
	m_paintSelectionButton.set_sensitive(any && canPaint);
	m_clearSelectionButton.set_sensitive(any && canPaint);
	m_deselectButton.set_sensitive(any);
}

void MainWindow::onPaintSelection() {
	std::vector<LedKeyboard::Key> keys = m_keyboardWidget.getSelectedKeys();
	if (keys.empty()) {
		status("Nothing is selected yet — drag across the board to choose keys");
		return;
	}
	Gdk::RGBA rgba = currentRGBA();
	const size_t changed = stageKeys(keys, rgba);
	colorsPanel(*this).remember(rgba);
	status(paintedSaid(changed, keys.size(), hexLabel(rgba)));
}

void MainWindow::onClearSelectionColor() {
	std::vector<LedKeyboard::Key> keys = m_keyboardWidget.getSelectedKeys();
	if (keys.empty()) {
		status("Nothing is selected yet — drag across the board to choose keys");
		return;
	}
	const size_t changed = stageKeys(keys, Gdk::RGBA("#000000"));
	status(changed == 0 ?
		(keys.size() == 1 ? std::string("That key is") :
			keyCount(keys.size()) + " are") + " already off" :
		"Turned " + keyCount(changed) + " off");
}

void MainWindow::onDeselectSelection() {
	// onSelectionChanged() reports it, whichever of the four ways in here
	// let go of the selection — this button, the menu, Escape, or the
	// board handing it back itself. One event, one sentence, one writer.
	m_keyboardWidget.clearSelection();
}

bool MainWindow::onRegionPress(GdkEventButton *event, int region) {
	ColorsPanel &panel = colorsPanel(*this);
	if (event->type == GDK_BUTTON_PRESS && event->button == 3) {
		panel.paintRegion(region, true);
		return true;
	}
	if (event->type == GDK_2BUTTON_PRESS && event->button == 1) {
		Gdk::RGBA initial = currentRGBA();
		if (m_regionAtPress == region)
			initial = m_regionColorAtPress;
		if (colorpicker::run(this, "Pick a color for region " +
		    std::to_string(region), initial)) {
			stageRegion(region, initial);
			colorsPanel(*this).adopt(initial);
			status("Painted region " + std::to_string(region) + " " +
				hexLabel(initial));
		}
		return true;
	}
	if (event->type == GDK_BUTTON_PRESS && event->button == 1) {
		// What the region wore before this click, so that the chooser a
		// double-click opens starts from it rather than from the colour
		// the first half of that double-click has already laid down.
		std::map<int, Gdk::RGBA>::const_iterator found = m_regionDraft.find(region);
		m_regionColorAtPress = found != m_regionDraft.end() ?
			found->second : Gdk::RGBA("#000000");
		m_regionAtPress = region;
		panel.paintRegion(region, false);
		return true;
	}
	return false;
}

void MainWindow::paintRegionBox(int region, const Gdk::RGBA &color) {
	if (region < 1 || region > (int)m_regionBoxes.size())
		return;
	styling::paint(*m_regionBoxes[region - 1], m_regionStyles[region - 1], color);
}

void MainWindow::setRegionPending(int region, bool pending) {
	if (region >= 1 && region <= (int)m_regionBoxes.size()) {
		Gtk::EventBox *box = m_regionBoxes[region - 1];
		if (pending)
			box->get_style_context()->add_class("kb-pending");
		else
			box->get_style_context()->remove_class("kb-pending");
	}
}

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

#include "MainWindowShared.h"

// The strip above the tabs, the notice bar under it and the line along
// the foot. Between them they are everything the window says about
// itself, which is why the words are decided here.
//
// One act, one name. The two buttons in the Profile section read "Send to
// keyboard" and "Discard changes", so everywhere in this window:
//
//     send      writing the colours on screen to the hardware
//     unsent    a colour on screen the keyboard has not been given
//     discard   throwing unsent changes away
//
// "Apply", "staged", "pending" and "draft" are those same three ideas
// under other names, and a person who meets two of them cannot tell
// whether they are looking at one thing or three.
//
// The division of labour is as strict as the vocabulary, because two
// places allowed to state the same fact will eventually state it
// differently:
//
//     the strip   permanently: which keyboard, what it is showing, and
//                 whether anything is unsent. Nothing else. With no
//                 keyboard it answers all three anyway — none, nothing,
//                 and here is how to get one.
//     the bar     the newest problem nobody has acknowledged, with what
//                 to do about it and, where there is one, the button that
//                 does it. Anything older and still unacknowledged is one
//                 click down. It stays until it is closed.
//     the foot    what just happened, and only that. It expires: an
//                 event is not a state, and a sentence that was true a
//                 minute ago has no business arguing with a strip that
//                 is true now. It also never repeats the strip. The two
//                 are at opposite ends of the window, so a repeat does
//                 not even read as emphasis — it reads as a second
//                 source, and a second source is a thing that can go
//                 stale on its own.

static const char *effectName(LedKeyboard::NativeEffect effect) {
	switch (effect) {
		case LedKeyboard::NativeEffect::color: return "fixed color";
		case LedKeyboard::NativeEffect::breathing: return "breathing";
		case LedKeyboard::NativeEffect::cycle: return "color cycle";
		case LedKeyboard::NativeEffect::waves: return "waves";
		case LedKeyboard::NativeEffect::hwave: return "horizontal wave";
		case LedKeyboard::NativeEffect::vwave: return "vertical wave";
		case LedKeyboard::NativeEffect::cwave: return "center wave";
		case LedKeyboard::NativeEffect::ripple: return "ripple";
		default: return "off";
	}
}

namespace {

// Long enough to read a full sentence twice, short enough that nobody
// comes back from lunch and reads it as the state of things.
const int kStatusLifetimeMs = 12000;
// The notice bar's own response id for the button that fixes the problem.
const int kFixResponse = 1;

// Where the notice bar keeps that button and the line that says what to
// do, so this file can show and hide them without members in a header it
// does not own.
const Glib::Quark &fixButtonKey() {
	static const Glib::Quark key("g810-led-notice-fix");
	return key;
}

const Glib::Quark &remedyLabelKey() {
	static const Glib::Quark key("g810-led-notice-remedy");
	return key;
}

bool endsWith(const std::string &text, const std::string &tail) {
	return text.size() >= tail.size() &&
	       text.compare(text.size() - tail.size(), tail.size(), tail) == 0;
}

// How a phrase is recognised at the door: word for word, or by its
// opening — keeping what follows, or taking the sentence with it.
enum Rewrite { kExact, kHead, kAll };

// A handful of panels still phrase their lines in the vocabulary this
// window has left behind, and one of them — "Staged key (ff0000) — press
// Apply to send" — is the sentence that used to sit at the foot
// contradicting the strip an inch above it. Every message in the window
// comes through status(), so this is the one door where they can be made
// to agree, and one door is the only way an agreement holds.
//
// Only the lane whose button says "Send to keyboard" is rewritten. The
// on-board effects have their own button, which says "Apply effect", so
// applying an effect stays applying an effect: one act, one name is not
// the same rule as one name for every act.
//
// A phrase that is not listed passes through untouched, which is also
// what happens the day a panel is edited to say the right thing itself:
// the entry stops matching and nothing else changes. These belong at
// their call sites; they are here because this file does not own them.
std::string sameWords(const std::string &message) {
	static const struct { const char *from; const char *to; Rewrite how; } map[] = {
		{ "Staged ",          "Painted ",              kHead  },
		{ "Applied ",         "Sent ",                 kHead  },
		{ "Applied",          "Sent to the keyboard",  kExact },
		// Pressed Send and there was nothing to send. Repeating the
		// strip's own "Nothing to send" back at the user is the window
		// answering a question with the label on the wall; this answers
		// it, in the words the Send button uses when it is dead.
		{ "Nothing to apply",
		  "The keyboard already has these colors",    kExact },
		{ "Draft reverted to the last applied state",
		  "Discarded — the colors on the keyboard are back", kExact },
		// The colour lane's own failures, which name the act they failed
		// at and so have to name it the way the button does.
		{ "Failed to apply — ",
		  "The keyboard did not take the colors",      kAll   },
		{ "Failed to apply region ", "Could not send region ", kHead },
		{ "Profile applied",
		  "Profile loaded and sent to the keyboard",   kExact },
		{ "Profile loaded — apply failed",
		  "Profile loaded, but the colors could not be sent", kAll },
		// Pasted profile text takes the same lane as a profile file and
		// keeps whatever ran off the back of it ("— raindrop running").
		{ "Profile text applied",
		  "Profile text sent to the keyboard",         kHead  },
		// Two hints that describe painting. Both used "stage", which is
		// the third name this window used to have for one act.
		{ "Paint keys to stage changes",
		  "Paint keys, then send them to the keyboard", kAll   },
		{ "Paint: click or drag across keys to stage",
		  "Paint: click or drag across keys to color them", kAll },
	};
	std::string said = message;
	for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); ++i) {
		const std::string from = map[i].from;
		if (map[i].how == kExact) {
			if (said == from) { said = map[i].to; break; }
		} else if (said.compare(0, from.size(), from) == 0) {
			said = map[i].how == kAll ? std::string(map[i].to) :
				std::string(map[i].to) + said.substr(from.size());
			break;
		}
	}
	// "Stop the wave before applying colors" is built from the animation's
	// name and the act it blocks, so the act arrives here as a tail rather
	// than an opening — and it is the send lane under its old name again.
	static const std::string applying = " before applying colors";
	if (endsWith(said, applying))
		said = said.substr(0, said.size() - applying.size()) +
			" before sending colors";
	// Counted messages are built by concatenation and come out saying
	// "1 keys". Nobody notices writing it; everybody notices reading it.
	if (endsWith(said, " 1 keys"))
		said.erase(said.size() - 1);
	return said;
}

// "Sound reactive running on Line In — press Stop to end" is an event
// with an instruction stapled to it. The event belongs at the foot; the
// instruction belongs on the button that carries it out and in the strip,
// where neither can go stale. Only a trailing clause is taken — a tail
// that runs on into whole sentences is somebody's paragraph, not an
// aside, and is left alone.
//
// A tail is an instruction when it opens by naming the control to work
// next. That is a narrow test on purpose. "Effect off — your colors are
// back" and "Color cycle — nothing is sent until you press Apply effect"
// both mention a control and neither is an instruction: the first is the
// outcome the user just got and the second is the whole point of the
// message. Only the tail that says "now go and press this" is dropped,
// because the button it names is on screen saying it better.
std::string withoutInstruction(const std::string &message) {
	static const char *instructions[] = {
		"press ", "then press ",
		// "<effect> is running on the keyboard — Turn effect off restores
		// your colors", where the button, the note above it and the strip
		// all say the same thing already.
		"Turn effect off ",
	};
	static const std::string dash = " — ";
	const size_t at = message.rfind(dash);
	if (at == std::string::npos)
		return message;
	const std::string tail = message.substr(at + dash.size());
	if (tail.find('.') != std::string::npos)
		return message;
	for (size_t i = 0; i < sizeof(instructions) / sizeof(instructions[0]); ++i) {
		const std::string opening = instructions[i];
		if (tail.compare(0, opening.size(), opening) == 0)
			return message.substr(0, at);
	}
	return message;
}

// What to do about a failure, and whether the one repair this window can
// carry out — reopening the keyboard — is the right one to offer with it.
// A message that says only that something failed leaves the user holding
// a problem and no next move, which is the difference between a report
// and an error a person can act on.
//
// What went wrong arrives as a sentence, so the sentence is what is read:
// a write the keyboard refused and a profile line that would not parse
// look the same from here otherwise. Anything unrecognised gets no advice
// rather than invented advice, because a plausible next move that does
// not work costs more than an admission that there is none.
struct Remedy { const char *say; bool rescan; };

// Advice for the failures that are not the keyboard's: a file that would
// not open, a file that would not be written, a profile with lines this
// build could not read. None of them has a button this window could put
// on the bar, but all three leave a person wondering what they are now
// supposed to do, and for all three the answer is short and true.
Remedy remedyForFile(const std::string &said) {
	static const struct { const char *opening; const char *say; } advice[] = {
		{ "Could not open ",
		  "Check that the file is still there and that you are allowed to "
		  "read it." },
		{ "Could not write ",
		  "Check that the folder still exists and that you are allowed to "
		  "write to it, or save somewhere else." },
		{ "Failed to write ",
		  "The disk may be full, or the file may be read-only. Try saving "
		  "somewhere else." },
		// The parser keeps every line it understood, so the important
		// thing here is not the failure — it is that the load happened
		// anyway, which the headline's "loaded with 2 problems" is easy
		// to read as the opposite of.
		{ "Profile loaded with ",
		  "The lines it could not read were skipped. The rest of the "
		  "profile was loaded." },
		{ "Text loaded with ",
		  "The lines it could not read were skipped. The rest of the text "
		  "was loaded." },
	};
	for (size_t i = 0; i < sizeof(advice) / sizeof(advice[0]); ++i) {
		const std::string opening = advice[i].opening;
		if (said.compare(0, opening.size(), opening) == 0) {
			const Remedy found = { advice[i].say, false };
			return found;
		}
	}
	static const Remedy none = { NULL, false };
	return none;
}

Remedy remedyFor(const std::string &said, bool deviceOpen) {
	// Everything any lane says when the keyboard refuses a write. These
	// are the ones the window can act on itself; the rest get words, or
	// nothing.
	static const char *refused[] = {
		"The keyboard did not take",
		"The keyboard would not",
		"Could not send",
		"Failed to apply effect",
		"Failed to turn the effect off",
	};
	for (size_t i = 0; i < sizeof(refused) / sizeof(refused[0]); ++i) {
		const std::string marker = refused[i];
		if (said.compare(0, marker.size(), marker) != 0)
			continue;
		// The handle it refused on is usually stale, and reopening it is
		// both the first thing to try and something this window can do
		// itself, so the bar carries the rescan either way. With nothing
		// open at all it carries only the button: the strip is already
		// saying, permanently, to connect a keyboard and rescan, and a
		// paragraph repeating it under the button that does it is the
		// same advice a third time.
		static const Remedy gone = { NULL, true };
		static const Remedy again = { "Rescan to open it again, or unplug "
			"the keyboard and plug it back in.", true };
		return deviceOpen ? again : gone;
	}
	return remedyForFile(said);
}

// The strip's two lines ellipsize rather than push the window wider, and
// both can be long: a product name is whatever the vendor wrote in the
// USB descriptor, and an audio source can be called "Monitor of AD104
// High Definition Audio Controller Digital Stereo (HDMI 2)". A line that
// has been cut short is a line the window is keeping from the reader, so
// it carries itself in a tooltip — and only then, because a tooltip
// repeating what is already fully on screen is just something in the way.
//
// Whether it was cut is asked of the layout, which is the only thing that
// knows: a line that has been given room to wrap is narrower than it
// wants and has lost nothing, and comparing widths cannot tell that apart
// from a line that has been cut.
void carryFullText(Gtk::Label &label) {
	label.signal_size_allocate().connect([&label](Gtk::Allocation&) {
		const bool cut = label.get_layout() &&
			pango_layout_is_ellipsized(label.get_layout()->gobj());
		// Whether one is wanted, and what it would say. Both are checked
		// before anything is set: this runs on every allocation, and a
		// property written on each of them is a property being notified
		// on each of them.
		if (!cut) {
			if (label.get_has_tooltip())
				label.set_has_tooltip(false);
			return;
		}
		const std::string full = label.get_text().raw();
		if (!label.get_has_tooltip() || label.get_tooltip_text().raw() != full)
			label.set_tooltip_text(full);
	});
}

// The second line of the strip is two different kinds of sentence, and
// they want opposite treatment. With a keyboard it reports what the LEDs
// are doing, and its length is the vendor's or the sound server's doing —
// one line, cut short, with the rest in a tooltip. With no keyboard it is
// the only instruction on screen, and a first-time user reading "Connect
// a supported keyboard — this window…" has been shown the half of it that
// says nothing. Then it wraps. Two lines at most either way, so the strip
// cannot grow without bound on a sentence somebody else wrote.
void sayFully(Gtk::Label &label, bool wraps) {
	if (label.get_line_wrap() == wraps)
		return;                       // this runs on every state change
	label.set_line_wrap(wraps);
	label.set_line_wrap_mode(Pango::WRAP_WORD_CHAR);
	label.set_ellipsize(Pango::ELLIPSIZE_END);
	label.set_lines(wraps ? 2 : 1);
}

// A line on the notice bar. It has two jobs beyond reading well, and both
// are about the size the window is allowed to be:
//
// It must fit any width it is given. A wrapping label's smallest width is
// its longest unbreakable run, and half of what this bar ever says is a
// file path — one long word to Pango. A horizontal box refused its own
// minimum does not shrink, it overflows, and what goes over the edge is
// the last thing packed: the close control, which is how a person is rid
// of the notice.
//
// And it must not make the window taller. A window's smallest height is
// worked out at its smallest width, where a label free to wrap is a
// paragraph — which is why a two-sentence notice used to add 163px to the
// window's minimum and stop a 620-tall window being 620 tall. Wrapping
// and ellipsizing together bound it: the text still breaks wherever it
// has to, and at a width where even that will not do it stops after
// `lines` instead of growing. At every width this window can actually be,
// nothing here reaches that limit.
void barLine(Gtk::Label &label, int lines) {
	label.set_xalign(0);
	label.set_line_wrap(true);
	label.set_line_wrap_mode(Pango::WRAP_WORD_CHAR);
	// set_lines needs both of these to bite.
	label.set_ellipsize(Pango::ELLIPSIZE_END);
	label.set_lines(lines);
	// And on the day something is cut short after all, it is still
	// readable — asked of the layout rather than guessed from the width,
	// because a label that wraps is narrower than it wants on purpose and
	// the width alone cannot tell the two apart.
	label.signal_size_allocate().connect([&label](Gtk::Allocation&) {
		const bool cut = label.get_layout() &&
			pango_layout_is_ellipsized(label.get_layout()->gobj());
		if (!cut) {
			if (label.get_has_tooltip())
				label.set_has_tooltip(false);
			return;
		}
		const std::string full = label.get_text().raw();
		if (!label.get_has_tooltip() || label.get_tooltip_text().raw() != full)
			label.set_tooltip_text(full);
	});
}

// Give the notice bar's own close control a name, in the tree and on the
// pointer both. Whatever buttons are in the bar at the moment this is
// called are given it; call it before adding any.
void nameCloseButton(Gtk::Widget &widget, const char *name) {
	if (Gtk::Button *button = dynamic_cast<Gtk::Button*>(&widget)) {
		button->set_tooltip_text(name);
		if (Glib::RefPtr<Atk::Object> spokenFor = button->get_accessible())
			spokenFor->set_name(name);
		return;
	}
	if (Gtk::Container *container = dynamic_cast<Gtk::Container*>(&widget))
		for (Gtk::Widget *child : container->get_children())
			if (child)
				nameCloseButton(*child, name);
}

// Where a notice about the whole window belongs: across the whole window,
// directly above the two columns. The bar used to sit inside the right
// column with the strip, which is a quarter of the window wide, and the
// arithmetic of that was brutal — the same two sentences that take two
// lines at 980px take four at 452, so the bar stood 136px tall on a
// 620-tall window and 232 with its details open, would not let the window
// be 620 tall at all, and took every one of those pixels out of the one
// column that had none to spare. None of it was the message's fault. It
// was being read through a slot a quarter of the width of the thing it
// was about.
//
// Found by walking rather than by being handed the box, because the shell
// belongs to the window's own builder and this file does not own that
// file. If the shape is ever not what is expected the caller's column is
// still there to fall back on, which is where this used to live and is
// merely cramped rather than broken.
Gtk::Box *shellAbove(Gtk::Widget &column, int &position) {
	Gtk::Widget *below = &column;
	for (Gtk::Widget *at = column.get_parent(); at; at = at->get_parent()) {
		Gtk::Box *box = dynamic_cast<Gtk::Box*>(at);
		if (box && dynamic_cast<Gtk::Window*>(box->get_parent())) {
			// Immediately above whatever holds the columns, whether that is
			// the second child of the shell or the fifth.
			const std::vector<Gtk::Widget*> children = box->get_children();
			position = (int)children.size();
			for (size_t i = 0; i < children.size(); ++i)
				if (children[i] == below)
					position = (int)i;
			return box;
		}
		below = at;
	}
	return NULL;
}

}  // namespace

// Everything above the tabs that never changes place: which keyboard,
// what it is showing, what has not been sent, and any problem that still
// needs the user's attention.
void MainWindow::buildStateStrip(Gtk::Box* rightCol) {
	Gtk::Box *strip = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_VERTICAL, 2));
	strip->set_margin_top(4);
	strip->set_margin_start(8);
	strip->set_margin_end(8);

	Gtk::Box *top = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 6));
	m_stateDeviceLabel.set_xalign(0);
	m_stateDeviceLabel.set_ellipsize(Pango::ELLIPSIZE_END);
	m_stateDeviceLabel.set_hexpand(true);
	m_stateDeviceLabel.get_style_context()->add_class("kb-heading");
	carryFullText(m_stateDeviceLabel);
	top->pack_start(m_stateDeviceLabel, true, true);
	// Shown only while a blocking device or sound-server query runs. A
	// spinner with nothing saying what it is waiting for is a control
	// with no label, so it carries its own.
	m_stateSpinner.set_no_show_all(true);
	m_stateSpinner.set_tooltip_text("Talking to the keyboard…");
	if (Glib::RefPtr<Atk::Object> atk = m_stateSpinner.get_accessible())
		atk->set_name("Talking to the keyboard");
	top->pack_start(m_stateSpinner, false, false);
	m_pendingLabel.set_xalign(1);
	top->pack_start(m_pendingLabel, false, false);
	strip->pack_start(*top, false, false);

	m_stateBoardLabel.set_xalign(0);
	m_stateBoardLabel.set_ellipsize(Pango::ELLIPSIZE_END);
	carryFullText(m_stateBoardLabel);
	strip->pack_start(m_stateBoardLabel, false, false);
	rightCol->pack_start(*strip, false, false);

	// A failure deserves more than a line at the foot that the next
	// message scrolls away, so it gets a bar that stays until it is
	// dismissed — and, where the remedy is known, the remedy on it.
	m_errorBar = Gtk::manage(new Gtk::InfoBar());
	m_errorBar->set_message_type(Gtk::MESSAGE_ERROR);
	m_errorBar->set_show_close_button(true);
	// GTK draws that control as a bare icon and gives it no name at all, so
	// the one thing on the bar that gets rid of the bar reached a screen
	// reader as "button". gtkmm hands out no pointer to it, so it is found
	// the only way there is — it is the one button the bar has before this
	// file adds any of its own — and named before Rescan joins it.
	nameCloseButton(*m_errorBar, "Dismiss this notice");
	// Two lines apiece for what happened and what to do about it: at every
	// width this window can be, both of ours are one, and two is the room
	// to be wrong about that in either direction.
	barLine(m_errorLabel, 2);
	// get_content_area() is typed as Gtk::Widget* in gtkmm 3.
	if (Gtk::Box *content = dynamic_cast<Gtk::Box*>(m_errorBar->get_content_area())) {
		Gtk::Box *stack = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_VERTICAL, 2));
		stack->pack_start(m_errorLabel, false, false);
		// What to do about it, under what happened, where it cannot be
		// missed and cannot be mistaken for part of the failure itself.
		// It lives on the bar rather than in a member because the header
		// this file writes into belongs to somebody else.
		Gtk::Label *remedy = Gtk::manage(new Gtk::Label());
		barLine(*remedy, 2);
		remedy->set_no_show_all(true);
		stack->pack_start(*remedy, false, false);
		m_errorBar->set_data(remedyLabelKey(), remedy);
		// Several bad lines in one profile belong in one report, and so
		// does an earlier problem that has not been acknowledged. This one
		// is a list and not a sentence, so it is neither cut short nor
		// counted in lines: it is behind a fold nobody opens by accident,
		// and a report with the interesting line ellipsized away is not a
		// report. It still breaks mid-path rather than overflowing.
		m_errorDetailsLabel.set_xalign(0);
		m_errorDetailsLabel.set_selectable(true);
		m_errorDetailsLabel.set_line_wrap(true);
		m_errorDetailsLabel.set_line_wrap_mode(Pango::WRAP_WORD_CHAR);
		m_errorDetails.set_label("Details");
		m_errorDetails.add(m_errorDetailsLabel);
		m_errorDetails.set_no_show_all(true);
		m_errorDetailsLabel.show();
		stack->pack_start(m_errorDetails, false, false);
		stack->show_all();
		content->pack_start(*stack, true, true);
	}
	// The one repair this window can carry out on the user's behalf, in
	// the bar's action area beside the close control — where the thing to
	// press about a notice belongs, and where the eye that has just
	// finished reading the sentence is already headed.
	//
	// It sat under the message for a while instead, because in the right
	// column the action area's 200-odd unwrappable pixels were the
	// difference between a bar that fitted a 980-wide window and one that
	// pushed the column past the edge. Across the whole window there is no
	// such arithmetic: 200px out of 980 is room the bar has, and the button
	// stops costing the message a line of its own.
	Gtk::Button *fix = Gtk::manage(new Gtk::Button("_Rescan for keyboards", true));
	fix->set_no_show_all(true);
	// Added as the bar's own response, so that dismissing the notice and
	// doing the repair stay one piece of code.
	m_errorBar->add_action_widget(*fix, kFixResponse);
	m_errorBar->set_data(fixButtonKey(), fix);
	m_errorBar->signal_response().connect([this](int response) {
		m_errorSticky = false;
		m_errorBar->hide();
		m_errorDetailsLabel.set_text("");
		m_errorDetails.hide();
		if (Gtk::Widget *remedy = static_cast<Gtk::Widget*>(
		    m_errorBar->get_data(remedyLabelKey())))
			remedy->hide();
		// Cleared along with everything else it belonged to, so the next
		// failure cannot inherit a repair that was never its own.
		if (Gtk::Widget *fix = static_cast<Gtk::Widget*>(
		    m_errorBar->get_data(fixButtonKey())))
			fix->hide();
		if (response == kFixResponse)
			rescanDevices();
	});
	m_errorBar->set_no_show_all(true);
	m_errorLabel.show();
	m_errorBar->get_content_area()->show();
	int above = 0;
	if (Gtk::Box *shell = shellAbove(*rightCol, above)) {
		shell->pack_start(*m_errorBar, false, false);
		shell->reorder_child(*m_errorBar, above);
	} else {
		rightCol->pack_start(*m_errorBar, false, false);
	}
}

// What the keyboard is showing, in one sentence — and when there is no
// keyboard, what to do about that, because the line is still there and a
// person looking at it still deserves an answer. The strip and the tray
// menu both say it, and two of them working it out separately would drift
// apart within a release. sensor reports a live microphone or screen
// capture, which callers present with more emphasis.
//
// The sentence is never allowed to claim more than is true, which is why
// the loaded profile's name is not in it. m_currentProfileName is the
// file this session started from; the keyboard is showing that file only
// until the first edit, and after a send it is showing that file plus
// whatever was sent. A name that is right at launch and quietly wrong
// afterwards is worse than no name, and which profile is open is a fact
// about the document, not about the LEDs.
std::string MainWindow::boardStateText(bool *sensorOut) const {
	std::string board;
	bool sensor = false;
	const char *animation = activeAnimationName();
	if (!m_deviceIsOpen) {
		// Nothing is showing anything, so the line spends itself on the
		// one thing worth knowing instead. This branch is the reason the
		// function exists in this shape: the strip used to write this
		// sentence itself and return early, leaving boardStateText() —
		// and therefore the tray icon — still answering "Showing your
		// colors" about a keyboard that had been unplugged.
		board = "Connect a supported keyboard, then press Rescan on the "
			"Device tab";
	} else if (animation) {
		board = std::string("Showing ") + animation +
			", driven by this computer";
		if (std::string(animation) == "sound reactive") {
			std::string source = m_audioSourceCombo.get_active_text();
			sensor = !selectedSourceIsMonitor();
			if (sensor)
				board = "Showing sound reactive · recording from microphone" +
					(source.empty() ? std::string() : " \"" + source + "\"");
			else if (!source.empty())
				board = "Showing sound reactive · " + source;
		} else if (std::string(animation) == "screen colors") {
			// Reading the display is not quiet context either, so it
			// gets the same undimmed treatment as a live microphone.
			sensor = true;
			if (!m_screenPlayer.isRunning()) {
				board = "Showing screen colors · a background process is "
					"reading the screen";
			} else {
				const std::string source = m_screenPlayer.sourceName();
				board = m_screenPlayer.waiting() ?
					"Showing screen colors · waiting for permission to capture" :
					"Showing screen colors · capturing " +
						(source.empty() ? std::string("the screen") : source);
			}
		}
	} else if (m_deviceShowsPreview && m_hasAppliedEffect &&
	           m_appliedEffect != LedKeyboard::NativeEffect::off) {
		board = std::string("Showing the on-board ") + effectName(m_appliedEffect);
	} else if (m_deviceShowsPreview) {
		// An effect or an animation had the LEDs and did not hand them
		// back. "A preview" said nothing about what is on the desk.
		board = "Showing what the last effect left behind";
	} else if (m_pendingCount > 0) {
		board = "Showing the colors you last sent";
	} else {
		board = "Showing your colors";
	}
	if (sensorOut)
		*sensorOut = sensor;
	return board;
}

// The one function that reads the draft and writes every conclusion drawn
// from it: the ring on each unsent key, the count in the strip, the
// sentence beside it, what the dead Send button says about itself, the
// tray, and whether Send and Discard are alive.
//
// The count and the sentence used to be worked out separately and could
// disagree — "No pending changes" over a board full of unsent colour. They
// are the same fact seen twice, so they are counted once: this pass writes
// m_pendingCount, and boardStateText() reads it rather than counting again.
// Nothing else in the window assigns it, which is what makes the agreement
// structural instead of a thing to remember.
void MainWindow::updateBoardState() {
	static const Gdk::RGBA black("#000000");
	size_t unsent = 0;
	// Only count per-key colours where Send can actually deliver them: on
	// g213/g413 the draft grid is hidden and applyDraft() takes the
	// region/whole-board lane, so counting them would leave a total that
	// never reaches zero.
	if (has(help::KeyboardFeatures::setkey))
	for (std::map<LedKeyboard::Key, Gdk::RGBA>::const_iterator it =
	     m_keyboardWidget.getKeyColors().begin();
	     it != m_keyboardWidget.getKeyColors().end(); ++it) {
		Gdk::RGBA applied = black;
		std::map<LedKeyboard::Key, Gdk::RGBA>::const_iterator found =
			m_appliedColors.find(it->first);
		if (found != m_appliedColors.end())
			applied = found->second;
		bool isUnsent = !rgbaEqual(it->second, applied);
		if (isUnsent)
			unsent++;
		m_keyboardWidget.markPending(it->first, isUnsent);
	}
	// Same rule for regions: a profile with "r" lines stages region
	// colours on any model, but only setregion keyboards can send them.
	if (has(help::KeyboardFeatures::setregion))
	for (std::map<int, Gdk::RGBA>::const_iterator it = m_regionDraft.begin();
	     it != m_regionDraft.end(); ++it) {
		Gdk::RGBA applied = black;
		std::map<int, Gdk::RGBA>::const_iterator found =
			m_regionApplied.find(it->first);
		if (found != m_regionApplied.end())
			applied = found->second;
		bool isUnsent = !rgbaEqual(it->second, applied);
		if (isUnsent)
			unsent++;
		if (it->first >= 1 && it->first <= (int)m_regionBoxes.size())
			setRegionPending(it->first, isUnsent);
	}
	if (!has(help::KeyboardFeatures::setkey) && has(help::KeyboardFeatures::setall) &&
	    !rgbaEqual(m_allKeysDraft, m_allKeysApplied))
		unsent++;
	m_pendingCount = unsent;

	// With an effect or an animation on the LEDs there is no delta to
	// send, but Send is exactly how the user gets their scheme back, so
	// it stays usable.
	const char *animation = activeAnimationName();
	const bool canSend = (unsent > 0 || m_deviceShowsPreview) &&
		!rainActive() && m_kbd.isOpen();
	m_applyKeyboardButton.set_sensitive(canSend);
	m_revertButton.set_sensitive(unsent > 0 && !rainActive());
	// A dead button is a question — "why can't I?" — and the answer is
	// known here, where it was decided. It is not in the strip: the strip
	// says what is true of the keyboard, not what is true of a control.
	// Written only when it changes, because this runs once per key of a
	// paint stroke.
	std::string why;
	if (!m_deviceIsOpen || !m_kbd.isOpen())
		why = "No keyboard is connected to send to";
	else if (animation)
		why = std::string("Stop ") + animation +
			" first — it is driving the keyboard now";
	else if (canSend)
		why = "Send the colors on this board to the keyboard (Ctrl+Enter)";
	else
		why = "The keyboard already has these colors";
	if (m_applyKeyboardButton.get_tooltip_text().raw() != why)
		m_applyKeyboardButton.set_tooltip_text(why);

	// Something other than the colors on this board has the LEDs: an
	// on-board effect the firmware is running, or an animation this
	// computer is driving. The two differ only in who is driving, and in
	// both the scheme comes back when it ends, so both say the same thing
	// here. They used to say opposite things — "Your colors are unsent"
	// for the effect and "Nothing to send" for the animation — because
	// this line was reading canSend, which is a fact about the Send
	// button rather than about the keyboard.
	const bool somethingElseHasTheLeds = animation != NULL || m_deviceShowsPreview;

	// Work waiting to be sent is the reason this window is open, so it is
	// stated in the window's own voice; everything else here is quiet. The
	// weight means one thing and one thing only — you have edits the
	// keyboard has not been given — and the wording says it too, for
	// anyone who cannot see the difference between them. It is worked out
	// before the keyboard is: unplugging one does not un-paint the board,
	// and a count that vanished would leave rings on keys nothing
	// accounted for.
	std::string count;
	if (unsent > 0)
		count = std::to_string(unsent) +
			(unsent == 1 ? " unsent change" : " unsent changes");
	else if (!m_deviceIsOpen)
		count = "";             // nothing waiting, and nothing to wait for
	else if (somethingElseHasTheLeds)
		// Nothing has been edited and the scheme is on screen only. The
		// line under this one says what took the LEDs, so this one stays
		// short — the keyboard's name is the more important thing on the
		// row, and a sentence here takes width the name is ellipsized
		// out of.
		//
		// It used to read "Your colors are unsent", which put a second,
		// opposite meaning on the one word this slot exists to carry:
		// "3 unsent changes" means you have edits waiting, and this state
		// is exactly the one where you have none. Read as a count it also
		// sent people to a Send button that is dead while an animation is
		// driving the keyboard. It says what is true of the keyboard
		// instead, and leaves counting to the counting branch.
		count = "Your colors are not on the keyboard";
	else
		count = "Nothing to send";
	if (unsent > 0) {
		m_pendingLabel.get_style_context()->remove_class("kb-hint");
		m_pendingLabel.set_markup("<b>" +
			Glib::Markup::escape_text(count) + "</b>");
	} else {
		m_pendingLabel.get_style_context()->add_class("kb-hint");
		m_pendingLabel.set_text(count);
	}

	// One writer for the name and one for the line beneath it, in every
	// state there is. Two branches that each set the same label are two
	// chances to set it differently.
	std::string product = m_deviceIsOpen ?
		m_kbd.getCurrentDevice().product : std::string("No keyboard connected");
	if (product.empty()) {
		// A keyboard set up by hand need not answer with a name, and the
		// strip still has to say which one this is.
		char ids[16];
		std::snprintf(ids, sizeof(ids), "%04x:%04x", m_openVendorID,
			m_openProductID);
		product = std::string("Keyboard ") + ids;
	}
	m_stateDeviceLabel.set_text(product);

	bool sensor = false;
	const std::string board = boardStateText(&sensor);
	// With nothing connected this line stops being a report and becomes
	// the one thing the window is asking the user to do, so it is allowed
	// the room to say it.
	sayFully(m_stateBoardLabel, !m_deviceIsOpen);
	// Everything else in this strip is quiet context; a live sensor — a
	// microphone, or the screen — is not, so it keeps its emphasis.
	if (sensor) {
		m_stateBoardLabel.get_style_context()->remove_class("kb-hint");
		m_stateBoardLabel.set_markup("<b>" +
			Glib::Markup::escape_text(board) + "</b>");
	} else {
		m_stateBoardLabel.get_style_context()->add_class("kb-hint");
		m_stateBoardLabel.set_text(board);
	}
	updateTray();
}

void MainWindow::statusError(const std::string &message,
                             const std::vector<std::string> &details,
                             bool /*sticky*/) {
	// Every failure is treated as one the user has to acknowledge. The
	// argument is ignored on purpose: a problem that an ordinary status
	// line could scroll away is precisely the one that gets missed, and
	// the bar costs a click to be rid of.
	if (!m_errorBar) {
		status(message);
		return;
	}
	// The bar is the whole account of this failure, so the foot must not
	// be left holding a cheerful sentence about the step before it.
	status("");

	const std::string said = sameWords(message);

	// A second failure does not erase the first. The newest is the
	// headline; anything still unacknowledged moves one click down,
	// keeping its own detail lines with it. A headline carries a bullet
	// so that counting them counts the problems.
	static const std::string bullet = "• ";
	std::string report;
	for (size_t i = 0; i < details.size(); ++i) {
		// A profile with bad lines arrives as a headline that quotes the
		// first of them and a list that contains it again, so the bar
		// showed "line 4: unknown key" twice, once as the problem and
		// once as its own detail. A detail the headline has already given
		// is not a detail. When they are all in the headline there is
		// nothing left to expand, which is the right answer too.
		if (!details[i].empty() && said.find(details[i]) != std::string::npos)
			continue;
		report += "    " + details[i] + "\n";
	}
	if (m_errorSticky && m_errorBar->get_visible()) {
		report += bullet + m_errorLabel.get_text().raw() + "\n";
		const std::string carried = m_errorDetailsLabel.get_text().raw();
		if (!carried.empty())
			report += carried + "\n";
	}
	if (!report.empty())
		report.erase(report.size() - 1);        // the last newline
	size_t earlier = 0;
	for (size_t at = 0; at < report.size(); at = report.find('\n', at) + 1) {
		if (report.compare(at, bullet.size(), bullet) == 0)
			earlier++;
		if (report.find('\n', at) == std::string::npos)
			break;
	}

	m_errorSticky = true;
	// What happened carries the weight; what to do about it follows in the
	// ordinary voice. Both are the bar's own colour, so the difference is
	// in the type: the sheet paints every label in here alike, and it is
	// not this file's to change.
	m_errorLabel.set_markup("<b>" + Glib::Markup::escape_text(said) + "</b>");
	// What to do about it goes under it, and stays out of the account
	// carried down when the next failure arrives: advice describes the
	// problem on top, and would be answering for the wrong one there.
	const Remedy remedy = remedyFor(said, m_deviceIsOpen);
	if (Gtk::Label *advice = static_cast<Gtk::Label*>(
	    m_errorBar->get_data(remedyLabelKey()))) {
		if (remedy.say) {
			advice->set_text(remedy.say);
			advice->show();
		} else {
			advice->hide();
		}
	}
	if (report.empty()) {
		m_errorDetailsLabel.set_text("");
		m_errorDetails.hide();
	} else {
		m_errorDetailsLabel.set_text(report);
		m_errorDetails.set_label(earlier == 0 ? std::string("Details") :
			earlier == 1 ? std::string("Details · 1 earlier problem") :
			"Details · " + std::to_string(earlier) + " earlier problems");
		m_errorDetails.set_expanded(false);
		m_errorDetails.show();
	}
	// Offer the repair only when it is the repair. That is usually the
	// same moment the advice above names it, and once it is not: with no
	// keyboard open the button is there on its own, because the strip is
	// already carrying the words.
	if (Gtk::Button *fix =
	    static_cast<Gtk::Button*>(m_errorBar->get_data(fixButtonKey()))) {
		if (remedy.rescan)
			fix->show();
		else
			fix->hide();
	}
	m_errorBar->show();
}

// Bridge until the blocking queries move off the GTK thread: show the
// spinner and let it paint before the call that will freeze the window.
void MainWindow::setBusy(bool busy) {
	if (busy) {
		m_stateSpinner.show();
		m_stateSpinner.start();
		for (int guard = 0; guard < 20 && Gtk::Main::events_pending(); ++guard)
			Gtk::Main::iteration(false);
	} else {
		m_stateSpinner.stop();
		m_stateSpinner.hide();
	}
}

// The foot reports one thing: what just happened. It does not restate the
// strip, it does not instruct, and it does not outlive the moment it
// describes — a status line that stays forever becomes a claim about the
// present, and then it starts contradicting the strip that really is one.
void MainWindow::status(const std::string &message) {
	Gtk::Statusbar* sb = m_statusPtr ? m_statusPtr : &m_statusbar;
	std::string spoken = withoutInstruction(sameWords(message));
	// "Connected to G512 RGB MECHANICAL GAMING KEYBOARD" under a strip
	// already headed "G512 RGB MECHANICAL GAMING KEYBOARD" spends a line
	// of the window on a word the eye has just read. The name is dropped
	// and the event — "Connected" — is kept, because the event is the
	// answer to what the user did and the name is not.
	const std::string shown = m_stateDeviceLabel.get_text().raw();
	if (!shown.empty() && spoken.size() > shown.size() &&
	    endsWith(spoken, shown)) {
		std::string without = spoken.substr(0, spoken.size() - shown.size());
		while (!without.empty() && without[without.size() - 1] == ' ')
			without.erase(without.size() - 1);
		if (endsWith(without, " to"))
			without.erase(without.size() - 3);
		if (!without.empty())
			spoken = without;
	}
	// What is left over after that can still be, word for word, a line the
	// strip is holding up permanently — "No keyboard connected" is what the
	// strip says the moment the keyboard goes, and it is also what the
	// colour lane says when you ask it to send with nothing to send to.
	// The act that produced it has already moved the strip; a second copy
	// of the same sentence at the other end of the window is not an
	// answer, it is an echo.
	if (spoken == shown || spoken == m_stateBoardLabel.get_text().raw())
		spoken.clear();
	sb->pop(m_statusContextId);
	const guint id = sb->push(spoken, m_statusContextId);
	if (spoken.empty())
		return;
	// Each message clears itself by its own id, so there is no timer to
	// cancel and no way for one message's timer to cut the next one
	// short. sigc's mem_fun on the bar drops the callback if the window
	// goes first. The arguments are remove_message(message, context).
	Glib::signal_timeout().connect_once(
		sigc::bind(sigc::mem_fun(*sb, &Gtk::Statusbar::remove_message),
			id, m_statusContextId), kStatusLifetimeMs);
}

void MainWindow::updateHistoryUI() {
	m_undoButton.set_sensitive(!m_undoStack.empty());
	m_redoButton.set_sensitive(!m_redoStack.empty());
}

void MainWindow::updatePendingState() {
	// The count, the rings, the wording and the buttons are one fact.
	updateBoardState();
}

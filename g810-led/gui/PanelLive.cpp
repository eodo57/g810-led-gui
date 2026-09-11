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
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fcntl.h>
#include <fstream>
#include <signal.h>
#include <unistd.h>

#include <glib/gstdio.h>

#include "MainWindowShared.h"

// The Live effects tab: the four host-driven lanes (raindrop, wave,
// sound reactive, screen colors), the frame path that feeds the board
// and the on-screen preview, and the daemons that keep a lane running
// after the window is gone.

// Live-effect chooser row to settings page. The first two rows are the
// raindrop modes, which share one page.
static const char *livePageFor(int mode) {
	switch (mode) {
		case 2: return "wave";
		case 3: return "audio";
		case 4: return "screen";
		default: return "rain";
	}
}

// The two lanes that read something outside the keyboard. Nothing in this
// panel may open either of them except a press on Start.
static bool sensorLane(int mode) {
	return mode == 3 || mode == 4;
}

// What the chosen effect does, in one line, directly under the chooser —
// the shape the On-board effects tab already has, so both tabs answer
// "what am I picking?" in the same place and the same voice.
static const char *liveSummaryFor(int mode) {
	switch (mode) {
		case 1: return "Drops fall over the colors you painted and fade back "
			"into them.";
		case 2: return "The whole board breathes together, or the curve "
			"travels across the keys.";
		case 3: return "The board follows what this computer is playing, or "
			"what a microphone hears.";
		case 4: return "The board takes its colors from what is on your "
			"display.";
		default: return "Drops fall down an unlit board, one key at a time.";
	}
}

// The one setting more than one lane has in common, in one wording. It
// was "Blend over the current colors" on the sound page, the same words
// again on the screen page with the opposite default, and a whole second
// entry in the chooser for the raindrop — three costumes for one idea.
static const char blendLabel[] = "_Blend over your colors";

// What becomes of a running effect when the window goes away. Said in one
// sentence, used both by the line under the chooser while one runs and by
// the screen caption's warning before one starts — the tab described this
// twice, in two wordings, and a person reading both could not tell
// whether they were the same promise.
static std::string closingSentence(bool screen) {
	if (TrayIcon::available())
		return "Closing the window keeps it running — the tray icon has the "
		       "controls.";
	if (screen)
		return "Closing the window asks whether to keep reading the screen.";
	return "Closing the window leaves it running in the background.";
}

// --- Shaping the tab --------------------------------------------------
//
// Every setting on every page is one row: its name on the left, its
// control on the right, all four pages sharing one name column. The
// effects differ in what they do, not in how they are laid out, so they
// are built from the helpers below rather than each arranging itself.

// A combo asks to be as wide as its widest row, and a sound source
// called "Family 17h/19h HD Audio Controller Analog Stereo (monitor)"
// asked for 684 pixels of it — which is how a panel in a 707px pane came
// to demand 807 and clip every control in it against the window edge.
// Ellipsizing the renderer gives the closed combo a floor of a few
// characters instead. The popup keeps its natural width, so the full
// name stays readable in the one place it has to be: the list you pick
// from.
static void fitToPane(Gtk::ComboBoxText &combo) {
	combo.set_popup_fixed_width(false);
	const std::vector<Gtk::CellRenderer*> cells = combo.get_cells();
	for (size_t i = 0; i < cells.size(); ++i) {
		Gtk::CellRendererText *text =
			dynamic_cast<Gtk::CellRendererText*>(cells[i]);
		if (!text)
			continue;
		text->property_ellipsize() = Pango::ELLIPSIZE_END;
		text->property_width_chars() = 8;
	}
}

// Pieces the panel has to reach again later — the two size groups that
// hold its columns straight, a line it rewrites, a row it shows and
// hides — but which are worth no member of their own. They ride on a
// widget that *is* a member, keyed by name; the data holds a reference,
// so a pointer read back can never outlive what it points at.
static void keepOn(Gtk::Widget &owner, const char *key, void *object) {
	g_object_set_data_full(G_OBJECT(owner.gobj()), key,
		g_object_ref(object), (GDestroyNotify)g_object_unref);
}

static void *keptOn(Gtk::Widget &owner, const char *key) {
	return g_object_get_data(G_OBJECT(owner.gobj()), key);
}

// Every row on this tab is three cells: the name, the control it names,
// and one cell at the end for whatever trails the control — the button
// that acts on it, the number it is set to, or nothing at all. Two size
// groups hold the first and the last to one width across all four pages,
// so changing effect does not shuffle the controls sideways and every
// combo, trough and graph on the tab ends on the same right edge. The
// page builders are reached through fixed signatures — a Gtk::Box and
// nothing else — so both groups travel on the box.
static const char nameColumnKey[] = "kb-live-name-column";
static const char tailColumnKey[] = "kb-live-tail-column";

static void setColumns(Gtk::Box *page,
                       const Glib::RefPtr<Gtk::SizeGroup> &names,
                       const Glib::RefPtr<Gtk::SizeGroup> &tails) {
	keepOn(*page, nameColumnKey, names->gobj());
	keepOn(*page, tailColumnKey, tails->gobj());
}

static Glib::RefPtr<Gtk::SizeGroup> column(Gtk::Box *page, const char *key) {
	if (void *group = keptOn(*page, key))
		return Glib::wrap(GTK_SIZE_GROUP(group), true);
	return Gtk::SizeGroup::create(Gtk::SIZE_GROUP_HORIZONTAL);
}

static Glib::RefPtr<Gtk::SizeGroup> nameColumn(Gtk::Box *page) {
	return column(page, nameColumnKey);
}

static Glib::RefPtr<Gtk::SizeGroup> tailColumn(Gtk::Box *page) {
	return column(page, tailColumnKey);
}

// What updateAnimUI() rewrites and shows, parked on the stack — which is
// a member, so it can find them again.
static const char liveNoteKey[] = "kb-live-note";
static const char sensorNoteKey[] = "kb-live-sensor-note";
static const char levelRowKey[] = "kb-live-level-row";
static const char dropSpeedKey[] = "kb-live-drop-speed";
// The row Start is in, which nothing this panel does to itself may scroll
// out of view.
static const char transportRowKey[] = "kb-live-transport";

static Gtk::Label *keptLabel(Gtk::Widget &owner, const char *key) {
	void *label = keptOn(owner, key);
	return label ? Glib::wrap(GTK_LABEL(label)) : NULL;
}

static Gtk::Widget *keptWidget(Gtk::Widget &owner, const char *key) {
	void *widget = keptOn(owner, key);
	return widget ? Glib::wrap(GTK_WIDGET(widget)) : NULL;
}

// Marks a row built by addSetting, so the fold below can find them all
// wherever they were packed — the head of the panel, any of the four
// pages, or the drawer inside one of them.
static const char settingRowKey[] = "kb-live-setting-row";

// One setting: the name, the control it names, and the cell that trails
// it. The label is pointed at the control, which is what makes a screen
// reader say "Listen to" rather than "combo box". `tail` is the button
// that acts on the control or the label that reads out its value; rows
// with neither are given the empty cell anyway, which is what keeps a
// combo with a button beside it and a combo without one ending on the
// same edge. `named` covers the case where the control is a box of its
// own — the label belongs to the thing in it, not to the box.
//
// The control and its trailing cell are one box inside the row rather
// than two more children of it, so that turning the row on its side puts
// the name above that pair instead of stacking all three. That is what
// foldNarrowRows() does, and why it can do it by setting one property.
static Gtk::Box *addSetting(Gtk::Box *page, const char *name,
                            Gtk::Widget &control, Gtk::Widget *tail = NULL,
                            Gtk::Widget *named = NULL) {
	Gtk::Box *row = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 8));
	g_object_set_data(G_OBJECT(row->gobj()), settingRowKey, row);
	Gtk::Label *label = Gtk::manage(new Gtk::Label(name));
	label->set_xalign(0);
	label->set_valign(Gtk::ALIGN_CENTER);
	Gtk::Widget &subject = named ? *named : control;
	label->set_mnemonic_widget(subject);
	// A slider, a meter or a colour well carries no text of its own, so
	// the row's name is the only thing there is to announce for it.
	//
	// A combo box is the same case, and used to be left out of it on the
	// reasoning that it "reads out its value". It does — and that is the
	// whole problem: what GTK hands a screen reader as the *name* of a
	// combo is its current value, so this one announced itself as
	// "Raindrop, combo box", said the value twice, never said what it
	// chose, and changed its own name as you arrowed through the list.
	// The label relation alone did not fix it, because a name that is
	// present wins over a relation. A spin button is worse still: it has
	// no text at all to fall back on.
	if (dynamic_cast<Gtk::Range*>(&subject) || dynamic_cast<Gtk::LevelBar*>(&subject) ||
	    dynamic_cast<Gtk::ColorButton*>(&subject) ||
	    dynamic_cast<Gtk::ComboBox*>(&subject) ||
	    dynamic_cast<Gtk::SpinButton*>(&subject))
		subject.get_accessible()->set_name(name);
	nameColumn(page)->add_widget(*label);
	row->pack_start(*label, false, false);
	Gtk::Box *cell = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 8));
	cell->pack_start(control, true, true);
	if (!tail)
		tail = Gtk::manage(new Gtk::Label());
	tailColumn(page)->add_widget(*tail);
	cell->pack_start(*tail, false, false);
	row->pack_start(*cell, true, true);
	page->pack_start(*row, false, false);
	return row;
}

// Name beside control needs the width of both columns and the control
// between them — 364px for this tab, which is more than the column it
// sits in is given at a 980-wide window. What that cost was not a tidier
// layout but a working one: the tab was read through a horizontal
// scrollbar, with Start — the one control the whole tab exists for — cut
// in half by the edge of the window. Below that width the name moves
// above the pair it names, which needs 248 and fits with room over.
//
// The width to fold at is measured rather than written down. It is what
// the page asks for while it is unfolded, so it stays right when a row is
// added, a label reworded or a slider given a wider read-out. Only
// measured while unfolded, because folded is not the question being
// asked.
static const char foldedKey[] = "kb-live-folded";
static const char wideEnoughKey[] = "kb-live-wide-enough";

static void turnRows(Gtk::Widget &root, bool folded) {
	Gtk::Box *box = dynamic_cast<Gtk::Box*>(&root);
	if (box && g_object_get_data(G_OBJECT(box->gobj()), settingRowKey)) {
		box->set_orientation(folded ? Gtk::ORIENTATION_VERTICAL :
			Gtk::ORIENTATION_HORIZONTAL);
		// Beside its control a name wants the gap between two columns;
		// above it, it wants the gap between a caption and its subject.
		box->set_spacing(folded ? 2 : 8);
	}
	if (Gtk::Container *container = dynamic_cast<Gtk::Container*>(&root))
		for (Gtk::Widget *child : container->get_children())
			if (child)
				turnRows(*child, folded);
}

static void foldNarrowRows(Gtk::Widget &anywhereOnTheTab) {
	Gtk::ScrolledWindow *pane = NULL;
	for (Gtk::Widget *up = anywhereOnTheTab.get_parent(); up; up = up->get_parent())
		if ((pane = dynamic_cast<Gtk::ScrolledWindow*>(up)))
			break;
	if (!pane)
		return;
	pane->signal_size_allocate().connect(sigc::track_obj(
		[pane](Gtk::Allocation &area) {
			Gtk::Viewport *port = dynamic_cast<Gtk::Viewport*>(pane->get_child());
			Gtk::Widget *content = port ? port->get_child() : NULL;
			if (!content)
				return;
			GObject *marks = G_OBJECT(pane->gobj());
			const bool folded =
				g_object_get_data(marks, foldedKey) != NULL;
			int wide = GPOINTER_TO_INT(g_object_get_data(marks, wideEnoughKey));
			if (!folded) {
				int natural = 0;
				content->get_preferred_width(wide, natural);
				g_object_set_data(marks, wideEnoughKey, GINT_TO_POINTER(wide));
			}
			if (wide <= 0)
				return;
			// A few pixels of slack, so that dragging the divider past the
			// exact width cannot leave the rows turning over and back.
			const bool wants = folded ? area.get_width() < wide + 8 :
				area.get_width() < wide;
			if (wants == folded)
				return;
			g_object_set_data(marks, foldedKey,
				wants ? GINT_TO_POINTER(1) : NULL);
			// Next time round rather than in the middle of this allocation:
			// turning a row asks for a new layout, and asking for one from
			// inside the layout it would replace is how a window comes to
			// spend a frame arguing with itself.
			Glib::signal_idle().connect_once(sigc::track_obj(
				[content, wants]() { turnRows(*content, wants); }, *content));
		}, *pane));
}

// A control that names itself — a check box, a button — starts where the
// names do, because its own text is its name and the names are the page's
// left edge.
//
// It used to be pushed across by an invisible label the width of the name
// column, on the reasoning that a check box belongs in the column its
// value would be in. That made a third edge in one card: the names at the
// left, this at 102px in, and the "Sensitivity and smoothing" drawer and
// the "Choose a different screen…" button — which name themselves too and
// are packed straight into the page — back at the left again. Whichever
// edge is right, two of them for one kind of thing is not, and the one
// every other self-naming control on the tab already used is this one.
static Gtk::Box *addSpanning(Gtk::Box *page, Gtk::Widget &control) {
	Gtk::Box *row = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 8));
	row->pack_start(control, false, false);
	page->pack_start(*row, false, false);
	return row;
}

// Prose runs the width of the page: it explains the page, not one row.
static void addNote(Gtk::Box *page, Gtk::Label &note, bool dim = true) {
	note.set_xalign(0);
	note.set_line_wrap(true);
	// A wrapped label asks for its whole paragraph on one line unless it
	// is told where to stop, and the panel would then be as wide as the
	// longest sentence in it.
	note.set_max_width_chars(38);
	if (dim)
		note.get_style_context()->add_class("kb-hint");
	page->pack_start(note, false, false);
}

// Something this panel shows and hides itself. It is shown once, here,
// because the window-wide show_all_children() that runs after the panel
// is built would otherwise put back whatever the panel had hidden.
static void selfManaged(Gtk::Widget &widget) {
	widget.show_all();
	widget.set_no_show_all(true);
}

// A slider with no number on it is a guess, and a number with no unit is
// a riddle. The number goes in the row's end cell rather than on the
// slider: a slider that draws its own value reserves room for the widest
// number its range can produce, so a 0..100 row and a 250..8000 row ended
// their troughs on different edges for a reason that had nothing to do
// with the design. Read out beside the trough instead, in the column the
// buttons are in, and every trough ends where every combo does.
static Gtk::Label *readsOut(Gtk::Scale &scale, const char *unit) {
	const std::string suffix = unit;
	scale.set_draw_value(false);
	scale.set_digits(0);
	Gtk::Label *value = Gtk::manage(new Gtk::Label());
	value->set_xalign(0);
	value->set_valign(Gtk::ALIGN_CENTER);
	value->get_style_context()->add_class("kb-hint");
	Gtk::Scale *slider = &scale;
	const sigc::slot<void> show = [slider, value, suffix]() {
		value->set_text(Glib::ustring::compose("%1%2",
			(int)(slider->get_value() + 0.5), suffix));
	};
	show();
	scale.signal_value_changed().connect(sigc::track_obj(show, *value));
	return value;
}

// A slider row: name, trough, the number it is set to.
static Gtk::Box *addSlider(Gtk::Box *page, const char *name, Gtk::Scale &scale,
                           const char *unit, const char *what) {
	scale.set_tooltip_text(what);
	return addSetting(page, name, scale, readsOut(scale, unit));
}

// The same, for a pair of sliders that are two ends of one setting: one
// reading of the range in the row's end cell rather than a number wedged
// between the two troughs, which at the narrowest window would leave
// neither of them wide enough to aim at.
static Gtk::Label *readsRange(Gtk::Scale &low, Gtk::Scale &high,
                              const char *unit) {
	const std::string suffix = unit;
	low.set_draw_value(false);
	high.set_draw_value(false);
	Gtk::Label *value = Gtk::manage(new Gtk::Label());
	value->set_xalign(0);
	value->set_valign(Gtk::ALIGN_CENTER);
	value->get_style_context()->add_class("kb-hint");
	Gtk::Scale *from = &low;
	Gtk::Scale *to = &high;
	const sigc::slot<void> show = [from, to, value, suffix]() {
		value->set_text(Glib::ustring::compose("%1–%2%3",
			(int)(from->get_value() + 0.5), (int)(to->get_value() + 0.5),
			suffix));
	};
	show();
	low.signal_value_changed().connect(sigc::track_obj(show, *value));
	high.signal_value_changed().connect(sigc::track_obj(show, *value));
	return value;
}

// A colour well is sized like a control, not stretched across the row: a
// swatch says "this is the colour", a full-width bar says "this is the
// setting", and these were reading as the latter.
static void asSwatch(ColorPickButton &button, const char *what) {
	button.set_use_alpha(false);
	button.set_halign(Gtk::ALIGN_START);
	button.set_valign(Gtk::ALIGN_CENTER);
	button.set_size_request(64, -1);
	button.get_style_context()->add_class("kb-swatch");
	button.set_tooltip_text(what);
}

// The input meter is drawn as this many blocks rather than as one
// continuous bar: a bar's empty trough is a hairline here, invisible at
// exactly the moment it most needs to read as a meter with nothing in
// it. Whatever sets the meter scales into this.
static const double levelBlocks = 20.0;

// A drawer that opens below the fold has opened nothing: the arrow turns,
// the pane does not move, and what it revealed is off the bottom of the
// window with no sign that it arrived. This brings it into view — but
// never far enough to push `keep` off the top, which is the row Start is
// in. Start is the point of this tab, and a disclosure triangle that took
// it away would be a worse trade than a drawer nobody can see: the panel
// scrolls itself only as far as it can without hiding the one control
// that makes the tab do anything. Past that the scrollbar is the user's.
//
// It runs on an idle at the back of the queue so it measures the layout
// the drawer caused rather than a stale one, and it is tracked on both
// widgets, so a drawer disposed before the idle runs takes the callback
// with it.
static void revealAfterLayout(Gtk::Widget &widget, Gtk::Widget &keep) {
	// By pointer, by value: a reference parameter captured by reference is
	// gone the moment this returns, and the callback runs later.
	Gtk::Widget *shown = &widget;
	Gtk::Widget *safe = &keep;
	Glib::signal_idle().connect(sigc::track_obj([shown, safe]() {
		Gtk::ScrolledWindow *pane = NULL;
		for (Gtk::Widget *up = shown->get_parent(); up; up = up->get_parent())
			if ((pane = dynamic_cast<Gtk::ScrolledWindow*>(up)))
				break;
		Gtk::Viewport *port = pane ?
			dynamic_cast<Gtk::Viewport*>(pane->get_child()) : NULL;
		Gtk::Widget *content = port ? port->get_child() : NULL;
		if (!content)
			return false;
		int x = 0, y = 0, safeX = 0, safeY = 0;
		if (!shown->translate_coordinates(*content, 0, 0, x, y) ||
		    !safe->translate_coordinates(*content, 0, 0, safeX, safeY))
			return false;
		const Glib::RefPtr<Gtk::Adjustment> down = pane->get_vadjustment();
		double foot = y + shown->get_allocated_height() - down->get_page_size();
		foot = std::min(foot, down->get_upper() - down->get_page_size());
		// Not one pixel past the top of the row that has to stay whole.
		foot = std::min(foot, (double)safeY);
		if (foot > down->get_value())
			down->set_value(foot);
		return false;
	}, widget, keep), Glib::PRIORITY_LOW);
}

// A check box's label is a sentence, and a sentence that cannot wrap is
// a panel as wide as the sentence. The cap is on what the label asks
// for, not on what it can shrink to — a wrapping label's floor is its
// longest word either way — so it is set just past the longest of these
// sentences, which keeps them on one line wherever there is room and
// still lets them fold in a narrow pane.
static void wrapLabel(Gtk::CheckButton &check) {
	Gtk::Label *label = dynamic_cast<Gtk::Label*>(check.get_child());
	if (!label)
		return;
	label->set_line_wrap(true);
	label->set_max_width_chars(44);
	label->set_xalign(0);
}

// Which point on the curve the keyboard is on. It rides on the graph
// rather than in a member, and is only ever drawn while the graph has the
// focus, so a pointer user never sees a cursor that means nothing to them.
static const char wavePointKey[] = "kb-live-wave-point";

static int selectedWavePoint(WaveGraph &graph) {
	const int stored = GPOINTER_TO_INT(
		g_object_get_data(G_OBJECT(graph.gobj()), wavePointKey));
	const int count = (int)graph.getWave().points.size();
	if (count < 1)
		return -1;
	return std::max(0, std::min(stored, count - 1));
}

static void selectWavePoint(WaveGraph &graph, int index) {
	g_object_set_data(G_OBJECT(graph.gobj()), wavePointKey,
		GINT_TO_POINTER(std::max(0, index)));
	// The cursor is drawn by the overlay above the graph, which is a
	// window of its own: repainting the graph alone would leave the mark
	// where the last point was.
	if (Gtk::Widget *host = graph.get_parent())
		host->queue_draw();
	graph.queue_draw();
}

// Everything the pointer can do to the curve, on keys. The graph is drawn
// rather than built, so it has no keyboard behaviour of its own — and
// without this the one control that shapes a wave by hand can only be
// reached with a mouse, which is not a control everyone has.
//
// `changed` says whether the wave itself moved, which is what the caller
// has to pass on to a running effect.
static bool editWaveByKey(WaveGraph &graph, GdkEventKey *event, bool &changed) {
	Wave wave = graph.getWave();
	const int count = (int)wave.points.size();
	const int at = selectedWavePoint(graph);
	if (at < 0 || at >= count)
		return false;
	const bool fine = (event->state & GDK_SHIFT_MASK) != 0;
	WavePoint point = wave.points[at];
	bool moved = false;

	switch (event->keyval) {
		case GDK_KEY_Left:
		case GDK_KEY_KP_Left:
			// Unshifted the arrows walk the points; shifted they carry the
			// one you are on along the curve.
			if (!fine) {
				selectWavePoint(graph, at - 1);
				return true;
			}
			point.t = std::max(0.0f, point.t - 0.02f);
			moved = true;
			break;
		case GDK_KEY_Right:
		case GDK_KEY_KP_Right:
			if (!fine) {
				selectWavePoint(graph, std::min(at + 1, count - 1));
				return true;
			}
			point.t = std::min(1.0f, point.t + 0.02f);
			moved = true;
			break;
		case GDK_KEY_Up:
		case GDK_KEY_KP_Up:
			point.y = std::min(1.0f, point.y + (fine ? 0.01f : 0.05f));
			moved = true;
			break;
		case GDK_KEY_Down:
		case GDK_KEY_KP_Down:
			point.y = std::max(0.0f, point.y - (fine ? 0.01f : 0.05f));
			moved = true;
			break;
		case GDK_KEY_Home:
			selectWavePoint(graph, 0);
			return true;
		case GDK_KEY_End:
			selectWavePoint(graph, count - 1);
			return true;
		case GDK_KEY_Insert:
		case GDK_KEY_plus:
		case GDK_KEY_KP_Add: {
			// Halfway to the next point, at the height the curve is
			// already passing through there, so adding one changes nothing
			// until it is moved.
			const float next = at + 1 < count ? wave.points[at + 1].t : 1.0f;
			const float t = (point.t + next) / 2.0f;
			wave.points.push_back(WavePoint(t, wave.brightnessAt(t)));
			graph.setWave(wave);
			for (size_t i = 0; i < graph.getWave().points.size(); ++i)
				if (graph.getWave().points[i].t == t) {
					selectWavePoint(graph, (int)i);
					break;
				}
			changed = true;
			return true;
		}
		case GDK_KEY_Delete:
		case GDK_KEY_KP_Delete:
		case GDK_KEY_BackSpace:
			// A colour sitting on this point goes first, then the point
			// itself — so one key takes back both of the things Enter and
			// Insert put there, in the order they were added.
			for (size_t i = 0; i < wave.colors.size(); ++i) {
				if (std::fabs(wave.colors[i].t - point.t) > 0.02f ||
				    wave.colors.size() < 2)
					continue;
				wave.colors.erase(wave.colors.begin() + i);
				graph.setWave(wave);
				changed = true;
				return true;
			}
			// Two points are the least a curve can be drawn from.
			if (count <= 2)
				return true;
			wave.points.erase(wave.points.begin() + at);
			graph.setWave(wave);
			selectWavePoint(graph, at - 1);
			changed = true;
			return true;
		case GDK_KEY_Return:
		case GDK_KEY_KP_Enter: {
			Gdk::RGBA rgba = fromLedColor(wave.colorAt(point.t));
			if (!colorpicker::run(
			    dynamic_cast<Gtk::Window*>(graph.get_toplevel()),
			    "Wave color stop", rgba))
				return true;
			wave.colors.push_back(WaveColorStop(point.t, toLedColor(rgba)));
			graph.setWave(wave);
			changed = true;
			return true;
		}
		default:
			return false;
	}

	if (!moved)
		return false;
	wave.points[at] = point;
	graph.setWave(wave);
	// setWave() sorts, so the index no longer names the point that was
	// just moved as soon as it crosses a neighbour — follow it by value.
	for (size_t i = 0; i < graph.getWave().points.size(); ++i)
		if (graph.getWave().points[i].t == point.t &&
		    graph.getWave().points[i].y == point.y) {
			selectWavePoint(graph, (int)i);
			break;
		}
	changed = true;
	return true;
}

void MainWindow::buildWaveSection(Gtk::Box* waveBox) {
	static const char *waveModeNames[] = { "Pulse (all keys)", "Travel (across board)" };
	for (const char *name : waveModeNames) m_waveModeCombo.append(name);
	m_waveModeCombo.set_active(0);
	fitToPane(m_waveModeCombo);
	m_waveModeCombo.set_tooltip_text(
		"Pulse: the whole board brightens and dims together · "
		"Travel: the same curve, running across the keys");
	addSetting(waveBox, "Motion", m_waveModeCombo);

	static const char *waveShapeNames[] = { "Sine", "Triangle", "Square", "Saw" };
	for (const char *name : waveShapeNames) m_waveShapeCombo.append(name);
	m_waveShapeCombo.set_active(0);
	fitToPane(m_waveShapeCombo);
	m_waveShapeCombo.set_tooltip_text("Redraws the curve below as this shape");
	addSetting(waveBox, "Shape", m_waveShapeCombo);

	addSlider(waveBox, "Cycle", m_wavePeriodScale, " ms",
		"How long one pass through the curve takes");

	// One setting, not two: the range the board moves between, read out
	// once as a range. Each end still says which it is, to the pointer and
	// to a screen reader.
	m_waveMinScale.set_tooltip_text("How dim the board goes at the bottom of the curve");
	m_waveMaxScale.set_tooltip_text("How bright it goes at the top of it");
	Gtk::Box *range = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 6));
	Gtk::Label *toLabel = Gtk::manage(new Gtk::Label("to"));
	toLabel->get_style_context()->add_class("kb-hint");
	range->pack_start(m_waveMinScale, true, true);
	range->pack_start(*toLabel, false, false);
	range->pack_start(m_waveMaxScale, true, true);
	addSetting(waveBox, "Brightness", *range,
		readsRange(m_waveMinScale, m_waveMaxScale, "%"), &m_waveMinScale);
	// The row is named for the pair; each end still names itself.
	m_waveMinScale.get_accessible()->set_name("Dimmest");
	m_waveMaxScale.get_accessible()->set_name("Brightest");

	// The graph is the wave, so it takes the width the pane can give it.
	// Its own 220px floor is what used to run it off the right edge. The
	// height is the page's slack: this is the tallest of the four, and it
	// has to end above the fold of a 620px window, not below it.
	m_waveGraph.set_size_request(120, 80);
	m_waveGraph.set_tooltip_text(
		"Click to add or drag a point · Double-click for a color stop · "
		"Right-click to delete · Keyboard: ← → pick a point, ↑ ↓ its "
		"brightness, Shift+← → move it in time, Insert adds, Delete "
		"removes, Enter puts a color on it");
	m_waveGraph.get_accessible()->set_name("Brightness curve");
	// The description is set by updateWaveDescription(), which keeps it
	// current.
	m_waveGraph.set_can_focus(true);
	m_waveGraph.add_events(Gdk::KEY_PRESS_MASK | Gdk::FOCUS_CHANGE_MASK);
	m_waveGraph.signal_key_press_event().connect(
		[this](GdkEventKey *event) {
			bool changed = false;
			if (!editWaveByKey(m_waveGraph, event, changed))
				return false;
			if (changed)
				onWaveGraphChanged();
			return true;
		}, false);
	// A drawn control shows nothing of its own about focus or about where
	// the keyboard is on it, and both have to be visible for the keys
	// above to be usable. Both are drawn by a transparent sheet laid over
	// the graph rather than by the graph itself: WaveGraph::on_draw
	// returns true, which ends the emission before anything connected
	// after it could run, and it owns a window of its own, so a parent
	// drawing at the same place would be painted over. An overlay child is
	// the one thing that is above it — and pass-through, so the pointer
	// still reaches the graph underneath.
	Gtk::Overlay *curveHost = Gtk::manage(new Gtk::Overlay());
	curveHost->add(m_waveGraph);
	Gtk::DrawingArea *cursor = Gtk::manage(new Gtk::DrawingArea());
	cursor->set_can_focus(false);
	curveHost->add_overlay(*cursor);
	curveHost->set_overlay_pass_through(*cursor, true);
	cursor->signal_draw().connect(sigc::track_obj(
		[this, cursor](const Cairo::RefPtr<Cairo::Context> &cr) {
			// is_focus(), not has_focus(): "the keyboard is on this
			// control" is what the ring and the cursor say, and that stays
			// true while the window itself is not the active one.
			if (!m_waveGraph.is_focus())
				return false;
			const double w = cursor->get_allocated_width();
			const double h = cursor->get_allocated_height();
			const Glib::RefPtr<Gtk::StyleContext> style =
				m_waveGraph.get_style_context();
			const Wave &wave = m_waveGraph.getWave();
			const int at = selectedWavePoint(m_waveGraph);
			if (at >= 0 && (size_t)at < wave.points.size()) {
				// Mirrors WaveGraph::toPixel(): the plot is inset 8 either
				// side, 10 from the top and 18 from the bottom.
				const double left = 8, right = w - 8, top = 10, bottom = h - 18;
				const double px = left + wave.points[at].t * (right - left);
				const double py = bottom - wave.points[at].y * (bottom - top);
				Gdk::RGBA mark("#66ccff");
				style->lookup_color("kb_accent", mark);
				cr->set_source_rgb(mark.get_red(), mark.get_green(),
					mark.get_blue());
				cr->set_line_width(2);
				cr->arc(px, py, 7.5, 0, 6.28318);
				cr->stroke();
			}
			style->render_focus(cr, 0, 0, w, h);
			return false;
		}, *cursor), false);
	// The property, not focus-in-event: the event only arrives when the
	// whole window becomes active, so a graph that took the focus inside
	// a window that already had it would keep an unmarked curve.
	m_waveGraph.property_is_focus().signal_changed().connect(
		sigc::track_obj([cursor]() { cursor->queue_draw(); }, *cursor));
	addSetting(waveBox, "Curve", *curveHost, NULL, &m_waveGraph);
	// No line under the graph summarising the wave: it said "Pulse Sine ·
	// 1.00 Hz (1000 ms) · brightness 15–100%", which is Motion, Shape,
	// Cycle and Brightness read back to you from four rows up, and the
	// row it cost pushed the bottom of this page out of a 820px window.
	// Pressing Start still says the whole thing in the status line.
}

// One chooser, one settings page, one button. The lanes were always
// mutually exclusive; four separate Start buttons only hid that.
void MainWindow::buildLiveSection(Gtk::Box* liveBox) {
	if (!m_liveFrame) m_liveFrame = (Gtk::Frame*)liveBox->get_parent();
	liveBox->set_spacing(8);
	// Reading down this tab gave "Live effects" (the tab), "Driven by
	// this computer…" (its subtitle), "Live effects" (this card) and then
	// "Effect" — four headings for one list, one of which repeated the
	// tab you clicked to get here. The card is the whole of the tab, so
	// the tab has already named it; the line it costs is worth more to
	// the panel at a 620px window than the repetition is.
	if (m_liveFrame && m_liveFrame->get_label_widget()) {
		m_liveFrame->get_label_widget()->set_no_show_all(true);
		m_liveFrame->get_label_widget()->hide();
	}

	static const char *modeNames[] = {
		"Raindrop", "Raindrop over your colors", "Wave", "Sound reactive",
		"Screen colors"
	};
	for (const char *name : modeNames) m_liveModeCombo.append(name);
	m_liveModeCombo.set_active(0);
	fitToPane(m_liveModeCombo);

	const Glib::RefPtr<Gtk::SizeGroup> names =
		Gtk::SizeGroup::create(Gtk::SIZE_GROUP_HORIZONTAL);
	const Glib::RefPtr<Gtk::SizeGroup> tails =
		Gtk::SizeGroup::create(Gtk::SIZE_GROUP_HORIZONTAL);
	// The head of the panel is built from the same three cells as every
	// settings row below it, so the chooser lines up with the controls it
	// chooses between.
	setColumns(liveBox, names, tails);

	// What will run, and the button that runs it, in the panel's first
	// row. This is the one thing the tab exists for: it cannot live at
	// the foot of a settings page taller than the pane, where a short
	// window hides it completely.
	m_liveToggleButton.set_use_underline(true);
	m_liveToggleButton.set_label("Sta_rt");
	// Wide enough that the one action worth pressing looks like one, and
	// fixed so the row does not twitch when "Start" becomes "Stop".
	m_liveToggleButton.set_size_request(96, -1);
	// The middle rung, like every other in-tab action: the one filled
	// button in the window is Send to keyboard.
	m_liveToggleButton.get_style_context()->add_class("kb-secondary");
	Gtk::Box *transport = addSetting(liveBox, "Effect", m_liveModeCombo,
		&m_liveToggleButton);
	keepOn(m_liveStack, transportRowKey, transport->gobj());

	// One line under the chooser, saying the most useful thing there is to
	// say about the selected effect right now: what it does, until it is
	// doing it — and then what becomes of it if the window closes, which
	// is the surprise worth spending a line on. Every effect gets one, so
	// the four read as four settings of one thing rather than as four
	// unrelated forms, and the card does not change height when Start is
	// pressed. updateAnimUI() owns what it says.
	Gtk::Label *note = Gtk::manage(new Gtk::Label());
	addNote(liveBox, *note);
	note->set_text(liveSummaryFor(m_liveModeCombo.get_active_row_number()));
	keepOn(m_liveStack, liveNoteKey, note->gobj());

	// What the two sensor lanes read, said before Start is pressed and
	// before anything else on the page: at a 620px window a settings page
	// does not fit in the pane, and a disclosure at the foot of one is a
	// disclosure the person who most needs it never sees. Both lanes put
	// it in the same place, in the same voice, undimmed — a live
	// microphone and a live screen capture are not quiet context.
	Gtk::Label *sourceNote = Gtk::manage(new Gtk::Label());
	addNote(liveBox, *sourceNote, false);
	sourceNote->set_text("This source is a microphone. Start records from it "
		"until you press Stop.");
	keepOn(m_liveStack, sensorNoteKey, sourceNote->gobj());
	selfManaged(*sourceNote);
	sourceNote->hide();
	// Whether this line belongs on screen is one rule with two triggers —
	// a different source, or a different lane — so both go through the one
	// place that holds it. Tracked: the combo outlives the panel's
	// teardown, so an untracked connection would fire into a half-disposed
	// tree.
	m_audioSourceCombo.signal_changed().connect(sigc::track_obj(
		[this]() { updateAnimUI(); }, *sourceNote));

	// The screen lane's half of the same disclosure: what this effect
	// reads, and whether the desktop will ask first.
	// updateScreenCaption() keeps it true and shows it only for that lane.
	addNote(liveBox, m_screenCaptionLabel, false);
	selfManaged(m_screenCaptionLabel);
	m_screenCaptionLabel.hide();
	// The one action that changes what the caption above says, next to it
	// rather than at the bottom of a page that does not fit: an action,
	// not a setting, so it takes the width of its label and sits at the
	// panel's own edge.
	m_screenForgetButton.set_tooltip_text(
		"Ask again which screen or window to capture the next time you "
		"press Start");
	m_screenForgetButton.set_halign(Gtk::ALIGN_START);
	liveBox->pack_start(m_screenForgetButton, false, false);
	// Until a screen has been shared there is nothing to choose again
	// from, and the caption says so in words; updateScreenCaption() brings
	// the button back the moment there is.
	selfManaged(m_screenForgetButton);
	m_screenForgetButton.hide();

	// The two raindrop modes share one settings page (the rain colour).
	Gtk::Box *rainPage = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_VERTICAL, 6));
	setColumns(rainPage, names, tails);
	buildAnimSection(rainPage);
	Gtk::Box *wavePage = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_VERTICAL, 6));
	setColumns(wavePage, names, tails);
	buildWaveSection(wavePage);
	Gtk::Box *audioPage = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_VERTICAL, 6));
	setColumns(audioPage, names, tails);
	buildAudioSection(audioPage);
	Gtk::Box *screenPage = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_VERTICAL, 6));
	setColumns(screenPage, names, tails);
	buildScreenSection(screenPage);
	m_liveStack.add(*rainPage, "rain");
	m_liveStack.add(*wavePage, "wave");
	m_liveStack.add(*audioPage, "audio");
	m_liveStack.add(*screenPage, "screen");
	m_liveStack.set_transition_type(Gtk::STACK_TRANSITION_TYPE_NONE);
	// A stack is as tall as its tallest page unless told otherwise, which
	// left raindrop — the shortest page by far — sitting on top of 450px
	// of empty card belonging to the wave. Each effect is now as tall as
	// its own controls and the card ends where they end.
	m_liveStack.set_vhomogeneous(false);
	liveBox->pack_start(m_liveStack, false, false);

	// Every row on the tab is built by now, including the four pages'
	// and the ones in the drawer, so this is the moment the fold can
	// measure what the wide form costs.
	foldNarrowRows(*liveBox);

	if (m_liveFrame) m_liveFrame->set_visible(false);
}

void MainWindow::buildAnimSection(Gtk::Box* animBox) {
	m_animColorButton.set_rgba(Gdk::RGBA("#4488ff"));
	asSwatch(m_animColorButton, "The color of the drops");
	addSetting(animBox, "Color", m_animColorButton);

	// The one thing this effect has that was never on screen. It ran at a
	// hard-coded frame of 100ms while every other lane offered its timing
	// — the wave its cycle, both sensors their smoothing — which is what
	// left this page with a single control on it. Read as a percentage of
	// that default, so the direction of the slider and the direction of
	// the effect agree: further right is faster.
	Gtk::Scale *speed = Gtk::manage(new Gtk::Scale(
		Gtk::Adjustment::create(100.0, 50.0, 250.0, 5.0, 25.0),
		Gtk::ORIENTATION_HORIZONTAL));
	addSlider(animBox, "Speed", *speed, "%",
		"How fast the drops fall and how often a new one starts");
	keepOn(m_liveStack, dropSpeedKey, speed->gobj());
	// What the two raindrop rows do differently — fall onto an unlit board
	// or onto the colours you painted — is said by the line under the
	// chooser, with every other effect's, rather than only here.
}

// The drop interval the Speed row asks for. 100% is the 100ms the effect
// used to be fixed at, and the two are inverse: twice the speed is half
// the interval.
static unsigned dropIntervalMs(Gtk::Widget &stack) {
	if (Gtk::Widget *found = keptWidget(stack, dropSpeedKey))
		if (Gtk::Range *speed = dynamic_cast<Gtk::Range*>(found))
			return (unsigned)std::max(20.0,
				std::min(400.0, 10000.0 / std::max(1.0, speed->get_value())));
	return 100;
}

void MainWindow::buildAudioSection(Gtk::Box* audioBox) {
	fitToPane(m_audioSourceCombo);
	m_audioRefreshButton.set_tooltip_text("Look again for sound sources");
	// Under Start, and the same width as it, so this combo and the one
	// above it end on the same edge.
	addSetting(audioBox, "Listen to", m_audioSourceCombo, &m_audioRefreshButton);

	// A microphone is not the same offer as "what you hear". The line
	// that says so is at the head of the panel with the screen lane's,
	// not here: at the smallest window this page starts at the fold, and
	// a promise printed below it is a promise nobody reads.

	static const char *audioModeNames[] = {
		"Spectrum bars", "Rainbow spectrum", "Level meter", "Beat flash"
	};
	for (const char *name : audioModeNames) m_audioModeCombo.append(name);
	m_audioModeCombo.set_active(0);
	fitToPane(m_audioModeCombo);
	m_audioModeCombo.set_tooltip_text(
		"Bars: columns are frequency bands, lit height is level · "
		"Rainbow: fixed hue per column, brightness follows the band · "
		"Level: the whole board follows loudness · "
		"Beat: a flash on every detected beat");
	addSetting(audioBox, "Show", m_audioModeCombo);

	m_audioLowColorButton.set_rgba(Gdk::RGBA("#0066ff"));
	m_audioHighColorButton.set_rgba(Gdk::RGBA("#ff0066"));
	asSwatch(m_audioLowColorButton, "The color at silence");
	asSwatch(m_audioHighColorButton, "The color at full level");
	Gtk::Box *quietRow = addSetting(audioBox, "Quiet", m_audioLowColorButton);
	Gtk::Box *loudRow = addSetting(audioBox, "Loud", m_audioHighColorButton);
	selfManaged(*quietRow);
	selfManaged(*loudRow);
	// Rainbow takes its hue from where a key is, not from these two, so
	// there they would be controls that do nothing. Better absent.
	m_audioModeCombo.signal_changed().connect(sigc::track_obj(
		[this, quietRow, loudRow]() {
			const bool used = m_audioModeCombo.get_active_row_number() != 1;
			quietRow->set_visible(used);
			loudRow->set_visible(used);
		}, *quietRow, *loudRow));

	// One name and one default for the setting both sensor lanes share:
	// it was the same box worded the same way but switched on for sound
	// and off for the screen, which made one setting look like two.
	m_audioOverlayCheck.set_label(blendLabel);
	wrapLabel(m_audioOverlayCheck);
	m_audioOverlayCheck.set_active(true);
	m_audioOverlayCheck.set_tooltip_text(
		"Mix the effect into the key colors from the Colors tab instead of "
		"over black, so the legends stay lit when nothing is playing");
	addSpanning(audioBox, m_audioOverlayCheck);

	m_audioLevelBar.set_mode(Gtk::LEVEL_BAR_MODE_DISCRETE);
	m_audioLevelBar.set_min_value(0.0);
	m_audioLevelBar.set_max_value(levelBlocks);
	m_audioLevelBar.set_value(0.0);
	m_audioLevelBar.set_size_request(-1, 12);
	m_audioLevelBar.set_valign(Gtk::ALIGN_CENTER);
	m_audioLevelBar.set_tooltip_text("What the effect is hearing right now");
	Gtk::Box *levelRow = addSetting(audioBox, "Level", m_audioLevelBar);
	// Nothing is being listened to until Start is pressed, so before then
	// this can only ever read empty — a meter pinned at zero that a person
	// has to decide is honest rather than broken. It appears with the
	// sound it is measuring. updateAnimUI() owns that.
	keepOn(m_liveStack, levelRowKey, levelRow->gobj());
	selfManaged(*levelRow);
	levelRow->hide();

	// Tuning most people never touch, folded away — behind a title that
	// says what is folded away rather than "Fine tuning".
	Gtk::Box *advanced = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_VERTICAL, 6));
	advanced->set_margin_top(4);
	Gtk::Expander *advancedExpander =
		Gtk::manage(new Gtk::Expander("Sensitivity and smoothing"));
	advancedExpander->add(*advanced);
	// The rows inside the drawer are rows of this page, so they share its
	// columns rather than lining up on their own.
	setColumns(advanced, nameColumn(audioBox), tailColumn(audioBox));
	Gtk::Widget *transport = keptWidget(m_liveStack, transportRowKey);
	advancedExpander->property_expanded().signal_changed().connect(
		sigc::track_obj([advancedExpander, transport]() {
			if (advancedExpander->get_expanded() && transport)
				revealAfterLayout(*advancedExpander, *transport);
		}, *advancedExpander));
	addSlider(advanced, "Sensitivity", m_audioGainScale, "%",
		"How hard a given loudness drives the board");
	addSlider(advanced, "Smoothing", m_audioSmoothScale, "%",
		"How slowly a bar falls back after a peak");
	wrapLabel(m_audioAutoGainCheck);
	m_audioAutoGainCheck.set_active(true);
	m_audioAutoGainCheck.set_tooltip_text(
		"Follow the loudest recent peak, so quiet passages still light the "
		"board and loud ones do not flatten it");
	addSpanning(advanced, m_audioAutoGainCheck);
	audioBox->pack_start(*advancedExpander, false, false);
}

void MainWindow::buildScreenSection(Gtk::Box* screenBox) {
	static const char *screenModeNames[] = {
		"Mirror the screen", "Single dominant color"
	};
	for (const char *name : screenModeNames) m_screenModeCombo.append(name);
	m_screenModeCombo.set_active(0);
	fitToPane(m_screenModeCombo);
	m_screenModeCombo.set_tooltip_text(
		"Mirror: each key takes the color of the part of the screen above "
		"it · Dominant: one color for the whole board, taken from the most "
		"colorful part of the screen");
	addSetting(screenBox, "Show", m_screenModeCombo);

	addSlider(screenBox, "Color boost", m_screenBoostScale, "%",
		"Screens are mostly muted greys; this pushes what the keys show "
		"towards saturated color. 0 reproduces the screen exactly");

	addSlider(screenBox, "Smoothing", m_screenSmoothScale, "%",
		"How slowly a key follows the screen — video would otherwise "
		"strobe the board");

	// The same box, the same words and the same default as the sound
	// lane's: one setting, not two that happen to look alike.
	m_screenOverlayCheck.set_label(blendLabel);
	wrapLabel(m_screenOverlayCheck);
	m_screenOverlayCheck.set_active(true);
	m_screenOverlayCheck.set_tooltip_text(
		"Mix the screen into the key colors from the Colors tab instead of "
		"over black, so the dark parts of the screen leave your own colors "
		"showing");
	addSpanning(screenBox, m_screenOverlayCheck);

	// The caption that says what this effect reads, and the button that
	// re-aims it, are at the head of the panel with the sound lane's
	// disclosure rather than at the foot of this page.
}

void MainWindow::stopAnimationIfRunning() {
	// Called before the device handle changes: every animation writes to
	// m_kbd from a worker thread, so none of them may outlive it.
	if (!m_raindrop.isRunning() && !m_wavePlayer.isRunning() &&
	    !m_audioPlayer.isRunning() && !m_screenPlayer.isRunning())
		return;
	m_expectedAnimation.clear();
	m_raindrop.stop();
	m_wavePlayer.stop();
	m_audioPlayer.stop();
	m_screenPlayer.stop();
	clearPreviewState();
	updateAnimUI();
}

bool MainWindow::applyRainFromCommands(const std::vector<ProfileCommand> &commands) {
	const ProfileCommand *rain = NULL;
	for (size_t i = 0; i < commands.size(); ++i) {
		if (commands[i].type == ProfileCommand::Type::rain)
			rain = &commands[i];
	}
	if (!rain || rain->value == 0)
		return false;
	Gdk::RGBA rgba;
	rgba.set_rgba(rain->color.red / 255.0, rain->color.green / 255.0,
	              rain->color.blue / 255.0, 1.0);
	m_animColorButton.set_rgba(rgba);
	startAnimation(rain->rainOverlay);
	return raindropActive();
}

bool MainWindow::rainActive() const {
	return raindropActive() || m_wavePlayer.isRunning() || audioActive() ||
	       screenActive();
}

bool MainWindow::raindropActive() const {
	return m_raindrop.isRunning() || rainDaemonAlive();
}

// What is on the board right now, for labels, refusals and status text.
const char *MainWindow::activeAnimationName() const {
	if (raindropActive())
		return m_animOverlay ? "color rain" : "raindrop";
	if (m_wavePlayer.isRunning())
		return "wave";
	if (audioActive())
		return "sound reactive";
	if (screenActive())
		return "screen colors";
	return NULL;
}

// Instant-lane guard: LedKeyboard has no locking, so the GTK thread must
// not write to it while an animation worker is writing frames.
bool MainWindow::blockedByAnimation(const char *action) {
	const char *name = activeAnimationName();
	if (!name)
		return false;
	status(std::string("Stop the ") + name + " before " + action);
	return true;
}

bool MainWindow::animControlsOk() const {
	return m_controlsEnabled && has(help::KeyboardFeatures::setkey) &&
	       has(help::KeyboardFeatures::rgb);
}

bool MainWindow::audioControlsOk() const {
	return animControlsOk() && AudioCapture::available();
}

std::string MainWindow::rainStatePath() const {
	return Glib::get_user_config_dir() + "/g810-led/raindrop.state";
}

std::string MainWindow::rainPidPath() const {
	return Glib::get_user_config_dir() + "/g810-led/raindrop.pid";
}

// A live pid is not proof that our daemon is behind it: nothing removes
// the file if the daemon dies on its own, and pids are recycled (the
// counter restarts every boot). Believing a recycled pid greys out half
// the window, and signalling it would SIGTERM an unrelated process — so
// confirm the process really is the daemon this file claims.
bool MainWindow::daemonAlive(const std::string &pidPath, const char *flag) {
	std::ifstream file(pidPath);
	if (!file.is_open())
		return false;
	pid_t pid = 0;
	file >> pid;
	if (pid <= 0 || ::kill(pid, 0) != 0)
		return false;

	std::ifstream cmdline("/proc/" + std::to_string(pid) + "/cmdline",
	                      std::ios::binary);
	if (!cmdline.is_open())
		return false;
	std::string args((std::istreambuf_iterator<char>(cmdline)),
	                 std::istreambuf_iterator<char>());
	// argv is NUL separated: argv[0] must be this program and one of the
	// arguments must be the daemon flag.
	bool ourProgram = false;
	bool ourFlag = false;
	size_t at = 0;
	for (int index = 0; at < args.size(); ++index) {
		std::string arg(args.c_str() + at);
		if (index == 0) {
			std::string name = arg.substr(arg.find_last_of('/') + 1);
			ourProgram = name.compare(0, 8, "g810-led") == 0;
		} else if (arg == flag) {
			ourFlag = true;
		}
		at += arg.size() + 1;
	}
	return ourProgram && ourFlag;
}

bool MainWindow::rainDaemonAlive() const {
	return daemonActive(m_rainDaemonStatus, rainPidPath(), "--rain-daemon",
	                    rainStatePath());
}

bool MainWindow::writeRainState() {
	if (!m_kbd.isOpen())
		return false;
	std::string dir = Glib::get_user_config_dir() + "/g810-led";
	g_mkdir_with_parents(dir.c_str(), 0755);
	LedKeyboard::DeviceInfo device = m_kbd.getCurrentDevice();
	RaindropAnimation::State state;
	state.vendorID = device.vendorID;
	state.productID = device.productID;
	state.serial = device.serialNumber;
	state.color = toLedColor(m_animColorButton.get_rgba());
	state.intervalMs = dropIntervalMs(m_liveStack);
	state.overlay = m_animOverlay;
	state.positions = m_keyboardWidget.getKeyPositions();
	if (m_animOverlay)
		state.base = m_raindrop.isRunning() ?
			m_raindrop.baseColors() : draftKeyColors();
	return RaindropAnimation::saveState(rainStatePath(), state);
}

// Re-exec ourselves detached, in the daemon mode selected by flag, on
// the state file just written. The pid lands in pidPath so a later
// session (or a stop press) can find it again.
void MainWindow::spawnDaemon(const char *flag, const std::string &statePath,
                             const std::string &pidPath) {
	pid_t pid = fork();
	if (pid < 0)
		return;
	if (pid == 0) {
		setsid();
		int fd = ::open("/dev/null", O_RDWR);
		if (fd >= 0) {
			dup2(fd, 0);
			dup2(fd, 1);
			dup2(fd, 2);
			if (fd > 2)
				::close(fd);
		}
		char exe[4096];
		ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
		if (n < 1)
			_exit(1);
		exe[n] = '\0';
		execl(exe, exe, flag, statePath.c_str(), (char *)NULL);
		_exit(1);
	}
	std::string dir = Glib::get_user_config_dir() + "/g810-led";
	g_mkdir_with_parents(dir.c_str(), 0755);
	std::ofstream file(pidPath);
	if (file.is_open())
		file << pid << "\n";
	invalidateDaemonStatus();
}

void MainWindow::killDaemon(const std::string &pidPath, const char *flag) {
	// Only signal a pid we have just confirmed is our own daemon.
	if (daemonAlive(pidPath, flag)) {
		std::ifstream file(pidPath);
		pid_t pid = 0;
		file >> pid;
		if (pid > 0) {
			::kill(pid, SIGTERM);
			// It can still be mid-frame. Every caller takes the device
			// back immediately afterwards, and two processes writing key
			// colours at once produce a mixed board, so wait briefly for
			// it to let go (it exits within one animation interval).
			for (int waited = 0; waited < 40 && ::kill(pid, 0) == 0; ++waited)
				usleep(10000);
		}
	}
	unlink(pidPath.c_str());
	invalidateDaemonStatus();
}

void MainWindow::spawnRainDaemon() {
	if (!writeRainState())
		return;
	spawnDaemon("--rain-daemon", rainStatePath(), rainPidPath());
}

void MainWindow::killRainDaemon() {
	killDaemon(rainPidPath(), "--rain-daemon");
}

// A daemon started for a different keyboard must not be adopted into
// this window's controls. The state file opens with vendor/product/
// serial, so only its first lines need reading.
bool MainWindow::daemonMatchesDevice(const std::string &statePath) const {
	if (!m_deviceIsOpen)
		return true;  // nothing to compare against yet
	std::ifstream file(statePath);
	if (!file.is_open())
		return true;
	unsigned vendor = 0, product = 0;
	std::string serial;
	std::string tag;
	for (int line = 0; line < 3 && file >> tag; ++line) {
		if (tag == "vendor")
			file >> std::hex >> vendor >> std::dec;
		else if (tag == "product")
			file >> std::hex >> product >> std::dec;
		else if (tag == "serial")
			std::getline(file >> std::ws, serial);
		else
			break;
	}
	if (vendor != m_openVendorID || product != m_openProductID)
		return false;
	return serial.empty() || m_openSerial.empty() || serial == m_openSerial;
}

bool MainWindow::daemonActive(DaemonStatus &status, const std::string &pidPath,
                              const char *flag, const std::string &statePath) const {
	const gint64 now = g_get_monotonic_time();
	if (status.checkedAt != 0 && now - status.checkedAt < 200000)
		return status.active;
	status.active = daemonAlive(pidPath, flag) && daemonMatchesDevice(statePath);
	status.checkedAt = now;
	return status.active;
}

void MainWindow::invalidateDaemonStatus() {
	m_rainDaemonStatus.checkedAt = 0;
	m_audioDaemonStatus.checkedAt = 0;
	m_screenDaemonStatus.checkedAt = 0;
}

bool MainWindow::audioActive() const {
	return m_audioPlayer.isRunning() || audioDaemonAlive();
}

std::string MainWindow::audioStatePath() const {
	return Glib::get_user_config_dir() + "/g810-led/audio.state";
}

std::string MainWindow::audioPidPath() const {
	return Glib::get_user_config_dir() + "/g810-led/audio.pid";
}

bool MainWindow::audioDaemonAlive() const {
	return daemonActive(m_audioDaemonStatus, audioPidPath(), "--audio-daemon",
	                    audioStatePath());
}

// False when there is nothing to hand a daemon — the caller must not
// spawn one then, or it would start from a stale state file.
bool MainWindow::writeAudioState() {
	if (!m_kbd.isOpen())
		return false;
	std::string dir = Glib::get_user_config_dir() + "/g810-led";
	g_mkdir_with_parents(dir.c_str(), 0755);
	LedKeyboard::DeviceInfo device = m_kbd.getCurrentDevice();
	AudioPlayer::State state;
	state.vendorID = device.vendorID;
	state.productID = device.productID;
	state.serial = device.serialNumber;
	state.source = m_audioSourceName.empty() ?
		selectedAudioSource() : m_audioSourceName;
	state.settings = audioSettings();
	state.positions = m_keyboardWidget.getKeyPositions();
	// Hand over exactly what the running effect blends over. Re-reading
	// the draft here would pick up edits made while it ran — including
	// ones the user just chose to discard on the way out.
	state.base = m_audioPlayer.isRunning() ?
		m_audioPlayer.baseColors() : draftKeyColors();
	return AudioPlayer::saveState(audioStatePath(), state);
}

void MainWindow::killAudioDaemon() {
	killDaemon(audioPidPath(), "--audio-daemon");
}

std::string MainWindow::screenStatePath() const {
	return Glib::get_user_config_dir() + "/g810-led/screen.state";
}

std::string MainWindow::screenPidPath() const {
	return Glib::get_user_config_dir() + "/g810-led/screen.pid";
}

bool MainWindow::screenDaemonAlive() const {
	return daemonActive(m_screenDaemonStatus, screenPidPath(), "--screen-daemon",
	                    screenStatePath());
}

// False when there is nothing to hand a daemon — the caller must not
// spawn one then, or it would start from a stale state file.
bool MainWindow::writeScreenState() {
	if (!m_kbd.isOpen())
		return false;
	std::string dir = Glib::get_user_config_dir() + "/g810-led";
	g_mkdir_with_parents(dir.c_str(), 0755);
	LedKeyboard::DeviceInfo device = m_kbd.getCurrentDevice();
	ScreenPlayer::State state;
	state.vendorID = device.vendorID;
	state.productID = device.productID;
	state.serial = device.serialNumber;
	state.settings = screenSettings();
	state.positions = m_keyboardWidget.getKeyPositions();
	// Hand over exactly what the running effect blends over, not the
	// draft as it stands now — which may hold edits made while it ran.
	state.base = m_screenPlayer.isRunning() ?
		m_screenPlayer.baseColors() : draftKeyColors();
	return ScreenPlayer::saveState(screenStatePath(), state);
}

void MainWindow::killScreenDaemon() {
	killDaemon(screenPidPath(), "--screen-daemon");
}

// Asked on the way out, every time, and answered "stop" by default.
// The other lanes generate their own light; this one reads the screen,
// so leaving it running with no window is a decision that belongs to
// the person closing the window, not to a setting they made once.
bool MainWindow::askKeepScreenRunning() {
	// Without a remembered share the daemon could only get a screen by
	// putting a portal dialog up out of nowhere. Do not offer that.
	if (!ScreenCapture::sourceRemembered())
		return false;
	Gtk::MessageDialog dialog(*this, "Keep screen colors running?", false,
		Gtk::MESSAGE_QUESTION, Gtk::BUTTONS_NONE, true);
	dialog.set_secondary_text(
		"The keyboard can go on mirroring the screen after this window "
		"closes. A background process will keep reading the screen you "
		"shared until you stop it — open this window again and press Stop.");
	dialog.add_button("_Stop it", Gtk::RESPONSE_REJECT);
	dialog.add_button("_Keep running", Gtk::RESPONSE_ACCEPT);
	dialog.set_default_response(Gtk::RESPONSE_REJECT);
	return dialog.run() == Gtk::RESPONSE_ACCEPT;
}

void MainWindow::handoffScreenDaemon() {
	if (!m_screenPlayer.isRunning())
		return;
	if (!writeScreenState()) {
		m_screenPlayer.stop();
		return;
	}
	spawnDaemon("--screen-daemon", screenStatePath(), screenPidPath());
	m_expectedAnimation.clear();
	m_expectedByDaemon = false;
	m_screenPlayer.stop();
	m_keyboardWidget.clearPreview();
}

// Push a saved/running configuration back into the controls.
void MainWindow::applyScreenState(const ScreenPlayer::State &state) {
	m_screenModeCombo.set_active(
		state.settings.mode == ScreenPlayer::Mode::dominant ? 1 : 0);
	m_screenBoostScale.set_value(state.settings.boost * 100.0);
	m_screenSmoothScale.set_value(state.settings.smoothing * 100.0);
	m_screenOverlayCheck.set_active(state.settings.overlay);
}

// Window closing with sound-reactive lighting on: hand the effect to a
// detached copy of ourselves so the keyboard keeps reacting.
void MainWindow::handoffAudioDaemon() {
	if (!m_audioPlayer.isRunning())
		return;
	if (!writeAudioState()) {
		m_audioPlayer.stop();
		return;
	}
	spawnDaemon("--audio-daemon", audioStatePath(), audioPidPath());
	m_expectedAnimation.clear();
	m_expectedByDaemon = false;
	m_audioPlayer.stop();
	m_keyboardWidget.clearPreview();
}

void MainWindow::handoffRainDaemon() {
	if (!m_raindrop.isRunning())
		return;
	spawnRainDaemon();
	m_expectedAnimation.clear();
	m_expectedByDaemon = false;
	m_raindrop.stop();
	m_keyboardWidget.clearPreview();
}

void MainWindow::stopRainEverywhere() {
	m_expectedAnimation.clear();
	m_expectedByDaemon = false;
	m_raindrop.stop();
	m_wavePlayer.stop();
	m_audioPlayer.stop();
	m_screenPlayer.stop();
	clearPreviewState();
	killAudioDaemon();
	killScreenDaemon();
	killRainDaemon();
	persistLastProfile();
}

void MainWindow::updateWaveDescription() {
	const std::string said = m_waveGraph.getWave().describe();
	m_waveDescLabel.set_text(said);
	// The whole wave in one sentence, on the graph itself. A sighted
	// reader has the four rows above it; someone hearing the page read
	// out has the curve announced as what it actually is, rather than as
	// a drawing area with a name.
	m_waveGraph.get_accessible()->set_description(said +
		". Click to add or drag a point, double-click for a color stop, "
		"right-click to delete one.");
}

void MainWindow::onWavePresetChanged() {
	if (m_waveIgnore)
		return;
	Wave wave = m_waveGraph.getWave();
	int shape = m_waveShapeCombo.get_active_row_number();
	if (shape == 1) wave.shape = Wave::Shape::triangle;
	else if (shape == 2) wave.shape = Wave::Shape::square;
	else if (shape == 3) wave.shape = Wave::Shape::saw;
	else wave.shape = Wave::Shape::sine;
	wave.applyPreset();
	m_waveIgnore = true;
	m_waveGraph.setWave(wave);
	m_waveIgnore = false;
	onWaveParamsChanged();
}

void MainWindow::onWaveParamsChanged() {
	if (m_waveIgnore)
		return;
	Wave wave = m_waveGraph.getWave();
	wave.mode = m_waveModeCombo.get_active_row_number() == 1 ?
		Wave::Mode::travel : Wave::Mode::pulse;
	wave.periodMs = (unsigned)m_wavePeriodScale.get_value();
	float minY = (float)m_waveMinScale.get_value() / 100.0f;
	float maxY = (float)m_waveMaxScale.get_value() / 100.0f;
	if (maxY < minY)
		maxY = minY;
	wave.minY = minY;
	wave.maxY = maxY;
	m_waveIgnore = true;
	m_waveGraph.setWave(wave);
	m_waveIgnore = false;
	updateWaveDescription();
	if (m_wavePlayer.isRunning())
		m_wavePlayer.setWave(wave);
}

void MainWindow::onWaveGraphChanged() {
	if (m_waveIgnore)
		return;
	updateWaveDescription();
	if (m_wavePlayer.isRunning())
		m_wavePlayer.setWave(m_waveGraph.getWave());
}

void MainWindow::startWave() {
	if (!ensureOpen())
		return;
	// Only one animation may own the device at a time (a profile load
	// can reach this without going through the toggle). Taking the board
	// back also ends whatever the watchdog was expecting, or a failure
	// below would be reported as the *previous* effect dying.
	m_expectedAnimation.clear();
	m_raindrop.stop();
	m_audioPlayer.stop();
	m_screenPlayer.stop();
	killRainDaemon();
	killAudioDaemon();
	killScreenDaemon();
	onWaveParamsChanged();
	m_keyboardWidget.clearPreview();
	m_wavePlayer.start(m_waveGraph.getWave(),
		m_keyboardWidget.getKeyPositions(),
		[this](const LedKeyboard::KeyValueArray &values) {
			return m_kbd.setKeys(values) && m_kbd.commit();
		},
		[this](const LedKeyboard::KeyValueArray &values) {
			// Every player ticks on a GTK timer, so this arrives on the
			// GTK thread already — see the note on Preview in
			// gui/FramePlayer.h.
			previewFrame(values);
		});
	if (!m_wavePlayer.isRunning()) {
		statusError("Failed to start wave");
		return;
	}
	noteAnimationStarted("Wave");
	m_deviceShowsPreview = true;
	persistLastProfile();
	updateAnimUI();
	status("Wave running — " + m_waveGraph.getWave().describe());
}

void MainWindow::appendWaveCommands(std::vector<ProfileCommand> &commands) const {
	if (!m_wavePlayer.isRunning())
		return;
	const Wave &wave = m_waveGraph.getWave();
	ProfileCommand header;
	header.type = ProfileCommand::Type::wave;
	header.waveTravel = wave.mode == Wave::Mode::travel;
	header.waveShape = wave.shape == Wave::Shape::triangle ? 1 :
		wave.shape == Wave::Shape::square ? 2 :
		wave.shape == Wave::Shape::saw ? 3 : 0;
	header.period = std::chrono::duration<uint16_t, std::milli>(
		(uint16_t)std::min(wave.periodMs, 65000u));
	header.waveMin = (uint8_t)std::round(wave.minY * 100);
	header.waveMax = (uint8_t)std::round(wave.maxY * 100);
	commands.push_back(header);
	for (const WavePoint &point : wave.points) {
		ProfileCommand cmd;
		cmd.type = ProfileCommand::Type::wavePoint;
		cmd.waveT = point.t;
		cmd.waveY = point.y;
		commands.push_back(cmd);
	}
	for (const WaveColorStop &stop : wave.colors) {
		ProfileCommand cmd;
		cmd.type = ProfileCommand::Type::waveColor;
		cmd.waveT = stop.t;
		cmd.color = stop.color;
		commands.push_back(cmd);
	}
}

void MainWindow::appendAudioCommands(std::vector<ProfileCommand> &commands) const {
	if (!audioActive())
		return;
	AudioPlayer::Settings settings = audioSettings();
	ProfileCommand command;
	command.type = ProfileCommand::Type::audio;
	command.value = 1;
	command.audioMode = settings.mode == AudioPlayer::Mode::rainbow ? 1 :
		settings.mode == AudioPlayer::Mode::level ? 2 :
		settings.mode == AudioPlayer::Mode::beat ? 3 : 0;
	command.color = settings.lowColor;
	command.audioHighColor = settings.highColor;
	command.audioGain = (uint16_t)std::round(settings.gain * 100.0f);
	command.audioSmoothing = (uint8_t)std::round(settings.smoothing * 100.0f);
	command.audioAutoGain = settings.autoGain;
	command.audioOverlay = settings.overlay;
	// The name resolved at start time, so reloading the profile listens
	// to the same device rather than to whatever the default is then.
	command.audioSource = m_audioSourceName;
	commands.push_back(command);
}

void MainWindow::appendScreenCommands(std::vector<ProfileCommand> &commands) const {
	if (!screenActive())
		return;
	const ScreenPlayer::Settings settings = screenSettings();
	ProfileCommand command;
	command.type = ProfileCommand::Type::screen;
	command.value = 1;
	command.screenMode = settings.mode == ScreenPlayer::Mode::dominant ? 1 : 0;
	command.screenBoost = (uint8_t)std::round(settings.boost * 100.0f);
	command.screenSmoothing = (uint8_t)std::round(settings.smoothing * 100.0f);
	command.screenOverlay = settings.overlay;
	// Deliberately no display identifier: which monitor this is means
	// nothing on someone else's machine, and the desktop already
	// remembers the local answer.
	commands.push_back(command);
}

// Whether the profile now being applied is one the user asked for by
// name. Both sensor lanes have to answer this the same way — a notice to
// dismiss at every launch is noise, and a silent load of a file the user
// just opened is a sensor configured behind their back — and only one of
// them is told: applyScreenFromCommands takes the flag, and all three
// callers run it first, on the same commands, before the audio lane. So
// the screen lane records the answer for both. The alternative is a
// second parameter on applyAudioFromCommands, which means changing its
// declaration in MainWindow.h.
static bool sensorLoadAsked = true;

// Never starts the capture, whatever the file asks for. Listening to an
// output monitor is a gentler thing than listening to the room, but it
// is still a recording stream the sound server opens on this machine's
// behalf, and a profile is a file that can arrive from anyone — or that
// was written by a session that happened to have the effect running.
// This used to start itself at every launch from the remembered
// session, which is a keyboard backlight deciding on its own to record.
// So: load the settings, point the chooser at them, and leave the one
// press that starts it to the person at the keyboard. Always returns
// false, which is what the callers read as "nothing was started".
bool MainWindow::applyAudioFromCommands(const std::vector<ProfileCommand> &commands) {
	const ProfileCommand *audio = NULL;
	for (size_t i = 0; i < commands.size(); ++i) {
		if (commands[i].type == ProfileCommand::Type::audio)
			audio = &commands[i];
	}
	if (!audio || audio->value == 0)
		return false;
	m_audioModeCombo.set_active((int)std::min<uint8_t>(audio->audioMode, 3));
	m_audioLowColorButton.set_rgba(fromLedColor(audio->color));
	m_audioHighColorButton.set_rgba(fromLedColor(audio->audioHighColor));
	m_audioGainScale.set_value(audio->audioGain);
	m_audioSmoothScale.set_value(audio->audioSmoothing);
	m_audioAutoGainCheck.set_active(audio->audioAutoGain);
	m_audioOverlayCheck.set_active(audio->audioOverlay);
	int row = -1;
	for (size_t i = 0; i < m_audioSources.size(); ++i) {
		if (m_audioSources[i].name == audio->audioSource) {
			row = (int)i + 2;
			break;
		}
	}
	// A source that is gone (different machine, unplugged headset)
	// falls back to the default output rather than failing the load.
	m_audioSourceCombo.set_active(row >= 0 ? row : 0);
	// Point the chooser at the loaded settings so Start is one press
	// away — without letting onLiveModeChanged start anything.
	if (!rainActive()) {
		m_liveIgnore = true;
		m_liveModeCombo.set_active(3);
		m_liveStack.set_visible_child("audio");
		m_liveIgnore = false;
	}
	updateAnimUI();
	// Quiet when the session is being restored: the panel is the
	// disclosure then — the chooser points at the effect and the line
	// under it names what would be listened to — and a notice to dismiss
	// at every launch is noise. A profile the user just opened gets the
	// notice, because nothing else about the window says the file wants a
	// sensor. Exactly what the screen lane does, for the same reasons.
	if (!sensorLoadAsked)
		return false;
	const std::string wants = selectedSourceIsMonitor() ?
		std::string("This profile uses sound-reactive lighting, which listens "
			"to what this computer is playing.") :
		"This profile wants to listen to \"" +
			m_audioSourceCombo.get_active_text().raw() +
			"\", which is a microphone.";
	statusError(wants + " Settings loaded — press Start if that is what you "
		"want.", std::vector<std::string>(), true);
	return false;
}

// Never starts the capture. A profile is a file that can arrive from
// anyone, and this effect reads the display — so it may set the controls
// and say so, and that is all. Always returns false, which is what the
// callers read as "nothing was started".
bool MainWindow::applyScreenFromCommands(const std::vector<ProfileCommand> &commands,
                                         bool announce) {
	// Recorded before the early return below: the audio lane reads it, and
	// a file with no screen line in it is still a file the user did or did
	// not ask for.
	sensorLoadAsked = announce;
	const ProfileCommand *screen = NULL;
	for (size_t i = 0; i < commands.size(); ++i) {
		if (commands[i].type == ProfileCommand::Type::screen)
			screen = &commands[i];
	}
	if (!screen || screen->value == 0)
		return false;
	m_screenModeCombo.set_active(screen->screenMode == 1 ? 1 : 0);
	m_screenBoostScale.set_value(screen->screenBoost);
	m_screenSmoothScale.set_value(screen->screenSmoothing);
	m_screenOverlayCheck.set_active(screen->screenOverlay);
	// Point the chooser at the loaded settings so Start is one click
	// away — without letting onLiveModeChanged start anything.
	if (!rainActive()) {
		m_liveIgnore = true;
		m_liveModeCombo.set_active(4);
		m_liveStack.set_visible_child("screen");
		m_liveIgnore = false;
	}
	// updateAnimUI() is what keeps the caption true, so it says the
	// right thing about the settings just loaded.
	updateAnimUI();
	if (announce)
		statusError("This profile uses screen colors, which reads what is on "
			"your display. Settings loaded — press Start if that is what you "
			"want.", std::vector<std::string>(), true);
	return false;
}

bool MainWindow::applyWaveFromCommands(const std::vector<ProfileCommand> &commands) {
	Wave wave;
	bool have = false;
	for (size_t i = 0; i < commands.size(); ++i) {
		const ProfileCommand &command = commands[i];
		if (command.type == ProfileCommand::Type::wave) {
			wave.mode = command.waveTravel ? Wave::Mode::travel : Wave::Mode::pulse;
			wave.shape = command.waveShape == 1 ? Wave::Shape::triangle :
				command.waveShape == 2 ? Wave::Shape::square :
				command.waveShape == 3 ? Wave::Shape::saw : Wave::Shape::sine;
			wave.periodMs = command.period.count();
			wave.minY = command.waveMin / 100.0f;
			wave.maxY = command.waveMax / 100.0f;
			wave.points.clear();
			wave.colors.clear();
			have = true;
		} else if (command.type == ProfileCommand::Type::wavePoint) {
			wave.points.push_back(WavePoint(command.waveT, command.waveY));
			have = true;
		} else if (command.type == ProfileCommand::Type::waveColor) {
			wave.colors.push_back(WaveColorStop(command.waveT, command.color));
			have = true;
		}
	}
	if (!have)
		return false;
	if (wave.points.empty())
		wave.applyPreset();
	wave.sort();
	m_waveIgnore = true;
	m_waveModeCombo.set_active(wave.mode == Wave::Mode::travel ? 1 : 0);
	m_waveShapeCombo.set_active((int)wave.shape);
	m_wavePeriodScale.set_value(wave.periodMs);
	m_waveMinScale.set_value(wave.minY * 100);
	m_waveMaxScale.set_value(wave.maxY * 100);
	m_waveGraph.setWave(wave);
	m_waveIgnore = false;
	updateWaveDescription();
	startWave();
	return m_wavePlayer.isRunning();
}

void MainWindow::refreshAudioSources() {
	setBusy(true);
	// One round trip for the list and the defaults; asking separately
	// used to cost two connections.
	m_audioSources = AudioCapture::query().sources;
	setBusy(false);
	int active = m_audioSourceCombo.get_active_row_number();
	m_audioSourceCombo.remove_all();
	// The two defaults resolve at start time, so they keep working when
	// the default sink or microphone changes.
	m_audioSourceCombo.append("Default output (what you hear)");
	m_audioSourceCombo.append("Default input (microphone)");
	for (size_t i = 0; i < m_audioSources.size(); ++i) {
		std::string label = m_audioSources[i].description;
		if (m_audioSources[i].monitor)
			label += " (monitor)";
		m_audioSourceCombo.append(label);
	}
	if (active < 0 || active >= (int)m_audioSources.size() + 2)
		active = 0;
	m_audioSourceCombo.set_active(active);
	if (!AudioCapture::available())
		status("This build has no audio support — install the libpulse "
		       "development files and rebuild for sound-reactive lighting");
	else if (m_audioSources.empty())
		status("No audio sources found — is PulseAudio or PipeWire running?");
}

std::string MainWindow::selectedAudioSource() const {
	int index = m_audioSourceCombo.get_active_row_number();
	if (index == 1)
		return AudioCapture::defaultInputName();
	if (index >= 2 && (size_t)(index - 2) < m_audioSources.size())
		return m_audioSources[index - 2].name;
	return AudioCapture::defaultMonitorName();
}

bool MainWindow::selectedSourceIsMonitor() const {
	const int index = m_audioSourceCombo.get_active_row_number();
	if (index == 1)
		return false;   // "Default input (microphone)"
	if (index >= 2 && (size_t)(index - 2) < m_audioSources.size())
		return m_audioSources[index - 2].monitor;
	return true;        // row 0 is the default output's monitor
}

AudioPlayer::Settings MainWindow::audioSettings() const {
	AudioPlayer::Settings settings;
	int mode = m_audioModeCombo.get_active_row_number();
	settings.mode = mode == 1 ? AudioPlayer::Mode::rainbow :
		mode == 2 ? AudioPlayer::Mode::level :
		mode == 3 ? AudioPlayer::Mode::beat : AudioPlayer::Mode::bars;
	settings.lowColor = toLedColor(m_audioLowColorButton.get_rgba());
	settings.highColor = toLedColor(m_audioHighColorButton.get_rgba());
	settings.gain = (float)(m_audioGainScale.get_value() / 100.0);
	settings.smoothing = (float)(m_audioSmoothScale.get_value() / 100.0);
	settings.autoGain = m_audioAutoGainCheck.get_active();
	settings.overlay = m_audioOverlayCheck.get_active();
	return settings;
}

// The on-screen draft as device colors — the scheme an overlaid effect
// blends into.
std::map<LedKeyboard::Key, LedKeyboard::Color> MainWindow::draftKeyColors() const {
	std::map<LedKeyboard::Key, LedKeyboard::Color> colors;
	const std::map<LedKeyboard::Key, Gdk::RGBA> &draft =
		m_keyboardWidget.getKeyColors();
	for (std::map<LedKeyboard::Key, Gdk::RGBA>::const_iterator it =
	     draft.begin(); it != draft.end(); ++it)
		colors[it->first] = toLedColor(it->second);
	return colors;
}

void MainWindow::onAudioParamsChanged() {
	if (m_audioPlayer.isRunning())
		m_audioPlayer.setSettings(audioSettings());
}

// Push a saved/running configuration back into the controls.
void MainWindow::applyAudioState(const AudioPlayer::State &state) {
	m_audioSourceName = state.source;
	int row = 0;
	for (size_t i = 0; i < m_audioSources.size(); ++i) {
		if (m_audioSources[i].name == state.source) {
			row = (int)i + 2;
			break;
		}
	}
	m_audioSourceCombo.set_active(row);
	m_audioModeCombo.set_active(
		state.settings.mode == AudioPlayer::Mode::rainbow ? 1 :
		state.settings.mode == AudioPlayer::Mode::level ? 2 :
		state.settings.mode == AudioPlayer::Mode::beat ? 3 : 0);
	m_audioLowColorButton.set_rgba(fromLedColor(state.settings.lowColor));
	m_audioHighColorButton.set_rgba(fromLedColor(state.settings.highColor));
	m_audioGainScale.set_value(state.settings.gain * 100.0);
	m_audioSmoothScale.set_value(state.settings.smoothing * 100.0);
	m_audioAutoGainCheck.set_active(state.settings.autoGain);
	m_audioOverlayCheck.set_active(state.settings.overlay);
}

void MainWindow::startAudio() {
	if (!AudioCapture::available()) {
		statusError("This build has no audio support — install the libpulse "
		            "development files and rebuild");
		return;
	}
	if (!ensureOpen())
		return;
	m_keyboardWidget.clearPreview();
	m_expectedAnimation.clear();
	m_raindrop.stop();
	m_wavePlayer.stop();
	m_screenPlayer.stop();
	killRainDaemon();
	killAudioDaemon();
	killScreenDaemon();
	std::string source = selectedAudioSource();
	m_audioSourceName = source;
	bool started = m_audioPlayer.start(source, audioSettings(),
		m_keyboardWidget.getKeyPositions(), draftKeyColors(),
		[this](const LedKeyboard::KeyValueArray &values) {
			return m_kbd.setKeys(values) && m_kbd.commit();
		},
		[this](const LedKeyboard::KeyValueArray &values) {
			// Every player ticks on a GTK timer, so this arrives on the
			// GTK thread already — see the note on Preview in
			// gui/FramePlayer.h.
			previewFrame(values);
		});
	if (!started) {
		std::string error = m_audioPlayer.lastError();
		statusError(error.empty() ?
			std::string("Failed to start sound-reactive lighting") :
			"Failed to start sound-reactive lighting: " + error);
		return;
	}
	noteAnimationStarted("Sound reactive");
	m_deviceShowsPreview = true;
	persistLastProfile();
	updateAnimUI();
	// The chooser's words, not the sound server's. "alsa_output.pci-0000_
	// 2b_00.1.hdmi-stereo-extra1.monitor" is the same fact the board strip
	// reports as "Default output (what you hear)", and one line of the
	// window should not answer a question in a language the line above it
	// does not speak.
	status("Sound reactive running on " +
		m_audioSourceCombo.get_active_text().raw() + " — press Stop to end");
}

ScreenPlayer::Settings MainWindow::screenSettings() const {
	ScreenPlayer::Settings settings;
	settings.mode = m_screenModeCombo.get_active_row_number() == 1 ?
		ScreenPlayer::Mode::dominant : ScreenPlayer::Mode::mirror;
	settings.boost = (float)(m_screenBoostScale.get_value() / 100.0);
	settings.smoothing = (float)(m_screenSmoothScale.get_value() / 100.0);
	settings.overlay = m_screenOverlayCheck.get_active();
	settings.intervalMs = 40;
	return settings;
}

bool MainWindow::screenActive() const {
	return m_screenPlayer.isRunning() || screenDaemonAlive();
}

bool MainWindow::screenControlsOk() const {
	return animControlsOk() && ScreenCapture::available();
}

void MainWindow::onScreenParamsChanged() {
	if (m_screenPlayer.isRunning())
		m_screenPlayer.setSettings(screenSettings());
}

// Says plainly what this effect reads, whether the desktop will ask
// first, and that nothing outlives the window — a keyboard backlight is
// not a reason for anything to keep watching the screen unattended.
//
// While something is being read it says that instead, at full contrast:
// once a capture is live, what it is capturing is the only thing on this
// page worth saying, and it is not quiet context. Every state here comes
// from the player, which is also where the state strip reads it, so the
// two cannot end up disagreeing.
void MainWindow::updateScreenCaption() {
	const bool remembered = ScreenCapture::sourceRemembered();
	// The caption and its button live at the head of the panel, above the
	// settings pages, so they are the screen lane's and only the screen
	// lane's: another effect's page must not be captioned with what this
	// one reads.
	const bool showing = m_liveModeCombo.get_active_row_number() == 4;
	m_screenCaptionLabel.set_visible(showing);
	// Forgetting a screen nobody has chosen does nothing, so there is no
	// button for it until there is one to forget — and it greys out
	// while a capture runs, which is when the portal will not be re-aimed.
	// This is the one place that knows, and the Forget handler calls it,
	// so the button goes the moment its work is done.
	m_screenForgetButton.set_visible(showing && remembered);
	m_screenForgetButton.set_sensitive(remembered && screenControlsOk() &&
		!screenActive());

	if (!ScreenCapture::available()) {
		m_screenCaptionLabel.set_text("This build has no screen capture "
			"support — install the PipeWire development files and rebuild.");
		return;
	}
	if (m_screenPlayer.isRunning() && m_screenPlayer.waiting()) {
		m_screenCaptionLabel.set_text("Waiting for permission — your desktop "
			"is asking which screen or window to share. Nothing is being read "
			"until you answer it.");
		return;
	}
	if (m_screenPlayer.isRunning()) {
		const std::string source = m_screenPlayer.sourceName();
		m_screenCaptionLabel.set_text("Reading " +
			(source.empty() ? std::string("your screen") : source) +
			" now. Stop ends it.");
		return;
	}
	if (screenDaemonAlive()) {
		m_screenCaptionLabel.set_text("A background process is reading the "
			"screen you shared. Stop ends it.");
		return;
	}
	// What the effect does with the screen is said by the line under the
	// chooser, and so is what becomes of it when the window closes — that
	// line says exactly this sentence while the capture runs, which is the
	// only time closing the window can leave one running. What is left for
	// here is the part only this lane has: who is asked, and when. At full
	// contrast, like the sound lane's: what a sensor reads is not quiet
	// context on either of them.
	m_screenCaptionLabel.set_text(remembered ?
		"Your desktop already remembers which screen to share." :
		"Your desktop will ask which screen or window to share the first "
		"time you press Start.");
}

void MainWindow::startScreen() {
	if (!ScreenCapture::available()) {
		statusError("This build has no screen capture support — install the "
		            "PipeWire development files and rebuild");
		return;
	}
	if (!ensureOpen())
		return;
	m_keyboardWidget.clearPreview();
	m_expectedAnimation.clear();
	m_raindrop.stop();
	m_wavePlayer.stop();
	m_audioPlayer.stop();
	killRainDaemon();
	killAudioDaemon();
	killScreenDaemon();
	const bool started = m_screenPlayer.start(screenSettings(),
		m_keyboardWidget.getKeyPositions(), draftKeyColors(),
		[this](const LedKeyboard::KeyValueArray &values) {
			return m_kbd.setKeys(values) && m_kbd.commit();
		},
		[this](const LedKeyboard::KeyValueArray &values) {
			// Every player ticks on a GTK timer, so this arrives on the
			// GTK thread already — see the note on Preview in
			// gui/FramePlayer.h.
			previewFrame(values);
		});
	if (!started) {
		const std::string error = m_screenPlayer.lastError();
		statusError(error.empty() ?
			std::string("Failed to start screen colors") :
			"Failed to start screen colors: " + error);
		return;
	}
	noteAnimationStarted("Screen colors");
	m_deviceShowsPreview = true;
	persistLastProfile();
	// What the strip is about to be painted with. Seeded here rather
	// than left at false: a capture that is already running (the portal
	// remembered the answer) delivers its first frame before the poll
	// below ever sees "waiting", and the strip would then keep claiming
	// it was waiting for permission it already had.
	m_screenWaiting = m_screenPlayer.waiting();
	updateAnimUI();
	// The portal answers on its own thread, so "started" here only means
	// the request is out — the first frame may be a dialog away.
	status(ScreenCapture::sourceRemembered() ?
		"Screen colors running — press Stop to end" :
		"Screen colors: your desktop is asking which screen to share");
}

// Runs while any software animation is up. It feeds the level meter and,
// more importantly, notices an animation that tore itself down — a failed
// device write or a lost audio source stops the player from the inside,
// and without this the UI would keep claiming it is running (so "Stop"
// would start a second one) and the on-screen keyboard would stay frozen
// on the last frame, silently swallowing every paint.
bool MainWindow::pollAnimations() {
	if (m_audioPlayer.isRunning())
		m_audioLevelBar.set_value(levelBlocks *
			std::max(0.0, std::min(1.0, (double)m_audioPlayer.level())));
	else
		m_audioLevelBar.set_value(0.0);

	// The portal answers on its own thread, so the moment the user says
	// yes is not a signal we can connect to — this is where "waiting for
	// permission" turns into "capturing <screen>".
	const bool waiting = m_screenPlayer.isRunning() && m_screenPlayer.waiting();
	if (waiting != m_screenWaiting) {
		m_screenWaiting = waiting;
		updateBoardState();
		// Both ways round: the panel has to stop claiming it is waiting
		// once permission is given, and start saying so when it is not.
		updateScreenCaption();
		if (!waiting && m_screenPlayer.isRunning()) {
			const std::string source = m_screenPlayer.sourceName();
			status("Screen colors running" +
				(source.empty() ? std::string() : " on " + source) +
				" — press Stop to end");
		}
	}

	if (rainActive())
		return true;
	if (m_expectedAnimation.empty())
		return false;

	const std::string name = m_expectedAnimation;
	const bool daemon = m_expectedByDaemon;
	// Only the audio lane keeps an error string, and only while we were
	// the ones driving it — reporting it for a raindrop or a wave would
	// quote a stale message from an earlier audio run.
	std::string reason;
	if (daemon)
		reason = "the background process ended";
	else if (name == "Sound reactive" && !m_audioPlayer.lastError().empty())
		reason = m_audioPlayer.lastError();
	else if (name == "Screen colors" && !m_screenPlayer.lastError().empty())
		reason = m_screenPlayer.lastError();
	else
		reason = "the keyboard write failed — check the connection";
	m_expectedAnimation.clear();
	m_expectedByDaemon = false;
	clearPreviewState();
	const bool restored = restoreAppliedState();
	updateAnimUI();
	status(name + " stopped: " + reason +
		(restored ? " (your colors are back)" : ""));
	return false;
}

// Remember what we started, so the watchdog can tell a crash from a stop
// the user asked for, and keep the watchdog running.
void MainWindow::noteAnimationStarted(const char *name, bool byDaemon) {
	m_expectedAnimation = name;
	m_expectedByDaemon = byDaemon;
	if (!m_animationTimer.connected())
		m_animationTimer = Glib::signal_timeout().connect(
			sigc::mem_fun(*this, &MainWindow::pollAnimations), 200);
}

// Drop the on-screen preview and put the draft colours back, including
// anything held back by the coalescer — otherwise a frame from an
// effect that has stopped would land on the draft a moment later.
void MainWindow::clearPreviewState() {
	m_previewPending.clear();
	if (m_previewTimer.connected())
		m_previewTimer.disconnect();
	m_keyboardWidget.clearPreview();
}

// The keyboard gets every frame; the screen gets at most this many.
// Each on-screen frame repaints the whole board — the damage is the
// grid, whatever changed within it — and a colour fade is
// indistinguishable at 15 fps from one at 25.
static const gint64 previewIntervalUs = 66000;

void MainWindow::previewFrame(const LedKeyboard::KeyValueArray &values) {
	for (size_t i = 0; i < values.size(); ++i)
		m_previewPending[values[i].key] = values[i].color;
	// Minimised: the effect keeps driving the keyboard, but nothing is
	// drawn until the window is back, and then only the merged result.
	if (m_previewHidden)
		return;
	const gint64 now = g_get_monotonic_time();
	const gint64 waited = now - m_lastPreviewAt;
	if (waited >= previewIntervalUs) {
		flushPreview();
		return;
	}
	if (!m_previewTimer.connected())
		m_previewTimer = Glib::signal_timeout().connect(
			sigc::mem_fun(*this, &MainWindow::flushPreview),
			(unsigned)((previewIntervalUs - waited) / 1000) + 1);
}

bool MainWindow::flushPreview() {
	if (m_previewPending.empty())
		return false;   // a direct flush beat the timer to it
	LedKeyboard::KeyValueArray frame;
	frame.reserve(m_previewPending.size());
	for (std::map<LedKeyboard::Key, LedKeyboard::Color>::const_iterator it =
	     m_previewPending.begin(); it != m_previewPending.end(); ++it) {
		LedKeyboard::KeyValue keyValue;
		keyValue.key = it->first;
		keyValue.color = it->second;
		frame.push_back(keyValue);
	}
	m_previewPending.clear();
	m_lastPreviewAt = g_get_monotonic_time();
	m_keyboardWidget.applyPreviewFrame(frame);
	return false;
}

bool MainWindow::onWindowState(GdkEventWindowState *event) {
	const bool hidden = (event->new_window_state &
		(GDK_WINDOW_STATE_ICONIFIED | GDK_WINDOW_STATE_WITHDRAWN)) != 0;
	if (hidden != m_previewHidden) {
		m_previewHidden = hidden;
		if (!hidden)
			flushPreview();   // catch up in one repaint
	}
	return false;
}

void MainWindow::onLiveToggle() {
	if (rainActive()) {
		stopLive();
		return;
	}
	startSelectedLive();
}

// Picking a different mode while one runs switches to it: the lanes are
// mutually exclusive, so leaving the old one running would contradict
// what the chooser now says.
void MainWindow::onLiveModeChanged() {
	if (m_liveIgnore)
		return;
	const int mode = m_liveModeCombo.get_active_row_number();
	m_liveStack.set_visible_child(livePageFor(mode));
	// Except into a sensor. This panel promises, in words, on the page
	// itself, that Start is what opens the microphone or the screen —
	// "Start records from it until you press Stop". Carrying a running
	// effect over into one would open a capture off the back of a
	// dropdown, making that promise false. So the board is handed back
	// and the sensor waits for the press it said it would wait for.
	if (rainActive() && sensorLane(mode)) {
		const char *running = activeAnimationName();
		std::string name = running ? running : "Live effect";
		name[0] = (char)toupper((unsigned char)name[0]);
		stopRainEverywhere();
		const bool restored = restoreAppliedState();
		updateAnimUI();
		status(name + " stopped" +
			(restored ? ", and your colors are back" : "") +
			" — press Start to run " +
			m_liveModeCombo.get_active_text().raw());
		return;
	}
	// The chooser is the only record of what was just picked, and
	// updateAnimUI() overwrites it with whatever is running. So the new
	// lane has to be started from it before that happens: the other way
	// round, the chooser snapped back and the effect already on the board
	// was restarted, which is why picking a different effect while one ran
	// looked like it did nothing.
	if (rainActive())
		startSelectedLive();
	updateAnimUI();
}

void MainWindow::startSelectedLive() {
	switch (m_liveModeCombo.get_active_row_number()) {
		case 1: startAnimation(true); break;
		case 2: startWave(); break;
		case 3: startAudio(); break;
		case 4: startScreen(); break;
		default: startAnimation(false); break;
	}
}

void MainWindow::stopLive() {
	const char *running = activeAnimationName();
	std::string name = running ? running : "Live effect";
	name[0] = (char)toupper((unsigned char)name[0]);
	stopRainEverywhere();
	bool restored = restoreAppliedState();
	updateAnimUI();
	status(restored ? name + " stopped — your colors are back" :
		name + " stopped — the keyboard keeps the last frame");
}

void MainWindow::startAnimation(bool overlay) {
	m_expectedAnimation.clear();
	m_wavePlayer.stop();
	m_audioPlayer.stop();
	m_screenPlayer.stop();
	killAudioDaemon();
	killScreenDaemon();
	killRainDaemon();
	if (!ensureOpen())
		return;
	m_keyboardWidget.clearPreview();
	LedKeyboard::Color color = toLedColor(m_animColorButton.get_rgba());
	std::map<LedKeyboard::Key, LedKeyboard::Color> base;
	if (overlay)
		base = draftKeyColors();
	m_animOverlay = overlay;
	m_raindrop.start(m_keyboardWidget.getKeyPositions(), color,
		[this](const LedKeyboard::KeyValueArray &values) {
			return m_kbd.setKeys(values) && m_kbd.commit();
		}, dropIntervalMs(m_liveStack), base,
		[this](const LedKeyboard::KeyValueArray &values) {
			// Every player ticks on a GTK timer, so this arrives on the
			// GTK thread already — see the note on Preview in
			// gui/FramePlayer.h.
			previewFrame(values);
		});
	if (!m_raindrop.isRunning()) {
		m_keyboardWidget.clearPreview();
		status(overlay ? "Failed to start color rain" : "Failed to start raindrop");
		return;
	}
	writeRainState();
	noteAnimationStarted(overlay ? "Color rain" : "Raindrop");
	m_deviceShowsPreview = true;
	persistLastProfile();
	updateAnimUI();
	status(overlay ?
		"Color rain running — drops blend over the current scheme" :
		"Raindrop running — press Stop to end");
}

void MainWindow::updateAnimUI() {
	// Two independent questions per control: may this model/device do it
	// at all (the *Ok gates), and does something else own the board now.
	const bool anim = rainActive();
	const bool rain = raindropActive();
	const bool audio = audioActive();
	const bool screen = screenActive();
	const bool audioDaemon = audioDaemonAlive();
	const bool screenDaemon = screenDaemonAlive();
	const bool animOk = animControlsOk();
	const bool audioOk = audioControlsOk();
	const bool screenOk = screenControlsOk();
	const int mode = m_liveModeCombo.get_active_row_number();

	// Keep the chooser pointing at whatever actually owns the board, so
	// the settings page and the Stop button describe the same thing.
	if (anim) {
		int running = rain ? (m_animOverlay ? 1 : 0) :
			m_wavePlayer.isRunning() ? 2 : screen ? 4 : 3;
		if (running != mode) {
			m_liveIgnore = true;
			m_liveModeCombo.set_active(running);
			m_liveStack.set_visible_child(livePageFor(running));
			m_liveIgnore = false;
		}
	}
	m_liveToggleButton.set_label(anim ? "Sto_p" : "Sta_rt");
	// Sound-reactive and screen colors each need their own backend on
	// top of the per-key RGB the others want; a daemon inherited from an
	// earlier session must stay stoppable even with no device open here.
	const int selected = m_liveModeCombo.get_active_row_number();
	// "Stop" on its own does not say what stops, and a screen reader has
	// nothing else to go on. The spoken name carries the effect; the
	// visible label stays short.
	m_liveToggleButton.get_accessible()->set_name(
		(anim ? "Stop " : "Start ") + m_liveModeCombo.get_active_text());
	const bool modeOk = selected == 3 ? audioOk : selected == 4 ? screenOk : animOk;
	m_liveToggleButton.set_sensitive(modeOk || audioDaemon || screenDaemon);
	// A button that will not press is a dead end unless it says why, and
	// the reason differs per effect: a build without the sound or capture
	// library, no keyboard, or a keyboard with no per-key colour.
	m_liveToggleButton.set_tooltip_text(
		modeOk || audioDaemon || screenDaemon ?
			"Run the selected effect from this computer; stopping it "
			"restores your colors" :
		selected == 3 && !AudioCapture::available() ?
			"This build has no sound support — install the libpulse "
			"development files and rebuild" :
		selected == 4 && !ScreenCapture::available() ?
			"This build has no screen capture support — install the "
			"PipeWire development files and rebuild" :
		!m_controlsEnabled ?
			"No keyboard is connected — the Device tab can look again" :
			"This keyboard has no per-key color for an effect to drive");
	m_liveModeCombo.set_sensitive(animOk);
	// The drop colour and the drop speed are handed to the animation once,
	// at the start, so they grey while one runs — and a greyed control
	// that does not say why is a dead end, the same one the audio source
	// below used to be.
	m_animColorButton.set_sensitive(animOk && !rain);
	m_animColorButton.set_tooltip_text(rain ?
		"The drop color is fixed while it runs — press Stop to change it" :
		"The color of the drops");
	if (Gtk::Widget *speed = keptWidget(m_liveStack, dropSpeedKey)) {
		speed->set_sensitive(animOk && !rain);
		speed->set_tooltip_text(rain ?
			"The speed is fixed while it runs — press Stop to change it" :
			"How fast the drops fall and how often a new one starts");
	}

	// The sound lane's half of the sensor disclosure, at the head of the
	// panel: only for that lane, and only when the source is one that
	// listens to the room rather than to what this computer is playing.
	if (Gtk::Widget *sensorNote = keptWidget(m_liveStack, sensorNoteKey))
		sensorNote->set_visible(selected == 3 && !selectedSourceIsMonitor());

	// The one line under the chooser. Idle it says what the effect does;
	// running, what happens to it if the window goes away — with a tray
	// the window only hides and this process keeps driving the board,
	// without one the effect is handed to a detached process, except
	// screen colors, which is asked about rather than assumed. Running,
	// the board itself is showing what the effect does, so the closing
	// behaviour is the thing worth the line.
	if (Gtk::Label *note = keptLabel(m_liveStack, liveNoteKey))
		note->set_text(anim ? closingSentence(screen) :
			liveSummaryFor(selected));

	m_waveModeCombo.set_sensitive(animOk);
	m_waveShapeCombo.set_sensitive(animOk);
	m_wavePeriodScale.set_sensitive(animOk);
	m_waveMinScale.set_sensitive(animOk);
	m_waveMaxScale.set_sensitive(animOk);
	m_waveGraph.set_sensitive(animOk);

	// The source is bound at start; mode, colors and gain stay live —
	// but not once a detached daemon owns the effect, which cannot be
	// re-tuned without restarting it.
	m_audioSourceCombo.set_sensitive(audioOk && !audio);
	m_audioSourceCombo.set_tooltip_text(audio ?
		"What the effect listens to is fixed while it runs — press Stop to "
		"change it" :
		"An output monitor reacts to what you hear; an input reacts to the "
		"microphone");
	m_audioRefreshButton.set_sensitive(audioOk && !audio);
	// The meter is here only while there is something to meter. A detached
	// daemon owns its own stream and reports nothing back, so that counts
	// as nothing to meter too.
	if (Gtk::Widget *level = keptWidget(m_liveStack, levelRowKey))
		level->set_visible(m_audioPlayer.isRunning());
	m_audioModeCombo.set_sensitive(audioOk && !audioDaemon);
	m_audioLowColorButton.set_sensitive(audioOk && !audioDaemon);
	m_audioHighColorButton.set_sensitive(audioOk && !audioDaemon);
	m_audioGainScale.set_sensitive(audioOk && !audioDaemon);
	m_audioSmoothScale.set_sensitive(audioOk && !audioDaemon);
	m_audioAutoGainCheck.set_sensitive(audioOk && !audioDaemon);
	m_audioOverlayCheck.set_sensitive(audioOk && !audioDaemon);

	// Everything about the screen effect stays live except which screen
	// is captured, which the portal binds at start.
	m_screenModeCombo.set_sensitive(screenOk && !screenDaemon);
	m_screenBoostScale.set_sensitive(screenOk && !screenDaemon);
	m_screenSmoothScale.set_sensitive(screenOk && !screenDaemon);
	m_screenOverlayCheck.set_sensitive(screenOk && !screenDaemon);
	// The Forget button and the caption below it answer the same question
	// — is there a screen the desktop already remembers — so one place
	// sets both.
	updateScreenCaption();

	// Loading a profile would fight the animation for the device, and so
	// would the instant lanes: both are refused while one runs, so grey
	// them out rather than letting the user find out by pressing them.
	// Neither lane needs a device though — both work on the draft.
	m_loadItem.set_sensitive(!anim);
	m_loadTextItem.set_sensitive(!anim);
	// Saving only reads state — and it is the only way to capture a
	// running wave/audio effect into a profile, so it stays available.
	m_saveItem.set_sensitive(true);
	m_saveAsItem.set_sensitive(true);
	m_applyEffectButton.set_sensitive(m_controlsEnabled && !anim);
	m_effectOffButton.set_sensitive(m_controlsEnabled && !anim);
	m_applyGKeysButton.set_sensitive(m_controlsEnabled && !anim &&
		has(help::KeyboardFeatures::gkeys));
	m_applyStartupButton.set_sensitive(m_controlsEnabled && !anim &&
		startupModeSupported(m_kbd.getKeyboardModel()));
	m_applyOnBoardButton.set_sensitive(m_controlsEnabled && !anim &&
		has(help::KeyboardFeatures::onboardmode));

	updatePendingState();
}

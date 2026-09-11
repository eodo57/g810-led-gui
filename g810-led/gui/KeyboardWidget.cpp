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

#include "KeyboardWidget.h"

#include "Styling.h"

#include <cstdlib>
#include <cstring>

#include <algorithm>
#include <cmath>
#include <iostream>

#include "../src/helpers/utils.h"

// Windowless overlay that draws the rubber-band selection box.
// It must be windowless: a GtkDrawingArea creates its own GdkWindow by
// default, which would sit above the key boxes and swallow every
// pointer event over the keyboard area. With no window it draws on the
// parent's window, and Gtk::Overlay pass-through skips it during event
// picking, so hover/clicks/drags reach the keys.
class BandOverlay : public Gtk::Widget {
	public:
		BandOverlay() {
			set_has_window(false);
			set_can_focus(false);
		}
		void setBand(const Gdk::Rectangle &band) {
			m_hasBand = true;
			m_band = band;
			queue_draw();
		}
		void clearBand() {
			if (m_hasBand) {
				m_hasBand = false;
				queue_draw();
			}
		}
	protected:
		virtual bool on_draw(const Cairo::RefPtr<Cairo::Context> &context) override {
			if (!m_hasBand)
				return false;
			context->set_source_rgba(0.35, 0.7, 1.0, 0.25);
			context->rectangle(m_band.get_x(), m_band.get_y(),
			                   m_band.get_width(), m_band.get_height());
			context->fill_preserve();
			context->set_source_rgba(0.35, 0.7, 1.0, 0.9);
			context->set_line_width(1.5);
			context->set_dash(std::vector<double>{4.0, 4.0}, 0.0);
			context->stroke();
			return true;
		}
	private:
		bool m_hasBand = false;
		Gdk::Rectangle m_band;
};

namespace {

// Matches the border-radius of .kb-key in gui/Styling.h, so a drawn fill
// and a CSS-drawn one have the same silhouette.
const double keyRadius = 4.0;

// The gap between keycaps, in pixels at any board size. It is a constant
// rather than a fraction because a sub-pixel gap is not a gap.
const int capGap = 2;

// How far the keyboard's own plate stands out past the outermost keys,
// in quarter units — 7mm, which is what KeyboardScene extrudes and what
// a G512 measures. The flat board draws it for the same reason the model
// does: without it the view is a scatter of loose keycaps floating in a
// black well, and the thing the well is supposed to have in it is a
// keyboard. It also gives the lamps' names somewhere to be printed,
// which is where the real board prints them.
const double plateBezel = 7.0 / (19.05 / 4.0);

// The shape of the whole object — plate included — for a key grid this
// many quarter-columns across and this many rows deep. The stage is cut
// to it and the plate is drawn to it, so the well and the thing standing
// in it are the same shape.
double plateAspect(int columns, int rows) {
	const double wide = std::max(1, columns) + 2.0 * plateBezel;
	const double deep = 4.0 * std::max(1, rows) + 2.0 * plateBezel;
	return wide / deep;
}

// The board never lets itself be the reason a window cannot get smaller,
// so it asks for a size any pane can give. Below this it would not be a
// keyboard any more, and the scrolled window it sits in takes over.
const int stageMinWidth = 300;
const int stageMinHeight = 120;

// How much deeper the stage asks to be than the keyboard standing in it,
// per view.
//
// The flat board is exactly as deep as a keyboard seen from above, so its
// tray is cut to that and it stands on all of it. The model is drawn in
// perspective and tilted towards you, so its silhouette is nearly half as
// deep as it is wide; in a tray cut tight to the flat board it would have
// to stand back until that depth fit, which is what once left it barely
// half the width of the flat one.
//
// One depth for both was tried and measured: the model filled 81% of the
// tray and the grid 56%, so switching to the grid put it in a well half
// again deeper than it needed with a band of bare floor above and below.
//
// This is what the stage asks for, not what it gets: the pane it lives in
// hands it the whole column, because a tray cut to a keyboard leaves 270px
// of undressed pane under it and that reads worse than floor does. So the
// well is deeper than either board needs and both are letterboxed in it —
// the model to about two thirds of the depth, the flat board to under a
// half, because a keyboard seen from directly above is a thinner object
// than the same keyboard seen at an angle. Neither can be made to fill
// that depth without stretching a keycap out of square, which is why the
// flat board was given its plate instead: what stands in the well now is
// an object with a body rather than a scatter of loose keycaps.
const double stageDepth3D = 1.75;
const double stageDepthFlat = 1.0;

// What the model does under the mouse, said where the hand already is.
// The board carries this wherever the pointer is not on a key, so the
// gestures are found by the person who is looking at the board and
// wondering — rather than only by the one who thinks to hover a
// checkbox at the other end of the toolbar.
const char *const sceneGestures =
	"Drag with the right button to turn the board, scroll to zoom, "
	"middle-drag to slide it. Ctrl+Home, or Reset view, puts it back.";

// What the board answers to, said to whoever cannot see it. This is the
// only description either view carries, and it is written for the person
// who has just arrived here with the Tab key: it used to hand a screen
// reader three mouse gestures and nothing else, which told them how the
// board works for everyone but them.
const char *const boardHelp =
	"The keyboard, key by key. Arrow keys move between keys and say which "
	"one you are on; Return or space colors it; Shift and Return adds it "
	"to the selection; Delete turns it off; Menu lists what else can be "
	"done to it.";

// The legend on a keycap, as a fraction of the cap: a board drawn 500px
// wide and one drawn 960px wide are the same picture at two scales.
// Chosen so the longest legend a 1u cap carries ("PrtSc") fits inside it,
// and — for the indicator lamps, which are three quarters the width —
// so that "Light" does.
//
// The floor is the size below which lettering stops being lettering. It
// used to be six pixels, which drew every legend on a board squeezed
// into a narrow pane as an unreadable smudge — worse than nothing,
// because a smudge still has to be looked at before it can be dismissed.
// At eight, a legend that cannot be read is not drawn at all: the cap
// keeps its colour and its shape, and its name is a hover away.
const double legendScale = 0.30;
const double lampLegendScale = 0.72;
const int legendMinPx = 8;
const int legendMaxPx = 15;

void roundedRect(const Cairo::RefPtr<Cairo::Context> &cr,
                 double width, double height, double radius) {
	radius = std::min(radius, std::min(width, height) / 2.0);
	const double pi = 3.14159265358979323846;
	cr->begin_new_sub_path();
	cr->arc(width - radius, radius, radius, -pi / 2, 0);
	cr->arc(width - radius, height - radius, radius, 0, pi / 2);
	cr->arc(radius, height - radius, radius, pi / 2, pi);
	cr->arc(radius, radius, radius, pi, 3 * pi / 2);
	cr->close_path();
}

}  // namespace

KeyCap::KeyCap() : m_hasFill(false) {
}

void KeyCap::setFill(const Gdk::RGBA &color) {
	if (m_hasFill && m_fill.get_red() == color.get_red() &&
	    m_fill.get_green() == color.get_green() &&
	    m_fill.get_blue() == color.get_blue())
		return;
	m_fill = color;
	m_hasFill = true;
	queue_draw();
}

void KeyCap::clearFill() {
	if (!m_hasFill)
		return;
	m_hasFill = false;
	queue_draw();
}

void KeyCap::setCursor(bool on) {
	if (m_cursor == on)
		return;
	m_cursor = on;
	queue_draw();
}

void KeyCap::setLamp(bool lamp) {
	if (m_lamp == lamp)
		return;
	m_lamp = lamp;
	queue_draw();
}

// A lamp is not a keycap and this is where the flat board stops drawing
// it as one. On the keyboard it is a short lit slot set into the plate
// with its name printed on the plate above it — which is what the model
// draws, and what the flat board drew as a full-size red square
// indistinguishable from the Delete key beside it. Same object, both
// views.
//
// The proportions are the model's: a 6.0 x 4.6mm lens in a cell 14.3mm
// wide and a key pitch deep. It sits low in the cell rather than in the
// middle of it, because the room that leaves above is what the name is
// printed in, and a name is worth more than a millimetre of symmetry.
void KeyCap::lens(double width, double height, double &x, double &y,
                  double &lensWidth, double &lensHeight) {
	lensWidth = std::min(width, std::max(3.0, width * 0.62));
	lensHeight = std::min(height, std::max(3.0, height * 0.30));
	x = (width - lensWidth) / 2.0;
	y = height - lensHeight - std::min(2.0, (height - lensHeight) / 4.0);
}

// A keycap's own request is the size of a key, not the size of its
// legend: the legend is drawn when it fits and left off when it does
// not (see on_size_allocate), so it must never be what decides how wide
// the board is.
void KeyCap::get_preferred_width_vfunc(int &minimum, int &natural) const {
	int childMin = 0, childNatural = 0;
	Gtk::EventBox::get_preferred_width_vfunc(childMin, childNatural);
	minimum = capGap;
	natural = std::max(minimum, childNatural);
}

void KeyCap::get_preferred_height_vfunc(int &minimum, int &natural) const {
	int childMin = 0, childNatural = 0;
	Gtk::EventBox::get_preferred_height_vfunc(childMin, childNatural);
	minimum = capGap;
	natural = std::max(minimum, childNatural);
}

void KeyCap::on_size_allocate(Gtk::Allocation &allocation) {
	Gtk::EventBox::on_size_allocate(allocation);
	// A legend wider than the cap it names is worse than no legend: it
	// spills over its neighbours and none of them can be read. Asked
	// once per layout, not once per frame.
	m_legendFits = false;
	if (Gtk::Widget *child = get_child()) {
		// An event box hands its child the whole allocation, so the two
		// pixels the marks are banded in are room the legend does not
		// have.
		const int room = allocation.get_width() - 2 * capGap;
		int minWidth = 0, natWidth = 0, minHeight = 0, natHeight = 0;
		child->get_preferred_width(minWidth, natWidth);
		child->get_preferred_height(minHeight, natHeight);
		m_legendFits = natWidth <= room &&
			natHeight <= allocation.get_height();
	}
}

bool KeyCap::on_draw(const Cairo::RefPtr<Cairo::Context> &cr) {
	const Gtk::Allocation allocation = get_allocation();
	const double width = allocation.get_width();
	const double height = allocation.get_height();
	Glib::RefPtr<Gtk::StyleContext> style = get_style_context();

	// The lit face: the whole cell for a key, a slot near the foot of it
	// for a lamp. Everything drawn below is drawn on the face — the cell
	// stays the thing you can click, which is how the model behaves too.
	double x = 0.0, y = 0.0, face = width, deep = height;
	double radius = keyRadius;
	if (m_lamp) {
		lens(width, height, x, y, face, deep);
		radius = std::min(keyRadius, deep / 2.0);
	}
	cr->save();
	cr->translate(x, y);
	if (m_hasFill) {
		roundedRect(cr, face, deep, radius);
		cr->set_source_rgb(m_fill.get_red(), m_fill.get_green(),
		                   m_fill.get_blue());
		cr->fill();
	} else {
		// Never painted (a placeholder, or before the first paint):
		// .kb-key, .kb-led and :disabled still describe it.
		style->render_background(cr, 0, 0, face, deep);
	}
	// Anything the sheet still says about the cap's outline. The marks are
	// not in there any more (see drawMarks).
	style->render_frame(cr, 0, 0, face, deep);
	drawMarks(cr, style, face, deep, radius);
	cr->restore();
	// A lamp carries no word on the lens — there is none on the keyboard,
	// and at that size there is no room for one. Its name is printed on
	// the board above it by KeyPlate, where a word has the room.
	if (Gtk::Widget *child = get_child()) {
		if (m_legendFits && !m_lamp) {
			// Clipped to the cap even so: a fraction of a pixel of
			// rounding either way must not put ink on the key next door.
			cr->save();
			cr->rectangle(0, 0, width, height);
			cr->clip();
			propagate_draw(*child, cr);
			cr->restore();
		}
	}
	return true;
}

// What this cap is painted, which is what its marks have to be seen
// against. A painted cap knows; an unpainted one is whatever the sheet
// says a cap of its kind is, asked of the sheet rather than copied here
// so the two cannot drift.
Gdk::RGBA KeyCap::capColor() const {
	if (m_hasFill)
		return m_fill;
	Glib::RefPtr<Gtk::StyleContext> style =
		const_cast<KeyCap*>(this)->get_style_context();
	const char *name = "kb_cap";
	if (!is_sensitive())
		name = "kb_cap_off";
	else if (style->has_class("kb-led"))
		name = "kb_cap_led";
	Gdk::RGBA found;
	if (style->lookup_color(name, found))
		return found;
	return Gdk::RGBA("#2f343c");
}

// Selection, being pointed at, holding an unsent colour and standing
// under the arrow keys, drawn as bands running in from the edge of the
// cap — the same picture, in the same order, at the same widths, as the
// model draws in KeyboardScene's fragment shader. There is one rule for
// what a mark on this board looks like and both views obey it.
//
// The colours are the sheet's own @kb_amber and @kb_accent, moved by
// styling::markColor until they can be seen against this particular cap.
// They were CSS border colours until they were not: a border-color is a
// constant, and a board painted the ring's own colour swallowed the ring
// whole while the model's stayed visible. A rule in a stylesheet cannot
// know what colour the key under it is. This can.
//
// The order matters and is not arbitrary. Chosen is outermost because it
// is the one a whole row of keys shares and has to read as a group from
// across the room; unsent sits inside it; being pointed at gives way when
// both of the others are on, because the pointer is already there saying
// so. Where the arrow keys are standing never gives way — it is the one
// thing about a key that must not be hidden by what else is true of it.
void KeyCap::drawMarks(const Cairo::RefPtr<Cairo::Context> &cr,
                       const Glib::RefPtr<Gtk::StyleContext> &style,
                       double width, double height, double radius) {
	const bool selected = style->has_class("kb-selected");
	const bool pending = style->has_class("kb-pending");
	const bool hovered = (get_state_flags() & Gtk::STATE_FLAG_PRELIGHT) ==
		Gtk::STATE_FLAG_PRELIGHT;
	int slots = 0, atSelected = 0, atPending = 0;
	if (selected)
		atSelected = ++slots;
	if (pending)
		atPending = ++slots;
	if (m_cursor)
		++slots;
	else if (hovered && slots < 2)
		++slots;
	if (slots == 0)
		return;
	const double shortest = std::min(width, height);
	// Set in pixels rather than as a share of the cap: a share of a cap is
	// a hairline on a board seen whole, and that is the size these have to
	// be read at. Two bands share half again the width of one, and none of
	// them may eat more than a sixth of the key — the colour and the
	// letter are what the key is drawn for.
	double band = std::max(1.3, std::min(7.0, 0.07 * shortest + 0.7));
	if (slots > 1)
		band *= 0.7;
	band = std::min(band, shortest * 0.17);
	if (band <= 0.0)
		return;
	Gdk::RGBA amber("#f5c211"), accent("#66ccff"), plain("#ffffff");
	style->lookup_color("kb_amber", amber);
	style->lookup_color("kb_accent", accent);
	const Gdk::RGBA cap = capColor();
	for (int slot = 1; slot <= slots; ++slot) {
		const double inset = (slot - 1) * band;
		const double innerWidth = width - 2.0 * inset - band;
		const double innerHeight = height - 2.0 * inset - band;
		if (innerWidth <= 0.0 || innerHeight <= 0.0)
			break;
		Gdk::RGBA hue = plain;
		if (slot == atSelected)
			hue = amber;
		else if (slot == atPending)
			hue = accent;
		const Gdk::RGBA ink = styling::markColor(hue, cap);
		cr->save();
		cr->translate(inset + band / 2.0, inset + band / 2.0);
		roundedRect(cr, innerWidth, innerHeight,
			std::max(0.5, radius - inset - band / 2.0));
		cr->set_line_width(band);
		cr->set_source_rgb(ink.get_red(), ink.get_green(), ink.get_blue());
		cr->stroke();
		cr->restore();
	}
}

void KeyPlate::setBoard(int columns, int rows) {
	if (columns < 1 || rows < 1 || (columns == m_columns && rows == m_rows))
		return;
	m_columns = columns;
	m_rows = rows;
	queue_resize();
}

// The largest keyboard this stage can hold, plate and all, centred in it.
void KeyPlate::body(Gdk::Rectangle &plate, double &bezel) const {
	const Gtk::Allocation allocation =
		const_cast<KeyPlate*>(this)->get_allocation();
	const double aspect = plateAspect(m_columns, m_rows);
	const int room = 2 * capGap;
	const int width = std::max(1, allocation.get_width() - room);
	const int height = std::max(1, allocation.get_height() - room);
	int wide = width;
	int deep = (int)std::floor(width / aspect);
	if (deep > height) {
		deep = height;
		wide = (int)std::floor(height * aspect);
	}
	plate = Gdk::Rectangle((allocation.get_width() - wide) / 2,
	                       (allocation.get_height() - deep) / 2, wide, deep);
	bezel = wide * plateBezel / (m_columns + 2.0 * plateBezel);
}

void KeyPlate::on_size_allocate(Gtk::Allocation &allocation) {
	Gtk::EventBox::on_size_allocate(allocation);
	Gtk::Widget *child = get_child();
	if (!child || !child->get_visible())
		return;
	Gdk::Rectangle plate;
	double bezel = 0.0;
	body(plate, bezel);
	// An event box owns its window, so its child is placed from the
	// box's own top-left corner and not from the window's.
	const int inset = (int)std::floor(bezel);
	Gtk::Allocation inner(plate.get_x() + inset, plate.get_y() + inset,
	                      std::max(1, plate.get_width() - 2 * inset),
	                      std::max(1, plate.get_height() - 2 * inset));
	child->size_allocate(inner);
}

void KeyPlate::setLamps(const std::vector<Lamp> &lamps) {
	m_lamps = lamps;
	queue_draw();
}

int KeyPlate::fitAll(const std::vector<Glib::ustring> &words, double width,
                     double height) const {
	// Below eight pixels a word stops being a word and becomes a smudge
	// over the board, which says less than bare board does. Above
	// thirteen it would start competing with the legends on the keys.
	for (int px = 13; px >= 8; --px) {
		Pango::AttrList attributes;
		Pango::AttrInt size = Pango::Attribute::create_attr_size_absolute(
			px * PANGO_SCALE);
		attributes.insert(size);
		bool all = true;
		for (size_t i = 0; i < words.size() && all; ++i) {
			Glib::RefPtr<Pango::Layout> layout =
				const_cast<KeyPlate*>(this)->create_pango_layout(words[i]);
			layout->set_attributes(attributes);
			// The ink, not the line box. A line box carries the room a
			// descender would need whether the word has one or not, and
			// four of these five words are ascenders and capitals; judged
			// by the box they are a third taller than the mark they leave
			// on the plate, which is enough to lose them on a small board.
			const Pango::Rectangle ink = layout->get_pixel_ink_extents();
			all = ink.get_width() <= width && ink.get_height() <= height;
		}
		if (all)
			return px;
	}
	return 0;
}

// The lamps' names, printed on the board in the clear strip above each
// lens.
//
// Five coloured blocks in the corner of a keyboard are the one thing on
// this board nobody can name by looking at it, and two of them used to
// carry no word at all: "Game" and "Light" are wider than a lamp, so the
// legend on the lens dropped exactly those two and left three named
// lamps beside two anonymous ones — which does not read as "these two
// have no name", it reads as two lamps that failed to draw. They are
// named here instead, above the lens where the keyboard itself prints
// them, at whatever size fits all five. Together or not at all.
//
// When the board is drawn small enough that five words will not fit — a
// keyboard in a 980px window is half the size — the group takes one name
// rather than the members taking five. "Indicators" is what the button
// that paints them is called, so it is what they are called here.
bool KeyPlate::on_draw(const Cairo::RefPtr<Cairo::Context> &cr) {
	Glib::RefPtr<Gtk::StyleContext> style = get_style_context();
	const Gtk::Allocation allocation = get_allocation();
	style->render_background(cr, 0, 0, allocation.get_width(),
		allocation.get_height());
	// The keyboard's own plate, under the keys. Drawn here rather than by
	// letting the event box paint and then covering it, because the keys
	// have to be on top of it and the well has to be under it.
	Gdk::Rectangle plate;
	double bezel = 0.0;
	body(plate, bezel);
	Gdk::RGBA metal("#8b9099");
	style->lookup_color("kb_plate", metal);
	cr->save();
	cr->translate(plate.get_x(), plate.get_y());
	roundedRect(cr, plate.get_width(), plate.get_height(),
		std::max(2.0, bezel * 0.9));
	cr->set_source_rgb(metal.get_red(), metal.get_green(), metal.get_blue());
	cr->fill();
	cr->restore();
	style->render_frame(cr, 0, 0, allocation.get_width(),
		allocation.get_height());
	for (Gtk::Widget *child : get_children())
		if (child)
			propagate_draw(*child, cr);
	if (m_lamps.empty())
		return true;
	// Where the lamps are now, which is a question only the widgets can
	// answer: the board is re-laid-out on every resize.
	std::vector<Glib::ustring> words;
	std::vector<Gdk::Rectangle> strips;
	double left = 0.0, right = 0.0, top = 0.0, bottom = 0.0;
	for (size_t i = 0; i < m_lamps.size(); ++i) {
		if (!m_lamps[i].cap || !m_lamps[i].cap->get_visible())
			return true;
		const Gtk::Allocation cell = m_lamps[i].cap->get_allocation();
		double lensX = 0.0, lensY = 0.0, lensWidth = 0.0, lensHeight = 0.0;
		KeyCap::lens(cell.get_width(), cell.get_height(), lensX, lensY,
			lensWidth, lensHeight);
		// The full pitch, gaps included: a word is allowed the room
		// between one lamp and the next, which is what lets "Light" print
		// at a size the lens's own width would have refused.
		Gdk::Rectangle strip(cell.get_x() - capGap, cell.get_y(),
			cell.get_width() + 2 * capGap, (int)lensY - 1);
		if (strip.get_height() < 8)
			return true;
		if (i == 0) {
			left = strip.get_x();
			top = strip.get_y();
			bottom = strip.get_y() + strip.get_height();
		}
		right = strip.get_x() + strip.get_width();
		words.push_back(m_lamps[i].name);
		strips.push_back(strip);
	}
	double slot = strips[0].get_width();
	for (size_t i = 1; i < strips.size(); ++i)
		slot = std::min(slot, (double)strips[i].get_width());
	// A gutter, so five words in five slots read as five words. Without
	// it "Game" and "Light" fill their slots to the last pixel and print
	// as GameLight.
	int px = fitAll(words, slot - 4.0, bottom - top);
	if (px == 0) {
		words.assign(1, Glib::ustring("Indicators"));
		strips.assign(1, Gdk::Rectangle((int)left, (int)top,
			(int)(right - left), (int)(bottom - top)));
		px = fitAll(words, right - left, bottom - top);
		if (px == 0)
			return true;
	}
	// Printed in whichever of black or white can be read on the plate, by
	// the rule every legend in this window is coloured by. It is ink on a
	// keyboard, not a label on a control.
	const Gdk::RGBA ink(styling::contrastingText(metal));
	cr->save();
	cr->set_source_rgb(ink.get_red(), ink.get_green(), ink.get_blue());
	Pango::AttrList attributes;
	Pango::AttrInt size = Pango::Attribute::create_attr_size_absolute(
		px * PANGO_SCALE);
	attributes.insert(size);
	for (size_t i = 0; i < words.size(); ++i) {
		Glib::RefPtr<Pango::Layout> layout = create_pango_layout(words[i]);
		layout->set_attributes(attributes);
		// Centred on the ink for the same reason it was measured by it:
		// centring a line box puts a word with no descender high in its
		// strip, and these words sit between a lens and the key above.
		const Pango::Rectangle ink = layout->get_pixel_ink_extents();
		cr->move_to(strips[i].get_x() - ink.get_x() +
				(strips[i].get_width() - ink.get_width()) / 2.0,
			strips[i].get_y() - ink.get_y() +
				(strips[i].get_height() - ink.get_height()) / 2.0);
		layout->show_in_cairo_context(cr);
	}
	cr->restore();
	return true;
}

KeyboardWidget::KeyboardWidget() {
	set_hexpand(true);
	set_vexpand(true);
	m_grid.set_row_spacing(capGap);
	m_grid.set_column_spacing(capGap);
	m_grid.set_column_homogeneous(true);
	m_grid.set_row_homogeneous(true);
	m_grid.set_hexpand(true);
	m_grid.set_vexpand(true);
	m_grid.set_halign(Gtk::ALIGN_FILL);
	m_grid.set_valign(Gtk::ALIGN_FILL);
	// Legends are drawn at the size the keycaps came out, so they have to
	// be resized when the board is. The grid's allocation is where that
	// size is known.
	m_grid.signal_size_allocate().connect(
		[this](Gtk::Allocation &allocation) {
			fitLegends(allocation.get_width());
		});
	m_gridBox.add(m_grid);
	m_gridBox.add_events(Gdk::BUTTON_PRESS_MASK | Gdk::BUTTON_RELEASE_MASK |
	                     Gdk::POINTER_MOTION_MASK | Gdk::KEY_PRESS_MASK |
	                     Gdk::FOCUS_CHANGE_MASK);
	// A stop under Tab, like the model has. The individual keycaps are
	// not focusable — a hundred and eight of them would be a hundred and
	// eight stops between the tools and the tabs — so the board is one
	// stop, and unticking "3D board" must not be a way to lose it.
	m_gridBox.set_can_focus(true);
	// The same name the model carries: which of the two is drawing is not
	// something a screen reader should have to explain.
	if (Glib::RefPtr<Atk::Object> named = m_gridBox.get_accessible()) {
		named->set_name("Keyboard");
		named->set_description(boardHelp);
	}
	m_gridBox.signal_key_press_event().connect(
		sigc::mem_fun(*this, &KeyboardWidget::onBoardKeyPress));
	// The ring is drawn by the stage around the well, for both views: an
	// event box draws no focus of its own, and the ring has to be found
	// in the same place whichever view is up.
	m_focusWatch.push_back(m_gridBox.signal_focus_in_event().connect(
		[this](GdkEventFocus*) {
			onBoardFocus(true);
			return false;
		}));
	m_focusWatch.push_back(m_gridBox.signal_focus_out_event().connect(
		[this](GdkEventFocus*) {
			onBoardFocus(false);
			return false;
		}));

	// The board is drawn as a model unless that is impossible or the
	// user asked for the flat grid; the grid is built either way, so
	// falling back costs nothing and switching is immediate.
	if (getenv("G810_FORCE_2D") == NULL) {
		m_sceneWidget = Gtk::manage(new KeyboardScene());
		m_scene = m_sceneWidget;
		// A GL area is windowless, so button and scroll events would be
		// delivered to whatever owns the window underneath it and never
		// reach these handlers — the same reason the grid sits in an
		// event box. The scene fills the box, so their coordinates are
		// the same and nothing has to be translated.
		m_sceneBox.add(*m_sceneWidget);
		m_sceneBox.set_can_focus(true);
		m_sceneBox.set_tooltip_text(sceneGestures);
		// It takes focus, so it needs a name for anyone who cannot see
		// what they have just tabbed onto — and the same description the
		// grid carries, because which of the two happens to be drawing is
		// not a difference in what the board can be asked to do.
		if (Glib::RefPtr<Atk::Object> named = m_sceneBox.get_accessible()) {
			named->set_name("Keyboard");
			named->set_description(boardHelp);
		}
		m_sceneBox.add_events(Gdk::BUTTON_PRESS_MASK | Gdk::BUTTON_RELEASE_MASK |
		                      Gdk::POINTER_MOTION_MASK | Gdk::SCROLL_MASK |
		                      Gdk::SMOOTH_SCROLL_MASK | Gdk::KEY_PRESS_MASK |
		                      Gdk::LEAVE_NOTIFY_MASK | Gdk::FOCUS_CHANGE_MASK);
		// Tabbing onto the board changed nothing on screen: an event box
		// draws no focus of its own, so the one stop in this pane that
		// answers a keypress was also the one with nothing to show for
		// it. The ring is drawn by the stage around the well — a ring
		// drawn here would be underneath the model, which is blitted
		// over this widget's own drawing rather than into it.
		m_focusWatch.push_back(m_sceneBox.signal_focus_in_event().connect(
			[this](GdkEventFocus*) {
				onBoardFocus(true);
				return false;
			}));
		m_focusWatch.push_back(m_sceneBox.signal_focus_out_event().connect(
			[this](GdkEventFocus*) {
				onBoardFocus(false);
				return false;
			}));
		m_sceneBox.signal_button_press_event().connect(
			sigc::mem_fun(*this, &KeyboardWidget::onScenePress));
		m_sceneBox.signal_motion_notify_event().connect(
			sigc::mem_fun(*this, &KeyboardWidget::onSceneMotion));
		m_sceneBox.signal_button_release_event().connect(
			sigc::mem_fun(*this, &KeyboardWidget::onSceneRelease));
		m_sceneBox.signal_scroll_event().connect(
			sigc::mem_fun(*this, &KeyboardWidget::onSceneScroll));
		m_sceneBox.signal_key_press_event().connect(
			sigc::mem_fun(*this, &KeyboardWidget::onBoardKeyPress));
		m_sceneBox.signal_leave_notify_event().connect(
			[this](GdkEventCrossing*) {
				if (m_scene)
					m_scene->clearHover();
				m_hoverValid = false;
				return false;
			});
		m_sceneWidget->signal_ready().connect(
			sigc::mem_fun(*this, &KeyboardWidget::onSceneReady));
		m_use3D = true;
		add(m_sceneBox);
	} else {
		add(m_gridBox);
	}

	m_bandOverlay = Gtk::manage(new BandOverlay());
	add_overlay(*m_bandOverlay);
	set_overlay_pass_through(*m_bandOverlay, true);

	// The board reads as a dark surface, but derived from the theme so it
	// sits correctly in both light and dark (see .kb-board in main.cpp).
	get_style_context()->add_class("kb-board");

	m_gridBox.signal_button_press_event().connect(
		sigc::mem_fun(*this, &KeyboardWidget::onGridButtonPress));
	m_gridBox.signal_motion_notify_event().connect(
		sigc::mem_fun(*this, &KeyboardWidget::onGridButtonMotion));
	m_gridBox.signal_button_release_event().connect(
		sigc::mem_fun(*this, &KeyboardWidget::onGridButtonRelease));
	add_events(Gdk::BUTTON_PRESS_MASK | Gdk::BUTTON_RELEASE_MASK |
	           Gdk::POINTER_MOTION_MASK);
	signal_button_press_event().connect(
		sigc::mem_fun(*this, &KeyboardWidget::onOverlayPress));
	signal_motion_notify_event().connect(
		sigc::mem_fun(*this, &KeyboardWidget::onOverlayMotion));
	signal_button_release_event().connect(
		sigc::mem_fun(*this, &KeyboardWidget::onOverlayRelease));

	rebuildLayout();
	show_all_children();
}

KeyboardWidget::~KeyboardWidget() {
	// Not endDrag(): that clears the band overlay, and this widget is a
	// by-value member of a stack-allocated window, so GTK has already
	// destroyed (and gtkmm deleted) the managed overlay child by the
	// time this runs. Releasing the pointer grab is all that is left.
	ungrabDrag();
	// Giving up the focus is one of the things that happens on the way
	// out, and it happens while the scene is being destroyed — which is
	// after the signal the handler would raise. Cut the wire here, in a
	// destructor body, which is the last moment at which every member is
	// still whole.
	for (sigc::connection &watch : m_focusWatch)
		watch.disconnect();
}

void KeyboardWidget::setLayoutFlags(const LayoutFlags &flags) {
	bool changed = m_buttons.empty() ||
		flags.setkey != m_flags.setkey ||
		flags.logo1 != m_flags.logo1 || flags.logo2 != m_flags.logo2 ||
		flags.multimedia != m_flags.multimedia ||
		flags.gkeyCount != m_flags.gkeyCount ||
		flags.numpad != m_flags.numpad ||
		flags.dropsIndicatorsAndStop != m_flags.dropsIndicatorsAndStop ||
		flags.setindicators != m_flags.setindicators;
	// Store first: rebuildLayout() reads m_flags.
	m_flags = flags;
	if (changed)
		rebuildLayout();
}

const KeyboardWidget::LayoutFlags &KeyboardWidget::getLayoutFlags() const {
	return m_flags;
}

const std::map<LedKeyboard::Key, KeyboardWidget::KeyPosition>&
KeyboardWidget::getKeyPositions() const {
	return m_keyPositions;
}

// The layout of the connected model, in quarter key units: the single
// description of what keys exist and where, shared by the widget grid
// and by the 3D scene, which extrudes these same rectangles.
std::vector<KeyboardWidget::KeyRow> KeyboardWidget::buildLayoutRows() const {
	// 94 homogeneous columns = quarter-key units (1u = 4 columns):
	// 4 for the leading G-key slot + the 90-column G512 base layout.
	// gkeys models add a second 4-col strip (g6–g9) for 98 columns.
	// Key names are the CLI's canonical names, resolved with
	// utils::parseKey so the vocabulary can never drift from the CLI.
	std::vector<KeyRow> rows;
	// Numpad keys only exist on numpad models (g410/gpro are TKL).
	auto np = [this](const char *name, const char *label, int width,
	                 int height = 1) -> KeySpec {
		if (!m_flags.numpad)
			return {"", "", width, height, false};
		return {name, label, width, height, false};
	};

	// The indicator lamps belong in the function row, level with the
	// logo, which is where they are on the keyboard. That leaves this
	// row for the logo alone, so on a board without one it is not built
	// at all — an empty row would make the model a whole key pitch
	// deeper than the thing it is a picture of.
	const bool lampsInFunctionRow = m_flags.setkey && m_flags.setindicators;
	if (m_flags.logo1 || m_flags.logo2 || !m_flags.setkey) {
		KeyRow row;
		row.keys.push_back({"", "", 4, 1, false});
		if (m_flags.logo2) {
			row.keys.push_back({"", "", 40, 1, false});
			row.keys.push_back({"logo", "G", 4, 1, false});
			row.keys.push_back({"logo2", "G2", 4, 1, false});
			row.keys.push_back({"", "", 22, 1, false});
		} else if (m_flags.logo1) {
			row.keys.push_back({"", "", 40, 1, false});
			row.keys.push_back({"logo", "G", 8, 1, false});
			row.keys.push_back({"", "", 22, 1, false});
		} else {
			row.keys.push_back({"", "", 70, 1, false});
		}
		// Boards with no key grid (g213, g413) have no function row to
		// put them in, so they keep them here.
		if (m_flags.setindicators && !lampsInFunctionRow) {
			row.keys.push_back({"caps", "Caps", 3, 1, false});
			row.keys.push_back({"num", "Num", 3, 1, false});
			row.keys.push_back({"scroll", "Scrl", 3, 1, false});
			row.keys.push_back({"game", "Game", 3, 1, false});
			row.keys.push_back({"backlight", "Light", 3, 1, false});
			row.keys.push_back({"", "", 5, 1, false});
		} else {
			row.keys.push_back({"", "", 20, 1, false});
		}
		rows.push_back(row);
	}

	// Row 1 (media models only): media cluster above the numpad.
	if (m_flags.multimedia) {
		KeyRow row;
		row.keys.push_back({"", "", 4, 1, false});
		row.keys.push_back({"", "", 58, 1, false});
		row.keys.push_back({"next", "Next", 4, 1, false});
		row.keys.push_back({"prev", "Prev", 4, 1, false});
		row.keys.push_back({"stop", "Stop", 4, 1, false});
		row.keys.push_back({"play", "Play", 4, 1, false});
		row.keys.push_back({"mute", "Mute", 4, 1, false});
		row.keys.push_back({"", "", 12, 1, false});
		rows.push_back(row);
	}

	if (m_flags.setkey) {
		// Function row: Esc | gap | F1-F4 | gap | F5-F8 | gap | F9-F12 |
		// gap | PrtSc ScrLk Pause | gap | indicator lamps. The numpad
		// does not start until the number row (see the photograph:
		// NumLk sits beside the 1 key, not beside F9).
		//
		// The three gaps between the F-key groups are wider than one
		// quarter unit, which is what carries PrtSc, ScrLk and Pause out
		// to columns 65, 69 and 73 — the same columns as Ins, Home and
		// PgUp below them. They line up on the keyboard, so they line up
		// here: 8 + 3 + 16 + 3 + 16 + 2 + 16 + 1 = 65.
		KeyRow fRow;
		fRow.keys.push_back({"", "", 4, 1, false});
		fRow.keys.push_back({"esc", "Esc", 4, 1, false});
		fRow.keys.push_back({"", "", 3, 1, false});
		fRow.keys.push_back({"f1", "F1", 4, 1, false});
		fRow.keys.push_back({"f2", "F2", 4, 1, false});
		fRow.keys.push_back({"f3", "F3", 4, 1, false});
		fRow.keys.push_back({"f4", "F4", 4, 1, false});
		fRow.keys.push_back({"", "", 3, 1, false});
		fRow.keys.push_back({"f5", "F5", 4, 1, false});
		fRow.keys.push_back({"f6", "F6", 4, 1, false});
		fRow.keys.push_back({"f7", "F7", 4, 1, false});
		fRow.keys.push_back({"f8", "F8", 4, 1, false});
		fRow.keys.push_back({"", "", 2, 1, false});
		fRow.keys.push_back({"f9", "F9", 4, 1, false});
		fRow.keys.push_back({"f10", "F10", 4, 1, false});
		fRow.keys.push_back({"f11", "F11", 4, 1, false});
		fRow.keys.push_back({"f12", "F12", 4, 1, false});
		fRow.keys.push_back({"", "", 1, 1, false});
		fRow.keys.push_back({"print_screen", "PrtSc", 4, 1, false});
		fRow.keys.push_back({"scroll_lock", "ScrLk", 4, 1, false});
		fRow.keys.push_back({"pause_break", "Pause", 4, 1, false});
		fRow.keys.push_back({"", "", 2, 1, false});
		if (lampsInFunctionRow) {
			// Close together, in the corner above the numpad, which is
			// where they are on the board.
			fRow.keys.push_back({"caps", "Caps", 3, 1, false});
			fRow.keys.push_back({"num", "Num", 3, 1, false});
			fRow.keys.push_back({"scroll", "Scrl", 3, 1, false});
			fRow.keys.push_back({"game", "Game", 3, 1, false});
			fRow.keys.push_back({"backlight", "Light", 3, 1, false});
		} else {
			fRow.keys.push_back({"", "", 15, 1, false});
		}
		rows.push_back(fRow);

		// Main row 1
		KeyRow r1;
		r1.keys.push_back({"", "", 4, 1, false});
		r1.keys.push_back({"tilde", "`", 4, 1, false});
		r1.keys.push_back({"1", "1", 4, 1, false});
		r1.keys.push_back({"2", "2", 4, 1, false});
		r1.keys.push_back({"3", "3", 4, 1, false});
		r1.keys.push_back({"4", "4", 4, 1, false});
		r1.keys.push_back({"5", "5", 4, 1, false});
		r1.keys.push_back({"6", "6", 4, 1, false});
		r1.keys.push_back({"7", "7", 4, 1, false});
		r1.keys.push_back({"8", "8", 4, 1, false});
		r1.keys.push_back({"9", "9", 4, 1, false});
		r1.keys.push_back({"0", "0", 4, 1, false});
		r1.keys.push_back({"minus", "-", 4, 1, false});
		r1.keys.push_back({"equal", "=", 4, 1, false});
		r1.keys.push_back({"backspace", "⌫", 8, 1, false});
		r1.keys.push_back({"", "", 1, 1, false});
		r1.keys.push_back({"insert", "Ins", 4, 1, false});
		r1.keys.push_back({"home", "Home", 4, 1, false});
		r1.keys.push_back({"page_up", "PgUp", 4, 1, false});
		r1.keys.push_back({"", "", 1, 1, false});
		r1.keys.push_back(np("num_lock", "NumLk", 4));
		r1.keys.push_back(np("num_slash", "/", 4));
		r1.keys.push_back(np("num_asterisk", "*", 4));
		r1.keys.push_back(np("num_minus", "-", 4));
		// Where the G-key column attaches, whether or not the rows above
		// this one exist on this model.
		const size_t numberRowIndex = rows.size();
		rows.push_back(r1);

		// Main row 2
		KeyRow r2;
		r2.keys.push_back({"", "", 4, 1, false});
		r2.keys.push_back({"tab", "Tab", 6, 1, false});
		r2.keys.push_back({"q", "Q", 4, 1, false});
		r2.keys.push_back({"w", "W", 4, 1, false});
		r2.keys.push_back({"e", "E", 4, 1, false});
		r2.keys.push_back({"r", "R", 4, 1, false});
		r2.keys.push_back({"t", "T", 4, 1, false});
		r2.keys.push_back({"y", "Y", 4, 1, false});
		r2.keys.push_back({"u", "U", 4, 1, false});
		r2.keys.push_back({"i", "I", 4, 1, false});
		r2.keys.push_back({"o", "O", 4, 1, false});
		r2.keys.push_back({"p", "P", 4, 1, false});
		r2.keys.push_back({"open_bracket", "[", 4, 1, false});
		r2.keys.push_back({"close_bracket", "]", 4, 1, false});
		r2.keys.push_back({"backslash", "\\", 6, 1, false});
		r2.keys.push_back({"", "", 1, 1, false});
		r2.keys.push_back({"del", "Del", 4, 1, false});
		r2.keys.push_back({"end", "End", 4, 1, false});
		r2.keys.push_back({"page_down", "PgDn", 4, 1, false});
		r2.keys.push_back({"", "", 1, 1, false});
		r2.keys.push_back(np("num7", "7", 4));
		r2.keys.push_back(np("num8", "8", 4));
		r2.keys.push_back(np("num9", "9", 4));
		r2.keys.push_back(np("num_plus", "+", 4, 2));
		rows.push_back(r2);

		// Main row 3
		KeyRow r3;
		r3.keys.push_back({"", "", 4, 1, false});
		r3.keys.push_back({"caps_lock", "Caps", 7, 1, false});
		r3.keys.push_back({"a", "A", 4, 1, false});
		r3.keys.push_back({"s", "S", 4, 1, false});
		r3.keys.push_back({"d", "D", 4, 1, false});
		r3.keys.push_back({"f", "F", 4, 1, false});
		r3.keys.push_back({"g", "G", 4, 1, false});
		r3.keys.push_back({"h", "H", 4, 1, false});
		r3.keys.push_back({"j", "J", 4, 1, false});
		r3.keys.push_back({"k", "K", 4, 1, false});
		r3.keys.push_back({"l", "L", 4, 1, false});
		r3.keys.push_back({"semicolon", ";", 4, 1, false});
		r3.keys.push_back({"quote", "'", 4, 1, false});
		r3.keys.push_back({"enter", "Enter", 9, 1, false});
		r3.keys.push_back({"", "", 1, 1, false});
		r3.keys.push_back({"", "", 12, 1, false});
		r3.keys.push_back({"", "", 1, 1, false});
		r3.keys.push_back(np("num4", "4", 4));
		r3.keys.push_back(np("num5", "5", 4));
		r3.keys.push_back(np("num6", "6", 4));
		r3.keys.push_back({"", "", 4, 1, false});   // num_plus spans down
		rows.push_back(r3);

		// Main row 4
		KeyRow r4;
		r4.keys.push_back({"", "", 4, 1, false});
		r4.keys.push_back({"shift_left", "Shift", 5, 1, false});
		r4.keys.push_back({"intl_backslash", "<>", 4, 1, false});
		r4.keys.push_back({"z", "Z", 4, 1, false});
		r4.keys.push_back({"x", "X", 4, 1, false});
		r4.keys.push_back({"c", "C", 4, 1, false});
		r4.keys.push_back({"v", "V", 4, 1, false});
		r4.keys.push_back({"b", "B", 4, 1, false});
		r4.keys.push_back({"n", "N", 4, 1, false});
		r4.keys.push_back({"m", "M", 4, 1, false});
		r4.keys.push_back({"comma", ",", 4, 1, false});
		r4.keys.push_back({"period", ".", 4, 1, false});
		r4.keys.push_back({"slash", "/", 4, 1, false});
		r4.keys.push_back({"shift_right", "Shift", 11, 1, false});
		r4.keys.push_back({"", "", 1, 1, false});
		r4.keys.push_back({"", "", 4, 1, false});
		r4.keys.push_back({"arrow_top", "↑", 4, 1, false});
		r4.keys.push_back({"", "", 4, 1, false});
		r4.keys.push_back({"", "", 1, 1, false});
		r4.keys.push_back(np("num1", "1", 4));
		r4.keys.push_back(np("num2", "2", 4));
		r4.keys.push_back(np("num3", "3", 4));
		r4.keys.push_back(np("numenter", "Enter", 4, 2));
		rows.push_back(r4);

		// Main row 5: Logitech bottom row (Fn replaces Menu on G512/G513,
		// standard Menu key elsewhere)
		KeyRow r5;
		r5.keys.push_back({"", "", 4, 1, false});
		r5.keys.push_back({"ctrl_left", "Ctrl", 5, 1, false});
		r5.keys.push_back({"win_left", "Win", 5, 1, false});
		r5.keys.push_back({"alt_left", "Alt", 5, 1, false});
		r5.keys.push_back({"space", "", 25, 1, false});
		r5.keys.push_back({"alt_right", "Alt", 5, 1, false});
		r5.keys.push_back({"win_right", "Win", 5, 1, false});
		r5.keys.push_back({"menu", "Menu", 5, 1, false});
		r5.keys.push_back({"ctrl_right", "Ctrl", 5, 1, false});
		r5.keys.push_back({"", "", 1, 1, false});
		r5.keys.push_back({"arrow_left", "←", 4, 1, false});
		r5.keys.push_back({"arrow_bottom", "↓", 4, 1, false});
		r5.keys.push_back({"arrow_right", "→", 4, 1, false});
		r5.keys.push_back({"", "", 1, 1, false});
		r5.keys.push_back(np("num0", "0", 8));
		r5.keys.push_back(np("num_period", ".", 4));
		r5.keys.push_back({"", "", 4, 1, false});   // numenter spans down
		rows.push_back(r5);

		// G-key columns: g1–g5 in the original slot, and on the g910
		// g6–g9 in an extra leading strip (layout width 94 → 98). The
		// g815 shares the gkeys feature bit but only has five, and its
		// firmware ignores g6–g9 entirely.
		if (m_flags.gkeyCount > 0) {
			for (KeyRow &row : rows)
				row.keys.insert(row.keys.begin(), {"", "", 4, 1, false});
			const char *gLeft[5] = {"g1", "g2", "g3", "g4", "g5"};
			const char *gExtra[4] = {"g6", "g7", "g8", "g9"};
			const int extra = std::max(0, std::min(4, m_flags.gkeyCount - 5));
			const size_t mainIndex = numberRowIndex;
			for (size_t i = 0; i < rows.size(); ++i) {
				if (i >= mainIndex && i - mainIndex < 5)
					rows[i].keys[1] = {gLeft[i - mainIndex],
					                   gLeft[i - mainIndex], 4, 1, false};
				if (i >= mainIndex && (int)(i - mainIndex) < extra)
					rows[i].keys[0] = {gExtra[i - mainIndex],
					                   gExtra[i - mainIndex], 4, 1, false};
			}
		}
	}

	// LedKeyboard::setKeys drops these on the g815, so an Apply that
	// "succeeds" would never reach them; draw them as placeholders
	// instead of letting the draft and the board disagree forever.
	static const char *ignoredByFirmware[] = {
		"caps", "num", "scroll", "game", "stop"
	};
	if (m_flags.dropsIndicatorsAndStop) {
		for (KeyRow &row : rows) {
			for (KeySpec &spec : row.keys) {
				for (const char *name : ignoredByFirmware) {
					if (std::strcmp(spec.name, name) == 0) {
						spec.placeholder = true;
						spec.name = "";
					}
				}
			}
		}
	}

	return rows;
}

// The layout resolved to placed caps: names turned into key ids,
// columns accumulated, spacers dropped. The grid attaches widgets from
// this and the scene extrudes it, so the two views cannot describe
// different keyboards.
std::vector<keylayout::Cap> KeyboardWidget::layoutCaps() const {
	std::vector<keylayout::Cap> caps;
	const std::vector<KeyRow> rows = buildLayoutRows();
	int rowIndex = 0;
	for (const KeyRow &row : rows) {
		int colIndex = 0;
		for (const KeySpec &spec : row.keys) {
			if (spec.name[0] == '\0' && !spec.placeholder) {
				colIndex += spec.width;
				continue;
			}
			keylayout::Cap cap;
			cap.label = spec.label;
			cap.name = spec.name;
			cap.row = rowIndex;
			cap.column = colIndex;
			cap.width = spec.width;
			cap.height = spec.height;
			cap.controllable = false;
			if (!spec.placeholder) {
				LedKeyboard::Key key;
				if (!utils::parseKey(spec.name, key)) {
					std::cerr << "g810-led-gui: unknown key name in layout: "
					          << spec.name << std::endl;
					colIndex += spec.width;
					continue;
				}
				cap.key = key;
				cap.controllable = true;
				cap.indicator = key == LedKeyboard::Key::caps ||
					key == LedKeyboard::Key::num ||
					key == LedKeyboard::Key::scroll ||
					key == LedKeyboard::Key::game ||
					key == LedKeyboard::Key::backlight;
			}
			caps.push_back(cap);
			colIndex += spec.width;
		}
		rowIndex++;
	}
	return caps;
}

void KeyboardWidget::rebuildLayout() {
	endDrag();
	for (Gtk::Widget *child : m_grid.get_children())
		m_grid.remove(*child);
	m_buttons.clear();
	m_keyStyles.clear();
	// Paired with m_keyStyles: leaving it behind would convince paintKey
	// that the new (empty) provider already carries the right colour.
	m_keyTextColor.clear();
	m_colors.clear();
	m_preview.clear();
	m_keyPositions.clear();
	m_keyNames.clear();
	m_selected.clear();
	// A different keyboard is a different set of keys, and a cursor on a
	// key the new board does not have would answer no arrow press.
	m_cursorValid = false;
	m_keyLabels.clear();
	m_ledLabels.clear();
	m_gridBox.setLamps(std::vector<KeyPlate::Lamp>());
	m_legendPx = 0;

	const std::vector<keylayout::Cap> caps = layoutCaps();
	// The shape of this particular keyboard: a TKL has no numpad and a
	// board with no logo has one row fewer, and both have to look like
	// the thing on the desk rather than like a G512 with holes in it.
	// Measured between the outermost keycaps, because that is the span a
	// grid lays out — a leading spacer column with nothing in it is not
	// a column at all.
	int firstColumn = 0, lastColumn = 1, firstRow = 0, lastRow = 1;
	if (!caps.empty()) {
		firstColumn = lastColumn = caps[0].column;
		firstRow = lastRow = caps[0].row;
		for (const keylayout::Cap &cap : caps) {
			firstColumn = std::min(firstColumn, cap.column);
			firstRow = std::min(firstRow, cap.row);
			lastColumn = std::max(lastColumn, cap.column + cap.width);
			lastRow = std::max(lastRow, cap.row + cap.height);
		}
	}
	m_layoutColumns = std::max(1, lastColumn - firstColumn);
	m_layoutRows = std::max(1, lastRow - firstRow);
	m_gridBox.setBoard(m_layoutColumns, m_layoutRows);
	std::vector<KeyPlate::Lamp> lamps;
	for (size_t i = 0; i < caps.size(); ++i) {
		const keylayout::Cap &cap = caps[i];
		// Keys are EventBox+Label, not Gtk::Button: GtkButton consumes
		// primary-button presses internally (gesture + class closure
		// returning TRUE), and GTK3's boolean-handled accumulator stops
		// the whole signal emission at the first TRUE handler, so our
		// press handlers would never run on a Button. EventBox has no
		// internal press handling.
		KeyCap *keyBox = Gtk::manage(new KeyCap());
		Gtk::Label *label = Gtk::manage(new Gtk::Label(cap.label));
		label->set_xalign(0.5);
		label->set_yalign(0.5);
		(cap.indicator ? m_ledLabels : m_keyLabels).push_back(label);
		keyBox->setLamp(cap.indicator);
		keyBox->add(*label);
		keyBox->set_can_focus(false);
		keyBox->set_hexpand(true);
		keyBox->set_vexpand(true);
		label->set_hexpand(true);
		label->set_vexpand(true);
		applyBaseStyles(keyBox, cap.indicator);
		keyBox->add_events(Gdk::BUTTON_PRESS_MASK |
		                   Gdk::BUTTON_RELEASE_MASK |
		                   Gdk::POINTER_MOTION_MASK |
		                   Gdk::ENTER_NOTIFY_MASK |
		                   Gdk::LEAVE_NOTIFY_MASK);
		if (!cap.controllable) {
			keyBox->set_sensitive(false);
			keyBox->set_tooltip_text("not controllable");
		} else {
			const LedKeyboard::Key key = cap.key;
			keyBox->set_tooltip_text(cap.name);
			keyBox->signal_button_press_event().connect(sigc::bind(
				sigc::mem_fun(*this, &KeyboardWidget::onKeyButtonPress), key));
			keyBox->signal_motion_notify_event().connect(sigc::bind(
				sigc::mem_fun(*this, &KeyboardWidget::onKeyButtonMotion), key));
			keyBox->signal_button_release_event().connect(sigc::bind(
				sigc::mem_fun(*this, &KeyboardWidget::onKeyButtonRelease), key));
			m_buttons[key] = keyBox;
			m_colors[key] = Gdk::RGBA("#000000");
			m_keyPositions[key] = KeyPosition(cap.row, cap.column + cap.width / 2);
			m_keyNames[key] = cap.name;
			// base black from CSS; override only on paint
		}
		m_grid.attach(*keyBox, cap.column, cap.row, cap.width, cap.height);
		if (cap.indicator) {
			KeyPlate::Lamp lamp;
			lamp.cap = keyBox;
			lamp.name = cap.label;
			lamps.push_back(lamp);
		}
	}
	m_gridBox.setLamps(lamps);
	if (m_scene)
		m_scene->setCaps(caps);
	show_all_children();
}

void KeyboardWidget::applyBaseStyles(Gtk::EventBox *box, bool isLed) {
	box->get_style_context()->add_class("kb-key");
	if (isLed)
		box->get_style_context()->add_class("kb-led");
}

// Columns are quarter units, rows are whole ones, so a keyboard 94
// columns across and 6 rows deep is 23.5 keys by 6 — and the plate stands
// 7mm out past the outermost of them on every side, as the real one does.
// This is the shape of the whole object, because it is the shape the
// stage has to be cut to for the object to stand in it.
double KeyboardWidget::boardAspect() const {
	return plateAspect(m_layoutColumns, m_layoutRows);
}

// The legend size follows the keycap size, so the board is one picture
// at any scale instead of small keys with big lettering.
void KeyboardWidget::fitLegends(int gridWidth) {
	const double column =
		(gridWidth - (m_layoutColumns - 1) * capGap) / (double)m_layoutColumns;
	const double unit = column * 4.0 + 3.0 * capGap;   // one 1u keycap
	int px = (int)std::floor(unit * legendScale);
	px = std::max(legendMinPx, std::min(legendMaxPx, px));
	if (px == m_legendPx)
		return;
	m_legendPx = px;
	// Pango attributes rather than CSS: a style provider per keycap has
	// to be parsed, and there are a hundred of them.
	Pango::AttrList letters;
	Pango::AttrInt size = Pango::Attribute::create_attr_size_absolute(
		px * PANGO_SCALE);
	letters.insert(size);
	Pango::AttrList lamps;
	Pango::AttrInt small = Pango::Attribute::create_attr_size_absolute(
		std::max(legendMinPx, (int)std::floor(px * lampLegendScale)) *
		PANGO_SCALE);
	lamps.insert(small);
	for (Gtk::Label *label : m_keyLabels)
		label->set_attributes(letters);
	// An indicator lamp is three quarters the width of a key and carries
	// words ("Scrl", "Light"), so its lettering is scaled to the cap it
	// is on rather than to a keycap — otherwise the two longest of the
	// five are dropped for not fitting, and a lamp with no legend is a
	// square nobody can name.
	for (Gtk::Label *label : m_ledLabels)
		label->set_attributes(lamps);
}

// The board takes the whole stage it is given, and asks for no more than
// any pane can spare.
//
// Both numbers are a floor, not a wish: whatever the widget asks for as
// its natural size is what a viewport hands it, even when the viewport
// is smaller than that, so a board that asked for the room its legends
// wanted was a board with its numeric keypad off the edge of the window.
// What each view does with the room is its own business — the plate
// holds the grid to the keyboard's proportions and the model fits itself
// to the frustum — and because both fill the same rectangle, switching
// between them moves nothing.
void KeyboardWidget::get_preferred_width_vfunc(int &minimum,
                                               int &natural) const {
	minimum = natural = stageMinWidth;
}

void KeyboardWidget::get_preferred_height_vfunc(int &minimum,
                                                int &natural) const {
	minimum = natural = stageMinHeight;
}

KeyboardWidget::type_signal_key KeyboardWidget::signal_key_pressed() {
	return m_signal_key_pressed;
}

KeyboardWidget::type_signal_key KeyboardWidget::signal_key_pick() {
	return m_signal_key_pick;
}

KeyboardWidget::type_signal_key KeyboardWidget::signal_key_cleared() {
	return m_signal_key_cleared;
}

KeyboardWidget::type_signal_key KeyboardWidget::signal_key_menu() {
	return m_signal_key_menu;
}

void KeyboardWidget::selectRowOf(LedKeyboard::Key key) {
	std::map<LedKeyboard::Key, KeyPosition>::const_iterator at =
		m_keyPositions.find(key);
	if (at == m_keyPositions.end())
		return;
	std::set<LedKeyboard::Key> selection;
	for (std::map<LedKeyboard::Key, KeyPosition>::const_iterator it =
	     m_keyPositions.begin(); it != m_keyPositions.end(); ++it) {
		if (it->second.first == at->second.first)
			selection.insert(it->first);
	}
	setSelection(selection);
}

void KeyboardWidget::selectSameColorAs(LedKeyboard::Key key) {
	std::map<LedKeyboard::Key, Gdk::RGBA>::const_iterator at = m_colors.find(key);
	if (at == m_colors.end())
		return;
	std::set<LedKeyboard::Key> selection;
	for (std::map<LedKeyboard::Key, Gdk::RGBA>::const_iterator it =
	     m_colors.begin(); it != m_colors.end(); ++it) {
		if (it->second.get_red() == at->second.get_red() &&
		    it->second.get_green() == at->second.get_green() &&
		    it->second.get_blue() == at->second.get_blue())
			selection.insert(it->first);
	}
	setSelection(selection);
}

KeyboardWidget::type_signal_void KeyboardWidget::signal_selection_changed() {
	return m_signal_selection_changed;
}

std::vector<LedKeyboard::Key> KeyboardWidget::getSelectedKeys() const {
	std::vector<LedKeyboard::Key> keys(m_selected.begin(), m_selected.end());
	return keys;
}

void KeyboardWidget::clearSelection() {
	setSelection(std::set<LedKeyboard::Key>());
}

void KeyboardWidget::toggleSelection(LedKeyboard::Key key) {
	std::set<LedKeyboard::Key> selection = m_selected;
	if (selection.count(key))
		selection.erase(key);
	else
		selection.insert(key);
	setSelection(selection);
}

void KeyboardWidget::setSelection(const std::set<LedKeyboard::Key> &selection) {
	if (selection == m_selected)
		return;
	// The new selection has to be in place before anything repaints: the
	// model is told what a key's state is by reading this set, so while
	// the old one was still current it drew every change backwards — a
	// band selection lit nothing and clearing it lit the keys it had
	// just let go of.
	const std::set<LedKeyboard::Key> previous = m_selected;
	m_selected = selection;
	for (LedKeyboard::Key key : previous)
		if (!m_selected.count(key))
			setKeySelected(key, false);
	for (LedKeyboard::Key key : m_selected)
		if (!previous.count(key))
			setKeySelected(key, true);
	m_signal_selection_changed.emit();
}

// Selection and pending state live in the widget; this is how the model
// gets told about them.
void KeyboardWidget::syncFlags(LedKeyboard::Key key) {
	if (!m_scene)
		return;
	m_scene->setKeyFlags(key, m_selected.count(key) > 0,
		m_pending.count(key) > 0);
}

void KeyboardWidget::setKeySelected(LedKeyboard::Key key, bool selected) {
	std::map<LedKeyboard::Key, KeyCap*>::iterator it = m_buttons.find(key);
	if (it != m_buttons.end()) {
		if (selected)
			it->second->get_style_context()->add_class("kb-selected");
		else
			it->second->get_style_context()->remove_class("kb-selected");
	}
	// Not conditional on there being a keycap widget: the model draws
	// keys the grid may not have built.
	syncFlags(key);
}

bool KeyboardWidget::keyAtPoint(double x, double y, LedKeyboard::Key &key) {
	// The model can say exactly which cap a point is on, at any angle;
	// the grid's cells are rectangles, so containment is the answer.
	if (m_scene)
		return m_scene->keyAt(x, y, key);
	for (std::map<LedKeyboard::Key, KeyCap*>::const_iterator it =
	     m_buttons.begin(); it != m_buttons.end(); ++it) {
		Gdk::Rectangle rect;
		if (!keyButtonRect(it->first, rect))
			continue;
		if (x >= rect.get_x() && x < rect.get_x() + rect.get_width() &&
		    y >= rect.get_y() && y < rect.get_y() + rect.get_height()) {
			key = it->first;
			return true;
		}
	}
	return false;
}

void KeyboardWidget::setMode(Mode mode) {
	if (mode == m_mode)
		return;
	endDrag();
	m_mode = mode;
}

KeyboardWidget::Mode KeyboardWidget::mode() const {
	return m_mode;
}

bool KeyboardWidget::keyButtonRect(LedKeyboard::Key key, Gdk::Rectangle &rect) {
	if (m_scene)
		return m_scene->keyRect(key, rect);
	std::map<LedKeyboard::Key, KeyCap*>::iterator it = m_buttons.find(key);
	if (it == m_buttons.end())
		return false;
	int x = 0, y = 0;
	it->second->translate_coordinates(*this, 0, 0, x, y);
	Gtk::Allocation allocation = it->second->get_allocation();
	rect = Gdk::Rectangle(x, y, allocation.get_width(),
	                      allocation.get_height());
	return true;
}

void KeyboardWidget::grabDrag() {
	if (m_dragGrabbed)
		return;
	gtk_grab_add(GTK_WIDGET(gobj()));
	m_dragGrabbed = true;
}

void KeyboardWidget::ungrabDrag() {
	if (!m_dragGrabbed)
		return;
	gtk_grab_remove(GTK_WIDGET(gobj()));
	m_dragGrabbed = false;
}

void KeyboardWidget::beginDrag(double x, double y, bool hasKey, bool shiftUnion) {
	m_dragStarted = true;
	m_dragActive = false;
	m_dragHasKey = hasKey;
	m_dragShiftUnion = shiftUnion;
	m_dragOriginX = m_dragCurrentX = x;
	m_dragOriginY = m_dragCurrentY = y;
	if (hasKey) {
		std::map<LedKeyboard::Key, Gdk::RGBA>::const_iterator it =
			m_colors.find(m_dragKey);
		if (it != m_colors.end())
			m_colorAtPress = it->second;
	}
	grabDrag();
}

void KeyboardWidget::updateDrag(double x, double y) {
	if (!m_dragStarted)
		return;
	if (!m_dragActive) {
		if (std::abs(x - m_dragOriginX) < 5 && std::abs(y - m_dragOriginY) < 5)
			return;
		m_dragActive = true;
		// A drag that started on a key, with the brush in hand, paints
		// the keys it crosses. Anywhere else it is a rubber band — which
		// is how a selection is made in either mode.
		if (m_mode == PAINT && m_dragHasKey) {
			m_stroking = true;
			m_stroked.clear();
			m_signal_stroke_begin.emit();
			strokeOnto(m_dragKey);
		} else {
			m_selectionAtDragStart = m_dragShiftUnion ?
				m_selected : std::set<LedKeyboard::Key>();
		}
	}
	m_dragCurrentX = x;
	m_dragCurrentY = y;

	if (m_stroking) {
		LedKeyboard::Key key = LedKeyboard::Key::a;
		if (keyAtPoint(x, y, key))
			strokeOnto(key);
		return;
	}

	Gdk::Rectangle band(
		(int)std::min(m_dragOriginX, m_dragCurrentX),
		(int)std::min(m_dragOriginY, m_dragCurrentY),
		(int)std::abs(m_dragCurrentX - m_dragOriginX),
		(int)std::abs(m_dragCurrentY - m_dragOriginY));
	// A perfectly horizontal/vertical drag would produce a zero-height /
	// zero-width band that intersects nothing; give it a minimum size.
	if (band.get_width() < 4)
		band.set_width(4);
	if (band.get_height() < 4)
		band.set_height(4);
	std::set<LedKeyboard::Key> selection = m_selectionAtDragStart;
	for (std::map<LedKeyboard::Key, KeyCap*>::iterator it = m_buttons.begin();
	     it != m_buttons.end(); ++it) {
		Gdk::Rectangle rect;
		if (keyButtonRect(it->first, rect) && band.intersects(rect))
			selection.insert(it->first);
	}
	setSelection(selection);
	m_bandOverlay->setBand(band);
}

// One key of a stroke. The signal is the same one a click emits, so the
// stroke stages keys through exactly the path a click does.
void KeyboardWidget::strokeOnto(LedKeyboard::Key key) {
	if (!m_stroked.insert(key).second)
		return;
	m_signal_key_pressed.emit(key);
}

void KeyboardWidget::endDrag() {
	ungrabDrag();
	m_dragStarted = false;
	m_dragActive = false;
	if (m_stroking) {
		m_stroking = false;
		m_stroked.clear();
		m_signal_stroke_end.emit();
	}
	m_bandOverlay->clearBand();
}

void KeyboardWidget::cancelDrag() {
	if (!m_dragStarted)
		return;
	if (m_dragActive && !m_stroking)
		setSelection(m_selectionAtDragStart);
	endDrag();
}

// A press that landed on a key, in widget coordinates. Both views end
// up here, which is why they behave the same.
bool KeyboardWidget::pressOnKey(GdkEventButton *event, LedKeyboard::Key key,
                                double x, double y) {
	// A hand on the mouse and a hand on the arrow keys are the same
	// person: touching a key with one moves the other's cursor there, so
	// that arrowing on after a click carries on from where the click was
	// rather than from wherever the cursor was left.
	if (event->button == 1 || event->button == 3)
		setCursorKey(key, false);
	if (event->type == GDK_BUTTON_PRESS && event->button == 3) {
		// Platform convention: secondary click opens a menu. Clearing is
		// the first item in it, so the old shortcut is still one click.
		m_signal_key_menu.emit(key);
		return true;
	}
	if (event->type == GDK_2BUTTON_PRESS && event->button == 1) {
		endDrag();
		m_signal_key_pick.emit(key);
		return true;
	}
	if (event->type == GDK_BUTTON_PRESS && event->button == 1) {
		if (event->state & GDK_SHIFT_MASK) {
			toggleSelection(key);
			return true;
		}
		m_dragKey = key;
		beginDrag(x, y, true, false);
		return true;
	}
	return false;
}

bool KeyboardWidget::onKeyButtonPress(GdkEventButton *event, LedKeyboard::Key key) {
	if (!m_use3D)
		m_gridBox.grab_focus();
	int x = 0, y = 0;
	m_buttons[key]->translate_coordinates(*this, (int)event->x, (int)event->y,
	                                      x, y);
	return pressOnKey(event, key, x, y);
}

bool KeyboardWidget::onKeyButtonMotion(GdkEventMotion *event, LedKeyboard::Key key) {
	if (!m_dragStarted || !m_dragHasKey || key != m_dragKey)
		return false;
	int x = 0, y = 0;
	m_buttons[key]->translate_coordinates(*this, (int)event->x,
	                                      (int)event->y, x, y);
	updateDrag(x, y);
	return true;
}

bool KeyboardWidget::onKeyButtonRelease(GdkEventButton *event, LedKeyboard::Key key) {
	if (!m_dragStarted || !m_dragHasKey || key != m_dragKey)
		return false;
	return onOverlayRelease(event);
}

// The scene is a single surface: ask it what is under the pointer, then
// hand over to the same handlers the grid uses. What is new here is
// only the camera, which has to share the mouse with the editor: the
// left button still paints and band-selects, the right button turns the
// board (and still opens the menu if it never moved), the middle button
// slides it, the wheel zooms.
bool KeyboardWidget::onScenePress(GdkEventButton *event) {
	if (!m_scene)
		return false;
	m_sceneBox.grab_focus();
	LedKeyboard::Key key = LedKeyboard::Key::a;
	const bool onKey = m_scene->keyAt(event->x, event->y, key);
	if (event->button == 3 || event->button == 2) {
		m_orbiting = event->button == 3;
		m_panning = event->button == 2;
		m_orbitX = event->x;
		m_orbitY = event->y;
		m_orbitMoved = false;
		m_orbitOnKey = onKey;
		m_orbitKey = key;
		return true;
	}
	if (onKey)
		return pressOnKey(event, key, event->x, event->y);
	if (event->type == GDK_BUTTON_PRESS && event->button == 1) {
		beginDrag(event->x, event->y, false,
			(event->state & GDK_SHIFT_MASK) != 0);
		return true;
	}
	return false;
}

bool KeyboardWidget::onSceneMotion(GdkEventMotion *event) {
	if (!m_scene)
		return false;
	if (m_orbiting || m_panning) {
		const double dx = event->x - m_orbitX;
		const double dy = event->y - m_orbitY;
		if (std::abs(dx) > 3 || std::abs(dy) > 3)
			m_orbitMoved = true;
		if (m_orbiting)
			m_scene->orbit(dx, dy);
		else
			m_scene->pan(dx, dy);
		if (m_orbitMoved)
			saidViewMoved(m_orbiting ? "turned" : "moved");
		m_orbitX = event->x;
		m_orbitY = event->y;
		return true;
	}
	if (m_dragStarted) {
		updateDrag(event->x, event->y);
		return true;
	}
	// Hover, so the key under the pointer is obvious at an angle, and
	// so the lamps — which carry no legend — can still be identified.
	LedKeyboard::Key key = LedKeyboard::Key::a;
	if (m_scene->keyAt(event->x, event->y, key)) {
		if (!m_hoverValid || m_hoverKey != key) {
			m_scene->clearHover();
			m_scene->setHovered(key, true);
			m_hoverKey = key;
			m_hoverValid = true;
			std::map<LedKeyboard::Key, std::string>::const_iterator name =
				m_keyNames.find(key);
			m_sceneBox.set_tooltip_text(name == m_keyNames.end() ?
				std::string() : name->second);
		}
	} else if (m_hoverValid) {
		m_scene->clearHover();
		m_hoverValid = false;
		// Off the keys and back on the bare board: what the board itself
		// does, rather than nothing at all.
		m_sceneBox.set_tooltip_text(sceneGestures);
	}
	return true;
}

bool KeyboardWidget::onSceneRelease(GdkEventButton *event) {
	if (!m_scene)
		return false;
	if (m_orbiting || m_panning) {
		const bool wasOrbit = m_orbiting;
		const bool moved = m_orbitMoved;
		m_orbiting = m_panning = false;
		// A right-click that never turned the board is still a menu.
		if (wasOrbit && !moved && m_orbitOnKey)
			m_signal_key_menu.emit(m_orbitKey);
		return true;
	}
	return onOverlayRelease(event);
}

bool KeyboardWidget::onSceneScroll(GdkEventScroll *event) {
	if (!m_scene)
		return false;
	if (event->direction == GDK_SCROLL_UP)
		m_scene->zoomBy(1.0);
	else if (event->direction == GDK_SCROLL_DOWN)
		m_scene->zoomBy(-1.0);
	else if (event->direction == GDK_SCROLL_SMOOTH)
		m_scene->zoomBy(-event->delta_y);
	else
		return true;
	saidViewMoved("zoomed");
	return true;
}

KeyboardWidget::type_signal_key KeyboardWidget::signal_cursor_moved() {
	return m_signal_cursor_moved;
}

// Arriving here with the Tab key is arriving somewhere, and the cursor is
// where. It is put on a key straight away and said out loud, so that the
// first arrow press moves from a known place rather than conjuring one.
void KeyboardWidget::onBoardFocus(bool in) {
	if (in) {
		const bool wasSet = m_cursorValid;
		ensureCursor();
		showCursorMark(true);
		if (m_cursorValid && !wasSet)
			m_signal_cursor_moved.emit(m_cursorKey);
	} else {
		showCursorMark(false);
	}
	m_signal_focus_changed.emit();
}

bool KeyboardWidget::hasCursor() const {
	return m_cursorValid;
}

LedKeyboard::Key KeyboardWidget::cursorKey() const {
	return m_cursorKey;
}

std::string KeyboardWidget::keyName(LedKeyboard::Key key) const {
	std::map<LedKeyboard::Key, std::string>::const_iterator it =
		m_keyNames.find(key);
	return it == m_keyNames.end() ? std::string() : it->second;
}

void KeyboardWidget::showCursorMark(bool shown) {
	if (m_cursorShown == shown)
		return;
	m_cursorShown = shown;
	if (m_scene)
		m_scene->clearCursor();
	std::map<LedKeyboard::Key, KeyCap*>::iterator cap =
		m_buttons.find(m_cursorKey);
	if (cap != m_buttons.end())
		cap->second->setCursor(false);
	if (!shown || !m_cursorValid)
		return;
	if (m_scene)
		m_scene->setCursor(m_cursorKey, true);
	if (cap != m_buttons.end())
		cap->second->setCursor(true);
}

void KeyboardWidget::setCursorKey(LedKeyboard::Key key, bool announce) {
	if (m_keyPositions.find(key) == m_keyPositions.end())
		return;
	const bool shown = m_cursorShown;
	showCursorMark(false);
	m_cursorKey = key;
	m_cursorValid = true;
	m_cursorShown = false;
	showCursorMark(shown);
	if (announce)
		m_signal_cursor_moved.emit(key);
}

void KeyboardWidget::ensureCursor() {
	if (m_cursorValid && m_keyPositions.count(m_cursorKey))
		return;
	if (m_keyPositions.empty())
		return;
	// The key nearest the top-left corner of the board, which on every
	// layout here is Esc — the same place the eye starts.
	const std::map<LedKeyboard::Key, KeyPosition>::const_iterator first =
		std::min_element(m_keyPositions.begin(), m_keyPositions.end(),
			[](const std::pair<const LedKeyboard::Key, KeyPosition> &a,
			   const std::pair<const LedKeyboard::Key, KeyPosition> &b) {
				if (a.second.first != b.second.first)
					return a.second.first < b.second.first;
				return a.second.second < b.second.second;
			});
	m_cursorKey = first->first;
	m_cursorValid = true;
}

// Nearest key in a direction, in the grid's own units. Left and right
// look along the row first and only then give up; up and down take the
// nearest key by column on the next row that has one, so a cursor
// walking down the left-hand edge passes Esc, Tab, Caps, Shift, Ctrl
// rather than stopping at the first row with a gap in it.
bool KeyboardWidget::moveCursor(int dx, int dy) {
	ensureCursor();
	if (!m_cursorValid)
		return false;
	const std::map<LedKeyboard::Key, KeyPosition>::const_iterator at =
		m_keyPositions.find(m_cursorKey);
	if (at == m_keyPositions.end())
		return false;
	const int row = at->second.first, column = at->second.second;
	bool found = false;
	LedKeyboard::Key best = m_cursorKey;
	int bestRow = 0, bestColumn = 0;
	for (std::map<LedKeyboard::Key, KeyPosition>::const_iterator it =
	     m_keyPositions.begin(); it != m_keyPositions.end(); ++it) {
		const int otherRow = it->second.first, otherColumn = it->second.second;
		if (dy != 0) {
			if ((otherRow - row) * dy <= 0)
				continue;
			// Rows first, then how far sideways it lands. Ties go to the
			// key already further along in the direction of travel, which
			// keeps the walk from doubling back.
			if (found) {
				const int rowStep = std::abs(otherRow - row);
				const int bestStep = std::abs(bestRow - row);
				if (rowStep != bestStep) {
					if (rowStep > bestStep)
						continue;
				} else if (std::abs(otherColumn - column) >=
				           std::abs(bestColumn - column)) {
					continue;
				}
			}
		} else {
			if (otherRow != row)
				continue;
			if ((otherColumn - column) * dx <= 0)
				continue;
			if (found && std::abs(otherColumn - column) >=
			             std::abs(bestColumn - column))
				continue;
		}
		found = true;
		best = it->first;
		bestRow = otherRow;
		bestColumn = otherColumn;
	}
	if (!found)
		return false;
	setCursorKey(best, true);
	return true;
}

// Everything the pointer can do to one key, from the keys. The board is
// one Tab stop, so this is the whole of its keyboard interface, and it
// is deliberately the same set of acts the mouse has: arrows for where
// the pointer would be, Return or space for the click, Shift for the
// shift-click, Delete for "turn this one off", Menu for the right
// button.
bool KeyboardWidget::onBoardKeyPress(GdkEventKey *event) {
	const bool shift = (event->state & GDK_SHIFT_MASK) != 0;
	const bool control = (event->state & GDK_CONTROL_MASK) != 0;
	switch (event->keyval) {
	case GDK_KEY_Left:  case GDK_KEY_KP_Left:  return moveCursor(-1, 0);
	case GDK_KEY_Right: case GDK_KEY_KP_Right: return moveCursor(1, 0);
	case GDK_KEY_Up:    case GDK_KEY_KP_Up:    return moveCursor(0, -1);
	case GDK_KEY_Down:  case GDK_KEY_KP_Down:  return moveCursor(0, 1);
	case GDK_KEY_Home:  case GDK_KEY_KP_Home:
	case GDK_KEY_End:   case GDK_KEY_KP_End: {
		// The ends of the row the cursor is on. This used to be where
		// the model's view was reset from, which was the only thing the
		// board answered at all; that has moved to Ctrl+Home, so that
		// Home means on this board what it means in every other grid.
		const bool end = event->keyval == GDK_KEY_End ||
			event->keyval == GDK_KEY_KP_End;
		if (control && !end) {
			resetBoardView();
			return true;
		}
		bool moved = false;
		while (moveCursor(end ? 1 : -1, 0))
			moved = true;
		return moved;
	}
	case GDK_KEY_Return:
	case GDK_KEY_KP_Enter:
	case GDK_KEY_space:
		// Ctrl+Return is the window's "send to keyboard" and must reach
		// it even from here: the board is where a person will be standing
		// when they have finished painting.
		if (control)
			return false;
		ensureCursor();
		if (!m_cursorValid)
			return false;
		if (shift) {
			toggleSelection(m_cursorKey);
			// Choosing a key changes nothing on the board but its ring,
			// which is no use to someone who cannot see it. Saying the
			// cursor's line again is what reports the act: the word
			// "chosen" arrives or leaves.
			m_signal_cursor_moved.emit(m_cursorKey);
		} else if (m_mode == SELECT) {
			std::set<LedKeyboard::Key> only;
			only.insert(m_cursorKey);
			setSelection(only);
		} else {
			m_signal_key_pressed.emit(m_cursorKey);
		}
		return true;
	case GDK_KEY_Delete:
	case GDK_KEY_KP_Delete:
	case GDK_KEY_BackSpace:
		ensureCursor();
		if (!m_cursorValid)
			return false;
		m_signal_key_cleared.emit(m_cursorKey);
		return true;
	case GDK_KEY_Menu:
	case GDK_KEY_F10:
		if (event->keyval == GDK_KEY_F10 && !shift)
			return false;
		ensureCursor();
		if (!m_cursorValid)
			return false;
		m_signal_key_menu.emit(m_cursorKey);
		return true;
	default:
		break;
	}
	return false;
}



// The context only exists once the widget has been shown, so whether
// the model can be drawn at all is answered here — and if it cannot,
// the grid takes over rather than leaving an empty editor.
void KeyboardWidget::onSceneReady() {
	if (m_sceneFailed || !m_scene || m_scene->usable())
		return;
	m_sceneFailed = true;
	// This is raised from inside the scene's realize, and the container
	// cannot be rearranged from there: GTK is still walking its children
	// on the way to mapping them, and taking one out from under it trips
	// an assertion in gtk_widget_real_map. Swap once that walk is over.
	// The slot is a member function, so it is dropped if the widget goes
	// away first.
	Glib::signal_idle().connect(
		sigc::mem_fun(*this, &KeyboardWidget::fallbackToGrid));
}

bool KeyboardWidget::fallbackToGrid() {
	const bool was3D = m_use3D;
	setView3D(false);
	// Switching views announces itself. Being told the model is
	// impossible while the grid is already up does not change the view,
	// but it is still news: it is what takes the switch out of service.
	if (!was3D)
		m_signal_view_changed.emit();
	return false;
}

bool KeyboardWidget::view3D() const {
	return m_use3D;
}

bool KeyboardWidget::view3DPossible() const {
	return m_sceneWidget != nullptr && !m_sceneFailed;
}

KeyboardWidget::type_signal_void KeyboardWidget::signal_view_changed() {
	return m_signal_view_changed;
}

KeyboardWidget::type_signal_void KeyboardWidget::signal_focus_changed() {
	return m_signal_focus_changed;
}

KeyboardWidget::type_signal_text KeyboardWidget::signal_view_moved() {
	return m_signal_view_moved;
}

void KeyboardWidget::saidViewMoved(const char *verb) {
	const gint64 now = g_get_monotonic_time();
	if (now - m_viewMovedSaid < 500000)
		return;
	m_viewMovedSaid = now;
	m_signal_view_moved.emit(verb);
}

void KeyboardWidget::resetBoardView() {
	if (m_scene)
		m_scene->resetView();
}

// Whichever of the two is on screen: they are one board to the person
// tabbing onto it, and the ring the stage draws is the same ring.
bool KeyboardWidget::hasVisibleFocus() const {
	return m_use3D ? m_sceneBox.has_visible_focus() :
		m_gridBox.has_visible_focus();
}

KeyboardWidget::type_signal_void KeyboardWidget::signal_stroke_begin() {
	return m_signal_stroke_begin;
}

KeyboardWidget::type_signal_void KeyboardWidget::signal_stroke_end() {
	return m_signal_stroke_end;
}

// Swapping the board between the model and the grid. Both are built and
// both are kept current, so this is only a matter of which one is in
// the overlay.
void KeyboardWidget::setView3D(bool on) {
	if (on && !view3DPossible())
		on = false;
	if (on == m_use3D && get_child() != nullptr)
		return;
	endDrag();
	m_use3D = on;
	if (get_child())
		remove();
	if (m_use3D) {
		m_scene = m_sceneWidget;
		add(m_sceneBox);
		m_sceneBox.show_all();
		// The model has only been told about colours while it was the
		// one drawing; catch it up.
		for (std::map<LedKeyboard::Key, KeyCap*>::const_iterator it =
		     m_buttons.begin(); it != m_buttons.end(); ++it) {
			paintKey(it->first);
			syncFlags(it->first);
		}
	} else {
		m_scene = nullptr;
		add(m_gridBox);
		m_gridBox.show_all();
	}
	// The cursor is a place on the board, not a property of the drawing
	// of it: switching views has to carry the mark across, or a person
	// who unticked "3D board" mid-walk would lose where they were.
	if (m_cursorShown) {
		m_cursorShown = false;
		showCursorMark(true);
	}
	// Whoever asked for the change, the window is told about it here, so
	// the switch in the toolbar cannot end up claiming one view while the
	// other is on screen.
	m_signal_view_changed.emit();
}

bool KeyboardWidget::onGridButtonPress(GdkEventButton *event) {
	if (event->type == GDK_BUTTON_PRESS && event->button == 1) {
		// The model takes the focus when it is clicked; so does the grid,
		// so that where the keyboard focus is does not depend on which
		// view happens to be drawing.
		m_gridBox.grab_focus();
		int x = 0, y = 0;
		m_gridBox.translate_coordinates(*this, (int)event->x, (int)event->y,
		                                x, y);
		beginDrag(x, y, false, (event->state & GDK_SHIFT_MASK) != 0);
		return true;
	}
	return false;
}

bool KeyboardWidget::onGridButtonMotion(GdkEventMotion *event) {
	if (!m_dragStarted || m_dragHasKey)
		return false;
	int x = 0, y = 0;
	m_gridBox.translate_coordinates(*this, (int)event->x, (int)event->y, x, y);
	updateDrag(x, y);
	return true;
}

bool KeyboardWidget::onGridButtonRelease(GdkEventButton *event) {
	if (!m_dragStarted || m_dragHasKey)
		return false;
	return onOverlayRelease(event);
}

bool KeyboardWidget::onOverlayPress(GdkEventButton *event) {
	if (!m_dragStarted)
		return false;
	if (event->type == GDK_2BUTTON_PRESS && event->button == 1 && m_dragHasKey) {
		LedKeyboard::Key key = m_dragKey;
		endDrag();
		m_signal_key_pick.emit(key);
		return true;
	}
	return false;
}

bool KeyboardWidget::onOverlayMotion(GdkEventMotion *event) {
	if (!m_dragStarted)
		return false;
	// Reached through the drag's GTK grab, including from windows that
	// belong to other widgets once the pointer leaves the board — their
	// event coordinates mean nothing here, so go via root coordinates.
	double x = event->x, y = event->y;
	Glib::RefPtr<Gdk::Window> window = get_window();
	if (window) {
		int originX = 0, originY = 0;
		window->get_origin(originX, originY);
		Gtk::Allocation allocation = get_allocation();
		x = event->x_root - originX - allocation.get_x();
		y = event->y_root - originY - allocation.get_y();
	}
	updateDrag(x, y);
	return true;
}

bool KeyboardWidget::onOverlayRelease(GdkEventButton *event) {
	if (!m_dragStarted)
		return false;
	if (event->type == GDK_BUTTON_RELEASE && event->button == 1) {
		bool wasDrag = m_dragActive;
		bool hasKey = m_dragHasKey;
		LedKeyboard::Key key = m_dragKey;
		endDrag();
		// A click, on a key. What that means is the mode: staging the
		// colour, or selecting that key and nothing else.
		if (!wasDrag && hasKey) {
			if (m_mode == SELECT) {
				std::set<LedKeyboard::Key> only;
				only.insert(key);
				setSelection(only);
			} else {
				m_signal_key_pressed.emit(key);
			}
		}
		return true;
	}
	return false;
}

static Gdk::RGBA solidColor(const Gdk::RGBA &color) {
	Gdk::RGBA result;
	result.set_rgba(color.get_red(), color.get_green(), color.get_blue(), 1.0);
	return result;
}

void KeyboardWidget::paintKey(LedKeyboard::Key key) {
	std::map<LedKeyboard::Key, KeyCap*>::iterator it = m_buttons.find(key);
	if (it == m_buttons.end())
		return;
	Gdk::RGBA color("#3c3c3c");
	std::map<LedKeyboard::Key, Gdk::RGBA>::const_iterator preview = m_preview.find(key);
	if (preview != m_preview.end())
		color = preview->second;
	else {
		std::map<LedKeyboard::Key, Gdk::RGBA>::const_iterator draft = m_colors.find(key);
		if (draft != m_colors.end())
			color = draft->second;
	}
	// The cap is drawn; only the legend needs the style engine, and only
	// when the readable colour actually changes from black to white or
	// back. During a fade that is rare, which is the whole point.
	if (m_scene)
		m_scene->setKeyColor(key, color);
	it->second->setFill(color);
	const std::string text = styling::contrastingText(color);
	std::map<LedKeyboard::Key, std::string>::iterator known =
		m_keyTextColor.find(key);
	if (known == m_keyTextColor.end() || known->second != text) {
		m_keyTextColor[key] = text;
		styling::paintText(*it->second, m_keyStyles[key], text);
	}
}

void KeyboardWidget::setKeyColor(LedKeyboard::Key key, const Gdk::RGBA &color) {
	// Only keys this layout renders may enter the draft. Group painting
	// and profile loading address keys by name regardless of the model
	// (keysForGroup is model-independent), so without this a g815 would
	// collect draft colours for caps/num/scroll/game/stop and g6-g9 —
	// keys with no keycap, which the firmware discards but which would
	// still be counted as pending and then recorded as applied.
	if (!m_buttons.count(key))
		return;
	m_colors[key] = solidColor(color);
	if (!m_preview.count(key))
		paintKey(key);
}

void KeyboardWidget::setAllColors(const Gdk::RGBA &color) {
	Gdk::RGBA solid = solidColor(color);
	for (std::map<LedKeyboard::Key, Gdk::RGBA>::iterator it = m_colors.begin();
	     it != m_colors.end(); ++it)
		it->second = solid;
	for (std::map<LedKeyboard::Key, KeyCap*>::iterator it = m_buttons.begin();
	     it != m_buttons.end(); ++it) {
		if (!m_preview.count(it->first))
			paintKey(it->first);
	}
}

void KeyboardWidget::setPreviewColor(LedKeyboard::Key key, const Gdk::RGBA &color) {
	m_preview[key] = solidColor(color);
	paintKey(key);
}

void KeyboardWidget::clearPreviewKey(LedKeyboard::Key key) {
	if (!m_preview.erase(key))
		return;
	paintKey(key);
}

void KeyboardWidget::clearPreview() {
	if (m_preview.empty())
		return;
	std::vector<LedKeyboard::Key> keys;
	for (std::map<LedKeyboard::Key, Gdk::RGBA>::const_iterator it = m_preview.begin();
	     it != m_preview.end(); ++it)
		keys.push_back(it->first);
	m_preview.clear();
	for (LedKeyboard::Key key : keys)
		paintKey(key);
}

void KeyboardWidget::applyPreviewFrame(const LedKeyboard::KeyValueArray &values) {
	for (const LedKeyboard::KeyValue &keyValue : values) {
		Gdk::RGBA draft("#000000");
		getKeyColor(keyValue.key, draft);
		int dr = (int)std::round(draft.get_red() * 255.0);
		int dg = (int)std::round(draft.get_green() * 255.0);
		int db = (int)std::round(draft.get_blue() * 255.0);
		if (keyValue.color.red == dr && keyValue.color.green == dg &&
		    keyValue.color.blue == db) {
			if (m_preview.erase(keyValue.key))
				paintKey(keyValue.key);
			continue;
		}
		std::map<LedKeyboard::Key, Gdk::RGBA>::iterator prev =
			m_preview.find(keyValue.key);
		if (prev != m_preview.end() &&
		    (int)std::round(prev->second.get_red() * 255.0) == keyValue.color.red &&
		    (int)std::round(prev->second.get_green() * 255.0) == keyValue.color.green &&
		    (int)std::round(prev->second.get_blue() * 255.0) == keyValue.color.blue)
			continue;
		Gdk::RGBA rgba;
		rgba.set_rgba(keyValue.color.red / 255.0,
		              keyValue.color.green / 255.0,
		              keyValue.color.blue / 255.0, 1.0);
		m_preview[keyValue.key] = rgba;
		paintKey(keyValue.key);
	}
}

bool KeyboardWidget::getKeyColor(LedKeyboard::Key key, Gdk::RGBA &color) const {
	std::map<LedKeyboard::Key, Gdk::RGBA>::const_iterator it = m_colors.find(key);
	if (it == m_colors.end())
		return false;
	color = it->second;
	return true;
}

Gdk::RGBA KeyboardWidget::getColorAtPress() const {
	return m_colorAtPress;
}

const std::map<LedKeyboard::Key, Gdk::RGBA>& KeyboardWidget::getKeyColors() const {
	return m_colors;
}

void KeyboardWidget::markPending(LedKeyboard::Key key, bool pending) {
	std::map<LedKeyboard::Key, KeyCap*>::iterator it = m_buttons.find(key);
	if (it == m_buttons.end())
		return;
	if (pending) {
		it->second->get_style_context()->add_class("kb-pending");
		m_pending.insert(key);
	} else {
		it->second->get_style_context()->remove_class("kb-pending");
		m_pending.erase(key);
	}
	syncFlags(key);
}

void KeyboardWidget::clearPendingMarks() {
	for (std::map<LedKeyboard::Key, KeyCap*>::iterator it = m_buttons.begin();
	     it != m_buttons.end(); ++it)
		it->second->get_style_context()->remove_class("kb-pending");
	const std::set<LedKeyboard::Key> was = m_pending;
	m_pending.clear();
	for (std::set<LedKeyboard::Key>::const_iterator it = was.begin();
	     it != was.end(); ++it)
		syncFlags(*it);
}

BoardStage::BoardStage(KeyboardWidget &board) : m_board(board) {
	// The rim carries the board's focus, so it has to be repainted when
	// the board takes it or gives it up: invalidating the board itself
	// would only redraw the picture, and the ring is not in it.
	m_board.signal_focus_changed().connect([this]() { queue_draw(); });
	// The tray is cut to the view that is drawing, so switching views is
	// a resize of the tray and not only a redraw of what is in it.
	m_board.signal_view_changed().connect([this]() { queue_resize(); });
}

bool BoardStage::on_draw(const Cairo::RefPtr<Cairo::Context> &context) {
	Gtk::ScrolledWindow::on_draw(context);
	if (!m_board.hasVisibleFocus())
		return false;
	// Around the rim, not around the picture: the well's own box, which
	// is the allocation less the margin the sheet holds it off the pane
	// with. The offset and the radius of the ring itself are the
	// stylesheet's, so this is the ring every other control wears.
	Glib::RefPtr<Gtk::StyleContext> style = get_style_context();
	const Gtk::Border margin = style->get_margin();
	const Gtk::Allocation area = get_allocation();
	style->render_focus(context, margin.get_left(), margin.get_top(),
		area.get_width() - margin.get_left() - margin.get_right(),
		area.get_height() - margin.get_top() - margin.get_bottom());
	return false;
}

Gtk::SizeRequestMode BoardStage::get_request_mode_vfunc() const {
	return Gtk::SIZE_REQUEST_HEIGHT_FOR_WIDTH;
}

void BoardStage::frame(int &horizontal, int &vertical) const {
	// Everything between the pane and the board: the gap the well is held
	// off the pane by, its rim, and the lip inside it. Read from the style
	// rather than repeated here, so the two cannot drift apart.
	Glib::RefPtr<Gtk::StyleContext> style =
		const_cast<BoardStage*>(this)->get_style_context();
	const Gtk::Border margin = style->get_margin();
	const Gtk::Border padding = style->get_padding();
	const Gtk::Border border = style->get_border();
	horizontal = margin.get_left() + margin.get_right() +
		padding.get_left() + padding.get_right() +
		border.get_left() + border.get_right();
	vertical = margin.get_top() + margin.get_bottom() +
		padding.get_top() + padding.get_bottom() +
		border.get_top() + border.get_bottom();
}

void BoardStage::get_preferred_height_for_width_vfunc(int width, int &minimum,
                                                      int &natural) const {
	int horizontal = 0, vertical = 0;
	frame(horizontal, vertical);
	const double aspect = m_board.boardAspect();
	const double depth = m_board.view3D() ? stageDepth3D : stageDepthFlat;
	const int floorWidth = std::max(1, width - horizontal);
	// The shallowest well that still has a board in it rather than a
	// strip of one.
	const int shallowest = stageMinHeight + vertical;
	natural = std::max(shallowest,
		(int)std::floor(floorWidth / aspect * depth) + vertical);
	// The shape is what the stage wants, not what it demands. A window
	// dragged shorter than a keyboard is deep would otherwise stop dead
	// at the height this asked for; asking for less as a minimum lets the
	// board come down with the window instead, and both views fit
	// themselves to the well they are given.
	minimum = shallowest;
}

void BoardStage::get_preferred_height_vfunc(int &minimum, int &natural) const {
	get_preferred_height_for_width_vfunc(get_min_content_width(), minimum,
	                                     natural);
}

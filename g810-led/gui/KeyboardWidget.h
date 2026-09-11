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

#ifndef KEYBOARD_WIDGET
#define KEYBOARD_WIDGET

#include <gtkmm.h>

#include <map>
#include <set>
#include <vector>

#include "../src/classes/Keyboard.h"
#include "KeyLayout.h"
#include "KeyboardScene.h"

class BandOverlay;

// One keycap.
//
// The colour is drawn, not styled. Styling it meant reloading a CSS
// provider per key on every change: 2.9 µs to parse the stylesheet plus
// the style invalidation that follows it, and a live effect repainting
// a whole board at 25 fps does that ~2 750 times a second. Setting a
// colour and invalidating measures 0.19 µs, and what is left in CSS is
// the unlit cap, the disabled placeholders and the dimmer indicator
// caps — appearance that does not depend on the LED colour.
//
// The marks are drawn here too, and for a harder reason than speed: a
// mark has to be seen against the key it is on, and only the thing that
// draws the key knows what colour that is. .kb-selected and .kb-pending
// are still the state — the widget adds and removes them, and a test can
// read them — but what they look like is decided in on_draw, by the same
// styling::markColor the model's shader marks by.
class KeyCap : public Gtk::EventBox {
	public:
		KeyCap();
		// Repaints only when the colour actually changed.
		void setFill(const Gdk::RGBA &color);
		void clearFill();
		// The key the arrow keys are standing on. Drawn as the innermost
		// band, and never dropped: selection and unsent colour take the
		// outer bands, and where typing will land is not something that
		// may be hidden by what else is true of the key.
		void setCursor(bool on);
		// An indicator lamp rather than a keycap: a short lit slot set
		// into the board, the size the model draws it, with the room
		// above it left clear for the name the plate prints there.
		void setLamp(bool lamp);
		// Where the lamp's lens sits inside a cell that size — asked by
		// the plate as well, which prints the name in the clear strip
		// above it and must agree with this about where that strip ends.
		static void lens(double width, double height, double &x, double &y,
		                 double &lensWidth, double &lensHeight);

	protected:
		bool on_draw(const Cairo::RefPtr<Cairo::Context> &cr) override;
		// A keycap asks for room for its legend, and a hundred legends
		// used to add up to a fixed 1074px board — wider than the pane at
		// every window size, so a third of the keyboard was off the edge.
		// The cap will take a legend's worth of room when there is one to
		// take, but it will settle for the size of a key on a small
		// screen and drop the legend rather than push the board off it.
		void get_preferred_width_vfunc(int &minimum, int &natural) const override;
		void get_preferred_height_vfunc(int &minimum, int &natural) const override;
		void on_size_allocate(Gtk::Allocation &allocation) override;

	private:
		// What the marks have to be seen against: the LED colour if this
		// cap is painted, and whatever the sheet says an unlit cap of
		// this kind is if it is not.
		Gdk::RGBA capColor() const;
		void drawMarks(const Cairo::RefPtr<Cairo::Context> &cr,
		               const Glib::RefPtr<Gtk::StyleContext> &style,
		               double width, double height, double radius);

		Gdk::RGBA m_fill;
		bool m_hasFill;
		bool m_cursor = false;
		bool m_lamp = false;
		// Whether the legend still fits inside the cap at this size.
		bool m_legendFits = true;
};

// What the flat board's keycaps sit on.
//
// It covers the whole stage, so a rubber band can be started anywhere on
// it and not only where there are keys, and it hands the grid inside the
// largest rectangle of the keyboard's own proportions that will fit,
// centred. That is what keeps a keycap square: the grid is homogeneous,
// so a grid stretched to the shape of the pane is a keyboard with
// rectangular keys.
//
// A Gtk::AspectFrame would say the same thing in fewer lines, but a
// GtkFrame is a card in this window's stylesheet, and the board is not
// a card.
// It is also where the lamps get their names. A lamp is the one thing on
// this board that cannot be named from its shape — five coloured blocks
// in a corner — and a real keyboard prints the names beside the lenses
// rather than on them, because a lens is four millimetres of light and a
// word is not. The model silkscreens them on its plate; this is the flat
// board's plate, so this is where it does the same.
class KeyPlate : public Gtk::EventBox {
	public:
		// One lamp and the word that names it. The widget is kept rather
		// than a rectangle: the board is re-laid-out on every resize and
		// a rectangle would be a copy of where the lamp used to be.
		struct Lamp {
			Gtk::Widget *cap;
			Glib::ustring name;
			Lamp() : cap(NULL) {}
		};

		// The key grid's shape, in quarter-unit columns and whole rows.
		// The plate around it is worked out from that.
		void setBoard(int columns, int rows);
		void setLamps(const std::vector<Lamp> &lamps);

	protected:
		void on_size_allocate(Gtk::Allocation &allocation) override;
		bool on_draw(const Cairo::RefPtr<Cairo::Context> &cr) override;

	private:
		// The plate this board stands on, in this widget's coordinates,
		// and how far in from it the keys start.
		void body(Gdk::Rectangle &plate, double &bezel) const;
		// The largest type, within the range that is still lettering,
		// that puts every one of these words inside a box that wide and
		// that deep. Zero when none of them does.
		int fitAll(const std::vector<Glib::ustring> &words,
		           double width, double height) const;

		int m_columns = 94;
		int m_rows = 6;
		std::vector<Lamp> m_lamps;
};

// On-screen keyboard: a grid of event boxes whose layout is built from
// the connected model's feature flags (logo key, media cluster, G-key
// column, indicator LEDs). The grid uses quarter-key units (1u = 4
// columns) so real keycap widths (1.25u, 1.5u, 1.75u, 2.25u, ...) can
// be represented. Total width: 94 columns, or 98 with the extra G-key
// strip (g6–g9).
//
// The widget is a pure draft editor: it tracks the per-key colors shown
// on screen and emits interaction signals; the device writes are done
// by MainWindow on "Apply".
//
// Interaction: left-click paints, right-click clears, double-click
// picks a color, shift+click toggles selection, left-drag draws a
// rubber-band that selects the keys it covers.
class KeyboardWidget : public Gtk::Overlay {
	public:
		// Feature flags of the connected keyboard, mirroring
		// src/helpers/help.h; drive which keys are rendered.
		struct LayoutFlags {
			bool rgb = true;
			bool intensity = false;
			bool logo1 = false;
			bool logo2 = false;
			bool multimedia = false;
			// How many G-keys the model actually has: the feature bit is
			// shared by the g910 (g1-g9) and the g815 (g1-g5 only).
			int gkeyCount = 0;
			// Some models silently drop keys they cannot address (see
			// LedKeyboard::setKeys); those are drawn but not controllable
			// rather than pretending an Apply reached them.
			bool dropsIndicatorsAndStop = false;
			bool numpad = true;
			bool setindicators = true;
			bool setkey = true;  // false: no per-key grid (g213/g413)
		};

		// What the left button does. The two used to be one gesture — a
		// click painted, a drag rubber-banded — so a single key could not
		// be selected without being painted, and the only way to select
		// anything without painting it was to band more than one key.
		enum Mode {
			PAINT,    // click paints; drag from a key paints across them
			SELECT    // click selects that key alone; nothing is painted
		};

		KeyboardWidget();
		virtual ~KeyboardWidget();

		typedef sigc::signal<void, LedKeyboard::Key> type_signal_key;
		typedef sigc::signal<void> type_signal_void;

		type_signal_key signal_key_pressed();  // click (no drag, no shift)
		type_signal_key signal_key_pick();     // double-click
		type_signal_key signal_key_cleared();  // clear a single key
		type_signal_key signal_key_menu();     // right-click: context menu
		type_signal_void signal_selection_changed();
		// Raised when the board switches between the model and the grid,
		// including when the model turns out not to be drawable.
		type_signal_void signal_view_changed();
		// Raised when the board gains or loses the keyboard focus, so
		// whatever frames it can say so.
		type_signal_void signal_focus_changed();
		// Raised when a gesture has turned, slid or zoomed the model,
		// carrying which of the three it was. The window says so, and
		// says how to put it back: a board left nose-down and six keycaps
		// wide is the one state in this window a mouse can reach and
		// cannot be seen out of.
		typedef sigc::signal<void, const Glib::ustring&> type_signal_text;
		type_signal_text signal_view_moved();
		// Whether the board is the focused widget, and focus is being
		// shown at all — GTK hides it until a key is pressed, so that a
		// window driven by the mouse is not covered in rings.
		bool hasVisibleFocus() const;
		// The ends of a paint stroke. A drag across the board is one
		// action to the person doing it, so it should be one to undo.
		type_signal_void signal_stroke_begin();
		type_signal_void signal_stroke_end();

		void setMode(Mode mode);
		Mode mode() const;

		// The board as a model, or as the flat grid of keys.
		void setView3D(bool on);
		bool view3D() const;
		bool view3DPossible() const;
		// Back to the angle and distance the board opens at. Reachable
		// from Home while the board has focus, and from the button in
		// the toolbar, which is the half of it a mouse can find.
		void resetBoardView();

		// The shape of the connected keyboard, width over height, in
		// keycap units. Both views are drawn to it, which is what makes
		// switching between them a change of drawing and not of layout.
		double boardAspect() const;

		void setLayoutFlags(const LayoutFlags &flags);
		const LayoutFlags &getLayoutFlags() const;

		// Key grid positions (row, column in quarter units) of the
		// rendered layout, used by animations.
		typedef std::pair<int, int> KeyPosition;
		const std::map<LedKeyboard::Key, KeyPosition>& getKeyPositions() const;

		// --- The board from the keyboard ----------------------------------
		//
		// Everything the pointer can do to one key, the arrow keys can do
		// too. Without this the one act this program exists for — colour
		// that key, the one under your finger — had no keyboard path at
		// all: the board was a single Tab stop that answered nothing but
		// Home, so a person who cannot use a mouse could paint the seven
		// preset groups and never a key of their own choosing.
		//
		// The cursor is a place on the board, not a selection: moving it
		// changes nothing, so arrowing about is always safe. Return or
		// space is what acts, and it does exactly what a click would.
		bool hasCursor() const;
		LedKeyboard::Key cursorKey() const;
		// Raised when the cursor lands on a key, so the window can say
		// which key it is and what colour it holds. It is the only report
		// a person navigating this way gets.
		type_signal_key signal_cursor_moved();
		// The CLI name of a key ("g", "enter"), which is what the window
		// calls keys everywhere it names one.
		std::string keyName(LedKeyboard::Key key) const;

		std::vector<LedKeyboard::Key> getSelectedKeys() const;
		void clearSelection();
		// Selection helpers for the context menu: "everything on this row"
		// and "everything that is currently this color" are the two the
		// grid can answer without the user clicking key by key.
		void selectRowOf(LedKeyboard::Key key);
		void selectSameColorAs(LedKeyboard::Key key);

		// Draft colors (the on-screen state, not the device state).
		void setKeyColor(LedKeyboard::Key key, const Gdk::RGBA &color);
		void setAllColors(const Gdk::RGBA &color);
		bool getKeyColor(LedKeyboard::Key key, Gdk::RGBA &color) const;
		Gdk::RGBA getColorAtPress() const;
		const std::map<LedKeyboard::Key, Gdk::RGBA>& getKeyColors() const;
		void cancelDrag();

		void setPreviewColor(LedKeyboard::Key key, const Gdk::RGBA &color);
		void clearPreviewKey(LedKeyboard::Key key);
		void clearPreview();
		void applyPreviewFrame(const LedKeyboard::KeyValueArray &values);

		// Pending marks (draft != applied, managed by MainWindow).
		void markPending(LedKeyboard::Key key, bool pending);
		void clearPendingMarks();

	public:
		// The layout tables live in gui/KeyLayout.h so the 3D scene can
		// extrude the same rectangles this grid lays out.
		typedef keylayout::KeySpec KeySpec;
		typedef keylayout::KeyRow KeyRow;
		// The layout resolved to placed caps, for anything that needs to
		// draw this keyboard rather than lay out widgets.
		std::vector<keylayout::Cap> layoutCaps() const;

	protected:
		// The board asks for a size any pane can give it and fills
		// whatever it gets. Left to its children it asked either for a
		// square (the model reports nothing, so an aspect frame's 1:1
		// won) or for 1074px (the grid's legends), and the pane it lives
		// in can give neither.
		void get_preferred_width_vfunc(int &minimum, int &natural) const override;
		void get_preferred_height_vfunc(int &minimum, int &natural) const override;

	private:

		std::vector<KeyRow> buildLayoutRows() const;
		void rebuildLayout();
		bool onKeyButtonPress(GdkEventButton *event, LedKeyboard::Key key);
		bool onKeyButtonMotion(GdkEventMotion *event, LedKeyboard::Key key);
		bool onKeyButtonRelease(GdkEventButton *event, LedKeyboard::Key key);
		// The scene is one surface, so these ask it which key is under
		// the pointer and then run the very same logic the grid's
		// per-key handlers do — the gestures are shared, not copied.
		bool onScenePress(GdkEventButton *event);
		bool onSceneMotion(GdkEventMotion *event);
		bool onSceneRelease(GdkEventButton *event);
		bool onSceneScroll(GdkEventScroll *event);
		// One handler for both views: which of the two happens to be
		// drawing is not something the arrow keys should depend on.
		bool onBoardKeyPress(GdkEventKey *event);
		// Nearest key in a direction, measured in the grid's own units
		// (whole rows, quarter-unit columns). dx and dy are -1, 0 or 1.
		bool moveCursor(int dx, int dy);
		// Where the cursor goes when the board is tabbed onto with none
		// set: the top-left key, which is Esc on every board here.
		void ensureCursor();
		void setCursorKey(LedKeyboard::Key key, bool announce);
		// The mark is drawn only while the board has the keyboard focus.
		// A ring left on a key after the focus has gone elsewhere is a
		// claim about where typing will land that is no longer true.
		void showCursorMark(bool shown);
		void onBoardFocus(bool in);
		bool pressOnKey(GdkEventButton *event, LedKeyboard::Key key,
		                double x, double y);
		void onSceneReady();
		// Hands the board back to the grid, from an idle: see onSceneReady.
		bool fallbackToGrid();

		bool onGridButtonPress(GdkEventButton *event);
		bool onGridButtonMotion(GdkEventMotion *event);
		bool onGridButtonRelease(GdkEventButton *event);
		bool onOverlayPress(GdkEventButton *event);
		bool onOverlayMotion(GdkEventMotion *event);
		bool onOverlayRelease(GdkEventButton *event);

		void applyBaseStyles(Gtk::EventBox *box, bool isLed);
		// Legends are sized from the keycap, not from the theme: a board
		// drawn 500px wide and one drawn 960px wide are the same picture
		// at two scales, so the lettering has to scale with it.
		void fitLegends(int gridWidth);

		void beginDrag(double x, double y, bool hasKey, bool shiftUnion);
		void updateDrag(double x, double y);
		void endDrag();
		void grabDrag();
		void ungrabDrag();
		void toggleSelection(LedKeyboard::Key key);
		void setSelection(const std::set<LedKeyboard::Key> &selection);
		void setKeySelected(LedKeyboard::Key key, bool selected);
		bool keyButtonRect(LedKeyboard::Key key, Gdk::Rectangle &rect);
		// Which key is under a point, in the coordinates the drag works
		// in. The model answers exactly; the grid is asked cell by cell.
		bool keyAtPoint(double x, double y, LedKeyboard::Key &key);
		// Say that a gesture moved the board, at most twice a second.
		void saidViewMoved(const char *verb);
		// One key of a paint stroke, painted at most once per stroke.
		void strokeOnto(LedKeyboard::Key key);
		void paintKey(LedKeyboard::Key key);
		void syncFlags(LedKeyboard::Key key);

		// The grid and the overlay are both windowless, so a press on the
		// gaps between keycaps would be delivered to the enclosing
		// viewport and never reach us. This event box gives the widget's
		// interior a GdkWindow, which is what makes a rubber-band drag
		// startable anywhere on the board.
		KeyPlate m_gridBox;
		Gtk::Grid m_grid;
		// The layout in keycap units — quarter-unit columns and whole
		// rows — which is where the board's shape comes from.
		int m_layoutColumns = 94;
		int m_layoutRows = 7;
		// Every legend, and the size they are currently drawn at, so a
		// resize that does not change the size costs nothing.
		std::vector<Gtk::Label*> m_keyLabels;
		std::vector<Gtk::Label*> m_ledLabels;
		int m_legendPx = 0;
		// The board is drawn either as this grid of widgets or as the
		// scene; the state above them, and every gesture, is the same.
		// m_sceneWidget is built once and kept; m_scene is it when the
		// model is the view being shown, and null when the grid is.
		Gtk::EventBox m_sceneBox;
		KeyboardScene *m_sceneWidget = nullptr;
		KeyboardScene *m_scene = nullptr;
		bool m_use3D = false;
		bool m_sceneFailed = false;
		// Where the pointer went down, for telling a right-click apart
		// from a drag that turns the board.
		double m_orbitX = 0, m_orbitY = 0;
		bool m_orbiting = false;
		bool m_orbitMoved = false;
		LedKeyboard::Key m_orbitKey = LedKeyboard::Key::a;
		bool m_orbitOnKey = false;
		bool m_panning = false;
		BandOverlay *m_bandOverlay;
		std::map<LedKeyboard::Key, KeyCap*> m_buttons;
		// One CSS provider per keycap, for the legend colour only: the
		// keycap colour itself is drawn (see KeyCap). contrastingText()
		// picks between pure black and pure white, so this only reloads
		// when that choice flips — a handful of times a second across
		// the board, not once per key per frame.
		std::map<LedKeyboard::Key, Glib::RefPtr<Gtk::CssProvider>> m_keyStyles;
		std::map<LedKeyboard::Key, std::string> m_keyTextColor;
		std::map<LedKeyboard::Key, Gdk::RGBA> m_colors;
		std::map<LedKeyboard::Key, Gdk::RGBA> m_preview;
		std::map<LedKeyboard::Key, KeyPosition> m_keyPositions;
		// The CLI name of each key, which the flat grid puts in a
		// per-key tooltip and the scene shows on hover.
		std::map<LedKeyboard::Key, std::string> m_keyNames;
		LedKeyboard::Key m_hoverKey = LedKeyboard::Key::a;
		bool m_hoverValid = false;
		// Where the arrow keys are standing, and whether that is being
		// drawn. The two are separate because the place survives losing
		// the focus and coming back to it: tabbing away and back must not
		// send you to the top-left corner again.
		LedKeyboard::Key m_cursorKey = LedKeyboard::Key::a;
		bool m_cursorValid = false;
		bool m_cursorShown = false;
		std::set<LedKeyboard::Key> m_selected;
		// Pending marks were only ever a CSS class; the model needs them
		// as state it can be told about.
		std::set<LedKeyboard::Key> m_pending;
		std::set<LedKeyboard::Key> m_selectionAtDragStart;
		LayoutFlags m_flags;
		Mode m_mode = PAINT;
		// A paint stroke: which keys it has already covered, so dragging
		// back over one does not stage it twice.
		bool m_stroking = false;
		std::set<LedKeyboard::Key> m_stroked;

		// Rubber-band drag state (coordinates in overlay space).
		double m_dragOriginX = 0, m_dragOriginY = 0;
		double m_dragCurrentX = 0, m_dragCurrentY = 0;
		bool m_dragStarted = false;
		bool m_dragActive = false;
		bool m_dragHasKey = false;   // press came from a key (paints on click)
		bool m_dragShiftUnion = false;
		bool m_dragGrabbed = false;
		LedKeyboard::Key m_dragKey = LedKeyboard::Key::a;  // valid if m_dragHasKey
		Gdk::RGBA m_colorAtPress = Gdk::RGBA("#000000");

		type_signal_key m_signal_key_pressed;
		type_signal_key m_signal_key_pick;
		type_signal_key m_signal_key_cleared;
		type_signal_key m_signal_key_menu;
		type_signal_key m_signal_cursor_moved;
		type_signal_void m_signal_selection_changed;
		type_signal_void m_signal_view_changed;
		type_signal_void m_signal_focus_changed;
		type_signal_text m_signal_view_moved;
		// When the foot was last told the board had moved. A drag raises
		// motion at the rate the screen refreshes and a touchpad raises
		// scroll events faster still; the same sentence written sixty
		// times a second is sixty timeouts and no more information.
		gint64 m_viewMovedSaid = 0;
		// The handlers that raise the one above, kept so the destructor
		// can cut them before the members they read are gone.
		std::vector<sigc::connection> m_focusWatch;
		type_signal_void m_signal_stroke_begin;
		type_signal_void m_signal_stroke_end;
};

// The stage: the well the board is shown in, dressed by the stylesheet
// (.kb-stage) and shaped here.
//
// The shape it asks for is the shape of the keyboard standing on it
// rather than the shape of the pane, and how deep that is depends on
// which view is drawing: the flat board is seen from directly above and
// is as deep as a keyboard, while the model is tilted towards you and
// its silhouette is nearly half as deep as it is wide.
//
// Asks, and does not get. The pane hands the stage the whole column,
// deliberately — a well cut to a keyboard leaves a quarter of the pane
// bare underneath it, which was measured and looked like something that
// had failed to load. So the well is deeper than either board needs and
// both stand letterboxed in it, and this shape survives as the minimum
// the stage will accept and as the floor under a very short window.
// Neither view can be made to fill that depth without stretching a
// keycap out of square; what the flat board does instead is stand on the
// same plate the model does, so that what is in the well is a keyboard
// with a body and the floor around it is floor.
class BoardStage : public Gtk::ScrolledWindow {
	public:
		explicit BoardStage(KeyboardWidget &board);

	protected:
		Gtk::SizeRequestMode get_request_mode_vfunc() const override;
		void get_preferred_height_vfunc(int &minimum, int &natural) const override;
		void get_preferred_height_for_width_vfunc(int width, int &minimum,
		                                          int &natural) const override;
		// The focus ring for the board belongs to the well and not to the
		// board itself: the model is blitted over the widget that holds
		// it, so a ring drawn inside the well would be painted over. The
		// rim is outside the picture and can always be seen.
		bool on_draw(const Cairo::RefPtr<Cairo::Context> &context) override;

	private:
		// What the rim and the padding around the well cost, so the
		// keyboard inside it is the shape it should be and not the well.
		void frame(int &horizontal, int &vertical) const;

		KeyboardWidget &m_board;
};

#endif

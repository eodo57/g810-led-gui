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
#include <cstdint>
#include <fstream>
#include <functional>
#include <sstream>

#include <glib/gstdio.h>

#include "MainWindowShared.h"

bool MainWindow::has(help::KeyboardFeatures flag) const {
	return help::hasFeature(m_features, flag);
}

// The code-level allowlist in Keyboard.cpp is authoritative here:
// setStartupMode returns false for g512/g513/g815 despite help.h.
bool MainWindow::startupModeSupported(LedKeyboard::KeyboardModel model) {
	switch (model) {
		case LedKeyboard::KeyboardModel::g213:
		case LedKeyboard::KeyboardModel::g410:
		case LedKeyboard::KeyboardModel::g512:
		case LedKeyboard::KeyboardModel::g513:
		case LedKeyboard::KeyboardModel::g610:
		case LedKeyboard::KeyboardModel::g810:
		case LedKeyboard::KeyboardModel::g910:
		case LedKeyboard::KeyboardModel::gpro:
			return true;
		default:
			return false;
	}
}

KeyboardWidget::LayoutFlags MainWindow::layoutFlagsFor(
		help::KeyboardFeatures features, LedKeyboard::KeyboardModel model) {
	KeyboardWidget::LayoutFlags flags;
	flags.rgb = help::hasFeature(features, help::KeyboardFeatures::rgb);
	flags.intensity = help::hasFeature(features, help::KeyboardFeatures::intensity);
	flags.logo1 = help::hasFeature(features, help::KeyboardFeatures::logo1);
	flags.logo2 = help::hasFeature(features, help::KeyboardFeatures::logo2);
	flags.multimedia = help::hasFeature(features, help::KeyboardFeatures::multimedia);
	// The gkeys feature bit says "has G-keys", not how many: only the
	// g910 exposes g6-g9, and the g815's firmware ignores them.
	flags.gkeyCount = help::hasFeature(features, help::KeyboardFeatures::gkeys) ?
		(model == LedKeyboard::KeyboardModel::g910 ? 9 : 5) : 0;
	flags.dropsIndicatorsAndStop = (model == LedKeyboard::KeyboardModel::g815);
	flags.numpad = help::hasFeature(features, help::KeyboardFeatures::numpad);
	flags.setindicators = help::hasFeature(features, help::KeyboardFeatures::setindicators);
	flags.setkey = help::hasFeature(features, help::KeyboardFeatures::setkey);
	return flags;
}

MainWindow::MainWindow() :
 	m_applyKeyboardButton("Se_nd to keyboard"),
 	m_revertButton("_Discard changes"),
 	m_rescanButton("_Rescan"),
 	m_listKeyboardsButton("L_ist"),
 	m_printDeviceButton("_Print info"),
 	m_intensityScale(Gtk::Adjustment::create(255.0, 0.0, 255.0, 1.0, 8.0)),
 	m_paintAllButton("All ke_ys"),
 	m_paintSelectionButton("Pa_int"),
 	m_clearSelectionButton("_Clear"),
 	m_deselectButton("D_eselect"),
 	m_periodSpin(Gtk::Adjustment::create(1000.0, 256.0, 65000.0, 256.0, 256.0)),
 	m_storeCheck("Store to on-board lighting"),
 	m_applyEffectButton("Apply e_ffect"),
 	m_effectOffButton("Turn effect _off"),
 	m_applyGKeysButton("Apply _G-keys"),
 	m_applyStartupButton("Appl_y startup mode"),
 	m_applyOnBoardButton("Apply on-_board mode"),

 	m_liveToggleButton("Sta_rt"),
 	m_wavePeriodScale(Gtk::Adjustment::create(1000.0, 250.0, 8000.0, 50.0, 250.0)),
 	m_waveMinScale(Gtk::Adjustment::create(15.0, 0.0, 100.0, 1.0, 5.0)),
 	m_waveMaxScale(Gtk::Adjustment::create(100.0, 0.0, 100.0, 1.0, 5.0)),
 	m_audioRefreshButton("Refres_h"),
 	m_audioGainScale(Gtk::Adjustment::create(100.0, 25.0, 400.0, 5.0, 25.0)),
 	m_audioSmoothScale(Gtk::Adjustment::create(45.0, 0.0, 95.0, 1.0, 5.0)),
 	m_audioAutoGainCheck("Auto _gain (follow the loudest recent peak)"),
 	m_audioOverlayCheck("_Blend over the current colors"),
 	m_screenBoostScale(Gtk::Adjustment::create(55.0, 0.0, 100.0, 1.0, 5.0)),
 	m_screenSmoothScale(Gtk::Adjustment::create(50.0, 0.0, 95.0, 1.0, 5.0)),
 	m_screenOverlayCheck("Blend o_ver the current colors"),
 	m_screenForgetButton("Choose a different scree_n…") {
	m_applyKeyboardButton.set_use_underline(true);
	m_revertButton.set_use_underline(true);
	m_rescanButton.set_use_underline(true);
	m_listKeyboardsButton.set_use_underline(true);
	m_printDeviceButton.set_use_underline(true);
	m_paintAllButton.set_use_underline(true);
	m_paintSelectionButton.set_use_underline(true);
	m_clearSelectionButton.set_use_underline(true);
	m_deselectButton.set_use_underline(true);
	m_applyEffectButton.set_use_underline(true);
	m_effectOffButton.set_use_underline(true);
	m_applyGKeysButton.set_use_underline(true);
	m_applyStartupButton.set_use_underline(true);
	m_applyOnBoardButton.set_use_underline(true);
	m_audioRefreshButton.set_use_underline(true);
	m_audioAutoGainCheck.set_use_underline(true);
	m_audioOverlayCheck.set_use_underline(true);
	m_screenOverlayCheck.set_use_underline(true);
	m_screenForgetButton.set_use_underline(true);
	m_manualDeviceCheck.set_use_underline(true);

	set_title("g810-led");
	// Wide enough for the board and the control column both. It used to
	// open at 1150x640, which left the column 358px once the pane took
	// its 780: the primary button of the Live tab was sliced through by
	// the panel edge and two of the four tabs hid behind scroll chevrons.
	// Resizing down was always fine — only the size it opened at was not.
	set_default_size(1500, 860);
	set_border_width(6);

	buildUI();
	setupSignals();
	setupAccelerators();

	show_all_children();

	// show_all_children() re-shows every frame the section builders hid,
	// so collapse them again for the "nothing connected yet" state.
	refreshFeatures();
	setControlsEnabled(false);
	onSelectionChanged();
	updatePendingState();
	updateWaveDescription();
	refreshAudioSources();
	updateScreenCaption();
	buildTrayMenu();
	rebuildRecentMenu();
	rescanDevices();
	if (rainDaemonAlive()) {
		RaindropAnimation::State state;
		if (RaindropAnimation::loadState(rainStatePath(), state))
			m_animOverlay = state.overlay;
		// Watch it like our own effect: a daemon inherited from an
		// earlier session can exit at any time, and until we notice, the
		// window keeps a "Stop" button that would start a second one.
		noteAnimationStarted(m_animOverlay ? "Color rain" : "Raindrop", true);
		updateAnimUI();
	}
	// A daemon from a previous session may still be driving the board;
	// show its settings so the controls match what is running.
	if (audioDaemonAlive()) {
		AudioPlayer::State state;
		if (AudioPlayer::loadState(audioStatePath(), state))
			applyAudioState(state);
		noteAnimationStarted("Sound reactive", true);
		updateAnimUI();
	}
	// Likewise for a screen capture the user chose to keep running on
	// the way out of an earlier session: show its settings, and let
	// Stop be the way to end it.
	if (screenDaemonAlive()) {
		ScreenPlayer::State state;
		if (ScreenPlayer::loadState(screenStatePath(), state))
			applyScreenState(state);
		noteAnimationStarted("Screen colors", true);
		updateAnimUI();
	}
}

MainWindow::~MainWindow() {
	if (m_animationTimer.connected())
		m_animationTimer.disconnect();
	// The animation writer threads hold m_kbd; stop them before the
	// device goes away under them.
	m_audioPlayer.stop();
	m_screenPlayer.stop();
	m_wavePlayer.stop();
	m_raindrop.stop();
	m_kbd.close();
}

void MainWindow::buildUI() {
	Gtk::Box *vbox = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_VERTICAL, 4));
	add(*vbox);

	buildMenuBar(vbox);

	Gtk::Paned *paned = Gtk::manage(new Gtk::Paned(Gtk::ORIENTATION_HORIZONTAL));
	paned->set_position(780);
	paned->set_wide_handle(true);
	vbox->pack_start(*paned, true, true);

	Gtk::Box *rightCol = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_VERTICAL, 4));
	rightCol->set_hexpand(true);
	// resize, but do not shrink: the board may take the room it is given
	// and may not take the column's. A handle dragged to the right edge
	// would otherwise clip the controls the way the old default window
	// size did.
	paned->pack2(*rightCol, true, false);

	buildLeftSide(paned);
	buildRightSide(rightCol);

	// Tab crosses the window the way the eye does: the board and its
	// tools, then the column beside them.
	//
	// It has to be said out loud here, because GTK works the order out
	// from geometry and cannot read this one. A GtkPaned gives each half
	// its own GdkWindow, so both halves report an allocation starting at
	// x=0, and the sort that should have compared "which is further left"
	// compares two zeroes and falls through to width — putting whichever
	// half happens to be narrower first. Measured: the right column is
	// 697px against the left's 780, so Tab from the top of the window
	// landed in the middle of the Colors tab, and the board — the subject
	// of the whole window — was the last stop before the cycle wrapped.
	// Drag the handle far enough and the order would silently swap over.
	//
	// set_focus_chain is deprecated in 3.24 and has no replacement in
	// GTK 3; the alternative is a focus order nobody chose.
	std::vector<Gtk::Widget*> order;
	order.push_back(m_leftBox);
	order.push_back(rightCol);
	paned->set_focus_chain(order);

	// --- Status bar
	m_statusPtr = &m_statusbar;
	m_statusContextId = m_statusbar.get_context_id("main");
	vbox->pack_start(m_statusbar, false, false);
	// Nothing is said here at startup. The foot reports what just
	// happened, and at this point nothing has: the scan two lines further
	// down the constructor is the first event of the session and pushes
	// its own line over anything written now. What used to be here was a
	// three-clause instruction that no one ever saw, and it named the one
	// act in this window twice more — "stage" and "Apply" — than the two
	// buttons it was describing.
}

// A control that is switched off is a question — "why can't I?" — and
// the answer belongs on the control, where the pointer already is. Both
// history buttons spend most of a session off, so each carries two
// sentences and swaps them the moment its state does.
static void sayWhyWhenOff(Gtk::Button &button, const char *alive,
                          const char *dead) {
	button.set_tooltip_text(button.get_sensitive() ? alive : dead);
	button.property_sensitive().signal_changed().connect(
		[&button, alive, dead]() {
			button.set_tooltip_text(button.get_sensitive() ? alive : dead);
		});
}

// The row along the top: file handling in a menu, and the two things you
// reach for most often — undo and redo — named at the far end of it.
void MainWindow::buildMenuBar(Gtk::Box* vbox) {
	struct Entry {
		Gtk::MenuItem *item;
		const char *label;
		std::function<void()> action;
	};
	// Three ways in, then the two ways out, then leaving — with a rule
	// between each group. They were one undivided run of five before, and
	// "Load text…" sat below Save as… where nothing suggested it was
	// another way of loading; renamed, it says what it takes and stands
	// with the other two that take it.
	//
	// "Colors", not "profile". A profile is a file on disk and the file
	// chooser is welcome to call it one; what these five items act on is
	// the thing every other line in this window calls your colors, and
	// one act may not have two names. This menu was the only place the
	// other word appeared.
	const Entry fileEntries[] = {
		{&m_loadItem,     "_Open colors…",         [this]() { onLoadProfile(); }},
		{&m_recentItem,   "_Recent",               std::function<void()>()},
		{&m_loadTextItem, "_Paste colors as text…", [this]() { onLoadProfileText(); }},
		{NULL,            NULL,                    std::function<void()>()},
		{&m_saveItem,     "_Save colors",          [this]() { onSaveCurrent(); }},
		{&m_saveAsItem,   "Save colors _as…",      [this]() { onSaveProfile(); }},
		{NULL,            NULL,                    std::function<void()>()},
		// close(), not hide(): it raises delete-event, which is where the
		// "you have unsent changes" prompt lives.
		{&m_quitItem,     "_Quit",                 [this]() { close(); }},
	};
	for (const Entry &entry : fileEntries) {
		if (!entry.item) {
			m_fileMenu.append(*Gtk::manage(new Gtk::SeparatorMenuItem()));
			continue;
		}
		entry.item->set_label(entry.label);
		entry.item->set_use_underline(true);
		if (entry.action)
			entry.item->signal_activate().connect(entry.action);
		m_fileMenu.append(*entry.item);
	}
	// The recent list is built and refreshed in one place already; the
	// menu only changes owner.
	m_recentItem.set_submenu(m_recentMenu);

	m_fileItem.set_label("_File");
	m_fileItem.set_use_underline(true);
	m_fileItem.set_submenu(m_fileMenu);
	m_menuBar.append(m_fileItem);

	// Mouse gestures on the board are in here with the key combinations,
	// because a person looking for "how do I do that without the mouse"
	// and a person looking for "what else can the mouse do" are the same
	// person with the same question, and one list answers both.
	m_shortcutsItem.set_label("_Shortcuts and gestures");
	m_shortcutsItem.set_use_underline(true);
	m_shortcutsItem.signal_activate().connect(
		sigc::mem_fun(*this, &MainWindow::onShowShortcuts));
	m_helpMenu.append(m_shortcutsItem);
	m_helpItem.set_label("_Help");
	m_helpItem.set_use_underline(true);
	m_helpItem.set_submenu(m_helpMenu);
	m_menuBar.append(m_helpItem);

	// Named, not drawn. These were a pair of stock rotate arrows, and the
	// icons cost more than they saved three times over: the theme this
	// desktop ships draws that name as an arrowhead and a scatter of
	// dots, which is not an arrow at 16px; switched off — which is how
	// every session opens — GTK halves the alpha of a disabled image, so
	// the brightest pixel measured 2.85:1 against the header, under the
	// 3:1 floor a non-text control has to clear; and a screen reader was
	// handed a button with no accessible name at all, the only two such
	// controls in the window. They were also the only two icons in a
	// window that is otherwise entirely words. Two words in the emptiest
	// row of the window fix all three at once.
	m_undoButton.set_label("Undo");
	m_redoButton.set_label("Redo");
	m_undoButton.set_relief(Gtk::RELIEF_NONE);
	m_redoButton.set_relief(Gtk::RELIEF_NONE);
	m_undoButton.set_sensitive(false);
	m_redoButton.set_sensitive(false);
	// Both keep naming the act when they are off. A control you cannot
	// press is exactly the one whose purpose you have to be told, and the
	// reason is added to that rather than put in its place.
	sayWhyWhenOff(m_undoButton, "Undo the last color change (Ctrl+Z)",
		"Undo the last color change — nothing has been changed yet");
	sayWhyWhenOff(m_redoButton, "Redo the change you just undid "
		"(Ctrl+Shift+Z)", "Redo — nothing has been undone yet");

	Gtk::Box *row = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 0));
	row->pack_start(m_menuBar, true, true);
	// Packed from the right, so redo goes first and they read undo, redo.
	row->pack_end(m_redoButton, false, false);
	row->pack_end(m_undoButton, false, false);
	vbox->pack_start(*row, false, false);
}

void MainWindow::buildLeftSide(Gtk::Paned* paned) {
	m_leftBox = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_VERTICAL, 6));
	// The stage takes the width of the pane and the depth a keyboard
	// needs, and sits in the middle of what is left.
	m_keyboardScroll = Gtk::manage(new BoardStage(m_keyboardWidget));
	// The well the board stands in, dressed by the stylesheet. Named
	// rather than reached by its position in the tree, which is what the
	// sheet asks for and what keeps any other scroller in this pane from
	// being dressed as a stage by accident.
	m_keyboardScroll->get_style_context()->add_class("kb-stage");
	m_keyboardScroll->set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
	m_keyboardScroll->set_propagate_natural_width(false);
	m_keyboardScroll->set_propagate_natural_height(false);
	// The width below which the board stops being a keyboard. The stage
	// never asks for more than this, so the pane is free to be any width
	// at all; it never settles for less, so there is no size at which the
	// board is quietly cut off instead.
	m_keyboardScroll->set_min_content_width(300);
	// The stage takes the room between the tools above it and the card
	// below, however much that is, and the board stands in the middle of
	// it. Two earlier arrangements are worth naming, because this is the
	// third: centring the stage inside the pane put 190px of nothing
	// between the Paint/Select buttons and the board they act on, and
	// pinning it to the top moved that nothing to the floor instead —
	// 275px of it at the size this window opens at, 43% of the column,
	// with nothing framing it and nothing in it. A void that large does
	// not read as room, it reads as something that failed to load, and it
	// was worst of all with the flat board, which is a 222px strip and
	// left two thirds of the column bare.
	//
	// Filling is what makes the two views the same picture: whichever is
	// drawing, the well is the same size and the board is in the middle
	// of it. What is around the board now is the stage's own floor, which
	// is dressed, framed and obviously deliberate.
	// Should the board ever be too big for its well after all, that has
	// to be something you can see. An overlay scrollbar is invisible
	// until the pointer is already over it, which is how a third of the
	// keyboard came to be off the edge with nothing on screen saying so.
	m_keyboardScroll->set_overlay_scrolling(false);
	// A GtkScrolledWindow takes focus by default, and this one had
	// nothing to scroll: tabbing into it drew a dotted rectangle the
	// height of the pane over the board and did nothing else. It is here
	// as a last resort for a window too small to hold a keyboard, not as
	// somewhere to arrive.
	m_keyboardScroll->set_can_focus(false);

	// Straight in, with no aspect frame in between. That frame forced the
	// board into a square — and a keyboard is three times as wide as it
	// is deep, so a quarter of the square was keyboard and the rest was
	// black. The board fills the stage now, and each view keeps its own
	// shape inside it: the grid holds the keyboard's proportions, the
	// model fits itself to the frustum.
	m_keyboardScroll->add(m_keyboardWidget);

	// Painting and selecting are separate tools, and which one the left
	// button is holding is stated rather than remembered: clicking a key
	// used to paint it whatever you wanted, and the only way to select
	// without painting was to rubber-band more than one key.
	Gtk::Box *modeRow = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 8));
	modeRow->get_style_context()->add_class("kb-toolbar");
	// Lined up with the stage below it, edge for edge: they are the only
	// two things in this pane, and two rectangles that are nearly the
	// same width read as a mistake rather than as a hierarchy.
	modeRow->set_margin_start(8);
	modeRow->set_margin_end(8);
	Gtk::Box *modeButtons =
		Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 0));
	modeButtons->get_style_context()->add_class("linked");
	m_paintModeButton.set_label("_Paint");
	m_selectModeButton.set_label("_Select");
	m_selectModeButton.join_group(m_paintModeButton);
	for (Gtk::RadioButton *button : {&m_paintModeButton, &m_selectModeButton}) {
		button->set_mode(false);      // a toggle button, not a radio dot
		button->set_use_underline(true);
		modeButtons->pack_start(*button, false, false);
	}
	m_paintModeButton.set_tooltip_text(
		"Click a key to color it; drag across keys to paint them. Drag from "
		"the bare board to rubber-band a selection.");
	m_selectModeButton.set_tooltip_text(
		"Click a key to select just that one; drag to rubber-band a group. "
		"Nothing is painted in this mode.");
	m_paintModeButton.set_active(
		m_keyboardWidget.mode() == KeyboardWidget::PAINT);
	m_selectModeButton.set_active(
		m_keyboardWidget.mode() == KeyboardWidget::SELECT);
	m_paintModeButton.signal_toggled().connect(
		sigc::mem_fun(*this, &MainWindow::onModeToggled));
	// The hint that used to sit beside these buttons was a permanent
	// sentence across the top of the board. What the two tools do is in
	// their tooltips, and the status bar says it the moment you switch,
	// so the board keeps the room instead.
	modeRow->pack_start(*modeButtons, false, false);
	updateModeHint();

	// The board is a model by default; this is the way back to the flat
	// grid, and the way anyone whose machine cannot draw it finds out.
	// It shares the toolbar with the tools rather than holding a row of
	// its own at the bottom of the pane, and its label is two words: the
	// paragraph it used to be was 606px wide, which is what made the left
	// pane too wide for a 980px window and pushed Paint off the screen.
	m_view3DCheck.set_label("_3D board");
	m_view3DCheck.set_use_underline(true);
	// What the switch does, and no more: the gestures themselves are on
	// the board, in its own tooltip, and on the button that undoes them.
	// Said in three places they would drift; said where the hand is, they
	// are found by the person who needs them.
	m_view3DCheck.set_tooltip_text(
		"Show the keyboard as a model you can turn and zoom, instead of a "
		"flat grid seen from above.");
	m_view3DCheck.set_active(m_keyboardWidget.view3D());
	m_view3DCheck.signal_toggled().connect([this]() {
		if (m_viewIgnore)
			return;
		m_keyboardWidget.setView3D(m_view3DCheck.get_active());
		onViewChanged();
		// Which board is now on the stage, in the same shape of sentence
		// both ways round. What the model does under the mouse is not
		// repeated here: it is on the board itself and on Reset view,
		// where it is still there a minute later.
		status(m_keyboardWidget.view3D() ?
			"3D board — the keyboard as a model you can turn and zoom" :
			"Flat board — the whole keyboard, seen from above");
	});
	m_keyboardWidget.signal_view_changed().connect(
		sigc::mem_fun(*this, &MainWindow::onViewChanged));
	modeRow->pack_end(m_view3DCheck, false, false);

	// The way back from a turned or zoomed board, and — in its tooltip —
	// where the gestures that get you there are named. A gesture nobody
	// is told about is a gesture nobody has, and the only statement of
	// these used to be in the handler that runs when 3D is switched
	// *on*, which never runs, because 3D is how the window opens.
	// Alt+V, not Alt+R: the device lane already has Rescan on R, and two
	// live claims on one letter make the letter useless.
	Gtk::Button *resetView = Gtk::manage(new Gtk::Button("Reset _view"));
	resetView->set_use_underline(true);
	resetView->set_tooltip_text(
		"Put the board back the way it started (Ctrl+Home). Drag it with "
		"the right button to turn it, scroll to zoom, middle-drag to slide "
		"it.");
	resetView->signal_clicked().connect([this]() {
		m_keyboardWidget.resetBoardView();
		status("Board back to its starting view");
	});
	modeRow->pack_end(*resetView, false, false);
	// Nothing to reset in the flat grid: it is drawn from directly above
	// and cannot be turned. Hidden rather than disabled, since a control
	// that is permanently dead is worse than one that is not there — and
	// held out of show_all_children(), which would otherwise put it back
	// on a machine that cannot draw the model at all.
	resetView->set_no_show_all(true);
	resetView->set_visible(m_keyboardWidget.view3D());
	m_keyboardWidget.signal_view_changed().connect(
		sigc::track_obj([this, resetView]() {
			resetView->set_visible(m_keyboardWidget.view3D());
		}, *resetView, m_keyboardWidget));
	// Turning or zooming the board says so, and says how to undo it. The
	// board is the one thing in the window a gesture can leave in a state
	// with no visible way out of it.
	m_keyboardWidget.signal_view_moved().connect(
		[this](const Glib::ustring &verb) {
			status("Board " + verb +
				" — press Ctrl+Home, or Reset view, to put it back");
		});

	// The tools stay at the head of the pane and the tray keeps the middle
	// of what is left. Both were tried the other way round — the two
	// packed together as one block — and it costs more than it buys: the
	// tray is cut to the view drawing in it, so a block would move the
	// switch you had just clicked out from under the pointer every time
	// you used it. This way the switch never moves, and neither does the
	// keyboard: what changes when the view is switched is where the rim
	// of the tray is, closing in on whichever board is standing in it.
	m_leftBox->pack_start(*modeRow, false, false);

	// A keyboard with no per-key lighting (a g213) has no board, and the
	// stage is hidden for it. These are the board's tools, so they go
	// with it rather than staying behind with nothing to act on.
	// Tracked against both widgets it touches, not merely connected. The
	// signal belongs to the scroller but the handler writes to the row,
	// and at teardown the row is destroyed first — so an untracked
	// connection would still be live when disposing the scroller hides
	// it, and would write through a freed pointer. sigc::mem_fun on a
	// trackable gets this for free; a lambda holding a raw pointer has to
	// ask for it.
	m_keyboardScroll->property_visible().signal_changed().connect(
		sigc::track_obj([this, modeRow]() {
			modeRow->set_visible(m_keyboardScroll->get_visible());
		}, *modeRow, *m_keyboardScroll));

	// expand, do not fill. The stage works out an aspect-correct height for
	// the width it is given (KeyboardWidget's BoardStage), and filling threw
	// that answer away: at 1500x860 the flat board asked for 252px and was
	// handed 537, so the keyboard floated in 150px of black above and below
	// — the largest region of the window, 42% full. Taking the height it
	// asks for and letting the extra room stay outside it is the whole fix.
	m_leftBox->pack_start(*m_keyboardScroll, true, false);
	// Shown only for a keyboard with no per-key lighting, and held out of
	// show_all: refreshFeatures() is the one place that decides whether
	// there is a board to draw, and a blanket show_all would put "this
	// keyboard has no per-key lighting" underneath a drawn keyboard.
	m_regionHintLabel.set_no_show_all(true);
	m_leftBox->pack_start(m_regionHintLabel, true, true);
	buildSavedSection(m_leftBox);
	// Resizes with the window, but does not shrink past what it holds:
	// the handle stops where the board's own minimum is. Dragging it
	// further used to leave a keyboard with its right-hand end behind the
	// edge of the pane, and the only way back was to find the handle
	// again in a pane that no longer showed you what you had lost.
	paned->pack1(*m_leftBox, true, false);
}

// Keeping the board and fetching it back, under the board it acts on.
//
// This used to exist only in the File menu, under a name — "profile" —
// that appears nowhere else in the window: everything else calls the same
// thing "your colors". So a person who had painted a board and wanted to
// keep it had to guess that "Save profile" meant their colors, and had to
// go looking in a menu to find it, in a window that otherwise puts what
// you can do on the screen. It is one act, it has one name, and it is now
// where the thing it acts on is.
void MainWindow::buildSavedSection(Gtk::Box *parent) {
	Gtk::Box *box = addSection(parent, "Your colors");
	// A card in the left column must never be what decides how tall the
	// pane is: the board above it gives up its floor first.
	if (Gtk::Widget *frame = box->get_parent())
		frame->set_valign(Gtk::ALIGN_END);

	Gtk::Box *row = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 6));
	m_savedAsLabel.set_xalign(0);
	m_savedAsLabel.set_ellipsize(Pango::ELLIPSIZE_MIDDLE);
	m_savedAsLabel.get_style_context()->add_class("kb-hint");
	row->pack_start(m_savedAsLabel, true, true);

	// No mnemonics on these two. Alt+O is already "Turn effect off" and
	// Alt+V is already "Reset view", and both of those are on screen at
	// the same time as these are — two live claims on one letter make the
	// letter useless. They have Ctrl+O and Ctrl+S, which are the keys a
	// person would try first and are listed in the Shortcuts sheet.
	m_openColorsButton.set_label("Open…");
	m_openColorsButton.set_tooltip_text(
		"Open a file of colors and put it on the board (Ctrl+O)");
	m_openColorsButton.signal_clicked().connect(
		sigc::mem_fun(*this, &MainWindow::onLoadProfile));
	m_saveColorsButton.set_label("Save…");
	m_saveColorsButton.signal_clicked().connect(
		sigc::mem_fun(*this, &MainWindow::onSaveCurrent));
	row->pack_start(m_openColorsButton, false, false);
	row->pack_start(m_saveColorsButton, false, false);
	box->pack_start(*row, false, false);

	// The last few files, by name and one press away. A name you can see
	// is worth more than one you have to remember, and this is the only
	// list in the window a person is asked to recognise rather than read.
	m_recentRow = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 4));
	m_recentRow->set_no_show_all(true);
	box->pack_start(*m_recentRow, false, false);
	updateSavedSection();
}

void MainWindow::updateSavedSection() {
	if (!m_recentRow)
		return;
	m_savedAsLabel.set_text(m_currentProfileName.empty() ?
		"Not saved to a file yet" : "From " + m_currentProfileName);
	m_saveColorsButton.set_tooltip_text(m_currentProfileName.empty() ?
		"Keep these colors in a file (Ctrl+S)" :
		"Save these colors back to " + m_currentProfileName + " (Ctrl+S)");

	for (Gtk::Widget *child : m_recentRow->get_children())
		m_recentRow->remove(*child);
	std::vector<std::string> paths = recentProfiles();
	// The file that is already open is not somewhere to go back to.
	paths.erase(std::remove(paths.begin(), paths.end(), m_currentProfilePath),
		paths.end());
	m_recentRow->set_visible(!paths.empty());
	if (paths.empty())
		return;
	Gtk::Label *heading = Gtk::manage(new Gtk::Label("Recent"));
	heading->get_style_context()->add_class("kb-hint");
	m_recentRow->pack_start(*heading, false, false);
	// Shown one by one: the row is held out of show_all (see above), and
	// show_all on a widget that asked to be left out does nothing at all.
	heading->show();
	// Three, not five: this row shares a card with the two buttons above
	// it, and a fourth name pushed the row wider than the narrow pane.
	for (size_t i = 0; i < paths.size() && i < 3; ++i) {
		const std::string path = paths[i];
		std::string name = path.substr(path.find_last_of('/') + 1);
		// The extension is the file system's business. Everywhere else in
		// this window these are called colors, and ".profile" on the end
		// of every one of them says nothing that tells them apart.
		const size_t dot = name.rfind(".profile");
		if (dot != std::string::npos && dot + 8 == name.size())
			name.erase(dot);
		Gtk::Button *item = Gtk::manage(new Gtk::Button(name));
		item->get_style_context()->add_class("kb-quiet");
		item->set_tooltip_text("Open " + path);
		if (Glib::RefPtr<Atk::Object> spoken = item->get_accessible())
			spoken->set_name("Open " + name);
		item->signal_clicked().connect(
			[this, path]() { loadProfileFile(path); });
		m_recentRow->pack_start(*item, false, false);
		item->show();
	}
}

// The one line at the head of a tab that says when that tab's changes
// reach the keyboard. The tabs do not agree about that — Colors and
// On-board effects each wait for their own button, Live effects starts
// the moment you press Start — and it is the thing about this window a
// person is likeliest to get wrong. Written in one place so the notes
// cannot drift apart in wording, size, colour or indent.
//
// Every tab has one and every note is short enough to stay on one line
// at the narrowest window this opens at. Both matter for the same
// reason: the note sits above the first card, so a tab without one, or
// one that wraps to two lines, moves every card on that tab down and the
// content jumps as you switch tabs.
static void addLaneNote(Gtk::Box* tab, const char *note) {
	Gtk::Label *label = Gtk::manage(new Gtk::Label(note));
	label->get_style_context()->add_class("dim-label");
	label->set_xalign(0);
	// Wrapping is still allowed, because a clipped sentence is worse than
	// a moved card; the strings are what keep it from happening.
	label->set_line_wrap(true);
	label->set_margin_start(8);
	label->set_margin_end(8);
	label->set_margin_top(4);
	tab->pack_start(*label, false, false);
}

void MainWindow::buildRightSide(Gtk::Box* rightCol) {
	buildStateStrip(rightCol);

	// The two buttons go straight under the strip, with no card and no
	// heading around them. They used to stand in a frame headed
	// "Profile", which named the wrong thing: profiles are files, they
	// are loaded and saved from the File menu, and nothing in that box
	// touched one. What the pair actually acts on is the count in the
	// line directly above — "3 unsent changes" — so they belong against
	// it rather than boxed off from it behind a title.
	buildApplySection(rightCol);

	Gtk::Notebook *tabs = nullptr;
	Gtk::Box *colorsTab = nullptr;
	Gtk::Box *effectTab = nullptr;
	Gtk::Box *liveTab = nullptr;
	Gtk::Box *deviceTab = nullptr;
	buildTabs(rightCol, tabs, colorsTab, effectTab, liveTab, deviceTab);

	// This tab is the odd one out: nothing here is a colour, and what it
	// sets the keyboard keeps after the computer is gone. It was also the
	// only tab with no note at all, which left its first card 54px higher
	// than the others' and made every switch to it a jolt.
	addLaneNote(deviceTab, "About this keyboard, and what it does on its own");

	// --- Device section
	Gtk::Box *deviceBox = this->addSection(deviceTab, "Device");
	buildDeviceSection(deviceBox);

	// Which lane this tab is, in the words its own button uses. This one
	// is the only lane in the window that waits, which is why it says so.
	addLaneNote(colorsTab, "Held here until you press Send to keyboard");

	// --- Color section (rgb picker or intensity slider)
	Gtk::Box *colorBox = this->addSection(colorsTab, "Color");
	buildColorSection(colorBox);

	// --- Paint section (stages into the draft)
	Gtk::Box *paintBox = this->addSection(colorsTab, "Paint");
	buildPaintSection(paintBox);

	// --- Effects section. This lane has its own button, and this note
	// names it. It used to open "Applies immediately", which was the one
	// outright false sentence in the window: choosing an effect here only
	// draws it on the board, and the status line said so 500px below
	// ("nothing is sent until you press Apply effect") while this line
	// said the opposite. The button is called Apply effect; the note now
	// agrees with the button and with the status line.
	addLaneNote(effectTab, "Runs on the keyboard once you press Apply effect");

	Gtk::Box *effectBox = this->addSection(effectTab, "Keyboard effect");
	buildEffectsSection(effectBox);

	// --- G Keys section (g910/g815, instant lane)
	Gtk::Box *gKeysBox = this->addSection(deviceTab, "M / G keys");
	buildGKeysSection(gKeysBox);

	// --- Startup mode section (instant lane)
	Gtk::Box *startupBox = this->addSection(deviceTab, "Startup mode");
	buildStartupSection(startupBox);

	// --- On-board mode section (g815, instant lane)
	Gtk::Box *onBoardBox = this->addSection(deviceTab, "Lighting control");
	buildOnBoardSection(onBoardBox);

	// --- Live effects: raindrop / wave / sound-reactive as one chooser
	addLaneNote(liveTab, "Driven by this computer, one effect at a time");

	Gtk::Box *liveBox = this->addSection(liveTab, "Live effects");
	buildLiveSection(liveBox);

	// --- Regions section (g213, staged lane)
	Gtk::Box *regionsBox = this->addSection(colorsTab, "Regions");
	buildRegionsSection(regionsBox);
}

// The tray menu is drawn by the desktop, not by us: these are ordinary
// GTK widgets, exported over the bus by the appindicator library. Built
// once, then kept current by updateTray().
void MainWindow::buildTrayMenu() {
	if (!TrayIcon::available())
		return;
	m_trayStatusItem.set_sensitive(false);
	m_trayDeviceItem.set_sensitive(false);
	m_trayShowItem.set_label("Show window");
	m_trayStopItem.set_label("Stop effect");
	m_trayLaneItem.set_label("Live effect");
	m_trayModeItem.set_label("Mode");
	m_trayQuitItem.set_label("Quit");

	m_trayMenu.append(m_trayStatusItem);
	m_trayMenu.append(m_trayDeviceItem);
	m_trayMenu.append(*Gtk::manage(new Gtk::SeparatorMenuItem()));
	m_trayMenu.append(m_trayShowItem);
	m_trayMenu.append(m_trayStopItem);
	m_trayMenu.append(m_trayLaneItem);
	m_trayMenu.append(m_trayModeItem);
	m_trayMenu.append(*Gtk::manage(new Gtk::SeparatorMenuItem()));
	m_trayMenu.append(m_trayQuitItem);

	m_trayShowItem.signal_activate().connect(
		sigc::mem_fun(*this, &MainWindow::showFromTray));
	m_trayStopItem.signal_activate().connect(
		sigc::mem_fun(*this, &MainWindow::stopLive));
	m_trayQuitItem.signal_activate().connect(
		sigc::mem_fun(*this, &MainWindow::quitFromTray));
	m_trayMenu.show_all();
}

// One submenu built from a combo box the window already has, so the
// tray can never offer a choice the window does not — or miss one.
void MainWindow::fillSubmenuFromCombo(Gtk::MenuItem &item, Gtk::Menu *&menu,
                                      Gtk::ComboBoxText &combo,
                                      const sigc::slot<void, int> &chosen) {
	if (menu) {
		item.unset_submenu();
		delete menu;
	}
	menu = new Gtk::Menu();
	Glib::RefPtr<Gtk::TreeModel> model = combo.get_model();
	const int active = combo.get_active_row_number();
	Gtk::RadioButtonGroup group;
	int row = 0;
	if (model) {
		for (Gtk::TreeModel::iterator it = model->children().begin();
		     it != model->children().end(); ++it, ++row) {
			Glib::ustring text;
			it->get_value(0, text);
			Gtk::RadioMenuItem *entry =
				Gtk::manage(new Gtk::RadioMenuItem(group, text));
			entry->set_active(row == active);
			entry->signal_activate().connect([this, chosen, row, entry]() {
				// set_active() while filling fires this too.
				if (m_trayIgnore || !entry->get_active())
					return;
				chosen(row);
			});
			menu->append(*entry);
		}
	}
	menu->show_all();
	item.set_submenu(*menu);
	item.set_visible(row > 1);
}

void MainWindow::updateTray() {
	if (!m_tray.visible())
		return;
	bool sensor = false;
	const std::string board = boardStateText(&sensor);
	m_trayStatusItem.set_label(board);
	m_trayDeviceItem.set_label(m_deviceIsOpen ?
		m_kbd.getCurrentDevice().product : std::string("No keyboard connected"));
	m_trayStopItem.set_sensitive(rainActive());
	m_tray.setTooltip("g810-led — " + board);

	// The modes on offer belong to whichever lane is selected; raindrop
	// has none, and then the item is not shown at all.
	const int lane = m_liveModeCombo.get_active_row_number();
	Gtk::ComboBoxText *modes = lane == 2 ? &m_waveShapeCombo :
		lane == 3 ? &m_audioModeCombo : lane == 4 ? &m_screenModeCombo : NULL;
	const int mode = modes ? modes->get_active_row_number() : -1;
	// Only when something actually moved: rebuilding a menu re-exports
	// it over the bus, and doing that behind an open menu would make it
	// flicker under the pointer.
	if (lane == m_trayLaneShown && mode == m_trayModeShown)
		return;
	m_trayLaneShown = lane;
	m_trayModeShown = mode;

	m_trayIgnore = true;
	fillSubmenuFromCombo(m_trayLaneItem, m_trayLaneMenu, m_liveModeCombo,
		sigc::mem_fun(*this, &MainWindow::chooseLaneFromTray));
	if (modes)
		fillSubmenuFromCombo(m_trayModeItem, m_trayModeMenu, *modes,
			sigc::mem_fun(*this, &MainWindow::chooseModeFromTray));
	else
		m_trayModeItem.set_visible(false);
	m_trayIgnore = false;
}

void MainWindow::chooseLaneFromTray(int row) {
	m_liveModeCombo.set_active(row);   // onLiveModeChanged restarts the lane
	if (!rainActive())
		startSelectedLive();
	updateTray();
}

void MainWindow::chooseModeFromTray(int row) {
	const int lane = m_liveModeCombo.get_active_row_number();
	if (lane == 2)
		m_waveShapeCombo.set_active(row);
	else if (lane == 3)
		m_audioModeCombo.set_active(row);
	else if (lane == 4)
		m_screenModeCombo.set_active(row);
	updateTray();
}

void MainWindow::showFromTray() {
	// The icon stands in for the window; with the window back there is
	// nothing for it to stand in for.
	m_tray.hide();
	present();
}

void MainWindow::quitFromTray() {
	// Quit means quit: the board goes back to the user's colors rather
	// than keeping whatever the effect last wrote.
	if (rainActive())
		stopRainEverywhere();
	restoreAppliedState();
	m_tray.hide();
	Gtk::Main::quit();
}

// Keeps the checkbox honest about which board is actually showing, and
// says something the one time it is not what was asked for.
void MainWindow::onViewChanged() {
	m_viewIgnore = true;
	m_view3DCheck.set_active(m_keyboardWidget.view3D());
	m_viewIgnore = false;
	m_view3DCheck.set_sensitive(m_keyboardWidget.view3DPossible() ||
		m_keyboardWidget.view3D());
	if (!m_keyboardWidget.view3D() && !m_keyboardWidget.view3DPossible() &&
	    !m_viewFallbackSaid) {
		m_viewFallbackSaid = true;
		status("This computer cannot draw the 3D board — showing the flat one");
	}
}

void MainWindow::onModeToggled() {
	if (m_modeIgnore)
		return;
	const bool paint = m_paintModeButton.get_active();
	m_keyboardWidget.setMode(paint ?
		KeyboardWidget::PAINT : KeyboardWidget::SELECT);
	updateModeHint();
	status(paint ? "Paint: click or drag across keys to color them"
	             : "Select: click or drag to choose keys — nothing is painted");
}

// Keeps the two tool buttons agreeing with the tool the board is
// actually holding, whoever changed it.
void MainWindow::updateModeHint() {
	const bool paint = m_keyboardWidget.mode() == KeyboardWidget::PAINT;
	m_modeIgnore = true;
	m_paintModeButton.set_active(paint);
	m_selectModeButton.set_active(!paint);
	m_modeIgnore = false;
}

void MainWindow::buildApplySection(Gtk::Box* applyBox) {
	Gtk::Grid *applyRow = Gtk::manage(new Gtk::Grid());
	applyRow->set_column_spacing(4);
	// Lined up with the strip above it, which is the fact these two act
	// on, and held off the tabs below.
	applyRow->set_margin_start(8);
	applyRow->set_margin_end(8);
	applyRow->set_margin_top(8);
	applyRow->set_margin_bottom(4);
	// Nothing is said about Send here. Whether it can be pressed and why
	// not are worked out in updateBoardState(), which writes the answer
	// straight onto the button; a second sentence set once at build time
	// would be the same control described by two owners, and the loser
	// would be whichever ran last.
	m_revertButton.set_tooltip_text(
		"Discard the unsent changes — the board goes back to the colors "
		"the keyboard is showing");
	// The one action this window exists for gets the platform's
	// primary styling; everything else stays quiet.
	m_applyKeyboardButton.get_style_context()->add_class("suggested-action");
	m_applyKeyboardButton.set_hexpand(true);
	m_revertButton.set_hexpand(true);
	applyRow->attach(m_applyKeyboardButton, 0, 0, 1, 1);
	applyRow->attach(m_revertButton, 1, 0, 1, 1);
	applyBox->pack_start(*applyRow, false, false);
}

// Ordered the way the work flows: choose colours, then the keyboard's own
// effects, then the host-driven live ones, with setup last.
void MainWindow::buildTabs(Gtk::Box* rightCol, Gtk::Notebook*& tabs,
                           Gtk::Box*& colorsTab, Gtk::Box*& effectTab,
                           Gtk::Box*& liveTab, Gtk::Box*& deviceTab) {
	tabs = Gtk::manage(new Gtk::Notebook());
	tabs->set_scrollable(true);
	tabs->set_show_border(false);
	rightCol->pack_start(*tabs, true, true);
	colorsTab = makeTab(tabs, "Colors");
	effectTab = makeTab(tabs, "On-board effects");
	liveTab = makeTab(tabs, "Live effects");
	deviceTab = makeTab(tabs, "Device");
}

Gtk::Box* MainWindow::addSection(Gtk::Box* parent, const std::string& title) {
	Gtk::Frame *frame = Gtk::manage(new Gtk::Frame());
	Gtk::Label *label = Gtk::manage(new Gtk::Label());
	label->set_markup("<b>" + title + "</b>");
	frame->set_label_widget(*label);
	Gtk::Box *box = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_VERTICAL, 4));
	box->set_margin_top(6);
	box->set_margin_bottom(6);
	box->set_margin_start(8);
	box->set_margin_end(8);
	frame->add(*box);
	parent->pack_start(*frame, false, false);
	return box;
}

Gtk::Box* MainWindow::makeTab(Gtk::Notebook* tabs, const char* name) {
	Gtk::ScrolledWindow *scroll = Gtk::manage(new Gtk::ScrolledWindow());
	scroll->set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
	// The same rule the board's own well follows, for the same reason: an
	// overlay scrollbar is drawn only once the pointer is already over
	// it, so until then a card cut off by the bottom of the pane looks
	// like a card that is broken rather than one you can scroll to. At
	// 980x620 that hid a Brightness slider and a whole curve editor on
	// the Live tab with nothing on screen saying they were there. A real
	// scrollbar takes its width from the content and is visible the
	// moment there is anything below the fold.
	scroll->set_overlay_scrolling(false);
	scroll->set_propagate_natural_height(false);
	scroll->set_propagate_natural_width(false);
	scroll->set_min_content_height(120);
	scroll->set_min_content_width(200);
	Gtk::Box *box = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_VERTICAL, 4));
	scroll->add(*box);
	tabs->append_page(*scroll, name);
	return box;
}

void MainWindow::setupSignals() {
	m_rescanButton.signal_clicked().connect(
		sigc::mem_fun(*this, &MainWindow::rescanDevices));
	m_deviceCombo.signal_changed().connect(
		sigc::mem_fun(*this, &MainWindow::onDeviceChanged));
	m_manualDeviceCheck.signal_toggled().connect(
		sigc::mem_fun(*this, &MainWindow::onManualDeviceToggled));
	m_vidEntry.signal_changed().connect(
		sigc::mem_fun(*this, &MainWindow::onManualEntryChanged));
	m_pidEntry.signal_changed().connect(
		sigc::mem_fun(*this, &MainWindow::onManualEntryChanged));
	m_serialEntry.signal_changed().connect(
		sigc::mem_fun(*this, &MainWindow::onManualEntryChanged));
	m_protocolCombo.signal_changed().connect(
		sigc::mem_fun(*this, &MainWindow::onManualEntryChanged));
	m_listKeyboardsButton.signal_clicked().connect(
		sigc::mem_fun(*this, &MainWindow::onListKeyboards));
	m_printDeviceButton.signal_clicked().connect(
		sigc::mem_fun(*this, &MainWindow::onPrintDevice));
	m_applyKeyboardButton.signal_clicked().connect(
		[this]() { onApplyKeyboard(); });
	m_revertButton.signal_clicked().connect(
		sigc::mem_fun(*this, &MainWindow::onRevert));
	m_paintAllButton.signal_clicked().connect(
		sigc::mem_fun(*this, &MainWindow::onPaintAll));
	m_colorButton.signal_color_set().connect(
		sigc::mem_fun(*this, &MainWindow::onColorChosen));
	m_paintSelectionButton.signal_clicked().connect(
		sigc::mem_fun(*this, &MainWindow::onPaintSelection));
	m_clearSelectionButton.signal_clicked().connect(
		sigc::mem_fun(*this, &MainWindow::onClearSelectionColor));
	m_deselectButton.signal_clicked().connect(
		sigc::mem_fun(*this, &MainWindow::onDeselectSelection));
	m_applyEffectButton.signal_clicked().connect(
		sigc::mem_fun(*this, &MainWindow::onApplyEffect));
	m_effectOffButton.signal_clicked().connect(
		sigc::mem_fun(*this, &MainWindow::onEffectOff));
	m_effectCombo.signal_changed().connect(
		sigc::mem_fun(*this, &MainWindow::onEffectChanged));
	m_applyGKeysButton.signal_clicked().connect(
		sigc::mem_fun(*this, &MainWindow::onApplyGKeys));
	m_applyStartupButton.signal_clicked().connect(
		sigc::mem_fun(*this, &MainWindow::onApplyStartupMode));
	m_applyOnBoardButton.signal_clicked().connect(
		sigc::mem_fun(*this, &MainWindow::onApplyOnBoardMode));
	m_undoButton.signal_clicked().connect(
		sigc::mem_fun(*this, &MainWindow::onUndo));
	m_redoButton.signal_clicked().connect(
		sigc::mem_fun(*this, &MainWindow::onRedo));
	m_liveToggleButton.signal_clicked().connect(
		sigc::mem_fun(*this, &MainWindow::onLiveToggle));
	m_liveModeCombo.signal_changed().connect(
		sigc::mem_fun(*this, &MainWindow::onLiveModeChanged));
	m_waveModeCombo.signal_changed().connect(
		sigc::mem_fun(*this, &MainWindow::onWaveParamsChanged));
	m_waveShapeCombo.signal_changed().connect(
		sigc::mem_fun(*this, &MainWindow::onWavePresetChanged));
	m_wavePeriodScale.signal_value_changed().connect(
		sigc::mem_fun(*this, &MainWindow::onWaveParamsChanged));
	m_waveMinScale.signal_value_changed().connect(
		sigc::mem_fun(*this, &MainWindow::onWaveParamsChanged));
	m_waveMaxScale.signal_value_changed().connect(
		sigc::mem_fun(*this, &MainWindow::onWaveParamsChanged));
	m_waveGraph.signal_changed().connect(
		sigc::mem_fun(*this, &MainWindow::onWaveGraphChanged));
	m_audioRefreshButton.signal_clicked().connect(
		sigc::mem_fun(*this, &MainWindow::refreshAudioSources));
	m_audioModeCombo.signal_changed().connect(
		sigc::mem_fun(*this, &MainWindow::onAudioParamsChanged));
	m_audioLowColorButton.signal_color_set().connect(
		sigc::mem_fun(*this, &MainWindow::onAudioParamsChanged));
	m_audioHighColorButton.signal_color_set().connect(
		sigc::mem_fun(*this, &MainWindow::onAudioParamsChanged));
	m_audioGainScale.signal_value_changed().connect(
		sigc::mem_fun(*this, &MainWindow::onAudioParamsChanged));
	m_audioSmoothScale.signal_value_changed().connect(
		sigc::mem_fun(*this, &MainWindow::onAudioParamsChanged));
	m_audioAutoGainCheck.signal_toggled().connect(
		sigc::mem_fun(*this, &MainWindow::onAudioParamsChanged));
	m_audioOverlayCheck.signal_toggled().connect(
		sigc::mem_fun(*this, &MainWindow::onAudioParamsChanged));
	m_screenModeCombo.signal_changed().connect(
		sigc::mem_fun(*this, &MainWindow::onScreenParamsChanged));
	m_screenBoostScale.signal_value_changed().connect(
		sigc::mem_fun(*this, &MainWindow::onScreenParamsChanged));
	m_screenSmoothScale.signal_value_changed().connect(
		sigc::mem_fun(*this, &MainWindow::onScreenParamsChanged));
	m_screenOverlayCheck.signal_toggled().connect(
		sigc::mem_fun(*this, &MainWindow::onScreenParamsChanged));
	// Only offered while the effect is stopped (updateAnimUI), since the
	// portal binds the screen at start and cannot be re-aimed mid-run.
	m_screenForgetButton.signal_clicked().connect([this]() {
		ScreenCapture::forgetSource();
		updateScreenCaption();
		status("Forgotten — Start will ask which screen to capture");
	});
	m_keyboardWidget.signal_selection_changed().connect(
		sigc::mem_fun(*this, &MainWindow::onSelectionChanged));
	m_keyboardWidget.signal_key_pressed().connect(
		sigc::mem_fun(*this, &MainWindow::onKeyPressed));
	// A stroke across the board stages many keys, but it was one movement
	// of one hand: one undo step.
	m_keyboardWidget.signal_stroke_begin().connect([this]() {
		if (m_undoBatch)
			return;
		pushUndo();
		m_undoBatch = true;
		m_strokeBatch = true;
	});
	m_keyboardWidget.signal_stroke_end().connect([this]() {
		if (!m_strokeBatch)
			return;
		m_undoBatch = false;
		m_strokeBatch = false;
	});
	m_keyboardWidget.signal_key_pick().connect(
		sigc::mem_fun(*this, &MainWindow::onKeyPick));
	m_keyboardWidget.signal_key_cleared().connect(
		sigc::mem_fun(*this, &MainWindow::onKeyCleared));
	m_keyboardWidget.signal_key_menu().connect(
		sigc::mem_fun(*this, &MainWindow::onKeyMenu));
	m_keyboardWidget.signal_cursor_moved().connect(
		sigc::mem_fun(*this, &MainWindow::onCursorMoved));
	signal_key_press_event().connect(
		sigc::mem_fun(*this, &MainWindow::onWindowKeyPress), false);
	signal_delete_event().connect(
		sigc::mem_fun(*this, &MainWindow::onDeleteEvent));
	signal_window_state_event().connect(
		sigc::mem_fun(*this, &MainWindow::onWindowState));
}

void MainWindow::setupAccelerators() {
	Glib::RefPtr<Gtk::AccelGroup> accels = Gtk::AccelGroup::create();
	add_accel_group(accels);
	m_applyKeyboardButton.add_accelerator("clicked", accels, GDK_KEY_Return,
		Gdk::CONTROL_MASK, Gtk::ACCEL_VISIBLE);
	m_applyKeyboardButton.add_accelerator("clicked", accels, GDK_KEY_KP_Enter,
		Gdk::CONTROL_MASK, Gtk::ACCEL_VISIBLE);
	m_undoButton.add_accelerator("clicked", accels, GDK_KEY_z,
		Gdk::CONTROL_MASK, Gtk::ACCEL_VISIBLE);
	m_redoButton.add_accelerator("clicked", accels, GDK_KEY_z,
		Gdk::CONTROL_MASK | Gdk::SHIFT_MASK, Gtk::ACCEL_VISIBLE);
	m_redoButton.add_accelerator("clicked", accels, GDK_KEY_y,
		Gdk::CONTROL_MASK, Gtk::ACCEL_VISIBLE);
	// On the menu items, and visible: the menu prints each shortcut next
	// to what it does, which is where someone looks for it.
	m_loadItem.add_accelerator("activate", accels, GDK_KEY_o,
		Gdk::CONTROL_MASK, Gtk::ACCEL_VISIBLE);
	m_saveItem.add_accelerator("activate", accels, GDK_KEY_s,
		Gdk::CONTROL_MASK, Gtk::ACCEL_VISIBLE);
	m_saveAsItem.add_accelerator("activate", accels, GDK_KEY_s,
		Gdk::CONTROL_MASK | Gdk::SHIFT_MASK, Gtk::ACCEL_VISIBLE);
	m_quitItem.add_accelerator("activate", accels, GDK_KEY_q,
		Gdk::CONTROL_MASK, Gtk::ACCEL_VISIBLE);
	// F1 is the one key every desktop agrees means help, and it is the
	// only one of these a person can reach without being told.
	m_shortcutsItem.add_accelerator("activate", accels, GDK_KEY_F1,
		(Gdk::ModifierType)0, Gtk::ACCEL_VISIBLE);
	// Ctrl+? was advertised in the menu and in the shortcut list, and did
	// nothing. On any layout where ? is the shifted stroke of another key
	// — which is most of them — the event arrives carrying Shift, and an
	// accelerator registered for Ctrl alone is compared against the whole
	// modifier state and does not match. Both spellings are registered
	// now, so the shortcut works whichever way the layout produces the
	// character. Not ACCEL_VISIBLE: the menu prints the first one it
	// finds, and F1 is the one worth printing.
	for (Gdk::ModifierType shift : {Gdk::CONTROL_MASK,
	                                Gdk::CONTROL_MASK | Gdk::SHIFT_MASK})
		m_shortcutsItem.add_accelerator("activate", accels, GDK_KEY_question,
			shift, (Gtk::AccelFlags)0);
}

void MainWindow::refreshFeatures() {
	m_features = help::getKeyboardFeatures(m_kbd.getKeyboardModel());

	// Paint section: key groups applicable to this model.
	m_groups.clear();
	if (has(help::KeyboardFeatures::logo1))
		m_groups.push_back({"logo", LedKeyboard::KeyGroup::logo});
	if (has(help::KeyboardFeatures::setindicators))
		m_groups.push_back({"indicators", LedKeyboard::KeyGroup::indicators});
	if (has(help::KeyboardFeatures::gkeys))
		m_groups.push_back({"gkeys", LedKeyboard::KeyGroup::gkeys});
	m_groups.push_back({"fkeys", LedKeyboard::KeyGroup::fkeys});
	m_groups.push_back({"modifiers", LedKeyboard::KeyGroup::modifiers});
	if (has(help::KeyboardFeatures::multimedia))
		m_groups.push_back({"multimedia", LedKeyboard::KeyGroup::multimedia});
	m_groups.push_back({"arrows", LedKeyboard::KeyGroup::arrows});
	if (has(help::KeyboardFeatures::numpad))
		m_groups.push_back({"numeric", LedKeyboard::KeyGroup::numeric});
	m_groups.push_back({"functions", LedKeyboard::KeyGroup::functions});
	m_groups.push_back({"keys", LedKeyboard::KeyGroup::keys});
	rebuildGroupChips();

	m_rebuilding = true;
	m_targetCombo.remove_all();
	m_targetCombo.append("All");
	m_targetCombo.append("Keys");
	if (has(help::KeyboardFeatures::logo1))
		m_targetCombo.append("Logo");
	m_targetCombo.set_active(0);
	m_storeCheck.set_active(false);

	// Color control: rgb picker vs intensity slider.
	if (m_rgbColorBox) m_rgbColorBox->set_visible(has(help::KeyboardFeatures::rgb));
	if (m_intensityBox) m_intensityBox->set_visible(has(help::KeyboardFeatures::intensity));

	// Keyboard grid vs hint for non-per-key models.
	LedKeyboard::KeyboardModel model = m_kbd.getKeyboardModel();
	if (m_keyboardScroll) m_keyboardScroll->set_visible(has(help::KeyboardFeatures::setkey));
	m_regionHintLabel.set_visible(!has(help::KeyboardFeatures::setkey));
	if (m_features == help::KeyboardFeatures::none)
		m_regionHintLabel.set_markup(
			"<span size=\"large\">No keyboard connected.</span>\n\n"
			"Connect a supported Logitech keyboard and press Rescan\n"
			"in the Device tab.");
	else if (has(help::KeyboardFeatures::setregion))
		m_regionHintLabel.set_markup(
			"<span size=\"large\">This keyboard lights five zones, "
			"not single keys.</span>\n\n"
			"Color them in the Regions section — or all at once with Paint\n"
			"all keys — then press Send to keyboard.");
	else
		m_regionHintLabel.set_markup(
			"<span size=\"large\">This keyboard has no per-key lighting.</span>\n\n"
			"Set its brightness with Paint all keys,\n"
			"then press Send to keyboard.");

	// Model-specific sections.
	if (m_gKeysFrame) m_gKeysFrame->set_visible(has(help::KeyboardFeatures::gkeys));
	if (m_startupFrame) m_startupFrame->set_visible(startupModeSupported(model));
	if (m_onBoardFrame) m_onBoardFrame->set_visible(has(help::KeyboardFeatures::onboardmode));
	if (m_regionsFrame) m_regionsFrame->set_visible(has(help::KeyboardFeatures::setregion));
	if (m_liveFrame) m_liveFrame->set_visible(has(help::KeyboardFeatures::setkey) &&
	                        has(help::KeyboardFeatures::rgb));

	// M-key values: g910 takes 0-7, g815 only 1-3.
	m_mnKeyCombo.remove_all();
	if (model == LedKeyboard::KeyboardModel::g815) {
		m_mnKeyCombo.append("M1");
		m_mnKeyCombo.append("M2");
		m_mnKeyCombo.append("M1 + M2");
	} else {
		m_mnKeyCombo.append("None");
		m_mnKeyCombo.append("M1");
		m_mnKeyCombo.append("M2");
		m_mnKeyCombo.append("M1 + M2");
		m_mnKeyCombo.append("M3");
		m_mnKeyCombo.append("M1 + M3");
		m_mnKeyCombo.append("M2 + M3");
		m_mnKeyCombo.append("M1 + M2 + M3");
	}
	m_mnKeyCombo.set_active(0);
	// The combos are whole again, so the handlers can be trusted with
	// them, and the one that was stood down gets the call it missed.
	m_rebuilding = false;
	onEffectChanged();

	if (has(help::KeyboardFeatures::setregion) && m_regionDraft.empty()) {
		for (int region = 1; region <= 5; region++) {
			m_regionDraft[region] = Gdk::RGBA("#000000");
			m_regionApplied[region] = Gdk::RGBA("#000000");
		}
		for (size_t i = 0; i < m_regionBoxes.size(); ++i)
			paintRegionBox((int)i + 1, Gdk::RGBA("#000000"));
	}
}

void MainWindow::setControlsEnabled(bool enabled) {
	m_controlsEnabled = enabled;
	m_deviceCombo.set_sensitive(!m_manualOverrideActive && !m_devices.empty());
	m_listKeyboardsButton.set_sensitive(true);
	m_printDeviceButton.set_sensitive(enabled);
	m_colorButton.set_sensitive(enabled);
	m_intensityScale.set_sensitive(enabled);
	m_paintAllButton.set_sensitive(enabled && has(help::KeyboardFeatures::setall));
	m_groupChips.set_sensitive(enabled && has(help::KeyboardFeatures::setgroup) &&
	                           has(help::KeyboardFeatures::setkey));
	// And each chip in its own right: see rebuildGroupChips.
	for (Gtk::Button *chip : m_groupChipButtons)
		chip->set_sensitive(m_groupChips.get_sensitive());
	m_effectCombo.set_sensitive(enabled);
	m_targetCombo.set_sensitive(enabled);
	m_storeCheck.set_sensitive(enabled && has(help::KeyboardFeatures::userstoredlighting));
	m_applyEffectButton.set_sensitive(enabled);
	m_effectOffButton.set_sensitive(enabled);
	m_mrKeyCombo.set_sensitive(enabled && has(help::KeyboardFeatures::gkeys));
	m_mnKeyCombo.set_sensitive(enabled && has(help::KeyboardFeatures::gkeys));
	m_gKeysModeCombo.set_sensitive(enabled && has(help::KeyboardFeatures::gkeys));
	m_applyGKeysButton.set_sensitive(enabled && has(help::KeyboardFeatures::gkeys));
	m_startupModeCombo.set_sensitive(enabled && startupModeSupported(
		m_kbd.getKeyboardModel()));
	m_applyStartupButton.set_sensitive(enabled && startupModeSupported(
		m_kbd.getKeyboardModel()));
	m_onBoardModeCombo.set_sensitive(enabled &&
		has(help::KeyboardFeatures::onboardmode));
	m_applyOnBoardButton.set_sensitive(enabled &&
		has(help::KeyboardFeatures::onboardmode));
	for (Gtk::EventBox *box : m_regionBoxes)
		box->set_sensitive(enabled && has(help::KeyboardFeatures::setregion));
	m_keyboardWidget.set_sensitive(enabled && has(help::KeyboardFeatures::setkey));
	onSelectionChanged();
	onEffectChanged();
	// updateAnimUI has the final say on everything that depends on what
	// is running; it re-applies these same gates.
	updateAnimUI();
}

LedKeyboard::Color MainWindow::currentColor() const {
	return toLedColor(currentRGBA());
}

Gdk::RGBA MainWindow::currentRGBA() const {
	if (has(help::KeyboardFeatures::intensity)) {
		double value = m_intensityScale.get_value() / 255.0;
		Gdk::RGBA rgba;
		rgba.set_rgba(value, value, value, 1.0);
		return rgba;
	}
	return solidRGBA(m_colorButton.get_rgba());
}

// --- Staged draft ------------------------------------------------------

MainWindow::DraftSnapshot MainWindow::snapshotDraft() const {
	DraftSnapshot snapshot;
	snapshot.keys = m_keyboardWidget.getKeyColors();
	snapshot.regions = m_regionDraft;
	snapshot.allKeys = m_allKeysDraft;
	return snapshot;
}

void MainWindow::applySnapshot(const DraftSnapshot &snapshot) {
	for (std::map<LedKeyboard::Key, Gdk::RGBA>::const_iterator it =
	     snapshot.keys.begin(); it != snapshot.keys.end(); ++it)
		m_keyboardWidget.setKeyColor(it->first, it->second);
	m_regionDraft = snapshot.regions;
	for (std::map<int, Gdk::RGBA>::const_iterator it = m_regionDraft.begin();
	     it != m_regionDraft.end(); ++it) {
		if (it->first >= 1 && it->first <= (int)m_regionBoxes.size())
			paintRegionBox(it->first, it->second);
	}
	m_allKeysDraft = snapshot.allKeys;
	updatePendingState();
}

// Called before a change, not after: the stack holds the state to go back
// to. A new edit invalidates anything that was undone.
void MainWindow::pushUndo() {
	if (m_undoBatch)
		return;
	m_undoStack.push_back(snapshotDraft());
	if (m_undoStack.size() > 64)
		m_undoStack.erase(m_undoStack.begin());
	m_redoStack.clear();
	updateHistoryUI();
}

void MainWindow::onUndo() {
	if (m_undoStack.empty()) {
		status("Nothing to undo");
		return;
	}
	m_redoStack.push_back(snapshotDraft());
	DraftSnapshot snapshot = m_undoStack.back();
	m_undoStack.pop_back();
	applySnapshot(snapshot);
	updateHistoryUI();
	status("Undone");
}

void MainWindow::onRedo() {
	if (m_redoStack.empty()) {
		status("Nothing to redo");
		return;
	}
	m_undoStack.push_back(snapshotDraft());
	DraftSnapshot snapshot = m_redoStack.back();
	m_redoStack.pop_back();
	applySnapshot(snapshot);
	updateHistoryUI();
	status("Redone");
}

// Painting one key. Answers whether the board actually changed, because
// painting a key the colour it already is changes nothing and the window
// must not pretend otherwise: on the board this opens with, every key is
// #ff0000 and so is the colour well, so the very first click a new user
// makes lands here. It used to answer "Painted key G #ff0000" while the
// header said "Nothing to send" and Send stayed grey — three places, two
// of them contradicting the third — and it left an undo step behind that
// undid nothing.
bool MainWindow::stageKey(LedKeyboard::Key key, const Gdk::RGBA &color) {
	Gdk::RGBA already;
	if (m_keyboardWidget.getKeyColor(key, already) &&
	    already.get_red() == color.get_red() &&
	    already.get_green() == color.get_green() &&
	    already.get_blue() == color.get_blue())
		return false;
	pushUndo();
	m_keyboardWidget.setKeyColor(key, color);
	updatePendingState();
	return true;
}

// Painting a set of keys, answering how many of them actually changed.
// Same reason as stageKey: on a board that is all one colour, painting a
// group with that colour is nothing happening, and "Painted the function
// row #ff0000" over a header reading "Nothing to send" is the window
// disagreeing with itself about what the user just did.
size_t MainWindow::stageKeys(const std::vector<LedKeyboard::Key> &keys,
                             const Gdk::RGBA &color) {
	size_t changed = 0;
	for (LedKeyboard::Key key : keys) {
		Gdk::RGBA already;
		if (m_keyboardWidget.getKeyColor(key, already) &&
		    already.get_red() == color.get_red() &&
		    already.get_green() == color.get_green() &&
		    already.get_blue() == color.get_blue())
			continue;
		++changed;
	}
	if (!changed)
		return 0;
	pushUndo();
	for (LedKeyboard::Key key : keys)
		m_keyboardWidget.setKeyColor(key, color);
	updatePendingState();
	return changed;
}

void MainWindow::stageAllKeys(const Gdk::RGBA &color) {
	pushUndo();
	Gdk::RGBA solid = solidRGBA(color);
	if (!has(help::KeyboardFeatures::setkey)) {
		m_allKeysDraft = solid;
		if (has(help::KeyboardFeatures::setregion)) {
			for (int region = 1; region <= 5; region++) {
				m_regionDraft[region] = solid;
				if (region <= (int)m_regionBoxes.size())
					paintRegionBox(region, solid);
			}
		}
		updatePendingState();
		return;
	}
	m_keyboardWidget.setAllColors(solid);
	updatePendingState();
}

bool MainWindow::hasPendingChanges() const {
	return m_pendingCount > 0;
}

bool MainWindow::restoreAppliedState() {
	if (!m_kbd.isOpen())
		return false;
	static const Gdk::RGBA black("#000000");
	bool wrote = false;
	if (has(help::KeyboardFeatures::setkey)) {
		// A full write, not a delta: the board is not showing what we
		// last sent, so there is nothing to diff against.
		LedKeyboard::KeyValueArray values;
		for (std::map<LedKeyboard::Key, Gdk::RGBA>::const_iterator it =
		     m_appliedColors.begin(); it != m_appliedColors.end(); ++it) {
			LedKeyboard::KeyValue keyValue;
			keyValue.key = it->first;
			keyValue.color = toLedColor(it->second);
			values.push_back(keyValue);
		}
		if (values.empty())
			return false;
		wrote = m_kbd.setKeys(values) && m_kbd.commit();
	} else {
		wrote = true;
		if (has(help::KeyboardFeatures::setall))
			wrote = m_kbd.setAllKeys(toLedColor(m_allKeysApplied)) && m_kbd.commit();
		if (wrote && has(help::KeyboardFeatures::setregion)) {
			for (int region = 1; region <= 5; region++) {
				Gdk::RGBA color = black;
				std::map<int, Gdk::RGBA>::const_iterator found =
					m_regionApplied.find(region);
				if (found != m_regionApplied.end())
					color = found->second;
				if (!m_kbd.setRegion((uint8_t)region, toLedColor(color)))
					wrote = false;
			}
		}
	}
	if (wrote) {
		m_deviceShowsPreview = false;
		m_keyboardWidget.clearPreview();
		updatePendingState();
	}
	return wrote;
}

// Wrapper for the callers that only care whether anything is still
// unsent; "the keyboard already has these colors" counts as done.
bool MainWindow::onApplyKeyboard() {
	return applyDraft() != ApplyResult::failed;
}

MainWindow::ApplyResult MainWindow::applyDraft() {
	if (blockedByAnimation("sending colors"))
		return ApplyResult::failed;
	if (!ensureOpen())
		return ApplyResult::failed;
	static const Gdk::RGBA black("#000000");
	// After an effect the board no longer shows what we last wrote, so a
	// delta against m_appliedColors would send nothing (or the wrong
	// subset): write the whole scheme instead.
	const bool full = m_deviceShowsPreview;

	if (!has(help::KeyboardFeatures::setkey)) {
		// Whole-board and region lanes (self-contained packets; commit is
		// a no-op on the non-transactional g213/g413 anyway).
		bool any = false;
		if (has(help::KeyboardFeatures::setall) &&
		    (full || !rgbaEqual(m_allKeysDraft, m_allKeysApplied))) {
			if (!m_kbd.setAllKeys(toLedColor(m_allKeysDraft)) || !m_kbd.commit()) {
				statusError("The keyboard did not take the colors");
				return ApplyResult::failed;
			}
			m_allKeysApplied = m_allKeysDraft;
			any = true;
		}
		for (int region = 1; region <= 5; region++) {
			Gdk::RGBA draft = black;
			Gdk::RGBA applied = black;
			std::map<int, Gdk::RGBA>::const_iterator d =
				m_regionDraft.find(region);
			if (d != m_regionDraft.end())
				draft = d->second;
			std::map<int, Gdk::RGBA>::const_iterator a =
				m_regionApplied.find(region);
			if (a != m_regionApplied.end())
				applied = a->second;
			if (!full && rgbaEqual(draft, applied))
				continue;
			if (!m_kbd.setRegion((uint8_t)region, toLedColor(draft))) {
				statusError("Could not send region " + std::to_string(region));
				return ApplyResult::failed;
			}
			any = true;
		}
		if (!any) {
			status("The keyboard already has these colors");
			return ApplyResult::nothingToDo;
		}
		m_regionApplied = m_regionDraft;
		m_deviceShowsPreview = false;
		persistLastProfile();
		updatePendingState();
		status("Sent to the keyboard");
		return ApplyResult::applied;
	}

	// Key-color lane: one setKeys + one commit for the whole delta
	// (the CLI's -pp / profile batch path).
	LedKeyboard::KeyValueArray values;
	for (std::map<LedKeyboard::Key, Gdk::RGBA>::const_iterator it =
	     m_keyboardWidget.getKeyColors().begin();
	     it != m_keyboardWidget.getKeyColors().end(); ++it) {
		Gdk::RGBA applied = black;
		std::map<LedKeyboard::Key, Gdk::RGBA>::const_iterator found =
			m_appliedColors.find(it->first);
		if (found != m_appliedColors.end())
			applied = found->second;
		if (!full && rgbaEqual(it->second, applied))
			continue;
		LedKeyboard::KeyValue keyValue;
		keyValue.key = it->first;
		keyValue.color = toLedColor(it->second);
		values.push_back(keyValue);
	}
	if (values.empty()) {
		status("The keyboard already has these colors");
		return ApplyResult::nothingToDo;
	}
	if (!m_kbd.setKeys(values) || !m_kbd.commit()) {
		statusError("The keyboard did not take the colors");
		return ApplyResult::failed;
	}
	m_appliedColors = m_keyboardWidget.getKeyColors();
	m_deviceShowsPreview = false;
	persistLastProfile();
	updatePendingState();
	status("Sent " + std::to_string(values.size()) +
		(values.size() == 1 ? " key" : " keys"));
	return ApplyResult::applied;
}

void MainWindow::onRevert() {
	pushUndo();
	static const Gdk::RGBA black("#000000");
	for (std::map<LedKeyboard::Key, Gdk::RGBA>::const_iterator it =
	     m_keyboardWidget.getKeyColors().begin();
	     it != m_keyboardWidget.getKeyColors().end(); ++it) {
		Gdk::RGBA applied = black;
		std::map<LedKeyboard::Key, Gdk::RGBA>::const_iterator found =
			m_appliedColors.find(it->first);
		if (found != m_appliedColors.end())
			applied = found->second;
		m_keyboardWidget.setKeyColor(it->first, applied);
	}
	for (std::map<int, Gdk::RGBA>::iterator it = m_regionDraft.begin();
	     it != m_regionDraft.end(); ++it) {
		Gdk::RGBA applied = black;
		std::map<int, Gdk::RGBA>::const_iterator found =
			m_regionApplied.find(it->first);
		if (found != m_regionApplied.end())
			applied = found->second;
		it->second = applied;
		if (it->first >= 1 && it->first <= (int)m_regionBoxes.size())
			paintRegionBox(it->first, applied);
	}
	m_allKeysDraft = m_allKeysApplied;
	updatePendingState();
	status("Discarded — the colors on the keyboard are back");
}

const char *MainWindow::modelFileName(LedKeyboard::KeyboardModel model) {
	switch (model) {
		case LedKeyboard::KeyboardModel::g213: return "g213";
		case LedKeyboard::KeyboardModel::g410: return "g410";
		case LedKeyboard::KeyboardModel::g413: return "g413";
		case LedKeyboard::KeyboardModel::g512: return "g512";
		case LedKeyboard::KeyboardModel::g513: return "g513";
		case LedKeyboard::KeyboardModel::g610: return "g610";
		case LedKeyboard::KeyboardModel::g810: return "g810";
		case LedKeyboard::KeyboardModel::g815: return "g815";
		case LedKeyboard::KeyboardModel::g910: return "g910";
		case LedKeyboard::KeyboardModel::gpro: return "gpro";
		default: return "unknown";
	}
}

std::string MainWindow::lastProfilePath() const {
	return Glib::get_user_config_dir() + "/g810-led/" +
		modelFileName(m_layoutModel) + ".profile";
}

std::vector<ProfileCommand> MainWindow::draftProfileCommands() const {
	std::vector<ProfileCommand> commands;
	if (has(help::KeyboardFeatures::setkey)) {
		const std::map<LedKeyboard::Key, Gdk::RGBA> &colors =
			m_keyboardWidget.getKeyColors();
		std::map<uint32_t, size_t> counts;
		for (std::map<LedKeyboard::Key, Gdk::RGBA>::const_iterator it =
		     colors.begin(); it != colors.end(); ++it) {
			LedKeyboard::Color color = toLedColor(it->second);
			uint32_t packed = ((uint32_t)color.red << 16) |
				((uint32_t)color.green << 8) | color.blue;
			counts[packed]++;
		}
		uint32_t majority = 0;
		size_t best = 0;
		for (std::map<uint32_t, size_t>::const_iterator it = counts.begin();
		     it != counts.end(); ++it) {
			if (it->second > best) {
				best = it->second;
				majority = it->first;
			}
		}
		ProfileCommand all;
		all.type = ProfileCommand::Type::all;
		all.color.red = (uint8_t)((majority >> 16) & 0xff);
		all.color.green = (uint8_t)((majority >> 8) & 0xff);
		all.color.blue = (uint8_t)(majority & 0xff);
		commands.push_back(all);
		for (std::map<LedKeyboard::Key, Gdk::RGBA>::const_iterator it =
		     colors.begin(); it != colors.end(); ++it) {
			LedKeyboard::Color color = toLedColor(it->second);
			uint32_t packed = ((uint32_t)color.red << 16) |
				((uint32_t)color.green << 8) | color.blue;
			if (packed == majority)
				continue;
			ProfileCommand key;
			key.type = ProfileCommand::Type::key;
			key.key = it->first;
			key.color = color;
			commands.push_back(key);
		}
	} else if (has(help::KeyboardFeatures::setregion)) {
		std::map<uint32_t, size_t> counts;
		LedKeyboard::Color regionColors[6];
		for (int region = 1; region <= 5; region++) {
			Gdk::RGBA rgba("#000000");
			std::map<int, Gdk::RGBA>::const_iterator found =
				m_regionDraft.find(region);
			if (found != m_regionDraft.end())
				rgba = found->second;
			regionColors[region] = toLedColor(rgba);
			uint32_t packed = ((uint32_t)regionColors[region].red << 16) |
				((uint32_t)regionColors[region].green << 8) |
				regionColors[region].blue;
			counts[packed]++;
		}
		uint32_t majority = 0;
		size_t best = 0;
		for (std::map<uint32_t, size_t>::const_iterator it = counts.begin();
		     it != counts.end(); ++it) {
			if (it->second > best) {
				best = it->second;
				majority = it->first;
			}
		}
		ProfileCommand all;
		all.type = ProfileCommand::Type::all;
		all.color.red = (uint8_t)((majority >> 16) & 0xff);
		all.color.green = (uint8_t)((majority >> 8) & 0xff);
		all.color.blue = (uint8_t)(majority & 0xff);
		commands.push_back(all);
		for (int region = 1; region <= 5; region++) {
			uint32_t packed = ((uint32_t)regionColors[region].red << 16) |
				((uint32_t)regionColors[region].green << 8) |
				regionColors[region].blue;
			if (packed == majority)
				continue;
			ProfileCommand cmd;
			cmd.type = ProfileCommand::Type::region;
			cmd.region = (uint8_t)region;
			cmd.color = regionColors[region];
			commands.push_back(cmd);
		}
	} else if (has(help::KeyboardFeatures::setall)) {
		ProfileCommand all;
		all.type = ProfileCommand::Type::all;
		all.color = toLedColor(m_allKeysDraft);
		commands.push_back(all);
	}
	ProfileCommand commit;
	commit.type = ProfileCommand::Type::commit;
	commands.push_back(commit);
	appendWaveCommands(commands);
	appendAudioCommands(commands);
	appendScreenCommands(commands);
	// raindropActive(), not the rainActive() umbrella: that one means
	// "some live effect owns the board", so asking it here wrote a
	// raindrop line into a profile that was running a different lane —
	// and the next load started a raindrop nobody asked for.
	if (raindropActive()) {
		ProfileCommand rain;
		rain.type = ProfileCommand::Type::rain;
		rain.value = 1;
		rain.color = toLedColor(m_animColorButton.get_rgba());
		rain.rainOverlay = m_animOverlay;
		commands.push_back(rain);
	}
	if (m_hasAppliedEffect) {
		ProfileCommand fx;
		fx.type = ProfileCommand::Type::fx;
		fx.effect = m_appliedEffect;
		fx.part = m_appliedEffectPart;
		fx.period = m_appliedEffectPeriod;
		fx.color = m_appliedEffectColor;
		fx.storage = m_appliedEffectStorage;
		commands.push_back(fx);
	}
	if (m_hasAppliedGKeys) {
		ProfileCommand c;
		c.type = ProfileCommand::Type::mr; c.value = m_appliedMR; commands.push_back(c);
		c.type = ProfileCommand::Type::mn; c.value = m_appliedMN; commands.push_back(c);
		c.type = ProfileCommand::Type::gkm; c.value = m_appliedGKeysMode; commands.push_back(c);
	}
	if (m_hasAppliedStartup) {
		ProfileCommand sm;
		sm.type = ProfileCommand::Type::startupMode;
		sm.startupMode = m_appliedStartup;
		commands.push_back(sm);
	}
	if (m_hasAppliedOnBoard) {
		ProfileCommand obm;
		obm.type = ProfileCommand::Type::onBoardMode;
		obm.onBoardMode = m_appliedOnBoard;
		commands.push_back(obm);
	}
	return commands;
}

void MainWindow::stageProfileCommands(const std::vector<ProfileCommand> &commands,
                                      bool honorDevice) {
	// Loading a profile stages many commands; the user thinks of it as
	// one action, so it gets one undo step.
	const bool outerBatch = m_undoBatch;
	if (!outerBatch) {
		pushUndo();
		m_undoBatch = true;
	}
	struct BatchGuard {
		MainWindow *self; bool outer;
		~BatchGuard() { if (!outer) self->m_undoBatch = false; }
	} guard{this, outerBatch};
	for (const ProfileCommand &command : commands) {
		switch (command.type) {
			case ProfileCommand::Type::all:
				stageAllKeys(fromLedColor(command.color));
				break;
			case ProfileCommand::Type::group:
				stageKeys(LedKeyboard::keysForGroup(command.group),
				          fromLedColor(command.color));
				if (honorDevice && m_kbd.isOpen())
					m_kbd.setGroupKeys(command.group, command.color);
				break;
			case ProfileCommand::Type::key:
				stageKey(command.key, fromLedColor(command.color));
				if (honorDevice && m_kbd.isOpen())
					m_kbd.setKey({command.key, command.color});
				break;
			case ProfileCommand::Type::region:
				stageRegion(command.region, fromLedColor(command.color));
				break;
			case ProfileCommand::Type::mr:
				if (honorDevice && m_kbd.isOpen())
					m_kbd.setMRKey(command.value);
				m_hasAppliedGKeys = true;
				m_appliedMR = command.value;
				m_mrKeyCombo.set_active(command.value ? 1 : 0);
				break;
			case ProfileCommand::Type::mn:
				if (honorDevice && m_kbd.isOpen())
					m_kbd.setMNKey(command.value);
				m_hasAppliedGKeys = true;
				m_appliedMN = command.value;
				{
					int idx = command.value;
					if (m_layoutModel == LedKeyboard::KeyboardModel::g815 && command.value > 0) idx = command.value - 1;
					m_mnKeyCombo.set_active(std::max(0, idx));
				}
				break;
			case ProfileCommand::Type::gkm:
				if (honorDevice && m_kbd.isOpen())
					m_kbd.setGKeysMode(command.value);
				m_hasAppliedGKeys = true;
				m_appliedGKeysMode = command.value;
				m_gKeysModeCombo.set_active(command.value ? 1 : 0);
				break;
			case ProfileCommand::Type::startupMode:
				if (honorDevice && m_kbd.isOpen())
					m_kbd.setStartupMode(command.startupMode);
				m_hasAppliedStartup = true;
				m_appliedStartup = command.startupMode;
				m_startupModeCombo.set_active(command.startupMode == LedKeyboard::StartupMode::color ? 1 : 0);
				break;
			case ProfileCommand::Type::onBoardMode:
				if (honorDevice && m_kbd.isOpen())
					m_kbd.setOnBoardMode(command.onBoardMode);
				m_hasAppliedOnBoard = true;
				m_appliedOnBoard = command.onBoardMode;
				m_onBoardModeCombo.set_active(command.onBoardMode == LedKeyboard::OnBoardMode::software ? 1 : 0);
				break;
			case ProfileCommand::Type::fx: {
				if (honorDevice && m_kbd.isOpen())
					m_kbd.setNativeEffect(command.effect, command.part,
						command.period, command.color, command.storage);
				// Mirror it into the panel, or the controls would describe
				// a different effect from the one now running (and Save
				// would write that different one back out).
				static const LedKeyboard::NativeEffect effectOrder[] = {
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
				for (size_t i = 0; i < sizeof(effectOrder) / sizeof(effectOrder[0]); ++i) {
					if (effectOrder[i] == command.effect) {
						m_effectCombo.set_active((int)i);
						break;
					}
				}
				m_targetCombo.set_active(
					command.part == LedKeyboard::NativeEffectPart::keys ? 1 :
					command.part == LedKeyboard::NativeEffectPart::logo ? 2 : 0);
				m_effectColorButton.set_rgba(fromLedColor(command.color));
				if (command.period.count() > 0)
					m_periodSpin.set_value(command.period.count());
				m_storeCheck.set_active(
					command.storage == LedKeyboard::NativeEffectStorage::user);
				m_hasAppliedEffect = true;
				m_appliedEffect = command.effect;
				m_appliedEffectPart = command.part;
				m_appliedEffectPeriod = command.period;
				m_appliedEffectColor = command.color;
				m_appliedEffectStorage = command.storage;
				break;
			}
			case ProfileCommand::Type::commit:
				if (honorDevice)
					onApplyKeyboard();
				break;
			case ProfileCommand::Type::audio:
			case ProfileCommand::Type::screen:
			case ProfileCommand::Type::rain:
			case ProfileCommand::Type::wave:
			case ProfileCommand::Type::wavePoint:
			case ProfileCommand::Type::waveColor:
				break;
		}
	}
}

void MainWindow::markDraftApplied() {
	m_appliedColors = m_keyboardWidget.getKeyColors();
	m_regionApplied = m_regionDraft;
	m_allKeysApplied = m_allKeysDraft;
}

std::vector<std::string> MainWindow::recentProfiles() const {
	std::vector<std::string> paths;
	std::ifstream file(Glib::get_user_config_dir() + "/g810-led/recent");
	std::string line;
	while (std::getline(file, line))
		if (!line.empty())
			paths.push_back(line);
	return paths;
}

// Which profile is open, in the one place a document's name belongs.
// The strip on the right deliberately leaves it out — it says what the
// keyboard is showing, and after the first edit the profile's name is no
// longer a true answer to that. Which file Ctrl+S will overwrite is a
// different question, it has no other answer anywhere in the window, and
// a title bar is what a title bar is for.
static void showProfileInTitle(Gtk::Window &window, const std::string &name) {
	window.set_title(name.empty() ? "g810-led" : name + " — g810-led");
}

void MainWindow::rememberProfile(const std::string &path) {
	if (path.empty())
		return;
	m_currentProfilePath = path;
	m_currentProfileName = path.substr(path.find_last_of('/') + 1);
	showProfileInTitle(*this, m_currentProfileName);
	std::vector<std::string> paths = recentProfiles();
	paths.erase(std::remove(paths.begin(), paths.end(), path), paths.end());
	paths.insert(paths.begin(), path);
	if (paths.size() > 5)
		paths.resize(5);
	std::string dir = Glib::get_user_config_dir() + "/g810-led";
	g_mkdir_with_parents(dir.c_str(), 0755);
	std::ofstream out(dir + "/recent");
	for (size_t i = 0; i < paths.size(); ++i)
		out << paths[i] << "\n";
	rebuildRecentMenu();
	updateSavedSection();
	updateBoardState();
}

void MainWindow::rebuildRecentMenu() {
	m_recentMenu.foreach([this](Gtk::Widget &child) { m_recentMenu.remove(child); });
	std::vector<std::string> paths = recentProfiles();
	if (paths.empty()) {
		Gtk::MenuItem *empty = Gtk::manage(
			new Gtk::MenuItem("Nothing opened yet"));
		empty->set_sensitive(false);
		m_recentMenu.append(*empty);
	}
	for (size_t i = 0; i < paths.size(); ++i) {
		const std::string path = paths[i];
		Gtk::MenuItem *item = Gtk::manage(
			new Gtk::MenuItem(path.substr(path.find_last_of('/') + 1)));
		item->set_tooltip_text(path);
		item->signal_activate().connect([this, path]() { loadProfileFile(path); });
		m_recentMenu.append(*item);
	}
	m_recentMenu.show_all();
	m_recentItem.set_sensitive(true);
}

// One line of the shortcut list — or, with nothing in the second field, a
// heading over the lines that follow it.
struct ShortcutRow { const char *press; const char *does; };

// One column of the list. Nineteen rows at one weight in one stack is a
// wall of text, and it read as a wall: the way to find "how do I turn the
// board" in it was to read all nineteen. Two columns split what acts on
// the window from what acts on the board, and the sub-headings inside a
// column split that again — so the answer is found by looking at four
// headings rather than by reading every line.
static void fillShortcutColumn(Gtk::Grid *grid, const char *heading,
                               const ShortcutRow *rows, size_t count) {
	Gtk::Label *top = Gtk::manage(new Gtk::Label(heading));
	top->get_style_context()->add_class("kb-heading");
	top->set_xalign(0);
	grid->attach(*top, 0, 0, 2, 1);
	int line = 1;
	for (size_t i = 0; i < count; ++i) {
		if (!rows[i].does) {
			Gtk::Label *group = Gtk::manage(new Gtk::Label(rows[i].press));
			group->get_style_context()->add_class("kb-title");
			group->set_xalign(0);
			// Room above a heading, none below it, so each group reads as
			// belonging to the words over it instead of floating halfway
			// between two of them. .kb-title carries 8px of its own, so
			// this is the difference and not the whole gap.
			group->set_margin_top(line == 1 ? 0 : 12);
			grid->attach(*group, 0, line++, 2, 1);
			continue;
		}
		Gtk::Label *press = Gtk::manage(new Gtk::Label(rows[i].press));
		press->get_style_context()->add_class("kb-shortcut");
		press->set_xalign(1);
		press->set_yalign(0);
		Gtk::Label *does = Gtk::manage(new Gtk::Label(rows[i].does));
		does->set_xalign(0);
		does->set_yalign(0);
		// Wraps rather than widening the dialog past the window it opens
		// over; a person on a 980px screen has to be able to read it too.
		// One width, asked for and accepted: a wrapping label whose
		// minimum is narrower than its natural is a label whose height
		// depends on which of the two it is measured at, and a
		// GtkScrolledWindow measures its child's height at the minimum —
		// which is how this dialog came out 1400px tall for 600px of list.
		does->set_line_wrap(true);
		does->set_width_chars(34);
		does->set_max_width_chars(34);
		grid->attach(*press, 0, line, 1, 1);
		grid->attach(*does, 1, line, 1, 1);
		line++;
	}
}

void MainWindow::onShowShortcuts() {
	// Named for what is in it. Half of this list is what the mouse can do
	// to the board, and a window headed "Keyboard shortcuts" is the wrong
	// place to look for that — doubly so here, where "keyboard" is also
	// the thing on the desk.
	Gtk::Dialog dialog("Shortcuts and gestures", *this, true);
	Gtk::Button *close = dialog.add_button("Close", Gtk::RESPONSE_CLOSE);
	dialog.set_default_response(Gtk::RESPONSE_CLOSE);
	if (close)
		close->grab_default();

	// One heading, not two. "Your colors" and "Profiles" were the same
	// six keys under two names, and the split was the clearest statement
	// in the window that it had not decided what to call the thing it
	// saves.
	static const ShortcutRow window[] = {
		{"Your colors", NULL},
		{"Ctrl+Enter", "Send the colors to the keyboard"},
		{"Ctrl+Z", "Undo the last color change"},
		{"Ctrl+Shift+Z / Ctrl+Y", "Put that change back"},
		{"Ctrl+O", "Open colors from a file"},
		{"Ctrl+S", "Save them back to that file"},
		{"Ctrl+Shift+S", "Save them under a new name"},
		{"Help and quitting", NULL},
		{"F1 / Ctrl+?", "This list"},
		{"Ctrl+Q", "Quit"},
	};
	// The board answers to gestures that nothing on screen can state
	// without a sentence printed across it for the rest of the session,
	// so they are stated here, where someone wondering what they can do
	// comes looking.
	// Every act here is listed twice, once for each hand: the mouse
	// gesture and the key that does the same thing. The list used to be
	// gestures alone, which said in the plainest possible way that the
	// one act this program exists for was not for people who do not use
	// a mouse.
	static const ShortcutRow board[] = {
		{"Painting and choosing keys", NULL},
		{"Alt+P / Alt+S", "Switch the Paint and Select tools"},
		{"Click or drag (Paint)", "Color one key, or a run of them"},
		{"Click or drag (Select)", "Choose one, or rubber-band a group"},
		{"Shift-click", "Add a key, or take one out"},
		{"Double-click (Paint)", "Choose a color for that one key"},
		{"Double-click (Select)", "Choose every key of that color"},
		{"Right-click", "What can be done to that key"},
		{"Escape", "Drop the selection, abandon a drag"},
		{"The board, from the keys", NULL},
		{"Tab", "Step onto the board"},
		{"Arrow keys", "Move from key to key"},
		{"Home / End", "First or last key of the row"},
		{"Return or space", "Color that key (or choose it, in Select)"},
		{"Shift+Return", "Add that key, or take it out"},
		{"Delete", "Turn that key off"},
		{"Menu key", "What can be done to that key"},
		{"Turning the 3D board", NULL},
		{"Right-drag", "Turn the keyboard"},
		{"Middle-drag", "Slide it without turning it"},
		{"Scroll", "Move closer or further away"},
		{"Ctrl+Home", "Put it back the way it started"},
	};

	Gtk::Box *columns = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 36));
	columns->set_margin_top(14);
	columns->set_margin_bottom(14);
	columns->set_margin_start(18);
	columns->set_margin_end(18);
	const struct { const char *heading; const ShortcutRow *rows; size_t count; }
	lists[] = {
		{"The window", window, sizeof(window) / sizeof(window[0])},
		{"The board", board, sizeof(board) / sizeof(board[0])},
	};
	for (size_t i = 0; i < sizeof(lists) / sizeof(lists[0]); ++i) {
		Gtk::Grid *grid = Gtk::manage(new Gtk::Grid());
		grid->set_row_spacing(5);
		grid->set_column_spacing(14);
		grid->set_valign(Gtk::ALIGN_START);
		fillShortcutColumn(grid, lists[i].heading, lists[i].rows, lists[i].count);
		columns->pack_start(*grid, true, true);
	}

	// A list this long has to survive a short screen: the dialog can be
	// made smaller than its contents, and then this scrolls rather than
	// putting Close off the bottom edge where nothing can reach it.
	Gtk::ScrolledWindow *scroll = Gtk::manage(new Gtk::ScrolledWindow());
	scroll->set_policy(Gtk::POLICY_NEVER, Gtk::POLICY_AUTOMATIC);
	scroll->set_propagate_natural_height(true);
	scroll->set_propagate_natural_width(true);
	scroll->add(*columns);
	dialog.get_content_area()->pack_start(*scroll, true, true);
	// A rule and some room under it, so Close reads as the way out of the
	// dialog rather than as one more thing in the list. It used to sit in
	// the corner with 20px between it and the last row.
	Gtk::Separator *rule =
		Gtk::manage(new Gtk::Separator(Gtk::ORIENTATION_HORIZONTAL));
	dialog.get_content_area()->pack_start(*rule, false, false);
	if (Gtk::Box *actions = dialog.get_action_area()) {
		actions->set_margin_top(10);
		actions->set_margin_bottom(10);
		actions->set_margin_start(18);
		actions->set_margin_end(18);
	}
	dialog.show_all();
	dialog.run();
}

void MainWindow::persistLastProfile() {
	std::string dir = Glib::get_user_config_dir() + "/g810-led";
	g_mkdir_with_parents(dir.c_str(), 0755);
	std::ofstream file(lastProfilePath());
	if (!file.is_open())
		return;
	file << serializeProfileText(draftProfileCommands());
}

void MainWindow::restorePreview() {
	std::vector<std::string> paths;
	paths.push_back(lastProfilePath());
	paths.push_back("/etc/g810-led/profile");
	for (size_t i = 0; i < paths.size(); ++i) {
		std::ifstream file(paths[i]);
		if (!file.is_open())
			continue;
		std::stringstream buffer;
		buffer << file.rdbuf();
		std::vector<ProfileCommand> commands;
		std::string error;
		parseProfileText(buffer.str(), commands, error);
		if (commands.empty())
			continue;
		stageProfileCommands(commands, false);
		m_currentProfileName = paths[i].substr(paths[i].find_last_of('/') + 1);
		// The session file is our own bookkeeping, not a profile the user
		// manages: only a real profile becomes the Ctrl+S target.
		//
		// Neither of these two goes in the title, though the second is a
		// real file. The title answers "which profile am I working on",
		// and the answer to that at launch is "none yet": one candidate is
		// this window's own scratch file and the other is the packaged
		// default at /etc/g810-led/profile, whose basename is the word
		// "profile" — a window headed "profile — g810-led" for a document
		// nobody opened says less than one headed with nothing.
		if (paths[i] != lastProfilePath())
			m_currentProfilePath = paths[i];

		// Restoring the board is not the same as restoring the keyboard,
		// and this window cannot tell the two apart: the hardware is
		// write-only for per-key colour, so what the keys are actually
		// showing is unknowable. Marking the draft applied here — which is
		// all this used to do — asserted that they already matched. When
		// they did not, the window opened insisting there was nothing to
		// send while showing colours the keyboard had never been given,
		// and the one button that could have fixed it was greyed out on
		// the strength of the assertion. So make the claim true rather
		// than merely make it.
		if (rainActive()) {
			// Except when something is already driving the keys. A live
			// effect owns them until it stops, and the scheme underneath is
			// what it hands back then — which is exactly what marking the
			// draft applied records.
			markDraftApplied();
			status("Your colors are ready — an effect is running, so they "
				"come back when it stops");
		} else if (!m_kbd.isOpen() || applyDraft() == ApplyResult::failed) {
			// Nothing written, so nothing claimed: the board shows the
			// colours, the strip says they are unsent, and Send is live to
			// try again. applyDraft has already said why it could not.
		} else {
			markDraftApplied();
		}
		// This is the baseline the session starts from, not an edit the
		// user made, so it must not be sitting on the undo stack.
		m_undoStack.clear();
		m_redoStack.clear();
		updateHistoryUI();
		updatePendingState();
		// Resume whichever software animation the profile was saved
		// with — but never fight a daemon that is already running it.
		if (!rainActive()) {
			// Quietly at startup: the chooser and its caption already
			// say what this profile wants, and a notice the user has to
			// dismiss at every launch would be noise.
			applyScreenFromCommands(commands, false);
			if (!applyAudioFromCommands(commands) &&
			    !applyWaveFromCommands(commands))
				applyRainFromCommands(commands);
		}
		return;
	}
}

void MainWindow::onLoadProfile() {
	Gtk::FileChooserDialog dialog("Open colors", Gtk::FILE_CHOOSER_ACTION_OPEN);
	dialog.set_transient_for(*this);
	dialog.add_button("Cancel", Gtk::RESPONSE_CANCEL);
	dialog.add_button("Load", Gtk::RESPONSE_OK);
	Glib::RefPtr<Gtk::FileFilter> filter = Gtk::FileFilter::create();
	filter->set_name("Color files (*.profile)");
	filter->add_pattern("*.profile");
	filter->add_pattern("*.txt");
	dialog.add_filter(filter);
	Glib::RefPtr<Gtk::FileFilter> all = Gtk::FileFilter::create();
	all->set_name("All files");
	all->add_pattern("*");
	dialog.add_filter(all);
	if (dialog.run() != Gtk::RESPONSE_OK)
		return;
	loadProfileFile(dialog.get_filename());
}

void MainWindow::loadProfileFile(const std::string &path) {
	stopRainEverywhere();
	std::ifstream file(path);
	if (!file.is_open()) {
		statusError("Could not open " + path);
		return;
	}
	// Only now: adopting the file retitles the window, points Ctrl+S at it
	// and puts it at the head of the recent list, evicting one that works.
	// A path that cannot even be opened has earned none of that.
	rememberProfile(path);
	std::stringstream buffer;
	buffer << file.rdbuf();

	std::vector<ProfileCommand> commands;
	std::string error;
	std::vector<std::string> errors;
	bool parsed = parseProfileText(buffer.str(), commands, error, &errors);

	// Instant-lane commands need the device; open once for the whole
	// load. Staged commands only touch the draft.
	bool needDevice = false;
	for (const ProfileCommand &command : commands) {
		if (command.type == ProfileCommand::Type::mr ||
		    command.type == ProfileCommand::Type::mn ||
		    command.type == ProfileCommand::Type::gkm ||
		    command.type == ProfileCommand::Type::startupMode ||
		    command.type == ProfileCommand::Type::onBoardMode ||
		    command.type == ProfileCommand::Type::fx ||
		    command.type == ProfileCommand::Type::rain ||
		    command.type == ProfileCommand::Type::wave ||
		    command.type == ProfileCommand::Type::audio) {
			needDevice = true;
			break;
		}
	}
	if (needDevice)
		ensureOpen();

	bool committed = false;
	bool applyFailed = false;
	for (const ProfileCommand &command : commands) {
		if (command.type == ProfileCommand::Type::commit) {
			// An empty delta means the board already matches the profile,
			// which is a successful load, not a connection problem.
			ApplyResult result = applyDraft();
			if (result == ApplyResult::applied)
				committed = true;
			else if (result == ApplyResult::failed)
				applyFailed = true;
			continue;
		}
		std::vector<ProfileCommand> one(1, command);
		stageProfileCommands(one, true);
	}
	updatePendingState();
	const char *started = NULL;
	// Loads the controls and says what the profile wants; it never
	// starts the capture, so it is not part of the chain below.
	applyScreenFromCommands(commands, true);
	if (applyAudioFromCommands(commands)) started = "sound reactive";
	else if (applyWaveFromCommands(commands)) started = "wave";
	else if (applyRainFromCommands(commands)) started = "raindrop";
	if (!parsed) {
		statusError("Profile loaded with " + std::to_string(errors.size()) +
			(errors.size() == 1 ? " problem — " : " problems — ") + error, errors);
	} else if (applyFailed) {
		status("Profile loaded, but the colors could not be sent");
	} else if (started) {
		status(std::string("Profile loaded — ") + started + " running");
	} else if (committed) {
		status("Profile loaded and sent to the keyboard");
	} else {
		status("Profile loaded — press Send to keyboard");
	}
}

// Ctrl+S writes back to the profile that is loaded; only an unnamed
// draft has to go through the chooser.
void MainWindow::onSaveCurrent() {
	if (m_currentProfilePath.empty()) {
		onSaveProfile();
		return;
	}
	writeProfile(m_currentProfilePath);
}

void MainWindow::onSaveProfile() {
	Gtk::FileChooserDialog dialog("Save colors as", Gtk::FILE_CHOOSER_ACTION_SAVE);
	dialog.set_transient_for(*this);
	dialog.set_do_overwrite_confirmation(true);
	dialog.add_button("Cancel", Gtk::RESPONSE_CANCEL);
	dialog.add_button("Save", Gtk::RESPONSE_OK);
	Glib::RefPtr<Gtk::FileFilter> filter = Gtk::FileFilter::create();
	filter->set_name("Color files (*.profile)");
	filter->add_pattern("*.profile");
	filter->add_pattern("*.txt");
	dialog.add_filter(filter);
	Glib::RefPtr<Gtk::FileFilter> allFiles = Gtk::FileFilter::create();
	allFiles->set_name("All files");
	allFiles->add_pattern("*");
	dialog.add_filter(allFiles);
	if (dialog.run() != Gtk::RESPONSE_OK)
		return;

	std::string path = dialog.get_filename();
	if (path.find('.') == std::string::npos)
		path += ".profile";
	writeProfile(path);
}

void MainWindow::writeProfile(const std::string &path) {
	std::ofstream file(path);
	if (!file.is_open()) {
		statusError("Could not write " + path);
		return;
	}
	file << serializeProfileText(draftProfileCommands());
	if (!file.good()) {
		statusError("Failed to write " + path);
		return;
	}
	rememberProfile(path);
	status("Saved " + path);
}

void MainWindow::onLoadProfileText() {
	// Named for what the person does, not for the pipe it stands in for.
	// The title used to read "Load profile text (stdin/pipe equivalent)",
	// which is a sentence about the command-line tool this window was
	// grown from and means nothing to anyone who has not read its manual.
	Gtk::Dialog dialog("Paste colors as text", *this, true);
	dialog.add_button("Cancel", Gtk::RESPONSE_CANCEL);
	Gtk::Button *load = dialog.add_button("Load", Gtk::RESPONSE_OK);
	if (load)
		load->get_style_context()->add_class("suggested-action");
	dialog.set_default_response(Gtk::RESPONSE_OK);
	dialog.set_default_size(560, 380);

	// An empty box and a Load button is a memory test: the format is the
	// one the profile files are written in, and nothing on screen said so
	// or showed a line of it. Four lines is the whole of what most
	// profiles use, and it is quicker to read than any description of it.
	Gtk::Label *what = Gtk::manage(new Gtk::Label(
		"The same lines a saved profile is written in — one command per "
		"line, comments after #."));
	what->get_style_context()->add_class("dim-label");
	what->set_xalign(0);
	what->set_line_wrap(true);
	Gtk::Label *example = Gtk::manage(new Gtk::Label());
	example->set_markup(
		"<tt>a 202020        <span alpha='55%'># every key</span>\n"
		"g fkeys ff8800  <span alpha='55%'># one group</span>\n"
		"k g 66ccff      <span alpha='55%'># one key</span>\n"
		"c               <span alpha='55%'># send it to the keyboard</span></tt>");
	example->get_style_context()->add_class("kb-hint");
	example->set_xalign(0);

	Gtk::TextView *tv = Gtk::manage(new Gtk::TextView());
	tv->set_editable(true);
	tv->set_monospace(true);
	tv->set_wrap_mode(Gtk::WRAP_WORD_CHAR);
	tv->set_left_margin(6);
	tv->set_top_margin(4);
	if (Glib::RefPtr<Atk::Object> atk = tv->get_accessible())
		atk->set_name("Colors as text");
	Gtk::ScrolledWindow *sw = Gtk::manage(new Gtk::ScrolledWindow());
	sw->add(*tv);
	sw->get_style_context()->add_class("kb-well");
	sw->set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);

	Gtk::Box *body = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_VERTICAL, 8));
	body->set_margin_top(12);
	body->set_margin_bottom(12);
	body->set_margin_start(14);
	body->set_margin_end(14);
	body->pack_start(*what, false, false);
	body->pack_start(*example, false, false);
	body->pack_start(*sw, true, true);
	dialog.get_content_area()->pack_start(*body, true, true);
	if (Gtk::Box *actions = dialog.get_action_area()) {
		actions->set_margin_bottom(10);
		actions->set_margin_start(14);
		actions->set_margin_end(14);
	}
	dialog.get_content_area()->show_all();

	if (dialog.run() != Gtk::RESPONSE_OK) return;

	std::string text = tv->get_buffer()->get_text();
	if (text.empty()) return;

	stopRainEverywhere();

	std::vector<ProfileCommand> commands;
	std::string error;
	std::vector<std::string> errors;
	bool parsed = parseProfileText(text, commands, error, &errors);

	bool needDevice = false;
	for (const ProfileCommand &command : commands) {
		if (command.type == ProfileCommand::Type::mr ||
		    command.type == ProfileCommand::Type::mn ||
		    command.type == ProfileCommand::Type::gkm ||
		    command.type == ProfileCommand::Type::startupMode ||
		    command.type == ProfileCommand::Type::onBoardMode ||
		    command.type == ProfileCommand::Type::fx ||
		    command.type == ProfileCommand::Type::rain ||
		    command.type == ProfileCommand::Type::wave ||
		    command.type == ProfileCommand::Type::audio) {
			needDevice = true;
			break;
		}
	}
	if (needDevice) ensureOpen();

	stageProfileCommands(commands, true);
	updatePendingState();
	const char *started = NULL;
	// Loads the controls and says what the profile wants; it never
	// starts the capture, so it is not part of the chain below.
	applyScreenFromCommands(commands, true);
	if (applyAudioFromCommands(commands)) started = "sound reactive";
	else if (applyWaveFromCommands(commands)) started = "wave";
	else if (applyRainFromCommands(commands)) started = "raindrop";
	if (!parsed) {
		statusError("Text loaded with " + std::to_string(errors.size()) +
			(errors.size() == 1 ? " problem — " : " problems — ") + error, errors);
	} else if (started) {
		status(std::string("Profile text sent to the keyboard — ") + started +
			" running");
	} else {
		status("Profile text loaded");
	}
}

bool MainWindow::onWindowKeyPress(GdkEventKey *event) {
	// Ctrl+S and Ctrl+Shift+S are accelerators on the File menu now, so
	// there is nothing to intercept here but Escape.
	if (event->keyval == GDK_KEY_Escape) {
		m_keyboardWidget.cancelDrag();
		m_keyboardWidget.clearSelection();
		return true;
	}
	return false;
}

bool MainWindow::onDeleteEvent(GdkEventAny *) {
	if (hasPendingChanges()) {
		// The same three words the window uses everywhere else, on the one
		// screen where getting them wrong costs work: the question is
		// whether to send, the button that throws the colors away says
		// Discard, and how much is at stake is counted rather than called
		// "changes" — the strip behind this dialog is showing that same
		// number, so the two cannot be read as different piles of work.
		const std::string count = std::to_string(m_pendingCount) +
			(m_pendingCount == 1 ? " color change has" : " color changes have");
		const char *animation = activeAnimationName();
		Gtk::MessageDialog dialog(*this,
			"Send your changes to the keyboard before closing?",
			false, Gtk::MESSAGE_QUESTION, Gtk::BUTTONS_NONE, true);
		dialog.set_secondary_text(count + " not been sent to the keyboard. " +
			(animation ?
				std::string("Sending stops ") + animation +
					", which is driving the keyboard now." :
				std::string("Discard throws the work away for good.")));
		dialog.add_button("Cancel", Gtk::RESPONSE_CANCEL);
		dialog.add_button("Discard", Gtk::RESPONSE_REJECT);
		// The label the button in the window carries, so the answer to
		// "what does this do" is one word the user has already read.
		Gtk::Widget *send = dialog.add_button("Send to keyboard",
			Gtk::RESPONSE_ACCEPT);
		// Keeping the work is the safe answer and the likely one, so it is
		// what Enter does and what the eye lands on; throwing it away is
		// marked as the destructive one. Escape still cancels.
		if (send)
			send->get_style_context()->add_class("suggested-action");
		if (Gtk::Widget *drop = dialog.get_widget_for_response(Gtk::RESPONSE_REJECT))
			drop->get_style_context()->add_class("destructive-action");
		dialog.set_default_response(Gtk::RESPONSE_ACCEPT);
		int response = dialog.run();
		if (response == Gtk::RESPONSE_ACCEPT) {
			// applyDraft() refuses while an animation owns the device, so
			// take the board back first — otherwise Send silently does
			// nothing and the window can never be closed that way.
			if (rainActive())
				stopRainEverywhere();
			if (!onApplyKeyboard())
				return true;
		} else if (response == Gtk::RESPONSE_REJECT) {
			// Discarded: put the draft back to what is on the keyboard,
			// so nothing downstream (a daemon handoff, the persisted
			// profile) keeps the changes the user just threw away.
			onRevert();
		} else {
			return true;
		}
	}
	// With somewhere to put an icon, closing does not end anything: the
	// window hides, this process goes on driving the effect, and the
	// tray says what is running and offers to stop it. That is also the
	// disclosure the screen lane needs, so it is not asked about here.
	if (rainActive() && TrayIcon::available()) {
		m_trayLaneShown = -2;   // force one rebuild for this appearance
		m_trayModeShown = -2;
		m_tray.show(m_trayMenu);
		updateTray();
		hide();
		status("Still running — the tray icon has the controls");
		return true;   // keep the window, hidden
	}

	// Otherwise the old behaviour, for desktops with no tray: hand any
	// running animation to a detached daemon so it outlives us. Doing
	// this earlier would spawn a daemon behind a window the user then
	// kept open.
	handoffRainDaemon();
	handoffAudioDaemon();
	// Screen colors is not handed over silently like those two: it
	// reads the display, so closing the window asks first. Answered
	// "stop", the board goes back to the user's colors rather than
	// freezing on whatever frame happened to be last.
	if (m_screenPlayer.isRunning()) {
		if (askKeepScreenRunning()) {
			handoffScreenDaemon();
		} else {
			m_screenPlayer.stop();
			m_expectedAnimation.clear();
			m_expectedByDaemon = false;
			clearPreviewState();
			restoreAppliedState();
		}
	}
	// The main loop is no longer tied to this window (see gui/main.cpp),
	// so leaving is now something to say out loud.
	Gtk::Main::quit();
	return false;
}

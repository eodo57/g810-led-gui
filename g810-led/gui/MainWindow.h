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

#ifndef MAIN_WINDOW
#define MAIN_WINDOW

#include <gtkmm.h>

#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "../src/classes/Keyboard.h"
#include "../src/helpers/help.h"

#include "AudioPlayer.h"
#include "ColorPicker.h"
#include "KeyboardWidget.h"
#include "ProfileParser.h"
#include "Raindrop.h"
#include "ScreenPlayer.h"
#include "TrayIcon.h"
#include "WaveGraph.h"
#include "WavePlayer.h"

// Main window: on-screen keyboard on the left, control panel on the
// right, status bar at the bottom. Talks to the keyboard through the
// LedKeyboard API from libg810-led.so.
//
// Apply flow (key-color lane): every paint/group/selection edit only
// updates the on-screen draft; "Apply to keyboard" flushes the draft
// delta to the device with one setKeys + one commit (the CLI's -pp
// batch path). "Revert" restores the last-applied snapshot. The
// effect/G-keys/startup/on-board/region lanes are self-contained
// packets and apply immediately.
class MainWindow : public Gtk::Window {
	public:
		MainWindow();
		virtual ~MainWindow();

	private:
		struct GroupEntry { const char *label; LedKeyboard::KeyGroup group; };

		static bool startupModeSupported(LedKeyboard::KeyboardModel model);
		static KeyboardWidget::LayoutFlags layoutFlagsFor(
			help::KeyboardFeatures features, LedKeyboard::KeyboardModel model);
		bool has(help::KeyboardFeatures flag) const;

		void rescanDevices();
		bool openSelectedDevice();
		bool ensureOpen();
		void onManualDeviceToggled();
		void tryManualOpen();
		void onListKeyboards();
		void onPrintDevice();
		void refreshFeatures();
		void setControlsEnabled(bool enabled);
		void status(const std::string &message);

		LedKeyboard::Color currentColor() const;
		Gdk::RGBA currentRGBA() const;

		// Staged draft (key-color / region lane)
		// false when the key already held that colour: nothing was
		// staged, nothing was pushed onto the undo stack, and whatever is
		// about to be said had better not claim otherwise.
		bool stageKey(LedKeyboard::Key key, const Gdk::RGBA &color);
		// How many of them the board did not already show in that colour.
		// Zero means nothing was staged and nothing was pushed onto the
		// undo stack; whatever is said afterwards has to match.
		size_t stageKeys(const std::vector<LedKeyboard::Key> &keys,
		                 const Gdk::RGBA &color);
		void stageRegion(int region, const Gdk::RGBA &color);
		void stageAllKeys(const Gdk::RGBA &color);
		void updatePendingState();
		// Draft history. Painting used to be one-way: "All keys" replaced a
		// hand-built board with no way back except discarding everything.
		struct DraftSnapshot {
			std::map<LedKeyboard::Key, Gdk::RGBA> keys;
			std::map<int, Gdk::RGBA> regions;
			Gdk::RGBA allKeys;
		};
		DraftSnapshot snapshotDraft() const;
		void applySnapshot(const DraftSnapshot &snapshot);
		void pushUndo();
		void onUndo();
		void onRedo();
		void updateHistoryUI();
		void stopAnimationIfRunning();
		bool hasPendingChanges() const;
		// Writing the draft has three outcomes, and callers care which:
		// an empty delta is a successful no-op, not a device failure.
		enum class ApplyResult { applied, nothingToDo, failed };
		ApplyResult applyDraft();
		// Native effects and software animations take the LEDs over and
		// leave them wherever they stopped, while the window still shows
		// the scheme underneath. Writing that scheme back is what makes
		// them a preview instead of a one-way door.
		bool restoreAppliedState();
		bool onApplyKeyboard();
		void onRevert();
		void onLoadProfile();
		void loadProfileFile(const std::string &path);
		void onSaveProfile();      // Save as… (always asks)
		void onSaveCurrent();      // Ctrl+S: overwrite the loaded profile
		void writeProfile(const std::string &path);
		// Recent profiles, so a scheme is one click away instead of a file
		// chooser and a remembered directory.
		std::vector<std::string> recentProfiles() const;
		void rememberProfile(const std::string &path);
		void rebuildRecentMenu();
		// The card under the board: which file the colors came from, the
		// two buttons that fetch and keep them, and the last few files.
		void buildSavedSection(Gtk::Box *parent);
		void updateSavedSection();
		void onShowShortcuts();
		void onLoadProfileText();
		bool onDeleteEvent(GdkEventAny *event);

		// The status icon that stands in for the window while a live
		// effect runs, and the menu the desktop draws for it.
		void buildTrayMenu();
		void updateTray();
		void showFromTray();
		void quitFromTray();
		void chooseLaneFromTray(int row);
		void chooseModeFromTray(int row);
		void fillSubmenuFromCombo(Gtk::MenuItem &item, Gtk::Menu *&menu,
		                          Gtk::ComboBoxText &combo,
		                          const sigc::slot<void, int> &chosen);
		// What the board is doing, shared by the strip and the tray.
		std::string boardStateText(bool *sensor = NULL) const;

		// Every live effect hands its frames here instead of straight to
		// the on-screen keyboard: the device wants every frame, the
		// screen does not, and a window nobody can see wants none.
		void previewFrame(const LedKeyboard::KeyValueArray &values);
		bool flushPreview();
		bool onWindowState(GdkEventWindowState *event);

		void onViewChanged();
		void onModeToggled();
		// The line under the board that says what the left button does.
		void updateModeHint();

		void buildUI();
		void buildMenuBar(Gtk::Box* vbox);
		void buildLeftSide(Gtk::Paned* paned);
		void buildRightSide(Gtk::Box* rightCol);
		void setupSignals();
		void setupAccelerators();

		// The one place that always says what the keyboard is doing.
		void buildStateStrip(Gtk::Box* rightCol);
		void updateBoardState();
		// sticky: the notice stays until the user dismisses it or another
		// error replaces it, instead of being superseded by the next
		// ordinary status message. For things the user must actually see.
		void statusError(const std::string &message,
		                 const std::vector<std::string> &details =
		                     std::vector<std::string>(),
		                 bool sticky = false);
		void setBusy(bool busy);
		Gtk::Box* addSection(Gtk::Box* parent, const std::string& title);
		Gtk::Box* makeTab(Gtk::Notebook* tabs, const char* name);

		TrayIcon m_tray;
		Gtk::Menu m_trayMenu;
		Gtk::MenuItem m_trayStatusItem;
		Gtk::MenuItem m_trayDeviceItem;
		Gtk::MenuItem m_trayShowItem;
		Gtk::MenuItem m_trayStopItem;
		Gtk::MenuItem m_trayLaneItem;
		Gtk::MenuItem m_trayModeItem;
		Gtk::MenuItem m_trayQuitItem;
		Gtk::Menu *m_trayLaneMenu = nullptr;
		Gtk::Menu *m_trayModeMenu = nullptr;
		bool m_trayIgnore = false;   // filling a submenu, not a user click
		// What the submenus were last built for, so they are rebuilt
		// (and re-exported over the bus) only when they would differ.
		int m_trayLaneShown = -2;
		int m_trayModeShown = -2;

		// Frames waiting to be drawn, merged so a coalesced frame never
		// drops a key that changed in it.
		std::map<LedKeyboard::Key, LedKeyboard::Color> m_previewPending;
		sigc::connection m_previewTimer;
		gint64 m_lastPreviewAt = 0;
		bool m_previewHidden = false;

		static const char *modelFileName(LedKeyboard::KeyboardModel model);
		std::string lastProfilePath() const;
		std::vector<ProfileCommand> draftProfileCommands() const;
		void stageProfileCommands(const std::vector<ProfileCommand> &commands,
		                          bool honorDevice);
		void markDraftApplied();
		void persistLastProfile();
		void restorePreview();
		bool applyRainFromCommands(const std::vector<ProfileCommand> &commands);

		void onKeyPressed(LedKeyboard::Key key);
		void onKeyPick(LedKeyboard::Key key);
		void onKeyCleared(LedKeyboard::Key key);
		// Where the arrow keys have landed on the board, said out loud.
		void onCursorMoved(LedKeyboard::Key key);
		void onKeyMenu(LedKeyboard::Key key);
		void onColorChosen();
		void onPaintAll();
		void onPaintSelection();
		void onClearSelectionColor();
		void onDeselectSelection();
		void onSelectionChanged();
		void onApplyEffect();
		void onEffectOff();
		void onEffectChanged();
		void onApplyGKeys();
		void onApplyStartupMode();
		void onApplyOnBoardMode();
		bool onRegionPress(GdkEventButton *event, int region);
		// The four software animations are mutually exclusive, so the UI
		// presents them as one chooser with one Start/Stop button.
		void onLiveToggle();
		void onLiveModeChanged();
		void startSelectedLive();
		void stopLive();
		void buildLiveSection(Gtk::Box* liveBox);
		void startAnimation(bool overlay);
		void updateAnimUI();
		void onDeviceChanged();
		void onManualEntryChanged();
		// Hex-only, four digits: say so at the field instead of failing
		// with an errno after the fact.
		bool validateIdEntry(Gtk::Entry &entry, const char *what);
		void buildColorSection(Gtk::Box* colorBox);
		void buildApplySection(Gtk::Box* applyBox);
		void buildPaintSection(Gtk::Box* paintBox);
		void buildEffectsSection(Gtk::Box* effectBox);
		void buildDeviceSection(Gtk::Box* deviceBox);
		void buildWaveSection(Gtk::Box* waveBox);
		void buildRegionsSection(Gtk::Box* regionsBox);
		void buildGKeysSection(Gtk::Box* gKeysBox);
		void buildStartupSection(Gtk::Box* startupBox);
		void buildOnBoardSection(Gtk::Box* onBoardBox);
		void buildAnimSection(Gtk::Box* animBox);
		void buildAudioSection(Gtk::Box* audioBox);
		void buildScreenSection(Gtk::Box* screenBox);
		void buildTabs(Gtk::Box* rightCol, Gtk::Notebook*& tabs,
		               Gtk::Box*& colorsTab, Gtk::Box*& effectTab,
		               Gtk::Box*& liveTab, Gtk::Box*& deviceTab);
		void setRegionPending(int region, bool pending);
		void onWavePresetChanged();
		void onWaveParamsChanged();
		void onWaveGraphChanged();
		void startWave();
		void updateWaveDescription();
		void appendWaveCommands(std::vector<ProfileCommand> &commands) const;
		bool applyWaveFromCommands(const std::vector<ProfileCommand> &commands);
		void refreshAudioSources();
		void onAudioParamsChanged();
		void startAudio();
		AudioPlayer::Settings audioSettings() const;
		std::map<LedKeyboard::Key, LedKeyboard::Color> draftKeyColors() const;
		std::string selectedAudioSource() const;
		// A monitor source is the loopback of an output — "what you hear".
		// Anything else is a real capture device, i.e. a microphone, and
		// only the user may switch one on.
		bool selectedSourceIsMonitor() const;
		bool audioActive() const;
		std::string audioStatePath() const;
		std::string audioPidPath() const;
		bool audioDaemonAlive() const;
		bool writeAudioState();
		void killAudioDaemon();
		void handoffAudioDaemon();
		void applyAudioState(const AudioPlayer::State &state);
		void appendAudioCommands(std::vector<ProfileCommand> &commands) const;
		bool applyAudioFromCommands(const std::vector<ProfileCommand> &commands);
		void onScreenParamsChanged();
		void startScreen();
		std::string screenStatePath() const;
		std::string screenPidPath() const;
		bool screenDaemonAlive() const;
		bool writeScreenState();
		void killScreenDaemon();
		// Only on the way out, and only if the user says so: unlike the
		// other lanes this one reads the screen, so it is never detached
		// behind their back.
		bool askKeepScreenRunning();
		void handoffScreenDaemon();
		void applyScreenState(const ScreenPlayer::State &state);
		ScreenPlayer::Settings screenSettings() const;
		bool screenActive() const;
		void appendScreenCommands(std::vector<ProfileCommand> &commands) const;
		// Settings only: screen content is a sensor, so a profile may
		// describe the effect but never switch the capture on. announce
		// raises the notice explaining that — wanted on an explicit
		// load, noise on the automatic one at startup.
		bool applyScreenFromCommands(const std::vector<ProfileCommand> &commands,
		                             bool announce);
		void updateScreenCaption();
		// rainActive() is the umbrella "some software animation owns the
		// board"; the others name which one, so labels and messages can
		// stop calling everything a raindrop.
		bool rainActive() const;
		bool raindropActive() const;
		const char *activeAnimationName() const;
		bool blockedByAnimation(const char *action);
		// Sensitivity gates, cached so updateAnimUI cannot re-enable what
		// setControlsEnabled disabled for a missing device or feature.
		bool animControlsOk() const;
		bool audioControlsOk() const;
		bool screenControlsOk() const;
		bool pollAnimations();
		// byDaemon: a detached process owns the effect, so we cannot know
		// why it ended and must not blame a device write for it.
		void noteAnimationStarted(const char *name, bool byDaemon = false);
		bool daemonMatchesDevice(const std::string &statePath) const;
		// "Is a daemon of this kind alive, and is it driving the keyboard
		// this window has open?" costs a pid read, a /proc read and a
		// state read — and rainActive() asks it on every draft edit, so
		// the answer is cached briefly and dropped whenever we start or
		// stop a daemon ourselves.
		struct DaemonStatus {
			bool active = false;
			gint64 checkedAt = 0;
		};
		bool daemonActive(DaemonStatus &status, const std::string &pidPath,
		                  const char *flag, const std::string &statePath) const;
		void invalidateDaemonStatus();
		void clearPreviewState();
		std::string rainStatePath() const;
		std::string rainPidPath() const;
		bool rainDaemonAlive() const;
		// Shared daemon plumbing (rain and audio both re-exec us). The
		// flag identifies which daemon a pid file is supposed to hold.
		static bool daemonAlive(const std::string &pidPath, const char *flag);
		void spawnDaemon(const char *flag, const std::string &statePath,
		                 const std::string &pidPath);
		void killDaemon(const std::string &pidPath, const char *flag);
		bool writeRainState();
		void spawnRainDaemon();
		void killRainDaemon();
		void handoffRainDaemon();
		void stopRainEverywhere();
		bool onWindowKeyPress(GdkEventKey *event);

		LedKeyboard m_kbd;
		KeyboardWidget m_keyboardWidget;
		LedKeyboard::KeyboardModel m_layoutModel =
			LedKeyboard::KeyboardModel::unknown;

		// Left side
		Gtk::Box *m_leftBox = nullptr;
		Gtk::ScrolledWindow *m_keyboardScroll = nullptr;
		Gtk::Label m_regionHintLabel;
		// Keeping and fetching the board — under the board, because that
		// is what it acts on. Until this existed the only door to it was
		// the File menu, in a window that puts everything else on screen.
		Gtk::Label m_savedAsLabel;
		Gtk::Box *m_recentRow = nullptr;
		Gtk::Button m_openColorsButton;
		Gtk::Button m_saveColorsButton;

		// Persistent state strip: device, what the board is showing, and
		// how much of the draft has not been sent.
		Gtk::Label m_stateDeviceLabel;
		Gtk::Label m_stateBoardLabel;
		Gtk::Spinner m_stateSpinner;
		Gtk::InfoBar *m_errorBar = nullptr;
		Gtk::Label m_errorLabel;
		Gtk::Expander m_errorDetails;
		bool m_errorSticky = false;
		Gtk::Label m_errorDetailsLabel;
		std::string m_currentProfileName;

		// Staged apply
		Gtk::Label m_pendingLabel;
		Gtk::Button m_applyKeyboardButton;
		Gtk::Button m_revertButton;
		std::map<LedKeyboard::Key, Gdk::RGBA> m_appliedColors;
		std::map<int, Gdk::RGBA> m_regionDraft;
		std::map<int, Gdk::RGBA> m_regionApplied;
		// Whole-board draft for models without per-key support but with
		// setAllKeys (g413 intensity; also used by g213's "All keys").
		Gdk::RGBA m_allKeysDraft = Gdk::RGBA("#000000");
		Gdk::RGBA m_allKeysApplied = Gdk::RGBA("#000000");
		size_t m_pendingCount = 0;

		// Device
		Gtk::ComboBoxText m_deviceCombo;
		Gtk::Button m_rescanButton;
		Gtk::Button m_listKeyboardsButton;
		Gtk::Button m_printDeviceButton;
		Gtk::Label m_deviceLabel;
		std::vector<LedKeyboard::DeviceInfo> m_devices;
		bool m_ignoreDeviceChange = false;
		int m_regionAtPress = 0;
		Gdk::RGBA m_regionColorAtPress = Gdk::RGBA("#000000");

		// Manual device / unsupported (API parity for -dv -dp -ds -tuk)
		Gtk::CheckButton m_manualDeviceCheck;
		Gtk::Entry m_vidEntry;
		Gtk::Entry m_pidEntry;
		Gtk::Entry m_serialEntry;
		Gtk::ComboBoxText m_protocolCombo;
		bool m_manualOverrideActive = false;
		std::vector<std::vector<uint16_t>> m_originalSupported;

		// Color (rgb) / intensity
		Gtk::Grid *m_rgbColorBox = nullptr;
		Gtk::Grid *m_intensityBox = nullptr;
		ColorPickButton m_colorButton;
		Gtk::Scale m_intensityScale;

		// Paint (staging)
		Gtk::Button m_paintAllButton;
		// Groups as visible chips rather than a combo the user must open
		// to remember what is in it; hovering one previews it on the board.
		Gtk::FlowBox m_groupChips;
		std::vector<Gtk::Button*> m_groupChipButtons;
		void rebuildGroupChips();
		void previewGroup(size_t index, bool on);
		void onPaintGroupIndex(size_t index);
		Gtk::Label m_selectionLabel;
		Gtk::Button m_paintSelectionButton;
		Gtk::Button m_clearSelectionButton;
		Gtk::Button m_deselectButton;

		// Effects
		Gtk::ComboBoxText m_effectCombo;
		Gtk::ComboBoxText m_targetCombo;
		ColorPickButton m_effectColorButton;
		Gtk::SpinButton m_periodSpin;
		Gtk::CheckButton m_storeCheck;
		Gtk::Button m_applyEffectButton;
		Gtk::Button m_effectOffButton;

		// G keys (g910/g815)
		Gtk::Frame *m_gKeysFrame = nullptr;
		Gtk::ComboBoxText m_mrKeyCombo;
		Gtk::ComboBoxText m_mnKeyCombo;
		Gtk::ComboBoxText m_gKeysModeCombo;
		Gtk::Button m_applyGKeysButton;

		// Startup / on-board mode
		Gtk::Frame *m_startupFrame = nullptr;
		Gtk::ComboBoxText m_startupModeCombo;
		Gtk::Button m_applyStartupButton;
		Gtk::Frame *m_onBoardFrame = nullptr;
		Gtk::ComboBoxText m_onBoardModeCombo;
		Gtk::Button m_applyOnBoardButton;

		// Regions (g213)
		Gtk::Frame *m_regionsFrame = nullptr;
		std::vector<Gtk::EventBox*> m_regionBoxes;
		std::vector<Glib::RefPtr<Gtk::CssProvider>> m_regionStyles;
		void paintRegionBox(int region, const Gdk::RGBA &color);

		// Animation
		ColorPickButton m_animColorButton;
		Gtk::Frame *m_liveFrame = nullptr;
		Gtk::ComboBoxText m_liveModeCombo;
		Gtk::Stack m_liveStack;
		Gtk::Button m_liveToggleButton;
		bool m_liveIgnore = false;
		bool m_animOverlay = false;
		RaindropAnimation m_raindrop;

		Gtk::ComboBoxText m_waveModeCombo;
		Gtk::ComboBoxText m_waveShapeCombo;
		Gtk::Scale m_wavePeriodScale;
		Gtk::Scale m_waveMinScale;
		Gtk::Scale m_waveMaxScale;
		Gtk::Label m_waveDescLabel;
		WaveGraph m_waveGraph;
		WavePlayer m_wavePlayer;
		bool m_waveIgnore = false;

		// Sound-reactive lighting
		Gtk::ComboBoxText m_audioSourceCombo;
		Gtk::Button m_audioRefreshButton;
		Gtk::ComboBoxText m_audioModeCombo;
		ColorPickButton m_audioLowColorButton;
		ColorPickButton m_audioHighColorButton;
		Gtk::Scale m_audioGainScale;
		Gtk::Scale m_audioSmoothScale;
		Gtk::CheckButton m_audioAutoGainCheck;
		Gtk::CheckButton m_audioOverlayCheck;
		Gtk::LevelBar m_audioLevelBar;
		AudioPlayer m_audioPlayer;
		std::vector<AudioCapture::Source> m_audioSources;
		// Source resolved when the effect started — the daemon has to be
		// handed the same one, not a re-resolved default.
		std::string m_audioSourceName;
		sigc::connection m_animationTimer;

		// Screen colors
		Gtk::ComboBoxText m_screenModeCombo;
		Gtk::Scale m_screenBoostScale;
		Gtk::Scale m_screenSmoothScale;
		Gtk::CheckButton m_screenOverlayCheck;
		Gtk::Button m_screenForgetButton;
		Gtk::Label m_screenCaptionLabel;
		ScreenPlayer m_screenPlayer;
		// Last known "the portal has not answered yet", so the strip is
		// only rewritten when it changes.
		bool m_screenWaiting = false;

		Gtk::CheckButton m_view3DCheck;
		bool m_viewIgnore = false;
		bool m_viewFallbackSaid = false;

		// What the left button does on the board. Two toggles rather than
		// one, so the mode you are in is readable without clicking it.
		Gtk::RadioButton m_paintModeButton;
		Gtk::RadioButton m_selectModeButton;
		Gtk::Label m_modeHintLabel;
		bool m_modeIgnore = false;

		Gtk::Statusbar m_statusbar;
		Gtk::Statusbar* m_statusPtr = nullptr;
		guint m_statusContextId;

		std::vector<GroupEntry> m_groups;
		help::KeyboardFeatures m_features = help::KeyboardFeatures::none;
		bool m_controlsEnabled = false;
		std::vector<DraftSnapshot> m_undoStack;
		std::vector<DraftSnapshot> m_redoStack;
		// One user action is one undo step, so a profile load (which
		// stages many commands) pushes once and suppresses the rest. A
		// paint stroke does the same, and remembers that it was the one
		// that opened the batch so it only closes its own.
		// True while refreshFeatures() is rebuilding the combos a device
		// change invalidates. Emptying a combo takes its rows out one at
		// a time and the loss of the active row makes it say it changed,
		// so its handler would run against a store that is half cleared —
		// and on the way to clearing another. Every such handler stands
		// down until the rebuild has finished and can tell them the truth.
		bool m_rebuilding = false;
		bool m_undoBatch = false;
		bool m_strokeBatch = false;
		Gtk::Menu m_keyMenu;

		// The menu bar along the top. Loading, saving and the recent list
		// are file handling, which belongs in a menu rather than in a row
		// of buttons competing with Send to keyboard.
		Gtk::MenuBar m_menuBar;
		Gtk::MenuItem m_fileItem;
		Gtk::MenuItem m_helpItem;
		Gtk::Menu m_fileMenu;
		Gtk::Menu m_helpMenu;
		Gtk::MenuItem m_loadItem;
		Gtk::MenuItem m_recentItem;
		Gtk::MenuItem m_saveItem;
		Gtk::MenuItem m_saveAsItem;
		Gtk::MenuItem m_loadTextItem;
		Gtk::MenuItem m_quitItem;
		Gtk::MenuItem m_shortcutsItem;
		Gtk::Menu m_recentMenu;

		std::string m_currentProfilePath;
		// Arrows, not words: a counter-clockwise one and a clockwise one,
		// at the right end of the menu row.
		Gtk::Button m_undoButton;
		Gtk::Button m_redoButton;
		// True while the board is showing something other than the
		// applied scheme, so Apply stays reachable and writes in full.
		bool m_deviceShowsPreview = false;
		mutable DaemonStatus m_rainDaemonStatus;
		mutable DaemonStatus m_audioDaemonStatus;
		mutable DaemonStatus m_screenDaemonStatus;
		// Identity of the device this window has open, kept here so the
		// daemon check can stay const (LedKeyboard's accessors are not).
		bool m_deviceIsOpen = false;
		uint16_t m_openVendorID = 0;
		uint16_t m_openProductID = 0;
		std::string m_openSerial;
		// Name of the animation we believe is running; if it disappears
		// without the user asking, the watchdog reports why.
		std::string m_expectedAnimation;
		bool m_expectedByDaemon = false;

		// Tracked last-applied non-color state for complete profile save/emit
		bool m_hasAppliedEffect = false;
		LedKeyboard::NativeEffect m_appliedEffect = LedKeyboard::NativeEffect::off;
		LedKeyboard::NativeEffectPart m_appliedEffectPart = LedKeyboard::NativeEffectPart::all;
		std::chrono::duration<uint16_t, std::milli> m_appliedEffectPeriod =
			std::chrono::duration<uint16_t, std::milli>(0);
		LedKeyboard::Color m_appliedEffectColor = {0,0,0};
		LedKeyboard::NativeEffectStorage m_appliedEffectStorage = LedKeyboard::NativeEffectStorage::none;

		bool m_hasAppliedGKeys = false;
		uint8_t m_appliedMR = 0;
		uint8_t m_appliedMN = 0;
		uint8_t m_appliedGKeysMode = 0;

		bool m_hasAppliedStartup = false;
		LedKeyboard::StartupMode m_appliedStartup = LedKeyboard::StartupMode::wave;

		bool m_hasAppliedOnBoard = false;
		LedKeyboard::OnBoardMode m_appliedOnBoard = LedKeyboard::OnBoardMode::board;
};

#endif

#pragma once
// QML application controller: owns the app services, performs panel intents on
// the GUI thread, runs the NAVIGATE/EDIT/SHIFT state machine, drives the dirty
// fingerprint and exposes transport/metronome state to QML.
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>

#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <vector>

#include "app_context.hpp"
#include "app_state.hpp"
#include "arrange_view_model.hpp"
#include "config_popup_model.hpp"
#include "audio_glue.hpp"
#include "eq_model.hpp"
#include "eq_view_model.hpp"
#include "fx_slot.hpp"
#include "fx_view_model.hpp"
#include "learn_view_model.hpp"
#include "link_client.hpp"
#include "midi_glue.hpp"
#include "midi_monitor_view_model.hpp"
#include "mixer_model.hpp"
#include "mixer_view_model.hpp"
#include "performance_model.hpp"
#include "project_logic.hpp"
#include "projects_view_model.hpp"
#include "recorder_view_model.hpp"
#include "routing_view_model.hpp"
#include "session_settings.hpp"
#include "session_view_model.hpp"
#include "system_view_model.hpp"
#include "timeline_view_model.hpp"
#include "tools_view_model.hpp"
#include "xy_view_model.hpp"

namespace mnky::app {

class AppController final : public QObject {
  Q_OBJECT
  Q_PROPERTY(int tab READ tab WRITE setTab NOTIFY tabChanged)
  Q_PROPERTY(bool fullscreen READ fullscreen WRITE setFullscreen NOTIFY fullscreenChanged)
  Q_PROPERTY(bool dirty READ dirty NOTIFY dirtyChanged)
  // No-Pico mode: no Pico was constructed (--no-pico). QML keys the persistent
  // NO PICO badge and the unavailable TRS rows off this; it never changes.
  Q_PROPERTY(bool noPicoMode READ noPicoMode CONSTANT)
  Q_PROPERTY(bool recording READ recording NOTIFY transportChanged)
  Q_PROPERTY(bool rolling READ rolling NOTIFY transportChanged)
  // SONG mode governs playback (docs/daw-plan.md W6): on = the ARRANGE clip
  // lanes, off = the free Active-take/loop path. Persisted per project.
  Q_PROPERTY(bool songMode READ songMode WRITE setSongMode NOTIFY songModeChanged)
  Q_PROPERTY(bool overdub READ overdub NOTIFY transportChanged)
  Q_PROPERTY(bool clickEnabled READ clickEnabled WRITE setClickEnabled NOTIFY clickChanged)
  Q_PROPERTY(double clickLevel READ clickLevel WRITE setClickLevel NOTIFY clickChanged)
  Q_PROPERTY(int clickBars READ clickBars WRITE setClickBars NOTIFY clickChanged)
  Q_PROPERTY(int toolsPage READ toolsPage WRITE setToolsPage NOTIFY toolsPageChanged)
  Q_PROPERTY(int systemShiftPage READ systemShiftPage WRITE setSystemShiftPage NOTIFY systemShiftPageChanged)
  Q_PROPERTY(QString transportState READ transportState NOTIFY transportChanged)
  Q_PROPERTY(QString positionText READ positionText NOTIFY transportChanged)
  Q_PROPERTY(QString tempoText READ tempoText NOTIFY transportChanged)
  // Numeric tempo for the touch TEMPO popup slider (40..240 BPM).
  Q_PROPERTY(double tempoBpm READ tempoBpm NOTIFY transportChanged)
  Q_PROPERTY(QString noticeText READ noticeText NOTIFY noticeChanged)
  // Severity of the notice slot: "info" / "warn" / "error". Info expires
  // after 6 s, warn after 10 s, error stays until the next successful action
  // of its domain calls clearNotice().
  Q_PROPERTY(QString noticeKind READ noticeKind NOTIFY noticeChanged)
  // Metronome button label: CLICK / CLICK REC / CLICK OFF (mode-aware).
  Q_PROPERTY(QString clickLabel READ clickLabel NOTIFY clickChanged)
  Q_PROPERTY(bool tempoPopupOpen READ tempoPopupOpen NOTIFY tempoPopupChanged)
  Q_PROPERTY(bool tabGridOpen READ tabGridOpen NOTIFY tabGridChanged)
  // The six pages the four-destination nav shows behind More, in visual order.
  // Main.qml binds the sheet to the same list the ring is built from, so the
  // nav and the panel can never disagree.
  Q_PROPERTY(QVariantList morePageTabs READ morePageTabs CONSTANT)
  // A11: the TRACKS n-m / N label opens a page grid; a page is a jump, the two
  // arrows keep stepping one page.
  Q_PROPERTY(bool bankGridOpen READ bankGridOpen NOTIFY bankGridChanged)
  Q_PROPERTY(bool takeSheetOpen READ takeSheetOpen NOTIFY takeSheetChanged)
  Q_PROPERTY(int takeSheetTrack READ takeSheetTrack NOTIFY takeSheetChanged)
  // Stage 4: the recorder shows ONE selected-track button; tapping it opens a
  // picker sheet instead of parking 18 permanent selectors above the takes.
  Q_PROPERTY(bool trackPickerOpen READ trackPickerOpen NOTIFY trackPickerChanged)
  // MIDI-learn target capture: while true every learnable control is outlined
  // and the next touched one becomes the capture target (A9 arm-then-touch).
  Q_PROPERTY(bool learnArmed READ learnArmed NOTIFY learnChanged)
  Q_PROPERTY(QString panelMode READ panelMode NOTIFY panelModeChanged)
  Q_PROPERTY(QString focusId READ focusId NOTIFY focusChanged)
  Q_PROPERTY(int focusCount READ focusCount NOTIFY focusChanged)
  // RECMODE summary for the transport strip: NOW / COUNT-IN n BARS / PUNCH.
  Q_PROPERTY(QString recModeText READ recModeText NOTIFY recModeChanged)
  Q_PROPERTY(QStringList shiftActions READ shiftActions CONSTANT)
  Q_PROPERTY(double masterPeakLeft READ masterPeakLeft NOTIFY metersChanged)
  Q_PROPERTY(double masterPeakRight READ masterPeakRight NOTIFY metersChanged)
  Q_PROPERTY(bool keypadOpen READ keypadOpen NOTIFY keypadChanged)
  // The KnobChip precision sheet is QML-local (it steps the chip's own value),
  // but the controller must know it is up: EXIT has to dismiss the topmost
  // sheet, and a tab jump or an overlay close has to take it down with its owner.
  Q_PROPERTY(bool knobSheetOpen READ knobSheetOpen NOTIFY knobSheetChanged)
  Q_PROPERTY(QString keypadTitle READ keypadTitle NOTIFY keypadChanged)
  Q_PROPERTY(QString keypadInitial READ keypadInitial NOTIFY keypadChanged)
  Q_PROPERTY(QString keypadError READ keypadError NOTIFY keypadChanged)
  Q_PROPERTY(bool confirmOpen READ confirmOpen NOTIFY confirmChanged)
  Q_PROPERTY(QString confirmTitle READ confirmTitle NOTIFY confirmChanged)
  Q_PROPERTY(QString confirmMessage READ confirmMessage NOTIFY confirmChanged)
  Q_PROPERTY(QString confirmLabel READ confirmLabel NOTIFY confirmChanged)
  Q_PROPERTY(bool valuePopupVisible READ valuePopupVisible NOTIFY valuePopupChanged)
  Q_PROPERTY(QString valuePopupLabel READ valuePopupLabel NOTIFY valuePopupChanged)
  Q_PROPERTY(QString valuePopupText READ valuePopupText NOTIFY valuePopupChanged)
  // Window-Y of the touch point that owns the current gesture (a slider drag or
  // an EQ node drag), or -1 when no finger is down. The readout is pinned to the
  // top band; QML flips it below this point when the point would otherwise sit
  // under it, so a readout is never under the finger. Touch controls set and
  // clear it, the panel EDIT path never does, so it always keeps the top band.
  Q_PROPERTY(double touchAnchorY READ touchAnchorY WRITE setTouchAnchorY NOTIFY touchAnchorChanged)
  // Window coordinates of the summary button that opened the last config
  // popup. ConfigPopup scales/fades from this point (120 ms); -1 means the
  // caller had no anchor and the popup grows from its centre.
  Q_PROPERTY(double configOriginX READ configOriginX NOTIFY configOriginChanged)
  Q_PROPERTY(double configOriginY READ configOriginY NOTIFY configOriginChanged)

 public:
  explicit AppController(bool emulator, bool noPico, QObject* parent = nullptr);
  ~AppController() override;

  void start();
  void shutdown();

  int tab() const { return tab_; }
  Q_INVOKABLE void setTab(int tab);
  // EQ fullscreen: the shell hides the tab row and the transport strip so the
  // grid gets the whole 800x480 panel. Leaving the EQ tab restores the bridge.
  bool fullscreen() const { return fullscreen_; }
  Q_INVOKABLE void setFullscreen(bool fullscreen);
  bool dirty() const { return state_.dirty.load(); }
  bool noPicoMode() const { return noPico_; }
  bool recording() const { return audio_.recording(); }
  bool rolling() const { return state_.rolling.load(); }
  bool songMode() const;
  Q_INVOKABLE void setSongMode(bool on);
  Q_INVOKABLE void toggleSongMode();
  bool overdub() const { return state_.overdub.load(); }
  bool clickEnabled() const { return state_.clickEnabled.load(); }
  Q_INVOKABLE void setClickEnabled(bool enabled);
  double clickLevel() const { return state_.clickLevel.load(); }
  Q_INVOKABLE void setClickLevel(double level);
  int clickBars() const { return state_.clickBars.load(); }
  Q_INVOKABLE void setClickBars(int bars);
  int toolsPage() const { return toolsPage_; }
  Q_INVOKABLE void setToolsPage(int page);
  int systemShiftPage() const { return systemShiftPage_; }
  Q_INVOKABLE void setSystemShiftPage(int page);
  QString transportState() const { return transportState_; }
  QString positionText() const { return positionText_; }
  QString tempoText() const { return tempoText_; }
  double tempoBpm() const { return state_.tempoX100.load() / 100.0; }
  QString noticeText() const { return noticeText_; }
  QString noticeKind() const { return noticeKind_; }
  QString clickLabel() const;
  bool tempoPopupOpen() const { return tempoPopupOpen_; }
  bool tabGridOpen() const { return tabGridOpen_; }
  QVariantList morePageTabs() const { return {1, 4, 5, 6, 7, 3}; }
  bool bankGridOpen() const { return bankGridOpen_; }
  bool takeSheetOpen() const { return takeSheetOpen_; }
  int takeSheetTrack() const { return takeSheetTrack_; }
  bool trackPickerOpen() const { return trackPickerOpen_; }
  bool knobSheetOpen() const { return knobSheetOpen_; }
  bool learnArmed() const;
  QString panelMode() const { return panelMode_; }
  QString focusId() const { return focusId_; }
  int focusCount() const { return static_cast<int>(focusIds_.size()); }
  QStringList shiftActions() const { return shiftActions_; }
  double masterPeakLeft() const { return masterPeakLeft_; }
  double masterPeakRight() const { return masterPeakRight_; }
  bool keypadOpen() const { return keypadOpen_; }
  QString keypadTitle() const { return keypadTitle_; }
  QString keypadInitial() const { return keypadInitial_; }
  QString keypadError() const { return keypadError_; }
  bool confirmOpen() const { return confirmOpen_; }
  QString confirmTitle() const { return confirmTitle_; }
  QString confirmMessage() const { return confirmMessage_; }
  QString confirmLabel() const { return confirmLabel_; }
  bool valuePopupVisible() const { return valuePopupVisible_; }
  QString valuePopupLabel() const { return valuePopupLabel_; }
  QString valuePopupText() const { return valuePopupText_; }
  double touchAnchorY() const { return touchAnchorY_; }
  void setTouchAnchorY(double y);
  double configOriginX() const { return configOriginX_; }
  double configOriginY() const { return configOriginY_; }

  // Transient control-value readout. `kind` selects the formatting (pan, db,
  // percent, ratio, ms, bpm, int, generic). Called by every TouchSlider and by
  // the panel EDIT-mode adjust path; the single-shot timer hides it 1.5 s after
  // the last change.
  Q_INVOKABLE void showValuePopup(const QString& label, const QString& kind,
                                  double value);
  Q_INVOKABLE void showTextPopup(const QString& label, const QString& text);
  // The shared notice slot, callable from QML: `kind` is "info", "warn" or
  // "error" (the same severity/expiry rules the controller uses internally).
  // Screens and popups announce actions here instead of inventing local status
  // lines, so every action lands in the one slot.
  Q_INVOKABLE void announce(const QString& kind, const QString& text);
  Q_INVOKABLE void hideValuePopup();

  MixerViewModel* mixer() { return mixerVm_.get(); }
  RoutingViewModel* routing() { return routingVm_.get(); }
  TrackFxViewModel* fx() { return fxVm_.get(); }
  RecorderViewModel* recorder() { return recorderVm_.get(); }
  ProjectsViewModel* projects() { return projectsVm_.get(); }
  SystemViewModel* system() { return systemVm_.get(); }
  LearnViewModel* learn() { return learnVm_.get(); }
  SessionViewModel* session() { return sessionVm_.get(); }
  MidiMonitorViewModel* midiMonitor() { return midiMonitorVm_.get(); }
  ToolsViewModel* tools() { return toolsVm_.get(); }
  ConfigPopupModel* config() { return configVm_.get(); }
  EqViewModel* eq() { return eqVm_.get(); }
  XyViewModel* xy() { return xyVm_.get(); }
  TimelineViewModel* timeline() { return timelineVm_.get(); }
  ArrangeViewModel* arrange() { return arrangeVm_.get(); }

  // Transport / screen actions used by QML buttons.
  Q_INVOKABLE void play();
  Q_INVOKABLE void playStart();
  Q_INVOKABLE void stop();
  Q_INVOKABLE void recToggle();
  Q_INVOKABLE void toggleOverdub();
  // RECMODE (docs/daw-plan.md W2): the transport's long-press REC opens the
  // NOW / COUNT-IN n bars / PUNCH picker; `recModeText` is the transport strip's
  // one-line readout of the current choice.
  Q_INVOKABLE void openRecMode();
  QString recModeText() const;
  // TRACK MODE (docs/daw-plan.md W1): tap a strip's (or the recorder's) mode
  // pill to cycle, long-press to pick. Both land on the selected track's mode.
  Q_INVOKABLE void cycleTrackMode(int slot);
  Q_INVOKABLE void openTrackMode(int slot);
  // REC SOURCE (W2b): the chosen tap point of one track, for the status line
  // and the popups.
  Q_INVOKABLE int trackRecSource(int slot) const;
  // TAKE row actions (W4) and NEW TRACK FROM THIS TAKE (W3).
  Q_INVOKABLE void openTakeActions(int row);
  // Stage 4 recorder hierarchy: the selected-track picker sheet.
  Q_INVOKABLE void openTrackPicker();
  Q_INVOKABLE void closeTrackPicker();
  Q_INVOKABLE void chooseTrack(int track);
  // the KnobChip precision sheet reports its open state so the panel can close
  // it; the value itself stays in QML next to the chip it edits.
  Q_INVOKABLE void setKnobSheetOpen(bool open);
  Q_INVOKABLE void requestRemoveTake();
  Q_INVOKABLE void requestNewTrackFromTake(int row);
  Q_INVOKABLE void requestEditTake(int row);
  // RENDER REGION TO TRACK from the region-ops popup (W3).
  Q_INVOKABLE void renderRegionToTrack();
  // RENDER SONG (W7): master bounce into the take folder, listed as a MASTER
  // take; the notice names the file and says what the mix includes.
  Q_INVOKABLE void renderSong();
  // ARRANGE section rename through the shared keypad (W6).
  Q_INVOKABLE void requestArrangeSectionRename();
  Q_INVOKABLE void selectStrip(int index);
  Q_INVOKABLE void shiftAction(int index);
  Q_INVOKABLE void tapTempo();
  // Touch tempo popup (A7): 40..240 BPM slider, +-1 buttons and TAP.
  Q_INVOKABLE void openTempoPopup();
  // D2: an error notice stays until the next successful action of its domain
  // clears it; a screen/screenshot reset uses the same call.
  Q_INVOKABLE void clearNotice();
  Q_INVOKABLE void closeTempoPopup();
  Q_INVOKABLE void setTempoBpm(int bpm);
  // Tab grid (C3): the ring's single TAB stop expands a 2x5 grid of the ten
  // tabs; picking one navigates and closes it.
  Q_INVOKABLE void openTabGrid();
  Q_INVOKABLE void closeTabGrid();
  Q_INVOKABLE void openBankGrid();
  Q_INVOKABLE void closeBankGrid();
  Q_INVOKABLE void chooseBankPage(int page);
  // D3: only tabs 0,1,2,3,4,5,7 have a SHIFT face.
  Q_INVOKABLE bool tabHasShiftFace(int tab) const;

  // Keypad / confirmation overlays.
  Q_INVOKABLE void requestKeypad(const QString& title, const QString& initial,
                                 const QString& action, int index);
  Q_INVOKABLE void keypadAccept(const QString& text);
  Q_INVOKABLE void keypadCancel();
  Q_INVOKABLE void requestConfirm(const QString& title, const QString& message,
                                  const QString& confirmLabel, const QString& action,
                                  int index);
  Q_INVOKABLE void confirmAccept();
  Q_INVOKABLE void confirmCancel();
  Q_INVOKABLE void requestLoadProject(int row);
  // A3: the 44x44 "..." target selects without loading; the row body loads.
  Q_INVOKABLE void selectProjectRow(int row);
  Q_INVOKABLE void selectTakeRow(int row);
  Q_INVOKABLE void activateTakeRow(int row);
  Q_INVOKABLE void requestDeleteProject(int row);
  Q_INVOKABLE void requestRenameProject();
  Q_INVOKABLE void requestRenameTake();
  Q_INVOKABLE void requestDeleteTake();
  Q_INVOKABLE void requestImportTake();
  // Pipe-chain preset files on the Routing SHIFT > PIPES page. Both reuse the
  // take path entry: EXPORT asks for a name (or path), IMPORT for an existing
  // file. The project folder is the default location and the .json extension is
  // implied; a validation failure keeps the keypad open with the reason.
  Q_INVOKABLE void requestExportPipeChain();
  Q_INVOKABLE void requestImportPipeChain();

  // Dynamic mixer. ADD TRACK opens the config popup pre-filled with the first
  // free slot's name, input and output; the strip appears in track order the
  // moment OK adds it. REMOVE TRACK asks for confirmation and leaves the slot's
  // configuration, name and takes in the project, so a re-add restores them.
  Q_INVOKABLE void requestAddTrack();
  Q_INVOKABLE void requestRemoveTrack(int slot);
  // A4: rename a live track through the shared keypad. The strip name's 600 ms
  // long-press and the channel popout's RENAME button both land here.
  Q_INVOKABLE void requestRenameTrack(int slot);
  // A10: the recorder header -> "TAKE FOR TRACK nn" sheet; choosing a row makes
  // it ACTIVE and repaints the strip/waveforms.
  Q_INVOKABLE void openTakeSheet(int track);
  Q_INVOKABLE void closeTakeSheet();
  Q_INVOKABLE void chooseTakeRow(int row);
  Q_INVOKABLE void chooseLiveInput();
  // W5: the take edit popup. Every EDIT affordance (recorder take row, the
  // row's "..." menu, the take sheet) opens it for one take; opening makes that
  // take ACTIVE because region edits are applied to the track's active take.
  Q_INVOKABLE void openTakeEdit(int row);
  Q_INVOKABLE void closeTakeEdit();
  // A9: arm a capture without a target, then pick the target by touching it.
  Q_INVOKABLE void armLearnTargetCapture();
  Q_INVOKABLE void learnTargetTouched(const QString& focusId);
  Q_INVOKABLE bool isLearnableFocus(const QString& focusId) const;
  // The add-track popup's NAME button opens the shared keypad; accepting writes
  // the name back into the live popup draft.
  Q_INVOKABLE void requestAddTrackName();
  // Selector popups: every cycle-style selector opens the option list instead
  // of stepping a value, and a pick applies immediately.
  Q_INVOKABLE void openInputSource(int slot);
  Q_INVOKABLE void openOutputDest(int slot);
  // Per-track FX rack popup (TRACK FX) and its two popups: the slot selector
  // and the slot's delay/reverb parameter editor. `slot` is 0 = FX1, 1 = FX2.
  Q_INVOKABLE void openTrackFx(int slot);
  Q_INVOKABLE void closeTrackFx();
  Q_INVOKABLE void openFxSlot(int slot);
  Q_INVOKABLE void openFxSlotParams(int slot);
  // Full channel popout opened from a strip's expand affordance.
  Q_INVOKABLE void openChannel(int slot);
  Q_INVOKABLE void closeChannel();

  // B5: the NO PICO badge opens this explanation with an OPEN SYSTEM action.
  Q_INVOKABLE void showNoPicoInfo();
  // Screenshot/tap runner helpers: seed a deterministic arrangement and the
  // punch auto-place notice. Not called by the UI.
  void demoSeedArrange();
  void demoPunchAutoPlace();
  // Screenshot runner helper: force an overlay state for a deterministic grab.
  Q_INVOKABLE void openDemoKeypad();
  // Screenshot runner helper: force the SHIFT face (the panel reaches it with a
  // 500 ms EXIT long-hold, which an offscreen grab cannot perform).
  Q_INVOKABLE void showShiftFace(bool on);
  // Screenshot runner helper: inject one TRS input message through MidiGlue, so
  // a piped TRS route's outgoing send is visible on the Monitor screen without
  // a MIDI source. port 0/1 = TRS IN1/IN2; the message is always 3 bytes.
  void injectDemoTrsMidi(int port, int status, int data1, int data2);
  Q_INVOKABLE void openConfig(const QString& kind);
  // Open a config popup from a summary button: `item` is the button (a
  // QQuickItem) and its centre becomes the popup's scale origin.
  Q_INVOKABLE void openConfigFrom(const QString& kind, QObject* item);
  // Open the choice list of one bool/enum pipe parameter. The card supplies its
  // pipe slot and positional parameter; `item` is the card (scale origin).
  Q_INVOKABLE void openPipeParam(int pipe, int param, QObject* item);
  Q_INVOKABLE void configAccept();
  Q_INVOKABLE void configCancel();

 signals:
  void tabChanged();
  void fullscreenChanged();
  void dirtyChanged();
  void transportChanged();
  void songModeChanged();
  void clickChanged();
  void toolsPageChanged();
  void systemShiftPageChanged();
  void noticeChanged();
  void tempoPopupChanged();
  void tabGridChanged();
  void bankGridChanged();
  void takeSheetChanged();
  void learnChanged();
  // The keypad buffer and cursor live in QML; the panel path asks it to move
  // the cursor or accept through these two signals (A2 modal trap).
  void keypadCursorMoveRequested(int delta);
  void keypadAcceptRequested();
  // G7: the panel reaches the keypad's keys as ring stops; a PRESS asks the
  // QML buffer to insert a character or run an edit verb (buffer stays in QML).
  void keypadTextRequested(const QString& text);
  void keypadEditRequested(const QString& action);
  void knobSheetChanged();
  // The owning overlay closed (or the user pressed EXIT): the chip hides its
  // precision sheet. Without this a modal sheet outlived the popout it was
  // opened from and swallowed every later touch.
  void knobSheetCloseRequested();
  void trackPickerChanged();
  void panelModeChanged();
  void focusChanged();
  void recModeChanged();
  void metersChanged();
  void keypadChanged();
  void confirmChanged();
  void valuePopupChanged();
  void touchAnchorChanged();
  void configOriginChanged();

 private:
  // The headless tap runner drives the same panel intents (focus PRESS, EXIT)
  // the Pico sends, so these private handlers can be regression-gated without
  // exposing a test-only API to QML.
  friend class UiTapRunner;
  void timerTick();
  // G9: every overlay saves the invoking focus id on open and restores it on
  // close (by id when it still exists, else by the saved index).
  void noteFocusReturn();
  void restoreFocusReturn();
  void dismissKnobSheet();
  void perform(const Intent& intent);
  void performPanelFocus(const Intent& intent);
  // EDIT-mode directions on the EQ node / X/Y grid. Returns true when handled.
  bool performGridEdit(const Intent& intent);
  void moveFocus(int delta);
  void activateFocus();
  void adjustFocused(int detents, bool fine);
  bool isEditableFocus(const QString& id) const;
  void rebuildFocusIds();
  void openConfigInternal(const QString& kind);
  void setPanelMode(const QString& mode);
  void updateShiftState(const LinkStatus& status);
  void updateTransport();
  void updateDirtyState();
  void updatePanelLeds();
  void expirePendingRec();
  // COUNT-IN / PUNCH scheduling: armed by toggleTake(), consumed once per tick
  // by timerTick() at the measure boundary the schedule names.
  void armRecSchedule();
  void pollRecSchedule(const LinkStatus& status);
  void cancelRecSchedule();
  // Track mode changes: auto-PLAY after a take, the notice, and the strip
  // repaint.
  void applyTakeModeAfterStop(MixerModel::TrackMask recorded);
  void createTrackFromTake(int row, const QString& name, bool move);
  void takeAction(int action);
  void toggleTake();
  // `keepRolling` is the PUNCH OUT rule: the take closes but the transport
  // keeps playing past the out point.
  void stopTake(bool keepRolling = false);
  void startTakeNow();
  void projectStateChanged();
  void openArrangeClipEdit(int track, const QString& takeKey);
  void placePunchTakeClips(MixerModel::TrackMask recorded);
  std::uint32_t displayTick(const LinkStatus& status) const;
  double displayBeats(const LinkStatus& status) const;
  int songPunchBar(int loopRelativeMeasure) const;
  void registerLearnTargets();
  enum NoticeKind { NoticeInfo, NoticeWarn, NoticeError };
  void showNotice(NoticeKind kind, const QString& notice);
  void setTakeSheetTrack(int track);
  void popupGain(const QString& label, int strip);
  void popupPan(const QString& label, int strip);
  void popupFxWet(const QString& label, int strip, int slot);
  // One parameter card of a rack slot (the popup's knobs and the panel encoder
  // read the same {index,name,value,minimum,maximum,stepped} row).
  QVariantMap paramCard(int track, int slot, int param) const;

  AppState state_;
  SessionSettings settings_;
  MixerModel mixer_;
  FxManager fx_;
  // Declared before AudioGlue: the send chains are wired into the engine by the
  // audio glue, and the EQ instances are the screen's parameter store.
  PerformanceModel performance_;
  EqModel eqModel_;
  MidiGlue midi_;
  AudioGlue audio_;
  LinkClient link_;
  std::unique_ptr<AppContext> context_;

  std::unique_ptr<MixerViewModel> mixerVm_;
  std::unique_ptr<RoutingViewModel> routingVm_;
  std::unique_ptr<TrackFxViewModel> fxVm_;
  std::unique_ptr<RecorderViewModel> recorderVm_;
  std::unique_ptr<ProjectsViewModel> projectsVm_;
  std::unique_ptr<SystemViewModel> systemVm_;
  std::unique_ptr<LearnViewModel> learnVm_;
  std::unique_ptr<SessionViewModel> sessionVm_;
  std::unique_ptr<MidiMonitorViewModel> midiMonitorVm_;
  std::unique_ptr<ToolsViewModel> toolsVm_;
  std::unique_ptr<ConfigPopupModel> configVm_;
  std::unique_ptr<EqViewModel> eqVm_;
  std::unique_ptr<XyViewModel> xyVm_;
  std::unique_ptr<TimelineViewModel> timelineVm_;
  std::unique_ptr<ArrangeViewModel> arrangeVm_;

  QTimer timer_;
  int tab_ = 0;
  bool fullscreen_ = false;
  int toolsPage_ = 0;
  int systemShiftPage_ = 0;
  int refreshTicks_ = 0;
  int focusByScreen_[kScreenCount] = {};
  std::vector<QString> focusIds_;
  int focusIndex_ = 0;
  QString focusId_;
  QString panelMode_ = QStringLiteral("navigate");
  bool shiftFace_ = false;
  QStringList shiftActions_;

  DirtyTracker dirtyTracker_;
  ChordDetector chord_;
  // COUNT-IN / PUNCH: what the schedule waits for. None = nothing pending.
  enum class RecSchedule { None, CountIn, PunchIn };
  RecSchedule recSchedule_ = RecSchedule::None;
  int recPunchOutBar_ = 0;
  // True once the schedule's transport START has been observed rolling; a stop
  // after that cancels the schedule, a stop before it is the queued-start gap.
  bool recScheduleSawRolling_ = false;
  // The RECMODE value the transport strip last showed (change detection).
  int lastRecMode_ = 0;
  int lastCountInBars_ = 2;
  // PUNCH INTO SONG: the take opened by the punch schedule auto-places at the
  // punch in point once it stops. `punchPlaceStartBeats_` is the absolute song
  // position; `punchPlaceArmed_` survives until the take is adopted.
  bool punchPlaceArmed_ = false;
  double punchPlaceStartBeats_ = 0.0;
  bool punchEngaged_ = false;
  // Song transport seek rebasing: the raw Pico tick resets on START, so the
  // displayed position is remembered across the transition. A non-negative
  // `transportZeroAnchorTick_` is the STOP-STOP return-to-zero anchor: the
  // offset equals -anchor and is re-pinned while the transport stays stopped.
  bool lastRolling_ = false;
  std::uint32_t lastLinkTick_ = 0;
  std::int64_t transportZeroAnchorTick_ = -1;
  // The name the NEW TRACK FROM TAKE keypad accepted, held until the MOVE /
  // KEEP ON SOURCE pick creates the track.
  QString pendingTrackFromTakeName_;
  // The file RENDER REGION TO TRACK baked, waiting for the new track's name.
  std::filesystem::path pendingRenderPath_;
  bool pendingRec_ = false;
  std::uint64_t pendingRecMs_ = 0;
  MixerModel::TrackMask takeArmMask_ = 0;

  std::uint8_t lastLedMask_ = 0;
  std::uint8_t lastLedStates_ = 0;

  QString transportState_ = QStringLiteral("STOP");
  QString positionText_ = QStringLiteral("001:1:00 / 00:00");
  QString tempoText_ = QStringLiteral("120.00 BPM");
  QString noticeText_;
  QString noticeKind_ = QStringLiteral("info");
  QTimer noticeTimer_;
  bool tempoPopupOpen_ = false;
  bool tabGridOpen_ = false;
  bool bankGridOpen_ = false;
  // D7: the import preview asked for confirmation; this is the path it will add
  // when the confirmation accepts.
  QString pendingImportPath_;
  bool takeSheetOpen_ = false;
  int takeSheetTrack_ = -1;
  bool trackPickerOpen_ = false;
  bool knobSheetOpen_ = false;
  // G9 focus-return stack: nested overlays (channel -> TRACK FX -> config /
  // keypad) each restore the stop they were opened from.
  std::vector<QString> focusReturnStack_;
  bool suppressFocusReturn_ = false;
  // Last non-Off metronome mode: enabling CLICK from Off restores it instead
  // of silently rewriting the configured mode (A8).
  int lastNonZeroClickMode_ = 0;
  // Engine take loops are mapped with the transport tempo at apply time, so a
  // tempo change re-applies them (Engine::setTakeLoop contract).
  int loopTempoX100_ = 0;
  bool lastClickEnabled_ = false;
  double lastClickLevel_ = 0.5;
  int lastClickBars_ = 1;
  int lastClickMode_ = 0;
  int lastClickOutput_ = 0;
  double lastClickGainDb_ = -6.0;
  double masterPeakLeft_ = 0.0;
  double masterPeakRight_ = 0.0;

  bool shiftHeld_ = false;
  std::uint64_t shiftPressMs_ = 0;
  std::deque<std::uint64_t> tapTimes_;

  bool keypadOpen_ = false;
  QString keypadTitle_;
  QString keypadInitial_;
  QString keypadError_;
  QString keypadAction_;
  int keypadIndex_ = -1;

  bool confirmOpen_ = false;
  QString confirmTitle_;
  QString confirmMessage_;
  QString confirmLabel_;
  QString confirmAction_;
  int confirmIndex_ = -1;

  QTimer valuePopupTimer_;
  bool valuePopupVisible_ = false;
  QString valuePopupLabel_;
  QString valuePopupText_;
  double touchAnchorY_ = -1.0;
  double configOriginX_ = -1.0;
  double configOriginY_ = -1.0;

  bool started_ = false;
  // --no-pico: LinkClient has no SPI transport or PicoLink and the Pi owns the
  // clock/transport. Fixed for the process lifetime.
  bool noPico_ = false;
};

}  // namespace mnky::app

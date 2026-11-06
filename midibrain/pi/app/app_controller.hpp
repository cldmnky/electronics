#pragma once
// QML application controller: owns the app services, performs panel intents on
// the GUI thread, runs the NAVIGATE/EDIT/SHIFT state machine, drives the dirty
// fingerprint and exposes transport/metronome state to QML.
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <cstdint>
#include <deque>
#include <memory>
#include <vector>

#include "app_context.hpp"
#include "app_state.hpp"
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
  Q_PROPERTY(bool overdub READ overdub NOTIFY transportChanged)
  Q_PROPERTY(bool clickEnabled READ clickEnabled WRITE setClickEnabled NOTIFY clickChanged)
  Q_PROPERTY(double clickLevel READ clickLevel WRITE setClickLevel NOTIFY clickChanged)
  Q_PROPERTY(int clickBars READ clickBars WRITE setClickBars NOTIFY clickChanged)
  Q_PROPERTY(int toolsPage READ toolsPage WRITE setToolsPage NOTIFY toolsPageChanged)
  Q_PROPERTY(int systemShiftPage READ systemShiftPage WRITE setSystemShiftPage NOTIFY systemShiftPageChanged)
  Q_PROPERTY(QString transportState READ transportState NOTIFY transportChanged)
  Q_PROPERTY(QString positionText READ positionText NOTIFY transportChanged)
  Q_PROPERTY(QString tempoText READ tempoText NOTIFY transportChanged)
  Q_PROPERTY(QString noticeText READ noticeText NOTIFY noticeChanged)
  Q_PROPERTY(QString panelMode READ panelMode NOTIFY panelModeChanged)
  Q_PROPERTY(QString focusId READ focusId NOTIFY focusChanged)
  Q_PROPERTY(int focusCount READ focusCount NOTIFY focusChanged)
  Q_PROPERTY(QStringList shiftActions READ shiftActions CONSTANT)
  Q_PROPERTY(double masterPeakLeft READ masterPeakLeft NOTIFY metersChanged)
  Q_PROPERTY(double masterPeakRight READ masterPeakRight NOTIFY metersChanged)
  Q_PROPERTY(bool keypadOpen READ keypadOpen NOTIFY keypadChanged)
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
  QString noticeText() const { return noticeText_; }
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
  Q_INVOKABLE void hideValuePopup();

  MixerViewModel* mixer() { return mixerVm_.get(); }
  RoutingViewModel* routing() { return routingVm_.get(); }
  FxViewModel* fx() { return fxVm_.get(); }
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

  // Transport / screen actions used by QML buttons.
  Q_INVOKABLE void play();
  Q_INVOKABLE void playStart();
  Q_INVOKABLE void stop();
  Q_INVOKABLE void recToggle();
  Q_INVOKABLE void toggleOverdub();
  Q_INVOKABLE void selectStrip(int index);
  Q_INVOKABLE void shiftAction(int index);
  Q_INVOKABLE void tapTempo();

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
  // The add-track popup's NAME button opens the shared keypad; accepting writes
  // the name back into the live popup draft.
  Q_INVOKABLE void requestAddTrackName();
  // Selector popups: every cycle-style selector opens the option list instead
  // of stepping a value, and a pick applies immediately.
  Q_INVOKABLE void openInputSource(int slot);
  Q_INVOKABLE void openOutputDest(int slot);
  Q_INVOKABLE void openInsert(int slot);
  // Full channel popout opened from a strip's expand affordance.
  Q_INVOKABLE void openChannel(int slot);
  Q_INVOKABLE void closeChannel();

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
  void clickChanged();
  void toolsPageChanged();
  void systemShiftPageChanged();
  void noticeChanged();
  void panelModeChanged();
  void focusChanged();
  void metersChanged();
  void keypadChanged();
  void confirmChanged();
  void valuePopupChanged();
  void touchAnchorChanged();
  void configOriginChanged();

 private:
  void timerTick();
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
  void toggleTake();
  void stopTake();
  void startTakeNow();
  void projectStateChanged();
  void registerLearnTargets();
  void setNotice(const QString& notice);
  void popupGain(const QString& label, int strip);
  void popupPan(const QString& label, int strip);
  void popupSend(const QString& label, int strip, int send);
  void popupTrackSend(const QString& label, int send);

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
  std::unique_ptr<FxViewModel> fxVm_;
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
  bool pendingRec_ = false;
  std::uint64_t pendingRecMs_ = 0;
  MixerModel::TrackMask takeArmMask_ = 0;

  std::uint8_t lastLedMask_ = 0;
  std::uint8_t lastLedStates_ = 0;

  QString transportState_ = QStringLiteral("STOP");
  QString positionText_ = QStringLiteral("001:1:00 / 00:00");
  QString tempoText_ = QStringLiteral("120.00 BPM");
  QString noticeText_;
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

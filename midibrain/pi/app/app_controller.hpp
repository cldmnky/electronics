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
#include "audio_glue.hpp"
#include "fx_slot.hpp"
#include "fx_view_model.hpp"
#include "learn_view_model.hpp"
#include "link_client.hpp"
#include "midi_glue.hpp"
#include "mixer_model.hpp"
#include "mixer_view_model.hpp"
#include "project_logic.hpp"
#include "projects_view_model.hpp"
#include "recorder_view_model.hpp"
#include "routing_view_model.hpp"
#include "system_view_model.hpp"

namespace mnky::app {

class AppController final : public QObject {
  Q_OBJECT
  Q_PROPERTY(int tab READ tab WRITE setTab NOTIFY tabChanged)
  Q_PROPERTY(bool dirty READ dirty NOTIFY dirtyChanged)
  Q_PROPERTY(bool recording READ recording NOTIFY transportChanged)
  Q_PROPERTY(bool rolling READ rolling NOTIFY transportChanged)
  Q_PROPERTY(bool overdub READ overdub NOTIFY transportChanged)
  Q_PROPERTY(bool clickEnabled READ clickEnabled WRITE setClickEnabled NOTIFY clickChanged)
  Q_PROPERTY(double clickLevel READ clickLevel WRITE setClickLevel NOTIFY clickChanged)
  Q_PROPERTY(int clickBars READ clickBars WRITE setClickBars NOTIFY clickChanged)
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

 public:
  explicit AppController(bool emulator, QObject* parent = nullptr);
  ~AppController() override;

  void start();
  void shutdown();

  int tab() const { return tab_; }
  void setTab(int tab);
  bool dirty() const { return state_.dirty.load(); }
  bool recording() const { return audio_.recording(); }
  bool rolling() const { return state_.rolling.load(); }
  bool overdub() const { return state_.overdub.load(); }
  bool clickEnabled() const { return state_.clickEnabled.load(); }
  Q_INVOKABLE void setClickEnabled(bool enabled);
  double clickLevel() const { return state_.clickLevel.load(); }
  Q_INVOKABLE void setClickLevel(double level);
  int clickBars() const { return state_.clickBars.load(); }
  Q_INVOKABLE void setClickBars(int bars);
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

  MixerViewModel* mixer() { return mixerVm_.get(); }
  RoutingViewModel* routing() { return routingVm_.get(); }
  FxViewModel* fx() { return fxVm_.get(); }
  RecorderViewModel* recorder() { return recorderVm_.get(); }
  ProjectsViewModel* projects() { return projectsVm_.get(); }
  SystemViewModel* system() { return systemVm_.get(); }
  LearnViewModel* learn() { return learnVm_.get(); }

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

  // Screenshot runner helper: force an overlay state for a deterministic grab.
  Q_INVOKABLE void openDemoKeypad();

 signals:
  void tabChanged();
  void dirtyChanged();
  void transportChanged();
  void clickChanged();
  void noticeChanged();
  void panelModeChanged();
  void focusChanged();
  void metersChanged();
  void keypadChanged();
  void confirmChanged();

 private:
  void timerTick();
  void perform(const Intent& intent);
  void performPanelFocus(const Intent& intent);
  void moveFocus(int delta);
  void activateFocus();
  void adjustFocused(int detents, bool fine);
  bool isEditableFocus(const QString& id) const;
  void rebuildFocusIds();
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

  AppState state_;
  MixerModel mixer_;
  FxManager fx_;
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

  QTimer timer_;
  int tab_ = 0;
  int refreshTicks_ = 0;
  int focusByScreen_[kScreenCount] = {};
  std::vector<QString> focusIds_;
  int focusIndex_ = 0;
  QString focusId_;
  QString panelMode_ = QStringLiteral("navigate");
  QStringList shiftActions_;

  DirtyTracker dirtyTracker_;
  ChordDetector chord_;
  bool pendingRec_ = false;
  std::uint64_t pendingRecMs_ = 0;

  std::uint8_t lastLedMask_ = 0;
  std::uint8_t lastLedStates_ = 0;

  QString transportState_ = QStringLiteral("STOP");
  QString positionText_ = QStringLiteral("001:1:00 / 00:00");
  QString tempoText_ = QStringLiteral("120.00 BPM");
  QString noticeText_;
  bool lastClickEnabled_ = false;
  double lastClickLevel_ = 0.5;
  int lastClickBars_ = 1;
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

  bool started_ = false;
};

}  // namespace mnky::app

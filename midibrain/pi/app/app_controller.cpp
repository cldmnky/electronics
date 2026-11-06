#include "app_controller.hpp"

#include <QFileInfo>
#include <QQuickItem>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <string>

#include "logging.hpp"
#include "pipe_ui.hpp"
#include "project_io.hpp"
#include "routing_model.hpp"

namespace mnky::app {
namespace {
// Tab order after the FX tab retired into the per-track rack popup and EDIT
// retired into the take edit popup: 0 Mixer, 1 Routing, 2 Recorder, 3 System,
// 4 Projects, 5 Monitor, 6 EQ, 7 X/Y (logic.hpp kScreenCount matches).
constexpr int kProjectsTabIndex = 4;
constexpr int kMonitorTabIndex = 5;
constexpr int kEqTabIndex = 6;
constexpr int kXyTabIndex = 7;
constexpr int kArrangeTabIndex = 8;

std::uint64_t nowMs() {
  using namespace std::chrono;
  return static_cast<std::uint64_t>(
      duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

std::string learnTarget(int strip, const char* parameter) {
  char buffer[40] = {};
  std::snprintf(buffer, sizeof buffer, "mixer.ch%02d.%s", strip + 1, parameter);
  return buffer;
}
}  // namespace

AppController::AppController(bool emulator, bool noPico, QObject* parent)
    : QObject(parent),
      midi_(emulator),
      audio_(mixer_, fx_, performance_, state_, emulator),
      link_(midi_, state_, emulator, noPico) {
  noPico_ = noPico;
  context_ = std::make_unique<AppContext>(AppContext{
      state_, mixer_, fx_, midi_, audio_, link_, settings_, eqModel_, performance_});

  mixerVm_ = std::make_unique<MixerViewModel>(mixer_, settings_, eqModel_);
  mixerVm_->setClapHost(&fx_);
  routingVm_ = std::make_unique<RoutingViewModel>(midi_, noPico_);
  fxVm_ = std::make_unique<TrackFxViewModel>(mixer_, *mixerVm_);
  recorderVm_ =
      std::make_unique<RecorderViewModel>(state_, audio_, mixer_, link_, settings_);
  timelineVm_ = std::make_unique<TimelineViewModel>(state_, audio_, *recorderVm_,
                                                    link_, settings_);
  arrangeVm_ = std::make_unique<ArrangeViewModel>(state_, audio_, mixer_,
                                                  *recorderVm_, settings_);
  projectsVm_ = std::make_unique<ProjectsViewModel>(
      *context_, [this] { projectStateChanged(); });
  systemVm_ = std::make_unique<SystemViewModel>(link_, audio_, midi_, fx_, state_,
                                                settings_);
  learnVm_ = std::make_unique<LearnViewModel>(midi_);
  sessionVm_ = std::make_unique<SessionViewModel>(state_, mixer_, link_, settings_);
  midiMonitorVm_ = std::make_unique<MidiMonitorViewModel>(midi_, noPico_);
  toolsVm_ = std::make_unique<ToolsViewModel>(state_, link_);
  configVm_ =
      std::make_unique<ConfigPopupModel>(state_, mixer_, settings_, eqModel_);
  configVm_->setTimeline(timelineVm_.get());
  configVm_->setRouting(routingVm_.get());
  configVm_->setMixer(mixerVm_.get());
  // The TAKE "..." menu and NEW TRACK FROM TAKE both need keypad/confirm flows
  // and notices, which belong to the controller, so the popup reports the pick.
  configVm_->setTakeActionSink([this](int action) {
    configCancel();
    takeAction(action);
  });
  configVm_->setTrackFromTakeSink([this](int choice) {
    const int row = configVm_->trackFromTakeRow();
    configCancel();
    // The keypad already accepted the name; it is held in the keypad action's
    // last accepted text.
    createTrackFromTake(row, pendingTrackFromTakeName_, /*move=*/choice == 0);
    pendingTrackFromTakeName_.clear();
  });
  eqVm_ = std::make_unique<EqViewModel>(eqModel_, mixer_);
  xyVm_ = std::make_unique<XyViewModel>(mixer_, *mixerVm_);
  // Every EQ/XY screen change shows one transient readout, the same single
  // popup instance the sliders and the panel EDIT path drive.
  connect(eqVm_.get(), &EqViewModel::valuePopupRequested, this,
          [this](const QString& label, const QString& kind, double value) {
            showValuePopup(label, kind, value);
          });
  connect(eqVm_.get(), &EqViewModel::textPopupRequested, this,
          [this](const QString& label, const QString& text) {
            showTextPopup(label, text);
          });
  // Live EQ forwarding: every grid/panel edit reaches the engine's per-strip
  // and main inserts, not just the session. applyEq writes all 18x4 + 4 bands,
  // so an all-None state is an explicit bypass.
  connect(eqVm_.get(), &EqViewModel::changed, this,
          [this] { audio_.applyEq(eqModel_.capture()); });
  connect(xyVm_.get(), &XyViewModel::textPopupRequested, this,
          [this](const QString& label, const QString& text) {
            showTextPopup(label, text);
          });
  connect(recorderVm_.get(), &RecorderViewModel::activeTakeChanged, mixerVm_.get(),
          &MixerViewModel::notifyActiveTakesChanged);
  // The take edit popup is a modal: its own focus ring replaces the tab's the
  // moment it opens or closes, so NAV can never walk behind it.
  connect(timelineVm_.get(), &TimelineViewModel::editOpenChanged, this,
          [this] { rebuildFocusIds(); });
  // ARRANGE: the view model reports user-facing outcomes; the controller owns
  // notices, the keypad/confirm modals and the take edit popup.
  connect(arrangeVm_.get(), &ArrangeViewModel::noticeRequested, this,
          [this](const QString& kind, const QString& text) {
            if (kind == QStringLiteral("warn")) {
              showNotice(NoticeWarn, text);
            } else if (kind == QStringLiteral("error")) {
              showNotice(NoticeError, text);
            } else {
              showNotice(NoticeInfo, text);
            }
          });
  connect(arrangeVm_.get(), &ArrangeViewModel::editTakeRequested, this,
          [this](int track, const QString& takeKey) {
            openArrangeClipEdit(track, takeKey);
          });
  connect(arrangeVm_.get(), &ArrangeViewModel::deleteClipRequested, this,
          [this](int index) {
            const QString name = arrangeVm_->clipNameAt(index);
            requestConfirm(QStringLiteral("DELETE CLIP ") + name,
                           QStringLiteral("Remove the clip from the song? The take "
                                          "file stays on disk."),
                           QStringLiteral("DELETE"),
                           QStringLiteral("arrange-clip-delete"), index);
          });
  connect(arrangeVm_.get(), &ArrangeViewModel::songModeChanged, this, [this] {
    emit songModeChanged();
    updateTransport();
  });
  connect(arrangeVm_.get(), &ArrangeViewModel::clipMenuChanged, this,
          [this] { rebuildFocusIds(); });
  connect(arrangeVm_.get(), &ArrangeViewModel::lengthPopupChanged, this,
          [this] { rebuildFocusIds(); });
  // Live device pickers: the audio layer falls back to a working backend when a
  // requested device cannot open. The screen keeps the fallback note and this
  // popup says what happened, so a pick never fails silently.
  connect(systemVm_.get(), &SystemViewModel::applyNotice, this,
          [this](const QString& note) {
            showNotice(NoticeWarn, QStringLiteral("audio: ") + note);
            showTextPopup(QStringLiteral("AUDIO DEVICE"), note);
          });

  shiftActions_ = {QStringLiteral("Save"), QStringLiteral("Panic"), QStringLiteral("Tap"),
                   QStringLiteral("Click")};
  // TRS->TRS pipe chains are mirrored to the Pico as 0x18/0x19 commands; the
  // link thread applies them before it pumps routed messages, so a chain edit
  // is live before the next 0x1A send. Pairs touching USB never take this path.
  midi_.setPipeSync([this](midi::Port source, midi::Port destination,
                           const std::vector<session::MidiPipe>& pipes,
                           bool enabled) {
    link_.requestMidiRoutePipes(source, destination, pipes, enabled);
  });
  registerLearnTargets();

  timer_.setInterval(66);
  connect(&timer_, &QTimer::timeout, this, &AppController::timerTick);
  valuePopupTimer_.setSingleShot(true);
  valuePopupTimer_.setInterval(1500);
  connect(&valuePopupTimer_, &QTimer::timeout, this,
          [this] { hideValuePopup(); });
  noticeTimer_.setSingleShot(true);
  connect(&noticeTimer_, &QTimer::timeout, this, [this] { clearNotice(); });
  // MIDI-learn arm-then-touch: the target outline lives on the controls, so a
  // target change has to repaint every control that binds app.learnArmed.
  connect(learnVm_.get(), &LearnViewModel::targetArmedChanged, this,
          [this] { emit learnChanged(); });
  connect(learnVm_.get(), &LearnViewModel::captureChanged, this,
          [this] { emit learnChanged(); });
  rebuildFocusIds();
}

AppController::~AppController() {
  shutdown();
}

void AppController::start() {
  if (started_) return;
  audio_.start();
  midi_.open();
  // MIDI-hub default: the one documented matrix in routing_model.hpp (also what
  // the Routing screen's RESET restores) sends both TRS inputs to USB and turns
  // every other pair off. A default-on USB input would echo our own virtual port
  // straight back out.
  for (int source = 0; source < routing::kInputCount; ++source) {
    for (int destination = 0; destination < routing::kOutputCount; ++destination) {
      if (!routing::kDefaultRoutes[source][destination]) continue;
      midi_.setRoute(routing::kInputs[source], routing::kOutputs[destination], true);
    }
  }
  routingVm_->refresh();

  link_.start();
  audio_.applyMonitorSettings(settings_);
  audio_.applyTakeLoops(settings_);
  audio_.applyEq(eqModel_.capture());
  timelineVm_->reloadFromSettings();
  arrangeVm_->reloadFromSettings();
  sessionVm_->pushClockToLink();

  state_.clearDirty();
  dirtyTracker_.clear(project::fingerprint(*context_));
  started_ = true;
  timer_.start();
  log(std::string("app: started with ") + (audio_.emulated() ? "emulator" : "device") +
      " backends");
  if (noPico_) {
    log("app: NO PICO: audio, USB MIDI, recorder, takes, mixer, FX, EQ, X/Y and "
        "projects are active; TRS MIDI/clock, panel controls and LEDs are absent");
  }
  updateTransport();
  updatePanelLeds();
}

void AppController::shutdown() {
  if (!started_) return;
  timer_.stop();
  link_.stop();
  audio_.stop();
  midi_.close();
  started_ = false;
}

void AppController::setTab(int tab) {
  const int clamped = std::clamp(tab, 0, kScreenCount - 1);
  closeTabGrid();
  closeBankGrid();
  closeTempoPopup();
  closeTakeSheet();
  closeTakeEdit();
  if (arrangeVm_ != nullptr) {
    arrangeVm_->closeClipMenu();
    arrangeVm_->closeLengthPopup();
  }
  if (clamped == tab_) return;
  // D3: a tab without a second face leaves the SHIFT face instead of landing
  // in a dead state with an empty ring.
  if (shiftFace_ && !tabHasShiftFace(clamped)) {
    shiftFace_ = false;
    panelMode_ = QStringLiteral("navigate");
    emit panelModeChanged();
  }
  // Fullscreen belongs to the EQ grid; any tab change brings the bridge back so
  // the other screens keep their tab row and transport strip.
  if (fullscreen_ && clamped != kEqTabIndex) setFullscreen(false);
  hideValuePopup();
  focusByScreen_[tab_] = focusIndex_;
  tab_ = clamped;
  state_.currentTab.store(tab_);
  focusIndex_ = focusByScreen_[tab_];
  emit tabChanged();
  rebuildFocusIds();
}

void AppController::setFullscreen(bool fullscreen) {
  if (fullscreen == fullscreen_) return;
  fullscreen_ = fullscreen;
  emit fullscreenChanged();
  rebuildFocusIds();
}

void AppController::setClickEnabled(bool enabled) {
  if (enabled == state_.clickEnabled.load()) return;
  state_.clickEnabled.store(enabled);
  // A8: the button flips only the enable flag. A configured REC ONLY click
  // stays REC ONLY; only an Off mode falls back to the last configured mode
  // (On play when the session has never had one).
  if (enabled && state_.clickMode.load() == 0) {
    state_.clickMode.store(lastNonZeroClickMode_ != 0 ? lastNonZeroClickMode_ : 2);
  }
  emit clickChanged();
}

QString AppController::clickLabel() const {
  if (!state_.clickEnabled.load()) return QStringLiteral("CLICK OFF");
  if (state_.clickMode.load() == 1) return QStringLiteral("CLICK REC");
  return QStringLiteral("CLICK");
}

void AppController::setClickLevel(double level) {
  const double clamped = std::clamp(level, 0.0, 1.0);
  state_.clickLevel.store(static_cast<float>(clamped));
  state_.clickGainDb.store(gainToDb(static_cast<float>(clamped)));
  emit clickChanged();
}

void AppController::setClickBars(int bars) {
  state_.clickBars.store(std::clamp(bars, 1, 8));
  emit clickChanged();
}

void AppController::setToolsPage(int page) {
  const int clamped = std::clamp(page, 0, 2);
  if (clamped == toolsPage_) return;
  toolsPage_ = clamped;
  emit toolsPageChanged();
}

void AppController::setSystemShiftPage(int page) {
  const int clamped = std::clamp(page, 0, 2);
  if (clamped == systemShiftPage_) return;
  systemShiftPage_ = clamped;
  emit systemShiftPageChanged();
  rebuildFocusIds();
}

void AppController::play() {
  // A1: an armed strip with an active take monitors its input, so PLAY would
  // replay the room instead of the take. Say it in the notice slot.
  const int muted = mixerVm_->armedWithActiveTakeCount();
  if (muted > 0) {
    showNotice(NoticeWarn,
               QStringLiteral("playback: %1 armed track(s) monitor input, not their "
                              "takes")
                   .arg(muted));
  }
  if (!link_.status().rolling) {
    if (chord_.onPress(ChordDetector::Button::Play, static_cast<std::uint32_t>(nowMs()))) {
      startTakeNow();
    } else {
      link_.requestTransport(1);
    }
  }
}

void AppController::playStart() {
  link_.requestTransport(1);
}

void AppController::stop() {
  pendingRec_ = false;
  cancelRecSchedule();
  chord_.reset();
  link_.requestTransport(0);
  if (audio_.recording()) stopTake();
}

void AppController::recToggle() {
  // The panel REC button (amount 0) waits up to 300 ms for a PLAY press while
  // stopped, so the documented REC+PLAY chord can start the take; REC alone
  // still starts it when the window expires.
  if (audio_.recording()) {
    stopTake();
    return;
  }
  if (link_.status().rolling) {
    startTakeNow();
    return;
  }
  if (chord_.onPress(ChordDetector::Button::Rec, static_cast<std::uint32_t>(nowMs()))) {
    startTakeNow();
  } else {
    pendingRec_ = true;
    pendingRecMs_ = nowMs();
  }
}

void AppController::toggleOverdub() {
  state_.overdub.store(!state_.overdub.load());
  emit transportChanged();
  updatePanelLeds();
}

void AppController::selectStrip(int index) {
  const int clamped = std::clamp(index, 0, audio::kMaxTracks - 1);
  state_.selectedStrip.store(clamped);
  mixerVm_->setSelectedStrip(clamped);
}

void AppController::shiftAction(int index) {
  switch (index) {
    case 0:
      projectsVm_->saveProject();
      break;
    case 1:
      midi_.panic();
      showNotice(NoticeInfo,
                 QStringLiteral("PANIC sent (CC123, all 16 channels)"));
      break;
    case 2:
      tapTempo();
      break;
    case 3:
      setClickEnabled(!clickEnabled());
      break;
    default:
      break;
  }
}

void AppController::tapTempo() {
  const auto now = nowMs();
  tapTimes_.push_back(now);
  while (!tapTimes_.empty() && now - tapTimes_.front() > 2000) tapTimes_.pop_front();
  if (tapTimes_.size() < 3) {
    showNotice(NoticeInfo,
               QStringLiteral("tap tempo: %1/3 taps")
                   .arg(static_cast<int>(tapTimes_.size())));
    return;
  }
  // Three-tap average: the last three taps give the two intervals the tempo
  // is averaged over, so the hint (TAP three times) and the math agree.
  const std::size_t count = std::min<std::size_t>(tapTimes_.size(), 3);
  const std::size_t start = tapTimes_.size() - count;
  double total = 0.0;
  for (std::size_t i = start + 1; i < tapTimes_.size(); ++i) {
    total += static_cast<double>(tapTimes_[i] - tapTimes_[i - 1]);
  }
  const double average = total / static_cast<double>(count - 1);
  if (average <= 0.0) return;
  const int bpmX100 =
      clampTempoX100(static_cast<int>(std::lround(60000.0 * 100.0 / average)));
  state_.tempoX100.store(bpmX100);
  link_.requestTempo(static_cast<std::uint16_t>(bpmX100));
  showValuePopup(QStringLiteral("TEMPO"), QStringLiteral("bpm"), bpmX100);
  showNotice(NoticeInfo, QStringLiteral("tap tempo: ") +
                             QString::fromStdString(formatTempo(bpmX100)));
  updateTransport();
}

void AppController::openTempoPopup() {
  if (tempoPopupOpen_) return;
  tempoPopupOpen_ = true;
  emit tempoPopupChanged();
  rebuildFocusIds();
}

void AppController::closeTempoPopup() {
  if (!tempoPopupOpen_) return;
  tempoPopupOpen_ = false;
  emit tempoPopupChanged();
  rebuildFocusIds();
}

void AppController::setTempoBpm(int bpm) {
  const int tempo = clampTempoX100(std::clamp(bpm, 40, 240) * 100);
  if (tempo == state_.tempoX100.load()) {
    showValuePopup(QStringLiteral("TEMPO"), QStringLiteral("bpm"), tempo);
    return;
  }
  state_.tempoX100.store(tempo);
  link_.requestTempo(static_cast<std::uint16_t>(tempo));
  showValuePopup(QStringLiteral("TEMPO"), QStringLiteral("bpm"), tempo);
  updateTransport();
}

void AppController::openTabGrid() {
  if (tabGridOpen_) return;
  tabGridOpen_ = true;
  focusIndex_ = tab_;
  emit tabGridChanged();
  rebuildFocusIds();
}

void AppController::closeTabGrid() {
  if (!tabGridOpen_) return;
  tabGridOpen_ = false;
  emit tabGridChanged();
  rebuildFocusIds();
}

void AppController::openBankGrid() {
  if (bankGridOpen_) return;
  bankGridOpen_ = true;
  emit bankGridChanged();
  rebuildFocusIds();
}

void AppController::closeBankGrid() {
  if (!bankGridOpen_) return;
  bankGridOpen_ = false;
  emit bankGridChanged();
  rebuildFocusIds();
}

void AppController::chooseBankPage(int page) {
  mixerVm_->setBank(page);
  showTextPopup(QStringLiteral("MIXER PAGE"), mixerVm_->bankText());
  closeBankGrid();
}

bool AppController::tabHasShiftFace(int tab) const {
  // D3: tabs 5 (MIDI) and 7 (XY) have no second face.
  return tab <= 3 || tab == kProjectsTabIndex || tab == kEqTabIndex ||
         tab == kArrangeTabIndex;
}

void AppController::requestKeypad(const QString& title, const QString& initial,
                                  const QString& action, int index) {
  keypadTitle_ = title;
  keypadInitial_ = initial;
  keypadAction_ = action;
  keypadIndex_ = index;
  keypadError_.clear();
  keypadOpen_ = true;
  emit keypadChanged();
}

void AppController::keypadAccept(const QString& text) {
  QString error;
  if (keypadAction_ == QStringLiteral("project-rename")) {
    error = projectsVm_->renameSelected(text);
  } else if (keypadAction_ == QStringLiteral("take-rename")) {
    error = recorderVm_->renameSelected(text);
  } else if (keypadAction_ == QStringLiteral("render-region")) {
    // RENDER REGION TO TRACK: the baked file becomes the new track's ACTIVE
    // take. The source track keeps its take, because the render wrote a new
    // file instead of moving one.
    error = QString::fromStdString(validateProjectName(text.toStdString()));
    if (error.isEmpty()) {
      const int slot = mixer_.firstInactiveSlot();
      if (slot < 0 || pendingRenderPath_.empty()) {
        error = QStringLiteral("no free track slot");
      } else {
        const QString key = QString::fromStdString(pendingRenderPath_.string());
        if (!mixerVm_->addTrack(slot, text, -1, false, 0)) {
          error = QStringLiteral("could not add the track");
        } else {
          mixerVm_->setMode(slot, kTrackModePlay);
          settings_.setActiveTake(slot, key.toStdString());
          recorderVm_->resyncActiveTakes();
          mixerVm_->refresh();
          recorderVm_->tracksChanged();
          timelineVm_->refresh();
          pendingRenderPath_.clear();
          showNotice(NoticeInfo, QStringLiteral("track %1 %2 - rendered take")
                                     .arg(slot + 1, 2, 10, QLatin1Char('0'))
                                     .arg(text));
        }
      }
    }
  } else if (keypadAction_ == QStringLiteral("new-track-from-take")) {
    // The name is accepted here; the MOVE / KEEP choice is the next popup.
    error = QString::fromStdString(validateProjectName(text.toStdString()));
    if (error.isEmpty()) {
      const auto* entry = recorderVm_->takes()->entry(keypadIndex_);
      const int slot = mixer_.firstInactiveSlot();
      if (entry == nullptr || slot < 0) {
        error = QStringLiteral("no free track slot");
      } else {
        pendingTrackFromTakeName_ = text;
        configVm_->setTrackFromTakeTarget(
            keypadIndex_,
            QStringLiteral("%1  ->  TRACK %2 %3")
                .arg(entry->name)
                .arg(slot + 1, 2, 10, QLatin1Char('0'))
                .arg(text.toUpper()));
        keypadOpen_ = false;
        keypadError_.clear();
        emit keypadChanged();
        openConfigInternal(QStringLiteral("trackfromtake"));
        return;
      }
    }
  } else if (keypadAction_ == QStringLiteral("take-import")) {
    // D7: the header is read before anything is imported. A 48 kHz violation
    // keeps the keypad open with the refusal; a good file shows its duration
    // and channel count and asks once before it becomes ACTIVE.
    const QString preview = recorderVm_->takeImportPreview(text);
    if (preview.startsWith(QStringLiteral("import refused")) ||
        preview.startsWith(QStringLiteral("no such file")) ||
        preview.startsWith(QStringLiteral("import needs")) ||
        preview.startsWith(QStringLiteral("enter a WAV"))) {
      error = preview;
    } else {
      pendingImportPath_ = text;
      keypadOpen_ = false;
      keypadError_.clear();
      emit keypadChanged();
      requestConfirm(QStringLiteral("IMPORT TAKE"), preview,
                     QStringLiteral("IMPORT"), QStringLiteral("take-import"), -1);
      return;
    }
  } else if (keypadAction_ == QStringLiteral("pipe-chain-export")) {
    error = routingVm_->exportPipeChain(
        text, QString::fromStdString(state_.currentProject().string()));
    if (error.isEmpty()) {
      showNotice(NoticeInfo, QStringLiteral("pipe chain exported: ") + text);
      showTextPopup(QStringLiteral("PIPE CHAIN EXPORTED"), text);
    }
  } else if (keypadAction_ == QStringLiteral("arrange-section-rename")) {
    arrangeVm_->renameSection(text);
  } else if (keypadAction_ == QStringLiteral("track-rename")) {
    // Same 48-character printable-ASCII rule as a project name.
    error = QString::fromStdString(validateProjectName(text.toStdString()));
    if (error.isEmpty()) {
      mixer_.setTrackName(keypadIndex_, text.toStdString());
      mixerVm_->notifyTrackNamesChanged();
      recorderVm_->tracksChanged();
      timelineVm_->refresh();
      eqVm_->tracksChanged();
      showTextPopup(QStringLiteral("TRACK RENAMED"), text);
    }
  } else if (keypadAction_ == QStringLiteral("add-track-name")) {
    configVm_->setAddTrackName(text);
  } else if (keypadAction_ == QStringLiteral("pipe-chain-import")) {
    error = routingVm_->importPipeChain(
        text, QString::fromStdString(state_.currentProject().string()));
    if (error.isEmpty()) {
      showNotice(NoticeInfo, QStringLiteral("pipe chain imported: ") + text);
      showTextPopup(QStringLiteral("PIPE CHAIN IMPORTED"), text);
    }
  }
  if (!error.isEmpty()) {
    keypadError_ = error;
    emit keypadChanged();
    return;
  }
  keypadOpen_ = false;
  keypadError_.clear();
  emit keypadChanged();
}

void AppController::keypadCancel() {
  keypadOpen_ = false;
  keypadError_.clear();
  emit keypadChanged();
}

void AppController::requestConfirm(const QString& title, const QString& message,
                                   const QString& confirmLabel, const QString& action,
                                   int index) {
  confirmTitle_ = title;
  confirmMessage_ = message;
  confirmLabel_ = confirmLabel;
  confirmAction_ = action;
  confirmIndex_ = index;
  confirmOpen_ = true;
  emit confirmChanged();
}

void AppController::confirmAccept() {
  const QString action = confirmAction_;
  const int index = confirmIndex_;
  confirmOpen_ = false;
  emit confirmChanged();
  if (action == QStringLiteral("project-load")) {
    projectsVm_->loadRow(index);
  } else if (action == QStringLiteral("project-delete")) {
    projectsVm_->selectRow(index);
    projectsVm_->deleteSelected();
  } else if (action == QStringLiteral("take-delete")) {
    recorderVm_->deleteSelected();
    timelineVm_->refresh();
    mixerVm_->notifyActiveTakesChanged();
  } else if (action == QStringLiteral("arrange-clip-delete")) {
    arrangeVm_->deleteClipAt(index);
  } else if (action == QStringLiteral("take-remove")) {
    // Project only: the take file stays on disk.
    recorderVm_->removeSelected();
    timelineVm_->refresh();
    mixerVm_->notifyActiveTakesChanged();
    showNotice(NoticeInfo, QStringLiteral("take removed from the project - "
                                           "the file stays on disk"));
  } else if (action == QStringLiteral("remove-track")) {
    mixerVm_->removeTrack(index);
  } else if (action == QStringLiteral("routing-reset")) {
    routingVm_->reset();
    showNotice(NoticeInfo, QStringLiteral("routing reset to TRS IN 1/2 -> USB OUT"));
  } else if (action == QStringLiteral("take-import")) {
    const QString path = pendingImportPath_;
    pendingImportPath_.clear();
    const QString error = recorderVm_->importTake(path);
    if (!error.isEmpty()) {
      showNotice(NoticeError, error);
    } else {
      showTextPopup(QStringLiteral("TAKE IMPORTED"),
                    QFileInfo(path).fileName() + QStringLiteral(" is ACTIVE"));
    }
  } else if (action == QStringLiteral("open-system")) {
    setTab(3);
  }
}

void AppController::confirmCancel() {
  confirmOpen_ = false;
  emit confirmChanged();
}

void AppController::requestLoadProject(int row) {
  projectsVm_->selectRow(row);
  if (dirty()) {
    requestConfirm(QStringLiteral("LOAD PROJECT"),
                   QStringLiteral("Discard unsaved changes and load ") +
                       projectsVm_->projectName(row) + QStringLiteral("?"),
                   QStringLiteral("DISCARD + LOAD"), QStringLiteral("project-load"), row);
    return;
  }
  projectsVm_->loadRow(row);
}

void AppController::selectProjectRow(int row) {
  // A3: the row's "..." target selects without loading, so RENAME/DELETE on
  // the SHIFT face can act on a visible row without discarding the session.
  projectsVm_->selectRow(row);
  const QString name = projectsVm_->projectName(row);
  if (!name.isEmpty()) {
    showTextPopup(QStringLiteral("SELECTED"),
                  name + QStringLiteral(" - tap the row to load"));
  }
}

void AppController::selectTakeRow(int row) {
  recorderVm_->selectRow(row);
  const auto* entry = recorderVm_->takes()->entry(row);
  if (entry != nullptr) {
    showTextPopup(QStringLiteral("SELECTED"), entry->name);
  }
}

void AppController::activateTakeRow(int row) {
  // A3/A10: the row body loads the take (ACTIVE + waveform). The name comes
  // back as a value, never as a pointer into the take list: activating rescans
  // that list, and a pointer held across the rescan is a use-after-free (the
  // SIGBUS a clip tap used to cause on the Pi).
  const QString name = recorderVm_->activateRow(row);
  if (!name.isEmpty()) {
    showTextPopup(QStringLiteral("TAKE ACTIVE"), name);
  }
}

void AppController::requestDeleteProject(int row) {
  projectsVm_->selectRow(row);
  requestConfirm(QStringLiteral("DELETE PROJECT ") + projectsVm_->projectName(row),
                 QStringLiteral("Delete ") + projectsVm_->projectName(row) +
                     QStringLiteral("? The folder and its project.mnkybox are removed. "
                                    "This cannot be undone."),
                 QStringLiteral("DELETE"), QStringLiteral("project-delete"), row);
}

void AppController::requestRenameProject() {
  const int row = projectsVm_->selectedRow();
  if (row < 0) {
    showNotice(NoticeWarn, QStringLiteral("select a row first (...)"));
    return;
  }
  requestKeypad(QStringLiteral("RENAME PROJECT ") + projectsVm_->projectName(row),
                projectsVm_->projectName(row),
                QStringLiteral("project-rename"), row);
}

void AppController::requestRenameTake() {
  const int row = recorderVm_->selectedRow();
  const auto* entry = recorderVm_->takes()->entry(row);
  if (entry == nullptr) {
    showNotice(NoticeWarn, QStringLiteral("select a row first (...)"));
    return;
  }
  requestKeypad(QStringLiteral("RENAME TAKE ") + entry->name,
                QString::fromStdString(entry->path.filename().string()),
                QStringLiteral("take-rename"), row);
}

void AppController::requestDeleteTake() {
  const int row = recorderVm_->selectedRow();
  const auto* entry = recorderVm_->takes()->entry(row);
  if (entry == nullptr) {
    showNotice(NoticeWarn, QStringLiteral("select a row first (...)"));
    return;
  }
  // W4: the confirmation names the file it removes, verbatim per the plan.
  const QString file = QString::fromStdString(entry->path.filename().string());
  requestConfirm(QStringLiteral("DELETE TAKE ") + entry->name,
                 QStringLiteral("DELETE ") + file +
                     QStringLiteral(" from disk? This cannot be undone."),
                 QStringLiteral("DELETE"), QStringLiteral("take-delete"), row);
}

void AppController::showNoPicoInfo() {
  requestConfirm(
      QStringLiteral("NO PICO"),
      QStringLiteral("TRS MIDI, TRS clock, panel switches and LEDs are absent. "
                     "USB MIDI, USB clock, audio, takes and projects stay live."),
      QStringLiteral("OPEN SYSTEM"), QStringLiteral("open-system"), -1);
}

void AppController::requestImportTake() {
  requestKeypad(QStringLiteral("IMPORT TAKE (WAV PATH)"),
                QString::fromStdString(state_.takeDirectory().string()) +
                    QLatin1Char('/'),
                QStringLiteral("take-import"), -1);
}

void AppController::requestExportPipeChain() {
  requestKeypad(QStringLiteral("EXPORT PIPE CHAIN (NAME)"),
                routingVm_->defaultChainName(), QStringLiteral("pipe-chain-export"),
                -1);
}

void AppController::requestImportPipeChain() {
  const std::string directory = state_.currentProject().string();
  const QString initial =
      directory.empty() ? QString()
                        : QString::fromStdString(directory) + QLatin1Char('/');
  requestKeypad(QStringLiteral("IMPORT PIPE CHAIN (NAME)"), initial,
                QStringLiteral("pipe-chain-import"), -1);
}

void AppController::requestAddTrack() {
  openConfigFrom(QStringLiteral("addtrack"), nullptr);
}

void AppController::requestRemoveTrack(int slot) {
  const QString name = mixerVm_->suggestedTrackName(slot);  requestConfirm(
      QStringLiteral("REMOVE TRACK"),
      QStringLiteral("Remove %1 from the mixer? Its input, sends, insert and "
                     "takes stay in the project, so re-adding the track "
                     "restores them.")
          .arg(name.toUpper()),
      QStringLiteral("REMOVE"), QStringLiteral("remove-track"), slot);
}

void AppController::requestRenameTrack(int slot) {
  if (!mixerVm_->trackSlotActive(slot)) return;
  requestKeypad(QStringLiteral("RENAME TRACK %1").arg(slot + 1, 2, 10, QLatin1Char('0')),
                QString::fromStdString(mixer_.trackName(slot)),
                QStringLiteral("track-rename"), slot);
}

void AppController::openTakeSheet(int track) {
  setTakeSheetTrack(track);
}

void AppController::closeTakeSheet() {
  if (!takeSheetOpen_) return;
  takeSheetOpen_ = false;
  emit takeSheetChanged();
  rebuildFocusIds();
}

void AppController::setTakeSheetTrack(int track) {
  const int clamped = std::clamp(track, 0, kNumStrips - 1);
  // The sheet lists the recorder's per-track take list, so the recorder's
  // selected track follows the lane the sheet was opened from.
  recorderVm_->setSelectedTrack(clamped);
  timelineVm_->setSelectedTrack(clamped);
  takeSheetTrack_ = clamped;
  takeSheetOpen_ = true;
  emit takeSheetChanged();
  rebuildFocusIds();
}

void AppController::chooseTakeRow(int row) {
  if (takeSheetTrack_ < 0) return;
  recorderVm_->selectRow(row);
  recorderVm_->activateSelected();
  const auto* entry = recorderVm_->takes()->entry(row);
  closeTakeSheet();
  timelineVm_->refresh();
  if (entry != nullptr) {
    showTextPopup(QStringLiteral("TAKE ACTIVE"), entry->name);
  }
}

void AppController::openTakeEdit(int row) {
  const auto* entry = recorderVm_->takes()->entry(row);
  if (entry == nullptr) {
    showNotice(NoticeWarn, QStringLiteral("select a take row first (...)"));
    return;
  }
  // Copy everything this call needs before the activation below rescans the
  // take folder: the row storage is freed by that rescan (the SIGBUS class the
  // clip-click tap gate covers).
  const QString name = entry->name;
  const bool active = entry->active;
  const int track = recorderVm_->selectedTrack();
  recorderVm_->selectRow(row);
  if (!active) {
    // Region edits are applied to the track's ACTIVE take, so the EDIT subject
    // has to be the take the track plays. Activating is a pointer change: no
    // file is written and UNDO/CANCEL never touch audio.
    recorderVm_->activateSelected();
  }
  closeTakeSheet();
  timelineVm_->openEdit(track);
  showNotice(NoticeInfo,
             QStringLiteral("EDIT %1 - non-destructive (RENDER writes a new take)")
                 .arg(name));
}

void AppController::closeTakeEdit() {
  if (timelineVm_ == nullptr || !timelineVm_->editOpen()) return;
  timelineVm_->closeEdit();
  rebuildFocusIds();
}

void AppController::chooseLiveInput() {
  if (takeSheetTrack_ < 0) return;
  recorderVm_->clearActive();
  closeTakeSheet();
  timelineVm_->refresh();
  showTextPopup(QStringLiteral("TAKE"), QStringLiteral("LIVE INPUT"));
}

bool AppController::learnArmed() const {
  return learnVm_ != nullptr && learnVm_->targetArmed();
}

void AppController::armLearnTargetCapture() {
  learnVm_->armTargetCapture();
  emit learnChanged();
}

bool AppController::isLearnableFocus(const QString& focusId) const {
  // The registered learn targets (AppController::registerLearnTargets) are the
  // mixer gain/pan/sends, the master gain and the click level. A control id
  // maps to one of those paths only when the id names the same parameter.
  QString id = focusId;
  if (id.startsWith(QStringLiteral("shift."))) id = id.mid(6);
  if (id.startsWith(QStringLiteral("mixer.gain.")) ||
      id.startsWith(QStringLiteral("mixer.pan.")) ||
      id.startsWith(QStringLiteral("mixer.fx1.")) ||
      id.startsWith(QStringLiteral("mixer.fx2."))) {
    return true;
  }
  return id == QStringLiteral("mixer.master");
}

void AppController::learnTargetTouched(const QString& focusId) {
  if (!learnArmed()) return;
  QString id = focusId;
  if (id.startsWith(QStringLiteral("shift."))) id = id.mid(6);
  QString path;
  if (id == QStringLiteral("mixer.master")) {
    path = QStringLiteral("mixer.master.gain");
  } else if (id.startsWith(QStringLiteral("mixer.gain."))) {
    path = QString::fromStdString(
        learnTarget(id.section(QLatin1Char('.'), 2, 2).toInt(), "gain"));
  } else if (id.startsWith(QStringLiteral("mixer.pan."))) {
    path = QString::fromStdString(
        learnTarget(id.section(QLatin1Char('.'), 2, 2).toInt(), "pan"));
  } else if (id.startsWith(QStringLiteral("mixer.fx1."))) {
    path = QString::fromStdString(
        learnTarget(id.section(QLatin1Char('.'), 2, 2).toInt(), "fx1wet"));
  } else if (id.startsWith(QStringLiteral("mixer.fx2."))) {
    path = QString::fromStdString(
        learnTarget(id.section(QLatin1Char('.'), 2, 2).toInt(), "fx2wet"));
  }
  if (path.isEmpty()) return;
  learnVm_->setTargetByPath(path);
  showTextPopup(QStringLiteral("LEARN TARGET"), LearnViewModel::humanTarget(path));
}

void AppController::requestAddTrackName() {
  requestKeypad(QStringLiteral("TRACK NAME"), configVm_->addTrackName(),
                QStringLiteral("add-track-name"), 0);
}

void AppController::openInputSource(int slot) {
  configVm_->setInputTarget(slot);
  openConfigFrom(QStringLiteral("inputsource"), nullptr);
}

void AppController::openOutputDest(int slot) {
  configVm_->setOutputTarget(slot);
  openConfigFrom(QStringLiteral("outputdest"), nullptr);
}

void AppController::openTrackFx(int slot) {
  fxVm_->openFor(mixerVm_->selectedStrip(), slot);
  rebuildFocusIds();
}

void AppController::closeTrackFx() {
  fxVm_->close();
  rebuildFocusIds();
}

void AppController::openFxSlot(int slot) {
  configVm_->setFxTarget(mixerVm_->selectedStrip(), slot);
  openConfigFrom(QStringLiteral("fxslot"), nullptr);
}

void AppController::openFxSlotParams(int slot) {
  const int track = mixerVm_->selectedStrip();
  const int choice = mixerVm_->fxChoiceAt(track, slot);
  configVm_->setFxTarget(track, slot);
  if (choice == 1) {
    openConfigFrom(QStringLiteral("delay"), nullptr);
  } else if (choice == 2) {
    openConfigFrom(QStringLiteral("reverb"), nullptr);
  } else if (choice >= MixerViewModel::kFxChoicePlugin) {
    // A CLAP slot's editors are the plugin's own parameter chips in TRACK FX.
    mixerVm_->setTrackIndex(track);
    openTrackFx(slot);
  } else {
    // No effect in this slot: name the state instead of opening an editor that
    // would accept values nothing runs.
    showTextPopup(QStringLiteral("FX%1").arg(slot == 1 ? 2 : 1),
                  mixerVm_->fxSlotNote(track, slot));
  }
}

void AppController::openChannel(int slot) {
  mixerVm_->openChannel(slot);
  rebuildFocusIds();
}

void AppController::closeChannel() {
  mixerVm_->closeChannel();
  rebuildFocusIds();
}

void AppController::demoSeedArrange() {
  if (arrangeVm_ == nullptr) return;
  settings_.song.sections.clear();
  settings_.song.clips.clear();
  arrangeVm_->addSection();  // INTRO
  arrangeVm_->addSection();  // SECTION 2
  arrangeVm_->addSection();  // SECTION 3
  settings_.song.sections[0].name = "INTRO";
  settings_.song.sections[0].bars = 4;
  settings_.song.sections[1].name = "VERSE";
  settings_.song.sections[1].bars = 8;
  settings_.song.sections[2].name = "CHORUS";
  settings_.song.sections[2].bars = 8;
  settings_.songLengthBeats = 80.0;  // 20 bars
  settings_.loopOn = true;
  settings_.loopStartBeats = 0.0;
  settings_.loopEndBeats = 0.0;
  settings_.songMode = true;
  std::string take;
  int takeTrack = -1;
  for (int track = 0; track < audio::kMaxTracks; ++track) {
    const auto key = settings_.activeTake(track);
    if (!key.empty()) {
      take = key;
      takeTrack = track;
      break;
    }
  }
  if (take.empty()) {
    take = "demo-take.wav";
    takeTrack = mixerVm_->trackIndex();
  }
  int first = -1;
  const double starts[] = {0.0, 24.0, 48.0, 24.0, 48.0};
  const int tracks[] = {takeTrack, takeTrack, takeTrack, takeTrack + 1, takeTrack + 1};
  for (int index = 0; index < 5; ++index) {
    const int track = std::clamp(tracks[index], 0, audio::kMaxTracks - 1);
    const int clip = arrangeVm_->placePunchTake(track, take, starts[index], nullptr);
    if (index == 0) first = clip;
  }
  arrangeVm_->reloadFromSettings();
  if (first >= 0) arrangeVm_->selectClip(first);
  showNotice(NoticeInfo, QStringLiteral("ARRANGE demo - 3 sections, 5 clips"));
}

void AppController::demoPunchAutoPlace() {
  if (arrangeVm_ == nullptr) return;
  settings_.songMode = true;
  settings_.punch.in.enabled = true;
  settings_.punch.in.measure = 9;
  settings_.punch.out.enabled = true;
  settings_.punch.out.measure = 11;
  settings_.loopStartBeats = 0.0;
  settings_.loopEndBeats = 0.0;
  punchPlaceStartBeats_ = arrange::punchStartBeats(9, 0.0);
  std::string key;
  int sourceTrack = mixerVm_->trackIndex();
  for (int current = 0; current < audio::kMaxTracks; ++current) {
    const auto active = settings_.activeTake(current);
    if (!active.empty()) {
      key = active;
      sourceTrack = current;
      break;
    }
  }
  if (key.empty()) key = "demo-take.wav";
  // The punch clip lands on an empty lane so the demo placement cannot be
  // refused by an existing clip; the take key is independent of the lane.
  int track = std::min(audio::kMaxTracks - 1, sourceTrack + 2);
  if (track == sourceTrack) track = std::max(0, sourceTrack - 2);
  arrangeVm_->placePunchTake(track, key, punchPlaceStartBeats_, nullptr);
  punchPlaceArmed_ = false;
  showNotice(NoticeInfo, QStringLiteral("TAKE SAVED -> clip at bar %1")
                             .arg(arrange::barOfBeat(punchPlaceStartBeats_)));
}

void AppController::openDemoKeypad() {
  requestKeypad(QStringLiteral("RENAME PROJECT"), QStringLiteral("Project-demo"),
                QStringLiteral("project-rename"), -1);
}

void AppController::showShiftFace(bool on) {
  // D3: a tab without a second face never enters a dead SHIFT state.
  if (on && !tabHasShiftFace(tab_)) {
    showNotice(NoticeWarn, QStringLiteral("NO SHIFT FACE ON THIS TAB"));
    showTextPopup(QStringLiteral("SHIFT"),
                  QStringLiteral("NO SHIFT FACE ON THIS TAB"));
    return;
  }
  if (on) {
    closeTabGrid();
    closeTempoPopup();
    closeTakeSheet();
  }
  shiftFace_ = on;
  setPanelMode(on ? QStringLiteral("shift") : QStringLiteral("navigate"));
  rebuildFocusIds();
}

void AppController::injectDemoTrsMidi(int port, int status, int data1, int data2) {
  midi::MidiEvent event;
  event.source = port == 0 ? midi::Port::TrsIn1 : midi::Port::TrsIn2;
  event.size = 3;
  event.data[0] = static_cast<std::uint8_t>(status & 0xFF);
  event.data[1] = static_cast<std::uint8_t>(data1 & 0x7F);
  event.data[2] = static_cast<std::uint8_t>(data2 & 0x7F);
  event.stampUs = midi::piNowUs();
  midi_.handleInput(event);
}

void AppController::openConfig(const QString& kind) {
  // A panel/keyboard path has no summary button to grow from: use the panel
  // centre so a later openConfigFrom() origin cannot leak into this popup.
  configOriginX_ = -1.0;
  configOriginY_ = -1.0;
  emit configOriginChanged();
  openConfigInternal(kind);
}

void AppController::openConfigFrom(const QString& kind, QObject* item) {
  auto* quick = qobject_cast<QQuickItem*>(item);
  if (quick != nullptr) {
    const QPointF centre =
        quick->mapToScene(QPointF(quick->width() / 2.0, quick->height() / 2.0));
    configOriginX_ = centre.x();
    configOriginY_ = centre.y();
  } else {
    configOriginX_ = -1.0;
    configOriginY_ = -1.0;
  }
  emit configOriginChanged();
  openConfigInternal(kind);
}

void AppController::openPipeParam(int pipe, int param, QObject* item) {
  configVm_->setPipeParamTarget(pipe, param);
  openConfigFrom(QStringLiteral("pipeparam"), item);
}

void AppController::openConfigInternal(const QString& kind) {
  // A card always names its parameter; this is the panel/keyboard path, where
  // the ring points at the selected pipe's first positional parameter.
  if (kind == QStringLiteral("pipeparam")) {
    configVm_->setPipeParamTarget(routingVm_->selectedPipe(),
                                  configVm_->pipeParamIndex());
  }
  // A selector popup opened without an explicit strip (panel/keyboard path)
  // targets the TRACK screen's strip, which is where the ring points.
  if (kind == QStringLiteral("inputsource") &&
      !mixerVm_->trackSlotActive(configVm_->inputTarget())) {
    configVm_->setInputTarget(mixerVm_->trackIndex());
  } else if (kind == QStringLiteral("outputdest") &&
             !mixerVm_->trackSlotActive(configVm_->outputTarget())) {
    configVm_->setOutputTarget(mixerVm_->trackIndex());
  } else if (kind == QStringLiteral("fxslot") &&
             !mixerVm_->trackSlotActive(configVm_->fxTargetTrack())) {
    // The selector always targets a slot on a live track: a removed track would
    // show an empty list over a strip nobody can see.
    configVm_->setFxTarget(mixerVm_->selectedStrip(), configVm_->fxTargetSlot());
  } else if (kind == QStringLiteral("addtrack")) {
    configVm_->setAddTrackSlot(mixerVm_->nextFreeSlot());
  }
  if (!configVm_->openKind(kind)) return;
  // A popup opened from the SHIFT face keeps that face underneath it; the
  // popup's own focus trap replaces the ring while it is open, and CANCEL puts
  // the shift stops back instead of dropping to the primary face.
  panelMode_ = shiftFace_ ? QStringLiteral("shift") : QStringLiteral("navigate");
  focusIndex_ = 0;
  emit panelModeChanged();
  rebuildFocusIds();
}

void AppController::configAccept() {
  configVm_->accept();
  emit clickChanged();
  emit dirtyChanged();
  rebuildFocusIds();
}

void AppController::configCancel() {
  configVm_->cancel();
  rebuildFocusIds();
}

void AppController::timerTick() {
  Intent intent;
  while (state_.nextIntent(intent)) perform(intent);

  expirePendingRec();
  const auto status = link_.status();
  // SONG transport seek (W4/W5): the Pico tick resets on START, so preserve
  // the displayed song position across the stopped -> rolling transition. Free
  // mode keeps its legacy "START begins at bar 1" behaviour.
  if (settings_.songMode && status.rolling && !lastRolling_) {
    // target = the song position the previous link tick represented. The START
    // reset the raw tick to 0, so the offset absorbs the old link tick and the
    // displayed position (seek point or stop point) is preserved.
    const std::int64_t target =
        state_.transportOffsetTicks.load(std::memory_order_relaxed) +
        static_cast<std::int64_t>(lastLinkTick_);
    state_.transportOffsetTicks.store(
        target - static_cast<std::int64_t>(status.tick));
  }
  lastRolling_ = status.rolling;
  lastLinkTick_ = status.tick;
  // Loop wrap: the transport loops the song or section window while SONG mode
  // plays a non-empty arrangement. A wrap is a seek to the loop start, so the
  // engine's song reader detects the frame jump and re-primes.
  if (settings_.songMode && arrangeVm_ != nullptr && arrangeVm_->loopOn() &&
      status.rolling && arrangeVm_->clipCount() > 0) {
    const double beats = displayBeats(status);
    const double start = arrangeVm_->effectiveLoopStartBeats();
    const double end = arrangeVm_->effectiveLoopEndBeats();
    if (end > start && beats >= end) {
      const auto target = static_cast<std::int64_t>(
          std::llround(start / 4.0 * static_cast<double>(kTicksPerBeat)));
      state_.transportOffsetTicks.store(target - static_cast<std::int64_t>(status.tick));
      arrangeVm_->noteLoopWrap();
    }
  }
  pollRecSchedule(status);
  if (state_.recMode.load() != lastRecMode_ ||
      state_.countInBars.load() != lastCountInBars_) {
    lastRecMode_ = state_.recMode.load();
    lastCountInBars_ = state_.countInBars.load();
    emit recModeChanged();
  }
  state_.rolling.store(status.rolling);
  state_.tempoX100.store(status.tempoX100);
  if (status.tempoX100 != loopTempoX100_) {
    loopTempoX100_ = status.tempoX100;
    audio_.applyTakeLoops(settings_);
  }
  updateShiftState(status);
  updateTransport();
  if (arrangeVm_ != nullptr) arrangeVm_->refresh();

  ++refreshTicks_;
  // MIDI-learn writes the model atomics on RtMidi's thread; the mixer only has
  // to re-read them about once a second. FX params can be changed by the plugin
  // itself, so they refresh every tick.
  if (refreshTicks_ % 15 == 0) mixerVm_->refresh();
  if (refreshTicks_ % 15 == 0) learnVm_->refresh();
  // RtMidi/CoreMIDI/ALSA enumeration is cheap enough at this rate, and it is
  // the only way a hot-plugged interface becomes a routable node: the registry
  // keeps an id stable by identity across the unplug/replug.
  if (refreshTicks_ % 30 == 0) midi_.refreshDevices();
  routingVm_->refresh();
  fxVm_->refresh();
  recorderVm_->refresh();
  timelineVm_->refresh();
  projectsVm_->refresh();
  learnVm_->poll();
  midiMonitorVm_->refresh();
  if (refreshTicks_ % 4 == 0) systemVm_->refresh();
  if (refreshTicks_ % 15 == 0) toolsVm_->refresh();
  configVm_->refreshSummaries();
  audio_.applyMonitorSettings(settings_);

  updateDirtyState();
  updatePanelLeds();
  rebuildFocusIds();
}

void AppController::perform(const Intent& intent) {
  switch (intent.kind) {
    case Intent::Kind::None:
      break;
    case Intent::Kind::NavUp:
    case Intent::Kind::NavDown:
    case Intent::Kind::NavLeft:
    case Intent::Kind::NavRight:
    case Intent::Kind::NavPress:
    case Intent::Kind::EncoderSw:
      performPanelFocus(intent);
      break;
    case Intent::Kind::EncoderTurn:
      if (keypadOpen_) {
        // A2: the encoder moves the keypad cursor, never a hidden control.
        emit keypadCursorMoveRequested(intent.amount);
      } else if (confirmOpen_) {
        // The confirmation owns the panel; the encoder does nothing.
      } else if (tempoPopupOpen_) {
        setTempoBpm(state_.tempoX100.load() / 100 + intent.amount);
      } else if (configVm_ && configVm_->open()) {
        adjustFocused(intent.amount, shiftHeld_);
      } else if (panelMode_ == QStringLiteral("edit")) {
        adjustFocused(intent.amount, shiftHeld_);
      } else {
        const int step = shiftHeld_ ? kTempoStepFineX100 : kTempoStepCoarseX100;
        const int tempo = clampTempoX100(state_.tempoX100.load() + intent.amount * step);
        state_.tempoX100.store(tempo);
        link_.requestTempo(static_cast<std::uint16_t>(tempo));
        showValuePopup(QStringLiteral("TEMPO"), QStringLiteral("bpm"), tempo);
      }
      break;
    case Intent::Kind::TempoStep: {
      const int tempo = clampTempoX100(state_.tempoX100.load() + intent.amount);
      state_.tempoX100.store(tempo);
      link_.requestTempo(static_cast<std::uint16_t>(tempo));
      showValuePopup(QStringLiteral("TEMPO"), QStringLiteral("bpm"), tempo);
      break;
    }
    case Intent::Kind::Play:
      play();
      break;
    case Intent::Kind::PlayStart:
      playStart();
      break;
    case Intent::Kind::Stop:
      stop();
      break;
    case Intent::Kind::RecToggle:
      if (intent.amount >= 1) {
        // Screen START TAKE is immediate; the panel REC button uses the chord
        // window.
        if (audio_.recording()) {
          stopTake();
        } else {
          startTakeNow();
        }
      } else {
        recToggle();
      }
      break;
    case Intent::Kind::OverdubToggle:
      toggleOverdub();
      break;
    case Intent::Kind::Shift:
      break;
  }
  updatePanelLeds();
}

void AppController::performPanelFocus(const Intent& intent) {
  // A2: the keypad and the confirmation trap the panel. NAV is inert, PRESS
  // accepts, the encoder moves the keypad cursor (perform()), and the short
  // EXIT press cancels (updateShiftState). Nothing behind the modal can be
  // activated: rebuildFocusIds() returns an empty ring while one is open.
  if (keypadOpen_ || confirmOpen_) {
    switch (intent.kind) {
      case Intent::Kind::NavPress:
      case Intent::Kind::EncoderSw:
        if (keypadOpen_) {
          emit keypadAcceptRequested();
        } else {
          confirmAccept();
        }
        break;
      default:
        break;
    }
    return;
  }
  if (tempoPopupOpen_ || tabGridOpen_ || takeSheetOpen_) {
    const bool grid = tabGridOpen_;
    switch (intent.kind) {
      case Intent::Kind::NavUp:
        moveFocus(grid ? -5 : -1);
        break;
      case Intent::Kind::NavDown:
        moveFocus(grid ? 5 : 1);
        break;
      case Intent::Kind::NavLeft:
        moveFocus(-1);
        break;
      case Intent::Kind::NavRight:
        moveFocus(1);
        break;
      case Intent::Kind::NavPress:
      case Intent::Kind::EncoderSw:
        if (tempoPopupOpen_) {
          closeTempoPopup();
        } else if (tabGridOpen_) {
          activateFocus();
        } else {
          activateFocus();
        }
        break;
      default:
        break;
    }
    rebuildFocusIds();
    return;
  }
  if (arrangeVm_ != nullptr &&
      (arrangeVm_->clipMenuOpen() || arrangeVm_->lengthPopupOpen())) {
    // The clip "..." sheet and the LEN stepper are modals: the ring walks their
    // controls, NAV directions do not leak to the screen behind, and PRESS on
    // a knob card enters EDIT like every other focused value.
    switch (intent.kind) {
      case Intent::Kind::NavUp:
      case Intent::Kind::NavLeft:
        moveFocus(-1);
        break;
      case Intent::Kind::NavDown:
      case Intent::Kind::NavRight:
        moveFocus(1);
        break;
      case Intent::Kind::NavPress:
        if (panelMode_ == QStringLiteral("edit")) {
          setPanelMode(QStringLiteral("navigate"));
        } else if (isEditableFocus(focusId_)) {
          setPanelMode(QStringLiteral("edit"));
        } else {
          activateFocus();
        }
        break;
      case Intent::Kind::EncoderSw:
        if (panelMode_ == QStringLiteral("edit")) {
          setPanelMode(QStringLiteral("navigate"));
        } else {
          activateFocus();
        }
        break;
      default:
        break;
    }
    rebuildFocusIds();
    return;
  }
  if (configVm_ && configVm_->open()) {
    switch (intent.kind) {
      case Intent::Kind::NavUp:
      case Intent::Kind::NavLeft:
        moveFocus(-1);
        break;
      case Intent::Kind::NavDown:
      case Intent::Kind::NavRight:
        moveFocus(1);
        break;
      case Intent::Kind::NavPress:
        if (panelMode_ == QStringLiteral("edit")) {
          setPanelMode(QStringLiteral("navigate"));
        } else if (isEditableFocus(focusId_)) {
          setPanelMode(QStringLiteral("edit"));
        } else {
          activateFocus();
        }
        break;
      case Intent::Kind::EncoderSw:
        if (panelMode_ == QStringLiteral("edit")) {
          setPanelMode(QStringLiteral("navigate"));
        } else {
          activateFocus();
        }
        break;
      default:
        break;
    }
    rebuildFocusIds();
    return;
  }
  if (shiftFace_) {
    // SHIFT face: LEFT/RIGHT changes tab (still holding SHIFT), UP/DOWN moves
    // the focus ring inside the face. The tab bar itself is not in the face
    // ring, so a tab switch never loses the face.
    switch (intent.kind) {
      case Intent::Kind::NavLeft:
        setTab(moveIndex(tab_, -1, kScreenCount));
        break;
      case Intent::Kind::NavRight:
        setTab(moveIndex(tab_, 1, kScreenCount));
        break;
      case Intent::Kind::NavUp:
        moveFocus(-1);
        break;
      case Intent::Kind::NavDown:
        moveFocus(1);
        break;
      case Intent::Kind::NavPress:
        if (panelMode_ == QStringLiteral("edit")) {
          setPanelMode(QStringLiteral("navigate"));
        } else if (isEditableFocus(focusId_)) {
          setPanelMode(QStringLiteral("edit"));
        } else {
          activateFocus();
        }
        break;
      case Intent::Kind::EncoderSw:
        if (panelMode_ == QStringLiteral("edit")) {
          setPanelMode(QStringLiteral("navigate"));
        } else {
          activateFocus();
        }
        break;
      default:
        break;
    }
    rebuildFocusIds();
    return;
  }

  // ARRANGE NAV walks clips spatially (rows = tracks, columns = grid steps):
  // LEFT/RIGHT pick the neighbouring clip on the lane, UP/DOWN the nearest clip
  // on the adjacent lane. PRESS opens the clip "..."; the panel knob keeps the
  // focused clip in EDIT.
  if (tab_ == kArrangeTabIndex && !shiftFace_ && arrangeVm_ != nullptr &&
      focusId_.startsWith(QStringLiteral("arrange.clip."))) {
    const int dx = intent.kind == Intent::Kind::NavLeft
                       ? -1
                       : (intent.kind == Intent::Kind::NavRight ? 1 : 0);
    const int dy = intent.kind == Intent::Kind::NavUp
                       ? -1
                       : (intent.kind == Intent::Kind::NavDown ? 1 : 0);
    if (dx != 0 || dy != 0) {
      const int next = arrangeVm_->neighborClip(arrangeVm_->selectedClip(), dx, dy);
      if (next >= 0) {
        arrangeVm_->selectClip(next);
        focusId_ = QStringLiteral("arrange.clip.%1").arg(next);
        const auto found = std::find(focusIds_.begin(), focusIds_.end(), focusId_);
        if (found != focusIds_.end()) {
          focusIndex_ = static_cast<int>(std::distance(focusIds_.begin(), found));
          focusByScreen_[tab_] = focusIndex_;
        }
        emit focusChanged();
      }
      rebuildFocusIds();
      return;
    }
  }

  // EDIT on the EQ grid or an X/Y grid: the directions are the control (node
  // frequency/gain, grid point), so they never move the focus ring there.
  if (panelMode_ == QStringLiteral("edit") && performGridEdit(intent)) {
    rebuildFocusIds();
    return;
  }

  switch (intent.kind) {
    case Intent::Kind::NavUp:
    case Intent::Kind::NavLeft:
      moveFocus(-1);
      break;
    case Intent::Kind::NavDown:
    case Intent::Kind::NavRight:
      moveFocus(1);
      break;
    case Intent::Kind::NavPress:
      // EDIT timeline: PRESS on the cursor sets IN, then OUT; PRESS on an edge
      // sets that edge to the cursor and enters EDIT for the bars:beats nudge.
      if (focusId_ == QStringLiteral("timeline.in") ||
          focusId_ == QStringLiteral("timeline.out") ||
          focusId_ == QStringLiteral("timeline.cursor")) {
        if (focusId_ == QStringLiteral("timeline.in")) {
          timelineVm_->setInAtCursor();
          showTextPopup(QStringLiteral("REGION IN"),
                        timelineVm_->beatTextOf(timelineVm_->inBeats()));
          setPanelMode(QStringLiteral("edit"));
        } else if (focusId_ == QStringLiteral("timeline.out")) {
          timelineVm_->setOutAtCursor();
          showTextPopup(QStringLiteral("REGION OUT"),
                        timelineVm_->beatTextOf(timelineVm_->outBeats()));
          setPanelMode(QStringLiteral("edit"));
        } else if (!timelineVm_->selectionActive()) {
          timelineVm_->setInAtCursor();
          showTextPopup(QStringLiteral("REGION IN"),
                        timelineVm_->beatTextOf(timelineVm_->inBeats()));
          setPanelMode(QStringLiteral("edit"));
        } else if (timelineVm_->inBeats() == timelineVm_->outBeats()) {
          timelineVm_->setOutAtCursor();
          showTextPopup(QStringLiteral("REGION OUT"),
                        timelineVm_->beatTextOf(timelineVm_->outBeats()));
          setPanelMode(QStringLiteral("edit"));
        } else {
          timelineVm_->clearSelection();
          timelineVm_->setInAtCursor();
          showTextPopup(QStringLiteral("REGION IN"),
                        timelineVm_->beatTextOf(timelineVm_->inBeats()));
          setPanelMode(QStringLiteral("edit"));
        }
      } else if (panelMode_ == QStringLiteral("edit")) {
        setPanelMode(QStringLiteral("navigate"));
      } else if (focusId_.startsWith(QStringLiteral("arrange.clip."))) {
        // W6: PRESS on a clip opens its "..." actions; MOVE/TRIM in that menu
        // put the ring into EDIT on the same clip for the value knob.
        activateFocus();
      } else if (isEditableFocus(focusId_)) {
        setPanelMode(QStringLiteral("edit"));
      } else {
        activateFocus();
      }
      break;
    case Intent::Kind::EncoderSw:
      if (panelMode_ == QStringLiteral("edit")) {
        setPanelMode(QStringLiteral("navigate"));
      } else {
        mixerVm_->toggleArm(state_.selectedStrip.load());
      }
      break;
    default:
      break;
  }
}

bool AppController::performGridEdit(const Intent& intent) {
  const int dx = intent.kind == Intent::Kind::NavLeft
                     ? -1
                     : (intent.kind == Intent::Kind::NavRight ? 1 : 0);
  const int dy = intent.kind == Intent::Kind::NavUp
                     ? -1
                     : (intent.kind == Intent::Kind::NavDown ? 1 : 0);
  if (dx == 0 && dy == 0) return false;
  QString id = focusId_;
  if (id.startsWith(QStringLiteral("shift."))) id = id.mid(6);
  if (id == QStringLiteral("timeline.cursor")) {
    const double stepBeats = shiftHeld_ ? timeline::kCursorCoarseBeats
                                        : timeline::kStepFineBeats;
    timelineVm_->moveCursor(dx * stepBeats, dy);
    showTextPopup(QStringLiteral("TIMELINE CURSOR"),
                  timelineVm_->beatTextOf(timelineVm_->cursorBeats()));
    return true;
  }
  if (id == QStringLiteral("timeline.in") || id == QStringLiteral("timeline.out") ||
      id == QStringLiteral("edit.in") || id == QStringLiteral("edit.out")) {
    const double stepBeats = shiftHeld_ ? timeline::kStepCoarseBeats
                                        : timeline::kStepFineBeats;
    const bool isIn = id.endsWith(QStringLiteral(".in"));
    const QString text = isIn ? timelineVm_->nudgeIn(dx * stepBeats)
                              : timelineVm_->nudgeOut(dx * stepBeats);
    showTextPopup(isIn ? QStringLiteral("REGION IN") : QStringLiteral("REGION OUT"),
                  text);
    return true;
  }
  if (id == QStringLiteral("eq.band.select") || id.startsWith(QStringLiteral("eq.node."))) {
    eqVm_->nudgeSelected(dx, dy, shiftHeld_);
    return true;
  }
  if (id == QStringLiteral("xy.delay") || id == QStringLiteral("xy.reverb")) {
    // Grid 0 bends FX1, grid 1 bends FX2 of the selected track.
    xyVm_->nudge(id == QStringLiteral("xy.reverb") ? 1 : 0, dx, dy, shiftHeld_);
    return true;
  }
  return false;
}

void AppController::moveFocus(int delta) {
  if (focusIds_.empty()) return;
  focusIndex_ = moveIndex(focusIndex_, delta, static_cast<int>(focusIds_.size()));
  focusByScreen_[tab_] = focusIndex_;
  const QString id = focusIds_[static_cast<std::size_t>(focusIndex_)];
  if (id != focusId_) {
    focusId_ = id;
    emit focusChanged();
  }
}

void AppController::setPanelMode(const QString& mode) {
  if (mode == panelMode_) return;
  // D3: refuse the SHIFT face on a tab that has none.
  if (mode == QStringLiteral("shift") && !tabHasShiftFace(tab_)) {
    showNotice(NoticeWarn, QStringLiteral("NO SHIFT FACE ON THIS TAB"));
    return;
  }
  panelMode_ = mode;
  if (mode == QStringLiteral("shift")) {
    shiftFace_ = true;
    focusIndex_ = 0;
  }
  emit panelModeChanged();
  rebuildFocusIds();
}

void AppController::updateShiftState(const LinkStatus& status) {
  const bool held = status.shift;
  const auto now = nowMs();
  if (held && !shiftHeld_) {
    shiftHeld_ = true;
    shiftPressMs_ = now;
  }
  if (held && shiftHeld_ && !shiftFace_ && now - shiftPressMs_ >= 500) {
    setPanelMode(QStringLiteral("shift"));
  }
  if (!held && shiftHeld_) {
    shiftHeld_ = false;
    const bool longHold = now - shiftPressMs_ >= 500;
    if (shiftFace_) {
      // EXIT leaves the SHIFT face (including a shift-face EDIT).
      shiftFace_ = false;
      setPanelMode(QStringLiteral("navigate"));
      rebuildFocusIds();
    } else if (!longHold) {
      // EXIT short press: dismiss an open overlay, otherwise leave EDIT.
      if (configVm_->open()) {
        configCancel();
      } else if (confirmOpen_) {
        confirmCancel();
      } else if (keypadOpen_) {
        keypadCancel();
      } else if (tempoPopupOpen_) {
        closeTempoPopup();
      } else if (tabGridOpen_) {
        closeTabGrid();
      } else if (arrangeVm_ != nullptr && arrangeVm_->clipMenuOpen()) {
        arrangeVm_->closeClipMenu();
      } else if (arrangeVm_ != nullptr && arrangeVm_->lengthPopupOpen()) {
        arrangeVm_->closeLengthPopup();
      } else if (takeSheetOpen_) {
        closeTakeSheet();
      } else if (timelineVm_->editOpen()) {
        closeTakeEdit();
      } else if (panelMode_ == QStringLiteral("edit")) {
        setPanelMode(QStringLiteral("navigate"));
      }
    }
  }
}

bool AppController::isEditableFocus(const QString& focusId) const {
  QString id = focusId;
  if (id.startsWith(QStringLiteral("shift."))) id = id.mid(6);
  if (id.startsWith(QStringLiteral("config."))) {
    return id == QStringLiteral("config.gain") || id == QStringLiteral("config.bars") ||
           id.startsWith(QStringLiteral("config.pipeparam.")) ||
           id.startsWith(QStringLiteral("config.mode.")) ||
           id.startsWith(QStringLiteral("config.out.")) ||
           id.startsWith(QStringLiteral("config.phones.")) ||
           id.startsWith(QStringLiteral("config.cuestyle.")) ||
           id.startsWith(QStringLiteral("config.out34.")) ||
           id == QStringLiteral("config.thresh") || id == QStringLiteral("config.ratio") ||
           id == QStringLiteral("config.attack") || id == QStringLiteral("config.release") ||
           id == QStringLiteral("config.makeup") ||
           id == QStringLiteral("config.timeline.gain") ||
           id == QStringLiteral("config.timeline.fadein") ||
           id == QStringLiteral("config.timeline.fadeout");
  }
  if (id.startsWith(QStringLiteral("mixer."))) {
    return id.contains(QStringLiteral("gain")) || id.contains(QStringLiteral("pan")) ||
           id.contains(QStringLiteral("fx1")) || id.contains(QStringLiteral("fx2")) ||
           id.contains(QStringLiteral("cue")) || id.contains(QStringLiteral("out34")) ||
           id.startsWith(QStringLiteral("mixer.in.")) ||
           id == QStringLiteral("mixer.master");
  }
  if (id.startsWith(QStringLiteral("track."))) {
    return id == QStringLiteral("track.gain") || id == QStringLiteral("track.trim") ||
           id == QStringLiteral("track.pan") || id == QStringLiteral("track.fx1") ||
           id == QStringLiteral("track.fx2") || id == QStringLiteral("track.cue") ||
           id == QStringLiteral("track.out34") || id == QStringLiteral("track.input") ||
           id == QStringLiteral("track.output");
  }
  if (id.startsWith(QStringLiteral("loop."))) {
    return id == QStringLiteral("loop.start") || id == QStringLiteral("loop.end");
  }
  if (id.startsWith(QStringLiteral("routing.transpose."))) return true;
  if (id.startsWith(QStringLiteral("routing.pipes.param."))) return true;
  if (id.startsWith(QStringLiteral("fx."))) {
    if (id == QStringLiteral("fx.delay.time") || id == QStringLiteral("fx.delay.feedback") ||
        id == QStringLiteral("fx.delay.mix") || id == QStringLiteral("fx.delay.cutoff") ||
        id == QStringLiteral("fx.delay.width") ||
        id == QStringLiteral("fx.reverb.predelay") ||
        id == QStringLiteral("fx.reverb.decay") ||
        id == QStringLiteral("fx.reverb.damping") || id == QStringLiteral("fx.reverb.mix") ||
        id == QStringLiteral("fx.reverb.size")) {
      return true;
    }
    return id.endsWith(QStringLiteral(".return")) || id.contains(QStringLiteral(".param."));
  }
  if (id.startsWith(QStringLiteral("channel."))) {
    return id == QStringLiteral("channel.gain") || id == QStringLiteral("channel.trim") ||
           id == QStringLiteral("channel.pan") || id == QStringLiteral("channel.fx1") ||
           id == QStringLiteral("channel.fx2") || id == QStringLiteral("channel.cue") ||
           id == QStringLiteral("channel.out34");
  }
  if (id.startsWith(QStringLiteral("system."))) {
    return id == QStringLiteral("system.backend") || id == QStringLiteral("system.input") ||
           id == QStringLiteral("system.buffer") ||
           id == QStringLiteral("system.devout.main") ||
           id == QStringLiteral("system.devout.out34") ||
           id == QStringLiteral("system.devout.phones") ||
           id == QStringLiteral("system.click.level") ||
           id == QStringLiteral("system.click.bars");
  }
  if (id.startsWith(QStringLiteral("punch."))) {
    return id.endsWith(QStringLiteral(".measure"));
  }
  if (id.startsWith(QStringLiteral("monitor."))) {
    return id == QStringLiteral("monitor.clickgain") ||
           id == QStringLiteral("monitor.comp.thresh") ||
           id == QStringLiteral("monitor.comp.ratio") ||
           id == QStringLiteral("monitor.comp.attack") ||
           id == QStringLiteral("monitor.comp.release") ||
           id == QStringLiteral("monitor.comp.makeup") ||
           id.startsWith(QStringLiteral("monitor.clickmode")) ||
           id.startsWith(QStringLiteral("monitor.clickout")) ||
           id.startsWith(QStringLiteral("monitor.phones")) ||
           id.startsWith(QStringLiteral("monitor.cuestyle")) ||
           id.startsWith(QStringLiteral("monitor.out34")) ||
           id.startsWith(QStringLiteral("monitor.comp.mode"));
  }
  if (id == QStringLiteral("eq.subject.track")) return true;
  if (id == QStringLiteral("eq.band.gain") || id == QStringLiteral("eq.band.q") ||
      id == QStringLiteral("eq.band.freq")) {
    return true;
  }
  if (id == QStringLiteral("xy.delay") || id == QStringLiteral("xy.reverb")) return true;
  if (id == QStringLiteral("config.delay.feedback") ||
      id == QStringLiteral("config.delay.mix") ||
      id == QStringLiteral("config.delay.division") ||
      id == QStringLiteral("config.reverb.predelay") ||
      id == QStringLiteral("config.reverb.decay") ||
      id == QStringLiteral("config.reverb.damping")) {
    return true;
  }
  if (id == QStringLiteral("arrange.clip.gain") ||
      id == QStringLiteral("arrange.clip.fadein") ||
      id == QStringLiteral("arrange.clip.fadeout")) {
    return true;
  }
  if (id.startsWith(QStringLiteral("arrange.clip."))) {
    bool numeric = false;
    (void)id.mid(13).toInt(&numeric);
    return numeric;
  }
  if (id == QStringLiteral("tools.brightness")) return true;
  if (id == QStringLiteral("learn.target")) return true;
  if (id == QStringLiteral("timeline.cursor") || id == QStringLiteral("timeline.in") ||
      id == QStringLiteral("timeline.out")) {
    return true;
  }
  if (id == QStringLiteral("edit.in") || id == QStringLiteral("edit.out") ||
      id == QStringLiteral("edit.value") || id == QStringLiteral("edit.op.0") ||
      id == QStringLiteral("edit.op.1") || id == QStringLiteral("edit.op.2") ||
      id == QStringLiteral("edit.op.3") || id == QStringLiteral("edit.op.4")) {
    return true;
  }
  return false;
}

void AppController::activateFocus() {
  QString focusId = focusId_;
  if (focusId.startsWith(QStringLiteral("shift."))) focusId = focusId.mid(6);
  if (focusId.isEmpty()) return;
  const auto parts = focusId.split(QLatin1Char('.'));

  if (timelineVm_ != nullptr && timelineVm_->editOpen() && parts.size() >= 2 &&
      parts[0] == QStringLiteral("edit")) {
    const QString what = parts[1];
    const int index = parts.size() >= 3 ? parts[2].toInt() : 0;
    if (what == QStringLiteral("close")) {
      closeTakeEdit();
    } else if (what == QStringLiteral("zoom")) {
      timelineVm_->zoom(parts.size() >= 3 && parts[2] == QStringLiteral("in") ? 1 : -1);
    } else if (what == QStringLiteral("op")) {
      timelineVm_->selectEditOp(index);
      showTextPopup(QStringLiteral("REGION OP"), timelineVm_->editOpName());
    } else if (what == QStringLiteral("in")) {
      timelineVm_->setInAtCursor();
      showTextPopup(QStringLiteral("REGION IN"),
                    QString::fromStdString(timeline::beatTextCompact(
                        timelineVm_->inBeats())));
      setPanelMode(QStringLiteral("edit"));
    } else if (what == QStringLiteral("out")) {
      timelineVm_->setOutAtCursor();
      showTextPopup(QStringLiteral("REGION OUT"),
                    QString::fromStdString(timeline::beatTextCompact(
                        timelineVm_->outBeats())));
      setPanelMode(QStringLiteral("edit"));
    } else if (what == QStringLiteral("value")) {
      setPanelMode(QStringLiteral("edit"));
    } else if (what == QStringLiteral("apply")) {
      const QString error = timelineVm_->applyEditOp();
      if (error.isEmpty()) {
        showNotice(NoticeInfo, QStringLiteral("APPLIED %1 - %2 REGION(S)%3")
                                   .arg(timelineVm_->editOpName())
                                   .arg(timelineVm_->regionCount())
                                   .arg(timelineVm_->canUndo()
                                            ? QStringLiteral(" - UNDO available")
                                            : QString()));
      } else {
        showNotice(NoticeWarn, error);
        showTextPopup(QStringLiteral("NOT APPLIED"), error);
      }
    } else if (what == QStringLiteral("undo")) {
      if (timelineVm_->undo()) {
        showNotice(NoticeInfo, QStringLiteral("EDIT UNDO - %1 REGION(S)")
                                   .arg(timelineVm_->regionCount()));
      } else {
        showNotice(NoticeWarn, QStringLiteral("EDIT UNDO - nothing to undo"));
      }
    } else if (what == QStringLiteral("render")) {
      renderRegionToTrack();
    }
    return;
  }

  if (fxVm_ && fxVm_->open() && parts.size() >= 2 &&
      parts[0] == QStringLiteral("trackfx")) {
    if (parts[1] == QStringLiteral("recsource")) {
      openTrackMode(fxVm_->track());
      return;
    }
    // PRESS on a rack control gives the encoder its directions; the slot
    // selector is a real tap (it opens the list) so it is handled in QML.
    const QString what = parts[2];
    if (what == QStringLiteral("wet") || what == QStringLiteral("param")) {
      setPanelMode(QStringLiteral("edit"));
    }
    return;
  }

  if (configVm_ && configVm_->open() && parts.size() >= 2 &&
      parts[0] == QStringLiteral("config")) {
    const int index = parts.size() >= 3 ? parts[2].toInt() : 0;
    if (parts[1] == QStringLiteral("ok")) {
      configAccept();
    } else if (parts[1] == QStringLiteral("cancel")) {
      configCancel();
    } else if (parts[1] == QStringLiteral("pipeparam")) {
      configVm_->setPipeParamChoice(index);
    } else if (parts[1] == QStringLiteral("mode")) {
      if (configVm_->kind() == QStringLiteral("metronome")) {
        configVm_->setClickMode(index);
      } else {
        configVm_->setCompMode(index);
      }
    } else if (parts[1] == QStringLiteral("out")) {
      configVm_->setClickOutput(index);
    } else if (parts[1] == QStringLiteral("phones")) {
      configVm_->setPhonesSrc(index);
    } else if (parts[1] == QStringLiteral("cuestyle")) {
      configVm_->setCueStyle(index);
    } else if (parts[1] == QStringLiteral("out34")) {
      configVm_->setOut34Mode(index);
    } else if (parts[1] == QStringLiteral("trackmode")) {
      configVm_->setTrackMode(index);
    } else if (parts[1] == QStringLiteral("recsource")) {
      configVm_->setTrackRecSource(index);
    } else if (parts[1] == QStringLiteral("recmode")) {
      configVm_->setRecMode(index);
    } else if (parts[1] == QStringLiteral("takeaction")) {
      // TAKE row menu: PRESS runs the action the ring points at.
      configVm_->chooseTakeAction(index);
    } else if (parts[1] == QStringLiteral("trackfromtake")) {
      configVm_->chooseTrackFromTake(index);
    } else if (parts[1] == QStringLiteral("on")) {
      configVm_->setCompOn(!configVm_->compOn());
    } else if (parts[1] == QStringLiteral("send") && parts.size() >= 4) {
      const int source = parts[3] == QStringLiteral("post") ? 1 : 0;
      if (parts[2] == QStringLiteral("cue")) {
        configVm_->setSendCue(source);
      } else if (parts[2] == QStringLiteral("fx1")) {
        configVm_->setSendFx1(source);
      } else if (parts[2] == QStringLiteral("fx2")) {
        configVm_->setSendFx2(source);
      } else if (parts[2] == QStringLiteral("out34")) {
        configVm_->setSendOut34(source);
      } else if (parts[2] == QStringLiteral("rec")) {
        configVm_->setSendRec(source);
      }
    } else if (parts[1] == QStringLiteral("timeline") && parts.size() >= 4) {
      if (parts[2] == QStringLiteral("op")) {
        configVm_->setTimelineOp(parts[3].toInt());
      }
    } else if (parts[1] == QStringLiteral("eqtype") && parts.size() >= 3) {
      configVm_->setEqType(parts[2].toInt());
    } else if (parts[1] == QStringLiteral("delay") && parts.size() >= 3) {
      if (parts[2] == QStringLiteral("beat")) {
        configVm_->setDelayBeat(!configVm_->delayBeat());
      } else if (parts[2] == QStringLiteral("ping")) {
        configVm_->setDelayPing(!configVm_->delayPing());
      } else if (parts[2] == QStringLiteral("div.prev")) {
        configVm_->setDelayDivision(configVm_->delayDivision() - 1);
      } else if (parts[2] == QStringLiteral("div.next")) {
        configVm_->setDelayDivision(configVm_->delayDivision() + 1);
      }
    } else if (parts[1] == QStringLiteral("reverb") && parts.size() >= 3) {
      if (parts[2] == QStringLiteral("model")) {
        configVm_->setReverbModel(parts.size() >= 4 ? parts[3].toInt() : 0);
      }
    } else if (parts[1] == QStringLiteral("input") && parts.size() >= 3) {
      configVm_->setInputSourceIndex(parts[2].toInt());
    } else if (parts[1] == QStringLiteral("output") && parts.size() >= 3) {
      configVm_->setOutputIndex(parts[2].toInt());
    } else if (parts[1] == QStringLiteral("fxslot") && parts.size() >= 3) {
      configVm_->setFxSlotChoice(parts[2].toInt());
    } else if (parts[1] == QStringLiteral("pipesource") && parts.size() >= 3) {
      configVm_->setPipeSourceIndex(parts[2].toInt());
    } else if (parts[1] == QStringLiteral("pipedest") && parts.size() >= 3) {
      configVm_->setPipeDestIndex(parts[2].toInt());
    } else if (parts[1] == QStringLiteral("pipetype") && parts.size() >= 3) {
      configVm_->setPipeTypeIndex(parts[2].toInt());
    } else if (parts[1] == QStringLiteral("delaydivision") && parts.size() >= 3) {
      configVm_->setDelayDivision(parts[2].toInt());
    } else if (parts[1] == QStringLiteral("reverbmodel") && parts.size() >= 3) {
      configVm_->setReverbModel(parts[2].toInt());
    } else if (parts[1] == QStringLiteral("addtrack") && parts.size() >= 3) {
      if (parts[2] == QStringLiteral("name")) {
        requestAddTrackName();
      } else if (parts[2] == QStringLiteral("mode") && parts.size() >= 4) {
        configVm_->setAddTrackInputMode(parts[3].toInt());
      } else if (parts[2] == QStringLiteral("ch") && parts.size() >= 4) {
        configVm_->setAddTrackChannel(parts[3].toInt());
      } else if (parts[2] == QStringLiteral("out") && parts.size() >= 4) {
        configVm_->setAddTrackOutput(parts[3].toInt());
      }
    } else if (parts[1] == QStringLiteral("pipeparam") && parts.size() >= 3) {
      configVm_->setPipeParamChoice(parts[2].toInt());
    } else if (parts[1] == QStringLiteral("pipe") && parts.size() >= 4) {
      if (parts[2] == QStringLiteral("ch")) {
        configVm_->togglePipeChannel(parts[3].toInt());
      } else if (parts[2] == QStringLiteral("preset")) {
        configVm_->setPipeChannelPreset(parts[3].toInt());
      }
    } else if (parts[1] == QStringLiteral("delay") || parts[1] == QStringLiteral("reverb")) {
      setPanelMode(QStringLiteral("edit"));
    } else if (parts[1] == QStringLiteral("gain") || parts[1] == QStringLiteral("bars") ||
               parts[1] == QStringLiteral("thresh") || parts[1] == QStringLiteral("ratio") ||
               parts[1] == QStringLiteral("attack") || parts[1] == QStringLiteral("release") ||
               parts[1] == QStringLiteral("makeup") ||
               (parts[1] == QStringLiteral("timeline") &&
                (parts[2] == QStringLiteral("gain") ||
                 parts[2] == QStringLiteral("fadein") ||
                 parts[2] == QStringLiteral("fadeout")))) {
      setPanelMode(QStringLiteral("edit"));
    }
    return;
  }

  if (parts.size() == 2 && parts[0] == QStringLiteral("tab")) {
    setTab(parts[1].toInt());
    return;
  }
  if (focusId == QStringLiteral("tab.menu")) {
    // C3: the single TAB stop expands the 2x5 grid; the next PRESS picks.
    openTabGrid();
    return;
  }
  if (parts.size() == 2 && parts[0] == QStringLiteral("tabgrid")) {
    const int index = parts[1].toInt();
    closeTabGrid();
    setTab(index);
    return;
  }
  if (focusId == QStringLiteral("tempo.done")) {
    closeTempoPopup();
    return;
  }
  if (parts.size() >= 3 && parts[0] == QStringLiteral("take") &&
      parts[1] == QStringLiteral("sheet")) {
    if (parts[2] == QStringLiteral("live")) {
      chooseLiveInput();
    } else if (parts[2] == QStringLiteral("close")) {
      closeTakeSheet();
    } else if (parts[2] == QStringLiteral("row") && parts.size() >= 4) {
      chooseTakeRow(parts[3].toInt());
    }
    return;
  }
  if (focusId.startsWith(QStringLiteral("take.sheet."))) {
    // The take sheet traps the ring, so its rows must be pressable.
    if (focusId == QStringLiteral("take.sheet.close")) {
      closeTakeSheet();
    } else if (focusId == QStringLiteral("take.sheet.live")) {
      chooseLiveInput();
    } else {
      chooseTakeRow(focusId.mid(16).toInt());
    }
    return;
  }
  if (focusId == QStringLiteral("transport.recmode")) {
    openRecMode();
    return;
  }
  if (focusId == QStringLiteral("transport.click")) {
    setClickEnabled(!clickEnabled());
    return;
  }
  if (focusId == QStringLiteral("transport.song")) {
    toggleSongMode();
    return;
  }
  if (focusId == QStringLiteral("arrange.addsection") ||
      focusId == QStringLiteral("shift.arrange.addsection")) {
    arrangeVm_->addSection();
    return;
  }
  if (focusId == QStringLiteral("arrange.dupsection") ||
      focusId == QStringLiteral("shift.arrange.dupsection")) {
    arrangeVm_->duplicateSection();
    return;
  }
  if (focusId == QStringLiteral("arrange.loopsection") ||
      focusId == QStringLiteral("shift.arrange.loopsection")) {
    arrangeVm_->loopSection();
    return;
  }
  if (focusId == QStringLiteral("arrange.rename") ||
      focusId == QStringLiteral("shift.arrange.rename")) {
    requestArrangeSectionRename();
    return;
  }
  if (focusId == QStringLiteral("arrange.length") ||
      focusId == QStringLiteral("shift.arrange.length")) {
    arrangeVm_->openLengthPopup();
    return;
  }
  if (focusId == QStringLiteral("arrange.loop") ||
      focusId == QStringLiteral("shift.arrange.loop")) {
    arrangeVm_->setLoopOn(!arrangeVm_->loopOn());
    return;
  }
  if (focusId == QStringLiteral("arrange.zoomout") ||
      focusId == QStringLiteral("shift.arrange.zoomout")) {
    arrangeVm_->zoomOut();
    return;
  }
  if (focusId == QStringLiteral("arrange.zoomin") ||
      focusId == QStringLiteral("shift.arrange.zoomin")) {
    arrangeVm_->zoomIn();
    return;
  }
  if (focusId == QStringLiteral("arrange.render") ||
      focusId == QStringLiteral("shift.arrange.render")) {
    renderSong();
    return;
  }
  if (focusId == QStringLiteral("arrange.clipmenu")) {
    arrangeVm_->openClipMenu(arrangeVm_->selectedClip());
    return;
  }
  if (focusId == QStringLiteral("arrange.clip.close")) {
    arrangeVm_->closeClipMenu();
    return;
  }
  if (focusId == QStringLiteral("arrange.clip.pane.back")) {
    arrangeVm_->showClipMenuActions();
    return;
  }
  if (focusId.startsWith(QStringLiteral("arrange.clip.action."))) {
    const int action = focusId.mid(20).toInt();
    arrangeVm_->clipMenuAction(action);
    if ((action == 1 || action == 2) && arrangeVm_->hasClipSelection()) {
      // MOVE/TRIM arm the panel value knob on the clip: return the ring to the
      // clip block and enter EDIT, so the next detent is POS or LEN.
      focusId_ = QStringLiteral("arrange.clip.%1").arg(arrangeVm_->selectedClip());
      const auto found = std::find(focusIds_.begin(), focusIds_.end(), focusId_);
      if (found != focusIds_.end()) {
        focusIndex_ = static_cast<int>(std::distance(focusIds_.begin(), found));
        focusByScreen_[tab_] = focusIndex_;
      }
      setPanelMode(QStringLiteral("edit"));
      emit focusChanged();
      rebuildFocusIds();
      panelMode_ = QStringLiteral("edit");
      emit panelModeChanged();
    }
    return;
  }
  if (focusId == QStringLiteral("arrange.length.done")) {
    arrangeVm_->closeLengthPopup();
    return;
  }
  if (focusId == QStringLiteral("arrange.length.decbar")) {
    arrangeVm_->adjustLengthBars(-1);
    showTextPopup(QStringLiteral("LEN"), arrangeVm_->lengthText());
    return;
  }
  if (focusId == QStringLiteral("arrange.length.incbar")) {
    arrangeVm_->adjustLengthBars(1);
    showTextPopup(QStringLiteral("LEN"), arrangeVm_->lengthText());
    return;
  }
  if (focusId == QStringLiteral("arrange.length.decbeat")) {
    arrangeVm_->adjustLengthBeats(-1);
    showTextPopup(QStringLiteral("LEN"), arrangeVm_->lengthText());
    return;
  }
  if (focusId == QStringLiteral("arrange.length.incbeat")) {
    arrangeVm_->adjustLengthBeats(1);
    showTextPopup(QStringLiteral("LEN"), arrangeVm_->lengthText());
    return;
  }
  if (parts.size() >= 2 &&
      (parts[0] == QStringLiteral("arrange") ||
       (parts[0] == QStringLiteral("shift") && parts.size() >= 3 &&
        parts[1] == QStringLiteral("arrange")))) {
    const int base = parts[0] == QStringLiteral("shift") ? 2 : 1;
    const QString what = parts[base];
    bool numeric = false;
    const int argument = parts.size() > base + 1
                             ? parts[base + 1].toInt(&numeric)
                             : -1;
    if (!numeric) return;
    if (what == QStringLiteral("section")) {
      arrangeVm_->selectSection(argument);
      return;
    }
    if (what == QStringLiteral("clip")) {
      arrangeVm_->openClipMenu(argument);
      return;
    }
    if (what == QStringLiteral("clipmenu")) {
      arrangeVm_->clipMenuAction(argument);
      return;
    }
    if (what == QStringLiteral("grid")) {
      arrangeVm_->setGrid(argument);
      return;
    }
  }
  if (focusId == QStringLiteral("channel.rename")) {
    requestRenameTrack(mixerVm_->channelSlot());
    return;
  }
  if (focusId == QStringLiteral("mixer.add")) {
    requestAddTrack();
    return;
  }
  if (focusId == QStringLiteral("mixer.mode.0")) {
    mixerVm_->setSelectedMode(false);
    return;
  }
  if (focusId == QStringLiteral("mixer.mode.1")) {
    mixerVm_->setSelectedMode(true);
    return;
  }

  if (parts.size() >= 2 && parts[0] == QStringLiteral("track")) {
    if (parts[1] == QStringLiteral("select") && parts.size() >= 3) {
      mixerVm_->setTrackIndex(parts[2].toInt());
    } else if (parts[1] == QStringLiteral("arm")) {
      mixerVm_->toggleArm(mixerVm_->trackIndex());
    } else if (parts[1] == QStringLiteral("mute")) {
      mixerVm_->toggleMute(mixerVm_->trackIndex());
    } else if (parts[1] == QStringLiteral("solo")) {
      mixerVm_->toggleSolo(mixerVm_->trackIndex());
    } else if (parts[1] == QStringLiteral("mode")) {
      if (parts.size() >= 3) {
        mixerVm_->setSelectedMode(parts[2].toInt());
      } else {
        openTrackMode(mixerVm_->trackIndex());
      }
    } else if (parts[1] == QStringLiteral("input")) {
      if (parts.size() >= 3 && parts[2] == QStringLiteral("pair")) {
        mixerVm_->setTrackInPair(!mixerVm_->trackInPair());
        showTextPopup(QStringLiteral("INPUT SOURCE"), mixerVm_->trackInputText());
      } else {
        // Picker: EDIT turns the encoder into the source stepper.
        setPanelMode(QStringLiteral("edit"));
        showTextPopup(QStringLiteral("INPUT SOURCE"), mixerVm_->trackInputText());
      }
    } else if (parts[1] == QStringLiteral("output")) {
      setPanelMode(QStringLiteral("edit"));
      showTextPopup(QStringLiteral("OUTPUT DEST"), mixerVm_->trackOutputText());
    } else if (parts[1] == QStringLiteral("fx")) {
      // TRACK SHIFT > TRACK FX: the same popup the channel popout opens.
      openTrackFx(0);
    } else if (parts[1] == QStringLiteral("gain") || parts[1] == QStringLiteral("trim") ||
               parts[1] == QStringLiteral("pan") || parts[1] == QStringLiteral("fx1") ||
               parts[1] == QStringLiteral("fx2") || parts[1] == QStringLiteral("cue") ||
               parts[1] == QStringLiteral("out34")) {
      setPanelMode(QStringLiteral("edit"));
    }
    return;
  }

  if (parts.size() >= 2 && parts[0] == QStringLiteral("tools") &&
      parts[1] == QStringLiteral("page") && parts.size() >= 3) {
    setToolsPage(parts[2].toInt());
    return;
  }
  if (focusId == QStringLiteral("tools.brightness")) {
    setPanelMode(QStringLiteral("edit"));
    return;
  }

  if (parts.size() >= 2 && parts[0] == QStringLiteral("loop")) {
    if (parts[1] == QStringLiteral("enable")) {
      recorderVm_->setLoopEnabled(!recorderVm_->loopEnabled());
      showTextPopup(QStringLiteral("LOOP"),
                    recorderVm_->loopEnabled() ? recorderVm_->loopText()
                                               : QStringLiteral("OFF"));
    } else if (parts[1] == QStringLiteral("start") || parts[1] == QStringLiteral("end")) {
      setPanelMode(QStringLiteral("edit"));
    }
    return;
  }

  if (parts.size() >= 3 && parts[0] == QStringLiteral("clock")) {
    // Per-output clock/transport gates: index 2 is the output (0/1 TRS, 2 USB).
    const int output = parts[2].toInt();
    const bool clock = parts[1] == QStringLiteral("clock");
    if (!clock && parts[1] != QStringLiteral("transport")) return;
    if (output == 0) {
      clock ? sessionVm_->setTrs1Clock(!sessionVm_->trs1Clock())
            : sessionVm_->setTrs1Transport(!sessionVm_->trs1Transport());
    } else if (output == 1) {
      clock ? sessionVm_->setTrs2Clock(!sessionVm_->trs2Clock())
            : sessionVm_->setTrs2Transport(!sessionVm_->trs2Transport());
    } else if (output == 2) {
      clock ? sessionVm_->setUsbClock(!sessionVm_->usbClock())
            : sessionVm_->setUsbTransport(!sessionVm_->usbTransport());
    }
    const QStringList names = {QStringLiteral("TRS OUT1"), QStringLiteral("TRS OUT2"),
                               QStringLiteral("USB OUT")};
    showTextPopup(names.value(output) + QStringLiteral(" ") +
                      (clock ? QStringLiteral("CLOCK") : QStringLiteral("TRANSPORT")),
                  QStringLiteral("session v2 clock gate toggled"));
    return;
  }

  if (parts.size() >= 2 && parts[0] == QStringLiteral("midimon")) {
    if (parts[1] == QStringLiteral("clock") && parts.size() >= 3 &&
        parts[2] == QStringLiteral("filter")) {
      midiMonitorVm_->setHideClock(!midiMonitorVm_->hideClock());
      showTextPopup(QStringLiteral("MIDI MONITOR"),
                    midiMonitorVm_->hideClock() ? QStringLiteral("clock bytes hidden")
                                                : QStringLiteral("clock bytes shown"));
    } else if (parts[1] == QStringLiteral("reset")) {
      midiMonitorVm_->reset();
      showTextPopup(QStringLiteral("MIDI MONITOR"), QStringLiteral("counters cleared"));
    } else if (parts[1] == QStringLiteral("port") && parts.size() >= 3) {
      const int port = parts[2].toInt();
      showTextPopup(QStringLiteral("MIDI ") + midiPortName(port),
                    midiMonitorVm_->portSummary(port));
    }
    return;
  }

  if (parts.size() >= 3 && parts[0] == QStringLiteral("punch")) {
    const QString point = parts[1];
    if (parts[2] == QStringLiteral("enable")) {
      if (point == QStringLiteral("playfrom")) {
        sessionVm_->setPunchPlayFromEnabled(!sessionVm_->punchPlayFromEnabled());
      } else if (point == QStringLiteral("in")) {
        sessionVm_->setPunchInEnabled(!sessionVm_->punchInEnabled());
      } else if (point == QStringLiteral("out")) {
        sessionVm_->setPunchOutEnabled(!sessionVm_->punchOutEnabled());
      }
    } else if (parts[2] == QStringLiteral("measure")) {
      setPanelMode(QStringLiteral("edit"));
    }
    return;
  }

  if (parts.size() >= 2 && parts[0] == QStringLiteral("monitor")) {
    const int index = parts.size() >= 3 ? parts[2].toInt() : 0;
    if (parts[1] == QStringLiteral("summary") && parts.size() >= 3) {
      openConfig(parts[2]);
    } else if (parts[1] == QStringLiteral("sends")) {
      openConfig(QStringLiteral("sends"));
    } else if (parts[1] == QStringLiteral("clickmode")) {
      sessionVm_->setClickMode(index);
    } else if (parts[1] == QStringLiteral("clickout")) {
      sessionVm_->setClickOutput(index);
    } else if (parts[1] == QStringLiteral("phones")) {
      sessionVm_->setPhonesSrc(index);
    } else if (parts[1] == QStringLiteral("cuestyle")) {
      sessionVm_->setCueStyle(index);
    } else if (parts[1] == QStringLiteral("out34")) {
      sessionVm_->setOut34Mode(index);
    } else if (parts[1] == QStringLiteral("send") && parts.size() >= 4) {
      const int source = parts[3] == QStringLiteral("post") ? 1 : 0;
      if (parts[2] == QStringLiteral("cue")) {
        sessionVm_->setSendCue(source);
      } else if (parts[2] == QStringLiteral("fx1")) {
        sessionVm_->setSendFx1(source);
      } else if (parts[2] == QStringLiteral("fx2")) {
        sessionVm_->setSendFx2(source);
      } else if (parts[2] == QStringLiteral("out34")) {
        sessionVm_->setSendOut34(source);
      } else if (parts[2] == QStringLiteral("rec")) {
        sessionVm_->setSendRec(source);
      }
    } else if (parts[1] == QStringLiteral("comp")) {
      if (parts[2] == QStringLiteral("on")) {
        sessionVm_->setCompOn(!sessionVm_->compOn());
      } else if (parts[2] == QStringLiteral("mode")) {
        sessionVm_->setCompMode(parts.size() >= 4 ? parts[3].toInt() : 0);
      } else if (parts[2] == QStringLiteral("thresh") ||
                 parts[2] == QStringLiteral("ratio") ||
                 parts[2] == QStringLiteral("attack") ||
                 parts[2] == QStringLiteral("release") ||
                 parts[2] == QStringLiteral("makeup")) {
        setPanelMode(QStringLiteral("edit"));
      }
    } else if (parts[1] == QStringLiteral("clickgain")) {
      setPanelMode(QStringLiteral("edit"));
    }
    return;
  }

  if (parts.size() >= 2 && parts[0] == QStringLiteral("mixer") &&
      parts[1] == QStringLiteral("master")) {
    setPanelMode(QStringLiteral("edit"));
    return;
  }

  if (parts.size() >= 3 && parts[0] == QStringLiteral("mixer")) {
    const QString what = parts[1];
    if (what == QStringLiteral("bank")) {
      // The two page buttons step one page; the TRACKS label opens the grid.
      if (parts[2] == QStringLiteral("pages")) {
        openBankGrid();
      } else {
        mixerVm_->stepBank(parts[2].toInt() == 0 ? -1 : 1);
      }
      return;
    }
    if (what == QStringLiteral("expand")) {
      openChannel(parts[2].toInt());
      return;
    }
    if (what == QStringLiteral("master")) {
      setPanelMode(QStringLiteral("edit"));
      return;
    }
    const int strip = parts[2].toInt();
    selectStrip(strip);
    if (what == QStringLiteral("gain") || what == QStringLiteral("pan") ||
        what == QStringLiteral("fx1") || what == QStringLiteral("fx2") ||
        what == QStringLiteral("cue") || what == QStringLiteral("out34")) {
      setPanelMode(QStringLiteral("edit"));
    } else if (what == QStringLiteral("mute")) {
      mixerVm_->toggleMute(strip);
    } else if (what == QStringLiteral("solo")) {
      mixerVm_->toggleSolo(strip);
    } else if (what == QStringLiteral("arm")) {
      mixerVm_->toggleArm(strip);
    } else if (what == QStringLiteral("trackmode")) {
      // One tap cycles the mode (W1); the word says what the strip does now.
      mixerVm_->setMode(strip, nextTrackMode(mixerVm_->modeAt(strip)));
      showTextPopup(QStringLiteral("TRACK MODE"), mixerVm_->modeWordAt(strip));
    } else if (what == QStringLiteral("in")) {
      // Tap-to-open selector list: the strip's input source opens the same
      // popup the touch surface uses, so there is no cycle-stepper to learn.
      openInputSource(strip);
    }
    return;
  }

  if (parts.size() >= 2 && parts[0] == QStringLiteral("channel")) {
    // Full channel popout: the TRACK property family already addresses the
    // popout's slot (openChannel points it there).
    const QString what = parts[1];
    if (what == QStringLiteral("close")) {
      closeChannel();
    } else if (what == QStringLiteral("remove")) {
      requestRemoveTrack(mixerVm_->trackIndex());
    } else if (what == QStringLiteral("input")) {
      openInputSource(mixerVm_->trackIndex());
    } else if (what == QStringLiteral("output")) {
      openOutputDest(mixerVm_->trackIndex());
    } else if (what == QStringLiteral("fx")) {
      openTrackFx(0);
    } else if (what == QStringLiteral("mute")) {
      mixerVm_->setTrackMute(!mixerVm_->trackMute());
    } else if (what == QStringLiteral("solo")) {
      mixerVm_->setTrackSolo(!mixerVm_->trackSolo());
    } else if (what == QStringLiteral("arm")) {
      mixerVm_->setTrackArm(!mixerVm_->trackArm());
    } else if (what == QStringLiteral("mode")) {
      mixerVm_->setSelectedMode(nextTrackMode(mixerVm_->selectedMode()));
      showTextPopup(QStringLiteral("TRACK MODE"), mixerVm_->selectedModeWord());
    } else if (what == QStringLiteral("gain") || what == QStringLiteral("trim") ||
               what == QStringLiteral("pan") || what == QStringLiteral("fx1") ||
               what == QStringLiteral("fx2") || what == QStringLiteral("cue") ||
               what == QStringLiteral("out34")) {
      setPanelMode(QStringLiteral("edit"));
    }
    return;
  }

  if (parts.size() >= 2 && parts[0] == QStringLiteral("routing")) {
    if (parts[1] == QStringLiteral("page") && parts.size() >= 3) {
      routingVm_->setShiftPage(parts[2].toInt());
      rebuildFocusIds();
    } else if (parts[1] == QStringLiteral("shiftpipes")) {
      // Touch affordance on the primary face: the MIDI FX pipe chain lives on
      // the SHIFT face, whose touch gesture is the Routing tab long press.
      showShiftFace(true);
      routingVm_->setShiftPage(1);
      rebuildFocusIds();
      showTextPopup(QStringLiteral("SHIFT PIPES"),
                    QStringLiteral("MIDI FX chains - Routing SHIFT > PIPES"));
    } else if (parts[1] == QStringLiteral("filter") && parts.size() >= 3) {
      const int delta = parts[2] == QStringLiteral("prev") ? -1 : 1;
      routingVm_->setFilterPage(routingVm_->filterPage() + delta);
      showTextPopup(QStringLiteral("FILTER SOURCES"),
                    QStringLiteral("page %1 / %2")
                        .arg(routingVm_->filterPage() + 1)
                        .arg(routingVm_->filterPageCount()));
      rebuildFocusIds();
    } else if (parts[1] == QStringLiteral("pipes") && parts.size() >= 3) {
      const QString what = parts[2];
      if (what == QStringLiteral("src") && parts.size() >= 4 &&
          parts[3] == QStringLiteral("name")) {
        openConfig(QStringLiteral("pipesource"));
      } else if (what == QStringLiteral("dst") && parts.size() >= 4 &&
                 parts[3] == QStringLiteral("name")) {
        openConfig(QStringLiteral("pipedest"));
      } else if (what == QStringLiteral("scope")) {
        openConfig(QStringLiteral("pipechannels"));
      } else if (what == QStringLiteral("add") && parts.size() >= 4 &&
                 parts[3] == QStringLiteral("type")) {
        openConfig(QStringLiteral("pipetype"));
      } else if (what == QStringLiteral("chain")) {
        openConfig(QStringLiteral("chainmenu"));
      } else if (what == QStringLiteral("add")) {
        routingVm_->addPipe();
        showTextPopup(QStringLiteral("PIPE ADDED"), routingVm_->addTypeName());
      } else if (what == QStringLiteral("select") && parts.size() >= 4) {
        routingVm_->selectPipe(parts[3].toInt());
      } else if (what == QStringLiteral("bypass") && parts.size() >= 4) {
        routingVm_->selectPipe(parts[3].toInt());
        routingVm_->toggleSelectedPipeBypass();
      } else if (what == QStringLiteral("up") && parts.size() >= 4) {
        routingVm_->selectPipe(parts[3].toInt());
        routingVm_->moveSelectedPipe(-1);
      } else if (what == QStringLiteral("down") && parts.size() >= 4) {
        routingVm_->selectPipe(parts[3].toInt());
        routingVm_->moveSelectedPipe(1);
      } else if (what == QStringLiteral("remove") && parts.size() >= 4) {
        routingVm_->selectPipe(parts[3].toInt());
        routingVm_->removeSelectedPipe();
      } else if (what == QStringLiteral("param")) {
        setPanelMode(QStringLiteral("edit"));
      }
      rebuildFocusIds();
    } else if (parts[1] == QStringLiteral("reset")) {
      // A12: a patchbay can be ten minutes of routing. The panel asks once;
      // touch holds the button for 800 ms instead (RoutingScreen HoldButton).
      requestConfirm(
          QStringLiteral("RESET ROUTING?"),
          QStringLiteral("Every wire and channel filter returns to "
                         "TRS IN 1/2 -> USB OUT."),
          QStringLiteral("RESET"), QStringLiteral("routing-reset"), -1);
    } else if (parts[1] == QStringLiteral("node") && parts.size() >= 4) {
      // The focus id carries the stable midi::Port id (0..5 fixed, 6..31 per
      // device), so the panel ring keeps its stop when the device list changes.
      const int port = parts[3].toInt();
      if (parts[2] == QStringLiteral("in")) {
        routingVm_->selectInputPort(port);
      } else {
        routingVm_->selectOutputPort(port);
      }
    } else if (parts[1] == QStringLiteral("mask") && parts.size() >= 4) {
      const int source = parts[2].toInt();
      const int channel = parts[3].toInt();
      const auto masks = routingVm_->channelMasks();
      const int mask = masks.value(source).toInt();
      routingVm_->setMaskBit(source, channel, (mask & (1 << channel)) == 0);
    } else if (parts[1] == QStringLiteral("transpose")) {
      setPanelMode(QStringLiteral("edit"));
    }
    return;
  }

  if (parts.size() >= 2 && parts[0] == QStringLiteral("recorder")) {
    if (parts[1] == QStringLiteral("track") && parts.size() >= 3) {
      recorderVm_->setSelectedTrack(parts[2].toInt());
    } else if (parts[1] == QStringLiteral("arm")) {
      mixerVm_->toggleArm(recorderVm_->selectedTrack());
    } else if (parts[1] == QStringLiteral("start")) {
      recorderVm_->startStop();
    } else if (parts[1] == QStringLiteral("import")) {
      requestImportTake();
    } else if (parts[1] == QStringLiteral("live")) {
      recorderVm_->clearActive();
    } else if (parts[1] == QStringLiteral("activate")) {
      recorderVm_->activateSelected();
    } else if (parts[1] == QStringLiteral("rename")) {
      requestRenameTake();
    } else if (parts[1] == QStringLiteral("delete")) {
      requestDeleteTake();
    } else if (parts[1] == QStringLiteral("take") && parts.size() >= 3) {
      recorderVm_->selectRow(parts[2].toInt());
    }
    return;
  }

  if (parts.size() >= 2 && parts[0] == QStringLiteral("system")) {
    if (parts[1] == QStringLiteral("page") && parts.size() >= 3) {
      setSystemShiftPage(parts[2].toInt());
    } else if (parts[1] == QStringLiteral("panic")) {
      shiftAction(1);
    } else if (parts[1] == QStringLiteral("apply")) {
      systemVm_->apply();
    } else if (parts[1] == QStringLiteral("refresh")) {
      systemVm_->refreshDevices();
    } else if (parts[1] == QStringLiteral("backend") ||
               parts[1] == QStringLiteral("input") ||
               parts[1] == QStringLiteral("buffer")) {
      // Pickers: EDIT turns the encoder into the list stepper.
      setPanelMode(QStringLiteral("edit"));
    } else if (parts[1] == QStringLiteral("devout") && parts.size() >= 3) {
      // Device output pair picker (Main 1&2 / OUT3&4 / phones).
      setPanelMode(QStringLiteral("edit"));
      const int pair = parts[2] == QStringLiteral("out34") ? sessionVm_->deviceOutOut34()
                      : (parts[2] == QStringLiteral("phones")
                             ? sessionVm_->deviceOutPhones()
                             : sessionVm_->deviceOutMain());
      showTextPopup(QStringLiteral("DEVICE OUT"),
                    sessionVm_->deviceOutPairLabels().value(pair));
    } else if (parts[1] == QStringLiteral("click")) {
      setClickEnabled(!clickEnabled());
    } else if (parts[1] == QStringLiteral("learn")) {
      const QString what = parts[2];
      if (what == QStringLiteral("arm")) {
        if (learnVm_->capturing()) {
          learnVm_->cancelCapture();
        } else {
          learnVm_->startCapture();
        }
      } else if (what == QStringLiteral("clear")) {
        learnVm_->clearAll();
      } else if (what == QStringLiteral("binding") && parts.size() >= 4) {
        learnVm_->removeBinding(parts[3].toInt());
      }
    } else if (parts[1] == QStringLiteral("backend") ||
               parts[1] == QStringLiteral("input") ||
               parts[1] == QStringLiteral("buffer")) {
      setPanelMode(QStringLiteral("edit"));
    }
    return;
  }

  if (parts.size() >= 2 && parts[0] == QStringLiteral("projects")) {
    if (parts[1] == QStringLiteral("new")) {
      projectsVm_->newProject();
    } else if (parts[1] == QStringLiteral("save")) {
      projectsVm_->saveProject();
    } else if (parts[1] == QStringLiteral("rename")) {
      requestRenameProject();
    } else if (parts[1] == QStringLiteral("delete")) {
      const int row = projectsVm_->selectedRow();
      if (row >= 0) requestDeleteProject(row);
    } else if (parts[1] == QStringLiteral("row") && parts.size() >= 3) {
      requestLoadProject(parts[2].toInt());
    } else if (parts[1] == QStringLiteral("template") && parts.size() >= 3) {
      if (parts[2] == QStringLiteral("new")) {
        projectsVm_->newTemplate();
      } else if (parts[2] == QStringLiteral("save")) {
        projectsVm_->saveTemplate();
      } else if (parts[2] == QStringLiteral("clear")) {
        projectsVm_->clearTemplate();
      }
    }
    return;
  }

  if (parts.size() >= 2 && parts[0] == QStringLiteral("eq")) {
    if (parts[1] == QStringLiteral("subject")) {
      if (parts.size() >= 3 && parts[2] == QStringLiteral("main")) {
        eqVm_->selectMain();
      } else {
        // Track stepper: EDIT hands the encoder to the subject list.
        setPanelMode(QStringLiteral("edit"));
      }
    } else if (parts[1] == QStringLiteral("band") && parts.size() >= 3) {
      if (parts[2] == QStringLiteral("select")) {
        // The panel A-button: one PRESS steps to the next band.
        eqVm_->stepBand(1);
      } else if (parts[2] == QStringLiteral("type") && parts.size() >= 4) {
        eqVm_->selectType(parts[3].toInt());
      } else if (parts[2] == QStringLiteral("type")) {
        openConfig(QStringLiteral("eqtype"));
      } else if (parts[2] == QStringLiteral("gain") ||
                 parts[2] == QStringLiteral("q") ||
                 parts[2] == QStringLiteral("freq")) {
        setPanelMode(QStringLiteral("edit"));
      }
    } else if (parts[1] == QStringLiteral("node") && parts.size() >= 3) {
      // PRESS on a node selects that band and gives the directions to it.
      eqVm_->selectBand(parts[2].toInt());
      setPanelMode(QStringLiteral("edit"));
    } else if (parts[1] == QStringLiteral("fullscreen")) {
      setFullscreen(!fullscreen_);
      showTextPopup(QStringLiteral("EQ VIEW"),
                    fullscreen_ ? QStringLiteral("FULLSCREEN - bridge hidden")
                                : QStringLiteral("PANEL - bridge shown"));
    }
    return;
  }

  if (parts.size() >= 2 && parts[0] == QStringLiteral("xy")) {
    if (parts[1] == QStringLiteral("freeze")) {
      xyVm_->toggleFreeze();
    } else if (parts[1] == QStringLiteral("delay") ||
               parts[1] == QStringLiteral("reverb")) {
      // Grid ids keep their historical names; the grid is the rack slot.
      const int slot = parts[1] == QStringLiteral("reverb") ? 1 : 0;
      if (parts.size() >= 3 && parts[2] == QStringLiteral("params")) {
        if (slot == 1 && xyVm_->frozen()) {
          showTextPopup(QStringLiteral("FX2"),
                        QStringLiteral("FREEZE locks the reverb params"));
        } else {
          openFxSlotParams(slot);
        }
      } else {
        setPanelMode(QStringLiteral("edit"));
      }
    }
    return;
  }

  if (parts.size() >= 2 && parts[0] == QStringLiteral("timeline")) {
    if (parts[1] == QStringLiteral("view") && parts.size() >= 3) {
      timelineVm_->scrollView(parts[2] == QStringLiteral("prev") ? -1 : 1);
    } else if (parts[1] == QStringLiteral("zoom") && parts.size() >= 3) {
      timelineVm_->zoom(parts[2] == QStringLiteral("in") ? 1 : -1);
    } else if (parts[1] == QStringLiteral("cursor")) {
      setPanelMode(QStringLiteral("edit"));
      showTextPopup(QStringLiteral("TIMELINE CURSOR"),
                    timelineVm_->beatTextOf(timelineVm_->cursorBeats()));
    } else if (parts[1] == QStringLiteral("in")) {
      timelineVm_->setInAtCursor();
      setPanelMode(QStringLiteral("edit"));
      showTextPopup(QStringLiteral("REGION IN"),
                    timelineVm_->beatTextOf(timelineVm_->inBeats()));
    } else if (parts[1] == QStringLiteral("out")) {
      timelineVm_->setOutAtCursor();
      setPanelMode(QStringLiteral("edit"));
      showTextPopup(QStringLiteral("REGION OUT"),
                    timelineVm_->beatTextOf(timelineVm_->outBeats()));
    }
    return;
  }

  if (parts.size() >= 2 && parts[0] == QStringLiteral("learn")) {
    if (parts[1] == QStringLiteral("target")) {
      setPanelMode(QStringLiteral("edit"));
    } else if (parts[1] == QStringLiteral("arm")) {
      // A9: PRESS on ARM CAPTURE arms the target capture (no pre-picked id).
      armLearnTargetCapture();
    } else if (parts[1] == QStringLiteral("clear")) {
      learnVm_->clearAll();
    } else if (parts[1] == QStringLiteral("binding") && parts.size() >= 3) {
      learnVm_->removeBinding(parts[2].toInt());
    }
  }
}

void AppController::adjustFocused(int detents, bool fine) {
  QString focusId = focusId_;
  if (focusId.startsWith(QStringLiteral("shift."))) focusId = focusId.mid(6);
  if (focusId.isEmpty() || detents == 0) return;
  const auto parts = focusId.split(QLatin1Char('.'));
  const double step = static_cast<double>(detents);

  if (timelineVm_ != nullptr && timelineVm_->editOpen() && parts.size() >= 2 &&
      parts[0] == QStringLiteral("edit")) {
    // Panel parity in the popup: the knob moves whatever the ring stopped on -
    // an edge handle, the focused op, the op's value, or the zoom level.
    const QString what = parts[1];
    if (what == QStringLiteral("in") || what == QStringLiteral("out")) {
      const double stepBeats = step * (shiftHeld_ ? timeline::kStepCoarseBeats
                                                  : timeline::kStepFineBeats);
      if (what == QStringLiteral("in")) {
        (void)timelineVm_->nudgeIn(stepBeats);
      } else {
        (void)timelineVm_->nudgeOut(stepBeats);
      }
      showTextPopup(what == QStringLiteral("in") ? QStringLiteral("REGION IN")
                                                 : QStringLiteral("REGION OUT"),
                    QString::fromStdString(timeline::beatTextCompact(
                        what == QStringLiteral("in") ? timelineVm_->inBeats()
                                                     : timelineVm_->outBeats())));
    } else if (what == QStringLiteral("value")) {
      const auto op = static_cast<timeline::EditOp>(timelineVm_->editOp());
      if (op == timeline::EditOp::Gain) {
        const double next =
            timelineVm_->previewGainDb() + step * (fine ? 0.1 : 0.5);
        timelineVm_->setEditGainDb(next);
        showValuePopup(QStringLiteral("REGION GAIN"), QStringLiteral("db"),
                       timelineVm_->previewGainDb());
      } else if (op == timeline::EditOp::FadeIn) {
        const double next =
            timelineVm_->previewFadeInBeats() + step * (fine ? 0.1 : 0.25);
        timelineVm_->setEditFadeIn(next);
        showValuePopup(QStringLiteral("FADE IN"), QStringLiteral("generic"),
                       timelineVm_->previewFadeInBeats());
      } else if (op == timeline::EditOp::FadeOut) {
        const double next =
            timelineVm_->previewFadeOutBeats() + step * (fine ? 0.1 : 0.25);
        timelineVm_->setEditFadeOut(next);
        showValuePopup(QStringLiteral("FADE OUT"), QStringLiteral("generic"),
                       timelineVm_->previewFadeOutBeats());
      } else {
        showTextPopup(QStringLiteral("REGION OP"),
                      timelineVm_->editOpName() + QStringLiteral(" - APPLY runs it"));
      }
    } else if (what == QStringLiteral("op")) {
      timelineVm_->selectEditOp(moveIndex(timelineVm_->editOp(),
                                         detents > 0 ? 1 : -1,
                                         timeline::kEditOpCount));
      showTextPopup(QStringLiteral("REGION OP"), timelineVm_->editOpName());
    } else if (what == QStringLiteral("zoom")) {
      timelineVm_->zoom(detents > 0 ? 1 : -1);
      showTextPopup(QStringLiteral("ZOOM"),
                    QString::number(timelineVm_->viewBeats(), 'f', 2) +
                        QStringLiteral(" beats across"));
    }
    return;
  }

  if (focusId.startsWith(QStringLiteral("arrange.clip."))) {
    if (focusId == QStringLiteral("arrange.clip.gain")) {
      arrangeVm_->setClipGainDb(arrangeVm_->clipGainDb() +
                                step * (fine ? 0.1 : 0.5));
      showValuePopup(QStringLiteral("CLIP GAIN"), QStringLiteral("db"),
                     arrangeVm_->clipGainDb());
      return;
    }
    if (focusId == QStringLiteral("arrange.clip.fadein")) {
      arrangeVm_->setClipFadeIn(arrangeVm_->clipFadeInBeats() +
                                step * (fine ? 0.05 : 0.25));
      showTextPopup(QStringLiteral("FADE IN"),
                    QString::number(arrangeVm_->clipFadeInBeats(), 'f', 2) +
                        QStringLiteral(" beats"));
      return;
    }
    if (focusId == QStringLiteral("arrange.clip.fadeout")) {
      arrangeVm_->setClipFadeOut(arrangeVm_->clipFadeOutBeats() +
                                 step * (fine ? 0.05 : 0.25));
      showTextPopup(QStringLiteral("FADE OUT"),
                    QString::number(arrangeVm_->clipFadeOutBeats(), 'f', 2) +
                        QStringLiteral(" beats"));
      return;
    }
    bool numeric = false;
    const int clip = focusId.mid(13).toInt(&numeric);
    if (numeric && arrangeVm_->clipCount() > clip) {
      arrangeVm_->selectClip(clip);
      if (shiftHeld_) {
        arrangeVm_->nudgeClipLength(detents);
        const auto rows = arrangeVm_->clips();
        const double beats =
            rows.at(clip).toMap().value(QStringLiteral("lengthBeats")).toDouble();
        showTextPopup(QStringLiteral("LEN"),
                      QString::number(beats / 4.0, 'f', 1) +
                          QStringLiteral(" bars"));
      } else {
        arrangeVm_->nudgeClipPosition(detents);
        const auto rows = arrangeVm_->clips();
        const double beats =
            rows.at(clip).toMap().value(QStringLiteral("startBeats")).toDouble();
        showTextPopup(QStringLiteral("POS"),
                      QString::number(beats / 4.0, 'f', 1) +
                          QStringLiteral(" bars"));
      }
      return;
    }
  }

  if (parts.size() >= 3 && parts[0] == QStringLiteral("mixer") &&
      parts[1] == QStringLiteral("bank") && parts[2] == QStringLiteral("pages")) {
    // A11: the panel focuses the label and the encoder still walks pages, so
    // a long turn stays a valid way to reach page 16 without the grid.
    mixerVm_->stepBank(detents > 0 ? 1 : -1);
    showTextPopup(QStringLiteral("MIXER PAGE"), mixerVm_->bankText());
    return;
  }

  if (fxVm_ && fxVm_->open() && parts.size() >= 2 &&
      parts[0] == QStringLiteral("trackfx")) {
    if (parts[1] == QStringLiteral("recsource")) {
      openTrackMode(fxVm_->track());
      return;
    }
    // PRESS on a rack control gives the encoder its directions; the slot
    // selector is a real tap (it opens the list) so it is handled in QML.
    const QString what = parts[2];
    if (what == QStringLiteral("wet") || what == QStringLiteral("param")) {
      setPanelMode(QStringLiteral("edit"));
    }
    return;
  }

  if (fxVm_ && fxVm_->open() && parts.size() >= 3 &&
      parts[0] == QStringLiteral("trackfx")) {
    // The encoder turns a rack control the ring stopped on: the wet chip, or one
    // card of the slot's parameter grid (its own range and step decide the
    // increment).
    const int slot = parts[1] == QStringLiteral("fx2") ? 1 : 0;
    const int track = mixerVm_->selectedStrip();
    if (parts[2] == QStringLiteral("wet")) {
      const double next = std::clamp(mixerVm_->fxWetAt(track, slot) +
                                         step * (fine ? 0.01 : 0.05),
                                     0.0, 1.0);
      mixerVm_->setFxWetAt(track, slot, next);
      showValuePopup(QStringLiteral("FX%1 WET").arg(slot + 1),
                     QStringLiteral("percent"), next);
    } else if (parts[2] == QStringLiteral("param") && parts.size() >= 4) {
      const int param = parts[3].toInt();
      const auto card = paramCard(track, slot, param);
      if (!card.isEmpty()) {
        const double value = card.value(QStringLiteral("value")).toDouble();
        const double minimum = card.value(QStringLiteral("minimum")).toDouble();
        const double maximum = card.value(QStringLiteral("maximum")).toDouble();
        const bool stepped = card.value(QStringLiteral("stepped")).toBool();
        const double span = maximum - minimum;
        const double delta = stepped ? step : step * (fine ? span / 100.0 : span / 20.0);
        const double next = std::clamp(value + delta, minimum, maximum);
        mixerVm_->setFxParamAt(track, slot, param, next);
        showValuePopup(QStringLiteral("FX%1 ").arg(slot + 1) +
                           card.value(QStringLiteral("name")).toString(),
                       QStringLiteral("generic"), next);
      }
    }
    return;
  }

  if (configVm_ && configVm_->open() && parts.size() >= 2 &&
      parts[0] == QStringLiteral("config")) {
    if (parts[1] == QStringLiteral("gain")) {
      configVm_->setClickGainDb(configVm_->clickGainDb() + step * (fine ? 0.1 : 0.5));
      showValuePopup(QStringLiteral("CLICK GAIN"), QStringLiteral("db"),
                     configVm_->clickGainDb());
    } else if (parts[1] == QStringLiteral("bars")) {
      configVm_->setClickBars(configVm_->clickBars() + detents);
      showValuePopup(QStringLiteral("CLICK BARS"), QStringLiteral("int"),
                     configVm_->clickBars());
    } else if (parts[1] == QStringLiteral("pipeparam")) {
      configVm_->setPipeParamChoice(configVm_->pipeParamChoice() + detents);
    } else if (parts[1] == QStringLiteral("mode")) {
      if (configVm_->kind() == QStringLiteral("metronome")) {
        configVm_->setClickMode(moveIndex(configVm_->clickMode(), detents > 0 ? 1 : -1, 3));
      } else {
        configVm_->setCompMode(moveIndex(configVm_->compMode(), detents > 0 ? 1 : -1, 2));
      }
    } else if (parts[1] == QStringLiteral("out")) {
      configVm_->setClickOutput(moveIndex(configVm_->clickOutput(), detents > 0 ? 1 : -1, 3));
    } else if (parts[1] == QStringLiteral("phones")) {
      configVm_->setPhonesSrc(moveIndex(configVm_->phonesSrc(), detents > 0 ? 1 : -1, 2));
    } else if (parts[1] == QStringLiteral("cuestyle")) {
      configVm_->setCueStyle(moveIndex(configVm_->cueStyle(), detents > 0 ? 1 : -1, 2));
    } else if (parts[1] == QStringLiteral("out34")) {
      configVm_->setOut34Mode(moveIndex(configVm_->out34Mode(), detents > 0 ? 1 : -1, 2));
    } else if (parts[1] == QStringLiteral("thresh")) {
      configVm_->setCompThreshDb(configVm_->compThreshDb() + step * (fine ? 0.1 : 0.5));
      showValuePopup(QStringLiteral("COMP THRESH"), QStringLiteral("db"),
                     configVm_->compThreshDb());
    } else if (parts[1] == QStringLiteral("ratio")) {
      configVm_->setCompRatio(configVm_->compRatio() + step * (fine ? 0.1 : 0.5));
      showValuePopup(QStringLiteral("COMP RATIO"), QStringLiteral("ratio"),
                     configVm_->compRatio());
    } else if (parts[1] == QStringLiteral("attack")) {
      configVm_->setCompAttackMs(configVm_->compAttackMs() + step * (fine ? 0.5 : 2.0));
      showValuePopup(QStringLiteral("COMP ATTACK"), QStringLiteral("ms"),
                     configVm_->compAttackMs());
    } else if (parts[1] == QStringLiteral("release")) {
      configVm_->setCompReleaseMs(configVm_->compReleaseMs() + step * (fine ? 1.0 : 5.0));
      showValuePopup(QStringLiteral("COMP RELEASE"), QStringLiteral("ms"),
                     configVm_->compReleaseMs());
    } else if (parts[1] == QStringLiteral("makeup")) {
      configVm_->setCompMakeupDb(configVm_->compMakeupDb() + step * (fine ? 0.1 : 0.5));
      showValuePopup(QStringLiteral("COMP MAKEUP"), QStringLiteral("db"),
                     configVm_->compMakeupDb());
    } else if (parts[1] == QStringLiteral("timeline") && parts.size() >= 3) {
      if (parts[2] == QStringLiteral("gain")) {
        configVm_->setTimelineGainDb(configVm_->timelineGainDb() +
                                     step * (fine ? 0.1 : 0.5));
        showValuePopup(QStringLiteral("REGION GAIN"), QStringLiteral("db"),
                       configVm_->timelineGainDb());
      } else if (parts[2] == QStringLiteral("fadein")) {
        configVm_->setTimelineFadeInBeats(configVm_->timelineFadeInBeats() +
                                          step * (fine ? 0.1 : 0.25));
        showTextPopup(QStringLiteral("FADE IN"),
                      QString::number(configVm_->timelineFadeInBeats(), 'f', 2) +
                          QStringLiteral(" beats"));
      } else if (parts[2] == QStringLiteral("fadeout")) {
        configVm_->setTimelineFadeOutBeats(configVm_->timelineFadeOutBeats() +
                                           step * (fine ? 0.1 : 0.25));
        showTextPopup(QStringLiteral("FADE OUT"),
                      QString::number(configVm_->timelineFadeOutBeats(), 'f', 2) +
                          QStringLiteral(" beats"));
      }
    } else if (parts[1] == QStringLiteral("delay") && parts.size() >= 3) {
      if (parts[2] == QStringLiteral("feedback")) {
        configVm_->setDelayFeedback(configVm_->delayFeedback() +
                                    step * (fine ? 0.005 : 0.02));
        showValuePopup(QStringLiteral("DELAY FEEDBACK"), QStringLiteral("percent"),
                       configVm_->delayFeedback());
      } else if (parts[2] == QStringLiteral("mix")) {
        configVm_->setDelayMix(configVm_->delayMix() + step * (fine ? 0.005 : 0.02));
        showValuePopup(QStringLiteral("DELAY MIX"), QStringLiteral("percent"),
                       configVm_->delayMix());
      } else if (parts[2] == QStringLiteral("division")) {
        configVm_->setDelayDivision(configVm_->delayDivision() +
                                    (detents > 0 ? 1 : -1));
        showTextPopup(QStringLiteral("DELAY DIVISION"),
                      QString::fromLatin1(audio::fx::Delay::divisionName(
                          configVm_->delayDivision())));
      }
    } else if (parts[1] == QStringLiteral("reverb") && parts.size() >= 3) {
      if (parts[2] == QStringLiteral("predelay")) {
        configVm_->setReverbPredelayMs(configVm_->reverbPredelayMs() +
                                       step * (fine ? 1.0 : 5.0));
        showValuePopup(QStringLiteral("REVERB PREDELAY"), QStringLiteral("ms"),
                       configVm_->reverbPredelayMs());
      } else if (parts[2] == QStringLiteral("decay")) {
        configVm_->setReverbDecay(configVm_->reverbDecay() +
                                  step * (fine ? 0.005 : 0.02));
        showValuePopup(QStringLiteral("REVERB DECAY"), QStringLiteral("generic"),
                       configVm_->reverbDecay());
      } else if (parts[2] == QStringLiteral("damping")) {
        configVm_->setReverbDamping(configVm_->reverbDamping() +
                                    step * (fine ? 0.005 : 0.02));
        showValuePopup(QStringLiteral("REVERB DAMPING"), QStringLiteral("generic"),
                       configVm_->reverbDamping());
      }
    }
    return;
  }

  if (parts.size() >= 2 && parts[0] == QStringLiteral("channel")) {
    // The popout's TRACK property family addresses the strip it was opened on.
    const QString what = parts[1];
    if (what == QStringLiteral("gain")) {
      mixerVm_->setTrackGainDb(mixerVm_->trackGainDb() + step * (fine ? 0.1 : 0.5));
      showValuePopup(QStringLiteral("CHANNEL GAIN"), QStringLiteral("db"),
                     mixerVm_->trackGainDb());
    } else if (what == QStringLiteral("trim")) {
      mixerVm_->setTrackTrimDb(mixerVm_->trackTrimDb() + step * (fine ? 0.1 : 0.5));
      showValuePopup(QStringLiteral("CHANNEL TRIM"), QStringLiteral("db"),
                     mixerVm_->trackTrimDb());
    } else if (what == QStringLiteral("pan")) {
      mixerVm_->setTrackPan(mixerVm_->trackPan() + step * (fine ? 0.005 : 0.02));
      showValuePopup(QStringLiteral("CHANNEL PAN"), QStringLiteral("pan"),
                     mixerVm_->trackPan());
    } else if (what == QStringLiteral("fx1") || what == QStringLiteral("fx2")) {
      const int slot = what == QStringLiteral("fx2") ? 1 : 0;
      const double next = std::clamp(mixerVm_->fxWetAt(mixerVm_->trackIndex(), slot) +
                                         step * (fine ? 0.005 : 0.02),
                                     0.0, 1.0);
      mixerVm_->setFxWetAt(mixerVm_->trackIndex(), slot, next);
      showValuePopup(QStringLiteral("CHANNEL FX%1 WET").arg(slot + 1),
                     QStringLiteral("percent"), next);
    } else if (what == QStringLiteral("cue")) {
      mixerVm_->setTrackCue(mixerVm_->trackCue() + step * (fine ? 0.005 : 0.02));
      showValuePopup(QStringLiteral("CHANNEL CUE"), QStringLiteral("percent"),
                     mixerVm_->trackCue());
    } else if (what == QStringLiteral("out34")) {
      mixerVm_->setTrackOut34(mixerVm_->trackOut34() + step * (fine ? 0.005 : 0.02));
      showValuePopup(QStringLiteral("CHANNEL OUT34"), QStringLiteral("percent"),
                     mixerVm_->trackOut34());
    }
    return;
  }

  if (parts.size() >= 2 && parts[0] == QStringLiteral("mixer") &&
      parts[1] == QStringLiteral("master")) {
    const double delta = step * (fine ? 0.1 : 0.5);
    mixerVm_->setMasterDb(mixerVm_->masterDb() + delta);
    showValuePopup(QStringLiteral("MASTER"), QStringLiteral("db"),
                   mixerVm_->masterDb());
    return;
  }

  if (parts.size() >= 3 && parts[0] == QStringLiteral("mixer")) {
    const QString what = parts[1];
    const int strip = parts[2].toInt();
    selectStrip(strip);
    if (what == QStringLiteral("in")) {
      showTextPopup(QStringLiteral("IN %1 INPUT").arg(strip + 1, 2, 10, QLatin1Char('0')),
                    mixerVm_->cycleInSource(strip, detents > 0 ? 1 : -1));
      return;
    }
    if (what == QStringLiteral("gain")) {
      mixerVm_->setGainDb(strip, mixerVm_->gainDbAt(strip) +
                                     step * (fine ? 0.1 : 0.5));
      popupGain(QStringLiteral("IN %1 GAIN").arg(strip + 1, 2, 10, QLatin1Char('0')),
                strip);
    } else if (what == QStringLiteral("pan")) {
      mixerVm_->setPan(strip, mixerVm_->panAt(strip) +
                                  step * (fine ? 0.005 : 0.02));
      popupPan(QStringLiteral("IN %1 PAN").arg(strip + 1, 2, 10, QLatin1Char('0')),
               strip);
    } else if (what == QStringLiteral("fx1") || what == QStringLiteral("fx2")) {
      const int slot = what == QStringLiteral("fx2") ? 1 : 0;
      const double next = std::clamp(mixerVm_->fxWetAt(strip, slot) +
                                         step * (fine ? 0.005 : 0.02),
                                     0.0, 1.0);
      mixerVm_->setFxWetAt(strip, slot, next);
      popupFxWet(QStringLiteral("IN %1 FX%2 WET")
                     .arg(strip + 1, 2, 10, QLatin1Char('0'))
                     .arg(slot + 1),
                 strip, slot);
    } else if (what == QStringLiteral("cue")) {
      mixerVm_->setCue(strip, mixerVm_->cueSend(strip) + step * (fine ? 0.005 : 0.02));
      showValuePopup(QStringLiteral("IN %1 CUE").arg(strip + 1, 2, 10, QLatin1Char('0')),
                     QStringLiteral("percent"), mixerVm_->cueSend(strip));
    } else if (what == QStringLiteral("out34")) {
      mixerVm_->setOut34(strip, mixerVm_->out34Send(strip) + step * (fine ? 0.005 : 0.02));
      showValuePopup(QStringLiteral("IN %1 OUT34").arg(strip + 1, 2, 10, QLatin1Char('0')),
                     QStringLiteral("percent"), mixerVm_->out34Send(strip));
    }
    return;
  }

  if (parts.size() >= 2 && parts[0] == QStringLiteral("track")) {
    if (parts[1] == QStringLiteral("gain")) {
      mixerVm_->setTrackGainDb(mixerVm_->trackGainDb() + step * (fine ? 0.1 : 0.5));
      showValuePopup(QStringLiteral("VOL"), QStringLiteral("db"),
                     mixerVm_->trackGainDb());
    } else if (parts[1] == QStringLiteral("trim")) {
      mixerVm_->setTrackTrimDb(mixerVm_->trackTrimDb() + step * (fine ? 0.1 : 0.5));
      showValuePopup(QStringLiteral("GAIN"), QStringLiteral("db"),
                     mixerVm_->trackTrimDb());
    } else if (parts[1] == QStringLiteral("pan")) {
      mixerVm_->setTrackPan(mixerVm_->trackPan() + step * (fine ? 0.005 : 0.02));
      showValuePopup(QStringLiteral("PAN"), QStringLiteral("pan"),
                     mixerVm_->trackPan());
    } else if (parts[1] == QStringLiteral("fx1") || parts[1] == QStringLiteral("fx2")) {
      const int slot = parts[1] == QStringLiteral("fx2") ? 1 : 0;
      const double next = std::clamp(mixerVm_->fxWetAt(mixerVm_->trackIndex(), slot) +
                                         step * (fine ? 0.005 : 0.02),
                                     0.0, 1.0);
      mixerVm_->setFxWetAt(mixerVm_->trackIndex(), slot, next);
      showValuePopup(QStringLiteral("FX%1 WET").arg(slot + 1),
                     QStringLiteral("percent"), next);
    } else if (parts[1] == QStringLiteral("cue")) {
      mixerVm_->setTrackCue(mixerVm_->trackCue() + step * (fine ? 0.005 : 0.02));
      showValuePopup(QStringLiteral("CUE"), QStringLiteral("percent"),
                     mixerVm_->trackCue());
    } else if (parts[1] == QStringLiteral("out34")) {
      mixerVm_->setTrackOut34(mixerVm_->trackOut34() + step * (fine ? 0.005 : 0.02));
      showValuePopup(QStringLiteral("OUT34"), QStringLiteral("percent"),
                     mixerVm_->trackOut34());
    } else if (parts[1] == QStringLiteral("input")) {
      // Source picker: none, then IN 01..18 (the PAIR toggle is a NAV_PRESS).
      mixerVm_->cycleTrackInCh(detents > 0 ? 1 : -1);
      showTextPopup(QStringLiteral("INPUT SOURCE"), mixerVm_->trackInputText());
    } else if (parts[1] == QStringLiteral("output")) {
      mixerVm_->cycleTrackOutDst(detents > 0 ? 1 : -1);
      showTextPopup(QStringLiteral("OUTPUT DEST"), mixerVm_->trackOutputText());
    }
    return;
  }

  if (parts.size() >= 2 && parts[0] == QStringLiteral("loop")) {
    const double step = fine ? kLoopBeatsPerStep : 1.0;
    if (parts[1] == QStringLiteral("start")) {
      recorderVm_->setLoopStartBeats(recorderVm_->loopStartBeats() + step * detents);
      showValuePopup(QStringLiteral("LOOP START"), QStringLiteral("generic"),
                     recorderVm_->loopStartBeats());
    } else if (parts[1] == QStringLiteral("end")) {
      recorderVm_->setLoopEndBeats(recorderVm_->loopEndBeats() + step * detents);
      showValuePopup(QStringLiteral("LOOP END"), QStringLiteral("generic"),
                     recorderVm_->loopEndBeats());
    }
    return;
  }

  if (parts.size() >= 3 && parts[0] == QStringLiteral("punch") &&
      parts[2] == QStringLiteral("measure")) {
    const QString point = parts[1];
    const int delta = detents;
    if (point == QStringLiteral("playfrom")) {
      sessionVm_->setPunchPlayFromMeasure(sessionVm_->punchPlayFromMeasure() + delta);
      showValuePopup(QStringLiteral("PLAY FROM"), QStringLiteral("int"),
                     sessionVm_->punchPlayFromMeasure());
    } else if (point == QStringLiteral("in")) {
      sessionVm_->setPunchInMeasure(sessionVm_->punchInMeasure() + delta);
      showValuePopup(QStringLiteral("PUNCH IN"), QStringLiteral("int"),
                     sessionVm_->punchInMeasure());
    } else if (point == QStringLiteral("out")) {
      sessionVm_->setPunchOutMeasure(sessionVm_->punchOutMeasure() + delta);
      showValuePopup(QStringLiteral("PUNCH OUT"), QStringLiteral("int"),
                     sessionVm_->punchOutMeasure());
    }
    return;
  }

  if (parts.size() >= 2 && parts[0] == QStringLiteral("monitor")) {
    if (parts[1] == QStringLiteral("clickmode")) {
      sessionVm_->setClickMode(moveIndex(sessionVm_->clickMode(), detents > 0 ? 1 : -1, 3));
      showTextPopup(QStringLiteral("CLICK MODE"),
                    sessionVm_->clickMode() == 0 ? QStringLiteral("Off")
                    : (sessionVm_->clickMode() == 1 ? QStringLiteral("Record")
                                                    : QStringLiteral("On play")));
    } else if (parts[1] == QStringLiteral("clickout")) {
      sessionVm_->setClickOutput(moveIndex(sessionVm_->clickOutput(), detents > 0 ? 1 : -1, 3));
      showTextPopup(QStringLiteral("CLICK OUT"),
                    sessionVm_->clickOutput() == 0 ? QStringLiteral("Main")
                    : (sessionVm_->clickOutput() == 1 ? QStringLiteral("Phones")
                                                      : QStringLiteral("Out34")));
    } else if (parts[1] == QStringLiteral("phones")) {
      sessionVm_->setPhonesSrc(moveIndex(sessionVm_->phonesSrc(), detents > 0 ? 1 : -1, 2));
      showTextPopup(QStringLiteral("PHONES"),
                    sessionVm_->phonesSrc() == 0 ? QStringLiteral("CUE")
                                                 : QStringLiteral("Main"));
    } else if (parts[1] == QStringLiteral("cuestyle")) {
      sessionVm_->setCueStyle(moveIndex(sessionVm_->cueStyle(), detents > 0 ? 1 : -1, 2));
      showTextPopup(QStringLiteral("CUE STYLE"),
                    sessionVm_->cueStyle() == 0 ? QStringLiteral("Fader")
                                                : QStringLiteral("Toggle"));
    } else if (parts[1] == QStringLiteral("out34")) {
      sessionVm_->setOut34Mode(moveIndex(sessionVm_->out34Mode(), detents > 0 ? 1 : -1, 2));
      showTextPopup(QStringLiteral("OUT3&4"),
                    sessionVm_->out34Mode() == 0 ? QStringLiteral("Stereo")
                                                 : QStringLiteral("Mono"));
    } else if (parts[1] == QStringLiteral("clickgain")) {
      sessionVm_->setClickGainDb(sessionVm_->clickGainDb() + step * (fine ? 0.1 : 0.5));
      showValuePopup(QStringLiteral("CLICK GAIN"), QStringLiteral("db"),
                     sessionVm_->clickGainDb());
    } else if (parts[1] == QStringLiteral("comp")) {
      if (parts.size() < 3) return;
      if (parts[2] == QStringLiteral("thresh")) {
        sessionVm_->setCompThreshDb(sessionVm_->compThreshDb() + step * (fine ? 0.1 : 0.5));
        showValuePopup(QStringLiteral("COMP THRESH"), QStringLiteral("db"),
                       sessionVm_->compThreshDb());
      } else if (parts[2] == QStringLiteral("ratio")) {
        sessionVm_->setCompRatio(sessionVm_->compRatio() + step * (fine ? 0.1 : 0.5));
        showValuePopup(QStringLiteral("COMP RATIO"), QStringLiteral("ratio"),
                       sessionVm_->compRatio());
      } else if (parts[2] == QStringLiteral("attack")) {
        sessionVm_->setCompAttackMs(sessionVm_->compAttackMs() + step * (fine ? 0.5 : 2.0));
        showValuePopup(QStringLiteral("COMP ATTACK"), QStringLiteral("ms"),
                       sessionVm_->compAttackMs());
      } else if (parts[2] == QStringLiteral("release")) {
        sessionVm_->setCompReleaseMs(sessionVm_->compReleaseMs() + step * (fine ? 1.0 : 5.0));
        showValuePopup(QStringLiteral("COMP RELEASE"), QStringLiteral("ms"),
                       sessionVm_->compReleaseMs());
      } else if (parts[2] == QStringLiteral("makeup")) {
        sessionVm_->setCompMakeupDb(sessionVm_->compMakeupDb() + step * (fine ? 0.1 : 0.5));
        showValuePopup(QStringLiteral("COMP MAKEUP"), QStringLiteral("db"),
                       sessionVm_->compMakeupDb());
      } else if (parts[2] == QStringLiteral("mode")) {
        sessionVm_->setCompMode(moveIndex(sessionVm_->compMode(), detents > 0 ? 1 : -1, 2));
        showTextPopup(QStringLiteral("COMP MODE"),
                      sessionVm_->compMode() == 0 ? QStringLiteral("Peak")
                                                  : QStringLiteral("RMS"));
      }
    }
    return;
  }

  if (parts.size() >= 2 && parts[0] == QStringLiteral("eq")) {
    if (parts[1] == QStringLiteral("subject") && parts.size() >= 3) {
      eqVm_->stepSubject(detents > 0 ? 1 : -1);
    } else if (parts[1] == QStringLiteral("band") && parts.size() >= 3) {
      if (parts[2] == QStringLiteral("gain")) {
        eqVm_->adjustGain(step * (fine ? 0.1 : 0.5));
      } else if (parts[2] == QStringLiteral("q")) {
        eqVm_->adjustQ(step * (fine ? EqModel::kQStepFine : EqModel::kQStep));
      } else if (parts[2] == QStringLiteral("freq")) {
        eqVm_->adjustFreq(step * (fine ? EqModel::kFreqStepOctaves / 4.0
                                       : EqModel::kFreqStepOctaves));
      }
    } else if (parts[1] == QStringLiteral("node") && parts.size() >= 3) {
      // Encoder in EDIT on a node: frequency (the focused value), SHIFT fine.
      eqVm_->adjustFreq(step * (fine ? EqModel::kFreqStepOctaves / 4.0
                                     : EqModel::kFreqStepOctaves));
    }
    return;
  }

  if (parts.size() >= 2 && parts[0] == QStringLiteral("xy")) {
    if (parts[1] == QStringLiteral("delay") || parts[1] == QStringLiteral("reverb")) {
      xyVm_->nudge(parts[1] == QStringLiteral("reverb") ? 1 : 0, 0,
                   detents > 0 ? 1 : -1, fine);
    }
    return;
  }

  if (parts.size() >= 2 && parts[0] == QStringLiteral("tools") &&
      parts[1] == QStringLiteral("brightness")) {
    toolsVm_->setBrightness(toolsVm_->brightness() + detents * (fine ? 1 : 5));
    showValuePopup(QStringLiteral("BRIGHTNESS"), QStringLiteral("percent"),
                   toolsVm_->brightness() / 100.0);
    return;
  }

  if (parts.size() >= 5 && parts[0] == QStringLiteral("routing") &&
      parts[1] == QStringLiteral("pipes") && parts[2] == QStringLiteral("param")) {
    const int pipe = parts[3].toInt();
    const int param = parts[4].toInt();
    const auto params = routingVm_->pipeParams();
    for (const auto& entry : params) {
      const auto map = entry.toMap();
      if (map.value(QStringLiteral("param")).toInt() != param) continue;
      const int minimum = map.value(QStringLiteral("minimum")).toInt();
      const int maximum = map.value(QStringLiteral("maximum")).toInt();
      const int current = map.value(QStringLiteral("value")).toInt();
      const int span = std::max(1, maximum - minimum);
      const int stepSize = fine ? 1 : std::max(1, span / 100);
      const int next = std::clamp(current + detents * stepSize, minimum, maximum);
      routingVm_->setPipeParam(pipe, param, next);
      const auto updated = routingVm_->pipeParams();
      for (const auto& value : updated) {
        const auto row = value.toMap();
        if (row.value(QStringLiteral("param")).toInt() != param) continue;
        showTextPopup(row.value(QStringLiteral("name")).toString(),
                      row.value(QStringLiteral("valueText")).toString());
        break;
      }
      break;
    }
    return;
  }

  if (parts.size() >= 3 && parts[0] == QStringLiteral("routing") &&
      parts[1] == QStringLiteral("transpose")) {
    routingVm_->adjustTranspose(parts[2].toInt(), detents);
    const int index = parts[2].toInt();
    const auto values = routingVm_->transposes();
    showTextPopup(QStringLiteral("TRANSPOSE"),
                  index >= 0 && index < values.size() ? values.value(index).toString()
                                                      : QString());
    return;
  }

  if (parts.size() >= 2 && parts[0] == QStringLiteral("system")) {
    if (parts[1] == QStringLiteral("backend")) {
      const int count = systemVm_->backendLabels().size();
      systemVm_->setBackendIndex(moveIndex(systemVm_->backendIndex(), detents > 0 ? 1 : -1, count));
      showTextPopup(QStringLiteral("AUDIO OUTPUT"),
                    systemVm_->backendLabels().value(systemVm_->backendIndex()));
    } else if (parts[1] == QStringLiteral("input")) {
      const int count = systemVm_->inputLabels().size();
      systemVm_->setInputIndex(moveIndex(systemVm_->inputIndex(), detents > 0 ? 1 : -1, count));
      showTextPopup(QStringLiteral("AUDIO INPUT"),
                    systemVm_->inputLabels().value(systemVm_->inputIndex()));
    } else if (parts[1] == QStringLiteral("buffer")) {
      const int count = systemVm_->bufferLabels().size();
      systemVm_->setBufferIndex(moveIndex(systemVm_->bufferIndex(), detents > 0 ? 1 : -1, count));
      showTextPopup(QStringLiteral("BUFFER"),
                    systemVm_->bufferLabels().value(systemVm_->bufferIndex()));
    } else if (parts[1] == QStringLiteral("click")) {
      if (parts.size() >= 3 && parts[2] == QStringLiteral("level")) {
        setClickLevel(clickLevel() + step * (fine ? 0.01 : 0.05));
        showValuePopup(QStringLiteral("CLICK LEVEL"), QStringLiteral("percent"),
                       clickLevel());
      } else if (parts.size() >= 3 && parts[2] == QStringLiteral("bars")) {
        setClickBars(clickBars() + detents);
        showValuePopup(QStringLiteral("CLICK BARS"), QStringLiteral("int"),
                       clickBars());
      }
    } else if (parts[1] == QStringLiteral("devout") && parts.size() >= 3) {
      // Device output pair for Main 1&2 / OUT3&4 / phones. The picker list wraps.
      const int delta = detents > 0 ? 1 : -1;
      QString label = QStringLiteral("MAIN 1&2 DEVICE OUT");
      int pair = 0;
      if (parts[2] == QStringLiteral("out34")) {
        label = QStringLiteral("OUT3&4 DEVICE OUT");
        pair = moveIndex(sessionVm_->deviceOutOut34(), delta, kDeviceOutPairs);
        sessionVm_->setDeviceOutOut34(pair);
      } else if (parts[2] == QStringLiteral("phones")) {
        label = QStringLiteral("PHONES DEVICE OUT");
        pair = moveIndex(sessionVm_->deviceOutPhones(), delta, kDeviceOutPairs);
        sessionVm_->setDeviceOutPhones(pair);
      } else {
        pair = moveIndex(sessionVm_->deviceOutMain(), delta, kDeviceOutPairs);
        sessionVm_->setDeviceOutMain(pair);
      }
      showTextPopup(label, sessionVm_->deviceOutPairLabels().value(pair));
    }
    return;
  }

  if (parts.size() >= 2 && parts[0] == QStringLiteral("timeline")) {
    if (parts[1] == QStringLiteral("cursor")) {
      const double stepBeats = fine ? timeline::kStepFineBeats : 1.0;
      timelineVm_->moveCursor(step * stepBeats, 0);
      showTextPopup(QStringLiteral("TIMELINE CURSOR"),
                    timelineVm_->beatTextOf(timelineVm_->cursorBeats()));
    } else if (parts[1] == QStringLiteral("in")) {
      const double stepBeats = fine ? timeline::kStepFineBeats : timeline::kStepCoarseBeats;
      showTextPopup(QStringLiteral("REGION IN"),
                    timelineVm_->nudgeIn(step * stepBeats));
    } else if (parts[1] == QStringLiteral("out")) {
      const double stepBeats = fine ? timeline::kStepFineBeats : timeline::kStepCoarseBeats;
      showTextPopup(QStringLiteral("REGION OUT"),
                    timelineVm_->nudgeOut(step * stepBeats));
    }
    return;
  }

  if (parts.size() >= 2 && parts[0] == QStringLiteral("learn") &&
      parts[1] == QStringLiteral("target")) {
    const int count = learnVm_->targets().size();
    learnVm_->setTargetIndex(moveIndex(learnVm_->targetIndex(), detents > 0 ? 1 : -1, count));
    showTextPopup(QStringLiteral("LEARN TARGET"),
                  LearnViewModel::humanTarget(learnVm_->targets().value(learnVm_->targetIndex())));
  }
}

void AppController::rebuildFocusIds() {
  std::vector<QString> ids;
  if (keypadOpen_ || confirmOpen_) {
    // A2 modal trap: an open keypad or confirmation has no panel focus ring,
    // so NAV/PRESS/encoder can never reach a control behind it.
  } else if (tabGridOpen_) {
    for (int i = 0; i < kScreenCount; ++i) {
      ids.push_back(QStringLiteral("tabgrid.") + QString::number(i));
    }
  } else if (bankGridOpen_) {
    for (const auto& row : mixerVm_->bankPages()) {
      const auto page = row.toMap();
      if (page.value(QStringLiteral("empty")).toBool()) continue;
      ids.push_back(QStringLiteral("bank.page.%1")
                        .arg(page.value(QStringLiteral("index")).toInt()));
    }
    ids.push_back(QStringLiteral("bank.page.close"));
  } else if (timelineVm_ != nullptr && timelineVm_->editOpen()) {
    // EDIT popup: the modal traps NAV. The ring walks CLOSE, the two zoom
    // verbs, the two edge handles, the eight op cards, the focused op's value
    // chip and the three verbs (APPLY / UNDO / RENDER).
    ids.push_back(QStringLiteral("edit.close"));
    ids.push_back(QStringLiteral("edit.zoom.out"));
    ids.push_back(QStringLiteral("edit.zoom.in"));
    ids.push_back(QStringLiteral("edit.in"));
    ids.push_back(QStringLiteral("edit.out"));
    for (int op = 0; op < timeline::kEditOpCount; ++op) {
      ids.push_back(QStringLiteral("edit.op.%1").arg(op));
    }
    ids.push_back(QStringLiteral("edit.value"));
    ids.push_back(QStringLiteral("edit.apply"));
    ids.push_back(QStringLiteral("edit.undo"));
    ids.push_back(QStringLiteral("edit.render"));
  } else if (arrangeVm_ != nullptr && arrangeVm_->clipMenuOpen()) {
    for (int action = 0; action < arrangeVm_->clipActions().size(); ++action) {
      ids.push_back(QStringLiteral("arrange.clip.action.%1").arg(action));
    }
    if (arrangeVm_->clipMenuPane() != 0) {
      ids.push_back(QStringLiteral("arrange.clip.gain"));
      ids.push_back(QStringLiteral("arrange.clip.fadein"));
      ids.push_back(QStringLiteral("arrange.clip.fadeout"));
      ids.push_back(QStringLiteral("arrange.clip.pane.back"));
    }
    ids.push_back(QStringLiteral("arrange.clip.close"));
  } else if (arrangeVm_ != nullptr && arrangeVm_->lengthPopupOpen()) {
    ids.push_back(QStringLiteral("arrange.length.decbar"));
    ids.push_back(QStringLiteral("arrange.length.incbar"));
    ids.push_back(QStringLiteral("arrange.length.decbeat"));
    ids.push_back(QStringLiteral("arrange.length.incbeat"));
    ids.push_back(QStringLiteral("arrange.length.done"));
  } else if (takeSheetOpen_) {
    ids.push_back(QStringLiteral("take.sheet.live"));
    for (int row = 0; row < recorderVm_->takes()->rowCount(); ++row) {
      ids.push_back(QStringLiteral("take.sheet.row.%1").arg(row));
    }
    ids.push_back(QStringLiteral("take.sheet.close"));
  } else if (tempoPopupOpen_) {
    ids.push_back(QStringLiteral("tempo.done"));
  } else if (configVm_ && configVm_->open()) {
    const QString kind = configVm_->kind();
    if (kind == QStringLiteral("metronome")) {
      for (int mode = 0; mode < 3; ++mode) {
        ids.push_back(QStringLiteral("config.mode.%1").arg(mode));
        ids.push_back(QStringLiteral("config.out.%1").arg(mode));
      }
      ids.push_back(QStringLiteral("config.gain"));
      ids.push_back(QStringLiteral("config.bars"));
    } else if (kind == QStringLiteral("sends")) {
      for (const char* row : {"cue", "fx1", "fx2", "out34", "rec"}) {
        ids.push_back(QStringLiteral("config.send.%1.pre").arg(QLatin1String(row)));
        ids.push_back(QStringLiteral("config.send.%1.post").arg(QLatin1String(row)));
      }
    } else if (kind == QStringLiteral("trackmode")) {
      for (int mode = 0; mode < kTrackModeCount; ++mode) {
        ids.push_back(QStringLiteral("config.trackmode.%1").arg(mode));
      }
      for (int source = 0; source < kRecSourceCount; ++source) {
        ids.push_back(QStringLiteral("config.recsource.%1").arg(source));
      }
    } else if (kind == QStringLiteral("recmode")) {
      for (int mode = 0; mode < kRecModeCount; ++mode) {
        ids.push_back(QStringLiteral("config.recmode.%1").arg(mode));
      }
      ids.push_back(QStringLiteral("config.countin"));
    } else if (kind == QStringLiteral("takeactions")) {
      for (int action = 0; action < configVm_->takeActions().size(); ++action) {
        ids.push_back(QStringLiteral("config.takeaction.%1").arg(action));
      }
    } else if (kind == QStringLiteral("trackfromtake")) {
      ids.push_back(QStringLiteral("config.trackfromtake.0"));
      ids.push_back(QStringLiteral("config.trackfromtake.1"));
    } else if (kind == QStringLiteral("takeedit")) {
      ids.push_back(QStringLiteral("config.ok"));
    } else if (kind == QStringLiteral("monitor")) {
      ids.push_back(QStringLiteral("config.phones.0"));
      ids.push_back(QStringLiteral("config.phones.1"));
      ids.push_back(QStringLiteral("config.cuestyle.0"));
      ids.push_back(QStringLiteral("config.cuestyle.1"));
      ids.push_back(QStringLiteral("config.out34.0"));
      ids.push_back(QStringLiteral("config.out34.1"));
    } else if (kind == QStringLiteral("eqtype")) {
      for (int type = 0; type < EqModel::kTypeCount; ++type) {
        ids.push_back(QStringLiteral("config.eqtype.%1").arg(type));
      }
    } else if (kind == QStringLiteral("delay")) {
      ids.push_back(QStringLiteral("config.delay.beat"));
      ids.push_back(QStringLiteral("config.delay.ping"));
      for (int division = 0; division < audio::fx::Delay::kDivCount; ++division) {
        ids.push_back(QStringLiteral("config.delaydivision.%1").arg(division));
      }
      ids.push_back(QStringLiteral("config.delay.feedback"));
      ids.push_back(QStringLiteral("config.delay.mix"));
    } else if (kind == QStringLiteral("reverb")) {
      for (int model = 0; model < audio::fx::Reverb::kModelCount; ++model) {
        ids.push_back(QStringLiteral("config.reverb.model.%1").arg(model));
      }
      ids.push_back(QStringLiteral("config.reverb.predelay"));
      ids.push_back(QStringLiteral("config.reverb.decay"));
      ids.push_back(QStringLiteral("config.reverb.damping"));
    } else if (kind == QStringLiteral("timelineops")) {
      for (int op = 0; op < timeline::kTimelineUiOpCount; ++op) {
        ids.push_back(QStringLiteral("config.timeline.op.%1").arg(op));
      }
      ids.push_back(QStringLiteral("config.timeline.gain"));
      ids.push_back(QStringLiteral("config.timeline.fadein"));
      ids.push_back(QStringLiteral("config.timeline.fadeout"));
    } else if (kind == QStringLiteral("inputsource")) {
      const int channels = MixerViewModel::inputChannelCount();
      for (int state = 0; state < 1 + channels + channels / 2; ++state) {
        ids.push_back(QStringLiteral("config.input.%1").arg(state));
      }
    } else if (kind == QStringLiteral("outputdest")) {
      for (int destination = 0; destination < kOutDstCount; ++destination) {
        ids.push_back(QStringLiteral("config.output.%1").arg(destination));
      }
    } else if (kind == QStringLiteral("fxslot")) {
      // NONE / DELAY / REVERB plus every discovered CLAP plugin; a plugin
      // selection is a real ring stop now.
      for (int choice = 0; choice < configVm_->fxSlotLabels().size(); ++choice) {
        ids.push_back(QStringLiteral("config.fxslot.%1").arg(choice));
      }
    } else if (kind == QStringLiteral("pipesource")) {
      for (int index = 0; index < routingVm_->inputNodes().size(); ++index) {
        ids.push_back(QStringLiteral("config.pipesource.%1").arg(index));
      }
    } else if (kind == QStringLiteral("pipedest")) {
      for (int index = 0; index < routingVm_->outputNodes().size(); ++index) {
        ids.push_back(QStringLiteral("config.pipedest.%1").arg(index));
      }
    } else if (kind == QStringLiteral("pipetype")) {
      const int types = configVm_->pipeTypeLabels().size();
      for (int index = 0; index < types; ++index) {
        ids.push_back(QStringLiteral("config.pipetype.%1").arg(index));
      }
    } else if (kind == QStringLiteral("pipeparam")) {
      for (int choice = 0; choice < configVm_->pipeParamChoices().size(); ++choice) {
        ids.push_back(QStringLiteral("config.pipeparam.%1").arg(choice));
      }
    } else if (kind == QStringLiteral("chainmenu")) {
      ids.push_back(QStringLiteral("shift.routing.pipes.chain.export"));
      ids.push_back(QStringLiteral("shift.routing.pipes.chain.import"));
    } else if (kind == QStringLiteral("delaydivision")) {
      for (int division = 0; division < audio::fx::Delay::kDivCount; ++division) {
        ids.push_back(QStringLiteral("config.delaydivision.%1").arg(division));
      }
    } else if (kind == QStringLiteral("reverbmodel")) {
      for (int model = 0; model < audio::fx::Reverb::kModelCount; ++model) {
        ids.push_back(QStringLiteral("config.reverbmodel.%1").arg(model));
      }
    } else if (kind == QStringLiteral("addtrack")) {
      ids.push_back(QStringLiteral("config.addtrack.name"));
      for (int mode = 0; mode < 3; ++mode) {
        ids.push_back(QStringLiteral("config.addtrack.mode.%1").arg(mode));
      }
      for (int channel = 0; channel < MixerViewModel::inputChannelCount(); ++channel) {
        ids.push_back(QStringLiteral("config.addtrack.ch.%1").arg(channel));
      }
      for (int destination = 0; destination < kOutDstCount; ++destination) {
        ids.push_back(QStringLiteral("config.addtrack.out.%1").arg(destination));
      }
    } else if (kind == QStringLiteral("pipechannels")) {
      for (int channel = 0; channel < 16; ++channel) {
        ids.push_back(QStringLiteral("config.pipe.ch.%1").arg(channel));
      }
      for (int preset = 0; preset < 5; ++preset) {
        ids.push_back(QStringLiteral("config.pipe.preset.%1").arg(preset));
      }
    } else {
      ids.push_back(QStringLiteral("config.on"));
      ids.push_back(QStringLiteral("config.mode.0"));
      ids.push_back(QStringLiteral("config.mode.1"));
      ids.push_back(QStringLiteral("config.thresh"));
      ids.push_back(QStringLiteral("config.ratio"));
      ids.push_back(QStringLiteral("config.attack"));
      ids.push_back(QStringLiteral("config.release"));
      ids.push_back(QStringLiteral("config.makeup"));
    }
    ids.push_back(QStringLiteral("config.ok"));
    ids.push_back(QStringLiteral("config.cancel"));
  } else if (fxVm_ && fxVm_->open()) {
    // TRACK FX popup: it sits above the channel popout, so it owns the ring
    // while it is open. The two slot cards are FX1 then FX2; a CLAP slot's
    // parameter cards come from the plugin's own clap.params table.
    ids.push_back(QStringLiteral("trackfx.close"));
    for (int slot = 0; slot < MixerModel::kFxSlotCount; ++slot) {
      const QString prefix = QStringLiteral("trackfx.fx%1").arg(slot + 1);
      ids.push_back(prefix + QStringLiteral(".select"));
      ids.push_back(prefix + QStringLiteral(".wet"));
      if (mixerVm_->fxChoiceAt(mixerVm_->selectedStrip(), slot) != 0) {
        ids.push_back(prefix + QStringLiteral(".bypass"));
      }
      QVariantList params = mixerVm_->fxParams(mixerVm_->selectedStrip(), slot);
      for (const auto& entry : params) {
        ids.push_back(prefix + QStringLiteral(".param.%1")
                          .arg(entry.toMap().value(QStringLiteral("index")).toInt()));
      }
    }
    // The rack's REC SOURCE neighbour (W2b).
    ids.push_back(QStringLiteral("trackfx.recsource"));
  } else if (mixerVm_->channelOpen()) {
    // Full channel popout: it traps the ring like the config popup, so the
    // panel can reach every control of the channel it shows.
    ids.push_back(QStringLiteral("channel.close"));
    ids.push_back(QStringLiteral("channel.rename"));
    ids.push_back(QStringLiteral("channel.gain"));
    ids.push_back(QStringLiteral("channel.trim"));
    ids.push_back(QStringLiteral("channel.pan"));
    ids.push_back(QStringLiteral("channel.fx1"));
    ids.push_back(QStringLiteral("channel.fx2"));
    ids.push_back(QStringLiteral("channel.cue"));
    ids.push_back(QStringLiteral("channel.out34"));
    ids.push_back(QStringLiteral("channel.mute"));
    ids.push_back(QStringLiteral("channel.solo"));
    ids.push_back(QStringLiteral("channel.arm"));
    ids.push_back(QStringLiteral("channel.monitor"));
    ids.push_back(QStringLiteral("channel.input"));
    ids.push_back(QStringLiteral("channel.output"));
    ids.push_back(QStringLiteral("channel.fx"));
    ids.push_back(QStringLiteral("channel.remove"));
  } else if (shiftFace_) {
    switch (tab_) {
      case 0:
        if (mixerVm_->selectedMode()) {
          ids.push_back(QStringLiteral("shift.track.input"));
          ids.push_back(QStringLiteral("shift.track.input.pair"));
          ids.push_back(QStringLiteral("shift.track.output"));
          ids.push_back(QStringLiteral("shift.track.gain"));
          ids.push_back(QStringLiteral("shift.track.fx1"));
          ids.push_back(QStringLiteral("shift.track.fx2"));
          ids.push_back(QStringLiteral("shift.track.cue"));
          ids.push_back(QStringLiteral("shift.track.out34"));
          ids.push_back(QStringLiteral("shift.monitor.sends"));
          ids.push_back(QStringLiteral("shift.track.mode"));
          ids.push_back(QStringLiteral("shift.track.fx"));
        } else {
          const int first = mixerVm_->bank() * kStripsPerBank;
          for (int position = first;
               position < first + kStripsPerBank && position < mixerVm_->stripCount();
               ++position) {
            const QVariant slot = mixerVm_->data(mixerVm_->index(position, 0),
                                                MixerViewModel::SlotRole);
            ids.push_back(QStringLiteral("shift.mixer.gain.%1").arg(slot.toInt()));
            ids.push_back(QStringLiteral("shift.mixer.fx1.%1").arg(slot.toInt()));
            ids.push_back(QStringLiteral("shift.mixer.fx2.%1").arg(slot.toInt()));
            ids.push_back(QStringLiteral("shift.mixer.cue.%1").arg(slot.toInt()));
            ids.push_back(QStringLiteral("shift.mixer.out34.%1").arg(slot.toInt()));
            ids.push_back(QStringLiteral("shift.mixer.monitor.%1").arg(slot.toInt()));
          }
        }
        break;
      case 1:
        ids.push_back(QStringLiteral("shift.routing.reset"));
        ids.push_back(QStringLiteral("shift.routing.page.0"));
        ids.push_back(QStringLiteral("shift.routing.page.1"));
        if (routingVm_->shiftPage() == 0) {
          if (routingVm_->filterPageCount() > 1) {
            ids.push_back(QStringLiteral("shift.routing.filter.prev"));
            ids.push_back(QStringLiteral("shift.routing.filter.next"));
          }
          for (const auto& row : routingVm_->filterPageRows()) {
            const int source = row.toMap().value(QStringLiteral("nodeIndex")).toInt();
            for (int channel = 0; channel < 16; ++channel) {
              ids.push_back(QStringLiteral("shift.routing.mask.%1.%2").arg(source).arg(channel));
            }
            ids.push_back(QStringLiteral("shift.routing.transpose.%1").arg(source));
          }
          for (int output = 0; output < 3; ++output) {
            ids.push_back(QStringLiteral("shift.clock.clock.%1").arg(output));
            ids.push_back(QStringLiteral("shift.clock.transport.%1").arg(output));
          }
        } else {
          // The pipe page's selectors: every one of them opens a list. The
          // row actions exist on the selected row only, so the ring stops
          // there too.
          ids.push_back(QStringLiteral("shift.routing.pipes.src.name"));
          ids.push_back(QStringLiteral("shift.routing.pipes.dst.name"));
          if (routingVm_->outputInsertsAvailable()) {
            ids.push_back(QStringLiteral("shift.routing.pipes.insert.route"));
            ids.push_back(QStringLiteral("shift.routing.pipes.insert.add"));
            for (int chain = 0; chain < routingVm_->insertRows().size(); ++chain) {
              ids.push_back(
                  QStringLiteral("shift.routing.pipes.insert.chain.%1").arg(chain));
            }
          }
          if (routingVm_->insertSlot() >= 0) {
            const int insert = routingVm_->insertSlot();
            ids.push_back(
                QStringLiteral("shift.routing.pipes.insert.on.%1").arg(insert));
            ids.push_back(
                QStringLiteral("shift.routing.pipes.insert.up.%1").arg(insert));
            ids.push_back(
                QStringLiteral("shift.routing.pipes.insert.down.%1").arg(insert));
            ids.push_back(
                QStringLiteral("shift.routing.pipes.insert.remove.%1").arg(insert));
          }
          ids.push_back(QStringLiteral("shift.routing.pipes.add.type"));
          ids.push_back(QStringLiteral("shift.routing.pipes.add"));
          ids.push_back(QStringLiteral("shift.routing.pipes.scope"));
          ids.push_back(QStringLiteral("shift.routing.pipes.chain"));
          const int pipes = routingVm_->pipeRows().size();
          for (int pipe = 0; pipe < pipes; ++pipe) {
            ids.push_back(QStringLiteral("shift.routing.pipes.select.%1").arg(pipe));
          }
          const int selected = routingVm_->selectedPipe();
          if (selected >= 0 && selected < pipes) {
            ids.push_back(QStringLiteral("shift.routing.pipes.bypass.%1").arg(selected));
            ids.push_back(QStringLiteral("shift.routing.pipes.up.%1").arg(selected));
            ids.push_back(QStringLiteral("shift.routing.pipes.down.%1").arg(selected));
            ids.push_back(QStringLiteral("shift.routing.pipes.remove.%1").arg(selected));
          }
          const auto params = routingVm_->pipeParams();
          for (const auto& entry : params) {
            ids.push_back(QStringLiteral("shift.routing.pipes.param.%1.%2")
                              .arg(selected)
                              .arg(entry.toMap().value(QStringLiteral("param")).toInt()));
          }
        }
        break;
      case 2:
        ids.push_back(QStringLiteral("shift.recorder.import"));
        ids.push_back(QStringLiteral("shift.recorder.live"));
        ids.push_back(QStringLiteral("shift.recorder.activate"));
        ids.push_back(QStringLiteral("shift.recorder.rename"));
        ids.push_back(QStringLiteral("shift.recorder.delete"));
        ids.push_back(QStringLiteral("shift.loop.enable"));
        ids.push_back(QStringLiteral("shift.loop.start"));
        ids.push_back(QStringLiteral("shift.loop.end"));
        for (const char* point : {"playfrom", "in", "out"}) {
          ids.push_back(QStringLiteral("shift.punch.%1.enable").arg(QLatin1String(point)));
          ids.push_back(QStringLiteral("shift.punch.%1.measure").arg(QLatin1String(point)));
        }
        break;
      case 3:
        ids.push_back(QStringLiteral("shift.system.panic"));
        ids.push_back(QStringLiteral("shift.system.page.0"));
        ids.push_back(QStringLiteral("shift.system.page.1"));
        ids.push_back(QStringLiteral("shift.system.page.2"));
        if (systemShiftPage_ == 0) {
          ids.push_back(QStringLiteral("shift.monitor.summary.metronome"));
          ids.push_back(QStringLiteral("shift.monitor.summary.sends"));
          ids.push_back(QStringLiteral("shift.monitor.summary.monitor"));
          ids.push_back(QStringLiteral("shift.monitor.summary.comp"));
        } else if (systemShiftPage_ == 1) {
          ids.push_back(QStringLiteral("shift.learn.target"));
          ids.push_back(QStringLiteral("shift.learn.arm"));
          ids.push_back(QStringLiteral("shift.learn.clear"));
          for (int row = 0; row < learnVm_->bindings().size(); ++row) {
            ids.push_back(QStringLiteral("shift.learn.binding.%1").arg(row));
          }
        } else {
          ids.push_back(QStringLiteral("shift.tools.brightness"));
        }
        break;
      case kArrangeTabIndex:
        ids.push_back(QStringLiteral("shift.arrange.addsection"));
        ids.push_back(QStringLiteral("shift.arrange.dupsection"));
        ids.push_back(QStringLiteral("shift.arrange.loopsection"));
        ids.push_back(QStringLiteral("shift.arrange.rename"));
        ids.push_back(QStringLiteral("shift.arrange.length"));
        ids.push_back(QStringLiteral("shift.arrange.loop"));
        for (int grid = 0; grid < arrange::kGridCount; ++grid) {
          ids.push_back(QStringLiteral("shift.arrange.grid.%1").arg(grid));
        }
        ids.push_back(QStringLiteral("shift.arrange.zoomout"));
        ids.push_back(QStringLiteral("shift.arrange.zoomin"));
        ids.push_back(QStringLiteral("shift.arrange.render"));
        break;
      case 6: {
        // EQ SHIFT face: the band editor is too tall for the tile row, so the
        // sliders and the six explicit band types live here.
        ids.push_back(QStringLiteral("shift.eq.subject.main"));
        ids.push_back(QStringLiteral("shift.eq.subject.track"));
        ids.push_back(QStringLiteral("shift.eq.band.gain"));
        ids.push_back(QStringLiteral("shift.eq.band.q"));
        ids.push_back(QStringLiteral("shift.eq.band.freq"));
        ids.push_back(QStringLiteral("shift.eq.band.type"));
        break;
      }
      case 5:
        ids.push_back(QStringLiteral("shift.projects.new"));
        ids.push_back(QStringLiteral("shift.projects.rename"));
        ids.push_back(QStringLiteral("shift.projects.delete"));
        ids.push_back(QStringLiteral("shift.projects.template.new"));
        ids.push_back(QStringLiteral("shift.projects.template.save"));
        ids.push_back(QStringLiteral("shift.projects.template.clear"));
        break;
      default:
        break;
    }
  } else {
    // C3 step 1: the tab row is one TAB stop, not ten ring stops. Pressing it
    // expands the 2x5 grid above; the tab row itself stays a direct touch
    // target (QML keeps its own focusId for the tap runner).
    ids.push_back(QStringLiteral("tab.menu"));
    if (noPico_) {
      // A5: the touch transport is a panel stop only where there is no panel.
      ids.push_back(QStringLiteral("transport.play"));
      ids.push_back(QStringLiteral("transport.stop"));
      ids.push_back(QStringLiteral("transport.rec"));
    }
    ids.push_back(QStringLiteral("transport.click"));
    ids.push_back(QStringLiteral("transport.recmode"));
    ids.push_back(QStringLiteral("transport.song"));
    switch (tab_) {
      case 0: {
        ids.push_back(QStringLiteral("mixer.mode.0"));
        ids.push_back(QStringLiteral("mixer.mode.1"));
        if (mixerVm_->selectedMode()) {
          // Only the current page of four is on screen; the header page buttons
          // walk the same list the MIXER face banks.
          const int first = mixerVm_->bank() * kStripsPerBank;
          for (int position = first;
               position < first + kStripsPerBank && position < mixerVm_->stripCount();
               ++position) {
            const QVariant slot = mixerVm_->data(mixerVm_->index(position, 0),
                                                MixerViewModel::SlotRole);
            ids.push_back(QStringLiteral("track.select.%1").arg(slot.toInt()));
          }
          ids.push_back(QStringLiteral("track.gain"));
          ids.push_back(QStringLiteral("track.pan"));
          ids.push_back(QStringLiteral("track.mode"));
          ids.push_back(QStringLiteral("track.arm"));
          ids.push_back(QStringLiteral("track.mute"));
          ids.push_back(QStringLiteral("track.solo"));
        } else {
          const int first = mixerVm_->bank() * kStripsPerBank;
          for (int position = first;
               position < first + kStripsPerBank && position < mixerVm_->stripCount();
               ++position) {
            const QVariant slot = mixerVm_->data(mixerVm_->index(position, 0),
                                                MixerViewModel::SlotRole);
            const int strip = slot.toInt();
            ids.push_back(QStringLiteral("mixer.expand.%1").arg(strip));
            ids.push_back(QStringLiteral("mixer.trackmode.%1").arg(strip));
            ids.push_back(QStringLiteral("mixer.select.%1").arg(strip));
            ids.push_back(QStringLiteral("mixer.in.%1").arg(strip));
            ids.push_back(QStringLiteral("mixer.gain.%1").arg(strip));
            ids.push_back(QStringLiteral("mixer.pan.%1").arg(strip));
            ids.push_back(QStringLiteral("mixer.mute.%1").arg(strip));
            ids.push_back(QStringLiteral("mixer.solo.%1").arg(strip));
            ids.push_back(QStringLiteral("mixer.arm.%1").arg(strip));
          }
          ids.push_back(QStringLiteral("mixer.add"));
          ids.push_back(QStringLiteral("mixer.master"));
          ids.push_back(QStringLiteral("mixer.bank.0"));
          ids.push_back(QStringLiteral("mixer.bank.pages"));
          ids.push_back(QStringLiteral("mixer.bank.1"));
        }
        break;
      }
      case 1: {
        ids.push_back(QStringLiteral("routing.reset"));
        ids.push_back(QStringLiteral("routing.shiftpipes"));
        for (const auto& node : routingVm_->inputNodes()) {
          ids.push_back(QStringLiteral("routing.node.in.%1")
                            .arg(node.toMap().value(QStringLiteral("port")).toInt()));
        }
        for (const auto& node : routingVm_->outputNodes()) {
          ids.push_back(QStringLiteral("routing.node.out.%1")
                            .arg(node.toMap().value(QStringLiteral("port")).toInt()));
        }
        break;
      }
      case 2: {
        for (int track = 0; track < kNumStrips; ++track) {
          ids.push_back(QStringLiteral("recorder.track.%1").arg(track));
        }
        ids.push_back(QStringLiteral("recorder.mode"));
        ids.push_back(QStringLiteral("recorder.start"));
        for (int row = 0; row < recorderVm_->takes()->rowCount(); ++row) {
          ids.push_back(QStringLiteral("recorder.take.%1").arg(row));
          ids.push_back(QStringLiteral("recorder.take.menu.%1").arg(row));
        }
        break;
      }
      case 3: {
        ids.push_back(QStringLiteral("system.backend"));
        ids.push_back(QStringLiteral("system.input"));
        ids.push_back(QStringLiteral("system.buffer"));
        ids.push_back(QStringLiteral("system.devout.main"));
        ids.push_back(QStringLiteral("system.devout.out34"));
        ids.push_back(QStringLiteral("system.devout.phones"));
        ids.push_back(QStringLiteral("system.refresh"));
        break;
      }
      case kProjectsTabIndex: {
        ids.push_back(QStringLiteral("projects.save"));
        for (int row = 0; row < projectsVm_->model()->rowCount(); ++row) {
          ids.push_back(QStringLiteral("projects.row.%1").arg(row));
        }
        break;
      }
      case kMonitorTabIndex: {
        // MIDI MONITOR: one focus stop per port row plus RESET. Activating a
        // port reads its counters out in the value popup.
        ids.push_back(QStringLiteral("midimon.clock.filter"));
        ids.push_back(QStringLiteral("midimon.reset"));
        for (int port = 0; port < midi::kPortCount; ++port) {
          ids.push_back(QStringLiteral("midimon.port.%1").arg(port));
        }
        break;
      }
      case kEqTabIndex: {
        // EQ grid: subject selector, the A-button band step, one node per band,
        // the four corners and the fullscreen toggle.
        ids.push_back(QStringLiteral("eq.subject.main"));
        ids.push_back(QStringLiteral("eq.subject.track"));
        ids.push_back(QStringLiteral("eq.band.select"));
        for (int band = 0; band < EqModel::kBandCount; ++band) {
          ids.push_back(QStringLiteral("eq.node.%1").arg(band));
        }
        ids.push_back(QStringLiteral("eq.band.gain"));
        ids.push_back(QStringLiteral("eq.band.q"));
        ids.push_back(QStringLiteral("eq.band.freq"));
        ids.push_back(QStringLiteral("eq.band.type"));
        ids.push_back(QStringLiteral("eq.fullscreen"));
        break;
      }
      case kXyTabIndex: {
        // X/Y performance: the two grids, the freeze toggle and the two
        // secondary-parameter popups.
        ids.push_back(QStringLiteral("xy.delay"));
        ids.push_back(QStringLiteral("xy.reverb"));
        ids.push_back(QStringLiteral("xy.freeze"));
        ids.push_back(QStringLiteral("xy.delay.params"));
        ids.push_back(QStringLiteral("xy.reverb.params"));
        break;
      }
      case kArrangeTabIndex: {
        // ARRANGE: section markers, section verbs, the length/loop controls,
        // grid and zoom, every clip block and the selected clip's "...".
        for (int section = 0; section < arrangeVm_->sectionCount(); ++section) {
          ids.push_back(QStringLiteral("arrange.section.%1").arg(section));
        }
        ids.push_back(QStringLiteral("arrange.addsection"));
        ids.push_back(QStringLiteral("arrange.dupsection"));
        ids.push_back(QStringLiteral("arrange.loopsection"));
        ids.push_back(QStringLiteral("arrange.rename"));
        ids.push_back(QStringLiteral("arrange.length"));
        ids.push_back(QStringLiteral("arrange.loop"));
        for (int grid = 0; grid < arrange::kGridCount; ++grid) {
          ids.push_back(QStringLiteral("arrange.grid.%1").arg(grid));
        }
        ids.push_back(QStringLiteral("arrange.zoomout"));
        ids.push_back(QStringLiteral("arrange.zoomin"));
        for (int clip = 0; clip < arrangeVm_->clipCount(); ++clip) {
          ids.push_back(QStringLiteral("arrange.clip.%1").arg(clip));
        }
        if (arrangeVm_->hasClipSelection()) {
          ids.push_back(QStringLiteral("arrange.clipmenu"));
        }
        ids.push_back(QStringLiteral("arrange.render"));
        break;
      }
      default:
        break;
    }
  }

  if (focusIndex_ >= static_cast<int>(ids.size())) {
    focusIndex_ = ids.empty() ? 0 : static_cast<int>(ids.size()) - 1;
  }
  focusByScreen_[tab_] = focusIndex_;
  const QString next = ids.empty() ? QString() : ids[static_cast<std::size_t>(focusIndex_)];
  const bool countChanged = ids.size() != focusIds_.size();
  const bool idChanged = next != focusId_;
  focusIds_ = std::move(ids);
  focusId_ = next;
  if (countChanged || idChanged) emit focusChanged();
}

void AppController::expirePendingRec() {
  if (!pendingRec_) return;
  if (nowMs() - pendingRecMs_ < ChordDetector::kWindowMs) return;
  pendingRec_ = false;
  chord_.reset();
  if (!audio_.recording()) toggleTake();
}

QString AppController::recModeText() const {
  return QString::fromStdString(
      ::mnky::app::recModeText(state_.recMode.load(), state_.countInBars.load()));
}

void AppController::openRecMode() {
  if (recording()) {
    // Changing the schedule mid-take would silently rewrite what the running
    // take means; stop first and say so.
    showNotice(NoticeWarn, QStringLiteral("stop the take before changing RECMODE"));
    return;
  }
  configVm_->setTrackModeIndex(mixerVm_->trackIndex());
  openConfigInternal(QStringLiteral("recmode"));
}

int AppController::trackRecSource(int slot) const { return mixer_.recSource(slot); }

void AppController::cycleTrackMode(int slot) {
  if (slot < 0 || slot >= audio::kMaxTracks) return;
  const int next = nextTrackMode(mixer_.mode(slot));
  mixerVm_->setMode(slot, next);
  showTextPopup(QStringLiteral("TRACK %1 MODE").arg(slot + 1, 2, 10, QLatin1Char('0')),
                QString::fromStdString(trackModeWord(next)));
  if (trackModeRecords(next)) {
    showNotice(NoticeInfo,
               QStringLiteral("TRACK %1 %2 - input %3")
                   .arg(slot + 1, 2, 10, QLatin1Char('0'))
                   .arg(QString::fromStdString(trackModeWord(next)))
                   .arg(mixerVm_->inSourceText(slot)));
  }
}

void AppController::openTrackMode(int slot) {
  if (slot < 0 || slot >= audio::kMaxTracks) slot = mixerVm_->trackIndex();
  configVm_->setTrackModeIndex(slot);
  openConfigInternal(QStringLiteral("trackmode"));
}

void AppController::applyTakeModeAfterStop(MixerModel::TrackMask recorded) {
  for (int track = 0; track < audio::kMaxTracks && track < MixerModel::kMaskBits;
       ++track) {
    if ((recorded & (MixerModel::TrackMask{1} << static_cast<unsigned>(track))) == 0) {
      continue;
    }
    // ARM recorded and is done, so it falls back to PLAY (take audible, live
    // input off). OVERDUB keeps layering: the user chose that mode.
    if (mixer_.mode(track) == kTrackModeArm) {
      mixerVm_->setMode(track, kTrackModePlay);
    }
  }
}

void AppController::takeAction(int action) {
  const int row = configVm_->takeTargetRow();
  switch (action) {
    case 0:  // ACTIVE
      recorderVm_->selectRow(row);
      recorderVm_->activateSelected();
      mixerVm_->notifyActiveTakesChanged();
      timelineVm_->refresh();
      showNotice(NoticeInfo, QStringLiteral("take %1 is ACTIVE").arg(
                                 recorderVm_->activeTake()));
      break;
    case 1:  // EDIT (the take edit popup, plan W5)
      requestEditTake(row);
      break;
    case 2:  // RENAME (file + mapping)
      recorderVm_->selectRow(row);
      requestRenameTake();
      break;
    case 3:  // DELETE (disk + project)
      recorderVm_->selectRow(row);
      requestDeleteTake();
      break;
    case 4:  // REMOVE (project only)
      recorderVm_->selectRow(row);
      requestRemoveTake();
      break;
    case 5:  // IMPORT
      requestImportTake();
      break;
    case 6:  // NEW TRACK FROM THIS TAKE
      requestNewTrackFromTake(row);
      break;
    default:
      break;
  }
}

void AppController::openTakeActions(int row) {
  if (recorderVm_->takes()->count() == 0) {
    showNotice(NoticeWarn, QStringLiteral("no takes yet - tap START TAKE"));
    return;
  }
  const int clamped = std::clamp(row, 0, recorderVm_->takes()->count() - 1);
  recorderVm_->selectRow(clamped);
  configVm_->setTakeTarget(clamped, recorderVm_->takeNameAt(clamped));
  openConfigInternal(QStringLiteral("takeactions"));
}

void AppController::requestEditTake(int row) {
  // Every EDIT affordance (take row button, "..." menu, take sheet) lands in
  // the same popup; the placeholder config kind is gone with the EDIT tab.
  openTakeEdit(row);
}

void AppController::requestRemoveTake() {
  const int row = recorderVm_->selectedRow();
  const auto* entry = recorderVm_->takes()->entry(row);
  if (entry == nullptr) {
    showNotice(NoticeWarn, QStringLiteral("select a row first (...)"));
    return;
  }
  requestConfirm(QStringLiteral("REMOVE TAKE ") + entry->name,
                 QStringLiteral("Remove from project? The file stays on disk."),
                 QStringLiteral("REMOVE"), QStringLiteral("take-remove"), row);
}

void AppController::requestNewTrackFromTake(int row) {
  const auto* entry = recorderVm_->takes()->entry(row);
  if (entry == nullptr) {
    showNotice(NoticeWarn, QStringLiteral("select a row first (...)"));
    return;
  }
  recorderVm_->selectRow(row);
  // Default name = the take stem (the `<name>` part of `<nn>-<name>-<n>`), so
  // the drums case reads `beat` without typing.
  const auto parts = parseTakeFileName(entry->path.filename().string());
  const QString initial =
      parts.valid && !parts.name.empty()
          ? QString::fromStdString(parts.name)
          : QString::fromStdString(entry->path.stem().string());
  requestKeypad(QStringLiteral("NEW TRACK FROM TAKE"), initial,
                QStringLiteral("new-track-from-take"), row);
}

void AppController::createTrackFromTake(int row, const QString& name, bool move) {
  const auto* entry = recorderVm_->takes()->entry(row);
  if (entry == nullptr) return;
  const QString takeKey = entry->key;
  const QString takeName = entry->name;
  const int slot = mixer_.firstInactiveSlot();
  if (slot < 0) {
    showNotice(NoticeError, QStringLiteral("no free track slot"));
    return;
  }
  // W3: input NONE, output MAIN, mode PLAY, ACTIVE = the take. The take file
  // keeps its name: the `NN-` prefix is convention, not binding.
  if (!mixerVm_->addTrack(slot, name, -1, false, 0)) {
    showNotice(NoticeError, QStringLiteral("could not add the track"));
    return;
  }
  mixerVm_->setMode(slot, kTrackModePlay);
  settings_.setActiveTake(slot, takeKey.toStdString());
  if (move) settings_.setActiveTake(recorderVm_->selectedTrack(), {});
  recorderVm_->resyncActiveTakes();
  mixerVm_->refresh();
  recorderVm_->tracksChanged();
  timelineVm_->refresh();
  if (move) {
    showNotice(NoticeInfo, QStringLiteral("track %1 %2 - TAKE %3")
                               .arg(slot + 1, 2, 10, QLatin1Char('0'))
                               .arg(name, takeName));
  } else {
    showNotice(NoticeInfo, QStringLiteral("track %1 %2 - TAKE %3 kept on track %4")
                               .arg(slot + 1, 2, 10, QLatin1Char('0'))
                               .arg(name, takeName)
                               .arg(recorderVm_->selectedTrack() + 1));
  }
}

void AppController::renderRegionToTrack() {
  const int track = timelineVm_->selectedTrack();
  if (!timelineVm_->selectionActive() ||
      timelineVm_->outBeats() <= timelineVm_->inBeats()) {
    showNotice(NoticeWarn,
               QStringLiteral("set IN and OUT before RENDER REGION TO TRACK"));
    return;
  }
  // The take the region is cut from is the session's active take for the track;
  // the engine resolves the key against the take folder.
  const auto takeKey = settings_.activeTake(track);
  if (takeKey.empty()) {
    showNotice(NoticeWarn, QStringLiteral("no active take to render"));
    return;
  }
  const auto parts = parseTakeFileName(std::filesystem::path(takeKey).filename().string());
  const std::string name =
      parts.valid && !parts.name.empty() ? parts.name : "region";
  std::filesystem::path path;
  std::string error;
  if (!audio_.renderRegionToTake(track, takeKey, timelineVm_->inBeats(),
                                 timelineVm_->outBeats(), name, &path, &error)) {
    // A refusal names its reason instead of faking a take.
    showNotice(NoticeError, QString::fromStdString(error));
    return;
  }
  pendingRenderPath_ = path;
  requestKeypad(QStringLiteral("NEW TRACK FROM RENDER"),
                QString::fromStdString(path.stem().string()),
                QStringLiteral("render-region"), -1);
}


bool AppController::songMode() const {
  return arrangeVm_ != nullptr && arrangeVm_->songMode();
}

void AppController::setSongMode(bool on) {
  if (arrangeVm_ == nullptr) return;
  arrangeVm_->setSongMode(on);
}

void AppController::toggleSongMode() {
  if (arrangeVm_ == nullptr) return;
  arrangeVm_->setSongMode(!arrangeVm_->songMode());
}

void AppController::requestArrangeSectionRename() {
  if (arrangeVm_ == nullptr) return;
  const int index = arrangeVm_->selectedSection();
  const QString name = arrangeVm_->selectedSectionName();
  if (index < 0 || name.isEmpty()) {
    showNotice(NoticeWarn, QStringLiteral("add or select a section first"));
    return;
  }
  requestKeypad(QStringLiteral("RENAME SECTION"), name,
                QStringLiteral("arrange-section-rename"), index);
}

void AppController::renderSong() {
  if (arrangeVm_ == nullptr) return;
  if (audio_.recording()) {
    showNotice(NoticeWarn, QStringLiteral("stop the take before RENDER SONG"));
    return;
  }
  if (arrangeVm_->clipCount() == 0) {
    showNotice(NoticeWarn, QStringLiteral("RENDER SONG needs at least one clip"));
    return;
  }
  // Re-apply at the current tempo (the engine refuses a lane resolved at a
  // different tempo) and read the settled length in bars.
  arrangeVm_->applySongToEngine();
  if (arrangeVm_->readableClipCount() == 0) {
    showNotice(NoticeError,
               QStringLiteral("RENDER SONG refused - no readable clip takes"));
    return;
  }
  const int bars = std::max(
      1, static_cast<int>(std::lround(std::ceil(arrangeVm_->lengthBeats() / 4.0))));
  const std::time_t now = std::time(nullptr);
  std::tm parts{};
#if defined(_WIN32)
  if (gmtime_s(&parts, &now) != 0) {
    showNotice(NoticeError, QStringLiteral("cannot read the clock"));
    return;
  }
#else
  if (gmtime_r(&now, &parts) == nullptr) {
    showNotice(NoticeError, QStringLiteral("cannot read the clock"));
    return;
  }
#endif
  char name[64] = {};
  std::snprintf(name, sizeof name, "song-%04d%02d%02d-%02d%02d%02d.wav",
                parts.tm_year + 1900, parts.tm_mon + 1, parts.tm_mday,
                parts.tm_hour, parts.tm_min, parts.tm_sec);
  const auto path = state_.takeDirectory() / name;
  std::error_code createError;
  std::filesystem::create_directories(state_.takeDirectory(), createError);
  if (createError) {
    showNotice(NoticeError, QStringLiteral("cannot create the take folder"));
    return;
  }
  std::filesystem::path written;
  std::string error;
  if (!audio_.renderSong(path.string(), bars, &written, &error)) {
    showNotice(NoticeError, QString::fromStdString(error));
    return;
  }
  recorderVm_->takeStateChanged();
  recorderVm_->refresh();
  // The master bounce is a take in the list (MASTER badge), never the track's
  // ACTIVE take: selecting it is a deliberate audition step.
  showNotice(NoticeInfo,
             QStringLiteral("SONG RENDERED ") +
                 QString::fromStdString(written.filename().string()) +
                 QStringLiteral(" - master mix rendered (main bus)"));
  showTextPopup(QStringLiteral("SONG RENDERED"),
                QString::fromStdString(written.filename().string()));
}

void AppController::openArrangeClipEdit(int track, const QString& takeKey) {
  if (takeKey.isEmpty()) return;
  recorderVm_->setSelectedTrack(track);
  settings_.setActiveTake(track, takeKey.toStdString());
  recorderVm_->resyncActiveTakes();
  mixerVm_->notifyActiveTakesChanged();
  timelineVm_->setSelectedTrack(track);
  timelineVm_->refresh();
  timelineVm_->openEdit(track);
  showNotice(NoticeInfo,
             QStringLiteral("EDIT %1 - non-destructive (RENDER writes a new take)")
                 .arg(takeKey));
}

void AppController::placePunchTakeClips(MixerModel::TrackMask recorded) {
  if (arrangeVm_ == nullptr || !settings_.songMode) return;
  int placed = 0;
  int bar = 0;
  for (int track = 0; track < audio::kMaxTracks && track < MixerModel::kMaskBits;
       ++track) {
    if ((recorded & (MixerModel::TrackMask{1} << static_cast<unsigned>(track))) == 0) {
      continue;
    }
    const std::string key = settings_.activeTake(track);
    if (key.empty()) continue;
    std::string error;
    const int clip = arrangeVm_->placePunchTake(track, key, punchPlaceStartBeats_,
                                                &error);
    if (clip < 0) {
      showNotice(NoticeWarn, QString::fromStdString(error));
      continue;
    }
    ++placed;
    const auto clips = arrangeVm_->clips();
    if (clip >= 0 && static_cast<qsizetype>(clip) < clips.size()) {
      const auto row = clips.at(clip).toMap();
      bar = arrange::barOfBeat(row.value(QStringLiteral("startBeats")).toDouble());
    }
  }
  if (placed > 0) {
    showNotice(NoticeInfo,
               QStringLiteral("TAKE SAVED -> clip at bar %1").arg(bar));
  }
}

std::uint32_t AppController::displayTick(const LinkStatus& status) const {
  return transportTick(status.tick, state_.transportOffsetTicks.load());
}

double AppController::displayBeats(const LinkStatus& status) const {
  return static_cast<double>(displayTick(status)) /
         static_cast<double>(kTicksPerBeat) * 4.0;
}

int AppController::songPunchBar(int loopRelativeMeasure) const {
  if (arrangeVm_ == nullptr) return loopRelativeMeasure;
  const double beats = arrange::punchStartBeats(
      loopRelativeMeasure, arrangeVm_->effectiveLoopStartBeats());
  return arrange::barOfBeat(beats);
}

void AppController::startTakeNow() {
  pendingRec_ = false;
  chord_.reset();
  punchPlaceArmed_ = false;
  punchEngaged_ = false;
  toggleTake();
}

void AppController::toggleTake() {
  if (audio_.recording()) {
    stopTake();
    return;
  }
  // W2: a COUNT-IN or PUNCH press does not write yet - the schedule starts the
  // transport (which zeroes the tick, so the count-in engages at bar 1) and
  // pollRecSchedule() opens the take on the measure boundary it names.
  const int recMode = clampRecMode(state_.recMode.load());
  if (recMode != kRecModeNow && !link_.status().rolling) {
    if (mixer_.armMask() == 0) {
      showNotice(NoticeError,
                 QStringLiteral("take refused (arm at least one strip)"));
      return;
    }
    armRecSchedule();
    return;
  }
  takeArmMask_ = mixer_.armMask();
  if (!audio_.startTake(state_.takeDirectory())) {
    showNotice(NoticeError, QStringLiteral("take refused (arm at least one strip)"));
    return;
  }
  state_.recording.store(true);
  clearNotice();
  link_.requestTransport(1);
  recorderVm_->takeStateChanged();
  updateTransport();
  updatePanelLeds();
}

void AppController::armRecSchedule() {
  const int recMode = clampRecMode(state_.recMode.load());
  if (recMode == kRecModeCountIn) {
    recSchedule_ = RecSchedule::CountIn;
  } else {
    // PUNCH records between PUNCH IN and PUNCH OUT; a disabled marker is a
    // no-op boundary, so the punch runs from the press to the OUT measure (or
    // to the stop).
    recSchedule_ = RecSchedule::PunchIn;
    recPunchOutBar_ = settings_.punch.out.enabled ? settings_.punch.out.measure : 0;
  }
  state_.recScheduled.store(true);
  recScheduleSawRolling_ = false;
  punchEngaged_ = false;
  punchPlaceArmed_ = false;
  clearNotice();
  // START (command 1) resets the tick, so the roll begins at bar 1 exactly as
  // the status line states.
  link_.requestTransport(1);
  updateTransport();
}

void AppController::cancelRecSchedule() {
  if (recSchedule_ == RecSchedule::None) return;
  recSchedule_ = RecSchedule::None;
  recPunchOutBar_ = 0;
  state_.recScheduled.store(false);
  updateTransport();
}

void AppController::pollRecSchedule(const LinkStatus& status) {
  if (recSchedule_ == RecSchedule::None) return;
  if (!status.rolling) {
    // The transport START is a queued command, so the first polls after the REC
    // press still read "stopped". Only a roll that has started and then stopped
    // cancels the schedule.
    if (recScheduleSawRolling_) cancelRecSchedule();
    return;
  }
  recScheduleSawRolling_ = true;
  const int rawBar = barOfTick(status.tick);
  const int bar = barOfTick(displayTick(status));
  if (recSchedule_ == RecSchedule::CountIn) {
    // The click rolls from bar 1 for n bars with the recorder silent; the take
    // then opens at bar 1 of the recorded timeline, which is what the status
    // line promises. The tick cannot go negative, so the recorded bar 1 is
    // reached by restarting the transport on the boundary (same tempo, same
    // click phase) instead of pre-rolling to bar -n.
    const int engageBar = clampCountInBars(state_.countInBars.load()) + 1;
    if (rawBar < engageBar) return;
    recSchedule_ = RecSchedule::None;
    state_.recScheduled.store(false);
    link_.requestTransport(0);
    link_.requestTransport(1);
    toggleTake();
    return;
  }
  const int punchInMeasure =
      settings_.punch.in.enabled ? settings_.punch.in.measure : 1;
  const int punchOutMeasure = recPunchOutBar_;
  // SONG mode punch points are loop-relative (W4/W5 addendum): a marker bar
  // counts from the active loop start. Free mode keeps the legacy absolute
  // measure interpretation.
  const int punchIn = settings_.songMode ? songPunchBar(punchInMeasure)
                                         : punchInMeasure;
  const int punchOut = settings_.songMode && punchOutMeasure > 0
                           ? songPunchBar(punchOutMeasure)
                           : punchOutMeasure;
  if (!audio_.recording()) {
    if (!punchEngaged_ && bar >= punchIn) {
      punchEngaged_ = true;
      recSchedule_ = RecSchedule::None;
      state_.recScheduled.store(false);
      toggleTake();
      if (audio_.recording()) {
        punchPlaceArmed_ = true;
        punchPlaceStartBeats_ = arrange::punchStartBeats(
            punchInMeasure, arrangeVm_ == nullptr
                                ? 0.0
                                : arrangeVm_->effectiveLoopStartBeats());
      }
    }
    return;
  }
  if (punchOut > 0 && bar >= punchOut) {
    // PUNCH OUT: the take stops writing, the transport keeps rolling so the
    // performance can continue past the punch.
    stopTake(/*keepRolling=*/true);
    cancelRecSchedule();
  }
}

void AppController::stopTake(bool keepRolling) {
  const auto take = audio_.stopTake();
  state_.recording.store(false);
  if (!keepRolling) link_.requestTransport(0);
  // Adopt the core recorder's chNN.wav files into the session-v2 per-track
  // names before the take list rescans. This is the app-side name policy; a
  // future Recorder API can take the template directly.
  if (take.files > 0 && !take.path.empty()) {
    std::string lastKey;
    recorderVm_->adoptRecordedTake(take.path, takeArmMask_, &lastKey);
    // W1 step 4: the recorded tracks switch to PLAY and the notice names the
    // take and the way to hear it. OVERDUB stays in OVERDUB (§9 decision): it
    // is a layering mode the user asked for, not a one-shot arm.
    applyTakeModeAfterStop(takeArmMask_);
    if (punchPlaceArmed_ && settings_.songMode) {
      // PUNCH INTO SONG: the written take becomes a clip at the punch in point.
      // The auto-place notice replaces the generic TAKE SAVED line.
      punchPlaceArmed_ = false;
      placePunchTakeClips(takeArmMask_);
    } else if (!lastKey.empty()) {
      // The notice names the take without its extension, exactly as the plan
      // writes it: `TAKE SAVED 02-bass-001 - mode PLAY to hear it`.
      std::string stem = std::filesystem::path(lastKey).filename().string();
      if (stem.size() > 4 && stem.compare(stem.size() - 4, 4, ".wav") == 0) {
        stem.resize(stem.size() - 4);
      }
      showNotice(NoticeInfo, QStringLiteral("TAKE SAVED ") +
                                 QString::fromStdString(stem) +
                                 QStringLiteral(" - mode PLAY to hear it"));
    }
  }
  punchPlaceArmed_ = false;
  punchEngaged_ = false;
  recorderVm_->takeStateChanged();
  if (take.files > 0) {
    log("recorder: take finished, " + std::to_string(take.files) + " files in " + take.path);
  }
  updateTransport();
  updatePanelLeds();
}

void AppController::updateTransport() {
  const auto status = link_.status();
  // A6/D1: one transport vocabulary - REC / PLAY / STOP. The strip used to say
  // ROLL while the recorder said ROLLING; both now name the musical state.
  const QString state = recording() ? QStringLiteral("REC")
                        : (status.rolling ? QStringLiteral("PLAY")
                                          : QStringLiteral("STOP"));
  // SONG mode reads the song timeline: the displayed tick carries the app-side
  // seek offset, so a tap on the ARRANGE playhead moves this bar:beat readout.
  const std::uint32_t positionTick = displayTick(status);
  const QString position =
      QString::fromStdString(formatBarBeatTick(positionTick)) +
      QStringLiteral(" / ") +
      QString::fromStdString(
          formatDuration(tickSeconds(positionTick, status.tempoX100)));
  const QString tempo = QString::fromStdString(formatTempo(status.tempoX100));
  bool changed = false;
  if (state != transportState_) {
    transportState_ = state;
    changed = true;
  }
  if (position != positionText_) {
    positionText_ = position;
    changed = true;
  }
  if (tempo != tempoText_) {
    tempoText_ = tempo;
    changed = true;
  }
  if (changed) emit transportChanged();

  const bool clickOn = state_.clickEnabled.load();
  const double level = state_.clickLevel.load();
  const int bars = state_.clickBars.load();
  const int mode = state_.clickMode.load();
  if (mode != 0) lastNonZeroClickMode_ = mode;
  const int output = state_.clickOutput.load();
  const double gain = state_.clickGainDb.load();
  if (clickOn != lastClickEnabled_ || std::abs(level - lastClickLevel_) > 0.0001 ||
      bars != lastClickBars_ || mode != lastClickMode_ || output != lastClickOutput_ ||
      std::abs(gain - lastClickGainDb_) > 0.0001) {
    lastClickEnabled_ = clickOn;
    lastClickLevel_ = level;
    lastClickBars_ = bars;
    lastClickMode_ = mode;
    lastClickOutput_ = output;
    lastClickGainDb_ = gain;
    emit clickChanged();
  }

  const double left = audio_.peak(0);
  const double right = audio_.peak(1);
  mixerVm_->setMeters(left, right);
  std::array<double, audio::kMaxTracks> stripLevels{};
  for (int strip = 0; strip < audio::kMaxTracks; ++strip) {
    stripLevels[static_cast<std::size_t>(strip)] =
        meterFraction(audio_.stripPeak(strip));
  }
  mixerVm_->setStripMeters(stripLevels);
  if (std::abs(left - masterPeakLeft_) > 0.001 ||
      std::abs(right - masterPeakRight_) > 0.001) {
    masterPeakLeft_ = left;
    masterPeakRight_ = right;
    emit metersChanged();
  }
}

void AppController::updateDirtyState() {
  const auto fingerprint = project::fingerprint(*context_);
  const bool before = state_.dirty.load();
  if (dirtyTracker_.poll(fingerprint)) state_.markDirty();
  if (!before && state_.dirty.load()) emit dirtyChanged();
}

void AppController::projectStateChanged() {
  // A project save/load/new resets the dirty baseline; FX params may have been
  // replaced behind the FX screen, so rebuild its parameter models before
  // sampling the new fingerprint.
  fxVm_->refresh();
  routingVm_->refresh();
  mixerVm_->refresh();
  sessionVm_->refresh();
  // A project load/new starts the song transport at bar 1; a stale seek offset
  // from the previous project must not shift it.
  state_.transportOffsetTicks.store(0);
  recorderVm_->takeStateChanged();
  recorderVm_->resyncActiveTakes();
  timelineVm_->reloadFromSettings();
  arrangeVm_->reloadFromSettings();
  audio_.applyMonitorSettings(settings_);
  audio_.applyTakeLoops(settings_);
  audio_.applyEq(eqModel_.capture());
  sessionVm_->pushClockToLink();
  toolsVm_->refresh();
  clearNotice();
  state_.clearDirty();
  dirtyTracker_.clear(project::fingerprint(*context_));
  emit dirtyChanged();
}

void AppController::updatePanelLeds() {
  // No-Pico mode has no panel: there is no LED command to send and no Pico state
  // machine to feed. Keep the last mask untouched so a hardware run that is
  // restarted in no-Pico mode does not emit a stale frame.
  if (noPico_) return;
  std::uint8_t mask = 0;
  std::uint8_t states = 0;
  if (audio_.recording() || state_.recording.load()) {
    mask = static_cast<std::uint8_t>(mask | mb::LED_REC);
    states = static_cast<std::uint8_t>(states | mb::LED_REC);
  }
  if (state_.overdub.load()) {
    mask = static_cast<std::uint8_t>(mask | mb::LED_OVERDUB);
    states = static_cast<std::uint8_t>(states | mb::LED_OVERDUB);
  }
  if (link_.status().rolling) {
    mask = static_cast<std::uint8_t>(mask | mb::LED_PLAY);
    states = static_cast<std::uint8_t>(states | mb::LED_PLAY);
  }
  if (mask == lastLedMask_ && states == lastLedStates_) return;
  lastLedMask_ = mask;
  lastLedStates_ = states;
  link_.requestLeds(mask, states);
}

void AppController::registerLearnTargets() {
  for (int strip = 0; strip < kNumStrips; ++strip) {
    midi_.registerLearnTarget(learnTarget(strip, "gain"),
                              [this, strip](double value) {
                                mixer_.setGainDb(strip, static_cast<float>(
                                                             kMinGainDb +
                                                             value * (kMaxGainDb - kMinGainDb)));
                              });
    midi_.registerLearnTarget(learnTarget(strip, "pan"),
                              [this, strip](double value) {
                                mixer_.setPan(strip, static_cast<float>(value));
                              });
    // FX1/FX2 are the rack slots' wet now: the target names say so, so an old
    // "sendA"/"sendB" binding to a retired bus does not silently keep writing.
    midi_.registerLearnTarget(learnTarget(strip, "fx1wet"),
                              [this, strip](double value) {
                                mixer_.setFxWet(strip, 0, static_cast<float>(value));
                              });
    midi_.registerLearnTarget(learnTarget(strip, "fx2wet"),
                              [this, strip](double value) {
                                mixer_.setFxWet(strip, 1, static_cast<float>(value));
                              });
  }
  midi_.registerLearnTarget("mixer.master.gain", [this](double value) {
    mixer_.setMasterDb(static_cast<float>(kMinGainDb + value * (kMaxGainDb - kMinGainDb)));
  });
  midi_.registerLearnTarget("click.level", [this](double value) {
    state_.clickLevel.store(static_cast<float>(value));
  });
  // MIDI FX pipe parameters. The path set is fixed (9 routes x 8 pipe slots x
  // 10 positional parameters); a setter is a no-op until that pipe/param exists,
  // so the LearnEngine can hold a binding across a chain edit. The value range
  // comes from the generated schema metadata (pipe_ui.hpp) at dispatch time,
  // because a slot's pipe type can change after the binding was made.
  for (int source = 0; source < routing::kInputCount; ++source) {
    for (int destination = 0; destination < routing::kOutputCount; ++destination) {
      for (int pipe = 0; pipe < static_cast<int>(midi::kMaxRoutePipes); ++pipe) {
        for (int param = 0; param < static_cast<int>(midi::kMaxPipeParams); ++param) {
          const auto path = pipeui::learnTargetPath(source, destination, pipe, param);
          midi_.registerLearnTarget(path, [this, source, destination, pipe,
                                           param](double value) {
            const auto input = routing::kInputs[static_cast<std::size_t>(source)];
            const auto output = routing::kOutputs[static_cast<std::size_t>(destination)];
            const auto pipes = midi_.routePipes(input, output);
            if (pipe < 0 || pipe >= static_cast<int>(pipes.size())) return;
            const auto* spec =
                pipeui::pipeSpec(static_cast<int>(pipes[static_cast<std::size_t>(pipe)].type));
            if (spec == nullptr || param < 0 ||
                param >= static_cast<int>(spec->paramCount)) {
              return;
            }
            const auto& paramSpec = spec->params[static_cast<std::size_t>(param)];
            const int scaled = paramSpec.min + static_cast<int>(std::lround(
                                                   std::clamp(value, 0.0, 1.0) *
                                                   (paramSpec.max - paramSpec.min)));
            (void)midi_.setRoutePipeParam(input, output, pipe, param, scaled);
          });
        }
      }
    }
  }
}

void AppController::showNotice(NoticeKind kind, const QString& notice) {
  noticeText_ = notice;
  noticeKind_ = kind == NoticeInfo ? QStringLiteral("info")
                : (kind == NoticeWarn ? QStringLiteral("warn") : QStringLiteral("error"));
  noticeTimer_.stop();
  // D2: info 6 s, warn 10 s, error until the next successful action of its
  // domain calls clearNotice().
  if (kind == NoticeInfo) noticeTimer_.start(6000);
  else if (kind == NoticeWarn) noticeTimer_.start(10000);
  emit noticeChanged();
}

void AppController::clearNotice() {
  if (noticeText_.isEmpty()) return;
  noticeTimer_.stop();
  noticeText_.clear();
  emit noticeChanged();
}

void AppController::showValuePopup(const QString& label, const QString& kind,
                                   double value) {
  std::string text;
  if (kind == QStringLiteral("pan")) {
    text = formatPanValue(static_cast<float>(value));
  } else if (kind == QStringLiteral("db")) {
    text = formatDbValue(static_cast<float>(value));
  } else if (kind == QStringLiteral("percent")) {
    text = formatPercentValue(static_cast<float>(value));
  } else if (kind == QStringLiteral("ratio")) {
    text = formatRatioValue(static_cast<float>(value));
  } else if (kind == QStringLiteral("ms")) {
    text = formatMillisecondsValue(static_cast<float>(value));
  } else if (kind == QStringLiteral("bpm")) {
    text = formatTempo(static_cast<int>(std::lround(value)));
  } else if (kind == QStringLiteral("int")) {
    text = formatIntValue(value);
  } else if (kind == QStringLiteral("hz")) {
    text = formatHzValue(value);
  } else if (kind == QStringLiteral("q")) {
    text = formatQValue(value);
  } else {
    text = formatGenericValue(value);
  }
  showTextPopup(label, QString::fromStdString(text));
}

void AppController::showTextPopup(const QString& label, const QString& text) {
  valuePopupLabel_ = label;
  valuePopupText_ = text;
  valuePopupVisible_ = true;
  valuePopupTimer_.start();
  emit valuePopupChanged();
}

void AppController::hideValuePopup() {
  if (!valuePopupVisible_) return;
  valuePopupVisible_ = false;
  valuePopupTimer_.stop();
  emit valuePopupChanged();
}

void AppController::announce(const QString& kind, const QString& text) {
  const NoticeKind severity = kind == QStringLiteral("warn")   ? NoticeWarn
                              : kind == QStringLiteral("error") ? NoticeError
                                                                : NoticeInfo;
  showNotice(severity, text);
}

void AppController::setTouchAnchorY(double y) {
  const double value = std::isfinite(y) && y >= 0.0 ? y : -1.0;
  if (std::abs(value - touchAnchorY_) < 0.5) return;
  touchAnchorY_ = value;
  emit touchAnchorChanged();
}

void AppController::popupGain(const QString& label, int strip) {
  showValuePopup(label, QStringLiteral("db"), mixerVm_->gainDbAt(strip));
}

void AppController::popupPan(const QString& label, int strip) {
  showValuePopup(label, QStringLiteral("pan"), mixerVm_->panAt(strip));
}

void AppController::popupFxWet(const QString& label, int strip, int slot) {
  showValuePopup(label, QStringLiteral("percent"), mixerVm_->fxWetAt(strip, slot));
}

QVariantMap AppController::paramCard(int track, int slot, int param) const {
  for (const auto& entry : mixerVm_->fxParams(track, slot)) {
    const auto card = entry.toMap();
    if (card.value(QStringLiteral("index")).toInt() == param) return card;
  }
  return {};
}

}  // namespace mnky::app

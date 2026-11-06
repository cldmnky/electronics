#include "app_controller.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>

#include "logging.hpp"
#include "project_io.hpp"
#include "routing_model.hpp"

namespace mnky::app {
namespace {
constexpr int kProjectsTabIndex = 5;
constexpr int kMonitorTabIndex = 6;
constexpr int kEqTabIndex = 7;
constexpr int kXyTabIndex = 8;
constexpr int kEditTabIndex = 9;

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

  mixerVm_ = std::make_unique<MixerViewModel>(mixer_, settings_);
  routingVm_ = std::make_unique<RoutingViewModel>(midi_, noPico_);
  fxVm_ = std::make_unique<FxViewModel>(fx_, state_, mixer_, audio_.sampleRate());
  recorderVm_ =
      std::make_unique<RecorderViewModel>(state_, audio_, mixer_, link_, settings_);
  timelineVm_ = std::make_unique<TimelineViewModel>(state_, audio_, *recorderVm_,
                                                    link_, settings_);
  projectsVm_ = std::make_unique<ProjectsViewModel>(
      *context_, [this] { projectStateChanged(); });
  systemVm_ = std::make_unique<SystemViewModel>(link_, audio_, midi_, fx_, state_);
  learnVm_ = std::make_unique<LearnViewModel>(midi_);
  sessionVm_ = std::make_unique<SessionViewModel>(state_, mixer_, link_, settings_);
  midiMonitorVm_ = std::make_unique<MidiMonitorViewModel>(midi_, noPico_);
  toolsVm_ = std::make_unique<ToolsViewModel>(state_, link_);
  configVm_ =
      std::make_unique<ConfigPopupModel>(state_, mixer_, settings_, eqModel_, performance_);
  configVm_->setTimeline(timelineVm_.get());
  configVm_->setRouting(routingVm_.get());
  eqVm_ = std::make_unique<EqViewModel>(eqModel_);
  xyVm_ = std::make_unique<XyViewModel>(performance_);
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
  fxVm_->loadDefaults();
  audio_.applyMonitorSettings(settings_);
  audio_.applyTakeLoops(settings_);
  audio_.applyEq(eqModel_.capture());
  timelineVm_->reloadFromSettings();
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
  if (clamped == tab_) return;
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
  state_.clickMode.store(enabled ? 2 : 0);
  state_.clickEnabled.store(enabled);
  emit clickChanged();
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
  const int clamped = std::clamp(index, 0, kNumStrips - 1);
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
      setNotice(QStringLiteral("panic: CC123 sent on all 16 channels"));
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
    setNotice(QStringLiteral("tap tempo: %1/3 taps").arg(static_cast<int>(tapTimes_.size())));
    return;
  }
  // Average the last four intervals so one late tap does not dominate.
  const std::size_t count = std::min<std::size_t>(tapTimes_.size(), 5);
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
  setNotice(QStringLiteral("tap tempo: ") +
            QString::fromStdString(formatTempo(bpmX100)));
  updateTransport();
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
  } else if (keypadAction_ == QStringLiteral("take-import")) {
    error = recorderVm_->importTake(text);
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

void AppController::requestDeleteProject(int row) {
  projectsVm_->selectRow(row);
  requestConfirm(QStringLiteral("DELETE PROJECT"),
                 QStringLiteral("Delete ") + projectsVm_->projectName(row) +
                     QStringLiteral("? The folder and its project.mnkybox are removed. "
                                    "This cannot be undone."),
                 QStringLiteral("DELETE"), QStringLiteral("project-delete"), row);
}

void AppController::requestRenameProject() {
  const int row = projectsVm_->selectedRow();
  if (row < 0) {
    setNotice(QStringLiteral("select a project to rename"));
    return;
  }
  requestKeypad(QStringLiteral("RENAME PROJECT"), projectsVm_->projectName(row),
                QStringLiteral("project-rename"), row);
}

void AppController::requestRenameTake() {
  const int row = recorderVm_->selectedRow();
  const auto* entry = recorderVm_->takes()->entry(row);
  if (entry == nullptr) {
    setNotice(QStringLiteral("select a take to rename"));
    return;
  }
  requestKeypad(QStringLiteral("RENAME TAKE"),
                QString::fromStdString(entry->path.filename().string()),
                QStringLiteral("take-rename"), row);
}

void AppController::requestDeleteTake() {
  const int row = recorderVm_->selectedRow();
  const auto* entry = recorderVm_->takes()->entry(row);
  if (entry == nullptr) {
    setNotice(QStringLiteral("select a take to delete"));
    return;
  }
  requestConfirm(QStringLiteral("DELETE TAKE"),
                 QStringLiteral("Delete ") + entry->name +
                     QStringLiteral("? The WAV file is removed from the take folder. This "
                                    "cannot be undone."),
                 QStringLiteral("DELETE"), QStringLiteral("take-delete"), row);
}

void AppController::requestImportTake() {
  requestKeypad(QStringLiteral("IMPORT TAKE (WAV PATH)"),
                QString::fromStdString(state_.takeDirectory().string()) +
                    QLatin1Char('/'),
                QStringLiteral("take-import"), -1);
}

void AppController::openDemoKeypad() {
  requestKeypad(QStringLiteral("RENAME PROJECT"), QStringLiteral("Project-demo"),
                QStringLiteral("project-rename"), -1);
}

void AppController::showShiftFace(bool on) {
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
  state_.rolling.store(status.rolling);
  state_.tempoX100.store(status.tempoX100);
  if (status.tempoX100 != loopTempoX100_) {
    loopTempoX100_ = status.tempoX100;
    audio_.applyTakeLoops(settings_);
  }
  updateShiftState(status);
  updateTransport();

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
      if (configVm_ && configVm_->open()) {
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
  if (id == QStringLiteral("timeline.in") || id == QStringLiteral("timeline.out")) {
    const double stepBeats = shiftHeld_ ? timeline::kStepCoarseBeats
                                        : timeline::kStepFineBeats;
    const QString text = id == QStringLiteral("timeline.in")
                             ? timelineVm_->nudgeIn(dx * stepBeats)
                             : timelineVm_->nudgeOut(dx * stepBeats);
    showTextPopup(id == QStringLiteral("timeline.in") ? QStringLiteral("REGION IN")
                                                      : QStringLiteral("REGION OUT"),
                  text);
    return true;
  }
  if (id == QStringLiteral("eq.band.select") || id.startsWith(QStringLiteral("eq.node."))) {
    eqVm_->nudgeSelected(dx, dy, shiftHeld_);
    return true;
  }
  if (id == QStringLiteral("xy.delay") || id == QStringLiteral("xy.reverb")) {
    const int grid = id == QStringLiteral("xy.reverb") ? PerformanceModel::kReverbGrid
                                                      : PerformanceModel::kDelayGrid;
    xyVm_->nudge(grid, dx, dy, shiftHeld_);
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
           id.contains(QStringLiteral("sendA")) || id.contains(QStringLiteral("sendB")) ||
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
    return id.endsWith(QStringLiteral(".return")) || id.contains(QStringLiteral(".param."));
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
  if (id == QStringLiteral("tools.brightness")) return true;
  if (id == QStringLiteral("learn.target")) return true;
  if (id == QStringLiteral("timeline.cursor") || id == QStringLiteral("timeline.in") ||
      id == QStringLiteral("timeline.out")) {
    return true;
  }
  return false;
}

void AppController::activateFocus() {
  QString focusId = focusId_;
  if (focusId.startsWith(QStringLiteral("shift."))) focusId = focusId.mid(6);
  if (focusId.isEmpty()) return;
  const auto parts = focusId.split(QLatin1Char('.'));

  if (configVm_ && configVm_->open() && parts.size() >= 2 &&
      parts[0] == QStringLiteral("config")) {
    const int index = parts.size() >= 3 ? parts[2].toInt() : 0;
    if (parts[1] == QStringLiteral("ok")) {
      configAccept();
    } else if (parts[1] == QStringLiteral("cancel")) {
      configCancel();
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
  if (focusId == QStringLiteral("transport.click")) {
    setClickEnabled(!clickEnabled());
    return;
  }
  if (focusId == QStringLiteral("mixer.mode.0")) {
    mixerVm_->setTrackMode(false);
    return;
  }
  if (focusId == QStringLiteral("mixer.mode.1")) {
    mixerVm_->setTrackMode(true);
    return;
  }

  if (parts.size() >= 2 && parts[0] == QStringLiteral("track")) {
    if (parts[1] == QStringLiteral("select") && parts.size() >= 3) {
      mixerVm_->setTrackIndex(parts[2].toInt());
    } else if (parts[1] == QStringLiteral("rec")) {
      mixerVm_->toggleArm(mixerVm_->trackIndex());
    } else if (parts[1] == QStringLiteral("mute")) {
      mixerVm_->toggleMute(mixerVm_->trackIndex());
    } else if (parts[1] == QStringLiteral("solo")) {
      mixerVm_->toggleSolo(mixerVm_->trackIndex());
    } else if (parts[1] == QStringLiteral("monitor") && parts.size() >= 3) {
      mixerVm_->setTrackMonitor(parts[2].toInt());
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
      mixerVm_->setBank(parts[2].toInt());
      return;
    }
    if (what == QStringLiteral("master")) {
      setPanelMode(QStringLiteral("edit"));
      return;
    }
    const int strip = parts[2].toInt();
    selectStrip(strip);
    if (what == QStringLiteral("gain") || what == QStringLiteral("pan") ||
        what == QStringLiteral("sendA") || what == QStringLiteral("sendB") ||
        what == QStringLiteral("fx1") || what == QStringLiteral("fx2") ||
        what == QStringLiteral("cue") || what == QStringLiteral("out34")) {
      setPanelMode(QStringLiteral("edit"));
    } else if (what == QStringLiteral("mute")) {
      mixerVm_->toggleMute(strip);
    } else if (what == QStringLiteral("solo")) {
      mixerVm_->toggleSolo(strip);
    } else if (what == QStringLiteral("arm")) {
      mixerVm_->toggleArm(strip);
    } else if (what == QStringLiteral("monitor")) {
      const int next = (mixerVm_->data(mixerVm_->index(strip),
                                      MixerViewModel::MonitorRole).toInt() + 1) % 4;
      mixerVm_->setMonitor(strip, next);
      // MPC parity: PLAY = take only, AUTO = smart, BOTH = input + take,
      // MON = input always / take never (session::MixerChannelState::monitor).
      showTextPopup(QStringLiteral("INPUT MON"),
                    next == 0 ? QStringLiteral("Play")
                    : (next == 1 ? QStringLiteral("Auto")
                                 : (next == 2 ? QStringLiteral("Both")
                                              : QStringLiteral("Mon"))));
    } else if (what == QStringLiteral("in")) {
      // Compact strip input source: EDIT steps it like the TRACK SHIFT picker.
      // MONO/PAIR stays on the TRACK SHIFT face (its own PAIR button) and on the
      // strip's long press, so the encoder never has two meanings.
      setPanelMode(QStringLiteral("edit"));
      showTextPopup(QStringLiteral("IN %1 INPUT").arg(strip + 1, 2, 10, QLatin1Char('0')),
                    mixerVm_->inSourceText(strip));
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
      if (what == QStringLiteral("src") && parts.size() >= 4) {
        if (parts[3] == QStringLiteral("prev")) {
          routingVm_->cyclePipeSource(-1);
        } else if (parts[3] == QStringLiteral("next") ||
                   parts[3] == QStringLiteral("name")) {
          routingVm_->cyclePipeSource(1);
        } else {
          routingVm_->setPipeSource(parts[3].toInt());
        }
        showTextPopup(QStringLiteral("PIPE FROM"), routingVm_->pipeSourceName());
      } else if (what == QStringLiteral("dst") && parts.size() >= 4) {
        if (parts[3] == QStringLiteral("prev")) {
          routingVm_->cyclePipeDestination(-1);
        } else if (parts[3] == QStringLiteral("next") ||
                   parts[3] == QStringLiteral("name")) {
          routingVm_->cyclePipeDestination(1);
        } else {
          routingVm_->setPipeDestination(parts[3].toInt());
        }
        showTextPopup(QStringLiteral("PIPE TO"), routingVm_->pipeDestinationName());
      } else if (what == QStringLiteral("scope")) {
        openConfig(QStringLiteral("pipechannels"));
      } else if (what == QStringLiteral("type")) {
        routingVm_->cycleAddType(parts.size() >= 4 && parts[3] == QStringLiteral("prev") ? -1 : 1);
      } else if (what == QStringLiteral("add") && parts.size() >= 4 &&
                 parts[3] == QStringLiteral("type")) {
        routingVm_->cycleAddType(1);
        showTextPopup(QStringLiteral("PIPE TYPE"), routingVm_->addTypeName());
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
        if (parts.size() >= 4 && parts[3] == QStringLiteral("page")) {
          const int delta = parts.size() >= 5 && parts[4] == QStringLiteral("prev") ? -1 : 1;
          routingVm_->setParamPage(routingVm_->paramPage() + delta);
          showTextPopup(QStringLiteral("PIPE PARAMS"),
                        QStringLiteral("page %1 / %2")
                            .arg(routingVm_->paramPage() + 1)
                            .arg(routingVm_->paramPageCount()));
        } else {
          setPanelMode(QStringLiteral("edit"));
        }
      }
      rebuildFocusIds();
    } else if (parts[1] == QStringLiteral("reset")) {
      routingVm_->reset();
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

  if (parts.size() >= 3 && parts[0] == QStringLiteral("fx")) {
    const int slot = parts[1] == QStringLiteral("B") ? 1 : 0;
    const QString what = parts[2];
    auto* view = slot == 1 ? fxVm_->slotB() : fxVm_->slotA();
    if (what == QStringLiteral("plugin")) {
      view->loadDefault();
    } else if (what == QStringLiteral("eject")) {
      view->eject();
    } else if (what == QStringLiteral("save")) {
      view->saveState();
    } else if (what == QStringLiteral("load")) {
      view->loadState();
    } else if (what == QStringLiteral("bypass")) {
      view->toggleBypass();
    } else if (what == QStringLiteral("return") || what == QStringLiteral("param")) {
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
      if (parts.size() >= 3 && parts[2] == QStringLiteral("params")) {
        // Freeze owns every reverb parameter until it is released, so the
        // popup would accept and then write nothing: say so instead.
        if (parts[1] == QStringLiteral("reverb") && xyVm_->frozen()) {
          showTextPopup(QStringLiteral("REVERB"),
                        QStringLiteral("FREEZE locks the reverb params"));
        } else {
          openConfig(parts[1]);
        }
      } else {
        setPanelMode(QStringLiteral("edit"));
      }
    }
    return;
  }

  if (parts.size() >= 2 && parts[0] == QStringLiteral("timeline")) {
    if (parts[1] == QStringLiteral("page") && parts.size() >= 3) {
      if (parts[2] == QStringLiteral("prev")) timelineVm_->prevPage();
      else timelineVm_->nextPage();
    } else if (parts[1] == QStringLiteral("view") && parts.size() >= 3) {
      timelineVm_->scrollView(parts[2] == QStringLiteral("prev") ? -1 : 1);
    } else if (parts[1] == QStringLiteral("zoom") && parts.size() >= 3) {
      timelineVm_->zoom(parts[2] == QStringLiteral("in") ? 1 : -1);
    } else if (parts[1] == QStringLiteral("lane") && parts.size() >= 3) {
      timelineVm_->setSelectedTrack(parts[2].toInt());
      timelineVm_->setCursorBeats(timelineVm_->cursorBeats());
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
    } else if (parts[1] == QStringLiteral("ops")) {
      openConfig(QStringLiteral("timelineops"));
    } else if (parts[1] == QStringLiteral("undo")) {
      if (timelineVm_->undo()) {
        showTextPopup(QStringLiteral("EDIT UNDO"), timelineVm_->undoText());
      } else {
        showTextPopup(QStringLiteral("EDIT UNDO"), QStringLiteral("nothing to undo"));
      }
    }
    return;
  }

  if (parts.size() >= 2 && parts[0] == QStringLiteral("learn")) {
    if (parts[1] == QStringLiteral("target")) setPanelMode(QStringLiteral("edit"));
  }
}

void AppController::adjustFocused(int detents, bool fine) {
  QString focusId = focusId_;
  if (focusId.startsWith(QStringLiteral("shift."))) focusId = focusId.mid(6);
  if (focusId.isEmpty() || detents == 0) return;
  const auto parts = focusId.split(QLatin1Char('.'));
  const double step = static_cast<double>(detents);

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
      mixerVm_->setGainDb(strip, mixerVm_->data(mixerVm_->index(strip), MixerViewModel::GainDbRole).toDouble() +
                                     step * (fine ? 0.1 : 0.5));
      popupGain(QStringLiteral("IN %1 GAIN").arg(strip + 1, 2, 10, QLatin1Char('0')),
                strip);
    } else if (what == QStringLiteral("pan")) {
      mixerVm_->setPan(strip, mixerVm_->data(mixerVm_->index(strip), MixerViewModel::PanRole).toDouble() +
                                  step * (fine ? 0.005 : 0.02));
      popupPan(QStringLiteral("IN %1 PAN").arg(strip + 1, 2, 10, QLatin1Char('0')),
               strip);
    } else if (what == QStringLiteral("sendA")) {
      mixerVm_->setSend(strip, 0,
                        mixerVm_->data(mixerVm_->index(strip), MixerViewModel::SendARole).toDouble() +
                            step * (fine ? 0.005 : 0.02));
      popupSend(QStringLiteral("IN %1 FX1").arg(strip + 1, 2, 10, QLatin1Char('0')),
                strip, 0);
    } else if (what == QStringLiteral("sendB")) {
      mixerVm_->setSend(strip, 1,
                        mixerVm_->data(mixerVm_->index(strip), MixerViewModel::SendBRole).toDouble() +
                            step * (fine ? 0.005 : 0.02));
      popupSend(QStringLiteral("IN %1 FX2").arg(strip + 1, 2, 10, QLatin1Char('0')),
                strip, 1);
    } else if (what == QStringLiteral("fx1")) {
      mixerVm_->setSend(strip, 0, mixerVm_->data(mixerVm_->index(strip), MixerViewModel::SendARole).toDouble() +
                                          step * (fine ? 0.005 : 0.02));
      popupSend(QStringLiteral("IN %1 FX1").arg(strip + 1, 2, 10, QLatin1Char('0')),
                strip, 0);
    } else if (what == QStringLiteral("fx2")) {
      mixerVm_->setSend(strip, 1, mixerVm_->data(mixerVm_->index(strip), MixerViewModel::SendBRole).toDouble() +
                                          step * (fine ? 0.005 : 0.02));
      popupSend(QStringLiteral("IN %1 FX2").arg(strip + 1, 2, 10, QLatin1Char('0')),
                strip, 1);
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
    } else if (parts[1] == QStringLiteral("fx1")) {
      mixerVm_->setTrackSendA(mixerVm_->trackSendA() + step * (fine ? 0.005 : 0.02));
      showValuePopup(QStringLiteral("FX1"), QStringLiteral("percent"),
                     mixerVm_->trackSendA());
    } else if (parts[1] == QStringLiteral("fx2")) {
      mixerVm_->setTrackSendB(mixerVm_->trackSendB() + step * (fine ? 0.005 : 0.02));
      showValuePopup(QStringLiteral("FX2"), QStringLiteral("percent"),
                     mixerVm_->trackSendB());
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
      const int grid = parts[1] == QStringLiteral("reverb")
                           ? PerformanceModel::kReverbGrid
                           : PerformanceModel::kDelayGrid;
      xyVm_->nudge(grid, 0, detents > 0 ? 1 : -1, fine);
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

  if (parts.size() >= 4 && parts[0] == QStringLiteral("fx")) {
    const int slot = parts[1] == QStringLiteral("B") ? 1 : 0;
    auto* view = slot == 1 ? fxVm_->slotB() : fxVm_->slotA();
    if (parts[2] == QStringLiteral("return")) {
      view->setReturnDb(view->returnDb() + step * (fine ? 0.1 : 0.5));
      showValuePopup(QStringLiteral("SEND %1 RETURN").arg(slot == 1 ? "B" : "A"),
                     QStringLiteral("db"), view->returnDb());
    } else if (parts[2] == QStringLiteral("param")) {
      const int index = parts[3].toInt();
      const auto params = fx_.slot(slot).params();
      if (index >= 0 && index < static_cast<int>(params.size())) {
        const auto& param = params[static_cast<std::size_t>(index)];
        const double range = std::max(0.0, param.max - param.min);
        const double paramStep = range / (fine ? 1000.0 : 100.0);
        view->adjustParam(index, step * paramStep);
        const auto updated = fx_.slot(slot).params();
        if (index < static_cast<int>(updated.size())) {
          showValuePopup(QString::fromStdString(updated[static_cast<std::size_t>(index)].name),
                         QStringLiteral("generic"),
                         updated[static_cast<std::size_t>(index)].value);
        }
      }
    }
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
                  learnVm_->targets().value(learnVm_->targetIndex()));
  }
}

void AppController::rebuildFocusIds() {
  std::vector<QString> ids;
  if (configVm_ && configVm_->open()) {
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
      ids.push_back(QStringLiteral("config.delay.div.prev"));
      ids.push_back(QStringLiteral("config.delay.division"));
      ids.push_back(QStringLiteral("config.delay.div.next"));
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
      for (int op = 0; op < timeline::kTimelineOpCount; ++op) {
        ids.push_back(QStringLiteral("config.timeline.op.%1").arg(op));
      }
      ids.push_back(QStringLiteral("config.timeline.gain"));
      ids.push_back(QStringLiteral("config.timeline.fadein"));
      ids.push_back(QStringLiteral("config.timeline.fadeout"));
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
  } else if (shiftFace_) {
    switch (tab_) {
      case 0:
        if (mixerVm_->trackMode()) {
          ids.push_back(QStringLiteral("shift.track.input"));
          ids.push_back(QStringLiteral("shift.track.input.pair"));
          ids.push_back(QStringLiteral("shift.track.output"));
          ids.push_back(QStringLiteral("shift.track.gain"));
          ids.push_back(QStringLiteral("shift.track.fx1"));
          ids.push_back(QStringLiteral("shift.track.fx2"));
          ids.push_back(QStringLiteral("shift.track.cue"));
          ids.push_back(QStringLiteral("shift.track.out34"));
          for (const char* destination : {"cue", "fx1", "fx2", "out34", "rec"}) {
            ids.push_back(QStringLiteral("shift.monitor.send.%1.pre").arg(QLatin1String(destination)));
            ids.push_back(QStringLiteral("shift.monitor.send.%1.post").arg(QLatin1String(destination)));
          }
          for (int mode = 0; mode < 4; ++mode) {
            ids.push_back(QStringLiteral("shift.track.monitor.%1").arg(mode));
          }
        } else {
          const int first = mixerVm_->bank() * kStripsPerBank;
          for (int i = 0; i < kStripsPerBank; ++i) {
            const int strip = first + i;
            ids.push_back(QStringLiteral("shift.mixer.gain.%1").arg(strip));
            ids.push_back(QStringLiteral("shift.mixer.fx1.%1").arg(strip));
            ids.push_back(QStringLiteral("shift.mixer.fx2.%1").arg(strip));
            ids.push_back(QStringLiteral("shift.mixer.cue.%1").arg(strip));
            ids.push_back(QStringLiteral("shift.mixer.out34.%1").arg(strip));
            ids.push_back(QStringLiteral("shift.mixer.monitor.%1").arg(strip));
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
          ids.push_back(QStringLiteral("shift.routing.pipes.src.prev"));
          ids.push_back(QStringLiteral("shift.routing.pipes.src.name"));
          ids.push_back(QStringLiteral("shift.routing.pipes.src.next"));
          ids.push_back(QStringLiteral("shift.routing.pipes.dst.prev"));
          ids.push_back(QStringLiteral("shift.routing.pipes.dst.name"));
          ids.push_back(QStringLiteral("shift.routing.pipes.dst.next"));
          ids.push_back(QStringLiteral("shift.routing.pipes.type.prev"));
          ids.push_back(QStringLiteral("shift.routing.pipes.add.type"));
          ids.push_back(QStringLiteral("shift.routing.pipes.type.next"));
          ids.push_back(QStringLiteral("shift.routing.pipes.add"));
          ids.push_back(QStringLiteral("shift.routing.pipes.scope"));
          const int pipes = routingVm_->pipeRows().size();
          for (int pipe = 0; pipe < pipes; ++pipe) {
            ids.push_back(QStringLiteral("shift.routing.pipes.select.%1").arg(pipe));
            ids.push_back(QStringLiteral("shift.routing.pipes.bypass.%1").arg(pipe));
            ids.push_back(QStringLiteral("shift.routing.pipes.up.%1").arg(pipe));
            ids.push_back(QStringLiteral("shift.routing.pipes.down.%1").arg(pipe));
            ids.push_back(QStringLiteral("shift.routing.pipes.remove.%1").arg(pipe));
          }
          ids.push_back(QStringLiteral("shift.routing.pipes.param.page.prev"));
          ids.push_back(QStringLiteral("shift.routing.pipes.param.page.next"));
          const int selected = routingVm_->selectedPipe();
          const auto params = routingVm_->pipeParams();
          for (const auto& entry : params) {
            ids.push_back(QStringLiteral("shift.routing.pipes.param.%1.%2")
                              .arg(selected)
                              .arg(entry.toMap().value(QStringLiteral("param")).toInt()));
          }
        }
        break;
      case 2: {
        ids.push_back(QStringLiteral("shift.fx.A.return"));
        auto* view = fxVm_->slotB();
        ids.push_back(QStringLiteral("shift.fx.B.plugin"));
        ids.push_back(QStringLiteral("shift.fx.B.eject"));
        ids.push_back(QStringLiteral("shift.fx.B.save"));
        ids.push_back(QStringLiteral("shift.fx.B.load"));
        ids.push_back(QStringLiteral("shift.fx.B.return"));
        ids.push_back(QStringLiteral("shift.fx.B.bypass"));
        for (int param = 0; param < view->params()->rowCount(); ++param) {
          ids.push_back(QStringLiteral("shift.fx.B.param.%1").arg(param));
        }
        break;
      }
      case 3:
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
      case 4:
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
      case 7: {
        // EQ SHIFT face: the band editor is too tall for the tile row, so the
        // sliders and the six explicit band types live here.
        ids.push_back(QStringLiteral("shift.eq.subject.main"));
        ids.push_back(QStringLiteral("shift.eq.subject.track"));
        ids.push_back(QStringLiteral("shift.eq.band.gain"));
        ids.push_back(QStringLiteral("shift.eq.band.q"));
        ids.push_back(QStringLiteral("shift.eq.band.freq"));
        for (int type = 0; type < EqModel::kTypeCount; ++type) {
          ids.push_back(QStringLiteral("shift.eq.type.%1").arg(type));
        }
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
    for (int i = 0; i < kScreenCount; ++i) {
      ids.push_back(QStringLiteral("tab.") + QString::number(i));
    }
    ids.push_back(QStringLiteral("transport.click"));
    switch (tab_) {
      case 0: {
        ids.push_back(QStringLiteral("mixer.mode.0"));
        ids.push_back(QStringLiteral("mixer.mode.1"));
        if (mixerVm_->trackMode()) {
          for (int track = 0; track < kNumStrips; ++track) {
            ids.push_back(QStringLiteral("track.select.%1").arg(track));
          }
          ids.push_back(QStringLiteral("track.gain"));
          ids.push_back(QStringLiteral("track.pan"));
          ids.push_back(QStringLiteral("track.rec"));
          ids.push_back(QStringLiteral("track.mute"));
          ids.push_back(QStringLiteral("track.solo"));
        } else {
          const int first = mixerVm_->bank() * kStripsPerBank;
          for (int i = 0; i < kStripsPerBank; ++i) {
            const int strip = first + i;
            ids.push_back(QStringLiteral("mixer.select.%1").arg(strip));
            ids.push_back(QStringLiteral("mixer.in.%1").arg(strip));
            ids.push_back(QStringLiteral("mixer.gain.%1").arg(strip));
            ids.push_back(QStringLiteral("mixer.pan.%1").arg(strip));
            ids.push_back(QStringLiteral("mixer.mute.%1").arg(strip));
            ids.push_back(QStringLiteral("mixer.solo.%1").arg(strip));
            ids.push_back(QStringLiteral("mixer.arm.%1").arg(strip));
          }
          ids.push_back(QStringLiteral("mixer.master"));
          ids.push_back(QStringLiteral("mixer.bank.0"));
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
        ids.push_back(QStringLiteral("fx.A.plugin"));
        ids.push_back(QStringLiteral("fx.A.eject"));
        ids.push_back(QStringLiteral("fx.A.save"));
        ids.push_back(QStringLiteral("fx.A.load"));
        ids.push_back(QStringLiteral("fx.A.return"));
        ids.push_back(QStringLiteral("fx.A.bypass"));
        for (int param = 0; param < fxVm_->slotA()->params()->rowCount(); ++param) {
          ids.push_back(QStringLiteral("fx.A.param.%1").arg(param));
        }
        break;
      }
      case 3: {
        for (int track = 0; track < kNumStrips; ++track) {
          ids.push_back(QStringLiteral("recorder.track.%1").arg(track));
        }
        ids.push_back(QStringLiteral("recorder.arm"));
        ids.push_back(QStringLiteral("recorder.start"));
        for (int row = 0; row < recorderVm_->takes()->rowCount(); ++row) {
          ids.push_back(QStringLiteral("recorder.take.%1").arg(row));
        }
        break;
      }
      case 4: {
        ids.push_back(QStringLiteral("system.backend"));
        ids.push_back(QStringLiteral("system.input"));
        ids.push_back(QStringLiteral("system.buffer"));
        ids.push_back(QStringLiteral("system.devout.main"));
        ids.push_back(QStringLiteral("system.devout.out34"));
        ids.push_back(QStringLiteral("system.devout.phones"));
        ids.push_back(QStringLiteral("system.apply"));
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
      case kEditTabIndex: {
        // EDIT timeline: page/view/zoom, the six visible lanes, the panel
        // cursor and the two region edges, then the ConfigPopup ops and undo.
        ids.push_back(QStringLiteral("timeline.page.prev"));
        ids.push_back(QStringLiteral("timeline.page.next"));
        ids.push_back(QStringLiteral("timeline.view.prev"));
        ids.push_back(QStringLiteral("timeline.view.next"));
        ids.push_back(QStringLiteral("timeline.zoom.out"));
        ids.push_back(QStringLiteral("timeline.zoom.in"));
        for (int offset = 0; offset < timeline::kTracksPerPage; ++offset) {
          const int track = timelineVm_->page() * timeline::kTracksPerPage + offset;
          if (track >= kNumStrips) break;
          ids.push_back(QStringLiteral("timeline.lane.%1").arg(track));
        }
        ids.push_back(QStringLiteral("timeline.cursor"));
        ids.push_back(QStringLiteral("timeline.in"));
        ids.push_back(QStringLiteral("timeline.out"));
        ids.push_back(QStringLiteral("timeline.ops"));
        ids.push_back(QStringLiteral("timeline.undo"));
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

void AppController::startTakeNow() {
  pendingRec_ = false;
  chord_.reset();
  toggleTake();
}

void AppController::toggleTake() {
  if (audio_.recording()) {
    stopTake();
    return;
  }
  takeArmMask_ = mixer_.armMask();
  if (!audio_.startTake(state_.takeDirectory())) {
    setNotice(QStringLiteral("take refused (arm at least one strip)"));
    return;
  }
  state_.recording.store(true);
  link_.requestTransport(1);
  recorderVm_->takeStateChanged();
  updateTransport();
  updatePanelLeds();
}

void AppController::stopTake() {
  const auto take = audio_.stopTake();
  state_.recording.store(false);
  link_.requestTransport(0);
  // Adopt the core recorder's chNN.wav files into the session-v2 per-track
  // names before the take list rescans. This is the app-side name policy; a
  // future Recorder API can take the template directly.
  if (take.files > 0 && !take.path.empty()) {
    recorderVm_->adoptRecordedTake(take.path, takeArmMask_);
  }
  recorderVm_->takeStateChanged();
  if (take.files > 0) {
    log("recorder: take finished, " + std::to_string(take.files) + " files in " + take.path);
  }
  updateTransport();
  updatePanelLeds();
}

void AppController::updateTransport() {
  const auto status = link_.status();
  const QString state = recording() ? QStringLiteral("REC")
                        : (status.rolling ? QStringLiteral("ROLL")
                                          : QStringLiteral("STOP"));
  const QString position =
      QString::fromStdString(formatBarBeatTick(status.tick)) + QStringLiteral(" / ") +
      QString::fromStdString(formatDuration(tickSeconds(status.tick, status.tempoX100)));
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
  std::array<double, kNumStrips> stripLevels{};
  for (int strip = 0; strip < kNumStrips; ++strip) {
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
  recorderVm_->takeStateChanged();
  recorderVm_->resyncActiveTakes();
  timelineVm_->reloadFromSettings();
  audio_.applyMonitorSettings(settings_);
  audio_.applyTakeLoops(settings_);
  audio_.applyEq(eqModel_.capture());
  sessionVm_->pushClockToLink();
  toolsVm_->refresh();
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
    midi_.registerLearnTarget(learnTarget(strip, "sendA"),
                              [this, strip](double value) {
                                mixer_.setSend(strip, 0, static_cast<float>(value));
                              });
    midi_.registerLearnTarget(learnTarget(strip, "sendB"),
                              [this, strip](double value) {
                                mixer_.setSend(strip, 1, static_cast<float>(value));
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
  // comes from pipe_params.hpp at dispatch time, because a slot's pipe type can
  // change after the binding was made.
  for (int source = 0; source < routing::kInputCount; ++source) {
    for (int destination = 0; destination < routing::kOutputCount; ++destination) {
      for (int pipe = 0; pipe < static_cast<int>(midi::kMaxRoutePipes); ++pipe) {
        for (int param = 0; param < static_cast<int>(midi::kMaxPipeParams); ++param) {
          const auto path = pipeLearnTargetPath(source, destination, pipe, param);
          midi_.registerLearnTarget(path, [this, source, destination, pipe,
                                           param](double value) {
            const auto input = routing::kInputs[static_cast<std::size_t>(source)];
            const auto output = routing::kOutputs[static_cast<std::size_t>(destination)];
            const auto pipes = midi_.routePipes(input, output);
            if (pipe < 0 || pipe >= static_cast<int>(pipes.size())) return;
            const auto specs = pipeParamSpecs(pipes[static_cast<std::size_t>(pipe)].type);
            if (param < 0 || param >= static_cast<int>(specs.size())) return;
            const auto& spec = specs[static_cast<std::size_t>(param)];
            const int scaled = spec.minimum + static_cast<int>(std::lround(
                                                   std::clamp(value, 0.0, 1.0) *
                                                   (spec.maximum - spec.minimum)));
            (void)midi_.setRoutePipeParam(input, output, pipe, param, scaled);
          });
        }
      }
    }
  }
}

void AppController::setNotice(const QString& notice) {
  noticeText_ = notice;
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

void AppController::setTouchAnchorY(double y) {
  const double value = std::isfinite(y) && y >= 0.0 ? y : -1.0;
  if (std::abs(value - touchAnchorY_) < 0.5) return;
  touchAnchorY_ = value;
  emit touchAnchorChanged();
}

void AppController::popupGain(const QString& label, int strip) {
  showValuePopup(label, QStringLiteral("db"),
                 mixerVm_->data(mixerVm_->index(strip),
                                MixerViewModel::GainDbRole)
                     .toDouble());
}

void AppController::popupPan(const QString& label, int strip) {
  showValuePopup(label, QStringLiteral("pan"),
                 mixerVm_->data(mixerVm_->index(strip),
                                MixerViewModel::PanRole)
                     .toDouble());
}

void AppController::popupSend(const QString& label, int strip, int send) {
  showValuePopup(label, QStringLiteral("percent"),
                 mixerVm_->data(mixerVm_->index(strip),
                                send == 0 ? MixerViewModel::SendARole
                                          : MixerViewModel::SendBRole)
                     .toDouble());
}

void AppController::popupTrackSend(const QString& label, int send) {
  showValuePopup(label, QStringLiteral("percent"),
                 send == 0 ? mixerVm_->trackSendA() : mixerVm_->trackSendB());
}

}  // namespace mnky::app

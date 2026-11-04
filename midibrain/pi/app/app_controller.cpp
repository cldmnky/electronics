#include "app_controller.hpp"

#include <algorithm>
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

AppController::AppController(bool emulator, QObject* parent)
    : QObject(parent),
      midi_(emulator),
      audio_(mixer_, fx_, state_, emulator),
      link_(midi_, state_, emulator) {
  context_ = std::make_unique<AppContext>(
      AppContext{state_, mixer_, fx_, midi_, audio_, link_});

  mixerVm_ = std::make_unique<MixerViewModel>(mixer_);
  routingVm_ = std::make_unique<RoutingViewModel>(midi_);
  fxVm_ = std::make_unique<FxViewModel>(fx_, state_, mixer_, audio_.sampleRate());
  recorderVm_ = std::make_unique<RecorderViewModel>(state_, audio_, mixer_, link_);
  projectsVm_ = std::make_unique<ProjectsViewModel>(
      *context_, [this] { projectStateChanged(); });
  systemVm_ = std::make_unique<SystemViewModel>(link_, audio_, midi_, fx_, state_);
  learnVm_ = std::make_unique<LearnViewModel>(midi_);

  shiftActions_ = {QStringLiteral("Save"), QStringLiteral("Panic"), QStringLiteral("Tap"),
                   QStringLiteral("Click")};
  registerLearnTargets();

  timer_.setInterval(66);
  connect(&timer_, &QTimer::timeout, this, &AppController::timerTick);
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

  state_.clearDirty();
  dirtyTracker_.clear(project::fingerprint(*context_));
  started_ = true;
  timer_.start();
  log(std::string("app: started with ") + (audio_.emulated() ? "emulator" : "device") +
      " backends");
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
  focusByScreen_[tab_] = focusIndex_;
  tab_ = clamped;
  state_.currentTab.store(tab_);
  focusIndex_ = focusByScreen_[tab_];
  emit tabChanged();
  rebuildFocusIds();
}

void AppController::setClickEnabled(bool enabled) {
  state_.clickEnabled.store(enabled);
  emit clickChanged();
}

void AppController::setClickLevel(double level) {
  const double clamped = std::clamp(level, 0.0, 1.0);
  state_.clickLevel.store(static_cast<float>(clamped));
  emit clickChanged();
}

void AppController::setClickBars(int bars) {
  state_.clickBars.store(std::clamp(bars, 1, 8));
  emit clickChanged();
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
  requestKeypad(QStringLiteral("RENAME TAKE"), entry->name,
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
                     QStringLiteral("? All WAV files in the take folder are removed."),
                 QStringLiteral("DELETE"), QStringLiteral("take-delete"), row);
}

void AppController::openDemoKeypad() {
  requestKeypad(QStringLiteral("RENAME PROJECT"), QStringLiteral("Project-demo"),
                QStringLiteral("project-rename"), -1);
}

void AppController::timerTick() {
  Intent intent;
  while (state_.nextIntent(intent)) perform(intent);

  expirePendingRec();
  const auto status = link_.status();
  state_.rolling.store(status.rolling);
  state_.tempoX100.store(status.tempoX100);
  updateShiftState(status);
  updateTransport();

  ++refreshTicks_;
  // MIDI-learn writes the model atomics on RtMidi's thread; the mixer only has
  // to re-read them about once a second. FX params can be changed by the plugin
  // itself, so they refresh every tick.
  if (refreshTicks_ % 15 == 0) mixerVm_->refresh();
  routingVm_->refresh();
  fxVm_->refresh();
  recorderVm_->refresh();
  projectsVm_->refresh();
  learnVm_->poll();
  if (refreshTicks_ % 4 == 0) systemVm_->refresh();

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
      if (panelMode_ == QStringLiteral("edit")) {
        adjustFocused(intent.amount, shiftHeld_);
      } else {
        const int step = shiftHeld_ ? kTempoStepFineX100 : kTempoStepCoarseX100;
        const int tempo = clampTempoX100(state_.tempoX100.load() + intent.amount * step);
        state_.tempoX100.store(tempo);
        link_.requestTempo(static_cast<std::uint16_t>(tempo));
      }
      break;
    case Intent::Kind::TempoStep: {
      const int tempo = clampTempoX100(state_.tempoX100.load() + intent.amount);
      state_.tempoX100.store(tempo);
      link_.requestTempo(static_cast<std::uint16_t>(tempo));
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
  if (panelMode_ == QStringLiteral("shift")) {
    switch (intent.kind) {
      case Intent::Kind::NavUp:
      case Intent::Kind::NavLeft:
        focusIndex_ = moveIndex(focusIndex_, -1, static_cast<int>(shiftActions_.size()));
        break;
      case Intent::Kind::NavDown:
      case Intent::Kind::NavRight:
        focusIndex_ = moveIndex(focusIndex_, 1, static_cast<int>(shiftActions_.size()));
        break;
      case Intent::Kind::NavPress:
        shiftAction(focusIndex_);
        break;
      case Intent::Kind::EncoderSw:
        break;
      default:
        break;
    }
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
        mixerVm_->toggleArm(state_.selectedStrip.load());
      }
      break;
    default:
      break;
  }
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
  if (mode == QStringLiteral("shift")) focusIndex_ = 0;
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
  if (held && shiftHeld_ && panelMode_ != QStringLiteral("shift") &&
      now - shiftPressMs_ >= 500) {
    setPanelMode(QStringLiteral("shift"));
  }
  if (!held && shiftHeld_) {
    shiftHeld_ = false;
    const bool longHold = now - shiftPressMs_ >= 500;
    if (panelMode_ == QStringLiteral("shift")) {
      setPanelMode(QStringLiteral("navigate"));
    } else if (!longHold) {
      // EXIT short press: dismiss an open overlay, otherwise leave EDIT.
      if (confirmOpen_) {
        confirmCancel();
      } else if (keypadOpen_) {
        keypadCancel();
      } else if (panelMode_ == QStringLiteral("edit")) {
        setPanelMode(QStringLiteral("navigate"));
      }
    }
  }
}

bool AppController::isEditableFocus(const QString& id) const {
  if (id.startsWith(QStringLiteral("mixer."))) {
    return id.contains(QStringLiteral("gain")) || id.contains(QStringLiteral("pan")) ||
           id.contains(QStringLiteral("sendA")) || id.contains(QStringLiteral("sendB")) ||
           id == QStringLiteral("mixer.master");
  }
  if (id.startsWith(QStringLiteral("routing.transpose."))) return true;
  if (id.startsWith(QStringLiteral("fx."))) {
    return id.endsWith(QStringLiteral(".return")) || id.contains(QStringLiteral(".param."));
  }
  if (id.startsWith(QStringLiteral("system."))) {
    return id == QStringLiteral("system.backend") || id == QStringLiteral("system.input") ||
           id == QStringLiteral("system.buffer") ||
           id == QStringLiteral("system.click.level") ||
           id == QStringLiteral("system.click.bars");
  }
  if (id == QStringLiteral("learn.target")) return true;
  return false;
}

void AppController::activateFocus() {
  if (focusId_.isEmpty()) return;
  const auto parts = focusId_.split(QLatin1Char('.'));

  if (parts.size() == 2 && parts[0] == QStringLiteral("tab")) {
    setTab(parts[1].toInt());
    return;
  }
  if (focusId_ == QStringLiteral("transport.click")) {
    setClickEnabled(!clickEnabled());
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
        what == QStringLiteral("sendA") || what == QStringLiteral("sendB")) {
      setPanelMode(QStringLiteral("edit"));
    } else if (what == QStringLiteral("mute")) {
      mixerVm_->toggleMute(strip);
    } else if (what == QStringLiteral("solo")) {
      mixerVm_->toggleSolo(strip);
    } else if (what == QStringLiteral("arm")) {
      mixerVm_->toggleArm(strip);
    }
    return;
  }

  if (parts.size() >= 2 && parts[0] == QStringLiteral("routing")) {
    if (parts[1] == QStringLiteral("reset")) {
      routingVm_->reset();
    } else if (parts[1] == QStringLiteral("node") && parts.size() >= 4) {
      const int index = parts[3].toInt();
      if (parts[2] == QStringLiteral("in")) {
        routingVm_->selectInput(index);
      } else {
        routingVm_->selectOutput(index);
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
    if (parts[1] == QStringLiteral("arm") && parts.size() >= 3) {
      mixerVm_->toggleArm(parts[2].toInt());
    } else if (parts[1] == QStringLiteral("start")) {
      recorderVm_->startStop();
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
    if (parts[1] == QStringLiteral("apply")) {
      systemVm_->apply();
    } else if (parts[1] == QStringLiteral("refresh")) {
      systemVm_->refreshDevices();
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
    }
    return;
  }

  if (parts.size() >= 2 && parts[0] == QStringLiteral("learn")) {
    if (parts[1] == QStringLiteral("target")) setPanelMode(QStringLiteral("edit"));
  }
}

void AppController::adjustFocused(int detents, bool fine) {
  if (focusId_.isEmpty() || detents == 0) return;
  const auto parts = focusId_.split(QLatin1Char('.'));
  const double step = static_cast<double>(detents);

  if (parts.size() >= 2 && parts[0] == QStringLiteral("mixer") &&
      parts[1] == QStringLiteral("master")) {
    const double delta = step * (fine ? 0.1 : 0.5);
    mixerVm_->setMasterDb(mixerVm_->masterDb() + delta);
    return;
  }

  if (parts.size() >= 3 && parts[0] == QStringLiteral("mixer")) {
    const QString what = parts[1];
    const int strip = parts[2].toInt();
    selectStrip(strip);
    if (what == QStringLiteral("gain")) {
      mixerVm_->setGainDb(strip, mixerVm_->data(mixerVm_->index(strip), MixerViewModel::GainDbRole).toDouble() +
                                     step * (fine ? 0.1 : 0.5));
    } else if (what == QStringLiteral("pan")) {
      mixerVm_->setPan(strip, mixerVm_->data(mixerVm_->index(strip), MixerViewModel::PanRole).toDouble() +
                                  step * (fine ? 0.005 : 0.02));
    } else if (what == QStringLiteral("sendA")) {
      mixerVm_->setSend(strip, 0,
                        mixerVm_->data(mixerVm_->index(strip), MixerViewModel::SendARole).toDouble() +
                            step * (fine ? 0.005 : 0.02));
    } else if (what == QStringLiteral("sendB")) {
      mixerVm_->setSend(strip, 1,
                        mixerVm_->data(mixerVm_->index(strip), MixerViewModel::SendBRole).toDouble() +
                            step * (fine ? 0.005 : 0.02));
    }
    return;
  }

  if (parts.size() >= 3 && parts[0] == QStringLiteral("routing") &&
      parts[1] == QStringLiteral("transpose")) {
    routingVm_->adjustTranspose(parts[2].toInt(), detents);
    return;
  }

  if (parts.size() >= 4 && parts[0] == QStringLiteral("fx")) {
    const int slot = parts[1] == QStringLiteral("B") ? 1 : 0;
    auto* view = slot == 1 ? fxVm_->slotB() : fxVm_->slotA();
    if (parts[2] == QStringLiteral("return")) {
      view->setReturnDb(view->returnDb() + step * (fine ? 0.1 : 0.5));
    } else if (parts[2] == QStringLiteral("param")) {
      const int index = parts[3].toInt();
      const auto params = fx_.slot(slot).params();
      if (index >= 0 && index < static_cast<int>(params.size())) {
        const auto& param = params[static_cast<std::size_t>(index)];
        const double range = std::max(0.0, param.max - param.min);
        const double paramStep = range / (fine ? 1000.0 : 100.0);
        view->adjustParam(index, step * paramStep);
      }
    }
    return;
  }

  if (parts.size() >= 2 && parts[0] == QStringLiteral("system")) {
    if (parts[1] == QStringLiteral("backend")) {
      const int count = systemVm_->backendLabels().size();
      systemVm_->setBackendIndex(moveIndex(systemVm_->backendIndex(), detents > 0 ? 1 : -1, count));
    } else if (parts[1] == QStringLiteral("input")) {
      const int count = systemVm_->inputLabels().size();
      systemVm_->setInputIndex(moveIndex(systemVm_->inputIndex(), detents > 0 ? 1 : -1, count));
    } else if (parts[1] == QStringLiteral("buffer")) {
      const int count = systemVm_->bufferLabels().size();
      systemVm_->setBufferIndex(moveIndex(systemVm_->bufferIndex(), detents > 0 ? 1 : -1, count));
    } else if (parts[1] == QStringLiteral("click")) {
      if (parts.size() >= 3 && parts[2] == QStringLiteral("level")) {
        setClickLevel(clickLevel() + step * (fine ? 0.01 : 0.05));
      } else if (parts.size() >= 3 && parts[2] == QStringLiteral("bars")) {
        setClickBars(clickBars() + detents);
      }
    }
    return;
  }

  if (parts.size() >= 2 && parts[0] == QStringLiteral("learn") &&
      parts[1] == QStringLiteral("target")) {
    const int count = learnVm_->targets().size();
    learnVm_->setTargetIndex(moveIndex(learnVm_->targetIndex(), detents > 0 ? 1 : -1, count));
  }
}

void AppController::rebuildFocusIds() {
  std::vector<QString> ids;
  if (panelMode_ == QStringLiteral("shift")) {
    for (int i = 0; i < shiftActions_.size(); ++i) {
      ids.push_back(QStringLiteral("shift.") + QString::number(i));
    }
  } else {
    for (int i = 0; i < kScreenCount; ++i) {
      ids.push_back(QStringLiteral("tab.") + QString::number(i));
    }
    ids.push_back(QStringLiteral("transport.click"));
    switch (tab_) {
      case 0: {
        const int first = mixerVm_->bank() * kStripsPerBank;
        for (int i = 0; i < kStripsPerBank; ++i) {
          const int strip = first + i;
          ids.push_back(QStringLiteral("mixer.select.%1").arg(strip));
          ids.push_back(QStringLiteral("mixer.gain.%1").arg(strip));
          ids.push_back(QStringLiteral("mixer.pan.%1").arg(strip));
          ids.push_back(QStringLiteral("mixer.sendA.%1").arg(strip));
          ids.push_back(QStringLiteral("mixer.sendB.%1").arg(strip));
          ids.push_back(QStringLiteral("mixer.mute.%1").arg(strip));
          ids.push_back(QStringLiteral("mixer.solo.%1").arg(strip));
          ids.push_back(QStringLiteral("mixer.arm.%1").arg(strip));
        }
        ids.push_back(QStringLiteral("mixer.master"));
        ids.push_back(QStringLiteral("mixer.bank.0"));
        ids.push_back(QStringLiteral("mixer.bank.1"));
        break;
      }
      case 1: {
        ids.push_back(QStringLiteral("routing.reset"));
        for (int source = 0; source < routing::kInputCount; ++source) {
          ids.push_back(QStringLiteral("routing.node.in.%1").arg(source));
        }
        for (int destination = 0; destination < routing::kOutputCount; ++destination) {
          ids.push_back(QStringLiteral("routing.node.out.%1").arg(destination));
        }
        for (int source = 0; source < routing::kInputCount; ++source) {
          for (int channel = 0; channel < 16; ++channel) {
            ids.push_back(QStringLiteral("routing.mask.%1.%2").arg(source).arg(channel));
          }
        }
        for (int source = 0; source < routing::kInputCount; ++source) {
          ids.push_back(QStringLiteral("routing.transpose.%1").arg(source));
        }
        break;
      }
      case 2: {
        for (int slot = 0; slot < 2; ++slot) {
          const QString letter = slot == 1 ? QStringLiteral("B") : QStringLiteral("A");
          ids.push_back(QStringLiteral("fx.%1.plugin").arg(letter));
          ids.push_back(QStringLiteral("fx.%1.eject").arg(letter));
          ids.push_back(QStringLiteral("fx.%1.save").arg(letter));
          ids.push_back(QStringLiteral("fx.%1.load").arg(letter));
          ids.push_back(QStringLiteral("fx.%1.return").arg(letter));
          ids.push_back(QStringLiteral("fx.%1.bypass").arg(letter));
          auto* view = slot == 1 ? fxVm_->slotB() : fxVm_->slotA();
          for (int param = 0; param < view->params()->rowCount(); ++param) {
            ids.push_back(QStringLiteral("fx.%1.param.%2").arg(letter).arg(param));
          }
        }
        break;
      }
      case 3: {
        for (int strip = 0; strip < kNumStrips; ++strip) {
          ids.push_back(QStringLiteral("recorder.arm.%1").arg(strip));
        }
        ids.push_back(QStringLiteral("recorder.start"));
        ids.push_back(QStringLiteral("recorder.rename"));
        ids.push_back(QStringLiteral("recorder.delete"));
        for (int row = 0; row < recorderVm_->takes()->rowCount(); ++row) {
          ids.push_back(QStringLiteral("recorder.take.%1").arg(row));
        }
        break;
      }
      case 4: {
        ids.push_back(QStringLiteral("system.backend"));
        ids.push_back(QStringLiteral("system.input"));
        ids.push_back(QStringLiteral("system.buffer"));
        ids.push_back(QStringLiteral("system.apply"));
        ids.push_back(QStringLiteral("system.refresh"));
        ids.push_back(QStringLiteral("system.click"));
        ids.push_back(QStringLiteral("system.click.level"));
        ids.push_back(QStringLiteral("system.click.bars"));
        ids.push_back(QStringLiteral("learn.target"));
        ids.push_back(QStringLiteral("learn.arm"));
        ids.push_back(QStringLiteral("learn.clear"));
        for (int row = 0; row < learnVm_->bindings().size(); ++row) {
          ids.push_back(QStringLiteral("learn.binding.%1").arg(row));
        }
        break;
      }
      case kProjectsTabIndex: {
        ids.push_back(QStringLiteral("projects.new"));
        ids.push_back(QStringLiteral("projects.save"));
        ids.push_back(QStringLiteral("projects.rename"));
        ids.push_back(QStringLiteral("projects.delete"));
        for (int row = 0; row < projectsVm_->model()->rowCount(); ++row) {
          ids.push_back(QStringLiteral("projects.row.%1").arg(row));
        }
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
  if (clickOn != lastClickEnabled_ || std::abs(level - lastClickLevel_) > 0.0001 ||
      bars != lastClickBars_) {
    lastClickEnabled_ = clickOn;
    lastClickLevel_ = level;
    lastClickBars_ = bars;
    emit clickChanged();
  }

  const double left = audio_.peak(0);
  const double right = audio_.peak(1);
  mixerVm_->setMeters(left, right);
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
  state_.clearDirty();
  dirtyTracker_.clear(project::fingerprint(*context_));
  emit dirtyChanged();
}

void AppController::updatePanelLeds() {
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
}

void AppController::setNotice(const QString& notice) {
  noticeText_ = notice;
  emit noticeChanged();
}

}  // namespace mnky::app

#include "MainComponent.hpp"

#include "app_context.hpp"
#include "logging.hpp"
#include "project_io.hpp"
#include "screens/FxScreen.hpp"
#include "screens/MixerScreen.hpp"
#include "screens/ProjectsScreen.hpp"
#include "screens/RecorderScreen.hpp"
#include "screens/RoutingScreen.hpp"
#include "screens/SystemScreen.hpp"
#include "screens/routing_model.hpp"
#include "ui_style.hpp"

namespace mnky::app {
namespace {
constexpr int kProjectsTabIndex = 5;

std::uint32_t nowMs() {
  return static_cast<std::uint32_t>(juce::Time::getMillisecondCounter());
}
}  // namespace

void ContentTabbedComponent::resized() {
  TabbedComponent::resized();
  if (contentTopInset_ <= 0) return;
  for (int index = 0; index < getNumTabs(); ++index) {
    if (auto* content = getTabContentComponent(index)) {
      content->setBounds(content->getBounds().withTrimmedTop(contentTopInset_));
    }
  }
}

void MainComponent::Dot::paint(juce::Graphics& g) {
  g.setColour(style::accent);
  g.fillEllipse(getLocalBounds().toFloat());
}

MainComponent::MainComponent(bool forceEmulator)
    : midi_(forceEmulator),
      audio_(mixer_, fx_, state_, forceEmulator),
      link_(midi_, state_, forceEmulator),
      transport_(state_, link_) {
  context_ =
      std::make_unique<AppContext>(AppContext{state_, mixer_, fx_, midi_, audio_, link_});

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

  tabs_.setTabBarDepth(style::kTabDepth);
  tabs_.setOutline(0);
  tabs_.setContentTopInset(style::kTransportHeight);
  addAndMakeVisible(tabs_);

  mixerScreen_ = new MixerScreen(*context_);
  routingScreen_ = new RoutingScreen(*context_);
  fxScreen_ = new FxScreen(*context_);
  recorderScreen_ = new RecorderScreen(*context_);
  systemScreen_ = new SystemScreen(*context_);
  projectsScreen_ = new ProjectsScreen(*context_, [this] { projectStateChanged(); });

  tabs_.addTab("Mixer", style::background, mixerScreen_, true);
  tabs_.addTab("Routing", style::background, routingScreen_, true);
  tabs_.addTab("FX", style::background, fxScreen_, true);
  tabs_.addTab("Recorder", style::background, recorderScreen_, true);
  tabs_.addTab("System", style::background, systemScreen_, true);
  tabs_.addTab("Projects", style::background, projectsScreen_, true);

  // Added after the tab component so the strip and its dirty dot paint above
  // every screen. The strip sits in the content inset reserved above.
  addAndMakeVisible(transport_);
  tabDot_.setInterceptsMouseClicks(false, false);
  addChildComponent(tabDot_);

  mixerScreen_->setSelectedStrip(state_.selectedStrip.load());
  fxScreen_->loadDefaults();

  link_.start();
  startTimerHz(15);
  state_.clearDirty();
  dirtyTracker_.clear(project::fingerprint(*context_));
  log("app: started with " +
      juce::String(forceEmulator ? "emulator backends" : "hardware backends"));
}

MainComponent::~MainComponent() {
  stopTimer();
  link_.stop();
  audio_.stop();
  midi_.close();
}

void MainComponent::paint(juce::Graphics& g) {
  g.fillAll(findColour(juce::ResizableWindow::backgroundColourId));
}

void MainComponent::resized() {
  tabs_.setBounds(getLocalBounds());
  transport_.setBounds(0, style::kTabDepth, getWidth(), style::kTransportHeight);
  updateDirtyIndicators();
}

void MainComponent::timerCallback() {
  Intent intent;
  while (state_.nextIntent(intent)) perform(intent);

  expirePendingRec();

  const auto status = link_.status();
  state_.rolling.store(status.rolling);
  state_.tempoX100.store(status.tempoX100);
  // The tab bar is the source of truth for the screen: a user tap and a panel
  // gesture both end up here.
  state_.currentTab.store(tabs_.getCurrentTabIndex());

  mixerScreen_->refresh();
  routingScreen_->refresh();
  fxScreen_->refresh();
  recorderScreen_->refresh();
  projectsScreen_->refresh();
  // The System readout rebuilds a text block and the log tail scans the ring,
  // so it runs at a quarter of the meter/transport rate.
  if (++refreshTicks_ % 4 == 0) systemScreen_->refresh();

  updateDirtyState();
  transport_.refresh();
  updateDirtyIndicators();
  updatePanelLeds();
}

void MainComponent::expirePendingRec() {
  if (!pendingRec_) return;
  if (nowMs() - pendingRecMs_ < ChordDetector::kWindowMs) return;
  pendingRec_ = false;
  chord_.reset();
  if (!audio_.recording()) toggleTake();
}

void MainComponent::startTakeNow() {
  pendingRec_ = false;
  chord_.reset();
  toggleTake();
}

void MainComponent::perform(const Intent& intent) {
  switch (intent.kind) {
    case Intent::Kind::None:
      break;

    case Intent::Kind::MoveTab: {
      const int next = moveIndex(tabs_.getCurrentTabIndex(), intent.amount, kScreenCount);
      tabs_.setCurrentTabIndex(next);
      state_.currentTab.store(next);
      break;
    }

    case Intent::Kind::MoveStrip: {
      const int next = moveIndex(state_.selectedStrip.load(), intent.amount, kNumStrips);
      state_.selectedStrip.store(next);
      mixerScreen_->setSelectedStrip(next);
      break;
    }

    case Intent::Kind::ArmToggle: {
      const int strip = juce::jlimit(0, kNumStrips - 1, intent.amount);
      mixer_.setArm(strip, !mixer_.arm(strip));
      break;
    }

    case Intent::Kind::Play:
      // Play only starts from a stopped transport (idempotent, mirrors the Pico
      // transport_for_press() policy). A REC press within the chord window
      // upgrades this start into a take.
      if (!link_.status().rolling) {
        if (chord_.onPress(ChordDetector::Button::Play, nowMs())) {
          startTakeNow();
        } else {
          link_.requestTransport(1);
        }
      }
      break;

    case Intent::Kind::PlayStart:
      // EV_TRANSPORT 1 is "start": the firmware restarts at tick 0.
      link_.requestTransport(1);
      break;

    case Intent::Kind::Stop:
      pendingRec_ = false;
      chord_.reset();
      link_.requestTransport(0);
      if (audio_.recording()) stopTake();
      break;

    case Intent::Kind::RecToggle:
      // Screen START TAKE posts amount 1 for an immediate toggle. The panel REC
      // button (amount 0) waits up to 300 ms for a PLAY press while stopped, so
      // the documented REC+PLAY chord can start the take; REC alone still
      // starts it when the window expires.
      if (intent.amount >= 1) {
        startTakeNow();
        break;
      }
      if (audio_.recording()) {
        stopTake();
        break;
      }
      if (link_.status().rolling) {
        startTakeNow();
        break;
      }
      if (chord_.onPress(ChordDetector::Button::Rec, nowMs())) {
        startTakeNow();
      } else {
        pendingRec_ = true;
        pendingRecMs_ = nowMs();
      }
      break;

    case Intent::Kind::OverdubToggle:
      state_.overdub.store(!state_.overdub.load());
      break;

    case Intent::Kind::TempoStep: {
      const int tempo = clampTempoX100(state_.tempoX100.load() + intent.amount);
      state_.tempoX100.store(tempo);
      link_.requestTempo(static_cast<std::uint16_t>(tempo));
      break;
    }
  }
  updatePanelLeds();
}

void MainComponent::toggleTake() {
  if (audio_.recording()) {
    stopTake();
    return;
  }
  if (!audio_.startTake(state_.takeDirectory())) {
    log("recorder: take refused (arm at least one strip)");
    return;
  }
  state_.recording.store(true);
  link_.requestTransport(1);
  recorderScreen_->takeStateChanged();
  updatePanelLeds();
}

void MainComponent::stopTake() {
  const auto take = audio_.stopTake();
  state_.recording.store(false);
  link_.requestTransport(0);
  recorderScreen_->takeStateChanged();
  if (take.files > 0) {
    log("recorder: take finished, " + juce::String(take.files) + " files in " +
        juce::String(take.path));
  }
  updatePanelLeds();
}

void MainComponent::updateDirtyState() {
  const auto fingerprint = project::fingerprint(*context_);
  if (dirtyTracker_.poll(fingerprint)) state_.markDirty();
}

void MainComponent::updateDirtyIndicators() {
  const bool dirty = state_.dirty.load();
  if (!dirty) {
    tabDot_.setVisible(false);
    return;
  }
  auto& bar = tabs_.getTabbedButtonBar();
  const auto* button = bar.getTabButton(kProjectsTabIndex);
  if (button == nullptr) {
    tabDot_.setVisible(false);
    return;
  }
  const auto barBounds = bar.getBounds();
  const auto buttonBounds = button->getBounds();
  tabDot_.setBounds(tabs_.getX() + barBounds.getX() + buttonBounds.getRight() - 12,
                    tabs_.getY() + barBounds.getY() + 8, 7, 7);
  tabDot_.setVisible(true);
  tabDot_.toFront(false);
}

void MainComponent::projectStateChanged() {
  // A project save/load/new resets the dirty baseline; FX params may have been
  // replaced behind the FX screen, so rebuild its sliders before sampling the
  // new fingerprint.
  if (fxScreen_ != nullptr) fxScreen_->projectStateChanged();
  state_.clearDirty();
  dirtyTracker_.clear(project::fingerprint(*context_));
  transport_.refresh();
  updateDirtyIndicators();
}

void MainComponent::updatePanelLeds() {
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

}  // namespace mnky::app

#include "MainComponent.hpp"

#include "app_context.hpp"
#include "logging.hpp"
#include "screens/FxScreen.hpp"
#include "screens/MixerScreen.hpp"
#include "screens/RecorderScreen.hpp"
#include "screens/RoutingScreen.hpp"
#include "screens/SystemScreen.hpp"
#include "screens/routing_model.hpp"
#include "ui_style.hpp"

namespace mnky::app {

MainComponent::MainComponent(bool forceEmulator)
    : midi_(forceEmulator),
      audio_(mixer_, fx_, forceEmulator),
      link_(midi_, state_, forceEmulator) {
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
  addAndMakeVisible(tabs_);

  mixerScreen_ = new MixerScreen(*context_);
  routingScreen_ = new RoutingScreen(*context_);
  fxScreen_ = new FxScreen(*context_);
  recorderScreen_ = new RecorderScreen(*context_);
  systemScreen_ = new SystemScreen(*context_);

  tabs_.addTab("Mixer", style::background, mixerScreen_, true);
  tabs_.addTab("Routing", style::background, routingScreen_, true);
  tabs_.addTab("FX", style::background, fxScreen_, true);
  tabs_.addTab("Recorder", style::background, recorderScreen_, true);
  tabs_.addTab("System", style::background, systemScreen_, true);

  mixerScreen_->setSelectedStrip(state_.selectedStrip.load());
  fxScreen_->loadDefaults();

  link_.start();
  startTimerHz(15);
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
}

void MainComponent::timerCallback() {
  Intent intent;
  while (state_.nextIntent(intent)) perform(intent);

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
  // The System readout rebuilds a text block and the log tail scans the ring,
  // so it runs at a quarter of the meter/transport rate.
  if (++refreshTicks_ % 4 == 0) systemScreen_->refresh();
  updatePanelLeds();
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
      // transport_for_press() policy).
      if (!link_.status().rolling) link_.requestTransport(1);
      break;

    case Intent::Kind::PlayStart:
      // EV_TRANSPORT 1 is "start": the firmware restarts at tick 0.
      link_.requestTransport(1);
      break;

    case Intent::Kind::Stop:
      link_.requestTransport(0);
      if (audio_.recording()) stopTake();
      break;

    case Intent::Kind::RecToggle:
      toggleTake();
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

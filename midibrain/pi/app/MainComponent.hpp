#pragma once
// Root content component: owns the app services, the five screens and the tab
// bar, and performs panel intents on the message thread.
#include <cstdint>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "app_state.hpp"
#include "audio_glue.hpp"
#include "fx_slot.hpp"
#include "link_client.hpp"
#include "midi_glue.hpp"
#include "mixer_model.hpp"

namespace mnky::app {

struct AppContext;
class FxScreen;
class MixerScreen;
class RecorderScreen;
class RoutingScreen;
class SystemScreen;

class MainComponent final : public juce::Component, private juce::Timer {
 public:
  explicit MainComponent(bool forceEmulator);
  ~MainComponent() override;

  void paint(juce::Graphics& g) override;
  void resized() override;

 private:
  void timerCallback() override;
  void perform(const Intent& intent);
  void toggleTake();
  void stopTake();
  void updatePanelLeds();

  // Declaration order matters: services are destroyed in reverse order, after
  // the screens have been deleted by the tab bar (which is declared last).
  AppState state_;
  MixerModel mixer_;
  FxManager fx_;
  MidiGlue midi_;
  AudioGlue audio_;
  LinkClient link_;
  std::unique_ptr<AppContext> context_;

  juce::LookAndFeel_V4 lookAndFeel_;
  juce::TabbedComponent tabs_{juce::TabbedButtonBar::TabsAtTop};

  MixerScreen* mixerScreen_ = nullptr;
  RoutingScreen* routingScreen_ = nullptr;
  FxScreen* fxScreen_ = nullptr;
  RecorderScreen* recorderScreen_ = nullptr;
  SystemScreen* systemScreen_ = nullptr;

  std::uint8_t lastLedMask_ = 0;
  std::uint8_t lastLedStates_ = 0;
  int refreshTicks_ = 0;

  JUCE_DECLARE_NON_COPYABLE(MainComponent)
};

}  // namespace mnky::app

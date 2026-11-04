#pragma once
// Root content component: owns the app services, the six screens, the tab bar
// and the persistent transport strip, and performs panel intents on the message
// thread.
#include <cstdint>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "app_state.hpp"
#include "audio_glue.hpp"
#include "fx_slot.hpp"
#include "link_client.hpp"
#include "midi_glue.hpp"
#include "mixer_model.hpp"
#include "project_logic.hpp"
#include "transport_strip.hpp"

namespace mnky::app {

struct AppContext;
class FxScreen;
class MixerScreen;
class ProjectsScreen;
class RecorderScreen;
class RoutingScreen;
class SystemScreen;

// The tab component leaves a 28 px top inset inside each screen's content area
// for the transport strip that is drawn directly under the tab bar. TabbedComponent
// assigns the full content rectangle to every screen, so the inset is applied
// after its layout on every resize/tab change.
class ContentTabbedComponent final : public juce::TabbedComponent {
 public:
  ContentTabbedComponent() : juce::TabbedComponent(juce::TabbedButtonBar::TabsAtTop) {}

  void setContentTopInset(int inset) {
    contentTopInset_ = inset;
    resized();
  }

  void resized() override;

 private:
  int contentTopInset_ = 0;

  JUCE_DECLARE_NON_COPYABLE(ContentTabbedComponent)
};

class MainComponent final : public juce::Component, private juce::Timer {
 public:
  explicit MainComponent(bool forceEmulator);
  ~MainComponent() override;

  void paint(juce::Graphics& g) override;
  void resized() override;

 private:
  class Dot final : public juce::Component {
   public:
    void paint(juce::Graphics& g) override;
  };

  void timerCallback() override;
  void perform(const Intent& intent);
  void toggleTake();
  void stopTake();
  void startTakeNow();
  void updatePanelLeds();
  void updateDirtyState();
  void updateDirtyIndicators();
  void projectStateChanged();
  void expirePendingRec();

  // Declaration order matters: the tab component owns the screens and is
  // destroyed before the context/services, while the strip and its dot go
  // first because they only reference the services.
  AppState state_;
  MixerModel mixer_;
  FxManager fx_;
  MidiGlue midi_;
  AudioGlue audio_;
  LinkClient link_;
  std::unique_ptr<AppContext> context_;

  ContentTabbedComponent tabs_{};
  TransportStrip transport_;
  Dot tabDot_;

  MixerScreen* mixerScreen_ = nullptr;
  RoutingScreen* routingScreen_ = nullptr;
  FxScreen* fxScreen_ = nullptr;
  RecorderScreen* recorderScreen_ = nullptr;
  SystemScreen* systemScreen_ = nullptr;
  ProjectsScreen* projectsScreen_ = nullptr;

  DirtyTracker dirtyTracker_;
  ChordDetector chord_;
  bool pendingRec_ = false;
  std::uint32_t pendingRecMs_ = 0;

  std::uint8_t lastLedMask_ = 0;
  std::uint8_t lastLedStates_ = 0;
  int refreshTicks_ = 0;

  JUCE_DECLARE_NON_COPYABLE(MainComponent)
};

}  // namespace mnky::app

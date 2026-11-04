#pragma once
// Persistent 28 px transport strip that sits directly under the tab bar on
// every screen: rolling state, bar:beat plus elapsed time, BPM, the metronome
// toggle and the project dirty dot. It is a sibling of the tab component, so it
// stays visible while tabs change; MainComponent refreshes it from its timer.
#include <cstdint>
#include <string>

#include <juce_gui_basics/juce_gui_basics.h>

namespace mnky::app {

class AppState;
class LinkClient;

class TransportStrip final : public juce::Component {
 public:
  TransportStrip(AppState& state, LinkClient& link);

  void refresh();
  void paint(juce::Graphics& g) override;
  void resized() override;

 private:
  AppState& state_;
  LinkClient& link_;
  juce::TextButton click_;

  std::string stateText_ = "STOP";
  std::string positionText_ = "001:1:00 / 00:00";
  std::string tempoText_ = "120.00 BPM";
  bool dirty_ = false;
  bool clickOn_ = false;

  JUCE_DECLARE_NON_COPYABLE(TransportStrip)
};

}  // namespace mnky::app

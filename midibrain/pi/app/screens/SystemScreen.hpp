#pragma once
// System screen: link/clock/READY diagnostics, audio and MIDI backend summary,
// and the in-app log tail. This is the only place a user can see why something
// is silent on a headless Pi.
#include "Screen.hpp"

namespace mnky::app {

class SystemScreen final : public Screen {
 public:
  explicit SystemScreen(AppContext& context);

  void resized() override;
  void refresh() override;

 private:
  juce::TextEditor stats_;
  juce::TextEditor logTail_;
  juce::String statsCache_;
  juce::String logCache_;

  JUCE_DECLARE_NON_COPYABLE(SystemScreen)
};

}  // namespace mnky::app

#pragma once
// Routing screen: source x destination matrix, per-source MIDI channel mask and
// transpose. Mirrors mnky::midi::Router exactly; all writes go through MidiGlue,
// which guards the matrix against the MIDI input thread.
#include "Screen.hpp"

namespace mnky::app {

class RoutingScreen final : public Screen {
 public:
  explicit RoutingScreen(AppContext& context);

  void paint(juce::Graphics& g) override;
  void resized() override;

 private:
  static constexpr int kSourceCount = 3;
  static constexpr int kChannelCount = 16;

  void applyRoute(int source, int destination);
  void syncRoute(int source, int destination);
  void applyMask(int source);
  void syncMask(int source);
  void applyTranspose(int source);

  juce::Label sourceLabel_[kSourceCount];
  juce::Label destLabel_[kSourceCount];
  juce::Label maskLabel_[kSourceCount];
  juce::Label transposeLabel_[kSourceCount];
  juce::TextButton route_[kSourceCount][kSourceCount];
  juce::TextButton mask_[kSourceCount][kChannelCount];
  juce::Slider transpose_[kSourceCount];

  JUCE_DECLARE_NON_COPYABLE(RoutingScreen)
};

}  // namespace mnky::app

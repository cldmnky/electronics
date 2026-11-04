#pragma once
// Routing screen: a node-wire patchbay (three inputs on the left, three outputs
// on the right) over the per-source MIDI channel filters and transpose. Every
// write goes through MidiGlue, which guards the matrix against the MIDI input
// thread.
//
// Patchbay gestures: tap a wire to toggle that route, or tap an input and then
// an output. The selected input is the one whose routes stay bright; every
// other route dims. RESET restores routing::kDefaultRoutes.
#include <cstdint>

#include "Screen.hpp"

#include "routing_model.hpp"

namespace mnky::app {

class RoutingPatchbay final : public juce::Component {
 public:
  explicit RoutingPatchbay(AppContext& context);

  void paint(juce::Graphics& g) override;
  void resized() override;
  void mouseUp(const juce::MouseEvent& event) override;

  // Re-reads the live USB device counts and repaints only when they changed.
  void refresh();
  void clearSelection();

 private:
  void toggleRoute(int source, int destination);
  void paintWire(juce::Graphics& g, int source, int destination, bool enabled,
                 bool selected);
  void paintNode(juce::Graphics& g, juce::Rectangle<float> area, const juce::String& name,
                 const juce::String& detail, bool selected, bool active, bool highlight,
                 bool showIndicator);

  AppContext& context_;
  routing::Layout layout_;
  int selectedInput_ = -1;
  int usbInputs_ = -1;
  int usbOutputs_ = -1;
  std::uint32_t routeSignature_ = 0;

  JUCE_DECLARE_NON_COPYABLE(RoutingPatchbay)
};

class RoutingScreen final : public Screen {
 public:
  explicit RoutingScreen(AppContext& context);

  void paint(juce::Graphics& g) override;
  void resized() override;
  void refresh() override;

 private:
  static constexpr int kChannelCount = 16;

  void resetRoutes();
  void applyMask(int source);
  void syncMask(int source);
  void applyTranspose(int source);

  RoutingPatchbay patchbay_;
  juce::TextButton resetButton_;
  juce::Label maskLabel_[routing::kInputCount];
  juce::Label transposeLabel_[routing::kInputCount];
  juce::TextButton mask_[routing::kInputCount][kChannelCount];
  juce::Slider transpose_[routing::kInputCount];
  juce::Rectangle<int> captionArea_;

  JUCE_DECLARE_NON_COPYABLE(RoutingScreen)
};

}  // namespace mnky::app

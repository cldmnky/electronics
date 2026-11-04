#pragma once
// Non-visual model behind the routing screen: the three inputs and three
// outputs the patchbay shows, the documented default route matrix that RESET
// restores, and the node/wire geometry. Geometry is a pure function of the
// patchbay bounds (fractions only), so resizing the window re-derives every
// node and wire without pixel constants and the unit tests can assert the
// hit areas at more than one panel size.
#include <array>
#include <cstddef>
#include <limits>

#include <juce_gui_basics/juce_gui_basics.h>

#include "../midi/router.hpp"

namespace mnky::app::routing {

inline constexpr int kInputCount = 3;
inline constexpr int kOutputCount = 3;

// Patchbay order, left to right on each side. The index is the matrix index the
// screen uses; the midi::Port is what Router and MidiGlue are keyed by.
inline constexpr std::array<midi::Port, kInputCount> kInputs = {
    midi::Port::TrsIn1, midi::Port::TrsIn2, midi::Port::UsbIn};
inline constexpr std::array<midi::Port, kOutputCount> kOutputs = {
    midi::Port::TrsOut1, midi::Port::TrsOut2, midi::Port::UsbOut};

inline constexpr std::array<const char*, kInputCount> kInputNames = {
    "TRS IN 1", "TRS IN 2", "USB IN"};
inline constexpr std::array<const char*, kOutputCount> kOutputNames = {
    "TRS OUT 1", "TRS OUT 2", "USB OUT"};

// Documented hub default (MainComponent's startup wiring and the Routing
// screen's RESET both read this table): both TRS inputs are mirrored to USB so
// a keyboard on either DIN port plays the DAW, and nothing else is routed.
// UsbIn -> UsbOut stays off because it would echo our own virtual port straight
// back out.
inline constexpr std::array<std::array<bool, kOutputCount>, kInputCount>
    kDefaultRoutes = {{
        {false, false, true},   // TRS IN 1 -> USB OUT
        {false, false, true},   // TRS IN 2 -> USB OUT
        {false, false, false},  // USB IN reaches nothing by default
    }};

// One cubic wire from the right edge of an input node to the left edge of an
// output node. start/end are kept for the connection dots the paint draws.
struct Wire {
  juce::Path path;
  juce::Point<float> start;
  juce::Point<float> end;
};

struct Layout {
  juce::Rectangle<float> inputs[kInputCount];
  juce::Rectangle<float> outputs[kOutputCount];
  Wire wires[kInputCount][kOutputCount];
  float nodeWidth = 0.0f;
  float nodeHeight = 0.0f;
  float wireThickness = 0.0f;
  float hitSlop = 0.0f;
};

// Fractions of the patchbay bounds. 3 nodes at 24% height leave 14% gaps, so a
// node is 45 px tall in the 788 x 188 graph a 800 x 480 panel gets.
inline constexpr float kNodeWidthFraction = 0.20f;
inline constexpr float kNodeHeightFraction = 0.24f;
inline constexpr float kWireThicknessFraction = 0.045f;
inline constexpr float kWireHandleFraction = 0.45f;
// Generous tap slop for a wire, in fractions of the node height: ~20 px at the
// 800 x 480 panel size.
inline constexpr float kWireHitSlopFraction = 0.45f;

inline Layout layoutFor(juce::Rectangle<float> area) {
  Layout layout;
  if (area.isEmpty()) return layout;

  layout.nodeWidth = area.getWidth() * kNodeWidthFraction;
  layout.nodeHeight = area.getHeight() * kNodeHeightFraction;
  layout.wireThickness = layout.nodeHeight * kWireThicknessFraction;
  layout.hitSlop = layout.nodeHeight * kWireHitSlopFraction;

  for (int index = 0; index < kInputCount; ++index) {
    const float centreY = area.getY() + area.getHeight() *
                                             (static_cast<float>(index) + 0.5f) /
                                             static_cast<float>(kInputCount);
    const float top = centreY - layout.nodeHeight * 0.5f;
    layout.inputs[index] = {area.getX(), top, layout.nodeWidth, layout.nodeHeight};
    layout.outputs[index] = {area.getRight() - layout.nodeWidth, top, layout.nodeWidth,
                             layout.nodeHeight};
  }

  for (int source = 0; source < kInputCount; ++source) {
    for (int destination = 0; destination < kOutputCount; ++destination) {
      const auto from = juce::Point<float>{layout.inputs[source].getRight(),
                                           layout.inputs[source].getCentreY()};
      const auto to = juce::Point<float>{layout.outputs[destination].getX(),
                                         layout.outputs[destination].getCentreY()};
      const float handle = (to.x - from.x) * kWireHandleFraction;

      auto& wire = layout.wires[source][destination];
      wire.start = from;
      wire.end = to;
      wire.path.startNewSubPath(from);
      wire.path.cubicTo(from.x + handle, from.y, to.x - handle, to.y, to.x, to.y);
    }
  }
  return layout;
}

inline float distanceToSegment(juce::Point<float> point, juce::Point<float> a,
                               juce::Point<float> b) {
  const juce::Point<float> ab = b - a;
  const float lengthSquared = ab.x * ab.x + ab.y * ab.y;
  if (lengthSquared <= 0.0f) return point.getDistanceFrom(a);
  float t = ((point.x - a.x) * ab.x + (point.y - a.y) * ab.y) / lengthSquared;
  t = juce::jlimit(0.0f, 1.0f, t);
  return point.getDistanceFrom({a.x + ab.x * t, a.y + ab.y * t});
}

// Nearest wire within the touch slop, so a tap between two crossing wires
// toggles the visibly closer one. False when the tap misses every wire.
inline bool hitWire(const Layout& layout, juce::Point<float> point, int& source,
                    int& destination) {
  bool found = false;
  float best = layout.hitSlop;
  for (int s = 0; s < kInputCount; ++s) {
    for (int d = 0; d < kOutputCount; ++d) {
      float nearest = std::numeric_limits<float>::max();
      juce::PathFlatteningIterator iterator(layout.wires[s][d].path);
      while (iterator.next()) {
        nearest = juce::jmin(
            nearest, distanceToSegment(point, {iterator.x1, iterator.y1},
                                       {iterator.x2, iterator.y2}));
      }
      if (nearest <= layout.hitSlop && (!found || nearest < best)) {
        best = nearest;
        source = s;
        destination = d;
        found = true;
      }
    }
  }
  return found;
}

}  // namespace mnky::app::routing

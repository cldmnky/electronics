#pragma once
// Non-visual model behind the Routing screen: the three inputs and three
// outputs the patchbay shows, the documented default route matrix that RESET
// restores, and the node/wire geometry. Geometry is a pure function of the
// patchbay size (fractions only), so the QML canvas and the unit tests derive
// the same nodes and wire hit areas at every panel size.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>

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

// Documented hub default (controller startup wiring and the Routing screen's
// RESET both read this table): both TRS inputs are mirrored to USB so a
// keyboard on either DIN port plays the DAW, and nothing else is routed.
// UsbIn -> UsbOut stays off because it would echo our own virtual port out.
inline constexpr std::array<std::array<bool, kOutputCount>, kInputCount>
    kDefaultRoutes = {{
        {false, false, true},   // TRS IN 1 -> USB OUT
        {false, false, true},   // TRS IN 2 -> USB OUT
        {false, false, false},  // USB IN reaches nothing by default
    }};

struct Point {
  float x = 0.0f;
  float y = 0.0f;
};

struct Rect {
  float x = 0.0f;
  float y = 0.0f;
  float width = 0.0f;
  float height = 0.0f;

  bool contains(Point p) const {
    return p.x >= x && p.x <= x + width && p.y >= y && p.y <= y + height;
  }
  float right() const { return x + width; }
  float bottom() const { return y + height; }
  float centreY() const { return y + height * 0.5f; }
  bool empty() const { return width <= 0.0f || height <= 0.0f; }
};

// One cubic wire from the right edge of an input node to the left edge of an
// output node. The control points are exposed so a QML Canvas can stroke the
// exact same curve.
struct Wire {
  Point start;
  Point control1;
  Point control2;
  Point end;
};

struct Layout {
  Rect inputs[kInputCount];
  Rect outputs[kOutputCount];
  Wire wires[kInputCount][kOutputCount];
  float nodeWidth = 0.0f;
  float nodeHeight = 0.0f;
  float wireThickness = 0.0f;
  float hitSlop = 0.0f;
};

// Fractions of the patchbay area: 3 nodes at 33% height keep each touch area
// at or above 44 px at the 800 x 480 panel size (the nodes just touch).
inline constexpr float kNodeWidthFraction = 0.20f;
inline constexpr float kNodeHeightFraction = 0.33f;
inline constexpr float kWireThicknessFraction = 0.045f;
inline constexpr float kWireHandleFraction = 0.45f;
// Generous tap slop for a wire, in fractions of the node height.
inline constexpr float kWireHitSlopFraction = 0.50f;

inline Layout layoutFor(float width, float height) {
  Layout layout;
  if (width <= 0.0f || height <= 0.0f) return layout;

  layout.nodeWidth = width * kNodeWidthFraction;
  layout.nodeHeight = height * kNodeHeightFraction;
  layout.wireThickness = layout.nodeHeight * kWireThicknessFraction;
  layout.hitSlop = layout.nodeHeight * kWireHitSlopFraction;

  for (int index = 0; index < kInputCount; ++index) {
    const float centreY =
        height * (static_cast<float>(index) + 0.5f) / static_cast<float>(kInputCount);
    const float top = centreY - layout.nodeHeight * 0.5f;
    layout.inputs[index] = {0.0f, top, layout.nodeWidth, layout.nodeHeight};
    layout.outputs[index] = {width - layout.nodeWidth, top, layout.nodeWidth,
                             layout.nodeHeight};
  }

  for (int source = 0; source < kInputCount; ++source) {
    for (int destination = 0; destination < kOutputCount; ++destination) {
      const Point from{layout.inputs[source].right(), layout.inputs[source].centreY()};
      const Point to{layout.outputs[destination].x, layout.outputs[destination].centreY()};
      const float handle = (to.x - from.x) * kWireHandleFraction;
      auto& wire = layout.wires[source][destination];
      wire.start = from;
      wire.control1 = {from.x + handle, from.y};
      wire.control2 = {to.x - handle, to.y};
      wire.end = to;
    }
  }
  return layout;
}

inline float distanceToSegment(Point point, Point a, Point b) {
  const float abx = b.x - a.x;
  const float aby = b.y - a.y;
  const float lengthSquared = abx * abx + aby * aby;
  if (lengthSquared <= 0.0f) {
    const float dx = point.x - a.x;
    const float dy = point.y - a.y;
    return std::sqrt(dx * dx + dy * dy);
  }
  float t = ((point.x - a.x) * abx + (point.y - a.y) * aby) / lengthSquared;
  t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
  const float dx = point.x - (a.x + abx * t);
  const float dy = point.y - (a.y + aby * t);
  return std::sqrt(dx * dx + dy * dy);
}

// Distance from a point to a cubic Bezier, sampled densely enough for an
// 800 px panel. The QML hit test calls the same function through the view
// model, so a tap that paints on a wire also toggles that wire.
inline float distanceToCubic(Point point, const Wire& wire, int samples = 24) {
  float best = std::numeric_limits<float>::max();
  Point previous = wire.start;
  for (int i = 1; i <= samples; ++i) {
    const float t = static_cast<float>(i) / static_cast<float>(samples);
    const float u = 1.0f - t;
    const Point current{
        u * u * u * wire.start.x + 3.0f * u * u * t * wire.control1.x +
            3.0f * u * t * t * wire.control2.x + t * t * t * wire.end.x,
        u * u * u * wire.start.y + 3.0f * u * u * t * wire.control1.y +
            3.0f * u * t * t * wire.control2.y + t * t * t * wire.end.y};
    best = std::min(best, distanceToSegment(point, previous, current));
    previous = current;
  }
  return best;
}

// Nearest wire within the touch slop, so a tap between two crossing wires
// toggles the visibly closer one. False when the tap misses every wire.
inline bool hitWire(const Layout& layout, Point point, int& source, int& destination) {
  bool found = false;
  float best = layout.hitSlop;
  for (int s = 0; s < kInputCount; ++s) {
    for (int d = 0; d < kOutputCount; ++d) {
      const float nearest = distanceToCubic(point, layout.wires[s][d]);
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

// Node hit tests used by the tap handler before the wire test.
inline int hitInputNode(const Layout& layout, Point point) {
  for (int source = 0; source < kInputCount; ++source) {
    if (layout.inputs[source].contains(point)) return source;
  }
  return -1;
}

inline int hitOutputNode(const Layout& layout, Point point) {
  for (int destination = 0; destination < kOutputCount; ++destination) {
    if (layout.outputs[destination].contains(point)) return destination;
  }
  return -1;
}

}  // namespace mnky::app::routing

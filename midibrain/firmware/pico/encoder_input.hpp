#pragma once
// Host-testable decode for the front-panel value knob. The fitted Bourns
// PEC11R-4015F-S0024 is detentless and endless: 24 quadrature pulses per
// revolution, 360 degrees continuous, no detent marks. Four edges therefore
// complete one base step and the measured step rate scales that step, so a
// slow turn stays 1:1 while a flick crosses a long range.
//
// The knob carries no fixed meaning here. main.cpp reports every delta as
// CTL_ENCODER_TURN and the Pi applies it to the focused control (tempo when no
// panel is open); the local tempo generator is driven only by EV_SET_TEMPO.
//
// Contact bounce is handled by the quadrature state table: a chatter pair walks
// the state backwards and cancels in the edge accumulator instead of producing
// a step, so the Bourns 2 ms maximum bounce cannot invent a turn.
#include <cstdint>

namespace mb {

// 24 pulses per revolution x 4 edges per pulse = 96 edges per turn.
constexpr int kEdgesPerStep = 4;

// Step-interval buckets in microseconds. The 1 kHz control scan observes at
// most one edge per millisecond, so the fastest reachable step interval is
// about 4 ms; every bucket below stays reachable at that rate.
struct EncoderAcceleration {
  std::uint32_t slow_us = 40'000;    // >= 40 ms/step (~1 turn/s): x1
  std::uint32_t medium_us = 20'000;  // >= 20 ms/step: x2
  std::uint32_t fast_us = 8'000;     // >= 8 ms/step: x4, faster: x8
};

class ValueEncoder {
 public:
  struct Result {
    bool turned = false;
    std::int8_t delta = 0;  // signed int8 CTL_ENCODER_TURN payload
  };

  ValueEncoder() = default;

  // Feed the raw A/B levels (true = logic high, i.e. gpio_get) once per scan
  // with the monotonic microsecond clock. `fine` is the SHIFT/EXIT level: in
  // fine mode every step stays x1 so the Pi's fine increment is not multiplied.
  Result update(bool a, bool b, bool fine, std::uint32_t now_us) {
    const std::uint8_t state = static_cast<std::uint8_t>((a ? 2u : 0u) | (b ? 1u : 0u));
    const int edge = kQuadrature[(previous_ << 2) | state];
    previous_ = state;
    if (edge == 0) return {};
    edges_ += edge;
    if (edges_ < kEdgesPerStep && edges_ > -kEdgesPerStep) return {};
    const int direction = edges_ > 0 ? 1 : -1;
    edges_ -= direction * kEdgesPerStep;
    return emit(direction, fine, now_us);
  }

  // Seed the state from the idle levels so the first scan cannot report the
  // power-on transition as a turn.
  void reset(bool a, bool b) {
    previous_ = static_cast<std::uint8_t>((a ? 2u : 0u) | (b ? 1u : 0u));
    edges_ = 0;
    have_step_ = false;
  }

 private:
  // Index = (previous A<<1|B) << 2 | (current A<<1|B); +1/-1 is one valid
  // single-channel edge in that direction, 0 is no change or both channels at
  // once (unreachable by a mechanical encoder between two scans).
  static constexpr int kQuadrature[16] = {0, -1, 1, 0, 1, 0, 0, -1,
                                          -1, 0, 0, 1, 0, 1, -1, 0};

  Result emit(int direction, bool fine, std::uint32_t now_us) {
    int multiplier = 1;
    if (!fine && have_step_) {
      const std::uint32_t interval = now_us - last_step_us_;
      multiplier = interval >= acceleration_.slow_us     ? 1
                   : interval >= acceleration_.medium_us ? 2
                   : interval >= acceleration_.fast_us   ? 4
                                                         : 8;
    }
    have_step_ = true;
    last_step_us_ = now_us;
    return {true, static_cast<std::int8_t>(direction * multiplier)};
  }

  EncoderAcceleration acceleration_{};
  std::uint8_t previous_ = 0;
  int edges_ = 0;
  bool have_step_ = false;
  std::uint32_t last_step_us_ = 0;
};

}  // namespace mb

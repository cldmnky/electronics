#pragma once
// Host-testable panel switch decoding shared by the Pico firmware and
// protocol_test.cpp: 10 ms debounce, NAV direction auto-repeat and the
// SHIFT/EXIT long-hold. main.cpp owns the GPIO reads and the side effects;
// this class owns only which EV_CONTROL event a raw level produces and when.
// Control ids and state values live in midi.hpp (single registry).
#include <cstdint>

#include "midi.hpp"

namespace mb {

constexpr std::uint32_t kPanelDebounceMs = 10;

// One switch's decode path. Feed the RAW active-high level (true = pressed)
// every 1 kHz scan with the same monotonic millisecond clock the debouncer
// uses. `changed` marks a debounced level change (panel transport and record
// arming key off that edge); `event` marks an EV_CONTROL frame to emit.
class PanelControl {
 public:
  struct Result {
    bool changed = false;
    bool event = false;
    std::uint8_t value = 0;
  };

  explicit PanelControl(std::uint8_t id, std::uint32_t debounce_ms = kPanelDebounceMs,
                        std::uint32_t long_ms = kShiftLongMs,
                        std::uint32_t repeat_initial_ms = kNavRepeatInitialMs,
                        std::uint32_t repeat_ms = kNavRepeatMs)
      : id_(id), deb_(debounce_ms), long_(long_ms), repeat_(repeat_initial_ms, repeat_ms) {}

  Result update(bool raw_pressed, std::uint32_t now_ms) {
    Result r{};
    r.changed = deb_.update(raw_pressed, now_ms);
    const bool pressed = deb_.state();
    if (id_ == CTL_SHIFT_EXIT) {
      // Press (1) / release (0) edges plus one long-hold (2) past 500 ms.
      const int state = long_.update(pressed, now_ms);
      if (state >= 0) {
        r.event = true;
        r.value = static_cast<std::uint8_t>(state);
      }
    } else if (is_nav_direction(id_)) {
      // Press edge and auto-repeat both report PRESSED; release reports
      // RELEASED once the debouncer confirms it. NAV_PRESS never repeats.
      if (repeat_.update(pressed, now_ms)) {
        r.event = true;
        r.value = CTL_STATE_PRESSED;
      } else if (r.changed && !pressed) {
        r.event = true;
        r.value = CTL_STATE_RELEASED;
      }
    } else if (r.changed) {
      r.event = true;
      r.value = pressed ? CTL_STATE_PRESSED : CTL_STATE_RELEASED;
    }
    return r;
  }

  std::uint8_t id() const { return id_; }
  bool pressed() const { return deb_.state(); }

 private:
  std::uint8_t id_;
  Debouncer deb_;
  ShiftHold long_;
  NavRepeat repeat_;
};

}  // namespace mb

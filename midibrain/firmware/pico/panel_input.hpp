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

// Grouped decoding for the NAV stick (ALPS RKJXT1F42001). The page-399
// output-relation chart closes `Push-Com` in EVERY lever position - the A, B,
// C, D columns and the Center push column alike - and the board ties `com` to
// GND and `Push` to NAV_PRESS. A tilt therefore grounds NAV_PRESS together
// with its direction line, so five independent decoders report every
// navigation step as a select as well. No wiring can separate them: the fix
// is that a centre press is a qualified gesture.
//
// NavPressGate qualifies the raw Push level before the press decoder sees it:
// a press counts only when it starts with all four direction lines inactive
// and the gate armed. Any direction activity disarms the gate (a tilt's Push
// closure is swallowed, and a press held through a tilt is released), and the
// gate re-arms only after the direction lines are neutral again AND Push has
// been released - so the next select needs a fresh press edge in a neutral
// group. Directions are never gated: the chart's Push-Com closure is what
// makes them work.
//
// The gate takes RAW levels (before debounce): suppression must not lag the
// contact that causes it. Feed its return value to the NAV_PRESS PanelControl.
class NavPressGate {
 public:
  // direction_raw_any: at least one direction input active in this scan.
  // push_raw: the NAV_PRESS input active in this scan.
  // Returns the level the press decoder must see.
  bool update(bool direction_raw_any, bool push_raw) {
    if (direction_raw_any) {
      armed_ = false;
      return false;
    }
    if (!push_raw) {
      armed_ = true;
      return false;
    }
    return armed_;
  }

 private:
  bool armed_ = true;
};

}  // namespace mb

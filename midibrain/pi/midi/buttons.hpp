#pragma once
// Panel control -> app action mapping for the mnkybox Pi core. Pure and
// host-testable: the app feeds EV_CONTROL ids/values, this decides what the UI
// does. Control ids are the single registry in firmware/pico/midi.hpp (never
// duplicated here).
#include <cstdint>

#include "../../firmware/pico/midi.hpp"

namespace mnky::midi {

enum class Action : std::uint8_t {
  None,
  Play,          // start if stopped, ignored while rolling
  PlayStart,     // restart from tick 0
  Stop,
  RecToggle,
  OverdubToggle,
  TempoCoarse,   // encoder detent
  TempoFine,     // encoder detent with SHIFT held
  NavUp,
  NavDown,
  NavLeft,
  NavRight,
  NavPress,
  EncoderSw,
};

// `value` is the EV_CONTROL payload state (1 press, 0 release) except for
// CTL_ENCODER_TURN, where it is the signed int8 detent delta. Actions fire on
// press/detent only; releases return None. SHIFT_EXIT is a modifier, so it
// returns None and reaches the caller through the `shift` argument.
constexpr Action mapControl(std::uint8_t id, std::uint8_t value, bool shift) {
  switch (id) {
    case mb::CTL_NAV_UP:
      return value ? Action::NavUp : Action::None;
    case mb::CTL_NAV_DOWN:
      return value ? Action::NavDown : Action::None;
    case mb::CTL_NAV_LEFT:
      return value ? Action::NavLeft : Action::None;
    case mb::CTL_NAV_RIGHT:
      return value ? Action::NavRight : Action::None;
    case mb::CTL_NAV_PRESS:
      return value ? Action::NavPress : Action::None;
    case mb::CTL_ENCODER_SW:
      return value ? Action::EncoderSw : Action::None;
    case mb::CTL_REC:
      return value ? Action::RecToggle : Action::None;
    case mb::CTL_OVER_DUB:
      return value ? Action::OverdubToggle : Action::None;
    case mb::CTL_STOP:
      return value ? Action::Stop : Action::None;
    case mb::CTL_PLAY:
      return value ? Action::Play : Action::None;
    case mb::CTL_PLAY_START:
      return value ? Action::PlayStart : Action::None;
    case mb::CTL_ENCODER_TURN:
      if (value == 0) return Action::None;
      return shift ? Action::TempoFine : Action::TempoCoarse;
    default:  // CTL_SHIFT_EXIT and unknown ids: no action of their own
      return Action::None;
  }
}

}  // namespace mnky::midi

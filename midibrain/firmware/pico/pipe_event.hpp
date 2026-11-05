#pragma once
// Firmware adapter for the shared MIDI FX pipe engine (midibrain/midifx/core).
// The core is the only pipe implementation; this header only turns the link's
// MidiMessage into the core PipeEvent, so main.cpp and the host test share one
// conversion and no pipe behavior lives in the firmware.
#include <cstdint>

#include "../../midifx/core/midifx.hpp"
#include "midi.hpp"

namespace mb {
inline mnky::midifx::PipeEvent pipeEvent(const MidiMessage& message) {
  mnky::midifx::PipeEvent event;
  // A malformed size is a dropped event, not a truncated one; the UART parser
  // only produces 1..3.
  if (message.size < 1 || message.size > 3) return event;
  event.size = message.size;
  for (std::uint8_t i = 0; i < message.size; ++i) event.data[i] = message.data[i];
  return event;
}
}  // namespace mb

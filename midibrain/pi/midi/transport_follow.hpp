#pragma once
// Pi-side USB transport follow (session ClockState.transportFollow[2]).
//
// USB is not a clock source: the Pico has no USB-device view, and forwarding
// 24-PPQN ticks over SPI would reintroduce the arrival jitter external clock
// follow removes. USB transport is different: START/CONTINUE/STOP are single
// bytes, so when the session enables USB transport follow the app forwards
// them to the Pico through the existing EV_TRANSPORT path
// (LinkClient::requestTransport -> PicoLink::transport). This class is only the
// classification/gating policy; it is host-tested in test_midi.cpp.
#include <cstdint>
#include <functional>

#include "clock.hpp"
#include "router.hpp"

namespace mnky::midi {

class UsbTransportFollow {
 public:
  // Sends an EV_TRANSPORT command through the caller's existing link path
  // (app: LinkClient::requestTransport).
  using CommandFn = std::function<void(std::uint8_t)>;

  // Default off: the Pico stays transport master until the session enables it.
  void setEnabled(bool enabled) { enabled_ = enabled; }
  bool enabled() const { return enabled_; }

  // EV_TRANSPORT command (0 stop, 1 start, 2 continue) for a USB transport
  // byte, or -1 when follow is disabled or the event is not a 1-byte USB
  // transport message. Clock (0xF8) and everything else is never forwarded.
  int commandFor(const MidiEvent& ev) const {
    if (!enabled_ || ev.source != Port::UsbIn || ev.size != 1) return -1;
    if (ev.data[0] == kStartByte) return 1;
    if (ev.data[0] == kContinueByte) return 2;
    if (ev.data[0] == kStopByte) return 0;
    return -1;
  }
  bool forwards(const MidiEvent& ev) const { return commandFor(ev) >= 0; }

  // Forwards the command when the event qualifies; returns true when `send`
  // was called. This is the app's one-call seam into EV_TRANSPORT.
  bool forward(const MidiEvent& ev, const CommandFn& send) const {
    const int command = commandFor(ev);
    if (command < 0) return false;
    send(static_cast<std::uint8_t>(command));
    return true;
  }

 private:
  bool enabled_ = false;
};

}  // namespace mnky::midi

#pragma once
// MIDI over USART1 (D13 TX / D14 RX), TRS Type A on the base board.
//
// MIDI OUT is a current loop behind Q2 (high-side PNP): UART idle (high) keeps
// the loop off, a start bit turns it on, which is what the MIDI spec wants. The
// firmware therefore only has to send normal UART bytes - no inversion.
//
// There is no serial console on this board (the only USART is the MIDI jack),
// so the diagnostic channel here is MIDI SysEx: see SendSysEx() and
// selftest.cpp.

#include <cstddef>
#include <cstdint>

#include "hid/midi.h"

namespace froemix
{
class MidiIo
{
  public:
    void Init();

    /** Drain the receive queue. Call from the main loop. */
    void Poll();

    uint32_t RxCount() const { return rx_count_; }
    uint8_t  LastStatus() const { return last_status_; }
    bool     RxActive() { return midi_.RxActive(); }

    /** Manufacturer SysEx with the froemix ID; payload is data bytes only. */
    void SendSysEx(const uint8_t* payload, size_t size);

  private:
    daisy::MidiUartHandler midi_;
    uint32_t               rx_count_    = 0;
    uint8_t                last_status_ = 0;
};
} // namespace froemix

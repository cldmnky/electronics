#pragma once
// Sync in / out.
//
// J_SYNC_IN reaches the MCU through Q1 (MMBT3904) with a 10k pull-up, so the
// pin reads LOW while the jack is HIGH: the input is inverted (contracts.hpp).
// J_SYNC_OUT is a TL074 comparator/inverter chain that turns the pin's 0/3.3 V
// into the jack's 0/5 V gate, so the firmware only drives a logic level - it
// does not define the analogue shape, and the maximum toggle rate is unmeasured
// (contracts.hpp, kSyncOutRateQualified).

#include <cstdint>

#include "per/gpio.h"

namespace froemix
{
class SyncIo
{
  public:
    void Init();

    /** Gate at J_SYNC_IN, un-inverted (true = jack high). */
    bool Read();

    /** Drive J_SYNC_OUT (true = ~5 V at the jack). */
    void Write(bool high);

  private:
    daisy::GPIO in_;
    daisy::GPIO out_;
    bool        last_pin_state_ = false;
};
} // namespace froemix

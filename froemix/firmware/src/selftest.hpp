#pragma once
// Bring-up diagnostics (milestone M1 in doc/FIRMWARE.md).
//
// There is no serial console on this board: the only USART is the MIDI jack.
// So this module reports as (a) ASCII text on the OLED and (b) a MIDI SysEx
// frame once per second, which a bench tool can log while the module runs.
//
// It exercises the nets in pins.hpp - it proves the firmware *reaches* them,
// not that the analogue path meets its specification. Values are therefore
// raw/quantised, and every threshold shown is the contract constant used.

#include <cstddef>
#include <cstdint>

namespace froemix
{
class Board;

class Selftest
{
  public:
    void Init(Board* board);

    /** Called from the ~1 kHz loop; emits one report per second. */
    void Tick();

    uint32_t ReportCount() const { return reports_; }

  private:
    void Report();

    Board*   board_    = nullptr;
    uint32_t ticks_    = 0;
    uint32_t reports_  = 0;
};
} // namespace froemix

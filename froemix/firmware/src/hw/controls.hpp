#pragma once
// Panel controls: the six pots, the 5-way joystick ladder and the MODE button,
// all read through the single CD4051 on D17.
//
// doc/EURO-BOARD.md §2/§7: the ladder and the button are a *firmware contract* -
// firmware decodes levels, not pins. Thresholds are in contracts.hpp and are
// derived from the ladder table, not measured yet.

#include <cstddef>
#include <cstdint>

#include "per/adc.h"

namespace froemix
{
class Controls
{
  public:
    enum class Joystick
    {
        kNone = 0,
        kUp,
        kDown,
        kLeft,
        kRight,
        kPress,
    };

    void Init(daisy::AdcHandle& adc);

    /** Read every channel. Call at a steady rate: the mux selects one channel
     ** at a time and its settle time is not qualified (contracts.hpp). */
    void Update();

    float    Pot(size_t index) const;      // 0..1, index 0..5 = P1..P6
    Joystick Joy() const { return joy_; }
    bool     ButtonDown() const { return button_; }

  private:
    daisy::AdcHandle* adc_    = nullptr;
    float             pot_[6] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    Joystick          joy_    = Joystick::kNone;
    bool              button_ = false;
};
} // namespace froemix

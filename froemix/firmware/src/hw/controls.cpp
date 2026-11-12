#include "hw/controls.hpp"

#include <cmath>

#include "contracts.hpp"

namespace froemix
{
namespace
{
constexpr uint8_t kAdcChannelMux = 0;
// P3V3_A is the mux supply and the ADC reference, so a pot reading is
// ratiometric; only the ladder table's absolute levels matter here.
constexpr float kAdcFullScaleVolts = 3.3f;
// Software hysteresis for the ladder decode: the mux Ron (~2 %) and ADC noise
// would otherwise chatter between adjacent levels. bench-tune in M1.
constexpr float kLadderHysteresisVolts = 0.05f;

// Band boundaries of the ladder, low to high (doc/EURO-BOARD.md §2).
//   press | up | down | left | right | idle
constexpr float kBounds[] = {
    contracts::kLadderUpMin,
    contracts::kLadderDownMin,
    contracts::kLadderLeftMin,
    contracts::kLadderRightMin,
    contracts::kLadderIdleMin,
};
// State below each boundary: press, up, down, left, right.
constexpr Controls::Joystick kBelow[] = {
    Controls::Joystick::kPress,
    Controls::Joystick::kUp,
    Controls::Joystick::kDown,
    Controls::Joystick::kLeft,
    Controls::Joystick::kRight,
};

Controls::Joystick DecodeLadder(float volts)
{
    for(size_t i = 0; i < sizeof(kBounds) / sizeof(kBounds[0]); ++i)
        if(volts < kBounds[i])
            return kBelow[i];
    return Controls::Joystick::kNone;
}
} // namespace

void Controls::Init(daisy::AdcHandle& adc)
{
    adc_ = &adc;
}

void Controls::Update()
{
    if(adc_ == nullptr)
        return;

    for(size_t i = 0; i < contracts::kMuxPotChannels; ++i)
        pot_[i] = adc_->GetMuxFloat(kAdcChannelMux, i);

    const float ladder = adc_->GetMuxFloat(kAdcChannelMux,
                                           contracts::kMuxJoystickChannel)
                         * kAdcFullScaleVolts;
    const float button
        = adc_->GetMuxFloat(kAdcChannelMux, contracts::kMuxButtonChannel)
          * kAdcFullScaleVolts;

    // Decode, but never leave the current state within one hysteresis band of a
    // boundary: a level that sits on the line keeps its last decision.
    const Joystick decoded = DecodeLadder(ladder);
    Joystick       next    = decoded;
    if(decoded != joy_)
    {
        for(size_t i = 0; i < sizeof(kBounds) / sizeof(kBounds[0]); ++i)
        {
            if(std::fabs(ladder - kBounds[i]) < kLadderHysteresisVolts)
            {
                next = joy_;
                break;
            }
        }
    }
    joy_ = next;

    if(button < contracts::kButtonPressedMax)
        button_ = true;
    else if(button > contracts::kButtonPressedMax + kLadderHysteresisVolts)
        button_ = false;
}

float Controls::Pot(size_t index) const
{
    return index < contracts::kMuxPotChannels ? pot_[index] : 0.0f;
}
} // namespace froemix

#include "hw/pmod.hpp"

#include "contracts.hpp"

namespace froemix
{
namespace
{
// PC9635 register image (registers 0x00..0x17), from the eurorack-pmod
// reference driver. MODE1/MODE2 set the oscillator/PWM behaviour that the
// LEDOUT registers below assume.
constexpr uint8_t kLedConfig[] = {
    0x81, // 0x00 MODE1
    0x01, // 0x01 MODE2
    0x00, // 0x02 PWM0
    0x00, // 0x03 PWM1
    0x08, // 0x04 PWM2
    0x08, // 0x05 PWM3
    0x04, // 0x06 PWM4
    0x04, // 0x07 PWM5
    0x00, // 0x08 PWM6
    0x10, // 0x09 PWM7
    0x10, // 0x0A PWM8
    0x10, // 0x0B PWM9
    0x10, // 0x0C PWM10
    0x10, // 0x0D PWM11
    0x01, // 0x0E PWM12
    0x01, // 0x0F PWM13
    0x10, // 0x10 PWM14
    0x10, // 0x11 PWM15
    0xFF, // 0x12 GRPPWM
    0x00, // 0x13 GRPFREQ
    0xAA, // 0x14 LEDOUT0
    0xAA, // 0x15 LEDOUT1
    0xAA, // 0x16 LEDOUT2
    0xAA, // 0x17 LEDOUT3
};

constexpr uint8_t kPwmRegisterBase = 0x02;
} // namespace

bool Pmod::Init(daisy::I2CHandle& i2c, uint32_t timeout_ms)
{
    i2c_        = &i2c;
    leds_ready_ = false;

    if(!codec_.Init(i2c, timeout_ms))
        return false;

    for(size_t i = 0; i < sizeof(kLedConfig); ++i)
    {
        if(!WriteLedRegister(static_cast<uint8_t>(i), kLedConfig[i]))
            return false;
    }

    leds_ready_ = true;
    return true;
}

bool Pmod::SetLed(uint8_t index, uint8_t brightness)
{
    if(!leds_ready_ || index >= kLedCount)
        return false;
    return WriteLedRegister(kPwmRegisterBase + index, brightness);
}

void Pmod::LedsOff()
{
    for(uint8_t i = 0; i < kLedCount; ++i)
        SetLed(i, 0);
}

bool Pmod::WriteLedRegister(uint8_t reg, uint8_t value)
{
    return i2c_->WriteDataAtAddress(contracts::kPc9635I2cAddress,
                                    reg,
                                    1,
                                    &value,
                                    1,
                                    250)
           == daisy::I2CHandle::Result::OK;
}
} // namespace froemix

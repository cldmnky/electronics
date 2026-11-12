#pragma once
// eurorack-pmod (apfelaudio, H1) support: AK4619 codec plus the PC9635 LED
// driver that sits on the same I2C1 bus.
//
// Source: heartwerker/libDaisy feature/audio-tdm-codec-ak4619 and the
// eurorack-pmod hardware documentation (https://github.com/apfaudio/eurorack-pmod).
//
// What this file does NOT do yet (doc/FIRMWARE.md, "not in this scaffold"):
//   - jack insertion detection,
//   - the touch/proximity controller,
//   - the calibration EEPROM,
//   - the ^MUTE pin: H1 pin 6 is tied to P3V3_D on this board, so the module is
//     permanently unmuted and firmware has no control over it.

#include <cstdint>

#include "per/i2c.h"

#include "hw/ak4619.hpp"

namespace froemix
{
class Pmod
{
  public:
    static constexpr uint8_t kLedCount = 16;

    /** Initialize the codec and the LED driver. Returns false if the module is
     ** absent or not answering - the firmware then keeps the PMOD channels
     ** silent instead of stalling the boot. */
    bool Init(daisy::I2CHandle& i2c, uint32_t timeout_ms = 1000);

    bool Ready() const { return codec_.Ready() && leds_ready_; }

    /** PWM brightness for one LED (0 = off, 255 = full). */
    bool SetLed(uint8_t index, uint8_t brightness);

    /** All LEDs off (the driver's own LEDOUT registers stay on bank/PWM). */
    void LedsOff();

  private:
    bool WriteLedRegister(uint8_t reg, uint8_t value);

    Ak4619            codec_;
    daisy::I2CHandle* i2c_        = nullptr;
    bool              leds_ready_ = false;
};
} // namespace froemix

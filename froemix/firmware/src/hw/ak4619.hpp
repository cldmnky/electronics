#pragma once
// AK4619VN codec on the eurorack-pmod, I2C-only configuration.
//
// Source: heartwerker/libDaisy branch feature/audio-tdm-codec-ak4619
//         (src/dev/codec_ak4619.cpp, MIT) which is the driver the eurorack-pmod
//         Daisy example runs; register comments follow its commit history and
//         the AK4619VN datasheet 9.14 "Register Map".
// This is a port into the firmware tree (no libDaisy patch) so the codec
// configuration is ours to review; the *audio* side is SAI2 TDM in
// audio/audio6.cpp. Not yet validated on hardware: doc/FIRMWARE.md.

#include <cstdint>

#include "daisy_core.h"
#include "per/i2c.h"

namespace froemix
{
class Ak4619
{
  public:
    /** Configure the codec for 4-slot TDM, 32-bit slots, 48 kHz, 256*fs MCLK.
     ** `timeout_ms` bounds the retry loop: the codec only answers once the
     ** eurorack-pmod has power (its PDN/^MUTE pin is wired high on H1 pin 6).
     */
    bool Init(daisy::I2CHandle& i2c, uint32_t timeout_ms = 1000);

    bool Ready() const { return ready_; }

  private:
    bool WriteRegister(uint8_t reg, uint8_t value);

    daisy::I2CHandle* i2c_;
    bool              ready_;
};
} // namespace froemix

#include "hw/ak4619.hpp"

#include "contracts.hpp"
#include "sys/system.h"

namespace froemix
{
namespace
{
// Register image, AK4619VN datasheet 9.14, as used by the eurorack-pmod
// reference driver (see hw/ak4619.hpp for provenance).
constexpr uint8_t kConfig[] = {
    0x37, // 0x00 Power Management (all normal operation)
    0xAF, // 0x01 Audio I/F Format: TDM=1, DCF=I2S compatible, 32-bit, rising BICK
    0x1C, // 0x02 Reset Control: SLOT=1, DAC in 32-bit, DAC out 24-bit
    0x00, // 0x03 System Clock: FS=000 (MCLK = 256*fs, BICK = 128*fs, 48 kHz)
    0x44, // 0x04 MIC amp gain +6 dB (unused path, kept at the reference value)
    0x44, // 0x05 MIC amp gain +6 dB
    0x30, // 0x06 ADC1 L digital volume 0 dB
    0x30, // 0x07 ADC1 R digital volume 0 dB
    0x30, // 0x08 ADC2 L digital volume 0 dB
    0x30, // 0x09 ADC2 R digital volume 0 dB
    0x22, // 0x0A ADC digital filter: short delay sharp roll-off
    0x55, // 0x0B ADC analog input: all single-ended (AIN1L/R, AIN4L/R)
    0x00, // 0x0C reserved
    0x06, // 0x0D ADC mute/HPF: soft mute off, DC-cancel HPF disabled
    0x18, // 0x0E DAC1 L digital volume: 0x18 = 0 dB
    0x18, // 0x0F DAC1 R digital volume
    0x18, // 0x10 DAC2 L digital volume
    0x18, // 0x11 DAC2 R digital volume
    0x04, // 0x12 DAC input select: DAC1 = SDIN1, DAC2 = SDIN2
    0x05, // 0x13 DAC de-emphasis: off
    0x0A, // 0x14 DAC mute/filter
};

constexpr uint32_t kRegisterDelayMs = 4;
} // namespace

bool Ak4619::Init(daisy::I2CHandle& i2c, uint32_t timeout_ms)
{
    i2c_  = &i2c;
    ready_ = false;

    // Probe first: retrying the full register image masks a missing module as
    // "slow startup" and blocks the audio path.
    uint8_t probe   = 0;
    uint32_t waited = 0;
    while(waited < timeout_ms)
    {
        if(i2c_->ReadDataAtAddress(contracts::kAk4619I2cAddress,
                                   0x00,
                                   1,
                                   &probe,
                                   1,
                                   250)
           == daisy::I2CHandle::Result::OK)
            break;
        daisy::System::Delay(10);
        waited += 10;
    }
    if(waited >= timeout_ms)
        return false;

    for(size_t i = 0; i < sizeof(kConfig); ++i)
    {
        if(!WriteRegister(static_cast<uint8_t>(i), kConfig[i]))
            return false;
        daisy::System::Delay(kRegisterDelayMs);
    }

    ready_ = true;
    return true;
}

bool Ak4619::WriteRegister(uint8_t reg, uint8_t value)
{
    return i2c_->WriteDataAtAddress(contracts::kAk4619I2cAddress,
                                    reg,
                                    1,
                                    &value,
                                    1,
                                    250)
           == daisy::I2CHandle::Result::OK;
}
} // namespace froemix

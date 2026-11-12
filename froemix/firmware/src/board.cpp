#include "board.hpp"

#include "contracts.hpp"
#include "pins.hpp"

namespace froemix
{
namespace
{
// ADC channel order, referenced by Controls and CvIo.
constexpr size_t kAdcChannelCount = 3;
constexpr size_t kAdcChannelMux   = 0;
constexpr size_t kAdcChannelCv1   = 1;
constexpr size_t kAdcChannelCv2   = 2;
} // namespace

bool Board::Init()
{
    // 1. Seed: clocks, SDRAM, and its own WM8731 codec (that codec hangs on the
    //    module's internal I2C2, not on the H1 bus, so there is no bus clash).
    seed_.Init();
    seed_.StartLog(false);

    // 2. The Seed configured SAI1 for its own AudioHandle. Audio6 owns SAI1 and
    //    SAI2, so release SAI1 first; the codec stays configured.
    //    bench: confirm the re-init below is equivalent to skipping the Seed's
    //    ConfigureAudio() (the eurorack-pmod reference patched that out).
    seed_.audio_handle.DeInit();

    // 3. I2C1 (D11/D12) is the eurorack-pmod bus (AK4619 + PC9635 + expanders).
    daisy::I2CHandle::Config i2c_cfg;
    i2c_cfg.periph         = daisy::I2CHandle::Config::Peripheral::I2C_1;
    i2c_cfg.mode           = daisy::I2CHandle::Config::Mode::I2C_MASTER;
    i2c_cfg.speed          = daisy::I2CHandle::Config::Speed::I2C_400KHZ;
    i2c_cfg.pin_config.scl = pins::kI2cScl;
    i2c_cfg.pin_config.sda = pins::kI2cSda;
    i2c_.Init(i2c_cfg);

    // 4a. ADC: channel 0 is the CD4051 common node; 1 and 2 are the two CV
    //     inputs, which do NOT go through the mux.
    daisy::AdcChannelConfig adc_cfg[kAdcChannelCount];
    adc_cfg[kAdcChannelMux].InitMux(pins::kMuxCommon,
                                    contracts::kMuxChannels,
                                    pins::kMuxS0,
                                    pins::kMuxS1,
                                    pins::kMuxS2);
    adc_cfg[kAdcChannelCv1].InitSingle(pins::kCvIn1);
    adc_cfg[kAdcChannelCv2].InitSingle(pins::kCvIn2);
    adc_.Init(adc_cfg, kAdcChannelCount);
    adc_.Start();

    // 4b. DAC: buffer DISABLED is a hard board contract (contracts.hpp).
    daisy::DacHandle::Config dac_cfg;
    dac_cfg.chn        = daisy::DacHandle::Channel::BOTH;
    dac_cfg.mode       = daisy::DacHandle::Mode::POLLING;
    dac_cfg.bitdepth   = daisy::DacHandle::BitDepth::BITS_12;
    dac_cfg.buff_state = daisy::DacHandle::BufferState::DISABLED;
    seed_.dac.Init(dac_cfg);

    // 5. GPIO + the two slow peripherals.
    panel_led_.Init(pins::kPanelLed, daisy::GPIO::Mode::OUTPUT);
    panel_led_.Write(0);

    controls_.Init(adc_);
    cv_.Init(adc_, seed_.dac);
    cv_.SetAdcChannels(kAdcChannelCv1, kAdcChannelCv2);
    sync_.Init();
    midi_.Init();
    status_.display = display_.Init();

    // 6. eurorack-pmod, then audio. A missing module must not stop the base
    //    board's own audio path, so the failure is recorded, not fatal.
    status_.pmod_codec = pmod_.Init(i2c_);
    status_.pmod_leds  = status_.pmod_codec; // Pmod::Init sets both or neither

    status_.audio = audio_.Init(seed_.CheckBoardVersion()
                                == daisy::DaisySeed::BoardVersion::DAISY_SEED_1_1);

    selftest_.Init(this);
    return status_.audio;
}

bool Board::StartAudio(Audio6::Callback cb)
{
    return audio_.Start(contracts::kAudioBlockSize, cb);
}

void Board::Loop()
{
    controls_.Update();
    midi_.Poll();

    // No engine yet: the panel LED mirrors MODE so the UI path is visible.
    // An engine replaces this (doc/FIRMWARE.md).
    panel_led_.Write(controls_.ButtonDown() ? 1 : 0);

    selftest_.Tick();
}
} // namespace froemix

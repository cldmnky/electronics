#include "hw/cv.hpp"

namespace froemix
{
namespace
{
// The ADC has no external reference: VDDA (the Seed's 3.3 V analogue rail) is
// the reference, so this is nominal until M2 measures it.
constexpr float kAdcFullScaleVolts = 3.3f;
constexpr float kDacFullScaleVolts = 3.3f;
// The DAC is configured for 12-bit codes (DacHandle::BitDepth::BITS_12).
constexpr float kDacMaxCode = 4095.0f;
} // namespace

void CvIo::Init(daisy::AdcHandle& adc, daisy::DacHandle& dac)
{
    adc_             = &adc;
    dac_             = &dac;
    adc_channel_[0]  = 1;
    adc_channel_[1]  = 2;
}

void CvIo::SetAdcChannels(uint8_t cv_in_1_channel, uint8_t cv_in_2_channel)
{
    adc_channel_[0] = cv_in_1_channel;
    adc_channel_[1] = cv_in_2_channel;
}

float CvIo::ReadIn(size_t index) const
{
    if(adc_ == nullptr || index >= kInCount)
        return 0.0f;

    // V(adc) = offset - scale * V(jack)   ->   V(jack) = (offset - V(adc)) / scale
    const float adc_volts = adc_->GetFloat(adc_channel_[index])
                            * kAdcFullScaleVolts;
    const float nominal = (contracts::kCvInOffset - adc_volts)
                          / contracts::kCvInScale;
    return nominal * in_cal_[index].gain + in_cal_[index].offset;
}

void CvIo::WriteOut(size_t index, float volts)
{
    if(dac_ == nullptr || index >= kOutCount)
        return;

    const float corrected = (volts - out_cal_[index].offset)
                            / out_cal_[index].gain;
    float dac_volts = corrected / contracts::kCvOutGain;

    if(dac_volts < 0.0f)
        dac_volts = 0.0f;
    if(dac_volts > kDacFullScaleVolts)
        dac_volts = kDacFullScaleVolts;

    const uint16_t code
        = static_cast<uint16_t>(dac_volts / kDacFullScaleVolts * kDacMaxCode);

    // The nets are crossed on this board (contracts.hpp): jack 1 is DAC
    // channel TWO, jack 2 is DAC channel ONE.
    const daisy::DacHandle::Channel channel
        = (index == 0) ? daisy::DacHandle::Channel::TWO
                       : daisy::DacHandle::Channel::ONE;
    dac_->WriteValue(channel, code);
}

void CvIo::SetInCalibration(size_t index, float gain, float offset)
{
    if(index < kInCount)
    {
        in_cal_[index].gain   = gain;
        in_cal_[index].offset = offset;
    }
}

void CvIo::SetOutCalibration(size_t index, float gain, float offset)
{
    if(index < kOutCount)
    {
        out_cal_[index].gain   = gain;
        out_cal_[index].offset = offset;
    }
}
} // namespace froemix

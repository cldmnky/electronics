#pragma once
// CV in / CV out, with the calibration the board documentation requires.
//
// The resistor paths are board properties (contracts.hpp). The per-channel
// gain/offset added here are the *measured* corrections: until M2 has measured
// them this class reports CalibrationValid() == false and applies the nominal
// values only. Nothing here can compensate a missing endpoint (DESIGN-REVIEW
// R03) - that is what U11A/U11D and the disabled DAC buffer are for.

#include <cstddef>

#include "contracts.hpp"
#include "per/adc.h"
#include "per/dac.h"

namespace froemix
{
class CvIo
{
  public:
    static constexpr size_t kInCount  = 2; // J_CV_IN_1 / 2
    static constexpr size_t kOutCount = 2; // J_CV_OUT_1 / 2

    void Init(daisy::AdcHandle& adc, daisy::DacHandle& dac);

    /** ADC channel indices of CV in 1 / 2, in the order board.cpp initialises. */
    void SetAdcChannels(uint8_t cv_in_1_channel, uint8_t cv_in_2_channel);

    /** Volts at the jack, using the nominal transfer plus calibration. */
    float ReadIn(size_t index) const;

    /** Drive a jack voltage. The DAC code is offset-compensated and clamped to
     ** the codec's own 0..VDDA range, so an out-of-range request saturates
     ** instead of wrapping. */
    void WriteOut(size_t index, float volts);

    /** M2 writes the measured corrections here (and to the calibration file). */
    void SetInCalibration(size_t index, float gain, float offset);
    void SetOutCalibration(size_t index, float gain, float offset);
    bool CalibrationValid() const { return calibration_valid_; }
    void SetCalibrationValid() { calibration_valid_ = true; }

  private:
    struct Calibration
    {
        float gain   = 1.0f; // multiplies the calibrated quantity
        float offset = 0.0f; // added afterwards
    };

    daisy::AdcHandle* adc_ = nullptr;
    daisy::DacHandle* dac_ = nullptr;
    uint8_t           adc_channel_[kInCount];
    Calibration       in_cal_[kInCount];
    Calibration       out_cal_[kOutCount];
    bool              calibration_valid_ = false;
};
} // namespace froemix

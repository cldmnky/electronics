#pragma once

#include "effect.hpp"

namespace mnky::audio::fx {

/// Stereo-linked peak compressor.
///
/// Stable state indices (never reorder; append only with a version bump):
///   0 threshold_db  -60..0 dBFS
///   1 ratio          1..20 (1 = no compression)
///   2 attack_ms      0.1..100 ms
///   3 release_ms     5..1000 ms
///   4 makeup_db      0..24 dB
class Comp final : public Effect {
public:
    static constexpr const char* kId = "comp";
    static constexpr uint32_t kVersion = 1;

    enum ParamIndex : int {
        kThresholdDb = 0,
        kRatio = 1,
        kAttackMs = 2,
        kReleaseMs = 3,
        kMakeupDb = 4,
        kParamCount = 5,
    };

    Comp();

protected:
    void processBlock(float* const* ch, int nch, int n) override;
    void onSampleRateChanged() override;
    void onReset() override;

private:
    double mEnvelope = 0.0;
};

}  // namespace mnky::audio::fx

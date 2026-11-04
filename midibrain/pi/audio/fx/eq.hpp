#pragma once

#include "biquad.hpp"
#include "effect.hpp"

namespace mnky::audio::fx {

/// Four-band stereo EQ. Each instance is independent, so the same class serves
/// every track strip and the one main-bus EQ. Every band selects its own type;
/// the shared per-band controls are frequency, gain and Q.
///
/// State indices are stable (never reorder; append only with a version bump):
///   band N: type, freq_hz, gain_db, q for N = 0..3 (indices 0..15)
///   16:     active_band (0..3, UI selection; no audio effect)
///
/// Type is a stepped integer: 0 None, 1 LowCut, 2 LowShelf, 3 Parametric,
/// 4 HighShelf, 5 HighCut. Q is the biquad Q for cuts/parametric bands and the
/// shelf slope (clamped 0.1..1.0) for shelves; gain is ignored by cuts.
class Eq final : public Effect {
public:
    static constexpr const char* kId = "eq";
    static constexpr uint32_t kVersion = 2;
    static constexpr int kBandCount = 4;

    enum class BandType : int {
        None = 0,
        LowCut = 1,
        LowShelf = 2,
        Parametric = 3,
        HighShelf = 4,
        HighCut = 5,
    };

    enum BandField : int {
        kType = 0,
        kFreq = 1,
        kGain = 2,
        kQ = 3,
        kBandFieldCount = 4,
    };

    enum ParamIndex : int {
        kBand0Type = 0,
        kBand0Freq = 1,
        kBand0Gain = 2,
        kBand0Q = 3,
        kBand1Type = 4,
        kBand1Freq = 5,
        kBand1Gain = 6,
        kBand1Q = 7,
        kBand2Type = 8,
        kBand2Freq = 9,
        kBand2Gain = 10,
        kBand2Q = 11,
        kBand3Type = 12,
        kBand3Freq = 13,
        kBand3Gain = 14,
        kBand3Q = 15,
        kActiveBand = 16,
        kParamCount = 17,
    };

    static constexpr int bandParam(int band, BandField field) {
        return band * kBandFieldCount + static_cast<int>(field);
    }

    static const char* bandTypeName(BandType type);
    static const char* bandTypeName(int type);

    Eq();

    /// `band` is 0..3; an out-of-range band is ignored.
    bool setBandType(int band, BandType type);
    BandType bandType(int band) const;
    /// UI selection shared by the fullscreen grid; clamped to 0..3.
    bool setActiveBand(int band);
    int activeBand() const;

protected:
    void processBlock(float* const* ch, int nch, int n) override;
    void onSampleRateChanged() override;
    void onReset() override;

private:
    // The coefficient math and per-channel state live in biquad.hpp so the
    // engine's strip/main inserts run the identical formulas.
    Biquad mBands[kBandCount];
    BiquadState mState[2][kBandCount] = {};
    int mLastType[kBandCount] = {-1, -1, -1, -1};
};

}  // namespace mnky::audio::fx

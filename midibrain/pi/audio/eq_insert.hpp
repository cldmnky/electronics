#pragma once
// Four-band EQ insert for a mixer strip or the main bus. The band model is the
// session EQ contract (type 0 none, 1 low cut, 2 low shelf, 3 parametric,
// 4 high shelf, 5 high cut; the shared controls are gain, frequency and Q).
// Coefficients and state come from the shipped fx biquad (`fx/biquad.hpp`),
// shared with the FX-suite `fx::Eq` effect; this class only owns the four-band
// chain and the session clamps.
//
// A band with type None is bypassed. When every band is None `process()` is a
// no-op, so an unconfigured insert is bit-transparent and costs no samples.

#include <array>
#include <cmath>
#include <cstddef>

#include "fx/biquad.hpp"

namespace mnky::audio {

/// One EQ band as stored by the session (session::EqBand is the serialized
/// spelling of the same four fields). Engine setters clamp every field:
/// type 0..5, gain -24..+24 dB, frequency 20..20000 Hz, Q 0.1..24.
struct EqBand {
    int type = 0;
    float gainDb = 0.0f;
    float freqHz = 1000.0f;
    float q = 1.0f;
    friend bool operator==(const EqBand&, const EqBand&) = default;
};

class EqInsert {
public:
    static constexpr int kBandCount = 4;

    EqInsert();

    /// Control thread only; outside 8 kHz..192 kHz is ignored (the FX suite's
    /// accepted range). Active bands are rebuilt for the new rate.
    void setSampleRate(double sampleRate);
    double sampleRate() const { return mSampleRate; }

    /// Out-of-range `band` is ignored. A type change resets that band's filter
    /// state so a re-enabled band cannot ring with stale history.
    void setBand(int band, const EqBand& value);
    /// Out-of-range `band` reads as the session default band (None, 0 dB,
    /// 1000 Hz, Q 1.0).
    EqBand band(int band) const;

    /// True when at least one band is not None.
    bool active() const { return mActiveCount > 0; }

    /// In-place stereo band chain. The caller must provide both buffers.
    void process(float* left, float* right, int n);

    /// Clears every band's filter state (does not change the band settings).
    void reset();

private:
    void updateBand(int band);

    std::array<EqBand, kBandCount> mBands{};
    std::array<fx::Biquad, kBandCount> mCoefficients{};
    std::array<std::array<fx::BiquadState, 2>, kBandCount> mState{};
    int mActiveCount = 0;
    double mSampleRate = 48000.0;
};

inline EqInsert::EqInsert() {
    for (EqBand& band : mBands) band = EqBand{};
    for (int band = 0; band < kBandCount; ++band) updateBand(band);
}

inline EqBand EqInsert::band(int band) const {
    if (band < 0 || band >= kBandCount) return EqBand{};
    return mBands[static_cast<std::size_t>(band)];
}

inline void EqInsert::reset() {
    for (auto& channel : mState) {
        for (fx::BiquadState& state : channel) state = fx::BiquadState{};
    }
}

inline void EqInsert::setSampleRate(double sampleRate) {
    if (!(sampleRate >= 8000.0) || sampleRate > 192000.0) return;
    if (sampleRate == mSampleRate) return;
    mSampleRate = sampleRate;
    reset();
    for (int band = 0; band < kBandCount; ++band) updateBand(band);
}

inline void EqInsert::updateBand(int band) {
    const EqBand& value = mBands[static_cast<std::size_t>(band)];
    mCoefficients[static_cast<std::size_t>(band)] =
        value.type == 0 ? fx::Biquad{}
                        : fx::makeBand(value.type, mSampleRate, value.freqHz,
                                       value.gainDb, value.q);
}

inline void EqInsert::setBand(int band, const EqBand& value) {
    if (band < 0 || band >= kBandCount) return;
    const std::size_t index = static_cast<std::size_t>(band);
    EqBand next;
    next.type = value.type < 0 ? 0 : (value.type > 5 ? 5 : value.type);
    next.gainDb = std::isfinite(value.gainDb)
                      ? std::fmin(std::fmax(value.gainDb, -24.0f), 24.0f)
                      : 0.0f;
    next.freqHz = std::isfinite(value.freqHz)
                      ? std::fmin(std::fmax(value.freqHz, 20.0f), 20000.0f)
                      : 1000.0f;
    next.q = std::isfinite(value.q)
                 ? std::fmin(std::fmax(value.q, 0.1f), 24.0f)
                 : 1.0f;
    const bool typeChanged = next.type != mBands[index].type;
    mBands[index] = next;
    if (typeChanged) {
        for (fx::BiquadState& state : mState[index]) state = fx::BiquadState{};
    }
    mActiveCount = 0;
    for (const EqBand& candidate : mBands) {
        if (candidate.type != 0) ++mActiveCount;
    }
    updateBand(band);
}

inline void EqInsert::process(float* left, float* right, int n) {
    if (mActiveCount == 0 || n <= 0 || left == nullptr || right == nullptr) return;

    int active[kBandCount] = {};
    int activeCount = 0;
    for (int band = 0; band < kBandCount; ++band) {
        if (mBands[static_cast<std::size_t>(band)].type != 0) {
            active[activeCount++] = band;
        }
    }

    float* buffers[2] = {left, right};
    for (int channel = 0; channel < 2; ++channel) {
        float* buffer = buffers[channel];
        for (int i = 0; i < n; ++i) {
            double x = static_cast<double>(buffer[i]);
            for (int k = 0; k < activeCount; ++k) {
                const int band = active[k];
                x = fx::run(mCoefficients[static_cast<std::size_t>(band)],
                            mState[static_cast<std::size_t>(band)]
                                  [static_cast<std::size_t>(channel)],
                            x);
            }
            buffer[i] = fx::flushDenormal(static_cast<float>(x));
        }
        for (int band = 0; band < kBandCount; ++band) {
            fx::flushState(mState[static_cast<std::size_t>(band)]
                                 [static_cast<std::size_t>(channel)]);
        }
    }
}

}  // namespace mnky::audio

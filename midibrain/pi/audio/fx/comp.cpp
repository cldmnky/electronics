#include "comp.hpp"

#include <cmath>

namespace mnky::audio::fx {
namespace {

constexpr double kSilenceDb = -120.0;

double timeCoefficient(double milliseconds, double sampleRate) {
    const double samples = milliseconds * 0.001 * sampleRate;
    if (!(samples > 1.0)) return 0.0;
    return std::exp(-1.0 / samples);
}

}  // namespace

Comp::Comp()
    : Effect(kId, "Compressor", kVersion,
             {
                 Param{"threshold_db", "Threshold", -60.0, 0.0, -12.0, 0.0},
                 Param{"ratio", "Ratio", 1.0, 20.0, 4.0, 0.0},
                 Param{"attack_ms", "Attack", 0.1, 100.0, 10.0, 0.0},
                 Param{"release_ms", "Release", 5.0, 1000.0, 100.0, 0.0},
                 Param{"makeup_db", "Makeup", 0.0, 24.0, 0.0, 0.0},
             }) {}

void Comp::processBlock(float* const* ch, int nch, int n) {
    const int channels = nch < 2 ? 1 : 2;
    const double thresholdDb = nextParam(kThresholdDb, n);
    const double ratio = nextParam(kRatio, n);
    const double attack = timeCoefficient(nextParam(kAttackMs, n), sampleRate());
    const double release = timeCoefficient(nextParam(kReleaseMs, n), sampleRate());
    const double makeupDb = nextParam(kMakeupDb, n);
    const double slope = 1.0 - 1.0 / (ratio > 1.0 ? ratio : 1.0);

    for (int i = 0; i < n; ++i) {
        double peak = 0.0;
        for (int c = 0; c < channels; ++c) {
            if (ch[c] == nullptr) continue;
            const double magnitude = std::fabs(static_cast<double>(ch[c][i]));
            if (magnitude > peak) peak = magnitude;
        }

        // Peak detector: fast attack, slow release. Both coefficients are
        // one-pole so the envelope cannot overshoot or ring.
        const double coefficient = peak > mEnvelope ? attack : release;
        mEnvelope = coefficient * mEnvelope + (1.0 - coefficient) * peak;
        mEnvelope = flushDenormal(mEnvelope);

        const double envelopeDb = 20.0 * std::log10(mEnvelope > 1.0e-6 ? mEnvelope : 1.0e-6);
        double gainDb = makeupDb;
        if (envelopeDb > thresholdDb) gainDb -= (envelopeDb - thresholdDb) * slope;
        if (gainDb < kSilenceDb) gainDb = kSilenceDb;
        const double gain = std::pow(10.0, gainDb / 20.0);

        for (int c = 0; c < channels; ++c) {
            if (ch[c] == nullptr) continue;
            ch[c][i] = flushDenormal(static_cast<float>(static_cast<double>(ch[c][i]) * gain));
        }
    }
}

void Comp::onSampleRateChanged() {
    onReset();
}

void Comp::onReset() {
    mEnvelope = 0.0;
}

}  // namespace mnky::audio::fx

#include "click.hpp"

#include <cmath>

namespace mnky::audio {
namespace {

constexpr float kTwoPi = 6.28318530717958647692f;
// Five time constants inside the 30 ms window leave the tail at ~0.7 % when the
// hard cut lands, so the stop is inaudible.
constexpr float kDecayTimeConstants = 5.0f;
// Below this the envelope cannot contribute audible signal; snapping to zero
// keeps the decay out of the denormal range on CPUs without flush-to-zero.
constexpr float kDenormalFloor = 1e-12f;

double sanitizeSampleRate(double sampleRate) {
    if (!std::isfinite(sampleRate) || sampleRate <= 0.0) return 48000.0;
    return sampleRate;
}

}  // namespace

ClickSource::ClickSource(double sampleRate) : mSampleRate(sanitizeSampleRate(sampleRate)) {
    mLengthSamples = static_cast<int>(mSampleRate * (kLengthMs / 1000.0) + 0.5);
    if (mLengthSamples < 1) mLengthSamples = 1;
    const double tauSeconds = (kLengthMs / 1000.0) / kDecayTimeConstants;
    mEnvelopeDecay = static_cast<float>(std::exp(-1.0 / (tauSeconds * mSampleRate)));
}

void ClickSource::setEnabled(bool enabled) {
    if (!enabled) silence();
    mEnabled = enabled;
}

void ClickSource::setLevel(float level) {
    if (!(level > 0.0f)) level = 0.0f;
    if (level > 1.0f) level = 1.0f;
    mLevel = level;
}

void ClickSource::trigger(bool accent) {
    const double hz = accent ? kAccentHz : kPipHz;
    mPhaseInc = static_cast<float>(2.0 * 3.14159265358979323846 * hz / mSampleRate);
    mPhase = 0.0f;
    mEnvelope = 1.0f;
    mRemaining = mLengthSamples;
    mActive = true;
}

void ClickSource::silence() {
    mActive = false;
    mPhase = 0.0f;
    mEnvelope = 0.0f;
    mRemaining = 0;
    mHaveBeat = false;
}

void ClickSource::render(float* monoOut, int n, uint32_t tick, uint32_t ticksPerBeat, bool rolling) {
    if (monoOut == nullptr || n <= 0) return;
    if (!mEnabled || !rolling || ticksPerBeat == 0) {
        silence();
        for (int i = 0; i < n; ++i) monoOut[i] = 0.0f;
        return;
    }

    const uint32_t beat = tick / ticksPerBeat;
    const bool onBoundary = tick % ticksPerBeat == 0;
    const bool newBeat = mHaveBeat ? beat != mLastBeat : onBoundary;
    if (newBeat) trigger(beat % kBeatsPerBar == 0);
    mLastBeat = beat;
    mHaveBeat = true;

    const float level = mLevel;
    for (int i = 0; i < n; ++i) {
        if (!mActive) {
            monoOut[i] = 0.0f;
            continue;
        }
        float sample = static_cast<float>(std::sin(mPhase)) * mEnvelope * level;
        // A tiny level or a decaying tail must not put denormals on the bus.
        if (std::fabs(sample) < kDenormalFloor) sample = 0.0f;
        monoOut[i] = sample;
        mPhase += mPhaseInc;
        if (mPhase >= kTwoPi) mPhase -= kTwoPi * std::floor(mPhase / kTwoPi);
        mEnvelope *= mEnvelopeDecay;
        if (--mRemaining <= 0 || mEnvelope < kDenormalFloor) {
            mActive = false;
            mEnvelope = 0.0f;
            mRemaining = 0;
        }
    }
}

}  // namespace mnky::audio

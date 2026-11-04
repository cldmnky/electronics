#pragma once

#include <cstdint>

namespace mnky::audio {

/// Metronome pip generator: a sine pip with a 30 ms exponential decay, 2 kHz
/// accent on the first beat of each 4/4 bar and 1 kHz on the other beats.
/// JUCE-free, allocation-free and safe on the audio thread.
///
/// The caller owns tick timing: `render()` receives the transport tick at the
/// start of the block and the ticks per beat. A pip starts on the first block
/// that enters a new beat; the first call must already be exactly on a beat
/// boundary, and a repeated tick never retriggers. Beat placement therefore
/// carries one block of quantisation (2.7 ms at 48 kHz / 128 frames).
///
/// `render()` overwrites `monoOut`; the caller mixes the result into a bus.
/// Output is zeros unless enabled and rolling, so stopping the transport cuts
/// the pip instead of leaving a tail.
class ClickSource {
public:
    /// 4/4, matching the app's bar:beat:tick readout.
    static constexpr uint32_t kBeatsPerBar = 4;
    static constexpr float kAccentHz = 2000.0f;
    static constexpr float kPipHz = 1000.0f;
    static constexpr float kLengthMs = 30.0f;

    /// The sample rate sizes the oscillator and envelope; the project rate is
    /// 48 kHz. Non-positive or non-finite rates fall back to 48000.
    explicit ClickSource(double sampleRate = 48000.0);

    void setEnabled(bool enabled);
    /// Clamped to 0..1; negative and NaN levels become 0.
    void setLevel(float level);
    /// Writes `n` click samples into `monoOut` (overwritten). A null/empty
    /// output, `ticksPerBeat == 0`, disabled or stopped source renders zeros.
    void render(float* monoOut, int n, uint32_t tick, uint32_t ticksPerBeat, bool rolling);

private:
    void trigger(bool accent);
    void silence();

    double mSampleRate = 48000.0;
    bool mEnabled = false;
    float mLevel = 0.5f;
    bool mActive = false;
    float mPhase = 0.0f;
    float mPhaseInc = 0.0f;
    float mEnvelope = 0.0f;
    float mEnvelopeDecay = 0.0f;
    int mRemaining = 0;
    int mLengthSamples = 1;
    uint32_t mLastBeat = 0;
    bool mHaveBeat = false;
};

}  // namespace mnky::audio

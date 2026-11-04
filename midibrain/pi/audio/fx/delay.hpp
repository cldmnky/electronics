#pragma once

#include <vector>

#include "effect.hpp"

namespace mnky::audio::fx {

/// Delay time in milliseconds for `beats` beats at `bpm` (0.5 = eighth note).
/// Returns 0 for a non-positive or non-finite tempo/beat count.
double tempoSyncedMs(double bpm, double beats);

/// Stereo delay with fractional reads, tempo-locked Beat mode, ping-pong
/// routing and a bandpass filter in the feedback path.
///
/// Stable state indices (never reorder; append only with a version bump):
///   0 time_ms       1..2000 free-run time
///   1 feedback      0..0.95
///   2 mix           0..1 (0 = dry, 1 = wet only)
///   3 beat_mode     0/1; 1 uses bpm + division instead of time_ms
///   4 division      stepped 0..7 (1/32 .. 4/1 note)
///   5 ping_pong     0/1; cross-feeds L/R feedback and must be stereo to hear
///   6 bp_cutoff_hz  feedback-path bandpass centre, 20..20000
///   7 bp_width_oct  feedback-path bandpass width in octaves, 0.1..4
class Delay final : public Effect {
public:
    static constexpr const char* kId = "delay";
    static constexpr uint32_t kVersion = 2;
    static constexpr double kMaxTimeMs = 2000.0;
    static constexpr int kDivisionCount = 8;

    enum ParamIndex : int {
        kTimeMs = 0,
        kFeedback = 1,
        kMix = 2,
        kBeatMode = 3,
        kDivision = 4,
        kPingPong = 5,
        kBpCutoffHz = 6,
        kBpWidthOct = 7,
        kParamCount = 8,
    };

    enum Division : int {
        kDiv1_32 = 0,
        kDiv1_16 = 1,
        kDiv1_8 = 2,
        kDiv1_4 = 3,
        kDiv1_2 = 4,
        kDiv1_1 = 5,
        kDiv2_1 = 6,
        kDiv4_1 = 7,
        kDivCount = 8,
    };

    /// Beats for a division index (clamped to 0..7); quarter note = 1 beat.
    static double divisionBeats(int division);
    static const char* divisionName(int division);
    /// `divisionMs(bpm, division)`, or 0 when the tempo is unusable.
    static double divisionMs(double bpm, int division);

    Delay();

    /// Manual helper: sets free-run time_ms from `beats` and turns Beat mode
    /// off. Returns false when the tempo/beat pair is unusable.
    bool setTempoSync(double bpm, double beats);
    /// External tempo sampled once per block while Beat mode is on.
    void setBpm(double bpm);
    double bpm() const { return mBpm; }

protected:
    void processBlock(float* const* ch, int nch, int n) override;
    void onSampleRateChanged() override;
    void onReset() override;

private:
    struct Biquad {
        double b0 = 1.0;
        double b1 = 0.0;
        double b2 = 0.0;
        double a1 = 0.0;
        double a2 = 0.0;
    };

    struct BiquadState {
        double x1 = 0.0;
        double x2 = 0.0;
        double y1 = 0.0;
        double y2 = 0.0;
    };

    static Biquad bandpass(double sampleRate, double cutoffHz, double widthOctaves);
    static double runFilter(const Biquad& biquad, BiquadState& state, double x);
    static void flushFilter(BiquadState& state);

    std::vector<float> mLeft;
    std::vector<float> mRight;
    int mSize = 0;
    int mWrite = 0;
    Biquad mBandpass;
    BiquadState mBpState[2];
    double mBpm = 120.0;
};

}  // namespace mnky::audio::fx

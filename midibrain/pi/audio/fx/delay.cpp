#include "delay.hpp"

#include <cmath>

namespace mnky::audio::fx {
namespace {

constexpr double kPi = 3.14159265358979323846;

double clamp(double value, double min, double max) {
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

}  // namespace

double tempoSyncedMs(double bpm, double beats) {
    if (!std::isfinite(bpm) || !std::isfinite(beats) || bpm <= 0.0 || beats <= 0.0) return 0.0;
    const double milliseconds = beats * 60000.0 / bpm;
    return std::isfinite(milliseconds) ? milliseconds : 0.0;
}

double Delay::divisionBeats(int division) {
    static const double kBeats[kDivisionCount] = {0.125, 0.25, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0};
    if (division < 0) division = 0;
    if (division >= kDivisionCount) division = kDivisionCount - 1;
    return kBeats[division];
}

const char* Delay::divisionName(int division) {
    static const char* kNames[kDivisionCount] = {"1/32", "1/16", "1/8",  "1/4",
                                                 "1/2",  "1/1",  "2/1",  "4/1"};
    if (division < 0) division = 0;
    if (division >= kDivisionCount) division = kDivisionCount - 1;
    return kNames[division];
}

double Delay::divisionMs(double bpm, int division) {
    return tempoSyncedMs(bpm, divisionBeats(division));
}

Delay::Delay()
    : Effect(kId, "Stereo Delay", kVersion,
             {
                 Param{"time_ms", "Time", 1.0, kMaxTimeMs, 250.0, 0.0, false},
                 Param{"feedback", "Feedback", 0.0, 0.95, 0.35, 0.0, false},
                 Param{"mix", "Mix", 0.0, 1.0, 0.3, 0.0, false},
                 Param{"beat_mode", "Beat Mode", 0.0, 1.0, 0.0, 0.0, true},
                 Param{"division", "Division", 0.0, static_cast<double>(kDivCount - 1),
                       static_cast<double>(kDiv1_4), 0.0, true},
                 Param{"ping_pong", "Ping-Pong", 0.0, 1.0, 0.0, 0.0, true},
                 Param{"bp_cutoff_hz", "Feedback BP", 20.0, 20000.0, 1000.0, 0.0, false},
                 Param{"bp_width_oct", "BP Width", 0.1, 4.0, 1.0, 0.0, false},
             }) {
    // Allocate the delay line at the default rate: without this the effect
    // is dead silent until someone calls setSampleRate() with a *different*
    // rate (Reverb already does this; the omission muted every fresh Delay).
    onSampleRateChanged();
}

bool Delay::setTempoSync(double bpm, double beats) {
    const double milliseconds = tempoSyncedMs(bpm, beats);
    if (!(milliseconds > 0.0)) return false;
    if (!setParam(kBeatMode, 0.0)) return false;
    return setParam(kTimeMs, milliseconds);
}

void Delay::setBpm(double bpm) {
    if (std::isfinite(bpm) && bpm > 0.0) mBpm = bpm;
}

Delay::Biquad Delay::bandpass(double sampleRate, double cutoffHz, double widthOctaves) {
    const double limit = sampleRate * 0.45;
    const double cutoff = clamp(cutoffHz, 20.0, limit);
    const double width = clamp(widthOctaves, 0.1, 4.0);
    const double w0 = 2.0 * kPi * cutoff / sampleRate;
    const double cosw = std::cos(w0);
    const double sinw = std::sin(w0);
    const double alpha = sinw * std::sinh(std::log(2.0) * 0.5 * width);
    const double a0 = 1.0 + alpha;
    const double inverse = 1.0 / a0;
    Biquad result;
    result.b0 = alpha * inverse;
    result.b1 = 0.0;
    result.b2 = -alpha * inverse;
    result.a1 = -2.0 * cosw * inverse;
    result.a2 = (1.0 - alpha) * inverse;
    return result;
}

double Delay::runFilter(const Biquad& biquad, BiquadState& state, double x) {
    const double y = biquad.b0 * x + biquad.b1 * state.x1 + biquad.b2 * state.x2 - biquad.a1 * state.y1 -
                     biquad.a2 * state.y2;
    state.x2 = state.x1;
    state.x1 = x;
    state.y2 = state.y1;
    state.y1 = y;
    return y;
}

void Delay::flushFilter(BiquadState& state) {
    state.x1 = flushDenormal(state.x1);
    state.x2 = flushDenormal(state.x2);
    state.y1 = flushDenormal(state.y1);
    state.y2 = flushDenormal(state.y2);
}

void Delay::processBlock(float* const* ch, int nch, int n) {
    const int channels = nch < 2 ? 1 : 2;
    const double timeMs = nextParam(kTimeMs, n);
    const double feedback = nextParam(kFeedback, n);
    const double mix = nextParam(kMix, n);
    const double cutoff = nextParam(kBpCutoffHz, n);
    const double width = nextParam(kBpWidthOct, n);
    const int beatMode = static_cast<int>(std::lround(param(kBeatMode))) != 0 ? 1 : 0;
    const int division = static_cast<int>(std::lround(param(kDivision)));
    const int pingPong = static_cast<int>(std::lround(param(kPingPong))) != 0 ? 1 : 0;
    if (mSize <= 2) return;

    double delayMs = timeMs;
    if (beatMode != 0) {
        const double synced = divisionMs(mBpm, division);
        if (synced > 0.0) delayMs = synced;
    }
    double delaySamples = delayMs * 0.001 * sampleRate();
    const double maximum = static_cast<double>(mSize - 2);
    if (!(delaySamples >= 1.0)) delaySamples = 1.0;
    if (delaySamples > maximum) delaySamples = maximum;
    const int whole = static_cast<int>(delaySamples);
    const double fraction = delaySamples - static_cast<double>(whole);
    mBandpass = bandpass(sampleRate(), cutoff, width);

    for (int i = 0; i < n; ++i) {
        const int write = mWrite;
        int first = write - whole;
        if (first < 0) first += mSize;
        int second = first - 1;
        if (second < 0) second += mSize;

        const double echoLeft = static_cast<double>(mLeft[static_cast<size_t>(first)]) +
                                (static_cast<double>(mLeft[static_cast<size_t>(second)]) -
                                 static_cast<double>(mLeft[static_cast<size_t>(first)])) *
                                    fraction;
        double echoRight = 0.0;
        if (channels == 2) {
            echoRight = static_cast<double>(mRight[static_cast<size_t>(first)]) +
                        (static_cast<double>(mRight[static_cast<size_t>(second)]) -
                         static_cast<double>(mRight[static_cast<size_t>(first)])) *
                            fraction;
        }

        const double inputLeft = ch[0] != nullptr ? static_cast<double>(ch[0][i]) : 0.0;
        const double inputRight = channels == 2 && ch[1] != nullptr ? static_cast<double>(ch[1][i]) : 0.0;
        const double feedbackLeft = runFilter(mBandpass, mBpState[0], echoLeft);
        const double feedbackRight = channels == 2 ? runFilter(mBandpass, mBpState[1], echoRight) : 0.0;

        if (channels == 2 && pingPong != 0) {
            const double mono = 0.5 * (inputLeft + inputRight);
            mLeft[static_cast<size_t>(write)] = flushDenormal(static_cast<float>(mono + feedbackRight * feedback));
            mRight[static_cast<size_t>(write)] = flushDenormal(static_cast<float>(feedbackLeft * feedback));
        } else {
            mLeft[static_cast<size_t>(write)] =
                flushDenormal(static_cast<float>(inputLeft + feedbackLeft * feedback));
            if (channels == 2) {
                mRight[static_cast<size_t>(write)] =
                    flushDenormal(static_cast<float>(inputRight + feedbackRight * feedback));
            }
        }

        if (ch[0] != nullptr) {
            ch[0][i] = flushDenormal(static_cast<float>(inputLeft * (1.0 - mix) + echoLeft * mix));
        }
        if (channels == 2 && ch[1] != nullptr) {
            ch[1][i] = flushDenormal(static_cast<float>(inputRight * (1.0 - mix) + echoRight * mix));
        }

        ++mWrite;
        if (mWrite >= mSize) mWrite = 0;
    }

    flushFilter(mBpState[0]);
    flushFilter(mBpState[1]);
}

void Delay::onSampleRateChanged() {
    mSize = static_cast<int>(std::ceil(kMaxTimeMs * 0.001 * sampleRate())) + 4;
    if (mSize < 8) mSize = 8;
    mLeft.assign(static_cast<size_t>(mSize), 0.0f);
    mRight.assign(static_cast<size_t>(mSize), 0.0f);
    onReset();
}

void Delay::onReset() {
    for (float& value : mLeft) value = 0.0f;
    for (float& value : mRight) value = 0.0f;
    mWrite = 0;
    mBpState[0] = BiquadState{};
    mBpState[1] = BiquadState{};
}

}  // namespace mnky::audio::fx

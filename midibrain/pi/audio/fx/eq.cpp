#include "eq.hpp"

#include <cmath>
#include <string>
#include <vector>

namespace mnky::audio::fx {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kTypeCount = 5.0;

std::vector<Param> makeParams() {
    const int defaultTypes[Eq::kBandCount] = {2, 3, 3, 4};  // LShelf, 2x Param, HShelf
    const double defaultFreqs[Eq::kBandCount] = {120.0, 500.0, 2000.0, 8000.0};
    std::vector<Param> params;
    params.reserve(static_cast<size_t>(Eq::kParamCount));
    for (int band = 0; band < Eq::kBandCount; ++band) {
        const std::string prefix = "band" + std::to_string(band) + "_";
        const std::string label = "Band " + std::to_string(band + 1) + " ";
        params.push_back(Param{prefix + "type", label + "Type", 0.0, kTypeCount,
                               static_cast<double>(defaultTypes[band]), 0.0, true});
        params.push_back(Param{prefix + "freq", label + "Freq", 20.0, 20000.0, defaultFreqs[band], 0.0, false});
        params.push_back(Param{prefix + "gain", label + "Gain", -24.0, 24.0, 0.0, 0.0, false});
        params.push_back(Param{prefix + "q", label + "Q", 0.1, 10.0, 0.707, 0.0, false});
    }
    params.push_back(Param{"active_band", "Active Band", 0.0, 3.0, 0.0, 0.0, true});
    return params;
}

double clamp(double value, double min, double max) {
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

int bandTypeIndex(int type) {
    if (type < 0) return 0;
    return type > 5 ? 5 : type;
}

}  // namespace

const char* Eq::bandTypeName(BandType type) {
    return bandTypeName(static_cast<int>(type));
}

const char* Eq::bandTypeName(int type) {
    switch (bandTypeIndex(type)) {
        case 0: return "None";
        case 1: return "Low Cut";
        case 2: return "Low Shelf";
        case 3: return "Parametric";
        case 4: return "High Shelf";
        default: return "High Cut";
    }
}

Eq::Eq() : Effect(kId, "Parametric EQ", kVersion, makeParams()) {}

bool Eq::setBandType(int band, BandType type) {
    if (band < 0 || band >= kBandCount) return false;
    return setParam(bandParam(band, kType), static_cast<double>(type));
}

Eq::BandType Eq::bandType(int band) const {
    if (band < 0 || band >= kBandCount) return BandType::None;
    return static_cast<BandType>(bandTypeIndex(static_cast<int>(std::lround(param(bandParam(band, kType))))));
}

bool Eq::setActiveBand(int band) {
    if (band < 0 || band >= kBandCount) return false;
    return setParam(kActiveBand, static_cast<double>(band));
}

int Eq::activeBand() const {
    const int band = static_cast<int>(std::lround(param(kActiveBand)));
    return band < 0 ? 0 : (band >= kBandCount ? kBandCount - 1 : band);
}

Eq::Biquad Eq::normalized(double b0, double b1, double b2, double a0, double a1, double a2) {
    const double inverse = 1.0 / a0;
    Biquad result;
    result.b0 = b0 * inverse;
    result.b1 = b1 * inverse;
    result.b2 = b2 * inverse;
    result.a1 = a1 * inverse;
    result.a2 = a2 * inverse;
    return result;
}

Eq::Biquad Eq::lowCut(double sampleRate, double freq, double q) {
    const double w0 = 2.0 * kPi * freq / sampleRate;
    const double cosw = std::cos(w0);
    const double sinw = std::sin(w0);
    const double alpha = sinw / (2.0 * (q < 0.1 ? 0.1 : q));
    const double b0 = (1.0 + cosw) * 0.5;
    const double b1 = -(1.0 + cosw);
    const double b2 = (1.0 + cosw) * 0.5;
    const double a0 = 1.0 + alpha;
    const double a1 = -2.0 * cosw;
    const double a2 = 1.0 - alpha;
    return normalized(b0, b1, b2, a0, a1, a2);
}

Eq::Biquad Eq::highCut(double sampleRate, double freq, double q) {
    const double w0 = 2.0 * kPi * freq / sampleRate;
    const double cosw = std::cos(w0);
    const double sinw = std::sin(w0);
    const double alpha = sinw / (2.0 * (q < 0.1 ? 0.1 : q));
    const double b0 = (1.0 - cosw) * 0.5;
    const double b1 = 1.0 - cosw;
    const double b2 = (1.0 - cosw) * 0.5;
    const double a0 = 1.0 + alpha;
    const double a1 = -2.0 * cosw;
    const double a2 = 1.0 - alpha;
    return normalized(b0, b1, b2, a0, a1, a2);
}

Eq::Biquad Eq::lowShelf(double sampleRate, double freq, double gainDb, double q) {
    const double a = std::pow(10.0, gainDb / 40.0);
    const double w0 = 2.0 * kPi * freq / sampleRate;
    const double cosw = std::cos(w0);
    const double sinw = std::sin(w0);
    const double slope = clamp(q, 0.1, 1.0);
    const double alpha = sinw * 0.5 * std::sqrt((a + 1.0 / a) * (1.0 / slope - 1.0) + 2.0);
    const double beta = 2.0 * std::sqrt(a) * alpha;
    const double b0 = a * ((a + 1.0) - (a - 1.0) * cosw + beta);
    const double b1 = 2.0 * a * ((a - 1.0) - (a + 1.0) * cosw);
    const double b2 = a * ((a + 1.0) - (a - 1.0) * cosw - beta);
    const double a0 = (a + 1.0) + (a - 1.0) * cosw + beta;
    const double a1 = -2.0 * ((a - 1.0) + (a + 1.0) * cosw);
    const double a2 = (a + 1.0) + (a - 1.0) * cosw - beta;
    return normalized(b0, b1, b2, a0, a1, a2);
}

Eq::Biquad Eq::highShelf(double sampleRate, double freq, double gainDb, double q) {
    const double a = std::pow(10.0, gainDb / 40.0);
    const double w0 = 2.0 * kPi * freq / sampleRate;
    const double cosw = std::cos(w0);
    const double sinw = std::sin(w0);
    const double slope = clamp(q, 0.1, 1.0);
    const double alpha = sinw * 0.5 * std::sqrt((a + 1.0 / a) * (1.0 / slope - 1.0) + 2.0);
    const double beta = 2.0 * std::sqrt(a) * alpha;
    const double b0 = a * ((a + 1.0) + (a - 1.0) * cosw + beta);
    const double b1 = -2.0 * a * ((a - 1.0) + (a + 1.0) * cosw);
    const double b2 = a * ((a + 1.0) + (a - 1.0) * cosw - beta);
    const double a0 = (a + 1.0) - (a - 1.0) * cosw + beta;
    const double a1 = 2.0 * ((a - 1.0) - (a + 1.0) * cosw);
    const double a2 = (a + 1.0) - (a - 1.0) * cosw - beta;
    return normalized(b0, b1, b2, a0, a1, a2);
}

Eq::Biquad Eq::peaking(double sampleRate, double freq, double q, double gainDb) {
    const double a = std::pow(10.0, gainDb / 40.0);
    const double w0 = 2.0 * kPi * freq / sampleRate;
    const double cosw = std::cos(w0);
    const double sinw = std::sin(w0);
    const double alpha = sinw / (2.0 * (q < 0.1 ? 0.1 : q));
    const double b0 = 1.0 + alpha * a;
    const double b1 = -2.0 * cosw;
    const double b2 = 1.0 - alpha * a;
    const double a0 = 1.0 + alpha / a;
    const double a1 = -2.0 * cosw;
    const double a2 = 1.0 - alpha / a;
    return normalized(b0, b1, b2, a0, a1, a2);
}

Eq::Biquad Eq::makeBand(int type, double sampleRate, double freq, double gainDb, double q) {
    const double limit = sampleRate * 0.45;
    const double f = freq < 10.0 ? 10.0 : (freq > limit ? limit : freq);
    switch (bandTypeIndex(type)) {
        case 1: return lowCut(sampleRate, f, q);
        case 2: return lowShelf(sampleRate, f, gainDb, q);
        case 3: return peaking(sampleRate, f, q, gainDb);
        case 4: return highShelf(sampleRate, f, gainDb, q);
        case 5: return highCut(sampleRate, f, q);
        default: return Biquad{};
    }
}

double Eq::run(const Biquad& biquad, BiquadState& state, double x) {
    const double y = biquad.b0 * x + biquad.b1 * state.x1 + biquad.b2 * state.x2 - biquad.a1 * state.y1 -
                     biquad.a2 * state.y2;
    state.x2 = state.x1;
    state.x1 = x;
    state.y2 = state.y1;
    state.y1 = y;
    return y;
}

void Eq::flushState(BiquadState& state) {
    state.x1 = flushDenormal(state.x1);
    state.x2 = flushDenormal(state.x2);
    state.y1 = flushDenormal(state.y1);
    state.y2 = flushDenormal(state.y2);
}

void Eq::processBlock(float* const* ch, int nch, int n) {
    int active[kBandCount] = {};
    int activeCount = 0;
    for (int band = 0; band < kBandCount; ++band) {
        const int type = bandTypeIndex(
            static_cast<int>(std::lround(param(bandParam(band, kType)))));
        if (type != mLastType[band]) {
            mState[0][band] = BiquadState{};
            mState[1][band] = BiquadState{};
            mLastType[band] = type;
        }
        mBands[band] = makeBand(type, sampleRate(), nextParam(bandParam(band, kFreq), n),
                                nextParam(bandParam(band, kGain), n), nextParam(bandParam(band, kQ), n));
        if (type != 0) active[activeCount++] = band;
    }

    const int channels = nch < 2 ? 1 : 2;
    for (int c = 0; c < channels; ++c) {
        float* buffer = ch[c];
        if (buffer == nullptr) continue;
        for (int i = 0; i < n; ++i) {
            double x = static_cast<double>(buffer[i]);
            for (int k = 0; k < activeCount; ++k) {
                const int band = active[k];
                x = run(mBands[band], mState[c][band], x);
            }
            buffer[i] = flushDenormal(static_cast<float>(x));
        }
        for (int band = 0; band < kBandCount; ++band) flushState(mState[c][band]);
    }
}

void Eq::onSampleRateChanged() {
    onReset();
}

void Eq::onReset() {
    for (auto& channel : mState) {
        for (auto& state : channel) state = BiquadState{};
    }
    for (int& type : mLastType) type = -1;
}

}  // namespace mnky::audio::fx

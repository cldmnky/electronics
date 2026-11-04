#include "eq.hpp"

#include <cmath>
#include <string>
#include <vector>

namespace mnky::audio::fx {
namespace {

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

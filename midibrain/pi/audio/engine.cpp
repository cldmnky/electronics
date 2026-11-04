#include "engine.hpp"

#include <cmath>

namespace mnky::audio {
namespace {

constexpr float kHalfPi = 1.57079632679489661923f;

// Constant-power pan: gainL^2 + gainR^2 == 1 for every position. The `!(p >= 0)`
// form maps NaN to hard left instead of propagating it.
void constantPowerPan(float pan, float& gainL, float& gainR) {
    float p = pan;
    if (!(p >= 0.0f)) p = 0.0f;
    if (p > 1.0f) p = 1.0f;
    const float angle = p * kHalfPi;
    gainL = std::cos(angle);
    gainR = std::sin(angle);
}

// Mute always wins; any active solo narrows the mix to soloed strips.
bool audible(const Channel& ch, bool anySolo) {
    if (ch.mute) return false;
    return !anySolo || ch.solo;
}

}  // namespace

void Engine::process(const float* const* in, int nch, int n, float* outL, float* outR) {
    if (outL == nullptr || outR == nullptr || n <= 0) return;
    const int frames = n < kBlockSize ? n : kBlockSize;
    int inputs = nch < 0 ? 0 : nch;
    if (inputs > kMaxInputs) inputs = kMaxInputs;

    float accL[kBlockSize] = {};
    float accR[kBlockSize] = {};
    for (int s = 0; s < kNumSends; ++s) {
        for (int i = 0; i < frames; ++i) {
            mSendL[s][i] = 0.0f;
            mSendR[s][i] = 0.0f;
        }
    }

    bool anySolo = false;
    for (int c = 0; c < kMaxInputs; ++c) anySolo = anySolo || mChannels[c].solo;

    for (int c = 0; c < kMaxInputs; ++c) {
        const Channel& ch = mChannels[c];
        const float* src = c < inputs ? in[c] : nullptr;
        if (src == nullptr) {
            for (int i = 0; i < frames; ++i) mTap[c][i] = 0.0f;
            continue;
        }
        if (!audible(ch, anySolo)) {
            for (int i = 0; i < frames; ++i) mTap[c][i] = src[i] * ch.gain;
            continue;
        }
        float gainL = 0.0f;
        float gainR = 0.0f;
        constantPowerPan(ch.pan, gainL, gainR);
        for (int i = 0; i < frames; ++i) {
            const float x = src[i] * ch.gain;
            mTap[c][i] = x;
            accL[i] += x * gainL;
            accR[i] += x * gainR;
            for (int s = 0; s < kNumSends; ++s) {
                const float send = x * ch.send[s];
                mSendL[s][i] += send * gainL;
                mSendR[s][i] += send * gainR;
            }
        }
    }

    for (int s = 0; s < kNumSends; ++s) {
        if (mFx[s] != nullptr) {
            float* buses[2] = {mSendL[s], mSendR[s]};
            mFx[s]->process(buses, 2, frames);
        }
        const float ret = mFxReturn[s];
        for (int i = 0; i < frames; ++i) {
            accL[i] += mSendL[s][i] * ret;
            accR[i] += mSendR[s][i] * ret;
        }
    }

    for (int i = 0; i < frames; ++i) {
        outL[i] = accL[i] * mMaster;
        outR[i] = accR[i] * mMaster;
    }
}

const float* Engine::tap(int ch) const {
    if (ch < 0 || ch >= kMaxInputs) return nullptr;
    return mTap[ch];
}

void Engine::setChannel(int index, Channel ch) {
    if (index < 0 || index >= kMaxInputs) return;
    mChannels[index] = ch;
}

void Engine::setMaster(float gain) {
    mMaster = gain > 0.0f ? gain : 0.0f;
}

void Engine::setFx(int send, IEffect* fx) {
    if (send < 0 || send >= kNumSends) return;
    mFx[send] = fx;
}

void Engine::setFxReturn(int send, float gain) {
    if (send < 0 || send >= kNumSends) return;
    mFxReturn[send] = gain;
}

}  // namespace mnky::audio

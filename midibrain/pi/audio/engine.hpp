#pragma once

#include <cstdint>

namespace mnky::audio {

/// UMC1820 + ADA8200 capture width: 16 analog inputs plus 2 S/PDIF channels.
inline constexpr int kMaxInputs = 18;
/// Auxiliary FX send buses fed from every channel.
inline constexpr int kNumSends = 2;
/// Fixed ALSA processing block.
inline constexpr int kBlockSize = 128;

/// Where Engine::tap() reads a strip. `PostGain` is the historical `in * gain`
/// tap; `PreGain` is the raw device input and ignores the strip's gain.
enum class TapPoint : uint8_t { PreGain, PostGain };

/// Mixer strip for one input. `gain` is a linear pre-pan trim, `pan` runs from
/// 0 (hard left) through 0.5 (centre) to 1 (hard right) with a constant-power
/// law, and `send[s]` is a post-fader level into FX bus `s`. `tapPoint` picks
/// the recorder/monitor tap; the default keeps the post-gain behavior.
struct Channel {
    float gain = 1.0f;
    float pan = 0.5f;
    bool mute = false;
    bool solo = false;
    bool arm = false;
    float send[kNumSends] = {0.0f, 0.0f};
    TapPoint tapPoint = TapPoint::PostGain;
};

/// Stereo in-place FX unit hosted on a send bus. `ch` holds `nch` block pointers
/// of `n` frames each; the engine passes a 2-channel bus.
struct IEffect {
    virtual ~IEffect() = default;
    virtual void process(float* const* ch, int nch, int n) = 0;
    virtual const char* name() const = 0;
    /// Control-thread sample-rate notice; default ignores (stateless effects).
    /// Time-based effects re-allocate here. Engine forwards on setSampleRate.
    virtual void setSampleRate(double) {}
};

/// Mixer core: gain/pan, mute/solo, post-fader sends, FX returns and master.
/// Everything runs in the caller's (audio) thread and never allocates.
class Engine {
public:
    /// `n` must be <= kBlockSize. `in` holds `nch` (<= kMaxInputs) channel
    /// pointers; missing or null channels are silent. `outL`/`outR` receive the
    /// master-bus result.
    void process(const float* const* in, int nch, int n, float* outL, float* outR);

    /// Mono tap of one input at its Channel::tapPoint, valid until the next
    /// process() call: the raw input for PreGain, `in * gain` for the default
    /// PostGain. nullptr when `ch` is outside 0..kMaxInputs-1. Mute, solo, pan
    /// and master do not affect the tap.
    const float* tap(int ch) const;

    /// Out-of-range indices are ignored, so a UI can push all 18 strips blindly.
    void setChannel(int index, Channel ch);
    /// Live strip state (session capture); out-of-range returns a default strip.
    Channel channel(int index) const {
        return index >= 0 && index < kMaxInputs ? mChannels[index] : Channel{};
    }
    /// Linear master gain; negative and NaN values are clamped to 0.
    void setMaster(float gain);
    /// Live master gain (session capture).
    float masterGain() const { return mMaster; }
    /// `send` is 0..kNumSends-1; nullptr detaches and passes the bus through.
    void setFx(int send, IEffect* fx);
    /// Control thread only. Forwards to attached FX (see IEffect); engine
    /// processing itself is rate-agnostic.
    void setSampleRate(double sampleRate);
    /// Linear return level applied to the FX bus after process().
    void setFxReturn(int send, float gain);

private:
    Channel mChannels[kMaxInputs];
    IEffect* mFx[kNumSends] = {nullptr, nullptr};
    float mFxReturn[kNumSends] = {1.0f, 1.0f};
    float mMaster = 1.0f;
    float mTap[kMaxInputs][kBlockSize] = {};
    float mSendL[kNumSends][kBlockSize] = {};
    float mSendR[kNumSends][kBlockSize] = {};
};

}  // namespace mnky::audio

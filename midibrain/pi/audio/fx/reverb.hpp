#pragma once

#include <vector>

#include "effect.hpp"

namespace mnky::audio::fx {

/// Stereo Schroeder/Moorer reverb: predelay, four damped parallel combs and
/// two series allpasses per channel, with a small right-channel length offset
/// for width. A size control scales the network; four named models select
/// decay/damping/size presets while leaving predelay and mix untouched.
///
/// Stable state indices (never reorder; append only with a version bump):
///   0 predelay_ms  0..200 ms
///   1 decay        0..0.98 comb feedback
///   2 damping      0..1 one-pole lowpass in the comb feedback
///   3 mix          0..1 (0 = dry, 1 = wet only)
///   4 model        stepped 0..3 (Core, Room, Hall, Plate)
///   5 size         0.25..2.0 network scale
class Reverb final : public Effect {
public:
    static constexpr const char* kId = "reverb";
    static constexpr uint32_t kVersion = 2;
    static constexpr int kModelCount = 4;

    enum Model : int {
        kModelCore = 0,
        kModelRoom = 1,
        kModelHall = 2,
        kModelPlate = 3,
    };

    enum ParamIndex : int {
        kPredelayMs = 0,
        kDecay = 1,
        kDamping = 2,
        kMix = 3,
        kModel = 4,
        kSize = 5,
        kParamCount = 6,
    };

    struct ModelPreset {
        const char* name;
        double decay;
        double damping;
        double size;
    };

    static const ModelPreset& modelPreset(int model);
    static const char* modelName(int model);

    Reverb();

    /// Applies the preset's decay/damping/size and records the model index.
    /// Returns false for an out-of-range model. predelay and mix are untouched.
    bool setModel(int model);
    int model() const;

protected:
    void processBlock(float* const* ch, int nch, int n) override;
    void onSampleRateChanged() override;
    void onReset() override;

private:
    static constexpr int kNumCombs = 4;
    static constexpr int kNumAllpasses = 2;
    static constexpr double kMaxSize = 2.0;

    struct Comb {
        std::vector<float> buffer;
        int length = 0;
        int index = 0;
        double filter = 0.0;
    };

    struct Allpass {
        std::vector<float> buffer;
        int length = 0;
        int index = 0;
    };

    struct Channel {
        std::vector<float> predelay;
        int predelayIndex = 0;
        Comb combs[kNumCombs];
        Allpass allpasses[kNumAllpasses];
    };

    static int scaledLength(int base, double sampleRate, double size, int extra);
    static double processComb(Comb& comb, double input, double damping, double feedback);
    static double processAllpass(Allpass& allpass, double input);
    void updateLengths(double size);

    Channel mChannels[2];
};

}  // namespace mnky::audio::fx

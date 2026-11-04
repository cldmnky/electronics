#include "reverb.hpp"

#include <cmath>

namespace mnky::audio::fx {
namespace {

// Prime-ish lengths at 44.1 kHz, deliberately chosen for this implementation
// rather than copied from a library. They are scaled with the sample rate and
// the size parameter.
constexpr int kCombLengths[4] = {1103, 1201, 1297, 1409};
constexpr int kAllpassLengths[2] = {547, 439};
constexpr int kRightOffset = 17;
constexpr double kInputGain = 0.25;
constexpr double kCombNormalize = 0.25;
constexpr double kAllpassGain = 0.5;
constexpr double kMaxPredelayMs = 200.0;
constexpr double kReferenceRate = 44100.0;

double clamp(double value, double min, double max) {
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

}  // namespace

const Reverb::ModelPreset& Reverb::modelPreset(int model) {
    static const ModelPreset kModels[kModelCount] = {
        {"Core", 0.70, 0.50, 1.00},
        {"Room", 0.55, 0.65, 0.60},
        {"Hall", 0.82, 0.35, 1.35},
        {"Plate", 0.90, 0.15, 0.80},
    };
    if (model < 0) model = 0;
    if (model >= kModelCount) model = kModelCount - 1;
    return kModels[model];
}

const char* Reverb::modelName(int model) {
    return modelPreset(model).name;
}

Reverb::Reverb()
    : Effect(kId, "Reverb", kVersion,
             {
                 Param{"predelay_ms", "Pre-delay", 0.0, kMaxPredelayMs, 20.0, 0.0, false},
                 Param{"decay", "Decay", 0.0, 0.98, 0.7, 0.0, false},
                 Param{"damping", "Damping", 0.0, 1.0, 0.5, 0.0, false},
                 Param{"mix", "Mix", 0.0, 1.0, 0.25, 0.0, false},
                 Param{"model", "Model", 0.0, static_cast<double>(kModelCount - 1), 0.0, 0.0, true},
                 Param{"size", "Size", 0.25, kMaxSize, 1.0, 0.0, false},
             }) {
    onSampleRateChanged();
}

bool Reverb::setModel(int model) {
    if (model < 0 || model >= kModelCount) return false;
    const ModelPreset& preset = modelPreset(model);
    bool ok = setParam(kModel, static_cast<double>(model));
    ok = setParam(kDecay, preset.decay) && ok;
    ok = setParam(kDamping, preset.damping) && ok;
    ok = setParam(kSize, preset.size) && ok;
    return ok;
}

int Reverb::model() const {
    const int value = static_cast<int>(std::lround(param(kModel)));
    return value < 0 ? 0 : (value >= kModelCount ? kModelCount - 1 : value);
}

int Reverb::scaledLength(int base, double sampleRate, double size, int extra) {
    int length = static_cast<int>(std::lround(static_cast<double>(base) * sampleRate / kReferenceRate * size)) +
                 extra;
    return length < 8 ? 8 : length;
}

double Reverb::processComb(Comb& comb, double input, double damping, double feedback) {
    const double delayed = static_cast<double>(comb.buffer[static_cast<size_t>(comb.index)]);
    comb.filter = flushDenormal(delayed * (1.0 - damping) + comb.filter * damping);
    comb.buffer[static_cast<size_t>(comb.index)] =
        flushDenormal(static_cast<float>(input + comb.filter * feedback));
    if (++comb.index >= comb.length) comb.index = 0;
    return delayed;
}

double Reverb::processAllpass(Allpass& allpass, double input) {
    const double delayed = static_cast<double>(allpass.buffer[static_cast<size_t>(allpass.index)]);
    const double output = delayed - kAllpassGain * input;
    allpass.buffer[static_cast<size_t>(allpass.index)] =
        flushDenormal(static_cast<float>(input + kAllpassGain * delayed));
    if (++allpass.index >= allpass.length) allpass.index = 0;
    return output;
}

void Reverb::updateLengths(double size) {
    const double clampedSize = clamp(size, 0.25, kMaxSize);
    for (int c = 0; c < 2; ++c) {
        Channel& channel = mChannels[c];
        const int offset = c == 0 ? 0 : kRightOffset;
        for (int i = 0; i < kNumCombs; ++i) {
            Comb& comb = channel.combs[i];
            int current = scaledLength(kCombLengths[i], sampleRate(), clampedSize, offset);
            if (current > static_cast<int>(comb.buffer.size())) current = static_cast<int>(comb.buffer.size());
            comb.length = current;
            if (comb.index >= comb.length) comb.index = 0;
        }
        for (int i = 0; i < kNumAllpasses; ++i) {
            Allpass& allpass = channel.allpasses[i];
            int current = scaledLength(kAllpassLengths[i], sampleRate(), clampedSize, offset);
            if (current > static_cast<int>(allpass.buffer.size())) {
                current = static_cast<int>(allpass.buffer.size());
            }
            allpass.length = current;
            if (allpass.index >= allpass.length) allpass.index = 0;
        }
    }
}

void Reverb::processBlock(float* const* ch, int nch, int n) {
    const int channels = nch < 2 ? 1 : 2;
    const double predelayMs = nextParam(kPredelayMs, n);
    const double decay = nextParam(kDecay, n);
    const double damping = nextParam(kDamping, n);
    const double mix = nextParam(kMix, n);
    updateLengths(param(kSize));

    for (int c = 0; c < channels; ++c) {
        float* buffer = ch[c];
        if (buffer == nullptr) continue;
        Channel& channel = mChannels[c];
        if (channel.predelay.empty()) continue;
        int predelaySamples = static_cast<int>(std::lround(predelayMs * 0.001 * sampleRate()));
        const int maximumPredelay = static_cast<int>(channel.predelay.size()) - 1;
        if (predelaySamples < 0) predelaySamples = 0;
        if (predelaySamples > maximumPredelay) predelaySamples = maximumPredelay;

        for (int i = 0; i < n; ++i) {
            const double input = static_cast<double>(buffer[i]);
            double predelayed = input;
            if (predelaySamples > 0) {
                int read = channel.predelayIndex - predelaySamples;
                if (read < 0) read += static_cast<int>(channel.predelay.size());
                predelayed = static_cast<double>(channel.predelay[static_cast<size_t>(read)]);
            }
            channel.predelay[static_cast<size_t>(channel.predelayIndex)] =
                flushDenormal(static_cast<float>(input));
            if (++channel.predelayIndex >= static_cast<int>(channel.predelay.size())) {
                channel.predelayIndex = 0;
            }

            const double feed = predelayed * kInputGain;
            double wet = 0.0;
            for (Comb& comb : channel.combs) wet += processComb(comb, feed, damping, decay);
            wet *= kCombNormalize;
            for (Allpass& allpass : channel.allpasses) wet = processAllpass(allpass, wet);

            buffer[i] = flushDenormal(static_cast<float>(input * (1.0 - mix) + wet * mix));
        }
    }
}

void Reverb::onSampleRateChanged() {
    const double rate = sampleRate();
    const int predelaySamples = static_cast<int>(std::ceil(kMaxPredelayMs * 0.001 * rate)) + 4;
    for (int c = 0; c < 2; ++c) {
        Channel& channel = mChannels[c];
        const int offset = c == 0 ? 0 : kRightOffset;
        channel.predelay.assign(static_cast<size_t>(predelaySamples), 0.0f);
        for (int i = 0; i < kNumCombs; ++i) {
            channel.combs[i].buffer.assign(
                static_cast<size_t>(scaledLength(kCombLengths[i], rate, kMaxSize, offset)), 0.0f);
        }
        for (int i = 0; i < kNumAllpasses; ++i) {
            channel.allpasses[i].buffer.assign(
                static_cast<size_t>(scaledLength(kAllpassLengths[i], rate, kMaxSize, offset)), 0.0f);
        }
    }
    updateLengths(param(kSize));
    onReset();
}

void Reverb::onReset() {
    for (Channel& channel : mChannels) {
        for (float& value : channel.predelay) value = 0.0f;
        channel.predelayIndex = 0;
        for (Comb& comb : channel.combs) {
            for (float& value : comb.buffer) value = 0.0f;
            comb.index = 0;
            comb.filter = 0.0;
        }
        for (Allpass& allpass : channel.allpasses) {
            for (float& value : allpass.buffer) value = 0.0f;
            allpass.index = 0;
        }
    }
}

}  // namespace mnky::audio::fx

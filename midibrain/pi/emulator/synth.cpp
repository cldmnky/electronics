#include "synth.hpp"

#include <cmath>

namespace mnky::emulator {
namespace {

constexpr double kTwoPi = 6.283185307179586476925;
constexpr double kInt32Span = 4294967295.0;  // 0xFFFF'FFFF, the LCG output range

// Numerical Recipes LCG: one multiply-add per sample, no allocation, no global
// state, so every channel draws its own reproducible stream.
std::uint32_t nextRandom(std::uint32_t& state) {
  state = state * 1664525u + 1013904223u;
  return state;
}

}  // namespace

SynthBank::SynthBank() {
  for (int channel = 0; channel < audio::kMaxInputs; ++channel) {
    mPointers[channel] = mBuffer[channel];
    mGenerators[channel].rng = seedFor(channel);
  }
}

std::uint32_t SynthBank::seedFor(int channel) {
  // Fixed golden-ratio scramble: distinct streams per channel, identical
  // streams between runs.
  return 0x9e3779b9u * static_cast<std::uint32_t>(channel + 1) + 0x0000002bu;
}

void SynthBank::setVoice(int channel, Voice voice, float hz, float amplitude) {
  if (channel < 0 || channel >= audio::kMaxInputs) return;
  Generator& g = mGenerators[channel];
  g.voice = voice;
  g.hz = hz;
  g.amplitude = amplitude < 0.0f ? 0.0f : amplitude;
  g.phase = 0.0;
  g.rng = seedFor(channel);

  int active = 0;
  for (int c = 0; c < audio::kMaxInputs; ++c) {
    if (mGenerators[c].voice != Voice::Silent) active = c + 1;
  }
  mActiveChannels = active;
}

void SynthBank::clear() {
  for (int channel = 0; channel < audio::kMaxInputs; ++channel) {
    mGenerators[channel] = Generator{};
    mGenerators[channel].rng = seedFor(channel);
    mPeak[channel] = 0.0f;
    for (int i = 0; i < audio::kBlockSize; ++i) mBuffer[channel][i] = 0.0f;
  }
  mActiveChannels = 0;
}

void SynthBank::render(int frames) {
  const int n = frames < audio::kBlockSize ? frames : audio::kBlockSize;
  if (n <= 0) return;

  for (int channel = 0; channel < audio::kMaxInputs; ++channel) {
    Generator& g = mGenerators[channel];
    float* out = mBuffer[channel];
    float peak = 0.0f;
    if (g.voice == Voice::Sine) {
      const double step = kTwoPi * static_cast<double>(g.hz) /
                          static_cast<double>(kSampleRate);
      for (int i = 0; i < n; ++i) {
        const float sample = static_cast<float>(static_cast<double>(g.amplitude) *
                                               std::sin(g.phase));
        g.phase += step;
        if (g.phase >= kTwoPi) g.phase -= kTwoPi;
        out[i] = sample;
        const float magnitude = std::fabs(sample);
        if (magnitude > peak) peak = magnitude;
      }
    } else if (g.voice == Voice::Noise) {
      for (int i = 0; i < n; ++i) {
        const double unit = static_cast<double>(nextRandom(g.rng)) / kInt32Span;
        const float sample =
            static_cast<float>(static_cast<double>(g.amplitude) * (2.0 * unit - 1.0));
        out[i] = sample;
        const float magnitude = std::fabs(sample);
        if (magnitude > peak) peak = magnitude;
      }
    } else {
      for (int i = 0; i < n; ++i) out[i] = 0.0f;
    }
    mPeak[channel] = peak;
  }
}

float SynthBank::peak(int channel) const {
  if (channel < 0 || channel >= audio::kMaxInputs) return 0.0f;
  return mPeak[channel];
}

}  // namespace mnky::emulator

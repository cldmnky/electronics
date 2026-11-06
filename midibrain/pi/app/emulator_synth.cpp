#include "emulator_synth.hpp"

#include <cmath>

namespace mnky::app {
namespace {
constexpr double kTwoPi = 6.283185307179586476925;
constexpr double kPhaseScale = 4294967296.0;  // 2^32, one full turn
// Generator level: low enough that 18 summed strips stay below clipping with
// every strip fader at unity and the master at 0 dB.
constexpr float kSineLevel = 0.03f;
constexpr float kNoiseLevel = 0.015f;
}  // namespace

void EmulatorSynth::prepare(double sampleRate) {
  sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
  for (int channel = 0; channel < audio::kMaxDeviceInputs; ++channel) {
    // Odd channels get noise, even channels sine; the seeds differ so the noise
    // lanes are uncorrelated.
    phase_[channel] = 0;
    rng_[channel] = 0x9e3779b9u * static_cast<std::uint32_t>(channel + 1) + 0x1234567u;
    filtered_[channel] = 0.0f;
  }
}

float EmulatorSynth::noise(int channel) {
  std::uint32_t state = rng_[channel];
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  rng_[channel] = state;
  const float white = static_cast<float>(state) / 2147483648.0f - 1.0f;
  // One-pole low-pass so the noise lane is not harsh white noise.
  filtered_[channel] += 0.05f * (white - filtered_[channel]);
  return filtered_[channel];
}

void EmulatorSynth::fillBlock(float* const* channels, int channelCount, int frames) {
  if (channels == nullptr || channelCount <= 0 || frames <= 0) return;
  const int count = channelCount < audio::kMaxDeviceInputs ? channelCount : audio::kMaxDeviceInputs;
  const int n = frames < audio::kBlockSize ? frames : audio::kBlockSize;

  for (int channel = 0; channel < count; ++channel) {
    float* out = channels[channel];
    if (out == nullptr) continue;
    if ((channel & 1) != 0) {
      for (int i = 0; i < n; ++i) out[i] = noise(channel) * kNoiseLevel;
      continue;
    }
    // Even channels: a sine whose pitch steps up per stereo-ish pair.
    const double frequency = 110.0 * (1.0 + static_cast<double>(channel / 2));
    const auto increment = static_cast<std::uint32_t>(
        frequency / sampleRate_ * kPhaseScale);
    std::uint32_t phase = phase_[channel];
    for (int i = 0; i < n; ++i) {
      const double turn = static_cast<double>(phase) / kPhaseScale;
      out[i] = static_cast<float>(std::sin(turn * kTwoPi)) * kSineLevel;
      phase += increment;
    }
    phase_[channel] = phase;
  }

  for (int channel = count; channel < audio::kMaxDeviceInputs; ++channel) {
    if (channels[channel] != nullptr) {
      for (int i = 0; i < n; ++i) channels[channel][i] = 0.0f;
    }
  }
}

}  // namespace mnky::app

#pragma once
// Emulator-mode audio source: one deterministic generator per mixer channel,
// so the mixer, recorder and meters have signal on a machine with no ADAT
// interface. This is the app's own stand-in, not the emulator team's backend.
#include <cstdint>

#include "../audio/engine.hpp"

namespace mnky::app {

class EmulatorSynth {
 public:
  void prepare(double sampleRate);

  // Fills `channelCount` mono buffers of `frames` (<= audio::kBlockSize).
  void fillBlock(float* const* channels, int channelCount, int frames);

 private:
  float noise(int channel);

  double sampleRate_ = 48000.0;
  // 32-bit phase accumulators: modulo wraps for free in the audio callback.
  std::uint32_t phase_[audio::kMaxDeviceInputs] = {};
  std::uint32_t rng_[audio::kMaxDeviceInputs] = {};
  float filtered_[audio::kMaxDeviceInputs] = {};
};

}  // namespace mnky::app

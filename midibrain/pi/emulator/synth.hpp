#pragma once
// Scripted multitrack signal source for the headless emulator: one JUCE-free
// float generator per audio::Engine input channel, so a selftest session can
// produce audio without opening an ALSA/CoreAudio device.
//
// Deterministic by construction: no clock and no std::random_device. The noise
// LCG is seeded per channel and the sine phase advances by exactly
// 2*pi*hz/sampleRate per frame, so repeated runs produce the same samples and
// the WAV verification can assert on sample content instead of "not silent".
#include <cstdint>

#include "../audio/engine.hpp"

namespace mnky::emulator {

class SynthBank {
 public:
  enum class Voice : std::uint8_t { Silent, Sine, Noise };

  /// One sample rate for the whole bank; the recorder take and the WAV checks
  /// use the same value.
  static constexpr std::uint32_t kSampleRate = 48000;

  SynthBank();

  /// `channel` is the Engine input index (0..audio::kMaxInputs-1) and is
  /// ignored when out of range. `hz` is ignored for Silent and Noise;
  /// `amplitude` is the peak of the generated waveform.
  void setVoice(int channel, Voice voice, float hz, float amplitude);
  /// Back to all-silent with phases and noise seeds reset.
  void clear();

  /// Fills one block of `frames` (clamped to audio::kBlockSize) for every
  /// channel and records each channel's peak for peak().
  void render(int frames);
  /// Render-ready mono pointers for audio::Engine::process(in, activeChannels(), ...).
  const float* const* inputs() const { return mPointers; }
  /// Highest voiced channel index + 1, so a take does not render unused inputs.
  int activeChannels() const { return mActiveChannels; }
  /// Peak absolute sample of `channel` in the last rendered block (0 when
  /// silent or out of range).
  float peak(int channel) const;

 private:
  struct Generator {
    Voice voice = Voice::Silent;
    float hz = 0.0f;
    float amplitude = 0.0f;
    double phase = 0.0;
    std::uint32_t rng = 0;
  };
  static std::uint32_t seedFor(int channel);

  Generator mGenerators[audio::kMaxInputs];
  float mBuffer[audio::kMaxInputs][audio::kBlockSize] = {};
  const float* mPointers[audio::kMaxInputs] = {};
  float mPeak[audio::kMaxInputs] = {};
  int mActiveChannels = 0;
};

}  // namespace mnky::emulator

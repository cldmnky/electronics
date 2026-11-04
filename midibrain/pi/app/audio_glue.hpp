#pragma once
// Audio glue: the Engine + Recorder run behind one juce::AudioIODeviceCallback,
// fed either by a real audio device (Pi/ALSA, desktop/CoreAudio) or, in
// --emulator mode, by a clocked generator thread that uses the app's own
// EmulatorSynth. No emulator-team dependency.
#include <atomic>
#include <memory>

#include <juce_audio_devices/juce_audio_devices.h>

#include "../audio/engine.hpp"
#include "../audio/recorder.hpp"
#include "emulator_synth.hpp"
#include "fx_slot.hpp"
#include "mixer_model.hpp"

namespace mnky::app {

class AudioGlue final : private juce::AudioIODeviceCallback {
 public:
  AudioGlue(MixerModel& mixer, FxManager& fx, bool emulator);
  ~AudioGlue() override;
  AudioGlue(const AudioGlue&) = delete;
  AudioGlue& operator=(const AudioGlue&) = delete;

  bool start();
  void stop();
  bool emulated() const { return emulated_; }
  juce::String backendDescription() const;
  double sampleRate() const;
  int blockSize() const;

  bool startTake(const juce::File& directory);
  audio::Recorder::Take stopTake();
  bool recording() const;
  std::uint64_t drops() const;
  audio::Recorder::Take lastTake() const;

  // Read-and-decay master peak for the meters (0 = left, 1 = right).
  float peak(int channel);

 private:
  class EmulatorDriver;

  void audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
                                        int numInputChannels,
                                        float* const* outputChannelData,
                                        int numOutputChannels,
                                        int numSamples,
                                        const juce::AudioIODeviceCallbackContext& context) override;
  void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
  void audioDeviceStopped() override;

  void renderBlock(const float* const* inputChannelData, int numInputChannels,
                   float* outL, float* outR, int numSamples);
  void updatePeaks(const float* outL, const float* outR, int numSamples);

  MixerModel& mixer_;
  FxManager& fx_;
  bool emulated_;
  audio::Engine engine_;
  audio::Recorder recorder_;
  EmulatorSynth synth_;
  float synthBuffer_[audio::kMaxInputs][audio::kBlockSize] = {};

  juce::AudioDeviceManager deviceManager_;
  std::unique_ptr<EmulatorDriver> driver_;
  juce::String backend_;

  std::atomic<double> sampleRate_{48000.0};
  std::atomic<int> blockSize_{audio::kBlockSize};
  std::atomic<bool> running_{false};
  std::atomic<float> peaks_[2] = {};

  mutable juce::CriticalSection takeLock_;
  audio::Recorder::Take lastTake_;
};

}  // namespace mnky::app

#pragma once
// Audio glue: the Engine + Recorder run behind one juce::AudioIODeviceCallback,
// fed either by a real audio device (Pi/ALSA or JACK, desktop/CoreAudio) or, in
// --emulator mode, by a clocked generator thread that uses the app's own
// EmulatorSynth. No emulator-team dependency.
//
// Backend selection (System screen): there is exactly one AudioDeviceManager and
// one audio callback. Switching ALSA/JACK/Default restarts that one device
// manager with another device type; the emulator generator is the documented
// fallback when no device opens. The choice is persisted in the app's own
// PropertiesFile (never in a session file).
#include <atomic>
#include <memory>

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_data_structures/juce_data_structures.h>

#include "../audio/engine.hpp"
#include "../audio/recorder.hpp"
#include "audio_backend.hpp"
#include "emulator_synth.hpp"
#include "fx_slot.hpp"
#include "mixer_model.hpp"

namespace mnky::app {

// What the System screen shows: the persisted request, the backend it resolves
// to on this machine, the live device numbers and the xrun counter.
struct AudioBackendState {
  AudioBackendChoice choice;
  AudioBackend effective = kDefaultAudioBackend;
  AudioBackendAvailability availability;
  juce::String liveType;    // device type in use ("ALSA"/"JACK"), or "emulator"/"none"
  juce::String liveDevice;  // output device name in use
  double sampleRate = 0.0;
  int blockSize = 0;
  int xruns = 0;
  juce::String note;  // fallback/apply note; empty when the request was honoured
};

class AudioGlue final : private juce::AudioIODeviceCallback {
 public:
  AudioGlue(MixerModel& mixer, FxManager& fx, bool emulator);
  ~AudioGlue() override;
  AudioGlue(const AudioGlue&) = delete;
  AudioGlue& operator=(const AudioGlue&) = delete;

  bool start();
  void stop();
  bool emulated() const { return emulated_.load(); }
  juce::String backendDescription() const;
  double sampleRate() const;
  int blockSize() const;

  // --- backend selection (System screen) ------------------------------------
  // The persisted request, loaded from the app's PropertiesFile at construction.
  AudioBackendChoice audioChoice() const;
  // Cached device-type availability. The JACK probe opens and closes a JACK
  // client, so it runs on demand (startup, REFRESH, Apply) and never on a timer.
  AudioBackendAvailability backendAvailability() const;
  void rescanBackends();
  // Output device names of a backend's device type; empty when the type is
  // absent or has no devices.
  juce::StringArray outputDeviceNames(AudioBackend backend);
  // Restarts audio through the one AudioDeviceManager, or with the emulator
  // generator when the resolved backend is Default in --emulator mode. Persists
  // the request and returns an error string (empty on success).
  juce::String applyAudioChoice(const AudioBackendChoice& choice);
  AudioBackendState backendState() const;

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

  void loadAudioChoice();
  void saveAudioChoice();
  void startEmulator(const juce::String& reason);
  void stopEmulator();
  void closeDevice();
  juce::String openDevice(AudioBackend backend, const juce::String& outputDevice,
                          int bufferSize);
  juce::String platformDefaultDeviceType();
  juce::String resolveDeviceName(AudioBackend backend, const std::string& stored);
  juce::String describeChoice() const;

  MixerModel& mixer_;
  FxManager& fx_;
  bool emulatorStartup_ = false;  // --emulator: start on the generator
  std::atomic<bool> emulated_{false};
  audio::Engine engine_;
  audio::Recorder recorder_;
  EmulatorSynth synth_;
  float synthBuffer_[audio::kMaxInputs][audio::kBlockSize] = {};

  juce::AudioDeviceManager deviceManager_;
  std::unique_ptr<EmulatorDriver> driver_;
  juce::String backend_;

  AudioBackendChoice choice_;
  AudioBackendAvailability availability_;
  juce::String note_;  // fallback/apply note shown on the System screen
  std::unique_ptr<juce::PropertiesFile> properties_;

  std::atomic<double> sampleRate_{48000.0};
  std::atomic<int> blockSize_{audio::kBlockSize};
  std::atomic<bool> running_{false};
  std::atomic<float> peaks_[2] = {};

  mutable juce::CriticalSection takeLock_;
  audio::Recorder::Take lastTake_;
};

}  // namespace mnky::app

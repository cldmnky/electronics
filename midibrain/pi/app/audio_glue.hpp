#pragma once
// Audio glue: the Engine + Recorder run behind one Qt Multimedia audio path,
// fed either by a real QAudioSink (ALSA/PulseAudio on Linux, CoreAudio on
// macOS) or, in --emulator mode, by a clocked generator thread that uses the
// app's own EmulatorSynth. A selected QAudioSource feeds the first two engine
// inputs through a small lock-protected ring buffer.
//
// Backend selection (System screen): one QAudioSink at a time. Switching
// default/device/emulator restarts that one sink; QSettings stores the request
// (never a session file). JACK is not offered: Qt Multimedia exposes devices,
// and PipeWire's Pulse compatibility layer already provides inter-app routing.
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <QtMultimedia/QAudioFormat>

#include "../audio/click.hpp"
#include "../audio/engine.hpp"
#include "../audio/recorder.hpp"
#include "audio_backend.hpp"
#include "emulator_synth.hpp"
#include "fx_slot.hpp"
#include "mixer_model.hpp"
#include "session_settings.hpp"

QT_BEGIN_NAMESPACE
class QAudioSink;
class QAudioSource;
class QIODevice;
QT_END_NAMESPACE

namespace mnky::app {

class AppState;

struct AudioDeviceOption {
  std::string id;  // stable base64 of QAudioDevice::id()
  std::string name;
  bool isDefault = false;
};

// What the System screen shows: the persisted request, the backend it resolves
// to on this machine, the live device numbers and the fallback note.
struct AudioBackendState {
  AudioBackendChoice choice;
  AudioBackend effective = kDefaultAudioBackend;
  AudioBackendAvailability availability;
  std::string liveType;    // "Qt Multimedia", "emulator" or "none"
  std::string liveDevice;  // output device name in use
  double sampleRate = 0.0;
  int blockSize = 0;
  std::uint64_t callbackBlocks = 0;  // rendered engine blocks (audio callback proof)
  std::string note;  // fallback/apply note; empty when the request was honoured
};

class AudioGlue {
 public:
  AudioGlue(MixerModel& mixer, FxManager& fx, AppState& state, bool emulator);
  ~AudioGlue();
  AudioGlue(const AudioGlue&) = delete;
  AudioGlue& operator=(const AudioGlue&) = delete;

  bool start();
  void stop();
  bool emulated() const { return emulated_.load(); }
  std::string backendDescription() const;
  double sampleRate() const { return sampleRate_.load(); }
  int blockSize() const { return blockSize_.load(); }

  // --- backend selection (System screen) ------------------------------------
  AudioBackendChoice audioChoice() const { return choice_; }
  AudioBackendAvailability backendAvailability() const;
  void rescanBackends();
  std::vector<AudioDeviceOption> outputDevices() const;
  std::vector<AudioDeviceOption> inputDevices() const;
  // Persists the request and restarts the sink/emulator. Returns an error
  // string (empty on success).
  std::string applyAudioChoice(const AudioBackendChoice& choice);
  AudioBackendState backendState() const;

  bool startTake(const std::filesystem::path& directory);
  audio::Recorder::Take stopTake();
  bool recording() const;
  std::uint64_t drops() const;
  audio::Recorder::Take lastTake() const;

  // Control-thread monitor/compressor configuration. The audio callback copies
  // these atomics into the engine every block.
  void applyMonitorSettings(const SessionSettings& settings);
  // Control-thread take-loop configuration (session v2 takes.loop). Calls
  // Engine::setTakeLoop for all 18 tracks, which maps the beat window to take
  // frames with the transport tempo of that moment, so the caller re-applies it
  // after a tempo change (AppController::timerTick) as the engine documents.
  void applyTakeLoops(const SessionSettings& settings);

  // Control-thread take playback selection (engine take player). Returns false
  // with `error` when the engine refuses the file (not 48 kHz WAV, missing).
  bool setActiveTake(int track, const std::filesystem::path& path,
                     std::string* error = nullptr);
  void clearActiveTake(int track);

  // Read-and-decay master peak for the meters (0 = left, 1 = right).
  float peak(int channel);
  // Read-and-decay post-gain mono tap peak for one strip, for the mixer meters
  // and clip-hold LEDs. Out-of-range channels read 0.
  float stripPeak(int channel);

 private:
  class EmulatorDriver;
  class AudioRenderDevice;
  class AudioInputConsumer;

  void renderBlock(const float* const* inputChannelData, int numInputChannels,
                   float* outL, float* outR, int numSamples);
  void updatePeaks(const float* outL, const float* outR, int numSamples);
  void pushInput(const char* data, int frames, const QAudioFormat& format);
  void copyInput(float* left, float* right, int frames);

  void loadAudioChoice();
  void saveAudioChoice();
  void startEmulator(const std::string& reason);
  void stopEmulator();
  void closeAudio();
  std::string openDevice(const std::string& outputDeviceId, int bufferSize);
  bool openInput(const std::string& inputDeviceId);

  MixerModel& mixer_;
  FxManager& fx_;
  AppState& state_;
  bool emulatorStartup_ = false;  // --emulator: start on the generator
  std::atomic<bool> emulated_{false};
  audio::Engine engine_;
  audio::Recorder recorder_;
  EmulatorSynth synth_;
  float synthBuffer_[audio::kMaxInputs][audio::kBlockSize] = {};
  float inputL_[audio::kBlockSize] = {};
  float inputR_[audio::kBlockSize] = {};

  // Qt device handles are GUI-thread objects; the render device is read from
  // Qt's audio thread and only touches the engine.
  std::unique_ptr<QAudioSink> sink_;
  std::unique_ptr<QAudioSource> source_;
  std::unique_ptr<AudioRenderDevice> renderDevice_;
  std::unique_ptr<AudioInputConsumer> inputConsumer_;
  QAudioFormat outputFormat_;
  int outputBytesPerFrame_ = 8;
  int deviceBufferFrames_ = 0;

  std::unique_ptr<EmulatorDriver> driver_;
  std::string backend_;
  std::string deviceName_;
  std::string inputName_;

  AudioBackendChoice choice_;
  AudioBackendAvailability availability_;
  std::string note_;

  std::atomic<double> sampleRate_{48000.0};
  std::atomic<int> blockSize_{audio::kBlockSize};
  std::atomic<bool> running_{false};
  std::atomic<std::uint64_t> renderBlocks_{0};
  std::atomic<float> peaks_[2] = {};
  std::atomic<float> stripPeaks_[audio::kMaxInputs] = {};

  // Monitor configuration copied from SessionSettings on the GUI thread and
  // read once per block by the audio callback.
  std::atomic<int> monitorPhones_{0};
  std::atomic<int> monitorCueStyle_{0};
  std::atomic<int> monitorOut34Mode_{0};
  std::atomic<int> monitorSend_[audio::kNumSendDestinations] = {};
  std::atomic<bool> compOn_{false};
  std::atomic<float> compThreshDb_{-12.0f};
  std::atomic<float> compRatio_{2.0f};
  std::atomic<float> compAttackMs_{10.0f};
  std::atomic<float> compReleaseMs_{100.0f};
  std::atomic<float> compMakeupDb_{0.0f};
  std::atomic<int> compMode_{0};

  std::mutex inputLock_;
  std::vector<float> inputRingL_;
  std::vector<float> inputRingR_;
  std::uint64_t inputWritten_ = 0;
  int inputRingFrames_ = 0;

  mutable std::mutex takeLock_;
  audio::Recorder::Take lastTake_;
};

}  // namespace mnky::app

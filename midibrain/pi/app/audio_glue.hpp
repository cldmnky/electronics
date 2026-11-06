#pragma once
// Audio glue: the Engine + Recorder run behind one native audio backend, fed
// either by libpipewire/snd_pcm (audio::DeviceBackend, see
// audio/device_backend.hpp) or, in --emulator mode, by a clocked generator
// thread that uses the app's own EmulatorSynth. Qt Multimedia is not part of
// audio I/O any more; Qt stays the UI toolkit.
//
// Device frames are 48 kHz interleaved float32. The backend converts to and from
// the interface's real sample format (UMC1820: S32_LE capture, S24_3LE
// playback) and reports the negotiated period; the full 18-channel capture is
// fanned out to the engine strips through the existing
// setInputSource/inCh/inPair path, and the engine's Main/OUT3&4/phones buses are
// mapped onto the 20 device outputs through session deviceOut
// (Engine::deviceOut()).
//
// Backend selection (System screen / `audio/backend`): one backend at a time.
// Switching default/device/emulator or the API restarts it; QSettings stores
// the request (never a session file).
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "../audio/click.hpp"
#include "../audio/device_backend.hpp"
#include "../audio/engine.hpp"
#include "../audio/recorder.hpp"
#include "audio_backend.hpp"
#include "emulator_synth.hpp"
#include "fx_slot.hpp"
#include "mixer_model.hpp"
#include "performance_model.hpp"
#include "session_settings.hpp"

namespace mnky::app {

class AppState;

struct AudioDeviceOption {
  std::string id;  // "pipewire:<node.name>" or "alsa:<pcm>", stable per host
  std::string name;
  bool isDefault = false;
};

// What the System screen shows: the persisted request, the backend it resolves
// to on this machine, the live device numbers and the fallback note.
struct AudioBackendState {
  AudioBackendChoice choice;
  AudioBackend effective = kDefaultAudioBackend;
  AudioBackendAvailability availability;
  std::string liveType;    // "pipewire", "alsa", "emulator" or "none"
  std::string liveDevice;  // real node/card name in use
  double sampleRate = 0.0;
  int blockSize = 0;
  int outputChannels = 0;
  int inputChannels = 0;
  std::uint64_t callbackBlocks = 0;  // rendered engine blocks (audio callback proof)
  std::uint64_t xruns = 0;
  std::string note;  // fallback/apply note; empty when the request was honoured
};

class AudioGlue final : private audio::DeviceClient {
 public:
  // The two send buses are a chain: the built-in delay/reverb that the X/Y
  // grids drive runs first, then the app's CLAP slot for that bus.
  AudioGlue(MixerModel& mixer, FxManager& fx, PerformanceModel& performance,
            AppState& state, bool emulator);
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
  // Persists the request and restarts the backend/emulator. Returns an error
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
  // Engine::setTakeLoop for every track slot, which maps the beat window to take
  // frames with the transport tempo of that moment, so the caller re-applies it
  // after a tempo change (AppController::timerTick) as the engine documents.
  void applyTakeLoops(const SessionSettings& settings);
  // Control thread: one per-track FX rack slot (Engine::setTrackFxSlot).
  // MixerModel installs a sink that calls this after every rack edit, so the
  // rack reaches the engine without the audio callback touching the engine's
  // allocating setter. A CLAP id additionally creates/reuses the FxManager host
  // instance and attaches its adapter with Engine::setTrackFxSlotEffect(); a
  // built-in/empty id detaches and destroys the instance. applyTrackFxRack()
  // forwards every stored slot, which the session load and the new-project
  // reset use. `wet` is the slot's dry/wet crossfade; the effect clamps its own
  // parameters.
  void applyTrackFxSlot(int track, int slot);
  void applyTrackFxRack();
  // Control-thread EQ forwarding (session fx.eq -> Engine track/main inserts).
  // Every band is written, so an all-None state bypasses the inserts; the
  // engine clamps type 0..5, gain -24..+24 dB, freq 20..20000 Hz and Q
  // 0.1..24. The UI model caps Q at 10.0 (the CLAP effect range), so a loaded
  // q > 10 is normalized there and logged by the loader.
  void applyEq(const session::EqState& state);
  // Readback of the bands the engine currently applies (control thread; the
  // EQ forwarding test and the dirty fingerprint use it).
  session::EqState appliedEq() const;

  // Control-thread take playback selection (engine take player). Returns false
  // with `error` when the engine refuses the file (not 48 kHz WAV, missing).
  bool setActiveTake(int track, const std::filesystem::path& path,
                     std::string* error = nullptr);
  void clearActiveTake(int track);
  // Control-thread non-destructive region edit (Engine::setTakeRegions, session
  // v2 takes.edit). An empty list clears the edit and resumes the take stack;
  // the caller resolves each region's take key to a path first. Re-apply after
  // a tempo change, exactly like applyTakeLoops().
  bool setTakeRegions(int track, const std::vector<audio::TakeRegion>& regions,
                      std::string* error = nullptr);

  // Read-and-decay master peak for the meters (0 = left, 1 = right).
  float peak(int channel);
  // Read-and-decay post-gain mono tap peak for one strip, for the mixer meters
  // and clip-hold LEDs. Out-of-range channels read 0.
  float stripPeak(int channel);

 private:
  class EmulatorDriver;

  // audio::DeviceClient: both callbacks run on the backend thread, serialised.
  void renderDevice(float* interleaved, int frames, int channels) override;
  void captureDevice(const float* interleaved, int frames, int channels) override;

  void renderEmulatorBlock(int numSamples);
  void prepareEngineBlock();
  void writeRecorderBlock(int frames);
  void updatePeaks(const float* outL, const float* outR, int numSamples);

  void loadAudioChoice();
  void saveAudioChoice();
  void startEmulator(const std::string& reason);
  void stopEmulator();
  void closeAudio();
  // Opens the selected backend. Empty return means the device is running.
  std::string openBackend(AudioBackend backend, const std::string& outputDeviceId,
                          const std::string& inputDeviceId, int bufferSize);
  int outputChannelsFor(const std::string& node) const;
  int inputChannelsFor(const std::string& node) const;
  std::vector<std::string> outputPositionsFor(const std::string& node) const;
  std::vector<std::string> inputPositionsFor(const std::string& node) const;
  std::string deviceNameFor(const std::string& id) const;

  MixerModel& mixer_;
  FxManager& fx_;
  PerformanceModel& performance_;
  AppState& state_;
  bool emulatorStartup_ = false;  // --emulator: start on the generator
  std::atomic<bool> emulated_{false};
  audio::Engine engine_;
  audio::Recorder recorder_;
  EmulatorSynth synth_;
  float synthBuffer_[audio::kMaxDeviceInputs][audio::kBlockSize] = {};
  audio::DevicePump pump_;
  float capturePlanes_[audio::kMaxDeviceInputs][audio::kBlockSize] = {};
  float busMainL_[audio::kBlockSize] = {};
  float busMainR_[audio::kBlockSize] = {};
  float busOut34L_[audio::kBlockSize] = {};
  float busOut34R_[audio::kBlockSize] = {};
  float busPhonesL_[audio::kBlockSize] = {};
  float busPhonesR_[audio::kBlockSize] = {};
  audio::DeviceOutSlot deviceOutSlots_[audio::kMaxDeviceChannels] = {};

  // The native backend is created, opened and started on the control thread;
  // its callbacks only touch the engine, the pump and the recorder.
  std::unique_ptr<audio::DeviceBackend> device_;
  audio::DeviceInventory inventory_;
  int devicePeriodFrames_ = 0;

  std::unique_ptr<EmulatorDriver> driver_;
  std::string backend_;  // live backend description for the System readout
  std::string deviceName_;
  std::string inputName_;

  AudioBackendChoice choice_;
  AudioBackendAvailability availability_;
  std::string note_;

  std::atomic<double> sampleRate_{48000.0};
  std::atomic<int> blockSize_{audio::kBlockSize};
  std::atomic<int> outputChannels_{0};
  std::atomic<int> inputChannels_{0};
  std::atomic<bool> running_{false};
  std::atomic<std::uint64_t> renderBlocks_{0};
  std::atomic<float> peaks_[2] = {};
  // Per-track meter taps: one per mixer track slot (audio::kMaxTracks), not the
  // device capture width, so every strip has a meter.
  std::atomic<float> stripPeaks_[audio::kMaxTracks] = {};

  // Monitor configuration copied from SessionSettings on the GUI thread and
  // read once per block by the audio callback.
  std::atomic<int> monitorPhones_{0};
  std::atomic<int> monitorCueStyle_{0};
  std::atomic<int> monitorSend_[audio::kNumSendDestinations] = {};
  std::atomic<bool> compOn_{false};
  std::atomic<float> compThreshDb_{-12.0f};
  std::atomic<float> compRatio_{2.0f};
  std::atomic<float> compAttackMs_{10.0f};
  std::atomic<float> compReleaseMs_{100.0f};
  std::atomic<float> compMakeupDb_{0.0f};
  std::atomic<int> compMode_{0};

  mutable std::mutex takeLock_;
  audio::Recorder::Take lastTake_;
};

}  // namespace mnky::app

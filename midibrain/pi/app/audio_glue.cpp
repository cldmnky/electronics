#include "audio_glue.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "app_state.hpp"
#include "logic.hpp"
#include "logging.hpp"

namespace mnky::app {

static_assert(kDefaultAudioBufferSize == audio::kBlockSize,
              "the default buffer size offered by the System screen is the engine block size");

namespace {
juce::String yesNo(bool value) {
  return value ? "yes" : "no";
}
}  // namespace

// Clocked stand-in for an audio device: pushes kBlockSize frames through the
// same callback at 48 kHz so the mixer, recorder and meters behave as they do
// with a real interface. Wall-clock driven, not sample-accurate.
class AudioGlue::EmulatorDriver final : private juce::Thread {
 public:
  explicit EmulatorDriver(AudioGlue& owner)
      : juce::Thread("mnkybox emulator audio"), owner_(owner) {}

  // juce::Thread's control API is private through the private inheritance, so
  // AudioGlue starts and stops the driver through these two wrappers.
  void begin() { startThread(juce::Thread::Priority::high); }
  void end() {
    signalThreadShouldExit();
    stopThread(3000);
  }

  void run() override {
    constexpr int block = audio::kBlockSize;
    const double sampleRate = owner_.sampleRate();
    const double intervalMs = 1000.0 * static_cast<double>(block) / sampleRate;

    std::vector<float> left(static_cast<std::size_t>(block));
    std::vector<float> right(static_cast<std::size_t>(block));
    float* outputs[2] = {left.data(), right.data()};

    auto next = juce::Time::getMillisecondCounterHiRes();
    while (!threadShouldExit()) {
      owner_.renderBlock(nullptr, 0, outputs[0], outputs[1], block);
      next += intervalMs;
      const double now = juce::Time::getMillisecondCounterHiRes();
      const double wait = next - now;
      if (wait > 0.0) {
        juce::Thread::sleep(static_cast<unsigned int>(std::min(wait, 20.0)));
      } else if (wait < -intervalMs * 8.0) {
        next = now;  // fell far behind (debugger, sleep): resync, do not burst
      }
    }
  }

 private:
  AudioGlue& owner_;
};

AudioGlue::AudioGlue(MixerModel& mixer, FxManager& fx, AppState& state, bool emulator)
    : mixer_(mixer), fx_(fx), state_(state), emulatorStartup_(emulator), emulated_(emulator) {
  for (auto& peak : peaks_) peak.store(0.0f);
  synth_.prepare(sampleRate());
  loadAudioChoice();
}

AudioGlue::~AudioGlue() {
  stop();
  properties_.reset();
}

bool AudioGlue::start() {
  note_.clear();
  if (emulatorStartup_) {
    startEmulator("emulator audio (--emulator); apply ALSA or JACK to open a real device");
    log("audio: " + backend_);
    return true;
  }

  rescanBackends();
  const auto selection = resolveAudioBackend(choice_.backend, availability_);
  if (selection.fellBack) {
    note_ = selection.note;
    log("audio: " + note_);
  }
  const auto deviceName =
      selection.fellBack ? juce::String() : resolveDeviceName(selection.effective, choice_.deviceName);
  const auto error = openDevice(selection.effective, deviceName, choice_.bufferSize);
  if (error.isNotEmpty()) {
    closeDevice();
    note_ = "no audio device (" + error + "); using the emulator generator";
    startEmulator(note_);
    log("audio: " + backend_);
    return false;
  }
  log("audio: " + backend_);
  return true;
}

void AudioGlue::stop() {
  stopEmulator();
  closeDevice();
  if (recorder_.recording()) {
    const auto take = recorder_.stopTake();
    const juce::ScopedLock lock(takeLock_);
    lastTake_ = take;
  }
  running_.store(false);
}

void AudioGlue::startEmulator(const juce::String& reason) {
  emulated_.store(true);
  backend_ = "emulator: sine/noise generator, 48 kHz / 128 frames";
  if (reason.isNotEmpty()) note_ = reason;
  synth_.prepare(sampleRate());
  driver_ = std::make_unique<EmulatorDriver>(*this);
  driver_->begin();
  running_.store(true);
}

void AudioGlue::stopEmulator() {
  if (driver_ != nullptr) {
    driver_->end();
    driver_.reset();
  }
  emulated_.store(false);
}

void AudioGlue::closeDevice() {
  deviceManager_.removeAudioCallback(this);
  deviceManager_.closeAudioDevice();
}

juce::String AudioGlue::openDevice(AudioBackend backend, const juce::String& outputDevice,
                                   int bufferSize) {
  // One device manager for the whole app: release the emulator generator and
  // whatever device is open, pick the requested type, then open it with the
  // requested device/period. JACK ignores the rate and period (the server owns
  // them), which is why the System screen shows the live numbers.
  stopEmulator();
  closeDevice();

  // The platform default is the first device type with devices (ALSA before
  // JACK on Linux, CoreAudio on macOS); explicit Default after JACK must return
  // to it instead of keeping the last chosen type.
  const auto wantedType = backend == AudioBackend::Default
                              ? platformDefaultDeviceType()
                              : juce::String(std::string(audioDeviceTypeName(backend)));
  if (wantedType.isNotEmpty() && wantedType != deviceManager_.getCurrentAudioDeviceType()) {
    deviceManager_.setCurrentAudioDeviceType(wantedType, false);
  }

  juce::AudioDeviceManager::AudioDeviceSetup preferred;
  preferred.outputDeviceName = outputDevice;
  preferred.sampleRate = 48000.0;
  preferred.bufferSize = clampAudioBufferSize(bufferSize);

  auto error = deviceManager_.initialise(audio::kMaxInputs, 2, nullptr, true, {}, &preferred);
  if (error.isNotEmpty()) return error;

  deviceManager_.addAudioCallback(this);
  if (auto* device = deviceManager_.getCurrentAudioDevice()) {
    backend_ = device->getTypeName() + " / " + device->getName() + ", " +
               juce::String(device->getCurrentSampleRate(), 0) + " Hz / " +
               juce::String(device->getCurrentBufferSizeSamples()) + " frames, " +
               juce::String(device->getActiveInputChannels().countNumberOfSetBits()) + " in";
  } else {
    backend_ = "audio device manager active, no current device";
  }
  return {};
}

void AudioGlue::loadAudioChoice() {
  juce::PropertiesFile::Options options;
  options.applicationName = "mnkybox";
  options.filenameSuffix = ".settings";
#if JUCE_MAC
  options.folderName = "mnkybox";
  options.osxLibrarySubFolder = "Application Support";
#else
  options.folderName = ".config/mnkybox";
#endif
  properties_ = std::make_unique<juce::PropertiesFile>(options);

  choice_.backend =
      audioBackendFromKey(properties_->getValue("audio/backend", "default").toStdString());
  choice_.deviceName = properties_->getValue("audio/device", juce::String()).toStdString();
  choice_.bufferSize =
      clampAudioBufferSize(properties_->getIntValue("audio/bufferFrames", kDefaultAudioBufferSize));
  log("audio: properties " + properties_->getFile().getFullPathName() + "; " + describeChoice());
}

void AudioGlue::saveAudioChoice() {
  if (properties_ == nullptr) return;
  properties_->setValue("audio/backend",
                        juce::String(std::string(audioBackendKey(choice_.backend))));
  properties_->setValue("audio/device", juce::String(choice_.deviceName));
  properties_->setValue("audio/bufferFrames", choice_.bufferSize);
  (void)properties_->saveIfNeeded();
}

juce::String AudioGlue::describeChoice() const {
  return juce::String(audioBackendLabel(choice_.backend)) + " / " +
         (choice_.deviceName.empty() ? juce::String("default device")
                                     : juce::String(choice_.deviceName)) +
         " / " + juce::String(choice_.bufferSize) + " frames";
}

AudioBackendChoice AudioGlue::audioChoice() const {
  return choice_;
}

AudioBackendAvailability AudioGlue::backendAvailability() const {
  return availability_;
}

void AudioGlue::rescanBackends() {
  availability_ = {};
  for (auto* type : deviceManager_.getAvailableDeviceTypes()) {
    type->scanForDevices();
    const bool hasOutput = !type->getDeviceNames(false).isEmpty();
    if (type->getTypeName() == juce::String(std::string(audioDeviceTypeName(AudioBackend::Alsa)))) {
      availability_.alsa = hasOutput;
    } else if (type->getTypeName() ==
               juce::String(std::string(audioDeviceTypeName(AudioBackend::Jack)))) {
      availability_.jack = hasOutput;
    }
  }

#if defined(MNKY_HAVE_JACK)
  const juce::String jackHint("no JACK device type or no jackd server");
#else
  const juce::String jackHint("JACK was not built in (no jack headers at configure time)");
#endif
  log("audio: backends ALSA=" + yesNo(availability_.alsa) + " JACK=" + yesNo(availability_.jack) +
      (availability_.jack ? juce::String() : "; " + jackHint));
}

juce::StringArray AudioGlue::outputDeviceNames(AudioBackend backend) {
  // getAvailableDeviceTypes() creates and scans the types (and settles on a
  // default type the first time), so the current type is only read afterwards.
  const auto& types = deviceManager_.getAvailableDeviceTypes();
  const auto wanted = backend == AudioBackend::Default
                          ? platformDefaultDeviceType()
                          : juce::String(std::string(audioDeviceTypeName(backend)));
  for (auto* type : types) {
    if (type->getTypeName() != wanted) continue;
    type->scanForDevices();
    return type->getDeviceNames(false);
  }
  return {};
}

juce::String AudioGlue::platformDefaultDeviceType() {
  // JUCE registers its device types in platform order and picks the first one
  // with devices; that is the type "Default" means on a fresh start.
  const auto& types = deviceManager_.getAvailableDeviceTypes();
  for (auto* type : types) {
    type->scanForDevices();
    if (!type->getDeviceNames(false).isEmpty() || !type->getDeviceNames(true).isEmpty()) {
      return type->getTypeName();
    }
  }
  return {};
}

juce::String AudioGlue::resolveDeviceName(AudioBackend backend, const std::string& stored) {
  if (stored.empty()) return {};
  const auto names = outputDeviceNames(backend);
  std::vector<std::string> available;
  available.reserve(static_cast<std::size_t>(names.size()));
  for (const auto& name : names) available.push_back(name.toStdString());
  return juce::String(resolveAudioDeviceName(stored, available));
}

juce::String AudioGlue::applyAudioChoice(const AudioBackendChoice& choice) {
  choice_ = choice;
  choice_.bufferSize = clampAudioBufferSize(choice.bufferSize);
  saveAudioChoice();
  note_.clear();

  const auto selection = resolveAudioBackend(choice_.backend, availability_);
  if (selection.fellBack) note_ = selection.note;
  log("audio: apply " + describeChoice() + " -> " +
      juce::String(audioBackendLabel(selection.effective)));

  if (emulatorStartup_ && selection.effective == AudioBackend::Default) {
    // --emulator keeps the generated audio path for the default backend; the
    // explicit ALSA/JACK requests below still open a real device.
    closeDevice();
    startEmulator({});
    log("audio: " + backend_);
    return {};
  }

  const auto deviceName = selection.fellBack
                              ? juce::String()
                              : resolveDeviceName(selection.effective, choice_.deviceName);
  const auto error = openDevice(selection.effective, deviceName, choice_.bufferSize);
  if (error.isNotEmpty()) {
    closeDevice();
    note_ = "device open failed: " + error + "; using the emulator generator";
    startEmulator(note_);
    log("audio: " + note_);
    return error;
  }

  const auto wantedType = selection.effective == AudioBackend::Default
                              ? platformDefaultDeviceType()
                              : juce::String(std::string(audioDeviceTypeName(selection.effective)));
  const auto liveType = deviceManager_.getCurrentAudioDeviceType();
  if (wantedType.isNotEmpty() && wantedType != liveType && note_.isEmpty()) {
    note_ = "device manager opened " + liveType + " instead of " + wantedType;
  }
  if (note_.isNotEmpty()) log("audio: " + note_);
  log("audio: " + backend_);
  return {};
}

AudioBackendState AudioGlue::backendState() const {
  AudioBackendState state;
  state.choice = choice_;
  state.availability = availability_;
  const auto selection = resolveAudioBackend(choice_.backend, availability_);
  state.effective = selection.effective;
  state.note = note_.isNotEmpty() ? note_ : juce::String(selection.note);

  // A device object can outlive its server/device (JACK shutdown, unplugged
  // card), so "live" means open and reporting non-zero numbers, not "present".
  auto* device = deviceManager_.getCurrentAudioDevice();
  const bool deviceLive = device != nullptr && device->isOpen() &&
                          device->getCurrentSampleRate() > 0.0 &&
                          device->getCurrentBufferSizeSamples() > 0;
  if (deviceLive) {
    state.liveType = device->getTypeName();
    state.liveDevice = device->getName();
    state.sampleRate = device->getCurrentSampleRate();
    state.blockSize = device->getCurrentBufferSizeSamples();
    state.xruns = deviceManager_.getXRunCount();
  } else if (emulated()) {
    state.liveType = "emulator";
    state.sampleRate = sampleRate();
    state.blockSize = blockSize();
  } else {
    state.liveType = "none";
  }
  return state;
}

juce::String AudioGlue::backendDescription() const {
  // The last successful open stays useful, but a device that has shut down must
  // not read as running in the System summary.
  if (!emulated() && !running_.load()) return backend_ + " (stopped)";
  return backend_;
}

double AudioGlue::sampleRate() const {
  return sampleRate_.load();
}

int AudioGlue::blockSize() const {
  return blockSize_.load();
}

bool AudioGlue::startTake(const juce::File& directory) {
  const auto mask = mixer_.armMask();
  if (mask == 0) {
    log("recorder: no armed channel (arm at least one strip)");
    return false;
  }
  const auto rate = static_cast<std::uint32_t>(sampleRate());
  if (!recorder_.startTake(directory.getFullPathName().toStdString(), mask, rate)) {
    log("recorder: startTake failed for " + directory.getFullPathName());
    return false;
  }
  log("recorder: take started in " + directory.getFullPathName() + " (" +
      juce::String(static_cast<int>(mask)) + " arm mask, " +
      juce::String(static_cast<int>(rate)) + " Hz)");
  return true;
}

audio::Recorder::Take AudioGlue::stopTake() {
  auto take = recorder_.stopTake();
  {
    const juce::ScopedLock lock(takeLock_);
    lastTake_ = take;
  }
  log("recorder: take " + juce::String(take.path) + ", " + juce::String(take.files) +
      " files, " + juce::String(static_cast<juce::int64>(take.frames)) + " frames, " +
      juce::String(static_cast<juce::int64>(take.drops)) + " dropped blocks");
  return take;
}

bool AudioGlue::recording() const {
  return recorder_.recording();
}

std::uint64_t AudioGlue::drops() const {
  return recorder_.drops();
}

audio::Recorder::Take AudioGlue::lastTake() const {
  const juce::ScopedLock lock(takeLock_);
  return lastTake_;
}

float AudioGlue::peak(int channel) {
  const int index = channel == 1 ? 1 : 0;
  const float value = peaks_[index].load(std::memory_order_relaxed);
  peaks_[index].store(value * 0.82f, std::memory_order_relaxed);
  return value;
}

void AudioGlue::audioDeviceAboutToStart(juce::AudioIODevice* device) {
  if (device == nullptr) return;
  sampleRate_.store(device->getCurrentSampleRate() > 0.0 ? device->getCurrentSampleRate()
                                                         : 48000.0);
  blockSize_.store(device->getCurrentBufferSizeSamples());
  synth_.prepare(sampleRate());
  running_.store(true);
  log("audio: callback started, " + juce::String(sampleRate(), 1) + " Hz / " +
      juce::String(blockSize()) + " frames");
}

void AudioGlue::audioDeviceStopped() {
  running_.store(false);
  log("audio: callback stopped");
}

void AudioGlue::audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
                                                 int numInputChannels,
                                                 float* const* outputChannelData,
                                                 int numOutputChannels, int numSamples,
                                                 const juce::AudioIODeviceCallbackContext&) {
  if (outputChannelData == nullptr || numOutputChannels <= 0 || numSamples <= 0) return;

  for (int channel = 0; channel < numOutputChannels; ++channel) {
    if (outputChannelData[channel] != nullptr) {
      juce::FloatVectorOperations::clear(outputChannelData[channel], numSamples);
    }
  }
  if (numOutputChannels < 2 || outputChannelData[0] == nullptr ||
      outputChannelData[1] == nullptr) {
    return;
  }

  renderBlock(inputChannelData, numInputChannels, outputChannelData[0],
              outputChannelData[1], numSamples);
}

void AudioGlue::renderBlock(const float* const* inputChannelData, int numInputChannels,
                            float* outL, float* outR, int numSamples) {
  if (outL == nullptr || outR == nullptr || numSamples <= 0) return;

  // One read per block: the backend can change on the message thread while a
  // callback is still draining.
  const bool generated = emulated_.load();

  int offset = 0;
  while (offset < numSamples) {
    const int frames = std::min(audio::kBlockSize, numSamples - offset);
    const float* inputs[audio::kMaxInputs] = {};

    if (generated) {
      float* generatedChannels[audio::kMaxInputs] = {};
      for (int channel = 0; channel < audio::kMaxInputs; ++channel) {
        generatedChannels[channel] = synthBuffer_[channel];
        inputs[channel] = synthBuffer_[channel];
      }
      synth_.fillBlock(generatedChannels, audio::kMaxInputs, frames);
    } else {
      const int count = juce::jlimit(0, audio::kMaxInputs, numInputChannels);
      for (int channel = 0; channel < count; ++channel) {
        if (inputChannelData != nullptr) inputs[channel] = inputChannelData[channel];
      }
    }

    mixer_.applyTo(engine_);
    for (int send = 0; send < audio::kNumSends; ++send) {
      engine_.setFx(send, fx_.effect(send));
    }
    engine_.process(inputs, generated ? audio::kMaxInputs : numInputChannels, frames,
                    outL + offset, outR + offset);

    if (recorder_.recording()) {
      const float* taps[audio::kMaxInputs] = {};
      for (int channel = 0; channel < audio::kMaxInputs; ++channel) {
        taps[channel] = engine_.tap(channel);
      }
      recorder_.writeBlock(taps, outL + offset, outR + offset, frames);
    }

    // Metronome after the recorder tap: the click is monitoring, not take
    // material. The shim renders silence until audio::ClickSource lands; the
    // enabled/level/tick/rolling wiring is the real contract.
    if (state_.clickEnabled.load(std::memory_order_relaxed)) {
      click_.setEnabled(true);
      click_.setLevel(state_.clickLevel.load(std::memory_order_relaxed));
      float clickBuffer[audio::kBlockSize] = {};
      click_.render(clickBuffer, frames, state_.tick.load(std::memory_order_relaxed),
                    static_cast<std::uint32_t>(kTicksPerBeat),
                    state_.rolling.load(std::memory_order_relaxed));
      for (int i = 0; i < frames; ++i) {
        outL[offset + i] += clickBuffer[i];
        outR[offset + i] += clickBuffer[i];
      }
    } else {
      click_.setEnabled(false);
    }

    offset += frames;
  }

  updatePeaks(outL, outR, numSamples);
}

void AudioGlue::updatePeaks(const float* outL, const float* outR, int numSamples) {
  float left = 0.0f;
  float right = 0.0f;
  for (int i = 0; i < numSamples; ++i) {
    left = std::max(left, std::abs(outL[i]));
    right = std::max(right, std::abs(outR[i]));
  }
  for (int channel = 0; channel < 2; ++channel) {
    const float blockPeak = channel == 0 ? left : right;
    const float previous = peaks_[channel].load(std::memory_order_relaxed);
    peaks_[channel].store(std::max(previous, blockPeak), std::memory_order_relaxed);
  }
}

}  // namespace mnky::app

#include "audio_glue.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <exception>

#include <QSettings>

#include "../midi/usb.hpp"
#include "app_state.hpp"
#include "fx_params.hpp"
#include "logic.hpp"
#include "logging.hpp"

namespace mnky::app {

static_assert(kDefaultAudioBufferSize == audio::kBlockSize,
              "the default buffer size offered by the System screen is the engine block size");

namespace {

std::string deviceName(const audio::DeviceInfo& device) {
  return device.name + " (" + audio::deviceApiName(device.api) + ", " +
         (device.channels > 0 ? std::to_string(device.channels) + " ch" : "unknown width") + ")";
}

std::string joinDeviceNames(const std::vector<audio::DeviceInfo>& devices) {
  std::string joined;
  for (const auto& device : devices) {
    if (!joined.empty()) joined += ", ";
    joined += device.name + " [" + audio::deviceApiName(device.api) + "]";
  }
  return joined;
}

}  // namespace

// Clocked stand-in for an audio device: pushes kBlockSize frames through the
// same render path at 48 kHz so the mixer, recorder and meters behave as they
// do with a real interface. Wall-clock driven, not sample-accurate.
class AudioGlue::EmulatorDriver final {
 public:
  explicit EmulatorDriver(AudioGlue& owner) : owner_(owner) {}
  ~EmulatorDriver() { stop(); }

  void start() {
    if (thread_.joinable()) return;
    exit_.store(false);
    thread_ = std::thread([this] { run(); });
  }
  void stop() {
    exit_.store(true);
    if (thread_.joinable()) thread_.join();
  }

 private:
  void run() {
    constexpr int block = audio::kBlockSize;
    const double sampleRate = owner_.sampleRate();
    const double intervalMs =
        sampleRate > 0.0 ? 1000.0 * static_cast<double>(block) / sampleRate : 2.6667;

    auto next = std::chrono::steady_clock::now();
    while (!exit_.load(std::memory_order_relaxed)) {
      owner_.renderEmulatorBlock(block);
      next += std::chrono::microseconds(static_cast<long long>(intervalMs * 1000.0));
      const auto now = std::chrono::steady_clock::now();
      if (next > now) {
        std::this_thread::sleep_for(next - now);
      } else if (now - next > std::chrono::milliseconds(50)) {
        next = now;  // fell far behind (debugger, sleep): resync, do not burst
      }
    }
  }

  AudioGlue& owner_;
  std::atomic<bool> exit_{false};
  std::thread thread_;
};

AudioGlue::AudioGlue(MixerModel& mixer, FxManager& fx, PerformanceModel& performance,
                     AppState& state, bool emulator)
    : mixer_(mixer),
      fx_(fx),
      performance_(performance),
      state_(state),
      emulatorStartup_(emulator),
      emulated_(emulator) {
  // Register the CLAP adapters once: each send bus is builtin chain -> CLAP.
  for (int send = 0; send < audio::kNumSends; ++send) {
    performance_.setClapEffect(send, fx_.effect(send));
  }
  for (auto& peak : peaks_) peak.store(0.0f);
  for (auto& peak : stripPeaks_) peak.store(0.0f);
  for (auto& source : monitorSend_) source.store(1);
  // The rack is control-thread state: every edit forwards one slot to the
  // engine here instead of from the audio callback (see MixerModel::setFxSlot).
  mixer_.setFxSlotSink([this](int track, int slot) { applyTrackFxSlot(track, slot); });
  synth_.prepare(sampleRate());
  pump_.reset();
  loadAudioChoice();
}

AudioGlue::~AudioGlue() {
  mixer_.setFxSlotSink({});
  stop();
}

bool AudioGlue::start() {
  note_.clear();
  rescanBackends();

  if (emulatorStartup_) {
    startEmulator("emulator audio (--emulator); apply an explicit audio device to open "
                  "real audio");
    log("audio: " + backend_);
    return true;
  }

  const auto selection =
      resolveAudioBackend(choice_.backend, choice_.outputDeviceId, availability_);
  if (selection.fellBack) {
    note_ = selection.note;
    log("audio: " + note_);
  }
  if (selection.effective == AudioBackend::Emulator) {
    startEmulator(note_);
    log("audio: " + backend_);
    return false;
  }
  const auto error =
      openBackend(selection.effective, choice_.outputDeviceId, choice_.inputDeviceId,
                  choice_.bufferSize);
  if (!error.empty()) {
    closeAudio();
    note_ = "no audio device (" + error + "); using the emulator generator";
    startEmulator(note_);
    log("audio: " + backend_);
    return false;
  }
  log("audio: " + backend_);
  return true;
}

void AudioGlue::stop() {
  const bool wasRunning = running_.load();
  const std::uint64_t blocks = renderBlocks_.load(std::memory_order_relaxed);
  const std::uint64_t xruns = device_ != nullptr ? device_->xruns() : 0;
  stopEmulator();
  closeAudio();
  if (wasRunning) {
    log("audio: stopped after " + std::to_string(blocks) + " engine blocks, " +
        std::to_string(xruns) + " device xruns");
  }
  if (recorder_.recording()) {
    const auto take = recorder_.stopTake();
    const std::lock_guard<std::mutex> lock(takeLock_);
    lastTake_ = take;
  }
  running_.store(false);
}

void AudioGlue::startEmulator(const std::string& reason) {
  closeAudio();
  emulated_.store(true);
  deviceName_.clear();
  inputName_.clear();
  backend_ = "emulator: sine/noise generator, 48 kHz / 128 frames";
  if (!reason.empty()) note_ = reason;
  synth_.prepare(sampleRate());
  driver_ = std::make_unique<EmulatorDriver>(*this);
  driver_->start();
  running_.store(true);
}

void AudioGlue::stopEmulator() {
  if (driver_ != nullptr) {
    driver_->stop();
    driver_.reset();
  }
  emulated_.store(false);
}

void AudioGlue::closeAudio() {
  if (device_ != nullptr) {
    device_->stop();
    device_->close();
    device_.reset();
  }
  deviceName_.clear();
  inputName_.clear();
  devicePeriodFrames_ = 0;
  outputChannels_.store(0);
  inputChannels_.store(0);
  running_.store(false);
  emulated_.store(false);
  pump_.reset();
}

std::string AudioGlue::openBackend(AudioBackend backend, const std::string& outputDeviceId,
                                   const std::string& inputDeviceId, int bufferSize) {
  stopEmulator();
  closeAudio();
  note_.clear();

  audio::DeviceApi api = audio::DeviceApi::None;
  std::string outputNode;
  switch (backend) {
    case AudioBackend::PipeWire:
    case AudioBackend::Alsa:
      api = backend == AudioBackend::PipeWire ? audio::DeviceApi::PipeWire
                                              : audio::DeviceApi::Alsa;
      // An explicit device id may pin one node/card while the API stays
      // selected; an id from the other API is ignored and the API default is
      // opened instead.
      if (!outputDeviceId.empty()) {
        audio::DeviceApi deviceApi = audio::DeviceApi::None;
        std::string node;
        if (audio::parseDeviceId(outputDeviceId, &deviceApi, &node) && deviceApi == api) {
          outputNode = node;
        } else {
          note_ = "output device ignored: it belongs to a different audio API";
          log("audio: " + note_);
        }
      }
      break;
    case AudioBackend::Default:
      switch (defaultOutputBackend(availability_)) {
        case AudioBackend::PipeWire:
          api = audio::DeviceApi::PipeWire;
          break;
        case AudioBackend::Alsa:
          api = audio::DeviceApi::Alsa;
          break;
        default:
          api = audio::DeviceApi::None;
          break;
      }
      break;
    case AudioBackend::Device:
      if (!audio::parseDeviceId(outputDeviceId, &api, &outputNode)) {
        return "device id \"" + outputDeviceId + "\" has no audio API prefix";
      }
      break;
    case AudioBackend::Emulator:
      return "the emulator is not a device backend";
  }
  if (api == audio::DeviceApi::None) return "no audio API is available on this host";
  if (!audio::deviceApiCompiled(api)) {
    return std::string(audio::deviceApiName(api)) + " is not compiled into this build";
  }

  std::string inputNode;
  if (!inputDeviceId.empty()) {
    audio::DeviceApi inputApi = audio::DeviceApi::None;
    std::string parsedInput;
    if (!audio::parseDeviceId(inputDeviceId, &inputApi, &parsedInput) || inputApi != api) {
      note_ = "input device ignored: it belongs to a different audio API";
      log("audio: " + note_);
    } else {
      inputNode = parsedInput;
    }
  }

  audio::DeviceConfig config;
  config.sampleRate = 48000.0;
  config.blockFrames = clampAudioBufferSize(bufferSize);
  config.streamName = "mnkybox";
  config.outputId = outputNode;
  config.inputId = inputNode;
  config.outputChannels = outputChannelsFor(outputNode);
  if (api == audio::DeviceApi::PipeWire && config.outputChannels <= 0) {
    // A PipeWire stream can always ask for the full width: the graph converts
    // down to a narrower sink, and an explicit UMC node negotiates its own 20.
    config.outputChannels = audio::kMaxDeviceOutputs;
  }
  config.inputChannels = inputChannelsFor(inputNode);
  if (api == audio::DeviceApi::PipeWire && !inputNode.empty() && config.inputChannels <= 0) {
    config.inputChannels = audio::kMaxDeviceInputs;
  }
  // PipeWire links channel by channel through the node's positions. Without
  // them a multichannel interface is opened stereo-first (the Qt/Pulse bug);
  // the backend falls back to AUX0.. when the node does not report positions.
  config.outputPositions = outputPositionsFor(outputNode);
  config.inputPositions = inputPositionsFor(inputNode);

  std::string error;
  auto created = audio::createBackend(api, &error);
  if (created == nullptr) return error;
  if (!created->open(config, this, &error)) {
    created->close();
    return error.empty() ? "audio device open failed" : error;
  }
  if (!created->start(&error)) {
    created->close();
    return error.empty() ? "audio device start failed" : error;
  }
  device_ = std::move(created);
  sampleRate_.store(device_->sampleRate());
  const int period = device_->periodFrames();
  blockSize_.store(period > 0 ? period : audio::kBlockSize);
  devicePeriodFrames_ = blockSize_.load();
  outputChannels_.store(device_->outputChannels());
  inputChannels_.store(device_->inputChannels());
  running_.store(true);
  performance_.setSampleRate(sampleRate());
  engine_.setSampleRate(sampleRate());
  deviceName_ = deviceNameFor(outputDeviceId);
  if (deviceName_.empty()) deviceName_ = device_->description();
  inputName_ = inputDeviceId.empty() ? std::string() : deviceNameFor(inputDeviceId);
  backend_ = device_->description();
  if (!inputName_.empty()) backend_ += " / input " + inputName_;
  if (static_cast<int>(std::lround(sampleRate())) != 48000) {
    note_ = "device runs at " + std::to_string(static_cast<int>(sampleRate())) +
            " Hz instead of the requested 48000 Hz";
  }
  if (devicePeriodFrames_ != clampAudioBufferSize(bufferSize) && note_.empty()) {
    note_ = "device period is " + std::to_string(devicePeriodFrames_) +
            " frames instead of the requested " +
            std::to_string(clampAudioBufferSize(bufferSize));
  }
  if (device_->inputChannels() == 0 && !inputDeviceId.empty() && note_.empty()) {
    note_ = "input device did not open; capture is silent";
  }
  return {};
}

int AudioGlue::outputChannelsFor(const std::string& node) const {
  if (node.empty()) return 0;
  for (const auto& device : inventory_.outputs) {
    if (device.node == node && device.channels > 0) return device.channels;
  }
  return 0;
}

int AudioGlue::inputChannelsFor(const std::string& node) const {
  if (node.empty()) return 0;
  for (const auto& device : inventory_.inputs) {
    if (device.node == node && device.channels > 0) return device.channels;
  }
  return 0;
}

std::vector<std::string> AudioGlue::outputPositionsFor(const std::string& node) const {
  if (node.empty()) return {};
  for (const auto& device : inventory_.outputs) {
    if (device.node == node) return device.positions;
  }
  return {};
}

std::vector<std::string> AudioGlue::inputPositionsFor(const std::string& node) const {
  if (node.empty()) return {};
  for (const auto& device : inventory_.inputs) {
    if (device.node == node) return device.positions;
  }
  return {};
}

std::string AudioGlue::deviceNameFor(const std::string& id) const {
  for (const auto& device : inventory_.outputs) {
    if (device.id == id) return device.name;
  }
  for (const auto& device : inventory_.inputs) {
    if (device.id == id) return device.name;
  }
  return {};
}

void AudioGlue::loadAudioChoice() {
  QSettings settings;
  choice_.backend =
      audioBackendFromKey(settings.value(QStringLiteral("audio/backend"), QStringLiteral("default"))
                              .toString()
                              .toStdString());
  choice_.outputDeviceId =
      settings.value(QStringLiteral("audio/outputDevice"), QString()).toString().toStdString();
  choice_.inputDeviceId =
      settings.value(QStringLiteral("audio/inputDevice"), QString()).toString().toStdString();
  choice_.bufferSize = clampAudioBufferSize(
      settings.value(QStringLiteral("audio/bufferFrames"), kDefaultAudioBufferSize).toInt());
  log("audio: settings " + settings.fileName().toStdString() + "; " +
      audioBackendLabel(choice_.backend) + " / " +
      (choice_.outputDeviceId.empty() ? "default device" : choice_.outputDeviceId) + " / " +
      std::to_string(choice_.bufferSize) + " frames");
}

void AudioGlue::saveAudioChoice() {
  QSettings settings;
  settings.setValue(QStringLiteral("audio/backend"),
                    QString::fromStdString(std::string(audioBackendKey(choice_.backend))));
  settings.setValue(QStringLiteral("audio/outputDevice"),
                    QString::fromStdString(choice_.outputDeviceId));
  settings.setValue(QStringLiteral("audio/inputDevice"),
                    QString::fromStdString(choice_.inputDeviceId));
  settings.setValue(QStringLiteral("audio/bufferFrames"), choice_.bufferSize);
}

void AudioGlue::rescanBackends() {
  inventory_ = audio::enumerateDevices();
  availability_ = {};
  availability_.pipewireOutput = inventory_.pipewireOutput;
  availability_.pipewireInput = inventory_.pipewireInput;
  availability_.alsaOutput = inventory_.alsaOutput;
  availability_.alsaInput = inventory_.alsaInput;
  availability_.defaultOutput = inventory_.pipewireOutput || inventory_.alsaOutput;
  for (const auto& device : inventory_.outputs) {
    availability_.outputDeviceIds.push_back(device.id);
  }
  for (const auto& device : inventory_.inputs) {
    availability_.inputDeviceIds.push_back(device.id);
  }
  // Real per-device USB MIDI names for the device list: RtMidi port names
  // grouped by client instead of one generic "USB" label. MidiGlue owns the
  // actual ports; this probe only enumerates.
  try {
    midi::UsbMidi probe;
    availability_.midiInputNames = midiDeviceNamesFromPortNames(probe.listInputs());
    availability_.midiOutputNames = midiDeviceNamesFromPortNames(probe.listOutputs());
  } catch (const std::exception& error) {
    log(std::string("audio: USB MIDI enumeration failed: ") + error.what());
  }
  log("audio: devices default=" + std::string(availability_.defaultOutput ? "yes" : "no") +
      " pipewire out=" + std::to_string(inventory_.pipewireOutput ? 1 : 0) + " in=" +
      std::to_string(inventory_.pipewireInput ? 1 : 0) + " [" +
      joinDeviceNames(inventory_.outputs) + "] alsa out=" +
      std::to_string(inventory_.alsaOutput ? 1 : 0) + " in=" +
      std::to_string(inventory_.alsaInput ? 1 : 0) + " midi in=[" +
      [&] {
        std::string joined;
        for (const auto& name : availability_.midiInputNames) {
          if (!joined.empty()) joined += ", ";
          joined += name;
        }
        return joined;
      }() +
      "] midi out=[" +
      [&] {
        std::string joined;
        for (const auto& name : availability_.midiOutputNames) {
          if (!joined.empty()) joined += ", ";
          joined += name;
        }
        return joined;
      }() +
      "]");
}

AudioBackendAvailability AudioGlue::backendAvailability() const {
  return availability_;
}

std::vector<AudioDeviceOption> AudioGlue::outputDevices() const {
  std::vector<AudioDeviceOption> options;
  for (const auto& device : inventory_.outputs) {
    AudioDeviceOption option;
    option.id = device.id;
    option.name = deviceName(device);
    option.isDefault = device.isDefault;
    options.push_back(std::move(option));
  }
  return options;
}

std::vector<AudioDeviceOption> AudioGlue::inputDevices() const {
  std::vector<AudioDeviceOption> options;
  for (const auto& device : inventory_.inputs) {
    AudioDeviceOption option;
    option.id = device.id;
    option.name = deviceName(device);
    option.isDefault = device.isDefault;
    options.push_back(std::move(option));
  }
  return options;
}

std::string AudioGlue::applyAudioChoice(const AudioBackendChoice& choice) {
  choice_ = choice;
  choice_.bufferSize = clampAudioBufferSize(choice.bufferSize);
  saveAudioChoice();
  note_.clear();
  // Release the device before enumerating: an ALSA hw PCM cannot be probed
  // while this process owns it, and a stale channel count would drop the
  // device width from the list.
  closeAudio();
  rescanBackends();

  const auto selection =
      resolveAudioBackend(choice_.backend, choice_.outputDeviceId, availability_);
  if (selection.fellBack) note_ = selection.note;
  log("audio: apply " + audioBackendLabel(choice_.backend) + " / " +
      (choice_.outputDeviceId.empty() ? "default device" : choice_.outputDeviceId) + " / " +
      std::to_string(choice_.bufferSize) + " frames -> " +
      audioBackendLabel(selection.effective));

  if (emulatorStartup_ && selection.effective == AudioBackend::Default) {
    // --emulator keeps the generated audio path for the default backend; the
    // explicit device requests below still open real audio.
    startEmulator({});
    log("audio: " + backend_);
    return {};
  }
  if (selection.effective == AudioBackend::Emulator) {
    startEmulator(note_);
    log("audio: " + backend_);
    return {};
  }

  const auto error = openBackend(selection.effective, choice_.outputDeviceId,
                                 choice_.inputDeviceId, choice_.bufferSize);
  if (!error.empty()) {
    closeAudio();
    note_ = "device open failed: " + error + "; using the emulator generator";
    startEmulator(note_);
    log("audio: " + note_);
    return error;
  }
  log("audio: " + backend_);
  return {};
}

AudioBackendState AudioGlue::backendState() const {
  AudioBackendState state;
  state.choice = choice_;
  state.availability = availability_;
  const auto selection =
      resolveAudioBackend(choice_.backend, choice_.outputDeviceId, availability_);
  state.effective = selection.effective;
  state.note = !note_.empty() ? note_ : selection.note;

  if (emulated()) {
    state.liveType = "emulator";
    state.liveDevice = "generator";
    state.sampleRate = sampleRate();
    state.blockSize = blockSize();
  } else if (device_ != nullptr && running_.load()) {
    state.liveType = audio::deviceApiName(device_->api());
    state.liveDevice = deviceName_;
    state.sampleRate = sampleRate();
    state.blockSize = device_->periodFrames() > 0 ? device_->periodFrames() : blockSize();
    state.outputChannels = outputChannels_.load();
    state.inputChannels = inputChannels_.load();
    state.callbackBlocks = renderBlocks_.load(std::memory_order_relaxed);
    state.xruns = device_->xruns();
  } else {
    state.liveType = "none";
  }
  return state;
}

std::string AudioGlue::backendDescription() const {
  if (!emulated() && !running_.load()) return backend_ + " (stopped)";
  return backend_;
}

bool AudioGlue::startTake(const std::filesystem::path& directory) {
  const auto mask = mixer_.armMask();
  if (mask == 0) {
    log("recorder: no armed channel (arm at least one strip)");
    return false;
  }
  const auto rate = static_cast<std::uint32_t>(sampleRate());
  if (!recorder_.startTake(directory.string(), mask, rate)) {
    log("recorder: startTake failed for " + directory.string());
    return false;
  }
  log("recorder: take started in " + directory.string() + " (" +
      std::to_string(mask) + " arm mask, " + std::to_string(rate) + " Hz)");
  return true;
}

audio::Recorder::Take AudioGlue::stopTake() {
  auto take = recorder_.stopTake();
  {
    const std::lock_guard<std::mutex> lock(takeLock_);
    lastTake_ = take;
  }
  log("recorder: take " + take.path + ", " + std::to_string(take.files) + " files, " +
      std::to_string(take.frames) + " frames, " + std::to_string(take.drops) +
      " dropped blocks");
  return take;
}

bool AudioGlue::recording() const {
  return recorder_.recording();
}

std::uint64_t AudioGlue::drops() const {
  return recorder_.drops();
}

audio::Recorder::Take AudioGlue::lastTake() const {
  const std::lock_guard<std::mutex> lock(takeLock_);
  return lastTake_;
}

float AudioGlue::peak(int channel) {
  const int index = channel == 1 ? 1 : 0;
  const float value = peaks_[index].load(std::memory_order_relaxed);
  peaks_[index].store(value * 0.82f, std::memory_order_relaxed);
  return value;
}

float AudioGlue::stripPeak(int channel) {
  if (channel < 0 || channel >= audio::kMaxTracks) return 0.0f;
  const auto index = static_cast<std::size_t>(channel);
  const float value = stripPeaks_[index].load(std::memory_order_relaxed);
  stripPeaks_[index].store(value * 0.82f, std::memory_order_relaxed);
  return value;
}

void AudioGlue::applyMonitorSettings(const SessionSettings& settings) {
  monitorPhones_.store(settings.cue.phonesSrc);
  monitorCueStyle_.store(settings.cue.style);
  monitorSend_[static_cast<std::size_t>(audio::SendDestination::Cue)].store(
      settings.sends.cue);
  monitorSend_[static_cast<std::size_t>(audio::SendDestination::Fx1)].store(
      settings.sends.fx1);
  monitorSend_[static_cast<std::size_t>(audio::SendDestination::Fx2)].store(
      settings.sends.fx2);
  monitorSend_[static_cast<std::size_t>(audio::SendDestination::Out34)].store(
      settings.sends.out34);
  monitorSend_[static_cast<std::size_t>(audio::SendDestination::Rec)].store(
      settings.sends.rec);
  compOn_.store(settings.mainComp.on);
  compThreshDb_.store(settings.mainComp.threshDb);
  compRatio_.store(settings.mainComp.ratio);
  compAttackMs_.store(settings.mainComp.attackMs);
  compReleaseMs_.store(settings.mainComp.releaseMs);
  compMakeupDb_.store(settings.mainComp.makeupDb);
  compMode_.store(settings.mainComp.mode);
}

void AudioGlue::applyTakeLoops(const SessionSettings& settings) {
  for (int track = 0; track < audio::kMaxTracks; ++track) {
    const auto& loop = settings.takeLoop[static_cast<std::size_t>(track)];
    audio::TakeLoop window;
    window.enabled = loop.enabled;
    window.startBeats = loop.startBeats;
    window.endBeats = loop.endBeats;
    engine_.setTakeLoop(track, window);
  }
}

void AudioGlue::applyTrackFxSlot(int track, int slot) {
  if (track < 0 || track >= audio::kMaxTracks || slot < 0 ||
      slot >= MixerModel::kFxSlotCount) {
    return;
  }
  const auto value = mixer_.fxSlot(track, slot);
  audio::TrackFxSlot next;
  next.id = value.id;
  next.bypass = value.bypass;
  next.wet = value.wet;
  next.params = value.params;
  const bool clap = !value.id.empty() && !fxIsBuiltin(value.id);
  if (!clap) {
    // Clear the effect state first so the audio thread stops selecting the
    // external branch, then detach and destroy the plugin instance. FxManager
    // keeps the tiny wrapper so a callback that already loaded the adapter
    // cannot dereference freed memory.
    engine_.setTrackFxSlot(track, slot, next);
    engine_.setTrackFxSlotEffect(track, slot, nullptr);
    fx_.unloadTrackEffect(track, slot);
    return;
  }

  const std::string currentId = engine_.trackFxSlot(track, slot).id;
  if (currentId != value.id) {
    // Selection/switch: detach the old instance before the engine publishes
    // the new id, then attach the new adapter once it is loaded and active.
    engine_.setTrackFxSlotEffect(track, slot, nullptr);
  }
  engine_.setTrackFxSlot(track, slot, next);
  if (!fx_.loadTrackEffect(track, slot, value.id, sampleRate(), audio::kBlockSize)) {
    engine_.setTrackFxSlotEffect(track, slot, nullptr);
    log("track fx " + std::to_string(track) + "/" + std::to_string(slot) +
        ": " + value.id + " not loaded - the slot passes the strip dry");
    return;
  }
  // The session vector is the slot's parameter contract; apply every value in
  // the plugin's own stable order. The CLAP host latches writes per block.
  const auto params = fx_.trackParams(track, slot);
  const std::size_t count = std::min(params.size(), value.params.size());
  for (std::size_t index = 0; index < count; ++index) {
    (void)fx_.setTrackParam(track, slot, index, value.params[index]);
  }
  engine_.setTrackFxSlotEffect(track, slot, fx_.trackEffect(track, slot));
}

void AudioGlue::applyTrackFxRack() {
  for (int track = 0; track < audio::kMaxTracks; ++track) {
    for (int slot = 0; slot < MixerModel::kFxSlotCount; ++slot) {
      applyTrackFxSlot(track, slot);
    }
  }
}

void AudioGlue::applyEq(const session::EqState& state) {
  // The EQ store is the session's: iterate its own width so a later widening
  // needs no change here (and a narrower store is never over-read).
  for (std::size_t track = 0; track < state.tracks.size(); ++track) {
    for (int band = 0; band < 4; ++band) {
      const auto& source = state.tracks[track][static_cast<std::size_t>(band)];
      audio::EqBand value;
      value.type = source.type;
      value.gainDb = source.gainDb;
      value.freqHz = source.freqHz;
      value.q = source.q;
      engine_.setTrackEqBand(static_cast<int>(track), band, value);
    }
  }
  for (int band = 0; band < 4; ++band) {
    const auto& source = state.main[static_cast<std::size_t>(band)];
    audio::EqBand value;
    value.type = source.type;
    value.gainDb = source.gainDb;
    value.freqHz = source.freqHz;
    value.q = source.q;
    engine_.setMainEqBand(band, value);
  }
}

session::EqState AudioGlue::appliedEq() const {
  session::EqState state;
  for (std::size_t track = 0; track < state.tracks.size(); ++track) {
    for (int band = 0; band < 4; ++band) {
      const audio::EqBand value = engine_.trackEqBand(static_cast<int>(track), band);
      auto& out = state.tracks[track][static_cast<std::size_t>(band)];
      out.type = value.type;
      out.gainDb = value.gainDb;
      out.freqHz = value.freqHz;
      out.q = value.q;
    }
  }
  for (int band = 0; band < 4; ++band) {
    const audio::EqBand value = engine_.mainEqBand(band);
    auto& out = state.main[static_cast<std::size_t>(band)];
    out.type = value.type;
    out.gainDb = value.gainDb;
    out.freqHz = value.freqHz;
    out.q = value.q;
  }
  return state;
}

bool AudioGlue::setActiveTake(int track, const std::filesystem::path& path,
                              std::string* error) {
  if (error != nullptr) error->clear();
  return engine_.setActiveTake(track, path.string(), error);
}

void AudioGlue::clearActiveTake(int track) { engine_.clearActiveTake(track); }

bool AudioGlue::setTakeRegions(int track, const std::vector<audio::TakeRegion>& regions,
                               std::string* error) {
  return engine_.setTakeRegions(track, regions, error);
}

void AudioGlue::captureDevice(const float* interleaved, int frames, int channels) {
  pump_.pushCapture(interleaved, frames, channels);
}

void AudioGlue::prepareEngineBlock() {
  mixer_.applyTo(engine_);
  for (int send = 0; send < audio::kNumSends; ++send) {
    engine_.setFx(send, &performance_.effect(send));
  }
  engine_.setPhonesSource(monitorPhones_.load(std::memory_order_relaxed) == 1
                              ? audio::PhonesSource::Main
                              : audio::PhonesSource::Cue);
  engine_.setCueStyle(monitorCueStyle_.load(std::memory_order_relaxed) == 1
                          ? audio::CueStyle::Toggle
                          : audio::CueStyle::Level);
  for (int destination = 0; destination < audio::kNumSendDestinations; ++destination) {
    engine_.setSendSource(
        static_cast<audio::SendDestination>(destination),
        monitorSend_[static_cast<std::size_t>(destination)].load(std::memory_order_relaxed) == 0
            ? audio::SendSource::PreFader
            : audio::SendSource::PostFader);
  }
  audio::MainCompSettings comp;
  comp.on = compOn_.load(std::memory_order_relaxed);
  comp.threshDb = compThreshDb_.load(std::memory_order_relaxed);
  comp.ratio = compRatio_.load(std::memory_order_relaxed);
  comp.attackMs = compAttackMs_.load(std::memory_order_relaxed);
  comp.releaseMs = compReleaseMs_.load(std::memory_order_relaxed);
  comp.makeupDb = compMakeupDb_.load(std::memory_order_relaxed);
  comp.mode = compMode_.load(std::memory_order_relaxed) == 1 ? audio::CompressorMode::Rms
                                                             : audio::CompressorMode::Peak;
  engine_.setMainComp(comp);
  engine_.setClickMode(static_cast<audio::ClickMode>(
      std::clamp(state_.clickMode.load(std::memory_order_relaxed), 0, 2)));
  engine_.setClickOutput(static_cast<audio::ClickOutput>(
      std::clamp(state_.clickOutput.load(std::memory_order_relaxed), 0, 2)));
  engine_.setClickGainDb(state_.clickGainDb.load(std::memory_order_relaxed));
  engine_.setRecording(recorder_.recording());
  engine_.setTransport(state_.rolling.load(std::memory_order_relaxed),
                       state_.tick.load(std::memory_order_relaxed),
                       static_cast<std::uint32_t>(kTicksPerBeat),
                       state_.tempoX100.load(std::memory_order_relaxed));
}

void AudioGlue::writeRecorderBlock(int frames) {
  if (!recorder_.recording()) return;
  const float* taps[audio::kMaxDeviceInputs] = {};
  for (int channel = 0; channel < audio::kMaxDeviceInputs; ++channel) {
    taps[channel] = engine_.tap(channel);
  }
  recorder_.writeBlock(taps, busMainL_, busMainR_, frames);
}

void AudioGlue::renderDevice(float* interleaved, int frames, int channels) {
  if (interleaved == nullptr || frames <= 0 || channels <= 0) return;
  const int width = std::min(channels, audio::kMaxDeviceChannels);
  const audio::DeviceOut deviceOut = engine_.deviceOut();
  audio::buildDeviceOutMap(deviceOut, width, deviceOutSlots_);
  if (channels > audio::kMaxDeviceChannels) {
    std::fill(interleaved, interleaved + static_cast<std::size_t>(frames) * channels, 0.0f);
  }
  int offset = 0;
  while (offset < frames) {
    const int block = std::min(audio::kBlockSize, frames - offset);
    float* planes[audio::kMaxDeviceInputs] = {};
    for (int channel = 0; channel < audio::kMaxDeviceInputs; ++channel) {
      planes[channel] = capturePlanes_[channel];
    }
    pump_.readCapture(planes, block);
    const float* inputs[audio::kMaxDeviceInputs] = {};
    for (int channel = 0; channel < audio::kMaxDeviceInputs; ++channel) {
      inputs[channel] = capturePlanes_[channel];
    }
    prepareEngineBlock();
    audio::OutputBuses outputs;
    outputs.mainL = busMainL_;
    outputs.mainR = busMainR_;
    outputs.out34L = busOut34L_;
    outputs.out34R = busOut34R_;
    outputs.phonesL = busPhonesL_;
    outputs.phonesR = busPhonesR_;
    engine_.process(inputs, audio::kMaxDeviceInputs, block, outputs);
    writeRecorderBlock(block);
    audio::mapBusesToDevice(busMainL_, busMainR_, busOut34L_, busOut34R_,
                            busPhonesL_, busPhonesR_, deviceOutSlots_, channels, block,
                            interleaved + static_cast<std::size_t>(offset) * channels);
    updatePeaks(busMainL_, busMainR_, block);
    renderBlocks_.fetch_add(1, std::memory_order_relaxed);
    offset += block;
  }
}

void AudioGlue::renderEmulatorBlock(int numSamples) {
  if (numSamples <= 0) return;
  int offset = 0;
  while (offset < numSamples) {
    const int block = std::min(audio::kBlockSize, numSamples - offset);
    float* generated[audio::kMaxDeviceInputs] = {};
    const float* inputs[audio::kMaxDeviceInputs] = {};
    for (int channel = 0; channel < audio::kMaxDeviceInputs; ++channel) {
      generated[channel] = synthBuffer_[channel];
      inputs[channel] = synthBuffer_[channel];
    }
    synth_.fillBlock(generated, audio::kMaxDeviceInputs, block);
    prepareEngineBlock();
    audio::OutputBuses outputs;
    outputs.mainL = busMainL_;
    outputs.mainR = busMainR_;
    outputs.out34L = busOut34L_;
    outputs.out34R = busOut34R_;
    outputs.phonesL = busPhonesL_;
    outputs.phonesR = busPhonesR_;
    engine_.process(inputs, audio::kMaxDeviceInputs, block, outputs);
    writeRecorderBlock(block);
    updatePeaks(busMainL_, busMainR_, block);
    renderBlocks_.fetch_add(1, std::memory_order_relaxed);
    offset += block;
  }
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
  // Per-strip meter taps: post-gain mono, independent of mute/pan/master. The
  // engine tap holds the last processed block (<= kBlockSize frames).
  const int tapFrames = std::min(numSamples, audio::kBlockSize);
  for (int channel = 0; channel < audio::kMaxTracks; ++channel) {
    const float* tap = engine_.tap(channel);
    if (tap == nullptr) continue;
    float stripPeak = 0.0f;
    for (int i = 0; i < tapFrames; ++i) {
      stripPeak = std::max(stripPeak, std::abs(tap[i]));
    }
    auto& slot = stripPeaks_[channel];
    const float previous = slot.load(std::memory_order_relaxed);
    slot.store(std::max(previous, stripPeak), std::memory_order_relaxed);
  }
}

}  // namespace mnky::app

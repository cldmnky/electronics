#include "audio_glue.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <exception>

#include <QAudioDevice>
#include <QAudioSink>
#include <QAudioSource>
#include <QByteArray>
#include <QIODevice>
#include <QMediaDevices>
#include <QAudioFormat>
#include <QSettings>

#include "app_state.hpp"
#include "logic.hpp"
#include "logging.hpp"

namespace mnky::app {

static_assert(kDefaultAudioBufferSize == audio::kBlockSize,
              "the default buffer size offered by the System screen is the engine block size");

namespace {

std::string encodeDeviceId(const QAudioDevice& device) {
  return device.id().toBase64().toStdString();
}

std::string deviceName(const QAudioDevice& device) {
  return device.description().toStdString();
}

QAudioDevice findOutput(const std::string& id) {
  for (const auto& device : QMediaDevices::audioOutputs()) {
    if (encodeDeviceId(device) == id) return device;
  }
  return {};
}

QAudioDevice findInput(const std::string& id) {
  for (const auto& device : QMediaDevices::audioInputs()) {
    if (encodeDeviceId(device) == id) return device;
  }
  return {};
}

int bytesPerSample(QAudioFormat::SampleFormat format) {
  return format == QAudioFormat::Float ? 4 : 2;
}

// Prefer 48 kHz stereo float, fall back to 16-bit, then to the device's own
// format with the sample format coerced to one the engine can convert.
QAudioFormat pickFormat(const QAudioDevice& device) {
  QAudioFormat format;
  format.setSampleRate(48000);
  format.setChannelCount(2);
  format.setSampleFormat(QAudioFormat::Float);
  if (device.isFormatSupported(format)) return format;
  format.setSampleFormat(QAudioFormat::Int16);
  if (device.isFormatSupported(format)) return format;
  format = device.preferredFormat();
  if (format.channelCount() <= 0) format.setChannelCount(2);
  if (format.sampleRate() <= 0) format.setSampleRate(48000);
  if (format.sampleFormat() != QAudioFormat::Float &&
      format.sampleFormat() != QAudioFormat::Int16) {
    format.setSampleFormat(QAudioFormat::Int16);
  }
  return format;
}

float clampSample(float value) {
  return value < -1.0f ? -1.0f : (value > 1.0f ? 1.0f : value);
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
    float left[block] = {};
    float right[block] = {};

    auto next = std::chrono::steady_clock::now();
    while (!exit_.load(std::memory_order_relaxed)) {
      owner_.renderBlock(nullptr, 0, left, right, block);
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

// Pull-mode sink source: Qt asks for bytes on its audio thread; each request is
// rendered through the same Engine/Recorder/Click path as the emulator.
class AudioGlue::AudioRenderDevice final : public QIODevice {
 public:
  AudioRenderDevice(AudioGlue& owner, const QAudioFormat& format)
      : owner_(owner),
        format_(format),
        bytesPerFrame_(format.channelCount() * bytesPerSample(format.sampleFormat())) {}

  bool isSequential() const override { return true; }
  qint64 bytesAvailable() const override { return 32768; }

 protected:
  qint64 readData(char* data, qint64 maxSize) override {
    if (data == nullptr || maxSize <= 0 || bytesPerFrame_ <= 0) return 0;
    const int channels = format_.channelCount();
    qint64 produced = 0;
    while (maxSize - produced >= bytesPerFrame_) {
      const int frames =
          static_cast<int>(std::min<qint64>(audio::kBlockSize,
                                           (maxSize - produced) / bytesPerFrame_));
      if (frames <= 0) break;
      owner_.renderBlock(nullptr, 0, renderL_, renderR_, frames);

      for (int frame = 0; frame < frames; ++frame) {
        const float left = clampSample(renderL_[frame]);
        const float right = clampSample(renderR_[frame]);
        for (int channel = 0; channel < channels; ++channel) {
          const float value = channel == 0 ? left : (channel == 1 ? right : 0.0f);
          if (format_.sampleFormat() == QAudioFormat::Float) {
            std::memcpy(data + produced, &value, sizeof value);
            produced += static_cast<qint64>(sizeof value);
          } else {
            const auto pcm = static_cast<qint16>(std::lround(value * 32767.0f));
            std::memcpy(data + produced, &pcm, sizeof pcm);
            produced += static_cast<qint64>(sizeof pcm);
          }
        }
      }
    }
    return produced;
  }

  qint64 writeData(const char*, qint64) override { return 0; }

 private:
  AudioGlue& owner_;
  QAudioFormat format_;
  int bytesPerFrame_ = 8;
  float renderL_[audio::kBlockSize] = {};
  float renderR_[audio::kBlockSize] = {};
};

// Push-mode source consumer: Qt writes captured frames here; they land in a
// bounded ring the render thread reads for engine inputs 1/2.
class AudioGlue::AudioInputConsumer final : public QIODevice {
 public:
  AudioInputConsumer(AudioGlue& owner, const QAudioFormat& format)
      : owner_(owner), format_(format) {}

  bool isSequential() const override { return true; }
  qint64 bytesAvailable() const override { return 0; }

 protected:
  qint64 readData(char*, qint64) override { return 0; }

  qint64 writeData(const char* data, qint64 maxSize) override {
    const int bytesPerFrame =
        format_.channelCount() * bytesPerSample(format_.sampleFormat());
    if (data == nullptr || maxSize <= 0 || bytesPerFrame <= 0) return 0;
    const int frames = static_cast<int>(maxSize / bytesPerFrame);
    if (frames <= 0) return 0;
    owner_.pushInput(data, frames, format_);
    return static_cast<qint64>(frames) * bytesPerFrame;
  }

 private:
  AudioGlue& owner_;
  QAudioFormat format_;
};

AudioGlue::AudioGlue(MixerModel& mixer, FxManager& fx, AppState& state, bool emulator)
    : mixer_(mixer), fx_(fx), state_(state), emulatorStartup_(emulator), emulated_(emulator) {
  for (auto& peak : peaks_) peak.store(0.0f);
  synth_.prepare(sampleRate());
  loadAudioChoice();
}

AudioGlue::~AudioGlue() {
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
  const auto error = openDevice(selection.outputDeviceId, choice_.bufferSize);
  if (!error.empty()) {
    closeAudio();
    note_ = "no audio device (" + error + "); using the emulator generator";
    startEmulator(note_);
    log("audio: " + backend_);
    return false;
  }
  if (!choice_.inputDeviceId.empty()) {
    (void)openInput(choice_.inputDeviceId);
  }
  log("audio: " + backend_);
  return true;
}

void AudioGlue::stop() {
  stopEmulator();
  closeAudio();
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
  if (source_ != nullptr) {
    source_->stop();
    source_.reset();
  }
  inputConsumer_.reset();
  if (sink_ != nullptr) {
    sink_->stop();
    sink_.reset();
  }
  renderDevice_.reset();
  deviceName_.clear();
  inputName_.clear();
  deviceBufferFrames_ = 0;
  running_.store(false);
  emulated_.store(false);
}

std::string AudioGlue::openDevice(const std::string& outputDeviceId, int bufferSize) {
  stopEmulator();
  closeAudio();

  QAudioDevice device;
  if (outputDeviceId.empty()) {
    device = QMediaDevices::defaultAudioOutput();
  } else {
    device = findOutput(outputDeviceId);
  }
  if (device.isNull()) return "no usable output device";

  outputFormat_ = pickFormat(device);
  outputBytesPerFrame_ =
      outputFormat_.channelCount() * bytesPerSample(outputFormat_.sampleFormat());
  if (outputBytesPerFrame_ <= 0) return "unsupported output format";
  sampleRate_.store(outputFormat_.sampleRate() > 0 ? outputFormat_.sampleRate() : 48000.0);
  synth_.prepare(sampleRate());

  const int requested = clampAudioBufferSize(bufferSize);
  sink_ = std::make_unique<QAudioSink>(device, outputFormat_);
  sink_->setBufferSize(requested * outputBytesPerFrame_);
  // A device that dies (server exit, unplug) must stop reading as "live" in the
  // System readout instead of keeping the last start numbers forever.
  QObject::connect(sink_.get(), &QAudioSink::stateChanged, sink_.get(),
                   [this](QAudio::State state) {
                     if (state == QAudio::ActiveState) {
                       running_.store(true);
                       log("audio: sink active, " +
                           std::to_string(static_cast<int>(sampleRate())) + " Hz / " +
                           std::to_string(blockSize()) + " frames");
                     } else if (state == QAudio::StoppedState) {
                       running_.store(false);
                       if (sink_ != nullptr && sink_->error() != QAudio::NoError) {
                         note_ = "output device stopped with an error";
                         log("audio: output device stopped with an error");
                       }
                     }
                   });
  renderDevice_ = std::make_unique<AudioRenderDevice>(*this, outputFormat_);
  renderDevice_->open(QIODevice::ReadOnly);
  sink_->start(renderDevice_.get());
  if (sink_->error() != QAudio::NoError) {
    return "QAudioSink start failed";
  }

  deviceName_ = deviceName(device);
  const int liveBuffer = static_cast<int>(sink_->bufferSize()) /
                         std::max(1, outputBytesPerFrame_);
  deviceBufferFrames_ = liveBuffer > 0 ? liveBuffer : requested;
  blockSize_.store(deviceBufferFrames_);
  running_.store(true);
  backend_ = "Qt Multimedia / " + deviceName_ + ", " +
             std::to_string(static_cast<int>(sampleRate())) + " Hz / " +
             std::to_string(deviceBufferFrames_) + " frames";
  if (static_cast<int>(std::lround(sampleRate())) != 48000) {
    note_ = "device runs at " + std::to_string(static_cast<int>(sampleRate())) +
            " Hz instead of the requested 48000 Hz";
  }
  if (deviceBufferFrames_ != requested && note_.empty()) {
    note_ = "device buffer is " + std::to_string(deviceBufferFrames_) +
            " frames instead of the requested " + std::to_string(requested);
  }
  return {};
}

bool AudioGlue::openInput(const std::string& inputDeviceId) {
  if (source_ != nullptr) {
    source_->stop();
    source_.reset();
  }
  inputConsumer_.reset();
  if (inputDeviceId.empty()) return false;
  const auto device = findInput(inputDeviceId);
  if (device.isNull()) {
    log("audio: input device unavailable");
    return false;
  }
  const auto format = pickFormat(device);
  inputRingFrames_ = static_cast<int>(format.sampleRate() > 0 ? format.sampleRate() : 48000);
  {
    const std::lock_guard<std::mutex> lock(inputLock_);
    inputRingL_.assign(static_cast<std::size_t>(inputRingFrames_), 0.0f);
    inputRingR_.assign(static_cast<std::size_t>(inputRingFrames_), 0.0f);
    inputWritten_ = 0;
  }
  source_ = std::make_unique<QAudioSource>(device, format);
  inputConsumer_ = std::make_unique<AudioInputConsumer>(*this, format);
  inputConsumer_->open(QIODevice::WriteOnly);
  source_->start(inputConsumer_.get());
  if (source_->error() != QAudio::NoError) {
    source_->stop();
    source_.reset();
    inputConsumer_.reset();
    log("audio: input source start failed");
    return false;
  }
  inputName_ = deviceName(device);
  log("audio: input " + inputName_);
  return true;
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
  availability_ = {};
  availability_.defaultOutput = !QMediaDevices::defaultAudioOutput().isNull();
  std::string outputNames;
  std::string inputNames;
  for (const auto& device : QMediaDevices::audioOutputs()) {
    availability_.outputDeviceIds.push_back(encodeDeviceId(device));
    if (!outputNames.empty()) outputNames += ", ";
    outputNames += device.description().toStdString();
  }
  for (const auto& device : QMediaDevices::audioInputs()) {
    availability_.inputDeviceIds.push_back(encodeDeviceId(device));
    if (!inputNames.empty()) inputNames += ", ";
    inputNames += device.description().toStdString();
  }
  log("audio: Qt devices default=" + std::string(availability_.defaultOutput ? "yes" : "no") +
      " outputs=" + std::to_string(availability_.outputDeviceIds.size()) + " [" +
      outputNames + "] inputs=" + std::to_string(availability_.inputDeviceIds.size()) +
      " [" + inputNames + "]");
}

AudioBackendAvailability AudioGlue::backendAvailability() const {
  return availability_;
}

std::vector<AudioDeviceOption> AudioGlue::outputDevices() const {
  std::vector<AudioDeviceOption> options;
  const auto defaultDevice = QMediaDevices::defaultAudioOutput();
  const std::string defaultId = defaultDevice.isNull() ? std::string() : encodeDeviceId(defaultDevice);
  for (const auto& device : QMediaDevices::audioOutputs()) {
    AudioDeviceOption option;
    option.id = encodeDeviceId(device);
    option.name = device.description().toStdString();
    option.isDefault = !defaultId.empty() && option.id == defaultId;
    options.push_back(option);
  }
  return options;
}

std::vector<AudioDeviceOption> AudioGlue::inputDevices() const {
  std::vector<AudioDeviceOption> options;
  const auto defaultDevice = QMediaDevices::defaultAudioInput();
  const std::string defaultId = defaultDevice.isNull() ? std::string() : encodeDeviceId(defaultDevice);
  for (const auto& device : QMediaDevices::audioInputs()) {
    AudioDeviceOption option;
    option.id = encodeDeviceId(device);
    option.name = device.description().toStdString();
    option.isDefault = !defaultId.empty() && option.id == defaultId;
    options.push_back(option);
  }
  return options;
}

std::string AudioGlue::applyAudioChoice(const AudioBackendChoice& choice) {
  choice_ = choice;
  choice_.bufferSize = clampAudioBufferSize(choice.bufferSize);
  saveAudioChoice();
  note_.clear();
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

  const auto error = openDevice(selection.outputDeviceId, choice_.bufferSize);
  if (!error.empty()) {
    closeAudio();
    note_ = "device open failed: " + error + "; using the emulator generator";
    startEmulator(note_);
    log("audio: " + note_);
    return error;
  }
  if (!choice_.inputDeviceId.empty()) (void)openInput(choice_.inputDeviceId);
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
  } else if (sink_ != nullptr && running_.load()) {
    state.liveType = "Qt Multimedia";
    state.liveDevice = deviceName_;
    state.sampleRate = sampleRate();
    state.blockSize = blockSize();
    state.callbackBlocks = renderBlocks_.load(std::memory_order_relaxed);
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

void AudioGlue::pushInput(const char* data, int frames, const QAudioFormat& format) {
  if (data == nullptr || frames <= 0) return;
  const int channels = format.channelCount();
  const int bytesPerFrame = channels * bytesPerSample(format.sampleFormat());
  if (channels <= 0 || bytesPerFrame <= 0) return;
  const std::lock_guard<std::mutex> lock(inputLock_);
  if (inputRingFrames_ <= 0 ||
      inputRingL_.size() != static_cast<std::size_t>(inputRingFrames_)) {
    return;
  }
  for (int frame = 0; frame < frames; ++frame) {
    float left = 0.0f;
    float right = 0.0f;
    for (int channel = 0; channel < channels; ++channel) {
      float value = 0.0f;
      const char* sample = data + static_cast<qint64>(frame) * bytesPerFrame +
                           channel * bytesPerSample(format.sampleFormat());
      if (format.sampleFormat() == QAudioFormat::Float) {
        std::memcpy(&value, sample, sizeof value);
      } else {
        qint16 pcm = 0;
        std::memcpy(&pcm, sample, sizeof pcm);
        value = static_cast<float>(pcm) / 32767.0f;
      }
      if (channel == 0) left = value;
      if (channel == 1) right = value;
    }
    if (channels == 1) right = left;
    const auto index = static_cast<std::size_t>(
        inputWritten_ % static_cast<std::uint64_t>(inputRingFrames_));
    inputRingL_[index] = left;
    inputRingR_[index] = right;
    ++inputWritten_;
  }
}

void AudioGlue::copyInput(float* left, float* right, int frames) {
  if (left == nullptr || right == nullptr || frames <= 0) return;
  const std::lock_guard<std::mutex> lock(inputLock_);
  if (inputRingFrames_ <= 0 || inputRingL_.empty()) {
    std::fill(left, left + frames, 0.0f);
    std::fill(right, right + frames, 0.0f);
    return;
  }
  const std::uint64_t available =
      std::min<std::uint64_t>(inputWritten_, static_cast<std::uint64_t>(frames));
  const int missing = frames - static_cast<int>(available);
  for (int i = 0; i < missing; ++i) {
    left[i] = 0.0f;
    right[i] = 0.0f;
  }
  for (int i = 0; i < static_cast<int>(available); ++i) {
    const auto index = static_cast<std::size_t>(
        (inputWritten_ - available + static_cast<std::uint64_t>(i)) %
        static_cast<std::uint64_t>(inputRingFrames_));
    left[missing + i] = inputRingL_[index];
    right[missing + i] = inputRingR_[index];
  }
}

void AudioGlue::renderBlock(const float* const* inputChannelData, int numInputChannels,
                            float* outL, float* outR, int numSamples) {
  if (outL == nullptr || outR == nullptr || numSamples <= 0) return;
  renderBlocks_.fetch_add(1, std::memory_order_relaxed);

  // One read per block: the backend can change on the GUI thread while a
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
    } else if (inputChannelData != nullptr && numInputChannels > 0) {
      const int count = std::min(audio::kMaxInputs, numInputChannels);
      for (int channel = 0; channel < count; ++channel) inputs[channel] = inputChannelData[channel];
    } else {
      // Real-device path: the optional QAudioSource ring feeds inputs 1/2.
      copyInput(inputL_, inputR_, frames);
      inputs[0] = inputL_;
      inputs[1] = inputR_;
    }

    mixer_.applyTo(engine_);
    for (int send = 0; send < audio::kNumSends; ++send) {
      engine_.setFx(send, fx_.effect(send));
    }
    engine_.process(inputs, generated ? audio::kMaxInputs : (inputChannelData != nullptr
                                                                ? numInputChannels
                                                                : 2),
                    frames, outL + offset, outR + offset);

    if (recorder_.recording()) {
      const float* taps[audio::kMaxInputs] = {};
      for (int channel = 0; channel < audio::kMaxInputs; ++channel) {
        taps[channel] = engine_.tap(channel);
      }
      recorder_.writeBlock(taps, outL + offset, outR + offset, frames);
    }

    // Metronome after the recorder tap: the click is monitoring, not take
    // material.
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

#include "audio_glue.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include "logic.hpp"
#include "logging.hpp"

namespace mnky::app {

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

AudioGlue::AudioGlue(MixerModel& mixer, FxManager& fx, bool emulator)
    : mixer_(mixer), fx_(fx), emulated_(emulator) {
  for (auto& peak : peaks_) peak.store(0.0f);
  synth_.prepare(sampleRate());
}

AudioGlue::~AudioGlue() {
  stop();
}

bool AudioGlue::start() {
  if (emulated_) {
    backend_ = "emulator: sine/noise generator, 48 kHz / 128 frames";
    synth_.prepare(sampleRate());
    driver_ = std::make_unique<EmulatorDriver>(*this);
    driver_->begin();
    running_.store(true);
    log("audio: " + backend_);
    return true;
  }

  const auto error = deviceManager_.initialise(audio::kMaxInputs, 2, nullptr, true);
  if (error.isNotEmpty()) {
    backend_ = "no audio device (" + error + "); using the emulator generator";
    log("audio: " + backend_);
    emulated_ = true;
    driver_ = std::make_unique<EmulatorDriver>(*this);
    driver_->begin();
    running_.store(true);
    return false;
  }

  deviceManager_.addAudioCallback(this);
  if (auto* device = deviceManager_.getCurrentAudioDevice()) {
    backend_ = device->getTypeName() + " / " + device->getName() + ", " +
               juce::String(device->getCurrentSampleRate(), 0) + " Hz / " +
               juce::String(device->getCurrentBufferSizeSamples()) + " frames, " +
               juce::String(device->getActiveInputChannels().countNumberOfSetBits()) +
               " in";
  } else {
    backend_ = "audio device manager active, no current device";
  }
  log("audio: " + backend_);
  return true;
}

void AudioGlue::stop() {
  if (driver_ != nullptr) {
    driver_->end();
    driver_.reset();
  }
  deviceManager_.removeAudioCallback(this);
  deviceManager_.closeAudioDevice();
  if (recorder_.recording()) {
    const auto take = recorder_.stopTake();
    const juce::ScopedLock lock(takeLock_);
    lastTake_ = take;
  }
  running_.store(false);
}

juce::String AudioGlue::backendDescription() const {
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

  int offset = 0;
  while (offset < numSamples) {
    const int frames = std::min(audio::kBlockSize, numSamples - offset);
    const float* inputs[audio::kMaxInputs] = {};

    if (emulated_) {
      float* generated[audio::kMaxInputs] = {};
      for (int channel = 0; channel < audio::kMaxInputs; ++channel) {
        generated[channel] = synthBuffer_[channel];
        inputs[channel] = synthBuffer_[channel];
      }
      synth_.fillBlock(generated, audio::kMaxInputs, frames);
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
    engine_.process(inputs, emulated_ ? audio::kMaxInputs : numInputChannels, frames,
                    outL + offset, outR + offset);

    if (recorder_.recording()) {
      const float* taps[audio::kMaxInputs] = {};
      for (int channel = 0; channel < audio::kMaxInputs; ++channel) {
        taps[channel] = engine_.tap(channel);
      }
      recorder_.writeBlock(taps, outL + offset, outR + offset, frames);
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

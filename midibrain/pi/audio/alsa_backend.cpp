#include "alsa_backend.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <thread>
#include <cstdio>
#include <map>
#include <memory>
#include <mutex>
#include <poll.h>
#include <string>
#include <thread>
#include <vector>

#if MNKY_HAVE_ALSA
#include <alsa/asoundlib.h>
#endif

namespace mnky::audio {
namespace {

#if MNKY_HAVE_ALSA

/// Format preference for both directions. S32_LE is the UMC1820 capture
/// format, S24_3LE its playback format; F32 and S16 cover other cards and the
/// plughw fallback.
constexpr SampleFormat kFormatPreference[] = {SampleFormat::S32, SampleFormat::F32,
                                              SampleFormat::S24_3, SampleFormat::S16};
constexpr int kBufferPeriods = 3;  // 2-3 periods: 384 frames at the 128 request

snd_pcm_format_t toAlsaFormat(SampleFormat format) {
  switch (format) {
    case SampleFormat::F32:
      return SND_PCM_FORMAT_FLOAT_LE;
    case SampleFormat::S32:
      return SND_PCM_FORMAT_S32_LE;
    case SampleFormat::S24_3:
      return SND_PCM_FORMAT_S24_3LE;
    case SampleFormat::S24_32:
      // ALSA names the 24-bit-in-32-bit-word layout S24_LE; snd_pcm must not
      // confuse it with the packed 3-byte S24_3LE.
      return SND_PCM_FORMAT_S24_LE;
    case SampleFormat::S16:
      return SND_PCM_FORMAT_S16_LE;
    case SampleFormat::Unknown:
      break;
  }
  return SND_PCM_FORMAT_UNKNOWN;
}

/// Channel/rate facts gathered by enumeration. An ALSA hw PCM cannot be opened
/// while the running backend owns it, so the last successful probe is kept and
/// reused instead of dropping the channel width from the device list.
struct PcmFacts {
  int channels = 0;
  bool rateOk = false;
};

std::mutex& factsMutex() {
  static std::mutex mutex;
  return mutex;
}

std::map<std::string, PcmFacts>& factsCache() {
  static std::map<std::string, PcmFacts> cache;
  return cache;
}

std::string factsKey(const std::string& pcm, snd_pcm_stream_t direction) {
  return std::string(direction == SND_PCM_STREAM_PLAYBACK ? "out:" : "in:") + pcm;
}

PcmFacts cachedFacts(const std::string& pcm, snd_pcm_stream_t direction) {
  const std::lock_guard<std::mutex> lock(factsMutex());
  const auto found = factsCache().find(factsKey(pcm, direction));
  return found == factsCache().end() ? PcmFacts{} : found->second;
}

void rememberFacts(const std::string& pcm, snd_pcm_stream_t direction,
                   const PcmFacts& facts) {
  const std::lock_guard<std::mutex> lock(factsMutex());
  factsCache()[factsKey(pcm, direction)] = facts;
}

/// Opens a PCM read-only for enumeration and reads channel/rate capability.
PcmFacts probeFacts(const std::string& pcm, snd_pcm_stream_t direction) {
  PcmFacts facts;
  snd_pcm_t* handle = nullptr;
  if (snd_pcm_open(&handle, pcm.c_str(), direction, SND_PCM_NONBLOCK) < 0 ||
      handle == nullptr) {
    return cachedFacts(pcm, direction);
  }
  snd_pcm_hw_params_t* params = nullptr;
  if (snd_pcm_hw_params_malloc(&params) >= 0) {
    if (snd_pcm_hw_params_any(handle, params) >= 0) {
      unsigned int channels = 0;
      if (snd_pcm_hw_params_get_channels_max(params, &channels) >= 0) {
        facts.channels = static_cast<int>(channels);
      }
      facts.rateOk = snd_pcm_hw_params_test_rate(handle, params, 48000, 0) == 0;
    }
    snd_pcm_hw_params_free(params);
  }
  snd_pcm_close(handle);
  if (facts.channels > 0) rememberFacts(pcm, direction, facts);
  return facts.channels > 0 ? facts : cachedFacts(pcm, direction);
}

/// Card + device of the PCM that ALSA's `default` resolves to, if it opens.
bool probeDefault(snd_pcm_stream_t direction, int* card, int* device) {
  snd_pcm_t* handle = nullptr;
  if (snd_pcm_open(&handle, "default", direction, SND_PCM_NONBLOCK) < 0 ||
      handle == nullptr) {
    return false;
  }
  bool ok = false;
  snd_pcm_info_t* info = nullptr;
  if (snd_pcm_info_malloc(&info) >= 0) {
    if (snd_pcm_info(handle, info) >= 0) {
      *card = snd_pcm_info_get_card(info);
      *device = static_cast<int>(snd_pcm_info_get_device(info));
      ok = true;
    }
    snd_pcm_info_free(info);
  }
  snd_pcm_close(handle);
  return ok;
}

struct PcmHandle {
  snd_pcm_t* handle = nullptr;
  SampleFormat format = SampleFormat::Unknown;
  int channels = 0;
  int period = 0;
  int buffer = 0;
};

/// Opens one PCM and fixes the parameter set. `hw:` is tried first; a card that
/// cannot do the negotiated format/channels/rate is retried as `plughw:`, where
/// ALSA's plug layer converts instead of refusing the stream.
bool openPcm(const std::string& pcm, snd_pcm_stream_t direction, int wantedChannels,
             int periodFrames, double sampleRate, PcmHandle* result,
             std::string* error) {
  const std::string names[2] = {pcm, pcm.rfind("hw:", 0) == 0 ? "plug" + pcm : pcm};
  std::string lastError;
  for (int attempt = 0; attempt < 2; ++attempt) {
    if (attempt == 1 && names[1] == names[0]) break;
    snd_pcm_t* handle = nullptr;
    int opened = snd_pcm_open(&handle, names[attempt].c_str(), direction,
                              SND_PCM_NONBLOCK);
    // Switching from PipeWire to the ALSA path can race the graph releasing
    // the card, so an EBUSY open is retried briefly instead of falling back to
    // the emulator generator.
    for (int retry = 0; opened == -EBUSY && retry < 15; ++retry) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      opened = snd_pcm_open(&handle, names[attempt].c_str(), direction,
                            SND_PCM_NONBLOCK);
    }
    if (opened < 0 || handle == nullptr) {
      lastError = std::string("snd_pcm_open ") + names[attempt] + ": " +
                  snd_strerror(opened);
      continue;
    }
    snd_pcm_hw_params_t* params = nullptr;
    if (snd_pcm_hw_params_malloc(&params) < 0) {
      snd_pcm_close(handle);
      lastError = "snd_pcm_hw_params_malloc failed";
      continue;
    }
    SampleFormat chosen = SampleFormat::Unknown;
    snd_pcm_format_t alsaFormat = SND_PCM_FORMAT_UNKNOWN;
    const bool anyOk = snd_pcm_hw_params_any(handle, params) >= 0;
    if (anyOk) {
      for (const SampleFormat candidate : kFormatPreference) {
        const snd_pcm_format_t format = toAlsaFormat(candidate);
        if (snd_pcm_hw_params_test_format(handle, params, format) == 0) {
          chosen = candidate;
          alsaFormat = format;
          break;
        }
      }
    }
    unsigned int channels = 0;
    int directionValue = 0;
    snd_pcm_uframes_t period = static_cast<snd_pcm_uframes_t>(
        periodFrames > 0 ? periodFrames : kBlockSize);
    snd_pcm_uframes_t buffer = period * kBufferPeriods;
    const std::string plug = names[attempt].rfind("plughw:", 0) == 0 ? "plughw" : "hw";
    bool ok = anyOk && chosen != SampleFormat::Unknown;
    if (ok && snd_pcm_hw_params_set_access(handle, params,
                                           SND_PCM_ACCESS_RW_INTERLEAVED) < 0) {
      ok = false;
      lastError = plug + " rejects RW_INTERLEAVED";
    }
    if (ok && snd_pcm_hw_params_set_format(handle, params, alsaFormat) < 0) {
      ok = false;
      lastError = plug + " rejects " + sampleFormatName(chosen);
    }
    if (ok) {
      if (wantedChannels > 0) {
        channels = static_cast<unsigned int>(wantedChannels);
      } else if (snd_pcm_hw_params_get_channels_max(params, &channels) < 0) {
        channels = 2;
      }
      if (snd_pcm_hw_params_set_channels(handle, params, channels) < 0) {
        ok = false;
        lastError = plug + " rejects " + std::to_string(channels) + " channels";
      }
    }
    unsigned int rate = static_cast<unsigned int>(sampleRate + 0.5);
    if (ok && snd_pcm_hw_params_set_rate(handle, params, rate, 0) < 0) {
      ok = false;
      lastError = plug + " rejects " + std::to_string(rate) + " Hz";
    }
    if (ok && snd_pcm_hw_params_set_period_size_near(handle, params, &period,
                                                     &directionValue) < 0) {
      ok = false;
      lastError = plug + " rejects a " + std::to_string(periodFrames) + "-frame period";
    }
    if (ok) {
      buffer = period * kBufferPeriods;
      if (snd_pcm_hw_params_set_buffer_size_near(handle, params, &buffer) < 0) {
        ok = false;
        lastError = plug + " rejects a " + std::to_string(kBufferPeriods) + "-period buffer";
      }
    }
    if (ok && snd_pcm_hw_params(handle, params) < 0) {
      ok = false;
      lastError = plug + " hw_params failed";
    }
    int signedValue = 0;
    if (ok) {
      snd_pcm_format_t applied = SND_PCM_FORMAT_UNKNOWN;
      if (snd_pcm_hw_params_get_format(params, &applied) < 0 ||
          snd_pcm_hw_params_get_channels(params, &channels) < 0 ||
          snd_pcm_hw_params_get_period_size(params, &period, &signedValue) < 0 ||
          snd_pcm_hw_params_get_buffer_size(params, &buffer) < 0) {
        ok = false;
        lastError = plug + " cannot report the applied hw_params";
      }
      chosen = applied == SND_PCM_FORMAT_FLOAT_LE
                   ? SampleFormat::F32
                   : (applied == SND_PCM_FORMAT_S32_LE
                          ? SampleFormat::S32
                          : (applied == SND_PCM_FORMAT_S24_3LE
                                 ? SampleFormat::S24_3
                                 : (applied == SND_PCM_FORMAT_S24_LE
                                        ? SampleFormat::S24_32
                                        : (applied == SND_PCM_FORMAT_S16_LE
                                               ? SampleFormat::S16
                                               : SampleFormat::Unknown))));
      if (chosen == SampleFormat::Unknown) {
        ok = false;
        lastError = plug + " applied an unsupported sample format";
      }
    }
    snd_pcm_sw_params_t* sw = nullptr;
    if (ok) {
      if (snd_pcm_sw_params_malloc(&sw) < 0 ||
          snd_pcm_sw_params_current(handle, sw) < 0) {
        ok = false;
      } else {
        if (direction == SND_PCM_STREAM_PLAYBACK) {
          snd_pcm_sw_params_set_start_threshold(handle, sw, buffer);
        } else {
          snd_pcm_sw_params_set_start_threshold(handle, sw, 1);
        }
        snd_pcm_sw_params_set_avail_min(handle, sw, period);
        if (snd_pcm_sw_params(handle, sw) < 0) ok = false;
      }
      if (sw != nullptr) snd_pcm_sw_params_free(sw);
    }
    snd_pcm_hw_params_free(params);
    if (!ok) {
      snd_pcm_close(handle);
      if (lastError.empty()) lastError = std::string("cannot configure ") + names[attempt];
      continue;
    }
    rememberFacts(pcm, direction,
                  PcmFacts{static_cast<int>(channels), sampleRate == 48000.0});
    result->handle = handle;
    result->format = chosen;
    result->channels = static_cast<int>(channels);
    result->period = static_cast<int>(period);
    result->buffer = static_cast<int>(buffer);
    return true;
  }
  if (error != nullptr) *error = lastError.empty() ? "cannot open " + pcm : lastError;
  return false;
}

class AlsaBackend final : public DeviceBackend {
 public:
  AlsaBackend() = default;
  ~AlsaBackend() override { close(); }

  DeviceApi api() const override { return DeviceApi::Alsa; }
  std::vector<DeviceInfo> enumerateOutputs() override {
    return enumerateAlsaDevices().outputs;
  }
  std::vector<DeviceInfo> enumerateInputs() override {
    return enumerateAlsaDevices().inputs;
  }

  bool open(const DeviceConfig& config, DeviceClient* client,
            std::string* error) override {
    close();
    if (error != nullptr) error->clear();
    if (client == nullptr) {
      if (error != nullptr) *error = "alsa: no device client";
      return false;
    }
    if (config.sampleRate <= 0.0 ||
        std::abs(config.sampleRate - 48000.0) > 0.5) {
      if (error != nullptr) *error = "alsa: only 48 kHz is supported";
      return false;
    }
    client_ = client;
    sampleRate_ = 48000.0;

    const std::string outputPcm = config.outputId.empty() ? "default" : config.outputId;
    const int wantedOut = config.outputChannels > 0 ? config.outputChannels : 0;
    if (!openPcm(outputPcm, SND_PCM_STREAM_PLAYBACK, wantedOut, config.blockFrames,
                 sampleRate_, &playback_, error)) {
      close();
      return false;
    }
    if (!config.inputId.empty()) {
      const int wantedIn = config.inputChannels > 0 ? config.inputChannels : 0;
      std::string inputError;
      if (!openPcm(config.inputId, SND_PCM_STREAM_CAPTURE, wantedIn,
                   config.blockFrames, sampleRate_, &capture_, &inputError)) {
        // Output is mandatory, capture is not: keep playing without it, but say
        // why the 18-channel capture is absent.
        inputError_ = inputError;
      }
    }
    outChannels_ = playback_.channels;
    inChannels_ = capture_.handle != nullptr ? capture_.channels : 0;
    period_ = std::max(1, playback_.period);
    if (capture_.handle != nullptr) period_ = std::max(1, std::min(period_, capture_.period));
    if (capture_.handle != nullptr) {
      snd_pcm_poll_descriptors(capture_.handle, captureFds_, 1);
      captureFd_ = captureFds_[0].fd;
    }
    snd_pcm_poll_descriptors(playback_.handle, playbackFds_, 1);
    playbackFd_ = playbackFds_[0].fd;

    renderFloats_.assign(static_cast<std::size_t>(period_) * outChannels_, 0.0f);
    renderBytes_.assign(static_cast<std::size_t>(period_) *
                            static_cast<std::size_t>(frameBytes(playback_.format, outChannels_)),
                        0);
    if (capture_.handle != nullptr) {
      captureBytes_.assign(static_cast<std::size_t>(period_) *
                               static_cast<std::size_t>(frameBytes(capture_.format, inChannels_)),
                           0);
      captureFloats_.assign(static_cast<std::size_t>(period_) * inChannels_, 0.0f);
    }
    description_ = "alsa / " + outputPcm + " (" + std::to_string(outChannels_) +
                   " out, " + sampleFormatName(playback_.format) + ")";
    if (capture_.handle != nullptr) {
      description_ += " + " + config.inputId + " (" + std::to_string(inChannels_) +
                      " in, " + sampleFormatName(capture_.format) + ")";
    } else if (!inputError_.empty()) {
      description_ += " [no capture: " + inputError_ + "]";
    }
    description_ += " " + std::to_string(static_cast<int>(sampleRate_)) + " Hz / " +
                    std::to_string(period_) + " frames";
    return true;
  }

  void close() override {
    stop();
    if (playback_.handle != nullptr) snd_pcm_close(playback_.handle);
    if (capture_.handle != nullptr) snd_pcm_close(capture_.handle);
    playback_ = PcmHandle{};
    capture_ = PcmHandle{};
    client_ = nullptr;
    outChannels_ = 0;
    inChannels_ = 0;
    period_ = 0;
    description_.clear();
    inputError_.clear();
  }

  bool start(std::string* error) override {
    if (error != nullptr) error->clear();
    if (playback_.handle == nullptr) {
      if (error != nullptr) *error = "alsa: no playback PCM is open";
      return false;
    }
    if (thread_.joinable()) return true;
    if (snd_pcm_prepare(playback_.handle) < 0) {
      if (error != nullptr) *error = "alsa: snd_pcm_prepare(playback) failed";
      return false;
    }
    if (capture_.handle != nullptr) {
      if (snd_pcm_prepare(capture_.handle) < 0 || snd_pcm_start(capture_.handle) < 0) {
        if (error != nullptr) *error = "alsa: snd_pcm_start(capture) failed";
        return false;
      }
    }
    exit_.store(false);
    thread_ = std::thread([this] { threadLoop(); });
    running_.store(true);
    return true;
  }

  void stop() override {
    exit_.store(true);
    if (thread_.joinable()) thread_.join();
    running_.store(false);
    if (playback_.handle != nullptr) snd_pcm_drop(playback_.handle);
    if (capture_.handle != nullptr) snd_pcm_drop(capture_.handle);
  }

  bool running() const override { return running_.load(std::memory_order_relaxed); }
  double sampleRate() const override { return sampleRate_; }
  int periodFrames() const override { return period_; }
  int outputChannels() const override { return outChannels_; }
  int inputChannels() const override { return inChannels_; }
  std::uint64_t xruns() const override { return xruns_.load(std::memory_order_relaxed); }
  std::string description() const override { return description_; }

 private:
  void readPeriod() {
    const int stride = frameBytes(capture_.format, inChannels_);
    int done = 0;
    while (done < period_) {
      const snd_pcm_sframes_t got = snd_pcm_readi(
          capture_.handle,
          captureBytes_.data() + static_cast<std::size_t>(done) * stride,
          static_cast<snd_pcm_uframes_t>(period_ - done));
      if (got == -EAGAIN) break;
      if (got < 0) {
        xruns_.fetch_add(1, std::memory_order_relaxed);
        if (snd_pcm_recover(capture_.handle, static_cast<int>(got), 1) < 0) {
          exit_.store(true);
          return;
        }
        done = 0;
        continue;
      }
      done += static_cast<int>(got);
    }
    if (done <= 0) return;
    decodeInterleaved(captureBytes_.data(), capture_.format, inChannels_, done,
                      captureFloats_.data());
    client_->captureDevice(captureFloats_.data(), done, inChannels_);
  }

  void writePeriod() {
    client_->renderDevice(renderFloats_.data(), period_, outChannels_);
    encodeInterleaved(renderFloats_.data(), outChannels_, playback_.format, period_,
                      renderBytes_.data());
    const int stride = frameBytes(playback_.format, outChannels_);
    int done = 0;
    while (done < period_) {
      const snd_pcm_sframes_t wrote = snd_pcm_writei(
          playback_.handle,
          renderBytes_.data() + static_cast<std::size_t>(done) * stride,
          static_cast<snd_pcm_uframes_t>(period_ - done));
      if (wrote == -EAGAIN) {
        if (snd_pcm_wait(playback_.handle, 20) <= 0) break;
        continue;
      }
      if (wrote < 0) {
        xruns_.fetch_add(1, std::memory_order_relaxed);
        if (snd_pcm_recover(playback_.handle, static_cast<int>(wrote), 1) < 0) {
          exit_.store(true);
          return;
        }
        // Re-render into the recovered ring from the start of this period.
        client_->renderDevice(renderFloats_.data(), period_, outChannels_);
        encodeInterleaved(renderFloats_.data(), outChannels_, playback_.format, period_,
                          renderBytes_.data());
        done = 0;
        continue;
      }
      done += static_cast<int>(wrote);
    }
  }

  void threadLoop() {
    struct pollfd fds[2] = {};
    while (!exit_.load(std::memory_order_relaxed)) {
      int count = 0;
      if (capture_.handle != nullptr) {
        fds[count].fd = captureFd_;
        fds[count].events = POLLIN;
        ++count;
      }
      fds[count].fd = playbackFd_;
      fds[count].events = POLLOUT;
      const int playbackIndex = count;
      ++count;
      const int ready = poll(fds, static_cast<nfds_t>(count), 100);
      if (ready < 0) {
        if (errno == EINTR) continue;
        break;
      }
      if (ready == 0) continue;
      if (capture_.handle != nullptr && (fds[0].revents & (POLLIN | POLLERR)) != 0) {
        readPeriod();
      }
      if ((fds[playbackIndex].revents & (POLLOUT | POLLERR)) != 0) {
        writePeriod();
      }
    }
    running_.store(false);
  }

  PcmHandle playback_;
  PcmHandle capture_;
  DeviceClient* client_ = nullptr;
  double sampleRate_ = 48000.0;
  int outChannels_ = 0;
  int inChannels_ = 0;
  int period_ = 0;
  int playbackFd_ = -1;
  int captureFd_ = -1;
  struct pollfd playbackFds_[1] = {};
  struct pollfd captureFds_[1] = {};
  std::vector<float> renderFloats_;
  std::vector<unsigned char> renderBytes_;
  std::vector<float> captureFloats_;
  std::vector<unsigned char> captureBytes_;
  std::atomic<bool> exit_{false};
  std::atomic<bool> running_{false};
  std::atomic<std::uint64_t> xruns_{0};
  std::thread thread_;
  std::string description_;
  std::string inputError_;
};

#else  // !MNKY_HAVE_ALSA

class AlsaBackendPlaceholder final : public DeviceBackend {
 public:
  DeviceApi api() const override { return DeviceApi::Alsa; }
  std::vector<DeviceInfo> enumerateOutputs() override { return {}; }
  std::vector<DeviceInfo> enumerateInputs() override { return {}; }
  bool open(const DeviceConfig&, DeviceClient*, std::string* error) override {
    if (error != nullptr) *error = "alsa: libasound is not available in this build";
    return false;
  }
  void close() override {}
  bool start(std::string*) override { return false; }
  void stop() override {}
  bool running() const override { return false; }
  double sampleRate() const override { return 0.0; }
  int periodFrames() const override { return 0; }
  int outputChannels() const override { return 0; }
  int inputChannels() const override { return 0; }
  std::uint64_t xruns() const override { return 0; }
  std::string description() const override { return "alsa (not compiled)"; }
};

#endif

}  // namespace

bool alsaCompiled() {
#if MNKY_HAVE_ALSA
  return true;
#else
  return false;
#endif
}

DeviceInventory enumerateAlsaDevices() {
  DeviceInventory inventory;
#if MNKY_HAVE_ALSA
  int defaultOutputCard = -1;
  int defaultOutputDevice = -1;
  const bool hasDefaultOutput =
      probeDefault(SND_PCM_STREAM_PLAYBACK, &defaultOutputCard, &defaultOutputDevice);
  int defaultInputCard = -1;
  int defaultInputDevice = -1;
  const bool hasDefaultInput =
      probeDefault(SND_PCM_STREAM_CAPTURE, &defaultInputCard, &defaultInputDevice);
  if (hasDefaultOutput) {
    inventory.alsaOutput = true;
  }
  if (hasDefaultInput) {
    inventory.alsaInput = true;
  }

  int card = -1;
  while (snd_card_next(&card) == 0 && card >= 0) {
    char controlName[16] = {};
    std::snprintf(controlName, sizeof controlName, "hw:%d", card);
    snd_ctl_t* control = nullptr;
    if (snd_ctl_open(&control, controlName, 0) < 0 || control == nullptr) continue;
    snd_ctl_card_info_t* cardInfo = nullptr;
    if (snd_ctl_card_info_malloc(&cardInfo) < 0) {
      snd_ctl_close(control);
      continue;
    }
    std::string cardId;
    std::string cardName;
    if (snd_ctl_card_info(control, cardInfo) >= 0) {
      cardId = snd_ctl_card_info_get_id(cardInfo);
      cardName = snd_ctl_card_info_get_name(cardInfo);
    }
    snd_ctl_card_info_free(cardInfo);
    if (cardId.empty()) {
      snd_ctl_close(control);
      continue;
    }
    int device = -1;
    while (snd_ctl_pcm_next_device(control, &device) == 0 && device >= 0) {
      snd_pcm_info_t* pcmInfo = nullptr;
      if (snd_pcm_info_malloc(&pcmInfo) < 0) break;
      snd_pcm_info_set_device(pcmInfo, static_cast<unsigned int>(device));
      snd_pcm_info_set_subdevice(pcmInfo, 0);
      snd_pcm_info_set_stream(pcmInfo, SND_PCM_STREAM_PLAYBACK);
      const bool playback = snd_ctl_pcm_info(control, pcmInfo) >= 0;
      snd_pcm_info_set_stream(pcmInfo, SND_PCM_STREAM_CAPTURE);
      const bool capture = snd_ctl_pcm_info(control, pcmInfo) >= 0;
      std::string pcmDescription;
      if (playback || capture) pcmDescription = snd_pcm_info_get_name(pcmInfo);
      snd_pcm_info_free(pcmInfo);
      if (!playback && !capture) continue;

      const std::string pcm = "hw:CARD=" + cardId + ",DEV=" + std::to_string(device);
      const std::string name = pcmDescription.empty() ? cardName : cardName + " " + pcmDescription;
      if (playback) {
        const PcmFacts facts = probeFacts(pcm, SND_PCM_STREAM_PLAYBACK);
        DeviceInfo info;
        info.api = DeviceApi::Alsa;
        info.id = makeDeviceId(DeviceApi::Alsa, pcm);
        info.node = pcm;
        info.name = name;
        info.channels = facts.channels;
        info.sampleRate = facts.rateOk ? 48000 : 0;
        info.isDefault = hasDefaultOutput && defaultOutputCard == card &&
                         defaultOutputDevice == device;
        inventory.outputs.push_back(std::move(info));
        inventory.alsaOutput = true;
      }
      if (capture) {
        const PcmFacts facts = probeFacts(pcm, SND_PCM_STREAM_CAPTURE);
        DeviceInfo info;
        info.api = DeviceApi::Alsa;
        info.id = makeDeviceId(DeviceApi::Alsa, pcm);
        info.node = pcm;
        info.name = name;
        info.channels = facts.channels;
        info.sampleRate = facts.rateOk ? 48000 : 0;
        info.isDefault = hasDefaultInput && defaultInputCard == card &&
                         defaultInputDevice == device;
        inventory.inputs.push_back(std::move(info));
        inventory.alsaInput = true;
      }
    }
    snd_ctl_close(control);
  }
#endif
  return inventory;
}

std::unique_ptr<DeviceBackend> createAlsaBackend(std::string* error) {
  if (error != nullptr) error->clear();
#if MNKY_HAVE_ALSA
  return std::make_unique<AlsaBackend>();
#else
  if (error != nullptr) *error = "alsa: libasound is not available in this build";
  return nullptr;
#endif
}

}  // namespace mnky::audio

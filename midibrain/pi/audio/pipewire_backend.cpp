#include "pipewire_backend.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if MNKY_HAVE_PIPEWIRE
#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <spa/param/audio/raw.h>
#include <spa/param/buffers.h>
#include <spa/pod/builder.h>
#include <spa/pod/parser.h>
#include <spa/utils/result.h>
#endif

namespace mnky::audio {
namespace {

#if MNKY_HAVE_PIPEWIRE

constexpr std::uint32_t kEngineRate = 48000;
constexpr int kScratchFrames = 8192;
constexpr int kNegotiateTimeoutMs = 3000;
constexpr int kCaptureGraceMs = 500;
constexpr int kEnumerationTimeoutMs = 2000;

void ensurePipeWireInit() {
  static std::once_flag once;
  std::call_once(once, [] { pw_init(nullptr, nullptr); });
}

SampleFormat formatFromSpa(std::uint32_t format) {
  switch (format) {
    case SPA_AUDIO_FORMAT_F32_LE:
    case SPA_AUDIO_FORMAT_F32_BE:
      return SampleFormat::F32;
    case SPA_AUDIO_FORMAT_S32_LE:
    case SPA_AUDIO_FORMAT_S32_BE:
      return SampleFormat::S32;
    case SPA_AUDIO_FORMAT_S24_LE:
    case SPA_AUDIO_FORMAT_S24_BE:
      return SampleFormat::S24_3;
    case SPA_AUDIO_FORMAT_S24_32_LE:
    case SPA_AUDIO_FORMAT_S24_32_BE:
      return SampleFormat::S24_32;
    case SPA_AUDIO_FORMAT_S16_LE:
    case SPA_AUDIO_FORMAT_S16_BE:
      return SampleFormat::S16;
    default:
      break;
  }
  return SampleFormat::Unknown;
}

spa_audio_format spaFromFormat(SampleFormat format) {
  switch (format) {
    case SampleFormat::F32:
      return SPA_AUDIO_FORMAT_F32_LE;
    case SampleFormat::S32:
      return SPA_AUDIO_FORMAT_S32_LE;
    case SampleFormat::S24_3:
      return SPA_AUDIO_FORMAT_S24_LE;  // packed 3-byte 24-bit
    case SampleFormat::S24_32:
      return SPA_AUDIO_FORMAT_S24_32_LE;  // 24-bit in 32-bit words
    case SampleFormat::S16:
      return SPA_AUDIO_FORMAT_S16_LE;
    case SampleFormat::Unknown:
      break;
  }
  return SPA_AUDIO_FORMAT_UNKNOWN;
}

/// SPA channel position from a PipeWire/ALSA-style name. Unknown names keep
/// SPA_AUDIO_CHANNEL_UNKNOWN so a wrong map cannot silently move audio.
std::uint32_t positionFromName(const std::string& name) {
  static const struct {
    const char* name;
    std::uint32_t position;
  } kPositions[] = {
      {"MONO", SPA_AUDIO_CHANNEL_MONO}, {"FL", SPA_AUDIO_CHANNEL_FL},
      {"FR", SPA_AUDIO_CHANNEL_FR},     {"FC", SPA_AUDIO_CHANNEL_FC},
      {"LFE", SPA_AUDIO_CHANNEL_LFE},   {"SL", SPA_AUDIO_CHANNEL_SL},
      {"SR", SPA_AUDIO_CHANNEL_SR},     {"RL", SPA_AUDIO_CHANNEL_RL},
      {"RR", SPA_AUDIO_CHANNEL_RR},     {"RC", SPA_AUDIO_CHANNEL_RC},
      {"FLC", SPA_AUDIO_CHANNEL_FLC},   {"FRC", SPA_AUDIO_CHANNEL_FRC},
      {"TC", SPA_AUDIO_CHANNEL_TC},     {"TFL", SPA_AUDIO_CHANNEL_TFL},
      {"TFC", SPA_AUDIO_CHANNEL_TFC},   {"TFR", SPA_AUDIO_CHANNEL_TFR},
      {"TRL", SPA_AUDIO_CHANNEL_TRL},   {"TRC", SPA_AUDIO_CHANNEL_TRC},
      {"TRR", SPA_AUDIO_CHANNEL_TRR},   {"UNKNOWN", SPA_AUDIO_CHANNEL_UNKNOWN},
      {"NA", SPA_AUDIO_CHANNEL_NA},
  };
  for (const auto& entry : kPositions) {
    if (name == entry.name) return entry.position;
  }
  if (name.rfind("AUX", 0) == 0) {
    const int index = std::atoi(name.c_str() + 3);
    if (index >= 0 && index <= 31) {
      return static_cast<std::uint32_t>(SPA_AUDIO_CHANNEL_AUX0 + index);
    }
  }
  return SPA_AUDIO_CHANNEL_UNKNOWN;
}

int channelsFromProps(const spa_dict* props) {
  if (props == nullptr) return 0;
  const char* value = spa_dict_lookup(props, SPA_KEY_AUDIO_CHANNELS);
  if (value == nullptr) return 0;
  return std::max(0, std::atoi(value));
}

// ----------------------------------------------------------------- enumeration

struct EnumState;

struct NodeProbe {
  pw_node* proxy = nullptr;
  spa_hook hook{};
  EnumState* state = nullptr;
  bool sink = false;
  std::size_t index = 0;
};

struct EnumState {
  pw_main_loop* loop = nullptr;
  pw_core* core = nullptr;
  pw_registry* registry = nullptr;
  DeviceInventory inventory;
  pw_node_events nodeEvents{};
  std::vector<std::unique_ptr<NodeProbe>> nodes;
  int syncDone = 0;
  bool timedOut = false;
};

void onEnumTimeout(void* data, std::uint64_t /*expirations*/) {
  auto* state = static_cast<EnumState*>(data);
  state->timedOut = true;
  pw_main_loop_quit(state->loop);
}

void onNodeInfo(void* data, const pw_node_info* info) {
  auto* probe = static_cast<NodeProbe*>(data);
  if (probe == nullptr || probe->state == nullptr || info == nullptr) return;
  if (info->props == nullptr) return;
  std::vector<DeviceInfo>& devices =
      probe->sink ? probe->state->inventory.outputs : probe->state->inventory.inputs;
  if (probe->index >= devices.size()) return;
  DeviceInfo& device = devices[probe->index];
  // node info carries audio.channels/audio.position; the registry global
  // properties do not, so the real width comes from here.
  const int channels = channelsFromProps(info->props);
  if (channels > 0) device.channels = channels;
  if (const char* positions = spa_dict_lookup(info->props, SPA_KEY_AUDIO_POSITION)) {
    device.positions.clear();
    std::string list = positions;
    std::size_t start = 0;
    while (start <= list.size()) {
      const std::size_t comma = list.find(',', start);
      const std::string name =
          list.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
      if (!name.empty()) device.positions.push_back(name);
      if (comma == std::string::npos) break;
      start = comma + 1;
    }
  }
  if (const char* description = spa_dict_lookup(info->props, PW_KEY_NODE_DESCRIPTION)) {
    if (*description != '\0') device.name = description;
  }
}

void onRegistryGlobal(void* data, std::uint32_t id, std::uint32_t /*permissions*/,
                      const char* type, std::uint32_t /*version*/,
                      const spa_dict* props) {
  auto* state = static_cast<EnumState*>(data);
  if (props == nullptr || std::strcmp(type, PW_TYPE_INTERFACE_Node) != 0) return;
  const char* mediaClass = spa_dict_lookup(props, PW_KEY_MEDIA_CLASS);
  if (mediaClass == nullptr) return;
  const bool sink = std::strcmp(mediaClass, "Audio/Sink") == 0;
  const bool source = std::strcmp(mediaClass, "Audio/Source") == 0;
  if (!sink && !source) return;
  const char* node = spa_dict_lookup(props, PW_KEY_NODE_NAME);
  if (node == nullptr || *node == '\0') return;
  const char* description = spa_dict_lookup(props, PW_KEY_NODE_DESCRIPTION);
  const char* nick = spa_dict_lookup(props, PW_KEY_NODE_NICK);
  DeviceInfo info;
  info.api = DeviceApi::PipeWire;
  info.node = node;
  info.id = makeDeviceId(DeviceApi::PipeWire, node);
  info.name = description != nullptr && *description != '\0'
                  ? description
                  : (nick != nullptr && *nick != '\0' ? nick : node);
  info.sampleRate = static_cast<int>(kEngineRate);
  // The channel width is filled in from the bound node's info event; keep the
  // address stable by pushing the DeviceInfo first and pointing at it.
  if (sink) {
    state->inventory.outputs.push_back(std::move(info));
    state->inventory.pipewireOutput = true;
  } else {
    state->inventory.inputs.push_back(std::move(info));
    state->inventory.pipewireInput = true;
  }
  auto probe = std::make_unique<NodeProbe>();
  probe->state = state;
  probe->sink = sink;
  probe->index = sink ? state->inventory.outputs.size() - 1
                      : state->inventory.inputs.size() - 1;
  probe->proxy = reinterpret_cast<pw_node*>(pw_registry_bind(
      state->registry, id, PW_TYPE_INTERFACE_Node, PW_VERSION_NODE, 0));
  if (probe->proxy == nullptr) return;
  spa_zero(probe->hook);
  pw_node_add_listener(probe->proxy, &probe->hook, &state->nodeEvents, probe.get());
  state->nodes.push_back(std::move(probe));
}

void onEnumCoreDone(void* data, std::uint32_t id, int seq) {
  auto* state = static_cast<EnumState*>(data);
  if (id != PW_ID_CORE || seq < 0) return;
  // First roundtrip delivers the globals (and binds the nodes); the second one
  // delivers every node's info event before it completes.
  if (state->syncDone++ == 0) {
    (void)pw_core_sync(state->core, PW_ID_CORE, 0);
    return;
  }
  pw_main_loop_quit(state->loop);
}

DeviceInventory enumeratePipeWire() {
  DeviceInventory empty;
  ensurePipeWireInit();
  EnumState state;
  state.loop = pw_main_loop_new(nullptr);
  if (state.loop == nullptr) return empty;
  pw_context* context = pw_context_new(pw_main_loop_get_loop(state.loop), nullptr, 0);
  if (context == nullptr) {
    pw_main_loop_destroy(state.loop);
    return empty;
  }
  pw_core* core = pw_context_connect(context, nullptr, 0);
  if (core == nullptr) {
    pw_context_destroy(context);
    pw_main_loop_destroy(state.loop);
    return empty;
  }
  pw_registry* registry = pw_core_get_registry(core, PW_VERSION_REGISTRY, 0);
  if (registry == nullptr) {
    pw_core_disconnect(core);
    pw_context_destroy(context);
    pw_main_loop_destroy(state.loop);
    return empty;
  }
  state.core = core;
  state.registry = registry;
  state.nodeEvents.version = PW_VERSION_NODE_EVENTS;
  state.nodeEvents.info = onNodeInfo;
  spa_hook registryHook;
  spa_hook coreHook;
  spa_zero(registryHook);
  spa_zero(coreHook);
  pw_registry_events registryEvents;
  spa_zero(registryEvents);
  registryEvents.version = PW_VERSION_REGISTRY_EVENTS;
  registryEvents.global = onRegistryGlobal;
  pw_core_events coreEvents;
  spa_zero(coreEvents);
  coreEvents.version = PW_VERSION_CORE_EVENTS;
  coreEvents.done = onEnumCoreDone;
  pw_registry_add_listener(registry, &registryHook, &registryEvents, &state);
  pw_core_add_listener(core, &coreHook, &coreEvents, &state);
  struct spa_source* timer =
      pw_loop_add_timer(pw_main_loop_get_loop(state.loop), onEnumTimeout, &state);
  timespec timeout{kEnumerationTimeoutMs / 1000, 0};
  timespec interval{0, 0};
  if (timer != nullptr) {
    pw_loop_update_timer(pw_main_loop_get_loop(state.loop), timer, &timeout, &interval,
                         false);
  }
  (void)pw_core_sync(core, PW_ID_CORE, 0);
  pw_main_loop_run(state.loop);
  if (timer != nullptr) pw_loop_destroy_source(pw_main_loop_get_loop(state.loop), timer);
  for (auto& probe : state.nodes) {
    spa_hook_remove(&probe->hook);
    if (probe->proxy != nullptr) pw_proxy_destroy(reinterpret_cast<pw_proxy*>(probe->proxy));
  }
  state.nodes.clear();
  spa_hook_remove(&registryHook);
  spa_hook_remove(&coreHook);
  pw_proxy_destroy(reinterpret_cast<pw_proxy*>(registry));
  pw_core_disconnect(core);
  pw_context_destroy(context);
  pw_main_loop_destroy(state.loop);
  return state.inventory;
}

// --------------------------------------------------------------------- backend

class PipeWireBackend;

/// Per-stream listener data: the same callbacks serve playback and capture, and
/// the stream they belong to decides which negotiated state they update.
struct StreamContext {
  PipeWireBackend* backend = nullptr;
  bool output = false;
  spa_hook listener{};
  pw_stream* stream = nullptr;
  std::atomic<int> channels{0};
  std::atomic<SampleFormat> format{SampleFormat::Unknown};
  std::atomic<int> rate{0};
  std::atomic<int> bufferFrames{0};
  std::atomic<int> period{0};
};

class PipeWireBackend final : public DeviceBackend {
 public:
  PipeWireBackend() {
    playbackEvents_.version = PW_VERSION_STREAM_EVENTS;
    playbackEvents_.state_changed = &PipeWireBackend::onStateChanged;
    playbackEvents_.param_changed = &PipeWireBackend::onParamChanged;
    playbackEvents_.process = &PipeWireBackend::onPlaybackProcess;
    captureEvents_.version = PW_VERSION_STREAM_EVENTS;
    captureEvents_.state_changed = &PipeWireBackend::onStateChanged;
    captureEvents_.param_changed = &PipeWireBackend::onParamChanged;
    captureEvents_.process = &PipeWireBackend::onCaptureProcess;
  }
  ~PipeWireBackend() override { close(); }

  DeviceApi api() const override { return DeviceApi::PipeWire; }
  std::vector<DeviceInfo> enumerateOutputs() override {
    return enumeratePipeWireDevices().outputs;
  }
  std::vector<DeviceInfo> enumerateInputs() override {
    return enumeratePipeWireDevices().inputs;
  }

  bool open(const DeviceConfig& config, DeviceClient* client,
            std::string* error) override {
    close();
    if (error != nullptr) error->clear();
    if (client == nullptr) {
      if (error != nullptr) *error = "pipewire: no device client";
      return false;
    }
    if (config.sampleRate <= 0.0 || std::abs(config.sampleRate - 48000.0) > 0.5) {
      if (error != nullptr) *error = "pipewire: only 48 kHz is supported";
      return false;
    }
    ensurePipeWireInit();
    client_ = client;
    sampleRate_ = 48000.0;
    wantedOutputChannels_ = config.outputChannels > 0 ? config.outputChannels : 2;
    wantedInputChannels_ = config.inputChannels > 0 ? config.inputChannels : kMaxInputs;
    wantedPeriod_ = config.blockFrames > 0 ? config.blockFrames : kBlockSize;
    streamName_ = config.streamName.empty() ? "mnkybox" : config.streamName;

    loop_ = pw_main_loop_new(nullptr);
    if (loop_ == nullptr) {
      if (error != nullptr) *error = "pipewire: cannot create a main loop";
      return false;
    }
    quitSource_ = pw_loop_add_event(pw_main_loop_get_loop(loop_), onQuitEvent, this);
    context_ = pw_context_new(pw_main_loop_get_loop(loop_), nullptr, 0);
    if (context_ == nullptr) {
      if (error != nullptr) *error = "pipewire: cannot create a context";
      close();
      return false;
    }
    core_ = pw_context_connect(context_, nullptr, 0);
    if (core_ == nullptr) {
      if (error != nullptr) *error = "pipewire: cannot connect to the server";
      close();
      return false;
    }

    if (!createStream(&playback_, config.outputId, wantedOutputChannels_,
                      config.outputPositions, error)) {
      close();
      return false;
    }
    if (!config.inputId.empty() &&
        !createStream(&capture_, config.inputId, wantedInputChannels_,
                      config.inputPositions, &inputError_)) {
      inputError_ = "capture stream refused: " + inputError_;
    }
    renderFloats_.assign(static_cast<std::size_t>(kScratchFrames) * wantedOutputChannels_,
                         0.0f);
    captureFloats_.assign(static_cast<std::size_t>(kScratchFrames) * wantedInputChannels_,
                          0.0f);

    targetLabel_ = config.outputId.empty() ? "default sink" : config.outputId;
    thread_ = std::thread([this] { loopThread(); });

    {
      std::unique_lock<std::mutex> lock(mutex_);
      const auto deadline =
          std::chrono::steady_clock::now() + std::chrono::milliseconds(kNegotiateTimeoutMs);
      negotiated_.wait_until(lock, deadline, [this] {
        return !lastError_.empty() ||
               playback_.format.load() != SampleFormat::Unknown;
      });
      if (lastError_.empty() &&
          playback_.format.load() == SampleFormat::Unknown) {
        lastError_ = "pipewire: playback stream negotiation timed out";
      }
      if (capture_.stream != nullptr && lastError_.empty() &&
          capture_.format.load() == SampleFormat::Unknown) {
        negotiated_.wait_for(lock, std::chrono::milliseconds(kCaptureGraceMs), [this] {
          return !lastError_.empty() ||
                 capture_.format.load() != SampleFormat::Unknown;
        });
        if (capture_.format.load() == SampleFormat::Unknown) {
          inputError_ = "capture stream negotiation timed out";
        }
      }
      if (!lastError_.empty()) {
        if (error != nullptr) *error = lastError_;
        lock.unlock();
        close();
        return false;
      }
      if (playback_.rate.load() != static_cast<int>(kEngineRate) ||
          playback_.format.load() == SampleFormat::Unknown ||
          playback_.channels.load() <= 0) {
        lastError_ = "pipewire: negotiated " + std::to_string(playback_.channels.load()) +
                     " channels at " + std::to_string(playback_.rate.load()) + " Hz " +
                     sampleFormatName(playback_.format.load());
        if (error != nullptr) *error = lastError_;
        lock.unlock();
        close();
        return false;
      }
      // A narrower negotiation is legal (the graph downmixes), but on a
      // multichannel interface it is the stereo-first failure this backend
      // exists to prevent, so it stays visible in the description.
      negotiatedNarrower_ = playback_.channels.load() < wantedOutputChannels_;
    }

    outputChannels_ = playback_.channels.load();
    inputChannels_ =
        capture_.format.load() != SampleFormat::Unknown ? capture_.channels.load() : 0;
    // The requested 128-frame quantum is the initial report; playbackProcess()
    // replaces it with the frames the graph actually hands us.
    period_.store(std::clamp(wantedPeriod_, kMinPeriodFrames, kBlockSize * 64));
    description_ = "pipewire / " + streamName_ + " -> " + targetLabel_ + " (" +
                   std::to_string(outputChannels_) + " out " +
                   sampleFormatName(playback_.format.load()) + ")";
    if (inputChannels_ > 0) {
      description_ += " + " + std::to_string(inputChannels_) + " in " +
                      sampleFormatName(capture_.format.load());
    } else if (!inputError_.empty()) {
      description_ += " [no capture: " + inputError_ + "]";
    }
    if (negotiatedNarrower_) {
      description_ += " [negotiated " + std::to_string(outputChannels_) + " of " +
                      std::to_string(wantedOutputChannels_) + " requested channels]";
    }
    description_ += " " + std::to_string(static_cast<int>(sampleRate_)) + " Hz / " +
                    std::to_string(period_.load()) + " frames";
    return true;
  }

  void close() override {
    active_.store(false);
    if (thread_.joinable()) {
      if (loop_ != nullptr && quitSource_ != nullptr) {
        pw_loop_signal_event(pw_main_loop_get_loop(loop_), quitSource_);
      }
      thread_.join();
    } else {
      teardownInLoop();
    }
    client_ = nullptr;
    outputChannels_ = 0;
    inputChannels_ = 0;
    period_.store(0);
    description_.clear();
    inputError_.clear();
    targetLabel_.clear();
    lastError_.clear();
    running_.store(false);
  }

  bool start(std::string* error) override {
    if (error != nullptr) error->clear();
    if (playback_.stream == nullptr) {
      if (error != nullptr) *error = "pipewire: no playback stream";
      return false;
    }
    if (!lastError_.empty()) {
      if (error != nullptr) *error = lastError_;
      return false;
    }
    // The streams are connected active; start() only opens the device client
    // gate. pw_stream_set_active is loop-thread-only in PipeWire 1.4.
    active_.store(true);
    running_.store(true);
    return true;
  }

  void stop() override {
    active_.store(false);
    running_.store(false);
  }

  bool running() const override { return running_.load(std::memory_order_relaxed); }
  double sampleRate() const override { return sampleRate_; }
  int periodFrames() const override { return period_.load(); }
  int outputChannels() const override { return outputChannels_; }
  int inputChannels() const override { return inputChannels_; }
  std::uint64_t xruns() const override { return xruns_.load(std::memory_order_relaxed); }
  std::string description() const override { return description_; }

 private:
  void loopThread() {
    pw_main_loop_run(loop_);
    teardownInLoop();
  }

  /// Runs on the loop thread: PipeWire proxies may only be destroyed from the
  /// thread that owns the loop.
  void teardownInLoop() {
    if (playback_.stream != nullptr) {
      pw_stream_destroy(playback_.stream);
      resetStream(playback_);
    }
    if (capture_.stream != nullptr) {
      pw_stream_destroy(capture_.stream);
      resetStream(capture_);
    }
    if (loop_ != nullptr && quitSource_ != nullptr) {
      pw_loop_destroy_source(pw_main_loop_get_loop(loop_), quitSource_);
      quitSource_ = nullptr;
    }
    if (core_ != nullptr) {
      pw_core_disconnect(core_);
      core_ = nullptr;
    }
    if (context_ != nullptr) {
      pw_context_destroy(context_);
      context_ = nullptr;
    }
    if (loop_ != nullptr) {
      pw_main_loop_destroy(loop_);
      loop_ = nullptr;
    }
    running_.store(false);
  }

  static void resetStream(StreamContext& context) {
    context.stream = nullptr;
    context.backend = nullptr;
    context.output = false;
    context.channels.store(0);
    context.format.store(SampleFormat::Unknown);
    context.rate.store(0);
    context.bufferFrames.store(0);
    context.period.store(0);
    spa_zero(context.listener);
  }

  static void onQuitEvent(void* data, std::uint64_t /*count*/) {
    auto* self = static_cast<PipeWireBackend*>(data);
    pw_main_loop_quit(self->loop_);
  }

  bool createStream(StreamContext* context, const std::string& target, int channels,
                    const std::vector<std::string>& positions, std::string* error) {
    const bool output = context == &playback_;
    const std::string latency =
        std::to_string(wantedPeriod_) + "/" + std::to_string(kEngineRate);
    const std::string nodeName = output ? streamName_ : streamName_ + "-capture";
    pw_properties* props = pw_properties_new(
        PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY, output ? "Playback" : "Capture",
        PW_KEY_MEDIA_ROLE, "Production", PW_KEY_APP_NAME, "mnkybox", PW_KEY_NODE_NAME,
        nodeName.c_str(), PW_KEY_NODE_DESCRIPTION,
        output ? "mnkybox main out" : "mnkybox inputs", PW_KEY_NODE_LATENCY,
        latency.c_str(), nullptr);
    if (props == nullptr) {
      if (error != nullptr) *error = "pipewire: cannot create stream properties";
      return false;
    }
    if (!target.empty()) pw_properties_set(props, PW_KEY_TARGET_OBJECT, target.c_str());
    pw_stream* stream = pw_stream_new(core_, nodeName.c_str(), props);
    if (stream == nullptr) {
      if (error != nullptr) *error = "pipewire: cannot create a stream";
      return false;
    }
    context->stream = stream;
    context->output = output;
    context->backend = this;
    context->format.store(SampleFormat::Unknown);
    context->channels.store(0);
    context->rate.store(0);
    context->bufferFrames.store(0);
    context->period.store(0);
    spa_zero(context->listener);
    pw_stream_add_listener(stream, &context->listener,
                           output ? &playbackEvents_ : &captureEvents_, context);

    // Two EnumFormat alternatives: the first is the native format we prefer, the
    // second keeps a fixed-point card working without a conversion plugin.
    std::uint8_t podBuffer[4096];
    spa_pod_builder builder = SPA_POD_BUILDER_INIT(podBuffer, sizeof(podBuffer));
    const spa_pod* pods[2] = {nullptr, nullptr};
    int podCount = 0;
    const SampleFormat preference[2] = {
        output ? SampleFormat::F32 : SampleFormat::S32,
        output ? SampleFormat::S32 : SampleFormat::F32};
    for (const SampleFormat candidate : preference) {
      spa_audio_info_raw info;
      spa_zero(info);
      info.format = spaFromFormat(candidate);
      info.rate = kEngineRate;
      info.channels = static_cast<std::uint32_t>(channels);
      for (int channel = 0;
           channel < channels && channel < static_cast<int>(SPA_AUDIO_MAX_CHANNELS);
           ++channel) {
        if (positions.size() == static_cast<std::size_t>(channels)) {
          info.position[channel] = positionFromName(positions[static_cast<std::size_t>(channel)]);
        } else if (channels == 1) {
          info.position[channel] = SPA_AUDIO_CHANNEL_MONO;
        } else if (channels == 2) {
          info.position[channel] = channel == 0 ? SPA_AUDIO_CHANNEL_FL : SPA_AUDIO_CHANNEL_FR;
        } else {
          // Multichannel interfaces (UMC1820 class) expose AUXn; an explicit
          // AUX map is what makes PipeWire link all N channels instead of the
          // stereo-first FL/FR pair.
          info.position[channel] =
              static_cast<std::uint32_t>(SPA_AUDIO_CHANNEL_AUX0 + channel);
        }
      }
      const spa_pod* pod =
          spa_format_audio_raw_build(&builder, SPA_PARAM_EnumFormat, &info);
      if (pod != nullptr) pods[podCount++] = pod;
    }
    // PipeWire's direction is the data-flow direction as seen from the graph:
    // a playback stream PRODUCES (PW_DIRECTION_OUTPUT), a capture stream
    // CONSUMES (PW_DIRECTION_INPUT). Getting this backwards turns playback
    // into a microphone consumer and capture into a source (found on the Pi).
    const pw_direction direction = output ? PW_DIRECTION_OUTPUT : PW_DIRECTION_INPUT;
    constexpr int kFlags =
        PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS | PW_STREAM_FLAG_RT_PROCESS;
    const int connected = pw_stream_connect(
        stream, direction, PW_ID_ANY, static_cast<pw_stream_flags>(kFlags), pods, podCount);
    if (connected < 0) {
      if (error != nullptr) {
        *error = std::string("pipewire: cannot connect the ") +
                 (output ? "playback" : "capture") + " stream: " +
                 spa_strerror(connected);
      }
      return false;
    }
    return true;
  }

  static void onStateChanged(void* data, pw_stream_state /*old*/, pw_stream_state state,
                             const char* error) {
    auto* context = static_cast<StreamContext*>(data);
    if (state != PW_STREAM_STATE_ERROR) return;
    context->backend->running_.store(false);
    std::lock_guard<std::mutex> lock(context->backend->mutex_);
    context->backend->lastError_ =
        std::string("pipewire: ") + (error != nullptr ? error : "stream error");
    context->backend->negotiated_.notify_all();
  }

  static void onParamChanged(void* data, std::uint32_t id, const spa_pod* param) {
    auto* context = static_cast<StreamContext*>(data);
    if (param == nullptr) return;
    if (id == SPA_PARAM_Format) {
      spa_audio_info_raw info;
      spa_zero(info);
      if (spa_format_audio_raw_parse(param, &info) < 0) return;
      const SampleFormat format = formatFromSpa(info.format);
      if (format == SampleFormat::Unknown) {
        std::lock_guard<std::mutex> lock(context->backend->mutex_);
        context->backend->lastError_ = "pipewire: unsupported negotiated sample format";
        context->backend->negotiated_.notify_all();
        return;
      }
      context->format.store(format);
      context->channels.store(static_cast<int>(info.channels));
      context->rate.store(static_cast<int>(info.rate));
      std::lock_guard<std::mutex> lock(context->backend->mutex_);
      context->backend->negotiated_.notify_all();
      return;
    }
    if (id == SPA_PARAM_Buffers) {
      std::int32_t size = 0;
      std::int32_t stride = 0;
      if (spa_pod_parse_object(param, SPA_TYPE_OBJECT_ParamBuffers, nullptr,
                               SPA_PARAM_BUFFERS_size, SPA_POD_Int(&size),
                               SPA_PARAM_BUFFERS_stride, SPA_POD_Int(&stride)) < 0) {
        return;
      }
      if (stride <= 0) return;
      context->bufferFrames.store(size / stride);
    }
  }

  static void onPlaybackProcess(void* data) {
    static_cast<StreamContext*>(data)->backend->playbackProcess();
  }
  static void onCaptureProcess(void* data) {
    static_cast<StreamContext*>(data)->backend->captureProcess();
  }

  pw_stream_events playbackEvents_ = {};
  pw_stream_events captureEvents_ = {};

  void playbackProcess() {
    pw_buffer* buffer = pw_stream_dequeue_buffer(playback_.stream);
    if (buffer == nullptr) {
      xruns_.fetch_add(1, std::memory_order_relaxed);
      return;
    }
    spa_buffer* spaBuffer = buffer->buffer;
    spa_data* data = spaBuffer != nullptr ? &spaBuffer->datas[0] : nullptr;
    const int channels = playback_.channels.load();
    const SampleFormat format = playback_.format.load();
    const int stride = frameBytes(format, channels);
    if (data == nullptr || data->data == nullptr || channels <= 0 || stride <= 0) {
      pw_stream_queue_buffer(playback_.stream, buffer);
      return;
    }
    int frames = static_cast<int>(buffer->requested);
    const int capacity = static_cast<int>(renderFloats_.size()) / channels;
    if (frames <= 0) frames = static_cast<int>(data->maxsize) / stride;
    if (frames > capacity) frames = capacity;
    if (frames <= 0) {
      pw_stream_queue_buffer(playback_.stream, buffer);
      return;
    }
    playback_.period.store(frames);
    period_.store(frames);
    if (!active_.load(std::memory_order_relaxed)) {
      std::fill(renderFloats_.data(),
                renderFloats_.data() + static_cast<std::size_t>(frames) * channels, 0.0f);
    } else {
      client_->renderDevice(renderFloats_.data(), frames, channels);
    }
    encodeInterleaved(renderFloats_.data(), channels, format, frames,
                      static_cast<unsigned char*>(data->data));
    data->chunk->offset = 0;
    data->chunk->stride = stride;
    data->chunk->size = static_cast<std::uint32_t>(frames) * static_cast<std::uint32_t>(stride);
    pw_stream_queue_buffer(playback_.stream, buffer);
  }

  void captureProcess() {
    pw_buffer* buffer = nullptr;
    while ((buffer = pw_stream_dequeue_buffer(capture_.stream)) != nullptr) {
      spa_buffer* spaBuffer = buffer->buffer;
      spa_data* data = spaBuffer != nullptr ? &spaBuffer->datas[0] : nullptr;
      const int channels = capture_.channels.load();
      const SampleFormat format = capture_.format.load();
      const int stride = frameBytes(format, channels);
      if (data == nullptr || data->data == nullptr || channels <= 0 || stride <= 0) {
        pw_stream_queue_buffer(capture_.stream, buffer);
        continue;
      }
      const std::uint32_t offset = data->chunk != nullptr ? data->chunk->offset : 0;
      const std::uint32_t bytes = data->chunk != nullptr ? data->chunk->size : data->maxsize;
      int frames = static_cast<int>(bytes / static_cast<std::uint32_t>(stride));
      const int capacity = static_cast<int>(captureFloats_.size()) / channels;
      if (frames > capacity) frames = capacity;
      if (frames > 0 && active_.load(std::memory_order_relaxed)) {
        decodeInterleaved(static_cast<const unsigned char*>(data->data) + offset, format,
                          channels, frames, captureFloats_.data());
        client_->captureDevice(captureFloats_.data(), frames, channels);
      }
      pw_stream_queue_buffer(capture_.stream, buffer);
    }
  }

  pw_main_loop* loop_ = nullptr;
  spa_source* quitSource_ = nullptr;
  pw_context* context_ = nullptr;
  pw_core* core_ = nullptr;
  StreamContext playback_;
  StreamContext capture_;
  DeviceClient* client_ = nullptr;
  double sampleRate_ = 48000.0;
  int wantedOutputChannels_ = 2;
  int wantedInputChannels_ = kMaxInputs;
  int wantedPeriod_ = kBlockSize;
  int outputChannels_ = 0;
  int inputChannels_ = 0;
  std::atomic<int> period_{0};
  std::string streamName_ = "mnkybox";
  std::string targetLabel_;
  std::string description_;
  std::string inputError_;
  std::string lastError_;
  std::vector<float> renderFloats_;
  std::vector<float> captureFloats_;
  bool negotiatedNarrower_ = false;
  std::atomic<bool> active_{false};
  std::atomic<bool> running_{false};
  std::atomic<std::uint64_t> xruns_{0};
  std::mutex mutex_;
  std::condition_variable negotiated_;
  std::thread thread_;
};

#else  // !MNKY_HAVE_PIPEWIRE

class PipeWirePlaceholder final : public DeviceBackend {
 public:
  DeviceApi api() const override { return DeviceApi::PipeWire; }
  std::vector<DeviceInfo> enumerateOutputs() override { return {}; }
  std::vector<DeviceInfo> enumerateInputs() override { return {}; }
  bool open(const DeviceConfig&, DeviceClient*, std::string* error) override {
    if (error != nullptr) *error = "pipewire: libpipewire is not available in this build";
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
  std::string description() const override { return "pipewire (not compiled)"; }
};

#endif

}  // namespace

bool pipewireCompiled() {
#if MNKY_HAVE_PIPEWIRE
  return true;
#else
  return false;
#endif
}

DeviceInventory enumeratePipeWireDevices() {
#if MNKY_HAVE_PIPEWIRE
  return enumeratePipeWire();
#else
  return {};
#endif
}

std::unique_ptr<DeviceBackend> createPipeWireBackend(std::string* error) {
  if (error != nullptr) error->clear();
#if MNKY_HAVE_PIPEWIRE
  return std::make_unique<PipeWireBackend>();
#else
  if (error != nullptr) *error = "pipewire: libpipewire is not available in this build";
  return nullptr;
#endif
}

}  // namespace mnky::audio

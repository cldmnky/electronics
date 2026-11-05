#pragma once
// Audio device I/O seam for the mnkybox core: the engine renders 48 kHz float32
// blocks, and every real audio interface is reached through one DeviceBackend
// (native libpipewire in the product path, snd_pcm as the fallback). The Qt app
// owns exactly one backend at a time; Qt Multimedia no longer carries audio.
//
// Contract for implementations:
//  - Device frames are interleaved float32 at 48 kHz, in device channel order.
//    Sample-format conversion (S32_LE, S24_3LE, F32_LE, ...) happens inside the
//    backend through the exact helpers below, never through a reinterpret_cast.
//  - The requested period is kBlockSize (128) frames. A backend that negotiates
//    a larger period must split it into <= kBlockSize render calls, because the
//    engine's block size is a hard ceiling.
//  - DeviceClient::renderDevice()/captureDevice() are invoked from one backend
//    thread and never concurrently, so a client only needs single-thread
//    discipline inside each callback. The backend may call them with fewer
//    frames than the period (a partial buffer at shutdown).
//  - open()/start()/stop()/close() and enumerate() run on the control thread;
//    nothing there is called from the audio callback.
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "engine.hpp"

namespace mnky::audio {

/// Widest device output the app maps: UMC1820-class 20-channel playback
/// (18 analog + 2 S/PDIF). Input width is kMaxInputs (18).
inline constexpr int kMaxDeviceOutputs = 20;
/// Upper bound of the device channel slot table. DeviceOut values are clamped
/// to 0..31, so a wider interface still maps its first 32 channels.
inline constexpr int kMaxDeviceChannels = 32;
/// Smallest period a backend may report; a device that cannot do better is
/// split by the client instead.
inline constexpr int kMinPeriodFrames = 32;

/// Interleaved sample layouts the backends convert explicitly. The enum is
/// closed on purpose: every value has a tested encode/decode pair.
enum class SampleFormat : std::uint8_t {
  Unknown = 0,
  F32,     ///< 32-bit IEEE float little-endian (SND_PCM_FORMAT_FLOAT_LE)
  S32,     ///< 32-bit signed little-endian PCM
  S24_3,   ///< packed 24-bit signed little-endian in 3 bytes (S24_3LE)
  S24_32,  ///< 24-bit signed little-endian in the low 24 bits of a 32-bit word
  S16,     ///< 16-bit signed little-endian PCM
};

/// Bytes per sample; 0 for Unknown.
int bytesPerSample(SampleFormat format);
/// Short stable name for logs ("S32_LE", "S24_3LE", "F32_LE", "S16_LE").
const char* sampleFormatName(SampleFormat format);
/// Format used by `count` bytes per sample, Unknown when there is no match.
SampleFormat sampleFormatFromBytes(int bytes);

/// One interleaved frame's worth of bytes.
inline int frameBytes(SampleFormat format, int channels) {
  return bytesPerSample(format) * (channels > 0 ? channels : 0);
}

/// Decode one little-endian sample. Exact for every representable input:
/// S32 -1.0 is exactly -1.0f, F32 is a bit copy, S24_3 sign-extends from bit 23.
float decodeSample(const unsigned char* source, SampleFormat format);
/// Encode one sample. Clamped to [-1, 1] and rounded once with std::lround, so
/// +1.0 maps to the positive full scale and -1.0 to the negative full scale;
/// no wrap-around and no silent truncation. F32 stores the clamped value.
void encodeSample(unsigned char* destination, SampleFormat format, float value);

/// Decodes `frames` interleaved device frames into interleaved float32 with the
/// same channel count, one decodeSample() per sample.
void decodeInterleaved(const unsigned char* source, SampleFormat format,
                       int channels, int frames, float* destination);
/// Encodes `frames` interleaved float32 frames into the device layout.
void encodeInterleaved(const float* source, int channels, SampleFormat format,
                       int frames, unsigned char* destination);

enum class DeviceApi : std::uint8_t { PipeWire = 0, Alsa = 1, None = 2 };

const char* deviceApiName(DeviceApi api);
/// True when this build compiled the API's backend in (Linux with the dev
/// package); false on every other host, where createBackend() returns null.
bool deviceApiCompiled(DeviceApi api);

/// One enumerable endpoint with its REAL backend names: the PipeWire node.name
/// and node.description, or the ALSA PCM id and card/PCM description.
struct DeviceInfo {
  DeviceApi api = DeviceApi::None;
  std::string id;    ///< prefixed identifier: "pipewire:<node.name>" / "alsa:<pcm>"
  std::string name;  ///< human name: PipeWire node.description / ALSA card + PCM
  std::string node;  ///< raw backend identifier without the prefix
  int channels = 0;  ///< reported width, 0 when the backend cannot tell
  /// Channel positions as the node/card reports them ("AUX0".."AUX31", "FL",
  /// "FR", ...). PipeWire links channel by channel through these; a stream that
  /// omits them is linked stereo-first to a multichannel node (the exact bug
  /// the native backend replaces).
  std::vector<std::string> positions;
  int sampleRate = 0;  ///< 48000 when the endpoint supports the engine rate, else 0
  bool isDefault = false;
};

/// Device id helpers shared by the app, the backends and the tests.
std::string makeDeviceId(DeviceApi api, const std::string& node);
bool parseDeviceId(const std::string& id, DeviceApi* api, std::string* node);

/// Enumeration result for the whole host: both APIs, both directions.
struct DeviceInventory {
  std::vector<DeviceInfo> outputs;
  std::vector<DeviceInfo> inputs;
  bool pipewireOutput = false;  ///< a PipeWire default sink is available
  bool pipewireInput = false;   ///< a PipeWire default source is available
  bool alsaOutput = false;      ///< an ALSA default playback PCM opens
  bool alsaInput = false;       ///< an ALSA default capture PCM opens
};

/// Enumerates both APIs (control thread). Never throws; a missing server or
/// card simply contributes nothing.
DeviceInventory enumerateDevices();

/// What a backend is asked to open. `outputId`/`inputId` are unprefixed backend
/// names; empty output means the API default sink/card, empty input means "do
/// not capture". `outputChannels`/`inputChannels` of 0 mean "the endpoint's
/// reported width" (the backend negotiates and reports what it got).
struct DeviceConfig {
  double sampleRate = 48000.0;
  int blockFrames = kBlockSize;
  int outputChannels = 0;
  int inputChannels = 0;
  std::vector<std::string> outputPositions;
  std::vector<std::string> inputPositions;
  std::string outputId;
  std::string inputId;
  std::string streamName = "mnkybox";
};

/// Callbacks the backend drives. Implemented by the app's audio glue; the
/// default-free pure virtuals make the direction contract explicit.
class DeviceClient {
 public:
  virtual ~DeviceClient() = default;
  /// Pull `frames` frames of interleaved float32 for the playback device.
  virtual void renderDevice(float* interleaved, int frames, int channels) = 0;
  /// Push `frames` frames of interleaved float32 captured from the device.
  virtual void captureDevice(const float* interleaved, int frames, int channels) = 0;
};

/// One opened audio interface. Implementations convert to/from interleaved
/// float32 and own their real-time thread.
class DeviceBackend {
 public:
  DeviceBackend() = default;
  virtual ~DeviceBackend() = default;
  DeviceBackend(const DeviceBackend&) = delete;
  DeviceBackend& operator=(const DeviceBackend&) = delete;

  virtual DeviceApi api() const = 0;
  /// Enumerates this API's endpoints; control thread, safe while running.
  virtual std::vector<DeviceInfo> enumerateOutputs() = 0;
  virtual std::vector<DeviceInfo> enumerateInputs() = 0;

  virtual bool open(const DeviceConfig& config, DeviceClient* client,
                    std::string* error) = 0;
  virtual void close() = 0;
  virtual bool start(std::string* error) = 0;
  virtual void stop() = 0;
  virtual bool running() const = 0;

  virtual double sampleRate() const = 0;
  /// Negotiated period: the frames handed to one render call, clamped to
  /// kBlockSize or less. 0 before start.
  virtual int periodFrames() const = 0;
  virtual int outputChannels() const = 0;
  virtual int inputChannels() const = 0;
  /// Device underruns/overruns since open().
  virtual std::uint64_t xruns() const = 0;
  /// Real names and the negotiated stream layout, e.g.
  /// "pipewire / UMC1820 Multichannel (20 out, 18 in) 48000 Hz / 128 frames".
  virtual std::string description() const = 0;
};

/// Creates one backend. Returns null (with a reason) when this build has no
/// such API or the host has no server; callers then fall back to the other API.
std::unique_ptr<DeviceBackend> createBackend(DeviceApi api, std::string* error);

// --- device output routing ---------------------------------------------------
// The map itself is audio::DeviceOut (engine.hpp, session v2 deviceOut):
// Main = 0/1, OUT3&4 = 2/3, phones = 4/5 by default.

/// Which engine bus writes a device channel.
enum class DeviceOutSlot : std::uint8_t {
  None = 0,
  MainL,
  MainR,
  Out34L,
  Out34R,
  PhonesL,
  PhonesR,
};

/// Builds the per-channel slot table. A bus pair that points at the same device
/// channel keeps the first slot and leaves the second silent; Main wins over
/// OUT3&4, which wins over phones, so two buses cannot sum into one channel.
void buildDeviceOutMap(const DeviceOut& deviceOut, int deviceChannels,
                       DeviceOutSlot* slots);

/// Writes one engine block's three stereo buses into interleaved device frames
/// through `slots`. Bus pointers may be null (silent).
void mapBusesToDevice(const float* mainL, const float* mainR, const float* out34L,
                      const float* out34R, const float* phonesL,
                      const float* phonesR, const DeviceOutSlot* slots,
                      int deviceChannels, int frames, float* interleaved);

// --- device <-> engine block bridge -----------------------------------------
/// Interleaved device frames in, engine input planes out. The capture side runs
/// on the backend thread, the render side on the same thread for a single
/// backend, so the ring only needs relaxed atomics; it is sized for a whole
/// graph cycle of jitter (4096 frames = 85 ms at 48 kHz).
class DevicePump {
 public:
  static constexpr int kRingFrames = 4096;

  DevicePump() = default;
  DevicePump(const DevicePump&) = delete;
  DevicePump& operator=(const DevicePump&) = delete;

  void reset();

  /// Appends interleaved capture frames (device channel order). Channels past
  /// kMaxInputs are dropped; missing channels are silent.
  void pushCapture(const float* interleaved, int frames, int channels);
  /// Copies the next `frames` captured frames into `planes` (kMaxInputs
  /// pointers, each at least `frames` floats), zero-filling what has not
  /// arrived yet and skipping ahead when the capture side drifted so far that
  /// the backlog no longer fits. Returns the frames actually copied.
  int readCapture(float* const* planes, int frames);

  std::uint64_t capturedFrames() const {
    return captureFrames_.load(std::memory_order_relaxed);
  }
  std::uint64_t capturedBlocks() const {
    return captureBlocks_.load(std::memory_order_relaxed);
  }
  std::uint64_t droppedFrames() const {
    return captureDropped_.load(std::memory_order_relaxed);
  }

 private:
  float ring_[kMaxInputs][kRingFrames] = {};
  std::atomic<std::uint64_t> writeFrame_{0};
  std::uint64_t readFrame_ = 0;  // render side only
  std::atomic<std::uint64_t> captureFrames_{0};
  std::atomic<std::uint64_t> captureBlocks_{0};
  std::atomic<std::uint64_t> captureDropped_{0};
};

}  // namespace mnky::audio

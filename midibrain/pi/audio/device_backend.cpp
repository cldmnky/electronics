#include "device_backend.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "alsa_backend.hpp"
#include "pipewire_backend.hpp"

namespace mnky::audio {
namespace {

inline std::uint16_t readLe16(const unsigned char* bytes) {
  return static_cast<std::uint16_t>(
      static_cast<std::uint16_t>(bytes[0]) |
      static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[1]) << 8));
}

inline std::uint32_t readLe32(const unsigned char* bytes) {
  return static_cast<std::uint32_t>(bytes[0]) |
         (static_cast<std::uint32_t>(bytes[1]) << 8) |
         (static_cast<std::uint32_t>(bytes[2]) << 16) |
         (static_cast<std::uint32_t>(bytes[3]) << 24);
}

inline void writeLe16(unsigned char* bytes, std::uint16_t value) {
  bytes[0] = static_cast<unsigned char>(value & 0xFFu);
  bytes[1] = static_cast<unsigned char>((value >> 8) & 0xFFu);
}

inline void writeLe32(unsigned char* bytes, std::uint32_t value) {
  bytes[0] = static_cast<unsigned char>(value & 0xFFu);
  bytes[1] = static_cast<unsigned char>((value >> 8) & 0xFFu);
  bytes[2] = static_cast<unsigned char>((value >> 16) & 0xFFu);
  bytes[3] = static_cast<unsigned char>((value >> 24) & 0xFFu);
}

/// Sign-extend a 24-bit two's-complement value held in the low 24 bits.
inline std::int32_t signExtend24(std::uint32_t value) {
  const std::uint32_t sign = 0x800000u;
  const std::uint32_t extended = (value & sign) != 0 ? (value | 0xFF000000u) : (value & 0xFFFFFFu);
  return static_cast<std::int32_t>(extended);
}

inline float clampUnit(float value) {
  if (std::isnan(value)) return 0.0f;
  if (value < -1.0f) return -1.0f;
  return value > 1.0f ? 1.0f : value;
}

inline long roundSample(float value, double fullScale) {
  return std::lround(static_cast<double>(clampUnit(value)) * fullScale);
}

}  // namespace

int bytesPerSample(SampleFormat format) {
  switch (format) {
    case SampleFormat::F32:
    case SampleFormat::S32:
    case SampleFormat::S24_32:
      return 4;
    case SampleFormat::S24_3:
      return 3;
    case SampleFormat::S16:
      return 2;
    case SampleFormat::Unknown:
      break;
  }
  return 0;
}

const char* sampleFormatName(SampleFormat format) {
  switch (format) {
    case SampleFormat::F32:
      return "F32_LE";
    case SampleFormat::S32:
      return "S32_LE";
    case SampleFormat::S24_3:
      return "S24_3LE";
    case SampleFormat::S24_32:
      return "S24_32_LE";
    case SampleFormat::S16:
      return "S16_LE";
    case SampleFormat::Unknown:
      break;
  }
  return "unknown";
}

SampleFormat sampleFormatFromBytes(int bytes) {
  switch (bytes) {
    case 4:
      return SampleFormat::S32;
    case 3:
      return SampleFormat::S24_3;
    case 2:
      return SampleFormat::S16;
    default:
      break;
  }
  return SampleFormat::Unknown;
}

float decodeSample(const unsigned char* source, SampleFormat format) {
  if (source == nullptr) return 0.0f;
  switch (format) {
    case SampleFormat::F32: {
      const std::uint32_t bits = readLe32(source);
      float value = 0.0f;
      std::memcpy(&value, &bits, sizeof value);
      return value;
    }
    case SampleFormat::S32:
      return static_cast<float>(
          static_cast<double>(static_cast<std::int32_t>(readLe32(source))) / 2147483648.0);
    case SampleFormat::S24_3: {
      const std::uint32_t raw = static_cast<std::uint32_t>(source[0]) |
                                (static_cast<std::uint32_t>(source[1]) << 8) |
                                (static_cast<std::uint32_t>(source[2]) << 16);
      return static_cast<float>(static_cast<double>(signExtend24(raw)) / 8388608.0);
    }
    case SampleFormat::S24_32:
      return static_cast<float>(
          static_cast<double>(signExtend24(readLe32(source))) / 8388608.0);
    case SampleFormat::S16:
      return static_cast<float>(
          static_cast<double>(static_cast<std::int16_t>(readLe16(source))) / 32768.0);
    case SampleFormat::Unknown:
      break;
  }
  return 0.0f;
}

void encodeSample(unsigned char* destination, SampleFormat format, float value) {
  if (destination == nullptr) return;
  const float clamped = clampUnit(value);
  switch (format) {
    case SampleFormat::F32: {
      // Float devices carry the engine value unchanged (a bus may exceed unity
      // on purpose); only non-finite values are replaced with silence.
      const float stored = std::isfinite(value) ? value : 0.0f;
      std::uint32_t bits = 0;
      std::memcpy(&bits, &stored, sizeof bits);
      writeLe32(destination, bits);
      return;
    }
    case SampleFormat::S32:
      writeLe32(destination,
                static_cast<std::uint32_t>(static_cast<std::int32_t>(roundSample(clamped, 2147483647.0))));
      return;
    case SampleFormat::S24_3: {
      const auto value24 = static_cast<std::uint32_t>(
          static_cast<std::int32_t>(roundSample(clamped, 8388607.0)) & 0xFFFFFF);
      destination[0] = static_cast<unsigned char>(value24 & 0xFFu);
      destination[1] = static_cast<unsigned char>((value24 >> 8) & 0xFFu);
      destination[2] = static_cast<unsigned char>((value24 >> 16) & 0xFFu);
      return;
    }
    case SampleFormat::S24_32: {
      const auto value24 =
          static_cast<std::int32_t>(roundSample(clamped, 8388607.0)) & 0xFFFFFF;
      writeLe32(destination, static_cast<std::uint32_t>(signExtend24(static_cast<std::uint32_t>(value24))));
      return;
    }
    case SampleFormat::S16:
      writeLe16(destination,
                static_cast<std::uint16_t>(static_cast<std::int16_t>(roundSample(clamped, 32767.0))));
      return;
    case SampleFormat::Unknown:
      break;
  }
}

void decodeInterleaved(const unsigned char* source, SampleFormat format,
                       int channels, int frames, float* destination) {
  const int bytes = bytesPerSample(format);
  if (source == nullptr || destination == nullptr || bytes <= 0 || channels <= 0 ||
      frames <= 0) {
    return;
  }
  const std::size_t samples = static_cast<std::size_t>(channels) * frames;
  for (std::size_t sample = 0; sample < samples; ++sample) {
    destination[sample] = decodeSample(source + sample * bytes, format);
  }
}

void encodeInterleaved(const float* source, int channels, SampleFormat format,
                       int frames, unsigned char* destination) {
  const int bytes = bytesPerSample(format);
  if (source == nullptr || destination == nullptr || bytes <= 0 || channels <= 0 ||
      frames <= 0) {
    return;
  }
  const std::size_t samples = static_cast<std::size_t>(channels) * frames;
  for (std::size_t sample = 0; sample < samples; ++sample) {
    encodeSample(destination + sample * bytes, format, source[sample]);
  }
}

const char* deviceApiName(DeviceApi api) {
  switch (api) {
    case DeviceApi::PipeWire:
      return "pipewire";
    case DeviceApi::Alsa:
      return "alsa";
    case DeviceApi::None:
      break;
  }
  return "none";
}

bool deviceApiCompiled(DeviceApi api) {
  switch (api) {
    case DeviceApi::PipeWire:
      return pipewireCompiled();
    case DeviceApi::Alsa:
      return alsaCompiled();
    case DeviceApi::None:
      break;
  }
  return false;
}

std::string makeDeviceId(DeviceApi api, const std::string& node) {
  return std::string(deviceApiName(api)) + ":" + node;
}

bool parseDeviceId(const std::string& id, DeviceApi* api, std::string* node) {
  const std::size_t colon = id.find(':');
  if (colon == std::string::npos) return false;
  const std::string prefix = id.substr(0, colon);
  DeviceApi parsed = DeviceApi::None;
  if (prefix == "pipewire") {
    parsed = DeviceApi::PipeWire;
  } else if (prefix == "alsa") {
    parsed = DeviceApi::Alsa;
  } else {
    return false;
  }
  if (api != nullptr) *api = parsed;
  if (node != nullptr) *node = id.substr(colon + 1);
  return true;
}

DeviceInventory enumerateDevices() {
  const DeviceInventory pipewire = enumeratePipeWireDevices();
  const DeviceInventory alsa = enumerateAlsaDevices();
  DeviceInventory result;
  result.outputs = pipewire.outputs;
  result.outputs.insert(result.outputs.end(), alsa.outputs.begin(), alsa.outputs.end());
  result.inputs = pipewire.inputs;
  result.inputs.insert(result.inputs.end(), alsa.inputs.begin(), alsa.inputs.end());
  result.pipewireOutput = pipewire.pipewireOutput;
  result.pipewireInput = pipewire.pipewireInput;
  result.alsaOutput = alsa.alsaOutput;
  result.alsaInput = alsa.alsaInput;
  return result;
}

std::unique_ptr<DeviceBackend> createBackend(DeviceApi api, std::string* error) {
  if (error != nullptr) error->clear();
  switch (api) {
    case DeviceApi::PipeWire:
      return createPipeWireBackend(error);
    case DeviceApi::Alsa:
      return createAlsaBackend(error);
    case DeviceApi::None:
      break;
  }
  if (error != nullptr) *error = "no audio API selected";
  return nullptr;
}

void buildDeviceOutMap(const DeviceOut& deviceOut, int deviceChannels,
                       DeviceOutSlot* slots) {
  if (slots == nullptr || deviceChannels <= 0) return;
  for (int channel = 0; channel < deviceChannels; ++channel) slots[channel] = DeviceOutSlot::None;

  const auto place = [slots, deviceChannels](int channel, DeviceOutSlot slot) {
    if (channel < 0 || channel >= deviceChannels) return;
    if (slots[channel] != DeviceOutSlot::None) return;  // first bus wins
    slots[channel] = slot;
  };
  place(deviceOut.main[0], DeviceOutSlot::MainL);
  place(deviceOut.main[1], DeviceOutSlot::MainR);
  place(deviceOut.out34[0], DeviceOutSlot::Out34L);
  place(deviceOut.out34[1], DeviceOutSlot::Out34R);
  place(deviceOut.phones[0], DeviceOutSlot::PhonesL);
  place(deviceOut.phones[1], DeviceOutSlot::PhonesR);
}

void mapBusesToDevice(const float* mainL, const float* mainR, const float* out34L,
                      const float* out34R, const float* phonesL,
                      const float* phonesR, const DeviceOutSlot* slots,
                      int deviceChannels, int frames, float* interleaved) {
  if (slots == nullptr || interleaved == nullptr || deviceChannels <= 0 || frames <= 0) {
    return;
  }
  for (int frame = 0; frame < frames; ++frame) {
    float* output = interleaved + static_cast<std::size_t>(frame) * deviceChannels;
    for (int channel = 0; channel < deviceChannels; ++channel) {
      const float* bus = nullptr;
      switch (slots[channel]) {
        case DeviceOutSlot::MainL:
          bus = mainL;
          break;
        case DeviceOutSlot::MainR:
          bus = mainR;
          break;
        case DeviceOutSlot::Out34L:
          bus = out34L;
          break;
        case DeviceOutSlot::Out34R:
          bus = out34R;
          break;
        case DeviceOutSlot::PhonesL:
          bus = phonesL;
          break;
        case DeviceOutSlot::PhonesR:
          bus = phonesR;
          break;
        case DeviceOutSlot::None:
          break;
      }
      output[channel] = bus != nullptr ? bus[frame] : 0.0f;
    }
  }
}

void DevicePump::reset() {
  writeFrame_.store(0, std::memory_order_relaxed);
  readFrame_ = 0;
  captureFrames_.store(0, std::memory_order_relaxed);
  captureBlocks_.store(0, std::memory_order_relaxed);
  captureDropped_.store(0, std::memory_order_relaxed);
}

void DevicePump::pushCapture(const float* interleaved, int frames, int channels) {
  if (interleaved == nullptr || frames <= 0 || channels <= 0) return;
  const std::uint64_t start = writeFrame_.load(std::memory_order_relaxed);
  const int width = std::min(channels, kMaxInputs);
  for (int frame = 0; frame < frames; ++frame) {
    const std::uint64_t index =
        (start + static_cast<std::uint64_t>(frame)) % static_cast<std::uint64_t>(kRingFrames);
    const float* input = interleaved + static_cast<std::size_t>(frame) * channels;
    for (int channel = 0; channel < width; ++channel) {
      ring_[channel][index] = input[channel];
    }
  }
  writeFrame_.store(start + static_cast<std::uint64_t>(frames), std::memory_order_relaxed);
  captureFrames_.fetch_add(static_cast<std::uint64_t>(frames), std::memory_order_relaxed);
  captureBlocks_.fetch_add(1, std::memory_order_relaxed);
}

int DevicePump::readCapture(float* const* planes, int frames) {
  if (planes == nullptr || frames <= 0) return 0;
  const std::uint64_t write = writeFrame_.load(std::memory_order_relaxed);
  std::uint64_t available = write - readFrame_;
  if (available > static_cast<std::uint64_t>(kRingFrames / 2)) {
    // The capture side ran ahead of the graph (device clock difference or a
    // stalled render): keep the newest block instead of growing latency.
    const std::uint64_t dropped = available - static_cast<std::uint64_t>(frames);
    captureDropped_.fetch_add(dropped, std::memory_order_relaxed);
    readFrame_ = write - static_cast<std::uint64_t>(frames);
    available = static_cast<std::uint64_t>(frames);
  }
  const int have = static_cast<int>(
      std::min<std::uint64_t>(available, static_cast<std::uint64_t>(frames)));
  const int missing = frames - have;
  for (int channel = 0; channel < kMaxInputs; ++channel) {
    float* plane = planes[channel];
    if (plane == nullptr) continue;
    std::fill(plane, plane + missing, 0.0f);
  }
  for (int frame = 0; frame < have; ++frame) {
    const std::uint64_t index =
        (readFrame_ + static_cast<std::uint64_t>(frame)) %
        static_cast<std::uint64_t>(kRingFrames);
    for (int channel = 0; channel < kMaxInputs; ++channel) {
      if (planes[channel] != nullptr) planes[channel][missing + frame] = ring_[channel][index];
    }
  }
  readFrame_ += static_cast<std::uint64_t>(have);
  return have;
}

}  // namespace mnky::audio

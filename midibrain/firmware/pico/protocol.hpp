#pragma once
// Host-testable demonstration codec. Not yet a Pico SPI driver or a complete
// transport protocol; do not use it for scheduled MIDI or production hardware.
#include <array>
#include <cstddef>
#include <cstdint>

namespace mb {
constexpr std::size_t frame_size = 32, payload_size = 18;
using Bytes = std::array<std::uint8_t, frame_size>;
enum class Type : std::uint8_t { hello = 1, ack = 2, control_event = 3 };
struct Frame {
  Type type{};
  std::uint16_t sequence{};
  std::uint32_t session{};
  std::uint8_t length{};
  std::array<std::uint8_t, payload_size> payload{};
};
inline std::uint16_t crc16(const std::uint8_t* data, std::size_t n) {
  std::uint16_t crc = 0xffff;
  for (std::size_t i = 0; i < n; ++i) {
    crc ^= static_cast<std::uint16_t>(data[i]) << 8;
    for (int bit = 0; bit < 8; ++bit)
      crc = static_cast<std::uint16_t>((crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1);
  }
  return crc;
}
inline bool encode(const Frame& f, Bytes& out) {
  const auto type = static_cast<std::uint8_t>(f.type);
  if (f.length > payload_size || !f.session || type < 1 || type > 3) return false;
  out.fill(0);
  out[0] = 'M'; out[1] = 'B'; out[2] = 1; out[3] = static_cast<std::uint8_t>(f.type);
  out[4] = static_cast<std::uint8_t>(f.sequence); out[5] = static_cast<std::uint8_t>(f.sequence >> 8);
  out[6] = f.length; // byte 7 reserved, must be zero
  for (int i = 0; i < 4; ++i) out[8 + i] = static_cast<std::uint8_t>(f.session >> (8 * i));
  for (std::size_t i = 0; i < f.length; ++i) out[12 + i] = f.payload[i];
  const auto crc = crc16(out.data(), 30);
  out[30] = static_cast<std::uint8_t>(crc); out[31] = static_cast<std::uint8_t>(crc >> 8);
  return true;
}
inline bool decode(const Bytes& in, Frame& f) {
  if (in[0] != 'M' || in[1] != 'B' || in[2] != 1 || in[7] != 0 || in[6] > payload_size) return false;
  const auto crc = crc16(in.data(), 30);
  if (in[30] != (crc & 0xff) || in[31] != (crc >> 8)) return false;
  for (std::size_t i = 12 + in[6]; i < 30; ++i) if (in[i] != 0) return false;
  if (in[3] < 1 || in[3] > 3) return false;
  Frame result{};
  result.type = static_cast<Type>(in[3]);
  result.sequence = static_cast<std::uint16_t>(in[4] | (in[5] << 8));
  result.length = in[6];
  for (int i = 0; i < 4; ++i) result.session |= static_cast<std::uint32_t>(in[8 + i]) << (8 * i);
  if (!result.session) return false;
  for (std::size_t i = 0; i < result.length; ++i) result.payload[i] = in[12 + i];
  f = result;
  return true;
}

// Switch must remain unchanged for threshold milliseconds. Caller supplies
// monotonic time; unsigned subtraction also handles 32-bit timer wraparound.
class Debouncer {
 public:
  explicit Debouncer(std::uint32_t threshold_ms = 10) : threshold_(threshold_ms) {}
  bool update(bool raw, std::uint32_t now) {
    if (raw != candidate_) { candidate_ = raw; changed_at_ = now; }
    if (candidate_ != stable_ && static_cast<std::uint32_t>(now - changed_at_) >= threshold_) {
      stable_ = candidate_;
      return true;
    }
    return false;
  }
  bool state() const { return stable_; }
 private:
  std::uint32_t threshold_, changed_at_ = 0;
  bool candidate_ = false, stable_ = false;
};
} // namespace mb

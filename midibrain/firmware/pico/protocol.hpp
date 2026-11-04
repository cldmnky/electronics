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

// ---------------------------------------------------------------------------
// MIDI FX route pipes and per-output clock extras (Pi->Pico). These are the
// wire contract for the session MidiPipeType/ClockRoute fields; the Pico
// evaluates a chain in route order where the routed bytes already flow. Only
// physical TRS ports exist here (src/dst 0 = TRS IN1/OUT1, 1 = TRS IN2/OUT2);
// routes touching USB or the merged Pi-side ports stay on the Pi and are not
// represented by these frames.
// ---------------------------------------------------------------------------
constexpr std::uint8_t EV_MIDI_ROUTE = 0x18;       // begin/clear one route chain
constexpr std::uint8_t EV_MIDI_ROUTE_PIPE = 0x19;  // set one pipe (chunked params)
constexpr std::uint8_t EV_MIDI_ROUTE_SEND = 0x1A;  // routed MIDI through the chain
constexpr std::uint8_t EV_CLOCK_ROUTE_EXT = 0x1B;  // tempoDiv + syncDelayMs

// EV_MIDI_ROUTE payload[3] flag bits; reserved bits must be zero.
constexpr std::uint8_t ROUTE_FLAG_ON = 0x01;
constexpr std::uint8_t ROUTE_FLAG_MASK = ROUTE_FLAG_ON;

// Pipe chain limits, shared by the wire codec and midi_pipe.hpp. A param is a
// signed 16-bit positional integer; the largest Wave-1 pipe (Filter) has 8,
// and the frame carries at most 5 per chunk (offset selects the next chunk).
constexpr std::size_t route_pipe_count_max = 8;
constexpr std::size_t route_pipe_param_max = 10;
constexpr std::size_t route_pipe_param_frame_max = 5;

// EV_MIDI_ROUTE layout: [1]=src, [2]=dst, [3]=flags. Length 4. Starts a fresh
// chain and clears its runtime state (dispatcher rotation, sustain latch and
// pending note lengths); flags bit 0 enables the route.
inline bool pack_midi_route(Frame& f, std::uint16_t seq, std::uint32_t session,
                            std::uint8_t src, std::uint8_t dst, std::uint8_t flags) {
  if (src > 1 || dst > 1 || (flags & ~ROUTE_FLAG_MASK) != 0) return false;
  f = Frame{};
  f.type = Type::control_event;
  f.sequence = seq;
  f.session = session;
  f.length = 4;
  f.payload[0] = EV_MIDI_ROUTE;
  f.payload[1] = src;
  f.payload[2] = dst;
  f.payload[3] = flags;
  return true;
}
inline bool unpack_midi_route(const Frame& f, std::uint8_t& src, std::uint8_t& dst,
                              std::uint8_t& flags) {
  if (f.type != Type::control_event || f.length != 4 || f.payload[0] != EV_MIDI_ROUTE ||
      f.payload[1] > 1 || f.payload[2] > 1 || (f.payload[3] & ~ROUTE_FLAG_MASK) != 0)
    return false;
  src = f.payload[1];
  dst = f.payload[2];
  flags = f.payload[3];
  return true;
}

// EV_MIDI_ROUTE_PIPE layout: [1]=src, [2]=dst, [3]=pipe index, [4]=type,
// [5]=bypass, [6]=param offset, [7]=param count, then count signed int16 LE
// params. Length 8 + 2*count. type 1..16 is accepted (Wave-2 ids are stored
// but pass through on this firmware); param offset+count <= 10 and count <= 5.
inline bool pack_midi_route_pipe(Frame& f, std::uint16_t seq, std::uint32_t session,
                                 std::uint8_t src, std::uint8_t dst,
                                 std::uint8_t index, std::uint8_t type, bool bypass,
                                 std::uint8_t param_offset, const std::int16_t* params,
                                 std::size_t count) {
  if (src > 1 || dst > 1 || index >= route_pipe_count_max || type < 1 || type > 16 ||
      count > route_pipe_param_frame_max ||
      param_offset + count > route_pipe_param_max || (count != 0 && params == nullptr))
    return false;
  f = Frame{};
  f.type = Type::control_event;
  f.sequence = seq;
  f.session = session;
  f.length = static_cast<std::uint8_t>(8 + 2 * count);
  f.payload[0] = EV_MIDI_ROUTE_PIPE;
  f.payload[1] = src;
  f.payload[2] = dst;
  f.payload[3] = index;
  f.payload[4] = type;
  f.payload[5] = bypass ? 1 : 0;
  f.payload[6] = param_offset;
  f.payload[7] = static_cast<std::uint8_t>(count);
  for (std::size_t i = 0; i < count; ++i) {
    const auto bits = static_cast<std::uint16_t>(params[i]);
    f.payload[8 + 2 * i] = static_cast<std::uint8_t>(bits);
    f.payload[9 + 2 * i] = static_cast<std::uint8_t>(bits >> 8);
  }
  return true;
}
inline bool unpack_midi_route_pipe(const Frame& f, std::uint8_t& src, std::uint8_t& dst,
                                   std::uint8_t& index, std::uint8_t& type, bool& bypass,
                                   std::uint8_t& param_offset, std::int16_t* params,
                                   std::size_t capacity, std::size_t& count) {
  if (f.type != Type::control_event || f.length < 8 ||
      f.payload[0] != EV_MIDI_ROUTE_PIPE || f.payload[1] > 1 || f.payload[2] > 1 ||
      f.payload[3] >= route_pipe_count_max || f.payload[4] < 1 || f.payload[4] > 16 ||
      f.payload[5] > 1 || f.payload[7] > route_pipe_param_frame_max ||
      f.payload[6] + f.payload[7] > route_pipe_param_max)
    return false;
  count = f.payload[7];
  if (f.length != 8 + 2 * count || capacity < count || (count != 0 && params == nullptr))
    return false;
  src = f.payload[1];
  dst = f.payload[2];
  index = f.payload[3];
  type = f.payload[4];
  bypass = f.payload[5] != 0;
  param_offset = f.payload[6];
  for (std::size_t i = 0; i < count; ++i) {
    const auto bits = static_cast<std::uint16_t>(f.payload[8 + 2 * i] |
                                                 (f.payload[9 + 2 * i] << 8));
    params[i] = static_cast<std::int16_t>(bits);
  }
  return true;
}

// EV_MIDI_ROUTE_SEND layout: [1]=src, [2]=dst, [3]=size, [4..]=bytes. Length
// 4 + size (size 1..3). The Pico runs the configured chain for that route and
// emits the result on dst; a route that is not enabled drops the message.
inline bool pack_midi_route_send(Frame& f, std::uint16_t seq, std::uint32_t session,
                                 std::uint8_t src, std::uint8_t dst,
                                 const std::uint8_t* data, std::uint8_t size) {
  if (src > 1 || dst > 1 || size < 1 || size > 3 || data == nullptr) return false;
  f = Frame{};
  f.type = Type::control_event;
  f.sequence = seq;
  f.session = session;
  f.length = static_cast<std::uint8_t>(4 + size);
  f.payload[0] = EV_MIDI_ROUTE_SEND;
  f.payload[1] = src;
  f.payload[2] = dst;
  f.payload[3] = size;
  for (std::uint8_t i = 0; i < size; ++i) f.payload[4 + i] = data[i];
  return true;
}
inline bool unpack_midi_route_send(const Frame& f, std::uint8_t& src, std::uint8_t& dst,
                                   std::uint8_t* data, std::uint8_t& size) {
  if (f.type != Type::control_event || f.length < 5 ||
      f.payload[0] != EV_MIDI_ROUTE_SEND || f.payload[1] > 1 || f.payload[2] > 1 ||
      f.payload[3] < 1 || f.payload[3] > 3 || f.length != 4 + f.payload[3] || data == nullptr)
    return false;
  src = f.payload[1];
  dst = f.payload[2];
  size = f.payload[3];
  for (std::uint8_t i = 0; i < size; ++i) data[i] = f.payload[4 + i];
  return true;
}

// EV_CLOCK_ROUTE_EXT layout: [1]=port (0 = OUT1/uart0, 1 = OUT2/uart1),
// [2]=tempoDiv (1, 2 or 4), [3..4]=syncDelayMs LE16. Length 5. The firmware
// clamps an invalid divider to 1 and the delay to 0..500 ms (the session reader
// applies the same clamp), so a stale/corrupt value cannot silence an output.
inline bool pack_clock_route_ext(Frame& f, std::uint16_t seq, std::uint32_t session,
                                 std::uint8_t port, std::uint8_t tempo_div,
                                 std::uint16_t sync_delay_ms) {
  if (port > 1 || (tempo_div != 1 && tempo_div != 2 && tempo_div != 4)) return false;
  f = Frame{};
  f.type = Type::control_event;
  f.sequence = seq;
  f.session = session;
  f.length = 5;
  f.payload[0] = EV_CLOCK_ROUTE_EXT;
  f.payload[1] = port;
  f.payload[2] = tempo_div;
  f.payload[3] = static_cast<std::uint8_t>(sync_delay_ms);
  f.payload[4] = static_cast<std::uint8_t>(sync_delay_ms >> 8);
  return true;
}
inline bool unpack_clock_route_ext(const Frame& f, std::uint8_t& port,
                                   std::uint8_t& tempo_div, std::uint16_t& sync_delay_ms) {
  if (f.type != Type::control_event || f.length != 5 ||
      f.payload[0] != EV_CLOCK_ROUTE_EXT || f.payload[1] > 1)
    return false;
  port = f.payload[1];
  tempo_div = f.payload[2];
  sync_delay_ms = static_cast<std::uint16_t>(f.payload[3] | (f.payload[4] << 8));
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

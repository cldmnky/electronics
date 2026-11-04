#pragma once
// MIDI byte-stream parser and mnkybox CONTROL_EVENT payload codec. No Pico
// SDK dependency: unit-tested on the host in protocol_test.cpp. Times are
// caller-supplied monotonic microseconds; the protocol core lives in
// protocol.hpp (32-byte frames, CRC-16/CCITT-FALSE).
#include <cstdint>

#include "protocol.hpp"

namespace mb {
// ---------------------------------------------------------------------------
// Parsed MIDI messages
// ---------------------------------------------------------------------------
struct MidiMessage {
  std::uint8_t port{};      // 0 = MIDI IN 1 (UART0), 1 = MIDI IN 2 (UART1)
  std::uint8_t size{};      // 1..3 data bytes
  std::uint8_t data[3]{};   //
  std::uint32_t stamp_us{};  // capture time of the final byte
};

// Expected data bytes (after the status) for 0x80..0xEF; -1 selects SysEx.
inline int midi_expected(std::uint8_t status) {
  if (status < 0x80) return 0;
  if (status >= 0xF0) {
    switch (status) {
      case 0xF1: case 0xF3: return 1;
      case 0xF2: return 2;
      default: return 0;  // 0xF6..0xFF single byte; 0xF0/0xF4/0xF5/0xFD handled below
    }
  }
  switch (status & 0xF0) {
    case 0xC0: case 0xD0: return 1;
    default: return 2;
  }
}

// Byte-stream parser, exact per MIDI 1.0: running status for channel voice
// (fixed lengths, self-realigning after any interruption); realtime
// 0xF8/FA/FB/FC/FE/FF interleaved anywhere, never disturbing a partial
// message or running status; system common F1/F2/F3/F6 cancels running
// status; a status byte always abandons a partial message (interrupted bytes
// are unrecoverable); undefined 0xF4/0xF5/0xF9/0xFD ignored without touching
// any state; EOX ends the SysEx skip and preserves running status so the
// stream resumes cleanly; velocity-0 note-on passes through opaquely
// (note-off semantics belong upstream); SysEx content skipped (v1 limitation:
// long SysEx is dropped, documented); MIDI 2.0/UMP out of scope (the
// 31250-baud UARTs are MIDI 1.0).
class MidiParser {
 public:
  explicit MidiParser(std::uint8_t port = 0) : port_(port) {}
  // Returns true once per complete message; inspect last().
  bool feed(std::uint8_t byte, std::uint32_t now_us) {
    if (byte >= 0xF8) {  // realtime: always a complete one-byte message.
      // 0xF9/0xFD are undefined: ignored without touching any state.
      if (byte == 0xF9 || byte == 0xFD) return false;
      msg_ = MidiMessage{};
      msg_.port = port_;
      msg_.size = 1;
      msg_.data[0] = byte;
      msg_.stamp_us = now_us;
      return true;
    }
    if (byte == 0xF7) {  // SysEx end (or lone EOX): drop, keep running status
      sysex_ = false;
      count_ = 0;
      return false;
    }
    if (byte == 0xF0 || sysex_) {  // SysEx start/body: skip until 0xF7
      sysex_ = (byte != 0xF7);
      count_ = 0;
      return false;
    }
    if (byte >= 0x80) {
      if (byte == 0xF4 || byte == 0xF5 || byte == 0xF9 || byte == 0xFD) return false;
      status_ = byte;
      have_status_ = true;
      // Channel voice sustains running status; system common cancels it.
      running_ = (byte < 0xF0) ? byte : 0;
      count_ = 0;
      expected_ = midi_expected(byte);
      if (expected_ == 0) {  // single-byte system common (0xF6)
        msg_ = MidiMessage{};
        msg_.port = port_;
        msg_.size = 1;
        msg_.data[0] = byte;
        msg_.stamp_us = now_us;
        have_status_ = false;  // no live message after a 1-byte status
        return true;
      }
      return false;
    }
    // Data byte: needs a live status (running or partial message). A stale
    // data byte with no status is dropped; completion resets the count while
    // running status persists for the next message.
    if (!have_status_ || count_ >= 2) return false;
    data_[count_++] = byte;
    if (count_ < expected_) return false;
    if (count_ > expected_) {  // unreachable safety: lengths are fixed
      count_ = 0;
      return false;
    }
    msg_ = MidiMessage{};
    msg_.port = port_;
    msg_.size = static_cast<std::uint8_t>(1 + expected_);
    msg_.data[0] = status_;
    msg_.data[1] = data_[0];
    msg_.data[2] = (expected_ > 1) ? data_[1] : 0;
    msg_.stamp_us = now_us;
    count_ = 0;
    // Channel voice keeps running status for the next message; a completed
    // system-common message leaves no live status behind.
    if (running_ == 0) have_status_ = false;
    return true;
  }
  const MidiMessage& last() const { return msg_; }

 private:
  std::uint8_t port_, running_ = 0, status_ = 0, count_ = 0;
  int expected_ = 0;
  bool sysex_ = false, have_status_ = false;
  std::uint8_t data_[2]{};
  MidiMessage msg_{};
};

// ---------------------------------------------------------------------------
// CONTROL_EVENT payload registry (payload[0] = kind)
// ---------------------------------------------------------------------------
constexpr std::uint8_t EV_MIDI_MSG = 0x01;     // Pico->Pi timestamped MIDI
constexpr std::uint8_t EV_CONTROL = 0x02;      // Pico->Pi button/encoder input
constexpr std::uint8_t EV_TEMPO = 0x04;        // Pico->Pi local tempo (bpm*100 LE16)
constexpr std::uint8_t EV_CLOCK_TICK = 0x05;   // Pico->Pi clock tick + stamp (USB mirror)
constexpr std::uint8_t EV_SET_TEMPO = 0x10;    // Pi->Pico bpm*100 LE16
constexpr std::uint8_t EV_TRANSPORT = 0x11;    // Pi->Pico 0 stop, 1 start, 2 continue
constexpr std::uint8_t EV_CLOCK_ENABLE = 0x12;// Pi->Pico, accepted but ignored: clock is always on
constexpr std::uint8_t EV_TRANSPORT_STATE = 0x06;  // Pico->Pi rolling flag + tick (USB mirror)
constexpr std::uint8_t EV_LED_SET = 0x13;      // Pi->Pico mask, states
constexpr std::uint8_t EV_MIDI_SEND = 0x15;    // Pi->Pico port, size, bytes
constexpr std::uint8_t EV_TRANSPORT_MODE = 0x16;  // Pi->Pico 0 local, 1 manual

// Control ids for EV_CONTROL payload[1]; payload[2] is state (1/0), except
// ENCODER_TURN which carries a signed int8 delta.
constexpr std::uint8_t CTL_NAV_UP = 1, CTL_NAV_DOWN = 2, CTL_NAV_LEFT = 3;
constexpr std::uint8_t CTL_NAV_RIGHT = 4, CTL_NAV_PRESS = 5, CTL_ENCODER_SW = 6;
constexpr std::uint8_t CTL_REC = 10, CTL_OVER_DUB = 11, CTL_STOP = 12;
constexpr std::uint8_t CTL_PLAY = 13, CTL_PLAY_START = 14, CTL_SHIFT_EXIT = 15;
constexpr std::uint8_t CTL_ENCODER_TURN = 16;

// LED mask bits for EV_LED_SET (payload[1] mask, payload[2] states).
constexpr std::uint8_t LED_REC = 0x01, LED_OVERDUB = 0x02, LED_PLAY = 0x04;

// Panel buttons that drive local transport on press, mirroring EV_TRANSPORT
// commands (1 start, 0 stop; there is no continue button). PLAY starts only
// when stopped (idempotent press); PLAY_START always restarts from tick 0;
// STOP always stops. REC/OVER_DUB/SHIFT and the rest are Pi policy (events
// only) so recording behaviour stays in one place. Returns -1 for none.
inline int transport_for_press(std::uint8_t id, bool rolling) {
  if (id == CTL_PLAY_START) return 1;
  if (id == CTL_PLAY && !rolling) return 1;
  if (id == CTL_STOP) return 0;
  return -1;
}

// EV_MIDI_MSG layout: [1]=port, [2]=size, [3..5]=bytes, [6..9]=stamp_us LE.
inline bool pack_midi(Frame& f, std::uint16_t seq, std::uint32_t session, const MidiMessage& m) {
  if (m.size < 1 || m.size > 3) return false;
  f = Frame{};
  f.type = Type::control_event;
  f.sequence = seq;
  f.session = session;
  f.length = 10;
  f.payload[0] = EV_MIDI_MSG;
  f.payload[1] = m.port;
  f.payload[2] = m.size;
  f.payload[3] = m.data[0];
  f.payload[4] = m.data[1];
  f.payload[5] = m.data[2];
  for (int i = 0; i < 4; ++i) f.payload[6 + i] = static_cast<std::uint8_t>(m.stamp_us >> (8 * i));
  return true;
}
inline bool unpack_midi(const Frame& f, MidiMessage& m) {
  if (f.type != Type::control_event || f.length != 10 || f.payload[0] != EV_MIDI_MSG) return false;
  if (f.payload[2] < 1 || f.payload[2] > 3) return false;
  m = MidiMessage{};
  m.port = f.payload[1];
  m.size = f.payload[2];
  m.data[0] = f.payload[3];
  m.data[1] = f.payload[4];
  m.data[2] = f.payload[5];
  for (int i = 0; i < 4; ++i) m.stamp_us |= static_cast<std::uint32_t>(f.payload[6 + i]) << (8 * i);
  return true;
}

// EV_CONTROL layout: [1]=control id, [2]=state or signed delta. Length 3.
inline bool pack_control(Frame& f, std::uint16_t seq, std::uint32_t session,
                         std::uint8_t id, std::uint8_t value) {
  f = Frame{};
  f.type = Type::control_event;
  f.sequence = seq;
  f.session = session;
  f.length = 3;
  f.payload[0] = EV_CONTROL;
  f.payload[1] = id;
  f.payload[2] = value;
  return true;
}
// EV_TEMPO layout: [1..2]=bpm*100 LE16. Length 3.
inline bool pack_tempo(Frame& f, std::uint16_t seq, std::uint32_t session, std::uint16_t bpm_x100) {
  f = Frame{};
  f.type = Type::control_event;
  f.sequence = seq;
  f.session = session;
  f.length = 3;
  f.payload[0] = EV_TEMPO;
  f.payload[1] = static_cast<std::uint8_t>(bpm_x100);
  f.payload[2] = static_cast<std::uint8_t>(bpm_x100 >> 8);
  return true;
}
// EV_CLOCK_TICK layout: [1..4]=tick count LE32, [5..8]=Pico stamp_us LE32.
// Length 9. Sent every tick while rolling so the Pi can discipline its USB
// clock and map Pico timestamps into its own timeline (see docs/protocol.md).
// The stamp is captured at tick time; delivery delay must NOT be mistaken
// for tick time (the Pi estimates bus latency separately).
inline bool pack_tick(Frame& f, std::uint16_t seq, std::uint32_t session,
                       std::uint32_t tick, std::uint32_t stamp_us) {
  f = Frame{};
  f.type = Type::control_event;
  f.sequence = seq;
  f.session = session;
  f.length = 9;
  f.payload[0] = EV_CLOCK_TICK;
  for (int i = 0; i < 4; ++i) f.payload[1 + i] = static_cast<std::uint8_t>(tick >> (8 * i));
  for (int i = 0; i < 4; ++i) f.payload[5 + i] = static_cast<std::uint8_t>(stamp_us >> (8 * i));
  return true;
}
// EV_TRANSPORT_STATE layout: [1]=rolling (0 stopped, 1 rolling),
// [2..5]=tick count LE32 at the transition. Length 6. Sent on every
// transport transition, at boot, and when the Pi (re)connects, so the USB
// mirror always knows whether ticks mean "roll" or "idle".
inline bool pack_transport_state(Frame& f, std::uint16_t seq, std::uint32_t session,
                                 bool rolling, std::uint32_t tick) {
  f = Frame{};
  f.type = Type::control_event;
  f.sequence = seq;
  f.session = session;
  f.length = 6;
  f.payload[0] = EV_TRANSPORT_STATE;
  f.payload[1] = rolling ? 1 : 0;
  for (int i = 0; i < 4; ++i) f.payload[2 + i] = static_cast<std::uint8_t>(tick >> (8 * i));
  return true;
}
}  // namespace mb

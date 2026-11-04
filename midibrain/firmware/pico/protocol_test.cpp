#include "protocol.hpp"
#include "midi.hpp"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>

int main() {
  using namespace mb;
  const auto* sample = reinterpret_cast<const std::uint8_t*>("123456789");
  assert(crc16(sample, 9) == 0x29b1);
  Frame hello{Type::hello, 123, 0x78563412, 0, {}};
  Bytes wire{};
  assert(encode(hello, wire));
  assert(wire[0] == 'M' && wire[1] == 'B' && wire[2] == 1);
  assert(wire[8] == 0x12 && wire[9] == 0x34 && wire[10] == 0x56 && wire[11] == 0x78);
  Frame decoded{};
  assert(decode(wire, decoded));
  assert(decoded.type == Type::hello && decoded.sequence == 123 && decoded.session == 0x78563412);
  auto bad = wire; bad[5] ^= 1; assert(!decode(bad, decoded));
  bad = wire; bad[2] = 2; assert(!decode(bad, decoded));
  bad = wire; bad[6] = 19; assert(!decode(bad, decoded));
  bad = wire; bad[7] = 1; assert(!decode(bad, decoded));
  bad = wire; bad[29] = 1;
  const auto crc = crc16(bad.data(), 30); bad[30] = crc & 0xff; bad[31] = crc >> 8;
  assert(!decode(bad, decoded)); // valid checksum but nonzero padding
  Frame too_big{Type::ack, 0, 1, 19, {}}; assert(!encode(too_big, wire));
  Frame no_session{Type::ack, 0, 0, 0, {}}; assert(!encode(no_session, wire));
  Frame invalid{static_cast<Type>(255), 0, 1, 0, {}}; assert(!encode(invalid, wire));
  Frame full{Type::ack, 0xffff, 1, 18, {}}; full.payload.fill(0xa5);
  assert(encode(full, wire) && decode(wire, decoded) && decoded.payload == full.payload);

  // Simulated bouncy nav press and release. No physical GPIO is involved.
  Debouncer nav(10);
  assert(!nav.update(true, 100)); assert(!nav.update(false, 103));
  assert(!nav.update(true, 105)); assert(!nav.update(true, 114));
  assert(nav.update(true, 115) && nav.state());
  assert(!nav.update(false, 120)); assert(nav.update(false, 130) && !nav.state());
  Debouncer wrap(10); assert(!wrap.update(true, 0xfffffffau)); assert(wrap.update(true, 4));
  Frame event{Type::control_event, 124, hello.session, 2, {}};
  event.payload[0] = 1; event.payload[1] = 1; // NAV_UP pressed, simulated
  assert(encode(event, wire) && decode(wire, decoded));
  assert(decoded.session == hello.session && decoded.payload[0] == 1 && decoded.payload[1] == 1);
  // MIDI parser: note-on with running status, realtime interleaved mid-message.
  MidiParser p0(0);
  assert(!p0.feed(0x90, 1000)); assert(!p0.feed(0x3C, 1001));
  assert(p0.feed(0xF8, 1002));  // clock slips between data bytes
  assert(p0.last().size == 1 && p0.last().data[0] == 0xF8);
  assert(p0.feed(0x7F, 1003));
  assert(p0.last().size == 3 && p0.last().data[0] == 0x90);
  assert(p0.last().data[1] == 0x3C && p0.last().data[2] == 0x7F);
  assert(!p0.feed(0x40, 1004)); assert(p0.feed(0x50, 1005));  // running status
  assert(p0.last().data[0] == 0x90 && p0.last().data[1] == 0x40);
  // Program change (2 bytes), song position (3 bytes), SysEx skipped.
  MidiParser p1(1);
  assert(!p1.feed(0xC0, 2000)); assert(p1.feed(0x05, 2001));
  assert(p1.last().size == 2 && p1.last().port == 1);
  assert(!p1.feed(0xF2, 2002)); assert(!p1.feed(0x00, 2003));
  assert(p1.feed(0x20, 2004) && p1.last().size == 3);
  assert(!p1.feed(0xF0, 2005)); assert(!p1.feed(0x41, 2006));
  assert(!p1.feed(0xF7, 2007));  // SysEx dropped, parser recovers
  assert(!p1.feed(0xF4, 2008) && !p1.feed(0x40, 2009));  // stale data dropped
  // Event round-trips through real frames.
  Frame ef{};
  MidiMessage m{1, 3, {0x99, 0x00, 0x7F}, 0x00ABCDEF};
  assert(pack_midi(ef, 7, 0x12345678, m));
  assert(encode(ef, wire) && decode(wire, decoded) && unpack_midi(decoded, m));
  assert(m.port == 1 && m.size == 3 && m.data[0] == 0x99 && m.stamp_us == 0x00ABCDEF);
  MidiMessage badm{0, 4, {}, 0}; assert(!pack_midi(ef, 0, 1, badm));
  Frame cf{};
  assert(pack_control(cf, 8, 0x12345678, CTL_ENCODER_TURN, 0xFF));
  assert(encode(cf, wire) && decode(wire, decoded));
  assert(decoded.payload[0] == EV_CONTROL && decoded.payload[1] == CTL_ENCODER_TURN);
  assert(static_cast<std::int8_t>(decoded.payload[2]) == -1);
  Frame tf{};
  assert(pack_tempo(tf, 9, 0x12345678, 12000));
  assert(encode(tf, wire) && decode(wire, decoded));
  assert(decoded.payload[0] == EV_TEMPO && (decoded.payload[1] | (decoded.payload[2] << 8)) == 12000);
  Frame kf{};
  assert(pack_tick(kf, 10, 0x12345678, 0x00003039, 0x00ABCDEF));
  assert(encode(kf, wire) && decode(wire, decoded));
  assert(decoded.payload[0] == EV_CLOCK_TICK && decoded.length == 9);
  assert((decoded.payload[1] | (decoded.payload[2] << 8) | (decoded.payload[3] << 16) | (decoded.payload[4] << 24)) == 0x3039);
  assert((decoded.payload[5] | (decoded.payload[6] << 8) | (decoded.payload[7] << 16) | (decoded.payload[8] << 24)) == 0x00ABCDEF);
  Frame sf{};
  assert(pack_transport_state(sf, 11, 0x12345678, true, 0x00000048));
  assert(encode(sf, wire) && decode(wire, decoded));
  assert(decoded.payload[0] == EV_TRANSPORT_STATE && decoded.length == 6);
  assert(decoded.payload[1] == 1);
  assert((decoded.payload[2] | (decoded.payload[3] << 8) | (decoded.payload[4] << 16) | (decoded.payload[5] << 24)) == 0x48);
  // Message-length table, exact for every channel status.
  for (int st = 0x80; st <= 0xEF; ++st) {
    const int hi = st & 0xF0;
    assert(midi_expected(static_cast<std::uint8_t>(st)) == ((hi == 0xC0 || hi == 0xD0) ? 1 : 2));
  }
  assert(midi_expected(0xF1) == 1 && midi_expected(0xF3) == 1);
  assert(midi_expected(0xF2) == 2 && midi_expected(0xF6) == 0);
  // Undefined bytes ignored without touching live state (mid-message too).
  MidiParser u(0);
  assert(!u.feed(0xF9, 3000) && !u.feed(0xFD, 3001) && !u.feed(0x40, 3002));
  assert(!u.feed(0x90, 3003)); assert(!u.feed(0x3C, 3004));
  assert(!u.feed(0xF9, 3005) && !u.feed(0xFD, 3006));
  assert(u.feed(0x7F, 3007) && u.last().data[1] == 0x3C && u.last().data[2] == 0x7F);
  // System common cancels running status; EOX preserves it.
  MidiParser v(0);
  assert(!v.feed(0x90, 4000)); assert(!v.feed(0x01, 4001)); assert(v.feed(0x02, 4002));
  assert(!v.feed(0xF1, 4003)); assert(v.feed(0x00, 4004) && v.last().data[0] == 0xF1);
  assert(!v.feed(0x40, 4005));  // running status is gone
  assert(!v.feed(0x90, 4006)); assert(!v.feed(0x3C, 4007));
  assert(!v.feed(0xF0, 4008)); assert(!v.feed(0x41, 4009));  // SysEx starts mid-message
  assert(v.feed(0xF8, 4010) && v.last().data[0] == 0xF8);  // realtime inside SysEx
  assert(!v.feed(0xF7, 4011));
  assert(!v.feed(0x40, 4012)); assert(v.feed(0x50, 4013));  // running resumes
  assert(v.last().data[0] == 0x90 && v.last().data[1] == 0x40 && v.last().data[2] == 0x50);
  // New status abandons a partial message; tune request leaves no status.
  MidiParser w(0);
  assert(!w.feed(0x90, 5000)); assert(!w.feed(0x3C, 5001));
  assert(!w.feed(0x80, 5002)); assert(!w.feed(0x40, 5003));
  assert(w.feed(0x50, 5004) && w.last().data[0] == 0x80);
  assert(w.feed(0xF6, 5005) && w.last().size == 1);
  assert(!w.feed(0x40, 5006));  // nothing live after F6
  // Velocity-zero passes through opaquely; long running-status runs align.
  MidiParser x(0);
  assert(!x.feed(0x92, 6000)); assert(!x.feed(0x40, 6001));
  assert(x.feed(0x00, 6002) && x.last().data[2] == 0x00);
  for (int i = 0; i < 8; ++i) {
    assert(!x.feed(static_cast<std::uint8_t>(0x30 + i), 6010 + i));
    assert(x.feed(static_cast<std::uint8_t>(0x40 + i), 6020 + i));
    assert(x.last().data[0] == 0x92 && x.last().data[1] == 0x30 + i);
  }
  // Panel transport mapping: PLAY idempotent, PLAY_START restarts, STOP stops.
  assert(transport_for_press(CTL_PLAY_START, true) == 1);
  assert(transport_for_press(CTL_PLAY_START, false) == 1);
  assert(transport_for_press(CTL_PLAY, false) == 1);
  assert(transport_for_press(CTL_PLAY, true) == -1);
  assert(transport_for_press(CTL_STOP, true) == 0);
  assert(transport_for_press(CTL_STOP, false) == 0);
  assert(transport_for_press(CTL_REC, false) == -1);
  assert(transport_for_press(CTL_OVER_DUB, true) == -1);
  assert(transport_for_press(CTL_SHIFT_EXIT, false) == -1);
  assert(transport_for_press(CTL_NAV_PRESS, false) == -1);
  std::cout << "PASS: framed loopback + simulated debounced control event (no SPI hardware)\n";
  std::cout << "PASS: MIDI parser (running status, realtime, SysEx skip) + event codecs\n";
}

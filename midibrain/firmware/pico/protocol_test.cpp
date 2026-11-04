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
  std::cout << "PASS: framed loopback + simulated debounced control event (no SPI hardware)\n";
  std::cout << "PASS: MIDI parser (running status, realtime, SysEx skip) + event codecs\n";
}

#include "protocol.hpp"
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
  std::cout << "PASS: framed loopback + simulated debounced control event (no SPI hardware)\n";
}

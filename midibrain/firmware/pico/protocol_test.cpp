#include "protocol.hpp"
#include "midi.hpp"
#include "panel_input.hpp"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <vector>

namespace {
struct Feed {
  std::uint32_t at_ms;
  bool pressed;
};

struct Emitted {
  std::uint32_t at_ms;
  std::uint8_t id;
  std::uint8_t value;
};

// Drive one PanelControl through a level timeline at the firmware's 1 kHz
// scan rate and collect the EV_CONTROL events it emits.
std::vector<Emitted> drive(mb::PanelControl& control, const std::vector<Feed>& feeds,
                           std::uint32_t until_ms) {
  std::vector<Emitted> events;
  std::size_t next = 0;
  bool level = false;
  for (std::uint32_t t = 0; t <= until_ms; ++t) {
    while (next < feeds.size() && feeds[next].at_ms == t) level = feeds[next++].pressed;
    const mb::PanelControl::Result r = control.update(level, t);
    if (r.event) events.push_back({t, control.id(), r.value});
  }
  return events;
}

// Encode one EV_CONTROL exactly as the Pico would (fixed session and sequence
// make the log reproducible; payload bytes are the contract).
void print_control_event(std::uint32_t at_ms, std::uint8_t id, std::uint8_t value) {
  mb::Frame frame{};
  assert(mb::pack_control(frame, 1, 0x12345678u, id, value));
  mb::Bytes wire{};
  assert(mb::encode(frame, wire));
  char hex[2 * mb::frame_size + 1] = {};
  for (std::size_t i = 0; i < wire.size(); ++i)
    std::snprintf(hex + 2 * i, 3, "%02x", static_cast<unsigned>(wire[i]));
  std::printf("t=%4u ms  id=%2u  value=%u  payload=%02x %02x %02x  frame=%s\n",
              static_cast<unsigned>(at_ms), static_cast<unsigned>(id),
              static_cast<unsigned>(value), static_cast<unsigned>(wire[12]),
              static_cast<unsigned>(wire[13]), static_cast<unsigned>(wire[14]), hex);
}

void print_gesture(const char* name, std::uint8_t id, const std::vector<Feed>& feeds,
                   std::uint32_t until_ms) {
  mb::PanelControl control(id);
  const std::vector<Emitted> events = drive(control, feeds, until_ms);
  std::printf("%s\n", name);
  if (events.empty()) std::printf("  (no events)\n");
  for (const auto& e : events) print_control_event(e.at_ms, e.id, e.value);
}

void check_events(const char* name, std::uint8_t id,
                  const std::vector<Feed>& feeds, std::uint32_t until_ms,
                  const std::vector<Emitted>& expected) {
  mb::PanelControl control(id);
  const std::vector<Emitted> got = drive(control, feeds, until_ms);
  assert(got.size() == expected.size());
  for (std::size_t i = 0; i < expected.size(); ++i) {
    assert(got[i].at_ms == expected[i].at_ms);
    assert(got[i].id == expected[i].id);
    assert(got[i].value == expected[i].value);
  }
  (void)name;
}

void run_panel_tests() {
  // NAV direction press+release: debounced edge pair, no repeat.
  check_events("nav press+release", mb::CTL_NAV_UP, {{100, true}, {300, false}},
               400, {{{110, mb::CTL_NAV_UP, mb::CTL_STATE_PRESSED},
                       {310, mb::CTL_NAV_UP, mb::CTL_STATE_RELEASED}}});
  // NAV direction held 0.8 s: press edge, first repeat 400 ms later, then
  // every 80 ms, single release on let-go.
  check_events("nav hold repeat", mb::CTL_NAV_UP, {{100, true}, {900, false}},
               1000, {{{110, mb::CTL_NAV_UP, mb::CTL_STATE_PRESSED},
                        {510, mb::CTL_NAV_UP, mb::CTL_STATE_PRESSED},
                        {590, mb::CTL_NAV_UP, mb::CTL_STATE_PRESSED},
                        {670, mb::CTL_NAV_UP, mb::CTL_STATE_PRESSED},
                        {750, mb::CTL_NAV_UP, mb::CTL_STATE_PRESSED},
                        {830, mb::CTL_NAV_UP, mb::CTL_STATE_PRESSED},
                        {910, mb::CTL_NAV_UP, mb::CTL_STATE_RELEASED}}});
  // NAV_PRESS held 1.4 s: exactly press + release, never repeats.
  check_events("nav press no repeat", mb::CTL_NAV_PRESS,
               {{100, true}, {1500, false}}, 1600,
               {{{110, mb::CTL_NAV_PRESS, mb::CTL_STATE_PRESSED},
                 {1510, mb::CTL_NAV_PRESS, mb::CTL_STATE_RELEASED}}});
  // SHIFT short press: 1 -> 0 with no long-hold state 2.
  check_events("shift short", mb::CTL_SHIFT_EXIT, {{100, true}, {400, false}},
               500, {{{110, mb::CTL_SHIFT_EXIT, mb::CTL_STATE_PRESSED},
                       {410, mb::CTL_SHIFT_EXIT, mb::CTL_STATE_RELEASED}}});
  // SHIFT long hold: state 2 exactly once past 500 ms, then release.
  check_events("shift long", mb::CTL_SHIFT_EXIT, {{100, true}, {1200, false}},
               1300, {{{110, mb::CTL_SHIFT_EXIT, mb::CTL_STATE_PRESSED},
                        {610, mb::CTL_SHIFT_EXIT, mb::CTL_STATE_LONG_HOLD},
                        {1210, mb::CTL_SHIFT_EXIT, mb::CTL_STATE_RELEASED}}});
  // Bouncy edges inside the 10 ms window collapse to one press + release.
  check_events("bouncy nav", mb::CTL_NAV_DOWN,
               {{100, true}, {103, false}, {105, true}, {300, false}}, 400,
               {{{115, mb::CTL_NAV_DOWN, mb::CTL_STATE_PRESSED},
                 {310, mb::CTL_NAV_DOWN, mb::CTL_STATE_RELEASED}}});
}

void run_evidence() {
  std::printf("EV_CONTROL evidence from PanelControl + pack_control + encode\n");
  std::printf("scan 1 kHz, debounce 10 ms, NAV repeat 400 ms then 80 ms, SHIFT long 500 ms\n");
  std::printf("session=0x12345678 sequence=1 (fixed for reproducibility)\n\n");
  print_gesture("NAV_UP press+release", mb::CTL_NAV_UP, {{100, true}, {300, false}}, 400);
  print_gesture("NAV_DOWN press+release", mb::CTL_NAV_DOWN, {{100, true}, {300, false}}, 400);
  print_gesture("NAV_LEFT press+release", mb::CTL_NAV_LEFT, {{100, true}, {300, false}}, 400);
  print_gesture("NAV_RIGHT press+release", mb::CTL_NAV_RIGHT, {{100, true}, {300, false}}, 400);
  print_gesture("NAV_PRESS held 1.4 s (no repeat)", mb::CTL_NAV_PRESS,
                {{100, true}, {1500, false}}, 1600);
  print_gesture("SHIFT_EXIT short press (no state 2)", mb::CTL_SHIFT_EXIT,
                {{100, true}, {400, false}}, 500);
  print_gesture("SHIFT_EXIT long hold (state 2)", mb::CTL_SHIFT_EXIT,
                {{100, true}, {1200, false}}, 1300);
  print_gesture("NAV_UP held 0.8 s (auto-repeat)", mb::CTL_NAV_UP,
                {{100, true}, {900, false}}, 1000);
}
}  // namespace

int main(int argc, char** argv) {
  if (argc == 2 && std::strcmp(argv[1], "--evidence") == 0) {
    run_evidence();
    return 0;
  }
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
  run_panel_tests();
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
  assert(pack_transport_state(sf, 11, 0x12345678, true, 0x00000048, true, false));
  assert(encode(sf, wire) && decode(wire, decoded));
  assert(decoded.payload[0] == EV_TRANSPORT_STATE && decoded.length == 8);
  assert(decoded.payload[1] == 1);
  assert((decoded.payload[2] | (decoded.payload[3] << 8) | (decoded.payload[4] << 16) | (decoded.payload[5] << 24)) == 0x48);
  assert(decoded.payload[6] == LED_REC && decoded.payload[7] == 0);
  // Indicator matrix: solid armed+rolling, blinking armed+stopped, play on roll.
  const IndicatorState off{}, rec{true, false, false, true}, recBlink{true, false, false, false};
  const IndicatorState recRoll{true, false, true, false}, od{false, true, false, true};
  const IndicatorState run{false, false, true, false}, both{true, true, true, true};
  assert(indicator_mask(off) == 0);
  assert(indicator_mask(rec) == LED_REC);
  assert(indicator_mask(recBlink) == 0);
  assert(indicator_mask(recRoll) == (LED_REC | LED_PLAY));
  assert(indicator_mask(od) == LED_OVERDUB);
  assert(indicator_mask(run) == LED_PLAY);
  assert(indicator_mask(both) == (LED_REC | LED_OVERDUB | LED_PLAY));
  // LED_SET interpreter: plain partial overwrite vs arm sync on bit 7.
  LedCommand plain = interpret_led_set(LED_PLAY, LED_PLAY);
  assert(!plain.sync_arms);
  LedCommand sync = interpret_led_set(LED_ARM_SYNC, LED_REC | LED_OVERDUB);
  assert(sync.sync_arms && sync.rec_arm && sync.overdub_arm);
  LedCommand syncOff = interpret_led_set(LED_ARM_SYNC, 0);
  assert(syncOff.sync_arms && !syncOff.rec_arm && !syncOff.overdub_arm);
  // Control plane: STATUS_REQ/STATUS_RESP, REBOOT and RESET_LINK round trips.
  Frame sq{};
  assert(pack_status_req(sq, 12, 0x12345678));
  assert(encode(sq, wire) && decode(wire, decoded));
  assert(decoded.payload[0] == EV_STATUS_REQ && decoded.length == 1);
  LinkStatus status{0x01020304, 0x11223344, 7, 0x2233, 0x4455, STATUS_LINK_UP | STATUS_ROLLING};
  Frame sr{};
  assert(pack_status_resp(sr, 13, 0x12345678, status));
  assert(encode(sr, wire) && decode(wire, decoded));
  assert(decoded.payload[0] == EV_STATUS_RESP && decoded.length == 15);
  LinkStatus out{};
  assert(unpack_status_resp(decoded, out));
  assert(out.uptime_s == 0x01020304 && out.ticks == 0x11223344);
  assert(out.txq_depth == 7 && out.txov == 0x2233 && out.rxov == 0x4455);
  assert(out.flags == (STATUS_LINK_UP | STATUS_ROLLING));
  Frame wrong = decoded;
  wrong.payload[0] = EV_STATUS_REQ;
  assert(!unpack_status_resp(wrong, out));
  wrong = decoded;
  wrong.length = 14;
  assert(!unpack_status_resp(wrong, out));
  Frame rb{};
  assert(pack_reboot(rb, 14, 0x12345678, 0));
  assert(encode(rb, wire) && decode(wire, decoded));
  assert(decoded.payload[0] == EV_REBOOT && decoded.length == 2 && decoded.payload[1] == 0);
  assert(pack_reboot(rb, 15, 0x12345678, 1) && rb.payload[1] == 1);
  assert(!pack_reboot(rb, 15, 0x12345678, 2));
  Frame rl{};
  assert(pack_reset_link(rl, 16, 0x12345678));
  assert(encode(rl, wire) && decode(wire, decoded));
  assert(decoded.payload[0] == EV_RESET_LINK && decoded.length == 1);
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
  // SHIFT hold detector: edges pass through, long fires once while held.
  ShiftHold sh;
  assert(sh.update(false, 0) == -1);
  assert(sh.update(true, 100) == 1);
  assert(sh.update(true, 599) == -1);
  assert(sh.update(true, 600) == 2);  // exact threshold
  assert(sh.update(true, 601) == -1);  // once only
  assert(sh.update(false, 602) == 0);
  assert(sh.update(false, 603) == -1);
  assert(sh.update(true, 700) == 1);  // re-press works
  assert(sh.update(false, 750) == 0);  // short press: no long event
  ShiftHold shwrap;
  assert(shwrap.update(true, 0xFFFFFF00u) == 1);
  assert(shwrap.update(true, 0xFFFFFF00u + 499) == -1);
  assert(shwrap.update(true, 0xFFFFFF00u + 500) == 2);  // wraparound-safe
  assert(shwrap.update(false, 10) == 0);
  // EV_CONTROL vocabulary and repeat eligibility are frozen in midi.hpp.
  assert(CTL_STATE_RELEASED == 0 && CTL_STATE_PRESSED == 1 && CTL_STATE_LONG_HOLD == 2);
  assert(kNavRepeatInitialMs == 400 && kNavRepeatMs == 80 && kShiftLongMs == 500);
  assert(is_nav_direction(CTL_NAV_UP) && is_nav_direction(CTL_NAV_DOWN));
  assert(is_nav_direction(CTL_NAV_LEFT) && is_nav_direction(CTL_NAV_RIGHT));
  assert(!is_nav_direction(CTL_NAV_PRESS) && !is_nav_direction(CTL_ENCODER_SW));
  assert(!is_nav_direction(CTL_SHIFT_EXIT) && !is_nav_direction(CTL_REC));

  // Debounce: a bouncy NAV_UP press still emits exactly one press event.
  PanelControl up(CTL_NAV_UP);
  const auto bouncy = drive(up, {{100, true}, {103, false}, {105, true}, {114, false},
                                 {115, true}, {300, false}}, 400);
  assert(bouncy.size() == 2);
  assert(bouncy[0].at_ms == 125 && bouncy[0].id == CTL_NAV_UP &&
         bouncy[0].value == CTL_STATE_PRESSED);
  assert(bouncy[1].at_ms == 310 && bouncy[1].value == CTL_STATE_RELEASED);

  // Direction hold: press at 110, repeat 400 ms later, then every 80 ms.
  PanelControl held_up(CTL_NAV_UP);
  const auto repeats = drive(held_up, {{100, true}, {900, false}}, 1000);
  assert(repeats.size() == 7);
  const std::uint32_t repeat_times[] = {110, 510, 590, 670, 750, 830, 910};
  for (std::size_t i = 0; i < repeats.size(); ++i) {
    assert(repeats[i].at_ms == repeat_times[i] && repeats[i].id == CTL_NAV_UP);
    assert(repeats[i].value ==
           (i + 1 < repeats.size() ? CTL_STATE_PRESSED : CTL_STATE_RELEASED));
  }
  // Release resets the repeat clock: the next press gets a full 400 ms.
  const auto repressed = drive(held_up, {{2000, true}, {2450, false}}, 2500);
  assert(repressed.size() == 3);
  assert(repressed[0].at_ms == 2010 && repressed[0].value == CTL_STATE_PRESSED);
  assert(repressed[1].at_ms == 2410 && repressed[1].value == CTL_STATE_PRESSED);
  assert(repressed[2].at_ms == 2460 && repressed[2].value == CTL_STATE_RELEASED);

  // A starved scan emits at most one repeat: no catch-up burst.
  PanelControl starved(CTL_NAV_DOWN);
  assert(!starved.update(true, 100).event);  // debounce not elapsed
  assert(starved.update(true, 110).event);   // press
  const auto gap = starved.update(true, 2000);
  assert(gap.event && gap.value == CTL_STATE_PRESSED);
  assert(!starved.update(true, 2001).event);
  assert(!starved.update(false, 2010).event);
  const auto gap_release = starved.update(false, 2020);
  assert(gap_release.event && gap_release.value == CTL_STATE_RELEASED);

  // Repeat timing stays correct across 32-bit millisecond wraparound.
  NavRepeat wrap_repeat;
  assert(wrap_repeat.update(true, 0xFFFFFF00u));
  assert(!wrap_repeat.update(true, 0xFFFFFF00u + 399));
  assert(wrap_repeat.update(true, 0xFFFFFF00u + 400));
  assert(!wrap_repeat.update(true, 0xFFFFFF00u + 479));
  assert(wrap_repeat.update(true, 0xFFFFFF00u + 480));
  assert(!wrap_repeat.update(false, 10));

  // NAV_PRESS is a single press edge: held 1.4 s, no repeat.
  PanelControl press(CTL_NAV_PRESS);
  const auto click = drive(press, {{100, true}, {1500, false}}, 1600);
  assert(click.size() == 2);
  assert(click[0].at_ms == 110 && click[0].value == CTL_STATE_PRESSED);
  assert(click[1].at_ms == 1510 && click[1].value == CTL_STATE_RELEASED);

  // SHIFT/EXIT: short press never reports 2; long hold reports it once.
  PanelControl exit_short(CTL_SHIFT_EXIT);
  const auto short_press = drive(exit_short, {{100, true}, {400, false}}, 500);
  assert(short_press.size() == 2);
  assert(short_press[0].at_ms == 110 && short_press[0].value == CTL_STATE_PRESSED);
  assert(short_press[1].at_ms == 410 && short_press[1].value == CTL_STATE_RELEASED);
  PanelControl exit_long(CTL_SHIFT_EXIT);
  const auto long_hold = drive(exit_long, {{100, true}, {1200, false}}, 1300);
  assert(long_hold.size() == 3);
  assert(long_hold[0].at_ms == 110 && long_hold[0].value == CTL_STATE_PRESSED);
  assert(long_hold[1].at_ms == 610 && long_hold[1].value == CTL_STATE_LONG_HOLD);
  assert(long_hold[2].at_ms == 1210 && long_hold[2].value == CTL_STATE_RELEASED);

  // Every emitted state encodes to the documented EV_CONTROL frame.
  for (const auto* events : {&repeats, &click, &short_press, &long_hold}) {
    for (const auto& e : *events) {
      Frame cf{};
      Bytes cb{};
      assert(pack_control(cf, 1, 0x12345678u, e.id, e.value));
      assert(encode(cf, cb) && decode(cb, decoded));
      assert(decoded.type == Type::control_event && decoded.length == 3);
      assert(decoded.payload[0] == EV_CONTROL && decoded.payload[1] == e.id);
      assert(decoded.payload[2] == e.value);
    }
  }
  std::cout << "PASS: framed loopback + simulated debounced control event (no SPI hardware)\n";
  std::cout << "PASS: MIDI parser (running status, realtime, SysEx skip) + event codecs\n";
}

#include "protocol.hpp"
#include "clock_follow.hpp"
#include "clock_scheduler.hpp"
#include "midi_tx_scheduler.hpp"
#include "route_chain.hpp"
#include "clock_output.hpp"
#include "midi.hpp"
#include "pipe_event.hpp"
#include "panel_input.hpp"
#include "encoder_input.hpp"
#include <cassert>
#include <cmath>
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

// Synthetic A/B timeline for the value knob: each entry is a sampled level
// pair (bit 1 = A, bit 0 = B, as gpio_get reports them) held for a dwell in
// microseconds. The driver walks it at the firmware's 1 kHz scan cadence and
// collects the deltas the encoder reports.
typedef std::vector<std::pair<std::uint8_t, std::uint32_t>> KnobTimeline;

std::vector<int> drive_encoder(mb::ValueEncoder& encoder, std::uint8_t idle,
                               const KnobTimeline& timeline, bool fine,
                               std::uint32_t start_us = 0) {
  std::vector<int> deltas;
  std::uint32_t now_us = start_us;
  encoder.reset((idle & 2u) != 0, (idle & 1u) != 0);
  for (const auto& entry : timeline) {
    for (std::uint32_t elapsed = 0; elapsed < entry.second; elapsed += 1000, now_us += 1000) {
      const mb::ValueEncoder::Result r =
          encoder.update((entry.first & 2u) != 0, (entry.first & 1u) != 0, fine, now_us);
      if (r.turned) deltas.push_back(r.delta);
    }
  }
  return deltas;
}

// Append `pulses` quarter-turn pulses: A leads clockwise (00 10 11 01 00), B
// leads counter-clockwise. Each state is held `edge_us`, the idle state after
// a pulse `gap_us`, so a pulse completes one base step every four edges.
void knob_turns(KnobTimeline& timeline, bool clockwise, int pulses,
                std::uint32_t edge_us, std::uint32_t gap_us) {
  static const std::uint8_t cw[3] = {2, 3, 1};
  static const std::uint8_t ccw[3] = {1, 3, 2};
  for (int p = 0; p < pulses; ++p) {
    for (int i = 0; i < 3; ++i) timeline.push_back({clockwise ? cw[i] : ccw[i], edge_us});
    timeline.push_back({0, gap_us});
  }
}

int knob_sum(const std::vector<int>& deltas) {
  int sum = 0;
  for (int d : deltas) sum += d;
  return sum;
}

// The knob is a detentless 24-pulse/revolution encoder with acceleration. These
// tests pin the properties the Pi relies on: one step per pulse when slow, a
// multiplied (int8) delta when flicked, no acceleration in SHIFT/fine mode, no
// invented steps from contact bounce, and no drift from a sampled jump.
void run_value_encoder_tests() {
  using namespace mb;

  // Slow clockwise turn, edges 15 ms apart: 10 pulses -> 10 steps of +1.
  {
    ValueEncoder encoder;
    KnobTimeline slow;
    knob_turns(slow, true, 10, 15'000, 10'000);
    const auto deltas = drive_encoder(encoder, 0, slow, false);
    assert(deltas.size() == 10);
    for (int d : deltas) assert(d == 1);
  }

  // Three edges are not a step; the fourth completes exactly one.
  {
    ValueEncoder encoder;
    const std::vector<int> partial =
        drive_encoder(encoder, 0, {{2, 20'000}, {3, 20'000}, {1, 20'000}}, false);
    assert(partial.empty());
    ValueEncoder completion;
    const std::vector<int> complete =
        drive_encoder(completion, 0, {{2, 20'000}, {3, 20'000}, {1, 20'000}, {0, 20'000}}, false);
    assert(complete.size() == 1 && complete[0] == 1);
  }

  // Counter-clockwise is the negative direction, same magnitude.
  {
    ValueEncoder encoder;
    KnobTimeline ccw;
    knob_turns(ccw, false, 4, 15'000, 10'000);
    const auto deltas = drive_encoder(encoder, 0, ccw, false);
    assert(deltas.size() == 4);
    for (int d : deltas) assert(d == -1);
  }

  // Acceleration buckets, measured between completed steps: 28 ms -> x2,
  // 12 ms -> x4, 4 ms -> x8. The 1 kHz scan cannot report faster than that.
  {
    ValueEncoder encoder;
    KnobTimeline medium;
    knob_turns(medium, true, 4, 7'000, 1'000);
    const auto deltas = drive_encoder(encoder, 0, medium, false);
    assert(deltas.size() == 4 && deltas[0] == 1 && deltas[1] == 2 && deltas[3] == 2);
  }
  {
    ValueEncoder encoder;
    KnobTimeline fast;
    knob_turns(fast, true, 4, 3'000, 1'000);
    const auto deltas = drive_encoder(encoder, 0, fast, false);
    assert(deltas.size() == 4 && deltas[0] == 1 && deltas[1] == 4 && deltas[3] == 4);
  }
  {
    ValueEncoder encoder;
    KnobTimeline flick;
    knob_turns(flick, true, 6, 1'000, 1'000);
    const auto deltas = drive_encoder(encoder, 0, flick, false);
    assert(deltas.size() == 6 && deltas[0] == 1 && deltas[1] == 8 && deltas[5] == 8);
  }

  // SHIFT/fine mode never accelerates: the Pi applies its own fine increment.
  {
    ValueEncoder encoder;
    KnobTimeline flick;
    knob_turns(flick, false, 6, 1'000, 1'000);
    const auto deltas = drive_encoder(encoder, 0, flick, true);
    assert(deltas.size() == 6);
    for (int d : deltas) assert(d == -1);
  }

  // Contact bounce (datasheet 2.0 ms max at 15 rpm) cancels in the edge
  // accumulator: a bounce pair on A adds no step and no net drift.
  {
    ValueEncoder encoder;
    KnobTimeline clean;
    knob_turns(clean, true, 6, 15'000, 10'000);
    const auto clean_deltas = drive_encoder(encoder, 0, clean, false);

    ValueEncoder bouncy;
    KnobTimeline noisy;
    knob_turns(noisy, true, 6, 15'000, 10'000);
    for (std::size_t i = 0; i < noisy.size(); ++i) {
      if (noisy[i].first != 2) continue;
      noisy.insert(noisy.begin() + static_cast<std::ptrdiff_t>(i) + 1,
                   {{0, 1'000}, {2, 1'000}});  // A chatters low for 1 ms, twice
      i += 2;
    }
    const auto noisy_deltas = drive_encoder(bouncy, 0, noisy, false);
    assert(noisy_deltas.size() == clean_deltas.size());
    assert(knob_sum(noisy_deltas) == knob_sum(clean_deltas));
  }

  // Chatter with no real rotation never reports a turn.
  {
    ValueEncoder encoder;
    KnobTimeline chatter;
    for (int i = 0; i < 6; ++i) chatter.push_back({static_cast<std::uint8_t>(i % 2 == 0 ? 2 : 0), 1'000});
    assert(drive_encoder(encoder, 0, chatter, false).empty());
  }

  // A sampled double transition (both channels changed between scans) is not a
  // turn; the state machine resynchronizes and the following edges count.
  {
    ValueEncoder encoder;
    const auto jumped =
        drive_encoder(encoder, 0, {{3, 2'000}, {1, 2'000}, {0, 2'000}, {2, 2'000}}, false);
    assert(jumped.empty());  // three valid edges: the 00 -> 11 jump added none
    ValueEncoder resync;
    const auto completed = drive_encoder(
        resync, 0, {{3, 2'000}, {1, 2'000}, {0, 2'000}, {2, 2'000}, {3, 2'000}}, false);
    assert(completed.size() == 1 && completed[0] == 1);
  }

  // Microsecond-clock wraparound keeps the interval (and the bucket) correct.
  {
    ValueEncoder encoder;
    KnobTimeline slow;
    knob_turns(slow, true, 3, 15'000, 10'000);
    const auto deltas = drive_encoder(encoder, 0, slow, false, 0xFFFFFF00u);
    assert(deltas.size() == 3);
    for (int d : deltas) assert(d == 1);
  }

  // The delta is the frozen int8 CTL_ENCODER_TURN payload, not a new code.
  {
    Frame frame{};
    Bytes bytes{};
    Frame decoded{};
    assert(pack_control(frame, 7, 0x12345678u, CTL_ENCODER_TURN, static_cast<std::uint8_t>(-8)));
    assert(encode(frame, bytes) && decode(bytes, decoded));
    assert(decoded.type == Type::control_event && decoded.length == 3);
    assert(decoded.payload[0] == EV_CONTROL && decoded.payload[1] == CTL_ENCODER_TURN);
    assert(static_cast<std::int8_t>(decoded.payload[2]) == -8);
  }

  std::printf("PASS: value-knob decode (smooth steps, acceleration, fine mode, "
              "bounce, wraparound, int8 delta)\n");
}

mb::MidiMessage test_message(std::uint8_t status, std::uint8_t data1 = 0,
                             std::uint8_t data2 = 0, std::uint8_t size = 3) {
  mb::MidiMessage m{};
  m.size = size;
  m.data[0] = status;
  m.data[1] = data1;
  m.data[2] = data2;
  return m;
}

// The 0x18/0x19/0x1A/0x1B wire frames for the MIDI FX route pipes. Chain
// behavior lives in the shared core suite (midifx/tests/test_core.cpp); this
// test proves the firmware wire decode reaches the core with the right types,
// chunked params, per-pipe channel scope and MidiMessage conversion.
void run_wire_config_tests() {
  using namespace mb;
  const mnky::midifx::PipeEvent converted = pipeEvent(test_message(0x90, 60, 100));
  assert(converted.size == 3 && converted.data[0] == 0x90 &&
         converted.data[1] == 60 && converted.data[2] == 100);
  const MidiMessage malformed{0, 4, {0x90, 60, 100}, 0};
  assert(pipeEvent(malformed).size == 0);

  // Route begin, a chunked pipe carrying a channel scope, a route send and the
  // clock-extras frame.
  {
    Frame f{};
    Bytes wire{};
    Frame decoded{};
    assert(pack_midi_route(f, 30, 0x12345678, 1, 0, ROUTE_FLAG_ON));
    assert(encode(f, wire) && decode(wire, decoded));
    std::uint8_t src = 9, dst = 9, flags = 0;
    assert(unpack_midi_route(decoded, src, dst, flags));
    assert(src == 1 && dst == 0 && flags == ROUTE_FLAG_ON);
    assert(!pack_midi_route(f, 31, 0x12345678, 2, 0, 0));
    assert(!pack_midi_route(f, 31, 0x12345678, 0, 0, 0x80));

    const std::int16_t params[4] = {60, 64, 64, 127};
    assert(pack_midi_route_pipe(f, 32, 0x12345678, 0, 1, 2, 2, false, 0, params, 4,
                                0x0003));
    assert(f.length == 10 + 2 * 4);
    assert(encode(f, wire) && decode(wire, decoded));
    std::uint8_t index = 0, type = 0, offset = 0;
    bool bypass = true;
    std::uint16_t ch_mask = 0;
    std::int16_t out_params[route_pipe_param_frame_max_legacy]{};
    std::size_t count = 0;
    assert(unpack_midi_route_pipe(decoded, src, dst, index, type, bypass, offset,
                                  ch_mask, out_params,
                                  route_pipe_param_frame_max_legacy, count));
    assert(src == 0 && dst == 1 && index == 2 && type == 2 && !bypass);
    assert(offset == 0 && count == 4 && out_params[0] == 60 && out_params[3] == 127);
    assert(ch_mask == 0x0003);
    // Five params need two current chunks; the scope rides the offset-0 one.
    assert(!pack_midi_route_pipe(f, 33, 0x12345678, 0, 1, 0, 2, false, 0, params, 5));
    const std::int16_t six[6] = {1, 2, 3, 4, 5, 6};
    assert(!pack_midi_route_pipe(f, 33, 0x12345678, 0, 1, 0, 2, false, 5, six, 6));
    assert(!pack_midi_route_pipe(f, 33, 0x12345678, 0, 1, 0, 2, false, 0, params, 5));

    // Backward tolerance: a pre-scope frame (length 8 + 2*count, five params,
    // no mask) still decodes and means chMask = all channels.
    Frame legacy{};
    legacy.type = Type::control_event;
    legacy.sequence = 40;
    legacy.session = 0x12345678;
    legacy.length = static_cast<std::uint8_t>(8 + 2 * 5);
    legacy.payload[0] = EV_MIDI_ROUTE_PIPE;
    legacy.payload[1] = 0;
    legacy.payload[2] = 1;
    legacy.payload[3] = 0;
    legacy.payload[4] = 2;
    legacy.payload[5] = 0;
    legacy.payload[6] = 0;
    legacy.payload[7] = 5;
    const std::int16_t legacy_params[5] = {60, 64, 64, 127, -7};
    for (std::size_t i = 0; i < 5; ++i) {
      const auto bits = static_cast<std::uint16_t>(legacy_params[i]);
      legacy.payload[8 + 2 * i] = static_cast<std::uint8_t>(bits);
      legacy.payload[9 + 2 * i] = static_cast<std::uint8_t>(bits >> 8);
    }
    assert(encode(legacy, wire) && decode(wire, decoded));
    ch_mask = 0;
    assert(unpack_midi_route_pipe(decoded, src, dst, index, type, bypass, offset,
                                  ch_mask, out_params,
                                  route_pipe_param_frame_max_legacy, count));
    assert(count == 5 && out_params[4] == -7);
    assert(ch_mask == route_pipe_channel_mask_default);
    // A scope frame cannot smuggle the legacy fifth param past the 18-byte
    // payload: a length-20 scope frame is rejected before its mask is read.
    Frame oversize{};
    oversize.type = Type::control_event;
    oversize.length = 20;
    oversize.payload[0] = EV_MIDI_ROUTE_PIPE;
    oversize.payload[1] = 0;
    oversize.payload[2] = 1;
    oversize.payload[3] = 0;
    oversize.payload[4] = 2;
    oversize.payload[5] = 0;
    oversize.payload[6] = 0;
    oversize.payload[7] = 5;
    assert(!unpack_midi_route_pipe(oversize, src, dst, index, type, bypass, offset,
                                   ch_mask, out_params,
                                   route_pipe_param_frame_max_legacy, count));

    const std::uint8_t bytes[3] = {0x90, 60, 100};
    assert(pack_midi_route_send(f, 34, 0x12345678, 1, 0, bytes, 3));
    assert(encode(f, wire) && decode(wire, decoded));
    std::uint8_t size = 0;
    std::uint8_t out_bytes[3]{};
    assert(unpack_midi_route_send(decoded, src, dst, out_bytes, size));
    assert(src == 1 && dst == 0 && size == 3 && out_bytes[2] == 100);
    assert(!pack_midi_route_send(f, 35, 0x12345678, 0, 0, bytes, 4));

    assert(pack_clock_route_ext(f, 36, 0x12345678, 1, 4, 500));
    assert(encode(f, wire) && decode(wire, decoded));
    std::uint8_t port = 9, div = 0;
    std::uint16_t delay = 0;
    assert(unpack_clock_route_ext(decoded, port, div, delay));
    assert(port == 1 && div == 4 && delay == 500);
    // A corrupt/out-of-contract divider still reaches applyExtras, which
    // clamps it to 1 and the delay to 500 instead of silencing the output.
    decoded.payload[2] = 3;
    decoded.payload[3] = 0x58;
    decoded.payload[4] = 0x02;  // 600 ms
    assert(unpack_clock_route_ext(decoded, port, div, delay));
    ClockGates clamped;
    assert(clamped.applyExtras(port, div, delay));
    assert(clamped.tempoDiv(1) == 1 && clamped.syncDelayMs(1) == 500);
    assert(!pack_clock_route_ext(f, 37, 0x12345678, 0, 3, 0));
    assert(!pack_clock_route_ext(f, 37, 0x12345678, 2, 1, 0));
  }

  // Sends one pipe through real 0x19 frames in wire-limit chunks. The scope
  // travels on the offset-0 chunk; a continuation frame's mask byte is ignored
  // by the firmware exactly like its bypass byte.
  auto send_pipe = [](mnky::midifx::PipeChain& chain, std::uint8_t index,
                      std::uint8_t type, const std::int16_t* params,
                      std::size_t param_count, std::uint16_t ch_mask) {
    std::size_t offset = 0;
    do {
      const std::size_t chunk = param_count - offset < route_pipe_param_frame_max
                                    ? param_count - offset
                                    : route_pipe_param_frame_max;
      Frame f{};
      Bytes wire{};
      Frame decoded{};
      const std::int16_t* chunk_params =
          params == nullptr ? nullptr : params + offset;
      assert(pack_midi_route_pipe(f, 0, 0x12345678, 0, 1, index, type, false,
                                  static_cast<std::uint8_t>(offset), chunk_params,
                                  chunk, ch_mask));
      assert(encode(f, wire) && decode(wire, decoded));
      std::uint8_t src = 9, dst = 9, out_index = 9, out_type = 0, out_offset = 9;
      bool bypass = true;
      std::uint16_t out_mask = 0;
      std::int16_t raw[route_pipe_param_frame_max_legacy]{};
      std::size_t count = 0;
      assert(unpack_midi_route_pipe(decoded, src, dst, out_index, out_type, bypass,
                                    out_offset, out_mask, raw,
                                    route_pipe_param_frame_max_legacy, count));
      assert(src == 0 && dst == 1 && out_index == index && out_type == type);
      assert(!bypass && out_offset == offset && count == chunk);
      assert(out_mask == ch_mask);
      std::int32_t params32[route_pipe_param_frame_max_legacy]{};
      for (std::size_t i = 0; i < count; ++i) params32[i] = raw[i];
      assert(chain.setPipeChunk(out_index, out_type, bypass, out_offset, params32,
                                count, out_mask));
      offset += chunk;
    } while (offset < param_count);
  };

  // Every Wave-2 type fits the 0x19 transport with its real positional params
  // (the five-param LFO/harmonizer need two chunks).
  {
    mnky::midifx::PipeChain chain;
    chain.begin(true);
    const std::int16_t repeater[3] = {1, 4, 30};
    const std::int16_t lfo[5] = {74, 3, 2, 100, 64};
    const std::int16_t harmonizer[5] = {4, -12, -5, 5, 12};
    const std::int16_t delay[3] = {250, 3, 25};
    const std::int16_t* per_type[4] = {repeater, lfo, harmonizer, delay};
    const std::size_t counts[4] = {3, 5, 5, 3};
    for (std::size_t i = 0; i < 4; ++i)
      send_pipe(chain, static_cast<std::uint8_t>(i),
                static_cast<std::uint8_t>(13 + i), per_type[i], counts[i],
                route_pipe_channel_mask_default);
    assert(chain.pipeCount() == 4);
    // The repeater holds the note-on, the LFO emits CCs on the grid and the
    // delay echoes them 250 ms later (the train's ms clock is tick * 21).
    assert(chain.process(pipeEvent(test_message(0x90, 60, 100)), 0).count == 0);
    assert(chain.clockTick(0, 0).count == 0);
    const auto lfo_cc = chain.clockTick(6, 126);
    assert(lfo_cc.count == 1 && (lfo_cc.events[0].data[0] & 0xF0) == 0xB0 &&
           lfo_cc.events[0].data[1] == 74);
    const auto echo = chain.tick(376);
    assert(echo.count >= 1 && (echo.events[0].data[0] & 0xF0) == 0xB0);
  }

  // An Arp needs six positional params: two chunks at offsets 0 and 4, then the
  // 1/32 grid starts on tick 3.
  {
    mnky::midifx::PipeChain chain;
    chain.begin(true);
    chain.setSeed(0x12345678u);
    const std::int16_t arp[6] = {3, 0, 1, 50, 0, 50};
    send_pipe(chain, 0, 12, arp, 6, route_pipe_channel_mask_default);
    assert(chain.pipeCount() == 1);
    assert(chain.process(pipeEvent(test_message(0x90, 60, 100)), 0).count == 0);
    assert(chain.clockTick(0, 0).count == 0);
    const auto step = chain.clockTick(3, 63);
    assert(step.count == 1 && (step.events[0].data[0] & 0xF0) == 0x90 &&
           step.events[0].data[1] == 60);
  }

  // The channel scope reaches the engine through 0x19: a note-on-blocking
  // Filter scoped to channel 1 drops channel 1 and leaves channel 0 alone; the
  // continuation chunk cannot move the scope once set.
  {
    mnky::midifx::PipeChain chain;
    chain.begin(true);
    const std::int16_t blocking[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    send_pipe(chain, 0, 1, blocking, 8, 0x0002);
    const auto in_scope =
        chain.process(pipeEvent(test_message(0x91, 60, 100)), 0);
    const auto out_of_scope =
        chain.process(pipeEvent(test_message(0x90, 60, 100)), 0);
    assert(in_scope.count == 0);
    assert(out_of_scope.count == 1);
  }

  std::printf("PASS: MIDI route pipe wire frames (0x18/0x19/0x1A/0x1B, chMask, "
              "MidiMessage conversion)\n");
}

// Generator and output-insert route kinds: the same RouteChains class the
// firmware main loop uses, so the wire pseudo-source bytes, generator-head
// legality, insert ordering and the no-input generator path are tested here.
void run_route_chain_tests() {
  using namespace mb;
  using mnky::midifx::PipeOutput;

  // Pseudo-source bytes are the 0x18/0x19 src field; 0/1 stay the TRS inputs.
  assert(route_src_generator == 0xFF);
  assert(route_src_insert(0) == 0xFE && route_src_insert(1) == 0xFD);
  assert(route_src_insert(route_insert_slot_max - 1) == 0xFB);
  assert(route_src_valid(0) && route_src_valid(1) && route_src_valid(route_src_generator));
  assert(route_src_valid(route_src_insert(3)));
  assert(!route_src_valid(2) && !route_src_valid(0xFA));
  assert(route_src_insert_slot(route_src_insert(2)) == 2);

  // A 0x18 frame carries the generator and insert pseudo-sources; a legacy
  // frame (src 0/1) still decodes, and an undefined src byte is refused.
  {
    Frame f{};
    Bytes wire{};
    Frame decoded{};
    std::uint8_t src = 9, dst = 9, flags = 0;
    assert(pack_midi_route(f, 50, 0x12345678, route_src_generator, 1, ROUTE_FLAG_ON));
    assert(encode(f, wire) && decode(wire, decoded));
    assert(unpack_midi_route(decoded, src, dst, flags));
    assert(src == route_src_generator && dst == 1 && flags == ROUTE_FLAG_ON);
    assert(pack_midi_route(f, 51, 0x12345678, route_src_insert(2), 0, 0));
    assert(encode(f, wire) && decode(wire, decoded));
    assert(unpack_midi_route(decoded, src, dst, flags));
    assert(src == route_src_insert(2) && dst == 0 && flags == 0);
    assert(!pack_midi_route(f, 52, 0x12345678, 2, 0, 0));
    assert(!pack_midi_route(f, 52, 0x12345678, 0xFA, 0, 0));

    const std::int16_t lfo[4] = {1, 0, 2, 64};
    assert(pack_midi_route_pipe(f, 53, 0x12345678, route_src_generator, 1, 0, 14,
                                false, 0, lfo, 4));
    assert(encode(f, wire) && decode(wire, decoded));
    std::uint8_t index = 0, type = 0, offset = 0;
    bool bypass = true;
    std::uint16_t ch_mask = 0;
    std::int16_t out[route_pipe_param_frame_max_legacy]{};
    std::size_t count = 0;
    assert(unpack_midi_route_pipe(decoded, src, dst, index, type, bypass, offset,
                                  ch_mask, out, route_pipe_param_frame_max_legacy,
                                  count));
    assert(src == route_src_generator && dst == 1 && type == 14 && count == 4);

    // A pre-pseudo 0x18 frame (src byte 1) has the same meaning as today.
    Frame legacy{};
    legacy.type = Type::control_event;
    legacy.sequence = 54;
    legacy.session = 0x12345678;
    legacy.length = 4;
    legacy.payload[0] = EV_MIDI_ROUTE;
    legacy.payload[1] = 1;
    legacy.payload[2] = 0;
    legacy.payload[3] = ROUTE_FLAG_ON;
    assert(encode(legacy, wire) && decode(wire, decoded));
    assert(unpack_midi_route(decoded, src, dst, flags));
    assert(src == 1 && dst == 0 && flags == ROUTE_FLAG_ON);
  }

  // Generator route: the LFO head produces CC on the target with no input
  // message; a modifier cannot be configured as the head.
  {
    RouteChains chains;
    assert(chains.begin(route_src_generator, 1, true));
    const std::int32_t lfo[5] = {1, 0, 2, 64, 64};  // CC1, 1/16 grid
    const std::int32_t arp[1] = {0};
    assert(!chains.setPipeChunk(route_src_generator, 1, 0, 12, false, 0, arp, 1));
    assert(chains.setPipeChunk(route_src_generator, 1, 0, 14, false, 0, lfo, 5));
    std::vector<std::pair<std::uint8_t, PipeOutput>> emitted;
    auto emit = [&](std::uint8_t dst, const PipeOutput& out) {
      emitted.emplace_back(dst, out);
    };
    chains.service(0, 1000, emit);       // seeds the grid; no step yet
    assert(emitted.empty());
    chains.service(6, 1000, emit);       // first 1/16 step
    assert(emitted.size() == 1);
    assert(emitted[0].first == 1 && emitted[0].second.count == 1);
    assert(emitted[0].second.events[0].data[0] == 0xB0);
    assert(emitted[0].second.events[0].data[1] == 1);
  }

  // Output inserts: every routed message passes insert 0 then 1, in route
  // order. CC1 -> PC5 at slot 0, PC5 -> CC7 at slot 1 proves the order (the
  // reverse order would leave a program change).
  {
    RouteChains chains;
    assert(chains.begin(route_src_insert(0), 1, true));
    assert(chains.begin(route_src_insert(1), 1, true));
    assert(chains.begin(0, 1, true));
    const std::int32_t cc_to_pc[5] = {1, 0, 127, 2, 5};
    const std::int32_t pc_to_cc[5] = {2, 0, 127, 1, 7};
    assert(chains.setPipeChunk(route_src_insert(0), 1, 0, 11, false, 0,
                               cc_to_pc, 5));
    assert(chains.setPipeChunk(route_src_insert(1), 1, 0, 11, false, 0,
                               pc_to_cc, 5));
    const PipeOutput cc = chains.processRoute(
        0, 1, pipeEvent(test_message(0xB0, 1, 99)), 1000);
    assert(cc.count == 1 && cc.events[0].data[0] == 0xB0 &&
           cc.events[0].data[1] == 7);
    const PipeOutput note = chains.processRoute(
        0, 1, pipeEvent(test_message(0x90, 60, 100)), 1000);
    assert(note.count == 1 && note.events[0].data[0] == 0x90 &&
           note.events[0].data[1] == 60);
  }

  // A timing insert (MidiDelay) shifts the routed stream: the dry note and CC
  // pass, and both echoes arrive at the same due time.
  {
    RouteChains chains;
    assert(chains.begin(route_src_insert(0), 0, true));
    assert(chains.begin(0, 0, true));
    const std::int32_t delay[3] = {10, 1, 0};
    assert(chains.setPipeChunk(route_src_insert(0), 0, 0, 16, false, 0, delay, 3));
    std::vector<PipeOutput> emitted;
    auto emit = [&](std::uint8_t, const PipeOutput& out) { emitted.push_back(out); };
    assert(chains.processRoute(0, 0, pipeEvent(test_message(0x90, 60, 100)), 1000)
               .count == 1);
    assert(chains.processRoute(0, 0, pipeEvent(test_message(0xB0, 1, 50)), 1000)
               .count == 1);
    chains.service(0, 1009, emit);
    assert(emitted.empty());
    chains.service(0, 1010, emit);
    assert(emitted.size() == 1 && emitted[0].count == 2);
    assert(emitted[0].events[0].data[0] == 0x90);
    assert(emitted[0].events[1].data[0] == 0xB0);
  }

  // Generator output also passes the output inserts before the wire.
  {
    RouteChains chains;
    assert(chains.begin(route_src_generator, 1, true));
    assert(chains.begin(route_src_insert(0), 1, true));
    const std::int32_t lfo[5] = {1, 0, 2, 64, 64};
    const std::int32_t cc_to_pc[5] = {1, 0, 127, 2, 5};
    assert(chains.setPipeChunk(route_src_generator, 1, 0, 14, false, 0, lfo, 5));
    assert(chains.setPipeChunk(route_src_insert(0), 1, 0, 11, false, 0,
                               cc_to_pc, 5));
    std::vector<PipeOutput> emitted;
    auto emit = [&](std::uint8_t, const PipeOutput& out) { emitted.push_back(out); };
    chains.service(0, 1000, emit);
    chains.service(6, 1000, emit);
    assert(emitted.size() == 1 && emitted[0].count == 1);
    assert(emitted[0].events[0].data[0] == 0xC0);
    assert(emitted[0].events[0].data[1] == 5);
  }

  std::printf("PASS: generator and output-insert route kinds (0x18/0x19 "
              "pseudo-sources, generator head, insert order, timing shift)\n");
}

void run_clock_extras_tests() {
  using namespace mb;

  // Divider pattern: every tick at 1, every second at 2, every fourth at 4.
  assert(clock_div_due(0, 1) && clock_div_due(1, 1) && clock_div_due(2, 1));
  for (std::uint32_t tick = 0; tick < 8; ++tick) {
    assert(clock_div_due(tick, 2) == (tick % 2 == 0));
    assert(clock_div_due(tick, 4) == (tick % 4 == 0));
  }
  assert(clock_div_due(1, 3));  // invalid value never silences clock

  // Gate defaults, divider/delay clamping, and per-output isolation.
  ClockGates gates;
  assert(gates.tempoDiv(0) == 1 && gates.tempoDiv(1) == 1);
  assert(gates.syncDelayMs(0) == 0 && gates.syncDelayMs(1) == 0);
  assert(gates.applyExtras(0, 2, 600));
  assert(gates.tempoDiv(0) == 2 && gates.syncDelayMs(0) == 500);
  assert(gates.tempoDiv(1) == 1 && gates.syncDelayMs(1) == 0);
  assert(gates.applyExtras(0, 3, 12));
  assert(gates.tempoDiv(0) == 1 && gates.syncDelayMs(0) == 12);
  assert(gates.applyExtras(1, 4, 0));
  assert(gates.tempoDiv(1) == 4);
  assert(!gates.applyExtras(2, 1, 0));
  assert(gates.tempoDiv(0) == 1 && gates.tempoDiv(1) == 4);

  // Delay line: exact 500 ms boundary, FIFO order, clamp and overflow drop.
  {
    ClockDelayLine line;
    line.setDelayMs(500);
    assert(line.delayMs() == 500 && line.pending() == 0);
    assert(line.push(1000));
    assert(!line.popDue(500999));
    assert(line.popDue(501000));
    assert(!line.popDue(501001));
    assert(line.push(0));
    assert(line.push(1000));
    assert(!line.popDue(499999));
    assert(line.popDue(500000));
    assert(line.pending() == 1);
    assert(!line.popDue(500999));
    assert(line.popDue(501000));
    line.setDelayMs(600);  // clamp
    assert(line.delayMs() == 500);
    line.setDelayMs(0);
    assert(line.delayMs() == 0);
    // A config change starts a fresh line (no stale delayed ticks).
    line.setDelayMs(500);
    assert(line.push(10));
    assert(line.pending() == 1);
    line.setDelayMs(5);
    assert(line.pending() == 0 && line.delayMs() == 5);
    assert(line.push(1234));
    assert(!line.popDue(6233));
    assert(line.popDue(6234));
    line.clear();
    for (std::uint32_t i = 0; i < kClockDelayDepth; ++i) assert(line.push(0));
    assert(!line.push(0));
    assert(line.drops() == 1 && line.pending() == kClockDelayDepth);
    line.clear();
    assert(line.pending() == 0);
    // 32-bit timer wraparound stays ordered: due = 0xFFFFF000 + 500 ms.
    line.setDelayMs(500);
    assert(line.push(0xFFFFF000u));
    assert(!line.popDue(0x00000000u));
    assert(line.popDue(0x00079120u));
  }

  std::printf("PASS: clock extras (divider, 500 ms clamp, delay line, wraparound)\n");
}

void run_clock_follow_tests() {
  using namespace mb;
  ClockFollow f;
  assert(f.source() == CLOCK_SOURCE_INTERNAL && !f.following());
  assert(f.transportMask() == 0);
  assert(!f.followsTransport(0) && !f.followsTransport(1));
  assert(!f.hasEstimate());
  assert(f.bpmX100() == 0 && f.intervalUsRounded() == 0);
  assert(!f.onTick(500'000));  // internal generator: external ticks are ignored

  // EV validation: source 3+ and reserved/USB mask bits are rejected without
  // touching the current follow state.
  assert(!f.apply(CLOCK_SOURCE_TRS2 + 1, 0));
  assert(!f.apply(CLOCK_SOURCE_TRS1, CLOCK_FOLLOW_MASK | 0x04));
  assert(f.source() == CLOCK_SOURCE_INTERNAL && f.transportMask() == 0);
  assert(f.apply(CLOCK_SOURCE_TRS1, CLOCK_FOLLOW_TRS1 | CLOCK_FOLLOW_TRS2));
  assert(f.source() == CLOCK_SOURCE_TRS1 && f.following());
  assert(f.followsTransport(0) && f.followsTransport(1));
  assert(!f.followsTransport(2));  // USB follow is Pi-side only

  // First tick arms; the second seeds the interval estimate.
  assert(!f.onTick(1'000'000));
  assert(!f.hasEstimate());
  assert(f.onTick(1'000'000 + 20'833));
  assert(f.hasEstimate());
  assert(std::fabs(f.intervalUs() - 20'833.0) < 0.001);
  assert(f.bpmX100() == 12000);
  assert(f.intervalUsRounded() == 20'833);

  // 120 bpm synthetic train with deterministic zero-mean arrival jitter: the
  // EMA converges on the true interval and a jitter-free local period.
  std::uint32_t stamp = 1'000'000 + 20'833;
  const int jitter[7] = {400, -500, 0, 300, -200, 600, -600};
  for (int i = 0; i < 400; ++i) {
    stamp += static_cast<std::uint32_t>(20'833 + jitter[i % 7]);
    assert(f.onTick(stamp));
  }
  assert(std::fabs(f.intervalUs() - 20'833.0) < 100.0);
  assert(f.bpmX100() >= 11950 && f.bpmX100() <= 12050);
  assert(f.intervalUsRounded() >= 20'800 && f.intervalUsRounded() <= 20'870);

  // One 2.5x interval is an outlier: ignored for the estimate (rejection);
  // the next normal interval is accepted again.
  const double before_outlier = f.intervalUs();
  const std::uint64_t outliers_before = f.outliers();
  stamp += 2 * 20'833 + 10'000;
  assert(!f.onTick(stamp));
  assert(f.outliers() == outliers_before + 1);
  assert(f.intervalUs() == before_outlier);
  stamp += 20'833;
  assert(f.onTick(stamp));
  assert(std::fabs(f.intervalUs() - before_outlier) < 10.0);

  // Loss: no tick for >3 expected intervals coasts on the last estimate; the
  // first real interval after the gap re-seeds (resume adapt).
  const std::uint32_t last = stamp;
  assert(!f.coasting(last + 3 * 20'833));
  const std::uint32_t coast_after =
      last + static_cast<std::uint32_t>(3.0 * f.intervalUs()) + 1;
  assert(f.coasting(coast_after));
  const std::uint64_t losses_before = f.losses();
  const double before_loss = f.intervalUs();
  stamp = last + 4 * 20'833;
  assert(!f.onTick(stamp));  // loss gap, not a tick interval
  assert(f.losses() == losses_before + 1);
  assert(f.intervalUs() == before_loss);  // coast
  assert(f.onTick(stamp + 20'833));
  assert(std::fabs(f.intervalUs() - 20'833.0) < 1.0);
  stamp += 20'833;

  // A tempo change during the outage is followed (the post-loss interval seeds
  // instead of being rejected against the stale estimate).
  stamp += 4 * 20'833;
  assert(!f.onTick(stamp));  // another loss
  stamp += 50'000;           // 50 bpm, 2.4x the stale estimate
  assert(f.onTick(stamp));
  assert(std::fabs(f.intervalUs() - 50'000.0) < 0.001);
  assert(f.bpmX100() == 5000);

  // A source that resumes more than 3x slower is still acquired: the first
  // post-loss interval is the gap, the second is the new tick spacing.
  ClockFollow slow_resume;
  assert(slow_resume.apply(CLOCK_SOURCE_TRS1, 0));
  assert(!slow_resume.onTick(0));
  assert(slow_resume.onTick(20'833));  // 120 bpm before the outage
  std::uint32_t slow_stamp = 20'833 + 4 * 20'833;  // >3x gap => loss
  assert(!slow_resume.onTick(slow_stamp));
  slow_stamp += 83'333;  // 30 bpm, >3x slower than the stale estimate
  assert(slow_resume.onTick(slow_stamp));
  assert(std::fabs(slow_resume.intervalUs() - 83'333.0) < 0.001);
  assert(slow_resume.bpmX100() == 3000);

  // Out-of-window intervals keep being rejected for the estimate; only a
  // detected loss gap re-seeds (no silent re-acquisition).
  ClockFollow reject;
  assert(reject.apply(CLOCK_SOURCE_TRS1, 0));
  assert(!reject.onTick(0));
  assert(reject.onTick(50'000));
  assert(reject.bpmX100() == 5000);
  std::uint32_t t = 50'000;
  for (int i = 0; i < 4; ++i) {
    t += 125'000;  // 2.5x: outside the window every time
    assert(!reject.onTick(t));
  }
  assert(std::fabs(reject.intervalUs() - 50'000.0) < 0.001);
  assert(reject.outliers() == 4);

  // The generator period and announced tempo stay inside the firmware window.
  ClockFollow fast;
  assert(fast.apply(CLOCK_SOURCE_TRS1, 0));
  assert(!fast.onTick(0));
  assert(fast.onTick(500));  // 2000 bpm raw
  assert(fast.bpmX100() == 30000);
  assert(fast.intervalUsRounded() == 8333);
  ClockFollow slow;
  assert(slow.apply(CLOCK_SOURCE_TRS2, 0));
  assert(!slow.onTick(0));
  assert(slow.onTick(200'000));  // 12.5 bpm raw
  assert(slow.bpmX100() == 2000);
  assert(slow.intervalUsRounded() == 125000);

  // Interval measurement is 32-bit wraparound safe.
  ClockFollow wrap;
  assert(wrap.apply(CLOCK_SOURCE_TRS2, 0));
  assert(!wrap.onTick(0xFFFFF000u));
  assert(wrap.onTick(0xFFFFF000u + 20'833u));
  assert(std::fabs(wrap.intervalUs() - 20'833.0) < 0.001);

  // PLL: the two-argument tick measures phase against the pending local
  // deadline and pulls an 8 ms offset out through bounded 200 us nudges (the
  // review's free-running-EMA failure mode). The nudge is consumed once per
  // tick and never exceeds the slew limit; a zero error stops nudging.
  ClockFollow pll;
  assert(pll.apply(CLOCK_SOURCE_TRS1, 0));
  std::uint32_t pll_stamp = 1'000'000;
  std::uint32_t pll_pending = pll_stamp + 20'833 + 8'000;  // 8 ms behind
  std::uint32_t pll_period = 20'833;
  assert(!pll.onTick(pll_stamp, pll_pending));  // arm
  assert(pll.takePhaseNudgeUs() == 0);
  bool saw_max_slew = false;
  for (int i = 0; i < 120; ++i) {
    pll_stamp += 20'833;
    pll_pending += pll_period;
    pll.onTick(pll_stamp, pll_pending);
    const std::int32_t nudge = pll.takePhaseNudgeUs();
    assert(nudge >= -kClockFollowMaxSlewUs && nudge <= kClockFollowMaxSlewUs);
    if (nudge == -kClockFollowMaxSlewUs) saw_max_slew = true;
    assert(pll.takePhaseNudgeUs() == 0);  // consumed exactly once
    pll_pending += nudge;
    pll_period = pll.intervalUsRounded();
  }
  assert(saw_max_slew);
  const std::int32_t residual =
      static_cast<std::int32_t>(pll_stamp + pll_period - pll_pending);
  assert(std::fabs(static_cast<double>(residual)) < 500.0);

  // The integral trim is bounded to 0.1 % of the period, so the PLL can never
  // itself move tempo beyond the announced accuracy target.
  assert(pll.intervalUsRounded() >= 20'810 && pll.intervalUsRounded() <= 20'856);

  // A source change restarts acquisition (stamps from different inputs are
  // never mixed); changing only the mask keeps the estimate.
  assert(f.apply(CLOCK_SOURCE_TRS2, CLOCK_FOLLOW_TRS1));
  assert(!f.hasEstimate());
  assert(f.source() == CLOCK_SOURCE_TRS2);
  assert(f.followsTransport(0) && !f.followsTransport(1));

  // Transport-byte mapping: FA start, FB continue, FC stop, everything else
  // (including clock) is not a transport command.
  assert(transport_command_for_byte(0xFA) == 1);
  assert(transport_command_for_byte(0xFB) == 2);
  assert(transport_command_for_byte(0xFC) == 0);
  assert(transport_command_for_byte(0xF8) == -1);
  assert(transport_command_for_byte(0x90) == -1);

  // EV_CLOCK_SOURCE frame validation and round trip.
  Frame cf{};
  Bytes wire{};
  Frame decoded{};
  assert(pack_clock_source(cf, 20, 0x12345678, CLOCK_SOURCE_TRS2,
                           CLOCK_FOLLOW_TRS1 | CLOCK_FOLLOW_TRS2));
  assert(encode(cf, wire) && decode(wire, decoded));
  assert(decoded.payload[0] == EV_CLOCK_SOURCE && decoded.length == 3);
  assert(decoded.payload[1] == CLOCK_SOURCE_TRS2);
  assert(decoded.payload[2] == (CLOCK_FOLLOW_TRS1 | CLOCK_FOLLOW_TRS2));
  assert(!pack_clock_source(cf, 21, 0x12345678, 3, 0));
  assert(!pack_clock_source(cf, 22, 0x12345678, CLOCK_SOURCE_TRS1, 0x04));
  std::printf("PASS: external clock follow (jittered convergence, outlier, coast/resume, mask, EV)\n");
}

void run_clock_scheduler_tests() {
  using namespace mb;
  // Deadline grid: at 120.00 bpm the exact period is 20833.333... us. Every
  // tick k must land on epoch + floor(k * 250'000'000'000 / 12000) ns. A naive
  // uint32-microsecond period (20833 us, then re-arming from the callback)
  // falls behind by 0.333 us per tick; this asserts the exact rational grid
  // over a simulated 2.9-hour run (500,000 ticks).
  constexpr std::uint64_t kDen = 12000;
  constexpr std::uint64_t kTicks = 500'000;
  const std::uint64_t epoch_us = 1000;
  auto exact_ns = [](std::uint64_t k) {
    return (k / kDen) * kClockTickNumeratorNs +
           ((k % kDen) * kClockTickNumeratorNs) / kDen;
  };
  ClockScheduler s;
  s.reset(12000, epoch_us);
  assert(s.nextDeadlineNs() == epoch_us * 1000ull + exact_ns(1));
  for (std::uint64_t k = 1; k <= kTicks; ++k) {
    const std::uint64_t expected_ns = epoch_us * 1000ull + exact_ns(k);
    assert(s.nextDeadlineNs() == expected_ns);
    const std::uint64_t deadline_us = s.nextDeadlineUs();
    const std::uint64_t deadline_as_ns = deadline_us * 1000ull;
    assert(deadline_as_ns >= expected_ns);
    assert(deadline_as_ns - expected_ns < 1000ull);  // sub-microsecond
    if (k == 4) {
      // 4 * 20833.333... us = 83333.333... us: the grid is at 83334 us, not
      // the 83332 us a fixed 20833 us period would give.
      assert(deadline_us - epoch_us == 83'334ull);
    }
    s.advance();
  }
  assert(s.intervalUsRounded() == 20833);

  // A tempo change rebases the period at the pending deadline without moving
  // it: no re-phase, no dropped tick, and the new rational grid is exact.
  ClockScheduler c;
  c.reset(12000, 0);
  for (int i = 0; i < 10; ++i) c.advance();
  const std::uint64_t pending = c.nextDeadlineNs();
  c.setTempoX100(9000);
  assert(c.nextDeadlineNs() == pending);
  const std::uint64_t q = kClockTickNumeratorNs / 9000ull;
  const std::uint64_t r = kClockTickNumeratorNs % 9000ull;
  for (std::uint64_t k = 1; k <= 1000; ++k) {
    assert(c.advance() == pending + k * q + (k * r) / 9000ull);
  }

  // Follow-style integer period override stays on the absolute grid.
  ClockScheduler f;
  f.reset(12000, 0);
  f.advance();
  const std::uint64_t base = f.nextDeadlineNs();
  f.setPeriodNs(50'000'000);  // 50 ms period override (follow path)
  for (std::uint64_t k = 1; k <= 10; ++k) {
    assert(f.advance() == base + k * 50'000'000ull);
  }

  // External follow rebases the pending deadline (phase), not the grid anchor:
  // the nudge moves that one grid point and the exact rational period carries
  // on from the moved point with no dropped tick.
  ClockScheduler rebase_scheduler;
  rebase_scheduler.reset(12000, 0);
  rebase_scheduler.advance();
  const std::uint64_t rebase_pending = rebase_scheduler.nextDeadlineNs();
  assert(rebase_scheduler.rebasePendingNs(-400'000) == rebase_pending - 400'000ull);
  assert(rebase_scheduler.rebasePendingNs(1'000'000) == rebase_pending + 600'000ull);
  for (std::uint64_t k = 1; k <= 100; ++k) {
    assert(rebase_scheduler.advance() == exact_ns(2 + k) + 600'000ull);
  }

  // A stalled alarm skips whole missed ticks instead of bursting them; the
  // surviving deadline is still the same absolute grid point.
  ClockScheduler m;
  m.reset(12000, 0);
  const std::uint64_t first_us = m.nextDeadlineUs();
  assert(m.skipToAfter(first_us + 2 * first_us + 1) == 3);
  assert(m.nextDeadlineNs() == exact_ns(4));
  assert(m.skipToAfter(first_us) == 0);

  std::printf("PASS: deadline clock scheduler (500k ticks, exact rational grid, tempo rebase, skip)\n");
}

void run_midi_tx_scheduler_tests() {
  using namespace mb;

  // Priority: a clock byte enqueued behind ten complete note messages is the
  // next byte on the wire; the note messages follow in their original order.
  MidiTxScheduler tx;
  std::uint8_t notes[10][3]{};
  for (std::uint8_t i = 0; i < 10; ++i) {
    notes[i][0] = 0x90;
    notes[i][1] = static_cast<std::uint8_t>(0x30 + i);
    notes[i][2] = 0x64;
    assert(tx.enqueue(notes[i], 3));
  }
  assert(tx.enqueueRealtime(0xF8));
  assert(tx.nextByte() == 0xF8);
  std::vector<std::uint8_t> out;
  for (int b = tx.nextByte(); b >= 0; b = tx.nextByte())
    out.push_back(static_cast<std::uint8_t>(b));
  assert(out.size() == 30);
  for (std::size_t i = 0; i < 10; ++i) {
    for (std::size_t j = 0; j < 3; ++j) assert(out[i * 3 + j] == notes[i][j]);
  }
  assert(!tx.pending());

  // Realtime interleaves between the bytes of a longer message (legal MIDI),
  // including a transport byte; framing of the interrupted message survives.
  MidiTxScheduler mid;
  const std::uint8_t cc[3] = {0xB0, 7, 100};
  assert(mid.enqueue(cc, 3));
  assert(mid.nextByte() == 0xB0);
  assert(mid.enqueueRealtime(0xF8));
  assert(mid.nextByte() == 0xF8);
  assert(mid.nextByte() == 7);
  assert(mid.enqueueRealtime(0xFA));
  assert(mid.nextByte() == 0xFA);
  assert(mid.nextByte() == 100);
  assert(mid.nextByte() == -1);

  // Overflow: a full queue drops the OLDEST non-Note-Off message as a whole.
  // A queued Note Off (including note-on velocity 0) is never the victim and
  // no message is ever cut in half.
  MidiTxScheduler ov;
  for (std::uint32_t i = 0; i < MidiTxScheduler::kNormalDepth; ++i) {
    std::uint8_t msg[3] = {0x90, static_cast<std::uint8_t>(i), 0x64};
    if (i == 3) {  // velocity-0 note-on: protected like a Note Off
      msg[0] = 0x90;
      msg[2] = 0x00;
    } else if (i == 5) {  // explicit Note Off: protected too
      msg[0] = 0x80;
      msg[2] = 0x40;
    }
    assert(ov.enqueue(msg, 3));
  }
  assert(ov.maxNormalDepth() == MidiTxScheduler::kNormalDepth);
  std::uint8_t newcomer[3] = {0x90, 0x70, 0x7F};
  assert(ov.enqueue(newcomer, 3));  // drops message 0 (oldest safe)
  assert(ov.droppedNormal() == 1 && ov.rejectedNormal() == 0);
  assert(ov.normalDepth() == MidiTxScheduler::kNormalDepth);
  out.clear();
  for (int b = ov.nextByte(); b >= 0; b = ov.nextByte())
    out.push_back(static_cast<std::uint8_t>(b));
  assert(out.size() == 3 * MidiTxScheduler::kNormalDepth);  // whole frames only
  bool saw_note_off = false, saw_velocity_zero_off = false;
  std::vector<std::uint8_t> first_notes;
  for (std::size_t i = 0; i < out.size(); i += 3) {
    assert(out[i] == 0x80 || out[i] == 0x90);  // no orphaned data bytes
    if (out[i] == 0x80) saw_note_off = true;
    if (out[i] == 0x90 && out[i + 2] == 0x00) saw_velocity_zero_off = true;
    first_notes.push_back(out[i + 1]);
  }
  assert(saw_note_off && saw_velocity_zero_off);
  assert(first_notes.front() == 1);  // message 0 was dropped, 1 shifted up
  assert(out[2 * 3] == 0x90 && out[2 * 3 + 2] == 0x00);  // velocity-0 Note Off kept
  assert(out[4 * 3] == 0x80);  // explicit Note Off kept

  // An in-flight message is never dropped (its first byte is already on the
  // wire); overflow drops a not-yet-started message instead.
  MidiTxScheduler ip;
  std::uint8_t inflight[3] = {0x90, 0x11, 0x22};
  assert(ip.enqueue(inflight, 3));
  assert(ip.nextByte() == 0x90);
  for (std::uint32_t i = 1; i < MidiTxScheduler::kNormalDepth; ++i) {
    std::uint8_t msg[3] = {0x90, static_cast<std::uint8_t>(i), 0x33};
    assert(ip.enqueue(msg, 3));
  }
  assert(ip.enqueue(newcomer, 3));
  out.clear();
  for (int b = ip.nextByte(); b >= 0; b = ip.nextByte())
    out.push_back(static_cast<std::uint8_t>(b));
  assert(out.size() == 2 + 3 * (MidiTxScheduler::kNormalDepth - 1));
  assert(out[0] == 0x11 && out[1] == 0x22);  // in-flight message completed

  // If the queue holds only Note Offs, the NEW message is refused: a queued
  // Note Off is never sacrificed, so nothing can hang a note.
  MidiTxScheduler offs;
  for (std::uint32_t i = 0; i < MidiTxScheduler::kNormalDepth; ++i) {
    std::uint8_t msg[3] = {0x80, static_cast<std::uint8_t>(i), 0x00};
    assert(offs.enqueue(msg, 3));
  }
  assert(!offs.enqueue(newcomer, 3));
  assert(offs.rejectedNormal() == 1 && offs.droppedNormal() == 0);
  std::uint32_t off_count = 0;
  for (int b = offs.nextByte(); b >= 0; b = offs.nextByte()) {
    assert((b & 0xF0) == 0x80 || b < 0x80);
    if ((b & 0xF0) == 0x80) ++off_count;
  }
  assert(off_count == MidiTxScheduler::kNormalDepth);

  // Realtime overflow drops the new byte, never a queued clock byte.
  MidiTxScheduler rt;
  for (std::uint32_t i = 0; i < MidiTxScheduler::kRealtimeDepth; ++i)
    assert(rt.enqueueRealtime(0xF8));
  assert(!rt.enqueueRealtime(0xFC));
  assert(rt.droppedRealtime() == 1);
  assert(rt.realtimeDepth() == MidiTxScheduler::kRealtimeDepth);
  for (std::uint32_t i = 0; i < MidiTxScheduler::kRealtimeDepth; ++i)
    assert(rt.nextByte() == 0xF8);

  std::printf("PASS: MIDI TX scheduler (clock priority, realtime interleave, whole-message overflow, Note Off kept)\n");
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
  run_value_encoder_tests();
  run_wire_config_tests();
  run_route_chain_tests();
  run_clock_extras_tests();
  run_clock_follow_tests();
  run_clock_scheduler_tests();
  run_midi_tx_scheduler_tests();
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
  // EV_CLOCK_ROUTE: per-output gates default on, bit0/bit1 apply, and a bad
  // port or reserved flag bit is rejected without changing any gate.
  assert(EV_CLOCK_ROUTE == 0x14);
  assert(CLOCK_ROUTE_CLOCK == 0x01 && CLOCK_ROUTE_TRANSPORT == 0x02);
  assert(CLOCK_ROUTE_DEFAULT == (CLOCK_ROUTE_CLOCK | CLOCK_ROUTE_TRANSPORT));
  ClockGates gates;
  assert(gates.clockOn(0) && gates.transportOn(0));
  assert(gates.clockOn(1) && gates.transportOn(1));  // legacy always-on default
  assert(gates.apply(0, CLOCK_ROUTE_TRANSPORT));
  assert(!gates.clockOn(0) && gates.transportOn(0));
  assert(gates.clockOn(1) && gates.transportOn(1));  // other output untouched
  assert(gates.apply(1, 0));
  assert(!gates.clockOn(1) && !gates.transportOn(1));
  assert(gates.apply(0, CLOCK_ROUTE_DEFAULT));
  assert(gates.clockOn(0) && gates.transportOn(0));
  assert(!gates.apply(2, CLOCK_ROUTE_DEFAULT));  // only OUT1/OUT2 exist
  assert(gates.clockOn(0) && gates.transportOn(0));  // rejected: unchanged
  assert(!gates.apply(0, 0x04));  // reserved flag bit
  assert(!gates.apply(0, 0x80));
  assert(gates.clockOn(0) && gates.transportOn(0));
  Frame cr{};
  assert(pack_clock_route(cr, 17, 0x12345678, 1, CLOCK_ROUTE_TRANSPORT));
  assert(encode(cr, wire) && decode(wire, decoded));
  assert(decoded.payload[0] == EV_CLOCK_ROUTE && decoded.length == 3);
  assert(decoded.payload[1] == 1 && decoded.payload[2] == CLOCK_ROUTE_TRANSPORT);
  assert(!pack_clock_route(cr, 18, 0x12345678, 2, CLOCK_ROUTE_DEFAULT));
  assert(!pack_clock_route(cr, 19, 0x12345678, 0, 0x04));
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

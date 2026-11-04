#include "protocol.hpp"
#include "clock_follow.hpp"
#include "clock_output.hpp"
#include "midi.hpp"
#include "midi_pipe.hpp"
#include "panel_input.hpp"
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

mb::MidiMessage test_message(std::uint8_t status, std::uint8_t data1 = 0,
                             std::uint8_t data2 = 0, std::uint8_t size = 3) {
  mb::MidiMessage m{};
  m.size = size;
  m.data[0] = status;
  m.data[1] = data1;
  m.data[2] = data2;
  return m;
}

mb::Pipe test_pipe(mb::MidiPipeType type, std::initializer_list<int> params,
                   bool bypass = false) {
  mb::Pipe p;
  p.type = type;
  p.bypass = bypass;
  p.param_count = static_cast<std::uint8_t>(params.size());
  std::size_t i = 0;
  for (int value : params) {
    p.params[i++] = value;
  }
  return p;
}

mb::MidiPipeChain test_chain(mb::MidiPipeType type,
                             std::initializer_list<int> params,
                             bool bypass = false) {
  mb::MidiPipeChain chain;
  chain.begin(true);
  chain.setSeed(0x12345678u);
  assert(chain.append(test_pipe(type, params, bypass)));
  return chain;
}

bool output_has(const mb::PipeOutput& out, std::uint8_t status, std::uint8_t data1,
                std::uint8_t data2, std::uint8_t size = 3) {
  for (std::size_t i = 0; i < out.count; ++i) {
    if (out.events[i].size != size) continue;
    if (out.events[i].data[0] != status || out.events[i].data[1] != data1) continue;
    if (size == 1 || out.events[i].data[2] == data2) return true;
  }
  return false;
}

void run_pipe_tests() {
  using namespace mb;

  // Filter: every one of the eight mask positions independently. A zero mask
  // is drop for that kind; missing params default to pass.
  const MidiMessage filter_inputs[8] = {
      test_message(0x90, 60, 100),  // note on
      test_message(0x80, 60, 0),    // note off
      test_message(0xB0, 7, 100),   // cc
      test_message(0xC0, 12, 0, 2), // pc
      test_message(0xD0, 90, 0, 2), // channel pressure
      test_message(0xE0, 0, 64),    // bend
      test_message(0xF8, 0, 0, 1),  // realtime
      test_message(0xF0, 0, 0, 1),  // sysex
  };
  for (std::size_t pass = 0; pass < 8; ++pass) {
    std::array<int, 8> mask{};
    mask[pass] = 1;
    Pipe filter;
    filter.type = MidiPipeType::Filter;
    filter.param_count = 8;
    for (std::size_t i = 0; i < mask.size(); ++i) filter.params[i] = mask[i];
    MidiPipeChain chain;
    chain.begin(true);
    chain.setSeed(1);
    assert(chain.append(filter));
    for (std::size_t kind = 0; kind < 8; ++kind) {
      const PipeOutput out = chain.process(filter_inputs[kind], 0);
      assert(out.count == (kind == pass ? 1u : 0u));
    }
  }
  {
    // All params missing -> every kind passes (no silent mute).
    MidiPipeChain chain = test_chain(MidiPipeType::Filter, {});
    for (const auto& m : filter_inputs) assert(chain.process(m, 0).count == 1);
  }
  {
    // A system-common message outside the mask registry passes.
    MidiPipeChain chain = test_chain(MidiPipeType::Filter, {0, 0, 0, 0, 0, 0, 0, 0});
    const PipeOutput out = chain.process(test_message(0xF1, 0, 0, 2), 0);
    assert(out.count == 1 && out.events[0].size == 2);
  }

  // NoteRange: inclusive note edges and an inclusive note-on velocity window.
  // Release velocity is deliberately not filtered (a high floor must not
  // strand a held note).
  {
    MidiPipeChain chain = test_chain(MidiPipeType::NoteRange, {60, 64, 64, 127});
    assert(chain.process(test_message(0x90, 59, 100), 0).count == 0);
    assert(chain.process(test_message(0x90, 60, 64), 0).count == 1);
    assert(chain.process(test_message(0x90, 60, 63), 0).count == 0);
    assert(chain.process(test_message(0x90, 64, 127), 0).count == 1);
    assert(chain.process(test_message(0x90, 65, 100), 0).count == 0);
    assert(chain.process(test_message(0x80, 60, 0), 0).count == 1);
    assert(chain.process(test_message(0xB0, 60, 100), 0).count == 1);
    // Reversed lo/hi is normalised instead of dropping everything.
    MidiPipeChain reversed = test_chain(MidiPipeType::NoteRange, {64, 60, 0, 127});
    assert(reversed.process(test_message(0x90, 62, 100), 0).count == 1);
  }

  // CcRemap: endpoints, a middle value, and out-of-range pass-through.
  {
    MidiPipeChain chain = test_chain(MidiPipeType::CcRemap, {10, 20, 100, 110});
    assert(output_has(chain.process(test_message(0xB0, 10, 1), 0), 0xB0, 100, 1));
    assert(output_has(chain.process(test_message(0xB0, 15, 1), 0), 0xB0, 105, 1));
    assert(output_has(chain.process(test_message(0xB0, 20, 1), 0), 0xB0, 110, 1));
    assert(output_has(chain.process(test_message(0xB0, 9, 1), 0), 0xB0, 9, 1));
    assert(output_has(chain.process(test_message(0xB0, 21, 1), 0), 0xB0, 21, 1));
    // Reverse output range and a one-to-one input range.
    MidiPipeChain reverse = test_chain(MidiPipeType::CcRemap, {0, 2, 127, 0});
    assert(output_has(reverse.process(test_message(0xB0, 0, 1), 0), 0xB0, 127, 1));
    assert(output_has(reverse.process(test_message(0xB0, 2, 1), 0), 0xB0, 0, 1));
    MidiPipeChain single = test_chain(MidiPipeType::CcRemap, {7, 7, 70, 70});
    assert(output_has(single.process(test_message(0xB0, 7, 1), 0), 0xB0, 70, 1));
  }

  // ChannelRemap: 0 = any input channel, output channel 1..16.
  {
    MidiPipeChain any = test_chain(MidiPipeType::ChannelRemap, {0, 5});
    const PipeOutput out = any.process(test_message(0x93, 60, 100), 0);
    assert(out.count == 1 && out.events[0].data[0] == 0x94);  // channel 5
    MidiPipeChain specific = test_chain(MidiPipeType::ChannelRemap, {2, 16});
    const PipeOutput nonmatch = specific.process(test_message(0x90, 60, 100), 0);
    assert(nonmatch.count == 1 && nonmatch.events[0].data[0] == 0x90);
    const PipeOutput moved = specific.process(test_message(0x91, 60, 100), 0);
    assert(moved.count == 1 && moved.events[0].data[0] == 0x9F);
    const PipeOutput non_channel = specific.process(test_message(0xF8, 0, 0, 1), 0);
    assert(non_channel.count == 1 && non_channel.events[0].data[0] == 0xF8);
  }

  // ScaleQuantize: six scales, root transposition and the absolute user mask.
  {
    // C major: 61 -> 60 (tie resolves down), 63 -> 62 (nearest down).
    MidiPipeChain major = test_chain(MidiPipeType::ScaleQuantize, {1, 0, 0});
    assert(output_has(major.process(test_message(0x90, 61, 100), 0), 0x90, 60, 100));
    assert(output_has(major.process(test_message(0x90, 63, 100), 0), 0x90, 62, 100));
    assert(output_has(major.process(test_message(0x90, 60, 100), 0), 0x90, 60, 100));
    // D minor (root 2): C#3 (61) -> C3 (60), F#3 (66) -> F3 (65).
    MidiPipeChain minor = test_chain(MidiPipeType::ScaleQuantize, {2, 2, 0});
    assert(output_has(minor.process(test_message(0x90, 61, 100), 0), 0x90, 60, 100));
    assert(output_has(minor.process(test_message(0x90, 66, 100), 0), 0x90, 65, 100));
    // Major pentatonic on C leaves E and drops nothing; F# snaps to G.
    MidiPipeChain penta_major =
        test_chain(MidiPipeType::ScaleQuantize, {3, 0, 0});
    assert(output_has(penta_major.process(test_message(0x90, 64, 100), 0), 0x90, 64, 100));
    assert(output_has(penta_major.process(test_message(0x90, 66, 100), 0), 0x90, 67, 100));
    // Minor pentatonic on C: Eb stays, E snaps down to Eb.
    MidiPipeChain penta_minor =
        test_chain(MidiPipeType::ScaleQuantize, {4, 0, 0});
    assert(output_has(penta_minor.process(test_message(0x90, 63, 100), 0), 0x90, 63, 100));
    assert(output_has(penta_minor.process(test_message(0x90, 64, 100), 0), 0x90, 63, 100));
    // Dorian on C: Eb and Bb are in, E and B snap down.
    MidiPipeChain dorian = test_chain(MidiPipeType::ScaleQuantize, {5, 0, 0});
    assert(output_has(dorian.process(test_message(0x90, 63, 100), 0), 0x90, 63, 100));
    assert(output_has(dorian.process(test_message(0x90, 64, 100), 0), 0x90, 63, 100));
    assert(output_has(dorian.process(test_message(0x90, 71, 100), 0), 0x90, 70, 100));
    // User mask 0xAAA = pitch classes 1,3,5,7,9,11; C snaps down to B.
    MidiPipeChain user = test_chain(MidiPipeType::ScaleQuantize, {6, 0, 0xAAA});
    assert(output_has(user.process(test_message(0x90, 61, 100), 0), 0x90, 61, 100));
    assert(output_has(user.process(test_message(0x90, 60, 100), 0), 0x90, 59, 100));
    // mask12 boundary: 0 = no allowed class (pass unchanged), 0xFFF = all pass.
    MidiPipeChain empty_mask = test_chain(MidiPipeType::ScaleQuantize, {6, 0, 0});
    assert(output_has(empty_mask.process(test_message(0x90, 60, 100), 0), 0x90, 60, 100));
    MidiPipeChain full_mask =
        test_chain(MidiPipeType::ScaleQuantize, {6, 0, 0xFFF});
    assert(output_has(full_mask.process(test_message(0x90, 61, 100), 0), 0x90, 61, 100));
    // Low/high MIDI note edges stay in range after octave wrapping.
    assert(output_has(major.process(test_message(0x90, 0, 100), 0), 0x90, 0, 100));
    assert(output_has(major.process(test_message(0x90, 127, 100), 0), 0x90, 127, 100));
    // Scale 0 (and an out-of-range scale id) is off and passes untouched.
    MidiPipeChain off = test_chain(MidiPipeType::ScaleQuantize, {0, 0, 0});
    assert(output_has(off.process(test_message(0x90, 61, 100), 0), 0x90, 61, 100));
    MidiPipeChain bad_scale = test_chain(MidiPipeType::ScaleQuantize, {99, 0, 0});
    assert(output_has(bad_scale.process(test_message(0x90, 61, 100), 0), 0x90, 61, 100));
  }

  // Chance: 0 % passes, 100 % drops note-ons only; note-offs keep flowing so
  // a dropped note cannot stick.
  {
    MidiPipeChain never = test_chain(MidiPipeType::Chance, {0});
    assert(never.process(test_message(0x90, 60, 100), 0).count == 1);
    MidiPipeChain always = test_chain(MidiPipeType::Chance, {100});
    assert(always.process(test_message(0x90, 60, 100), 0).count == 0);
    assert(always.process(test_message(0x80, 60, 0), 0).count == 1);
    assert(always.process(test_message(0xB0, 7, 100), 0).count == 1);
  }

  // VelocityRandom: amount bounds, the floor, and amount 0 still applies the
  // floor to note-on velocity; note-off release velocity is untouched.
  {
    MidiPipeChain humanize = test_chain(MidiPipeType::VelocityRandom, {5, 40});
    for (int i = 0; i < 200; ++i) {
      const PipeOutput out = humanize.process(test_message(0x90, 60, 64), 0);
      assert(out.count == 1 && out.events[0].data[2] >= 40);
      assert(out.events[0].data[2] <= 69);
    }
    MidiPipeChain floor_only = test_chain(MidiPipeType::VelocityRandom, {0, 100});
    assert(output_has(floor_only.process(test_message(0x90, 60, 20), 0), 0x90, 60, 100));
    assert(output_has(floor_only.process(test_message(0x90, 60, 127), 0), 0x90, 60, 127));
    assert(output_has(floor_only.process(test_message(0x80, 60, 5), 0), 0x80, 60, 5));
  }

  // Dispatcher: rotation across the configured window, wrapping past ch 16,
  // and note-off pairing back to the channel the note-on used.
  {
    MidiPipeChain dispatch = test_chain(MidiPipeType::Dispatcher, {4, 15});
    const std::uint8_t expected[6] = {15, 16, 1, 2, 15, 16};
    for (int i = 0; i < 6; ++i) {
      const PipeOutput on =
          dispatch.process(test_message(0x90, static_cast<std::uint8_t>(60 + i), 100), 0);
      assert(on.count == 1);
      assert((on.events[0].data[0] & 0x0F) == expected[i] - 1);
      const PipeOutput off =
          dispatch.process(test_message(0x80, static_cast<std::uint8_t>(60 + i), 0), 0);
      assert(off.count == 1);
      assert((off.events[0].data[0] & 0x0F) == expected[i] - 1);
    }
  }

  // Sustain: CC64 down latches note-offs, CC64 up releases them as note-offs;
  // after release a normal note-off passes again.
  {
    MidiPipeChain sustain = test_chain(MidiPipeType::Sustain, {1});
    assert(sustain.process(test_message(0xB0, 64, 127), 0).count == 1);
    assert(sustain.process(test_message(0x90, 60, 100), 0).count == 1);
    assert(sustain.process(test_message(0x80, 60, 0), 0).count == 0);
    assert(sustain.process(test_message(0x90, 64, 100), 0).count == 1);
    assert(sustain.process(test_message(0x80, 64, 0), 0).count == 0);
    const PipeOutput release = sustain.process(test_message(0xB0, 64, 0), 0);
    assert(release.count == 3);  // two note-offs, then CC64
    assert(output_has(release, 0x80, 60, 0) && output_has(release, 0x80, 64, 0));
    assert(output_has(release, 0xB0, 64, 0));
    assert(sustain.process(test_message(0x80, 60, 0), 0).count == 1);
    // latch 0 is a pass-through pipe.
    MidiPipeChain off = test_chain(MidiPipeType::Sustain, {0});
    assert(off.process(test_message(0xB0, 64, 127), 0).count == 1);
    assert(off.process(test_message(0x80, 60, 0), 0).count == 1);
  }

  // NoteLength: fixed length schedules one off and ignores an early note-off;
  // length 0 leaves the stream alone. The generated off re-enters the chain
  // after the pipe (a later ChannelRemap still sees it).
  {
    MidiPipeChain length = test_chain(MidiPipeType::NoteLength, {100});
    assert(length.process(test_message(0x90, 60, 100), 1000).count == 1);
    assert(length.process(test_message(0x80, 60, 0), 1050).count == 0);
    assert(length.tick(1099).count == 0);
    const PipeOutput due = length.tick(1100);
    assert(due.count == 1 && due.events[0].data[0] == 0x80 && due.events[0].data[1] == 60);
    assert(length.tick(1200).count == 0);
    MidiPipeChain staged;
    staged.begin(true);
    assert(staged.append(test_pipe(MidiPipeType::NoteLength, {50})));
    assert(staged.append(test_pipe(MidiPipeType::ChannelRemap, {0, 5})));
    assert(staged.process(test_message(0x90, 60, 100), 0).count == 1);
    const PipeOutput remapped = staged.tick(50);
    assert(remapped.count == 1 && remapped.events[0].data[0] == 0x84 &&
           remapped.events[0].data[1] == 60);
    MidiPipeChain off = test_chain(MidiPipeType::NoteLength, {0});
    assert(off.process(test_message(0x80, 60, 0), 1000).count == 1);
    assert(off.tick(2000).count == 0);
  }

  // Transform: kind conversion, match range, non-match pass-through, bend
  // 14-bit output and invalid out-kind safety.
  {
    MidiPipeChain cc_to_note = test_chain(MidiPipeType::Transform, {1, 7, 7, 0, 60});
    assert(output_has(cc_to_note.process(test_message(0xB0, 7, 100), 0), 0x90, 60, 100));
    assert(output_has(cc_to_note.process(test_message(0xB0, 8, 100), 0), 0xB0, 8, 100));
    MidiPipeChain note_to_cc = test_chain(MidiPipeType::Transform, {0, 60, 60, 1, 74});
    assert(output_has(note_to_cc.process(test_message(0x90, 60, 100), 0), 0xB0, 74, 100));
    assert(output_has(note_to_cc.process(test_message(0x90, 61, 100), 0), 0x90, 61, 100));
    MidiPipeChain to_bend = test_chain(MidiPipeType::Transform, {0, 60, 60, 3, 8192});
    const PipeOutput bend = to_bend.process(test_message(0x90, 60, 100), 0);
    assert(bend.count == 1 && bend.events[0].data[0] == 0xE0 &&
           bend.events[0].data[1] == 0 && bend.events[0].data[2] == 64);
    MidiPipeChain to_pc = test_chain(MidiPipeType::Transform, {1, 10, 20, 2, 42});
    const PipeOutput pc = to_pc.process(test_message(0xB0, 10, 9), 0);
    assert(pc.count == 1 && pc.events[0].size == 2 && pc.events[0].data[0] == 0xC0 &&
           pc.events[0].data[1] == 42);
    MidiPipeChain to_pressure =
        test_chain(MidiPipeType::Transform, {1, 10, 20, 4, 77});
    const PipeOutput pressure = to_pressure.process(test_message(0xB0, 10, 9), 0);
    assert(pressure.count == 1 && pressure.events[0].size == 2 &&
           pressure.events[0].data[0] == 0xD0 && pressure.events[0].data[1] == 77);
    MidiPipeChain partial = test_chain(MidiPipeType::Transform, {});
    assert(output_has(partial.process(test_message(0x90, 60, 100), 0), 0x90, 60, 100));
  }

  // Bypass is honoured for every Wave-1 type: the configured transform never
  // runs, and an empty chain/unknown Wave-2 id passes bytes untouched.
  for (std::uint8_t type = 1; type <= 11; ++type) {
    MidiPipeChain bypassed;
    bypassed.begin(true);
    bypassed.setSeed(7);
    Pipe p;
    p.type = static_cast<MidiPipeType>(type);
    p.bypass = true;
    p.param_count = 5;
    p.params[0] = 0;
    p.params[1] = 0;
    p.params[2] = 127;
    p.params[3] = 1;
    p.params[4] = 1;
    assert(bypassed.append(p));
    const PipeOutput out = bypassed.process(test_message(0x90, 60, 100), 0);
    assert(out.count == 1 && out.events[0].data[0] == 0x90 &&
           out.events[0].data[1] == 60 && out.events[0].data[2] == 100);
  }
  {
    // Chunked wire-style configuration: a Filter has 8 positional params, so
    // it arrives as a 5-param chunk followed by a 3-param chunk at offset 5.
    MidiPipeChain chunked;
    chunked.begin(true);
    const std::int32_t first[5] = {1, 0, 0, 0, 0};
    const std::int32_t rest[3] = {0, 0, 0};
    assert(chunked.setPipeChunk(0, 1, false, 0, first, 5));
    assert(chunked.setPipeChunk(0, 1, false, 5, rest, 3));
    assert(chunked.process(test_message(0x90, 60, 100), 0).count == 1);
    assert(chunked.process(test_message(0x80, 60, 0), 0).count == 0);
    assert(!chunked.setPipeChunk(1, 1, false, 5, rest, 3));  // no pipe 1 yet
    assert(!chunked.setPipeChunk(0, 2, false, 5, rest, 3));  // type mismatch
    assert(!chunked.setPipeChunk(8, 1, false, 0, first, 5));  // index out of range
  }
  {
    MidiPipeChain unknown;
    unknown.begin(true);
    Pipe p;
    p.type = static_cast<MidiPipeType>(12);
    p.param_count = 0;
    assert(unknown.append(p));
    const PipeOutput out = unknown.process(test_message(0x90, 60, 100), 0);
    assert(out.count == 1 && out.events[0].data[1] == 60);
    // Out-of-range/unknown params never crash a pipe access: the missing
    // inHi/outLo/outHi fall back to their documented defaults.
    MidiPipeChain sparse = test_chain(MidiPipeType::CcRemap, {3});
    assert(output_has(sparse.process(test_message(0xB0, 3, 1), 0), 0xB0, 0, 1));
  }

  // Wire config round trips: route begin, a chunked pipe, a route send and
  // the clock-extras frame.
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

    const std::int16_t params[5] = {60, 64, 64, 127, -7};
    assert(pack_midi_route_pipe(f, 32, 0x12345678, 0, 1, 2, 2, false, 0, params, 5));
    assert(encode(f, wire) && decode(wire, decoded));
    std::uint8_t index = 0, type = 0, offset = 0;
    bool bypass = true;
    std::int16_t out_params[5]{};
    std::size_t count = 0;
    assert(unpack_midi_route_pipe(decoded, src, dst, index, type, bypass, offset,
                                  out_params, 5, count));
    assert(src == 0 && dst == 1 && index == 2 && type == 2 && !bypass);
    assert(offset == 0 && count == 5 && out_params[0] == 60 && out_params[4] == -7);
    assert(!pack_midi_route_pipe(f, 33, 0x12345678, 0, 1, 0, 2, false, 6, params, 5));
    assert(!pack_midi_route_pipe(f, 33, 0x12345678, 0, 1, 0, 0, false, 0, params, 5));

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

  std::printf("PASS: Wave-1 MIDI pipes (filter/range/remap/scale/chance/velocity/"
              "dispatch/sustain/length/transform, bypass, wire config)\n");
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
  run_pipe_tests();
  run_clock_extras_tests();
  run_clock_follow_tests();
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

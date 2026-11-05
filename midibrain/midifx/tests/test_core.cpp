// Behavior suite for the shared MIDI FX pipe core (midifx/core/midifx.hpp).
//
// Moved here from firmware/pico/protocol_test.cpp when the firmware adopted
// the core, so the firmware and (later) the Pi run ONE behavior suite. The
// expectations are the firmware's - every Wave-1 pipe with boundary values,
// bypass, chunked configuration and the Wave-2 synthetic tick grids - plus the
// Pi engine's extra diagnostics (config validation, the dropped-event counter,
// seed determinism) and the schema-default checks that bind pipes.json to
// observed behavior. A failing check aborts through assert (the firmware suite
// style) or prints FAIL for the diagnostics tests.
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <vector>

#include "midifx/core/midifx.hpp"

namespace {

int g_failures = 0;

void check(bool ok, const char* what, const char* file, int line) {
  if (ok) return;
  std::fprintf(stderr, "FAIL %s (%s:%d)\n", what, file, line);
  ++g_failures;
}

#define CHECK(cond) check((cond), #cond, __FILE__, __LINE__)

void pass(const char* name) { std::printf("PASS %s\n", name); }

using mnky::midifx::PipeChain;
using mnky::midifx::PipeConfig;
using mnky::midifx::PipeEvent;
using mnky::midifx::PipeOutput;
using mnky::midifx::PipeType;

PipeEvent test_event(std::uint8_t status, std::uint8_t data1 = 0,
                             std::uint8_t data2 = 0, std::uint8_t size = 3) {
  PipeEvent m;
  m.size = size;
  m.data[0] = status;
  m.data[1] = data1;
  m.data[2] = data2;
  return m;
}

PipeConfig test_pipe(PipeType type, std::initializer_list<int> params,
                   bool bypass = false) {
  PipeConfig p;
  p.type = type;
  p.bypass = bypass;
  p.paramCount = static_cast<std::uint8_t>(params.size());
  std::size_t i = 0;
  for (int value : params) {
    p.params[i++] = value;
  }
  return p;
}

PipeChain test_chain(PipeType type,
                             std::initializer_list<int> params,
                             bool bypass = false) {
  PipeChain chain;
  chain.begin(true);
  chain.setSeed(0x12345678u);
  assert(chain.append(test_pipe(type, params, bypass)));
  return chain;
}

bool output_has(const PipeOutput& out, std::uint8_t status, std::uint8_t data1,
                std::uint8_t data2, std::uint8_t size = 3) {
  for (std::size_t i = 0; i < out.count; ++i) {
    if (out.events[i].size != size) continue;
    if (out.events[i].data[0] != status || out.events[i].data[1] != data1) continue;
    if (size == 1 || out.events[i].data[2] == data2) return true;
  }
  return false;
}

// --- Wave-2 clocked pipe helpers -------------------------------------------
// One wire event with the tick (or millisecond) the chain emitted it on.
struct StampedEvent {
  std::uint32_t at;
  std::uint8_t size;
  std::uint8_t data[3];
};

void collect(const PipeOutput& out, std::uint32_t at,
             std::vector<StampedEvent>& events) {
  for (std::size_t i = 0; i < out.count; ++i)
    events.push_back({at, out.events[i].size,
                      {out.events[i].data[0], out.events[i].data[1],
                       out.events[i].data[2]}});
}

// The firmware's main-loop order: millisecond deadlines first, then the clocked
// pipes, one call per tick. The 24-PPQN grid is the only clock the pipes use, so
// the tick index is the whole input a synthetic tick train needs.
std::vector<StampedEvent> run_tick_train(PipeChain& chain,
                                         std::uint32_t from_tick,
                                         std::uint32_t to_tick) {
  std::vector<StampedEvent> events;
  for (std::uint32_t tick = from_tick; tick <= to_tick; ++tick) {
    const std::uint32_t now_ms = tick * 21u;  // 120 bpm: 20.833 ms per tick
    collect(chain.tick(now_ms), tick, events);
    collect(chain.clockTick(tick, now_ms), tick, events);
  }
  return events;
}

// Millisecond-only train for MidiDelay, whose parameter is delayMs.
std::vector<StampedEvent> run_ms_train(PipeChain& chain,
                                       std::uint32_t from_ms,
                                       std::uint32_t to_ms) {
  std::vector<StampedEvent> events;
  for (std::uint32_t now = from_ms; now <= to_ms; ++now)
    collect(chain.tick(now), now, events);
  return events;
}

// Events of one status nibble (0x90 note-on, 0x80 note-off, 0xB0 CC), in order.
std::vector<StampedEvent> kind_events(const std::vector<StampedEvent>& events,
                                     std::uint8_t status) {
  std::vector<StampedEvent> filtered;
  for (const auto& event : events)
    if ((event.data[0] & 0xF0) == status) filtered.push_back(event);
  return filtered;
}

std::vector<int> notes_of(const std::vector<StampedEvent>& events) {
  std::vector<int> notes;
  for (const auto& event : events) notes.push_back(event.data[1]);
  return notes;
}

std::vector<int> values_of(const std::vector<StampedEvent>& events) {
  std::vector<int> values;
  for (const auto& event : events) values.push_back(event.data[2]);
  return values;
}

std::vector<std::uint32_t> times_of(const std::vector<StampedEvent>& events) {
  std::vector<std::uint32_t> times;
  for (const auto& event : events) times.push_back(event.at);
  return times;
}

void run_pipe_tests() {
  using namespace mnky::midifx;

  // Filter: every one of the eight mask positions independently. A zero mask
  // is drop for that kind; missing params default to pass.
  const PipeEvent filter_inputs[8] = {
      test_event(0x90, 60, 100),  // note on
      test_event(0x80, 60, 0),    // note off
      test_event(0xB0, 7, 100),   // cc
      test_event(0xC0, 12, 0, 2), // pc
      test_event(0xD0, 90, 0, 2), // channel pressure
      test_event(0xE0, 0, 64),    // bend
      test_event(0xF8, 0, 0, 1),  // realtime
      test_event(0xF0, 0, 0, 1),  // sysex
  };
  for (std::size_t pass = 0; pass < 8; ++pass) {
    std::array<int, 8> mask{};
    mask[pass] = 1;
    PipeConfig filter;
    filter.type = PipeType::Filter;
    filter.paramCount = 8;
    for (std::size_t i = 0; i < mask.size(); ++i) filter.params[i] = mask[i];
    PipeChain chain;
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
    PipeChain chain = test_chain(PipeType::Filter, {});
    for (const auto& m : filter_inputs) assert(chain.process(m, 0).count == 1);
  }
  {
    // A system-common message outside the mask registry passes.
    PipeChain chain = test_chain(PipeType::Filter, {0, 0, 0, 0, 0, 0, 0, 0});
    const PipeOutput out = chain.process(test_event(0xF1, 0, 0, 2), 0);
    assert(out.count == 1 && out.events[0].size == 2);
  }

  // NoteRange: inclusive note edges and an inclusive note-on velocity window.
  // Release velocity is deliberately not filtered (a high floor must not
  // strand a held note).
  {
    PipeChain chain = test_chain(PipeType::NoteRange, {60, 64, 64, 127});
    assert(chain.process(test_event(0x90, 59, 100), 0).count == 0);
    assert(chain.process(test_event(0x90, 60, 64), 0).count == 1);
    assert(chain.process(test_event(0x90, 60, 63), 0).count == 0);
    assert(chain.process(test_event(0x90, 64, 127), 0).count == 1);
    assert(chain.process(test_event(0x90, 65, 100), 0).count == 0);
    assert(chain.process(test_event(0x80, 60, 0), 0).count == 1);
    assert(chain.process(test_event(0xB0, 60, 100), 0).count == 1);
    // Reversed lo/hi is normalised instead of dropping everything.
    PipeChain reversed = test_chain(PipeType::NoteRange, {64, 60, 0, 127});
    assert(reversed.process(test_event(0x90, 62, 100), 0).count == 1);
  }

  // CcRemap: endpoints, a middle value, and out-of-range pass-through.
  {
    PipeChain chain = test_chain(PipeType::CcRemap, {10, 20, 100, 110});
    assert(output_has(chain.process(test_event(0xB0, 10, 1), 0), 0xB0, 100, 1));
    assert(output_has(chain.process(test_event(0xB0, 15, 1), 0), 0xB0, 105, 1));
    assert(output_has(chain.process(test_event(0xB0, 20, 1), 0), 0xB0, 110, 1));
    assert(output_has(chain.process(test_event(0xB0, 9, 1), 0), 0xB0, 9, 1));
    assert(output_has(chain.process(test_event(0xB0, 21, 1), 0), 0xB0, 21, 1));
    // Reverse output range and a one-to-one input range.
    PipeChain reverse = test_chain(PipeType::CcRemap, {0, 2, 127, 0});
    assert(output_has(reverse.process(test_event(0xB0, 0, 1), 0), 0xB0, 127, 1));
    assert(output_has(reverse.process(test_event(0xB0, 2, 1), 0), 0xB0, 0, 1));
    PipeChain single = test_chain(PipeType::CcRemap, {7, 7, 70, 70});
    assert(output_has(single.process(test_event(0xB0, 7, 1), 0), 0xB0, 70, 1));
  }

  // ChannelRemap: 0 = any input channel, output channel 1..16.
  {
    PipeChain any = test_chain(PipeType::ChannelRemap, {0, 5});
    const PipeOutput out = any.process(test_event(0x93, 60, 100), 0);
    assert(out.count == 1 && out.events[0].data[0] == 0x94);  // channel 5
    PipeChain specific = test_chain(PipeType::ChannelRemap, {2, 16});
    const PipeOutput nonmatch = specific.process(test_event(0x90, 60, 100), 0);
    assert(nonmatch.count == 1 && nonmatch.events[0].data[0] == 0x90);
    const PipeOutput moved = specific.process(test_event(0x91, 60, 100), 0);
    assert(moved.count == 1 && moved.events[0].data[0] == 0x9F);
    const PipeOutput non_channel = specific.process(test_event(0xF8, 0, 0, 1), 0);
    assert(non_channel.count == 1 && non_channel.events[0].data[0] == 0xF8);
  }

  // ScaleQuantize: six scales, root transposition and the absolute user mask.
  {
    // C major: 61 -> 60 (tie resolves down), 63 -> 62 (nearest down).
    PipeChain major = test_chain(PipeType::ScaleQuantize, {1, 0, 0});
    assert(output_has(major.process(test_event(0x90, 61, 100), 0), 0x90, 60, 100));
    assert(output_has(major.process(test_event(0x90, 63, 100), 0), 0x90, 62, 100));
    assert(output_has(major.process(test_event(0x90, 60, 100), 0), 0x90, 60, 100));
    // D minor (root 2): C#3 (61) -> C3 (60), F#3 (66) -> F3 (65).
    PipeChain minor = test_chain(PipeType::ScaleQuantize, {2, 2, 0});
    assert(output_has(minor.process(test_event(0x90, 61, 100), 0), 0x90, 60, 100));
    assert(output_has(minor.process(test_event(0x90, 66, 100), 0), 0x90, 65, 100));
    // Major pentatonic on C leaves E and drops nothing; F# snaps to G.
    PipeChain penta_major =
        test_chain(PipeType::ScaleQuantize, {3, 0, 0});
    assert(output_has(penta_major.process(test_event(0x90, 64, 100), 0), 0x90, 64, 100));
    assert(output_has(penta_major.process(test_event(0x90, 66, 100), 0), 0x90, 67, 100));
    // Minor pentatonic on C: Eb stays, E snaps down to Eb.
    PipeChain penta_minor =
        test_chain(PipeType::ScaleQuantize, {4, 0, 0});
    assert(output_has(penta_minor.process(test_event(0x90, 63, 100), 0), 0x90, 63, 100));
    assert(output_has(penta_minor.process(test_event(0x90, 64, 100), 0), 0x90, 63, 100));
    // Dorian on C: Eb and Bb are in, E and B snap down.
    PipeChain dorian = test_chain(PipeType::ScaleQuantize, {5, 0, 0});
    assert(output_has(dorian.process(test_event(0x90, 63, 100), 0), 0x90, 63, 100));
    assert(output_has(dorian.process(test_event(0x90, 64, 100), 0), 0x90, 63, 100));
    assert(output_has(dorian.process(test_event(0x90, 71, 100), 0), 0x90, 70, 100));
    // User mask 0xAAA = pitch classes 1,3,5,7,9,11; C snaps down to B.
    PipeChain user = test_chain(PipeType::ScaleQuantize, {6, 0, 0xAAA});
    assert(output_has(user.process(test_event(0x90, 61, 100), 0), 0x90, 61, 100));
    assert(output_has(user.process(test_event(0x90, 60, 100), 0), 0x90, 59, 100));
    // mask12 boundary: 0 = no allowed class (pass unchanged), 0xFFF = all pass.
    PipeChain empty_mask = test_chain(PipeType::ScaleQuantize, {6, 0, 0});
    assert(output_has(empty_mask.process(test_event(0x90, 60, 100), 0), 0x90, 60, 100));
    PipeChain full_mask =
        test_chain(PipeType::ScaleQuantize, {6, 0, 0xFFF});
    assert(output_has(full_mask.process(test_event(0x90, 61, 100), 0), 0x90, 61, 100));
    // Low/high MIDI note edges stay in range after octave wrapping.
    assert(output_has(major.process(test_event(0x90, 0, 100), 0), 0x90, 0, 100));
    assert(output_has(major.process(test_event(0x90, 127, 100), 0), 0x90, 127, 100));
    // Scale 0 (and an out-of-range scale id) is off and passes untouched.
    PipeChain off = test_chain(PipeType::ScaleQuantize, {0, 0, 0});
    assert(output_has(off.process(test_event(0x90, 61, 100), 0), 0x90, 61, 100));
    PipeChain bad_scale = test_chain(PipeType::ScaleQuantize, {99, 0, 0});
    assert(output_has(bad_scale.process(test_event(0x90, 61, 100), 0), 0x90, 61, 100));
  }

  // Chance: 0 % passes, 100 % drops note-ons only; note-offs keep flowing so
  // a dropped note cannot stick.
  {
    PipeChain never = test_chain(PipeType::Chance, {0});
    assert(never.process(test_event(0x90, 60, 100), 0).count == 1);
    PipeChain always = test_chain(PipeType::Chance, {100});
    assert(always.process(test_event(0x90, 60, 100), 0).count == 0);
    assert(always.process(test_event(0x80, 60, 0), 0).count == 1);
    assert(always.process(test_event(0xB0, 7, 100), 0).count == 1);
  }

  // VelocityRandom: amount bounds, the floor, and amount 0 still applies the
  // floor to note-on velocity; note-off release velocity is untouched.
  {
    PipeChain humanize = test_chain(PipeType::VelocityRandom, {5, 40});
    for (int i = 0; i < 200; ++i) {
      const PipeOutput out = humanize.process(test_event(0x90, 60, 64), 0);
      assert(out.count == 1 && out.events[0].data[2] >= 40);
      assert(out.events[0].data[2] <= 69);
    }
    PipeChain floor_only = test_chain(PipeType::VelocityRandom, {0, 100});
    assert(output_has(floor_only.process(test_event(0x90, 60, 20), 0), 0x90, 60, 100));
    assert(output_has(floor_only.process(test_event(0x90, 60, 127), 0), 0x90, 60, 127));
    assert(output_has(floor_only.process(test_event(0x80, 60, 5), 0), 0x80, 60, 5));
  }

  // Dispatcher: rotation across the configured window, wrapping past ch 16,
  // and note-off pairing back to the channel the note-on used.
  {
    PipeChain dispatch = test_chain(PipeType::Dispatcher, {4, 15});
    const std::uint8_t expected[6] = {15, 16, 1, 2, 15, 16};
    for (int i = 0; i < 6; ++i) {
      const PipeOutput on =
          dispatch.process(test_event(0x90, static_cast<std::uint8_t>(60 + i), 100), 0);
      assert(on.count == 1);
      assert((on.events[0].data[0] & 0x0F) == expected[i] - 1);
      const PipeOutput off =
          dispatch.process(test_event(0x80, static_cast<std::uint8_t>(60 + i), 0), 0);
      assert(off.count == 1);
      assert((off.events[0].data[0] & 0x0F) == expected[i] - 1);
    }
  }

  // Sustain: CC64 down latches note-offs, CC64 up releases them as note-offs;
  // after release a normal note-off passes again.
  {
    PipeChain sustain = test_chain(PipeType::Sustain, {1});
    assert(sustain.process(test_event(0xB0, 64, 127), 0).count == 1);
    assert(sustain.process(test_event(0x90, 60, 100), 0).count == 1);
    assert(sustain.process(test_event(0x80, 60, 0), 0).count == 0);
    assert(sustain.process(test_event(0x90, 64, 100), 0).count == 1);
    assert(sustain.process(test_event(0x80, 64, 0), 0).count == 0);
    const PipeOutput release = sustain.process(test_event(0xB0, 64, 0), 0);
    assert(release.count == 3);  // two note-offs, then CC64
    assert(output_has(release, 0x80, 60, 0) && output_has(release, 0x80, 64, 0));
    assert(output_has(release, 0xB0, 64, 0));
    assert(sustain.process(test_event(0x80, 60, 0), 0).count == 1);
    // latch 0 is a pass-through pipe.
    PipeChain off = test_chain(PipeType::Sustain, {0});
    assert(off.process(test_event(0xB0, 64, 127), 0).count == 1);
    assert(off.process(test_event(0x80, 60, 0), 0).count == 1);
  }

  // NoteLength: fixed length schedules one off and ignores an early note-off;
  // length 0 leaves the stream alone. The generated off re-enters the chain
  // after the pipe (a later ChannelRemap still sees it).
  {
    PipeChain length = test_chain(PipeType::NoteLength, {100});
    assert(length.process(test_event(0x90, 60, 100), 1000).count == 1);
    assert(length.process(test_event(0x80, 60, 0), 1050).count == 0);
    assert(length.tick(1099).count == 0);
    const PipeOutput due = length.tick(1100);
    assert(due.count == 1 && due.events[0].data[0] == 0x80 && due.events[0].data[1] == 60);
    assert(length.tick(1200).count == 0);
    PipeChain staged;
    staged.begin(true);
    assert(staged.append(test_pipe(PipeType::NoteLength, {50})));
    assert(staged.append(test_pipe(PipeType::ChannelRemap, {0, 5})));
    assert(staged.process(test_event(0x90, 60, 100), 0).count == 1);
    const PipeOutput remapped = staged.tick(50);
    assert(remapped.count == 1 && remapped.events[0].data[0] == 0x84 &&
           remapped.events[0].data[1] == 60);
    PipeChain off = test_chain(PipeType::NoteLength, {0});
    assert(off.process(test_event(0x80, 60, 0), 1000).count == 1);
    assert(off.tick(2000).count == 0);
  }

  // Transform: kind conversion, match range, non-match pass-through, bend
  // 14-bit output and invalid out-kind safety.
  {
    PipeChain cc_to_note = test_chain(PipeType::Transform, {1, 7, 7, 0, 60});
    assert(output_has(cc_to_note.process(test_event(0xB0, 7, 100), 0), 0x90, 60, 100));
    assert(output_has(cc_to_note.process(test_event(0xB0, 8, 100), 0), 0xB0, 8, 100));
    PipeChain note_to_cc = test_chain(PipeType::Transform, {0, 60, 60, 1, 74});
    assert(output_has(note_to_cc.process(test_event(0x90, 60, 100), 0), 0xB0, 74, 100));
    assert(output_has(note_to_cc.process(test_event(0x90, 61, 100), 0), 0x90, 61, 100));
    PipeChain to_bend = test_chain(PipeType::Transform, {0, 60, 60, 3, 8192});
    const PipeOutput bend = to_bend.process(test_event(0x90, 60, 100), 0);
    assert(bend.count == 1 && bend.events[0].data[0] == 0xE0 &&
           bend.events[0].data[1] == 0 && bend.events[0].data[2] == 64);
    PipeChain to_pc = test_chain(PipeType::Transform, {1, 10, 20, 2, 42});
    const PipeOutput pc = to_pc.process(test_event(0xB0, 10, 9), 0);
    assert(pc.count == 1 && pc.events[0].size == 2 && pc.events[0].data[0] == 0xC0 &&
           pc.events[0].data[1] == 42);
    PipeChain to_pressure =
        test_chain(PipeType::Transform, {1, 10, 20, 4, 77});
    const PipeOutput pressure = to_pressure.process(test_event(0xB0, 10, 9), 0);
    assert(pressure.count == 1 && pressure.events[0].size == 2 &&
           pressure.events[0].data[0] == 0xD0 && pressure.events[0].data[1] == 77);
    PipeChain partial = test_chain(PipeType::Transform, {});
    assert(output_has(partial.process(test_event(0x90, 60, 100), 0), 0x90, 60, 100));
  }

  // Bypass is honoured for every Wave-1 type: the configured transform never
  // runs, and an empty chain/unknown Wave-2 id passes bytes untouched.
  for (std::uint8_t type = 1; type <= 11; ++type) {
    PipeChain bypassed;
    bypassed.begin(true);
    bypassed.setSeed(7);
    PipeConfig p;
    p.type = static_cast<PipeType>(type);
    p.bypass = true;
    p.paramCount = 5;
    p.params[0] = 0;
    p.params[1] = 0;
    p.params[2] = 127;
    p.params[3] = 1;
    p.params[4] = 1;
    assert(bypassed.append(p));
    const PipeOutput out = bypassed.process(test_event(0x90, 60, 100), 0);
    assert(out.count == 1 && out.events[0].data[0] == 0x90 &&
           out.events[0].data[1] == 60 && out.events[0].data[2] == 100);
  }
  {
    // Chunked wire-style configuration: a Filter has 8 positional params, so
    // it arrives as a 5-param chunk followed by a 3-param chunk at offset 5.
    PipeChain chunked;
    chunked.begin(true);
    const std::int32_t first[5] = {1, 0, 0, 0, 0};
    const std::int32_t rest[3] = {0, 0, 0};
    assert(chunked.setPipeChunk(0, 1, false, 0, first, 5));
    assert(chunked.setPipeChunk(0, 1, false, 5, rest, 3));
    assert(chunked.process(test_event(0x90, 60, 100), 0).count == 1);
    assert(chunked.process(test_event(0x80, 60, 0), 0).count == 0);
    assert(!chunked.setPipeChunk(1, 1, false, 5, rest, 3));  // no pipe 1 yet
    assert(!chunked.setPipeChunk(0, 2, false, 5, rest, 3));  // type mismatch
    assert(!chunked.setPipeChunk(8, 1, false, 0, first, 5));  // index out of range
  }
  {
    // Type ids are the wire contract (1..16); anything outside it is rejected
    // at config time instead of being stored and passed through silently.
    PipeChain unknown;
    unknown.begin(true);
    PipeConfig p;
    p.type = static_cast<PipeType>(17);
    p.paramCount = 0;
    assert(!unknown.append(p));
    // Out-of-range/unknown params never crash a pipe access: the missing
    // inHi/outLo/outHi fall back to their documented defaults.
    PipeChain sparse = test_chain(PipeType::CcRemap, {3});
    assert(output_has(sparse.process(test_event(0xB0, 3, 1), 0), 0xB0, 0, 1));
  }

  std::printf("PASS: Wave-1 MIDI pipes (filter/range/remap/scale/chance/velocity/"
              "dispatch/sustain/length/transform, bypass)\n");
}

void run_wave2_pipe_tests() {
  using namespace mnky::midifx;

  // Division accuracy: an arp division is 24 >> div ticks, so 1/4, 1/8, 1/16 and
  // 1/32 land on ticks 24, 12, 6 and 3. One held note keeps the pattern trivial.
  {
    const std::uint32_t expected[4] = {24, 12, 6, 3};
    for (int div = 0; div < 4; ++div) {
      PipeChain chain = test_chain(PipeType::Arp, {div, 0, 1, 50, 0, 50});
      assert(chain.process(test_event(0x90, 60, 100), 0).count == 0);
      const auto ons = kind_events(run_tick_train(chain, 0, expected[div] * 3 + 1), 0x90);
      assert(times_of(ons) == (std::vector<std::uint32_t>{expected[div],
                                                          2 * expected[div],
                                                          3 * expected[div]}));
      assert(notes_of(ons) == (std::vector<int>{60, 60, 60}));
    }
  }

  // Gate percentage of a 1/16 step: 50 % is 3 ticks on / 3 ticks off, and the
  // gate note-off always precedes the next step's note-on.
  {
    PipeChain chain = test_chain(PipeType::Arp, {2, 0, 1, 50, 0, 50});
    chain.process(test_event(0x90, 60, 100), 0);
    const auto events = run_tick_train(chain, 0, 13);
    assert(times_of(kind_events(events, 0x90)) == (std::vector<std::uint32_t>{6, 12}));
    assert(times_of(kind_events(events, 0x80)) == (std::vector<std::uint32_t>{9}));
  }

  // Swing: 50 is straight, 75 delays every odd step by 25 % of the step
  // (six ticks at 1/4), so the pair is 24 + 6 ticks and the next pair restarts
  // on the grid: ons on 30, 48 and 78.
  {
    PipeChain straight = test_chain(PipeType::Arp, {0, 0, 1, 50, 0, 50});
    straight.process(test_event(0x90, 60, 100), 0);
    assert(times_of(kind_events(run_tick_train(straight, 0, 73), 0x90)) ==
           (std::vector<std::uint32_t>{24, 48, 72}));
    PipeChain swung = test_chain(PipeType::Arp, {0, 0, 1, 75, 0, 50});
    swung.process(test_event(0x90, 60, 100), 0);
    assert(times_of(kind_events(run_tick_train(swung, 0, 78), 0x90)) ==
           (std::vector<std::uint32_t>{30, 48, 78}));
  }

  // Accent scales the velocity of every fourth grid step (the beat) by
  // (100 + accent) %: velocity 80 at accent 50 becomes 120 on steps 4 and 8.
  {
    PipeChain chain = test_chain(PipeType::Arp, {3, 0, 1, 50, 50, 50});
    chain.process(test_event(0x90, 60, 80), 0);
    const auto ons = kind_events(run_tick_train(chain, 0, 24), 0x90);
    assert(values_of(ons) == (std::vector<int>{80, 80, 80, 120, 80, 80, 80, 120}));
  }

  // Arp types over a held C-E-G, one octave, 1/4 steps: up, down, updown (the
  // down leg repeats neither endpoint), chord (all held notes on one step) and
  // random (any pattern entry, and not always the same one).
  {
    const auto arp_notes = [](int type, int octaves, std::uint32_t until) {
      PipeChain chain = test_chain(PipeType::Arp, {0, type, octaves, 50, 0, 25});
      assert(chain.process(test_event(0x90, 60, 100), 0).count == 0);
      assert(chain.process(test_event(0x90, 64, 90), 0).count == 0);
      assert(chain.process(test_event(0x90, 67, 80), 0).count == 0);
      return notes_of(kind_events(run_tick_train(chain, 0, until), 0x90));
    };
    assert(arp_notes(0, 1, 96) == (std::vector<int>{60, 64, 67, 60}));
    assert(arp_notes(1, 1, 96) == (std::vector<int>{67, 64, 60, 67}));
    assert(arp_notes(2, 1, 96) == (std::vector<int>{60, 64, 67, 64}));
    // Two octaves stack the set 12 semitones higher before walking it.
    assert(arp_notes(0, 2, 144) == (std::vector<int>{60, 64, 67, 72, 76, 79}));
    // Chord holds every base-octave note together; octaves is ignored.
    {
      PipeChain chord = test_chain(PipeType::Arp, {0, 4, 4, 50, 0, 25});
      chord.process(test_event(0x90, 60, 100), 0);
      chord.process(test_event(0x90, 64, 90), 0);
      chord.process(test_event(0x90, 67, 80), 0);
      const auto events = run_tick_train(chord, 0, 24);
      assert(notes_of(kind_events(events, 0x90)) == (std::vector<int>{60, 64, 67}));
      assert(values_of(kind_events(events, 0x90)) == (std::vector<int>{100, 90, 80}));
      assert(times_of(kind_events(events, 0x90)) ==
             (std::vector<std::uint32_t>{24, 24, 24}));
    }
    {
      const std::vector<int> random_notes = arp_notes(3, 1, 16 * 24);
      assert(random_notes.size() == 16);
      bool saw_other = false;
      for (int note : random_notes) {
        assert(note == 60 || note == 64 || note == 67);
        saw_other = saw_other || note != random_notes[0];
      }
      assert(saw_other);
    }
  }

  // Holds drive the notes: the note-on is consumed (the arp plays its own), the
  // last note-off releases everything immediately and the grid stops.
  {
    PipeChain chain = test_chain(PipeType::Arp, {2, 0, 1, 50, 0, 50});
    assert(chain.process(test_event(0x90, 60, 100), 0).count == 0);
    assert(kind_events(run_tick_train(chain, 0, 6), 0x90).size() == 1);
    const PipeOutput released = chain.process(test_event(0x80, 60, 0), 0);
    assert(released.count == 1);
    assert(released.events[0].data[0] == 0x80 && released.events[0].data[1] == 60);
    assert(run_tick_train(chain, 7, 48).empty());
  }

  // A chain edit drops the clocked runtime state: the note it had on is
  // released on the next tick instead of sounding forever.
  {
    PipeChain chain = test_chain(PipeType::Arp, {2, 0, 1, 50, 0, 50});
    chain.process(test_event(0x90, 60, 100), 0);
    assert(kind_events(run_tick_train(chain, 0, 6), 0x90).size() == 1);
    assert(chain.setPipeChunk(0, 1, false, 0, nullptr, 0));  // replace with a Filter
    const PipeOutput flushed = chain.tick(1000);
    assert(flushed.count == 1);
    assert(flushed.events[0].data[0] == 0x80 && flushed.events[0].data[1] == 60);
  }

  // Bypassing a running arp is the same edit seen through the bypass flag: the
  // note it had on goes off and the routed bytes pass again.
  {
    PipeChain chain = test_chain(PipeType::Arp, {2, 0, 1, 50, 0, 50});
    chain.process(test_event(0x90, 60, 100), 0);
    assert(kind_events(run_tick_train(chain, 0, 6), 0x90).size() == 1);
    const std::int32_t params[1] = {0};
    assert(chain.setPipeChunk(0, 12, true, 0, params, 1));  // bypass, same type
    const PipeOutput flushed = chain.tick(1000);
    assert(flushed.count == 1 && flushed.events[0].data[0] == 0x80 &&
           flushed.events[0].data[1] == 60);
    const PipeOutput passthrough = chain.process(test_event(0x90, 62, 64), 0);
    assert(passthrough.count == 1 && passthrough.events[0].data[1] == 62);
  }

  // Repeater: every division step retriggers the held set, the falloff envelope
  // restarts every `repeats` steps and each strike is gated for half a step.
  {
    PipeChain chain = test_chain(PipeType::Repeater, {2, 3, 50});
    assert(chain.process(test_event(0x90, 60, 100), 0).count == 0);
    const auto events = run_tick_train(chain, 0, 25);
    assert(times_of(kind_events(events, 0x90)) ==
           (std::vector<std::uint32_t>{6, 12, 18, 24}));
    assert(values_of(kind_events(events, 0x90)) == (std::vector<int>{100, 50, 25, 100}));
    assert(times_of(kind_events(events, 0x80)) ==
           (std::vector<std::uint32_t>{9, 15, 21}));
    // repeats 1 repeats the same note at full velocity on every step.
    PipeChain single = test_chain(PipeType::Repeater, {2, 1, 90});
    single.process(test_event(0x90, 60, 100), 0);
    assert(values_of(kind_events(run_tick_train(single, 0, 18), 0x90)) ==
           (std::vector<int>{100, 100, 100}));
  }

  // CcLfo: one wave cycle spans eight division steps, the phase is the absolute
  // step index and depth/center scale the sampled shape onto 0..127. Values are
  // depth 64 around center 64 at 1/16 (phases 1, 2, 3, 4).
  {
    const auto lfo_values = [](int wave, std::uint32_t until) {
      PipeChain chain = test_chain(PipeType::CcLfo, {74, wave, 2, 64, 64});
      return values_of(kind_events(run_tick_train(chain, 0, until), 0xB0));
    };
    assert(lfo_values(0, 24) == (std::vector<int>{109, 127, 109, 64}));
    assert(lfo_values(1, 24) == (std::vector<int>{96, 127, 96, 64}));
    assert(lfo_values(2, 24) == (std::vector<int>{127, 127, 127, 0}));
    assert(lfo_values(3, 24) == (std::vector<int>{17, 32, 48, 64}));
    // A full cycle wraps back to phase 0 on step 8 (tick 48): the sine returns
    // to the centre and the square to its high half.
    assert(lfo_values(0, 48) ==
           (std::vector<int>{109, 127, 109, 64, 19, 0, 19, 64}));
    assert(lfo_values(2, 48).back() == 127);
    // The CC number and channel are part of the contract (no channel param).
    PipeChain chain = test_chain(PipeType::CcLfo, {74, 0, 2, 64, 64});
    const auto cc = kind_events(run_tick_train(chain, 0, 6), 0xB0);
    assert(cc.size() == 1 && cc[0].data[0] == 0xB0 && cc[0].data[1] == 74);
    // depth 0 holds the centre; the value is clamped into the CC range.
    PipeChain flat = test_chain(PipeType::CcLfo, {1, 0, 2, 0, 90});
    assert(values_of(kind_events(run_tick_train(flat, 0, 12), 0xB0)) ==
           (std::vector<int>{90, 90}));
    PipeChain clipped = test_chain(PipeType::CcLfo, {1, 2, 2, 127, 10});
    assert(values_of(kind_events(run_tick_train(clipped, 0, 12), 0xB0)) ==
           (std::vector<int>{127, 127}));  // 10 + 127 clamps to 127
  }

  // Harmonizer: the input note passes and each voice is added, and every
  // note-off that follows releases exactly the notes that note-on created.
  {
    PipeChain chain = test_chain(PipeType::Harmonizer, {2, 7, 4});
    const PipeOutput on = chain.process(test_event(0x90, 60, 100), 0);
    std::vector<StampedEvent> stamped;
    collect(on, 0, stamped);
    assert(notes_of(stamped) == (std::vector<int>{60, 67, 64}));
    std::vector<StampedEvent> offs;
    collect(chain.process(test_event(0x80, 60, 0), 0), 0, offs);
    assert(notes_of(offs) == (std::vector<int>{60, 67, 64}));
  }

  // Overlap: holding C (+7, +4) makes E sound twice (the C+4 voice and E's own
  // note-on). Releasing E must not silence the harmonized voice; releasing C
  // then closes both E references.
  {
    PipeChain chain = test_chain(PipeType::Harmonizer, {2, 7, 4});
    std::vector<StampedEvent> events;
    collect(chain.process(test_event(0x90, 60, 100), 0), 0, events);
    collect(chain.process(test_event(0x90, 64, 90), 0), 1, events);
    assert(notes_of(events) == (std::vector<int>{60, 67, 64, 64, 71, 68}));
    std::vector<StampedEvent> off_e;
    collect(chain.process(test_event(0x80, 64, 0), 0), 0, off_e);
    assert(notes_of(off_e) == (std::vector<int>{71, 68}));  // E itself stays on
    std::vector<StampedEvent> off_c;
    collect(chain.process(test_event(0x80, 60, 0), 0), 0, off_c);
    assert(notes_of(off_c) == (std::vector<int>{60, 67, 64}));
    // A retriggered note-on does not stack voices or references.
    std::vector<StampedEvent> retrigger;
    collect(chain.process(test_event(0x90, 60, 100), 0), 0, retrigger);
    collect(chain.process(test_event(0x90, 60, 100), 0), 1, retrigger);
    assert(notes_of(retrigger) == (std::vector<int>{60, 67, 64, 60}));
    std::vector<StampedEvent> release;
    collect(chain.process(test_event(0x80, 60, 0), 0), 0, release);
    assert(notes_of(release) == (std::vector<int>{60, 67, 64}));
    assert(chain.process(test_event(0x80, 60, 0), 0).count == 1);  // untracked off
  }

  // Sustain interplay, both orders. With the Sustain first the harmonizer sees
  // the input note-off only when CC64 lifts, so the voices release together with
  // it; with the Sustain last the harmonizer's own offs are latched and released
  // by the sustain pipe like any other note-off.
  {
    PipeChain sustain_first;
    sustain_first.begin(true);
    assert(sustain_first.append(test_pipe(PipeType::Sustain, {1})));
    assert(sustain_first.append(test_pipe(PipeType::Harmonizer, {1, 7})));
    assert(sustain_first.process(test_event(0xB0, 64, 127), 0).count == 1);
    std::vector<StampedEvent> events;
    assert(sustain_first.process(test_event(0x90, 60, 100), 0).count == 2);
    assert(sustain_first.process(test_event(0x80, 60, 0), 0).count == 0);
    collect(sustain_first.process(test_event(0xB0, 64, 0), 0), 0, events);
    // The sustain pipe's release reaches the harmonizer as the input note-off,
    // which answers with the matched pair instead of passing it through.
    assert(notes_of(kind_events(events, 0x80)) == (std::vector<int>{60, 67}));

    PipeChain harmonizer_first;
    harmonizer_first.begin(true);
    assert(harmonizer_first.append(test_pipe(PipeType::Harmonizer, {1, 7})));
    assert(harmonizer_first.append(test_pipe(PipeType::Sustain, {1})));
    assert(harmonizer_first.process(test_event(0xB0, 64, 127), 0).count == 1);
    assert(harmonizer_first.process(test_event(0x90, 60, 100), 0).count == 2);
    assert(harmonizer_first.process(test_event(0x80, 60, 0), 0).count == 0);
    std::vector<StampedEvent> latched;
    collect(harmonizer_first.process(test_event(0xB0, 64, 0), 0), 0, latched);
    assert(notes_of(kind_events(latched, 0x80)) == (std::vector<int>{60, 67}));
  }

  // Bypass: every Wave-2 type passes the routed bytes untouched and emits
  // nothing on the clock grid.
  for (std::uint8_t type = 12; type <= 16; ++type) {
    PipeChain chain;
    chain.begin(true);
    chain.setSeed(7);
    PipeConfig p;
    p.type = static_cast<PipeType>(type);
    p.bypass = true;
    p.paramCount = 6;
    for (std::size_t i = 0; i < p.params.size(); ++i) p.params[i] = 1;
    assert(chain.append(p));
    const PipeOutput out = chain.process(test_event(0x90, 60, 100), 0);
    assert(out.count == 1 && out.events[0].data[0] == 0x90 &&
           out.events[0].data[1] == 60 && out.events[0].data[2] == 100);
    assert(run_tick_train(chain, 0, 48).empty());
  }

  // MidiDelay: the dry event passes and `repeats` echoes follow delayMs apart,
  // each attenuated by falloff; the note-off echo keeps the echo gates paired.
  {
    PipeChain chain = test_chain(PipeType::MidiDelay, {100, 3, 50});
    assert(chain.process(test_event(0x90, 60, 100), 0).count == 1);
    const auto events = run_ms_train(chain, 1, 400);
    const auto ons = kind_events(events, 0x90);
    assert(times_of(ons) == (std::vector<std::uint32_t>{100, 200, 300}));
    assert(values_of(ons) == (std::vector<int>{100, 50, 25}));
    assert(notes_of(ons) == (std::vector<int>{60, 60, 60}));
    // The echo tail is what turns a chain edit into a stuck note: the delayed
    // note-ons are tracked, so the edit releases them.
    assert(chain.setPipeChunk(0, 1, false, 0, nullptr, 0));
    const PipeOutput flushed = chain.tick(1000);
    assert(flushed.count == 1 && flushed.events[0].data[0] == 0x80 &&
           flushed.events[0].data[1] == 60);
  }
  {
    PipeChain chain = test_chain(PipeType::MidiDelay, {100, 1, 0});
    assert(chain.process(test_event(0x90, 60, 100), 0).count == 1);
    assert(chain.process(test_event(0x80, 60, 0), 30).count == 1);
    const auto events = run_ms_train(chain, 31, 200);
    assert(times_of(kind_events(events, 0x90)) == (std::vector<std::uint32_t>{100}));
    assert(times_of(kind_events(events, 0x80)) == (std::vector<std::uint32_t>{130}));
    // delayMs 0 (the default) is off and only passes the dry event.
    PipeChain off = test_chain(PipeType::MidiDelay, {});
    assert(off.process(test_event(0x90, 60, 100), 0).count == 1);
    assert(run_ms_train(off, 1, 500).empty());
  }

  std::printf("PASS: Wave-2 clocked pipes (arp divisions/types/swing/accent/gate, "
              "repeater falloff, LFO waves, harmonizer pairing + sustain, delay "
              "taps, bypass)\n");
}

// Chain/scope helpers for the pipe-channel-scope expectations ported from the
// Pi suite (pi/midi/test_midifx.cpp::channelScope). The firmware's 0x19 frame
// cannot carry a mask (a Pico chain is always 0xFFFF), but the Pi chain owner
// sets PipeConfig::chMask, so the shared core implements the same gate.
PipeConfig scoped(PipeType type, std::initializer_list<int> params,
                  std::uint16_t mask) {
  PipeConfig config = test_pipe(type, params);
  config.chMask = mask;
  return config;
}

PipeChain chain_of(std::initializer_list<PipeConfig> pipes) {
  PipeChain chain;
  chain.begin(true);
  chain.setSeed(0x12345678u);
  const std::vector<PipeConfig> config(pipes);
  CHECK(chain.configure(config.data(), config.size()));
  return chain;
}

PipeOutput run(PipeChain& chain, const PipeEvent& event, std::uint32_t nowMs = 0) {
  return chain.process(event, nowMs);
}

PipeEvent test_note_on(std::uint8_t channel = 0, std::uint8_t note = 60,
                       std::uint8_t velocity = 100) {
  return test_event(static_cast<std::uint8_t>(0x90 | channel), note, velocity);
}

PipeEvent test_note_off(std::uint8_t channel = 0, std::uint8_t note = 60) {
  return test_event(static_cast<std::uint8_t>(0x80 | channel), note, 0);
}

bool identical(const PipeEvent& a, const PipeEvent& b) {
  return a.size == b.size && a.data[0] == b.data[0] &&
         a.data[1] == b.data[1] && a.data[2] == b.data[2];
}

bool matches(const PipeEvent& event, std::uint8_t status, std::uint8_t data1,
             std::uint8_t data2, std::uint8_t size) {
  return event.size == size && event.data[0] == status &&
         event.data[1] == data1 && event.data[2] == data2;
}

void channelScope() {
  // Filter: a note-on blocker drops channel 1 and leaves channel 2 alone.
  PipeChain filter =
      chain_of({scoped(PipeType::Filter, {0, 0, 0, 0, 0, 0, 0, 0}, 0x0001)});
  CHECK(run(filter, test_note_on(0, 60, 100)).count == 0);
  const PipeEvent otherNote = test_note_on(1, 60, 100);
  const PipeOutput filterOther = run(filter, otherNote);
  CHECK(filterOther.count == 1 && identical(filterOther.events[0], otherNote));
  // A restricted scope leaves a channel-less realtime byte untouched even
  // though the same Filter would block it with the default all-mask.
  const PipeEvent clock = test_event(0xF8, 0, 0, 1);
  const PipeOutput scopedClock = run(filter, clock);
  CHECK(scopedClock.count == 1 && identical(scopedClock.events[0], clock));
  PipeChain allFilter =
      chain_of({test_pipe(PipeType::Filter, {0, 0, 0, 0, 0, 0, 0, 0})});
  CHECK(run(allFilter, clock).count == 0);

  // NoteRange: matching note outside the window drops, other channel passes.
  PipeChain range = chain_of({scoped(PipeType::NoteRange, {0, 59, 0, 127}, 0x0001)});
  CHECK(run(range, test_note_on(0, 60, 100)).count == 0);
  const PipeOutput rangeOther = run(range, test_note_on(1, 60, 100));
  CHECK(rangeOther.count == 1 && identical(rangeOther.events[0], test_note_on(1, 60, 100)));

  // CcRemap: matching CC is remapped, other channel is byte-identical.
  PipeChain cc = chain_of({scoped(PipeType::CcRemap, {0, 127, 100, 100}, 0x0001)});
  const PipeOutput ccMapped = run(cc, test_event(0xB0, 1, 5));
  CHECK(ccMapped.count == 1 && matches(ccMapped.events[0], 0xB0, 100, 5, 3));
  const PipeEvent ccOther = test_event(0xB1, 1, 5);
  const PipeOutput ccKept = run(cc, ccOther);
  CHECK(ccKept.count == 1 && identical(ccKept.events[0], ccOther));

  // ChannelRemap: the scope reads the channel at this pipe's entry.
  PipeChain remap = chain_of({scoped(PipeType::ChannelRemap, {0, 5}, 0x0001)});
  const PipeOutput remapped = run(remap, test_note_on(0, 60, 100));
  CHECK(remapped.count == 1 && matches(remapped.events[0], 0x94, 60, 100, 3));
  const PipeEvent remapOther = test_note_on(1, 60, 100);
  const PipeOutput remapKept = run(remap, remapOther);
  CHECK(remapKept.count == 1 && identical(remapKept.events[0], remapOther));

  // ScaleQuantize: matching note is quantized, other channel is untouched.
  PipeChain scale = chain_of({scoped(PipeType::ScaleQuantize, {1, 0}, 0x0001)});
  const PipeOutput quantized = run(scale, test_note_on(0, 61, 100));
  CHECK(quantized.count == 1 && quantized.events[0].data[1] == 60);
  const PipeEvent scaleOther = test_note_on(1, 61, 100);
  const PipeOutput scaleKept = run(scale, scaleOther);
  CHECK(scaleKept.count == 1 && identical(scaleKept.events[0], scaleOther));

  // Chance: the RNG is consumed only for matching note-ons.
  PipeChain chance = chain_of({scoped(PipeType::Chance, {100}, 0x0001)});
  CHECK(run(chance, test_note_on(0, 60, 100)).count == 0);
  const PipeEvent chanceOther = test_note_on(1, 60, 100);
  const PipeOutput chanceKept = run(chance, chanceOther);
  CHECK(chanceKept.count == 1 && identical(chanceKept.events[0], chanceOther));

  // VelocityRandom: matching velocity is clamped to the floor, other channel
  // keeps its velocity.
  PipeChain velocity =
      chain_of({scoped(PipeType::VelocityRandom, {0, 127}, 0x0001)});
  const PipeOutput loud = run(velocity, test_note_on(0, 60, 100));
  CHECK(loud.count == 1 && loud.events[0].data[2] == 127);
  const PipeEvent velocityOther = test_note_on(1, 60, 100);
  const PipeOutput velocityKept = run(velocity, velocityOther);
  CHECK(velocityKept.count == 1 && identical(velocityKept.events[0], velocityOther));

  // Dispatcher: matching note is spread to the configured channel, other
  // channel passes without advancing the rotation.
  PipeChain dispatcher = chain_of({scoped(PipeType::Dispatcher, {2, 5}, 0x0001)});
  const PipeOutput dispatched = run(dispatcher, test_note_on(0, 60, 100));
  CHECK(dispatched.count == 1 && matches(dispatched.events[0], 0x94, 60, 100, 3));
  const PipeEvent dispatcherOther = test_note_on(1, 60, 100);
  const PipeOutput dispatcherKept = run(dispatcher, dispatcherOther);
  CHECK(dispatcherKept.count == 1 && identical(dispatcherKept.events[0], dispatcherOther));

  // Sustain: only matching channels may latch or be held; an out-of-scope
  // CC64 release passes through and does not release the in-scope latch.
  PipeChain sustain = chain_of({scoped(PipeType::Sustain, {1}, 0x0001)});
  CHECK(run(sustain, test_note_on(0, 60, 100)).count == 1);
  CHECK(run(sustain, test_event(0xB0, 64, 127)).count == 1);
  CHECK(run(sustain, test_note_off(0, 60)).count == 0);
  const PipeEvent otherOff = test_note_off(1, 60);
  const PipeOutput otherHeld = run(sustain, otherOff);
  CHECK(otherHeld.count == 1 && identical(otherHeld.events[0], otherOff));
  const PipeEvent otherRelease = test_event(0xB1, 64, 0);
  const PipeOutput otherReleaseKept = run(sustain, otherRelease);
  CHECK(otherReleaseKept.count == 1 &&
        identical(otherReleaseKept.events[0], otherRelease));
  const PipeOutput release = run(sustain, test_event(0xB0, 64, 0));
  CHECK(release.count == 2);  // held note-off + the CC64 release
  CHECK(matches(release.events[0], 0x80, 60, 0, 3));

  // NoteLength: matching off is suppressed until the scheduled one; other
  // channel's off is untouched. The scheduled off re-enters after the pipe.
  PipeChain length = chain_of({scoped(PipeType::NoteLength, {100}, 0x0001)});
  CHECK(run(length, test_note_on(0, 60, 100), 1000).count == 1);
  CHECK(run(length, test_note_off(0, 60), 1050).count == 0);
  const PipeEvent lengthOther = test_note_off(1, 60);
  const PipeOutput lengthKept = run(length, lengthOther, 1050);
  CHECK(lengthKept.count == 1 && identical(lengthKept.events[0], lengthOther));
  const PipeOutput due = length.tick(1100);
  CHECK(due.count == 1 && matches(due.events[0], 0x80, 60, 0, 3));

  // Transform: matching CC is converted, other channel is byte-identical.
  PipeChain transform =
      chain_of({scoped(PipeType::Transform, {1, 74, 74, 1, 7}, 0x0001)});
  const PipeOutput converted = run(transform, test_event(0xB0, 74, 33));
  CHECK(converted.count == 1 && matches(converted.events[0], 0xB0, 7, 33, 3));
  const PipeEvent transformOther = test_event(0xB1, 74, 33);
  const PipeOutput transformKept = run(transform, transformOther);
  CHECK(transformKept.count == 1 && identical(transformKept.events[0], transformOther));

  // A 0 mask makes the pipe inert, including for channel-less bytes.
  PipeChain inert =
      chain_of({scoped(PipeType::Filter, {0, 0, 0, 0, 0, 0, 0, 0}, 0x0000)});
  const PipeEvent inertNote = test_note_on(0, 60, 100);
  const PipeOutput inertOut = run(inert, inertNote);
  CHECK(inertOut.count == 1 && identical(inertOut.events[0], inertNote));
  CHECK(run(inert, clock).count == 1);
  CHECK(run(inert, test_event(0xB0, 1, 2)).count == 1);

  // ChannelRemap ordering: the next pipe's scope sees the remapped channel.
  PipeChain remapThenScopedFilter = chain_of({
      scoped(PipeType::ChannelRemap, {0, 5}, 0x0001),
      scoped(PipeType::Filter, {0, 0, 0, 0, 0, 0, 0, 0}, 0x0010),
  });
  CHECK(run(remapThenScopedFilter, test_note_on(0, 60, 100)).count == 0);
  PipeChain remapThenWrongScope = chain_of({
      scoped(PipeType::ChannelRemap, {0, 5}, 0x0001),
      scoped(PipeType::Filter, {0, 0, 0, 0, 0, 0, 0, 0}, 0x0001),
  });
  const PipeOutput afterRemap = run(remapThenWrongScope, test_note_on(0, 60, 100));
  CHECK(afterRemap.count == 1 && matches(afterRemap.events[0], 0x94, 60, 100, 3));
  // A pipe-gate before the remap sees the original channel.
  PipeChain filterThenRemap = chain_of({
      scoped(PipeType::Filter, {0, 0, 0, 0, 0, 0, 0, 0}, 0x0001),
      scoped(PipeType::ChannelRemap, {0, 5}, 0xFFFF),
  });
  CHECK(run(filterThenRemap, test_note_on(0, 60, 100)).count == 0);
  const PipeOutput otherRemapped = run(filterThenRemap, test_note_on(1, 60, 100));
  CHECK(otherRemapped.count == 1 &&
        matches(otherRemapped.events[0], 0x94, 60, 100, 3));

  pass("pipe channel scope");
}

// ---------------------------------------------------------------------------
// Chain configuration: the Pi-side configure() path plus the 0x19 chunk limits
// ---------------------------------------------------------------------------
void configureAndLimits() {
  using namespace mnky::midifx;
  // A fresh chain is disabled: an unconfigured route drops bytes.
  PipeChain fresh;
  CHECK(fresh.process(test_event(0x90, 60, 100), 0).count == 0);
  fresh.begin(true);
  CHECK(fresh.pipeCount() == 0);
  CHECK(fresh.process(test_event(0x90, 60, 100), 0).count == 1);

  // configure() replaces the whole chain atomically and enables it (the Pi
  // chain owner has no separate pipe gate).
  const PipeConfig pipes[2] = {test_pipe(PipeType::Filter, {1, 1, 1, 1, 1, 1, 1, 1}),
                               test_pipe(PipeType::ChannelRemap, {0, 5})};
  PipeChain chain;
  CHECK(chain.configure(pipes, 2));
  CHECK(chain.enabled());
  CHECK(chain.pipeCount() == 2);
  const PipeOutput moved = chain.process(test_event(0x90, 60, 100), 0);
  CHECK(moved.count == 1 && moved.events[0].data[0] == 0x94);

  // Invalid input leaves the current chain untouched.
  const PipeConfig badType[1] = {test_pipe(static_cast<PipeType>(17), {})};
  CHECK(!chain.configure(badType, 1));
  PipeConfig tooManyParams = test_pipe(PipeType::Filter, {});
  tooManyParams.paramCount = 11;
  CHECK(!chain.configure(&tooManyParams, 1));
  const PipeConfig tooManyPipes[9] = {
      test_pipe(PipeType::Filter, {}), test_pipe(PipeType::Filter, {}),
      test_pipe(PipeType::Filter, {}), test_pipe(PipeType::Filter, {}),
      test_pipe(PipeType::Filter, {}), test_pipe(PipeType::Filter, {}),
      test_pipe(PipeType::Filter, {}), test_pipe(PipeType::Filter, {}),
      test_pipe(PipeType::Filter, {})};
  CHECK(!chain.configure(tooManyPipes, 9));
  CHECK(chain.pipeCount() == 2);
  CHECK(chain.process(test_event(0x90, 60, 100), 0).count == 1);

  // An empty chain is valid, passes valid messages and drops malformed sizes.
  PipeChain empty;
  CHECK(empty.empty());
  CHECK(empty.configure(nullptr, 0));
  CHECK(empty.empty() && empty.enabled());
  CHECK(empty.process(test_event(0x90, 60, 100), 0).count == 1);
  PipeEvent malformed;
  malformed.size = 0;
  CHECK(empty.process(malformed, 0).count == 0);
  malformed.size = 4;
  CHECK(empty.process(malformed, 0).count == 0);
  CHECK(!chain.configure(nullptr, 1));

  // The maximum chain length is 8; 9 is rejected and leaves the chain alone.
  {
    std::vector<PipeConfig> eight;
    for (int i = 0; i < 8; ++i) eight.push_back(test_pipe(PipeType::Filter, {}));
    PipeChain full;
    CHECK(full.configure(eight.data(), eight.size()));
    CHECK(full.pipeCount() == 8);
    eight.push_back(test_pipe(PipeType::Filter, {}));
    CHECK(!full.configure(eight.data(), eight.size()));
    CHECK(full.pipeCount() == 8);
  }

  // Chain order is the wire order: a ChannelRemap before a Transform decides
  // the channel the transform output carries.
  {
    PipeChain ordered = chain_of({test_pipe(PipeType::ChannelRemap, {0, 2}),
                                  test_pipe(PipeType::Transform, {0, 60, 60, 1, 7})});
    const PipeOutput out = ordered.process(test_event(0x90, 60, 99), 0);
    CHECK(out.count == 1 && matches(out.events[0], 0xB1, 7, 99, 3));
  }

  // setPipeChunk is the 0x19 path: the offset-0 chunk carries the channel
  // scope, a continuation chunk keeps it (wire continuation frames cannot move
  // a pipe's scope).
  {
    PipeChain scopedChain;
    scopedChain.begin(true);
    // A note-on-blocking Filter scoped to channel 1: channel 1 is dropped,
    // channel 0 passes untouched because it never enters the pipe.
    const std::int32_t maskFirst[4] = {0, 0, 0, 0};
    CHECK(scopedChain.setPipeChunk(0, 1, false, 0, maskFirst, 4, 0x0002));
    CHECK(scopedChain.process(test_note_on(0, 60, 100), 0).count == 1);
    CHECK(scopedChain.process(test_note_on(1, 60, 100), 0).count == 0);
    const std::int32_t maskTail[4] = {0, 0, 0, 0};
    CHECK(scopedChain.setPipeChunk(0, 1, false, 4, maskTail, 4, 0x0001));
    // The continuation's mask is ignored: channel 1 is still in scope (dropped)
    // and channel 0 still passes through.
    CHECK(scopedChain.process(test_note_on(0, 60, 100), 0).count == 1);
    CHECK(scopedChain.process(test_note_on(1, 60, 100), 0).count == 0);
  }

  // setPipeChunk is the 0x19 path: index/type/offset/count bounds.
  CHECK(!chain.setPipeChunk(8, 1, false, 0, nullptr, 0));
  CHECK(!chain.setPipeChunk(0, 0, false, 0, nullptr, 0));
  CHECK(!chain.setPipeChunk(0, 17, false, 0, nullptr, 0));
  const std::int32_t params[6] = {0, 0, 0, 0, 0, 0};
  CHECK(!chain.setPipeChunk(0, 1, false, 6, params, 5));
  CHECK(!chain.setPipeChunk(0, 1, false, 0, params, 11));
  CHECK(!chain.setPipeChunk(0, 2, false, 5, params, 1));  // type mismatch

  // NoteLength's scheduled off re-enters after the pipe, so a later
  // ChannelRemap still sees it (the Pi engine's staged-chain expectation).
  PipeChain staged;
  staged.begin(true);
  CHECK(staged.append(test_pipe(PipeType::NoteLength, {50})));
  CHECK(staged.append(test_pipe(PipeType::ChannelRemap, {0, 5})));
  CHECK(staged.process(test_event(0x90, 60, 100), 0).count == 1);
  const PipeOutput remapped = staged.tick(50);
  CHECK(remapped.count == 1 && remapped.events[0].data[0] == 0x84 &&
        remapped.events[0].data[1] == 60);
  pass("configure/limits");
}

// ---------------------------------------------------------------------------
// Output cap diagnostics: events past kMaxPipeEvents are counted, never
// silently lost. A Sustain release burst is the easiest way past the cap.
// ---------------------------------------------------------------------------
void outputCapDiagnostics() {
  using namespace mnky::midifx;
  PipeChain chain = test_chain(PipeType::Sustain, {1});
  CHECK(chain.process(test_event(0xB0, 64, 127), 0).count == 1);
  for (int note = 0; note < 72; ++note)
    CHECK(chain.process(test_event(0x90, static_cast<std::uint8_t>(note), 100), 0)
              .count == 1);
  for (int note = 0; note < 72; ++note)
    CHECK(chain.process(test_event(0x80, static_cast<std::uint8_t>(note), 0), 0)
              .count == 0);
  const PipeOutput release = chain.process(test_event(0xB0, 64, 0), 0);
  CHECK(release.count == kMaxPipeEvents);
  CHECK(release.dropped == 73 - kMaxPipeEvents);
  pass("output cap diagnostics");
}

// ---------------------------------------------------------------------------
// Chance/VelocityRandom stream: deterministic for a seed, rewound by a config
// change, and 0 maps to 1 so xorshift32 never locks at zero.
// ---------------------------------------------------------------------------
std::vector<bool> chancePattern(std::uint32_t seed, int events) {
  using namespace mnky::midifx;
  PipeChain chain = test_chain(PipeType::Chance, {50});
  chain.setSeed(seed);
  std::vector<bool> pattern;
  for (int i = 0; i < events; ++i)
    pattern.push_back(chain.process(test_event(0x90, 60, 100), 0).count == 1);
  return pattern;
}

void seedDeterminism() {
  // Chance clamps its percent to 0..100 and exposes the effective seed.
  CHECK(test_chain(PipeType::Chance, {1000})
            .process(test_event(0x90, 60, 100), 0)
            .count == 0);
  CHECK(test_chain(PipeType::Chance, {-30})
            .process(test_event(0x90, 60, 100), 0)
            .count == 1);
  {
    PipeChain seeded = test_chain(PipeType::Chance, {50});
    seeded.setSeed(12345);
    CHECK(seeded.seed() == 12345u);
    seeded.setSeed(0);
    CHECK(seeded.seed() == 1u);
    CHECK(seeded.seed() == 1u);
  }
  // Reconfiguring rewinds the stream to the seed.
  {
    const PipeConfig config[1] = {test_pipe(PipeType::Chance, {50})};
    PipeChain rewound;
    CHECK(rewound.configure(config, 1));
    rewound.setSeed(7);
    int firstPass = 0;
    for (int i = 0; i < 20; ++i)
      firstPass += rewound.process(test_event(0x90, 60, 100), 0).count;
    CHECK(rewound.configure(config, 1));
    int secondPass = 0;
    for (int i = 0; i < 20; ++i)
      secondPass += rewound.process(test_event(0x90, 60, 100), 0).count;
    CHECK(firstPass == secondPass);
  }
  const std::vector<bool> first = chancePattern(0x12345678u, 200);
  const std::vector<bool> again = chancePattern(0x12345678u, 200);
  const std::vector<bool> other = chancePattern(0x12345679u, 200);
  const std::vector<bool> zero = chancePattern(0, 200);
  const std::vector<bool> one = chancePattern(1, 200);
  CHECK(first == again);
  CHECK(first != other);
  CHECK(zero == one);  // setSeed(0) is documented to behave as seed 1
  pass("seed determinism");
}

// ---------------------------------------------------------------------------
// Generated schema defaults ARE the engine's missing-param behavior. Each case
// configures a pipe with no positional params and checks the default the
// generated table supplies (midifx/gen/generated/midifx_params.hpp).
// ---------------------------------------------------------------------------
void schemaDefaults() {
  using namespace mnky::midifx;
  static_assert(kMaxPipeParams >= generated::kMaxParamsPerPipe,
                "the 0x19 param capacity must cover the schema");
  CHECK(generated::kPipeCount == 16);

  CHECK(test_chain(PipeType::Filter, {}).process(test_event(0x90, 60, 100), 0).count == 1);
  CHECK(test_chain(PipeType::NoteRange, {})
            .process(test_event(0x90, 0, 1), 0)
            .count == 1);
  CHECK(test_chain(PipeType::CcRemap, {})
            .process(test_event(0xB0, 7, 9), 0)
            .events[0]
            .data[1] == 7);
  CHECK(test_chain(PipeType::ChannelRemap, {})
            .process(test_event(0x93, 60, 100), 0)
            .events[0]
            .data[0] == 0x90);
  CHECK(test_chain(PipeType::ScaleQuantize, {})
            .process(test_event(0x90, 61, 100), 0)
            .events[0]
            .data[1] == 61);
  CHECK(test_chain(PipeType::Chance, {})
            .process(test_event(0x90, 60, 100), 0)
            .count == 1);
  CHECK(test_chain(PipeType::VelocityRandom, {})
            .process(test_event(0x90, 60, 100), 0)
            .events[0]
            .data[2] == 100);
  CHECK(test_chain(PipeType::Sustain, {})
            .process(test_event(0x80, 60, 0), 0)
            .count == 1);
  CHECK(test_chain(PipeType::NoteLength, {})
            .process(test_event(0x80, 60, 0), 0)
            .count == 1);
  CHECK(test_chain(PipeType::Transform, {})
            .process(test_event(0x90, 60, 100), 0)
            .count == 1);
  CHECK(test_chain(PipeType::Harmonizer, {})
            .process(test_event(0x90, 60, 100), 0)
            .count == 1);
  PipeChain silentDelay = test_chain(PipeType::MidiDelay, {});
  CHECK(silentDelay.process(test_event(0x90, 60, 100), 0).count == 1);
  CHECK(run_ms_train(silentDelay, 1, 500).empty());

  // Dispatcher defaults are 2 channels from channel 1.
  {
    PipeChain dispatch = test_chain(PipeType::Dispatcher, {});
    CHECK((dispatch.process(test_event(0x90, 60, 100), 0).events[0].data[0] & 0x0F) == 0);
    CHECK((dispatch.process(test_event(0x90, 61, 100), 0).events[0].data[0] & 0x0F) == 1);
  }
  // Arp defaults: 1/4 division, up, 1 octave, swing 50, accent 0, gate 50.
  // One held note therefore plays on ticks 24 and 48 and gates off 12 ticks
  // after each step.
  {
    PipeChain arp = test_chain(PipeType::Arp, {});
    CHECK(arp.process(test_event(0x90, 60, 100), 0).count == 0);
    const auto events = run_tick_train(arp, 0, 48);
    CHECK(times_of(kind_events(events, 0x90)) ==
          (std::vector<std::uint32_t>{24, 48}));
    CHECK(times_of(kind_events(events, 0x80)) == (std::vector<std::uint32_t>{36}));
  }
  // Repeater defaults: 1/4, one repeat at full velocity, no falloff.
  {
    PipeChain repeater = test_chain(PipeType::Repeater, {});
    repeater.process(test_event(0x90, 60, 100), 0);
    const auto events = run_tick_train(repeater, 0, 48);
    CHECK(values_of(kind_events(events, 0x90)) == (std::vector<int>{100, 100}));
  }
  // CcLfo defaults: CC 1, sine, 1/4, depth 0 around center 64 -> flat CC 64.
  {
    PipeChain lfo = test_chain(PipeType::CcLfo, {});
    const auto ccs = kind_events(run_tick_train(lfo, 0, 24), 0xB0);
    CHECK(ccs.size() == 1 && ccs[0].data[1] == 1 && ccs[0].data[2] == 64);
  }
  pass("schema defaults");
}


// ---------------------------------------------------------------------------
// Pi parity extras: expectations the Pi suite had that the firmware suite did
// not spell out.
// ---------------------------------------------------------------------------
void piParityExtras() {
  using namespace mnky::midifx;
  // Velocity 0 on 0x90 is a note-off, so a note-on-only filter drops it.
  PipeChain noteOnly =
      test_chain(PipeType::Filter, {1, 0, 0, 0, 0, 0, 0, 0});
  CHECK(noteOnly.process(test_event(0x90, 60, 0), 0).count == 0);

  // Transform matches a 14-bit bend value when matchKind is BEND (3).
  PipeChain bendMatch = test_chain(PipeType::Transform, {3, 150, 150, 1, 74});
  CHECK(bendMatch.process(test_event(0xE0, 150 & 0x7F, 150 >> 7), 0).count == 1);
  const PipeOutput converted =
      bendMatch.process(test_event(0xE0, 150 & 0x7F, 150 >> 7), 0);
  CHECK(converted.count == 1 && (converted.events[0].data[0] & 0xF0) == 0xB0);
  CHECK(bendMatch.process(test_event(0xE0, 149 & 0x7F, 149 >> 7), 0).count == 1);
  CHECK((bendMatch.process(test_event(0xE0, 149 & 0x7F, 149 >> 7), 0)
             .events[0]
             .data[0] &
         0xF0) == 0xE0);

  // Swapped match window: 60..72 still matches its endpoints.
  PipeChain swapped = test_chain(PipeType::Transform, {0, 72, 60, 1, 7});
  CHECK((swapped.process(test_event(0x90, 60, 1), 0).events[0].data[0] & 0xF0) == 0xB0);
  CHECK((swapped.process(test_event(0x90, 72, 1), 0).events[0].data[0] & 0xF0) == 0xB0);
  CHECK((swapped.process(test_event(0x90, 59, 1), 0).events[0].data[0] & 0xF0) == 0x90);

  // A second note-on for the same key replaces the NoteLength deadline.
  {
    PipeChain pending = test_chain(PipeType::NoteLength, {50});
    CHECK(pending.process(test_note_on(0, 61, 100), 0).count == 1);
    CHECK(pending.process(test_note_on(0, 61, 100), 10).count == 1);
    CHECK(pending.tick(59).count == 0);
    CHECK(pending.tick(60).count == 1);
    CHECK(pending.process(test_note_off(0, 62), 70).count == 1);
    // lengthMs clamps to 60000.
    PipeChain longNote = test_chain(PipeType::NoteLength, {200000});
    CHECK(longNote.process(test_note_on(), 0).count == 1);
    CHECK(longNote.tick(59999).count == 0);
    CHECK(longNote.tick(60000).count == 1);
  }
  pass("pi parity extras");
}

}  // namespace

int main() {
  run_pipe_tests();
  run_wave2_pipe_tests();
  configureAndLimits();
  outputCapDiagnostics();
  seedDeterminism();
  schemaDefaults();
  channelScope();
  piParityExtras();
  if (g_failures != 0) {
    std::fprintf(stderr, "%d midifx core failure(s)\n", g_failures);
    return 1;
  }
  std::printf("PASS midifx core behavior\n");
  return 0;
}

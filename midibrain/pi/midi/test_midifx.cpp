// Host self-test for the portable MIDI FX Wave-1 pipe engine (midifx.hpp).
// No framework: a failing check prints FAIL and the process exits non-zero.
// Covers every Wave-1 pipe with boundary values, bypass, the Wave-2 rejection
// contract and the deterministic RNG, and prints one PASS line per behaviour.
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <string>
#include <vector>

#include "midifx.hpp"

namespace {

int g_failures = 0;

void check(bool ok, const char* what, const char* file, int line) {
  if (ok) return;
  std::fprintf(stderr, "FAIL %s (%s:%d)\n", what, file, line);
  ++g_failures;
}

#define CHECK(cond) check((cond), #cond, __FILE__, __LINE__)

void pass(const char* name) { std::printf("PASS %s\n", name); }

using mnky::midi::kMaxPipeEvents;
using mnky::midi::PipeChain;
using mnky::midi::PipeConfig;
using mnky::midi::PipeEvent;
using mnky::midi::PipeOutput;
using mnky::midi::PipeType;

PipeEvent event(std::uint8_t status, std::uint8_t d1 = 0, std::uint8_t d2 = 0,
                std::uint8_t size = 3) {
  PipeEvent out;
  out.size = size;
  out.data[0] = status;
  out.data[1] = d1;
  out.data[2] = d2;
  return out;
}

PipeConfig pipe(PipeType type, std::initializer_list<std::int32_t> params,
                bool bypass = false) {
  PipeConfig config;
  config.type = type;
  config.bypass = bypass;
  for (std::int32_t value : params) {
    if (config.paramCount >= config.params.size()) break;
    config.params[config.paramCount++] = value;
  }
  return config;
}

PipeChain chainWith(std::initializer_list<PipeConfig> pipes) {
  PipeChain chain;
  const std::vector<PipeConfig> config(pipes);
  CHECK(chain.configure(config.data(), config.size()));
  return chain;
}

PipeOutput run(PipeChain& chain, const PipeEvent& input, std::uint32_t nowMs = 1000) {
  return chain.process(input, nowMs);
}

bool matches(const PipeEvent& got, std::uint8_t status, std::uint8_t d1,
             std::uint8_t d2, std::uint8_t size) {
  return got.size == size && got.data[0] == status && got.data[1] == d1 &&
         got.data[2] == d2;
}

const PipeEvent noteOn(std::uint8_t channel = 0, std::uint8_t note = 60,
                       std::uint8_t velocity = 100) {
  return event(static_cast<std::uint8_t>(0x90 | channel), note, velocity);
}

const PipeEvent noteOff(std::uint8_t channel = 0, std::uint8_t note = 60) {
  return event(static_cast<std::uint8_t>(0x80 | channel), note, 0);
}

// ---------------------------------------------------------------------------
// Filter: one pass/block bit per message kind, other kinds always pass
// ---------------------------------------------------------------------------
void filterPipe() {
  PipeChain open = chainWith({pipe(PipeType::Filter, {})});
  const PipeEvent kinds[] = {
      noteOn(), noteOff(), event(0xB0, 1, 2), event(0xC0, 5, 0, 2),
      event(0xD0, 9, 0, 2), event(0xE0, 1, 2), event(0xF8, 0, 0, 1),
      event(0xF0, 0, 0, 1), event(0xF1, 0, 0, 1),
  };
  for (const PipeEvent& kind : kinds) {
    const PipeOutput out = run(open, kind);
    CHECK(out.count == 1 && !out.rejected);
    CHECK(matches(out.events[0], kind.data[0], kind.data[1], kind.data[2], kind.size));
  }

  // [noteOn,noteOff,cc,pc,press,bend,realtime,sysex]: only note-on passes.
  PipeChain noteOnly =
      chainWith({pipe(PipeType::Filter, {1, 0, 0, 0, 0, 0, 0, 0})});
  CHECK(run(noteOnly, noteOn()).count == 1);
  CHECK(run(noteOnly, noteOff()).count == 0);
  CHECK(run(noteOnly, event(0xB0, 1, 2)).count == 0);
  CHECK(run(noteOnly, event(0xC0, 5, 0, 2)).count == 0);
  CHECK(run(noteOnly, event(0xD0, 9, 0, 2)).count == 0);
  CHECK(run(noteOnly, event(0xE0, 1, 2)).count == 0);
  CHECK(run(noteOnly, event(0xF8, 0, 0, 1)).count == 0);
  CHECK(run(noteOnly, event(0xF0, 0, 0, 1)).count == 0);
  CHECK(run(noteOnly, event(0xF1, 0, 0, 1)).count == 1);  // uncovered: always pass

  // Velocity 0 on 0x90 is a note-off, so a note-on-only filter drops it.
  CHECK(run(noteOnly, noteOn(0, 60, 0)).count == 0);
  pass("filter");
}

// ---------------------------------------------------------------------------
// NoteRange: inclusive note window, velocity window on note-on only, swap/clamp
// ---------------------------------------------------------------------------
void noteRangePipe() {
  PipeChain range = chainWith({pipe(PipeType::NoteRange, {60, 72, 10, 127})});
  CHECK(run(range, noteOn(0, 60, 10)).count == 1);   // lo edge, velocity floor
  CHECK(run(range, noteOn(0, 72, 10)).count == 1);   // hi edge
  CHECK(run(range, noteOn(0, 59, 100)).count == 0);
  CHECK(run(range, noteOn(0, 73, 100)).count == 0);
  CHECK(run(range, noteOn(0, 64, 9)).count == 0);    // below velocity floor
  CHECK(run(range, noteOff(0, 64)).count == 1);      // release vel 0 still passes
  CHECK(run(range, event(0xB0, 1, 2)).count == 1);   // non-notes pass

  PipeChain swapped = chainWith({pipe(PipeType::NoteRange, {72, 60})});
  CHECK(run(swapped, noteOn(0, 60, 100)).count == 1);
  CHECK(run(swapped, noteOn(0, 72, 100)).count == 1);
  CHECK(run(swapped, noteOn(0, 59, 100)).count == 0);

  // Out-of-range params clamp to 0..127: 200..300 leaves only note 127.
  PipeChain clamped = chainWith({pipe(PipeType::NoteRange, {200, 300})});
  CHECK(run(clamped, noteOn(0, 127, 100)).count == 1);
  CHECK(run(clamped, noteOn(0, 126, 100)).count == 0);

  PipeChain defaults = chainWith({pipe(PipeType::NoteRange, {})});
  CHECK(run(defaults, noteOn(0, 0, 0)).count == 1);
  CHECK(run(defaults, noteOn(0, 127, 127)).count == 1);
  pass("note range");
}

// ---------------------------------------------------------------------------
// CcRemap: linear range map, endpoints, rounding, single-CC span, non-CC
// ---------------------------------------------------------------------------
void ccRemapPipe() {
  PipeChain identity = chainWith({pipe(PipeType::CcRemap, {})});
  const PipeOutput unchanged = run(identity, event(0xB0, 42, 99));
  CHECK(unchanged.count == 1 && matches(unchanged.events[0], 0xB0, 42, 99, 3));

  PipeChain full = chainWith({pipe(PipeType::CcRemap, {0, 127, 0, 127})});
  for (int cc = 0; cc < 128; ++cc) {
    const PipeOutput out =
        run(full, event(0xB0, static_cast<std::uint8_t>(cc), 1));
    CHECK(out.count == 1 && out.events[0].data[1] == cc);
  }

  PipeChain half = chainWith({pipe(PipeType::CcRemap, {0, 64, 0, 127})});
  const PipeOutput zero = run(half, event(0xB0, 0, 1));
  const PipeOutput mid = run(half, event(0xB0, 32, 1));
  const PipeOutput top = run(half, event(0xB0, 64, 1));
  CHECK(zero.events[0].data[1] == 0);
  CHECK(mid.events[0].data[1] == 64);   // 32/64 -> 63.5, half-up = 64
  CHECK(top.events[0].data[1] == 127);
  CHECK(run(half, event(0xB0, 65, 1)).events[0].data[1] == 65);  // outside: untouched

  PipeChain single = chainWith({pipe(PipeType::CcRemap, {7, 7, 64, 64})});
  CHECK(run(single, event(0xB0, 7, 1)).events[0].data[1] == 64);
  CHECK(run(single, event(0xB0, 8, 1)).events[0].data[1] == 8);

  PipeChain reversed = chainWith({pipe(PipeType::CcRemap, {64, 0, 127, 0})});
  CHECK(run(reversed, event(0xB0, 0, 1)).events[0].data[1] == 127);
  CHECK(run(reversed, event(0xB0, 64, 1)).events[0].data[1] == 0);

  // 14-bit and note messages are never CC-remapped, even with a matching byte.
  const PipeOutput note = run(full, noteOn(0, 42, 100));
  CHECK(matches(note.events[0], 0x90, 42, 100, 3));
  pass("cc remap");
}

// ---------------------------------------------------------------------------
// ChannelRemap: any/all channels, clamp, non-channel bytes untouched
// ---------------------------------------------------------------------------
void channelRemapPipe() {
  PipeChain anyToTwo = chainWith({pipe(PipeType::ChannelRemap, {0, 2})});
  for (int channel = 0; channel < 16; ++channel) {
    const PipeOutput out =
        run(anyToTwo, noteOn(static_cast<std::uint8_t>(channel), 60, 100));
    CHECK(out.count == 1 && (out.events[0].data[0] & 0x0F) == 1);
  }
  const PipeOutput cc = run(anyToTwo, event(0xB0, 7, 1));
  CHECK((cc.events[0].data[0] & 0x0F) == 1);

  PipeChain onlyThree = chainWith({pipe(PipeType::ChannelRemap, {3, 1})});
  CHECK((run(onlyThree, noteOn(2, 60, 100)).events[0].data[0] & 0x0F) == 0);
  CHECK((run(onlyThree, noteOn(1, 60, 100)).events[0].data[0] & 0x0F) == 1);

  // outCh clamps to 1..16; a 0 becomes channel 1, 17 becomes 16.
  PipeChain low = chainWith({pipe(PipeType::ChannelRemap, {0, 0})});
  CHECK((run(low, noteOn(5, 60, 100)).events[0].data[0] & 0x0F) == 0);
  PipeChain high = chainWith({pipe(PipeType::ChannelRemap, {0, 17})});
  CHECK((run(high, noteOn(5, 60, 100)).events[0].data[0] & 0x0F) == 15);

  const PipeOutput clock = run(anyToTwo, event(0xF8, 0, 0, 1));
  CHECK(matches(clock.events[0], 0xF8, 0, 0, 1));
  pass("channel remap");
}

// ---------------------------------------------------------------------------
// ScaleQuantize: nearest pitch class (down on tie), root, user mask, edges
// ---------------------------------------------------------------------------
void scaleQuantizePipe() {
  PipeChain cMajor = chainWith({pipe(PipeType::ScaleQuantize, {1, 0})});
  CHECK(run(cMajor, noteOn(0, 60, 100)).events[0].data[1] == 60);
  CHECK(run(cMajor, noteOn(0, 61, 100)).events[0].data[1] == 60);  // tie down
  CHECK(run(cMajor, noteOn(0, 62, 100)).events[0].data[1] == 62);
  CHECK(run(cMajor, noteOn(0, 63, 100)).events[0].data[1] == 62);  // tie down
  CHECK(run(cMajor, noteOn(0, 64, 100)).events[0].data[1] == 64);
  CHECK(run(cMajor, noteOn(0, 65, 100)).events[0].data[1] == 65);
  CHECK(run(cMajor, noteOn(0, 66, 100)).events[0].data[1] == 65);  // tie down

  // Root shifts the whole scale: D major keeps C# but pulls C down to B and
  // D# down to D (downward tie).
  PipeChain dMajor = chainWith({pipe(PipeType::ScaleQuantize, {1, 2})});
  CHECK(run(dMajor, noteOn(0, 61, 100)).events[0].data[1] == 61);
  CHECK(run(dMajor, noteOn(0, 60, 100)).events[0].data[1] == 59);
  CHECK(run(dMajor, noteOn(0, 63, 100)).events[0].data[1] == 62);

  // scale 0 / out-of-range scales pass unchanged.
  PipeChain off = chainWith({pipe(PipeType::ScaleQuantize, {0, 0})});
  CHECK(run(off, noteOn(0, 61, 100)).events[0].data[1] == 61);
  PipeChain invalid = chainWith({pipe(PipeType::ScaleQuantize, {9, 0})});
  CHECK(run(invalid, noteOn(0, 61, 100)).events[0].data[1] == 61);

  // User mask is absolute pitch classes (C only); root is ignored.
  PipeChain userC = chainWith({pipe(PipeType::ScaleQuantize, {6, 7, 0x001})});
  CHECK(run(userC, noteOn(0, 1, 100)).events[0].data[1] == 0);
  CHECK(run(userC, noteOn(0, 60, 100)).events[0].data[1] == 60);
  CHECK(run(userC, noteOn(0, 127, 100)).events[0].data[1] == 120);  // wraps down

  // An empty mask leaves every note alone rather than dropping it.
  PipeChain empty = chainWith({pipe(PipeType::ScaleQuantize, {6, 0, 0})});
  CHECK(run(empty, noteOn(0, 61, 100)).events[0].data[1] == 61);

  // Note-offs quantize too, so a held pair keeps matching pitch classes.
  CHECK(run(cMajor, noteOff(0, 61)).events[0].data[1] == 60);
  pass("scale quantize");
}

// ---------------------------------------------------------------------------
// Chance: drop note-ons only, clamps, deterministic per-route RNG
// ---------------------------------------------------------------------------
void chancePipe() {
  PipeChain off = chainWith({pipe(PipeType::Chance, {0})});
  CHECK(run(off, noteOn()).count == 1);
  PipeChain always = chainWith({pipe(PipeType::Chance, {100})});
  CHECK(run(always, noteOn()).count == 0);
  CHECK(run(always, noteOff()).count == 1);
  CHECK(run(always, event(0xB0, 1, 2)).count == 1);
  CHECK(run(always, noteOn(0, 60, 0)).count == 1);  // velocity 0 is a note-off

  PipeChain clampedHigh = chainWith({pipe(PipeType::Chance, {1000})});
  CHECK(run(clampedHigh, noteOn()).count == 0);
  PipeChain clampedLow = chainWith({pipe(PipeType::Chance, {-30})});
  CHECK(run(clampedLow, noteOn()).count == 1);

  // Same seed and config replay the same decisions; a different seed differs.
  {
    std::vector<PipeConfig> config{pipe(PipeType::Chance, {50})};
    PipeChain first;
    PipeChain second;
    PipeChain other;
    CHECK(first.configure(config.data(), config.size()));
    CHECK(second.configure(config.data(), config.size()));
    CHECK(other.configure(config.data(), config.size()));
    first.setSeed(12345);
    second.setSeed(12345);
    other.setSeed(999);
    CHECK(first.seed() == 12345);
    int keptFirst = 0, keptSecond = 0, keptOther = 0;
    bool differs = false;
    for (int i = 0; i < 200; ++i) {
      const int a = run(first, noteOn()).count;
      const int b = run(second, noteOn()).count;
      const int c = run(other, noteOn()).count;
      keptFirst += a;
      keptSecond += b;
      keptOther += c;
      if (a != c) differs = true;
    }
    CHECK(keptFirst == keptSecond);
    CHECK(keptFirst > 0 && keptFirst < 200);
    CHECK(differs);
    CHECK(keptOther > 0 && keptOther < 200);
  }

  // Reconfiguring rewinds the stream to the seed.
  {
    std::vector<PipeConfig> config{pipe(PipeType::Chance, {50, 0})};
    PipeChain chain;
    CHECK(chain.configure(config.data(), config.size()));
    chain.setSeed(7);
    int firstPass = 0;
    for (int i = 0; i < 20; ++i) firstPass += run(chain, noteOn()).count;
    CHECK(chain.configure(config.data(), config.size()));
    int secondPass = 0;
    for (int i = 0; i < 20; ++i) secondPass += run(chain, noteOn()).count;
    CHECK(firstPass == secondPass);
  }
  PipeChain zeroSeed = chainWith({pipe(PipeType::Chance, {50})});
  zeroSeed.setSeed(0);
  CHECK(zeroSeed.seed() == 1);
  pass("chance");
}

// ---------------------------------------------------------------------------
// VelocityRandom: amount window, floor, note-on only, deterministic
// ---------------------------------------------------------------------------
void velocityRandomPipe() {
  PipeChain off = chainWith({pipe(PipeType::VelocityRandom, {0, 0})});
  CHECK(run(off, noteOn(0, 60, 100)).events[0].data[2] == 100);

  PipeChain wide = chainWith({pipe(PipeType::VelocityRandom, {127, 0})});
  wide.setSeed(42);
  bool sawDelta = false;
  for (int i = 0; i < 64; ++i) {
    const PipeOutput out = run(wide, noteOn(0, 60, 100));
    CHECK(out.count == 1);
    if (out.events[0].data[2] != 100) sawDelta = true;
    CHECK(out.events[0].data[2] <= 127);
  }
  CHECK(sawDelta);

  PipeChain floored = chainWith({pipe(PipeType::VelocityRandom, {127, 64})});
  floored.setSeed(42);
  for (int i = 0; i < 64; ++i) {
    const PipeOutput out = run(floored, noteOn(0, 60, 10));
    CHECK(out.events[0].data[2] >= 64 && out.events[0].data[2] <= 127);
  }

  // amount clamps to 0..127: a huge amount still lands in 0..127.
  PipeChain clamped = chainWith({pipe(PipeType::VelocityRandom, {999, -5})});
  clamped.setSeed(3);
  for (int i = 0; i < 64; ++i) {
    const PipeOutput out = run(clamped, noteOn(0, 60, 64));
    CHECK(out.events[0].data[2] <= 127);
  }

  // Note-offs and CCs pass unchanged.
  CHECK(run(wide, noteOff(0, 60)).events[0].data[2] == 0);
  CHECK(run(wide, event(0xB0, 7, 80)).events[0].data[2] == 80);

  // Same seed replays the stream.
  PipeChain a = chainWith({pipe(PipeType::VelocityRandom, {20, 0})});
  PipeChain b = chainWith({pipe(PipeType::VelocityRandom, {20, 0})});
  a.setSeed(2024);
  b.setSeed(2024);
  for (int i = 0; i < 32; ++i) {
    const PipeOutput outA = run(a, noteOn(0, 64, 90));
    const PipeOutput outB = run(b, noteOn(0, 64, 90));
    CHECK(outA.events[0].data[2] == outB.events[0].data[2]);
  }
  pass("velocity random");
}

// ---------------------------------------------------------------------------
// Dispatcher: round-robin note spread, note-off follows its note-on channel
// ---------------------------------------------------------------------------
void dispatcherPipe() {
  PipeChain two = chainWith({pipe(PipeType::Dispatcher, {2, 1})});
  const PipeOutput first = run(two, noteOn(0, 60, 100));
  const PipeOutput second = run(two, noteOn(0, 61, 100));
  const PipeOutput third = run(two, noteOn(0, 62, 100));
  CHECK((first.events[0].data[0] & 0x0F) == 0);
  CHECK((second.events[0].data[0] & 0x0F) == 1);
  CHECK((third.events[0].data[0] & 0x0F) == 0);
  // Each off returns on the channel its note-on was assigned.
  CHECK((run(two, noteOff(0, 60)).events[0].data[0] & 0x0F) == 0);
  CHECK((run(two, noteOff(0, 61)).events[0].data[0] & 0x0F) == 1);
  CHECK((run(two, noteOff(0, 61)).events[0].data[0] & 0x0F) == 0);  // unassigned

  // numCh clamps to 2..16; startCh 16 wraps to 1 on the second slot.
  PipeChain clamped = chainWith({pipe(PipeType::Dispatcher, {1, 16})});
  CHECK((run(clamped, noteOn(0, 70, 100)).events[0].data[0] & 0x0F) == 15);
  CHECK((run(clamped, noteOn(0, 71, 100)).events[0].data[0] & 0x0F) == 0);

  // Channels are keyed per input channel.
  PipeChain twoCh = chainWith({pipe(PipeType::Dispatcher, {2, 1})});
  CHECK((run(twoCh, noteOn(0, 60, 100)).events[0].data[0] & 0x0F) == 0);
  CHECK((run(twoCh, noteOn(1, 60, 100)).events[0].data[0] & 0x0F) == 1);

  const PipeOutput cc = run(twoCh, event(0xB0, 7, 1));
  CHECK((cc.events[0].data[0] & 0x0F) == 0);
  pass("dispatcher");
}

// ---------------------------------------------------------------------------
// Sustain: latch holds note-offs, CC64 release flushes them
// ---------------------------------------------------------------------------
void sustainPipe() {
  PipeChain off = chainWith({pipe(PipeType::Sustain, {0})});
  CHECK(run(off, noteOff(0, 60)).count == 1);

  PipeChain latch = chainWith({pipe(PipeType::Sustain, {1})});
  CHECK(run(latch, noteOn(0, 60, 100)).count == 1);
  const PipeOutput ccOn = run(latch, event(0xB0, 64, 127));
  CHECK(ccOn.count == 1 && matches(ccOn.events[0], 0xB0, 64, 127, 3));
  CHECK(run(latch, noteOff(0, 60)).count == 0);  // held
  CHECK(run(latch, noteOff(0, 64)).count == 0);  // held
  const PipeOutput release = run(latch, event(0xB0, 64, 0));
  CHECK(release.count == 3);  // two offs, then the CC that caused them
  CHECK(matches(release.events[0], 0x80, 60, 0, 3));
  CHECK(matches(release.events[1], 0x80, 64, 0, 3));
  CHECK(matches(release.events[2], 0xB0, 64, 0, 3));
  CHECK(run(latch, noteOff(0, 60)).count == 1);  // unlatched again

  // A note-off for a non-held note is not duplicated on release, and the latch
  // is per channel.
  CHECK(run(latch, event(0xB0, 64, 127)).count == 1);
  CHECK(run(latch, noteOff(1, 72)).count == 0);
  const PipeOutput channelRelease = run(latch, event(0xB0, 64, 0));
  CHECK(channelRelease.count == 2);
  CHECK(matches(channelRelease.events[0], 0x81, 72, 0, 3));

  // The 64-event output cap is counted, not silently padded.
  PipeChain burst = chainWith({pipe(PipeType::Sustain, {1})});
  CHECK(run(burst, event(0xB0, 64, 127)).count == 1);
  for (int channel = 0; channel < 16; ++channel)
    for (int note = 0; note < 8; ++note)
      CHECK(run(burst, noteOff(static_cast<std::uint8_t>(channel),
                               static_cast<std::uint8_t>(note)))
                .count == 0);
  const PipeOutput capped = run(burst, event(0xB0, 64, 0));
  CHECK(capped.count == kMaxPipeEvents);
  CHECK(capped.dropped == 16 * 8 + 1 - kMaxPipeEvents);
  pass("sustain");
}

// ---------------------------------------------------------------------------
// NoteLength: fixed length, incoming off suppressed, tick after the pipe
// ---------------------------------------------------------------------------
void noteLengthPipe() {
  PipeChain zero = chainWith({pipe(PipeType::NoteLength, {0})});
  CHECK(run(zero, noteOn()).count == 1);
  CHECK(run(zero, noteOff()).count == 1);

  PipeChain fixed = chainWith(
      {pipe(PipeType::NoteLength, {100}), pipe(PipeType::ChannelRemap, {0, 5})});
  const PipeOutput on = run(fixed, noteOn(0, 60, 100), 1000);
  CHECK(on.count == 1 && (on.events[0].data[0] & 0x0F) == 4);  // remapped
  CHECK(run(fixed, noteOff(0, 60), 1050).count == 0);         // suppressed
  CHECK(fixed.tick(1099).count == 0);
  const PipeOutput due = fixed.tick(1100);
  CHECK(due.count == 1 && (due.events[0].data[0] & 0x0F) == 4);
  CHECK(matches(due.events[0], 0x84, 60, 0, 3));
  CHECK(fixed.tick(1101).count == 0);  // emitted once

  // A second note-on for the same key replaces the deadline; a note-off for a
  // key with no pending off passes.
  PipeChain pending = chainWith({pipe(PipeType::NoteLength, {50})});
  CHECK(run(pending, noteOn(0, 61, 100), 0).count == 1);
  CHECK(run(pending, noteOn(0, 61, 100), 10).count == 1);
  CHECK(pending.tick(59).count == 0);
  CHECK(pending.tick(60).count == 1);
  CHECK(run(pending, noteOff(0, 62), 70).count == 1);

  // lengthMs clamps to 60000.
  PipeChain longNote = chainWith({pipe(PipeType::NoteLength, {200000})});
  CHECK(run(longNote, noteOn(), 0).count == 1);
  CHECK(longNote.tick(59999).count == 0);
  CHECK(longNote.tick(60000).count == 1);
  pass("note length");
}

// ---------------------------------------------------------------------------
// Transform: kind matching and conversion in both directions
// ---------------------------------------------------------------------------
void transformPipe() {
  // note -> CC, keeping note velocity as CC value.
  PipeChain noteToCc = chainWith({pipe(PipeType::Transform, {0, 60, 60, 1, 7})});
  const PipeOutput cc = run(noteToCc, noteOn(0, 60, 99));
  CHECK(cc.count == 1 && matches(cc.events[0], 0xB0, 7, 99, 3));
  CHECK(run(noteToCc, noteOn(0, 61, 99)).count == 1);  // no match: unchanged
  CHECK(matches(run(noteToCc, noteOn(0, 61, 99)).events[0], 0x90, 61, 99, 3));

  // CC -> note (sources that are not notes use velocity 100).
  PipeChain ccToNote = chainWith({pipe(PipeType::Transform, {1, 7, 7, 0, 64})});
  CHECK(matches(run(ccToNote, event(0xB0, 7, 33)).events[0], 0x90, 64, 100, 3));

  // PC -> PC.
  PipeChain pcToPc = chainWith({pipe(PipeType::Transform, {2, 5, 5, 2, 9})});
  CHECK(matches(run(pcToPc, event(0xC0, 5, 0, 2)).events[0], 0xC0, 9, 0, 2));

  // Bend is a 14-bit match and output; 8192 -> LSB 0, MSB 64.
  PipeChain bend = chainWith({pipe(PipeType::Transform, {3, 0, 16383, 3, 8192})});
  const PipeOutput bendOut = run(bend, event(0xE0, 0, 64));
  CHECK(bendOut.count == 1 && matches(bendOut.events[0], 0xE0, 0, 64, 3));

  // Pressure -> PC, 2-byte output.
  PipeChain pressure = chainWith({pipe(PipeType::Transform, {4, 0, 127, 2, 5})});
  CHECK(matches(run(pressure, event(0xD0, 20, 0, 2)).events[0], 0xC0, 5, 0, 2));

  // A partial config (fewer than 4 params) is not a conversion.
  PipeChain partial = chainWith({pipe(PipeType::Transform, {0, 60, 60})});
  CHECK(matches(run(partial, noteOn(0, 60, 99)).events[0], 0x90, 60, 99, 3));

  // Swapped match window: 60..72 still matches 60 and 72.
  PipeChain swapped = chainWith({pipe(PipeType::Transform, {0, 72, 60, 1, 7})});
  CHECK(run(swapped, noteOn(0, 60, 1)).events[0].data[0] == 0xB0);
  CHECK(run(swapped, noteOn(0, 72, 1)).events[0].data[0] == 0xB0);
  CHECK(run(swapped, noteOn(0, 59, 1)).events[0].data[0] == 0x90);
  pass("transform");
}

// ---------------------------------------------------------------------------
// Bypass, chain order, wire limits and configure rejection
// ---------------------------------------------------------------------------
void bypassAndLimits() {
  PipeChain bypassed =
      chainWith({pipe(PipeType::Filter, {0, 0, 0, 0, 0, 0, 0, 0}, true)});
  CHECK(run(bypassed, noteOn()).count == 1);
  CHECK(run(bypassed, event(0xB0, 1, 2)).count == 1);

  // Order matters: a ChannelRemap before a Filter changes nothing about the
  // filter; a Filter before a Transform can starve it.
  PipeChain ordered =
      chainWith({pipe(PipeType::ChannelRemap, {0, 2}),
                 pipe(PipeType::Transform, {0, 60, 60, 1, 7})});
  CHECK(matches(run(ordered, noteOn(0, 60, 99)).events[0], 0xB1, 7, 99, 3));

  // Max chain length is 8; 9 is rejected and leaves the chain untouched.
  {
    std::vector<PipeConfig> eight;
    for (int i = 0; i < 8; ++i) eight.push_back(pipe(PipeType::Filter, {}));
    PipeChain chain;
    CHECK(chain.configure(eight.data(), eight.size()));
    CHECK(chain.pipeCount() == 8);
    eight.push_back(pipe(PipeType::Filter, {}));
    CHECK(!chain.configure(eight.data(), eight.size()));
    CHECK(chain.pipeCount() == 8);
  }

  // Invalid type ids and oversized parameter lists are rejected.
  {
    std::vector<PipeConfig> config{pipe(PipeType::Filter, {})};
    config[0].type = static_cast<PipeType>(0);
    PipeChain chain;
    CHECK(!chain.configure(config.data(), config.size()));
    CHECK(chain.empty());
    config[0].type = static_cast<PipeType>(17);
    CHECK(!chain.configure(config.data(), config.size()));
    config[0] = pipe(PipeType::Filter, {});
    config[0].paramCount = 11;
    CHECK(!chain.configure(config.data(), config.size()));
    CHECK(!chain.configure(nullptr, 1));
    CHECK(chain.configure(nullptr, 0));  // empty chain is valid
  }

  // Empty chain passes valid messages and rejects malformed sizes silently.
  PipeChain empty;
  CHECK(run(empty, noteOn()).count == 1);
  PipeEvent bad;
  bad.size = 0;
  CHECK(run(empty, bad).count == 0);
  bad.size = 4;
  CHECK(run(empty, bad).count == 0);
  pass("bypass, order and limits");
}

// ---------------------------------------------------------------------------
// Wave 2: configure tolerated, evaluation rejected with a reason
// ---------------------------------------------------------------------------
void wave2Rejection() {
  struct Case {
    PipeType type;
    const char* text;
  };
  const Case cases[] = {
      {PipeType::Arp, "Arp"},       {PipeType::Repeater, "Repeater"},
      {PipeType::CcLfo, "CcLfo"},   {PipeType::Harmonizer, "Harmonizer"},
      {PipeType::MidiDelay, "MidiDelay"},
  };
  for (const Case& item : cases) {
    CHECK(mnky::midi::wave2Reason(item.type) != nullptr);
    PipeChain chain = chainWith({pipe(PipeType::Filter, {}), pipe(item.type, {})});
    CHECK(chain.wave2Pipe() == 1);
    CHECK(chain.pipeCount() == 2);
    const PipeOutput out = run(chain, noteOn());
    CHECK(out.rejected && out.count == 0);
    CHECK(out.rejectPipe == 1);
    CHECK(out.reason != nullptr);
    if (out.reason != nullptr) CHECK(std::string(out.reason).find("Wave-2") != std::string::npos);
    CHECK(chain.tick(5000).rejected);
  }
  CHECK(mnky::midi::wave2Reason(PipeType::Filter) == nullptr);
  CHECK(mnky::midi::wave2Reason(PipeType::Transform) == nullptr);

  // A bypassed Wave-2 pipe is ignored: the Wave-1 chain still runs.
  PipeChain bypassed =
      chainWith({pipe(PipeType::Arp, {}, true), pipe(PipeType::Chance, {100})});
  CHECK(bypassed.wave2Pipe() == -1);
  CHECK(run(bypassed, noteOn()).count == 0);

  // Bypass toggling is part of the config, so reconfiguring re-evaluates the
  // rejection state.
  std::vector<PipeConfig> config{pipe(PipeType::Arp, {}, true)};
  PipeChain chain;
  CHECK(chain.configure(config.data(), config.size()));
  CHECK(chain.wave2Pipe() == -1);
  config[0].bypass = false;
  CHECK(chain.configure(config.data(), config.size()));
  CHECK(chain.wave2Pipe() == 0);
  pass("wave 2 rejection");
}

}  // namespace

int main() {
  filterPipe();
  noteRangePipe();
  ccRemapPipe();
  channelRemapPipe();
  scaleQuantizePipe();
  chancePipe();
  velocityRandomPipe();
  dispatcherPipe();
  sustainPipe();
  noteLengthPipe();
  transformPipe();
  bypassAndLimits();
  wave2Rejection();

  if (g_failures != 0) {
    std::fprintf(stderr, "FAILURES %d\n", g_failures);
    return 1;
  }
  std::printf("PASS test_midifx\n");
  return 0;
}

#pragma once
// Timing math, wave tables and the bounded note/queue state one clocked pipe
// position needs. Shared by the Pico firmware and the Pi core (see
// midifx/core/midifx.hpp for the engine and the clock contract).
//
// The engine owns no clock. A clocked pipe is driven by an absolute tick index
// on a 24-PPQN grid plus a monotonic millisecond timestamp, both supplied by
// the caller: the Pico feeds its tick alarm counter, a Pi-side caller feeds the
// tick derived from its disciplined USB-clock phase. A clocked division is
// therefore exactly 24 >> div ticks (1/4 = 24, 1/8 = 12, 1/16 = 6, 1/32 = 3),
// and swing, gates and repeat spacing quantise to whole ticks. Only MidiDelay
// counts milliseconds, because its parameter is delayMs.
#include <array>
#include <cstddef>
#include <cstdint>

namespace mnky::midifx {

inline constexpr std::uint32_t kTicksPerQuarter = 24;
// One catch-up call replays at most this many missed division steps; a longer
// backlog (main-loop stall, transport START resetting the tick counter) resyncs
// to the current grid instead of firing a burst of stale notes.
inline constexpr std::size_t kMaxStepsPerCall = 8;
// Held notes tracked per pipe (a route's own input, one channel or several).
inline constexpr std::size_t kMaxHeldNotes = 16;
// Notes a pipe may have sounding at once: an arp gate, a repeater chord or the
// delayed note-ons of a MidiDelay tail.
inline constexpr std::size_t kMaxSoundingNotes = 16;
// Arp pattern length cap: 4 octaves x 8 held notes; the lowest entries win.
inline constexpr std::size_t kMaxArpPattern = 32;
inline constexpr std::size_t kMaxHarmonyVoices = 4;  // the session's i1..i4
inline constexpr std::size_t kMaxHarmonyEntries = 24;
// Queued MidiDelay input events; one slot carries a whole repeat chain, so this
// bounds simultaneous delayed events, not events x repeats.
inline constexpr std::size_t kMaxDelayEvents = 32;
inline constexpr std::uint32_t kMaxDelayMs = 2000;
inline constexpr int kMaxDelayRepeats = 8;
// Repeater retriggers per burst.
inline constexpr int kMaxRepeatCount = 8;
// LFO cycle length in division steps. The rate follows the division: at
// 120 bpm 1/16 gives 1 Hz, 1/32 2 Hz, 1/8 0.5 Hz and 1/4 0.25 Hz, and two LFO
// pipes on the same division stay in phase because the phase is the absolute
// step index.
inline constexpr std::size_t kLfoStepsPerCycle = 8;

constexpr int clampInt(int value, int lo, int hi) {
  return value < lo ? lo : (value > hi ? hi : value);
}

// 24 >> div ticks per step. Out-of-range divisions clamp: a corrupt RAM value
// must never stall a pipe.
constexpr std::uint32_t divTicks(int div) {
  return kTicksPerQuarter >> static_cast<std::uint32_t>(clampInt(div, 0, 3));
}

// Absolute grid index of `tick` for a division of `stepTicks`.
constexpr std::uint32_t stepIndex(std::uint32_t tick, std::uint32_t stepTicks) {
  return stepTicks ? tick / stepTicks : 0;
}

// swing 50 is straight. Above 50 every odd step is delayed by (swing - 50) % of
// the step, below 50 it plays early; whole ticks, because 24 PPQN is the finest
// clock a 24-PPQN caller has. A step pair is then stepTicks + swingTicks apart
// and the next pair restarts on the grid.
constexpr std::int32_t swingTicks(int swing, std::uint32_t stepTicks) {
  return (clampInt(swing, 0, 100) - 50) *
         static_cast<std::int32_t>(stepTicks) / 100;
}

// Absolute tick of grid step `step`; odd steps carry the swing offset.
constexpr std::int64_t stepDueTick(std::uint32_t step, std::uint32_t stepTicks,
                                   std::int32_t swing) {
  return static_cast<std::int64_t>(step) * static_cast<std::int64_t>(stepTicks) +
         ((step & 1u) ? swing : 0);
}

// Gate length in ticks: at least one tick and never longer than the step.
constexpr std::uint32_t gateTicks(int gatePercent, std::uint32_t stepTicks) {
  const std::uint32_t gate =
      stepTicks * static_cast<std::uint32_t>(clampInt(gatePercent, 1, 100)) / 100u;
  return gate ? gate : 1u;
}

// (100 - falloff) % of value, truncated. Callers that emit note-ons clamp the
// result to >= 1 themselves: a 0 velocity note-on is a note-off on the wire.
constexpr std::uint8_t attenuate(std::uint8_t value, int falloff) {
  return static_cast<std::uint8_t>(static_cast<int>(value) *
                                   (100 - clampInt(falloff, 0, 100)) / 100);
}

// Eight samples per cycle, -127..+127: round(127 * sin(2*pi*k/8)) for k = 0..7.
inline constexpr std::array<std::int8_t, kLfoStepsPerCycle> kLfoSine{
    0, 90, 127, 90, 0, -90, -127, -90};
inline constexpr std::array<std::int8_t, kLfoStepsPerCycle> kLfoTriangle{
    0, 64, 127, 64, 0, -64, -127, -64};
inline constexpr std::array<std::int8_t, kLfoStepsPerCycle> kLfoSquare{
    127, 127, 127, 127, -127, -127, -127, -127};
inline constexpr std::array<std::int8_t, kLfoStepsPerCycle> kLfoSaw{
    -127, -95, -64, -32, 0, 32, 64, 95};

// wave 0 sine, 1 triangle, 2 square, 3 saw (the session's CcLfo order).
constexpr std::int32_t lfoShape(int wave, std::size_t phase) {
  const std::size_t index = phase % kLfoStepsPerCycle;
  switch (wave) {
    case 1:
      return kLfoTriangle[index];
    case 2:
      return kLfoSquare[index];
    case 3:
      return kLfoSaw[index];
    default:
      return kLfoSine[index];
  }
}

// center + depth * shape / 127, truncated toward zero and clamped to the CC
// range. depth 0 holds the centre; depth 127 reaches the shape extremes.
constexpr std::uint8_t lfoValue(int wave, std::size_t phase, int depth,
                                int center) {
  const std::int32_t scaled = static_cast<std::int32_t>(clampInt(depth, 0, 127)) *
                              lfoShape(wave, phase) / 127;
  return static_cast<std::uint8_t>(
      clampInt(clampInt(center, 0, 127) + static_cast<int>(scaled), 0, 127));
}

// One note a pipe holds or sounds. `channel` is 0..15 (the wire nibble).
struct PipeNote {
  std::uint8_t channel = 0;
  std::uint8_t note = 0;
  std::uint8_t velocity = 0;
};

// Fixed-capacity note set kept sorted by (note, channel). The order is what
// makes an arp pattern deterministic; a duplicate (channel, note) never stacks.
template <std::size_t Capacity>
class PipeNoteSet {
 public:
  std::size_t count() const { return count_; }
  const PipeNote* data() const { return notes_.data(); }
  const PipeNote& at(std::size_t index) const { return notes_[index]; }

  void clear() { count_ = 0; }

  bool contains(std::uint8_t channel, std::uint8_t note) const {
    for (std::size_t i = 0; i < count_; ++i)
      if (notes_[i].channel == channel && notes_[i].note == note) return true;
    return false;
  }

  // Sorted insert; an already present (channel, note) is left as it is and
  // reports success, a full set reports failure without changing anything.
  bool add(const PipeNote& note) {
    std::size_t pos = 0;
    while (pos < count_ && before(notes_[pos], note)) ++pos;
    if (pos < count_ && notes_[pos].channel == note.channel &&
        notes_[pos].note == note.note)
      return true;
    if (count_ >= Capacity) return false;
    for (std::size_t i = count_; i > pos; --i) notes_[i] = notes_[i - 1];
    notes_[pos] = note;
    ++count_;
    return true;
  }

  bool remove(std::uint8_t channel, std::uint8_t note) {
    for (std::size_t i = 0; i < count_; ++i) {
      if (notes_[i].channel != channel || notes_[i].note != note) continue;
      for (std::size_t j = i + 1; j < count_; ++j) notes_[j - 1] = notes_[j];
      --count_;
      return true;
    }
    return false;
  }

 private:
  static bool before(const PipeNote& a, const PipeNote& b) {
    if (a.note != b.note) return a.note < b.note;
    return a.channel < b.channel;
  }

  std::array<PipeNote, Capacity> notes_{};
  std::size_t count_ = 0;
};

// Ordered arp pattern for the held set `held[0..heldCount)`. `octaves` 1..4
// stacks the same notes 12 semitones higher; notes above 127 are dropped and the
// list stops at kMaxArpPattern entries (lowest first). type 0 up, 1 down,
// 2 updown (the down leg repeats neither endpoint, so 60 64 67 gives
// 60 64 67 64), 3 random (list order; the caller picks the index) and 4 chord
// (base octave only, every note at once, `octaves` ignored).
inline std::size_t buildArpPattern(const PipeNote* held, std::size_t heldCount,
                                   int type, int octaves,
                                   std::array<PipeNote, kMaxArpPattern>& out) {
  const int octaveRange = clampInt(octaves, 1, 4);
  std::array<PipeNote, kMaxArpPattern> up{};
  std::size_t upCount = 0;
  const int octavesUsed = type == 4 ? 1 : octaveRange;
  for (int octave = 0; octave < octavesUsed; ++octave) {
    for (std::size_t i = 0; i < heldCount && upCount < kMaxArpPattern; ++i) {
      const int note = static_cast<int>(held[i].note) + 12 * octave;
      if (note > 127) continue;
      up[upCount] = held[i];
      up[upCount].note = static_cast<std::uint8_t>(note);
      ++upCount;
    }
  }
  if (type == 1) {  // down
    for (std::size_t i = 0; i < upCount; ++i) out[i] = up[upCount - 1 - i];
    return upCount;
  }
  for (std::size_t i = 0; i < upCount; ++i) out[i] = up[i];
  if (type != 2 || upCount < 3) return upCount;
  std::size_t size = upCount;
  for (std::size_t i = upCount - 1; i > 0 && size < kMaxArpPattern; --i)
    out[size++] = up[i - 1];
  return size;
}

// Output notes for one harmonized input: out[0] is the input note itself,
// followed by one entry per distinct offset. Offset 0, duplicates and notes
// outside 0..127 are skipped, so every output note is unique and needs exactly
// one matching note-off.
inline std::size_t harmonyOutputs(
    std::uint8_t note, int voices,
    const std::array<int, kMaxHarmonyVoices>& offsets,
    std::array<std::uint8_t, kMaxHarmonyVoices + 1>& out) {
  const int count = clampInt(voices, 0, static_cast<int>(kMaxHarmonyVoices));
  std::size_t size = 0;
  out[size++] = note;
  for (int i = 0; i < count; ++i) {
    const int shifted =
        static_cast<int>(note) + offsets[static_cast<std::size_t>(i)];
    if (shifted < 0 || shifted > 127) continue;
    if (shifted == note) continue;  // the input note is already out[0]
    bool duplicate = false;
    for (std::size_t k = 0; k < size; ++k)
      duplicate = duplicate || out[k] == static_cast<std::uint8_t>(shifted);
    if (duplicate) continue;
    out[size++] = static_cast<std::uint8_t>(shifted);
  }
  return size;
}

// One queued MidiDelay echo. `echoesLeft` counts this echo and the ones that
// follow, so a slot carries a whole repeat chain and the queue is bounded by
// simultaneous input events instead of events x repeats. `data` holds the
// already attenuated bytes, so a re-queue only attenuates once more.
struct DelayEcho {
  std::uint32_t dueMs = 0;
  std::uint8_t size = 0;
  std::uint8_t data[3]{};
  std::uint8_t echoesLeft = 0;
  bool used = false;
};

// Runtime state of one clocked pipe position in a chain. A chain keeps one of
// these per pipe index, so two pipes of the same type never share state.
struct ClockedPipeState {
  PipeNoteSet<kMaxHeldNotes> held;
  // Notes this pipe turned on and still owes a note-off: arp/repeater gates,
  // delay echoes and harmonized voices.
  PipeNoteSet<kMaxSoundingNotes> sounding;
  bool stepping = false;     // the division grid is running
  std::uint32_t step = 0;    // absolute grid index of the next step
  std::uint32_t cursor = 0;  // arp pattern cursor
  std::uint32_t burst = 0;   // repeater strike index since the hold started
  std::uint32_t gateTick = 0;
  bool gatePending = false;

  // Harmonizer refcounts: one entry per sounding output note, `count` is the
  // number of held input notes that map onto it (so overlapping voices release
  // in the right order).
  struct HarmonyEntry {
    std::uint8_t channel = 0;
    std::uint8_t note = 0;
    std::uint8_t count = 0;
    bool used = false;
  };
  std::array<HarmonyEntry, kMaxHarmonyEntries> harmony{};

  // Set when a chain edit took the runtime state away while notes were on; the
  // chain releases them on its next tick instead of leaving them sounding.
  bool orphaned = false;
};

}  // namespace mnky::midifx

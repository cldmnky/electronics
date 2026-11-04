#pragma once
// Wave-2 clocked pipe helpers for the mnkybox Pico firmware: timing math, wave
// tables, note sets and the runtime state one clocked pipe position needs.
// Pure logic: no Pico SDK, host-tested in protocol_test.cpp. midi_pipe.hpp owns
// the chain plumbing (dispatch, re-injection, bypass); everything here can be
// checked without a chain.
//
// The Pico tick alarm is the clock master (main.cpp clock_tick, 24 ticks per
// quarter note), so a clocked division is exactly 24 >> div ticks: 1/4 = 24,
// 1/8 = 12, 1/16 = 6, 1/32 = 3. Every scheduled event is therefore a tick
// index, never wall-clock time; only MidiDelay counts milliseconds, because its
// parameter is delayMs. Two consequences: swing, gates and repeat spacing are
// quantised to whole ticks (a 1/32 step is 3 ticks, so a 50 % gate is 1 tick),
// and no pipe needs an external timing source.
#include <array>
#include <cstddef>
#include <cstdint>

namespace mb {
inline constexpr std::uint32_t kPipeTicksPerQuarter = 24;
// One catch-up call replays at most this many missed division steps; a longer
// backlog (main-loop stall, transport START resetting clock_ticks) resyncs to
// the current grid instead of firing a burst of stale notes.
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

inline int pipeClamp(int v, int lo, int hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

// 24 >> div ticks per step. Out-of-range divisions clamp: a corrupt RAM value
// must never stall a pipe.
inline std::uint32_t pipeDivTicks(int div) {
  return kPipeTicksPerQuarter >> static_cast<std::uint32_t>(pipeClamp(div, 0, 3));
}

// Absolute grid index of `tick` for a division of `step_ticks`.
inline std::uint32_t pipeStepIndex(std::uint32_t tick, std::uint32_t step_ticks) {
  return step_ticks ? tick / step_ticks : 0;
}

// swing 50 is straight. Above 50 every odd step is delayed by (swing - 50) % of
// the step, below 50 it plays early; whole ticks, because 24 PPQN is the finest
// clock the Pico has. A step pair is then step_ticks + swing_ticks apart and the
// next pair restarts on the grid.
inline std::int32_t pipeSwingTicks(int swing, std::uint32_t step_ticks) {
  return (pipeClamp(swing, 0, 100) - 50) *
         static_cast<std::int32_t>(step_ticks) / 100;
}

// Absolute tick of grid step `step`; odd steps carry the swing offset.
inline std::int64_t pipeStepDueTick(std::uint32_t step, std::uint32_t step_ticks,
                                    std::int32_t swing_ticks) {
  return static_cast<std::int64_t>(step) * static_cast<std::int64_t>(step_ticks) +
         ((step & 1u) ? swing_ticks : 0);
}

// Gate length in ticks: at least one tick and never longer than the step.
inline std::uint32_t pipeGateTicks(int gate_pct, std::uint32_t step_ticks) {
  const std::uint32_t gate =
      step_ticks * static_cast<std::uint32_t>(pipeClamp(gate_pct, 1, 100)) / 100u;
  return gate ? gate : 1u;
}

// (100 - falloff) % of value, truncated. Callers that emit note-ons clamp the
// result to >= 1 themselves: a 0 velocity note-on is a note-off on the wire.
inline std::uint8_t attenuate(std::uint8_t value, int falloff) {
  return static_cast<std::uint8_t>(static_cast<int>(value) *
                                   (100 - pipeClamp(falloff, 0, 100)) / 100);
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
inline std::int32_t lfoShape(int wave, std::size_t phase) {
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
inline std::uint8_t lfoValue(int wave, std::size_t phase, int depth, int center) {
  const std::int32_t scaled = static_cast<std::int32_t>(pipeClamp(depth, 0, 127)) *
                              lfoShape(wave, phase) / 127;
  return static_cast<std::uint8_t>(
      pipeClamp(pipeClamp(center, 0, 127) + static_cast<int>(scaled), 0, 127));
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
  bool add(const PipeNote& n) {
    std::size_t pos = 0;
    while (pos < count_ && before(notes_[pos], n)) ++pos;
    if (pos < count_ && notes_[pos].channel == n.channel &&
        notes_[pos].note == n.note)
      return true;
    if (count_ >= Capacity) return false;
    for (std::size_t i = count_; i > pos; --i) notes_[i] = notes_[i - 1];
    notes_[pos] = n;
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

// Ordered arp pattern for the held set `held[0..held_count)`. `octaves` 1..4
// stacks the same notes 12 semitones higher; notes above 127 are dropped and the
// list stops at kMaxArpPattern entries (lowest first). type 0 up, 1 down,
// 2 updown (the down leg repeats neither endpoint, so 60 64 67 gives
// 60 64 67 64), 3 random (list order; the caller picks the index) and 4 chord
// (base octave only, every note at once, `octaves` ignored).
inline std::size_t buildArpPattern(const PipeNote* held, std::size_t held_count,
                                   int type, int octaves,
                                   std::array<PipeNote, kMaxArpPattern>& out) {
  const int octave_range = pipeClamp(octaves, 1, 4);
  std::array<PipeNote, kMaxArpPattern> up{};
  std::size_t up_count = 0;
  const int octaves_used = type == 4 ? 1 : octave_range;
  for (int octave = 0; octave < octaves_used; ++octave) {
    for (std::size_t i = 0; i < held_count && up_count < kMaxArpPattern; ++i) {
      const int note = static_cast<int>(held[i].note) + 12 * octave;
      if (note > 127) continue;
      up[up_count] = held[i];
      up[up_count].note = static_cast<std::uint8_t>(note);
      ++up_count;
    }
  }
  if (type == 1) {  // down
    for (std::size_t i = 0; i < up_count; ++i) out[i] = up[up_count - 1 - i];
    return up_count;
  }
  for (std::size_t i = 0; i < up_count; ++i) out[i] = up[i];
  if (type != 2 || up_count < 3) return up_count;
  std::size_t size = up_count;
  for (std::size_t i = up_count - 1; i > 0 && size < kMaxArpPattern; --i)
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
  const int count = pipeClamp(voices, 0, static_cast<int>(kMaxHarmonyVoices));
  std::size_t size = 0;
  out[size++] = note;
  for (int i = 0; i < count; ++i) {
    const int shifted = static_cast<int>(note) + offsets[static_cast<std::size_t>(i)];
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

// One queued MidiDelay echo. `echoes_left` counts this echo and the ones that
// follow, so a slot carries a whole repeat chain and the queue is bounded by
// simultaneous input events instead of events x repeats. `data` holds the
// already attenuated bytes, so a re-queue only attenuates once more.
struct DelayEcho {
  std::uint32_t due_ms = 0;
  std::uint8_t size = 0;
  std::uint8_t data[3]{};
  std::uint8_t echoes_left = 0;
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
  std::uint32_t gate_tick = 0;
  bool gate_pending = false;

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

}  // namespace mb

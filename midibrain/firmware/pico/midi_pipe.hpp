#pragma once
// MIDI FX Wave-1 pipe engine for the mnkybox Pico firmware. Pure logic: no
// Pico SDK, host-tested in protocol_test.cpp. The Pi sends the session's
// RouteState.pipes over EV_MIDI_ROUTE / EV_MIDI_ROUTE_PIPE (protocol.hpp) and
// the routed bytes over EV_MIDI_ROUTE_SEND; main.cpp calls the chain in route
// order and writes the resulting bytes to the physical TRS output.
//
// Positional parameter order is the contract frozen in
// pi/session/session.hpp (MidiPipeType). Missing parameters use the defaults
// documented here; unknown types (Wave-2 ids 12..16) pass through unchanged.
// Wave 1 is the session's "stateless" set: no tempo, transport or scheduled
// timeline. A chain owns only the minimum pairing state its pipes need:
// Dispatcher note->channel assignments, Sustain's held-note latch, NoteLength's
// pending note-offs and the Chance/VelocityRandom RNG. Every evaluation returns
// fresh events; nothing here needs the clock tick except NoteLength's deadlines.
#include <array>
#include <cstddef>
#include <cstdint>

#include "midi.hpp"

namespace mb {
inline constexpr std::size_t kMaxPipeParams = route_pipe_param_max;
inline constexpr std::size_t kMaxRoutePipes = route_pipe_count_max;
// One evaluation can emit multiple events (Sustain releases its held notes).
// 64 covers every realistic held-note set without heap allocation.
inline constexpr std::size_t kMaxPipeEvents = 64;
inline constexpr std::size_t kMaxPendingNoteOffs = 64;

// Stable ids from pi/session/session.hpp. Wave 2 is listed so the wire codec
// can store it, but the Wave-1 engine deliberately leaves it untouched.
enum class MidiPipeType : std::uint8_t {
  Filter = 1,         // [noteOn,noteOff,cc,pc,press,bend,realtime,sysex] 1=pass
  NoteRange = 2,      // [lo,hi,velLo,velHi]
  CcRemap = 3,        // [inLo,inHi,outLo,outHi] linear CC-number map
  ChannelRemap = 4,   // [inCh 0=any, outCh 1..16]
  ScaleQuantize = 5,  // [scale,root,mask12]; scale 0 off, 1..5 scales, 6 user
                      // (user mask is absolute pitch classes; root ignored)
  Chance = 6,         // [percent 0..100] drop note-ons; note-offs always pass
  VelocityRandom = 7, // [amount 0..127, minVel 0..127]
  Dispatcher = 8,     // [numCh 2..16, startCh 1..16]
  Sustain = 9,        // [latch 0/1] CC64 sustain, holding note-offs while on
  NoteLength = 10,    // [lengthMs 0=off, else fixed note length]
  Transform = 11,     // [matchKind,matchLo,matchHi,outKind,outVal]
  Arp = 12,
  Repeater = 13,
  CcLfo = 14,
  Harmonizer = 15,
  MidiDelay = 16,
};
static_assert(static_cast<std::uint8_t>(MidiPipeType::Filter) == 1);
static_assert(static_cast<std::uint8_t>(MidiPipeType::NoteRange) == 2);
static_assert(static_cast<std::uint8_t>(MidiPipeType::CcRemap) == 3);
static_assert(static_cast<std::uint8_t>(MidiPipeType::ChannelRemap) == 4);
static_assert(static_cast<std::uint8_t>(MidiPipeType::ScaleQuantize) == 5);
static_assert(static_cast<std::uint8_t>(MidiPipeType::Chance) == 6);
static_assert(static_cast<std::uint8_t>(MidiPipeType::VelocityRandom) == 7);
static_assert(static_cast<std::uint8_t>(MidiPipeType::Dispatcher) == 8);
static_assert(static_cast<std::uint8_t>(MidiPipeType::Sustain) == 9);
static_assert(static_cast<std::uint8_t>(MidiPipeType::NoteLength) == 10);
static_assert(static_cast<std::uint8_t>(MidiPipeType::Transform) == 11);

struct Pipe {
  MidiPipeType type = MidiPipeType::Filter;
  bool bypass = false;
  std::uint8_t param_count = 0;
  std::array<std::int32_t, kMaxPipeParams> params{};
};

struct PipeEvent {
  std::uint8_t size = 0;
  std::uint8_t data[3]{};
};

struct PipeOutput {
  std::array<PipeEvent, kMaxPipeEvents> events{};
  std::size_t count = 0;
  void push(const PipeEvent& e) {
    if (count < events.size()) events[count++] = e;
  }
};

// Missing positional params use these fallbacks: Filter all pass; NoteRange
// 0..127 with velocity 0..127; CcRemap identity; ChannelRemap any -> 1;
// ScaleQuantize off; Chance 0 %; VelocityRandom 0/0; Dispatcher 2/1;
// Sustain off; NoteLength off; a Transform without an outKind passes through
// (with outKind 0..4, outVal defaults to 0). Values are clamped to each pipe's
// documented range before use.

// One route's ordered pipe chain plus its runtime state. The route is enabled
// by begin(true); append/setPipeChunk configure pipes after it. Any config
// change resets the runtime state so a stale dispatcher/sustain/note-length
// decision never leaks into the new chain.
class MidiPipeChain {
 public:
  MidiPipeChain() { resetState(); }

  void begin(bool on) {
    on_ = on;
    pipe_count_ = 0;
    resetState();
  }
  bool enabled() const { return on_; }
  std::size_t pipeCount() const { return pipe_count_; }

  bool append(const Pipe& p) {
    return setPipeChunk(pipe_count_, static_cast<std::uint8_t>(p.type), p.bypass,
                        0, p.params.data(), p.param_count);
  }

  // Sets/replaces one pipe, or one parameter chunk of it. `offset == 0` starts
  // a pipe at `index` (index == pipeCount appends, index < pipeCount replaces);
  // a larger offset extends an existing pipe's positional params. The wire
  // frame carries at most route_pipe_param_frame_max params per chunk.
  bool setPipeChunk(std::uint8_t index, std::uint8_t type, bool bypass,
                    std::uint8_t offset, const std::int32_t* params,
                    std::size_t count) {
    if (index >= kMaxRoutePipes || type < 1 || type > 16) return false;
    if (offset > kMaxPipeParams || count > kMaxPipeParams - offset) return false;
    if (count != 0 && params == nullptr) return false;
    if (offset == 0) {
      if (index > pipe_count_) return false;
      Pipe p;
      p.type = static_cast<MidiPipeType>(type);
      p.bypass = bypass;
      p.param_count = static_cast<std::uint8_t>(count);
      for (std::size_t i = 0; i < count; ++i) p.params[i] = params[i];
      pipes_[index] = p;
      if (index == pipe_count_) ++pipe_count_;
    } else {
      if (index >= pipe_count_) return false;
      Pipe& p = pipes_[index];
      if (static_cast<std::uint8_t>(p.type) != type) return false;
      for (std::size_t i = 0; i < count; ++i) p.params[offset + i] = params[i];
      if (offset + count > p.param_count)
        p.param_count = static_cast<std::uint8_t>(offset + count);
    }
    resetState();
    return true;
  }

  void setSeed(std::uint32_t seed) {
    seed_ = seed ? seed : 1u;
    rng_state_ = seed_;
  }

  // Applies the chain to one routed message. A disabled/empty chain passes the
  // message unchanged. now_ms is the note-length clock (monotonic ms).
  PipeOutput process(const MidiMessage& m, std::uint32_t now_ms) {
    PipeOutput out;
    if (!on_ || m.size < 1 || m.size > 3) return out;
    PipeEvent input;
    input.size = m.size;
    for (std::uint8_t i = 0; i < m.size && i < 3; ++i) input.data[i] = m.data[i];
    return runFrom(0, input, now_ms);
  }

  // Emits note-offs whose fixed NoteLength has elapsed. Each off re-enters the
  // chain at the NoteLength pipe's successor, so later pipes (for example
  // ChannelRemap or Sustain) still see it.
  PipeOutput tick(std::uint32_t now_ms) {
    PipeOutput out;
    if (!on_) return out;
    for (auto& pending : pending_) {
      if (!pending.used) continue;
      if (static_cast<std::int32_t>(now_ms - pending.due_ms) < 0) continue;
      pending.used = false;
      PipeEvent off;
      off.size = 3;
      off.data[0] = static_cast<std::uint8_t>(0x80 | pending.channel);
      off.data[1] = pending.note;
      off.data[2] = 0;
      PipeOutput tail = runFrom(pending.stage, off, now_ms);
      for (std::size_t i = 0; i < tail.count; ++i) out.push(tail.events[i]);
    }
    return out;
  }

  void resetState() {
    dispatch_index_ = 0;
    for (auto& channel : note_channel_) channel.fill(0);
    sustain_latched_ = false;
    for (auto& channel : sustain_held_) channel.fill(0);
    for (auto& pending : pending_) pending = PendingOff{};
    rng_state_ = seed_;
  }

 private:
  struct PendingOff {
    bool used = false;
    std::uint32_t due_ms = 0;
    std::uint8_t stage = 0;
    std::uint8_t channel = 0;
    std::uint8_t note = 0;
  };

  // Message kinds used by Filter/Transform. 8 means "system/common, not
  // covered by a pipe mask"; those messages always pass.
  enum Kind : int {
    kNoteOn = 0,
    kNoteOff = 1,
    kCc = 2,
    kPc = 3,
    kPressure = 4,
    kBend = 5,
    kRealtime = 6,
    kSysex = 7,
    kOther = 8,
  };

  static int kindOf(const PipeEvent& e) {
    if (e.size < 1) return kOther;
    const std::uint8_t status = e.data[0];
    if (status >= 0xF8) return kRealtime;
    if (status == 0xF0 || status == 0xF7) return kSysex;
    if (status >= 0xF0) return kOther;
    switch (status & 0xF0) {
      case 0x80:
        return kNoteOff;
      case 0x90:
        return (e.size >= 3 && e.data[2] == 0) ? kNoteOff : kNoteOn;
      case 0xB0:
        return kCc;
      case 0xC0:
        return kPc;
      case 0xD0:
      case 0xA0:
        return kPressure;
      case 0xE0:
        return kBend;
      default:
        return kOther;
    }
  }

  static int clampInt(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
  }

  static int param(const Pipe& p, std::size_t index, int fallback) {
    return index < p.param_count ? static_cast<int>(p.params[index]) : fallback;
  }

  static void append(std::array<PipeEvent, kMaxPipeEvents>& out, std::size_t& count,
                     const PipeEvent& e) {
    if (count < kMaxPipeEvents) out[count++] = e;
  }

  std::uint32_t nextRandom() {
    std::uint32_t x = rng_state_;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng_state_ = x ? x : 0x9e3779b9u;
    return rng_state_;
  }

  static bool scaleAllowed(int scale, int root, std::uint16_t mask12,
                           std::array<bool, 12>& allowed) {
    allowed.fill(false);
    if (scale == 6) {
      for (int pc = 0; pc < 12; ++pc) allowed[pc] = ((mask12 >> pc) & 1u) != 0;
      return true;
    }
    const std::uint8_t* intervals = nullptr;
    std::size_t count = 0;
    switch (scale) {
      case 1: {
        static constexpr std::uint8_t kMajor[] = {0, 2, 4, 5, 7, 9, 11};
        intervals = kMajor;
        count = 7;
        break;
      }
      case 2: {
        static constexpr std::uint8_t kMinor[] = {0, 2, 3, 5, 7, 8, 10};
        intervals = kMinor;
        count = 7;
        break;
      }
      case 3: {
        static constexpr std::uint8_t kPentaMajor[] = {0, 2, 4, 7, 9};
        intervals = kPentaMajor;
        count = 5;
        break;
      }
      case 4: {
        static constexpr std::uint8_t kPentaMinor[] = {0, 3, 5, 7, 10};
        intervals = kPentaMinor;
        count = 5;
        break;
      }
      case 5: {
        static constexpr std::uint8_t kDorian[] = {0, 2, 3, 5, 7, 9, 10};
        intervals = kDorian;
        count = 7;
        break;
      }
      default:
        return false;
    }
    for (std::size_t i = 0; i < count; ++i)
      allowed[static_cast<std::size_t>((root + intervals[i]) % 12)] = true;
    return true;
  }

  static bool quantizeNote(int note, int scale, int root, std::uint16_t mask12,
                           int& result) {
    std::array<bool, 12> allowed{};
    if (!scaleAllowed(scale, root, mask12, allowed)) return false;
    bool any = false;
    for (bool value : allowed) any = any || value;
    if (!any) return false;
    const int pc = note % 12;
    if (allowed[static_cast<std::size_t>(pc)]) {
      result = note;
      return true;
    }
    // Nearest pitch class, downward on a tie. Octave-wrap the result so a note
    // at the 0/127 edge still lands on a real MIDI note.
    for (int distance = 1; distance <= 6; ++distance) {
      int down = pc - distance;
      if (down < 0) down += 12;
      if (allowed[static_cast<std::size_t>(down)]) {
        int candidate = note - distance;
        if (candidate < 0) candidate += 12;
        if (candidate > 127) candidate -= 12;
        result = candidate;
        return true;
      }
      int up = pc + distance;
      if (up >= 12) up -= 12;
      if (allowed[static_cast<std::size_t>(up)]) {
        int candidate = note + distance;
        if (candidate > 127) candidate -= 12;
        if (candidate < 0) candidate += 12;
        result = candidate;
        return true;
      }
    }
    return false;
  }

  PipeOutput runFrom(std::size_t start_pipe, const PipeEvent& start,
                     std::uint32_t now_ms) {
    PipeOutput out;
    if (!on_) return out;
    std::array<PipeEvent, kMaxPipeEvents> current{};
    std::array<PipeEvent, kMaxPipeEvents> next{};
    current[0] = start;
    std::size_t current_count = 1;
    for (std::size_t index = start_pipe; index < pipe_count_ && current_count != 0;
         ++index) {
      std::size_t next_count = 0;
      runPipe(pipes_[index], index, current, current_count, next, next_count,
              now_ms);
      current = next;
      current_count = next_count;
    }
    for (std::size_t i = 0; i < current_count; ++i) out.push(current[i]);
    return out;
  }

  void runPipe(const Pipe& p, std::size_t pipe_index,
               const std::array<PipeEvent, kMaxPipeEvents>& in,
               std::size_t in_count,
               std::array<PipeEvent, kMaxPipeEvents>& out,
               std::size_t& out_count, std::uint32_t now_ms) {
    out_count = 0;
    if (p.bypass || static_cast<std::uint8_t>(p.type) > 11) {
      for (std::size_t i = 0; i < in_count; ++i) append(out, out_count, in[i]);
      return;
    }
    switch (p.type) {
      case MidiPipeType::Filter: {
        for (std::size_t i = 0; i < in_count; ++i) {
          const int kind = kindOf(in[i]);
          if (kind < 8) {
            const int pass = param(p, static_cast<std::size_t>(kind), 1);
            if (pass == 0) continue;
          }
          append(out, out_count, in[i]);
        }
        return;
      }

      case MidiPipeType::NoteRange: {
        int lo = clampInt(param(p, 0, 0), 0, 127);
        int hi = clampInt(param(p, 1, 127), 0, 127);
        if (lo > hi) {
          const int swap = lo;
          lo = hi;
          hi = swap;
        }
        const int vel_lo = clampInt(param(p, 2, 0), 0, 127);
        const int vel_hi = clampInt(param(p, 3, 127), 0, 127);
        for (std::size_t i = 0; i < in_count; ++i) {
          const int kind = kindOf(in[i]);
          if (kind != kNoteOn && kind != kNoteOff) {
            append(out, out_count, in[i]);
            continue;
          }
          const int note = in[i].data[1];
          if (note < lo || note > hi) continue;
          // Release velocity is commonly 0; the window is a note-on filter so
          // a high floor cannot strand a held note.
          if (kind == kNoteOn) {
            const int velocity = in[i].data[2];
            if (velocity < vel_lo || velocity > vel_hi) continue;
          }
          append(out, out_count, in[i]);
        }
        return;
      }

      case MidiPipeType::CcRemap: {
        int in_lo = clampInt(param(p, 0, 0), 0, 127);
        int in_hi = clampInt(param(p, 1, 127), 0, 127);
        if (in_lo > in_hi) {
          const int swap = in_lo;
          in_lo = in_hi;
          in_hi = swap;
        }
        const int out_lo = clampInt(param(p, 2, 0), 0, 127);
        const int out_hi = clampInt(param(p, 3, 127), 0, 127);
        for (std::size_t i = 0; i < in_count; ++i) {
          PipeEvent e = in[i];
          if (e.size >= 3 && (e.data[0] & 0xF0) == 0xB0) {
            const int cc = e.data[1];
            if (cc >= in_lo && cc <= in_hi) {
              int mapped = out_lo;
              const int span = in_hi - in_lo;
              if (span > 0) {
                const long long numerator =
                    static_cast<long long>(cc - in_lo) * (out_hi - out_lo);
                const long long rounded =
                    numerator >= 0 ? (numerator + span / 2) / span
                                   : (numerator - span / 2) / span;
                mapped = out_lo + static_cast<int>(rounded);
              }
              e.data[1] = static_cast<std::uint8_t>(clampInt(mapped, 0, 127));
            }
          }
          append(out, out_count, e);
        }
        return;
      }

      case MidiPipeType::ChannelRemap: {
        const int in_ch = clampInt(param(p, 0, 0), 0, 16);
        const int out_ch = clampInt(param(p, 1, 1), 1, 16);
        for (std::size_t i = 0; i < in_count; ++i) {
          PipeEvent e = in[i];
          if (e.size >= 1 && e.data[0] >= 0x80 && e.data[0] < 0xF0) {
            const int channel = (e.data[0] & 0x0F) + 1;
            if (in_ch == 0 || channel == in_ch)
              e.data[0] = static_cast<std::uint8_t>((e.data[0] & 0xF0) |
                                                    (out_ch - 1));
          }
          append(out, out_count, e);
        }
        return;
      }

      case MidiPipeType::ScaleQuantize: {
        const int scale = param(p, 0, 0);
        if (scale <= 0 || scale > 6) {
          for (std::size_t i = 0; i < in_count; ++i)
            append(out, out_count, in[i]);
          return;
        }
        const int root = clampInt(param(p, 1, 0), 0, 11);
        const auto mask12 =
            static_cast<std::uint16_t>(param(p, 2, 0x0FFF) & 0x0FFF);
        for (std::size_t i = 0; i < in_count; ++i) {
          PipeEvent e = in[i];
          const int kind = kindOf(e);
          if (kind == kNoteOn || kind == kNoteOff) {
            int quantized = e.data[1];
            if (quantizeNote(e.data[1], scale, root, mask12, quantized))
              e.data[1] = static_cast<std::uint8_t>(clampInt(quantized, 0, 127));
          }
          append(out, out_count, e);
        }
        return;
      }

      case MidiPipeType::Chance: {
        const int percent = clampInt(param(p, 0, 0), 0, 100);
        for (std::size_t i = 0; i < in_count; ++i) {
          if (kindOf(in[i]) == kNoteOn && percent > 0 &&
              static_cast<int>(nextRandom() % 100u) < percent)
            continue;
          append(out, out_count, in[i]);
        }
        return;
      }

      case MidiPipeType::VelocityRandom: {
        const int amount = clampInt(param(p, 0, 0), 0, 127);
        const int floor = clampInt(param(p, 1, 0), 0, 127);
        for (std::size_t i = 0; i < in_count; ++i) {
          PipeEvent e = in[i];
          if (kindOf(e) == kNoteOn) {
            int delta = 0;
            if (amount > 0) {
              const int span = 2 * amount + 1;
              delta = static_cast<int>(nextRandom() % static_cast<std::uint32_t>(span)) -
                      amount;
            }
            const int velocity = clampInt(static_cast<int>(e.data[2]) + delta,
                                          floor, 127);
            e.data[2] = static_cast<std::uint8_t>(velocity);
          }
          append(out, out_count, e);
        }
        return;
      }

      case MidiPipeType::Dispatcher: {
        const int num_channels = clampInt(param(p, 0, 2), 2, 16);
        const int start_channel = clampInt(param(p, 1, 1), 1, 16);
        for (std::size_t i = 0; i < in_count; ++i) {
          PipeEvent e = in[i];
          const int kind = kindOf(e);
          if (kind == kNoteOn || kind == kNoteOff) {
            const std::size_t input_channel =
                static_cast<std::size_t>(e.data[0] & 0x0F);
            const std::uint8_t note = e.data[1];
            if (kind == kNoteOn) {
              const int slot = static_cast<int>(dispatch_index_ %
                                                static_cast<std::uint32_t>(num_channels));
              const int channel =
                  1 + ((start_channel - 1 + slot) % 16);
              note_channel_[input_channel][note] =
                  static_cast<std::uint8_t>(channel);
              ++dispatch_index_;
              e.data[0] = static_cast<std::uint8_t>((e.data[0] & 0xF0) |
                                                    (channel - 1));
            } else {
              const std::uint8_t assigned =
                  note_channel_[input_channel][note];
              if (assigned != 0) {
                e.data[0] = static_cast<std::uint8_t>((e.data[0] & 0xF0) |
                                                      (assigned - 1));
                note_channel_[input_channel][note] = 0;
              }
            }
          }
          append(out, out_count, e);
        }
        return;
      }

      case MidiPipeType::Sustain: {
        const bool latch_enabled = param(p, 0, 0) != 0;
        if (!latch_enabled) {
          for (std::size_t i = 0; i < in_count; ++i)
            append(out, out_count, in[i]);
          return;
        }
        for (std::size_t i = 0; i < in_count; ++i) {
          const PipeEvent& event = in[i];
          const int kind = kindOf(event);
          if (kind == kCc && event.size >= 3 && event.data[1] == 64) {
            if (event.data[2] >= 64) {
              sustain_latched_ = true;
            } else {
              sustain_latched_ = false;
              for (std::size_t channel = 0; channel < sustain_held_.size();
                   ++channel) {
                for (int note = 0; note < 128; ++note) {
                  const std::size_t word = static_cast<std::size_t>(note >> 6);
                  const auto bit =
                      static_cast<std::uint64_t>(1) << (note & 63);
                  if ((sustain_held_[channel][word] & bit) == 0) continue;
                  PipeEvent off;
                  off.size = 3;
                  off.data[0] = static_cast<std::uint8_t>(0x80 | channel);
                  off.data[1] = static_cast<std::uint8_t>(note);
                  off.data[2] = 0;
                  append(out, out_count, off);
                }
              }
              for (auto& channel : sustain_held_) channel.fill(0);
            }
            append(out, out_count, event);
            continue;
          }
          if (kind == kNoteOff) {
            const std::size_t channel =
                static_cast<std::size_t>(event.data[0] & 0x0F);
            const int note = event.data[1];
            const std::size_t word = static_cast<std::size_t>(note >> 6);
            const auto bit = static_cast<std::uint64_t>(1) << (note & 63);
            if (sustain_latched_) {
              sustain_held_[channel][word] |= bit;
              continue;
            }
            sustain_held_[channel][0] = 0;
            sustain_held_[channel][1] = 0;
            append(out, out_count, event);
            continue;
          }
          append(out, out_count, event);
        }
        return;
      }

      case MidiPipeType::NoteLength: {
        int length_ms = param(p, 0, 0);
        if (length_ms <= 0) {
          for (std::size_t i = 0; i < in_count; ++i)
            append(out, out_count, in[i]);
          return;
        }
        if (length_ms > 60000) length_ms = 60000;
        for (std::size_t i = 0; i < in_count; ++i) {
          PipeEvent e = in[i];
          const int kind = kindOf(e);
          if (kind == kNoteOn) {
            append(out, out_count, e);
            scheduleNoteOff(e, pipe_index, length_ms, now_ms);
          } else if (kind == kNoteOff) {
            // A fixed-length note ignores the incoming off and waits for the
            // scheduled one; otherwise a short key press would defeat the pipe.
            if (!hasPending(e.data[0] & 0x0F, e.data[1]))
              append(out, out_count, e);
          } else {
            append(out, out_count, e);
          }
        }
        return;
      }

      case MidiPipeType::Transform: {
        // A partial config without an output kind is not a conversion; leave
        // the bytes alone instead of silently retuning every note to 0.
        if (p.param_count < 4) {
          for (std::size_t i = 0; i < in_count; ++i)
            append(out, out_count, in[i]);
          return;
        }
        const int match_kind = clampInt(param(p, 0, 0), 0, 4);
        const bool bend_match = match_kind == 3;
        const int match_scale = bend_match ? 16383 : 127;
        int match_lo = clampInt(param(p, 1, 0), 0, match_scale);
        int match_hi = clampInt(param(p, 2, match_scale), 0, match_scale);
        if (match_lo > match_hi) {
          const int swap = match_lo;
          match_lo = match_hi;
          match_hi = swap;
        }
        const int out_kind = clampInt(param(p, 3, 0), 0, 4);
        const int out_value = clampInt(param(p, 4, 0), 0, 16383);
        for (std::size_t i = 0; i < in_count; ++i) {
          PipeEvent e = in[i];
          if (!transformMatches(e, match_kind, match_lo, match_hi)) {
            append(out, out_count, e);
            continue;
          }
          PipeEvent converted;
          if (makeTransform(e, out_kind, out_value, converted))
            append(out, out_count, converted);
          else
            append(out, out_count, e);
        }
        return;
      }

      default:
        for (std::size_t i = 0; i < in_count; ++i)
          append(out, out_count, in[i]);
        return;
    }
  }

  static bool transformMatches(const PipeEvent& e, int match_kind, int lo,
                               int hi) {
    const int kind = kindOf(e);
    int value = e.data[1];
    if (match_kind == 0) {
      if (kind != kNoteOn && kind != kNoteOff) return false;
    } else if (match_kind == 1) {
      if (kind != kCc) return false;
    } else if (match_kind == 2) {
      if (kind != kPc) return false;
    } else if (match_kind == 3) {
      if (kind != kBend || e.size < 3) return false;
      value = static_cast<int>(e.data[1]) | (static_cast<int>(e.data[2]) << 7);
    } else if (match_kind == 4) {
      if (kind != kPressure) return false;
    } else {
      return false;
    }
    return value >= lo && value <= hi;
  }

  static bool makeTransform(const PipeEvent& source, int out_kind, int out_value,
                            PipeEvent& out) {
    const std::uint8_t channel = source.data[0] & 0x0F;
    const int source_kind = kindOf(source);
    switch (out_kind) {
      case 0: {
        out.size = 3;
        out.data[0] = static_cast<std::uint8_t>(0x90 | channel);
        out.data[1] = static_cast<std::uint8_t>(clampInt(out_value, 0, 127));
        out.data[2] = static_cast<std::uint8_t>(
            source_kind == kNoteOn ? source.data[2]
                                   : (source_kind == kNoteOff ? 0 : 100));
        return true;
      }
      case 1: {
        out.size = 3;
        out.data[0] = static_cast<std::uint8_t>(0xB0 | channel);
        out.data[1] = static_cast<std::uint8_t>(clampInt(out_value, 0, 127));
        out.data[2] = source.size >= 3 ? source.data[2] : 127;
        return true;
      }
      case 2: {
        out.size = 2;
        out.data[0] = static_cast<std::uint8_t>(0xC0 | channel);
        out.data[1] = static_cast<std::uint8_t>(clampInt(out_value, 0, 127));
        return true;
      }
      case 3: {
        const int bend = clampInt(out_value, 0, 16383);
        out.size = 3;
        out.data[0] = static_cast<std::uint8_t>(0xE0 | channel);
        out.data[1] = static_cast<std::uint8_t>(bend & 0x7F);
        out.data[2] = static_cast<std::uint8_t>((bend >> 7) & 0x7F);
        return true;
      }
      case 4: {
        out.size = 2;
        out.data[0] = static_cast<std::uint8_t>(0xD0 | channel);
        out.data[1] = static_cast<std::uint8_t>(clampInt(out_value, 0, 127));
        return true;
      }
      default:
        return false;
    }
  }

  bool hasPending(std::uint8_t channel, std::uint8_t note) const {
    for (const auto& pending : pending_) {
      if (pending.used && pending.channel == channel && pending.note == note)
        return true;
    }
    return false;
  }

  void scheduleNoteOff(const PipeEvent& note_on, std::size_t pipe_index,
                       int length_ms, std::uint32_t now_ms) {
    const std::uint8_t channel = note_on.data[0] & 0x0F;
    const std::uint8_t note = note_on.data[1];
    PendingOff* slot = nullptr;
    for (auto& pending : pending_) {
      if (pending.used && pending.channel == channel && pending.note == note) {
        slot = &pending;
        break;
      }
    }
    if (slot == nullptr) {
      for (auto& pending : pending_) {
        if (!pending.used) {
          slot = &pending;
          break;
        }
      }
    }
    if (slot == nullptr) return;  // full: fall back to the incoming note-off
    slot->used = true;
    slot->due_ms = now_ms + static_cast<std::uint32_t>(length_ms);
    slot->stage = static_cast<std::uint8_t>(pipe_index + 1);
    slot->channel = channel;
    slot->note = note;
  }

  std::array<Pipe, kMaxRoutePipes> pipes_{};
  std::size_t pipe_count_ = 0;
  bool on_ = false;

  std::uint32_t seed_ = 0x9e3779b9u;
  std::uint32_t rng_state_ = 0x9e3779b9u;

  std::uint32_t dispatch_index_ = 0;
  std::array<std::array<std::uint8_t, 128>, 16> note_channel_{};

  bool sustain_latched_ = false;
  // Two 64-bit words cover notes 0..127 for each of the 16 channels; bit n
  // in word (n >> 6) marks a note-off latched by the sustain pipe.
  std::array<std::array<std::uint64_t, 2>, 16> sustain_held_{};

  std::array<PendingOff, kMaxPendingNoteOffs> pending_{};
};

}  // namespace mb

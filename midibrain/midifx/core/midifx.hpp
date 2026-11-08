#pragma once
// Shared MIDI FX pipe engine: one ordered chain turns one routed MIDI message
// into zero or more messages. This is the single behavior implementation for
// all 16 pipe types (stateless 1..11, clocked 12..16), compiled unchanged by
// both builds: the Pico firmware and the Pi core include this header.
//
// Ownership and clock contract
// ----------------------------
// The engine owns no timer, transport state or allocator. Every call injects
// the caller's timeline: nowMs is a monotonic millisecond counter and tickIndex
// is an absolute 24-PPQN tick on the caller's grid. The Pico passes its alarm
// tick counter; a Pi-side caller passes the tick derived from its disciplined
// USB-clock phase. A transport START that resets the caller's tick counter is
// handled by the grid resync in stepSequencer()/stepLfo(). Only MidiDelay
// counts milliseconds (its parameter is delayMs).
//
// Wire contract
// -------------
// A pipe's positional params ARE the session's params[] order and the 0x19
// EV_MIDI_ROUTE_PIPE offsets: missing offsets use the generated schema default
// (midifx/gen/generated/midifx_params.hpp), extra ones are ignored. At most 8
// pipes per chain, 10 params per pipe. Everything is fixed-capacity: no heap,
// no exceptions, no RTTI. A chain edit resets runtime state; clocked pipes
// release the notes they had turned on, so an edit can never leave a note
// sounding forever.
//
// Runtime state per chain position
// -------------------------------
// A chain keeps one runtime slot per pipe position: Dispatcher note->channel
// assignments, the Sustain latch, NoteLength's pending note-offs, the
// Chance/VelocityRandom xorshift32 stream, arp/repeater/LFO grids, harmonizer
// voice refcounts and the MidiDelay echo queue.
#include <array>
#include <cstddef>
#include <cstdint>

#include "../gen/generated/midifx_params.hpp"
#include "timing.hpp"

namespace mnky::midifx {

// Wire limits shared with firmware/pico/protocol.hpp (route_pipe_count_max /
// route_pipe_param_max); firmware/pico/main.cpp static_asserts the match.
inline constexpr std::size_t kMaxRoutePipes = 8;
inline constexpr std::size_t kMaxPipeParams = 10;
// One evaluation can emit many events (Sustain releases its held notes, a
// delayed chord echoes). 64 covers the realistic set without heap allocation.
inline constexpr std::size_t kMaxPipeEvents = 64;
inline constexpr std::size_t kMaxPendingNoteOffs = 64;
inline constexpr std::uint32_t kDefaultPipeSeed = 0x9e3779b9u;

// Stable ids from pi/session/session.hpp. The positional parameter contract is
// in pipes.json (docs: gen/generated/pipes-docs.md); the core uses the
// generated defaults for every missing offset.
enum class PipeType : std::uint8_t {
  Filter = 1,         // [noteOn,noteOff,cc,pc,pressure,bend,realtime,sysex]
  NoteRange = 2,      // [lo,hi,velLo,velHi]
  CcRemap = 3,        // [inLo,inHi,outLo,outHi] linear CC-number map
  ChannelRemap = 4,   // [inCh 0 = any, outCh 1..16]
  ScaleQuantize = 5,  // [scale,root,mask12]; scale 0 off, 1..5 scales, 6 user
  Chance = 6,         // [percent 0..100] drop note-ons; note-offs always pass
  VelocityRandom = 7, // [amount 0..127, minVel 0..127]
  Dispatcher = 8,     // [numCh 2..16, startCh 1..16]
  Sustain = 9,        // [latch 0/1] hold note-offs while CC64 is down
  NoteLength = 10,    // [lengthMs 0 = off, else fixed note length]
  Transform = 11,     // [matchKind,matchLo,matchHi,outKind,outVal]
  Arp = 12,           // [div,type,octaves,swing,accent,gate]
  Repeater = 13,      // [div,repeats,falloff]
  CcLfo = 14,         // [cc,wave,div,depth,center]
  Harmonizer = 15,    // [voices,i1,i2,i3,i4]
  MidiDelay = 16,     // [delayMs,repeats,falloff]
};

// One configured pipe. `paramCount` is how many positional params actually
// arrived; params past it read the schema default.
struct PipeConfig {
  PipeType type = PipeType::Filter;
  bool bypass = false;
  // Per-pipe channel scope: a channel-voice message (0x80..0xEF) enters the
  // pipe only when bit (channel, 0-based) is set; every other message passes
  // this pipe unchanged, in stream order. 0xFFFF (default) processes all
  // messages including channel-less system/realtime bytes; a restricted mask
  // leaves those untouched because they have no channel to match. 0 makes the
  // pipe inert. Carried alongside the positional params on the 0x19 offset-0
  // chunk; a continuation chunk keeps the established scope. The generated
  // schema owns the default (kChannelScope).
  std::uint16_t chMask = generated::kChannelScope.defaultValue;
  std::uint8_t paramCount = 0;
  std::array<std::int32_t, kMaxPipeParams> params{};
};

// One MIDI 1.0 message, 1..3 bytes.
struct PipeEvent {
  std::uint8_t size = 0;
  std::uint8_t data[3] = {};
};

struct PipeOutput {
  std::array<PipeEvent, kMaxPipeEvents> events{};
  std::size_t count = 0;
  // Events past kMaxPipeEvents (a Sustain release burst or a delayed chord can
  // exceed the cap); the earliest events win, the rest are counted here.
  std::size_t dropped = 0;

  void push(const PipeEvent& event) {
    if (count < events.size()) {
      events[count++] = event;
    } else {
      ++dropped;
    }
  }
};

// One route's ordered chain plus its runtime state. Reconfiguring resets the
// runtime state, so a stale pairing decision can never leak into a new chain.
class PipeChain {
 public:
  PipeChain() { resetState(); }

  // Route gate: begin(true) enables the chain (and starts an empty one),
  // begin(false) clears it; a disabled chain drops routed bytes. A fresh chain
  // is disabled, matching the firmware's RAM-only route state.
  void begin(bool on) {
    on_ = on;
    pipe_count_ = 0;
    resetState();
  }
  bool enabled() const { return on_; }
  bool empty() const { return pipe_count_ == 0; }
  std::size_t pipeCount() const { return pipe_count_; }

  // Copies one configured pipe (positional params included) for snapshot
  // owners. False for an index past the chain; the output is untouched then.
  bool pipeAt(std::size_t index, PipeConfig& out) const {
    if (index >= pipe_count_) return false;
    out = pipes_[index];
    return true;
  }

  // Route legality for a generator route (src = kSrcGenerator): the chain HEAD
  // must be a pipe whose schema role is generator (pipes.json role). An empty
  // chain is legal but emits nothing; a modifier head is refused when the
  // chain is configured. Bypassing the head does not change its role.
  bool headIsGenerator() const {
    return pipe_count_ != 0 &&
           generated::pipeIsGenerator(static_cast<std::uint8_t>(pipes_[0].type));
  }

  bool append(const PipeConfig& pipe) {
    return setPipeChunk(pipe_count_, static_cast<std::uint8_t>(pipe.type),
                        pipe.bypass, 0, pipe.params.data(), pipe.paramCount);
  }

  // Sets/replaces one pipe, or one parameter chunk of it. `offset == 0` starts
  // a pipe at `index` (index == pipeCount appends, index < pipeCount replaces)
  // and applies `chMask`; a larger offset extends an existing pipe's positional
  // params and keeps the scope the offset-0 chunk established (the wire's
  // continuation chunks cannot move a pipe's scope). The 0x19 frame carries at
  // most four params per chunk; this accepts any chunk size up to the 10-param
  // contract.
  bool setPipeChunk(std::uint8_t index, std::uint8_t type, bool bypass,
                    std::uint8_t offset, const std::int32_t* params,
                    std::size_t count,
                    std::uint16_t chMask = generated::kChannelScope.defaultValue) {
    if (index >= kMaxRoutePipes || type < 1 || type > 16) return false;
    if (offset > kMaxPipeParams || count > kMaxPipeParams - offset) return false;
    if (count != 0 && params == nullptr) return false;
    if (offset == 0) {
      if (index > pipe_count_) return false;
      PipeConfig pipe;
      pipe.type = static_cast<PipeType>(type);
      pipe.bypass = bypass;
      pipe.chMask = chMask;
      pipe.paramCount = static_cast<std::uint8_t>(count);
      for (std::size_t i = 0; i < count; ++i) pipe.params[i] = params[i];
      pipes_[index] = pipe;
      if (index == pipe_count_) ++pipe_count_;
    } else {
      if (index >= pipe_count_) return false;
      PipeConfig& pipe = pipes_[index];
      if (static_cast<std::uint8_t>(pipe.type) != type) return false;
      for (std::size_t i = 0; i < count; ++i) pipe.params[offset + i] = params[i];
      if (offset + count > pipe.paramCount)
        pipe.paramCount = static_cast<std::uint8_t>(offset + count);
    }
    resetState();
    return true;
  }

  // Replaces the whole chain in one call (the Pi's whole-chain configure path).
  // Accepts ids 1..16, at most kMaxRoutePipes pipes and kMaxPipeParams params
  // each. Invalid input leaves the current chain untouched and returns false. A
  // successful configure enables the chain: the Pi chain owner has no separate
  // pipe gate (its router decides whether a route runs), so configuring and
  // evaluating is one step. Use begin(false) to mute.
  bool configure(const PipeConfig* pipes, std::size_t count) {
    if (count > kMaxRoutePipes) return false;
    if (pipes == nullptr && count != 0) return false;
    for (std::size_t i = 0; i < count; ++i) {
      const std::uint8_t type = static_cast<std::uint8_t>(pipes[i].type);
      if (type < 1 || type > 16) return false;
      if (pipes[i].paramCount > kMaxPipeParams) return false;
    }
    for (std::size_t i = 0; i < count; ++i) pipes_[i] = pipes[i];
    for (std::size_t i = count; i < kMaxRoutePipes; ++i) pipes_[i] = PipeConfig{};
    pipe_count_ = count;
    on_ = true;
    resetState();
    return true;
  }

  // Chance/VelocityRandom stream. 0 maps to 1 (xorshift32 needs a nonzero
  // state); every config change keeps the seed but rewinds the stream.
  void setSeed(std::uint32_t seed) {
    seed_ = seed != 0 ? seed : 1u;
    rng_state_ = seed_;
  }
  std::uint32_t seed() const { return seed_; }

  // Applies the chain to one routed message. nowMs is the monotonic
  // millisecond clock (wrap-safe comparisons).
  PipeOutput process(const PipeEvent& input, std::uint32_t nowMs) {
    PipeOutput out;
    if (!on_ || input.size < 1 || input.size > 3) return out;
    return runFrom(0, input, nowMs);
  }

  // Millisecond deadlines: fixed NoteLength offs and MidiDelay echoes, plus the
  // notes an earlier config edit orphaned. Each generated event re-enters the
  // chain at its pipe's successor, so later pipes still see it. Call it every
  // main-loop pass.
  PipeOutput tick(std::uint32_t nowMs) {
    PipeOutput out;
    releaseOrphans(nowMs, out);
    if (!on_) return out;
    for (auto& pending : pending_) {
      if (!pending.used) continue;
      if (static_cast<std::int32_t>(nowMs - pending.dueMs) < 0) continue;
      pending.used = false;
      PipeEvent off;
      off.size = 3;
      off.data[0] = static_cast<std::uint8_t>(0x80 | pending.channel);
      off.data[1] = pending.note;
      off.data[2] = 0;
      mergeOutput(out, runFrom(pending.stage, off, nowMs));
    }
    for (std::size_t index = 0; index < pipe_count_; ++index) {
      const PipeConfig& pipe = pipes_[index];
      if (pipe.bypass || pipe.type != PipeType::MidiDelay) continue;
      serviceDelay(pipe, index, nowMs, out);
    }
    return out;
  }

  // Advances the clocked pipes on the caller's 24-PPQN grid. tickIndex is the
  // absolute tick count (24 per quarter note): division steps, swing, gates,
  // LFO phase and the arp accent grid are all tick indexes. Every generated
  // event re-enters the chain after its own pipe. Call it every main-loop pass
  // with the latest tick.
  PipeOutput clockTick(std::uint32_t tickIndex, std::uint32_t nowMs) {
    PipeOutput out;
    releaseOrphans(nowMs, out);
    if (!on_) return out;
    for (std::size_t index = 0; index < pipe_count_; ++index) {
      const PipeConfig& pipe = pipes_[index];
      if (pipe.bypass) continue;
      switch (pipe.type) {
        case PipeType::Arp:
        case PipeType::Repeater:
          releaseGate(index, tickIndex, nowMs, out);
          stepSequencer(pipe, index, tickIndex, nowMs, out);
          break;
        case PipeType::CcLfo:
          stepLfo(pipe, index, tickIndex, nowMs, out);
          break;
        default:
          break;
      }
    }
    return out;
  }

  // Clears pairing/sustain/pending state and rewinds the RNG to the seed.
  // Clocked pipes forget their held notes and grid, but the note-ons they
  // already put on the wire stay committed: tick() releases them before
  // anything else, so editing a chain cannot leave a note sounding forever.
  void resetState() {
    dispatch_index_ = 0;
    for (auto& channel : note_channel_) channel.fill(0);
    sustain_latched_ = false;
    for (auto& channel : sustain_held_) channel.fill(0);
    for (auto& pending : pending_) pending = PendingOff{};
    rng_state_ = seed_;
    for (std::size_t index = 0; index < kMaxRoutePipes; ++index) {
      ClockedPipeState& state = clocked_[index];
      state.orphaned = state.sounding.count() != 0 || harmonyCount(state) != 0;
      state.held.clear();
      state.stepping = false;
      state.step = 0;
      state.cursor = 0;
      state.burst = 0;
      state.gatePending = false;
      for (auto& echo : delay_[index]) echo = DelayEcho{};
    }
  }

 private:
  struct PendingOff {
    bool used = false;
    std::uint32_t dueMs = 0;
    std::uint8_t stage = 0;
    std::uint8_t channel = 0;
    std::uint8_t note = 0;
  };

  // Message kinds used by Filter/Transform. kOther means "system/common, not
  // covered by a pipe mask"; those messages always pass a Filter.
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

  static int kindOf(const PipeEvent& event) {
    if (event.size < 1) return kOther;
    const std::uint8_t status = event.data[0];
    if (status >= 0xF8) return kRealtime;
    if (status == 0xF0 || status == 0xF7) return kSysex;
    if (status >= 0xF0) return kOther;
    switch (status & 0xF0) {
      case 0x80:
        return kNoteOff;
      case 0x90:
        return (event.size >= 3 && event.data[2] == 0) ? kNoteOff : kNoteOn;
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

  // A missing positional param uses the generated schema default, so the
  // schema (pipes.json) is the only place defaults are written down.
  static int param(const PipeConfig& pipe, std::size_t index) {
    if (index < pipe.paramCount) return static_cast<int>(pipe.params[index]);
    return static_cast<int>(generated::pipeDefaultParam(
        static_cast<std::uint8_t>(pipe.type), index));
  }

  static void append(PipeEvent* out, std::size_t& count, std::size_t& dropped,
                     const PipeEvent& event) {
    if (count < kMaxPipeEvents) {
      out[count++] = event;
    } else {
      ++dropped;
    }
  }

  static void mergeOutput(PipeOutput& out, const PipeOutput& source) {
    for (std::size_t i = 0; i < source.count; ++i) out.push(source.events[i]);
    out.dropped += source.dropped;
  }

  std::uint32_t nextRandom() {
    std::uint32_t x = rng_state_;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng_state_ = x != 0 ? x : kDefaultPipeSeed;
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

  PipeOutput runFrom(std::size_t startPipe, const PipeEvent& start,
                     std::uint32_t nowMs) {
    PipeOutput out;
    if (!on_) return out;
    std::array<PipeEvent, kMaxPipeEvents> current{};
    std::array<PipeEvent, kMaxPipeEvents> next{};
    current[0] = start;
    std::size_t currentCount = 1;
    for (std::size_t index = startPipe; index < pipe_count_ && currentCount != 0;
         ++index) {
      const PipeConfig& pipe = pipes_[index];
      std::size_t nextCount = 0;
      if (pipe.bypass || pipe.chMask == 0xFFFF) {
        runPipe(pipe, index, current.data(), currentCount, next.data(),
                nextCount, out.dropped, nowMs);
      } else {
        // Matching events are fed one at a time so out-of-scope events keep
        // their position and stateful pipes still see matching events in
        // their original order.
        for (std::size_t i = 0; i < currentCount; ++i) {
          if (inChannelScope(pipe.chMask, current[i]))
            runPipe(pipe, index, &current[i], 1, next.data(), nextCount,
                    out.dropped, nowMs);
          else
            append(next.data(), nextCount, out.dropped, current[i]);
        }
      }
      current = next;
      currentCount = nextCount;
    }
    for (std::size_t i = 0; i < currentCount; ++i) out.push(current[i]);
    return out;
  }

  // The chMask gate (the Pi spec: matching channels enter the pipe, every
  // other message passes unchanged in stream order). A restricted mask also
  // leaves channel-less system/realtime bytes untouched because they have no
  // channel to match; 0 makes the pipe inert.
  static bool inChannelScope(std::uint16_t mask, const PipeEvent& event) {
    if (mask == 0xFFFF) return true;
    if (event.size < 1) return false;
    const std::uint8_t status = event.data[0];
    if (status < 0x80 || status >= 0xF0) return false;
    return (mask & static_cast<std::uint16_t>(1u << (status & 0x0F))) != 0;
  }

  // One pipe's pass: transforms in[0..inCount) into out, appending at
  // outCount (the caller initialises it). Appending in place is what lets the
  // scoped path feed one event at a time without a second output buffer (the
  // Pico runs on a 2 KB stack). A bypassed pipe copies its input through
  // unchanged. pipeIndex is this pipe's chain position (NoteLength schedules
  // its offs to re-enter at index + 1; clocked pipes re-inject their generated
  // events there); nowMs is the monotonic ms clock. Appends past
  // kMaxPipeEvents are counted in dropped.
  void runPipe(const PipeConfig& pipe, std::size_t pipeIndex,
               const PipeEvent* in, std::size_t inCount, PipeEvent* out,
               std::size_t& outCount, std::size_t& dropped,
               std::uint32_t nowMs) {
    if (pipe.bypass) {
      for (std::size_t i = 0; i < inCount; ++i) append(out, outCount, dropped, in[i]);
      return;
    }
    switch (pipe.type) {
      // Kind gate: params[0..7] in Kind order -- [noteOn noteOff cc pc press
      // bend realtime sysex] -- where 1 (the default) passes and 0 drops.
      // System/common messages (kOther) have no mask bit and always pass.
      case PipeType::Filter: {
        for (std::size_t i = 0; i < inCount; ++i) {
          const int kind = kindOf(in[i]);
          if (kind < kOther) {
            if (param(pipe, static_cast<std::size_t>(kind)) == 0) continue;
          }
          append(out, outCount, dropped, in[i]);
        }
        return;
      }

      // Note window [lo,hi] (reversed bounds swap) with a note-on velocity
      // window [velLo,velHi]. Out-of-window notes drop; note-offs test only
      // the note window, other messages pass.
      case PipeType::NoteRange: {
        int lo = clampInt(param(pipe, 0), 0, 127);
        int hi = clampInt(param(pipe, 1), 0, 127);
        if (lo > hi) {
          const int swap = lo;
          lo = hi;
          hi = swap;
        }
        const int velLo = clampInt(param(pipe, 2), 0, 127);
        const int velHi = clampInt(param(pipe, 3), 0, 127);
        for (std::size_t i = 0; i < inCount; ++i) {
          const int kind = kindOf(in[i]);
          if (kind != kNoteOn && kind != kNoteOff) {
            append(out, outCount, dropped, in[i]);
            continue;
          }
          const int note = in[i].data[1];
          if (note < lo || note > hi) continue;
          // Release velocity is commonly 0; the window is a note-on filter so
          // a high floor cannot strand a held note.
          if (kind == kNoteOn) {
            const int velocity = in[i].data[2];
            if (velocity < velLo || velocity > velHi) continue;
          }
          append(out, outCount, dropped, in[i]);
        }
        return;
      }

      // Linear CC-number remap: controllers in [inLo,inHi] rescale onto
      // [outLo,outHi], rounded half away from zero. CCs outside the input
      // window and non-CC messages pass unchanged.
      case PipeType::CcRemap: {
        int inLo = clampInt(param(pipe, 0), 0, 127);
        int inHi = clampInt(param(pipe, 1), 0, 127);
        if (inLo > inHi) {
          const int swap = inLo;
          inLo = inHi;
          inHi = swap;
        }
        const int outLo = clampInt(param(pipe, 2), 0, 127);
        const int outHi = clampInt(param(pipe, 3), 0, 127);
        for (std::size_t i = 0; i < inCount; ++i) {
          PipeEvent event = in[i];
          if (event.size >= 3 && (event.data[0] & 0xF0) == 0xB0) {
            const int cc = event.data[1];
            if (cc >= inLo && cc <= inHi) {
              int mapped = outLo;
              const int span = inHi - inLo;
              if (span > 0) {
                const long long numerator =
                    static_cast<long long>(cc - inLo) * (outHi - outLo);
                const long long rounded =
                    numerator >= 0 ? (numerator + span / 2) / span
                                   : (numerator - span / 2) / span;
                mapped = outLo + static_cast<int>(rounded);
              }
              event.data[1] = static_cast<std::uint8_t>(clampInt(mapped, 0, 127));
            }
          }
          append(out, outCount, dropped, event);
        }
        return;
      }

      // Channel rewriter: channel-voice messages on inCh (0 = any) move to
      // outCh (1..16). System messages (status >= 0xF0) carry no channel and
      // pass untouched.
      case PipeType::ChannelRemap: {
        const int inChannel = clampInt(param(pipe, 0), 0, 16);
        const int outChannel = clampInt(param(pipe, 1), 1, 16);
        for (std::size_t i = 0; i < inCount; ++i) {
          PipeEvent event = in[i];
          if (event.size >= 1 && event.data[0] >= 0x80 && event.data[0] < 0xF0) {
            const int channel = (event.data[0] & 0x0F) + 1;
            if (inChannel == 0 || channel == inChannel)
              event.data[0] = static_cast<std::uint8_t>((event.data[0] & 0xF0) |
                                                        (outChannel - 1));
          }
          append(out, outCount, dropped, event);
        }
        return;
      }

      // Snap note events to the selected scale: 1..5 transpose the fixed
      // scales by root; 6 is the user mask of absolute pitch classes (root
      // ignored). quantizeNote rounds ties down and octave-wraps at the
      // 0/127 edges; scale 0 (or invalid) disables the pipe.
      case PipeType::ScaleQuantize: {
        const int scale = param(pipe, 0);
        if (scale <= 0 || scale > 6) {
          for (std::size_t i = 0; i < inCount; ++i)
            append(out, outCount, dropped, in[i]);
          return;
        }
        const int root = clampInt(param(pipe, 1), 0, 11);
        const auto mask12 =
            static_cast<std::uint16_t>(param(pipe, 2) & 0x0FFF);
        for (std::size_t i = 0; i < inCount; ++i) {
          PipeEvent event = in[i];
          const int kind = kindOf(event);
          if (kind == kNoteOn || kind == kNoteOff) {
            int quantized = event.data[1];
            if (quantizeNote(event.data[1], scale, root, mask12, quantized))
              event.data[1] = static_cast<std::uint8_t>(clampInt(quantized, 0, 127));
          }
          append(out, outCount, dropped, event);
        }
        return;
      }

      // Chance drop: a note-on is dropped when rng % 100 < percent, i.e.
      // percent is the drop chance (0 disables, 100 drops every note-on).
      // Note-offs and other kinds always pass.
      case PipeType::Chance: {
        const int percent = clampInt(param(pipe, 0), 0, 100);
        for (std::size_t i = 0; i < inCount; ++i) {
          if (kindOf(in[i]) == kNoteOn && percent > 0 &&
              static_cast<int>(nextRandom() % 100u) < percent)
            continue;
          append(out, outCount, dropped, in[i]);
        }
        return;
      }

      // Humanize: each note-on velocity gains a uniform random delta in
      // [-amount,+amount], clamped to [minVel,127]. Other messages pass.
      case PipeType::VelocityRandom: {
        const int amount = clampInt(param(pipe, 0), 0, 127);
        const int floorVelocity = clampInt(param(pipe, 1), 0, 127);
        for (std::size_t i = 0; i < inCount; ++i) {
          PipeEvent event = in[i];
          if (kindOf(event) == kNoteOn) {
            int delta = 0;
            if (amount > 0) {
              const int span = 2 * amount + 1;
              delta = static_cast<int>(nextRandom() %
                                       static_cast<std::uint32_t>(span)) -
                      amount;
            }
            const int velocity = clampInt(static_cast<int>(event.data[2]) + delta,
                                          floorVelocity, 127);
            event.data[2] = static_cast<std::uint8_t>(velocity);
          }
          append(out, outCount, dropped, event);
        }
        return;
      }

      // Round-robin spread: successive note-ons rotate across numCh channels
      // starting at startCh (wrapping within 1..16). The (input channel, note)
      // assignment is remembered so the matching note-off follows it to the
      // same channel; other messages pass.
      case PipeType::Dispatcher: {
        const int numChannels = clampInt(param(pipe, 0), 2, 16);
        const int startChannel = clampInt(param(pipe, 1), 1, 16);
        for (std::size_t i = 0; i < inCount; ++i) {
          PipeEvent event = in[i];
          const int kind = kindOf(event);
          if (kind == kNoteOn || kind == kNoteOff) {
            const std::size_t inputChannel =
                static_cast<std::size_t>(event.data[0] & 0x0F);
            const std::uint8_t note = event.data[1];
            if (kind == kNoteOn) {
              const int slot = static_cast<int>(
                  dispatch_index_ % static_cast<std::uint32_t>(numChannels));
              const int channel = 1 + ((startChannel - 1 + slot) % 16);
              note_channel_[inputChannel][note] = static_cast<std::uint8_t>(channel);
              ++dispatch_index_;
              event.data[0] = static_cast<std::uint8_t>((event.data[0] & 0xF0) |
                                                        (channel - 1));
            } else {
              const std::uint8_t assigned = note_channel_[inputChannel][note];
              if (assigned != 0) {
                event.data[0] = static_cast<std::uint8_t>((event.data[0] & 0xF0) |
                                                          (assigned - 1));
                note_channel_[inputChannel][note] = 0;
              }
            }
          }
          append(out, outCount, dropped, event);
        }
        return;
      }

      // CC64 latch. Pedal value >= 64 latches: note-offs arriving while
      // latched are captured (held) instead of passed. Pedal value < 64
      // releases, emitting every held note-off ahead of the pedal message.
      // latch 0 disables the pipe entirely.
      case PipeType::Sustain: {
        const bool latchEnabled = param(pipe, 0) != 0;
        if (!latchEnabled) {
          for (std::size_t i = 0; i < inCount; ++i)
            append(out, outCount, dropped, in[i]);
          return;
        }
        for (std::size_t i = 0; i < inCount; ++i) {
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
                  append(out, outCount, dropped, off);
                }
              }
              for (auto& channel : sustain_held_) channel.fill(0);
            }
            append(out, outCount, dropped, event);
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
            sustain_held_[channel][word] &= ~bit;
            append(out, outCount, dropped, event);
            continue;
          }
          append(out, outCount, dropped, event);
        }
        return;
      }

      // Fixed note length: the note-on passes and a matching note-off is
      // scheduled for nowMs + lengthMs (capped at 60 s), re-entering the chain
      // at this pipe's successor via tick(). lengthMs <= 0 disables.
      case PipeType::NoteLength: {
        int lengthMs = param(pipe, 0);
        if (lengthMs <= 0) {
          for (std::size_t i = 0; i < inCount; ++i)
            append(out, outCount, dropped, in[i]);
          return;
        }
        if (lengthMs > 60000) lengthMs = 60000;
        for (std::size_t i = 0; i < inCount; ++i) {
          PipeEvent event = in[i];
          const int kind = kindOf(event);
          if (kind == kNoteOn) {
            append(out, outCount, dropped, event);
            scheduleNoteOff(event, pipeIndex, lengthMs, nowMs);
          } else if (kind == kNoteOff) {
            // A fixed-length note ignores the incoming off and waits for the
            // scheduled one; otherwise a short key press would defeat the pipe.
            if (!hasPending(event.data[0] & 0x0F, event.data[1]))
              append(out, outCount, dropped, event);
          } else {
            append(out, outCount, dropped, event);
          }
        }
        return;
      }

      // Message conversion: matchKind 0 note, 1 cc, 2 pc, 3 bend (14-bit
      // value), 4 pressure; a match with the value in [matchLo,matchHi]
      // converts to outKind/outVal (0 noteOn, 1 cc, 2 pc, 3 bend 14-bit,
      // 4 pressure). Non-matches pass unchanged.
      case PipeType::Transform: {
        // A partial config without an output kind is not a conversion; leave
        // the bytes alone instead of silently retuning every note to 0.
        if (pipe.paramCount < 4) {
          for (std::size_t i = 0; i < inCount; ++i)
            append(out, outCount, dropped, in[i]);
          return;
        }
        const int matchKind = clampInt(param(pipe, 0), 0, 4);
        const bool bendMatch = matchKind == 3;
        const int matchScale = bendMatch ? 16383 : 127;
        int matchLo = clampInt(param(pipe, 1), 0, matchScale);
        // A missing matchHi covers the whole match range of the selected kind
        // (a bend default of 127 would only match the low 1 % of travel).
        int matchHi = pipe.paramCount > 2 ? clampInt(param(pipe, 2), 0, matchScale)
                                          : matchScale;
        if (matchLo > matchHi) {
          const int swap = matchLo;
          matchLo = matchHi;
          matchHi = swap;
        }
        const int outKind = clampInt(param(pipe, 3), 0, 4);
        const int outValue = clampInt(param(pipe, 4), 0, 16383);
        for (std::size_t i = 0; i < inCount; ++i) {
          PipeEvent event = in[i];
          if (!transformMatches(event, matchKind, matchLo, matchHi)) {
            append(out, outCount, dropped, event);
            continue;
          }
          PipeEvent converted;
          if (makeTransform(event, outKind, outValue, converted))
            append(out, outCount, dropped, converted);
          else
            append(out, outCount, dropped, event);
        }
        return;
      }

      // Arpeggiator: held note-ons are consumed (never passed on), the notes
      // played are generated on the division grid by clockTick(). The set is
      // per channel/note, sorted, and a change to it restarts the pattern from
      // its first entry. Releasing every held note stops the pipe and releases
      // whatever it has sounding.
      case PipeType::Arp: {
        ClockedPipeState& state = clocked_[pipeIndex];
        for (std::size_t i = 0; i < inCount; ++i) {
          PipeEvent event = in[i];
          const int kind = kindOf(event);
          if (kind == kNoteOn) {
            holdNote(state, event);
            continue;
          }
          if (kind == kNoteOff) {
            state.held.remove(static_cast<std::uint8_t>(event.data[0] & 0x0F),
                              event.data[1]);
            if (state.held.count() == 0)
              stopSequencer(state, out, outCount, dropped);
            else
              state.cursor = 0;
            continue;
          }
          append(out, outCount, dropped, event);
        }
        return;
      }

      // Repeater/ratchet: the same held-note engine as the arp, but every
      // division step retriggers the whole held set instead of walking a
      // pattern, with the falloff envelope restarting every `repeats` steps.
      case PipeType::Repeater: {
        ClockedPipeState& state = clocked_[pipeIndex];
        for (std::size_t i = 0; i < inCount; ++i) {
          PipeEvent event = in[i];
          const int kind = kindOf(event);
          if (kind == kNoteOn) {
            holdNote(state, event);
            continue;
          }
          if (kind == kNoteOff) {
            state.held.remove(static_cast<std::uint8_t>(event.data[0] & 0x0F),
                              event.data[1]);
            if (state.held.count() == 0)
              stopSequencer(state, out, outCount, dropped);
            continue;
          }
          append(out, outCount, dropped, event);
        }
        return;
      }

      // CcLfo: emits its own CC values on the division grid and never touches
      // the routed bytes.
      case PipeType::CcLfo:
        for (std::size_t i = 0; i < inCount; ++i)
          append(out, outCount, dropped, in[i]);
        return;

      // Harmonizer: the input note passes through and each voice is added as an
      // extra note-on. Refcounts make offs follow their ons, including when two
      // held notes map onto the same output note (hold C with +4 while E is
      // held: E is released once, by whichever input note is released last).
      // A retriggered note-on (same channel/note still active) does not stack
      // voices. Position relative to a Sustain pipe decides what the harmonizer
      // sees: after it, input offs arrive when CC64 lifts, so the voices
      // release together with the input note; before it, the harmonizer emits
      // voice offs that the Sustain then latches like any other note-off.
      case PipeType::Harmonizer: {
        ClockedPipeState& state = clocked_[pipeIndex];
        const int voices = clampInt(param(pipe, 0), 0,
                                    static_cast<int>(kMaxHarmonyVoices));
        if (voices == 0) {
          for (std::size_t i = 0; i < inCount; ++i)
            append(out, outCount, dropped, in[i]);
          return;
        }
        std::array<int, kMaxHarmonyVoices> offsets{};
        for (std::size_t i = 0; i < offsets.size(); ++i)
          offsets[i] = clampInt(param(pipe, 1 + i), -48, 48);
        for (std::size_t i = 0; i < inCount; ++i) {
          PipeEvent event = in[i];
          const int kind = kindOf(event);
          const std::uint8_t channel =
              static_cast<std::uint8_t>(event.data[0] & 0x0F);
          std::array<std::uint8_t, kMaxHarmonyVoices + 1> notes{};
          const std::size_t noteCount =
              harmonyOutputs(event.data[1], voices, offsets, notes);
          if (kind == kNoteOn) {
            // A retrigger of a note this pipe already holds is a dry note-on
            // only: stacking its voices again would need two note-offs.
            const bool retrigger = state.held.contains(channel, event.data[1]);
            append(out, outCount, dropped, event);
            if (retrigger) continue;
            state.held.add(PipeNote{channel, event.data[1], event.data[2]});
            harmonyAdd(state, channel, notes[0]);
            for (std::size_t v = 1; v < noteCount; ++v) {
              if (!harmonyAdd(state, channel, notes[v])) break;
              PipeEvent voice = event;
              voice.data[1] = notes[v];
              append(out, outCount, dropped, voice);
            }
            continue;
          }
          if (kind == kNoteOff && state.held.remove(channel, event.data[1])) {
            // When the dry note-on was emitted untracked (reference table full)
            // its off has to pass through, or the note would stick.
            const bool dryTracked = harmonyActive(state, channel, notes[0]);
            for (std::size_t v = 0; v < noteCount; ++v) {
              if (!harmonyRelease(state, channel, notes[v])) continue;
              PipeEvent voice = event;
              voice.data[1] = notes[v];
              append(out, outCount, dropped, voice);
            }
            if (!dryTracked) append(out, outCount, dropped, event);
            continue;
          }
          append(out, outCount, dropped, event);
        }
        return;
      }

      // MidiDelay: the dry event passes and `repeats` echoes are queued, each
      // one delayMs later and attenuated by falloff. tick() replays them, so a
      // note-off echo trails its note-on echo by the original gate length and
      // the echo tail never re-enters this pipe.
      case PipeType::MidiDelay: {
        const int delayMs =
            clampInt(param(pipe, 0), 0, static_cast<int>(kMaxDelayMs));
        const int repeats = clampInt(param(pipe, 1), 0, kMaxDelayRepeats);
        for (std::size_t i = 0; i < inCount; ++i) {
          append(out, outCount, dropped, in[i]);
          if (delayMs == 0 || repeats == 0) continue;
          const int kind = kindOf(in[i]);
          if (kind != kNoteOn && kind != kNoteOff && kind != kCc) continue;
          queueEcho(pipeIndex, in[i], repeats,
                    nowMs + static_cast<std::uint32_t>(delayMs));
        }
        return;
      }
    }
    // Only an id outside the wire's 1..16 range could reach here (a defensive
    // passthrough; setPipeChunk/configure reject those ids).
    for (std::size_t i = 0; i < inCount; ++i)
      append(out, outCount, dropped, in[i]);
  }

  static bool transformMatches(const PipeEvent& event, int matchKind, int lo,
                               int hi) {
    const int kind = kindOf(event);
    int value = event.data[1];
    if (matchKind == 0) {
      if (kind != kNoteOn && kind != kNoteOff) return false;
    } else if (matchKind == 1) {
      if (kind != kCc) return false;
    } else if (matchKind == 2) {
      if (kind != kPc) return false;
    } else if (matchKind == 3) {
      if (kind != kBend || event.size < 3) return false;
      value = static_cast<int>(event.data[1]) |
              (static_cast<int>(event.data[2]) << 7);
    } else if (matchKind == 4) {
      if (kind != kPressure) return false;
    } else {
      return false;
    }
    return value >= lo && value <= hi;
  }

  static bool makeTransform(const PipeEvent& source, int outKind, int outValue,
                            PipeEvent& out) {
    const std::uint8_t channel = source.data[0] & 0x0F;
    const int sourceKind = kindOf(source);
    switch (outKind) {
      case 0: {
        out.size = 3;
        out.data[0] = static_cast<std::uint8_t>(0x90 | channel);
        out.data[1] = static_cast<std::uint8_t>(clampInt(outValue, 0, 127));
        out.data[2] = static_cast<std::uint8_t>(
            sourceKind == kNoteOn ? source.data[2]
                                  : (sourceKind == kNoteOff ? 0 : 100));
        return true;
      }
      case 1: {
        out.size = 3;
        out.data[0] = static_cast<std::uint8_t>(0xB0 | channel);
        out.data[1] = static_cast<std::uint8_t>(clampInt(outValue, 0, 127));
        out.data[2] = source.size >= 3 ? source.data[2] : 127;
        return true;
      }
      case 2: {
        out.size = 2;
        out.data[0] = static_cast<std::uint8_t>(0xC0 | channel);
        out.data[1] = static_cast<std::uint8_t>(clampInt(outValue, 0, 127));
        return true;
      }
      case 3: {
        const int bend = clampInt(outValue, 0, 16383);
        out.size = 3;
        out.data[0] = static_cast<std::uint8_t>(0xE0 | channel);
        out.data[1] = static_cast<std::uint8_t>(bend & 0x7F);
        out.data[2] = static_cast<std::uint8_t>((bend >> 7) & 0x7F);
        return true;
      }
      case 4: {
        out.size = 2;
        out.data[0] = static_cast<std::uint8_t>(0xD0 | channel);
        out.data[1] = static_cast<std::uint8_t>(clampInt(outValue, 0, 127));
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

  void scheduleNoteOff(const PipeEvent& noteOn, std::size_t pipeIndex,
                       int lengthMs, std::uint32_t nowMs) {
    const std::uint8_t channel = noteOn.data[0] & 0x0F;
    const std::uint8_t note = noteOn.data[1];
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
    slot->dueMs = nowMs + static_cast<std::uint32_t>(lengthMs);
    slot->stage = static_cast<std::uint8_t>(pipeIndex + 1);
    slot->channel = channel;
    slot->note = note;
  }

  // -------------------------------------------------------------------------
  // Clocked pipes. Generated events go through runGenerated(), which re-enters
  // the chain after the producing pipe. That is only correct from
  // tick()/clockTick(): inside runPipe() the enclosing pass already forwards a
  // pipe's output through the remaining stages, so the *_offs helpers below
  // append there instead of injecting.
  // -------------------------------------------------------------------------
  void runGenerated(std::size_t pipeIndex, const PipeEvent& event,
                    std::uint32_t nowMs, PipeOutput& out) {
    mergeOutput(out, runFrom(pipeIndex + 1, event, nowMs));
  }

  static PipeEvent noteOffEvent(const PipeNote& note) {
    PipeEvent off;
    off.size = 3;
    off.data[0] = static_cast<std::uint8_t>(0x80 | note.channel);
    off.data[1] = note.note;
    off.data[2] = 0;
    return off;
  }

  // Inside a pipe pass: the enclosing runFrom() forwards these offs.
  void appendSoundingOffs(ClockedPipeState& state, PipeEvent* out,
                          std::size_t& outCount, std::size_t& dropped) {
    for (std::size_t i = 0; i < state.sounding.count(); ++i)
      append(out, outCount, dropped, noteOffEvent(state.sounding.at(i)));
    state.sounding.clear();
    state.gatePending = false;
  }

  // From tick()/clockTick(): release through the pipe's successors while the
  // chain runs stages, raw when the whole route is off (no stage would run).
  void emitRelease(const PipeNote& note, std::size_t pipeIndex,
                   std::uint32_t nowMs, PipeOutput& out) {
    if (on_)
      runGenerated(pipeIndex, noteOffEvent(note), nowMs, out);
    else
      out.push(noteOffEvent(note));
  }

  void injectSoundingOffs(ClockedPipeState& state, std::size_t pipeIndex,
                          std::uint32_t nowMs, PipeOutput& out) {
    for (std::size_t i = 0; i < state.sounding.count(); ++i)
      emitRelease(state.sounding.at(i), pipeIndex, nowMs, out);
    state.sounding.clear();
    state.gatePending = false;
  }

  // A chain edit dropped the runtime state while notes were on; release them
  // before anything else so an edit can never leave a note sounding forever.
  void releaseOrphans(std::uint32_t nowMs, PipeOutput& out) {
    for (std::size_t index = 0; index < kMaxRoutePipes; ++index) {
      ClockedPipeState& state = clocked_[index];
      if (!state.orphaned) continue;
      state.orphaned = false;
      // Sounding notes first (arp/repeater gates and delay echoes), then the
      // harmonizer's reference-counted outputs.
      injectSoundingOffs(state, index, nowMs, out);
      for (auto& entry : state.harmony) {
        if (!entry.used) continue;
        const PipeNote note{entry.channel, entry.note, 0};
        entry = ClockedPipeState::HarmonyEntry{};
        emitRelease(note, index, nowMs, out);
      }
    }
  }

  // A held note-on is consumed by the arp/repeater and restarts the pattern
  // when the held set changes. At kMaxHeldNotes the extra note-on is dropped
  // together with its note-off (documented cap), never half tracked.
  void holdNote(ClockedPipeState& state, const PipeEvent& event) {
    const PipeNote note{static_cast<std::uint8_t>(event.data[0] & 0x0F),
                        event.data[1], event.data[2]};
    const bool known = state.held.contains(note.channel, note.note);
    if (!state.held.add(note) || known) return;
    state.cursor = 0;
  }

  // The last held note is gone: stop the grid and release everything the pipe
  // turned on ("all notes off on empty hold set").
  void stopSequencer(ClockedPipeState& state, PipeEvent* out,
                     std::size_t& outCount, std::size_t& dropped) {
    state.stepping = false;
    state.cursor = 0;
    state.burst = 0;
    appendSoundingOffs(state, out, outCount, dropped);
  }

  // One note-on for a clocked pipe: tracked in the sounding set first, so a
  // full set drops the note instead of stranding it, then re-injected.
  void emitClockedNote(const PipeNote& note, int accent, std::size_t pipeIndex,
                       std::uint32_t nowMs, PipeOutput& out) {
    PipeNote tracked = note;
    const int boost = clampInt(accent, 0, 100);
    if (boost > 0)
      tracked.velocity = static_cast<std::uint8_t>(
          clampInt((static_cast<int>(note.velocity) * (100 + boost) + 50) / 100,
                   1, 127));
    if (!clocked_[pipeIndex].sounding.add(tracked)) return;
    PipeEvent on;
    on.size = 3;
    on.data[0] = static_cast<std::uint8_t>(0x90 | tracked.channel);
    on.data[1] = tracked.note;
    on.data[2] = tracked.velocity;
    runGenerated(pipeIndex, on, nowMs, out);
  }

  // Gate note-offs of one arp/repeater step, emitted when their tick arrives so
  // a step never retriggers a note that is still sounding.
  void releaseGate(std::size_t pipeIndex, std::uint32_t tickIndex,
                   std::uint32_t nowMs, PipeOutput& out) {
    ClockedPipeState& state = clocked_[pipeIndex];
    if (!state.gatePending) return;
    if (static_cast<std::int32_t>(tickIndex - state.gateTick) < 0) return;
    injectSoundingOffs(state, pipeIndex, nowMs, out);
  }

  // Shared Arp/Repeater engine: the held set decides whether the grid runs, the
  // division and swing decide when, and the pipe type decides what a step
  // emits. state.step is the absolute grid index of the next step, so the
  // phase survives a config edit and never drifts from the caller's tick.
  void stepSequencer(const PipeConfig& pipe, std::size_t pipeIndex,
                     std::uint32_t tickIndex, std::uint32_t nowMs,
                     PipeOutput& out) {
    ClockedPipeState& state = clocked_[pipeIndex];
    if (state.held.count() == 0) {
      state.stepping = false;
      return;
    }
    const bool repeater = pipe.type == PipeType::Repeater;
    const std::uint32_t stepTicks = divTicks(param(pipe, 0));
    const std::int32_t swing =
        repeater ? 0 : swingTicks(param(pipe, 3), stepTicks);
    if (!state.stepping) {
      // The first step lands on the next division boundary after the first
      // note-on: a clocked pipe never jumps the grid.
      state.stepping = true;
      state.step = stepIndex(tickIndex, stepTicks) + 1;
      state.cursor = 0;
      state.burst = 0;
    }
    const std::uint32_t nowStep = stepIndex(tickIndex, stepTicks);
    // A caller whose tick counter restarts on transport START makes a stored
    // step further ahead than one division stale: rejoin the current grid.
    if (state.step > nowStep + 1) state.step = nowStep;
    for (std::size_t guard = 0; guard < kMaxStepsPerCall; ++guard) {
      if (stepDueTick(state.step, stepTicks, swing) >
          static_cast<std::int64_t>(tickIndex))
        return;
      injectSoundingOffs(state, pipeIndex, nowMs, out);
      if (repeater)
        emitRepeaterStep(pipe, state, pipeIndex, tickIndex, stepTicks, nowMs, out);
      else
        emitArpStep(pipe, state, pipeIndex, state.step, tickIndex, nowMs, out);
      ++state.step;
    }
    // Still behind after kMaxStepsPerCall: drop the backlog and rejoin the grid.
    state.step = stepIndex(tickIndex, stepTicks) + 1;
  }

  void emitArpStep(const PipeConfig& pipe, ClockedPipeState& state,
                   std::size_t pipeIndex, std::uint32_t step,
                   std::uint32_t tickIndex, std::uint32_t nowMs, PipeOutput& out) {
    const int type = clampInt(param(pipe, 1), 0, 4);
    const int octaves = clampInt(param(pipe, 2), 1, 4);
    const int accent = clampInt(param(pipe, 4), 0, 100);
    std::array<PipeNote, kMaxArpPattern> pattern{};
    const std::size_t size = buildArpPattern(state.held.data(), state.held.count(),
                                             type, octaves, pattern);
    if (size == 0) return;
    // The accent marks the grid's beat (every fourth step), not a pattern
    // position, so arps with different pattern lengths accent together.
    const int boost = (step & 3u) == 0 ? accent : 0;
    std::size_t first = 0;
    std::size_t last = size;  // chord: every base-octave held note at once
    if (type != 4) {
      std::size_t pick = state.cursor % size;
      if (type == 3) pick = static_cast<std::size_t>(nextRandom()) % size;
      first = pick;
      last = pick + 1;
      ++state.cursor;
    }
    for (std::size_t i = first; i < last; ++i)
      emitClockedNote(pattern[i], boost, pipeIndex, nowMs, out);
    state.gatePending = true;
    state.gateTick = tickIndex + gateTicks(param(pipe, 5), divTicks(param(pipe, 0)));
  }

  void emitRepeaterStep(const PipeConfig& pipe, ClockedPipeState& state,
                        std::size_t pipeIndex, std::uint32_t tickIndex,
                        std::uint32_t stepTicks, std::uint32_t nowMs,
                        PipeOutput& out) {
    const int repeats = clampInt(param(pipe, 1), 1, kMaxRepeatCount);
    const int falloff = clampInt(param(pipe, 2), 0, 100);
    // Strike index inside the burst: the falloff envelope restarts every
    // `repeats` steps and with every new hold (state.burst resets on start).
    const std::uint32_t strike = state.burst % static_cast<std::uint32_t>(repeats);
    for (std::size_t i = 0; i < state.held.count(); ++i) {
      PipeNote note = state.held.at(i);
      for (std::uint32_t k = 0; k < strike; ++k)
        note.velocity = attenuate(note.velocity, falloff);
      if (note.velocity == 0) note.velocity = 1;
      emitClockedNote(note, 0, pipeIndex, nowMs, out);
    }
    ++state.burst;
    state.gatePending = true;
    state.gateTick = tickIndex + (stepTicks / 2 ? stepTicks / 2 : 1u);
  }

  // CcLfo has no channel parameter in the session contract, so it emits on MIDI
  // channel 1; put a ChannelRemap after it to move the stream.
  void stepLfo(const PipeConfig& pipe, std::size_t pipeIndex,
               std::uint32_t tickIndex, std::uint32_t nowMs, PipeOutput& out) {
    ClockedPipeState& state = clocked_[pipeIndex];
    const std::uint32_t stepTicks = divTicks(param(pipe, 2));
    if (!state.stepping) {
      state.stepping = true;
      state.step = stepIndex(tickIndex, stepTicks) + 1;
    }
    const std::uint32_t nowStep = stepIndex(tickIndex, stepTicks);
    if (state.step > nowStep + 1) state.step = nowStep;
    for (std::size_t guard = 0; guard < kMaxStepsPerCall; ++guard) {
      if (stepDueTick(state.step, stepTicks, 0) >
          static_cast<std::int64_t>(tickIndex))
        return;
      PipeEvent cc;
      cc.size = 3;
      cc.data[0] = 0xB0;
      cc.data[1] = static_cast<std::uint8_t>(clampInt(param(pipe, 0), 0, 127));
      cc.data[2] = lfoValue(
          clampInt(param(pipe, 1), 0, 3),
          static_cast<std::size_t>(state.step % kLfoStepsPerCycle),
          clampInt(param(pipe, 3), 0, 127), clampInt(param(pipe, 4), 0, 127));
      runGenerated(pipeIndex, cc, nowMs, out);
      ++state.step;
    }
    state.step = nowStep + 1;
  }

  // --- MidiDelay -----------------------------------------------------------
  // Queues one echo chain. A full queue drops that echo and leaves the dry
  // event untouched (documented cap: kMaxDelayEvents simultaneous delayed
  // events, each carrying its whole repeat chain).
  void queueEcho(std::size_t pipeIndex, const PipeEvent& event, int echoes,
                 std::uint32_t dueMs) {
    if (echoes <= 0) return;
    for (auto& echo : delay_[pipeIndex]) {
      if (echo.used) continue;
      echo.used = true;
      echo.dueMs = dueMs;
      echo.size = event.size;
      for (std::size_t i = 0; i < 3; ++i) echo.data[i] = event.data[i];
      echo.echoesLeft = static_cast<std::uint8_t>(echoes);
      return;
    }
  }

  // Replays the due echoes oldest first, so a note-off echo never overtakes its
  // note-on echo in a stalled pass, and re-queues the rest of each repeat chain
  // one delay later with the falloff applied.
  void serviceDelay(const PipeConfig& pipe, std::size_t pipeIndex,
                    std::uint32_t nowMs, PipeOutput& out) {
    const int delayMs = clampInt(param(pipe, 0), 0, static_cast<int>(kMaxDelayMs));
    if (delayMs == 0) return;
    const int falloff = clampInt(param(pipe, 2), 0, 100);
    std::array<DelayEcho, kMaxDelayEvents>& queue = delay_[pipeIndex];
    for (;;) {
      DelayEcho* next = nullptr;
      for (auto& echo : queue) {
        if (!echo.used) continue;
        if (static_cast<std::int32_t>(nowMs - echo.dueMs) < 0) continue;
        if (next != nullptr &&
            static_cast<std::int32_t>(echo.dueMs - next->dueMs) >= 0)
          continue;
        next = &echo;
      }
      if (next == nullptr) return;
      const DelayEcho echo = *next;
      next->used = false;
      PipeEvent event;
      event.size = echo.size;
      for (std::size_t i = 0; i < 3; ++i) event.data[i] = echo.data[i];
      const int kind = kindOf(event);
      const std::uint8_t channel =
          static_cast<std::uint8_t>(event.data[0] & 0x0F);
      if (kind == kNoteOn) {
        // Only echo a note-on that can be tracked; an untracked echo would be
        // stranded by a chain edit (an orphaned note-off echo is harmless).
        if (!clocked_[pipeIndex].sounding.add(
                PipeNote{channel, event.data[1], event.data[2]}))
          continue;
      } else if (kind == kNoteOff) {
        clocked_[pipeIndex].sounding.remove(channel, event.data[1]);
      }
      runGenerated(pipeIndex, event, nowMs, out);
      if (echo.echoesLeft <= 1) continue;
      DelayEcho repeat = echo;
      repeat.echoesLeft = static_cast<std::uint8_t>(echo.echoesLeft - 1);
      repeat.dueMs = nowMs + static_cast<std::uint32_t>(delayMs);
      if (kind == kNoteOn || kind == kCc) {
        std::uint8_t value = attenuate(repeat.data[2], falloff);
        if (kind == kNoteOn && value == 0) value = 1;
        repeat.data[2] = value;
      }
      for (auto& slot : queue) {
        if (slot.used) continue;
        slot = repeat;
        break;
      }
    }
  }

  // --- Harmonizer refcounts ------------------------------------------------
  static std::size_t harmonyCount(const ClockedPipeState& state) {
    std::size_t count = 0;
    for (const auto& entry : state.harmony) count += entry.used ? 1u : 0u;
    return count;
  }

  static bool harmonyActive(const ClockedPipeState& state, std::uint8_t channel,
                            std::uint8_t note) {
    for (const auto& entry : state.harmony)
      if (entry.used && entry.channel == channel && entry.note == note)
        return true;
    return false;
  }

  // Adds one reference. False when the table is full: the caller then skips
  // that voice, so anything it emits stays matched on the wire.
  static bool harmonyAdd(ClockedPipeState& state, std::uint8_t channel,
                         std::uint8_t note) {
    for (auto& entry : state.harmony) {
      if (!entry.used || entry.channel != channel || entry.note != note) continue;
      if (entry.count != 0xFF) ++entry.count;
      return true;
    }
    for (auto& entry : state.harmony) {
      if (entry.used) continue;
      entry.used = true;
      entry.channel = channel;
      entry.note = note;
      entry.count = 1;
      return true;
    }
    return false;
  }

  // Drops one reference; true when it was the last one, so the caller emits the
  // note-off. This is what makes offs follow their ons through overlaps.
  static bool harmonyRelease(ClockedPipeState& state, std::uint8_t channel,
                             std::uint8_t note) {
    for (auto& entry : state.harmony) {
      if (!entry.used || entry.channel != channel || entry.note != note) continue;
      if (entry.count > 1) {
        --entry.count;
        return false;
      }
      entry = ClockedPipeState::HarmonyEntry{};
      return true;
    }
    return false;
  }

  std::array<PipeConfig, kMaxRoutePipes> pipes_{};
  std::size_t pipe_count_ = 0;
  bool on_ = false;

  std::uint32_t seed_ = kDefaultPipeSeed;
  std::uint32_t rng_state_ = kDefaultPipeSeed;

  std::uint32_t dispatch_index_ = 0;
  std::array<std::array<std::uint8_t, 128>, 16> note_channel_{};

  bool sustain_latched_ = false;
  // Two 64-bit words cover notes 0..127 for each of the 16 channels; bit n
  // in word (n >> 6) marks a note-off latched by the sustain pipe.
  std::array<std::array<std::uint64_t, 2>, 16> sustain_held_{};

  std::array<PendingOff, kMaxPendingNoteOffs> pending_{};

  // One runtime slot per chain position for the clocked pipes, plus their echo
  // queue. Bounded on purpose: kMaxHeldNotes/kMaxSoundingNotes per slot and
  // kMaxDelayEvents echoes per MidiDelay (see timing.hpp).
  std::array<ClockedPipeState, kMaxRoutePipes> clocked_{};
  std::array<std::array<DelayEcho, kMaxDelayEvents>, kMaxRoutePipes> delay_{};
};

}  // namespace mnky::midifx

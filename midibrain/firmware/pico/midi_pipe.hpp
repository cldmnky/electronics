#pragma once
// MIDI FX pipe engine for the mnkybox Pico firmware (Wave 1 stateless pipes,
// Wave 2 clocked pipes). Pure logic: no Pico SDK, host-tested in
// protocol_test.cpp. The Pi sends the session's RouteState.pipes over
// EV_MIDI_ROUTE / EV_MIDI_ROUTE_PIPE (protocol.hpp) and the routed bytes over
// EV_MIDI_ROUTE_SEND; main.cpp calls the chain in route order and writes the
// resulting bytes to the physical TRS output.
//
// Positional parameter order is the contract frozen in
// pi/session/session.hpp (MidiPipeType). Missing parameters use the defaults
// documented here; every type id the wire accepts (1..16) is evaluated.
//
// Wave 1 (1..11) is the session's stateless set: no tempo, transport or
// scheduled timeline. A chain owns only the minimum pairing state its pipes
// need: Dispatcher note->channel assignments, Sustain's held-note latch,
// NoteLength's pending note-offs and the Chance/VelocityRandom RNG.
//
// Wave 2 (12..16) is clocked and held-note driven: Arp, Repeater, CcLfo,
// Harmonizer and MidiDelay. Their timing comes from the Pico tick alarm (the
// Pico is the clock master), handed to clockTick() as the absolute tick index;
// see midi_clock_pipes.hpp for the division/gate/LFO math. Events they generate
// re-enter the chain after their own pipe, exactly like NoteLength's scheduled
// note-offs, so later pipes still see them. Their committed note-ons are always
// released: on the empty hold set, on bypass, and when a chain edit drops the
// runtime state (tick() flushes those). MidiDelay is the only pipe that uses
// milliseconds; its parameter is delayMs and its echo queue is bounded.
#include <array>
#include <cstddef>
#include <cstdint>

#include "midi.hpp"
#include "midi_clock_pipes.hpp"

namespace mb {
inline constexpr std::size_t kMaxPipeParams = route_pipe_param_max;
inline constexpr std::size_t kMaxRoutePipes = route_pipe_count_max;
// One evaluation can emit multiple events (Sustain releases its held notes).
// 64 covers every realistic held-note set without heap allocation.
inline constexpr std::size_t kMaxPipeEvents = 64;
inline constexpr std::size_t kMaxPendingNoteOffs = 64;

// Stable ids from pi/session/session.hpp. 1..11 are the Wave-1 stateless pipes,
// 12..16 the Wave-2 clocked ones.
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
  Arp = 12,         // [div 0..3; type 0 up,1 down,2 updown,3 random,4 chord;
                    //  octaves 1..4; swing 0..100; accent 0..100; gatePct 1..100]
  Repeater = 13,    // [div 0..3; repeats 1..8; falloff 0..100]
  CcLfo = 14,       // [cc 0..127; wave 0 sine,1 tri,2 square,3 saw; div 0..3;
                    //  depth 0..127; center 0..127]
  Harmonizer = 15,  // [n 0..4 voices; i1..i4 semitone offsets -48..48]
  MidiDelay = 16,   // [delayMs 0..2000; repeats 0..8; falloff 0..100]
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
// (with outKind 0..4, outVal defaults to 0). Arp 1/4, up, 1 octave, straight
// swing, no accent, 50 % gate; Repeater 1/4, 1 repeat, no falloff; CcLfo CC 1
// sine at 1/4 with depth 0 (holds centre 64, so it is silent until configured);
// Harmonizer 0 voices (pass through); MidiDelay delayMs 0 (off) with 0 repeats.
// Values are clamped to each pipe's documented range before use.

// One route's ordered pipe chain plus its runtime state. The route is enabled
// by begin(true); append/setPipeChunk configure pipes after it. Any config
// change resets the runtime state so a stale dispatcher/sustain/note-length
// decision never leaks into the new chain. Clocked pipes release the notes they
// had turned on before that reset clears them (see tick()).
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

  // Emits note-offs whose fixed NoteLength has elapsed, the echoes a MidiDelay
  // pipe has queued, and the note-offs of clocked pipes whose notes an earlier
  // config change orphaned. Each one re-enters the chain at its pipe's
  // successor, so later pipes (for example ChannelRemap or Sustain) still see
  // it. now_ms is the millisecond clock; call it every main-loop pass.
  PipeOutput tick(std::uint32_t now_ms) {
    PipeOutput out;
    releaseOrphans(now_ms, out);
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
    for (std::size_t index = 0; index < pipe_count_; ++index) {
      const Pipe& p = pipes_[index];
      if (p.bypass || p.type != MidiPipeType::MidiDelay) continue;
      serviceDelay(p, index, now_ms, out);
    }
    return out;
  }

  // Advances the clocked pipes on the Pico's own tick alarm. tick_index is the
  // absolute clock tick count (24 per quarter note), which is the only clock a
  // clocked pipe uses: division steps, swing, gates, LFO phase and the arp
  // accent grid are all tick indexes. Every generated event re-enters the chain
  // after its own pipe. Call it every main-loop pass with the latest tick.
  PipeOutput clockTick(std::uint32_t tick_index, std::uint32_t now_ms) {
    PipeOutput out;
    releaseOrphans(now_ms, out);
    if (!on_) return out;
    for (std::size_t index = 0; index < pipe_count_; ++index) {
      const Pipe& p = pipes_[index];
      if (p.bypass) continue;
      switch (p.type) {
        case MidiPipeType::Arp:
        case MidiPipeType::Repeater:
          releaseGate(index, tick_index, now_ms, out);
          stepSequencer(p, index, tick_index, now_ms, out);
          break;
        case MidiPipeType::CcLfo:
          stepLfo(p, index, tick_index, now_ms, out);
          break;
        default:
          break;
      }
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
    // Clocked pipes forget their held notes and grid, but the note-ons they
    // already put on the wire stay committed: tick() releases them before
    // anything else, so editing a chain cannot leave a note sounding forever.
    // Queued MidiDelay echoes are dropped with the rest of the config; their
    // already-emitted note-ons are released through the same path.
    for (std::size_t index = 0; index < kMaxRoutePipes; ++index) {
      ClockedPipeState& state = clocked_[index];
      state.orphaned = state.sounding.count() != 0 || harmonyCount(state) != 0;
      state.held.clear();
      state.stepping = false;
      state.step = 0;
      state.cursor = 0;
      state.burst = 0;
      state.gate_pending = false;
      for (auto& echo : delay_[index]) echo = DelayEcho{};
    }
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

  static int clampInt(int v, int lo, int hi) { return pipeClamp(v, lo, hi); }

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

  // One pipe's pass: transforms in[0..in_count) into out (out_count resets
  // first). A bypassed pipe copies its input through unchanged, and so does an
  // id outside the wire's 1..16 range. pipe_index is this pipe's chain position
  // (NoteLength schedules its offs to re-enter at index + 1, clocked pipes
  // re-inject their generated events there); now_ms is the monotonic ms clock.
  // Appends past kMaxPipeEvents drop silently (append()).
  void runPipe(const Pipe& p, std::size_t pipe_index,
               const std::array<PipeEvent, kMaxPipeEvents>& in,
               std::size_t in_count,
               std::array<PipeEvent, kMaxPipeEvents>& out,
               std::size_t& out_count, std::uint32_t now_ms) {
    out_count = 0;
    if (p.bypass) {
      for (std::size_t i = 0; i < in_count; ++i) append(out, out_count, in[i]);
      return;
    }
    switch (p.type) {
      // Kind gate: params[0..7] in Kind order -- [noteOn noteOff cc pc press
      // bend realtime sysex] -- where 1 (the default) passes and 0 drops.
      // System/common messages (kOther) have no mask bit and always pass.
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

      // Note window [lo,hi] (reversed bounds swap) with a note-on velocity
      // window [velLo,velHi]. Out-of-window notes drop; note-offs test only
      // the note window, other messages pass.
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

      // Linear CC-number remap: controllers in [inLo,inHi] rescale onto
      // [outLo,outHi], rounded half away from zero. CCs outside the input
      // window and non-CC messages pass unchanged.
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

      // Channel rewriter: channel-voice messages on inCh (0 = any) move to
      // outCh (1..16). System messages (status >= 0xF0) carry no channel and
      // pass untouched.
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

      // Snap note events to the selected scale: 1..5 transpose the fixed
      // scales by root; 6 is the user mask of absolute pitch classes (root
      // ignored). quantizeNote rounds ties down and octave-wraps at the
      // 0/127 edges; scale 0 (or invalid) disables the pipe.
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

      // Chance drop: a note-on is dropped when rng % 100 < percent, i.e.
      // percent is the drop chance (0 disables, 100 drops every note-on).
      // Note-offs and other kinds always pass.
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

      // Humanize: each note-on velocity gains a uniform random delta in
      // [-amount,+amount], clamped to [minVel,127]. Other messages pass.
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

      // Round-robin spread: successive note-ons rotate across numCh
      // channels starting at startCh (wrapping within 1..16). The (input
      // channel, note) assignment is remembered so the matching note-off
      // follows it to the same channel; other messages pass.
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

      // CC64 latch. Pedal value >= 64 latches: note-offs arriving while
      // latched are captured (held) instead of passed. Pedal value < 64
      // releases, emitting every held note-off ahead of the pedal message.
      // latch 0 disables the pipe entirely.
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

      // Fixed note length: the note-on passes and a matching note-off is
      // scheduled for now_ms + lengthMs (capped at 60 s), re-entering the
      // chain at this pipe's successor via tick(). lengthMs <= 0 disables.
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

      // Message conversion: matchKind 0 note, 1 cc, 2 pc, 3 bend (14-bit
      // value), 4 pressure; a match with the value in [matchLo,matchHi]
      // converts to outKind/outVal (0 noteOn, 1 cc, 2 pc, 3 bend 14-bit,
      // 4 pressure -- see makeTransform). Non-matches pass unchanged.
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

      // Arpeggiator: held note-ons are consumed (never passed on), the notes
      // played are generated on the division grid by clockTick(). The set is
      // per channel/note, sorted, and a change to it restarts the pattern from
      // its first entry. Releasing every held note stops the pipe and releases
      // whatever it has sounding.
      case MidiPipeType::Arp: {
        ClockedPipeState& state = clocked_[pipe_index];
        for (std::size_t i = 0; i < in_count; ++i) {
          PipeEvent e = in[i];
          const int kind = kindOf(e);
          if (kind == kNoteOn) {
            holdNote(state, e);
            continue;
          }
          if (kind == kNoteOff) {
            state.held.remove(static_cast<std::uint8_t>(e.data[0] & 0x0F),
                              e.data[1]);
            if (state.held.count() == 0)
              stopSequencer(state, out, out_count);
            else
              state.cursor = 0;
            continue;
          }
          append(out, out_count, e);
        }
        return;
      }

      // Repeater/ratchet: the same held-note engine as the arp, but every
      // division step retriggers the whole held set instead of walking a
      // pattern, with the falloff envelope restarting every `repeats` steps.
      case MidiPipeType::Repeater: {
        ClockedPipeState& state = clocked_[pipe_index];
        for (std::size_t i = 0; i < in_count; ++i) {
          PipeEvent e = in[i];
          const int kind = kindOf(e);
          if (kind == kNoteOn) {
            holdNote(state, e);
            continue;
          }
          if (kind == kNoteOff) {
            state.held.remove(static_cast<std::uint8_t>(e.data[0] & 0x0F),
                              e.data[1]);
            if (state.held.count() == 0)
              stopSequencer(state, out, out_count);
            continue;
          }
          append(out, out_count, e);
        }
        return;
      }

      // CcLfo: emits its own CC values on the division grid and never touches
      // the routed bytes.
      case MidiPipeType::CcLfo:
        for (std::size_t i = 0; i < in_count; ++i) append(out, out_count, in[i]);
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
      case MidiPipeType::Harmonizer: {
        ClockedPipeState& state = clocked_[pipe_index];
        const int voices = clampInt(param(p, 0, 0), 0,
                                    static_cast<int>(kMaxHarmonyVoices));
        if (voices == 0) {
          for (std::size_t i = 0; i < in_count; ++i) append(out, out_count, in[i]);
          return;
        }
        std::array<int, kMaxHarmonyVoices> offsets{};
        for (std::size_t i = 0; i < offsets.size(); ++i)
          offsets[i] = clampInt(param(p, 1 + i, 0), -48, 48);
        for (std::size_t i = 0; i < in_count; ++i) {
          PipeEvent e = in[i];
          const int kind = kindOf(e);
          const std::uint8_t channel =
              static_cast<std::uint8_t>(e.data[0] & 0x0F);
          std::array<std::uint8_t, kMaxHarmonyVoices + 1> notes{};
          const std::size_t note_count =
              harmonyOutputs(e.data[1], voices, offsets, notes);
          if (kind == kNoteOn) {
            // A retrigger of a note this pipe already holds is a dry note-on
            // only: stacking its voices again would need two note-offs.
            const bool retrigger = state.held.contains(channel, e.data[1]);
            append(out, out_count, e);
            if (retrigger) continue;
            state.held.add(PipeNote{channel, e.data[1], e.data[2]});
            harmonyAdd(state, channel, notes[0]);
            for (std::size_t v = 1; v < note_count; ++v) {
              if (!harmonyAdd(state, channel, notes[v])) break;
              PipeEvent voice = e;
              voice.data[1] = notes[v];
              append(out, out_count, voice);
            }
            continue;
          }
          if (kind == kNoteOff && state.held.remove(channel, e.data[1])) {
            // When the dry note-on was emitted untracked (reference table full)
            // its off has to pass through, or the note would stick.
            const bool dry_tracked = harmonyActive(state, channel, notes[0]);
            for (std::size_t v = 0; v < note_count; ++v) {
              if (!harmonyRelease(state, channel, notes[v])) continue;
              PipeEvent voice = e;
              voice.data[1] = notes[v];
              append(out, out_count, voice);
            }
            if (!dry_tracked) append(out, out_count, e);
            continue;
          }
          append(out, out_count, e);
        }
        return;
      }

      // MidiDelay: the dry event passes and `repeats` echoes are queued, each
      // one delayMs later and attenuated by falloff. tick() replays them, so a
      // note-off echo trails its note-on echo by the original gate length and
      // the echo tail never re-enters this pipe.
      case MidiPipeType::MidiDelay: {
        const int delay_ms =
            clampInt(param(p, 0, 0), 0, static_cast<int>(kMaxDelayMs));
        const int repeats = clampInt(param(p, 1, 0), 0, kMaxDelayRepeats);
        for (std::size_t i = 0; i < in_count; ++i) {
          append(out, out_count, in[i]);
          if (delay_ms == 0 || repeats == 0) continue;
          const int kind = kindOf(in[i]);
          if (kind != kNoteOn && kind != kNoteOff && kind != kCc) continue;
          queueEcho(pipe_index, in[i], repeats, now_ms + static_cast<std::uint32_t>(delay_ms));
        }
        return;
      }

      // Only an id outside the wire's 1..16 range reaches here: pass it
      // unchanged rather than silently silencing the route.
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

  // -------------------------------------------------------------------------
  // Wave-2 clocked pipes. Generated events go through runGenerated(), which
  // re-enters the chain after the producing pipe. That is only correct from
  // tick()/clockTick(): inside runPipe() the enclosing pass already forwards a
  // pipe's output through the remaining stages, so the *_offs helpers below
  // append there instead of injecting.
  // -------------------------------------------------------------------------
  void runGenerated(std::size_t pipe_index, const PipeEvent& ev,
                    std::uint32_t now_ms, PipeOutput& out) {
    const PipeOutput tail = runFrom(pipe_index + 1, ev, now_ms);
    for (std::size_t i = 0; i < tail.count; ++i) out.push(tail.events[i]);
  }

  static PipeEvent noteOffEvent(const PipeNote& n) {
    PipeEvent off;
    off.size = 3;
    off.data[0] = static_cast<std::uint8_t>(0x80 | n.channel);
    off.data[1] = n.note;
    off.data[2] = 0;
    return off;
  }

  // Inside a pipe pass: the enclosing runFrom() forwards these offs.
  void appendSoundingOffs(ClockedPipeState& state,
                          std::array<PipeEvent, kMaxPipeEvents>& out,
                          std::size_t& out_count) {
    for (std::size_t i = 0; i < state.sounding.count(); ++i)
      append(out, out_count, noteOffEvent(state.sounding.at(i)));
    state.sounding.clear();
    state.gate_pending = false;
  }

  // From tick()/clockTick(): release through the pipe's successors while the
  // chain runs stages, raw when the whole route is off (no stage would run).
  void emitRelease(const PipeNote& n, std::size_t pipe_index, std::uint32_t now_ms,
                   PipeOutput& out) {
    if (on_)
      runGenerated(pipe_index, noteOffEvent(n), now_ms, out);
    else
      out.push(noteOffEvent(n));
  }

  void injectSoundingOffs(ClockedPipeState& state, std::size_t pipe_index,
                          std::uint32_t now_ms, PipeOutput& out) {
    for (std::size_t i = 0; i < state.sounding.count(); ++i)
      emitRelease(state.sounding.at(i), pipe_index, now_ms, out);
    state.sounding.clear();
    state.gate_pending = false;
  }

  // A chain edit dropped the runtime state while notes were on; release them
  // before anything else so an edit can never leave a note sounding forever.
  void releaseOrphans(std::uint32_t now_ms, PipeOutput& out) {
    for (std::size_t index = 0; index < kMaxRoutePipes; ++index) {
      ClockedPipeState& state = clocked_[index];
      if (!state.orphaned) continue;
      state.orphaned = false;
      // Sounding notes first (arp/repeater gates and delay echoes), then the
      // harmonizer's reference-counted outputs.
      injectSoundingOffs(state, index, now_ms, out);
      for (auto& entry : state.harmony) {
        if (!entry.used) continue;
        const PipeNote note{entry.channel, entry.note, 0};
        entry = ClockedPipeState::HarmonyEntry{};
        emitRelease(note, index, now_ms, out);
      }
    }
  }

  // A held note-on is consumed by the arp/repeater and restarts the pattern
  // when the held set changes. At kMaxHeldNotes the extra note-on is dropped
  // together with its note-off (documented cap), never half tracked.
  void holdNote(ClockedPipeState& state, const PipeEvent& e) {
    const PipeNote note{static_cast<std::uint8_t>(e.data[0] & 0x0F), e.data[1],
                        e.data[2]};
    const bool known = state.held.contains(note.channel, note.note);
    if (!state.held.add(note) || known) return;
    state.cursor = 0;
  }

  // The last held note is gone: stop the grid and release everything the pipe
  // turned on ("all notes off on empty hold set").
  void stopSequencer(ClockedPipeState& state,
                     std::array<PipeEvent, kMaxPipeEvents>& out,
                     std::size_t& out_count) {
    state.stepping = false;
    state.cursor = 0;
    state.burst = 0;
    appendSoundingOffs(state, out, out_count);
  }

  // One note-on for a clocked pipe: tracked in the sounding set first, so a
  // full set drops the note instead of stranding it, then re-injected.
  void emitClockedNote(const PipeNote& n, int accent, std::size_t pipe_index,
                       std::uint32_t now_ms, PipeOutput& out) {
    PipeNote note = n;
    const int boost = clampInt(accent, 0, 100);
    if (boost > 0)
      note.velocity = static_cast<std::uint8_t>(
          clampInt((static_cast<int>(n.velocity) * (100 + boost) + 50) / 100, 1, 127));
    if (!clocked_[pipe_index].sounding.add(note)) return;
    PipeEvent on;
    on.size = 3;
    on.data[0] = static_cast<std::uint8_t>(0x90 | note.channel);
    on.data[1] = note.note;
    on.data[2] = note.velocity;
    runGenerated(pipe_index, on, now_ms, out);
  }

  // Gate note-offs of one arp/repeater step, emitted when their tick arrives
  // so a step never retriggers a note that is still sounding.
  void releaseGate(std::size_t pipe_index, std::uint32_t tick_index,
                   std::uint32_t now_ms, PipeOutput& out) {
    ClockedPipeState& state = clocked_[pipe_index];
    if (!state.gate_pending) return;
    if (static_cast<std::int32_t>(tick_index - state.gate_tick) < 0) return;
    injectSoundingOffs(state, pipe_index, now_ms, out);
  }

  // Shared Arp/Repeater engine: the held set decides whether the grid runs, the
  // division and swing decide when, and the pipe type decides what a step
  // emits. state.step is the absolute grid index of the next step, so the
  // phase survives a config edit and never drifts from the tick alarm.
  void stepSequencer(const Pipe& p, std::size_t pipe_index,
                     std::uint32_t tick_index, std::uint32_t now_ms,
                     PipeOutput& out) {
    ClockedPipeState& state = clocked_[pipe_index];
    if (state.held.count() == 0) {
      state.stepping = false;
      return;
    }
    const bool repeater = p.type == MidiPipeType::Repeater;
    const std::uint32_t step_ticks = pipeDivTicks(param(p, 0, 0));
    const std::int32_t swing_ticks =
        repeater ? 0 : pipeSwingTicks(param(p, 3, 50), step_ticks);
    if (!state.stepping) {
      // The first step lands on the next division boundary after the first
      // note-on: a clocked pipe never jumps the grid.
      state.stepping = true;
      state.step = pipeStepIndex(tick_index, step_ticks) + 1;
      state.cursor = 0;
      state.burst = 0;
    }
    const std::uint32_t now_step = pipeStepIndex(tick_index, step_ticks);
    // clock_ticks restarts on transport START, so a stored step further ahead
    // than one division is stale: rejoin the current grid.
    if (state.step > now_step + 1) state.step = now_step;
    for (std::size_t guard = 0; guard < kMaxStepsPerCall; ++guard) {
      if (pipeStepDueTick(state.step, step_ticks, swing_ticks) >
          static_cast<std::int64_t>(tick_index))
        return;
      injectSoundingOffs(state, pipe_index, now_ms, out);
      if (repeater)
        emitRepeaterStep(p, state, pipe_index, tick_index, step_ticks, now_ms, out);
      else
        emitArpStep(p, state, pipe_index, state.step, step_ticks, tick_index, now_ms, out);
      ++state.step;
    }
    // Still behind after kMaxStepsPerCall: drop the backlog and rejoin the grid.
    state.step = pipeStepIndex(tick_index, step_ticks) + 1;
  }

  void emitArpStep(const Pipe& p, ClockedPipeState& state, std::size_t pipe_index,
                   std::uint32_t step_index, std::uint32_t step_ticks,
                   std::uint32_t tick_index, std::uint32_t now_ms,
                   PipeOutput& out) {
    const int type = clampInt(param(p, 1, 0), 0, 4);
    const int octaves = clampInt(param(p, 2, 1), 1, 4);
    const int accent = clampInt(param(p, 4, 0), 0, 100);
    std::array<PipeNote, kMaxArpPattern> pattern{};
    const std::size_t size = buildArpPattern(state.held.data(), state.held.count(),
                                             type, octaves, pattern);
    if (size == 0) return;
    // The accent marks the grid's beat (every fourth step), not a pattern
    // position, so arps with different pattern lengths accent together.
    const int boost = (step_index & 3u) == 0 ? accent : 0;
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
      emitClockedNote(pattern[i], boost, pipe_index, now_ms, out);
    state.gate_pending = true;
    state.gate_tick = tick_index + pipeGateTicks(param(p, 5, 50), step_ticks);
  }

  void emitRepeaterStep(const Pipe& p, ClockedPipeState& state,
                        std::size_t pipe_index, std::uint32_t tick_index,
                        std::uint32_t step_ticks, std::uint32_t now_ms,
                        PipeOutput& out) {
    const int repeats = clampInt(param(p, 1, 1), 1, kMaxRepeatCount);
    const int falloff = clampInt(param(p, 2, 0), 0, 100);
    // Strike index inside the burst: the falloff envelope restarts every
    // `repeats` steps and with every new hold (state.burst resets on start).
    const std::uint32_t strike = state.burst % static_cast<std::uint32_t>(repeats);
    for (std::size_t i = 0; i < state.held.count(); ++i) {
      PipeNote note = state.held.at(i);
      for (std::uint32_t k = 0; k < strike; ++k)
        note.velocity = attenuate(note.velocity, falloff);
      if (note.velocity == 0) note.velocity = 1;
      emitClockedNote(note, 0, pipe_index, now_ms, out);
    }
    ++state.burst;
    state.gate_pending = true;
    state.gate_tick = tick_index + (step_ticks / 2 ? step_ticks / 2 : 1u);
  }

  // CcLfo has no channel parameter in the session contract, so it emits on MIDI
  // channel 1; put a ChannelRemap after it to move the stream.
  void stepLfo(const Pipe& p, std::size_t pipe_index, std::uint32_t tick_index,
               std::uint32_t now_ms, PipeOutput& out) {
    ClockedPipeState& state = clocked_[pipe_index];
    const std::uint32_t step_ticks = pipeDivTicks(param(p, 2, 0));
    if (!state.stepping) {
      state.stepping = true;
      state.step = pipeStepIndex(tick_index, step_ticks) + 1;
    }
    const std::uint32_t now_step = pipeStepIndex(tick_index, step_ticks);
    if (state.step > now_step + 1) state.step = now_step;
    for (std::size_t guard = 0; guard < kMaxStepsPerCall; ++guard) {
      if (pipeStepDueTick(state.step, step_ticks, 0) >
          static_cast<std::int64_t>(tick_index))
        return;
      PipeEvent cc;
      cc.size = 3;
      cc.data[0] = 0xB0;
      cc.data[1] = static_cast<std::uint8_t>(clampInt(param(p, 0, 1), 0, 127));
      cc.data[2] = lfoValue(
          clampInt(param(p, 1, 0), 0, 3),
          static_cast<std::size_t>(state.step % kLfoStepsPerCycle),
          clampInt(param(p, 3, 0), 0, 127), clampInt(param(p, 4, 64), 0, 127));
      runGenerated(pipe_index, cc, now_ms, out);
      ++state.step;
    }
    state.step = now_step + 1;
  }

  // --- MidiDelay -----------------------------------------------------------
  // Queues one echo chain. A full queue drops that echo and leaves the dry
  // event untouched (documented cap: kMaxDelayEvents simultaneous delayed
  // events, each carrying its whole repeat chain).
  void queueEcho(std::size_t pipe_index, const PipeEvent& ev, int echoes,
                 std::uint32_t due_ms) {
    if (echoes <= 0) return;
    for (auto& echo : delay_[pipe_index]) {
      if (echo.used) continue;
      echo.used = true;
      echo.due_ms = due_ms;
      echo.size = ev.size;
      for (std::size_t i = 0; i < 3; ++i) echo.data[i] = ev.data[i];
      echo.echoes_left = static_cast<std::uint8_t>(echoes);
      return;
    }
  }

  // Replays the due echoes oldest first, so a note-off echo never overtakes its
  // note-on echo in a stalled pass, and re-queues the rest of each repeat chain
  // one delay later with the falloff applied.
  void serviceDelay(const Pipe& p, std::size_t pipe_index, std::uint32_t now_ms,
                    PipeOutput& out) {
    const int delay_ms = clampInt(param(p, 0, 0), 0, static_cast<int>(kMaxDelayMs));
    if (delay_ms == 0) return;
    const int falloff = clampInt(param(p, 2, 0), 0, 100);
    std::array<DelayEcho, kMaxDelayEvents>& queue = delay_[pipe_index];
    for (;;) {
      DelayEcho* next = nullptr;
      for (auto& echo : queue) {
        if (!echo.used) continue;
        if (static_cast<std::int32_t>(now_ms - echo.due_ms) < 0) continue;
        if (next != nullptr &&
            static_cast<std::int32_t>(echo.due_ms - next->due_ms) >= 0)
          continue;
        next = &echo;
      }
      if (next == nullptr) return;
      const DelayEcho echo = *next;
      next->used = false;
      PipeEvent ev;
      ev.size = echo.size;
      for (std::size_t i = 0; i < 3; ++i) ev.data[i] = echo.data[i];
      const int kind = kindOf(ev);
      const std::uint8_t channel = static_cast<std::uint8_t>(ev.data[0] & 0x0F);
      if (kind == kNoteOn) {
        // Only echo a note-on that can be tracked; an untracked echo would be
        // stranded by a chain edit (an orphaned note-off echo is harmless).
        if (!clocked_[pipe_index].sounding.add(
                PipeNote{channel, ev.data[1], ev.data[2]}))
          continue;
      } else if (kind == kNoteOff) {
        clocked_[pipe_index].sounding.remove(channel, ev.data[1]);
      }
      runGenerated(pipe_index, ev, now_ms, out);
      if (echo.echoes_left <= 1) continue;
      DelayEcho repeat = echo;
      repeat.echoes_left = static_cast<std::uint8_t>(echo.echoes_left - 1);
      repeat.due_ms = now_ms + static_cast<std::uint32_t>(delay_ms);
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
      if (entry.used && entry.channel == channel && entry.note == note) return true;
    return false;
  }

  // Adds one reference. False when the table is full: the caller then skips that
  // voice, so anything it emits stays matched on the wire.
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

  // One runtime slot per chain position for the Wave-2 pipes, plus their echo
  // queue. Bounded on purpose: kMaxHeldNotes/kMaxSoundingNotes per slot and
  // kMaxDelayEvents echoes per MidiDelay (see midi_clock_pipes.hpp).
  std::array<ClockedPipeState, kMaxRoutePipes> clocked_{};
  std::array<std::array<DelayEcho, kMaxDelayEvents>, kMaxRoutePipes> delay_{};
};

}  // namespace mb

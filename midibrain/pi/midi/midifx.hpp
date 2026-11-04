#pragma once
// Portable MIDI FX Wave-1 pipe engine for the mnkybox Pi core (JUCE-free,
// Pi-free, heap-free). One route owns one ordered chain; a chain turns one
// routed MIDI message into zero or more messages.
//
// The positional parameter order is the contract frozen in
// pi/session/session.hpp (MidiPipeType) and already implemented on the Pico in
// firmware/pico/midi_pipe.hpp. Wave 1 is the stateless set (types 1..11).
// Clocked Wave-2 types 12..16 are accepted by configure() so a session round
// trips, but process()/tick() reject the whole chain with a static reason
// until the Wave-2 clocked engine exists; a bypassed Wave-2 pipe does not
// reject. That is deliberately stricter than the firmware's current
// pass-through: on the Pi a clocked pipe must not silently emit
// un-clock-processed bytes.
//
// Ownership: this engine evaluates only routes touching UsbIn/UsbOut (see
// Router::piOwnsPipeChain). A TRS->TRS chain is configured on the Pico with
// EV_MIDI_ROUTE / EV_MIDI_ROUTE_PIPE and evaluated there for
// EV_MIDI_ROUTE_SEND, so the Pi never runs the same chain twice.
//
// Convergence follow-up: firmware/pico/midi_pipe.hpp should eventually adopt
// this file as the single implementation instead of keeping a second copy of
// the Wave-1 semantics. Do not change firmware from here.
#include <array>
#include <cstddef>
#include <cstdint>

namespace mnky::midi {

// Wire limits shared with firmware/pico/protocol.hpp: at most 8 pipes per
// route and 10 positional parameters per pipe.
inline constexpr std::size_t kMaxRoutePipes = 8;
inline constexpr std::size_t kMaxPipeParams = 10;
// One evaluation can emit many events (Sustain releases its held notes).
// 64 matches the firmware engine and keeps the output heap-free.
inline constexpr std::size_t kMaxPipeEvents = 64;
inline constexpr std::size_t kMaxPendingNoteOffs = 64;
inline constexpr std::uint8_t kFirstWave2Pipe = 12;
inline constexpr std::uint32_t kDefaultPipeSeed = 0x9e3779b9u;

// Stable ids from pi/session/session.hpp. Comments carry the positional
// parameter contract; configure() accepts any underlying value in 1..16.
enum class PipeType : std::uint8_t {
  Filter = 1,         // [noteOn,noteOff,cc,pc,press,bend,realtime,sysex] 1=pass
  NoteRange = 2,      // [lo,hi,velLo,velHi]
  CcRemap = 3,        // [inLo,inHi,outLo,outHi] linear CC-number map
  ChannelRemap = 4,   // [inCh 0=any, outCh 1..16]
  ScaleQuantize = 5,  // [scale,root,mask12]; scale 0 off, 1..5 scales, 6 user
  Chance = 6,         // [percent 0..100] drop note-ons; note-offs always pass
  VelocityRandom = 7, // [amount 0..127, minVel 0..127]
  Dispatcher = 8,     // [numCh 2..16, startCh 1..16]
  Sustain = 9,        // [latch 0/1] hold note-offs while CC64 is down
  NoteLength = 10,    // [lengthMs 0=off, else fixed note length]
  Transform = 11,     // [matchKind,matchLo,matchHi,outKind,outVal]
  Arp = 12,
  Repeater = 13,
  CcLfo = 14,
  Harmonizer = 15,
  MidiDelay = 16,
};

// One configured pipe. Missing positional parameters (paramCount too small)
// use the per-type safe defaults documented in midifx.cpp; values are clamped
// to each pipe's range before use.
struct PipeConfig {
  PipeType type = PipeType::Filter;
  bool bypass = false;
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
  // Events past kMaxPipeEvents (a Sustain release burst can exceed the cap);
  // drop-oldest is not used, the earliest events win.
  std::size_t dropped = 0;
  // A Wave-2 pipe refused the evaluation: no events, reason/rejectPipe set.
  bool rejected = false;
  std::uint8_t rejectPipe = 0;
  const char* reason = nullptr;

  void push(const PipeEvent& event) {
    if (count < events.size()) {
      events[count++] = event;
    } else {
      ++dropped;
    }
  }
};

// Static rejection text for a Wave-2 id, or nullptr for a Wave-1 id. The
// pointer is valid for the process lifetime, so it is safe to log/store.
const char* wave2Reason(PipeType type) noexcept;

// One route's ordered chain plus its runtime state (Dispatcher note->channel
// assignments, Sustain's held-note latch, NoteLength's pending note-offs and
// the Chance/VelocityRandom RNG). Reconfiguring resets the runtime state, so a
// stale pairing decision can never leak into a new chain.
class PipeChain {
 public:
  PipeChain() { resetState(); }

  // Replaces the chain. Accepts type ids 1..16, at most kMaxRoutePipes pipes
  // and kMaxPipeParams parameters each. Invalid input leaves the current
  // chain untouched and returns false.
  bool configure(const PipeConfig* pipes, std::size_t count);

  std::size_t pipeCount() const { return pipe_count_; }
  bool empty() const { return pipe_count_ == 0; }
  // First enabled (not bypassed) Wave-2 pipe index, or -1. A rejected chain
  // exposes the offending pipe for the caller's log.
  int wave2Pipe() const { return wave2_pipe_; }

  // Chance/VelocityRandom stream. 0 maps to 1 (xorshift32 needs a nonzero
  // state); every config change keeps the seed but rewinds the stream.
  void setSeed(std::uint32_t seed);
  std::uint32_t seed() const { return seed_; }

  // Applies the chain to one routed message. `nowMs` is the NoteLength clock,
  // in the caller's monotonic millisecond timeline (wrap-safe comparisons).
  // Empty/invalid input yields an empty result, matching the firmware.
  PipeOutput process(const PipeEvent& input, std::uint32_t nowMs);

  // Emits fixed-length note-offs whose NoteLength deadline has elapsed. Each
  // off re-enters the chain at the NoteLength pipe's successor, so later pipes
  // (ChannelRemap, Sustain, ...) still see it.
  PipeOutput tick(std::uint32_t nowMs);

  // Clears pairing/sustain/pending state and rewinds the RNG to the seed.
  void resetState();

 private:
  struct PendingOff {
    bool used = false;
    std::uint32_t dueMs = 0;
    std::uint8_t stage = 0;
    std::uint8_t channel = 0;
    std::uint8_t note = 0;
  };

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

  static int kindOf(const PipeEvent& event);
  static int clampInt(int value, int lo, int hi);
  static int param(const PipeConfig& pipe, std::size_t index, int fallback);
  static void append(std::array<PipeEvent, kMaxPipeEvents>& out, std::size_t& count,
                     std::size_t& dropped, const PipeEvent& event);
  static bool scaleAllowed(int scale, int root, std::uint16_t mask12,
                           std::array<bool, 12>& allowed);
  static bool quantizeNote(int note, int scale, int root, std::uint16_t mask12,
                           int& result);
  static bool transformMatches(const PipeEvent& event, int matchKind, int lo, int hi);
  static bool makeTransform(const PipeEvent& source, int outKind, int outValue,
                            PipeEvent& out);

  std::uint32_t nextRandom();
  bool hasPending(std::uint8_t channel, std::uint8_t note) const;
  void scheduleNoteOff(const PipeEvent& noteOn, std::size_t pipeIndex,
                       int lengthMs, std::uint32_t nowMs);
  PipeOutput runFrom(std::size_t startPipe, const PipeEvent& start,
                     std::uint32_t nowMs);
  void runPipe(const PipeConfig& pipe, std::size_t pipeIndex,
               const std::array<PipeEvent, kMaxPipeEvents>& in, std::size_t inCount,
               std::array<PipeEvent, kMaxPipeEvents>& out, std::size_t& outCount,
               std::size_t& dropped, std::uint32_t nowMs);

  std::array<PipeConfig, kMaxRoutePipes> pipes_{};
  std::size_t pipe_count_ = 0;
  int wave2_pipe_ = -1;

  std::uint32_t seed_ = kDefaultPipeSeed;
  std::uint32_t rng_state_ = kDefaultPipeSeed;

  std::size_t dispatch_index_ = 0;
  std::array<std::array<std::uint8_t, 128>, 16> note_channel_{};

  bool sustain_latched_ = false;
  // Two 64-bit words cover notes 0..127 per channel; bit n in word (n >> 6)
  // marks a note-off latched by the Sustain pipe.
  std::array<std::array<std::uint64_t, 2>, 16> sustain_held_{};

  std::array<PendingOff, kMaxPendingNoteOffs> pending_{};
};

}  // namespace mnky::midi

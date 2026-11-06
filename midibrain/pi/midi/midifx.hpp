#pragma once
// Pi adapter over the shared MIDI FX core (midibrain/midifx/core).
//
// The core is the single behavior implementation for all 16 pipe types: the
// Pico firmware links it directly and the Pi router reaches it through this
// thin header. The adapter keeps the `mnky::midi` names the router and the Pi
// test suite already use; it owns no behavior. A fresh chain is enabled (the
// old Pi engine's contract: Router decides whether a route runs), and the
// pipe role table comes from the same generated schema both builds compile.
//
// Ownership: the router evaluates only chains it owns (piOwnsPipeChain for
// src->dst routes and the generator/output-insert pseudo-sources). A TRS->TRS
// chain is configured on the Pico over the link and evaluated there, so each
// chain runs on exactly one side.
#include <array>
#include <cstddef>
#include <cstdint>

#include "../../midifx/core/midifx.hpp"

namespace mnky::midi {

inline constexpr std::size_t kMaxRoutePipes = mnky::midifx::kMaxRoutePipes;
inline constexpr std::size_t kMaxPipeParams = mnky::midifx::kMaxPipeParams;
inline constexpr std::size_t kMaxPipeEvents = mnky::midifx::kMaxPipeEvents;
inline constexpr std::size_t kMaxPendingNoteOffs = mnky::midifx::kMaxPendingNoteOffs;

using PipeType = mnky::midifx::PipeType;
using PipeConfig = mnky::midifx::PipeConfig;
using PipeEvent = mnky::midifx::PipeEvent;

// Router-facing output. `rejected`/`rejectPipe`/`reason` are the retired Wave-2
// rejection seam: the shared core evaluates every type, so they stay false /
// nullptr and exist only while callers (and the diagnostics struct) are
// migrated off the temporary guard.
struct PipeOutput {
  std::array<PipeEvent, kMaxPipeEvents> events{};
  std::size_t count = 0;
  std::size_t dropped = 0;
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

class PipeChain {
 public:
  PipeChain() { core_.begin(true); }

  void begin(bool on) { core_.begin(on); }
  bool enabled() const { return core_.enabled(); }
  bool empty() const { return core_.empty(); }
  std::size_t pipeCount() const { return core_.pipeCount(); }
  bool pipeAt(std::size_t index, PipeConfig& out) const {
    return core_.pipeAt(index, out);
  }
  bool headIsGenerator() const { return core_.headIsGenerator(); }

  bool append(const PipeConfig& pipe) { return core_.append(pipe); }

  bool setPipeChunk(std::uint8_t index, std::uint8_t type, bool bypass,
                    std::uint8_t offset, const std::int32_t* params,
                    std::size_t count,
                    std::uint16_t chMask =
                        mnky::midifx::generated::kChannelScope.defaultValue) {
    return core_.setPipeChunk(index, type, bypass, offset, params, count, chMask);
  }

  bool configure(const PipeConfig* pipes, std::size_t count) {
    return core_.configure(pipes, count);
  }

  void setSeed(std::uint32_t seed) { core_.setSeed(seed); }
  std::uint32_t seed() const { return core_.seed(); }
  void resetState() { core_.resetState(); }

  PipeOutput process(const PipeEvent& input, std::uint32_t nowMs) {
    return convert(core_.process(input, nowMs));
  }
  PipeOutput tick(std::uint32_t nowMs) { return convert(core_.tick(nowMs)); }
  PipeOutput clockTick(std::uint32_t tickIndex, std::uint32_t nowMs) {
    return convert(core_.clockTick(tickIndex, nowMs));
  }

 private:
  static PipeOutput convert(const mnky::midifx::PipeOutput& source) {
    PipeOutput out;
    out.count = source.count;
    out.dropped = source.dropped;
    for (std::size_t i = 0; i < source.count; ++i) out.events[i] = source.events[i];
    return out;
  }

  mnky::midifx::PipeChain core_;
};

}  // namespace mnky::midi

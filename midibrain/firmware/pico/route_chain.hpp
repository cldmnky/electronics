#pragma once
// The firmware's route graph: four physical TRS src->dst routes plus the two
// pseudo-source route kinds from docs/protocol.md.
//
//   src 0/1            physical TRS IN1/IN2 -> a TRS output (0/1)
//   route_src_generator  a chain whose HEAD must be a generator pipe
//                        (pipes.json role); it emits on the tick grid without
//                        consuming an input
//   route_src_insert(s)  output insert slot s; every message routed to that
//                        output passes insert 0, then 1, ... in route order
//                        before it reaches the UART
//
// One RouteChains instance owns every chain and its runtime state. main.cpp
// uses the Pico tick alarm as the clock; the host protocol test drives the same
// class, so the wire decode, generator legality and insert ordering are tested
// without the Pico. The shared core (midibrain/midifx/core) is the only pipe
// behavior; this header only routes events between chains.
#include <cstddef>
#include <cstdint>

#include "../../midifx/core/midifx.hpp"
#include "pipe_event.hpp"
#include "protocol.hpp"

namespace mb {

class RouteChains {
 public:
  static constexpr std::size_t kOutputs = 2;
  static constexpr std::size_t kInsertSlots = route_insert_slot_max;

  // Configures one chain. The 0x18 begin clears it; 0x19 chunks append or
  // extend a pipe. A generator chain's head must have the generator role, so a
  // modifier can never be configured as a source (the frame is a bad frame,
  // not a silent no-op).
  bool begin(std::uint8_t src, std::uint8_t dst, bool on) {
    PipeChain* target = chain(src, dst);
    if (target == nullptr) return false;
    target->begin(on);
    return true;
  }

  bool setPipeChunk(std::uint8_t src, std::uint8_t dst, std::uint8_t index,
                    std::uint8_t type, bool bypass, std::uint8_t offset,
                    const std::int32_t* params, std::size_t count,
                    std::uint16_t chMask = route_pipe_channel_mask_default) {
    PipeChain* target = chain(src, dst);
    if (target == nullptr) return false;
    if (route_src_is_generator(src) && index == 0 && offset == 0 &&
        !mnky::midifx::generated::pipeIsGenerator(type))
      return false;
    return target->setPipeChunk(index, type, bypass, offset, params, count, chMask);
  }

  // The routed-byte path for a real TRS source (EV_MIDI_ROUTE_SEND): run the
  // source route chain, then every output insert in order.
  mnky::midifx::PipeOutput processRoute(std::uint8_t src, std::uint8_t dst,
                                        const mnky::midifx::PipeEvent& input,
                                        std::uint32_t nowMs) {
    if (!route_src_is_trs(src) || dst >= kOutputs) return {};
    return throughInserts(dst, 0, pipes_[src][dst].process(input, nowMs), nowMs);
  }

  // Main-loop service: millisecond deadlines (NoteLength/MidiDelay), the
  // clocked pipes on the caller's 24-PPQN tick grid, and the generator chains.
  // Every chain's output flows through the remaining output inserts before it
  // is emitted; an insert's own time-generated events flow through the later
  // inserts, so route order is preserved on every path.
  template <typename Emit>
  void service(std::uint32_t tickIndex, std::uint32_t nowMs, Emit emit) {
    for (std::uint8_t dst = 0; dst < kOutputs; ++dst) {
      for (std::uint8_t src = 0; src < 2; ++src) {
        serviceChain(pipes_[src][dst], dst, 0, tickIndex, nowMs, emit);
      }
      for (std::size_t slot = 0; slot < kInsertSlots; ++slot) {
        // An insert's generated output is already past `slot`; later inserts
        // still apply.
        serviceChain(inserts_[slot][dst], dst,
                     static_cast<std::uint8_t>(slot + 1), tickIndex, nowMs, emit);
      }
      serviceChain(generators_[dst], dst, 0, tickIndex, nowMs, emit);
    }
  }

  // Per-chain RNG seeds are a boot-session decision: identical session + time
  // gives identical Chance/VelocityRandom decisions, on both route kinds.
  void setSeeds(std::uint32_t session, std::uint32_t entropy) {
    std::uint32_t n = 0;
    for (auto& row : pipes_)
      for (auto& chain : row) chain.setSeed(session ^ ((n++ + 1) * 0x9e3779b9u) ^ entropy);
    for (auto& chain : generators_)
      chain.setSeed(session ^ ((n++ + 1) * 0x9e3779b9u) ^ entropy);
    for (auto& row : inserts_)
      for (auto& chain : row) chain.setSeed(session ^ ((n++ + 1) * 0x9e3779b9u) ^ entropy);
  }

  const mnky::midifx::PipeChain* chain(std::uint8_t src, std::uint8_t dst) const {
    if (dst >= kOutputs) return nullptr;
    if (route_src_is_trs(src)) return &pipes_[src][dst];
    if (route_src_is_generator(src)) return &generators_[dst];
    if (route_src_is_insert(src))
      return &inserts_[route_src_insert_slot(src)][dst];
    return nullptr;
  }

 private:
  using PipeChain = mnky::midifx::PipeChain;
  using PipeOutput = mnky::midifx::PipeOutput;

  PipeChain* chain(std::uint8_t src, std::uint8_t dst) {
    return const_cast<PipeChain*>(
        static_cast<const RouteChains*>(this)->chain(src, dst));
  }

  // Feeds `in` through output inserts [first, kInsertSlots) in route order.
  PipeOutput throughInserts(std::uint8_t dst, std::size_t first,
                            const PipeOutput& in, std::uint32_t nowMs) {
    PipeOutput current = in;
    for (std::size_t slot = first; slot < kInsertSlots; ++slot) {
      PipeChain& insert = inserts_[slot][dst];
      if (!insert.enabled() || insert.empty()) continue;
      PipeOutput next;
      for (std::size_t i = 0; i < current.count; ++i)
        merge(next, insert.process(current.events[i], nowMs));
      next.dropped += current.dropped;
      current = next;
    }
    return current;
  }

  template <typename Emit>
  void serviceChain(PipeChain& chain, std::uint8_t dst, std::uint8_t firstInsert,
                    std::uint32_t tickIndex, std::uint32_t nowMs, Emit& emit) {
    if (!chain.enabled()) return;
    const PipeOutput timed = throughInserts(dst, firstInsert, chain.tick(nowMs), nowMs);
    if (timed.count != 0 || timed.dropped != 0) emit(dst, timed);
    const PipeOutput clocked =
        throughInserts(dst, firstInsert, chain.clockTick(tickIndex, nowMs), nowMs);
    if (clocked.count != 0 || clocked.dropped != 0) emit(dst, clocked);
  }

  static void merge(PipeOutput& out, const PipeOutput& source) {
    for (std::size_t i = 0; i < source.count; ++i) out.push(source.events[i]);
    out.dropped += source.dropped;
  }

  PipeChain pipes_[2][kOutputs];
  PipeChain generators_[kOutputs];
  PipeChain inserts_[kInsertSlots][kOutputs];
};

}  // namespace mb

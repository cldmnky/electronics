#include "router.hpp"

#include <utility>

namespace mnky::midi {
namespace {

// Applies the source transpose to one delivery candidate. Only note on/off
// (0x80/0x90) shift; a pitch outside 0..127 drops that event instead of
// clamping it onto the wrong key.
bool transposeForOutput(const MidiEvent& in, int shift, MidiEvent& out) {
  out = in;
  if (shift == 0) return true;
  const std::uint8_t status = in.data[0];
  if (status >= 0xF0) return true;
  const std::uint8_t kind = status & 0xF0;
  if (kind != 0x80 && kind != 0x90) return true;
  const int pitch = static_cast<int>(in.data[1]) + shift;
  if (pitch < 0 || pitch > 127) return false;
  out.data[1] = static_cast<std::uint8_t>(pitch);
  return true;
}

PipeEvent toPipeEvent(const MidiEvent& in) {
  PipeEvent event;
  event.size = in.size;
  for (std::uint8_t i = 0; i < in.size && i < 3; ++i) event.data[i] = in.data[i];
  return event;
}

MidiEvent fromPipeEvent(const MidiEvent& origin, const PipeEvent& event) {
  MidiEvent out = origin;
  out.size = event.size;
  for (std::size_t i = 0; i < 3; ++i)
    out.data[i] = i < event.size ? event.data[i] : 0;
  return out;
}

// A time-generated event has no source route identity; only the destination is
// meaningful to the sink, so the stamp keeps dst and the caller's clock.
MidiEvent generatedEvent(Port dst, std::uint32_t nowMs) {
  MidiEvent ev;
  ev.source = dst;
  ev.stampUs = static_cast<std::uint64_t>(nowMs) * 1000u;
  return ev;
}

}  // namespace

bool Router::setRoute(Port src, Port dst, bool enabled) {
  if (!isInput(src) || !isOutput(dst)) return false;
  route_[idx(src)][idx(dst)] = enabled;
  return true;
}

bool Router::routeEnabled(Port src, Port dst) const {
  if (!isInput(src) || !isOutput(dst)) return false;
  return route_[idx(src)][idx(dst)];
}

void Router::setChannelMask(Port src, std::uint16_t mask) {
  if (!isValidPort(src)) return;
  channelMask_[idx(src)] = mask;
}

std::uint16_t Router::channelMask(Port src) const {
  if (!isValidPort(src)) return kAllChannels;
  return channelMask_[idx(src)];
}

void Router::setTranspose(Port src, int semitones) {
  if (!isValidPort(src)) return;
  if (semitones < -127) semitones = -127;
  if (semitones > 127) semitones = 127;
  transpose_[idx(src)] = semitones;
}

int Router::transpose(Port src) const {
  if (!isValidPort(src)) return 0;
  return transpose_[idx(src)];
}

PipeChain* Router::mutableChain(Port src, Port dst) const {
  const int slot = pipeIndex(src, dst);
  if (slot >= 0) return &pipes_[static_cast<std::size_t>(slot)];
  if (!isInput(src) || !isOutput(dst)) return nullptr;
  const auto found = dynamicPipes_.find(dynamicPipeKey(src, dst));
  return found == dynamicPipes_.end() ? nullptr : &found->second;
}

bool Router::setPipes(Port src, Port dst, const PipeConfig* pipes, std::size_t count) {
  const int slot = pipeIndex(src, dst);
  if (slot >= 0) return pipes_[static_cast<std::size_t>(slot)].configure(pipes, count);
  if (!isInput(src) || !isOutput(dst)) return false;
  const int key = dynamicPipeKey(src, dst);
  const auto found = dynamicPipes_.find(key);
  if (found != dynamicPipes_.end()) return found->second.configure(pipes, count);
  PipeChain chain;
  if (!chain.configure(pipes, count)) return false;
  dynamicPipes_.emplace(key, std::move(chain));
  return true;
}

std::size_t Router::pipeCount(Port src, Port dst) const {
  const int slot = pipeIndex(src, dst);
  if (slot >= 0) return pipes_[static_cast<std::size_t>(slot)].pipeCount();
  const PipeChain* chain = mutableChain(src, dst);
  return chain == nullptr ? 0 : chain->pipeCount();
}

Router::PseudoChain* Router::pseudoChain(int key) const {
  const auto found = pseudoPipes_.find(key);
  return found == pseudoPipes_.end() ? nullptr : &found->second;
}

bool Router::setGenerator(Port dst, bool enabled) {
  if (!isOutput(dst) || !piOwnsPipeChain(kSrcGenerator, dst)) return false;
  pseudoPipes_[generatorKey(dst)].enabled = enabled;
  return true;
}

bool Router::generatorEnabled(Port dst) const {
  const PseudoChain* entry = pseudoChain(generatorKey(dst));
  return entry != nullptr && entry->enabled;
}

bool Router::setGeneratorPipes(Port dst, const PipeConfig* pipes, std::size_t count) {
  if (!isOutput(dst) || !piOwnsPipeChain(kSrcGenerator, dst)) return false;
  if (count != 0 && pipes == nullptr) return false;
  if (count != 0 &&
      !mnky::midifx::generated::pipeIsGenerator(static_cast<std::uint8_t>(pipes[0].type)))
    return false;
  PipeChain chain;
  if (!chain.configure(pipes, count)) return false;
  pseudoPipes_[generatorKey(dst)].chain = std::move(chain);
  return true;
}

std::size_t Router::generatorPipeCount(Port dst) const {
  const PseudoChain* entry = pseudoChain(generatorKey(dst));
  return entry == nullptr ? 0 : entry->chain.pipeCount();
}

bool Router::setOutputInsert(Port dst, std::size_t slot, bool enabled) {
  if (!isOutput(dst) || slot >= kMaxOutputInserts ||
      !piOwnsPipeChain(kSrcOutputInsert, dst))
    return false;
  pseudoPipes_[insertKey(dst, slot)].enabled = enabled;
  return true;
}

bool Router::outputInsertEnabled(Port dst, std::size_t slot) const {
  if (slot >= kMaxOutputInserts) return false;
  const PseudoChain* entry = pseudoChain(insertKey(dst, slot));
  return entry != nullptr && entry->enabled;
}

bool Router::setOutputInsertPipes(Port dst, std::size_t slot,
                                  const PipeConfig* pipes, std::size_t count) {
  if (!isOutput(dst) || slot >= kMaxOutputInserts ||
      !piOwnsPipeChain(kSrcOutputInsert, dst))
    return false;
  PipeChain chain;
  if (!chain.configure(pipes, count)) return false;
  pseudoPipes_[insertKey(dst, slot)].chain = std::move(chain);
  return true;
}

std::size_t Router::outputInsertPipeCount(Port dst, std::size_t slot) const {
  if (slot >= kMaxOutputInserts) return 0;
  const PseudoChain* entry = pseudoChain(insertKey(dst, slot));
  return entry == nullptr ? 0 : entry->chain.pipeCount();
}

bool Router::generatorPipes(Port dst, PipeConfig* out, std::size_t capacity,
                            std::size_t& count) const {
  const PseudoChain* entry = pseudoChain(generatorKey(dst));
  if (entry == nullptr || entry->chain.pipeCount() > capacity) return false;
  count = entry->chain.pipeCount();
  for (std::size_t i = 0; i < count; ++i)
    if (!entry->chain.pipeAt(i, out[i])) return false;
  return true;
}

bool Router::outputInsertPipes(Port dst, std::size_t slot, PipeConfig* out,
                               std::size_t capacity, std::size_t& count) const {
  if (slot >= kMaxOutputInserts) return false;
  const PseudoChain* entry = pseudoChain(insertKey(dst, slot));
  if (entry == nullptr || entry->chain.pipeCount() > capacity) return false;
  count = entry->chain.pipeCount();
  for (std::size_t i = 0; i < count; ++i)
    if (!entry->chain.pipeAt(i, out[i])) return false;
  return true;
}

void Router::setPipeSeed(Port src, Port dst, std::uint32_t seed) {
  const int slot = pipeIndex(src, dst);
  if (slot >= 0) {
    pipes_[static_cast<std::size_t>(slot)].setSeed(seed);
    return;
  }
  if (!isInput(src) || !isOutput(dst)) return;
  dynamicPipes_[dynamicPipeKey(src, dst)].setSeed(seed);
}

std::uint32_t Router::pipeSeed(Port src, Port dst) const {
  const int slot = pipeIndex(src, dst);
  if (slot >= 0) return pipes_[static_cast<std::size_t>(slot)].seed();
  const PipeChain* chain = mutableChain(src, dst);
  return chain == nullptr ? 0 : chain->seed();
}

void Router::clearPipes() {
  for (auto& chain : pipes_) chain = PipeChain{};
  dynamicPipes_.clear();
  pseudoPipes_.clear();
}

void Router::clearPseudoPipes() { pseudoPipes_.clear(); }

Router::PipeDiagnostics Router::pipeDiagnostics() const { return pipeStats_; }

void Router::deliverThroughInsert(Port dst, std::size_t slot, const MidiEvent& ev,
                                  std::uint32_t nowMs, const DeliverFn& deliver) const {
  if (slot >= kMaxOutputInserts) {
    ++pipeStats_.emitted;
    deliver(dst, ev);
    return;
  }
  PseudoChain* insert = pseudoChain(insertKey(dst, slot));
  if (insert == nullptr || !insert->enabled || insert->chain.empty()) {
    deliverThroughInsert(dst, slot + 1, ev, nowMs, deliver);
    return;
  }
  const PipeOutput out = insert->chain.process(toPipeEvent(ev), nowMs);
  pipeStats_.dropped += out.dropped;
  for (std::size_t i = 0; i < out.count; ++i)
    deliverThroughInsert(dst, slot + 1, fromPipeEvent(ev, out.events[i]), nowMs,
                         deliver);
}

void Router::deliverToDestination(Port dst, const MidiEvent& ev, int shift,
                                  std::uint32_t nowMs,
                                  const DeliverFn& deliver) const {
  MidiEvent shifted;
  if (!transposeForOutput(ev, shift, shifted)) return;
  deliverThroughInsert(dst, 0, shifted, nowMs, deliver);
}

void Router::route(const MidiEvent& ev, const DeliverFn& deliver) const {
  if (!isInput(ev.source) || ev.size < 1 || ev.size > 3) return;
  const std::uint8_t status = ev.data[0];
  if (status < 0x80) return;  // not a message start; the parser rejects these
  const bool channelVoice = status < 0xF0;
  if (channelVoice && (channelMask_[idx(ev.source)] & (1u << (status & 0x0F))) == 0) return;
  const int shift = transpose_[idx(ev.source)];
  // One millisecond timeline for NoteLength deadlines, matching the firmware's
  // 32-bit ms clock; the comparison is wrap-safe.
  const std::uint32_t nowMs = static_cast<std::uint32_t>(ev.stampUs / 1000u);

  for (int d = 0; d < kMaxPorts; ++d) {
    const Port dst = static_cast<Port>(d);
    if (!isOutput(dst) || !route_[idx(ev.source)][d]) continue;
    PipeChain* chainPtr = mutableChain(ev.source, dst);
    if (chainPtr == nullptr || !piOwnsPipeChain(ev.source, dst) ||
        chainPtr->empty()) {
      // No stored chain (or a firmware-owned TRS->TRS route): forward the
      // bytes untouched; the destination's output inserts still apply.
      deliverToDestination(dst, ev, shift, nowMs, deliver);
      continue;
    }
    ++pipeStats_.evaluations;
    PipeChain& chain = *chainPtr;
    // Fixed-length offs due at this message's time are emitted before the new
    // message, so a note-off can never arrive after a later note-on.
    const PipeOutput timed = chain.tick(nowMs);
    const PipeOutput processed = chain.process(toPipeEvent(ev), nowMs);
    pipeStats_.dropped += timed.dropped + processed.dropped;
    for (std::size_t i = 0; i < timed.count; ++i)
      deliverToDestination(dst, fromPipeEvent(ev, timed.events[i]), shift, nowMs,
                           deliver);
    for (std::size_t i = 0; i < processed.count; ++i)
      deliverToDestination(dst, fromPipeEvent(ev, processed.events[i]), shift,
                           nowMs, deliver);
  }
}

void Router::serviceChain(PipeChain& chain, Port dst, std::size_t firstSlot,
                          int shift, std::uint32_t nowMs, std::uint32_t tickIndex,
                          bool clocked, const DeliverFn& deliver) const {
  if (chain.empty()) return;
  const PipeOutput out = clocked ? chain.clockTick(tickIndex, nowMs)
                                 : chain.tick(nowMs);
  pipeStats_.dropped += out.dropped;
  for (std::size_t i = 0; i < out.count; ++i) {
    const MidiEvent ev = fromPipeEvent(generatedEvent(dst, nowMs), out.events[i]);
    MidiEvent shifted;
    if (!transposeForOutput(ev, shift, shifted)) continue;
    deliverThroughInsert(dst, firstSlot, shifted, nowMs, deliver);
  }
}

void Router::tick(std::uint32_t nowMs, const DeliverFn& deliver) const {
  for (int s = 0; s < kMaxPorts; ++s) {
    const Port src = static_cast<Port>(s);
    if (!isInput(src)) continue;
    for (int d = 0; d < kMaxPorts; ++d) {
      const Port dst = static_cast<Port>(d);
      if (!isOutput(dst) || !route_[s][d] || !piOwnsPipeChain(src, dst)) continue;
      PipeChain* chain = mutableChain(src, dst);
      if (chain == nullptr) continue;
      serviceChain(*chain, dst, 0, transpose_[s], nowMs, 0, false, deliver);
    }
  }
  for (std::size_t slot = 0; slot < kMaxOutputInserts; ++slot) {
    for (int d = 0; d < kMaxPorts; ++d) {
      const Port dst = static_cast<Port>(d);
      PseudoChain* insert = pseudoChain(insertKey(dst, slot));
      if (insert == nullptr || !insert->enabled) continue;
      serviceChain(insert->chain, dst, slot + 1, 0, nowMs, 0, false, deliver);
    }
  }
  for (int d = 0; d < kMaxPorts; ++d) {
    const Port dst = static_cast<Port>(d);
    PseudoChain* generator = pseudoChain(generatorKey(dst));
    if (generator == nullptr || !generator->enabled) continue;
    serviceChain(generator->chain, dst, 0, 0, nowMs, 0, false, deliver);
  }
}

void Router::clockTick(std::uint32_t tickIndex, std::uint32_t nowMs,
                       const DeliverFn& deliver) const {
  for (int s = 0; s < kMaxPorts; ++s) {
    const Port src = static_cast<Port>(s);
    if (!isInput(src)) continue;
    for (int d = 0; d < kMaxPorts; ++d) {
      const Port dst = static_cast<Port>(d);
      if (!isOutput(dst) || !route_[s][d] || !piOwnsPipeChain(src, dst)) continue;
      PipeChain* chain = mutableChain(src, dst);
      if (chain == nullptr) continue;
      serviceChain(*chain, dst, 0, transpose_[s], nowMs, tickIndex, true, deliver);
    }
  }
  for (std::size_t slot = 0; slot < kMaxOutputInserts; ++slot) {
    for (int d = 0; d < kMaxPorts; ++d) {
      const Port dst = static_cast<Port>(d);
      PseudoChain* insert = pseudoChain(insertKey(dst, slot));
      if (insert == nullptr || !insert->enabled) continue;
      serviceChain(insert->chain, dst, slot + 1, 0, nowMs, tickIndex, true, deliver);
    }
  }
  for (int d = 0; d < kMaxPorts; ++d) {
    const Port dst = static_cast<Port>(d);
    PseudoChain* generator = pseudoChain(generatorKey(dst));
    if (generator == nullptr || !generator->enabled) continue;
    serviceChain(generator->chain, dst, 0, 0, nowMs, tickIndex, true, deliver);
  }
}

}  // namespace mnky::midi

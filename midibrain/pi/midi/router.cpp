#include "router.hpp"

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
}

Router::PipeDiagnostics Router::pipeDiagnostics() const { return pipeStats_; }

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
    if (chainPtr == nullptr || !piOwnsPipeChain(ev.source, dst)) {
      // No stored chain (or a firmware-owned TRS->TRS route): forward the
      // bytes untouched (transpose is the Pi's source-wide transform, applied
      // on both sides of the split).
      MidiEvent out;
      if (!transposeForOutput(ev, shift, out)) continue;
      deliver(dst, out);
      continue;
    }
    PipeChain& chain = *chainPtr;
    if (chain.empty()) {
      MidiEvent out;
      if (!transposeForOutput(ev, shift, out)) continue;
      deliver(dst, out);
      continue;
    }
    PipeEvent input;
    input.size = ev.size;
    for (std::uint8_t i = 0; i < ev.size; ++i) input.data[i] = ev.data[i];
    ++pipeStats_.evaluations;
    if (chain.wave2Pipe() >= 0) {
      recordRejection(chain.process(input, nowMs));
      continue;
    }
    // Fixed-length offs due at this message's time are emitted before the new
    // message, so a note-off can never arrive after a later note-on.
    deliverPipeOutput(chain.tick(nowMs), dst, ev, shift, deliver);
    deliverPipeOutput(chain.process(input, nowMs), dst, ev, shift, deliver);
  }
}

void Router::tick(std::uint32_t nowMs, const DeliverFn& deliver) const {
  for (int s = 0; s < kMaxPorts; ++s) {
    const Port src = static_cast<Port>(s);
    if (!isInput(src)) continue;
    for (int d = 0; d < kMaxPorts; ++d) {
      const Port dst = static_cast<Port>(d);
      if (!isOutput(dst) || !route_[s][d]) continue;
      PipeChain* chainPtr = mutableChain(src, dst);
      if (chainPtr == nullptr || !piOwnsPipeChain(src, dst)) continue;
      PipeChain& chain = *chainPtr;
      if (chain.empty() || chain.wave2Pipe() >= 0) continue;
      MidiEvent stamp;
      stamp.source = src;
      stamp.stampUs = static_cast<std::uint64_t>(nowMs) * 1000u;
      const int shift = transpose_[s];
      deliverPipeOutput(chain.tick(nowMs), dst, stamp, shift, deliver);
    }
  }
}

void Router::recordRejection(const PipeOutput& output) const {
  ++pipeStats_.rejected;
  pipeStats_.lastRejectReason = output.reason;
  pipeStats_.lastRejectPipe = static_cast<int>(output.rejectPipe);
}

void Router::deliverPipeOutput(const PipeOutput& output, Port dst,
                               const MidiEvent& ev, int shift,
                               const DeliverFn& deliver) const {
  if (output.rejected) {
    recordRejection(output);
    return;
  }
  pipeStats_.dropped += output.dropped;
  for (std::size_t i = 0; i < output.count; ++i) {
    MidiEvent out = ev;
    out.size = output.events[i].size;
    for (std::size_t b = 0; b < 3; ++b)
      out.data[b] = b < out.size ? output.events[i].data[b] : 0;
    MidiEvent shifted;
    if (!transposeForOutput(out, shift, shifted)) continue;
    ++pipeStats_.emitted;
    deliver(dst, shifted);
  }
}

}  // namespace mnky::midi

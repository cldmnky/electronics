#include "router.hpp"

namespace mnky::midi {

bool Router::setRoute(Port src, Port dst, bool enabled) {
  if (!isInput(src) || !isOutput(dst)) return false;
  route_[idx(src)][idx(dst)] = enabled;
  return true;
}

bool Router::routeEnabled(Port src, Port dst) const {
  if (!isInput(src) || !isOutput(dst)) return false;
  return route_[idx(src)][idx(dst)];
}

void Router::setChannelMask(Port src, std::uint16_t mask) { channelMask_[idx(src)] = mask; }

std::uint16_t Router::channelMask(Port src) const { return channelMask_[idx(src)]; }

void Router::setTranspose(Port src, int semitones) {
  if (semitones < -127) semitones = -127;
  if (semitones > 127) semitones = 127;
  transpose_[idx(src)] = semitones;
}

int Router::transpose(Port src) const { return transpose_[idx(src)]; }

void Router::route(const MidiEvent& ev, const DeliverFn& deliver) const {
  if (!isInput(ev.source) || ev.size < 1 || ev.size > 3) return;
  const std::uint8_t status = ev.data[0];
  if (status < 0x80) return;  // not a message start; the parser rejects these
  const bool channelVoice = status < 0xF0;
  if (channelVoice && (channelMask_[idx(ev.source)] & (1u << (status & 0x0F))) == 0) return;
  const std::uint8_t kind = status & 0xF0;
  const bool note = channelVoice && (kind == 0x80 || kind == 0x90);
  const int shift = note ? transpose_[idx(ev.source)] : 0;

  MidiEvent out = ev;
  if (shift != 0) {
    const int pitch = static_cast<int>(ev.data[1]) + shift;
    if (pitch < 0 || pitch > 127) return;
    out.data[1] = static_cast<std::uint8_t>(pitch);
  }
  for (int d = 0; d < kPortCount; ++d) {
    const Port dst = static_cast<Port>(d);
    if (!isOutput(dst) || !route_[idx(ev.source)][d]) continue;
    deliver(dst, out);
  }
}

}  // namespace mnky::midi

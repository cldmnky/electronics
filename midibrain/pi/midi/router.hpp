#pragma once
// MIDI routing matrix for the mnkybox Pi core. JUCE-free, host-testable.
//
// Port taxonomy: ports 0..5 are the firmware/wire identities from
// pi/ARCHITECTURE.md -- the two TRS inputs, the legacy/virtual UsbIn merge, the
// two TRS outputs and the UsbOut fan-out. They keep their meanings forever.
// Runtime-discovered USB MIDI devices are additional routable endpoints at ids
// 6..31 (see port_registry.hpp); a route refers to a dynamic id exactly like a
// fixed one, and the glue owns which of the two directions a device uses.
//
// Pipe ownership: the Pico has no USB view, so a Wave-1 pipe chain on a route
// touching UsbIn/UsbOut (fixed or a dynamic USB device) is evaluated here (see
// piOwnsPipeChain). A TRS->TRS chain stays on the Pico and is only configured
// over the link (pi/link EV_MIDI_ROUTE*), never evaluated here, so each route's
// chain runs exactly once on exactly one side. Route objects are copied for
// session snapshots; route()/tick() mutate the pipe runtime state (RNG,
// sustain latch, note lengths), so callers serialize them like the rest of the
// matrix.
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>

#include "midifx.hpp"

namespace mnky::midi {

enum class Port : std::uint8_t { TrsIn1, TrsIn2, UsbIn, TrsOut1, TrsOut2, UsbOut };

// Fixed ports (0..5) keep the firmware ABI. Dynamic USB endpoints occupy
// 6..31; kMaxPorts must equal session::kSessionMaxPorts (32), which
// midi_glue.hpp asserts because midi/ stays independent of the session header.
inline constexpr int kPortCount = 6;
inline constexpr int kFirstDynamicPort = kPortCount;
inline constexpr int kMaxPorts = 32;
inline constexpr int kMaxDynamicPorts = kMaxPorts - kFirstDynamicPort;

constexpr int portId(Port p) { return static_cast<int>(p); }

// Any id with a mask/transpose/route slot: the six fixed ports and every
// dynamic id. (isInput/isOutput also encode the route role.)
constexpr bool isValidPort(Port p) { return portId(p) < kMaxPorts; }

constexpr bool isDynamicPort(Port p) {
  return portId(p) >= kFirstDynamicPort && portId(p) < kMaxPorts;
}

// Dynamic port id -> route endpoint. `id` must be in the dynamic range
// (PortRegistry only hands those out); Router accessors reject the rest.
constexpr Port dynamicPort(int id) { return static_cast<Port>(id); }

// A dynamic id is both a possible route source and destination: the direction
// is a property of the registered device (PortRegistry), not of the integer,
// because one USB device can carry an input and an output endpoint. The glue
// only produces events from connected input-capable devices and only sends to
// connected output-capable ones, so an unused role stays dormant.
constexpr bool isInput(Port p) {
  return p == Port::TrsIn1 || p == Port::TrsIn2 || p == Port::UsbIn ||
         isDynamicPort(p);
}

constexpr bool isOutput(Port p) {
  return p == Port::TrsOut1 || p == Port::TrsOut2 || p == Port::UsbOut ||
         isDynamicPort(p);
}

constexpr bool isFixedInput(Port p) {
  return p == Port::TrsIn1 || p == Port::TrsIn2 || p == Port::UsbIn;
}

constexpr bool isFixedOutput(Port p) {
  return p == Port::TrsOut1 || p == Port::TrsOut2 || p == Port::UsbOut;
}

// The one side-ownership rule for MIDI FX chains: the Pi evaluates chains on
// routes it alone can see. TRS->TRS belongs to firmware's route engine
// (firmware/pico/main.cpp route_pipes, fed by EV_MIDI_ROUTE_SEND); evaluating
// it here as well would process the same message twice. Any route touching a
// dynamic USB device is USB-side and therefore Pi-owned. The predicate answers
// "is this route USB-side", not "is this registry direction live"; Router does
// not own the registry, and a dormant/invalid direction simply never carries
// events.
constexpr bool piOwnsPipeChain(Port src, Port dst) {
  return src == Port::UsbIn || dst == Port::UsbOut || isDynamicPort(src) ||
         isDynamicPort(dst);
}

// One MIDI 1.0 message. `size` is 1..3 data bytes (SysEx is not forwarded, see
// docs/protocol.md); `device` is the dynamic port id for a per-device USB input
// (the enumeration index on the legacy merged UsbIn path) and 0 elsewhere;
// `stampUs` is the Pi monotonic capture time.
struct MidiEvent {
  Port source{Port::TrsIn1};
  std::uint8_t device{};
  std::uint8_t size{};
  std::uint8_t data[3]{};
  std::uint64_t stampUs{};
};

// destination port, message (already channel-filtered, piped and transposed).
using DeliverFn = std::function<void(Port dst, const MidiEvent& ev)>;

// Routes default to off: the app owns the routing matrix, and a default-on
// matrix would echo USB input back out of UsbOut as soon as a client loops our
// own virtual port back to us.
class Router {
 public:
  static constexpr std::uint16_t kAllChannels = 0xFFFF;

  Router() { channelMask_.fill(kAllChannels); }

  // Only input -> output pairs exist; any other pair is rejected (false).
  bool setRoute(Port src, Port dst, bool enabled);
  bool routeEnabled(Port src, Port dst) const;

  // Channel filter for channel voice (0x80..0xEF) from `src`. Bit n is MIDI
  // channel n+1, so 0x0001 = channel 1 only, kAllChannels = no filtering.
  // Applies to every source port; default kAllChannels.
  void setChannelMask(Port src, std::uint16_t mask);
  std::uint16_t channelMask(Port src) const;  // kAllChannels for an invalid id

  // Semitone shift applied to note on/off (0x80/0x90) from `src`; clamped to
  // +/-127. Notes that the shift pushes outside 0..127 are dropped rather than
  // clamped, so a shifted pair never lands on the wrong pitch.
  void setTranspose(Port src, int semitones);
  int transpose(Port src) const;  // 0 for an invalid id

  // Ordered Wave-1 chain for one route (midifx.hpp). The six fixed ports keep
  // their 3x3 slot array; any route touching a dynamic USB id stores its chain
  // in a sparse map, so a per-device USB route can carry a scoped chain too.
  // Only piOwnsPipeChain() routes evaluate a stored chain here; a TRS->TRS
  // chain is the Pi's mirror of what the link configured on the Pico. Replaces
  // the chain and resets its runtime state. False for an invalid pair, a type
  // outside 1..16, or a chain over the 8-pipe/10-param wire limits; the
  // previous chain then stays untouched.
  bool setPipes(Port src, Port dst, const PipeConfig* pipes, std::size_t count);
  std::size_t pipeCount(Port src, Port dst) const;
  // Drops every fixed and dynamic chain (project load is authoritative).
  void clearPipes();

  // Deterministic per-route RNG seed for Chance/VelocityRandom; 0 maps to 1.
  // Pipe streams survive events, so restoring the same seed replays decisions.
  void setPipeSeed(Port src, Port dst, std::uint32_t seed);
  std::uint32_t pipeSeed(Port src, Port dst) const;

  // Fixed-length NoteLength offs are due on the time line, not on an input
  // event: call this from the link/pump loop with a monotonic millisecond
  // clock on the same timeline as ev.stampUs (piNowUs()/1000). route() also
  // flushes due offs for the event's own route at ev.stampUs/1000, so a
  // fake-stamp test is deterministic without a timer.
  void tick(std::uint32_t nowMs, const DeliverFn& deliver) const;

  // Wave-2 chains configure but refuse evaluation; this is the log seam.
  // lastRejectReason is a static string from midifx.hpp and lastRejectPipe its
  // chain index, or -1 before the first rejection.
  struct PipeDiagnostics {
    std::uint64_t evaluations = 0;
    std::uint64_t rejected = 0;
    std::uint64_t emitted = 0;
    std::uint64_t dropped = 0;
    const char* lastRejectReason = nullptr;
    int lastRejectPipe = -1;
  };
  PipeDiagnostics pipeDiagnostics() const;

  // Delivers `ev` to every enabled destination for its source. The channel
  // mask runs first, then the route's pipe chain (Pi-owned routes only), then
  // the source transpose; realtime and system common (0xF0..0xFF) are never
  // channel-masked or transposed (a Filter pipe can still drop them by config).
  void route(const MidiEvent& ev, const DeliverFn& deliver) const;

 private:
  static constexpr int idx(Port p) { return static_cast<int>(p); }
  // Fixed input->output pair to its 3x3 pipe slot (0 = TRS IN1/OUT1), or -1
  // when either side is dynamic (sparse map instead).
  static constexpr int pipeIndex(Port src, Port dst) {
    if (!isFixedInput(src) || !isFixedOutput(dst)) return -1;
    const int in = src == Port::TrsIn1 ? 0 : (src == Port::TrsIn2 ? 1 : 2);
    const int out = dst == Port::TrsOut1 ? 0 : (dst == Port::TrsOut2 ? 1 : 2);
    return in * 3 + out;
  }
  static constexpr int dynamicPipeKey(Port src, Port dst) {
    return idx(src) * kMaxPorts + idx(dst);
  }
  // Existing chain for a route, or nullptr. Dynamic chains are created only by
  // setPipes()/setPipeSeed(); route()/tick() never allocate.
  PipeChain* mutableChain(Port src, Port dst) const;

  void deliverPipeOutput(const PipeOutput& output, Port dst, const MidiEvent& ev,
                         int shift, const DeliverFn& deliver) const;
  void recordRejection(const PipeOutput& output) const;

  std::array<std::uint16_t, kMaxPorts> channelMask_{};
  std::array<int, kMaxPorts> transpose_{};
  std::array<std::array<bool, kMaxPorts>, kMaxPorts> route_{};
  // Pipe runtime state (RNG, sustain latch, note lengths) is logically part of
  // the route, not of the caller: route()/tick() are const but advance it. The
  // array stays 3x3 for the fixed pairs; dynamic pairs use the sparse map.
  mutable std::array<PipeChain, 9> pipes_{};
  // Sparse chain storage for routes touching dynamic USB ids (key =
  // src * kMaxPorts + dst). Only configured routes exist here.
  mutable std::map<int, PipeChain> dynamicPipes_{};
  mutable PipeDiagnostics pipeStats_{};
};

}  // namespace mnky::midi

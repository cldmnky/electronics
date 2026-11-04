#pragma once
// MIDI routing matrix for the mnkybox Pi core. JUCE-free, host-testable.
//
// Port taxonomy is fixed by pi/ARCHITECTURE.md: three merged sources (the two
// TRS UARTs plus one USB source that merges every open device) and three
// destinations (the two TRS UARTs plus USB fan-out to every open output).
#include <cstdint>
#include <functional>

namespace mnky::midi {

enum class Port : std::uint8_t { TrsIn1, TrsIn2, UsbIn, TrsOut1, TrsOut2, UsbOut };

inline constexpr int kPortCount = 6;

constexpr bool isInput(Port p) {
  return p == Port::TrsIn1 || p == Port::TrsIn2 || p == Port::UsbIn;
}

constexpr bool isOutput(Port p) {
  return p == Port::TrsOut1 || p == Port::TrsOut2 || p == Port::UsbOut;
}

// One MIDI 1.0 message. `size` is 1..3 data bytes (SysEx is not forwarded, see
// docs/protocol.md); `device` is the USB device index for Port::UsbIn and 0
// elsewhere; `stampUs` is the Pi monotonic capture time.
struct MidiEvent {
  Port source{Port::TrsIn1};
  std::uint8_t device{};
  std::uint8_t size{};
  std::uint8_t data[3]{};
  std::uint64_t stampUs{};
};

// destination port, message (already channel-filtered and transposed).
using DeliverFn = std::function<void(Port dst, const MidiEvent& ev)>;

// Routes default to off: the app owns the routing matrix, and a default-on
// matrix would echo USB input back out of UsbOut as soon as a client loops our
// own virtual port back to us.
class Router {
 public:
  static constexpr std::uint16_t kAllChannels = 0xFFFF;

  // Only input -> output pairs exist; any other pair is rejected (false).
  bool setRoute(Port src, Port dst, bool enabled);
  bool routeEnabled(Port src, Port dst) const;

  // Channel filter for channel voice (0x80..0xEF) from `src`. Bit n is MIDI
  // channel n+1, so 0x0001 = channel 1 only, kAllChannels = no filtering.
  // Applies to every source port; default kAllChannels.
  void setChannelMask(Port src, std::uint16_t mask);
  std::uint16_t channelMask(Port src) const;

  // Semitone shift applied to note on/off (0x80/0x90) from `src`; clamped to
  // +/-127. Notes that the shift pushes outside 0..127 are dropped rather than
  // clamped, so a shifted pair never lands on the wrong pitch.
  void setTranspose(Port src, int semitones);
  int transpose(Port src) const;

  // Delivers `ev` to every enabled destination for its source. Realtime and
  // system common (0xF0..0xFF) always pass, untransposed and unfiltered.
  void route(const MidiEvent& ev, const DeliverFn& deliver) const;

 private:
  static constexpr int idx(Port p) { return static_cast<int>(p); }

  std::uint16_t channelMask_[kPortCount] = {kAllChannels, kAllChannels, kAllChannels,
                                            kAllChannels, kAllChannels, kAllChannels};
  int transpose_[kPortCount] = {};
  bool route_[kPortCount][kPortCount] = {};
};

}  // namespace mnky::midi

#pragma once
// Engine capacity taxonomy. Single-sourced here because
// take_player.hpp cannot include engine.hpp (engine.hpp includes it).
//
//   kMaxDeviceInputs (18)  device capture width: UMC1820 16 analog inputs plus
//                          2 S/PDIF channels. Engine::process() reads at most
//                          this many device channels; the device pump and the
//                          device backends use the same width.
//   kMaxTracks (64)        mixer tracks (strips). Each track is mono (one
//                          internal channel) or stereo (two), and every
//                          per-track array is sized to this: Channel state,
//                          input selection/pair, output destination, EQ band
//                          settings, insert, take selection/loop/region, take
//                          voice and the recorder tap.
//   kMaxChannels (128)     internal mix channels. Track t owns channels 2t
//                          (primary/left) and 2t+1 (secondary/right, stereo
//                          tracks only), so 64 stereo tracks are 128 channels.
//                          The per-channel input map and the per-channel EQ
//                          filter state are sized to this.
//   kMaxInputs (18)        legacy name for the device width. It stays the
//                          device-input concept (the frozen audio tests and
//                          the device/tap call sites use it that way); track
//                          code uses kMaxTracks.

namespace mnky::audio {

/// UMC1820 capture width: 16 analog inputs plus 2 S/PDIF channels.
inline constexpr int kMaxDeviceInputs = 18;

/// Mixer tracks; each owns one (mono) or two (stereo) internal channels.
inline constexpr int kMaxTracks = 64;

/// Internal mix channels: two per stereo track, one per mono track.
inline constexpr int kMaxChannels = kMaxTracks * 2;

/// Legacy spelling of the device width (see the taxonomy above).
inline constexpr int kMaxInputs = kMaxDeviceInputs;

static_assert(kMaxChannels == 2 * kMaxTracks,
              "each track owns at most two internal channels");

}  // namespace mnky::audio

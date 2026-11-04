#pragma once
// Versioned session state for the mnkybox Pi DAW (JUCE-free, portable C++20).
//
// One session is one UTF-8 JSON file:
//   {"app":"mnkybox","session":1,
//    "router":{"routes":[{"src":0,"dst":5,"on":true}],
//              "channels":{"0":65535},"transpose":{"0":0}},
//    "mixer":{"channels":[{"gain":1,"pan":0.5,"mute":false,"solo":false,
//                          "arm":false,"send":[0,0]} x18],"master":1},
//    "fx":{"bus":[{"id":"com.example.gain","params":[0.5],"bypass":false} x2]},
//    "transport":{"bpm_x100":12000},
//    "click":{"enabled":false,"level":0.5,"bars":1},
//    "midimap":[{"cc":74,"ch":1,"target":"mixer.ch3.gain","track":3}]}
// Port ids are midi::Port's underlying integers. FX ids are CLAP plugin ids or
// "builtin.<name>"; parameter indices are the effect's stable order (owned and
// documented by the FX team). "click" and "midimap" are additive version-1
// sections: capture() leaves them at their defaults and apply() does not touch
// them, because the app owns the metronome and MIDI-learn tables. The reader
// tolerates unknown fields, defaults missing ones and rejects corrupt input
// with a reason string; it never throws across this API.
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "../audio/engine.hpp"
#include "../midi/clock.hpp"
#include "../midi/router.hpp"

namespace mnky::session {

inline constexpr int kVersion = 1;
inline constexpr const char* kAppId = "mnkybox";
// Two FX send buses, matching audio::kNumSends.
inline constexpr int kFxBuses = audio::kNumSends;

struct RouteState {
  int src = 0;
  int dst = 0;
  bool on = false;
  friend bool operator==(const RouteState&, const RouteState&) = default;
};

// Router state is a full snapshot: every port's channel mask and transpose are
// present, so a default RouterState is the documented "all channels, no shift".
struct RouterState {
  RouterState() { channels.fill(midi::Router::kAllChannels); }

  std::vector<RouteState> routes;
  std::array<std::uint16_t, midi::kPortCount> channels{};
  std::array<int, midi::kPortCount> transpose{};
  friend bool operator==(const RouterState&, const RouterState&) = default;
};

struct MixerChannelState {
  float gain = 1.0f;
  float pan = 0.5f;
  bool mute = false;
  bool solo = false;
  bool arm = false;
  std::array<float, audio::kNumSends> send{};
  friend bool operator==(const MixerChannelState&, const MixerChannelState&) = default;
};

struct MixerState {
  std::array<MixerChannelState, audio::kMaxInputs> channels{};
  float master = 1.0f;
  friend bool operator==(const MixerState&, const MixerState&) = default;
};

struct FxBusState {
  std::string id;  // empty = empty slot
  std::vector<double> params;
  bool bypass = false;
  friend bool operator==(const FxBusState&, const FxBusState&) = default;
};

struct FxState {
  std::array<FxBusState, kFxBuses> bus{};
  friend bool operator==(const FxState&, const FxState&) = default;
};

struct TransportState {
  int bpmX100 = midi::UsbClock::kDefaultBpmX100;
  friend bool operator==(const TransportState&, const TransportState&) = default;
};

// Metronome configuration. `level` is a linear 0..1 click level; `bars` is the
// count-in length in bars. Absence means click off with these neutral defaults.
struct ClickState {
  bool enabled = false;
  float level = 0.5f;
  int bars = 1;
  friend bool operator==(const ClickState&, const ClickState&) = default;
};

// One MIDI-learn binding. `target` is a dotted app parameter path such as
// "mixer.ch3.gain" or "fx.0.mix"; the session layer keeps unknown targets and
// lets the app resolve them. `cc` is 0..127 and `ch` is 1..16.
struct MidiMapEntry {
  int cc = 0;
  int ch = 1;
  std::string target;
  int track = 0;
  friend bool operator==(const MidiMapEntry&, const MidiMapEntry&) = default;
};

struct Snapshot {
  RouterState router;
  MixerState mixer;
  FxState fx;
  TransportState transport;
  ClickState click;
  std::vector<MidiMapEntry> midimap;
  friend bool operator==(const Snapshot&, const Snapshot&) = default;
};

// The seam between the portable session core and the app's CLAP slot owner.
// effectId() is the CLAP plugin id or "builtin.<name>", empty for an empty
// slot. loadEffect() returns false when the effect is unavailable; apply()
// then leaves that slot untouched (tolerant load). Parameter indices are the
// effect's stable order.
class FxSlotTarget {
 public:
  virtual ~FxSlotTarget() = default;
  virtual std::string effectId() const = 0;
  virtual bool loadEffect(const std::string& id) = 0;
  virtual void clearEffect() = 0;  // idempotent
  virtual std::size_t paramCount() const = 0;
  virtual double paramValue(std::size_t index) const = 0;
  virtual bool setParamValue(std::size_t index, double value) = 0;
  virtual bool bypassed() const = 0;
  virtual void setBypassed(bool bypass) = 0;
};

// Reads the live Router/Engine (and optional FX slots) into a session snapshot.
// Ports 0..kPortCount-1 and all 18 mixer strips are always captured, so the
// result is canonical and save/load round-trips exactly. The click and midimap
// sections stay at their defaults; the app fills them before save().
Snapshot capture(const midi::Router& router, const audio::Engine& engine,
                 std::span<FxSlotTarget*> fxSlots = {});

// Applies a snapshot to the live objects. The router and mixer are replaced
// wholesale (defaults for anything the snapshot omits); FX slots are updated
// only when the effect id can be loaded. Invalid port/route ids are ignored.
// Click and midimap are app-owned and are not applied here.
void apply(const Snapshot& snapshot, midi::Router& router, audio::Engine& engine,
           std::span<FxSlotTarget*> fxSlots = {});

// Writes the fixed JSON object. Returns false on open/write failure.
bool save(const std::string& path, const Snapshot& snapshot) noexcept;

struct LoadResult {
  bool ok = false;
  std::string error;  // human-readable reason when ok == false
  Snapshot snapshot;
};

// Reads and validates a session file. Version != 1 is rejected, as is any
// malformed JSON or wrong-typed known field; missing fields take defaults and
// unknown fields are ignored. Never throws.
LoadResult load(const std::string& path) noexcept;

}  // namespace mnky::session

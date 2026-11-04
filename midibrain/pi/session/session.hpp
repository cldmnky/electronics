#pragma once
// Versioned session state for the mnkybox Pi DAW (JUCE-free, portable C++20).
//
// One session is one UTF-8 JSON file:
//   {"app":"mnkybox","session":2,"template":false,
//    "router":{"routes":[{"src":0,"dst":5,"on":true}],
//              "channels":{"0":65535},"transpose":{"0":0}},
//    "mixer":{"channels":[{"gain":1,"pan":0.5,"mute":false,"solo":false,
//                          "arm":false,"send":[0,0],"cue":0,"out34":0} x18],
//             "master":1,"out34mode":0},
//    "fx":{"bus":[{"id":"com.example.gain","params":[0.5],"bypass":false} x2]},
//    "transport":{"bpm_x100":12000},
//    "click":{"mode":0,"output":0,"gainDb":0,"bars":1},
//    "midimap":[{"cc":74,"ch":1,"target":"mixer.ch3.gain","track":3}],
//    "takes":{"active":{"0":"","1":"02-drums-003.wav"}},
//    "punch":{"playFrom":{"enabled":false,"measure":1},
//               "in":{"enabled":false,"measure":1},
//               "out":{"enabled":false,"measure":1}},
//    "cue":{"phonesSrc":0,"style":0},
//    "sends":{"cue":1,"fx1":1,"fx2":1,"out34":1,"rec":1},
//    "mainComp":{"on":false,"threshDb":-12,"ratio":2,"attackMs":10,
//                  "releaseMs":100,"makeupDb":0,"mode":0}}
// Port ids are midi::Port's underlying integers. FX ids are CLAP plugin ids or
// "builtin.<name>"; parameter indices are the effect's stable order (owned and
// documented by the FX team). "click", "midimap", "takes", "punch", "cue",
// "sends" and "mainComp" are additive version-2 sections: capture() leaves
// them at their defaults and apply() does not touch them, because the app
// owns the metronome, take registry, punch points, monitor and bus-comp
// state. The reader accepts version 1 (mapping legacy click enabled/level)
// and version 2; it tolerates unknown fields, defaults missing ones and
// rejects corrupt input with a reason string; it never throws across this API.
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

inline constexpr int kVersion = 2;
// Lowest session version this build reads (v1 click maps to v2 fields).
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
  float cueSend = 0.0f;    // per-track CUE bus send level (v2)
  float out34Send = 0.0f;  // per-track OUT3&4 send level (v2)
  // Input monitoring (MPC-style): 0 = off (never pass live input),
  // 1 = auto (live input iff armed, or no active take playing), 2 = on.
  int monitor = 1;
  friend bool operator==(const MixerChannelState&, const MixerChannelState&) = default;
};

struct MixerState {
  std::array<MixerChannelState, audio::kMaxInputs> channels{};
  float master = 1.0f;
  int out34Mode = 0;  // v2: 0 = OUT3&4 stereo pair, 1 = two mono sends
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

// Per-output clock/transport routing. `trs` has one entry per TRS output
// (index 0 = TRS OUT1, 1 = TRS OUT2); `usb` covers the USB mirror output.
// Defaults preserve the legacy always-on behavior.
struct ClockRoute {
  bool clock = true;
  bool transport = true;
  friend bool operator==(const ClockRoute&, const ClockRoute&) = default;
};
struct ClockState {
  std::array<ClockRoute, 2> trs{};
  ClockRoute usb;
  friend bool operator==(const ClockState&, const ClockState&) = default;
};

// Metronome configuration (v2). `mode`: 0 = off, 1 = record-only, 2 = on-play.
// `output`: 0 = main 1&2, 1 = phones, 2 = out 3&4. `gainDb` is -96..+20.
// `bars` is the count-in length in bars. `enabled`/`level` are v1 legacy:
// the reader maps them (enabled -> mode, level 0..1 -> ~gainDb) and the
// writer no longer emits them. Absence means click off, main out, 0 dB.
struct ClickState {
  bool enabled = false;
  float level = 0.5f;
  int bars = 1;
  int mode = 0;
  int output = 0;
  float gainDb = 0.0f;
  friend bool operator==(const ClickState&, const ClickState&) = default;
};

// One punch/transport marker: start or stop recording/playback at a measure
// boundary when enabled. Measures are 1-based; the reader clamps below 1.
struct PunchPoint {
  bool enabled = false;
  int measure = 1;
  friend bool operator==(const PunchPoint&, const PunchPoint&) = default;
};

struct PunchState {
  PunchPoint playFrom;
  PunchPoint in;
  PunchPoint out;
  friend bool operator==(const PunchState&, const PunchState&) = default;
};

// Active take per track: empty string means the live input (no take
// playback). Non-empty is the take file name stem the track plays back when
// disarmed. Newest-wins on overlap is engine policy, not stored here.
// LoopState is per-track take looping in beats (fractional allowed):
// enabled with end <= start loops the whole active take.
struct LoopState {
  bool enabled = false;
  double startBeats = 0.0;
  double endBeats = 0.0;
  friend bool operator==(const LoopState&, const LoopState&) = default;
};
// One non-destructive region edit: play `lengthBeats` of `take` starting at
// `offsetBeats` (length 0 = to end of take), with gain/fades applied at
// render. `normalize` peak-normalizes the region in one pass. Files are
// never rewritten by edits; an explicit render-to-new-take op (not stored
// here) is the only destructive path.
struct TakeRegion {
  std::string take;
  double offsetBeats = 0.0;
  double lengthBeats = 0.0;
  float gainDb = 0.0f;  // -96..+12
  double fadeInBeats = 0.0;
  double fadeOutBeats = 0.0;
  bool normalize = false;
  friend bool operator==(const TakeRegion&, const TakeRegion&) = default;
};
struct TakesState {
  std::array<std::string, audio::kMaxInputs> active{};
  std::array<LoopState, audio::kMaxInputs> loop{};
  std::array<std::vector<TakeRegion>, audio::kMaxInputs> edit{};
  friend bool operator==(const TakesState&, const TakesState&) = default;
};

// Monitor configuration. `phonesSrc`: 0 = CUE mix, 1 = Main duplicate.
// `style`: 0 = per-track CUE fader levels, 1 = per-track CUE toggle.
struct CueState {
  int phonesSrc = 0;
  int style = 0;
  friend bool operator==(const CueState&, const CueState&) = default;
};

// Per-destination send source: 0 = pre-fader (gain applied, vol not),
// 1 = post-fader. Gain is always applied upstream of both.
struct SendsState {
  int cue = 1;
  int fx1 = 1;
  int fx2 = 1;
  int out34 = 1;
  int rec = 1;
  friend bool operator==(const SendsState&, const SendsState&) = default;
};

// Main-bus compressor (subset of the classic parameter set). Threshold in dB
// (-48..0), ratio 1..100, attack/release in ms, makeup in dB (-36..+36),
// `mode`: 0 = peak, 1 = RMS. Applied to Main 1&2 (+phones when phonesSrc
// is Main); never to OUT3&4.
struct MainCompState {
  bool on = false;
  float threshDb = -12.0f;
  float ratio = 2.0f;
  float attackMs = 10.0f;
  float releaseMs = 100.0f;
  float makeupDb = 0.0f;
  int mode = 0;
  friend bool operator==(const MainCompState&, const MainCompState&) = default;
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
  ClockState clock;
  ClickState click;
  std::vector<MidiMapEntry> midimap;
  bool isTemplate = false;  // v2: settings-only project, no take registry
  TakesState takes;
  PunchState punch;
  CueState cue;
  SendsState sends;
  MainCompState mainComp;
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
// result is canonical and save/load round-trips exactly. The app-owned
// sections (click, midimap, takes, punch, cue, sends, mainComp, isTemplate)
// stay at their defaults; the app fills them before save().
Snapshot capture(const midi::Router& router, const audio::Engine& engine,
                 std::span<FxSlotTarget*> fxSlots = {});

// Applies a snapshot to the live objects. The router and mixer are replaced
// wholesale (defaults for anything the snapshot omits); FX slots are updated
// only when the effect id can be loaded. Invalid port/route ids are ignored.
// The app-owned sections (click, midimap, takes, punch, cue, sends,
// mainComp, isTemplate, out34mode and the per-channel cue/out34 sends) are
// not applied here; the app forwards them once the engine exposes them.
void apply(const Snapshot& snapshot, midi::Router& router, audio::Engine& engine,
           std::span<FxSlotTarget*> fxSlots = {});

// Writes the fixed JSON object. Returns false on open/write failure.
bool save(const std::string& path, const Snapshot& snapshot) noexcept;

struct LoadResult {
  bool ok = false;
  std::string error;  // human-readable reason when ok == false
  Snapshot snapshot;
};

// Reads and validates a session file. Versions 1 and 2 are accepted (v1
// click enabled/level map onto the v2 mode/output/gainDb fields); anything
// else is rejected, as is any malformed JSON or wrong-typed known field;
// missing fields take defaults and unknown fields are ignored. Never throws.
LoadResult load(const std::string& path) noexcept;

}  // namespace mnky::session

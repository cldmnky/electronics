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

// MIDI FX pipe types (stable ids; positional int params documented below).
// Wave 1 (stateless): Filter..Transform. Wave 2 (clocked): Arp..MidiDelay.
enum class MidiPipeType : std::uint8_t {
  Filter = 1,        // [noteOn,noteOff,cc,pc,press,bend,realtime,sysex] 1 = pass
  NoteRange = 2,     // [lo,hi,velLo,velHi] note + velocity window
  CcRemap = 3,       // [inLo,inHi,outLo,outHi] linear range map
  ChannelRemap = 4,  // [inCh 0 = any, outCh 1..16]
  ScaleQuantize = 5, // [scale 0 = off,1 major,2 minor,3 pentaMaj,4 pentaMin,
                       //  5 dorian,6 user; root 0..11; mask12 pitch classes]
  Chance = 6,        // [percent 0..100] drop notes by chance
  VelocityRandom = 7,// [amount 0..127 humanize +/-, minVel 0..127 floor]
  Dispatcher = 8,    // [numCh 2..16, startCh 1..16] round-robin note spread
  Sustain = 9,       // [latch 0/1] drop note-offs while latched
  NoteLength = 10,   // [lengthMs 0 = off, else fixed note length]
  Transform = 11,    // [matchKind,matchLo,matchHi,outKind,outVal] kinds:
                       //  0 note,1 cc,2 pc,3 bend,4 pressure
  Arp = 12,          // [div 0=1/4,1=1/8,2=1/16,3=1/32; type 0 up,1 down,
                       //  2 updown,3 random,4 chord; octaves 1..4;
                       //  swing 0..100; accent 0..100; gatePct 1..100]
  Repeater = 13,     // [div 0..3 as Arp; repeats 1..8; falloff 0..100]
  CcLfo = 14,        // [cc 0..127; wave 0 sine,1 tri,2 square,3 saw;
                       //  div 0..3; depth 0..127; center 0..127]
  Harmonizer = 15,   // [n 0..4 voices; i1..i4 semitone offsets -48..48]
  MidiDelay = 16,    // [delayMs 0..2000; repeats 0..8; falloff 0..100]
};
struct MidiPipe {
  MidiPipeType type = MidiPipeType::Filter;
  bool bypass = false;
  std::vector<int> params;
  friend bool operator==(const MidiPipe&, const MidiPipe&) = default;
};
struct RouteState {
  int src = 0;
  int dst = 0;
  bool on = false;
  std::vector<MidiPipe> pipes;  // evaluated in order where bytes flow
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
  // Input source: mono device channel 0..17, or -1 = none. `inPair` binds
  // the next channel as a stereo pair (inCh must be even; odd clamps down).
  // Output destination: 0 = Main 1&2, 1 = OUT3&4, 2 = phones, 3 = none.
  int inCh = 0;
  bool inPair = false;
  int outDst = 0;
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

// EQ band: `type` 0 = none, 1 = low cut, 2 = low shelf, 3 = parametric,
// 4 = high shelf, 5 = high cut. `gainDb` unused by cut types. Stored per
// track + main; the engine has no EQ insert yet, so these are edited,
// displayed and project-persisted ahead of the DSP seam (see NOTES).
struct EqBand {
  int type = 0;
  float gainDb = 0.0f;
  float freqHz = 1000.0f;
  float q = 1.0f;
  friend bool operator==(const EqBand&, const EqBand&) = default;
};
struct EqState {
  std::array<std::array<EqBand, 4>, audio::kMaxInputs> tracks{};
  std::array<EqBand, 4> main{};
  friend bool operator==(const EqState&, const EqState&) = default;
};
// X/Y performance grid positions, normalized 0..1. Delay X = feedback-
// filter cutoff, Y = width; reverb X = size, Y = level. The app maps these
// onto live effect params; the last position persists per project.
struct XyState {
  float delayX = 0.5f;
  float delayY = 0.5f;
  float reverbX = 0.5f;
  float reverbY = 0.5f;
  friend bool operator==(const XyState&, const XyState&) = default;
};
struct FxState {
  std::array<FxBusState, kFxBuses> bus{};
  EqState eq;
  XyState xy;
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
  int tempoDiv = 1;    // clock divider: 1, 2 or 4 (drop multiples)
  int syncDelayMs = 0;  // clock delay 0..500 ms to align late hardware
  friend bool operator==(const ClockRoute&, const ClockRoute&) = default;
};
// External clock follow. `source`: 0 = internal generator (legacy behavior),
// 1 = TRS IN1, 2 = TRS IN2. (USB as a clock source is planned: the Pico has
// no USB-device view, so Pi-forwarded 24ppq ticks would reintroduce the
// jitter this removes; the reader clamps 3+ back to internal.)
// INVARIANT: the Pico always sends clock. Follow disciplines the generator
// (tempo + phase); it never gates, mutes or stops it. Loss of the external
// source coasts at the last estimate — downstream gear never sees the clock
// disappear. The per-output clock toggles gate wires, never the generator.
// `transportFollow` gates whose transport bytes drive us: index 0 = TRS IN1,
// 1 = TRS IN2, 2 = USB (Pi-side: forwarded as EV_TRANSPORT when enabled).
// All default off — the Pico stays master until configured otherwise.
struct ClockState {
  std::array<ClockRoute, 2> trs{};
  ClockRoute usb;
  int source = 0;
  std::array<bool, 3> transportFollow{false, false, false};
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

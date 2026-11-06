#pragma once
// Versioned session state for the mnkybox Pi DAW (JUCE-free, portable C++20).
//
// One session is one UTF-8 JSON file:
//   {"app":"mnkybox","session":2,"template":false,
//    "router":{"routes":[{"src":0,"dst":5,"on":true,"pipes":[]},
//                        {"src":-1,"dst":2,"on":true,"pipes":[]}],
//              "channels":{"0":65535},"transpose":{"0":0}},
//    "mixer":{"tracks":[{"slot":0,"name":"kick"},{"slot":1,"name":""}],
//             "channels":[{"gain":1,"pan":0.5,"mute":false,"solo":false,
//                          "arm":false,"send":[0,0],"cue":0,"out34":0,
//                          "insert":{"id":"","bypass":false,
//                                     "params":[]},
//                          "fxSlots":[{"id":"","bypass":false,"wet":1,
//                                       "params":[]},
//                                      {"id":"","bypass":false,"wet":1,
//                                       "params":[]}]} x audio::kMaxInputs],
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
// documented by the FX team). Additive version-2 keys extend the layout
// without a version bump, because their absence is exactly the legacy state:
//   "mixer.tracks"   ordered list of the slots that exist (see MixerState);
//                    absent = all audio::kMaxInputs slots in order.
//   "mixer.channels[i].insert"  DEPRECATED legacy single per-track FX insert
//                    (see FxInsertState): read as FX1 when "fxSlots" is absent
//                    and kept for old files/old readers only.
//   "mixer.channels[i].fxSlots"  the per-track FX rack: exactly two
//                    FxInsertState objects, FX1 then FX2, post-EQ pre-fader and
//                    in series (see the table below). Wins over "insert" when
//                    a file carries both.
//   "router.routes[i].src" -1 / -2  generator and output-insert chains (see
//                    kSrcGenerator / kSrcOutputInsert); absent routes keep the
//                    documented src 0..31 -> dst 0..31 port pairs.
// "click", "midimap", "takes", "punch", "cue",
// "sends" and "mainComp" are additive version-2 sections: capture() leaves
// them at their defaults and apply() does not touch them, because the app
// owns the metronome, take registry, punch points, monitor and bus-comp
// state. The reader accepts version 1 (mapping legacy click enabled/level)
// and version 2; it tolerates unknown fields, defaults missing ones and
// rejects corrupt input with a reason string; it never throws across this API.
#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <vector>

#include "../audio/engine.hpp"
#include "../midi/clock.hpp"
#include "../midi/router.hpp"

namespace mnky::session {

inline constexpr int kVersion = 2;
// Dynamic (USB) port ids live at 6..31; 0..5 are the fixed TRS/merged ports.
inline constexpr int kSessionMaxPorts = 32;
// Lowest session version this build reads (v1 click maps to v2 fields).
inline constexpr const char* kAppId = "mnkybox";
// Track display names are short strings that also end up in take filenames;
// the reader truncates longer ones at a UTF-8 boundary (bytes, not codepoints).
inline constexpr std::size_t kMaxTrackNameBytes = 64;
// Two FX send buses, matching audio::kNumSends.
inline constexpr int kFxBuses = audio::kNumSends;
// Per-track FX rack size. Index 0 is FX1, index 1 is FX2; the engine runs
// FX1 -> FX2 in series, post-EQ and pre-fader.
inline constexpr int kTrackFxSlots = 2;

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
  // Channel scope: the pipe processes only messages whose channel (0-based)
  // has its bit set; other channels pass through unchanged. 0xFFFF = all.
  // Combined with per-device routes this gives "this arp on this device's
  // channels 1-4 only" exactly.
  std::uint16_t chMask = 0xFFFF;
  std::vector<int> params;
  friend bool operator==(const MidiPipe&, const MidiPipe&) = default;
};
// Pseudo-sources for route kinds beyond src->dst pairs:
//   kSrcGenerator (-1): chain emits from generator pipes (chain head must be
//     generator-capable per pipes.json role); no input consumed. Target is a
//     real output.
//   kSrcOutputInsert (-2): every message routed to `dst` passes this chain
//     first (an insert on the output; use for timing shifts on a sync
//     channel etc.). Multiple inserts on one dst chain in route order.
// Route endpoints are range-checked on read: src is one of the two pseudo
// sources or a real port 0..kSessionMaxPorts-1 (so -2..31), dst is a real port
// 0..kSessionMaxPorts-1. The router has no pseudo port: apply() forwards a
// pseudo route to Router::setGenerator*/setOutputInsert* when its destination
// is Pi-owned (USB) and skips it when the Pico owns that output (TRS), where
// the link configures it over 0x18/0x19. capture() reads the enabled Pi-side
// pseudo chains back; real port pairs stay as before (the app owns their
// chains).
inline constexpr int kSrcGenerator = -1;
inline constexpr int kSrcOutputInsert = -2;

struct RouteState {
  int src = 0;
  int dst = 0;
  bool on = false;
  std::vector<MidiPipe> pipes;  // evaluated in order where bytes flow
  friend bool operator==(const RouteState&, const RouteState&) = default;
};

// Router state is a full snapshot: every port's channel mask and transpose are
// present, so a default RouterState is the documented "all channels, no shift".
// `devices` binds dynamic port ids (6..31; discovered USB MIDI devices) to a
// stable identity string ("usb:<name>" convention), so routes/channels survive
// restarts and replug order: the app re-binds by name at load, and a missing
// device leaves its routes dormant but intact, never silently reassigned.
struct RouterState {
  RouterState() { channels.fill(midi::Router::kAllChannels); }

  std::vector<RouteState> routes;
  std::array<std::uint16_t, kSessionMaxPorts> channels{};
  std::array<int, kSessionMaxPorts> transpose{};
  std::map<int, std::string> devices;
  friend bool operator==(const RouterState&, const RouterState&) = default;
};

// One per-track FX insert instance. The same object is one slot of the
// per-track FX rack (MixerChannelState::fxSlots) and the deprecated legacy
// single insert (MixerChannelState::insert).
//
// FROZEN WIRE CONTRACT for the engine and UI workers:
//   "fxSlots":[{"id":<string>,"bypass":<bool>,"wet":<0..1>,
//                "params":[<double>...]}, {...}]   // FX1, FX2
//   id      "" = no effect; "builtin.delay" | "builtin.reverb" | CLAP id
//   bypass  hard dry switch for this slot, independent of wet
//   wet     wet/dry mix for this slot, stored 0..1 (clamped on read, default
//           1.0); the FX1/FX2 mix knobs bind to this field
//   params  effect parameters, doubles in the effect's stable order (the same
//           contract as fx.bus: extra saved params are ignored on load and
//           the effect clamps its own ranges)
// Decoding a file without "fxSlots" migrates the legacy single "insert" into
// FX1 and the legacy per-track "send[i]" value into fxSlots[i].wet (a slot
// whose "wet" key is absent uses that value when the channel has one,
// otherwise 1.0); a file with "fxSlots" wins and its explicit wet is kept.
// "send" stays in the format and still round-trips, but it is deprecated: the
// FX1/FX2 knobs now bind to fxSlots[i].wet. apply() forwards both slots to
// Engine::setTrackFxSlot() and capture() reads them back (audio wave 11).
struct FxInsertState {
  std::string id;  // empty = none; "builtin.delay" | "builtin.reverb" | CLAP id
  bool bypass = false;
  float wet = 1.0f;  // 0..1 wet/dry mix, clamped on read
  std::vector<double> params;
  friend bool operator==(const FxInsertState&, const FxInsertState&) = default;
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
  // Input monitoring (MPC-style): 0 = playback only (never pass live input;
  // "just playback"), 1 = auto (live input iff armed, or no active take
  // playing), 2 = on (live input + take layer; "both playback and
  // monitoring"), 3 = monitor only (live input always, take never plays;
  // "just monitoring").
  int monitor = 1;
  // Input source: mono device channel 0..17, or -1 = none. `inPair` binds
  // the next channel as a stereo pair (inCh must be even; odd clamps down).
  // Output destination: 0 = Main 1&2, 1 = OUT3&4, 2 = phones, 3 = none.
  int inCh = 0;
  bool inPair = false;
  int outDst = 0;
  // DEPRECATED legacy per-track FX insert (v2): the old single effect
  // instance. Its wire form "insert":{"id","bypass","params"} stays in the
  // format for old files and old readers; a file without "fxSlots" reads it
  // as FX1, and the engine seam still applies/captures it as of audio wave 9.
  // New code must use `fxSlots`.
  FxInsertState insert;
  // Per-track FX rack (v2): exactly kTrackFxSlots effects, FX1 then FX2, run
  // in series post-EQ pre-fader by the engine. See FxInsertState for the
  // frozen per-slot fields and the legacy migration rules. apply() forwards
  // both slots to Engine::setTrackFxSlot() and capture() reads them back
  // (audio wave 11); the deprecated `insert` is the legacy FX1 view.
  std::array<FxInsertState, kTrackFxSlots> fxSlots;
  friend bool operator==(const MixerChannelState&, const MixerChannelState&) = default;
};

struct MixerState {
  // Per-slot configuration store (fixed capacity by design: the audio path
  // never allocates). Which slots EXIST is dynamic: `tracks` is the ordered
  // list of active slots; the mixer shows exactly these, in this order.
  // Absent "tracks" (legacy sessions) = all slots active in order; an empty
  // list is valid (new empty project).
  struct TrackEntry {
    int slot = 0;        // 0..audio::kMaxInputs-1
    std::string name;    // display name (also embedded in take filenames)
    friend bool operator==(const TrackEntry&, const TrackEntry&) = default;
  };
  // The identity list: slot i named "", i.e. the legacy full-capacity layout.
  // It is both the default value of `tracks` and what an absent "tracks" key
  // reads as, so a default MixerState is exactly a legacy session. The entry
  // count follows audio::kMaxInputs, never a literal.
  static std::vector<TrackEntry> allSlots() {
    std::vector<TrackEntry> list;
    list.reserve(static_cast<std::size_t>(audio::kMaxInputs));
    for (int slot = 0; slot < audio::kMaxInputs; ++slot) {
      list.push_back(TrackEntry{slot, {}});
    }
    return list;
  }
  std::array<MixerChannelState, audio::kMaxInputs> channels{};
  // Wire form: "tracks":[{"slot":n,"name":"..."},...]. An absent key keeps
  // allSlots(); an empty array is a valid empty project. Slots are unique and
  // inside 0..kMaxInputs-1: a repeated slot or an out-of-range one is a load
  // error with a named reason, because a corrupt file must not silently drop
  // or invent a track (the app's add-track operation is the idempotent side:
  // adding a slot that is already listed keeps its first position). Names are
  // truncated to kMaxTrackNameBytes. This list is ordering/visibility only:
  // channels[] stays the fixed capacity store, so a hidden slot keeps its
  // settings.
  std::vector<TrackEntry> tracks = allSlots();
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

// Physical device output channel routing (UMC1820-class multichannel out).
// Each bus writes to a pair of device channels: 0-based, defaults Main =
// 1&2, OUT3&4 = 3&4, phones = 5&6. The app maps engine buses onto the
// device through these; the UI offers the pickers.
struct DeviceOutState {
  std::array<int, 2> main{0, 1};
  std::array<int, 2> out34{2, 3};
  std::array<int, 2> phones{4, 5};
  friend bool operator==(const DeviceOutState&, const DeviceOutState&) = default;
};

struct Snapshot {
  RouterState router;
  MixerState mixer;
  FxState fx;
  TransportState transport;
  ClockState clock;
  DeviceOutState deviceOut;
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
// Ports 0..kPortCount-1 and all audio::kMaxInputs mixer strips are always
// captured, so the result is canonical and save/load round-trips exactly. The app-owned
// sections (click, midimap, takes, punch, cue, sends, mainComp, isTemplate)
// stay at their defaults; the app fills them before save(). mixer.tracks is
// likewise app-owned while the engine has no dynamic track list: capture()
// reports allSlots() (the legacy layout), and the app overwrites it with its
// live list. The per-track FX rack is engine state (audio wave 11): capture()
// reads both slots back and apply() forwards them; a built-in id comes back
// with the values the DSP holds, a CLAP id and its parameter vector verbatim
// (the plugin owns its table). The deprecated `insert` is captured as the
// legacy FX1 view and still applies to FX1 for an in-memory snapshot that
// predates the rack.
Snapshot capture(const midi::Router& router, const audio::Engine& engine,
                 std::span<FxSlotTarget*> fxSlots = {});

// Applies a snapshot to the live objects. The router and mixer are replaced
// wholesale (defaults for anything the snapshot omits); FX slots are updated
// only when the effect id can be loaded. Invalid port/route ids are ignored.
// The pseudo-source route kinds (kSrcGenerator, kSrcOutputInsert) are
// forwarded to the Router's generator/output-insert chains when their
// destination is Pi-owned; a TRS destination belongs to the Pico and is left
// for the link. A real port pair's pipe list stays app-owned, as before.
// The app-owned sections (click, midimap, takes, punch, cue, sends,
// mainComp, isTemplate, out34mode and the per-channel cue/out34 sends) are
// not applied here; the app forwards them once the engine exposes them. The
// same holds for mixer.tracks (dynamic track list) and the per-track FX rack
// (mixer.channels[].fxSlots).
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
// else is rejected, as is any malformed JSON, wrong-typed known field or
// out-of-range value of a known field (track slot, repeated track slot, route
// endpoint). Missing fields take defaults - so a file without "tracks" loads as
// the full slot list - and unknown fields are ignored. Never throws.
LoadResult load(const std::string& path) noexcept;

}  // namespace mnky::session

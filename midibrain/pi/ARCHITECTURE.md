# mnkybox Pi DAW — architecture

Target: Raspberry Pi 5 (Linux, PipeWire or ALSA) with an 800×480 touch UI. The
dev machine is macOS and everything also builds and runs headless there
(emulator backends, offscreen Qt). C++20 throughout. No external test
framework: assert-based `*_test.cpp` mains, built with plain `c++` or registered
with CTest.

## Tree

```
pi/
  ARCHITECTURE.md      # this file: module contracts and standing rules
  CMakeLists.txt       # root build: pins, core libs, CTest, the Qt app
  third_party/         # vendored RtMidi 6.1.0 + the pinned-dependency table
  link/                # Pico SPI link core (JUCE-free)
  midi/                # router, USB MIDI, clock discipline, buttons (JUCE-free)
  audio/               # engine, per-track FX rack, recorder+WAV, device backends
  clap/                # CLAP host core + example gain plugin (JUCE-free)
  session/             # versioned JSON project format (JUCE-free)
  update/              # Pico 2 UF2/BOOTSEL updater core (JUCE-free)
  app/                 # Qt 6 QML touchscreen application (mnkybox)
  emulator/            # scripted backends + headless selftest
  bench/               # studio measurement harness + --bench-clock driver
```

## Standing rules

- Core (`link midi audio clap session update`) is **JUCE-free,
  Linux-header-free, macOS-safe**: no `<juce_*>`, no `<linux/...>`, no
  ALSA/gpio includes outside `#ifdef __linux__` with a portable fallback
  (throw or mock). Qt lives in `app/` only.
- Core modules are held to zero warnings with `-Wall -Wextra -Werror`.
  Third-party code (RtMidi, Qt, CLAP) is never compiled with those flags.
- The 32-byte wire format is single-sourced: `#include "../../firmware/pico/protocol.hpp"`
  and `"../../firmware/pico/midi.hpp"` (relative to `pi/<mod>/`). Never
  redefine magic/version/CRC/registry values.
- Pins: Pico GPIO numbers from `MB_PIN_*` (`firmware/pico/pins.h`, generated);
  Pi BCM numbers from `tscircuit/lib/pin-map.ts` (`READY_IRQ` = BCM 24).
  Protocol semantics: `docs/protocol.md` (SPI contract, READY, session/seq
  dedup, idle-fill repeats, registry, Pico-clock-master + USB mirror design).
- QML calls into a view model only through `Q_INVOKABLE` methods and
  `Q_PROPERTY` read/write accessors; a method QML calls must be one of those
  two, because the meta-object is the whole interface.
- Tests live next to code (`test_<mod>.cpp`) and build with
  `clang++ -std=c++20 -Wall -Wextra -Werror -I. -I../third_party[/rtmidi] <test>.cpp <sources...> -o /tmp/t_<mod> && /tmp/t_<mod>`
  printing PASS lines and exiting 0. RtMidi compiles into the midi test/user
  with `-D__MACOSX_CORE__` (macOS) / `-D__LINUX_ALSA__` (Pi) plus the
  frameworks it needs.
- Toolchain: on macOS use Apple `clang++` (a bare `c++` may be nix GCC, which
  cannot see the macOS frameworks and fails the midi build while looking
  innocent). On Pi/Linux use the system `c++`. The root CMake enforces this.
- Never float a dependency on a branch: versions are pinned in
  `pi/CMakeLists.txt` and match `third_party/README.md`.

## Module contracts (exact names — implementations must match)

### link/ — namespace `mnky::link`
- `class ISpiTransport { virtual ~; virtual bool transfer(const uint8_t out[32], uint8_t in[32]) = 0; }`
- `class EmulatorTransport : ISpiTransport` — scripted Pico: auto HELLO/ACK,
  tick frames at settable tempo, `injectButton(id,state)`, `injectMidi(port,bytes)`,
  captures `MIDI_SEND`s. Works everywhere, no hardware.
- `#ifdef __linux__ class LinuxSpidevTransport` (`/dev/spidev0.0`, 1 MHz, mode 0,
  ≥10 us CS setup, full-32B-or-fail) + `class ReadyGpio` (`/dev/gpiochip0`,
  BCM 24, polled). Elsewhere: stub constructors throwing `std::runtime_error`.
- `class PicoLink` (owns handshake/session/seq/dedup/queues, drives any transport):
  `explicit PicoLink(ISpiTransport&)`, `void poll()` (paced ≥1 ms transaction),
  `bool linkUp() const`, `struct Event{enum Kind{MidiIn,Control,Tempo,Tick,State,Linked,LinkLost} kind; uint8_t a[10]; uint8_t n; uint32_t u32a,u32b;}` + `bool nextEvent(Event&)` drain,
  `sendMidi(port,data,n)`, `setTempo(bpmx100)`, `transport(cmd)`, `clockEnable(b)` (accepted/ignored by firmware — still send),
  `ledSet(mask,states)`, `setManual(b)`, `onTick(TickFn)` with
  `using TickFn = std::function<void(uint32_t tick,uint32_t picoStampUs,uint64_t piNowUs)>`,
  `session()/piSession()`, `struct Stats{uint64_t tx,rx,bad,dup,overruns;} stats()`.
- Tests: handshake against EmulatorTransport, dedup (repeat ignored), idle-fill,
  MIDI_SEND capture, tick callback ordering, session change = reconnect.

### midi/ — namespace `mnky::midi`
- `enum class Port : uint8_t { TrsIn1, TrsIn2, UsbIn, TrsOut1, TrsOut2, UsbOut }`
  (`UsbIn` = merged all USB devices, `UsbOut` = fan-out to all opens).
- `struct MidiEvent { Port source; uint8_t device; uint8_t size; uint8_t data[3]; uint64_t stampUs; }`
- `class Router`: `setRoute(src,dst,bool)`, `setChannelMask(src,uint16_t)`,
  `setTranspose(src,int)`, `route(const MidiEvent&, deliverFn)` where
  `deliverFn(Port dst, const MidiEvent&)`; channel filter applies to channel
  voice (pass if bit set, `0xFFFF` = all), transpose on note on/off only,
  realtime/system-common always pass untransposed. Tested incl. merge fan-out.
- `class UsbClock` (pure logic, fake-clock tested): `addTick(tick,picoStamp,piNow)`,
  `setTempo(bpmx100)`, `setRolling(bool)`, `nextDueUs(piNow)` → optional phase
  time of next USB clock byte, `popDue(piNow)` advances. EMA offset, never
  mistaking arrival for stamp.
- `class UsbMidi` (RtMidi wrapper): `listInputs()/listOutputs()`,
  `openAll/outputAll` incl. virtual `"mnkybox"` ports for emulator mode,
  input callback → `MidiEvent` with device index.
- `enum class Action {None,Play,PlayStart,Stop,RecToggle,OverdubToggle,TempoCoarse,TempoFine,NavUp,NavDown,NavLeft,NavRight,NavPress,EncoderSw}`,
  `Action mapControl(uint8_t id,uint8_t value,bool shift)` — pure, tested
  (ids from `mb::CTL_*` in `firmware/pico/midi.hpp`).
- MIDI byte constants: reuse `mb::` parsers; there is no second registry.

### audio/ — namespace `mnky::audio`
- Capacity is single-sourced in `capacity.hpp`: `kMaxDeviceInputs = 18`
  (UMC1820 capture width), `kMaxTracks = 64` mixer tracks, `kMaxChannels = 128`
  internal mix channels, `kBlockSize = 128`.
- `struct Channel{float gain,pan; bool mute,solo,arm; float send[2];}` plus the
  per-track input/output assignment, EQ bands and `tapPoint`.
- `struct IEffect{virtual ~; virtual void process(float*const* ch,int nch,int n)=0; virtual const char* name() const=0;}`
- `class Engine`: `setChannel(int,Channel)`, `setMaster(float)`,
  `setTrackFxSlot()` / `setFx()` (external CLAP effect), `setRecSource()`,
  `setTakeVoice()`/`setTakeLoop()`/`setSongLanes()` for playback,
  `process(const float*const* in,int nch,int n,float* outL,float* outR)`,
  `const float* tap(int ch)`. Solo logic: any-solo ⇒ only solos audible.
  Constant-power pan. Both rack slots run per track, post-EQ/pre-fader, in
  series. `process()` never allocates.
- `class Recorder`: `startTake(dir,armMask,sampleRate,sampleRate,masterArm)`,
  `writeBlock(taps,masterL/R,n)` (non-blocking, drop counter), `Take stopTake()`
  with `{path,files,frames,drops}`. Own minimal float32 WAV writer
  (`take-<utc>/chNN.wav` + `master.wav`). Background thread + mutex queue.
- Device I/O is native: `device_backend.hpp` owns exactly one backend at a time
  — libpipewire (product), ALSA `snd_pcm` (fallback), emulator (host tests).
  Qt Multimedia is not part of audio I/O.
- Requirement anchor: 18 ch capture, 48 kHz, float32 masters.

### clap/ — namespace `mnky::clap`
- Headers: `-I../third_party/clap/include` (or the FetchContent include dir),
  `#include <clap/clap.h>`.
- `class Host : public audio::IEffect`: `bool load(path)` (dlopen entry/init),
  `params()` → `vector<Param{id,name,min,max,value}>`, `setParam(id,v)`,
  `saveState/loadState(path)` (state ext), `process()` (audio-ports ext, stereo
  float32, static params latched per block), `unload()`. Minimal host callbacks
  (thread-check/main-thread/timer/log). Graceful fail when exts are missing.
- Test seam: construct from factory function pointers so a `MockPlugin`
  (in-process fake clap_plugin) exercises load/init/activate/process/params/
  state without a `.so`. `MockPlugin` lives in test code.
- `effects/mnkygain/`: example gain+softclip+lowpass bundle (gain/drive/tone/mix,
  params+state exts). Builds as a loadable module; bundle layout in its README.
  It is the default FX for sends and the emulator.

### session/ — namespace `mnky::session`
- One versioned JSON object per project (`project.mnkybox`), hand-rolled writer
  and parser, no JSON dependency. `load()` returns `LoadResult{ok,error,snapshot}`
  and never throws; wrong version, malformed JSON, wrong-typed known fields and
  files over 4 MiB are rejected with a reason. Unknown fields are ignored,
  missing fields take documented defaults.
- `capture()` reads Router + Engine into a canonical `Snapshot`; `apply()`
  replaces router/mixer state wholesale and leaves an unavailable effect alone.
- `mode` and `recSource` are canonical; `monitor` and `send[]` are deprecated
  on-disk spellings that still round-trip. Absence of a section is exactly the
  legacy state, so old files load unchanged.

### update/ — namespace `mnky::update`
- Strict UF2 validation against the RP2350 bootrom contract, BOOTSEL drive
  identification, copy-with-progress, disappearance and link-return
  verification, plus the `pico-update` CLI.

## Audio stack: the app and the system

The product audio stack is **PipeWire**: `pipewire` + `wireplumber` (session
manager) + ALSA `snd_pcm` compatibility. The app opens the device through
`audio/device_backend.hpp` — libpipewire when a PipeWire sink exists, ALSA
otherwise, emulator on the host — and enumerates devices at every start; it
never assumes a node name. It honours the server quantum rather than demanding
a period size, and has no dependency on `jackd` or any daemon binary name.
Inter-app routing belongs to the PipeWire graph (`qpwgraph`, `pw-link`), not to
the app UI.

## Acceptance

`make test` = prototype verification (typecheck, build, placement, protocol,
SPICE). The DAW program's gates: full CTest green on the macOS host and the
studio Pi, `--unittests` (headless logic), `--ui-taps` (every control tapped
headless; any QML warning = fail), `--panel-taps` (panel intent parity),
`--screenshots` sets, and a studio Pi run for anything touching audio/MIDI
timing (see `pi/STUDIO-PI.md` and `pi/bench/`). Physical hardware acceptance is
`docs/acceptance.md`'s domain.

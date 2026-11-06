# mnkybox Pi DAW — architecture for parallel teams

Target: Raspberry Pi 5 (Linux/ALSA) with a 800×480 touch UI. Dev machine is
macOS; **everything must also compile and run headless here** (emulator
backends + selftest). C++20 throughout. No external test framework:
assert-based `*_test.cpp` mains, built with plain `c++` (see per-module
commands below). Children never commit; the parent commits per landed team.

## Tree (one writer per directory — do not touch another team's dir)

```
pi/
  ARCHITECTURE.md      # this file (parent-owned)
  CMakeLists.txt       # setup team (root build: core libs, CTest, JUCE app)
  third_party/README.md
  third_party/rtmidi/  # vendored RtMidi 6.1.0 (parent-owned, do not modify)
  link/                # Pico SPI link core (JUCE-free)
  midi/                # router, USB MIDI, clock discipline, buttons (JUCE-free)
  audio/               # engine, recorder+WAV, sends (JUCE-free)
  clap/                # CLAP host core + example gain plugin (JUCE-free)
  app/                 # wave 2: JUCE touchscreen application (GUI team)
  emulator/            # wave 2: emulator backends + headless selftest binary
```

## Non-negotiable rules

- Core (`link midi audio clap`) is **JUCE-free, Linux-header-free, macOS-safe**:
  no `<juce_*>`, no `<linux/...>`, no ALSA/gpio includes. Pi-only code goes
  behind `#ifdef __linux__` with a portable fallback (throw or mock).
- The 32-byte wire format is single-sourced: `#include "../../firmware/pico/protocol.hpp"`
  and `"../../firmware/pico/midi.hpp"` (relative to `pi/<mod>/`). Never
  redefine magic/version/CRC/registry values.
- Pins: Pico GPIO numbers from `MB_PIN_*` (`firmware/pico/pins.h`, generated);
  Pi BCM numbers from `tscircuit/lib/pin-map.ts` (`READY_IRQ` = BCM 24).
  Protocol semantics: `docs/protocol.md` (SPI contract, READY, session/seq
  dedup, idle-fill repeats, registry, Pico-clock-master + USB mirror design).
- Tests live next to code (`test_<mod>.cpp`), build with:
  `clang++ -std=c++20 -Wall -Wextra -Werror -I. -I../third_party[/rtmidi] <test>.cpp <sources...> -o /tmp/t_<mod> && /tmp/t_<mod>`
  must print PASS lines and exit 0. RtMidi compiles into the midi test/user
  with `-D__MACOSX_CORE__` (mac) / `-D__LINUX_ALSA__` (Pi) + frameworks as needed.
- Toolchain: on macOS ALWAYS Apple `clang++` (bare `c++` here is nix GCC,
  which cannot see macOS frameworks — the midi build fails under it while
  looking innocent). On Pi/Linux use system `c++`. Root CMake enforces this.

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
  input callback → `MidiEvent` with device index. Guard API defines per README.
- `enum class Action {None,Play,PlayStart,Stop,RecToggle,OverdubToggle,TempoCoarse,TempoFine,NavUp,NavDown,NavLeft,NavRight,NavPress,EncoderSw}`,
  `Action mapControl(uint8_t id,uint8_t value,bool shift)` — pure, tested
  (ids from `mb::CTL_*` in `firmware/pico/midi.hpp`).
- MIDI byte constants: reuse `mb::` parsers where sensible; no second registry.

### audio/ — namespace `mnky::audio`
- `kMaxInputs=18, kNumSends=2, kBlockSize=128`. `struct Channel{float gain=1,pan=0.5; bool mute,solo,arm; float send[2]={0,0};}`.
- `struct IEffect{virtual ~; virtual void process(float*const* ch,int nch,int n)=0; virtual const char* name() const=0;}` (stereo in practice; ch arrays sized by caller).
- `class Engine`: `setChannel(int,Channel)`, `setMaster(float)`, `setFx(int,IEffect*)`,
  `setFxReturn(int,float)`, `process(const float*const* in,int nch,int n,float* outL,float* outR)`,
  `const float* tap(int ch)` (post-gain mono, block-valid). Solo logic: any-solo ⇒ only solos audible. Tested incl. pan law (constant-power), solo/mute matrix, sends.
- `class Recorder`: `startTake(dir,armMask,sampleRate)`, `writeBlock(taps[18],masterL/R,n)` (non-blocking, drop counter),
  `Take stopTake()` with `{path,files,frames,drops}`, `drops()`. Own minimal
  float32 WAV writer (RIFF/WAVE/FMT/extensionless `take-<utc>/chNN.wav` +
  `master.wav`). Background thread + mutex queue (prototype-grade; drops counted).
- Requirement anchor: 18 ch, 48 kHz, 24-bit-class capture (float32 masters it).

### clap/ — namespace `mnky::clap`
- Headers: `-I../third_party/clap/include`, `#include <clap/clap.h>`.
- `class Host : public audio::IEffect`: `bool load(path)` (dlopen entry/init),
  `params()` → `vector<Param{id,name,min,max,value}>`, `setParam(id,v)`,
  `saveState/loadState(path)` (state ext), `process()` (audio-ports ext, stereo
  float32, static params latched per block), `unload()`. Minimal host callbacks
  (thread-check/main-thread/timer/log). Graceful fail when exts missing.
- Test seam: construct from factory function pointers so a `MockPlugin`
  (in-process fake clap_plugin) exercises load/init/activate/process/params/
  state without any `.so`. `MockPlugin` lives in test code.
- `effects/mnkygain/`: example gain+softclip+lowpass bundle (gain/drive/tone/mix,
  params+state exts). Builds as a loadable module here; bundle layout + Pi
  notes in its README. It is the default FX for sends and the emulator.

## Current state (2026-11-06)

Everything above is built: `app/` is the Qt 6 QML touchscreen application
(9 tabs, SHIFT faces, popups, dynamic mixer, per-track FX rack, EQ/X-Y,
edit popup, panel/touch parity) and `emulator/` is the scripted backends +
headless selftest. The tree also carries `update/` (Pico UF2/BOOTSEL
updater core) and `bench/` (studio measurement harness). The one-writer
rule and the module contracts above remain law for new work.

## Acceptance

`make test` = prototype verification (typecheck, build, placement,
protocol, SPICE). The DAW program's gates: full CTest green on both hosts,
`--ui-taps` clean (every control tapped headless; any QML warning = fail),
offscreen screenshot sets per wave, and the studio Pi run for anything
touching audio/MIDI timing (see pi/STUDIO-PI.md and pi/bench/). Physical
hardware acceptance is docs/acceptance.md's domain.

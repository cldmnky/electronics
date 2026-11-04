# mnkybox app (`pi/app/`) — JUCE touchscreen DAW

Wave-2 GUI team deliverable. `juce_add_gui_app(mnkybox)` target, added by the
root build through its documented wave-2 hook (`foreach(wave2_dir app emulator)`
in `pi/CMakeLists.txt`); no other file outside this directory was changed.
Single-instance guard is a `juce::InterProcessLock` in the temp directory.

## How to build and run (macOS arm64 dev machine)

```sh
cmake -S midibrain/pi -B /tmp/pi-build          # fetches pinned JUCE 9.0.5 + CLAP 1.2.10
cmake --build /tmp/pi-build --target mnkybox -j
/tmp/pi-build/app/mnkybox_artefacts/RelWithDebInfo/mnkybox.app/Contents/MacOS/mnkybox --emulator
```

* `--emulator` forces the emulator backends: scripted Pico SPI transport
  (`link::EmulatorTransport`), the app's own sine/noise generator instead of an
  audio device, and virtual `mnkybox` USB MIDI ports.
* Without the flag the app asks for the hardware paths: `/dev/spidev0.0` + READY
  GPIO (Pi only) and the default audio device. On a dev machine the SPI transport
  constructor throws, the app logs that and continues with the emulator link, so
  a desktop run still works (audio may come from CoreAudio).
* `--unittests` runs the non-visual logic (`logic.hpp` panel/value dispatch and
  the in-tree mnkygain FX slot) and exits with a meaningful code; no window, no
  audio device, no MIDI port is opened. Also registered as CTest
  `mnkybox_app_logic`.
* **No visual QA was done**: this machine has no display, so the 800 x 480 layout,
  touch-target sizes and tab bar are owner-side checks.

## What works

* Five screens behind a 48 px tab bar (800 x 480 landscape-first, resizable up to
  1600 x 960): Mixer, Routing, FX, Recorder, System.
* Mixer: 18 strips (gain, pan, sends A/B, mute/solo/arm) with >= 44 px controls,
  pinned master strip with stereo peak meters, horizontal scrolling.
* Routing: node-wire patchbay (TRS IN 1/2 + USB IN left, TRS OUT 1/2 + USB OUT
  right) over the 3x3 route matrix. Tap a wire or an input then an output to
  toggle; the selected input keeps its routes bright and dims the rest; RESET
  restores the documented default matrix (both TRS inputs -> USB OUT). The USB
  nodes show the live open device/port counts. Below it: the 16-channel mask
  per source and per-source transpose, all writing straight into `midi::Router`
  through a guarded glue.
* FX: two CLAP send slots. Default plugin is the in-tree `mnkygain` build
  (`MNKY_DEFAULT_PLUGIN` compile definition), parameter sliders come from
  `clap::Host::params()`, state save/load through the plugin's state extension,
  return level per slot. Plugin loading runs on the message thread; the audio
  thread only enters `Host::process()` behind a try-lock and passes dry when a
  load is in flight.
* Recorder: transport readout from the Pico link (`rolling`, tick ->
  `bar:beat:tick`, tempo), 18 arm toggles, take list scanned from the take
  directory (WAV duration via `juce_audio_formats`), drop counter, free-disk readout.
* System: link stats (tx/rx/bad/dup/overruns, sessions), READY GPIO, clock
  discipline (offset/latency/skipped/reordered, primed/rolling), transport,
  audio+MIDI backend summary, FX paths and the in-app log tail.
* Panel wiring: `EV_CONTROL` -> `midi::mapControl` -> `logic.hpp` intent ->
  `MainComponent` performs it (screen/strip navigation, arm, play/start/stop,
  record, overdub, tempo coarse/fine with `CTL_SHIFT_EXIT` as modifier).
  Panel LEDs follow recording/overdub/rolling through `PicoLink::ledSet`.
* Clock plumbing: `PicoLink::onTick` feeds `midi::UsbClock`; clock bytes are
  mirrored to `UsbOut` while rolling; MIDI input stamps are mapped through the
  clock when it is warm.

## Deliberately not done / Pi follow-ups

* SysEx is not forwarded (core limitation), no MIDI 2.0/UMP.
* Arm changes do not extend a take that is already running (`Recorder` fixes the
  arm mask at `startTake`).
* Overdub is a flagged state and LED only; it does not mix into an existing take.
* Take list shows WAV duration from `master.wav`; a take killed mid-write shows
  as 0 s.
* No persistence of mixer/routing settings between runs (no config file yet).
* The "real device" path uses 18 inputs / 2 outputs; the UMC1820 + ADA8200
  channel map, ALSA period settings and latency have never been measured on a Pi.
* READY_IRQ is only polled on Linux (`/dev/gpiochip0`, BCM 24); elsewhere the
  System screen says "not sampled".
* JUCE licence (AGPLv3 vs commercial) is still undecided: do not distribute a
  built bundle.

## Implementation notes

* Core libs (`link midi audio clap`) are used exactly as landed; the app owns no
  protocol, pin or registry constants. The emulator synth is app-local on
  purpose (no dependency on `pi/emulator/`).
* Threading: UI/message thread owns plugin loading, routing config and recorder
  control. The link thread owns PicoLink, UsbClock, TRS sends and the delivery
  queue flush. Audio thread reads atomics from `MixerModel` once per block and
  never locks, allocates or logs.

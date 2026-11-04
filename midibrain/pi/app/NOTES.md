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
  audio+MIDI backend summary, FX paths, the audio backend selector (below) and
  the in-app log tail.
* Audio backend selector: default / ALSA / JACK, the backend's output device and
  the requested period size, one `APPLY` that restarts audio and a `REFRESH` that
  re-probes the device types. The choice is persisted in the app's own
  `PropertiesFile` (`~/.config/mnkybox/mnkybox.settings` on Linux,
  `~/Library/Application Support/mnkybox/mnkybox.settings` on macOS), never in a
  session file. `Default` is the untouched platform default, i.e. ALSA on the Pi.
* Panel wiring: `EV_CONTROL` -> `midi::mapControl` -> `logic.hpp` intent ->
  `MainComponent` performs it (screen/strip navigation, arm, play/start/stop,
  record, overdub, tempo coarse/fine with `CTL_SHIFT_EXIT` as modifier).
  Panel LEDs follow recording/overdub/rolling through `PicoLink::ledSet`.
* Clock plumbing: `PicoLink::onTick` feeds `midi::UsbClock`; clock bytes are
  mirrored to `UsbOut` while rolling; MIDI input stamps are mapped through the
  clock when it is warm.

## JACK audio backend (System screen)

Raspberry Pi OS runs PipeWire by default and jackd during bring-up; both speak
JACK, so one JACK device type makes the app a routable citizen instead of an ALSA
island. There is still exactly one `juce::AudioDeviceManager` (and one audio
callback): switching backend restarts that manager with another device type.

* Build (`pi/CMakeLists.txt`, `pi/app/CMakeLists.txt`): on Linux the root build
  requires the JACK headers through `pkg-config jack` and publishes an interface
  target `mnky_jack` (`JUCE_JACK=1`, `MNKY_HAVE_JACK=1`, include/link flags); the
  app links it when present. macOS skips detection silently, so `JUCE_JACK` stays
  0 and the selector shows `JACK (unavailable)`. JUCE dlopen()s libjack at
  runtime; the client name is `mnkybox`, which is what `jack_lsp` shows.
* Selection model (`audio_backend.hpp`, JUCE-free): backend keys for the
  PropertiesFile, menu labels, availability, `resolveAudioBackend()` (a request
  for an unavailable backend falls back to `Default` with a note) and the
  buffer-size list (64/128/256/512/1024, 128 = `audio::kBlockSize`). Unit-tested
  in `logic_tests.cpp` (`mnkybox audio backend selection`).
* Apply path (`AudioGlue::applyAudioChoice`): persist the request, resolve it,
  then either restart the device manager on the requested type/device/period or
  fall back to the emulator generator with the reason in the note. In
  `--emulator` mode `Default` still means the generator (the documented emulator
  backend); an explicit ALSA/JACK request opens a real device there too, which is
  how the guest proof below was taken. A device that dies (jackd exit) is shown
  as `(stopped)` / `no audio device`, and REFRESH turns the backend entry into
  `JACK (unavailable)`; APPLY then falls back to `Default` without a restart.
* System readout: `AUDIO` summary plus a live line with the device type in use,
  the device name, the sample rate/block size the device reports and the xrun
  counter (`AudioDeviceManager::getXRunCount`, so JUCE's own load measurement is
  included). JACK ignores the requested period/rate: the server owns them, and
  the live line shows what is actually running.

Guest proof (`pi/qemu/STAGE-C2.md` loop, Debian arm64, Xvfb 800x480, `--emulator`,
`jackd -d dummy -r 48000 -p 128`): `screenshots/23-jack-system.jpg` (JACK + device
`system` + 128 frames, live `JACK / system 48000 Hz / 128 frames`),
`23-jack-unavailable.jpg` (jackd stopped: `JACK (unavailable)`, `no audio device`,
fallback note) and `24-jack-proof.jpg` (a take recorded while JACK was live).
Log lines: `audio: apply JACK / system / 128 frames -> JACK`, `audio: callback
started, 48000.0 Hz / 128 frames`, `audio: JACK / system, 48000 Hz / 128 frames,
2 in`; `jack_lsp` lists `mnkybox:in_1/2` and `mnkybox:out_1/2`, auto-connected to
`system:capture_*` / `system:playback_*`. The take in `24-jack-proof.jpg` is
384256 frames at 48 kHz (8.005 s) with 0 dropped blocks. Xruns grow in this guest
because jackd's dummy driver reports `JackTimedDriver::Process XRun` under QEMU
(no real-time scheduling): the counter is live, the recorder drops stay 0.

## Deliberately not done / Pi follow-ups

* SysEx is not forwarded (core limitation), no MIDI 2.0/UMP.
* Arm changes do not extend a take that is already running (`Recorder` fixes the
  arm mask at `startTake`).
* Overdub is a flagged state and LED only; it does not mix into an existing take.
* Take list shows WAV duration from `master.wav`; a take killed mid-write shows
  as 0 s.
* No persistence of mixer/routing settings between runs (no config file yet);
  the audio backend is the exception (see the JACK section).
* The audio backend choice is verified against jackd in the VM only. PipeWire's
  JACK layer on the real Pi, jackd's real-time setup, the UMC1820 period/rate and
  actual round-trip latency are unmeasured.
* JACK period/rate are server-owned: the buffer picker asks, the live line shows
  what the server runs. There is no resampling, so a 44.1 kHz JACK server runs the
  engine at 44.1 kHz.
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
* Threading: UI/message thread owns plugin loading, routing config, the audio
  backend choice and recorder control. The link thread owns PicoLink, UsbClock,
  TRS sends and the delivery queue flush. Audio thread reads atomics from
  `MixerModel` and the emulated/generated flag once per block and never locks,
  allocates or logs.

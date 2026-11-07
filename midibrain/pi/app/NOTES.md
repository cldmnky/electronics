# mnkybox app (`pi/app/`) — Qt 6 QML touchscreen DAW

The wave-2 UI is now Qt 6 QML: `QGuiApplication` + `QQmlApplicationEngine`, a
`QQuickWindow` shell and ten QML screens over C++ view models. The JUCE app
(`juce_add_gui_app`, `MainComponent`, the LookAndFeel/theme and the JUCE
`AudioDeviceManager`) is gone. The core libs (`link midi audio clap session`,
`firmware/`) are untouched and stay JUCE-free; this change is UI layer + audio
device path only.

## Build and run

macOS (Homebrew Qt; the app target is added by the root build's wave-2 hook):

```sh
cmake -S midibrain/pi -B /tmp/pi-build -DCMAKE_PREFIX_PATH=$(brew --prefix qt)
cmake --build /tmp/pi-build --target mnkybox -j8
/tmp/pi-build/app/mnkybox --emulator
```

Linux/Pi build (Debian bookworm guest or Pi):

```sh
cmake -S midibrain/pi -B ~/pi-build
cmake --build ~/pi-build -j8
~/pi-build/app/mnkybox --emulator
```

* `--emulator` forces the emulator backends: scripted Pico SPI transport
  (`link::EmulatorTransport`), the app's own sine/noise generator instead of an
  audio device, and virtual `mnkybox` USB MIDI ports.
* Without the flag the app requests `/dev/spidev0.0` + READY GPIO (Pi only) and
  the platform's default **Qt Multimedia** audio output. The SPI transport
  constructor throws on macOS; the link logs that and continues with the
  emulator transport, so a desktop run still starts.
* `--unittests` runs the C++ checks (`logic_tests.cpp`) and exits; no window,
  audio device, MIDI port or QML engine. Also registered as CTest
  `mnkybox_app_logic` (374 checks, 0 failures).
* `--no-pico` runs on a bare Pi 5 with no Pico attached. `LinkClient` is built
  without an SPI transport, `PicoLink` or READY GPIO, so there is no handshake,
  no reconnect loop and no "link lost" log; the status line says `NO PICO`. The
  Pi owns the clock and transport, USB audio/MIDI stay live and the Pico-owned
  TRS MIDI/clock, panel controls and LEDs are reported unavailable. There is no
  auto-detect/fallback: without the flag the app takes exactly the existing
  Pico/hardware path. Independent of `--emulator` (use both for a deterministic
  desktop run); see "No-Pico mode" below for the exact behaviour and proof.
* `--screenshots DIR` is the headless proof: forces the emulator backends, loads
  every QML screen and writes `30-qt-*.png` with `QQuickWindow::grabWindow()`,
  then exits 0. Run it with `QT_QPA_PLATFORM=offscreen`; no Xvfb is needed.
* `--screenshots-live DIR` is the same grab run against the real Qt Multimedia
  audio backend. It was used for the PipeWire/Pulse evidence run below and does
  not imply the emulator backends.

## No-Pico mode (`--no-pico`, bare Pi 5)

`--no-pico` is the bare-Pi mode and the only switch for it. `--bench` was the
working name in the first draft and is not implemented. The flag changes only
the link/clock/transport ownership, not the audio or MIDI backend selection
(`--emulator` still chooses the generated audio and virtual ports, which is how
the offscreen proof is deterministic). Without `--no-pico` the app takes exactly
today's full Pico path: no auto-detection and no fallback heuristic.

What is constructed and what is not:

* `LinkClient(midi, state, emulated, noPico=true)` creates **no**
  `LinuxSpidevTransport`/`EmulatorTransport`, **no** `PicoLink` and **no**
  `ReadyGpio`. There is no HELLO handshake, no reconnect state machine and no
  `link: lost` log. `status()` exposes `noPico=true`, `linkUp=false` (nothing is
  connected; the UI reads `noPico` first and prints `NO PICO`).
* The one no-Pico pump thread is a 1 ms clock/MIDI pump, not an SPI poll thread:
  it applies queued tempo/transport/gate commands, calls `MidiGlue::pump()` (the
  same link-thread role as hardware mode) and emits the Pi clock to USB OUT. It
  performs zero SPI transactions (`status().stats.tx/rx == 0`).
* Transport commands have no Pico to echo a state frame, so they **are** the
  state change: START/CONTINUE set rolling and peg the free-run phase, STOP
  clears it, START resets the Pi tick to 0 (firmware `transport_cmd()`
  semantics). No `EV_TRANSPORT` frame exists or is sent. AppState `rolling`,
  `tick` and `tempoX100` are written directly for the UI and the audio engine.
* `midi::UsbClock` gains a documented free-run mode (`setFreeRun()` /
  `startFreeRun()` / `advanceFreeRun()`): no Pico stamps arrive, the first byte
  is one interval after START, each emitted byte advances the phase by
  `intervalUs()`, a `setTempo()` edit applies to the next interval, and
  `addTick()` is ignored. The per-output `clock`/`transport` gates still apply to
  the USB clock bytes; `advanceFreeRun()` keeps the Pi tick moving while the USB
  clock gate mutes its bytes, so tap tempo and ensemble tempo edits are instant
  and the timeline does not freeze behind a closed gate.
* TRS is absent because the TRS jacks/UARTs and the panel hardware live on the
  Pico: TRS clock (including clock-source follow; the app has no separate
  follow control, so the session's `clock.source` is never applied) and the TRS
  clock/transport gates are inert, panel controls are touch-only and the LEDs
  are absent. `Routing` SHIFT disables the TRS OUT gate buttons; `clockText()`
  says so explicitly. On-screen PLAY/STOP/REC are shown only in this mode
  because the hardware transport buttons and their REC chord are Pico inputs.
* MIDI: USB in/out is fully live, including the Pi-owned (USB-touching) pipe
  chains evaluated by `midi::Router`. A routed TRS delivery is refused by the
  no-Pico sink, so the Monitor records no TRS OUT send and it never reaches the
  0x1A path; the first refusal logs
  `no-pico: TRS MIDI output unavailable - 0x15/0x1A route send suppressed (no
  Pico)`. TRS pipe-chain edits stay project metadata (a 0x18/0x19 command is
  not sent) and the pipe UI reports `INERT (NO PICO)`.
* UI: the System screen shows a NO PICO banner (active vs physically absent
  subsystems) and a `NO PICO` status block; the transport strip carries a
  persistent `NO PICO` badge; the Monitor screen shows all four TRS rows as
  `UNAVAILABLE - NO PICO (Pico link disabled)` instead of zero counters; the
  Routing patchbay labels the TRS nodes `UNAVAILABLE (NO PICO)`.

Proof: `mnkybox --unittests` covers the no-Pico link (no SPI transactions,
NO PICO status), direct transport, free-run scheduling and the UNAVAILABLE/inert
models; `QT_QPA_PLATFORM=offscreen mnkybox --screenshots DIR --no-pico` writes
the required `35-bench-mixer.png` (persistent badge, free-running position) and
`35-bench-system.png` (banner + status block) in addition to the normal set. The
screenshot filenames keep the "bench" spelling from the original task brief; the
UI text inside them is NO PICO.

## Versions used for verification

* macOS 15 (arm64, AppleClang), Homebrew `qt` **6.12.0**.
* Debian bookworm arm64 guest (QEMU, kernel 6.1.0-54-arm64), Qt **6.4.2**:
  `qt6-base-dev 6.4.2+dfsg-10`, `qt6-declarative-dev 6.4.2+dfsg-1`,
  `qt6-multimedia-dev 6.4.2-5`, `qml6-module-qtquick-controls 6.4.2+dfsg-1`,
  `qt6-wayland 6.4.2-1`, plus the runtime QML modules
  `qml6-module-qtquick-window`, `qml6-module-qtqml-workerscript`,
  `qml6-module-qtqml`, `qml6-module-qtquick-layouts`,
  `qml6-module-qtquick-templates` (6.4.2+dfsg-1) and
  `qt6-declarative-dev-tools`.
* PipeWire verification packages in the same guest: `pipewire 0.3.65-3+deb12u1`,
  `pipewire-bin 0.3.65-3+deb12u1`, `pipewire-pulse 0.3.65-3+deb12u1`,
  `wireplumber 0.4.13-1`, `pulseaudio-utils 16.1+dfsg1-2+b1`,
  `libspa-0.2-modules 0.3.65-3+deb12u1`. Bluetooth is intentionally not
  installed (`libspa-0.2-bluetooth` absent).
* The sources use the Qt 6.4-compatible subset: unversioned `import QtQuick` /
  `import QtQuick.Controls` style, no Qt 6.5+ APIs, no `qt_add_qml_module`
  requirement. Both versions build the same QML resources.

## Structure

* `main.cpp` — flags, QLockFile single-instance guard, QML singleton
  registration, context properties, `--screenshots` runner.
* `app_controller.*` — owns the services, performs queued panel intents on the
  GUI thread, NAVIGATE/EDIT/SHIFT state machine, dirty fingerprint, transport
  text, tap tempo, panic, keypad/confirm overlays.
* Pure/reusable (Qt-free) logic kept from the JUCE app: `logic.*`,
  `text_buffer.hpp`, `project_logic.*`, `mixer_model.*`, `emulator_synth.*`,
  `routing_model.hpp`, `audio_backend.hpp`, `wav_info.*`.
* Qt bridges: `midi_glue.*` (router + `LearnEngine` + panic), `link_client.*`
  (`PicoLink`/`UsbClock` thread), `fx_slot.*` (CLAP host), `project_io.*`
  (session capture/apply), `audio_glue.*` (QAudioSink/QAudioSource + Engine +
  Recorder + ClickSource).
* QML view models: `mixer_view_model.*`, `routing_view_model.*`,
  `fx_view_model.*`, `recorder_view_model.*`, `projects_view_model.*`,
  `system_view_model.*`, `learn_view_model.*`.
* `qml/Style.qml` — the single QML singleton for the dark theme tokens and touch
  metrics. The contrast floors from the old `theme.hpp`/`ui_style.hpp` comments
  are re-encoded here and measured by `logic_tests.cpp`:
  text 14.63:1, dim text 6.90:1, accent 8.96:1 on `#15171b`;
  background luminance 0.0085 and panel 0.0151 (both <= 0.03); touch 44 px.
* Screens: `qml/screens/{Mixer,Routing,Fx,Recorder,System,Projects}Screen.qml`;
  shared `qml/components`, overlays in `qml/overlays`.

## Audio: Qt Multimedia (ALSA default, PulseAudio on the desktop)

`AudioGlue` uses one `QAudioSink` (pull mode: a `QIODevice` renders 128-frame
engine blocks) and an optional `QAudioSource` (push mode: captured frames land in
a bounded ring for engine inputs 1/2). The engine/recorder/click path is the
same JUCE-free core; the audio thread never locks the mixer, only `FxSlot`'s
try-lock adapter.

* On Linux Qt Multimedia uses **PulseAudio** when a server is present
  (Raspberry Pi OS and the product Buildroot image: PipeWire + pipewire-pulse;
  see `../BUILDROOT-AUDIO.md`) and **ALSA** when it is not. The System screen
  shows the Qt device list; `Default output` means whatever Qt selects for the
  platform. ALSA is the local/default fallback when no Pulse server is running;
  the product image's default is pipewire-pulse. There is no JACK-API or
  ALSA-direct app code.
* `--emulator` starts on the generator. Applying `Default output` there keeps
  the generator; applying an **explicit audio device** opens real audio. This is
  the documented way to probe an interface from an emulator-mode run.
* Buffer sizes: 64/128/256/512/1024 frames are offered; 128 is the engine block
  size. The live line shows the device's actual rate/buffer, because Qt may
  choose a larger device buffer than requested.
* Input is **opt-in** (`No input` is the default). Qt exposes a stereo source,
  so captured audio reaches engine inputs 1/2; channels 3-18 stay silent. The
  emulator generator still fills all 18.
* No JACK device type is compiled or probed. **Dropped JACK UI**: the JUCE
  device manager's ALSA/JACK switch cannot be expressed through Qt Multimedia's
  device API, and Raspberry Pi OS already routes Qt's Pulse/PipeWire stream to
  other apps. Use a PipeWire/Pulse patchbay for inter-app routing. The System
  backend hint says this on screen.
* `xruns` is shown as `n/a (Qt)`: Qt 6.4 exposes no xrun counter. No latency,
  round-trip or timing claim is made.
* The audio backend choice (backend, output device id, input device id, buffer
  frames) is persisted in `QSettings` (`~/Library/Preferences/com.mnkybox.mnkybox.plist`
  on macOS, `~/.config/mnkybox/mnkybox.conf` on Linux), never in a session file.

## PipeWire/PulseAudio verification (Debian guest)

The product image's audio stack is PipeWire, reached through Qt Multimedia's
PulseAudio backend (`pipewire-pulse`). The app has no JACK-API or ALSA-direct
code; the ALSA path remains only Qt's local fallback when no Pulse server exists.

Guest setup:

```sh
sudo apt-get install -y pipewire pipewire-pulse wireplumber pulseaudio-utils
export XDG_RUNTIME_DIR=/run/user/1000      # mkdir -p/chmod 700 if absent
setsid nohup pipewire          >/tmp/pipewire.log          2>&1 </dev/null &
setsid nohup pipewire-pulse    >/tmp/pipewire-pulse.log    2>&1 </dev/null &
# WirePlumber needs a session bus when the guest has no X11/DISPLAY.
setsid nohup dbus-run-session -- wireplumber >/tmp/wireplumber.log 2>&1 </dev/null &
sleep 3
pw-cli info 0
pactl info
```

Observed server/devices (exact guest output):

```text
Server Name: PulseAudio (on PipeWire 0.3.65)
Default Sample Specification: float32le 2ch 48000Hz
Default Sink:   alsa_output.platform-snd_aloop.0.analog-stereo
Default Source: alsa_input.platform-snd_aloop.0.analog-stereo
# pactl list short sinks
51  alsa_output.platform-snd_aloop.0.analog-stereo  PipeWire  s32le 2ch 48000Hz  IDLE
# pactl list short sources
51  alsa_output.platform-snd_aloop.0.analog-stereo.monitor  PipeWire  s32le 2ch 48000Hz
52  alsa_input.platform-snd_aloop.0.analog-stereo          PipeWire  s32le 2ch 48000Hz
# pw-cli list-objects (nodes)
id 44  node.name = "alsa_output.platform-snd_aloop.0.analog-stereo"  media.class = "Audio/Sink"
id 45  node.name = "alsa_input.platform-snd_aloop.0.analog-stereo"   media.class = "Audio/Source"
```

App run against that server (`QT_QPA_PLATFORM=offscreen`, no `--emulator`,
`XDG_RUNTIME_DIR=/run/user/1000`):

```text
audio: Qt devices default=yes outputs=1 [Built-in Audio Analog Stereo] inputs=1 [Built-in Audio Analog Stereo]
audio: sink active, 48000 Hz / 128 frames
audio: Qt Multimedia / Built-in Audio Analog Stereo, 48000 Hz / 768 frames
# while running: pactl list short sink-inputs
99  51  98  PipeWire  float32le 2ch 48000Hz
# pactl list sink-inputs
Sink Input #120
  Sample Specification: float32le 2ch 48000Hz
  application.name = "mnkybox"
  media.name = "QtmPulseStream-<pid>-<stream>"
```

The app sets `PULSE_PROP_OVERRIDE=application.name=mnkybox` when the caller has
not chosen a name, so PipeWire graph tools see `mnkybox`, not Qt's default
`QtPulseAudio:<pid>`.

* The sink-input line proves the app is a live PulseAudio client on the
  PipeWire server, and Qt's device names prove it enumerated the PipeWire
  sink/source rather than only ALSA.
* `QAudioSink` requested a 128-frame buffer; PipeWire/pipewire-pulse supplied a
  768-frame device buffer, which the System screen reports. The buffer picker is
  a request/hint, not a JACK-style period demand.
* The engine render count is exposed as `blocks N` in the System audio line; the
  evidence screenshot `qemu/screenshots/30-qt-pipewire-system.png` shows
  `LIVE Qt Multimedia / Built-in Audio Analog Stereo`, 48000 Hz / 768 frames and
  a non-zero block count, proving the audio callback ran.
* The source is enumerated (`inputs=1`) and visible in `pactl`; no input device
  is selected by default, so the source-output list is empty. Opening the input
  is an explicit System-screen selection.
* This guest has no real sound card: the sink/source are QEMU's `snd_aloop`
  loopback surfaced through PipeWire. The run proves Qt Multimedia ↔ PipeWire /
  pipewire-pulse client wiring and callback flow, not DAC quality or latency.
* JACK-API support is **intentionally absent**. PipeWire graph routing
  (`qpwgraph`, `pw-link`) covers inter-app needs; no jackd or pipewire-jack
  dependency is required by the app. ALSA remains only the fallback when no
  Pulse/PipeWire server is present.

Test commands/results:

```sh
QT_QPA_PLATFORM=offscreen ./app/mnkybox --screenshots-live ~/pipewire-shots  # 11/11 PNGs
QT_QPA_PLATFORM=offscreen ./app/mnkybox                                      # sink active, blocks > 0
pactl list short sink-inputs                                                 # one PipeWire stream
```

## Wave 3: TRACK / TAKE / PUNCH / MONITOR / TOOLS (31-*)

* **MIXER vs TRACK** — the Mixer tab has a MIXER/TRACK switch. MIXER is one
  setting across many tracks (banked strips, master); TRACK is one track with
  many settings: VOL, GAIN, PAN, FX1, FX2, CUE, OUT34, REC/MUTE/SOLO and an
  18-button track selector. VOL and GAIN are two app-side dB values summed into
  the single `audio::Channel::gain` because session v2 has one `gain` field and
  the recorder tap is post-gain; the engine's newer `vol` field is not used yet.
* **TAKE screen** (Recorder tab) — one track at a time, its take list with an
  ACTIVE flag (empty active = live input), set-active/clear-live, pointer import
  (absolute path, no copy; the keypad accepts `/` and `.`), rename through the
  shared keypad and delete after confirmation. The engine take player gets the
  selected file; a rejected/missing pointer clears playback.
* **Take naming** — after a core take stops, the app renames each recorded
  `chNN.wav` to `<nn>-<trackname>-<take#>.wav` (for example
  `02-ch02-003.wav`) and makes it that track's active take. Track names default
  to `chNN`; they are not session-persisted. There is no recorder API for the
  template yet, so the rename is app-side after `Recorder::stopTake()`.
* **PUNCH** — PLAY FROM / PUNCH IN / PUNCH OUT enable+measure markers in the
  session v2 `punch` section. The engine exposes `TransportPosition`; automatic
  punch scheduling is not wired in this wave, so the markers persist and the UI
  edits them without changing transport behaviour.
* **MONITOR** — click mode (off/record/on-play), click output
  (main/phones/out34), click gain, phones source, CUE style, OUT3&4 stereo/mono,
  the cue/fx1/fx2/out34/rec pre-post matrix and the main-bus compressor are all
  session v2 fields. The app pushes them into the engine atomics every UI tick;
  the engine applies the send sources, cue/out34 buses, click and compressor.
  The main-bus compressor is never applied to OUT3&4.
* **TOOLS** — take-volume free space, low-space banner (<2 GiB or <5 %), screen
  brightness (best-effort `/sys/class/backlight`; stored in `QSettings` when no
  backlight exists) and app/firmware versions. The app version is the CMake
  project version; the link status carries no firmware version field, so a
  hardware link shows “not reported”.
* **Clip LEDs** — every mixer strip has a post-gain meter and a clip-hold LED
  (`ClipHold`, 30 UI ticks ≈ 2 s). `31-clip.png` uses the deterministic
  `showClipDemo()` helper because the emulator never overloads by chance.
* **Per-track input monitoring** — the TRACK strip has OFF / AUTO / ON and the
  mixer strip carries a compact mark. Session v2 stores it as
  `mixer.channels[].monitor` (0/1/2) and `MixerModel::applyTo()` feeds it to
  `audio::Engine` through `Channel::monitor`. Behaviour: **Auto** = hear the live
  input when the track is armed or no take is playing, and hear the take (no
  doubling) when a take is active; **On** = always hear the live input, layering
  it over a take; **Off** = takes only. Mixer strips show `OFF` and `ON` always
  and `A` only when an active take makes Auto choose the take, so the default
  Auto state does not add noise to every strip.
* **Transient value popup** — one `qml/components/ValuePopup.qml` instance in
  `Main.qml` shows a floating label + formatted value while a control changes,
  then hides 1.5 s after the last change. `TouchSlider` triggers it on every
  touch drag and `AppController::adjustFocused()` triggers it for the panel
  EDIT-mode joystick, so both paths share one readout. Formatting is in
  `logic.hpp`: pan = `Center` / `Lxx` / `Rxx`, gains/returns/comp threshold =
  dB, sends/levels = %, ratio = `n:1`, attack/release = ms, tempo = BPM, FX
  params = two decimals. Pan has a 2 % centre detent in `MixerModel` and
  `TouchSlider`, so the popup confirms `Center`. The popup is disabled, has no
  `MouseArea` and never takes focus; `31-value-popup.png` is a scripted
  `setPan(1, 0.51)` + `showValuePopup` grab taken before the 1.5 s timeout
  (no debug hold flag).

## Wave 4: LOOP / I/O ASSIGN / CLOCK OUT / MIDI MONITOR (32-*)

* **Take loop** (Recorder SHIFT face) — per-track `takes.loop`
  (enabled + start/end beats, 0.25-beat grid). The app fills `AudioGlue`'s loop
  copy and `AudioGlue::applyTakeLoops()` calls `audio::Engine::setTakeLoop()` for
  all 18 tracks. Because the engine maps beats to take frames with the tempo at
  apply time, `AppController::timerTick` re-applies the windows whenever the link
  tempo changes. An end at or below the start loops the whole active take; the
  face says so and shows the resolved window (`WHOLE TAKE` / `a - b b`).
  Playback-only, as the engine contract states: an armed strip ignores the take.
* **I/O assignment** (TRACK SHIFT face) — the two former "assignment pending"
  stubs are now pickers bound to `mixer.channels[].inCh/inPair/outDst`: the input
  stepper walks NONE + IN 01..18 with a MONO/PAIR toggle (a pair keeps the first
  channel even and shows `IN 05/06`), the output stepper walks
  MAIN 1&2 / OUT3&4 / PHONES / NONE. Values persist in session v2 and the dirty
  fingerprint. **Missing engine API**: `audio::Channel` has no `inCh`/`inPair`/
  `outDst` fields, so `MixerModel::applyTo()` cannot forward them; until the
  audio side adds per-track source/destination routing the values are stored and
  displayed only (no audio effect).
* **Clock/transport gates** (Routing SHIFT face) — per-output CLOCK and TRANSP
  toggles for TRS OUT1, TRS OUT2 and USB OUT, stored in the session v2 `clock`
  section. TRS outputs go to the Pico as `EV_CLOCK_ROUTE` via
  `LinkClient::requestClockRoute()` → `link::PicoLink::clockRoute()`; the USB row
  drives the Pi-side mirror gates (`UsbClock::setClockEnabled()/
  setTransportEnabled()` through `LinkClient::requestUsbClockGates()`), which
  suppress the mirrored 0xF8 and START/CONTINUE/STOP bytes. `ClockRoute::
  tempoDiv` and `syncDelayMs` cannot be sent: `EV_CLOCK_ROUTE` carries only the
  two gate bits, so they stay session-only and the face's summary line says so.
* **MIDI MONITOR** (7th tab) — live per-port counters (notes / CC / clock /
  other) for TRS IN1, TRS IN2, USB IN, TRS OUT1, TRS OUT2 and USB OUT plus a
  newest-first message log with ages and a CLK filter. The ring lives in
  `MidiGlue` (128 events, bounded, same lock as the matrix) and records what the
  Pi handles: inputs as they arrive, routed outputs when the link thread flushes
  them, and the Pi-side USB clock mirror. The Pico generates clock/transport for
  the TRS outputs itself and does not echo it back, so TRS OUT rows count routed
  messages only — the screen footer says this rather than implying otherwise.
* The tab row is now seven screens (`kScreenCount = 7`, Monitor last); the tab
  bar shows seven equal cells at 800 px.

## Wave 5: EQ GRID / X/Y PERFORMANCE (33-*)

* **Two new tabs** - the tab row is nine equal cells: Mixer, Routing, FX,
  Recorder, System, Projects, Monitor, **EQ**, **X/Y**. The existing indices are
  unchanged, `kScreenCount` is 9 and the panel tab ids are `tab.0..8`.
  `app.fullscreen` hides the tab row and the transport strip; `setTab` restores
  the bridge when another tab is picked.
* **EQ grid** (`qml/screens/EqScreen.qml`, `eq_model.*`, `eq_view_model.*`) -
  log-frequency x gain grid (20 Hz..20 kHz, +/-24 dB) with four draggable nodes
  (the selected one pink), the summed response curve, a track + MAIN subject
  selector, the selected band's GAIN/Q/FREQUENCY/TYPE corners and the FULLSCREEN
  toggle. Band values live in one `audio::fx::Eq` per subject (18 tracks + Main),
  so the ranges (gain +/-24 dB, Q 0.1..10, 20 Hz..20 kHz), the band type names
  and the curve come from the shipped effect instead of a second copy. The curve
  is that effect's own impulse response sampled at 49 log-spaced frequencies, so
  it cannot drift from the DSP. Types are None/LCut/LShelf/Param/HShelf/HCut; a
  cut band shows `-- (cut)` instead of a dB value and ignores gain writes, and
  switching a band into a cut neutralises its stored gain.
* **EQ has no audio insert yet** - `audio::Engine` exposes only the two FX send
  buses, both owned by the FX screen's CLAP slots. There is no per-track or
  main-bus insert, so every EQ instance is edited, displayed and
  project-persisted but **not processed in the audio path**. A follow-up audio
  piece adds the inserts; this wave makes no EQ audio claim.
* **EQ panel path** - focus ring `eq.subject.main`, `eq.subject.track`,
  `eq.band.select`, `eq.node.0..3`, `eq.band.gain`, `eq.band.q`, `eq.band.freq`,
  `eq.band.type`, `eq.fullscreen`. PRESS on a node selects that band and enters
  EDIT; in EDIT the directions move the node (left/right = frequency, and with
  SHIFT the coarse half-octave step; up/down = gain) while the encoder moves
  frequency (SHIFT = fine). `eq.band.select` is the A-button equivalent: one
  PRESS steps to the next band (wrapping 4 -> 1). PRESS on the TYPE corner opens
  the `eqtype` ConfigPopup, the secondary type picker; the SHIFT face carries the
  band steppers and all six type buttons (`shift.eq.*`). On a cut band SHIFT+up/
  down adjusts Q instead of gain (USER-MANUAL section 9), and every change shows
  one transient popup (`1.00 kHz`, `+3.0 dB`, `Q 1.50`, or
  `Param - 3.00 kHz - +2.5 dB` for a drag).
* **X/Y performance** (`qml/screens/XyScreen.qml`, `performance_model.*`,
  `xy_view_model.*`) - two grids with the frozen USER-MANUAL section 10 mapping:
  delay X = feedback-filter cutoff / Y = width, reverb X = size / Y = level. Both
  go through the effects' own X/Y seam (`Effect::setXY` + `setXYNormalized`), so
  the normalization and the ranges belong to `audio::fx`; the delay cutoff axis is
  therefore **linear in Hz**, as that seam documents (x = 0.5 is ~10 kHz).
* **The two grids are audible** - `PerformanceModel` runs the built-in
  `audio::fx::Delay` (send 1) and `audio::fx::Reverb` (send 2) **ahead of** the
  CLAP slot adapters on the engine's two send buses (`AudioGlue`), so a grid move
  is heard on the FX1/FX2 returns. The chain only `try_lock`s on the audio thread:
  a parameter write makes one block pass through dry, the `FxSlot::Adapter` rule.
  Both effects start fully wet because a send bus carries a copy of the strip; the
  delay/reverb secondary parameters are editable in the `delay`/`reverb`
  ConfigPopups. Nothing here is measured (no latency, no level claim).
* **Multitouch** - each grid owns its `MultiPointTouchArea` and forwards every Qt
  `TouchPoint` id to `XyViewModel`, so one finger on each grid is two independent
  points at once. The header badge and the app log both read
  `2 TOUCH - DELAY x1 + REVERB x1`. The offscreen screenshot runner cannot
  synthesize real `QTouchEvent`s, so `33-xy.png` drives the same view-model entry
  point the `TouchPoint` handlers call (`touchBegin(grid, pointId, x, y)`) with
  two point ids; the `MultiPointTouchArea` delivery itself needs real touch
  hardware and stays unverified.
* **Freeze** - panel-punchable (`xy.freeze`, PRESS toggles; the on-screen button
  is the same call). The shipped `Reverb` header has no freeze parameter, so
  freeze is a macro: longest decay (0.98), no damping, fully wet, largest size,
  and release restores every reverb parameter plus the grid position. While
  frozen the reverb grid and the reverb secondary parameters are locked and the
  point is drawn at the maximum corner; the delay grid stays live.
* **X/Y panel path** - ring `xy.delay`, `xy.reverb`, `xy.freeze`,
  `xy.delay.params`, `xy.reverb.params`. PRESS on a grid enters EDIT, the
  directions move the point (0.02 per step, 0.10 with SHIFT), the encoder moves Y
  and PRESS drops out of EDIT. The PARAMS entries open the `delay` (Beat, Ping,
  Division, Feedback, Mix) and `reverb` (Model, Predelay, Decay, Damping)
  ConfigPopups.
* **Persistence** - session v2 `Snapshot::fx.eq` (per track + Main, four bands
  each) and `Snapshot::fx.xy` (normalized positions) are captured and applied by
  `project_io` and folded into the dirty fingerprint, so project save/load and the
  dirty dot cover every EQ band and every grid position. The freeze flag is a
  performance gesture, not a setting: it lives in the model only and is
  deliberately outside the fingerprint, because nothing could save it.
* **Theme** - `Style.nodePink` (#f06292) is the selected EQ node and the freeze
  state; the accent stays the focus-ring/tab colour.

## Wave 6: EDIT timeline + MIDI FX pipe chains (34-*)

### EDIT timeline (10th tab, `TimelineScreen.qml`)

* Time runs left to right in session beats, tracks top to bottom in six-track
  pages (18 tracks, three pages). P< / P> move page and V< / V> the view window, Z- / Z+
  zoom between 8 and 64 beats; the playhead comes from the link tick count and
  the tempo.
* Waveform thumbnails come only from the `.peaks` sidecar (`audio::readPeaks`).
  A missing/stale sidecar draws the explicit dotted placeholder and one
  background `PeakScanner` fills the cache later; the paint path never scans.
  A lane with no active take says "NO ACTIVE TAKE" instead.
* Region selection: touch drag on a lane sets IN/OUT, or the panel cursor + IN
  and OUT buttons (`timeline.cursor/in/out`). The selection is drawn on the
  selected lane; the loop brace (`takes.loop`) and the punch markers
  (`punch.playFrom/in/out`) are displayed but remain owned by the Recorder and
  Session screens.
* OPS opens the shared ConfigPopup (`timelineops`) with TRIM/SPLIT/GAIN/FADES/
  NORM/DELETE/CLEAR/RENDER, gain and fade drafts, a waveform preview and UNDO.
  Edits are the frozen session v2 `takes.edit` region list: every op rewrites
  that list, re-applies it through `AudioGlue::setTakeRegions` and marks the
  project dirty. Undo is 16 levels per track and survives page/view changes but
  not a project load.
* RENDER is deliberately refused: `audio::TakePlayer` has no
  render-to-new-take call; the popup and the status line name
  `audio::TakePlayer::renderRegionsToTake()` and no take file is ever rewritten.
* Region beats are mapped to take frames by the engine with the transport tempo
  at apply time, so the timeline re-applies every edit after a tempo change
  (`TimelineViewModel::refresh`).

### Routing SHIFT: MIDI FX pipe chains (`PIPES` page)

* One ordered chain per input->output pair (9 pairs). The page is popup
  selectors: FROM and TO are two live readouts that open the port lists, the ADD
  row carries the type selector, ADD, the `SCOPE CH n` chip (the chMask popup)
  and one `CHAIN` button, and the chain preset file actions live in that chain
  menu popup. The chain itself is one line per pipe; tapping a row selects it and
  only the selected row carries the small BYP/up/down/remove buttons.
* The parameter editors are **schema-driven** (schema-driven pipe UI).
  `midifx/gen/gen_params.py` emits `midifx/gen/generated/midifx_ui.hpp` from
  `midifx/pipes.json`: per positional parameter a label, range, default, unit,
  choices and a display format. `pi/app/pipe_ui.cpp` is a generic adapter over
  that table (no per-type rows, unlike the retired `pipe_params.*`): every
  positional parameter is one card in a two-column grid (`PipeParamCard.qml`),
  four cards visible and the rest scrolled. A value with a unit or a plain int is
  an arc `KnobDial` whose drag popup carries the unit ("50%", "250 ms", "+4 st",
  "C4"); a bool or an enum is a label + value chip that opens the `pipeparam`
  ConfigPopup with the schema choices (OFF/ON for a bool). A new pipe type is a `pipes.json`
  entry plus one regenerated header; `midifx/tests/test_codegen.py` proves it
  with a throwaway fixture type that never ships.
* Change discipline: edit `pipes.json` (and `wire-order.txt` when the positional
  order really changes), run `python3 midifx/gen/gen_params.py`, commit the
  regenerated files. `--check` fails on a stale or missing generated file, so
  the screen cannot lag the schema.
* `MidiGlue` owns the stored lists, keeps `Router::setPipes` in sync and is the
  only writer; `project_io` captures them into `RouteState::pipes` and applies
  them to a router snapshot on load (`session::apply()` ignores that field).
* Engine split is shown, not hidden, and stated as the code is: a route touching
  USB is Pi-owned and evaluates all 16 types through the shared midifx core (the
  old `process()` rejection of ids 12..16 is gone). What the Pi app does not
  drive is the 24-PPQN grid pass: `MidiGlue::pump` calls `Router::tick`
  (milliseconds: NoteLength, MidiDelay and the stateless pipes) and nothing
  calls `Router::clockTick`, so a clocked pipe on a Pi-owned route holds its
  notes instead of generating steps. Its row says `NO PI CLOCK TICK` and the
  header says `CLOCKED PIPE: PI HAS NO 24-PPQN TICK`. A TRS->TRS chain is
  mirrored to the Pico with
  `EV_MIDI_ROUTE`/`EV_MIDI_ROUTE_PIPE` (0x18/0x19) on every edit/project load,
  and routed TRS bytes are sent with `EV_MIDI_ROUTE_SEND` (0x1A) so every pipe
  type runs there on the firmware's own `clockTick` grid. The Pi applies the
  channel mask and its source
  transpose before that send, so the TRS->TRS order is openly
  mask -> transpose -> Pico chain (the USB route keeps mask -> pipes ->
  transpose).
* `MidiGlue::pump` calls `Router::tick` on the same millisecond time base as the
  routed events, so NoteLength (type 10) pending offs are flushed even without
  another input message. `Router::clockTick` has no Pi-side caller, which is what
  the clocked-pipe status line reports.
* Every configured pipe parameter is a MIDI-learn target
  (`routing.pipe.<src>.<dst>.<pipe>.<param>`); the target set is the generated
  schema parameter count, the target list refreshes with chain edits and an
  unset slot is a no-op, so a binding survives a chain change until that
  pipe/param exists again. The path stays positional on purpose: the
  LearnEngine's setter table is registered once (720 paths) and a key-suffixed
  path would orphan every learned knob as soon as a pipe type changed.
* Chain presets (chain presets): EXPORT/IMPORT on the pipe row
  writes/reads one JSON file per route chain (`t`, `bypass`, `chm`, `params`
  plus the generated schema version) inside the project folder, with the take
  path entry for the name (`.json` implied). Import validates against the
  schema - a pipe type outside 1..16, a chain longer than 8 pipes, more params
  than the type defines or a newer schema version is refused with the reason in
  the keypad instead of being partially applied (`pi/app/pipe_preset.cpp`).

### Link 0x1A sender

`link::PicoLink::sendMidiRoute(src,dst,data,n)` queues the documented
`EV_MIDI_ROUTE_SEND` frame (`mb::pack_midi_route_send`); it is per-message and
has no resend cache (the 0x18/0x19 chain config already has one). The link test
decodes the wire frame and checks src/dst/size/bytes plus the 0/1 port and 1..3
byte validation. `LinkClient` uses it only for a TRS->TRS delivery whose stored
chain is non-empty; an unpiped TRS send and every USB->TRS send stay on 0x15.

## Panel interaction (docs/panel-interaction.md)

Implemented in `AppController` with a focus ring per screen and IDs such as
`mixer.gain.N`, `routing.mask.S.C`, `fx.A.param.N`, `recorder.track.N`,
`track.select.N`, `punch.in.measure`, `monitor.comp.thresh`,
`system.backend`, `projects.row.N`, `loop.start`/`loop.end`/`loop.enable`,
`track.input`/`track.input.pair`/`track.output`,
`clock.clock.N`/`clock.transport.N` and `midimon.clock.filter`/`midimon.reset`/
`midimon.port.N`; the tab row itself is `tab.0..6`.

* NAVIGATE: NAV moves the focus ring (wrap); NAV_PRESS toggles a toggle or enters
  EDIT on a value; EncoderSw arms the selected strip; encoder turn changes tempo
  (SHIFT held = fine).
* EDIT: encoder adjusts the focused value (SHIFT held = fine); NAV_PRESS or a
  short EXIT leaves EDIT; the value stays and the dirty dot covers persistence.
* SHIFT: EXIT held >500 ms replaces the tab row with Save / Panic / Tap / Click;
  NAV moves across them and NAV_PRESS runs the action. A short EXIT dismisses an
  open keypad/confirm, else leaves EDIT. A redundant long-hold is a no-op.
* Save = save current project; Panic = CC123 on all 16 channels of USB + TRS
  outputs; Tap = 3+ taps inside 2 s set the tempo; Click = metronome toggle.
* Panel LEDs follow recording/overdub/rolling exactly as before.

## Feature parity checklist

| Feature | Status |
|---|---|
| Banked mixer (18 strips, 2 banks, master, meters, clip LEDs) | **works** |
| MIXER/TRACK mode + single-track strip | **works** |
| TAKE screen: per-track list, ACTIVE, pointer import, rename/delete | **works**; engine take playback selected from the list |
| Per-track input monitoring Off/Auto/On + mixer marks | **works**; persisted as `mixer.channels[].monitor` |
| Transient value popup (touch + panel EDIT) with pan Center/dB/% formatting | **works**; 1.5 s auto-hide, centre detent |
| Per-track take naming `<nn>-<trackname>-<take#>` | **works**; app-side rename after stop |
| Punch marker UI + session v2 persistence | **works**; transport scheduling not wired |
| Metronome / monitor / send matrix / main comp UI | **works**; persisted and pushed to the engine |
| TOOLS disk banner, brightness, app/firmware versions | **works**; brightness needs a Linux backlight |
| Per-track take loop (beats, whole-take rule) | **works**; engine `setTakeLoop`, re-applied on tempo change |
| Per-track input source / output destination pickers | **stored + displayed**; engine has no per-track source/destination yet |
| Per-output clock/transport gates (TRS OUT1/2, USB) | **works**; `EV_CLOCK_ROUTE` + Pi-side USB mirror gates, divider/delay session-only |
| MIDI MONITOR tab: per-port counters + event log | **works**; Pi-side view, Pico-generated TRS OUT clock not echoed |
| Routing patchbay: wire/node toggle, RESET, 16-channel masks, transpose | **works** |
| FX params + state (two CLAP send slots) | **works**; state is saved/loaded at fixed `~/Music/fx-slot-A/B.state` |
| Recorder arm / take start-stop / rename / delete | **works** |
| Take list (files, duration, size) | **works**; in-tree float-WAV header parser |
| Projects save/load/rename/delete, dirty dot (tab + SAVE + strip) | **works**; session v1 incl. click and midimap |
| Touch keypad | **works** (QML overlay, 44 px keys, ASCII/48-char rules) |
| Transport strip + metronome toggle | **works**; click level/bars on System |
| System status + backend selector | **works** (Qt default/emulator/device, input, buffer, live/note, log) |
| Panel-button mapping | **works** per the NAVIGATE/EDIT/SHIFT model |
| Session apply | **works**; mixer/router/FX/tempo/click/midimap |
| Click config | **works** (enable, level, 1-8 bars; session round-trip) |
| MIDI-learn capture mode | **works** (target picker, arm, next CC binds, remove/clear; persisted in `midimap`) |
| EQ grid: 4 nodes, response curve, corners, track+Main, fullscreen | **works**; every edit is forwarded live to `Engine::setTrackEqBand`/`setMainEqBand`, and a loaded Q > 10 is normalized to 10 with a log note |
| EDIT timeline: 18 tracks (6/page), `.peaks` thumbnails, IN/OUT, ops, undo, loop brace, punch markers | **works**; regions are session v2 `takes.edit` applied through `Engine::setTakeRegions`; RENDER is refused naming the missing core call |
| Routing SHIFT pipe chains: 16 types, add/remove/reorder/bypass, params, MIDI learn | **works**; Pi-owned routes run every type on the Pi through the shared core, with no 24-PPQN grid pass yet (a clocked pipe there says `NO PI CLOCK TICK`), TRS->TRS chains mirror to the Pico (0x18/0x19) and routed bytes go out 0x1A |
| X/Y performance grids, multitouch, panel path, freeze | **works**; built-in delay/reverb run on the FX1/FX2 sends ahead of the CLAP slots; freeze is a parameter macro |

### Explicitly dropped / changed from the JUCE app

* **JACK backend UI** — no JACK-specific code; Qt Multimedia device list instead;
  PipeWire/Pulse patchbay covers inter-app routing (see above).
* **FX plugin file picker / state file picker** — no Qt Widgets file dialog in a
  `QGuiApplication`; the FX screen loads `FxSlot::defaultPluginPath()`
  (`MNKY_DEFAULT_PLUGIN`, `MNKY_PLUGIN_PATH`, or conventional locations) and the
  default state paths. A plugin that is not on that path cannot be loaded from
  the UI.
* **Take directory chooser** — the take directory stays
  `~/Music/mnkybox-takes` (shown on screen); no folder picker.
* **18-channel hardware capture** — the Qt source is stereo into inputs 1/2;
  the 18-channel UMC1820/ADA8200 map is still unproven. The emulator fills 18.
* **xrun counter / measured latency** — `n/a (Qt)`; no audio-timing claim.
* **JACK period/rate ownership** — n/a after dropping JACK.
* **Hot-plug auto refresh** — devices are re-probed on startup, APPLY and
  REFRESH (not on a timer).
* **Overdub** remains a flag/LED only; SysEx is still not forwarded; an arm-mask
  change does not extend a running take (core limitations carried over).
* **`click.bars`** is persisted and editable (1-8) but `audio::ClickSource` has
  no count-in parameter, so the rendered pip remains the 4/4 accent pattern. The
  JUCE app had the same limitation.
* The old JUCE `FileChooser`-based screenshots/QA method is replaced by the
  offscreen `--screenshots` runner; touch hardware itself is still unverified.

## Verification performed

macOS (Qt 6.12.0, AppleClang):

* `cmake -S midibrain/pi -B /tmp/pi-build -DCMAKE_PREFIX_PATH=$(brew --prefix qt)`
  and `cmake --build /tmp/pi-build -j8`: **warning-free**, all targets.
* `ctest --test-dir /tmp/pi-build --output-on-failure`: **12/12 passed**
  (incl. `mnkybox_app_logic` = 161 checks, 0 failures).
* `QT_QPA_PLATFORM=offscreen /tmp/pi-build/app/mnkybox --screenshots /tmp/mnky-shots`:
  **9/9 PNGs**, 0 failures: `31-{mixer,clip,track,takes,punch,monitor,tools,keypad,value-popup}.png`.
* `QT_QPA_PLATFORM=offscreen /tmp/pi-build/app/mnkybox --screenshots-live` in the
  guest against PipeWire: the earlier 30-* live run remains the PipeWire evidence
  (`30-qt-pipewire-system.png`); this wave's 31-* set is the emulator screenshot run.
* Hardware-path smoke (no `--emulator`, offscreen): the real QAudioSink opened
  `Qt Multimedia / MOMENTUM 4, 48000 Hz / 128 frames`; the link still fell back
  to the emulator transport because macOS has no spidev. This proves the sink
  path starts, not that audio timing or the capture path is correct.

Linux guest (Debian bookworm arm64, Qt 6.4.2):

* rsync of host `midibrain/{pi,firmware}` (excluding `qemu`, `build`,
  `node_modules`, `dist`, `fab`, `.git`) to `~/mb-src/`, ccache-warmed
  `cmake --build ~/pi-build2 -j8`: **green in 6.5 s**, no warnings.
* `ctest --output-on-failure`: **12/12 passed**.
* `QT_QPA_PLATFORM=offscreen ./app/mnkybox --screenshots ~/qt-shots`:
  **9/9 PNGs**, 0 failures. The wave-3 set is checked in as
  `qemu/screenshots/31-{mixer,clip,track,takes,punch,monitor,tools,keypad,value-popup}.png`.
  The earlier 30-* set and the live PipeWire evidence are kept alongside it.

### Wave 5 run (EQ + X/Y)

macOS (Qt 6.12.0, AppleClang, fresh build directory `/tmp/pi-eq-final`):

* `cmake -S midibrain/pi -B /tmp/pi-eq-final2 -DCMAKE_PREFIX_PATH=$(brew --prefix qt)`
  then `cmake --build /tmp/pi-eq-final2 -j8`: **exit 0, zero warnings** (a clean
  configure+build of the shared tree; one earlier attempt raced another team's
  mid-edit `pi/midi` CMake and was simply re-run).
* `ctest --test-dir /tmp/pi-eq-final2 --output-on-failure`: **16/16 passed**
  (`mnkybox_app_logic` = 298 checks, 0 failures; the wave added 94 EQ/X/Y checks,
  including one impulse test per send chain that proves the built-in delay and
  reverb really process the FX1/FX2 buses and that the grid X/Y positions reach
  the DSP).
* `QT_QPA_PLATFORM=offscreen ./app/mnkybox --screenshots /tmp/eq-shots`:
  **21/21 PNGs**, 0 failures, no QML warnings; the new steps are `33-eq`,
  `33-eq-popup`, `33-eq-full`, `33-xy`, `33-xy-params`, `33-xy-freeze`,
  `33-eq-type`, `33-eq-shift`.

Linux guest (Debian bookworm arm64, Qt 6.4.2, `~/pi-build2`):

* rsync of the same tree (excluding `qemu`, `build`, `node_modules`, `dist`,
  `fab`, `.git`) + `cmake -S ~/mb-src/pi -B ~/pi-build2` + full
  `cmake --build ~/pi-build2 -j8`: **exit 0, no warnings, 10.4 s** (ccache warm;
  the shared tree also carried other teams' midi/audio changes).
* `ctest --output-on-failure`: **16/16 passed**.
* `QT_QPA_PLATFORM=offscreen ./app/mnkybox --screenshots ~/qt-shots-33`:
  **21/21 PNGs**, 0 failures, no QML warnings; the log carries
  `xy: 2 TOUCH - DELAY x1 + REVERB x1` from the multitouch step, and
  `--unittests` prints 298 checks, 0 failures. The wave-5 set
  is checked in as
  `qemu/screenshots/33-{eq,eq-popup,eq-full,eq-shift,eq-type,xy,xy-params,xy-freeze}.png`.
  The 31-*/32-* shots still show the old seven-tab row (this wave only wrote
  33-* files).

### Wave 6 run (no-Pico mode, bare Pi 5)

macOS (Qt 6.12.0, AppleClang, clean directory `/tmp/pi-nopico-final`):

* `cmake -S midibrain/pi -B /tmp/pi-nopico-final -DCMAKE_PREFIX_PATH=$(brew --prefix qt)
  -DCMAKE_CXX_COMPILER=/usr/bin/c++` then `cmake --build /tmp/pi-nopico-final -j8`:
  **exit 0, zero warnings** (the app sources stay under `-Wall -Wextra -Werror`).
* `ctest --test-dir /tmp/pi-nopico-final --output-on-failure`: **17/17 passed**.
  `mnkybox_app_logic` prints **374 checks, 0 failures** (the no-Pico block adds 33
  checks: NO PICO status/no SPI stats, direct START/STOP, Pi tick free-run, the
  gate-independent free-run tick and USB-gate suppression, the TRS 0x1A
  suppression log/monitor, Routing/Monitor UNAVAILABLE and `INERT (NO PICO)`);
  `test_midi` adds `clock free-run no-Pico scheduling`.
* `QT_QPA_PLATFORM=offscreen ./app/mnkybox --screenshots /tmp/nopico-final-shots --no-pico`:
  **28/28 PNGs**, 0 failures, no QML warnings, including
  `35-bench-mixer.png` and `35-bench-system.png`.
* Default-path regression check: the same binary without `--no-pico`
  (`--screenshots /tmp/nopico-default-shots --emulator`) produces **26/26
  steps** (no 35-* steps), `31-mixer.png` has no NO PICO badge and no touch
  PLAY/STOP/REC, and `34-pipes.png` still shows `ACTIVE` / `ACTIVE (PICO
  CLOCK)` / `CLOCKED PIPE LIVE (Pico clockTick)`. The flag is the only switch.

Raspberry Pi 5 (studio, Debian 13 trixie arm64, Qt 6.8.2, native build):

* rsync of `midibrain/{pi,firmware}` to `~/mnky` (same excludes as above), then
  `cmake -S pi -B /tmp/pi-build-nopico -DCMAKE_CXX_COMPILER_LAUNCHER=ccache`
  (fresh directory, configure exit 0) and `cmake --build /tmp/pi-build-nopico -j4`:
  **exit 0, zero warnings**.
* `ctest --test-dir /tmp/pi-build-nopico --output-on-failure`: **17/17 passed**.
* `QT_QPA_PLATFORM=offscreen /tmp/pi-build-nopico/app/mnkybox --screenshots
  /tmp/pi-nopico-shots --no-pico`: **28/28 PNGs**, 0 failures, no QML warnings.
* `QT_QPA_PLATFORM=offscreen timeout -s INT 8 /tmp/pi-build-nopico/app/mnkybox
  --no-pico` (hardware backends, no `--emulator`): exit 124 from the timeout;
  the log shows `--no-pico selected: no Pico, no SPI transport`, `link: NO PICO
  mode`, `audio: Qt Multimedia / UMC1820 Multichannel, 48000 Hz / 384 frames`,
  `midi: USB 7 in / 14 out`, `app: started with device backends` and the no-Pico
  active/absent banner line. There is no spidev open and no link-lost line
  (grep: 0 spidev, 0 "link: lost").
* `QT_QPA_PLATFORM=offscreen /tmp/pi-build-nopico/app/mnkybox --screenshots-live
  /tmp/pi-nopico-live --no-pico`: 28/28 PNGs against the real UMC1820 sink
  (`midi: USB 7 in / 14 out` - the count follows the attached ALSA MIDI
  devices). The checked-in `qemu/screenshots/35-bench-system.png` shows `LINK
  NO PICO`, `TRANSPORT tick 159 ... owned by the Pi (NO PICO)`, the UMC1820 live
  audio line and `MIDI USB ... (NO PICO: USB only)`; `35-bench-mixer.png` shows
  the persistent NO PICO badge and the touch PLAY/STOP/REC controls while
  rolling. The same run's `34-monitor.png` shows all four TRS rows `UNAVAILABLE
  - NO PICO (Pico link disabled)`, live USB rows and `USB OUT ... clk 140`; 140
  equals the Pi tick in the transport readout, i.e. the free-run clock emitted
  one 0xF8 per tick.

No Pico was attached to the studio Pi (`/dev/spidev0.0` is absent), which is the
condition this mode is for. No MIDI-clock timing/jitter measurement is claimed
for the Pi free-run; the no-Pico pump is an ordinary 1 ms thread, not an RT task.

No hardware, touch-panel, audio-timing, measured metronome-level or JACK/PipeWire
routing test is claimed. The screenshots are offscreen software-renderer grabs,
not photographs of the panel. Punch scheduling, independent VOL/GAIN session
persistence and a firmware version readout remain explicitly unimplemented.

### Wave 7 run (monitor parity, per-device routing, pipe scope, tap driver)

macOS (Qt 6.12.0, AppleClang, clean directory `/tmp/pi-build-clean`):

* `cmake -S pi -B /tmp/pi-build-clean -DCMAKE_PREFIX_PATH=$(brew --prefix qt)
  -DCMAKE_CXX_COMPILER=/usr/bin/c++` + full `cmake --build ... -j8`: **exit 0,
  zero warnings** (`-Wall -Wextra -Werror` on the app sources).
* `ctest --test-dir /tmp/pi-build-clean --output-on-failure`: **20/20 passed**;
  `mnkybox_app_logic` prints **433 checks, 0 failures**.
* `QT_QPA_PLATFORM=offscreen ./app/mnkybox --screenshots pi/qemu/screenshots`:
  **36/36 PNGs**, 0 failures. The wave-7 shots are
  `38-monitor4.png` (MPC INPUT MON PLAY/AUTO/BOTH/MON),
  `38-routing-devices.png` (the detected IAC Driver Bus 1 as a separate
  input and output node beside TRS IN/OUT 1+2 and the merged USB IN/OUT), and
  `38-pipechmask.png` (the 16-toggle pipe scope popup, preset row and
  "SCOPE CH 1-4" readout).
* `./app/mnkybox --ui-taps`: **375 taps, 0 QML warnings, 0 failures** in 29 s.
  The runner walks the tab bar, transport strip, all ten primary screens, every
  SHIFT face (both MIXER faces plus the Routing filter and pipe pages), and all
  nine ConfigPopup kinds (every non-terminal control + CANCEL, then OK). It
  caught two real defects: the new `togglePipeChannel` was not `Q_INVOKABLE`,
  and the pipe row's up/down/remove handlers selected then mutated the chain
  from a ListView delegate, which destroyed the delegate mid-handler
  (`ReferenceError: routing is not defined`). Both are fixed; the row actions
  are now single `movePipe` / `removePipe` calls.
* Input monitoring is four-way end to end: `MixerModel` stores session
  `monitor` 0..3, maps 3 to `audio::MonitorMode::InputOnly`, and the project
  round trip (scratch Engine -> session capture -> load) preserves MON without
  an app-side override. `Engine::setMonitorMode(InputOnly)` is the engine half.
* Routing nodes are data-driven from `MidiGlue::usbPorts()`: fixed ports 0..5
  stay first, registry ids 6..31 follow with their stable `"usb:<name>"`
  identity, and route/pipe/channel-mask edits use the same views for both.
  The channel-filter page pages in three source rows; the pipe page cycles the
  FROM/TO node lists so a long device list never shrinks a touch target.
* `project_io` now saves/loads the router through
  `MidiGlue::captureSessionRouter()` / `applySessionRouter()`, so a per-device
  route and its `MidiPipe::chMask` survive a project round trip (covered by the
  unit test).

No hardware or touch-panel measurement is claimed; the taps are in-process
offscreen `QMouseEvent` pairs, not a real digitizer.

### Wave 8 run (dynamic mixer, live settings, selector popups, FX tab, channel popout)

Owner direction: the mixer must be a dynamic track list (start empty, ADD/REMOVE
TRACK), four strips per page with bigger faders, no apply buttons anywhere,
selector popups instead of in-place cycle steppers, the built-in delay/reverb
full settings on the FX tab beside the CLAP slots, and a full channel popout
from a strip. Implementation and seams:

* Dynamic mixer. `MixerModel` keeps the fixed per-slot store and adds a UI-side
  ordered `tracks` list plus an atomic per-slot `active` gate. ADD TRACK appends
  the first free slot with the popup's name, input (none / mono 1..18 / stereo
  pair) and output (Main / OUT3&4 / phones / none); REMOVE TRACK (confirmed in
  `ConfirmDialog`) drops the strip while the slot's settings, name, insert and
  takes stay in the project, so re-adding restores them. A removed slot is
  silent by contract: `applyTo()` forwards input `-1` and output destination
  `None` for it and `armMask()` never includes it. A new project is empty; the
  session v2 reader fills all 18 slots for a legacy file that has no
  `mixer.tracks` key.
* Four strips per page. `MixerViewModel` rows are the track order and each row
  carries its slot (`SlotRole`), so a strip's controls and focus ids address the
  slot, not the position. The bank pages four strips and the header readout is
  `TRACKS n-m / total`; the two historical bank buttons step one page.
* No apply buttons. `ConfigPopupModel` writes every change to the models the
  moment it is made and CANCEL restores the values the popup opened with, so
  OK/CLOSE never applies anything late. ADD TRACK is the one kind that creates
  on OK (CANCEL adds nothing); the timeline region ops keep OK = RUN. The System
  device/buffer pickers reopen the audio stream on every pick, and the audio
  layer's fallback reason is shown both on the screen and as a notice popup.
* Selector popups. Cycle-style selectors are tap-to-open option lists:
  input source (none, mono 1..18, nine pairs), output destination, per-track FX
  insert (none / builtin delay / builtin reverb), delay division, reverb model,
  and the Routing pipe page's FROM / TO / ADD type. `TouchButton` draws a chevron
  (`down` = opens a list, `up` = expands the channel) as the visual affordance;
  the value button stays the same 44 px target.
* FX tab. The primary face shows the built-in Delay and Reverb panels with their
  FULL parameters (time, feedback, mix, beat, division, ping-pong, feedback-path
  cutoff and width; model, pre-delay, decay, damping, mix, size) beside compact
  CLAP send A/B cards. SHIFT keeps the large CLAP B parameter editor with both
  return levels. The built-in values are `PerformanceModel`'s own parameters, so
  an X/Y grid move and a panel knob edit are the same value.
* Channel popout. The strip's expand chevron opens `ChannelPopout`: fader with
  meter, pan, trim, M/S/ARM, the four send levels, the four-way input monitor,
  input source, output destination, the per-track insert with bypass, the four
  EQ band summaries and REMOVE TRACK. Every field is live; CLOSE only closes and
  the popout traps the panel focus ring while it is open. The TRACK tab stays.
* Per-track insert. `MixerModel::Insert {id, bypass, params}` is stored per slot
  and travels in session v2 `mixer.channels[].insert`; the engine instance path
  lands in the parallel audio work item, so this wave ships the selector, the
  bypass and the persistence.

Verification (macOS, Qt 6.12.0, AppleClang, clean directory
`/tmp/pi-build-uxclean`, configured with `-DFETCHCONTENT_SOURCE_DIR_CLAP` at the
previous run's fetched headers so the clean configure needs no network):

* `cmake -S midibrain/pi -B /tmp/pi-build-uxclean ...` + full
  `cmake --build ... -j14`: **exit 0, zero warnings** (`-Wall -Wextra -Werror`
  on the app sources).
* `ctest --test-dir /tmp/pi-build-uxclean --output-on-failure`: **21/21 passed**;
  `mnkybox_app_logic` prints **750 checks, 0 failures**.
* `QT_QPA_PLATFORM=offscreen ./app/mnkybox --ui-taps`: **603 taps, 0 QML
  warnings, 0 failures**. The run covers the empty mixer, the add-track popup,
  the channel popout and every ConfigPopup kind, including the six new selector
  lists and the pipe FROM/TO/type lists.
* `./app/mnkybox --screenshots /tmp/ux-final`: **81/81 PNGs**, 0 failures. The
  wave-8 proof set copied into `pi/qemu/screenshots/`: `40-mixer-empty.png`,
  `40-mixer-add-track.png`, `40-mixer-bank.png`, `40-fx-builtin.png`,
  `40-channel-popout.png`, `40-popup-input.png`, `40-popup-insert.png`.
* `projectRoundTripTests` covers the dynamic track list and the per-track insert
  through a real save/load, so `mixer.tracks` and `mixer.channels[].insert` are
  proven on the app<->session seam.

No hardware, touch-digitizer, audio-timing or plugin-instance test is claimed.
The FX insert's audio path, and a written track name in a legacy project (the
take naming keeps the `chNN` default when the name is empty), remain explicit
limits of this wave.

### Wave 9 run (pipes declutter, popup selectors, knob-card parameters)

Owner direction: "the midi pipes is a bit cluttered"; "< name > buttons" must
become popup selectors; "make use of encoders in more places" (the arc-dial card
from the comp popup). Implementation:

* Routing SHIFT > PIPES is rebuilt around popup selectors. FROM and TO are two
  live readouts that open the port lists (`pipesource` / `pipedest`); the ADD row
  is the type selector (`pipetype`), a compact ADD, the `SCOPE CH n` chip
  (`pipechannels`) and one `CHAIN` button. EXPORT/IMPORT moved into that chain
  menu popup (`chainmenu`), which frees the row. Both `< >` stepper pairs, the
  type stepper and the parameter pager are gone, along with
  `RoutingViewModel::paramPage/setParamPage/cyclePipeSource/cyclePipeDestination/
  cycleAddType` and `pipeui::pageCount`.
* The type popup is a scrolled list of the 16 schema types with each type's
  schema role and the selected type's doc line (`pipeui::pipeRoleName` /
  `pipeDocs`), so a pick is made from the description.
* The chain is one compact line per pipe (index + name + status chip + parameter
  count). A tap selects the row for parameter editing; BYP/up/down/remove exist
  on the selected row only, and the focus ring stops there too.
* Parameters are `PipeParamCard.qml` cards in a two-column grid: an arc dial
  with label and value for a numeric parameter (drag or panel encoder), a
  label + value chip that opens the `pipeparam` ConfigPopup for a bool/enum
  parameter. Four cards fit; a longer schema scrolls. Every card carries
  `shift.routing.pipes.param.<pipe>.<param>` so panel EDIT/encoder drives it.
* Value popup placement: the readout now clamps below the persistent bridge
  (tab row + transport strip) instead of covering the tab row (`ValuePopup.qml`),
  and stays at the page margin in a fullscreen face.
* Stepper sweep: the routing transposes on the FILTER page are arc-dial cards
  (`shift.routing.transpose.<n>` preserved), the click BARS value is a
  `ConfigKnob` card, and the delay DIVISION is a full eight-button choice row
  instead of a prev/name/next stepper.
* Engine split corrected against the code: the shared midifx core implements all
  16 types and no longer rejects ids 12..16, which the old status line claimed.
  What is still missing Pi-side is the 24-PPQN grid pass (`MidiGlue::pump`
  drives `Router::tick` only; nothing calls `Router::clockTick`), so a clocked
  pipe on a Pi-owned route now reads `NO PI CLOCK TICK` on its row and
  `CLOCKED PIPE: PI HAS NO 24-PPQN TICK` in the header; a TRS->TRS chain reports
  `CLOCKED PIPE LIVE (Pico clockTick)`.

Verification (macOS, Qt 6.12.0, clean directory `/tmp/pi-build-ui41-final`):

* fresh `cmake -S midibrain/pi -B /tmp/pi-build-ui41-final
  -DCMAKE_PREFIX_PATH=$(brew --prefix qt)` + `cmake --build ... -j14`:
  **exit 0, zero warnings**.
* `ctest --test-dir /tmp/pi-build-ui41-final --output-on-failure`: **21/21
  passed**; `mnkybox_app_logic` prints **841 checks, 0 failures** (new coverage:
  the parameter choice popup for a bool and an enum, its live write and CANCEL
  restore, the type list's roles/docs, and the corrected engine-split readout).
* `QT_QPA_PLATFORM=offscreen ./app/mnkybox --ui-taps`: **532 taps, 0 QML
  warnings, 0 failures**, including the two card-family passes
  (`shift.1.pipes.cards.toggle`, `.knob`), the generic pipes pass, the two chain
  menu scenarios and the new `config.pipeparam` / `config.chainmenu` kinds.
* `./app/mnkybox --screenshots /tmp/ui41-shots`: **89/89 PNGs**, 0 failures. The
  wave-9 proof set copied into `pi/qemu/screenshots/`: `41-routing-pipes.png`,
  `41-routing-pipes-knob.png`, `41-popup-pipetype.png`,
  `41-popup-pipeparam.png`, `41-chain-menu.png`, `41-popup-metronome.png`,
  `41-popup-delay.png`. The 39-*/40-* files stay untouched as the before set: a
  full re-run rewrites those names with the new layout.
* Raspberry Pi 5 (studio, Debian trixie arm64, Qt 6.8.2, offscreen only, under
  `nice`/`ionice`): incremental build of the synced tree **exit 0, zero
  warnings**, `--ui-taps` **550 taps, 0 QML warnings, 0 failures**,
  `--screenshots` **89/89, 0 failures**; the 41-* render matches the macOS set.

Named tap-driver limits: the parameter grid scrolls, so each card scenario
reaches the four visible cards only (all cards keep their focus ids in the ring);
the chain menu's EXPORT/IMPORT keep their historical
`shift.routing.pipes.chain.*` ids, so the generic `config.` passes do not tap them
and the two dedicated chain scenarios do. No hardware, touch or timing claim.

## Capacity (64 tracks / 128 channels, 2026-11-06)

Named capacities from `audio/capacity.hpp` everywhere in the app path:
track slots/arrays/loops use `audio::kMaxTracks` (64), device capture width
uses `audio::kMaxDeviceInputs` (18), `kNumStrips` is the legacy fixed-row
screens' width only. Mixer banks paginate the tracks LIST (4 per page;
64 tracks = 16 pages); input enumeration is model-driven (device width,
never a literal). Masks are `uint64_t` (`MixerModel::TrackMask`);
`RecorderViewModel::adoptRecordedTake` matches `Recorder::startTake(uint64_t)`.
App-owned take state (SessionSettings loop/edit/active arrays, AudioGlue
meter taps) is `kMaxTracks`-wide. Saving a project with a track slot beyond
the session store fails CLOSED with an explicit reason instead of writing an
unloadable file. Assumption: the engine-side capacity raise (done in
pi/audio/capacity.hpp by the engine wave) keeps `kMaxInputs` as the
legacy device-width name; session entry counts follow `audio::kMaxInputs`.

## Wave 9: slider law + knob chips (2026-11-06)

Owner rule: tight space => KnobChip, never a slider (spec §6). KnobChip.qml =
framed arc dial + label + value with drag, panel-encoder via focusId and
value-popup during drag. Converted: mixer shift sends, strip PAN, TRACK
rows, channel popout sends, FX returns, brightness, loop/punch fields, and
all ConfigPopup params (ConfigSlider deleted). Kept: strip/master/popout
gain faders. 41-* screenshots cover the converted surfaces; --ui-taps 547
clean, 841 app checks.

## Wave 10: per-track FX rack UI (2026-11-06)

Owner direction: "per track delay/reverb settings", "select what we want on
FX1/FX2", "move away from the global FX tab, use a popup for configuring per
track FX settings". Engine + session landed in 2ee640c (audio wave 11); this
wave is the last mile: the rack reaches every surface.

**Data path.** `MixerModel` now owns the rack (`fxSlots[2]` per track:
`{id, bypass, wet, params}`). `MixerModel::setFxSlot()` calls a control-thread
sink that `AudioGlue` installs (`applyTrackFxSlot()` ->
`Engine::setTrackFxSlot()`), so the engine's allocating setter is never reached
from the audio callback (`applyTo()` stays allocation-free). A session load
forwards every stored slot once (`applyTrackFxRack()`), and `project_io` writes
both slots plus the deprecated `insert` (the FX1 view) from the model, so save
and the engine agree. `fx_params.{hpp,cpp}` is the app's one table of the
built-in effects' stable indices, ranges, defaults, units and value formats; the
index constants come from `audio/fx/delay.hpp` / `reverb.hpp` (static_asserts
catch a reorder), the values restate what the effect constructors publish, and
the effect still clamps everything it receives.

**TRACK FX popup (`qml/overlays/TrackFxPopup.qml`, `fx_view_model.*`).** Opened
from the channel popout's TRACK FX row (which replaced the old INSERT row) and
from the X/Y screen's parameter buttons. Two slot cards, FX1 then FX2: effect
selector (opens the `fxslot` ConfigPopup: NONE / DELAY / REVERB / PLUGIN), WET
KnobChip, BYPASS chip, the effect's full parameter grid as KnobChip cards (the
slider law), and the subline "series FX1 -> FX2 - per track". Every edit is live
through MixerViewModel, so the rack, the session and the engine are one value.
**CLAP choice: retired the CLAP slot panel with the tab.** `FxSlotPanel.qml`,
`FxParamKnob.qml`, `BuiltinDelayPanel.qml`, `BuiltinReverbPanel.qml` and
`FxScreen.qml` are deleted; `fx_slot.*` (FxSlot/FxManager) stays as the engine's
send-bus CLAP seam for a later host wave. Per-track CLAP hosting is still not
wired in that wave (audio wave 11 "CLAP gap"), so the PLUGIN choice was listed
but disabled then; the gap-closing wave below replaced it with the real plugin
list and a live host per CLAP slot.

**FX tab retired (9 tabs).** Row is Mixer, Routing, Recorder, System, Projects,
Monitor, EQ, X/Y, EDIT (`kScreenCount` 9, tab constants in `app_controller.cpp`
and `Main.qml`); `tabHasShiftFace()` is tabs 0..3 + Projects + EQ. EDIT stays for
the DAW plan's later popup move.

**Strip knobs = WET.** The FX1/FX2 knobs on the mixer SHIFT strips (roles
`fx1Wet`/`fx2Wet` + the slot's effect name), the TRACK SHIFT rows, the channel
popout and the panel EDIT paths all bind `fxSlots[i].wet`; labels stay FX1/FX2
and the value popup reads "FX1 WET n%". The MIDI-learn targets were renamed
`mixer.chNN.fx1wet`/`fx2wet` (an old `sendA`/`sendB` binding no longer binds:
the bus it wrote is retired). The deprecated per-track `send[i]` values are only
written by the session loader now (they still round-trip and the reader migrates
them into `wet` for a file without `fxSlots`); the Monitor SHIFT SENDS popup
labels its FX1/FX2 source rows as legacy in one line.

**X/Y per track.** Grid 0 bends the selected track's FX1, grid 1 its FX2
(delay X = cutoff, Y = width; reverb X = size, Y = level) through the same rack
writes, so a grid move is exactly the slot parameter. The selected track is the
mixer's selected strip and the screen gained a track row on the mixer's
four-per-page banks. A slot that is NONE or a plugin id has no grid: it shows
"slot is NONE - pick DELAY or REVERB" (or the plugin gap) plus a PICK EFFECT
chip that opens the same slot selector. FREEZE is the reverb macro over the
selected track's reverb slot and restores the exact vector on release. The X/Y
delay/reverb parameter popups (`delay`, `delaydivision`, `reverb`,
`reverbmodel`) were retargeted from `PerformanceModel` to the rack slot
AppController points them at; CANCEL restores the whole slot vector. The global
`PerformanceModel` send-bus chain and `session fx.xy`/`fx.bus` stay in the
format and in AudioGlue (the engine ignores the retired buses) but no screen
writes them any more.

### Wave 10 run

macOS (Qt 6.12.0, AppleClang, clean directory `/tmp/pi-fx-ui`,
`-DCMAKE_PREFIX_PATH=$(brew --prefix qt)`):

* clean configure + `cmake --build -j8`: **exit 0, zero warnings** (app sources
  stay under `-Wall -Wextra -Werror`).
* `ctest --output-on-failure`: **21/21 passed**; `mnkybox_app_logic` prints
  **903 checks, 0 failures**, including the new rack checks (slot defaults from
  the table, wet clamp, effect independence, PLUGIN refused, positional/clamped
  parameter writes, per-track popup grid rebinding, reverb model preset
  expansion, project save/load of both slots, X/Y writes = rack writes, freeze
  save/restore).
* `QT_QPA_PLATFORM=offscreen ./app/mnkybox --ui-taps --no-pico`: **559 taps, 0
  QML warnings, 0 failures**, including the two new scenarios
  `mixer.trackfx` (21 taps across both cards) and `mixer.trackfx.none`, plus the
  X/Y bound/unbound passes and the `config.fxslot` kind.
* `QT_QPA_PLATFORM=offscreen ./app/mnkybox --screenshots /tmp/fx-shots --no-pico`:
  **117 steps, 0 failures**, no QML warnings. Checked in as
  `qemu/screenshots/43-{tabs,track-fx,track-fx-wet,popup-fxslot,mixer-shift-wet,
  track-shift,channel-popout,xy-track,xy-picker}.png`.

Raspberry Pi 5 (studio, Debian trixie arm64, Qt 6.8.2, offscreen only, under
`nice`/`ionice`, fresh `/tmp/pi-fx-ui`): synced tree configure + `-j4` build
**exit 0, zero warnings/errors**, CTest **21/21**, `--ui-taps` **575 taps, 0 QML
warnings, 0 failures**, `--screenshots` **117 steps, 0 failures** (9 43-* PNGs,
written to /tmp and removed afterwards). No hardware, touch or timing claim.

Not done in this wave (named): `docs/USER-MANUAL.md` still describes the FX tab
and the global X/Y grids - a docs wave must rewrite section 10 and the tab list
(the task restricted edits to `pi/app/`); per-track CLAP hosting; MIDI-learn
bindings saved as `sendA`/`sendB` do not rebind.

### Gap-closing wave: per-track CLAP hosting + output inserts

**Per-track CLAP hosting.** `fx_slot.*` stopped being a send-bus-only seam:
`FxManager` now also owns one `FxSlot` wrapper per track rack slot, created on
the first CLAP selection (`loadTrackEffect`) and never for NONE/DELAY/REVERB.
`AudioGlue::applyTrackFxSlot` is the one control-thread path: a CLAP id asks the
manager for the instance, applies the slot's session parameter vector through
the plugin's own `clap.params` extension, and attaches the existing `IEffect`
adapter with `Engine::setTrackFxSlotEffect(track, slot, adapter)`. Built-in and
empty ids set the engine slot state first, detach with a null effect, then call
`FxSlot::unload()` (deactivate, destroy the plugin, deinit and `dlclose` the
module). The wrapper stays parked so a callback that already loaded the adapter
cannot dereference freed memory; `FxManager` destroys every wrapper after
`AudioGlue`/the Engine in `AppController` member order. No leaks are claimed
beyond that bounded wrapper pool.

Plugin discovery is lazy (`FxManager::plugins()` on the first selector/open):
`MNKY_DEFAULT_PLUGIN` first, then `MNKY_PLUGIN_PATH`, the conventional macOS/
Linux CLAP directories and `FxSlot::defaultPluginPath()`. Each candidate is
loaded just long enough to read its factory id, name and default parameter
table, then unloaded. The slot selector is NONE / DELAY / REVERB / one entry per
discovered plugin (mnkygain in this build); the TRACK FX card's parameter chips
are the plugin's own names, ranges and values, and the note names the instance
state. The session stores the CLAP id plus the positional parameter vector in
`mixer.channels[].fxSlots[]`; load recreates the instance lazily through the
same mixer sink.

Instance policy: lazy, with a measured simultaneous cap of 16 live track hosts.
The 17th selection is refused before the model changes and the selector note
reads exactly `PLUGIN LIMIT REACHED - unload one first`; switching a plugin in
an already admitted slot keeps one admission. `--clap-bench` is the reproducible
measurement mode (no audio device, no QML).

**Output inserts.** `MidiGlue` now stores the ordered `kSrcOutputInsert` chains
per destination (compact list, four slots max) and keeps the live `Router`
slots in sync on every add/remove/reorder/ON/edit. Capture writes
`RouteState{src = -2, dst, on, pipes}` in slot order and load maps them back,
including a disabled chain that still carries pipes. Only a Pi-owned output
(USB OUT or a dynamic USB output) exposes inserts; a TRS output is Pico-owned
and the INSERTS strip says so instead of pretending. The Routing SHIFT > PIPES
page gained a compact INSERTS strip: ROUTE edits the normal FROM->TO chain, a
numbered chip edits that insert chain in the same schema-driven editor, + adds
a chain, ON toggles its gate, ^/v reorder and X removes. The chain accepts every
Wave-1/Wave-2 modifier; generator-role heads are filtered out of its ADD list and
refused at the MidiGlue store boundary. Generator routes are
unchanged: FROM = GENERATOR remains source-less with a generator-role head, and
changing FROM returns the editor to the route chain.

**Run (macOS, Qt 6.12.0, clean `/tmp/pi-gap-final`).** Configure + full
`cmake --build -j8`: exit 0, zero warnings. CTest **21/21**; app logic **996
checks, 0 failures** (new CLAP host, 17th-refusal, teardown, project round-trip
and output-insert tests, including generator-head rejection in an insert). `--ui-taps --no-pico`: **606 taps, 0 QML warnings, 0
failures**. `--screenshots --no-pico`: **145 steps, 0 failures**; checked in as
`qemu/screenshots/45-{track-fx-plugin,popup-fxslot-plugin,output-insert,
generator-route}.png`.

**Studio Pi 5 (Debian trixie arm64, Qt 6.8.2).** Synced tree configure + `-j4`
build under `nice`/`ionice`: exit 0, zero warnings. CTest **21/21**; app logic
**996 checks, 0 failures**; `--ui-taps` **651 taps, 0 QML warnings**; offscreen
screenshots **145 steps** with all four 45-* PNGs. `--clap-bench` (64-frame
stereo block, 48 kHz; budget 1333.3 us):

| instances | us/block | us/instance | % of 64-frame budget |
|---:|---:|---:|---:|
| 1 | 3.41 | 3.41 | 0.26% |
| 2 | 6.82 | 3.41 | 0.51% |
| 4 | 13.65 | 3.41 | 1.02% |
| 8 | 27.32 | 3.41 | 2.05% |
| 16 | 54.68 | 3.42 | 4.10% |

The 17th selection printed `accepted=0 message="PLUGIN LIMIT REACHED - unload
one first"`; teardown left 0 instances. This is CPU cost of the plugin
process() calls only, not a full-engine real-time, latency or audio-quality
claim.

## DAW W4/W5 — ARRANGE tab, SONG mode, punch-into-song, RENDER SONG

**Song model and persistence.** The ARRANGE tab is `ArrangeViewModel` plus
`arrange_logic.*` (Qt-free section/clip math covered by `--unittests`). Song
state lives in the session `song` object: `mode`, `lengthBeats`, the loop window
`{on,startBeats,endBeats}`, sections, clips, grid and the deprecated
`lengthBars`. `project_io.cpp` mirrors the live `SessionSettings` editor fields
into that object on save and back out on load, all in `project.mnkybox`;
`lengthBars` is written as `round(lengthBeats/4)` for old readers. The legacy
app-owned `arrange.mnkybox` sidecar is never written. A project load folds it
once and removes it: the sidecar wins only when `project.mnkybox` predates the
song timing keys (loaded `lengthBeats == 0`), and a stale sidecar is ignored and
removed when the session already carries them. `songMode` defaults on; an
empty/new arrangement keeps the engine in free-take mode
(`applySongToEngine` only enables the clip scheduler when at least one lane
resolves), so the W1 take path is never silently muted by an empty SONG project.
The whole-song loop is on by default, end 0 = song length.

**Transport seek.** The Pico owns the raw tick and `link_client.*` is
untouched. A tap on the ARRANGE playhead writes a signed
`AppState::transportOffsetTicks`; `AudioGlue::prepareEngineBlock` passes
`transportTick(state.tick, offset)` to `Engine::setTransport`, and every UI
readout/punch decision uses the same helper. On a stopped→rolling transition
in SONG mode the offset is rebased with the previous link tick, so a seek while
stopped or rolling survives the Pico START tick reset and STOP+PLAY continues
from the playhead. Free mode keeps its legacy START-at-bar-1 behaviour. The
engine's existing song scheduler sees the frame discontinuity and seeks its
reader; no new engine API was needed.

**Punch into song.** `pollRecSchedule` compares the displayed song bar with the
punch markers. In SONG mode the marker measures are loop-relative (absolute
beat = loop start + (measure-1)*4) and a REC press engages once
(`punchEngaged_`); PUNCH OUT calls `stopTake(keepRolling=true)` so the transport
continues. After the take is adopted, `placePunchTakeClips` resolves each armed
track's new active take and adds a clip at the punch in beat; the notice is
`TAKE SAVED -> clip at bar N`. With SONG off the original absolute punch
semantics are unchanged.

**RENDER SONG and the master take.** `AudioGlue::renderSong` holds a render lock
that the audio callback also takes, so the engine's offline render never races
`process()` (the callback is paused for the duration, as the engine contract
requires). The output is `song-YYYYMMDD-HHMMSS.wav` in the take folder. The
Recorder scan lists root-level `song-*.wav` files on every track as `MASTER`
entries; selecting one only makes it that track's ACTIVE take for audition and
never changes the file. The notice states the main-bus scope honestly:
`SONG RENDERED <file> - master mix rendered (main bus)`.

**UI.** `ArrangeScreen.qml` draws the section band, ruler, lanes, clip blocks
(TimelineWave values from `.peaks`), loop band and playhead; `ArrangeShiftFace`
keeps the section verbs and grid/zoom. `ArrangeClipPopup.qml` is the clip `...`
sheet (actions plus GAIN/FADE KnobChips). Clips are dragged with a local
preview and committed once on release (`commitClipDrag`), so a model update
cannot reset the drag delegate; the engine lane is re-applied on release, not
per pixel. Focus ids cover sections, toolbar verbs, clips, clip actions and the
LEN stepper; NAV moves clips spatially and EDIT turns the knob into POS/LEN.
The transport strip gains a persisted `SONG` chip; the tab row is now nine tabs
(Mixer..ARRANGE).

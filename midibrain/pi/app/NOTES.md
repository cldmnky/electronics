# mnkybox app (`pi/app/`) — Qt 6 QML touchscreen DAW

The wave-2 UI is now Qt 6 QML: `QGuiApplication` + `QQmlApplicationEngine`, a
`QQuickWindow` shell and seven QML screens over C++ view models. The JUCE app
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
  `mnkybox_app_logic` (161 checks, 0 failures).
* `--screenshots DIR` is the headless proof: forces the emulator backends, loads
  every QML screen and writes `30-qt-*.png` with `QQuickWindow::grabWindow()`,
  then exits 0. Run it with `QT_QPA_PLATFORM=offscreen`; no Xvfb is needed.
* `--screenshots-live DIR` is the same grab run against the real Qt Multimedia
  audio backend. It was used for the PipeWire/Pulse evidence run below and does
  not imply the emulator backends.

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

## Panel interaction (docs/panel-interaction.md)

Implemented in `AppController` with a focus ring per screen and IDs such as
`mixer.gain.N`, `routing.mask.S.C`, `fx.A.param.N`, `recorder.track.N`,
`track.select.N`, `punch.in.measure`, `monitor.comp.thresh`,
`system.backend`, `projects.row.N`; the tab row itself is `tab.0..5`.

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

No hardware, touch-panel, audio-timing, measured metronome-level or JACK/PipeWire
routing test is claimed. The screenshots are offscreen software-renderer grabs,
not photographs of the panel. Punch scheduling, independent VOL/GAIN session
persistence and a firmware version readout remain explicitly unimplemented.

# mnkybox Pi DAW (`pi/`)

C++20 host software for the **mnkybox** recorder/MIDI hub: Raspberry Pi 5,
800×480 touch UI, Pico 2 as SPI link + clock master. Everything in this
directory also builds and runs **headless on the macOS dev machine** through the
emulator backends and the offscreen Qt platform.

A green build says nothing about audio latency, MIDI/SysEx behaviour on real
interfaces, the SPI link, power or the mechanical stack. See
[ARCHITECTURE.md](ARCHITECTURE.md) for the module contracts and
[../docs/](../docs/) for the hardware-side status.

## Layout

```
pi/
  CMakeLists.txt      root build: dependency pins, core libs, CTest, the Qt app
  ARCHITECTURE.md     module contracts and standing rules
  third_party/        vendored RtMidi 6.1.0 + the pinned-dependency table
  link/               Pico SPI link core
  midi/               router, USB MIDI, clock discipline, buttons
  audio/              engine, per-track FX rack, recorder + WAV, device backends
  clap/               CLAP host core + example mnkygain plugin
  session/            versioned JSON project format
  update/             Pico 2 UF2/BOOTSEL updater core
  app/                Qt 6 QML touchscreen application (`mnkybox`)
  emulator/           scripted backends + headless selftest
  bench/              studio measurement harness + `--bench-clock` driver
```

The core modules are JUCE-free and Qt-free; Qt lives in `app/` only.

## Build and test (dev machine, macOS arm64)

Requires CMake ≥ 3.24, a C++20 compiler, Qt 6 (`brew install qt`), `git` and
network access for the first configure (CLAP is fetched shallow and pinned).

```sh
cmake -S midibrain/pi -B /tmp/pi-build -DCMAKE_PREFIX_PATH=$(brew --prefix qt)
cmake --build /tmp/pi-build -j
ctest --test-dir /tmp/pi-build --output-on-failure
```

Targets:

| Target | Kind | Notes |
|---|---|---|
| `mnky_rtmidi` | static lib | RtMidi 6.1.0 compiled **once**; `__MACOSX_CORE__` (macOS) / `__LINUX_ALSA__` (Pi) is a `PUBLIC` define so every consumer sees the same `RtMidi` layout |
| `mnky_link` `mnky_midi` `mnky_audio` `mnky_clap` `mnky_session` `mnky_update` | static libs | fixed names from ARCHITECTURE.md |
| `mnky_clap_headers` | interface | CLAP 1.2.10 `include/` (headers only; the upstream CLAP CMake project is deliberately not added) |
| `mnkygain` | loadable module | example CLAP gain plugin from `clap/effects/mnkygain/` |
| `mnkybox` | executable | the Qt 6 QML app (`app/CMakeLists.txt`) |
| `mnky_selftest` `mnky_clock_sim_test` | executables | headless emulator selftest and clock-simulation check |
| `test_link` `test_midi` `test_audio` `test_clap` `test_session` `test_update` (+ module test binaries) | test binaries | registered with CTest |

### Optional modules

Every module subdirectory is optional: a missing directory skips its target
instead of failing configure.

* `mnky_core_module()` uses the module's own `CMakeLists.txt` when it has one,
  and otherwise globs `<dir>/*.cpp` (excluding `test_*.cpp`) into the fixed
  library name.
* `mnky_module_tests()` registers `<dir>/test_*.cpp` with CTest, and skips a
  test the module already defines itself. A module without tests adds none.

Test binaries are compiled with `-UNDEBUG` in every build type: module tests are
assert-based, and a `Release`/`RelWithDebInfo` `-DNDEBUG` would silently disable
every check.

Each module also builds standalone as documented in ARCHITECTURE.md
(`cmake -S midibrain/pi/<mod> -B …`, or the plain `c++ -std=c++20 …` line).

## App command line

| Flag | Purpose |
|---|---|
| `--emulator` | scripted Pico SPI transport, in-process synth instead of a device, virtual `mnkybox` USB MIDI ports |
| `--no-pico` | no link: the Pi is clock master and the TRS rows read unavailable |
| `--unittests` | headless logic tests (no window) |
| `--ui-taps` | headless tap gate over every touch control; any QML warning fails |
| `--panel-taps` | headless panel-intent gate (focus PRESS/EXIT parity) |
| `--screenshots [dir]` | offscreen screenshot set; `--screenshots-live` drives the real device |
| `--bench-clock` | automated MIDI-clock acceptance run against the real device, see `bench/README.md` |
| `--clap-bench` | CLAP host timing measurement |

## Pinned dependencies

Versions are single-sourced in `pi/CMakeLists.txt` (`MNKY_CLAP_VERSION`) and must
stay identical to [third_party/README.md](third_party/README.md). Never float on
a branch. Qt 6 comes from the host toolchain (Homebrew on macOS, Debian packages
on the Pi) and the QML stays inside the 6.4-compatible subset.

One macOS dev-machine fact is baked into the root build: **Apple clang is
selected on Darwin** when the caller has not chosen a compiler (`CC`/`CXX`
unset, no `-DCMAKE_CXX_COMPILER`), because the app needs the macOS SDK
frameworks that a non-Apple `g++` on `PATH` does not have. Standalone
per-module tests still work with the plain `c++` on `PATH`; they are then a
different C++ runtime from the CMake-built artifacts, so do not link objects
from the two.

## Pi notes

* The Pi 5 build is the same root build with `gcc`/`clang` and the system CMake;
  RtMidi is compiled with `__LINUX_ALSA__` and links `asound`. Verification
  target is the studio Pi 5 — see [STUDIO-PI.md](STUDIO-PI.md).
* Display, touch, USB-C power, cooling and NVMe stack details are tracked in
  `../docs/mechanical-stack.md` and `../docs/requirements.md`; none of them are
  proven by this build.
* Qt licence (LGPLv3 vs commercial) is undecided, so no binary built here may be
  distributed yet.

## Status

Built and running on macOS (dev) and the studio Pi 5 (product host, UMC1820):

- **App**: Qt 6 QML (`mnkybox`), 9 tabs + SHIFT faces + popups; dynamic mixer
  (tracks are created, up to 64 with stereo pairs), track modes
  (PLAY/ARM/OVERDUB/LIVE), record start modes (NOW/COUNT-IN/PUNCH), record
  source (PRE/POST/POST+FX), take manager + take-to-track, edit popup with
  full-width waveforms, per-track FX rack (FX1/FX2 selectable: built-in
  delay/reverb + CLAP plugins, lazy 16-instance hosting), EQ + per-track X/Y,
  panel/touch parity (NAV/EDIT/SHIFT + value knob), value popups, slider law,
  `--ui-taps` and `--panel-taps` headless gates.
- **Cores**: link (SPI + no-pico Pi clock master), MIDI (router, per-device USB
  ports, MIDI-learn, MIDI FX pipes), audio (64-track engine, per-track EQ and
  FX rack, take playback + regions + loops, song clip scheduler, RENDER SONG),
  CLAP host + example plugin, sessions v2 (song + takes + rack + router).
- **Tooling**: `--screenshots`, emulator + headless selftest, studio bench
  harness (jitter/latency/gadget probes, `--bench-clock`), updater core
  (`pi/update`, UF2 + BOOTSEL).

Open: hardware acceptance items (see `docs/acceptance.md`); physical touch and
timing measurement; the brain-os image workstream is parked
([BUILDROOT-AUDIO.md](BUILDROOT-AUDIO.md)).

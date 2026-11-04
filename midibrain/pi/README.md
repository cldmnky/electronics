# mnkybox Pi DAW (`pi/`)

C++20 host software for the **mnkybox** recorder/MIDI hub: Raspberry Pi 5,
800×480 touch UI, Pico 2 as SPI link + clock master. Everything in this
directory also builds and runs **headless on the macOS dev machine**; nothing
here has been run on Pi hardware yet.

This is prototype software. A green build says nothing about audio latency,
MIDI/SysEx behaviour on real interfaces, the SPI link, power or the mechanical
stack. See [ARCHITECTURE.md](ARCHITECTURE.md) for the module contracts and
[../docs/](../docs/) for the hardware-side status.

## Layout and ownership

```
pi/
  CMakeLists.txt      root build (setup team): pins, RtMidi, core libs, CTest, JUCE smoke
  ARCHITECTURE.md     module contracts (parent-owned)
  third_party/        vendored RtMidi 6.1.0 + pinned-dependency table (parent-owned)
  link/               Pico SPI link core      (JUCE-free)
  midi/               router / USB MIDI / clock / buttons (JUCE-free, uses RtMidi)
  audio/              engine + multitrack float32 recorder (JUCE-free)
  clap/               CLAP host core + example mnkygain plugin (JUCE-free)
  app/                wave 2: JUCE touchscreen application
  emulator/           wave 2: emulator backends + headless selftest
```

One writer per directory. The root build never edits module code; modules never
edit the root build.

## Build and test (dev machine, macOS arm64)

Requires CMake ≥ 3.24, a C++20 compiler, `git` and network access for the first
configure (both dependencies are fetched shallow and pinned).

```sh
cmake -S midibrain/pi -B /tmp/pi-build
cmake --build /tmp/pi-build -j
ctest --test-dir /tmp/pi-build --output-on-failure
```

The first configure shallow-clones JUCE 9.0.5 and CLAP 1.2.10 into
`/tmp/pi-build/_deps` (~25–40 s) and builds JUCE's `juceaide` host tool.
A full `-j` build then takes roughly a minute on an M-series laptop; later
builds are incremental. No artifact from this directory is installed, packaged
or flashed.

Targets:

| Target | Kind | Notes |
|---|---|---|
| `mnky_rtmidi` | static lib | RtMidi 6.1.0 compiled **once**; `__MACOSX_CORE__` (macOS) / `__LINUX_ALSA__` (Pi) is a `PUBLIC` define so every consumer sees the same `RtMidi` layout |
| `mnky_link` `mnky_midi` `mnky_audio` `mnky_clap` | static libs | fixed names from ARCHITECTURE.md |
| `mnky_clap_headers` | interface | CLAP 1.2.10 `include/` (headers only; the upstream CLAP CMake project is deliberately not added) |
| `mnkygain` | loadable module | example CLAP gain plugin from `clap/effects/mnkygain/`, needs the real CLAP headers |
| `mnky_juce_smoke` | static lib | compile smoke: `juce_gui_basics` + `juce_audio_devices`. Compiles and links into a static archive, never runs. The wave-2 `app/` team owns the real `juce_add_gui_app` target |
| `test_link` `test_midi` `test_audio` (+ `clap_host_core`) | test binaries | registered with CTest |

### Ownership-tolerant wiring

Modules land in parallel, so configure never fails on a missing module:

* `mnky_core_module()` skips a module whose directory is absent, uses the
  module's own `CMakeLists.txt` when it has one, and otherwise globs
  `<dir>/*.cpp` (excluding `test_*.cpp`) into the fixed library name.
* `mnky_module_tests()` registers `<dir>/test_*.cpp` with CTest, and skips a
  test the module already defines itself (e.g. `test_clap`/`clap_host_core`).
  A module without tests simply adds none.
* `app/CMakeLists.txt` and `emulator/CMakeLists.txt` are added if present.

Test binaries are compiled with `-UNDEBUG` in every build type: module tests are
assert-based, and a `Release`/`RelWithDebInfo` `-DNDEBUG` would silently disable
every check.

Each module can also be built standalone exactly as documented in
ARCHITECTURE.md (`cmake -S midibrain/pi/<mod> -B …`, or the plain
`c++ -std=c++20 …` lines). Those standalone builds are the reference for the
module teams; the root build is what CI and the app link against.

## Pinned dependencies

Versions are single-sourced in `pi/CMakeLists.txt` (`MNKY_JUCE_VERSION`,
`MNKY_CLAP_VERSION`) and must stay identical to
[third_party/README.md](third_party/README.md). Never float on a branch.

Two macOS dev-machine facts are baked into the root build:

* **Apple clang is selected on Darwin** when the caller has not chosen a
  compiler (`CC`/`CXX` unset, no `-DCMAKE_CXX_COMPILER`). JUCE compiles against
  the macOS SDK frameworks (ApplicationServices/HarfBuzz, CoreMIDI), which a
  non-Apple `g++` on `PATH` does not have. Override with
  `-DCMAKE_CXX_COMPILER=…` if you know your toolchain can build JUCE.
  Standalone per-module tests still work with the plain `c++` on `PATH` — they
  are JUCE-free, but they are then a different C++ runtime from the CMake-built
  artifacts, so do not link objects from the two.
* **`-include cstring` for C++/ObjC++ on Apple.** Upstream JUCE 9.0.5 includes
  `<cstring>` in `juce_StandardHeader.h` only for Linux/BSD, Android and MSVC;
  current libc++ no longer pulls it in transitively, so `juce_core`'s
  `memset`/`memmove` fail to compile. The header is force-included instead of
  patching the pinned tree, and the same flag is passed to JUCE's configure-time
  `juceaide` bootstrap through `CXXFLAGS`. Both go away when a JUCE version that
  fixes the include is pinned.

## Pi notes

* The Pi 5 build is the same root build with `gcc`/`clang` and the system CMake;
  RtMidi is compiled with `__LINUX_ALSA__` and links `asound`, and JUCE's
  `audio_devices` uses ALSA. **This has not been built or run on a Pi yet** —
  the ARM/ALSA path is unverified.
* Display, touch (I2C), USB-C power, cooling and NVMe stack details are tracked
  in `../docs/mechanical-stack.md` and `../docs/requirements.md`; none of them
  are proven by this build.
* JUCE licence (GPLv3 vs commercial) is undecided, so no binary built here may
  be distributed yet.

## Status

Working today: root configure/build on macOS arm64, RtMidi compiled once, the
core module libraries and their CTest suites, the example CLAP gain plugin,
the JUCE touchscreen application (`mnkybox`, `--emulator` backends verified
running with link up and real CLAP load), and the headless `mnky_selftest`
(link/MIDI/record/CLAP/WAV, exit 0).

Not yet: any Pi hardware run, visual UI review on the 800x480 touch display,
and any audio/MIDI timing or latency measurement.

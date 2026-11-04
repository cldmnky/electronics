# mnkygain — mnkybox example CLAP effect

JUCE-free, single-file stereo effect: **gain → tanh drive → one-pole tone → dry/wet
mix**. It is the reference FX for the mnkybox sends and the emulator, and the
smoke test for `mnky::clap::Host`.

| id | name  | range            | default | notes                                  |
|----|-------|------------------|---------|----------------------------------------|
| 0  | Gain  | -24 … +24 dB     | 0 dB    | linear `10^(dB/20)`                    |
| 1  | Drive | 0 … 1            | 0       | pre-gain 1× … 10× into `tanh` (no AGC) |
| 2  | Tone  | 200 … 18000 Hz   | 8000 Hz | one-pole low-pass on the driven signal |
| 3  | Mix   | 0 … 1            | 1       | dry/wet, 1 = fully processed           |

Extensions: `clap.params`, `clap.audio-ports` (one main stereo in, one main stereo
out, in-place allowed), `clap.state` (magic `MKGA`, version 1, four doubles).
Parameter events are applied immediately (their `time` offset is ignored), which is
fine because the host latches parameter writes per block.

## Headers

The plugin must be built against the real CLAP headers (1.2.10; the directory that
contains `clap/clap.h`). The test-only stub under `../../test/clap_stub` is **not**
for plugin builds. Once the setup team's FetchContent lands, the CMake target
`mnkygain` builds automatically; the direct lines below always work.

## macOS: build the `.clap` bundle

```sh
CLAP_INC=/path/to/clap-1.2.10/include        # contains clap/clap.h
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fvisibility=hidden -I"$CLAP_INC" \
    -dynamiclib mnkygain.cpp -o mnkygain.clap/Contents/MacOS/mnkygain   # mkdir -p first
```

Bundle layout (the directory, not the Mach-O, is the unit a host loads; CLAP passes
the bundle path to `clap_entry.init()`):

```
mnkygain.clap/
  Contents/
    Info.plist            # CFBundleExecutable=mnkygain, CFBundleIdentifier=com.cldmnky.mnkygain,
                          # CFBundlePackageType=BNDL, CFBundleVersion=0.1.0
    MacOS/mnkygain        # Mach-O dylib exporting exactly `clap_entry`
```

Install for local testing with `~/Library/Audio/Plug-Ins/CLAP/mnkygain.clap` (or any
directory listed in `CLAP_PATH`). `-fvisibility=hidden` is required to export only
`clap_entry` (`nm -gU Contents/MacOS/mnkygain` must print nothing else).

## Linux (Pi 5): build the `.clap` file

```sh
CLAP_INC=/path/to/clap-1.2.10/include
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fPIC -fvisibility=hidden -shared \
    -I"$CLAP_INC" mnkygain.cpp -o mnkygain.clap
cp mnkygain.clap ~/.clap/            # or /usr/lib/clap
```

On Linux a `.clap` is the shared object itself; no bundle directory and no
`Info.plist`. The mnkybox host accepts either form (`Host::bundleBinaryPath()`).

## CMake

```sh
cmake -S .. -B build -DMNKY_CLAP_HEADER_DIR=/path/to/clap-1.2.10/include
cmake --build build
```

## Not implemented (deliberately)

No GUI, no latency reporting, no MIDI, no per-note modulation, no sample-accurate
automation, no drive auto-gain compensation, no oversampling — 2-channel blocks
only. Nothing here has been measured on Pi hardware yet.

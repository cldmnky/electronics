# Stage B — Debian arm64 guest provisioned, first Linux core build green

Scope of this stage: provision the stage-A VM, snapshot `midibrain/{pi,firmware}`
into the guest, configure the CMake project in-guest, build **only the core
targets**, and run the core CTest suite. No `mnkybox` (JUCE app) build, no GUI
run, no screenshots — that is stage C.

Result: **core build clean with GCC 12.2.0 under `-Wall -Wextra -Werror`, 5/5
core tests pass in the guest, emulator selftest `RESULT: PASS` with the real
`mnkygain` CLAP module loaded. No source changes were needed and none were made.**

## Guest environment (measured)

```
Linux mnkybox 6.1.0-54-arm64 #1 SMP Debian 6.1.190-1 (2026-10-16) aarch64 GNU/Linux
gcc (Debian 12.2.0-14+deb12u1) 12.2.0
cmake version 3.25.1
git version 2.39.5
1.8.1                                    # pkg-config
rsync  version 3.2.7  protocol version 32
```

| Item | Value |
| --- | --- |
| VM | stage-A VM, still running (pid file `vm.pid`), HVF, `-smp 8`, 4 GiB, 20 GiB disk |
| SSH | `mnky@localhost:2223`, key auth, `BatchMode=yes ConnectTimeout=10` |
| Installed packages after provisioning | 548 `ii` lines (`dpkg -l \| grep -c '^ii'`) |
| Disk | `/dev/vda1 20G, 2.4G used, 17G avail (13%)` |
| Build dir | `~/pi-build` 307 M (JUCE source 212 M, CLAP source 6.7 M) |
| Host/guest clock | agree (`Wed Nov  4 02:36 UTC 2026` on both) |

## 1. Provisioning

One `apt-get update`, then **one** `apt-get install` (the briefed package list,
plus `rsync` — see deviations). `/var/log/apt/history.log`:

```
Start-Date: 2026-11-04  02:30:35
Commandline: apt-get install -y build-essential cmake git pkg-config libasound2-dev alsa-utils
  libjack-jackd2-dev libfreetype6-dev libfontconfig1-dev libx11-dev libxcomposite-dev
  libxcursor-dev libxext-dev libxinerama-dev libxrandr-dev libxrender-dev libgl1-mesa-dev
  libcurl4-openssl-dev xvfb x11-apps x11-utils
End-Date: 2026-11-04  02:30:45
```

224 packages configured (`grep -c '^Setting up' /tmp/apt-install.log`), no `E:` or
`W:` lines in the log, 10 s wall clock.

Installed versions (`dpkg -l` excerpt, trimmed to the requested packages):

```
ii  alsa-utils                 1.2.8-1               arm64  Utilities for configuring and using ALSA
ii  build-essential            12.9                  arm64  Informational list of build-essential packages
ii  cmake                      3.25.1-1              arm64  cross-platform, open-source make system
ii  git                        1:2.39.5-0+deb12u3    arm64  fast, scalable, distributed revision control system
ii  libasound2-dev:arm64       1.2.8-1+b1            arm64  shared library for ALSA applications -- development files
ii  libcurl4-openssl-dev:arm64 7.88.1-10+deb12u15    arm64  development files and documentation for libcurl (OpenSSL flavour)
ii  libfontconfig1-dev:arm64   2.14.1-4              arm64  generic font configuration library - dummy package
ii  libfreetype6-dev:arm64     2.12.1+dfsg-5+deb12u4 arm64  FreeType 2 font engine, development files (transitional package)
ii  libgl1-mesa-dev:arm64      22.3.6-1+deb12u2      arm64  transitional dummy package
ii  libjack-jackd2-dev:arm64   1.9.21~dfsg-3         arm64  JACK Audio Connection Kit (development files)
ii  libx11-dev:arm64           2:1.8.4-2+deb12u2     arm64  X11 client-side library (development headers)
ii  libxcomposite-dev:arm64    1:0.4.5-1             arm64  X11 Composite extension library (development headers)
ii  libxcursor-dev:arm64       1:1.2.1-1             arm64  X cursor management library (development files)
ii  libxext-dev:arm64          1:1.3.4-1+b1          arm64  X11 miscellaneous extensions library (development headers)
ii  libxinerama-dev:arm64      1:1.1.4-3             arm64  X11 Xinerama extension library (development headers)
ii  libxrandr-dev:arm64        1:1.5.2-2+b1          arm64  X11 RandR extension library (development headers)
ii  libxrender-dev:arm64       1:0.9.10-1.1          arm64  X Rendering Extension client library (development files)
ii  pkg-config:arm64           1.8.1-1               arm64  manage compile and link flags for libraries (transitional package)
ii  x11-apps                   7.7+9                 arm64  X applications
ii  x11-utils                  7.7+5                 arm64  X11 utilities
ii  xvfb                       2:21.1.7-3+deb12u13   arm64  Virtual Framebuffer 'fake' X server
ii  rsync                      3.2.7-1+deb12u6       arm64  fast, versatile, remote (and local) file-copying tool
```

Development headers that JUCE 9.0.5 will need in stage C are present:
`/usr/include/alsa/asoundlib.h`, `/usr/include/fontconfig/fontconfig.h`,
`/usr/include/jack/jack.h`; configure's own probes agree:

```
-- Found PkgConfig: /usr/bin/pkg-config (found version "1.8.1")
-- Checking for module 'alsa'
--   Found alsa, version 1.2.8
-- Checking for modules 'freetype2;fontconfig'
--   Found freetype2, version 24.3.18
```

### ALSA: `snd-dummy` is not in this kernel

```
$ sudo -n modprobe snd-dummy
modprobe: FATAL: Module snd-dummy not found in directory /lib/modules/6.1.0-54-arm64
$ aplay -l
aplay: device_list:274: no soundcards found...
$ cat /proc/asound/cards
cat: /proc/asound/cards: No such file or directory
```

Recorded, not worked around — sound devices are stage C. The cause is the Debian
arm64 cloud kernel config, not a missing package:

```
# CONFIG_SND_DUMMY is not set
CONFIG_SND_ALOOP=m          # snd-aloop.ko present in /lib/modules/6.1.0-54-arm64
CONFIG_SND_SEQUENCER=m      # snd-seq.ko / snd-seq-dummy.ko also present
```

So stage C can get an ALSA card with `snd-aloop` (loopback PCM) and a sequencer
client for RtMidi's `__LINUX_ALSA__` backend with `snd-seq` + `snd-seq-dummy`;
`snd-dummy` is simply unavailable on this image.

### Xvfb

`provision.sh` starts `Xvfb :99 -screen 0 1024x600x24 -nolisten tcp` (idempotent:
skipped if an `Xvfb` is already running). It is running now and answers:

```
$ DISPLAY=:99 xdpyinfo
version number:    11.0
vendor string:     The X.Org Foundation
vendor release number:    12101007
X.Org version: 1.21.1.7
```

No X client, no GUI app and no screenshot was started in this stage.

## 2. Repo snapshot into the guest

Everything the CMake build needs: `pi/` (root CMakeLists, `third_party/rtmidi`,
all module dirs, `cmake/`, app + emulator CMakeLists) and `firmware/` (the
`protocol.hpp` / `midi.hpp` codecs are included by `pi/link` and `pi/midi` via
`../../firmware/pico/...`).

```sh
cd midibrain
rsync -a --delete --exclude qemu/ --exclude build/ --exclude node_modules/ \
      --exclude dist/ --exclude fab/ --exclude __pycache__/ --exclude '*.o' \
      --exclude .git/ \
      -e "ssh -p 2223 -o BatchMode=yes -o ConnectTimeout=10 \
          -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null" \
      pi/ mnky@localhost:midibrain/pi/
rsync -a --delete --exclude build/ ... firmware/ mnky@localhost:midibrain/firmware/
```

Exclusion rationale: `pi/qemu/` holds the 433 MiB disk image and seed ISO;
`firmware/pico/build/` is a 26 MiB host Pico-SDK build tree. Neither belongs in
the guest.

| Item | Bytes | Wall clock |
| --- | --- | --- |
| `pi/` transferred (`Total file size`) | 803,796 B (sent 811,869 B) | 0.216 s |
| `firmware/` transferred | 53,316 B (sent 54,227 B) | 0.202 s |
| Guest `du -sb ~/midibrain/pi` | 869,332 B | — |
| Guest `du -sb ~/midibrain/firmware` | 61,508 B | — |

## 3. Configure

```sh
ssh -p 2223 ... mnky@localhost
cmake -S ~/midibrain/pi -B ~/pi-build
```

Took ~45 s wall clock, dominated by the in-guest FetchContent clones
(JUCE 9.0.5, 91.74 MiB pack; CLAP 1.2.10, 2.42 MiB). Verbatim tail (`\r`
progress spam removed):

```
HEAD is now at d60b7a780 JUCE version 9.0.5
[100%] Completed 'juce-populate'
[100%] Built target juce-populate
-- Performing Test CMAKE_HAVE_LIBC_PTHREAD - Success
-- Found Threads: TRUE
HEAD is now at 195b42a CLAP 1.2.10
[100%] Completed 'clap-populate'
[100%] Built target clap-populate
-- midibrain/clap: using CLAP headers from /home/mnky/pi-build/_deps/clap-src/include
-- mnkybox: pi/clap/ defines test_clap itself — not re-registering it
-- mnkybox app: default FX plugin $<TARGET_FILE:mnkygain>
-- mnkybox: JUCE 9.0.5 (/home/mnky/pi-build/_deps/juce-src), CLAP 1.2.10 (headers) configured for Linux with GNU
-- Configuring done
-- Generating done
-- Build files have been written to: /home/mnky/pi-build
EXIT=0
```

Default build type from the project is `RelWithDebInfo`; the app target was
configured by the root CMakeLists (it owns `app/CMakeLists.txt`) but was **not
built**.

## 4. Build — core targets only

```sh
timeout 500 cmake --build ~/pi-build --target \
  test_link test_midi test_audio test_clap mnky_selftest mnkygain -j8
```

Verbatim log tail:

```
[  0%] Building CXX object link/CMakeFiles/mnky_link.dir/link.cpp.o
[ 33%] Linking CXX static library libmnky_link.a
[ 33%] Built target mnky_link
[ 66%] Building CXX object CMakeFiles/test_link.dir/link/test_link.cpp.o
[100%] Linking CXX executable test_link
[100%] Built target test_link
[ 12%] Building CXX object CMakeFiles/mnky_rtmidi.dir/third_party/rtmidi/RtMidi.cpp.o
[ 25%] Linking CXX static library libmnky_rtmidi.a
[ 25%] Built target mnky_rtmidi
[ 37%] Building CXX object midi/CMakeFiles/mnky_midi.dir/router.cpp.o
[ 50%] Building CXX object midi/CMakeFiles/mnky_midi.dir/clock.cpp.o
[ 62%] Building CXX object midi/CMakeFiles/mnky_midi.dir/usb.cpp.o
[ 75%] Linking CXX static library libmnky_midi.a
[ 75%] Built target mnky_midi
[ 87%] Building CXX object CMakeFiles/test_midi.dir/midi/test_midi.cpp.o
[100%] Linking CXX executable test_midi
[100%] Built target test_midi
[  0%] Building CXX object audio/CMakeFiles/mnky_audio.dir/engine.cpp.o
[ 25%] Building CXX object audio/CMakeFiles/mnky_audio.dir/recorder.cpp.o
[ 50%] Linking CXX static library libmnky_audio.a
[ 50%] Built target mnky_audio
[ 75%] Building CXX object CMakeFiles/test_audio.dir/audio/test_audio.cpp.o
[100%] Linking CXX executable test_audio
[100%] Built target test_audio
[ 33%] Built target mnky_audio
[ 50%] Building CXX object clap/CMakeFiles/mnky_clap.dir/host.cpp.o
[ 66%] Linking CXX static library libmnky_clap.a
[ 66%] Built target mnky_clap
[ 83%] Building CXX object clap/CMakeFiles/test_clap.dir/test_clap.cpp.o
[100%] Linking CXX executable test_clap
[100%] Built target test_clap
[ 14%] Built target mnky_audio
[ 21%] Built target mnky_link
[ 35%] Built target mnky_rtmidi
[ 57%] Built target mnky_clap
[ 78%] Built target mnky_midi
[ 85%] Building CXX object emulator/CMakeFiles/mnky_selftest.dir/selftest.cpp.o
[ 85%] Building CXX object emulator/CMakeFiles/mnky_selftest.dir/scenario.cpp.o
[ 92%] Building CXX object emulator/CMakeFiles/mnky_selftest.dir/synth.cpp.o
[100%] Linking CXX executable mnky_selftest
[100%] Built target mnky_selftest
[  0%] Building CXX object clap/CMakeFiles/mnkygain.dir/effects/mnkygain/mnkygain.cpp.o
[100%] Linking CXX shared module mnkygain.so
[100%] Built target mnkygain
EXIT=0
```

`grep -E 'error:|warning:'` over the whole log: **no matches**. The core modules
are compiled with `-Wall -Wextra -Werror` (root `MNKY_WARNINGS`), so GCC 12 vs
the macOS clang baseline produced **zero** diagnostics. Build wall clock ~40 s
with `-j8`, no swap needed.

Binaries: `~/pi-build/{test_link,test_midi,test_audio}`, `~/pi-build/clap/test_clap`,
`~/pi-build/clap/mnkygain.so`, `~/pi-build/emulator/mnky_selftest`.

## 5. Tests

Registered tests (`ctest -N`) — note the real names differ from the target names:

```
  Test #1: test_link
  Test #2: test_midi
  Test #3: test_audio
  Test #4: clap_host_core
  Test #5: mnkybox_app_logic
  Test #6: emulator_selftest
Total Tests: 6
```

`-R 'link|midi|audio|clap|selftest'` therefore selects 1–4 and 6, and skips #5
(`mnkybox_app_logic`, the app target's test — stage C). `ctest -N` also warns
that `app/mnkybox_artefacts/.../mnkybox` does not exist yet; that is only the app
test's COMMAND being resolved, not a failure.

```
$ ctest --test-dir ~/pi-build -R 'link|midi|audio|clap|selftest' --output-on-failure
Internal ctest changing into directory: /home/mnky/pi-build
Test project /home/mnky/pi-build
    Start 1: test_link
1/5 Test #1: test_link ........................   Passed    0.58 sec
    Start 2: test_midi
2/5 Test #2: test_midi ........................   Passed    0.01 sec
    Start 3: test_audio
3/5 Test #3: test_audio .......................   Passed    0.01 sec
    Start 4: clap_host_core
4/5 Test #4: clap_host_core ...................   Passed    0.00 sec
    Start 6: emulator_selftest
5/5 Test #6: emulator_selftest ................   Passed    6.29 sec

100% tests passed, 0 tests failed out of 5

Total Test time (real) =   6.89 sec
```

### Emulator selftest, in the guest

Bare (what CTest runs — the CLAP stage SKIPs without a module path):

```
$ ~/pi-build/emulator/mnky_selftest
mnkybox emulator selftest (headless, no display, no JUCE)
clap module: (none supplied)
PASS  link-handshake  link up after 3 exchanges, session 0x5049434f, 279 ticks over 5.01 s, tempo echo 140.00 bpm, stats tx=5010 rx=5010 dup=4721 bad=0
PASS  midi-loopback   note-on 90 3C 64: TRS IN 1 -> TRS OUT 1 (MIDI_SEND port 0) + USB OUT, channel-2 note filtered, 0xF8 passed
PASS  record-take     3 WAVs, 96000 frames at 48000 Hz, 0 drops, arms 0x1 + 0x2, /tmp/mnkybox-selftest.oK43lM/take-20261104T023552Z
SKIP  fx-clap         no CLAP module path (pass argv[1] or MNKY_SELFTEST_CLAP_PLUGIN); stub-gain 0.25 measured 0.250 (master rms 0.303 vs 0.484 send-only reference)
PASS  wav-verify      3 float32 WAVs, 96000 frames: ch00 rms 0.354 with 1759 crossings (~440 Hz), ch01 rms 0.116 with 47967 crossings, master L/R rms 0.211/0.211
5 stages: 4 passed, 0 failed, 1 skipped
RESULT: PASS
```

Same binary with the just-built Linux plugin (manual run, proves the
`mnkygain.so` target actually loads under `dlopen` on Linux):

```
$ ~/pi-build/emulator/mnky_selftest ~/pi-build/clap/mnkygain.so
mnkybox emulator selftest (headless, no display, no JUCE)
clap module: /home/mnky/pi-build/clap/mnkygain.so
PASS  link-handshake  link up after 3 exchanges, session 0x5049434f, 279 ticks over 5.01 s, tempo echo 140.00 bpm, stats tx=5010 rx=5010 dup=4721 bad=0
PASS  midi-loopback   note-on 90 3C 64: TRS IN 1 -> TRS OUT 1 (MIDI_SEND port 0) + USB OUT, channel-2 note filtered, 0xF8 passed
PASS  record-take     3 WAVs, 96000 frames at 48000 Hz, 0 drops, arms 0x1 + 0x2, /tmp/mnkybox-selftest.r3a78p/take-20261104T023604Z
PASS  fx-clap         mnkygain com.cldmnky.mnkygain (4 params): gain +12 dB -> 2.204x, -24 dB -> 0.063x, Engine master rms 0.922 vs 0.484 send-only reference
PASS  wav-verify      3 float32 WAVs, 96000 frames: ch00 rms 0.354 with 1759 crossings (~440 Hz), ch01 rms 0.116 with 47967 crossings, master L/R rms 0.211/0.211
5 stages: 5 passed, 0 failed, 0 skipped
RESULT: PASS
```

Consequence for stage C: configure with
`-DMNKY_SELFTEST_CLAP_PLUGIN=$HOME/pi-build/clap/mnkygain.so` (or pass the path
as `argv[1]`) so the CTest entry exercises `fx-clap` instead of skipping it.

## 6. Fixes proposed

**None.** The core modules needed no Linux-only adjustment: no missing standard
includes, no platform-define slips and no `-Werror` diagnostics from GCC 12.2.0.
No file outside `midibrain/pi/qemu/` was created, edited or moved, and no host
tree file was touched.

## 7. Open risks

1. **No ALSA card in this guest.** `# CONFIG_SND_DUMMY is not set`; `aplay -l`
   finds nothing and `/proc/asound/cards` does not exist. Every core test here is
   device-independent, so nothing was masked, but stage C must either use
   `snd-aloop` (`CONFIG_SND_ALOOP=m`) or accept that no hardware PCM exists. No
   audio path has been validated against a real or emulated device.
2. **RtMidi ALSA backend is compiled, not exercised.** `RtMidi.cpp` is built with
   `__LINUX_ALSA__` and links `libasound`, but no test opens a sequencer client.
   `CONFIG_SND_SEQUENCER=m` and `CONFIG_SND_SEQ_DUMMY=m` exist, so stage C should
   `modprobe snd-seq snd-seq-dummy` and check `/dev/snd/seq` before promising
   MIDI port enumeration on the Pi.
3. **No swap, 4 GiB RAM.** The core build peaked well inside that, but the JUCE
   app is one very large unity translation unit; stage C may need a guest
   swapfile (stage A already flagged this).
4. **`mnkybox_app_logic` (Test #5) was neither built nor run** — deliberately out
   of stage-B scope. `ctest` alone (no `-R`) would report it as failing to find
   the binary until the app is built.
5. **In-guest FetchContent clones cost ~250 MiB and ~45 s per fresh build dir.**
   They are shallow and pinned (`JUCE 9.0.5`, `CLAP 1.2.10`), but a squashed
   `~/pi-build` is worth keeping between stages rather than reconfiguring into a
   new directory.
6. **Disk snapshot state.** Stage A boots a writable qcow2 with no overlay, so
   the provisioning above now lives in `debian.qcow2`. There is no way to roll
   back to the stock image without re-downloading it; a `qemu-img` backing-file
   overlay before stage C would restore that option.
7. **Xvfb is running from this stage onward** (pid owned by the `mnky` user,
   log `/tmp/xvfb.log`). It is a display server only — no client has connected —
   but a later stage should confirm it is still alive before screenshot work.

## 8. Files and reproduction

- `provision.sh` — keeper: the single `apt-get install` line, `modprobe
  snd-dummy` (expected to fail on this kernel and recorded as such), Xvfb on
  `:99`, and a versions header. Idempotent; re-run measured at 2.45 s with a
  no-op apt. It made no repo build.
- `STAGE-B.md` — this file.
- Guest artifacts (not in the repo): `~/midibrain/{pi,firmware}`,
  `~/pi-build` (307 M), `/tmp/{apt-update,apt-install,configure,build}.log`.

Full sequence: `run-vm.sh` → `provision.sh` → rsync (step 2) → `cmake -S … -B …`
→ `cmake --build … --target <core>` → `ctest -R '…'`.

Wall clock for the measured run (guest clock, `/var/log/apt/history.log` +
`date`): apt start 02:30:35, rsync 02:31:5x, configure ~02:32–02:33, build
~02:33–02:34, ctest 02:34–02:35, selftest runs 02:35:52 and 02:36:04 — about
**6 minutes of guest work** end to end, within the staged time budget.

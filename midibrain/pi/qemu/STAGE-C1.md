# Stage C1 — Linux app build in the Debian arm64 guest (JUCE compiled for Linux/X11)

Scope of this stage: provision what stage B left out for GUI work, refresh the
guest tree, configure a fresh ccache build directory, get the ALSA/MIDI devices
and Xvfb up, prove the `xwd` screenshot pipe without the app, and **build and
link the `mnkybox` app target** (plus `mnkygain` and `mnky_selftest`) in the
guest. No app launch, no app screenshot, no recording — that is stage C2.

**Result: `mnkybox` linked in the guest (ELF aarch64, 124,592,120 bytes,
`EXIT=0`), the screenshot pipe is proven, and one Linux-only build fix is
required in `pi/app/CMakeLists.txt` (applied in the guest only — the host tree was
not touched; see §6).**

## Guest environment (measured)

```
Linux mnkybox 6.1.0-54-arm64 #1 SMP Debian 6.1.190-1 (2026-10-16) aarch64 GNU/Linux
8 CPUs, 3920 MiB RAM, 20 GiB disk (/dev/vda1, 7.2 G used / 12 G avail after this stage)
```

VM from `STAGE-A.md` was already running (pid in `vm.pid`, HVF); it answered SSH
on the first probe at `2026-11-04 02:39:04 UTC`, so `run-vm.sh` was not needed.

| Guest delta added by this stage | Version | Why |
| --- | --- | --- |
| `ccache` | 4.8+really4.7.5-1 | compiler cache for the fix loop (`-DCMAKE_{C,CXX}_COMPILER_LAUNCHER=ccache`) |
| `xdotool` | 1:3.20160805.1-5 | stage C2 input injection |
| `netpbm` | 2:11.01.00-2 | `xwdtopnm`/`pnmtojpeg` for the screenshot pipe |
| `libxi-dev` | 2:1.8-1+b1 | **build blocker** — `juce_gui_basics.h:394` includes `<X11/extensions/XInput2.h>` (JUCE defaults `JUCE_USE_XINPUT=1` on Linux); stage B's package list has no XInput2 headers |
| group `audio` membership for `mnky` | — | `/dev/snd/*` is `root:audio 0660`; see §2 |
| `/swapfile`, 4 GiB | — | `-j8` cc1plus was OOM-killed; see §5 |
| modules `snd-aloop`, `snd-seq-dummy` | kernel 6.1.0-54 | ALSA card + sequencer in this kernel (no `snd-dummy`) |
| Xvfb `:99` | 21.1.7 | already running from stage B (pid 5671); pidfile method below |

All of it is scripted, idempotent, in `provision-c.sh` (re-run measured: **3.35 s**,
no-op except verification output). Modules, swapfile and the group change are
session-only on purpose (no `/etc/modules`, no `/etc/fstab`) — a guest reboot
needs `provision-c.sh` again.

## 1. Repo refresh into the guest

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

Both trees were byte-identical to stage B (`Number of files transferred: 0`,
`pi/` 803,796 B, `firmware/` 53,316 B), 0.20 s and 0.21 s. No host file changed.

## 2. ALSA / MIDI device status

`sudo modprobe snd-aloop; sudo modprobe snd-seq-dummy` → both `EXIT=0`.

```
$ cat /proc/asound/cards
 0 [Loopback       ]: Loopback - Loopback
                      Loopback 1
$ aplay -l
**** List of PLAYBACK Hardware Devices ****
card 0: Loopback [Loopback], device 0: Loopback PCM [Loopback PCM]
  Subdevices: 8/8
  Subdevice #0: subdevice #0            # ... 8 subdevices, then device 1 identically
$ lsmod | grep -E '^snd'
snd_seq_dummy          16384  0
snd_seq                90112  1 snd_seq_dummy
snd_seq_device         16384  1 snd_seq
snd_aloop              32768  0
snd_pcm               122880  1 snd_aloop
snd_timer              40960  3 snd_seq,snd_aloop,snd_pcm
snd                    98304  5 snd_seq,snd_seq_device,snd_timer,snd_aloop,snd_pcm
soundcore              16384  1 snd
$ aconnect -l
client 0: 'System' [type=kernel]
    0 'Timer           '
    1 'Announce        '
client 14: 'Midi Through' [type=kernel]
    0 'Midi Through Port-0'
```

**Permission trap found and fixed.** With the modules loaded but `mnky` outside
group `audio`, both tools fail while `/proc/asound` looks healthy:

```
$ aplay -l
aplay: device_list:274: no soundcards found...
$ aconnect -l
ALSA lib seq_hw.c:466:(snd_hw_open) open /dev/snd/seq failed: Permission denied
can't open sequencer
$ ls -l /dev/snd
crw-rw---- 1 root audio 116,  1 Nov  4 02:40 seq
crw-rw---- 1 root audio 116,  6 Nov  4 02:40 controlC0
...
```

Cause: `aplay -l` enumerates by opening `/dev/snd/controlC*`, so a permission
failure is reported as "no soundcards found" — not as a permission error.
`sudo usermod -aG audio mnky` (effective on the next SSH session) fixes both; the
outputs above are from the fixed session. This also matters for RtMidi's
`__LINUX_ALSA__` backend and for `audio::Engine`'s ALSA PCM, i.e. for stage C2.

## 3. Configure — fresh ccache build dir

```sh
cmake -S ~/midibrain/pi -B ~/pi-build-ccache \
      -DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DCMAKE_C_COMPILER_LAUNCHER=ccache
```

`EXIT=0` at `02:40:56 UTC`, ~30 s (FetchContent re-cloned JUCE 9.0.5 and CLAP
1.2.10 into the new dir; log `/tmp/configure-ccache.log`). Verbatim tail:

```
-- midibrain/clap: using CLAP headers from /home/mnky/pi-build-ccache/_deps/clap-src/include
-- mnkybox: pi/clap/ defines test_clap itself — not re-registering it
-- mnkybox app: default FX plugin $<TARGET_FILE:mnkygain>
-- mnkybox: JUCE 9.0.5 (/home/mnky/pi-build-ccache/_deps/juce-src), CLAP 1.2.10 (headers) configured for Linux with GNU
-- Configuring done
-- Generating done
-- Build files have been written to: /home/mnky/pi-build-ccache
EXIT=0
```

JUCE's configure probes here: `alsa 1.2.8`, `freetype2 24.3.18`, `fontconfig 2.14.1`,
`egl 1.5`, `gl 1.2`, `glesv2 3.2`, `libcurl 7.88.1`; `webkit2gtk-4.0` and
`gtk+-x11-3.0` absent — expected, the app builds with `NEEDS_WEB_BROWSER FALSE` /
`JUCE_WEB_BROWSER=0`. Build dir 666 M, ccache cache 108 M.

## 4. Build — `mnkybox`, `mnkygain`, `mnky_selftest`

Final, successful command (run detached so the SSH session cannot time out; log
`/tmp/build-app.log`, `EXIT` line appended by the wrapper):

```sh
timeout 1500 nice -n 5 cmake --build ~/pi-build-ccache \
                       --target mnkybox mnkygain mnky_selftest -j4
```

```
[  1%] Built target mnkygain
[  5%] Built target mnky_rtmidi
[  8%] Built target mnky_audio
[ 10%] Built target mnky_link
[ 13%] Built target mnky_clap
[ 20%] Built target mnky_midi
...  (juce_core/events/graphics/gui_basics/data_structures/audio_basics/
      audio_devices/audio_formats unity objects, opus, harfbuzz, sheenbidi, libjpg)
/home/mnky/pi-build-ccache/_deps/juce-src/.../arm/NSQ_del_dec_neon_intr.c:424:55:
    warning: iteration 80 invokes undefined behavior [-Waggressive-loop-optimizations]
[100%] Linking CXX executable mnkybox_artefacts/RelWithDebInfo/mnkybox
[100%] Built target mnkybox
[100%] Built target mnkygain
[100%] Linking CXX executable mnky_selftest
[100%] Built target mnky_selftest
EXIT=0
Wed Nov  4 02:51:32 UTC 2026
```

Started `02:50:21`, finished `02:51:32` → **71 s** for all 15 app TUs + ~24 JUCE
module TUs at `-j4`. Exactly **one** diagnostic in the whole log: the vendored
Opus line above, inside JUCE's own tree (a warning, not an error). Zero
diagnostics from `pi/` sources.

### Attempts before it (why they failed, what fixed each)

| # | Start → end (UTC) | Command | Failure |
| --- | --- | --- | --- |
| 1 | 02:41:36 → 02:41:43 | `-j8` | `juce_gui_basics.h:394: fatal error: X11/extensions/XInput2.h: No such file or directory` → installed `libxi-dev` |
| 2 | 02:42:17 → 02:45:01 | `-j8` | guest OOM killer, 5 objects (`juce_graphics.cpp`, `juce_graphics_Harfbuzz.cpp`, `juce_gui_basics.cpp`, `_3`, `_4`): `c++: fatal error: Killed signal terminated program cc1plus` — dmesg: `Out of memory: Killed process 7994 (cc1plus) total-vm:1238088kB, anon-rss:1184556kB` → 4 GiB `/swapfile` + `-j4` |
| 3 | 02:47:03 → 02:48:07 | `-j4` | `-Werror` on JUCE's vendored third-party code: `opus/silk/NLSF2A.c:54: 'cos_LSF_QA' may be used uninitialized [-Werror=maybe-uninitialized]`, `opusfile.c:1767`, `NSQ_del_dec_neon_intr.c:424 [-Werror=aggressive-loop-optimizations]`, `harfbuzz/hb-meta.hh`, `hb-algs.hh` → §6 fix |
| 4 | 02:50:21 → 02:51:32 | `-j4` | none, `EXIT=0` |

Attempt 2 also proves the memory ceiling independently of ccache: peak per
cc1plus is 0.8–1.2 GB VM, so `-j8` needs >4 GiB; `-j4` + 4 GiB swap is stable
(`Swap: 40 MiB used` at peak, i.e. it fits in RAM with headroom).

### Artifacts

| Path (in guest) | Bytes | SHA-256 | Notes |
| --- | --- | --- | --- |
| `~/pi-build-ccache/app/mnkybox_artefacts/RelWithDebInfo/mnkybox` | 124,592,120 | `10f8dc6690ea3847e8a74a2e551e0c704cde38eff5974caba91016bcfd07c098` | ELF 64-bit LSB pie executable, ARM aarch64, `with debug_info, not stripped`, `.comment: GCC (Debian 12.2.0-14+deb12u1) 12.2.0` |
| `~/pi-build-ccache/clap/mnkygain.so` | — | `33f4e2a026a90a706d505d04c4d62b94ddc3ba9b1867cb482c6ae70e611af6b0` | CLAP plugin, needed by C2 |
| `~/pi-build-ccache/emulator/mnky_selftest` | — | `a2dab00a24949d16583a845e0684c6eb6937cae5f510362377b8cff53fdb96fd` | headless emulator selftest, needed by C2 |

`ldd` resolves completely (0 `not found`):

```
libasound.so.2, libfontconfig.so.1, libfreetype.so.6, libpng16.so.16,
libz.so.1, libbrotlicommon.so.1 / libbrotlidec.so.1, libexpat.so.1,
libstdc++.so.6, libgcc_s.so.1, libm.so.6, libc.so.6, linux-vdso
```

Note for C2: **`libX11`, `libXrandr`, `libXinerama`, `libXcursor`, `libXcomposite`,
`libXext`, `libXrender` and `libXi` do not appear in `ldd`** — JUCE 9 loads the X11
client libraries at run time through `juce_XSymbols_linux`
(`dlopen`), so the link step cannot prove they are present. All of them are
installed in the guest (`libx11-dev` & co. from stage B, `libxi-dev` now), but
their absence or an Xvfb extension gap will only surface when the app starts —
that is the first thing C2 should look at if window creation fails.

## 5. Screenshot pipe proven without the app

Xvfb was already up from stage B; the pidfile method used from now on:

```sh
# start (idempotent), then record the pid
pgrep -x Xvfb >/dev/null || {
  nohup Xvfb :99 -screen 0 1024x600x24 -nolisten tcp >/tmp/xvfb.log 2>&1 &
  echo $! > /tmp/xvfb.pid
}
pgrep -x Xvfb > /tmp/xvfb.pid     # 5671 Xvfb :99 -screen 0 1024x600x24 -nolisten tcp
DISPLAY=:99 xdpyinfo | head -3    # name of display: :99 / version number: 11.0
```

The pipe, with `pipefail`, no app involved:

```sh
DISPLAY=:99 xwd -root -silent | xwdtopnm | pnmtojpeg > /tmp/xroot.jpg
# xwdtopnm: writing PPM file ; pipeline_exit=0 ; 02:40:27 UTC
$ ls -l /tmp/xroot.jpg
-rw-r--r-- 1 mnky mnky 10355 Nov  4 02:40 /tmp/xroot.jpg
$ file /tmp/xroot.jpg
JPEG image data, JFIF standard 1.01, ... baseline, precision 8, 1024x600, components 3
```

Saved verbatim as `screenshots/00-xroot.jpg` (10,355 bytes, 1024x600 JPEG, the
greyscale X root window,
SHA-256 `c022b4e6662ace8bcec653efe6c7d623a52aeeab02f3edd712de224fca733bb1`). `xwdtopnm` → `P6 1024 600` also confirmed on the PPM
stream directly. `xdotool` and `netpbm` are installed but unused until C2.

## 6. Fix proposed (one file, Linux-only)

`pi/app/CMakeLists.txt` applies `-Wall -Wextra -Werror` to the **whole target**,
and `juce_add_gui_app` compiles the JUCE module sources (including vendored
HarfBuzz, Opus, SheenBidi, libjpeg) into that same target. The comment there
claims the flags "only judge the app's own sources" — true for the app's own
`.cpp`s, false for the JUCE module TUs. clang on macOS does not emit GCC's
`-Wmaybe-uninitialized` / `-Waggressive-loop-optimizations`, so the macOS build
never showed the problem; GCC 12.2.0 turns it into 6 hard errors in code the
project does not own.

Fix: keep the standard, scope it to `MNKY_APP_SOURCES`. Diff (against
`pi/app/CMakeLists.txt` at `main`, 2026-11-04):

```diff
@@ line 26
-target_sources(mnkybox PRIVATE
+# The app's own translation units, kept in a variable so the warning standard
+# below can be applied to them alone (juce_add_gui_app compiles the JUCE modules
+# into this same target).
+set(MNKY_APP_SOURCES
   main.cpp
   MainComponent.cpp
   ...
   screens/SystemScreen.cpp)
 
+target_sources(mnkybox PRIVATE ${MNKY_APP_SOURCES})
+
@@ line 74
-# The ARCHITECTURE.md acceptance standard: zero warnings. JUCE's module headers
-# are consumed as system headers by the juce:: targets, so this only judges the
-# app's own sources.
-target_compile_options(mnkybox PRIVATE -Wall -Wextra -Werror)
+# The ARCHITECTURE.md acceptance standard: zero warnings, judged on the app's own
+# sources only. juce_add_gui_app builds the JUCE module sources as part of this
+# target, and their vendored third-party code (HarfBuzz, Opus) trips GCC-only
+# -Wextra diagnostics (-Wmaybe-uninitialized, -Waggressive-loop-optimizations)
+# that clang on macOS does not emit. A target-wide -Werror would fail the Linux
+# build on code the project does not own, so scope the standard to MNKY_APP_SOURCES.
+set_source_files_properties(${MNKY_APP_SOURCES} PROPERTIES
+  COMPILE_OPTIONS "-Wall;-Wextra;-Werror")
```

Lines touched: 26 (list becomes `set()`), 42–45 (new `target_sources` call),
74–77 (comment + `-Werror` scoping). No behavior change, no `pi/` source change,
no JUCE change, nothing outside `app/CMakeLists.txt`.

Status: **applied in the guest tree only** (`~/midibrain/pi/app/CMakeLists.txt`),
which is what the successful build in §4 was verified against. The host tree is
untouched (`git status` clean apart from the new `pi/qemu/` files listed in §9) —
the parent applies it. Consequence: **the next `rsync -a --delete` from the host
will silently revert the guest patch**, so C2 must apply it to the host tree (or
re-apply the patch in the guest) before building again.

Not needed / not changed: `mnky_*` core modules, `emulator/`, `cmake/`, JUCE
itself, and no `-Wno-*` suppressions were introduced.

## 7. Wall clock

Guest UTC, measured (configure ran detached, so the device/screenshot
verification steps overlap it):

| Time | Step |
| --- | --- |
| 02:39:04 | SSH verified, VM already up |
| ~02:39:1x | rsync `pi/` + `firmware/` (0 files changed, 0.4 s total) |
| 02:39:58–~02:40:2x | `apt-get install ccache xdotool netpbm` (34 s host-measured) |
| ~02:40:2x–02:40:56 | fresh ccache configure, `EXIT=0` (~30 s, detached) |
| ~02:40:0x–~02:40:2x | modules, group fix, `aplay -l`, `aconnect -l` |
| 02:40:27 | `/tmp/xroot.jpg` written (pipe proven) |
| 02:41:36–02:51:32 | 4 build attempts (7 s / 2 m 44 s / 1 m 4 s / 1 m 11 s) |
| ~02:52:4x | `provision-c.sh` re-run for idempotency (3.35 s) |

≈ **12.5 minutes** guest wall clock from first SSH to linked app, of which 5 m 6 s
is compiling — 3 m 55 s of that spent discovering the XInput2 header, the 4 GiB
RAM ceiling and the `-Werror` scoping issue.

## 8. Open risks

1. **The `app/CMakeLists.txt` fix is only in the guest**, not in the repo. Until
   the parent applies it, every fresh configure here reproduces the 6 Opus/HarfBuzz
   `-Werror` errors, and the next host→guest rsync silently undoes it (see §6).
   The claim "macOS is unaffected" is reasoned, not measured in this stage — the
   host build was not run.
2. **`ccache` did not help in this stage**: `Cacheable calls 137, Hits 0, Misses
   137` — attempt 4 changed the flag *position* for every TU (target-level →
   per-source), so every object missed. The 71 s build is a cold-Linux number; an
   untouched rebuild is expected to be seconds, unmeasured.
3. **Memory.** 4 GiB guest RAM alone OOMs `-j8` JUCE unity TUs. `-j4` + 4 GiB
   `/swapfile` is the measured-stable setting; both the swapfile and the modules
   are session-only and disappear on reboot.
4. **The app has not been run.** No proof of window creation, GL context, audio
   device open or MIDI port enumeration on this guest. X11 client libraries are
   `dlopen`ed at run time, so nothing in the link output covers §4's note.
5. **`mnkybox_app_logic` (CTest #5) is still unrun** — it executes `mnkybox
   --unittests`, i.e. launching the app, which is out of C1 scope. `ctest -N` now
   resolves the binary instead of warning about a missing file, but the test
   itself is C2's.
6. **ALSA/MIDI surface is thin**: `snd-aloop` gives one loopback card (PCM only),
   and the only sequencer port in the guest is `14 Midi Through Port-0`. There is
   no `snd-dummy` in this kernel (`# CONFIG_SND_DUMMY is not set`) and no USB MIDI
   hardware. C2 must not read "port Midi Through" as Pi-typical port enumeration.
7. **Guest disk grew to 7.2 G of 20 G** (`~/pi-build` 307 M from stage B,
   `~/pi-build-ccache` 666 M, ccache 108 M). Room for C2 artefacts, not for many
   more build directories.
8. **Two build directories now exist** (`~/pi-build` from stage B without ccache,
   `~/pi-build-ccache` here). C2 should stay in `~/pi-build-ccache` so the ccache
   and the built `mnkygain.so`/`mnky_selftest` paths in `MNKY_DEFAULT_PLUGIN` and
   `-DMNKY_SELFTEST_CLAP_PLUGIN` stay valid.

## 9. Files

New, all under `midibrain/pi/qemu/` (nothing committed or pushed):

- `STAGE-C1.md` — this file.
- `provision-c.sh` — idempotent stage C guest deltas: packages (`ccache`,
  `xdotool`, `netpbm`, `libxi-dev`), `audio` group, `snd-aloop` +
  `snd-seq-dummy`, 4 GiB `/swapfile`, Xvfb `:99` with `/tmp/xvfb.pid`, and a
  verification dump (`aplay -l`, `aconnect -l`, `lsmod`, swap, `xdpyinfo`).
  Re-run measured at 3.35 s.
- `screenshots/00-xroot.jpg` — 10,355-byte 1024x600 JPEG of the Xvfb root
  window, the proof that `xwd | xwdtopnm | pnmtojpeg` works in this guest.

Untouched: every other file in the repo, `debian.qcow2`, `seed.iso`,
`vm.{log,pid}`, and the guest-side `app/CMakeLists.txt` patch exists only in
`~/midibrain` (§6).

Guest-side artifacts (not in the repo): `~/pi-build-ccache`, `/tmp/configure-ccache.log`,
`/tmp/build-app-oom.log`, `/tmp/build-app-werror.log`, `/tmp/build-app.log`,
`/tmp/xroot.jpg`, `/swapfile`.

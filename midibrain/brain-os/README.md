# MNKYBOX boot image (`midibrain/brain-os`)

Buildroot external tree (name **MNKYBOX**) that builds a bootable Raspberry Pi 5
SD card image for the mnkybox recorder/MIDI hub: systemd, PipeWire 1.6 with
`pipewire-pulse`, WirePlumber and `pipewire-jack`, Mesa/V3D under Weston 15 on
the Waveshare 3.5inch DSI LCD (H), a Qt 6 QML application on the **Wayland**
platform plugin (native Wayland, full multitouch), WiFi and key-only Dropbear.

The application is **not** compiled here. It ships as a rootfs overlay: prebuilt
aarch64 binaries in `/opt/mnkybox/bin` plus systemd units. Until the application
team lands the real binaries, stub scripts with documented exit codes stand in
(see [application overlay contract](#application-overlay-contract)).

Nothing in this directory has been run on Raspberry Pi 5 hardware yet. See
[What is unverified](#what-is-unverified-without-pi-5-hardware).

## Build

Host prerequisites are the standard Buildroot list: `wget`, `git`, `cpio`,
`bc`, `bzip2` (`bzcat`), `unzip`, `rsync`, `file`, `perl` **with its core
modules** (`ExtUtils::MakeMaker`, `FindBin`, `IPC::Cmd`, `Thread::Queue`,
`English`), `flex`, `bison`, `m4`, `python3`, a C/C++ toolchain and `make`.
A `perl-base`-only installation fails Buildroot's dependency check.

```sh
# 1. Buildroot 2026.08.1 (verified, see below)
curl -LO https://buildroot.org/downloads/buildroot-2026.08.1.tar.xz
echo "34fb7bea8a213ffde39cc02e61703391560f26e0850f03138c722d0b09880f2b  buildroot-2026.08.1.tar.xz" | sha256sum -c
tar xf buildroot-2026.08.1.tar.xz && cd buildroot-2026.08.1

# 2. Configure and download (all hash checks must pass)
make BR2_EXTERNAL=/path/to/electronics/midibrain/brain-os mnkybox_rpi5_defconfig
make source

# 3. Build (cold build: 90-150 min on 8+ cores; needs ~30 GB of disk)
make -j"$(nproc)"
```

Output: `output/images/sdcard.img` (write it with `dd` or Raspberry Pi Imager),
`output/images/boot.vfat` and `output/images/rootfs.ext4`.

`make` runs, in this order: `board/mnkybox/post-build.sh` (assembles the boot
partition tree, compiles the panel overlay, finishes the root filesystem),
`board/mnkybox/post-image.sh` (genimage), then `board/mnkybox/check-image.sh`
(the acceptance checks). A failing check fails the build.

Checks can be re-run by hand:

```sh
board/mnkybox/check-image.sh output/images     # image acceptance list
board/mnkybox/check-elf-deps.sh output/images  # prebuilt app dependencies
scripts/qemu-user-smoke.sh output/target       # user-space smoke test (qemu-user)
scripts/native-smoke.sh                        # same checks, run inside the rootfs
```

`scripts/native-smoke.sh` is meant to run **inside** the built root filesystem
on real aarch64 hardware, for example on the owner's Pi 5 with
`systemd-nspawn -D output/target` (or a chroot): it executes the same binaries
without qemu-user, so it catches glibc, library and loader problems that
emulation hides. It still cannot test the Pi kernel, DRM/DSI, touch, audio or
systemd unit ordering; those need the flashed image.

## Verified inputs

**Buildroot tarball.** `buildroot-2026.08.1.tar.xz`.

| | |
|---|---|
| SHA-256 | `34fb7bea8a213ffde39cc02e61703391560f26e0850f03138c722d0b09880f2b` |
| Verification | matches the checksum in `buildroot-2026.08.1.tar.xz.sign` (clearsigned by Arnout Vandecappelle, key `18C7 DF28 19C1 733D 822D 599E A500 D6EE 9CB0 E540`; `gpg --verify`: Good signature), and `xz -t` passes |
| Retrieved | 2026-11-04 |

**Kernel.** Raspberry Pi downstream `rpi-6.12.y`, version 6.12.61, commit
`21b410140c47ffab5668399f6f143c7d7b935c8b` — the commit Buildroot 2026.08.1
pins for `raspberrypi5_defconfig`. Its tarball hash is copied into
`board/mnkybox/patches/linux/linux.hash` (and `.../linux-headers/`), so
`BR2_DOWNLOAD_FORCE_CHECK_HASHES=y` verifies it.

**Toolchain.** Bootlin aarch64 glibc stable external toolchain, as in the
upstream Pi 5 defconfig.

**Package versions, re-confirmed against the 2026.08 tree** (they all moved
since the first plan):

| Package | Version | Symbol used |
|---|---|---|
| PipeWire | 1.6.6 | `BR2_PACKAGE_PIPEWIRE` |
| PipeWire JACK client | built by PipeWire (`-Dpipewire-jack=enabled`) | no Kconfig symbol, see below |
| pipewire-pulse | built by PipeWire (`-Dlibpulse=enabled`) | `BR2_PACKAGE_PULSEAUDIO` (client lib only) |
| WirePlumber | 0.5.14 | `BR2_PACKAGE_WIREPLUMBER` + `BR2_PACKAGE_LUA_5_4` |
| Weston | 15.0.1 | `BR2_PACKAGE_WESTON` (DRM, kiosk shell, no XWayland) |
| Mesa | 26.1.8 | `BR2_PACKAGE_MESA3D_GALLIUM_DRIVER_V3D` (+ VC4 runtime), EGL/GLES |
| Qt | 6.11.2 | `BR2_PACKAGE_QT6BASE`, `QT6DECLARATIVE`, `QT6WAYLAND`, `QT6MULTIMEDIA`, `QT6SVG`, `QT6VIRTUALKEYBOARD` |
| seatd | 0.9.1 | `BR2_PACKAGE_SEATD` + `BR2_PACKAGE_SEATD_DAEMON` |
| wpa_supplicant | 2.12 | `BR2_PACKAGE_WPA_SUPPLICANT` + `_NL80211` |
| Dropbear | 2026.94 | `BR2_PACKAGE_DROPBEAR` |
| linux-firmware | 20260810 | `BR2_PACKAGE_LINUX_FIRMWARE_CYPRESS_CYW43XXX` |
| libinput | 1.31.3 | `BR2_PACKAGE_LIBINPUT` (weston selects it) |
| kernel / headers | 6.12.61 | `BR2_PACKAGE_HOST_LINUX_HEADERS_CUSTOM_6_12` |

**Waveshare panel.** Vendor overlay blob
`https://files.waveshare.com/wiki/common/Waveshare_35DSI.dtbo`, sha256
`ae09deaccceb80ced72519977fedc6d7becbd548a3043538c147ad40633b4611` (4747 bytes),
retrieved 2026-11-04 from <https://www.waveshare.com/wiki/3.5inch_DSI_LCD_(H)>.
Only its numbers are used; the overlay itself is our own source in
`board/mnkybox/dts/waveshare-3p5-dsi-h-overlay.dts`.

**BCM43455 NVRAM.** `brcmfmac43455-sdio.txt` from
[RPi-Distro/firmware-nonfree, branch bookworm](https://github.com/RPi-Distro/firmware-nonfree),
sha256 `ca709be81a78bdb6932936374f39943acbd7af07fae6151011127599a3ce9e3d`
(2074 bytes), retrieved 2026-11-04; provenance in
`board/mnkybox/firmware/brcmfmac43455-sdio.txt.source`.

## Measured reference data (Raspberry Pi 5, Rev 1.1)

The owner's reference box is a Raspberry Pi 5 Model B Rev 1.1, 16 GB RAM,
4 cores, Debian 13, kernel `6.18.34+rpt-rpi-2712`, NVMe with 181 GB free. It is
a **verification** box only: the image is still cross-built on x86_64, and this
tree never plans a native build on the Pi. Its probes produced the following
measured facts, which narrow the "unverified" list below. They are quoted
verbatim from that box and are **not** results from this image.

| Measurement | Quoted evidence | What it settles |
|---|---|---|
| WiFi chip is BCM43455 | `brcmfmac: brcm_fw_alloc_request: using brcm/brcmfmac43455-sdio for chip BCM4345/6` | confirms correction 3: the Pi 5 needs `brcmfmac43455-sdio`, never a BCM43456 file |
| Cypress firmware variant in use | `brcmfmac_cyw` module loaded next to `brcmfmac` | supports using `cypress/cyfmac43455-sdio.bin` as the blob source |
| `goodix_ts` driver exists in the Pi kernel line | module `goodix_ts` present | the GT911 path is a stock driver, not a vendor patch |
| DSI-2 connector works | `card0-DSI-2` is `connected` to a 1920x1080 DSI monitor (not our panel) | the Pi 5 `&dsi1` node drives the second MIPI connector; our overlay's `dsi1` default targets exactly that port |
| A Goodix chip is present but does not come up at 0x14 | `Goodix-TS 11-0014: Error reading 1 bytes from 0x8140: -121` (after dummy regulators) | the 0x5d-versus-0x14 address selection is real: the INT level during reset picks the address, and a wrong selection looks exactly like this probe failure |
| Active touchscreen on that box is ILI210x, not Goodix | `N: Name="ILI210x Touchscreen"` at `i2c-11/11-0041` | the Goodix controller on that box is a second device on the same bus, so address collisions must be considered |
| No USB audio interface attached | `--- no soundcards ---` (only `seq` and `timer`) | UMC1820 bring-up, default-sink policy and 48 kHz graph are still unmeasured |

The overlay therefore keeps `0x5d` as the primary GT911 address and declares
`0x14` as the documented alternative, in that order. If touch does not probe,
`i2cdetect -y 11` shows which address answers, and the `.dts` is the single
place to change it.

## Decisions, including three plan corrections

1. **No U-Boot.** U-Boot has no Raspberry Pi 5 / BCM2712 board:
   `configs/rpi_5_defconfig` returns 404 in every release from v2025.01 to
   v2026.01 (checked 2026-11-04). The Pi 5 firmware loads the kernel directly,
   as the upstream Buildroot `raspberrypi5_defconfig` does. The image contains
   `kernel_2712.img` (the kernel `Image`) instead of `u-boot.bin`, selected by
   `kernel=kernel_2712.img` in `config.txt`.
2. **PipeWire JACK has no Kconfig symbol** (still true in 2026.08).
   `package/pipewire/pipewire.mk` passes `-Dpipewire-jack=enabled` **only** when
   `BR2_PACKAGE_JACK2=y`, which would put JACK2 in the image. `external.mk`
   rewrites `PIPEWIRE_CONF_OPTS` to `-Dpipewire-jack=enabled -Djack=disabled`,
   so PipeWire builds its own client library and JACK2 stays out. It lands in
   `/usr/lib/pipewire-0.3/jack/libjack.so.0` (that is where PipeWire installs
   it); external JACK clients reach it through `pw-jack` or `LD_LIBRARY_PATH`.
   `check-image.sh` asserts it exists and that no JACK2 `jackd`/`libjack.so` is
   present.
3. **Pi 5 WiFi is BCM43455, not BCM43456.** RPi-Distro/firmware-nonfree ships
   `brcmfmac43455-sdio.raspberrypi,5-model-b.*` and no BCM43456 file for the
   Pi 5 (BCM43456 is Pi 400 / CM4). Buildroot's `linux-firmware` has no BCM43455
   blob at all, so the image uses `CYPRESS_CYW43XXX`
   (`cypress/cyfmac43455-sdio.bin`, `.clm_blob`) plus the symlinks and the
   NVRAM file that `post-build.sh` installs.

Further decisions from the owner redirects:

* **Qt 6 QML on native Wayland, no XWayland.** The application is Qt 6, so the
  image ships Qt 6.11 with the Wayland QPA and Weston 15 without XWayland. No
  X11 client libraries are installed. This also gives the app full multitouch
  (see [Touch](#touch-and-calibration)). JUCE remains only inside the app
  binaries as a headless component and changes nothing in this tree.
* **PREEMPT_RT, in-tree.** Linux 6.12 merged PREEMPT_RT upstream and this
  tree's arm64 port selects `ARCH_SUPPORTS_RT` (`arch/arm64/Kconfig:105`), so
  `CONFIG_PREEMPT_RT=y` needs no out-of-tree patch stack and no separate
  `rpi-6.12.y-rt` branch. It is enabled; the fallback (plain `CONFIG_PREEMPT` +
  `threadirqs` + `SCHED_FIFO` via `LimitRTPRIO`) is recorded in the fragment in
  case RT ever has to be dropped.
* **Session units are systemd *user* units** of user `mnkybox`, with lingering
  enabled (`/var/lib/systemd/linger/mnkybox`), so `user@1000.service` starts at
  boot and runs `pipewire`, `pipewire-pulse`, `wireplumber`, `weston` and
  `mnkybox-app`. This is what the application team asked for; it also gives one
  `XDG_RUNTIME_DIR` (`/run/user/1000`) to all of them.
  `seatd` stays a **system** unit: it is the seat broker that hands DRM master
  to the user's Weston, it must run before any session, and running it inside
  the session would deadlock the very session it serves. Real-time limits are
  raised on `user@1000.service` (a drop-in), because user units cannot lift
  `RLIMIT_RTPRIO`/`RLIMIT_MEMLOCK` themselves.
* **Audio is PulseAudio-compatible.** The app talks to audio only through Qt
  Multimedia's PulseAudio backend, i.e. `pipewire-pulse`. `pipewire-alsa` is
  the fallback for ALSA clients, `pipewire-jack` is there for external JACK
  clients, and routing happens in the PipeWire graph (`pw-link`), not in the
  app UI. Default device policy is WirePlumber configuration, not app code.

## What the image contains

| Item | Value |
|---|---|
| Init | systemd, no getty, no serial console |
| Session | user `mnkybox`, user manager with lingering, `XDG_RUNTIME_DIR=/run/user/1000` |
| Sound | PipeWire 1.6 + WirePlumber 0.5 + `pipewire-pulse` + `pipewire-alsa` + `pipewire-jack` |
| Display | Weston 15 DRM backend, kiosk shell, Mesa V3D, Qt 6.11 Wayland client |
| Seat | `seatd` (system unit); user `mnkybox` in `seat`, `video`, `render`, `input`, `audio` |
| WiFi | brcmfmac (BCM43455) + wpa_supplicant + systemd-networkd + wireless-regdb |
| SSH | Dropbear with `-s` (no password logins) |
| Bring-up tools | `evtest`, `libinput debug-events`, `weston-touch-calibrator`, `wpctl`, `pw-link`, `i2cdetect` |
| Kernel | rpi-6.12.y 6.12.61, `CONFIG_PREEMPT_RT`, HZ 1000, 4 KiB pages |
| Layout | p1 FAT32 128 MiB boot, p2 ext4 1 GiB root |

Enabled units: system units `systemd-networkd`, `dropbear`, `seatd`,
`mnkybox-firstboot`; user units `pipewire`, `pipewire-pulse`, `wireplumber`,
`weston`, `mnkybox-app`. `post-build.sh` creates the symlinks and
`check-image.sh` asserts them.

## First boot: WiFi and SSH

Both are optional and both are off by default — no secret and no country are
baked into the image.

1. Mount the FAT partition of the written card on a computer.
2. `mnkybox/wpa_supplicant.conf`: set `country=XX` and your network. **The
   country is mandatory**: without it the regulatory database refuses to
   transmit, and the first-boot unit leaves WiFi off and logs why.
3. `mnkybox/authorized_keys`: your OpenSSH public key. Dropbear runs with `-s`,
   so password login is impossible; without a key, SSH login is impossible.
4. Boot. `mnkybox-firstboot.service` copies the files into place, renames them
   to `*.applied`, enables `wpa_supplicant@wlan0.service` and starts it.
5. Wired Ethernet is always on DHCP, so a wired login works even when WiFi is
   misconfigured.

There is no serial console: UART0 and UART1 are reserved for MIDI
(`midibrain/docs/pin-map.md`), so `cmdline.txt` has no UART `console=` and
`config.txt` enables no serial function. The kernel console is `tty1` (the DSI
panel). Recovery from a bad network configuration: pull the card, mount the FAT
partition, fix `mnkybox/wpa_supplicant.conf`, remove the `.applied` suffix.

## Application overlay contract

The image provides the paths, user, session, runtime directory, display, sound
server and limits; the application team supplies `/opt/mnkybox/bin/mnkybox`
(aarch64, glibc) and any libraries it needs. The full table is in
`board/mnkybox/rootfs-overlay/opt/mnkybox/CONTRACT.md`, installed as
`/opt/mnkybox/CONTRACT.md`. The unit already sets `QT_QPA_PLATFORM=wayland`,
`QT_WAYLAND_DISABLE_WINDOWDECORATION=1`, `QT_WAYLAND_SHELL_INTEGRATION=xdg-shell`
and the virtual-keyboard input method.

Stubs ship today:

| File | Behaviour |
|---|---|
| `/opt/mnkybox/bin/mnkybox` | prints a message, exits **78** (`EX_CONFIG`); `--version` exits 0 and says "stub" |
| `/opt/mnkybox/bin/mnky_selftest` | same, for the headless self-test |

`mnkybox-app.service` retries three times and then stays failed, which is
visible in `systemctl --user --failed`; the rest of the system keeps working.
After the real binaries land, `check-elf-deps.sh` fails the build if a needed
library or the program interpreter is missing from the root filesystem.

## Audio

```
Qt Multimedia (PulseAudio backend)  ->  pipewire-pulse  ->  PipeWire graph  ->  ALSA (snd-usb-audio)
external JACK clients               ->  pipewire-jack   ->        ^
other ALSA clients                  ->  pipewire-alsa   ->        ^
```

* Graph rate 48 kHz for everything:
  `/etc/pipewire/pipewire.conf.d/50-mnkybox.conf`.
* Default sink and source: a UMC1820-class USB Audio Class interface
  (`snd-usb-audio`, no firmware) whenever it is present, otherwise
  WirePlumber's normal choice:
  `/etc/wireplumber/wireplumber.conf.d/50-mnkybox.conf` raises its
  `priority.session`. The policy is configuration, applied at every boot.
* Routing between application streams and JACK/ALSA clients is done in the
  graph: `pw-link -l`, `pw-link <out> <in>`, `wpctl status`.
* There is no PulseAudio daemon in the image; `pipewire-pulse` serves Pulse
  clients. `check-image.sh` asserts that.

## Display, touch and calibration

The panel is declared by our own overlay, compiled at build time from
`board/mnkybox/dts/waveshare-3p5-dsi-h-overlay.dts` into
`overlays/waveshare-3p5-dsi-h.dtbo` (`dtc -@`). `config.txt` has:

```
dtoverlay=vc4-kms-v3d
dtoverlay=waveshare-3p5-dsi-h,dsi1
ignore_lcd=1
dtparam=spi=on
```

and deliberately **no** `display_auto_detect`.

**Touch pipeline.** The GT911 reports up to 5 simultaneous points. The in-kernel
`goodix` driver publishes them as Linux input protocol B MT slots
(`ABS_MT_SLOT` / `ABS_MT_POSITION_X/Y` / `ABS_MT_TRACKING_ID`), so libinput,
Weston and Qt's Wayland client all receive every finger. Multitouch is therefore
supported end to end on the OS side; the QML code must use touch handlers
(`MultiPointTouchArea`, `PinchHandler`, `DragHandler`) to act on more than one
point. The UI is single-touch-safe by design, so wave 1 works with one finger.

**Interrupt line.** The vendor overlay declares no interrupt and no reset GPIO
for this panel, and no primary source we have documents those pins. This is not
fatal: `goodix_request_irq()` in `drivers/input/touchscreen/goodix.c` falls back
to a polling timer when `client->irq` is 0, which is what the vendor relies on.
Polling costs latency and CPU. If a schematic confirms the pins, add
`interrupt-parent`/`interrupts` and `reset-gpios`/`irq-gpios` to the overlay
instead of guessing them.

**Calibration and bring-up commands** (over SSH):

```sh
evtest                          # raw events: check ABS_MT_SLOT / ABS_MT_POSITION_*
libinput debug-events           # libinput's view of the touchscreen
libinput list-devices           # device, size and calibration properties
weston-touch-calibrator         # writes a udev rule with LIBINPUT_CALIBRATION_MATRIX
modetest -c                     # connectors, modes and the output name
i2cdetect -y 11                 # GT911 must answer at 0x5d or 0x14 (i2c_csi_dsi1)
dmesg | grep -iE "goodix|dsi|panel"
```

`touchscreen-size-x/y` in the overlay are set to 800x480, the coordinate space
Weston presents after the 90 degree rotation. If touch axes come out swapped or
clipped, change them to 480x800 in the overlay, or calibrate with
`weston-touch-calibrator` (it writes
`/etc/udev/rules.d/99-touch-calibration.rules` with `LIBINPUT_CALIBRATION_MATRIX`,
which survives reboot).

**Which connector.** The Pi 5 has two 22-pin MIPI connectors. In the Pi 5 device
tree `drm-dsi1 = &dsi0` and `drm-dsi2 = &dsi1`, so `dsi1` in the overlay means
the `&dsi1` node. If nothing comes up, try the other connector with `dsi0`; the
touch bus follows automatically (`i2c_csi_dsi0` vs `i2c_csi_dsi1`).

**Overlay names.** Waveshare publishes one blob, `Waveshare_35DSI.dtbo`, selected
as `dtoverlay=waveshare_35DSI,35H,dsi1` (older 5.10-era driver packages used
`WS_xinchDSI_Screen` + `WS_xinchDSI_Touch`). We ship neither blob: our overlay
replaces both and takes only the vendor's timings (33.6 MHz, 480x800, 70x50 mm)
and touch addresses.

**Data lanes.** The overlay defaults to 2 lanes. The vendor blob leaves its
one-lane fragment dormant, i.e. its own default is 1 lane; 2 lanes is our
assumption because 480x800 RGB888 at 33.6 MHz has little margin on one lane. If
the panel stays black with the backlight on, try
`dtoverlay=waveshare-3p5-dsi-h,dsi1,one-lane` (or `four-lane`).

**Cable.** The panel uses a 15-pin FPC on a 22-pin Pi connector. Before changing
software, re-seat the ribbon: contacts facing the correct way, fully inserted,
latch closed. A reversed or half-seated cable gives a blank panel or no DSI
link, which looks exactly like a device-tree fault. Cable orientation, pinout and
the 15-to-22-pin adapter are **not** verified by this repository.

**Rotation.** `weston.ini` sets `transform=90` so the 480x800 portrait panel is
used as 800x480. If the output is unrotated, name the connector explicitly
(`modetest -c`) in the `[output]` section.

## Real-time kernel

`CONFIG_PREEMPT_RT=y` from the kernel tree itself (Linux 6.12 merged RT, arm64
selects `ARCH_SUPPORTS_RT`), `CONFIG_HZ_1000`, `NO_HZ_IDLE`, and `threadirqs` on
the kernel command line. Rationale: the audio engine's block budget is 2.67 ms
with the Pico 2 as clock master, and RT removes the remaining non-preemptible
sections; the cost is throughput, which this appliance does not need. If RT ever
has to be dropped, the fallback is `CONFIG_PREEMPT` + `threadirqs` +
`SCHED_FIFO` for the audio threads, and the fragment must record that change.
Do not switch on the strength of this document: measure on hardware first.

Real-time limits come from `/etc/systemd/system/user@1000.service.d/limits.conf`
(`LimitRTPRIO=95`, `LimitMEMLOCK=infinity`); `/etc/security/limits.d/audio.conf`
covers interactive logins.

## What is unverified without Pi 5 hardware

1. **The image boots at all.** No card has been written. The boot chain
   (firmware → `kernel_2712.img` → ext4 root on p2 → systemd → user manager →
   session units) is built from the upstream Pi 5 layout, not observed.
2. **PREEMPT_RT on this tree.** The option is in-tree and selected, and the
   build is checked; runtime latency, driver behaviour under RT (vc4/v3d, rp1,
   brcmfmac) and any RT-unsafe driver are untested.
3. **DSI panel and touch.** Connector choice (`dsi1` vs `dsi0`), lane count, the
   `Generic,panel-dsi` binding, `ignore_lcd`, rotation, the GT911 polling
   fallback and both I2C addresses are untested. The reference box shows a
   working DSI-2 connector with a 1920x1080 monitor and a Goodix probe failing
   at `0x14`, but never with this panel.
4. **Weston 15 + Mesa 26 on the Pi 5.** Mesa 26 no longer has a separate kmsro
   option; whether EGL/GBM works on the RP1 display node is unknown. If it does
   not, Weston falls back to its pixman renderer (slower but functional). Check
   `journalctl --user -u weston` for the renderer.
5. **Qt 6.11 on Wayland.** The Wayland QPA plugin, kiosk-shell fullscreen,
   virtual keyboard and multitouch delivery to Qt are untested on hardware.
6. **Audio.** PipeWire + `pipewire-pulse` + WirePlumber policy, the 48 kHz
   graph, UMC1820 priority, real-time scheduling and the 2.67 ms budget are
   unmeasured, and the reference box currently reports `--- no soundcards ---`,
   so there is not even a USB interface to test against yet. `libjack.so` is
   proven to exist in the built rootfs, not to work with any client.
7. **WiFi and Bluetooth.** BCM43455 firmware loading, the NVRAM file, the
   country/regdb gate and association are untested. Bluetooth is not configured
   at all (UART0/UART1 are reserved for MIDI).
8. **SSH and first boot.** `mnkybox-firstboot.service`, the `.applied` rename,
   Dropbear `-s` and key-only login are untested.
9. **Power, cooling, NVMe and the mechanical stack.** Out of scope here; see
   `midibrain/docs/mechanical-stack.md`. The image assumes an SD card
   (`root=/dev/mmcblk0p2`).
10. **qemu-system.** Only the user-space smoke test is provided
    (`scripts/qemu-user-smoke.sh`); it uses the host kernel, so no Pi kernel, no
    GPU, no DSI, no touch, no systemd boot and no meaningful timing.

## Repository layout

```
midibrain/brain-os/
  external.desc, external.mk, Config.in   Buildroot external tree (name MNKYBOX)
  configs/mnkybox_rpi5_defconfig          the defconfig
  board/mnkybox/
    boot/config.txt, boot/cmdline.txt     firmware boot files
    boot-files/                           templates placed on the FAT partition
    dts/waveshare-3p5-dsi-h-overlay.dts   panel + touch overlay source
    firmware/                             BCM43455 NVRAM + provenance
    patches/linux{,-headers}/             kernel tarball hash
    linux-rpi5-mnkybox.fragment           kernel fragment (PREEMPT_RT, GT911, ...)
    genimage-mnkybox.cfg                  p1 FAT32 128 MiB + p2 ext4 1 GiB
    post-build.sh, post-image.sh          image assembly
    check-image.sh, check-elf-deps.sh     acceptance checks
    users.txt                             mnkybox user, seat/input/render groups
    rootfs-overlay/                       user+system units, PipeWire/WirePlumber
                                          policy, networkd, weston.ini, stubs
  scripts/qemu-user-smoke.sh              user-space smoke test
```

Ownership: this directory owns the image only. `midibrain/pi/` owns the
application, `midibrain/docs/pin-map.md` owns the pin allocation, and neither is
modified from here.

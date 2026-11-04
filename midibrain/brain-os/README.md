# MNKYBOX boot image (`midibrain/brain-os`)

Buildroot external tree (name **MNKYBOX**) that builds a bootable Raspberry Pi 5
SD card image for the mnkybox recorder/MIDI hub: systemd, PipeWire (with its own
JACK-compatible `libjack.so`), WirePlumber, Weston 14 on the Waveshare 3.5inch
DSI LCD (H) with XWayland for the JUCE application, WiFi and key-only Dropbear.

The application is **not** compiled here. It ships as a rootfs overlay: prebuilt
aarch64 binaries in `/opt/mnkybox/bin` plus systemd units. Until the application
team lands the real binaries, stub scripts with documented exit codes stand in
(see [application overlay contract](#application-overlay-contract)).

Nothing in this directory has been run on Raspberry Pi 5 hardware yet. See
[What is unverified](#what-is-unverified-without-pi-5-hardware).

## Build

Host prerequisites are the standard Buildroot 2025.02 list: `wget`, `git`,
`cpio`, `bc`, `bzip2` (`bzcat`), `unzip`, `rsync`, `file`, `perl` **with its
core modules** (`ExtUtils::MakeMaker`, `FindBin`, `IPC::Cmd`, `Thread::Queue`,
`English`), `flex`, `bison`, `m4`, `python3`, a C/C++ toolchain and `make`.
Distributions ship all of this; a `perl-base`-only installation fails
Buildroot's dependency check.

```sh
# 1. Buildroot 2025.02 LTS (verified, see below)
curl -LO https://buildroot.org/downloads/buildroot-2025.02.19.tar.xz
echo "e5e58bff1da91f022f60cefdfe280140c08023cd0aeec1e5f6f9da0ea866d69c  buildroot-2025.02.19.tar.xz" | sha256sum -c
tar xf buildroot-2025.02.19.tar.xz && cd buildroot-2025.02.19

# 2. Configure and download (all hash checks must pass)
make BR2_EXTERNAL=/path/to/electronics/midibrain/brain-os mnkybox_rpi5_defconfig
make source

# 3. Build (cold build: 60-120 min on 8+ cores; needs ~25 GB of disk)
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
scripts/qemu-user-smoke.sh output/target       # user-space smoke test
```

## Verified inputs

**Buildroot tarball.** `buildroot-2025.02.19.tar.xz`, the latest 2025.02.x LTS
release at the time of writing.

| | |
|---|---|
| SHA-256 | `e5e58bff1da91f022f60cefdfe280140c08023cd0aeec1e5f6f9da0ea866d69c` |
| Verification | matches the checksum in `buildroot-2025.02.19.tar.xz.sign` (clearsigned by Arnout Vandecappelle, key `18C7 DF28 19C1 733D 822D 599E A500 D6EE 9CB0 E540`, `gpg --verify`: Good signature), and `xz -t` passes |
| Retrieved | 2026-11-04 |

**Kernel.** Raspberry Pi downstream `rpi-6.12.y`, commit
`576cc10e1ed50a9eacffc7a05c796051d7343ea4` — the commit Buildroot 2025.02
pins for `raspberrypi5_defconfig`. Its tarball hash is copied into
`board/mnkybox/patches/linux/linux.hash` (and `.../linux-headers/`), so
`BR2_DOWNLOAD_FORCE_CHECK_HASHES=y` verifies it.

**Package versions** in this Buildroot release: PipeWire 1.2.8, WirePlumber
0.5.5, Weston 14.0.1, Mesa with V3D, Linux headers 6.12.

**Waveshare panel.** Vendor overlay blob
`https://files.waveshare.com/wiki/common/Waveshare_35DSI.dtbo`, sha256
`ae09deaccceb80ced72519977fedc6d7becbd548a3043538c147ad40633b4611` (4747 bytes),
retrieved 2026-11-04 from the wiki page
<https://www.waveshare.com/wiki/3.5inch_DSI_LCD_(H)>. Only its numbers are used:
the overlay itself is our own source in
`board/mnkybox/dts/waveshare-3p5-dsi-h-overlay.dts`.

**BCM43455 NVRAM.** `brcmfmac43455-sdio.txt` from
[RPi-Distro/firmware-nonfree, branch bookworm](https://github.com/RPi-Distro/firmware-nonfree),
sha256 `ca709be81a78bdb6932936374f39943acbd7af07fae6151011127599a3ce9e3d`
(2074 bytes), retrieved 2026-11-04. Provenance is in
`board/mnkybox/firmware/brcmfmac43455-sdio.txt.source`.

## Decisions that differ from the first plan

The reviewed plan contained three assumptions that did not survive checking.
Each is recorded here with the evidence.

1. **No U-Boot.** The plan asked for the U-Boot `rpi_5` loader. U-Boot has no
   Raspberry Pi 5 / BCM2712 board: `configs/rpi_5_defconfig` returns 404 in
   every release from v2025.01 to v2026.01 (checked 2026-11-04). The Pi 5
   firmware therefore loads the kernel directly, exactly as the upstream
   Buildroot `raspberrypi5_defconfig` does. `sdcard.img` contains
   `kernel_2712.img` (the kernel `Image`) instead of `u-boot.bin`, and
   `config.txt` selects it with `kernel=kernel_2712.img`.
2. **PipeWire JACK has no Kconfig symbol.** There is no
   `BR2_PACKAGE_PIPEWIRE_JACK` in Buildroot 2025.02. `package/pipewire/pipewire.mk`
   passes `-Dpipewire-jack=enabled` **only** when `BR2_PACKAGE_JACK2=y`, which
   would put JACK2 in the image. `external.mk` therefore rewrites
   `PIPEWIRE_CONF_OPTS` to `-Dpipewire-jack=enabled -Djack=disabled`, so
   PipeWire builds its own client library and JACK2 stays out. The library is
   installed where PipeWire puts it:
   `/usr/lib/pipewire-0.3/jack/libjack.so.0`. JACK clients find it through
   `pw-jack`, or by adding that directory to `LD_LIBRARY_PATH`; it is
   deliberately not in `/usr/lib`. `check-image.sh` asserts the file exists and
   that no JACK2 `jackd`/`libjack.so` is present.
3. **Pi 5 WiFi is BCM43455, not BCM43456.** RPi-Distro/firmware-nonfree ships
   `brcmfmac43455-sdio.raspberrypi,5-model-b.{bin,clm_blob,txt}` and no
   BCM43456 file for the Pi 5 (BCM43456 is Pi 400 / CM4). Buildroot's
   `linux-firmware` package has no BCM43455 blob at all, so the image uses
   `BR2_PACKAGE_LINUX_FIRMWARE_CYPRESS_CYW43XXX`
   (`cypress/cyfmac43455-sdio.bin`, `.clm_blob`) plus the symlinks and the
   NVRAM file that `post-build.sh` installs.

Two smaller corrections: the panel is 480x800 **portrait** (800x480 landscape
after rotation), and the DSI data-lane count is our assumption (see
[DSI bring-up](#dsi-bring-up)).

## What the image contains

| Item | Value |
|---|---|
| Init | systemd (`BR2_INIT_SYSTEMD`), no getty, no serial console |
| Sound | PipeWire 1.2 + WirePlumber, ALSA (`libasound.so.2`), PipeWire `libjack.so.0` |
| Display | Weston 14 DRM backend, kiosk shell, XWayland, Mesa V3D + kmsro |
| Seat | `seatd` (systemd-logind is disabled); user `mnkybox` is in `seat`, `video`, `render`, `input`, `audio` |
| Runtime dir | `XDG_RUNTIME_DIR=/run/mnkybox`, mode 0700, owned by `mnkybox` |
| WiFi | brcmfmac (BCM43455) + wpa_supplicant + systemd-networkd + wireless-regdb |
| SSH | Dropbear with `-s` (no password logins), enabled units listed below |
| Kernel | rpi-6.12.y, `CONFIG_PREEMPT` (not RT), HZ 1000, 4 KiB pages |
| Layout | p1 FAT32 128 MiB boot, p2 ext4 1 GiB root |

Enabled units (symlinks created by `post-build.sh`, asserted by
`check-image.sh`): `systemd-networkd`, `dropbear`, `seatd`, `pipewire`,
`wireplumber`, `weston`, `mnkybox-app`, `mnkybox-firstboot`.

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
panel). Recovery from a bad network configuration is: pull the card, mount the
FAT partition, fix `mnkybox/wpa_supplicant.conf`, remove the `.applied` suffix.

## Application overlay contract

The image provides the paths, user, runtime directory, display, sound server and
limits; the application team supplies `/opt/mnkybox/bin/mnkybox` (aarch64,
glibc) and any libraries it needs. The full table is in
`board/mnkybox/rootfs-overlay/opt/mnkybox/CONTRACT.md`, which is installed as
`/opt/mnkybox/CONTRACT.md`.

Stubs ship today:

| File | Behaviour |
|---|---|
| `/opt/mnkybox/bin/mnkybox` | prints a message, exits **78** (`EX_CONFIG`); `--version` exits 0 and says "stub" |
| `/opt/mnkybox/bin/mnky_selftest` | same, for the headless self-test |

`mnkybox-app.service` retries three times and then stays failed, which is
visible in `systemctl --failed`; the rest of the system keeps working. After the
real binaries land, `check-elf-deps.sh` fails the build if a needed library or
the program interpreter is missing from the root filesystem.

## DSI bring-up

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

**Which connector.** The Pi 5 has two 22-pin MIPI connectors. In the Pi 5 device
tree `drm-dsi1 = &dsi0` and `drm-dsi2 = &dsi1`, so `dsi1` in the overlay means
the `&dsi1` node (DRM "DSI-2"). If nothing comes up, try the other connector by
changing the parameter to `dsi0`; the touch bus follows automatically
(`i2c_csi_dsi0` vs `i2c_csi_dsi1`).

**Overlay names.** Waveshare publishes one blob, `Waveshare_35DSI.dtbo`, selected
as `dtoverlay=waveshare_35DSI,35H,dsi1` (older 5.10-era driver packages used
`WS_xinchDSI_Screen` + `WS_xinchDSI_Touch` instead). We do not ship either blob:
our overlay replaces both and takes only the vendor's timings (33.6 MHz,
480x800, 70x50 mm) and touch addresses.

**Data lanes.** The overlay defaults to 2 lanes. The vendor blob leaves its
one-lane fragment dormant, i.e. its own default is 1 lane; 2 lanes is our
assumption because 480x800 RGB888 at 33.6 MHz has little margin on one lane.
If the panel stays black with the backlight on, try
`dtoverlay=waveshare-3p5-dsi-h,dsi1,one-lane` (or `four-lane`) before suspecting
the cable.

**Touch (GT911).** The controller answers at I2C address `0x5d` or `0x14`
depending on the module's reset/INT strap; the overlay declares both nodes, as
the vendor does. To check: `i2cdetect -y 11` (i2c-11 is `i2c_csi_dsi1` on the
Pi 5; `-y 10` for `i2c_csi_dsi0`) should show one of the two addresses, and
`dmesg | grep -i goodix` should show a probe. No touch without a probe.

**Cable.** The panel uses a 15-pin FPC on a 22-pin Pi connector. Before changing
anything in software, re-seat the ribbon: contacts facing the correct way, fully
inserted, latch closed. A reversed or half-seated cable gives a blank panel or
no DSI link, which looks exactly like a device-tree fault. Cable orientation,
pinout and the 15-to-22-pin adapter are **not** verified by this repository.

**Rotation.** `weston.ini` sets `transform=90` so the 480x800 portrait panel is
used as 800x480. If the output is unrotated, name the connector explicitly
(`modetest -c` shows it) in the `[output]` section.

**Diagnostics** (over SSH): `dmesg | grep -iE "dsi|panel|goodix|vc4|v3d"`,
`modetest -c`, `journalctl -u weston -b`, `journalctl -u mnkybox-app -b`.

## Real-time kernel

`CONFIG_PREEMPT` (full preemption), `CONFIG_HZ_1000`, `NO_HZ_IDLE`, and
`threadirqs` on the kernel command line. PREEMPT_RT is **not** enabled: there is
no RT patchset for this `rpi-6.12.y` tree, and the audio engine's block budget
is 2.67 ms with the Pico 2 as clock master, which full preemption covers. The
documented upgrade path, if measurements ever show it is needed, is the
Raspberry Pi `rpi-6.12.y-rt` branch (or its successor) plus the same fragment,
`CONFIG_PREEMPT_RT=y`, and a re-measurement of the engine budget. Do not switch
to RT on the strength of this document: measure first.

PipeWire's `module-rt` gets its real-time priority from
`LimitRTPRIO=95`/`LimitMEMLOCK=infinity` in the units (limits.conf does not
apply to units started by PID 1); `/etc/security/limits.d/audio.conf` covers
interactive logins.

## What is unverified without Pi 5 hardware

Everything below is a claim this repository does **not** support. It is the list
to work through on the first bench session.

1. **The image boots at all.** No card has been written. The boot chain
   (firmware → `kernel_2712.img` → ext4 root on p2 → systemd) is built from the
   upstream Pi 5 defconfig layout, not observed.
2. **DSI panel and touch.** Connector choice (`dsi1` vs `dsi0`), data-lane
   count, the `Generic,panel-dsi` binding, `ignore_lcd`, rotation and the GT911
   address are untested. See [DSI bring-up](#dsi-bring-up).
3. **GPU / Weston.** Mesa V3D with kmsro on the Pi 5 display controller is
   assumed, not measured. If Weston falls back to its pixman renderer, the UI
   still appears but slowly; check `journalctl -u weston` for the renderer.
4. **XWayland with the JUCE application.** JUCE has no native Wayland backend in
   the versions this project pins, which is why XWayland is enabled; that the
   application renders correctly under Weston's kiosk shell is untested.
5. **Audio.** PipeWire real-time scheduling, USB audio device enumeration,
   ALSA/JACK client compatibility and the 2.67 ms block budget are all
   unmeasured. `libjack.so` is only proven to exist, not to work with the app.
6. **WiFi and Bluetooth.** BCM43455 firmware loading, the NVRAM file, the
   country/regdb gate and association are untested. Bluetooth is not configured
   at all (UART0/UART1 are reserved for MIDI, and the on-board Bluetooth needs
   one of them on other Pi models).
7. **SSH and first boot.** `mnkybox-firstboot.service`, the `.applied` rename,
   Dropbear `-s` and key-only login are untested.
8. **Power, cooling, NVMe and the mechanical stack.** Out of scope here; see
   `midibrain/docs/mechanical-stack.md`. The image assumes an SD card
   (`root=/dev/mmcblk0p2`); NVMe boot would need `cmdline.txt` and `fstab`
   changes.
9. **qemu-system.** Only the user-space smoke test is provided
   (`scripts/qemu-user-smoke.sh`). It uses the host kernel: no Pi kernel, no
   GPU, no DSI, no touch, no systemd boot, and timing is meaningless. A
   `qemu-system-aarch64` run with the built kernel would test early boot only;
   it is not wired up.

## Repository layout

```
midibrain/brain-os/
  external.desc, external.mk, Config.in   Buildroot external tree (name MNKYBOX)
  configs/mnkybox_rpi5_defconfig          the defconfig
  board/mnkybox/
    boot/config.txt, boot/cmdline.txt     firmware boot files
    boot-files/                           templates placed on the FAT partition
    dts/waveshare-3p5-dsi-h-overlay.dts   panel overlay source (compiled at build)
    firmware/                             BCM43455 NVRAM + provenance
    patches/linux{,-headers}/             kernel tarball hash
    linux-rpi5-mnkybox.fragment           kernel fragment
    genimage-mnkybox.cfg                  p1 FAT32 128 MiB + p2 ext4 1 GiB
    post-build.sh, post-image.sh          image assembly
    check-image.sh, check-elf-deps.sh     acceptance checks
    users.txt                             mnkybox user, seat/input/render groups
    rootfs-overlay/                       units, network, weston.ini, stubs
  scripts/qemu-user-smoke.sh              user-space smoke test
```

Ownership: this directory owns the image only. `midibrain/pi/` owns the
application, `midibrain/docs/pin-map.md` owns the pin allocation, and neither is
modified from here.

# MNKYBOX application overlay contract

This directory is the interface between the image (`midibrain/brain-os`) and
the application (`midibrain/pi`). The image never compiles the application: it
copies prebuilt files into the root filesystem.

## What the image provides

| Item | Value |
|---|---|
| Binary path | `/opt/mnkybox/bin/mnkybox` |
| Self-test path | `/opt/mnkybox/bin/mnky_selftest` |
| User / group | `mnkybox` / `mnkybox`, uid 1000, gid 1000 |
| Extra groups | `audio`, `video`, `input`, `render`, `seat` |
| Home | `/home/mnkybox` |
| Session | systemd **user** manager of `mnkybox`, started at boot by lingering |
| Runtime dir | `XDG_RUNTIME_DIR=/run/user/1000` (created by the user manager) |
| Display | Wayland: `WAYLAND_DISPLAY=wayland-0`, `QT_QPA_PLATFORM=wayland` |
| Compositor | Weston 15, DRM backend, kiosk shell, fullscreen, 800x480 after rotation |
| Audio | Qt Multimedia **PulseAudio backend** → `pipewire-pulse` (PipeWire 1.6 + WirePlumber 0.5) |
| Audio fallback | `pipewire-alsa` (ALSA clients), `pipewire-jack` (external JACK clients) |
| Limits | `LimitRTPRIO=95`, `LimitMEMLOCK=infinity` on `user@1000.service` |
| Units | user units `pipewire`, `pipewire-pulse`, `wireplumber`, `weston`, `mnkybox-app` |
| Persistent state | `/var/lib/mnkybox` (created empty at build time) |

The application unit is `mnkybox-app.service` (user unit). It starts after
`pipewire`, `wireplumber`, `pipewire-pulse` and `weston`, and it already sets:

```
WAYLAND_DISPLAY=wayland-0
QT_QPA_PLATFORM=wayland
QT_WAYLAND_DISABLE_WINDOWDECORATION=1
QT_WAYLAND_SHELL_INTEGRATION=xdg-shell
QT_IM_MODULE=qtvirtualkeyboard
QT_IM_MODULES=qtvirtualkeyboard
HOME=/home/mnkybox
```

## Audio contract

* The application talks to audio **only** through Qt Multimedia's PulseAudio
  backend, which connects to `pipewire-pulse`. There is no JACK API call and no
  direct ALSA access in the application.
* `pipewire-alsa` is installed as a fallback for ALSA clients and
  `pipewire-jack` for external JACK clients. Routing is done in the PipeWire
  graph with `pw-link`, not from the application UI.
* Default device policy is configuration, not application code:
  `/etc/wireplumber/wireplumber.conf.d/50-mnkybox.conf` gives a
  UMC1820-class USB Audio Class interface (`snd-usb-audio`, no firmware) the
  highest `priority.session`, so it is the default sink and source whenever it
  is present. Otherwise the normal WirePlumber choice applies.
* The graph runs at 48 kHz:
  `/etc/pipewire/pipewire.conf.d/50-mnkybox.conf` sets
  `default.clock.rate = 48000` and `allowed-rates = [ 48000 ]`.

## Touch and display contract

* The GT911 reports up to 5 contact points. The kernel `goodix` driver
  publishes them as Linux input protocol B MT slots; libinput, Weston and Qt's
  Wayland client all see every finger.
* Qt Quick must use touch-capable handlers (`MultiPointTouchArea`,
  `PinchHandler`, `DragHandler`, `TapHandler`) to act on more than one point;
  mouse handlers see only the first.
* The UI is single-touch-safe by design (large targets), so wave 1 works with
  one finger.

## What the application team must supply

1. The binary, aarch64 (glibc), at `/opt/mnkybox/bin/mnkybox`, executable.
2. Any shared libraries or data it needs, under `/opt/mnkybox/`. The image does
   not add library paths; ship an `/etc/ld.so.conf.d/mnkybox.conf` in the same
   change and run `ldconfig` from `post-build.sh` if you need one.
3. Optionally the self-test binary at `/opt/mnkybox/bin/mnky_selftest`.

Replace the stub scripts in
`board/mnkybox/rootfs-overlay/opt/mnkybox/bin/` or add the binaries to
`/opt/mnkybox/bin/` at image build time. Then run `check-elf-deps.sh`: it reads
every ELF file in that directory and fails when a needed shared library or the
program interpreter is missing from the root filesystem.

## Libraries already in the image

Qt 6.11 (Core, Gui, Qml, Quick, Wayland, Multimedia, Svg, VirtualKeyboard),
libpulse, PipeWire client + ALSA/JACK compatibility, WirePlumber, ALSA
(`libasound`), Mesa EGL/GLES (V3D), `libdrm`, `libgbm`, `libudev`, `libdbus`,
freetype, fontconfig, glibc, libstdc++, zlib, libpng.

Anything else is missing on purpose: add it to `configs/mnkybox_rpi5_defconfig`
or ship it under `/opt/mnkybox/`, and say so in the pull request.

## Stub exit codes (until the real binaries land)

| Code | Meaning |
|---|---|
| 0 | `--version` only: identifies the stub |
| 78 | `EX_CONFIG`: the real binary is not installed |

`mnkybox-app.service` retries three times (`StartLimitBurst=3`,
`StartLimitIntervalSec=300`) and then stays failed, which is visible in
`systemctl --user --failed` (or `systemctl --failed` for system units). The
image still boots and the network still works.

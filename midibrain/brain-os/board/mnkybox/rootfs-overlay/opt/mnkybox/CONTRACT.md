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
| Runtime dir | `XDG_RUNTIME_DIR=/run/mnkybox` (mode 0700, owned by `mnkybox`) |
| Display | X11 via XWayland: `DISPLAY=:0`; Wayland: `WAYLAND_DISPLAY=wayland-0` |
| Sound | PipeWire 1.2 + WirePlumber 0.5; ALSA (`libasound.so.2`) and PipeWire's `libjack.so.0` are installed |
| Limits | `LimitRTPRIO=95`, `LimitMEMLOCK=infinity` in `mnkybox-app.service` |
| Unit | `mnkybox-app.service`, started after `pipewire`, `wireplumber`, `weston` |
| Persistent state | `/var/lib/mnkybox` (created empty at build time) |

## What the application team must supply

1. The binary, aarch64 (glibc), at `/opt/mnkybox/bin/mnkybox`, executable.
2. Any shared libraries or data it needs, under `/opt/mnkybox/` (the image does
   not add library paths; if you need one, ship an `/etc/ld.so.conf.d/mnkybox.conf`
   in the same change and run `ldconfig` from `post-build.sh`).
3. Optionally the self-test binary at `/opt/mnkybox/bin/mnky_selftest`.

Replace the stub scripts in
`board/mnkybox/rootfs-overlay/opt/mnkybox/bin/` or add the binaries to
`/opt/mnkybox/bin/` at image build time. Then run `check-elf-deps.sh`: it reads
every ELF file in that directory and fails when a needed shared library or the
program interpreter is missing from the root filesystem.

## Libraries already in the image

ALSA (`libasound`), PipeWire client and its JACK library, `libX11`,
`libXext`, `libXinerama`, `libXrandr`, `libXcursor`, `libfreetype`,
`libfontconfig`, Mesa EGL/GLES (V3D + kmsro), `libdrm`, `libgbm`, `libudev`,
`libdbus`, glibc, libstdc++, zlib, libpng.

Anything else is missing on purpose: add it to `configs/mnkybox_rpi5_defconfig`
or ship it under `/opt/mnkybox/`, and say so in the pull request.

## Stub exit codes (until the real binaries land)

| Code | Meaning |
|---|---|
| 0 | `--version` only: identifies the stub |
| 78 | `EX_CONFIG`: the real binary is not installed |

`mnkybox-app.service` retries three times (`StartLimitBurst=3`,
`StartLimitIntervalSec=300`) and then stays failed, which is visible in
`systemctl --failed`. The image still boots and the network still works.

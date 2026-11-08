# Studio Pi 5 — native ARM build/test machine

`ssh magnus@studio` (key auth, passwordless sudo). This box is the ARM
verification target for build/test evidence; the QEMU guest stays as a
clean-room fallback.

## Hardware / OS

- Raspberry Pi 5 Model B Rev 1.1, 16 GB RAM, 4 cores, NVMe root (181 GB free).
- Debian 13 trixie (full Debian, NOT the Buildroot image): PipeWire 1.4.2 +
  wireplumber + pipewire-pulse running, reachable over SSH (`pactl info`
  works headless).
- **UMC1820 attached** (card 1): PipeWire `alsa_input…multichannel-input`
  s32le **18ch** 48 kHz + `alsa_output…multichannel-output` s24le **20ch**
  48 kHz. Capture proof: `arecord -D hw:1,0 -c 18` returns exactly
  10,368,044 bytes for 3 s (header + 18×4×48000×3). Converter-level proof
  (mapping, takes, latency) runs here, not in the guest.

## Toolchain (apt)

g++ 14.2, cmake 3.31.6, ninja, ccache, libasound2-dev (RtMidi ALSA),
qt6-base-dev, qt6-declarative-dev, qt6-multimedia-dev,
qml6-module-qtquick-controls, qt6-wayland — **Qt 6.8.2**. QML must stay in
the 6.4-compatible subset (guest is 6.4.2); 6.8-only APIs fail there.

## Workflow

```sh
# sync (from macOS repo root; same excludes as the guest flow)
rsync -az --delete --exclude=qemu --exclude=build --exclude=node_modules \
  --exclude=dist --exclude=fab --exclude=.git \
  midibrain/pi midibrain/firmware midibrain/midifx magnus@studio:~/mnky/
# build (ccache warm after the first run; -j4) — FAIL LOUDLY: AND-chain the
# steps or use `set -e`; a `;` chain reports the last command's status and can
# hide a failed build behind a stale binary.
ssh magnus@studio "cd ~/mnky && cmake -S pi -B /tmp/pi-build \
  -DCMAKE_CXX_COMPILER_LAUNCHER=ccache && cmake --build /tmp/pi-build -j4"
# test + offscreen QML
ssh magnus@studio "ctest --test-dir /tmp/pi-build --output-on-failure \
  && QT_QPA_PLATFORM=offscreen /tmp/pi-build/app/mnkybox --unittests"
```

Rules: short SSH commands only (no call over 600 s); start long builds
with nohup + log file and check back — never hold a tool call open on a
build. Nothing commits from the Pi; it is a verification target, and
`~/mnky` is a sync drop (safe to `--delete`).

**NEVER write an image to this Pi's own storage** (no `dd`/`pv`/`bmaptool`
to `/dev/mmcblk0`, `/dev/nvme0n1`, or any partition of them) — it would
brick the build host and every workflow that depends on it. An image is
written to a **spare SD card in a USB reader, from the Mac**; the Pi only
BUILDS the `.img`, it never flashes it.

ARM gate: native configure + `-j4` build clean, full CTest green,
`mnkybox --unittests` green offscreen. After any file-list change, reconfigure
explicitly before trusting green — an existing build dir can link stale
objects that a fresh configure rejects. Set the UMC nodes as PipeWire defaults
before app audio proof: `pactl set-default-sink/source` on the multichannel
nodes.

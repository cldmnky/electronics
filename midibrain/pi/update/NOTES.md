# update/ — Pico 2 (RP2350) firmware-update core

Host-tested update core: strict UF2 validation, positive RP2350 BOOTSEL drive
identification, copy-with-progress, disappearance and link-return verification,
plus the manual `pico-update` CLI. It is a prototype tool, not a manufacturing
or hardware-validation claim. Nothing here has been run against a real Pico 2
in BOOTSEL mode.

## Safety model (refuse by default)

The updater must never write to the Pi's own storage or any other disk.

- **Identification is a whitelist.** A drive is a candidate only when all of
  these match: removable block disk in sysfs, USB VID:PID `2e8a:000f` (RP2350
  bootrom), exact 128 MiB virtual volume size, exactly one mounted `vfat`
  partition at a non-root mount point, `/dev/disk/by-label/RP2350` pointing at
  that mount source, and `INFO_UF2.TXT` containing
  `Model: Raspberry Pi RP2350`. Any ambiguity (two candidates, a lookalike, an
  unreadable signature) is a refusal with a reason.
- **No raw block-device writes.** The bootrom presents a virtual FAT16 volume
  and self-flashes when a `.uf2` file lands on it. `flashUf2()` writes one file
  (`midibrain.uf2`) inside the verified mount point only; it never opens a
  device node, never mounts anything, and never writes outside that directory.
- **Re-verification immediately before the copy.** The `BootselDevice` passed
  in is evidence of an earlier scan, not authority: `flashUf2()` re-runs the
  full scan and requires the same block device, partition, mount point, USB
  serial and size.
- **No success without confirmation.** The copy is reported successful only
  after the BOOTSEL volume disappears (the bootrom accepted the image and
  rebooted). A drive that stays present is a failure even though the bytes were
  written.
- **Explicit CLI confirmation.** `pico-update flash` and `pico-update update`
  refuse without `--yes`; validation and `find` are read-only.

## Verified image facts and primary sources

UF2 family IDs, from pico-sdk **2.3.1**
(`src/common/boot_uf2_headers/include/boot/uf2.h`, commit `079c6f3902…`,
retrieved 2026-11-05, SHA-256 `51843536e7ae3f0577b7c78150d30c33c2a478b52082a8858b119d54acfe4ccd`):

| Family | ID | Accepted |
|---|---|---|
| RP2040 | `0xe48bff56` | **no** — refused explicitly (this value was wrongly named "RP2350" in the brief) |
| RP2350-E10 absolute marker | `0xe48bff57` | only in the exact picotool marker shape, at most once, first block |
| RP2350 ARM-S | `0xe48bff59` | **yes** — every main-flash block |
| RP2350 RISC-V | `0xe48bff5a` | **no** |
| RP2350 ARM-NS | `0xe48bff5b` | **no** |

The project's own `firmware/pico/build/midibrain.uf2`
(`picotool uf2 convert … --family rp2350-arm-s --platform rp2350 --abs-block`)
is the golden layout the tests reproduce: **1** absolute marker block followed
by **157** contiguous `0xe48bff59` pages from `0x10000000` to `0x10009d00`.
`picotool info` reports family `rp2350-arm-s`, target RP2350. The marker is the
RP2350-E10 workaround from picotool's `elf2uf2.cpp` `gen_abs_block()` (flags
`0xa000`, address `0x10ffff00`, 256 × `0xef`, `0x9957e304` extension tag); it
carries no flash payload. A second marker or a payload-bearing absolute block is
corrupt.

BOOTSEL identity, from the RP2350 A4 bootrom source
(`github.com/raspberrypi/pico-bootrom-rp2350`, commit `c6cdb1711f…`, retrieved
2026-11-05):

| Fact | Source | SHA-256 |
|---|---|---|
| VID:PID `2e8a:000f` | `src/nsboot/nsboot_usb_client.h` | `1917f39f9f55b4f5198c53f3190b14d6800cc0f6e4467872687fa4788fa846f3` |
| 128 MiB virtual volume | `src/nsboot/usb_virtual_disk.h` (`VOLUME_SIZE`) | `38eb052521b67374a153792c66dbda3637d1b506aa53eee20130e20321f7996c` |
| default label `RP2350` | `src/nsboot/usb_virtual_disk.c` (`DEFAULT_VOLUME_LABEL`) | `26d6deeb41730aa4b5bc376a1d27deb17fdf639e4f9867e2a5a68427794b2c5e` |
| `INFO_UF2.TXT` `Model: Raspberry Pi RP2350` | `src/nsboot/info_uf2_txt.h` + defaults in `usb_virtual_disk.c` | `598c7becfc354e51086d5b7d02de4dbce7436093966c8a27eeec11dbcf610703` |

The bootrom source was fetched to a temporary directory for verification and is
not committed (third-party document policy). The family value in the original
brief (`0xe48bff56` for RP2350) was corrected after this check and is covered by
`testUf2FamilyRefusals()`.

## API (`update.hpp`)

The Tools UI binds to exactly these entry points in `mnky::update`:

```cpp
Uf2Validation    validateUf2(const std::string& path);
BootselLookup    findBootsel();                       // device or reason
bool             rebootToBootsel(link::PicoLink& link); // EV_REBOOT usbBoot=1
FlashResult      flashUf2(const BootselDevice& device, const std::string& path,
                          const FlashProgressFn& progress);
WaitForPicoResult waitForPico(link::PicoLink& link, std::uint64_t timeoutMs);
```

Supporting headers: `uf2.hpp` (validation result/constants), `bootsel.hpp`
(device/lookup and the `findBootselAt(BootselPaths)` test seam), `flash.hpp`
(progress/result and the `FlashHooks` test seam). `flashUf2` also has a
`FlashHooks` overload; production callers use the 3-argument form.

`rebootToBootsel()` and `waitForPico()` touch the `PicoLink` directly, so they
must run on the thread that owns it (the link core is single-threaded by
contract).

Notable behaviour:

- `rebootToBootsel()` only queues `EV_REBOOT` (mode 1). The caller must keep
  polling the `PicoLink` so the frame is transmitted.
- `waitForPico()` polls the link, sends `EV_STATUS_REQ`, requires a status
  answer with `mb::STATUS_LINK_UP`, and accepts it only when the Pico session
  changed, the link was down at entry, or the status differs from the entry
  snapshot. Non-tick events are returned in `WaitForPicoResult::events`; tick
  events are dropped so a long wait cannot overflow the link's event queue.
- **Firmware version:** the wire protocol has no version field
  (`docs/protocol.md` 0x21 is uptime/ticks/counters only), so
  `firmwareVersionExposed` is always false and no version string is invented.
  The CLI reports `firmware version: not exposed by the link protocol`.

## Manual CLI

```sh
# read-only checks
pico-update validate firmware/pico/build/midibrain.uf2
pico-update find

# already in BOOTSEL (volume must be mounted)
pico-update flash firmware/pico/build/midibrain.uf2 --yes

# running Pico: reboot into BOOTSEL, copy, wait for it to return
pico-update update firmware/pico/build/midibrain.uf2 --yes --spidev /dev/spidev0.0
```

`update` first looks for an already-present BOOTSEL drive; only if none is
found does it try to hand the running Pico over. The BOOTSEL volume must already
be mounted (udisks/systemd does this when the drive appears); this tool
deliberately never mounts media or raw-writes a disk.

## Tests

`update/test_update.cpp` covers:

- good images (plain and the golden 1-marker + 157-block `midibrain.uf2`
  layout), file-path validation, missing files;
- refusals for RP2040/RISC-V/ARM-NS/unknown families, magic, flags, payload
  size, block order/count, address continuity, marker shape/order/count,
  truncation, empty and oversized images;
- BOOTSEL identification and refusal of lookalikes: RP2040 bootloader VID:PID,
  wrong size, non-removable disk, wrong/missing volume label, wrong/missing
  `INFO_UF2.TXT`, missing mount, `ext4` mount, root mount point, two identified
  drives, changed mount/identity;
- flash success (progress monotonic, exact bytes on the fake volume, drive
  disappearance required), invalid image, changed mount, missing signature,
  unconfirmed disappearance, retry overwrite;
- full orchestration with a switchable fake Pico transport: validate → reboot
  BOOTSEL → copy → disappearance → new Pico session → status handshake; plus a
  timeout when the Pico never returns.

Everything uses a temporary fake sysfs/mounts/dev tree and a fake link; no test
opens a real block device or touches the host's disks.

```sh
# standalone
cd midibrain/pi/update
cmake -S . -B /tmp/update-build && cmake --build /tmp/update-build -j
ctest --test-dir /tmp/update-build --output-on-failure

# root build
cmake -S midibrain/pi -B /tmp/pi-build && cmake --build /tmp/pi-build -j
ctest --test-dir /tmp/pi-build --output-on-failure
```

## Pending / not verified

- **No hardware run.** The RP2350 bootrom strings, volume label, size and USB
  IDs are from the A4 bootrom source and pico-sdk, not measured on the target
  Pico 2. First-article verification must confirm `findBootsel()` on the real
  device (and the exact `INFO_UF2.TXT` contents).
- **Mount and udev assumption.** A headless Pi without an automounter will
  report `not mounted`; a system without `/dev/disk/by-label` symlinks will
  report the missing label. Both are refusals by design. No mount/udisks helper
  is included.
- **No read-back checksum.** The bootrom tears down the volume as it flashes;
  completion is proven by the volume disappearing plus the link return, not by
  reading the file back.
- **Partial-copy failure mode.** If a write fails midway, a partial `.uf2` can
  remain on the BOOTSEL volume and the bootrom may have flashed some pages. The
  reporter returns failure; recovery is to re-run the update (the bootrom stays
  in BOOTSEL unless it accepted a complete image).
- **UI wiring.** `pi/app` owns the Tools screen; this module only supplies the
  API and the CLI. No progress-on-screen or version display claim is made here.
- **SPI link transport.** `LinuxSpidevTransport` is the existing link module's
  unvalidated transport; `waitForPico()` inherits its timing limits. No
  signal-integrity or timing claim.

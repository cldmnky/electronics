# Stage A — headless AArch64 Debian VM under QEMU/HVF

Scope of this stage: boot the stock Debian 12 arm64 cloud image headless with a
cloud-init seed and prove SSH, disk growth, CPU/RAM and guest internet access.
Nothing is installed in the guest, no repo checkout, no build. That is stage B.

The VM has **no emulated GPU or display** on purpose: `-display none`, serial
console only. Future graphics work happens inside the guest with Xvfb and
screenshots, not through a QEMU video device.

## Host environment (measured)

| Item | Value |
| --- | --- |
| Hardware | Apple M4 Pro, 14 CPUs, 48 GB RAM |
| macOS | 26.7.1 (arm64) |
| QEMU | 11.1.3 (`/opt/homebrew/bin/qemu-system-aarch64`, brew) |
| EDK2 firmware | `/opt/homebrew/share/qemu/edk2-aarch64-code.fd`, `edk2-stable202408-prebuilt` (reported by the guest serial log) |
| xorriso | 1.5.8.pl02 (installed via brew during this stage) |

## Guest environment (measured)

| Item | Value |
| --- | --- |
| Image | `debian-12-generic-arm64.qcow2` from `https://cloud.debian.org/images/cloud/bookworm/latest/` (417 MiB / 437,649,408 bytes, matches upstream `Content-Length`) |
| OS | Debian GNU/Linux 12 (bookworm) |
| Kernel | `6.1.0-54-arm64` (Debian `6.1.190-1`, 2026-10-16) |
| cloud-init | 22.4.2, `status: done`, `DataSourceNoCloud [seed=/dev/vdb][dsmode=net]` |
| Boot time | SSH reachable well inside the 240 s budget (guest up <1 min) |

## Verification results

```
$ ssh -p 2223 ... mnky@localhost 'uname -a; df -h /; nproc; free -g; ...'
Linux mnkybox 6.1.0-54-arm64 #1 SMP Debian 6.1.190-1 (2026-10-16) aarch64 GNU/Linux
/dev/vda1        20G 1002M   18G   6% /
8
               total        used        free      shared  buff/cache   available
Mem:               3           0           3           0           0           3
Swap:              0           0           0
```

| Check | Expected | Observed |
| --- | --- | --- |
| `uname -m` | aarch64 | `aarch64` |
| `df -h /` | ~20G | `/dev/vda1 20G, 18G avail` (cloud-init growpart expanded) |
| `nproc` | 8 | `8` |
| `free -g` | ~4G | `3.8Gi` total (4G configured) |
| passwordless sudo | works | `sudo -n true` OK for user `mnky` |
| guest internet | reachable | `curl -sI https://github.com` → `HTTP/2 200` |
| accelerator | HVF | guest booted with `-accel hvf`; no TCG fallback used |
| block devices in guest | disk + seed CD | `vda` 20G, `vdb` 370K label `CIDATA` |

## Files

Tracked (keeper) files in `midibrain/pi/qemu/`:

- `run-vm.sh` — boots the VM in the background (nohup), serial console to `vm.log`, pid in `vm.pid`.
- `seed/user-data` — cloud-init `#cloud-config`: user `mnky`, `/bin/bash`, passwordless sudo, host SSH key, `manage_etc_hosts: true`, no `packages:` section.
- `seed/meta-data` — `instance-id: mnkybox-01`, `local-hostname: mnkybox`.
- `.gitignore` — keeps the disk image, seed ISO, log and pidfile out of git.
- `STAGE-A.md` — this file.

Local, untracked-by-design artifacts (all reproducible from the commands below):

- `debian.qcow2` — 433 MiB on host, 20 GiB virtual.
- `seed.iso` — 370 KiB, volume label `CIDATA`.
- `vm.log`, `vm.pid`, `curl.log`.

## Known-good command lines

Download the cloud image (skipped when a complete image is already present):

```
curl -fL -o midibrain/pi/qemu/debian.qcow2 \
  https://cloud.debian.org/images/cloud/bookworm/latest/debian-12-generic-arm64.qcow2
qemu-img resize midibrain/pi/qemu/debian.qcow2 20G
```

Build the seed ISO (rebuild after any edit of `seed/user-data`):

```
cd midibrain/pi/qemu
xorriso -as mkisofs -volid CIDATA -joliet -rock -o seed.iso seed/
xorriso -indev seed.iso -pvd_info | grep -i 'volume id'   # -> CIDATA
```

Boot, connect, stop:

```
midibrain/pi/qemu/run-vm.sh
ssh -p 2223 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null mnky@localhost
kill "$(cat midibrain/pi/qemu/vm.pid)"
```

The exact QEMU line `run-vm.sh` starts:

```
qemu-system-aarch64 -M virt -accel hvf -cpu host -smp 8 -m 4G \
  -bios /opt/homebrew/share/qemu/edk2-aarch64-code.fd \
  -drive file=debian.qcow2,if=none,id=hd -device virtio-blk-pci,drive=hd \
  -drive file=seed.iso,media=cdrom,readonly=on \
  -netdev user,id=net0,hostfwd=tcp::2223-:22 -device virtio-net-pci,netdev=net0 \
  -serial mon:stdio -display none
```

## Deviations from the brief, and why

1. The downloaded file is named `debian.qcow2` (the URL basename was renamed) so it matches the `-drive file=debian.qcow2` argument in the boot line.
2. The image is 417 MiB, not the ~280 MB the brief expected. This was checked against the server's `Content-Length` (437,649,408 bytes, exact match) and `qemu-img info` reports a valid, uncorrupted qcow2 — the image is current bookworm, not a truncated download.
3. `xorriso` is called with `-joliet -rock` in addition to `-volid CIDATA`, so the ISO carries Rock Ridge/Joliet names; the NoCloud seed was still read from `/dev/vdb`.
4. `seed/meta-data` also sets `local-hostname: mnkybox`, matching the `hostname` in `user-data`.
5. `run-vm.sh` adds preflight checks before starting QEMU: firmware present, disk/ISO present, no live pidfile, host port 2223 free. These catch a stale VM or a port conflict instead of failing inside `vm.log`.
6. `-smp 8` is kept exactly as briefed on a 14-core host; the guest reports `nproc` = 8.

## Known limitations to carry into stage B

- `-serial mon:stdio` multiplexes the QEMU monitor onto the same stdio that is redirected to `vm.log`, so the monitor is not interactive while the VM is backgrounded. Serial output (and boot errors) are in `vm.log`. If an interactive monitor is needed later, add a `-monitor unix:...` socket; do not switch to a display.
- The guest has no swap (`Swap: 0B`) and 3.8 GiB RAM. Fine for stage A; a large parallel build in stage B may need a swapfile inside the guest.
- `seed.iso` is a snapshot of `seed/user-data`; editing the seed requires rebuilding the ISO and rebooting the VM.
- The VM currently boots from a writable qcow2 with no snapshot/backing file, so stage B changes persist. Booting stage B in a disposable overlay (`qemu-img create -b debian.qcow2 -F qcow2 overlay.qcow2`) is worth deciding before installing packages.

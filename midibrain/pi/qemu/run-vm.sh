#!/usr/bin/env bash
# Boot the headless AArch64 Debian VM used for mnkybox Linux verification.
#
# Stage A only: this script boots the stock Debian cloud image with a
# cloud-init seed and leaves a reachable SSH port on localhost. Nothing is
# provisioned beyond the seed; dev packages, repo checkout and builds are
# later stages.
#
# The guest has no display: QEMU runs with -display none and the serial
# console is captured in vm.log. Graphics work is done inside the guest with
# Xvfb, so no emulated GPU/display device is configured here.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
disk="$here/debian.qcow2"
seed_iso="$here/seed.iso"
log="$here/vm.log"
pidfile="$here/vm.pid"
ssh_port=2223

die() { printf 'run-vm.sh: %s\n' "$*" >&2; exit 1; }

command -v qemu-system-aarch64 >/dev/null || die "qemu-system-aarch64 not found (brew install qemu)"

efi="$(brew --prefix)/share/qemu/edk2-aarch64-code.fd"
[[ -f $efi ]] || die "EDK2 AArch64 firmware missing: $efi (ls \$(brew --prefix)/share/qemu/)"

[[ -s $disk ]] || die "disk image missing: $disk (see README/STAGE-A.md for the download)"
[[ -s $seed_iso ]] || die "seed ISO missing: $seed_iso (build with the xorriso command in STAGE-A.md)"

# One VM at a time: a live pidfile means the port is already claimed.
if [[ -f $pidfile ]] && kill -0 "$(cat "$pidfile")" 2>/dev/null; then
  die "VM already running (pid $(cat "$pidfile")); stop it before booting again"
fi

# Refuse to start when something else already listens on the forwarded port,
# otherwise QEMU aborts on bind and the failure only shows up in vm.log.
if (exec 3<>"/dev/tcp/127.0.0.1/$ssh_port") 2>/dev/null; then
  exec 3>&-
  die "host port $ssh_port is already in use"
fi

nohup qemu-system-aarch64 \
  -M virt \
  -accel hvf \
  -cpu host \
  -smp 8 \
  -m 4G \
  -bios "$efi" \
  -drive file="$disk",if=none,id=hd \
  -device virtio-blk-pci,drive=hd \
  -drive file="$seed_iso",media=cdrom,readonly=on \
  -netdev user,id=net0,hostfwd=tcp::"$ssh_port"-:22 \
  -device virtio-net-pci,netdev=net0 \
  -serial mon:stdio \
  -display none \
  >"$log" 2>&1 &

echo $! >"$pidfile"
printf 'VM pid %s, serial console in %s\n' "$(cat "$pidfile")" "$log"
printf 'SSH once cloud-init has run: ssh -p %s -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null mnky@localhost\n' "$ssh_port"

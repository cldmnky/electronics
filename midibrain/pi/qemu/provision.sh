#!/usr/bin/env bash
# Provision the mnkybox Debian 12 arm64 guest for Linux builds of midibrain/pi.
#
# Stage B keeper. Run it from the host after `run-vm.sh` and once SSH answers:
#
#   midibrain/pi/qemu/run-vm.sh
#   midibrain/pi/qemu/provision.sh          # this script (idempotent)
#
# This script only provisions the guest: one apt-get install, the ALSA dummy
# device, Xvfb on :99 for later headless UI work, and a versions header. The
# repo snapshot, cmake configure, target build and ctest run are documented (with
# measured results) in STAGE-B.md; they are not repeated here because the source
# tree and build directory are per-stage inputs, not guest provisioning.
#
# Versions this was verified against (Debian 12 bookworm, kernel 6.1.0-54-arm64):
#   gcc 12.2.0  cmake 3.25.1  git 2.39.5  pkg-config 1.8.1  rsync 3.2.7
#   alsa-utils 1.2.8  libasound2-dev 1.2.8+b1  xvfb 21.1.7
#
# snd-dummy is NOT built into this kernel (# CONFIG_SND_DUMMY is not set), so the
# modprobe below fails by design on this image; the failure is recorded and the
# script continues. Sound-device work is stage C.
set -euo pipefail

ssh_port="${MNKY_SSH_PORT:-2223}"
guest="${MNKY_SSH_USER:-mnky}@localhost"

# Every ssh/rsync in this project is non-interactive: never prompt, never hang.
# UserKnownHostsFile=/dev/null keeps the throwaway VM from polluting known_hosts.
ssh_opts=(-o BatchMode=yes -o ConnectTimeout=10 -o StrictHostKeyChecking=no
          -o UserKnownHostsFile=/dev/null)

# JUCE 9.0.5 on Linux needs ALSA (audio_devices), X11 + freetype/fontconfig
# (gui_basics), and Mesa GL headers; cmake/gcc/git/pkg-config are the build
# toolchain. rsync is required by the repo-snapshot step in STAGE-B.md and is
# absent from the stock cloud image, so it is installed here too.
packages=(
  build-essential cmake git pkg-config rsync
  libasound2-dev alsa-utils libjack-jackd2-dev
  libfreetype6-dev libfontconfig1-dev libgl1-mesa-dev
  libx11-dev libxcomposite-dev libxcursor-dev libxext-dev libxinerama-dev
  libxrandr-dev libxrender-dev libcurl4-openssl-dev
  xvfb x11-apps x11-utils
)

guest_run() {
  ssh "${ssh_opts[@]}" -p "$ssh_port" "$guest" "$@"
}

guest_run true || { printf 'provision.sh: no SSH answer on %s (start the VM with run-vm.sh)\n' "$ssh_port" >&2; exit 1; }

# --- Packages: one update, one install ---------------------------------------
guest_run 'sudo -n env DEBIAN_FRONTEND=noninteractive apt-get update -qq'
guest_run "sudo -n env DEBIAN_FRONTEND=noninteractive apt-get install -y -qq ${packages[*]}"

# --- ALSA dummy device -------------------------------------------------------
# Expected on the stock arm64 cloud kernel to fail: the module is not shipped.
if guest_run 'sudo -n modprobe snd-dummy 2>&1'; then
  printf 'snd-dummy: loaded\n'
else
  printf 'snd-dummy: not available in this kernel (CONFIG_SND_DUMMY not set) — continuing\n' >&2
fi
guest_run 'aplay -l 2>&1 || true'

# --- Xvfb on :99 -------------------------------------------------------------
# No QEMU display device exists (stage A kept -display none), so the guest has no
# X server. Later stages start Xvfb themselves for UI screenshots; starting it
# here keeps one display number for every stage.
guest_run 'pgrep -x Xvfb >/dev/null || (nohup Xvfb :99 -screen 0 1024x600x24 -nolisten tcp >/tmp/xvfb.log 2>&1 & echo "Xvfb started on :99")'

# --- Versions header ---------------------------------------------------------
printf '\n--- guest ---\n'
guest_run 'uname -a; gcc --version | head -1; cmake --version | head -1; git --version; pkg-config --version; rsync --version | head -1'
printf '\n--- packages installed by this script ---\n'
guest_run "dpkg -l ${packages[*]} 2>/dev/null | grep -E '^ii'"
printf '\n--- ALSA ---\n'
guest_run 'aplay -l 2>&1 || true; ls /proc/asound/cards 2>&1 || true'

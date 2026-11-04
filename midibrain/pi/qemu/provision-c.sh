#!/usr/bin/env bash
# Stage C guest deltas on top of provision.sh (Debian 12 arm64, kernel 6.1.0-54-arm64).
#
# Run it from the host after `run-vm.sh` + `provision.sh` and once SSH answers:
#
#   midibrain/pi/qemu/provision-c.sh        # this script (idempotent)
#
# What stage C needs that stage B did not install or configure, and why:
#
#   ccache, xdotool, netpbm   the Linux fix loop and the Xvfb screenshot pipe
#                             (`xwd | xwdtopnm | pnmtojpeg`)
#   libxi-dev                 JUCE 9.0.5 compiles with JUCE_USE_XINPUT=1 on Linux and
#                             juce_gui_basics.h includes <X11/extensions/XInput2.h>;
#                             stage B's package list lacks it, so the GUI target cannot
#                             compile without it
#   group audio               /dev/snd/* is root:audio 0660; without membership
#                             `aplay -l` reports "no soundcards found" and
#                             `aconnect -l` fails with "open /dev/snd/seq: Permission denied"
#   snd-aloop, snd-seq-dummy  the kernel has no snd-dummy (# CONFIG_SND_DUMMY is not set);
#                             snd-aloop provides card 0 "Loopback", snd-seq-dummy provides
#                             the ALSA sequencer with client 14 "Midi Through"
#   /swapfile (4 GiB)         4 GiB RAM is not enough for `-j8` compiling JUCE's unity
#                             translation units: the guest OOM killer terminated cc1plus
#   Xvfb :99                  the guest has no display device (stage A: -display none)
#
# Session-only state, deliberately not persisted: the kernel modules (no
# /etc/modules entry), the swapfile (no /etc/fstab entry) and the group change for
# the already running login. A reboot needs this script again.
#
# Verified against: ccache 4.8+really4.7.5-1, xdotool 1:3.20160805.1-5,
# netpbm 2:11.01.00-2, libxi-dev 2:1.8-1+b1. See STAGE-C1.md for the measured run.
set -euo pipefail

ssh_port="${MNKY_SSH_PORT:-2223}"
guest="${MNKY_SSH_USER:-mnky}@localhost"

ssh_opts=(-o BatchMode=yes -o ConnectTimeout=10 -o StrictHostKeyChecking=no
          -o UserKnownHostsFile=/dev/null)

# libxi-dev pulls the X11 input extension headers; the rest are stage C tools.
packages=(ccache xdotool netpbm libxi-dev)

guest_run() {
  ssh "${ssh_opts[@]}" -p "$ssh_port" "$guest" "$@"
}

guest_run true || { printf 'provision-c.sh: no SSH answer on %s (start the VM with run-vm.sh)\n' "$ssh_port" >&2; exit 1; }

# --- Packages ----------------------------------------------------------------
guest_run 'sudo -n env DEBIAN_FRONTEND=noninteractive apt-get update -qq'
guest_run "sudo -n env DEBIAN_FRONTEND=noninteractive apt-get install -y -qq ${packages[*]}"

# --- ALSA access -------------------------------------------------------------
# Idempotent: usermod -aG is a no-op when the group is already listed. Takes
# effect for the *next* SSH session, not for the current one.
guest_run 'id -nG | tr " " "\n" | grep -qx audio || sudo -n usermod -aG audio "$(id -un)"'

# --- ALSA kernel modules -----------------------------------------------------
guest_run 'sudo -n modprobe snd-aloop && sudo -n modprobe snd-seq-dummy'

# --- Swap --------------------------------------------------------------------
# Only create/swap on when no swap device is active, so re-running is free.
guest_run 'if ! /sbin/swapon --show | grep -q .; then
             sudo -n fallocate -l 4G /swapfile &&
             sudo -n chmod 600 /swapfile &&
             sudo -n mkswap /swapfile >/dev/null &&
             sudo -n /sbin/swapon /swapfile
           fi'

# --- Xvfb :99 ----------------------------------------------------------------
# Pidfile method used by every stage C screenshot: one Xvfb, pid in /tmp/xvfb.pid,
# server log in /tmp/xvfb.log. `xwd -root` needs a live server on :99.
guest_run 'mkdir -p /tmp/.X11-unix; pgrep -x Xvfb >/dev/null || {
             nohup Xvfb :99 -screen 0 1024x600x24 -nolisten tcp >/tmp/xvfb.log 2>&1 &
             echo $! > /tmp/xvfb.pid
           }
           pgrep -x Xvfb > /tmp/xvfb.pid'

# --- Verification ------------------------------------------------------------
printf '\n--- versions ---\n'
guest_run 'ccache --version | head -1; xdotool --version; pnmtopng -version 2>&1 | head -1'
printf '\n--- groups ---\n'
guest_run 'getent group audio'
printf '\n--- swap ---\n'
guest_run '/sbin/swapon --show; free -m | tail -1'
printf '\n--- ALSA cards ---\n'
guest_run 'cat /proc/asound/cards; aplay -l 2>&1 | head -8'
printf '\n--- ALSA sequencer ---\n'
guest_run 'aconnect -l 2>&1'
printf '\n--- modules ---\n'
guest_run 'lsmod | grep -E "^snd_(aloop|seq|seq_dummy|pcm|timer)" '
printf '\n--- Xvfb ---\n'
guest_run 'pgrep -a Xvfb; DISPLAY=:99 xdpyinfo | head -3'

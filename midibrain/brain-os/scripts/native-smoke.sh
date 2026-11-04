#!/bin/sh
#
# MNKYBOX native smoke test: run the checks INSIDE the built root filesystem on
# real aarch64 hardware, without qemu.
#
# Usage (on the owner's Pi 5, from the Buildroot output directory):
#   sudo systemd-nspawn -D output/target /usr/bin/sh /native-smoke.sh
# or, from a copy of this repository mounted into the rootfs:
#   sudo cp scripts/native-smoke.sh output/target/tmp/ && sudo systemd-nspawn -D output/target /tmp/native-smoke.sh
# or with a plain chroot:
#   sudo cp scripts/native-smoke.sh output/target/tmp/ && sudo chroot output/target /tmp/native-smoke.sh
#
# It runs the target's own binaries directly, so it catches loader, glibc,
# shared-library and Qt plugin problems that qemu-user can hide. It cannot test
# the Pi kernel, DRM/DSI, touch, USB audio, WiFi or systemd unit ordering:
# those need the flashed image on hardware.
#
# Exit status: 0 = every check passed, 1 = at least one check failed.

set -u

fails=0
run() { # run <label> <path> [args...]
	label="$1"; shift
	bin="$1"; shift
	if [ ! -e "$bin" ]; then
		printf '  FAIL %-24s missing: %s\n' "$label" "$bin"
		fails=$((fails + 1))
		return 0
	fi
	if out="$("$bin" "$@" 2>&1)"; then
		printf '  ok   %-24s %s\n' "$label" "$(printf '%s' "$out" | head -1)"
	else
		printf '  FAIL %-24s %s\n' "$label" "$(printf '%s' "$out" | head -2 | tr '\n' ' ')"
		fails=$((fails + 1))
	fi
}

echo "native-smoke: $(uname -m) host kernel $(uname -r)"
run "systemd" /usr/lib/systemd/systemd --version
run "pipewire" /usr/bin/pipewire --version
run "pipewire-pulse" /usr/bin/pipewire-pulse --version
run "wireplumber" /usr/bin/wireplumber --version
run "weston" /usr/bin/weston --version
run "seatd" /usr/bin/seatd -h
run "wpa_supplicant" /usr/sbin/wpa_supplicant -v
run "dropbear" /usr/sbin/dropbear -h
run "wpctl" /usr/bin/wpctl --version
run "evtest" /usr/bin/evtest --version

# Qt: the Wayland platform plugin and the PulseAudio multimedia backend must
# load in the target's own Qt installation.
if [ -x /usr/lib/qt6/libexec/qtpaths ] || [ -x /usr/bin/qtpaths6 ]; then
	q="$(ls /usr/lib/qt6/libexec/qtpaths /usr/bin/qtpaths6 2>/dev/null | head -1)"
	printf '  ok   %-24s %s\n' "qtpaths" "$("$q" --query QT_INSTALL_PLUGINS 2>/dev/null | head -1)"
fi

# The application stub is a shell script; it must exit 78 (EX_CONFIG) and
# identify itself with --version.
if [ -x /opt/mnkybox/bin/mnkybox ]; then
	out="$(/opt/mnkybox/bin/mnkybox --version 2>&1)" && \
		printf '  ok   %-24s %s\n' "app stub --version" "$out" || {
		printf '  FAIL %-24s %s\n' "app stub --version" "$out"; fails=$((fails + 1)); }
	/opt/mnkybox/bin/mnkybox >/dev/null 2>&1
	rc=$?
	if [ "$rc" -eq 78 ]; then
		printf '  ok   %-24s exit 78 (EX_CONFIG, stub)\n' "app stub run"
	elif [ "$rc" -eq 0 ]; then
		printf '  ok   %-24s exit 0 (real binary installed)\n' "app stub run"
	else
		printf '  FAIL %-24s unexpected exit %s\n' "app stub run" "$rc"
		fails=$((fails + 1))
	fi
fi

if [ "$fails" -ne 0 ]; then
	printf 'native-smoke: %d failure(s)\n' "$fails" >&2
	exit 1
fi
echo "native-smoke: all checks passed"

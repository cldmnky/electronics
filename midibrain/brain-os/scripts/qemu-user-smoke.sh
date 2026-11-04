#!/bin/sh
#
# MNKYBOX qemu-user smoke test: execute the target's aarch64 user-space
# binaries on the build host with qemu-aarch64-static.
#
# Usage: ./qemu-user-smoke.sh [TARGET_DIR]
#   TARGET_DIR defaults to $TARGET_DIR (set by Buildroot) or output/target.
#   QEMU_AARCH64 overrides the qemu binary (default: qemu-aarch64-static).
#
# What this proves: the binaries in the root filesystem are aarch64 ELF, their
# shared libraries resolve, and they start far enough to print a version. That
# catches a broken toolchain, a missing library and a wrong architecture before
# an image is written to a card.
#
# What this CANNOT prove, and does not try to:
#   * nothing of the Pi 5 kernel is exercised (qemu-user uses the host kernel);
#   * no DRM/KMS, DSI panel, touch, GPU (V3D), DMA or audio hardware exists;
#   * systemd cannot run as PID 1 in a chroot, so unit ordering, seatd,
#     Weston, PipeWire and the first-boot unit are NOT tested here;
#   * timing and latency are meaningless under emulation.
# Use qemu-system-aarch64 with the built kernel for anything beyond user space,
# and real hardware for the panel, touch, audio and WiFi.
#
# Exit status: 0 = every check passed, 1 = at least one check failed.

set -eu

BOARD_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/../board/mnkybox" && pwd)"

TARGET_DIR="${1:-${TARGET_DIR:-}}"
if [ -z "${TARGET_DIR}" ]; then
	TARGET_DIR="$(CDPATH= cd -- "${BOARD_DIR}/../../../../output/target" 2>/dev/null && pwd || true)"
fi
[ -n "${TARGET_DIR}" ] && [ -d "${TARGET_DIR}" ] || {
	echo "qemu-user-smoke: no target directory; build the image first" >&2
	exit 1
}

QEMU="${QEMU_AARCH64:-qemu-aarch64-static}"
if ! command -v "${QEMU}" >/dev/null 2>&1; then
	echo "qemu-user-smoke: ${QEMU} not found." >&2
	echo "  Install qemu-user-static (or set QEMU_AARCH64). Example:" >&2
	echo "  https://github.com/multiarch/qemu-user-static/releases" >&2
	echo "  SKIPPED: user-space smoke test not run." >&2
	exit 0
fi

fails=0
run() { # run <label> <path> [args...]
	label="$1"; shift
	bin="$1"; shift
	if [ ! -e "${TARGET_DIR}${bin}" ]; then
		printf '  FAIL %-28s missing: %s\n' "${label}" "${bin}"
		fails=$((fails + 1))
		return 0
	fi
	if out="$("${QEMU}" -L "${TARGET_DIR}" "${TARGET_DIR}${bin}" "$@" 2>&1)"; then
		printf '  ok   %-28s %s\n' "${label}" "$(printf '%s' "${out}" | head -1)"
	else
		printf '  FAIL %-28s %s\n' "${label}" "$(printf '%s' "${out}" | head -2 | tr '\n' ' ')"
		fails=$((fails + 1))
	fi
}

echo "qemu-user-smoke: ${TARGET_DIR} with $(basename "${QEMU}")"
run "systemd" /usr/lib/systemd/systemd --version
run "pipewire" /usr/bin/pipewire --version
run "wireplumber" /usr/bin/wireplumber --version
run "weston" /usr/bin/weston --version
run "seatd" /usr/bin/seatd -h
run "wpa_supplicant" /usr/sbin/wpa_supplicant -v
run "dropbear" /usr/sbin/dropbear -h
run "busybox" /bin/busybox true

# The application stub is a shell script, so it needs the target's shell.
if [ -e "${TARGET_DIR}/opt/mnkybox/bin/mnkybox" ]; then
	if out="$("${QEMU}" -L "${TARGET_DIR}" "${TARGET_DIR}/bin/busybox" sh \
			/opt/mnkybox/bin/mnkybox --version 2>&1)"; then
		printf '  ok   %-28s %s\n' "app stub" "${out}"
	else
		printf '  FAIL %-28s %s\n' "app stub" "${out}"
		fails=$((fails + 1))
	fi
fi

if [ "${fails}" -ne 0 ]; then
	printf 'qemu-user-smoke: %d failure(s)\n' "${fails}" >&2
	exit 1
fi
echo "qemu-user-smoke: all user-space checks passed"

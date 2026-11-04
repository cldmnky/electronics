#!/bin/sh
#
# MNKYBOX image acceptance checks (no Raspberry Pi hardware needed).
#
# Usage: ./check-image.sh [BINARIES_DIR]
#   BINARIES_DIR defaults to $BINARIES_DIR from Buildroot, or to the
#   "output/images" directory of the Buildroot tree this board directory
#   belongs to when run by hand.
#
# What this proves:
#   * sdcard.img has the required partition layout (p1 FAT32 128 MiB, p2 ext4 1 GiB)
#   * the FAT boot partition carries the boot files, config.txt and cmdline.txt
#     with the agreed contents, and no UART console
#   * the ext4 root filesystem runs systemd, has the required units enabled,
#     the BCM43455 WiFi firmware, PipeWire's libjack, libasound, and no JACK2
#   * ELF dependencies of any prebuilt application binaries resolve
#
# What it cannot prove: that the image boots, that the DSI panel and touch work,
# that audio runs at the required latency, or that WiFi associates. See README.md.

set -eu

BOARD_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
: "${BOARD_DIR:?}"

BINARIES_DIR="${1:-${BINARIES_DIR:-}}"
if [ -z "${BINARIES_DIR}" ]; then
	BINARIES_DIR="$(CDPATH= cd -- "${BOARD_DIR}/../../../../.." 2>/dev/null && pwd)/output/images"
fi
[ -d "${BINARIES_DIR}" ] || { echo "check-image: no ${BINARIES_DIR}" >&2; exit 1; }

HOST_DIR="${HOST_DIR:-$(CDPATH= cd -- "${BINARIES_DIR}/../host" 2>/dev/null && pwd)}"
IMG="${BINARIES_DIR}/sdcard.img"
ROOTFS="${BINARIES_DIR}/rootfs.ext4"

DEBUGFS="${HOST_DIR}/sbin/debugfs"
[ -x "${DEBUGFS}" ] || DEBUGFS=debugfs
MTOOLS="${HOST_DIR}/bin"
export PATH="${MTOOLS}:${PATH}"
export MTOOLS_SKIP_CHECK=1

fails=0
ok()   { printf '  ok   %s\n' "$*"; }
bad()  { printf '  FAIL %s\n' "$*"; fails=$((fails + 1)); }
head1() { printf '\n%s\n' "$*"; }

# ------------------------------------------------------------------ partitions
# Read one byte of the image as an unsigned decimal number.
byte_at() {
	dd if="$1" bs=1 skip="$2" count=1 2>/dev/null | od -An -tu1 -v | tr -d ' \n'
}

# Partition entry i (0-based) fields, from the MBR at offset 446.
part_field() { # img i {status|type|start|sectors}
	_i="$2"
	_off=$((446 + 16 * _i))
	case "$3" in
	status)  byte_at "$1" "${_off}" ;;
	type)    byte_at "$1" $((_off + 4)) ;;
	start)
		b0=$(byte_at "$1" $((_off + 8)));  b1=$(byte_at "$1" $((_off + 9)))
		b2=$(byte_at "$1" $((_off + 10))); b3=$(byte_at "$1" $((_off + 11)))
		echo $((b0 + b1 * 256 + b2 * 65536 + b3 * 16777216)) ;;
	sectors)
		b0=$(byte_at "$1" $((_off + 12))); b1=$(byte_at "$1" $((_off + 13)))
		b2=$(byte_at "$1" $((_off + 14))); b3=$(byte_at "$1" $((_off + 15)))
		echo $((b0 + b1 * 256 + b2 * 65536 + b3 * 16777216)) ;;
	esac
}

head1 "disk image"
if [ -f "${IMG}" ]; then
	ok "sdcard.img exists ($(wc -c < "${IMG}") bytes)"
else
	bad "sdcard.img is missing"
	echo; echo "check-image: ${fails} failure(s)"; exit 1
fi

p1_type=$(part_field "${IMG}" 0 type)
p2_type=$(part_field "${IMG}" 1 type)
p1_start=$(part_field "${IMG}" 0 start)
p1_sectors=$(part_field "${IMG}" 0 sectors)
p2_start=$(part_field "${IMG}" 1 start)
p2_sectors=$(part_field "${IMG}" 1 sectors)

[ "${p1_type}" = "12" ] && ok "p1 partition type 0x0c (FAT32 LBA)" || bad "p1 type is ${p1_type}, expected 12 (0x0c FAT32 LBA)"
[ "${p2_type}" = "131" ] && ok "p2 partition type 0x83 (Linux)" || bad "p2 type is ${p2_type}, expected 131 (0x83)"
[ "$(part_field "${IMG}" 0 status)" = "128" ] && ok "p1 is marked bootable" || bad "p1 is not marked bootable"

p1_mib=$((p1_sectors / 2048))
[ "${p1_mib}" -ge 100 ] && [ "${p1_mib}" -le 200 ] && \
	ok "p1 size ${p1_mib} MiB (FAT32 boot partition, ~128 MiB)" || \
	bad "p1 size ${p1_mib} MiB is outside 100..200 MiB"

p2_mib=$((p2_sectors / 2048))
[ "${p2_mib}" -eq 1024 ] && ok "p2 size ${p2_mib} MiB (ext4 root, 1 GiB)" || \
	bad "p2 size ${p2_mib} MiB, expected 1024 MiB"

[ $((p1_start + p1_sectors)) -eq "${p2_start}" ] && ok "p2 starts directly after p1" || \
	bad "partitions are not contiguous (p1 ends at $((p1_start + p1_sectors)), p2 starts at ${p2_start})"

p2_offset=$((p2_start * 512))
rootfs_bytes=$(wc -c < "${ROOTFS}" 2>/dev/null || echo 0)
[ "${rootfs_bytes}" -eq $((p2_sectors * 512)) ] && \
	ok "rootfs.ext4 (${rootfs_bytes} bytes) fills p2 exactly" || \
	bad "rootfs.ext4 is ${rootfs_bytes} bytes but p2 holds $((p2_sectors * 512))"

# ------------------------------------------------------------------ FAT content
head1 "boot partition (p1, FAT32)"
FAT_IMG="${IMG}@@$((p1_start * 512))"

# mdir on an explicit path is case-insensitive on FAT and returns non-zero when
# the entry does not exist, which is what we want here.
fat_has() { # $1 = path inside the FAT partition
	mdir -i "${FAT_IMG}" "::/$1" >/dev/null 2>&1
}
for f in config.txt cmdline.txt kernel_2712.img bcm2712-rpi-5-b.dtb; do
	if fat_has "${f}"; then
		ok "boot/${f}"
	else
		bad "boot/${f} is missing"
	fi
done
for d in overlays mnkybox; do
	if fat_has "${d}"; then
		ok "boot/${d}/ directory"
	else
		bad "boot/${d}/ directory is missing"
	fi
done
for o in overlays/vc4-kms-v3d.dtbo overlays/waveshare-3p5-dsi-h.dtbo \
	 mnkybox/wpa_supplicant.conf mnkybox/authorized_keys; do
	if fat_has "${o}"; then
		ok "boot/${o}"
	else
		bad "boot/${o} is missing"
	fi
done

fat_extract() { # $1 = file in FAT, $2 = local output
	mcopy -n -i "${FAT_IMG}" "::/$1" "$2" 2>/dev/null
}

tmpd=$(mktemp -d)
trap 'rm -rf "${tmpd}"' EXIT

head1 "config.txt"
if fat_extract config.txt "${tmpd}/config.txt"; then
	cfg="${tmpd}/config.txt"
	for line in "kernel=kernel_2712.img" "dtoverlay=vc4-kms-v3d" \
		    "dtoverlay=waveshare-3p5-dsi-h,dsi1" "ignore_lcd=1" "dtparam=spi=on"; do
		if grep -qxF "${line}" "${cfg}"; then
			ok "config.txt: ${line}"
		else
			bad "config.txt is missing the line: ${line}"
		fi
	done
	if grep -q 'display_auto_detect' "${cfg}"; then
		bad "config.txt must not set display_auto_detect"
	else
		ok "config.txt does not set display_auto_detect"
	fi
else
	bad "could not read config.txt from the boot partition"
fi

head1 "cmdline.txt"
if fat_extract cmdline.txt "${tmpd}/cmdline.txt"; then
	cmd="$(cat "${tmpd}/cmdline.txt")"
	for word in "root=/dev/mmcblk0p2" "rootwait" "threadirqs"; do
		if printf '%s\n' "${cmd}" | grep -qw -- "${word}"; then
			ok "cmdline.txt: ${word}"
		else
			bad "cmdline.txt is missing: ${word}"
		fi
	done
	if printf '%s\n' "${cmd}" | grep -Eq 'console=(ttyAMA|ttyS|serial)'; then
		bad "cmdline.txt has a UART console, but UART0/UART1 are reserved for MIDI"
	else
		ok "cmdline.txt has no UART console (UART0/UART1 reserved for MIDI)"
	fi
else
	bad "could not read cmdline.txt from the boot partition"
fi

# --------------------------------------------------------------- rootfs checks
head1 "root filesystem (p2, ext4)"
fs_has() {
	"${DEBUGFS}" -R "stat \"$1\"" "${ROOTFS}" 2>/dev/null | grep -q '^Inode:'
}
fs_ls() {
	"${DEBUGFS}" -R "ls -l \"$1\"" "${ROOTFS}" 2>/dev/null
}

if [ ! -f "${ROOTFS}" ]; then
	bad "rootfs.ext4 is missing"
else
	fs_has /usr/lib/systemd/systemd && ok "systemd is the init (/usr/lib/systemd/systemd)" || \
		bad "systemd is missing from the root filesystem"

	wants=/etc/systemd/system/multi-user.target.wants
	wants_ls="$(fs_ls "${wants}")"
	for u in dropbear.service systemd-networkd.service pipewire.service \
		 wireplumber.service weston.service mnkybox-app.service \
		 mnkybox-firstboot.service; do
		if printf '%s\n' "${wants_ls}" | grep -q -- "${u}"; then
			ok "enabled unit: ${u}"
		else
			bad "unit is not enabled: ${u}"
		fi
	done

	for f in /usr/bin/pipewire /usr/bin/wireplumber /usr/bin/weston \
		 /usr/bin/seatd /usr/bin/wpa_supplicant /usr/sbin/dropbear \
		 /opt/mnkybox/bin/mnkybox /etc/security/limits.d/audio.conf \
		 /etc/weston.ini /etc/wpa_supplicant/wpa_supplicant.conf; do
		fs_has "${f}" && ok "present: ${f}" || bad "missing: ${f}"
	done

	jack_ls="$(fs_ls /usr/lib/pipewire-0.3/jack)"
	if printf '%s\n' "${jack_ls}" | grep -q 'libjack\.so\.0'; then
		ok "PipeWire libjack present (/usr/lib/pipewire-0.3/jack/libjack.so.0)"
	else
		bad "PipeWire libjack is missing from /usr/lib/pipewire-0.3/jack/"
	fi
	fs_has /usr/lib/libasound.so.2 && ok "libasound.so.2 present" || \
		bad "libasound.so.2 is missing"
	# JACK2 must not be in the image (owner decision: PipeWire's libjack only).
	if fs_has /usr/bin/jackd; then
		bad "JACK2 (/usr/bin/jackd) is in the image"
	else
		ok "no JACK2 daemon in the image"
	fi
	if fs_ls /usr/lib | grep -q '^.*libjack\.so'; then
		bad "a libjack.so outside PipeWire's private directory is in the image"
	else
		ok "no JACK2 libjack.so in /usr/lib"
	fi

	fw_ls="$(fs_ls /lib/firmware/brcm)"
	for f in brcmfmac43455-sdio.bin brcmfmac43455-sdio.clm_blob \
		 brcmfmac43455-sdio.txt brcmfmac43455-sdio.raspberrypi,5-model-b.txt; do
		if printf '%s\n' "${fw_ls}" | grep -qF -- "${f}"; then
			ok "firmware: brcm/${f}"
		else
			bad "firmware is missing: brcm/${f}"
		fi
	done
	fs_has /lib/firmware/regulatory.db && ok "wireless-regdb regulatory.db present" || \
		bad "regulatory.db is missing (BR2_PACKAGE_WIRELESS_REGDB)"

	fs_has /boot && ok "/boot exists (mount point for p1)" || bad "/boot mount point is missing"
fi

# --------------------------------------------------------------- ELF checks
head1 "prebuilt application binaries"
if [ -x "${BOARD_DIR}/check-elf-deps.sh" ]; then
	if "${BOARD_DIR}/check-elf-deps.sh" "${BINARIES_DIR}"; then
		ok "ELF dependency check passed"
	else
		bad "ELF dependency check failed"
	fi
else
	bad "check-elf-deps.sh is missing or not executable"
fi

printf '\n'
if [ "${fails}" -ne 0 ]; then
	printf 'check-image: %d failure(s)\n' "${fails}" >&2
	exit 1
fi
echo "check-image: all checks passed"

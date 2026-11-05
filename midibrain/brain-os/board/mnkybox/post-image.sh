#!/bin/sh
#
# MNKYBOX post-image: build sdcard.img from the staged boot tree and the ext4
# root filesystem, then verify the result. A failing check fails the build.
#
# genimage is given --rootpath "${BINARIES_DIR}" so that the boot.vfat image's
# "mountpoint = /boot" resolves to ${BINARIES_DIR}/boot (assembled by
# post-build.sh). The root filesystem is prebuilt by Buildroot, so rootpath
# plays no part in sdcard.img itself.

set -eu

BOARD_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
: "${BINARIES_DIR:?BINARIES_DIR is not set}"

GENIMAGE_CFG="${BOARD_DIR}/genimage-mnkybox.cfg"
GENIMAGE_TMP="${BUILD_DIR:-${BINARIES_DIR}}/genimage.tmp"

rm -rf "${GENIMAGE_TMP}"

genimage \
	--rootpath "${BINARIES_DIR}" \
	--tmppath "${GENIMAGE_TMP}" \
	--inputpath "${BINARIES_DIR}" \
	--outputpath "${BINARIES_DIR}" \
	--config "${GENIMAGE_CFG}"

rm -rf "${GENIMAGE_TMP}"

# Acceptance checks: boot files, config contents, partition layout, rootfs
# contents (systemd, enabled units, firmware, PipeWire libjack), ELF deps.
"${BOARD_DIR}/check-image.sh"

printf 'post-image: %s/sdcard.img\n' "${BINARIES_DIR}"

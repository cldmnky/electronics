#!/bin/sh
#
# MNKYBOX post-build: assemble the boot partition staging tree in
# ${BINARIES_DIR}/boot and finish the root filesystem.
#
# Runs from Buildroot with TARGET_DIR, BINARIES_DIR, HOST_DIR and BUILD_DIR in
# the environment. Everything here is deterministic and fails loudly: a missing
# input stops the build instead of producing a half-working image.

set -eu

BOARD_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
: "${TARGET_DIR:?TARGET_DIR is not set}"
: "${BINARIES_DIR:?BINARIES_DIR is not set}"
: "${HOST_DIR:?HOST_DIR is not set}"

BOOT_STAGE="${BINARIES_DIR}/boot"
OVERLAY_DTS="${BOARD_DIR}/dts/waveshare-3p5-dsi-h-overlay.dts"
OVERLAY_DTBO_NAME="waveshare-3p5-dsi-h.dtbo"

die() { printf 'post-build: %s\n' "$*" >&2; exit 1; }
log() { printf 'post-build: %s\n' "$*"; }

# ---------------------------------------------------------------- boot staging
rm -rf "${BOOT_STAGE}"
mkdir -p "${BOOT_STAGE}/overlays" "${BOOT_STAGE}/mnkybox"

# Raspberry Pi firmware boot files: config.txt and cmdline.txt come from this
# board directory (not from the rpi-firmware package defaults).
install -m 0644 "${BOARD_DIR}/boot/config.txt" "${BOOT_STAGE}/config.txt"
install -m 0644 "${BOARD_DIR}/boot/cmdline.txt" "${BOOT_STAGE}/cmdline.txt"

# Kernel image. config.txt asks for kernel_2712.img, the name the Raspberry Pi
# 5 firmware uses for the arm64 kernel; the firmware loads whatever name
# "kernel=" gives it.
[ -f "${BINARIES_DIR}/Image" ] || die "kernel image ${BINARIES_DIR}/Image is missing"
install -m 0644 "${BINARIES_DIR}/Image" "${BOOT_STAGE}/kernel_2712.img"

# Device tree blob for the Pi 5 (BR2_LINUX_KERNEL_INTREE_DTS_NAME).
DTB="${BINARIES_DIR}/bcm2712-rpi-5-b.dtb"
[ -f "${DTB}" ] || die "device tree ${DTB} is missing"
install -m 0644 "${DTB}" "${BOOT_STAGE}/${DTB##*/}"

# Standard overlays from the Raspberry Pi firmware package (vc4-kms-v3d,
# disable-bt, ...).
if [ -d "${BINARIES_DIR}/rpi-firmware/overlays" ]; then
	cp -a "${BINARIES_DIR}/rpi-firmware/overlays/." "${BOOT_STAGE}/overlays/"
else
	die "${BINARIES_DIR}/rpi-firmware/overlays is missing (BR2_PACKAGE_RPI_FIRMWARE_INSTALL_DTB_OVERLAYS)"
fi
[ -f "${BOOT_STAGE}/overlays/vc4-kms-v3d.dtbo" ] || \
	die "overlays/vc4-kms-v3d.dtbo missing from the firmware package"

# Waveshare 3.5inch DSI LCD (H) overlay: compiled here from the vendored .dts
# source with the Buildroot host dtc. No .dtbo is committed to the repository.
DTC="${HOST_DIR}/bin/dtc"
[ -x "${DTC}" ] || die "host dtc not found at ${DTC} (BR2_PACKAGE_HOST_DTC)"
[ -f "${OVERLAY_DTS}" ] || die "overlay source ${OVERLAY_DTS} is missing"
"${DTC}" -@ -I dts -O dtb -o "${BOOT_STAGE}/overlays/${OVERLAY_DTBO_NAME}" "${OVERLAY_DTS}"
log "compiled overlays/${OVERLAY_DTBO_NAME} from ${OVERLAY_DTS##*/}"

# First-boot material on the FAT partition. Templates only: never secrets.
install -m 0644 "${BOARD_DIR}/boot-files/wpa_supplicant.conf" "${BOOT_STAGE}/mnkybox/wpa_supplicant.conf"
install -m 0644 "${BOARD_DIR}/boot-files/authorized_keys" "${BOOT_STAGE}/mnkybox/authorized_keys"
install -m 0644 "${BOARD_DIR}/boot-files/README.txt" "${BOOT_STAGE}/mnkybox/README.txt"

# ------------------------------------------------------------- root filesystem

# Lock the root account: SSH public keys only (Dropbear runs with -s, and the
# first-boot unit installs authorized_keys). No console getty is configured.
sed -i 's/^root:[^:]*:/root:*:/' "${TARGET_DIR}/etc/shadow"

# BCM43455 WiFi firmware (see board/mnkybox/firmware/*.source for provenance).
FW="${TARGET_DIR}/lib/firmware/brcm"
[ -d "${FW}" ] || die "${FW} is missing (BR2_PACKAGE_LINUX_FIRMWARE)"
[ -f "${TARGET_DIR}/lib/firmware/cypress/cyfmac43455-sdio.bin" ] || \
	die "cypress/cyfmac43455-sdio.bin is missing (BR2_PACKAGE_LINUX_FIRMWARE_CYPRESS_CYW43XXX)"
install -m 0644 "${BOARD_DIR}/firmware/brcmfmac43455-sdio.txt" "${FW}/brcmfmac43455-sdio.txt"
ln -sfn brcmfmac43455-sdio.txt "${FW}/brcmfmac43455-sdio.raspberrypi,5-model-b.txt"
ln -sfn ../cypress/cyfmac43455-sdio.bin "${FW}/brcmfmac43455-sdio.bin"
ln -sfn ../cypress/cyfmac43455-sdio.clm_blob "${FW}/brcmfmac43455-sdio.clm_blob"

# systemd unit enablement. Symlinks are created here rather than committed, so
# check-image.sh can assert exactly this list and the overlay stays readable.
enable_unit() {
	unit="$1"
	wants="$2"
	# Our own units live in /etc/systemd/system (overlay), package units in
	# /usr/lib/systemd/system; link to whichever exists so systemd resolves it.
	if [ -f "${TARGET_DIR}/etc/systemd/system/${unit}" ]; then
		unit_path="/etc/systemd/system/${unit}"
	elif [ -f "${TARGET_DIR}/usr/lib/systemd/system/${unit}" ]; then
		unit_path="/usr/lib/systemd/system/${unit}"
	else
		die "unit ${unit} is missing from the root filesystem"
	fi
	mkdir -p "${TARGET_DIR}/etc/systemd/system/${wants}.wants"
	ln -sfn "${unit_path}" "${TARGET_DIR}/etc/systemd/system/${wants}.wants/${unit}"
}
# System units.
enable_unit systemd-networkd.service multi-user.target
enable_unit dropbear.service multi-user.target
enable_unit seatd.service multi-user.target
enable_unit mnkybox-firstboot.service multi-user.target

# Session units are *user* units of user mnkybox. They are enabled through the
# user manager's default.target, and the manager itself starts at boot because
# lingering is enabled for that user (below). This is what the application team
# asked for: PipeWire, pipewire-pulse and WirePlumber live in the user session,
# with the XDG_RUNTIME_DIR the user manager provides (/run/user/1000).
# Our own copies live in /etc/systemd/user; link to them explicitly.
enable_user_unit_local() {
	unit="$1"
	[ -f "${TARGET_DIR}/etc/systemd/user/${unit}" ] || \
		die "user unit ${unit} is missing from the overlay"
	mkdir -p "${TARGET_DIR}/etc/systemd/user/default.target.wants"
	ln -sfn "/etc/systemd/user/${unit}" \
		"${TARGET_DIR}/etc/systemd/user/default.target.wants/${unit}"
}
enable_user_unit_local pipewire.service
enable_user_unit_local pipewire-pulse.service
enable_user_unit_local wireplumber.service
enable_user_unit_local weston.service
enable_user_unit_local mnkybox-app.service

# Lingering: systemd-logind starts user@1000.service at boot for this user, so
# the session units run without anybody logging in.
mkdir -p "${TARGET_DIR}/var/lib/systemd/linger"
: > "${TARGET_DIR}/var/lib/systemd/linger/mnkybox"

# /etc/systemd/system/*.service overrides must win over /usr/lib ones: our
# pipewire/wireplumber/weston/app units live in the overlay and are complete
# replacements, not drop-ins, so nothing else is needed here.

# Mount point for the FAT boot partition (fstab mounts p1 here).
mkdir -p "${TARGET_DIR}/boot"
install -d -m 0755 "${TARGET_DIR}/var/lib/mnkybox"

# Empty machine-id: systemd generates a stable one on first boot.
: > "${TARGET_DIR}/etc/machine-id"

log "boot staging tree: $(find "${BOOT_STAGE}" -type f | wc -l) files"

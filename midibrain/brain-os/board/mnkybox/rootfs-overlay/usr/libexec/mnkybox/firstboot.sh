#!/bin/sh
#
# MNKYBOX first boot: apply what the operator put on the FAT boot partition in
# /boot/mnkybox/ and rename the consumed files to *.applied, so a later boot
# does not re-apply stale data. No secret is ever baked into the image.
#
#   wpa_supplicant.conf  -> /etc/wpa_supplicant/wpa_supplicant-wlan0.conf
#                           + enables wpa_supplicant@wlan0.service
#                           Only when it sets country=; without a country the
#                           regulatory database refuses to transmit, so WiFi
#                           stays off and the file is left in place.
#   authorized_keys      -> /root/.ssh/authorized_keys (mode 0600)
#                           Dropbear runs with -s (no password logins).
#
# Exit status: 0 when nothing needed doing or everything succeeded, non-zero
# when a file exists but cannot be applied (the unit then shows as failed).

set -eu

BOOTDIR="${MNKYBOX_BOOTDIR:-/boot/mnkybox}"
SSH_DIR="/root/.ssh"
WPA_CONF="/etc/wpa_supplicant/wpa_supplicant-wlan0.conf"

log() { printf 'mnkybox-firstboot: %s\n' "$*"; }

if [ ! -d "${BOOTDIR}" ]; then
	log "no ${BOOTDIR} (boot partition not mounted?); nothing to do"
	exit 0
fi

# ------------------------------------------------------------------ SSH keys
if [ -f "${BOOTDIR}/authorized_keys" ]; then
	mkdir -p "${SSH_DIR}"
	chmod 0700 "${SSH_DIR}"
	cp "${BOOTDIR}/authorized_keys" "${SSH_DIR}/authorized_keys"
	chmod 0600 "${SSH_DIR}/authorized_keys"
	if [ -s "${SSH_DIR}/authorized_keys" ]; then
		log "installed ${SSH_DIR}/authorized_keys"
	else
		log "WARNING: ${BOOTDIR}/authorized_keys is empty; ssh login will fail"
	fi
	mv "${BOOTDIR}/authorized_keys" "${BOOTDIR}/authorized_keys.applied"
fi

# ---------------------------------------------------------------------- WiFi
if [ -f "${BOOTDIR}/wpa_supplicant.conf" ]; then
	if ! grep -Eq '^[[:space:]]*country=' "${BOOTDIR}/wpa_supplicant.conf"; then
		log "WiFi left OFF: ${BOOTDIR}/wpa_supplicant.conf has no country= line"
		log "edit it on a computer and reboot (see ${BOOTDIR}/README.txt)"
		exit 0
	fi
	mkdir -p "$(dirname "${WPA_CONF}")"
	cp "${BOOTDIR}/wpa_supplicant.conf" "${WPA_CONF}"
	chmod 0600 "${WPA_CONF}"
	systemctl enable wpa_supplicant@wlan0.service
	systemctl restart wpa_supplicant@wlan0.service || \
		log "WARNING: wpa_supplicant@wlan0.service did not start"
	mv "${BOOTDIR}/wpa_supplicant.conf" "${BOOTDIR}/wpa_supplicant.conf.applied"
	log "WiFi configured from ${BOOTDIR}/wpa_supplicant.conf"
fi

exit 0

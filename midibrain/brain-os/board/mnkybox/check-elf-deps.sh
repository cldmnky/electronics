#!/bin/sh
#
# MNKYBOX ELF dependency check for the prebuilt application.
#
# Usage: ./check-elf-deps.sh [BINARIES_DIR]
#
# The application is not compiled by Buildroot: it is dropped into the root
# filesystem as prebuilt aarch64 binaries under /opt/mnkybox/bin. This script
# reads every ELF file there, lists the shared libraries and the program
# interpreter it needs, and fails when one of them is not in the target root
# filesystem. Run it after the app team replaces the stub scripts; while the
# stubs are in place there is nothing to check and the script says so.
#
# Exit codes: 0 = all dependencies resolve (or no ELF binary yet), 1 = missing.

set -eu

BOARD_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
BINARIES_DIR="${1:-${BINARIES_DIR:-}}"
if [ -z "${BINARIES_DIR}" ]; then
	BINARIES_DIR="$(CDPATH= cd -- "${BOARD_DIR}/../../../../.." 2>/dev/null && pwd)/output/images"
fi

TARGET_DIR="${TARGET_DIR:-$(CDPATH= cd -- "${BINARIES_DIR}/../target" 2>/dev/null && pwd)}"
[ -d "${TARGET_DIR}" ] || { echo "check-elf-deps: no target directory (${TARGET_DIR})" >&2; exit 1; }

HOST_DIR="${HOST_DIR:-$(CDPATH= cd -- "${BINARIES_DIR}/../host" 2>/dev/null && pwd)}"
READELF="${HOST_DIR}/bin/readelf"
[ -x "${READELF}" ] || READELF=readelf

APP_DIR="${TARGET_DIR}/opt/mnkybox/bin"
if [ ! -d "${APP_DIR}" ]; then
	echo "check-elf-deps: ${APP_DIR} does not exist; nothing to check"
	exit 0
fi

is_elf() {
	[ -f "$1" ] && [ "$(dd if="$1" bs=1 count=4 2>/dev/null)" = "$(printf '\177ELF')" ]
}

lib_present() {
	# $1 = library file name (libfoo.so.1)
	_lib="$1"
	for d in "${TARGET_DIR}/lib" "${TARGET_DIR}/usr/lib"; do
		[ -e "${d}/${_lib}" ] && return 0
	done
	# architecture-specific subdirectories (e.g. /lib/aarch64-linux-gnu)
	if find "${TARGET_DIR}/lib" "${TARGET_DIR}/usr/lib" -maxdepth 2 -name "${_lib}" \
		-print -quit 2>/dev/null | grep -q .; then
		return 0
	fi
	return 1
}

found=0
missing=0
for bin in "${APP_DIR}"/*; do
	is_elf "${bin}" || continue
	found=$((found + 1))
	name="${bin##*/}"
	echo "check-elf-deps: ${name}"

	interp="$("${READELF}" -l "${bin}" 2>/dev/null | sed -n 's/.*interpreter: \(.*\)\]/\1/p')"
	if [ -n "${interp}" ]; then
		if [ -e "${TARGET_DIR}${interp}" ]; then
			echo "  ok   interpreter ${interp}"
		else
			echo "  FAIL interpreter ${interp} is missing"
			missing=$((missing + 1))
		fi
	fi

	for lib in $("${READELF}" -d "${bin}" 2>/dev/null |
		     sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p'); do
		if lib_present "${lib}"; then
			echo "  ok   ${lib}"
		else
			echo "  FAIL ${lib} is not in the target root filesystem"
			missing=$((missing + 1))
		fi
	done
done

if [ "${found}" -eq 0 ]; then
	echo "check-elf-deps: no ELF binary in ${APP_DIR} (stub scripts in place); nothing to check"
	exit 0
fi

if [ "${missing}" -ne 0 ]; then
	echo "check-elf-deps: ${missing} missing dependenc(ies) across ${found} binary(ies)" >&2
	exit 1
fi

echo "check-elf-deps: ${found} binary(ies), all dependencies present"

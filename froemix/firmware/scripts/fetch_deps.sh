#!/usr/bin/env bash
#
# Fetch the pinned Daisy dependencies and apply the local libDaisy patch.
#
# Everything lands in firmware/external/ (gitignored). The pins are exact SHAs;
# a moved tag or a patch that no longer applies is a hard error, not a warning.
# See froemix/doc/FIRMWARE.md for why the TDM patch exists.

set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
EXT="$HERE/external"
PATCHES="$HERE/patches"

# Latest releases at the time of the scaffold. Bump deliberately, re-run the
# bench checks in doc/FIRMWARE.md, and record the bump there.
LIBDAISY_REPO="https://github.com/electro-smith/libDaisy"
LIBDAISY_TAG="v9.0.0"
LIBDAISY_SHA="08087203debc646d3710ec8caa35bac6cf20ab5e"

DAISYSP_REPO="https://github.com/electro-smith/DaisySP"
DAISYSP_SHA="2c72eaf9eac5fc0dca1919d65d606da907832618"

clone_pinned() { # <repo> <sha> <dir> <tag-or-empty>
    local repo="$1" sha="$2" dir="$3" tag="${4:-}"
    if [ -d "$dir/.git" ]; then
        echo "  present: $dir"
        return 0
    fi
    echo "  clone:   $dir"
    if [ -n "$tag" ]; then
        git clone --depth 1 --branch "$tag" --recurse-submodules \
            --shallow-submodules "$repo" "$dir"
    else
        git clone "$repo" "$dir"
        git -C "$dir" checkout --quiet "$sha"
        git -C "$dir" submodule update --init --recursive --depth 1
    fi
}

check_pin() { # <dir> <sha>
    local dir="$1" want="$2" got
    got="$(git -C "$dir" rev-parse HEAD)"
    if [ "$got" != "$want" ]; then
        echo "ERROR: $dir is at $got, pinned $want" >&2
        exit 1
    fi
}

echo "froemix firmware: dependencies"
mkdir -p "$EXT"

clone_pinned "$LIBDAISY_REPO" "$LIBDAISY_SHA" "$EXT/libDaisy" "$LIBDAISY_TAG"
check_pin "$EXT/libDaisy" "$LIBDAISY_SHA"

clone_pinned "$DAISYSP_REPO" "$DAISYSP_SHA" "$EXT/DaisySP"
check_pin "$EXT/DaisySP" "$DAISYSP_SHA"

echo "  patches: local libDaisy changes"
shopt -s nullglob
for patch in "$PATCHES"/*.patch; do
    name="$(basename "$patch")"
    if git -C "$EXT/libDaisy" apply --check --reverse "$patch" >/dev/null 2>&1; then
        echo "    applied: $name"
        continue
    fi
    # Fail loudly: a patch that no longer applies means the pin moved under us.
    git -C "$EXT/libDaisy" apply --check "$patch"
    git -C "$EXT/libDaisy" apply "$patch"
    echo "    applied now: $name"
done

echo "froemix firmware: dependencies ready"

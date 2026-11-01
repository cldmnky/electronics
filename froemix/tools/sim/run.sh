#!/usr/bin/env bash
# Run the froemix analog verification decks and print the measured table.
#
#   cd froemix/tools/sim && bash run.sh
#
# Needs ngspice on PATH (macOS: `brew install ngspice`, Debian: `apt install
# ngspice`). Nothing here touches the board: these are standalone SPICE decks
# transcribed from tscircuit/sections/*.tsx, they do not read dist/ or the
# build. Raw output lands in out/ (one .log + one .dat per deck) and the
# measured table is written to out/measured.txt.
set -euo pipefail

cd "$(dirname "$0")"

if ! command -v ngspice >/dev/null 2>&1; then
    echo "ngspice not found -- install it first (brew install ngspice)" >&2
    exit 1
fi

mkdir -p out
for deck in audio_in audio_out cv_inout ref_bias; do
    echo "== $deck =="
    ngspice -b -o "out/$deck.log" "$deck.cir" >/dev/null
done

python3 analyze.py | tee out/measured.txt

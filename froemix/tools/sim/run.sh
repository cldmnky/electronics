#!/usr/bin/env bash
# Run and ASSERT the froemix analog verification decks.
#
#   cd froemix/tools/sim && bash run.sh
#   bash run.sh --source-only        # linkage + model tables, no simulator
#   bash run.sh --deck audio_in      # one deck
#
# Needs a native ngspice on PATH (Debian/Ubuntu: `apt install ngspice`,
# macOS: `brew install ngspice`) or `--ngspice PATH`. A missing simulator is a
# FAILURE, not a skip: an absent simulation must never look like a passed one.
#
# Every deck is copied into a fresh private directory below
# $TMPDIR/froemix-sim/ and run there with its own log and its own stderr, so a
# result can never be read from a stale or committed file. The committed
# `out/` directory is HISTORY (the numbers RESULTS.md quotes); nothing here
# writes to it. Exit code 0 means every assertion held, 1 any failed check
# (including a missing/NaN/missing-.meas result or an ngspice error line),
# 2 an incomplete environment.
set -uo pipefail

cd "$(dirname "$0")"

if ! command -v python3 >/dev/null 2>&1; then
    echo "python3 not found -- the decks and their checks are driven from it" >&2
    exit 2
fi

exec python3 run_checks.py "$@"

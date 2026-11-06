#!/usr/bin/env bash
# MIDI Brain PRODUCTION release. Order of operations is the point:
#
#   1. physical acceptance gate  - fails closed, runs BEFORE anything is
#      written or deleted, and before any fabrication export exists;
#   2. the shared fab pipeline   - prototype verification, fab exports,
#      JLCPCB BOM/CPL + sourcing and the silkscreen-in-bounds gate
#      (tscircuit/scripts/fab_pipeline.sh).
#
# Run from anywhere: `bun run release` (tscircuit/) or `make release`
# (midibrain/). There is no flag that skips step 1: an unresolved physical item
# keeps fab/ untouched.
#
# The ordering path (`make order`) runs the same pipeline without the gate and
# then prints the JLCPCB order sheet; ordering does not pass or weaken this
# gate, and `make release` remains the acceptance harness for production.
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$SCRIPT_DIR/../.." && pwd)

printf '\n########## 1. physical acceptance gate (fail closed) ##########\n'
# Exports nothing and deletes nothing. Any pending item, stale hardware
# fingerprint, malformed state or override key stops the release here.
( cd "$ROOT/tscircuit" && bun run scripts/release_gate.ts )

printf '\n########## 2. shared fab pipeline (verification, export, checks) ##########\n'
bash "$ROOT/tscircuit/scripts/fab_pipeline.sh"

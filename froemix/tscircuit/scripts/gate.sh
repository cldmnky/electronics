#!/usr/bin/env bash
# Froemix headless regression gate: every check the release runs, minus the
# fab exports. Run from anywhere: `bun run gate` (CI users: see README
# "CI / regression").
#
# Two checks in this list cannot fail by exit code alone, so the gate asserts
# their numbers: KiCad ERC (check:erc only prints a count) and the placement
# DRC (the one known H1 warning must stay a warning, not an error).
set -euo pipefail

cd "$(dirname "$0")/.."
PROJECT_DIR=$(pwd)

LOG_DIR="${TMPDIR:-/tmp}/froemix-gate"
rm -rf "$LOG_DIR"
mkdir -p "$LOG_DIR"

STEP=0
STEP_START=$SECONDS
STEP_NAME=""
GATE_START=$SECONDS
INCOMPLETE=""

step() {
  STEP=$((STEP + 1))
  STEP_START=$SECONDS
  STEP_NAME=$1
  printf '\n=== [%d] %s ===\n' "$STEP" "$1"
}

# run <log-slug> <command...>: fail the gate on nonzero exit, show the tail
# of the log either way so a passing run still shows what was checked.
run() {
  local slug=$1
  shift
  local log="$LOG_DIR/$slug.log"
  if ! "$@" >"$log" 2>&1; then
    printf '\n!!! GATE FAILED at step %d (%s): `%s` exited nonzero\n' \
      "$STEP" "$STEP_NAME" "$*" >&2
    printf -- '--- last 30 lines of %s ---\n' "$log" >&2
    tail -30 "$log" >&2
    printf '\nfull log: %s\n' "$log" >&2
    exit 1
  fi
  printf '    ok in %ds\n' "$((SECONDS - STEP_START))"
  tail -3 "$log" | sed 's/^/    /'
}

assert_zero() { # assert_zero <label> <file> <grep-pattern>
  local n
  n=$(grep -cE "$3" "$2" || true)
  if [ "$n" != "0" ]; then
    printf '\n!!! GATE FAILED at step %d (%s): %s %s\n' "$STEP" "$STEP_NAME" "$n" "$1" >&2
    grep -E "$3" "$2" | head -20 >&2
    exit 1
  fi
}

printf 'froemix gate - %s\n' "$PROJECT_DIR"

# 1. KiCad ERC. check:erc always exits 0, so the gate asserts the report.
step "KiCad ERC - 0 unexcluded errors/warnings"
if [ "${FROEMIX_SKIP_ERC:-0}" = "1" ]; then
  printf '    SKIPPED (FROEMIX_SKIP_ERC=1) - gate result is INCOMPLETE\n'
  INCOMPLETE="ERC skipped"
else
  # must match the report path in package.json's check:erc
  ERC_REPORT=/tmp/froemix-erc.rpt
  rm -f "$ERC_REPORT"
  run erc bun run check:erc
  if [ ! -f "$ERC_REPORT" ]; then
    printf '\n!!! GATE FAILED at step %d (%s): %s was not written - kicad-cli missing?\n' \
      "$STEP" "$STEP_NAME" "$ERC_REPORT" >&2
    printf '    check:erc looks for kicad-cli under /Applications/KiCad/KiCad.app/Contents/MacOS\n' >&2
    exit 1
  fi
  assert_zero "unexcluded ERC entries in $ERC_REPORT:" "$ERC_REPORT" '^\s+; (error|warning)$'
  printf '    0 unexcluded (exclusions: froemix/tmp/froemix.kicad_pro)\n'
fi

# 2. Build first: the three KiCad comparisons below read the BUILT circuit
# (dist/index/circuit.json), so a clean checkout has nothing to compare yet.
step "tsci build"
run build bun run build

# 3. KiCad netlist vs built circuit: pin for pin.
step "KiCad netlist vs built circuit"
run netlist bun run check:netlist

# 4. Footprints vs KiCad board: pad for pad.
step "Footprints vs KiCad board"
run footprints bun run check:footprints

# 5. JLCPCB parts vs schematic (needs network).
step "JLCPCB parts vs schematic (network)"
run parts bun run check:parts

# 6. tscircuit source/connectivity diagnostics. `check source` and
# `check pin_specification` are part of the skill's pre-export checklist; they
# may warn (datasheet fetch timeouts, U14's passive-only pins) but must not
# report errors.
step "tsci check source - Errors 0"
SOURCE_LOG="$LOG_DIR/check-source.log"
set +e
bunx tsci check source >"$SOURCE_LOG" 2>&1
SOURCE_EXIT=$?
set -e
if [ "$SOURCE_EXIT" -ne 0 ] || ! grep -q 'Errors: 0' "$SOURCE_LOG"; then
  STEP_NAME="tsci check source - Errors 0"
  printf '    see %s\n' "$SOURCE_LOG" >&2
  printf '\n!!! GATE FAILED at step %d (%s): not "Errors: 0"\n' "$STEP" "$STEP_NAME" >&2
  exit 1
fi
printf '    ok (see log)\n'

step "tsci check pin_specification - Errors 0"
PIN_SPEC_LOG="$LOG_DIR/check-pin-specification.log"
set +e
bunx tsci check pin_specification >"$PIN_SPEC_LOG" 2>&1
PIN_SPEC_EXIT=$?
set -e
if [ "$PIN_SPEC_EXIT" -ne 0 ] || ! grep -q 'Errors: 0' "$PIN_SPEC_LOG"; then
  STEP_NAME="tsci check pin_specification - Errors 0"
  printf '    see %s\n' "$PIN_SPEC_LOG" >&2
  printf '\n!!! GATE FAILED at step %d (%s): not "Errors: 0"\n' "$STEP" "$STEP_NAME" >&2
  exit 1
fi
printf '    ok (see log)\n'

step "tsci check netlist"
run check-netlist bunx tsci check netlist

# 7. Placement DRC: Errors 0. `tsci check placement` exits nonzero on the one
# known H1 false positive (a warning), so the gate asserts the counts and that
# the single warning is exactly that one - a new warning fails the gate.
step "tsci check placement - Errors 0 (known H1 warning allowed)"
PLACEMENT_LOG="$LOG_DIR/check-placement.log"
bunx tsci check placement >"$PLACEMENT_LOG" 2>&1 || true
assert_zero "placement errors:" "$PLACEMENT_LOG" '^- .*_error:'
PLACEMENT_WARNINGS=$(grep -cE '^- .*_warning:' "$PLACEMENT_LOG" || true)
if [ "$PLACEMENT_WARNINGS" != "1" ] ||
  ! grep -qE '^- pcb_connector_not_in_accessible_orientation_warning: H1 ' "$PLACEMENT_LOG"; then
  printf '\n!!! GATE FAILED at step %d (%s): expected only the known H1 warning, found %s warning(s)\n' \
    "$STEP" "$STEP_NAME" "$PLACEMENT_WARNINGS" >&2
  grep -E '^- .*_(error|warning):' "$PLACEMENT_LOG" | head -20 >&2
  printf 'full log: %s\n' "$PLACEMENT_LOG" >&2
  exit 1
fi
grep -qE '^Errors: 0$' "$PLACEMENT_LOG" || {
  printf '\n!!! GATE FAILED at step %d (%s): no "Errors: 0" line in placement output\n' \
    "$STEP" "$STEP_NAME" >&2
  exit 1
}
printf '    ok in %ds\n' "$((SECONDS - STEP_START))"
grep -E '^- .*_warning:' "$PLACEMENT_LOG" | sed 's/^/    /'
grep -E '^(Errors|Warnings): ' "$PLACEMENT_LOG" | sed 's/^/    /'

# 8. Copper shorts (gerber-derived).
step "tsci check shorts"
run check-shorts bunx tsci check shorts

# 9. Routing style.
step "tsci check pcb-style"
run check-pcb-style bunx tsci check pcb-style

# 10. Regenerate schematic/PCB/3D and diff against the committed baselines.
step "tsci snapshot --test --3d"
run snapshot bunx tsci snapshot --test --3d

printf '\n=== gate passed: %d steps in %ds' "$STEP" "$((SECONDS - GATE_START))"
if [ -n "$INCOMPLETE" ]; then
  printf ' (INCOMPLETE: %s)' "$INCOMPLETE"
fi
printf ' ===\n'
printf 'logs: %s\n' "$LOG_DIR"

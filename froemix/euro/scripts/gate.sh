#!/usr/bin/env bash
# Froemix euro board - one pass/fail contract, safe in CI.
#
#   bash scripts/gate.sh
#
# Steps (all required, nothing skippable):
#   1. tsci build            both circuits (board + panel), routes the board
#   2. tsci check netlist    source diagnostics
#   3. tsci check placement  courtyard/edge DRC: 0 errors, 0 warnings
#   4. tsci check shorts     unintended copper
#   5. tsci check pcb-style  odd-angle / staircase routing
#   6. check_mating.py       mating contract, panel alignment, 0 unconnected ports
#   7. panel_check.ts        panel artwork: frozen holes, rounded window, stroke
#                            widths, hardware keepouts, no copper
#   8. panel tests           mutation tests for the panel checks (bun test)
#   9. tsci snapshot --test  schematic/PCB match __snapshots__/
#
# Runtime ~1min warm. Per-step logs land in $TMPDIR/froemix-euro-gate/. The
# panel steps read the panel circuit JSON the build step just wrote.
# Needs bun (the tscircuit CLI is a bun program) and python3.

set -uo pipefail
cd "$(dirname "$0")/.."

LOG_DIR="${TMPDIR:-/tmp}/froemix-euro-gate"
mkdir -p "$LOG_DIR"
export PATH="$HOME/.bun/bin:$PATH"

FAILED=0
step() {
  local name="$1"; shift
  printf '\n=== %s\n' "$name"
  local log="$LOG_DIR/${name// /-}.log"
  # `tsci check placement` exits 1 whenever it prints any "issues:" entry, even
  # with Errors 0 / Warnings 0 (informational notes such as "rotate R5"). The
  # log parse at the bottom of this script is the authority for that step.
  local lenient=0
  if [ "$name" = "check placement" ]; then lenient=1; fi
  if "$@" >"$log" 2>&1 || { [ "$lenient" = 1 ] && grep -q "Errors: 0" "$log"; }; then
    echo "  ok"
  else
    echo "  FAILED - see $log"
    tail -15 "$log" | sed 's/^/    /'
    FAILED=1
  fi
}

step "build"          npx tsci build
step "check netlist"  npx tsci check netlist
step "check placement" npx tsci check placement
step "check shorts"   npx tsci check shorts
step "check pcb-style" npx tsci check pcb-style
step "mating contract" python3 scripts/check_mating.py
step "panel artwork"  bun scripts/panel_check.ts
step "panel tests"    bun test lib/panel
step "snapshot diff"  npx tsci snapshot --test

# ---------------------------------------------------------------------------
# Two tscircuit checks exit 0 even when they report errors, so their logs are
# parsed instead: `check placement` must say Errors: 0 / Warnings: 0, and
# `check_mating.py` prints PASS on success (its exit code is reliable, the grep
# is a belt-and-braces check on the numbers it reports).
# ---------------------------------------------------------------------------
placement_log="$LOG_DIR/check-placement.log"
if grep -q "Errors: 0" "$placement_log" 2>/dev/null; then
  if grep -qE "Warnings: [1-9]" "$placement_log"; then
    echo "FAILED: tsci check placement reported warnings"
    FAILED=1
  fi
else
  echo "FAILED: tsci check placement did not report 'Errors: 0'"
  FAILED=1
fi

mating_log="$LOG_DIR/mating-contract.log"
if ! grep -q "^PASS" "$mating_log" 2>/dev/null; then
  echo "FAILED: check_mating.py did not print PASS"
  FAILED=1
fi
if grep -q "unconnected ports" "$mating_log"; then
  n=$(grep -oE "[0-9]+ unconnected ports" "$mating_log" | head -1 | cut -d' ' -f1)
  if [ "${n:-0}" != "0" ]; then
    echo "FAILED: $n unconnected ports"
    FAILED=1
  fi
fi

printf '\n'
if [ "$FAILED" -ne 0 ]; then
  echo "GATE FAILED (logs in $LOG_DIR)"
  exit 1
fi
grep -E "^(routing|PASS)" "$mating_log" | sed 's/^/  /'
echo "GATE PASSED"

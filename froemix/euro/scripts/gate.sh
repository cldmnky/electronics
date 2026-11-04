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
#   7. check_madc.py         mux inputs are pot/joystick/button islands on P3V3_A
#   8. mechanical_check.ts   actual supplier identities + Z/actuator envelopes
#   9. panel_check.ts        audited holes, rounded window, artwork/keepouts
#  10. regression tests     mating mutations + panel/stack/printable CAD tests
#  11. tsci snapshot --test  schematic/PCB match __snapshots__/
#
# Runtime ~1min warm. Per-step logs land in $TMPDIR/froemix-euro-gate/. The
# panel steps read the panel circuit JSON the build step just wrote.
# Needs bun (the tscircuit CLI is a bun program) and python3.

set -uo pipefail
export FROEMIX_BUILD_DATE="${FROEMIX_BUILD_DATE:-$(date -u +%F)}"
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
    # An empty log is not a pass: a step that printed nothing did not check
    # anything, whatever its exit status.
    if [ ! -s "$log" ]; then
      echo "  FAILED - exited 0 but printed nothing ($log)"
      FAILED=1
      return
    fi
    echo "  ok"
  else
    echo "  FAILED - see $log"
    tail -15 "$log" | sed 's/^/    /'
    FAILED=1
  fi
}

step "build"          npx tsci build
# Every later step reads the built JSON; a missing file must stop the gate here
# instead of producing seven confusing failures.
for built in dist/index/circuit.json dist/panel/circuit.json; do
  if [ ! -s "$built" ]; then
    echo "FAILED: $built was not written by the build"
    FAILED=1
  fi
done
if [ "$FAILED" -ne 0 ]; then
  echo "GATE FAILED: the build did not produce both circuits (logs in $LOG_DIR)"
  exit 1
fi
step "check netlist"  npx tsci check netlist
step "check placement" npx tsci check placement
step "check shorts"   npx tsci check shorts
step "check pcb-style" npx tsci check pcb-style
step "mating contract" python3 scripts/check_mating.py
step "mux input contract" python3 scripts/check_madc.py
step "mechanical stack" bun scripts/mechanical_check.ts
step "panel artwork"  bun scripts/panel_check.ts
step "regression tests" bash -c 'python3 scripts/test_mating.py && python3 scripts/test_madc.py && bun test lib/panel lib/mechanical'
# tsci snapshot reads dist rather than rebuilding; preserve the actual dated
# build for later release checks while comparing a deterministic snapshot.
snapshot_diff() (
  local saved
  saved=$(mktemp -d)
  cp dist/index/circuit.json "$saved/index.json"
  cp dist/panel/circuit.json "$saved/panel.json"
  trap 'cp "$saved/index.json" dist/index/circuit.json; cp "$saved/panel.json" dist/panel/circuit.json; rm -rf "$saved"' EXIT
  export FROEMIX_BUILD_DATE=2024-08-10
  npx tsci build && npx tsci snapshot --test
)
step "snapshot diff" snapshot_diff

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

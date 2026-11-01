#!/usr/bin/env bash
# Froemix euro board release: run the euro gate, then export the fab artifacts
# for BOTH euro entries (the front board and its panel) into froemix/fab/euro/.
#
# Same conventions as the base board's scripts/release.sh: the gerbers export
# writes a zip that also carries bom.csv + pick_and_place.csv, so those two are
# extracted next to it. The panel is a mechanical FR4 board (2mm, holes and
# silkscreen only), so it gets its own gerbers/step under fab/euro/panel/.
#
# Nothing outside froemix/fab/ is deleted; fab/ is generated output.
set -euo pipefail

cd "$(dirname "$0")/.."
PROJECT_DIR=$(pwd)
FAB_DIR=$(cd .. && pwd)/fab/euro
ENTRY=index.circuit.tsx
PANEL=panel.circuit.tsx

printf '\n########## 1. euro gate ##########\n'
bash "$PROJECT_DIR/scripts/gate.sh"

printf '\n########## 2. euro fab exports ##########\n'
rm -rf "$FAB_DIR"
mkdir -p "$FAB_DIR" "$FAB_DIR/panel"

export_fmt() { # export_fmt <entry> <format> <output-file>
  local entry=$1 fmt=$2 out=$3
  local start=$SECONDS
  printf '    %-10s %-14s -> %s ...' "$entry" "$fmt" "${out#"$FAB_DIR"/}"
  if ! npx tsci export "$entry" -f "$fmt" -o "$out" >"${TMPDIR:-/tmp}/froemix-euro-export-$fmt.log" 2>&1; then
    printf ' FAILED\n'
    tail -20 "${TMPDIR:-/tmp}/froemix-euro-export-$fmt.log" >&2
    exit 1
  fi
  printf ' ok (%ds)\n' "$((SECONDS - start))"
}

export_fmt "$ENTRY" gerbers "$FAB_DIR/euro-gerbers.zip"
export_fmt "$ENTRY" step "$FAB_DIR/euro.step"
export_fmt "$ENTRY" schematic-svg "$FAB_DIR/euro-schematic.svg"
export_fmt "$ENTRY" pcb-svg "$FAB_DIR/euro-pcb.svg"

export_fmt "$PANEL" gerbers "$FAB_DIR/panel/panel-gerbers.zip"
export_fmt "$PANEL" step "$FAB_DIR/panel/panel.step"

# bom.csv + pick_and_place.csv live inside the board's gerbers zip.
python3 - "$FAB_DIR/euro-gerbers.zip" "$FAB_DIR" <<'PY'
import pathlib
import sys
import zipfile

archive, fab_dir = sys.argv[1], pathlib.Path(sys.argv[2])
wanted = {"bom.csv": "euro-bom.csv", "pick_and_place.csv": "euro-pnp.csv"}
with zipfile.ZipFile(archive) as zf:
    names = set(zf.namelist())
    for member, dest in wanted.items():
        if member not in names:
            sys.exit(f"{archive}: no {member} in the gerbers zip")
        (fab_dir / dest).write_bytes(zf.read(member))
print("    extracted bom.csv -> euro-bom.csv, pick_and_place.csv -> euro-pnp.csv")
PY

cp ../doc/EURO-BOARD.md "$FAB_DIR/EURO-BOARD.md"

printf '\n########## 3. artifacts ##########\n'
( cd "$FAB_DIR" && find . -type f | sort | while read -r f; do
    bytes=$(wc -c < "$f" | tr -d ' ')
    printf '    %-28s %9.1f KB\n' "${f#./}" "$(awk -v b="$bytes" 'BEGIN {printf "%.1f", b/1024}')"
  done )
printf '    folder: %s\n' "$FAB_DIR"

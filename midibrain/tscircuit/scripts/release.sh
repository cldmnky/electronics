#!/usr/bin/env bash
# MIDI Brain PRODUCTION release. Order of operations is the point:
#
#   1. physical acceptance gate  - fails closed, runs BEFORE anything is
#      written or deleted, and before any fabrication export exists;
#   2. prototype verification    - `make test` (design/software consistency),
#      which is not an approval;
#   3. fab exports               - only now is fab/ wiped and rebuilt;
#   4. JLCPCB BOM/CPL + sourcing - strict CSV parsing, pinned sides/counts,
#      counts compared with jlc-order.json;
#   5. silkscreen-in-bounds gate on the exported Gerbers.
#
# Run from anywhere: `bun run release` (tscircuit/) or `make release`
# (midibrain/). There is no flag that skips step 1: an unresolved physical item
# keeps fab/ untouched.
#
# Format notes (verified against `bunx tsci export --help` on the pinned CLI):
# there is no `bom` and no `pnp` export format. The `gerbers` export writes a
# zip that ALSO contains `bom.csv` (JLCPCB part numbers included) and
# `pick_and_place.csv` (JLCPCB rotation convention), so this script exports the
# zip once per board and extracts those two CSVs next to it.
#
# Three boards, three upload sets: front (UI), rear (base), panel (bare FR-4,
# fabrication only - its bom/pick_and_place exports are empty by design and the
# script fails if a component ever lands on the panel file).
#
# Neither the gate nor this script writes to git, and nothing outside
# midibrain/fab/ is deleted (that directory is generated output).
set -euo pipefail

cd "$(dirname "$0")/../.."
ROOT=$(pwd)
FAB_DIR="$ROOT/fab"

printf '\n########## 1. physical acceptance gate (fail closed) ##########\n'
# Exports nothing and deletes nothing. Any pending item, stale hardware
# fingerprint, malformed state or override key stops the release here.
( cd "$ROOT/tscircuit" && bun run scripts/release_gate.ts )

printf '\n########## 2. prototype verification (not an approval) ##########\n'
make -C "$ROOT" test
# The release also enforces the manufacturing targets that `make test` only
# reports (via drill/pad and PTH ring above the published floor).
python3 "$ROOT/tscircuit/scripts/fab_check.py" --strict-targets

printf '\n########## 3. fab exports ##########\n'
rm -rf "$FAB_DIR"
mkdir -p "$FAB_DIR/front" "$FAB_DIR/rear" "$FAB_DIR/panel"

export_fmt() { # export_fmt <entry> <format> <output-file>
  local entry=$1
  local fmt=$2
  local out=$3
  local start=$SECONDS
  printf '    %-22s -> %s ...' "$entry/$fmt" "${out#"$FAB_DIR"/}"
  if ! (cd "$ROOT/tscircuit" && bunx tsci export "$entry" -f "$fmt" -o "$out" >"${TMPDIR:-/tmp}/midibrain-export-$fmt.log" 2>&1); then
    printf ' FAILED\n'
    tail -20 "${TMPDIR:-/tmp}/midibrain-export-$fmt.log" >&2
    exit 1
  fi
  printf ' ok (%ds)\n' "$((SECONDS - start))"
}

for spec in "front.circuit.tsx front" "index.circuit.tsx rear" "panel.circuit.tsx panel"; do
  # shellcheck disable=SC2086
  set -- $spec
  entry=$1
  name=$2
  export_fmt "$entry" gerbers        "$FAB_DIR/$name/$name-gerbers.zip"
  export_fmt "$entry" step           "$FAB_DIR/$name/$name.step"
  export_fmt "$entry" schematic-svg  "$FAB_DIR/$name/$name-schematic.svg"
  export_fmt "$entry" pcb-svg        "$FAB_DIR/$name/$name-pcb.svg"
done

printf '\n########## 4. JLC BOM/CPL + sourcing BOM ##########\n'
# Strict CSV parsing (every row must match the header), pinned SMT sides,
# LCSC C-number syntax, THT exclusions, an explicit qualified-process row for
# the encoder, and placement counts compared with jlc-order.json.
python3 "$ROOT/tscircuit/scripts/sourcing_bom.py" \
  "$FAB_DIR" "$ROOT/tscircuit/scripts/sourcing-extras.csv" "$ROOT/jlc-order.json"

printf '\n########## 5. silkscreen bounds gate ##########\n'
# Parse the Legend Gerbers and prove no silk leaves the board outline (0.5 mm
# guard). Added after the 2026-11 title ran 19 mm off the panel edge: unit tests
# only see anchor positions, never the exporter's rendered glyph extents, so the
# Gerber file is authoritative.
python3 - "$FAB_DIR" <<'PY'
import re
import sys
import zipfile
from pathlib import Path

fab = Path(sys.argv[1])
BOUNDS = {"front": (128, 112), "rear": (101.6, 112), "panel": (132.08, 128.5)}
GUARD = 0.5


def silk_bbox(text):
    half = {}  # dcode -> half-extent (conservative pad for its marks)
    cur, cx, cy = None, None, None
    lo = [float("inf"), float("inf")]
    hi = [float("-inf"), float("-inf")]

    def mark(x, y):
        pad = half.get(cur, 0.5)
        lo[0] = min(lo[0], x - pad); lo[1] = min(lo[1], y - pad)
        hi[0] = max(hi[0], x + pad); hi[1] = max(hi[1], y + pad)

    for ln in text.splitlines():
        if ln.startswith("G36") or ln.startswith("%SR"):
            sys.exit(f"silk gate: unsupported Gerber construct: {ln[:60]}")
        if ln.startswith("%ADD"):
            m = re.match(r"%ADD(\d+)([CR]),([\d.]+)(?:X([\d.]+))?", ln)
            if m:
                d, kind, a, b = m.group(1), m.group(2), float(m.group(3)), m.group(4)
                half[d] = (max(a, float(b)) if b else a) / 2
            else:
                nums = re.findall(r"[\d.]+", ln.split(",", 1)[1])
                nums = [float(n) for n in nums]
                d = re.match(r"%ADD(\d+)", ln).group(1)
                half[d] = (max(nums) / 2) if nums else 0.5
            continue
        if ln.startswith("%") or (len(ln) >= 3 and ln[:3] in ("M00", "M02", "G04", "G01", "G70", "G71", "G75", "G90", "G91")):
            continue
        for m in re.finditer(r"D0*(\d+)", ln):
            cur = m.group(1)
        for m in re.finditer(r"X(-?\d+)(Y(-?\d+))?", ln):
            cx = int(m.group(1)) / 1e6
            if m.group(3) is not None:
                cy = int(m.group(3)) / 1e6
            if cur in ("1", "3") and cx is not None and cy is not None:
                mark(cx, cy)
        for m in re.finditer(r"(?<![XY0-9])Y(-?\d+)", ln):
            cy = int(m.group(1)) / 1e6
            if cur in ("1", "3") and cx is not None:
                mark(cx, cy)
    if lo[0] == float("inf"):
        return None
    return (lo[0], lo[1], hi[0], hi[1])


for name, (w, h) in BOUNDS.items():
    with zipfile.ZipFile(fab / name / f"{name}-gerbers.zip") as zf:
        members = sorted(n for n in zf.namelist() if n in ("F_SilkScreen.gbr", "B_SilkScreen.gbr"))
        if not members:
            sys.exit(f"{name}: no silkscreen gerbers in the zip")
        for member in members:
            box = silk_bbox(zf.read(member).decode())
            if box is None:
                print(f"    {name}/{member}: empty, skipped")
                continue
            x1, y1, x2, y2 = box
            ok_box = (x1 >= -w / 2 + GUARD and x2 <= w / 2 - GUARD and
                      y1 >= -h / 2 + GUARD and y2 <= h / 2 - GUARD)
            print(f"    {name}/{member}: silk X {x1:.2f}..{x2:.2f} Y {y1:.2f}..{y2:.2f} "
                  f"(board +-({w / 2:.2f},{h / 2:.2f})) {'OK' if ok_box else 'OFF-BOARD'}")
            if not ok_box:
                sys.exit(f"{name}/{member}: silkscreen leaves the board outline")
PY

# The order-day guides live in docs/ (fab/ is wiped on every release); copy them
# in so the upload folder is self-documenting.
cp "$ROOT/docs/JLC-ASSEMBLY.md" "$ROOT/docs/assembly.md" "$ROOT/jlc-order.json" "$FAB_DIR/"

printf '\n########## 6. artifacts ##########\n'
( cd "$FAB_DIR" && find . -type f | sort | awk '{ printf "    %s\n", $1 }' )
printf '    folder: %s\n' "$FAB_DIR"

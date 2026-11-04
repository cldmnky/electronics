#!/usr/bin/env bash
# MIDI Brain release: run the full gate, then export the fab artifacts into
# midibrain/fab/. Run from anywhere: `bun run release` (tscircuit/) or
# `make release` (midibrain/).
#
# Format notes (verified against `bunx tsci export --help` on the pinned CLI):
# there is no `bom` and no `pnp` export format. The `gerbers` export writes a
# zip that ALSO contains `bom.csv` (JLCPCB part numbers included) and
# `pick_and_place.csv` (JLCPCB rotation convention), so this script exports the
# zip once per board and extracts those two CSVs next to it.
#
# Three boards, three upload sets: front (UI), rear (base), panel (bare FR-4,
# fabrication only — its bom/pick_and_place exports are empty by design and
# the script fails if a component ever lands on the panel file).
#
# Neither the gate nor this script writes to git, and nothing outside
# midibrain/fab/ is deleted (that directory is generated output).
set -euo pipefail

cd "$(dirname "$0")/../.."
ROOT=$(pwd)
FAB_DIR="$ROOT/fab"

printf '\n########## 1. gate ##########\n'
make -C "$ROOT" test

printf '\n########## 2. fab exports ##########\n'
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

printf '\n########## 3. JLC BOM/CPL + sourcing BOM ##########\n'
python3 - "$FAB_DIR" "$ROOT/tscircuit/scripts/sourcing-extras.csv" <<'PY'
import csv
import pathlib
import sys
import zipfile

fab = pathlib.Path(sys.argv[1])
extras_path = pathlib.Path(sys.argv[2])

# Through-hole parts per board: JLCPCB's standard SMT service cannot place
# these. They stay in the combined BOM (they still have to be bought) and stay
# out of the SMT CPL. Front THT carries LCSC numbers; rear THT (sockets and
# pin headers) has none and must be sourced generically for hand soldering.
THT = {
    "front": {"J1", "J2", "J3", "J4",
              "SW1", "SW2", "SW3", "SW4", "SW5", "SW6",
              "U6", "J5"},
    "rear": {"J_PICO_L", "J_PICO_R", "J_PI", "J_FRONT"},
}
# Expected SMT assembly sides, pinned so a future edit cannot silently move a
# part to the other side. Both boards are single-sided for JLCPCB: front SMT
# all on top (panel side), rear SMT all on bottom. Front J5 stays bottom — it
# is THT and must mate rearward — and THT bodies live wherever they mate.
EXPECTED_SMT_SIDE = {"front": "top", "rear": "bottom"}


def norm(row, key):
    return (row.get(key) or "").strip()


for name in ("front", "rear"):
    d = fab / name
    with zipfile.ZipFile(d / f"{name}-gerbers.zip") as zf:
        names = set(zf.namelist())
        for member in ("bom.csv", "pick_and_place.csv"):
            if member not in names:
                sys.exit(f"{name}: no {member} in the gerbers zip")
        (d / "bom.csv").write_bytes(zf.read("bom.csv"))
        (d / "pnp-all.csv").write_bytes(zf.read("pick_and_place.csv"))
    print(f"    {name}: extracted bom.csv + pick_and_place.csv")

    bom_rows = list(csv.DictReader((d / "bom.csv").open()))
    pnp_rows = list(csv.DictReader((d / "pnp-all.csv").open()))
    if {r["Designator"] for r in bom_rows} != {r["Designator"] for r in pnp_rows}:
        sys.exit(f"{name}: BOM and CPL disagree on designators: "
                 f"{sorted({r['Designator'] for r in bom_rows} ^ {r['Designator'] for r in pnp_rows})}")

    layers = {r["Designator"]: r["Layer"] for r in pnp_rows}
    smt_refs = set(layers) - THT[name]
    if set(layers) - smt_refs != THT[name] & set(layers):
        pass  # THT subset check below covers unknown refs instead
    unknown = set(layers) - smt_refs - THT[name]
    if unknown:
        sys.exit(f"{name}: unexpected designators outside THT set: {sorted(unknown)}")

    # Owner rule, enforced: every SMT line item needs an LCSC C-number.
    no_lcsc = [r["Designator"] for r in bom_rows
               if r["Designator"] in smt_refs and not norm(r, "JLCPCB Part #")]
    if no_lcsc:
        sys.exit(f"{name}: SMT without LCSC part number: {', '.join(no_lcsc)}")

    # Pinned assembly sides: every SMT placement on the expected side.
    wrong_side = sorted(r for r in smt_refs if layers[r].lower() != EXPECTED_SMT_SIDE[name])
    if wrong_side:
        sys.exit(f"{name}: SMT off the {EXPECTED_SMT_SIDE[name]} side: {wrong_side}")

    def pkg(row):
        lcsc = norm(row, "JLCPCB Part #")
        fp = norm(row, "Footprint")
        return "-" if fp == lcsc or not fp else fp

    # Combined upload BOM: one row per unique part (THT included).
    groups = {}
    for row in bom_rows:
        comment = norm(row, "Value") or norm(row, "Comment")
        groups.setdefault((comment, pkg(row), norm(row, "JLCPCB Part #")), []).append(row["Designator"])
    with (d / f"{name}-bom-jlc.csv").open("w", newline="") as fh:
        w = csv.writer(fh)
        w.writerow(["Comment", "Designator", "Footprint", "LCSC Part #", "Quantity"])
        for (comment, footprint, lcsc), refs in sorted(groups.items(), key=lambda kv: kv[1][0]):
            w.writerow([comment, ",".join(sorted(refs)), footprint, lcsc, len(refs)])

    # SMT-only CPL for the assembly order.
    smt = [r for r in pnp_rows if r["Designator"] in smt_refs]
    with (d / f"{name}-cpl-jlc.csv").open("w", newline="") as fh:
        w = csv.writer(fh)
        w.writerow(["Designator", "Mid X", "Mid Y", "Layer", "Rotation"])
        for r in smt:
            w.writerow([r["Designator"], r["Mid X"], r["Mid Y"], r["Layer"], r["Rotation"]])

    # Split BOMs: smt for the JLC order page (nothing to un-tick), hand for
    # the bench. THT keeps its LCSC numbers where it has them.
    smt_groups, hand_groups = {}, {}
    for row in bom_rows:
        ref = row["Designator"]
        lcsc = norm(row, "JLCPCB Part #")
        value = norm(row, "Value")
        mpn = norm(row, "Comment")
        comment = value or mpn or lcsc
        if ref in smt_refs:
            smt_groups.setdefault((comment, pkg(row), lcsc), []).append(ref)
        else:
            hand_groups.setdefault((lcsc, mpn or comment, comment, pkg(row)), []).append(ref)
    with (d / f"{name}-bom-smt.csv").open("w", newline="") as fh:
        w = csv.writer(fh)
        w.writerow(["Comment", "Designator", "Footprint", "LCSC Part #", "Quantity"])
        for (comment, package, lcsc), refs in sorted(smt_groups.items(), key=lambda kv: kv[1][0]):
            w.writerow([comment, ",".join(sorted(refs)), package, lcsc, len(refs)])
    with (d / f"{name}-bom-hand.csv").open("w", newline="") as fh:
        w = csv.writer(fh)
        w.writerow(["Board", "Designators", "Quantity", "MPN", "LCSC", "Package",
                    "Side", "Method", "Source", "Notes"])
        for (lcsc, mpn, comment, package), refs in sorted(hand_groups.items(), key=lambda kv: kv[1][0]):
            sides = sorted({layers.get(r, "?") for r in refs})
            source = "LCSC/JLCPCB" if lcsc else "generic (no LCSC in design)"
            w.writerow([name, ",".join(sorted(refs)), len(refs), mpn, lcsc, package,
                        "/".join(sides), "hand", source, comment])

    all_refs = {r["Designator"] for r in bom_rows}
    split_refs = {r for refs in smt_groups.values() for r in refs} | {r for refs in hand_groups.values() for r in refs}
    if all_refs != split_refs:
        sys.exit(f"{name}: BOM split lost designators: {sorted(all_refs ^ split_refs)}")
    print(f"    {name}-bom-smt.csv   {len(smt_groups)} unique parts, {len(smt_refs)} placements "
          f"(all {EXPECTED_SMT_SIDE[name]}, single-sided)")
    print(f"    {name}-bom-hand.csv  {len(hand_groups)} unique parts, "
          f"{sum(len(r) for r in hand_groups.values())} placements (hand solder)")

# Panel: fabrication only. Empty BOM/CPL by design — fail loudly otherwise.
pd = fab / "panel"
with zipfile.ZipFile(pd / "panel-gerbers.zip") as zf:
    names = set(zf.namelist())
    for member in ("bom.csv", "pick_and_place.csv"):
        if member not in names:
            sys.exit(f"panel: no {member} in the gerbers zip")
    bom_raw = zf.read("bom.csv").decode().strip()
    pnp_raw = zf.read("pick_and_place.csv").decode().strip()
panel_bom_rows = [ln for ln in bom_raw.splitlines()[1:] if ln.strip()] if bom_raw else []
panel_pnp_rows = [ln for ln in pnp_raw.splitlines()[1:] if ln.strip()] if pnp_raw else []
if panel_bom_rows or panel_pnp_rows:
    sys.exit(f"panel: expected no components, got BOM rows {panel_bom_rows} PNP rows {panel_pnp_rows}")
print("    panel: no components (fabrication only, no BOM/CPL)")

# Silkscreen-in-bounds gate: parse the Legend Gerbers and prove no silk
# leaves the board outline (0.5 mm guard). Added after the 2026-11 title ran
# 19 mm off the panel edge: unit tests only see anchor positions, never the
# exporter's rendered glyph extents, so the Gerber file is authoritative.
import re

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

# Sourcing BOM: bench hand-solder rows from both boards plus the curated
# modules/cables/hardware list. Stock snapshots must be rechecked at order time.
hand_files = [(fab / "front" / "front-bom-hand.csv"), (fab / "rear" / "rear-bom-hand.csv")]
cols = ["Board", "Designators", "Quantity", "MPN", "LCSC", "Package",
        "Side", "Method", "Source", "Notes"]
merged = []
for path in hand_files:
    with path.open() as fh:
        for row in csv.DictReader(fh):
            merged.append([row[c] for c in cols])
with extras_path.open() as fh:
    extra_rows = list(csv.DictReader(fh))
    if extra_rows and list(extra_rows[0].keys()) != cols:
        sys.exit(f"sourcing-extras.csv columns {list(extra_rows[0].keys())} != {cols}")
    for row in extra_rows:
        merged.append([row[c] for c in cols])
with (fab / "sourcing-bom.csv").open("w", newline="") as fh:
    w = csv.writer(fh)
    w.writerow(cols)
    w.writerows(merged)
print(f"    sourcing-bom.csv   {len(merged)} rows "
      f"({sum(1 for r in merged if r[0] in ('front', 'rear'))} bench + "
      f"{sum(1 for r in merged if r[0] not in ('front', 'rear'))} modules/cables/hardware)")
print("    NOTE: stock and package fit must be rechecked at order time (owner rule)")
PY

# The order-day guides live in doc/ (fab/ is wiped on every release); copy them
# in so the upload folder is self-documenting.
cp "$ROOT/docs/JLC-ASSEMBLY.md" "$ROOT/docs/assembly.md" "$ROOT/jlc-order.json" "$FAB_DIR/"

printf '\n########## 4. artifacts ##########\n'
( cd "$FAB_DIR" && find . -type f | sort | awk '{ printf "    %s\n", $1 }' )
printf '    folder: %s\n' "$FAB_DIR"

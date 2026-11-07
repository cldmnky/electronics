#!/usr/bin/env bash
# Froemix release: run the full gate, then export the fab artifacts into
# froemix/fab/. Run from anywhere: `bun run release`.
#
# Format notes (verified against `bunx tsci export --help` and the installed
# CLI's ALLOWED_EXPORT_FORMATS): there is no `bom` and no `pnp` export format.
# The `gerbers` export writes a zip that ALSO contains `bom.csv` (JLCPCB part
# numbers included) and `pick_and_place.csv` (JLCPCB rotation convention), so
# this script exports the zip once and extracts those two CSVs next to it.
#
# Neither the gate nor this script writes to git, and nothing outside
# froemix/fab/ is deleted (that directory is generated output).
set -euo pipefail
export FROEMIX_BUILD_DATE="${FROEMIX_BUILD_DATE:-$(date -u +%F)}"
printf 'Build date (UTC): %s\n' "$FROEMIX_BUILD_DATE"

cd "$(dirname "$0")/.."
PROJECT_DIR=$(pwd)
FAB_DIR=$(cd .. && pwd)/fab
ENTRY=index.circuit.tsx

# ---------------------------------------------------------------------------
# 0. Physical acceptance, fail closed and FIRST: nothing below this block may
#    build, export or delete before every required physical item is accepted
#    with recorded evidence. `$FAB_DIR` still exists afterwards with this
#    revision's contents if the gate blocks, so a blocked release destroys
#    nothing. See ../doc/acceptance.md.
# ---------------------------------------------------------------------------
if [ "${FROEMIX_SKIP_ERC:-0}" = "1" ]; then
  printf '\nRELEASE REFUSED: FROEMIX_SKIP_ERC=1\n' >&2
  printf 'An ERC skip is a prototype convenience. A production release must run ERC,\n' >&2
  printf 'and physical acceptance cannot substitute for it. Nothing was built,\n' >&2
  printf 'exported or deleted. Unset FROEMIX_SKIP_ERC to release.\n' >&2
  exit 1
fi
printf '\n########## 0. physical acceptance (fail closed) ##########\n'
if ! bun run ../tools/release/gate.ts --check; then
  printf '\nRELEASE BLOCKED: physical acceptance is not complete.\n' >&2
  printf 'Nothing was built, exported or deleted; %s is untouched.\n' "$FAB_DIR" >&2
  printf 'A pending item is normal today: see ../doc/acceptance.md and ../doc/evidence/README.md.\n' >&2
  exit 1
fi

printf '\n########## 1. gate ##########\n'
bash "$PROJECT_DIR/scripts/gate.sh"

printf '\n########## 2. fab exports ##########\n'
rm -rf "$FAB_DIR"
mkdir -p "$FAB_DIR"

export_fmt() { # export_fmt <format> <output-file>
  local fmt=$1
  local out=$2
  local start=$SECONDS
  printf '    %-14s -> %s ...' "$fmt" "${out#"$FAB_DIR"/}"
  if ! bunx tsci export "$ENTRY" -f "$fmt" -o "$out" >"${TMPDIR:-/tmp}/froemix-export-$fmt.log" 2>&1; then
    printf ' FAILED\n'
    tail -20 "${TMPDIR:-/tmp}/froemix-export-$fmt.log" >&2
    exit 1
  fi
  printf ' ok (%ds)\n' "$((SECONDS - start))"
}

export_fmt gerbers "$FAB_DIR/froemix-gerbers.zip"
export_fmt step "$FAB_DIR/froemix.step"
export_fmt schematic-svg "$FAB_DIR/froemix-schematic.svg"
export_fmt pcb-svg "$FAB_DIR/froemix-pcb.svg"

# bom.csv + pick_and_place.csv live inside the gerbers zip (see header note).
python3 - "$FAB_DIR/froemix-gerbers.zip" "$FAB_DIR" <<'PY'
import pathlib
import sys
import zipfile

archive, fab_dir = sys.argv[1], pathlib.Path(sys.argv[2])
wanted = {"bom.csv": "froemix-bom.csv", "pick_and_place.csv": "froemix-pnp.csv"}
with zipfile.ZipFile(archive) as zf:
    names = set(zf.namelist())
    for member, dest in wanted.items():
        if member not in names:
            sys.exit(f"{archive}: no {member} in the gerbers zip")
        (fab_dir / dest).write_bytes(zf.read(member))
    gerbers = sorted(n for n in names if n not in wanted)
print(f"    extracted bom.csv -> froemix-bom.csv, pick_and_place.csv -> froemix-pnp.csv")
print(f"    gerbers zip: {len(gerbers)} fab files ({', '.join(gerbers)})")
PY

# JLCPCB assembly files. Their SMT order takes a BOM (one row per part, with
# quantity) and a CPL (one row per placement, SMT only). The tscircuit exports
# are per-designator and include the 10 through-hole parts in the CPL, so derive
# the JLC-shaped versions here instead of hand-editing on the order page.
python3 - "$FAB_DIR" <<'PY2'
import csv
import pathlib
import sys

fab = pathlib.Path(sys.argv[1])
# Through-hole parts: JLCPCB's standard SMT service cannot place these; they are
# either hand-soldered or ordered as THT assembly. Keep them in the BOM (they
# still have to be bought) and out of the SMT CPL.
THT = {"CN1", "CN2", "CN3", "CN4", "H1", "J1", "J2", "C24", "C25", "U16"}

bom_rows = list(csv.DictReader((fab / "froemix-bom.csv").open()))
pnp_rows = list(csv.DictReader((fab / "froemix-pnp.csv").open()))

groups = {}
for row in bom_rows:
    lcsc = (row.get("JLCPCB Part #") or "").strip()
    # JLCPCB reads "Comment" as the human description (10uF, 100k, TL074...).
    # The exported Comment column holds the manufacturer part number, so use
    # Value when we have it and fall back to Comment.
    comment = (row.get("Value") or row.get("Comment", "")).strip()
    footprint = (row.get("Footprint", "") or "").strip()
    # For parts on custom footprints tscircuit repeats the LCSC code in the
    # Footprint column. JLCPCB resolves the footprint from the part number, so
    # write "-" rather than a part number that looks like a package.
    if footprint == lcsc or not footprint:
        footprint = "-"
    key = (comment, footprint, lcsc)
    groups.setdefault(key, []).append(row["Designator"])

with (fab / "froemix-bom-jlc.csv").open("w", newline="") as fh:
    w = csv.writer(fh)
    w.writerow(["Comment", "Designator", "Footprint", "LCSC Part #", "Quantity"])
    for (comment, footprint, lcsc), refs in sorted(groups.items(), key=lambda kv: kv[1][0]):
        w.writerow([comment, ",".join(sorted(refs)), footprint, lcsc, len(refs)])

smt = [r for r in pnp_rows if r["Designator"] not in THT]
# Check the actual export as well as circuit.json: never upload a mixed-side
# CPL while ordering single-sided assembly.
if len(smt) != 107 or any(r["Layer"].lower() != "bottom" for r in smt):
    sys.exit(f"Expected 107 bottom-side SMT placements in exported CPL; got {len(smt)}: "
             f"{[(r['Designator'], r['Layer']) for r in smt if r['Layer'].lower() != 'bottom']}")
with (fab / "froemix-cpl-jlc.csv").open("w", newline="") as fh:
    w = csv.writer(fh)
    w.writerow(["Designator", "Mid X", "Mid Y", "Layer", "Rotation"])
    for r in smt:
        w.writerow([r["Designator"], r["Mid X"], r["Mid Y"], r["Layer"], r["Rotation"]])

(fab / "froemix-pnp-all.csv").write_bytes((fab / "froemix-pnp.csv").read_bytes())

# Two upload-sized BOMs next to the combined one: the SMT file contains only the
# parts JLCPCB places (so the order page has nothing to un-tick), and the hand
# file is the shopping list for everything soldered by hand.
layer = {r["Designator"]: r["Layer"] for r in pnp_rows}
smt_refs = set(layer) - THT
smt_groups, hand_groups = {}, {}
for row in bom_rows:
    ref = row["Designator"]
    lcsc = (row.get("JLCPCB Part #") or "").strip()
    value = (row.get("Value") or "").strip()
    mpn = (row.get("Comment") or "").strip()
    footprint = (row.get("Footprint", "") or "").strip()
    package = "-" if footprint == lcsc or not footprint else footprint
    comment = value or mpn or lcsc
    if ref in smt_refs:
        smt_groups.setdefault((comment, package, lcsc), []).append(ref)
    else:
        hand_groups.setdefault((lcsc, mpn or comment, comment, package), []).append(ref)

with (fab / "froemix-bom-smt.csv").open("w", newline="") as fh:
    w = csv.writer(fh)
    w.writerow(["Comment", "Designator", "Footprint", "LCSC Part #", "Quantity"])
    for (comment, package, lcsc), refs in sorted(smt_groups.items(), key=lambda kv: kv[1][0]):
        w.writerow([comment, ",".join(sorted(refs)), package, lcsc, len(refs)])

with (fab / "froemix-bom-hand.csv").open("w", newline="") as fh:
    w = csv.writer(fh)
    w.writerow(["LCSC Part #", "Quantity", "Comment", "MPN", "Package",
                "Designators", "Side"])
    for (lcsc, mpn, comment, package), refs in sorted(hand_groups.items(), key=lambda kv: kv[1][0]):
        sides = sorted({layer.get(r, "?") for r in refs})
        w.writerow([lcsc, len(refs), comment, mpn, package,
                    ",".join(sorted(refs)), "/".join(sides)])

# The split must be exhaustive and agree with the CPL.
all_refs = {r["Designator"] for r in bom_rows}
split_refs = {r for refs in smt_groups.values() for r in refs} | {r for refs in hand_groups.values() for r in refs}
if all_refs != split_refs:
    sys.exit(f"BOM split lost designators: {sorted(all_refs ^ split_refs)}")
if smt_refs != {r["Designator"] for r in smt}:
    sys.exit("BOM split disagrees with the CPL about which parts are SMT")

hand_count = sum(len(r) for r in hand_groups.values())
print(f"    froemix-bom-smt.csv      {len(smt_groups)} unique parts, {len(smt_refs)} placements (JLC places these)")
print(f"    froemix-bom-hand.csv     {len(hand_groups)} unique parts, {hand_count} placements (hand-solder list)")

missing_lcsc = [r["Designator"] for r in bom_rows if not (r.get("JLCPCB Part #") or "").strip()]
print(f"    froemix-bom-jlc.csv      {len(groups)} unique parts, {len(bom_rows)} placements")
print(f"    froemix-cpl-jlc.csv      {len(smt)} SMT placements ({len(THT)} THT excluded)")
print(f"    froemix-pnp-all.csv      {len(pnp_rows)} placements (reference, incl. THT)")
if missing_lcsc:
    print(f"    WARNING: no JLCPCB part number for {', '.join(missing_lcsc)}")
PY2

# The order-day guide lives in doc/ (fab/ is wiped on every release); copy it in
# so the upload folder is self-documenting.
cp ../doc/JLC-ASSEMBLY.md ../doc/ASSEMBLY.md "$FAB_DIR/"

printf '\n########## 3. artifacts ##########\n'
( cd "$FAB_DIR" && ls -l | awk 'NR>1 { printf "    %-24s %9.1f KB\n", $9, $5/1024 }' )
printf '    folder: %s\n' "$FAB_DIR"

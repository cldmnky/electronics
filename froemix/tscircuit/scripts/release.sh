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

cd "$(dirname "$0")/.."
PROJECT_DIR=$(pwd)
FAB_DIR=$(cd .. && pwd)/fab
ENTRY=index.circuit.tsx

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
with (fab / "froemix-cpl-jlc.csv").open("w", newline="") as fh:
    w = csv.writer(fh)
    w.writerow(["Designator", "Mid X", "Mid Y", "Layer", "Rotation"])
    for r in smt:
        w.writerow([r["Designator"], r["Mid X"], r["Mid Y"], r["Layer"], r["Rotation"]])

(fab / "froemix-pnp-all.csv").write_bytes((fab / "froemix-pnp.csv").read_bytes())
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

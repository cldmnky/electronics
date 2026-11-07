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
export FROEMIX_BUILD_DATE="${FROEMIX_BUILD_DATE:-$(date -u +%F)}"
printf 'Build date (UTC): %s\n' "$FROEMIX_BUILD_DATE"

cd "$(dirname "$0")/.."
PROJECT_DIR=$(pwd)
FAB_DIR=$(cd .. && pwd)/fab/euro
ENTRY=index.circuit.tsx
PANEL=panel.circuit.tsx

# ---------------------------------------------------------------------------
# 0. Physical acceptance, fail closed and FIRST: nothing below this block may
#    build, export or delete before every required physical item is accepted
#    with recorded evidence. fab/euro/ is untouched when the gate blocks (the
#    guard runs before `rm -rf "$FAB_DIR"`). See ../doc/acceptance.md.
# ---------------------------------------------------------------------------
if [ "${FROEMIX_SKIP_ERC:-0}" = "1" ]; then
  printf '\nRELEASE REFUSED: FROEMIX_SKIP_ERC=1\n' >&2
  printf 'An ERC skip is a prototype convenience. A production release must run ERC.\n' >&2
  printf 'Nothing was built, exported or deleted. Unset FROEMIX_SKIP_ERC to release.\n' >&2
  exit 1
fi
printf '\n########## 0. physical acceptance (fail closed) ##########\n'
if ! bun run ../tools/release/gate.ts --check; then
  printf '\nRELEASE BLOCKED: physical acceptance is not complete.\n' >&2
  printf 'Nothing was built, exported or deleted; %s is untouched.\n' "$FAB_DIR" >&2
  printf 'A pending item is normal today: see ../doc/acceptance.md and ../doc/evidence/README.md.\n' >&2
  exit 1
fi

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

# Use the panel's real Gerber/profile/drill verification, not an unchecked
# direct export. The existing order-file path stays stable.
bun scripts/panel_release.ts
# panel_release.ts records the design it actually exported; naming the file from
# that value keeps this copy in step with DEFAULT_PANEL_DESIGN / --design.
DESIGN=$(cat dist/panel-release/selected-design.txt)
cp "dist/panel-release/froemix-panel-$DESIGN-gerbers.zip" "$FAB_DIR/panel/panel-gerbers.zip"
cp "dist/panel-release/panel-release-$DESIGN.json" "$FAB_DIR/panel/"
mkdir -p "$FAB_DIR/panel/preview"
cp dist/panel-release/preview/* "$FAB_DIR/panel/preview/"
export_fmt "$PANEL" step "$FAB_DIR/panel/panel.step"
bun scripts/mechanical_release.ts "$FAB_DIR/mechanical"
cp ../doc/FRONT-PANEL.md "$FAB_DIR/panel/"

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

# JLCPCB assembly files, same convention as the base board: their SMT order takes
# a BOM (one row per part, grouped by designator, with quantity) and a CPL (one
# row per SMT placement). The tscircuit exports are per-designator and include
# the through-hole parts in the pick-and-place, so derive the JLC-shaped
# versions here instead of hand-editing on the order page.
python3 - "$FAB_DIR" <<'PY2'
import csv
import pathlib
import re
import sys

fab = pathlib.Path(sys.argv[1])
# Through-hole parts: hand-soldered unless JLC's through-hole service is
# ordered. Everything not listed here must be SMT and is checked below.
THT = {
    "CN1", "CN2", "CN4",                       # 2.00mm mating sockets
    "J3", "J4", "J5", "J6", "J7", "J8",       # mono jacks
    "J9", "J10", "J11", "J12",                 # CV jacks
    "J13", "J14",                              # TRS MIDI jacks
    "RV1", "RV2", "RV3", "RV4", "RV5", "RV6",  # pots
    "SW1", "SW2",                              # joystick, button
    "DL5", "DL6",                              # 3mm CV jack LEDs
    "D1",                                       # 3mm panel LED
    "DS1",                                      # OLED module header
}
SMT_RE = re.compile(r"^(R\d+|D[2-7])$")

bom_rows = list(csv.DictReader((fab / "euro-bom.csv").open()))
pnp_rows = list(csv.DictReader((fab / "euro-pnp.csv").open()))

unknown = [r["Designator"] for r in bom_rows
           if r["Designator"] not in THT and not SMT_RE.match(r["Designator"])]
if unknown:
    sys.exit(f"euro release: unclassified designators (add to THT or SMT_RE): {', '.join(unknown)}")

# JLCPCB reads "Comment" as the human description (1k, 2.2k, ...). The exported
# Comment column holds the manufacturer part number, so prefer Value, then the
# LCSC code for modules whose value is the part number.
groups = {}
for row in bom_rows:
    lcsc = (row.get("JLCPCB Part #") or "").strip()
    value = (row.get("Value") or "").strip()
    mpn = (row.get("Comment") or "").strip()
    # passives carry a human value ("10k"), modules carry an MPN; the LCSC code
    # is only a last resort so nothing lands on the order page blank.
    if value and value != lcsc:
        comment = value
    elif mpn and mpn != lcsc:
        comment = mpn
    else:
        comment = lcsc
    footprint = (row.get("Footprint", "") or "").strip()
    # tscircuit repeats the LCSC code in Footprint for parts without a package
    # name; JLCPCB resolves the footprint from the part number, so write "-".
    if footprint == lcsc or not footprint:
        footprint = "-"
    groups.setdefault((comment, footprint, lcsc), []).append(row["Designator"])

with (fab / "euro-bom-jlc.csv").open("w", newline="") as fh:
    w = csv.writer(fh)
    w.writerow(["Comment", "Designator", "Footprint", "LCSC Part #", "Quantity"])
    for (comment, footprint, lcsc), refs in sorted(groups.items(), key=lambda kv: kv[1][0]):
        w.writerow([comment, ",".join(sorted(refs)), footprint, lcsc, len(refs)])

smt = [r for r in pnp_rows if r["Designator"] not in THT]
with (fab / "euro-cpl-jlc.csv").open("w", newline="") as fh:
    w = csv.writer(fh)
    w.writerow(["Designator", "Mid X", "Mid Y", "Layer", "Rotation"])
    for r in smt:
        w.writerow([r["Designator"], r["Mid X"], r["Mid Y"], r["Layer"], r["Rotation"]])

(fab / "euro-pnp-all.csv").write_bytes((fab / "euro-pnp.csv").read_bytes())

# Upload-sized split: euro-bom-smt.csv has only what JLCPCB places, and
# euro-bom-hand.csv is the shopping list for the hand-soldered through-hole side.
layer = {r["Designator"]: r["Layer"] for r in pnp_rows}
smt_groups, hand_groups = {}, {}
for row in bom_rows:
    ref = row["Designator"]
    lcsc = (row.get("JLCPCB Part #") or "").strip()
    value = (row.get("Value") or "").strip()
    mpn = (row.get("Comment") or "").strip()
    footprint = (row.get("Footprint", "") or "").strip()
    package = "-" if footprint == lcsc or not footprint else footprint
    comment = value if value and value != lcsc else (mpn if mpn and mpn != lcsc else lcsc)
    if ref in THT:
        hand_groups.setdefault((lcsc, mpn or comment, comment, package), []).append(ref)
    else:
        smt_groups.setdefault((comment, package, lcsc), []).append(ref)

with (fab / "euro-bom-smt.csv").open("w", newline="") as fh:
    w = csv.writer(fh)
    w.writerow(["Comment", "Designator", "Footprint", "LCSC Part #", "Quantity"])
    for (comment, package, lcsc), refs in sorted(smt_groups.items(), key=lambda kv: kv[1][0]):
        w.writerow([comment, ",".join(sorted(refs)), package, lcsc, len(refs)])

with (fab / "euro-bom-hand.csv").open("w", newline="") as fh:
    w = csv.writer(fh)
    w.writerow(["LCSC Part #", "Quantity", "Comment", "MPN", "Package",
                "Designators", "Side"])
    for (lcsc, mpn, comment, package), refs in sorted(hand_groups.items(), key=lambda kv: kv[1][0]):
        sides = sorted({layer.get(r, "?") for r in refs})
        w.writerow([lcsc, len(refs), comment, mpn, package,
                    ",".join(sorted(refs)), "/".join(sides)])

all_refs = {r["Designator"] for r in bom_rows}
split_refs = {r for refs in smt_groups.values() for r in refs} | {r for refs in hand_groups.values() for r in refs}
if all_refs != split_refs:
    sys.exit(f"BOM split lost designators: {sorted(all_refs ^ split_refs)}")

hand_count = sum(len(r) for r in hand_groups.values())
print(f"    euro-bom-smt.csv         {len(smt_groups)} unique parts, {sum(len(r) for r in smt_groups.values())} placements (JLC places these)")
print(f"    euro-bom-hand.csv        {len(hand_groups)} unique parts, {hand_count} placements (hand-solder list)")

# Cross-checks: every SMT part must be in the CPL, every CPL row must be an SMT
# part, and nothing may be silently missing from either file.
bom_smt = {r["Designator"] for r in bom_rows if SMT_RE.match(r["Designator"])}
cpl_smt = {r["Designator"] for r in smt}
if bom_smt != cpl_smt:
    sys.exit("euro release: SMT BOM/CPL mismatch "
             f"(bom-only {sorted(bom_smt - cpl_smt)}, cpl-only {sorted(cpl_smt - bom_smt)})")
missing_lcsc = [r["Designator"] for r in bom_rows if not (r.get("JLCPCB Part #") or "").strip()]
print(f"    euro-bom-jlc.csv         {len(groups)} unique parts, {len(bom_rows)} placements")
print(f"    euro-cpl-jlc.csv         {len(smt)} SMT placements ({len(THT)} THT hand-solder)")
print(f"    euro-pnp-all.csv         {len(pnp_rows)} placements (reference, incl. THT)")
if missing_lcsc:
    print(f"    WARNING: no JLCPCB part number for {', '.join(missing_lcsc)}")
PY2

printf '\n########## 3. artifacts ##########\n'
( cd "$FAB_DIR" && find . -type f | sort | while read -r f; do
    bytes=$(wc -c < "$f" | tr -d ' ')
    printf '    %-28s %9.1f KB\n' "${f#./}" "$(awk -v b="$bytes" 'BEGIN {printf "%.1f", b/1024}')"
  done )
printf '    folder: %s\n' "$FAB_DIR"

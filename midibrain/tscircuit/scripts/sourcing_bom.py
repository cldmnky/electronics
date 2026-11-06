#!/usr/bin/env python3
"""JLCPCB BOM/CPL and sourcing-BOM builder for the MIDI Brain release.

Used by ``tscircuit/scripts/release.sh`` after the fab exports, and covered by
``test_sourcing_bom.py``. Everything here is strict on purpose:

* every CSV row must have exactly the header's field count — a missing or an
  extra field is an error on *every* row, not only the first one, because a
  short/long row silently shifts Package/Side/Method/Notes;
* the curated list's Notes carry sourcing warnings (for example the 3 A rating
  of CAB-15455), so a parse that drops a field would drop a warning too;
* side and count expectations are pinned so a design change cannot silently
  move a placement to the other side or change the order-day quantities.

Known history: 15 rows of ``sourcing-extras.csv`` once carried an extra empty
field (an extra empty column between LCSC and Package). It was not an unquoted
comma inside Notes. The data is fixed and ``read_csv_strict`` now rejects any
row whose field count differs from the header.

The parsers use ``csv.reader(..., strict=True)`` and reject malformed quotes.
Designators must be unique and non-empty in both the BOM and the CPL, and an
SMT line needs a real LCSC C-number (``C`` plus digits), not merely some text
in the supplier column. Blank optional fields stay blank.

The front encoder U6 stays out of the SMT CPL, but its method is not silently
set to "hand": hand soldering that part is not recommended, so the row asks for
a qualified THT process until an evidenced process is selected.

This module writes only inside the fab directory it is given. It does not
approve an order: see docs/acceptance.md for the physical gates.
"""
from __future__ import annotations

import csv
import io
import json
import pathlib
import re
import sys
import zipfile
from typing import Iterable, Sequence

COLUMNS = ["Board", "Designators", "Quantity", "MPN", "LCSC", "Package",
           "Side", "Method", "Source", "Notes"]

# Through-hole parts per board: JLCPCB's standard SMT service cannot place
# these. They stay in the combined BOM (they still have to be bought) and stay
# out of the SMT CPL. Front THT carries LCSC numbers; rear THT (sockets and
# pin headers) has none and must be sourced generically for hand soldering.
THT = {
    "front": {"J1", "J2", "J3", "J4",
              "SW1", "SW2", "SW3", "SW4", "SW5", "SW6",
              # Panel POWER momentary and the 1x2 jumper header that takes the
              # owner's 6-inch F-F DuPont pair to the Pi 5 PWR connector.
              "SW7", "J_PWR",
              "U6", "J5"},
    "rear": {"J_PICO_L", "J_PICO_R", "J_PI", "J_FRONT"},
}

# Expected SMT assembly sides, pinned so a future edit cannot silently move a
# part to the other side. Both boards are single-sided for JLCPCB: front SMT
# all on top (panel side), rear SMT all on bottom. Front J5 stays bottom - it
# is THT and must mate rearward - and THT bodies live wherever they mate.
EXPECTED_SMT_SIDE = {"front": "top", "rear": "bottom"}

EXPECTED_COUNT_KEYS = ("bom_placements", "cpl_smt_placements",
                       "cpl_smt_top", "cpl_smt_bottom", "tht_excluded")

BOARD_FILES = {"front": "front.circuit.tsx", "rear": "index.circuit.tsx"}

# Columns the tscircuit exports must provide; a rename has to fail here rather
# than write an order file with a missing field.
BOM_REQUIRED_COLUMNS = ("Designator", "JLCPCB Part #", "Footprint")
PNP_REQUIRED_COLUMNS = ("Designator", "Mid X", "Mid Y", "Layer", "Rotation")

LCSC_C_NUMBER = re.compile(r"^C\d+$")

# THT that needs an evidenced process rather than an assumption. The Bourns
# PEC11R encoder is not recommended for hand soldering, so its row must not
# claim "hand". It stays out of the SMT CPL either way.
THT_DEFAULT_METHOD = "hand"
THT_QUALIFIED_PROCESS = {
    "front": {
        "U6": (
            "qualified THT process required (hand solder not recommended; "
            "selective/wave or validated manual process to be evidenced)",
            "Encoder U6: hand solder not recommended by the manufacturer; "
            "process not yet qualified - pending acceptance item "
            "encoder_solder_process",
        ),
    },
}


class SourcingError(Exception):
    """A BOM/CPL/sourcing input is malformed or contradicts the design."""


def read_csv_strict(path: pathlib.Path, columns: Sequence[str] | None = None
                    ) -> tuple[list[str], list[dict[str, str]]]:
    """Read a CSV, rejecting any row whose field count differs from the header.

    ``columns`` pins the required header (same names, same order). Blank lines,
    duplicate or empty header names and rows with no Board/Designators value
    are errors. Line numbers are reported for the offending row.
    """
    path = pathlib.Path(path)
    try:
        text = path.read_text(encoding="utf-8")
    except FileNotFoundError as exc:
        raise SourcingError(f"{path}: missing file") from exc
    except UnicodeDecodeError as exc:
        raise SourcingError(f"{path}: not UTF-8 text: {exc}") from exc
    if not text.strip():
        raise SourcingError(f"{path}: empty file")

    try:
        rows = list(csv.reader(io.StringIO(text), strict=True))
    except csv.Error as exc:
        raise SourcingError(f"{path}: malformed CSV: {exc}") from exc
    header = rows[0]
    if not header:
        raise SourcingError(f"{path}: empty header row")
    if any(not h.strip() for h in header):
        raise SourcingError(f"{path}: header has an empty column name: {header}")
    if len(set(header)) != len(header):
        raise SourcingError(f"{path}: duplicate header column: {header}")
    if columns is not None and list(header) != list(columns):
        raise SourcingError(
            f"{path}: header {header} != required columns {list(columns)}")

    out: list[dict[str, str]] = []
    diagnostics: list[str] = []
    for offset, row in enumerate(rows[1:], start=2):
        if not row or all(not field.strip() for field in row):
            diagnostics.append(f"{path}:{offset}: blank row")
            continue
        if len(row) != len(header):
            diagnostics.append(
                f"{path}:{offset}: {len(row)} fields, header has {len(header)} "
                f"({row[0] if row else '<blank>'})")
            continue
        out.append(dict(zip(header, row)))
    if diagnostics:
        raise SourcingError("; ".join(diagnostics))

    for i, row in enumerate(out, start=2):
        if "Board" in row and "Designators" in row:
            if not row["Board"].strip() or not row["Designators"].strip():
                raise SourcingError(f"{path}:{i}: empty Board or Designators")
    return list(header), out


def norm(row: dict[str, str], key: str) -> str:
    return (row.get(key) or "").strip()


def read_zip_csv(zf: zipfile.ZipFile, member: str, name: str,
                 required: Sequence[str] = (),
                 allow_empty: bool = False) -> tuple[list[str], list[dict[str, str]]]:
    """Read a CSV from a gerbers zip.

    ``allow_empty`` exists for the panel only: the exporter writes zero-byte
    ``bom.csv``/``pick_and_place.csv`` for a board with no components. Front
    and rear must still fail on an empty file, because their BOM/CPL are the
    order input.
    """
    if member not in set(zf.namelist()):
        raise SourcingError(f"{name}: no {member} in the gerbers zip")
    text = zf.read(member).decode("utf-8")
    if allow_empty and not text.strip():
        return [], []
    try:
        rows = list(csv.reader(io.StringIO(text), strict=True))
    except csv.Error as exc:
        raise SourcingError(f"{name}: {member}: malformed CSV: {exc}") from exc
    if not rows:
        raise SourcingError(f"{name}: {member} is empty")
    header = rows[0]
    missing = [c for c in required if c not in header]
    if missing:
        raise SourcingError(f"{name}: {member} is missing columns {missing} "
                            f"(header {header})")
    out = []
    for offset, row in enumerate(rows[1:], start=2):
        if not row or all(not field.strip() for field in row):
            continue
        if len(row) != len(header):
            raise SourcingError(
                f"{name}: {member} line {offset}: {len(row)} fields, "
                f"header has {len(header)}")
        out.append(dict(zip(header, row)))
    return header, out


def _write_csv(path: pathlib.Path, header: Sequence[str],
               rows: Iterable[Sequence[object]]) -> None:
    with path.open("w", newline="") as fh:
        writer = csv.writer(fh)
        writer.writerow(header)
        writer.writerows(rows)


def process_board(name: str, bom_rows: list[dict[str, str]],
                  pnp_rows: list[dict[str, str]], out_dir: pathlib.Path
                  ) -> dict[str, int]:
    """Write the JLCPCB and bench BOM/CPL files for one board.

    Returns the placement counts, which ``verify_expected_counts`` compares with
    ``jlc-order.json``.
    """
    out_dir = pathlib.Path(out_dir)
    if name not in THT:
        raise SourcingError(f"unknown board {name!r}")

    def designators(rows: list[dict[str, str]], label: str) -> set[str]:
        seen: set[str] = set()
        for row in rows:
            ref = norm(row, "Designator")
            if not ref:
                raise SourcingError(f"{name}: {label} row with an empty Designator")
            if ref in seen:
                raise SourcingError(f"{name}: duplicate Designator {ref} in the {label}")
            seen.add(ref)
        return seen

    bom_refs = designators(bom_rows, "BOM")
    pnp_refs = designators(pnp_rows, "CPL")
    if bom_refs != pnp_refs:
        raise SourcingError(
            f"{name}: BOM and CPL disagree on designators: "
            f"{sorted(bom_refs ^ pnp_refs)}")

    layers = {norm(r, "Designator"): norm(r, "Layer") for r in pnp_rows}
    # Every designator is either pinned THT or SMT. A new part therefore has
    # to be added to THT above, or it must pass the LCSC and side checks below.
    smt_refs = set(layers) - THT[name]

    # Owner rule, enforced: every SMT line item needs a real LCSC C-number
    # (C plus digits), not merely a non-empty supplier string.
    bad_lcsc = []
    for row in bom_rows:
        ref = norm(row, "Designator")
        if ref not in smt_refs:
            continue
        lcsc = norm(row, "JLCPCB Part #")
        if not LCSC_C_NUMBER.match(lcsc):
            bad_lcsc.append(f"{ref}={lcsc or '<blank>'}")
    if bad_lcsc:
        raise SourcingError(f"{name}: SMT line without a valid LCSC C-number "
                            f"(C + digits): {', '.join(sorted(bad_lcsc))}")

    # Pinned assembly sides: every SMT placement on the expected side.
    want_side = EXPECTED_SMT_SIDE[name]
    wrong_side = sorted(r for r in smt_refs if layers[r].lower() != want_side)
    if wrong_side:
        raise SourcingError(f"{name}: SMT off the {want_side} side: {wrong_side}")

    def pkg(row: dict[str, str]) -> str:
        lcsc = norm(row, "JLCPCB Part #")
        fp = norm(row, "Footprint")
        return "-" if fp == lcsc or not fp else fp

    # Combined upload BOM: one row per unique part (THT included).
    groups: dict[tuple[str, str, str], list[str]] = {}
    for row in bom_rows:
        comment = norm(row, "Value") or norm(row, "Comment")
        key = (comment, pkg(row), norm(row, "JLCPCB Part #"))
        groups.setdefault(key, []).append(norm(row, "Designator"))
    _write_csv(out_dir / f"{name}-bom-jlc.csv",
               ["Comment", "Designator", "Footprint", "LCSC Part #", "Quantity"],
               [[comment, ",".join(sorted(refs)), footprint, lcsc, len(refs)]
                for (comment, footprint, lcsc), refs
                in sorted(groups.items(), key=lambda kv: sorted(kv[1])[0])])

    # SMT-only CPL for the assembly order.
    smt = [r for r in pnp_rows if norm(r, "Designator") in smt_refs]
    _write_csv(out_dir / f"{name}-cpl-jlc.csv",
               ["Designator", "Mid X", "Mid Y", "Layer", "Rotation"],
               [[norm(r, "Designator"), norm(r, "Mid X"), norm(r, "Mid Y"),
                 norm(r, "Layer"), norm(r, "Rotation")] for r in smt])

    # Split BOMs: smt for the JLC order page (nothing to un-tick), hand for
    # the bench. THT keeps its LCSC numbers where it has them.
    def tht_process(ref: str) -> tuple[str, str]:
        """(Method, extra Notes) for a hand/qualified-process line."""
        special = THT_QUALIFIED_PROCESS.get(name, {}).get(ref)
        if special:
            return special
        return THT_DEFAULT_METHOD, ""

    smt_groups: dict[tuple[str, str, str], list[str]] = {}
    hand_groups: dict[tuple[str, str, str, str, str], list[str]] = {}
    for row in bom_rows:
        ref = norm(row, "Designator")
        lcsc = norm(row, "JLCPCB Part #")
        value = norm(row, "Value")
        mpn = norm(row, "Comment")
        comment = value or mpn or lcsc
        if ref in smt_refs:
            smt_groups.setdefault((comment, pkg(row), lcsc), []).append(ref)
        else:
            method, extra = tht_process(ref)
            # Blank optional fields stay blank (footprint, LCSC); only the
            # combined JLC BOM substitutes "-" for a footprint JLCPCB needs.
            note = "; ".join(part for part in (comment, extra) if part)
            hand_groups.setdefault((lcsc, mpn or comment, method,
                                    norm(row, "Footprint"), note),
                                   []).append(ref)
    _write_csv(out_dir / f"{name}-bom-smt.csv",
               ["Comment", "Designator", "Footprint", "LCSC Part #", "Quantity"],
               [[comment, ",".join(sorted(refs)), package, lcsc, len(refs)]
                for (comment, package, lcsc), refs
                in sorted(smt_groups.items(), key=lambda kv: sorted(kv[1])[0])])
    _write_csv(out_dir / f"{name}-bom-hand.csv", COLUMNS,
               [[name, ",".join(sorted(refs)), len(refs), mpn, lcsc, footprint,
                 "/".join(sorted({layers.get(r, "?") for r in refs})), method,
                 "LCSC/JLCPCB" if lcsc else "generic (no LCSC in design)", note]
                for (lcsc, mpn, method, footprint, note), refs
                in sorted(hand_groups.items(), key=lambda kv: sorted(kv[1])[0])])

    all_refs = bom_refs
    split_refs = ({r for refs in smt_groups.values() for r in refs}
                  | {r for refs in hand_groups.values() for r in refs})
    if all_refs != split_refs:
        raise SourcingError(f"{name}: BOM split lost designators: "
                            f"{sorted(all_refs ^ split_refs)}")

    return {
        "bom_placements": len(bom_rows),
        "cpl_smt_placements": len(smt),
        "cpl_smt_top": sum(1 for r in smt if norm(r, "Layer").lower() == "top"),
        "cpl_smt_bottom": sum(1 for r in smt if norm(r, "Layer").lower() == "bottom"),
        "tht_excluded": len(THT[name] & set(layers)),
    }


def merge_sourcing(hand_paths: Sequence[pathlib.Path],
                   extras_path: pathlib.Path, out_path: pathlib.Path
                   ) -> list[dict[str, str]]:
    """Merge the per-board hand BOMs with the curated extras list.

    Every file is read strictly, so a shifted row cannot lose a sourcing
    warning (3 A cable rating, unproven module, TBD hardware).
    """
    merged: list[dict[str, str]] = []
    for path in hand_paths:
        path = pathlib.Path(path)
        _, rows = read_csv_strict(path, COLUMNS)
        merged.extend(rows)
    _, extras = read_csv_strict(pathlib.Path(extras_path), COLUMNS)
    merged.extend(extras)
    _write_csv(pathlib.Path(out_path), COLUMNS,
               [[row[c] for c in COLUMNS] for row in merged])
    return merged


def verify_expected_counts(order_path: pathlib.Path,
                           counts: dict[str, dict[str, int]]) -> None:
    """Compare generated placement counts with jlc-order.json.

    A mismatch means the design changed after the order settings were written.
    The message gives the exact delta per board and key, so the order file can
    be updated from the generated output instead of guessed.
    """
    order_path = pathlib.Path(order_path)
    try:
        order = json.loads(order_path.read_text(encoding="utf-8"))
    except FileNotFoundError as exc:
        raise SourcingError(f"{order_path}: missing file") from exc
    except json.JSONDecodeError as exc:
        raise SourcingError(f"{order_path}: invalid JSON: {exc}") from exc

    problems: list[str] = []
    for board in ("front", "rear"):
        entry = order.get(board)
        if not isinstance(entry, dict):
            problems.append(f"{board}: missing from {order_path.name}")
            continue
        expected = (entry.get("assembly") or {}).get("expected_counts")
        if not isinstance(expected, dict):
            problems.append(f"{board}: assembly.expected_counts missing")
            continue
        missing = [k for k in EXPECTED_COUNT_KEYS if k not in expected]
        if missing:
            problems.append(f"{board}: expected_counts missing {missing}")
        got = counts.get(board)
        if got is None:
            problems.append(f"{board}: no generated counts")
            continue
        for key in EXPECTED_COUNT_KEYS:
            if key not in expected:
                continue
            if expected[key] != got[key]:
                problems.append(
                    f"{board}.{key}: jlc-order.json says {expected[key]}, "
                    f"generated {got[key]} (delta {got[key] - expected[key]:+d})")
    if problems:
        raise SourcingError("; ".join(problems))


def verify_panel_empty(bom_rows: list[dict[str, str]],
                       pnp_rows: list[dict[str, str]]) -> None:
    if bom_rows or pnp_rows:
        raise SourcingError(
            f"panel: expected no components, got BOM rows {len(bom_rows)} "
            f"PNP rows {len(pnp_rows)}")


def main(argv: Sequence[str]) -> int:
    if len(argv) != 4:
        print("usage: sourcing_bom.py <fab-dir> <sourcing-extras.csv> "
              "<jlc-order.json>", file=sys.stderr)
        return 2
    fab = pathlib.Path(argv[1])
    extras = pathlib.Path(argv[2])
    order = pathlib.Path(argv[3])

    try:
        counts: dict[str, dict[str, int]] = {}
        for name in ("front", "rear"):
            d = fab / name
            with zipfile.ZipFile(d / f"{name}-gerbers.zip") as zf:
                _, bom_rows = read_zip_csv(zf, "bom.csv", name,
                                           BOM_REQUIRED_COLUMNS)
                _, pnp_rows = read_zip_csv(zf, "pick_and_place.csv", name,
                                           PNP_REQUIRED_COLUMNS)
                (d / "bom.csv").write_bytes(zf.read("bom.csv"))
                (d / "pnp-all.csv").write_bytes(zf.read("pick_and_place.csv"))
            print(f"    {name}: extracted bom.csv + pick_and_place.csv")
            counts[name] = process_board(name, bom_rows, pnp_rows, d)
            print(f"    {name}-bom-smt.csv   "
                  f"{counts[name]['cpl_smt_placements']} placements "
                  f"(all {EXPECTED_SMT_SIDE[name]}, single-sided)")

        pd = fab / "panel"
        with zipfile.ZipFile(pd / "panel-gerbers.zip") as zf:
            # The panel has no components, so the exporter emits zero-byte
            # bom.csv/pick_and_place.csv. That is the documented shape; any
            # parsed row still fails verify_panel_empty below.
            _, panel_bom = read_zip_csv(zf, "bom.csv", "panel", allow_empty=True)
            _, panel_pnp = read_zip_csv(zf, "pick_and_place.csv", "panel",
                                        allow_empty=True)
        verify_panel_empty(panel_bom, panel_pnp)
        print("    panel: no components (fabrication only, no BOM/CPL)")

        verify_expected_counts(order, counts)
        print("    jlc-order.json placement counts match the generated files")

        merged = merge_sourcing(
            [(fab / "front" / "front-bom-hand.csv"),
             (fab / "rear" / "rear-bom-hand.csv")],
            extras, fab / "sourcing-bom.csv")
        bench = sum(1 for r in merged if r["Board"] in ("front", "rear"))
        print(f"    sourcing-bom.csv   {len(merged)} rows "
              f"({bench} bench + {len(merged) - bench} modules/cables/hardware)")
        print("    NOTE: stock and package fit must be rechecked at order time "
              "(owner rule)")
    except SourcingError as exc:
        print(f"FAIL {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))

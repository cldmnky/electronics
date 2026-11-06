#!/usr/bin/env python3
"""JLCPCB order-sheet printer for MIDI Brain.

`make order` runs the shared fab pipeline (the same export steps `make release`
runs after its physical acceptance gate) and then calls this script. It prints
the upload files per board, the order settings from `jlc-order.json`, the guide
references and the order-page tick list.

Ordering is not blocked by the acceptance gate: `hardware-acceptance.json` is
the separate evidence harness that `make release` enforces. This sheet only
prints a packet that the pipeline just generated; it does not approve one.

The counts printed here come from `jlc-order.json`. They were already checked
against the generated BOM/CPL by `sourcing_bom.py` during the export.
"""
from __future__ import annotations

import json
import pathlib
import sys

BOARDS = ("front", "rear", "panel")
UPLOAD_FILES = {
    "front": ("front/front-gerbers.zip", "front/front-bom-smt.csv",
              "front/front-cpl-jlc.csv", "front/front-bom-hand.csv"),
    "rear": ("rear/rear-gerbers.zip", "rear/rear-bom-smt.csv",
             "rear/rear-cpl-jlc.csv", "rear/rear-bom-hand.csv"),
    "panel": ("panel/panel-gerbers.zip",),
}


class SheetError(Exception):
    """The generated packet is incomplete or the order settings are missing."""


def num(value: object) -> str:
    return f"{value:g}" if isinstance(value, (int, float)) else str(value)


def board_line(name: str, entry: dict) -> str:
    board = entry.get("board") or {}
    dims = board.get("dimensions_mm") or []
    dims_text = " x ".join(num(d) for d in dims) if dims else "dimensions missing"
    if dims:
        dims_text += " mm"
    bits = [dims_text, f"{num(board.get('layers'))} layers",
            f"{num(board.get('thickness_mm'))} mm"]
    if name == "panel":
        bits.append("no copper, no solder mask, fabrication only")
    else:
        bits.append(f"{num(board.get('outer_copper_oz'))} oz copper")
        bits.append(str(board.get("surface_finish", "finish not set")))
        counts = (entry.get("assembly") or {}).get("expected_counts") or {}
        side = "top" if counts.get("cpl_smt_top") else "bottom"
        bits.append(f"SMT {side} only ({num(counts.get('cpl_smt_placements'))})")
    return f"  {name:<7}: " + ", ".join(bits)


def build_sheet(root: pathlib.Path, fab: pathlib.Path) -> list[str]:
    if not fab.is_dir():
        raise SheetError(f"{fab} does not exist; run `make order`")
    missing = [name for name in BOARDS
               if not all((fab / rel).is_file() for rel in UPLOAD_FILES[name])]
    if missing:
        raise SheetError(f"{fab} is incomplete for {missing}; rerun `make order`")

    try:
        order = json.loads((root / "jlc-order.json").read_text())
    except (OSError, ValueError) as exc:
        raise SheetError(f"cannot read jlc-order.json: {exc}") from exc

    front = (order.get("front") or {}).get("assembly") or {}
    rear = (order.get("rear") or {}).get("assembly") or {}
    fc = front.get("expected_counts") or {}
    rc = rear.get("expected_counts") or {}

    lines = [
        "=== JLCPCB order: MIDI Brain =======================================",
        "boards  : front/UI, rear/base (20HP) and panel (26HP/3U)",
        "intent  : production order for all three boards",
        "settings: fab/jlc-order.json (copied from the canonical jlc-order.json)",
        "guide   : docs/JLC-ASSEMBLY.md + fab/JLC-ASSEMBLY.md (order page);",
        "          docs/assembly.md + fab/assembly.md (bench assembly)",
        "check   : recheck LCSC stock/package and the assembly preview before paying",
        "",
        "boards  :",
    ]
    for name in BOARDS:
        lines.append(board_line(name, order.get(name) or {}))
    lines += [
        "",
        "front/UI board - upload these:",
        "  gerbers : fab/front/front-gerbers.zip",
        f"  BOM SMT : fab/front/front-bom-smt.csv   ({num(fc.get('bom_placements'))} BOM placements, {num(fc.get('cpl_smt_placements'))} SMT)",
        "  CPL     : fab/front/front-cpl-jlc.csv",
        f"  bench   : fab/front/front-bom-hand.csv  ({num(fc.get('tht_excluded'))} THT incl. U6 PEC11R-4015F-S0024 C143789: JLC THT if offered, else bench-qualify)",
        "",
        "rear/base board - upload these:",
        "  gerbers : fab/rear/rear-gerbers.zip",
        f"  BOM SMT : fab/rear/rear-bom-smt.csv    ({num(rc.get('bom_placements'))} BOM placements, {num(rc.get('cpl_smt_placements'))} SMT)",
        "  CPL     : fab/rear/rear-cpl-jlc.csv",
        f"  bench   : fab/rear/rear-bom-hand.csv   ({num(rc.get('tht_excluded'))} THT headers/sockets)",
        "",
        "panel - upload these (fabrication only):",
        "  gerbers : fab/panel/panel-gerbers.zip",
        "  no BOM / no CPL: the panel has zero components",
        "",
        "bench sourcing: fab/sourcing-bom.csv (modules, cables and hardware; JLC does not mount THT)",
        "",
        "order-page tick list:",
        "  [ ] three orders: front + rear with SMT assembly; panel fabrication only",
        f"  [ ] front: 2 layers, 1.6 mm, 1 oz, lead-free HASL, SMT top only ({num(fc.get('cpl_smt_placements'))})",
        f"  [ ] rear : 2 layers, 1.6 mm, 1 oz, lead-free HASL, SMT bottom only ({num(rc.get('cpl_smt_placements'))})",
        "  [ ] panel: 2 layers, 1.6 mm, no copper, no solder mask both sides (confirm JLC accepts)",
        "  [ ] THT: buy/fit the hand BOMs separately; U6 PEC11R-4015F-S0024 C143789 (THT)",
        "      JLC THT assembly if offered, else bench-qualify per docs/assembly.md;",
        "      no catalog SMD 0-detent 24 PPR encoder with anchor lugs; lug-less SMD not accepted",
        "  [ ] preview both assembly sides: rotations, polarity/pin-1, drill, mask, silk",
        "  [ ] record DFM/CPL findings in docs/evidence/ (scope_dfm_cpl)",
        "====================================================================",
    ]
    return lines


def main(argv: list[str]) -> int:
    if len(argv) > 1:
        print(f"usage: order_sheet.py (no arguments; got {argv[1:]})",
              file=sys.stderr)
        return 2
    root = pathlib.Path(__file__).resolve().parents[2]
    try:
        lines = build_sheet(root, root / "fab")
    except SheetError as exc:
        print(f"FAIL {exc}", file=sys.stderr)
        return 1
    print("\n".join(lines))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))

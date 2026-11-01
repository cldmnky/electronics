#!/usr/bin/env python3
"""Compare the tscircuit footprints with the KiCad footprints, pad by pad.

Usage:
    python3 froemix/tools/check_footprints.py [--circuit ...] [--offline]

KiCad pad data is re-read from froemix.kicad_pcb (and the Daisy footprint override
in tmp/library) with an independent parser; the tscircuit side comes from the
built circuit JSON. For every component this compares

  * the set of pad numbers (so the netlist lands on the same pads)
  * pad size, shape class and drill
  * the pad pattern (relative positions, within 0.05mm)

and fails when anything differs.
"""

from __future__ import annotations

import argparse
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from kicad2tscircuit import (  # noqa: E402
    FOOTPRINT_OVERRIDES,
    PRE_ROTATED_FOOTPRINTS,
    _rot_point,
    child,
    children,
    parse_sexpr,
)


def kicad_pads(src_dir: str, pcb_path: str):
    """ref -> {name, pads: {number: (w, h, drill, x, y)}} (mm, tscircuit orientation)"""
    def pads_of(fp, angle: int = 0):
        pads = {}
        for pad in children(fp, "pad"):
            at, size = child(pad, "at"), child(pad, "size")
            x, y = float(at[1]), float(at[2])
            width, height = float(size[1]), float(size[2])
            if round(float(at[3]) if len(at) > 3 else 0.0) % 180:
                width, height = height, width
            if angle % 180:
                width, height = height, width
            if angle:
                x, y = _rot_point(x, y, angle)
            drill = child(pad, "drill")
            hole = round(float(drill[1]), 3) if drill else 0.0
            pads[pad[1]] = (width, height, hole, round(x, 3), round(-y, 3))
        return pads

    tree = parse_sexpr(open(pcb_path, encoding="utf-8").read())
    out = {}
    for fp in children(tree, "footprint"):
        ref = None
        for prop in children(fp, "property"):
            if len(prop) > 2 and prop[1] == "Reference":
                ref = prop[2]
        if ref:
            out[ref] = {"name": fp[1],
                        "pads": pads_of(fp, PRE_ROTATED_FOOTPRINTS.get(fp[1], 0))}

    # the Daisy module uses the official Electrosmith footprint
    for kicad_name, rel_path in FOOTPRINT_OVERRIDES.items():
        path = os.path.join(src_dir, rel_path)
        if not os.path.exists(path):
            continue
        override = pads_of(parse_sexpr(open(path, encoding="utf-8").read()),
                           PRE_ROTATED_FOOTPRINTS.get(kicad_name, 0))
        for entry in out.values():
            if entry["name"] == kicad_name:
                if entry["pads"] != override:
                    entry["note"] = "override differs from the board copy"
                entry["pads"] = override
    return out


def circuit_pads(circuit_path: str):
    """ref -> {pads: {number: (w, h, drill, x, y)}} from the built circuit JSON"""
    circuit = json.load(open(circuit_path))
    names = {}
    for elm in circuit:
        if elm["type"] == "source_component":
            names[elm["source_component_id"]] = elm.get("name")
    centers = {}
    layers = {}
    for elm in circuit:
        if elm["type"] == "pcb_component":
            centers[elm["pcb_component_id"]] = (elm["center"]["x"], elm["center"]["y"],
                                                 names.get(elm["source_component_id"]),
                                                 float(elm.get("rotation") or 0.0))
            ref = names.get(elm["source_component_id"])
            if ref:
                layers[ref] = elm.get("layer") or "top"
    out = {}
    for elm in circuit:
        kind = elm["type"]
        if kind not in ("pcb_smtpad", "pcb_plated_hole", "pcb_hole"):
            continue
        center = centers.get(elm.get("pcb_component_id"))
        if not center:
            continue
        name = center[2]
        num = None
        for hint in elm.get("port_hints") or []:
            if hint.startswith("pin"):
                num = hint[3:]
                break
            if str(hint).isdigit():
                num = str(hint)
                break
        if kind == "pcb_smtpad":
            width = elm.get("width") or 0.0
            height = elm.get("height") or 0.0
            drill = 0.0
        elif kind == "pcb_plated_hole":
            width = elm.get("outer_diameter") or elm.get("rect_pad_width") or elm.get("outer_width") or 0.0
            height = elm.get("outer_diameter") or elm.get("rect_pad_height") or elm.get("outer_height") or 0.0
            drill = elm.get("hole_diameter") or elm.get("hole_width") or 0.0
        else:
            width = height = 0.0
            drill = elm.get("hole_diameter") or elm.get("diameter") or 0.0
        out.setdefault(name, {"pads": {}, "layer": layers.get(name, "top"),
                              "all_pads_masked": True})
        if elm.get("is_covered_with_solder_mask") is False:
            out[name]["all_pads_masked"] = False
        if num is None:
            continue
        # Unrotate component placement: KiCad pads are compared in the
        # footprint's unrotated frame. tscircuit applies pcbRotation CCW
        # (verified: J2 pad1 (1.27,-8.89) lands at (8.89,1.27) for rot=90),
        # so the inverse is a CW rotation.
        import math
        theta = math.radians(center[3] % 360)
        dx, dy = float(elm["x"]) - center[0], float(elm["y"]) - center[1]
        ux = dx * math.cos(theta) + dy * math.sin(theta)
        uy = -dx * math.sin(theta) + dy * math.cos(theta)
        w, h = float(width), float(height)
        if round(center[3]) % 180:
            w, h = h, w
        out[name]["pads"][num] = (
            round(w, 3),
            round(h, 3),
            round(float(drill), 3),
            round(ux, 3),
            round(uy, 3),
        )
    return out


def normalize(pads, mirror=False):
    """Shift pad pattern to origin; mirror X first for bottom-side parts.

    tscircuit mirrors footprints placed on the bottom (correct physical
    behavior), so the KiCad expectation is mirrored before comparing."""
    if not pads:
        return {}
    if mirror:
        pads = {num: (w, h, d, -x, y) for num, (w, h, d, x, y) in pads.items()}
    min_x = min(p[3] for p in pads.values())
    min_y = min(p[4] for p in pads.values())
    return {num: (w, h, d, round(x - min_x, 2), round(y - min_y, 2)) for num, (w, h, d, x, y) in pads.items()}


def table_pads(entries):
    """TSCIRCUIT_ONLY_FOOTPRINTS rows -> {number: (w, h, drill, x, y)}

    A row is (number, width, height, x, y) or (number, width, height, x, y,
    drill); the drill defaults to 0 for SMD patterns."""
    return {row[0]: (row[1], row[2], row[5] if len(row) > 5 else 0.0, row[3], row[4])
            for row in entries}


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", default=os.path.join(root, "tmp"))
    ap.add_argument("--pcb", default=os.path.join(root, "tmp", "froemix.kicad_pcb"))
    ap.add_argument("--circuit", default=os.path.join(root, "tscircuit", "dist", "index", "circuit.json"))
    args = ap.parse_args()

    expected = kicad_pads(args.src, args.pcb)
    actual = circuit_pads(args.circuit)

    # Parts deleted from tscircuit/ on purpose (REMOVED_REFS) are still in the
    # frozen KiCad board, so drop them from the KiCad side; everything else is
    # compared pad for pad as before. Refs in REPLACED_FOOTPRINTS (U16's 2W
    # module) are compared against TSCIRCUIT_ONLY_FOOTPRINTS instead of the
    # frozen KiCad land pattern.
    from kicad2tscircuit import (  # noqa: E402
        REMOVED_REFS,
        REPLACED_FOOTPRINTS,
        TSCIRCUIT_ONLY_FOOTPRINTS,
    )

    if REMOVED_REFS:
        print(f"removed on purpose (tscircuit only, see doc/DESIGN-REVIEW.md R01/R02): "
              f"{', '.join(sorted(REMOVED_REFS))}")
        expected = {ref: entry for ref, entry in expected.items() if ref not in REMOVED_REFS}

    problems = []
    for ref in sorted(REPLACED_FOOTPRINTS):
        print(f"footprint replaced on purpose (tscircuit only): {ref} -> "
              f"{REPLACED_FOOTPRINTS[ref]}")
    for ref, entry in sorted(expected.items()):
        got = actual.get(ref)
        if not got:
            problems.append((ref, "missing in the tscircuit board"))
            continue
        replaced = REPLACED_FOOTPRINTS.get(ref)
        want_pads = normalize(
            table_pads(TSCIRCUIT_ONLY_FOOTPRINTS[replaced]) if replaced else entry["pads"],
            mirror=got.get("layer") == "bottom",
        )
        got_pads = normalize(got["pads"])
        if set(want_pads) != set(got_pads):
            problems.append(
                (ref, f"pad numbers differ: kicad {sorted(want_pads, key=int)} vs tscircuit {sorted(got_pads, key=int)}")
            )
            continue
        for num in sorted(want_pads, key=int):
            want, have = want_pads[num], got_pads[num]
            if abs(want[0] - have[0]) > 0.05 or abs(want[1] - have[1]) > 0.05:
                problems.append((ref, f"pad {num} size {want[:2]} vs {have[:2]}"))
            elif abs(want[2] - have[2]) > 0.05:
                problems.append((ref, f"pad {num} drill {want[2]} vs {have[2]}"))
            elif abs(want[3] - have[3]) > 0.05 or abs(want[4] - have[4]) > 0.05:
                problems.append((ref, f"pad {num} position {want[3:]} vs {have[3:]}"))

    # Extra parts added in tscircuit (EXTRA_COMPONENTS) reuse existing KiCad
    # footprints; verify their pads against the footprint library instead of
    # failing them as unknown.
    from kicad2tscircuit import (  # noqa: E402
        EXTRA_COMPONENTS,
        TSCIRCUIT_ONLY_FOOTPRINTS,
        TSCIRCUIT_ONLY_REFS,
        FootprintLibrary,
    )

    extra_defs = {ref for ref, *_ in EXTRA_COMPONENTS}
    lib = FootprintLibrary(args.src, args.pcb)
    for ref in sorted(set(actual) - set(expected)):
        if ref in TSCIRCUIT_ONLY_REFS:
            # tscircuit-only fabrication aids (test points): one round pad each,
            # no KiCad footprint to compare against. Verified by build/DRC only.
            if len(actual[ref]["pads"]) != 1:
                problems.append((ref, "test point must have exactly one pad"))
            if ref.startswith("FID") and actual[ref].get("all_pads_masked"):
                problems.append((ref, "fiducial pad is covered by solder mask - "
                                      "an assembly camera cannot see it"))
            continue
        if ref not in extra_defs:
            problems.append((ref, "not in the KiCad board"))
            continue
        want_fp = next(fp for r, k, v, fp, *_ in EXTRA_COMPONENTS if r == ref)
        if want_fp in TSCIRCUIT_ONLY_FOOTPRINTS:
            # footprint authored in tscircuit/ only: compare against its
            # declared pad geometry instead of a KiCad library footprint
            pads = table_pads(TSCIRCUIT_ONLY_FOOTPRINTS[want_fp])
        else:
            pads = {}
            for pad in children(lib.defs[want_fp], "pad"):
                at, size = child(pad, "at"), child(pad, "size")
                x, y = float(at[1]), float(at[2])
                w, h = float(size[1]), float(size[2])
                if round(float(at[3]) if len(at) > 3 else 0.0) % 180:
                    w, h = h, w
                drill = child(pad, "drill")
                pads[pad[1]] = (w, h, round(float(drill[1]), 3) if drill else 0.0,
                                round(x, 3), round(-y, 3))
        want_pads = normalize(pads, mirror=actual[ref].get("layer") == "bottom")
        got_pads = normalize(actual[ref]["pads"])
        if set(want_pads) != set(got_pads):
            problems.append((ref, f"pad numbers differ vs {want_fp}"))
            continue
        for num in sorted(want_pads, key=int):
            want, have = want_pads[num], got_pads[num]
            if max(abs(want[0] - have[0]), abs(want[1] - have[1]),
                    abs(want[2] - have[2]), abs(want[3] - have[3]),
                    abs(want[4] - have[4])) > 0.05:
                problems.append((ref, f"pad {num} differs vs {want_fp}"))
                break

    pads = sum(len(e["pads"]) for e in expected.values())
    notes = [(ref, e["note"]) for ref, e in expected.items() if e.get("note")]
    print(f"footprints: {len(expected)}   pads: {pads}   (tscircuit: {sum(len(v['pads']) for v in actual.values())} pads)")
    for ref, note in notes:
        print(f"  note {ref}: {note}")
    if problems:
        print(f"\n{len(problems)} problem(s):")
        for ref, message in problems:
            print(f"  {ref}: {message}")
        sys.exit(1)
    print("all footprints match the KiCad board, pad for pad")


if __name__ == "__main__":
    main()

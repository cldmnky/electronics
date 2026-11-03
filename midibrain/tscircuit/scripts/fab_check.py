#!/usr/bin/env python3
"""Check prototype circuit JSON against JLCPCB 2-layer, 1 oz FR4 limits.

Source: https://jlcpcb.com/pcb-fabrication/fr4-pcb (retrieved for this review).
This is a geometry sanity check, not a substitute for JLCPCB's CAM/DFM review.
"""
from __future__ import annotations
import json
import sys
from pathlib import Path

# JLCPCB 1 oz, 2-layer limits used by this check.
MIN_TRACE = 0.10          # mm, track width and spacing
MIN_VIA_HOLE = 0.15       # mm
MIN_VIA_PAD = 0.25        # mm
MIN_ANNULAR = 0.05        # mm
MIN_PLATED_HOLE = 0.15    # mm, round plated
MIN_PLATED_SLOT = 0.50    # mm, smallest plated slot dimension
MIN_NONPLATED = 0.20      # mm
MIN_SMT_PAD = 0.25        # mm, smallest pad dimension
MIN_BOARD_EDGE = 0.20     # mm, copper to board edge/cutout
BOARD_THICKNESS = 1.6
LAYERS = 2

def load(path: Path):
    with path.open() as f:
        return json.load(f)

def check(name: str, path: Path) -> int:
    data = load(path)
    board = next(e for e in data if e.get("type") == "pcb_board")
    traces = [e for e in data if e.get("type") == "pcb_trace"]
    vias = [e for e in data if e.get("type") == "pcb_via"]
    plated = [e for e in data if e.get("type") == "pcb_plated_hole"]
    holes = [e for e in data if e.get("type") == "pcb_hole"]
    pads = [e for e in data if e.get("type") == "pcb_smtpad"]
    silk_paths = [e for e in data if e.get("type") == "pcb_silkscreen_path"]
    silk_texts = [e for e in data if e.get("type") == "pcb_silkscreen_text"]

    trace_widths = [s.get("width", 0) for t in traces for s in t.get("route", []) if s.get("route_type") == "wire"]
    via_rows = [(v.get("hole_diameter", 0), v.get("outer_diameter", 0)) for v in vias]
    plated_rows = []
    for p in plated:
        if "hole_diameter" in p and p.get("hole_diameter") is not None:
            h = p["hole_diameter"]
            o = p.get("outer_diameter") or p.get("rect_pad_width") or h
            plated_rows.append(("round", h, h, o, o))
        else:
            hw, hh = p.get("hole_width"), p.get("hole_height")
            ow, oh = p.get("outer_width"), p.get("outer_height")
            if None not in (hw, hh, ow, oh):
                plated_rows.append(("slot", hw, hh, ow, oh))
    nonplated = [h.get("hole_diameter", 0) for h in holes if h.get("hole_diameter")]
    smt = [(p.get("width", 0), p.get("height", 0)) for p in pads if p.get("width") and p.get("height")]
    silk_strokes = [p.get("stroke_width", 0) for p in silk_paths if p.get("stroke_width")]
    silk_fonts = [t.get("font_size", 0) for t in silk_texts if t.get("font_size")]

    min_trace = min(trace_widths) if trace_widths else 0
    min_via_hole = min((h for h, _ in via_rows), default=0)
    min_via_pad = min((o for _, o in via_rows), default=0)
    min_ring = min(((o - h) / 2 for h, o in via_rows), default=0)
    min_plated_round = min((min(hw, hh) for shape, hw, hh, _, _ in plated_rows if shape == "round"), default=0)
    min_plated_slot = min((min(hw, hh) for shape, hw, hh, _, _ in plated_rows if shape == "slot"), default=0)
    min_plated_ring = min((min((ow - hw) / 2, (oh - hh) / 2) for _, hw, hh, ow, oh in plated_rows), default=0)
    min_nonplated = min(nonplated) if nonplated else 0
    min_smt = min((min(w, h) for w, h in smt), default=0)

    failures = []
    def req(ok, text):
        print(("PASS " if ok else "FAIL ") + text)
        if not ok:
            failures.append(text)

    print(f"\n=== {name} ({path}) ===")
    print(f"board: {board.get('width')} x {board.get('height')} mm, {board.get('num_layers')} layers, {board.get('thickness')} mm, {board.get('material')}")
    req(board.get("num_layers") == LAYERS, f"layers = {board.get('num_layers')} (need {LAYERS})")
    req(abs(float(board.get("thickness", 0)) - BOARD_THICKNESS) < 1e-6, f"thickness = {board.get('thickness')} mm")
    req(float(board.get("width", 0)) >= 3 and float(board.get("height", 0)) >= 3, "board dimensions >= 3 x 3 mm")
    req(float(board.get("min_board_edge_clearance", 0)) >= MIN_BOARD_EDGE, f"board-edge rule = {board.get('min_board_edge_clearance')} mm (need {MIN_BOARD_EDGE})")
    req(min_trace >= MIN_TRACE, f"min trace width = {min_trace:.3f} mm (min {MIN_TRACE})")
    req(min_via_hole >= MIN_VIA_HOLE, f"min via hole = {min_via_hole:.3f} mm (min {MIN_VIA_HOLE})")
    req(min_via_pad >= MIN_VIA_PAD, f"min via pad = {min_via_pad:.3f} mm (min {MIN_VIA_PAD})")
    req(min_ring + 1e-9 >= MIN_ANNULAR, f"min via annular ring = {min_ring:.3f} mm (min {MIN_ANNULAR})")
    req(min_plated_round >= MIN_PLATED_HOLE, f"min round plated hole = {min_plated_round:.3f} mm (min {MIN_PLATED_HOLE})")
    if any(shape == "slot" for shape, *_ in plated_rows):
        req(min_plated_slot >= MIN_PLATED_SLOT, f"min plated slot = {min_plated_slot:.3f} mm (min {MIN_PLATED_SLOT})")
    req(min_plated_ring >= MIN_ANNULAR, f"min plated-hole annular ring = {min_plated_ring:.3f} mm (min {MIN_ANNULAR})")
    req(min_nonplated >= MIN_NONPLATED, f"min non-plated hole = {min_nonplated:.3f} mm (min {MIN_NONPLATED})")
    req(min_smt >= MIN_SMT_PAD, f"min SMT pad dimension = {min_smt:.3f} mm (min {MIN_SMT_PAD})")
    print(f"INFO traces={len(traces)} vias={len(vias)} plated_holes={len(plated)} smt_pads={len(pads)}")
    print(f"INFO min silk stroke={min(silk_strokes) if silk_strokes else 0:.3f} mm, min silk text={min(silk_fonts) if silk_fonts else 0:.3f} mm")
    print(f"INFO board rules: trace/pad={board.get('min_trace_to_pad_edge_clearance')} pad/pad={board.get('min_pad_edge_to_pad_edge_clearance')} hole/trace={board.get('min_trace_to_hole_edge_clearance')}")
    return 0 if not failures else 1

def main() -> int:
    root = Path(__file__).resolve().parents[1]
    errors = 0
    for name, rel in [("front/UI", "dist/front/circuit.json"), ("rear/base", "dist/index/circuit.json")]:
        p = root / rel
        if not p.exists():
            print(f"FAIL missing {p}", file=sys.stderr)
            errors += 1
            continue
        errors += check(name, p)
    return 0 if errors == 0 else 1

if __name__ == "__main__":
    raise SystemExit(main())

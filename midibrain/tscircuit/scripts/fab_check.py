#!/usr/bin/env python3
"""Geometry check of the built circuit JSON against JLCPCB published limits.

Primary source (retrieved 2026-11-04):
  https://jlcpcb.com/capabilities/pcb-capabilities
  saved copy SHA-256 5c5e26084f12287b271ee8219a8e4b2ad1adb35bddd71d261c3cf61854f75dcb
Quoted values used below:
  * "Min. track width and spacing (1 oz) 0.10 / 0.10 mm ... 1- and 2-layer"
  * "Min. Via hole size/diameter 0.15/0.25mm"; "Preferred Min. Via hole size:
    0.2mm"; "0.2mm or 0.25mm hole size with via diameter less than 0.45mm,
    will cost more"
  * "PTH annular ring ... 2-layer: 1 oz: Recommended 0.25 mm or above;
    absolute minimum 0.18 mm"
  * "Min. Non-plated holes 0.50mm"
  * "Min. Plated Slots Width 2-layer: 0.5mm"
  * "Routed 0.2mm: Copper clearance from routed board edges: >=0.2 mm"
  * "Drill Diameter ... 2-layer: 0.15 - 6.3 mm"
  * "Pad to track clearance (1oz) 0.1mm"
  * "SMD pad to pad clearance (different nets) 0.15mm"
  * "Thickness for FR4 are: 0.4/0.6/0.8/1.0/1.2/1.6/2.0 mm"
  * "Minimum Dimensions FR4 ...: 3 x 3 mm"

Two severities:

* FAIL - below a value JLCPCB publishes, or missing data. The numbers cannot be
  fabricated as drawn, or the data does not prove anything.
* TARGET - inside the published floor but below this project's manufacturing
  target (via drill 0.2 mm, via pad 0.45 mm, PTH annular ring 0.25 mm). The
  target exists so a release does not sit on a process floor. TARGET items make
  ``--strict-targets`` (the release gate) fail; ``make test`` only reports them.

A via at 0.2/0.3 mm (0.05 mm ring) is legal at the published via floor. It is
not an annular-ring claim for PTH pads and it is not HDI, and it is below the
target above. PTH pads need the separate PTH annular rule.

This is a geometry sanity check, not a JLCPCB CAM/DFM approval, and not a
physical first-article acceptance.
"""
from __future__ import annotations

import json
import sys
from pathlib import Path

SOURCE_URL = "https://jlcpcb.com/capabilities/pcb-capabilities"
SOURCE_RETRIEVED = "2026-11-04"
SOURCE_SHA256 = ("5c5e26084f12287b271ee8219a8e4b2ad1adb35bddd71d261c3cf61854f75dcb")

# Published limits (2-layer, 1 oz FR4 unless noted).
MIN_TRACE = 0.10          # mm, track width and spacing
MIN_TRACE_SPACING = 0.10  # mm, same source line
MIN_VIA_DRILL = 0.15      # mm
MIN_VIA_PAD = 0.25        # mm
MIN_VIA_RING = (MIN_VIA_PAD - MIN_VIA_DRILL) / 2   # 0.05 mm, published floor
MIN_PTH_ANNULAR = 0.18    # mm, absolute minimum (2-layer, 1 oz)
REC_PTH_ANNULAR = 0.25    # mm, recommended
MIN_DRILL = 0.15          # mm, 2-layer drill range 0.15 - 6.3
MAX_DRILL = 6.3
MIN_NPTH = 0.50           # mm (0.2 mm is NOT a published limit)
MIN_PLATED_SLOT = 0.50    # mm, 2-layer
MIN_BOARD_EDGE = 0.20     # mm, copper clearance from routed edge
MIN_PAD_TO_TRACK = 0.10   # mm, 1 oz
MIN_SMD_PAD_GAP = 0.15    # mm, different nets
MIN_BOARD_MM = 3.0
FR4_THICKNESSES = (0.4, 0.6, 0.8, 1.0, 1.2, 1.6, 2.0)
LAYERS = 2

# Project manufacturing targets (above the published floor, on purpose).
TARGET_VIA_DRILL = 0.20
TARGET_VIA_PAD = 0.45     # below 0.45 mm JLCPCB charges the small-via option
TARGET_PTH_ANNULAR = REC_PTH_ANNULAR
PROJECT_MIN_SMT_PAD = 0.25   # project floor; not a published JLCPCB number


class Result:
    def __init__(self, name: str, path: Path):
        self.name = name
        self.path = path
        self.failures: list[str] = []
        self.warnings: list[str] = []
        self.targets: list[str] = []
        self.infos: list[str] = []

    def fail(self, text: str) -> None:
        self.failures.append(text)

    def warn(self, text: str) -> None:
        self.warnings.append(text)

    def target(self, text: str) -> None:
        """Inside the published floor, below the project target or unverified."""
        self.targets.append(text)

    def info(self, text: str) -> None:
        self.infos.append(text)

    def req(self, ok: bool, text: str) -> None:
        """Hard requirement: a published limit or required data."""
        if not ok:
            self.fail(text)

    @property
    def ok(self) -> bool:
        return not self.failures


def load(path: Path) -> list:
    with path.open() as f:
        data = json.load(f)
    if not isinstance(data, list) or not data:
        raise ValueError(f"{path}: expected a non-empty circuit JSON array")
    return data


def _num(value) -> float | None:
    return float(value) if isinstance(value, (int, float)) and not isinstance(value, bool) else None


def collect(data: list) -> dict:
    """Pull the geometry the checks need, keeping 'missing' distinct from 0."""
    board = next((e for e in data if e.get("type") == "pcb_board"), None)
    traces = [e for e in data if e.get("type") == "pcb_trace"]
    vias = [e for e in data if e.get("type") == "pcb_via"]
    plated = [e for e in data if e.get("type") == "pcb_plated_hole"]
    holes = [e for e in data if e.get("type") == "pcb_hole"]
    pads = [e for e in data if e.get("type") == "pcb_smtpad"]
    silk_paths = [e for e in data if e.get("type") == "pcb_silkscreen_path"]
    silk_texts = [e for e in data if e.get("type") == "pcb_silkscreen_text"]
    return {"board": board, "traces": traces, "vias": vias,
            "plated": plated, "holes": holes, "pads": pads,
            "silk_paths": silk_paths, "silk_texts": silk_texts}


def check_board(name: str, data: list, strict_targets: bool = False) -> Result:
    r = Result(name, Path(name))
    g = collect(data)
    board = g["board"]
    if board is None:
        r.fail("no pcb_board element: cannot check fabrication limits")
        return r

    # --- required data ---------------------------------------------------
    width = _num(board.get("width"))
    height = _num(board.get("height"))
    thickness = _num(board.get("thickness"))
    layers = board.get("num_layers")
    if width is None or height is None:
        r.fail("board width/height missing from the circuit JSON")
    if thickness is None:
        r.fail("board thickness missing from the circuit JSON")
    if layers is None:
        r.fail("board layer count missing from the circuit JSON")

    if not g["traces"]:
        r.fail("no pcb_trace elements: no routed copper to check")
    if not g["vias"]:
        r.fail("no pcb_via elements: via limits cannot be checked (missing data)")
    if not g["pads"]:
        r.fail("no pcb_smtpad elements: pad limits cannot be checked (missing data)")
    if not g["plated"]:
        r.fail("no pcb_plated_hole elements: PTH limits cannot be checked")

    trace_widths: list[float] = []
    for t in g["traces"]:
        for seg in t.get("route", []):
            if seg.get("route_type") != "wire":
                continue
            w = _num(seg.get("width"))
            if w is None:
                r.fail(f"pcb_trace {t.get('pcb_trace_id', '?')} route segment has no width")
                continue
            trace_widths.append(w)

    via_rows: list[tuple[float, float]] = []
    for v in g["vias"]:
        hole = _num(v.get("hole_diameter"))
        outer = _num(v.get("outer_diameter"))
        if hole is None or outer is None:
            r.fail(f"pcb_via {v.get('pcb_via_id', '?')} missing hole/outer diameter")
            continue
        if outer <= hole:
            r.fail(f"pcb_via {v.get('pcb_via_id', '?')}: outer diameter {outer} <= hole {hole}")
            continue
        via_rows.append((hole, outer))

    # (hole_w, hole_h, pad_w, pad_h) in mm; a round hole has hole_w == hole_h.
    plated_round: list[tuple[float, float, float, float]] = []
    plated_slot: list[tuple[float, float, float, float]] = []
    for p in g["plated"]:
        pid = p.get("pcb_plated_hole_id", "?")
        shape = p.get("shape")
        if shape == "circle":
            h = _num(p.get("hole_diameter"))
            o = _num(p.get("outer_diameter"))
            if h is None or o is None:
                r.fail(f"plated hole {pid} ({shape}) missing hole/outer diameter")
                continue
            plated_round.append((h, h, o, o))
        elif shape == "circular_hole_with_rect_pad":
            h = _num(p.get("hole_diameter"))
            pw, ph = _num(p.get("rect_pad_width")), _num(p.get("rect_pad_height"))
            if None in (h, pw, ph):
                r.fail(f"plated hole {pid} ({shape}) missing hole/rect pad size")
                continue
            plated_round.append((h, h, pw, ph))
        elif shape in ("oval", "pill"):
            hw, hh = _num(p.get("hole_width")), _num(p.get("hole_height"))
            ow, oh = _num(p.get("outer_width")), _num(p.get("outer_height"))
            if None in (hw, hh, ow, oh):
                r.fail(f"plated slot {pid} ({shape}) missing hole/pad size")
                continue
            plated_slot.append((hw, hh, ow, oh))
        else:
            r.fail(f"plated hole {pid}: unsupported shape {shape!r}, "
                   f"limits cannot be checked")

    npth: list[float] = []
    for h in g["holes"]:
        d = _num(h.get("hole_diameter"))
        if d is None:
            r.fail(f"pcb_hole {h.get('pcb_hole_id', '?')} has no diameter")
            continue
        npth.append(d)

    smt_dims: list[float] = []
    for p in g["pads"]:
        w, h = _num(p.get("width")), _num(p.get("height"))
        if w is None or h is None:
            r.fail(f"pcb_smtpad {p.get('pcb_smtpad_id', '?')} missing width/height")
            continue
        smt_dims.append(min(w, h))

    silk_strokes = [w for w in (_num(p.get("stroke_width")) for p in g["silk_paths"]) if w]
    silk_fonts = [f for f in (_num(t.get("font_size")) for t in g["silk_texts"]) if f]

    # --- board ----------------------------------------------------------
    r.info(f"board {width} x {height} mm, {layers} layers, {thickness} mm, "
           f"{board.get('material')}")
    r.req(layers == LAYERS, f"layers = {layers} (this check is for {LAYERS}-layer)")
    r.req(thickness is not None and thickness in FR4_THICKNESSES,
          f"thickness = {thickness} mm (published FR4 set {FR4_THICKNESSES})")
    r.req(width is not None and height is not None
          and width >= MIN_BOARD_MM and height >= MIN_BOARD_MM,
          f"board {width} x {height} mm >= {MIN_BOARD_MM} x {MIN_BOARD_MM} mm")

    edge_rule = _num(board.get("min_board_edge_clearance"))
    r.req(edge_rule is not None and edge_rule >= MIN_BOARD_EDGE,
          f"board-edge copper rule = {edge_rule} mm (published >= {MIN_BOARD_EDGE})")

    # --- traces ---------------------------------------------------------
    if trace_widths:
        min_trace = min(trace_widths)
        r.req(min_trace >= MIN_TRACE,
              f"min trace width = {min_trace:.3f} mm (published {MIN_TRACE}); "
              f"widths used {sorted(set(round(w, 3) for w in trace_widths))}")
    else:
        r.fail("no routed wire segments with a width")

    # --- vias -----------------------------------------------------------
    if via_rows:
        min_drill = min(h for h, _ in via_rows)
        min_pad = min(o for _, o in via_rows)
        min_ring = min((o - h) / 2 for h, o in via_rows)
        r.req(min_drill >= MIN_VIA_DRILL,
              f"min via drill = {min_drill:.3f} mm (published {MIN_VIA_DRILL})")
        r.req(min_pad >= MIN_VIA_PAD,
              f"min via pad = {min_pad:.3f} mm (published {MIN_VIA_PAD})")
        r.req(min_ring + 1e-9 >= MIN_VIA_RING,
              f"min via ring = {min_ring:.3f} mm (published floor {MIN_VIA_RING:.3f}; "
              f"this is the via floor, not a PTH annular value and not HDI)")
        if min_ring < 0.10:
            r.info(f"via ring {min_ring:.3f} mm is at the published floor and below "
                   f"the project target {TARGET_VIA_DRILL}/{TARGET_VIA_PAD} mm "
                   f"drill/pad (ring 0.125 mm)")
        if min_drill < TARGET_VIA_DRILL - 1e-9 or min_pad < TARGET_VIA_PAD - 1e-9:
            r.target(f"via target: min {min_drill:.3f}/{min_pad:.3f} mm drill/pad is "
                     f"below the project target {TARGET_VIA_DRILL}/{TARGET_VIA_PAD} mm "
                     f"(published floor is 0.15/0.25 mm; via pads below 0.45 mm take "
                     f"JLCPCB's small-via option)")
    r.info(f"vias: {len(via_rows)} with "
           f"{sorted({(round(h, 3), round(o, 3)) for h, o in via_rows})}")

    # --- plated holes (PTH) ---------------------------------------------
    if plated_round:
        drills = [max(hw, hh) for hw, hh, _, _ in plated_round]
        rings = [(min(ow - hw, oh - hh) / 2) for hw, hh, ow, oh in plated_round]
        r.req(min(drills) >= MIN_DRILL,
              f"min PTH drill = {min(drills):.3f} mm (published 2-layer range "
              f"{MIN_DRILL}-{MAX_DRILL})")
        r.req(max(drills) <= MAX_DRILL,
              f"max PTH drill = {max(drills):.3f} mm (published max {MAX_DRILL})")
        min_ring = min(rings)
        r.req(min_ring + 1e-9 >= MIN_PTH_ANNULAR,
              f"min PTH annular ring = {min_ring:.3f} mm (published absolute "
              f"minimum {MIN_PTH_ANNULAR}, recommended {REC_PTH_ANNULAR})")
        if MIN_PTH_ANNULAR - 1e-9 <= min_ring < REC_PTH_ANNULAR - 1e-9:
            r.warn(f"PTH annular ring {min_ring:.3f} mm is above the absolute "
                   f"minimum but below the recommended {REC_PTH_ANNULAR} mm")
        if min_ring < TARGET_PTH_ANNULAR - 1e-9:
            r.target(f"PTH ring target: {min_ring:.3f} mm below the project target "
                     f"{TARGET_PTH_ANNULAR} mm")
        r.info(f"PTH round holes: {len(plated_round)}, drills "
               f"{min(drills):.3f}-{max(drills):.3f} mm, min ring {min_ring:.3f} mm")
    if plated_slot:
        min_slot = min(min(hw, hh) for hw, hh, _, _ in plated_slot)
        r.req(min_slot >= MIN_PLATED_SLOT,
              f"min plated slot = {min_slot:.3f} mm (published 2-layer "
              f"{MIN_PLATED_SLOT})")

    # --- non-plated holes ----------------------------------------------
    if npth:
        min_npth = min(npth)
        r.req(min_npth >= MIN_NPTH,
              f"min NPTH = {min_npth:.3f} mm (published {MIN_NPTH:.3f}; the old "
              f"0.20 mm constant was not a published limit)")
        r.info(f"NPTH: {len(npth)} holes, {min_npth:.3f}-{max(npth):.3f} mm")

    # --- pads -----------------------------------------------------------
    if smt_dims:
        min_smt = min(smt_dims)
        r.req(min_smt >= PROJECT_MIN_SMT_PAD,
              f"min SMT pad dimension = {min_smt:.3f} mm (project floor "
              f"{PROJECT_MIN_SMT_PAD}, not a published JLCPCB limit)")

    # --- board rules vs published clearances ---------------------------
    def rule(key: str, published: float, label: str) -> None:
        val = _num(board.get(key))
        if val is None:
            r.fail(f"{label} rule missing from the circuit JSON")
            return
        if val + 1e-9 >= published:
            r.info(f"{label} rule {val} mm >= published {published} mm")
        else:
            r.target(f"{label} rule {val} mm is below published {published} mm; "
                     f"the rule is a design setting, not proof of actual copper "
                     f"clearance - verify in the JLCPCB DFM preview "
                     f"(acceptance item scope_dfm_cpl)")

    rule("min_trace_to_pad_edge_clearance", MIN_PAD_TO_TRACK, "track-to-pad")
    rule("min_pad_edge_to_pad_edge_clearance", MIN_SMD_PAD_GAP,
         "SMD pad-to-pad (different nets)")
    hole_rule = _num(board.get("min_trace_to_hole_edge_clearance"))
    r.info(f"track-to-hole rule {hole_rule} mm (published inner-layer value 0.2 mm; "
           f"this board has no inner layers)")

    # --- silkscreen (informational; the release gate parses the Gerbers) --
    r.info(f"min silk stroke {min(silk_strokes) if silk_strokes else 0:.3f} mm, "
           f"min silk text {min(silk_fonts) if silk_fonts else 0:.3f} mm "
           f"(library text; not production documentation)")

    if strict_targets:
        for text in r.targets:
            r.fail(text)
    return r


def main(argv: list[str]) -> int:
    strict = "--strict-targets" in argv[1:]
    root = Path(__file__).resolve().parents[1]
    boards = [("front/UI", root / "dist/front/circuit.json"),
              ("rear/base", root / "dist/index/circuit.json")]
    errors = 0
    print(f"JLCPCB limits: {SOURCE_URL} (retrieved {SOURCE_RETRIEVED}, "
          f"sha256 {SOURCE_SHA256[:16]}...)")
    print(f"mode: {'release gate (targets enforced)' if strict else 'report only'}")
    for name, path in boards:
        print(f"\n=== {name} ({path}) ===")
        if not path.exists():
            print(f"FAIL missing {path} - run the build first", file=sys.stderr)
            errors += 1
            continue
        try:
            result = check_board(name, load(path), strict_targets=strict)
        except (ValueError, KeyError) as exc:
            print(f"FAIL {exc}", file=sys.stderr)
            errors += 1
            continue
        for text in result.infos:
            print(f"INFO {text}")
        for text in result.warnings:
            print(f"WARN {text}")
        for text in result.targets:
            print(f"TARGET {text}")
        for text in result.failures:
            print(f"FAIL {text}")
        if result.ok:
            print(f"PASS {name}: geometry inside the checked limits")
        else:
            errors += 1
    print("\nNot a JLCPCB CAM/DFM approval and not a physical first article.")
    return 0 if errors == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))

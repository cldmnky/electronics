#!/usr/bin/env python3
"""Euro front board - silkscreen geometry rules.

What the board prints is defined in sections/silk.tsx; this checks the built
result, because a label is useless - or a short - if it lands on a pad, a hole
or the board edge.

Run from the project root after `tsci build`:

    python3 scripts/check_silk.py [--json PATH] [--verbose]

Rules
  1. Every free silk stroke is at least 0.15mm wide. `<silkscreentext>` prints
     at 0.09 x font size, so this rejects small text; the labels are drawn as
     polylines with an explicit width instead (lib/silk.tsx).
  2. Free silk clears every pad, hole and the board outline by 0.15mm.

Footprint-owned art (jack circles, 0603 dashes, the SOD-323 cathode bar) is not
re-checked here: it is part of the verified footprint.
"""

from __future__ import annotations

import json
import math
import sys
from dataclasses import dataclass
from pathlib import Path

MIN_STROKE = 0.15  # mm, JLCPCB minimum printed silk width
MIN_CLEARANCE = 0.15  # mm, silk to pad / hole
MIN_EDGE = 0.30  # mm, silk to routed outline
DEFAULT_JSON = Path("dist/index/circuit.json")


@dataclass(frozen=True)
class Box:
    x0: float
    y0: float
    x1: float
    y1: float

    def overlaps(self, other: "Box", gap: float = 0.0) -> bool:
        return not (
            self.x1 + gap <= other.x0
            or other.x1 + gap <= self.x0
            or self.y1 + gap <= other.y0
            or other.y1 + gap <= self.y0
        )

    def grow(self, d: float) -> "Box":
        return Box(self.x0 - d, self.y0 - d, self.x1 + d, self.y1 + d)

    @property
    def centre(self) -> tuple[float, float]:
        return ((self.x0 + self.x1) / 2, (self.y0 + self.y1) / 2)


def _rotated_box(cx: float, cy: float, w: float, h: float, degrees: float) -> Box:
    a = math.radians(degrees or 0)
    c, s = math.cos(a), math.sin(a)
    xs, ys = [], []
    for dx, dy in ((-w / 2, -h / 2), (w / 2, -h / 2), (w / 2, h / 2), (-w / 2, h / 2)):
        xs.append(cx + dx * c - dy * s)
        ys.append(cy + dx * s + dy * c)
    return Box(min(xs), min(ys), max(xs), max(ys))


def pads_and_holes(circuit: list[dict]) -> list[tuple[str, Box]]:
    out: list[tuple[str, Box]] = []
    for e in circuit:
        kind = e.get("type")
        if kind == "pcb_smtpad":
            box = _rotated_box(e["x"], e["y"], e["width"], e["height"], e.get("ccw_rotation", 0))
            out.append((f"smtpad {e.get('pcb_component_id', '?')}", box))
        elif kind == "pcb_plated_hole":
            w = e.get("outer_width")
            h = e.get("outer_height")
            if not w:
                w = h = e.get("outer_diameter") or e.get("hole_diameter", 0)
            out.append((f"hole {e.get('pcb_component_id', '?')}",
                        Box(e["x"] - w / 2, e["y"] - h / 2, e["x"] + w / 2, e["y"] + h / 2)))
        elif kind == "pcb_hole":
            d = e.get("hole_diameter", 0)
            out.append(("npth", Box(e["x"] - d / 2, e["y"] - d / 2, e["x"] + d / 2, e["y"] + d / 2)))
    return out


def free_silk(circuit: list[dict]) -> list[tuple[str, Box, float]]:
    """Silk not owned by a footprint, with the width it prints at."""
    out: list[tuple[str, Box, float]] = []
    for e in circuit:
        kind = e.get("type")
        if e.get("pcb_component_id"):
            continue
        if kind == "pcb_silkscreen_path":
            pts = e.get("route") or []
            if not pts:
                continue
            xs = [p["x"] for p in pts]
            ys = [p["y"] for p in pts]
            out.append((f"path({len(pts)})", Box(min(xs), min(ys), max(xs), max(ys)),
                        float(e.get("stroke_width") or 0)))
        elif kind == "pcb_silkscreen_circle":
            r = float(e.get("radius") or 0)
            out.append((f"circle(r{r:.1f})", Box(e["x"] - r, e["y"] - r, e["x"] + r, e["y"] + r),
                        float(e.get("stroke_width") or 0)))
        elif kind == "pcb_silkscreen_text":
            size = float(e.get("font_size") or 0)
            anchor = e.get("anchor_position") or {"x": 0, "y": 0}
            # text is centred; its length is not in the JSON, so bound it generously
            out.append((f"text:{e.get('text', '')[:28]}",
                        Box(anchor["x"] - size * 12, anchor["y"] - size, anchor["x"] + size * 12, anchor["y"] + size),
                        0.09 * size))
    return out


def main() -> int:
    path = DEFAULT_JSON
    if "--json" in sys.argv:
        path = Path(sys.argv[sys.argv.index("--json") + 1])
    verbose = "--verbose" in sys.argv
    circuit = json.loads(path.read_text())

    board = next(e for e in circuit if e.get("type") == "pcb_board")
    half_w, half_h = board["width"] / 2, board["height"] / 2
    outline = Box(-half_w, -half_h, half_w, half_h)

    obstacles = pads_and_holes(circuit)
    silk = free_silk(circuit)
    failures: list[str] = []

    for name, box, stroke in silk:
        where = f"{name} at {box.centre[0]:.1f},{box.centre[1]:.1f}"
        if stroke < MIN_STROKE - 1e-6:
            failures.append(f"{where} prints {stroke:.3f}mm wide (< {MIN_STROKE}mm)")
        grown = box.grow(stroke / 2)
        if (grown.x0 < outline.x0 + MIN_EDGE or grown.y0 < outline.y0 + MIN_EDGE
                or grown.x1 > outline.x1 - MIN_EDGE or grown.y1 > outline.y1 - MIN_EDGE):
            failures.append(f"{where} is inside the {MIN_EDGE}mm edge margin")
        for pad_name, pad in obstacles:
            if grown.overlaps(pad, MIN_CLEARANCE):
                failures.append(f"{where} hits {pad_name}")
                break

    if verbose:
        print(f"{len(silk)} free silk items, {len(obstacles)} pads/holes")
        for name, box, stroke in sorted(silk, key=lambda s: (-s[1].centre[1], s[1].centre[0])):
            cx, cy = box.centre
            print(f"  {cx:7.2f},{cy:7.2f}  {stroke:.2f}mm  {name}")

    if failures:
        for failure in failures:
            print("FAIL", failure)
        print(f"FAIL silk: {len(failures)} violation(s)")
        return 1
    print(f"PASS silk: {len(silk)} free items clear of {len(obstacles)} pads/holes, "
          f"all >= {MIN_STROKE}mm")
    return 0


if __name__ == "__main__":
    sys.exit(main())

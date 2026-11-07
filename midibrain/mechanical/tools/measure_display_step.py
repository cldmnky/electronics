#!/usr/bin/env python3
"""Read the Waveshare 4.3inch DSI LCD reference STEP without a CAD kernel.

CadQuery is not installed in the build image, so this reads the ISO-10303-21
text directly: every `MANIFOLD_SOLID_BREP` is followed through its reference
graph down to `CARTESIAN_POINT` entities and reduced to an axis-aligned
bounding box. That is enough to answer the two questions the panel geometry
needs from the model:

  * which solid is the GLASS, and what is its front-face outline (the panel
    aperture is sized from it), and
  * what the model's overall bounding box is (the 14.05 mm wiki thickness vs
    the STEP bbox).

Usage:  python3 mechanical/tools/measure_display_step.py [--top N]
Nothing is written; the numbers are printed for mechanical/layout.json and
tscircuit/lib/layout.ts to quote.
"""

from __future__ import annotations

import argparse
import re
from pathlib import Path

STEP = Path(__file__).resolve().parent.parent / "reference" / "waveshare-4.3inch-dsi-lcd.step"

ENTITY_RE = re.compile(r"^#(\d+)\s*=\s*([A-Z_0-9]+)\s*\((.*)\)\s*;\s*$", re.S)
REF_RE = re.compile(r"#(\d+)")
POINT_RE = re.compile(r"\(\s*(-?[\d.E+-]+)\s*,\s*(-?[\d.E+-]+)\s*,\s*(-?[\d.E+-]+)\s*\)")


def parse(path: Path) -> dict[int, tuple[str, str]]:
    data = path.read_text(errors="replace")
    body = data.split("DATA;", 1)[1].split("ENDSEC;", 1)[0]
    # Entities may wrap across lines; join until a ';' terminates the record.
    entities: dict[int, tuple[str, str]] = {}
    buffer = ""
    for raw_line in body.splitlines():
        buffer += raw_line.strip()
        if not buffer.endswith(";"):
            continue
        match = ENTITY_RE.match(buffer)
        if match:
            entities[int(match.group(1))] = (match.group(2), match.group(3))
        buffer = ""
    return entities


def points_under(entities: dict[int, tuple[str, str]], root: int) -> list[tuple[float, float, float]]:
    seen: set[int] = set()
    stack = [root]
    points: list[tuple[float, float, float]] = []
    while stack:
        current = stack.pop()
        if current in seen:
            continue
        seen.add(current)
        entity = entities.get(current)
        if entity is None:
            continue
        kind, args = entity
        if kind == "CARTESIAN_POINT":
            match = POINT_RE.search(args)
            if match:
                points.append(tuple(float(match.group(i)) for i in (1, 2, 3)))
            continue
        stack.extend(int(ref) for ref in REF_RE.findall(args))
    return points


def bbox(points: list[tuple[float, float, float]]) -> tuple[float, float, float, float, float, float]:
    xs = [p[0] for p in points]
    ys = [p[1] for p in points]
    zs = [p[2] for p in points]
    return min(xs), max(xs), min(ys), max(ys), min(zs), max(zs)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--top", type=int, default=12, help="largest solids to report")
    args = parser.parse_args()

    entities = parse(STEP)
    solid_ids = [i for i, (kind, _) in entities.items() if kind == "MANIFOLD_SOLID_BREP"]
    shells = [i for i, (kind, _) in entities.items() if kind == "CLOSED_SHELL"]

    solids = []
    for solid_id in solid_ids:
        points = points_under(entities, solid_id)
        if len(points) < 4:
            continue
        solids.append((solid_id, bbox(points), len(points)))

    all_points = [p for _, bb, _ in solids for p in [(bb[0], bb[2], bb[4]), (bb[1], bb[3], bb[5])]]
    overall = bbox(all_points)
    print(f"file      {STEP}")
    print(f"entities  {len(entities)}  solids {len(solids)}  closed shells {len(shells)}")
    print(
        "bbox      x {:.3f}..{:.3f} ({:.3f})  y {:.3f}..{:.3f} ({:.3f})  z {:.3f}..{:.3f} ({:.3f})".format(
            overall[0], overall[1], overall[1] - overall[0],
            overall[2], overall[3], overall[3] - overall[2],
            overall[4], overall[5], overall[5] - overall[4],
        )
    )

    def area(bb):
        return (bb[1] - bb[0]) * (bb[3] - bb[2])

    print(f"\nlargest solids by XY area (top {args.top}):")
    print(f"{'id':>6} {'w':>9} {'h':>9} {'d':>7} {'x0':>9} {'x1':>9} {'y0':>9} {'y1':>9} {'z0':>7} {'z1':>7}")
    for solid_id, bb, _ in sorted(solids, key=lambda s: area(s[1]), reverse=True)[: args.top]:
        print(
            f"{solid_id:>6} {bb[1]-bb[0]:>9.3f} {bb[3]-bb[2]:>9.3f} {bb[5]-bb[4]:>7.3f}"
            f" {bb[0]:>9.3f} {bb[1]:>9.3f} {bb[2]:>9.3f} {bb[3]:>9.3f} {bb[4]:>7.3f} {bb[5]:>7.3f}"
        )

    print(f"\ntopmost solids by z (front face, top {args.top}):")
    for solid_id, bb, _ in sorted(solids, key=lambda s: s[1][5], reverse=True)[: args.top]:
        print(
            f"{solid_id:>6} {bb[1]-bb[0]:>9.3f} {bb[3]-bb[2]:>9.3f} {bb[5]-bb[4]:>7.3f}"
            f" {bb[0]:>9.3f} {bb[1]:>9.3f} {bb[2]:>9.3f} {bb[3]:>9.3f} {bb[4]:>7.3f} {bb[5]:>7.3f}"
        )


if __name__ == "__main__":
    main()

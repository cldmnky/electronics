#!/usr/bin/env python3
"""Feedthrough / strain-relief bracket for the MIDI Brain front panel.

The part is built ONLY from ``mechanical/layout.json``, which is generated from
``tscircuit/lib/layout.ts`` by ``bun run scripts/export-mechanical.ts``. No
coordinate is repeated in this file: the first version of this script hardcoded
the slots at x = -48/-13/+22 and the mounts at x = -61.5/40 while the boards
drilled -38/-5/+35 and -60/55, and its plate ended at x = +45 - the left mount
was outside the plate and the right mount was in mid-air. The geometry now
comes from the shared data, and this script refuses to export a part when the
solid does not match the design rules.

Status: PROVISIONAL planning model, not a released part. Two inputs are still
unmeasured: the M2.5 screw-head/washer envelope and the printed hole fit. See
docs/MECHANICAL-VERIFICATION.md.

Z: the bracket sits on the REAR side of the front board; Z = 0 is the board
rear face and Z = thickness is the rear of the part. Print it flat on Z = 0.

CadQuery, installed locally (do NOT install into the system Python):

    uv venv /tmp/cq-venv
    uv pip install --python /tmp/cq-venv/bin/python cadquery
    /tmp/cq-venv/bin/python mechanical/feedthrough_bracket.py

If CadQuery's loader complains about ``libGL.so.1`` on a headless machine, the
geometry path never calls GL; a local stub in /tmp satisfies the loader:

    gcc -shared -fPIC -O0 /tmp/glstub.c -o /tmp/fakelib/libGL.so.1
    LD_LIBRARY_PATH=/tmp/fakelib /tmp/cq-venv/bin/python mechanical/feedthrough_bracket.py
"""
from __future__ import annotations

import sys
from pathlib import Path

from bracket_geometry import BracketGeometry, bracket_geometry, describe, load_layout, validate

OUT_DIR = Path(__file__).resolve().parent
STL = OUT_DIR / "feedthrough-bracket.stl"
STEP = OUT_DIR / "feedthrough-bracket.step"

# Feature checks are volumes in mm^3; a boolean that removes nothing is a defect.
VOLUME_EPS = 1e-6


def fail(message: str) -> None:
    print(f"FAIL {message}", file=sys.stderr)
    raise SystemExit(1)


def main() -> int:
    try:
        import cadquery as cq
    except ImportError as exc:  # pragma: no cover - environment dependent
        print(f"CadQuery is not importable ({exc}).\n"
              "Install it locally, do not touch the system Python:\n"
              "  uv venv /tmp/cq-venv\n"
              "  uv pip install --python /tmp/cq-venv/bin/python cadquery",
              file=sys.stderr)
        return 1

    layout = load_layout()
    geo: BracketGeometry = bracket_geometry(layout)
    notices = validate(geo, layout)

    print("== derived from mechanical/layout.json ==")
    for line in describe(geo):
        print(f"  {line}")

    o = geo.outline
    t = geo.thickness

    def box(x1: float, y1: float, x2: float, y2: float, z0: float = -1.0,
            z1: float | None = None) -> "cq.Workplane":
        z1 = t + 1.0 if z1 is None else z1
        return (cq.Workplane("XY").workplane(offset=z0)
                .center((x1 + x2) / 2, (y1 + y2) / 2)
                .rect(x2 - x1, y2 - y1).extrude(z1 - z0))

    def cylinder(x: float, y: float, radius: float, z0: float = -1.0,
                 z1: float | None = None) -> "cq.Workplane":
        z1 = t + 1.0 if z1 is None else z1
        return (cq.Workplane("XY").workplane(offset=z0).center(x, y)
                .circle(radius).extrude(z1 - z0))

    plate = box(o.x1, o.y1, o.x2, o.y2, 0.0, t)

    # Cut features. Slots run past the top edge so they are open, exactly as the
    # cable is laid in during assembly.
    features: list[tuple[str, "cq.Workplane"]] = []
    for slot, slot_id in zip(geo.slots, geo.slot_ids):
        features.append((f"slot {slot_id}", box(slot.x1, slot.y1, slot.x2, o.y2 + 1.0)))
    for i, mount in enumerate(geo.mounts):
        features.append((f"mount hole {i}", cylinder(mount.x, mount.y, mount.radius)))
    for i, head in enumerate(geo.head_clearance):
        features.append((f"head clearance {i}", cylinder(head.x, head.y, head.radius)))
    for i, tie in enumerate(geo.tie_holes):
        features.append((f"tie hole {i}", cylinder(tie.x, tie.y, tie.radius)))

    # 1. Every intended cut must actually remove material from the uncut plate.
    for name, tool in features:
        removed = plate.val().intersect(tool.val()).Volume()
        if removed <= VOLUME_EPS:
            fail(f"cut '{name}' removes nothing from the plate ({removed:.6f} mm^3); "
                 "the feature is outside the stock")

    solid = plate
    for _name, tool in features:
        solid = solid.cut(tool)

    body = solid.val()

    # 2. Nothing of the cut region may remain after the boolean.
    for name, tool in features:
        left = body.intersect(tool.val()).Volume()
        if left > VOLUME_EPS:
            fail(f"material left in '{name}' after the cut ({left:.6f} mm^3)")

    # 3. One piece: a bar cut in half cannot carry a strain relief.
    n_solids = solid.solids().size()
    if n_solids != 1:
        fail(f"the bracket is {n_solids} separate solids, expected 1")

    # 4. Material around each mount, sampled where an M2.5 nut sits.
    for i, mount in enumerate(geo.mounts):
        ring_radius = mount.radius + 1.15
        missing = [(x, y) for x, y in mount.ring(ring_radius, samples=12)
                   if not body.isInside(cq.Vector(x, y, t / 2))]
        if missing:
            fail(f"mount hole {i} has no material at {missing[0][0]:.2f}, {missing[0][1]:.2f}")

    # 5. Slot access in HEIGHT as well as width: open at the top edge, open over
    #    the whole declared panel opening, closed at the floor. A slot that
    #    starts inside the declared opening is the reviewed defect.
    for slot, slot_id, feedthrough in zip(geo.slots, geo.slot_ids, layout["feedthroughs"]):
        for y in (o.y2 - 0.5, o.y2 - 2.0, o.y2 - 5.0, slot.y1 + 0.2):
            if y <= slot.y1:
                continue
            if body.isInside(cq.Vector(slot.centre[0], y, t / 2)):
                fail(f"slot {slot_id} is blocked at y {y:.2f} (cable cannot be laid in)")
        opening_bottom = feedthrough["y"] - feedthrough["cutoutHeight"] / 2
        for y in (opening_bottom, opening_bottom + 0.5, feedthrough["y"]):
            if body.isInside(cq.Vector(slot.centre[0], y, t / 2)):
                fail(f"slot {slot_id} excludes the declared opening at y {y:.2f}")
        if not body.isInside(cq.Vector(slot.centre[0], slot.y1 - 0.5, t / 2)):
            fail(f"slot {slot_id} has no floor at y {slot.y1:.2f}")

    # 6. Head clearance: the spacer hardware must be able to pass the bar.
    for i, head in enumerate(geo.head_clearance):
        if body.isInside(cq.Vector(head.x, head.y, t / 2)):
            fail(f"head clearance {i} is blocked at its centre")

    volume = body.Volume()
    bb = body.BoundingBox()
    print("== solid self-check ==")
    print(f"  features: {len(features)} cut(s), 1 solid, volume {volume:.1f} mm^3")
    print(f"  bbox: x {bb.xmin:.2f}..{bb.xmax:.2f}, y {bb.ymin:.2f}..{bb.ymax:.2f}, "
          f"z {bb.zmin:.2f}..{bb.zmax:.2f}")
    print(f"  plate volume before cuts: {plate.val().Volume():.1f} mm^3, "
          f"removed {plate.val().Volume() - volume:.1f} mm^3")
    for note in notices:
        print(f"  NOTICE {note}")

    cq.exporters.export(solid, str(STL))
    cq.exporters.export(solid, str(STEP))
    print(f"== exported (not committed, reproducible) ==\n  {STL}\n  {STEP}")
    print("PROVISIONAL part: not a released drawing. Head envelope and printed hole fit pending.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

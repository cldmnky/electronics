#!/usr/bin/env python3
"""NAV keytop for the ALPS Alpine SKRHABE010 5-way switch (U5).

The part is built ONLY from ``printed_geometry.py``, which derives every
dimension from ``mechanical/layout.json``, ``tscircuit/lib/layout.ts`` and the
two datasheets. This script builds the solid, refuses to export when the solid
does not match the design rules, and re-reads what it wrote.

Shape (Z datum: panel FRONT face = 0, +Z rearward):

    head   D7.00 x 4.00 in front of the panel   -- the finger pad, 4 mm proud
    neck   D7.00 x 1.60 through the aperture    -- carries the side load
    flange D9.60 x 1.20 on the panel rear face  -- traps the cap, bears the load
    shank  D5.60 x 3.55 in the 8.4 mm air gap
    socket D3.20 x 1.50 over the D3.0 stem      -- floor 0.15 mm above the stem top

The head and the neck are ONE diameter on purpose: the head is the leading
feature when the cap is dropped through the aperture from the rear, so anything
in front of the flange that is wider than the aperture could never be
assembled through a one-piece panel. Assembly order is in README.md.

CadQuery, installed in a local environment (do NOT install into the system
Python):

    uv venv /tmp/cq-venv
    uv pip install --python /tmp/cq-venv/bin/python cadquery
    /tmp/cq-venv/bin/python mechanical/printed/nav_keytop.py

Status: PROVISIONAL printed part, not a released drawing. The print-fit and
assembly inputs are the ASSUMPTIONS in ``printed_geometry.py``; the first
article has to measure them (README.md).
"""
from __future__ import annotations

import sys
from pathlib import Path

from printed_geometry import (
    GeometryError,
    NavKeytop,
    apply_cuts,
    check_solid,
    describe_nav_keytop,
    export_stl_and_step,
    layout_export_notices,
    layout_source,
    load_layout,
    nav_keytop,
    probe_material,
    validate_nav_keytop,
    verify_step,
    verify_stl,
)

OUT_DIR = Path(__file__).resolve().parent
STL = OUT_DIR / "nav-keytop.stl"
STEP = OUT_DIR / "nav-keytop.step"


def cylinder(cq, radius: float, z0: float, z1: float, x: float = 0.0, y: float = 0.0):
    return (cq.Workplane("XY").workplane(offset=z0).center(x, y)
            .circle(radius).extrude(z1 - z0))


def frustum(cq, r0: float, r1: float, z0: float, height: float, x: float = 0.0, y: float = 0.0):
    """A truncated cone from (r0 at z0) to (r1 at z0 + height)."""
    from cadquery import Solid, Vector

    return cq.Workplane("XY").add(
        Solid.makeCone(r0, r1, height, pnt=Vector(x, y, z0), dir=Vector(0, 0, 1)))


def build_solid(cq, g: NavKeytop):
    """Build the keytop stock and its named cutters (the cuts are applied later)."""
    cx, cy = g.centre
    r_head = g.neck_diameter / 2          # head and neck are one diameter
    r_flange = g.flange_diameter / 2
    r_shank = g.shank_diameter / 2
    r_bore = g.socket_bore / 2

    stock = cylinder(cq, r_head, g.head_front_z, g.flange_front_z, cx, cy)
    stock = stock.union(cylinder(cq, r_flange, g.flange_front_z, g.flange_rear_z, cx, cy))
    stock = stock.union(cylinder(cq, r_shank, g.flange_rear_z, g.socket_mouth_z, cx, cy))

    features: list[tuple[str, object]] = []

    # 1. Blind socket over the stem, open toward the board.
    features.append(("socket bore", cylinder(cq, r_bore, g.socket_floor_z,
                                             g.socket_mouth_z + 1.0, cx, cy)))
    # 2. Socket mouth lead-in chamfer.
    c = g.mouth_chamfer
    features.append(("socket mouth chamfer",
                     frustum(cq, r_bore, r_bore + c, g.socket_mouth_z - c, c, cx, cy)))

    # 3. Head front edge break: the 45 deg chamfer plane meets the axis at
    #    head_front + c - r_head, so a cone from there removes the corner.
    c = g.head_edge_chamfer
    apex_z = g.head_front_z + c - r_head
    cone = frustum(cq, 0.0, r_head, apex_z, r_head, cx, cy)
    outer = cylinder(cq, r_head + 1.0, g.head_front_z - 1.0, g.head_front_z + c, cx, cy)
    features.append(("head edge chamfer", outer.cut(cone)))

    # 4. Flange rear edge break: the flange rear face is the part's only
    #    overhang, so it is chamfered 45 deg down to the shank.
    c = g.flange_rear_chamfer
    keep = frustum(cq, r_flange, r_flange - c, g.flange_rear_z - c, c, cx, cy)
    outer = cylinder(cq, r_flange + 0.1, g.flange_rear_z - c, g.flange_rear_z, cx, cy)
    features.append(("flange rear chamfer", outer.cut(keep)))

    return stock, features


def probe_points(g: NavKeytop) -> list[tuple[str, bool, list[tuple[float, float, float]]]]:
    """Named material/void samples that prove each feature of the solid.

    Every sample is at least 0.1 mm away from a face.
    """
    cx, cy = g.centre
    r_bore = g.socket_bore / 2
    r_shank = g.shank_diameter / 2
    r_head = g.neck_diameter / 2
    r_flange = g.flange_diameter / 2
    return [
        ("head is solid", True, [(cx + r_head - 1.0, cy, g.head_front_z + 1.0)]),
        ("neck is solid in the aperture", True,
         [(cx + r_head - 1.0, cy, (g.stack.panel_front_z + g.stack.panel_rear_z) / 2)]),
        ("flange ring is solid outside the shank", True,
         [(cx + (r_shank + r_flange) / 2, cy, g.flange_front_z + 0.5)]),
        ("socket wall is solid", True,
         [(cx + (r_bore + r_shank) / 2, cy, g.socket_floor_z + 0.6)]),
        ("socket floor keeps material", True,
         [(cx, cy, g.socket_floor_z - 0.4)]),
        ("socket is open above its floor", False,
         [(cx, cy, g.socket_floor_z + 0.6), (cx + r_bore - 0.3, cy, g.socket_mouth_z - 0.9)]),
        ("socket mouth chamfer removes the bore lip", False,
         [(cx + r_bore + g.mouth_chamfer - 0.2, cy, g.socket_mouth_z - 0.1)]),
        ("head chamfer removes the front corner", False,
         [(cx + r_head - 0.1, cy, g.head_front_z + 0.1)]),
        ("flange chamfer removes the rear corner", False,
         [(cx + r_flange - 0.1, cy, g.flange_rear_z - 0.1)]),
        ("nothing hangs below the socket mouth", False,
         [(cx, cy, g.socket_mouth_z + 0.4)]),
    ]


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

    try:
        layout = load_layout()
        source = layout_source()
        g = nav_keytop(layout, source)
        notices = validate_nav_keytop(g, layout)

        print("== derived from mechanical/layout.json, lib/layout.ts and the catalogs ==")
        for line in describe_nav_keytop(g):
            print(f"  {line}")

        stock, features = build_solid(cq, g)
        print("== solid self-check ==")
        print(f"  stock volume {stock.val().Volume():.2f} mm^3, "
              f"{len(features)} cuts")
        body, removed = apply_cuts(cq, stock, features)
        report = check_solid(cq, body, g.envelope, removed)
        for line in report.lines():
            print(f"  {line}")

        for what, want_material, points in probe_points(g):
            probe_material(cq, body, points, want_material, what)
        print(f"  probes: {len(probe_points(g))} feature samples match")

        export_stl_and_step(cq, body, STL, STEP)
        print("== exported, then re-read ==")
        mesh = verify_stl(STL, g.envelope, volume=report.volume)
        for line in mesh.lines():
            print(f"  {line}")
        step_volume = verify_step(cq, STEP, report.volume)
        print(f"  {STEP.name}: re-imports as 1 solid, volume {step_volume:.1f} mm^3")

        for note in layout_export_notices(layout, source):
            print(f"  NOTICE {note}")
        for note in notices:
            print(f"  NOTICE {note}")
    except GeometryError as exc:
        print(f"FAIL {exc}", file=sys.stderr)
        return 1

    print(f"== wrote\n  {STL}\n  {STEP}")
    print("PROVISIONAL part: not a released drawing. Measured print fit and the "
          "supplied encoder hardware decide the first article.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

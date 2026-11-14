#!/usr/bin/env python3
"""NAV cap for the ALPS Alpine RKJXT1F42001 4-direction stick switch (U5).

The part is built ONLY from ``printed_geometry.py``, which derives every
dimension from ``mechanical/layout.json``, ``tscircuit/lib/layout.ts`` and the
ALPS drawing. This script builds the solid, refuses to export when the solid
does not match the design rules, and re-reads what it wrote.

Shape (Z datum: panel FRONT face = 0, +Z rearward):

    head  D7.00 x 4.00 in front of the shaft tip   -- the finger pad
    shank D5.40 x 4.00 behind the head             -- carries the blind bore
    bore  D2.90 x 4.00, floor 0.15 above the tip   -- over the Ø2.5 flatted shaft

WHAT CHANGED WITH THE PART (owner decision 2026-11-14): the SMD SKRHABE010 put
its stem 5.0 mm above the board, so the old cap had to reach it through the
panel: a D7.00 neck guided in the 7.2 mm aperture and a D9.60 flange trapped
behind the panel. The RKJXT1F42001's shaft stands 17.4 mm above the board, i.e.
6.80 mm proud of the panel front, so this cap is a PUSH-ON CAP that sits on the
projecting shaft entirely IN FRONT of the panel:

  * no part of it enters the panel opening, so there is no neck fit and no
    flange. The panel opening now exists for the switch's own Ø7.20 stationary
    collar that stands inside it (lib/layout.ts PANEL_CUTOUTS.nav);
  * the bore is round over a shaft with one flat (1.80 across). A round bore
    cannot key on the flat, so the cap can rotate freely under a finger: there is
    no grip, no detent and no set screw, and nothing here shows that the cap
    stays on when the panel is lifted off. All three are unmeasured;
  * the old rounded-square-socket analysis belonged to the SKRHABE010 stem and
    is obsolete; it was removed with that part (docs/TODO.md).

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
    """Build the cap's stock and its named cutters (the cuts are applied later)."""
    cx, cy = g.centre
    r_head = g.head_diameter / 2
    r_shank = g.shank_diameter / 2
    r_bore = g.socket_bore / 2

    stock = cylinder(cq, r_head, g.head_front_z, g.head_rear_z, cx, cy)
    stock = stock.union(cylinder(cq, r_shank, g.head_rear_z, g.socket_mouth_z, cx, cy))

    features: list[tuple[str, object]] = []

    # 1. Blind socket over the flatted shaft, open toward the board (+Z).
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

    # 4. Shank rear edge break: the shank's rear face is the part's only
    #    overhang once the cap is on the shaft, so it is chamfered 45 deg. The
    #    kept core runs full diameter at mouth - c and r - c at the rear face.
    c = g.shank_rear_chamfer
    keep = frustum(cq, r_shank, r_shank - c, g.socket_mouth_z - c, c, cx, cy)
    outer = cylinder(cq, r_shank + 0.1, g.socket_mouth_z - c, g.socket_mouth_z, cx, cy)
    features.append(("shank rear chamfer", outer.cut(keep)))

    return stock, features


def probe_points(g: NavKeytop) -> list[tuple[str, bool, list[tuple[float, float, float]]]]:
    """Named material/void samples that prove each feature of the solid.

    Every sample is at least 0.1 mm away from a face.
    """
    cx, cy = g.centre
    r_bore = g.socket_bore / 2
    r_shank = g.shank_diameter / 2
    r_head = g.head_diameter / 2
    return [
        ("head is solid", True, [(cx + r_head - 1.0, cy, g.head_front_z + 1.0)]),
        ("head underside ring is solid outside the shank", True,
         [(cx + (r_shank + r_head) / 2, cy, g.head_rear_z - 0.5)]),
        ("shank wall is solid", True,
         [(cx + (r_bore + r_shank) / 2, cy, g.socket_floor_z + 0.6)]),
        ("socket floor keeps material", True,
         [(cx, cy, g.socket_floor_z - 0.4)]),
        ("socket is open above its floor", False,
         [(cx, cy, g.socket_floor_z + 0.6), (cx + r_bore - 0.3, cy, g.socket_mouth_z - 0.9)]),
        ("socket mouth chamfer removes the bore lip", False,
         [(cx + r_bore + g.mouth_chamfer - 0.2, cy, g.socket_mouth_z - 0.1)]),
        ("head chamfer removes the front corner", False,
         [(cx + r_head - 0.1, cy, g.head_front_z + 0.1)]),
        ("shank chamfer removes the rear corner", False,
         [(cx + r_shank - 0.1, cy, g.socket_mouth_z + 0.1)]),
        ("nothing hangs below the socket mouth", False,
         [(cx, cy, g.socket_mouth_z + 0.4)]),
        # The cap must stay entirely in FRONT of the panel front plane: if this
        # sample were solid, the part would sit in the panel opening.
        ("no material inside the panel opening", False,
         [(cx, cy, g.stack.panel_front_z + 0.3)]),
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

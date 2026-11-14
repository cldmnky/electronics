#!/usr/bin/env python3
"""Value knob for the Bourns PEC11R-4015F-S0024 encoder (U6).

The part is built ONLY from ``printed_geometry.py``, which derives every
dimension from ``mechanical/layout.json``, ``tscircuit/lib/layout.ts`` and the
Bourns datasheet. This script builds the solid, refuses to export when the solid
does not match the design rules, and re-reads what it wrote.

Shape (Z datum: panel FRONT face = 0, +Z rearward):

    body   D12.50 x 16.00      Z -18.70 .. -2.70 (2.70 mm in front of the panel)
    bore   D6.20 THROUGH (nothing covers the shaft tip), D-flat 1.60 mm from the
           axis (the sourced shaft flat is at 1.50: page 2 `4.5 +0/-0.05` from the
           shaft's top surface on a D6 +-0.1 shaft; `C 0.5` on page 3 is the TIP
           CHAMFER, not the flat depth)
    grip   16 x R1.60 flutes, 0.45 mm deep
    grub   radial D2.50 pilot (M3 x 0.5 TAP DRILL - not a key across-flats) on the
           flat's normal

Three findings are BLOCKERS (printed_geometry.validate_knob, and README.md
*Encoder blockers*): the supplied nut cannot be fitted on this encoder at the
10.6 mm panel (0.90 mm of thread against a 2.50 mm nut+washer), the D-bore's flat
cannot pass the ROUND part of the shaft (1.20 mm of shaft, 1.40 mm radial
interference), and the full 0.5 +-0.3 mm centre travel is not guaranteed. The
encoder stays in the BOM unchanged and no remedy is applied here.

CadQuery, installed in a local environment (do NOT install into the system
Python):

    uv venv /tmp/cq-venv
    uv pip install --python /tmp/cq-venv/bin/python cadquery
    /tmp/cq-venv/bin/python mechanical/printed/knob.py

Status: PROVISIONAL printed part, not a released drawing.
"""
from __future__ import annotations

import math
import sys
from pathlib import Path

from printed_geometry import (
    GeometryError,
    Knob,
    apply_cuts,
    check_solid,
    describe_knob,
    export_stl_and_step,
    knob,
    layout_export_notices,
    layout_source,
    load_layout,
    probe_material,
    validate_knob,
    verify_step,
    verify_stl,
)

OUT_DIR = Path(__file__).resolve().parent
STL = OUT_DIR / "knob.stl"
STEP = OUT_DIR / "knob.step"


def cylinder(cq, radius: float, z0: float, z1: float, x: float = 0.0, y: float = 0.0):
    return (cq.Workplane("XY").workplane(offset=z0).center(x, y)
            .circle(radius).extrude(z1 - z0))


def frustum(cq, r0: float, r1: float, z0: float, height: float, x: float = 0.0, y: float = 0.0):
    """A truncated cone from (r0 at z0) to (r1 at z0 + height)."""
    from cadquery import Solid, Vector

    return cq.Workplane("XY").add(
        Solid.makeCone(r0, r1, height, pnt=Vector(x, y, z0), dir=Vector(0, 0, 1)))


def radial_cylinder(cq, radius: float, start: tuple[float, float, float],
                    direction: tuple[float, float, float], length: float):
    """A cylinder along an arbitrary direction (the grub-screw pilot)."""
    from cadquery import Solid, Vector

    return cq.Workplane("XY").add(
        Solid.makeCylinder(radius, length, pnt=Vector(*start), dir=Vector(*direction)))


def rotated(cq, workplane, centre: tuple[float, float], angle_deg: float):
    """Rotate a cutter about the knob axis (the shaft flat couples them)."""
    if abs(angle_deg) < 1e-12:
        return workplane
    cx, cy = centre
    return workplane.rotate((cx, cy, 0), (cx, cy, 1), angle_deg)


def build_solid(cq, k: Knob):
    """Build the knob. Returns (stock, features) before the cuts are applied."""
    cx, cy = k.centre
    r_body = k.body_diameter / 2
    r_bore = k.bore_diameter / 2
    z0, z1 = k.front_z, k.rear_z

    stock = cylinder(cq, r_body, z0, z1, cx, cy)
    features: list[tuple[str, object]] = []

    # 1. The D bore: the shaft bore with its flat. One cutter, because the flat
    #    is the bore's shape, not a cut into already-removed material.
    bore = cylinder(cq, r_bore, z0 - 1.0, z1 + 1.0, cx, cy)
    flat = (cq.Workplane("XY").workplane(offset=z0 - 1.0)
            .center(cx + k.bore_flat_plane + (r_body + 2.0) / 2, cy)
            .rect(r_body + 2.0, 2 * (r_body + 2.0))
            .extrude(z1 - z0 + 2.0))
    features.append(("D bore", rotated(cq, bore.cut(flat), (cx, cy), k.flat_angle_deg)))

    # 2. Grip: shallow round flutes around the OD.
    for i in range(k.flute_count):
        angle = math.radians(k.flute_phase_deg + i * k.flute_pitch_deg)
        features.append((
            f"flute {i}",
            cylinder(cq, k.flute_radius, z0 - 1.0, z1 + 1.0,
                     cx + k.flute_cutter_radius * math.cos(angle),
                     cy + k.flute_cutter_radius * math.sin(angle))))

    # 3. Grub-screw pilot, radial, on the flat's normal, so the set screw bears
    #    on the shaft flat wherever the encoder is clocked.
    angle = math.radians(k.flat_angle_deg)
    direction = (math.cos(angle), math.sin(angle), 0.0)
    start = (cx + 1.5 * math.cos(angle), cy + 1.5 * math.sin(angle), k.grub_z)
    features.append(("grub pilot", radial_cylinder(
        cq, k.grub_pilot_diameter / 2, start, direction, r_body + 2.0)))

    # 4. Front edge break (the visible top of the printed part).
    c = k.edge_chamfer_front
    keep = frustum(cq, r_body - c, r_body, z0, c, cx, cy)
    features.append(("front edge chamfer",
                     cylinder(cq, r_body + 1.0, z0, z0 + c, cx, cy).cut(keep)))

    # 5. Rear edge break (the face that looks at the panel).
    c = k.edge_chamfer_rear
    keep = frustum(cq, r_body, r_body - c, z1 - c, c, cx, cy)
    features.append(("rear edge chamfer",
                     cylinder(cq, r_body + 1.0, z1 - c, z1, cx, cy).cut(keep)))

    # 6. Bore mouth lead-in, so the knob slides onto the shaft.
    c = k.bore_mouth_chamfer
    features.append(("bore mouth chamfer",
                     frustum(cq, r_bore, r_bore + c, z1 - c, c, cx, cy)))

    return stock, features


def probe_points(k: Knob) -> list[tuple[str, bool, list[tuple[float, float, float]]]]:
    """Named material/void samples that prove each feature of the solid.

    Every sample is at least 0.1 mm away from a face.
    """
    cx, cy = k.centre
    r_body = k.body_diameter / 2
    r_bore = k.bore_diameter / 2
    mid_z = (k.front_z + k.rear_z) / 2
    trough = math.radians(k.flute_phase_deg)
    r_trough = r_body - k.flute_depth
    return [
        ("body is solid outside the bore", True,
         [(cx - (r_bore + r_body) / 2, cy, mid_z)]),
        ("bore is open", False,
         [(cx, cy, mid_z), (cx - r_bore + 0.4, cy, mid_z)]),
        ("bore stays open up to the flat plane", False,
         [(cx + k.bore_flat_plane - 0.15, cy, mid_z)]),
        ("the D land behind the flat is material", True,
         [(cx + (k.bore_flat_plane + r_bore) / 2, cy, mid_z)]),
        ("flute trough is cut away", False,
         [(cx + (r_trough + 0.1) * math.cos(trough),
           cy + (r_trough + 0.1) * math.sin(trough), mid_z)]),
        ("a full-radius land stays on the +X axis", True,
         [(cx + r_body - 0.1, cy, mid_z - 2.0)]),
        ("grub pilot is open through the wall", False,
         [(cx + r_body - 0.5, cy, k.grub_z)]),
        ("material beside the grub pilot", True,
         [(cx + r_body - 0.5, cy, k.grub_z + 1.5)]),
        ("front edge chamfer removes the corner", False,
         [(cx + r_body - 0.1, cy, k.front_z + 0.1)]),
        ("rear edge chamfer removes the corner", False,
         [(cx + r_body - 0.1, cy, k.rear_z - 0.1)]),
        ("bore mouth chamfer removes the lip", False,
         [(cx - r_bore - 0.2, cy, k.rear_z - 0.1)]),
        ("material outside the bore behind the mouth chamfer", True,
         [(cx - r_bore - 0.2, cy, k.rear_z - 1.0)]),
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
        k = knob(layout, source)
        notices = validate_knob(k)

        print("== derived from mechanical/layout.json, lib/layout.ts and the datasheet ==")
        for line in describe_knob(k):
            print(f"  {line}")

        stock, features = build_solid(cq, k)
        print("== solid self-check ==")
        print(f"  stock volume {stock.val().Volume():.2f} mm^3, {len(features)} cuts")
        body, removed = apply_cuts(cq, stock, features)
        report = check_solid(cq, body, k.envelope, removed)
        for line in report.lines():
            print(f"  {line}")

        for what, want_material, points in probe_points(k):
            probe_material(cq, body, points, want_material, what)
        print(f"  probes: {len(probe_points(k))} feature samples match")

        export_stl_and_step(cq, body, STL, STEP)
        print("== exported, then re-read ==")
        mesh = verify_stl(STL, k.envelope, volume=report.volume)
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
    print("PROVISIONAL part: not a released drawing. The supplied nut/washer stack, "
          "the shaft clocking and the printed bore fit decide the first article.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
"""Printed B3F-4055 button cap, one geometry for SW1-SW7.

The part is built ONLY from ``printed_geometry.py``, which derives every
dimension from ``mechanical/layout.json``, ``tscircuit/lib/layout.ts`` and the
B3F-4055 datasheet. This script builds the solid, refuses to export when the
solid does not match the design rules, and re-reads what it wrote.

Shape (Z datum: panel FRONT face = 0, +Z rearward):

    plate   11.80 x 11.80 x 4.70 tall, Z -0.80..3.90
            - top at -0.80 (0.80 proud of the panel front)
            - 0.225 per side inside the 12.25 mm panel cutout
    recess  3.85 x 3.85 x 0.60 deep, opening REARWARD
            - floor at Z 3.30 = the B3F-4055 plunger face
            - rear face at Z 3.90, 0.025 per side on the 3.80 plunger

There is NO skirt. The 12.25 mm cutout is only 0.125 mm per side larger than
the 12.0 mm switch body, so a skirt could not pass the panel: the part is a
plate on the plunger, centred by the aperture. The same geometry is printed
seven times, one per switch; the seven openings are read from the export.

CadQuery, installed in a local environment (do NOT install into the system
Python):

    uv venv /tmp/cq-venv
    uv pip install --python /tmp/cq-venv/bin/python cadquery
    /tmp/cq-venv/bin/python mechanical/printed/button_cap.py

Status: PROVISIONAL printed part, not a released drawing. The print fit, the
axial retention and the press travel are the first article's measurements
(README.md); ``control_actuator`` is closed by design, not by measurement.
"""
from __future__ import annotations

import sys
from pathlib import Path

from printed_geometry import (
    ButtonCap,
    GeometryError,
    apply_cuts,
    button_cap,
    check_solid,
    describe_button_cap,
    export_stl_and_step,
    layout_export_notices,
    layout_source,
    load_layout,
    probe_material,
    validate_button_cap,
    verify_step,
    verify_stl,
)

OUT_DIR = Path(__file__).resolve().parent
STL = OUT_DIR / "button-cap.stl"
STEP = OUT_DIR / "button-cap.step"


def box(cq, x0: float, x1: float, y0: float, y1: float, z0: float, z1: float):
    """An axis-aligned box between two panel-Z planes."""
    return (cq.Workplane("XY").workplane(offset=z0)
            .center((x0 + x1) / 2, (y0 + y1) / 2)
            .rect(x1 - x0, y1 - y0).extrude(z1 - z0))


def build_solid(cq, g: ButtonCap):
    """Build the plate and its one cutter (the cuts are applied later)."""
    half = g.plate_square / 2
    stock = box(cq, -half, half, -half, half, g.top_z, g.rear_face_z)

    locator = g.locator_square / 2
    # Open toward the board and run 1 mm past the rear face, so the cut leaves a
    # clean mouth rather than a coincident face.
    features = [("locator recess",
                 box(cq, -locator, locator, -locator, locator,
                     g.plunger_face_z, g.rear_face_z + 1.0))]
    return stock, features


def probe_points(g: ButtonCap) -> list[tuple[str, bool, list[tuple[float, float, float]]]]:
    """Named material/void samples that prove each feature of the solid.

    The part is centred on the origin. Every sample stays at least 0.15 mm off
    a face. The last group proves the two shape rules the panel forces: nothing
    hangs below the cap rear face into the switch, and no skirt exists in the
    0.225 mm band between the plate and the 12.25 mm cutout.
    """
    half = g.plate_square / 2
    locator = g.locator_square / 2
    return [
        ("plate is solid above the recess floor", True,
         [(0.0, 0.0, g.plunger_face_z - 0.5),
          (half - 0.5, 0.0, g.top_z + 0.5)]),
        ("plate ring is solid beside the recess", True,
         [(half - 0.4, 0.0, g.rear_face_z - 0.4),
          (0.0, half - 0.4, (g.top_z + g.rear_face_z) / 2)]),
        ("locator recess is void", False,
         [(0.0, 0.0, g.plunger_face_z + 0.3),
          (locator - 0.3, 0.0, g.plunger_face_z + 0.3)]),
        ("nothing hangs below the cap rear face into the switch", False,
         [(0.0, 0.0, g.rear_face_z + 0.4),
          (half - 0.4, 0.0, g.rear_face_z + 0.4)]),
        ("no skirt in the cutout clearance band", False,
         [(half + 0.15, 0.0, g.rear_face_z - 0.4),
          (0.0, half + 0.15, g.rear_face_z - 0.4)]),
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
        g = button_cap(layout, source)
        notices = validate_button_cap(g, layout)

        print("== derived from mechanical/layout.json and lib/layout.ts ==")
        for line in describe_button_cap(g):
            print(f"  {line}")

        stock, features = build_solid(cq, g)
        print("== solid self-check ==")
        print(f"  stock volume {stock.val().Volume():.2f} mm^3, "
              f"{len(features)} cuts")
        body, removed = apply_cuts(cq, stock, features)
        report = check_solid(cq, body, g.envelope, removed)
        for line in report.lines():
            print(f"  {line}")

        probes = probe_points(g)
        for what, want_material, points in probes:
            probe_material(cq, body, points, want_material, what)
        print(f"  probes: {len(probes)} feature samples match")

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
    print("PROVISIONAL part: not a released drawing. The first-article fit, "
          "retention and press travel decide it.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

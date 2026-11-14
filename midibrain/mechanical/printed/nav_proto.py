#!/usr/bin/env python3
"""PROTOTYPE NAV sockets matched to the ALPS rounded-square stem tip.

Four printable specimens, each the SAME head, neck, flange and shank as the
installed `nav-keytop.stl`, with the socket cut as a SQUARE instead of the D3.2
circular bore:

    square-2.00.stl           2.00 mm across flats, R0.6 corners, 1.40 deep
    square-2.10.stl           2.10 mm across flats
    square-2.20.stl           2.20 mm across flats
    rock-square-2.10.stl      2.10 mm square + a 6.80 mm neck (ROCK variant)

Why a square: the vendor model's stem tip is a rounded square (1.95 +-0.05 across
flats, R0.5 corners, +-1.184 mm extent) between Z 3.5 and Z 4.9 above the mounting
face, and the Ø3 circular section sits lower, at Z 2.5. A Ø3.2 circular bore
therefore surrounds the SQUARE with 0.416 mm of nominal play at the tip's farthest
points and more toward the flats - it is not a fit at all. A square bore is the
only shape that can touch this tip on four faces.

What these are NOT: a chosen design, a release, or a proven fit. The installed
part is unchanged, every specimen is labelled PROTOTYPE, and the square size,
corner radius and rotation must be chosen by MEASUREMENT on the real switch
(the stem's shape here is vendor MODEL geometry, not a measured tolerance).
Nothing claims grip, retention or that the cap cannot rock or press the switch.

CadQuery, in a private environment (never the system Python):

    uv venv /tmp/cq-venv
    uv pip install --python /tmp/cq-venv/bin/python cadquery
    /tmp/cq-venv/bin/python mechanical/printed/nav_proto.py
"""
from __future__ import annotations

import sys
from pathlib import Path

from printed_geometry import (
    GeometryError,
    NavProtoSpec,
    apply_cuts,
    check_solid,
    export_stl_and_step,
    layout_source,
    load_layout,
    nav_keytop,
    nav_proto_envelope,
    nav_proto_numbers,
    nav_proto_specimens,
    validate_nav_proto,
    verify_step,
    verify_stl,
)

OUT_DIR = Path(__file__).resolve().parent / "nav-keytop-proto"


def cylinder(cq, radius: float, z0: float, z1: float, x: float = 0.0, y: float = 0.0):
    return (cq.Workplane("XY").workplane(offset=z0).center(x, y)
            .circle(radius).extrude(z1 - z0))


def box(cq, x0: float, x1: float, y0: float, y1: float, z0: float, z1: float):
    """An axis-aligned box between two panel-Z planes."""
    return (cq.Workplane("XY").workplane(offset=z0)
            .center((x0 + x1) / 2, (y0 + y1) / 2)
            .rect(x1 - x0, y1 - y0).extrude(z1 - z0))


def rounded_square(cq, g, spec: NavProtoSpec, x: float, y: float,
                   z0: float, z1: float):
    """A square prism with TRUE R=corner_radius corners, centred on (x, y).

    Built as the exact Minkowski sum of a (square - r) box with a disc of radius
    r: two crossing boxes plus four corner cylinders. Every corner is a real
    radius, not a chord. The earlier version sampled three points per corner and
    joined them with straight lines, which made the cavity up to 0.046 mm tighter
    at each corner than its own label (R 0.6, 45 deg steps) - a defect for a
    clearance feature whose smallest size is 0.025 mm per side.
    """
    half = spec.square_mm / 2
    r = spec.corner_radius_mm
    core = half - r
    body = box(cq, x - core, x + core, y - half, y + half, z0, z1)
    body = body.union(box(cq, x - half, x + half, y - core, y + core, z0, z1))
    for sx in (-1, 1):
        for sy in (-1, 1):
            body = body.union(cylinder(cq, r, z0, z1, x + sx * core, y + sy * core))
    return body


def build_specimen(cq, g, spec: NavProtoSpec):
    """The prototype's OWN stock: head, neck, flange and a SHORTER shank.

    The installed part's rear face is at socket_mouth_z 6.95, because its socket
    is 1.50 deep. A 1.40 mm prototype therefore has to END at
    nav_proto_rear_z() = 6.85. Reusing the installed stock would cut a 1.50 mm
    socket and move it 0.10 mm down the stem - the defect this function now
    avoids by building the rear face from the prototype's own planes.
    """
    import nav_keytop as installed
    from printed_geometry import nav_proto_floor_z, nav_proto_rear_z

    rear_z = nav_proto_rear_z(g, spec)
    floor_z = nav_proto_floor_z(g)
    cx, cy = g.centre
    # The chamfer cutters come from the installed builder: they depend only on the
    # head and flange planes, which a prototype does not change.
    _, installed_features = installed.build_solid(cq, g)
    keep = [(name, tool) for name, tool in installed_features
            if name in ("head edge chamfer", "flange rear chamfer")]

    # The stock is always built at the STANDARD neck D7.00; the ROCK variant then
    # CUTS its narrower neck, so that variant is a real, checked feature rather
    # than a stock dimension with nothing to verify.
    stock = cylinder(cq, g.neck_diameter / 2, g.head_front_z, g.flange_front_z, cx, cy)
    stock = stock.union(cylinder(cq, g.flange_diameter / 2, g.flange_front_z,
                                 g.flange_rear_z, cx, cy))
    stock = stock.union(cylinder(cq, g.shank_diameter / 2, g.flange_rear_z, rear_z, cx, cy))

    # A BLIND square socket from the floor to the rear face, plus a square lead-in
    # of NAV_PROTO_LEAD_IN at the mouth. Both cutters run 1 mm PAST the rear face,
    # so the mouth is a clean opening and not a coincident face.
    socket = rounded_square(cq, g, spec, cx, cy, floor_z, rear_z + 1.0)
    lead = spec.square_mm / 2 + 0.30
    lead_in = box(cq, cx - lead, cx + lead, cy - lead, cy + lead,
                  rear_z - 0.30, rear_z + 1.0)
    keep.append(("square socket", socket))
    keep.append(("square socket lead-in", lead_in))

    if spec.neck_diameter_mm != g.neck_diameter:
        # The ROCK variant: narrow the neck over the panel thickness only, so the
        # head still leads through the aperture and the flange still bears on the
        # panel rear. Nothing else changes.
        ring = (cq.Workplane("XY").workplane(offset=g.stack.panel_front_z)
                .center(g.centre[0], g.centre[1])
                .circle(g.neck_diameter / 2 + 1.0)
                .circle(spec.neck_diameter_mm / 2)
                .extrude(g.stack.thickness))
        keep.append(("neck relief (rock variant)", ring))
    return stock, keep


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
        specs = nav_proto_specimens(g)
        OUT_DIR.mkdir(parents=True, exist_ok=True)
        print("== PROTOTYPE NAV sockets: the installed part is NOT changed ==")
        for spec in specs:
            validate_nav_proto(spec, g)
            numbers = nav_proto_numbers(spec, g)
            stock, features = build_specimen(cq, g, spec)
            body, removed = apply_cuts(cq, stock, features)
            envelope = nav_proto_envelope(g, spec)
            report = check_solid(cq, body, envelope, removed)
            stl = OUT_DIR / f"{spec.name}.stl"
            step = OUT_DIR / f"{spec.name}.step"
            export_stl_and_step(cq, body, stl, step)
            mesh = verify_stl(stl, envelope, volume=report.volume)
            step_volume = verify_step(cq, step, report.volume)
            print(f"  {spec.name}: {numbers['square_across_flats_mm']:.2f} mm square, "
                  f"{numbers['clearance_per_side_mm']:.3f} mm per side, "
                  f"corner R{numbers['corner_radius_mm']:.2f}, neck "
                  f"D{numbers['neck_diameter_mm']:.2f}")
            print(f"    solid {report.solids} body / {report.shells} shell, "
                  f"{report.volume:.1f} mm^3; STL {mesh.triangles} triangles, "
                  f"{mesh.open_edges} open edges; STEP {step_volume:.1f} mm^3")
            print(f"    tilt before binding {numbers['tilt_before_binding_deg']:.2f} deg, "
                  f"square rotation backlash {numbers['square_rotation_backlash_deg']:.2f} deg, "
                  f"socket Z {numbers['socket_above_mounting_face_mm']} above the face")
            print(f"    engages ONLY the square tip: "
                  f"{numbers['only_engages_the_square_tip']}; reaches the circular "
                  f"collar: {numbers['reaches_the_circular_collar']}")
    except GeometryError as exc:
        print(f"FAIL {exc}", file=sys.stderr)
        return 1

    print(f"== wrote {OUT_DIR}/*.stl and *.step")
    print("PROTOTYPES: print all four, measure the real stem and its rotation, and "
          "record which one fits. No grip, retention or travel is claimed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

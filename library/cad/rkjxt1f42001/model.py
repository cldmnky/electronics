#!/usr/bin/env python3
"""ALPS Alpine RKJXT1F42001 - self-authored STEP model (Apache-2.0).

Every dimension below is taken from the ALPS drawing on catalogue page 398
("PC board mounting hole dimensions" / "Dimensions"), which is the same drawing
the footprint in `footprint.tsx` was authored from. Where the drawing prints no
value, the constant is marked MODEL-DERIVED: it is a simplification chosen for
this model, checked once against the owner's local, non-redistributable
alternate STEP as a sanity check, and it is NOT derived from that file. No
vendor CAD, no owner-supplied STEP and no SamacSys file is read, imported or
tessellated here; the result is our geometry and may be redistributed.

Frame (identical to footprint.tsx): origin on the shaft axis at the PCB
seating plane, +z toward the panel/shaft tip, +x/+y as in the printed hole
table.

    /tmp/cq-venv/bin/python library/cad/rkjxt1f42001/model.py --out model.step
    /tmp/cq-venv/bin/python library/cad/rkjxt1f42001/model.py --verify

`--verify` writes the model and then re-loads the written STEP to check the
header, the overall envelope and the key section planes. See SOURCE.md beside
this file for the provenance, the SHA-256 values and the list of what stays
simplified.
"""

from __future__ import annotations

import argparse
import hashlib
import math
import re
from pathlib import Path
from typing import Sequence

import cadquery as cq

SCRIPT_PATH = Path(__file__).resolve()
DEFAULT_OUT = SCRIPT_PATH.with_name("model.step")

# --------------------------------------------------------------------------
# Drawing dimensions. Page 398 unless the comment says MODEL-DERIVED.
# --------------------------------------------------------------------------

# Lower housing / frame: "4-17" prints 17.0 x 17.0 mm in plan, from the PCB
# seating plane to the top of the frame. MODEL-DERIVED: the 1.0 mm corner
# rounds (the drawing prints the square envelope; the alternate STEP's frame is
# a 17.2 mm square with lead cut-outs, ours is the simplified envelope).
FRAME_HALF = 8.5
FRAME_Z0 = 0.0
FRAME_Z1 = 3.0
FRAME_CORNER_R = 1.0

# Upper body: regular octagon, "4-14.5" across the flats, from the frame top to
# 8.6 mm above the seating plane (page 398). A regular octagon is fully defined
# by its inradius, so the vertex coordinate 7.25*(sqrt2-1) = 3.003 mm is
# derived, not printed.
BODY_ACROSS_FLATS = 14.5
BODY_Z0 = FRAME_Z1
BODY_Z1 = 8.6

# Stationary collar: page 398 prints the "10.5" collar top; MODEL-DERIVED: the
# collar diameter D7.20 (the drawing prints no collar diameter) and its 0.2 mm
# top chamfer. D7.20 is the same figure as the 3.6 mm silkscreen circle in
# footprint.tsx and the feature the panel opening has to clear.
COLLAR_D = 7.20
COLLAR_Z0 = BODY_Z1
COLLAR_Z1 = 10.5
COLLAR_TOP_CHAMFER = 0.2

# Four housing tabs. MODEL-DERIVED from the local alternate STEP, simplified to
# boxes: 0.8 mm wide (tangential) x 1.3 mm long (radial) x 0.4 mm high, sitting
# on the body top at 8.6 mm. The real clips are bent; this model keeps their
# envelope only.
TAB_W = 0.8
TAB_L = 1.3
TAB_H = 0.4
TAB_R = 5.18
TAB_Z0 = BODY_Z1

# Shaft. Page 398: main shaft "oe2.5", tip at "17.4", D-flat "1.8" across, and
# the flat side at y = +0.55 (0.55 mm from the axis; the far side of the D is
# the 2.5 mm shaft edge at y = -1.25). MODEL-DERIVED: the D1.8 base neck, its
# 10.5..11.2 mm height, the 0.2 mm tip chamfer and the +y flat orientation
# (the drawing prints the across-the-flat figure, not which side it faces).
NECK_D = 1.8
NECK_Z0 = COLLAR_Z1
NECK_Z1 = 11.2
SHAFT_D = 2.5
SHAFT_Z0 = NECK_Z1
SHAFT_Z1 = 17.4
FLAT_Y = 0.55
SHAFT_TIP_CHAMFER = 0.2

# Terminal posts: the drawing prints flat blades 0.7 x 0.25 mm that stand
# "3.2" mm below the seating plane. A straight post per hole is a
# simplification of the vendor's bent blades (the real ones are kinked inside
# the housing to reach the contacts). MODEL-DERIVED: the orientation rule -
# each blade's 0.25 mm face points at the shaft axis and its 0.7 mm width runs
# tangentially, the same rule for all ten posts.
BLADE_W = 0.7
BLADE_T = 0.25
BLADE_Z0 = -3.2
BLADE_Z1 = 0.0

# Position lug: the printed "for position lug" hole is D1.1 (non-plated, see
# footprint.tsx). MODEL-DERIVED: the 1.2 mm projection below the seating plane
# (the alternate STEP's lug ends 1.2 mm below the plane).
LUG_D = 1.1
LUG_DEPTH = 1.2

# Printed hole table, mm from the shaft axis. Same eleven positions as
# footprint.tsx; the ten electrical terminals are the posts above, the lug is
# separate and non-electrical.
HOLES: tuple[tuple[str, float, float], ...] = (
    ("A", -1.5, -7.8),
    ("B", 7.8, -1.5),
    ("C", 1.5, 7.8),
    ("D", -7.8, 1.5),
    ("com", -1.0, 5.78),
    ("Push", 1.0, -6.98),
    ("Ground", 6.86, -3.75),
    ("EncoderA", -7.8, -1.5),
    ("EncoderB", 7.8, 1.5),
    ("EncoderCom", -1.5, 7.8),
)
LUG_X = -3.8
LUG_Y = -1.5

# OCCT stamps the export time into FILE_NAME. Pinning it keeps a re-run of this
# script byte-identical, so the SHA-256 in SOURCE.md stays checkable.
STEP_HEADER_NAME = "RKJXT1F42001.step"
STEP_TIME_STAMP = "2026-11-15T00:00:00"


# --------------------------------------------------------------------------
# Geometry
# --------------------------------------------------------------------------


def _octagon_vertices(across_flats: float) -> list[tuple[float, float]]:
    """Regular octagon with flats facing +-x/+-y, so its bbox is across-flats."""
    r = across_flats / 2.0
    v = r * (math.sqrt(2.0) - 1.0)  # where the 45 degree corner cuts meet the flats
    return [
        (r, v),
        (v, r),
        (-v, r),
        (-r, v),
        (-r, -v),
        (-v, -r),
        (v, -r),
        (r, -v),
    ]


def _box(x0: float, x1: float, y0: float, y1: float, z0: float, z1: float) -> cq.Solid:
    return cq.Solid.makeBox(x1 - x0, y1 - y0, z1 - z0, cq.Vector(x0, y0, z0))


def _frame() -> cq.Solid:
    """17.0 x 17.0 mm lower housing, 0..3.0 mm, 1.0 mm corner rounds."""
    return (
        cq.Workplane("XY")
        .rect(2 * FRAME_HALF, 2 * FRAME_HALF)
        .extrude(FRAME_Z1 - FRAME_Z0)
        .translate((0, 0, FRAME_Z0))
        .edges("|Z")
        .fillet(FRAME_CORNER_R)
        .val()
    )


def _body() -> cq.Solid:
    """Upper body: regular octagon 14.5 mm across flats, z 3.0..8.6."""
    return (
        cq.Workplane("XY")
        .workplane(offset=BODY_Z0)
        .polyline(_octagon_vertices(BODY_ACROSS_FLATS))
        .close()
        .extrude(BODY_Z1 - BODY_Z0)
        .val()
    )


def _collar() -> cq.Solid:
    """D7.20 stationary collar, z 8.6..10.5, 0.2 mm chamfer on the top edge."""
    return (
        cq.Workplane("XY")
        .workplane(offset=COLLAR_Z0)
        .circle(COLLAR_D / 2.0)
        .extrude(COLLAR_Z1 - COLLAR_Z0)
        .faces(">Z")
        .edges()
        .chamfer(COLLAR_TOP_CHAMFER)
        .val()
    )


def _shaft() -> cq.Solid:
    """D1.8 neck (10.5..11.2) plus the D2.5 shaft with the +y flat (11.2..17.4)."""
    neck = (
        cq.Workplane("XY")
        .workplane(offset=NECK_Z0)
        .circle(NECK_D / 2.0)
        .extrude(NECK_Z1 - NECK_Z0)
        .val()
    )
    shaft = (
        cq.Workplane("XY")
        .workplane(offset=SHAFT_Z0)
        .circle(SHAFT_D / 2.0)
        .extrude(SHAFT_Z1 - SHAFT_Z0)
        # The flat: 0.55 mm from the axis on +y, so the D measures 1.8 across.
        .cut(_box(-SHAFT_D, SHAFT_D, FLAT_Y, FLAT_Y + 1.0, SHAFT_Z0, SHAFT_Z1))
        # Chamfer after the cut so it wraps the D profile, flat included.
        .faces(">Z")
        .edges()
        .chamfer(SHAFT_TIP_CHAMFER)
        .val()
    )
    return neck.fuse(shaft).clean()


def _posts() -> list[cq.Solid]:
    """One straight 0.7 x 0.25 mm blade per printed terminal hole, z -3.2..0."""
    posts = []
    for _name, x, y in HOLES:
        blade = cq.Solid.makeBox(
            BLADE_T,
            BLADE_W,
            BLADE_Z1 - BLADE_Z0,
            cq.Vector(-BLADE_T / 2.0, -BLADE_W / 2.0, BLADE_Z0),
        )
        # At angle 0 the 0.25 mm thickness is radial (x) and the 0.7 mm width is
        # tangential (y); rotating by the hole's polar angle keeps that rule for
        # all ten posts and keeps every post inside the 17.0 mm envelope.
        posts.append(
            blade.rotate(
                cq.Vector(0, 0, 0), cq.Vector(0, 0, 1), math.degrees(math.atan2(y, x))
            ).translate(cq.Vector(x, y, 0))
        )
    return posts


def _lug() -> cq.Solid:
    """D1.1 position lug at the printed lug hole, 1.2 mm below the plane."""
    return cq.Solid.makeCylinder(
        LUG_D / 2.0, LUG_DEPTH, cq.Vector(LUG_X, LUG_Y, -LUG_DEPTH), cq.Vector(0, 0, 1)
    )


def _tabs() -> list[cq.Solid]:
    """Four 0.8 x 1.3 x 0.4 mm housing tabs on the body top, radial long side."""
    tabs = []
    for sx, sy in ((0, 1), (0, -1), (1, 0), (-1, 0)):
        cx, cy = TAB_R * sx, TAB_R * sy
        half_x = TAB_W / 2.0 if sx == 0 else TAB_L / 2.0
        half_y = TAB_L / 2.0 if sx == 0 else TAB_W / 2.0
        tabs.append(
            _box(cx - half_x, cx + half_x, cy - half_y, cy + half_y, TAB_Z0, TAB_Z0 + TAB_H)
        )
    return tabs


def build() -> cq.Shape:
    """The whole part as one fused solid (a compound only if a face contact fails)."""
    pieces: list[cq.Shape] = [
        _frame(),
        _body(),
        *_tabs(),
        _collar(),
        _shaft(),
        *_posts(),
        _lug(),
    ]
    return pieces[0].fuse(*pieces[1:]).clean()


# --------------------------------------------------------------------------
# Export
# --------------------------------------------------------------------------


def _pin_step_header(text: str) -> str:
    """Name the part and fix the export time stamp so re-runs are identical."""
    text = re.sub(
        r"(FILE_NAME\(')[^']*(')", r"\g<1>" + STEP_HEADER_NAME + r"\g<2>", text, count=1
    )
    text = re.sub(
        r"(FILE_NAME\('[^']*',')[^']*(')", r"\g<1>" + STEP_TIME_STAMP + r"\g<2>", text, count=1
    )
    return text


def write_step(out: Path) -> Path:
    out.parent.mkdir(parents=True, exist_ok=True)
    cq.exporters.export(build(), str(out), exportType="STEP", unit="MM", outputUnit="MM")
    text = out.read_text(encoding="latin-1")
    out.write_text(_pin_step_header(text), encoding="latin-1")
    return out


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


# --------------------------------------------------------------------------
# Verification
# --------------------------------------------------------------------------

# (label, z, expected xmin/xmax/ymin/ymax in mm); each plane is inside a solid
# region so the section is a real cross-section, never a tangent face.
SECTION_PLANES: tuple[tuple[str, float, tuple[float, float, float, float]], ...] = (
    (
        "z=1.0 lower housing 17.0 x 17.0",
        1.0,
        (-FRAME_HALF, FRAME_HALF, -FRAME_HALF, FRAME_HALF),
    ),
    (
        "z=4.0 upper body 14.5 across flats",
        4.0,
        (
            -BODY_ACROSS_FLATS / 2,
            BODY_ACROSS_FLATS / 2,
            -BODY_ACROSS_FLATS / 2,
            BODY_ACROSS_FLATS / 2,
        ),
    ),
    ("z=9.5 collar D7.20", 9.5, (-COLLAR_D / 2, COLLAR_D / 2, -COLLAR_D / 2, COLLAR_D / 2)),
    ("z=12.0 shaft, 1.8 across the flat", 12.0, (-1.25, 1.25, -1.25, FLAT_Y)),
    (
        "z=17.3 shaft tip inside D2.5",
        17.3,
        (
            -(SHAFT_D / 2 - SHAFT_TIP_CHAMFER / 2),
            SHAFT_D / 2 - SHAFT_TIP_CHAMFER / 2,
            -(SHAFT_D / 2 - SHAFT_TIP_CHAMFER / 2),
            FLAT_Y - SHAFT_TIP_CHAMFER / 2,
        ),
    ),
)

TOL = 5e-3


class Report:
    def __init__(self) -> None:
        self.failures = 0
        self.checks = 0

    def check(self, ok: bool, label: str, detail: str) -> None:
        self.checks += 1
        if not ok:
            self.failures += 1
        print(f"{'PASS' if ok else 'FAIL'}  {label}: {detail}")

    def bbox(
        self, label: str, got: Sequence[float], want: Sequence[float], tol: float = TOL
    ) -> None:
        ok = all(abs(g - w) <= tol for g, w in zip(got, want))
        self.check(ok, label, f"got {_ranges(got)} want {_ranges(want)} +-{tol}")


def _ranges(v: Sequence[float]) -> str:
    return f"x {v[0]:+.3f}..{v[1]:+.3f} y {v[2]:+.3f}..{v[3]:+.3f}"


def _section_ranges(shape: cq.Shape, z: float) -> list[float] | None:
    section = cq.Workplane("XY").add(shape).section(z)
    if not section.vals():
        return None
    bb = section.val().BoundingBox()
    return [bb.xmin, bb.xmax, bb.ymin, bb.ymax]


def _probe(shape: cq.Shape, x: float, y: float, z: float, size: float = 0.06) -> float:
    """Volume of the model inside a small box centred on (x, y, z)."""
    h = 0.02
    box = cq.Solid.makeBox(
        size, size, h, cq.Vector(x - size / 2.0, y - size / 2.0, z - h / 2.0)
    )
    try:
        return shape.intersect(box).Volume()
    except Exception:  # empty intersection: OCCT raises on a null shape
        return 0.0


def verify(path: Path) -> int:
    report = Report()
    text = path.read_text(encoding="latin-1")

    report.check(text.startswith("ISO-10303-21;"), "STEP header", "starts with ISO-10303-21;")
    report.check(
        "END-ISO-10303-21;" in text,
        "STEP trailer",
        "ENDSEC + END-ISO-10303-21 present",
    )
    report.check(
        "EXTERNAL_REFERENCE" not in text,
        "no external references",
        "no EXTERNAL_REFERENCE entity",
    )
    report.check(
        "SI_UNIT(.MILLI.,.METRE.)" in text,
        "units",
        "LENGTH_UNIT is SI_UNIT(.MILLI.,.METRE.) - millimetres",
    )

    shape = cq.importers.importStep(str(path)).val()
    solids = shape.Solids()
    report.check(shape.isValid(), "solids valid", f"{len(solids)} solid(s), OCCT valid")
    report.check(
        len(solids) >= 1,
        "no nested assembly",
        f"top-level shape holds {len(solids)} solid(s)",
    )

    bb = shape.BoundingBox()
    envelope = [bb.xmin, bb.xmax, bb.ymin, bb.ymax]
    inside = max(abs(bb.xmin), abs(bb.xmax), abs(bb.ymin), abs(bb.ymax)) <= FRAME_HALF + TOL
    report.check(
        inside,
        "bbox inside the 17.0 mm housing (+-8.5)",
        _ranges(envelope) + f" z {bb.zmin:+.3f}..{bb.zmax:+.3f}",
    )
    report.bbox("bbox x/y", envelope, [-FRAME_HALF, FRAME_HALF, -FRAME_HALF, FRAME_HALF])
    report.check(
        abs(bb.zmin - BLADE_Z0) <= TOL and abs(bb.zmax - SHAFT_Z1) <= TOL,
        "bbox z",
        f"z {bb.zmin:+.3f}..{bb.zmax:+.3f} want {BLADE_Z0:+.3f}..{SHAFT_Z1:+.3f}",
    )

    for label, z, want in SECTION_PLANES:
        got = _section_ranges(shape, z)
        if got is None:
            report.check(False, label, "empty section")
            continue
        report.bbox(label, got, want)

    for name, x, y in HOLES:
        volume = _probe(shape, x, y, BLADE_Z0 + 0.01)
        report.check(
            volume > 0,
            f"post {name} reaches z={BLADE_Z0} at ({x:+.2f}, {y:+.2f})",
            f"probe volume {volume:.6f} mm3",
        )

    lug_volume = _probe(shape, LUG_X, LUG_Y, -LUG_DEPTH + 0.01)
    report.check(
        lug_volume > 0,
        f"position lug reaches z={-LUG_DEPTH} at ({LUG_X}, {LUG_Y})",
        f"probe volume {lug_volume:.6f} mm3",
    )

    for sx, sy in ((0, 1), (0, -1), (1, 0), (-1, 0)):
        tab_volume = _probe(shape, TAB_R * sx, TAB_R * sy, TAB_Z0 + TAB_H / 2)
        report.check(
            tab_volume > 0,
            f"housing tab at ({TAB_R * sx:+g}, {TAB_R * sy:+g}) z {TAB_Z0}..{TAB_Z0 + TAB_H}",
            f"probe volume {tab_volume:.6f} mm3",
        )

    print(f"\n{report.checks - report.failures}/{report.checks} checks passed")
    return 1 if report.failures else 0


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT, help="STEP file to write")
    parser.add_argument(
        "--verify", action="store_true", help="re-load the written STEP and check the key planes"
    )
    args = parser.parse_args(argv)

    out = write_step(args.out)
    print(f"wrote {out} ({out.stat().st_size} bytes)")
    print(f"sha256 {out.name} {sha256(out)}")
    print(f"sha256 {SCRIPT_PATH.name} {sha256(SCRIPT_PATH)}")
    if args.verify:
        print()
        return verify(out)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

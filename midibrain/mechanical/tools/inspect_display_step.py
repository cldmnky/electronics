#!/usr/bin/env python3
"""Inspect the vendor display STEP with a real CAD kernel.

The panel aperture and the display placement depend on which plane is the glass,
which plane is the PCB and where the mounting bosses sit. The text-only reader
(``measure_display_step.py``) reports every solid in its own frame; this tool
imports the assembly with CadQuery, so the transforms are resolved and the
landmarks are the ones the placement uses.

    LD_LIBRARY_PATH=<libGL prefix> /tmp/cq-venv/bin/python \
      mechanical/tools/inspect_display_step.py /path/4_3inch-dsi-lcd-V1.stp

It prints, and nothing else - no writes, no exports:

  * the union bounding box,
  * every planar face normal to Z, grouped by its Z plane, with outline size,
  * the solid candidates for the SMT mounting bosses,
  * the solids whose footprint is 21.412 x 4.826 mm.

About those 21.412 x 4.826 mm solids: they are **TFT1 flex bonds**, named
``EXTRUDED-106739`` and ``EXTRUDED-107488`` under ``TFT1_ASM`` (verified by an
XCAF label walk; recorded in ``step_geometry.VENDOR_DISPLAY_MODEL`` as
``tft1_flex_bonds``). They are NOT a panel connector, and this tool does not name
a connector. It reports geometry only, in the assembly frame, and does not print
the inner-hole pattern; read that from the labels or from
``step_geometry.VENDOR_DISPLAY_MODEL``.

CadQuery is optional and never installed into the system Python. The OpenGL
note in ``mechanical/step_assembly.py`` applies here too.
"""
from __future__ import annotations

import sys
from collections import defaultdict
from pathlib import Path

DEFAULT = (Path(__file__).resolve().parent.parent
           / "reference" / "waveshare-4.3inch-dsi-lcd.step")


def main(argv: list[str]) -> int:
    try:
        import cadquery as cq
    except ImportError as exc:  # pragma: no cover - environment dependent
        print(f"CadQuery is not importable ({exc}).", file=sys.stderr)
        return 1
    path = Path(argv[1]) if len(argv) > 1 else DEFAULT
    if not path.is_file():
        print(f"{path} is not a file; download the vendor drawing ZIP and pass the .stp",
              file=sys.stderr)
        return 1

    shape = cq.importers.importStep(str(path)).val()
    bb = shape.BoundingBox()
    print(f"file      {path}")
    print(f"solids    {len(shape.Solids())}")
    print(f"bbox      x {bb.xmin:8.3f}..{bb.xmax:8.3f} ({bb.xlen:7.3f})  "
          f"y {bb.ymin:8.3f}..{bb.ymax:8.3f} ({bb.ylen:7.3f})  "
          f"z {bb.zmin:8.3f}..{bb.zmax:8.3f} ({bb.zlen:7.3f})")

    planes: dict[float, dict] = defaultdict(
        lambda: {"area": 0.0, "faces": 0, "x": [1e9, -1e9], "y": [1e9, -1e9]})
    for face in shape.Faces():
        if face.geomType() != "PLANE":
            continue
        if abs(abs(face.normalAt().z) - 1.0) > 1e-6:
            continue
        key = round(face.Center().z, 4)
        fb = face.BoundingBox()
        entry = planes[key]
        entry["area"] += face.Area()
        entry["faces"] += 1
        entry["x"] = [min(entry["x"][0], fb.xmin), max(entry["x"][1], fb.xmax)]
        entry["y"] = [min(entry["y"][0], fb.ymin), max(entry["y"][1], fb.ymax)]

    print("\nplanar faces normal to Z, highest plane first:")
    print(f"{'z':>9} {'area':>9} {'faces':>6}  {'x extent':>20} {'y extent':>20}")
    for z in sorted(planes, reverse=True):
        e = planes[z]
        print(f"{z:9.4f} {e['area']:9.2f} {e['faces']:6d}  "
              f"{e['x'][0]:9.3f}..{e['x'][1]:9.3f} {e['y'][0]:9.3f}..{e['y'][1]:9.3f}")

    print("\nSMT boss candidates (5.0-6.0 mm cube, spanning the PCB):")
    bosses = []
    for solid in shape.Solids():
        b = solid.BoundingBox()
        if 5.0 <= b.xlen <= 6.0 and 5.0 <= b.ylen <= 6.0 and 5.0 <= b.zlen <= 6.0:
            c = solid.Center()
            bosses.append((c.x, c.y, b.zmin, b.zmax))
    for x, y, zmin, zmax in sorted(bosses):
        print(f"  centre ({x:8.3f}, {y:8.3f})  z {zmin:7.3f}..{zmax:7.3f}")
    if bosses:
        xs = sorted({round(b[0], 3) for b in bosses})
        ys = sorted({round(b[1], 3) for b in bosses})
        print(f"  x set {xs}")
        print(f"  y set {ys}")
        print(f"  outer pattern  x span {max(xs) - min(xs):.3f}  "
              f"y span {max(ys) - min(ys):.3f}  "
              f"centre ({(max(xs) + min(xs)) / 2:.3f}, {(max(ys) + min(ys)) / 2:.3f})")

    print("\n21.412 x 4.826 mm bodies (TFT1 flex bonds, EXTRUDED-106739 / 107488;")
    for solid in shape.Solids():
        b = solid.BoundingBox()
        dims = sorted([b.xlen, b.ylen, b.zlen])
        if 20.5 < dims[2] < 22.5 and 4.2 < dims[1] < 5.4 and dims[0] < 2.0:
            c = solid.Center()
            print(f"  centre ({c.x:8.3f}, {c.y:8.3f}, {c.z:8.3f})  "
                  f"span {b.xlen:.3f} x {b.ylen:.3f} x {b.zlen:.3f}  "
                  f"z {b.zmin:.3f}..{b.zmax:.3f}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))

#!/usr/bin/env python3
"""Check that the silkscreen stays inside the board outline.

Usage:
    python3 froemix/tools/check_silk.py

DESIGN-REVIEW R12: the bottom board notes used a left anchor on a mirrored
layer, so the exported B_SilkScreen.gbr carried glyphs down to x = -47.5mm
while Edge_Cuts.gbr stopped at x = -40.45mm - the revision and date were
clipped off the fabricated board and no gate step noticed. The PCB SVG
snapshot cannot catch this either: it draws the clipped result.

This check exports the gerbers (`tsci export -f gerbers`, the same files
`bun run release` ships) into a temp dir, unzips them, and compares every
coordinate word of B_SilkScreen.gbr and F_SilkScreen.gbr against the bounding
box of Edge_Cuts.gbr inset by INSET_MM. Exits 1 and lists the offending
extents; prints one summary line with both layers' extents and the board size.

Only `X..Y..D01/D02/D03` coordinate words are read. The aperture macros
(`%AM...%`) and aperture definitions (`%ADD..*%`) are full of numbers that look
like geometry, and the coordinate words are modal (an X-only or Y-only word
keeps the other axis from the previous point), so the parser tracks one
current point per file and ignores everything else.

The check is on coordinates, not on the stroke width: a 0.15mm silk line whose
endpoint sits exactly on the inset boundary can still overhang by half a
stroke. That is why the silk layout keeps a ~1mm margin rather than relying on
the 0.05mm inset.
"""

from __future__ import annotations

import os
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile

INSET_MM = 0.05

SILK_LAYERS = ("B_SilkScreen.gbr", "F_SilkScreen.gbr")
OUTLINE_LAYER = "Edge_Cuts.gbr"

# X/Y are 4.6-format integers (1e-6 mm), leading zero omitted, absolute. The
# word may follow a G-code on the same line, so it is matched at end of line
# rather than at the start.
COORD_WORD = re.compile(r"(?:X(-?\d+))?(?:Y(-?\d+))?D0[123]\*$")

TSCIRCUIT_DIR = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "tscircuit")


def parse_coords(path: str) -> list[tuple[float, float]]:
    """Every X/Y coordinate word in a gerber file, in mm, in file order."""
    points: list[tuple[float, float]] = []
    x: float | None = None
    y: float | None = None
    with open(path, encoding="ascii", errors="replace") as handle:
        for line in handle:
            match = COORD_WORD.search(line.strip())
            if not match:
                continue
            if match.group(1) is not None:
                x = int(match.group(1)) / 1e6
            if match.group(2) is not None:
                y = int(match.group(2)) / 1e6
            if x is not None and y is not None:
                points.append((x, y))
    return points


def extent(points: list[tuple[float, float]]) -> tuple[float, float, float, float]:
    xs = [p[0] for p in points]
    ys = [p[1] for p in points]
    return min(xs), max(xs), min(ys), max(ys)


def export_gerbers(workdir: str) -> None:
    """Run `tsci export -f gerbers` in tscircuit/ and unzip it into workdir."""
    zip_path = os.path.join(workdir, "froemix-gerbers.zip")
    cmd = ["bunx", "tsci", "export", "index.circuit.tsx", "-f", "gerbers", "-o", zip_path]
    result = subprocess.run(cmd, cwd=TSCIRCUIT_DIR, capture_output=True, text=True)
    if result.returncode != 0 or not os.path.exists(zip_path):
        sys.stderr.write("gerber export failed: %s\n" % " ".join(cmd))
        sys.stderr.write(result.stdout[-2000:] + result.stderr[-2000:])
        raise SystemExit(1)
    with zipfile.ZipFile(zip_path) as archive:
        archive.extractall(workdir)


def main() -> int:
    workdir = tempfile.mkdtemp(prefix="froemix-silk-")
    try:
        export_gerbers(workdir)

        outline = parse_coords(os.path.join(workdir, OUTLINE_LAYER))
        if not outline:
            sys.stderr.write("%s has no coordinate words - cannot locate the board outline\n" % OUTLINE_LAYER)
            return 1
        left, right, bottom, top = extent(outline)
        limit = (left + INSET_MM, right - INSET_MM, bottom + INSET_MM, top - INSET_MM)

        failures: list[str] = []
        extents: list[str] = []
        for layer in SILK_LAYERS:
            path = os.path.join(workdir, layer)
            if not os.path.exists(path):
                failures.append("%s: missing from the gerber export" % layer)
                continue
            points = parse_coords(path)
            if not points:
                failures.append("%s: no coordinate words" % layer)
                continue
            x0, x1, y0, y1 = extent(points)
            extents.append("%s x %7.2f..%7.2f y %7.2f..%7.2f" % (layer.removesuffix(".gbr"), x0, x1, y0, y1))
            outside = [p for p in points if not (limit[0] <= p[0] <= limit[1] and limit[2] <= p[1] <= limit[3])]
            if outside:
                ox0, ox1, oy0, oy1 = extent(outside)
                failures.append(
                    "%s: %d of %d coordinate words outside the board outline "
                    "(offending extents x %.3f..%.3f y %.3f..%.3f, allowed x %.3f..%.3f y %.3f..%.3f)"
                    % (layer, len(outside), len(points), ox0, ox1, oy0, oy1, limit[0], limit[1], limit[2], limit[3])
                )

        board = "board %.2f x %.2f mm, inset %.2fmm" % (right - left, top - bottom, INSET_MM)
        if failures:
            for failure in failures:
                sys.stderr.write("silk out of bounds: %s\n" % failure)
            sys.stderr.write("silkscreen check FAILED: %s\n" % board)
            return 1
        print("silkscreen inside %s: %s" % (board, " | ".join(extents)))
        return 0
    finally:
        shutil.rmtree(workdir, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())

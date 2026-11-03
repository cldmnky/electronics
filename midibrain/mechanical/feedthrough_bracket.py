#!/usr/bin/env python3
"""Provisional feedthrough/strain-relief bracket for the MIDI Brain front panel.

Not a final mechanical release: verify the purchased USB-C/USB-A feedthrough
bodies and panel thickness before printing. Units: mm.
"""
import cadquery as cq
from pathlib import Path

foot = cq.Workplane("XY").box(105, 4, 3, centered=(False, False, False)).translate((-60, 43, 0))
plate = cq.Workplane("XY").box(105, 17, 3, centered=(False, False, False)).translate((-60, 47, 0))
plate = plate.union(foot)
# Three open-top slots for the panel-feedthrough bodies.
for x in (-48, -13, 22):
    slot = cq.Workplane("XY").box(18, 8, 4, centered=(False, False, False)).translate((x - 9, 55, -0.5))
    plate = plate.cut(slot)
# Two M2.5 mounting holes aligned with the front board.
for x in (-61.5, 40):
    plate = plate.cut(cq.Workplane("XY").circle(1.35).extrude(5).translate((x, 46.5, -1)))
# Cable-tie/strain-relief holes between the feedthrough positions.
for x in (-58, -30, 5, 35):
    plate = plate.cut(cq.Workplane("XY").circle(1.6).extrude(5).translate((x, 61, -1)))
out = Path(__file__).resolve().parent
cq.exporters.export(plate, str(out / "feedthrough-bracket.stl"))
cq.exporters.export(plate, str(out / "feedthrough-bracket.step"))
print(out / "feedthrough-bracket.stl")

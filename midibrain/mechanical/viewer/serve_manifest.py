"""Write models.json, with each model's place in the panel stack.

Why this is not just a file list: every `tsci export -f step` board comes out in
its OWN frame - x/y are panel-aligned, but z is measured from that board's
mid-plane with +z pointing out of the face that looks at the panel. Loading them
all at their own origin piles six solids on top of each other, which is what the
viewer used to do. Each model now carries a scene transform:

    scaleZ = -1            main.js maps panel z (rearward) to scene -z; the
                           export's +z already points at the panel, so negating
                           it puts the panel-facing side on +scene z
    positionZ = -(T + 0.8) shift it rearward until the panel-facing face sits at
                           panel z = T, i.e. scene z = -T

SCENE CONVENTION (do not regress this): the stack runs along SCENE Z with the
PANEL FRONT at z = 0 and everything else at NEGATIVE z, panel nearest the
camera; the camera stands on +z looking down -z, with +y up. That is the same
orientation tscircuit's 3D view draws: screen-right = +x, screen-up = +y. It
used to run along scene Y with the camera behind the rear board, and a left-
handed panel frame (x right, y up, z rearward) then read mirrored. Only a
z-along-the-view fix can correct that; no camera position can.

`T` is that board's face nearest the panel in the panel frame, straight out of
mechanical/layout.json provisionalZ (panel front = 0, +z rearward). 0.8 is half
the 1.6 mm board thickness, i.e. the mid-plane the export is centred on.

This is a viewing aid, not a fit approval.
"""
import json
import sys
from pathlib import Path

root = Path(sys.argv[1])
out = Path(sys.argv[2])
layout = json.loads((root / "mechanical/layout.json").read_text(encoding="utf-8"))
z = layout["provisionalZ"]
half = layout["frontPcb"]["thickness"] / 2.0

# model stem -> (panel-frame face nearest the panel, shown on load)
PLACEMENT = {
    "panel": (z["panelFront"], True),
    "front": (z["frontBoardTop"], True),
    "rear": (z["rearBoardFront"], True),
    "assembly": (z["frontBoardTop"], False),  # duplicates panel + front + rear
    "pi-panel": (None, False),                # own frame, not part of the stack
}

models = []
for pattern, label in (
    ("mechanical/reference/*.step", "reference"),
    ("mechanical/cad-out/*.step", "assembly"),
    ("mechanical/viewer/models/*.step", "viewer"),
):
    for path in sorted(root.glob(pattern)):
        # A reference model (the vendor display) is in its own frame, so it
        # cannot be placed in the stack without its origin; start hidden.
        top, shown = PLACEMENT.get(path.stem, (None, False))
        if label == "reference":
            # The vendor module is in its own frame AND its STEP carries
            # non-solid construction geometry (raw points reach x 106 where the
            # solid envelope is 106.1 wide), so it cannot be placed by centre
            # arithmetic. main.js aligns it on the LOADED bounding box instead:
            # centre it on the display position and put the glass face on the
            # panel-front plane, screen toward the panel.
            shown = True
        entry = {
            "label": f"{label}: {path.name}",
            "url": str(path.relative_to(root)),
            "bytes": path.stat().st_size,
            "visible": shown,
        }
        if label == "reference":
            # Ground truth from the STEP itself (occt product bboxes): BOARD at
            # z -3.34..-1.74, SMTSO bosses -7.34..-1.84, IPS/touch panel up to
            # +4.8. So +z is the SCREEN side, and the screen must end up facing
            # the panel: negate z only (scaleZ), which keeps board x/y intact.
            entry["scaleZ"] = -1
            entry["align"] = {
                "x": layout["display"]["x"],
                "y": layout["display"]["y"],
                "frontSceneZ": z["displayGlassFront"],
                "depth": round(z["displayRearMost"] - z["displayGlassFront"], 3),
            }
        if top is not None:
            entry["scaleZ"] = -1
            entry["positionZ"] = round(-(top + half), 3)
            entry["panelTopZ"] = round(top, 3)
        models.append(entry)

carrier = {
    "bosses": layout["displayBossPattern"],
    "offsetX": layout["display"]["x"],
    "offsetY": layout["display"]["y"],
    "moduleRear": z["displayRearMost"],
    "rearBoardFront": z["rearBoardFront"],
}

# The panel's printed silkscreen. A STEP carries surfaces, not ink, so the only
# way the viewer can show the wordmark and the legends is the panel's own 2D
# artwork: `make mechanical-steps` exports panel.circuit.tsx with
# `tsci export -f pcb-svg --layer top` (the same format scripts/render_boards.ts
# uses) into the viewer directory. main.js crops it to the board outline, keeps
# the pcb_silkscreen_* marks and maps them onto the panel front face as a
# texture - flat ink, never extruded geometry.
silk_svg = root / "mechanical/viewer/panel-silk.svg"
data = {"base": "/", "models": models, "carrier": carrier}
if silk_svg.is_file():
    data["silk"] = {
        "url": str(silk_svg.relative_to(root)),
        "width": layout["panel"]["width"],
        "height": layout["panel"]["height"],
    }
out.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
print(f"mechanical-view: {len(models)} STEP file(s) listed in {out}")
for m in models:
    if "positionZ" in m:
        where = f"panel z {m['panelTopZ']:5.2f}..{m['panelTopZ'] + 2 * half:5.2f}  (scene z {m['positionZ']:+7.2f})"
    else:
        where = "own frame, aligned on its loaded bbox, screen toward the panel"
    print(f"  {'shown ' if m['visible'] else 'hidden'} {m['label']:<46} {where}")
if "silk" in data:
    print(f"  silk    {data['silk']['url']}  ({data['silk']['width']} x {data['silk']['height']} mm panel)")
else:
    print("  silk    MISSING: run `make mechanical-steps` for the panel silkscreen svg")

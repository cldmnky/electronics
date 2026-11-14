"""Write models.json, with each model's place in the panel stack.

Why this is not just a file list: every `tsci export -f step` board comes out in
its OWN frame - x/y are panel-aligned, but z is measured from that board's
mid-plane with +z pointing out of the board's parts side. Loading them all at
their own origin piles six solids on top of each other, which is what the viewer
used to do. Each model now carries a scene transform:

    scaleY = -1           invert z, because +z in the export points at the panel
                          front while the panel frame counts z rearward
    positionY = T + 0.8   puts the board's parts-side face at panel z = T

`T` is that board's TOP face in the panel frame, straight out of
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

# model stem -> (panel-frame TOP face of that board, shown on load)
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
            # the panel: invert z only (scaleY), which keeps board x/y intact.
            entry["scaleY"] = -1
            entry["align"] = {
                "x": layout["display"]["x"],
                "y": layout["display"]["y"],
                "frontSceneY": z["displayGlassFront"],
                "depth": round(z["displayRearMost"] - z["displayGlassFront"], 3),
            }
        if top is not None:
            entry["scaleY"] = -1
            entry["positionY"] = round(top + half, 3)
            entry["panelTopZ"] = round(top, 3)
        models.append(entry)

carrier = {
    "bosses": layout["displayBossPattern"],
    "offsetX": layout["display"]["x"],
    "offsetY": layout["display"]["y"],
    "moduleRear": z["displayRearMost"],
    "rearBoardFront": z["rearBoardFront"],
}
out.write_text(json.dumps({"base": "/", "models": models, "carrier": carrier}, indent=2) + "\n", encoding="utf-8")
print(f"mechanical-view: {len(models)} STEP file(s) listed in {out}")
for m in models:
    if "positionY" in m:
        where = f"panel z {m['panelTopZ']:5.2f}..{m['panelTopZ'] + 2 * half:5.2f}  (scene y {m['positionY']:+6.2f})"
    else:
        where = "own frame, aligned on its loaded bbox, screen toward the panel"
    print(f"  {'shown ' if m['visible'] else 'hidden'} {m['label']:<46} {where}")

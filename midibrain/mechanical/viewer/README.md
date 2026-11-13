# Mechanical STEP viewer

A local web page that renders the midibrain mechanical STEP models, so the
panel/board/display stack can be looked at without FreeCAD.

```bash
make mechanical-view            # installs deps once, serves :8161, opens the browser
make mechanical-view PORT=9000  # another port
make mechanical-view NOOPEN=1   # serve only
```

Stop it with Ctrl-C. `serve.sh` regenerates `models.json` on every start (it is
gitignored) and lists every STEP it finds under:

- `mechanical/reference/*.step` — vendor reference models (Waveshare display)
- `mechanical/cad-out/*.step` — the composed assembly, if you ran `make mechanical-cad`
- `mechanical/viewer/models/` — anything you drop here

## Why this exists and what it is not

tscircuit's 3D view only draws parts that belong to a **board circuit**, and its
`cadModel` takes a URL. The panel, the display and the composed assembly are not
board parts, so they never appear there. This viewer reads the actual STEP files
with OpenCascade (`occt-import-js`) and `three.js`, offline after one
`npm install`.

It is a **viewing aid, not a fit approval**. The models are vendor CAD or
generated placeholders. No dimension here is a measurement of a delivered part.

## Controls

| Control | What it does |
| --- | --- |
| drag / wheel / right-drag | orbit / zoom / pan |
| **Z cut (mm)** | clips everything above that CAD z; drag it down to slice through the stack |
| **fit view** | re-fit the camera to the loaded meshes |
| **shaded / wireframe** | toggle |
| **show stack boxes** | overlay the documented stack as translucent boxes |

## The stack overlay

Boxes for the parts with no STEP, using the heights in
`tscircuit/lib/layout.ts` (`PROVISIONAL_Z`) and the jack STEP bounding boxes:

| Box | Size (x × y × z, mm) | z0 (panel front = 0) | Source |
| --- | --- | --- | --- |
| panel | 152.0 × 128.5 × 1.6 | 0.00 | `PANEL` (30HP, 152.0 actual) |
| front board | 128.0 × 112.0 × 1.6 | 17.45 | `FRONT_PCB`, `PROVISIONAL_Z.frontBoardTop` 19.05 |
| stereo jack (×4) | 10.0 × 12.73 × 15.5 | 0.00 | PJ366ST STEP bounding box |
| button + cap (×7) | 12.0 × 12.0 × 10.0 | 0.00 | Omron B32/B3F reference, 10.0 ±0.4 board→panel front |

Positions are the connector/button XY from `lib/layout.ts` at the time of
writing; if the layout moves, these do too — update them here or read them from
`mechanical/layout.json`.

## Two things the models themselves told us

1. **The Waveshare display STEP loads as 394 solids, bounding box
   106.1 × 68.363 × 13.820 mm** — which matches
   `mechanical/reference/waveshare-4.3inch-dsi-lcd.source.json` exactly, so the
   file is the model that source record describes.
2. **Its own axes are canvas-flat**: X = 106.1 (width), Y = 68.36 (height),
   Z = 13.82 (thickness). The viewer draws it in that vendor orientation at the
   origin — it is **not** stood up against the panel here. Standing it up and
   giving it a z is what `mechanical/step_assembly.py` does; use the composed
   assembly from `make mechanical-cad` for the real placement.

## Licensing

The vendor STEP is **not committed** (`midibrain/.gitignore` has `*.step`); only
`mechanical/reference/*.source.json` is tracked, with URL, retrieval date and
SHA-256 so the file can be re-fetched. Do not commit vendor CAD here without
checking its redistribution terms.

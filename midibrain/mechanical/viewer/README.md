# Mechanical STEP viewer

A local web page that renders the midibrain mechanical STEP models, so the
panel/board/display stack can be looked at without FreeCAD.

```bash
make mechanical-steps           # export every board + the stack as real STEP
make mechanical-view            # installs deps once, serves :8161, opens the browser
make mechanical-view PORT=9000  # another port
make mechanical-view NOOPEN=1   # serve only
```

`make mechanical-steps` runs `tsci export -f step` for each board circuit and
writes the results into `mechanical/viewer/models/` (gitignored - they are 0.03
to 13 MB of generated binary each):

| model | solids | envelope (mm) | what it is |
| --- | ---: | --- | --- |
| `panel.step` | 1 | 152.0 x 128.5 x 1.6 | the 30HP FR-4 panel, apertures and drills included |
| `front.step` | 85 | 150.0 x 112.0 x 37.7 | front/UI board **with every placed part** in its real Z |
| `rear.step` | 40 | 148.0 x 112.0 x 26.0 | rear/base board with its parts |
| `pi-panel.step` | 1 | 101.2 x 128.5 x 1.6 | the 20HP Pi mounting panel |
| `assembly.step` | 87 | 152.0 x 128.5 x 43.1 | the whole stack: front board at the Z = 0 anchor, panel and rear board as envelopes |

These are tscircuit's own board export, so the parts are the real footprints at
the real heights.

### How the models are placed

`serve_manifest.py` writes the placement, so nothing is eyeballed:

- Each board's STEP comes out in **its own frame**: x/y are panel-aligned, but z
  is measured from that board's mid-plane with +z pointing out of the board's
  parts side. Loaded at their own origins they piled up on each other - that is
  what the viewer used to do.
- So each model carries `scaleY = -1` and `positionY = T + 0.8`, where `T` is
  that board's TOP face in the panel frame, straight out of
  `mechanical/layout.json` `provisionalZ` (panel front 0, +z rearward). Result:
  panel 0.00…1.60, front board **10.60…12.20**, rear board **23.40…25.00**
  (owner-directed 10.6 mm panel front, provisional).
- The vendor display (Waveshare) is in its **own** frame and its STEP also
  carries non-solid construction geometry (raw points reach x = 106 where the
  solid envelope is 106.1 wide), so it cannot be placed by centre arithmetic.
  The viewer aligns it on the **loaded** bounding box: centred on
  `display.x/y` and with its screen on the panel-front plane, i.e. facing the
  panel. Which side is the screen is not guessed - it is read off the STEP's own
  products through OpenCascade: `BOARD` sits at z -3.34…-1.74 and the SMTSO
  bosses at -7.34…-1.84, while the IPS/touch panel reaches +4.8, so +z is the
  screen and z is inverted.
- The **display carrier and its four spacers** are drawn as a placeholder from
  `displayStandoff.spacerLength` between the module's rear plane and the rear
  board's front face, at the module's `displayBossPattern`. There is no STEP for
  the carrier yet (`display_boss_pattern` is open); the spacer and pad diameters
  are named placeholders, so read this as "where it bolts", not as a part.

### The Z cut

`Z cut (panel z, mm)` peels the stack at that panel z: everything with
`panel z <= cut` is kept. It starts at 100 (above the whole stack, so it looks
off). It previously clipped along the board's own y axis instead of the stack
direction, and started at -1, which hid most of the module.

The old `STACK_MM` placeholder boxes are gone: they were a sketch of the
**pre-respin** stack (128 mm board at z 17.45) and duplicated real geometry. They are
still a viewing aid, not a fit approval: tscircuit derives component Z from its
own CAD models, and a vendor model with a wrong origin shows up as a wrong
height here.

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
| front board | 150.0 × 112.0 × 1.6 | **10.60** | `FRONT_PCB`, `PROVISIONAL_Z.frontBoardTop` |
| rear board | 148.0 × 112.0 × 1.6 | **23.40** | `REAR_PCB`, `PROVISIONAL_Z.rearBoardFront` |
| stereo jack (×4) | 10.0 × 12.73 × 15.5 | 0.00 | PJ366ST STEP bounding box |
| button + cap (×7) | 12.0 × 12.0 × 10.0 | 0.00 | Omron B32/B3F reference box. **Verified**: `10.0 ±0.4` is PCB top → **cap top**, so a bought B32 top sits 0.60 mm behind our panel front; the printed cap's own height is `BUTTON_CAP_PRINTED` (4.70, top 0.80 proud) |

(The 128 mm / 17.45 mm figures that used to be here were the pre-respin stack;
`mechanical-steps` above is the accurate route now that the boards export.)

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

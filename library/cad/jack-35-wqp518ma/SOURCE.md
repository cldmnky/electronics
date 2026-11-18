# PJ301M-12.step — QingPu/Thonkiconn WQP518MA (mono)

## Source

- URL: https://github.com/clacktronics/AudioJacks/blob/14a88866e93b8ce4a31ad376b0c6eb85cd4d2cf3/AudioJacks.3dshapes/PJ301M-12.step
- Repository: https://github.com/clacktronics/AudioJacks (commit `14a88866e93b8ce4a31ad376b0c6eb85cd4d2cf3`)
- Retrieved: 2026-11-13
- File: `PJ301M-12.step`, 729350 bytes, SHA-256 `3ae3e409ef498c9f111f1d10e737c7df1e36df8bfff62e1f66482a8388211620`
- STEP header: `ISO-10303-21`, `FILE_DESCRIPTION(('FreeCAD Model'),'2;1')`,
  schema `AUTOMOTIVE_DESIGN`, generated 2021-03-19 with FreeCAD / Open CASCADE 7.3

## Licence

MIT — the repository's `LICENSE` at the pinned commit reads "MIT License,
Copyright (c) 2021 Barwise" (the Clacktronics AudioJacks library is published
under it).
Redistribution of the model is permitted with the licence notice.

## What was verified

- The file is a STEP (ISO-10303-21) part, not a mesh export.
- The repository's own footprint for the same part,
  `AudioJacks.pretty/Jack_3.5mm_QingPu_PJ301M-12_Vertical.kicad_mod` at the same
  commit, gives the pad line reproduced in the footprint used by the boards:
  **S 6.48 / R 3.38 / T 4.92 mm from the barrel datum** (mono).
- `DATA.md` in that repository classifies the part family and its mounting.

## 2026-11-17 footprint warning

The stored upstream footprint has **1.6 × 0.6mm oval slots**. The
[Thonk mono drawing](https://www.thonk.co.uk/wp-content/uploads/2014/02/Thonkiconn_Jack_Datasheet.pdf)
shows blades up to 1.5 × 0.6mm: the slot has no width clearance and its
rounded ends do not contain the blade corners. Do not use this footprint for
a new board. Froemix uses provisional 2.4 × 1.2mm slots and a barrel copper
keepout; actual blade dimensions, finished slots and fit still need physical
and CAM verification. The library CAD remains unchanged and this row stays
`draft`.

## What was NOT verified

- The upstream README says **"WARNING NOT YET COMPLETE, DO NOT USE"** and
  `DATA.md` marks every part's Testing Status as **"Untested"**. The model has
  therefore not been checked against a physical jack by its author, and not by
  us either.
- Origin, axis and units of the model were not re-measured; the board placement
  assumes the model's origin is the barrel/panel axis and that it is in mm. If
  a render shows the body offset or rotated, correct this note before trusting
  the 3D view — do not move the footprint to compensate.
- Body height, bushing length and nut geometry come from the model only; the
  panel plane and the 9 mm stack are not asserted by it.

## 2026-11-18 vendor drawing (the part actually ordered)

The assembled order uses Exploding Shed `100515` / `100515P020`
(<https://www.exploding-shed.com/thonkiconn-mono-wqp518ma/100515>). That page
carries QingPu's drawing, *Thonkiconn Mono (WQP518MA)*:

- **WQP518MA = the former PJ398SM and PJ301M-12**; "footprint and dimensions
  are the same", only the tab shape changed (rectangular now).
- **With switching function**: three terminals — sleeve, tip, tip switch.
- Body height 10 mm, thread 4.5 mm, panel hole 6 mm, M6 x 0.5.

Its "PCB LAYOUT (TOP VIEW)" is the same pad line as the stereo part: pins at
**4.92 / 3.38 / 6.48 mm** from the barrel datum (spacings 8.30 and 3.10), blades
**0.5 x 1.5 (pin 3, the switch), 0.6 x 1.5 (pin 2, tip), 0.6 x 1.3 (pin 1,
sleeve)**; the schematic marks pin 1 as the sleeve. So on this part the tip is
the middle pad and the switch occupies +4.92 mm.

Caveat: `PJ301M-12.step` in this directory is the *stereo* model reused — its
solids are named `T`, `R`, `S`, and it has no switch contact. It is usable for
the pad line, body envelope and barrel only; it does not prove the terminal
functions.

Froemix's mono footprint keeps pin1/2/3 labels with sleeve at +6.48 and tip at
−4.92 mm, i.e. the drawing mirrored: the part is mounted rotated 180° in plane.
The copper is consistent with the part in that orientation (the silk prints
S/T on the pads), but it is the opposite of the stereo jacks on the same board
and of the drawing a builder will have in hand. Normalise the two in the next
revision or keep the letters as the authority.

# PJ366ST.step — QingPu/Thonkiconn WQP419GR (stereo)

## Source

- URL: https://github.com/clacktronics/AudioJacks/blob/14a88866e93b8ce4a31ad376b0c6eb85cd4d2cf3/AudioJacks.3dshapes/PJ366ST.step
- Repository: https://github.com/clacktronics/AudioJacks (commit `14a88866e93b8ce4a31ad376b0c6eb85cd4d2cf3`)
- Retrieved: 2026-11-13
- File: `PJ366ST.step`, 825691 bytes, SHA-256 `63a4bfe339d28c0ac4aeeefacf8a0c48b1a846f8db7ec034be59c18128356ddc`
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
  `AudioJacks.pretty/Jack_3.5mm_QingPu_PJ366ST_Vertical.kicad_mod` at the same
  commit, gives the pad line reproduced in the footprint used by the boards:
  **S 6.48 / R 3.38 / T 4.92 mm from the barrel datum** (stereo).
- `DATA.md` in that repository classifies the part family and its mounting.

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

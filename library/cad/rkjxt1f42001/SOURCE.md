# sw-rkjxt1f42001 CAD provenance

- Part: ALPS Alpine RKJXT1F42001, 4-direction stick switch with centre push and
  encoder (THT, 11 holes: 10 electrical + 1 position lug)
- Datasheet: <https://mm.digikey.com/Volume0/opasdata/d220001/medias/docus/6727/rkjxt-1890304-1-4.pdf>
  (ALPS catalogue pages 398, 399, 391 and 412), retrieved 2026-11-14,
  SHA-256 `8b0484447b2209de2cc1e4becaa28984606efd472202a4d83d5936c62975cf0c`.
  Product page: <https://tech.alpsalpine.com/e/products/detail/RKJXT1F42001/>.
  The PDF itself is NOT committed (vendor document); the URL and hash are.
- Footprint: `cad/rkjxt1f42001/footprint.tsx`, AUTHORED here from the drawing.
  It is not copied from any vendor library. The same footprint is used by the
  `midibrain` board build at `midibrain/tscircuit/imports/RKJXT1F42001.tsx`,
  and `midibrain/tscircuit/lib/rkjxt-nav.test.ts` compares the pad coordinates
  of the two files so they cannot drift.
- STEP: `cad/rkjxt1f42001/model.step`, AUTHORED here by `model.py` from the
  drawing and licensed Apache-2.0 — see "STEP model (authored here)" below. No
  vendor CAD file is committed. The ALPS vendor STEP (single fused solid,
  `RKJXT1F42001.STEP` in the vendor zip), the owner-supplied alternate STEP (16
  solids) and a SamacSys model archive (`LIB_RKJXT1F42001.zip`) carry no
  redistribution grant. All three were used LOCALLY only, to cross-check pad
  positions and heights; none of their geometry is copied, converted or
  tessellated into ours. (The SamacSys `license.txt` forbids redistributing its
  models "in any form useable as a PCB Library Component".)
- License: the Apache-2.0 repository license covers both authored assets. The
  footprint and the STEP are authored from the manufacturer's published
  dimensions, so the geometry is ours, not the vendor's.
- Local comparison only: the authored `model.step` above is the default. The
  owner-supplied 16-solid alternate STEP remains an optional local override at
  `midibrain/mechanical/local-cad/rkjxt1f42001.step` (gitignored, SHA-256
  `24d6b9e11b8d8c59f97125d3e65a39cd0e1573fd43e1b9c091272566be1aeae1`, origin on
  the shaft axis at the seating plane, +z toward the panel). It is not part of
  this library and is never committed.
- SHA-256 of this footprint: compute with
  `shasum -a 256 library/cad/rkjxt1f42001/footprint.tsx`. It changes whenever the
  footprint is edited, so it is not repeated here.

## How the footprint was derived

Scale: the drawing is vector, so the hole pattern was read from the PDF's own
path data, not from a raster. One point on the page = 0.3997 mm (the printed
"15.6" dimension spans 39.024 pt across the outer hole centres). All ten
electrical holes plus the lug come out on the printed 0.4 mm grid:

| terminal (as printed) | x (mm) | y (mm) | hole |
| --- | --- | --- | --- |
| A | -1.50 | -7.80 | Ø1.0 plated |
| B | +7.80 | -1.50 | Ø1.0 plated |
| C | +1.50 | +7.80 | Ø1.0 plated |
| D | -7.80 | +1.50 | Ø1.0 plated |
| com | -1.00 | +5.78 | Ø1.0 plated |
| Push | +1.00 | -6.98 | Ø1.0 plated |
| Ground terminal | +6.86 | -3.75 | Ø1.0 plated |
| Encoder A | -7.80 | -1.50 | Ø1.0 plated |
| Encoder B | +7.80 | +1.50 | Ø1.0 plated |
| Encoder com | -1.50 | +7.80 | Ø1.0 plated |
| for position lug | -3.80 | -1.50 | Ø1.1 **not plated** |

Cross-checks (local only, not committed):

- Both STEP models put their terminal posts on exactly those ten positions to
  within 0.04 mm, and the lug at (-3.80, -1.50). The drawn pattern is therefore
  the physical pattern with NO mirroring, even though the drawing is headed
  "viewed from mounting side": the drawing view IS the view from the component
  side of the board, which is what a footprint is.
- The SamacSys KiCad footprint, read as a third opinion, has the same eleven
  positions with the file's Y axis negated (its own Y-down convention). Its
  symbol duplicates terminal D on the lug hole, so its pin *names* were NOT
  used.
- The direction-to-terminal mapping comes from the page-399 output-relation
  chart (columns A/B/C/D/Centre push) plus the page-398 lever-direction figure
  (A up, B left, C down, D right). Each direction's contact sits on the far
  side of its own lettered hole, which is what a lever on a fulcrum does and
  what the drawing draws.

## STEP model (authored here)

- File: `cad/rkjxt1f42001/model.step`, written by `cad/rkjxt1f42001/model.py`
  with CadQuery 2.8.0 (OCCT 7.9) in a private virtualenv — the same convention
  as `midibrain/docs/mechanical-cad.md`:
  ```bash
  /tmp/cq-venv/bin/python library/cad/rkjxt1f42001/model.py \
    --out library/cad/rkjxt1f42001/model.step --verify
  ```
- Source: the same ALPS drawing as the footprint (catalogue page 398). The
  generator builds the part from CadQuery primitives — boxes, cylinders and an
  octagon polygon — and reads no CAD file at all. The ALPS vendor STEP, the
  owner-supplied 16-solid alternate STEP and the SamacSys archive are not
  imported, converted or tessellated by it, and none of their geometry is in
  the committed file. The geometry is ours.
- License: Apache-2.0, the repository license. No part of the file is licensed
  by ALPS or by a CAD vendor.
- SHA-256: `953f3b17cc825ddeb99b76bea85de59662938b3658c8488cc2c6edc7bc49e2a1`
  (308 371 bytes; recompute with `shasum -a 256 library/cad/rkjxt1f42001/model.step`).
  The hash of `model.py` is not repeated here because it changes with the
  script.
- Frame: origin on the shaft axis at the PCB seating plane, +z toward the
  panel, +x/+y as in the printed hole table — identical to `footprint.tsx`.
  Units mm, one fused ISO-10303-21 solid, no external references. The export
  names the part `RKJXT1F42001.step` in `FILE_NAME` and pins the timestamp, so a
  re-run of the generator is byte-identical.

### Dimensions used

| feature | value | source |
| --- | --- | --- |
| lower housing | 17.0 x 17.0 mm in plan, z 0.0..3.0 | drawing "4-17", page 398 |
| upper body | regular octagon, 14.5 mm across the flats, z 3.0..8.6 | drawing "4-14.5" plus the 8.6 body top, page 398 |
| octagon corners | vertices at (±7.25, ±3.003) and (±3.003, ±7.25) | derived from the across-flats figure: 7.25·(√2−1) = 3.003 |
| collar top | z 10.5 | drawing "10.5", page 398 |
| shaft | Ø2.5, z 11.2..17.4 | drawing "oe2.5" and "17.4", page 398 |
| D-flat | 1.8 mm across; flat plane at y = +0.55, round side at y = −1.25 | drawing "1.8" with the 2.5 shaft: −1.25 + 1.8 = 0.55 |
| terminal blades | 0.7 x 0.25 mm, z −3.2..0.0 | drawing "flat blades 0.7 x 0.25" and "3.2", page 398 |
| terminal positions | the ten printed hole positions in the table above | page 398 |
| position lug | Ø1.1 at (−3.8, −1.5) | "for position lug" hole, page 398 (NPTH Ø1.1) |

### Model-derived (no printed value)

| feature | value | why |
| --- | --- | --- |
| lower-housing corner rounds | R1.0 | the drawing prints the square envelope only; the vendor lower frame is a 17.2 mm square with lead cut-outs |
| collar | Ø7.20, z 8.6..10.5, 0.2 mm top chamfer | page 398 prints no collar diameter; Ø7.20 is the 3.6 mm silkscreen circle of `footprint.tsx` |
| four housing tabs | 0.8 mm tangential x 1.3 mm radial x 0.4 mm high at (0, ±5.18) and (±5.18, 0), z 8.6..9.0 | position and size read from the local alternate STEP, simplified to boxes |
| shaft base neck | Ø1.8, z 10.5..11.2 | no printed neck diameter or height |
| tip chamfer | 0.2 mm around the D profile | no printed edge break |
| D-flat orientation | flat faces +y | the drawing prints the across-the-flat figure, not which side it faces; +y is what the local alternate STEP shows |
| D-flat extent | the whole main shaft, z 11.2..17.4 | no printed flat start; the local alternate STEP's flat starts between 12.0 and 12.5, so ours is longer |
| blade orientation | 0.25 mm radial, 0.7 mm tangential, one rule for all ten posts | the vendor rotates individual blades to suit its internal contacts; a single rule is reproducible and keeps every post inside the 17.0 mm envelope |
| position-lug depth | 1.2 mm below the seating plane | no printed value; the local alternate STEP's lug ends 1.2 mm below the plane |

### Simplified or not modelled

- The ten terminal posts are straight 0.7 x 0.25 mm prisms. The vendor's are
  kinked blades that reach the internal contacts, and its encoder pair also
  climbs the outside of the housing: that is why a z = 4.0 section of the vendor
  model measures 15.73 mm across while ours is the printed 14.5 mm octagon.
- No internal switch or encoder contacts, no springs, no frame lead cut-outs.
- The four tabs are boxes, not bent clips; their extent is close to, not equal
  to, the vendor's.
- The octagon corners are sharp. (The vendor body itself measures 14.45 mm
  across the flats, i.e. the same flats with a small corner radius; the printed
  14.5 mm is what is modelled.)
- The STEP is a body model for 3D viewing and panel/stack checks. It does not
  prove pin numbering, contact behaviour or soldering, which the drawing and
  the footprint own.

### Verification of the written STEP

`--verify` re-loads `model.step` and checks 29 conditions; all pass. Measured
values:

| check | measured | expected |
| --- | --- | --- |
| header / units | `ISO-10303-21;`, `SI_UNIT(.MILLI.,.METRE.)`, `END-ISO-10303-21;` | mm, no `EXTERNAL_REFERENCE` |
| solids | 1 solid, OCCT-valid, 102 faces | no nested assembly |
| overall bbox | x −8.500..+8.500, y −8.500..+8.500, z −3.200..+17.400 | 17.0 x 17.0, z −3.2..17.4 |
| z = 1.0 | x ±8.500, y ±8.500 | 17.0 mm lower housing |
| z = 4.0 | x ±7.250, y ±7.250 | 14.5 mm across flats |
| z = 9.5 | x ±3.600, y ±3.600 | collar Ø7.20 |
| z = 12.0 | x ±1.250, y −1.250..+0.550 | shaft Ø2.5 with the 1.8 mm D-flat |
| z = 17.3 | x ±1.150, y −1.150..+0.450 | inside Ø2.5, 0.2 mm tip chamfer |
| ten posts | a probe at each printed hole centre at z = −3.19 is solid | blades reach z = −3.2 |
| lug | a probe at (−3.8, −1.5) at z = −1.19 is solid | lug reaches z = −1.2 |
| four tabs | a probe at each tab centre at z = 8.8 is solid | tabs present, z 8.6..9.0 |

One-time cross-check against the local alternate STEP (sanity only — that file
is NOT a source and is not committed): vendor bbox 17.2 x 17.2 x
(−3.236..17.400) versus our 17.0 x 17.0 x (−3.200..17.400); vendor volume
1 679.6 mm³ in 16 solids versus our 1 950.2 mm³ in one solid, the difference
being the missing internal mechanism and the simplified straight posts. The
shaft, flat side and seating plane land on the same figures; the vendor collar
is slightly tapered (7.13 mm at z = 9.5) where ours is a Ø7.20 cylinder.

## Verified

- Hole count, positions, diameters: from the drawing's own vector geometry,
  cross-checked against two independent 3D models.
- Pin numbering: ALPS prints terminal NAMES, never numbers. The numbering
  `pin1..pin10` = A, B, C, D, com, Push, Ground terminal, Encoder A, Encoder B,
  Encoder com is this library's own, stated here so a future reader does not
  mistake it for a manufacturer number.
- Pad ring: 1.5 mm pad on a 1.0 mm hole (0.25 mm ring). The drawn terminals are
  flat blades 0.7 x 0.25 mm, so they fit a 1.0 mm hole.
- The lug is a separate mechanical feature: it is drawn as a non-plated Ø1.1
  hole with no net. It also keys the part in rotation; the hole pattern alone
  would not.
- Body: 17 x 17 mm, 14.5 mm across the flats (drawing "4-17", "4-14.5");
  courtyard 17.2 x 17.2 mm. The silk is drawn as four corner chamfers only,
  because the 1.5 mm pads at ±7.8 reach 8.55 mm and a full outline would sit on
  copper.

## Independent checks on this footprint

- **Reviewed 2026-11-14**: the footprint was checked by a second reviewer
  against the SamacSys KiCad file and the ALPS drawing — all **11 holes agree**
  (the SamacSys file negates Y, its own Y-down convention), and the lug is
  **NPTH Ø1.1 mm**, as authored here. The SamacSys file itself is not committed:
  its `license.txt` forbids redistribution "in any form useable as a PCB Library
  Component", and **this repository is PUBLIC**, so the SamacSys, ALPS vendor and
  owner-supplied STEP files all stay out of git.
- ALPS prints terminal NAMES and never pin numbers: the `pin1..pin10` order and
  the name-to-pad mapping in the table above are **ours**, stated here so no
  future reader mistakes them for a manufacturer number.

## NOT verified (why the row stays `draft`)

- The row's `step` and `cad_license` columns are still empty: the authored
  `model.step` is redistributable and its hash is recorded above, but wiring it
  into `library/components.csv` (and into the footprint's `cadModel`) is a
  separate edit that has not been made, so the row still has no reproducible
  3D asset from the library tool's point of view and stays `draft`. The model
  itself has not been checked against a physical switch either — see the STEP
  section above for what is model geometry.
- The collar diameter (Ø7.20) and the four housing tabs (9.00 mm) are model
  geometry: page 398 prints neither. They drive the panel opening
  (`PANEL_CUTOUTS.nav = 8.2`, provisional) and the zero-gap tab contact risk.
- The page-399 output-relation chart is NOT ambiguous about the centre push:
  the pair "Push-Com" is ON in EVERY lever position - the four direction
  columns and the Center push column alike - and that closure is what makes the
  directions work. Every tilt therefore grounds NAV_PRESS together with its
  direction line; the midibrain firmware decodes the five contacts as one group
  (`mb::NavPressGate`) so a tilt cannot select. The board ties both `com` and
  `Ground terminal` to GND, and a first-article meter check confirms the
  terminal identity (the pin numbers are this library's own).
- No shop column is filled. **DigiKey SKU `4809-RKJXT1F42001-ND`** was read from
  a Wayback capture of digikey.com product 19529127 (snapshot **2024-10-01**); the
  live site answers HTTP 403 from this environment, so it is an ARCHIVE FACT and
  not a live check, and it is recorded in the CSV row's `notes` rather than in
  `digikey_sku` because that column requires a price and the only figure in the
  capture is USD (8.45 at 1, 6.95 at 10, EX-VAT) while this library records
  DigiKey Sweden in SEK. **Electrokit**: no row found in its sitemaps.
  **Conrad**: Cloudflare blocks automated clients, so its article number must be
  copied by hand. A search result is not a SKU.
- The footprint has not been checked against a physical part: pad positions come
  from the drawing and two models, and the first article must confirm them with
  a meter and a test fit.

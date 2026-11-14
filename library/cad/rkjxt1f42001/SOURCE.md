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
- STEP: **none, and none may be committed.** The ALPS vendor STEP (single fused
  solid, `RKJXT1F42001.STEP` in the vendor zip) and the owner-supplied alternate
  STEP (16 solids) carry no redistribution grant. Both were used LOCALLY only,
  to cross-check pad positions and heights, and neither file is in this
  repository. A SamacSys model archive (`LIB_RKJXT1F42001.zip`) was also
  consulted locally; its `license.txt` forbids redistributing the models "in any
  form useable as a PCB Library Component", so no SamacSys file is committed and
  it was used only as a second opinion on the hole pattern.
- License: the footprint is authored from the manufacturer's published
  dimensions, so the geometry is ours, not the vendor's.
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

- No STEP model may be committed, so the library row has no `step` and cannot
  be promoted. The 3D check of body, origin and axis is therefore a local,
  one-off cross-check, not a reproducible asset.
- The collar diameter (Ø7.20) and the four housing tabs (9.00 mm) are model
  geometry: page 398 prints neither. They drive the panel opening
  (`PANEL_CUTOUTS.nav = 8.0`, provisional) and the zero-gap tab contact risk.
- The centre-push return terminal is NOT proven: the page-399 circuit diagram
  draws the push between "Push" and "Com", while the page-399 chart shows
  "Push-Com" on in every column. The midibrain board ties both `com` and
  `Ground terminal` to GND so the push closes under either reading, and records
  a first-article meter check. Nothing here says which reading is right.
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

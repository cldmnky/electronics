# STEP and printable mockups — verification model, not a drawing

The CAD model in `mechanical/step_assembly.py` exists to answer one question and
to make the answer visible: **which bodies does the stack actually contain, and
which of them can be proven to fit?** It is not a released drawing, not a fit
approval and not a measurement. Every physical acceptance item stays pending
(`hardware-acceptance.json`, `docs/acceptance.md`).

Coordinates come only from `mechanical/layout.json`, which
`tscircuit/scripts/export-mechanical.ts` generates from `tscircuit/lib/layout.ts`.
No CAD file restates a position, and nothing here changes a panel, connector,
mounting, control or board coordinate.

## What the model contains, and what is known about it

| Body | Geometry | What is certain |
|---|---|---|
| `PANEL_32HP_3U` | 162.56 × 128.5 × 1.6 mm; 20 apertures and 14 drills from `PANEL_APERTURES` / `PANEL_DRILLS`: LCD, three feedthroughs with six screw holes, six buttons, three LED light-pipe openings, encoder, POWER, NAV, four jacks, four standoffs, four rail holes | outlines, diameters and positions are canonical (the same arrays `panel.circuit.tsx` renders) |
| `FRONT_BOARD_PROVISIONAL_Z` | 128 × 112 × 1.6 mm at Z 19.05…20.65; top relief notch, 4 × Ø4.3 LCD boss holes, standoff, front↔rear and bracket mount holes | XY canonical; **Z is the `PROVISIONAL_Z` chain (assumptions A and B)** |
| `REAR_BOARD_PROVISIONAL_Z` | 101.6 × 112 × 1.6 mm at Z 40.65…42.25; front↔rear and Pi mount holes | XY canonical; Z adds the **candidate** 20 mm board-to-board gap |
| `DISPLAY_WAVESHARE_VENDOR_STEP` | the vendor STEP, SHA-256 verified, bounding-box centre on the module centre, bounding-box minimum Z on the panel front face | vendor geometry; model depth 13.82 mm against the stack's 14.05 mm vendor maximum |
| `DISPLAY_ENVELOPE_PROVISIONAL` | 106 × 68 mm, Z 0…14.05, drawn only when no verified vendor STEP is supplied | an envelope, not vendor geometry |
| `DISPLAY_FPC_CONNECTOR_PROVISIONAL` | 21.412 × 4.826 × 1.8 mm on the display PCB rear face at (38.84, 11.7) | body size from the STEP; placement from the drawing |
| `FEEDTHROUGH_*_FLANGE_PLAN_MARKER` | the Adafruit 908 flange outline, 38.30 × 11.90 mm, as a 0.2 mm marker on the panel rear face | flange size from the drawing; **no depth is published, so no flange volume exists** |
| `USB_C_CANDIDATE_FLANGE_NON_APPROVED` | opt-in only (`--usbc-candidate-envelope`): advertised 25 × 10.2 × 5 mm from a marketplace listing | NON-APPROVED advertisement, no pitch, no current rating, no PD result |

The panel carries every opening: 20 apertures (LCD, three feedthroughs, six
buttons, three LEDs, encoder, POWER, NAV, four jacks) and 14 drills (six
feedthrough screws, four standoffs, four rails). Both lists come from
`PANEL_APERTURES` / `PANEL_DRILLS` in `tscircuit/lib/layout.ts`, the same data
`panel.circuit.tsx` renders. `tscircuit/lib/panel-cutouts.test.ts` compares the
two element by element against the built `dist/panel/circuit.json`, so the CAD
panel cannot drift from the board. A before/after check of the whole built panel
(Gerbers and circuit JSON) is in the Tool section below.

## The collision rule

`mechanical/step_geometry.py` classifies two bodies as:

- **clear** — one documented axis proves a separation (the bodies cannot touch),
- **collision** — all three axes overlap with positive depth and no axis is a
  candidate value,
- **undetermined** — anything else: an axis with no value, an unbounded extent,
  or a result that depends on a candidate value. This is the fail-closed case.

A documented **XY overlap is a projection, not a collision**. The Adafruit 908
flange spans y 47.55…59.45 at its canonical centre while the front board's
relief notch starts at y = 48, so the projection overlaps board material by
0.45 mm (17.235 mm² at x = −5; 50.435 mm² at x = +35, which also overlaps
beside the notch). The flange's depth below the panel is not in the drawing, so
the flange-versus-board result is **UNDETERMINED**. There is no threshold in the
code that converts an unknown depth into a verdict, and a candidate Z volume
used for drawing can never produce a definitive result.

## Outputs

Default directory `mechanical/cad-out/`; override with `--out DIR` (or
`make mechanical-cad CAD_OUT=...`). The script refuses a path that is an
existing file, the filesystem root, the project root, or a directory holding
files it does not write — unless `--force` is passed. Nothing is ever deleted.

| File | Contents |
|---|---|
| `midibrain-step-assembly.step` | the named solids above, one named body per material/part |
| `mockup-panel.stl` | 1:1 panel mockup |
| `mockup-front-board.stl` | 1:1 front/UI board mockup |
| `mockup-rear-board.stl` | 1:1 rear/base board mockup |
| `scale-check-100mm.stl` | 100.00 × 10.00 × 3.00 mm printer scale bar |
| `light-pipe-reach-gauge.stl` | step gauge: 17.45 mm tall, 10.16 mm low step |
| `assumptions.json` | provenance, unknowns, pair verdicts, volumes, round-trip result |
| `print-checklist.md` | the physical print check |

`*.step` and `*.stl` are ignored by `midibrain/.gitignore`. These are generated
files; never commit them, and never call them a released drawing.

## Regenerating

CadQuery goes in a private environment; never install it into the system
Python, and never stub the OpenGL libraries it links against:

```sh
uv venv /tmp/cq-venv
uv pip install --python /tmp/cq-venv/bin/python cadquery
# If importing reports `libGL.so.1`, unpack the distribution packages into a
# local prefix and export LD_LIBRARY_PATH to it, then retry.
LD_LIBRARY_PATH=<prefix>/usr/lib64 /tmp/cq-venv/bin/python mechanical/step_assembly.py --out /tmp/midibrain-cad
```

The vendor display STEP is not in the checkout (third-party binary). Download
it, verify **both** hashes against
`mechanical/reference/waveshare-4.3inch-dsi-lcd.source.json`, then pass it:

```sh
curl -sSLO https://www.waveshare.net/w/upload/2/28/4.3inch_DSI_LCD_Drawing.zip
# option 1: the archive, checked for BOTH hashes (archive and STEP member)
LD_LIBRARY_PATH=<prefix>/usr/lib64 /tmp/cq-venv/bin/python mechanical/step_assembly.py \
  --display-archive 4.3inch_DSI_LCD_Drawing.zip --out /tmp/midibrain-cad
# option 2: an extracted STEP, checked for its own hash
LD_LIBRARY_PATH=<prefix>/usr/lib64 /tmp/cq-venv/bin/python mechanical/step_assembly.py \
  --display-step 4_3inch-dsi-lcd-V1.stp --out /tmp/midibrain-cad
```

`--display-archive` is the executable form of the source record: it computes the
SHA-256 of the ZIP, compares it with `archive_sha256`, then computes the SHA-256
of the named `.stp` member and compares it with `step_sha256`. Either mismatch
stops the build with that value in the message. `--display-step` checks the STEP
hash only. The extracted STEP goes to a private temporary directory, so the
third-party binary is never written into the tree.

A file that does not match is refused because the panel aperture was derived from
that model: a different model would change the geometry the aperture is checked
against. The verification result (both hashes, or "absent") is written into
`assumptions.json` as `display_step`.

Optional candidate envelope for the supplied, non-approved USB-C cable:

```sh
... step_assembly.py --usbc-candidate-envelope --out /tmp/midibrain-cad
```

It draws only the advertised flange (25 × 10.2 × 5 mm) as a candidate volume,
changes no panel cutout and produces no verdict. The 35 mm rear body is not
drawn: its cross-section is not stated. No 5 A, PD or pass-through claim exists.

## Tool: compare two builds, nothing may drift

`mechanical/tools/compare_panel_build.py` compares a build taken before a source
change with one taken after it, and refuses any difference it cannot explain. It
reads files only: no build, no `fab/`, no writes.

```sh
# before the change
bunx tsci export panel.circuit.tsx -f gerbers -o /tmp/before/panel.zip
(cd /tmp/before && unzip -q panel.zip -d gerbers)
cp tscircuit/dist/panel/circuit.json /tmp/before/
# ...make the change, rebuild, then take the same snapshot in /tmp/after
python3 mechanical/tools/compare_panel_build.py \
  --gerbers /tmp/before/gerbers /tmp/after/gerbers \
  --json /tmp/before/circuit.json /tmp/after/circuit.json
```

What it tolerates, and why:

- **Gerbers**: only the four exporter timestamp lines
  (`%TF.CreationDate`, `G04 Created by tscircuit (builder) date`,
  `; DRILL file {…} date`, `; #@! TF.CreationDate`). Two builds of unchanged
  sources differ in nothing else, which was verified by building twice and
  diffing. Every coordinate, aperture, drill hit and silk line is compared.
- **circuit JSON**: `source_filesystem_md5_hash` (a hash of the sources) and the
  random `*_warning_id` suffix are ignored, and warning elements are compared as
  a set because the builder emits them in a varying order. Every other element is
  compared in order, field by field, so a moved or reordered feature fails.

The consolidation of the panel apertures into `layout.ts` was checked this way:
all twelve panel files (nine Gerbers, the drill file, `bom.csv`,
`pick_and_place.csv`) are identical after timestamp normalisation, and the panel,
front and rear circuit JSON geometry is identical in order.

## Tests

`make mechanical` runs the pure tests, which need no CAD library:

- `mechanical/test_step_geometry.py` — the collision rule, the exact 0.45 mm
  projection and its mutations, coordinate-drift refusals, the output-directory
  guard, the assumptions document, the opt-in USB-C candidate.
- `mechanical/test_light_pipe_reach.py` — the interval computation, the
  unknown-reference-plane rules and the invalid inputs.
- `mechanical/test_compare_panel_build.py` — the before/after comparison tool,
  including the cases where it must report a difference.
- `tscircuit/lib/panel-cutouts.test.ts` (bun) — the canonical panel apertures and
  drills against the built `dist/panel/circuit.json`, element by element.

`mechanical/test_cad_roundtrip.py` re-imports the exports with the real kernel
(CadQuery for the STEP, VTK for the STL meshes) and skips with a stated reason
where no CAD library exists:

```sh
LD_LIBRARY_PATH=<prefix>/usr/lib64 MIDIBRAIN_DISPLAY_STEP=4_3inch-dsi-lcd-V1.stp \
  /tmp/cq-venv/bin/python -m unittest discover -s mechanical -p 'test_*.py'
```

## Light-pipe reach — an incomplete result, on purpose

`mechanical/light_pipe_reach.py` works with two signed reference offsets, because
the two planes a light pipe spans are not the panel and the board:

    Z = 0 at the panel FRONT face, +Z rearward
    panel rear face      P = 1.60 mm   (documented panel thickness)
    front-board top      B = 19.05 mm  (PROVISIONAL_Z, not measured)
    gap                  g = B - P = 17.45 mm
    pipe rear face       R = B + rear_reference_offset_mm    (signed, unknown)
    pipe front face      F = P + front_reference_offset_mm   (signed, unknown)
    required length      L = R - F = g + rear offset - front offset
    shortfall            s = L - pipe length

The front offset is the term a front flange or a front seat inside the panel
sets; the rear offset is the term the LED, a socket or a board hole sets. No
inequality between them is assumed, and nothing forces either one to zero.

The only recorded numbers are the 17.45 mm gap and the 10.16 mm candidate pipe.
Their difference, **17.45 − 10.16 = 7.29 mm, is arithmetic and nothing else**:
it is not a shortfall, not a bound and not a verdict, because both reference
planes are unmeasured. While any offset or tolerance is unknown the status is
`incomplete`, `required_length_mm` and `shortfall_mm` are `None`, and no bound of
any kind is reported. `mechanical/test_light_pipe_reach.py` checks that an
unknown front offset cannot produce a verdict and that no output uses
bound-claiming words.

The gauge prints the two documented numbers as heights (17.45 mm and 10.16 mm),
so a bench check can start from them. It is a scale reference, not a reach
measurement: the pipe's own reference planes must be measured first.

## Physical print checklist

1. Print at 1:1, 100 % scale, in millimetres. Do not let the slicer rescale.
2. Measure `scale-check-100mm.stl` with calipers: it must be 100.00 mm. A 1 %
   scale error is 1 mm on the panel and rejects the print.
3. Check the LCD aperture against the real module glass (105.42 × 67.07 mm) and
   offer the display to it from behind: 0.9 mm clearance per side. Test the
   button, LED, jack, encoder, NAV and POWER openings against the real parts:
   the mockup carries all 34 panel features, so every opening can be tried.
4. Fit the Adafruit 908 through its panel openings and measure the depth from
   the flange seat to the rear-most body point. That number is missing today.
5. Lay the front-board mockup over the real display bosses and confirm the four
   Ø4.3 LCD holes and the top relief notch against all three feedthrough bodies.
6. Check the panel-to-board gap with the reach gauge. 17.45 mm is provisional.
7. Record nothing as accepted. Enter measurements in `docs/evidence/` with a
   reviewer and a date; all 28 acceptance items stay pending until then.

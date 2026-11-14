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
| `PANEL_30HP_3U` | 152.0 × 128.5 × 1.6 mm; 20 apertures and **10 drills** from `PANEL_APERTURES` / `PANEL_DRILLS`: LCD, three feedthroughs with six screw holes, six buttons, three LED light-pipe openings, encoder, POWER, NAV, four jacks, four rail holes (no panel standoffs since 2026-11-13) | outlines, diameters and positions are canonical (the same arrays `panel.circuit.tsx` renders) |
| `FRONT_BOARD_PROVISIONAL_Z` | **150 × 112 × 1.6 mm at Z 10.60…12.20**; one U-shaped opening (the old notch merged into the display opening, through the top edge), the four front↔rear spacer holes; **no** display holes, no panel standoffs, no bracket screws | XY canonical; **Z is the `PROVISIONAL_Z` chain, derived from the owner-directed 10.6 mm `PANEL_TO_FRONT_BOARD_TOP`** |
| `REAR_BOARD_PROVISIONAL_Z` | **148 × 112 × 1.6 mm at Z 23.40…25.00**; the four front↔rear spacer holes and the four Ø2.7 display-carrier holes (`LCD_MOUNT_HOLES`), no Pi holes since 2026-11-12 | XY canonical; Z is the 11.20 mm direct-mate seat-to-seat gap on `PROVISIONAL_Z`, from the 10.60 mm board face |
| `DISPLAY_WAVESHARE_VENDOR_STEP` | the vendor STEP, SHA-256 verified, placed by a rigid 180° rotation so the glass front face is at Z = 0 and the electronics point rearward; X/Y from the outer SMT boss pattern centre. The rotation about **Y is the drawn candidate only** — the in-plane axis is pending | vendor geometry; model depth 13.82 mm is the model, not the 14.05 mm vendor maximum |
| `DISPLAY_ENVELOPE_PROVISIONAL` | 106 × 68 mm, Z 0…14.05, drawn only when no verified vendor STEP is supplied | an envelope, not vendor geometry |
| `MODEL_FIT_SPACER_PLACEHOLDER_NON_APPROVED_1..4` | four annuli (OD 5.0, ID 2.7 mm **assumed for illustration**) drawn across the gap between the drawn boss rear plane and the board face | **geometric placeholder only, and currently on the SUPERSEDED chain**: the carrier now sits on the rear board with a **9.35 mm** adjustable spacer (`DISPLAY_STANDOFF`, derived), and the drawn model gap is **0.36 mm**. The placeholders follow the DRAWN planes (recomputed on every build, not pre-respin), and their length, diameter, thread and assembly remain first-article tasks |
| `DISPLAY_CONNECTOR_PLANNING_MARKER` | 21.412 × 4.826 × 1.8 mm box at (38.84, 11.7) on the **provisional** design-chain plane Z 10.05, not the model's drawn PCB rear Z 6.24 | **legacy unverified planning data, not a verified connector.** The size has no reliable source: the model's 21.412 × 4.826 mm bodies are TFT1 flex bonds (`EXTRUDED-106739` / `EXTRUDED-107488`), not this connector. Position is an unverified drawing record; routing and fit are unknown |
| `FEEDTHROUGH_*_FLANGE_PLAN_MARKER` | the Adafruit 908 flange outline, 38.30 × 11.90 mm, as a 0.2 mm marker on the panel rear face | flange size from the drawing; **no depth is published, so no flange volume exists** |
| `USB_C_CANDIDATE_FLANGE_NON_APPROVED` | opt-in only (`--usbc-candidate-envelope`): advertised 25 × 10.2 × 5 mm from a marketplace listing | NON-APPROVED advertisement, no pitch, no current rating, no PD result |
| `PI_MOUNTING_PANEL_20HP_3U` | 101.2 × 128.5 × 1.6 mm, 4 × M2.5 Pi holes on the Pi 5 58 × 49 pattern (landscape, mirrored, pattern centre 10 mm toward +X) and 4 × Ø3.2 rail holes. Also a separate verification model, `midibrain-pi-panel.step` and `mockup-pi-panel.stl`, carrying the panel and a placeholder Pi board | geometry canonical via `PI_PANEL_*` in `lib/layout.ts`, guarded by `lib/pi-panel.test.ts`; `step_assembly.py` derives the Pi board's holes from the drawing and fails the build unless they coincide with the panel's holes. The Pi is not in the main-module assembly |

The panel carries every opening: 20 apertures (LCD, three feedthroughs, six
buttons, three LEDs, encoder, POWER, NAV, four jacks) and 10 drills (six
feedthrough screws, four rails). Both lists come from
`PANEL_APERTURES` / `PANEL_DRILLS` in `tscircuit/lib/layout.ts`, the same data
`panel.circuit.tsx` renders. `tscircuit/lib/panel-cutouts.test.ts` compares the
two element by element against the built `dist/panel/circuit.json`, so the CAD
panel cannot drift from the board. A before/after check of the whole built panel
(Gerbers and circuit JSON) is in the Tool section below.

## Display placement — glass datum, and what it does not prove

The vendor frame points the other way: its **+Z leaves the glass**, its +X is mirrored relative to the layout frame,
and only its +Y agrees with the layout frame. Placing the model by its bounding-box minimum — the earlier defect —
put the boss ends at Z ≈ 1.7 mm and the glass at Z ≈ 11.9 mm, i.e. the electronics toward the panel.

The placement is now a **rigid rotation plus translation**:

    rotation     180 deg about Y:  (x, y, z) -> (-x, y, -z)     determinant +1 (a rotation, not a mirror)
    translation  glass front face -> Z = 0; outer SMT boss pattern centre -> the module centre

**The in-plane axis is NOT settled (status PENDING).** Reversing Z with a rotation always mirrors one in-plane axis, so
the choice is X or Y. Both are recorded:

| Candidate | Mapping | Inner plain-hole pattern |
|---|---|---|
| about Y (**used for drawing**) | (x, y, z) → (−x, y, −z) | lands on the recorded `DISPLAY_STANDOFFS` |
| about X | (x, y, z) → (x, −y, −z) | does not |

Matching `DISPLAY_STANDOFFS` is **not independent proof**: the drawing shows readable rear-side silkscreen and may be
a **rear view**, so mapping drawing left/right straight into the front-view layout may itself mirror the pattern - and
`DISPLAY_STANDOFFS` carries that same no-flip premise. The match shows only that the drawn candidate is consistent
with the layout's own premise. The outer 99.30 × 61.80 mm boss pattern is symmetric, so it fixes the centre and cannot
decide the axis.

What would settle it: a real front/rear **labelled photograph** of the delivered module, or the physical part offered
to the front board. Until then no document and no test may call either axis proven.

Planes in the illustrative placement (Z = 0 is the panel front face, +Z rearward):

| Plane | Vendor z | Placed global z |
|---|---:|---:|
| Touch/frame forward face | +4.80 | **−1.90** |
| Glass front (datum) | +2.90 | **0.00** |
| Glass rear | 0.00 | 2.90 |
| PCB front | −1.74 | 4.64 |
| PCB rear | −3.34 | **6.24** |
| Boss ends | −7.34 | 10.24 |
| Rear-most modelled feature | −9.02 | **11.92** |

**Disclosure.** In this placement the touch/frame forward face sits **1.90 mm in front of the panel front face**
(global Z = −1.90 mm). It is NOT flush with the panel and NOT a fit: the real touch layer, frame relief, aperture
relief and seating are unmeasured. The model's frame is 106.10 × 67.80 mm against the recorded 107.22 × 68.87 mm
aperture; that arithmetic is illustrative and is not a clearance result.

**The model's own planes are separate from the two recorded chains.** Measured from the model: glass front → PCB rear
**6.24 mm**, PCB rear → boss ends **4.00 mm**, frame 1.90 mm ahead of the glass. Those are model planes. They are not
the vendor's stated **14.05 mm** maximum overall depth, and not the provisional **10.05 mm** glass-front-to-PCB-rear
design chain. No equation relates the three; the automated tests assert that 6.24 and 10.05 are not equated.

**Connector — identity not established.** An earlier revision of this file called the two 21.412 × 4.826 mm bodies
the DSI FPC connector and reported a ~24 mm disagreement with the drawing. **That was a misidentification and is
withdrawn.** Reading the STEP's XCAF label tree shows those two bodies are `EXTRUDED-106739` / `EXTRUDED-107488`
inside `TFT1_ASM` — TFT flex bonds, not a panel connector — and that they carry no connector-position evidence.

The body nearest the recorded connector position is a separate named part, `DISPLAY_CAM`, a direct child of
`4_3INCH-DSI-LCD-PCBA_ASM`: 4.822 × 22.400 × 5.680 mm at vendor x −51.975…−47.153, y −13.570…8.830,
z −9.020…−3.340. Under the drawn candidate its centre is (40.064, 11.000) mm, an offset of (+1.227, −0.700) mm
(1.413 mm) from the recorded (38.837, 11.700) mm.

That offset is an **observation, not a match**: the name says camera; the body is 22.400 mm long against the recorded
21.412 mm; a second small solid sits under the same label; and no body in the file is named as a DSI, FPC or 15-pin
connector. So this file asserts **no** connector identity, pin count, electrical pinout or physical side. Both the
recorded position and the rear/front frame mapping are **UNVERIFIED**, and the drawing's position and length
arithmetic (the long-axis wording is disputed) needs independent re-derivation. No board coordinate changed; the
first-article gate `display_fpc_idc` stays pending.

## LCD mount placeholders — geometry, not a part

Four bodies named `MODEL_FIT_SPACER_PLACEHOLDER_NON_APPROVED_1` … `_4` show where a
standoff would sit. They are drawn only when a verified vendor model is supplied,
because their span comes from two drawn planes:

    vendor SMT boss rear plane   Z 10.24   (model)
    front-board top face         Z 10.60   (owner-directed, PROVISIONAL)
    model gap                     0.36 mm  = 10.60 - 10.24

The 19.05 mm board face in the text above is superseded history: at the old datum
the drawn board OVERLAPPED the boss plane by 0.24 mm, and at 10.60 mm it clears it
by 0.36 mm. The display no longer hangs off the front board at all; the carrier
spacers span a DERIVED **9.35 mm** from the REAR board's front face
(`DISPLAY_STANDOFF`), and the **carrier geometry itself does not exist yet**.

| Number | Value | What it is |
|---|---:|---|
| model gap (drawn planes) | **0.36 mm** | 10.60 board face less the 10.24 boss rear plane |
| design-chain spacer | **9.35 mm** | 23.40 rear-board front less 10.05 display PCB rear, less the 4.00 boss projection |
| design-chain display-end plane | **1.25 mm** | 10.60 less 9.35: the chain plane and the drawn boss plane DISAGREE by 8.99 mm, and at this datum the chain plane lies inside the drawn module |
| current spacer | **9.35 mm** | rear-board front face to boss ends (`DISPLAY_STANDOFF.spacerLength`, derived) |
| current plane separation | **13.35 mm** | rear-board front face to display PCB rear (`planeSeparation`, derived) |

No document may call the superseded 8.81 mm model gap or 5.00 mm spacer a current or approved dimension.

The placeholders use an **assumed** OD 5.0 mm and ID 2.7 mm, illustration only, no
dimensional claim. OD exceeds the board's 4.3 mm LCD hole so a bearing annulus is
visible, and it fits inside the 5.5 mm boss body. Each placeholder is checked to
have **no positive-volume intrusion** against the display or the board: the
boolean overlap at the touching faces measures about 1e-5 mm³ and the limit is
1e-3 mm³, so a real penetration fails.

Not claimed: the correct spacer length, the standoff diameter, the glass seating
plane, the mounting thread and its engagement, the assembly order, and any
physical fit. Those are first-article tasks. In envelope mode (no verified vendor
STEP) the placeholders are **skipped and recorded as skipped**, because the
14.05 mm envelope maximum is not a boss plane.

`assumptions.json` records the mount block, the residual and a `body_legend` that
names every drawn body with its status.

## Board face convention — requested, not implemented

The separate request for both boards' CAD-bottom faces toward the panel is **not implemented**, and no PCB source, SMT
side or stackup is changed for it:

- the board models are plain slabs, so a flipped slab looks identical and proves nothing;
- the front/UI board puts its SMT parts and the control actuators on top today, and the rear/base board already puts
  its SMT parts on the bottom;
- turning either board over is a real PCB layer/side reassignment that moves every part, the silkscreen, the assembly
  drawing and the CPL side.

It needs an owner decision and a PCB revision. The model keeps the current orientation and makes no compliance claim;
`assumptions.json` records it under `board_face_convention`.

## The collision rule

`mechanical/step_geometry.py` classifies two bodies as:

- **clear** — one documented axis proves a separation (the bodies cannot touch),
- **collision** — all three axes overlap with positive depth and no axis is a
  candidate value,
- **undetermined** — anything else: an axis with no value, an unbounded extent,
  or a result that depends on a candidate value. This is the fail-closed case.

A documented **XY overlap is a projection, not a collision**. The Adafruit 908
flange spans y 47.55…59.45 and extends past the board's top edge; since the
display opening now runs through that edge over x −63.25…43.25, the flange no
longer projects over board material where it used to. The flange's depth below
the panel is not in the drawing, so the flange-versus-board result is
**UNDETERMINED**. There is no threshold in the code that converts an unknown
depth into a verdict, and a candidate Z volume used for drawing can never
produce a definitive result. The recorded 0.45 mm / 17.235 mm² / 50.435 mm²
figures were measured on the pre-respin notch.

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
| `light-pipe-reach-gauge.stl` | same-datum step gauge for the two NOMINAL planes, valid in EITHER order: tall 16.5 mm (pipe tip), low 10.60 mm (board face), step 5.9 mm, with the SIGNED board-minus-tip = −5.9 mm (the tip plane lies behind the board face). Not an LED gap and not a fit |
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
  a set because the builder emits them in a varying order. Every other field is
  compared in order, element by element.

**What the JSON comparison does and does not prove.** The builder renumbers
identifiers between runs, so a *top-level* `_id` value ending in `_<digits>` is
compared **by occurrence pattern**: each value becomes the index of its first
appearance in that document, per namespace. That absorbs a consistent
renumbering, and it does **not** establish link integrity. Three limits are
stated in the tool's docstring and each has its own regression test:

1. a token used **once** can be relabelled freely, so a changed **singleton**
   reference is not guaranteed to be detected - even a dangling one;
2. any value ending in `_<digits>` is one token, so changing only the trailing
   digits of a **compound** value such as `schematic_port_3-schematic_port_11`
   is absorbed;
3. **nested** dicts and arrays are compared **raw**, so a consistent renumbering
   inside one is a *false failure* (never a false pass).

The comparison is a document check: geometry, element order and identifier
pattern. It is **not a netlist check**.

The consolidation of the panel apertures into `layout.ts` was checked this way:
all twelve panel files (nine Gerbers, the drill file, `bom.csv`,
`pick_and_place.csv`) are identical after timestamp normalisation, and the panel,
front and rear circuit JSON geometry is identical in order.

## Tests

`make mechanical` runs the geometry tests. It needs **bun** for the panel silk
check (that test exports and parses the real Legend Gerber and FAILS, never
skips, when bun or `tscircuit/node_modules` is missing). It needs **no CAD
library** for any of them:

- `mechanical/test_step_geometry.py` — the collision rule, the exact 0.45 mm
  projection and its mutations, coordinate-drift refusals, the output-directory
  guard, the assumptions document, the opt-in USB-C candidate.
- `mechanical/test_light_pipe_reach.py` — the interval computation, the
  unknown-reference-plane rules and the invalid inputs.
- `mechanical/test_compare_panel_build.py` — the before/after comparison tool,
  including the cases where it must report a difference.
- `tscircuit/lib/panel-cutouts.test.ts` (bun) — the canonical panel apertures and
  drills against the built `dist/panel/circuit.json`, element by element.
- `mechanical/test_panel_silk_gerber.py` — the emitted `F_SilkScreen.gbr` against
  every panel opening and drill (parser, geometry, zip safety, and the real
  export with its PLAY START mutation). Pure parser/geometry/zip tests only:
  `python3 -m unittest mechanical.test_panel_silk_gerber.GerberParserTest
  mechanical.test_panel_silk_gerber.ClearanceGeometryTest
  mechanical.test_panel_silk_gerber.ZipSafetyTest`
- `mechanical/test_cad_roundtrip.py` — with a real kernel and the vendor model:
  the display placement (glass datum, plane order, rotation not mirror, mounting
  centres, boards unchanged), the archive and STEP hash refusals, and the exports
  re-imported at 1:1.

`mechanical/test_cad_roundtrip.py` re-imports the exports with the real kernel
(CadQuery for the STEP, VTK for the STL meshes) and skips with a stated reason
where no CAD library exists:

```sh
LD_LIBRARY_PATH=<prefix>/usr/lib64 MIDIBRAIN_DISPLAY_STEP=4_3inch-dsi-lcd-V1.stp \
  /tmp/cq-venv/bin/python -m unittest discover -s mechanical -p 'test_*.py'
```

## Light-pipe reach — one datum, optical gap unknown

`mechanical/light_pipe_reach.py` uses ONE datum — the panel FRONT face, Z = 0,
+Z rearward — and the candidate's own dimensions:

    panel front face        Z = 0                              datum
    head underside          H = 0 + head_underside_offset      ASSUMPTION: 0
    pipe tip                T = H + shaft_length               nominal 16.5
    board near face         B = 10.00                          provisional (PANEL_TO_FRONT_BOARD_TOP)
    LED top                 L = B - led_height                 the LED rises
                                                              TOWARD the panel,
                                                              so it SUBTRACTS
    optical gap             g = L - T                          None (unknown)
    nominal tip to board    t = B - T                          nominal -6.50 (the tip PASSES the board: the pipe is too long)

The candidate is the owner-selected **Signal Construct PGN25L165**: shaft Ø2.5
±0.05 mm, x = 16.5 ±0.1 mm head-underside-to-tip, head Ø3 ±0.05 mm, head height
1.0 mm, mounting hole Ø2.6 −0.05 mm (2.55-2.60). Its source is pinned in
`mechanical/reference/pgn25l165.source.json` (URL and SHA-256; no third-party PDF
is committed). The owner's screenshot states 17.5 mm overall, which is 16.5 + 1.0.

Two things this section deliberately does not do:

- it does not turn 16.5 or 17.5 into a subtraction against a stack: the retired
  Bivar PLP2-400-C 10.16 mm reference and the `17.45 − 10.16 = 7.29 mm` figure
  are **withdrawn** (they compared different reference planes and the 10.16 mm
  value is not in the current Bivar datasheet);
- it does not call the resulting number an LED gap. The optical gap needs the LED height,
  the seated plane, the stack tolerances, retention in 1.6 mm FR-4 and the real
  hole tolerance; all are unmeasured, so `optical_gap_mm` is `None` and the
  status is `incomplete`.

The gauge is generated by `reach_gauge_dimensions()` and is valid for **both**
plane orders: the tall bar reaches `max(tip, board)` from the panel FRONT face,
the short one `min(tip, board)`, so the step is always a magnitude and the
SIGNED distance keeps its direction. For the current stack that is tall 16.5 mm
(pipe tip), low 10.60 mm (board face), step 5.9 mm and
`nominalTipToBoardPlane = 10.60 - 16.5 = -5.90` mm: the tip plane lies BEHIND the
board face, so the pipe would cross the front board under the two assumed planes.
That is arithmetic, not a measured interference. The earlier version raised
whenever the pipe was not in front of the board, which rejected the only case this
stack has. No step here is an LED gap, a fit or a measured stack value.

## The vendor display STEP is not tracked, and both CAD modes are supported

`mechanical/reference/waveshare-4.3inch-dsi-lcd.step` is third-party binary CAD and
is **NOT committed** (it may not be redistributed). The builder therefore supports
two honest modes, and the tests assert whichever one actually happened:

* **vendor source absent** (the default on a clean checkout: no file, and neither
  `MIDIBRAIN_DISPLAY_ARCHIVE` nor `MIDIBRAIN_DISPLAY_STEP` set): `display_step.status`
  is `absent` and the display is drawn as `DISPLAY_ENVELOPE_PROVISIONAL`
  (106 x 68 x 14.05 mm), explicitly "not vendor geometry". The placement-specific
  tests (drawn boss planes, spacer placeholders, orientation) are SKIPPED, because
  there is no vendor geometry to measure.
* **verified vendor source present** (`MIDIBRAIN_DISPLAY_ARCHIVE=…` or
  `MIDIBRAIN_DISPLAY_STEP=…`, or a verified default file): the archive and/or STEP
  SHA-256 must match the recorded source, `status` is `verified`, and every
  placement test runs with 0 skips.

A file whose hash does NOT match is refused in both modes, and that refusal is
tested separately. Never commit the vendor CAD to make a test run.

Run both:

```bash
env -u MIDIBRAIN_DISPLAY_ARCHIVE -u MIDIBRAIN_DISPLAY_STEP \
  python -m unittest discover -s mechanical -p 'test_*.py'   # 296 tests, supported skips
MIDIBRAIN_DISPLAY_ARCHIVE=/path/drawing.zip \
  python -m unittest discover -s mechanical -p 'test_*.py'   # 296 tests, 0 skips
```

## Physical print checklist

1. Print at 1:1, 100 % scale, in millimetres. Do not let the slicer rescale.
2. Measure `scale-check-100mm.stl` with calipers: it must be 100.00 mm. A 1 %
   scale error is 1 mm on the panel and rejects the print.
3. Check the LCD aperture against the real module glass (105.42 × 67.07 mm) and
   offer the display to it from behind: 0.9 mm clearance per side. Test the
   button, LED, jack, encoder, NAV and POWER openings against the real parts:
   the mockup carries every panel feature, so every opening can be tried.
4. Fit the Adafruit 908 through its panel openings and measure the depth from
   the flange seat to the rear-most body point. That number is missing today.
5. Offer the front-board mockup to the real display module: the module must
   pass through the 106.50 × 68.90 mm opening, and the four display-carrier
   holes are on the REAR board (not the front board).
6. Check the panel-to-board gap with the reach gauge: 9.0 mm panel rear to
   board top is provisional, and the gauge shows both nominal planes (16.5 tip
   and 10.6 board) with the signed direction.
7. Record nothing as accepted. Enter measurements in `docs/evidence/` with a
   reviewer and a date; all 31 acceptance items stay pending until then.

## Looking at the models

`make mechanical-view` serves `mechanical/` to a local STEP viewer
(`mechanical/viewer/`, three.js + OpenCascade via `occt-import-js`, dependencies
from npm, offline after the first install). It loads the vendor display STEP and,
when present, the composed assembly from `make mechanical-cad`; a "stack boxes"
overlay draws the panel, front board, jacks and buttons from the dimensions in
`tscircuit/lib/layout.ts`. Reads only - it can never change or approve anything,
and its own README records the display model's vendor axis orientation.

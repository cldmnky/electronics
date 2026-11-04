# Mechanical verification record

Branch `feat/midibrain-hardware-mechanical`. This file records what was actually run, what it proves, and what is
still open. **Nothing here is a physical fit approval.** The boards and the printed bracket remain prototype
geometry; no fabrication, print or purchase is released by this document.

Coordinates are millimetres with panel centre (0, 0), +X right, +Y up, and Z = 0 at the front (operator) face of the
panel with +Z rearward. `tscircuit/lib/layout.ts` is the single coordinate source.

## 1. Commands and results

Run from `midibrain/` unless stated. `PATH=/workspace/home/.bun/bin:$PATH` for Bun.

| Command | Result | What it proves |
|---|---|---|
| `cd tscircuit && bun run typecheck` | clean (`tsc --noEmit`) | Types of the layout, panel circuit, placement check and export script. |
| `cd tscircuit && bun run scripts/export-mechanical.ts` | writes `mechanical/layout.json` | The CAD bridge is generated from the canonical layout; the script writes no other file. |
| `cd tscircuit && bun run scripts/export-mechanical.ts --check` | "is up to date", exit 0, file hash unchanged | The tracked JSON matches the layout and `--check` writes nothing. A stale or missing file exits 1; an unknown argument exits 2. |
| `cd tscircuit && bun test lib/layout.test.ts lib/panel-art.test.ts` | 31 pass, 0 fail | Geometry rules, JSON staleness, `--check` modes, provisional-stack invariants. |
| `cd tscircuit && bun run build` | 3 circuits built | Panel/front/rear build with the new rounded-polygon aperture. |
| `cd tscircuit && bun run placement` | 230 pass, 0 fail | Built circuit JSON matches the shared coordinates, including the measured aperture-corner clearance. |
| `python3 -m unittest discover -s mechanical -p 'test_*.py'` | 49 tests, OK | Pure bracket/aperture geometry, plus regression tests that reject the reviewed defects. |
| `/tmp/cq-venv/bin/python mechanical/feedthrough_bracket.py` | 11 cuts, 1 solid, 3790.1 mm³, self-check pass, STL/STEP written (genuine libGL, CadQuery 2.8.0) | The CadQuery solid is buildable and every intended cut exists. |
| `git status --short` | only owned files touched | No change outside the assigned files (see §7). |

CadQuery setup is the standard one, in a local environment (never in the system Python):

```
uv venv /tmp/cq-venv
uv pip install --python /tmp/cq-venv/bin/python cadquery
/tmp/cq-venv/bin/python mechanical/feedthrough_bracket.py
```

CadQuery requires the genuine platform OpenGL libraries (`libGL`, `libGLX`, `libOpenGL`, `libGLdispatch`): the
OpenCascade kernel links against them even for a geometry-only run. Install them as distribution packages, or unpack
them into a local prefix and set `LD_LIBRARY_PATH` to it. **Do not substitute a stub library.** A no-op `libGL` hides a
broken environment instead of fixing it, and it is not a project instruction.

Recorded result with the genuine kernel (CadQuery 2.8.0, Debian bookworm `libGL` family unpacked under
`/tmp/cq-gl-prefix` and used through `LD_LIBRARY_PATH`):

```
LD_LIBRARY_PATH=/tmp/cq-gl-prefix/usr/lib/x86_64-linux-gnu \
  /tmp/cq-venv/bin/python mechanical/feedthrough_bracket.py
# 11 cuts, 1 solid, 3790.1 mm3, self-check pass, STL/STEP written
```

STL/STEP are generated, reproducible and stay untracked (`.gitignore`: `*.stl`, `*.step`).

### Gate commands for the build pipeline

The gate should call these four in order. Each is cheap and none needs CadQuery except the last:

```
cd midibrain/tscircuit && bun run scripts/export-mechanical.ts --check   # verify only; writes nothing
cd midibrain/tscircuit && bun test lib/layout.test.ts                   # staleness + geometry rules
cd midibrain && python3 -m unittest discover -s mechanical -p 'test_*.py'
cd midibrain/tscircuit && bun run build && bun run placement     # built panel: aperture and placement
```

Optional, needs CadQuery: `cd midibrain && python3 mechanical/feedthrough_bracket.py`.

`mechanical/layout.json` is generated output. `layout.test.ts` compares it with `mechanicalExport()` and fails when it
is stale, so the data cannot drift in one direction only: the generated file is never used to overwrite a layout
source.

The gate must use `--check`, not the generator. The generator (no argument) writes the file, so a gate that runs it
would repair a stale tracked file and then pass with an uncommitted change. `--check` never writes: it exits 1 for a
stale or missing file and exits 2 for an unknown argument. Tests for all four cases (fresh, stale, missing, unknown
argument) are in `lib/layout.test.ts`, and each one proves the file content is unchanged afterwards.

## 1a. Primary evidence for the display

Two manufacturer files were read for this pass, and both were measured rather than quoted:

- 2D drawing: `3.5_inch_DSI_LCD_H-2D-PDF.pdf` (`files.waveshare.com/wiki/3.5inch_DSI_LCD_H`).
- 3D model: `3.5_inch_DSI_LCD_H_3D-STP.zip` → `3.5 inch DSI LCD (H)_3d-20251129.stp`, imported with CadQuery 2.8.0.

Measured in the raw model frame (z = 0 at the lens front face, −z rearward):

| Feature | Measurement |
|---|---|
| Lens front plane z = 0 | x −44.435…+44.435 (88.870), y ±26.280 (52.560) |
| PCB rear plane z = −6.7 | x −43.380…+43.120 (86.500, centre −0.130), y ±28.000 (56.000) |
| Mounting bosses (4) | body r2.75 with an r1.25 M2.5 thread, z −6.70…−10.70, centres x −39.880/+18.120, y ±24.500 |
| Model bounding box | x −44.435…44.635, y ±28.000, z −10.700…0.000 |

Consequences now in the canonical data:

- Lens and PCB are **separate envelopes and are not concentric**: the PCB centre is 0.13 mm off the lens centre.
- The 4.0 mm behind the PCB rear face is the **mounting-boss envelope**, which is the rear-most feature. It is not a
  connector-only figure. A spacer screws onto the boss ends.
- **Plane separation and spacer length are different numbers.** Plane separation 9.0 mm (front-board top face to display
  PCB rear face, still provisional) needs a **5.0 mm spacer**, because the bosses already reach 4.0 mm below that plane.
  A 9 mm spacer would add the boss projection again and push the display through the panel.
- **Orientation:** the raw model is rotated 180° about Z so the 25 mm PCB margin falls at front-view LEFT. The bosses then
  sit at x −18.12 / +39.88, the PCB centre at +0.13, and the active area at +2.905 mm. The earlier hole offsets
  (−18.25/+39.75) omitted the 0.13 mm offset, and the earlier active sign (−2.905) was the raw-frame sign. Both are
  corrected; glass centre (−17, 11) and all control centres are unchanged.
- The model reaches x 44.635, i.e. 0.200 mm past the lens OD, inside the panel thickness zone. The 0.9 mm per-side
  aperture allowance still leaves about 0.7 mm there.
- **Physical first article is still pending** for the hole pattern, the active-area position and the boss thread.

## 2. LCD aperture

| Property | Withdrawn | Now | Note |
|---|---:|---:|---|
| Size | 89.2 × 56.3 | **90.67 × 54.36** | Vendor **LENS OD** 88.87 × 52.56 + 0.9 mm per side. The 86.5 × 56.0 PCB never passes the panel, so the PCB height must not size the aperture. |
| Clearance per side | 0.165 / 0.15 | **0.90** | 0.2 mm JLCPCB profile tolerance + 0.2 mm vendor LENS OD tolerance (all on one side) + 0.5 mm unverified registration/assembly allowance. |
| Corner relief | sharp rect | **1.0 mm radius**, explicit | Routed inside corners cannot be sharp. |
| Corner clearance to a square module corner | 0.165 / 0.15 | **0.851 measured** | Measured on the built panel polygon, not on a formula. |

The aperture was **expanded**, not rounded inward. The polygon chords lie inside the true corner arc, so the emitted
aperture is marginally smaller than the exact rounded rectangle: the 0.851 mm figure is the measured segment distance,
which is the case that matters. `lib/layout.test.ts` and `mechanical/test_bracket_geometry.py` both measure it, and
`scripts/check_placement.ts` re-measures it on the built panel.

## 3. Feedthrough bracket

| Reviewed defect | Status now |
|---|---|
| Python hardcoded slots −48/−13/+22 and mounts −61.5/40 while the boards drilled −38/−5/+35 and −60/55 | Fixed. Every position comes from `mechanical/layout.json`; nothing is parsed or repeated. |
| Left mount cut outside the plate (plate ended at x = +45) | Fixed. Outline is derived: x −63…58, 121 × 14.5 × 3 mm, spanning both mounts with a 3.0 mm nut wall (1.65 mm ligament outside a Ø2.7 hole). |
| 20/40/40 mm slots at 40 mm pitch touched, leaving a 0 mm web between the two USB-A slots | Fixed. Slots are sized from the vendor panel cutouts plus 1.5 mm per side: 15.6 / 17.9 / 17.9 mm. Webs are 16.25 mm and 22.10 mm. |
| No clearance for the top front↔rear spacer hardware at (±44, 43) | Fixed in the model: Ø6.5 head-clearance slots at both positions, which break out of the bottom edge. The head envelope itself is still an assumption. |
| Slot height not checked: a fixed floor at y = 50 cut into the declared panel openings (USB-C reaches 49.7, USB-A 49.85) | Fixed. Each floor is derived from its own feedthrough's declared opening bottom less 1.5 mm: 48.20 / 48.35 / 48.35, all above the notch bottom (48.0). Width and height are both tested, and the CAD self-check probes the declared opening bottom, its centre and the slot top. |
| No geometry tests, no CAD self-check | Added: 38 pure-geometry tests plus the CadQuery self-check in §1. |
| Stale STL/STEP | Regenerated from the current data for verification; not committed. |

The self-check tests four things on the solid: every cut removes material from the untracked plate (the feature exists
where intended), no material is left in any cut region, the part is one solid, and material exists on a ring around
each mount plus an open top, an unblocked declared opening and a closed floor at each slot. Slot width and floor use the
vendor **panel openings only**; the rear body, flange and cable-bundle envelopes are unmeasured and stay pending.

## 4. Provisional stack, depth and case verdict

- Glass and PCB envelopes are now separate in the canonical data: LENS OD 88.87 × 52.56 ±0.2, LENS VA 75.8 × 45.36
  ±0.2, PCB 86.5 × 56.0. The active-area offset is **+2.905 mm** in the layout frame (the −2.905 of the raw model frame
  after a 180° rotation); only the front-board silkscreen window uses it. Mounting holes are the bosses at
  x −18.12/+39.88, y ±24.50, i.e. the 0.13 mm PCB-to-lens offset is now included.
- One display interface definition, split into two quantities: **plane separation 9.0 mm** (front-board TOP face to
  display PCB REAR face, provisional) and **spacer length 5.0 mm** (plane separation − 4.0 mm boss projection). It
  replaces the contradictory `3.5 mm` (layout) and `8–10 mm` (prose) pair; the separate 3 mm spacer is gone.
- Z chain from the vendor side view (corrected): lens front 0.00 → display PCB rear 6.70 → connector rear-most 10.70;
  front-board top 15.70; front-board rear 17.30. Assumptions, not measurements: lens front flush with the panel front
  face, and the 9.0 mm standoff. The panel-to-board gap (14.10 mm) follows from them.
- Display underside: 5.00 mm available, tallest known front-side part 2.20 mm (Toshiba 11-4L1S MIDI receiver, max
  installed 2.1 ± 0.1 mm, p. 15), i.e. 2.80 mm over that part. That does **not** prove the display underside: the
  component map, FPC latch and module tolerance are unmapped. Open.
- Light pipe: 10.16 mm pipe against a 14.10 mm gap; 3.94 mm is unmeasured and unexplained, so a longer pipe is likely.
  Open.
- Depth: 72.9 mm from the panel front face at the high end of every candidate, inside the owner's ≤110 mm target on
  paper only.
- Eurorack Go (40 mm lower row, 62 mm upper row): the stack is ≈59–71 mm behind the panel back. **No approval.**

## 5. Open physical gates (no fake pass)

Each item needs a measurement or a sample, not an estimate. None of them is closed by this pass.

| Gate | What is still unproven |
|---|---|
| Display underside / FPC map | The display's rear component map, the 15-pin FPC latch and cable bend against the front-board top SMD (tallest known 2.2 mm in a 5.00 mm gap) and against the (−44, 43) spacer head. |
| Light pipe | 10.16 mm pipe against a 14.10 mm gap: reach, seating and the likely need for a different part. |
| Controls reach | Encoder shaft/knob, NAV cap, button actuators and jack bushings through 1.6 mm FR-4 plus the panel-to-board gap; that the reach matches each purchased part. |
| Jack bushing/nut retention | PJ-301M-3-B barrel and nut clamping on a 1.6 mm panel, the 6.2 mm hole and the 4 mm jack pitch. |
| M2.5 head/washer envelope | The Ø6.5 bracket head clearance and the 1.65 mm ligaments are assumptions; measure the screws, nuts and washers. |
| Printed hole fit | Ø2.7 printed holes are the board clearance diameter, not a verified FDM fit for an M2.5 screw. |
| Pi underside microSD | The Pi 5 microSD card over the R13–R18/Q3 zone below the Pi PCB at the current spacer height. |
| Cooler / M.2 HAT+ / SSD | Real stack height, SSD size, airflow and thermal behaviour; the 20–30 mm figure is a candidate range. |
| Cable bends | DSI cable, GPIO ribbon, J5 2×13 IDC plug, Pico USB plug, panel feedthrough tails. |
| Feedthrough flange vs front board | Adafruit 908 flange spans y 47.55…59.45 against a notch of y 48…56: 0.45 mm overlap with board material. |
| USB-C current path | The 3 A feedthrough cannot meet a 5 A requirement; part choice and a current/PD test are pending. |
| Depth and case | No cooler/HAT/SSD model, no service access, and the Eurorack Go is not approved for this stack. |

## 6. Process notes

- **Encoder (Bourns PEC11R-4215F-S0024):** the manufacturer does not recommend hand soldering. It needs a qualified
  soldering process (controlled temperature, preheat, limited dwell) or factory placement. "THT, hand-solder" is not
  by itself an acceptable process for this part.
- **USB-C panel feedthrough:** the existing candidate SparkFun CAB-15455 is rated **3 A, not 5 A**. No 5 A rating was
  invented for it and the owner's 5 A-capable requirement was not changed. The part choice is pending. Stop buying the
  3 A candidate as the power feedthrough until a documented 5 A part that fits, or an explicit owner decision, exists.

## 7. Requested changes in files owned by other coders

Not edited here, by instruction. Requested by the mechanical pass:

1. `Makefile` — add the mechanical checks to `make test`:
   `cd midibrain && python3 -m unittest discover -s mechanical -p 'test_*.py'`, and, before the Bun tests,
   `cd midibrain/tscircuit && bun run scripts/export-mechanical.ts --check` (verify only, never generate).
2. `.gitignore` — the comment "the current bracket STL/STEP are stale vs layout" is no longer true: the binaries are
   reproducible from `mechanical/layout.json` and the CAD self-check gates them. Suggest rewording to "generated;
   reproducible; not committed".
3. `docs/assembly.md` — encoder hand-solder process note; light-pipe length pending; the bracket is now 121 × 14.5 × 3
   mm with 15.6/17.9/17.9 mm slots (not 105 × 21 mm with 18 mm slots).
4. `docs/decisions.md` — record the single 9.0 mm provisional display standoff with the Z datum, and the USB-C
   feedthrough as an open part choice rated 3 A today.
5. `docs/design-review.md` — "LCD lowered to y = 0", "display glass y = −24…32", "8–10 mm standoffs" and
   "89.2 × 56.3 mm cutout" are stale; the display centre is (−17, 11), the aperture is 90.67 × 54.36 mm, the lens is
   88.87 × 52.56 and the 6.7/4.0 mm depth labels were reversed.
6. `docs/acceptance.md` — add the §5 gates as named acceptance items.
7. `docs/fab-check.md` — the panel LCD cutout is now a rounded-rectangle polygon (28 points, r = 1.0 mm), not a rect;
   state that in the panel row.
8. `tscircuit/scripts/sourcing-extras.csv` and `jlc-order.json` — the USB-C feedthrough line must not imply 5 A; mark
   the candidate 3 A and the replacement pending.
9. `tscircuit/front.circuit.tsx` — no edit needed for this pass (it reads `FEEDTHROUGH_BRACKET.mountX/mountY` and
   `FRONT_TOP_NOTCH`), but the display-underside keepout over the top-side SMD is an open gate there too.

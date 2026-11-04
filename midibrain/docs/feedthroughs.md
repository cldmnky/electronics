# USB feedthroughs and strain relief

The three front USB ports stay **panel-mounted feedthrough cables**, not USB receptacles soldered to the front board.
The front board is a support structure: its top edge has a U-shaped opening for the feedthrough bodies, two bracket
mounting holes at (−60, 46.5) and (55, 46.5), and a 3D-printed bracket for cable strain relief.

All positions come from `tscircuit/lib/layout.ts`. The printed part is built from `mechanical/layout.json`, which is
generated from it. Verification record: `docs/MECHANICAL-VERIFICATION.md`.

## Front panel layout (provisional, from the single coordinate source)

- Panel feedthrough centres at **y = 53.5 mm** (this was written as 56 mm in an earlier revision; 53.5 is correct):
  - USB-C power: x = −38 mm
  - UMC/audio USB-A: x = −5 mm
  - MIDI-hub USB-A: x = +35 mm
- Display centre **(−17, +11)**, so the 88.87 × 56.00 mm module spans x −61.4…+26.6 and y −17…+39.
- Transport buttons at **y = −34 mm**, jacks at y = −47.5 mm, LEDs at y = −24 mm.
- Front-board top relief notch: 105 × 8 mm at (−2.5, 52), i.e. x −55…+50, y 48…56, open to the top edge only. The
  board stays solid below y = 48 across the notch, which is the surface the bracket bolts to.

## Panel cutouts (provisional, to be checked against purchased samples)

- USB-A, each: **14.9 × 7.3 mm** opening (vendor 14.65 × 7.10 plus 0.25 assembly allowance), 2 × **Ø3.5 mm** holes
  **30 mm** apart on the Adafruit 908 drawing.
- USB-C: **12.6 × 7.6 mm** slot for SparkFun CAB-15455, two Ø3.2 holes on 18 mm centres for the clamp.
- LCD aperture: 90.67 × 54.36 mm with 1.0 mm corner relief at (−17, 11), sized from the vendor LENS OD
  (88.87 × 52.56), not the module PCB; see `docs/mechanical-stack.md`.
- Leave at least 25–35 mm behind the panel for the cable tails and bend radius. Unverified.
- These are **panel openings**. They do not prove the rear body, flange or cable-bundle envelopes: those stay pending
  against the purchased samples.

## Flange fit against the front board — open finding

The Adafruit 908 flange is 38.30 × 11.90 mm. At the canonical y = 53.5 centre it spans y 47.55…59.45, while the board
notch spans y 48…56 and the board top edge is at y = 56. The flange therefore overlaps board material by **0.45 mm at
its lower edge** and extends past the board's top edge. Options, none chosen: raise the feedthrough centres, trim the
flange, or lower the notch (the notch cannot go below y = 47.85 without breaking into the bracket mount holes). This is
a named physical gate — measure the purchased flange first.

## Selected parts

| Function | Candidate | Status |
|---|---|---|
| USB-A ×2 | [Adafruit 908](https://www.adafruit.com/product/908), [drawing](https://cdn-shop.adafruit.com/product-files/908/USB+A+Female.pdf) | USB 2.0, 340 mm. Flange 38.30 × 11.90 mm; opening 14.65 × 7.10 mm; 2 × Ø3.5 on 30 mm centres. Sample not bought. |
| USB-C power | SparkFun CAB-15455, USB-C F panel → USB-C M | 305 mm, USB 2.0, **3 A**. Flange 26.4 × 8 mm; panel slot 12.6 × 7.6 mm; two Ø3.2 on 18 mm. **Not 5 A.** |
| Internal USB-C cable | 0.3 m USB-C male-to-male, e-marked | Exact MPN and current rating TBD. Keep it short. |
| Bracket | 3D-printed PETG `mechanical/feedthrough-bracket.stl` | 121 × 14.5 × 3 mm, three open-top slots, two mount holes, two head-clearance slots, four tie holes. Reproducible, not committed. |
| Bracket hardware | 2 × M2.5 × 8 mm screws/nuts; 4 × 2.5 × 100 mm cable ties | Hand assembly. Head/washer envelope unmeasured. |

### USB-C current rating — pending part choice, do not paper over it

The existing candidate (CAB-15455) is **3 A**. The owner's requirement for a 5 A-capable supply path is unchanged and
the layout does not alter it. Do **not** invent a 5 A rating for this cable and do not substitute a part that has no
documented current rating. **Stop buying the 3 A CAB-15455 as the power feedthrough** until one of these is settled by
test or by a documented part:

1. a panel feedthrough with a documented 5 A / PD rating that fits the 14 mm top strip (Neutrik NAUSB-C is documented
   at 5 A but its D-series flange is ~31 mm tall and does not fit), or
2. an explicit owner decision to accept 3 A with a measured `vcgencmd get_throttled` result under load.

The Adafruit 4218/6069 round USB-C panel cables are **not** confirmed 5 A; do not use them for Pi 5 power without a
current/PD test.

## Bracket geometry (derived, provisional)

Generated to `mechanical/layout.json` by `cd tscircuit && bun run scripts/export-mechanical.ts`. Python never restates
a coordinate.

| Feature | Value | Why |
|---|---|---|
| Outline | x −63…58, y 41.5…56 (121 × 14.5 × 3 mm) | Mount centres plus a 3.0 mm nut wall; bottom 2.5 mm above the display top (39.0); top at the board top edge. |
| Mount holes | Ø2.7 at (−60, 46.5), (55, 46.5) | Same holes the front board drills. 1.65 mm edge ligament. |
| Cable slots | open-top, widths 15.6 / 17.9 / 17.9 and floors y = 48.20 / 48.35 / 48.35 at x = −38 / −5 / +35 | Vendor panel cutout + 1.5 mm per side in width AND height. The floor is each feedthrough's declared opening bottom less 1.5 mm, above the notch bottom (48.0). A fixed floor at y = 50 cut into the declared openings (USB-C 49.7, USB-A 49.85). |
| Slot webs | 16.25 mm and 22.10 mm | The first draft used 20/40/40 mm flange-sized slots that touched at x = +15, leaving a 0 mm web. Not printable, withdrawn. |
| Head clearance | Ø6.5 slots at (±44, 43) | The top front↔rear spacer hardware sits in the same 3 mm layer. The envelope is a provisional assumption, not a measurement; the slots break out of the bottom edge as open slots. |
| Cable ties | 4 × Ø3.2 at x −52.23, −22.07, +15.00, +48.80, y 53 | In the webs, never in a slot or a mount ligament. |

The bracket is the printed part closest to release, and it is still only a planning model: two inputs (M2.5 head
envelope, printed-hole fit for an M2.5 screw) are unmeasured. See `docs/MECHANICAL-VERIFICATION.md`.

## Front board changes

- Top relief notch 105 × 8 mm at (−2.5, 52), open to the top edge only; no display cutout.
- Bracket holes (−60, 46.5) and (55, 46.5), Ø2.7.
- The LCD mounts on the solid front board at the asymmetric 58 × 49 mm holes.
- No USB data is routed on the PCB; all USB signals stay in the panel cables.

## Verification still required

- Buy one USB-C feedthrough with a documented rating and two Adafruit 908 cables; measure body depth, flange and panel
  cutouts, and resolve the 0.45 mm flange overlap above.
- Print the bracket, fit it to the front board, and check that it clears the display rear, the DSI cable, the top
  spacer hardware and the front-panel rail.
- Test the complete feedthrough/cable chain with the official 5 V/5 A supply; check `vcgencmd get_throttled` under load.
- Do not order the panel or print the bracket for use until the sample fit and the current test have results.

# JLCPCB order guide: production packet

**Not an approval to order.** All physical acceptance items remain pending.
`make release` first checks `hardware-acceptance.json` and its source-bound
evidence. If it fails, no export runs and existing `fab/` files are untouched.
A successful prototype `make test` is not a production approval.

A separately approved engineering prototype follows [acceptance.md](acceptance.md).
It supplies review/first-article material, not a shortcut through the production
gate. Recheck exact parts, stock, package and assembly previews before payment.

## Files to upload after approval

| Purpose | File |
| --- | --- |
| Front fabrication | `fab/front/front-gerbers.zip` |
| Front SMT BOM | **`fab/front/front-bom-smt.csv`** |
| Front SMT CPL | `fab/front/front-cpl-jlc.csv` |
| Rear fabrication | `fab/rear/rear-gerbers.zip` |
| Rear SMT BOM | **`fab/rear/rear-bom-smt.csv`** |
| Rear SMT CPL | `fab/rear/rear-cpl-jlc.csv` |
| Panel fabrication only | `fab/panel/panel-gerbers.zip` |
| Pi-panel fabrication only | `fab/pi-panel/pi-panel-gerbers.zip` |
| Bench sourcing | `fab/sourcing-bom.csv` and each board's `*-bom-hand.csv` |
| Reference only | Raw `bom.csv`, `pnp-all.csv`, combined `*-bom-jlc.csv`, STEP and schematic/PCB views |

Upload the **SMT-only BOM** with the SMT-only CPL. The combined reference BOM
also contains THT parts; it is not the default SMT upload. All BOM designators,
C-number syntax, assembly sides and placement counts are checked. Do not
hand-edit generated fabrication files to repair a source problem.

## Board options

- Front 128 × 112 mm; rear 101.6 × 112 mm (20HP); panel 152.0 × 128.5 mm
  (30HP/3U, 152.0 actual against the 152.4 mm nominal); pi-panel 101.2 ×
  128.5 mm (20HP/3U, the Pi mounting panel — bare FR-4 like the 30HP panel).
  Each board is 2-layer, 1.6 mm FR-4.
- Front and rear use 1 oz copper and candidate lead-free HASL. Apply the
  actual emitted source rules, reviewed Gerbers and
  [fab-check.md](fab-check.md), not obsolete defaults from an older export.
  The project via target is 0.20 mm drill / at least 0.45 mm pad; the production
  check enforces its manufacturing targets.
- Panel: **no copper, no placed components, no solder mask on either side**,
  white legends. No copper finish is required. Obtain fabricator confirmation
  of the no-mask/copperless order and review routed apertures and tolerances.
- The LCD opening is a rounded-rectangle polygon, **107.22 × 68.87 mm** with
  1 mm corner relief, derived from lens dimensions rather than PCB height.
  Physical display and rail fit remain acceptance items.

## SMT and THT assignment

### Front

Expected raw source/BOM count: **46** = **31** SMT placements + **15** hand/BOM-only lines, matching `jlc-order.json` and the built board. Expected SMT CPL count: **31**, all on
**top/panel side**. This includes the MIDI circuits, passives, LEDs and NAV.
The rear face carries the J5 THT female socket body and the solder tails of the
other through-hole parts, not SMT.

THT exclusions: `J1`–`J4`, `SW1`–`SW7`, `U6`, `J5`, `J_PWR` and `DISP1`. They remain in the bench/
combined BOM. `U6` is the Bourns PEC11R-4015F-S0024 (C143789), detentless
24 PPR with two THT anchor lugs; the 2026-11-06 JLC/LCSC search found no SMD
encoder with 0 detents, 24+ PPR and through-hole anchor lugs (the SMD
anchor-lug families top out at 15 PPR), and a lug-less SMD knob encoder is
not accepted. It stays THT and out of the SMT CPL: request JLCPCB THT/hand-
solder assembly for it if offered, otherwise bench-qualify per
[assembly.md](assembly.md). Bourns does not recommend hand soldering, so the
process, profile and inspection must be accepted before production.

`DISP1` is the 4.3-inch DSI module. It bolts to the four SMTSO bosses on the
front face, so it is part of the board mechanically, but it has no pads and no
LCSC part: it is drawn in the tscircuit 3D view as a translucent envelope from
`DISPLAY_STACK` (106.00 x 68.00 x 14.05 mm, rear face 5.0 mm above the board
top) and it appears on the hand BOM with the ElectroKit article. It is not on
the SMT CPL. The envelope is a reference box - the boss pattern, the FPC exit
and the installed height are unverified (`display_boss_pattern`,
`display_fpc_idc`).

### Rear

Expected raw source/BOM count: **24**. Expected SMT CPL count: **20**, all on
**bottom/rear side**, including the two new local bypass capacitors C13/C14.

THT exclusions: `J_PICO_L`, `J_PICO_R`, `J_PI`, `J_FRONT`. `J_PI` uses the
XFCN BH254V-40P import with its reference C-number (not a perimeter-numbered
generic header). `J_FRONT` is the Sullins SBH11-PBPC-D13-ST-BK male box header
of the **direct 2 × 13 mate** and has no LCSC number, so it is non-SMT hardware.
Pico sockets still need exact sourcing. The male's Ø1.00 mm drill is the
manufacturer recommendation (drawing 11091 sheet 1); the front female
SFH11-PBPC-D13-ST-BK uses the same drill as a **provisional assumption**
because no female PCB hole is published. Do not substitute a -M181 (no key)
variant or another body without re-running the mate test and reviewing the
first-article fit.

Counts are validated against `jlc-order.json` and the generated files. A source
change that changes these quantities must update the reviewed order settings;
never edit a CPL merely to force a match.

## Mandatory preview and process review

- Inspect **both assembly sides**, every polarity and pin-1 mark, and actual
  bottom-side rotations in the fabricator's assembly preview.
- Inspect drill sizes, annular rings, copper/mask clearances, apertures,
  silkscreen legibility and tooling/panel/fiducial requirements with JLCPCB.
  Automated shorts/style checks and a valid export are not CAM approval.
- Confirm that the hand/qualified-process BOM is bought and fitted separately
  from the standard SMT service.
- Confirm the complete enclosure and service envelopes, actual light-pipe
  selection and control/jack reach, not only component centres.

## Bench sourcing safeguards

`sourcing-bom.csv` preserves all strictly parsed Notes. In particular:

- CAB-15455 is **3 A and rejected for the 5 A power path**. The exact suitable
  feedthrough/cable chain and PD/droop/temperature tests are still pending.
- Light pipe: owner-selected **candidate** Signal Construct PGN25L165 (Ø2.6 panel hole, shaft 16.5 ±0.1, overall 17.5). Not an approved fit: the seated plane, LED height, retention in 1.6 mm FR-4, hole tolerance and stack tolerances are unmeasured, and the optical gap is null. The retired Bivar PLP2-400-C 10.16 mm reference and the 7.29 mm subtraction are withdrawn. Select the final part after the reach and seating checks. The 2.55-2.60 mm hole is NOT guaranteed: the JLCPCB NPTH bands conflict and the panel emits each circular cutout in both the NPTH drill file and Edge_Cuts; the JLC viewer and support must confirm the de-duplication and the achievable hole size.
- The display uses a **5 mm provisional spacer beyond 4 mm bosses**, producing
  9 mm PCB-plane separation. Plane separation is not the spacer purchase length.
- Knob, NAV/button actuators, Pi spacers (provisional 3.5 mm), the direct
  2 × 13 connector pair, the Pi GPIO ribbon and cooler/HAT/SSD remain subject
  to the recorded physical acceptance checks.
- The bracket generator and CAD self-check now agree with canonical layout
  data. STL/STEP are reproducible, ignored outputs; actual printed fit and
  strain relief are still pending.

See [assembly.md](assembly.md), [mechanical-stack.md](mechanical-stack.md)
and the physical acceptance record ([acceptance.md](acceptance.md)). No source document or generated
packet is a substitute for the required reviewed evidence.

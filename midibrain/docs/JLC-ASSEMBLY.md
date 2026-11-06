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
| Bench sourcing | `fab/sourcing-bom.csv` and each board's `*-bom-hand.csv` |
| Reference only | Raw `bom.csv`, `pnp-all.csv`, combined `*-bom-jlc.csv`, STEP and schematic/PCB views |

Upload the **SMT-only BOM** with the SMT-only CPL. The combined reference BOM
also contains THT parts; it is not the default SMT upload. All BOM designators,
C-number syntax, assembly sides and placement counts are checked. Do not
hand-edit generated fabrication files to repair a source problem.

## Board options

- Front 128 × 112 mm; rear 101.6 × 112 mm (20HP); panel 132.08 × 128.5 mm
  (26HP/3U). Each board is 2-layer, 1.6 mm FR-4.
- Front and rear use 1 oz copper and candidate lead-free HASL. Apply the
  actual emitted source rules, reviewed Gerbers and
  [fab-check.md](fab-check.md), not obsolete defaults from an older export.
  The project via target is 0.20 mm drill / at least 0.45 mm pad; the production
  check enforces its manufacturing targets.
- Panel: **no copper, no placed components, no solder mask on either side**,
  white legends. No copper finish is required. Obtain fabricator confirmation
  of the no-mask/copperless order and review routed apertures and tolerances.
- The LCD opening is a rounded-rectangle polygon, **90.67 × 54.36 mm** with
  1 mm corner relief, derived from lens dimensions rather than PCB height.
  Physical display and rail fit remain acceptance items.

## SMT and THT assignment

### Front

Expected raw source/BOM count: **43**. Expected SMT CPL count: **31**, all on
**top/panel side**. This includes the MIDI circuits, passives, LEDs and NAV.
The rear face carries the J5 THT body and solder tails, not SMT.

THT exclusions: `J1`–`J4`, `SW1`–`SW6`, `U6`, `J5`. They remain in the bench/
combined BOM. `U6` is the Bourns PEC11R-4015F-S0024 (C143789), detentless
24 PPR with two THT anchor lugs; the 2026-11-06 JLC/LCSC search found no SMD
encoder with 0 detents, 24+ PPR and through-hole anchor lugs (the SMD
anchor-lug families top out at 15 PPR), and a lug-less SMD knob encoder is
not accepted. It stays THT and out of the SMT CPL: request JLCPCB THT/hand-
solder assembly for it if offered, otherwise bench-qualify per
[assembly.md](assembly.md). Bourns does not recommend hand soldering, so the
process, profile and inspection must be accepted before production.

### Rear

Expected raw source/BOM count: **24**. Expected SMT CPL count: **20**, all on
**bottom/rear side**, including the two new local bypass capacitors C13/C14.

THT exclusions: `J_PICO_L`, `J_PICO_R`, `J_PI`, `J_FRONT`. The latter two use
XFCN BH254V-40P/BH254V-26P imports with reference C-numbers; they are no longer
perimeter-numbered generic headers. Pico sockets still need exact sourcing.
Check actual ribbon keying, pin 1 and continuity; do not substitute another
body without reviewing geometry and first-article fit.

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
- Bivar PLP2-400-C is a **10.16 mm reference, not an approved fit** to the
  provisional 14.1 mm panel gap. Select the final part after reach/seating checks.
- The display uses a **5 mm provisional spacer beyond 4 mm bosses**, producing
  9 mm PCB-plane separation. Plane separation is not the spacer purchase length.
- Knob, NAV/button actuators, Pi spacers, ribbons and cooler/HAT/SSD remain
  subject to the recorded physical acceptance checks.
- The bracket generator and CAD self-check now agree with canonical layout
  data. STL/STEP are reproducible, ignored outputs; actual printed fit and
  strain relief are still pending.

See [assembly.md](assembly.md), [MECHANICAL-VERIFICATION.md](MECHANICAL-VERIFICATION.md)
and [HARDWARE-REVIEW.md](HARDWARE-REVIEW.md). No source document or generated
packet is a substitute for the required reviewed evidence.

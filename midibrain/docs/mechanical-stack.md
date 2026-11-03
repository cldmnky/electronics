# Mechanical arrangement — preliminary, not a clearance approval

Coordinates are mm with panel centre (0, 0), +X right and +Y up looking at the **front**. PCB FRONT / CAD top faces the panel; REAR / CAD bottom faces the rack cavity. The concept aligns the panel and PCB axes; there is not yet a verified Z datum or standoff height. Looking from behind reverses the apparent X direction.

The original `assets/front-panel-concept.png` is a visual reference only. The owner has since approved a 26HP, two-board layout with a 3.5-inch DSI display, five transport buttons below it (REC, OVER DUB, STOP, PLAY, PLAY START), TEMPO/NAV at its right, the USB feedthroughs at the top and four MIDI jacks below. `assets/front-panel.svg` is generated from `tscircuit/lib/layout.ts`. All icons are symbolic, not actual footprints/cutouts, and the artwork is not a mechanical drawing.

Chosen stack, FRONT to REAR: front panel with an 89.2 × 56.3 mm LCD cutout; 128 × 112 mm UI board with four LCD mounting holes; 3.5-inch DSI display mounted above the UI board on M2.5 standoffs; 20HP (101.6 × 112 mm) base board; Pi 5 on the base board rear side; Pi cooler/NVMe behind the Pi. The display is 6.70 mm deep plus its 15-pin FPC; use 8–10 mm standoffs so the rear connectors clear the UI board. DSI video, 3.3 V and I2C touch share the included 12 cm cable to the Pi 5 DSI1 connector. The UI board right-hand control column overhangs the 20HP base board and connects through the 2×13 cable. This is the intended arrangement, not a physical fit approval.

## Preliminary plan-view envelope

| Item | Candidate | Status |
|---|---|---|
| 26HP panel | 132.08 × 128.5 | Nominal project envelope, rail hole locations and tolerances still need rack-system confirmation. |
| Front/UI PCB | 128 × 112 × 1.6 | Routed prototype. No display cutout; four LCD standoff holes at the vendor’s asymmetric 58 × 49 mm pattern. Five THT buttons, three 0805 LEDs, four vertical THT TRS jacks and the MIDI SMD circuitry. |
| Rear/base PCB | 101.6 × 112 × 1.6 (20HP) | Routed prototype. Pi holes on 58 × 49 mm grid; Pico on the left, Pi ribbon header top, front interconnect right. The 128 mm front board overhangs this 20HP board on the right by about 26.2 mm. |
| LCD mounting | 58 × 49 mm pattern: holes 25.00 mm from PCB left and 3.50 mm from right; 3.50 mm from top/bottom | Front-board M2.5 standoffs hold the LCD. The display is 6.70 mm deep plus FPC; use 8–10 mm standoffs/spacers. Panel cutout is 89.2 × 56.3 mm. |
| Pi PCB | 85 × 56, portrait | Rear of the 20HP base board, component side rearward, centre +6 east of the board origin (`PI_MOUNT`). Pi spans X −22…+34: 7 mm clear of the Pico (right edge −29), 2.5 mm clear of U3/U4, 8.5 mm clear of J_FRONT, 3 mm clear of J_PI. R13–R18/Q3 sit under the Pi PCB (fine for 0603/SOT at 2.5 mm). The 12 cm DSI cable reaches Pi DSI1. |
| Display | Waveshare `3.5inch DSI LCD (H)`, SKU 33087 | Vendor drawing: 88.87 × 56.00 mm module, 75.80 × 45.36 mm active area, 58 × 49 mm mounting holes, 6.70 mm rear depth. 480 × 800 portrait default; rotate to 800 × 480 landscape. 3.3 V 180 mA, DSI1 + I2C touch on one 15-pin FPC. |
| Display rear interfaces | One 15-pin FPC for DSI, 3.3 V and I2C touch | The included 12 cm DSI cable adapts to the Pi 5 22-pin DSI1 connector. No HDMI or touch-USB cable. Verify FPC bend radius, latch and clearance. |
| Control and jack centres | REC/OVER DUB/STOP/PLAY at x = −53/−29/−5/+19, PLAY START at x = +47, y = −34; LEDs at x = −53/−29/+19, y = −25; TEMPO (47,30), NAV (47,11), SHIFT/EXIT (47,−8); MIDI jacks at x = −53/−29/−5/+19, y = −47.5 | Point placement only; actual button/jack bodies, panel bushings and actuator heights still need a sample check. |
| USB feedthrough centres | Three at y = 56; LCD centre at y = 7 (glass +35…−21) | Panel-mounted 16 mm USB-C 5 A coupler and two Adafruit 908 USB-A feedthroughs. The front board has a top relief notch and a 3D-printed bracket; see `docs/feedthroughs.md`. Cable bodies/bends still need a sample check. |
| Pi cooler / NVMe | candidate full-size M.2 HAT+ and Pi Active Cooler | Their use and exact stacking height/clearances must be checked against the exact revisions and actual assembly. Confirm that access to the 40-pin header remains possible. |
| HDMI, USB audio, powered hub, display touch and front USB-C | panel/feedthrough assemblies proposed | Connector orientation, length, bend radii, retention, shielding, voltage drop and Pi-port access not yet measured. |

**Depth is not yet ≤110 mm verified.** The STEP model gives 14.04 mm for the display/standoff assembly; add the base board (1.6 mm), Pi (1.6 mm), Pi-side spacer (2–3 mm), cooler/NVMe stack (roughly 25 mm) and cable/service clearance. This is approximately 45–50 mm before plugs, panel thickness and airflow, so 110 mm looks feasible but is **not a measured assembly clearance**. The Pi must be supported by the base board/standoffs, not by the GPIO ribbon. The Pico sockets and USB plugs still need physical clearance checks.

Before freezing layout: acquire actual display/cable/controls drawings and production-grade Pi mount data; measure or import complete component models; assign FRONT/REAR placements; check accessible assembly, socket and cooler airflow; generate front/rear/side views and collision/clearance report. No increase of panel width or installed depth without an explicit product decision.

| Front panel | 26HP × 3U FR-4, 1.6 mm, no solder mask, white silkscreen, ENIG | Bare PCB panel with LCD/control/USB/jack cutouts and three 4 mm LED light-pipe holes. |
| Panel↔front standoffs | 4 × M2.5 at (±61, ±53), shared panel/front holes (`FRONT_BOARD_HOLES`) | Moved out from (±60, ±50): old bottom-left head sat inside the J1 jack courtyard, old top-left heads were 3.5 mm apart (board standoff vs bracket mount). 1.65 mm edge ligament to the front board. Length TBD (~10 mm + panel gap); pending sample. |
| Front↔rear spacers | 4 × M2.5 at (±44, +43/−52), shared front/rear holes (`FRONT_REAR_MOUNT`), panel does not carry these | Rear board's only mechanical home; replaces the unmatched (±45, ±50) rear pattern. Length TBD (~18–20 mm pending J5 IDC bend mockup). Residuals: top pair needs clearance holes in the feedthrough-bracket regen; (−44, 43) needs low-profile top hardware for the 3.5 mm display gap and a display-underside keepout check. |

## Behringer Eurorack Go fit review (2026-11-03, design check only)

Case data from the Thomann product page (`behringer_eurorack_go.htm`): 2 rows ×
140 HP, 6U, M3 slide nuts, bus board with 32 slots, +12 V/3 A, −12 V/1 A,
+5 V/1 A, chassis 112 × 723 × 311 mm. **Maximum module depth 40 mm (lower row)
and 62 mm (upper row).** The module draws nothing from the Eurorack bus (Pi is
fed from the external 27 W USB-C supply through the panel feedthrough), so this
is a physical-fit question only.

### Width / height: PASS with one correction applied

- 26HP panel (132.08 mm) fits a 140 HP row; 128.5 mm panel height fits 3U.
- **Rail-hole bug found and fixed:** the panel used Y ±56.75 (7.5 mm edge
  inset). Doepfer 3U — same rule as Froemix (`RAIL_HOLE_Y = PANEL_H/2 − 3.0`) —
  needs Y **±61.25** (3.0 mm inset); X ±58.54 (7.5 mm inset) was already
  correct. Fixed in `tscircuit/lib/layout.ts`; panel rebuilds and the
  96-check placement gate passes. With the old holes the module could not have
  bolted to any standard rail, Go included.
- Round Ø3.2 holes suit the Go's sliding M3 nuts, but slotted holes (as on
  most Eurorack panels) are recommended for tolerance.
- Front PCB 128 × 112 leaves 2.04 mm/side and 8.25 mm top/bottom inside the
  panel. 112 mm board height exceeds the ~110 mm Doepfer practice for clearing
  rails and sliding nuts; the board sits ~13–15 mm behind the panel so it
  should clear the rail extrusion in Z, but confirm against the Go rail profile
  before release.

### Depth: FAIL for the Go as currently stacked

Nominal Z budget from the panel front (design values, not measurements):

| Segment | Nominal |
|---|---|
| Panel 1.6 mm | 1.6 |
| Panel-back to front-PCB top (display 6.7 + 3.5 standoff + ~3 spacer) | 12–15 |
| Front PCB 1.6 | 1.6 |
| Front-to-rear gap (J5 IDC plug + cable bend; rear top bare) | 18–22 |
| Rear PCB 1.6 | 1.6 |
| Rear-to-Pi spacer + Pi PCB | ~4 |
| Pi components / Active Cooler / M.2 HAT+ / SSD | ~20–30 |
| **Rear-most behind panel back** | **≈60–68** |

That exceeds the **40 mm lower row by 20–28 mm** and is at or over the
**62 mm upper row** before tolerances, DSI/ribbon folds, the Pico USB plug and
airflow. Even a stripped Pi (no HAT, passive cooling) lands around ~55 mm
behind the panel — still over the lower row and thermally risky for a Pi 5
doing 18-channel audio. The project's own ≤110 mm depth target was never
compatible with this case. **Verdict: do not plan this module for the
Eurorack Go; use a ≥100 mm-deep case or split the Pi/cooler/NVMe out of the
rack.** No fabrication release until the case is reselected and the stack
re-proven.

### Board-to-board and component blockers (fixed 2026-11-04, residuals open)

- **Shared front-rear grid (was: no common grid):** front↔rear M2.5 spacers
  at (±44, +43/−52) from one `FRONT_REAR_MOUNT` constant consumed by both
  boards and verified in the placement gate (rear H1–H4 land on the same XY).
  The old rear (±45, ±50) pattern is gone. Length TBD (~18–20 mm pending the
  J5 IDC bend mockup). Residuals: the bracket regen must cut clearance holes
  for the top pair, and (−44, 43) needs low-profile top hardware plus a
  display-underside keepout check against the vendor drawing.
- **Pico vs Pi overlap (was: 1 mm XY overlap + Z foul):** Pico right edge
  (X −29) against a centred Pi's left edge (X −28), with the 10–12 mm Pico
  crashing through the Pi plane (2.5 mm spacer). Fixed by shifting the Pi
  +6 east in `PI_MOUNT` (portrait-corrected pitches 49 × 58, which also fixes
  the stale landscape `PI_HOLES` const that disagreed with the board's
  portrait drills). All Pi links are flexible cables, so nothing else moved.
  Residual: verify the Pi 5 microSD card (bottom side) against the
  R13–R18/Q3 zone now under the Pi PCB; else use 5 mm Pi spacers.
- **J1 vs bottom-left standoff (was: head inside jack courtyard):**
  panel↔front standoffs moved to (±61, ±53); the M2.5 head now clears the J1
  courtyard (X −58…−48) by 0.5 mm, and the top-left heads are 6.6 mm apart
  instead of 3.5 mm. Mandated −53 column untouched. Residual: 0.5 mm is a
  design-check number — confirm against the standoff vendor's head/flange
  dimensions at first article.
- J5 corridor (X ≈29–39, Y −10…+30) clears the display PCB edge (~26.3) by
  ~3 mm and the control column (~39.5) by ~1 mm — tight, opposite board sides,
  passes placement DRC; IDC plug overhang and THT solder-tail protrusion still
  need a physical sample.
- LCD top (39.0) to feedthrough bracket bottom (41.5): 2.5 mm. Display left
  (−61.44) to front-PCB edge (−64): 2.56 mm. Both tight but consistent.
- C5 (50, 46) vs right bracket mount (55, 46.5): ~1 mm screw-head clearance.
- Button-to-LED 9 mm, label offsets and the 19 mm right-column pitch are
  consistent across panel, front board and preview artwork (96/96 placement
  gate).

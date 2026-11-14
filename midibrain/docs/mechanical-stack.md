# Mechanical arrangement — provisional, not a clearance approval

Coordinates are mm with panel centre (0, 0), +X right and +Y up looking at the **front**. Every number here is a
planning value from `tscircuit/lib/layout.ts`, which is the single source; nothing below is a measurement or a fit
approval. Open physical gates: [acceptance.md](acceptance.md) and [TODO.md](TODO.md).

**Z datum: Z = 0 is the front (operator-side) face of the panel; +Z points rearward into the rack cavity.** PCB
FRONT / CAD top faces the panel; REAR / CAD bottom faces the rack cavity. Looking from behind reverses the apparent
X direction.

## Current respin state (owner decisions 2026-11-13)

This is the current arrangement. It is geometry from `tscircuit/lib/layout.ts`; it is **not** a fit approval, and
`make test` is not green while the tests are re-derived (see [TODO.md](TODO.md)).

| Quantity | Current value | Constant in `lib/layout.ts` |
|---|---|---|
| Panel front → front-board top face | **10.0 mm** (panel rear 8.4 mm), set by the ordered Thonkiconn jacks | `PANEL_TO_FRONT_BOARD_TOP`, `PANEL_TO_FRONT_BOARD_GAP` |
| Front/UI PCB | **150 × 112 × 1.6 mm**, U-shaped: two legs (11.75 mm left, 31.75 mm right) joined by a bottom bar | `FRONT_PCB` |
| Rear/base PCB | **148 × 112 × 1.6 mm** | `REAR_PCB` |
| Front board rear face → rear board front face | 11.20 ±0.20 mm nominal (direct 2 × 13 pair, seat to seat) | `INTERCONNECT_MECH.seatToSeat` |
| Front-board opening (ONE cut, through the top edge) | 106.50 × 68.90 mm, x −63.25…43.25, y −23.45…through the edge, 3.0 mm chamfers | `FRONT_BOARD_OPENING`, `FRONT_BOARD_OPENING_POLYGON` |
| Display mount | printed carrier bolted to the **rear** board's top face; 8.75 mm adjustable spacers from that face, 12.75 mm to the display PCB rear | `DISPLAY_STANDOFF` |
| Display carrier holes (rear board, Ø2.7) | (−59.15, 41.9) (40.15, 41.9) (−59.15, −19.9) (40.15, −19.9) | `LCD_MOUNT_HOLES` |
| Front↔rear spacer holes (BOTH boards, Ø2.7) | (−69, −35) (71.5, −35) (−69, 46) (71.5, 46) | `FRONT_REAR_MOUNT` |
| Panel drilled holes | six feedthrough screws (2 × Ø3.2, 4 × Ø3.5), the four Ø3.2 rail holes and the four Ø3.2 `STAND_` panel-mount screws | `PANEL_DRILLS`, `PANEL_MOUNT_HOLES` |
| Panel-to-front-board mount frame (BOTH boards, Ø3.2) | (−71.5, −52) (71.5, −52) (−71.5, 52) (71.5, 52), M3, standoff 8.40 mm nominal | `PANEL_MOUNT_POINTS`, `PANEL_STANDOFF` |
| Panel LCD aperture | 107.22 × 68.87 mm at (−10.0, 11), 1.0 mm corner relief | `PANEL_LCD_CUTOUT` |
| USB panel feedthroughs (y 53.5) | x **−52 / −23.5 / +11** | `USB_FEEDTHROUGHS` |
| Right control column (x 52) | POWER/SW7 y 49, VALUE/U6 y 31, NAV/U5 y 12, SHIFT/EXIT/SW6 y −5, PLAY START/SW5 y −34 | `POWER_BUTTON`, `RIGHT_CONTROLS`, `PLAY_START` |
| J_PWR (rear face, rotation 180, mating axis +Y) | (64, 44.5) | `POWER_HEADER` |
| J5 / J_FRONT (direct 2 × 13 pair) | (66, 20), rotation 90, right of the display | `FRONT_INTERCONNECT` |
| Transport row (y −34, 24 mm pitch, centred under the opening) | x −46 / −22 / +2 / +26 | `FRONT_COLUMNS`, `TRANSPORT_Y` |
| MIDI jacks (y −47.5, 12 mm pitch, one block at the left) | x −62 / −50 / −38 / −26 | `MIDI_JACK_X` |
| MIDI clusters | in1 (−16, −48) in2 (2, −48) out1 (20, −46.1) out2 (38, −46.1) | `MIDI_CLUSTERS` |
| Button stack vs Omron's reference | **excess 0.00 mm** (10.0 mm board-to-panel-front is Omron's own 10.0 ±0.4) | `BUTTON_STACK` |
| Button caps (SW1–SW7, 7 off) | **printed cap standard**: 11.80 square plate, 3.85 × 0.60 locator, top Z −0.80 (0.80 proud), part height 4.10; bought B32-13x0 is the documented alternative | `BUTTON_CAP_PRINTED`, `BUTTON_REACH` |
| Light pipe | the selected PGN25L165 (16.5 mm shaft) is 6.5 mm **too long**; about 9.2 mm is needed and no part is selected | `LIGHT_PIPE_REACH` |
| Main module installed depth (provisional) | 47.1 mm from the panel front face, 45.5 mm behind the panel back | `provisionalInstalledDepth()` |

**SUPERSEDED by the 2026-11-13 owner decisions** - do not quote these as current: the 19.05 mm panel-to-board
distance and its 17.45 mm panel-rear gap; the 128 mm front and 101.6 mm rear boards; the display screwed to the
front board; the 9.0 mm plane separation with a 5.0 mm spacer; the separate 105 × 8 mm top relief notch; the four
panel standoff holes at (±61, ±53) and the closed front board without a display opening; J_PWR in the notch at (−2.5, 47); the POWER button at the bottom right; the MIDI clusters in
the right-hand strip; MIDI jacks sharing the transport columns. Each replaced value stays visible in
[decisions.md](decisions.md), which is a log, not a current-state document.

## Approved envelope (owner decisions)

| Item | Value | Note |
|---|---|---|
| Front panel | **30HP × 3U, 152.0 × 128.5 × 1.6 actual** (owner decision 2026-11-08, replaces the 32HP/162.56 mm panel of 2026-11-07) | Rail holes X ±68.58, Y ±61.25, Ø3.2 (spacing 137.16 = 27 × 5.08, the Doepfer multiple rule; 7.42 mm centre inset, 5.82 mm hole edge to panel edge). 152.0 is OUR ESTIMATE, 0.4 mm under the 152.4 mm nominal (0.2 mm per side): not a Doepfer-stated 30HP value, not owner-exact, not a mandate and not a guaranteed fit. Sources: [Doepfer A-100 mechanical information](https://doepfer.de/a100_man/a100m_e.htm) and its [hole figure A100FP.gif](https://doepfer.de/a100_man/A100FP.gif), which gives 128.5 mm height, a 7.5 mm horizontal and 3.0 mm vertical hole inset, and Ø3.2 mm holes but publishes no 30HP width. The fixed 27 × 5.08 grid on 152.0 mm gives 7.42 mm, i.e. **0.08 mm tighter than the figure's 7.5 mm** - disclosed, not corrected. Display window 107.22 × 68.87 mm centred on the module, unchanged. |
| Front/UI PCB | **150 × 112 × 1.6**, U-shaped (owner decision 2026-11-13) | The display left this board, so its opening is a hole: two legs (11.75 mm left, 31.75 mm right) joined by a bottom bar. It carries the four front↔rear spacer holes and the four Ø3.2 panel-mount holes of the frame below, but no display holes. `J5`/`J_FRONT` sit at (66, 20) in the right leg. |
| Rear/base PCB | **148 × 112 × 1.6** (owner decision 2026-11-13, replaced the 20HP 101.6 mm board) | Pico sockets on the left, J_PI (40-pin ribbon header) at the top, J_FRONT male on the TOP face. It carries the four front↔rear spacer holes and the four Ø2.7 display-carrier holes on the module's threaded SMTSO boss pattern (99.30 × 61.80 mm centres, 3.35 / 3.10 insets, layout x −59.15 / +40.15, y +41.90 / −19.90). The drawn 58.00 × 49.04 plain-hole pattern stays as the recorded alternative (`DISPLAY_STANDOFFS`). The Pi and its hole grid are **not** on this board: the Pi moved to its own mounting panel on 2026-11-12. |
| Pi mounting panel | **20HP × 3U, 101.2 × 128.5 × 1.6 actual** (owner decision 2026-11-12) | Pure mechanical JLCPCB FR-4, no copper and no components, fabricated like the 30HP panel. Same width convention (20 × 5.08 − 0.4) and the same Doepfer rail-hole rule as the 30HP panel: X spacing 86.36 = 17 × 5.08 at the same 7.42 mm centre inset (holes ±43.18), Y ±61.25, Ø3.2. Pi 5 (with Active Cooler + M.2 HAT+ + NVMe SSD) bolts to the panel's **rear** face on 3.5 mm candidate standoffs, **landscape** and mirrored (component side away from the panel): the Pi 5's 58 × 49 pattern (M2.5, Ø2.7) sits 10 mm toward +X, with holes at (−19, ±24.5) and (+39, ±24.5). The Pi board is ±42.5 × ±28 and its connectors reach −45.8…+44.2 × −29.7…+28 (`piBoardToPanel`; Pi 5 drawing RP-008347-DS-1, read from its vector data). Source `tscircuit/pi-panel.circuit.tsx`, geometry `PI_PANEL_*` in `lib/layout.ts`, guard `lib/pi-panel.test.ts`. USB stays on the 30HP panel (no USB opening is added to the Pi mounting panel); the three feedthroughs themselves moved on 2026-11-13, see the respin table. |
| Installed depth target | ≤110 mm | Target retained. Not verified; see the depth section. |

Owner control centres (2026-11-13, `RIGHT_CONTROLS`, `FRONT_COLUMNS`, `MIDI_JACK_X` in `lib/layout.ts`): transport
buttons REC / OVER DUB / STOP / PLAY at x −46 / −22 / +2 / +26, y −34 (24 mm pitch, centred under the opening whose
centre is −10.0); PLAY START at (52, −34); VALUE (52, 31); NAV (52, 12); SHIFT/EXIT (52, −5); POWER (52, 49); LEDs
12 mm beside REC / OVER DUB / PLAY at y −34; MIDI jacks at y −47.5 in one 12 mm-pitch block at x −62 / −50 / −38 /
−26; display centre (−10.0, 11). The jacks deliberately no longer share the transport X: cables under SW1–SW4 fouled
the buttons and the hand pressing them (commit `cf26c42`).

## Provisional Z stack

The chain is now derived from **one owner number** - the panel front sits 10.0 mm above the front board's top face
(`PANEL_TO_FRONT_BOARD_TOP`) - plus the vendor depth figures. It does not certify a gap.

- The 10.0 mm is set by the ordered parts, not by the display: the Thonkiconn WQP419GR body is 8.3 mm plus 4.5 mm of
  M6 × 0.5 bushing thread, so its bushing face lands 12.8 mm above the PCB and the panel rear sits at 8.4 mm; Omron's
  B32 reference is 10.0 ±0.4 mm from the board to the panel FRONT (the printed cap uses the same 10.0 mm plane and
  stands 0.80 mm proud of it); the PEC11R M7 thread spans about 6.5…13.5 mm.
- The display is still assumed to sit with its glass front flush with the panel front face (Z = 0,
  `DISPLAY_GLASS_FRONT_Z`, Assumption A). Nothing has measured this. The vendor model puts its touch/frame forward
  face **1.90 mm in front of the glass front face**, so an illustrative placement with the glass at Z = 0 puts that
  frame at Z = −1.90 mm (1.90 mm proud of the panel). That is a property of the supplied model, not a fit.
- Vendor figures for the 4.3-inch module: **14.05 mm overall** (wiki FAQ, the vendor's stated maximum), split
  provisionally as 10.05 mm from the **glass front face to the display PCB rear face** plus the **4.0 mm** boss
  projection behind it. Only the 4.0 mm is a vendor-modelled number; the 10.05 mm is derived (14.05 − 4.0), not
  measured, and the split is the thing the first article replaces with two direct measurements. The reference STEP's
  union bbox is 13.82 mm; see the thickness resolution in the display table below.
- The display no longer hangs off the front board at all. It is carried by a printed part bolted to the **rear**
  board's top face on 8.75 mm spacers, and those spacers are the adjustment that trims the glass plane to the panel
  front at assembly.

| Feature | Z (mm) | Basis |
|---|---:|---|
| Panel front / rear faces | 0 / 1.60 | 1.6 mm FR-4. |
| Display glass front | 0.00 | Assumption A, not measured. |
| Display mounting PCB | 8.45 | Glass front + 10.05 − 1.6 mm PCB (provisional split). |
| Display PCB rear face | 10.05 | Glass front + 10.05 mm (provisional split, not measured). |
| Display rear-most point (boss ends) | 14.05 | 10.05 + 4.0 mm boss projection; equals the wiki overall depth. |
| Front-board top face | **10.00** | `PANEL_TO_FRONT_BOARD_TOP` - the owner number, not a consequence of the display. |
| Front-board rear face | 11.60 | 1.6 mm board. |
| Rear-board front face | 22.80 | Front-board rear + the 11.20 mm seat-to-seat interconnect dimension. |
| Rear-board rear face | 24.40 | 1.6 mm board. |
| Panel rear → front-board top | **8.40** | `PANEL_TO_FRONT_BOARD_GAP`; a consequence of the 10.0 mm, not evidence for it. |
| Display rear-most point → front-board top | **−4.05** | `DISPLAY_UNDERSIDE_GAP`: the module's rear 4.05 mm sits BEHIND the board's top face, which is why the board carries a 106.50 × 68.90 opening instead of the module. |
| Rear-board front face → display PCB rear | **12.75** | `DISPLAY_STANDOFF.planeSeparation` (provisional design target). |
| Rear-board front face → module boss ends | **8.75** | `DISPLAY_STANDOFF.spacerLength` = 12.75 − 4.0 boss projection; the adjustable dimension. |

### The vendor model's own planes — measured, and separate from the design chain

The hash-verified reference STEP was re-inspected with a real CAD kernel
(`mechanical/tools/inspect_display_step.py`, CadQuery 2.8.0, 394 solids). Its own
planes are:

| Vendor-model plane | Vendor z (mm) | Basis |
|---|---:|---|
| Touch/frame forward face | **+4.80** | planar face, 106.10 × 67.80 mm |
| Cover-glass front face (`43H-800480-IPS`) | **+2.90** | planar face, 105.42 × 67.07 mm |
| Cover-glass rear face | 0.00 | planar face, 105.42 × 67.07 mm |
| PCB front face | −1.74 | planar face, 106.00 × 68.00 mm |
| PCB rear face | **−3.34** | planar face, 106.00 × 68.00 mm |
| SMT boss ends (`SMTSO-M2_5-4ET`) | **−7.34** | 8 boss bodies, 5.5 mm cube |
| Rear-most modelled feature | −9.02 | 2.5 × 22.0 mm body, not a boss |
| Union bounding box | −9.02…+4.80 (13.82) | 106.10 × 68.363 × 13.82 mm |

It follows from those planes, and only from them:

- glass front → PCB rear = **6.24 mm**,
- PCB rear → boss ends = **4.00 mm** (this agrees with the 4.0 mm boss projection
  used below),
- boss ends → rear-most feature = 1.68 mm,
- touch/frame forward face = **1.90 mm in front of the glass front face**.

**In-plane orientation is not established.** The Z reversal is forced (the vendor +Z points out of the glass), but
the in-plane axis is not: the model is drawn with a 180° rotation about Y and the 180° rotation about X is an equal
candidate. The drawing shows readable rear-side silkscreen and may be a rear view, so matching the recorded
`DISPLAY_STANDOFFS` is consistency with the layout's own no-flip premise, not independent proof. A real front/rear
labelled photograph of the module, or the physical part, settles it. Both candidates are recorded in
`mechanical/step_geometry.py` (`DISPLAY_ORIENTATION`, status PENDING) and in the assumptions file.

**These are model planes and nothing else.** The 6.24 mm is NOT the provisional
10.05 mm glass-front-to-PCB-rear chain, and the 13.82 mm model depth is NOT the
vendor's stated 14.05 mm maximum overall depth. The three sets answer different
questions and must not be equated: the 10.05 mm chain is a design assumption, the
14.05 mm is the vendor's stated maximum, and 6.24 mm is what the supplied model
draws. The provisional chain stands unchanged; the first article measures the
real part.

**Spacer length is not the plane separation.** The spacer screws onto the boss ends, which already reach 4.0 mm below the
display PCB rear face, and both numbers are now measured from the **rear board's front face** (the carrier bolts to
that face), so:

| Quantity | Provisional value | Meaning |
|---|---:|---|
| Plane separation | 12.75 mm | Rear-board front face to display PCB **rear face**. |
| Boss projection | 4.0 mm | Boss ends stand 4.0 mm behind that PCB rear face (primary model). |
| Spacer length | **8.75 mm** (adjustable) | Rear-board front face to the module's boss ends = 12.75 − 4.0. This is the length the carrier spacers set, and adjusting it is how the glass plane is trimmed to the panel front. |

Ordering a 12.75 mm spacer while intending a 12.75 mm plane separation would add the boss projection a second time and
push the display through the panel. The 19.05 mm / 9.0 mm / 5.0 mm chain that these numbers replace is **superseded**
(owner decision 2026-11-13); the drawn STEP placeholders still sit on the old 8.81 mm gap until the CAD is regenerated.

The exact carrier geometry is still to be drawn and checked: all four boss points are now inside the 148 mm rear board
(at x −59.15 / +40.15), so none has to be carried outboard as it did on the 20HP board. Spacer length, diameter,
thread and assembly remain first-article tasks.

**Display underside — pending, no clearance claimed.** `DISPLAY_UNDERSIDE_GAP` is **−4.05 mm**: with the panel front
10.0 mm above the front board, the module's rear-most point (14.05) lies 4.05 mm *behind* the front board's top face.
Nothing is cleared here and no board material may sit under the module - that is exactly why the front board carries
a 106.50 × 68.90 mm opening instead of the display. The tallest front-board top-side part with a manufacturer-stated
height is the MIDI receiver in the Toshiba 11-4L1S body, **maximum installed height 2.1 ± 0.1 mm (drawing p. 15)**,
i.e. 2.2 mm (`FRONT_TOP_SMD_MAX_HEIGHT`), and it now sits under open air. The display's own underside component map,
its 15-pin FPC latch and the module tolerance are still unmapped: keep this as a gate.

**Light pipe — the selected part is now too long.** The candidate is the Signal Construct **PGN25L165** (manufacturer
drawing <https://static.rapidonline.com/pdf/07-1015_v1.pdf>, SHA-256
`5350ad1f0a795e0d688dc74e223239c82cfb3e27951244a10cb43873eeeaa46b`, retrieved 2026-11-09). The drawing gives
shaft Ø2.5 ±0.05 mm, **x = 16.5 ±0.1 mm from the head underside to the tip**, head Ø3 ±0.05 mm, head height 1.0 mm,
three longitudinal ribs and a **Ø2.6 −0.05 mm mounting hole** (2.55-2.60 mm). The owner's screenshot for this MPN
states **17.5 mm overall**, which is 16.5 + 1.0. The panel openings are therefore 3 × Ø2.6 mm nominal, 12 mm beside
REC / OVER DUB / PLAY at x −34 / −10 / +14, y −34.

Datum: panel FRONT face Z = 0, +Z rearward. With the head underside on the panel front face — an **assumption, not a
measurement** — the nominal tip plane is **16.5 mm**, and the front-board top face is now at 10.00 mm, so the tip would
pass the board by **6.50 mm** (`LIGHT_PIPE_REACH.nominalTipToBoardPlane = −6.50`). A pipe of about **9.2 mm** shaft
length is needed (10.0 mm minus an eyeballed ~0.8 mm LED body) and **no part is selected**. That is arithmetic between
assumed planes and **not the LED-to-pipe optical gap**: the LED height, brightness/coupling, retention in 1.6 mm FR-4,
the real hole tolerance and the stack tolerances are unmeasured, so `opticalGap` is **null** and the status is
**incomplete**.

The earlier Bivar **PLP2-400-C 10.16 mm** reference and the `17.45 − 10.16 = 7.29 mm` subtraction are **withdrawn**
together with the 17.45 mm plane they were computed from: the 10.16 mm value is not in the current Bivar datasheet,
that family's Ø3.3 mm flange falls through the old Ø4.0 mm opening, and the subtraction compared different reference
planes. Neither 17.45 − 16.5 nor 17.45 − 17.5 replaces it, and neither does 8.4 − 16.5.

Manufacturer facts that are **not** stated: any user cutting instruction for the shaft, and any proven retention in
1.6 mm FR-4. Trimming is **not documented**; it is not claimed to be forbidden either. The distributor SKU
(Conrad 2435525) appears only in an externally reported search and the site could not be reached, so it is
**PROVISIONAL**. The ribs are visible on the drawing; their grip is unknown. The `light_pipes` acceptance item stays
pending, and no physical fit is claimed.

**The 2.55-2.60 mm hole is NOT a guaranteed fabrication result.** The drawing's hole range is 2.55-2.60 mm, and the
JLCPCB sources conflict: standard NPTH is quoted as **±0.05 mm** in one place and **±0.08 mm** in another, while the
capabilities page gives **+0.13/−0.08 mm** for through holes with **no NPTH-specific split**
([capabilities](https://jlcpcb.com/capabilities/pcb-capabilities),
[NPTH design guide](https://jlcpcb.com/blog/npth-design-guide)). No band is adopted here and none is promised.

The panel also emits **each circular cutout in both `drill_npth.drl` and `Edge_Cuts.gbr`**, and CAM de-duplication of
drill versus routing is **unverified**. Before any order, the **JLC viewer and JLC support** must confirm (a) how that
duplicate feature is processed and (b) whether 2.55-2.60 mm can be held on this panel. The CAD and the tests assert
the **intended nominal** Ø2.6 mm geometry only; they make no manufacturing-capability claim.

## LCD aperture (panel cutout)

| Property | Value | Reason |
|---|---:|---|
| Size | **107.22 × 68.87** | Module **glass front-face outline** 105.42 × 67.07 plus 0.9 mm per side. The 106 × 68 PCB and the 95.54 × 54.36 printed bezel are behind/inside this, so neither sizes the aperture. |
| Corner relief | 1.0 mm radius | A routed inside corner cannot be sharp; emitted as an explicit rounded polygon. |
| Clearance per side | 0.90 | 0.2 mm JLCPCB profile tolerance + 0.2 mm vendor GLASS tolerance (all charged to one side) + 0.5 mm unverified registration/assembly allowance. |
| Corner clearance | 0.851 measured | Polygon chords lie inside the true arc, so the aperture is slightly smaller than the exact rounded rectangle; measured on the built panel by `scripts/check_placement.ts` at the four GLASS corners, above the 0.5 mm target. |

### 4.3-inch DSI LCD: measured numbers, both sources

| Feature | Value | Source |
| --- | --- | --- |
| Module PCB outline | 106.00 × 68.00 × 1.6 mm | vendor drawing (drawn) / STEP plane pair z 0 → 1.6 |
| Glass front-face outline | **105.42 × 67.07 mm** | reference STEP, cover-glass front face (16-point loop at z 2.900 / 0.000) |
| Front frame around the glass | 106.10 × 67.80 mm | reference STEP (faces at z 3.80 / 4.80) |
| Active area (VA) | **95.04 × 53.856 mm** | 800 × 0.1188 / 480 × 0.1122 mm pixels; STEP VA face 95.04 × 53.86 at z 2.900 |
| Printed bezel opening | 95.54 × 54.36 mm | STEP plane = VA + 0.25 mm per side |
| Mounting pattern (used) | **99.30 × 61.80 mm**, insets 3.35 / 3.10, 4× Ø4.30 | vendor drawing; STEP `SMTSO-M2_5-4ET` threaded SMT bosses sit here |
| Inner hole pattern (alternative) | 58.00 × 49.04 mm, insets 25.62 / 22.38 / 9.50 / 9.46, 4× Ø4.30 | vendor drawing; plain plated holes, no thread |
| Overall thickness | **14.05 mm (wiki FAQ)** vs **13.82 mm (STEP bbox)** | RESOLVED as far as the sources allow: the wiki FAQ states the vendor's overall thickness 14.05; the reference STEP's union bbox (CadQuery import, 106.10 × 68.36 × 13.82) is 0.23 mm thinner, and **no modelled solid reaches the 14.05 plane**. The model's only overhangs past the 106 × 68 PCB envelope are in X/Y (front frame 106.10 × 67.80, union y 68.362, i.e. ≤ 0.18 mm past the PCB edge) — nothing in Z explains the difference, so it is the vendor's stated maximum vs. modelled nominal, not glass overhang or feet. The stack uses the LARGER (14.05) so the panel gap cannot be optimistic. |
| Recorded connector data — **UNVERIFIED** | 15-pin, rear face, right-hand long edge in the drawing's view; body 21.412 × 4.826 mm; module-local centre (+48.34, +0.7) → absolute 38.84, 11.7 — all recorded design values, not established | drawing record only (7.11 px/mm, ±0.4 mm); see the model check below the table |
| Panel-to-front-board gap | **8.40 mm provisional** | 10.0 mm (`PANEL_TO_FRONT_BOARD_TOP`, owner decision 2026-11-13) less the 1.60 mm panel. It is now set by the ordered jacks and the Omron reference, not derived from the display. The replaced 17.45 mm (Assumptions A + B, display screwed to the front board) was 14.10 mm for the withdrawn 3.5-inch module. |
| Light-pipe reach | PGN25L165: shaft 16.5 ±0.1, overall 17.5, head 1.0 | Nominal tip plane **16.5** from the panel front against a **10.00** board plane: the tip would pass the board by **6.50**, so this part is **too long** and a ~9.2 mm shaft is needed with no part selected. Optical gap **null**, status **incomplete**. The retired 10.16 / 7.29 figures are **withdrawn**. |

**Connector model check (2026-11-08, corrected).** The STEP's XCAF label tree shows the two 21.412 × 4.826 mm bodies are `EXTRUDED-106739` / `EXTRUDED-107488` inside `TFT1_ASM` — TFT flex bonds, **not** a panel connector. An earlier revision treated them as the FPC connector and inferred a ~24 mm disagreement with the drawing; that reading is withdrawn. The nearest named body to the recorded position is `DISPLAY_CAM` (4.822 × 22.400 × 5.680 mm, vendor x −51.975…−47.153, y −13.570…8.830, z −9.020…−3.340), placed centre (40.064, 11.000) mm against the recorded (38.837, 11.700) mm — an observation of 1.413 mm, **not a match**. Identity, pin count, electrical side, the long-axis wording and the rear/front frame mapping all stay unverified and need independent re-derivation. The recorded position does not move the PCB. Whether the connector, plug or first bend intersects the front board's top-side parts remains unknown and needs a first-article check. The CAD model draws the connector only as `DISPLAY_CONNECTOR_PLANNING_MARKER` on the provisional Z 10.05 plane.

Display window (owner decision D1): the aperture follows the GLASS, not the 106 × 68 PCB envelope, and not the
95.54 × 54.36 printed border. Window = glass + 0.9 mm per side = 107.22 × 68.87 mm, centred on the module at
(−10.0, 11). Vertical fit: the glass-derived window is 68.87 mm against a 65.10 mm free band when the LED light pipes
sit above the buttons, so the authorised fallback (3) was applied — the REC / OVER DUB / PLAY light pipes moved 12 mm
beside their buttons. The window spans y −23.435…+45.435 and keeps **4.44 mm** below it to the transport-button
openings (top edge −27.875) and **4.265 mm** above it to the lowest feedthrough cutout floor (49.70). All legends sit
above or below their own opening (owner rule 2026-11-13), so none intrudes into those bands: the feedthrough labels
are at y 59.0 (`LABEL_OFFSET.feedthrough` in `lib/layout.ts`). Horizontal: the window left edge −63.61 keeps **12.39 mm to
the panel edge** (−76.0), a true perpendicular distance. Its **X projection** to the M3 rail-hole edge (66.98) is
**3.37 mm**, but the rail hole sits at a different Y, so the projection is not a web and not a shortest clearance -
the true 2D distance is code to be re-measured from the emitted aperture polygon by `tscircuit/lib/layout.test.ts`,
which is part of the current test audit (see [TODO.md](TODO.md)). The knob at
x = 52 (Ø12.5 body, right edge 58.25) keeps **17.75 mm to the panel edge**, again perpendicular. Every figure here is
planning geometry: rail nut/washer fit, neighbour-module fit and the physical print remain pending. The front board's
left edge is at −75 (150 mm board) and the opening starts at −63.25, so the left leg is 11.75 mm; the opening's right
edge 43.25 keeps **2.50 mm** to the value-knob body (Ø12.5 at x = 52).

Reference CAD path: `mechanical/reference/waveshare-4.3inch-dsi-lcd.step` (STEP SHA-256
`01eef9172ff4364691f4de37ff98f95437e96caa647b94b4bd4a1c54ac4519ed`, downloaded from the wiki's
`4.3inch_DSI_LCD_Drawing.zip`, archive SHA-256 `f75cb7ad…`, provenance in
`mechanical/reference/waveshare-4.3inch-dsi-lcd.source.json`). CadQuery is not installed in the build image, so
`mechanical/tools/measure_display_step.py` reads the ISO-10303-21 text directly and prints every solid's bounding box
plus the front-facing planar faces; the glass and bezel numbers above come from that script.

Kit contents and the standoff row: the box carries three DSI cables — **50 mm** and **120 mm** 15-pin→22-pin (the Pi 5
DSI1 interface) and a 160 mm 15-pin→15-pin cable that does NOT fit DSI1 — plus screws and copper pillars. The module
itself carries M2.5 SMT threaded standoffs (`SMTSO-M2_5-4ET`, 4 mm class) on the outer 99.30 × 61.80 pattern, which is
that pattern that is FITTED (see the reconciliation row above); the inner 58.00 × 49.04 plain holes stay recorded as
alternative data (DISPLAY_STANDOFFS) and the kit's copper pillars can span them electrically. Design DSI cable pick:
the **120 mm** 15→22 cable is the starting candidate; the run now goes from the carrier on the rear board to the Pi on
its own panel, so the fitted length is a `pi_panel_harness` measurement, not a design claim. The **12.75 mm** plane
separation = 4.0 mm boss projection + **8.75 mm** spacer, measured from the rear board's front face, remains
provisional against the kit pillars and is verified by acceptance item `display_boss_pattern`.

### Display FPC ↔ J5 coexistence (unverified; `display_fpc_idc` pending)

The earlier planning position (38.84, 11.7), connector identity and body dimensions require re-derivation.
The STEP features previously used for the FPC height were identified as TFT flex bonds, not as a verified
connector. The drawn planning marker is not on the placed vendor model's PCB-rear plane. Consequently neither the
connector's location nor its clearance to J5 is established. The owner has since required the direct 2 × 13 mate: the
female socket is on the front board's bottom (rear-facing) face and the male header on the rear board's top face. The
Pi no longer sits on this board (2026-11-12). J5/J_FRONT themselves now sit at **(66, 20), rotation 90**, in the right
leg - to the **right** of the display, with no board material under the module.

The front-board top plane is now Z = 10.00 mm, which is not a measured FPC corridor either (the module's own body
occupies Z 0…14.05 and passes through the board's 106.50 × 68.90 opening). The 120 mm 15→22 cable is a candidate, not
an approved route. Measure the actual connector, cable path, bends, latch and the J5 socket together on the first
article before claiming clearance. Do not use the former 12.9 mm connector-stack, 6.1 mm clearance, 2 mm bend-radius
or 19.05 mm stack figures as verified values.

The display source is the 4.3-inch drawing and reference STEP recorded above; the withdrawn 3.5-inch 2D/3D files are no
longer a source. The active area is centred on the module outline (`DISPLAY_ACTIVE_OFFSET_X = 0`); only the front-board
silkscreen window uses it, and the panel aperture comes from the glass outline. `tscircuit` has no rounded-rectangle
cutout, so `panel.circuit.tsx` emits `roundedRectOutline()` from `lib/layout.ts` as a polygon.

**Panel openings (from `USB_FEEDTHROUGHS` / `PANEL_LCD_CUTOUT`).** Three feedthroughs at
y = 53.5: USB-C power at x = **−52** (12.6 × 7.6 mm slot, Ø3.2 holes on 18 mm centres),
UMC audio USB-A at x = **−23.5** and MIDI-hub USB-A at x = **+11** (14.9 × 7.3 mm each, Ø3.5 holes
on 30 mm centres). The 28.50 / 34.50 mm pitch is set by the vendor **flange screws**, not by the cutout widths (a
1.00 mm FR-4 web between the nearest screw holes needs 4.35 / 4.50 mm); measured screw-hole gaps on the built panel
are 18 / 4.5 / 30 / 4.5 / 30 mm, i.e. webs of 1.15 / 1.00 / 1.15 mm (commit `e16bce9`). The LCD window is
107.22 × 68.87 mm at (−10.0, 11). Leave at least 25–35 mm behind the panel for cable tails and bend radius;
unverified. These are panel openings, not proof of the rear body, flange or cable-bundle envelopes.

**Feedthrough fit — open finding.** The Adafruit 908 flange is 38.30 × 11.90 mm, so at x −23.5 / +11 it spans
−42.65…−4.35 / −8.15…30.15 and at y 53.5 it spans y 47.55…59.45: it extends past the board's top edge but no longer
over board material, because above y −23.45 the front board is open over x −63.25…43.25 and all three cutouts sit
inside that band. Only the USB-C flange (26.4 mm, SparkFun CAB-15455 at x −52) reaches 1.95 mm over the left leg, and
the flange's depth below the panel is not published. This stays an XY-projection finding and a named physical gate —
measure the purchased flange first.

## Board face convention — requested, not implemented (pending)

A separate requirement asks for **both boards' CAD-bottom faces toward the panel**. It is not implemented, and no
board source, SMT side or stackup is changed for it:

- The printed board models in the CAD file are plain slabs. A bare slab has no top/bottom feature marker, so a
  flipped slab would look identical and prove nothing.
- The front/UI board carries its SMT parts and the control actuators on the **top** face today, and the rear/base
  board already carries its SMT parts on the **bottom** face.
- Putting either board's bottom face toward the panel is therefore a real PCB layer/side reassignment: it moves every
  part, the silkscreen, the assembly drawing and the CPL side.

This needs an owner decision and a PCB revision. Until then the CAD model keeps the current board orientation and
makes no compliance claim. Recorded in the assumptions file as `board_face_convention`.

## Panel button opening: 12.25 mm square; printed cap is the standard

Every B3F-4055 button (SW1–SW7) takes a **printed cap**, one per switch, seven
in total. The bought Omron/ARATAS **B32-1380** (red, 12 × 12 mm square) is the
documented **alternative**; the black equivalent is **B32-1310**. The B32 is no
longer bought for the standard build because Omron does not dimension its cap
top, so its seated height was never derivable. The printed cap turns that
unknown into a number we set: its underside rests on the plunger face and its
top is placed 0.80 mm proud of the panel front. The part is built by
`mechanical/printed/button_cap.py`; `mechanical/printed/README.md` carries the
print and first-article instructions.

Facts from the Omron B32 datasheet (SHA-256
`90834f79bcb19ed59fc242f0a005d8ca47fcdbf2f4583bc53695695bb675d28f`, re-read
2026-11-12, page 3) still describe the **alternative**:

| Fact | Value |
|---|---|
| B32 key top | 12 ±0.15 mm square |
| **Reference: PC board to the panel FRONT face** | **10.0 ±0.4 mm** |
| Panel thickness range | 1.0 to 2.0 mm |
| Reference panel cutout | **12.2 +0.05/0 mm square** |
| Corner radii | R0.5 on the key top, R0.2 on the skirt |

The 10.0 ±0.4 dimension is **not** a cap height. The reference drawing shows the
panel's front face 10 mm above the PCB and the B32 cap top just below the
panel's rear face. With a 1.0–2.0 mm panel that puts the panel rear 8.0–9.0 mm
above the PCB; Omron does not dimension the cap top. The B3F-4055 datasheet
(SHA-256 `be9cf69e5f43fb7689448a2097c19858e5643c0e2e49cd77d018881543e27a1f`)
gives the switch envelope: a square 3.8 ±0.1 mm projected plunger on a
12 ±0.2 mm square body, 7.3 ±0.2 mm from the board to the plunger face.

**Panel opening.** `PANEL_CUTOUTS.button = 12.25` mm square (sharp corners).
12.25 is the top of Omron's 12.2 +0.05/0 band, so the largest B32 cap
(12.15 mm) clears by 0.05 mm per side. The printed plate is 11.80 mm square and
clears by **0.225 mm per side**. The 1.6 mm panel is inside the 1.0–2.0 mm
range. The routed-square tolerance at JLCPCB is **not verified**; confirm it
before ordering.

**Actuator stack: the board now sits at Omron's reference plane.** Z datum:
panel front face Z = 0, +Z rearward. The front board top is at Z 10.00
(`PANEL_TO_FRONT_BOARD_TOP`, owner decision 2026-11-13), so the
board-to-panel-front distance equals Omron's own reference and the
board-to-panel-rear distance is 8.4 mm.

| Quantity | Value |
|---|---:|
| Omron reference board-to-panel-front | 10.0 mm (9.6…10.4) |
| Our board-to-panel-front | 10.00 mm |
| **Stack depth excess vs the reference** | **0.00 mm (±0.4)** |
| Omron reference panel rear above PCB (1.6 mm panel) | 8.4 mm |
| Our panel rear above PCB | 8.4 mm |
| B3F-4055 plunger face (7.3 above board) | Z 2.70 |
| Printed cap top (0.80 proud of the panel front) | Z −0.80 (10.80 above board) |
| Printed cap top above the switch | 10.80 − 7.30 = **3.50** |

**The printed cap is a three-term part, not a two-term one.** The plunger sits
INSIDE a locator recess, so the cap's rear face stops 0.60 mm behind the plunger
face:

```
  cap top           Z -0.80   0.80 proud of the panel front
  panel front       Z  0.00
  panel rear        Z  1.60
  recess floor      Z  2.70   = B3F-4055 plunger face (7.30 above the board)
  cap rear face     Z  3.30   = recess floor + 0.60 locator depth

  part height = 2.70 flush + 0.60 locator + 0.80 proud = 4.10 mm
  cap top above board = 10.00 + 0.80 = 10.80 mm
  cap top above switch = 10.80 - 7.30 = 3.50 mm
```

Deriving the height as flush + proud (3.50) would be the same mistake the 3D
body made twice: it would put the cap top 0.6 mm below the panel front. The
four-term cross-check (underside 2.70, rear face 3.30, top −0.80, height 4.10)
is asserted in `tscircuit/lib/layout.test.ts` and re-derived from the panel stack
and the B3F datasheet in `mechanical/printed/printed_geometry.py`.

**Why a plate and not a skirt.** The 12.25 mm cutout is only 0.125 mm per side
larger than the 12.0 ±0.2 mm switch body, so a skirt could not pass the panel
and could not be assembled. The printed cap is therefore a plate on the
plunger: 11.80 × 11.80 mm, with a 3.85 × 3.85 × 0.60 mm locator recess opening
rearward over the 3.80 mm plunger (0.025 mm per side). The aperture centres the
cap; there is no other guide. Axial retention is the printed fit on the plunger
alone.

`BUTTON_STACK.stackDepthExcess` is 0.00 mm: the 9.05 mm shortfall the button
caps, the jack bushings and the encoder bushing all showed under the 19.05 mm
chain is gone. `control_actuator` is now **closed by design** for the button
height: the cap's 4.10 mm part and 10.80 mm top are ours to set, not a bought
part's undimensioned property. It is **not yet closed by measurement**: the
first article must record (a) the installed cap height and press travel, (b)
the printed locator fit and whether a cap stays on its plunger when the panel is
lifted, (c) the 0.60 mm locator ring's clearance to the body top at rest and at
full travel (the datasheet page recorded here gives the 7.3 mm overall height,
not the body top), and (d) the routed 12.25 mm square's real size. If the bought
B32 alternative is used instead, its seated height and press travel are still
unmeasured. Recorded as `BUTTON_CAP_PRINTED` and `BUTTON_REACH` in
`lib/layout.ts`.

## Board-to-board relationship

Both boards are centred on their own origin, so the 150 mm front board overhangs the 148 mm rear board by
**1.0 mm per side**. The right-hand control column at x = 52 is now inside the rear-board outline (±74), and the
four front↔rear M2.5 spacers sit at (−69, −35) (71.5, −35) (−69, 46) (71.5, 46) — the positions where the U-shaped
front board actually has material (its two legs) and both boards are solid; the right-hand pair sits inside the
148 mm rear board (±74), 1.15 mm from the hole edge to the board edge. The old (±44/±45, −52/43) grid is dead: on the
U board (−44, −52) lands on the jack row and (45, 43) on the opening's cut edge.

- Front↔rear spacers: length = the direct 2 × 13 pair's **11.20 ±0.20 mm** nominal seat-to-seat drawing dimension
  (Sullins 11091 sheet 2), not a measured gap and not a purchased-part choice until the pair is fitted and measured.
  The two top spacers at y 46 sit inside the feedthrough bracket envelope, so the printed bracket carries
  head-clearance circles (Ø6.5 planning envelope) there; the M2.5 head/washer envelope is unmeasured. The bracket
  itself must be regenerated from the new positions before it is printed.
- Panel↔front standoffs: **four M3 points** (owner decision 2026-11-13, `PANEL_MOUNT_POINTS`), one frame at
  (±71.5, ±52) drilled through BOTH the panel (`STAND_1…4`) and the front board (`STANDOFF_1…4`). Four, not two:
  the bushing/clamping hardware sits in the panel's lower half, and two points on one horizontal line leave that line
  as a free hinge — the panel would still rock top-for-bottom, and the display opening removes its stiffest region.
  X = ±71.5 is forced by the board (the opening starts at −63.25, so the left leg is 11.75 mm wide); at ±71.5 the hole
  edge keeps 1.9 mm to the board edge and 2.9 mm to the panel edge, and the Ø5.5 standoff body still lands on FR-4.
  Y = ±52 is the widest row pair: 6.50 mm to the nearest RACK hole (2D), 3.05 mm to the nearest front↔rear spacer
  hole, 6.65 mm to the U opening, 9.08 mm to the LCD aperture (2D from the rounded outline), 5.81 mm to the MIDI IN 1
  jack aperture, 4.5 mm to the J1 jack courtyard, and ≥6.40 mm to any copper (pad, via, trace) on the built board — the nearest
  drilled hole is the front↔rear spacer at 3.05 mm. scripts/check_placement.ts measures the built JSON, and the GND
  pour gets its own 0.2 mm anti-pad ring around each new hole.
  **Length and tolerance discipline:** 8.40 mm nominal = `PANEL_STACK.boardToPanelRear` = board top (10.00) minus
  panel thickness (1.60) — derived, never typed. They are the SAME **adjustable** family as the front↔rear spacers,
  NOT fixed-height: the panel plane is set by the MIDI jack bushings (they clamp the panel), the 8.40 mm is a nominal
  and the bushing-stack tolerance behind it is UNMEASURED, so a rigid spacer would either hold the panel off the
  jacks or bend the panel and preload the jack solder joints. Each is trimmed/shimmied at assembly to the measured
  gap. The specific standoff (thread length, material) and the gap are first-article readings.
- Direct interconnect (J5/J_FRONT at (66, 20), rotation 90; body 7.60 × 40.52 mm): the female socket and the male
  shroud stand in the 11.20 mm gap in the right leg, to the right of the display opening. The recorded FPC connector is
  a planning marker only (identity, side and position unverified), so no clearance from it is claimed. The remaining
  FPC question is the module's own tail and the female solder tail (3.20 mm from the seating face, i.e. a nominal
  1.60 mm beyond a 1.60 mm PCB, fillet unmeasured; see `display_fpc_idc` above).
- Pi side of J_FRONT: the male tail is **3.00 mm from the connector seating face** (drawing 11091 sheet 1), so it
  protrudes a **nominal 1.40 mm beyond a 1.60 mm rear PCB**; the 26 pads have no Pi board above them (the Pi is on its own panel). A **candidate** Pi
  spacer of 3.5 mm is recorded, but it is a candidate only and NOT a safety claim: the solder fillet height, the PCB
  thickness tolerance, the Pi hardware, the actual clearance, the Pi's own connectors and the cooler/HAT stack are all
  unmeasured, so this stays pending.
- Display carrier: the module hangs on a printed carrier bolted to the rear board's TOP face at `LCD_MOUNT_HOLES`,
  with the front board's opening around it and no board material under the module. The carrier's own outline,
  thickness, spacer bosses and fastening are not drawn yet. The front board carries no display holes and no display
  silkscreen beyond the single "DISPLAY OPENING" note (owner decision 2026-11-13); the rear board's TOP face carries
  the LCD1–LCD4 labels and the four L-shaped register ticks, derived from `LCD_MOUNT_HOLES`.
- C5/C6 and the rest of the right column: the column moved to POWER 49 / VALUE 31 / NAV 12 / SHIFT·EXIT −5, so the
  old "C5 (50, 46) against the right bracket mount" check no longer describes this board; re-derive the head and web
  checks from `layout.ts` plus `check_placement.ts` output rather than from memory.

## Provisional installed-depth chain

Two rack modules since the Pi moved to its own mounting panel (owner decision
2026-11-12). Both chains are arithmetic on unmeasured candidates, not verified
installed depths: no cooler/HAT/SSD, cable-bend or airflow model exists.

**Main module (30HP panel + boards + socketed Pico), panel front face to its
rear-most point:**

| Segment | Provisional | Basis |
|---|---:|---|
| Front-board rear face | **11.60** | See the Z stack (`PROVISIONAL_Z.frontBoardRear`). |
| Front↔rear gap (direct 2 × 13 mate, seat to seat) | 11.2 | Drawing 11091 sheet 2, ±0.20 mm; not measured. |
| Rear/base PCB | 1.6 | 1.6 mm FR-4. |
| Pico socket stack | 12.7 | CANDIDATE: ~8.5 mm female header + the Pico 2's 1.0 mm PCB (Pico 2 datasheet: "1 mm thick PCB") + a ~3.2 mm micro-USB body (unmeasured). The console USB plug adds its own bend; unmeasured. |
| Harness allowance | 10 | Ribbon fold and cable bends at the module exit (feedthrough bracket slots); unmeasured. |
| **Total from the panel front face** | **47.1** | `provisionalInstalledDepth()`; **45.5 mm behind the panel back** = `provisionalModuleDepthBehindPanel()`. |

With the Pi on its own panel, the J_FRONT male tails (nominal 1.40 mm
protrusion below the rear PCB) point into open air under the rear board. The
rear board carries no Pi holes (removed in the 2026-11 review fix). The 3.5 mm
`PI_SPACER_MM` is the candidate standoff of the **Pi mounting panel** only.

**Pi mounting panel (20HP), its panel back face to the rear-most point:**

| Segment | Provisional | Basis |
|---|---:|---|
| Pi standoff + Pi PCB | 5.1 | CANDIDATE 3.5 mm standoff (must clear the Pi underside/microSD slot; unmeasured) + the Pi's 1.6 mm PCB. |
| Pi cooler + M.2 HAT+ + SSD | 30 | Candidate range 20–30; the owner decision 2026-11-12 keeps the NVMe HAT. |
| Harness allowance | 10 | USB/DSI/ribbon/pigtail bends at the Pi; unmeasured. |
| **Behind the panel back** | **45.1** | `provisionalPiPanelDepth()`. |

The ≤110 mm target therefore holds on paper for both modules at the high end of
every candidate. That is arithmetic on unmeasured candidates, not a verified
installed depth; the check that keeps the chains inside the target is one of the
tests awaiting the respin audit (see [TODO.md](TODO.md)).

### Behringer Eurorack Go fit (design check, no approval)

Case data from the Thomann product page: 2 × 140 HP, 6U, M3 slide nuts, +12 V/3 A, −12 V/1 A, +5 V/1 A, chassis
112 × 723 × 311 mm, **maximum module depth 40 mm (lower row) and 62 mm (upper row)**. The module draws nothing from
the Eurorack bus.

- Width/height: 30HP (152.0 mm actual) + the 20HP Pi mounting panel (101.2 mm actual) = 50 HP of a 140 HP row; 128.5 mm fits 3U on both panels. Rail holes are at the Doepfer 3U positions
  Y ±61.25 after the 2026-11 fix (30HP panel: X ±68.58, spacing 137.16 = 27 × 5.08; 20HP panel: X ±43.18, spacing 86.36 = 17 × 5.08). The horizontal inset is 7.42 mm on both, i.e. 0.08 mm inside the 7.5 mm the Doepfer hole figure (A100FP.gif) draws; the vertical inset stays the figure's 3.0 mm with Ø3.2 mm holes, and the 128.5 mm height matches it. Round Ø3.2 holes suit the Go's sliding M3 nuts; slotted holes are still recommended. Rail nut/washer fit, neighbour-module fit and the print remain pending.
- Depth (two-module arrangement, owner decision 2026-11-12): the main module is **45.5 mm behind its panel back**
  (47.1 from the panel front face) and the Pi mounting panel is **45.1 mm behind its panel back**, both on the
  candidate chains above. Both sit inside the 62 mm upper row on paper; **both are over the 40 mm lower row**, so the
  two modules claim the upper row. The main module's 45.5 mm follows the 10.0 mm panel distance (2026-11-13) and is
  9 mm shallower than the 54.55 mm it replaced. Historical, single module: the stack was ≈63–75 mm behind the panel
  back (76.25 − 1.6 = 74.65 mm at the high end; the low end of the candidate ranges was 62.65 mm), which exceeded the
  40 mm lower row by 23–35 mm and sat at or over the 62 mm upper row before tolerances, ribbon folds, the Pico USB plug
  and airflow.
- **Verdict: the single module was NOT approved for the Eurorack Go (2026-11), and that verdict is why the Pi moved
  to its own panel. The two-module arrangement is the designed fit for the Go's upper row ON PAPER ONLY — no
  approval.** Every number above is a candidate: the harness runs, the ribbon fold, the Pico console USB plug,
  airflow and neighbour clearance are unmeasured. No fabrication release claims a case fit; the physical evidence is
  acceptance items `case_fit`, `pi_panel_harness` and `zstack`. The ≤110 mm target was never compatible with a 40 mm
  row.

### Pi mounting panel and the inter-module harness (owner decision 2026-11-12)

The Pi 5 (Active Cooler + M.2 HAT+ + NVMe SSD) bolts to the **rear face** of
its own pure-mechanical 20HP × 3U FR-4 panel (`tscircuit/pi-panel.circuit.tsx`,
geometry `PI_PANEL_*` in `lib/layout.ts`). The panel carries no copper and no
components; JLCPCB fabricates it like the 30HP panel. The module sits **beside
the main module in the same row** (the added HP), not behind it. The Pi is
**landscape** with its component side away from the panel, so the panel sees
it mirrored: the GPIO header runs along the panel's top edge (ribbon to
`J_PI`), USB-C and micro-HDMI face the bottom edge, USB-A ×2 and RJ45 face the
left edge (3.3 mm beyond the board), and the drawing's left edge (microSD side,
1.7 mm beyond the board) faces the right edge. All plugs insert in the panel
plane. The DSI and PCIe connector positions are not established by the drawing
and are not claimed.

**USB stays on the 30HP panel** (no USB opening is added to the mounting panel), but the three feedthroughs **moved
on 2026-11-13**: they are at x −52 / −23.5 / +11 (see the respin table). The harness between the two modules is six
runs, all PLANNING lengths (the modules sit side by side, so the run is a lateral route across the row;
150–350 mm is a planning estimate for a chosen placement, not a measurement); nothing is measured or proven:

| Run | From | To | Open points |
|---|---|---|---|
| USB-C power feed | 30HP panel feedthrough (x −52) | Pi USB-C inlet | Must be 5 A-rated end to end. The feedthrough itself is unresolved (`usb_c_5a_path`: CAB-15455 is 3 A and rejected); droop and heating over the longer run are `usb_pd_droop_thermal` + `pi_panel_harness`. |
| UMC AUDIO USB | feedthrough (x −23.5) | Pi USB-A | USB 2.0 extension; shielded, keep ≤1 m. |
| MIDI HUB USB | feedthrough (x +11) | Pi USB-A | Same. |
| Display DSI FPC | display FPC connector on the REAR board's carrier (planning marker only) | Pi MIPI connector (22-pin) | The kit ships 50 mm and 120 mm cables; a longer 15-pin 0.5 mm FPC plus the kit's 22-pin adaptation is expected. Contact orientation (A/A vs A/B), latch, bend radius, touch I²C and DSI signal integrity over the fitted length are unverified. |
| 40-wire GPIO ribbon | `J_PI` (rear board, BH254V-40P) | Pi 40-pin header | 1:1 IDC. Pin-1 orientation and all-40 continuity to the net list are required before power. **The M.2 HAT+ must not occupy the GPIO header the ribbon needs**: the fitted HAT's MPN and its GPIO/PCIe connection method decide whether a pass-through or stacking arrangement is needed. |
| J_PWR pigtail | front board `J_PWR`, rear face at (64, 44.5) in the right leg | Pi `J2` bare pads | Longer than the recorded 2 × 6-inch DuPont pair; strain relief and a qualified connection to the bare `J2` pads stay pending (`power_header_mount`). |

The bundle leaves the main module through the feedthrough bracket's three
cable slots (15.6 / 17.9 / 17.9 mm wide); their existing strain-relief holes
can take the tie. The mounting panel itself needs no cable cutout: every run
reaches the Pi module. Acceptance item `pi_panel_harness` carries the
evidence list.

### Panel power header vs feedthrough bracket (unresolved after the respin; `power_header_mount` pending)

J_PWR is a 90 deg through-hole 1 x 2 header on the front board's **REAR face**, at **(64, 44.5)**, rotation 180, mating
axis +Y (`POWER_HEADER`). The owner moved it there on 2026-11-13 together with the POWER button: it no longer has to sit
on an edge or in the top relief notch (which no longer exists as a separate cut), and its plug lives in the 11.20 mm
board gap instead of a notch void. Its 5.08 mm plug spans x 63.5...68.5, between SW7's two pad rows.

The **bracket pocket recorded on 2026-11-10 is obsolete**. `BRACKET_POWER_POCKET` is derived from
`POWER_HEADER_KEEPOUT`, which now derives to x 60.19...67.81, y 41.96...63.40 - entirely **outboard of the bracket bar**
(which ends at x 58). What is still true: the bar covers y 45...56 on the same rear face, so the plug, its cable bend and
the 4.80 mm body still have to clear printed material wherever the carrier/bracket is re-drawn. Regenerate the bar from
`layout.ts`, re-derive any pocket, and re-run the CAD self-check before printing; `lib/power-header.test.ts` currently
still asserts the pre-respin placement and is part of the test audit (see [TODO.md](TODO.md)).

Still open and unchanged by the move: the exact hole-to-housing offset (the 4.80 mm datum is unclear, sheet 1 of 2
missing), the 1.60 mm pad and 8.00 mm plug reach as engineering assumptions, the generic DuPont-style plug, soldering and
plugging access, the UMC AUDIO cable tail in the adjacent slot, and sourcing (`digikey.se` returned Cloudflare 403 on
2026-11-10; no SKU or stock claimed). The Electrokit breakaway strip was proposed by the researcher and **not selected**
by the owner. The Pi end is two bare pads (`J2`), so a fitted header or a qualified wired connection is still required.

### Feedthrough bracket

The 3 mm PETG bar on the rear side of the front board: outline x −63…58, y **45…56** (121 × **11.0** mm), mount
holes (−60, 46.5) and (55, 46.5) — **no longer drilled in the board**, since all board mounting holes were removed on
2026-11-13 — and three open-top cable slots sized from the vendor panel cutouts in **both** directions. Everything is
derived from `lib/layout.ts`, so the respin moved the slots with the feedthroughs:

| Slot | x | width | floor y |
|---|---:|---:|---:|
| USB-C POWER | −52 | 15.6 | 48.20 |
| UMC AUDIO USB | −23.5 | 17.9 | 48.35 |
| MIDI HUB USB | +11 | 17.9 | 48.35 |

Each floor is its own feedthrough's declared opening bottom less 1.5 mm. The bar also carries Ø6.5 head-clearance circles
around the two top front-rear spacers at y 46 and four Ø3.2 cable-tie holes in the webs. The panel opening does not
prove the unmeasured rear body, flange or cable bundle.

**The exported geometry is stale.** `mechanical/layout.json` was last generated at commit `6cee601` and still holds the
old feedthrough x (−38 / −5 / +35), the old spacer grid and the old bracket pocket, so `mechanical/feedthrough_bracket.py`
and `step_assembly.py` would build the pre-respin part. Run `bun run scripts/export-mechanical.ts` and the bracket
generator, then the CAD self-check (`mechanical/feedthrough_bracket.py` + `mechanical/bracket_geometry.py`, not run by
`make test` because CadQuery is optional), before any print or fabrication claim. The bar's slots are also 15.6 / 17.9 /
17.9 mm wide against 12.6 / 14.9 / 14.9 mm cutouts, so a printed fit is still unmeasured.

### STEP assembly and printable mockups (verification only)

`mechanical/step_assembly.py` builds a named-solid STEP assembly and individual
1:1 panel/front-board/rear-board STL mockups from `mechanical/layout.json`, plus
a printer scale bar and a light-pipe reach gauge. The flange-versus-board result
stays **UNDETERMINED**: the projection is XY only and the Adafruit 908 drawing
states no flange depth. **The exported `layout.json` is stale** (last written at
commit `6cee601`), so the mockups currently show the pre-respin boards, notch
and feedthrough positions; regenerate it before drawing any conclusion from
them. `make mechanical` runs the pure tests; the CAD exports are gitignored and
regenerated on demand. Paths, the collision rule, the vendor STEP hash check and
the print checklist are in [mechanical-cad.md](mechanical-cad.md).

## Hardware process notes

- **Encoder (Bourns PEC11R-4015F-S0024, C143789):** the manufacturer does not recommend hand soldering this part; it needs a
  qualified soldering process (controlled iron temperature, preheat, limited dwell) or factory assembly. Treat "THT,
  hand-solder" as *not* automatically acceptable for it.
- **USB-C panel feedthrough:** the current candidate (SparkFun CAB-15455) is rated **3 A**, not 5 A. Do not add a 5 A
  claim to it, and do not change the owner's power requirement on the strength of it. The replacement is a pending
  part choice, gated on a current/PD test; see [assembly.md](assembly.md) and `usb_c_5a_path` in
  [acceptance.md](acceptance.md).

## Before freezing the layout

Acquire and measure: the display (rear connectors, underside map, FPC latch, mounting bosses), the Adafruit 908 and
USB-C feedthrough bodies and flanges, the M2.5/M3 hardware envelopes, the display carrier and its 8.75 mm spacers, a
~9.2 mm-class light pipe, the Pi 5 sample, the cooler/M.2 HAT+/SSD stack, and every cable bend. Build the stack
mock-up, then re-run the gates. No increase of panel width or installed depth without an explicit owner decision.

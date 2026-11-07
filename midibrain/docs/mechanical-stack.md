# Mechanical arrangement — provisional, not a clearance approval

Coordinates are mm with panel centre (0, 0), +X right and +Y up looking at the **front**. Every number here is a
planning value from `tscircuit/lib/layout.ts`, which is the single source; nothing below is a measurement or a fit
approval. Verification record and open gates: `docs/MECHANICAL-VERIFICATION.md`.

**Z datum: Z = 0 is the front (operator-side) face of the panel; +Z points rearward into the rack cavity.** PCB
FRONT / CAD top faces the panel; REAR / CAD bottom faces the rack cavity. Looking from behind reverses the apparent
X direction.

## Approved envelope (owner decision, unchanged)

| Item | Value | Note |
|---|---|---|
| Front panel | **32HP × 3U, 162.56 × 128.5 × 1.6** (owner decision 2026-11-07) | Rail holes X ±73.78, Y ±61.25 (7.5 / 3.0 mm edge insets, Doepfer 3U rule). Display window 107.22 × 68.87 mm centred on the module. |
| Front/UI PCB | **128 × 112 × 1.6 (unchanged)** | Four display standoff holes on the drawing's mounting pattern: 25.62 / 22.38 and 9.50 / 9.46 edge insets → 58.00 × 49.04 mm, i.e. layout x −27.38 / +30.62, y +24.50 / −24.54 from the module centre (−9.5, 11). |
| Rear/base PCB | 101.6 × 112 × 1.6 (20HP) | Pi hole grid 58 × 49 mm (portrait), Pico on the left. |
| Installed depth target | ≤110 mm | Target retained. Not verified; see the depth section. |

Owner control centres are unchanged and must not be moved: transport buttons REC/OVER DUB/STOP/PLAY at
x = −53/−29/−5/+19, y = −34; PLAY START at (47, −34); TEMPO (47, 30); NAV (47, 11); SHIFT/EXIT (47, −8); LEDs above
REC/OVER DUB/PLAY at y = −24; MIDI jacks at y = −47.5 in the four transport columns; display centre (−17, 11).

## Provisional Z stack

The chain rests on two **explicit assumptions** and the vendor depth figures. It does not certify a gap.

- Assumption A: the display glass front sits flush with the panel front face (Z = 0). Nothing has measured this.
- Assumption B: the display standoff is **9.0 mm** (front-board top face to the display PCB **rear** face).
- Vendor figures, from the 2D drawing **and** the 3D model (both primary): 6.7 mm from the **lens front face to the
  display PCB rear face**, then 4.0 mm from that PCB rear face to the **mounting-boss ends**, which are the rear-most
  feature; overall 10.7 mm = 6.7 + 4.0. The earlier note called 6.7 the "rear envelope" and 4.0 the "forward envelope";
  that assignment was reversed and is withdrawn, and the 4.0 mm is not a connector-only figure.

| Feature | Z (mm) | Basis |
|---|---:|---|
| Panel front / rear faces | 0 / 1.60 | 1.6 mm FR-4. |
| Display glass front | 0.00 | Assumption A, not measured. |
| Display mounting PCB | 4.00 | Forward envelope 4.0 below assumption A. |
| Display PCB rear face | 6.70 | Vendor: lens front face to PCB rear. |
| Display rear-most point (connector) | 10.70 | Vendor overall depth 10.7. |
| Front-board top face | 15.70 | Display PCB rear face + 9.0 mm standoff (assumption B). |
| Front-board rear face | 17.30 | 1.6 mm board. |
| Panel rear → front-board top | 14.10 | Falls out of A and B; a consequence, not evidence. |

**Spacer length is not the plane separation.** The spacer screws onto the boss ends, which already reach 4.0 mm below the
display PCB rear face, so:

| Quantity | Provisional value | Meaning |
|---|---:|---|
| Plane separation | 9.0 mm | Front-board top face to display PCB **rear face**. |
| Boss projection | 4.0 mm | Boss ends stand 4.0 mm behind that PCB rear face (primary model). |
| Spacer length | 5.0 mm | What to order: plane separation − boss projection. |

Ordering a 9 mm spacer while intending a 9 mm plane separation would add the boss projection a second time and push the
display through the panel. The old `3.5 mm` may have meant the free length beyond the bosses; that reading is withdrawn in
favour of the two explicit numbers. Both remain provisional.

**One standoff definition only.** The earlier `3.5 mm` value in `layout.ts` and the `8–10 mm` figure in the older
text described different quantities at the same time; `3.5 mm` is withdrawn and the separate 3 mm "additional
clearance spacer" is gone because it double-counted the same gap. 9.0 mm is provisional: it is the middle of the old
8–10 mm candidate range, and the 6.7 mm from lens front to PCB rear plus the 4.0 mm connector depth fit inside it.

**Display underside — pending, no clearance claimed.** The display rear-most point (10.70) is 5.00 mm from the
front-board top face. The tallest front-side part with a manufacturer-stated height is the MIDI receiver in the
Toshiba 11-4L1S body, **maximum installed height 2.1 ± 0.1 mm (drawing p. 15)**, i.e. 2.2 mm, which leaves 2.80 mm over
that part. That figure does **not** prove the display underside: the display's own underside component map, its 15-pin
FPC latch and the module tolerance are unmapped. Keep this as a gate.

**Light pipe — reach pending, and now clearly short.** The Bivar PLP2-400-C is 10.16 mm long and the provisional
panel-to-board gap is 14.10 mm, so 3.94 mm is unaccounted. No LED height or seat allowance is recorded, because neither
is measured. Check the Bivar drawing and the actual LED, and expect a different (longer) light pipe to be needed.

## LCD aperture (panel cutout)

| Property | Value | Reason |
|---|---:|---|
| Size | 90.67 × 54.36 | **LENS OD** 88.87 × 52.56 plus 0.9 mm per side. The 86.5 × 56.0 PCB never passes the panel, so the PCB height must not size the aperture. |
| Corner relief | 1.0 mm radius | A routed inside corner cannot be sharp; emitted as an explicit rounded polygon. |
| Clearance per side | 0.90 | 0.2 mm JLCPCB profile tolerance + 0.2 mm vendor GLASS tolerance (all charged to one side) + 0.5 mm unverified registration/assembly allowance. |
| Corner clearance | 0.906 measured | Polygon chords lie inside the true arc, so the aperture is slightly smaller than the exact rounded rectangle; measured on the built panel by `scripts/check_placement.ts` at the four GLASS corners, above the 0.5 mm target. |

### 4.3-inch DSI LCD: measured numbers, both sources

| Feature | Value | Source |
| --- | --- | --- |
| Module PCB outline | 106.00 × 68.00 × 1.6 mm | vendor drawing (drawn) / STEP plane pair z 0 → 1.6 |
| Glass front-face outline | **105.42 × 67.07 mm** | reference STEP, cover-glass front face (16-point loop at z 2.900 / 0.000) |
| Front frame around the glass | 106.10 × 67.80 mm | reference STEP (faces at z 3.80 / 4.80) |
| Active area (VA) | **95.04 × 53.856 mm** | 800 × 0.1188 / 480 × 0.1122 mm pixels; STEP VA face 95.04 × 53.86 at z 2.900 |
| Printed bezel opening | 95.54 × 54.36 mm | STEP plane = VA + 0.25 mm per side |
| Mounting pattern (used) | 58.00 × 49.04 mm, insets 25.62 / 22.38 / 9.50 / 9.46, 4× Ø4.30 | vendor drawing; STEP circles r2.15 |
| Outer hole pattern (alternative) | 99.30 × 61.80 mm, insets 3.35 / 3.10, 4× Ø4.30 | vendor drawing; STEP `SMTSO-M2_5-4ET` SMT bosses sit here |
| Overall thickness | **14.05 mm (wiki FAQ)** vs **13.82 mm (STEP bbox)** | Both recorded: the wiki number is the vendor's stated maximum, the STEP bbox is the modelled geometry (106.10 × 68.36 × 13.82). The 0.23 mm difference is unallocated; the stack uses the LARGER (14.05) so the panel gap cannot be optimistic. |
| Panel-to-front-board gap | **17.45 mm provisional** | Assumption A (glass front flush with the panel front face, z = 0) + Assumption B (9.0 mm plane separation to the PCB rear) + 10.05 mm glass-front-to-PCB rear + 1.60 mm panel − 1.60 mm panel … see `PROVISIONAL_Z` in `tscircuit/lib/layout.ts`. Was 14.10 mm for the withdrawn 3.5-inch module. |
| Light-pipe reach | 10.16 mm pipe vs 17.45 mm gap | **7.29 mm unaccounted, PENDING**: a longer pipe (or a different optical path) is required; no LED height or seating allowance is claimed. |

Display window (owner decision D1): the aperture follows the GLASS, not the 106 × 68 PCB envelope, and not the
95.54 × 54.36 printed border. Window = glass + 0.9 mm per side = 107.22 × 68.87 mm, centred on the module at
(−9.5, 11). Vertical fit: the glass-derived window is 68.87 mm against a 65.10 mm free band when the LED light pipes
sit above the buttons, so the authorised fallback (3) was applied — the REC / OVER DUB / PLAY light pipes moved 12 mm
beside their buttons. The band is then +49.70 (feedthrough cutout floor) to −27.80 (transport-button opening top) =
77.50 mm, and the window keeps 4.365 mm to the button openings, 1.165 mm to the feedthrough labels (moved from y 44.0
to 47.5) and 2.865 mm … 7.935 mm of panel web around it. Horizontal: the window left edge −63.11 keeps 9.07 mm to
the M3 rail-hole edge (72.18) and 18.17 mm to the panel edge (−81.28); the module left edge −62.5 keeps 1.5 mm to
the 128 mm board edge and the module right edge +43.5 keeps 2.25 mm to the value-knob body (Ø12.5 at x = 52).

Reference CAD path: `mechanical/reference/waveshare-4.3inch-dsi-lcd.step` (STEP SHA-256
`01eef9172ff4364691f4de37ff98f95437e96caa647b94b4bd4a1c54ac4519ed`, downloaded from the wiki's
`4.3inch_DSI_LCD_Drawing.zip`, archive SHA-256 `f75cb7ad…`, provenance in
`mechanical/reference/waveshare-4.3inch-dsi-lcd.source.json`). CadQuery is not installed in the build image, so
`mechanical/tools/measure_display_step.py` reads the ISO-10303-21 text directly and prints every solid's bounding box
plus the front-facing planar faces; the glass and bezel numbers above come from that script.

Kit contents and the standoff row: the box carries three DSI cables — **50 mm** and **120 mm** 15-pin→22-pin (the Pi 5
DSI1 interface) and a 160 mm 15-pin→15-pin cable that does NOT fit DSI1 — plus screws and copper pillars. The module
itself carries M2.5 SMT standoffs (`SMTSO-M2_5-4ET`, 4 mm class) on the outer 99.30 × 61.80 pattern; whether OUR
spacers land on the module's threaded bosses or on the inner through-hole pattern is a first-article question, and the
`DISPLAY_STANDOFFS` / `DISPLAY_BOSS_PATTERN` data pair lets the answer be switched without re-deriving coordinates.
The 9.0 mm plane separation + 5.0 mm spacer remains provisional against the kit pillars.

Primary sources for the display: the 2D drawing `3.5_inch_DSI_LCD_H-2D-PDF.pdf` and the 3D model
`3.5_inch_DSI_LCD_H_3D-STP.zip` → `3.5 inch DSI LCD (H)_3d-20251129.stp`. The model was read with CadQuery 2.8.0: lens
front plane z = 0 spans x ±44.435, y ±26.280; the PCB rear plane z = −6.7 spans x −43.38…43.12 (centre −0.13) and
y ±28.0; four bosses (body r2.75, M2.5 thread r1.25) reach z −10.7 at x −39.88/+18.12, y ±24.5. The model also reaches
x 44.635, i.e. 0.200 mm past the lens OD, inside the panel thickness zone; the 0.9 mm per-side allowance still leaves
0.7 mm there.

The withdrawn 89.2 × 56.3 mm aperture left 0.165 mm horizontally and 0.15 mm vertically per side, inside the ±0.2 mm
fabrication tolerance, and it sized the height from the PCB instead of the lens. It is not to be restored.

**Active area and silkscreen window.** The LENS VA is 75.8 × 45.36 mm with long-axis borders 3.63 mm and 9.44 mm, so the
active centre sits (3.63 − 9.44)/2 = −2.905 mm from the lens centre in the **raw model frame**. The layout rotates that
model 180° about Z (so the 25 mm PCB margin sits at front-view LEFT), which makes the layout offset **+2.905 mm, i.e. to
the right**; the model agrees independently (raw active face x −40.805…+34.995). Only the front-board *silkscreen* window
uses this value: the panel aperture comes from the symmetric lens outline, so no mechanical feature depends on it. Glass
centre (−17, 11) and all control centres are unchanged.

**Mounting holes.** The four display bosses, not the PCB corners, are the mounting pattern: after the 180° rotation they
sit at x −18.12 and +39.88, y ±24.50 (58 × 49 mm pitch). The earlier −18.25 / +39.75 omitted the 0.13 mm PCB-to-lens
offset, which places the PCB centre at +0.13. That puts the 25.00 mm margin on the LEFT and 3.50 mm on the right, as the
chosen orientation intends. Physical first article still has to confirm the pattern. `tscircuit` has no rounded-rectangle cutout, so `panel.circuit.tsx`
emits `roundedRectOutline()` from `lib/layout.ts` as a polygon.

## Board-to-board relationship

Both boards are centred on their own origin, so the 128 mm front board overhangs the 101.6 mm rear board by
**13.2 mm per side**, not 26.2 mm on one side. The four front↔rear M2.5 spacers at (±44, +43/−52) sit inside the rear
board (edge ligament 5.45 mm). The right-hand control column at x = 47 is on the rear board outline but its hardware
overhangs; a left-flush rear board (26.4 mm one side) is a different, unchosen arrangement and would be an owner
decision.

- Front↔rear spacers: length TBD (~18–20 mm pending the J5 IDC bend mockup). The top pair at (±44, 43) sits inside
  the feedthrough bracket envelope, so the printed bracket carries head-clearance slots there; the M2.5 head/washer
  envelope is unmeasured.
- Panel↔front standoffs: 4 × M2.5 at (±61, ±53), 1.65 mm edge ligament; length TBD.
- J5 corridor (X ≈29–39, Y −10…+30) clears the display PCB edge (~26.3) by ~3 mm and the control column (~39.5) by
  ~1 mm. IDC plug overhang and solder-tail protrusion still need a physical sample.
- LCD top (39.0) to bracket bottom (41.5): 2.5 mm. Display left (−61.44) to front-PCB edge (−64): 2.56 mm.
- C5 (50, 46) against the right bracket mount (55, 46.5): about 1 mm of screw-head clearance; sample check.

## Provisional installed-depth chain

| Segment | Provisional | Basis |
|---|---:|---|
| Front-board rear face | 17.3 | See the Z stack. |
| Front↔rear gap (J5 IDC plug + cable bend) | 20 | Candidate range 18–22. |
| Rear/base PCB | 1.6 | 1.6 mm FR-4. |
| Pi spacer + Pi PCB | 4 | Candidate. |
| Pi cooler + M.2 HAT+ + SSD | 30 | Candidate range 20–30. |
| **Total from the panel front face** | **72.9** | Sum of the above. |

The ≤110 mm target therefore holds on paper at the high end of every candidate, and `lib/layout.test.ts` keeps the
chain inside it. That is arithmetic on unmeasured candidates, not a verified installed depth: no cooler/HAT/SSD,
cable-bend or airflow model exists.

### Behringer Eurorack Go fit (design check, no approval)

Case data from the Thomann product page: 2 × 140 HP, 6U, M3 slide nuts, +12 V/3 A, −12 V/1 A, +5 V/1 A, chassis
112 × 723 × 311 mm, **maximum module depth 40 mm (lower row) and 62 mm (upper row)**. The module draws nothing from
the Eurorack bus.

- Width/height: 32HP (162.56 mm) fits a 140 HP row with 2 × (140 − 32) HP of space left; 128.5 mm fits 3U; rail holes are at the Doepfer 3U positions
  Y ±61.25 after the 2026-11 fix. Round Ø3.2 holes suit the Go's sliding M3 nuts; slotted holes are still recommended.
- Depth: the provisional stack is **≈59–71 mm behind the panel back** (72.9 − 1.6 = 71.3 mm at the high end, and
  ≈59.3 mm at the low end). That exceeds the 40 mm lower row by 19–31 mm and is at or over the 62 mm upper row
  before tolerances, ribbon folds, the Pico USB plug and airflow.
- **Verdict: not approved for the Eurorack Go. Do not plan this module for that case.** No fabrication release until
  the case is reselected or the Pi/cooler/NVMe stack leaves the rack. The ≤110 mm target was never compatible with a
  40 mm row.

### Feedthrough bracket

The 3 mm PETG bar on the rear side of the front board, spanning the top relief notch: outline x −63…58, y 41.5…56
(121 × 14.5 mm), mounts (−60, 46.5) and (55, 46.5), three open-top cable slots sized from the vendor panel cutouts
in **both** directions — 15.6 / 17.9 / 17.9 mm wide at x = −38 / −5 / +35, floors at y = 48.20 / 48.35 / 48.35 —
head-clearance slots Ø6.5 at (±44, 43) for the top front↔rear spacer hardware, and four Ø3.2 cable-tie holes in the
webs. Each slot floor is its own feedthrough's declared opening bottom less 1.5 mm, kept above the board notch bottom
(48.0); the panel opening does not prove the unmeasured rear body, flange or cable bundle. All of it is derived from `lib/layout.ts` and
generated into `mechanical/layout.json`; the first draft hardcoded slots at −48/−13/+22 and mounts at −61.5/40 with
20/40/40 mm slots that touched each other. Details, reasons and the self-check: `docs/feedthroughs.md` and
`docs/MECHANICAL-VERIFICATION.md`.

## Hardware process notes

- **Encoder (Bourns PEC11R-4215F-S0024):** the manufacturer does not recommend hand soldering this part; it needs a
  qualified soldering process (controlled iron temperature, preheat, limited dwell) or factory assembly. Treat "THT,
  hand-solder" as *not* automatically acceptable for it.
- **USB-C panel feedthrough:** the current candidate (SparkFun CAB-15455) is rated **3 A**, not 5 A. Do not add a 5 A
  claim to it, and do not change the owner's power requirement on the strength of it. The replacement is a pending
  part choice, gated on a current/PD test; see `docs/MECHANICAL-VERIFICATION.md` for the requested sourcing change.

## Before freezing the layout

Acquire and measure: the display (rear connectors, underside map, FPC latch, mounting bosses), the Adafruit 908 and
USB-C feedthrough bodies and flanges, the M2.5/M3 hardware envelopes, the Bivar light pipe, the Pi 5 sample, the
cooler/M.2 HAT+/SSD stack, and every cable bend. Build the stack mock-up, then re-run the gates. No increase of panel
width or installed depth without an explicit owner decision.

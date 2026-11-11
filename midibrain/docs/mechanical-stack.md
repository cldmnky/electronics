# Mechanical arrangement — provisional, not a clearance approval

Coordinates are mm with panel centre (0, 0), +X right and +Y up looking at the **front**. Every number here is a
planning value from `tscircuit/lib/layout.ts`, which is the single source; nothing below is a measurement or a fit
approval. Open physical gates: [acceptance.md](acceptance.md) and [TODO.md](TODO.md).

**Z datum: Z = 0 is the front (operator-side) face of the panel; +Z points rearward into the rack cavity.** PCB
FRONT / CAD top faces the panel; REAR / CAD bottom faces the rack cavity. Looking from behind reverses the apparent
X direction.

## Approved envelope (owner decision, unchanged)

| Item | Value | Note |
|---|---|---|
| Front panel | **30HP × 3U, 152.0 × 128.5 × 1.6 actual** (owner decision 2026-11-08, replaces the 32HP/162.56 mm panel of 2026-11-07) | Rail holes X ±68.58, Y ±61.25, Ø3.2 (spacing 137.16 = 27 × 5.08, the Doepfer multiple rule; 7.42 mm centre inset, 5.82 mm hole edge to panel edge). 152.0 is OUR ESTIMATE, 0.4 mm under the 152.4 mm nominal (0.2 mm per side): not a Doepfer-stated 30HP value, not owner-exact, not a mandate and not a guaranteed fit. Sources: [Doepfer A-100 mechanical information](https://doepfer.de/a100_man/a100m_e.htm) and its [hole figure A100FP.gif](https://doepfer.de/a100_man/A100FP.gif), which gives 128.5 mm height, a 7.5 mm horizontal and 3.0 mm vertical hole inset, and Ø3.2 mm holes but publishes no 30HP width. The fixed 27 × 5.08 grid on 152.0 mm gives 7.42 mm, i.e. **0.08 mm tighter than the figure's 7.5 mm** - disclosed, not corrected. Display window 107.22 × 68.87 mm centred on the module, unchanged. |
| Front/UI PCB | **128 × 112 × 1.6 (unchanged)** | Four display mounting holes on the module's threaded SMTSO boss pattern: 99.30 × 61.80 mm centres (3.35 / 3.10 insets), i.e. layout x −49.65 / +49.65, y +30.90 / −30.90 from the module centre (−9.5, 11). The drawn 58.00 × 49.04 plain-hole pattern stays as the recorded alternative (`DISPLAY_STANDOFFS`). |
| Rear/base PCB | 101.6 × 112 × 1.6 (20HP) | Pi hole grid 58 × 49 mm (portrait), Pico on the left. |
| Installed depth target | ≤110 mm | Target retained. Not verified; see the depth section. |

Owner control centres are unchanged and must not be moved: transport buttons REC/OVER DUB/STOP/PLAY at
x = −53/−29/−5/+19, y = −34; PLAY START at (52, −34); VALUE (52, 30); NAV (52, 11); SHIFT/EXIT (52, −8); LEDs 12 mm
beside REC/OVER DUB/PLAY at y = −34; MIDI jacks at y = −47.5 in the four transport columns; display centre (−9.5, 11).

## Provisional Z stack

The chain rests on two **explicit assumptions** and the vendor depth figures. It does not certify a gap.

- Assumption A: the display glass front sits flush with the panel front face (Z = 0). Nothing has measured this. The
  vendor model puts its touch/frame forward face **1.90 mm in front of the glass front face**, so an illustrative
  placement with the glass at Z = 0 puts that frame at Z = −1.90 mm (1.90 mm proud of the panel). That is a property of
  the supplied model, not a fit: the real touch layer, frame relief and seating are unmeasured.
- Assumption B: the display standoff is **9.0 mm** (front-board top face to the display PCB **rear** face).
- Vendor figures for the 4.3-inch module: **14.05 mm overall** (wiki FAQ, the vendor's stated maximum), split
  provisionally as 10.05 mm from the **glass front face to the display PCB rear face** plus the **4.0 mm** boss
  projection behind it. Only the 4.0 mm is a vendor-modelled number; the 10.05 mm is derived (14.05 − 4.0), not
  measured, and the split is the thing the first article replaces with two direct measurements. The reference STEP's
  union bbox is 13.82 mm; see the thickness resolution in the display table below.

| Feature | Z (mm) | Basis |
|---|---:|---|
| Panel front / rear faces | 0 / 1.60 | 1.6 mm FR-4. |
| Display glass front | 0.00 | Assumption A, not measured. |
| Display mounting PCB | 8.45 | Glass front + 10.05 − 1.6 mm PCB (provisional split). |
| Display PCB rear face | 10.05 | Glass front + 10.05 mm (provisional split, not measured). |
| Display rear-most point (boss ends) | 14.05 | 10.05 + 4.0 mm boss projection; equals the wiki overall depth. |
| Front-board top face | 19.05 | Display PCB rear face + 9.0 mm standoff (assumption B). |
| Front-board rear face | 20.65 | 1.6 mm board. |
| Panel rear → front-board top | 17.45 | Falls out of A and B; a consequence, not evidence. |

### The vendor model's own planes — measured, and separate from the two chains

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
display PCB rear face, so:

| Quantity | Provisional value | Meaning |
|---|---:|---|
| Plane separation | 9.0 mm | Front-board top face to display PCB **rear face**. |
| Boss projection | 4.0 mm | Boss ends stand 4.0 mm behind that PCB rear face (primary model). |
| Spacer length | 5.0 mm (design-chain candidate only) | What that chain would order: plane separation − boss projection. **Not** the vendor model's gap: the drawn boss ends sit at global Z 10.24 and the drawn board top face at 19.05, a **model gap of 8.81 mm**. A 5.00 mm spacer anchored on the board face ends at 14.05 and misses those boss ends by **3.81 mm**. The two chains do not agree and neither is approved; the correct length, diameter, thread and assembly are first-article tasks. The STEP model draws four non-approved placeholders over the 8.81 mm gap so the mount is visible. |

Ordering a 9 mm spacer while intending a 9 mm plane separation would add the boss projection a second time and push the
display through the panel. The old `3.5 mm` may have meant the free length beyond the bosses; that reading is withdrawn in
favour of the two explicit numbers. Both remain provisional.

**One standoff definition only.** The earlier `3.5 mm` value in `layout.ts` and the `8–10 mm` figure in the older
text described different quantities at the same time; `3.5 mm` is withdrawn and the separate 3 mm "additional
clearance spacer" is gone because it double-counted the same gap. 9.0 mm is provisional: it is the middle of the old
8–10 mm candidate range, and the 10.05 mm from glass front to PCB rear plus the 4.0 mm boss projection fit inside it.

**Display underside — pending, no clearance claimed.** The display rear-most point (14.05, the boss ends) is 5.00 mm
from the
front-board top face. The tallest front-side part with a manufacturer-stated height is the MIDI receiver in the
Toshiba 11-4L1S body, **maximum installed height 2.1 ± 0.1 mm (drawing p. 15)**, i.e. 2.2 mm, which leaves 2.80 mm over
that part. That figure does **not** prove the display underside: the display's own underside component map, its 15-pin
FPC latch and the module tolerance are unmapped. Keep this as a gate.

**Light pipe — owner-selected candidate, one datum, optical gap unknown.** The candidate is the Signal Construct
**PGN25L165** (manufacturer drawing <https://static.rapidonline.com/pdf/07-1015_v1.pdf>, SHA-256
`5350ad1f0a795e0d688dc74e223239c82cfb3e27951244a10cb43873eeeaa46b`, retrieved 2026-11-09). The drawing gives
shaft Ø2.5 ±0.05 mm, **x = 16.5 ±0.1 mm from the head underside to the tip**, head Ø3 ±0.05 mm, head height 1.0 mm,
three longitudinal ribs and a **Ø2.6 −0.05 mm mounting hole** (2.55-2.60 mm). The owner's screenshot for this MPN
states **17.5 mm overall**, which is 16.5 + 1.0. The panel openings are therefore 3 × Ø2.6 mm nominal, at their
unchanged LED positions (−41 / −17 / +31, y −34).

Datum: panel FRONT face Z = 0, +Z rearward. With the head underside on the panel front face — an **assumption, not a
measurement** — the nominal tip plane is **16.5 mm**, and the provisional board near face at 19.05 mm puts the tip
**2.55 mm** in front of that plane. That is arithmetic between two assumed planes and **not the LED-to-pipe optical
gap**. The LED height, brightness/coupling, retention in 1.6 mm FR-4, the real hole tolerance and the stack tolerances
are all unmeasured, so the gap is **null** and the status **incomplete**. No bound follows from the ±0.1 mm shaft
tolerance alone.

The earlier Bivar **PLP2-400-C 10.16 mm** reference and the `17.45 − 10.16 = 7.29 mm` subtraction are **withdrawn**:
the 10.16 mm value is not in the current Bivar datasheet, that family's Ø3.3 mm flange falls through the old Ø4.0 mm
opening, and the subtraction compared different reference planes. Neither 17.45 − 16.5 nor 17.45 − 17.5 replaces it.

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
| Panel-to-front-board gap | **17.45 mm provisional** | Assumption A (glass front flush with the panel front face, z = 0) + Assumption B (9.0 mm plane separation to the PCB rear) + 10.05 mm glass-front-to-PCB rear + 1.60 mm panel − 1.60 mm panel … see `PROVISIONAL_Z` in `tscircuit/lib/layout.ts`. Was 14.10 mm for the withdrawn 3.5-inch module. |
| Light-pipe reach | PGN25L165: shaft 16.5 ±0.1, overall 17.5, head 1.0 | Nominal tip plane **16.5** from the panel front, **2.55** to the provisional 19.05 board plane — arithmetic, not an LED gap. Optical gap **null**, status **incomplete**; LED height, retention and stack tolerances pending. The retired 10.16 / 7.29 figures are **withdrawn**. |

**Connector model check (2026-11-08, corrected).** The STEP's XCAF label tree shows the two 21.412 × 4.826 mm bodies are `EXTRUDED-106739` / `EXTRUDED-107488` inside `TFT1_ASM` — TFT flex bonds, **not** a panel connector. An earlier revision treated them as the FPC connector and inferred a ~24 mm disagreement with the drawing; that reading is withdrawn. The nearest named body to the recorded position is `DISPLAY_CAM` (4.822 × 22.400 × 5.680 mm, vendor x −51.975…−47.153, y −13.570…8.830, z −9.020…−3.340), placed centre (40.064, 11.000) mm against the recorded (38.837, 11.700) mm — an observation of 1.413 mm, **not a match**. Identity, pin count, electrical side, the long-axis wording and the rear/front frame mapping all stay unverified and need independent re-derivation. The recorded position does not move the PCB. Whether the connector, plug or first bend intersects the front board's top-side parts remains unknown and needs a first-article check. The CAD model draws the connector only as `DISPLAY_CONNECTOR_PLANNING_MARKER` on the provisional Z 10.05 plane.

Display window (owner decision D1): the aperture follows the GLASS, not the 106 × 68 PCB envelope, and not the
95.54 × 54.36 printed border. Window = glass + 0.9 mm per side = 107.22 × 68.87 mm, centred on the module at
(−9.5, 11). Vertical fit: the glass-derived window is 68.87 mm against a 65.10 mm free band when the LED light pipes
sit above the buttons, so the authorised fallback (3) was applied — the REC / OVER DUB / PLAY light pipes moved 12 mm
beside their buttons. The band is then +49.70 (feedthrough cutout floor) to −27.80 (transport-button opening top) =
77.50 mm, and the window keeps 4.365 mm to the button openings, 1.165 mm to the feedthrough labels (moved from y 44.0
to 47.5) and 2.865 mm … 7.935 mm of panel web around it. Horizontal, all values recomputed from `mechanical/layout.json`: the window left edge −63.11 keeps **12.89 mm to the
panel edge** (−76.0), a true perpendicular distance. Its **X projection** to the M3 rail-hole edge (66.98) is
**3.87 mm**, but the rail hole sits at a different Y, so the true 2D distance from the emitted aperture polygon to the
nearest rail-hole edge is **15.423 mm**. The knob at x = 52 (Ø12.5 body, right edge 58.25) keeps **17.75 mm to the
panel edge**, again perpendicular; its **X projection** to the rail-hole edge is **8.73 mm**, and the true 2D distance
from the knob body to the nearest Ø3.2 rail hole is **27.526 mm**. Those projections are labelled as projections and are
not webs or shortest clearances; the 2D figures are asserted in `tscircuit/lib/layout.test.ts`. Every figure here is
planning geometry: rail nut/washer fit, neighbour-module fit and the physical print remain pending. The module left edge
−62.5 keeps 1.5 mm to the 128 mm board edge and the module right edge +43.5 keeps 2.25 mm to the value-knob body
(Ø12.5 at x = 52).

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
the **120 mm** 15→22 cable — the coexistence/route contract below needs slack for two 90° folds of the flex inside the
display stack slot and the drop to the Pi (the provisional path is ~60 mm; 50 mm leaves none). The 9.0 mm plane
separation = 4.0 mm boss projection + 5.0 mm spacer remains provisional against the kit pillars and is verified by
acceptance item `display_boss_pattern`.

### Display FPC ↔ J5 coexistence (unverified; `display_fpc_idc` pending)

The earlier planning position (38.84, 11.7), connector identity and body dimensions require re-derivation.
The STEP features previously used for the FPC height were identified as TFT flex bonds, not as a verified
connector. The drawn planning marker is not on the placed vendor model's PCB-rear plane. Consequently neither the
connector's location nor its clearance to J5 is established. The proposed front-board face change may also change
which side carried J5. The owner has since required the direct 2 × 13 mate: the female socket is on the front board's
bottom (rear-facing) face and the male header on the rear board's top face, opposite the Pi.

The provisional stack puts the front-board top plane at Z = 19.05 mm, but that is not a measurement and does not
prove a usable FPC bend corridor. The 120 mm 15→22 cable is a candidate, not an approved route. Measure the actual
connector, cable path, bends, latch and the J5 socket together on the first article before claiming clearance. Do not
use the former 12.9 mm connector-stack, 6.1 mm clearance, or 2 mm bend-radius estimates as verified values.

The display source is the 4.3-inch drawing and reference STEP recorded above; the withdrawn 3.5-inch 2D/3D files are no
longer a source. The active area is centred on the module outline (`DISPLAY_ACTIVE_OFFSET_X = 0`); only the front-board
silkscreen window uses it, and the panel aperture comes from the glass outline. `tscircuit` has no rounded-rectangle
cutout, so `panel.circuit.tsx` emits `roundedRectOutline()` from `lib/layout.ts` as a polygon.

**Panel openings (from `USB_FEEDTHROUGHS` / `PANEL_LCD_CUTOUT`).** Three feedthroughs at
y = 53.5: USB-C power at x = −38 (12.6 × 7.6 mm slot, Ø3.2 holes on 18 mm centres),
UMC audio USB-A at x = −5 and MIDI-hub USB-A at x = +35 (14.9 × 7.3 mm each, Ø3.5 holes
on 30 mm centres). The LCD window is 107.22 × 68.87 mm at (−9.5, 11). Leave at least
25–35 mm behind the panel for cable tails and bend radius; unverified. These are panel
openings, not proof of the rear body, flange or cable-bundle envelopes.

**Feedthrough fit — open finding.** The Adafruit 908 flange is 38.30 × 11.90 mm. At the canonical y = 53.5 centre it
spans y 47.55…59.45, while the board notch spans y 48…56 and the board top edge is at y = 56. The flange therefore
overlaps board material by **0.45 mm at its lower edge** and extends past the board's top edge. Options, none chosen:
raise the feedthrough centres, trim the flange, or lower the notch (the notch cannot go below y = 47.85 without breaking
into the bracket mount holes). This is a named physical gate — measure the purchased flange first.

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

## Panel button opening — UNRESOLVED, provisional 12.4 mm round

`PANEL_CUTOUTS.button = 12.4` is an **unverified provisional** value. No Omron
drawing states a 12.4 mm round opening, and the primary data does not support
one. Sources (retrieved 2026-11-08):

| Source | SHA-256 | What it states |
|---|---|---|
| [Omron B3F datasheet](https://omronfs.omron.com/en_US/ecb/products/pdf/en-b3f.pdf) | `be9cf69e5f43fb7689448a2097c19858e5643c0e2e49cd77d018881543e27a1f` | B3F-4055: projected plunger **square □3.8 ±0.1 mm**, body **12 ±0.2 mm square**, total height **7.3 ±0.2 mm** |
| [Omron B32 datasheet](https://omronfs.omron.com/en_US/ecb/products/pdf/en-b32.pdf) | `90834f79bcb19ed59fc242f0a005d8ca47fcdbf2f4583bc53695695bb675d28f` | a **12 × 12 mm square key top** with a **12.2 × 12.2 mm +0.05/0** reference cutout, and a **Ø9.5 ±0.2 mm round key top** with a **Ø9.7 mm +0.05/0** reference cutout |

Consequences, all unresolved:

- The plunger is **square**, and a square cap cannot pass a Ø12.4 mm round hole
  (a 12.2 mm square's diagonal is 17.25 mm). The round opening is therefore not
  demonstrated to accept any of the listed caps.
- The cap families are **not selected**. Choosing one is an owner decision, and
  it interacts with the opening shape and the stack.
- The reference cutouts carry a **+0.05/0 mm** tolerance. This document does
  **not** claim JLCPCB can hold 0.05 mm on a routed panel cutout; that is
  unverified.
- **Arithmetic only, with the physical stack unmeasured:** in the provisional
  17.45 mm panel-rear-to-board-top gap, Omron's reference board-to-cap-face
  figures put neither key top at the panel plane — square reference
  **10.0 ±0.4 mm** leaves 17.45 − 10.0 = **7.45 mm** (7.05…7.85 mm range), and
  round **11.5 ±0.4 mm** leaves 17.45 − 11.5 = **5.95 mm** (5.55…6.35 mm). These
  are subtractions on a provisional gap, not measurements.

The `control_actuator` acceptance item stays **pending**. The owner must choose a
switch extension, a cap, or a stack redesign before the opening is changed. **Do
not change Ø12.4 on the strength of this section**, and do not treat any value
here as a fitted dimension.

## Board-to-board relationship

Both boards are centred on their own origin, so the 128 mm front board overhangs the 101.6 mm rear board by
**13.2 mm per side**, not 26.2 mm on one side. The four front↔rear M2.5 spacers at (±45, 43) and (±44, −52) sit inside the rear
board (edge ligament 5.45 mm). The right-hand control column at x = 52 is outside the rear-board outline (±50.8) and
its hardware overhangs; a left-flush rear board (26.4 mm one side) is a different, unchosen arrangement and would be an owner
decision.

- Front↔rear spacers: length = the direct 2 × 13 pair's **11.20 ±0.20 mm** nominal seat-to-seat drawing dimension
  (Sullins 11091 sheet 2), not a measured gap and not a purchased-part choice until the pair is fitted and measured.
  The top pair at (±45, 43) sits inside
  the feedthrough bracket envelope, so the printed bracket carries head-clearance slots there; the M2.5 head/washer
  envelope is unmeasured. (±44 → ±45 was forced by the new display boss hole at (+40.15, +41.90): the web goes
  0.50 → 1.47 mm; ±46 was tried and rejected — the rear autorouter then placed a via at (50.46, 6.56) with 0.118 mm
  of copper to the board edge, under the published 0.20 mm rule.)
- Panel↔front standoffs: 4 × M2.5 at (±61, ±53), 1.65 mm edge ligament; length TBD.
- Direct interconnect (X 5.6…14.4, Y −15.3…25.3): the J5 female socket body and the J_FRONT male shroud stand in
  the 11.20 mm gap here, with the 106 mm module above the same plan area. The recorded FPC connector is a planning
  marker only (identity, side and position unverified), so no clearance from it is claimed. The remaining FPC
  question is the front board's own top-side parts and the female tail (3.20 mm from the seating face, i.e. a nominal
  1.60 mm beyond a 1.60 mm PCB, fillet unmeasured; see `display_fpc_idc` above).
- Pi side of J_FRONT: the male tail is **3.00 mm from the connector seating face** (drawing 11091 sheet 1), so it
  protrudes a **nominal 1.40 mm beyond a 1.60 mm rear PCB**; the 26 pads lie over the Pi PCB area. A **candidate** Pi
  spacer of 3.5 mm is recorded, but it is a candidate only and NOT a safety claim: the solder fillet height, the PCB
  thickness tolerance, the Pi hardware, the actual clearance, the Pi's own connectors and the cooler/HAT stack are all
  unmeasured, so this stays pending.
- Display top edge (+45.0) vs the bracket bar (bottom 42.0): they overlap in PLAN. Their Z separation follows from
  provisional stack assumptions and requires physical verification. Display left (−62.5) to front-PCB edge (−64): 1.5 mm ligament
  (owner decision D2).
- C5 (50, 46) against the right bracket mount (55, 46.5) and the top spacer pair (±45, 43): centre distances 9.66 /
  5.83 mm, screw-head class; sample check.

## Provisional installed-depth chain

| Segment | Provisional | Basis |
|---|---:|---|
| Front-board rear face | 20.65 | See the Z stack. |
| Front↔rear gap (direct 2 × 13 mate, seat to seat) | 11.2 | Drawing 11091 sheet 2, ±0.20 mm; not measured. |
| Rear/base PCB | 1.6 | 1.6 mm FR-4. |
| Pi spacer + Pi PCB | 5.1 | 3.5 mm **candidate** spacer + 1.6 mm Pi PCB. The male tail is 3.00 mm from the seating face = a nominal 1.40 mm protrusion beyond a 1.6 mm PCB; the fillet and clearance are unmeasured, so the 3.5 mm is not a safety claim. |
| Pi cooler + M.2 HAT+ + SSD | 30 | Candidate range 20–30. |
| **Total from the panel front face** | **68.55** | Sum of the above; `provisionalInstalledDepth()` in `layout.ts`. |

The ≤110 mm target therefore holds on paper at the high end of every candidate, and `lib/layout.test.ts` keeps the
chain inside it. That is arithmetic on unmeasured candidates, not a verified installed depth: no cooler/HAT/SSD,
cable-bend or airflow model exists.

### Behringer Eurorack Go fit (design check, no approval)

Case data from the Thomann product page: 2 × 140 HP, 6U, M3 slide nuts, +12 V/3 A, −12 V/1 A, +5 V/1 A, chassis
112 × 723 × 311 mm, **maximum module depth 40 mm (lower row) and 62 mm (upper row)**. The module draws nothing from
the Eurorack bus.

- Width/height: 30HP (152.0 mm actual) fits a 140 HP row with (140 − 30) = 110 HP of space left; 128.5 mm fits 3U; rail holes are at the Doepfer 3U positions
  Y ±61.25 after the 2026-11 fix (30HP panel: X ±68.58, spacing 137.16 = 27 × 5.08). The horizontal inset is 7.42 mm, i.e. 0.08 mm inside the 7.5 mm the Doepfer hole figure (A100FP.gif) draws; the vertical inset stays the figure's 3.0 mm with Ø3.2 mm holes, and the 128.5 mm height matches it. Round Ø3.2 holes suit the Go's sliding M3 nuts; slotted holes are still recommended. Rail nut/washer fit, neighbour-module fit and the print remain pending.
- Depth: the provisional stack is **≈63–75 mm behind the panel back** (76.25 − 1.6 = 74.65 mm at the high end; the
  low end of the candidate ranges is 62.65 mm). That exceeds the 40 mm lower row by 23–35 mm and is at or over the
  62 mm upper row before tolerances, ribbon folds, the Pico USB plug and airflow.
- **Verdict: not approved for the Eurorack Go. Do not plan this module for that case.** No fabrication release until
  the case is reselected or the Pi/cooler/NVMe stack leaves the rack. The ≤110 mm target was never compatible with a
  40 mm row.

### Panel power header vs feedthrough bracket (blocked; `power_header_mount` pending)

The owner requires J_PWR on the front board's REAR face at the middle of the top relief notch (x ≈ −2.5 mm, pads
on board material below y = 48 mm, pins into the notch). On that same face the printed bracket is a 3 mm bar from
y 42.0 to 56.0 mm, with open slots whose floors are at y 48.20 / 48.35 / 48.35 mm and the UMC AUDIO slot spanning
x −13.95…3.95 mm. A right-angle header's body (candidate Harwin M20-9960245: 4.80 mm body above the PCB, 6.10 mm
exposed pin, 2.54 mm body width) plus its 2.54 mm female housing therefore intersect the bracket's solid material
below the slot floor, and the UMC AUDIO cable tail occupies the same slot. The owner approved a pocket in that bracket (2026-11-10), so the plan is recorded in `tscircuit/lib/layout.ts`:
`POWER_HEADER` (rear face, x = −2.5 mm, y = 47.00 mm = notch 48 mm − pad 1.60/2 − 0.2 mm edge rule, mating axis +Y),
`POWER_HEADER_KEEPOUT` and `BRACKET_POWER_POCKET` (the pocket opens into the UMC AUDIO slot at its floor y = 48.35 mm and
reaches the bar's top edge; the bar's 3.00 mm thickness and its three slots stay unchanged). **Implemented**: the independent architect reading fixed the orientation (long 6.10 mm mating pins parallel to the PCB,
short 3.00 mm PC tails perpendicular, a true right angle), so `J_PWR` is placed at x = −2.5 mm, y = 47.00 mm on the rear
face at 180° with the mating axis toward global +Y, and the bracket carries the owner-approved through pocket
`BRACKET_POWER_POCKET` (x −6.81…1.81 mm, y 43.96…56 mm). `mechanical/feedthrough_bracket.py` cuts it and self-checks:
**one connected solid**, no material inside the declared `POWER_HEADER_KEEPOUT` box over its 4.80 mm height, 1.96 mm of
bar below the pocket and 7.14/2.14 mm beside it, the other two slots untouched. The exact hole-to-housing offset is
**still unresolved** (the 4.80 mm datum is unclear, sheet 1 of 2 missing), the 1.60 mm pad and 8.00 mm plug reach are
engineering assumptions, the DuPont-style plug is unmeasured, UMC AUDIO cable access is **unverified**, and the printed
pocket fit is unproven. Sourcing is
UNVERIFIED (digikey.se Cloudflare 403; no SKU or stock claimed), and the Electrokit breakaway strip was proposed by
the researcher, not selected by the owner. `lib/power-header.test.ts` checks the built board and the pocket; `power_header_mount` stays pending.

### Feedthrough bracket

The 3 mm PETG bar on the rear side of the front board, spanning the top relief notch: outline x −63…58, y 41.5…56
(121 × 14.5 mm), mounts (−60, 46.5) and (55, 46.5), three open-top cable slots sized from the vendor panel cutouts
in **both** directions — 15.6 / 17.9 / 17.9 mm wide at x = −38 / −5 / +35, floors at y = 48.20 / 48.35 / 48.35 —
head-clearance slots Ø6.5 at (±44, 43) for the top front↔rear spacer hardware, and four Ø3.2 cable-tie holes in the
webs. Each slot floor is its own feedthrough's declared opening bottom less 1.5 mm, kept above the board notch bottom
(48.0); the panel opening does not prove the unmeasured rear body, flange or cable bundle. All of it is derived from `lib/layout.ts` and
generated into `mechanical/layout.json`; the first draft hardcoded slots at −48/−13/+22 and mounts at −61.5/40 with
20/40/40 mm slots that touched each other. The bracket is generated from `layout.ts`; the CAD self-check lives in
`mechanical/feedthrough_bracket.py` + `mechanical/bracket_geometry.py` and is not run by `make test` (CadQuery is optional).

### STEP assembly and printable mockups (verification only)

`mechanical/step_assembly.py` builds a named-solid STEP assembly and individual
1:1 panel/front-board/rear-board STL mockups from `mechanical/layout.json`, plus
a printer scale bar and a light-pipe reach gauge. The flange-versus-board result
stays **UNDETERMINED**: the 0.45 mm overlap is an XY projection and the Adafruit
908 drawing states no flange depth. `make mechanical` runs the pure tests; the
CAD exports are gitignored and regenerated on demand. Paths, the collision rule,
the vendor STEP hash check and the print checklist are in
[mechanical-cad.md](mechanical-cad.md).

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
USB-C feedthrough bodies and flanges, the M2.5/M3 hardware envelopes, the Bivar light pipe, the Pi 5 sample, the
cooler/M.2 HAT+/SSD stack, and every cable bend. Build the stack mock-up, then re-run the gates. No increase of panel
width or installed depth without an explicit owner decision.

# Close-out — Waveshare 4.3inch DSI LCD panel change

> **This file began as a time-boxed recovery snapshot and is kept as the close-out record.** Everything it listed as
> NOT STARTED, blocked or partial has since landed at **f1262bd** (`midibrain: display swap to Waveshare 4.3inch DSI
> LCD + 32HP panel`); the recovery notes below are rewritten to their final state, and the owner-decision table stays
> as the history. The only open items left are the four **first-article physical checks** at the bottom — nothing in
> the design is waiting on a decision. This close-out also resolves the last four documentary gaps: the store SKU, the
> glass outline row, the FPC connector keep-out and the 14.05 / 13.82 mm thickness question.

## Owner decision (history, unchanged)

| item | value |
| --- | --- |
| old display | Waveshare 3.5inch DSI LCD (H), SKU 33087 — not sourceable |
| new display | Waveshare **4.3inch DSI LCD**, ElectroKit art **4103 5687**, 479 SEK, in stock |
| resolution | 800 x 480 — identical, the UI code needs no change |
| panel | 4.3 inch IPS LCD, MIPI DSI, capacitive 5-point touch, 6H glass |
| in the box | 3 DSI cables (50 mm 15-pin→22-pin, 120 mm 15-pin→22-pin, 160 mm 15-pin→15-pin), screws, copper pillars/standoffs |
| D1 (2026-11-07) | the panel **window** is sized from the **glass**, not from the 106 × 68 PCB envelope |
| D2 (2026-11-07) | the value-knob column moves **47 → 52 mm**; the front board stays **128 × 112**; the panel is **forced to 32HP** |

## What landed (single source: `tscircuit/lib/layout.ts`)

| quantity | landed value | where |
| --- | --- | --- |
| panel | **32HP × 3U = 162.56 × 128.5 × 1.6**, rail holes X ±73.78 / Y ±61.25 | `PANEL`, `RAIL_HOLES` |
| LCD panel cutout | **107.22 × 68.87** (glass 105.42 × 67.07 + 0.9 mm per side, r1.0 corners) centred at **(−9.5, 11)** | `PANEL_LCD_CUTOUT`, `LCD_CUTOUT_ALLOWANCE`, `DISPLAY_CONCEPT` |
| module envelope behind the panel | 106.00 × 68.00 × 1.6 PCB; glass front-face outline **105.42 × 67.07**; printed bezel 95.54 × 54.36 | `DISPLAY_VENDOR` |
| active area (VA) | **95.04 × 53.856** (800 × 0.1188 / 480 × 0.1122 mm) | `DISPLAY_VENDOR.active` |
| overall thickness | **14.05** (wiki FAQ; STEP union bbox 13.82 — see below) | `DISPLAY_VENDOR.overallDepth` |
| front PCB | **128 × 112 × 1.6** (unchanged by D2) | `FRONT_PCB` |
| rear/base PCB | 101.6 × 112 × 1.6 | `REAR_PCB` |
| display standoff pattern (used) | **58.00 × 49.04** centres (insets 25.62 / 22.38, 9.50 / 9.46), 4 × **Ø4.30** | `DISPLAY_STANDOFFS` |
| outer boss pattern (alternative) | **99.30 × 61.80** centres (insets 3.35 / 3.10), 4 × **Ø4.30** | `DISPLAY_BOSS_PATTERN` |
| value knob / NAV / SHIFT-EXIT / PLAY START column | **x = 52** (was 47), 19 mm pitch | `RIGHT_COLUMN` |
| 15-pin DSI FPC connector | body 21.412 × 4.826, rear face, right-hand long edge (rear view); absolute **(38.84, 11.7)** | `DISPLAY_FPC_CONNECTOR(_AT)` |

The hole diameter is worth one line because the earlier table listed "Ø2.15": 2.15 is the STEP **radius** of the eight
CIRCLE entities, so the hole is **Ø4.30**, which is what `DISPLAY_VENDOR.holeDiameter` carries. The drawing does not
dimension the hole at all. Likewise the module's mounting pattern is 58.00 × **49.04** (9.50 / 9.46 top and bottom
insets); the Pi grid is 58.00 × 49.00, so the 0.04 mm is the module's own asymmetry, not a layout slip.

## 1. Drawing, SKU and dimensions — DONE

| source | URL | SHA-256 |
| --- | --- | --- |
| wiki (canonical: SKU, FAQ, cable/pillar instructions) | https://www.waveshare.com/wiki/4.3inch_DSI_LCD | — (page) |
| product page (store listing, `Exterior-Size.jpg`, Rev2.2 drawing) | https://www.waveshare.com/4.3inch-dsi-lcd.htm → https://www.waveshare.com/img/devkit/LCD/4.3DP/Exterior-Size.jpg | `85b664dc36c56706d96426958b540b118181d60fa088a8777d56549c1294f615` |
| 3D model (wiki "Drawing", STEP) | https://www.waveshare.net/w/upload/2/28/4.3inch_DSI_LCD_Drawing.zip → `4_3inch-dsi-lcd-V1.stp` | archive `f75cb7ad8c5ab00471d7dda22e5f11a85eacb78c63f4ac25836b6dc88a843646`, STEP `01eef9172ff4364691f4de37ff98f95437e96caa647b94b4bd4a1c54ac4519ed` |

**Waveshare SKU — CONFIRMED online.** The store listing for `4.3inch-dsi-lcd.htm` carries the machine-readable product
record `"sku": "16239"`, `"id": "16239"`, `"mpn": "4.3inch DSI LCD"`, `"url": "https://www.waveshare.com/4.3inch-dsi-lcd.htm"`,
i.e. **SKU 16239**, the value already recorded in `mechanical/reference/waveshare-4.3inch-dsi-lcd.source.json`. The wiki
page is the canonical page for the same part. What stays a first-article item is only the physical tie-break: the
`source.json` names "the SKU printed on the module label" as the ultimate authority, so read the label on the delivered
module and record it next to the ElectroKit article number — the two sources above agree, but neither is the part in hand.

**Revision.** The STEP file is *named* `4_3inch-dsi-lcd-V1.stp` while the drawing is Rev2.2, which looked like a
mismatch. It is not one: the STEP's own top-level assembly product is **`4_3INCH_DSI_LCD_V2_2_ASM`**, so the model and
the drawing are the same revision, and only the file name lags.

Measured (drawing rear view, unit mm; STEP cross-check with `mechanical/tools/measure_display_step.py`):

| feature | value | cross-check |
| --- | --- | --- |
| PCB outline | **106.00 × 68.00 mm** | STEP plane 106.0 × 68.0 at z = 1.6 |
| PCB thickness | 1.6 mm | STEP z = 0 → 1.6 plane pair |
| outer mounting holes | **99.30 × 61.80 mm** pitch, insets 3.35 / 3.10 mm, 4 × **Ø4.30** | STEP circles r2.15 at (3.35, 3.10), (102.65, 64.90) … |
| Raspberry Pi pattern holes | **58.00 × 49.04 mm** (insets 25.62 / 22.38, 9.50 / 9.46), 4 × Ø4.30 | STEP circles r2.15 at (25.617, 9.458) … (83.617, 58.508) |
| overall thickness | **14.05 mm** | wiki FAQ; STEP union bbox 13.82 mm — reconciled below |
| active area (VA) | **95.04 × 53.856 mm** | STEP VA plane 95.04 × 53.86 at z 2.900 (printed bezel = VA + 0.25 mm/side) |
| glass/bezel outline | **105.42 × 67.07 mm** — MEASURED, and the cutout is built from it | STEP cover-glass front face (16-point loop at z 2.900 / 0.000); front frame around it 106.10 × 67.80 |
| 15-pin FPC connector | body **21.412 × 4.826 mm**, rear face, right-hand long edge (rear view); module-local centre (+48.34, +0.7) | STEP extruded bodies 106739 / 107488; position from the drawing at 7.11 px/mm (see below) |

### FPC connector — the number that was missing

The connector is on the module's **rear face** at the right-hand long edge of the rear view. The STEP models it as two
extruded bodies, **21.412 × 4.826 mm** (ids 106739 / 107488, 1.0 and 0.8 mm tall), but
`measure_display_step.py` reads every solid in its own frame and the text-only reader does not resolve assembly
placements, so the **position** comes from the Rev2.2 drawing at its 7.11 px/mm scale: bar 244…398 px = 21.8 mm (agrees
with the STEP's 21.412 to 0.4 mm), long-axis centre 33.3 mm from the top edge, body 4.5 mm deep with its outer face
2.25 mm inboard of the module edge (±0.4 mm reading error). Landed in `layout.ts` as `DISPLAY_FPC_CONNECTOR` plus
`DISPLAY_FPC_CONNECTOR_AT` = **(38.84, 11.7)** absolute, and exported into `mechanical/layout.json`.

It does **not** move the PCB this rev: the connector faces the front board's rear side and sits 20.5 mm inside the
128 mm board edge (module right edge +43.5 against board edge +64.0), so the cable leaves *over* the front board. That
puts the plug, its latch and the first bend over the **J5 IDC corridor (X ≈ 29–39, Y −10…+30)**, which is why the cable
bend is a first-article check rather than a settled design.

### Thickness: 14.05 vs 13.82 — reconciled

The wiki FAQ states an overall thickness of **14.05 mm**; the STEP's union bounding box (CadQuery import, 106.10 × 68.36
× **13.82** mm) is 0.23 mm thinner. Read against the model, the difference is **not** glass overhang and **not** feet:

* the model's only overhangs past the 106 × 68 PCB envelope are **in X and Y** — the front frame around the glass is
  106.10 × 67.80 and the union reaches y 68.362, i.e. at most 0.18 mm past the PCB edge — and none of that adds to Z;
* the largest front-most modelled body is 4.219 mm above the PCB plane and the rear-most is the `SMTSO-M2_5-4ET`
  boss, so the solids end at **13.82 mm**, with **no solid reaching the 14.05 plane**.

So the two primary sources differ by 0.23 mm and the model cannot allocate it: it is the vendor's **stated maximum**
versus the **modelled nominal**, not a feature that was left out of either. The stack therefore uses the **larger,
14.05 mm** (`DISPLAY_VENDOR.overallDepth`), which keeps the panel gap conservative by 0.23 mm, and
`docs/mechanical-stack.md` quotes both numbers with this explanation. The provisional split behind it — 10.05 mm glass
front → PCB rear plus 4.0 mm boss projection — is design data, not a measurement; only the 4.0 mm comes from the model.

## 2. Panel geometry, window and mounts — DONE

### 2a. Vertical fit — resolved via the glass

The earlier run reported a **4.70 mm shortfall**: a 68.00 mm module plus 0.9 mm per side needs a 69.80 mm aperture
against a 65.10 mm free band (feedthrough cutout floor +49.70 down to the LED opening top −22.00). That shortfall was an
**artifact of sizing the window from the PCB envelope**. Sized from the **glass** (D1) the aperture is **68.87 mm tall**,
and with the authorised fallback (3) — the REC / OVER DUB / PLAY light pipes moved 12 mm beside their buttons,
`LED_Y = TRANSPORT_Y` — the free band becomes **+49.70 → −27.80 = 77.50 mm**. The window (centre y = 11, so +45.435 …
−23.435) therefore keeps **4.365 mm** to the transport-button opening top, **2.065 mm** to the feedthrough labels
(moved from y 44.0 to 47.5) and **4.265 mm** to the cutout floor. No transport control, jack or panel legend moved
down; option (2) from the recovery note is withdrawn.

### 2b. Horizontal fit — 32HP plus the knob column at x = 52

30HP failed only because the 106 mm module had to keep clear of the value-knob body at x = 47 while also clearing the
M3 rail hole. With **D2** (knob column 47 → 52) the module sits at **x −9.5** and:

| term | 32HP, knob at 52 |
| --- | --- |
| module edges | −62.50 / +43.50 |
| front-board edge (128 mm) | −64.00 → **1.50 mm** ligament |
| knob body (Ø12.5 at x = 52) left edge | 45.75 → **2.25 mm** clear of the module |
| window left edge | −63.11 → **9.07 mm** to the M3 rail-hole edge (−72.18), **18.17 mm** to the panel edge (−81.28) |

32HP = **162.56 mm** is therefore the width that carries the module and the backward-compatible control column; the
withdrawn 136 mm front-board proposal is gone with it.

## 3. Front board — DONE

The board stays **128 × 112** (D2) and carries the 106 mm module with 1.5 mm to its edge and 2.25 mm to the knob body.
Mounting holes derive from the module centre and the drawing's insets (`LCD_MOUNT_HOLES` =
x −36.88 / +21.12, y +35.50 / −13.54), and the outer `DISPLAY_BOSS_PATTERN` stays in the data as the alternative if
the first article finds the SMT bosses are the threaded ones. Rear board 101.6 × 112 and the IDC/Pi/Pico interfaces are
untouched.

## 4. Regeneration and gates — DONE

`make test` and `make order` are green at f1262bd. For this close-out `make test` was re-run green (147 tests, 0
fail; placement reports the 107.22 × 68.87 aperture with 0.851 mm corner clearance); `make order` is untouched by it
because the only source change is the additive `DISPLAY_FPC_CONNECTOR` export plus its `mechanical/layout.json` row —
no PCB, panel or silkscreen geometry moved. Order for a **fresh worktree**:
`cd tscircuit && bun install && bun run build`, then `make test` — `make test` runs `bun test` before its own build
step, so a bare worktree without `dist/` fails on `dist/front/circuit.json` for environmental reasons, not on geometry.

The hardware fingerprint was `2c00c4f06f96…` at f1262bd. Adding `DISPLAY_FPC_CONNECTOR` to `lib/layout.ts` re-hashes the
hardware sources, so `hardware-acceptance.json` now records **`937941eb996edc6c03e0e88a0a7d221d909839b3ef2ea84b0bbf27a9400f0f08`**
(`bun run scripts/release_gate.ts --fingerprint`). No acceptance evidence existed against the old hash, so nothing went
stale; the next hardware change needs the same refresh in the same commit.

## 5. Sourcing, docs and fingerprint — DONE

* `scripts/sourcing-extras.csv`: DISP1, CAB_DSI and STANDOFF_LCD rows carry ElectroKit 4103 5687, the three included
  DSI cable lengths and the kit pillars plus the module's own M2.5 SMTSO bosses (4 mm class, outer pattern).
* `docs/mechanical-stack.md`, `docs/decisions.md`, `README.md`, `docs/USER-MANUAL.md`, `docs/ui-refresh-spec.md` §7 and
  `docs/touch-testing-checklist.md` carry the 32HP / 107.22 × 68.87 / 14.05 numbers. The stack doc's Z chain was moved
  from the withdrawn 3.5-inch figures (6.7 / 10.7 mm) to the 4.3-inch split (10.05 + 4.0 = 14.05, gap 17.45 mm).
* Fingerprint refreshed, as above.

## Reference: UI mechanical rationale

Old active area 75.8 × 45.36 mm (4.2 mm per 44 px) → new **95.04 × 53.856 mm** (0.1188 × 0.1122 mm per pixel):

* the 44 px minimum target is now **5.23 mm** (was 4.2 mm);
* the 64 px primary target is now **7.60 mm** (was ~6.1 mm);
* the px targets themselves are unchanged — same 800 × 480 panel, no UI code change.

## Remaining physical checks (first article — the only open items)

1. **Module-label SKU.** Read the SKU printed on the delivered module (the `source.json` authority) and record it
   against ElectroKit 4103 5687 next to the online value **16239**. Online store listing and wiki agree; only the part in
   hand can confirm.
2. **Glass seating and boss-plane clearance.** The window follows the glass (D1), but whether the **glass** front face
   actually sits flush with the panel front face (Assumption A, Z = 0) is unmeasured, and so is the clearance between
   the panel's rear face and the **PCB boss plane**. Check both after the first stack-up; if the glass seats proud, the
   0.9 mm per-side allowance is what absorbs it, and the plane separation (9.0 mm, spacer 5.0 mm) absorbs the boss side.
3. **Cable bend.** Which of the 50 / 120 / 160 mm cables fits the module → Pi 5 DSI1 run, its bend radius and latch
   clearance — specifically past the J5 IDC corridor the connector now sits over (X ≈ 29–39, Y −10…+30). The 160 mm
   cable is 15-pin→15-pin and does not fit DSI1 at all.
4. **Touch-window alignment.** Capacitive mapping against the 107.22 × 68.87 aperture, 5-point, and the 64 px target at
   7.60 mm; confirm the 0.25 mm-per-side bezel overlap does not clip the touch edge.

# Recovery summary — Waveshare 4.3inch DSI LCD panel change

Status of the 2026-11-07 owner decision (3.5inch DSI LCD SKU 33087 unsourced ->
Waveshare 4.3inch DSI LCD, ElectroKit art 4103 5687). Written at the end of a
time-boxed run, so it records exactly what is done, what is partial and what is
not started. **Nothing in `tscircuit/lib/layout.ts` has changed yet**, so the
tree still builds and every gate still passes: the panel is still 26HP and the
display data is still the 3.5inch vendor block.

## Owner decision (unchanged)

| item | value |
| --- | --- |
| old display | Waveshare 3.5inch DSI LCD (H), SKU 33087 — not sourceable |
| new display | Waveshare **4.3inch DSI LCD**, ElectroKit art **4103 5687**, 479 SEK, in stock |
| resolution | 800 x 480 — identical, the UI code needs no change |
| panel | 4.3 inch IPS LCD, MIPI DSI, capacitive 5-point touch, 6H glass |
| in the box | 3 DSI cables (50 mm 15-pin→22-pin, 120 mm 15-pin→22-pin, 160 mm 15-pin→15-pin), screws, copper pillars/standoffs |

## 1. Authoritative drawing, SKU and dimensions — DONE

Primary sources (retrieved 2026-11-07):

| source | URL | SHA-256 |
| --- | --- | --- |
| wiki (canonical, SKU + FAQ + cable/pillar instructions) | https://www.waveshare.com/wiki/4.3inch_DSI_LCD | — (page) |
| product page (exterior dimension drawing, `Exterior-Size.jpg`) | https://www.waveshare.com/4.3inch-dsi-lcd.htm -> https://www.waveshare.com/img/devkit/LCD/4.3DP/Exterior-Size.jpg | `85b664dc36c56706d96426958b540b118181d60fa088a8777d56549c1294f615` |
| 3D model (wiki "Drawing", STEP) | https://www.waveshare.net/w/upload/2/28/4.3inch_DSI_LCD_Drawing.zip -> `4_3inch-dsi-lcd-V1.stp` | `f75cb7ad8c5ab00471d7dda22e5f11a85eacb78c63f4ac25836b6dc88a843646` |

Waveshare SKU of the module: the wiki/product page for *4.3inch DSI LCD* is the
canonical page for this part; the ElectroKit article 4103 5687 lists the
Waveshare model name "4.3inch DSI LCD" and SKU 4103 (ElectroKit's own article
number). The Waveshare store SKU string was **not** confirmed in this run beyond
the wiki page identity — the follow-up must read the SKU printed on the module
label / the Waveshare store listing and record it next to `DISPLAY_DATUM`.

Measured from the dimension drawing (rear view, "Unit:mm") and cross-checked
against the STEP model (points/cylinders read directly, raw frame):

| feature | value | cross-check |
| --- | --- | --- |
| PCB outline | **106.00 x 68.00 mm** | STEP plane 106.0 x 68.0 at z = 1.6 |
| PCB thickness | 1.6 mm | STEP z = 0 -> 1.6 plane pair |
| outer mounting holes | **99.30 x 61.80 mm** pitch, inset 3.35 / 3.10 mm from the PCB edges, 4x Ø2.15 | STEP circles r2.15 at (3.35, 3.1), (102.65, 64.9) … |
| Raspberry Pi pattern holes | **58.00 x 49.00 mm**, 25.62 / 22.38 mm from the left/right edge, 9.50 / 9.46 mm from the top/bottom edge, 4x Ø2.15 | STEP circles r2.15 at (25.617, 9.458) … (83.617, 58.508) |
| overall thickness | **14.05 mm** | wiki FAQ "overall thickness of the 4.3inch DSI LCD: 14.05mm"; STEP z extent |
| active area (VA) | **95.04 x 53.856 mm** (0.1188 x 0.1122 mm pixel, 800 x 480) | STEP VA plane 95.54 x 54.36 = VA + 0.50 mm (0.25 mm bezel overlap per side) |
| glass/bezel outline | **NOT yet measured** — see the blocker below | — |
| FPC/DSI connector | 15-pin, on the PCB's right-hand long edge (drawing); exact position not yet transcribed to layout coordinates | STEP `TP-CONNECTOR` / FPC tail at the bottom-left in the rear view |

The rear-most feature and the exact z of the glass front face are also still to
be read out of the STEP (`SMTSO-M2_5-4ET` M2.5 SMT bosses are present, i.e. the
module ships with its own 4 mm-class threaded bosses, same idea as the 3.5inch).

## 2. Panel geometry + display window + mounts — NOT STARTED (blocked, numbers below)

Two findings this run produced, both of which need an owner/design decision
before `layout.ts` is edited:

### 2a. Vertical fit blocks *both* 30HP and 32HP

The panel is 128.5 mm tall regardless of HP. Between the row the aperture has to
live in, the free band is smaller than the aperture:

| term | value |
| --- | --- |
| feedthrough cutout floor (highest obstacle) | y = +49.70 |
| feedthrough label bottom (1.8 mm text at y = 44.0) | y = +43.10 |
| LED opening top (Ø4.0 at y = -24.0) | y = -22.00 |
| free band | **65.10 mm** |
| aperture needed for a 68.00 mm module at 0.9 mm per side | **69.80 mm** |
| **shortfall** | **4.70 mm** |

Growing the panel in X does not help: the display is 106 wide, not 68 tall, and
the panel height is fixed by 3U. Options, all needing a decision:

1. size the panel aperture from the **measured glass outline** instead of the
   106 x 68 PCB envelope (the glass is very likely 3-5 mm shorter in Y; this is
   the cheapest fix and needs the drawing of the front face);
2. move the transport/LED block down (~4 mm: buttons y -34 -> -38, jacks
   -47.5 -> -51.5). Jack cutout bottom becomes -54.6 against a panel edge of
   -64.25 and the rail-hole row at y -61.25 — geometrically possible, but it
   moves every transport control and the panel art;
3. move the LVDS-style LED row (REC/OVER DUB/PLAY light pipes at y -24) out of
   the display's band — e.g. beside their buttons instead of above them;
4. move the USB feedthrough row up and deepen the front-board relief notch.

### 2b. Horizontal fit: 30HP is marginal, 32HP works if the knob column moves

Constraints (display module 106 wide, module envelope, knob body Ø12.5 at
`RIGHT_COLUMN.x = 47`, rail holes Ø3.2 at X ±(W/2 − 7.5), M3 rack rule):

| | 30HP (152.40) | 32HP (162.56) |
| --- | --- | --- |
| rail hole X | ±68.70 | ±73.78 |
| nearest rail-hole edge | 67.10 | 72.18 |
| free width right of the knob body (knob at 47) | 47 − 6.25 − 2 = 38.75 | 38.75 |
| display must end at/left of | 38.75 | 38.75 |
| display left edge (106 wide) | -67.25 | -67.25 |
| ligament to the rail hole, display centred to keep the knob clear | **0.15 (fails)** | **4.93** |
| panel edge margin, same position | 8.95 | 14.03 |

So **30HP fails only because of the rail hole** when the display has to keep
clear of the value-knob body; 32HP passes with ~4.9 mm to the rail hole and
~14 mm to the panel edge. Alternative that keeps 30HP alive: keep the display
where it is (module left edge ≈ -63.2, i.e. `DISPLAY_CONCEPT.x` ≈ -10) and move
the right-hand control column 5 mm right (47 -> 52, body edge 58.25 against the
128 mm board edge 64) — that recovers the rail-hole ligament. The panel X
numbers above are the reason the task's "prefer 30HP" cannot be answered from
the current data alone; both options are computed here, neither is committed.

## 3. Front board — NOT STARTED (evaluation below)

* 128 x 112 (X ±64) can carry the 106-wide module only if the module's left
  edge stays ≥ -62.5 (1.5 mm edge ligament): left edge -62.5 means the module
  ends at +43.5, which is 3.75 mm from the knob body edge (40.75) — that is an
  **overlap of 2.5 mm** with the knob. The 128 mm board therefore cannot hold
  the module and the knob at x = 47 at the same time.
* Either the board grows (136 mm = ±68 fixes it with 1.75 mm edge ligament while
  the knob stays at 47), or the knob column moves right (52 mm: module -62.25 …
  +43.75, 1.75 mm to the board edge, 2 mm knob gap). Width growth does not touch
  the ≤ 110 mm depth rule (that is Z, unchanged).
* Rear board 101.6 x 112, the IDC/Pi/Pico interfaces: untouched, as directed.

## 4. Regeneration + gates — DONE (for the current, unchanged layout)

`python3 tscircuit/scripts/test_sourcing_bom.py` (35 tests) and
`python3 tscircuit/scripts/fab_check.py` pass after the sourcing edit, and
`bun run build` + `bun run test` pass in `tscircuit/` (145 tests, 0 fail) once
the worktree has `bun install` and a `dist/` build — a fresh worktree has
neither, so a bare `bun test` fails on `dist/front/circuit.json` before the
`make test` order (build, then test) is used. Renders,
panel preview, fab outputs and `jlc-order.json` counts were **not** regenerated
because `layout.ts` did not change: the generated artifacts are still correct
for the 26HP panel. The full `make test` / `make order` runs are for the
follow-up that actually changes geometry.

## 5. Sourcing + docs + fingerprint — PARTIAL

* `scripts/sourcing-extras.csv`: DISP1, CAB_DSI and STANDOFF_LCD rows updated
  (ElectroKit 4103 5687, the three included DSI cable lengths, kit pillars +
  the module's own 4 mm-class SMTSO bosses). Sourcing parse still passes.
* `docs/decisions.md`, `README.md`, `docs/USER-MANUAL.md`,
  `docs/ui-refresh-spec.md` §7, `docs/touch-testing-checklist.md`,
  `docs/mechanical-stack.md`: **NOT updated** — the HP choice and the aperture
  size are unresolved (2a/2b), so writing the new numbers now would record
  geometry that does not exist.
* `hardware-acceptance.json` fingerprint: **NOT refreshed** (it changes only
  with a real source change; the sourcing CSV edit does change the hash input —
  the follow-up must re-run `bun run scripts/release_gate.ts --fingerprint` and
  record the new value in the same commit as the layout change).

## Reference: UI mechanical rationale (for the follow-up doc edits)

Old active area 75.8 x 45.36 mm (4.2 mm per 44 px). New active area
95.04 x 53.856 mm: 0.1188 x 0.1122 mm per pixel, so

* the 44 px minimum target is now **5.23 mm** (was 4.2 mm);
* the 64 px primary target is now **7.60 mm** (was ~6.1 mm);
* (px targets themselves stay identical — same 800 x 480 panel).

## Physical first-article checks the owner must do (unchanged in kind)

1. Display seating: glass to panel aperture registration on all four sides, and
   whether the glass or the PCB boss plane is what the panel must clear.
2. Cable bend: which of the 50/120/160 mm cables fits the real front-board →
   Pi 5 DSI1 run, plus bend radius and latch clearance.
3. Touch alignment: capacitive mapping against the new aperture (5-point, and
   the 64 px target at 7.6 mm).
4. Boss/standoff stack: the module's 4 mm-class SMTSO bosses + kit pillars
   against the provisional plane separation — the 14.1 mm panel gap is still
   **provisional-with-numbers**, not a measurement.

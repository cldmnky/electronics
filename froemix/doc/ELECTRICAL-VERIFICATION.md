# Electrical verification — MIDI in/out (base board)

Scope: the MIDI receiver and transmitter on the base board. This document is the
evidence behind the third review round in `doc/DESIGN-REVIEW.md` (R01/R02) and
the numbers in `doc/FRONTBOARD-INTERFACE.md` §2. It records what was checked
against a primary source on disk, what could only be bounded because the source
was not reachable, and what remains a physical measurement.

Nothing here changes the mechanical contract: no pad, pin number, net or
placement moved. The built circuit still has **102 nets and 429 pads**, all
connector and control centres are unchanged, and no PCB part is rotated.

**No hardware was measured for this document.** Every current and power below is
arithmetic from a data sheet or a circuit value; every physical item that needs
a bench (MCU levels, loop current, receiver threshold, derating in a closed
rack) is listed as open in §7. Nothing here is a bench result.

## 1. What changed, and what did not

| Ref | Before | Now | Why |
| --- | --- | --- | --- |
| U15 | TLP2362 (`C37658`) | **TLP2361** (`C107626`, `TLP2361(TPL,E`) | TLP2362 needs IFHL ≤ 5.0 mA with a 7.5 mA recommended on-current; this board's own 3.3 V transmitter delivers 5.34 mA worst case. TLP2361 is the same Toshiba **SO6 11-4L1S** with the same pin map and IFHL ≤ 1.6 mA at 125 °C. |
| Q2 | MMBT3906 PNP (`C53444`) | **AO3401A** P-channel MOSFET (`C15127`, AOS Rev 3.1) | No base current, much lower drop, −4.0 A rating. Pins 1/2/3 = gate/source/drain. |
| R58 | 4.7 kΩ base resistor (`C23162`) | **180 Ω** gate resistor (`C22828`, `0603WAF1800T5E`) | Bounds the gate charge peak at 19.44 mA and still reaches the −2.5 V RDS(on) point. |
| R61 | 10 kΩ base pull-up (`C25804`) | **2.2 kΩ** gate pull-up (`C4190`, `0603WAF2201T5E`) | Holds the gate at the source while `USART1_TX` is high or high-Z. |
| R59 | 22 Ω 1 W (`C118189`) | **33 Ω** 1 W (`C53728`, `25121WF330JT4E`) | The resistor in the MOSFET/ring leg takes CA-033's 3.3 V source value. |
| R60 | 33 Ω 1 W (`C53728`) | **22 Ω** 1 W (`C118189`, `25121WF220JT4E`) | The permanently grounded tip leg has no transistor in it; 22 Ω keeps a hard fault inside 1 W. |

Unchanged on purpose: the Type A wiring (ring = source, tip = sink), R47 220 Ω
and D1 in the receiver, the J1 Type A/B shunt block, R48 2.2 kΩ, C28 100 nF, and
every net name including `NET_Q2_B`/`NET_Q2_C` (they now carry the gate and the
drain).

Two element-level facts, both verified in the installed core:

- `<mosfet>` **cannot** carry `pinLabels`: `mosfetProps` has no such field, the
  zod schema strips it, and the element hard-wires the schematic symbol order
  pin1 = drain, pin2 = source, pin3 = gate. That is the reverse of the AO3401A's
  SOT-23 pinout, so the tree uses
  `<transistor type="mosfet" symbolName="p_channel_e_mosfet_transistor_gate_left_drain_bottom">`
  with explicit `pinLabels={{pin1:"gate",pin2:"source",pin3:"drain"}}`.
- A non-default `sheetWidth`/`sheetHeight` on `<schematicsheet>` raises a
  `schematic_sheet_styling_warning` in this core, so the schematic sheets stay A4
  and the notes have to fit inside the standard frame.

## 2. Primary sources used

| Source | Used for |
| --- | --- |
| Toshiba **TLP2361** data sheet Rev 5.0 (2015-11-02), 16 pages, on disk | VF ≤ 1.65 V at IF = 2 mA; IFHL ≤ 1.3 mA (−40…105 °C) / ≤ 1.6 mA (−40…125 °C); recommended IF 2…6 mA, VF(OFF) 0…0.8 V, VCC 2.7…5.5 V; VOL ≤ 0.4 V at 3.2 mA, VOH ≥ 3.2 V at −20 µA; inverting totem-pole output; 0.1 µF between pins 6 and 4; package 11-4L1S with pins 1 = A, 3 = K, 4 = GND, 5 = VO, 6 = VCC; IF absolute maximum 10 mA (pulsed 40 mA) |
| Toshiba **TLP2362** data sheet Rev 6.0 (local copy `review-c37658.pdf`; the old vendor link now serves a 404 page) | the replaced part's IFHL ≤ 5.0 mA and IF(ON) from 7.5 mA, and that its package and pin map are identical |
| AOS **AO3401A** Rev 3.1 (December 2023), on disk | RDS(on) ≤ 85 mΩ at VGS = −2.5 V, ≤ 60 mΩ at −4.5 V, 50 mΩ at −10 V and 75 mΩ at 125 °C; VGS(th) −0.5…−1.3 V; VGS ±12 V; ID −4.0 A (25 °C) / −3.2 A (70 °C); IDSS −1 µA (25 °C) / −5 µA (125 °C) at VGS = 0; Qg(4.5 V) 7 nC; Ciss 645 pF; SOT-23 pinout G/S/D |
| **Uniroyal** thick-film chip resistors, Feb 12 2019 V.3, on disk | `25121WF330JT4E` = 2512 body, 1 W (codes 5–6 "1W"), F = ±1 %, 330J = 33.0 Ω; 1 W is the rating up to 70 °C ambient, derated above it |
| **MIDI CA-033** (MIDI 1.0 Electrical Specification Update, 2014), on disk | 3.3 V ±5 % signalling; RA 33 Ω / RC 10 Ω recommended; the receiver is an LED + 220 Ω with VF ≤ 1.9 V and "must require less than 5 mA to turn on"; the short-circuit guidance that produced the 0.5 W RA requirement |
| LCSC product page `https://www.lcsc.com/product-detail/C22828.html` (HTTP 200, 410 103 bytes, SHA-256 `25e436cafe13d129da78bb53350b71c56622679d14d6eaed922fc37b3a4db61c`) and the JLCPCB parts API answer for `C22828` (7 934 bytes, SHA-256 `214e1ebe104c1c591deb3d9809f838e3590e6e2fc39c0afd5b88890232ea2219`) | the **180 Ω** gate resistor is `C22828` = `0603WAF1800T5E`, UNI-ROYAL, 0603, "100mW 180Ω 75V Thick Film Resistor ±1%", 2 232 417 in stock at the time of the query |
| LCSC part pages (fetched, quoted in the BOM) | that `C22828` = 0603WAF1800T5E (180 Ω 1 %), `C4190` = 0603WAF2201T5E (2.2 kΩ), `C53728` = 25121WF330JT4E, `C118189` = 25121WF220JT4E, `C15127` = AO3401A, `C107626` = TLP2361(TPL,E — no part number in this document is a guess |

**Not verified — stated as an assumption.** The **STM32H750** data sheet could
not be obtained from this host: `st.com` fails on both HTTP/2 and HTTP/1.1 from
here (curl exits 92 / times out with 0 bytes), the `.com.cn` host and a Keil
mirror path are not available, and no mirror carrying the primary text was
found. Every MCU number below is therefore an **assumption**, marked
`ASSUMPTION`, and the physical gate stays open:

- per-I/O DC absolute maximum **±20 mA** (some STM32 families specify 25 mA);
- VOL ≤ **0.4 V** and VOH ≥ **VDD − 0.4 V** at the rated output current;
- the reset state of `USART1_TX` is **high-Z** (analog/input, no internal
  pull-down). If the part actually enables an internal pull-down in reset, the
  gate divider moves by that resistance and the off-state margin below must be
  re-derived — the bound in §4 is the conservative case only if the pin is not
  pulled down internally.

The prior claim "the MCU pin limit is 20 mA" that an earlier report quoted from
memory is **not** a verified figure; do not treat it as one.

## 3. Transmitter: loop current and the linear chain

All quantities are computed from the values in the **built** circuit
(`tools/check_elec.py`), not copied from a comment. Symbols: VDD = 3.3 V,
tolerance ±5 % (CA-033), resistors ±1 %, RDS(on) at 125 °C = 85 mΩ × 1.5 =
**127.5 mΩ** (the data sheet's own temperature factor on the −10 V spec).

| Case | Equation | Result |
| --- | --- | --- |
| Loop into a CA-033 receiver (LED + 220 Ω, VF 1.9 V) | (3.135 − 1.9) / (55 Ω × 1.01 + 220 Ω × 1.05 + 0.1275) | **4.31 mA** |
| Same, with 1.65 V typical VF and no tolerances | (3.3 − 1.65) / (55 + 220 + 0.085) | 6.00 mA |
| CA-033's own 43 Ω legs, same receiver | (3.135 − 1.9) / (43 × 1.01 + 231) | 4.50 mA |
| Gate charge peak | 3.465 / (180 × 0.99) | **19.44 mA** |
| Gate voltage, pin low at VOL 0.4 V | divider at the tolerance corners: 180 Ω +1 % against 2.2 kΩ −1 % | 0.6107 V → **VGS = −2.5243 V** |
| Gate voltage, pin high at VOH min | 180 Ω −1 % against 2.2 kΩ +1 % | 2.7647 V → **\|VGS\| = 0.3703 V** |
| R61 current while the pin is low | 3.135 / 2200 | 1.42 mA (1.5 mA at 3.3 V) |
| Gate edge | 180 Ω × 645 pF | 116 ns (one bit is 32 µs) |

Two consequences worth stating plainly:

- **4.31 mA against CA-033 — three different figures, kept apart.**
  1. **Specified nominal loop: 5 mA.** CA-033: "The MIDI circuit is a 5 mA
     current loop", and the receiver "must require less than 5 mA to turn on".
  2. **PC900V benchmark: 4 mA.** CA-033 quotes a PC900V worst-case forward
     current requirement of 4 mA at RD = 280 Ω in a 5 V receiver.
  3. **43 Ω reference calculation: 4.472 mA.** That is what CA-033's own
     recommended 3.3 V values (RA 33 Ω source, RC 10 Ω sink) produce under its
     stated assumptions (3.135 V, +5 % resistors, 1.9 V VF). It is a figure
     CA-033 calculates from its recommendation, not a stated minimum.
  Under the same ±5 % assumptions this board's 55 Ω legs give **4.277 mA**
  (4.31 mA with its 1 % parts): **0.277 mA above the PC900V benchmark**,
  **0.195 mA below the 43 Ω reference calculation**, and **below the 5 mA
  nominal**. The 22 Ω tip resistor is the fault-driven choice (§4), so the legs
  are 55 Ω rather than 43 Ω. Consequence: a receiver whose turn-on threshold
  sits above about 4.3 mA at 3.3 V can be marginal with this transmitter. That
  is a **documented limitation of the transmitter, not a clean margin and not a
  measured result**; recovering it needs a different fault strategy on the tip
  leg.
- **VGS(on) = −2.5243 V** at the divisor's tolerance corner (180 Ω +1 % against
  2.2 kΩ −1 %, VOL 0.4 V) is just inside the −2.5 V RDS(on) spec point, so the
  85 mΩ figure applies. If the MCU's VOL were worse than 0.4 V the FET would be
  less enhanced — the assumption is what protects this number, which is why it
  must be measured.

## 4. Faults, with the correct bounding assumptions

Zero RDS(on) is the **worst case for the series resistor** (it carries the full
supply) and the **best case for the FET**; the RDS(on) maximum is the reverse.
Both are computed, and the leg with no transistor is computed on its own.

| Fault (driver on) | Zero RDS(on) | RDS(on) = 127.5 mΩ |
| --- | --- | --- |
| Ring to GND: current | **106.1 mA** | 105.6 mA |
| Ring to GND: R59 (1 W part) | **0.368 W** | 0.365 W |
| Ring to GND: Q2 dissipation | 0 W (by assumption) | **1.42 mW** |
| Tip forced to +3.465 V: current | **159.1 mA** | n/a — **no MOSFET in the tip leg** |
| Tip forced to +3.465 V: R60 (1 W part) | **0.551 W** | n/a |

- R59's 0.368 W is 37 % of its 1 W rating; R60's 0.551 W is 55 %. The 2512
  rating holds to 70 °C ambient and derates above it, which matters in a closed
  rack — the derating curve is in the data sheet, section 9.
- 106 mA is far inside the AO3401A's −3.2 A at 70 °C and its −27 A pulsed
  rating; the FET is not the weak part in any of these faults.
- 22 Ω on the tip rather than CA-033's 10 Ω is exactly what keeps the
  permanently grounded leg inside 1 W: 10 Ω would dissipate 1.20 W and fail.
- Body diode: with the ring forced above 3.3 V by a mis-plugged second
  transmitter, the AO3401A's body diode conducts into `P3V3_D`. The external
  source's 220 Ω limits that to a few mA, the rail absorbs it, and VDS stays far
  inside −30 V. Recorded, not protected against; the PNP it replaced had the
  same reverse path via its collector-base junction.

## 5. Receiver: TLP2361 on the same loop

| Case | Result | Against |
| --- | --- | --- |
| This board's own transmitter (220 Ω R47, VF 1.65 V) | **5.34 mA worst**, 6.54 mA typical | IFHL ≤ 1.6 mA at 125 °C → **3.34× margin**; recommended 2…6 mA (typical point above it; 10 mA absolute maximum at 25 °C, 8.05 mA derated at 125 °C) |
| A 5 V legacy transmitter with 2 × 220 Ω | **5.63 mA** | inside 6 mA recommended, inside the 8.05 mA derated absolute maximum at 125 °C |
| A non-compliant 5 V transmitter with 33/10 Ω legs | **14.79 mA** | **above the 10 mA figure at 25 °C and the 8.05 mA derated figure at 125 °C** — pending, see §7 |
| The replaced TLP2362 on the same loop | 5.34 mA | 1.07× its 5 mA IFHL and 0.71× its 7.5 mA recommended on-current → **not guaranteed**, which is why the part changed |

The receiver's logic is unchanged: TLP2361 is inverting with a totem-pole
output, so loop current off (UART idle) gives VO high, and a start bit pulls VO
low. VOH is ≥ 3.2 V at −20 µA (the MCU input draws microamps), VOL is ≤ 0.4 V
into the 3.2 mA rating, and R48's 1.5 mA flows only when VO is low.

## 6. Schematic: supported layout, measured on the rendered SVG

The laid-out sheets are a drawing, not a connectivity artefact: every number in
this section comes from the exported SVG, not from a text-width estimate.

### 6.1 Probe first: does an explicit position survive?

Before touching the board, a throwaway probe on the **pinned core** asked the one
question the layout depends on — `<schematicsheet><group schRelative>`, three
parts with explicit `schX`/`schY`/`pcbX`/`pcbY`, one explicit `<netlabel>`:

| Given | Came back in `dist/probe/circuit.json` |
| --- | --- |
| R1 `schX=-4 schY=2` | schematic centre `(-4, 2)` |
| R2 `schX=2 schY=2` | `(2, 2)` |
| C1 `schX=2 schY=-3` | `(2, -3)` |
| R1 `pcbX=-5 pcbY=3` | PCB centre `(-5, 3)`, `pcb_group_0` (the board group) |
| C1 `pcbX=4 pcbY=0` | `(4, 0)`, `pcb_group_0` |
| `<netlabel schX=-8 schY=2 anchorSide="left">` | `anchor_position (-8, 2)`, `anchor_side left` |
| nets | `A, B, C, GND` — global, no scope of their own |

Conclusions, all from that run: explicit schematic coordinates **are** honoured
inside a relative group; a group inside a sheet is **not** a subcircuit
(`source_group g1` has `is_subcircuit` unset; only the board group is a
subcircuit), so net names stay global; and `pcbX`/`pcbY` are passed through
untouched, so the group cannot move a part on the board.

### 6.2 The Root sheet: explicit placement

`schSectionName` is gone from every manually controlled sheet. The Root sheet is
one non-subcircuit `<group name="root-layout" schRelative>` with all 18 parts at
explicit coordinates (schematic units; the A4 drawing area is ±16.78 × ±11.87):

| Part | schX, schY | Part | schX, schY |
| --- | --- | --- | --- |
| R53 | −12.0, 5.5 | CN2 | −11.5, 1.8 |
| R55 | −9.8, 5.5 | DaisySeed1 | −5.5, 0.5 (box 2.6 × 6.0) |
| R57 | −7.6, 5.5 | CN1 | 2.5, 1.8 |
| R52 | −5.4, 5.5 | CN3 | 8.0, 1.0 |
| R51 | −3.2, 5.5 | CN4 | 8.0, −2.2 |
| R54 | −1.0, 5.5 | C31 | −13.0, −2.5 |
| R56 | 1.2, 5.5 | U1 | −10.5, −6.5 |
| C1 | 0.5, −5.0 | H1 | −4.0, −6.5 |
| C29 | 3.5, −5.0 | C30 | 6.5, −5.0 |

The layout reserves each part's label fan-out (labels grow away from a pin by up
to ~1.9 units), the seven pull-ups sit in one row at `schRotation={90}` so their
`P3V3_D` pin faces up (the placement checker's own suggestion, which is why it
now reports **0** `TwoPinComponentShouldBeVertical`), and the Daisy's box was
widened to 2.6 × 6.0 so its 21 left rows get 0.286 units of pitch instead of the
default 0.2.

**The exported page follows the content, not the origin.** Moving the drawing
to the page edges was measured, not assumed: with a `<schematictext>` at
`schX=-15.6` the exported A4 frame sits at −23.49 … +8.67, and the same sheet
without that text at −17.08 … +15.08. Page placement is therefore a property of
the content, so the layout is kept compact (labels x −14.08 … +9.67, y −7.63 …
+6.18; notes at x −12.1, y 11.1 … 7.95) and `tools/svg_overlaps.py` checks that
every label tag lies inside the frame of its own sheet — it does, with 1.6 units
of margin on the tight side.

**The board did not move.** `bunx tsci snapshot --test --3d` still reports
`index.circuit-pcb.snap.svg` ✅ and `index.circuit-3d.snap.png` ✅ while only the
schematic differs, i.e. the PCB is byte-identical; `check:footprints` still
finds 429 pads and `check:netlist` still 102 nets.

### 6.3 Measured before/after (identical tools on both trees)

`bunx tsci check schematic-placement`, frozen baseline `e0606da` (read-only
worktree, same build command) versus this tree:

| Finding | `e0606da` | this tree |
| --- | --- | --- |
| `TwoPinComponentShouldBeVertical` | 54 | **0** |
| `ComponentNetLabelCollision` | 4 | **2** (Audio, MIDI) |
| `SchematicTextCollision` | 3 | **1** (Audio) |
| `DiodeResistorNotAligned` | 3 | 4 |

`tools/svg_overlaps.py` on both **exported SVGs** — it reads the rendered label
tag polygons, so these are the overlap counts a reader would see. Its policy is
an overlap of **more than 12 % of the smaller box**; boxes that only share an
edge do not count, and the tool prints the strict (threshold 0) count next to it
so a hairline contact is never hidden:

| Sheet | label/label before → after | label/text before → after |
| --- | --- | --- |
| Root | **18 → 0** | **13 → 0** |
| MIDI | 0 → 0 | 0 → 0 |
| Audio | 0 → 0 | **4 → 0** |
| CV | 0 → 0 | **1 → 0** |
| Power | 0 → 0 | 0 → 0 |
| **total** | **18 → 0** | **18 → 0** |

No label tag sits on a component body on any sheet either (measured the same
way: 0 of 135 tags). `tools/svg_overlaps.py --expect built-in` records all five
sheets at `0/0`, so the check has no headroom on that number: any overlap above
the 12 % area allowance on any sheet fails. The tool prints a second line with
the **strict** threshold
(any intersection at all, no area allowance): this tree has **1 label/label and
4 label/text hairline contacts** left (Root 1/1, MIDI 0/1, Audio 0/2) against
21/28 on the baseline. Those five are box corners that touch without covering
text; they are recorded here rather than argued away, and the two-round budget
did not include chasing them. A 0/0 report is only accepted when the file passes
the tool's structure gate: well-formed SVG, all five expected sheets, and at
least one net label, one component and one text on each of them, so a frame-only
or empty export cannot report a clean sheet.

`tools/test_svg_overlaps.py` proves that gate with synthetic files: truncated
XML, an empty document, a frame-only export, a missing sheet, a real
label-on-label collision, a label over a value text, a hairline contact that only
the strict line reports, and a label outside the page frame.

### 6.4 Audio and CV: frozen positions, then one nudge

The same pattern is used on the two analog sheets, in two bounded rounds:

1. **Freeze.** `lib/schematic-layout.ts` holds `AUDIO_LAYOUT` and `CV_LAYOUT`,
   the exact positions the installed core had produced on its own (34 and 42
   entries). Both sheets became one non-subcircuit `<group schRelative>` using
   those coordinates, with `schSectionName` removed. The drawing is the reviewed
   one; only the numbers moved out of the layout engine into a named map.
2. **Nudge.** Two residual overlaps were left, both on Audio: the `VCC` label
   tag was drawn on C11's `100nF` value text and a `U3A_IN_N` tag on its `C11`
   reference text. The decoupling pair C10/C11 moved up by 0.6 units as a block
   in `AUDIO_LAYOUT`; nothing else moved, no PCB coordinate is in that file.

After round 2 every sheet measures 0/0 at the 12 % area threshold, 1 label/label
and 4 label/text at strict zero tolerance (§6.3), and 0 label tags over a
component body. `tsci check schematic-placement` still reports
stylistic items — 3 `TwoPinComponentShouldBeVertical` on CV (R25, R34, R43) and
4 `DiodeResistorNotAligned`, plus 2 `ComponentNetLabelCollision` groups and 1
`SchematicTextCollision` whose boxes the renderer does not reproduce (measured:
no tag over a body, no tag over a text). Those are listed as stylistic, not
closed, and no further layout round was spent on them.

### 6.5 What is still not clean

No label/label or label/text overlap remains on any sheet at the 12 % area
threshold (§6.3–6.4). Pin names drawn inside a symbol box are rendered by the
symbol renderer and are not circuit JSON; the SVG tool does measure them (they
are ordinary `<text>` elements). At strict zero tolerance 1 label/label and
4 label/text hairline contacts remain (box corners touching without covering
text), reported on every run beside the policy count. **The drawing is not a
finished production service schematic**; it is a measured, reproducible
improvement with the remaining contacts and the unreproduced native box-pair
notes counted exactly.

## 7. Open items (physical or pending)

1. **MCU limits are assumptions** (§2). Measure `USART1_TX` VOL/VOH and the
   reset state on a real Seed, then re-run `tools/check_elec.py` with the real
   numbers instead of the ASSUMPTION constants.
2. **Loop current 4.31 mA vs CA-033's 4.50 mA** reference. Verify against the
   receivers actually in use; if one needs more, the tip leg's fault strategy
   has to change (it is the only resistor that is not behind the FET).
3. **Typical loop current 6.54 mA** into this board's own receiver is above the
   TLP2361's 6 mA recommended on-current but inside the 8.05 mA derated absolute
   maximum at 125 °C. The strongest legal 3.3 V sender corner (8.1754 mA
   measured, 8.1771 mA reference) is inside the 25 °C 10 mA figure and
   **outside the 125 °C 8.05 mA derated figure**; the deck asserts both facts,
   the second as a negative check. The 1.35 V VF in that corner is a 25 °C
   datasheet number and −2 mV/°C is a typical coefficient, not a guaranteed
   bound, so an illustrative hot VF (1.15 V → ≈8.95 mA) is also asserted outside
   the derated figure. This is an explicit high-current/temperature **risk
hold**: the model does not resolve it, no operating-temperature range is
   declared, and no bench approval is implied. Measure IF on a real unit across
   temperature before any high-current claim.
4. **Non-compliant 5 V sources** using 33/10 Ω legs would drive 14.79 mA into
   the receiver, above both the 25 °C and the derated 125 °C absolute-maximum
   figures. Nothing on this board clamps the input loop; a series resistor or
   clamp would be a board change.
5. **U15 is pad-only in 3D** (the C37658 EasyEDA model belonged to the old
   part, and no verified model for C107626 exists). Same policy as the other
   models without verified offsets (AGENTS.md §6).
6. **Schematic: the measured collisions are closed, the drawing is not
   finished.** All five sheets are 0 label/label and 0 label/text in the
   rendered SVG, and no label tag sits on a component body (§6.3). What remains
   is stylistic, not a collision: 3 `TwoPinComponentShouldBeVertical` on CV and
   4 `DiodeResistorNotAligned`, plus native `ComponentNetLabelCollision`/
   `SchematicTextCollision` boxes that the renderer does not reproduce. The
   drawing is **not** a finished production service schematic: chip splitting
   for the TL074s (`<schematicbox>`), per-sheet `sheetWidth`/`sheetHeight` if
   upstream stops warning about non-default sizes, and the CV orientation notes
   are the next steps.

## 8. Reproduce

```bash
cd froemix/tscircuit
bunx tsci build                                  # writes dist/index/circuit.json
python3 ../tools/check_elec.py                   # electrical model, 22 checks
python3 ../tools/test_elec.py                    # negative tests for that model
python3 ../tools/check_schematic.py              # note geometry (circuit JSON)
bunx tsci export dist/index/circuit.json -f schematic-svg -o /tmp/froemix-sch.svg
python3 ../tools/svg_overlaps.py --expect built-in /tmp/froemix-sch.svg   # rendered overlaps
python3 ../tools/check_extras.py                 # pin/net map of the changed parts
python3 ../tools/check_values.py                 # literals vs build and MPN
python3 ../tools/check_footprints.py             # 429 pads, unchanged
python3 ../tools/check_netlist.py --kicad-cli /path/to/kicad-cli   # 102 nets
KICAD_CLI=/path/to/kicad-cli python3 ../tools/test_kicad_cli.py    # resolver tests
```

To reproduce the before/after numbers of §6.1, build the frozen baseline in a
throwaway worktree and run the same two tools on it:

```bash
git worktree add --detach /tmp/froemix-baseline e0606da
cd /tmp/froemix-baseline/froemix/tscircuit && bun install && bunx tsci build
bunx tsci export index.circuit.tsx -f schematic-svg -o /tmp/base.svg
bunx tsci check schematic-placement dist/index/circuit.json
python3 <this tree>/froemix/tools/check_schematic.py --circuit dist/index/circuit.json
```

`check_elec.py` and `check_schematic.py` are not in `scripts/gate.sh` yet (that
file belongs to the gate work); run them until the gate owner adds them.

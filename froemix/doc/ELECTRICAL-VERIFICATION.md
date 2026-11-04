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
| Toshiba **TLP2362** data sheet Rev 6.0, on disk | the replaced part's IFHL ≤ 5.0 mA and IF(ON) from 7.5 mA, and that its package and pin map are identical |
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
| Gate voltage, pin low at VOL 0.4 V | divider 180 Ω / 2.2 kΩ | 0.607 V → **VGS = −2.528 V** |
| Gate voltage, pin high at VOH min | (2.735/180 + 3.135/2200) / (1/180 + 1/2200) | 2.764 V → **\|VGS\| = 0.370 V** |
| R61 current while the pin is low | 3.135 / 2200 | 1.42 mA (1.5 mA at 3.3 V) |
| Gate edge | 180 Ω × 645 pF | 116 ns (one bit is 32 µs) |

Two consequences worth stating plainly:

- **4.31 mA against CA-033's own example, read correctly.** CA-033 states a
  5 mA current loop and requires the receiver to turn on with **less than
  5 mA**; it sets **no minimum transmitter current**. Its recommended 3.3 V
  values (RA 33 Ω source, RC 10 Ω sink) would give **4.47 mA** worst case under
  its own ±5 % resistor assumption — that is an example computed from those
  values, not a normative 4.47/4.50 mA floor. Under the same assumptions this
  board's 55 Ω legs give **4.28 mA** (4.31 mA with its 1 % parts), i.e.
  **0.19 mA below that example** and **0.28 mA above the 4 mA worst-case
  current** CA-033 quotes for the PC900V. The 22 Ω tip resistor is the
  fault-driven choice (§4), so the legs are 55 Ω, not 43 Ω. Consequence: a
  receiver whose turn-on threshold sits above about 4.3 mA at 3.3 V can be
  marginal with this transmitter. That is a **documented limitation of the
  transmitter, not a clean margin and not a measured result**; recovering it
  needs a different fault strategy on the tip leg.
- **VGS(on) = −2.528 V** is just inside the −2.5 V RDS(on) spec point, so the
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
| This board's own transmitter (220 Ω R47, VF 1.65 V) | **5.34 mA worst**, 6.54 mA typical | IFHL ≤ 1.6 mA at 125 °C → **3.34× margin**; recommended 2…6 mA (typical point above it, absolute maximum 10 mA) |
| A 5 V legacy transmitter with 2 × 220 Ω | **5.63 mA** | inside 6 mA recommended, inside 10 mA absolute |
| A non-compliant 5 V transmitter with 33/10 Ω legs | **14.79 mA** | **above the 10 mA absolute maximum** — pending, see §7 |
| The replaced TLP2362 on the same loop | 5.34 mA | 1.07× its 5 mA IFHL and 0.71× its 7.5 mA recommended on-current → **not guaranteed**, which is why the part changed |

The receiver's logic is unchanged: TLP2361 is inverting with a totem-pole
output, so loop current off (UART idle) gives VO high, and a start bit pulls VO
low. VOH is ≥ 3.2 V at −20 µA (the MCU input draws microamps), VOL is ≤ 0.4 V
into the 3.2 mA rating, and R48's 1.5 mA flows only when VO is low.

## 6. Schematic: what was changed, and the before/after numbers

The rendered schematic comes from the installed core's auto-layout; on this
version there is no supported way to pin a component to an exact `schX`/`schY`
inside `<schematicsheet>` (`_getSchematicLayoutMode` falls back to
`match-adapt`, and a section's members are packed again inside the section). All
changes therefore use elements the installed core supports, and every number
below was measured twice with the **same** tool on **two** trees: the frozen
baseline `e0606da` (read-only worktree, built the same way) and this tree.

1. **Component orientation.** `tsci check schematic-placement` flags every
   two-pin part whose power-connected pin does not face up and prints the exact
   `deltaSchRotation`. Applying those deltas as the supported `schRotation` prop
   (24 on the CV sheet, 28 on Audio, 2 on Power) removes **54 of 54** such
   findings. This is a layout fix in the schematic, not a PCB change: no pad,
   no net and no placement moves.
2. **Functional grouping.** `schSectionName` groups the sheets that have two
   natural halves (audio in/out, CV/sync, 5 V entry/converter, MIDI in/out). The
   core lays each group out and then arranges the groups, which is what
   separates the MIDI receiver from the transmitter. The Root sheet keeps a
   single group on purpose: two groups there **added three** label collisions
   (18 → 21), so the grouping was removed there and the Root sheet matches the
   baseline again.
3. **Sheet notes.** 6…10 `<schematictext>` lines per sheet (rails, direction,
   values and part numbers, decoupling, interface warnings), placed in the free
   band at the top of the A4 frame.
4. **`<schematicsection>` titles were tried and removed.** With more than two
   groups the section bounds overlap, so the titles were drawn on top of each
   other and over the parts. `schSectionName` without the title element keeps
   the grouping and loses the collision.

### 6.1 Identical checks, baseline `e0606da` → this tree

`tsci check schematic-placement`, same tool, same input (`dist/index/circuit.json`
from each tree):

| Finding | `e0606da` | this tree |
| --- | --- | --- |
| `TwoPinComponentShouldBeVertical` | **54** | **0** |
| `ComponentNetLabelCollision` groups | 4 | 3 |
| `SchematicTextCollision` | 3 | 1 |
| `DiodeResistorNotAligned` | 3 | 4 |

`tools/check_schematic.py`, same tool, same input:

| Sheet | label/label before | label/label after | notes added |
| --- | --- | --- | --- |
| main | 18 | 18 | 8 |
| midi | 0 | 0 | 10 |
| audio | 0 | 0 | 7 |
| cv | 1 | 1 | 7 |
| power | 0 | 0 | 6 |
| **total** | **19** | **19** | 38 |

Rendered SVG, same estimator on both exports (every `<text>` box, 0.55 ×
font-size width model; the baseline export and this tree's export were produced
by the same command):

| Sheet | overlapping text pairs before | after |
| --- | --- | --- |
| Root | 178 | 178 |
| MIDI | 21 | 12 |
| Audio | 47 | 34 |
| CV | 58 | 40 |
| Power | 5 | 5 |
| **total** | **309** | **269 (−40, −12.9 %)** |

So: the render improves by 40 overlapping text pairs, the placement checker
loses all 54 orientation findings, and the label/label count is **unchanged, not
improved**. Nineteen label/label collisions remain, 18 of them in the packed
Daisy/header cluster on the Root sheet. **This schematic is not a clean
production service drawing and is not claimed to be one**; what it is, is a
measured improvement over the baseline with the remaining defects counted.

**What cannot be measured here:** pin names drawn *inside* a symbol box are
rendered by the schematic-symbol renderer and are not circuit JSON, so a
programmatic "label over pin name" count only covers port labels. Those
collisions stay a human/visual check.

## 7. Open items (physical or pending)

1. **MCU limits are assumptions** (§2). Measure `USART1_TX` VOL/VOH and the
   reset state on a real Seed, then re-run `tools/check_elec.py` with the real
   numbers instead of the ASSUMPTION constants.
2. **Loop current 4.31 mA vs CA-033's 4.50 mA** reference. Verify against the
   receivers actually in use; if one needs more, the tip leg's fault strategy
   has to change (it is the only resistor that is not behind the FET).
3. **Typical loop current 6.54 mA** into this board's own receiver is above the
   TLP2361's 6 mA recommended on-current (inside the 10 mA absolute maximum).
   It is inside spec but at the top of the window; measure IF on a real unit.
4. **Non-compliant 5 V sources** using 33/10 Ω legs would drive 14.79 mA into
   the receiver, above the 10 mA absolute maximum. Nothing on this board clamps
   the input loop; a series resistor or clamp would be a board change.
5. **U15 is pad-only in 3D** (the C37658 EasyEDA model belonged to the old
   part, and no verified model for C107626 exists). Same policy as the other
   models without verified offsets (AGENTS.md §6).
6. **Schematic label collisions are reduced, not closed.** 19 label/label
   collisions remain (18 on the Root sheet), and the pin-name collisions inside
   the symbol boxes cannot be counted at all (§6.1). The drawing is **not**
   production-ready as a service schematic. The next real improvements are chip
   splitting for the Daisy and the TL074s with `<schematicbox>` (one box per
   unit, so a 39-pin box stops sharing a row with the headers), and per-sheet
   `sheetWidth`/`sheetHeight` if upstream stops warning about non-default sizes.

## 8. Reproduce

```bash
cd froemix/tscircuit
bunx tsci build                                  # writes dist/index/circuit.json
python3 ../tools/check_elec.py                   # electrical model, 22 checks
python3 ../tools/test_elec.py                    # negative tests for that model
python3 ../tools/check_schematic.py              # note/label diagnostics
python3 ../tools/check_extras.py                 # pin/net map of the changed parts
python3 ../tools/check_values.py                 # literals vs build and MPN
python3 ../tools/check_footprints.py             # 429 pads, unchanged
python3 ../tools/check_netlist.py --kicad-cli /path/to/kicad-cli   # 102 nets
KICAD_CLI=/path/to/kicad-cli python3 ../tools/test_kicad_cli.py    # resolver tests
bunx tsci export index.circuit.tsx -f schematic-svg -o /tmp/froemix-sch.svg
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

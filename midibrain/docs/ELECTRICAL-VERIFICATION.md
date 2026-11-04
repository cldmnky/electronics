# Electrical verification — what was checked, what was not

Status: **prototype definition.** Nothing here is a fabrication release, a
hardware measurement or a signal-integrity claim. Every number comes from a
datasheet limit, a stated tolerance, or the built circuit JSON in `dist/`.

Branch: `feat/midibrain-hardware-electrical`.
Boards: rear/base `index.circuit.tsx` (101.6 × 112 mm, 2 layers, parts on the
bottom) and front/UI `front.circuit.tsx` (128 × 112 mm, 2 layers, parts on the
top).

## 1. How to reproduce

```sh
cd midibrain/tscircuit
bun install --frozen-lockfile
bun run build                      # writes dist/index, dist/front, dist/panel
python3 scripts/check_electrical.py # independent electrical checks (73)
python3 scripts/test_electrical.py  # negative mutations (9 cases)
bun run typecheck && bun test lib/*.test.ts && bun run placement
cd ../spice && python3 run_tests.py
```

`make test` at `midibrain/` runs the same set in the project's order.

## 2. Connector pin mapping (the main defect fixed)

`J_PI` (Pi 40-pin) and `J_FRONT` (2 × 13 interconnect) used the generic
`pinrow40_p2.54mm_rows2` and `pinrow26_p2.54mm_rows2` footprints. Those number
pads around the perimeter: pins 1…20 down one row, 21…40 back along the other.
A 2 × N IDC header and the Raspberry Pi GPIO header number pins the other way:
odd pins in one row, even pins in the other, pairs (1,2), (3,4), ….

The logical net names were right; the copper was not. Pin 3 landed 5.08 mm
along the first row instead of in the second pair, so every net after pin 2 sat
on the wrong physical pad.

Fix: both connectors now use the exact parts already selected in
`docs/design-review.md`:

| Ref | Part | LCSC | Pads |
|---|---|---|---|
| `J_PI` | XFCN BH254V-40P | C48687632 | 2 × 20, 2.54 mm, 1.1 mm drill |
| `J_FRONT` | XFCN BH254V-26P | C48687629 | 2 × 13, 2.54 mm, 1.02 mm drill |
| `J5` (front board) | XFCN BH254V-26P | C48687629 | unchanged, same part |

The imported footprints carry their own body silkscreen and courtyard; the
pads are numbered conventionally. Measured from `dist/index/circuit.json`:
`J_PI` pin 1 at (24.13, 47.89), pin 2 at (24.13, 50.43), pin 3 at (21.59,
47.89) — alternating rows, 2.54 mm column pitch. `J_FRONT` and `J5` behave the
same way.

The Pico sockets stay on `pinrow20_p2.54mm`. That is correct: the Pico module
itself numbers its 40 pins around its perimeter, so each 1 × 20 socket row
needs consecutive numbering.

## 3. Q3 reset MOSFET pin numbering

The generic `<mosfet>` element binds pin 1 to drain and pin 3 to gate
(`mosfetPins` in `@tscircuit/props`). The selected part is a JCET 2N7002,
LCSC C8545, whose SOT-23 pins are **1 = GATE, 2 = SOURCE, 3 = DRAIN**
(primary: “SOT-23 Plastic-Encapsulate MOSFETS 2N7002”, J,Sep,2016, page 1 pin
list and page 4 package drawing, e1 = 1.800–2.000 mm).

`imports/C8545_2N7002.tsx` declares the part explicitly with
`pinLabels = { pin1: "gate", pin2: "source", pin3: "drain" }` and the existing
SOT-23 land pattern (1.325 × 0.600 mm pads, 1.900 mm lead pair, 2.275 mm rows).
Pad coordinates are unchanged, so the physical placement did not move; only
the net on each pad changed:

| Pad | Net (now) | Net (before) |
|---|---|---|
| 1 | `RESET_GATE` | `RUN` (drain) |
| 2 | `GND` | `GND` |
| 3 | `RUN` | `RESET_GATE` (gate) |

## 4. MIDI receiver: TLP2362 replaced by TLP2361

### Why

The input loop can deliver about 4.4 mA at its worst-case minimum. The
TLP2362 guarantees a threshold input current of 5.0 mA maximum and recommends
7.5–14 mA on-state; that loop is below both, so nothing was guaranteed.

### Replacement

| Item | Old | New |
|---|---|---|
| Part | Toshiba TLP2362(TPL,E | Toshiba **TLP2361(TPL,E** |
| LCSC | C37658 | **C107626** |
| Package | 11-4L1S (SO6) | 11-4L1S (SO6), same land pattern |
| Pins | 1 anode, 3 cathode, 4 GND, 5 VO, 6 VCC | identical |
| Output | open collector | inverting **totem pole** |
| Threshold IFHL max | 5.0 mA | **1.3 mA at Ta ≤ 105 °C, 1.6 mA at 125 °C** |
| Recommended IF(ON) | 7.5–14 mA | **2–6 mA** |
| Height | 2.2 mm (same package) | **2.2 mm max** (2.1 ± 0.1, package drawing) |

`imports/TLP2361_TPL_E.tsx` reuses the verified 11-4L1S pad geometry, carries
the new part number and C-number, and deliberately has **no CAD model**, so the
TLP2362 model cannot follow the new part.

### Worst-case loop current

Loop: sender supply → sender 2 × R → ring (DIN 4) → this design's 220 Ω (R1/R2)
→ LED → tip (DIN 5) → sender switch. Supply ±5 %, resistors ±1 %.

| Corner | Loop current | Against TLP2361 | Against TLP2362 |
|---|---|---|---|
| 5 V sender, minimum | **4.35 mA** | 3.35× the 1.3 mA threshold | **0.87× — not guaranteed** |
| 5 V sender, maximum | 5.97 mA | inside 2–6 mA recommended, 60 % of the 10 mA absolute max | — |
| 3.3 V CA-033 sender, minimum | 4.45 mA | 3.42× the threshold | — |
| 3.3 V CA-033 sender, maximum | **7.47 mA** | **above the 6 mA recommended maximum**, 75 % of the 10 mA absolute max | — |

Assumptions, stated as such: the minimum corner uses VF = 1.85 V because the
datasheet 1.65 V maximum is specified at 2 mA only and the loop runs near
4.4 mA; the maximum corner uses the 1.35 V minimum at 2 mA. Sender
saturation and cable drop are ignored in the minimum corner, which makes the
4.35 mA figure optimistic in the safe direction only if the real VF stays below
1.85 V. **That VF assumption and the 3.3 V high-current corner are unresolved
until measured.**

### Output level and loading

- `VO` is a totem pole. `VOH ≥ 3.2 V` at 20 µA sink, 3.3 V supply. The 2.2 kΩ
  pull-up (R3/R4) ties to the same 3.3 V rail, so it can only raise the high
  level, never pull it down.
- In the low state the pull-up sinks about 1.3 mA, inside the 3.2 mA sink
  condition at which `VOL ≤ 0.4 V` is guaranteed.
- The pull-up is therefore redundant but harmless, and it keeps the option of
  the old open-collector part.
- **Pending:** compare `VOH ≥ 3.2 V` with the RP2350 input threshold from the
  RP2350 datasheet. The design assumption is VIH ≤ 0.7 × IOVDD = 2.31 V; that
  value was not read from a primary source in this pass.

### Timing

80 ns maximum propagation delay (both directions), 25 ns maximum pulse width
distortion, measured by Toshiba at IF = 2 mA. At 31.25 kbaud (32 µs per bit)
this is not a constraint. **No measured timing is claimed.**

## 5. Local bypass at the two logic buffers

`C13` and `C14` are new 100 nF parts (C14663, 0603) at U3 and U4. Measured from
the built output, pad centre to pad centre:

| Cap | Buffer | Rail pad → VCC pad | Ground pad → GND pad | Position |
|---|---|---|---|---|
| C13 | U3 | 1.76 mm | 3.40 mm | (37.55, −10.45), 0° |
| C14 | U4 | 1.76 mm | 3.40 mm | (37.55, −5.45), 0° |

The rule applied by the checker: the supply pad within 3.0 mm of the buffer
VCC pad, the ground pad within 5.0 mm straight line of the buffer GND pad (the
return is the ground plane). A single 0603 cannot sit within 3 mm of *both*
pads of an SOT-23-5: those pads are 3.2 mm apart and the package body blocks the
direct path. The 3 mm target is met on the supply side; the return side is
3.40 mm and is documented here rather than hidden.

## 6. Board rules, pours and copper

Board props are one source, `lib/electrical.ts` `BOARD_POLICY`, applied to both
boards and visible in the emitted `pcb_board` element:

| Rule | Value |
|---|---|
| `min_trace_width` | 0.20 mm (nominal 0.25 mm) |
| `min_via_hole_diameter` | 0.20 mm |
| `min_via_pad_diameter` | 0.45 mm (0.125 mm annular ring) |
| `min_pad_edge_to_pad_edge_clearance` | 0.15 mm (JLCPCB 2-layer rule) |
| `min_trace_to_pad_edge_clearance` | 0.15 mm |
| `min_board_edge_clearance` | 0.20 mm |
| Layers | 2 |

Ground pours: rear board on the **top** layer (parts are on the bottom), front
board on the **bottom** layer (parts are on the top). No blind or buried vias.

### Recount, before → after this change

| Metric | Rear board | Front board |
|---|---|---|
| Trace width | 0.15 mm everywhere → 0.20/0.225/0.25 mm | 0.15 mm → 0.20/0.225/0.25 mm |
| Via drill / pad | 0.20 / 0.30 mm → **0.20 / 0.45 mm** | 0.20 / 0.30 mm → **0.20 / 0.45 mm** |
| Vias | 207 → **163** | 88 → **81** |
| Routed traces | 83 → 87 | 91 → 91 |
| Routed length | not measured before → **4058 mm** | not measured before → **2307 mm** |
| Unconnected pads (emitted warnings) | 26 → **3** | 12 → 12 |
| Routing / DRC errors | 0 → **0** | 0 → **0** |
| Ground pours | none → **1 (top, GND)** | none → **1 (bottom, GND)** |

The three remaining unconnected rear pads are `J_PICO_R` pins 15, 17 and 20
(unused Pico GPIO) and are allow-listed in the checker. The twelve on the front
board are the second contact pair of each B3F-4055 tact switch.

The rear board still needs 163 vias: about 49 SMT pads on a board whose other
layer is the ground plane need one layer change each, and the rest are signal
layer changes. No router preset was changed, so **no claim is made that this is
an optimum**; the count is reported so a later change can be compared.

### Routing-path cache warning (unresolved, upstream)

`tsci build` prints

```
Could not save autorouting paths for subcircuit_source_group_0:
Routes contain a junction or endpoint that cannot select a unique PCB port
```

once per board. Investigation: the warning comes from the route-cache writer,
not from the router. Routing itself finishes with `rounded_traces=87,
errors=0` and no `pcb_autorouting_error` element is emitted, and no route-cache
file is written. It is an upstream tscircuit limitation with junctions on
tracked nets; it was not patched and no generated output was hand-edited.
**Unresolved upstream warning.** Its only effect is that a rebuild re-routes
instead of reusing cached paths.

## 7. The independent checker

`scripts/check_electrical.py` reads only `dist/*/circuit.json`. It does not
import the TypeScript sources, so it is an independent statement about the
built output. It derives each pad's physical pin number and net from
`pcb_port → source_port → source_net`, then checks:

- conventional 2 × N numbering **by pad coordinate** for `J_PI`, `J_FRONT` and
  `J5`, plus the expected net on every pin (Pi header, Pico sockets,
  interconnect, TRS jacks);
- that `J_FRONT` and `J5` agree pin for pin (one straight ribbon);
- Q3 pad 1/2/3 = gate/source/drain, and that the three pads form a SOT-23 land
  pattern;
- TLP2361 on both MIDI inputs with the right pinout, and no BOM line or part
  number that still names the TLP2362;
- MIDI Type A polarity through the net graph: ring (DIN 4) → 220 Ω → opto
  anode, opto cathode → tip (DIN 5) for the inputs; ring sourced through 22 Ω
  from the PNP collector, tip sunk through 33 Ω to ground for the outputs;
- bypass presence, part number, and pad distances;
- ground pour layer and net, SMT side, 2 layers, board rules, trace widths, via
  drill and pad, minimum pad gap, and zero emitted errors;
- the analytical loop-current margins of section 4, including that the replaced
  TLP2362 would fail.

`scripts/test_electrical.py` proves the checker can fail. Each case copies
`dist/`, breaks one thing, and requires a rejection:

| Mutation | Rejected by |
|---|---|
| `J_PI` pads re-numbered around the perimeter, net names untouched | the coordinate numbering check, while the pad-to-net table still passes — a name-only check cannot see this |
| Q3 pad 1 = drain, pad 3 = gate | the Q3 check |
| `C13` removed | the bypass check |
| MIDI IN ring/tip swapped | the Type A polarity check |
| a trace set to 0.15 mm | the trace-width check |
| a via pad set to 0.30 mm | the via-pad check |
| the ground pour removed | the pour check |
| TLP2362 put back on the BOM | the receiver check |

## 8. SPICE: what it now does and does not show

`spice/midi_in_common.inc` replaces the old always-on behavioural receiver.
The model now has a **parameterised threshold current** and a diode fitted to
the datasheet forward voltage (1.48 V at 2 mA), and it is inverting with a
voltage output, matching the TLP2361.

| Deck | Sender | Threshold | Measured loop current | Output |
|---|---|---|---|---|
| `midi_in.cir` | 5.00 V | 1.3 mA | 4.906 mA | low |
| `midi_in_weak.cir` | 4.75 V, high VF | 1.3 mA | 4.248 mA | low |
| `midi_in_2362.cir` | 4.75 V, high VF | 5.0 mA | 4.248 mA | **stays high** |

The third deck is the point: the same current that switches a TLP2361 fails to
switch a TLP2362. `spice/run_tests.py` now writes its ngspice logs into a
private `tempfile.mkdtemp()` directory (parallel checkouts cannot collide) and
prints the command, stdout, stderr and log on failure. The power, reset and
SPI-enable decks are unchanged.

This remains a resistor-network and behavioural model. It does not model
Toshiba silicon, propagation delay, CTR, supply current, common-mode
transient immunity, isolation or temperature. **It is not a hardware
qualification.**

## 9. Schematics — seen, and what still reads badly

Both boards now declare a `<schematicsheet>` and `<schematicsection>` blocks
(connectors, power, isolation on the rear; MIDI in, MIDI out, controls, power,
interconnect on the front), and every component carries `schSheetName` and
`schSectionName`. Render them with

```sh
cd midibrain/tscircuit
./node_modules/.bin/tsci snapshot front.circuit.tsx --schematic-only --update
./node_modules/.bin/tsci snapshot index.circuit.tsx --schematic-only --update
```

(they are not committed; they regenerate from the sources). In the rendered
SVG the section titles appear as text above their blocks and the clusters are recognisable: the rear board shows
the Pi header, interconnect and Pico sockets with their pin numbers, U3/U4 with
R19/R20, Q3 with R16–R18, and the power parts. Connectivity is unchanged.

Known residual readability problems, seen in the rendered SVG and **not**
fixed:

- On the front board the automatic layout puts `R1`/`R2` on top of `J1`/`J2`,
  so the refdes and “220Ω” text overlap the jack net labels.
- `U1`/`U2` show the opto symbol but no visible value or part-number text; the
  opto pin numbers 1/3/4/5/6 are not drawn on the symbol.
- The front sheet needs `ANSI_B` because an A4 sheet left 25 components outside
  the drawing area; the builder then warns that a non-default sheet size is in
  use. That warning is accepted, not hidden.
- Section boxes are not drawn, only titles; parts of one section can still sit
  far from each other (the rear power parts are split across the sheet).

## 10. Primary sources

| Item | Source | SHA-256 of the copy used |
|---|---|---|
| JCET 2N7002 SOT-23 | `/tmp/review-c8545.pdf` (J,Sep,2016) | `7941fb423af7c6c6c8979063a7e8819bb19217ece275efcce18948950a41d9f6` |
| Toshiba TLP2361 | https://datasheet.lcsc.com/datasheet/pdf/ecf1cd8277534c97a26aa40e285f7e10.pdf?productCode=C107626 , `/tmp/midi-recv-research/TLP2361_lcsc.pdf` (Rev.5.0, 2015-11-02) | `97b3fc235c385f0b928aca2f52293b4a06ea41edaaad2fb4abe20532a2d84236` |
| Toshiba TLP2362 (replaced) | `/tmp/review-c37658.pdf` (Rev.6.0, 2016-01-19) | `0686005de511e3987d3eac309f840a44d3475358eaf5567d66a646ef63d2c6e6` |
| TI SN74LVC1G126 (unchanged part) | `/tmp/review-sn74lvc1g126.pdf` | `c73611ec7322b956154ca042b1b96c362d2c3a9e79aeae1e545467071289cadf` |
| MIDI CA-033 electrical update | `/tmp/CA-033-MIDI-1-0-Electrical-Specification-Update-2014-English.pdf` | — |
| MIDI TRS adapters (Type A) | `/tmp/US-MIDI-2-0-Adapters-the-Responsibility-of-February-2024.pdf` | — |

## 11. Unresolved items — do not treat this board as release-ready

1. **TLP2361 high-current corner.** A 3.3 V CA-033 sender with low VF can push
   7.47 mA, above the 6 mA recommended on-state current (still below the 10 mA
   absolute maximum). High-current and high-temperature behaviour is not
   qualified; measure on first article.
2. **VF assumption.** The 1.85 V bound used for the minimum-current corner is an
   assumption above the 2 mA datasheet limit. Measure VF at 4.4 mA over
   temperature.
3. **RP2350 input threshold** not read from a primary source in this pass; the
   `VOH ≥ 3.2 V` comparison uses the 0.7 × IOVDD design assumption only.
4. **Stock** for C107626 was a snapshot (33005 pieces at research time);
   recheck at order.
5. **Autoroute cache warning** is upstream and unresolved (section 6).
6. **Schematic readability** items in section 9.
7. **Not re-verified here:** SN74LVC1G126 pinout and maximum height, MMBT3906
   pinout, B5819W polarity (the checker verifies the net pair only), TRS jack
   contact continuity, and every mechanical question in
   `docs/design-review.md`. The 2.2 mm TLP2361 height fits the existing SO-6
   land area; no 3D clearance check was run.
8. No ERC, DRC sign-off, Gerber review, first article or hardware measurement
   was performed. The 5 V sender source, the resistor tolerances and the
   cable drop come from the MIDI specification, not from a measurement of the
   actual instruments that will be plugged in.

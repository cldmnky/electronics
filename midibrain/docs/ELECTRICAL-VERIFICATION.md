# Electrical verification — what was checked, what was not

Status: **prototype definition.** Nothing here is a fabrication release, a
hardware measurement or a signal-integrity claim. Every number comes from a
datasheet limit, a stated tolerance, or the built circuit JSON in `dist/`.

Boards: rear/base `index.circuit.tsx` (101.6 × 112 mm, 2 layers, parts on the
bottom) and front/UI `front.circuit.tsx` (128 × 112 mm, 2 layers, parts on the
top).

## 1. How to reproduce

```sh
cd midibrain/tscircuit
bun install --frozen-lockfile
bun run build                      # writes dist/index, dist/front, dist/panel
python3 scripts/check_electrical.py # independent electrical checks (238)
python3 scripts/test_electrical.py  # negative mutations and model cases (35)
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
the historical design-review record (removed in the 2026-11-06 consolidation; git history):

| Ref | Part | LCSC | Pads |
|---|---|---|---|
| `J_PI` | XFCN BH254V-40P | C48687632 | 2 × 20, 2.54 mm, 1.1 mm drill |
| `J_FRONT` | XFCN BH254V-26P | C48687629 | 2 × 13, 2.54 mm, 1.02 mm drill |
| `J5` (front board) | XFCN BH254V-26P | C48687629 | unchanged, same part |

The imports are `<connector>` elements, not generic chips, and both footprints
declare `insertionDirection="from_above"` in their unrotated local orientation.
That is the real part semantics: a vertical through-hole box header. It removes
the `pcb_connector_not_in_accessible_orientation_warning` that an undeclared
direction produced, and it removes the three `pin_specification` warnings a
generic chip emitted per connector. The imported footprints carry their own
body silkscreen and courtyard; the pads are numbered conventionally. Measured from `dist/index/circuit.json`:
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

`imports/C8545_2N7002.tsx` declares the part explicitly as a `<transistor
type="mosfet">` with `pinLabels = { pin1: "gate", pin2: "source", pin3:
"drain" }` — the element type and the terminal names cannot be overridden by
the caller — on the existing SOT-23 land pattern (1.325 × 0.600 mm pads, 1.900 mm lead pair, 2.275 mm rows).
Pad coordinates are unchanged, so the physical placement did not move; only
the net on each pad changed:

| Pad | Net (now) | Net (before) |
|---|---|---|
| 1 | `RESET_GATE` | `RUN` (drain) |
| 2 | `GND` | `GND` |
| 3 | `RUN` | `RESET_GATE` (gate) |

## 4. MIDI receiver: TLP2362 replaced by TLP2361

### Why

The input loop can deliver 3.435 mA at its worst-case minimum (section 4). The
TLP2362 guarantees a threshold input current of 5.0 mA maximum and recommends
7.5–14 mA on-state; the loop is below both, so nothing was guaranteed.

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
| Absolute maximum IF | 25 mA | **10 mA at Ta = 25 °C**, derating −0.13 mA/°C for Ta ≥ 110 °C → **8.05 mA at 125 °C** |
| Height | 2.2 mm (same package) | **2.2 mm max** (2.1 ± 0.1, package drawing) |

`imports/TLP2361_TPL_E.tsx` reuses the verified 11-4L1S pad geometry, carries
the new part number and C-number, and deliberately has **no CAD model**, so the
TLP2362 model cannot follow the new part.

### Worst-case loop current

Loop: sender supply → sender RA → ring (DIN 4) → this design's 220 Ω (R1/R2)
→ LED → tip (DIN 5) → sender RC → sender driver → ground.

CA-033 (2014) page 2, figure 1, is the reference for both sender cases:
**+5 V ±10 % with RA 220 Ω 5 % and RC 220 Ω 5 %**, and **+3.3 V ±5 % with
RA 33 Ω 5 % and RC 10 Ω 5 %**. RA and RC are not equal in the 3.3 V case. The
receiver resistor is this design's 1 % part. CA-033 does not specify the sender
driver saturation. The minimum-current corner assumes 0.3 V of sender loss;
the maximum-current corner assumes 0 V because no minimum loss is guaranteed
(a low-loss sender can drop almost nothing). Both are stated assumptions
about the external device; the model includes both.

| Corner | Loop current | Against the 125 °C TLP2361 threshold (1.6 mA) | Against TLP2362 |
|---|---|---|---|
| 5 V sender, minimum | **3.435 mA** | 2.15× | **0.69× — not guaranteed** |
| 5 V sender, maximum | **6.527 mA** | above the 6 mA recommended maximum; 65 % of the 25 °C absolute max (10 mA) and 81 % of the 125 °C derated limit (8.05 mA) | — |
| 3.3 V sender, minimum | **3.684 mA** | 2.30× | 0.74× |
| 3.3 V sender, maximum | **8.177 mA** | above the 6 mA recommended maximum; 82 % of the 25 °C absolute max (10 mA) and 1.6 % above the 8.05 mA 125 °C derated limit **in this model** (illustrative only, see below) | — |

The datasheet's absolute maximum is a **25 °C** rating (10 mA) with a stated
derating of −0.13 mA/°C above 110 °C; it is not a full-temperature limit. The
checker carries the derating constant and fails if the 125 °C value it computes
is not 8.05 mA.

**The 8.177 mA comparison is illustrative, not a measurement or a rating.** That
bound comes from the model: CA-033 sender corners, the datasheet VF minimum at
2 mA, and an assumed 0 V sender driver drop. It is therefore **not** a measured
input current, and **no safe maximum ambient temperature is derived from it**.
Physical current and temperature at the high-rail/low-VF corners stay acceptance
item
`midi_receiver_high_current_thermal`, which must record the actual current and
ambient; the 6 mA recommended on-state current is exceeded by both model
corners and remains a recorded deviation.

The minimum corner uses VF = 1.85 V because the datasheet 1.65 V maximum is
specified at 2 mA only and the loop runs near 3.7 mA; the maximum corner uses
the 1.35 V minimum at 2 mA. **The VF assumption and both high-current corners
are unresolved until measured.**

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

### Legacy receiver compatibility: the AO3401A stage now meets it

CA-033 page 4 models a legacy receiver as an LED of up to 1.9 V in series with
220 Ω, and gives the PC900V a worst-case forward current of 4 mA at
RD = 280 Ω, VRX = 5 V. The transmitter must satisfy
VTX ≥ 0.005 × 220 + 1.9 = 3.0 V and (RA + RC) = (VTX − 3.0) / 0.005, which is
27 Ω at 3.135 V.

The output stage is now a **high-side P-channel switch**:

| Item | Part | LCSC |
|---|---|---|
| Switch | Alpha & Omega **AO3401A**, SOT-23, pin 1 gate, 2 source, 3 drain | **C15127** |
| Gate series | 100 Ω 0603 1 % | C22775 |
| Gate pull-up (gate to 3.3 V) | 2.2 kΩ 0603 1 % | C4190 |
| Ring resistor RA | **33 Ω** 2512 1 W 1 % | C53728 |
| Tip resistor RC | **22 Ω** 2512 1 W 1 % | C118189 |

The receiver resistor stays at 220 Ω and TRS Type A polarity is unchanged:
ring (DIN 4) is sourced from the switch drain through RA, tip (DIN 5) is sunk
to ground through RC.

| Case | Current into a legacy receiver |
|---|---|
| Worst case: 3.135 V rail, 1.9 V LED, 1 % resistors at +tolerance, RDS(ON) 85 mΩ | **4.309 mA** |
| Same with a 150 mΩ hot RDS(ON) assumption | 4.308 mA |
| Replaced PNP stage (22 Ω ring, 33 Ω tip, 0.25 V VCE(sat)) | 3.437 mA |

The new stage therefore **meets the 4 mA PC900V worst case** at 25 °C, at
107 % of the requirement, and the conclusion is insensitive to RDS(ON). The
old PNP stage did not: the checker keeps that calculation as a negative
reference. The 4 mA figure is the receiver's worst case, not a measurement of
any real device.

### Gate drive and off state (25 °C calculations)

| Check | Value |
|---|---|
| Gate low (UART pin low at VOL 0.5 V, pull-up 2178 Ω, series 101 Ω) | \|VGS\| = **2.518 V** |
| RDS(ON) test point in the datasheet | \|VGS\| = 2.5 V, so 85 mΩ applies |
| Gate rating | ±12 V, so 2.518 V is far inside |
| Gate with the UART pin high-Z (3.465 V rail, pull-up 2222 Ω, series 101 Ω, RP2350 pull-down 36 kΩ) | \|VGS\| = **0.210 V** |
| Gate threshold magnitude, minimum | 0.5 V at 25 °C |

The off state is below the threshold **at 25 °C only**. The datasheet gives no
guaranteed threshold over −40..125 °C, and the gate charge figure (Qg 7 nC at
4.5 V) is a typical value, so **no full-temperature off guarantee and no
switching-edge claim is made**. Both stay pending bench work.

### Fault dissipation and the deliberate CA-033 deviation

At the strongest rail (3.465 V) and with the 1 % resistors at −1 %:

| Fault | Dissipation |
|---|---|
| Ring to ground, RA only (32.67 Ω) | **0.366 W** |
| Tip connected to the positive rail, RC only (21.78 Ω) | **0.547 W** |
| Ring-to-tip short, both in series at low resistance (54.45 Ω) | 0.132 W (RA) + 0.088 W (RC) |

Both parts are 2512 1 W resistors, so the worst single-part fault uses 55 % of
the rating at 25 °C. The Uniroyal datasheet (Feb 12 2019 V.3, section 9,
figure 1) rates 2512 1 W continuous only to 70 °C ambient with derating above
it, so **sustained-fault behaviour above 70 °C stays pending bench work** and
the numbers stay provisional until then.

CA-033 asks for RA 33 Ω and **RC 10 Ω** at 3.3 V. This design uses 33 Ω and
**22 Ω**. That is a deliberate deviation: RC is the tip's return to ground, so
the fault that heats it is the **tip connected to the positive rail** (a
wrong-wired plug or a short to a rail), not the tip connected to ground (which
carries no current here). A 10 Ω part would carry 3.465² / 9.5 = **1.264 W**
in that fault, far above the 0.25 W that CA-033 assumes for RC. The 22 Ω 1 W
part is the smallest standard value that keeps the fault inside one 1 W 2512
part. The checker asserts both halves of this argument and names the faults by
the electrical connection: `ring to ground`, `tip to positive rail`,
`ring tip ring`, `ring tip tip`.

### Receiver output against the RP2350 input

Primary source: RP2350 datasheet table 1683, pdf page 1337 (printed page
1336), IOVDD = 3.3 V: **VIH minimum 2.0 V, VIL maximum 0.8 V, input leakage
1 µA maximum**.

| Margin | Value |
|---|---|
| TLP2361 VOH 3.2 V (at 20 µA) vs VIH 2.0 V | **1.2 V** |
| TLP2361 VOL 0.4 V (at 3.2 mA sink) vs VIL 0.8 V | **0.4 V** |
| 1 µA leakage through the 2.2 kΩ pull-up | 2.2 mV |

The pull-up ties to the same 3.3 V rail, so it can only raise the high level.
RP2350 errata E9A2 (an undefined-level input leakage case) was raised by the
reviewer; the totem-pole drive and the 2.2 kΩ pull-up dominate any such
leakage. **The errata document itself was not read in this pass.**

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

### Recount: before the electrical work → current build

The right-hand column is measured on the current build. The autorouter re-runs
on every build, so via and trace counts are **build-dependent**; the hole and
pad sets are not.

| Metric | Rear board | Front board |
|---|---|---|
| Trace width | 0.15 mm everywhere → 0.20/0.225/0.25 mm | 0.15 mm → 0.20/0.225/0.25 mm |
| Via drill / pad | 0.20 / 0.30 mm → **0.20 / 0.45 mm** | 0.20 / 0.30 mm → **0.20 / 0.45 mm** |
| Vias (build-dependent) | 207 → **316** | 88 → **68** |
| Routed traces (build-dependent) | 83 → **102** | 91 → **93** |
| Unconnected pads (declared do-not-connect) | 26, all deliberate unused header pins | 14, unused switch contacts |
| Routing / DRC errors | 0 → **0** | 0 → **0** |
| Ground pours | none → **1 (top, GND)** | none → **1 (bottom, GND)** |

The 26 rear pads are the 23 Pi-header pins this design does not use plus
`J_PICO_R` pins 15, 17 and 20 (unused Pico GPIO). The 14 front pads are the
second contact pair of the seven B3F-4055 tact switches (six transport buttons
plus the panel POWER momentary). The checker no longer keeps a flat allowlist:
it derives the open set from the connectivity tables, so a pin that *should* be
connected and is not fails the check. The earlier "26 → 3" figure came from the
`<chip>` element suppressing the warnings and was not a connectivity
improvement.

The rear board still needs 316 vias on this build: about 47 SMT pads on a board
whose other layer is the ground plane need one layer change each, the rest are
signal layer changes, and the 15 probe points add a fan-out each. The
autorouter is re-run per build, so the count is **build-dependent**. No router
preset was changed, so **no claim is made that this is an optimum**; the count
is reported so a later change can be compared.

### Probe points (testability)

Fifteen bare plated through-holes on the rear/base board, one source
`tscircuit/lib/probe-points.ts`. They add **no part**: each pad connects to a net
that already exists, the source declares `doNotPlace`, no supplier or
manufacturer part number is invented, and the pads therefore have no BOM line
and no CPL line. They bridge no rail.

Geometry, measured on the built JSON: **0.80 mm drill, 1.60 mm pad, 0.40 mm
annular ring** on both copper layers, so a probe reaches the same net from
either face. The ring target is 0.25 mm, not the published 0.18 mm floor. The
installed `<testpoint footprintVariant="through_hole">` was **not** used: it
derives the pad from the drill alone (`platedhole_d<hole>`, pad = 1.5 × drill),
ignores `padDiameter`, and produced a 0.20 mm ring on the first attempt. The
pad is authored with the footprinter string `platedhole_d0.8mm_pd1.6mm`, and
`check_electrical.py` fails if either number or the ring moves.

| Ref | Net | Position (mm) | Label | What the pad is on |
|---|---|---|---|---|
| TP1 | `P5V` | (−40, 52.5) | `5V` | Pi header pins 2/4 and D3 anode: the Pi 5 V rail **before** D3, not Pico `VSYS` |
| TP2 | `P3V3` | (−35, 52.5) | `3V3` | Pico module pin 36 (3V3 OUT) and the board 3.3 V rail: the Pico's SMPS output |
| TP3 | `PI_3V3` | (−40, 49.5) | `PI3V3` | Pi header pin 1 only, then R19 (100 Ω) to net `OE`; **not** the Pico 3V3 rail |
| TP4 | `RUN` | (−35, 49.5) | `RUN` | Pico module pin 30, Q3 drain and R18 (10 k to `P3V3`) |
| TP5 | `SPI_SCLK_PICO` | (−40, 46.5) | `SCLK` | Pico pin 24 (GPIO18), Pico side of R14 (100 Ω) |
| TP6 | `SPI_CS_PICO` | (−35, 46.5) | `CS` | Pico pin 22 (GPIO17), Pico side of R15 (100 Ω) |
| TP7 | `SPI_MOSI_PICO` | (−40, 43.5) | `MOSI` | Pico pin 21 (GPIO16), Pico side of R13 (100 Ω) |
| TP8 | `SPI_MISO_PICO` | (−35, 43.5) | `MISO` | Pico pin 25 (GPIO19) and U3 input A (buffer input, Pico side) |
| TP9 | `READY_PICO` | (−40, 40.5) | `READY` | Pico pin 26 (GPIO20) and U4 input A (buffer input, Pico side) |
| TP10 | `GND` | (−35, 40.5) | `GND` | local return beside the logic field |
| TP11 | `MIDI_OUT_1_TX` | (−33, −30) | `TX1` | Pico pin 1 (UART0 TX); on the front board it reaches R5 (100 Ω) pin 1, Q1 gate is R5 pin 2 |
| TP12 | `MIDI_IN_1_RX` | (−30, −30) | `RX1` | Pico pin 2 (UART0 RX); on the front board U1 pin 5 (TLP2361 VO) drives it, R3 (2.2 k) pull-up |
| TP13 | `MIDI_OUT_2_TX` | (−27, −30) | `TX2` | Pico pin 6 (UART1 TX); on the front board it reaches R6 (100 Ω) pin 1, Q2 gate is R6 pin 2 |
| TP14 | `MIDI_IN_2_RX` | (−33, −33) | `RX2` | Pico pin 7 (UART1 RX); on the front board U2 pin 5 (TLP2361 VO) drives it, R4 (2.2 k) pull-up |
| TP15 | `GND` | (−30, −33) | `GND` | local return beside the MIDI points (TP11–TP14) |

MIDI channel 1 and channel 2 are separate nets on separate pads with separate
front-board parts: TX1 reaches R5 (Q1 gate is R5 pin 2), TX2 reaches R6 (Q2 gate
is R6 pin 2); RX1 comes from U1 pin 5 with the R3 pull-up, RX2 from U2 pin 5 with
the R4 pull-up. The two local grounds (TP10 in the logic field, TP15 in the MIDI
group) are the same `GND` net.

`PI_3V3` and `P3V3` are separate nets on separate pads. The only parts between
them are R19 (Pi 3V3 → 100 Ω → `OE`) and R20 (100 kΩ, `OE` → GND); U3 and U4 are
powered from `P3V3` and their `OE` pins are driven from that divider. No
component has ports on both nets, and the checker fails if one appears. **The
Pi and Pico regulators are never coupled**; the boards already relied on that
before the probe points existed.

Placement: the logic field fills the empty top-left block (two columns 5 mm
apart, 5 rows 3 mm apart) and the MIDI points sit in the open pocket right of the
Pico's micro-USB plug in two rows (three points at 3 mm pitch, two below). No
two probe pads are closer than **3.0 mm**, every pad keeps **2.0 mm** to the
board edge, the pads are outside the Pico module (51 × 21 mm, Pico 2 datasheet),
the Pi PCB (85 × 56 mm on the 58 × 49 mm hole grid), the J_PI and J_FRONT bodies
with their ribbons and the spacer holes, and the nearest other component pad or
hole is 2.85 mm away. The 2.0 mm edge target is one source constant,
`PROBE_EDGE_MM` in `lib/probe-points.ts`; `lib/probe-points.test.ts` checks the
envelopes, the edge target and the labels, and `check_electrical.py` re-checks
the same geometry independently on the built coordinates.

**Physical access is not verified.** The envelopes above are planning boxes, not
measured cable or part clearances. Nothing here claims that a probe tip, a clip
or a finger can reach a pad inside the assembled stack, and **in-rack access is
not claimed at all**: that is acceptance item `probe_point_access`, which must
record the actual assembly state used (front board off, Pi removed, Pico
socketed, USB cable fitted) for each pad.

### Pico 2 SWD: documented, not implemented

The Pico 2 brings SWD to **three pads on the module's lower edge**, which are
separate from the 40-pin socket pins. Primary source: *Raspberry Pi Pico 2
Datasheet* (document revision 2026-07-03), Figure 2 (pinout, page 5),
retrieved 2026-11-08 from
<https://datasheets.raspberrypi.com/pico/pico-2-datasheet.pdf>, SHA-256
`cbcfaf881018c686495a2e53c7bd281e7e0a8217d79447d73eda7c8f74b7f675`:
the pads read
**SWCLK, GND, SWDIO** in that order when viewed from the module's component
side with the micro-USB at the top. The drawn pad spacing matches the 2.54 mm
pin pitch of Figure 2 (scaled from the drawing; the text does not state it).
Figure 4
(physical pin numbering, page 9) shows the three pads between the two socket
rows at the board end opposite the USB but does **not** name the signals and
does not dimension the pad centres. The module is socketed with its USB end at
−Y on this board (module pin 1, `J_PICO_L` pad 1, is at y = −24.13), so those
pads face the +Y end. The exact pad centres were not measured.

**No SWD anchor pads were added.** A safe, mechanically viable hand-wired link
to those pads is unproven: no wire gauge, insulation, strain relief, connector,
probe ground reference or procedure has been selected, and the debug pads carry
no connection to the 40-pin sockets. Adding isolated copper would not create
access and would invite a claim the design cannot support. Access stays a
documented gap (`docs/TODO.md`), not a feature.

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
  from the AO3401A drain via 33 Ω, tip sunk through 22 Ω to ground for the
  outputs, gate driven through 100 Ω with a 2.2 kΩ pull-up to 3.3 V;
- bypass presence, part number, and pad distances;
- ground pour layer and net, SMT side, 2 layers, board rules, trace widths, via
  drill and pad, minimum pad gap, and zero emitted errors;
- the 15 probe points on the rear board: exact ref set, one circular plated
  through-hole each on both layers, authored 0.80 mm drill / 1.60 mm pad,
  ring ≥ 0.25 mm, exact net, exact position, `doNotPlace` only, no supplier or
  manufacturer part number, a routed trace at the pad with other pads on the
  same net, pitch ≥ 3.0 mm, 2.0 mm to the board edge, 2 mm to the nearest other
  pad or hole, the Pico/Pi/ribbon envelopes, and that no component sits on both
  `PI_3V3` and `P3V3`;
- the analytical loop current of section 4 against hand-calculated reference
  values (3.435/6.527 mA and 3.684/8.177 mA), the 125 °C threshold and the
  datasheet's absolute-maximum derating (8.05 mA at 125 °C), the output stage
  (gate drive, off state, legacy current, the ring-to-ground / tip-to-positive-
  rail / ring-tip faults and the CA-033 deviation) and the RP2350 input margins.

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
| supply tolerance loosened to 5 % | the reference-case comparison |
| sender resistor tolerance loosened to 1 % | the reference-case comparison |
| receiver resistor tolerance removed | the reference-case comparison |
| the 105 °C threshold used instead of 125 °C | the threshold check |
| the sender driver-drop bound dropped | the reference-case comparison |
| a probe pad moved onto the other 3.3 V rail | the exact-net check and the rail-separation check |
| a probe pad ring reduced to 0.24 mm | the annular-ring check |
| a probe drill changed to 0.50 mm | the authored drill/pad check |
| a probe pad moved inside the Pico envelope | the envelope check |
| a probe point renamed away | the exact-set check |
| two probe pads on one spot | the pad-pitch check |
| a placeholder probe part number added | the bare-copper check |
| a probe pad set to one layer | the plated-through-hole check |
| `doNotPlace` removed from a probe pad | the do-not-place set check |
| the trace at a MIDI RX2 pad deleted | the routed, already-existing-net check |
| a MIDI TX2 pad moved onto the channel-1 TX net | the exact-net check |
| a MIDI RX2 pad moved inside the Pi envelope | the envelope check |
| R19 moved so a part sits on both 3.3 V rails | the rail-coupling check |

## 8. SPICE: what it now does and does not show

`spice/midi_in_common.inc` replaces the old always-on behavioural receiver.
The model now has a **parameterised threshold current** and a diode fitted to
the datasheet forward voltage (1.48 V at 2 mA), and it is inverting with a
voltage output, matching the TLP2361.

| Deck | Sender | Threshold | Measured loop current | Output |
|---|---|---|---|---|
| `midi_in.cir` | 5.00 V, RA/RC 220 Ω | 1.3 mA | 4.906 mA | low |
| `midi_in_weak.cir` | 4.75 V, RA/RC 220 Ω, high VF | 1.3 mA | 4.248 mA | low |
| `midi_in_2362.cir` | 4.75 V, RA/RC 220 Ω, high VF | 5.0 mA | 4.248 mA | **stays high** |

`midi_out.cir` is now a P-channel smoke model: level-1 MOSFET fitted to the
datasheet DC numbers, 100 Ω / 2.2 kΩ gate network, 33 Ω ring, 22 Ω tip. It
measures 0 mA loop current while idle, 5.087 mA while active and 3.131 V on
the ring. It is **not** the AOS model and it does not model gate charge,
switching edges or temperature.

The MIDI IN decks model the nominal 5 V CA-033 sender; the analytical corners in
section 4 also cover ±10 % supply, 5 % resistors, the 3.3 V sender and the
driver drop, which the SPICE decks do not.

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

- `U1`/`U2` now print `TLP2361` as a literal label under the symbol, so the
  fitted part is visible on the sheet. The opto pin numbers 1/3/4/5/6 are still
  not drawn on the symbol.
- `R1` sits at a manual schematic position (schematic only, no PCB part moved)
  and no longer overlaps `J1`'s pin labels; this is verified in the rendered
  sheet.
- Still overlapping in the rendered sheet: one net label box in the controls
  block sits over the `U5`/`U6` pin area. It is **not** fixed.
- The front sheet needs `ANSI_B` because an A4 sheet left 25 components outside
  the drawing area; the builder then warns that a non-default sheet size is in
  use. That warning is accepted, not hidden.
- Section boxes are not drawn, only titles and dashed boundaries; parts of one
  section can still sit far from each other (the rear power parts are split
  across the sheet).
- The sheet still needs `ANSI_B` for the front board, which the builder warns
  about. A4 left 25 components outside the drawing area.
- `R1`'s manual position reorders the MIDI IN block (J2 now sits at the bottom
  of the section). Connectivity is unchanged and the checker confirms every
  pad and net.

## 10. Primary sources

| Item | Source | SHA-256 of the copy used |
|---|---|---|
| JCET 2N7002 SOT-23 | `/tmp/review-c8545.pdf` (J,Sep,2016) | `7941fb423af7c6c6c8979063a7e8819bb19217ece275efcce18948950a41d9f6` |
| Toshiba TLP2361 | https://datasheet.lcsc.com/datasheet/pdf/ecf1cd8277534c97a26aa40e285f7e10.pdf?productCode=C107626 , `/tmp/midi-recv-research/TLP2361_lcsc.pdf` (Rev.5.0, 2015-11-02) | `97b3fc235c385f0b928aca2f52293b4a06ea41edaaad2fb4abe20532a2d84236` |
| AOS AO3401A | `/tmp/midibrain-AO3401A.pdf` (Rev 3.1, December 2023) | — |
| Toshiba TLP2362 (replaced) | `/tmp/review-c37658.pdf` (Rev.6.0, 2016-01-19) | `0686005de511e3987d3eac309f840a44d3475358eaf5567d66a646ef63d2c6e6` |
| TI SN74LVC1G126 (unchanged part) | `/tmp/review-sn74lvc1g126.pdf` | `c73611ec7322b956154ca042b1b96c362d2c3a9e79aeae1e545467071289cadf` |
| Raspberry Pi Pico 2 datasheet (SWD debug pads, Figure 2) | https://datasheets.raspberrypi.com/pico/pico-2-datasheet.pdf (revision 2026-07-03, retrieved 2026-11-08) | `cbcfaf881018c686495a2e53c7bd281e7e0a8217d79447d73eda7c8f74b7f675` |
| MIDI CA-033 electrical update | `/tmp/CA-033-MIDI-1-0-Electrical-Specification-Update-2014-English.pdf` | — |
| MIDI TRS adapters (Type A) | `/tmp/US-MIDI-2-0-Adapters-the-Responsibility-of-February-2024.pdf` | — |

## 11. Unresolved items — do not treat this board as release-ready

1. **TLP2361 high-current corner.** Both sender corners exceed the 6 mA
   recommended on-state current at their maximum (6.53 mA and 8.18 mA). Both are
   below the absolute maximum **at 25 °C (10 mA)**; the absolute maximum is
   derated −0.13 mA/°C above 110 °C, so the 3.3 V model corner (8.18 mA) is
   above the 8.05 mA limit at 125 °C. **The comparison is illustrative only**
   (model currents from CA-033 corners, the 2 mA VF minimum and an assumed 0 V
   sender drop) and **no safe ambient is derived**. Physical input current and
   temperature are not qualified; measure both, with the ambient, on first
   article.
1a. **Output stage pending items.** The AO3401A stage meets the 4 mA legacy
   worst case on paper (4.309 mA at 25 °C). The full-temperature off state, the
   switching edges and the 1 W 2512 derating above 70 °C are **not** qualified,
   and the parent adds a pending acceptance item `midi_out_legacy`.
2. **VF assumption.** The 1.85 V bound used for the minimum-current corner is an
   assumption above the 2 mA datasheet limit. Measure VF at about 3.7 mA over
   temperature.
3. **RP2350 input thresholds** now come from table 1683 (VIH 2.0 V, VIL 0.8 V,
   leakage 1 µA at IOVDD 3.3 V). RP2350 errata E9A2 was not read directly.
4. **Stock** for C107626 was a snapshot (33005 pieces at research time);
   recheck at order.
5. **Autoroute cache warning** is upstream and unresolved (section 6).
6. **Schematic readability** items in section 9.
7. **Not re-verified here:** SN74LVC1G126 pinout and maximum height, MMBT3906
   pinout, B5819W polarity (the checker verifies the net pair only), TRS jack
   contact continuity, and every mechanical question in
   the historical design-review record. The 2.2 mm TLP2361 height fits the existing SO-6
   land area; no 3D clearance check was run.
8. No ERC, DRC sign-off, Gerber review, first article or hardware measurement
   was performed. The 5 V sender source, the resistor tolerances and the
   cable drop come from the MIDI specification, not from a measurement of the
   actual instruments that will be plugged in.

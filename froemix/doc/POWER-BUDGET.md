# U16 5 V rail — power budget and acceptance test

**Status: budget closes with margin on the 2 W module; nothing here is measured
yet.** This file records the 5 V budget required by DESIGN-REVIEW R13, the input
capacitor that R13 asked for (C32), and the test that has to be run during
bring-up. Do not characterize P5V as a regulated precision 5 V rail.

## 1. The converter and its ratings

| Item | Value | Source |
| --- | --- | --- |
| U16 part | B2405S-**2**WR3 (JLCPCB C5369484), YLPTEC, 4-lead SIP on the B_S-2WR3 pin-out, body 19.65 × 7.05 × 10.16 mm | JLC part record |
| Leads | **1 = +Vin, 2 = −Vin, 4 = 0V, 6 = +Vo**; positions 1–2–4–6 on a 2.54 mm grid, leads 3 and 5 do not exist | datasheet / EasyEDA footprint for C5369484 |
| Input range | 21.6 … 26.4 VDC (24 V nominal), i.e. VCC − VEE ≈ 23.4 V | datasheet |
| Output | 5 V, **2 W / 400 mA maximum**, **unregulated** | datasheet |
| Minimum load | **40 mA** | datasheet (YLPTEC/MORNSUN B_S-2WR3 series) |
| Isolation | 1.5 kVDC | datasheet |
| Board footprint | `jclpcb:PWRM-TH-4P-L19.65-W7.05-P2.54-B2405S-2W` — 4 plated holes, 1.0 mm drill / 1.8 mm pad, at x = −6.35 / −3.81 / +1.27 / +6.35 mm | `lib/footprints.tsx` |

**Why the 2 W part and not the 1 W one.** The 1 W B2405S-1WR3 (200 mA) cannot
cover the worst case: 194 mA estimated without the H1 expansion module and
267 mA with it (§3). R13 therefore fitted the 2 W member of the same family on
the same 24 V input. That swap is not a value change: the 2 W part is a
different package (19.65 × 7.05 mm body, 4 leads on the 1/2/4/6 positions of a
12.7 mm span instead of 4 leads on 1…4), so U16 has its own footprint, its own
placement and a re-solved autoroute. §5 lists exactly what moved.

Two consequences that are easy to get wrong:

- **Unregulated.** The 5 V output is an unfiltered, load- and input-dependent
  voltage (typ. ±10 % over the load range, worse at light load). Anything that
  needs 5.0 V ± 5 % is not supported by this rail. The 2 W part has the same
  behaviour; only the current rating changed.
- **40 mA minimum load** (was 20 mA on the 1 W part). With the Seed removed the
  only P5V loads are C1/C27 (no DC path) and TP1, so the converter runs at no
  load and the output rises. Never power the board for a measurement without the
  Seed installed, or load P5V with ≥ 40 mA (e.g. 120 Ω to GND) first.

**Input capacitor (DESIGN-REVIEW R13):** C32, 1 µF / 50 V 0603 (C559769,
CC0603KRX7R9BB105), across VIN_P (**module pin 1**) and VIN_N (**module pin 2**)
of U16. The B_S-2WR3 datasheet table 1 asks for 1 µF/50 V on a 24 VDC input. C26
(10 µF, 50 V) sits across VCC/VEE **before** L1/L2 and is therefore not the
recommended local input bypass, even though it is electrically on the same two
nets. The routed C32→U16 leg is **2.64 mm** (rail leg, `bun run
report:decoupling`); C32 sits between the two input leads on the far side of the
lead row, because the module body hangs 6.15 mm to the other side of the leads
(§5).

**Output capacitor:** C27 10 µF on P5V is the recommended output cap and stays
where it is.

## 2. What hangs off the 5 V rail

P5V has exactly one consumer plus its capacitors and test point:

```
U16.VOUT_P (pin 6) ── P5V ──┬── C1  10uF      ┬── DaisySeed1.VIN  (pin 39)
                            ├── C27 10uF      └── TP1
                            └── (nothing else on the base board)
```

Everything else is downstream of the Seed's **onboard 3.3 V regulator**, which
is fed from VIN and returns 3.3 V on pin 38 (V3V3_D). So the 5 V current is the
sum of the 3.3 V loads divided by the regulator's efficiency and the 3.3/5 V
ratio:

```
I(P5V) = I(P3V3 total) × (3.3 V / 5 V) / η ,  η ≈ 0.9  →  × 0.733
```

P3V3_D consumers on this board (`dist/index/circuit.json`, net P3V3_D):

| Consumer | Pins | What it is |
| --- | --- | --- |
| DaisySeed1 V3V3_D | 38 | **source** — STM32H750, SDRAM, codec |
| SD card | via CN3 (SPI + pull-ups R53…R57) | card write bursts |
| OLED header | via CN4 | SSD1306 128×64 module |
| H1 (PMOD) | 6, 11, 12 | optional external codec/DSP module |
| U15 TLP2362 | VCC (6) | MIDI IN optocoupler, plus R48 2.2 k LED pull-up |
| I2C pull-ups | R51, R52 4.7 k | SCL/SDA |
| MIDI OUT ring | R59 33 R → J_MIDI_OUT_RING | only conducts into the far end / a fault |
| Front panel | CN1 pin 14 | 3.3 V is also brought out on the euro connector |
| Decoupling | C28, C29, C30 | 2 × 100 nF + 10 µF |

Note that CN1 pin 14 makes the 3.3 V budget *open-ended*: the front panel can
draw arbitrary current from V3V3_D. Any measurement below must state whether
the front panel is connected.

## 3. Worst-case budget

Every row is an input assumption, not a measurement. `[ds]` = from a
datasheet/part record, `[est]` = engineering estimate for this design.

| Load (at 3.3 V unless noted) | Typical | Max | Basis |
| --- | --- | --- | --- |
| Seed: STM32H750 @ 480 MHz + SDRAM + codec, full DSP load | 70 mA | 120 mA | `[est]` — Seed datasheet gives the regulator headroom, not a per-load figure |
| SD card write burst | 30 mA | 100 mA | `[est]`/`[ds]` — SD spec allows 200 mA peaks; 100 mA is the usual card-class figure |
| OLED (SSD1306 with charge pump) | 12 mA | 25 mA | `[ds]` |
| U15 TLP2362 + R48 2.2 k | 7 mA | 14 mA | `[ds]` 14 mA max from the JLC record |
| Pull-ups R51/R52/R53…R57 (both I2C low) | 1 mA | 5 mA | calculated |
| **Subtotal without H1** | **120 mA** | **264 mA** | |
| H1 codec/DSP module (optional) | 40 mA | 100 mA | `[est]` — unknown module |
| **Subtotal with H1** | **160 mA** | **364 mA** | |

Converted to the 5 V rail (× 0.733, the factor from §2):

| Case | 5 V current | Fraction of the 400 mA rating | Verdict against 400 mA |
| --- | --- | --- | --- |
| Typical, no H1 | 88 mA | 22 % | ok |
| Max, no H1 | 194 mA | 49 % | ok |
| Typical with H1 | 117 mA | 29 % | ok |
| Max with H1 | **267 mA** | **67 %** | **ok — inside the rating with 133 mA (33 %) of margin** |

The converter's own losses are not specified in the datasheet, and the 5 V
test-point measurement adds a small tolerance. Even so the budget closes in
every case: the worst case is 67 % of the rating, and the case that could not be
budgeted on the 1 W part (H1 fitted) has 33 % headroom. The minimum load is the
remaining care item: 40 mA, i.e. the Seed installed or a 120 Ω dummy load.

## 4. Acceptance test (must be run before sign-off)

Setup:

- Board on the bench, **U16 installed**, Seed installed (the Seed is both the
  40 mA minimum load and the 3.3 V regulator whose efficiency is being
  measured).
- **Current-limited bench supply on J2** (§3 of AGENTS.md: J2 pins 9/10 = +12 V,
  1/2 = −12 V, 3–8 = GND), limit 150 mA per rail, ±12.0 V.
- Ammeter in series with U16 pin 6 (VOUT_P) or a current probe on the P5V
  copper; do **not** break the trace — lift one leg of C1 and use the pad, or
  use a series shunt on TP1/P5V.
- Measure with a DMM in DC mode over ≥ 10 s per state; record the 5 V rail
  voltage at U16 pin 6 as well.

States to record:

1. **Idle** — firmware booted, no audio, SD mounted, OLED on.
2. **SD write** — sustained card write (`dd` of a large file), measure the peak.
3. **Full DSP load** — worst-case audio algorithm at full sample rate, both
   channels, OLED on.
4. **Repeat 1–3 with the H1 module connected** if the front/expansion board is
   part of the build.

Pass/fail:

- **PASS ≤ 300 mA** on P5V in every state. 300 mA is 75 % of the 400 mA rating
  and 33 mA above the worst-case estimate of §3 (267 mA) — the margin covers
  datasheet tolerance, converter efficiency and the unmeasured loads while
  still catching a real overload long before the module's limit. The rail must
  also stay above the Seed's 5 V minimum (4.5 V per the Seed datasheet) in every
  state.
- **FAIL > 300 mA**, or a rail sag below 4.5 V, or audible/visible reset
  behaviour during SD writes → the load is higher than §3 assumes; find it
  (H1 module, front-panel 3.3 V take-off on CN1 pin 14) before moving to a
  larger converter — and that means another footprint, placement and baseline,
  not another value swap.

Record the four numbers in this file when they exist. Until then R13 stays open
on the measurement, not on the rating: **measure before the module is fitted for
good**, i.e. do not sign the board off on the paper budget alone.

## 5. What the 2 W swap changed on this board

Recorded so the next revision does not have to re-derive it (this section used
to be "fallback: the 2 W module, 7-pin SIP, not a drop-in" — the 2 W B_S-2WR3 is
in fact a 4-lead SIP on the module's own 1/2/4/6 positions, not a 7-pin part):

- **Footprint** `jclpcb:PWRM-TH-4P-L19.65-W7.05-P2.54-B2405S-2W`
  (`tscircuit/lib/footprints.tsx`): 4 plated holes, 1.0 mm drill / 1.8 mm pad, at
  local x = −6.35 / −3.81 / +1.27 / +6.35 mm on y = 0 (lead 1 → lead 6 = 12.7 mm).
  Body silk 19.65 × 7.05 mm spanning x −8.56 … +11.10 and **y −6.147 … +0.903**
  (the lead row is 0.90 mm from one long edge of the body, not centred), body
  hanging 6.15 mm to one side; courtyard +0.25 mm on every side, i.e. x
  −8.815 … +11.325, y −6.390 … +1.160. Geometry is the part's own, harvested
  from the LCSC/EasyEDA footprint (`tsci import C5369484`), not a symmetric
  assumption. Pin 1 has a square pad plus a 45° silk bevel and a lead tick, and
  the guide line inside the body is broken exactly where leads 3 and 5 are
  absent, so the module cannot be hand-fitted one 2.54 mm position off.
- **Pin map** (`tscircuit/lib/pins.ts`, `B2405S_2W_PINS`): pin 1 `VIN_P`, pin 2
  `VIN_N`, pin 4 `VOUT_N`, pin 6 `VOUT_P`. The frozen KiCad symbol had 1..4 as
  −Vin, +Vin, −Vo, +Vo, so the numbers are rewritten for the comparison by
  `SYMBOL_PIN_RENUMBER` in `tools/kicad2tscircuit.py` (1↔2 swap on the input
  pair, 3→4 and 4→6 on the output pair); every U16 net keeps exactly the nodes
  it had. Note that this also corrects the input polarity: pad 1 is the
  module's **+Vin**, and it is wired to `U16_VIN_P`.
- **Placement** U16 moved to (8.50, −49.50) mm on the bottom, body overhang
  toward the bottom board edge (courtyard x −2.825 … +17.315, y −55.890 …
  −48.340). Clear of J2 (1.19 mm), C24 (6.4 mm), C25, the four M3 holes and the
  board edge (`bunx tsci check placement`: 0 errors).
- **C32** moved to (13.58, −47.10) mm, between the two input leads on the side
  of the lead row the body does not overhang; the routed rail leg is 2.64 mm.
- **C23** stayed at (4.94, −54.67) mm: it is a **top**-side part and the module
  hangs off the **bottom**, so there is no physical or courtyard conflict (the
  placement checker compares courtyards per layer). Its pads are 5.2 mm from the
  U16 lead row, clear of the 1.0 mm drills.
- **Routing** re-solved: **304 traces / 334 vias / 4971 mm** of copper, 0
  unconnected (the previous board with the 1 W part was 304/325/4934). Snapshots
  regenerated.

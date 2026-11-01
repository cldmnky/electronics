# U16 5 V rail — power budget and acceptance test

**Status: budget only, nothing here is measured yet.** This file records the
5 V budget required by DESIGN-REVIEW R13, the input capacitor that R13 asked
for (C32), and the test that has to be run before the board is signed off. Do
not characterize P5V as a regulated precision 5 V rail.

## 1. The converter and its ratings

| Item | Value | Source |
| --- | --- | --- |
| U16 part | B2405S-1WR3 (JLCPCB C3031175), SIP-4, 11.6 × 6.0 mm, 2.54 mm pitch | JLC part record |
| Input range | 21.6 … 26.4 VDC (24 V nominal), i.e. VCC − VEE ≈ 23.4 V | datasheet |
| Output | 5 V, **1 W / 200 mA maximum**, **unregulated** | datasheet |
| Minimum load | **20 mA** | datasheet (YLPTEC/MORNSUN B_S-1WR3 series) |
| Isolation | 1.5 kVDC | datasheet |
| Board footprint | `jclpcb:PWRM-TH_4P-L11.5-W6.0-P2.54_B1503S`, 4 pads at 2.54 mm | `lib/footprints.tsx` |

Two consequences that are easy to get wrong:

- **Unregulated.** The 5 V output is an unfiltered, load- and input-dependent
  voltage (typ. ±10 % over the load range, worse at light load). Anything that
  needs 5.0 V ± 5 % is not supported by this rail.
- **20 mA minimum load.** With the Seed removed, the only P5V loads are C1/C27
  (no DC path) and TP1 — the converter runs at no load and the output rises.
  Never power the board for a measurement without the Seed installed, or load
  P5V with ≥ 20 mA (e.g. 220 Ω to GND) first.

**Input capacitor (DESIGN-REVIEW R13):** C32, 1 µF / 50 V 0603 (C559769,
CC0603KRX7R9BB105), directly across VIN_P (pin 2) and VIN_N (pin 1) of U16,
2.3 mm pad-centre from the VIN_P pad. The B_S-1WR3 datasheet table 1 asks for
1 µF/50 V on a 24 VDC input. C26 (10 µF, 50 V) sits across VCC/VEE **before**
L1/L2 and is therefore not the recommended local input bypass, even though it
is electrically on the same two nets.

## 2. What hangs off the 5 V rail

P5V has exactly one consumer plus its capacitors and test point:

```
U16.VOUT_P ── P5V ──┬── C1  10uF      ┬── DaisySeed1.VIN  (pin 39)
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

| Case | 5 V current | Fraction of the 200 mA rating | Verdict against 200 mA |
| --- | --- | --- | --- |
| Typical, no H1 | 88 mA | 44 % | ok |
| Max, no H1 | **194 mA** | **97 %** | no margin — inside the rating but only just |
| Typical with H1 | 117 mA | 59 % | ok |
| Max with H1 | **267 mA** | **134 %** | **exceeds the rating** |

Also add the converter's own losses (not specified in the datasheet) and the
5 V test-point/measurement tolerance, and the "max, no H1" row is at the edge.
The honest summary is: **the 1 W part has no margin in the worst case, and the
H1 module cannot be budgeted at all.** That is why the rail gets an acceptance
test instead of a claim.

## 4. Acceptance test (must be run before sign-off)

Setup:

- Board on the bench, **U16 installed**, Seed installed (20 mA minimum load and
  the 3.3 V regulator are what is being measured).
- **Current-limited bench supply on J2** (§3 of AGENTS.md: J2 pins 9/10 = +12 V,
  1/2 = −12 V, 3–8 = GND), limit 150 mA per rail, ±12.0 V.
- Ammeter in series with U16 pin 4 (VOUT_P) or a current probe on the P5V
  copper; do **not** break the trace — lift one leg of C1 and use the pad, or
  use a series shunt on TP1/P5V.
- Measure with a DMM in DC mode over ≥ 10 s per state; record the 5 V rail
  voltage at U16 pin 4 as well.

States to record:

1. **Idle** — firmware booted, no audio, SD mounted, OLED on.
2. **SD write** — sustained card write (`dd` of a large file), measure the peak.
3. **Full DSP load** — worst-case audio algorithm at full sample rate, both
   channels, OLED on.
4. **Repeat 1–3 with the H1 module connected** if the front/expansion board is
   part of the build.

Pass/fail:

- **PASS ≤ 170 mA** on P5V in every state (85 % of the 200 mA rating, i.e. 30 mA
  of headroom for the datasheet tolerances and the unmeasured loads). The rail
  must also stay above the Seed's 5 V minimum (4.5 V per the Seed datasheet) in
  every state.
- **FAIL > 170 mA**, or a rail sag below 4.5 V, or audible/visible reset
  behaviour during SD writes → **replace U16 with a 2 W module** (§5).

Record the four numbers in this file when they exist. Until then R13 stays open.

## 5. Fallback: the 2 W module (package caveat)

If the test fails, the drop-in-class replacement is a **2 W / 400 mA** member of
the same isolated SIP family (e.g. B2405S-**2**WR3). It is *not* a drop-in:

- The 2 W parts are **7-pin SIP with a 15.24 mm pin span**, not the 4-pin
  2.54 mm-pitch pattern of the 1 W part. The current footprint
  (`jclpcb:PWRM-TH_4P-L11.5-W6.0-P2.54_B1503S`, 4 pads, 2.54 mm pitch) cannot
  carry it, so the change needs **a new through-hole footprint**, a new
  placement for U16 (it is a hand-soldered bottom-side part in the rack
  cavity), a courtyard re-check against J2/C23/C25/L1, and a fresh autoroute +
  snapshot baseline.
- Pin numbering and the position of the two input/output pin pairs differ, so
  the schematic symbol pins (VIN_N/VIN_P/VOUT_N/VOUT_P = 1/2/3/4) must be
  re-mapped, not just re-footprinted.
- The 2 W part still needs the same input capacitor (C32 covers it) and still
  has a minimum-load requirement; check its datasheet before assuming the
  no-load behaviour improves.

This is recorded as a *plan*, not a change: U16 stays B2405S-1WR3 on this
revision. Changing it is a placement + footprint + routing revision, not a
value swap.

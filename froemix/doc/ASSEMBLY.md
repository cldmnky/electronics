# Hand-assembly sheet — parts JLC does not mount

Everything in `froemix-cpl-jlc.csv` is machine-placed by JLCPCB's SMT line
(106 placements: 98 on the bottom, 8 on the top). This sheet covers
**everything else**: the 10 through-hole parts kept out of the CPL, the two
jumper shunts for `J1` (not board parts), the Daisy Seed module (dual
`doNotPlace`), and the bare-copper aids.

Silkscreen references: every part below has its refdes printed next to it on
the **bottom** (component) side, so they read correctly when you flip the board
over. The board also carries a mirrored legend
`HAND SOLDER: CN1-4 H1 J1 J2 / C24 C25 U16 + DAISY SEED MODULE` and the
`FROEMIX` badge.

| Ref | Part | Package / footprint | Side | LCSC | Orientation cue |
| --- | --- | --- | --- | --- | --- |
| `CN1`, `CN2` | A2005WV-16P, 2.00 mm pin header, 1×16 | THT, 2.00 mm pitch | top | C225268 | pin 1 = **square pad** (left end, marked on silk) |
| `CN3` | HX PZ2.0-1x8P, 2.00 mm pin header | THT | top | C22438291 | pin 1 square pad |
| `CN4` | HX PZ2.0-1x7P, 2.00 mm pin header | THT | top | C22438290 | pin 1 square pad |
| `H1` | HC-PM254-8.5H, 2×6 2.54 mm right-angle (PMOD) | THT right-angle | bottom | C22373932 | body extends **+X**, toward the right board edge; pin 1 square |
| `J1` | 2×3 2.54 mm header (**MIDI polarity selector**) | THT | top | C65114 | pin 1 square; needs **two shunts**, see below |
| `J2` | 2×8 2.54 mm IDC box header (Eurorack power) | THT shrouded | bottom | C3406 | pin 1 = the square/`1`-marked pad; the built silk key/notch is on the **lower long side** (y ≈ −59.25…−60.56), i.e. **the same board edge as the two lower M3 holes**. Check the shroud key against the ribbon connector before soldering — the key must engage the connector's polarizing rib, not force it |
| `C24`, `C25` | 100 µF 50 V radial electrolytic, 8 × 12 mm | THT, 3.5 mm pitch | bottom | C2749 | **polarity**: `+` marked on silk; C25 is the negative-rail cap, its `+` faces **GND**. Body is 8 mm — the silk circle is the can, keep neighbours clear of it |
| `U16` | B2405S-**2**WR3, isolated 24 V→5 V SIP module (2 W / 400 mA) | THT, 4 leads on the 1-2-4-6 positions of a 2.54 mm grid | bottom | C5369484 | **pin 1 = `+VIN`** (square pad, 45° silk bevel and lead tick); leads 3 and 5 are absent, and the silk guide line inside the body is broken at those two positions, so the module cannot go in one position off. The body hangs 6.15 mm to one side of the lead row (toward the bottom board edge). Module stands **up** (≈10 mm tall) |
| `DaisySeed1` | Electrosmith Daisy Seed Rev4 module | 2 × 20 × 2.54 mm, 15.24 mm row spacing | bottom | — | pin 1 at the end marked on silk; the module lies **horizontally**, and its **USB receptacle is at the LEFT (−X) end of the module** (board x ≈ −25…−26.5, y ≈ 36.5…44.5), reachable **from behind** the board. There is no cutout to align with |
| `J1` shunts ×2 | 2.54 mm jumper shunt (not shipped with the board) | — | on `J1` | — | two shunts, Type A or Type B, see the MIDI section |
| `FID1–FID4` | fiducials | bare copper, 1 mm + 0.5 mm mask window | bottom | — | do nothing (no part) |
| `TP1–TP8` | test points | bare copper, 1.2 mm | bottom | — | do nothing (probe points) |

Nothing else changed for hand assembly with the last design revision: the new
parts (`Q2`, `R58`, `R59`, `R60`, `D5`, `C31`, `C32`, `C33`, `C34`, the
re-valued CV resistors) are 0603/SOT-23/2512 SMD and are all machine-placed by
JLC. `C24`/`C25` kept their 3.5 mm pitch and their through-holes; only the silk
body circle and the courtyard grew to the real 8 mm can.

## MIDI input polarity — `J1` (read this before fitting the shunts)

`J1` is **not** a MIDI socket: it is a 2 × 3 jumper block that selects the
TRS MIDI pinout. The MIDI socket lives on the front-panel board; `J1` sets
which TRS contact is tip and which is ring for the receive path.

| `J1` pad | Net | What it is |
| --- | --- | --- |
| 1, 6 | `NET_D1_A` | optocoupler **cathode** node (U15 CATHODE, D1 anode) |
| 2, 5 | `NET_J1_2` | → `R47` 220 Ω → optocoupler **anode** (`NET_D1_K`) |
| 3 | `J_MIDI_IN_RING` | TRS **ring** contact (current sink, MIDI IN) |
| 4 | `J_MIDI_IN_TIP` | TRS **tip** contact (current source, MIDI IN) |

MIDI TRS **Type A** puts the transmitter's current source on the **tip** and
its sink (DIN pin 5) on the **ring**; Type B swaps the two. On a receiver that
means Type A is *tip → LED anode*, and that is what pads 4–5 select. The two
pinouts, as built:

| Type | Shunts | Path |
| --- | --- | --- |
| **A** | **4–5 and 3–1** | tip → pad 5 → `R47` → opto anode; ring → pad 1 → opto cathode |
| **B** | 3–5 and 4–6 | ring → pad 5 → `R47` → opto anode; tip → pad 6 → opto cathode |

Shunting 4–2 / 3–6 instead of 4–5 / 3–1 is **electrically identical** (pads 2
and 5 are one net) — either pair is a valid Type A; likewise 3–2 / 4–1 for
Type B.

**This board's MIDI OUT is Type A too** (`R59` 22 Ω 1 W sources `P3V3_D` into
`J_MIDI_OUT_TIP`, `R60` 33 Ω sinks `J_MIDI_OUT_RING` through `Q2`), so a
front-panel jack wired **straight through** (tip → tip, ring → ring) is
correct for Type A equipment on both directions. Fit the Type A shunts unless
the gear in front of you is Type B.

- **Two 2.54 mm jumper shunts are required** and they are **not** board parts:
  they are not in the BOM, not in the CPL and JLC does not fit them. Buy two
  and fit one of the two pairs above.
- **Confirm the required type with a meter against the front-panel jack before
  powering up.** No shunts means the MIDI input path is open (no receive at
  all), and the wrong pair silently swaps tip/ring — the opto then sees the
  wrong polarity and, in the reverse direction, is protected by D1 but deaf.
  With a Type A front panel (the default here) the meter check is simply: pad 4
  (tip) and pad 5 (opto anode side) buzz, pad 3 (ring) and pad 1 (opto cathode
  side) buzz.

## Order of work

1. **Inspect the JLC return** — 106 SMT placements, 4 bare fiducials and 8 bare
   test points already on the board. Confirm the GND plane is visible on the
   bottom and the Daisy footprint is empty.
2. **Right-angle parts first** (they cannot be added once neighbours are tall):
   `H1` (bottom), then `J2` (bottom). Check `J2`'s shroud key against the
   ribbon connector before soldering it down.
3. **Power stage next, before anything expensive is on the board**: `U16`,
   `C24`, `C25` (watch polarity: C25's `+` faces GND, i.e. away from the VEE
   pad), then the headers `CN1`–`CN4` and `J1`. Solder the two opposite corner
   pins of each header, check the header sits square, then do the rest.
4. **Verify the rails with the module out of the socket** — `+12 V` and
   `−12 V` on `J2` from a **current-limited** supply (start at 50 mA/rail,
   ±12.0 V), with a **dummy load on P5V** (120 Ω to GND, i.e. ~42 mA — `U16`
   needs a ≥40 mA minimum load and its output rises at no load). `J2` is wired
   the standard Eurorack way: pads **1/2 = −12 V**, **3–8 = GND**, **9/10 =
   +12 V**, pads 11–16 (the bus 5 V/CV/GATE pins) are unconnected here. Expect
   ≈11.7 V at `TP4` (VCC) and `TP5` (VEE) after the Schottky drop, 5 V at
   `TP1` (P5V, unregulated — see below), and 3.3 V at `TP2`/`TP3`
   (P3V3_D/P3V3_A) **only once the Daisy module is fitted**, because the Seed
   *is* the 3.3 V regulator: with the module out, `TP2`/`TP3` read 0 V and
   that is correct. Never fit the Seed first and power up to "see if it
   works": a short on the rails then takes the module with it.
5. **Fit the two `J1` shunts** in the polarity the front board needs (above).
6. **Daisy Seed last**, and only after the rails verified: it is the part you
   least want to desolder, and it is the one part you cannot buy from JLCPCB.
   Fit it, tack two opposite corner pins, verify the board sits flat and the
   module is not cocked on a bent pin, then solder all 40 pins. The USB
   receptacle ends up at the **left (−X) end of the module, on the bottom
   side**, so it is reachable from behind the assembled module — no alignment
   step against a board edge.
7. **Re-check the rails with the module fitted** — `TP1` 5 V, `TP2` 3.3 V,
   `TP3` 3.3 V analog, `TP4` ≈ +11.7 V, `TP5` ≈ −11.7 V, `TP6` −10.0 V
   reference, `TP7` sync input, `TP8` muxed ADC node. Run the P5V budget test
   in [`POWER-BUDGET.md`](POWER-BUDGET.md) before calling the unit good
   (PASS ≤ 300 mA on P5V in every firmware state).
8. **Clean and inspect**: flux off, then a continuity pass on P5V / P3V3_D /
   P3V3_A / VCC / VEE / N10V_REF against the corresponding `TP` pads — every
   rail has a 1.2 mm test pad on the bottom side for exactly this.

## Notes

- **Two-sided build**: the machine-placed parts are on both faces (98 bottom,
  8 top). Handle the board by the edges; the bottom side carries the analog
  circuitry and the plane.
- **5 V rail is not a regulated 5.000 V rail.** `U16` (B2405S-2WR3) is a 2 W /
  400 mA **unregulated** isolated converter with a 40 mA minimum load: expect
  roughly ±10 % over load, worse at light load. Never measure the board with
  `P5V` unloaded. Budget, worst case and acceptance test:
  [`POWER-BUDGET.md`](POWER-BUDGET.md).
- **Iron settings**: 320–350 °C, chisel tip for the headers, fine tip for the
  Daisy's 2.54 mm pitch. Use leaded or lead-free per your supply; the board is
  ENIG, so either wets well.
- **Do not reflow the Daisy** unless you have a controlled profile: the module
  has its own connectors and an SD slot on the underside.
- **Heights to plan for** (all on the bottom, i.e. inside the rack cavity
  behind the panel): Daisy ≈ 12 mm, `U16` ≈ 10 mm, `C24`/`C25` ≈ 12 mm, `J2`
  shroud ≈ 9 mm, `H1` ≈ 8.5 mm. The top side is flat apart from the 2.00 mm
  headers (`CN1`–`CN4`) and the 2.54 mm `J1`.
- **Jumper shunts are consumables, not BOM lines**: two 2.54 mm shunts on `J1`,
  see the MIDI section. Nothing else on this sheet is a loose part.
- **Fiducials / test points are not parts**: nothing to fit, nothing in the BOM.
  If you probe them, use a fine spring pin; they are 1.2 mm bare copper.

## Verification after assembly

```bash
cd froemix
make erc netlist footprints parts   # design-side checks (unchanged by assembly)
make decoupling                     # cap -> IC routed copper distances
```

Then the power-first sequence above: current-limited `±12 V` on `J2`, dummy
load on `P5V`, rails at `TP1`…`TP5`, and only then the Daisy module and the
`POWER-BUDGET.md` acceptance test.

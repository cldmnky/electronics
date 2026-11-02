# Deep design review — Froemix Rev 0.1

Reviewed design: git commit `89807b7` (hand-maintained tscircuit source).

**Disposition: do not order this revision yet.** The port/routing regression
checks pass, but there are circuit-level faults and unsafe fault cases that
those checks do not test. A matching KiCad netlist also reproduces mistakes
already present in KiCad.

> **Status note (added after the fixes, original findings left as written except
> where a finding's own number was wrong):** the tree now
> carries the fixes for every finding below. With the resolution table that
> follows, **this revision may be ordered, subject to the two bring-up gates:
> R13 (measure P5V against the written budget before trusting the module) and
> R14 (the front board is bound by the written interface).** The gate is 15/15
> green and now includes the checks the review asked for (value literals,
> tscircuit-only parts, silkscreen bounds); the analog numbers quoted throughout
> are simulated, not measured on hardware, exactly as stated in the original
> findings.

## Resolution status

Every Rxx below was re-checked against the fixed tree. "Fixed" means the design
or the tooling changed; "closed by ..." means the finding is answered by a
written document plus a test that runs outside the gate, because it cannot be
closed by a schematic change on this revision.

| Item | Status | Where | One-line reason + the number that changed |
| --- | --- | --- | --- |
| R01 | **Fixed** | `sections/midi.tsx` (`REMOVED_REFS`: D2) | D2 was forward-parallel with the U15 LED; removed, D1 stays. MIDI IN current is now ~5.27 mA through the normal 220 Ω path instead of a 0.7 V shunt. |
| R02 | **Fixed** | `sections/midi.tsx`, `lib/footprints.tsx` (`FP_jclpcb_R2512`) | MIDI OUT is a non-inverting **Type A** transmitter: `Q2` is an **MMBT3906 PNP** (`C53444`, base through `R58` 4.7k, pulled up by `R61` 10k to `P3V3_D`) that switches `P3V3_D` into `R59` **22 Ω 1 W 2512** (`C118189`) and out on the **ring** (DIN pin 4, source), while the **tip** returns through `R60` **33 Ω 1 W 2512** (`C53728`) to GND (DIN pin 5, sink) — so the MCU pin no longer sits on the cable **and** the polarity is right: idle (UART high) leaves the PNP off, i.e. no loop current, and the start bit turns it on. Faults: a ring shorted to GND with the driver on is 3.3/22 = 150 mA, inside Q2's 200 mA rating, and 0.50 W in R59's 1 W part; a tip shorted to 3.3 V is 0.33 W in R60's 1 W part; a tip shorted to GND is nothing. Worst-case loop current is still **4.48 mA (≈4.5 mA)** into a receiver at the TLP2362's 1.7 V maximum Vf (3.135 V − 1.7 V − 0.2 V over 22 Ω + 33 Ω + 220 Ω). |
| R03 | **Fixed** | `sections/cv.tsx`, `INTENTIONAL_PIN_MOVES` (U11.3/12, R31.1, R32.1) | The two spare TL074 channels are unity-gain followers on the DAC outputs, so the unbuffered DAC's 13 kΩ only drives an opamp input. Simulation: 1.5121 V/V with the 13 kΩ source (was 1.085 without the follower). **Firmware must disable the DAC buffer** (`buff_state = DISABLED`). |
| R04 | **Fixed** | `sections/cv.tsx` (D5, C2128) | 1N4148WS across Q1 base-emitter, cathode on the base: a −10 V patch now clamps at about −0.6 V instead of driving −9.09 V into a 6 V-rated junction. |
| R05 | **Fixed** | `doc/ASSEMBLY.md` (rewritten) | J2's key is documented on the lower long side (silk y ≈ −59.25…−60.56, same edge as the lower M3 holes), the Daisy USB is documented at the module's **−X** end (board x ≈ −25…−26.5, y ≈ 36.5…44.5), J1 is documented as a Type A/B jumper block needing **two shunts** (not board parts) with the corrected map — **Type A = shunts 3–5 and 4–6** (ring → opto anode, tip → opto cathode), **Type B = 4–2 and 3–1**, and a 2.54 mm shunt only bridges the 2.54 mm pairs (4–5 is a 3.59 mm diagonal, never a valid pair) — and the sequence is power-first with a dummy load on P5V before the module is fitted. |
| R06 | **Fixed** | `sections/main.tsx` (C31), `tools/report_decoupling.py` | U1 has a local 100 nF VDD bypass in `P3V3_A` (7.71 mm routed rail leg; C19 used to be a 29.96 mm path away). The report is now one graph per net, layer-aware, measured to the supply pin: U1 7.71 mm rail / 12.98 mm GND, and the GND leg is the IC's return pin (VSS/VEE/DGND/GND/VMINUS), not a grounded signal input. A part whose return pin sits on another rail (the TL074s' VMINUS on VEE) now prints `-` instead of crediting a grounded non-inverting input; only a part with no named return pin at all (U14) falls back to the nearest pad on GND. |
| R07 | **Fixed** | `sections/main.tsx`, `sections/cv.tsx`, `tools/check_values.py`, gate step 7 | R51/R52 are `4.7k` and R24 is `49.9k` (built 4700 / 49900 Ω, MPN `0603WAF4701T5E` / `0603WAF4992T5E`); a gate step now compares every passive literal with the built value and the MPN (95 literals, 38 MPN cross-checks). |
| R08 | **Fixed** | `sections/audio.tsx` (R13/R14 = 33k, C4216) | Output gain is 3.300 (10.37 dB), so 1 Vrms full scale is 4.62 V peak at the jack against ±11.7 V rails — no rail clipping. −3 dB at 30.9 kHz simulated (30.0 kHz with the TL074 model) instead of 20.5 kHz at ×9.9. |
| R09 | **Fixed** | `sections/cv.tsx` (R39/R40, `INTENTIONAL_PIN_MOVES` R39.2/R40.2, C33/C34) | Feedback is taken past the 1 kΩ series resistor, so the DC gain is a load-independent 1.5121 V/V into 100 k, 50 k, 10 k and open (was 4.941 / 4.892 / 4.536 V: −11.8 and −98 ¢/octave of load error). That puts the 1 kΩ and the cable capacitance inside the loop, so `C33`/`C34` **100 pF across R39/R40** (zero at 15.9 kHz) compensate it: stable (0 % overshoot) to about **10 nF** of cable capacitance, marginal at 100 nF full scale, verified in `tools/sim` (deck 5). The 1 kΩ still limits a short to ~5 mA. |
| R10 | **Fixed** | `sections/cv.tsx` (R33/R42 30.1k, R34/R43 187k) | ±5 V now maps to 0.1046…3.1146 V (0.301 V/V, offset 1.6096 V) instead of exactly 0/3.3 V; the worst 1 % corner is 0.0742/3.1775 V, ≥60 mV clear of the MCP6002's rails. 1 % parts cost ±2.02 % of scale instead of clipping the endpoint. |
| R11 | **Fixed** | `lib/footprints.tsx` (`FP_jclpcb_CAP_TH_BD8_0_P3_50_D0_6_FD`, C24/C25 at (28.0,−44.5) and (33.5,−53.5)) | The footprint now carries the real 8 mm body circle (r = 4.0 mm silk) and an 8.6 × 8.6 mm courtyard (was 6.5 × 3.18 mm offset from the pads), so the placement checker sees the can. Pads/drills unchanged. |
| R12 | **Fixed** | `sections/silk.tsx`, `tools/check_silk.py`, gate step 14 | Every text is `anchorAlignment="center"`; the exported silk now spans x −36.58…39.77 (bottom) inside a −40.45…40.45 board, and the gate compares the gerber coordinates of both silk layers against `Edge_Cuts`. |
| R13 | **Fixed (2 W module) + measurement open** | `doc/POWER-BUDGET.md`, `sections/power.tsx` (U16, C32), `lib/pins.ts`, `lib/footprints.tsx` | U16 is now the **2 W / 400 mA B2405S-2WR3** (`C5369484`) on its own 4-lead 1/2/4/6 land pattern, because the written budget (194 mA worst case without the H1 module, 267 mA with it) did not fit the 1 W part's 200 mA. The budget now closes at 49 % / 67 % of the rating, C32 1 µF/50 V (C559769) is still the datasheet input capacitor (now 2.64 mm of routed rail from module pins 1/2), and the acceptance test stays as a bring-up step with the documented **PASS ≤ 300 mA**. **The measurement is still outstanding; the rating is not.** |
| R14 | **Closed by interface specification** | `doc/FRONTBOARD-INTERFACE.md` | `M_ADC_0…7` are specified as pot/attenuverter-only, powered from this board's own `P3V3_A`/`GND`, inside 0–3.3 V in every power order, with a front-board acceptance test that measures all eight pins at both pot extremes and in three power orders. **No clamp was added to this revision, so the restriction on the front board is what keeps U1 and the Seed ADC pins alive.** |

Two findings are therefore *not* closed by hardware on this revision, and that
is deliberate: R13 needs a bench measurement, and R14 is a constraint on a board
that does not exist yet. Both now have a document, a pass criterion and an owner
(the bring-up step and the front-board design respectively). Everything else is
fixed in the design or in the gate.

### Second review round

An independent review of the fixed tree found five things wrong, all of them in
the fixes rather than in the original design. What changed after it:

- **J1's Type A/B map was inverted.** The built nets decide it, not the prose:
  pads 1/6 are the U15 **cathode** node, pads 2/5 reach the **anode** through
  R47, pad 3 is the ring and pad 4 the tip. Type A puts the sender's current
  source on the **ring** (DIN pin 4) and its sink on the **tip** (DIN pin 5), so
  a Type A receiver takes ring → anode: **Type A = shunts 3–5 + 4–6** and
  **Type B = 4–2 + 3–1** (`doc/ASSEMBLY.md` and the R05 finding above were
  corrected). A 2.54 mm shunt only bridges the 2.54 mm pairs (1–2, 1–3, 2–4,
  3–4, 3–5, 4–6, 5–6); **4–5 is a 3.59 mm diagonal** and the old text specified
  it, which no shunt can fit.
- **MIDI OUT was inverted and is now a non-inverting Type A transmitter.** The
  NPN open collector conducted at UART idle (high) and broke the loop on the
  start bit — the opposite of MIDI — and it put the source on the tip. `Q2` is
  now an MMBT3906 **PNP high-side switch** (`C53444`, emitter on `P3V3_D`, base
  through `R58`, pulled up by `R61`), so idle/high-Z = off = no loop current and
  the start bit = on; the source leaves through `R59` on the **ring** and the
  **tip** is the sink through `R60` to GND. The panel jack wires straight
  through, and `doc/FRONTBOARD-INTERFACE.md` CN1 pins 3/4 were re-labelled to
  match: pin 3 = OUT tip = sink, pin 4 = OUT ring = source.
- **`R59` is 22 Ω 1 W (`C118189`), not 33 Ω**, so the transmitter keeps **4.48 mA
  (≈4.5 mA)** worst-case loop current into a receiver at the TLP2362's 1.7 V
  maximum Vf while the 0603 resistors that started R02 are gone. `R60` stays
  33 Ω: a tip pulled to 3.3 V then dissipates 330 mW in the 1 W part, and a ring
  shorted to GND is 150 mA through `Q2`, inside its 200 mA rating.
- **`C33`/`C34` 100 pF were added across R39/R40.** Moving the CV-out feedback
  past the 1 kΩ (the R09 fix) put the 1 kΩ and the cable capacitance inside the
  TL074 loop, and 1 nF rang with ~18° of phase margin. The 100 pF zero at
  15.9 kHz makes the loop stable to about 10 nF of cable; simulated in
  `tools/sim` (deck 5).
- **A new gate step (`check_extras`) closed the hole the netlist check left.**
  `check_netlist.py` strips `EXTRA_COMPONENTS` before comparing, so the pins of
  every tscircuit-only part were unverified; step 4 now asserts each declared
  pin sits on its declared net, that no `REMOVED_REFS` survived and that all
  six `INTENTIONAL_PIN_MOVES` are applied. The gate is 15 steps.

This is a review, not a redesign: no circuit, placement, BOM, or fabrication
output was changed. P1 means resolve before ordering a patchable module; P2
means correct or explicitly accept the limitation. Risks that need measurement
are identified separately from confirmed connectivity/value errors.

## Front-board LED fix (post-review, before first order)

Two front-board findings were fixed after the design review and before the
first fabrication order:

- **Audio activity LEDs removed (DL1–DL4, R10–R13, D2–D5).** The direct
  `jack -> 2.2k -> 1N4148 -> LED -> GND` branch is a nonlinear load. On the
  audio outputs it sits outside the base board's op-amp feedback loop; an
  illustrative ngspice run of the existing output chain with the front-board
  branch gave ~3.98 V positive / 4.62 V negative peaks and **~4.5 % THD** at
  full scale. On an audio input driven from 1 kΩ it gave **~4.8 % THD** at the
  jack. Both are illustrative models, not hardware measurements, but the
  loading is unambiguous from the connectivity. The four LEDs and their four
  panel holes are gone. The two CV-output LEDs remain: their 1 kΩ series
  resistor is inside the compensated CV feedback loop (R09), so the LED current
  is corrected rather than becoming an audio-band error.
- **MODE pull-up added (R8, 10 kΩ, P3V3_A to M_ADC_7).** The button LED was
  previously the only pull-up. Its leakage left the released mux node at an
  undefined intermediate voltage; an illustrative LED model gave roughly
  1.6–2.0 V depending on ADC/mux loading, not the documented 3.3 V. R8 defines
  the released level; the LED is now only an indicator. Pressed, R8 draws
  0.33 mA in addition to the 1.3 mA LED current.

`check_madc.py` now asserts the 10 kΩ R8 connection and rejects the removed
audio-LED refs; the panel fixture records the four-hole removal as an audited
mechanical revision. Counts: 12 SMT + 27 THT placements, 27 panel holes.

## Evidence and coverage

- `make gate`: **12/12 passed**, including ERC, netlist/footprint/part comparisons,
  source/pin checks, placement, Gerber-based shorts, PCB style, and the snapshots
  that the CLI tests. This does **not** establish analog function or protection.
- Inspected all five circuit sections, pin maps, footprints, fabrication aids,
  silkscreen, built `dist/index/circuit.json`, exported JLC BOM, and assembly guide.
- Cross-checked the STM32H750 DAC/GPIO specifications, TLP2362, the **actual
  YLPTEC** B2405S-1WR3 (C3031175), TL074, LM4040C10, component catalogue ratings,
  and MIDI CA-033. Do not substitute a different manufacturer's same-name part
  datasheet without checking it.
- Re-ran all four existing ngspice decks in a temporary directory; all analyses
  completed. Also ran isolated MIDI LED-shunt and full-scale audio checks.
- Independently checked resistor literals against their built numeric values,
  and power-pin distances using net- and layer-aware trace graphs.
- Inspected the actual `B_SilkScreen.gbr` and `Edge_Cuts.gbr` coordinate extents.
- No physical measurements, worst-case startup simulation, EMC test, or complete
  fabrication DFM sign-off was performed. Simplified opamp/diode simulations are
  not measurements of assembled hardware.

## P1 findings

### R01 — D2 bypasses the MIDI optocoupler LED: MIDI input will not work

**Confirmed wiring error.** `tscircuit/sections/midi.tsx:54–69,186–188`.

U15's LED is wired anode=`NET_D1_K`, cathode=`NET_D1_A`. D2, a 1N4148, has
**exactly the same anode and cathode nets**. It is therefore directly in parallel
with the infrared LED, in the forward direction. D1 is the correctly oriented
reverse-protection diode; D2 is not another useful protection diode.

A forward-biased silicon diode clamps the LED voltage well below its required
forward voltage. Toshiba specifies about 1.55 V typical for the LED at 10 mA.
An illustrative ngspice DC test with a 5 V source, 440 Ω transmitter resistance
and R47=220 Ω gave:

| D2 | LED voltage | Optocoupler LED current |
| --- | --- | --- |
| fitted | 0.705 V | approximately 4 pA |
| absent | 1.526 V | approximately 5.27 mA |

These are simple diode models, not a vendor-certified optocoupler simulation;
the important result is the direct shunt, which is unambiguous from connectivity.

**Action:** remove/DNP D2 (or redesign the intended protection circuit), retain
D1's antiparallel protection, and test MIDI reception. Changing R48 cannot fix
this input-side fault.

**After that fix:** check optocoupler input-current margin. TLP2362 is correctly
an **open-collector** device and works at 3.3 V, but has a 5 mA maximum switching
threshold and a recommended on-current starting at 7.5 mA. CA-033 calls for a
receiver turning on below 5 mA; some compliant transmitters deliver less than
5 mA at corners. Typical operation is not a worst-case interoperability proof.
A lower-threshold optocoupler is worth considering with its own pinout checked.

### R02 — MIDI OUT can overload R49 and the Seed GPIO during cable faults

**Confirmed component-rating mismatch under a short.**
`tscircuit/sections/midi.tsx:129–164`.

- R49 is 33 Ω directly from P3V3_D to the MIDI OUT ring. Its fitted C23140 part
  is a **100 mW 0603** resistor.
- Grounding the ring gives `I = 3.3/33 = 100 mA` and
  `P = 3.3²/33 = 330 mW`, over three times the resistor's continuous rating.
  At +5% supply and −1% resistance it is about 368 mW. A TS plug/adapter or
  cable fault can make this sustained, not merely an insertion transient.
- R50 is also 33 Ω, with USART1_TX connected directly to the MCU. A short can
  demand currents outside the STM32H750's 20 mA per-pin limit; GPIO output
  resistance is not a specified safe current limiter.
- The two 33 Ω resistors also differ from CA-033's recommended 33 Ω / 10 Ω
  3.3 V transmitter arrangement, reducing worst-case loop-current margin.

CA-033 explicitly specifies a 0.5 W rating or additional current limiting on
the 33 Ω supply resistor and warns about buffer short-circuit limits.

**Action:** redesign the transmitter's fault protection and drive stage. Use
properly rated resistors/current limiting and a suitably protected buffer or
driver. Do not simply reduce R50 to 10 Ω while leaving an unprotected MCU pin
behind it. Verify both normal-loop current and sustained short conditions.

### R03 — The CV OUT design assumes an ideal DAC that the Seed does not provide

**Confirmed datasheet/design-model mismatch; actual endpoints need measurement.**
`tscircuit/sections/cv.tsx:385–402,718–772`;
`tools/sim/cv_inout.cir:85–110`.

R31/R32 connect the Seed DACs directly to 33 kΩ inverting summing resistors.
The simulation replaces the MCU DAC with an ideal voltage source spanning
0–3.3 V, and consequently reports 0–4.94 V at a 100 kΩ jack load.

The STM32H750 datasheet specifies:

- **DAC buffer enabled:** characterized output range **0.2 V to VREF+−0.2 V**.
  This is a guaranteed usable range, not a claim that every part clips at
  exactly 0.200 V. With a 3.3 V reference, the present gain maps that range to
  about **0.299–4.641 V**, not a guaranteed 0–5 V.
- **DAC buffer disabled:** output can approach the rails, but output resistance
  is **10.3–16 kΩ, 13 kΩ typical**. It is appreciably loaded by R31/R32.
  At typical 13 kΩ, the idealized full-scale jack output becomes
  `3.3 × 49.9/(33+13) × 100/101 ≈ 3.54 V`.

Thus disabling the internal buffer alone does not fix the design. Firmware
calibration cannot manufacture missing endpoint voltage or recover saturation.
There is no firmware in this tree defining and validating the selected mode.

**Action:** design and verify the DAC interface explicitly. One possible repair
is to use the two unused U11 channels (A/D) as high-input-impedance followers
before R31/R32, with an appropriate unbuffered-DAC configuration. Then check
settling, offset, DAC linearity, reference tolerance, and full-scale range.
This is a proposal, not a verified modification.

### R04 — SYNC IN has no reverse base-emitter clamp for bipolar Eurorack signals

**Confirmed over-rating condition if a negative signal is patched.**
`tscircuit/sections/cv.tsx:167–244`.

Q1 is MMBT3904, emitter at GND. R22=100 kΩ feeds its base from J_SYNC_IN;
R23=1 MΩ pulls the base to GND. No antiparallel base-emitter protection diode
is present. The transistor's emitter-base reverse-voltage rating is 6 V.

Ignoring breakdown, the divider drives the base to approximately −9.09 V from
a −10 V patch, or −10.91 V from −12 V. The 100 kΩ resistor limits avalanche
current, but it does not prevent reverse base-emitter breakdown or its possible
gain/noise degradation. This is not a guarantee of immediate destruction; it is
an avoidable stress outside the component rating.

**Action:** add an appropriate reverse clamp (for example a diode with anode at
emitter/GND and cathode at base), and verify the input threshold/current over
positive and negative patch voltages. Alternatively state and enforce a
unipolar-only interface; relying on patch discipline is weak for Eurorack.

### R05 — The hand-assembly guide gives contradictory orientation instructions

**Confirmed documentation/geometry mismatches.** `doc/ASSEMBLY.md:20–23,37–40`.

- It says J2's key slot must face **away from the board edge**. The actual J2
  footprint draws its notch on the **−Y, lower-edge-facing side**:
  `lib/footprints.tsx:76–84`; built notch endpoints are around y=−59.25…−60.56.
  Rotating a polarized power connector to follow the prose is dangerous.
- It says the Daisy USB faces the **top edge**. The Seed has a pre-rotated R90
  footprint and lies horizontally; its USB outline is at the **−X end in
  top-view board coordinates**, not the +Y/top edge. Pin 1 is at
  `(−23.9,48.15)` in those coordinates. The assembly procedure repeats the
  incorrect direction.
- J1 is a MIDI polarity selector, not merely a header to solder. The required
  **two jumper shunts are missing from both the hand-parts list and the
  instructions**. With no shunts, the MIDI input path is open even after R01 is
  fixed. From the built nets: J1 pads 1/6 are the opto **cathode** node, pads
  2/5 go through R47 to the opto **anode**, pad 3 is the ring and pad 4 the tip.
  TRS **Type A** puts the sender's current source on the ring (DIN pin 4) and its
  sink on the tip (DIN pin 5), so a receiver takes ring → anode: Type A is
  shunts **3–5 and 4–6**; Type B (tip/ring swapped) is **4–2 and 3–1**. A
  2.54 mm shunt only bridges pads 2.54 mm apart — the pairs on this block are
  1–2, 1–3, 2–4, 3–4, 3–5, 4–6 and 5–6 — and the electrically equivalent
  3–2 / 4–1 are 3.59 mm diagonals, so they are not an option either.
  Verify against the selected jack/cable with a meter.

**Action:** replace ambiguous prose with annotated top/bottom assembly drawings
showing real pad numbers, connector keys, rail polarity, Seed USB location and
jumper settings. Verify the BOM-identified connector's physical pin-1/key
relationship. Check the power stage under a suitable load **before** fitting
the owned Seed module. The existing guide should not be used as sign-off.

## P2 findings

### R06 — U1 has no local supply bypass, and the distance report overstates coverage

`tscircuit/sections/main.tsx:415–454`;
`tools/report_decoupling.py:28–43,52–73,141–151`.

The CD4051 uses P3V3_A/GND, but has no local bypass capacitor. C19 is at U13,
not at U1. From the built pad coordinates, C19's rail pad to U1 pin 16 is
**25.55 mm straight-line / 29.96 mm along explicit routed rail copper**.
The Seed's internal capacitors are also not a local U1 bypass.

The report only examines a fixed list that omits U1. It then chooses the
shortest path between *any* cap and IC pads, without enforcing the supply pin,
net identity or copper layer in its graph. A ground leg to a grounded signal
input, or a crossing on another layer, can be mistaken for supply decoupling.
For example, C14 to **U11 pin 4** is 7.05 mm along the rail, not the quoted
4.5 mm minimum to any IC pin. These are single rail-leg lengths, not complete
high-frequency loop-inductance measurements; the GND pour is not in this graph.

**Action:** add a local bypass at U1 VDD/VSS and make the report pin/net/layer
aware. Include every powered IC rather than claiming all ICs from a partial
pair list. Review actual return paths and mux settling as well.

### R07 — RKM resistor strings silently compile to wrong numeric values and BOM comments

**Confirmed by the rebuilt circuit JSON and exported JLC BOM.**
`tscircuit/sections/main.tsx:276,296`; `sections/cv.tsx:249`.

| Ref | TSX | Intended / LCSC part | Built resistance / BOM Comment |
| --- | --- | --- | --- |
| R51/R52 | `4K7` | 4.7 kΩ, C23162 | **4 kΩ** / `4k` |
| R24 | `49K9` | 49.9 kΩ, C23184 | **49 kΩ** / `49k` |

The installed value parser drops the suffix digits. The LCSC-selected physical
parts are the intended values; the source model, schematic and BOM descriptions
are not. This creates contradictory assembly instructions and wrong analytical
values even though the parts gate passes.

**Action:** use unambiguous literals `4.7k` and `49.9k`, and check built numeric
values against the supplier data. `check_parts.py` currently derives expected
values from `build_components()` in the retired generator/KiCad model, not
from the built resistor values. The gate therefore misses this disagreement.

### R08 — Full-scale Seed audio output overdrives the ×10 output stages

`tscircuit/sections/audio.tsx:459–536,620–717`.

The Seed datasheet specifies 0 dBFS at about 1 Vrms at AUDIO OUT. U3's first
stage has magnitude gain 10, so a full-scale sine asks for **14.14 V peak**
before the second unity stage. The actual rails are about ±11.7 V; even an
ideal rail-to-rail opamp could not do that, and TL074 needs additional headroom.

An isolated transient run using the existing simple TL074 model, ±11.7 V rails
and a 1 Vrms/1 kHz source clips at about **±10.10 V at the jack**. The exact
physical clipping threshold needs measurement, but the rail-budget failure
at full scale is independent of that model. The existing AC-only deck cannot
reveal waveform clipping.

**Action:** choose the intended jack level and reduce analog gain, or explicitly
reserve firmware output headroom/limiting. For nominal ±5 V at full scale,
roughly ×3.54 would be needed from a 1 Vrms source. Verify the actual owned
Seed revision and codec level before selecting final gains.

### R09 — The 1 kΩ CV output resistors create load-dependent pitch error

`tscircuit/sections/cv.tsx:648–683`.

R45/R46 are outside the opamp feedback loop. This is useful isolation, but it
makes CV gain depend on how many modules are patched and their input impedance.
With an ideal 3.3 V DAC source (so this calculation is independent of R03):

| Jack load | Full-scale output |
| --- | --- |
| open | 4.990 V |
| 100 kΩ | 4.941 V |
| 50 kΩ (two 100 kΩ inputs) | 4.892 V |
| 10 kΩ | 4.536 V |

Changing from one 100 kΩ input to two after calibration changes scale by about
**−11.8 cents/octave**; changing to 10 kΩ gives about **−98.2 cents/octave**.
Calibration of fixed resistor tolerances does not remove variable loading.
TL074CPWR offset is also absent from the zero-offset simulation: its 10 mV
maximum input offset at 25°C can become tens of mV at the CV output through the
two stages' noise gains. Do not promise zero volts or precision pitch from the
nominal transfer function alone.

**Action:** specify allowed loading and accuracy, or use a suitably compensated
precision output topology that addresses output impedance and short protection
together. Include offset, DAC/reference errors and temperature in calibration.

### R10 — CV IN has no headroom at the claimed ±5 V endpoints

`tscircuit/sections/cv.tsx:362–382,422–459,781–815`.

The nominal transfer is `Vout = 1.65 − 0.33 Vin`; ±5 V requires exactly 3.3 V
and 0 V. An MCP6002 is not an ideal rail-to-rail voltage source, and resistor,
reference and offset errors consume further margin. The clipped region cannot
be restored by calibration. This was previously documented but remains a real
range limitation if full ±5 V capture is required.

**Action:** design guard bands and calibrate in the linear region, or narrow
the specified input range. The earlier **0.030/3.270 V** simulation endpoints
come from a deliberately hard-coded 30 mV model clamp; they are **not measured
endpoints or a validated vendor model** of this assembled circuit.

### R11 — C24/C25 courtyards do not contain the selected 8 mm capacitor bodies

`tscircuit/lib/footprints.tsx:238–244`.

C2749 is an **8 mm diameter** radial electrolytic. The footprint supplies only
pads and a plus marker, with a courtyard **6.5 × 3.18 mm**, offset from the body
centre. It cannot represent the part's physical keepout. The placement checker
can therefore report no overlaps while permitting another part into the body.
No actual body collision is asserted here; this is a confirmed DRC blind spot.

**Action:** preserve the correct pad pitch/drills but add the real body outline
and an appropriate ≥8 mm body courtyard/clearance. Recheck both capacitor bodies,
the U16 body, tool access and mounting hardware against the assembled board.
Matching an underspecified original KiCad footprint is not sufficient.

### R12 — Bottom silkscreen is actually outside the board in the release Gerbers

`tscircuit/sections/silk.tsx:68–94,116–131`.

The board notes use `center_left` at x=−39.6 on the bottom. The renderer/exporter
mirrors bottom text about its anchor, so the glyphs extend in the *negative* X
direction rather than into the board as intended.

This is not just a preview concern: the exported `B_SilkScreen.gbr` contains
coordinates down to **x=−47.525 mm**, while `Edge_Cuts.gbr` stops at
**x=−40.450 mm**. Board name/revision/date are clipped off the fabricated PCB.
The new badge's wordmark and tagline are mirrored outside their intended
13 × 7 mm badge area as well, although not all of that text is off-board.

**Action:** correct bottom-side anchoring/placement, validate actual glyph
bounds, inspect the bottom silkscreen alone, and add an outline/pad-clearance
check for silkscreen. Refreshing snapshots does not validate readability.

## Unclosed risks — do not mistake these for measured failures

### R13 — U16 has only a 1 W / 200 mA output budget for the Seed and peripherals

**Status: the finding is fixed, the measurement is not.** This section is the
finding as raised against the 1 W B2405S-1WR3 (C3031175). U16 is now the 2 W
B2405S-2WR3 (C5369484, module pins 1/2/4/6) with its own footprint, placement
and pin map; the budget closes at 194 mA / 267 mA worst case against 400 mA, and
the acceptance test in `doc/POWER-BUDGET.md` is PASS ≤ 300 mA, to be run during
bring-up before the module is fitted for good. The rest of this section is the
reasoning that led to the swap.

**P1 sign-off risk; actual overload has not been measured.**
`tscircuit/sections/power.tsx:250–278`;
`sections/main.tsx:207–246,260–272`.

The exact YLPTEC part C3031175 is rated **5 V, 200 mA maximum**, with a 20 mA
minimum load, and is unregulated. Selecting the 24 V input variant fixed the
old input-voltage error; it did not increase the output-power rating.

P5V powers the Seed, which in turn supplies SD, OLED, optocoupler, pots/mux and
H1's optional codec module. There is no recorded worst-case 5 V power budget or
startup/current measurement. As an illustration, **if** the Seed needs 140 mA
at 5 V and an external 3.3 V load needs 100 mA, a 90%-efficient conversion adds
about 73 mA at 5 V: already 213 mA before other peripherals. These are illustrative
loads, not measurements of this board.

Also, the converter's recommended input capacitor is across its **input pins**.
C26 is on VCC/VEE **before both L1/L2**, so it is not the recommended local
post-inductor input bypass. Inspect the input/output ripple and stability rather
than assuming those remote capacitors implement the datasheet application.

**Action:** budget/measure the actual Seed revision, CPU load, SD write bursts,
OLED and H1 loads, startup and rail tolerances. Choose a converter with justified
margin if those exceed the rating, and verify its recommended filtering. Do
not characterize the output as a regulated precision 5 V rail. Load an
unregulated converter appropriately when testing it before installing the Seed.

### R14 — M_ADC inputs are safe only under the stated pot-only constraint

**P2 interface risk, already known; not an inherent error for rail-bounded pots.**
`tscircuit/sections/main.tsx:93–100,439–454`.

CN1's M_ADC_0…7 go directly through a 0–3.3 V CD4051 to the Seed ADC, without
series current limiting or external clamps. This can be acceptable for pot
wipers powered only from the board's own 3.3 V/GND. It is **not** a patchable
Eurorack CV interface or a protected input for an independently powered board.

**Action:** enforce that constraint in the front-board design, including power
sequencing, or add suitable limiting/clamps and verify mux/ADC acquisition
settling. Do not assume 5 V-tolerant digital GPIO ratings apply to ADC operation.

## Checks that did not reveal a new fault

- J2 power nets: pins 1/2 are −12 V; 3–8 GND; 9/10 +12 V. D3/D4 series diode
  polarities are consistent with their respective rails.
- U16's 1/2/3/4 pin mapping agrees with the YLPTEC datasheet: input−, input+,
  output−, output+. The approximately 23.4 V differential input is appropriate
  for a 24 V part at nominal rack rails.
- C26's selected 10 µF ceramic is rated **50 V**, so spanning the two rails
  does not exceed its voltage rating. C24/C25 are 50 V parts.
- C25 polarity is electrically correct: positive pad 1 goes to GND; negative
  pad 2 goes to VEE. A misleading 3D stripe is not the electrical polarity.
- TL074 and MCP6002 power/pin maps, the grounded unused U11 followers, and
  LM4040C10 cathode/anode orientation are consistent with their datasheets.
- The reference bias has margin: about 0.673 mA shunt current at VEE=−11.7 V,
  and the existing sweep gives about 0.445 mA at −11.2 V, above the exact
  LM4040C10's 125 µA full-temperature minimum.
- The Seed audio interface is AC coupled on the module; no missing external
  codec-bias coupling capacitor is established by the base board's DC-coupled
  amplifier stages. Headroom is the demonstrated audio-output issue instead.
- The Seed GPIO mappings agree with its pinout. Repurposing the exposed USB
  **HS** pins for mux selection is not the same as shorting its onboard micro
  USB **FS** data pins.
- Existing routing/short checks remain green. No claim of an additional PCB
  copper short or missing routed connection is made by this review.

## Recommended closure order

1. Correct R01/R02 (MIDI functionality/protection), R03 (DAC interface) and R04
   (negative-input stress). Decide on R13's measured power budget before a PCB
   revision locks in the converter and its footprint.
2. Add U1 bypass; resolve resistor-literal/BOM discrepancies; set explicit
   audio/CV ranges and loaded CV accuracy. Improve the tests so these failures
   cannot pass the regression gate again.
3. Correct physical courtyards, silkscreen and the assembly guide/jumper list.
   Produce annotated bottom-side assembly and power-connector drawings.
4. Rebuild, run the gate and expanded electrical checks, regenerate the release,
   inspect the new JLC placement/DFM previews, and only then approve ordering.

## Datasheet / standards references

- [MIDI CA-033, 2014 electrical specification](https://mitxela.com/other/ca33.pdf),
  pp. 2–5: loop current, 33 Ω/10 Ω transmitter, resistor power and short protection.
- [Toshiba TLP2362](https://toshiba.semicon-storage.com/info/docget.jsp?did=7706&prodName=TLP2362),
  sections 3, 8, 9: open-collector output, LED forward voltage and input current.
  [Accessible datasheet mirror](https://datasheet.lcsc.com/datasheet/pdf/6486fb51249afe0f2fa67a5b3d9d24d4.pdf?productCode=C53244819).
- [STM32H750 datasheet DS12556](https://www.st.com/resource/en/datasheet/stm32h750vb.pdf),
  current characteristics and DAC electrical characteristics (rev. 5 tables 20,
  87/185 for silicon revisions Y/V): 20 mA GPIO limit; DAC range and output resistance.
- [YLPTEC B_S-1WR3 series, exact supplier datasheet](https://atta.szlcsc.com/upload/public/pdf/source/20240730/D84BE5DB47D0DE4F61CEFDAC0B24FA93.pdf),
  selection guide, application circuit and pinout;
  [C3031175 catalogue](https://www.lcsc.com/product-detail/C3031175.html).
- [TL074](https://www.ti.com/lit/ds/symlink/tl074.pdf): TL074C offset and output swing.
- [LM4040C10](https://www.ti.com/lit/ds/symlink/lm4040.pdf): exact 10 V grade current
  minimum and reverse-voltage tolerance (not the separate LM4040-N family).
- [Daisy Seed datasheet](https://daisy.nyc3.cdn.digitaloceanspaces.com/products/seed/Daisy_Seed_datasheet.pdf),
  pinout, 1 Vrms full-scale audio and module power interface. Confirm the owned
  module revision before finalizing assumptions.
- [C23140 resistor](https://jlcpcb.com/partdetail/23867-0603WAF330JT5E/C23140): 33 Ω,
  100 mW; the same rating is recorded in `.parts-cache.json`.

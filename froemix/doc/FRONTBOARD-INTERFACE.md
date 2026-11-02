# Base board ↔ front (euro/panel) board interface

Electrical contract between this base board and the 20 HP front board that
carries the jacks, pots/attenuverters, MIDI TRS, display and panel hardware
(the board that does **not** exist yet — see README, "Repository layout").

This document is the closure of DESIGN-REVIEW **R14** (`M_ADC` inputs are safe
only under the pot-only constraint). Everything marked **HARD** is a
requirement, not a preference: the base board has no protection that would
survive a violation, and one violation can destroy parts that are not
replaceable from stock (the owned Daisy Seed module).

The interface is two 2.00 mm 16-pin headers. Their purchase identity is now the
6mm-shoulder XKB `X4612WV-16I-C40D28-E60` (`C2883069`) so the existing 4.3mm
front sockets mate across 11mm clear spacers; the old `A2005WV-16P` was too
short. Pads, positions and labels are unchanged. `CN2` is the jack bus; `CN1`
carries MIDI, the muxed ADC inputs and 3.3 V. The mechanical contract (including
CN4 cut to seven ways and the J1/shunt limit) is
[`MECHANICAL-STACK.md`](MECHANICAL-STACK.md).

## 1. HARD requirements on `M_ADC_0` … `M_ADC_7`

`M_ADC_0…7` are **pot/attenuverter wiper inputs** — on the panel that exists,
`M_ADC_0…5` are the six pots, `M_ADC_6` the UI joystick's resistor ladder and
`M_ADC_7` the illuminated button's switch divider (`doc/EURO-BOARD.md` §2) —
nothing else. All of them are the same class of divider referenced only to
this board's `P3V3_A`/`GND`, which is what the rules below constrain.

- **HARD: the only thing that may drive `M_ADC_*` is a divider (potentiometer,
  attenuverter, resistor ladder or switch) powered from this board's own
  `P3V3_A` and `GND`** (`CN1` pin 15 / pin 16). No external CV, no ±12 V, no
  5 V logic, no signal from an independently powered module, no "just for a
  test" bench supply.
- **HARD: the voltage on every `M_ADC_*` pin must stay within 0 … 3.3 V at all
  times — including every power-up and power-down order**, with either board
  connected first, and while the front board is powered but the base board is
  not. There is no series resistor and no clamp between the header and the mux.
- **Why:** `M_ADC_*` goes straight into U1 (CD4051, 0…3.3 V only) and from there
  to the Seed's ADC pins (Daisy pins 24, 25, 28–30 are **3.3 V-only, not 5 V
  tolerant**). A voltage above ~3.8 V, or a negative voltage, injects current
  into the mux switch and the MCU's ESD structures. Both the CD4051 and the
  Seed's ADC block can be damaged, and the Seed is a single-source module that
  is hand-fitted.
- **Power sequencing is a design requirement on the front board**: if the front
  board ever receives 3.3 V from anywhere other than `CN1` pin 15 (a
  self-powered module, a USB-serial adapter, a second ribbon), it must guarantee
  its wipers stay at or below the base board's `P3V3_A` in every combination.
  Pulling the mux inputs up through a pot to an independent 3.3 V rail is
  **not** acceptable. If a design cannot guarantee that, it needs series
  resistors + Schottky clamps on the base board first — that is a board
  revision, not a firmware fix.
- Corollary: a front panel that wants a real CV input must take it through
  `J_CV_IN_1`/`J_CV_IN_2` (CN2 pins 7/8, ±5 V attenuverting inputs), which are
  the only analog inputs with a designed range.

`M_ADC_*` are read through a single CD4051, so keep the source resistance low:
a 10 kΩ pot is at most 2.5 kΩ at mid-travel, a 100 kΩ pot is 25 kΩ and slows
the sample-and-hold settling noticeably. Firmware owns the channel switching
delay and ADC acquisition time; the released MODE node has a 10 kΩ pull-up,
higher source resistance than a pot wiper. Verify settling on real hardware
when switching from a low channel to released MODE, including the actual ADC
sample time and scan rate; no particular delay has been qualified.

## 2. Connector pinout

### CN2 — jack bus (2.00 mm 16-pin, top side, board y ≈ −36.4)

| CN2 | net | Front-board function |
| --- | --- | --- |
| 1 | `J_AUDIO_IN_L` | audio in L (left/mono) |
| 2 | `J_AUDIO_IN_R` | audio in R |
| 3 | `J_AUDIO_OUT_L` | audio out L |
| 4 | `J_AUDIO_OUT_R` | audio out R |
| 5 | `J_SYNC_IN` | sync/clock in (unipolar, see §4) |
| 6 | `J_SYNC_OUT` | sync/clock out |
| 7 | `J_CV_IN_1` | CV in 1 (attenuverter) |
| 8 | `J_CV_IN_2` | CV in 2 (attenuverter) |
| 9 | `J_CV_OUT_1` | CV out 1 |
| 10 | `J_CV_OUT_2` | CV out 2 |
| 11–15 | *unconnected on the base board* | reserved: three are the candidate encoder pins (ENC_A/ENC_B/CLICK), the decision is still open |
| 16 | `GND` | ground, return of everything above |

### CN1 — MIDI, mux, power (2.00 mm 16-pin, top side, board y ≈ +59.6)

| CN1 | net | Notes |
| --- | --- | --- |
| 1 | `J_MIDI_IN_TIP` | MIDI IN TRS tip — DIN pin 5, the sender's current **sink**; reaches the opto cathode through the `J1` Type A shunts |
| 2 | `J_MIDI_IN_RING` | MIDI IN TRS ring — DIN pin 4, the sender's current **source**; reaches the opto anode through `R47` |
| 3 | `J_MIDI_OUT_TIP` | MIDI OUT TRS tip — this board's current **sink** (DIN pin 5): `R60` 33 Ω 1 W to GND |
| 4 | `J_MIDI_OUT_RING` | MIDI OUT TRS ring — this board's current **source** (DIN pin 4): `Q2` (MMBT3906 PNP) switches `P3V3_D` into `R59` 22 Ω 1 W |
| 5–12 | `M_ADC_0` … `M_ADC_7` | **§1 applies** — pot / joystick / button wipers only |
| 13 | `LED` | Seed `ADC_1` pin driven as an output; the panel LED needs its own series resistor |
| 14 | `P3V3_D` | 3.3 V digital, from the Seed's onboard regulator — see §5 |
| 15 | `P3V3_A` | 3.3 V analog, from the Seed `V3V3_A` — the **only** rail a pot may be powered from |
| 16 | `GND` | ground |

The MIDI TRS polarity is selected by the jumper block `J1` **on the base
board** (Type A or Type B, see `doc/ASSEMBLY.md`), not by the front board. The
front board only wires tip/ring through to the socket.

**Both directions are Type A on this board, so the panel jack wires straight
through** (tip → tip, ring → ring on both the IN and the OUT socket).

- **MIDI OUT: Type A transmitter.** The **ring** carries the current source
  (`Q2` switches `P3V3_D` into `R59` 22 Ω 1 W, **4.48 mA ≈ 4.5 mA** worst-case
  loop current into a receiver at the TLP2362's 1.7 V maximum Vf), and the
  **tip** is the sink (`R60` 33 Ω 1 W to GND). `Q2` is a high-side PNP, so it
  is **off at idle** (UART high or high-Z) and a start bit turns it on — the
  loop current is present only while data is being sent, as MIDI requires. Do
  not cross tip/ring on the panel: a crossover would turn the pair into Type B
  and every Type A device would see reversed polarity.
- **MIDI IN: defaults to Type A via shunts 3–5 + 4–6** on `J1`. Type B (4–2 +
  3–1) is available for Type B gear; the front board does not decide it. A
  2.54 mm shunt only bridges pads 2.54 mm apart, and 4–5 is a 3.59 mm diagonal,
  so it can never be one of the pairs.
- **Receiver threshold limitation:** the on-board receiver is a **TLP2362 with
  a 5 mA maximum switching threshold** (recommended on-current from 7.5 mA),
  while this board's own transmitter delivers **4.48 mA (≈4.5 mA) worst case**
  into a worst-case receiver. A source that only just meets CA-033 — or a
  receiver here driven by a marginal transmitter — can therefore be
  intermittent. That is a component-choice limit, not a wiring error: expect to
  verify interoperability with the actual units before relying on it.

## 3. CV in — `J_CV_IN_1` / `J_CV_IN_2`

Fixed analog path on the base board (U13A/U13B, MCP6002 on 3.3 V, summing
against the −10 V reference):

```
V(CV_IN_n) = 1.6096 V − 0.3010 × V(J_CV_IN_n)
```

| Quantity | Value |
| --- | --- |
| Input range | **±5 V** (the declared Eurorack range; ±10 V is **not** supported) |
| Output at −5 V in | 3.1146 V |
| Output at +5 V in | 0.1046 V |
| Scale | 0.301 V/V = 0.301 V per octave — **1 V/oct tracking needs firmware calibration** |
| Offset | 1.6096 V (i.e. 0 V in → 1.6096 V at the ADC) |
| With 1 % resistors (fitted) | ±2.02 % of scale (±32.5 mV) and ±32.5 mV offset; worst-case endpoints 0.0742 V / 3.1775 V, still ≥60 mV clear of both rails |
| Input impedance | 100 kΩ (R30/R44) |

The front board must therefore present CV as a low-impedance ±5 V source
(a pot attenuating a ±5 V CV is fine; a passive multiple of an unbuffered
source is not). Anything beyond ±5 V saturates the summer — it does not clip
softly, it just stops at the rail and the pitch is wrong.

## 4. CV out, sync, audio

### CV out — `J_CV_OUT_1` / `J_CV_OUT_2`

```
V(J_CV_OUT_n) = 1.5121 × V(DAC)
```

- **0 … 3.3 V DAC code → 0 … 4.99 V at the jack**, load-independent: R45/R46
  (1 kΩ) sit **inside** the feedback loop, so a single 100 kΩ input, two
  patched inputs or a 10 kΩ input all land on the same 4.990 V. The 1 kΩ is
  the short-circuit current limiter (~5 mA plus the TL074's own limit).
- **HARD firmware requirement:** the STM32H750 DAC **output buffer must be
  disabled** — in libDaisy, `DacHandle::Config buff_state = DISABLED`, i.e. the
  channel drives 0 … VDDA directly. With the buffer enabled the guaranteed
  DAC range is 0.2 V … VDDA−0.2 V and the jack cannot reach 0 V or full scale.
  The 33 kΩ summing resistors are fed through unity-gain followers (U11A/U11D
  for CV out 1/2) precisely so the unbuffered DAC's 10.3–16 kΩ output
  resistance does not cost gain: 28 % of the output range would be lost
  without them.
- Accuracy with 1 % resistors: ±4.04 % worst case at the jack (4.79…5.19 V),
  48.5 ¢/octave worst case; 0.1 % parts bring it to ±0.40 %.
- Op-amp offset adds **±60 mV** at the jack (TL074 Vos plus the second stage's
  noise gain), on top of the resistor tolerance. It is a fixed offset, so it is
  **calibratable** in firmware.
- The front board may load the output with **≥10 kΩ**; below that the opamp
  output (not the jack voltage) runs out of swing at about 5.54 V.
- **Maximum recommended load capacitance 10 nF.** The 1 kΩ series resistors sit
  inside the feedback loop (that is what makes the gain load-independent), so
  cable capacitance is in the loop: `C33`/`C34` 100 pF across the 100 kΩ
  feedback make 1 nF and 10 nF steps clean (0 % overshoot, 48 µs settle),
  while **100 nF rings on full-scale steps** (~50 % overshoot, 230 µs to
  settle — the op-amp charging the cable through the 1 kΩ, simulated in
  `tools/sim` deck 5). Keep panel/jack capacitance at or below 10 nF.

### Sync in / out — `J_SYNC_IN` / `J_SYNC_OUT`

- `J_SYNC_IN` is a **unipolar** comparator input (Q1 MMBT3904, 100 kΩ series
  into a 1 MΩ divider). Q1's base-emitter junction is clamped by D5, so a
  negative patch is no longer an over-rating condition, but the input is
  intended for 0…+5 V (or a gate/trigger) — apply ±12 V here and the threshold
  is meaningless even though nothing breaks.
- `J_SYNC_OUT` is **about 5.0 V, not 3.3 V**: a 1 kΩ-protected output driven by
  a ×1.512 gate shifter (U11B R24/R21 = 49.9k/33k, then U11C ×1), i.e.
  `3.3 V × 1.512 = 4.99 V` max with the 1 kΩ series resistor (R29) in front.
  So: **it is a 0 / 5 V gate into anything on the front board** — do not feed it
  into a 3.3 V-only panel logic input, and do not clamp it to a 3.3 V rail with
  a series resistor small enough to load it. It matches `J_SYNC_IN`'s own
  0…+5 V expectation, so a normalled sync return works.

### Audio in / out

| Path | Gain | Full scale | −3 dB |
| --- | --- | --- | --- |
| `J_AUDIO_IN_*` → Seed `AUDIO_IN_*` | **0.100** (−20.0 dB) | ±11.7 V at the jack → ±1.17 V at the ADC (safe for the codec) | 19.7 kHz (simulated, two cascaded poles) |
| Seed `AUDIO_OUT_*` → `J_AUDIO_OUT_*` | **3.300** (10.4 dB) | 1 Vrms (Daisy `0 dBFS`) → **3.27 Vrms ≈ 4.62 V peak** at the jack | 30.9 kHz (simulated, ideal opamps; 30.0 kHz with the TL074 model) |

- Both audio chains are **DC-coupled** on this board. Any DC offset the
  firmware leaves on the codec output is amplified ×3.3 and appears at the
  jack — the firmware must not put DC on `AUDIO_OUT_*`.
- 1 Vrms full scale is what the Daisy datasheet specifies; with ×3.3 the peak
  is 4.62 V against ±11.7 V rails, so full scale no longer clips (the pre-fix
  ×10 chain clipped at ±10.1 V). Do not raise the gain without redoing that
  arithmetic.
- Audio jacks: AC-couple on the front board if you want to be safe against
  DC offsets from *other* modules; the base board does not.

## 5. 3.3 V current budget on `P3V3_D` / `P3V3_A`

`P3V3_D` and `P3V3_A` are **not** generated on the base board: both come out of
the Daisy Seed (its onboard 3.3 V regulator, fed from `P5V`), which is itself
fed by the base board's **2 W / 400 mA unregulated** isolated converter (U16,
B2405S-2WR3). The whole 5 V budget therefore scales with the 3.3 V load:

```
I(P5V) ≈ I(P3V3_D total) × 0.733     (3.3/5 V, 90 % efficiency)
```

Base-board `P3V3_D` loads (from `doc/POWER-BUDGET.md`, all `[est]`/datasheet
figures, no measurement yet):

| Load | Typical | Max |
| --- | --- | --- |
| Seed itself: STM32H750 + SDRAM + codec, full DSP load | 70 mA | 120 mA |
| SD card write burst (CN3) | 30 mA | 100 mA |
| OLED (CN4) | 12 mA | 25 mA |
| MIDI opto U15 + R48 | 7 mA | 14 mA |
| I2C/SD pull-ups | 1 mA | 5 mA |
| Subtotal (base board, no H1 module) | **120 mA** | **264 mA** |
| H1 PMOD codec/DSP module (optional, external) | 40 mA | 100 mA |
| **Total** | **160 mA** | **364 mA** |

| Case | 5 V current | vs the 400 mA rating |
| --- | --- | --- |
| Typical, no H1 | 88 mA | 22 %, ok |
| Max, no H1 | 194 mA | 49 %, ok |
| Max, with H1 | 267 mA | 67 %, ok (133 mA = 33 % margin) |

A front board that adds more than ~130 mA at 3.3 V (≈95 mA at 5 V) no longer
fits that margin — see `doc/POWER-BUDGET.md` §3 before adding more load.

`P3V3_A` loads on the base board are small — U1 CD4051 (µA), U13 MCP6002
(≤0.2 mA), R25 10 kΩ, the bypasses — so roughly **1 mA typ / 2 mA max**,
**plus the panel pots**, which are the dominant term:

```
I(pots) = n_pots × 3.3 V / R_pot      e.g. 8 × 10 kΩ pots = 2.6 mA
```

Rules for the front board:

- **Budget the panel at ≤ 10 mA from `P3V3_A`** (8–10 pots at 10 kΩ, plus an
  LED driver if any) and **≤ 10 mA from CN1 pin 14 (`P3V3_D`)**. Every
  milliampere here is 0.73 mA on the 1 W converter.
- Do not power a display, an MCU, a codec module or an LED strip from
  `P3V3_D`. The OLED header (CN4) and the H1 PMOD ribbon are the sanctioned
  ways to attach those, and they are already in the budget above.
- Panel LEDs: `LED` (CN1 pin 13) is a raw MCU pin. Put the series resistor on
  the **front board**; do not draw more than ~4 mA and never let the panel pull
  the pin above 3.3 V.
- If the front board needs more than that, it needs its own 3.3 V regulator fed
  from `+12 V` — and then its analog section still may not drive `M_ADC_*`
  (§1).

## 6. Acceptance test for the first front board

Run in this order before the front board is trusted with the base board:

1. **Meter, unpowered.** With the front board alone: no continuity from any
   `M_ADC_*` pad to `+12 V`, `−12 V`, `5 V`, `P3V3_D` or any jack tip/ring.
   Each `M_ADC_n` reaches exactly one control node — a pot wiper, the joystick
   ladder or the button — and nothing else. Check the `CN1`/`CN2` pin-1
   orientation against the base board before mating (2.00 mm headers are not
   keyed).
2. **Standalone sweep, every control through every state.** Power the front
   board from an isolated 3.3 V supply on `P3V3_A`/`GND` only — no other rail,
   no base board — and confirm every `M_ADC_*` pin stays within 0 … 3.3 V in
   every state of the control that drives it:
   - `M_ADC_0…5` — the six pots (and any attenuverter wired to them): both
     extremes, expect ≈0 V … ≈3.3 V.
   - `M_ADC_6` — the UI joystick's resistor ladder: the five switch states
     (up / down / left / right / press) **plus the idle state with nothing
     pressed**. Every state must be a distinct, stable level inside 0 … 3.3 V.
   - `M_ADC_7` — the illuminated button: released (no switch current, LED dark,
     the node sits near `P3V3_A`) and pressed (≈0 V, the button's LED lights at
     approximately 1.3 mA through its series resistor, depending on LED Vf).
     R8 provides the direct 10 kΩ pull-up; the LED is a separate indicator
     branch. The channel must never be driven as an output.
   No point may exceed 3.3 V or go negative in any state, and every level must
   come from this board's own `P3V3_A`/`GND` divider — not from the supply, a
   jack or a second module.
3. **Sequencing.** Mate the boards and repeat (2) — every control in every state
   — for both orders: front board powered first, base board powered first, and
   with the base board off while the front board is on. In every case `M_ADC_*`
   must stay ≤ `P3V3_A`.
4. **CV in.** Patch a calibrated ±5 V into `J_CV_IN_1`/`2` and measure at
   `TP`/ADC: 3.1146 V at −5 V, 0.1046 V at +5 V (0.0742/3.1775 V allowed at the
   1 % resistor corners), no clipping at either end.
5. **CV out.** With firmware driving the DAC and the buffer **disabled**, code
   0 and full scale give 0.000 V and 4.99 V at `J_CV_OUT_n` into 100 kΩ (the
   ±4.04 % resistor and ±60 mV op-amp-offset window applies; it calibrates out),
   and the same values with 10 kΩ or two 100 kΩ loads patched (load
   independence). Steps into 1 nF and 10 nF must not ring; do not exceed 10 nF.
6. **Audio.** 1 Vrms/1 kHz at the Seed output gives ≈3.27 Vrms at
   `J_AUDIO_OUT_*` with no clipping; sweep 20 Hz…20 kHz and confirm the
   ≈30 kHz roll-off and no DC offset at the jack. Feed ±10 V into
   `J_AUDIO_IN_*` and confirm ≈1.0 V at the ADC.
7. **MIDI.** With the `J1` shunts fitted for the chosen TRS type (Type A =
   3–5 + 4–6, the default), a MIDI note from a known-good source produces
   activity on `USART1_RX`; unplug and re-plug the cable with the boards powered
   to prove the opto input survives (D1 clamps the reverse direction, D2 is
   gone). Then check the OUT path with a Type A receiver: the **ring** must be
   the current source (`Q2` on, sourcing through `R59`) and the **tip** the
   sink (`R60` to GND) — at idle the source is off and no loop current flows,
   and a start bit turns it on.
8. **Current.** Measure `P3V3_A` and `P3V3_D` current with the panel connected
   and compare with §5's ≤10 mA/≤10 mA budget, then re-run the `P5V`
   acceptance test in `doc/POWER-BUDGET.md` (PASS ≤ 300 mA) with the front
   board attached.

Record the numbers here when they exist. Until step 8 passes, the front board
is not signed off — and `M_ADC_*` in particular is a "must not exceed" rule, not
a "usually fine" rule.

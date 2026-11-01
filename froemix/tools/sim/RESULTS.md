# Analog verification — froemix base board

**Status: RUN.** Every number in the "measured" columns below came out of
ngspice 47 on 2026-11-01 (macOS 26.7.1 arm64, `brew install ngspice`). Raw
output is committed in `out/` (one `.log` + one `.dat` per deck) and the table
that `run.sh` prints is `out/measured.txt`.

```bash
cd froemix/tools/sim
bash run.sh            # 4 decks + analyze.py, writes out/ and out/measured.txt
python3 analyze.py     # re-analyse existing out/ without re-running SPICE
```

Nothing here touches the board: the decks are standalone `.cir` files
transcribed from `tscircuit/sections/*.tsx` (they never read `dist/` or run a
build), and this is not part of `bun run gate`. If a resistor or capacitor
changes in the TSX, change the deck — the deck is a copy, not a source.

| Deck | Circuit (TSX) | Analyses |
| --- | --- | --- |
| `audio_in.cir` | U2A/U2B, L channel — `sections/audio.tsx` | AC 1 Hz…1 MHz |
| `audio_out.cir` | U3A/U3B, L channel — `sections/audio.tsx` | AC 1 Hz…1 MHz |
| `cv_inout.cir` | U13 CV-in summer, U12 CV-out scaler — `sections/cv.tsx` | DC sweep ±5 V, DC sweep 0…3.3 V |
| `ref_bias.cir` | U14 LM4040C10 + R41 — `sections/cv.tsx` | DC sweep VEE −12.2…−11.2 V |

## Measured vs expected

### 1. Audio in — `J_AUDIO_IN_L` → `AUDIO_IN_L` (U2, TL074, ±12 V)

| Quantity | Expected | Measured | Verdict |
| --- | --- | --- | --- |
| DC gain (100k/100k × 100k/10k) | 0.1000 = −20.00 dB | 0.10000 = −20.000 dB | pass |
| stage 1 DC gain (`NET_C2_2`) | 1.000 = 0.00 dB | 1.000 = −0.000 dB | pass |
| −3 dB, single-pole estimate (brief) | 28.4 kHz (10k ∥ 560pF) | — | superseded |
| −3 dB, both poles cascaded | 19.9 kHz (33.86k ∥ 28.42k) | **19.71 kHz** | pass |
| gain at 1 kHz | 0.0999 | 0.09990 (−20.009 dB) | pass |
| gain at 20 kHz | 0.0704 | 0.07028 (−23.063 dB) | pass |

### 2. Audio out — `AUDIO_OUT_L` → `J_AUDIO_OUT_L` (U3, TL074, ±12 V, 100k load)

| Quantity | Expected | Measured | Verdict |
| --- | --- | --- | --- |
| DC gain before the 1k series R | 10.00 = 20.00 dB | 9.999 = 19.999 dB | pass |
| DC gain at the jack (1k + 100k) | 9.901 = 19.91 dB | 9.9004 = 19.913 dB | pass |
| −3 dB, single-pole estimate (brief) | 33.9 kHz (100k ∥ 47pF) | — | superseded |
| −3 dB, both poles cascaded, ideal opamps | 21.8 kHz | 21.75 kHz (see note) | pass |
| −3 dB, both poles cascaded, TL074 model | — | **20.46 kHz** | pass |
| gain at 20 kHz | — | 7.110 (+17.04 dB) | info |

The 21.75 kHz row is the same deck with the opamp made ideal (`A0=1e9
GBW=1e12`); it reproduces the closed-form 21.79 kHz to 0.2 %, which is what
validates the opamp model. The fitted TL074 costs another 6 % of bandwidth.

### 3. CV in — `J_CV_IN_1` → `CV_IN_1` (U13, MCP6002, 3.3 V single supply)

`Vout = −(R33/R30)·Vin + (R33/R34)·10 V = −0.330·Vin + 1.650`

| Quantity | Expected | Measured | Verdict |
| --- | --- | --- | --- |
| `V(Vin = −5 V)`, unclamped opamp | 3.300 V | 3.29995 V | pass |
| `V(Vin = +5 V)`, unclamped opamp | 0.000 V | 0.00000 V | pass |
| scale | 0.330 V/V (0.330 V/oct) | 0.33000 V/V | pass |
| `V(Vin = −5 V)`, real MCP6002 | — | **3.270 V (clipped)** | see finding 2 |
| `V(Vin = +5 V)`, real MCP6002 | — | **0.030 V (clipped)** | see finding 2 |
| endpoint, 1 % resistors | ±2.02 % scale, ±33 mV offset | 3.2346…3.3666 V / −0.0333…+0.0327 V | info |
| endpoint, 0.1 % resistors | ±0.20 % scale, ±3.3 mV offset | 3.2934…3.3066 V / −0.0033…+0.0033 V | info |
| pitch error, 1 % resistors | 24 ¢/octave | **24.2 ¢/octave** (+ a fixed ±101 ¢ offset) | pass |
| pitch error, 0.1 % resistors | 2.4 ¢/octave | **2.4 ¢/octave** (+ a fixed ±10 ¢ offset) | pass |

The corner instances (`*_N5HI/N5LO/P5HI/P5LO`) are the true worst cases at both
endpoints: scaling `RF` and `RIN` together makes the scale and offset errors
cancel at the +5 V endpoint by construction, so the cross corners are needed to
bound it. 1 % corners are `RF 33.33k / RIN 99k / RREF 198k` (scale and offset
max) and the three sign combinations; 0.1 % corners are the same at ×0.1.

### 4. CV out — `CV_OUT_1` → `J_CV_OUT_1` (U12, TL074, ±12 V, 100k load)

`Vout = (R35/R31)·(R40/R37)·Vin = 1.5121·Vin`

| Quantity | Expected | Measured | Verdict |
| --- | --- | --- | --- |
| `V(Vdac = 0 V)` | 0.000 V | 0.00000 V | pass |
| `V(Vdac = 3.3 V)` at the jack, 100k load | 4.940 V | 4.94048 V | pass |
| `V(Vdac = 3.3 V)` open (1.01 × loaded) | 4.990 V | 4.9899 V (derived) | pass |
| scale at the jack | 1.4971 V/V loaded (1.5121 open) | 1.49712 V/V | pass |
| endpoint, 1 % resistors | ±4.04 % worst case | 4.747…5.142 V (−3.92 %…+4.08 %) | see finding 4 |
| endpoint, 0.1 % resistors | ±0.40 % | 4.921…4.960 V (−0.40 %…+0.40 %) | info |
| pitch error, 1 % resistors | 48.5 ¢/oct worst case (34.3 RSS) | **48.9 / 47.1 ¢/octave** (the two corners) | pass |
| pitch error, 0.1 % resistors | 4.85 ¢/oct worst case (3.4 RSS) | **±4.8 ¢/octave** | pass |

### 5. −10 V reference — U14 LM4040C10, R41 2.2k, `N10V_REF`

| Quantity | Expected | Measured | Verdict |
| --- | --- | --- | --- |
| `V(N10V_REF)` | −10.000 V | −10.0004 V | pass |
| cathode current, VEE = −12.0 V | 0.809 mA (KCL below) | **808.9 µA** | pass |
| cathode current, VEE = −11.2 V | 0.445 mA | 445.4 µA | pass |
| cathode current, VEE = −12.2 V | 0.900 mA | 899.8 µA | pass |
| minimum over −11.2…−12.2 V | > 125 µA (brief), > 100 µA (LM4040-10 min) | **445.4 µA = 3.6×** | pass |

KCL at `N10V_REF`: `I_shunt = I_R41 − I_loads`, where `I_R41 = (V(N10V_REF) −
VEE)/2.2k` and `I_loads = 2 × 10 V / 200k = 100 µA` (R34/R43 see the U13 virtual
grounds, i.e. 200k to ground at DC). The summing loads *reduce* the shunt
current — they supply part of the R41 current themselves.

## Findings

1. **Both audio chains roll off at ~20 kHz, not at the 28/34 kHz the brief
   expected.** Each chain has two cascaded single-pole filters, not one: the
   −3 dB point of two identical poles is 0.6436 × the pole frequency, and the
   audio-in poles (33.9 kHz and 28.4 kHz) are close enough to interact. Measured
   −3 dB: **19.7 kHz in**, **20.5 kHz out**; at 15 kHz that is already **1.86 dB
   and 1.73 dB down**, and at 20 kHz **2.2 dB and 2.9 dB**. Nothing is broken —
   the corner lands on the audio band edge rather than well above it — but
   "flat to 20 kHz" is not true, and the quoted "~28 kHz / ~34 kHz" figures are
   44 %/65 % too high. Do not repeat them.
2. **The CV-in endpoints sit exactly on the 3.3 V rails, so the real MCP6002
   cannot reach them.** The nominal mapping is exact (0.000/3.300 V), but the
   MCP6002 output stops ~30 mV short, so the fitted board measures **0.030 V and
   3.270 V**: the outer ~1 % of the ADC range is unreachable at both ends, and
   any input beyond ±5 V simply clips there. With 1 % resistors the corner
   instances want 3.367 V / −0.033 V, i.e. they clip too. Consequences: a
   single-point firmware calibration at the extremes will read a saturated
   value; calibrate at ±4 V instead, and treat ±5 V as the clip point.
3. **The −10 V reference draws 0.809 mA, not 0.77 mA.** 0.77 mA is the R41
   current at VEE ≈ −11.7 V (or the shunt current at VEE ≈ −11.9 V); at the
   nominal −12.0 V rail the shunt carries 0.809 mA and the R41 current is
   0.909 mA. Either way the LM4040's 100 µA minimum is met with 3.6× margin
   across the whole −11.2…−12.2 V sweep, which was the actual question.
4. **With 1 % resistors the CV-out worst case leaves the intended 0–5 V
   window:** 5.142 V (+2.8 %) at the jack for a full-scale 3.3 V DAC code, and
   4.747 V at the other extreme. That is inside the TL074's ±10 V swing so
   nothing clips, but a downstream VCO expecting 0–5 V sees a 4.7–5.1 V range.
   The README's "~3 % / 36 ¢/octave" is the RSS figure (2.86 % → 34.3 ¢); the
   *worst case* is 4.04 % → 48.5 ¢/octave. 0.1 % parts bring it to ±0.40 %.
5. **1 % parts cost 24 ¢/octave on CV in and 48 ¢/octave worst case on CV out,
   plus fixed offsets** (CV in ±101 ¢ from the offset term, CV out none). The
   scale error is calibratable with two points; the CV-in offset with one. Both
   are already flagged as open items in the README/AGENTS.

## Method, and what the numbers do and do not cover

- Opamp model: one-pole, `A0 = gm·RP`, pole at `GBW/A0`, output clamped to
  `V(VP) − HEAD` / `V(VM) + HEAD`. TL074: `A0 = 2e5`, `GBW = 3 MHz`,
  `HEAD = 1.5 V`; MCP6002: `A0 = 1e5`, `GBW = 1 MHz`, `HEAD = 30 mV`.
  Validation: with the opamp made ideal the audio-out deck reproduces the
  closed-form two-pole answer to 0.2 % (21.75 kHz vs 21.79 kHz) and the exact
  divider gain (9.90099). Reproduce with
  `sed 's/A0=2e5 GBW=3e6/A0=1e9 GBW=1e12/' audio_out.cir` in a scratch copy.
- LM4040 model: ideal 10.0 V shunt + 0.5 Ω dynamic impedance. It regulates at
  any current, so it cannot prove the ≥100 µA requirement by itself — the deck
  reports the current instead, which is what the requirement is about.
- Not modelled: opamp input offset voltage (MCP6002 Vos ±4.5 mV → ~6.7 mV at
  the ADC, i.e. a fixed ~20 ¢ CV-in offset), input bias current, resistor
  tempco, the LM4040C10's ±0.5 % initial tolerance (±8 mV at the CV-in
  output, i.e. a fixed ~25 ¢ offset), supply-rail tolerance other than the VEE
  sweep, and any load capacitance. All are offsets or second-order and none
  changes a scale factor.
- The audio chains are DC-coupled on this board (no series capacitor between
  `AUDIO_OUT_L` and R11); the decks drive the source at DC 0, so any DC level on
  the Daisy codec output is *not* covered — it would be amplified ×10.
- Only the left channel of each audio pair is simulated; the right channel is
  identical by construction (R2/R3/R6/R7/C3/C9 and R12/R14/R15/R18/C13).

## Alternative runner: the tscircuit WASM engine (checked, not used)

Attempt (a) was to run these checks through `<analogsimulation
spiceEngine="ngspice" />`. Result: **the CLI does not bundle a native engine,
but it does not need one.** `@tscircuit/ngspice-spice-engine` (already in
`node_modules`) is a thin wrapper that fetches `@tscircuit/eecircuit-engine`
1.7.6 (a 21.7 MB ESM/WASM ngspice build) from `jscdn.tscircuit.com` at runtime.
A scratch project in `/tmp` (`node_modules` symlinked to this project's, no
native ngspice on PATH) built a 1:2 divider and produced
`simulation_transient_voltage_graph` values of 1.000 V / 0.667 V, i.e. it works,
but it needs network access and only exposes tran/op/dc/ac as circuit-json
graphs (it parses the netlist with `spicets` and honours a single `.print`), so
there is no `.control`/`wrdata` scripting and it cannot run these `.cir` decks
as-is. Real ngspice (installed here in about a minute) is the better runner for
`.cir` decks; the WASM path stays as the fallback for machines without Homebrew.

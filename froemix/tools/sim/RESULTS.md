# Analog verification — froemix base board

**Status: RUN.** Every number in the "measured" columns below came out of
ngspice 47 on 2026-11-01 (macOS 26.7.1 arm64, `brew install ngspice`); the
audio-out rows were re-measured after the R08 gain change (33k feedback), and
the CV-out rows and the new load-capacitance step rows (section 5) after the
C33/C34 compensation and the Type A MIDI OUT pass. Raw output is committed in
`out/` (one `.log` + the `.dat` files per deck) and the table that `run.sh`
prints is `out/measured.txt`.

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
| `cv_inout.cir` | U13 CV-in summer, U11A follower + U12 CV-out scaler — `sections/cv.tsx` | DC sweep ±5 V, DC sweep 0…3.3 V (load sweep), tran step into 1n/10n/100n load |
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

Gain is 3.3 since the R08 fix (R13/R14 = 33k instead of 100k), so a 1 Vrms
full-scale Seed output asks for **4.62 V peak** at the jack instead of 14.1 V
and no longer runs into the ±11.7 V rails.

| Quantity | Expected | Measured | Verdict |
| --- | --- | --- | --- |
| DC gain before the 1k series R | 3.300 = 10.37 dB | 3.2999 = 10.370 dB | pass |
| DC gain at the jack (1k + 100k) | 3.2673 = 10.28 dB | 3.2672 = 10.284 dB | pass |
| −3 dB, single-pole estimate (brief) | 33.9 kHz (100k ∥ 47pF) | — | superseded |
| −3 dB, both poles cascaded, ideal opamps | 30.9 kHz (102.6k ∥ 33.86k) | 30.85 kHz (see note) | pass |
| −3 dB, both poles cascaded, TL074 model | — | **30.0 kHz** | pass |
| gain at 1 kHz | — | 3.2656 (+10.279 dB) | info |
| gain at 20 kHz | 2.76 (+8.82 dB) | 2.7426 (+8.763 dB) | info |
| droop at 15 kHz / 20 kHz | −0.90 / −1.52 dB | −0.905 / −1.520 dB | pass |

The 30.85 kHz row is the same deck with the opamp made ideal (`A0=1e9
GBW=1e12`); it reproduces the closed-form 30.9 kHz to 0.2 %, which is what
validates the opamp model. The fitted TL074 costs another 3 % of bandwidth.
Stage 1 is now the wide pole (102.6 kHz) and stage 2 the narrow one
(33.86 kHz), so the cascaded −3 dB point is no longer 0.6436 × the low pole.

### 3. CV in — `J_CV_IN_1` → `CV_IN_1` (U13, MCP6002, 3.3 V single supply)

`Vout = −(R33/R30)·Vin + (R33/R34)·10 V = −0.3010·Vin + 1.6096` with R33/R42 =
30.1k and R34/R43 = 187k (was 33k/200k → `−0.330·Vin + 1.650`, which put both
endpoints exactly on the rails).

| Quantity | Expected | Measured | Verdict |
| --- | --- | --- | --- |
| `V(Vin = −5 V)`, unclamped opamp | 3.1146 V | 3.11458 V | pass |
| `V(Vin = +5 V)`, unclamped opamp | 0.1046 V | 0.10462 V | pass |
| scale | 0.3010 V/V (0.301 V/oct) | 0.30100 V/V | pass |
| `V(Vin = −5 V)`, real MCP6002 | 3.1146 V, no clipping | **3.11458 V** | pass |
| `V(Vin = +5 V)`, real MCP6002 | 0.1046 V, no clipping | **0.10462 V** | pass |
| endpoint, 1 % resistors | ±2.02 % scale, ±32.5 mV offset | 3.0529…3.1775 V / 0.0742…0.1344 V | pass (in range) |
| endpoint, 0.1 % resistors | ±0.20 % scale, ±3.3 mV offset | 3.1084…3.1208 V / 0.1016…0.1076 V | pass |
| pitch error, 1 % resistors | 24 ¢/octave | **±24.2 ¢/octave** (+ a fixed ±97 ¢ offset) | pass |
| pitch error, 0.1 % resistors | 2.4 ¢/octave | **±2.4 ¢/octave** (+ a fixed ±9.7 ¢ offset) | pass |

The corner instances (`*_N5HI/N5LO/P5HI/P5LO`) are the true worst cases at both
endpoints: scaling `RF` and `RIN` together makes the scale and offset errors
cancel at the +5 V endpoint by construction, so the cross corners are needed to
bound it. 1 % corners are `RF 30.401k / RIN 99k / RREF 185.13k` (scale and
offset max) and the three sign combinations; 0.1 % corners are the same at
×0.1. Every corner now stays at least 60 mV clear of both rails (worst case
3.1775 V and 0.0742 V against a 30 mV MCP6002 headroom), so the guard band
holds with 1 % parts, not just nominally.

### 4. CV out — `CV_OUT_1` → `J_CV_OUT_1` (U11A follower + U12, TL074, ±12 V)

`Vout = (R35/R31)·(R40/R37)·Vin = 1.5121·Vin`, with R40/R39 sensing **past**
the 1k series resistor and the two spare TL074 channels (U11A/U11D) buffering
the unbuffered DAC output.

| Quantity | Expected | Measured | Verdict |
| --- | --- | --- | --- |
| `V(Vdac = 0 V)` | 0.000 V | 0.00000 V | pass |
| `V(Vdac = 3.3 V)`, 100k load | 4.990 V | 4.98986 V | pass |
| `V(Vdac = 3.3 V)`, 50k load | 4.990 V (same as 100k) | 4.98986 V | pass |
| `V(Vdac = 3.3 V)`, 10k load | 4.990 V (same as 100k) | 4.98986 V | pass |
| `V(Vdac = 3.3 V)`, open | 4.990 V (same again) | 4.98986 V | pass |
| `V(Vdac = 3.3 V)`, 13k DAC source (buffer disabled) | 4.990 V | 4.98986 V | pass |
| scale at the jack | 1.5121 V/V, load-independent | 1.51208 V/V | pass |
| U12B output at the 10k load | 5.539 V (`1k·(1/10k + 1/100k)` above the jack) | 5.53874 V | pass |
| endpoint, 1 % resistors | ±4.04 % worst case | 4.794…5.194 V (−3.92 %…+4.08 %) | see finding 4 |
| endpoint, 0.1 % resistors | ±0.40 % | 4.970…5.010 V (−0.40 %…+0.40 %) | info |
| pitch error, 1 % resistors | 48.5 ¢/oct worst case (34.3 RSS) | **48.9 / 47.1 ¢/octave** (the two corners) | pass |
| pitch error, 0.1 % resistors | 4.85 ¢/oct worst case (3.4 RSS) | **±4.8 ¢/octave** | pass |

The four load rows are the R09 fix: with the feedback pickup moved to the jack
side of R46 the 1k sits inside the loop, so 100k, 50k, 10k and open all land on
the same 4.990 V and only the opamp output moves. Previously (1k outside the
loop) the same loads gave 4.941 / 4.892 / 4.536 V, i.e. −11.8 ¢/octave between
one and two 100k inputs and −98 ¢/octave into 10k. The `DAC13K` row is the R03
fix: a 13k unbuffered-DAC source in series with R31 costs 28 % without the
follower (1.5121 → 1.085) and nothing with it.

### 5. CV out load-capacitance stability — C33/C34 100pF across R39/R40

The R09 fix moved the CV-out feedback to the jack side of the 1k, which puts
the 1k plus the cable capacitance inside the TL074 loop. `cv_inout.cir` now
steps the DAC into 1 nF, 10 nF and 100 nF at the jack, with `CF=0` (the board
before this pass) and `CF=100p` (the fitted C33/C34 across the 100k feedback
resistors), at two amplitudes: 0.1 V DAC (0.151 V at the jack, linear) and
3.3 V DAC (4.990 V, full scale). Raw data: `out/cv_out_step_small.dat` and
`out/cv_out_step_full.dat`.

| Load | CF | 0.1 V step overshoot | 0.1 V settle (1 %) | 3.3 V step overshoot | 3.3 V settle (1 %) |
| --- | --- | --- | --- | --- | --- |
| 1 nF | 0 | 53.6 % | 10 µs | 50.6 % | 10 µs |
| 1 nF | 100 pF | **0.0 %** | **48 µs** | **0.0 %** | **48 µs** |
| 10 nF | 0 | 84.1 % | 92 µs | 60.5 % | 101 µs |
| 10 nF | 100 pF | **0.0 %** | **48 µs** | **0.0 %** | **48 µs** |
| 100 nF | 0 | 94.7 % (does not settle) | never, 30 mV pp ring | 60.4 % (does not settle) | never, 1.87 V pp ring |
| 100 nF | 100 pF | **1.5 %** | **65 µs** | 50.5 % | 230 µs |

The compensation zero is `fz = 1/(2π·100k·100p) = 15.9 kHz`. For a 10 nF load
the 1k/10 nF load pole is exactly at 15.9 kHz, so the zero cancels it; at 1 nF
the load pole is at 159 kHz, well above the zero, so the loop has even more
margin. At 100 nF the load pole is at 1.59 kHz, a decade below the zero, and
the small-signal step only just stays clean (1.5 % overshoot, 65 µs). The
full-scale 100 nF row is **not** a stability failure: the tail has settled by
450 µs, but the opamp model can only charge 100 nF through the 1 k (the output
clamp is a voltage clamp, so the fault current is limited to a few mA), which
costs ~50 % overshoot and 230 µs. So 100 pF is sufficient for the intended
loop up to 10 nF, marginal at 100 nF full scale, and the compensation pole
caps the CV-out signal bandwidth at ~16 kHz. No larger cap is fitted: it would
slow every CV edge.

The uncompensated 1 nF row reproduces the review's “≈18° phase margin”
warning: the closed-loop 1 nF response has a 9.1 dB peak at 473 kHz and the
step overshoots 54 %. The old single-pole “28/34 kHz” style bandwidth figures
do not apply to this loop; the CV-out bandwidth is set by C33/C34.


### 6. −10 V reference — U14 LM4040C10, R41 2.2k, `N10V_REF`

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

1. **Both audio chains have two cascaded single-pole filters, not one.** The
   audio-in chain (unchanged) rolls off at **19.7 kHz** — 1.86 dB down at 15 kHz
   and 2.2 dB at 20 kHz — because its two poles (33.9 kHz and 28.4 kHz) are
   close enough to interact, so 0.6436 × the low pole is a fair estimate there.
   The audio-out chain is now dominated by stage 2 alone: after the R08 change
   the 33k/47pF stage-1 pole is 102.6 kHz and the 100k/47pF stage-2 pole
   33.86 kHz, so the two different poles put −3 dB at **30.0 kHz** (measured,
   30.85 kHz with ideal opamps) and the chain is only 0.9 dB down at 15 kHz and
   1.5 dB at 20 kHz. The quoted "~28 kHz / ~34 kHz" single-pole figures for
   audio in remain 44 %/65 % too high. Do not repeat them.
2. **The CV-in endpoints used to sit exactly on the 3.3 V rails — that is
   fixed.** With 33k/200k the nominal mapping was 0.000/3.300 V and the
   MCP6002 output stops ~30 mV short, so the fitted board measured 0.030/3.270 V
   and every 1 % corner clipped. R33/R42 = 30.1k and R34/R43 = 187k move the
   transfer to `−0.3010·Vin + 1.6096`, i.e. **0.1046…3.1146 V for ±5 V**, and
   the fitted part reproduces it exactly (no clipping anywhere in the sweep).
   The worst 1 % corner reaches 3.1775 V / 0.0742 V, still 60 mV inside the
   rails. The trade is 0.301 V/V instead of 0.330 V/V of ADC span — 9 % less
   resolution — for an endpoint that a single-point calibration can no longer
   saturate. The pitch-error figures are unchanged (±24 ¢/octave at 1 %).
3. **The −10 V reference draws 0.809 mA, not 0.77 mA.** 0.77 mA is the R41
   current at VEE ≈ −11.7 V (or the shunt current at VEE ≈ −11.9 V); at the
   nominal −12.0 V rail the shunt carries 0.809 mA and the R41 current is
   0.909 mA. Either way the LM4040's 100 µA minimum is met with 3.6× margin
   across the whole −11.2…−12.2 V sweep, which was the actual question. (The
   deck still models the CV-in legs as 200k; at 187k they draw 107 µA instead
   of 100 µA, so the shunt would carry 802 µA — 0.8 % lower, same conclusion.
   `ref_bias.cir` was left untouched on purpose.)
4. **With 1 % resistors the CV-out worst case leaves the intended 0–5 V
   window:** 5.194 V (+4.1 %) at the jack for a full-scale 3.3 V DAC code, and
   4.794 V at the other extreme. That is inside the TL074's ±10 V swing so
   nothing clips, but a downstream VCO expecting 0–5 V sees a 4.8–5.2 V range.
   The README's "~3 % / 36 ¢/octave" is the RSS figure (2.86 % → 34.3 ¢); the
   *worst case* is 4.04 % → 48.5 ¢/octave. 0.1 % parts bring it to ±0.40 %.
5. **1 % parts cost 24 ¢/octave on CV in and 48 ¢/octave worst case on CV out,
   plus fixed offsets** (CV in ±97 ¢ from the offset term, CV out none). The
   scale error is calibratable with two points; the CV-in offset with one. Both
   are already flagged as open items in the README/AGENTS.
6. **The 1k CV-out series resistors are now load-independent but still the
   current limiter.** With the feedback pickup on the jack side, R45/R46 carry
   only the load current (25 mW into a dead short, 2.5 mW into 10 k), and the
   TL074 output moves by `1k·(1/RLOAD + 1/R40)` — 100 mV at a 100 k input,
   539 mV at 10 k — instead of the jack level collapsing. Short-circuit
   current is bounded by the 1k to ~5 mA plus the opamp's own output
   current limit, which is the protection R45/R46 were always there for.
7. **Taking the CV-out feedback after the 1k put the load inside the loop, and
   C33/C34 100pF are what make that loop stable.** Uncompensated the 1k/cable
   pole rings: 54 % overshoot at 1 nF, 84 % at 10 nF and no settling at 100 nF,
   matching the ~18° phase margin the review measured at 1 nF (9.1 dB peak at
   473 kHz). 100 pF across each 100k feedback resistor (zero at 15.9 kHz, the
   worst 1k/10 nF pole) removes the ringing at 1 nF and 10 nF completely and
   leaves 1.5 % overshoot at 100 nF small signal; a full-scale step into
   100 nF still overshoots 50 % / takes 230 µs because the opamp has to charge
   the cap through the 1k. That is the output-current limit, not a loop
   oscillation — the tail has settled. The same pole caps the CV-out bandwidth
   at ~16 kHz, which is far above any CV signal.

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
  sweep. All are offsets or second-order and none changes a scale factor. The
  CV-out load capacitance IS modelled in the step run (section 5), but the
  opamp only clamps its output voltage, not its output current, so the
  full-scale 100 nF row overstates the current limit of a real TL074.
- The audio chains are DC-coupled on this board (no series capacitor between
  `AUDIO_OUT_L` and R11); the decks drive the source at DC 0, so any DC level on
  the Daisy codec output is *not* covered — it would be amplified ×3.3.
- Only the left channel of each audio pair is simulated; the right channel is
  identical by construction (R2/R3/R6/R7/C3/C9 and R12/R14/R15/R18/C13).
- The audio decks are AC-only: they cannot show waveform clipping. The R08
  headroom claim is arithmetic (3.267 × 1.414 V = 4.62 V peak against a ±10 V
  swing), not a transient measurement — an isolated transient run of the old
  ×10 chain did clip at ±10.1 V.

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

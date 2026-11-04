# Analog verification — froemix base board and front board

**Status: ASSERTED AND REPEATABLE.** Every number below came out of a run of
`froemix/tools/sim/run_checks.py`, which now fails (exit 1) when a deck result
leaves its limit, when a linked value or connection no longer matches the
tscircuit source, when a model parameter is not in the provenance table, or when
a `wrdata` file / `.meas` result is missing, empty or holds a NaN. The last run
reported **129 checks over 9 decks, all passing**, on Linux with native
`ngspice-39`; the earlier table in this file was measured with `ngspice-47` on
macOS and the numbers agree to about 0.1 %.

These are design checks against **behavioural models**, not measurements of a
built board, and not manufacturer SPICE models. `models.py` holds the provenance
of every model parameter; the limits each check asserts are in `assertions.py`,
next to the requirement they stand for.

```bash
cd froemix/tools/sim
bash run.sh                  # every deck + every assertion (needs native ngspice)
bash run.sh --source-only    # source linkage and model tables only, no simulator
bash run.sh --deck audio_in  # one deck
python3 -m unittest test_sim # tests for the tools themselves (drift must be caught)
python3 analyze.py --dir <run-dir>   # human-readable table of a run directory
```

Exit codes: `0` all checks passed, `1` a check failed (including bad or absent
output), `2` the environment is incomplete (no ngspice, no source tree). A
missing simulator is a **failure, never a skip**.

## Isolation — why a stale file cannot pass

Each deck is copied into a **new, uniquely named** directory and run there,
with its own `wrdata` files, its own log and its own stderr. The default root
is `$TMPDIR/froemix-sim/`; `--workdir DIR` puts a fresh `DIR/<deck>-<random>/`
child there instead. An existing directory, or anything inside the requested
work root, is **never deleted** -- a caller's tree must survive a run, and one
run must not remove another (the harmless
`libtinfo` warning of a native ngspice is kept, never treated as a design
error). The reader rejects a missing file, an empty or header-only file, a
ragged row, a non-finite value, a missing `.meas` name and any `Error:` line in
the log. The runner also refuses a deck whose first line is not a comment (SPICE
treats that line as the title and silently drops it) and any passive value
written with a bare `M` suffix, which SPICE reads as milli while the schematic
means mega.

Nothing writes into the repository. **The committed `out/` directory is
history**: it holds the numbers the old macOS run produced, so `analyze.py`
prints it with a warning that it is not a current result. Refreshing `out/`
would dirty tracked files, so the tools do not do it.

## Decks and what each one covers

| Deck | Circuit (source) | Analyses |
| --- | --- | --- |
| `audio_in.cir` | U2A/U2B, L channel — `sections/audio.tsx` | AC 1 Hz…1 MHz, 10 Vpk 1 kHz headroom, 1k/10k source impedance |
| `audio_out.cir` | U3A/U3B, L channel — `sections/audio.tsx` | AC 1 Hz…1 MHz, load sweep (100k/50k/10k/open), 1 Vrms headroom, old gain-10 evaluation |
| `cv_inout.cir` | U13 CV-in summer, U11A follower + U12 CV-out scaler — `sections/cv.tsx` | DC ±5 V (ideal, 1 %, 0.1 % corners, fitted MCP6002), AC CV-in pole, DC 0…3.3 V load/DAC-source sweep, tran steps into 1/10/100 nF with and without C33/C34 |
| `ref_bias.cir` | U14 LM4040C10 + R41 — `sections/cv.tsx` | DC sweep VEE −12.2…−11.2 V, extra-load cases, VEE start-up ramp |
| `sync_io.cir` | Q1/D5 sync input, U11B/U11C sync output — `sections/cv.tsx` | DC −10…+10 V (clamp, levels, clamp current), 0…3.3 V sync-out sweep, 1 kHz square |
| `mux_adc.cir` | U1 CD4051 + ADC sample node — `sections/main.tsx` | 8-code channel scan (1-of-8 decode) and channel-change settling for a 10 k and a 100 k pot |
| `front_audio.cir` | base-board audio into the front board | THD with and without the removed audio jack-LED branch |
| `front_cv_led.cir` | last compensated CV stage — `sections/cv.tsx` + euro `jack-leds.tsx` | tran with 100k‖10 nF, LED branch present, and a 100 nF over-spec case |
| `front_mode.cir` | euro MODE button — `euro/sections/button.tsx` | pull-up, LED current, 100 pF sample, and the "LED only" negative case |

## Source linkage — the decks cannot drift silently

`source_link.py` parses the canonical TSX (`tscircuit/sections/*.tsx`, and for
the front decks `euro/sections/*.tsx` + `euro/lib/layout.ts`) with the same SI
literal parser gate step 7 uses, and links **63 deck elements** to their source
parts. For each linked element it compares

* the **value** to numerical parsing precision (relative window 1e-9): a deck
  transcribes the NOMINAL value of a part, so `33000` and `33100` ohm must NOT
  compare equal. Physical part tolerance is a different subject and lives in
  the 1 % / 0.1 % corner instances of the CV decks, which are engineering cases
  declared in `ASSUMPTIONS`; the same applies to the assertion bands in
  `assertions.py`, which are engineering limits and stay separate from this
  equality rule; and
* the **connection**: every deck node is mapped to a named source pin (or to the
  net of another part's pin), so a changed net, a swapped pin or a reordered
  polar part fails.

`DECK_NETS` does the same for the behavioural mux, whose deck uses the board's
own net names (`M_ADC_0…7`, `MULTIPLEX_S0…S2`, `MULTIPLEX_ADC_1`) against U1's
pins, and `check_mux_rails` asserts U1 `VDD/VSS/VEE/INH`. Elements that are not
source parts must be declared in `ASSUMPTIONS` with a reason (supplies,
stimuli, case loads, model internals); an undeclared element fails.

`test_sim.py` proves the linkage has teeth: an altered source **value**
(`R4` 100k → 110k, and a small 100k → 100.4k drift below 0.5 %), an altered
source **connection** (`R19` pin 2, U11 `IN1P`, U1 `X0`), an altered **deck**
value (33k → 33100), an altered deck node, a small instance-parameter drift
(`RF1` 49.9k → 49.95k), an unexplained deck element and an out-of-table **model
limit** (GBW 3 MHz → 30 MHz, diode `N`) are each detected. Two further tests
hold the runner: a caller-supplied `--workdir` keeps every file and directory it
had (a sentinel file, a same-named file, an older run directory), and a failing
assertion exits nonzero.

## Model provenance, and what is NOT modelled

Every model parameter must appear in `models.py` with its source, or the check
fails. The entries: TL074 (A0 2e5, GBW 3 MHz, 1.5 V output headroom, datasheet
typical), MCP6002 (1e5, 1 MHz, 30 mV), an ideal op-amp for model validation,
LM4040C10 (10.0 V, 0.5 Ω), 1N4148/1N4148WS forward characteristic (`IS`, `N`),
an illustrative LED, an illustrative 10 V breakdown knee (load/start-up cases
only), an illustrative MMBT3904-class NPN, the CD4051 250 Ω ON resistance
(assumption, not a maximum) and a 100 pF ADC sample capacitance (assumption;
the STM32H750 acquisition value depends on the sample time, which firmware
owns).

**The MCU is an STM32H750 (Daisy Seed).** No GPIO, ADC-leakage or drive figure
is copied from another board; the decks only use 0…3.3 V logic and the ADC pin
range from `doc/FRONTBOARD-INTERFACE.md`.

Not modelled anywhere: op-amp input offset voltage, bias current, slew rate,
output current limit (the clamp limits voltage, not current), noise, charge
injection, mux OFF isolation and ON-resistance flatness, resistor tempco,
supply tolerance other than the VEE sweep, and every thermal effect. The output
capacitor current in the full-scale 100 nF row is therefore optimistic.

## Measured vs expected (limits are asserted, not printed)

### 1. Audio in — `J_AUDIO_IN_L` → `AUDIO_IN_L` (U2, TL074, ±12 V)

| Quantity | Limit asserted | Measured |
| --- | --- | --- |
| DC gain at the jack | 0.0995…0.1005 | 0.0999985 |
| stage 1 DC gain (`NET_C2_2`) | 0.995…1.005 | 0.99999 |
| −3 dB (both poles cascaded) | 19.0…20.6 kHz | **19.707 kHz** |
| gain at 20 kHz | 0.0683…0.0725 | 0.070182 |
| gain at 1 kHz | −20.11…−19.91 dB | −20.009 dB |
| 10 Vpk headroom, peak / trough | ±0.99…±1.01 V | 0.998918 / −0.998918 V |
| THD at 10 Vpk, 1 kHz | ≤1 % | 0.0000 % |
| gain with a 1 k source | 0.985…0.995 | 0.989648 |
| gain with a 10 k source | 0.904…0.914 | 0.908678 |

### 2. Audio out — `AUDIO_OUT_L` → `J_AUDIO_OUT_L` (U3, TL074, ±12 V)

| Quantity | Limit asserted | Measured |
| --- | --- | --- |
| DC gain before R19 | 3.2835…3.3165 | 3.2999 |
| DC gain at the jack (100k) | 3.2510…3.2836 | 3.26722 |
| −3 dB | 29.0…32.5 kHz | **29.999 kHz** |
| gain at 20 kHz | 2.70…2.80 | 2.74066 |
| headroom at 1 Vrms in, peak / trough | ±4.55…±4.68 V | 4.61752 / −4.61752 V |
| old gain-10 chain, clamped peak | 10.0…10.6 V | 10.3959 V |
| THD at 1 Vrms, 1 kHz | ≤1 % | 0.0000 % |
| load sweep at the jack (100k/50k/10k/open) | 0.9901/0.9804/0.9091/1.0000 ±0.7 % | 0.98965 / 0.97995 / 0.90868 / 0.99954 |
| stage-2 output, every load | 0.995…1.005 | 0.999544 |

### 3. CV in — `J_CV_IN_1` → `CV_IN_1` (U13, MCP6002, 3.3 V)

| Quantity | Limit asserted | Measured |
| --- | --- | --- |
| `V(in = −5 V)` | 3.1096…3.1196 V | 3.11458 V |
| `V(in = +5 V)` | 0.0996…0.1096 V | 0.104624 V |
| scale | 0.30010…0.30190 V/V | 0.30100 V/V |
| fitted MCP6002 at both ends | within 10 mV of ideal | 3.11458 / 0.104624 V |
| 1 % corners: high / low endpoint | 0…3.24 V | 3.0529…3.1775 / 0.0742…0.1344 V |
| 1 % corner scale error | ≤2.1 % | 1.98…2.02 % |
| 0.1 % corner scale error | ≤0.21 % | 0.199…0.201 % |
| gain at 10 Hz / 1 kHz | −10.48…−10.38 / −17.9…−16 dB | −10.430 / −17.036 dB |

### 4. CV out — `CV_OUT_1` → `J_CV_OUT_1` (U11A follower + U12, TL074, ±12 V)

| Quantity | Limit asserted | Measured |
| --- | --- | --- |
| `V(3.3 V DAC)` | 4.975…5.005 V | 4.98986 V |
| `V(0 V DAC)` | ±2 mV | 0.00000 V |
| load independence, open / 10k | ≤0.2 mV | 0.0010 / 0.0040 mV |
| unbuffered DAC source 10.3k / 13k / 16k | ≤1 mV from nominal | 0.0000 mV each |
| **no follower** (negative case) | 1.063…1.107 V/V | 1.0848 V/V |
| 1 % corner spread | 5.15…5.21 / 4.77…4.83 V | 5.1935 / 4.7942 V |
| 0.1 % corner spread | 4.995…5.015 / 4.965…4.985 V | 5.0099 / 4.9699 V |
| 1 nF / 10 nF small step, C33/C34 | overshoot ≤2 %, settle ≤100 µs | 0.0 %, 48 µs |
| 100 nF small step, C33/C34 | overshoot ≤3 % | 1.5 % |
| 100 nF small step, **no** C33/C34 | overshoot 40…70 % | 53.6 % |
| 1 nF small step, **no** C33/C34 | overshoot 40…70 % | 53.6 % |
| 10 nF small step, **no** C33/C34 | overshoot ≤95 % | 84.1 % |
| 3.3 V step into 100 nF (**above spec**) | overshoot 30…70 % | 50.5 % |

The CV-out spec is **≤10 nF of cable/load capacitance** (AGENTS.md §3). The
100 nF cases are the negative control: they show what leaving the spec does, and
they prove the compensation and the model are not a rubber stamp.

### 5. −10 V reference — U14 LM4040C10, R41 2.2k

| Quantity | Limit asserted | Measured |
| --- | --- | --- |
| `V(N10V_REF)` at VEE = −12 V | −10.02…−9.98 V | −10.00040 V |
| shunt current at VEE = −12 V | 780…830 µA | **802.0 µA** |
| shunt current, worst low rail (−11.2 V) | ≥125 µA | **438.4 µA** |
| extra 22k load: `V` / shunt current | −10.05…−9.90 V / ≥125 µA | −9.9738 V / 361.0 µA |
| extra 2.2k load (fault): \|V\| | ≤9 V | 5.930 V |
| start-up at 1 ms (VEE still low): \|V\| | ≤5 V | 4.614 V |
| start-up settled at 7 ms | −10.05…−9.94 V | −9.9948 V |
| start-up peak cathode current | ≤15 mA | 0.805 mA |

The deck now uses the fitted 187k summing resistors (R34/R43), so the drain is
**802 µA**, not the 809 µA the old 200k placeholder gave; the conclusion (3.5×
the 125 µA threshold) is unchanged.

### 6. Sync in / sync out — Q1, D5, U11B, U11C

| Quantity | Limit asserted | Measured |
| --- | --- | --- |
| base clamp at −10 V in | −0.75…−0.35 V | −0.4774 V |
| base **without D5** at −10 V in | −9.5…−5.0 V | −9.0909 V |
| clamp current through R22 | ≤200 µA | 95.2 µA |
| `SYNC_IN` at +5 V in | ≤0.4 V | 0.0595 V |
| `SYNC_IN` at −5 V in | 3.0…3.31 V | 3.3000 V |
| `SYNC_IN` over a 1 kHz square | within 0…3.3 V | 3.3000 / 0.0595 V |
| sync-out gain before R29 | 1.5045…1.5196 V/V | 1.5121 V/V |
| sync-out gain at the jack | 1.4865…1.5076 V/V | 1.4971 V/V |
| sync out at a 3.3 V logic level | 4.90…4.98 V | 4.9405 V |

### 7. U1 CD4051 and ADC settling

| Quantity | Limit asserted | Measured |
| --- | --- | --- |
| 1-of-8 decode, every code | one channel each, ±2 mV | 8/8 channels, 0.09973…0.79781 V |
| 10 k pot: error 10 µs after a change | ≤1 mV | 0.0000 mV |
| 100 k pot: error 10 µs after a change | ≥10 mV | 55.6 mV |
| 100 k pot: error at 40 µs | ≤1 mV | 0.0003 mV |

So a 10 k pot settles far inside a 20 µs scan window and a 100 k pot does not,
which is the documented front-board limit. The mux ON resistance (250 Ω) and the
sample capacitance (100 pF) are **assumptions**, recorded in `models.py`.

### 8. Front board

| Quantity | Limit asserted | Measured |
| --- | --- | --- |
| audio THD, LED branch removed | ≤0.02 % | 0.0000 % (out and in) |
| audio THD, removed branch fitted (**negative**) | ≥1 % | 4.532 % / 4.843 % |
| CV jack with / without the LED branch | 4.90…5.10 V | 4.98995 / 4.98994 V |
| LED effect on the CV DC level | ≤10 mV | 0.011 mV |
| CV jack LED current | 0.1…3 mA | 1.079 mA |
| 100 nF CV load (**above the 10 nF spec**) | 6.0…8.0 V peak | 6.9943 V |
| MODE released / pressed | 3.27…3.31 V / ≤10 mV | 3.29000 V / 0.000159 V |
| button LED current when pressed | 0.2…3 mA | 1.263 mA |
| 100 pF sample 9 µs after connecting | ≤10 mV error | 0.373 mV |
| MODE released **without R8** (**negative**) | 0.5…2.0 V | 1.6314 V |

## Findings

1. **Both audio chains have two cascaded poles; the single-pole figures in the
   design brief are wrong.** Audio in lands at 19.707 kHz (poles 33.86 kHz and
   28.42 kHz), audio out at 29.999 kHz (poles 102.6 kHz and 33.86 kHz). Do not
   quote 28 kHz / 34 kHz as −3 dB points.
2. **The audio input gain depends on the source impedance**, because R1 100k is
   in series with the patched source: 1 k costs 0.9 % (0.9896), 10 k costs 9.1 %
   (0.9087). Both are now asserted.
3. **The old ×10 audio output chain hits the clamp.** The pre-R08 100k feedback
   asks 14.1 V peak from a 1 Vrms input and the model clamps at ±10.4 V; the
   fitted 33k asks 4.62 V. The change is what keeps the chain linear.
4. **The follower is what makes the unbuffered DAC harmless.** With U11A/U11D
   the DAC's 10.3…16 kΩ source changes nothing (0.0 mV); without the follower
   the same 13 kΩ adds to R31 and the scale falls to 1.085 (−28 %).
5. **The CV-out loop needs C33/C34.** With them, 1 nF and 10 nF overshoot 0.0 %
   and settle in 48 µs; without them 1 nF overshoots 53.6 % and 10 nF 84.1 %.
   Above the ≤10 nF spec the compensation runs out: 100 nF small signal is
   marginal (1.5 %), full scale rings 50.5 % / 230 µs because the current is
   limited by the 1 k.
6. **The −10 V reference draws 802 µA at VEE = −12 V**, worst case 438 µA at
   −11.2 V, i.e. 3.5× the 125 µA threshold. An extra 22k load still leaves
   361 µA; an extra 2.2k load is a fault and the knee model shows the reference
   collapsing to −5.93 V. The start-up ramp settles by 7 ms with a 0.805 mA peak.
7. **The sync input clamp is load-bearing.** Without D5 the same −10 V patch
   pulls Q1's base to −9.09 V, outside its 6 V emitter-base rating; with D5 it
   stops at −0.48 V and R22 limits the clamp current to 95 µA. The sync output
   is +1.5121 V/V (4.94 V at a 3.3 V logic level) and inverts nothing else.
8. **The ADC mux works with a 10 k pot and needs a longer window with a 100 k
   pot**: 1 mV error in under 10 µs versus about 40 µs, at a 100 pF sample
   capacitor.
9. **1 % parts cost 24 ¢/octave on CV in and 48 ¢/octave (worst case) on CV
   out**, plus a fixed CV-in offset. Still an open item: firmware calibration,
   or 0.1 % parts.

## Reserved: MIDI (not covered here yet)

The MIDI receiver/transmitter decks (`midi_in`, `midi_out`) are **not** part of
this suite. The approved MIDI topology and its constants are owned by a separate
change; until that lands, this suite makes no claim about `sections/midi.tsx`
and no deck reads it. A change to `sections/midi.tsx` therefore does not fail
here — a stated gap, not a silent pass. When the decks are added, their expected
values go into `DECK_LINKS`/`assertions.py` in one place and the linkage will
protect them like the rest.

## What the numbers do and do not cover

* Only the left channel of each audio pair is simulated; the right channel is
  identical by construction (R2/R3/R6/R7/C3/C9 and R12/R14/R15/R18/C13).
* The audio chains are DC-coupled; the decks drive the source at DC 0, so a DC
  offset on the Daisy codec output is *not* covered (it would be amplified 3.3×).
* Op-amp offsets, bias currents, slew rate, output-current limiting, noise,
  temperature, mux charge injection and cable parasitics are not modelled.
* The reference deck's nominal numbers use an ideal shunt (it regulates at any
  current); the load and start-up cases use an illustrative breakdown knee. The
  ≥100 µA minimum is therefore shown by the reported current, not by a
  regulation failure.
* The CD4051 and the ADC sample node are a behavioural switch + resistor +
  capacitor, not a mux model.
* Nothing here is a substitute for the bring-up measurements listed in
  `froemix/AGENTS.md` §8 (5 V rail, front board first article, CV tolerance,
  MIDI receiver threshold).

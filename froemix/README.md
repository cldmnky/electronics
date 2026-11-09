# Froemix

Daisy Seed based Eurorack module. The design is split over two boards:

| Board | Contents | Status |
| --- | --- | --- |
| **base board** (`tmp/`, `tscircuit/`) | Daisy Seed, CD4051 ADC multiplexer, Eurorack power entry (+/-12V -> VCC/VEE, +5V, -10V reference), audio in/out buffers, CV in attenuverters, CV out drivers, sync in/out, opto isolated MIDI, OLED/microSD/PMOD headers, board-to-board headers | converted to tscircuit |
| **eurorack board** (`euro/`, `doc/EURO-BOARD.md`) | 20HP front board: 10 mono jacks + 2 TRS MIDI jacks, 6 pots, 5-way UI joystick, illuminated MODE button, 2 CV-output activity LEDs, 1.3" OLED, and the panel | designed, routed, gated; the electrical contract it must meet is [`doc/FRONTBOARD-INTERFACE.md`](doc/FRONTBOARD-INTERFACE.md) |

Inspiration: Electrosmith Daisy Patch (`~/Downloads/Patch-Design-Files-EAGLE`,
20HP, 19 jacks, SMT euro jacks + TRS MIDI).

The Daisy Seed module itself is already in hand, so it is marked `doNotPlace` and
kept out of the JLCPCB assembly.

**Engineering prototype, not production-approved.** `make test` checks the
sources and behavioural models. Every production entry point separately
requires the fingerprint-bound evidence in `hardware-acceptance.json`; all
19 items are pending. See [hardware verification](doc/HARDWARE-VERIFICATION.md)
and [acceptance](doc/acceptance.md).

## Repository layout

```
froemix/
├── tmp/                 original KiCad 10 project (frozen reference; layout lives in tscircuit/)
│   └── library/         jclpcb.pretty + official Electrosmith DaisyKiCad footprints
├── tools/
│   ├── kicad2tscircuit.py   KiCad -> tscircuit port baseline (retired as an editor)
│   ├── check_netlist.py     netlist vs KiCad (pin for pin)
│   ├── check_extras.py      tscircuit-only parts: pins, removed refs, pin moves
│   ├── check_footprints.py  footprints vs KiCad (pad for pad)
│   ├── check_parts.py       JLCPCB parts vs schematic (type, value, package)
│   ├── report_decoupling.py cap -> IC routed-copper distances (informational)
│   ├── lcsc_models.json     EasyEDA 3D models per LCSC code (harvested once)
│   ├── release/             physical acceptance validator and regression tests
│   └── sim/                 source-linked audio/CV/power/control/MIDI SPICE checks
├── euro/                the 20HP front board + panel (tscircuit project)
│   ├── index.circuit.tsx    jacks, pots, OLED, LED + the 3 sockets that mate CN1/CN2/CN4
│   ├── panel.circuit.tsx    the front panel (holes + silkscreen, no copper)
│   ├── lib/layout.ts        ONE table of coordinates, imported by board and panel
│   ├── lib/footprints.tsx   XKB PJ-301M jack, Alps RK09K, sockets, OLED, LED
│   ├── panel/               built-artwork Nord/Signal previews and 1:1 fit templates
│   └── scripts/             board/panel gates and separate fabrication releases
├── tscircuit/           the base board design (hand-maintained)
│   ├── index.circuit.tsx    board outline + one <schematicsheet> per KiCad sheet
│   ├── sections/*.tsx       components per sheet, nets.tsx with all <net> elements,
│   │                        fabrication.tsx (fiducials/test points), silk.tsx
│   ├── lib/
│   │   ├── pins.ts          pin labels (KiCad pin number -> label) per IC
│   │   └── footprints.tsx   exact footprints for every part on the board
│   └── scripts/             gate.sh / release.sh (`bun run gate` / `release`)
└── fab/                 release output (gitignored): gerbers, bom, pnp, svg, step
```

The KiCad port is done: `tscircuit/` is now the design itself — edit
`sections/*.tsx` and `index.circuit.tsx` directly (positions, widths, phases,
hints), preferably with `bun run dev` open for live feedback. Do NOT edit via
the generator anymore.

`tools/kicad2tscircuit.py` is retained as the port baseline plus the parsing
library the check scripts import (net names, pin labels, footprint/3D-model
maps, `EXTRA_COMPONENTS`, `PART_OVERRIDES`). `bun run regen:check` regenerates
into a temp dir and diffs against the tree: an empty diff means the design
still matches the ported schematics; any diff is exactly the set of direct
edits made since the port — review it, don't blindly overwrite either side.
The verification scripts (`check_netlist`, `check_footprints`, `check_parts`)
keep working unchanged: they compare the KiCad sources against the BUILT
`dist/index/circuit.json`, not against the generator.

## Printed board identity

The base, euro board and panel carry **Rev 0.1** and a UTC `YYYY-MM-DD` build
date on their rear faces. The base also carries the Nord monoline FROEMIX
wordmark, an OSHW gear and a short hand-build reminder. The gear is not a
certification mark; its attribution, share-alike notice and compliance caveat
are in [`assets/README.md`](assets/README.md).

`tsci dev` only serves files inside the project it is started in, so imports
from a common `shared/` directory break the browser preview even though a Node
build resolves them. The identity constants therefore live in both
`tscircuit/lib/build-info.ts` and `euro/lib/build-info.ts`, and the base's
`tscircuit/lib/wordmark.ts` mirrors the euro panel's `MONO_GLYPHS`. This is a
checked duplication, not a second source of truth: `tscircuit/scripts/test_silk.test.ts`
asserts both revisions and dates are equal and that the base's built wordmark is
byte-for-byte the euro geometry. A revision bump changes both files.

`make release` captures one date for all three outputs. Set
`FROEMIX_BUILD_DATE=2026-11-02 make release` to reproduce a dated set; use a
real calendar date. The snapshot gate deliberately uses `2024-08-10`, so
snapshots are stable and that reference date is **not** the release date.
Install both board projects with `make install`. Check the generated rear
silkscreens in the manufacturer preview; a successful gate is not CAM approval.

## Eurorack (front) board

The 20HP board that carries every panel control, plus its panel:

```bash
cd euro
bun install            # once
bun run dev            # live preview
bun run gate           # typecheck, board/mux/stack/panel checks and snapshots
```

It mates with the base board through CN1 (MIDI, muxed pot inputs, UI controls,
LED, rails), CN2 (the jack bus) and CN4 (display), on 2.00mm sockets on its
bottom side, at the base board's own frozen coordinates. CN3 (micro SD) is
intentionally not mated yet. `euro/scripts/check_mating.py` verifies, from the
built circuit JSON, that every socket pin lands on the base board's pin, that
all 22 panel parts sit exactly above their panel holes, that no panel hole is
orphaned, and that nothing is left unrouted.

The UI costs no extra wires: the 5-way joystick and the illuminated button sit
on the two muxed pot channels `M_ADC_6`/`M_ADC_7` as resistor ladders (see
`doc/EURO-BOARD.md` §2 and §7 — the ladder levels are a firmware contract). The two CV jack LEDs need no pins at all: each one is driven by its own jack
signal through a diode and a resistor. The four audio LEDs were removed because
their direct diode/LED load distorted the audio jacks.

Design record, floorplan and the open decisions (joystick sourcing, OLED
module, jack height, button part, CN3/SD, panel material):
[`doc/EURO-BOARD.md`](doc/EURO-BOARD.md).

### Faceplate design and fabrication

The panel-specific record is [doc/FRONT-PANEL.md](doc/FRONT-PANEL.md): material
comparison, two white-on-black design directions, JLCPCB order settings and a
first-article fit checklist. **Signal** is the selected direction for this
revision (`FROEMIX_PANEL_DESIGN=nord` builds the Nord alternative). Recommended
material is **2mm FR4 with standard
black solder mask and white print**, ordered separately from the electronic
boards, without assembly or a stencil. A Gerber check is not a physical-fit
approval. The corrected Z stack, cut-to-seven CN4 preparation, printable MODE/NAV
actuator prototypes and remaining physical acceptance tests are in
[`doc/MECHANICAL-STACK.md`](doc/MECHANICAL-STACK.md).

## Base board <-> eurorack board interface

`CN1`/`CN2` are 2.00mm 16-pin headers — now XKB **6mm-shoulder + 4mm-post**
`X4612WV-16I-C40D28-E60` (C2883069), with **11mm clear spacers**, mating the
existing 4.3mm front sockets. The old short `A2005WV-16P` cannot reach across
the stack. `CN2` is the jack bus, `CN1` carries MIDI, muxed ADC inputs and power.

| CN2 | net | CN1 | net |
| --- | --- | --- | --- |
| 1 | `J_AUDIO_IN_L` | 1 | `J_MIDI_IN_TIP` |
| 2 | `J_AUDIO_IN_R` | 2 | `J_MIDI_IN_RING` |
| 3 | `J_AUDIO_OUT_L` | 3 | `J_MIDI_OUT_TIP` |
| 4 | `J_AUDIO_OUT_R` | 4 | `J_MIDI_OUT_RING` |
| 5 | `J_SYNC_IN` | 5-12 | `M_ADC_0` .. `M_ADC_7` (muxed, for pots) |
| 6 | `J_SYNC_OUT` | 13 | `LED` |
| 7 | `J_CV_IN_1` | 14 | `P3V3_D` |
| 8 | `J_CV_IN_2` | 15 | `P3V3_A` |
| 9 | `J_CV_OUT_1` | 16 | `GND` |
| 10 | `J_CV_OUT_2` | | |
| 16 | `GND` | | |

Other headers: `CN3` micro SD, `CN4` OLED (SPI + DC/RES), `H1` PMOD (I2S + I2C),
`J1` MIDI 2x3 **polarity-select jumper block** (two shunts — not a socket;
**Type A = shunts 3–5 + 4–6**, **Type B = 4–2 + 3–1**, see
[`doc/ASSEMBLY.md`](doc/ASSEMBLY.md)), `J2` Eurorack 2x8 power. A 2.54 mm
shunt only bridges two pads 2.54 mm apart, so the diagonal 4–5 (3.59 mm) can
never be one of the pairs.

**Both MIDI directions are TRS Type A**. The output uses `Q2` AO3401A
P-channel high side, `R58` 180 Ω gate series and `R61` 2.2 kΩ gate-to-source.
Ring sources current through `R59` 33 Ω 1 W; tip returns through `R60`
22 Ω 1 W to GND. The input uses TLP2361 and defaults to Type A with the
`J1` shunts above. Straight-through panel wiring is correct. The 33/22 Ω
output differs deliberately from the CA-033 reference; interoperability,
high-current corners, fault temperature and the assumed STM32 GPIO bounds
remain pending. See [electrical verification](doc/ELECTRICAL-VERIFICATION.md).

The electrical contract for the front board (naming, limits, power budget and
its acceptance test) is [`doc/FRONTBOARD-INTERFACE.md`](doc/FRONTBOARD-INTERFACE.md):
`M_ADC_0…7` may only be driven by pot/attenuverter wipers powered from this
board's own `P3V3_A`/`GND`, inside 0–3.3 V in every power order.

## Verification

Install Bun, Python 3, **KiCad 10** with its genuine symbol/footprint libraries,
and native **ngspice**. For example, Debian/Ubuntu packages include `kicad`,
`kicad-symbols`, `kicad-footprints` and `ngspice`; macOS can use the KiCad
application and `brew install ngspice`. The frozen project also references
the genuine 4ms KiCad library. Missing tools/libraries must fail clearly.

`KICAD_CLI` selects a nonstandard executable; otherwise the checker uses PATH
and the macOS bundle. `FROEMIX_KICAD_SHARE` selects the KiCad share directory;
`FROEMIX_KICAD_LIBS` supplies external libraries. ERC runs on a private copy,
resolves library tables without editing schematics, requests errors, warnings
and exclusions, and rejects missing/truncated reports or unexpected exits.

```bash
cd froemix
make install           # install both projects from their locks
make typecheck         # both boards, no unsupported-prop suppressions
make test              # both gates + mechanical + unit + asserted SPICE checks
make sim               # source-linked SPICE only
make acceptance        # report physical state, not an approval
make acceptance-check  # nonzero while evidence is pending/invalid/stale
make release           # production: acceptance runs BEFORE exports/deletion
```

The canonical checks still compare 102 nets and 429 pads; base assembly has
117 placements (107 bottom SMT + 10 hand-fitted THT), using 40 unique codes.
The combined BOM is reference only: upload the SMT-only BOM/CPL. Exact merged
results are recorded in [HARDWARE-VERIFICATION.md](doc/HARDWARE-VERIFICATION.md).

**Firmware requirement:** the STM32H750 **DAC output buffer must be disabled**
(libDaisy `DacHandle::Config buff_state = DISABLED`, i.e. 0…VDDA output). The
CV-out stages are fed through unity-gain followers (U11A/U11D) so the unbuffered
DAC's ~13 kΩ output resistance does not cost gain; with the buffer enabled the
usable DAC range is 0.2…3.1 V and CV out cannot reach 0 V or full scale. See
[`doc/FRONTBOARD-INTERFACE.md`](doc/FRONTBOARD-INTERFACE.md) §4.

### Analog verification (SPICE)

Eleven native-ngspice behavioural decks cover audio, CV, reference bias,
sync, mux settling, front controls and MIDI. The runner checks nominal values
and connectivity against the canonical TSX, validates model assumptions and
asserts fresh results. Negative tests exercise source/deck/model drift and
missing, malformed, non-finite or stale output. These are simulated results,
not measurements of an assembled unit; see [RESULTS.md](tools/sim/RESULTS.md).

```bash
cd froemix/tools/sim
bash run.sh            # native ngspice; fresh private output, prints its location
python3 -m unittest test_sim
```

| Chain | Result |
| --- | --- |
| Audio in (`J_AUDIO_IN_L` → ADC) | gain 0.1000 (−20.00 dB) as designed, −3 dB at **19.7 kHz** — two cascaded poles (100k ∥ 47pF, 10k ∥ 560pF), not the 28 kHz single-pole estimate |
| Audio out (DAC → `J_AUDIO_OUT_L`) | gain **3.300** (10.37 dB) after the R08 fix (R13/R14 feedback 33k), 3.2673 at the jack into 100k; −3 dB at **30.9 kHz** simulated with ideal opamps (30.0 kHz with the TL074 model) — the ×10 chain that clipped the ±11.7 V rails is gone |
| CV in (U13) | **0.3010 V/V, offset 1.6096 V**: ±5 V → **3.1146 / 0.1046 V**, no clipping anywhere in the sweep (R33/R42 30.1k, R34/R43 187k); worst 1 % corner 3.1775 / 0.0742 V, still 60 mV clear of the rails |
| CV out (U12) | **1.5121 V/V, load-independent**: 0…3.3 V DAC → 0…4.990 V at 100k, 50k, 10k and open, because the feedback pickup sits past the 1 k series resistor (R09) |
| −10 V reference (U14) | 0.809 mA cathode current at VEE = −12 V, worst case 0.445 mA over −11.2…−12.2 V, i.e. 3.6× the 125 µA design floor (4.45× the datasheet 100 µA typ) |
| Pitch error, 1 % vs 0.1 % resistors | 24 ¢/octave (CV in) and 48.5 ¢/octave worst case (CV out); 0.1 % parts give 2.4 / 4.8 ¢/octave plus fixed offsets |

None of these change the board: they confirm the design (audio gain, CV
scaling, reference bias) or document behaviour the firmware and the euro board
have to live with (the two audio roll-offs, the 1 % CV tolerance, the ±5 V CV
input limit). The full `make test` includes these simulations. Board-only `bun run gate`
checks the layout/electrical source; it does not itself run SPICE. Simulations
never write `dist/` or the tracked historical `tools/sim/out/` data.

### BOM fixes applied during conversion

The KiCad project still carries the old part numbers; the converter patches them
(`PART_OVERRIDES` in `tools/kicad2tscircuit.py`). Keep the KiCad port frozen;
record intentional canonical changes in these metadata and comparison rules.

| Ref | Old | New | Why |
| --- | --- | --- | --- |
| R23 | `C728283` (1 mΩ 1 W current sense) | `C17927` `1206W4F1004T5E` (1 MΩ ±1%, **basic**) | schematic value is `1M`, the old part was a library leftover |
| C6, C7 | `C55393` `CL10C561JB8NNNC` | `C107054` `CC0603JRNPO9BN561` (560 pF NP0 ±5% 50 V) | old part is **discontinued, 0 stock** |
| J2 | none | `C3406` (2×8 2.54 mm IDC box header) | no JLCPCB part number in the KiCad BOM |
| CN1, CN2 | `C225268` `A2005WV-16P` (2mm shoulder) | `C2883069` `X4612WV-16I-C40D28-E60` (6mm total shoulder + 4mm post) | mechanical review: the old header could not enter the 4.3mm socket across the stack; pads/XY/nets unchanged |
| CN4 | `C225259` `A2005WV-7P` | `C2883061` `X4612WV-08I-C40D28-E60`, **cut to seven ways** | same 6mm stack; the stocked seven-way member (C2883060) did not resolve in JLC's catalogue |
| CN3 | `C225260` `A2005WV-8P` | `C22438291` `HX PZ2.0-1x8P ZZ` | only 131 pcs in stock |
| H1 | `C60565` `2.54-2*6PWZ` | `C22373932` `HC-PM254-8.5H-2x6PW` | only 62 pcs in stock; body depth 8.5 mm vs the 12.5 mm drawn in the footprint, same 2.54 mm pad grid |
| U16 | `C5365649` `B1205S-1WR3` (12V in, 1 W/200 mA) | `C5369484` `B2405S-2WR3` (24V in, **2 W/400 mA**) | input sees VCC−VEE ≈ 23.4V, double the 13.2V max; and the worst-case 5 V load (194 mA without the H1 module, 267 mA with it) does not fit 200 mA — see [`doc/POWER-BUDGET.md`](doc/POWER-BUDGET.md). Different package, so U16 also has its own footprint/placement |
| R48 | `C4216` 33k pull-up | `C4190` 2.2k | Historical correction for the original TLP2362 open-collector output; R48 remains 2.2k with the replacement TLP2361 totem-pole inverter |
| R29 | `C427297` 100R | `C21190` 1k | 5V/100R = 50mA into a short exceeds the TL074 drive; 1k like the other outs |
| C16, C21 | 100nF (48Hz LPF) | `C57112` 10nF NP0 (**basic**) | 100nF across the 30.1k CV-in leg kills audio-rate FM; 10nF = **529Hz** (1/(2π·30.1k·10n), was 482Hz against the old 33k), same class as the Patch reference |

`bun run check:parts` now reports all JLCPCB parts as matching the schematic.

### Value changes from the design review

The review (`doc/DESIGN-REVIEW.md`) changed these existing refs. The KiCad
project is frozen, so the changes live in `PART_OVERRIDES` /
`INTENTIONAL_PIN_MOVES` / `REMOVED_REFS` in `tools/kicad2tscircuit.py` and in
the tscircuit source.

| Ref | Change | Why |
| --- | --- | --- |
| R13, R14 | 100k → **33k** (`C4216`), gain ×10 → **×3.300** | R08: a 1 Vrms full-scale Seed output through ×10 asked for 14.1 V peak and clipped at ±10.1 V against ±11.7 V rails; ×3.3 is 4.62 V peak |
| R33, R42 | 33k → **30.1k** (`C23000`) | R10: the 33k/200k summer mapped ±5 V onto exactly 0/3.3 V, so the MCP6002 clipped both endpoints |
| R34, R43 | 200k → **187k** (`C22896`) | R10: offset leg, moves CV in to 0.104…3.114 V with 60 mV of rail headroom at the 1 % corners |
| D2 | **removed** (`C2128` 1N4148WS) | R01: it was in parallel with the U15 opto LED in the forward direction, so MIDI IN could never work. D1 (antiparallel) stays |
| R49, R50 | **removed** (33 Ω 0603 `C23140`, 100 mW) | R02: a grounded ring dissipates 330 mW. Replaced by the 1 W 2512 `R59`/`R60` around the new `Q2` PNP driver (next table), which also makes the OUT a **Type A** transmitter |
| U11A/U11D | spare TL074 channels rewired as **unity-gain followers** | R03: the unbuffered DAC is a ~13 kΩ source; the followers keep the 33k summing resistors from costing 28 % of the range (firmware must disable the DAC buffer). U11 pins 3/12 and R31/R32 pin 1 moved nets |
| R39, R40 | feedback pickup moved **past** the 1 k series resistor, plus **C33/C34 100 pF** across them | R09: 1 k outside the loop made CV gain load-dependent (−11.8 ¢/octave from one to two 100k inputs); the gain is now a load-independent 1.5121. Moving it inside the loop put the cable capacitance there too, so 100 pF per channel compensates that pole (stable to ~10 nF) |

### BOM notes

- 40 unique design codes: 32 SMT codes across 107 placements and 8 hand-part
  codes across 10 placements. Basic/extended status and stock must be reviewed
  in the current JLCPCB quote; a cached catalogue result is not a stock plan.
- LCSC codes for hand-fitted parts are design references. Select exact reviewed
  parts through RS, Electrokit or DigiKey with EU sales; see [EU-SOURCING.md](doc/EU-SOURCING.md).
- CN1/CN2 use the 6 mm-shoulder reference and CN4 is prepared to 7 ways. The
  historical selection stock was 198 and 832 units respectively, not a current
  purchasing guarantee. Daisy sockets, shunts and fasteners remain unqualified.
- H1 has an 8.5 mm body. `W12.5` in its legacy footprint name is not its actual
  body depth. Cable/enclosure clearance still requires an assembled check.
- Daisy is `doNotPlace`; its separately fitted sockets are not in the generated BOM.

### Parts added in tscircuit (not yet in KiCad)

These exist only in the hand-maintained tscircuit design (`EXTRA_COMPONENTS`
in `tools/kicad2tscircuit.py`). Do not regenerate or edit the frozen KiCad
schematics. Netlist, extras, footprint, part and value checks verify the
intentional differences.

| Ref | Part | Why |
| --- | --- | --- |
| C28 | 100nF 0603 `C14663`, P3V3_D–GND at U15 | Local supply bypass for the TLP2361 receiver |
| C29 | 100nF 0603 `C14663`, P3V3_D–GND | P3V3_D had no decoupling (only P3V3_A has C19) |
| C30 | 10µF 1206 `C13585`, P3V3_D–GND | P3V3_D bulk for SD/OLED current spikes |
| C31 | 100nF 0603 `C14663`, P3V3_A–GND at U1 | R06: U1 (CD4051) had no local bypass — C19 sits 25.6 mm away at U13. Now 7.71 mm of routed rail |
| C32 | 1µF/50V 0603 `C559769`, `U16_VIN_P`–`U16_VIN_N` | R13: the B2405S datasheet asks for 1 µF/50 V **at the input pins** (module pins 1/2, 2.64 mm of routed rail); C26 sits before L1/L2 and is not that cap |
| C33, C34 | 100pF C0G 0603 `C14665`, across R39/R40 (CV out 2 / CV out 1) | R09 follow-up: the in-loop 1 kΩ plus cable capacitance rang (~18° phase margin at 1 nF); the 100 pF zero at 15.9 kHz makes 1 nF and 10 nF loads clean (`tools/sim` deck 5) |
| Q2 | AO3401A P-channel SOT-23 `C15127`, G1/S2/D3 | Low-loss MIDI OUT high-side stage; old PNP legacy-current deficit retained as a negative model |
| R58 | 180 Ω 0603 `C22828`, `USART1_TX` → Q2 gate | Limits modelled gate transient peak to 19.44 mA at the stated worst rail/tolerance; STM32 rating verification remains pending |
| R59 | 33 Ω **1 W 2512, 1 %** `C53728`, Q2 drain → ring | Type A current source, approximately 0.368 W at the local 3.465 V ring-short corner with zero switch loss |
| R60 | 22 Ω **1 W 2512, 1 %** `C118189`, tip → GND | Permanently grounded return; approximately 0.551 W at the local 3.465 V tip-fault corner; thermal qualification pending |
| R61 | 2.2k 0603 `C4190`, Q2 gate → `P3V3_D` | Gate-to-source bias; reset, idle leakage, edge behaviour and temperature still require qualification |
| D5 | 1N4148WS SOD-323 `C2128`, cathode on `NET_Q1_1` | R04: clamps Q1's base-emitter, so a −10 V sync patch can no longer exceed the 6 V rating |
| R51, R52 | 4.7k 0603 `C23162`, SCL/SDA to P3V3_D | I2C needs pull-ups (cf. Patch R77/R89/R91) |
| R53–R57 | 47k 0603 `C25819`, SD_DAT0–3 + SD_CMD to P3V3_D | SD needs pull-ups (cf. Patch R3–R7 47K) |

`R13`/`R14` (audio feedback, 33k) and `R33`/`R34`/`R42`/`R43` (CV-in summer)
also belong here in spirit: they are existing KiCad refs whose **values**
changed, so they are `PART_OVERRIDES` rather than new components — see the
table above.

### Deliberately not changed

- **CV scaling resistors stay 1%.** ~2 % of scale on CV in and ~4.04 % worst
  case on CV out (24 ¢ and 48.5 ¢/octave, simulated), plus fixed offsets. 0.1 %
  replacements exist but none are JLCPCB basic, so this trades assembly cost for
  firmware calibration. Calibrate pitch in firmware; the CV-in endpoints no
  longer need a guard band in software (they now sit 60 mV inside the rails).
- **M_ADC mux inputs have no clamps.** The CD4051 runs at 0–3.3V; anything above
  ~3.8V damages U1 and the Seed ADCs (pins 24/25/28–30 are 3.3V-only, not 5V
  tolerant). That is now a written interface requirement for the euro board —
  pots/attenuverters powered from `P3V3_A`/`GND` only, `M_ADC_*` inside 0–3.3 V
  in every power-up/down order: [`doc/FRONTBOARD-INTERFACE.md`](doc/FRONTBOARD-INTERFACE.md)
  (finding R14).
- **No encoder counterpart.** The Patch reference has an encoder; the base board
  has no encoder pins. CN2 pads 11–15 are unconnected — reserve three for
  ENC_A/ENC_B/CLICK when the euro board is designed, or consciously drop it.
- H1 footprint body is 8.5mm deep (silk x 2.8…11.3), matching the fitted
  `HC-PM254-8.5H` part — the `W12.5` in the footprint name is just a name.
  (An older note claimed a 12.5→8.5mm mismatch; remeasured, no mismatch.)
- Pre-order checklist: use M3 standoffs/washers of <=5mm OD. The old note here
  claimed a 5.5mm-OD standoff reaches 0.3mm into J2's courtyard — that is wrong:
  the J2 courtyard edge is **3.96mm** from the (−36.95, −60.75) hole centre, so
  a 5.5mm-OD standoff (r = 2.75mm) **clears it by 1.2mm**. The tight holes are
  the other two: **C25** comes within 2.95mm of the (36.95, −60.75) hole and
  **CN1** within 3.05mm of the (−36.95, 60.75) hole, so ≤5mm OD is the hardware
  limit there (a 5.5mm standoff would sit on C25's courtyard). Also: sanity-check
  the CPL rotation/mirror mapping for bottom-side parts with JLC (classic
  bottom-mirror trap — 101 of the 107 bottom placements are at 180° and 6 at
  0°, but verify); 6x C1671 47pF caps emit
  `source_part_not_found_warning` (no EasyEDA 3D-model match — the LCSC part
  itself validates fine, they just render pad-only in 3D); the ~90
  `source_component_availability_warning`s are build-time data gaps, not BOM
  problems (`check_parts.py` is the authority and passes); silkscreen refdes
  exist for every IC/module, connector, electrolytic and diode plus `FROEMIX`,
  `REV 0.1`, the date and the hand-solder legend (31 texts,
  `sections/silk.tsx`) — the 0603/1206 passives stay unlabelled, assemble them
  from the CPL.

## JLCPCB order

`bun run release` produces everything the JLCPCB order page needs, including
JLC-shaped assembly files (their SMT line takes a BOM and a CPL, not the
per-designator exports tscircuit writes natively):

| Board | Upload | File |
| --- | --- | --- |
| base | Gerbers | `fab/froemix-gerbers.zip` |
| base | BOM (reference) | `fab/froemix-bom-jlc.csv` - all 40 design codes, not the SMT upload |
| base | BOM (upload) | **`fab/froemix-bom-smt.csv`** - 32 SMT codes, 107 placements |
| base | BOM (hand reference) | **`fab/froemix-bom-hand.csv`** - 8 codes, 10 placements; not an approved EU shopping list |
| base | CPL | `fab/froemix-cpl-jlc.csv` (107 SMT placements) |
| euro | Gerbers | `fab/euro/euro-gerbers.zip` |
| euro | BOM (upload) | **`fab/euro/euro-bom-smt.csv`** - 5 unique parts, 12 placements |
| euro | BOM (hand) | **`fab/euro/euro-bom-hand.csv`** - 9 unique parts, 27 through-hole |
| euro | CPL | `fab/euro/euro-cpl-jlc.csv` (12 SMT placements) |
| euro panel | Gerbers | `fab/euro/panel/panel-gerbers.zip` (2 mm FR4, holes + silkscreen only) |

`make release` produces both sets in one run (`fab/` for the base board,
`fab/euro/` for the front board and panel).
| Reference | `fab/froemix-pnp-all.csv`, `fab/froemix-bom.csv`, SVGs, `froemix.step` |

Hand-mount instructions for everything JLC does not place (10 through-hole
board parts, two separately sourced 1×20 female Daisy socket strips, two `J1`
jumper shunts and the plug-in Daisy module): [`doc/ASSEMBLY.md`](doc/ASSEMBLY.md).
The sockets and shunts are not in the generated hand BOM; no socket part has
been selected or physically validated. The base-board finish is lead-free HASL.

Electrical contract for the front (euro/panel) board — connector pinout, CV and
audio levels, the `M_ADC` restriction and the 3.3 V budget:
[`doc/FRONTBOARD-INTERFACE.md`](doc/FRONTBOARD-INTERFACE.md).

Full guide, including THT hand soldering, single-sided bottom SMT assembly, the
bottom-rotation check and the order-day checklist:
[`doc/JLC-ASSEMBLY.md`](doc/JLC-ASSEMBLY.md) (also copied into `fab/`).

## Production readiness

**Production is blocked on 19 pending acceptance items.** The design has
automated source, layout, geometry and behavioural-simulation checks; it has
no first-article or manufacturing approval. [acceptance.md](doc/acceptance.md)
lists the evidence needed for every item.

- Prototype checks cover copper policy, silkscreen bounds, fiducials, test
  points, net/pad identity, bottom-only SMT, mechanical tolerances and source
  consistency. A passing result is not a fitted-hardware measurement.
- U16 is specified as the 2 W/400 mA B2405S-2WR3. The paper budget closes;
  the actual P5V <=300 mA, transients and thermal acceptance remain pending.
- Mux inputs remain an own-rail-only interface. Powered/unpowered and backfeed
  checks, CV calibration and the DAC-buffer-off firmware rule require bring-up.
- The TLP2361/PMOS MIDI correction has source-linked corner/negative tests.
  High-current operation, interoperability, reset/edges, fault temperature and
  primary STM32 GPIO-limit verification remain open.
- Owner-approved rail holes now sit at ±43.18 mm X, ±61.25 mm Y. All control
  centres remain unchanged. Actual rack-lip, adjacent-module and full-height
  PCB fit still require the intended rack and printed 1:1 template.
- The NAV cup is 5.6 mm. Its computed worst corner wall is about 0.275 mm and
  swept clearance about 0.305 mm; resin strength, true tilt, retention and
  MODE travel/preload are not proved by CAD.
- <=5 mm fastener envelopes are a geometry budget, not a selected screw.
  Ordinary 5.5 mm M3 heads are too tight to the budget and 7 mm washers collide.
- Daisy sockets, J1 shunts, OLED support, jack/pot threads, EU hand sourcing,
  the ground/chassis scheme, ESD, thermal and DFM/CPL all require evidence.
- No encoder is implemented; CN2 pins 11–15 remain unused. Do not invent firmware
  features or repurpose pins merely to improve a drawing.

## Design review

A circuit-level review of the ported design ([`doc/DESIGN-REVIEW.md`](doc/DESIGN-REVIEW.md),
baseline commit `89807b7`) found 14 findings that the netlist/routing gates could
not see: a MIDI opto LED shorted by D2 (R01), under-rated MIDI output resistors
and an unprotected MCU pin (R02), a CV-out design that assumed an ideal DAC
(R03), an unclamped sync input (R04), a wrong assembly guide (R05), a missing
U1 bypass and a report that credited the wrong pin (R06), silently truncated
RKM resistor values (R07), a ×10 audio stage that clipped full scale (R08),
load-dependent CV out (R09), CV in with no headroom (R10), electrolytic
courtyards that did not contain the part (R11), silkscreen exported off the
board (R12), a 5 V budget nobody had written (R13) and `M_ADC` inputs with no
protection contract (R14).

The historical fixes and follow-ups are recorded in DESIGN-REVIEW.md. This
review adds [HARDWARE-VERIFICATION.md](doc/HARDWARE-VERIFICATION.md), source-linked
simulations, the limited owner-approved rail-hole revision and fail-closed
production acceptance. A written budget or interface specification does not
close its physical bring-up item. The U16 design already specifies the 2 W
part; no instruction to keep the old 1 W converter applies.

## CI / regression

`make test` is the complete prototype regression command: typechecks, genuine
KiCad ERC, build, original net/pad/part comparisons, electrical identity and
margins, source/netlist/placement/short/style checks, silkscreen bounds, both
board/panel regression suites, source-linked SPICE and acceptance-validator
unit tests. Network is needed for catalogue checks. Native ngspice is required.

`FROEMIX_SKIP_ERC=1` is only an explicitly incomplete prototype diagnostic.
Production entry points reject it and check physical acceptance before any
build, export or deletion. Missing tools and malformed reports must never pass.

Snapshot checks build with the fixed reference date `2024-08-10`. An intended
update must rebuild with that date **before** refreshing snapshots; setting the
date only on `snapshot` cannot change an already-built date. Use `make snapshots`
after inspecting and explaining the actual change. Do not accept snapshots
merely to remove a failure. Current schematic positions are canonical TS data,
not edited screenshots; visible/strict overlap limitations are recorded in
[ELECTRICAL-VERIFICATION.md](doc/ELECTRICAL-VERIFICATION.md).

## Base-board layout (tscircuit)

The layout lives in the hand-maintained `pcbX`/`pcbY`/`layer` props in
`sections/*.tsx` (`LAYOUT_ZONES`/`side_of` in the retired generator are history).
TOP faces the euro (front) board, BOTTOM faces out the back. The euro board
closes over the top at header height, so the top holds ONLY the mating headers
plus low-profile SMD — every tall body hangs off the back into the rack cavity.
Floorplan on the 80.9 x 128.5mm board (center origin, 4 layers: signal / GND /
PWR / signal):

| Zone | Bounds (mm) | Side | Contents |
| --- | --- | --- | --- |
| conn | x ±34, y 52…61 | top | CN1/CN3/CN4 (mating headers, from above) |
| conn2 | x −4…30, y −44…−35 | top | CN2 (audio/CV jack bus) at (12.42, −36.36) — 96mm below CN1 for opposite-end support (lower third, not the edge; the 4 standoffs carry the load) |
| daisy | x ±27, y 30.5…50 | bottom | DaisySeed1 laid horizontal (2x20 headers left/right) |
| center | x ±20, y 17…29.5 | bottom | U1 mux, I2C/SD pull-ups (R51–R57), P3V3_D decoupling (C29/C30), C1 |
| audio | x −37…−16, y −34…16 | bottom | U2/U3 + all audio passives |
| cv | x 2…20, y −34…16 | bottom | U11/U12/U13/U14, Q1, D5 + all CV/sync passives, and C33/C34 at their feedback resistors R39 (17.78, −15.94) / R40 (3.74, −19.16) |
| midi | x 22…37, y −34…−13 | bottom | U15, J1 (top), D1, R47/R48, C28 (U15's bypass) |
| midi drv | x −37…−29, y 32…54 | bottom | the MIDI OUT driver, moved out of the midi island into the left/top strip beside the Seed: Q2 (−31, 41), R58 (−30.5, 36.5), R61 (−30.5, 33.5), R59/R60 1 W 2512 (−33, 52 / −33, 46) |
| h1 | x 22…37, y −12…16 | bottom | H1 (PMOD, opening faces the right edge) |
| pwr | x ±34, y −61.5…−42 | bottom SMD / bottom THT | rail capacitors, inductors and Schottkys moved to bottom; J2, U16 (2 W module at (8.5, −49.5), body overhang toward the bottom edge), bulk electrolytics on the back |

- 4× M3 mounting holes (3.2mm) 3.5mm from each corner; conn/pwr zones keep
  clear of them in x.
- Zone packing (`place_by_zones`) is an obstacle-aware shelf pack: pinned parts
  (`LAYOUT_PINNED`, e.g. J2 on the bottom edge) are placed first and their
  courtyard keepout is seeded into the packer, which slides cells past those
  boxes instead of packing on top of them. Once a zone's ICs are down, the
  primary decoupling slot of each `DECOUPLING` pair is reserved the same way, so
  the bypass caps land beside their IC instead of wherever the shelf ends up
  (the remaining pairs fall back to another candidate or to the packed
  position; see `pin_decoupling`).
- GND has a copper pour on the bottom: `index.circuit.tsx` declares
  `<copperpour connectsTo="net.GND" layer="bottom" clearance="0.3mm"
  boardEdgeMargin="0.5mm" />`. Built result: **5 pour regions** on the bottom,
  approximately **7383.1 mm²** of copper with **73 clearance holes**, 0.3mm to
  pads/traces and 0.5mm to the board edge — 71.0 % of the 80.9 × 128.5 mm board. The pour needs a local
  workaround for an upstream defaulting bug: without `holeOffsetX`/`holeOffsetY="0mm"` on the
  `circular_hole_with_rect_pad` plated holes, `@tscircuit/core` emits
  `hole_offset_x/y` as `undefined`, the pour connectivity pass builds NaN arc
  vertices and the build dies in `@flatten-js/core`
  (`node.item.key.low` undefined). Repro + root cause: `doc/pour-repro/`.
  The pour is applied after routing; GND is still routed explicitly.
- J2/H1 use pre-rotated footprint variants (`PRE_ROTATED_FOOTPRINTS`): tscircuit
  rotates courtyard *centers* but keeps their *axes*, skewing every rotated
  courtyard (H1's landed 13mm off, on top of U13). Variants bake the rotation
  in and place with rotation 0. All components have `pcbRotation` 0.
- Bottom footprints mirror about their origin (verified empirically); the packer
  mirrors the center offset for bottom parts, and `check_footprints.py` mirrors
  the expectation the same way.
- `tsci check placement`: Errors 0, Warnings 1 — `H1 is facing x- but should
  face x+`, a checker false positive. Evidence: H1's built courtyard bulk sits
  at +X of its centre (33.05 vs 28.5), the footprint body extends +X to within
  2.4mm of the right edge, and `from_x_pos` is declared — the mouth faces +X
  toward the edge. The heuristic derives facing from courtyard asymmetry and
  disagrees; physics (pads, body, part spec) all agree. (R36 rotate-180
  suggestion is informational — the part is symmetric, ignore.)
- Daisy USB access (decided): the Seed hangs off the back, USB reachable from
  behind — **no cutout needed** on the euro board. The earlier cutout idea is
  dropped.
- H1 PMOD hangs off the back with its opening facing the right edge. It links
  to an apfaudio eurorack-pmod (AK4619 4-in/4-out frontend, external 3HP
  module) over a straight-through 2x6 IDC ribbon — see H1 section below.
  J2 Eurorack power stays **left of center** (pinned at x −18.5, y −56) with the
  ribbon mating from below.
- The band between the power zone and the midi/h1 zones (y −45…−31, x 20…37)
  and the 2mm gap at x 20…22 stay empty as routing room.

## Routing (tscircuit autorouter)

The board uses tscircuit's built-in local autorouter (`autorouter="auto-local"`
— no cloud calls), one global phase over all 102 nets after phase-0-only and
split-phase attempts: frozen phase-0 GND walls blocked signal routing
(`immutable fixed route conflict`), so everything solves together instead.
Result: **306 traces, 330 vias, 0 unconnected, 0 overlaps**, `check shorts` and
`check pcb-style` clean, **4947.1mm** of copper (sum of segment lengths, verified
against `dist/index/circuit.json`). History: grid packing 446 vias → zone
packing 372 → 0.25mm width floor + 8 fabrication test points 293/323/4730 →
MIDI OUT rework 300/325/4931 (D2 and two 0603 parts out, Q2 + three resistors +
one diode in, and the CV-out/CV-in value changes do not move copper) →
Type A MIDI + C33/C34 **304/325/4934** (the MIDI OUT driver moved beside the
Seed) →
**304/334/4971** (U16 became the 2 W module: new footprint, longer body, new
placement, C32 moved next to the input leads) → **306/335/4978.7**
(MIDI OUT PNP correction) → **306/330/4948.2** (eight power SMT parts moved to
the bottom; TP1 and the frozen connectors remain in place). Board runs
default autorouter effort (a historical `5x` trial was measured identical —
kept as declared intent).
Placement is grouped by function (audio/CV/midi/power/daisy zones + affinity
pack order), which cut vias 446 → 372 versus grid packing; ~1.1 vias/trace is
healthy and the remaining long hauls (CN1-top to CN2-bottom spans) are
architectural. Moving the MIDI island next to CN1 was tried and reverted: a
10mm top-right column cannot fit U15's 9.1mm row + passives beside Daisy/H1.
`tracehint` (standalone and nested forms) was verified non-functional in the
installed tscircuit version — no hint elements reach circuit.json — so route
control is placement + phases + widths, plus manual `pcbPath` if a net ever
demands it. A bottom GND plane now exists (see the copper-pour entry above),
but it did not change the routing: the pour is generated after the router runs
and the router does not route into it, so GND is still 0.25–0.3mm traces with
the plane underneath; current numbers are 306/330/4947.1. GND carries 57 pins on
the KiCad netlist — the next biggest net has 14 — so GND is still the single
largest consumer of traces, and pour-aware routing (the pour's `unbroken` prop,
untested here) is the remaining via saver. Reproduce
with
`bunx tsci build --autorouter-debug --autorouter-debug-dir dist/autorouter-debug`.

JLCPCB 4-layer 1oz rules are enforced on the `<board>` (their minimums:
trace/space 0.09mm, via drill 0.15mm, annular ring 0.10mm): `defaultTraceWidth
0.25mm`, `minTraceWidth` 0.25mm (a deliberate margin above the 0.15mm signal
minimum — the router falls back to the board minimum whenever a net has no
nominal width, so a lower floor would let GND drop to the fab minimum),
`minTraceToPadEdgeClearance` 0.1mm, `minViaHoleDiameter
0.2mm`, `minViaPadDiameter 0.45mm` (0.125mm ring), `minBoardEdgeClearance
0.4mm`. Power nets carry `nominalTraceWidth` 0.3–0.6mm (see `POWER_TRACE_WIDTH`).
VEE is 0.4mm rather than 0.6mm: the U16 negative rail carries <100mA, and a
0.6mm VEE trace kept producing `PcbTraceStaircase` style issues where it threads
the power/analog channels.

External routers (e.g. Freerouting) are possible via `tsci export -f
specctra-dsn`, but there is no SES re-import into tscircuit — the round trip
would fork the design into KiCad or need a manual SES→`pcbTracePaths`
conversion. Not needed: the built-in router completes the board. Manual
`pcbPath` overrides remain available for sensitive nets if review demands them.

## H1 PMOD link (apfaudio eurorack-pmod)

H1 (`HC-PM254-8.5H-2x6PW`, right-angle 2x6 2.54mm) connects to an
[apfaudio eurorack-pmod](https://github.com/apfaudio/eurorack-pmod) (AK4619VN
4-in/4-out DC-coupled frontend, R3.3/R3.5) via a straight-through 2x6 IDC
ribbon to its `PinHeader_2x06_P2.54mm_Horizontal` PMOD. Both sides number the
standard PMOD way (odd pins one row, even the other, pin 1 square), so a
non-twist cable maps 1:1 — but meter it on first assembly (their README warns
some ribbons swap; align the pin-1 stripe).

| H1 pin | Our net (Daisy) | eurorack-pmod pin | Match |
| --- | --- | --- | --- |
| 1 | SDI (SAI2_SD_B) | SDI | ✓ |
| 2 | SCL (I2C1_SCL) | SCL | ✓ |
| 3 | SDO (SAI2_SD_A) | SDO | ✓ |
| 4 | SDA (I2C1_SDA) | SDA | ✓ |
| 5 | LRCK (SAI2_FS) | LRCK | ✓ |
| 6 | P3V3_D | PDN (R3.3) / ^MUTE (R3.5) | ⚠ always-on, see below |
| 7 | BICK (SAI2_SCK) | BICK | ✓ |
| 8 | MCLK (SAI2_MCLK) | MCLK | ✓ |
| 9–10 | GND | GND | ✓ |
| 11–12 | P3V3_D | 3V3 IN | ✓ |

- **Pin 6:** their PDN/^MUTE is a 3.3V logic input with HIGH = on/unmuted on
  both revisions, so our hard 3V3 means always-on — safe, mute control simply
  unavailable. All 40 Daisy pins are taken, so a future MUTE GPIO would need a
  sacrifice (e.g. the LED on ADC_1).
- **Power:** their 3V3 IN load (codec digital + LEDs + expanders, roughly
  50–150mA) rides on the Daisy's 3V3_D rail next to our OLED/SD/U15 load.
  Measure the rail in bring-up; if it sags, the euro board gets its own 3V3.
- **Clocking (firmware):** AK4619 MCLK must come from Daisy SAI2_MCLK at a
  multiple the codec accepts (e.g. 256fs); SAI1 stays on the Seed codec.
  I2C control rides the same I2C1 the R51/R52 pull-ups already serve.
- This also answers the old "mezzanine" question: no board-to-board mating,
  just a ribbon to an external module. H1 stays where the packer put it
  (proven DRC-clean); the cable drapes +X over the empty lower-right area.

## 3D view

The dev-server 3D tab and `bun run snapshot --3d` show real bodies: exact
EasyEDA/LCSC models per part, harvested once with `tsci import <C-code>` into
`tools/lcsc_models.json` and emitted as `cadModel` props by the generator.
29 of the 38 LCSC codes on the board have a manufacturer model (C1671 reuses
the identical 0603 body of C14663). Rotation offsets were corrected for the
rotation between EasyEDA's footprints and ours (our TSSOP/SOIC pad 1 is
top-left, EasyEDA's bottom-left).

Pad-only (no transferable stock model): J1/J2/H1 headers, the Daisy Seed (no
model in the registry or DaisyKiCad), C65114/C3406/H1 whose EasyEDA origin
offsets don't transfer, the 1 W 2512 MIDI resistors (C118189/C53728, a new
footprint), C559769 (the U16 input bypass), C14665 (the C33/C34 compensation
caps) and the two new 0603 resistor values (C23000,
C22896 — 0603 bodies, so they could reuse C14663's model like C1671 does). Link
real STEP models for those later.

Eyeball on first look: IC pin-1 dots (rotation math is derived, not measured),
the C25 electrolytic stripe (C24/C25 share one footprint with opposite
polarity — one stripe is necessarily wrong), and the removed TLP2362 dot (its pad
numbering is mirrored vs a standard SO-5). Bodies and pads are right; only the
ink may be off.

## Conversion notes

- Sheet structure, references, values, pin numbers, pin names and the netlist come
  from the KiCad project; pin labels are curated per IC (`lib/pins.ts`) so the
  TL074 reads `OUT1/IN1N/IN1P/...` instead of `1/2/3/...`.
- Footprints are the exact KiCad footprints, including the official Electrosmith
  `DAISY_SEED.kicad_mod` from `tmp/library/DaisyKiCad-main/Daisy-Boards.pretty`
  (pad-for-pad identical to the copy embedded in `froemix.kicad_pcb`). Pads keep
  their KiCad numbering, so the JLCPCB part, the footprint and the tscircuit board
  agree.
- Silkscreen outlines and courtyards are generated too; where a KiCad footprint
  has no courtyard, one is derived from pads/silkscreen + 0.25mm.
- KiCad schematic page coordinates are not reused (KiCad mm and tscircuit
  schematic units are not the same scale); tscircuit lays the sheets out itself.
  The KiCad PCB placement was not reused either: that board is mid-edit and every
  footprint sits outside its `Edge.Cuts` outline. The placement here was built up
  by hand (zones, then moves) and now holds the frozen connector coordinates.
- A TL074 is one 14-pin `<chip>`, not four op-amp triangles.
- Diode footprints number pad 1 as the cathode; `<diode pinLabels={{pin1:"cathode", pin2:"anode"}}>`
  keeps that mapping and the JLCPCB polarity check happy.
- Net names may only contain `[A-Za-z0-9_]`, so a few KiCad names are translated
  (original names are kept as comments in `sections/nets.tsx`):

  | KiCad | tscircuit |
  | --- | --- |
  | `+12V`, `+5V`, `+3V3_A`, `+3V3_D` | `P12V`, `P5V`, `P3V3_A`, `P3V3_D` |
  | `-12V`, `-10V_REF` | `N12V`, `N10V_REF` |
  | `Net-(U2A--)` | `U2A_IN_N` |
  | `Net-(C12-Pad2)` | `NET_C12_2` |

- Both designs now run `tsc --noEmit` in their gates. React list keys belong on
  fragments, not on geometry primitives; unsupported potentiometer props were
  removed without changing PCB geometry. Do not bypass type errors with broad
  suppressions or assume the tscircuit CLI typechecks the source.

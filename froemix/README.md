# Froemix

Daisy Seed based Eurorack module. The design is split over two boards:

| Board | Contents | Status |
| --- | --- | --- |
| **base board** (`tmp/`, `tscircuit/`) | Daisy Seed, CD4051 ADC multiplexer, Eurorack power entry (+/-12V -> VCC/VEE, +5V, -10V reference), audio in/out buffers, CV in attenuverters, CV out drivers, sync in/out, opto isolated MIDI, OLED/microSD/PMOD headers, board-to-board headers | converted to tscircuit |
| **eurorack board** (`euro/`, `doc/EURO-BOARD.md`) | 20HP front board: 10 mono jacks + 2 TRS MIDI jacks, 6 pots, 5-way UI joystick, illuminated MODE button, 6 jack activity LEDs, 1.3" OLED, and the panel | designed, routed, gated |

Inspiration: Electrosmith Daisy Patch (`~/Downloads/Patch-Design-Files-EAGLE`,
20HP, 19 jacks, SMT euro jacks + TRS MIDI).

The Daisy Seed module itself is already in hand, so it is marked `doNotPlace` and
kept out of the JLCPCB assembly.

## Repository layout

```
froemix/
├── tmp/                 original KiCad 10 project (frozen reference; layout lives in tscircuit/)
│   └── library/         jclpcb.pretty + official Electrosmith DaisyKiCad footprints
├── tools/
│   ├── kicad2tscircuit.py   KiCad -> tscircuit port baseline (retired as an editor)
│   ├── check_netlist.py     netlist vs KiCad (pin for pin)
│   ├── check_footprints.py  footprints vs KiCad (pad for pad)
│   ├── check_parts.py       JLCPCB parts vs schematic (type, value, package)
│   ├── report_decoupling.py cap -> IC routed-copper distances (informational)
│   ├── lcsc_models.json     EasyEDA 3D models per LCSC code (harvested once)
│   └── sim/                 four ngspice decks + RESULTS.md (gain, CV, ref bias)
├── euro/                the 20HP front board + panel (tscircuit project)
│   ├── index.circuit.tsx    jacks, pots, OLED, LED + the 3 sockets that mate CN1/CN2/CN4
│   ├── panel.circuit.tsx    the front panel (holes + silkscreen, no copper)
│   ├── lib/layout.ts        ONE table of coordinates, imported by board and panel
│   ├── lib/footprints.tsx   PJ398SM / PJ366ST jack, Alps RK09K, sockets, OLED, LED
│   └── scripts/             gate.sh + check_mating.py (mating contract, panel alignment)
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

## Eurorack (front) board

The 20HP board that carries every panel control, plus its panel:

```bash
cd euro
bun install            # once
bun run dev            # live preview
bun run gate           # build + checks + mating contract + snapshot diff
```

It mates with the base board through CN1 (MIDI, muxed pot inputs, UI controls,
LED, rails), CN2 (the jack bus) and CN4 (display), on 2.00mm sockets on its
bottom side, at the base board's own frozen coordinates. CN3 (micro SD) is
intentionally not mated yet. `euro/scripts/check_mating.py` verifies, from the
built circuit JSON, that every socket pin lands on the base board's pin, that
all 26 panel parts sit exactly above their panel holes, that no panel hole is
orphaned, and that nothing is left unrouted.

The UI costs no extra wires: the 5-way joystick and the illuminated button sit
on the two muxed pot channels `M_ADC_6`/`M_ADC_7` as resistor ladders (see
`doc/EURO-BOARD.md` §2 and §7 — the ladder levels are a firmware contract). The
six jack LEDs need no pins at all: each one is driven by its own jack signal
through a diode and a resistor.

Design record, floorplan and the open decisions (joystick sourcing, OLED
module, jack height, button part, CN3/SD, panel material):
[`doc/EURO-BOARD.md`](doc/EURO-BOARD.md).

### Faceplate design and fabrication

The panel-specific record is [doc/FRONT-PANEL.md](doc/FRONT-PANEL.md): material
comparison, two white-on-black design directions, JLCPCB order settings and a
first-article fit checklist. Recommended material is **2mm FR4 with standard
black solder mask and white print**, ordered separately from the electronic
boards, without assembly or a stencil. A Gerber check is not a physical-fit
approval; the button reach, knob hardware and rack clearance still need checks.

## Base board <-> eurorack board interface

`CN1`/`CN2` are 2.00mm 16-pin headers (`A2005WV-16P`); `CN2` is the jack bus,
`CN1` carries MIDI, the muxed ADC inputs and power.

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
`J1` MIDI 2x3 pin header, `J2` Eurorack 2x8 power.

## Verification

```bash
cd froemix/tscircuit
bun install

# `bun run regen` is RETIRED: it overwrites tscircuit/ with packer output.
bun run gate               # everything below + build + snapshot diff (CI entry point)
bun run check:erc          # KiCad ERC, 0 unexcluded errors/warnings
bun run check:netlist      # 105 nets must match the KiCad netlist
bun run check:footprints   # every pad: number, size, drill, position (410 pads)
bun run check:parts        # JLCPCB part numbers vs schematic (needs network)
bun run build              # tsci build -> dist/index/circuit.json
bun run check              # tsci check netlist (tscircuit source diagnostics)
bunx tsci check placement  # Errors 0, 1 known H1 warning
bunx tsci check shorts     # unintended copper shorts
bunx tsci check pcb-style  # odd-angle / staircase routing
bunx tsci snapshot --test --3d   # diff against __snapshots__/ (--update to accept)
bun run report:decoupling  # cap -> IC routed-copper distances
bun run release            # gate + fab export into ../fab
bun run dev                # interactive preview/editing
```

Current status: the gate is green (`bun run gate`, 10/10 steps). Netlist 105/105
nets, 410 pads match KiCad pad for pad, `tsci check netlist` Errors 0/Warnings 0,
placement Errors 0 (plus the known H1 false positive), shorts and pcb-style
clean, routing 293 traces / 323 vias / 4730mm copper / 0 unconnected / 0
overlaps, JLCPCB parts validated (110 components, 34 unique parts). `bun run
release` writes gerbers, BOM, pick-and-place, schematic/PCB SVGs and STEP into
`fab/`. What is still open is listed under [Production readiness](#production-readiness).

### Analog verification (SPICE)

Electrical checks that no layout tool can make: gain, bandwidth, CV scaling and
the reference bias current, simulated from the schematic values in
`tscircuit/sections/*.tsx`. Four standalone ngspice decks live in
`tools/sim/`, with the measured-vs-expected table and the findings in
[`tools/sim/RESULTS.md`](tools/sim/RESULTS.md):

```bash
cd froemix/tools/sim
bash run.sh            # needs ngspice (brew install ngspice); writes out/
```

| Chain | Result |
| --- | --- |
| Audio in (`J_AUDIO_IN_L` → ADC) | gain 0.1000 (−20.00 dB) as designed, but −3 dB at **19.7 kHz** — two cascaded poles (100k ∥ 47pF, 10k ∥ 560pF), not the 28 kHz single-pole estimate |
| Audio out (DAC → `J_AUDIO_OUT_L`) | gain 9.900 into a 100k load (+19.91 dB), −3 dB at **20.5 kHz** (two cascaded poles again; 34 kHz was the single-pole estimate) |
| CV in (U13) | ±5 V → 0.000/3.300 V exactly, but the endpoints sit **on** the 3.3 V rails: the real MCP6002 clips them to 0.030/3.270 V |
| CV out (U12) | 0…3.3 V → 0…4.940 V into 100k (4.990 V open) as designed; 1 % resistors put the worst case at 4.75…5.14 V |
| −10 V reference (U14) | 0.809 mA cathode current at VEE = −12 V, worst case 0.445 mA over −11.2…−12.2 V, i.e. 3.6× the 125 µA design floor (4.45× the datasheet 100 µA typ) |
| Pitch error, 1 % vs 0.1 % resistors | 24 ¢/octave (CV in) and 47–49 ¢/octave worst case (CV out), plus a fixed ±101 ¢ CV-in offset; 0.1 % parts cost 2.4 / 4.8 ¢/octave |

None of these change the board: they either confirm the design (audio gain,
CV-out scaling, reference bias) or document behaviour the firmware and the
euro board have to live with (the two audio roll-offs, the clipped CV-in
endpoints, the 1 % CV tolerance). The simulation is not part of `bun run gate`
and never touches `dist/`.

### BOM fixes applied during conversion

The KiCad project still carries the old part numbers; the converter patches them
(`PART_OVERRIDES` in `tools/kicad2tscircuit.py`). Move them into KiCad and delete
the entries when convenient.

| Ref | Old | New | Why |
| --- | --- | --- | --- |
| R23 | `C728283` (1 mΩ 1 W current sense) | `C17927` `1206W4F1004T5E` (1 MΩ ±1%, **basic**) | schematic value is `1M`, the old part was a library leftover |
| C6, C7 | `C55393` `CL10C561JB8NNNC` | `C107054` `CC0603JRNPO9BN561` (560 pF NP0 ±5% 50 V) | old part is **discontinued, 0 stock** |
| J2 | none | `C3406` (2×8 2.54 mm IDC box header) | no JLCPCB part number in the KiCad BOM |
| CN4 | `C225259` `A2005WV-7P` | `C22438290` `HX PZ2.0-1x7P ZZ` | only **9 pcs** in stock |
| CN3 | `C225260` `A2005WV-8P` | `C22438291` `HX PZ2.0-1x8P ZZ` | only 131 pcs in stock |
| H1 | `C60565` `2.54-2*6PWZ` | `C22373932` `HC-PM254-8.5H-2x6PW` | only 62 pcs in stock; body depth 8.5 mm vs the 12.5 mm drawn in the footprint, same 2.54 mm pad grid |
| U16 | `C5365649` `B1205S-1WR3` (12V in) | `C3031175` `B2405S-1WR3` (24V in) | input sees VCC−VEE ≈ 23.4V, double the 13.2V max; the Patch reference uses a 24V isolator in the same wiring |
| R48 | `C4216` 33k pull-up | `C4190` 2.2k | TLP2362 VO is open-collector; 33k is too slow for 31.25kbps MIDI |
| R29 | `C427297` 100R | `C21190` 1k | 5V/100R = 50mA into a short exceeds the TL074 drive; 1k like the other outs |
| C16, C21 | 100nF (48Hz LPF) | `C57112` 10nF NP0 (**basic**) | 100nF across 33k kills audio-rate FM; 10nF = 480Hz, same class as the Patch reference |

`bun run check:parts` now reports all JLCPCB parts as matching the schematic.

### BOM notes

- 34 unique parts: 17 JLCPCB **basic** (passives, diodes, pull-ups, the 10nF
  CV caps), 17 extended (the ICs and connectors). There is no basic replacement
  for the TL074, CD4051, MCP6002, LM4040, TLP2362 or B2405S, so the
  extended-part fee applies to those.
- `CN1`/`CN2` (`C225268` `A2005WV-16P`) still have ~715 pcs in stock - enough for a
  short run, and the 2.00 mm 1×16 alternatives had less (50-61 pcs).
- Every part on the board now has >2000 pcs in stock except `CN1`/`CN2`
  (`C225268` `A2005WV-16P`, ~715 pcs, two per board) - fine for a prototype run.
- The H1 swap changes the right-angle header body depth (12.5 -> 8.5 mm). Same
  pad grid, but check it against the PMOD cable/enclosure before ordering.
- `DaisySeed1` is `doNotPlace` (module already in hand); `J2` has no
  manufacturer part number, only the LCSC number.

### Parts added in tscircuit (not yet in KiCad)

These exist only in the generated project (`EXTRA_COMPONENTS` in
`tools/kicad2tscircuit.py`). Mirror them into the KiCad schematics and remove
the entries when convenient. The netlist/footprint checks know about them, so
`bun run check:all` stays green.

| Ref | Part | Why |
| --- | --- | --- |
| C28 | 100nF 0603 `C14663`, P3V3_D–GND at U15 | TLP2362 datasheet: 0.1µF bypass pin6–pin4 within 1cm, mandatory |
| C29 | 100nF 0603 `C14663`, P3V3_D–GND | P3V3_D had no decoupling (only P3V3_A has C19) |
| C30 | 10µF 1206 `C13585`, P3V3_D–GND | P3V3_D bulk for SD/OLED current spikes |
| R51, R52 | 4.7k 0603 `C23162`, SCL/SDA to P3V3_D | I2C needs pull-ups (cf. Patch R77/R89/R91) |
| R53–R57 | 47k 0603 `C25819`, SD_DAT0–3 + SD_CMD to P3V3_D | SD needs pull-ups (cf. Patch R3–R7 47K) |

### Deliberately not changed

- **CV scaling resistors stay 1%.** ~3% RSS gain error, ~4.1% worst case
  (34–49¢/octave) on
  CV in/out. 0.1% replacements exist but none are JLCPCB basic, so this trades
  assembly cost for firmware calibration. Calibrate pitch in firmware.
- **M_ADC mux inputs have no clamps.** The CD4051 runs at 0–3.3V; anything above
  ~3.8V damages U1 and the Seed ADCs (pins 24/25/28–30 are 3.3V-only, not 5V
  tolerant). Constraint for the euro board: pots/attenuverters must be hard
  limited to 0–3.3V, or add series-R + Schottky clamps on the base board.
- **No encoder counterpart.** The Patch reference has an encoder; the base board
  has no encoder pins. CN2 pads 11–15 are unconnected — reserve three for
  ENC_A/ENC_B/CLICK when the euro board is designed, or consciously drop it.
- H1 footprint body is 8.5mm deep (silk x 2.8…11.3), matching the fitted
  `HC-PM254-8.5H` part — the `W12.5` in the footprint name is just a name.
  (An older note claimed a 12.5→8.5mm mismatch; remeasured, no mismatch.)
- Pre-order checklist: use M3 standoffs/washers of <=5mm OD (a 5.5mm-OD
  standoff at (-36.95, -60.75) reaches 0.3mm into J2's courtyard); sanity-check
  the CPL rotation/mirror mapping for bottom-side parts with JLC (classic
  bottom-mirror trap — all rotations are 0, but verify); 6x C1671 47pF caps
  emit `source_part_not_found_warning` (no EasyEDA 3D-model match — the LCSC
  part itself validates fine, they just render pad-only in 3D); the ~90
  `source_component_availability_warning`s are build-time data gaps, not BOM
  problems (`check_parts.py` is the authority and passes); silkscreen refdes now
  exist for every IC/module, connector, electrolytic and diode plus TP1–TP8,
  `FROEMIX`, `REV 0.1` and the date (34 texts, `sections/silk.tsx`) — the
  0603/1206 passives stay unlabelled, assemble them from the CPL.

## JLCPCB order

`bun run release` produces everything the JLCPCB order page needs, including
JLC-shaped assembly files (their SMT line takes a BOM and a CPL, not the
per-designator exports tscircuit writes natively):

| Upload | File |
| --- | --- |
| Gerbers | `fab/froemix-gerbers.zip` |
| BOM | `fab/froemix-bom-jlc.csv` (one row per unique part, with quantity) |
| CPL | `fab/froemix-cpl-jlc.csv` (100 SMT placements; the 10 through-hole parts are excluded) |
| Reference | `fab/froemix-pnp-all.csv`, `fab/froemix-bom.csv`, SVGs, `froemix.step` |

Hand-mount instructions for everything JLC does not place (10 through-hole
parts + the Daisy module): [`doc/ASSEMBLY.md`](doc/ASSEMBLY.md).

Full guide, including the THT hand-solder choice, two-sided assembly, the
bottom-rotation check and the order-day checklist:
[`doc/JLC-ASSEMBLY.md`](doc/JLC-ASSEMBLY.md) (also copied into `fab/`).

## Production readiness

| State | Item |
| --- | --- |
| done | **Copper width policy** — board floor 0.25mm, nothing on the board is thinner (rails 0.3–0.6mm) |
| done | **Silkscreen** — 34 texts: IC/connector/electrolytic/diode refdes, TP1–TP8, `FROEMIX`, `REV 0.1`, date |
| done | **Fiducials** — 4 FID pads, bottom side in the corner keepouts: bare copper with a 0.5mm mask window and no paste (verified in `B_Cu`+`B_Mask`, absent from `B_Paste`). `<fiducial>` itself exports mask-covered pads in this version, so they are one-pad footprints instead |
| done | **Test points** — 8 bottom-side TP pads: P5V, P3V3_D, P3V3_A, VCC, VEE, N10V_REF, SYNC_IN, MULTIPLEX_ADC_1 |
| done | **Decoupling** — every IC pair ≤6.6mm of routed copper; C14/C15 4.5/2.6mm (were 13.2/18.6), bulk C1 9.0mm / C30 18.9mm |
| done | **Gates** — ERC, netlist, footprints, JLCPCB parts, build, tscircuit checks, snapshot diff behind `bun run gate` |
| done | **Fab export** — `bun run release` → gerbers, BOM, PnP, schematic/PCB SVG, STEP in `fab/` |
| done | **GND copper pour** — bottom-layer GND plane: 8 pour regions, main plane 79.9 × 127.5mm with 91 holes (7498mm²) + 7 islands = 7540mm² of copper, 0.3mm clearances, 0.5mm to the board edge. Needs `holeOffsetX/Y="0mm"` on rect-pad plated holes (upstream defaulting bug, `doc/pour-repro/`); routing unchanged at 293/323/4730 |
| open | **Single-point GND/chassis tie** — not implemented, no decision recorded (standoffs vs rack chassis) |
| open | **Mux input clamps** — `M_ADC_*` has no series-R/Schottky clamp; the euro board must stay within 0–3.3V |
| open | **CV scaling 1%** — ~3% RSS / ~4.1% worst-case gain error (34–49¢/oct, simulated), firmware calibration required; 0.1% parts are the alternative |
| open | **Encoder** — no encoder pins on the base board; CN2 pads 11–15 unused, decision deferred |
| open | **THT parts in the SMT order** — the 10 through-hole parts (CN1–CN4, H1, J1, J2, C24, C25, U16) are in `froemix-bom.csv` and `froemix-pnp.csv` with no hand-solder flag; decide JLC through-hole assembly vs hand soldering before ordering |
| open | **JLC second side / CPL mirror** — 97 of 110 placements are on the bottom, and the exported CPL mirrors them (90×180°, 6×0°, H1 270° because its footprint is a pre-rotated `@R90` variant; all 13 top parts 0°). Verify that against JLCPCB's bottom-side preview before ordering assembly |
| open | **DFM review of the exported gerbers** — nobody has opened `fab/froemix-gerbers.zip` in a fab viewer yet (mask expansion, annular ring, drills, panel) |

## CI / regression

`bun run gate` (`tscircuit/scripts/gate.sh`) is the whole regression contract and
needs no GUI: ERC → build → netlist → footprints → parts → `tsci check source` →
`tsci check pin_specification` → `tsci check netlist`
→ placement → shorts → pcb-style → `snapshot --test`. The build runs early on
purpose: the three KiCad comparisons read the BUILT `dist/index/circuit.json`, so
a clean checkout has nothing to compare before it. It takes ~40s warm and ~1min
from a clean tree, writes one log per step to `$TMPDIR/froemix-gate/`, and calls
no cloud service. Requirements: KiCad's CLI for the ERC step
(`FROEMIX_SKIP_ERC=1` skips it and prints that the run is incomplete) and
network for `check_parts`. ngspice is not used.

Regression baselines are the three files in `__snapshots__/` (schematic SVG, PCB
SVG, 3D PNG). `bunx tsci snapshot --test --3d` — what the gate runs — never
writes them and fails on any difference, leaving `*.diff.svg` / `*.diff.png`
next to the baseline for inspection. After an intentional design change, run
`bunx tsci snapshot --3d --update` and commit the new baselines; never refresh
them just to turn the gate green, explain why the image changed first.

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
| cv | x 2…20, y −34…16 | bottom | U11/U12/U13/U14, Q1 + all CV/sync passives |
| midi | x 22…37, y −34…−13 | bottom | U15, J1, D1/D2, R47–R50, C28 |
| h1 | x 22…37, y −12…16 | bottom | H1 (PMOD, opening faces the right edge) |
| pwr | x ±34, y −61.5…−45 | top SMD / bottom THT | rail decoupling, ferrites, Schottkys on top; J2, U16, bulk electrolytics on the back |

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
  boardEdgeMargin="0.5mm" />`. Built result: 8 pour regions on the bottom, the
  main one 79.9 × 127.5mm with 91 holes (7498mm²) plus 7 connected islands,
  7540mm² of copper in total. The pour needs a local workaround for an upstream
  defaulting bug: without `holeOffsetX`/`holeOffsetY="0mm"` on the
  `circular_hole_with_rect_pad` plated holes, `@tscircuit/core` emits
  `hole_offset_x/y` as `undefined`, the pour connectivity pass builds NaN arc
  vertices and the build dies in `@flatten-js/core`
  (`node.item.key.low` undefined). Repro + root cause: `doc/pour-repro/`.
  Routing is unaffected (293/323/4730, GND still routed explicitly).
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
— no cloud calls), one global phase over all 105 nets after phase-0-only and
split-phase attempts: frozen phase-0 GND walls blocked signal routing
(`immutable fixed route conflict`), so everything solves together instead.
Result: **293 traces, 323 vias, 0 unconnected, 0 overlaps**, `check shorts` and
`check pcb-style` clean, 4730mm of copper (sum of segment lengths, verified
against `dist/index/circuit.json`). The 285/291/4662 numbers this section used to
quote predate the 0.25mm width floor and the fabrication aids, which landed
together: the floor is +25% copper width (no extra length by itself) and the 8
test points add 8 connections, and the router re-solves from source — 293/323/4730
is the combined result, a deliberate trade, not a regression. History: grid
packing 446 vias → zone packing 372 → ZONE_SPACING + audio-block shift 291.
Board runs
`autorouterEffortLevel="5x"` (measured: identical output to default effort —
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
the plane underneath and the numbers stay 293/323/4730. GND carries 57 pins on
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
31 of 34 LCSC codes have their manufacturer model (C1671 reuses the identical
0603 body of C14663). Rotation offsets were corrected for the rotation between
EasyEDA's footprints and ours (our TSSOP/SOIC pad 1 is top-left, EasyEDA's
bottom-left).

Pad-only (no transferable stock model): J1/J2/H1 headers, the Daisy Seed (no
model in the registry or DaisyKiCad), C65114/C3406/H1 whose EasyEDA origin
offsets don't transfer. Link real STEP models for those later.

Eyeball on first look: IC pin-1 dots (rotation math is derived, not measured),
the C25 electrolytic stripe (C24/C25 share one footprint with opposite
polarity — one stripe is necessarily wrong), and the TLP2362 dot (its pad
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

- `bunx tsc --noEmit` is not wired up: tscircuit ships one dependency as raw
  TypeScript, so `tsc` reports errors inside `node_modules`. The tscircuit CLI
  does not typecheck; use `bun run check` and the scripts above.

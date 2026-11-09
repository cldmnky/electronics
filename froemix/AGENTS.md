# AGENTS.md — froemix base board (tscircuit-only workflow)

This repo was ported from KiCad to tscircuit. The port is DONE. `tmp/` (KiCad
project) is a frozen reference; only the ERC exclusions in `tmp/froemix.kicad_pro`
are maintained (see §2). All design work happens in
`tscircuit/`. This file is the contract for anyone (human or agent) touching it.

## 1. Source of truth

- `tscircuit/index.circuit.tsx` + `tscircuit/sections/*.tsx` + `tscircuit/lib/*`
  **is the design**. Edit these files directly.
- `tools/kicad2tscircuit.py` is RETIRED as an editor. It remains as (a) the port
  baseline, (b) the parsing library the check scripts import (net names, pin
  labels, footprint/3D-model maps, `EXTRA_COMPONENTS`, `PART_OVERRIDES`). Never
  run plain `bun run regen` — it overwrites `tscircuit/` with packer output.
- `bun run regen:check` (from `tscircuit/`) regenerates to a temp dir and diffs.
  Since the port the tree has diverged on purpose, so a clean diff shows the
  placement moves (`pcbX/pcbY`) PLUS these known, expected deltas — anything
  outside this list means someone edited something unexpected:
  - `sections/silk.tsx` and `sections/fabrication.tsx` (no generator counterpart)
    and their import/render lines in `index.circuit.tsx`
  - board width props (`defaultTraceWidth`/`minTraceWidth` 0.25mm) and the
    `nets.tsx` width-policy comment
  - the hand-maintained header comment on the eight ported section files
  - **the parts the review added/removed**: `REMOVED_REFS` (D2, R49, R50) and the
    `EXTRA_COMPONENTS` blocks (C28–C34, Q2, R58–R61, D5, R51–R57); the generator
    has no counterpart for them, so they show up as missing/extra components in
    the diff
  - **the six `INTENTIONAL_PIN_MOVES`** — U11.3, U11.12, R31.1, R32.1, R39.2,
    R40.2 — i.e. six net-connection lines that differ from the port
  - **U16's 2 W land pattern** — the port emits the frozen KiCad
    `jclpcb:PWRM-TH_4P-L11.5-W6.0-P2.54_B1503S` footprint; the tree carries the
    authored `FP_jclpcb_PWRM_TH_4P_L19_65_W7_05_P2_54_B2405S_2W` and the
    module pin map `B2405S_2W_PINS` (pins 1/2/4/6, `PIN_LABELS` in the
    generator updated to match), so the footprint prop and the pins.ts const
    name differ from the generated ones
  - curated schematic layout: five sheet notes, supported symbol rotations,
    non-subcircuit `<group schRelative>` layouts, Root positions and
    `lib/schematic-layout.ts` Audio/CV coordinates; these never move PCB parts
  - MIDI: U15 TLP2361 `C107626` (same 11-4L1S physical map), Q2 AO3401A
    `C15127` G1/S2/D3, R58 180 Ω `C22828`, R61 2.2k `C4190`, R59 33 Ω
    `C53728` and R60 22 Ω `C118189` (2512 1 %/1 W); metadata must agree
  - `lib/pins.ts` (curated pin labels) and `scripts/` (`gate.sh`/`release.sh`
    have no generator counterpart; `regen:check` reports the whole directory as
    "Only in .")
  Widths, refs and nets are NO LONGER a red flag on their own — we own them.
  Compare against the port baseline (`git log`), not against an expectation of
  identity.
- tscircuit skill (syntax, CLI, workflow): `.opencode/skills/tscircuit/`
  (not vendored in git — install with `npx skills add tscircuit/skill` or
  `git clone https://github.com/tscircuit/skill`). Read `SKILL.md` +
  `WORKFLOW.md` before any board work. Do not invent JSX props or CLI flags
  (`tsci <cmd> --help` when unsure).

## 2. Everyday loop

```bash
cd froemix/tscircuit
bun install                    # once
bun run dev                    # local web app, http://localhost:3020 (live reload)
bun run typecheck              # tsc --noEmit, including supported JSX props
bun run gate                   # prototype headless gate, see below (CI entry point)
bunx tsci build --autorouter-debug --autorouter-debug-dir dist/autorouter-debug
bun run check:erc              # genuine native KiCad: 0 unexcluded errors/warnings
                               # exact existing exclusions only, see below
bun run check:netlist          # KiCad netlist vs built, must MATCH (102 nets)
bun run check:extras           # tscircuit-only parts: pins, removed refs, pin moves
bun run check:footprints       # pads vs KiCad, must MATCH (376 KiCad + 53 added
                               # = 429 pads)
bun run check:parts            # JLCPCB parts vs schematic (needs network)
bun run check:values           # value literals vs built values and MPNs
bun run check:silk             # B/F silkscreen inside the board outline (gerbers)
bunx tsci check netlist        # Errors 0, Warnings 0
bunx tsci check placement      # Errors 0, Warnings 1 (known H1 warning, §4 — the
                               # CLI exits 1 on it, so trust `bun run gate`)
bunx tsci check shorts         # clean
bunx tsci check pcb-style      # clean
cd .. && make snapshots && cd tscircuit  # dated BUILD then snapshot refresh; inspect diff
bunx tsci snapshot --test --3d     # diff against them (what the gate runs)
bun run report:decoupling      # cap->IC routed-copper distances (informational)
bash ../tools/sim/run.sh       # 11 asserted/source-linked native-ngspice decks
cd .. && make test && cd tscircuit  # complete prototype regression, including SPICE
bun run release                # BLOCKED until physical acceptance; then gate + fab exports
```

### The gate (`bun run gate` -> `scripts/gate.sh`)

One prototype pass/fail contract, safe in CI. It does not accept physical hardware:

- TypeScript (`tsc --noEmit`) before the build; never invent JSX props.
- Genuine KiCad ERC: 0 unexcluded errors/warnings. `tools/kicad_cli.py` resolves
  an explicit path/`KICAD_CLI`, PATH, then the macOS bundle. Missing executables
  or genuine symbol/footprint libraries fail clearly. The checker resolves
  library tables in a private project copy, never by editing frozen schematics.
  It requests `--severity-error --severity-warning --severity-exclusions` and
  validates report structure, all counts and subprocess status. Exclusions-only
  output is NOT proof of zero ordinary findings. Native KiCad 10 currently
  reports 0 unexcluded and 9 excluded; only the precise preapproved Daisy ADC
  pin identities can be maintained in `tmp/froemix.kicad_pro`. New findings fail.
- Build, then the 102-net / 429-pad comparisons, declared extras, 40 unique
  JLCPCB design codes (network), passive values and electrical pin/margin checks.
- Fresh native schematic SVG: nonempty five-sheet structure, label geometry,
  page bounds and note bounds. The explicit 12 % bbox-area collision policy
  is not zero-intersection proof; strict results and limitations stay visible.
- Source/pin specification/netlist, placement (only the exact known H1 warning),
  shorts, PCB style, real-gerber silkscreen bounds and schematic/PCB/3D snapshots.

Per-step logs land in `$TMPDIR/froemix-gate/`. Runtime depends on cache, network
and tool startup; do not promise an old 60-second timing. Native subprocess and
parsed-report failures must propagate. `FROEMIX_SKIP_ERC=1` is only an explicitly
incomplete prototype diagnostic; production refuses it. Both design gates
typecheck. `make test` also runs euro/panel/mating/mechanical/unit/SPICE checks.

### Analog verification (`tools/sim/`)

`make sim` runs 11 behavioural native-ngspice decks without a PCB build. They
cover audio input/output, CV input/output/step load, reference bias, sync, mux,
front controls and MIDI. `source_link.py` verifies canonical TSX values (nominal
precision, not a physical tolerance), pin paths and nets. `models.py` records
model limits, provenance and assumptions; `assertions.py`/`run_checks.py` require
fresh, finite, correctly shaped results. Source/deck/model drift and missing,
NaN, ragged or stale outputs must fail. The unit suite includes negative cases.

Each run uses a unique private child directory. Never delete a caller workdir,
read historical `tools/sim/out/` as a result, or overwrite that tracked history.
The runner prints its output directory. `make test` includes the simulations;
board-only `bun run gate` does not itself run SPICE. Native ngspice is required;
there is no implemented WASM fallback for this acceptance runner.

Current model results and exact commands are in `tools/sim/RESULTS.md`. The two
audio chains have TWO cascaded poles: the old single-pole 28/34 kHz claims are
wrong. CV calibration, <=10 nF cable/load stability, semiconductor behaviour,
MIDI high-current/thermal/edges and real power sequencing require bench evidence.
The H750 20 mA/VOL/VOH/reset bounds are explicit unverified assumptions, not
primary-datasheet guarantees. The existing `midi_output` acceptance item requires
primary ST datasheet verification in addition to bench work.

### Release (`bun run release` -> `scripts/release.sh`)

First validates the fingerprint-bound physical record in
`hardware-acceptance.json`. Missing, pending, stale or unsafe evidence stops
BEFORE builds, exports or deleting output directories. No override is supported;
all 19 items currently remain pending. The root, base, euro and direct panel
production entry points enforce this; `FROEMIX_SKIP_ERC=1` is rejected.
Only after acceptance does it run the gate and export into `froemix/fab/`:
`froemix-gerbers.zip` (13 fab files: 4-layer gerbers + drill), `froemix-bom.csv`,
`froemix-pnp.csv`, `froemix-schematic.svg`, `froemix-pcb.svg`, `froemix.step`.
`bom` and `pnp` are NOT `tsci export` formats (the CLI rejects them): the
`gerbers` export writes `bom.csv` + `pick_and_place.csv` (JLCPCB conventions)
inside its zip, and `release.sh` extracts those two into `fab/`. No git
operations, nothing outside `fab/` is deleted.

Measure routing with: unconnected ports (`pcb_port_not_connected_error`) must be
0; then minimize vias, then total copper mm (sum `pcb_trace` segment lengths in
`dist/index/circuit.json`). Reference: 306 traces / 330 vias / 4947.1mm / 0
unconnected / 0 overlaps. The autorouter is deterministic — identical trees
rebuild byte-identical numbers.

The 285/291/4662 baseline this section used to quote predates the 0.25mm width
floor (§3) and the fabrication aids, which landed together: 0.2 -> 0.25mm is
+25% copper *width* (no extra length by itself) and the 8 test points add 8
connections to existing nets, and the router re-solves everything from source,
so 293/323/4730 was the combined result of both (they were not measured
separately). It is a trade, not a regression: 0 unconnected, 0 overlaps, shorts
and pcb-style clean. The 4 fiducials carry no nets and change no routing. The
300/325/4931 revision added the Design-review MIDI OUT rework (D2 and R49/R50
out; Q2, R58, R59/R60, D5, C31, C32 in), 304/325/4934 added the Type A MIDI OUT
move (driver beside the Seed, 22 Ω source) plus C33/C34, **304/334/4971** is
the U16 2 W swap (new 19.65mm-long footprint, new placement, C32 moved to the
input leads), **306/335/4978.7** is the MIDI OUT polarity fix;
**306/330/4948.2** moved eight power SMT parts to the bottom without moving TP1.
The TLP2361/PMOS revision is **306/330/4947.1**, with all frozen centres intact.
All were measured the same way.

Not gated (deliberately): KiCad PCB DRC — `tmp/froemix.kicad_pcb` remains
a frozen mid-edit reference; the layout lives in tscircuit. `tracehint` is
ignored by the pinned core; route control remains placement/phases/widths.

Schematic placement is now supported through `<group schRelative>` without
`schSectionName`; direct positions inside an auto section are overridden.
The groups are NOT subcircuits, do not move PCB coordinates, and retain global
nets. Canonical Root/Audio/CV layouts materially reduce collisions. The SVG
checker reports its 12 % policy and a strict zero-threshold inventory; native
unreproduced box-pair/style findings and remaining hairline contacts are
recorded in `doc/ELECTRICAL-VERIFICATION.md`, not silently called clean.

## 3. Hard rules (placement, values, silkscreen, netlist)

- **Frozen connectors** (mechanical contract with the euro board — NEVER move):
  `CN1 (-17.58,59.64,top)`, `CN2 (12.42,-36.36,top)`, `CN3 (8.11,59.63,top)`,
  `CN4 (24.47,59.63,top)`, `H1 (28.5,20,bottom)`, `J1 (26.16,-15.89,top)`,
  `J2 (-18.5,-56,bottom)`. These are historical built bounding-box centres:
  **CN3/CN4 footprint origins (`pcbX`) are 8.14 / 24.50**, respectively; wider
  pin-1 pads shift the bbox by −0.03mm. Do not move a matching socket to the bbox
  coordinate. All 39 mated pads are now checked against both built boards.
  CN1/CN2 are at opposite board ends, but **both are on top**.
- **Panel geometry exception, already owner-approved:** only the four rail
  holes moved from X ±43.30 to ±43.18 mm, at unchanged Y ±61.25 mm. The 86.36 mm
  separation is 17 × 5.08 mm. There are 27 circular holes plus the OLED window;
  all other 23 holes, control centres, diameters and 101.6 mm width stay fixed.
  Do not restore removed audio-LED holes. Rack/adjacent-module fit is pending.
- **Sides are load-bearing.** TOP = mating headers and J1, hand-fitted THT.
  All 107 SMT placements are on BOTTOM (enforced by the single-side check);
  all tall bodies (Daisy sockets, C24/C25, U16, J2, H1) also hang off the
  BOTTOM into the rack cavity. The euro board closes over the top at header
  height — any tall body on top blocks mating. Keep it that way.
  **Corrected mechanical stack:** CN1/CN2 are C2883069 XKB E60 headers (6mm
  TOTAL shoulder + 4mm post), CN4 is C2883061 eight-way **cut to seven ways**;
  front sockets stay 4.3mm. Use **11mm clear spacers**, not 12mm. J1+shunts
  ≤9.5mm, front-board rear tails ≤1mm; set J1 before stacking. Source dimensions,
  printable MODE/NAV actuators and physical acceptance: `doc/MECHANICAL-STACK.md`.
  These are assembly dimensions, not invented tscircuit component-Z props.
- **No `pcbRotation` on any component** (rotation is baked into pre-rotated
  footprint variants, see §5 — tscircuit rotates courtyard *centers* but keeps
  their *axes*, skewing every rotated courtyard).
- **Decoupling pairs move together** (cap follows its IC by the same delta):
  C31-U1, C4/C5-U2, C10/C11-U3, C14/C15-U11, C17/C18-U12, C19-U13, C20-U14,
  C28-U15, C32-U16, C1/C29/C30-Daisy pins 39/38/38. Distances are the *routed
  copper path* (re-measure with `bun run report:decoupling`, which reads
  `dist/index/circuit.json`; centre-to-centre numbers are ~1.5mm smaller and
  hide detours). **Rail leg / GND leg**, in mm, measured on the current tree:

  | IC | cap | rail leg | GND leg (return pin) |
  | --- | --- | --- | --- |
  | U1 (CD4051, P3V3_A) | C31 | 7.71 | 12.98 (p7 VEE) |
  | U2 | C4 VCC / C5 VEE | 8.98 / 7.72 | - (return is VMINUS on VEE) |
  | U3 | C10 VCC / C11 VEE | 7.94 / 7.56 | - |
  | U11 | C14 VCC / C15 VEE | 7.87 / 2.89 | - |
  | U12 | C17 VCC / C18 VEE | 9.45 / 7.68 | - |
  | U13 (MCP6002, P3V3_A) | C19 | 3.81 | 36.19 (p4 VSS) |
  | U14 (LM4040, N10V_REF) | C20 | 6.61 | 10.67 (p1 CATHODE) |
  | U15 (TLP2361, P3V3_D) | C28 | 5.46 | 3.45 (p4 GND) |
  | DaisySeed1 | C29 / C30 (P3V3_D) | 4.70 / 19.09 | 3.60 / 29.80 (p40 DGND) |
  | DaisySeed1 | C1 (P5V) | 11.80 | 9.03 (p40 DGND) |
  | U16 (B2405S-2WR3, 2 W) | C32 (U16_VIN_P) | 2.64 | n/a (isolated) |

  The GND leg is measured to the IC's **supply-return pin** (a pin named
  GND/VSS/DGND/VMINUS/VEE), not to any pin that happens to sit on the GND net:
  the TL074 non-inverting inputs and U13's INA_P are grounded but are not the
  return, and crediting them made the report quote U1 9.77 / U13 27.92. A part
  whose return pin sits on **another rail** now prints `-` and no fallback is
  attempted: the TL074s return through VMINUS (VEE), so their only pins on GND
  are grounded non-inverting inputs and the old report quoted a fake 4.7mm
  "loop" (IN1P). The fallback to the nearest pad on GND stays only for a part
  with no named return pin at all (U14, a shunt reference).
  Keep the IC rail legs near 10mm and U13's 36.19mm GND leg in mind: it is
  the C19 return, not the rail — the rail leg (3.81mm) is what the bypass is
  for, and GND is additionally under the pour (not in the graph). U2's C4 VCC
  leg is 8.98mm; bulk C1 11.80mm and C30 19.09mm sit in the SD/OLED corner — acceptable for
  bulk, the first thing to improve if placement is ever reworked.
- **Courtyards are keepouts**: no overlaps (Errors 0 gate), M3 holes at
  (±36.95,±60.75) stay clear, use ≤5mm-OD standoff hardware. The 5.5mm-OD claim
  used to be justified with J2; that was wrong — **J2's courtyard edge is
  3.96mm from the (−36.95,−60.75) hole centre**, so a 5.5mm standoff clears it by
  1.2mm. The hardware limit comes from the other two holes: **C25** is 2.95mm
  from the (36.95,−60.75) hole and **CN1** 3.05mm from (−36.95,60.75), so
  ≤5mm OD (r = 2.5mm, 0.45mm clear). Board edge clearance ≥0.4mm.
  `mechanics_check.py` derives all 16 board-face envelopes from both built
  boards; its generated report, not a nominal radius alone, is authoritative.
  A 5.5mm M3 head leaves only 0.20mm at C25; a 7mm washer overlaps by 0.55mm.
  Neither is approved. Exact <=5mm hardware and material strength remain pending.
- **JLCPCB 4-layer/1oz rules on `<board>` are floors, not targets** (their
  minimums: trace/space 0.09mm, drill 0.15mm, ring 0.10mm):
  `defaultTraceWidth 0.25mm`, `minTraceWidth 0.25mm`,
  `minTraceToPadEdgeClearance 0.1mm`, `minViaHoleDiameter 0.2mm`,
  `minViaPadDiameter 0.45mm`, `minBoardEdgeClearance 0.4mm`,
  `autorouter="auto-local"`, single global phase, layers 4. The 0.25mm floor is
  a deliberate margin, not the fab limit: the router only honours a per-net
  `nominalTraceWidth` and otherwise falls back to the board minimum, so a 0.15mm
  floor would let GND/signal copper shrink to the fab minimum. No copper on the
  board is thinner than 0.25mm — keep it that way. Power rails use
  `nominalTraceWidth` 0.3–0.6mm (VEE 0.4, not 0.6 — kills a staircase warning).
- **One `<copperpour>`, bottom GND plane** — `index.circuit.tsx` declares
  `<copperpour connectsTo="net.GND" layer="bottom" clearance="0.3mm"
  boardEdgeMargin="0.5mm" />`. 0.3mm to pads/traces/cutouts, 0.5mm to the board
  edge (the board minimum is 0.4mm — the pour margin defaults to `clearance`,
  i.e. 0.3mm, which fails the build's copper-to-board-edge DRC). Built result:
  **5 `pcb_copper_pour` regions** on the bottom, approximately 7383.1 mm² of
  copper with 73 clearance holes, 71.0 % of the 80.9 × 128.5 mm board. The
  pour is generated **after** routing and the router does not route into it, so
  the current routing is 306/330/4947.1 and GND is still routed
  explicitly; making the pour carry GND current means pour-aware routing
  (`unbroken`), which is untested.
- **Pours need `holeOffsetX`/`holeOffsetY` on every `circular_hole_with_rect_pad`
  pad** (§5). Without them core leaves `hole_offset_x/y` undefined, the copper
  pour connectivity pass in `@tscircuit/core` dies in `@flatten-js/core`
  (`TypeError: undefined is not an object (evaluating 'node.item.key.low')`) and
  the build is fatal. Min repro + root cause: `doc/pour-repro/`. This is a
  local workaround for an upstream defaulting bug — do not "clean up" the props.
- **No RKM-in-the-middle value literals.** The installed value parser silently
drops the digits after the multiplier, so `4K7` builds as **4 kΩ** and `49K9` as
**49 kΩ** — the BOM comment, the schematic and any analysis then disagree with
the fitted part. Write `4.7k`, `49.9k`, `30.1k`, `187k`. `check:values` compares every passive literal against the built numeric value *and* the MPN,
so a regression fails the gate instead of surviving to the order.
- **Every bottom-side silkscreen text uses `anchorAlignment="center"`.** The
renderer/exporter mirrors bottom text about its anchor, so `center_left` runs the
glyphs in −X and off the board (that is exactly how `REV 0.1` and the date
reached x = −47.5 mm on a board that ends at −40.45 mm, DESIGN-REVIEW R12).
`center` mirrors onto itself. Keep every anchor ≥1.5 mm inside the edge.
- **Silkscreen must pass `python3 ../tools/check_silk.py`**. It
exports the gerbers and compares B/F `SilkScreen` coordinates against
`Edge_Cuts` inset by 0.05 mm — real fab output, not the SVG preview. The PCB
snapshot draws the clipped result and cannot catch this.
- **A deliberate netlist delta goes through `INTENTIONAL_PIN_MOVES`,
`SYMBOL_PIN_RENUMBER`, `REMOVED_REFS` or `EXTRA_COMPONENTS`** in
`tools/kicad2tscircuit.py`, with the
review item in the comment (`# ... DESIGN-REVIEW R09`) and the reason spelled
out in `doc/DESIGN-REVIEW.md`. The KiCad project is frozen, so the check scripts
apply those sets to the KiCad side before comparing: adding a part,
moving a pin, renumbering a pin or deleting a ref any other way makes
`check:netlist` fail, and
silencing it by loosening the comparison is not an option. Current sets:
`REMOVED_REFS` = D2, R49, R50; `EXTRA_COMPONENTS` = C31, C32, C33, C34, Q2, R58,
R59, R60, R61, D5 (+ the older C28–C30, R51–R57); `INTENTIONAL_PIN_MOVES` =
U11.3/U11.12, R31.1, R32.1, R39.2, R40.2; `SYMBOL_PIN_RENUMBER` = U16
1↔2 (the input pair swaps numbers because the module's pin 1 is +Vin) and
3→4, 4→6 (the output pair, the 2 W module's own pin numbers —
`doc/POWER-BUDGET.md` §5).
`check:extras` asserts
the first three sets against the built circuit, because `check:netlist` strips
them before comparing and would otherwise never look at those pins;
`SYMBOL_PIN_RENUMBER` needs no extra assertion (the nets are unchanged, only
the numbers) and `check:netlist` prints how many renumbers it applied.
- **Firmware contract: the STM32H750 DAC output buffer must be DISABLED.** In
libDaisy, `DacHandle::Config buff_state = DISABLED`, i.e. the DAC channel
drives 0…VDDA. U11A/U11D are unity-gain followers on the two DAC outputs for
exactly this reason: the unbuffered DAC is a 10.3–16 kΩ source and would lose
28 % of the CV range into the 33 kΩ summing resistors. With the buffer enabled
the guaranteed DAC range is only 0.2 V…VDDA−0.2 V, so CV out could not reach 0 V
or full scale and no calibration can fix it. Details:
`doc/FRONTBOARD-INTERFACE.md` §4; simulation: `tools/sim/RESULTS.md`.
- **`C33`/`C34` 100 pF across R39/R40 are load-compensation, not tone control.**
R09 moved the CV-out feedback past the 1 kΩ series resistor, which made the 1 kΩ
and anything patched onto the jack part of the TL074 loop; 1 nF then rang (54 %
overshoot, ~18° phase margin) and 10 nF was worse. The 100 pF gives a 15.9 kHz
zero, which cancels the worst 1 kΩ/10 nF pole: **the CV-out spec is ≤10 nF of
cable/load capacitance** — 1 nF and 10 nF steps are clean (0 % overshoot, 48 µs
settle), 100 nF rings on full-scale steps (~50 % overshoot, 230 µs). The same
pole caps CV-out bandwidth at ~16 kHz, far above any CV signal. Deck 5 in
`tools/sim` (`cv_out_step_small/full.dat`) is the proof — do not delete the caps
and do not raise them (a bigger cap slows every CV edge).
- **The front board's `M_ADC_*` restriction is a hard interface rule, not a
note.** Pots/attenuverters from this board's own `P3V3_A`/`GND`, 0–3.3 V in
every power order; otherwise U1 (CD4051) and the Seed ADC pins (24/25/28–30) can
be damaged. `doc/FRONTBOARD-INTERFACE.md` §1 (R14).

## 4. Known warnings (do not chase)

- `H1 is facing x- but should face x+`: false positive. Proven from built
  geometry (courtyard bulk +X of centre, 2.4mm from right edge) + footprint
  anatomy (8.5mm body, mouth +X) + `from_x_pos` declaration. The heuristic
  derives facing from courtyard asymmetry and disagrees with physics.
- R36 rotate-180: informational, part is symmetric.
- 6× C1671 `source_part_not_found_warning`: no EasyEDA 3D-model match — the
  LCSC part validates fine, those caps just render pad-only in 3D.
- ~90 `source_component_availability_warning`: build-time data gaps;
  `check_parts.py` is the authority and passes.
- React keys belong on keyed `Fragment` wrappers, not on tscircuit primitives.
  The euro preexisting unsupported keys/pot prop were corrected without moving
  hardware. Typecheck regressions must be fixed, not excused as CLI cosmetics.

## 5. Footprints — exact KiCad copies, keep them exact

- Every footprint in `lib/footprints.tsx` is pad-for-pad identical to the KiCad
  board (verified by `check_footprints.py`: pad numbers, sizes, drills,
  positions ±0.05mm). THT hole diameters are assembly-critical — never round.
- **Tscircuit-only footprints** (no KiCad counterpart, asserted against
  `TSCIRCUIT_ONLY_FOOTPRINTS` in `tools/kicad2tscircuit.py` — keep them in
  sync):
  - `FP_jclpcb_R2512` — the 1 W MIDI OUT resistors R59 (33 Ω, `C53728`) and
    R60 (22 Ω, `C118189`), both 1 %. 2512 body 6.35 × 3.2 mm, two 1.5 × 2.8 mm pads on a
    5.2 mm centre spacing, silk outside the pads, **7.2 × 3.7 mm courtyard**
    (0.25 mm margin).
  - `FP_jclpcb_CAP_TH_BD8_0_P3_50_D0_6_FD` (C24/C25) — the KiCad footprint only
    marked polarity (6.5 × 3.18 mm courtyard offset from the pads). It now
    carries the real **8 mm body circle** (r = 4.0 mm silk) and an
    **8.6 × 8.6 mm courtyard**, so the placement checker sees the can the part
    actually occupies (R11). Pads, drills and the `+` marker are unchanged.
  - `FP_jclpcb_PWRM_TH_4P_L19_65_W7_05_P2_54_B2405S_2W` (U16, the 2 W
    B2405S-2WR3) — replaces the frozen KiCad 1 W `PWRM-TH_4P-L11.5-W6.0-P2.54`
    land pattern, which cannot carry this module. Four 1.0 mm drills / 1.8 mm
    pads at x = −6.35 / −3.81 / +1.27 / +6.35 mm (leads 1, 2, 4, 6; 3 and 5 do
    not exist), body silk 19.65 × 7.05 mm spanning y −6.147 … +0.903 about the
    lead row (the part's own geometry, not a symmetric box), courtyard
    20.14 × 7.55 mm = x −8.815 … +11.325, y −6.390 … +1.160. Square pin-1 pad +
    45° silk bevel + lead tick, and the inner guide line is broken where leads
    3/5 are absent, so the module cannot go in one position off. Listed in
    `REPLACED_FOOTPRINTS`, which is how `check_footprints.py` verifies it
    against the table instead of the frozen KiCad land pattern.
- Every `circular_hole_with_rect_pad` pad carries `holeOffsetX="0mm"
  holeOffsetY="0mm"`. That is not a geometry change (0 is the default) but the
  copper pour requires it: core 0.0.2242 emits `hole_offset_x/y` as `undefined`
  unless the props are set, and the pour connectivity pass then builds NaN arc
  vertices and dies in `@flatten-js/core` (§3). Keep them on any new rect-pad
  plated hole — a pour on a net with such a pad crashes without it.
- Anatomy per footprint: `<smtpad>`/`<platedhole>` with `portHints=["pinN"]`
  matching KiCad pad numbers (diode pad 1 = cathode — keep
  `pinLabels={{pin1:"cathode",pin2:"anode"}}`), `<silkscreenpath>` outlines,
  `<courtyardrect>` (real `F.CrtYd` where the footprint has it, else
  pads/silk + 0.25mm). KiCad Y-down → tscircuit Y-up flip is already applied.
- Bottom-side parts mirror about their footprint origin (verified empirically:
  asymmetric pads land at origin − x). Account for this in any placement math.
- J2/H1/Daisy use **pre-rotated variants** (`@R90`, rotation baked into pads,
  silk AND courtyard, placed with rotation 0) — see §3, no exceptions.
- **Adding a footprint**: author the same four elements from the `.kicad_mod`
  (pad `at`/`size`/`drill`, `fp_line` silk, `F.CrtYd` or derived box), keep KiCad
  numbering, then extend `check_footprints.py`'s known-extra handling so the
  new ref verifies against the footprint library instead of failing as
  "not in the KiCad board". If the ref already exists in the frozen KiCad board
  with a footprint that cannot carry the new part, add it to
  `REPLACED_FOOTPRINTS` (ref → tscircuit-only footprint name) instead — that is
  how U16's 2 W module is verified. Regenerate nothing — hand-write, then run
  the gate.

## 6. 3D models — harvested LCSC models

- `tools/lcsc_models.json` maps 31 LCSC codes to exact EasyEDA models
  (`objUrl`+`stepUrl`+offsets, harvested once via `tsci import <C-code>`).
  Emitted as `cadModel={{...}}`. C1671 reuses C14663's identical 0603 body.
  Rotation offsets were corrected for the EasyEDA↔KiCad frame difference (our
  TSSOP/SOIC pad-1 is top-left, EasyEDA's bottom-left).
- No transferable model (pad-only in 3D): CN1/CN2/CN4 (new E60 headers; old
  short-header models removed), Daisy Seed, J1/J2/H1, C65114/C3406
  (EasyEDA origin offsets don't transfer), C118189/C53728 (the 2512 MIDI
  resistors), C559769 (C32), C14665 (C33/C34), C23000/C22896 (the new 0603
  values), plus the replacement C107626 TLP2361 and C15127 AO3401A.
  Do not transfer the removed TLP2362/MMBT3906 models as dimensional evidence.
  Link real STEP models only after checking their package and pad-1 orientation.
- Cosmetic known-issues: IC pin-1 dots are derived, not measured; C25's
  electrolytic stripe is wrong by construction (C24/C25 share one footprint
  with opposite polarity). The new TLP2361 has no trusted transferred model.
- Re-harvest: `tsci import <NEW-CODE>` in a scratch dir, copy the `cadModel`
  block into `lcsc_models.json`, verify rotation against pad-1 corners.

## 7. Delegation notes (for subagent runs)

- One writer per tree. Confirm no `tsci build` processes are running before
  starting (`ps aux | grep tsci`); `rm -rf dist` mid-round destroys evidence.
- Briefs must contain: frozen connector coordinates (§3), metric targets with
  numbers, the full gate (§2), and "positions-only" scope. Reviews must recount
  from `dist/index/circuit.json`, never trust reports.
- Subagent time-boxes (30 min) fit ~4-6 build rounds max — scope tasks to a
  single zone or stop after two flat rounds. Transcripts hold the per-round
  tables if a summary is lost to timeout.

## 8. Production status

**Engineering prototype only.** No physical acceptance item is complete.
`hardware-acceptance.json` has 19 pending items; `doc/acceptance.md` defines
evidence, reviewer, date, current source fingerprint and actual measurements.
The final merged verification commands/results belong in
`doc/HARDWARE-VERIFICATION.md`; an earlier green export is not manufacturing
approval. The fingerprint must be refreshed after intentional hardware/source
changes, never by marking pending work accepted.

Implemented/prototype-checked: 0.25mm copper floor; 107 bottom SMT placements;
102-net/429-pad intentional-delta comparisons; four fiducials/eight test points;
real-gerber silk bounds; bottom GND pour; authored 2W U16 footprint and paper
budget; corrected MIDI parts and Type A map; calibrated-CV/buffer-off contract;
mechanical corners and negative tests; source-linked SPICE; typechecks and
fail-closed ERC/production tooling. A model/CAD result is not a bench result.

Physical/source qualification still includes: stack contacts and retention,
<=5mm fasteners, NAV resin/tilt/retention, MODE force/preload/travel, jack/pot
threads and board bow, rack lip/neighbours, OLED support, Daisy sockets/J1
shunts, EU hand sourcing, P5V <=300mA/transients/thermal, CV calibration/load,
mux/sync every power order, MIDI threshold/high current/interoperability/edges
and faults, primary STM32 GPIO limits/reset verification, chassis tie, ESD,
DFM/CPL preview and first article. The GND pour is still added after routing
and does not replace explicit GND traces. CN2 11–15 remain unused; no encoder
feature is implemented. Do not approve an order from a simulation or a budget.

## 9. Sourcing rule (both boards)

JLCPCB/LCSC may supply the parts JLC places. **All hand-soldered parts must be
sourced from RS Components, Electrokit or DigiKey with EU sales.** LCSC numbers
in existing hand BOMs are design references, not approved sources. Some parts
and mechanically compatible replacements are still unresolved; do not treat the
current hand BOM as an EU shopping list. `doc/EU-SOURCING.md` describes the
search tool and review requirements.

- For machine-placed SMT, prefer an LCSC part that is **basic** at JLCPCB;
  `check_parts.py` validates that supplier data.
- For hand-fitted components and loose assembly hardware, find an approved EU
  seller before ordering and verify the exact part's electrical, footprint,
  mechanical and mating requirements. Record the reviewed supplier and MPN.
- Do not silently substitute an unavailable or discontinued part. A change to
  board-to-board headers, jacks, display, actuator or converter can require a
  board/panel redesign and first-article checks.
- `tools/source_parts.py` generates supplier search links and optional DigiKey
  API candidates, not compatibility approvals or a complete shopping list.

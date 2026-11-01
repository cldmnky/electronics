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
    `EXTRA_COMPONENTS` blocks (C28–C34, Q2, R58–R60, D5, R51–R57); the generator
    has no counterpart for them, so they show up as missing/extra components in
    the diff
  - **the six `INTENTIONAL_PIN_MOVES`** — U11.3, U11.12, R31.1, R32.1, R39.2,
    R40.2 — i.e. six net-connection lines that differ from the port
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
bun run gate                   # the 15-step headless gate, see below (CI entry point)
bunx tsci build --autorouter-debug --autorouter-debug-dir dist/autorouter-debug
bun run check:erc              # KiCad ERC, 0 UNEXCLUDED errors (2 known-benign
                               # pin_not_driven excluded in tmp/froemix.kicad_pro)
bun run check:netlist          # KiCad netlist vs built, must MATCH (102 nets)
bun run check:extras           # tscircuit-only parts: pins, removed refs, pin moves
bun run check:footprints       # pads vs KiCad, must MATCH (376 KiCad + 51 added
                               # = 427 pads)
bun run check:parts            # JLCPCB parts vs schematic (needs network)
bun run check:values           # value literals vs built values and MPNs
bun run check:silk             # B/F silkscreen inside the board outline (gerbers)
bunx tsci check netlist        # Errors 0, Warnings 0
bunx tsci check placement      # Errors 0, Warnings 1 (known H1 warning, §4 — the
                               # CLI exits 1 on it, so trust `bun run gate`)
bunx tsci check shorts         # clean
bunx tsci check pcb-style      # clean
bunx tsci snapshot --3d --update   # refresh __snapshots__/ after a real change
bunx tsci snapshot --test --3d     # diff against them (what the gate runs)
bun run report:decoupling      # cap->IC routed-copper distances (informational)
bash ../tools/sim/run.sh       # analog SPICE checks, needs ngspice (not gated)
bun run release                # gate + fab exports into froemix/fab/
```

### The gate (`bun run gate` -> `scripts/gate.sh`)

One pass/fail contract, release minus the fab exports, safe in CI:

1. KiCad ERC — 0 unexcluded
2. `tsci build` — autoroutes, writes `dist/index/circuit.json`
3. `check:netlist` — 102 nets match
4. `check:extras` — tscircuit-only parts: every `EXTRA_COMPONENTS` pin on its
   declared net, no `REMOVED_REFS` left, all six `INTENTIONAL_PIN_MOVES` applied
5. `check:footprints` — 427 pads match
6. `check:parts` — 38 unique JLCPCB parts match (needs network)
7. `check:values` — TSX value literals vs built circuit and MPNs
8. `tsci check source` — Errors 0
9. `tsci check pin_specification` — Errors 0
10. `tsci check netlist` — Errors 0, Warnings 0
11. `tsci check placement` — Errors 0, exactly the one known H1 warning
12. `tsci check shorts`
13. `tsci check pcb-style`
14. `check_silk.py` — B/F silkscreen inside the `Edge_Cuts` outline
15. `tsci snapshot --test --3d` — schematic/PCB/3D match `__snapshots__/`

Runtime ~60s warm, ~90s from a clean tree (no `dist/`, no `.tscircuit/cache`).
The build runs second because steps 3–7 compare against the BUILT circuit. Two
steps have no usable exit code, so the gate
parses instead: `check:erc` always exits 0 (the gate requires the report to be
written and the unexcluded count to be 0) and `tsci check placement` exits 1 on
the known warning (the gate requires 0 error entries and exactly the H1 warning
— a second warning fails). Step 14 exports gerbers into a temp dir, so it needs
no network; per-step logs land in `$TMPDIR/froemix-gate/`.
`FROEMIX_SKIP_ERC=1` skips step 1 on a machine without KiCad and prints that the
result is incomplete; nothing else is skippable. ngspice is not needed.

### Analog verification (`tools/sim/`)

Not part of the gate, and never reads `dist/` or runs a build: four standalone
ngspice decks transcribed from `sections/*.tsx` — `audio_in.cir`,
`audio_out.cir`, `cv_inout.cir`, `ref_bias.cir`. Each deck's header states the
circuit, the net names it maps to and the expected values; `run.sh` runs all
four and writes `out/` plus `out/measured.txt`; `RESULTS.md` holds the
measured-vs-expected table and the findings. Needs ngspice on PATH
(`brew install ngspice`) — the tscircuit WASM engine is the no-Homebrew
fallback, see RESULTS.md. If a resistor or capacitor value changes in the TSX,
update the matching deck: the decks are copies, not a source of truth.

Current findings (full table in `tools/sim/RESULTS.md`): audio in gain 0.1000
but −3 dB at 19.7 kHz; audio out gain 3.300 before the 1 k (3.2673 at the jack)
with −3 dB at 30.9 kHz (30.0 kHz with the TL074 model); CV in is
0.3010 V/V over ±5 V, i.e. 0.1046…3.1146 V with no clipping anywhere in the
sweep; CV out is 1.5121 V/V and load-independent (4.990 V at 100k, 50k, 10k and
open); the −10 V reference draws 0.809 mA at VEE = −12 V and never drops below
0.445 mA, 3.6× the 125 µA design minimum. 1 % scaling resistors cost 24 ¢/octave
(CV in) and 48.5 ¢/octave worst case (CV out). Both audio chains have TWO
cascaded poles — the "28 kHz / 34 kHz" single-pole figures that used to be
quoted are wrong, do not repeat them.

### Release (`bun run release` -> `scripts/release.sh`)

Runs the gate, then exports into `froemix/fab/` (gitignored, safe to delete):
`froemix-gerbers.zip` (13 fab files: 4-layer gerbers + drill), `froemix-bom.csv`,
`froemix-pnp.csv`, `froemix-schematic.svg`, `froemix-pcb.svg`, `froemix.step`.
`bom` and `pnp` are NOT `tsci export` formats (the CLI rejects them): the
`gerbers` export writes `bom.csv` + `pick_and_place.csv` (JLCPCB conventions)
inside its zip, and `release.sh` extracts those two into `fab/`. No git
operations, nothing outside `fab/` is deleted.

Measure routing with: unconnected ports (`pcb_port_not_connected_error`) must be
0; then minimize vias, then total copper mm (sum `pcb_trace` segment lengths in
`dist/index/circuit.json`). Reference: 304 traces / 325 vias / 4934mm / 0
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
out; Q2, R58, R59/R60, D5, C31, C32 in), and the current **304/325/4934** adds
the Type A MIDI OUT move (driver beside the Seed, 22 Ω source) plus C33/C34,
measured the same way.

Not gated (deliberately): KiCad PCB DRC — `tmp/froemix.kicad_pcb` is a frozen
mid-edit reference, layout lives here now. Schematic-placement label
collisions — auto-layout cosmetics; explicit `schX`/`schY` are overridden
inside `<schematicsheet>` in the installed version, so there is nothing to
apply. `tracehint` (standalone and nested) is silently ignored by the
installed core — route control is placement + phases + widths.

## 3. Hard rules (placement, values, silkscreen, netlist)

- **Frozen connectors** (mechanical contract with the euro board — NEVER move):
  `CN1 (-17.58,59.64,top)`, `CN2 (12.42,-36.36,top)`, `CN3 (8.11,59.63,top)`,
  `CN4 (24.47,59.63,top)`, `H1 (28.5,20,bottom)`, `J1 (26.16,-15.89,top)`,
  `J2 (-18.5,-56,bottom)`. CN1 top + CN2 bottom = opposite-end support.
- **Sides are load-bearing.** TOP = mating headers + low-profile SMD only.
  Everything else (all tall bodies: Daisy, C24/C25, U16, J2, H1) hangs off the
  BOTTOM into the rack cavity. The euro board closes over the top at header
  height — any tall body on top blocks mating. Keep it that way.
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
  | U1 (CD4051, P3V3_A) | C31 | 7.55 | 11.04 (p7 VEE) |
  | U2 | C4 VCC / C5 VEE | 9.03 / 8.73 | 4.76 (p3 IN1P) / 5.20 (p12 IN4P) |
  | U3 | C10 VCC / C11 VEE | 7.98 / 7.87 | 4.70 / 4.71 |
  | U11 | C14 VCC / C15 VEE | 7.72 / 2.89 | 6.22 / 2.62 |
  | U12 | C17 VCC / C18 VEE | 8.40 / 9.16 | 6.15 / 4.97 |
  | U13 (MCP6002, P3V3_A) | C19 | 3.81 | 29.45 (p4 VSS) |
  | U14 (LM4040, N10V_REF) | C20 | 5.73 | 7.08 (p1 CATHODE) |
  | U15 (TLP2362, P3V3_D) | C28 | 5.46 | 3.45 (p4 GND) |
  | DaisySeed1 | C29 / C30 (P3V3_D) | 4.70 / 19.12 | 3.60 / 27.85 (p40 DGND) |
  | DaisySeed1 | C1 (P5V) | 11.30 | 9.03 (p40 DGND) |
  | U16 | C32 (U16_VIN_P) | 2.32 | n/a (isolated) |

  The GND leg is measured to the IC's **supply-return pin** (a pin named
  GND/VSS/DGND/VMINUS/VEE), not to any pin that happens to sit on the GND net:
  the TL074 non-inverting inputs and U13's INA_P are grounded but are not the
  return, and crediting them made the report quote U1 9.77 / U13 27.92. U14 has
  no such pin (shunt reference), so its GND leg falls back to the nearest pad.
  Keep the IC rail legs under ~10mm and U13's odd 29mm GND leg in mind: it is
  the C19 return, not the rail — the rail leg (3.81mm) is what the bypass is
  for, and GND is additionally under the pour (not in the graph). Bulk C1
  11.3mm and C30 19.12mm sit in the SD/OLED corner — acceptable for bulk, the
  first thing to improve if placement is ever reworked.
- **Courtyards are keepouts**: no overlaps (Errors 0 gate), M3 holes at
  (±36.95,±60.75) stay clear, use ≤5mm-OD standoff hardware. The 5.5mm-OD claim
  used to be justified with J2; that was wrong — **J2's courtyard edge is
  3.96mm from the (−36.95,−60.75) hole centre**, so a 5.5mm standoff clears it by
  1.2mm. The hardware limit comes from the other two holes: **C25** is 2.95mm
  from the (36.95,−60.75) hole and **CN1** 3.05mm from (−36.95,60.75), so
  ≤5mm OD (r = 2.5mm, 0.45mm clear). Board edge clearance ≥0.4mm.
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
  **3 `pcb_copper_pour` regions** on the bottom, 7429.2 + 0.5 + 0.4 mm² =
  7430.1 mm² of copper with 95 clearance holes, 71.5 % of the 80.9 × 128.5 mm
  board. The
  pour is generated **after** routing and the router does not route into it, so
  the trace/via numbers are unchanged (304/325/4934) and GND is still routed
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
the fitted part. Write `4.7k`, `49.9k`, `30.1k`, `187k`. `check:values` (gate step
7) compares every passive literal against the built numeric value *and* the MPN,
so a regression fails the gate instead of surviving to the order.
- **Every bottom-side silkscreen text uses `anchorAlignment="center"`.** The
renderer/exporter mirrors bottom text about its anchor, so `center_left` runs the
glyphs in −X and off the board (that is exactly how `REV 0.1` and the date
reached x = −47.5 mm on a board that ends at −40.45 mm, DESIGN-REVIEW R12).
`center` mirrors onto itself. Keep every anchor ≥1.5 mm inside the edge.
- **Silkscreen must pass `python3 ../tools/check_silk.py`** (gate step 14). It
exports the gerbers and compares B/F `SilkScreen` coordinates against
`Edge_Cuts` inset by 0.05 mm — real fab output, not the SVG preview. The PCB
snapshot draws the clipped result and cannot catch this.
- **A deliberate netlist delta goes through `INTENTIONAL_PIN_MOVES`,
`REMOVED_REFS` or `EXTRA_COMPONENTS`** in `tools/kicad2tscircuit.py`, with the
review item in the comment (`# ... DESIGN-REVIEW R09`) and the reason spelled
out in `doc/DESIGN-REVIEW.md`. The KiCad project is frozen, so the check scripts
apply those three sets to the KiCad side before comparing: adding a part,
moving a pin or deleting a ref any other way makes `check:netlist` fail, and
silencing it by loosening the comparison is not an option. Current sets:
`REMOVED_REFS` = D2, R49, R50; `EXTRA_COMPONENTS` = C31, C32, C33, C34, Q2, R58,
R59, R60, D5 (+ the older C28–C30, R51–R57); `INTENTIONAL_PIN_MOVES` =
U11.3/U11.12, R31.1, R32.1, R39.2, R40.2. `check:extras` (gate step 4) asserts
all three sets against the built circuit, because `check:netlist` strips
them before comparing and would otherwise never look at those pins.
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
- "fiducial: `key` is not a prop" React warning on every run: cosmetic — a
  `key` prop passed to `<fiducial>` in `sections/fabrication.tsx`. The
  fiducials build, route and export correctly.

## 5. Footprints — exact KiCad copies, keep them exact

- Every footprint in `lib/footprints.tsx` is pad-for-pad identical to the KiCad
  board (verified by `check_footprints.py`: pad numbers, sizes, drills,
  positions ±0.05mm). THT hole diameters are assembly-critical — never round.
- **Tscircuit-only footprints** (no KiCad counterpart, asserted against
  `TSCIRCUIT_ONLY_FOOTPRINTS` in `tools/kicad2tscircuit.py` — keep the three in
  sync):
  - `FP_jclpcb_R2512` — the 1 W MIDI OUT resistors R59 (22 Ω, `C118189`) and
    R60 (33 Ω, `C53728`). 2512 body 6.35 × 3.2 mm, two 1.5 × 2.8 mm pads on a
    5.2 mm centre spacing, silk outside the pads, **7.2 × 3.7 mm courtyard**
    (0.25 mm margin).
  - `FP_jclpcb_CAP_TH_BD8_0_P3_50_D0_6_FD` (C24/C25) — the KiCad footprint only
    marked polarity (6.5 × 3.18 mm courtyard offset from the pads). It now
    carries the real **8 mm body circle** (r = 4.0 mm silk) and an
    **8.6 × 8.6 mm courtyard**, so the placement checker sees the can the part
    actually occupies (R11). Pads, drills and the `+` marker are unchanged.
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
  "not in the KiCad board". Regenerate nothing — hand-write, then run the gate.

## 6. 3D models — harvested LCSC models

- `tools/lcsc_models.json` maps 31 LCSC codes to exact EasyEDA models
  (`objUrl`+`stepUrl`+offsets, harvested once via `tsci import <C-code>`).
  Emitted as `cadModel={{...}}`. C1671 reuses C14663's identical 0603 body.
  Rotation offsets were corrected for the EasyEDA↔KiCad frame difference (our
  TSSOP/SOIC pad-1 is top-left, EasyEDA's bottom-left).
- No transferable model (pad-only in 3D): Daisy Seed, J1/J2/H1, C65114/C3406
  (EasyEDA origin offsets don't transfer), C118189/C53728 (the 2512 MIDI
  resistors), C559769 (C32), C14665 (C33/C34) and C23000/C22896 (the new 0603
  values). Link real STEP models
  when available.
- Cosmetic known-issues: IC pin-1 dots are derived, not measured; C25's
  electrolytic stripe is wrong by construction (C24/C25 share one footprint
  with opposite polarity); TLP2362 dot may be off (mirrored numbering).
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

Everything below was verified on this tree with `bun run gate` + `bun run
release`; no item is aspirational.

**Done**

- **Width policy**: board floor 0.25mm (`defaultTraceWidth`/`minTraceWidth`), so
  no copper is thinner than 0.25mm; power rails keep 0.3–0.6mm nominal (§3).
- **Silkscreen**: 31 texts in `sections/silk.tsx` — every IC/module, every
  connector, C24/C25, D1/D3/D4/D5, plus `FROEMIX`, `REV 0.1`, the date and the
  two-line hand-solder legend. Every text is `anchorAlignment="center"` and the
  whole layer is provably inside the outline (`check_silk.py`, gate step 14).
  The 0603/1206 passives stay unlabelled (labels do not fit between their
  courtyards); assemble those from the CPL.
- **Fiducials**: 4 bare-copper FID pads on the bottom (`FabricationSection`).
- **Test points**: 8 bottom-side TP pads — P5V, P3V3_D, P3V3_A, VCC, VEE,
  N10V_REF, SYNC_IN, MULTIPLEX_ADC_1.
- **Decoupling**: U1 has a local C31 (7.55 mm rail leg); every IC rail leg is
  ≤9.16 mm and the report is pin/net/layer aware (§3 has the table). C14/C15
  went from 13.2/18.6 mm to 7.72/2.89 mm as part of the CV rework; the GND legs
  are quoted to the IC's return pin, not to any grounded pin.
- **Regression gates**: 15 steps — ERC + build + netlist + tscircuit-only parts
  (`check:extras`) + footprints + parts +
  value literals + source/pin_spec/netlist + placement + shorts + pcb-style +
  silkscreen bounds + snapshot diff behind one command (`bun run gate`, §2).
- **Analog verification**: 4 ngspice decks in `tools/sim/` (audio in/out, CV
  in/out, −10 V reference) with a measured-vs-expected table in
  `tools/sim/RESULTS.md`; gains and scaling confirmed, the two audio roll-offs
  and the CV-in headroom documented (§2).
- **Design review closed**: every R01–R14 finding is fixed or has a named gate
  (`doc/DESIGN-REVIEW.md`, "Resolution status" and "Second review round"): MIDI
  OUT rework (Q2/R58/R59/R60, **Type A**: 22 Ω source into the tip, 33 Ω sink
  through Q2 from the ring), D2 removed, DAC followers + buffer-off firmware
  rule, D5 sync clamp, 33k audio feedback, 30.1k/187k CV in, load-independent
  CV out plus the C33/C34 100 pF load compensation, U1 bypass,
  value-literal + extras + silkscreen gates, real electrolytic courtyards,
  written 5 V budget (`doc/POWER-BUDGET.md`) and the front-board interface
  (`doc/FRONTBOARD-INTERFACE.md`).
- **Fab export**: `bun run release` writes gerbers/bom/pnp/svg/step into
  `froemix/fab/` (§2).
- **GND copper pour**: bottom-layer GND plane builds (`copperpour` in
  `index.circuit.tsx`, requires the `holeOffsetX`/`holeOffsetY` workaround, §3),
  3 regions / 7430.1 mm² / 95 clearance holes = 71.5 % of the board, gate 15/15
  green, routing 304 traces / 325 vias / 4934mm / 0 unconnected / 0 overlaps.

**Open — close these before ordering**

- **GND plane is not load-bearing yet**: the pour is added after routing and the
  router does not route into it, so GND is still 0.25–0.3mm traces plus the
  plane underneath. Pour-aware routing (`unbroken`) or dropping GND's explicit
  traces is the next step if via count matters (GND carries 57 pins).
- **GND/chassis tie**: no single-point tie to the standoffs or the rack chassis
  is implemented and no decision is recorded. Decide before the euro board
  defines the grounding scheme.
- **U16 5 V margin** (R13): budget written, measurement open. The 1 W/200 mA
  converter has no worst-case margin on paper (194 mA max without the H1
  module, 267 mA with it). The ≤170 mA P5V acceptance test in
  `doc/POWER-BUDGET.md` must be run during bring-up; FAIL swaps in the 2 W
  module (7-pin SIP, new footprint + placement + baseline).
- **Front board is not designed** (R14): the electrical contract is frozen in
  `doc/FRONTBOARD-INTERFACE.md` (M_ADC pot-only/0–3.3 V, CN1/CN2 pinout, CV and
  audio levels, 3.3 V budget, acceptance test); the board and its test are open.
- **CV tolerance**: 1% scaling resistors, ~2 % of scale (CV in) / ~4.04 % worst
  case (CV out) — firmware calibration required as designed; 0.1 % parts are the
  alternative.
- **Encoder**: no encoder pins on the base board; CN2 pads 11–15 are unused and
  reserved for that decision.
- **JLC second side / CPL mirror**: 98 of the 106 SMT placements are bottom-side
  and the exported CPL mirrors them (92 at 180°, 6 at 0°; 8 top parts at 0°);
  verify against JLCPCB's bottom-side expectation before paying for assembly.
- **MIDI receiver threshold**: the on-board receiver is a TLP2362 with a **5 mA
  maximum switching threshold** (recommended on-current from 7.5 mA) while our
  own Type A transmitter delivers **4.48 mA worst case** into a receiver at
  1.7 V Vf. A marginal CA-033 transmitter can be intermittent — verify with the
  real units (`doc/FRONTBOARD-INTERFACE.md` §2).
- **DFM review of the exported gerbers**: nobody has opened
  `froemix/fab/froemix-gerbers.zip` in a fab viewer yet. `check shorts` and
  `check pcb-style` are routing checks, not a DFM audit (mask expansion,
  annular ring, drill sizes, panel rails).

## 9. Sourcing rule (both boards)

Default supplier for every part is **LCSC / JLCPCB** - that is what `check_parts.py`
validates against and what the SMT line places. **Any part that LCSC does not
stock must be bought from Electrokit (`https://www.electrokit.com/`).** No third
supplier, no distributor-only parts.

Consequences to respect when a part is added:

- Prefer an LCSC part, and prefer one that is **basic** at JLCPCB.
- If the only option is not at LCSC, look for the Electrokit equivalent
  *before* the footprint is drawn: the footprint follows the part that can
  actually be bought. Record the Electrokit article number next to the LCSC
  column in the BOM, and note in the design record (`doc/EURO-BOARD.md` §8 for
  the euro board) that the part is distributor-sourced.
- A part that is "not recommended for new designs" or discontinued **and** not at
  LCSC is a stop item: find the replacement part from Electrokit, do not design
  it in and hope.
- The euro board's BOM and its Electrokit mapping live in `doc/EURO-BOARD.md`
  ("BOM and sourcing"). Keep it current when a part changes.

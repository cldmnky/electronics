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
bun run gate                   # the 10-step headless gate, see below (CI entry point)
bunx tsci build --autorouter-debug --autorouter-debug-dir dist/autorouter-debug
bun run check:erc              # KiCad ERC, 0 UNEXCLUDED errors (2 known-benign
                               # pin_not_driven excluded in tmp/froemix.kicad_pro)
bun run check:netlist          # KiCad netlist vs built, must MATCH (105 nets)
bun run check:footprints       # pads vs KiCad, must MATCH (382 KiCad + 28 added
                               # = 410 pads)
bun run check:parts            # JLCPCB parts vs schematic (needs network)
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
3. `check:netlist` — 105 nets match
4. `check:footprints` — 410 pads match
5. `check:parts` — 34 unique JLCPCB parts match (needs network)
6. `tsci check netlist` — Errors 0, Warnings 0
7. `tsci check placement` — Errors 0, exactly the one known H1 warning
8. `tsci check shorts`
9. `tsci check pcb-style`
10. `tsci snapshot --test --3d` — schematic/PCB/3D match `__snapshots__/`

Runtime ~40s warm, ~1min from a clean tree (no `dist/`, no `.tscircuit/cache`).
The build runs second because steps 3–5 compare against the BUILT circuit. Two
steps have no usable exit code, so the gate
parses instead: `check:erc` always exits 0 (the gate requires the report to be
written and the unexcluded count to be 0) and `tsci check placement` exits 1 on
the known warning (the gate requires 0 error entries and exactly the H1 warning
— a second warning fails). Per-step logs land in `$TMPDIR/froemix-gate/`.
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
but −3 dB at 19.7 kHz and audio out gain 9.900 into 100k with −3 dB at 20.5 kHz
(both chains have TWO cascaded poles, so the "28 kHz / 34 kHz" single-pole
figures that used to be quoted are wrong); CV in maps ±5 V to exactly 0/3.3 V
but the endpoints sit on the rails so the MCP6002 clips them to 0.030/3.270 V;
CV out 0…3.3 V → 0…4.940 V into 100k as designed; the −10 V reference draws
0.809 mA at VEE = −12 V and never drops below 0.445 mA, 3.6× the 125 µA design
minimum. 1 % scaling resistors cost 24 ¢/octave (CV in) and 47–49 ¢/octave
worst case (CV out).

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
`dist/index/circuit.json`). Reference: 293 traces / 323 vias / 4730mm / 0
unconnected / 0 overlaps. The autorouter is deterministic — identical trees
rebuild byte-identical numbers.

The 285/291/4662 baseline this section used to quote predates the 0.25mm width
floor (§3) and the fabrication aids, which landed together: 0.2 -> 0.25mm is
+25% copper *width* (no extra length by itself) and the 8 test points add 8
connections to existing nets, and the router re-solves everything from source,
so 293/323/4730 is the combined result of both (they were not measured
separately). It is a trade, not a regression: 0 unconnected, 0 overlaps, shorts
and pcb-style clean. The 4 fiducials carry no nets and change no routing.

Not gated (deliberately): KiCad PCB DRC — `tmp/froemix.kicad_pcb` is a frozen
mid-edit reference, layout lives here now. Schematic-placement label
collisions — auto-layout cosmetics; explicit `schX`/`schY` are overridden
inside `<schematicsheet>` in the installed version, so there is nothing to
apply. `tracehint` (standalone and nested) is silently ignored by the
installed core — route control is placement + phases + widths.

## 3. Hard placement rules

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
  C4/C5-U2, C10/C11-U3, C14/C15-U11, C17/C18-U12, C19-U13, C20-U14, C28-U15,
  C1/C29/C30-Daisy pins 39/38/38. Distances are the *routed copper path* from
  cap pad to IC pad (re-measure with `bun run report:decoupling`, which reads
  `dist/index/circuit.json`; centre-to-centre numbers are ~1.5mm smaller and
  hide detours): C4 4.8, C5 5.2, C10 4.7, C11 5.2, C14 4.5, C15 2.6, C17 6.6,
  C18 5.7, C19 3.8, C20 5.4, C28 3.4, C29 3.6; bulk C1 9.0 and C30 18.9
  (P3V3_D bulk sits in the SD/OLED corner — acceptable for bulk, the first
  thing to improve if placement is ever reworked). Keep the IC pairs under
  ~7mm of routed copper.
- **Courtyards are keepouts**: no overlaps (Errors 0 gate), M3 holes at
  (±36.95,±60.75) stay clear, use ≤5mm-OD standoff hardware (5.5mm reaches
  0.3mm into J2's courtyard). Board edge clearance ≥0.4mm.
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
  i.e. 0.3mm, which fails the build's copper-to-board-edge DRC). Built result: 8
  `pcb_copper_pour`
  regions on the bottom, 7498mm² main plane over the whole board with 91 holes
  plus 7 connected islands, 7540mm² total. The pour is generated **after**
  routing and the router does not route into it, so the trace/via numbers are
  unchanged (293/323/4730) and GND is still routed explicitly; making the pour
  carry GND current means pour-aware routing (`unbroken`), which is untested.
- **Pours need `holeOffsetX`/`holeOffsetY` on every `circular_hole_with_rect_pad`
  pad** (§5). Without them core leaves `hole_offset_x/y` undefined, the copper
  pour connectivity pass in `@tscircuit/core` dies in `@flatten-js/core`
  (`TypeError: undefined is not an object (evaluating 'node.item.key.low')`) and
  the build is fatal. Min repro + root cause: `doc/pour-repro/`. This is a
  local workaround for an upstream defaulting bug — do not "clean up" the props.

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
  (EasyEDA origin offsets don't transfer). Link real STEP models when available.
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
- **Silkscreen**: 34 texts in `sections/silk.tsx` — every IC/module, every
  connector, C24/C25, D1–D4, TP1–TP8, plus `FROEMIX`, `REV 0.1` and the date.
  The 0603/1206 passives stay unlabelled (labels do not fit between their
  courtyards); assemble those from the CPL.
- **Fiducials**: 4 bare-copper FID pads on the bottom (`FabricationSection`).
- **Test points**: 8 bottom-side TP pads — P5V, P3V3_D, P3V3_A, VCC, VEE,
  N10V_REF, SYNC_IN, MULTIPLEX_ADC_1.
- **Decoupling**: every IC pair ≤6.6mm of routed copper, C14/C15 now 4.5/2.6mm
  (were 13.2/18.6); bulk C1 9.0mm, C30 18.9mm (§3 has the table).
- **Regression gates**: ERC + netlist + footprints + parts + build + tscircuit
  checks + snapshot diff behind one command (`bun run gate`, §2).
- **Analog verification**: 4 ngspice decks in `tools/sim/` (audio in/out, CV
  in/out, −10 V reference) with a measured-vs-expected table in
  `tools/sim/RESULTS.md`; gains and scaling confirmed, two audio roll-offs and
  the clipped CV-in endpoints documented (§2).
- **Fab export**: `bun run release` writes gerbers/bom/pnp/svg/step into
  `froemix/fab/` (§2).
- **GND copper pour**: bottom-layer GND plane builds (`copperpour` in
  `index.circuit.tsx`, requires the `holeOffsetX`/`holeOffsetY` workaround, §3),
  8 regions / 7540mm² of copper, gate 12/12 green, routing unchanged at
  293 traces / 323 vias / 4730mm / 0 unconnected / 0 overlaps, `B_Cu.gbr` grew
  52KB → 371KB.

**Open — close these before ordering**

- **GND plane is not load-bearing yet**: the pour is added after routing and the
  router does not route into it, so GND is still 0.25–0.3mm traces plus the
  plane underneath. Pour-aware routing (`unbroken`) or dropping GND's explicit
  traces is the next step if via count matters (GND carries 57 pins).
- **GND/chassis tie**: no single-point tie to the standoffs or the rack chassis
  is implemented and no decision is recorded. Decide before the euro board
  defines the grounding scheme.
- **Mux input clamps**: `M_ADC_*` has no series-R/Schottky clamps; pots on the
  euro board must stay inside 0–3.3V (README "Deliberately not changed").
- **CV tolerance**: 1% scaling resistors, ~3% worst-case gain error — firmware
  calibration required as designed; 0.1% parts are the alternative.
- **Encoder**: no encoder pins on the base board; CN2 pads 11–15 are unused and
  reserved for that decision.
- **JLC second side / CPL mirror**: all rotations are 0 and 97 of 110
  placements are bottom-side; verify the mirrored CPL against JLCPCB's
  bottom-side expectation before paying for assembly.
- **DFM review of the exported gerbers**: nobody has opened
  `froemix/fab/froemix-gerbers.zip` in a fab viewer yet. `check shorts` and
  `check pcb-style` are routing checks, not a DFM audit (mask expansion,
  annular ring, drill sizes, panel rails).

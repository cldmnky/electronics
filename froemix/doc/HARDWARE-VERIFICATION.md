# Froemix hardware verification and remediation

This change covers Froemix only: the 16HP base PCB, 20HP front PCB and panel,
assembly mechanics, schematics, simulation and release checks. It does not
approve production hardware or replace first-article measurements.

## Fixed constraints

- Base PCB: 80.9 × 128.5 mm, 4 layers. Front PCB and panel: 101.6 × 128.5 mm.
- Preserve all frozen connector/control centres, board sides, mating-pin maps,
  the panel outline, current circular-hole diameters and OLED opening. The owner
  approved one exception in this review: correct the 4 rail-mount hole X centres
  from ±43.30 to ±43.18 mm (17×5.08 mm separation). Keep their Y centres unchanged
  at ±61.25 mm and all other 23 circular-hole centres unchanged. Do not restore
  the 4 removed audio-LED holes: that removal addressed nonlinear audio loading.
- Keep Type A MIDI polarity: ring = DIN 4/source; tip = DIN 5/return.
- Preserve the 0.25 mm base copper floor, bottom-only SMT and existing netlist,
  footprint and intentional-delta comparisons. Do not regenerate the frozen port.
- Source hand-fitted parts through RS, Electrokit or DigiKey with EU sales;
  an LCSC reference alone is not an approved hand-assembly purchase.

## Review and implementation plan

The architect and separate mechanical/electrical auditors reviewed the sources
before implementation. Independent writers use separate worktrees. The final
review must use the merged source and actual tool output, not agent summaries.

| Area | Baseline finding | Required change/evidence |
|---|---|---|
| Mechanical stack | Nominal insertion 3.3 mm; summed tolerance window 2.55–4.05 mm has only 0.05 mm margin to the policy | Explicit full corner calculation and validation; actual contact/bottoming/retention remains pending |
| NAV | Baseline radial clearance 0.204250 mm at assumed ±8° tilt, only 0.004250 mm above the 0.2 mm check | Improve printable geometry if wall/pocket constraints permit; sweep/corner tests; actual tilt and retention remain pending |
| MODE | 0.5 mm flange clearance with no verified switch travel | Check assembled worst cases; require measured travel/preload before ordering |
| Fasteners | Clearance limits and 5.0/5.5 mm screw-head statements disagree | Measure generated both-board courtyard/hole distances; distinguish spacer, head and washer envelopes; no assumed M3 head approval |
| Procurement | Daisy 1×20 sockets and J1 shunts lack approved exact identities | Record unresolved identities and block production; never invent compatible MPNs |
| Panel/control fit | Jack body 8.8 mm vs provisional minimum gap 8.5 mm; full-height rack and OLED support unverified | Record and test assumptions; require actual bushing/thread, PCB-bow, rack-lip and display-support evidence |
| Rack grid/width | Original panel holes at x=±43.3 have 86.60 mm separation, versus 17×5.08=86.36 mm on a fixed threaded strip; nominal 101.60 mm width has no neighbour-fit allowance | Owner approved correcting only the 4 rail-hole X centres to ±43.18 mm, retaining Y and every control centre. Record the explicit fixture revision and test the grid; actual rail-profile and adjacent-module fit remain pending |
| MIDI IN | TLP2362 guaranteed maximum threshold 5 mA exceeds the conservative loop minimum | Primary-backed replacement, physical pin/net checks, source-linked weak/high-current simulations; keep high-current qualification pending |
| MIDI OUT | PNP drop reduces legacy-receiver current below the 4 mA PC900 case | Primary-backed low-loss stage and fault analysis; use STM32H750 GPIO data, not RP2350 values |
| Schematics | Broad sheets and dense generic IC drawings hide functional paths | Functional sections, meaningful visible labels/values and actual rendered inspection without PCB moves |
| SPICE | Copied decks can drift; analysis prints results without failing | Assert bounded results, source values and topology; negative mutations; no stale/NaN output accepted |
| Tool portability | Full baseline stops at a hardcoded macOS KiCad path | Discover a genuine KiCad executable; retain ERC/netlist diagnostics and fail clearly when unavailable |
| Production release | Automated gate alone permits fabrication exports | Separate fail-closed, fingerprint-bound physical acceptance at every production entry point, before deleting or writing artifacts |

## Baseline verification

- Pinned base/front dependencies installed in the feature worktree.
- Base `tsci build` succeeded. Single-side SMT check: 107 bottom placements.
- The old `make gate` stopped at the hardcoded macOS `kicad-cli` path; setting
  `FROEMIX_SKIP_ERC=1` does not repair this second dependency. This was not a
  successful full gate.
- The unmodified euro gate passed: 86 traces, 53 vias, approximately 2351 mm
  copper, 0 unconnected ports and 0 trace errors. Base extras (20 parts,
  41 declared pins), footprints (429 pads), and values (95 passives) passed.
- The old `make sim` failed after its first deck under native ngspice: the
  control block wrote data, but the process returned nonzero. New verification
  must fix this failure and assert real fresh outputs, not read committed data.
- A genuine native KiCad 10.0.7 CLI was installed separately under `/tmp` from
  official Debian packages (not a stub or handwritten exporter). Running
  `check:netlist --kicad-cli <native-wrapper>` independently matched 102 nets,
  with the existing 4 pin renumbers and 6 intentional pin moves applied.
- Initial ERC also exposed environment-specific library paths and obsolete
  exclusion identities. These must be diagnosed precisely, not hidden with a
  general warning/error filter.

## Final verification

Full merged `make test` passes with exit 0 (log:
`/tmp/froemix-hardware-checks/final-test2.log`). Environment: pinned Bun
dependencies, genuine KiCad 10.0.7 CLI, genuine 4ms library, native ngspice.

- Base gate, 18/18 steps: typecheck clean; native ERC 0 unexcluded (0 errors,
  0 warnings, 9 preapproved exclusions, all three severity flags, report
  validated); build 107 bottom SMT placements; 102/102 nets; 20 extras with
  41 pins; 429 pads; 40 unique JLCPCB codes; 95 passive literals vs build and
  MPN; 22 electrical pin/margin checks; rendered schematic five sheets 0/0
  label collisions with page-bounds and note checks; source/pin/netlist,
  placement (only the known H1 warning), shorts and pcb-style clean;
  silkscreen inside the outline; schematic/PCB/3D snapshots match.
- Euro gate: PASS — typecheck, build, netlist, placement, shorts, pcb-style,
  mating and mux contracts, mechanical stack, panel artwork, regression
  tests and snapshot diff; 86 traces, 53 vias, 2351 mm copper, 0 unconnected.
- Release report: 0/19 accepted, 19 pending, fingerprint `36f8a0e8…` current;
  PHYSICAL ACCEPTANCE NOT APPROVED, correctly blocking production.
- Mechanical: 64 stack corners (insertion 2.55–4.05 mm), 32 actuator corners
  (NAV 5.6 mm cup, worst corner wall 0.275 mm, radial 0.305 mm), 16-row
  fastener/courtyard report; rack fit explicitly NOT certified.
- Unit tests: Python tools 93 OK, sim 49 OK, release bun 94 pass / 0 fail.
- SPICE: 177 checks over 11 decks, all pass (behavioural, not hardware).

One transient base-PCB snapshot mismatch occurred in an earlier merged run
and did not reproduce in three subsequent runs (standalone gate plus two
full `make test` passes). The committed snapshots were verified element-wise:
the base snap differs from its predecessor only in the pinned-date silkscreen
text, the euro snap only in float re-serialization with identical coordinates.

Unresolved by design: all 19 physical acceptance items (stack retention,
fasteners, NAV/MODE, jack/pot/rack/OLED fit, sockets/shunts, P5V load and
thermal, CV calibration and cable load, mux/sync power orders, MIDI threshold
and high-current corners including the 125 °C derated 8.05 mA risk hold,
primary STM32H750 GPIO-limit verification, chassis tie, ESD, DFM/CPL and
first article). Formal independent review cycle 2 covers this tree; its
findings and the two-cycle disposition will be recorded here before handover.

## Mechanical primary reference

[Doepfer A-100 construction details](https://doepfer.de/a100_man/a100m_e.htm)
states a 5.08 mm HP grid, mounting-hole separation in multiples of that grid,
128.5 mm panel height and 101.30 mm actual width for a nominal 101.60 mm 20HP
panel. The owner explicitly approved the limited 4-hole HP-grid correction
above after the mismatch was reported. No width change was approved. A
successful fit with sliding nuts does not prove fit with a fixed threaded
strip or a different rail profile.

## Physical acceptance

CAD dimensions, computed tolerances and SPICE outputs are not bench results.
Actual mating contact, switch travel, jack/pot threads, OLED support, rack fit,
Daisy clearance, electrical power sequencing, U16 loading, MIDI interoperability,
fault temperature, CV calibration, grounding/ESD and DFM/CPL approval require
first articles and recorded evidence. No such evidence is claimed here.

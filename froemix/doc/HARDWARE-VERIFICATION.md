# Froemix hardware verification and remediation

This change covers Froemix only: the 16HP base PCB, 20HP front PCB and panel,
assembly mechanics, schematics, simulation and release checks. It does not
approve production hardware or replace first-article measurements.

## Fixed constraints

- Base PCB: 80.9 × 128.5 mm, 4 layers. Front PCB and panel: 101.6 × 128.5 mm.
- Preserve all frozen connector/control centres, board sides, mating-pin maps,
  the panel outline, 27 current circular holes and OLED opening. Do not restore
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
- A genuine native KiCad 10.0.7 CLI was installed separately under `/tmp` from
  official Debian packages (not a stub or handwritten exporter). Running
  `check:netlist --kicad-cli <native-wrapper>` independently matched 102 nets,
  with the existing 4 pin renumbers and 6 intentional pin moves applied.
- Initial ERC also exposed environment-specific library paths and obsolete
  exclusion identities. These must be diagnosed precisely, not hidden with a
  general warning/error filter.

## Final verification

Implementation and the merged verification run are in progress. Exact commands,
counts, independent-review findings and unresolved limitations will be added
before the PR is handed over.

## Physical acceptance

CAD dimensions, computed tolerances and SPICE outputs are not bench results.
Actual mating contact, switch travel, jack/pot threads, OLED support, rack fit,
Daisy clearance, electrical power sequencing, U16 loading, MIDI interoperability,
fault temperature, CV calibration, grounding/ESD and DFM/CPL approval require
first articles and recorded evidence. No such evidence is claimed here.

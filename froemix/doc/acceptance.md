# Verification, acceptance and the production gate

Status: **prototype definition, not a fabrication release.** No physical
measurement has been recorded for this project. `make test` and `make gate` are
prototype verification; `make release` is the production path and it stops at the
physical acceptance gate until every item is accepted.

## The three commands

| Command | What it is | Can it produce an order set? |
|---|---|---|
| `make test` | Prototype verification: both gates, the SPICE decks, the mechanical/panel checks and the unit tests | No |
| `make gate` | The same regression gates without SPICE/mechanical/unit tests, plus a printed acceptance report | No |
| `make release` | Production: physical acceptance, then the gates, then the fab exports | Only when every item is accepted |

`make acceptance` prints the acceptance state and refuses nothing;
`make acceptance-check` is the production gate and refuses everything it cannot
verify.

## `make test` — prototype verification, not an approval

`make test` runs:

1. `make gate` — the base board's 15-step KiCad/tscircuit gate (ERC, build,
   netlist, tscircuit-only parts, footprints, JLCPCB parts, value literals,
   source/pin_spec/netlist checks, placement, shorts, pcb-style, silkscreen
   bounds, snapshots) and the euro board's 11-step gate (build, netlist,
   placement, shorts, style, mating contract, mux-input contract, mechanical
   stack, panel artwork, regression tests, snapshots);
2. the acceptance report — every required physical item and the current
   hardware fingerprint (informational: pending items do not fail this step,
   but a malformed or stale record does);
3. `make sim` — the four base-board ngspice decks plus the front-board decks
   (`tools/sim/run.sh`, `tools/sim/test_frontboard.py`; needs `ngspice`);
4. `make mechanical` — `euro/scripts/mechanical_check.ts` (supplier identities,
   Z/actuator envelopes) and `euro/scripts/panel_check.ts` (audited holes,
   window, artwork keepouts) on the just-built euro circuits;
5. `make unit-tests` — the offline Python tool tests (`tools/test_*.py`,
   including the portable `kicad-cli` resolver and the ERC checker) and the
   acceptance validator and entry-point guard tests (`bun test tools/release`).
   The silkscreen unit test and the euro panel/mechanical unit tests read the
   built circuits, so the gates run them after `tsci build` instead.

What that proves: the sources, netlist, placement, geometry, silkscreen, panel
artwork, mechanical envelopes, simulation models and the acceptance machinery
agree with each other.

What it **cannot** prove: that an 11 mm spacer stack closes, that a knob fits a
bushing, that the Daisy sockets and USB clear the rack, that the 5 V module stays
under 300 mA on the bench, that the mux and sync inputs survive a back-fed rail,
that the MIDI receiver meets its threshold with real units, that ESD does not
damage anything, that JLCPCB's CAM matches the gerbers, or that a first article
works. The SPICE decks are behavioural checks, not datasheet-accurate models.
"No test failed" never means "the hardware is approved".

## `make release` — production path, fail-closed

Order, and what each guard prevents:

1. `make acceptance-check` (a Make prerequisite, so it runs before the recipe):
   `bun tools/release/gate.ts --check` reads `hardware-acceptance.json`,
   recomputes the hardware fingerprint and validates every item. It builds
   nothing, exports nothing and deletes nothing. A pending or invalid item
   stops the release here.
2. `tscircuit/scripts/release.sh` repeats the same guard as its **first**
   action, before `bash gate.sh` and before `rm -rf fab/`, so a direct
   `bun run release` is gated too.
3. `euro/scripts/release.sh` repeats it before the euro gate and before
   `rm -rf fab/euro/`.
4. `euro/scripts/panel_release.ts` repeats it (via `checkPhysicalAcceptance`)
   before `dist/panel-release/` is created, written or deleted, so a direct
   `bun run panel:release` is gated too.
5. Every production entry point also **refuses `FROEMIX_SKIP_ERC=1`**: an ERC
   skip is a prototype convenience (`make gate` stays available with that
   variable), and no amount of accepted physical evidence substitutes for ERC.

There is no `--force`, `--skip`, `--allow-unverified`, `waive`, `override` or
`bypass` flag or key: unknown keys and override-like keys are rejected by the
validator, so no blanket flag can clear unresolved hardware. The guard tests in
`tools/release/entrypoints.test.ts` place sentinels in `fab/`,
`fab/euro/` and `euro/dist/panel-release/`, run each entry point with the
pending record and prove the entry point stopped before its gate step and left
everything in place.

`euro/scripts/mechanical_release.ts` still writes dimensioned prototype
artefacts (MODE/NAV STLs, hardware list). That is a prototype path, not
production certification: it does not check the acceptance record and its output
never enters `fab/` through a release.

## Machine-readable acceptance state

`hardware-acceptance.json` holds one entry per required item with
`state: "pending" | "accepted"`. An accepted item needs:

| Field | Requirement |
|---|---|
| `evidence` | a Markdown file directly in `doc/evidence/` (relative path, no `..`, no absolute path, not a directory, at least 200 bytes, and no symlink anywhere on the path below the project root) |
| `reviewer` | a named person, not `TBD`/`n/a`/`-` |
| `reviewed_at` | a real date `YYYY-MM-DD`, not in the future |
| `source_fingerprint` | equal to the record's `hardware_fingerprint`, which must equal the current fingerprint |
| `evidence_sha256` | optional; when present the file content must match |

The evidence file must name its item and contain at least one recorded value
with a unit. The path check resolves the real project root first, then walks
`doc/` and `doc/evidence/` and refuses a symlink on the way, so a symlinked
directory cannot put evidence outside the project while the `.md` file itself
looks regular; the resolved file must also stay inside the real evidence
directory and the real project root. A symlink ABOVE the project root is fine
(macOS `/tmp` often is one), and an evidence directory supplied by a caller must
itself resolve inside the project - it cannot become an allowed jail. Format and template: [evidence/README.md](evidence/README.md).
The fingerprint covers both boards' sources, the panel/CAD algorithms, the
packages and lock files, the JLCPCB order settings and the curated
sourcing/part maps; a hardware change makes old evidence stale, a
documentation-only commit does not.

## Required items (all currently pending)

| Item | What the evidence must show |
|---|---|
| `stack_11mm` | Assembled pair: 11 mm clear spacers, header shoulder/post engagement, front tails ≤ 1 mm, J1 + shunts ≤ 9.5 mm, installed depth |
| `daisy_socket` | Both 1x20 Daisy sockets: part identity, installed height, insert/remove force, rack/euro clearance, USB access |
| `fasteners` | Real head and washer MPNs and their measured OD, with clearance to C25, CN1 and J2 |
| `nav_actuator` | NAV reach through the panel, tilt/travel, retention, return force |
| `mode_switch` | MODE travel, preload, actuation force, cap fit, hole clearance |
| `jacks_pots` | Jack/pot bushing thread, panel hole fit, nut/washer seating, knob fit |
| `panel_rack` | 20HP panel in a real 3U rack: rail engagement, screw positions, flatness, neighbour clearance |
| `oled_support` | OLED identity, standoff height, window alignment, flex path, U16 clearance |
| `eu_hand_parts` | Every hand-soldered part and loose hardware item with an approved EU supplier and exact MPN |
| `p5v_rail` | P5V loaded ≤ 300 mA, start-up/load-step transients, U16 temperature |
| `cv_calibration` | CV in/out calibrated against a reference; firmware DAC buffer confirmed disabled |
| `cv_cable_load` | CV out stable with ≤ 10 nF cable/load; overshoot and settle recorded |
| `mux_sync` | Mux and sync in every power order, including back-fed, with no damage or latch-up |
| `midi_input` | Receiver threshold, high-current input, fault temperature |
| `midi_output` | Legacy receiver headroom, rise/fall edges, sustained ring/tip fault temperatures, plus the STM32H750 GPIO absolute limits and reset state verified against a primary ST datasheet (20 mA per pin, VOL 0.4 V, VOH and reset are assumptions, not measurements) |
| `ground_tie` | Single-point ground/chassis tie decided and measured |
| `esd` | ESD test on every exposed jack, opening and connector, with protection parts recorded |
| `dfm_cpl` | DFM/CPL review of both boards' and the panel's exported gerbers, BOM and placement preview |
| `first_article` | First-article boards and panel inspected and measured against this list |

Requirements are not evidence. The table states what to record; the state file
holds what has actually been recorded, which is nothing yet.

## ERC and the KiCad reference: portable, honest, and what it does not cover

`bun run check:erc` (from `tscircuit/`) runs `tools/check_erc.py`:

- it resolves `kicad-cli` portably — `--kicad-cli PATH`, then `KICAD_CLI`, then
  `PATH`, then the macOS bundle — and prints the tool version;
- it asks for **all three severities**: `--severity-error`,
  `--severity-warning` and `--severity-exclusions`. Asking only for
  `--severity-exclusions` reports the excluded findings and nothing else, so a
  report with real errors looks empty. That flag combination was a false pass
  and is now a regression test (`test_the_checker_requests_all_three_severities`);
- it runs ERC in a **private per-run scratch copy** of `tmp/`, so the frozen
  project is never modified, two runs cannot share a report, and a report from
  an earlier schematic can never be read as the current result;
- it **validates the report before believing it**: the `ERC report (...)` header,
  the `Report includes:` line (all three severities), at least one
  `***** Sheet` section, the closing
  `** ERC messages: N  Errors E  Warnings W` line, and counts that agree with
  the findings. A truncated, foreign or empty file is "cannot verify", never
  "0 findings";
- the process exit code is checked **independently** of the report: only 0 and 5
  (5 = `--exit-code-violations` found violations) are accepted, and no message
  text such as "Found 0 violations" can excuse another code;
- it repairs library-table **locations only, in the scratch copy**: entries that
  point into this repository's own `tmp/library` become `${KIPRJMOD}`, and the
  libraries the schematic references are looked up in the KiCad installation's
  own `symbols/` and `footprints/` (auto-detected, or `--kicad-share` /
  `FROEMIX_KICAD_SHARE`) and in `--libs` / `FROEMIX_KICAD_LIBS` (the conventional
  place is `~/.local/share/froemix/kicad-libs/`). Found libraries are appended to
  the private copy of the table; schematic bytes are never touched. Every
  library that cannot be found is printed with its URI and stays visible in the
  ERC report as an unexcluded finding — an unresolved environment is not hidden;
- it honours the project's ERC exclusions, reports the excluded count, and prints
  the unexcluded findings with their check name and position. A stale exclusion
  therefore shows up as an unexcluded finding.

Measured with genuine KiCad 10.0.7 (`kicad-cli version` = `10.0.7`, Debian sid
build) on Linux, with the official KiCad libraries and the genuine 4ms Eurorack
library available: **0 unexcluded findings, 9 excluded**. The two excluded
`pin_not_driven` entries are the approved Daisy Seed `AUDIO_IN_1`/`AUDIO_IN_2`
pins (driven through R9/R10, which ERC cannot trace through passives). KiCad 10
reports those pins with a per-pin marker identity, so `tmp/froemix.kicad_pro`
carries the exact identity:

```
pin_not_driven|1320800|469900|<pin UUID>|00000000-0000-0000-0000-000000000000|/root-sheet-UUID|/root-sheet-UUID|
```

No other exclusions were added, and no blanket filter exists: the identity needs
the pin UUID, the nil aux UUID and both sheet paths, and any other combination
leaves the finding unexcluded (verified by probing 20+ variants). With the older
symbol-UUID identity the two pins stayed unexcluded, which is how the false pass
went unnoticed.

`make gate` still accepts `FROEMIX_SKIP_ERC=1` (prototype machines without
KiCad) and prints that the result is INCOMPLETE. Every production entry point
refuses that variable.

The frozen KiCad reference is not the design; it is the port baseline. ERC on it
proves that the ported schematics still pass their own rules, not that the
tscircuit board is correct — that is what the netlist, footprints, extras,
values, DRC and silkscreen steps are for.

## How evidence gets produced

- The material for `dfm_cpl`, `stack_11mm`, `panel_rack` and `first_article` can
  only come from real material. Prototype builds stay outside this production
  path: export them by hand, mark them **"engineering prototype — not production
  ready"**, and keep them out of `fab/`.
- Engineering-prototype material does not satisfy `first_article` by existence:
  that item asks for a build inspected against the whole list.
- When every required item is accepted against the current fingerprint,
  `make release` passes the gate and produces the production order set.

## Recorded measurements vs requirements

| Measurement on record | Result |
|---|---|
| Physical bench measurements (any item) | none |
| Mechanical stack (11 mm spacers, tails, J1, sockets) | not measured; requirements only |
| Fasteners ≤ 5 mm OD | actual head/washer MPNs not selected |
| P5V rail ≤ 300 mA | budget only (`doc/POWER-BUDGET.md`); no loaded measurement |
| MIDI receiver threshold | datasheet limits only, no bench data |
| STM32H750 GPIO limits for the MIDI output (20 mA per pin, VOL 0.4 V, VOH, reset state) | explicit assumptions; primary ST datasheet not yet verified, so no absolute rating is claimed |
| ESD, ground/chassis tie | not measured; no decision recorded for the tie |
| DFM/CPL, first article | not done |
| KiCad ERC (prototype) | 0 unexcluded, 7 excluded, genuine KiCad 10.0.7 |
| SPICE (behavioural) | passes; not datasheet qualification |
| PCB geometry vs published JLC limits | passes (see `doc/JLC-ASSEMBLY.md`) |

# Verification and acceptance gates

Status: **prototype definition, not a fabrication release.** No physical
measurement has been recorded for this project yet. `make test` is prototype
verification; `make release` is the production path and it is gated.

## `make test` — prototype verification, not an approval

`make test` typechecks the tscircuit project, runs the Bun unit tests, checks
that `firmware/pico/pins.h` matches `lib/pin-map.ts`, builds and routes both
boards, runs the placement consistency check, parses the six `tsci check`
reports per board, checks geometry against the JLCPCB published limits, runs the
Python unit tests for those checks, runs the host C++ tests (framed codec,
clock emulation, clock follow, SPI slave, FX timing, link reliability,
midifx core/schema), runs the electrical checker and its negative tests, and
runs the mechanical freshness + geometry checks and the simplified ngspice
checks. The electrical and mechanical steps have their own targets
(`make electrical`, `make mechanical`) but are part of `make test`.
The mechanical target runs `bun run scripts/export-mechanical.ts --check`
(non-writing; fails when `mechanical/layout.json` is stale against
`lib/layout.ts`) and then the pure geometry tests in `mechanical/`. Regenerate
the export deliberately with `make mechanical-export` after an intentional
layout change. Full CAD stays optional and tool-dependent; the mechanical
acceptance items remain pending. Geometry and open gates:
[mechanical-stack.md](mechanical-stack.md).

What that *does* prove: the sources, netlist, placement, geometry, host code and
simplified SPICE models agree with each other, and the text of the tool reports
is what this project expects.

What it **cannot** prove: that a light pipe fits, that a cable bends, that a
receiver stays inside its recommended input current, that the stack is 110 mm
deep, that the encoder survives the soldering process, that JLCPCB's CAM matches
the Gerbers, or that a first article works. The SPICE models are behavioural /
resistor-network checks, **not** datasheet-accurate models or hardware
measurements. "No test failed" never means "the hardware is approved".

## `make release` — production path, gated and fail-closed

`tscircuit/scripts/release.sh` does this, in this order:

1. `bun run scripts/release_gate.ts` — the physical acceptance gate. It reads
   `hardware-acceptance.json`, validates every required item, recomputes the
   hardware fingerprint and **exports nothing and deletes nothing** when an item
   is pending or invalid. `fab/` is not touched.
2. `make test` plus `fab_check.py --strict-targets`.
3. Only then `rm -rf fab/` and the Gerber/STEP/SVG exports.

There is no `--force`, `--skip`, `--allow-unverified` or `override` key: unknown
keys and override-like keys are rejected by the validator, so no blanket flag can
clear unresolved hardware. A pending item is normal and expected today; the
release stops there.

## Machine-readable acceptance state

`hardware-acceptance.json` holds one entry per required item with
`state: "pending" | "accepted"`. An accepted item needs:

| Field | Requirement |
|---|---|
| `evidence` | a Markdown file directly in `docs/evidence/` (relative path, no `..`, no absolute path, no symlink, not empty) |
| `reviewer` | a named person, not `TBD`/`n/a` |
| `reviewed_at` | a real date `YYYY-MM-DD`, not in the future |
| `source_fingerprint` | equal to the state's `hardware_fingerprint`; the evidence must name the item and contain at least one recorded value with a unit (for example `12.4 mm`) |
| `evidence_sha256` | optional; when present the file content must match |

The **hardware fingerprint** is a SHA-256 over the canonical PCB/panel sources
(`tscircuit/*.circuit.tsx`, `lib/layout.ts`, `lib/pin-map.ts`, `lib/jack.tsx`,
`lib/panel-art.ts`, `imports/*.tsx`, `package.json`, `bun.lock`), the
mechanical TS-to-CAD bridge (`tscircuit/scripts/export-mechanical.ts`,
`mechanical/layout.json`), the CAD algorithm (`mechanical/feedthrough_bracket.py`,
`mechanical/bracket_geometry.py`), the fabrication order settings
(`jlc-order.json`) and the curated cable/hardware selection
(`tscircuit/scripts/sourcing-extras.csv`). A change to any of those makes old
evidence stale, so a report from a previous revision cannot approve changed
hardware: a CAD-algorithm, order-setting or part-selection change can move a
physical or DFM result even when `lib/layout.ts` is identical. A documentation-only commit — including the acceptance record itself —
does not change the fingerprint. A missing hardware source fails the gate
instead of being skipped. Print it with
`bun run scripts/release_gate.ts --fingerprint`. Format and template:
[docs/evidence/README.md](evidence/README.md).

## Required items (all currently pending)

| Item | What the evidence must show |
|---|---|
| `zstack` | Board, standoff, display, Pi, HAT, cooler and panel thicknesses that add up; installed depth recorded |
| `control_actuator` | Knob, button caps and 5-way nav actuator fitted and measured against the panel; direction legends |
| `jack_retention` | TRS barrel/nut/panel-hole fit, tip/ring/sleeve continuity, pull-out retention |
| `light_pipes` | Fitted PGN25L165 (candidate) seated plane, measured LED-to-pipe distance and LED visible through each of the three Ø2.6 mm nominal holes |
| `display_dsi` | Waveshare 4.3" DSI panel mounted at the fitted SMTSO boss pattern with one of the kit DSI cables (50 mm or 120 mm 15→22) routed; bend radius, latch and underside clearance |
| `display_boss_pattern` | The module's own SMTSO-M2.5 bosses (99.30 × 61.80 pattern) confirmed as the only threading; 9.0 mm plane separation = 4.0 mm boss + 5.0 mm spacer measured |
| `display_fpc_idc` | Display FPC plug/cable coexist with the front board's top-side parts: Z clearances, the routed bend per the route contract in mechanical-stack.md, and the clearance to the J5 female solder tails. The 2 × 13 link is a direct pair, so there is no plug or ribbon |
| `interconnect_mate` | Direct 2 × 13 pair fitted and measured **before any power**: 11.20 ±0.20 mm seat-to-seat gap with the support recorded, keyed in one orientation only, all 26 contacts continuity-tested pin for pin against the net list, the Ø1.00 mm drill recorded as the male recommendation and a provisional female assumption, and the male's tail recorded as 3.00 mm from the connector seating face, i.e. a nominal 1.40 mm protrusion beyond a 1.60 mm PCB, with the solder fillet and the actual clearance to the Pi PCB and the candidate 3.5 mm spacer measured (the spacer is not a safety claim) |
| `module_label_sku_read` | The delivered module label/SKU read and matches the selected Waveshare 4.3inch DSI LCD (ElectroKit 4103 5687) |
| `touch_window_alignment` | With the display seated in the 107.22 × 68.87 window, touch input stays aligned with the UI across the active area |

| `pi_microsd` | microSD insert/remove with the Pi in the stack and the case closed; clearance recorded |
| `cooler_hat_ssd` | Active Cooler + M.2 HAT+ + SSD stacked; height, airflow, connector clearance |
| `case_fit` | 30HP/3U panel (152.0 mm), 20HP base board and all rail/standoff positions in the case |
| `usb_c_5a_path` | An exact 5 A rated feedthrough from a primary source; CAB-15455 is 3 A and rejected for this path |
| `usb_pd_droop_thermal` | Full-load PD negotiation, voltage droop, connector/feedthrough temperature, both orientations |
| `encoder_solder_process` | Bourns PEC11R process qualified (selective/wave or validated manual) with profile and inspection |
| `midi_threshold` | Receiver threshold measured against the MIDI CA-033 receiver requirement (<5 mA at 5 V) at the recorded temperature |
| `midi_receiver_high_current_thermal` | TLP2361 input current and temperature at worst-case high-rail/low-VF corners with the recorded ambient: recommended 2–6 mA, absolute continuous 10 mA **at 25 °C**, derated −0.13 mA/°C above 110 °C (8.05 mA at 125 °C); the 220 Ω choice justified by measurement |
| `midi_out_legacy` | Legacy receiver loads: low-rail current, rise/fall < 2 µs, sustained ring/tip fault temperatures; independent output-topology review |
| `midi_interop` | Two real MIDI devices exchange note, clock and sysex in both directions |
| `midi_current_faults` | Short, open, reverse and over-current cases on both inputs and outputs |
| `scope_spi` | SPI0 clock/data and READY capture at the target rate, including Pico-USB and Pi-off cases |
| `scope_power_sequences` | Power-up/down order of Pi, Pico, display and 3.3 V rails; no backfeed or latch-up |
| `scope_thermal` | Closed-rack soak at recorded ambient and load; cooler noise and NVMe temperature |
| `probe_point_access` | Each rear-board probe point TP1–TP15 reached with the intended probe and read on its declared net, with the assembly state recorded per pad (front board fitted/off, Pi fitted/removed, Pico socketed, Pico USB cable fitted/not) and every obstruction noted. In-rack access is not assumed |
| `abrupt_power_loss` | Abrupt power removal and re-application on the full stack: rail decay order and time, Pico/Pi/display state, SD-card or NVMe write integrity after a loss during a recording, and any backfeed or latch-up seen |
| `scope_dfm_cpl` | JLCPCB DFM/order-preview review of copper, mask, silk, drill and CPL rotation on prototype material |
| `autoroute_upstream_warning` | The tscircuit "Could not save autorouting paths" warning resolved or its effect on the copper reviewed |
| `first_article` | First-article boards built, inspected and tested against this list |

Requirements are not evidence. The table states what to record; the state file
holds what has actually been recorded, which is nothing yet.

## Reviewed engineering prototype (how the evidence gets produced)

> **Superseded 2026-11-06 (owner decision): the prototype ceremony is
> retired.** Boards are ordered production-intent through `make order`
> (no decision rows, no prototype labeling). This gate remains the
> ACCEPTANCE HARNESS: its items are closed by measurements recorded in
> `docs/evidence/` as the hardware is built and exercised — the boards
> ordered today are what produce that evidence.

The DFM/CPL and first-article evidence can only come from prototype material, so
the project needs an explicit prototype path that is *not* the production
release. Policy:

- An **engineering prototype build** is a separate, owner-approved activity. The
  owner records the approval as a dated decision in [decisions.md](decisions.md)
  (who, why, what is being built, what is explicitly *not* claimed). No tool flag
  grants it, and `make release` does not accept it.
- A prototype may be ordered only with the outputs marked **"engineering
  prototype — not production ready"** in the order notes and in the record.
- The prototype provides the material for `scope_dfm_cpl`, `zstack`, `case_fit`
  and `first_article` evidence; its inspection results are recorded in
  `docs/evidence/` like any other measurement.
- Engineering-prototype material does **not** satisfy `first_article` by
  existence: `first_article` asks for a build inspected against the full list.
  Until then, nothing is called production-ready.
- When every required item is accepted against the current fingerprint,
  `make release` passes the gate and produces the production order set.

The exports that a prototype would need are the same `bunx tsci export` commands
used in `tscircuit/scripts/release.sh` (gerbers, step, schematic-svg, pcb-svg per
entry). Run them by hand under the recorded approval, and keep the outputs out of
the production `fab/` set.

## Recorded measurements vs requirements

| Measurement on record | Result |
|---|---|
| Physical bench measurements (any item) | none |
| Mechanical stack depth | not measured; ≤110 mm remains a requirement |
| USB-C 5 A path | no 5 A rated feedthrough selected; CAB-15455 is 3 A |
| TLP2361 receiver current/temperature | datasheet limits only, no bench data |
| MIDI interoperability | not tested with real devices |
| SPICE (behavioural, baseline) | passes; not datasheet qualification |
| PCB geometry vs published JLC limits | passes, with two open TARGET items (see [fab-check.md](fab-check.md)) |

## Actual physical blockers (unchanged)

- 3D envelope and service access: no top/rear/side model, no cable bend radii and
  no measured installed depth; the Pi, HAT, cooler and SSD stack is unproven.
- Display: sample not bought; rear connector envelope, cable path and mounting
  bosses unmeasured.
- Controls: knob, button caps and nav actuator not selected against the panel.
- Jacks: tip/ring/sleeve mapping and retention not measured on a sample.
- USB-C: 5 A feedthrough not selected; PD behaviour, droop, thermal and strain
  relief unmeasured.
- MIDI: TLP2361 threshold and high-current/temperature corners unmeasured;
  interoperability and fault cases unmeasured.
- Encoder: qualified solder process not chosen.
- Assembly: DFM/CPL review and first article not done.

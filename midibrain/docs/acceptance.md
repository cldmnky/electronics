# Verification and acceptance gates

Status: **prototype definition, not a fabrication release.** No physical
measurement has been recorded for this project yet. `make test` is prototype
verification; `make release` is the production path and it is gated.

## `make test` — prototype verification, not an approval

`make test` typechecks the tscircuit project, runs the Bun unit tests, checks
that `firmware/pico/pins.h` matches `lib/pin-map.ts`, builds and routes both
boards, runs the placement consistency check, parses the six `tsci check`
reports per board, checks geometry against the JLCPCB published limits, runs the
Python unit tests for those checks, runs the host C++ framed-codec/debounce
test, and runs the simplified ngspice checks. The electrical checker and its
negative tests plus the mechanical steps are separate targets: they are owned by
the electrical and mechanical work items. The mechanical target runs
`bun run scripts/export-mechanical.ts --check` (non-writing; fails when
`mechanical/layout.json` is stale against `lib/layout.ts`) and then the pure
geometry tests in `mechanical/` (`python3 -m unittest discover -s mechanical -p
"test_*.py"`). Regenerate the export deliberately with `make mechanical-export`
after an intentional layout change. Full CAD stays optional and tool-dependent;
the mechanical acceptance items remain pending. Details:
`docs/MECHANICAL-VERIFICATION.md` §7 (mechanical work item).

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
| `light_pipes` | Fitted PLP2-400-C length and LED-to-pipe gap; LED visible through each of three holes |
| `display_dsi` | Waveshare 3.5" DSI panel mounted with the 12 cm DSI cable routed; bend radius and latch |
| `pi_microsd` | microSD inserted/removed with the Pi stacked and the case closed |
| `cooler_hat_ssd` | Active Cooler + M.2 HAT+ + SSD stacked; height, airflow, connector clearance |
| `case_fit` | 26HP/3U panel, 20HP base board and all rail/standoff positions in the case |
| `usb_c_5a_path` | An exact 5 A rated feedthrough from a primary source; CAB-15455 is 3 A and rejected for this path |
| `usb_pd_droop_thermal` | Full-load PD negotiation, voltage droop, connector/feedthrough temperature, both orientations |
| `encoder_solder_process` | Bourns PEC11R process qualified (selective/wave or validated manual) with profile and inspection |
| `midi_threshold` | Receiver threshold measured against the MIDI CA-033 receiver requirement (<5 mA at 5 V) at the recorded temperature |
| `midi_receiver_high_current_thermal` | TLP2361 input current and temperature at worst-case high-rail/low-VF corners (recommended 2–6 mA, absolute continuous 10 mA, derating above 110 °C) |
| `midi_interop` | Two real MIDI devices exchange note, clock and sysex in both directions |
| `midi_current_faults` | Short, open, reverse and over-current cases on both inputs and outputs |
| `scope_spi` | SPI0 clock/data and READY capture at the target rate, including Pico-USB and Pi-off cases |
| `scope_power_sequences` | Power-up/down order of Pi, Pico, display and 3.3 V rails; no backfeed or latch-up |
| `scope_thermal` | Closed-rack soak at recorded ambient and load; cooler noise and NVMe temperature |
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

# mnkybox

A planned **32HP/3U** Eurorack recorder and MIDI hub (module name **mnkybox**; `midibrain/` stays the project/repo identifier) using a removable Raspberry Pi 5 and Pico 2, a **128 × 112 mm front/UI PCB**, a **20HP (101.6 × 112 mm) rear/base PCB**, and a separate front panel. Manufacturing target: **JLCPCB fabrication and SMD assembly, with SMD parts sourced from JLCPCB/LCSC**; through-hole parts are hand-soldered, except the Bourns PEC11R encoder, which needs a qualified THT process (hand soldering is not recommended). The boards are fully designed and routed; the mechanical stack, cable envelopes and physical first article are open acceptance items (tracked in `hardware-acceptance.json`, which `make release` enforces). The companion software -- Pi DAW app (Qt 6 QML), Pico 2 firmware and the shared MIDI FX core -- is built and running on a studio Pi 5 with a UMC1820; see [docs/USER-MANUAL.md](docs/USER-MANUAL.md) for the instrument and [pi/README.md](pi/README.md) for the code.

The user-supplied original front-panel reference is [`assets/front-panel-concept.png`](assets/front-panel-concept.png). The new [`assets/front-panel.svg`](assets/front-panel.svg) ([PNG snapshot](assets/front-panel-preview.png)) is generated from tentative centres in `tscircuit/lib/layout.ts`: REC/OVER DUB/STOP/PLAY/PLAY START below the 4.3-inch landscape display, with the REC/OVER DUB/PLAY light pipes beside their buttons; VALUE/NAV to its right, and SHIFT/EXIT below NAV. It is illustrative, **not a cutting template**. Waveshare 4.3inch DSI LCD (ElectroKit 4103 5687) is the selected display. It uses the Pi 5 DSI1 connector, 3.3 V and I2C touch on one 15-pin cable; no HDMI or touch-USB cable. The LCD mounts on the front board at the module's own SMT-standoff pattern (SMTSO-M2.5 bosses, 99.30 × 61.80 mm centres; the drawn 58 × 49.04 mm pattern is plain FR-4 and stays recorded as the alternative); the FR-4 front panel has a 107.22 × 68.87 mm window sized from the module's 105.42 × 67.07 mm glass front face and 4 mm light-pipe holes beside the three 0805 LEDs. Real mounting and cable envelopes still require validation.

## Reproduce current checks

Requires Bun 1.3.3, a C++20 compiler and `make`:

```sh
cd midibrain/tscircuit
bun install --frozen-lockfile
cd ..
make test
```

`make test` is **prototype verification, not a production approval**. It typechecks the tscircuit project, runs the Bun unit tests, checks `pins.h` against `lib/pin-map.ts`, builds and routes both boards, runs the placement consistency check, parses the six `tsci check` reports per board against pinned expectations, compares geometry with the published JLCPCB limits, runs the Python tests for those checks, runs a **host-only** framed-message/debounce simulation, and runs ngspice checks for MIDI loop current/polarity, Pi/Pico power backfeed, reset and SPI-buffer enable. The SPICE models are simplified behavioural checks, **not** datasheet-accurate models or hardware measurements. Nothing physical is measured. The electrical checker/negative tests and the mechanical bridge check are separate targets owned by the electrical and mechanical work items: the mechanical step runs `bun run scripts/export-mechanical.ts --check` (non-writing, fails when the exported `mechanical/layout.json` is stale) plus the pure geometry tests in `mechanical/`; `make mechanical-export` regenerates deliberately after a layout change. Full CAD stays optional and tool-dependent, and no mechanical or CAD evidence exists yet. See [docs/acceptance.md](docs/acceptance.md) and [docs/fab-check.md](docs/fab-check.md).

`make release` is the **production** path. It first runs the physical acceptance
gate (`midibrain/hardware-acceptance.json` + `docs/evidence/`, bound to a hash of
the PCB/panel sources, the CAD algorithm, the order settings and the cable/hardware
selection) and **stops before deleting or exporting anything** while
a required item is pending — which is every item today. There is no override
flag. Only after the gate passes does it export per-board Gerbers/STEP/SVG plus
JLCPCB BOM/CPL sets into `fab/` (wiped on every run, never hand-edit), verify the
placement counts against `jlc-order.json` (front 31 SMT, rear 20 SMT), and merge
the bench rows with the curated module/cable/hardware list into
`fab/sourcing-bom.csv`. Prototype outputs, not a fabrication release — see
[docs/JLC-ASSEMBLY.md](docs/JLC-ASSEMBLY.md) and recheck all LCSC stock at order
time. The encoder needs a qualified THT process (hand soldering is not
recommended), and SparkFun CAB-15455 is rejected for the 5 A USB-C path.

- [Requirements and unresolved decisions](docs/requirements.md)
- [Decisions and source register](docs/decisions.md)
- [Mechanical stack](docs/mechanical-stack.md)
- [Pin map and interface](docs/pin-map.md)
- [Protocol demonstration and limitations](docs/protocol.md)
- [Parts, cable and assembly shortlist](docs/assembly.md)
- [USB feedthroughs, panel cutouts and strain relief](docs/feedthroughs.md)
- [JLCPCB DRC/ERC and fabrication check](docs/fab-check.md)
- [SPICE design checks and results](docs/spice.md)
- [Acceptance and next steps](docs/acceptance.md)
- [Evidence format and hardware fingerprint](docs/evidence/README.md)

Prototype PCB renders: [front/UI](assets/front-pcb.png) and [20HP base](assets/base-pcb.png). These are routed design artifacts, not fabrication releases; the autorouter output has not been reviewed for manufacturing or signal integrity.

`make order` exports the three-board JLCPCB upload set and prints the order sheet (fab intent = production); `make release` remains the fail-closed acceptance harness. The encoder needs a qualified THT process; SparkFun CAB-15455 is rejected for the 5 A USB-C path.
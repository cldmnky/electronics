# mnkybox

A **30HP/3U** Eurorack recorder and MIDI hub (module name **mnkybox**;
`midibrain/` stays the project/repo identifier): a removable Raspberry Pi 5
runs the DAW app, a socketed Pico 2 owns MIDI, the panel and the master clock,
and a UMC1820 (plus ADA8200 over S/PDIF) provides 18 inputs at 48 kHz/24-bit.
The front/UI PCB is **128 × 112 mm**, the rear/base PCB is **20HP
(101.6 × 112 mm)**, the panel is a separate 30HP (152.0 mm) FR-4 board. The display is the
Waveshare **4.3inch DSI LCD** (ElectroKit 4103 5687), 800 × 480 with DSI video
and I2C touch on one 15-pin cable.

Manufacturing target: **JLCPCB fabrication and SMD assembly**, SMD parts from
JLCPCB/LCSC. Through-hole parts are hand-soldered except the Bourns PEC11R
encoder, which needs a qualified THT process (Bourns does not recommend hand
soldering). The boards are designed and routed; the mechanical stack, cable
envelopes and first article are **not** physically verified. Every physical
acceptance item is still pending in `hardware-acceptance.json`, and
`make release` is fail-closed on it.

The Pi app, Pico firmware and shared MIDI FX core are prototype software: they
build and pass host tests, and the app has run offscreen/headless on a studio
Pi 5 with a UMC1820 (no Pico attached). Nothing has been validated on the
physical panel, with a Pico over SPI, or for audio timing. See
[docs/TODO.md](docs/TODO.md) for every known gap.

## Repo map

| Path | What it is |
|---|---|
| `tscircuit/` | Canonical PCB/panel sources (`lib/layout.ts` is the single coordinate source), build, placement and check scripts |
| `mechanical/` | Generated `layout.json`, bracket CAD and pure geometry tests |
| `spice/` | Behavioural ngspice checks (MIDI loop, power backfeed, reset, SPI buffers) |
| `firmware/pico/` | RP2350 firmware: framing, MIDI parser, deadline clock, priority TX, SPI slave |
| `midifx/` | Shared MIDI FX core + `pipes.json` schema, compiled by firmware and Pi |
| `pi/` | Pi software: app (Qt 6 QML), audio engine, CLAP host, link, session, bench, updater |
| `brain-os/` | Parked image notes; not part of the current build |
| `assets/` | Front-panel concept/render and display reference drawing |
| `docs/` | The documentation set indexed below |

## Build and verify

Requires Bun 1.3.3, a C++20 compiler, Python 3 and `make`.

```sh
cd midibrain/tscircuit && bun install --frozen-lockfile && cd ..
make test          # prototype verification: typecheck, Bun/Python/host C++ tests,
                   # build + route, placement, six tsci reports per board, JLC
                   # geometry, electrical checks, mechanical freshness + geometry,
                   # shared midifx suite, simplified SPICE
```

`make test` proves the sources, netlist, placement, geometry, host code and
simplified SPICE models agree with each other. It does **not** prove a light
pipe fits, a cable bends, a receiver stays inside its current limit, the stack
depth, the encoder solder process, JLCPCB's CAM or a first article. The SPICE
models are behavioural, not datasheet models.

```sh
make gate          # physical acceptance state + hardware fingerprint
make order         # export the three-board JLCPCB upload set + order sheet
make release       # production path: gate first (fail-closed), then the same pipeline
make firmware      # Pico 2 UF2 (needs PICO_SDK_PATH; see firmware/README.md)
```

`make release` stops before deleting or exporting anything while a required
item is pending — which is every item today. There is no override flag.
Ordering through `make order` is not blocked by the gate and does not weaken
it. The encoder needs a qualified THT process; SparkFun CAB-15455 is 3 A and
rejected for the 5 A USB-C path.

## Documentation

Start with the [user manual](docs/USER-MANUAL.md) for the instrument and
[pi/README.md](pi/README.md) for the code. The rest:

- [TODO — missing functionality](docs/TODO.md) — the single gap list
- [Protocol and wire contract](docs/protocol.md)
- [Pin map and interfaces](docs/pin-map.md)
- [Decisions and sources](docs/decisions.md)
- [Requirements baseline](docs/requirements.md)
- [Mechanical stack and display](docs/mechanical-stack.md)
- [Parts, cables and assembly](docs/assembly.md)
- [JLCPCB order packet](docs/JLC-ASSEMBLY.md)
- [Fabrication check](docs/fab-check.md)
- [Electrical verification record](docs/ELECTRICAL-VERIFICATION.md)
- [SPICE checks](docs/spice.md)
- [Verification and acceptance gates](docs/acceptance.md)
- [MIDI-clock bench criteria](docs/clock-bench.md)
- [Panel interaction model](docs/panel-interaction.md)
- [UI refresh spec](docs/ui-refresh-spec.md)
- [Physical touch-testing checklist](docs/touch-testing-checklist.md)
- [Evidence format and hardware fingerprint](docs/evidence/README.md)

Prototype PCB renders: [front/UI](assets/front-pcb.png) and
[20HP base](assets/base-pcb.png). The panel artwork
([`assets/front-panel.svg`](assets/front-panel.svg),
[PNG](assets/front-panel-preview.png)) is generated from tentative centres in
`tscircuit/lib/layout.ts`; it is illustrative, **not a cutting template**.
These are design artifacts, not fabrication releases.

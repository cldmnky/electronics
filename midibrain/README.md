# mnkybox

A planned **26HP/3U** Eurorack recorder and MIDI hub (module name **mnkybox**; `midibrain/` stays the project/repo identifier) using a removable Raspberry Pi 5 and Pico 2, a **128 × 112 mm front/UI PCB**, a **20HP (101.6 × 112 mm) rear/base PCB**, and a separate front panel. Manufacturing target: **JLCPCB fabrication and SMD assembly, with SMD parts sourced from JLCPCB/LCSC**; through-hole parts are hand-soldered. This directory is a **work in progress, not a fabrication release**. Both tscircuit boards now contain prototype schematics, placed components and routed copper, but the mechanical stack, cable envelopes, manufacturing review and physical first article are unresolved. Do not manufacture or order them yet.

The user-supplied original front-panel reference is [`assets/front-panel-concept.png`](assets/front-panel-concept.png). The new [`assets/front-panel.svg`](assets/front-panel.svg) ([PNG snapshot](assets/front-panel-preview.png)) is generated from tentative centres in `tscircuit/lib/layout.ts`: REC/OVER DUB/STOP/PLAY/PLAY START below the 3.5-inch landscape display, with LEDs above REC/OVER DUB/PLAY; TEMPO/NAV to its right, and SHIFT/EXIT below NAV. It is illustrative, **not a cutting template**. Waveshare 3.5inch DSI LCD (H), SKU 33087, is the selected display candidate. It uses the Pi 5 DSI1 connector, 3.3 V and I2C touch on one 15-pin cable; no HDMI or touch-USB cable. The LCD mounts on the front board at the vendor’s asymmetric 58 × 49 mm hole pattern; the FR-4 front panel has an 89.2 × 56.3 mm cutout and 4 mm light-pipe holes above the three 0805 LEDs. Real mounting and cable envelopes still require validation.

## Reproduce current checks

Requires Bun 1.3.3, a C++20 compiler and `make`:

```sh
cd midibrain/tscircuit
bun install --frozen-lockfile
cd ..
make test
```

`make test` builds and routes both boards, checks provisional display/control centre relationships and pin allocations, typechecks the tscircuit project, runs a **host-only** framed-message/debounce simulation, and runs ngspice checks for MIDI loop current/polarity, Pi/Pico power backfeed, reset and SPI-buffer enable. The SPICE models are simplified design checks, not datasheet-accurate or hardware validation. It does not measure physical controls, timing, EMC or component fit. See [docs/acceptance.md](docs/acceptance.md).

`make release` runs the full gate, then exports per-board Gerbers/STEP/SVG plus JLCPCB BOM/CPL sets into `fab/` (wiped on every run, never hand-edit) and merges the bench hand-solder rows with the curated module/cable/hardware list into `fab/sourcing-bom.csv`. Prototype outputs, not a fabrication release — see [docs/JLC-ASSEMBLY.md](docs/JLC-ASSEMBLY.md) and recheck all LCSC stock at order time.

- [Requirements and unresolved decisions](docs/requirements.md)
- [Decisions and source register](docs/decisions.md)
- [Mechanical stack](docs/mechanical-stack.md)
- [Pin map and interface](docs/pin-map.md)
- [Protocol demonstration and limitations](docs/protocol.md)
- [Parts, cable and assembly shortlist](docs/assembly.md)
- [USB feedthroughs, panel cutouts and strain relief](docs/feedthroughs.md)
- [JLCPCB DRC/ERC and fabrication check](docs/fab-check.md)
- [SPICE design checks and results](docs/spice.md)
- [Design review: mechanical/electrical blockers and decisions](docs/design-review.md)
- [Acceptance and next steps](docs/acceptance.md)

Prototype PCB renders: [front/UI](assets/front-pcb.png) and [20HP base](assets/base-pcb.png). These are routed design artifacts, not fabrication releases; the autorouter output has not been reviewed for manufacturing or signal integrity.

No recorder, real MIDI interfaces, Pico firmware, Pi SPI driver or finished schematics have yet been implemented. Do not confuse successful build or tests with first-milestone completion.

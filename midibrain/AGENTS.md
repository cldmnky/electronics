# MIDI Brain project contract

- All implementation and generated local artifacts stay in `midibrain/`; do not modify Froemix.
- `tscircuit/` is the canonical PCB source. The current project is an **exploratory mechanical skeleton, not a manufacturable PCB**. Do not order it.
- Dimensions and placement candidates must cite primary drawings; do not invent footprints, part numbers, pinouts or assembly assignments. Unverified dimensions and missing collision data stay explicitly pending.
- `tscircuit/lib/layout.ts` is the single source of tentative panel and mechanical coordinates. The panel mockup is generated from it and is not a production drawing.
- Pi drawing dimensions are reference-only; do not treat the skeleton's hole positions as production geometry. The complete display, cable, USB-C power, Pi, cooling and NVMe stack must be proven before routing or release.
- Honor the owner-approved 26HP/3U front panel, 20HP (101.6 mm) base board and <=110 mm installed depth; document conflicts rather than silently expanding any of them. The target architecture is a 128 x 112 mm front/UI PCB, a 101.6 x 112 mm rear/base PCB and a separate panel.
- No bare microcontroller or unidentified navigation footprint. Pico and Pi are removable. Never couple their 3.3 V regulator outputs; power/backfeed isolation is unresolved.
- UART0 and UART1 are reserved for MIDI; SPI0 and READY are separate. Pin allocation is in `docs/pin-map.md` and `tscircuit/lib/pin-map.ts`; change both together.
- Do not claim a signal-integrity, timing, collision, power, manufacturing or hardware test has passed without the actual measurement. A successful skeleton build is not a validated electrical design.
- Manufacturing target: JLCPCB PCB fabrication and SMD assembly. Every SMD line item must have an LCSC/JLCPCB C-number, verified package/footprint and explicit assembly side. Do not add an SMD part that cannot be sourced from JLCPCB; through-hole parts may be hand-soldered unless explicitly assigned. Record stock/package verification and do not treat a catalog C-number as proof of fit.
- Use the documented tscircuit APIs and router. Pin dependencies and commit the lockfile; do not manually modify generated fab output. No fabrication export or purchase until the documented gates are resolved.
- Use `make test` for the current reproducible software/layout checks. It is a milestone check, not a manufacturing gate.

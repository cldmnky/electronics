# MIDI Brain project contract

- All implementation and generated local artifacts stay in `midibrain/`; do not modify Froemix.
- `tscircuit/` is the canonical PCB source. The hardware has passed a production-readiness review (GO for fab and assembly, conditional on the first-article checklist in `docs/acceptance.md` + `hardware-acceptance.json`). Never bypass the release gate (`make release` refuses while any acceptance item is pending).
- Dimensions and placement candidates must cite primary drawings; do not invent footprints, part numbers, pinouts or assembly assignments. Unverified dimensions and missing collision data stay explicitly pending.
- `tscircuit/lib/layout.ts` is the single source of tentative panel and mechanical coordinates. Panel coordinates, the display window (glass-derived) and control positions come from it only.
- Pi drawing dimensions are reference-only; do not treat the skeleton's hole positions as production geometry. The complete display, cable, USB-C power, Pi, cooling and NVMe stack must be proven before routing or release.
- Honor the owner-approved 30HP/3U front panel (152.0 mm actual; owner decision 2026-11-08, replacing the 32HP/162.56 mm panel of 2026-11-07), 20HP (101.6 mm) base board and <=110 mm installed depth; document conflicts rather than silently expanding any of them. The target architecture is a 128 x 112 mm front/UI PCB, a 101.6 x 112 mm rear/base PCB and a separate panel.
- No bare microcontroller or unidentified navigation footprint. Pico and Pi are removable. Never couple their 3.3 V regulator outputs; power/backfeed isolation is unresolved.
- UART0 and UART1 are reserved for MIDI; SPI0 and READY are separate. Pin allocation is in `docs/pin-map.md` and `tscircuit/lib/pin-map.ts`; change both together.
- Do not claim a signal-integrity, timing, collision, power, manufacturing or hardware test has passed without the actual measurement. A successful skeleton build is not a validated electrical design.
- Manufacturing target: JLCPCB PCB fabrication and SMD assembly. Every SMD line item must have an LCSC/JLCPCB C-number, verified package/footprint and explicit assembly side. Do not add an SMD part that cannot be sourced from JLCPCB; through-hole parts may be hand-soldered unless a qualified process is required (the encoder: hand soldering is not recommended). Record stock/package verification and do not treat a catalog C-number as proof of fit.
- Use the documented tscircuit APIs and router. Pin dependencies and commit the lockfile; do not manually modify generated fab output. No production fabrication export or purchase until the documented gates are resolved. A separately recorded, owner-approved engineering prototype follows the explicit process below; it is not a production approval.
- Use `make test` for the current reproducible software/layout checks. It is a milestone check, not a manufacturing gate.
- `make test` is prototype verification: it proves the sources, reports, geometry, host code and simplified SPICE models agree. It does not approve hardware.
- `make release` is the production path and runs the physical acceptance gate (`hardware-acceptance.json` + `docs/evidence/`) **before** it deletes or exports anything in `fab/`. While one required item is pending, the release stops and `fab/` is untouched. Never add an override/force/skip flag or state key, and never weaken the gate to make an export possible.
- Acceptance evidence is measured, names a reviewer and date, records at least one value with a unit, and is bound to the hardware fingerprint (a hash of the canonical PCB/panel sources, the CAD algorithm, `jlc-order.json` and `sourcing-extras.csv`). Requirements are not evidence. Never invent, copy or extrapolate bench data.
- Keep third-party primary sources compact: URL, retrieval date, SHA-256 and the quoted specification. Do not commit whole third-party documents.
- An **engineering prototype** build is a separate, owner-approved decision recorded in `docs/decisions.md`. It does not pass the gate, its outputs are labelled "engineering prototype — not production ready", and it supplies material for the DFM/CPL and first-article evidence. It never makes a board production-ready.
- Placement counts in `jlc-order.json` are verified against the generated BOM/CPL on every release; update them from the generated output, never by guessing.
- `sourcing-extras.csv` is parsed strictly: 10 columns on every row, real LCSC C-numbers (`C` + digits) for SMT. A shifted row hides a sourcing warning, so it fails the release.

## Build and gates (software)

- Build the Pi app with `/usr/bin/c++` (bare `c++` on macOS is broken nix GCC).
  Qt **6.4 subset only** - the Pi guest runs 6.4.2.
- A fresh worktree must run `bun install && bun run build` in `tscircuit/`
  before `make test` (its typecheck/dist steps run first and fail otherwise).
- NEVER trust a test before a fresh build (the stale-binary trap). The
  definition of done for any `pi/` change: zero-warning build
  (`-Wall -Wextra -Werror`), `ctest` all pass, `--unittests`,
  `--ui-taps`, `--panel-taps` (all with `QT_QPA_PLATFORM=offscreen`),
  `--screenshots` where visuals changed. Hardware changes: `make test`
  AND `make order` end to end.

## Code laws

- A C++ method called from QML must be `Q_INVOKABLE` (this bug class
  recurred three times - check twice).
- Never hold model pointers across rescans (use-after-free / SIGBUS
  class); copy values out first.
- `firmware/pico/protocol.hpp` is the frozen wire contract; frame
  layouts do not change casually, and `midifx/` is compiled by BOTH the
  firmware and the pi app - one change, two builds.
- Preserve every `focusId` you touch and maintain the hardcoded
  `rebuildFocusIds()` ring list (it is not a dynamic scan).
- Comments and docs state what the code does TODAY - no wave/stage/bug
  history. Missing functionality goes to `docs/TODO.md` (the single
  home). Documentation claims are grounded in the code, never
  aspirational.
- UI sizing and interaction law: `docs/ui-refresh-spec.md` (touch
  targets, control vocabulary) and `docs/panel-interaction.md` (panel =
  touch parity). ASCII strings only.

## Workflow

- One writer per directory/worktree. Keep ALL writes inside your own
  worktree (tool calls default to the main repo cwd - a stray write
  there blocks worktree creation for everyone).
- Children never commit: the parent verifies (build=0 and every gate)
  and commits/pushes. History is GPG-signed - never commit unsigned
  without explicit owner approval.
- Lane budget is ~30 minutes: stop cleanly and write a recovery summary
  before the deadline; never leave half-edits. Completed/failed runs
  auto-save their diff under the run's `worktree-diffs/` - capture it to
  `/tmp` immediately (retention is not forever).
- Untracked files block `worktree: true` (commit them first).
- Froemix is an active order: read-only, always. Never hand-edit
  generated fab output; `jlc-order.json` counts come from generated
  output only.

## Pointers

- Studio Pi (the owner's live machine + ARM verify host): `pi/STUDIO-PI.md`
  - never drive the real desktop; `pkill -x mnkybox`; nice/ionice.
- What is missing: `docs/TODO.md`. What was decided: `docs/decisions.md`.

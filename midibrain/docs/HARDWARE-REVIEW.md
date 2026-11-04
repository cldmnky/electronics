# Hardware review remediation

This change addresses the MIDI Brain tscircuit review. It does not certify an
assembled product. The original review identified physical pin-mapping defects,
MIDI current-margin risk, missing buffer bypassing, mechanical inconsistencies,
and gaps in the release checks.

## Scope

- Change MIDI Brain only. Keep the 26HP/3U panel, 128 × 112 mm front PCB,
  101.6 × 112 mm rear PCB, and the ≤110 mm installed-depth requirement.
- Preserve Type A MIDI polarity: ring is DIN 4/current source; tip is DIN 5/
  current return. The contrary review suggestion was withdrawn after checking
  the MIDI Association's published wiring explanation.
- Keep the 220 Ω input resistor. The output network changed from 22 Ω ring /
  33 Ω tip (PNP) to 33 Ω ring / 22 Ω tip (AO3401A P-channel high side) under a
  separately recorded architect decision: the PNP stage cannot guarantee the
  4 mA legacy-receiver worst case, and a CA-033 10 Ω return is unsafe in this
  permanently grounded tip leg (1.264 W tip fault). See R14.
- Make the physical pad-to-net relationships independently testable. A logical
  netlist alone does not prove compatibility with a fitted part or ribbon.
- Keep unresolved physical measurements visible and prevent a production export
  from treating pending acceptance as a pass.

## Review issue register

Status and verification after implementation. `make test` passes from a clean
build (exit 0): 128 Bun lib tests, 230 report checks, 33 sourcing tests,
27 fab-check tests, 110 electrical checks, 22 electrical mutation tests,
49 mechanical tests, SPICE smoke checks, firmware build. Nothing below is a
physical measurement; the pending first-article items stay in
`hardware-acceptance.json` (now 22 items, all pending).

| ID | Finding | Status and verification |
| --- | --- | --- |
| R01 | Rear Pi/front ribbon headers use perimeter numbering | Fixed. `J_PI`/`J_FRONT` use imported `BH254V_40P`/`BH254V_26P` with alternating IDC pad numbering; geometric pad/net checks plus a perimeter-numbering mutation test. Rear `pin_specification` 0 warnings. |
| R02 | Q3 gate/drain mapping disagrees with the exact 2N7002 | Fixed. `imports/C8545_2N7002.tsx` pins 1 gate / 2 source / 3 drain per the JCET datasheet; SOT-23 land-pattern check plus gate/drain-swap mutation test. |
| R03 | TLP2362 receive threshold lacks guaranteed current margin | Fixed (analysis) / pending (bench). TLP2361 (`C107626`) replaces TLP2362: 2.15×/2.30× margin at the 1.6 mA 125 °C threshold for corrected CA-033 corners; TLP2362 negative test kept. High-current corners (6.53/8.18 mA) exceed the 6 mA recommendation: pending qualification, not fixed by simulation. |
| R04 | U3/U4 have no local bypass capacitors | Fixed. C13/C14 100 nF (`C14663`) placed at the buffer VCC pins; pad-distance checks plus missing-bypass mutation test. |
| R05 | Bracket generator is stale and one mount cut misses its solid | Fixed. Canonical `layout.ts` → `export-mechanical.ts --check` → tracked `layout.json`; bracket from canonical data (11 cuts, one solid, 3790.1 mm³, genuine CadQuery verified). |
| R06 | 3 A USB-C feedthrough is proposed for the 5 A requirement | Rejected, pending. 3 A chain rejected for the 5 A path; exact power-chain qualification stays a pending acceptance item. |
| R07 | Display/control/jack/cable Z stack is unproven | Datums fixed, fit pending. Vendor PDF/STEP datums corrected (glass 88.87×52.56, PCB 86.5×56, bosses, 9 mm plane separation provisional, 14.1 mm panel gap); physical fit stays pending. |
| R08 | LCD cutout tolerance and internal corners are not established | Geometry fixed, fit pending. Aperture 90.67×54.36, 0.9 mm/side provisional budget, 1 mm corner relief; physical fit stays pending. Light-pipe 10.16 mm vs 14.1 mm gap recorded as not qualified. |
| R09 | Case depth is unproven; Eurorack Go budget conflicts | Pending. Named enclosure and measured stack required; no case approval inferred. |
| R10 | Release checks omit important diagnostics and physical pin identity | Fixed. Strict sourcing parser, report/fab checks that parse diagnostics, fingerprint-bound fail-closed production gate with no override; 22 pending physical items block release. |
| R11 | Sourcing CSV drops notes on malformed rows | Fixed. Exact-header strict validation (defect was an extra empty field); 33 parser tests; SMT-only JLC upload paths. |
| R12 | Schematics/documentation are not release-quality | Improved, one item open. TLP2361 labels added, R1/J1 collision fixed by schematic-only placement; controls-block net-label overlap over U5/U6 stays open and is recorded, not claimed fixed. Assembly docs aligned with verified datums. |
| R13 | Encoder hand-solder process is not manufacturer-recommended | Pending. Qualified THT process required; Bourns recommends against hand soldering. |
| R14 | MIDI OUT PNP stage cannot guarantee the 4 mA legacy worst case (found in review cycle 1) | Redesigned (analysis) / pending (bench). AO3401A (`C15127`, verified pinout/ratings) high-side switch, 100 Ω + 2.2 kΩ gate network, 33 Ω ring / 22 Ω tip 2512 1 % parts: 4.309 mA worst case (> 4 mA), gate drive 2.518 V ≥ 2.5 V test point, off state 0.210 V < 0.5 V at 25 °C, faults 0.366/0.547 W inside 1 W at 25 °C. Old-PNP deficit, missing-pull-up, oversized-series and 10 Ω-tip mutations kept. Full-temperature off state, edges, >70 °C derating and loop current stay pending under `midi_out_legacy`. |
| R15 | Imported J_PI/J_FRONT/Q3 lack pin metadata; J_PI lacks insertion direction (found in review cycle 1) | Fixed. `<connector>` elements with `insertionDirection="from_above"`, Q3 as `<transistor type="mosfet">` with pin labels; rear placement 0 warnings; no new allowlist entries. |
| R16 | Strengthened fab check cannot measure the J_PI pin-1 square hole (found in integration) | Fixed. `fab_check.py` measures `rotated_pill_hole_with_rect_pad` at quarter-turn rotations (pin-1 1.1 mm square hole / 1.7 mm pad: drill and 0.3 mm ring inside limits); other angles and unknown shapes fail closed; 3 new unit tests. |
| R17 | Makefile `electrical` target runs the checker from the wrong directory (found in integration) | Fixed. Target now runs inside `tscircuit/` like the other targets; full `make test` passes. |

## Acceptance is not simulation

Host tests, source checks, routing checks, analytical current bounds and simplified
SPICE models are design checks. They do not establish MIDI interoperability,
short-circuit temperature, GPIO power-order behaviour, ESD immunity, signal
integrity, installed depth, cooling, cable fit or a production soldering process.
Those items need the exact purchased parts and measured first articles.

## Primary references

- [MIDI Association: TRS wiring explanation](https://midi.org/updated-how-to-make-your-own-3-5mm-mini-stereo-trs-to-midi-5-pin-din-cables)
- [JCET 2N7002 / LCSC C8545 datasheet](https://datasheet.lcsc.com/datasheet/pdf/a141b8bd86b14475955ac8c4d3eea0a8.pdf?productCode=C8545)
- [Toshiba TLP2362 / LCSC C37658 datasheet](https://datasheet.lcsc.com/datasheet/pdf/fdfb3dc52097496bb28fdc922ec5c09e.pdf?productCode=C37658)
- [Toshiba TLP2361 / LCSC C107626 replacement datasheet](https://datasheet.lcsc.com/datasheet/pdf/ecf1cd8277534c97a26aa40e285f7e10.pdf?productCode=C107626)
- [TI SN74LVC1G126 datasheet](https://www.ti.com/lit/ds/symlink/sn74lvc1g126.pdf)
- [JLCPCB fabrication capabilities](https://jlcpcb.com/capabilities/pcb-capabilities)
- [Bourns PEC11R datasheet](https://www.bourns.com/docs/product-datasheets/pec11r.pdf)

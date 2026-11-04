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
- Keep the 220 Ω input resistor and the 22 Ω/33 Ω output network unless a
  separately justified electrical design change is reviewed.
- Make the physical pad-to-net relationships independently testable. A logical
  netlist alone does not prove compatibility with a fitted part or ribbon.
- Keep unresolved physical measurements visible and prevent a production export
  from treating pending acceptance as a pass.

## Review issue register

This register will be completed with implementation and verification evidence
before the pull request is handed over.

| ID | Finding | Required evidence |
| --- | --- | --- |
| R01 | Rear Pi/front ribbon headers use perimeter numbering | Correct alternating IDC pads; independent geometric pin/net checks and negative tests |
| R02 | Q3 gate/drain mapping disagrees with the exact 2N7002 | Datasheet-backed numbered mapping; generated-pad regression test |
| R03 | TLP2362 receive threshold lacks guaranteed current margin | Primary-verified replacement or an explicit unresolved release blocker; conservative current analysis |
| R04 | U3/U4 have no local bypass capacitors | Local capacitors, generated geometry checks, supply/return review |
| R05 | Bracket generator is stale and one mount cut misses its solid | Shared coordinates, material/clearance tests and CAD generation |
| R06 | 3 A USB-C feedthrough is proposed for the 5 A requirement | Reject unsuitable candidate; qualify an exact complete power chain before release |
| R07 | Display/control/jack/cable Z stack is unproven | Consistent provisional datums and an actual first-article collision/retention report |
| R08 | LCD cutout tolerance and internal corners are not established | Manufacturable geometry, tolerance assumptions and physical fit acceptance |
| R09 | Case depth is unproven; Eurorack Go budget conflicts | Named enclosure and measured installed stack; no inferred case approval |
| R10 | Release checks omit important diagnostics and physical pin identity | Full prototype gate, negative tests, separate fail-closed production acceptance |
| R11 | Sourcing CSV drops notes on malformed rows | Every-row strict parsing, corrected records, preserved warning regression tests |
| R12 | Schematics/documentation are not release-quality | Readable functional presentation, current assembly records and explicit unresolved diagnostics |
| R13 | Encoder hand-solder process is not manufacturer-recommended | Qualified assembly process and recorded first-article result |

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

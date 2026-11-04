# Design review: implemented corrections and physical blockers

**Engineering prototype, not a production fabrication release.** A green
`make test` is design verification, not an assembled-hardware qualification.
`make release` requires the evidence recorded in `hardware-acceptance.json`
before it changes `fab/` or exports production files. No physical item is
accepted by this change.

The issue register is [HARDWARE-REVIEW.md](HARDWARE-REVIEW.md). Primary mechanical
sources, CAD-derived coordinates and remaining fit questions are in
[MECHANICAL-VERIFICATION.md](MECHANICAL-VERIFICATION.md).

## Architecture retained

| Board | Envelope | Contents and assembly |
| --- | --- | --- |
| Front/UI | 128 × 112 × 1.6 mm | Four vertical TRS jacks, two isolated MIDI inputs, two PNP outputs, six transport/shift switches, three LEDs, NAV, encoder and J5. SMT on top; THT controls and connector fitted separately. The encoder needs a qualified soldering process. |
| Rear/base | 101.6 × 112 × 1.6 mm, 20HP | Removable Pico 2 sockets, Pi ribbon, front ribbon, SPI output buffers, reset FET, 5 V-to-VSYS diode and decoupling. SMT on bottom; THT headers fitted separately. |
| Panel | 132.08 × 128.5 × 1.6 mm, 26HP/3U | Bare FR-4 with printed legends and cutouts; no copper or placed components. No-mask fabrication and the finished panel require fabricator/first-article approval. |

The Pi and Pico remain removable. UART0/UART1 serve MIDI; SPI0, READY and the
open-drain reset path remain separate. The inter-board ribbon carries UART,
controls, LEDs, Pico-side 3.3 V and ground, not the external MIDI current loops.
USB/DSI video and external USB power travel through their actual cables, not
high-speed traces on these boards.

## Electrical corrections

- Rear `J_PI`/`J_FRONT` must use alternating IDC numbered pads, not a
  perimeter-numbered generic footprint. Independently test physical geometry
  and every assigned net against the Pi/Pico/front connector tables.
- Q3 is the exact JCET 2N7002, C8545: **1 gate, 2 source, 3 drain**. The generic
  MOSFET's old numbered mapping was wrong for this fitted part.
- U3/U4 require local 100 nF bypassing, with short supply and return paths.
- Routing and ground-reference policies belong in the canonical board source;
  fresh generated geometry and diagnostics, not source comments alone, verify
  their application.
- The receiver is Toshiba **TLP2361(TPL,E), C107626**, on the same 11-4L1S land
  pattern as the previous TLP2362. Its maximum threshold is 1.6 mA to 125 °C,
  rather than the TLP2362's 5 mA maximum. This closes the identified low-current
  margin problem on paper; it does not qualify the complete MIDI interface.

### MIDI network and part limits

| Function | Part/value | LCSC |
| --- | --- | --- |
| Receiver | Toshiba TLP2361(TPL,E), 11-4L1S | C107626 |
| LED reverse protection | 1N4148WS, SOD-323 | C2128 |
| Output PNP | MMBT3906LT1G, SOT-23 | C53444 |
| Input resistor | 220 Ω, 1%, 0603 | C22962 |
| RX pull-up | 2.2 kΩ, 1%, 0603 | C4190 |
| Output base resistor / pull-up | 4.7 kΩ / 10 kΩ, 0603 | C23162 / C25804 |
| Ring source / tip return | 22 Ω / 33 Ω, 1 W, 2512 | C118189 / C53728 |
| Local bypass | 100 nF, 0603 | C14663 |

Type A remains **ring = DIN 4/current source, tip = DIN 5/current return,
sleeve = DIN 2/shield**. Input sleeves have only the specified 100 nF AC path
to local ground; output sleeves are grounded. The antiparallel input diode
protects the optocoupler LED against reverse voltage. That is not an ESD test.

TLP2361 has an **inverting totem-pole** output, not the old open-collector
output. The retained 2.2 kΩ pull-up adds a low-state load within the stated
output-current checks. Its recommended LED current is 2–6 mA; absolute
continuous current is 10 mA, with derating above 110 °C. Some high-rail/low-VF
source corners can exceed 6 mA. Leave high-current/temperature and real-device
interoperability acceptance open; do not reduce the 220 Ω input resistor
without a separately reviewed compliant design.

### Power/reset intent, not measured acceptance

- Pi 5 V enters the Pico VSYS branch through B5819W, C8598. The Pico's onboard
  USB diode and this external diode form the intended ORing arrangement.
- Pico 3V3_OUT supplies the custom logic/front board. **Never join it to Pi
  3.3 V.** Pi 3.3 V is the output-buffer enable reference only.
- Pi MOSI/SCK/CS cross 100 Ω series resistors. These resistors are not isolation
  devices; power-up/down and unusual supply orders still need measurement.
- Pico MISO/READY pass through SN74LVC1G126 buffers, C7834, with OE referenced
  to Pi 3.3 V and pulled down when that rail is absent.
- Pi GPIO25 drives Q3 through 10 kΩ; Q3 pulls Pico RUN low. Its gate has a
  100 kΩ pull-down, and RUN has a 10 kΩ pull-up to Pico 3.3 V.

The owner approved this architecture. Simplified SPICE checks do not prove
backfeed behaviour, transition timing, fault temperature or all power orders.

## Mechanical corrections and remaining decisions

Canonical coordinates remain in `tscircuit/lib/layout.ts`; do not copy old
values back from this document.

- Glass centre is still **(−17, 11)**; all user-approved control centres remain
  unchanged. Lens OD is **88.87 × 52.56 mm**, not the PCB's 56 mm height.
- The panel aperture is **90.67 × 54.36 mm**, with explicit 1 mm corner relief
  and provisional tolerance allowance. The emitted polygon is checked.
- In the selected front-view orientation, the manufacturer's STEP gives boss
  centres **x = −18.12 / +39.88, y = ±24.5 mm** relative to the glass centre.
  This includes the 0.13 mm PCB/glass offset. Active-area offset is +2.905 mm.
- Glass front to PCB rear is **6.7 mm**. Boss ends project another **4 mm**,
  making the rear-most envelope **10.7 mm**. A spacer seats on those boss ends.
- The provisional **9 mm plane separation** therefore uses a **5 mm spacer**,
  not a 9 mm spacer. It is a design assumption awaiting an actual fitted part.
- With a flush-glass target, the resulting panel-to-front-board gap is
  **14.1 mm**. The 10.16 mm light-pipe reference is not approved for that gap.
  Select and qualify a suitable pipe, actuators, knob and jack retention.
- The bracket derives its stock, mounts, cable slots, head clearances and tie
  holes from the shared mechanical export. CAD checks material removal,
  remaining material and a single connected solid. Cable rear-body envelopes,
  printed-hole fit and strain relief still need real parts.
- Pi/cooler/HAT/SSD, microSD access, the DSI/ribbon folds and all plug service
  envelopes need a complete mock-up. The current paper depth is not case fit.
  **The Eurorack Go is not approved** for this stack; the 110 mm project target
  does not override a case's smaller depth limit.

Earlier HDMI/touch-USB display options, the 84 × 84 display, an 80 × 78 front-PCB
window, the 89.2 × 56.3 panel aperture and ambiguous 3.5/8–10 mm spacer statements
are historical, withdrawn assumptions. The selected display uses its included
12 cm DSI cable to Pi 5 DSI1; it does not need display HDMI or touch USB.

## Sourcing and process blockers

- SparkFun CAB-15455 is **3 A**, not a qualified 5 A feedthrough. It is rejected
  for the product's 5 A path. Select an exact suitably rated complete cable/
  feedthrough chain and measure PD, both orientations, droop and temperature.
- Bourns does not recommend hand soldering PEC11R. Record a qualified selective/
  wave or validated manual process, profile and inspection before approval.
- Exact jack continuity, NAV direction mapping, actuator reach, fasteners,
  light pipes and cable keying remain first-article checks.
- Recheck every exact MPN/package, stock and approved supplier before buying.
  A catalog C-number does not establish physical fit or production approval.

## Verification and next milestone

Run the complete prototype gate with `make test`; see
[fab-check.md](fab-check.md) and [acceptance.md](acceptance.md). The gate includes
fresh builds, independent physical pin checks and mutations, parsed diagnostics,
geometry, sourcing, mechanical freshness, host code and simplified SPICE.

Then obtain explicit approval for an **engineering prototype**, record DFM/CPL
review and first-article results, and resolve every required physical item.
Only then can the production release gate pass. No requirement, estimate,
CAD dimension or simulation result is substituted for a bench measurement.

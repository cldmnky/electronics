# Design review: mechanical and electrical blockers

Status: **prototype definition, not a fabrication release.** JLCPCB fabrication and SMD assembly are the manufacturing route. Every SMD line must use an LCSC/JLCPCB C-number; through-hole parts may be hand-soldered, except the encoder, whose hand soldering is not recommended and whose qualified process is pending. Stock figures are snapshots from the JLCPCB parts API and must be rechecked at order time. Physical acceptance is tracked in [acceptance.md](acceptance.md): every required item is still pending, and `make release` fails closed before any fabrication export.

## Design decisions already applied to the sources

These are implemented in `tscircuit/` and checked by `make test`, but a routed
prototype is not a validated board: the physical items below stay open.

- 26HP panel retained; five transport buttons and three SMD LEDs below the display; all four vertical TRS jacks in the bottom row. The LCD mounts on the front board; the panel is a separate bare FR-4 PCB.
- Front board 128 × 112 mm with an 89.2 × 56.3 mm panel cutout. The USB feedthroughs stay panel-mounted.
- Encoder push assigned to Pico GP2/pin 4 (`ENCODER_SW`); both pin-map files updated.
- JLCPCB/LCSC sourcing rule in the project contract and requirements.
- MIDI receiver changed from the TLP2362 (C37658) to the **TLP2361(TPL,E), C107626**; the electrical implementation of that change is in progress, and its threshold, high-current/temperature corners and interoperability are unmeasured.

## Proposed electrical architecture

### Board split

| Board | Contents | Assembly |
|---|---|---|
| Front/UI, 128 × 112 mm | 4 × vertical TRS jacks, 2 × MIDI input opto circuits, 2 × MIDI output drivers, 5-way nav, encoder, 5 transport buttons, 3 SMD LEDs, 2 × 13 interconnect | SMD factory-placed by JLCPCB; THT jacks/controls hand-soldered |
| Rear/base, 20HP 101.6 × 112 mm | 2 × Pico sockets, 2 × 20 Pi ribbon, SPI isolation, reset FET, 5 V→VSYS Schottky, decoupling, 2 × 13 interconnect, Pi mounting holes | SMD factory-placed; THT sockets/headers hand-soldered |

MIDI circuitry is on the front board so the current-loop wiring stays short. The interconnect carries only UART TX/RX, control signals, LED drive, 3.3 V and ground.

### Power and Pi/Pico isolation — proposed solution

- Pi 5 V (IDC pins 2/4) → **B5819W** Schottky (C8598) → Pico VSYS. The Pico USB VBUS is already diode-ORed inside the Pico; the external Schottky prevents Pico USB from backfeeding the Pi 5 V rail.
- Pico **3V3_OUT** powers the rear logic and the front board. Do **not** connect it to the Pi 3.3 V rail.
- Pi → Pico signals (MOSI, SCK, CS) are driven only by the Pi; add 100 Ω series resistors. When the Pi is off its pins are high-Z.
- Pico → Pi signals (MISO, READY) pass through **SN74LVC1G126DBVR** three-state buffers (C7834), powered from Pico 3.3 V. `OE` is driven from Pi 3.3 V (IDC pin 1) with a 100 kΩ pull-down. With the Pi off, OE is low and the Pi-side outputs are high-Z. This prevents Pico USB operation from driving the unpowered Pi.
- Pi GPIO25 (IDC pin 22) → 10 kΩ → **2N7002** gate (C8545); 100 kΩ gate pull-down; drain to Pico RUN; 10 kΩ RUN pull-up to Pico 3.3 V. Reset is open-drain and inactive when the Pi is off.
- Pi 3.3 V is used only as an OE/control reference, not as a Pico supply.

**Decision needed:** approve this power/isolation architecture. It is more complex than a programming jumper, but it allows Pico USB programming without relying on the operator to unplug the Pi ribbon.

### MIDI parts — proposed solution

Reuse the Froemix-proven topology, adapted to the selected TRS jack:

| Function | Part | LCSC | Package | Assembly |
|---|---|---|---|---|
| MIDI input opto | TLP2361(TPL,E) | C107626 | 11-4L1S | JLCPCB SMD (electrical merge in progress; threshold and current corners unmeasured) |
| Input reverse protection | 1N4148WS | C2128 | SOD-323 | JLCPCB SMD |
| MIDI output driver | MMBT3906LT1G | C53444 | SOT-23 | JLCPCB SMD |
| Input series resistor | 220 Ω 1% | C22962 | 0603 | JLCPCB SMD |
| Opto pull-up | 2.2 kΩ 1% | C4190 | 0603 | JLCPCB SMD |
| Output base resistor | 4.7 kΩ 1% | C23162 | 0603 | JLCPCB SMD |
| Output base pull-up | 10 kΩ 1% | C25804 | 0603 | JLCPCB SMD |
| Output ring source | 22 Ω 1 W | C118189 | 2512 | JLCPCB SMD |
| Output tip sink | 33 Ω 1 W | C53728 | 2512 | JLCPCB SMD |
| Opto bypass | 100 nF 50 V | C14663 | 0603 | JLCPCB SMD |
| Bulk decoupling | 10 µF 50 V | C13585 | 1206 | JLCPCB SMD |

MIDI input sleeve (jack pin 3) is **AC-coupled to ground through 100 nF**; there is no DC path to local ground. MIDI output sleeve is grounded. TRS Type A mapping: **tip = DIN 5, ring = DIN 4**, sleeve = DIN 2. The TLP2361 has an inverting totem-pole output, supply 2.7–5.5 V, threshold IF max 1.6 mA at 125 °C, recommended IF 2–6 mA and absolute continuous IF 10 mA with derating above 110 °C: the low-current threshold improves, but high-rail/low-VF source corners can exceed the recommended current, so the 220 Ω input resistor is not reduced without measurement.

### Selected connectors and controls

| Item | Part | LCSC | Mounting | Note |
|---|---|---|---|---|
| TRS jack | XKB PJ-301M-3-B | C381127 | THT vertical | Same part as Froemix; 3.5 mm stereo, barrel origin. Verify sample continuity. |
| 5-way nav | Alps SKRHABE010 | C139794 | SMD | A/B/C/D/CEN + COM; pins 7/8 mechanical tabs to GND. Needs actuator cap. |
| Encoder | Bourns PEC11R-4215F-S0024 | C143790 | THT | A/B/C + push switch; 24 detents. |
| Transport buttons | Omron B3F-4055 | C84931 | THT | 12 × 12 × 7.3 mm; pins 1/2 and 3/4 are contact pairs. |
| Optional nav buttons | Omron B3F-1000 | C93157 | THT | 6 × 6 × 4.3 mm alternative if the 5-way switch is rejected. |
| Pi ribbon header | XFCN BH254V-40P | C48687632 | THT | 2.54 mm 2 × 20 IDC. |
| Board interconnect | XFCN BH254V-26P | C48687629 | THT | 2.54 mm 2 × 13 IDC. |

The vertical TRS jack and SKRHABE010 pin mappings must still be checked against the physical parts before assembly. The three SMD LEDs are on the front board; the FR-4 panel has light-pipe holes.

## Mechanical blockers and options

### 1. Where does the Pi 5 physically sit? (resolved direction: 20HP base board)

| Option | Arrangement | Advantage | Risk |
|---|---|---|---|
| **A. Behind display** | Pi horizontal behind the 84 × 84 display, port edges at left/right; rear control board on the right/bottom | Shallow; uses the display footprint | Pi is 85 × 56, slightly wider than the display; both port edges need cable bends; cooler/HAT clearance must be proven |
| **B. Vertical right side** | Pi vertical behind the right-hand control column, port edge facing rear | Port access and cooling are easier | Only one short edge faces rear; the opposite port edge faces the panel; right-column hardware sits in front of it |
| **C. Separate rear carrier** | Pi on its own carrier at the rear; front board + rear control board in front | Best service access and modularity | Deepest; may exceed 110 mm; more mounting hardware |

**Chosen direction:** the LCD mounts on the front 128 × 112 mm board at the vendor’s asymmetric 58 × 49 mm hole pattern, with 8–10 mm standoffs. The panel has an 89.2 × 56.3 mm cutout. The Pi is on the rear of the 20HP base board and connects through the included 12 cm DSI cable. The right-hand control column overhangs the base board and connects by the 2×13 cable. This still needs a mock-up and depth check.

### 2. LCD mounting and panel cutout

The Waveshare-class display's HDMI and USB-C sockets exit at the **bottom** edge. The current buttons are 6 mm below the display glass; right-angle plugs and cable bends may still collide.

Options:
- **A.** Keep the requested under-display buttons, move them another 5–8 mm down, and use slim right-angle HDMI/USB-C cables. This is the preferred layout if the actual plugs measure small enough.
- **B.** Keep the buttons under the display but route the display cables through a cutout in the front board to the rear; requires a custom cable path.
- **C.** Move the transport row to the right column; not desired.
- **D.** Rotate the display 180° so its plugs exit at the top; the top USB row then becomes the conflict.

**Decision needed:** choose A, B, C or D after we measure the actual display. Buying one display sample is now the critical path.

### 3. Front-board shape around the display

The front board is solid behind the LCD; only the USB feedthroughs need a top relief notch. The LCD mounting holes are asymmetric: 25.00 mm from the PCB left, 3.50 mm from the right, 3.50 mm from top/bottom.

Options:
- **A.** Full frame board with an 80 × 78 mm cutout; add display standoffs at the four corners. Check frame strength and rail clearance.
- **B.** L-shaped board: controls under the display plus the right column, no material above/left of the display.
- **C.** Two front boards: one under-display transport strip and one right-hand control/MIDI strip, joined by a cable. This is a third PCB but avoids a large cutout and simplifies assembly.

**Recommendation:** **A** first; fall back to **B** if the frame is too narrow. **C** only if the display mounting forces it.

### 4. Depth and cooling

The depth budget is still unproven. The Pi 5 + Active Cooler + M.2 HAT+/SSD stack, display controller, two PCBs, standoffs and cable bends must be modeled together. **No 110 mm claim is made.** The Pi must be supported independently of the GPIO ribbon and board-to-board connectors.

**Decision needed:** approve the mock-up/measurement step before PCB outline freeze. The mechanical stack must be settled before component placement is finalized.

### 5. Front-panel USB feedthroughs

USB-C 5 A power, UMC audio USB-A, MIDI-hub USB-A, and display HDMI/USB-C cables still need exact panel feedthroughs, retention, bend radii and Pi-port assignments. No USB data is routed on the custom boards.

## Electrical decisions still needed

1. **Pico or Pico 2?** Decided: **Pico 2 (RP2350)** — same 40-pin module outline and pinout, so no PCB change; firmware builds with `PICO_BOARD=pico2`. On-target smoke test (console, MIDI loopback, SPI HELLO at 150 MHz sysclk) still outstanding.
2. **SN74LVC1G126 + B5819W + 2N7002 isolation design** — approved by the owner (see [decisions.md](decisions.md)) and implemented; covered only by simplified SPICE checks, so the scoped power-sequence tests stay open.
3. **Approve the Froemix-derived PNP MIDI output and TLP2362 input?** This is the recommended low-risk path.
4. **Approve PJ-301M-3-B vertical TRS jacks and SKRHABE010 nav switch?** The jack is the Froemix part; the panel holes, actuator cap and physical pin mapping need a sample check.
5. **Approve the 2 × 13 IDC interconnect?** It carries 3.3 V, ground, four UART signals, five nav signals, three encoder signals, four buttons, LED and spares. A keyed ribbon is preferred over loose wires.

## Verification status

Done and repeatable: sources build and route; netlist, pin-specification,
placement, shorts and PCB-style reports are parsed with pinned expectations;
geometry is compared with the published JLCPCB limits; host code and simplified
SPICE checks run. See [fab-check.md](fab-check.md) and [acceptance.md](acceptance.md).

Not done: any physical measurement, the DFM/CPL review and every item in the
acceptance list. A green `make test` is prototype verification only.

## Next actions

1. Buy one display sample and measure the rear connectors, mounting bosses and bottom cable envelope; record it as `display_dsi` evidence.
2. Build the Z-stack mock-up (Pi, HAT, cooler, SSD, boards, panel) and record the measured depth; this closes `zstack` and `case_fit` or proves a conflict.
3. Select the knob, button caps and nav actuator; measure against the panel (`control_actuator`).
4. Select an exact 5 A rated USB-C feedthrough (CAB-15455 is 3 A and rejected) and measure PD, droop and temperature.
5. Record the encoder solder process, then get the DFM/CPL and first-article evidence under a reviewed engineering prototype (see [acceptance.md](acceptance.md)).

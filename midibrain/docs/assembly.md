# Candidate components, assembly and feeders

This is a **prototype BOM**, not an approved purchasing or manufacturing release. JLCPCB is the PCB fabricator and SMD assembler; LCSC/JLCPCB part numbers are candidates and must be rechecked for stock/package before ordering. Through-hole parts are hand-soldered unless explicitly stated.

## Board split

| Board | Contents | Assembly |
|---|---|---|
| Front/UI, 128 × 112 mm | 5 transport buttons, 3 SMD LEDs, 4 vertical TRS jacks, 5-way nav, encoder, MIDI SMD circuitry, 2 × 13 interconnect | Single-sided top SMD by JLCPCB; THT controls/jacks/interconnect hand-soldered |
| Rear/base, 20HP 101.6 × 112 mm | Pico sockets, Pi ribbon, SPI buffers, reset FET, 5 V→VSYS diode, decoupling, 2 × 13 interconnect | SMD by JLCPCB; THT headers hand-soldered |
| Front panel, 26HP 132.08 × 128.5 × 1.6 mm | Bare FR-4, no solder mask, white silkscreen, ENIG; LCD/control/USB/jack cutouts, three 4 mm LED light-pipe holes | JLCPCB PCB only; no copper/components |

## Selected SMD parts

| Function | MPN | LCSC | Package | Board/side |
|---|---|---|---|---|
| MIDI input opto | TLP2362(TPL,E) | C37658 | SO-5 | Front/front |
| MIDI output PNP | MMBT3906LT1G | C53444 | SOT-23 | Front/front |
| MIDI input protection | 1N4148WS | C2128 | SOD-323 | Front/front |
| MISO/READY isolation | SN74LVC1G126DBVR | C7834 | SOT-23-5 | Rear/rear |
| Reset FET | 2N7002 | C8545 | SOT-23 | Rear/rear |
| Pi 5 V→VSYS Schottky | B5819W | C8598 | SOD-123 | Rear/rear |
| 100 Ω SPI series | 0603WAF1000T5E | C22775 | 0603 | Rear/rear |
| 220 Ω MIDI input | 0603WAF2200T5E | C22962 | 0603 | Front/front |
| 2.2 kΩ opto pull-up | 0603WAF2201T5E | C4190 | 0603 | Front/front |
| 4.7 kΩ output base | 0603WAF4701T5E | C23162 | 0603 | Front/front |
| 10 kΩ pull-up/gate | 0603WAF1002T5E | C25804 | 0603 | Front/front + Rear/rear |
| 100 kΩ pull-down | 0603WAF1003T5E | C25803 | 0603 | Rear/rear |
| 330 Ω LED | 0603WAF3300T5E | C23138 | 0603 | Front/front |
| 22 Ω 1 W output source | 25121WF220JT4E | C118189 | 2512 | Front/front |
| 33 Ω 1 W output sink | 25121WF330JT4E | C53728 | 2512 | Front/front |
| 100 nF bypass | CC0603KRX7R9BB104 | C14663 | 0603 | Front/front + Rear/rear |
| 10 µF bulk | CL31A106KBHNNNE | C13585 | 1206 | Front/front + Rear/rear |
| REC/OVER DUB LED | KT-0805R red | C2295 | 0805 | Front/front |
| PLAY LED | KT-0805G green | C2297 | 0805 | Front/front |
| 5-way nav | Alps SKRHABE010 | C139794 | SMD 8-pad | Front/front |

## THT, modules and panel parts

| Ref | Part | LCSC / source | Mounting | Assembly |
|---|---|---|---|---|
| J1–J4 | XKB PJ-301M-3-B vertical 3.5 mm stereo TRS (same as Froemix) | LCSC C381127 | THT vertical; 6 mm panel hole | Hand |
| SW1–SW6 | Omron B3F-4055 (SW6 = SHIFT/EXIT) | C84931 | THT | Hand |
| ENC1 | Bourns PEC11R-4215F-S0024 | C143790 | THT | Hand |
| DISP1 | Waveshare 3.5inch DSI LCD (H), SKU 33087 | Waveshare | Module; 58 × 49 mm holes, 6.70 mm rear | Hand |
| LP1–LP3 | Bivar PLP2-400-C clear light pipe | DigiKey/Mouser | 4.0 mm panel hole, 10.16 mm | Hand |
| PANEL1 | Bare FR-4 front panel, 1.6 mm, no solder mask, white silk, ENIG | JLCPCB | 26HP × 3U | PCB order |
| J_PI | 2 × 20 2.54 mm pin header (PEC20DAAN) | — | THT | Hand |
| J_STACK | 2 × 13 2.54 mm pin header (PEC13DAAN) | — | THT | Hand |
| Pico sockets | 2 × 1 × 20 2.54 mm female | — | THT | Hand |
| BR1 | USB feedthrough/strain-relief bracket, PETG | `mechanical/feedthrough-bracket.stl` | 3D printed | Print |
| J_PWR | SparkFun CAB-15455, USB-C female panel to USB-C male, 1 ft/305 mm, USB 2.0, **3 A** | SparkFun CAB-15455 | Panel; 12.6 × 7.6 mm slot, two Ø3.2 on 18 mm (clamp) | Cable | **3 A, not 5 A.** Pi 5 5 A supply will be limited; test. Does not meet the earlier 5 A target by itself. |
| J_AUDIO/J_HUB | 2 × Adafruit 908, USB-A female panel to USB-A male, 340 mm, USB 2.0 | Adafruit 908 | Panel; 14.65 × 7.10 opening, 2 × Ø3.5 on 30 mm | Cable |
| BR1 hardware | 2 × M2.5 × 8 screws/nuts; 4 × 2.5 × 100 mm cable ties | — | — | Hand |
| Panel↔front standoffs | 4 × M2.5 standoff, length TBD (~10 mm + panel gap); 8 × M2.5 screw/nut | — | Panel (±61, ±53) to front board | Hand, pending sample |
| Front↔rear spacers | 4 × M2.5 spacer, length TBD (~18–20 mm); 8 × M2.5 screw/nut, top-left low-profile head | — | Front/rear (±44, +43/−52) | Hand, pending J5 bend mockup |

## LED / panel scheme

- The three 0805 LEDs are assembled on the **front board** by JLCPCB: red above REC and OVER DUB, green above PLAY.
- The FR-4 front panel has **no solder mask** and white silkscreen. Bare FR-4 is opaque, so the panel has three **4 mm openings** with Bivar PLP2-400-C light pipes; do not expect light to pass through masked or unmasked FR-4.
- Keep the LED-to-pipe gap small and centered; the 10.16 mm pipe assumes roughly an 8 mm panel-to-front-board gap.
- The FR-4 panel needs an ENIG/HASL finish and no copper; confirm JLCPCB accepts “no solder mask both sides” in the order notes.

## Open items

- Exact Pico socket, USB-C 5 A coupler MPN, light-pipe length after mock-up, panel finish acceptance, and all mechanical clearances.
- The 2 × 20 Pi IDC pin numbering must be mapped to the physical Pi pinout and checked against the purchased ribbon.
- No BOM/CPL or fabrication release has been generated.

## Cable BOM

### Internal / module cables

| Ref | Cable | Connectors / length | Qty | Notes |
|---|---|---|---:|---|
| CAB_DSI | Waveshare 12 cm DSI cable / 15-pin FPC | 15-pin display FPC to 22-pin Pi 5 DSI1 | 1 | Included with DISP1 |
| CAB_GPIO | 40-pin 2 × 20 GPIO ribbon, female-to-female | 2.54 mm, ~150 mm (Adafruit 1988 or equivalent) | 1 | Pi 40-pin header to J_PI |
| CAB_FRONT | 26-way 2 × 13 IDC ribbon, female-to-female | 2.54 mm, 100–150 mm | 1 | Front J5 to rear J_FRONT; exact MPN TBD |
| CAB_PICO_USB | Micro-USB to USB-A programming cable | ~0.3 m | 1 | Pico USB; isolate Pi/control power during programming |
| CAB_USBC_PANEL | SparkFun CAB-15455 USB-C F panel → USB-C M | 305 mm, USB 2.0, 3 A | 1 | Panel feedthrough; 3 A limit |
| CAB_USBA_AUDIO | Adafruit 908 USB-A F panel → USB-A M | 340 mm, USB 2.0 | 1 | UMC/audio panel port |
| CAB_USBA_HUB | Adafruit 908 USB-A F panel → USB-A M | 340 mm, USB 2.0 | 1 | MIDI hub panel port |

### External / system cables

| Ref | Cable | Connectors / length | Qty | Notes |
|---|---|---|---:|---|
| CAB_PSU | Official Raspberry Pi 27 W USB-C PSU, 5.1 V / 5 A | USB-C, ~1.2 m | 1 | Plugs into CAB-15455; feedthrough is only 3 A |
| CAB_UMC_USB | USB-A male → USB-B male | 1 m | 1 | UMC1820 to front USB-A audio port |
| CAB_HUB_USB | USB-A male → USB-B male (or hub-specific upstream) | 0.5–1 m | 1 | Powered MIDI hub to front USB-A hub port; verify hub connector |
| CAB_ADAT | Optical ADAT cable | 0.5–1 m | 2 | UMC1820 ↔ ADA8200; one direction for 8 preamps, second for clock if needed |
| CAB_SPDIF | Coaxial or optical S/PDIF cable | 0.5–1 m | 1 | Stereo source to UMC1820 S/PDIF input; connector type TBD |
| CAB_MIDI_TRS | 3.5 mm TRS Type A → DIN 5 MIDI cable | 1 m | 4 | Two inputs, two outputs |
| CAB_HUB_PSU | Powered hub supply | Manufacturer-specific | 1 | Exact hub/PSU TBD |

**Important:** the SparkFun CAB-15455 is rated **3 A**, not the Pi 5’s 5 A. The official 5 A supply may negotiate only 3 A through it, causing USB current limiting/throttling. This is a documented deviation from the earlier 5 A requirement; test with `vcgencmd get_throttled` under load, or use a verified 5 A panel feedthrough.

# Euro board (front board + panel) — 20HP

The Eurorack front half of Froemix: the board that carries the jacks, the pots,
the UI controls, the MIDI sockets, the display and the panel. It mates with the
16HP base board, which holds all the analog circuitry.

Live design: `euro/index.circuit.tsx` (front board) and `euro/panel.circuit.tsx`
(panel). Both import `euro/lib/layout.ts`, so a panel hole and its part on the
board cannot drift apart — `euro/scripts/check_mating.py` proves it from the
built circuit JSON on every gate run.

Status: **designed, routed, gated; physical first article pending.** Not ordered.
[MECHANICAL-STACK.md](MECHANICAL-STACK.md) is the corrected Z/part/preparation
contract and supersedes the former 12mm spacer and actuator-height assumptions.

For the current faceplate artwork, material comparison, manufacturing files and
pre-order mechanical checklist, use [FRONT-PANEL.md](FRONT-PANEL.md). Its panel
geometry and ordering notes supersede the older faceplate assumptions below;
the electronic boards are unchanged by that artwork revision.

## 1. Mechanical contract

Everything in this section comes from the base board and must not change.

| Item | Value | Source |
| --- | --- | --- |
| Panel | 20HP x 3U = 101.6 x 128.5mm, 2mm | this design |
| Front board | 101.6 x 128.5mm, 2 layers | this design |
| Base board | 80.9 x 128.5mm, 4 layers | `froemix/AGENTS.md` |
| Mating socket footprint origins | CN1 (-17.58, 59.64), CN2 (12.42, -36.36), CN4 (24.50, 59.63) | base board `sections/main.tsx`; CN4 bbox centre 24.47 is not its origin |
| Base board standoffs | (±36.95, ±60.75), M3 | base board `index.circuit.tsx` |
| Rail screws | (±43.3, ±61.25), M3 | Doepfer 3U |

Both boards are centred on the same panel, so the base board's connector
coordinates are also the front board's socket coordinates — no offset, no
mirror. The sockets sit on the **bottom** of the front board and face down onto
the base board's top-side 2.00mm pin headers.

Bottom-side footprints mirror about their origin (`froemix/AGENTS.md` §5), so the
sockets are authored pre-mirrored (pin 1 at +X in footprint space) and land with
pin 1 on the base board's pin 1. `check_mating.py` compares all 39 pads and their
nets to the actual built base board, plus the frozen coordinates and faces.

CN3 (micro SD) is **not** mated: see §8.

### Stack-up (front to back)

```
panel underside        0.0 mm    front face is -2mm; M3 rack screws
front board TOP        9.0 mm    assembly target; controls/OLED face panel
front board BOTTOM    10.6 mm    1.6mm PCB; 4.3mm sockets toward base
base board TOP        21.6 mm    11mm clear M3 spacers; 6mm-shoulder headers
base board BOTTOM     23.2 mm    tall bodies / power / Daisy toward rack cavity
```

`PANEL_GAP = 9.0mm` means **front PCB top to panel underside**, an assembly
target, not a measured shoulder on the currently selected XKB jack. The XKB
drawing gives an 8.8mm body and additional bushing geometry; old PJ398SM/Alps
height claims are not evidence for replacement parts. Jig the actual panel,
jacks and pots before soldering. Actuator calculations cover an 8.5–9.6mm gap
and the 1.8–2.2mm panel thickness range.

The bare K6-1212A02 cap (7.3mm) and XUNPU stem (10mm) do not reach beyond the
panel front. Release now includes a **5.8mm-added-height captive MODE plunger**
and **5mm-added-height NAV cap** as printable STL prototypes. NAV alone changes
from Ø7 to Ø8mm; every centre stays fixed. A 2mm button riser was not a solution
through a 2.2mm panel, and its short pins cannot simply span a taller riser.
See [MECHANICAL-STACK.md](MECHANICAL-STACK.md) for dimensions, assembly and tests.

## 2. What the front board carries

| Group | Count | Part | Net |
| --- | --- | --- | --- |
| pots | 6 | Alps RK09K1130A5R, 10k linear | `M_ADC_0`..`M_ADC_5` |
| UI joystick | 1 | 5-way multi-control, 5 momentary contacts | `M_ADC_6` (ladder) |
| UI button | 1 | 12mm illuminated tactile, switch + LED | `M_ADC_7` |
| audio in | 2 | XKB PJ-301M-2-B mono jack, C381126 | `J_AUDIO_IN_L/R` |
| audio out | 2 | XKB PJ-301M-2-B mono jack, C381126 | `J_AUDIO_OUT_L/R` |
| CV in | 2 | XKB PJ-301M-2-B mono jack, C381126 | `J_CV_IN_1/2` |
| CV out | 2 | XKB PJ-301M-2-B mono jack, C381126 | `J_CV_OUT_1/2` |
| sync in/out | 2 | XKB PJ-301M-2-B mono jack, C381126 | `J_SYNC_IN`, `J_SYNC_OUT` |
| MIDI in/out | 2 | PJ366ST stereo jack (TRS MIDI) | `J_MIDI_IN_TIP/RING`, `J_MIDI_OUT_TIP/RING` |
| jack LEDs | 6 | 3mm LED + 2.2k + 1N4148 per jack | audio in/out L/R, CV out 1/2 |
| display | 1 | 1.3" 128x64 SPI OLED | `OLED_CS/SCL/DC/SDA/RES` |
| indicator | 1 | 3mm THT LED + 1k series R | `LED` |
| rails | — | `P3V3_A`, `P3V3_D`, `GND` | |

### Pots (6)

Wired as dividers across `P3V3_A`/`GND`, so the mux inputs can never leave the
CD4051's 0–3.3V range (`froemix/AGENTS.md` §4, "Mux input clamps"). 10k keeps
the source impedance the CD4051 sees at ≤2.5k and the total pot current at
2.0mA. Pin 1 = CCW (GND), pin 2 = wiper, pin 3 = CW (3.3V).

### UI joystick (5-way, one mux channel)

`M_ADC_6` carries five controls as a resistor ladder. All five contacts share
the switch's common pin, which is the mux channel node; each direction connects
its own resistor from that node to GND, and a 10k pull-up to `P3V3_A` sets idle.

| State | Resistor to GND | Node voltage |
| --- | --- | --- |
| idle | none | 3.30 V |
| press | 0 (direct) | 0.00 V |
| up | 1k | 0.30 V |
| down | 2.2k | 0.60 V |
| left | 4.7k | 1.05 V |
| right | 10k | 1.65 V |

The CD4051's ~250 Ω Ron adds ~2 % error, and a 12-bit ADC puts the closest pair
(press/up) ~370 counts apart. **The ladder is a firmware contract**: the
firmware decodes levels, not pins.

### UI button (illuminated, one mux channel)

Part: Korean Hroparts `K6-1212A02` (LCSC `C692901`, JLCPCB-stocked, 12mm, red
LED). Its bare 7.3mm cap does not reach the panel; the supplied `mode-plunger.stl`
adds 5.8mm (see [MECHANICAL-STACK.md](MECHANICAL-STACK.md)). Six electrical pins — C1/C2 are one internally connected switch rail and
C3/C4 the other, so the two pads of each rail are wired in parallel — plus two
0.8mm locating pegs that sit at (0, ±6.75) from the centre.

The button's own LED doubles as the pull-up, so the button shows its state with
no GPIO and no firmware:

```
P3V3_A -- 1k -- LED(A) -- LED(K) --+-- M_ADC_7
                                   +-- switch -- GND
```

Released: no current path, LED dark, the node floats to 3.3 V (full scale).
Pressed: the node is 0 V and the LED lights at (3.3 − 2.0)/1k = 1.3 mA.

### Jack activity LEDs (6, zero pins)

The signal at the jack drives its own LED — no GPIO, no firmware:

```
jack net -- 2.2k -- 1N4148 -->|-- LED -->|-- GND
```

The series diode passes only positive excursions, so audio and CV never put a
reverse half-cycle on the LED. At a 5 V peak: (5 − 0.7 − 2.0)/2.2k = 1.0 mA.

Sleeves are GND on every jack. The mono jacks' tip-switch pads are left
unconnected on purpose: inputs are AC-coupled on the base board, so a
normal-to-ground adds nothing, and an output must not be shorted by an empty
socket. MIDI needs TIP *and* RING, so those two are stereo (PJ366ST, 3 pads,
slotted holes).

## 3. Panel floorplan

20HP x 3U, origin at the centre, +Y up:

| y (mm) | Contents |
| --- | --- |
| +57.5 … +24 | OLED module 35.4 x 33.5mm; window at (0, +42.8); pots and UI either side |
| +45 | pots: x = ±28, ±43 (15mm pitch) |
| +25 | pot (-43), **UI joystick (-28)**, **MODE button (+28)**, pot (+43) |
| -3 | 4 jack LEDs, one above each audio jack |
| -14 | jacks: audio in L/R, audio out L/R, sync in/out (x = -42 … +42, 16.8mm pitch) |
| -30 | 2 jack LEDs above CV out 1/2 |
| -47 | jacks: CV in 1/2, CV out 1/2, MIDI in/out |
| -59.8 | `FROEMIX` + `daisy seed dsp - 20hp` |
| ±61.25 | M3 rack screws (±43.3) |

Labels: P1–P6 under each pot, `UI` under the joystick, `MODE` under the button,
a jack name under each jack.

## 4. Display

Selected 1.3" 128x64 SPI OLED module HS13L02B4S01 / C42432559 (SH1106),
35.4 x 33.5mm PCB, 7-pin 2.54mm header, active area 29.42 x 14.70mm.

The module is soldered **flat to the top of the front board**, header row at
(0, 55.5): its 7 pads are a plain 1x7 2.54mm pattern, and the seven `OLED_*`
signals run straight across the board from the CN4 socket (bottom side,
(24.5, 59.63)) to those pads. It is *not* plugged into CN4, because a 2.54mm
socket plus the module's own header would put the glass roughly a centimetre
further back and outside the 9mm actuator/panel budget.

The panel window is 30.5 x 16.0mm, centred on the active area at (0, 42.8):
`OLED_AT.y` (55.5) + `windowOffsetY` (-12.7).

## 5. Assembly

Mixed, like the base board: **19 SMD parts on the bottom** (13 × 0603 resistor,
6 × SOD-323 diode) that JLCPCB can place, and everything else through-hole by
hand: 10 mono jacks, 2 TRS jacks, 6 pots, the joystick, the button, 7 LEDs
(6 jack + 1 panel), the 1x7 OLED header and the three 2.00mm sockets (CN1, CN2,
CN4) on the bottom.

`make release` writes the JLCPCB-shaped assembly files next to the fabrication
data: `fab/euro/euro-bom-jlc.csv` (14 unique parts, 50 placements) and
`fab/euro/euro-cpl-jlc.csv` (the 19 SMT placements; the 31 through-hole parts are
BOM-only, exactly like the base board's THT policy). `euro-pnp-all.csv` is the
unfiltered placement list for reference, and `euro-bom-smt.csv` /
`euro-bom-hand.csv` are the same split as upload-sized / shopping-list files
(the hand file lists the 31 through-hole parts with quantity, MPN and side).

Socket stack: **6mm-shoulder E60 male + 4mm post**, existing **4.3mm socket**,
**11mm clear spacer**: insertion 3.3mm, insulator clearance 0.7mm nominal.
The old 2mm male could not reach across a 12mm gap. CN4 is a stocked eight-way
C2883061 strip **cut to seven ways before fitting**. J1/shunts ≤9.5mm; trim
front-board rear tails ≤1mm; set/test shunts before stacking. See the mechanical
guide for tolerance arithmetic and the physical contact/retention test.

`make release` also includes `fab/euro/mechanical/` (two STL actuator prototypes,
a hardware list, dimension manifest and mechanical guide). The panel ZIP now
goes through the panel-specific actual Gerber/profile/NPTH verifier.

## 6. Verification

`cd euro && bun run gate` (`scripts/gate.sh`), all green:

| Step | Result |
| --- | --- |
| `tsci build` | 2 circuits, 100 traces / 71 vias / 2497mm copper, 0 unconnected |
| `tsci check netlist` | source diagnostics, no errors |
| `tsci check placement` | Errors 0, Warnings 0 |
| `tsci check shorts` | no shorts |
| `tsci check pcb-style` | clean |
| `scripts/check_mating.py` | all 39 actual base/front pads and nets; faces, standoffs, 27 control holes, R0.5 OLED window |
| `scripts/mechanical_check.ts` | selected built supplier identities, stack insertion/clearance and actuator envelopes |
| `scripts/test_mating.py` / `bun test lib/mechanical` | negative mating/part/Z tests and printable-solid topology/dimensions |
| `scripts/check_madc.py` | the 8 `M_ADC_*` nets are closed islands of pots/joystick/button referenced only to `P3V3_A`/`GND` (the base-board mux has no clamps - see `doc/FRONTBOARD-INTERFACE.md`) |
| `scripts/panel_check.ts` | frozen 31-hole geometry, OLED bounds/radius, stroked artwork and hardware keepouts pass |
| `bun test lib/panel lib/mechanical` | panel geometry/art/export/preview plus mechanical regression tests |
| `tsci snapshot --test` | schematic + PCB baselines match |

The build log's `source_pin_missing_trace_warning`s are the 10 mono-jack tip
switches and CN2 pins 11–15 (encoder reserve) — every one is deliberate.

`scripts/gate.sh` treats `tsci check placement`'s exit code as advisory: that
command exits 1 on any informational `issues:` note (for example "rotate R5")
even with 0 errors and 0 warnings, so the log parse is the authority.

## 7. Firmware contract

The front board adds no wires, so everything the UI does is encoded on two mux
channels:

1. **Channel 6 (joystick)** — six levels, table in §2. Read after the CD4051
   selects channel 6; the settle time must cover the mux's ~250 Ω and the
   ladder's worst case (10k + 10k).
2. **Channel 7 (button)** — two levels: ~0 V pressed, ~3.3 V released. The LED
   is in the pull-up path, so do not drive that channel as an output.

## 8. Open questions (decide before ordering)

1. **Joystick sourcing — selected.** The current BOM uses XUNPU
   `WS-1004-ARL10026` (LCSC C42377836), not the retired Alps assumption.
   LCSC/JLCPCB remains the first supplier; use Electrokit for any unavailable
   part, as required by `AGENTS.md` §9.
2. **Joystick cap — dimensioned prototype supplied.** XUNPU's drawing gives
   10mm overall height, 3.15mm square stem. Print/finish `nav-cap.stl` and test
   all directions and centre press through the revised Ø8mm hole. The ±8° CAD
   test envelope is not a datasheet travel guarantee.
3. **Button reach — captive plunger supplied.** Print `mode-plunger.stl` in
   translucent resin, flange behind the panel. Test free return and no preload.
   Check the actual K6 LED polarity before soldering: its drawing does not
   reliably number the +/− pin positions.
4. **CN3 / micro SD.** Left unmated; the Daisy's own slot is used from the back.
   Options: mate CN3 and add a front micro-SD socket, or a 1x8 test header.
5. **OLED module.** C42432559 is selected and the XY/window offsets are checked.
   Actual glass/bezel Z, support and viewing angle still need a physical check.
6. **Panel material.** Recommended: 2mm FR4, black mask / white print. See
   [FRONT-PANEL.md](FRONT-PANEL.md) for the comparison and JLCPCB settings.
   An anodised aluminium alternative needs the separate JLCCNC mechanical-CAD
   and marking workflow; an ordinary PCB drill file alone does not specify it.
7. **Board height.** 128.5mm matches the base board. If the rack rails are the
   limit, both boards move together — do not change one.
8. **Encoder dropped.** Base board CN2 pads 11–15 stay reserved and unconnected;
   the UI is the 5-way plus the button.

## 9. BOM and sourcing

Rule (`froemix/AGENTS.md` §9): **LCSC/JLCPCB first, Electrokit for anything LCSC
does not stock.** The Eurokit lookup was done first, then the LCSC sweep — and
**every part has an LCSC code**, so nothing in this design comes from Electrokit.
Electrokit's numbers are kept below only as the record of the comparison.

| Qty | Part | LCSC | Stock | Basic/Ext | Note |
| --- | --- | --- | --- | --- | --- |
| 10 | 3.5mm mono jack + tip switch, M6x0.5 | C381126 | 745 | Ext | XKB `PJ-301M-2-B`; **replaces the Thonkiconn** |
| 2 | 3.5mm stereo jack (TRS MIDI), M6x0.5 | C381127 | 620 | Ext | XKB `PJ-301M-3-B` |
| 1 | 5-way navigation switch, 10.2 x 10.2mm | C42377836 | 2,669 | Ext | XUNPU `WS-1004-ARL10026`, 6 pins |
| 1 | 12mm illuminated button, red | C692901 | — | — | Hroparts `K6-1212A02` |
| 6 | 9mm pot, 10k lin, knurled 6mm, LM1 15mm | C470294 | 125 | Ext | Alps `RK09K1130A8G` (slotted-shaft `RK09K1130AP5` = C470295, 3,022) |
| 1 | 1.3" 128x64 SPI OLED, SH1106 | C42432559 | 37 | Ext | HS `HS13L02B4S01`, 35.4 x 33.5mm, 7-pin 2.54, GND/VCC/SCK/SDI/RES/DC/CS |
| 2 | 2.00mm 1x16 pin socket, 4.3mm | C22373851 | 5,076 | Ext | `HC-PM200-4.3H-1x16PZ` |
| 1 | 2.00mm 1x7 pin socket, 4.3mm | C22438139 | 4,740 | Ext | `HX PM2.0-1x7P ZC` |
| 7 | 3mm LED red 630nm | C440523 | 139,540 | Ext | TOGIALED; **pin 1 = anode** (footprint follows it) |
| 6 | 1N4148WS SOD-323 | C2128 | — | — | jack LED diodes |
| 7 | 1k 0603 | C21190 | — | Basic | ladder "up", panel LED R, button R |
| 6 | 2.2k 0603 | C4190 | — | Basic | jack LED ballast |
| 1 | 4.7k 0603 | C23162 | — | Basic | ladder "left" |
| 2 | 10k 0603 | C25804 | — | Basic | joystick pull-up, ladder "right" |

Thonkiconn `PJ398SM` / `PJ366ST` return **0 results at LCSC** and Electrokit has
no PCB-mount mono jack at all, so the jacks move to the XKB `PJ-301M` family —
the same M6x0.5 bushing and the same 6.0mm panel hole, so **the panel does not
change**.

### Decisions taken

- **Jacks stay PCB-mount.** A panel-mount ("hole mount") variant was considered
  and rejected: it would move 12 jacks off the board onto a wire harness (24
  panel solder joints plus crimps) and gain nothing, because the panel holes are
  the same 6.2mm either way and the `PJ-301M` pad question is already covered by
  the slotted pads. Everything on this board is bought from LCSC and the SMD
  side is JLCPCB-placed as it stands.
- **Assembly split** (unchanged): 19 SMD parts on the bottom (13 x 0603 resistor,
  6 x SOD-323 diode) are placed by JLCPCB; the through-hole side - 12 jacks,
  6 pots, joystick, button, 7 LEDs, OLED header, 3 sockets - is hand-soldered
  unless JLC's through-hole service is ordered. The panel is a bare PCB.

### Still to verify before ordering

1. **Jack pad line — resolved to a tolerant footprint** (see also "Decisions",
   above: the PCB-mount jack stays). The XKB `PJ-301M`
   datasheet has **no PCB land pattern**, so the pads were cross-checked against
   two independent footprints of the same family:

   | Source | short gap | long gap | span |
   | --- | --- | --- | --- |
   | KiCad official (QingPu WQP-PJ398SM) | 3.1mm | 8.3mm | 11.4mm |
   | 4ms `PJ301M-12` (shipping product) | 2.41mm | 8.56mm | 10.97mm |
   | XKB drawing (prints only the long gap) | — | 8.20mm | — |

   The long gap agrees within 0.4mm; the short gap disagrees by 0.7mm. The real
   pins are 1.0 x 0.2mm **flat tabs**, which want slots anyway, so all three
   pads are 1.2 x 2.0mm slots along the pin line: that takes the tab the way the
   part is designed to fit and covers either short gap. A first-article check is
   still worth ten minutes, but the footprint no longer depends on which
   variant arrives — **the same board takes the XKB part or a Thonkiconn.**
3. **OLED module, resolved.** The footprint now follows the `HS13L02B4S01`
   drawing: 35.4 x 33.5mm body, header row 2.0mm from the connector-side edge,
   active area 29.42 x 14.70mm centred 12.70mm below the header row, panel
   window 30.5 x 16.0mm at (0, 42.8). The module's four 3.2mm holes are not
   used - it is soldered on its seven pins.

### Electrokit (record only — not used)

| Part | Art. nr | Why not |
| --- | --- | --- |
| Alps RK09K 10k lin | 41012043 | LCSC C470294 is the same family |
| 5-way switch 12.4 x 12.4mm | 41035331 | LCSC C42377836 is smaller, cheaper, more stock |
| Waveshare 1.3" OLED (C) | 41024144 | SH1107 + different pin order; LCSC C42432559 matches our pinout |
| 12mm illuminated button | 41033204 | white LED Vf 3.2V is marginal on 3.3V; LCSC C692901 is red (~2.0V) |
| Amphenol ACJS-MV35-3 stereo jack | 41011901 | only 10 in stock; XKB C381127 keeps one jack family |
| Mono jack | 41001190 | chassis-mount, wrong pads, out of stock to 2026-11-13 |
| Thonkiconn `PJ398SM` | 41020778 | **discontinued** - Electrokit's page says "outdated and no longer active". There is no current PCB-mount Thonkiconn listing, so this fallback does not exist; the XKB `PJ-301M` from LCSC is the way. |

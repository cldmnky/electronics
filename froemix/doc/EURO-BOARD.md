# Euro board (front board + panel) — 20HP

The Eurorack front half of Froemix: the board that carries the jacks, the pots,
the MIDI sockets, the display and the panel. It mates with the 16HP base board,
which holds all the analog circuitry.

Live design: `euro/index.circuit.tsx` (front board) and `euro/panel.circuit.tsx`
(panel). Both import `euro/lib/layout.ts`, so a panel hole and its part on the
board cannot drift apart — `euro/scripts/check_mating.py` proves it from the
built circuit JSON on every gate run.

Status: **designed, routed, gated.** Not ordered. Open decisions in §7.

## 1. Mechanical contract

Everything in this section comes from the base board and must not change.

| Item | Value | Source |
| --- | --- | --- |
| Panel | 20HP x 3U = 101.6 x 128.5mm, 2mm | this design |
| Front board | 101.6 x 128.5mm, 2 layers | this design |
| Base board | 80.9 x 128.5mm, 4 layers | `froemix/AGENTS.md` |
| Mating sockets | CN1 (-17.58, 59.64), CN2 (12.42, -36.36), CN4 (24.5, 59.63) | base board `sections/main.tsx` |
| Base board standoffs | (±36.95, ±60.75), M3 | base board `index.circuit.tsx` |
| Rail screws | (±43.3, ±61.25), M3 | Doepfer 3U |

Both boards are centred on the same panel, so the base board's connector
coordinates are also the front board's socket coordinates — no offset, no
mirror. The sockets sit on the **bottom** of the front board and face down onto
the base board's top-side 2.00mm pin headers.

Bottom-side footprints mirror about their origin (`froemix/AGENTS.md` §5), so the
sockets are authored pre-mirrored (pin 1 at +X in footprint space) and land with
pin 1 on the base board's pin 1. `check_mating.py` asserts pin 1 and pin N of
each socket against the base board's coordinates, which is what catches a broken
mirror before the boards are made.

CN3 (micro SD) is **not** mated: see §7.

### Stack-up (front to back)

```
panel (2mm)            0 mm      pot/jack bushings + nuts, M3 rack screws
front board           10.0 mm    jacks, pots, OLED, LED on top; sockets on bottom
base board            22.0 mm    12mm M3 spacers, tall parts hang off the back
power + Daisy         back
```

`PANEL_GAP = 10.0mm` is the front board to panel distance. It is set by the
Thonkiconn jack (the bushing, ~9mm from the board surface, is the panel
support) and must be confirmed against the fitted parts before ordering: it is
the one number that decides whether the jack tips and pot bushings reach the
panel through a 2mm panel.

## 2. What the front board carries

Fixed by the base board pinout (`README.md`, "Base board <-> eurorack board
interface"):

| Group | Count | Part | Net |
| --- | --- | --- | --- |
| pots | 8 | Alps RK09K1130A5R, 10k linear | `M_ADC_0`..`M_ADC_7` |
| audio in | 2 | PJ398SM mono jack | `J_AUDIO_IN_L/R` |
| audio out | 2 | PJ398SM mono jack | `J_AUDIO_OUT_L/R` |
| CV in | 2 | PJ398SM mono jack | `J_CV_IN_1/2` |
| CV out | 2 | PJ398SM mono jack | `J_CV_OUT_1/2` |
| sync in/out | 2 | PJ398SM mono jack | `J_SYNC_IN`, `J_SYNC_OUT` |
| MIDI in/out | 2 | PJ366ST stereo jack (TRS MIDI) | `J_MIDI_IN_TIP/RING`, `J_MIDI_OUT_TIP/RING` |
| display | 1 | 1.3" 128x64 SPI OLED | `OLED_CS/SCL/DC/SDA/RES` |
| indicator | 1 | 3mm THT LED + 1k series R | `LED` |
| rails | — | `P3V3_A`, `P3V3_D`, `GND` | |

Pots are wired as dividers across `P3V3_A`/`GND`, so the mux inputs can never
leave the CD4051's 0–3.3V range (`froemix/AGENTS.md` §4, "Mux input clamps").
10k keeps the source impedance the CD4051 sees at ≤2.5k and the total pot
current at 2.6mA. Pin 1 = CCW (GND), pin 2 = wiper, pin 3 = CW (3.3V).

Sleeves are GND on every jack. The mono jacks' tip-switch pads are left
unconnected on purpose: inputs are AC-coupled on the base board, so a
normal-to-ground adds nothing, and an output must not be shorted by an empty
socket.

MIDI needs TIP *and* RING, so the two MIDI sockets are stereo (PJ366ST, 3 pads,
slotted holes); the other ten are mono with a switch contact (PJ398SM, 3 pads).

## 3. Panel floorplan

20HP x 3U, origin at the centre, +Y up:

| y (mm) | Contents |
| --- | --- |
| +58 … +25 | 1.3" OLED module (35.5 x 33mm body, window at (0, +40.5)); 4 pots each side |
| +45, +25 | pots: x = ±28, ±43 (15mm pitch) |
| -14 | jacks: audio in L/R, audio out L/R, sync in/out (x = -42 … +42, 16.8mm pitch) |
| -47 | jacks: CV in 1/2, CV out 1/2, MIDI in/out |
| -59.8 | `FROEMIX` + `daisy seed dsp - 20hp` |
| ±61.25 | M3 rack screws (±43.3) |

Pot labels P1–P8 sit under each knob; jack labels under each jack.

## 4. Display

1.3" 128x64 SPI OLED module (SH1106 or SSD1306), 35.5 x 33mm PCB, 7-pin 2.54mm
header along the top edge of the back, active area 29.4 x 14.7mm.

The module is soldered **flat to the top of the front board**, header row at
(0, 57): its 7 pads are a plain 1x7 2.54mm pattern, and the seven `OLED_*`
signals run straight across the board from the CN4 socket (bottom side,
(24.5, 59.63)) to those pads. It is *not* plugged into CN4, because a 2.54mm
socket plus the module's own header would put the glass ~12mm behind the panel,
past the jack bushings.

The panel window is 30 x 15.5mm at (0, 40.5).

## 5. Assembly

The front board is all through-hole: 10 mono jacks, 2 TRS jacks, 8 pots, a 1x7
2.54mm OLED header, 1 LED, 1 resistor, and the three 2.00mm sockets (CN1, CN2,
CN4) on the bottom. None of it is JLCPCB SMT work, so the board is ordered bare
and hand-soldered — the base board carries the SMT.

Socket stack: the base board's 2.00mm pin headers and the 2.00mm female sockets
overlap (the socket body is ~8.5mm), so a 12mm M3 spacer between the boards
leaves ~4mm of air and keeps the Daisy's USB port and the PMOD opening
reachable from the back.

## 6. Verification

`cd euro && bun run gate` (`scripts/gate.sh`), all green:

| Step | Result |
| --- | --- |
| `tsci build` | 2 circuits, 63 traces / 33 vias / 2179mm copper, 0 unconnected |
| `tsci check netlist` | source diagnostics, no errors |
| `tsci check placement` | Errors 0, Warnings 0 |
| `tsci check shorts` | no shorts |
| `tsci check pcb-style` | clean |
| `scripts/check_mating.py` | sockets pin-for-pin on the base board, 20 parts aligned with the panel, window at (0, 40.5) |
| `tsci snapshot --test` | schematic + PCB baselines match |

The 15 `source_pin_missing_trace_warning`s in the build log are the 10 mono-jack
tip switches and CN2 pins 11–15 (encoder reserve) — every one is deliberate.

## 7. Open questions (decide before ordering)

1. **CN3 / micro SD.** The base board exposes the SD bus on CN3, in parallel
   with the Daisy module's own slot. Options: (a) leave CN3 unmated, use the
   Daisy's slot from the back — what this draft does; (b) mate CN3 and add a
   micro-SD socket to the front board with a card slot cut into the panel;
   (c) mate CN3 and pass it to a 1x8 test header.
2. **OLED module.** The draft assumes the common 1.3"/7-pin/2.54mm module with a
   35.5 x 33mm body and no mounting holes. A module with 4x M2 holes needs those
   holes on the front board, and the window size changes with the module.
3. **Jack height / stack-up.** `PANEL_GAP = 10.0mm` decides the whole stack.
   Measure the fitted PJ398SM and RK09K bushings and update §1.
4. **Pots.** Alps RK09K1130A5R, 10k linear, 6mm knurled shaft. Any 9mm vertical
   pot with the same pad pitch drops in; a different shaft needs a different
   knob, not a different board.
5. **Encoder.** CN2 pads 11–15 are reserved on the base board and unused here.
   The draft leaves the five socket pads unconnected on purpose.
6. **Panel material.** PCB panel (2mm FR4, fabs with the board, silkscreen
   legend) as modelled. Aluminium would need a drill file only, and the
   silkscreen labels would become engraving.
7. **LED.** `LED` is Daisy `ADC_1` (pin 23) driving a 1k/3mm indicator. If it
   should be a power indicator instead, it must move to a rail.
8. **Board height.** 128.5mm matches the base board. If the rack rails are the
   limiting item, both boards move to ~110mm together — do not change one.

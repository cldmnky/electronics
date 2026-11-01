# Euro board (front board + panel) — 20HP

The Eurorack front half of Froemix: the board that carries the jacks, the pots,
the UI controls, the MIDI sockets, the display and the panel. It mates with the
16HP base board, which holds all the analog circuitry.

Live design: `euro/index.circuit.tsx` (front board) and `euro/panel.circuit.tsx`
(panel). Both import `euro/lib/layout.ts`, so a panel hole and its part on the
board cannot drift apart — `euro/scripts/check_mating.py` proves it from the
built circuit JSON on every gate run.

Status: **designed, routed, gated.** Not ordered. Open decisions in §8.

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
each socket against the base board's coordinates.

CN3 (micro SD) is **not** mated: see §8.

### Stack-up (front to back)

```
panel (2mm)            0 mm      pot/jack bushings + nuts, M3 rack screws
front board            9.0 mm    jacks, pots, UI, OLED, LEDs on top; sockets on bottom
base board            21.0 mm    12mm M3 spacers, tall parts hang off the back
power + Daisy         back
```

`PANEL_GAP = 9.0mm` is the front board to panel distance. It is set by the
**PJ398SM jack's shoulder** (9mm from the board surface; the panel rests there
and is clamped by the bushing and nut). Everything else has to reach that plane:

| Part | Height to its panel shoulder | At a 9mm gap |
| --- | --- | --- |
| PJ398SM / PJ366ST jack | 9.0mm | sets the gap |
| Alps RK09K pot | ~5.5mm body + M7 bushing | bushing reaches the panel, nut clamps |
| SKQUCAA010 5-way | 10.0mm body | 1.0mm proud of the panel — a cap goes on the 3.2mm stem |
| K6-1212A02 button | 7.3mm cap top | **1.7mm short** — see §8 |

The button is the one part that does not reach. A 2mm riser under the switch, a
taller cap, or a panel-mount button with flying leads are the three fixes;
decide with the part in hand.

## 2. What the front board carries

| Group | Count | Part | Net |
| --- | --- | --- | --- |
| pots | 6 | Alps RK09K1130A5R, 10k linear | `M_ADC_0`..`M_ADC_5` |
| UI joystick | 1 | 5-way multi-control, 5 momentary contacts | `M_ADC_6` (ladder) |
| UI button | 1 | 12mm illuminated tactile, switch + LED | `M_ADC_7` |
| audio in | 2 | PJ398SM mono jack | `J_AUDIO_IN_L/R` |
| audio out | 2 | PJ398SM mono jack | `J_AUDIO_OUT_L/R` |
| CV in | 2 | PJ398SM mono jack | `J_CV_IN_1/2` |
| CV out | 2 | PJ398SM mono jack | `J_CV_OUT_1/2` |
| sync in/out | 2 | PJ398SM mono jack | `J_SYNC_IN`, `J_SYNC_OUT` |
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
| down | 3.3k | 0.82 V |
| left | 6.8k | 1.34 V |
| right | 15k | 1.98 V |

The CD4051's ~250 Ω Ron adds ~2 % error, and a 12-bit ADC puts the closest pair
(press/up) ~370 counts apart. **The ladder is a firmware contract**: the
firmware decodes levels, not pins.

### UI button (illuminated, one mux channel)

Part: Korean Hroparts `K6-1212A02` (LCSC `C692901`, JLCPCB-stocked, 12mm, red
LED). Six electrical pins — C1/C2 are one internally connected switch rail and
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
| +57 … +24 | 1.3" OLED module (35.5 x 33mm body, window at (0, +40.5)); pots and UI either side |
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

1.3" 128x64 SPI OLED module (SH1106 or SSD1306), 35.5 x 33mm PCB, 7-pin 2.54mm
header along the top edge of the back, active area 29.4 x 14.7mm.

The module is soldered **flat to the top of the front board**, header row at
(0, 57): its 7 pads are a plain 1x7 2.54mm pattern, and the seven `OLED_*`
signals run straight across the board from the CN4 socket (bottom side,
(24.5, 59.63)) to those pads. It is *not* plugged into CN4, because a 2.54mm
socket plus the module's own header would put the glass ~12mm behind the panel.

The panel window is 30 x 15.5mm at (0, 40.5).

## 5. Assembly

Mixed, like the base board: 14 SMD parts on the bottom (8 × 0603 resistor,
6 × SOD-323 diode) that JLCPCB can place, and everything else through-hole by
hand: 10 mono jacks, 2 TRS jacks, 6 pots, the joystick, the button, 7 LEDs
(6 jack + 1 panel), the 1x7 OLED header and the three 2.00mm sockets (CN1, CN2,
CN4) on the bottom.

Socket stack: the base board's 2.00mm pin headers and the 2.00mm female sockets
overlap (the socket body is ~8.5mm), so a 12 mm M3 spacer between the boards
leaves ~4mm of air and keeps the Daisy's USB port and the PMOD opening
reachable from the back.

## 6. Verification

`cd euro && bun run gate` (`scripts/gate.sh`), all green:

| Step | Result |
| --- | --- |
| `tsci build` | 2 circuits, 98 traces / 58 vias / 2497mm copper, 0 unconnected |
| `tsci check netlist` | source diagnostics, no errors |
| `tsci check placement` | Errors 0, Warnings 0 |
| `tsci check shorts` | no shorts |
| `tsci check pcb-style` | clean |
| `scripts/check_mating.py` | 3 sockets pin-for-pin on the base board, 26 panel parts aligned with their holes, 31 holes all claimed, window at (0, 40.5) |
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
   ladder's worst case (15k + 10k).
2. **Channel 7 (button)** — two levels: ~0 V pressed, ~3.3 V released. The LED
   is in the pull-up path, so do not drive that channel as an output.

## 8. Open questions (decide before ordering)

1. **Joystick sourcing.** The Alps `SKQUCAA010` is "not recommended for new
   designs" and no LCSC/JLC listing exists — it must be bought from a
   distributor (Newark 59M1983, element14 1435775, Mouser). LCSC stock
   alternative: `SKRHABE010` (C139794, ~$0.39), the same 5-way function but SMD
   and much smaller, so it needs a different panel treatment. **This is the one
   part that can block the build.**
2. **Joystick stem height.** The body is 10mm tall, i.e. flush with
   `PANEL_GAP = 10.0mm`. Either fit a cap that clamps over the 3.2mm stem above
   the panel, or reduce `PANEL_GAP`. Measure the part.
3. **Button height and LED polarity.** The `K6-1212A02` cap tops out at 7.3mm
   against a 9mm panel gap (§1) — it needs a riser, a taller cap, or a
   panel-mount part. Its datasheet draws LED `+`/`-` but does not map them to
   pin positions: check the polarity on the part before soldering, because a
   reversed LED simply never lights.
4. **CN3 / micro SD.** Left unmated; the Daisy's own slot is used from the back.
   Options: mate CN3 and add a front micro-SD socket, or a 1x8 test header.
5. **OLED module.** The draft assumes the common 1.3"/7-pin/2.54mm module, 35.5
   x 33mm, no mounting holes.
6. **Panel material.** PCB panel (2mm FR4) as modelled; aluminium would be a
   drill file only, with engraved labels instead of silkscreen.
7. **Board height.** 128.5mm matches the base board. If the rack rails are the
   limit, both boards move together — do not change one.
8. **Encoder dropped.** Base board CN2 pads 11–15 stay reserved and unconnected;
   the UI is the 5-way plus the button.

## 9. BOM and sourcing

Rule (`froemix/AGENTS.md` §9): **LCSC/JLCPCB first, Electrokit for anything LCSC
does not stock.** No other supplier. Parts with a `supplierPartNumbers` entry in
the TSX are LCSC; the rest are listed here.

### LCSC (in the design, code in the TSX)

| Qty | Part | Code |
| --- | --- | --- |
| 6+1 | 1k 0603 (ladder "up", panel LED series R, button series R) | C21190 |
| 1 | 2.2k 0603 (ladder "down" in 2.2k form, jack LED ballast x6 separate) | C4190 |
| 1 | 4.7k 0603 (ladder "left") | C23162 |
| 2 | 10k 0603 (joystick pull-up, ladder "right") | C25804 |
| 1 | 2.2k x6 jack LED resistors | C4190 |
| 6 | 1N4148WS SOD-323 (jack LED diodes) | C2128 |
| 1 | 12mm illuminated button, red, K6-1212A02 | C692901 |

### Electrokit (`https://www.electrokit.com/`)

| Qty | Part | Art. nr | Price | Note |
| --- | --- | --- | --- | --- |
| 6 | Alps RK09K, 10k lin, 6mm knurled shaft | 41012043 | 25 SEK | **exact match** to the footprint - the linked datasheet names `RK09K1130A5R` |
| 1 | 5-way navigation switch, 12.4 x 12.4mm | 41035331 | 19.50 SEK | function matches, **footprint differs**: 6 pins on a 12.9 x 8.0mm pattern, 4 x 1.4mm + 2 x 1.2mm drills |
| 1 | 1.3" 128x64 SPI OLED, Waveshare "1.3inch OLED Module (C)" | 41024144 | 109 SEK | **footprint differs**: SH1107, 30 x 39.5mm board, pin order VCC/GND/DIN/CLK/CS/DC/RST |

### Blocked: the jacks

Electrokit has **no PCB-mount mono 3.5mm jack** (its `41001190` is chassis-mount
with solder lugs, wrong pad layout, and out of stock until 2026-11-13). Its
PCB-mount stereo option is the Amphenol `ACJS-MV35-3` (`41011901`, 18.50 SEK,
10 in stock, 2-row pin pattern, three 2.0mm pegs, no slots).

So the Thonkiconn `PJ398SM` / `PJ366ST` that `lib/footprints.tsx` currently
implements cannot be sourced under the rule. The choices:

1. **Move all 12 jacks to the Amphenol `ACJS-MV35-3`** - one part for mono and
   TRS MIDI, M6 bushing, 6.0mm panel hole (the panel does not change), but the
   board footprint changes for every jack and Electrokit had only 10 in stock
   for a 12-piece build.
2. **Find a 3.5mm PCB-mount jack at LCSC** - LCSC-first, so this is the
   preferred direction if such a part exists with a usable layout.
3. Keep the Thonkiconn and break the rule - not acceptable without a decision.

A lookup for option 2 (plus LCSC confirmation for the sockets, LEDs, OLED and
pot) is the next step; the jack decision lands here before any footprint work.

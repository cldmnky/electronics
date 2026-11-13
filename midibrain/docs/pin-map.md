# Pi / Pico allocation — preliminary

Source: [Pico 2 datasheet](https://datasheets.raspberrypi.com/pico/pico-2-datasheet.pdf) figures 2 and 4 and [RP2350 datasheet](https://datasheets.raspberrypi.com/rp2350/rp2350-datasheet.pdf); [Pi GPIO documentation](https://www.raspberrypi.com/documentation/computers/raspberry-pi.html). Pi numbering below is **40-pin physical position**, with BCM GPIO in parentheses; Pico numbering is **module physical position**, with RP2350 GPIO in parentheses. The Pico 2 keeps the 21 × 51 mm 40-pin module outline, so sockets and pin positions are unchanged from the Pico 1 plan. `tscircuit/lib/pin-map.ts` checks for allocation duplicates. This is a logical allocation, **not a connector footprint or approved cable pinout**.

| Function | Pi header | Pico module | Direction |
|---|---|---|---|
| SPI0 MOSI | 19 (GPIO10) | 21 (GP16 / SPI0 RX) | Pi → Pico |
| SPI0 MISO | 21 (GPIO9) | 25 (GP19 / SPI0 TX) | Pico → Pi |
| SPI0 clock | 23 (GPIO11) | 24 (GP18 / SPI0 SCK) | Pi → Pico |
| SPI0 CE0 | 24 (GPIO8) | 22 (GP17 / SPI0 CSn) | Pi → Pico |
| READY / IRQ | 18 (GPIO24) | 26 (GP20) | Pico → Pi through the SN74LVC1G126 buffer; on-target polarity/timing still a bench item |
| Reset request | 22 (GPIO25) | 30 (RUN) | 2N7002 open-drain reset implemented; power-sequence behaviour still a bench item |
| Common ground | 20 or 25 | any Pico GND pin | shared logic ground; electrical connector assignment pending |

Pico UART0: GP0/pin 1 TX → MIDI OUT 1; GP1/pin 2 RX ← isolated MIDI IN 1. UART1: GP4/pin 6 TX → MIDI OUT 2; GP5/pin 7 RX ← isolated MIDI IN 2. The jacks are not tied directly to the UARTs: the board carries a TLP2361 opto receiver on each input and an AO3401A high-side output stage. The circuits are designed and simulate; threshold, current and fault behaviour are unmeasured (`docs/TODO.md`).

| Pico signal | Pico GPIO / physical pin |
|---|---|
| NAV up/down/left/right/press | GP6/9, GP7/10, GP8/11, GP9/12, GP10/14 |
| Value encoder A/B | GP11/15, GP12/16 |
| Value encoder push | GP2/4 |
| REC / OVER DUB / STOP / PLAY / PLAY START | GP13/17, GP14/19, GP15/20, GP21/27, GP3/5 |
| SHIFT / EXIT | GP28/34 |
| REC / OVER DUB / PLAY LEDs | GP26/31, GP27/32, GP22/29; 330 Ω to 3.3 V |
| Available for future expansion | **none on the Pico header**; use an I2C expander or matrix if more controls are added |

Pico pins 39 (VSYS), 40 (VBUS), 36 (3V3 OUT), 37 (3V3 EN), 30 (RUN) are not generic IO. Pico is 3.3 V logic; do not apply 5 V to either board's GPIO or tie Pi 3.3 V to Pico 3.3 V OUT. The power branch is designed: B5819W Pi 5 V → Pico VSYS, SN74LVC1G126 buffers for MISO/READY enabled only from Pi 3.3 V, and a 2N7002 reset. Off-state injection, sequencing and backfeed are simulated only; they remain physical acceptance items (`scope_power_sequences`, `scope_spi`).

Connector part, mating-face pin order, wire length, shielding, decoupling and power branch protection remain open. Hardware pin mux and continuity must be tested with actual Pi OS/Pico SDK versions.

## Pi mounting panel and the harness (owner decision 2026-11-12)

The Pi 5 (with Active Cooler + M.2 HAT+ + NVMe SSD) bolts to the rear face of its
own 20HP/3U mounting panel beside the main module in the same row; the table above is unchanged as a
logical allocation, but every Pi signal now crosses **the 40-wire GPIO ribbon
from `J_PI` (rear board, BH254V-40P) to the Pi's 40-pin header** instead of a
header on the rear board. Plan 150–350 mm runs (side by side in the row; a planning estimate).
Before power: pin-1 orientation of the ribbon and all-40 continuity from `J_PI`
to the Pi header against the net list. The 1 MHz SPI link, the 100 Ω series
resistors and the buffered MISO/READY are unchanged.

Three more runs span the same gap and none has a proven length: the display DSI
FPC (kit cables are 50/120 mm, so a longer 15-pin 0.5 mm FPC plus the 22-pin
adaptation is expected), the J_PWR power-button pigtail to the Pi's bare `J2`
pads (longer than the recorded 2 × 6-inch DuPont pair), and the three USB
feedthrough cables — the feedthroughs themselves stay on the 30HP panel,
unchanged. The M.2 HAT+ must leave the 40-pin header free for the ribbon; the
fitted HAT's MPN and connection method are pending (`docs/TODO.md`). Evidence:
acceptance item `pi_panel_harness`; geometry: `PI_PANEL_*` in
`tscircuit/lib/layout.ts`.

## Panel POWER button (owner-approved 2026-11-06; pigtail amended 2026-11-06)

A panel momentary switch (B3F-4055 family, same as the transport row) is
wired to the **Pi 5's remote power-button connector** (the 2-pin header
paralleling the onboard power button — no custom power logic):

| Function | Path | Notes |
|---|---|---|
| POWER | front-board SW7 → J_PWR (right-angle Harwin M20-9960245, 1 x 2, on the front board's REAR face at the top notch, mating axis +Y) → 2x 6-inch 2.54 mm female-female DuPont jumpers (user-supplied) → the Pi end: J2 is TWO BARE PADS (Raspberry Pi power-button documentation and `images/j2.jpg`, retrieved 2026-11-10), so a correctly fitted header or a qualified wired connection is required and pending | off→press = power on; on→press = graceful shutdown request (OS handles clean halt); long-press = PMIC hard cut, last resort only |

The jumpers are **ACC A-0270 stock 6-inch F-F DuPont** leads the owner already
owns; they are not a board part. The earlier "front SW7 → 2 IDC conductors →
rear J_PWR" route is superseded: the direct 2 x 13 front/rear pair keeps every
conductor it had (all three ground conductors stay ground), the rear board
carries no power-button part, and no net in the 2 x 13 interconnect table (`INTERCONNECT_NETS` in
`tscircuit/lib/interconnect.tsx`, used by both boards) changes.
J_PWR is the right-angle Harwin M20-9960245 for those leads only — it has no
connection to any power rail, and its two nets reach only SW7.

Placement: away from the transport row (no accidental presses — panel
corner, see layout). The app hooks the shutdown path: project flush +
sync + `SAFE TO POWER OFF` before halt. Standby power (5V inlet live, Pico
powered in soft-off) is accepted; a master switch at the inlet is a
deferred option, not part of this change.

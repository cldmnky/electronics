# Pi / Pico allocation — preliminary

Source: [Pico 2 datasheet](https://datasheets.raspberrypi.com/pico/pico-2-datasheet.pdf) figures 2 and 4 and [RP2350 datasheet](https://datasheets.raspberrypi.com/rp2350/rp2350-datasheet.pdf); [Pi GPIO documentation](https://www.raspberrypi.com/documentation/computers/raspberry-pi.html). Pi numbering below is **40-pin physical position**, with BCM GPIO in parentheses; Pico numbering is **module physical position**, with RP2350 GPIO in parentheses. The Pico 2 keeps the 21 × 51 mm 40-pin module outline, so sockets and pin positions are unchanged from the Pico 1 plan. `tscircuit/lib/pin-map.ts` checks for allocation duplicates. This is a logical allocation, **not a connector footprint or approved cable pinout**.

| Function | Pi header | Pico module | Direction |
|---|---|---|---|
| SPI0 MOSI | 19 (GPIO10) | 21 (GP16 / SPI0 RX) | Pi → Pico |
| SPI0 MISO | 21 (GPIO9) | 25 (GP19 / SPI0 TX) | Pico → Pi |
| SPI0 clock | 23 (GPIO11) | 24 (GP18 / SPI0 SCK) | Pi → Pico |
| SPI0 CE0 | 24 (GPIO8) | 22 (GP17 / SPI0 CSn) | Pi → Pico |
| READY / IRQ | 18 (GPIO24) | 26 (GP20) | Pico → Pi, polarity and bias pending |
| Reset request | 22 (GPIO25) | 30 (RUN) | **Open-drain/reset circuit pending**; do not wire a push-pull GPIO directly without analysing power sequencing |
| Common ground | 20 or 25 | any Pico GND pin | shared logic ground; electrical connector assignment pending |

Pico UART0: GP0/pin 1 TX → MIDI OUT 1; GP1/pin 2 RX ← isolated MIDI IN 1. UART1: GP4/pin 6 TX → MIDI OUT 2; GP5/pin 7 RX ← isolated MIDI IN 2. MIDI jacks are not tied directly to UARTs; isolation and output drivers are unimplemented.

| Pico signal | Pico GPIO / physical pin |
|---|---|
| NAV up/down/left/right/press | GP6/9, GP7/10, GP8/11, GP9/12, GP10/14 |
| Tempo encoder A/B | GP11/15, GP12/16 |
| Tempo encoder push | GP2/4 |
| REC / OVER DUB / STOP / PLAY / PLAY START | GP13/17, GP14/19, GP15/20, GP21/27, GP3/5 |
| SHIFT / EXIT | GP28/34 |
| REC / OVER DUB / PLAY LEDs | GP26/31, GP27/32, GP22/29; 330 Ω to 3.3 V |
| Available for future expansion | **none on the Pico header**; use an I2C expander or matrix if more controls are added |

Pico pins 39 (VSYS), 40 (VBUS), 36 (3V3 OUT), 37 (3V3_EN), 30 (RUN) are not generic IO. Pico is 3.3 V logic; do not apply 5 V to either board's GPIO or tie Pi 3.3 V to Pico 3.3 V OUT. USB programming vs Pi-powered branch requires isolated power-path design and an off-state GPIO injection audit. The Pico datasheet §4.5 describes diode/FET alternatives; none has been selected here.

Connector part, mating-face pin order, wire length, shielding, decoupling and power branch protection remain open. Hardware pin mux and continuity must be tested with actual Pi OS/Pico SDK versions.

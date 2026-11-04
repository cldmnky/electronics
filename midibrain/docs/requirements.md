# Requirements baseline

## Confirmed by owner

- Two custom PCBs and a separate **26HP/3U** front panel. The front/UI PCB is **128 × 112 mm**; the rear/base PCB is **20HP (101.6 × 112 mm)**. Target installed depth ≤110 mm remains unchanged. CAD top faces the panel and rear faces the rack cavity.
- Removable Raspberry Pi 5, mechanically supported through its mounting holes; removable Pico or Pico 2. Pi ↔ Pico SPI with READY/IRQ and common ground.
- UMC1820 plus ADA8200 are independently powered; audio and headphone I/O remain on those units. Capture 18 channels 48 kHz/24-bit to NVMe; playback returns via the UMC1820. 18 populated channels require synchronized stereo S/PDIF in addition to the 16 analog inputs.
- A powered external USB MIDI hub, 2 × Type-A TRS MIDI inputs and 2 × outputs; master clock, transport, MIDI routing/filtering/merging.
- **3.5-inch 480 × 800 DSI display** (Waveshare 3.5inch DSI LCD (H), SKU 33087), five-way nav, tempo encoder, five transport buttons (REC, OVER DUB, STOP, PLAY, PLAY START), a SHIFT/EXIT button below the nav switch, and three LEDs above REC/OVER DUB/PLAY. The display uses the Pi 5 DSI1 connector, 3.3 V and I2C touch on one 15-pin cable; no HDMI or touch-USB cable. The five transport buttons go under the display; TEMPO and NAV go to its right. The three LEDs are 0805 SMD and assembled on the front board. Retain SPI between Pi and Pico.
- External front USB-C power connection feeding the Pi's USB-C input through a qualified cable/feedthrough; no Eurorack power connector.
- **JLCPCB manufacturing:** PCB fabrication and SMD assembly at JLCPCB, with every SMD part sourced from JLCPCB/LCSC and identified by an LCSC C-number. Through-hole controls, jacks, sockets and headers may be hand-soldered. Assembly side and hand/factory assignment must be explicit; no implicit assignment by package type.
- Target 2 h zero-xrun capture; ≤1 ms TRS clock timing error; ±0.1% tempo. A ≤1 s power-loss target cannot be guaranteed without tested backup power and storage recovery.

## Provisional, not a requirement freeze

- Front/UI PCB **128 × 112 mm**, rear/base PCB **101.6 × 112 mm**, each 1.6 mm and two layers. The rear board has a provisional 58 × 49 mm Pi mounting-hole grid plus board-to-front-board standoff holes. The Pi drawing's dimensions are explicitly not production data. Both boards now contain prototype circuits and routed copper but are **not fabrication-released**.
- Visual positions in `tscircuit/lib/layout.ts` put the requested controls around a nominal 84 mm square display, but do not establish actual cutout shapes, component footprints, front-to-back spacing or cable envelopes.
- Pico 2 (RP2350), full-size Pi M.2 HAT+ plus Active Cooler are candidates, not a proven combination within the enclosure. Pi 5 + NVMe + cooling mounting, ventilation, GPIO access and all cables remain to be checked.
- SPI signal allocation is preliminary; no claim of safe power sequencing/backfeed protection.
- No manufacturing release, schematic/ERC clearance, assembly assignment completion or hardware test exists yet.

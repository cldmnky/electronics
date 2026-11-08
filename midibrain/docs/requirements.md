# Requirements baseline

## Confirmed by owner

- Two custom PCBs and a separate **32HP/3U** front panel. The front/UI PCB is
  **128 × 112 mm**; the rear/base PCB is **20HP (101.6 × 112 mm)**; installed
  depth target ≤110 mm. CAD top faces the panel and the rear faces the rack
  cavity. 32HP and the knob-column move (47 → 52 mm) are owner decisions
  recorded in [decisions.md](decisions.md).
- Removable Raspberry Pi 5, mechanically supported through its mounting holes;
  removable Pico 2. Pi ↔ Pico SPI with READY/IRQ and common ground.
- UMC1820 plus ADA8200 are independently powered; audio and headphone I/O stay
  on those units. Capture 18 channels at 48 kHz/24-bit to NVMe; playback
  returns via the UMC1820. The 18 populated channels require synchronized
  stereo S/PDIF in addition to the 16 analog inputs.
- A powered external USB MIDI hub, 2 × Type-A TRS MIDI inputs and 2 × outputs;
  master clock, transport, MIDI routing/filtering/merging.
- **Waveshare 4.3inch DSI LCD** (ElectroKit 4103 5687), 800 × 480, DSI video
  and I2C touch on one 15-pin cable; no HDMI or touch-USB cable. Five-way nav,
  context-aware **VALUE** encoder (Bourns PEC11R-4015F-S0024), five transport
  buttons (REC, OVER DUB, STOP, PLAY, PLAY START), SHIFT/EXIT, and three LEDs
  (REC / OVER DUB / PLAY) beside their buttons on light pipes. Retain SPI
  between Pi and Pico.
- External front USB-C power feeding the Pi's USB-C input through a qualified
  cable/feedthrough; no Eurorack power connector.
- **JLCPCB manufacturing:** PCB fabrication and SMD assembly at JLCPCB, every
  SMD part from JLCPCB/LCSC with an LCSC C-number. Through-hole controls,
  jacks, sockets and headers may be hand-soldered; the PEC11R needs a qualified
  process. Assembly side and hand/factory assignment must be explicit.
- Target 2 h zero-xrun capture; ≤1 ms TRS clock timing error; ±0.1 % tempo.
  A ≤1 s power-loss target cannot be guaranteed without tested backup power
  and storage recovery.

## Provisional, not a requirement freeze

- Front/UI PCB 128 × 112 mm and rear/base PCB 101.6 × 112 mm, each 1.6 mm and
  two layers. The rear board has a 58 × 49 mm Pi mounting-hole grid plus
  board-to-front-board standoff holes. The Pi drawing is not production data.
  Both boards contain prototype circuits and routed copper but are **not
  fabrication-released**.
- Positions and dimensions in `tscircuit/lib/layout.ts` are planning values.
  They do not establish cutout fit, cable envelopes, component clearances or
  installed depth.
- Pico 2, full-size Pi M.2 HAT+ and Active Cooler are candidates, not a proven
  combination. Pi 5 + NVMe + cooling mounting, ventilation, GPIO access and all
  cables remain to be checked.
- No manufacturing release, ERC clearance, assembly assignment completion or
  physical hardware test exists yet.

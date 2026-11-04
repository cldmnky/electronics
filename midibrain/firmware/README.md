# mnkybox Pico firmware (RP2350 / Pico 2)

`pico/` holds the on-target firmware plus host-testable shared headers.
`protocol.hpp` (framing/CRC) and `midi.hpp` (parser, event codecs) have no Pico
SDK dependency; `protocol_test.cpp` runs them on the host under `make test`.
`main.cpp` is the RP2040 application; `pins.h` is generated from
`tscircuit/lib/pin-map.ts` (never hand-edit it).

## Toolchain setup (once per machine)

```sh
# ARM toolchain + Pico SDK live outside the repo:
mkdir -p ~/pico && cd ~/pico
curl -sSL -o toolchain.tar.xz \
  "https://developer.arm.com/-/media/Files/downloads/gnu/13.3.rel1/binrel/arm-gnu-toolchain-13.3.rel1-darwin-arm64-arm-none-eabi.tar.xz"
tar xf toolchain.tar.xz
git clone --depth 1 https://github.com/raspberrypi/pico-sdk.git
(cd pico-sdk && git submodule update --init --depth 1 lib/tinyusb)  # USB console
```

## Build

```sh
export PICO_SDK_PATH=$HOME/pico/pico-sdk
export PATH=$HOME/pico/arm-gnu-toolchain-*/bin:$PATH
# from midibrain/:
make firmware   # -> firmware/pico/build/midibrain.uf2
```

`make test` does **not** build firmware (SDK-dependent); it runs the host
codec/parser tests, the `pins.h` freshness check, the hardware gates and SPICE.

## Flash

1. Hold BOOTSEL, plug in Pico USB (or tap reset with BOOTSEL held).
2. Copy `firmware/pico/build/midibrain.uf2` onto the `RP2350` drive (Pico 2 shows `RP2350`, not `RPI-RP2`).
3. Debug console: USB serial (115200), boot banner + 1 Hz `[stat]` lines.

## What the firmware owns

- Both MIDI UARTs at 31250 baud (IRQ rings with per-byte timestamps, parser
  with running status + realtime interleave; SysEx skipped in v1).
- Debounced panel inputs (1 kHz scan), quadrature tempo encoder (SHIFT = fine),
  three LEDs (local mirror, Pi-overridable via LED_SET).
- Always-on local MIDI clock (24 ppq from boot at the current tempo,
  START/CONTINUE/STOP bytes on both UARTs) driven by encoder tempo or Pi
  SET_TEMPO, plus timestamped tick frames per tick and transport-state
  announcements so the Pi can mirror phase and rolling state to USB.
  Panel PLAY/PLAY_START/STOP drive transport locally (no Pi round-trip).
- SPI0 slave @1 MHz to the Pi: CSn-polled 32-byte exchanges, HELLO/ACK
  handshake with ROSC-derived session, sequence dedup, READY pin, 8 s watchdog.

See `docs/protocol.md` for the link contract, the CONTROL_EVENT registry and
the prototype limitations (no SysEx forwarding, no scheduled-output timeline,
master Must hold CS across full 32-byte transfers with >= 10 us setup).

# Firmware

Daisy Seed firmware for the froemix base board plus the
[apfelaudio eurorack-pmod](https://github.com/apfaudio/eurorack-pmod) on `H1`.

**Status: M0 scaffold.** It builds, links and is ready for bring-up; nothing
here has run on hardware yet (Rev 0.1 is at the fab). The contract doc is
[`../doc/FIRMWARE.md`](../doc/FIRMWARE.md) - read that before changing
anything in `src/`, and keep it true.

## Requirements

- `arm-none-eabi-gcc` (Arm GNU Toolchain), `make`, `dfu-util`, and optionally
  `openocd` for SWD debugging. With nix:

  ```bash
  nix shell nixpkgs#gcc-arm-embedded nixpkgs#dfu-util nixpkgs#openocd
  ```

## Build and install

```bash
make deps          # clone + pin libDaisy/DaisySP into external/, apply patches/
make               # libDaisy, DaisySP, then build/froemix.bin
make size          # linked image size
make program-dfu   # USB DFU (hold BOOT, tap RESET on the Seed)
```

`make deps` is idempotent and fails hard if a pinned SHA moved or a patch no
longer applies. `external/` and `build/` are gitignored; there is no vendored
copy of either library in this repository.

Bench upload without touching flash:

```bash
make APP_TYPE=BOOT_SRAM program
```

Debugging is SWD on the Seed's `J2` (1.27 mm 2x5, ARM Cortex 10-pin). Do **not**
mass-erase over SWD: the factory bootloader lives in internal flash.

## Layout

```
firmware/
├── src/pins.hpp          board net -> Seed pin, checked against the board source
├── src/contracts.hpp     hardware contracts as constants + static asserts
├── src/board.{hpp,cpp}   bring-up order and the status of each subsystem
├── src/audio/audio6.*    6-channel audio: SAI1 stereo + SAI2 4-slot TDM
├── src/hw/               ak4619, pmod, controls, cv, sync, midi, display
├── src/selftest.*        M1 diagnostics: OLED + MIDI SysEx report
├── patches/              local libDaisy changes (currently: TDM slots)
├── scripts/fetch_deps.sh pinned clone + patch application
└── Makefile
```

## Checks

```bash
python3 ../tools/check_firmware_pins.py   # pin map vs tscircuit/ + KiCad symbol
make check-pins                           # same thing, from here
```

There is no serial console on this board: diagnostics go to the OLED and to
MIDI SysEx (see `src/selftest.cpp`).

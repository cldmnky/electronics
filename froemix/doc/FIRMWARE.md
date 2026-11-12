# Firmware

The Daisy Seed firmware for the base board plus the
[apfelaudio eurorack-pmod](https://github.com/apfaudio/eurorack-pmod) on `H1`.
This document is the firmware contract: what the code is attached to, what it
must not do, and what has not been verified yet.

**State: M0 scaffold** (`froemix/firmware/`). It builds against pinned Daisy
libraries and links; **no line of it has run on hardware** - Rev 0.1 is at the
fab. Every claim below that depends on a measurement is marked *bench*.

Related documents: [`FRONTBOARD-INTERFACE.md`](FRONTBOARD-INTERFACE.md) (the
electrical contract with the front board), [`EURO-BOARD.md`](EURO-BOARD.md) §2/§7
(ladder + button), [`DESIGN-REVIEW.md`](DESIGN-REVIEW.md) R03/R09/R10/R14,
[`POWER-BUDGET.md`](POWER-BUDGET.md) (§5 of the interface doc builds on it).

## 1. What the firmware is attached to

Derived from `tscircuit/sections/main.tsx`, `tscircuit/lib/pins.ts` and the
Electrosmith `Daisy_Seed` symbol, and checked by
`tools/check_firmware_pins.py` (gate: `make -C firmware check-pins`).

| Pad | libDaisy | Net | Firmware role |
| --- | --- | --- | --- |
| 1 | `D0` | `MULTIPLEX_S0` | CD4051 A |
| 2-7 | `D1`-`D6` | `SD_DAT3..0`, `SD_CMD`, `SD_CLK` | SDMMC1 (not used yet) |
| 8 | `D7` | `OLED_CS` | SPI1 NSS |
| 9 | `D8` | `OLED_SCL` | SPI1 SCK |
| 10 | `D9` | `OLED_DC` | **GPIO out on the SPI1 MISO pad** |
| 11 | `D10` | `OLED_SDA` | SPI1 MOSI |
| 12/13 | `D11`/`D12` | `SCL`/`SDA` | I2C1: eurorack-pmod (AK4619, PC9635, expanders) |
| 14/15 | `D13`/`D14` | `USART1_TX`/`RX` | MIDI OUT / MIDI IN (TRS Type A) |
| 16-19 | - | `AUDIO_IN/OUT_L/R` | Seed codec, SAI1 |
| 22 | `D15` | `OLED_RES` | GPIO out |
| 23 | `D16` | `LED` | panel indicator LED |
| 24 | `D17` | `MULTIPLEX_ADC_1` | ADC, CD4051 common node |
| 25/26 | `D18`/`D19` | `CV_IN_1`/`CV_IN_2` | ADC, direct (not muxed) |
| 27 | `D20` | `SYNC_OUT` | GPIO out -> TL074 -> 0/5 V gate |
| 28 | `D21` | `SYNC_IN` | GPIO in, **inverted** (Q1 collector) |
| 29 | `D22` | `CV_OUT_1` | **DAC channel TWO** |
| 30 | `D23` | `CV_OUT_2` | **DAC channel ONE** |
| 31-35 | `D24`-`D28` | `MCLK`, `SDI`, `SDO`, `LRCK`, `BICK` | SAI2, TDM to the eurorack-pmod |
| 36/37 | `D29`/`D30` | `MULTIPLEX_S1`/`S2` | CD4051 B/C, shared with USB D-/D+ |
| 38-40 | - | `P3V3_D`, `P5V`, `GND` | power |

Consequences that are easy to get wrong:

- **USB is unusable at runtime.** `D29`/`D30` are the USB data lines and the two
  upper mux selects. No USB MIDI, no USB audio. DFU upload still works (the
  factory bootloader owns those pins before the application runs); debugging is
  SWD on the Seed's `J2`.
- Mux channel number is `S0 + 2*S1 + 4*S2`, and the three selects span two
  ports (`D0`, `D29`, `D30`).
- **There is no serial console.** The only USART is the MIDI jack; diagnostics
  are MIDI SysEx and the OLED.
- The DAC nets are crossed: jack 1 is DAC channel TWO, jack 2 is channel ONE.

## 2. Audio topology

Six channels at 48 kHz, 32-frame blocks:

| Channel | Source | Path |
| --- | --- | --- |
| 0, 1 | Seed WM8731 (SAI1, stereo I2S 24-bit) | `J_AUDIO_IN_*` gain 0.100 in, `J_AUDIO_OUT_*` gain 3.300 out |
| 2-5 | AK4619VN (SAI2, 4-slot 32-bit TDM) | eurorack-pmod's 4 in / 4 out jacks, DC-coupled |

- SAI2: block B is master/transmit (`D25` = codec SDIN1), block A is
  slave/receive (`D26` = codec SDOUT1), `D27` LRCK, `D28` BICK, `D24` MCLK.
- The two peripherals are **independent masters** on the same PLL: same rate, no
  guaranteed phase. `Audio6` services SAI2's buffer from SAI1's DMA callback and
  targets the half named by `SaiHandle::GetOffset()` (the half the codec's DMA
  just finished), which is safe at the cost of at most one extra block of
  latency. *bench: confirm no drift/duplication and measure the added latency.*
- The TDM slot -> eurorack-pmod jack order is the reference driver's
  (`slot 0..3` = channels 2..5). *bench: identify each jack; nothing in this
  repository pins it down.*
- AK4619 register image (`src/hw/ak4619.cpp`) is the eurorack-pmod reference
  driver's (TDM=1, I2S-compatible data format, 32-bit slots, 48 kHz,
  256*fs MCLK, DC-cancel HPF off). It is a port, not a re-derivation.
- The eurorack-pmod's `^MUTE`/`PDN` pin is wired to `P3V3_D` on `H1` pin 6, so
  the module is permanently unmuted: firmware has no control over it.
- `Audio6` does not use `daisy::AudioHandle`: its two-SAI mode is a stub
  ("Data will be filled externally") and it has no TDM. `Board::Init` releases
  the Seed's SAI1 with `audio_handle.DeInit()` before `Audio6` takes both
  peripherals. *bench: compare against skipping the Seed's `ConfigureAudio()`
  (what the eurorack-pmod reference patched); the DeInit route also links
  libDaisy's 16 KB of static audio buffers, which is why `RAM_D2_DMA` sits at
  ~61 % of 32 KB.*

## 3. Contracts encoded in `src/contracts.hpp`

| Contract | Value | Source |
| --- | --- | --- |
| DAC output buffer | **DISABLED** (hard) | interface §4, review R03 |
| CV out | `V_jack = 1.5121 * V_dac`, 0-4.99 V, +-4.04 % worst case, +-60 mV offset | interface §4, R09 |
| CV in | `V_adc = 1.6096 - 0.3010 * V_jack`, +-5 V only, 1 % parts | interface §3, R10 |
| Audio in / out gain | 0.100 / 3.300, no DC on outputs | interface §4 |
| Mux | 8 channels: 0-5 pots, 6 joystick ladder, 7 button | interface §2, EURO-BOARD §2 |
| Ladder levels | idle 3.30, right 1.65, left 1.05, down 0.60, up 0.30, press 0.00 V | EURO-BOARD §2/§7 |
| Sync in | inverted, ~0.72 V threshold, 0..+5 V intended | interface §4, R04 |
| Sync out | ~5 V gate from a TL074 comparator, max rate unmeasured | interface §4 |
| MIDI | 31250 baud, TRS Type A both directions | interface §2 |

The ladder decode thresholds are midpoints of that table; the mux settle time
and the sync-out rate have **no** qualified value, and the constants say so
(`kMuxSettleTimeQualified`, `kSyncOutRateQualified`).

## 4. Dependencies and the one local patch

Pinned in `firmware/scripts/fetch_deps.sh`, cloned into `firmware/external/`
(gitignored - never vendored, never floating):

- **libDaisy `v9.0.0`** = `08087203debc646d3710ec8caa35bac6cf20ab5e` (2026-09-29)
- **DaisySP** = `2c72eaf9eac5fc0dca1919d65d606da907832618` (2026-09-28)

The patch `patches/0001-libdaisy-sai-tdm-slots.patch` adds 32-bit-slot TDM to
`SaiHandle` (`Config::tdm_slots` plus an `InitProtocolTDM()` modelled on the
HAL's `SAI_InitPCM`). Upstream has no TDM support (electro-smith/libDaisy#499),
and the eurorack-pmod path needs it. The register-level choices (free protocol,
FS before first bit, active-low FS, 32-bit slots) come from
[heartwerker/libDaisy](https://github.com/heartwerker/libDaisy) branch
`feature/audio-tdm-codec-ak4619` (MIT) - the driver the eurorack-pmod Daisy
example runs - not from a fresh datasheet derivation. Nothing else in libDaisy
is modified.

Rules: bump a pin deliberately, re-run `make deps` (which fails if a patch no
longer applies), rebuild, and update this section with the date and what was
re-verified. Do not switch to a floating branch.

## 5. Build, flash, debug

```bash
cd froemix/firmware
nix shell nixpkgs#gcc-arm-embedded nixpkgs#dfu-util nixpkgs#openocd
make deps && make            # -> build/froemix.{elf,bin,hex}
make program-dfu             # hold BOOT, tap RESET on the Seed
make APP_TYPE=BOOT_SRAM program   # RAM run, leaves flash alone
```

- `APP_TYPE=BOOT_QSPI` is the default: app in QSPI, started by the factory
  bootloader. `BOOT_NONE` (libDaisy's default) would link at `0x08000000` and
  destroy the bootloader - do not use it on this board.
- SWD: the Seed's `J2`, 1.27 mm 2x5 Cortex 10-pin (`SWDIO`, `SWCLK`, `NRST`,
  `GND`). Attach-and-debug only; do not `mass_erase`.
- Current image: 108,736 B in QSPI (1.3 %), 68,888 B SRAM, 20,032 B of the
  32 KB DMA section.

## 6. What the scaffold does today

`main()`: `Board::Init()` -> `Board::StartAudio()` -> 1 kHz loop of
`Controls::Update()`, `MidiIo::Poll()`, panel LED, `Selftest::Tick()`.

- All 8 mux channels, both CV inputs, sync in/out, MIDI in, the OLED, the DACs
  (buffer off), and the eurorack-pmod codec + LED driver are initialised.
- The audio callback runs one engine only, `engine::Silent` (`src/engine/`),
  which zeroes all six outputs: the converter/DMA path is exercised and a
  DC-coupled output cannot step. That class is the engine seam; the DSP is M3.
- `Selftest` emits one MIDI SysEx frame per second and mirrors it on the OLED:
  `T<secs>,P<p1..p6>,J<joy>,B<btn>,C<cv1>,<cv2>,S<sync>,F<flags>` where each
  value is 0-99 and `F` is a hex bitmask (pmod codec, pmod LEDs, audio, display).
- Panel LED mirrors the MODE button until an engine defines it.

Not in this scaffold: DSP (only `engine::Silent`), SD card and presets,
jack-insertion
detection, the eurorack-pmod touch controller and calibration EEPROM, MIDI
voice handling beyond parsing, and any calibration persistence.

## 7. Bench verification (M1) - none of these are done

1. **Mux settle time.** Worst case 250 ohm Ron into a 100 k pot (25 k source) and
   the released MODE node (10 k pull-up). Measure a low->released-MODE channel
   switch; set `kMuxSettleTimeQualified` only with numbers.
2. **Ladder decode.** All five directions + press + idle on a real joystick,
   with the threshold table; the press/up pair is only ~370 counts apart.
3. **CV in calibration.** ±5 V sweep per channel against `1.6096 - 0.301*V`;
   record the measured gain/offset per channel (1 V/oct needs it).
4. **CV out calibration.** Both jacks, endpoints and offset; confirm buffer-off
   (`DacHandle::BufferState::DISABLED` actually reaches 0 V and full scale) and
   the crossed channel map.
5. **Sync in.** Threshold and polarity at the jack; **sync out** maximum toggle
   rate through the TL074 comparator (this sets the published limit).
6. **TDM.** Which slot appears on which eurorack-pmod jack; SAI1/SAI2 phase and
   the extra latency; no sample duplication at block boundaries.
7. **Seed SAI1 handover.** `audio_handle.DeInit()` + re-init vs the Seed's own
   `ConfigureAudio()`: does SAI2 receive stay reliable (the reference project
   found it did not, and patched `ConfigureAudio()` out).
8. **OLED.** Works with DC on the MISO pad and the SPI TX-only config.
9. **MIDI out.** Loop current into a real receiver; the on-board transmitter is
   4.48 mA worst case against a TLP2362 that switches at 5 mA max (interface
   §2) - that limit is the board's, not the firmware's.
10. **Audio levels.** 0.100 in / 3.300 out measured at the jacks; no DC.

## 8. Milestones

- **M0 (this tree)** - dependencies pinned, contracts encoded, six-channel audio
  and all peripherals initialised, pin map checked against the board, builds.
- **M1** - bench items in §7, results written here as measured values; the
  unqualified constants replaced by qualified ones.
- **M2** - calibration: per-channel CV in/out corrections persisted, 1 V/oct
  sweep documented.
- **M3** - engine platform: a real `engine::Engine` behind the existing seam,
  six assignable pots, OLED UI with NAV/SELECT + MODE, MIDI note/CC/clock/
  program, CV out as pitch/gate.
- **M4** - first engine and release: versioned image + DFU instructions.

The panel deliberately names no fixed function for P1-P6
([`FRONT-PANEL.md`](FRONT-PANEL.md)); the first engine is an owner decision, and
the platform in M3 is what keeps that open.

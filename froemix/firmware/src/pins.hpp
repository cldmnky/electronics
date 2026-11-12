#pragma once
// Froemix firmware pin map - the ONE place a Seed pin is bound to a board net.
//
// Derived from the board source, not assumed:
//   froemix/tscircuit/sections/main.tsx            (DaisySeed1 / H1 / CN1 / CN2 / U1)
//   froemix/tscircuit/lib/pins.ts                  (Seed pad -> signal name)
//   froemix/tmp/library/DaisyKiCad-main/Daisy-Boards.kicad_sym  (pad -> Dn)
//
// `python3 ../tools/check_firmware_pins.py` compares this file against those
// three sources and fails on drift, so a board respin cannot silently leave
// the firmware driving the wrong net.
//
// bench: Rev 0.1 is at the fab. Every entry here is the schematic's, not a
// probe result (doc/FIRMWARE.md, "verification status").
//
// Format, parsed by the checker - keep it exactly:
//     X(<pad>, <BOARD_NET>, <Dn>, <role>)

#include "daisy_seed.h"

// clang-format off
#define FROEMIX_SEED_PINS(X)                                                     \
    X( 1, MULTIPLEX_S0,    D0,  mux_s0)        /* CD4051 A, shared with USB_ID */\
    X( 2, SD_DAT3,         D1,  sd_dat3)                                         \
    X( 3, SD_DAT2,         D2,  sd_dat2)                                         \
    X( 4, SD_DAT1,         D3,  sd_dat1)                                         \
    X( 5, SD_DAT0,         D4,  sd_dat0)                                         \
    X( 6, SD_CMD,          D5,  sd_cmd)                                          \
    X( 7, SD_CLK,          D6,  sd_clk)                                          \
    X( 8, OLED_CS,         D7,  oled_cs)                                         \
    X( 9, OLED_SCL,        D8,  oled_sclk)                                       \
    X(10, OLED_DC,         D9,  oled_dc)       /* GPIO on the SPI MISO pad */     \
    X(11, OLED_SDA,        D10, oled_mosi)                                       \
    X(12, SCL,             D11, i2c_scl)                                         \
    X(13, SDA,             D12, i2c_sda)                                         \
    X(14, USART1_TX,       D13, midi_tx)                                         \
    X(15, USART1_RX,       D14, midi_rx)                                         \
    X(16, AUDIO_IN_L,      NA,   audio_in_l)   /* Seed codec, SAI1 */             \
    X(17, AUDIO_IN_R,      NA,   audio_in_r)                                     \
    X(18, AUDIO_OUT_L,     NA,   audio_out_l)                                   \
    X(19, AUDIO_OUT_R,     NA,   audio_out_r)                                   \
    X(20, GND,             NA,   agnd)                                           \
    X(21, P3V3_A,          NA,   v3v3_a)                                        \
    X(22, OLED_RES,        D15, oled_reset)                                      \
    X(23, LED,             D16, panel_led)                                       \
    X(24, MULTIPLEX_ADC_1, D17, mux_common)    /* CD4051 X, ADC */                \
    X(25, CV_IN_1,         D18, cv_in_1)                                         \
    X(26, CV_IN_2,         D19, cv_in_2)                                         \
    X(27, SYNC_OUT,        D20, sync_out)      /* 0/3.3 V -> TL074 -> 0/5 V */    \
    X(28, SYNC_IN,         D21, sync_in)       /* inverted: Q1 pull-up */         \
    X(29, CV_OUT_1,        D22, cv_out_1)      /* DAC channel TWO */              \
    X(30, CV_OUT_2,        D23, cv_out_2)      /* DAC channel ONE */              \
    X(31, MCLK,            D24, pmod_mclk)                                       \
    X(32, SDI,             D25, pmod_sdin)                                       \
    X(33, SDO,             D26, pmod_sdout)                                      \
    X(34, LRCK,            D27, pmod_lrck)                                       \
    X(35, BICK,            D28, pmod_bick)                                       \
    X(36, MULTIPLEX_S1,    D29, mux_s1)        /* CD4051 B, shared with USB D- */ \
    X(37, MULTIPLEX_S2,    D30, mux_s2)        /* CD4051 C, shared with USB D+ */ \
    X(38, P3V3_D,          NA,   v3v3_d)                                        \
    X(39, P5V,             NA,   vin)                                             \
    X(40, GND,             NA,   dgnd)
// clang-format on

namespace froemix
{
namespace pins
{
// Digital / analog pins.
constexpr daisy::Pin kMuxS0  = daisy::seed::D0;
constexpr daisy::Pin kMuxS1  = daisy::seed::D29;
constexpr daisy::Pin kMuxS2  = daisy::seed::D30;
constexpr daisy::Pin kOledCs = daisy::seed::D7;
constexpr daisy::Pin kOledSclk = daisy::seed::D8;
constexpr daisy::Pin kOledDc   = daisy::seed::D9; // NOT an SPI MISO
constexpr daisy::Pin kOledMosi = daisy::seed::D10;
constexpr daisy::Pin kI2cScl   = daisy::seed::D11;
constexpr daisy::Pin kI2cSda   = daisy::seed::D12;
constexpr daisy::Pin kMidiTx   = daisy::seed::D13;
constexpr daisy::Pin kMidiRx   = daisy::seed::D14;
constexpr daisy::Pin kOledReset  = daisy::seed::D15;
constexpr daisy::Pin kPanelLed   = daisy::seed::D16;
constexpr daisy::Pin kMuxCommon  = daisy::seed::D17; // CD4051 X output
constexpr daisy::Pin kCvIn1      = daisy::seed::D18;
constexpr daisy::Pin kCvIn2      = daisy::seed::D19;
constexpr daisy::Pin kSyncOut    = daisy::seed::D20;
constexpr daisy::Pin kSyncIn     = daisy::seed::D21;

// Eurorack-pmod (H1): SAI2 clock/data. MCLK/LRCK/BICK are driven by the Seed.
constexpr daisy::Pin kPmodMclk = daisy::seed::D24;
constexpr daisy::Pin kPmodSdin = daisy::seed::D25; // Seed TX -> AK4619 SDIN1
constexpr daisy::Pin kPmodSdout = daisy::seed::D26; // AK4619 SDOUT1 -> Seed RX
constexpr daisy::Pin kPmodLrck = daisy::seed::D27;
constexpr daisy::Pin kPmodBick = daisy::seed::D28;

// The Seed codec's own SAI1 pins are fixed on the module (GPIOE 2..6); they are
// not on the 40-pin header and are configured in audio/audio6.cpp.
} // namespace pins
} // namespace froemix

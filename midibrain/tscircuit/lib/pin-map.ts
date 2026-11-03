// Raspberry Pi Pico Rev 3 / RP2040. Physical pin numbers, not RP2040 pin numbers.
// Pi GPIO numbers are BCM. Pin reservations are tentative pending power/reset
// and level/backfeed analysis; no physical wiring is qualified by this table.
export const PICO_GPIO = [
  { gpio: 0, pin: 1, signal: "MIDI_OUT_1_UART0_TX" },
  { gpio: 1, pin: 2, signal: "MIDI_IN_1_UART0_RX" },
  { gpio: 2, pin: 4, signal: "ENCODER_SW" },
  { gpio: 3, pin: 5, signal: "PLAY_START" },
  { gpio: 4, pin: 6, signal: "MIDI_OUT_2_UART1_TX" },
  { gpio: 5, pin: 7, signal: "MIDI_IN_2_UART1_RX" },
  { gpio: 6, pin: 9, signal: "NAV_UP" },
  { gpio: 7, pin: 10, signal: "NAV_DOWN" },
  { gpio: 8, pin: 11, signal: "NAV_LEFT" },
  { gpio: 9, pin: 12, signal: "NAV_RIGHT" },
  { gpio: 10, pin: 14, signal: "NAV_PRESS" },
  { gpio: 11, pin: 15, signal: "ENCODER_A" },
  { gpio: 12, pin: 16, signal: "ENCODER_B" },
  { gpio: 13, pin: 17, signal: "REC" },
  { gpio: 14, pin: 19, signal: "OVER_DUB" },
  { gpio: 15, pin: 20, signal: "STOP" },
  { gpio: 16, pin: 21, signal: "SPI0_RX_MOSI" },
  { gpio: 17, pin: 22, signal: "SPI0_CSN" },
  { gpio: 18, pin: 24, signal: "SPI0_SCK" },
  { gpio: 19, pin: 25, signal: "SPI0_TX_MISO" },
  { gpio: 20, pin: 26, signal: "READY_IRQ" },
  { gpio: 21, pin: 27, signal: "PLAY" },
  { gpio: 22, pin: 29, signal: "PLAY_LED" },
  { gpio: 26, pin: 31, signal: "REC_LED" },
  { gpio: 27, pin: 32, signal: "OVERDUB_LED" },
  { gpio: 28, pin: 34, signal: "SHIFT_EXIT" },
] as const

export const PI_SPI = [
  { bcm: 10, pin: 19, signal: "SPI0_MOSI" },
  { bcm: 9, pin: 21, signal: "SPI0_MISO" },
  { bcm: 11, pin: 23, signal: "SPI0_SCLK" },
  { bcm: 8, pin: 24, signal: "SPI0_CE0" },
  { bcm: 24, pin: 18, signal: "READY_IRQ" },
  { bcm: 25, pin: 22, signal: "RESET_REQUEST_OPEN_DRAIN_PENDING" },
] as const

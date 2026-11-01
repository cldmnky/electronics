// Froemix base board - hand-maintained tscircuit source (see froemix/AGENTS.md).
// Ported once from the KiCad project in ../tmp by froemix/tools/kicad2tscircuit.py;
// that tool is retired as an editor, edit this file directly.

// Pin labels (KiCad pin number -> label) for the parts
// that are modelled as <chip> elements.

export const TL074_PINS = {
  pin1: "OUT1",
  pin2: "IN1N",
  pin3: "IN1P",
  pin4: "VPLUS",
  pin5: "IN2P",
  pin6: "IN2N",
  pin7: "OUT2",
  pin8: "OUT3",
  pin9: "IN3N",
  pin10: "IN3P",
  pin11: "VMINUS",
  pin12: "IN4P",
  pin13: "IN4N",
  pin14: "OUT4",
} as const

export const CD4051_PINS = {
  pin1: "X4",
  pin2: "X6",
  pin3: "X",
  pin4: "X7",
  pin5: "X5",
  pin6: "INH",
  pin7: "VEE",
  pin8: "VSS",
  pin9: "C",
  pin10: "B",
  pin11: "A",
  pin12: "X3",
  pin13: "X0",
  pin14: "X1",
  pin15: "X2",
  pin16: "VDD",
} as const

export const MCP6002_PINS = {
  pin1: "OUTA",
  pin2: "INA_N",
  pin3: "INA_P",
  pin4: "VSS",
  pin5: "INB_P",
  pin6: "INB_N",
  pin7: "OUTB",
  pin8: "VDD",
} as const

export const LM4040_PINS = {
  pin1: "CATHODE",
  pin2: "ANODE",
  pin3: "NC",
} as const

export const TLP2362_PINS = {
  pin1: "ANODE",
  pin3: "CATHODE",
  pin4: "GND",
  pin5: "VO",
  pin6: "VCC",
} as const

export const B1205S_PINS = {
  pin1: "VIN_N",
  pin2: "VIN_P",
  pin3: "VOUT_N",
  pin4: "VOUT_P",
} as const

export const MMBT3904_PINS = {
  pin1: "B",
  pin2: "E",
  pin3: "C",
} as const

export const DAISY_SEED_PINS = {
  pin1: "USB_ID",
  pin2: "SD_DAT3",
  pin3: "SD_DAT2",
  pin4: "SD_DAT1",
  pin5: "SD_DAT0",
  pin6: "SD_CMD",
  pin7: "SD_CLK",
  pin8: "SPI1_CS",
  pin9: "SPI1_SCK",
  pin10: "SPI1_POCI",
  pin11: "SPI1_PICO",
  pin12: "I2C1_SCL",
  pin13: "I2C1_SDA",
  pin14: "USART1_TX",
  pin15: "USART1_RX",
  pin16: "AUDIO_IN_1",
  pin17: "AUDIO_IN_2",
  pin18: "AUDIO_OUT_1",
  pin19: "AUDIO_OUT_2",
  pin20: "AGND",
  pin21: "V3V3_A",
  pin22: "ADC_0",
  pin23: "ADC_1",
  pin24: "ADC_2",
  pin25: "ADC_3",
  pin26: "ADC_4",
  pin27: "ADC_5",
  pin28: "ADC_6",
  pin29: "DAC_OUT2",
  pin30: "DAC_OUT1",
  pin31: "SAI2_MCLK",
  pin32: "SAI2_SD_B",
  pin33: "SAI2_SD_A",
  pin34: "SAI2_FS",
  pin35: "SAI2_SCK",
  pin36: "USB_D_N",
  pin37: "USB_D_P",
  pin38: "V3V3_D",
  pin39: "VIN",
  pin40: "DGND",
} as const

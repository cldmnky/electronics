// Froemix base board - hand-maintained tscircuit source (see froemix/AGENTS.md).
// Ported once from the KiCad project in ../tmp by froemix/tools/kicad2tscircuit.py;
// that tool is retired as an editor, edit this file directly.

// Root sheet: root sheet: Daisy Seed, ADC multiplexer and board connectors
// Source: tmp/froemix.kicad_sch

import { CD4051_PINS, DAISY_SEED_PINS } from "../lib/pins"
import { FP_Module_Electrosmith_Daisy_Seed_R90, FP_jclpcb_C0603, FP_jclpcb_C1206, FP_jclpcb_HDR_TH_12P_P2_54_H_M_R2_C6_S2_54_W12_5_R90, FP_jclpcb_HDR_TH_16P_P2_00_V_M, FP_jclpcb_HDR_TH_7P_P2_00_V_M, FP_jclpcb_HDR_TH_8P_P2_00_V_M, FP_jclpcb_R0603, FP_jclpcb_SOP_16_L9_9_W3_9_P1_27_LS6_1_BL } from "../lib/footprints"

export const MainSection = () => (
  <schematicsheet name="main" displayName="Root" sheetIndex={0}>
    {/* C1 10uF - CL31A106KBHNNNE - LCSC C13585 - CL31A106KBHNNNE - jclpcb:C1206 */}
    <capacitor
      name="C1"
      capacitance="10uF"
      footprint={FP_jclpcb_C1206}
      pcbX="-28mm"
      pcbY="25.5mm"
      layer="bottom"
      supplierPartNumbers={{ jlcpcb: ["C13585"] }}
      manufacturerPartNumber="CL31A106KBHNNNE"
      cadModel={{
      objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C13585.obj?uuid=c6790e9475e1483991d2c64340b24e96",
      stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C13585.step?uuid=c6790e9475e1483991d2c64340b24e96",
      pcbRotationOffset: 0,
      modelOriginPosition: { x: 0, y: 0, z: -0.65 },
    }}
      connections={{
        "pin1": "net.GND",
        "pin2": "net.P5V",
      }} />
    {/* C29 100nF - EXTRA_C29 - LCSC C14663 - CC0603KRX7R9BB104 - P3V3_D decoupling at the Daisy/SD/OLED headers (added in tscircuit, mirror into KiCad) - jclpcb:C0603 */}
    <capacitor
      name="C29"
      capacitance="100nF"
      footprint={FP_jclpcb_C0603}
      pcbX="-22.5mm"
      pcbY="29.6mm"
      layer="bottom"
      supplierPartNumbers={{ jlcpcb: ["C14663"] }}
      manufacturerPartNumber="CC0603KRX7R9BB104"
      cadModel={{
      objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C14663.obj?uuid=ac9b32e974bc448eab36b1293f859dcb",
      stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C14663.step?uuid=ac9b32e974bc448eab36b1293f859dcb",
      pcbRotationOffset: 0,
      modelOriginPosition: { x: 0, y: 0, z: -0.4 },
    }}
      connections={{
        "pin1": "net.P3V3_D",
        "pin2": "net.GND",
      }} />
    {/* C30 10uF - EXTRA_C30 - LCSC C13585 - CL31A106KBHNNNE - P3V3_D bulk for SD/OLED spikes (added in tscircuit, mirror into KiCad) - jclpcb:C1206 */}
    <capacitor
      name="C30"
      capacitance="10uF"
      footprint={FP_jclpcb_C1206}
      pcbX="-4mm"
      pcbY="29.2mm"
      layer="bottom"
      supplierPartNumbers={{ jlcpcb: ["C13585"] }}
      manufacturerPartNumber="CL31A106KBHNNNE"
      cadModel={{
      objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C13585.obj?uuid=c6790e9475e1483991d2c64340b24e96",
      stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C13585.step?uuid=c6790e9475e1483991d2c64340b24e96",
      pcbRotationOffset: 0,
      modelOriginPosition: { x: 0, y: 0, z: -0.65 },
    }}
      connections={{
        "pin1": "net.P3V3_D",
        "pin2": "net.GND",
      }} />
    {/* CN1 A2005WV-16P - LCSC C225268 - jclpcb:HDR-TH_16P-P2.00-V-M */}
    <connector
      name="CN1"
      footprint={FP_jclpcb_HDR_TH_16P_P2_00_V_M}
      pinCount={16}
      schPinArrangement={{ leftSide: [1, 3, 5, 7, 9, 11, 13, 15], rightSide: [2, 4, 6, 8, 10, 12, 14, 16] }}
      pcbX="-17.58mm"
      pcbY="59.64mm"
      supplierPartNumbers={{ jlcpcb: ["C225268"] }}
      cadModel={{
      objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C225268.obj?uuid=a7f72e52c0db453aa9c56a72ae587e86",
      stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C225268.step?uuid=a7f72e52c0db453aa9c56a72ae587e86",
      pcbRotationOffset: 0,
      modelOriginPosition: { x: -0.000025400000140507473, y: 0, z: 0.19999400000000023 },
    }}
      connections={{
        "pin1": "net.J_MIDI_IN_TIP",
        "pin2": "net.J_MIDI_IN_RING",
        "pin3": "net.J_MIDI_OUT_TIP",
        "pin4": "net.J_MIDI_OUT_RING",
        "pin5": "net.M_ADC_0",
        "pin6": "net.M_ADC_1",
        "pin7": "net.M_ADC_2",
        "pin8": "net.M_ADC_3",
        "pin9": "net.M_ADC_4",
        "pin10": "net.M_ADC_5",
        "pin11": "net.M_ADC_6",
        "pin12": "net.M_ADC_7",
        "pin13": "net.LED",
        "pin14": "net.P3V3_D",
        "pin15": "net.P3V3_A",
        "pin16": "net.GND",
      }} />
    {/* CN2 A2005WV-16P - LCSC C225268 - jclpcb:HDR-TH_16P-P2.00-V-M */}
    <connector
      name="CN2"
      footprint={FP_jclpcb_HDR_TH_16P_P2_00_V_M}
      pinCount={16}
      schPinArrangement={{ leftSide: [1, 3, 5, 7, 9, 11, 13, 15], rightSide: [2, 4, 6, 8, 10, 12, 14, 16] }}
      pcbX="12.42mm"
      pcbY="-36.36mm"
      supplierPartNumbers={{ jlcpcb: ["C225268"] }}
      cadModel={{
      objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C225268.obj?uuid=a7f72e52c0db453aa9c56a72ae587e86",
      stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C225268.step?uuid=a7f72e52c0db453aa9c56a72ae587e86",
      pcbRotationOffset: 0,
      modelOriginPosition: { x: -0.000025400000140507473, y: 0, z: 0.19999400000000023 },
    }}
      connections={{
        "pin1": "net.J_AUDIO_IN_L",
        "pin2": "net.J_AUDIO_IN_R",
        "pin3": "net.J_AUDIO_OUT_L",
        "pin4": "net.J_AUDIO_OUT_R",
        "pin5": "net.J_SYNC_IN",
        "pin6": "net.J_SYNC_OUT",
        "pin7": "net.J_CV_IN_1",
        "pin8": "net.J_CV_IN_2",
        "pin9": "net.J_CV_OUT_1",
        "pin10": "net.J_CV_OUT_2",
        "pin16": "net.GND",
      }} />
    {/* CN3 Micro SD - A2005WV-8P - LCSC C22438291 - HX PZ2.0-1x8P ZZ - jclpcb:HDR-TH_8P-P2.00-V-M */}
    <connector
      name="CN3"
      footprint={FP_jclpcb_HDR_TH_8P_P2_00_V_M}
      pinCount={8}
      schPinArrangement={{ leftSide: [1, 3, 5, 7], rightSide: [2, 4, 6, 8] }}
      pcbX="8.14mm"
      pcbY="59.63mm"
      supplierPartNumbers={{ jlcpcb: ["C22438291"] }}
      manufacturerPartNumber="HX PZ2.0-1x8P ZZ"
      cadModel={{
      objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C22438291.obj?uuid=429211f62e55477fa082021302e1f5d0",
      stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C22438291.step?uuid=429211f62e55477fa082021302e1f5d0",
      pcbRotationOffset: 0,
      modelOriginPosition: { x: 0, y: 0, z: 0.19999400000000023 },
    }}
      connections={{
        "pin1": "net.P3V3_D",
        "pin2": "net.SD_DAT3",
        "pin3": "net.SD_DAT2",
        "pin4": "net.SD_DAT1",
        "pin5": "net.SD_DAT0",
        "pin6": "net.GND",
        "pin7": "net.SD_CMD",
        "pin8": "net.SD_CLK",
      }} />
    {/* CN4 OLED - A2005WV-7P - LCSC C22438290 - HX PZ2.0-1x7P ZZ - jclpcb:HDR-TH_7P-P2.00-V-M */}
    <connector
      name="CN4"
      footprint={FP_jclpcb_HDR_TH_7P_P2_00_V_M}
      pinCount={7}
      schPinArrangement={{ leftSide: [1, 3, 5, 7], rightSide: [2, 4, 6] }}
      pcbX="24.5mm"
      pcbY="59.63mm"
      supplierPartNumbers={{ jlcpcb: ["C22438290"] }}
      manufacturerPartNumber="HX PZ2.0-1x7P ZZ"
      cadModel={{
      objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C22438290.obj?uuid=f62ae0b40c5a45078f02d1bb9a0fea45",
      stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C22438290.step?uuid=f62ae0b40c5a45078f02d1bb9a0fea45",
      pcbRotationOffset: 0,
      modelOriginPosition: { x: 0, y: 0, z: 0.19999400000000023 },
    }}
      connections={{
        "pin1": "net.GND",
        "pin2": "net.P3V3_D",
        "pin3": "net.OLED_CS",
        "pin4": "net.OLED_SCL",
        "pin5": "net.OLED_DC",
        "pin6": "net.OLED_SDA",
        "pin7": "net.OLED_RES",
      }} />
    {/* DaisySeed1 Electrosmith_Daisy_Seed_Rev4 - fitted by hand (DNP for JLC assembly) - Module:Electrosmith_Daisy_Seed */}
    <chip
      name="DaisySeed1"
      footprint={FP_Module_Electrosmith_Daisy_Seed_R90}
      pinLabels={DAISY_SEED_PINS}
      pinAttributes={{
        "AUDIO_IN_1": { requiresPower: true },
        "AUDIO_IN_2": { requiresPower: true },
        "AUDIO_OUT_1": { providesPower: true },
        "AUDIO_OUT_2": { providesPower: true },
        "AGND": { requiresPower: true, requiresGround: true },
        "V3V3_A": { providesPower: true },
        "V3V3_D": { providesPower: true },
        "VIN": { requiresPower: true },
        "DGND": { requiresPower: true, requiresGround: true },
      }}
      schPinArrangement={{ leftSide: ["USB_ID", "SD_DAT2", "SD_DAT0", "SD_CLK", "SPI1_SCK", "SPI1_PICO", "I2C1_SDA", "USART1_RX", "AUDIO_IN_1", "AUDIO_IN_2", "AGND", "ADC_1", "ADC_3", "ADC_5", "DAC_OUT2", "SAI2_MCLK", "SAI2_SD_A", "SAI2_SCK", "USB_D_P", "VIN", "DGND"], rightSide: ["SD_DAT3", "SD_DAT1", "SD_CMD", "SPI1_CS", "SPI1_POCI", "I2C1_SCL", "USART1_TX", "AUDIO_OUT_1", "AUDIO_OUT_2", "V3V3_A", "ADC_0", "ADC_2", "ADC_4", "ADC_6", "DAC_OUT1", "SAI2_SD_B", "SAI2_FS", "USB_D_N", "V3V3_D"] }}
      pcbX="-23.9mm"
      pcbY="48.15mm"
      layer="bottom"
      doNotPlace={true}
      connections={{
        "USB_ID": "net.MULTIPLEX_S0",
        "SD_DAT3": "net.SD_DAT3",
        "SD_DAT2": "net.SD_DAT2",
        "SD_DAT1": "net.SD_DAT1",
        "SD_DAT0": "net.SD_DAT0",
        "SD_CMD": "net.SD_CMD",
        "SD_CLK": "net.SD_CLK",
        "SPI1_CS": "net.OLED_CS",
        "SPI1_SCK": "net.OLED_SCL",
        "SPI1_POCI": "net.OLED_DC",
        "SPI1_PICO": "net.OLED_SDA",
        "I2C1_SCL": "net.SCL",
        "I2C1_SDA": "net.SDA",
        "USART1_TX": "net.USART1_TX",
        "USART1_RX": "net.USART1_RX",
        "AUDIO_IN_1": "net.AUDIO_IN_L",
        "AUDIO_IN_2": "net.AUDIO_IN_R",
        "AUDIO_OUT_1": "net.AUDIO_OUT_L",
        "AUDIO_OUT_2": "net.AUDIO_OUT_R",
        "AGND": "net.GND",
        "V3V3_A": "net.P3V3_A",
        "ADC_0": "net.OLED_RES",
        "ADC_1": "net.LED",
        "ADC_2": "net.MULTIPLEX_ADC_1",
        "ADC_3": "net.CV_IN_1",
        "ADC_4": "net.CV_IN_2",
        "ADC_5": "net.SYNC_OUT",
        "ADC_6": "net.SYNC_IN",
        "DAC_OUT2": "net.CV_OUT_1",
        "DAC_OUT1": "net.CV_OUT_2",
        "SAI2_MCLK": "net.MCLK",
        "SAI2_SD_B": "net.SDI",
        "SAI2_SD_A": "net.SDO",
        "SAI2_FS": "net.LRCK",
        "SAI2_SCK": "net.BICK",
        "USB_D_N": "net.MULTIPLEX_S1",
        "USB_D_P": "net.MULTIPLEX_S2",
        "V3V3_D": "net.P3V3_D",
        "VIN": "net.P5V",
        "DGND": "net.GND",
      }} />
    {/* H1 PMOD - 2*6Pheader - LCSC C22373932 - HC-PM254-8.5H-2x6PW - jclpcb:HDR-TH_12P-P2.54-H-M-R2-C6-S2.54-W12.5 */}
    <connector
      name="H1"
      footprint={FP_jclpcb_HDR_TH_12P_P2_54_H_M_R2_C6_S2_54_W12_5_R90}
      pinCount={12}
      schPinArrangement={{ leftSide: [1, 3, 5, 7, 9, 11], rightSide: [2, 4, 6, 8, 10, 12] }}
      pcbX="28.5mm"
      pcbY="20mm"
      layer="bottom"
      supplierPartNumbers={{ jlcpcb: ["C22373932"] }}
      manufacturerPartNumber="HC-PM254-8.5H-2x6PW"
      connections={{
        "pin1": "net.SDI",
        "pin2": "net.SCL",
        "pin3": "net.SDO",
        "pin4": "net.SDA",
        "pin5": "net.LRCK",
        "pin6": "net.P3V3_D",
        "pin7": "net.BICK",
        "pin8": "net.MCLK",
        "pin9": "net.GND",
        "pin10": "net.GND",
        "pin11": "net.P3V3_D",
        "pin12": "net.P3V3_D",
      }} />
    {/* R51 4K7 - EXTRA_R51 - LCSC C23162 - 0603WAF4701T5E - I2C SCL pull-up, cf. Patch R77 (added in tscircuit, mirror into KiCad) - jclpcb:R0603 */}
    <resistor
      name="R51"
      resistance="4K7"
      footprint={FP_jclpcb_R0603}
      pcbX="6.5mm"
      pcbY="51.8mm"
      layer="bottom"
      supplierPartNumbers={{ jlcpcb: ["C23162"] }}
      manufacturerPartNumber="0603WAF4701T5E"
      cadModel={{
      objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C23162.obj?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C23162.step?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      pcbRotationOffset: 0,
      modelOriginPosition: { x: -0.004999999999999977, y: 0, z: -0.01 },
    }}
      connections={{
        "pin1": "net.SCL",
        "pin2": "net.P3V3_D",
      }} />
    {/* R52 4K7 - EXTRA_R52 - LCSC C23162 - 0603WAF4701T5E - I2C SDA pull-up, cf. Patch R89/R91 (added in tscircuit, mirror into KiCad) - jclpcb:R0603 */}
    <resistor
      name="R52"
      resistance="4K7"
      footprint={FP_jclpcb_R0603}
      pcbX="10mm"
      pcbY="51.8mm"
      layer="bottom"
      supplierPartNumbers={{ jlcpcb: ["C23162"] }}
      manufacturerPartNumber="0603WAF4701T5E"
      cadModel={{
      objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C23162.obj?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C23162.step?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      pcbRotationOffset: 0,
      modelOriginPosition: { x: -0.004999999999999977, y: 0, z: -0.01 },
    }}
      connections={{
        "pin1": "net.SDA",
        "pin2": "net.P3V3_D",
      }} />
    {/* R53 47K - EXTRA_R53 - LCSC C25819 - 0603WAF4702T5E - SD DAT0 pull-up, cf. Patch R6 (added in tscircuit, mirror into KiCad) - jclpcb:R0603 */}
    <resistor
      name="R53"
      resistance="47k"
      footprint={FP_jclpcb_R0603}
      pcbX="-7.5mm"
      pcbY="51.8mm"
      layer="bottom"
      supplierPartNumbers={{ jlcpcb: ["C25819"] }}
      manufacturerPartNumber="0603WAF4702T5E"
      cadModel={{
      objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C25819.obj?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C25819.step?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      pcbRotationOffset: 0,
      modelOriginPosition: { x: -0.004999999999999977, y: 0, z: -0.01 },
    }}
      connections={{
        "pin1": "net.SD_DAT0",
        "pin2": "net.P3V3_D",
      }} />
    {/* R54 47K - EXTRA_R54 - LCSC C25819 - 0603WAF4702T5E - SD DAT1 pull-up, cf. Patch R7 (added in tscircuit, mirror into KiCad) - jclpcb:R0603 */}
    <resistor
      name="R54"
      resistance="47k"
      footprint={FP_jclpcb_R0603}
      pcbX="-11mm"
      pcbY="51.8mm"
      layer="bottom"
      supplierPartNumbers={{ jlcpcb: ["C25819"] }}
      manufacturerPartNumber="0603WAF4702T5E"
      cadModel={{
      objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C25819.obj?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C25819.step?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      pcbRotationOffset: 0,
      modelOriginPosition: { x: -0.004999999999999977, y: 0, z: -0.01 },
    }}
      connections={{
        "pin1": "net.SD_DAT1",
        "pin2": "net.P3V3_D",
      }} />
    {/* R55 47K - EXTRA_R55 - LCSC C25819 - 0603WAF4702T5E - SD DAT2 pull-up, cf. Patch R3 (added in tscircuit, mirror into KiCad) - jclpcb:R0603 */}
    <resistor
      name="R55"
      resistance="47k"
      footprint={FP_jclpcb_R0603}
      pcbX="-14.5mm"
      pcbY="51.8mm"
      layer="bottom"
      supplierPartNumbers={{ jlcpcb: ["C25819"] }}
      manufacturerPartNumber="0603WAF4702T5E"
      cadModel={{
      objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C25819.obj?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C25819.step?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      pcbRotationOffset: 0,
      modelOriginPosition: { x: -0.004999999999999977, y: 0, z: -0.01 },
    }}
      connections={{
        "pin1": "net.SD_DAT2",
        "pin2": "net.P3V3_D",
      }} />
    {/* R56 47K - EXTRA_R56 - LCSC C25819 - 0603WAF4702T5E - SD DAT3 pull-up, cf. Patch R4 (added in tscircuit, mirror into KiCad) - jclpcb:R0603 */}
    <resistor
      name="R56"
      resistance="47k"
      footprint={FP_jclpcb_R0603}
      pcbX="-18mm"
      pcbY="51.8mm"
      layer="bottom"
      supplierPartNumbers={{ jlcpcb: ["C25819"] }}
      manufacturerPartNumber="0603WAF4702T5E"
      cadModel={{
      objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C25819.obj?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C25819.step?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      pcbRotationOffset: 0,
      modelOriginPosition: { x: -0.004999999999999977, y: 0, z: -0.01 },
    }}
      connections={{
        "pin1": "net.SD_DAT3",
        "pin2": "net.P3V3_D",
      }} />
    {/* R57 47K - EXTRA_R57 - LCSC C25819 - 0603WAF4702T5E - SD CMD pull-up, cf. Patch R5 (added in tscircuit, mirror into KiCad) - jclpcb:R0603 */}
    <resistor
      name="R57"
      resistance="47k"
      footprint={FP_jclpcb_R0603}
      pcbX="-4mm"
      pcbY="51.8mm"
      layer="bottom"
      supplierPartNumbers={{ jlcpcb: ["C25819"] }}
      manufacturerPartNumber="0603WAF4702T5E"
      cadModel={{
      objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C25819.obj?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C25819.step?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      pcbRotationOffset: 0,
      modelOriginPosition: { x: -0.004999999999999977, y: 0, z: -0.01 },
    }}
      connections={{
        "pin1": "net.SD_CMD",
        "pin2": "net.P3V3_D",
      }} />
    {/* U1 CD4051BM/TR - 4051 - LCSC C507163 - jclpcb:SOP-16_L9.9-W3.9-P1.27-LS6.1-BL */}
    <chip
      name="U1"
      footprint={FP_jclpcb_SOP_16_L9_9_W3_9_P1_27_LS6_1_BL}
      pinLabels={CD4051_PINS}
      pinAttributes={{
        "INH": { requiresPower: true, requiresGround: true },
        "VEE": { requiresPower: true, requiresGround: true },
        "VSS": { requiresPower: true, requiresGround: true },
        "C": { requiresPower: true },
        "B": { requiresPower: true },
        "A": { requiresPower: true },
        "VDD": { requiresPower: true },
      }}
      schPinArrangement={{ leftSide: ["X4", "X", "X5", "INH", "VEE", "VSS", "C", "B", "A", "VDD"], rightSide: ["X6", "X7", "X3", "X0", "X1", "X2"] }}
      pcbX="-14.65mm"
      pcbY="25.75mm"
      layer="bottom"
      supplierPartNumbers={{ jlcpcb: ["C507163"] }}
      cadModel={{
      objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C507163.obj?uuid=35eb3630a43941e1a375b6d459fabe79",
      stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C507163.step?uuid=35eb3630a43941e1a375b6d459fabe79",
      pcbRotationOffset: 180,
      modelOriginPosition: { x: -0.000012700000013410317, y: -0.000012700000013410317, z: 0 },
    }}
      connections={{
        "X4": "net.M_ADC_4",
        "X6": "net.M_ADC_6",
        "X": "net.MULTIPLEX_ADC_1",
        "X7": "net.M_ADC_7",
        "X5": "net.M_ADC_5",
        "INH": "net.GND",
        "VEE": "net.GND",
        "VSS": "net.GND",
        "C": "net.MULTIPLEX_S2",
        "B": "net.MULTIPLEX_S1",
        "A": "net.MULTIPLEX_S0",
        "X3": "net.M_ADC_3",
        "X0": "net.M_ADC_0",
        "X1": "net.M_ADC_1",
        "X2": "net.M_ADC_2",
        "VDD": "net.P3V3_A",
      }} />
  </schematicsheet>
)

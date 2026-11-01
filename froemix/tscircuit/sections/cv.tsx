// Froemix base board - hand-maintained tscircuit source (see froemix/AGENTS.md).
// Ported once from the KiCad project in ../tmp by froemix/tools/kicad2tscircuit.py;
// that tool is retired as an editor, edit this file directly.

// CV sheet: sync in/out, CV in attenuverters, CV out drivers
// Source: tmp/cv.kicad_sch

import { LM4040_PINS, MCP6002_PINS, MMBT3904_PINS, TL074_PINS } from "../lib/pins"
import { FP_jclpcb_C0603, FP_jclpcb_R0603, FP_jclpcb_R1206, FP_jclpcb_SOIC_8_L4_9_W3_9_P1_27_LS6_0_BL, FP_jclpcb_SOT_23_3_L2_9_W1_6_P1_90_LS2_8_BR, FP_jclpcb_SOT_23_3_L3_0_W1_7_P0_95_LS2_9_BR, FP_jclpcb_TSSOP_14_L5_0_W4_4_P0_65_LS6_4_BL } from "../lib/footprints"

export const CvSection = () => (
  <schematicsheet name="cv" displayName="CV" sheetIndex={3}>
    {/* C14 100nF - CC0603KRX7R9BB104 - LCSC C14663 - jclpcb:C0603 */}
    <capacitor
      name="C14"
      capacitance="100nF"
      footprint={FP_jclpcb_C0603}
      pcbX="20.6mm"
      pcbY="8.6mm"
      layer="bottom"
      supplierPartNumbers={{ jlcpcb: ["C14663"] }}
      cadModel={{
      objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C14663.obj?uuid=ac9b32e974bc448eab36b1293f859dcb",
      stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C14663.step?uuid=ac9b32e974bc448eab36b1293f859dcb",
      pcbRotationOffset: 0,
      modelOriginPosition: { x: 0, y: 0, z: -0.4 },
    }}
      connections={{
        "pin1": "net.VCC",
        "pin2": "net.GND",
      }} />
    {/* C15 100nF - CC0603KRX7R9BB104 - LCSC C14663 - jclpcb:C0603 */}
    <capacitor
      name="C15"
      capacitance="100nF"
      footprint={FP_jclpcb_C0603}
      pcbX="15.81mm"
      pcbY="17.4mm"
      layer="bottom"
      supplierPartNumbers={{ jlcpcb: ["C14663"] }}
      cadModel={{
      objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C14663.obj?uuid=ac9b32e974bc448eab36b1293f859dcb",
      stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C14663.step?uuid=ac9b32e974bc448eab36b1293f859dcb",
      pcbRotationOffset: 0,
      modelOriginPosition: { x: 0, y: 0, z: -0.4 },
    }}
      connections={{
        "pin1": "net.VEE",
        "pin2": "net.GND",
      }} />
    {/* C16 10nF - CC0603KRX7R9BB104 - LCSC C57112 - 0603B103K500NT - jclpcb:C0603 */}
    <capacitor
      name="C16"
      capacitance="10nF"
      footprint={FP_jclpcb_C0603}
      pcbX="1.74mm"
      pcbY="28.13mm"
      layer="bottom"
      supplierPartNumbers={{ jlcpcb: ["C57112"] }}
      manufacturerPartNumber="0603B103K500NT"
      cadModel={{
      objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C57112.obj?uuid=ac9b32e974bc448eab36b1293f859dcb",
      stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C57112.step?uuid=ac9b32e974bc448eab36b1293f859dcb",
      pcbRotationOffset: 0,
      modelOriginPosition: { x: 0, y: 0, z: -0.4 },
    }}
      connections={{
        "pin1": "net.U13_INA_N",
        "pin2": "net.CV_IN_1",
      }} />
    {/* C17 100nF - CC0603KRX7R9BB104 - LCSC C14663 - jclpcb:C0603 */}
    <capacitor
      name="C17"
      capacitance="100nF"
      footprint={FP_jclpcb_C0603}
      pcbX="9.96mm"
      pcbY="1.005mm"
      layer="bottom"
      supplierPartNumbers={{ jlcpcb: ["C14663"] }}
      cadModel={{
      objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C14663.obj?uuid=ac9b32e974bc448eab36b1293f859dcb",
      stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C14663.step?uuid=ac9b32e974bc448eab36b1293f859dcb",
      pcbRotationOffset: 0,
      modelOriginPosition: { x: 0, y: 0, z: -0.4 },
    }}
      connections={{
        "pin1": "net.VCC",
        "pin2": "net.GND",
      }} />
    {/* C18 100nF - CC0603KRX7R9BB104 - LCSC C14663 - jclpcb:C0603 */}
    <capacitor
      name="C18"
      capacitance="100nF"
      footprint={FP_jclpcb_C0603}
      pcbX="9.96mm"
      pcbY="4.005mm"
      layer="bottom"
      supplierPartNumbers={{ jlcpcb: ["C14663"] }}
      cadModel={{
      objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C14663.obj?uuid=ac9b32e974bc448eab36b1293f859dcb",
      stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C14663.step?uuid=ac9b32e974bc448eab36b1293f859dcb",
      pcbRotationOffset: 0,
      modelOriginPosition: { x: 0, y: 0, z: -0.4 },
    }}
      connections={{
        "pin1": "net.VEE",
        "pin2": "net.GND",
      }} />
    {/* C19 100nF - CC0603KRX7R9BB104 - LCSC C14663 - jclpcb:C0603 */}
    <capacitor
      name="C19"
      capacitance="100nF"
      footprint={FP_jclpcb_C0603}
      pcbX="10mm"
      pcbY="11.85mm"
      layer="bottom"
      supplierPartNumbers={{ jlcpcb: ["C14663"] }}
      cadModel={{
      objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C14663.obj?uuid=ac9b32e974bc448eab36b1293f859dcb",
      stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C14663.step?uuid=ac9b32e974bc448eab36b1293f859dcb",
      pcbRotationOffset: 0,
      modelOriginPosition: { x: 0, y: 0, z: -0.4 },
    }}
      connections={{
        "pin1": "net.GND",
        "pin2": "net.P3V3_A",
      }} />
    {/* C20 100nF - CC0603KRX7R9BB104 - LCSC C14663 - jclpcb:C0603 */}
    <capacitor
      name="C20"
      capacitance="100nF"
      footprint={FP_jclpcb_C0603}
      pcbX="8.18mm"
      pcbY="-4.575mm"
      layer="bottom"
      supplierPartNumbers={{ jlcpcb: ["C14663"] }}
      cadModel={{
      objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C14663.obj?uuid=ac9b32e974bc448eab36b1293f859dcb",
      stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C14663.step?uuid=ac9b32e974bc448eab36b1293f859dcb",
      pcbRotationOffset: 0,
      modelOriginPosition: { x: 0, y: 0, z: -0.4 },
    }}
      connections={{
        "pin1": "net.GND",
        "pin2": "net.N10V_REF",
      }} />
    {/* C21 10nF - CC0603KRX7R9BB104 - LCSC C57112 - 0603B103K500NT - jclpcb:C0603 */}
    <capacitor
      name="C21"
      capacitance="10nF"
      footprint={FP_jclpcb_C0603}
      pcbX="11.12mm"
      pcbY="28.13mm"
      layer="bottom"
      supplierPartNumbers={{ jlcpcb: ["C57112"] }}
      manufacturerPartNumber="0603B103K500NT"
      cadModel={{
      objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C57112.obj?uuid=ac9b32e974bc448eab36b1293f859dcb",
      stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C57112.step?uuid=ac9b32e974bc448eab36b1293f859dcb",
      pcbRotationOffset: 0,
      modelOriginPosition: { x: 0, y: 0, z: -0.4 },
    }}
      connections={{
        "pin1": "net.U13_INB_N",
        "pin2": "net.CV_IN_2",
      }} />
    {/* Q1 MMBT3904 - MMBT3904_C181119 - LCSC C181119 - jclpcb:SOT-23-3_L2.9-W1.6-P1.90-LS2.8-BR */}
    <transistor
      name="Q1"
      type="npn"
      footprint={FP_jclpcb_SOT_23_3_L2_9_W1_6_P1_90_LS2_8_BR}
      pinLabels={{ pin1: "base", pin2: "emitter", pin3: "collector" }}
      pcbX="15.045mm"
      pcbY="4.62mm"
      layer="bottom"
      supplierPartNumbers={{ jlcpcb: ["C181119"] }}
      cadModel={{
      objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C181119.obj?uuid=cefd4596db214da394d9632b2b88f8f2",
      stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C181119.step?uuid=cefd4596db214da394d9632b2b88f8f2",
      pcbRotationOffset: 180,
      modelOriginPosition: { x: 0.000012699999956566899, y: 0, z: 0 },
    }}
      connections={{
        "base": "net.NET_Q1_1",
        "emitter": "net.GND",
        "collector": "net.NET_Q1_3",
      }} />
    {/* R21 33K - 0603WAF3302T5E - LCSC C4216 - jclpcb:R0603 */}
    <resistor
      name="R21"
      resistance="33k"
      footprint={FP_jclpcb_R0603}
      pcbX="20.5mm"
      pcbY="28.19mm"
      layer="bottom"
      supplierPartNumbers={{ jlcpcb: ["C4216"] }}
      cadModel={{
      objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C4216.obj?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C4216.step?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      pcbRotationOffset: 0,
      modelOriginPosition: { x: -0.004999999999999977, y: 0, z: -0.01 },
    }}
      connections={{
        "pin1": "net.SYNC_OUT",
        "pin2": "net.U11B_IN_N",
      }} />
    {/* R22 100K - 0603WAF1003T5E - LCSC C25803 - jclpcb:R0603 */}
    <resistor
      name="R22"
      resistance="100k"
      footprint={FP_jclpcb_R0603}
      pcbX="17.78mm"
      pcbY="-19.16mm"
      layer="bottom"
      supplierPartNumbers={{ jlcpcb: ["C25803"] }}
      cadModel={{
      objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C25803.obj?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C25803.step?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      pcbRotationOffset: 0,
      modelOriginPosition: { x: -0.004999999999999977, y: 0, z: -0.01 },
    }}
      connections={{
        "pin1": "net.J_SYNC_IN",
        "pin2": "net.NET_Q1_1",
      }} />
    {/* R23 1M - PA1206FRF470R001L - LCSC C17927 - 1206W4F1004T5E - jclpcb:R1206 */}
    <resistor
      name="R23"
      resistance="1M"
      footprint={FP_jclpcb_R1206}
      pcbX="4.66mm"
      pcbY="-9.08mm"
      layer="bottom"
      supplierPartNumbers={{ jlcpcb: ["C17927"] }}
      manufacturerPartNumber="1206W4F1004T5E"
      cadModel={{
      objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C17927.obj?uuid=da5f078ee43c429f91838a456f48f0a8",
      stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C17927.step?uuid=da5f078ee43c429f91838a456f48f0a8",
      pcbRotationOffset: 0,
      modelOriginPosition: { x: 0, y: 0, z: 0 },
    }}
      connections={{
        "pin1": "net.GND",
        "pin2": "net.NET_Q1_1",
      }} />
    {/* R24 49K9 - 0603WAF4992T5E - LCSC C23184 - jclpcb:R0603 */}
    <resistor
      name="R24"
      resistance="49K9"
      footprint={FP_jclpcb_R0603}
      pcbX="10.26mm"
      pcbY="-8.66mm"
      layer="bottom"
      supplierPartNumbers={{ jlcpcb: ["C23184"] }}
      cadModel={{
      objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C23184.obj?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C23184.step?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      pcbRotationOffset: 0,
      modelOriginPosition: { x: -0.004999999999999977, y: 0, z: -0.01 },
    }}
      connections={{
        "pin1": "net.U11B_IN_N",
        "pin2": "net.NET_R24_2",
      }} />
    {/* R25 10K - 0603WAF1002T5E - LCSC C25804 - jclpcb:R0603 */}
    <resistor
      name="R25"
      resistance="10k"
      footprint={FP_jclpcb_R0603}
      pcbX="14.94mm"
      pcbY="-8.66mm"
      layer="bottom"
      supplierPartNumbers={{ jlcpcb: ["C25804"] }}
      cadModel={{
      objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C25804.obj?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C25804.step?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      pcbRotationOffset: 0,
      modelOriginPosition: { x: -0.004999999999999977, y: 0, z: -0.01 },
    }}
      connections={{
        "pin1": "net.NET_Q1_3",
        "pin2": "net.P3V3_A",
      }} />
    {/* R26 100K - 0603WAF1003T5E - LCSC C25803 - jclpcb:R0603 */}
    <resistor
      name="R26"
      resistance="100k"
      footprint={FP_jclpcb_R0603}
      pcbX="3.74mm"
      pcbY="-12.72mm"
      layer="bottom"
      supplierPartNumbers={{ jlcpcb: ["C25803"] }}
      cadModel={{
      objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C25803.obj?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C25803.step?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      pcbRotationOffset: 0,
      modelOriginPosition: { x: -0.004999999999999977, y: 0, z: -0.01 },
    }}
      connections={{
        "pin1": "net.NET_R24_2",
        "pin2": "net.U11C_IN_N",
      }} />
    {/* R27 1K - 0603WAF1001T5E - LCSC C21190 - jclpcb:R0603 */}
    <resistor
      name="R27"
      resistance="1k"
      footprint={FP_jclpcb_R0603}
      pcbX="1.74mm"
      pcbY="24.86mm"
      layer="bottom"
      supplierPartNumbers={{ jlcpcb: ["C21190"] }}
      cadModel={{
      objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C21190.obj?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C21190.step?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      pcbRotationOffset: 0,
      modelOriginPosition: { x: -0.004999999999999977, y: 0, z: -0.01 },
    }}
      connections={{
        "pin1": "net.NET_Q1_3",
        "pin2": "net.SYNC_IN",
      }} />
    {/* R28 100K - 0603WAF1003T5E - LCSC C25803 - jclpcb:R0603 */}
    <resistor
      name="R28"
      resistance="100k"
      footprint={FP_jclpcb_R0603}
      pcbX="8.42mm"
      pcbY="-12.72mm"
      layer="bottom"
      supplierPartNumbers={{ jlcpcb: ["C25803"] }}
      cadModel={{
      objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C25803.obj?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C25803.step?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      pcbRotationOffset: 0,
      modelOriginPosition: { x: -0.004999999999999977, y: 0, z: -0.01 },
    }}
      connections={{
        "pin1": "net.U11C_IN_N",
        "pin2": "net.NET_R28_2",
      }} />
    {/* R29 1K - ERJP03F1000V - LCSC C21190 - 0603WAF1001T5E - jclpcb:R0603 */}
    <resistor
      name="R29"
      resistance="1k"
      footprint={FP_jclpcb_R0603}
      pcbX="3.74mm"
      pcbY="-22.38mm"
      layer="bottom"
      supplierPartNumbers={{ jlcpcb: ["C21190"] }}
      manufacturerPartNumber="0603WAF1001T5E"
      cadModel={{
      objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C21190.obj?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C21190.step?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      pcbRotationOffset: 0,
      modelOriginPosition: { x: -0.004999999999999977, y: 0, z: -0.01 },
    }}
      connections={{
        "pin1": "net.NET_R28_2",
        "pin2": "net.J_SYNC_OUT",
      }} />
    {/* R30 100K - 0603WAF1003T5E - LCSC C25803 - jclpcb:R0603 */}
    <resistor
      name="R30"
      resistance="100k"
      footprint={FP_jclpcb_R0603}
      pcbX="8.42mm"
      pcbY="-22.38mm"
      layer="bottom"
      supplierPartNumbers={{ jlcpcb: ["C25803"] }}
      cadModel={{
      objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C25803.obj?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C25803.step?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      pcbRotationOffset: 0,
      modelOriginPosition: { x: -0.004999999999999977, y: 0, z: -0.01 },
    }}
      connections={{
        "pin1": "net.J_CV_IN_1",
        "pin2": "net.U13_INA_N",
      }} />
    {/* R31 33K - 0603WAF3302T5E - LCSC C4216 - jclpcb:R0603 */}
    <resistor
      name="R31"
      resistance="33k"
      footprint={FP_jclpcb_R0603}
      pcbX="6.42mm"
      pcbY="24.86mm"
      layer="bottom"
      supplierPartNumbers={{ jlcpcb: ["C4216"] }}
      cadModel={{
      objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C4216.obj?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C4216.step?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      pcbRotationOffset: 0,
      modelOriginPosition: { x: -0.004999999999999977, y: 0, z: -0.01 },
    }}
      connections={{
        "pin1": "net.CV_OUT_1",
        "pin2": "net.U12A_IN_N",
      }} />
    {/* R32 33K - 0603WAF3302T5E - LCSC C4216 - jclpcb:R0603 */}
    <resistor
      name="R32"
      resistance="33k"
      footprint={FP_jclpcb_R0603}
      pcbX="11.1mm"
      pcbY="24.86mm"
      layer="bottom"
      supplierPartNumbers={{ jlcpcb: ["C4216"] }}
      cadModel={{
      objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C4216.obj?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C4216.step?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      pcbRotationOffset: 0,
      modelOriginPosition: { x: -0.004999999999999977, y: 0, z: -0.01 },
    }}
      connections={{
        "pin1": "net.CV_OUT_2",
        "pin2": "net.U12C_IN_N",
      }} />
    {/* R33 33K - 0603WAF3302T5E - LCSC C4216 - jclpcb:R0603 */}
    <resistor
      name="R33"
      resistance="33k"
      footprint={FP_jclpcb_R0603}
      pcbX="6.44mm"
      pcbY="28.19mm"
      layer="bottom"
      supplierPartNumbers={{ jlcpcb: ["C4216"] }}
      cadModel={{
      objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C4216.obj?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C4216.step?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      pcbRotationOffset: 0,
      modelOriginPosition: { x: -0.004999999999999977, y: 0, z: -0.01 },
    }}
      connections={{
        "pin1": "net.U13_INA_N",
        "pin2": "net.CV_IN_1",
      }} />
    {/* R34 200K - 0603WAF2003T5E - LCSC C25811 - jclpcb:R0603 */}
    <resistor
      name="R34"
      resistance="200k"
      footprint={FP_jclpcb_R0603}
      pcbX="13.1mm"
      pcbY="-12.72mm"
      layer="bottom"
      supplierPartNumbers={{ jlcpcb: ["C25811"] }}
      cadModel={{
      objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C25811.obj?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C25811.step?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      pcbRotationOffset: 0,
      modelOriginPosition: { x: -0.004999999999999977, y: 0, z: -0.01 },
    }}
      connections={{
        "pin1": "net.N10V_REF",
        "pin2": "net.U13_INA_N",
      }} />
    {/* R35 49.9K - 0603WAF4992T5E - LCSC C23184 - jclpcb:R0603 */}
    <resistor
      name="R35"
      resistance="49.9k"
      footprint={FP_jclpcb_R0603}
      pcbX="17.78mm"
      pcbY="-12.72mm"
      layer="bottom"
      supplierPartNumbers={{ jlcpcb: ["C23184"] }}
      cadModel={{
      objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C23184.obj?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C23184.step?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      pcbRotationOffset: 0,
      modelOriginPosition: { x: -0.004999999999999977, y: 0, z: -0.01 },
    }}
      connections={{
        "pin1": "net.U12A_IN_N",
        "pin2": "net.NET_R35_2",
      }} />
    {/* R36 49.9K - 0603WAF4992T5E - LCSC C23184 - jclpcb:R0603 */}
    <resistor
      name="R36"
      resistance="49.9k"
      footprint={FP_jclpcb_R0603}
      pcbX="3.74mm"
      pcbY="-15.94mm"
      layer="bottom"
      supplierPartNumbers={{ jlcpcb: ["C23184"] }}
      cadModel={{
      objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C23184.obj?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C23184.step?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      pcbRotationOffset: 0,
      modelOriginPosition: { x: -0.004999999999999977, y: 0, z: -0.01 },
    }}
      connections={{
        "pin1": "net.U12C_IN_N",
        "pin2": "net.NET_R36_2",
      }} />
    {/* R37 100K - 0603WAF1003T5E - LCSC C25803 - jclpcb:R0603 */}
    <resistor
      name="R37"
      resistance="100k"
      footprint={FP_jclpcb_R0603}
      pcbX="8.42mm"
      pcbY="-15.94mm"
      layer="bottom"
      supplierPartNumbers={{ jlcpcb: ["C25803"] }}
      cadModel={{
      objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C25803.obj?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C25803.step?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      pcbRotationOffset: 0,
      modelOriginPosition: { x: -0.004999999999999977, y: 0, z: -0.01 },
    }}
      connections={{
        "pin1": "net.NET_R35_2",
        "pin2": "net.U12B_IN_N",
      }} />
    {/* R38 100K - 0603WAF1003T5E - LCSC C25803 - jclpcb:R0603 */}
    <resistor
      name="R38"
      resistance="100k"
      footprint={FP_jclpcb_R0603}
      pcbX="13.1mm"
      pcbY="-15.94mm"
      layer="bottom"
      supplierPartNumbers={{ jlcpcb: ["C25803"] }}
      cadModel={{
      objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C25803.obj?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C25803.step?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      pcbRotationOffset: 0,
      modelOriginPosition: { x: -0.004999999999999977, y: 0, z: -0.01 },
    }}
      connections={{
        "pin1": "net.NET_R36_2",
        "pin2": "net.U12D_IN_N",
      }} />
    {/* R39 100K - 0603WAF1003T5E - LCSC C25803 - jclpcb:R0603 */}
    <resistor
      name="R39"
      resistance="100k"
      footprint={FP_jclpcb_R0603}
      pcbX="17.78mm"
      pcbY="-15.94mm"
      layer="bottom"
      supplierPartNumbers={{ jlcpcb: ["C25803"] }}
      cadModel={{
      objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C25803.obj?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C25803.step?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      pcbRotationOffset: 0,
      modelOriginPosition: { x: -0.004999999999999977, y: 0, z: -0.01 },
    }}
      connections={{
        "pin1": "net.U12D_IN_N",
        "pin2": "net.NET_R39_2",
      }} />
    {/* R40 100K - 0603WAF1003T5E - LCSC C25803 - jclpcb:R0603 */}
    <resistor
      name="R40"
      resistance="100k"
      footprint={FP_jclpcb_R0603}
      pcbX="3.74mm"
      pcbY="-19.16mm"
      layer="bottom"
      supplierPartNumbers={{ jlcpcb: ["C25803"] }}
      cadModel={{
      objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C25803.obj?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C25803.step?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      pcbRotationOffset: 0,
      modelOriginPosition: { x: -0.004999999999999977, y: 0, z: -0.01 },
    }}
      connections={{
        "pin1": "net.U12B_IN_N",
        "pin2": "net.NET_R40_2",
      }} />
    {/* R41 2.2K - 0603WAF2201T5E - LCSC C4190 - jclpcb:R0603 */}
    <resistor
      name="R41"
      resistance="2.2k"
      footprint={FP_jclpcb_R0603}
      pcbX="8.42mm"
      pcbY="-19.16mm"
      layer="bottom"
      supplierPartNumbers={{ jlcpcb: ["C4190"] }}
      cadModel={{
      objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C4190.obj?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C4190.step?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      pcbRotationOffset: 0,
      modelOriginPosition: { x: -0.004999999999999977, y: 0, z: -0.01 },
    }}
      connections={{
        "pin1": "net.N10V_REF",
        "pin2": "net.VEE",
      }} />
    {/* R42 33K - 0603WAF3302T5E - LCSC C4216 - jclpcb:R0603 */}
    <resistor
      name="R42"
      resistance="33k"
      footprint={FP_jclpcb_R0603}
      pcbX="15.82mm"
      pcbY="28.19mm"
      layer="bottom"
      supplierPartNumbers={{ jlcpcb: ["C4216"] }}
      cadModel={{
      objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C4216.obj?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C4216.step?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      pcbRotationOffset: 0,
      modelOriginPosition: { x: -0.004999999999999977, y: 0, z: -0.01 },
    }}
      connections={{
        "pin1": "net.U13_INB_N",
        "pin2": "net.CV_IN_2",
      }} />
    {/* R43 200K - 0603WAF2003T5E - LCSC C25811 - jclpcb:R0603 */}
    <resistor
      name="R43"
      resistance="200k"
      footprint={FP_jclpcb_R0603}
      pcbX="13.1mm"
      pcbY="-19.16mm"
      layer="bottom"
      supplierPartNumbers={{ jlcpcb: ["C25811"] }}
      cadModel={{
      objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C25811.obj?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C25811.step?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      pcbRotationOffset: 0,
      modelOriginPosition: { x: -0.004999999999999977, y: 0, z: -0.01 },
    }}
      connections={{
        "pin1": "net.N10V_REF",
        "pin2": "net.U13_INB_N",
      }} />
    {/* R44 100K - 0603WAF1003T5E - LCSC C25803 - jclpcb:R0603 */}
    <resistor
      name="R44"
      resistance="100k"
      footprint={FP_jclpcb_R0603}
      pcbX="13.1mm"
      pcbY="-22.38mm"
      layer="bottom"
      supplierPartNumbers={{ jlcpcb: ["C25803"] }}
      cadModel={{
      objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C25803.obj?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C25803.step?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      pcbRotationOffset: 0,
      modelOriginPosition: { x: -0.004999999999999977, y: 0, z: -0.01 },
    }}
      connections={{
        "pin1": "net.J_CV_IN_2",
        "pin2": "net.U13_INB_N",
      }} />
    {/* R45 1K - 0603WAF1001T5E - LCSC C21190 - jclpcb:R0603 */}
    <resistor
      name="R45"
      resistance="1k"
      footprint={FP_jclpcb_R0603}
      pcbX="17.78mm"
      pcbY="-22.38mm"
      layer="bottom"
      supplierPartNumbers={{ jlcpcb: ["C21190"] }}
      cadModel={{
      objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C21190.obj?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C21190.step?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      pcbRotationOffset: 0,
      modelOriginPosition: { x: -0.004999999999999977, y: 0, z: -0.01 },
    }}
      connections={{
        "pin1": "net.NET_R39_2",
        "pin2": "net.J_CV_OUT_2",
      }} />
    {/* R46 1K - 0603WAF1001T5E - LCSC C21190 - jclpcb:R0603 */}
    <resistor
      name="R46"
      resistance="1k"
      footprint={FP_jclpcb_R0603}
      pcbX="3.74mm"
      pcbY="-25.6mm"
      layer="bottom"
      supplierPartNumbers={{ jlcpcb: ["C21190"] }}
      cadModel={{
      objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C21190.obj?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C21190.step?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      pcbRotationOffset: 0,
      modelOriginPosition: { x: -0.004999999999999977, y: 0, z: -0.01 },
    }}
      connections={{
        "pin1": "net.NET_R40_2",
        "pin2": "net.J_CV_OUT_1",
      }} />
    {/* U11 TL074 - LCSC C96507 - jclpcb:TSSOP-14_L5.0-W4.4-P0.65-LS6.4-BL */}
    <chip
      name="U11"
      footprint={FP_jclpcb_TSSOP_14_L5_0_W4_4_P0_65_LS6_4_BL}
      pinLabels={TL074_PINS}
      pinAttributes={{
        "OUT1": { providesPower: true },
        "IN1N": { requiresPower: true },
        "IN1P": { requiresPower: true, requiresGround: true },
        "VPLUS": { requiresPower: true },
        "IN2P": { requiresPower: true, requiresGround: true },
        "IN2N": { requiresPower: true },
        "OUT2": { providesPower: true },
        "OUT3": { providesPower: true },
        "IN3N": { requiresPower: true },
        "IN3P": { requiresPower: true, requiresGround: true },
        "VMINUS": { requiresPower: true },
        "IN4P": { requiresPower: true, requiresGround: true },
        "IN4N": { requiresPower: true },
        "OUT4": { providesPower: true },
      }}
      schPinArrangement={{ leftSide: ["IN1N", "IN1P", "VPLUS", "IN2P", "IN2N", "IN3N", "IN3P", "VMINUS", "IN4P", "IN4N"], rightSide: ["OUT1", "OUT2", "OUT3", "OUT4"] }}
      pcbX="15.81mm"
      pcbY="12mm"
      layer="bottom"
      supplierPartNumbers={{ jlcpcb: ["C96507"] }}
      cadModel={{
      objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C96507.obj?uuid=5377177da492449fa1a3111d646cac17",
      stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C96507.step?uuid=5377177da492449fa1a3111d646cac17",
      pcbRotationOffset: 270,
      modelOriginPosition: { x: -0.000012700000013410317, y: 0, z: -0.069083 },
    }}
      connections={{
        "OUT1": "net.U11A_IN_N",
        "IN1N": "net.U11A_IN_N",
        "IN1P": "net.GND",
        "VPLUS": "net.VCC",
        "IN2P": "net.GND",
        "IN2N": "net.U11B_IN_N",
        "OUT2": "net.NET_R24_2",
        "OUT3": "net.NET_R28_2",
        "IN3N": "net.U11C_IN_N",
        "IN3P": "net.GND",
        "VMINUS": "net.VEE",
        "IN4P": "net.GND",
        "IN4N": "net.U11D_IN_N",
        "OUT4": "net.U11D_IN_N",
      }} />
    {/* U12 TL074 - LCSC C96507 - jclpcb:TSSOP-14_L5.0-W4.4-P0.65-LS6.4-BL */}
    <chip
      name="U12"
      footprint={FP_jclpcb_TSSOP_14_L5_0_W4_4_P0_65_LS6_4_BL}
      pinLabels={TL074_PINS}
      pinAttributes={{
        "OUT1": { providesPower: true },
        "IN1N": { requiresPower: true },
        "IN1P": { requiresPower: true, requiresGround: true },
        "VPLUS": { requiresPower: true },
        "IN2P": { requiresPower: true, requiresGround: true },
        "IN2N": { requiresPower: true },
        "OUT2": { providesPower: true },
        "OUT3": { providesPower: true },
        "IN3N": { requiresPower: true },
        "IN3P": { requiresPower: true, requiresGround: true },
        "VMINUS": { requiresPower: true },
        "IN4P": { requiresPower: true, requiresGround: true },
        "IN4N": { requiresPower: true },
        "OUT4": { providesPower: true },
      }}
      schPinArrangement={{ leftSide: ["IN1N", "IN1P", "VPLUS", "IN2P", "IN2N", "IN3N", "IN3P", "VMINUS", "IN4P", "IN4N"], rightSide: ["OUT1", "OUT2", "OUT3", "OUT4"] }}
      pcbX="4.85mm"
      pcbY="2.51mm"
      layer="bottom"
      supplierPartNumbers={{ jlcpcb: ["C96507"] }}
      cadModel={{
      objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C96507.obj?uuid=5377177da492449fa1a3111d646cac17",
      stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C96507.step?uuid=5377177da492449fa1a3111d646cac17",
      pcbRotationOffset: 270,
      modelOriginPosition: { x: -0.000012700000013410317, y: 0, z: -0.069083 },
    }}
      connections={{
        "OUT1": "net.NET_R35_2",
        "IN1N": "net.U12A_IN_N",
        "IN1P": "net.GND",
        "VPLUS": "net.VCC",
        "IN2P": "net.GND",
        "IN2N": "net.U12B_IN_N",
        "OUT2": "net.NET_R40_2",
        "OUT3": "net.NET_R36_2",
        "IN3N": "net.U12C_IN_N",
        "IN3P": "net.GND",
        "VMINUS": "net.VEE",
        "IN4P": "net.GND",
        "IN4N": "net.U12D_IN_N",
        "OUT4": "net.NET_R39_2",
      }} />
    {/* U13 MCP6002T-I/SN - MCP6002T-I_SN - LCSC C7377 - jclpcb:SOIC-8_L4.9-W3.9-P1.27-LS6.0-BL */}
    <chip
      name="U13"
      footprint={FP_jclpcb_SOIC_8_L4_9_W3_9_P1_27_LS6_0_BL}
      pinLabels={MCP6002_PINS}
      pinAttributes={{
        "OUTA": { providesPower: true },
        "INA_N": { requiresPower: true },
        "INA_P": { requiresPower: true, requiresGround: true },
        "VSS": { requiresPower: true, requiresGround: true },
        "INB_P": { requiresPower: true, requiresGround: true },
        "INB_N": { requiresPower: true },
        "OUTB": { providesPower: true },
        "VDD": { requiresPower: true },
      }}
      schPinArrangement={{ leftSide: ["INA_N", "INA_P", "VSS", "INB_P", "INB_N", "VDD"], rightSide: ["OUTA", "OUTB"] }}
      pcbX="4.88mm"
      pcbY="11.855mm"
      layer="bottom"
      supplierPartNumbers={{ jlcpcb: ["C7377"] }}
      cadModel={{
      objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C7377.obj?uuid=ec3b9f9b31a74655be3e55848dbee9c1",
      stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C7377.step?uuid=ec3b9f9b31a74655be3e55848dbee9c1",
      pcbRotationOffset: 180,
      modelOriginPosition: { x: -0.000012700000070253736, y: 0, z: 0 },
    }}
      connections={{
        "OUTA": "net.CV_IN_1",
        "INA_N": "net.U13_INA_N",
        "INA_P": "net.GND",
        "VSS": "net.GND",
        "INB_P": "net.GND",
        "INB_N": "net.U13_INB_N",
        "OUTB": "net.CV_IN_2",
        "VDD": "net.P3V3_A",
      }} />
    {/* U14 LM4040C10IDBZR - LCSC C201738 - jclpcb:SOT-23-3_L3.0-W1.7-P0.95-LS2.9-BR */}
    <chip
      name="U14"
      footprint={FP_jclpcb_SOT_23_3_L3_0_W1_7_P0_95_LS2_9_BR}
      pinLabels={LM4040_PINS}
      pinAttributes={{
        "CATHODE": { requiresGround: true },
      }}
      schPinArrangement={{ leftSide: ["CATHODE", "NC"], rightSide: ["ANODE"] }}
      pcbX="3.97mm"
      pcbY="-4.57mm"
      layer="bottom"
      supplierPartNumbers={{ jlcpcb: ["C201738"] }}
      cadModel={{
      objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C201738.obj?uuid=cefd4596db214da394d9632b2b88f8f2",
      stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C201738.step?uuid=cefd4596db214da394d9632b2b88f8f2",
      pcbRotationOffset: 180,
      modelOriginPosition: { x: -0.000012700000070253736, y: 0, z: 0 },
    }}
      connections={{
        "CATHODE": "net.GND",
        "ANODE": "net.N10V_REF",
      }} />
  </schematicsheet>
)

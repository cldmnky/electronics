// Froemix base board - hand-maintained tscircuit source (see froemix/AGENTS.md).
// Ported once from the KiCad project in ../tmp by froemix/tools/kicad2tscircuit.py;
// that tool is retired as an editor, edit this file directly.

// Audio sheet: stereo audio in/out buffers
// Source: tmp/audio.kicad_sch

import { TL074_PINS } from "../lib/pins"
import { FP_jclpcb_C0603, FP_jclpcb_R0603, FP_jclpcb_TSSOP_14_L5_0_W4_4_P0_65_LS6_4_BL } from "../lib/footprints"

import { AUDIO_LAYOUT } from "../lib/schematic-layout"

export const AudioSection = () => (
  <schematicsheet name="audio" displayName="Audio" sheetIndex={2}>
    {/* Schematic-only relative group: `schRelative` switches the auto-layout off
        for these children, so the frozen AUDIO_LAYOUT coordinates are used exactly.
        No pcbX/pcbY, footprint or net is touched, and a group inside a sheet
        is not a subcircuit, so the nets stay global. */}
    <group name="audio-layout" schRelative>
    {/* C2 47pF - CL10C470JB8NNNC - LCSC C1671 - jclpcb:C0603 */}
    <capacitor
      name="C2"
      schX={AUDIO_LAYOUT["C2"].x} schY={AUDIO_LAYOUT["C2"].y}
      schRotation={90}
      capacitance="47pF"
      footprint={FP_jclpcb_C0603}
      pcbX="-7.86mm"
      pcbY="5.73mm"
      layer="bottom"
      supplierPartNumbers={{ jlcpcb: ["C1671"] }}
      cadModel={{
      objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C14663.obj?uuid=ac9b32e974bc448eab36b1293f859dcb",
      stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C14663.step?uuid=ac9b32e974bc448eab36b1293f859dcb",
      pcbRotationOffset: 0,
      modelOriginPosition: { x: 0, y: 0, z: -0.4 },
    }}
      connections={{
        "pin1": "net.U2A_IN_N",
        "pin2": "net.NET_C2_2",
      }} />
    {/* C3 47pF - CL10C470JB8NNNC - LCSC C1671 - jclpcb:C0603 */}
    <capacitor
      name="C3"
      schX={AUDIO_LAYOUT["C3"].x} schY={AUDIO_LAYOUT["C3"].y}
      schRotation={90}
      capacitance="47pF"
      footprint={FP_jclpcb_C0603}
      pcbX="-3.16mm"
      pcbY="5.73mm"
      layer="bottom"
      supplierPartNumbers={{ jlcpcb: ["C1671"] }}
      cadModel={{
      objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C14663.obj?uuid=ac9b32e974bc448eab36b1293f859dcb",
      stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C14663.step?uuid=ac9b32e974bc448eab36b1293f859dcb",
      pcbRotationOffset: 0,
      modelOriginPosition: { x: 0, y: 0, z: -0.4 },
    }}
      connections={{
        "pin1": "net.U2C_IN_N",
        "pin2": "net.NET_C3_2",
      }} />
    {/* C4 100nF - CC0603KRX7R9BB104 - LCSC C14663 - jclpcb:C0603 */}
    <capacitor
      name="C4"
      schX={AUDIO_LAYOUT["C4"].x} schY={AUDIO_LAYOUT["C4"].y}
      capacitance="100nF"
      footprint={FP_jclpcb_C0603}
      pcbX="-11.04mm"
      pcbY="10.495mm"
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
    {/* C5 100nF - CC0603KRX7R9BB104 - LCSC C14663 - jclpcb:C0603 */}
    <capacitor
      name="C5"
      schX={AUDIO_LAYOUT["C5"].x} schY={AUDIO_LAYOUT["C5"].y}
      capacitance="100nF"
      footprint={FP_jclpcb_C0603}
      pcbX="-11.04mm"
      pcbY="13.495mm"
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
    {/* C6 560pF - CL10C561JB8NNNC - LCSC C107054 - CC0603JRNPO9BN561 - jclpcb:C0603 */}
    <capacitor
      name="C6"
      schX={AUDIO_LAYOUT["C6"].x} schY={AUDIO_LAYOUT["C6"].y}
      schRotation={90}
      capacitance="560pF"
      footprint={FP_jclpcb_C0603}
      pcbX="-17.26mm"
      pcbY="2.4mm"
      layer="bottom"
      supplierPartNumbers={{ jlcpcb: ["C107054"] }}
      manufacturerPartNumber="CC0603JRNPO9BN561"
      cadModel={{
      objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C107054.obj?uuid=ac9b32e974bc448eab36b1293f859dcb",
      stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C107054.step?uuid=ac9b32e974bc448eab36b1293f859dcb",
      pcbRotationOffset: 0,
      modelOriginPosition: { x: 0, y: 0, z: -0.4 },
    }}
      connections={{
        "pin1": "net.U2D_IN_N",
        "pin2": "net.NET_C6_2",
      }} />
    {/* C7 560pF - CL10C561JB8NNNC - LCSC C107054 - CC0603JRNPO9BN561 - jclpcb:C0603 */}
    <capacitor
      name="C7"
      schX={AUDIO_LAYOUT["C7"].x} schY={AUDIO_LAYOUT["C7"].y}
      schRotation={90}
      capacitance="560pF"
      footprint={FP_jclpcb_C0603}
      pcbX="-12.56mm"
      pcbY="2.4mm"
      layer="bottom"
      supplierPartNumbers={{ jlcpcb: ["C107054"] }}
      manufacturerPartNumber="CC0603JRNPO9BN561"
      cadModel={{
      objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C107054.obj?uuid=ac9b32e974bc448eab36b1293f859dcb",
      stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C107054.step?uuid=ac9b32e974bc448eab36b1293f859dcb",
      pcbRotationOffset: 0,
      modelOriginPosition: { x: 0, y: 0, z: -0.4 },
    }}
      connections={{
        "pin1": "net.U2B_IN_N",
        "pin2": "net.NET_C7_2",
      }} />
    {/* C8 47pF - CL10C470JB8NNNC - LCSC C1671 - jclpcb:C0603 */}
    <capacitor
      name="C8"
      schX={AUDIO_LAYOUT["C8"].x} schY={AUDIO_LAYOUT["C8"].y}
      schRotation={90}
      capacitance="47pF"
      footprint={FP_jclpcb_C0603}
      pcbX="-7.86mm"
      pcbY="2.4mm"
      layer="bottom"
      supplierPartNumbers={{ jlcpcb: ["C1671"] }}
      cadModel={{
      objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C14663.obj?uuid=ac9b32e974bc448eab36b1293f859dcb",
      stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C14663.step?uuid=ac9b32e974bc448eab36b1293f859dcb",
      pcbRotationOffset: 0,
      modelOriginPosition: { x: 0, y: 0, z: -0.4 },
    }}
      connections={{
        "pin1": "net.U3A_IN_N",
        "pin2": "net.NET_C8_2",
      }} />
    {/* C9 47pF - CL10C470JB8NNNC - LCSC C1671 - jclpcb:C0603 */}
    <capacitor
      name="C9"
      schX={AUDIO_LAYOUT["C9"].x} schY={AUDIO_LAYOUT["C9"].y}
      schRotation={90}
      capacitance="47pF"
      footprint={FP_jclpcb_C0603}
      pcbX="-3.16mm"
      pcbY="2.4mm"
      layer="bottom"
      supplierPartNumbers={{ jlcpcb: ["C1671"] }}
      cadModel={{
      objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C14663.obj?uuid=ac9b32e974bc448eab36b1293f859dcb",
      stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C14663.step?uuid=ac9b32e974bc448eab36b1293f859dcb",
      pcbRotationOffset: 0,
      modelOriginPosition: { x: 0, y: 0, z: -0.4 },
    }}
      connections={{
        "pin1": "net.U3C_IN_N",
        "pin2": "net.NET_C9_2",
      }} />
    {/* C10 100nF - CC0603KRX7R9BB104 - LCSC C14663 - jclpcb:C0603 */}
    <capacitor
      name="C10"
      schX={AUDIO_LAYOUT["C10"].x} schY={AUDIO_LAYOUT["C10"].y}
      capacitance="100nF"
      footprint={FP_jclpcb_C0603}
      pcbX="-0.12mm"
      pcbY="10.495mm"
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
    {/* C11 100nF - CC0603KRX7R9BB104 - LCSC C14663 - jclpcb:C0603 */}
    <capacitor
      name="C11"
      schX={AUDIO_LAYOUT["C11"].x} schY={AUDIO_LAYOUT["C11"].y}
      capacitance="100nF"
      footprint={FP_jclpcb_C0603}
      pcbX="-0.12mm"
      pcbY="13.495mm"
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
    {/* C12 47pF - CL10C470JB8NNNC - LCSC C1671 - jclpcb:C0603 */}
    <capacitor
      name="C12"
      schX={AUDIO_LAYOUT["C12"].x} schY={AUDIO_LAYOUT["C12"].y}
      schRotation={90}
      capacitance="47pF"
      footprint={FP_jclpcb_C0603}
      pcbX="-17.26mm"
      pcbY="5.73mm"
      layer="bottom"
      supplierPartNumbers={{ jlcpcb: ["C1671"] }}
      cadModel={{
      objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C14663.obj?uuid=ac9b32e974bc448eab36b1293f859dcb",
      stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C14663.step?uuid=ac9b32e974bc448eab36b1293f859dcb",
      pcbRotationOffset: 0,
      modelOriginPosition: { x: 0, y: 0, z: -0.4 },
    }}
      connections={{
        "pin1": "net.U3B_IN_N",
        "pin2": "net.NET_C12_2",
      }} />
    {/* C13 47pF - CL10C470JB8NNNC - LCSC C1671 - jclpcb:C0603 */}
    <capacitor
      name="C13"
      schX={AUDIO_LAYOUT["C13"].x} schY={AUDIO_LAYOUT["C13"].y}
      schRotation={90}
      capacitance="47pF"
      footprint={FP_jclpcb_C0603}
      pcbX="-12.56mm"
      pcbY="5.73mm"
      layer="bottom"
      supplierPartNumbers={{ jlcpcb: ["C1671"] }}
      cadModel={{
      objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C14663.obj?uuid=ac9b32e974bc448eab36b1293f859dcb",
      stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C14663.step?uuid=ac9b32e974bc448eab36b1293f859dcb",
      pcbRotationOffset: 0,
      modelOriginPosition: { x: 0, y: 0, z: -0.4 },
    }}
      connections={{
        "pin1": "net.U3D_IN_N",
        "pin2": "net.NET_C13_2",
      }} />
    {/* R1 100K - 0603WAF1003T5E - LCSC C25803 - jclpcb:R0603 */}
    <resistor
      name="R1"
      schX={AUDIO_LAYOUT["R1"].x} schY={AUDIO_LAYOUT["R1"].y}
      schRotation={90}
      resistance="100k"
      footprint={FP_jclpcb_R0603}
      pcbX="-17.26mm"
      pcbY="-10.53mm"
      layer="bottom"
      supplierPartNumbers={{ jlcpcb: ["C25803"] }}
      cadModel={{
      objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C25803.obj?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C25803.step?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      pcbRotationOffset: 0,
      modelOriginPosition: { x: -0.004999999999999977, y: 0, z: -0.01 },
    }}
      connections={{
        "pin1": "net.J_AUDIO_IN_L",
        "pin2": "net.U2A_IN_N",
      }} />
    {/* R2 100K - 0603WAF1003T5E - LCSC C25803 - jclpcb:R0603 */}
    <resistor
      name="R2"
      schX={AUDIO_LAYOUT["R2"].x} schY={AUDIO_LAYOUT["R2"].y}
      schRotation={90}
      resistance="100k"
      footprint={FP_jclpcb_R0603}
      pcbX="-12.58mm"
      pcbY="-13.75mm"
      layer="bottom"
      supplierPartNumbers={{ jlcpcb: ["C25803"] }}
      cadModel={{
      objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C25803.obj?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C25803.step?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      pcbRotationOffset: 0,
      modelOriginPosition: { x: -0.004999999999999977, y: 0, z: -0.01 },
    }}
      connections={{
        "pin1": "net.J_AUDIO_IN_R",
        "pin2": "net.U2C_IN_N",
      }} />
    {/* R3 100K - 0603WAF1003T5E - LCSC C25803 - jclpcb:R0603 */}
    <resistor
      name="R3"
      schX={AUDIO_LAYOUT["R3"].x} schY={AUDIO_LAYOUT["R3"].y}
      schRotation={90}
      resistance="100k"
      footprint={FP_jclpcb_R0603}
      pcbX="-7.9mm"
      pcbY="-4.09mm"
      layer="bottom"
      supplierPartNumbers={{ jlcpcb: ["C25803"] }}
      cadModel={{
      objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C25803.obj?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C25803.step?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      pcbRotationOffset: 0,
      modelOriginPosition: { x: -0.004999999999999977, y: 0, z: -0.01 },
    }}
      connections={{
        "pin1": "net.U2C_IN_N",
        "pin2": "net.NET_C3_2",
      }} />
    {/* R4 100K - 0603WAF1003T5E - LCSC C25803 - jclpcb:R0603 */}
    <resistor
      name="R4"
      schX={AUDIO_LAYOUT["R4"].x} schY={AUDIO_LAYOUT["R4"].y}
      schRotation={90}
      resistance="100k"
      footprint={FP_jclpcb_R0603}
      pcbX="-3.22mm"
      pcbY="-4.09mm"
      layer="bottom"
      supplierPartNumbers={{ jlcpcb: ["C25803"] }}
      cadModel={{
      objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C25803.obj?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C25803.step?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      pcbRotationOffset: 0,
      modelOriginPosition: { x: -0.004999999999999977, y: 0, z: -0.01 },
    }}
      connections={{
        "pin1": "net.U2A_IN_N",
        "pin2": "net.NET_C2_2",
      }} />
    {/* R5 100K - 0603WAF1003T5E - LCSC C25803 - jclpcb:R0603 */}
    <resistor
      name="R5"
      schX={AUDIO_LAYOUT["R5"].x} schY={AUDIO_LAYOUT["R5"].y}
      schRotation={-90}
      resistance="100k"
      footprint={FP_jclpcb_R0603}
      pcbX="-17.26mm"
      pcbY="-7.31mm"
      layer="bottom"
      supplierPartNumbers={{ jlcpcb: ["C25803"] }}
      cadModel={{
      objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C25803.obj?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C25803.step?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      pcbRotationOffset: 0,
      modelOriginPosition: { x: -0.004999999999999977, y: 0, z: -0.01 },
    }}
      connections={{
        "pin1": "net.NET_C2_2",
        "pin2": "net.U2B_IN_N",
      }} />
    {/* R6 100K - 0603WAF1003T5E - LCSC C25803 - jclpcb:R0603 */}
    <resistor
      name="R6"
      schX={AUDIO_LAYOUT["R6"].x} schY={AUDIO_LAYOUT["R6"].y}
      schRotation={-90}
      resistance="100k"
      footprint={FP_jclpcb_R0603}
      pcbX="-12.58mm"
      pcbY="-7.31mm"
      layer="bottom"
      supplierPartNumbers={{ jlcpcb: ["C25803"] }}
      cadModel={{
      objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C25803.obj?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C25803.step?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      pcbRotationOffset: 0,
      modelOriginPosition: { x: -0.004999999999999977, y: 0, z: -0.01 },
    }}
      connections={{
        "pin1": "net.NET_C3_2",
        "pin2": "net.U2D_IN_N",
      }} />
    {/* R7 10K - 0603WAF1002T5E - LCSC C25804 - jclpcb:R0603 */}
    <resistor
      name="R7"
      schX={AUDIO_LAYOUT["R7"].x} schY={AUDIO_LAYOUT["R7"].y}
      schRotation={90}
      resistance="10k"
      footprint={FP_jclpcb_R0603}
      pcbX="-7.9mm"
      pcbY="-7.31mm"
      layer="bottom"
      supplierPartNumbers={{ jlcpcb: ["C25804"] }}
      cadModel={{
      objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C25804.obj?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C25804.step?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      pcbRotationOffset: 0,
      modelOriginPosition: { x: -0.004999999999999977, y: 0, z: -0.01 },
    }}
      connections={{
        "pin1": "net.U2D_IN_N",
        "pin2": "net.NET_C6_2",
      }} />
    {/* R8 10K - 0603WAF1002T5E - LCSC C25804 - jclpcb:R0603 */}
    <resistor
      name="R8"
      schX={AUDIO_LAYOUT["R8"].x} schY={AUDIO_LAYOUT["R8"].y}
      schRotation={90}
      resistance="10k"
      footprint={FP_jclpcb_R0603}
      pcbX="-3.22mm"
      pcbY="-7.31mm"
      layer="bottom"
      supplierPartNumbers={{ jlcpcb: ["C25804"] }}
      cadModel={{
      objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C25804.obj?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C25804.step?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      pcbRotationOffset: 0,
      modelOriginPosition: { x: -0.004999999999999977, y: 0, z: -0.01 },
    }}
      connections={{
        "pin1": "net.U2B_IN_N",
        "pin2": "net.NET_C7_2",
      }} />
    {/* R9 1K - 0603WAF1001T5E - LCSC C21190 - jclpcb:R0603 */}
    <resistor
      name="R9"
      schX={AUDIO_LAYOUT["R9"].x} schY={AUDIO_LAYOUT["R9"].y}
      schRotation={-90}
      resistance="1k"
      footprint={FP_jclpcb_R0603}
      pcbX="-3.22mm"
      pcbY="-13.75mm"
      layer="bottom"
      supplierPartNumbers={{ jlcpcb: ["C21190"] }}
      cadModel={{
      objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C21190.obj?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C21190.step?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      pcbRotationOffset: 0,
      modelOriginPosition: { x: -0.004999999999999977, y: 0, z: -0.01 },
    }}
      connections={{
        "pin1": "net.NET_C7_2",
        "pin2": "net.AUDIO_IN_L",
      }} />
    {/* R10 1K - 0603WAF1001T5E - LCSC C21190 - jclpcb:R0603 */}
    <resistor
      name="R10"
      schX={AUDIO_LAYOUT["R10"].x} schY={AUDIO_LAYOUT["R10"].y}
      schRotation={-90}
      resistance="1k"
      footprint={FP_jclpcb_R0603}
      pcbX="-12.58mm"
      pcbY="-10.53mm"
      layer="bottom"
      supplierPartNumbers={{ jlcpcb: ["C21190"] }}
      cadModel={{
      objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C21190.obj?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C21190.step?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      pcbRotationOffset: 0,
      modelOriginPosition: { x: -0.004999999999999977, y: 0, z: -0.01 },
    }}
      connections={{
        "pin1": "net.NET_C6_2",
        "pin2": "net.AUDIO_IN_R",
      }} />
    {/* R11 10K - 0603WAF1002T5E - LCSC C25804 - jclpcb:R0603 */}
    <resistor
      name="R11"
      schX={AUDIO_LAYOUT["R11"].x} schY={AUDIO_LAYOUT["R11"].y}
      schRotation={-90}
      resistance="10k"
      footprint={FP_jclpcb_R0603}
      pcbX="-7.9mm"
      pcbY="-10.53mm"
      layer="bottom"
      supplierPartNumbers={{ jlcpcb: ["C25804"] }}
      cadModel={{
      objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C25804.obj?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C25804.step?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      pcbRotationOffset: 0,
      modelOriginPosition: { x: -0.004999999999999977, y: 0, z: -0.01 },
    }}
      connections={{
        "pin1": "net.AUDIO_OUT_L",
        "pin2": "net.U3A_IN_N",
      }} />
    {/* R12 10K - 0603WAF1002T5E - LCSC C25804 - jclpcb:R0603 */}
    <resistor
      name="R12"
      schX={AUDIO_LAYOUT["R12"].x} schY={AUDIO_LAYOUT["R12"].y}
      schRotation={-90}
      resistance="10k"
      footprint={FP_jclpcb_R0603}
      pcbX="-3.22mm"
      pcbY="-10.53mm"
      layer="bottom"
      supplierPartNumbers={{ jlcpcb: ["C25804"] }}
      cadModel={{
      objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C25804.obj?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C25804.step?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      pcbRotationOffset: 0,
      modelOriginPosition: { x: -0.004999999999999977, y: 0, z: -0.01 },
    }}
      connections={{
        "pin1": "net.AUDIO_OUT_R",
        "pin2": "net.U3C_IN_N",
      }} />
    {/* R13 33k - 0603WAF3302T5E - LCSC C4216 - jclpcb:R0603 */}
    <resistor
      name="R13"
      schX={AUDIO_LAYOUT["R13"].x} schY={AUDIO_LAYOUT["R13"].y}
      schRotation={90}
      resistance="33k"
      footprint={FP_jclpcb_R0603}
      pcbX="-17.26mm"
      pcbY="-0.87mm"
      layer="bottom"
      supplierPartNumbers={{ jlcpcb: ["C4216"] }}
      manufacturerPartNumber="0603WAF3302T5E"
      cadModel={{
      objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C4216.obj?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C4216.step?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      pcbRotationOffset: 0,
      modelOriginPosition: { x: -0.004999999999999977, y: 0, z: -0.01 },
    }}
      connections={{
        "pin1": "net.U3A_IN_N",
        "pin2": "net.NET_C8_2",
      }} />
    {/* R14 33k - 0603WAF3302T5E - LCSC C4216 - jclpcb:R0603 */}
    <resistor
      name="R14"
      schX={AUDIO_LAYOUT["R14"].x} schY={AUDIO_LAYOUT["R14"].y}
      schRotation={90}
      resistance="33k"
      footprint={FP_jclpcb_R0603}
      pcbX="-12.58mm"
      pcbY="-0.87mm"
      layer="bottom"
      supplierPartNumbers={{ jlcpcb: ["C4216"] }}
      manufacturerPartNumber="0603WAF3302T5E"
      cadModel={{
      objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C4216.obj?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C4216.step?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      pcbRotationOffset: 0,
      modelOriginPosition: { x: -0.004999999999999977, y: 0, z: -0.01 },
    }}
      connections={{
        "pin1": "net.U3C_IN_N",
        "pin2": "net.NET_C9_2",
      }} />
    {/* R15 100K - 0603WAF1003T5E - LCSC C25803 - jclpcb:R0603 */}
    <resistor
      name="R15"
      schX={AUDIO_LAYOUT["R15"].x} schY={AUDIO_LAYOUT["R15"].y}
      schRotation={-90}
      resistance="100k"
      footprint={FP_jclpcb_R0603}
      pcbX="-7.9mm"
      pcbY="-0.87mm"
      layer="bottom"
      supplierPartNumbers={{ jlcpcb: ["C25803"] }}
      cadModel={{
      objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C25803.obj?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C25803.step?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      pcbRotationOffset: 0,
      modelOriginPosition: { x: -0.004999999999999977, y: 0, z: -0.01 },
    }}
      connections={{
        "pin1": "net.NET_C9_2",
        "pin2": "net.U3D_IN_N",
      }} />
    {/* R16 100K - 0603WAF1003T5E - LCSC C25803 - jclpcb:R0603 */}
    <resistor
      name="R16"
      schX={AUDIO_LAYOUT["R16"].x} schY={AUDIO_LAYOUT["R16"].y}
      schRotation={-90}
      resistance="100k"
      footprint={FP_jclpcb_R0603}
      pcbX="-3.22mm"
      pcbY="-0.87mm"
      layer="bottom"
      supplierPartNumbers={{ jlcpcb: ["C25803"] }}
      cadModel={{
      objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C25803.obj?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C25803.step?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      pcbRotationOffset: 0,
      modelOriginPosition: { x: -0.004999999999999977, y: 0, z: -0.01 },
    }}
      connections={{
        "pin1": "net.NET_C8_2",
        "pin2": "net.U3B_IN_N",
      }} />
    {/* R17 100K - 0603WAF1003T5E - LCSC C25803 - jclpcb:R0603 */}
    <resistor
      name="R17"
      schX={AUDIO_LAYOUT["R17"].x} schY={AUDIO_LAYOUT["R17"].y}
      schRotation={90}
      resistance="100k"
      footprint={FP_jclpcb_R0603}
      pcbX="-17.26mm"
      pcbY="-4.09mm"
      layer="bottom"
      supplierPartNumbers={{ jlcpcb: ["C25803"] }}
      cadModel={{
      objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C25803.obj?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C25803.step?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      pcbRotationOffset: 0,
      modelOriginPosition: { x: -0.004999999999999977, y: 0, z: -0.01 },
    }}
      connections={{
        "pin1": "net.U3B_IN_N",
        "pin2": "net.NET_C12_2",
      }} />
    {/* R18 100K - 0603WAF1003T5E - LCSC C25803 - jclpcb:R0603 */}
    <resistor
      name="R18"
      schX={AUDIO_LAYOUT["R18"].x} schY={AUDIO_LAYOUT["R18"].y}
      schRotation={90}
      resistance="100k"
      footprint={FP_jclpcb_R0603}
      pcbX="-12.58mm"
      pcbY="-4.09mm"
      layer="bottom"
      supplierPartNumbers={{ jlcpcb: ["C25803"] }}
      cadModel={{
      objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C25803.obj?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C25803.step?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      pcbRotationOffset: 0,
      modelOriginPosition: { x: -0.004999999999999977, y: 0, z: -0.01 },
    }}
      connections={{
        "pin1": "net.U3D_IN_N",
        "pin2": "net.NET_C13_2",
      }} />
    {/* R19 1K - 0603WAF1001T5E - LCSC C21190 - jclpcb:R0603 */}
    <resistor
      name="R19"
      schX={AUDIO_LAYOUT["R19"].x} schY={AUDIO_LAYOUT["R19"].y}
      schRotation={-90}
      resistance="1k"
      footprint={FP_jclpcb_R0603}
      pcbX="-17.26mm"
      pcbY="-13.75mm"
      layer="bottom"
      supplierPartNumbers={{ jlcpcb: ["C21190"] }}
      cadModel={{
      objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C21190.obj?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C21190.step?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      pcbRotationOffset: 0,
      modelOriginPosition: { x: -0.004999999999999977, y: 0, z: -0.01 },
    }}
      connections={{
        "pin1": "net.NET_C12_2",
        "pin2": "net.J_AUDIO_OUT_L",
      }} />
    {/* R20 1K - 0603WAF1001T5E - LCSC C21190 - jclpcb:R0603 */}
    <resistor
      name="R20"
      schX={AUDIO_LAYOUT["R20"].x} schY={AUDIO_LAYOUT["R20"].y}
      schRotation={-90}
      resistance="1k"
      footprint={FP_jclpcb_R0603}
      pcbX="-7.9mm"
      pcbY="-13.75mm"
      layer="bottom"
      supplierPartNumbers={{ jlcpcb: ["C21190"] }}
      cadModel={{
      objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C21190.obj?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C21190.step?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      pcbRotationOffset: 0,
      modelOriginPosition: { x: -0.004999999999999977, y: 0, z: -0.01 },
    }}
      connections={{
        "pin1": "net.NET_C13_2",
        "pin2": "net.J_AUDIO_OUT_R",
      }} />
    {/* U2 TL074 - LCSC C96507 - jclpcb:TSSOP-14_L5.0-W4.4-P0.65-LS6.4-BL */}
    <chip
      name="U2"
      schX={AUDIO_LAYOUT["U2"].x} schY={AUDIO_LAYOUT["U2"].y}
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
      pcbX="-16.15mm"
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
        "OUT1": "net.NET_C2_2",
        "IN1N": "net.U2A_IN_N",
        "IN1P": "net.GND",
        "VPLUS": "net.VCC",
        "IN2P": "net.GND",
        "IN2N": "net.U2B_IN_N",
        "OUT2": "net.NET_C7_2",
        "OUT3": "net.NET_C3_2",
        "IN3N": "net.U2C_IN_N",
        "IN3P": "net.GND",
        "VMINUS": "net.VEE",
        "IN4P": "net.GND",
        "IN4N": "net.U2D_IN_N",
        "OUT4": "net.NET_C6_2",
      }} />
    {/* U3 TL074 - LCSC C96507 - jclpcb:TSSOP-14_L5.0-W4.4-P0.65-LS6.4-BL */}
    <chip
      name="U3"
      schX={AUDIO_LAYOUT["U3"].x} schY={AUDIO_LAYOUT["U3"].y}
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
      pcbX="-5.23mm"
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
        "OUT1": "net.NET_C8_2",
        "IN1N": "net.U3A_IN_N",
        "IN1P": "net.GND",
        "VPLUS": "net.VCC",
        "IN2P": "net.GND",
        "IN2N": "net.U3B_IN_N",
        "OUT2": "net.NET_C12_2",
        "OUT3": "net.NET_C9_2",
        "IN3N": "net.U3C_IN_N",
        "IN3P": "net.GND",
        "VMINUS": "net.VEE",
        "IN4P": "net.GND",
        "IN4N": "net.U3D_IN_N",
        "OUT4": "net.NET_C13_2",
      }} />
    </group>

    {/* Sheet notes: fixed positions in the free band above the auto-laid-out
        components (y 11.1 down, x -15.6 left aligned). tools/check_schematic.py
        asserts every note stays clear of the component and net-label boxes. */}
    <schematictext schX={-15.6} schY={11.10} anchor="center_left" fontSize={0.17}
      text="Audio sheet: two TL074 stages, VCC / VEE rails, one 100nF pair per package (C4/C5 at U2, C10/C11 at U3)." />
    <schematictext schX={-15.6} schY={10.65} anchor="center_left" fontSize={0.17}
      text="Audio in (U2, A-D): 100k input and feedback, 0.1000 V/V simulated, -3 dB at 19.7kHz. R1/R2 100k, C2/C3 47pF, C6/C7 560pF feedback." />
    <schematictext schX={-15.6} schY={10.20} anchor="center_left" fontSize={0.17}
      text="Audio out (U3, A-D): gain 3.300 (10.37dB) from the Seed inputs AUDIO_OUT_L/R, -3 dB at 30.9kHz simulated (30.0kHz with the TL074 model)." />
    <schematictext schX={-15.6} schY={9.75} anchor="center_left" fontSize={0.17}
      text="R11/R12 10k input, R13/R14 33k feedback, R19/R20 1k series at the jack. 1Vrms full scale becomes 4.62V peak, inside the +/-11.7V rails." />
    <schematictext schX={-15.6} schY={9.30} anchor="center_left" fontSize={0.17}
      text="Both audio chains have TWO cascaded poles: quote the simulated 19.7kHz / 30.9kHz, not a single-pole estimate." />
    <schematictext schX={-15.6} schY={8.85} anchor="center_left" fontSize={0.17}
      text="R9/R10 1k feed the Seed's AUDIO_IN_L/R from U2B/U2D; the Seed input is 3.3V-only, so the in-stage gain stays below 1." />
    <schematictext schX={-15.6} schY={8.40} anchor="center_left" fontSize={0.17}
      text="All values in tools/sim/audio_in.cir and audio_out.cir; results in tools/sim/RESULTS.md (simulated, not measured)." />
  </schematicsheet>
)

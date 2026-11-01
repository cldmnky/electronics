// Froemix base board - hand-maintained tscircuit source (see froemix/AGENTS.md).
// Ported once from the KiCad project in ../tmp by froemix/tools/kicad2tscircuit.py;
// that tool is retired as an editor, edit this file directly.

// Power sheet: Eurorack +-12V entry, +5V rail and +/-10V rails
// Source: tmp/power.kicad_sch

import { B1205S_PINS } from "../lib/pins"
import { FP_4ms_Connector_Pins_2x08_2_54mm_TH_EurorackPower_Shrouded_R90, FP_jclpcb_C0603, FP_jclpcb_C1206, FP_jclpcb_CAP_TH_BD8_0_P3_50_D0_6_FD, FP_jclpcb_L0603, FP_jclpcb_PWRM_TH_4P_L11_5_W6_0_P2_54_B1503S, FP_jclpcb_SOD_123_L2_7_W1_6_LS3_7_RD_1 } from "../lib/footprints"

export const PowerSection = () => (
  <schematicsheet name="power" displayName="Power" sheetIndex={4}>
    {/* C22 10uF - CL31A106KBHNNNE - LCSC C13585 - CL31A106KBHNNNE - jclpcb:C1206 */}
    <capacitor
      name="C22"
      capacitance="10uF"
      footprint={FP_jclpcb_C1206}
      pcbX="20.62mm"
      pcbY="-46.44mm"
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
        "pin2": "net.VCC",
      }} />
    {/* C23 10uF - CL31A106KBHNNNE - LCSC C13585 - CL31A106KBHNNNE - jclpcb:C1206 */}
    <capacitor
      name="C23"
      capacitance="10uF"
      footprint={FP_jclpcb_C1206}
      pcbX="4.94mm"
      pcbY="-54.67mm"
      supplierPartNumbers={{ jlcpcb: ["C13585"] }}
      manufacturerPartNumber="CL31A106KBHNNNE"
      cadModel={{
      objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C13585.obj?uuid=c6790e9475e1483991d2c64340b24e96",
      stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C13585.step?uuid=c6790e9475e1483991d2c64340b24e96",
      pcbRotationOffset: 0,
      modelOriginPosition: { x: 0, y: 0, z: -0.65 },
    }}
      connections={{
        "pin1": "net.VEE",
        "pin2": "net.GND",
      }} />
    {/* C24 100uF - 50V100uF8X12 - LCSC C2749 - 50V 100uF 8X12 - jclpcb:CAP-TH_BD8.0-P3.50-D0.6-FD */}
    <capacitor
      name="C24"
      capacitance="100uF"
      footprint={FP_jclpcb_CAP_TH_BD8_0_P3_50_D0_6_FD}
      polarized={true}
      pcbX="28mm"
      pcbY="-44.5mm"
      layer="bottom"
      supplierPartNumbers={{ jlcpcb: ["C2749"] }}
      manufacturerPartNumber="50V 100uF 8X12"
      cadModel={{
      objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C2749.obj?uuid=5d8fef05026249fba67d921d4fb62a95",
      stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C2749.step?uuid=5d8fef05026249fba67d921d4fb62a95",
      pcbRotationOffset: 0,
      modelOriginPosition: { x: 0, y: 0, z: -0.000006999999999646178 },
    }}
      connections={{
        "pin1": "net.VCC",
        "pin2": "net.GND",
      }} />
    {/* C25 100uF - 50V100uF8X12 - LCSC C2749 - 50V 100uF 8X12 - jclpcb:CAP-TH_BD8.0-P3.50-D0.6-FD */}
    <capacitor
      name="C25"
      capacitance="100uF"
      footprint={FP_jclpcb_CAP_TH_BD8_0_P3_50_D0_6_FD}
      polarized={true}
      pcbX="33.5mm"
      pcbY="-53.5mm"
      layer="bottom"
      supplierPartNumbers={{ jlcpcb: ["C2749"] }}
      manufacturerPartNumber="50V 100uF 8X12"
      cadModel={{
      objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C2749.obj?uuid=5d8fef05026249fba67d921d4fb62a95",
      stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C2749.step?uuid=5d8fef05026249fba67d921d4fb62a95",
      pcbRotationOffset: 0,
      modelOriginPosition: { x: 0, y: 0, z: -0.000006999999999646178 },
    }}
      connections={{
        "pin1": "net.GND",
        "pin2": "net.VEE",
      }} />
    {/* C26 10uF - CL31A106KBHNNNE - LCSC C13585 - CL31A106KBHNNNE - jclpcb:C1206 */}
    <capacitor
      name="C26"
      capacitance="10uF"
      footprint={FP_jclpcb_C1206}
      pcbX="19.86mm"
      pcbY="-54.67mm"
      supplierPartNumbers={{ jlcpcb: ["C13585"] }}
      manufacturerPartNumber="CL31A106KBHNNNE"
      cadModel={{
      objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C13585.obj?uuid=c6790e9475e1483991d2c64340b24e96",
      stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C13585.step?uuid=c6790e9475e1483991d2c64340b24e96",
      pcbRotationOffset: 0,
      modelOriginPosition: { x: 0, y: 0, z: -0.65 },
    }}
      connections={{
        "pin1": "net.VEE",
        "pin2": "net.VCC",
      }} />
    {/* C27 10uF - CL31A106KBHNNNE - LCSC C13585 - CL31A106KBHNNNE - jclpcb:C1206 */}
    <capacitor
      name="C27"
      capacitance="10uF"
      footprint={FP_jclpcb_C1206}
      pcbX="13.6mm"
      pcbY="-46.44mm"
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
    {/* D3 B5819W_C8598 - LCSC C8598 - B5819W - jclpcb:SOD-123_L2.7-W1.6-LS3.7-RD-1 */}
    <diode
      name="D3"
      footprint={FP_jclpcb_SOD_123_L2_7_W1_6_LS3_7_RD_1}
      pinLabels={{ pin1: "cathode", pin2: "anode" }}
      variant="schottky"
      pcbX="-0.06mm"
      pcbY="-59.01mm"
      supplierPartNumbers={{ jlcpcb: ["C8598"] }}
      manufacturerPartNumber="B5819W"
      cadModel={{
      objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C8598.obj?uuid=e9d505c99b6c436aaf827a29c5ba4f84",
      stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C8598.step?uuid=e9d505c99b6c436aaf827a29c5ba4f84",
      pcbRotationOffset: 0,
      modelOriginPosition: { x: 0, y: 0.000012699999999199463, z: -0.6 },
    }}
      connections={{
        "anode": "net.P12V",
        "cathode": "net.VCC",
      }} />
    {/* D4 B5819W_C8598 - LCSC C8598 - B5819W - jclpcb:SOD-123_L2.7-W1.6-LS3.7-RD-1 */}
    <diode
      name="D4"
      footprint={FP_jclpcb_SOD_123_L2_7_W1_6_LS3_7_RD_1}
      pinLabels={{ pin1: "cathode", pin2: "anode" }}
      variant="schottky"
      pcbX="6.44mm"
      pcbY="-59.01mm"
      supplierPartNumbers={{ jlcpcb: ["C8598"] }}
      manufacturerPartNumber="B5819W"
      cadModel={{
      objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C8598.obj?uuid=e9d505c99b6c436aaf827a29c5ba4f84",
      stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C8598.step?uuid=e9d505c99b6c436aaf827a29c5ba4f84",
      pcbRotationOffset: 0,
      modelOriginPosition: { x: 0, y: 0.000012699999999199463, z: -0.6 },
    }}
      connections={{
        "anode": "net.VEE",
        "cathode": "net.N12V",
      }} />
    {/* J2 Eurorack Power 16 Pin IDC - Eurorack_Power_16pin_Shrouded - LCSC C3406 - LL1007-1 (2*8 HITEMP) - 4ms_Connector:Pins_2x08_2.54mm_TH_EurorackPower_Shrouded */}
    <connector
      name="J2"
      footprint={FP_4ms_Connector_Pins_2x08_2_54mm_TH_EurorackPower_Shrouded_R90}
      pinCount={16}
      pinLabels={{
        "pin1": "12V",
        "pin2": "12V_2",
        "pin3": "GND",
        "pin4": "GND_4",
        "pin5": "GND_5",
        "pin6": "GND_6",
        "pin7": "GND_7",
        "pin8": "GND_8",
        "pin9": "12V_9",
        "pin10": "12V_10",
        "pin11": "5V",
        "pin12": "5V_12",
        "pin13": "CV",
        "pin14": "CV_14",
        "pin15": "GATE",
        "pin16": "GATE_16",
      }}
      schPinArrangement={{ leftSide: ["12V", "12V_2", "GND", "GND_4", "GND_5", "GND_6", "GND_7", "GND_8", "12V_9", "12V_10", "5V", "5V_12"], rightSide: ["CV", "CV_14", "GATE", "GATE_16"] }}
      pcbX="-18.5mm"
      pcbY="-56mm"
      layer="bottom"
      supplierPartNumbers={{ jlcpcb: ["C3406"] }}
      manufacturerPartNumber="LL1007-1 (2*8 HITEMP)"
      connections={{
        "12V": "net.N12V",
        "12V_2": "net.N12V",
        "GND": "net.GND",
        "GND_4": "net.GND",
        "GND_5": "net.GND",
        "GND_6": "net.GND",
        "GND_7": "net.GND",
        "GND_8": "net.GND",
        "12V_9": "net.P12V",
        "12V_10": "net.P12V",
      }} />
    {/* L1 2.2uH - MLZ1608A2R2WT000 - LCSC C76797 - MLZ1608A2R2WT000 - jclpcb:L0603 */}
    <inductor
      name="L1"
      inductance="2.2uH"
      footprint={FP_jclpcb_L0603}
      pcbX="-0.93mm"
      pcbY="-54.25mm"
      supplierPartNumbers={{ jlcpcb: ["C76797"] }}
      manufacturerPartNumber="MLZ1608A2R2WT000"
      cadModel={{
      objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C76797.obj?uuid=a7f84688c5004aa68dbcbddc99bd5473",
      stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C76797.step?uuid=a7f84688c5004aa68dbcbddc99bd5473",
      pcbRotationOffset: 0,
      modelOriginPosition: { x: 0.0023875999999063424, y: 0.015976600000044527, z: -0.25 },
    }}
      connections={{
        "pin1": "net.VCC",
        "pin2": "net.U16_VIN_P",
      }} />
    {/* L2 2.2uH - MLZ1608A2R2WT000 - LCSC C76797 - MLZ1608A2R2WT000 - jclpcb:L0603 */}
    <inductor
      name="L2"
      inductance="2.2uH"
      footprint={FP_jclpcb_L0603}
      pcbX="25.75mm"
      pcbY="-54.25mm"
      supplierPartNumbers={{ jlcpcb: ["C76797"] }}
      manufacturerPartNumber="MLZ1608A2R2WT000"
      cadModel={{
      objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C76797.obj?uuid=a7f84688c5004aa68dbcbddc99bd5473",
      stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C76797.step?uuid=a7f84688c5004aa68dbcbddc99bd5473",
      pcbRotationOffset: 0,
      modelOriginPosition: { x: 0.0023875999999063424, y: 0.015976600000044527, z: -0.25 },
    }}
      connections={{
        "pin1": "net.VEE",
        "pin2": "net.U16_VIN_N",
      }} />
    {/* U16 B1205S-1WR3 - B1205S-1WR3_C5365649 - LCSC C3031175 - B2405S-1WR3 - jclpcb:PWRM-TH_4P-L11.5-W6.0-P2.54_B1503S */}
    <chip
      name="U16"
      footprint={FP_jclpcb_PWRM_TH_4P_L11_5_W6_0_P2_54_B1503S}
      pinLabels={B1205S_PINS}
      pinAttributes={{
        "VIN_N": { requiresPower: true },
        "VIN_P": { requiresPower: true },
        "VOUT_N": { providesPower: true, requiresGround: true },
        "VOUT_P": { providesPower: true },
      }}
      schPinArrangement={{ leftSide: ["VIN_N", "VIN_P"], rightSide: ["VOUT_N", "VOUT_P"] }}
      pcbX="3.39mm"
      pcbY="-50.78mm"
      layer="bottom"
      supplierPartNumbers={{ jlcpcb: ["C3031175"] }}
      manufacturerPartNumber="B2405S-1WR3"
      cadModel={{
      objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C3031175.obj?uuid=f1c020aaa5b0488180ed500910c4cc9d",
      stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C3031175.step?uuid=f1c020aaa5b0488180ed500910c4cc9d",
      pcbRotationOffset: 0,
      modelOriginPosition: { x: 0, y: -2.3189945999999964, z: -0.00000819999999990273 },
    }}
      connections={{
        "VIN_N": "net.U16_VIN_N",
        "VIN_P": "net.U16_VIN_P",
        "VOUT_N": "net.GND",
        "VOUT_P": "net.P5V",
      }} />
    {/* C32 1uF - EXTRA_C32 - LCSC C559769 - CC0603KRX7R9BB105 - U16 input bypass across VIN_P/VIN_N, YLPTEC B_S-1WR3 datasheet Table 1: 24 VDC input -> Cin 1uF/50V. C26 sits before L1/L2, so it is not the local post-inductor input cap (DESIGN-REVIEW R13) - jclpcb:C0603 */}
    <capacitor
      name="C32"
      capacitance="1uF"
      footprint={FP_jclpcb_C0603}
      pcbX="3.96mm"
      pcbY="-53.10mm"
      layer="bottom"
      supplierPartNumbers={{ jlcpcb: ["C559769"] }}
      manufacturerPartNumber="CC0603KRX7R9BB105"
      connections={{
        "pin1": "net.U16_VIN_P",
        "pin2": "net.U16_VIN_N",
      }} />
  </schematicsheet>
)

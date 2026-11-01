// Froemix base board - hand-maintained tscircuit source (see froemix/AGENTS.md).
// Ported once from the KiCad project in ../tmp by froemix/tools/kicad2tscircuit.py;
// that tool is retired as an editor, edit this file directly.

// MIDI sheet: MIDI TRS in/out (opto isolated)
// Source: tmp/midi.kicad_sch

import { TLP2362_PINS } from "../lib/pins"
import { FP_jclpcb_C0603, FP_jclpcb_HDR_TH_6P_P2_54_V_M_R2_C3_S2_54_1, FP_jclpcb_OPTO_SMD_5_L3_7_W4_6_P1_27_LS7_0_BR, FP_jclpcb_R0603, FP_jclpcb_R2512, FP_jclpcb_SOD_323_L1_8_W1_3_LS2_5_RD, FP_jclpcb_SOT_23_3_L2_9_W1_6_P1_90_LS2_8_BR } from "../lib/footprints"

export const MidiSection = () => (
  <schematicsheet name="midi" displayName="MIDI" sheetIndex={1}>
    {/* C28 100nF - EXTRA_C28 - LCSC C14663 - CC0603KRX7R9BB104 - TLP2362 VCC bypass, datasheet-mandatory (added in tscircuit, mirror into KiCad) - jclpcb:C0603 */}
    <capacitor
      name="C28"
      capacitance="100nF"
      footprint={FP_jclpcb_C0603}
      pcbX="33.29mm"
      pcbY="-22.235mm"
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
    {/* D1 1N4148WS T4 - 1N4148WST4 - LCSC C2128 - jclpcb:SOD-323_L1.8-W1.3-LS2.5-RD
        antiparallel to the U15 LED: the MIDI IN reverse-protection diode (there is
        no forward diode across the LED - one would shunt it, DESIGN-REVIEW R01) */}
    <diode
      name="D1"
      footprint={FP_jclpcb_SOD_323_L1_8_W1_3_LS2_5_RD}
      pinLabels={{ pin1: "cathode", pin2: "anode" }}
      pcbX="24.02mm"
      pcbY="-26.76mm"
      layer="bottom"
      supplierPartNumbers={{ jlcpcb: ["C2128"] }}
      cadModel={{
      objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C2128.obj?uuid=7459fe65e23146c0a8d836e46a0add72",
      stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C2128.step?uuid=7459fe65e23146c0a8d836e46a0add72",
      pcbRotationOffset: 0,
      modelOriginPosition: { x: 0, y: 0.00011430000006384944, z: 0 },
    }}
      connections={{
        "anode": "net.NET_D1_A",
        "cathode": "net.NET_D1_K",
      }} />
    {/* J1 Header-Male-2.54_2x3 - LCSC C65114 - jclpcb:HDR-TH_6P-P2.54-V-M-R2-C3-S2.54-1 */}
    <connector
      name="J1"
      footprint={FP_jclpcb_HDR_TH_6P_P2_54_V_M_R2_C3_S2_54_1}
      pinCount={6}
      schPinArrangement={{ leftSide: [1, 3, 5], rightSide: [2, 4, 6] }}
      pcbX="26.16mm"
      pcbY="-15.89mm"
      supplierPartNumbers={{ jlcpcb: ["C65114"] }}
      connections={{
        "pin1": "net.NET_D1_A",
        "pin2": "net.NET_J1_2",
        "pin3": "net.J_MIDI_IN_RING",
        "pin4": "net.J_MIDI_IN_TIP",
        "pin5": "net.NET_J1_2",
        "pin6": "net.NET_D1_A",
      }} />
    {/* Q2 MMBT3904 - MMBT3904 - LCSC C181119 - jclpcb:SOT-23-3_L2.9-W1.6-P1.90-LS2.8-BR
        MIDI OUT open-collector driver (DESIGN-REVIEW R02): USART1_TX drives the
        base through R58 and the collector sinks J_MIDI_OUT_RING through R60, so
        a shorted cable can no longer overload the MCU pin. */}
    <transistor
      name="Q2"
      type="npn"
      footprint={FP_jclpcb_SOT_23_3_L2_9_W1_6_P1_90_LS2_8_BR}
      pinLabels={{ pin1: "base", pin2: "emitter", pin3: "collector" }}
      pcbX="-31mm"
      pcbY="41mm"
      layer="bottom"
      supplierPartNumbers={{ jlcpcb: ["C181119"] }}
      cadModel={{
      objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C181119.obj?uuid=cefd4596db214da394d9632b2b88f8f2",
      stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C181119.step?uuid=cefd4596db214da394d9632b2b88f8f2",
      pcbRotationOffset: 180,
      modelOriginPosition: { x: 0.000012699999956566899, y: 0, z: 0 },
    }}
      connections={{
        "base": "net.NET_Q2_B",
        "emitter": "net.GND",
        "collector": "net.NET_Q2_C",
      }} />
    {/* R47 220R - 0603WAF2200T5E - LCSC C22962 - jclpcb:R0603 */}
    <resistor
      name="R47"
      resistance="220"
      footprint={FP_jclpcb_R0603}
      pcbX="34.22mm"
      pcbY="-26.69mm"
      layer="bottom"
      supplierPartNumbers={{ jlcpcb: ["C22962"] }}
      cadModel={{
      objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C22962.obj?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C22962.step?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      pcbRotationOffset: 0,
      modelOriginPosition: { x: -0.004999999999999977, y: 0, z: -0.01 },
    }}
      connections={{
        "pin1": "net.NET_J1_2",
        "pin2": "net.NET_D1_K",
      }} />
    {/* R48 2.2K - 0603WAF3302T5E - LCSC C4190 - 0603WAF2201T5E - jclpcb:R0603 */}
    <resistor
      name="R48"
      resistance="2.2k"
      footprint={FP_jclpcb_R0603}
      pcbX="23.74mm"
      pcbY="-30.05mm"
      layer="bottom"
      supplierPartNumbers={{ jlcpcb: ["C4190"] }}
      manufacturerPartNumber="0603WAF2201T5E"
      cadModel={{
      objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C4190.obj?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C4190.step?uuid=6bd5cd867e9542ebae21caaf5d2d4c4d",
      pcbRotationOffset: 0,
      modelOriginPosition: { x: -0.004999999999999977, y: 0, z: -0.01 },
    }}
      connections={{
        "pin1": "net.USART1_RX",
        "pin2": "net.P3V3_D",
      }} />
    {/* R58 4K7 - 0603WAF4701T5E - LCSC C23162 - jclpcb:R0603 - MIDI OUT driver
        base resistor for Q2 */}
    <resistor
      name="R58"
      resistance="4.7k"
      footprint={FP_jclpcb_R0603}
      pcbX="-30.5mm"
      pcbY="36.5mm"
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
        "pin1": "net.USART1_TX",
        "pin2": "net.NET_Q2_B",
      }} />
    {/* R59 22R 1W - 25121WF220JT4E - LCSC C118189 - jclpcb:R2512 - MIDI OUT
        TIP current source: TRS Type A (tip = DIN pin 4 = current source), the
        MIDI Association default, so the front board wires the jack straight
        through. 22R still delivers ~4.5mA into a worst-case receiver: 3.3V
        -5% = 3.135V, minus Vf(max) 1.7V and Vce(sat) 0.2V leaves 1.235V over
        22R + 33R (+1%, 55.6R) + a 220R receiver = 275.6R -> 4.48mA. A grounded
        ring (a tip-to-ring cable short) puts 3.3^2/22 = 0.50W in the 1W part.
        This replaces
        the 100mW 0603 R49 (DESIGN-REVIEW R02). No EasyEDA 3D model - pad-only
        in 3D. */}
    <resistor
      name="R59"
      resistance="22"
      footprint={FP_jclpcb_R2512}
      pcbX="-33mm"
      pcbY="52mm"
      layer="bottom"
      supplierPartNumbers={{ jlcpcb: ["C118189"] }}
      manufacturerPartNumber="25121WF220JT4E"
      connections={{
        "pin1": "net.J_MIDI_OUT_TIP",
        "pin2": "net.P3V3_D",
      }} />
    {/* R60 33R 1W - 25121WF330JT4E - LCSC C53728 - jclpcb:R2512 - MIDI OUT
        RING current sink (Type A), fed from Q2's open collector instead of
        straight from the MCU pin (DESIGN-REVIEW R02). Keep 33R: a ring pulled
        to 3.3V (the tip of a Type-A source shorted to our ring by a mis-wired
        cable) drives (3.3 - 0.2)/33 = 94mA through Q2, inside the MMBT3904's
        200mA rating; 10R would be ~300mA and is not an option. A grounded
        ring dissipates 3.3^2/33 = 330mW in the 1W part. Pad-only in 3D. */}
    <resistor
      name="R60"
      resistance="33"
      footprint={FP_jclpcb_R2512}
      pcbX="-33mm"
      pcbY="46mm"
      layer="bottom"
      supplierPartNumbers={{ jlcpcb: ["C53728"] }}
      manufacturerPartNumber="25121WF330JT4E"
      connections={{
        "pin1": "net.J_MIDI_OUT_RING",
        "pin2": "net.NET_Q2_C",
      }} />
    {/* U15 TLP2362 - TLP2362(TPL,E(T - LCSC C37658 - jclpcb:OPTO-SMD-5_L3.7-W4.6-P1.27-LS7.0-BR */}
    <chip
      name="U15"
      footprint={FP_jclpcb_OPTO_SMD_5_L3_7_W4_6_P1_27_LS7_0_BR}
      pinLabels={TLP2362_PINS}
      pinAttributes={{
        "GND": { requiresPower: true, requiresGround: true },
        "VO": { requiresPower: true },
        "VCC": { requiresPower: true },
      }}
      schPinArrangement={{ leftSide: ["ANODE", "GND", "VO", "VCC"], rightSide: ["CATHODE"] }}
      pcbX="26.52mm"
      pcbY="-22.23mm"
      layer="bottom"
      supplierPartNumbers={{ jlcpcb: ["C37658"] }}
      cadModel={{
      objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C37658.obj?uuid=2f86d20676d041f7afcf733787651bd9",
      stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C37658.step?uuid=2f86d20676d041f7afcf733787651bd9",
      pcbRotationOffset: 0,
      modelOriginPosition: { x: -0.000025400000367881148, y: -0.0001142999999501626, z: 0 },
    }}
      connections={{
        "ANODE": "net.NET_D1_K",
        "CATHODE": "net.NET_D1_A",
        "GND": "net.GND",
        "VO": "net.USART1_RX",
        "VCC": "net.P3V3_D",
      }} />
  </schematicsheet>
)

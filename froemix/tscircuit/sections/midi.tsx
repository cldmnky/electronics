// Froemix base board - hand-maintained tscircuit source (see froemix/AGENTS.md).
// Ported once from the KiCad project in ../tmp by froemix/tools/kicad2tscircuit.py;
// that tool is retired as an editor, edit this file directly.

// MIDI sheet: MIDI TRS in/out (opto isolated)
// Source: tmp/midi.kicad_sch
//
// Two functional blocks, each its own schematicsection so the rendered sheet
// separates the receiver from the transmitter:
//   midi-in  - U15 TLP2361 opto receiver, R47 220R LED resistor, D1 reverse
//              clamp, R48 pull-up, C28 bypass, J1 Type A/B shunt block.
//   midi-out - Q2 AO3401A P-channel MOSFET high-side switch, R58 gate resistor,
//              R61 gate pull-up, R59 ring/source 33R and R60 tip/sink 22R 1W.
// The numbers behind every value are in doc/ELECTRICAL-VERIFICATION.md; the
// review items are doc/DESIGN-REVIEW.md R01/R02 (third round).

import { AO3401A_PINS, TLP2361_PINS } from "../lib/pins"
import { FP_jclpcb_C0603, FP_jclpcb_HDR_TH_6P_P2_54_V_M_R2_C3_S2_54_1, FP_jclpcb_OPTO_SMD_5_L3_7_W4_6_P1_27_LS7_0_BR, FP_jclpcb_R0603, FP_jclpcb_R2512, FP_jclpcb_SOD_323_L1_8_W1_3_LS2_5_RD, FP_jclpcb_SOT_23_3_L2_9_W1_6_P1_90_LS2_8_BR } from "../lib/footprints"

export const MidiSection = () => (
  <schematicsheet name="midi" displayName="MIDI" sheetIndex={1}>
    {/* ---- MIDI IN: opto receiver (current loop off = UART idle high) ---- */}
    {/* C28 100nF - EXTRA_C28 - LCSC C14663 - CC0603KRX7R9BB104 - U15 VCC bypass.
        TLP2361 datasheet section 8, note 2: a 0.1uF ceramic between pin 6 and
        pin 4 within 1cm is mandatory, otherwise the high-gain receiver may not
        switch. Routed rail leg 5.46mm, GND leg 3.45mm. jclpcb:C0603 */}
    <capacitor
      name="C28"
      schSectionName="midi-in"
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
      schSectionName="midi-in"
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
    {/* J1 Header-Male-2.54_2x3 - LCSC C65114 - jclpcb:HDR-TH-6P-P2.54-V-M-R2-C3-S2.54-1
        Type A/B shunt block, two shunts (not board parts): pads 1/6 are the U15
        cathode node, pads 2/5 reach the anode through R47, pad 3 is the ring and
        pad 4 the tip. Type A = shunts 3-5 (ring to anode) + 4-6 (tip to cathode);
        Type B = 4-2 + 3-1. doc/ASSEMBLY.md, DESIGN-REVIEW R05. */}
    <connector
      name="J1"
      schSectionName="midi-in"
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
    {/* R47 220R - 0603WAF2200T5E - LCSC C22962 - jclpcb:R0603
        LED series resistor. 220R is the CA-033 receiver value; with the 33R+22R
        source legs and 1% parts the loop current is 4.3..5.3mA, i.e. 2.7x..3.3x
        the TLP2361 1.6mA maximum threshold at 125C. */}
    <resistor
      name="R47"
      schSectionName="midi-in"
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
    {/* R48 2.2K - 0603WAF2201T5E - LCSC C4190 - jclpcb:R0603
        USART1_RX pull-up. U15 is now a TLP2361 with a TOTEM-POLE output (the
        TLP2362 was open collector), so the pull-up is no longer required to
        define the idle level - it is kept as the fail-safe high level if U15 is
        not fitted and costs 1.5mA when VO is low (within the 3.2mA VOL rating).
        The MCU pin sees VOH >= 3.2V (IO = -20uA) and VOL <= 0.4V. */}
    <resistor
      name="R48"
      schSectionName="midi-in"
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
    {/* U15 TLP2361(TPL,E - LCSC C107626 - jclpcb:OPTO-SMD-5_L3.7-W4.6-P1.27-LS7.0-BR
        Replaces the TLP2362 (C37658) - DESIGN-REVIEW R02 (third round). Both are
        Toshiba SO6 11-4L1S with the identical pin map, so the land pattern, pad
        numbers and every net stay exactly as they were. Why the change:
        TLP2362 IFHL max 5.0mA with a 7.5mA recommended on-current, i.e. a
        receiver outside CA-033's "must turn on below 5mA"; a 3.3V Type A
        transmitter delivers only 4.3..5.3mA. TLP2361 IFHL max is 1.3mA
        (-40..105C) and 1.6mA (-40..125C) with a 2..6mA recommended on-current,
        so the same loop drives it with >=2.7x margin.
        Logic: TLP2361 is an INVERTER with a totem-pole output (input high = LED
        on = output low). The loop is off at UART idle, so the LED is off and VO
        is high: no firmware inversion, same as before.
        The C37658 EasyEDA 3D model was tied to the old part; no verified model
        for C107626 is available, so U15 is pad-only in 3D (AGENTS.md section 6). */}
    <chip
      name="U15"
      schSectionName="midi-in"
      footprint={FP_jclpcb_OPTO_SMD_5_L3_7_W4_6_P1_27_LS7_0_BR}
      pinLabels={TLP2361_PINS}
      pinAttributes={{
        "GND": { requiresPower: true, requiresGround: true },
        "VO": { requiresPower: true },
        "VCC": { requiresPower: true },
      }}
      schPinArrangement={{ leftSide: ["ANODE", "GND", "VO", "VCC"], rightSide: ["CATHODE"] }}
      pcbX="26.52mm"
      pcbY="-22.23mm"
      layer="bottom"
      supplierPartNumbers={{ jlcpcb: ["C107626"] }}
      manufacturerPartNumber="TLP2361(TPL,E"
      connections={{
        "ANODE": "net.NET_D1_K",
        "CATHODE": "net.NET_D1_A",
        "GND": "net.GND",
        "VO": "net.USART1_RX",
        "VCC": "net.P3V3_D",
      }} />

    {/* ---- MIDI OUT: Type A transmitter (ring = source, tip = sink) ---- */}
    {/* Q2 AO3401A - LCSC C15127 - jclpcb:SOT-23-3_L2.9-W1.6-P1.90-LS2.8-BR
        Element: <transistor type="mosfet"> with an explicit P-channel MOSFET
        symbol, NOT <mosfet>. Reasons, both verified in the installed core
        (@tscircuit/props 0.0.3044 / @tscircuit/core):
          * mosfetProps has no pinLabels field, so a pinLabels prop on <mosfet>
            is stripped by the zod schema and the element hard-wires the
            symbol order pin1=drain, pin2=source, pin3=gate;
          * transistorProps does accept pinLabels, so the SOT-23 pinout
            1=gate, 2=source, 3=drain (AO3401A) lands on the right pads;
          * symbolName draws the real p_channel_e_mosfet_transistor symbol.
        High-side switch, P-channel enhancement MOSFET: source (pin 2) on
        P3V3_D, drain (pin 3) into R59 to the ring, gate (pin 1) from USART1_TX
        through R58 and pulled up by R61. It replaces the MMBT3906 PNP
        (C53444) - DESIGN-REVIEW R02 (third round): the legacy PNP dissipated
        ~3.437mA x Vce in the loop and its 200mA/0.35W SOT-23 case was margin-
        thin on a grounded-ring fault; the AO3401A is specified -4.0A (-3.2A at
        70C) with RDS(on) <= 85mOhm at VGS = -2.5V (<= 60mOhm at -4.5V, 75mOhm
        at 125C for the -10V spec), so the same 100mA fault drops 0.13V.
        Polarity is unchanged and still non-inverting: idle/high-Z leaves the
        gate at the source (R61, 2.2k) so the FET is OFF and the loop is open;
        the start bit pulls the gate low, VGS = -3.3V, and the FET conducts.
        Body diode: a mis-plugged second transmitter can feed the ring above
        3.3V; the diode then conducts into P3V3_D, limited by that source's
        220R to a few mA, and VDS stays far inside -30V.
        No 3D model in lib/lcsc_models.json for C15127: pad-only in 3D. */}
    <transistor
      name="Q2"
      schSectionName="midi-out"
      type="mosfet"
      symbolName="p_channel_e_mosfet_transistor_gate_left_drain_bottom"
      footprint={FP_jclpcb_SOT_23_3_L2_9_W1_6_P1_90_LS2_8_BR}
      pinLabels={AO3401A_PINS}
      pcbX="-31mm"
      pcbY="41mm"
      layer="bottom"
      supplierPartNumbers={{ jlcpcb: ["C15127"] }}
      manufacturerPartNumber="AO3401A"
      connections={{
        "gate": "net.NET_Q2_B",
        "source": "net.P3V3_D",
        "drain": "net.NET_Q2_C",
      }} />
    {/* R58 180R - 0603WAF1800T5E - LCSC C22828 - jclpcb:R0603
        Gate resistor, sized by the MCU pin, not by a base current: the gate is
        645pF of Ciss and needs 7nC at VGS = -4.5V.
        Peak: the gate starts at the source and the pin is pulled to its low
        level, so the worst-case bound is 3.465V / (180R - 1%) = 19.44mA, i.e.
        below the STM32H750 per-pin absolute maximum of +/-20mA (BOUND, see
        doc/ELECTRICAL-VERIFICATION.md section 3: the ST data sheet could not
        be downloaded from this host, so 20mA is a stated assumption and the
        earlier 100R/34.6mA value could NOT be shown compliant under either
        the 20mA or the 25mA family limit - that is why it changed).
        ON: with the pin at VOL max 0.4V the divider R58/R61 puts the gate at
        0.607V, so VGS = -(3.135 - 0.607) = -2.528V and the -2.5V RDS(on) spec
        (85mOhm max) is valid.
        OFF (pin high): VOH min = VDD - 0.4V, gate 2.764V, |VGS| = 0.371V, below
        the 0.5V minimum threshold - the FET is off, and the only leakage
        rating (VGS = 0) is 1uA at 25C / 5uA at 125C.
        Charge edge: 180R x 645pF = 116ns, far shorter than one 32us bit. */}
    <resistor
      name="R58"
      schSectionName="midi-out"
      resistance="180"
      footprint={FP_jclpcb_R0603}
      pcbX="-30.5mm"
      pcbY="36.5mm"
      layer="bottom"
      supplierPartNumbers={{ jlcpcb: ["C22828"] }}
      manufacturerPartNumber="0603WAF1800T5E"
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
    {/* R59 33R 1W 1% - 25121WF330JT4E - LCSC C53728 - jclpcb:R2512
        RING current source (TRS Type A: ring = DIN pin 4 = source, tip = DIN
        pin 5 = sink). 33R is CA-033's 3.3V RA value. It is the resistor in the
        MOSFET path, so the grounded-ring fault is 3.3V/33R = 100mA: 0.33W in
        this 1W part at the zero-RDS(on) bound, and the FET itself only sees
        (0.1A)^2 x 0.13R = 1.3mW at the RDS(on) maximum.
        The 1W 2512 rating holds to 70C ambient and derates above it
        (Uniroyal data sheet section 9), the F code is +/-1%. */}
    <resistor
      name="R59"
      schSectionName="midi-out"
      resistance="33"
      footprint={FP_jclpcb_R2512}
      pcbX="-33mm"
      pcbY="52mm"
      layer="bottom"
      supplierPartNumbers={{ jlcpcb: ["C53728"] }}
      manufacturerPartNumber="25121WF330JT4E"
      connections={{
        "pin1": "net.J_MIDI_OUT_RING",
        "pin2": "net.NET_Q2_C",
      }} />
    {/* R60 22R 1W 1% - 25121WF220JT4E - LCSC C118189 - jclpcb:R2512
        TIP current sink straight to GND (Type A), so there is NO transistor in
        this leg: any fault power is the resistor's own. 22R (not CA-033's 10R)
        is the fault-driven value - a tip forced to +3.3V dissipates
        3.3^2/22 = 0.495W, half of the 1W rating, where 10R would be 1.09W and
        fail. The cost is (RA+RC) = 55R instead of CA-033's 43R: 4.3mA worst
        case into a 1.9V/220R receiver instead of 4.47mA.
        Pad-only in 3D. */}
    <resistor
      name="R60"
      schSectionName="midi-out"
      resistance="22"
      footprint={FP_jclpcb_R2512}
      pcbX="-33mm"
      pcbY="46mm"
      layer="bottom"
      supplierPartNumbers={{ jlcpcb: ["C118189"] }}
      manufacturerPartNumber="25121WF220JT4E"
      connections={{
        "pin1": "net.J_MIDI_OUT_TIP",
        "pin2": "net.GND",
      }} />
    {/* R61 2.2K - 0603WAF2201T5E - LCSC C4190 - jclpcb:R0603
        Gate pull-up. 2.2k (was 10k for the PNP base) holds the gate at the
        source while USART1_TX is high-Z (reset, bootloader, pin not yet
        configured), so a floating UART cannot turn the MIDI OUT source on at
        idle. 2.2k also keeps the off state defined against the FET's 55pF Crss
        and 80pF Coss instead of a 10k node, and sinks 1.5mA while the pin is
        driven low - inside the MCU pin's DC budget. */}
    <resistor
      name="R61"
      schSectionName="midi-out"
      resistance="2.2k"
      footprint={FP_jclpcb_R0603}
      pcbX="-30.5mm"
      pcbY="33.5mm"
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
        "pin1": "net.NET_Q2_B",
        "pin2": "net.P3V3_D",
      }} />

    {/* Sheet notes: fixed positions in the free band above the auto-laid-out
        components (y 11.1 down, x -15.6 left aligned). tools/check_schematic.py
        asserts every note stays clear of the component and net-label boxes. */}
    <schematictext schX={-15.6} schY={11.10} anchor="center_left" fontSize={0.17}
      text="MIDI sheet: 31.25kbaud current loop, 3.3V signaling per MIDI CA-033. Logical 0 = current ON = start bit." />
    <schematictext schX={-15.6} schY={10.65} anchor="center_left" fontSize={0.17}
      text="MIDI IN: J_MIDI_IN_RING -> J1 shunts -> R47 220R -> U15 LED (pin 1/3) -> J_MIDI_IN_TIP. D1 is the reverse clamp across the LED." />
    <schematictext schX={-15.6} schY={10.20} anchor="center_left" fontSize={0.17}
      text="J1 shunts: Type A = 3-5 + 4-6 (ring to anode, tip to cathode); Type B = 4-2 + 3-1. Two 2.54mm shunts, not board parts." />
    <schematictext schX={-15.6} schY={9.75} anchor="center_left" fontSize={0.17}
      text="U15 TLP2361 (C107626): IFHL 1.6mA max at 125C, IF recommended 2..6mA, inverting totem-pole output (VO low = LED on). C28 100nF pins 6/4." />
    <schematictext schX={-15.6} schY={9.30} anchor="center_left" fontSize={0.17}
      text="R48 2.2k2 is a fail-safe pull-up on USART1_RX (1.5mA when VO is low, inside the 3.2mA VOL rating)." />
    <schematictext schX={-15.6} schY={8.85} anchor="center_left" fontSize={0.17}
      text="MIDI OUT: ring (DIN 4) is the source through Q2 + R59 33R 1W; tip (DIN 5) is the sink through R60 22R 1W to GND. Never cross tip/ring." />
    <schematictext schX={-15.6} schY={8.40} anchor="center_left" fontSize={0.17}
      text="Q2 AO3401A P-FET (C15127): source on P3V3_D, OFF at idle and high-Z (R61 2.2k holds the gate at the source), ON for the start bit." />
    <schematictext schX={-15.6} schY={7.95} anchor="center_left" fontSize={0.17}
      text="Gate network: R58 180R from USART1_TX, peak bound 19.44mA, VGS(on) -2.528V worst case, RDS(on) <= 85mR at VGS = -2.5V, 1.5mA DC through R61." />
    <schematictext schX={-15.6} schY={7.50} anchor="center_left" fontSize={0.17}
      text="Faults with the driver on: ring to GND = 100mA and 0.33W in R59; tip forced to +3.3V = 0.50W in R60 (no transistor in the tip leg)." />
    <schematictext schX={-15.6} schY={7.05} anchor="center_left" fontSize={0.17}
      text="Loop current into a 1.9V + 220R receiver: 4.31mA worst case (3.135V, 1% parts, RDS(on) 0.13R), 5.3mA typical. Limits: doc/ELECTRICAL-VERIFICATION.md." />
  </schematicsheet>
)

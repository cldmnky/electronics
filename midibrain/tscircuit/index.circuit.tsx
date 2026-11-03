import { Fragment } from "react"
import { B5819W_SL } from "./imports/B5819W_SL"
import { SN74LVC1G126DBVR } from "./imports/SN74LVC1G126DBVR"
import { FRONT_REAR_MOUNT, PI_HOLES, REAR_PCB } from "./lib/layout"

// REAR/BASE BOARD, 20HP x 112 mm. Prototype schematic/placement; not a
// fabrication release. The Pi 5 is mounted on the rear side using its 58 x 49
// hole grid; the Pico is on the left, the front interconnect on the right, and
// the Pi ribbon header at the top. All parts are on the rear side.

type RProps = { name: string; value: string; x: string; y: string; lcsc: string; mpn: string; p1: string; p2: string; rot?: string }
const R = ({ name, value, x, y, lcsc, mpn, p1, p2, rot }: RProps) => (
  <resistor
    name={name}
    resistance={value}
    footprint="0603"
    pcbX={x}
    pcbY={y}
    pcbRotation={rot}
    layer="bottom"
    supplierPartNumbers={{ jlcpcb: [lcsc] }}
    manufacturerPartNumber={mpn}
    connections={{ pin1: p1, pin2: p2 }}
  />
)
type CProps = { name: string; value: string; x: string; y: string; lcsc: string; mpn: string; p1: string; p2: string; footprint?: string; rot?: string }
const C = ({ name, value, x, y, lcsc, mpn, p1, p2, footprint = "0603", rot }: CProps) => (
  <capacitor
    name={name}
    capacitance={value}
    footprint={footprint}
    pcbX={x}
    pcbY={y}
    pcbRotation={rot}
    layer="bottom"
    supplierPartNumbers={{ jlcpcb: [lcsc] }}
    manufacturerPartNumber={mpn}
    connections={{ pin1: p1, pin2: p2 }}
  />
)

const picoLeft = {
  pin1: "net.MIDI_OUT_1_TX", pin2: "net.MIDI_IN_1_RX", pin3: "net.GND", pin4: "net.ENC_SW",
  pin5: "net.PLAY_START", pin6: "net.MIDI_OUT_2_TX", pin7: "net.MIDI_IN_2_RX", pin8: "net.GND",
  pin9: "net.NAV_UP", pin10: "net.NAV_DOWN", pin11: "net.NAV_LEFT", pin12: "net.NAV_RIGHT",
  pin13: "net.GND", pin14: "net.NAV_PRESS", pin15: "net.ENC_A", pin16: "net.ENC_B",
  pin17: "net.REC", pin18: "net.GND", pin19: "net.OVER_DUB", pin20: "net.STOP",
} as const

const picoRight = {
  pin1: "net.SPI_MOSI_PICO", pin2: "net.SPI_CS_PICO", pin3: "net.GND", pin4: "net.SPI_SCLK_PICO",
  pin5: "net.SPI_MISO_PICO", pin6: "net.READY_PICO", pin7: "net.PLAY", pin8: "net.GND",
  pin9: "net.PLAY_LED", pin10: "net.RUN", pin11: "net.REC_LED", pin12: "net.OVERDUB_LED",
  pin13: "net.GND", pin14: "net.SHIFT_EXIT", pin16: "net.P3V3", pin18: "net.GND", pin19: "net.VSYS",
} as const

export default () => (
  <board width={`${REAR_PCB.width}mm`} height={`${REAR_PCB.height}mm`} thickness={`${REAR_PCB.thickness}mm`} layers={2}>
    {/* Pi 5 mounting holes: 58 x 49 grid, portrait orientation. */}
    {PI_HOLES.map((h, i) => (
      <Fragment key={`pi-hole-${i}`}><hole name={`PI_H${i + 1}`} diameter="2.7mm" pcbX={`${h.x}mm`} pcbY={`${h.y}mm`} /></Fragment>
    ))}
    {/* Front-board spacer grid (shared XY, see lib/layout.ts FRONT_REAR_MOUNT). */}
    {FRONT_REAR_MOUNT.map((h, i) => (
      <Fragment key={`mount-${i}`}><hole name={`H${i + 1}`} diameter="2.7mm" pcbX={`${h.x}mm`} pcbY={`${h.y}mm`} /></Fragment>
    ))}

    {/* Pico sockets on the left margin; 51 mm module length vertical. */}
    <connector
      name="J_PICO_L"
      pinCount={20}
      footprint="pinrow20_p2.54mm"
      manufacturerPartNumber="PPTC201LFBN-RC"
      pcbX="-48.39mm"
      pcbY="0mm"
      pcbRotation="270deg"
      layer="bottom"
      connections={picoLeft}
    />
    <connector
      name="J_PICO_R"
      pinCount={20}
      footprint="pinrow20_p2.54mm"
      manufacturerPartNumber="PPTC201LFBN-RC"
      pcbX="-30.61mm"
      pcbY="0mm"
      pcbRotation="90deg"
      layer="bottom"
      connections={picoRight}
    />

    {/* Pi 40-pin ribbon across the top edge. */}
    <connector
      name="J_PI"
      pinCount={40}
      footprint="pinrow40_p2.54mm_rows2"
      manufacturerPartNumber="PEC20DAAN"
      pcbX="0mm"
      pcbY="48mm"
      layer="bottom"
      connections={{
        pin1: "net.PI_3V3", pin2: "net.P5V", pin4: "net.P5V", pin6: "net.GND",
        pin9: "net.GND", pin14: "net.GND", pin18: "net.READY_PI", pin19: "net.SPI_MOSI_PI",
        pin20: "net.GND", pin21: "net.SPI_MISO_PI", pin22: "net.RESET_PI", pin23: "net.SPI_SCLK_PI",
        pin24: "net.SPI_CS_PI", pin25: "net.GND", pin30: "net.GND", pin34: "net.GND", pin39: "net.GND",
      }}
    />

    {/* Front-board interconnect on the right edge. */}
    <connector
      name="J_FRONT"
      pinCount={26}
      footprint="pinrow26_p2.54mm_rows2"
      manufacturerPartNumber="PEC13DAAN"
      pcbX="45mm"
      pcbY="0mm"
      pcbRotation="90deg"
      layer="bottom"
      connections={{
        pin1: "net.P3V3", pin2: "net.GND", pin3: "net.MIDI_IN_1_RX", pin4: "net.MIDI_OUT_1_TX",
        pin5: "net.MIDI_IN_2_RX", pin6: "net.MIDI_OUT_2_TX", pin7: "net.NAV_UP", pin8: "net.NAV_DOWN",
        pin9: "net.NAV_LEFT", pin10: "net.NAV_RIGHT", pin11: "net.NAV_PRESS", pin12: "net.ENC_A",
        pin13: "net.ENC_B", pin14: "net.ENC_SW", pin15: "net.REC", pin16: "net.OVER_DUB",
        pin17: "net.STOP", pin18: "net.PLAY", pin19: "net.PLAY_START", pin20: "net.REC_LED",
        pin21: "net.OVERDUB_LED", pin22: "net.P3V3", pin23: "net.PLAY_LED", pin24: "net.GND", pin25: "net.GND", pin26: "net.SHIFT_EXIT",
      }}
    />

    <SN74LVC1G126DBVR
      name="U3"
      pcbX="38mm"
      pcbY="-8mm"
      layer="bottom"
      connections={{ A: "net.SPI_MISO_PICO", Y: "net.SPI_MISO_PI", OE: "net.OE", VCC: "net.P3V3", GND: "net.GND" }}
    />
    <SN74LVC1G126DBVR
      name="U4"
      pcbX="38mm"
      pcbY="-3mm"
      layer="bottom"
      connections={{ A: "net.READY_PICO", Y: "net.READY_PI", OE: "net.OE", VCC: "net.P3V3", GND: "net.GND" }}
    />

    <R name="R13" value="100" x="33mm" y="-16mm" lcsc="C22775" mpn="0603WAF1000T5E" p1="net.SPI_MOSI_PI" p2="net.SPI_MOSI_PICO" />
    <R name="R14" value="100" x="33mm" y="-13mm" lcsc="C22775" mpn="0603WAF1000T5E" p1="net.SPI_SCLK_PI" p2="net.SPI_SCLK_PICO" />
    <R name="R15" value="100" x="33mm" y="-10mm" lcsc="C22775" mpn="0603WAF1000T5E" p1="net.SPI_CS_PI" p2="net.SPI_CS_PICO" />
    <R name="R19" value="100" x="38mm" y="-20mm" rot="180deg" lcsc="C22775" mpn="0603WAF1000T5E" p1="net.PI_3V3" p2="net.OE" />
    <R name="R20" value="100k" x="38mm" y="-17mm" lcsc="C25803" mpn="0603WAF1003T5E" p1="net.OE" p2="net.GND" />

    <mosfet
      name="Q3"
      channelType="n"
      mosfetMode="enhancement"
      footprint="sot23"
      pcbX="36mm"
      pcbY="8mm"
      layer="bottom"
      supplierPartNumbers={{ jlcpcb: ["C8545"] }}
      manufacturerPartNumber="2N7002"
      connections={{ gate: "net.RESET_GATE", source: "net.GND", drain: "net.RUN" }}
    />
    <R name="R16" value="10k" x="32mm" y="6mm" rot="180deg" lcsc="C25804" mpn="0603WAF1002T5E" p1="net.RESET_PI" p2="net.RESET_GATE" />
    <R name="R17" value="100k" x="32mm" y="10mm" lcsc="C25803" mpn="0603WAF1003T5E" p1="net.RESET_GATE" p2="net.GND" />
    <R name="R18" value="10k" x="32mm" y="4mm" lcsc="C25804" mpn="0603WAF1002T5E" p1="net.RUN" p2="net.P3V3" />

    <B5819W_SL name="D3" pcbX="-45mm" pcbY="35mm" layer="bottom" connections={{ anode: "net.P5V", cathode: "net.VSYS" }} />
    <C name="C7" value="10uF" x="-40mm" y="35mm" lcsc="C13585" mpn="CL31A106KBHNNNE" footprint="1206" p1="net.P5V" p2="net.GND" />
    <C name="C8" value="100nF" x="-35mm" y="35mm" lcsc="C14663" mpn="CC0603KRX7R9BB104" p1="net.P5V" p2="net.GND" />
    <C name="C9" value="10uF" x="-45mm" y="30mm" lcsc="C13585" mpn="CL31A106KBHNNNE" footprint="1206" p1="net.VSYS" p2="net.GND" />
    <C name="C10" value="100nF" x="-40mm" y="30mm" rot="180deg" lcsc="C14663" mpn="CC0603KRX7R9BB104" p1="net.VSYS" p2="net.GND" />
    <C name="C11" value="10uF" x="-35mm" y="30mm" lcsc="C13585" mpn="CL31A106KBHNNNE" footprint="1206" p1="net.P3V3" p2="net.GND" />
    <C name="C12" value="100nF" x="-30mm" y="30mm" lcsc="C14663" mpn="CC0603KRX7R9BB104" p1="net.P3V3" p2="net.GND" />
  </board>
)

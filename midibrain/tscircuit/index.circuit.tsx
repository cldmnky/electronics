import { Fragment } from "react"
import { B5819W_SL } from "./imports/B5819W_SL"
import { BH254V_26P } from "./imports/BH254V_26P"
import { BH254V_40P } from "./imports/BH254V_40P"
import { C8545_2N7002 } from "./imports/C8545_2N7002"
import { SN74LVC1G126DBVR } from "./imports/SN74LVC1G126DBVR"
import { BOARD_POLICY } from "./lib/electrical"
import { FRONT_REAR_MOUNT, PI_HOLES, REAR_PCB } from "./lib/layout"
import { PROBE_FOOTPRINT, PROBE_HOLE_DIAMETER_MM, PROBE_PAD_DIAMETER_MM, PROBE_POINTS } from "./lib/probe-points"
import { RearSilk } from "./lib/silk"

// REAR/BASE BOARD, 20HP x 112 mm. Prototype schematic/placement; not a
// fabrication release. The Pi 5 is mounted on the rear side using its 58 x 49
// hole grid; the Pico is on the left, the front interconnect on the right, and
// the Pi ribbon header at the top. All parts are on the rear side.

type RProps = { name: string; value: string; x: string; y: string; lcsc: string; mpn: string; p1: string; p2: string; rot?: string; sec?: string }
const R = ({ name, value, x, y, lcsc, mpn, p1, p2, rot, sec = "isolation" }: RProps) => (
  <resistor
    name={name}
    resistance={value}
    footprint="0603"
    pcbX={x}
    pcbY={y}
    pcbRotation={rot}
    layer="bottom"
    schSheetName="rear"
    schSectionName={sec}
    supplierPartNumbers={{ jlcpcb: [lcsc] }}
    manufacturerPartNumber={mpn}
    connections={{ pin1: p1, pin2: p2 }}
  />
)
type CProps = { name: string; value: string; x: string; y: string; lcsc: string; mpn: string; p1: string; p2: string; footprint?: string; rot?: string; sec?: string }
const C = ({ name, value, x, y, lcsc, mpn, p1, p2, footprint = "0603", rot, sec = "power" }: CProps) => (
  <capacitor
    name={name}
    capacitance={value}
    footprint={footprint}
    pcbX={x}
    pcbY={y}
    pcbRotation={rot}
    layer="bottom"
    schSheetName="rear"
    schSectionName={sec}
    supplierPartNumbers={{ jlcpcb: [lcsc] }}
    manufacturerPartNumber={mpn}
    connections={{ pin1: p1, pin2: p2 }}
  />
)

// Pico module physical pins 1-20 (left row of the module) and 21-40 (right
// row). Each socket is a single-row 1x20 header, so its own perimeter
// numbering is the Pico numbering.
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

// Pi 40-pin physical position -> net. Physical positions are the Raspberry Pi
// header numbering: pin 1 at one end, odd pins on one row, even pins on the
// other, pairs (1,2), (3,4), ... The BH254V-40P footprint numbers its pads the
// same way, so this table is also the pad numbering.
const piHeader = {
  pin1: "net.PI_3V3", pin2: "net.P5V", pin4: "net.P5V", pin6: "net.GND",
  pin9: "net.GND", pin14: "net.GND", pin18: "net.READY_PI", pin19: "net.SPI_MOSI_PI",
  pin20: "net.GND", pin21: "net.SPI_MISO_PI", pin22: "net.RESET_PI", pin23: "net.SPI_SCLK_PI",
  pin24: "net.SPI_CS_PI", pin25: "net.GND", pin30: "net.GND", pin34: "net.GND", pin39: "net.GND",
} as const

// Raspberry Pi 40-pin positions this module does not use. Kept as one named
// declaration next to the used-pin table above, so "spare" is a stated design
// fact and not an omission: every pin here is a Pi-side ground, power rail or
// spare GPIO that this board deliberately leaves open.
const SPARE_PI_PINS = Object.fromEntries(
  [3, 5, 7, 8, 10, 11, 12, 13, 15, 16, 17, 26, 27, 28, 29, 31, 32, 33, 35, 36, 37, 38, 40]
    .map(pin => [`pin${pin}`, { doNotConnect: true }]),
) as Record<string, { doNotConnect: true }>

// Front-board interconnect, 2 x 13. Both connectors (J_FRONT here and J5 on
// the front board) use this table, so a straight 2x13 ribbon is pin-for-pin.
const frontInterconnect = {
  pin1: "net.P3V3", pin2: "net.GND", pin3: "net.MIDI_IN_1_RX", pin4: "net.MIDI_OUT_1_TX",
  pin5: "net.MIDI_IN_2_RX", pin6: "net.MIDI_OUT_2_TX", pin7: "net.NAV_UP", pin8: "net.NAV_DOWN",
  pin9: "net.NAV_LEFT", pin10: "net.NAV_RIGHT", pin11: "net.NAV_PRESS", pin12: "net.ENC_A",
  pin13: "net.ENC_B", pin14: "net.ENC_SW", pin15: "net.REC", pin16: "net.OVER_DUB",
  pin17: "net.STOP", pin18: "net.PLAY", pin19: "net.PLAY_START", pin20: "net.REC_LED",
  pin21: "net.OVERDUB_LED", pin22: "net.P3V3", pin23: "net.PLAY_LED", pin24: "net.GND",
  pin25: "net.GND", pin26: "net.SHIFT_EXIT",
} as const

export default () => (
  <board
    width={`${REAR_PCB.width}mm`}
    height={`${REAR_PCB.height}mm`}
    thickness={`${REAR_PCB.thickness}mm`}
    layers={2}
    {...BOARD_POLICY}
  >
    {/* Functional schematic blocks. One sheet per board, sections per block. */}
    <schematicsheet name="rear" displayName="MIDI Brain rear/base board" sheetSize="A4">
      <schematicsection name="headers" displayName="Connectors" />
      <schematicsection name="power" displayName="Power, decoupling and MIDI rail" />
      <schematicsection name="isolation" displayName="Pi/Pico isolation, reset" />
      <schematicsection name="probe" displayName="Probe points (bare copper, no parts)" />
    </schematicsheet>

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
      schSheetName="rear"
      schSectionName="headers"
      connections={picoLeft}
    />
    {/* Pico polarity/label facts: physical pin 15 (GND on a Pico 2 pinout),
        17 and 20 carry no net in this design - the Pico module sits on its own
        contacts - so the ports are declared do-not-connect here rather than
        left to print a missing-trace warning. A wired pin that loses its net
        still prints and still fails the report check. */}
    <connector
      name="J_PICO_R"
      pinCount={20}
      footprint="pinrow20_p2.54mm"
      manufacturerPartNumber="PPTC201LFBN-RC"
      pcbX="-30.61mm"
      pcbY="0mm"
      pcbRotation="90deg"
      layer="bottom"
      schSheetName="rear"
      schSectionName="headers"
      pinAttributes={{ pin15: { doNotConnect: true }, pin17: { doNotConnect: true }, pin20: { doNotConnect: true } }}
      connections={picoRight}
    />

    {/* Pi 40-pin ribbon across the top edge, XFCN BH254V-40P, C48687632.
        Numbered pads: pin 1..40 alternate between the two rows, matching the
        Pi header. A generic 2x2.54 pinrow footprint numbers pads around the
        perimeter instead and would land every net on the wrong pin. */}
    {/* Only the Pi header positions this module uses are wired (see
        `piHeader`); the rest are spare GPIO/ground rails on the Pi side. They
        are declared do-not-connect so the build reports the used set instead
        of printing a warning for every spare pin. The declaration is data, not
        a filter: wiring one of these later simply removes it from the list,
        and losing a wired net still prints and still fails. */}
    <BH254V_40P
      name="J_PI"
      pcbX="0mm"
      pcbY="48mm"
      layer="bottom"
      schSheetName="rear"
      schSectionName="headers"
      pinAttributes={SPARE_PI_PINS}
      connections={piHeader}
    />

    {/* Front-board interconnect on the right edge, XFCN BH254V-26P,
        C48687629 - the same part as J5 on the front board. */}
    <BH254V_26P
      name="J_FRONT"
      pcbX="45mm"
      pcbY="0mm"
      pcbRotation="90deg"
      layer="bottom"
      schSheetName="rear"
      schSectionName="headers"
      connections={frontInterconnect}
    />

    <SN74LVC1G126DBVR
      name="U3"
      pcbX="38mm"
      pcbY="-8mm"
      layer="bottom"
      schSheetName="rear"
      schSectionName="isolation"
      connections={{ A: "net.SPI_MISO_PICO", Y: "net.SPI_MISO_PI", OE: "net.OE", VCC: "net.P3V3", GND: "net.GND" }}
    />
    <SN74LVC1G126DBVR
      name="U4"
      pcbX="38mm"
      pcbY="-3mm"
      layer="bottom"
      schSheetName="rear"
      schSectionName="isolation"
      connections={{ A: "net.READY_PICO", Y: "net.READY_PI", OE: "net.OE", VCC: "net.P3V3", GND: "net.GND" }}
    />

    <R name="R13" value="100" x="33mm" y="-16mm" lcsc="C22775" mpn="0603WAF1000T5E" p1="net.SPI_MOSI_PI" p2="net.SPI_MOSI_PICO" />
    <R name="R14" value="100" x="33mm" y="-13mm" lcsc="C22775" mpn="0603WAF1000T5E" p1="net.SPI_SCLK_PI" p2="net.SPI_SCLK_PICO" />
    <R name="R15" value="100" x="33mm" y="-10mm" lcsc="C22775" mpn="0603WAF1000T5E" p1="net.SPI_CS_PI" p2="net.SPI_CS_PICO" />
    <R name="R19" value="100" x="38mm" y="-20mm" rot="180deg" lcsc="C22775" mpn="0603WAF1000T5E" p1="net.PI_3V3" p2="net.OE" />
    <R name="R20" value="100k" x="38mm" y="-17mm" lcsc="C25803" mpn="0603WAF1003T5E" p1="net.OE" p2="net.GND" />

    {/* 2N7002, LCSC C8545: pad 1 gate, pad 2 source, pad 3 drain. */}
    <C8545_2N7002
      name="Q3"
      pcbX="36mm"
      pcbY="8mm"
      layer="bottom"
      schSheetName="rear"
      schSectionName="isolation"
      connections={{ gate: "net.RESET_GATE", source: "net.GND", drain: "net.RUN" }}
    />
    <R name="R16" value="10k" x="32mm" y="6mm" rot="180deg" lcsc="C25804" mpn="0603WAF1002T5E" p1="net.RESET_PI" p2="net.RESET_GATE" />
    <R name="R17" value="100k" x="32mm" y="10mm" lcsc="C25803" mpn="0603WAF1003T5E" p1="net.RESET_GATE" p2="net.GND" />
    <R name="R18" value="10k" x="32mm" y="4mm" lcsc="C25804" mpn="0603WAF1002T5E" p1="net.RUN" p2="net.P3V3" />

    <B5819W_SL
      name="D3"
      pcbX="-45mm"
      pcbY="35mm"
      layer="bottom"
      schSheetName="rear"
      schSectionName="power"
      connections={{ anode: "net.P5V", cathode: "net.VSYS" }}
    />
    <C name="C7" value="10uF" x="-40mm" y="35mm" lcsc="C13585" mpn="CL31A106KBHNNNE" footprint="1206" p1="net.P5V" p2="net.GND" />
    <C name="C8" value="100nF" x="-35mm" y="35mm" lcsc="C14663" mpn="CC0603KRX7R9BB104" p1="net.P5V" p2="net.GND" />
    <C name="C9" value="10uF" x="-45mm" y="30mm" lcsc="C13585" mpn="CL31A106KBHNNNE" footprint="1206" p1="net.VSYS" p2="net.GND" />
    <C name="C10" value="100nF" x="-40mm" y="30mm" rot="180deg" lcsc="C14663" mpn="CC0603KRX7R9BB104" p1="net.VSYS" p2="net.GND" />
    <C name="C11" value="10uF" x="-35mm" y="30mm" lcsc="C13585" mpn="CL31A106KBHNNNE" footprint="1206" p1="net.P3V3" p2="net.GND" />
    <C name="C12" value="100nF" x="-30mm" y="30mm" lcsc="C14663" mpn="CC0603KRX7R9BB104" p1="net.P3V3" p2="net.GND" />
    {/* Local bypass at the two buffer VCC/GND pads: measured pad distances are
        in docs/ELECTRICAL-VERIFICATION.md and asserted by
        scripts/check_electrical.py (limit 3 mm). */}
    <C name="C13" value="100nF" x="37.55mm" y="-10.45mm" lcsc="C14663" mpn="CC0603KRX7R9BB104" p1="net.P3V3" p2="net.GND" />
    <C name="C14" value="100nF" x="37.55mm" y="-5.45mm" lcsc="C14663" mpn="CC0603KRX7R9BB104" p1="net.P3V3" p2="net.GND" />

    {/* Probe points: bare plated through-holes on nets that already exist, one
        source in lib/probe-points.ts. They add no part and bridge no rail; the
        explicit footprint carries the drill AND the pad (0.80 / 1.60 mm,
        0.40 mm ring) because the through-hole testpoint variant ignores
        padDiameter. Physical probe access is unverified: acceptance item
        probe_point_access. */}
    {PROBE_POINTS.map(p => (
      <testpoint
        key={p.ref}
        name={p.ref}
        footprint={PROBE_FOOTPRINT}
        holeDiameter={`${PROBE_HOLE_DIAMETER_MM}mm`}
        padDiameter={`${PROBE_PAD_DIAMETER_MM}mm`}
        doNotPlace
        layer="bottom"
        pcbX={`${p.x}mm`}
        pcbY={`${p.y}mm`}
        schSheetName="rear"
        schSectionName="probe"
        connections={{ pin1: `net.${p.net}` }}
      />
    ))}

    {/* Silkscreen wave: brand lockup, hand-solder hints, pin-1 and polarity
        marks. Every position and its clearance proof live in lib/silk.tsx. */}
    <RearSilk />

    {/* Rear-side ground pour; all parts are on the bottom layer. */}
    <copperpour name="GND_POUR_TOP" layer="top" connectsTo="net.GND" boardEdgeMargin="0.3mm" />
  </board>
)

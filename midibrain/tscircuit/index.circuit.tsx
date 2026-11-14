import { Fragment } from "react"
import { assembly } from "tscircuit"
import { B5819W_SL } from "./imports/B5819W_SL"
import { BH254V_40P } from "./imports/BH254V_40P"
import { C8545_2N7002 } from "./imports/C8545_2N7002"
import { SN74LVC1G126DBVR } from "./imports/SN74LVC1G126DBVR"
import { BOARD_POLICY } from "./lib/electrical"
import { INTERCONNECT_MECH, INTERCONNECT_NETS, SBH11_PBPC_D13_ST_BK } from "./lib/interconnect"
import { DISPLAY_CONCEPT, DISPLAY_VENDOR, FRONT_REAR_MOUNT, LCD_MOUNT_HOLES, MOUNT_HOLE_DIAMETER, REAR_INTERCONNECT, REAR_PCB } from "./lib/layout"
import { PROBE_FOOTPRINT, PROBE_HOLE_DIAMETER_MM, PROBE_PAD_DIAMETER_MM, PROBE_POINTS } from "./lib/probe-points"
import { RearSilk } from "./lib/silk"

// REAR/BASE BOARD, 20HP x 112 mm. Prototype schematic/placement; not a
// fabrication release. The Pi 5 no longer sits on this board (owner decision
// 2026-11-12): it is on its own 20HP mounting panel, reached through the 40-wire
// ribbon at J_PI. The Pico sockets are on the left and J_PI at the top. All SMT
// parts are on the rear (bottom) side; the front/rear male interconnect is the
// one THT body on the front-facing top side.

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

// Front/rear interconnect, 2 x 13: ONE net table for both boards, imported
// from lib/interconnect.tsx so the front socket and this male header can never
// disagree. Pin n carries the same net on both boards and lands on the same
// physical pad; lib/interconnect.test.ts proves that on the built JSON.

// ---- 2 x 13 interconnect body (male shroud side) ----------------------------
// The other half of the pair. Same reasoning as the front board's socket body:
// `<connector cadModel>` takes only `null` in tscircuit 0.0.3169, so the body is
// an `assembly.printedpart` - one cad_component, no footprint, no BOM row.
//
// REFERENCE ENVELOPE, NOT A FIT MODEL: a union of five slabs built only from
// INTERCONNECT_MECH (Sullins drawing 11091): maleHeight 9.1 (.358), bodyLength
// 40.52 (B), bodyEnvelope 8.8 (the conservative across-rows upper bound the
// module already uses). Local frame matches the footprint - Y is the
// 13-position axis, X across the two rows, +Z away from the board - and J_FRONT
// sits on this board's TOP face, so the shroud stands on +Z.
const maleSlab = (x: number, y: number, z: number, at: [number, number, number]) => ({
  type: "translate",
  vector: [at[0], at[1], at[2]],
  shape: { type: "cuboid", size: [x, y, z] },
})

const INTERCONNECT_MALE_W = INTERCONNECT_MECH.bodyEnvelope
const INTERCONNECT_MALE_BASE = 1.5
const INTERCONNECT_MALE_WALL = 1.15
const maleShroudEnvelope = (() => {
  const len = INTERCONNECT_MECH.bodyLength
  const h = INTERCONNECT_MECH.maleHeight
  const baseH = INTERCONNECT_MALE_BASE
  const wallH = h - baseH
  const innerW = INTERCONNECT_MALE_W - 2 * INTERCONNECT_MALE_WALL
  const z0 = REAR_PCB.thickness / 2
  return {
    type: "union",
    shapes: [
      // 13-position axis along LOCAL X, for the same reason as the front board's
      // socket: the 90 deg rotation carries it to global Y, where the pins step.
      maleSlab(len, INTERCONNECT_MALE_W, baseH, [0, 0, z0 + baseH / 2]),
      maleSlab(len, INTERCONNECT_MALE_WALL, wallH, [0, -INTERCONNECT_MALE_W / 2 + INTERCONNECT_MALE_WALL / 2, z0 + baseH + wallH / 2]),
      maleSlab(len, INTERCONNECT_MALE_WALL, wallH, [0, INTERCONNECT_MALE_W / 2 - INTERCONNECT_MALE_WALL / 2, z0 + baseH + wallH / 2]),
      maleSlab(INTERCONNECT_MALE_WALL, innerW, wallH, [-len / 2 + INTERCONNECT_MALE_WALL / 2, 0, z0 + baseH + wallH / 2]),
      maleSlab(INTERCONNECT_MALE_WALL, innerW, wallH, [len / 2 - INTERCONNECT_MALE_WALL / 2, 0, z0 + baseH + wallH / 2]),
    ],
  }
})()

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

    {/* Front-board spacer grid (shared XY, see lib/layout.ts FRONT_REAR_MOUNT). */}
    {/* 2026-11-13 (owner): the front-to-rear spacer holes are back on both
        boards, and the four PANEL standoff holes are the ±71.5/±52 frame.

        The DISPLAY CARRIER holes are NOT in that category and are added here:
        the 4.3" DSI module left the front board when the panel datum moved,
        and it now hangs on a printed carrier bolted to this board's top face.
        The centres are the module's own SMTSO boss pattern (the only M2.5
        threads it offers), offset by the display's placement - lib/layout.ts
        LCD_MOUNT_HOLES, so the carrier cannot drift from the front board's
        opening. R19 moved off the (39.65, -19.90) hole to (34, -22.5). */}
    {FRONT_REAR_MOUNT.map((h, i) => (
      <Fragment key={`spacer-${i}`}>
        <hole name={`H${i + 1}`} diameter={`${MOUNT_HOLE_DIAMETER}mm`} pcbX={`${h.x}mm`} pcbY={`${h.y}mm`} />
      </Fragment>
    ))}
    {LCD_MOUNT_HOLES.map((h, i) => (
      <Fragment key={`lcd-${i}`}>
        <hole name={`LCD_${i + 1}`} diameter={`${MOUNT_HOLE_DIAMETER}mm`} pcbX={`${h.x}mm`} pcbY={`${h.y}mm`} />
      </Fragment>
    ))}

    {/* ---- Display carrier markers, TOP layer ----
        The silk module prints the rear board's BOTTOM face, where the parts are.
        The 4.3" DSI module and its printed carrier live on this board's TOP face,
        so its markers are emitted here instead, on the layer a builder looks at
        while fitting the carrier. Everything is derived from LCD_MOUNT_HOLES, and
        the labels sit OUTBOARD of the module outline (the module is 106.1 x 68.4
        and hides anything under it): the outline here is 2 mm larger than the
        module, so it stays visible as a register mark. */}
    {/* Register marks, not a closed rectangle: a 110 x 72 outline crosses the
        Pico sockets' pads at y -25.4 and the probe pads at y 41-47 (measured). Four
        L-shaped corner ticks 4 mm outboard of the module outline say the same
        thing and touch nothing. */}
    {[[-1,-1],[1,-1],[-1,1],[1,1]].map(([sx, sy], i) => {
      const cx = DISPLAY_CONCEPT.x + sx * (DISPLAY_VENDOR.outline.width / 2 + 4)
      const cy = DISPLAY_CONCEPT.y + sy * (DISPLAY_VENDOR.outline.height / 2 + 4)
      return (
        <Fragment key={`lcd-tick-${i}`}>
          <silkscreenpath layer="top" strokeWidth="0.2mm"
            route={[{ x: cx, y: cy - sy * 6 }, { x: cx, y: cy }, { x: cx - sx * 6, y: cy }]} />
        </Fragment>
      )
    })}
    {LCD_MOUNT_HOLES.map((h, i) => (
      <Fragment key={`lcd-label-${i}`}>
        <silkscreentext layer="top" text={`LCD${i + 1}`} fontSize="1.1mm" anchorAlignment="center"
          pcbX={`${h.x + (h.x < DISPLAY_CONCEPT.x ? 9 : -9)}mm`} pcbY={`${h.y + (h.y > DISPLAY_CONCEPT.y ? 5 : -5)}mm`} />
      </Fragment>
    ))}
    <silkscreentext layer="top" text="DISPLAY CARRIER - 4x M2.5" fontSize="1.2mm" anchorAlignment="center"
      pcbX={`${DISPLAY_CONCEPT.x}mm`} pcbY={`${DISPLAY_CONCEPT.y + DISPLAY_VENDOR.outline.height / 2 + 8.5}mm`} />

    {/* Male box-header shroud of the 2 x 13 pair. */}
    <assembly.printedpart
      name="PRINT_IC_MALE"
      color="#8a8f98"
      cadModel={{
        jscad: maleShroudEnvelope,
        positionOffset: { x: REAR_INTERCONNECT.x, y: REAR_INTERCONNECT.y, z: 0 },
        rotationOffset: { x: 0, y: 0, z: REAR_INTERCONNECT.rotation },
        showAsTranslucentModel: true,
      }}
    />
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
    {/* J_PICO_R polarity/label facts. Each 1 x 20 socket numbers its own pads
        1-20, so THIS CONNECTOR's pin 15 is the Pico module's physical pin 35,
        ADC_VREF, which this design leaves unconnected. Ports pin15, pin17 and
        pin20 are therefore declared do-not-connect rather than left to print a
        missing-trace warning; a wired pin that loses its net still prints and
        still fails the report check. On the LEFT row, J_PICO_L pin 15 is module
        physical pin 15 = GP11 = ENC_A, NOT a ground; the left-row grounds are
        module physical pins 3, 8, 13 and 18 (`picoLeft` pins 3, 8, 13, 18). */}
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

    {/* Direct 2 x 13 interconnect, MALE side: Sullins SBH11-PBPC-D13-ST-BK
        straight box header on the rear board's TOP face, opposite the Pi. It
        shares REAR_INTERCONNECT with the front board's J5 (one constant), so
        the 26 pads coincide: the front socket plugs straight onto it with no
        plug and no ribbon. Its tail is 3.00 mm FROM THE CONNECTOR SEATING FACE,
        i.e. a NOMINAL 1.40 mm protrusion beyond a 1.60 mm rear PCB, and the 26
        pads have no Pi board above them (the Pi is on its own panel); the 3.5 mm Pi
        spacer is a CANDIDATE, not a
        safety claim, because the fillet and clearance are unmeasured
        (lib/layout.ts INTERCONNECT_PROTRUSION, PROVISIONAL_DEPTH). Physical
        fit, keying and pre-power continuity are pending (acceptance item
        interconnect_mate). */}
    <SBH11_PBPC_D13_ST_BK
      name="J_FRONT"
      pcbX={`${REAR_INTERCONNECT.x}mm`}
      pcbY={`${REAR_INTERCONNECT.y}mm`}
      pcbRotation={`${REAR_INTERCONNECT.rotation}deg`}
      layer="top"
      schSheetName="rear"
      schSectionName="headers"
      connections={INTERCONNECT_NETS}
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
    <R name="R14" value="100" x="33mm" y="-12.5mm" lcsc="C22775" mpn="0603WAF1000T5E" p1="net.SPI_SCLK_PI" p2="net.SPI_SCLK_PICO" />
    <R name="R15" value="100" x="33mm" y="-9mm" lcsc="C22775" mpn="0603WAF1000T5E" p1="net.SPI_CS_PI" p2="net.SPI_CS_PICO" />
    <R name="R19" value="100" x="34mm" y="-22.5mm" rot="180deg" lcsc="C22775" mpn="0603WAF1000T5E" p1="net.PI_3V3" p2="net.OE" />
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
        scripts/check_electrical.py (limit 3 mm).

        Keep C14 at the position that record documents (37.55, -5.45). It was
        briefly moved to (30.5, -6.5) in 02a643b to clear a SILK collision (U3's
        printed refdes landed on C14's pad, C14's own refdes on U4's pad), and
        that put the 100 nF 8.37 mm from U4's VCC pad, i.e. out of reach of the
        pin it exists for. The overlap is the known-and-accepted class in
        lib/silk.tsx (vendor footprints draw their own outlines across their own
        pads; the fab clips silk over exposed copper), and silk.test.ts cannot
        see it because it checks only the marks lib/silk.tsx emits, not refdes
        printed by the vendored footprints. Fix such a collision in the import
        (B3F_4055 takes a refTextAt prop, M20_9960245 prints its own {NAME} at a
        chosen spot), never by moving the capacitor. */}
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

    {/* Ground pour on the rear board's TOP layer. All SMT parts are on the
        bottom (Pi) side; the only body on this top side is the J_FRONT male
        interconnect, which is THT and added after SMT. */}
    <copperpour name="GND_POUR_TOP" layer="top" connectsTo="net.GND" boardEdgeMargin="0.3mm" />
  </board>
)

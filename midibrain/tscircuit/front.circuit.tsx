import { Fragment } from "react"
import { assembly } from "tscircuit"
import { A_1N4148WS } from "./imports/A_1N4148WS"
import { C15127_AO3401A } from "./imports/C15127_AO3401A"
import { B3F_4055 } from "./imports/B3F_4055"
import { PEC11R_4015F_S0024 } from "./imports/PEC11R_4015F_S0024"
import { BOARD_POLICY } from "./lib/electrical"
import { M20_9960245 } from "./imports/M20_9960245"
import { INTERCONNECT_MECH, INTERCONNECT_NETS, SFH11_PBPC_D13_ST_BK } from "./lib/interconnect"
import { MIDI_OUT_SERIES_OHM, MIDI_OUT_GATE_OHM, MIDI_RECEIVER_OHM } from "./lib/electrical-midi"
import { FP_JACK_TRS } from "./lib/jack"
import { SKRHABE010 } from "./imports/SKRHABE010"
import { FrontSilk } from "./lib/silk"
import { TLP2361_TPL_E } from "./imports/TLP2361_TPL_E"
import {
  DISPLAY_CONCEPT,
  frontBoardUpZ,
  FEEDTHROUGH_BRACKET,
  FRONT_BOARD_HOLES,
  FRONT_DISPLAY_OPENING,
  FRONT_BOARD_OPENING_POLYGON,
  FRONT_DISPLAY_KEEPOUT,
  FRONT_INTERCONNECT,
  FRONT_PCB,
  FRONT_REAR_MOUNT,
  FRONT_TOP_NOTCH,
  JACK_Y,
  MIDI_CLUSTERS,
  LED_Y,
  ledPosition,
  midiJackX,
  MOUNT_HOLE_DIAMETER,
  POWER_BUTTON,
  POWER_HEADER,
  PRINTED_PARTS,
  PROVISIONAL_Z,
  RIGHT_CONTROLS,
  TRANSPORT_Y,
  transportButtonX,
} from "./lib/layout"

// FRONT/UI BOARD. Prototype schematic/placement; not a fabrication release.
// SMD parts are JLCPCB/LCSC candidates. THT controls/jacks are hand-soldered.
// All positions import from lib/layout.ts (single source); nothing here may
// re-state a coordinate.

type RProps = {
  name: string
  value: string
  x: string
  y: string
  lcsc: string
  mpn: string
  p1: string
  p2: string
  footprint?: string
  rot?: string
  sec: string
  schX?: string
  schY?: string
}
const R = ({ name, value, x, y, lcsc, mpn, p1, p2, footprint = "0603", rot, sec, schX, schY }: RProps) => (
  <resistor
    name={name}
    resistance={value}
    footprint={footprint}
    pcbX={x}
    pcbY={y}
    pcbRotation={rot}
    layer="top"
    schSheetName="front"
    schSectionName={sec}
    schX={schX}
    schY={schY}
    supplierPartNumbers={{ jlcpcb: [lcsc] }}
    manufacturerPartNumber={mpn}
    connections={{ pin1: p1, pin2: p2 }}
  />
)

type CProps = { name: string; value: string; x: string; y: string; lcsc: string; mpn: string; p1: string; p2: string; footprint?: string; rot?: string; sec: string }
const C = ({ name, value, x, y, lcsc, mpn, p1, p2, footprint = "0603", rot, sec }: CProps) => (
  <capacitor
    name={name}
    capacitance={value}
    footprint={footprint}
    pcbX={x}
    pcbY={y}
    pcbRotation={rot}
    layer="top"
    schSheetName="front"
    schSectionName={sec}
    supplierPartNumbers={{ jlcpcb: [lcsc] }}
    manufacturerPartNumber={mpn}
    connections={{ pin1: p1, pin2: p2 }}
  />
)

// Sleeve decoupling cap follows its jack: 7 mm left, 5.5 mm above the barrel.
// (7 mm keeps the 0603 1.2 mm clear of the jack-body courtyard now that all
// SMT shares the panel side with the THT bodies.)
// Cluster geometry lives in CLUSTER_LOCAL: the opto is the origin, everything
// else is an offset from it, rendered inside a <group pcbX pcbY>. Moving a
// cluster is therefore one number in lib/layout.ts (MIDI_CLUSTERS) and the
// internal layout cannot drift; before this, every offset was `x + 8`-style
// arithmetic over an absolute coordinate, and lib/silk.tsx kept its own copy of
// the origins (which is how two silkscreen marks got stranded during the
// 10 mm respin).
const CLUSTER_LOCAL = {
  inDiode: { x: 8, y: 2 },      // 1N4148WS across the opto input
  inRingR: { x: -2, y: -5 },    // 220R in series with RING
  inPullup: { x: 8, y: -3.5 },  // 2.2k on the opto output
  inDecoup: { x: 4, y: 3 },     // 100nF at the opto's VCC/GND
  outTxR: { x: -2, y: 5 },      // 100R gate resistor
  outPullup: { x: 6, y: 5 },    // 2.2k gate pull-up
  outRingR: { x: -2, y: -6 },   // 33R 2512 on the drain
  outTipR: { x: 4.5, y: -6 },   // 220R 2512 on the tip
} as const

const MidiIn = ({ ch, jackX, jackY, at }: { ch: 1 | 2; jackX: number; jackY: number; at: { x: number; y: number } }) => {
  const j = `J${ch}`, u = `U${ch}`, d = `D${ch}`
  // Absolute position = cluster origin (lib/layout.ts MIDI_CLUSTERS) + a
  // CLUSTER_LOCAL offset. A <group pcbX pcbY> was tried first and rejected: the
  // group anchors on its children's bounding box, not on its own origin, so
  // every part landed ~7 mm off (measured, not assumed).
  const px = (dx: number) => `${at.x + dx}mm`
  const py = (dy: number) => `${at.y + dy}mm`
  const ring = `net.MIDI_IN_${ch}_RING`
  const tip = `net.MIDI_IN_${ch}_TIP`
  const anode = `net.MIDI_IN_${ch}_ANODE`
  const rxNet = `net.MIDI_IN_${ch}_RX`
  return (
    <>
      <connector
        name={j}
        footprint={FP_JACK_TRS}
        pinCount={3}
        pinLabels={{ pin1: "S", pin2: "R", pin3: "T" }}
        manufacturerPartNumber="WQP419GR"
        cadModel={{
          stepUrl: "https://raw.githubusercontent.com/clacktronics/AudioJacks/14a88866e93b8ce4a31ad376b0c6eb85cd4d2cf3/AudioJacks.3dshapes/PJ366ST.step",
          pcbRotationOffset: 0,
          modelOriginPosition: { x: 0, y: 0, z: 0 },
        }}
        pcbX={`${jackX}mm`}
        pcbY={`${jackY}mm`}
        layer="top"
        schSheetName="front"
        schSectionName="midi_in"
        connections={{ T: tip, R: ring, S: `net.MIDI_IN_${ch}_SLEEVE` }}
      />
      {/* Toshiba TLP2361(TPL,E, C107626: inverting totem-pole output, IFHL
          1.3 mA max to 105 C. Pin 1 anode, 3 cathode, 4 GND, 5 VO, 6 VCC. */}
      {/* Sleeve decoupling. It used to sit 7 mm beside its jack; at the new
          12 mm jack pitch there is no room between the 10 mm jack courtyards, so
          it moves into the cluster and the sleeve net becomes a short stub. */}
      <C name={`C${ch}`} value="100nF" sec="midi_in" x={px(0)} y={py(6)} lcsc="C14663" mpn="CC0603KRX7R9BB104" p1={`net.MIDI_IN_${ch}_SLEEVE`} p2="net.GND" />
      <TLP2361_TPL_E
        name={u}
        pcbX={px(0)}
        pcbY={py(0)}
        layer="top"
        schSheetName="front"
        schSectionName="midi_in"
        pinAttributes={{ pin1: { isPassive: true }, pin3: { isPassive: true }, pin4: { requiresGround: true }, pin5: { isPassive: true }, pin6: { requiresPower: true } }}
        connections={{
          pin1: anode,
          pin3: tip,
          pin4: "net.GND",
          pin5: rxNet,
          pin6: "net.P3V3",
        }}
      />
      <A_1N4148WS
        name={d}
        pcbX={px(CLUSTER_LOCAL.inDiode.x)}
        pcbY={py(CLUSTER_LOCAL.inDiode.y)}
        layer="top"
        schSheetName="front"
        schSectionName="midi_in"
        connections={{ cathode: anode, anode: tip }}
      />
      <R name={`R${ch}`} value={`${MIDI_RECEIVER_OHM}`} sec="midi_in" schX={ch === 1 ? "-21.5" : undefined} schY={ch === 1 ? "-1.3" : undefined} x={px(CLUSTER_LOCAL.inRingR.x)} y={py(CLUSTER_LOCAL.inRingR.y)} lcsc="C22962" mpn="0603WAF2200T5E" p1={ring} p2={anode} />
      <R name={`R${ch + 2}`} value="2.2k" sec="midi_in" x={px(CLUSTER_LOCAL.inPullup.x)} y={py(CLUSTER_LOCAL.inPullup.y)} lcsc="C4190" mpn="0603WAF2201T5E" p1={rxNet} p2="net.P3V3" />
      {/* C2 carries the placement note ("connections cross the routing path
          between its pads"), C1 does not; a 0603 X7R cap is non-polarized, so
          rotating it moves no polarity mark. */}
      <C name={`C${ch + 2}`} value="100nF" sec="midi_in" x={px(CLUSTER_LOCAL.inDecoup.x)} y={py(CLUSTER_LOCAL.inDecoup.y)} lcsc="C14663" mpn="CC0603KRX7R9BB104" p1="net.P3V3" p2="net.GND" />
    </>
  )
}

const MidiOut = ({ ch, jackX, jackY, at }: { ch: 1 | 2; jackX: number; jackY: number; at: { x: number; y: number } }) => {
  const j = `J${ch + 2}`, q = `Q${ch}`
  const px = (dx: number) => `${at.x + dx}mm`
  const py = (dy: number) => `${at.y + dy}mm`
  const ring = `net.MIDI_OUT_${ch}_RING`
  const tip = `net.MIDI_OUT_${ch}_TIP`
  const gate = `net.MIDI_OUT_${ch}_GATE`
  const drain = `net.MIDI_OUT_${ch}_DRAIN`
  const tx = `net.MIDI_OUT_${ch}_TX`
  return (
    <>
      <connector
        name={j}
        footprint={FP_JACK_TRS}
        pinCount={3}
        pinLabels={{ pin1: "S", pin2: "R", pin3: "T" }}
        manufacturerPartNumber="WQP419GR"
        cadModel={{
          stepUrl: "https://raw.githubusercontent.com/clacktronics/AudioJacks/14a88866e93b8ce4a31ad376b0c6eb85cd4d2cf3/AudioJacks.3dshapes/PJ366ST.step",
          pcbRotationOffset: 0,
          modelOriginPosition: { x: 0, y: 0, z: 0 },
        }}
        pcbX={`${jackX}mm`}
        pcbY={`${jackY}mm`}
        layer="top"
        schSheetName="front"
        schSectionName="midi_out"
        connections={{ T: tip, R: ring, S: "net.GND" }}
      />
      {/* High-side P-channel switch: AO3401A (C15127), pad 1 gate, 2 source,
          3 drain. The gate is pulled to the 3.3 V rail by 2.2 kOhm so the
          output is off while the UART pin is high or high-Z, and is driven low
          through 100 Ohm when the UART pin goes low. */}
      <C15127_AO3401A
        name={q}
        pcbX={px(0)}
        pcbY={py(0)}
        layer="top"
        schSheetName="front"
        schSectionName="midi_out"
        connections={{ gate, source: "net.P3V3", drain }}
      />
      <R name={`R${ch + 4}`} value={`${MIDI_OUT_GATE_OHM.series}`} sec="midi_out" x={px(CLUSTER_LOCAL.outTxR.x)} y={py(CLUSTER_LOCAL.outTxR.y)} lcsc="C22775" mpn="0603WAF1000T5E" p1={tx} p2={gate} />
      <R name={`R${ch + 6}`} value={`${MIDI_OUT_GATE_OHM.pullup}`} sec="midi_out" x={px(CLUSTER_LOCAL.outPullup.x)} y={py(CLUSTER_LOCAL.outPullup.y)} lcsc="C4190" mpn="0603WAF2201T5E" p1={gate} p2="net.P3V3" />
      {/* R9/R10 rotate 270 deg, not 0/90: `tsci check placement` flagged the
          90 deg pads as "connections cross the routing path between its pads",
          and 270 puts pad 1 on the routed side. Value, net and footprint are
          unchanged. */}
      <R name={`R${ch + 8}`} value={`${MIDI_OUT_SERIES_OHM.ring}`} sec="midi_out" x={px(CLUSTER_LOCAL.outRingR.x)} y={py(CLUSTER_LOCAL.outRingR.y)} lcsc="C53728" mpn="25121WF330JT4E" footprint="2512" rot="270deg" p1={drain} p2={ring} />
      {/* R11/R12 rotate 180 deg after the cluster moved to x = -49 for the
          4.3-inch module's LCD hole: `tsci check placement` flags their pads as
          "connections cross the routing path between its pads". Value, net and
          footprint are unchanged. */}
      <R name={`R${ch + 10}`} value={`${MIDI_OUT_SERIES_OHM.tip}`} sec="midi_out" x={px(CLUSTER_LOCAL.outTipR.x)} y={py(CLUSTER_LOCAL.outTipR.y)} rot="180deg" lcsc="C118189" mpn="25121WF220JT4E" footprint="2512" p1={tip} p2="net.GND" />
    </>
  )
}

// Transport buttons: one B3F-4055 per transport column plus PLAY START in the
// right-hand control column. X positions come from lib/layout.ts.
const TRANSPORT_SWITCHES = [
  { name: "SW1", button: "REC", net: "net.REC" },
  { name: "SW2", button: "OVER DUB", net: "net.OVER_DUB" },
  { name: "SW3", button: "STOP", net: "net.STOP" },
  { name: "SW4", button: "PLAY", net: "net.PLAY" },
  { name: "SW5", button: "PLAY START", net: "net.PLAY_START" },
] as const

// LEDs BESIDE REC / OVER DUB / PLAY (owner-authorised fallback 3, 2026-11-07:
// the glass-derived panel window owns the row above the transport buttons), 12
// mm to the right of their button so the panel openings keep a 3.8 mm FR-4 web.
// The 330R series resistor sits 4.5 mm above its LED: side by side it would
// reach into the neighbouring button courtyard (24 mm pitch against a 15 mm
// B3F-4055 courtyard leaves 9 mm of free board between two buttons).
// Anode nets and LCSC parts unchanged from the routed prototype.
const FRONT_LED_PARTS = [
  { name: "D3", button: "REC", lcsc: "C2295", mpn: "KT-0805R", drive: "net.REC_LED", anode: "net.REC_LED_A" },
  { name: "D4", button: "OVER DUB", lcsc: "C2295", mpn: "KT-0805R", drive: "net.OVERDUB_LED", anode: "net.OVERDUB_LED_A" },
  { name: "D5", button: "PLAY", lcsc: "C2297", mpn: "KT-0805G", drive: "net.PLAY_LED", anode: "net.PLAY_LED_A" },
] as const

// ---- Printed-part reference bodies (3D view only) ----
// The owner's two 3D-printed parts - the NAV keytop on U5 and the value knob on
// U6 - are not board parts: no pads, no footprint, no schematic symbol. They are
// drawn here so the 3D view shows what sits on the finished panel, the same way
// the display module's envelope used to be drawn. They are `assembly.printedpart`
// elements, NOT `<chip>`s: tscircuit still skips CAD for a `doNotPlace` part, so
// the old route (a padless chip per body) made each body a real placement in the
// hand BOM and in the placement report. `assembly.printedpart` has no PCB
// footprint at all, so it emits one cad_component and nothing else - the PCB,
// the BOM, the CPL and `tsci check placement` do not see them.
//
// REFERENCE ENVELOPES, NOT FIT MODELS. Each body is the revolved outline of the
// part (cylinders, union then subtract for the bore), built only from
// PRINTED_PARTS. Chamfers, the knob flutes and the grub-screw pilot are omitted:
// the geometry that has to be trusted lives in mechanical/printed/*.stl, and this
// is the same envelope those files are checked against.

// A solid cylinder on the part's own axis, between two panel-Z planes. JSCAD's
// cylinder is centred on its origin, so the translate carries the midpoint.
type JscadShape = Record<string, unknown>
const printCylinder = (diameter: number, zAt: number, zTo: number): JscadShape => ({
  type: "translate",
  vector: [0, 0, (frontBoardUpZ(zAt) + frontBoardUpZ(zTo)) / 2],
  shape: {
    type: "cylinder",
    radius: diameter / 2,
    height: Math.abs(frontBoardUpZ(zTo) - frontBoardUpZ(zAt)),
  },
})

const NAV_KEYTOP = PRINTED_PARTS.navKeytop
const NAV_FLANGE_REAR_Z = PROVISIONAL_Z.panelRear + NAV_KEYTOP.flangeThickness
const navKeytopEnvelope = {
  type: "subtract",
  shapes: [
    {
      type: "union",
      shapes: [
        // head + neck are one diameter through the panel aperture
        printCylinder(NAV_KEYTOP.neckDiameter, NAV_KEYTOP.zFront, PROVISIONAL_Z.panelRear),
        // flange bearing on the panel rear face
        printCylinder(NAV_KEYTOP.flangeDiameter, PROVISIONAL_Z.panelRear, NAV_FLANGE_REAR_Z),
        // shank down to the socket mouth
        printCylinder(NAV_KEYTOP.shankDiameter, NAV_FLANGE_REAR_Z, NAV_KEYTOP.zRear),
      ],
    },
    // the stem socket
    printCylinder(NAV_KEYTOP.socketBore, NAV_KEYTOP.zRear - NAV_KEYTOP.socketDepth, NAV_KEYTOP.zRear),
  ],
}

const VALUE_KNOB = PRINTED_PARTS.knob
const knobEnvelope = {
  type: "subtract",
  shapes: [
    printCylinder(VALUE_KNOB.bodyDiameter, VALUE_KNOB.zFront, VALUE_KNOB.zRear),
    // the shaft bore runs right through
    printCylinder(VALUE_KNOB.boreDiameter, VALUE_KNOB.zFront, VALUE_KNOB.zRear),
  ],
}

// ---- 2 x 13 interconnect body (female socket side) --------------------------
// Neither half of the interconnect had a CAD model, so the mating pair was
// invisible in every 3D view. `<connector cadModel>` only accepts `null` in
// tscircuit 0.0.3169 (an inline CSG body is rejected: 'Expected null, received
// object'), so the body is an `assembly.printedpart` like the printed parts:
// one cad_component, no footprint, no BOM row.
//
// REFERENCE ENVELOPE, NOT A FIT MODEL. A union of five slabs (base plus two long
// walls and two end walls): no booleans, because that is the CSG form proven in
// this repo. Every dimension is INTERCONNECT_MECH - the Sullins drawing 11091
// numbers - and nothing else:
//   femaleHeight 8.5 (catalog .335), insulatorLength 38.32 (drawing C),
//   7.00 across the rows (catalog .276).
// Local frame matches the footprint: Y is the 13-position axis, X across the
// two rows, +Z away from the board. The component sits on the BOTTOM face at
// FRONT_INTERCONNECT, so the body stands on -Z in panel terms.
const interconnectSlab = (x: number, y: number, z: number, at: [number, number, number]) => ({
  type: "translate",
  vector: [at[0], at[1], frontBoardUpZ(at[2])],
  shape: { type: "cuboid", size: [x, y, z] },
})

const INTERCONNECT_FEMALE_W = 7.0
const INTERCONNECT_BASE = 2.0
const INTERCONNECT_WALL = 1.0
const femaleSocketEnvelope = (() => {
  const len = INTERCONNECT_MECH.insulatorLength
  const h = INTERCONNECT_MECH.femaleHeight
  const baseH = INTERCONNECT_BASE
  const wallH = h - baseH
  const innerW = INTERCONNECT_FEMALE_W - 2 * INTERCONNECT_WALL
  const top = PROVISIONAL_Z.frontBoardRear
  return {
    type: "union",
    shapes: [
      interconnectSlab(INTERCONNECT_FEMALE_W, len, baseH, [0, 0, top + baseH / 2]),
      interconnectSlab(INTERCONNECT_WALL, len, wallH, [-INTERCONNECT_FEMALE_W / 2 + INTERCONNECT_WALL / 2, 0, top + baseH + wallH / 2]),
      interconnectSlab(INTERCONNECT_WALL, len, wallH, [INTERCONNECT_FEMALE_W / 2 - INTERCONNECT_WALL / 2, 0, top + baseH + wallH / 2]),
      interconnectSlab(innerW, INTERCONNECT_WALL, wallH, [0, -len / 2 + INTERCONNECT_WALL / 2, top + baseH + wallH / 2]),
      interconnectSlab(innerW, INTERCONNECT_WALL, wallH, [0, len / 2 - INTERCONNECT_WALL / 2, top + baseH + wallH / 2]),
    ],
  }
})()

export default () => (
  <board
    width={`${FRONT_PCB.width}mm`}
    height={`${FRONT_PCB.height}mm`}
    thickness={`${FRONT_PCB.thickness}mm`}
    layers={2}
    {...BOARD_POLICY}
    schGridColumnGap="4mm"
    schGridRowGap="3mm"
  >
    {/* Functional schematic blocks. One sheet per board, sections per block. */}
    <schematicsheet name="front" displayName="MIDI Brain front/UI board" sheetSize="ANSI_B">
      <schematicsection name="midi_in" displayName="MIDI IN 1/2 (TRS Type A, opto)" />
      <schematicsection name="midi_out" displayName="MIDI OUT 1/2 (TRS Type A, P-channel high side)" />
      <schematicsection name="controls" displayName="Controls and status LEDs" />
      <schematicsection name="power" displayName="Rail decoupling" />
      <schematicsection name="headers" displayName="Rear interconnect" />
    </schematicsheet>
    {/* The notch and the display opening are one cut now: the board is a U. */}

    {/* Display opening. At the 10 mm panel the module's body (glass front 0 ->
        rear-most 14.05 mm) crosses this board's 10.0...11.6 mm plane, so no
        board material may sit under it. The module hangs from the rear board's
        printed carrier; this board only has to let it through. */}
    <cutout shape="polygon" points={[...FRONT_BOARD_OPENING_POLYGON]} />
    <keepout shape="rect" width={`${FRONT_DISPLAY_KEEPOUT.width}mm`} height={`${FRONT_DISPLAY_KEEPOUT.height}mm`} pcbX={`${FRONT_DISPLAY_KEEPOUT.x}mm`} pcbY={`${FRONT_DISPLAY_KEEPOUT.y}mm`} excludeRefs={[".J_PWR"]} />
    {/* 2026-11-13 (owner): ALL mounting holes are removed for now - panel
        standoffs, front/rear spacer grid and the feedthrough-bracket screws.
        The boards are still positioned by the four jack bushings, the encoder
        bushings and the 2 x 13 interconnect; how the stack is finally fastened
        is an open item (docs/TODO.md). The constants in lib/layout.ts are kept,
        so putting them back is a one-line change per group. */}
    {/* 2026-11-13 (owner): the front-to-rear spacer holes are back (the panel
        standoffs are not - see lib/layout.ts). */}
    {FRONT_REAR_MOUNT.map((h, i) => (
      <Fragment key={`mount-hole-${i}`}><hole name={`MOUNT_${i + 1}`} diameter={`${MOUNT_HOLE_DIAMETER}mm`} pcbX={`${h.x}mm`} pcbY={`${h.y}mm`} /></Fragment>
    ))}
    {/* The module used to be screwed to four SMTSO bosses on this face. It now
        hangs from the rear board's printed carrier, so this board carries no
        display holes and no display silk - only the opening above. One note is
        kept so a builder knows what the hole is for. */}
    {/* The "DISPLAY OPENING" note lives in lib/silk.tsx with every other silk
        mark; printing it here as well duplicated the ink. */}
    {/* bracket screws: removed with the rest of the mounting holes (2026-11-13) */}


    {/* All SMT on the panel side for single-sided JLCPCB assembly. Since the
        2026-11-13 respin the display is an opening, so all four MIDI clusters
        live in the right-hand strip (x 58.3...75), stacked above the 2 x 13
        interconnect at its bottom. The left margin (12.25 mm) is too narrow for
        a cluster, and putting the interconnect there dragged every control net
        across the board. Jacks stay in the four shared transport/MIDI columns at the
        bottom edge; the jack nets are low-speed current loops. J5 stays bottom: the
        female socket's body is on the rear-facing side so the rear board's
        male header plugs straight into it, and its solder tails are the only
        through-hole joints on the panel side. */}
    <MidiIn ch={1} jackX={midiJackX("MIDI IN 1")} jackY={JACK_Y} at={MIDI_CLUSTERS.in1} />
    <MidiIn ch={2} jackX={midiJackX("MIDI IN 2")} jackY={JACK_Y} at={MIDI_CLUSTERS.in2} />
    {/* ch1 cluster shifted 4 mm left (x -45 -> -49) to clear the 4.3-inch
        module's bottom-left LCD_4 hole at (-41.88, -13.54), which the new
        58.00 x 49.04 mounting pattern puts inside the old Q1 courtyard. */}
    <MidiOut ch={1} jackX={midiJackX("MIDI OUT 1")} jackY={JACK_Y} at={MIDI_CLUSTERS.out1} />
    <MidiOut ch={2} jackX={midiJackX("MIDI OUT 2")} jackY={JACK_Y} at={MIDI_CLUSTERS.out2} />

    <SKRHABE010
      name="U5"
      pcbX={`${RIGHT_CONTROLS.NAV.x}mm`}
      pcbY={`${RIGHT_CONTROLS.NAV.y}mm`}
      layer="top"
      schSheetName="front"
      schSectionName="controls"
      connections={{ COM: "net.GND", A: "net.NAV_UP", B: "net.NAV_DOWN", C: "net.NAV_LEFT", D: "net.NAV_RIGHT", CEN: "net.NAV_PRESS", pin7: "net.GND", pin8: "net.GND" }}
    />
    {/* Value knob U6: Bourns PEC11R-4015F-S0024, LCSC C143789 - the detentless
        member of the PEC11R family (0 detents, 24 pulses/rev, 360 deg
        continuous, push switch), same 4xxxF-Sxxxx body and pad field as the
        24-detent part it replaces. Hand-soldered THT (the datasheet does not
        recommend hand soldering; acceptance item encoder_solder_process). */}
    <PEC11R_4015F_S0024
      name="U6"
      pcbX={`${RIGHT_CONTROLS.TEMPO.x}mm`}
      pcbY={`${RIGHT_CONTROLS.TEMPO.y}mm`}
      layer="top"
      schSheetName="front"
      schSectionName="controls"
      connections={{ A: "net.ENC_A", B: "net.ENC_B", C: "net.GND", pin1: "net.GND", pin2: "net.ENC_SW", MT1: "net.GND", MT2: "net.GND" }}
    />
    {/* The two printed parts, drawn as reference envelopes (see the block above
        FRONT_LED_PARTS). Position comes from PRINTED_PARTS.x/y, which is U5's and
        U6's own centre; Z comes from PRINTED_PARTS.zFront/zRear on the
        PROVISIONAL_Z datum. Translucent so the switch and the encoder stay
        visible through them. */}
    {/* Female socket body of the 2 x 13 pair. */}
    <assembly.printedpart
      name="PRINT_IC_FEMALE"
      color="#8a8f98"
      cadModel={{
        jscad: femaleSocketEnvelope,
        positionOffset: { x: FRONT_INTERCONNECT.x, y: FRONT_INTERCONNECT.y, z: 0 },
        rotationOffset: { x: 0, y: 0, z: FRONT_INTERCONNECT.rotation },
        showAsTranslucentModel: true,
      }}
    />
    <assembly.printedpart
      name="PRINT_NAV_KEYTOP"
      color="#3f6f8f"
      cadModel={{
        jscad: navKeytopEnvelope,
        positionOffset: { x: NAV_KEYTOP.x, y: NAV_KEYTOP.y, z: 0 },
        showAsTranslucentModel: true,
      }}
    />
    <assembly.printedpart
      name="PRINT_KNOB"
      color="#7a5c3e"
      cadModel={{
        jscad: knobEnvelope,
        positionOffset: { x: VALUE_KNOB.x, y: VALUE_KNOB.y, z: 0 },
        showAsTranslucentModel: true,
      }}
    />
    {/* B3F-4055 is a 4-pin button with two internal contact pairs (1-2 and
        3-4). Only pin1 (GND) and pin3 (signal) are wired; pin2/pin4 are the
        duplicated contacts and are declared noConnect so the build does not
        report a missing trace for a pin the circuit deliberately leaves open.
        A real lost net still prints and still fails. */}
    {TRANSPORT_SWITCHES.map(sw => (
      <B3F_4055
        key={sw.name}
        name={sw.name}
        pcbX={`${transportButtonX(sw.button)}mm`}
        pcbY={`${TRANSPORT_Y}mm`}
        layer="top"
        schSheetName="front"
        schSectionName="controls"
        noConnect={["pin2", "pin4"]}
        connections={{ pin1: "net.GND", pin3: sw.net }}
      />
    ))}
    <B3F_4055 name="SW6" pcbX={`${RIGHT_CONTROLS.SHIFT_EXIT.x}mm`} pcbY={`${RIGHT_CONTROLS.SHIFT_EXIT.y}mm`} layer="top" schSheetName="front" schSectionName="controls" noConnect={["pin2", "pin4"]} connections={{ pin1: "net.GND", pin3: "net.SHIFT_EXIT" }} />

    {FRONT_LED_PARTS.map((led, i) => (
      <Fragment key={led.name}>
        <R name={`R${13 + i}`} value="330" sec="controls" rot="180deg" x={`${ledPosition(transportButtonX(led.button)).x}mm`} y={`${LED_Y + 4.5}mm`} lcsc="C23138" mpn="0603WAF3300T5E" p1={led.drive} p2={led.anode} />
        <led
          name={led.name}
          pcbX={`${ledPosition(transportButtonX(led.button)).x}mm`}
          pcbY={`${LED_Y}mm`}
          layer="top"
          footprint="0805"
          schSheetName="front"
          schSectionName="controls"
          supplierPartNumbers={{ jlcpcb: [led.lcsc] }}
          manufacturerPartNumber={led.mpn}
          connections={{ anode: led.anode, cathode: "net.GND" }}
        />
      </Fragment>
    ))}
    {/* C5/C6 rotate 180 deg after the board widened and the interconnect moved:
        `tsci check placement` flagged both as "connections cross the routing path
        between its pads" at 0 deg. Value, net and footprint unchanged. */}
    <C name="C5" value="10uF" sec="power" x="49mm" y="19.9mm" rot="180deg" lcsc="C13585" mpn="CL31A106KBHNNNE" footprint="1206" p1="net.P3V3" p2="net.GND" />
    <C name="C6" value="100nF" sec="power" x="54.6mm" y="19.9mm" rot="180deg" lcsc="C14663" mpn="CC0603KRX7R9BB104" p1="net.P3V3" p2="net.GND" />

    {/* Direct 2 x 13 interconnect, FEMALE side: Sullins SFH11-PBPC-D13-ST-BK
        straight socket on the front board's BOTTOM face, mating the rear
        board's male box header with no plug and no ribbon. Its 26 pads carry
        INTERCONNECT_NETS, the same table the rear board uses, and
        lib/interconnect.test.ts proves on the built JSON that both boards put
        every net on the same physical pad. Physical fit, keying and pre-power
        continuity are pending (acceptance item interconnect_mate). */}
    <SFH11_PBPC_D13_ST_BK
      name="J5"
      pcbX={`${FRONT_INTERCONNECT.x}mm`}
      pcbY={`${FRONT_INTERCONNECT.y}mm`}
      pcbRotation={`${FRONT_INTERCONNECT.rotation}deg`}
      layer="bottom"
      schSheetName="front"
      schSectionName="headers"
      connections={INTERCONNECT_NETS}
    />

    {/* Panel POWER button (owner-approved): a plain momentary contact pair, no
        power circuitry. The two INNER contacts (pin1/pin3) go to J_PWR; the
        duplicated outer contacts (pin2/pin4) stay open and are declared
        noConnect, exactly like the transport row; the Omron B3F drawing
        (page 6) documents 1-2 and 3-4 as internally common pairs. From J_PWR
        the owner's 6-inch F-F DuPont jumpers leave the board. The Pi end is
        NOT a connector: the Raspberry Pi power-button documentation and its
        official images/j2.jpg (retrieved 2026-11-10) show J2 as two BARE PADS,
        so a fitted header or a qualified wired connection is needed there and
        its pitch, part, clearance, insulation and method are pending. Nothing
        here touches the 2 x 13 interconnect, ground or a rail. */}
    <B3F_4055
      name="SW7"
      pcbX={`${POWER_BUTTON.x}mm`}
      pcbY={`${POWER_BUTTON.y}mm`}
      pcbRotation={`${POWER_BUTTON.rotation}deg`}
      // "SW7" prints to the RIGHT of the body: 7 mm above it would be off the board.
      refTextAt={{ x: 9.2, y: 0 }}
      layer="top"
      schSheetName="front"
      schSectionName="controls"
      noConnect={["pin2", "pin4"]}
      connections={{ pin1: "net.PWR_SW_A", pin3: "net.PWR_SW_B" }}
    />
    {/* Direct 2 x 13 interconnect owner decision: J_PWR is now the right-angle
        Harwin M20-9960245 on the REAR (bottom) face at the middle of the long
        top relief notch, with its mating axis toward +Y into the notch. Its
        pads sit on board material below y = 48 mm, and the owner-approved
        bracket pocket clears the provisional keepout (lib/layout.ts
        POWER_HEADER, POWER_HEADER_KEEPOUT, BRACKET_POWER_POCKET). Sourced:
        2 ways, 2.54 mm pitch, Ø1.02 mm hole. ASSUMED and gated: the 1.60 mm
        pad, the housing offset (the 4.80 mm datum is ambiguous), the generic
        DuPont-style plug, access and the UMC AUDIO cable tail. */}
    <M20_9960245
      name="J_PWR"
      pcbX={`${POWER_HEADER.x}mm`}
      pcbY={`${POWER_HEADER.y}mm`}
      pcbRotation={`${POWER_HEADER.rotation}deg`}
      layer={POWER_HEADER.layer}
      schSheetName="front"
      schSectionName="headers"
      connections={{ pin1: "net.PWR_SW_A", pin2: "net.PWR_SW_B" }}
    />

    {/* Silkscreen wave: brand lockup, hand-solder hints, pin-1 and polarity
        marks. Every position and its clearance proof live in lib/silk.tsx. */}
    <FrontSilk />

    {/* Panel-side ground pour. Every SMD part is on the top layer and J5 (THT
        female) is on the bottom, so the free bottom copper is the ground
        plane. */}
    <copperpour name="GND_POUR_BOTTOM" layer="bottom" connectsTo="net.GND" boardEdgeMargin="0.3mm" />
  </board>
)
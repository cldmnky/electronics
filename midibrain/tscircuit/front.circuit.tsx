import { Fragment } from "react"
import { A_1N4148WS } from "./imports/A_1N4148WS"
import { C15127_AO3401A } from "./imports/C15127_AO3401A"
import { B3F_4055 } from "./imports/B3F_4055"
import { PEC11R_4015F_S0024 } from "./imports/PEC11R_4015F_S0024"
import { BOARD_POLICY } from "./lib/electrical"
import { M20_9960245 } from "./imports/M20_9960245"
import { INTERCONNECT_NETS, SFH11_PBPC_D13_ST_BK } from "./lib/interconnect"
import { MIDI_OUT_SERIES_OHM, MIDI_OUT_GATE_OHM, MIDI_RECEIVER_OHM } from "./lib/electrical-midi"
import { FP_JACK_TRS } from "./lib/jack"
import { SKRHABE010 } from "./imports/SKRHABE010"
import { FrontSilk } from "./lib/silk"
import { TLP2361_TPL_E } from "./imports/TLP2361_TPL_E"
import {
  DISPLAY_ANNOTATIONS,
  FEEDTHROUGH_BRACKET,
  FRONT_BOARD_HOLES,
  FRONT_INTERCONNECT,
  FRONT_REAR_MOUNT,
  FRONT_PCB,
  FRONT_TOP_NOTCH,
  JACK_Y,
  LCD_MOUNT_HOLES,
  LCD_WINDOW,
  LED_Y,
  ledPosition,
  MOUNT_HOLE_DIAMETER,
  POWER_BUTTON,
  POWER_HEADER,
  midiJackX,
  RIGHT_CONTROLS,
  DISPLAY_CONCEPT,
  DISPLAY_STACK,
  TRANSPORT_Y,
  transportButtonX,
  displayAnnotationXY,
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
const MidiIn = ({ ch, jackX, jackY, x, y }: { ch: 1 | 2; jackX: number; jackY: number; x: number; y: number }) => {
  const j = `J${ch}`, u = `U${ch}`, d = `D${ch}`
  const rx = `${x}mm`, ry = `${y}mm`
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
      <TLP2361_TPL_E
        name={u}
        pcbX={rx}
        pcbY={ry}
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
        pcbX={`${x + 8}mm`}
        pcbY={`${y + 2}mm`}
        layer="top"
        schSheetName="front"
        schSectionName="midi_in"
        connections={{ cathode: anode, anode: tip }}
      />
      <R name={`R${ch}`} value={`${MIDI_RECEIVER_OHM}`} sec="midi_in" schX={ch === 1 ? "-21.5" : undefined} schY={ch === 1 ? "-1.3" : undefined} x={`${x - 2}mm`} y={`${y - 6}mm`} lcsc="C22962" mpn="0603WAF2200T5E" p1={ring} p2={anode} />
      <R name={`R${ch + 2}`} value="2.2k" sec="midi_in" x={`${x + 8}mm`} y={`${y - 4}mm`} lcsc="C4190" mpn="0603WAF2201T5E" p1={rxNet} p2="net.P3V3" />
      {/* C2 carries the placement note ("connections cross the routing path
          between its pads"), C1 does not; a 0603 X7R cap is non-polarized, so
          rotating it moves no polarity mark. */}
      <C name={`C${ch}`} value="100nF" sec="midi_in" x={`${jackX - 7}mm`} y={`${jackY + 5.5}mm`} rot={ch === 2 ? "180deg" : undefined} lcsc="C14663" mpn="CC0603KRX7R9BB104" p1={`net.MIDI_IN_${ch}_SLEEVE`} p2="net.GND" />
      <C name={`C${ch + 2}`} value="100nF" sec="midi_in" x={`${x + 4}mm`} y={`${y + 3}mm`} lcsc="C14663" mpn="CC0603KRX7R9BB104" p1="net.P3V3" p2="net.GND" />
    </>
  )
}

const MidiOut = ({ ch, jackX, jackY, x, y }: { ch: 1 | 2; jackX: number; jackY: number; x: number; y: number }) => {
  const j = `J${ch + 2}`, q = `Q${ch}`
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
        pcbX={`${x}mm`}
        pcbY={`${y}mm`}
        layer="top"
        schSheetName="front"
        schSectionName="midi_out"
        connections={{ gate, source: "net.P3V3", drain }}
      />
      <R name={`R${ch + 4}`} value={`${MIDI_OUT_GATE_OHM.series}`} sec="midi_out" x={`${x - 2}mm`} y={`${y + 6}mm`} lcsc="C22775" mpn="0603WAF1000T5E" p1={tx} p2={gate} />
      <R name={`R${ch + 6}`} value={`${MIDI_OUT_GATE_OHM.pullup}`} sec="midi_out" x={`${x + 6}mm`} y={`${y + 6}mm`} lcsc="C4190" mpn="0603WAF2201T5E" p1={gate} p2="net.P3V3" />
      {/* R9/R10 rotate 270 deg, not 0/90: `tsci check placement` flagged the
          90 deg pads as "connections cross the routing path between its pads",
          and 270 puts pad 1 on the routed side. Value, net and footprint are
          unchanged. */}
      <R name={`R${ch + 8}`} value={`${MIDI_OUT_SERIES_OHM.ring}`} sec="midi_out" x={`${x - 2}mm`} y={`${y - 6}mm`} lcsc="C53728" mpn="25121WF330JT4E" footprint="2512" rot="270deg" p1={drain} p2={ring} />
      {/* R11/R12 rotate 180 deg after the cluster moved to x = -49 for the
          4.3-inch module's LCD hole: `tsci check placement` flags their pads as
          "connections cross the routing path between its pads". Value, net and
          footprint are unchanged. */}
      <R name={`R${ch + 10}`} value={`${MIDI_OUT_SERIES_OHM.tip}`} sec="midi_out" x={`${x + 4.5}mm`} y={`${y - 6}mm`} rot="180deg" lcsc="C118189" mpn="25121WF220JT4E" footprint="2512" p1={tip} p2="net.GND" />
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

// The display envelope as twelve 0.5 mm rods: a wireframe box. A solid box of
// the same size hides the SMT area underneath and reads as a second board,
// while this is only a reference - the module's underside map, FPC exit and
// installed height are unverified (display_fpc_idc, display_boss_pattern).
// Every dimension comes from DISPLAY_STACK; nothing here is re-typed.
const DISPLAY_ROD = 0.5
const displayEnvelopeWireframe = (() => {
  const w = DISPLAY_STACK.outlineWidth
  const d = DISPLAY_STACK.outlineHeight
  const h = DISPLAY_STACK.overallDepth
  const hx = w / 2 - DISPLAY_ROD / 2
  const hy = d / 2 - DISPLAY_ROD / 2
  const hz = h / 2 - DISPLAY_ROD / 2
  const rods: { size: number[]; at: number[] }[] = []
  for (const sy of [-1, 1]) for (const sz of [-1, 1]) rods.push({ size: [w, DISPLAY_ROD, DISPLAY_ROD], at: [0, sy * hy, sz * hz] })
  for (const sx of [-1, 1]) for (const sz of [-1, 1]) rods.push({ size: [DISPLAY_ROD, d, DISPLAY_ROD], at: [sx * hx, 0, sz * hz] })
  for (const sx of [-1, 1]) for (const sy of [-1, 1]) rods.push({ size: [DISPLAY_ROD, DISPLAY_ROD, h], at: [sx * hx, sy * hy, 0] })
  return {
    type: "union",
    shapes: rods.map(r => ({ type: "translate", vector: r.at, shape: { type: "cuboid", size: r.size } })),
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
    <cutout shape="rect" width={`${FRONT_TOP_NOTCH.width}mm`} height={`${FRONT_TOP_NOTCH.height}mm`} pcbX={`${FRONT_TOP_NOTCH.x}mm`} pcbY={`${FRONT_TOP_NOTCH.y}mm`} />
    {FRONT_BOARD_HOLES.map((h, i) => (
      <Fragment key={`board-hole-${i}`}><hole name={`BOARD_${i + 1}`} diameter={`${MOUNT_HOLE_DIAMETER}mm`} pcbX={`${h.x}mm`} pcbY={`${h.y}mm`} /></Fragment>
    ))}
    {FRONT_REAR_MOUNT.map((h, i) => (
      <Fragment key={`mount-hole-${i}`}><hole name={`MOUNT_${i + 1}`} diameter={`${MOUNT_HOLE_DIAMETER}mm`} pcbX={`${h.x}mm`} pcbY={`${h.y}mm`} /></Fragment>
    ))}
    {LCD_MOUNT_HOLES.map((h, i) => (
      <Fragment key={`lcd-hole-${i}`}><hole name={`LCD_${i + 1}`} diameter={`${MOUNT_HOLE_DIAMETER}mm`} pcbX={`${h.x}mm`} pcbY={`${h.y}mm`} /></Fragment>
    ))}
    {/* LCD assembly silkscreen: vendor outline, active-area window and hole pattern. */}
    <silkscreenrect pcbX={`${DISPLAY_CONCEPT.x}mm`} pcbY={`${DISPLAY_CONCEPT.y}mm`} width={`${DISPLAY_CONCEPT.width}mm`} height={`${DISPLAY_CONCEPT.height}mm`} strokeWidth="0.15mm" layer="top" />
    <silkscreenrect pcbX={`${LCD_WINDOW.x}mm`} pcbY={`${LCD_WINDOW.y}mm`} width={`${LCD_WINDOW.width}mm`} height={`${LCD_WINDOW.height}mm`} strokeWidth="0.12mm" layer="top" />
    {LCD_MOUNT_HOLES.map((h, i) => (
      <Fragment key={`lcd-silk-${i}`}><silkscreencircle pcbX={`${h.x}mm`} pcbY={`${h.y}mm`} radius="1.8mm" strokeWidth="0.15mm" layer="top" /></Fragment>
    ))}
    {DISPLAY_ANNOTATIONS.map((a, i) => {
      const p = displayAnnotationXY(a)
      return (
        <Fragment key={`lcd-note-${i}`}>
          <silkscreentext text={a.text} pcbX={`${p.x}mm`} pcbY={`${p.y}mm`} fontSize={`${a.fontSize}mm`} anchorAlignment="center" layer="top" />
        </Fragment>
      )
    })}
    {/* Display reference body for the 3D view (tsci dev, GLB, STEP). The
        module bolts to the four SMTSO bosses but is NOT a board part, so this
        is the only way tscircuit can draw it: the engine skips CAD for a
        doNotPlace part, and a standalone <cadmodel> has no parent component.
        The box is the vendor envelope from DISPLAY_STACK and nothing else -
        106.00 x 68.00 x 14.05 mm, rear face at planeSeparation -
        bossProjection = 5.0 mm above the board's top face, glass front on the
        panel-front plane of PROVISIONAL_Z. It is drawn as a 0.5 mm wireframe
        cage, not a solid slab, so the SMT parts beneath it stay visible.
        REFERENCE BOX, NOT A FIT: the
        underside map, the FPC exit, the boss projection and the installed
        height are unverified (display_fpc_idc, display_boss_pattern); the
        envelope is the vendor outline, the depth is the wiki FAQ's 14.05 mm.

        CONSEQUENCE: because it is not `doNotPlace` it is a placement. It has no
        pads, so it stays off the SMT BOM and CPL (sourcing_bom.py classifies it
        THT for the front board) and appears in the hand BOM instead - which is
        correct, the module is hand-fitted and already was in sourcing-extras. */}
    <chip
      name="DISP1"
      layer="top"
      showAsTranslucentModel
      manufacturerPartNumber="Waveshare 4.3inch DSI LCD"
      pcbX={`${DISPLAY_CONCEPT.x}mm`}
      pcbY={`${DISPLAY_CONCEPT.y}mm`}
      footprint={<footprint name="display-module-envelope" />}
      cadModel={{
        jscad: displayEnvelopeWireframe,
        positionOffset: { x: 0, y: 0, z: DISPLAY_STACK.overallDepth / 2 },
        zOffsetFromSurface: DISPLAY_STACK.planeSeparation - DISPLAY_STACK.bossProjection,
        size: {
          x: DISPLAY_STACK.outlineWidth,
          y: DISPLAY_STACK.outlineHeight,
          z: DISPLAY_STACK.overallDepth,
        },
        showAsTranslucentModel: true,
      }}
    />
    {FEEDTHROUGH_BRACKET.mountX.map((x, i) => (
      <Fragment key={`bracket-${i}`}><hole name={`BRACKET_${i + 1}`} diameter={`${MOUNT_HOLE_DIAMETER}mm`} pcbX={`${x}mm`} pcbY={`${FEEDTHROUGH_BRACKET.mountY}mm`} /></Fragment>
    ))}

    {/* All SMT on the panel side for single-sided JLCPCB assembly: MIDI
        opto/output drivers under the LCD (top layer); jacks in the four
        shared transport/MIDI columns at the bottom edge. J5 stays bottom: the
        female socket's body is on the rear-facing side so the rear board's
        male header plugs straight into it, and its solder tails are the only
        through-hole joints on the panel side. */}
    <MidiIn ch={1} jackX={midiJackX("MIDI IN 1")} jackY={JACK_Y} x={-45} y={10} />
    <MidiIn ch={2} jackX={midiJackX("MIDI IN 2")} jackY={JACK_Y} x={-15} y={10} />
    {/* ch1 cluster shifted 4 mm left (x -45 -> -49) to clear the 4.3-inch
        module's bottom-left LCD_4 hole at (-41.88, -13.54), which the new
        58.00 x 49.04 mounting pattern puts inside the old Q1 courtyard. */}
    <MidiOut ch={1} jackX={midiJackX("MIDI OUT 1")} jackY={JACK_Y} x={-49} y={-13} />
    <MidiOut ch={2} jackX={midiJackX("MIDI OUT 2")} jackY={JACK_Y} x={-15} y={-13} />

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
    <C name="C5" value="10uF" sec="power" x="50mm" y="46mm" lcsc="C13585" mpn="CL31A106KBHNNNE" footprint="1206" p1="net.P3V3" p2="net.GND" />
    <C name="C6" value="100nF" sec="power" x="60mm" y="-20mm" lcsc="C14663" mpn="CC0603KRX7R9BB104" p1="net.P3V3" p2="net.GND" />

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
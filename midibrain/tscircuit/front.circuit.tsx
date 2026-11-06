import { Fragment } from "react"
import { A_1N4148WS } from "./imports/A_1N4148WS"
import { C15127_AO3401A } from "./imports/C15127_AO3401A"
import { B3F_4055 } from "./imports/B3F_4055"
import { BH254V_26P } from "./imports/BH254V_26P"
import { PEC11R_4015F_S0024 } from "./imports/PEC11R_4015F_S0024"
import { BOARD_POLICY } from "./lib/electrical"
import { MIDI_OUT_SERIES_OHM, MIDI_OUT_GATE_OHM, MIDI_RECEIVER_OHM } from "./lib/electrical-midi"
import { FP_JACK_TRS } from "./lib/jack"
import { SKRHABE010 } from "./imports/SKRHABE010"
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
  MOUNT_HOLE_DIAMETER,
  midiJackX,
  RIGHT_CONTROLS,
  DISPLAY_CONCEPT,
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

type CProps = { name: string; value: string; x: string; y: string; lcsc: string; mpn: string; p1: string; p2: string; footprint?: string; sec: string }
const C = ({ name, value, x, y, lcsc, mpn, p1, p2, footprint = "0603", sec }: CProps) => (
  <capacitor
    name={name}
    capacitance={value}
    footprint={footprint}
    pcbX={x}
    pcbY={y}
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
        pinLabels={{ pin1: "tip", pin2: "ring", pin3: "sleeve" }}
        manufacturerPartNumber="PJ-301M-3-B"
        supplierPartNumbers={{ jlcpcb: ["C381127"] }}
        pcbX={`${jackX}mm`}
        pcbY={`${jackY}mm`}
        layer="top"
        schSheetName="front"
        schSectionName="midi_in"
        connections={{ tip, ring, sleeve: `net.MIDI_IN_${ch}_SLEEVE` }}
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
      <C name={`C${ch}`} value="100nF" sec="midi_in" x={`${jackX - 7}mm`} y={`${jackY + 5.5}mm`} lcsc="C14663" mpn="CC0603KRX7R9BB104" p1={`net.MIDI_IN_${ch}_SLEEVE`} p2="net.GND" />
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
        pinLabels={{ pin1: "tip", pin2: "ring", pin3: "sleeve" }}
        manufacturerPartNumber="PJ-301M-3-B"
        supplierPartNumbers={{ jlcpcb: ["C381127"] }}
        pcbX={`${jackX}mm`}
        pcbY={`${jackY}mm`}
        layer="top"
        schSheetName="front"
        schSectionName="midi_out"
        connections={{ tip, ring, sleeve: "net.GND" }}
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
      <R name={`R${ch + 8}`} value={`${MIDI_OUT_SERIES_OHM.ring}`} sec="midi_out" x={`${x - 2}mm`} y={`${y - 6}mm`} lcsc="C53728" mpn="25121WF330JT4E" footprint="2512" rot="90deg" p1={drain} p2={ring} />
      <R name={`R${ch + 10}`} value={`${MIDI_OUT_SERIES_OHM.tip}`} sec="midi_out" x={`${x + 4.5}mm`} y={`${y - 6}mm`} lcsc="C118189" mpn="25121WF220JT4E" footprint="2512" p1={tip} p2="net.GND" />
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

// LEDs above REC / OVER DUB / PLAY, with their 330R series resistors 4 mm left.
// Anode nets and LCSC parts unchanged from the routed prototype.
const FRONT_LED_PARTS = [
  { name: "D3", button: "REC", lcsc: "C2295", mpn: "KT-0805R", drive: "net.REC_LED", anode: "net.REC_LED_A" },
  { name: "D4", button: "OVER DUB", lcsc: "C2295", mpn: "KT-0805R", drive: "net.OVERDUB_LED", anode: "net.OVERDUB_LED_A" },
  { name: "D5", button: "PLAY", lcsc: "C2297", mpn: "KT-0805G", drive: "net.PLAY_LED", anode: "net.PLAY_LED_A" },
] as const

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
    {FEEDTHROUGH_BRACKET.mountX.map((x, i) => (
      <Fragment key={`bracket-${i}`}><hole name={`BRACKET_${i + 1}`} diameter={`${MOUNT_HOLE_DIAMETER}mm`} pcbX={`${x}mm`} pcbY={`${FEEDTHROUGH_BRACKET.mountY}mm`} /></Fragment>
    ))}

    {/* All SMT on the panel side for single-sided JLCPCB assembly: MIDI
        opto/output drivers under the LCD (top layer); jacks in the four
        shared transport/MIDI columns at the bottom edge. J5 stays bottom:
        its THT body must mate rearward into the front-rear ribbon. */}
    <MidiIn ch={1} jackX={midiJackX("MIDI IN 1")} jackY={JACK_Y} x={-45} y={10} />
    <MidiIn ch={2} jackX={midiJackX("MIDI IN 2")} jackY={JACK_Y} x={-15} y={10} />
    <MidiOut ch={1} jackX={midiJackX("MIDI OUT 1")} jackY={JACK_Y} x={-45} y={-13} />
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
    {TRANSPORT_SWITCHES.map(sw => (
      <B3F_4055
        key={sw.name}
        name={sw.name}
        pcbX={`${transportButtonX(sw.button)}mm`}
        pcbY={`${TRANSPORT_Y}mm`}
        layer="top"
        schSheetName="front"
        schSectionName="controls"
        connections={{ pin1: "net.GND", pin3: sw.net }}
      />
    ))}
    <B3F_4055 name="SW6" pcbX={`${RIGHT_CONTROLS.SHIFT_EXIT.x}mm`} pcbY={`${RIGHT_CONTROLS.SHIFT_EXIT.y}mm`} layer="top" schSheetName="front" schSectionName="controls" connections={{ pin1: "net.GND", pin3: "net.SHIFT_EXIT" }} />

    {FRONT_LED_PARTS.map((led, i) => (
      <Fragment key={led.name}>
        <R name={`R${13 + i}`} value="330" sec="controls" x={`${transportButtonX(led.button) - 4}mm`} y={`${LED_Y}mm`} lcsc="C23138" mpn="0603WAF3300T5E" p1={led.drive} p2={led.anode} />
        <led
          name={led.name}
          pcbX={`${transportButtonX(led.button)}mm`}
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
    <C name="C6" value="100nF" sec="power" x="60mm" y="-12mm" lcsc="C14663" mpn="CC0603KRX7R9BB104" p1="net.P3V3" p2="net.GND" />

    <BH254V_26P
      name="J5"
      pcbX={`${FRONT_INTERCONNECT.x}mm`}
      pcbY={`${FRONT_INTERCONNECT.y}mm`}
      pcbRotation={`${FRONT_INTERCONNECT.rotation}deg`}
      layer="bottom"
      schSheetName="front"
      schSectionName="headers"
      connections={{
        pin1: "net.P3V3",
        pin2: "net.GND",
        pin3: "net.MIDI_IN_1_RX",
        pin4: "net.MIDI_OUT_1_TX",
        pin5: "net.MIDI_IN_2_RX",
        pin6: "net.MIDI_OUT_2_TX",
        pin7: "net.NAV_UP",
        pin8: "net.NAV_DOWN",
        pin9: "net.NAV_LEFT",
        pin10: "net.NAV_RIGHT",
        pin11: "net.NAV_PRESS",
        pin12: "net.ENC_A",
        pin13: "net.ENC_B",
        pin14: "net.ENC_SW",
        pin15: "net.REC",
        pin16: "net.OVER_DUB",
        pin17: "net.STOP",
        pin18: "net.PLAY",
        pin19: "net.PLAY_START",
        pin20: "net.REC_LED",
        pin21: "net.OVERDUB_LED",
        pin22: "net.P3V3",
        pin23: "net.PLAY_LED",
        pin24: "net.GND",
        pin25: "net.GND",
        pin26: "net.SHIFT_EXIT",
      }}
    />

    {/* Panel-side ground pour. Every SMD part is on the top layer and J5 (THT)
        is on the bottom, so the free bottom copper is the ground plane. */}
    <copperpour name="GND_POUR_BOTTOM" layer="bottom" connectsTo="net.GND" boardEdgeMargin="0.3mm" />
  </board>
)
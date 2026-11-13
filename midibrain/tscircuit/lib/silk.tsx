// Board silkscreen for both PCBAs: brand artwork, hand-solder hints, reference
// designators for the parts that lack one and the pin-1/polarity marks.
//
// ONE source of truth for every silkscreen position on the two boards, in
// board millimetres (+X right, +Y up, origin at the board centre). Positions
// that lib/layout.ts already owns (jack, switch, LED and control columns) are
// DERIVED here, never restated; everything else is a literal that is measured
// against the built pads by `lib/silk.test.ts`, exactly like the Froemix
// `test_silk.test.ts` discipline. Nothing in this file is a fabrication
// approval: the release's Legend-Gerber parse is the authority for bounds, and
// the prints still need a first article.
//
// Rules that the tests enforce (see lib/silk.test.ts):
//  * every mark is at least SILK_GERBER_GUARD inside the routed outline (the
//    release's Gerber parse pads each mark by 0.5 mm on top of its own 0.5 mm
//    guard, so 1.0 mm is what the release actually requires);
//  * every mark clears pads, plated holes and bare holes by SILK_CLEARANCE;
//  * no silkscreen text box overlaps another one (text over text is unreadable
//    at 0.7 mm, so the free zones are mapped before anything is placed);
//  * every hand-soldered through-hole part carries a reference designator and a
//    hint at its own position, and every IC and multi-pin connector carries a
//    pin-1 mark beside pin 1;
//  * the wordmark reuses the panel letterforms (`lib/panel-art.ts`) at board
//    scale with 0.7 mm stems, so it reads as bold white-on-green silk.
//
// Bottom-layer artwork is MIRRORED by the exporter, so bottom-layer paths are
// mirrored here about their own centre to read correctly from the rear, and
// bottom-layer text is anchored `center` only: `center_left` lays the glyphs
// backwards out of the board (the 2026-11 panel title, docs/decisions.md).
//
// Anchor rule, same as the panel: tscircuit's text advance is measured at
// ~0.69 x fontSize per glyph (lib/layout.ts SILK_ADVANCE_RATIO); `textBounds`
// uses that ratio so the checks measure the ink the exporter prints.

import { Fragment } from "react"
import {
  FRONT_COLUMNS,
  FRONT_INTERCONNECT,
  FRONT_PCB,
  JACK_Y,
  LED_Y,
  ledPosition,
  REAR_PCB,
  POWER_BUTTON,
  POWER_HEADER,
  REAR_INTERCONNECT,
  RIGHT_CONTROLS,
  SILK_ADVANCE_RATIO,
  TRANSPORT_Y,
  midiJackX,
  transportButtonX,
} from "./layout"
import { WORDMARK_TEXT, textPaths, unionBounds, type Bounds, type Pt } from "./panel-art"
import { oshwPaths } from "./oshw"
import { PROBE_LABEL_FONT_MM, PROBE_POINTS, probeLabelPosition } from "./probe-points"

/** Drawn ink clearance kept between silkscreen and any pad, hole or other ink. */
export const SILK_CLEARANCE = 0.2
/**
 * Distance every silk coordinate keeps from the board edge. The release gate
 * parses the Legend Gerbers and pads each mark by a fixed 0.5 mm (its aperture
 * bookkeeping is conservative by design), on top of its own 0.5 mm guard, so a
 * coordinate must sit 1.0 mm inside the outline to pass. Measured, not guessed:
 * the first rear-board draft put a pin-1 dot at x = -50.0 and the gate reported
 * the board OFF-BOARD.
 */
export const SILK_GERBER_GUARD = 1.0
/** Pin-1 dot: radius of the drawn circle and the stroke that draws it. */
export const PIN1_DOT_RADIUS = 0.2
export const SILK_MARK_STROKE = 0.2
/** Board wordmark: panel letterforms, board scale, bold stems. */
export const BOARD_WORDMARK_CAP = 2.8
export const BOARD_WORDMARK_STROKE = 0.7
/** OSHW gear: width of the mark and the scanline fill aperture. */
export const OSHW_WIDTH = 6.4
export const OSHW_STROKE = 0.2

export type SilkTextRole = "ref" | "hint" | "pin1" | "polarity" | "note"

/** A text mark; `part` names the component it belongs to, when it has one. */
export type SilkTextMark = {
  text: string
  x: number
  y: number
  fontSize: number
  role: SilkTextRole
  part?: string
  /** CCW rotation in degrees. 90 is used for the left-strip spine: after the
   *  display opening took the middle of the board the only generous free zone
   *  on the front face is the 11.75 mm strip left of the opening, and long
   *  notes only fit there turned on their side. */
  rotation?: number
}

/**
 * A pin-1 index mark. `silk` marks are emitted by this module; `footprint`
 * marks are drawn by the part's own footprint and are listed here so one table
 * still proves every pin 1 is marked. Nothing on the front board qualifies any
 * more: the optos' vendor footprint turned out to draw no mark at all.
 */
export type Pin1Mark = { ref: string; x: number; y: number; source: "silk" | "footprint" }

export type WordmarkSpec = { x: number; y: number; capHeight: number; stroke: number; rotation?: number }
export type OshwSpec = { x: number; y: number; width: number; stroke: number; rotation?: number }

export type BoardSilkSpec = {
  board: "front" | "rear"
  layer: "top" | "bottom"
  width: number
  height: number
  wordmark: WordmarkSpec
  oshw: OshwSpec
  pin1: Pin1Mark[]
  texts: SilkTextMark[]
}

// ---- Front/UI board ----
// 2026-11-13, after the display left this board: where silk may go changed
// completely, so the free zones were re-mapped from the routed outline rather
// than nudged:
//  * the display opening (x -63.25...43.25, y -23.45...45.45) is a HOLE. The
//    board wordmark used to sit at (-20, 43.5), which is inside that hole - it
//    printed 119 stroke paths into thin air. The strip between the opening's
//    top edge and the relief notch is 2.55 mm, less than the 2.8 mm cap, so the
//    top of the board is unusable for identity;
//  * the LEFT strip (x -75...-63.25, 11.75 mm wide, 112 mm tall) is now
//    completely empty of parts - J5, SW7 and J_PWR all live on the right - so
//    the wordmark, the OSHW gear and the long legends live there, turned 90
//    degrees. That is the only place a 23 mm wordmark fits on this board;
//  * the right strip has pockets: (44...58, 37...44) under the POWER button,
//    x 70...74 beside J5, and the gaps between the MIDI clusters;
//  * the bottom strip belongs to the jack field, the transport row and the
//    cluster passives - only the note above the opening's bottom edge and the
//    per-part marks go there.
const FRONT_LEFT_STRIP_X = -69.2 // centre of the empty left strip, 5.8 mm from either edge
// Pin 1 is the SLEEVE pad on this part (QingPu/Thonkiconn WQP419GR: pads are
// S / R / T, and lib/jack.tsx labels them that way). The vendor schematic also
// numbers 1 = sleeve. The dot therefore sits at the sleeve pad, 6.48 mm below
// the barrel centre - it used to sit at the tip pad when the footprint called
// pin 1 the tip.
const FRONT_JACK_PIN1_DY = -6.48 // lib/jack.tsx: pin 1 (sleeve) below the barrel centre
const FRONT_JACK_PIN1_DX = -2.2  // clear of the sleeve's 3.1 mm land, which is 1.55 mm half-width
const FRONT_LED_PAD_DX = 1.0 // 0805 anode/cathode pads sit +-0.91 mm from centre
const FRONT_LED_MARK_DY = -1.6

const frontJacks = FRONT_COLUMNS.map((column, i) => ({
  ref: `J${i + 1}`,
  x: midiJackX(column.jack),
}))
const frontSwitches = [
  ...FRONT_COLUMNS.map((column, i) => ({ ref: `SW${i + 1}`, x: transportButtonX(column.button), y: TRANSPORT_Y })),
  { ref: "SW5", x: transportButtonX("PLAY START"), y: TRANSPORT_Y },
  { ref: "SW6", x: RIGHT_CONTROLS.SHIFT_EXIT.x, y: RIGHT_CONTROLS.SHIFT_EXIT.y },
  { ref: "SW7", x: POWER_BUTTON.x, y: POWER_BUTTON.y },
]
// REC / OVER DUB / PLAY indicators: same positions as the panel light pipes,
// i.e. 12 mm BESIDE the transport button, so the polarity letters follow
// ledPosition, never the button column.
const frontLeds = [
  { ref: "D3", x: ledPosition(transportButtonX("REC")).x },
  { ref: "D4", x: ledPosition(transportButtonX("OVER DUB")).x },
  { ref: "D5", x: ledPosition(transportButtonX("PLAY")).x },
]
// MIDI cluster placements, ONE pair per cluster (front.circuit.tsx <MidiIn> and
// <MidiOut> take them as props). Every mark below that belongs to a cluster -
// reference designators, pin-1 dots for the optos, the SOT-23 gates and the
// 1N4148WS cathodes - is derived from these, so moving a cluster can never
// strand its silk on the old spot (it did, twice, while the 10 mm respin was in
// progress). Keep in step with front.circuit.tsx.
const FRONT_CLUSTERS = {
  in1: { u: { x: 51, y: -48 } },
  in2: { u: { x: 64.6, y: -30 } },
  out1: { q: { x: 66.6, y: -46.05 }, d: { x: 59, y: -46 } },
  out2: { q: { x: 66.6, y: -8 }, d: { x: 72.6, y: -28 } },
} as const
// SOT-23 pin 1 sits at (-1.14, +0.95) from the gate's centre on this footprint
// (measured from the built pads: pin1 65.46,-45.10 with the body at 66.6,-46.05).
const FRONT_GATE_PIN1 = { dx: -1.14, dy: 0.95 }
const FRONT_GATE_DOT = { dx: -1.4, dy: 0 }
const FRONT_GATE_REF_OFFSET = { dx: 2.1, dy: 1.6 }
// 1N4148WS: cathode pad is 1.17 mm LEFT of the body centre, anode 1.17 right.
const FRONT_DIODE_CATHODE_DX = -1.17
const FRONT_DIODE_K_OFFSET = { dx: -2.4, dy: 0 }

// Known and accepted, measured 2026-11-13 on the built board: four VENDOR
// footprints draw their own silk across their own pads - AO3401A's two body
// verticals (24% and 19% of each segment inside pin 3 / pin 1), the PEC11R's two
// 0.70 mm nicks at pins 5 and 3 (5% each) and the M20-9960245 housing edge at
// pin 1 (19%). They are library geometry, not this table's ink: the fab clips
// silk over exposed copper, so they print as clean outlines. Redrawing them is
// a footprint change that has to be re-verified against the datasheets, so it is
// deliberately NOT done here. Everything this module draws is clean: 90 marks,
// 0 off-board, 0 inside either cutout, 0 over a pad, 0 text over text.
export const FRONT_SILK: BoardSilkSpec = {
  board: "front",
  layer: "top",
  width: FRONT_PCB.width,
  height: FRONT_PCB.height,
  // Wordmark and gear read bottom-to-top in the empty left strip.
  wordmark: { x: FRONT_LEFT_STRIP_X, y: 30, capHeight: BOARD_WORDMARK_CAP, stroke: BOARD_WORDMARK_STROKE, rotation: 90 },
  oshw: { x: FRONT_LEFT_STRIP_X, y: 8, width: OSHW_WIDTH, stroke: OSHW_STROKE, rotation: 90 },
  pin1: [
    // TLP2361 optos: the vendor footprint draws NO pin-1 mark (its silk is a
    // plain body outline with pad gaps - checked in imports/TLP2361_TPL_E.tsx,
    // which has zero circles and no chamfer), so this module draws the dot.
    // Pin 1 is at +3.15/-1.27 from the body centre on this footprint; the dot
    // sits a further 1.3 mm out, clear of the pill pad.
    ...(["in1", "in2"] as const).map((k) => ({
      ref: k === "in1" ? "U1" : "U2",
      x: FRONT_CLUSTERS[k].u.x + 3.15 + 1.3,
      y: FRONT_CLUSTERS[k].u.y - 1.27 - 1.3,
      source: "silk" as const,
    })),
    // SOT-23 gates: dot outboard of pin 1.
    ...(["out1", "out2"] as const).map((k) => ({
      ref: k === "out1" ? "Q1" : "Q2",
      x: FRONT_CLUSTERS[k].q.x + FRONT_GATE_PIN1.dx + FRONT_GATE_DOT.dx,
      y: FRONT_CLUSTERS[k].q.y + FRONT_GATE_PIN1.dy + FRONT_GATE_DOT.dy,
      source: "silk" as const,
    })),
    // Derived from the control column, which owner decision D2 moved 47 -> 52
    // and the 2026-11-13 column move dropped to y 20/1/-18.
    { ref: "U5", x: RIGHT_CONTROLS.NAV.x - 4.9, y: RIGHT_CONTROLS.NAV.y + 1.43, source: "silk" },
    { ref: "U6", x: RIGHT_CONTROLS.TEMPO.x - 4.0, y: RIGHT_CONTROLS.TEMPO.y + 7.9, source: "silk" },
    // TRS jacks: pin 1 is the sleeve contact, the pad furthest from the panel.
    ...frontJacks.map(({ ref, x }) => ({ ref, x: x + FRONT_JACK_PIN1_DX, y: JACK_Y + FRONT_JACK_PIN1_DY, source: "silk" as const })),
    // 2 x 13 direct interconnect, female socket on the bottom face. Pin 1 is the
    // +X end of the top row; the dot sits above it, clear of the J_PWR keepout.
    { ref: "J5", x: FRONT_INTERCONNECT.x + 2.6, y: FRONT_INTERCONNECT.y + 16.3, source: "silk" },
    // J_PWR 1x2 right-angle header: pin 1 is the -X pad.
    { ref: "J_PWR", x: POWER_HEADER.x - 1.9, y: POWER_HEADER.y + 1.7, source: "silk" },
    // B3F-4055 buttons: pin 1 is the +X, -Y pad. Marked because the owner asked
    // for pin 1 on every multi-pin part; note the switch is symmetric (1-2 and
    // 3-4 are internally common), so this is placement aid, not polarity.
    ...frontSwitches.map(({ ref, x, y }) => ({ ref, x: x + 6.25, y: y - 4.2, source: "silk" as const })),
  ],
  texts: [
    // ---- left strip: identity and legends, turned on their side ----
    { text: "C1-C6: MLCC, NO POLARITY", x: FRONT_LEFT_STRIP_X, y: -3, fontSize: 0.7, role: "note", rotation: 90 },
    { text: "HAND THT: J1-4 SW1-7 J5 J_PWR", x: FRONT_LEFT_STRIP_X, y: -21, fontSize: 0.7, role: "note", rotation: 90 },
    { text: "MIDI BRAIN FRONT/UI", x: FRONT_LEFT_STRIP_X, y: -40, fontSize: 1.0, role: "note", rotation: 90 },

    // ---- right strip, pocket under the POWER button ----
    { text: "POWER", x: 52.5, y: 43.0, fontSize: 1.4, role: "note" },
    // SW7 is the Pi's remote power button: a plain contact pair, no power
    // circuitry. Press = power on / graceful shutdown request.
    { text: "PRESS=ON", x: 52.5, y: 41.4, fontSize: 0.7, role: "hint", part: "SW7" },
    { text: "HOLD=OFF", x: 52.5, y: 40.2, fontSize: 0.7, role: "hint", part: "SW7" },
    { text: "SW7 THT", x: 52.5, y: 38.8, fontSize: 0.7, role: "hint", part: "SW7" },
    // J5's refdes is printed by its own footprint on the body; this is the
    // hand-solder tag, turned 90 in the 4 mm strip beside the connector.
    { text: "J5 SOCKET THT", x: 72.0, y: 20, fontSize: 0.8, role: "hint", part: "J5", rotation: 90 },
    // ---- notes over the board, under the display opening ----
    { text: "DISPLAY OPENING", x: -10, y: -25.45, fontSize: 0.9, role: "note" },
    // ---- bottom strip ----
    { text: "U6 THT - LUGS FIRST", x: RIGHT_CONTROLS.TEMPO.x, y: RIGHT_CONTROLS.TEMPO.y - 10.2, fontSize: 0.8, role: "hint", part: "U6" },
    ...frontJacks.map(({ ref, x }) => ({ text: `${ref} THT`, x, y: JACK_Y + 7.5, fontSize: 0.7, role: "hint" as const, part: ref })),
    // Hand-soldered buttons: the ref print sits above each body, so the tag
    // goes on the same line just left of it ("THT SW1").
    ...frontSwitches.slice(0, 6).map(({ ref, x, y }) => ({
      text: "THT",
      x: x - 4.5,
      y: y + 7,
      fontSize: 0.7,
      role: "hint" as const,
      part: ref,
    })),
    // Gate reference designators (tscircuit prints none for transistors here,
    // and lib/silk.tsx is the one place that decides where they land).
    ...(["out1", "out2"] as const).map((k) => ({
      text: k === "out1" ? "Q1" : "Q2",
      x: FRONT_CLUSTERS[k].q.x + FRONT_GATE_REF_OFFSET.dx,
      y: FRONT_CLUSTERS[k].q.y + FRONT_GATE_REF_OFFSET.dy,
      fontSize: 0.8,
      role: "ref" as const,
      part: k === "out1" ? "Q1" : "Q2",
    })),
    // 1N4148WS cathodes: the clamp diodes are the only polarised two-pin parts
    // on this board besides the LEDs, so their cathode is printed, not implied.
    ...(["out1", "out2"] as const).map((k) => ({
      text: "K",
      x: FRONT_CLUSTERS[k].d.x + FRONT_DIODE_CATHODE_DX + FRONT_DIODE_K_OFFSET.dx,
      y: FRONT_CLUSTERS[k].d.y + FRONT_DIODE_K_OFFSET.dy,
      fontSize: 0.7,
      role: "polarity" as const,
      part: k === "out1" ? "D1" : "D2",
    })),
    // LED polarity: anode/cathode beside their pads; the footprint also draws
    // the cathode bar on the cathode side.
    ...frontLeds.flatMap(({ ref, x }) => [
      { text: "A", x: x - FRONT_LED_PAD_DX, y: LED_Y + FRONT_LED_MARK_DY, fontSize: 0.6, role: "polarity" as const, part: ref },
      { text: "K", x: x + FRONT_LED_PAD_DX, y: LED_Y + FRONT_LED_MARK_DY, fontSize: 0.6, role: "polarity" as const, part: ref },
    ]),
    // Pin-1 number for the 2 x 13 interconnect, where the pinout matters.
    { text: "1", x: FRONT_INTERCONNECT.x + 3.5, y: FRONT_INTERCONNECT.y + 18.2, fontSize: 0.8, role: "pin1", part: "J5" },
  ],
}

// ---- Rear/base board ----
// Free zones: the whole strip below the Pi mounting grid (y < -33) is empty on
// both faces of the stack, and the top strip above the Pi ribbon header
// (y > 53) carries only its refdes.
const REAR_PICO_X = { L: -48.39, R: -30.61 } as const
const REAR_PICO_PIN1_Y = { L: -24.13, R: 24.13 } as const

export const REAR_SILK: BoardSilkSpec = {
  board: "rear",
  layer: "bottom",
  width: REAR_PCB.width,
  height: REAR_PCB.height,
  wordmark: { x: -6, y: -40.5, capHeight: BOARD_WORDMARK_CAP, stroke: BOARD_WORDMARK_STROKE },
  oshw: { x: -26.5, y: -40.5, width: OSHW_WIDTH, stroke: OSHW_STROKE },
  pin1: [
    { ref: "Q3", x: 38.3, y: 9.7, source: "silk" },
    // Pico sockets: pin 1 is at the bottom of the left row and the top of the
    // right row, which is the Pico module's own pin numbering.
    // Outboard of pin 1 there is only 1.6 mm of board: the dot sits below the
    // end pin instead, still nearest to pin 1 and inside the Gerber guard.
    { ref: "J_PICO_L", x: REAR_PICO_X.L, y: -25.45, source: "silk" },
    { ref: "J_PICO_R", x: -29.0, y: REAR_PICO_PIN1_Y.R, source: "silk" },
    { ref: "J_PI", x: 26.0, y: 46.9, source: "silk" },
    // Direct 2 x 13 interconnect, male header on the TOP face. Its body is on
    // the far side of this board, so the bottom-side marks sit where the 26
    // tails are soldered; both are derived from the shared placement.
    { ref: "J_FRONT", x: REAR_INTERCONNECT.x + 2.6, y: REAR_INTERCONNECT.y + 16.4, source: "silk" },
  ],
  texts: [
    { text: "MIDI BRAIN REAR/BASE", x: 30, y: -37, fontSize: 1.0, role: "note" },
    { text: "THT", x: REAR_PICO_X.L, y: -26.3, fontSize: 0.7, role: "hint", part: "J_PICO_L" },
    { text: "THT", x: REAR_PICO_X.R, y: -26.3, fontSize: 0.7, role: "hint", part: "J_PICO_R" },
    // U3/U4: the routed via fence and the C13/C14 bypass refdes leave no room
    // for a dot against pin 1, so it carries a printed "1" instead - the same
    // vocabulary the headers use - placed at the first free spot below-left of
    // the pin, nearest to pin 1 and clear of the via fence.
    { text: "1", x: 35.7, y: -10.65, fontSize: 0.6, role: "pin1", part: "U3" },
    { text: "1", x: 35.9, y: -5.55, fontSize: 0.6, role: "pin1", part: "U4" },
    { text: "J_PI THT", x: 14, y: 54.5, fontSize: 0.7, role: "hint", part: "J_PI" },
    { text: "J_FRONT THT", x: REAR_INTERCONNECT.x - 0.1, y: REAR_INTERCONNECT.y + 21.6, fontSize: 0.7, role: "hint", part: "J_FRONT" },
    { text: "1", x: 27.2, y: 47.9, fontSize: 0.7, role: "pin1", part: "J_PI" },
    { text: "1", x: REAR_INTERCONNECT.x + 3.4, y: REAR_INTERCONNECT.y + 17.6, fontSize: 0.7, role: "pin1", part: "J_FRONT" },
    // Probe points: one net label per pad, derived from lib/probe-points.ts so
    // a pad can never carry a label that belongs to another net. The automatic
    // reference designator (TP1..TP15, 0.4 mm) prints beside each pad from its
    // own footprint, so the pair reads e.g. "TP5" + "SCLK".
    ...PROBE_POINTS.map(p => {
      const at = probeLabelPosition(p)
      return { text: p.label, x: at.x, y: at.y, fontSize: PROBE_LABEL_FONT_MM, role: "hint" as const, part: p.ref }
    }),
  ],
}

export const BOARD_SILK: Record<BoardSilkSpec["board"], BoardSilkSpec> = {
  front: FRONT_SILK,
  rear: REAR_SILK,
}

// ---- Geometry helpers (measured by the tests, emitted by the component) ----

/** Ink box of one text mark, using the exporter's measured advance ratio. */
export const textBounds = (
  mark: Pick<SilkTextMark, "text" | "x" | "y" | "fontSize"> & { rotation?: number },
): Bounds => {
  const advance = mark.text.length * mark.fontSize * SILK_ADVANCE_RATIO
  const turned = Math.abs((mark.rotation ?? 0) % 180) === 90
  const width = turned ? mark.fontSize : advance
  const height = turned ? advance : mark.fontSize
  return {
    minX: mark.x - width / 2,
    minY: mark.y - height / 2,
    maxX: mark.x + width / 2,
    maxY: mark.y + height / 2,
    width,
    height,
    cx: mark.x,
    cy: mark.y,
  }
}

/** Rotate a point list CCW about (cx, cy) by `degrees`. */
export const rotatePoints = (points: Pt[], cx: number, cy: number, degrees: number): Pt[] => {
  if (!degrees) return points
  const a = (degrees * Math.PI) / 180
  const cos = Math.cos(a), sin = Math.sin(a)
  return points.map((q) => {
    const dx = q.x - cx, dy = q.y - cy
    return { x: cx + dx * cos - dy * sin, y: cy + dx * sin + dy * cos }
  })
}

/** Wordmark ink, mirrored on the bottom layer the way it is printed. */
export const wordmarkPaths = (spec: BoardSilkSpec): Pt[][] => {
  const { x, y, capHeight, rotation } = spec.wordmark
  const paths = textPaths(WORDMARK_TEXT, capHeight, x, y, "center")
  const rotated = rotation ? paths.map((p) => rotatePoints(p, x, y, rotation)) : paths
  return spec.layer === "bottom" ? rotated.map((p) => p.map((q) => ({ x: 2 * x - q.x, y: q.y }))) : rotated
}

/** OSHW gear ink; the mark is mirrored on the bottom layer. */
export const oshwRoutes = (spec: BoardSilkSpec): Pt[][] => {
  const { x, y, width, rotation } = spec.oshw
  const routes = oshwPaths(x, y, width, spec.layer === "bottom")
  return rotation ? routes.map((r) => rotatePoints(r, x, y, rotation)) : routes
}

/** Every piece of ink this module prints, for the clearance checks. */
export const silkInk = (spec: BoardSilkSpec): { stroke: number; paths: Pt[][] }[] => [
  { stroke: spec.wordmark.stroke, paths: wordmarkPaths(spec) },
  { stroke: spec.oshw.stroke, paths: oshwRoutes(spec) },
  ...spec.pin1
    .filter((mark) => mark.source === "silk")
    .map((mark) => {
      // A dot is ink as a closed square ring around its centre.
      const r = PIN1_DOT_RADIUS
      return {
        stroke: SILK_MARK_STROKE,
        paths: [[{ x: mark.x - r, y: mark.y - r }, { x: mark.x + r, y: mark.y - r }, { x: mark.x + r, y: mark.y + r }, { x: mark.x - r, y: mark.y + r }, { x: mark.x - r, y: mark.y - r }]],
      }
    }),
]

/** Bounds of everything this module prints on one board. */
export const silkInkBounds = (spec: BoardSilkSpec): Bounds | null => {
  const boxes = silkInk(spec).flatMap((group) =>
    group.paths.map((path) =>
      unionBounds([
        {
          minX: Math.min(...path.map((p) => p.x)) - group.stroke / 2,
          maxX: Math.max(...path.map((p) => p.x)) + group.stroke / 2,
          minY: Math.min(...path.map((p) => p.y)) - group.stroke / 2,
          maxY: Math.max(...path.map((p) => p.y)) + group.stroke / 2,
          width: 0,
          height: 0,
          cx: 0,
          cy: 0,
        },
      ]),
    ),
  )
  return unionBounds([...boxes, ...spec.texts.map(textBounds)])
}

// ---- Emission ----

/**
 * Silkscreen section for one board. Every element is keyed through a Fragment:
 * a bare `<silkscreentext>` in an array has no key and React drops it.
 */
export const BoardSilkSection = ({ spec }: { spec: BoardSilkSpec }) => (
  <>
    {spec.pin1
      .filter((mark) => mark.source === "silk")
      .map((mark) => (
        <Fragment key={`pin1-${mark.ref}`}>
          <silkscreencircle
            layer={spec.layer}
            pcbX={`${mark.x}mm`}
            pcbY={`${mark.y}mm`}
            radius={`${PIN1_DOT_RADIUS}mm`}
            strokeWidth={`${SILK_MARK_STROKE}mm`}
          />
        </Fragment>
      ))}
    {spec.texts.map((mark) => (
      <Fragment key={`silk-${mark.role}-${mark.text}-${mark.part ?? mark.x}`}>
        <silkscreentext
          text={mark.text}
          fontSize={`${mark.fontSize}mm`}
          anchorAlignment="center"
          layer={spec.layer}
          pcbX={`${mark.x}mm`}
          pcbY={`${mark.y}mm`}
          pcbRotation={`${mark.rotation ?? 0}deg`}
        />
      </Fragment>
    ))}
    {wordmarkPaths(spec).map((route, i) => (
      <Fragment key={`wordmark-${i}`}>
        <silkscreenpath layer={spec.layer} strokeWidth={`${spec.wordmark.stroke}mm`} route={route} />
      </Fragment>
    ))}
    {oshwRoutes(spec).map((route, i) => (
      <Fragment key={`oshw-${i}`}>
        <silkscreenpath layer={spec.layer} strokeWidth={`${spec.oshw.stroke}mm`} route={route} />
      </Fragment>
    ))}
  </>
)

export const FrontSilk = () => <BoardSilkSection spec={FRONT_SILK} />
export const RearSilk = () => <BoardSilkSection spec={REAR_SILK} />

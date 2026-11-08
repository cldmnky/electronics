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
}

/**
 * A pin-1 index mark. `silk` marks are emitted by this module; `footprint`
 * marks are drawn by the part's own footprint (the SO6 chamfer) and are listed
 * here so one table still proves every pin 1 is marked.
 */
export type Pin1Mark = { ref: string; x: number; y: number; source: "silk" | "footprint" }

export type WordmarkSpec = { x: number; y: number; capHeight: number; stroke: number }
export type OshwSpec = { x: number; y: number; width: number; stroke: number }

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
// Free zones were mapped from the routed board before placing anything:
//  * the strip between the display module's outline top (45.0: the 4.3-inch
//    module is 68 mm tall on y = 11) and the top relief notch bottom (48.0) is
//    only 3 mm, shorter than the 3.5 mm the 2.8 mm-cap wordmark inks, so the
//    brand lockup stays on the strip below it at y 43.5 and clears the two
//    front-to-rear spacer holes at (+-44, 43) instead. It is board identity on
//    a face that is covered by the module (the operator-facing wordmark is the
//    panel art); it is not an operator label and nothing else fits there;
//  * the block below the jack field (y < -42, x > 25) is empty - board
//    identity and the hand-solder legend live there.
const FRONT_JACK_PIN1_DY = 4.92 // lib/jack.tsx: pin 1 (tip) above the barrel centre
const FRONT_LED_PAD_DX = 1.0 // 0805 anode/cathode pads sit +-0.91 mm from centre
const FRONT_LED_MARK_DY = -1.6

const frontJacks = FRONT_COLUMNS.map((column, i) => ({
  ref: `J${i + 1}`,
  x: midiJackX(column.jack),
}))
const frontSwitches = [
  ...FRONT_COLUMNS.map((column, i) => ({ ref: `SW${i + 1}`, x: transportButtonX(column.button) })),
  { ref: "SW5", x: transportButtonX("PLAY START") },
  { ref: "SW6", x: RIGHT_CONTROLS.SHIFT_EXIT.x },
]
// REC / OVER DUB / PLAY indicators: same positions as the panel light pipes,
// i.e. 12 mm BESIDE the transport button, so the polarity letters follow
// ledPosition, never the button column.
const frontLeds = [
  { ref: "D3", x: ledPosition(transportButtonX("REC")).x },
  { ref: "D4", x: ledPosition(transportButtonX("OVER DUB")).x },
  { ref: "D5", x: ledPosition(transportButtonX("PLAY")).x },
]
// MIDI OUT gate transistors (AO3401A SOT-23, front.circuit.tsx <MidiOut>): the
// pin-1 pad side and, 2.1 mm right / 1.6 mm up from it, the ref designator. Q1's
// cluster moved 45 -> 49 mm left when the 4.3-inch module's bottom-left LCD
// hole landed inside the old courtyard at (-41.88, -13.54), so both marks are
// derived from this one pair: the designator cannot strand itself on the old
// spot (it did, at x -45.2, until this table was reconciled to the layout).
const frontGates = [
  { ref: "Q1", x: -51.3, y: -11.3 },
  { ref: "Q2", x: -17.3, y: -11.3 },
]
const FRONT_GATE_REF_OFFSET = { dx: 2.1, dy: 1.6 }

export const FRONT_SILK: BoardSilkSpec = {
  board: "front",
  layer: "top",
  width: FRONT_PCB.width,
  height: FRONT_PCB.height,
  wordmark: { x: -20, y: 43.5, capHeight: BOARD_WORDMARK_CAP, stroke: BOARD_WORDMARK_STROKE },
  oshw: { x: -38.6, y: 43.5, width: OSHW_WIDTH, stroke: OSHW_STROKE },
  pin1: [
    // SO6 chamfer marks drawn by the TLP2361 footprint itself.
    { ref: "U1", x: -41.95, y: 7.71, source: "footprint" },
    { ref: "U2", x: -11.95, y: 7.71, source: "footprint" },
    // SOT-23 gates (derived from the cluster, see frontGates) and the two
    // multi-pin modules.
    ...frontGates.map((gate) => ({ ref: gate.ref, x: gate.x, y: gate.y, source: "silk" as const })),
    // Derived from the control column, which owner decision D2 moved 47 -> 52.
    { ref: "U5", x: RIGHT_CONTROLS.NAV.x - 4.9, y: RIGHT_CONTROLS.NAV.y + 1.43, source: "silk" },
    { ref: "U6", x: RIGHT_CONTROLS.TEMPO.x - 4.0, y: RIGHT_CONTROLS.TEMPO.y + 7.9, source: "silk" },
    // TRS jacks: pin 1 is the tip contact at the panel end of the footprint.
    ...frontJacks.map(({ ref, x }) => ({ ref, x: x - 1.9, y: JACK_Y + FRONT_JACK_PIN1_DY, source: "silk" as const })),
    // 2x13 front/rear interconnect (bottom side).
    { ref: "J5", x: 36.6, y: 25.24, source: "silk" },
  ],
  texts: [
    // Bottom-right corner block: the panel POWER button, its jumper header and
    // then board identity. Kept clear of the M2.5 holes at (44, -52)/(61, -53)
    // and of the J4 jack courtyard.
    { text: "POWER", x: 45.5, y: -42.6, fontSize: 1.4, role: "note" },
    // SW7 is the Pi's remote power button: a plain contact pair, no power
    // circuitry. Press = power on / graceful shutdown request.
    { text: "POWER: PRESS = ON / SHUTDOWN", x: 45.5, y: -44.4, fontSize: 0.7, role: "hint", part: "SW7" },
    // Board identity and the hand-solder summary (JLC places every SMD line).
    { text: "MIDI BRAIN FRONT/UI", x: 33, y: -46.4, fontSize: 1.0, role: "note" },
    { text: "HAND THT: J1-4 SW1-6 J5 SW7", x: 33, y: -48.2, fontSize: 0.8, role: "note" },
    // J_PWR tag sits right above the header (4 mm rule), clear of its pads and
    // of the M2.5 hole at (44, -52).
    { text: "J_PWR - TO PI PWR", x: 37.5, y: -50.2, fontSize: 0.7, role: "hint", part: "J_PWR" },
    // U6 is the one part whose manual process is qualified, not assumed:
    // lugs first as the mechanical anchor, then a preheated quick joint
    // (docs/assembly.md "U6 encoder: THT fallback and bench technique").
    { text: "U6 THT - LUGS FIRST", x: RIGHT_CONTROLS.TEMPO.x, y: RIGHT_CONTROLS.TEMPO.y - 10.2, fontSize: 0.8, role: "hint", part: "U6" },
    { text: "J5 THT", x: FRONT_INTERCONNECT.x, y: 31.8, fontSize: 0.8, role: "hint", part: "J5" },
    ...frontJacks.map(({ ref, x }) => ({ text: `${ref} THT`, x, y: -40.65, fontSize: 0.7, role: "hint" as const, part: ref })),
    // Hand-soldered buttons: the ref print sits above each body, so the tag
    // goes on the same line just left of it ("THT SW1"). Below the body it
    // would collide with the LED polarity letters.
    ...frontSwitches.map(({ ref, x }) => ({
      text: "THT",
      x: x - 4.5,
      y: ref === "SW6" ? RIGHT_CONTROLS.SHIFT_EXIT.y + 7 : TRANSPORT_Y + 7,
      fontSize: 0.7,
      role: "hint" as const,
      part: ref,
    })),
    // Reference designators the builder otherwise cannot see: tscircuit prints
    // none for transistors or for the jack footprint. Derived from the gate
    // marks so the designator follows its part.
    ...frontGates.map((gate) => ({
      text: gate.ref,
      x: gate.x + FRONT_GATE_REF_OFFSET.dx,
      y: gate.y + FRONT_GATE_REF_OFFSET.dy,
      fontSize: 0.8,
      role: "ref" as const,
      part: gate.ref,
    })),
    // LED polarity: anode/cathode beside their pads; the footprint also draws
    // the cathode bar on the cathode side.
    ...frontLeds.flatMap(({ ref, x }) => [
      { text: "A", x: x - FRONT_LED_PAD_DX, y: LED_Y + FRONT_LED_MARK_DY, fontSize: 0.6, role: "polarity" as const, part: ref },
      { text: "K", x: x + FRONT_LED_PAD_DX, y: LED_Y + FRONT_LED_MARK_DY, fontSize: 0.6, role: "polarity" as const, part: ref },
    ]),
    // Pin-1 number for the 2x13 interconnect, where the pinout matters.
    { text: "1", x: 37.5, y: 27.4, fontSize: 0.8, role: "pin1", part: "J5" },
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
    { ref: "J_FRONT", x: 47.6, y: 16.4, source: "silk" },
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
    { text: "J_FRONT THT", x: 44.9, y: 21.6, fontSize: 0.7, role: "hint", part: "J_FRONT" },
    { text: "1", x: 27.2, y: 47.9, fontSize: 0.7, role: "pin1", part: "J_PI" },
    { text: "1", x: 48.4, y: 17.6, fontSize: 0.7, role: "pin1", part: "J_FRONT" },
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
export const textBounds = (mark: Pick<SilkTextMark, "text" | "x" | "y" | "fontSize">): Bounds => {
  const width = mark.text.length * mark.fontSize * SILK_ADVANCE_RATIO
  return {
    minX: mark.x - width / 2,
    minY: mark.y - mark.fontSize / 2,
    maxX: mark.x + width / 2,
    maxY: mark.y + mark.fontSize / 2,
    width,
    height: mark.fontSize,
    cx: mark.x,
    cy: mark.y,
  }
}

/** Wordmark ink, mirrored on the bottom layer the way it is printed. */
export const wordmarkPaths = (spec: BoardSilkSpec): Pt[][] => {
  const { x, y, capHeight } = spec.wordmark
  const paths = textPaths(WORDMARK_TEXT, capHeight, x, y, "center")
  return spec.layer === "bottom" ? paths.map((p) => p.map((q) => ({ x: 2 * x - q.x, y: q.y }))) : paths
}

/** OSHW gear ink; the mark is mirrored on the bottom layer. */
export const oshwRoutes = (spec: BoardSilkSpec): Pt[][] =>
  oshwPaths(spec.oshw.x, spec.oshw.y, spec.oshw.width, spec.layer === "bottom")

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

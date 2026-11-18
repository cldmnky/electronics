// Silkscreen primitives for the euro front board.
//
// `<silkscreentext>` prints with a stroke of `fontSize * 0.09`
// (@tscircuit/alphabet `strokeWidthRatio`): 0.7mm text draws a 0.063mm line and
// 1.7mm text a 0.153mm line, measured in the 0.2 print's gerber. JLCPCB's
// minimum printed width is 0.15mm (doc/FRONT-PANEL.md "Silk rules"), so only
// the 1.7mm board identity can be plain text. Every smaller label here is drawn
// as single-stroke polylines from the same alphabet the panel uses
// (`lib/panel-art.ts`), with an explicit stroke width.
//
// Bottom-layer paths are NOT mirrored by the gerber writer (checked: three
// disconnected bottom routes export with the coordinates they were given),
// while bottom-layer *text* is mirrored at render time. A path label on the
// bottom must therefore be mirrored here, about its own anchor, so that it
// reads correctly when the board is turned over - the same trick the base
// board uses for its rear wordmark. Every label is anchored at its centre,
// which makes the mirror a pure glyph flip: the printed box does not move.

import { Fragment } from "react"
import { textInkWidth, textPolylines, type Pt } from "./panel-art"

export type SilkLayer = "top" | "bottom"

/** JLCPCB's minimum reliably printed silk width, mm. */
export const SILK_STROKE = 0.15
/** Heavier stroke for reading-height notes (>= 2x the minimum). */
export const NOTE_STROKE = 0.2

/** Cap heights, mm. 0.8mm is the smallest that still prints counters at 0.15mm. */
export const LABEL_CAP = 0.8
export const PAD_CAP = 0.95
export const NOTE_CAP = 1.05

const mirrorAbout = (paths: Pt[][], cx: number): Pt[][] =>
  paths.map((path) => path.map((p) => ({ x: 2 * cx - p.x, y: p.y })))

type SilkTextProps = {
  text: string
  /** Centre of the printed ink box (never the nominal font size). */
  x: number
  /** Baseline. Glyph ink sits above it (ascenders/round letters overshoot). */
  y: number
  layer: SilkLayer
  cap?: number
  stroke?: number
}

/**
 * One line of single-stroke text, centre-anchored at (`x`, baseline `y`).
 * Throws on a character the alphabet does not have, rather than printing a
 * gap: a missing glyph on a build note is worse than a failed build.
 */
export const SilkText = ({
  text,
  x,
  y,
  layer,
  cap = LABEL_CAP,
  stroke = SILK_STROKE,
}: SilkTextProps) => {
  const half = textInkWidth(text, cap) / 2
  const placed = textPolylines(text, cap).map((path) =>
    path.map((p) => ({ x: x - half + p.x, y: y + p.y })),
  )
  const printed = layer === "bottom" ? mirrorAbout(placed, x) : placed
  return (
    <>
      {printed.map((route, i) => (
        <Fragment key={`${text}-${i}`}>
          <silkscreenpath layer={layer} strokeWidth={`${stroke}mm`} route={route} />
        </Fragment>
      ))}
    </>
  )
}

type SilkLineProps = {
  from: Pt
  to: Pt
  layer: SilkLayer
  stroke?: number
}

/** A plain stroke - leader ticks, brackets, polarity bars. */
export const SilkLine = ({ from, to, layer, stroke = SILK_STROKE }: SilkLineProps) => {
  const route =
    layer === "bottom"
      ? [from, to].map((p) => ({ x: 2 * ((from.x + to.x) / 2) - p.x, y: p.y }))
      : [from, to]
  return <silkscreenpath layer={layer} strokeWidth={`${stroke}mm`} route={route} />
}

type SilkDotProps = { x: number; y: number; layer: SilkLayer; radius?: number }

/** Pin-1 dot: a filled circle needs no mirroring. */
export const SilkDot = ({ x, y, layer, radius = 0.35 }: SilkDotProps) => (
  <silkscreencircle
    layer={layer}
    pcbX={`${x}mm`}
    pcbY={`${y}mm`}
    radius={`${radius}mm`}
    strokeWidth={`${SILK_STROKE}mm`}
  />
)

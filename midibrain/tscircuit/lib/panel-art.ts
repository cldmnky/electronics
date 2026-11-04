// mnkybox panel brand artwork, as data (signal-inspired).
//
// ONE source of truth for the drawn wordmark: the module name as single-stroke
// polylines from `@tscircuit/alphabet` plus the signal waveform motif
// (sine -> triangle -> square) flanking it, in panel millimetres (+X right,
// +Y up, origin at the panel centre). `panel.circuit.tsx` emits one
// `<silkscreenpath>` per path, so the ink in the built circuit JSON is exactly
// what this file computed. No fonts, no text primitives: a stroked path cannot
// silently run off the board the way `center_left`-anchored text did in
// 2026-11, and every position is computed, so the checks measure the ink.
//
// Kept independent of layout.ts on purpose: geometry in, geometry out. The
// circuit file and the tests combine this module with the shared coordinates.

import {
  glyphAdvanceRatio,
  glyphWidthRatio,
  lineAlphabet,
  spaceWidthRatio,
} from "@tscircuit/alphabet"

export type Pt = { x: number; y: number }
export const pt = (x: number, y: number): Pt => ({ x, y })

export type Bounds = { minX: number; minY: number; maxX: number; maxY: number; width: number; height: number; cx: number; cy: number }

export const polylineBounds = (path: Pt[]): Bounds | null => {
  if (path.length === 0) return null
  let minX = Infinity, minY = Infinity, maxX = -Infinity, maxY = -Infinity
  for (const p of path) {
    minX = Math.min(minX, p.x); minY = Math.min(minY, p.y)
    maxX = Math.max(maxX, p.x); maxY = Math.max(maxY, p.y)
  }
  return { minX, minY, maxX, maxY, width: maxX - minX, height: maxY - minY, cx: (minX + maxX) / 2, cy: (minY + maxY) / 2 }
}

export const unionBounds = (boxes: (Bounds | null)[]): Bounds | null => {
  const bs = boxes.filter((b): b is Bounds => b !== null)
  if (bs.length === 0) return null
  const minX = Math.min(...bs.map(b => b.minX))
  const minY = Math.min(...bs.map(b => b.minY))
  const maxX = Math.max(...bs.map(b => b.maxX))
  const maxY = Math.max(...bs.map(b => b.maxY))
  return { minX, minY, maxX, maxY, width: maxX - minX, height: maxY - minY, cx: (minX + maxX) / 2, cy: (minY + maxY) / 2 }
}

const distPointToSegment = (p: Pt, a: Pt, b: Pt): number => {
  const dx = b.x - a.x, dy = b.y - a.y
  const len2 = dx * dx + dy * dy
  const t = len2 === 0 ? 0 : Math.min(1, Math.max(0, ((p.x - a.x) * dx + (p.y - a.y) * dy) / len2))
  return Math.hypot(p.x - (a.x + t * dx), p.y - (a.y + t * dy))
}

const distSegments = (a1: Pt, a2: Pt, b1: Pt, b2: Pt): number =>
  Math.min(
    distPointToSegment(a1, b1, b2), distPointToSegment(a2, b1, b2),
    distPointToSegment(b1, a1, a2), distPointToSegment(b2, a1, a2),
  )

const distPathsToPaths = (pa: Pt[][], pb: Pt[][]): number => {
  let best = Infinity
  for (const A of pa)
    for (let i = 0; i + 1 < A.length; i++)
      for (const B of pb)
        for (let j = 0; j + 1 < B.length; j++)
          best = Math.min(best, distSegments(A[i]!, A[i + 1]!, B[j]!, B[j + 1]!))
  return best
}

// ---------------------------------------------------------------------------
// Print rules (same floors as the Froemix signal panel)
// ---------------------------------------------------------------------------
/** JLCPCB's rigid-board floor is 0.15 mm; artwork keeps a 0.2 mm floor. */
export const MIN_STROKE_WIDTH = 0.2
/** Silk stays this far inside the routed outline. */
export const SILK_EDGE_MARGIN = 0.5
/** Drawn clearance between neighbouring glyph strokes. */
export const MIN_GLYPH_CLEARANCE = 0.1
/** Extra letter tracking as a fraction of cap height (alphabet pitch is tight). */
export const LETTER_SPACING_RATIO = 0.22

/** Cap height of a glyph in alphabet units, measured from `H`. */
export const capHeightUnits = (() => {
  const h = lineAlphabet.H
  if (!h || h.length === 0) throw new Error("panel-art: alphabet has no H glyph")
  const ys = h.flatMap((s) => [s.y1, s.y2])
  return Math.max(...ys) - Math.min(...ys)
})()

// ---------------------------------------------------------------------------
// Alphabet text as placed polylines (Y-up, baseline at local y = 0)
// ---------------------------------------------------------------------------
const textGlyphPaths = (text: string, capHeight: number): { char: string; paths: Pt[][] }[] => {
  const scale = capHeight / capHeightUnits
  const spacingUnits = (LETTER_SPACING_RATIO * capHeight) / scale
  const glyphs: { char: string; paths: Pt[][] }[] = []
  let cursor = 0
  for (const ch of text) {
    if (ch === " ") {
      glyphs.push({ char: ch, paths: [] })
      cursor += glyphAdvanceRatio[" "] ?? spaceWidthRatio
      continue
    }
    const glyph = lineAlphabet[ch]
    if (!glyph) throw new Error(`panel-art: the alphabet has no glyph for ${JSON.stringify(ch)}`)
    glyphs.push({
      char: ch,
      paths: glyph.map((s) => [pt((cursor + s.x1) * scale, s.y1 * scale), pt((cursor + s.x2) * scale, s.y2 * scale)]),
    })
    cursor += (glyphAdvanceRatio[ch] ?? glyphWidthRatio) + spacingUnits
  }
  return glyphs
}

/** Lay out one string as single-stroke polylines, glyph order preserved. */
export const textPolylines = (text: string, capHeight: number): Pt[][] =>
  textGlyphPaths(text, capHeight).flatMap((g) => g.paths)

/** Drawn ink width of one line, in mm (first ink to last ink). */
export const textInkWidth = (text: string, capHeight: number): number => {
  const b = unionBounds(textPolylines(text, capHeight).map(polylineBounds))
  return b ? b.width : 0
}

/** Smallest drawn gap between neighbouring glyphs; negative means touching. */
export const minGlyphClearance = (text: string, capHeight: number, strokeWidth: number): number => {
  const glyphs = textGlyphPaths(text, capHeight)
  let best = Infinity
  for (let i = 0; i < glyphs.length - 1; i++) {
    const a = glyphs[i]!, b = glyphs[i + 1]!
    if (a.paths.length === 0 || b.paths.length === 0) continue
    best = Math.min(best, distPathsToPaths(a.paths, b.paths) - strokeWidth)
  }
  return best
}

export type Align = "center" | "left" | "right"

/** Move paths so the ink box centre (or one edge) lands on (x, y). */
export const placePaths = (paths: Pt[][], x: number, y: number, align: Align = "center"): Pt[][] => {
  const b = unionBounds(paths.map(polylineBounds))
  if (!b) return paths
  const from = align === "left" ? b.minX : align === "right" ? b.maxX : b.cx
  const dx = x - from
  const dy = y - b.cy
  return paths.map((p) => p.map((q) => pt(q.x + dx, q.y + dy)))
}

/** One printed line, anchored by its measured ink box (never by advance). */
export const textPaths = (text: string, capHeight: number, x: number, y: number, align: Align = "center"): Pt[][] =>
  placePaths(textPolylines(text, capHeight), x, y, align)

// ---------------------------------------------------------------------------
// Signal motif: one line running sine, then triangle, then square
// ---------------------------------------------------------------------------
export const waveformPath = (x0: number, x1: number, y: number, amplitude: number): Pt[] => {
  const steps = Math.max(24, Math.round(Math.abs(x1 - x0) / 0.5))
  const points: Pt[] = []
  for (let i = 0; i <= steps; i++) {
    const x = x0 + ((x1 - x0) * i) / steps
    const t = (x - x0) / (x1 - x0)
    let v: number
    if (t < 1 / 3) {
      v = Math.sin(t * 3 * Math.PI * 4)
    } else if (t < 2 / 3) {
      const u = (t - 1 / 3) * 3
      v = (2 / Math.PI) * Math.asin(Math.sin(u * 3 * 2 * Math.PI))
    } else {
      const u = (t - 2 / 3) * 3
      v = Math.sin(u * 3 * 2 * Math.PI) >= 0 ? 1 : -1
    }
    points.push(pt(x, y + amplitude * v))
  }
  return points
}

// ---------------------------------------------------------------------------
// Brand composition: mnkybox wordmark + flanking waves, centred on (cx, y)
// ---------------------------------------------------------------------------
export const WORDMARK_TEXT = "mnkybox" as const
export const WORDMARK_CAP = 3.2 as const
export const WORDMARK_STROKE = 0.7 as const
export const WAVE_AMPLITUDE = 0.8 as const
export const WAVE_STROKE = 0.3 as const
/** Gap between wordmark ink edge and wave start. */
export const WAVE_GAP = 3 as const
/** Wave run length per side. */
export const WAVE_LENGTH = 18 as const

export type StrokeGroup = { stroke: number; paths: Pt[][] }

/** Wordmark ink centred on (cx, y) with waves placed off its measured edges. */
export const brandArtPaths = (cx: number, y: number): StrokeGroup[] => {
  const word = textPaths(WORDMARK_TEXT, WORDMARK_CAP, cx, y, "center")
  const b = unionBounds(word.map(polylineBounds))
  if (!b) throw new Error("panel-art: empty wordmark")
  const inner = b.maxX - cx + WAVE_GAP
  const outer = inner + WAVE_LENGTH
  return [
    { stroke: WORDMARK_STROKE, paths: word },
    {
      stroke: WAVE_STROKE,
      paths: [
        waveformPath(cx - outer, cx - inner, y, WAVE_AMPLITUDE),
        waveformPath(cx + inner, cx + outer, y, WAVE_AMPLITUDE),
      ],
    },
  ]
}

/** Measured ink bounds of the composed brand (wordmark + waves). */
export const brandBounds = (cx: number, y: number): Bounds => {
  const b = unionBounds(brandArtPaths(cx, y).flatMap(g => g.paths.map(polylineBounds)))
  if (!b) throw new Error("panel-art: empty brand")
  return b
}

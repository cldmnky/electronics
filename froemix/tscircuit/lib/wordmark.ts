// The Nord monoline FROEMIX wordmark, as local board artwork.
//
// This is the same geometry as `euro/lib/panel-art.ts` (`MONO_GLYPHS` /
// `wordmarkPaths`). It is copied rather than imported because `tsci dev` only
// serves files INSIDE the project directory; an import from `../../euro` fails
// in the browser preview even though a Node build can read it. Keep the two
// copies identical: `scripts/test_silk.test.ts` compares the built base-board
// paths against the euro wordmark and fails on any drift.

type Point = { x: number; y: number }

const round = (v: number): number => Math.round(v * 1e6) / 1e6
const pt = (x: number, y: number): Point => ({ x: round(x), y: round(y) })

const circlePath = (cx: number, cy: number, radius: number, segments = 64): Point[] => {
  const out: Point[] = []
  for (let i = 0; i < segments; i++) {
    const a = (2 * Math.PI * i) / segments
    out.push(pt(cx + radius * Math.cos(a), cy + radius * Math.sin(a)))
  }
  return out
}

/** Seven hand-placed single-stroke letterforms, cap height 8, y-down local frame. */
const MONO_GLYPHS: Record<string, { w: number; paths: Point[][] }> = {
  F: {
    w: 7.5,
    paths: [
      [pt(0.65, 0), pt(0.65, 8)],
      [pt(0, 0.65), pt(7.5, 0.65)],
      [pt(0, 3.55), pt(5.9, 3.55)],
    ],
  },
  R: {
    w: 7.8,
    paths: [
      [pt(0.65, 0), pt(0.65, 8)],
      [pt(0, 0.65), pt(4.6, 0.65)],
      // semicircle bulging right, from (4.6,0.65) to (4.6,4.05), radius 1.7
      ...Array.from({ length: 11 }, (_, i) => {
        const a0 = -Math.PI / 2 + (Math.PI * i) / 11
        const a1 = -Math.PI / 2 + (Math.PI * (i + 1)) / 11
        return [
          pt(4.6 + 1.7 * Math.cos(a0), 2.35 + 1.7 * Math.sin(a0)),
          pt(4.6 + 1.7 * Math.cos(a1), 2.35 + 1.7 * Math.sin(a1)),
        ] as Point[]
      }),
      [pt(0.65, 4.05), pt(4.6, 4.05)],
      [pt(3.4, 4.05), pt(6.85, 7.55)],
    ],
  },
  O: { w: 8.0, paths: [circlePath(4, 4, 3.35, 48)] },
  E: {
    w: 7.2,
    paths: [
      [pt(0.65, 0), pt(0.65, 8)],
      [pt(0, 0.65), pt(7.2, 0.65)],
      [pt(0, 4), pt(6.3, 4)],
      [pt(0, 7.35), pt(7.2, 7.35)],
    ],
  },
  M: {
    w: 10.6,
    paths: [[pt(0.65, 8), pt(0.65, 0), pt(5.3, 6.3), pt(9.95, 0), pt(9.95, 8)]],
  },
  I: { w: 1.3, paths: [[pt(0.65, 0), pt(0.65, 8)]] },
  X: {
    w: 8.6,
    paths: [
      [pt(0.55, 0.45), pt(8.05, 7.55)],
      [pt(8.05, 0.45), pt(0.55, 7.55)],
    ],
  },
}

/** Letter tracking in glyph units. */
const MONO_TRACKING = 3.2

/**
 * `FROEMIX` as monoline strokes, `capHeight` millimetres tall, centred on
 * `centreX` at `baselineY`, with the drawn ink (not the advance widths) centred.
 */
export const wordmarkPaths = (
  text: string,
  capHeight: number,
  baselineY: number,
  centreX = 0,
): { paths: Point[][]; width: number } => {
  const scale = capHeight / 8
  const glyphs = [...text].map((ch) => {
    const g = MONO_GLYPHS[ch]
    if (!g) throw new Error(`wordmark: no monoline glyph for ${JSON.stringify(ch)}`)
    return g
  })
  const total = glyphs.reduce((sum, g) => sum + g.w, 0) + MONO_TRACKING * (glyphs.length - 1)
  const paths: Point[][] = []
  let cursor = -total / 2
  for (const g of glyphs) {
    for (const path of g.paths) {
      paths.push(
        path.map((p) => pt(centreX + (cursor + p.x) * scale, baselineY + (8 - p.y) * scale)),
      )
    }
    cursor += g.w + MONO_TRACKING
  }
  const xs = paths.flat().map((p) => p.x)
  const width = Math.max(...xs) - Math.min(...xs)
  const cx = (Math.max(...xs) + Math.min(...xs)) / 2
  const dx = centreX - cx
  return {
    paths: paths.map((path) => path.map((p) => pt(p.x + dx, p.y))),
    width,
  }
}

// Froemix front panel - pure geometry helpers (no JSX, no DOM).
//
// Shared by three consumers, so all of them measure the same thing:
//   lib/panel-art.ts      builds the artwork (window polygon, label ink)
//   lib/panel-checks.ts   validates the BUILT circuit JSON and the artwork
//   scripts/panel_*.ts    preview / release tooling
//
// Panel coordinates here are the tscircuit frame: origin at the board centre,
// +X right, +Y up, millimetres. Nothing in this file knows about tscircuit.

export type Pt = { x: number; y: number }

export type Bounds = {
  minX: number
  minY: number
  maxX: number
  maxY: number
  width: number
  height: number
  cx: number
  cy: number
}

/** Coordinates are carried at 1e-6mm; everything below compares with a tolerance. */
export const round = (v: number, digits = 6): number => {
  const f = 10 ** digits
  return Math.round(v * f) / f
}

export const pt = (x: number, y: number): Pt => ({ x: round(x), y: round(y) })

export const line = (x1: number, y1: number, x2: number, y2: number): Pt[] => [
  pt(x1, y1),
  pt(x2, y2),
]

/** Bounding box of a polyline's vertices (no stroke widening). */
export const polylineBounds = (points: Pt[]): Bounds | null => {
  if (points.length === 0) return null
  let minX = Infinity
  let minY = Infinity
  let maxX = -Infinity
  let maxY = -Infinity
  for (const p of points) {
    if (p.x < minX) minX = p.x
    if (p.x > maxX) maxX = p.x
    if (p.y < minY) minY = p.y
    if (p.y > maxY) maxY = p.y
  }
  return boundsOf(minX, minY, maxX, maxY)
}

export const boundsOf = (minX: number, minY: number, maxX: number, maxY: number): Bounds => ({
  minX,
  minY,
  maxX,
  maxY,
  width: maxX - minX,
  height: maxY - minY,
  cx: (minX + maxX) / 2,
  cy: (minY + maxY) / 2,
})

/** Union of several bounds; null when the list is empty. */
export const unionBounds = (list: (Bounds | null)[]): Bounds | null => {
  const real = list.filter((b): b is Bounds => b !== null)
  if (real.length === 0) return null
  return boundsOf(
    Math.min(...real.map((b) => b.minX)),
    Math.min(...real.map((b) => b.minY)),
    Math.max(...real.map((b) => b.maxX)),
    Math.max(...real.map((b) => b.maxY)),
  )
}

export const boundsOfPaths = (paths: Pt[][]): Bounds | null =>
  unionBounds(paths.map((p) => polylineBounds(p)))

// ---------------------------------------------------------------------------
// Rectangles and rounded rectangles
// ---------------------------------------------------------------------------

/** Axis-aligned rectangle polygon, counter-clockwise in the +Y-up frame. */
export const rectPolygon = (cx: number, cy: number, width: number, height: number): Pt[] => {
  const hw = width / 2
  const hh = height / 2
  return [
    pt(cx - hw, cy - hh),
    pt(cx + hw, cy - hh),
    pt(cx + hw, cy + hh),
    pt(cx - hw, cy + hh),
  ]
}

/**
 * Rectangle with rounded corners, as a closed polygon of straight chords.
 * `segmentsPerCorner` chords per 90 degree corner; the flat edges stay exactly
 * `width` x `height`, so the bounding box is not changed by the rounding.
 */
export const roundedRectPolygon = (opts: {
  cx: number
  cy: number
  width: number
  height: number
  radius: number
  segmentsPerCorner: number
}): Pt[] => {
  const { cx, cy, width, height } = opts
  const r = Math.max(0, Math.min(opts.radius, width / 2, height / 2))
  const n = Math.max(1, Math.floor(opts.segmentsPerCorner))
  const hw = width / 2
  const hh = height / 2
  if (r === 0) return rectPolygon(cx, cy, width, height)

  // Corner centre + the angle the arc starts at; arcs run counter-clockwise.
  const corners = [
    { dx: hw - r, dy: -(hh - r), startDeg: -90 }, // bottom-right
    { dx: hw - r, dy: hh - r, startDeg: 0 }, // top-right
    { dx: -(hw - r), dy: hh - r, startDeg: 90 }, // top-left
    { dx: -(hw - r), dy: -(hh - r), startDeg: 180 }, // bottom-left
  ]
  const out: Pt[] = []
  for (const c of corners) {
    for (let i = 0; i <= n; i++) {
      const a = ((c.startDeg + (90 * i) / n) * Math.PI) / 180
      out.push(pt(cx + c.dx + r * Math.cos(a), cy + c.dy + r * Math.sin(a)))
    }
  }
  return dedupe(out)
}

/** Drop consecutive duplicate vertices (arc ends coincide with flat edges). */
export const dedupe = (points: Pt[]): Pt[] => {
  const out: Pt[] = []
  for (const p of points) {
    const prev = out[out.length - 1]
    if (prev && Math.abs(prev.x - p.x) < 1e-9 && Math.abs(prev.y - p.y) < 1e-9) continue
    out.push(p)
  }
  const first = out[0]
  const last = out[out.length - 1]
  if (out.length > 1 && first && last && Math.abs(first.x - last.x) < 1e-9 && Math.abs(first.y - last.y) < 1e-9) {
    out.pop()
  }
  return out
}

/** Sagitta of one chord of a circle: the farthest the chord sits from the true arc. */
export const arcSagitta = (radius: number, segmentsPerQuarter: number): number =>
  radius * (1 - Math.cos(Math.PI / (4 * Math.max(1, segmentsPerQuarter))))

// ---------------------------------------------------------------------------
// Distances: point/segment, segment/segment, polyline/polyline, circle/polyline
// ---------------------------------------------------------------------------

export const dist = (a: Pt, b: Pt): number => Math.hypot(b.x - a.x, b.y - a.y)

export const distPointToSegment = (p: Pt, a: Pt, b: Pt): number => {
  const dx = b.x - a.x
  const dy = b.y - a.y
  const len2 = dx * dx + dy * dy
  if (len2 === 0) return dist(p, a)
  let t = ((p.x - a.x) * dx + (p.y - a.y) * dy) / len2
  t = Math.max(0, Math.min(1, t))
  return Math.hypot(p.x - (a.x + t * dx), p.y - (a.y + t * dy))
}

const cross = (ax: number, ay: number, bx: number, by: number) => ax * by - ay * bx

export const segmentsIntersect = (a: Pt, b: Pt, c: Pt, d: Pt): boolean => {
  const d1 = cross(b.x - a.x, b.y - a.y, c.x - a.x, c.y - a.y)
  const d2 = cross(b.x - a.x, b.y - a.y, d.x - a.x, d.y - a.y)
  const d3 = cross(d.x - c.x, d.y - c.y, a.x - c.x, a.y - c.y)
  const d4 = cross(d.x - c.x, d.y - c.y, b.x - c.x, b.y - c.y)
  return ((d1 > 0 && d2 < 0) || (d1 < 0 && d2 > 0)) && ((d3 > 0 && d4 < 0) || (d3 < 0 && d4 > 0))
}

export const distSegmentToSegment = (a: Pt, b: Pt, c: Pt, d: Pt): number => {
  if (segmentsIntersect(a, b, c, d)) return 0
  return Math.min(
    distPointToSegment(a, c, d),
    distPointToSegment(b, c, d),
    distPointToSegment(c, a, b),
    distPointToSegment(d, a, b),
  )
}

/** Shortest distance between two polylines (Infinity if either is empty). */
export const distPolylineToPolyline = (A: Pt[], B: Pt[]): number => {
  if (A.length === 0 || B.length === 0) return Infinity
  if (A.length === 1 && B.length === 1) return dist(A[0]!, B[0]!)
  if (A.length === 1) return distPointToPolyline(A[0]!, B)
  if (B.length === 1) return distPointToPolyline(B[0]!, A)
  let best = Infinity
  for (let i = 0; i < A.length - 1; i++) {
    for (let j = 0; j < B.length - 1; j++) {
      const d = distSegmentToSegment(A[i]!, A[i + 1]!, B[j]!, B[j + 1]!)
      if (d < best) {
        best = d
        if (best === 0) return 0
      }
    }
  }
  return best
}

export const distPointToPolyline = (p: Pt, points: Pt[]): number => {
  if (points.length === 0) return Infinity
  if (points.length === 1) return dist(p, points[0]!)
  let best = Infinity
  for (let i = 0; i < points.length - 1; i++) {
    const d = distPointToSegment(p, points[i]!, points[i + 1]!)
    if (d < best) best = d
  }
  return best
}

/** Shortest distance between two sets of polylines (Infinity if either is empty). */
export const distPathsToPaths = (A: Pt[][], B: Pt[][]): number => {
  let best = Infinity
  for (const a of A) {
    for (const b of B) {
      const d = distPolylineToPolyline(a, b)
      if (d < best) {
        best = d
        if (best === 0) return 0
      }
    }
  }
  return best
}

/** Even-odd point-in-polygon test; the polygon is treated as implicitly closed. */
export const pointInPolygon = (p: Pt, polygon: Pt[]): boolean => {
  let inside = false
  for (let i = 0, j = polygon.length - 1; i < polygon.length; j = i++) {
    const a = polygon[i]!
    const b = polygon[j]!
    const crosses = a.y > p.y !== b.y > p.y
    if (crosses) {
      const x = ((b.x - a.x) * (p.y - a.y)) / (b.y - a.y) + a.x
      if (p.x < x) inside = !inside
    }
  }
  return inside
}

/**
 * Smallest distance from a polyline to the boundary of a closed polygon.
 * Zero when the polyline crosses the boundary or lies (partly) inside it.
 */
export const distPolylineToPolygon = (points: Pt[], polygon: Pt[]): number => {
  for (const p of points) if (pointInPolygon(p, polygon)) return 0
  return distPolylineToPolyline(points, [...polygon, polygon[0]!])
}

/** Signed area of a closed polygon; positive when counter-clockwise. */
export const polygonSignedArea = (polygon: Pt[]): number => {
  let sum = 0
  for (let i = 0; i < polygon.length; i++) {
    const a = polygon[i]!
    const b = polygon[(i + 1) % polygon.length]!
    sum += a.x * b.y - b.x * a.y
  }
  return sum / 2
}

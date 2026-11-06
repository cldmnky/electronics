// The Open Source Hardware gear as board silkscreen ink.
//
// The official artwork is a filled polygon; tscircuit has no filled-polygon
// silkscreen primitive, and an image object is silently dropped by the Gerber
// exporter. So the polygon is converted to an even-odd scanline fill: short
// horizontal strokes at a pitch finer than the aperture, which overlap into a
// solid mark. Same adaptation as the Froemix base board.
//
// Attribution and share-alike terms: see ./oshw-outline.ts. The gear is the
// community open-hardware mark, not an OSHWA certification mark.
import { OSHW_OUTLINE } from "./oshw-outline"

export type OshwPoint = { x: number; y: number }

/** Row pitch of the fill, in millimetres. Below the 0.2 mm stroke so rows merge. */
export const OSHW_ROW_PITCH = 0.15

/**
 * Gear of `width` millimetres centred on (cx, cy), as silkscreen path routes.
 *
 * `mirror` is set for bottom-layer marks: the exporter mirrors the bottom
 * silkscreen image, so the artwork is mirrored here about its own centre to
 * read the right way round from the rear of the board.
 */
export function oshwPaths(cx: number, cy: number, width = 7, mirror = false): OshwPoint[][] {
  const minX = Math.min(...OSHW_OUTLINE.map((p) => p[0]))
  const maxX = Math.max(...OSHW_OUTLINE.map((p) => p[0]))
  const minY = Math.min(...OSHW_OUTLINE.map((p) => p[1]))
  const maxY = Math.max(...OSHW_OUTLINE.map((p) => p[1]))
  const scale = width / (maxX - minX)
  const midX = (minX + maxX) / 2
  const midY = (minY + maxY) / 2
  const sign = mirror ? -1 : 1
  const place = (x: number, y: number): OshwPoint => ({
    x: cx + sign * (x - midX) * scale,
    y: cy - (y - midY) * scale,
  })
  const result: OshwPoint[][] = []
  for (let y = minY + OSHW_ROW_PITCH / scale / 2; y < maxY; y += OSHW_ROW_PITCH / scale) {
    const crossings: number[] = []
    for (let i = 0; i < OSHW_OUTLINE.length; i++) {
      const a = OSHW_OUTLINE[i]!
      const b = OSHW_OUTLINE[(i + 1) % OSHW_OUTLINE.length]!
      if ((a[1] > y) !== (b[1] > y)) {
        crossings.push(a[0] + ((y - a[1]) * (b[0] - a[0])) / (b[1] - a[1]))
      }
    }
    crossings.sort((p, q) => p - q)
    if (crossings.length % 2 !== 0) throw new Error("oshw: scanline does not close the gear outline")
    for (let i = 0; i < crossings.length; i += 2) {
      result.push([place(crossings[i]!, y), place(crossings[i + 1]!, y)])
    }
  }
  return result
}

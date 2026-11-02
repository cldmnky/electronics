// OSHW gear: scaled, scanline-filled version of the official artwork.
// Attribution / share-alike notice and source: ../assets/README.md.
import { OSHW_OUTLINE } from "./oshw-outline"

type Point = { x: number; y: number }

/** 0.2mm silk strokes, overlapping by 0.05mm, with centre in board coordinates. */
export function oshwPaths(cx: number, cy: number, width = 7): Point[][] {
  const minX = Math.min(...OSHW_OUTLINE.map(p => p[0]))
  const maxX = Math.max(...OSHW_OUTLINE.map(p => p[0]))
  const minY = Math.min(...OSHW_OUTLINE.map(p => p[1]))
  const maxY = Math.max(...OSHW_OUTLINE.map(p => p[1]))
  const scale = width / (maxX - minX)
  const midX = (minX + maxX) / 2, midY = (minY + maxY) / 2
  const result: Point[][] = []
  for (let y = minY + 0.1 / scale; y < maxY; y += 0.15 / scale) {
    const intersections: number[] = []
    for (let i = 0; i < OSHW_OUTLINE.length; i++) {
      const a = OSHW_OUTLINE[i]!, b = OSHW_OUTLINE[(i + 1) % OSHW_OUTLINE.length]!
      if ((a[1] > y) !== (b[1] > y)) intersections.push(a[0] + (y-a[1]) * (b[0]-a[0]) / (b[1]-a[1]))
    }
    intersections.sort((a,b) => a-b)
    if (intersections.length % 2) throw new Error("OSHW gear scanline is not closed")
    for (let i = 0; i < intersections.length; i += 2) {
      // X is mirrored for correct reading from the back of the board.
      result.push([
        { x: cx - (intersections[i]! - midX) * scale, y: cy - (y - midY) * scale },
        { x: cx - (intersections[i+1]! - midX) * scale, y: cy - (y - midY) * scale },
      ])
    }
  }
  return result
}

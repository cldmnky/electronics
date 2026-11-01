/// <reference types="bun-types" />
// Panel geometry helpers: the primitives every panel check depends on.

import { describe, expect, test } from "bun:test"
import {
  arcSagitta,
  distPathsToPaths,
  distPolylineToPolygon,
  pointInPolygon,
  polygonSignedArea,
  polylineBounds,
  roundedRectPolygon,
  segmentsIntersect,
} from "./panel-geometry"
import { signedDistanceToRoundedRect } from "./panel-checks"

const WINDOW = { cx: 0, cy: 42.8, width: 30.5, height: 16, radius: 0.5 }

describe("rounded rectangle polygon", () => {
  const polygon = roundedRectPolygon({
    cx: WINDOW.cx,
    cy: WINDOW.cy,
    width: WINDOW.width,
    height: WINDOW.height,
    radius: WINDOW.radius,
    segmentsPerCorner: 8,
  })

  test("keeps the exact bounding box, so the window size does not change", () => {
    const b = polylineBounds(polygon)!
    expect(b.width).toBeCloseTo(30.5, 6)
    expect(b.height).toBeCloseTo(16, 6)
    expect(b.cx).toBeCloseTo(0, 6)
    expect(b.cy).toBeCloseTo(42.8, 6)
  })

  test("every vertex lies on the R0.5 rounded rectangle", () => {
    for (const p of polygon) {
      expect(Math.abs(signedDistanceToRoundedRect(p, WINDOW))).toBeLessThan(1e-6)
    }
  })

  test("8 chords per corner, far below the 0.1mm chord limit", () => {
    expect(arcSagitta(0.5, 8)).toBeLessThan(0.01)
    expect(arcSagitta(0.5, 1)).toBeGreaterThan(0.1)
    // a coarse corner would show up as a vertex far from the true arc
    const coarse = roundedRectPolygon({ ...WINDOW, segmentsPerCorner: 2 })
    const maxDeviation = Math.max(
      ...coarse.map((p) => Math.abs(signedDistanceToRoundedRect(p, WINDOW))),
    )
    expect(maxDeviation).toBeLessThan(0.01) // vertices are on the arc ...
    expect(coarse.length).toBeLessThan(polygon.length)
  })

  test("is a closed, counter-clockwise loop without duplicate vertices", () => {
    expect(polygonSignedArea(polygon)).toBeGreaterThan(0)
    const keys = new Set(polygon.map((p) => `${p.x},${p.y}`))
    expect(keys.size).toBe(polygon.length)
  })
})

describe("distance and containment", () => {
  const polygon = roundedRectPolygon({ ...WINDOW, segmentsPerCorner: 8 })

  test("a point inside the window measures 0, a point outside measures its gap", () => {
    expect(distPolylineToPolygon([{ x: 0, y: 42.8 }], polygon)).toBe(0)
    expect(distPolylineToPolygon([{ x: 0, y: 60 }], polygon)).toBeCloseTo(60 - (42.8 + 8), 6)
    expect(pointInPolygon({ x: 0, y: 42.8 }, polygon)).toBe(true)
    expect(pointInPolygon({ x: 0, y: 60 }, polygon)).toBe(false)
  })

  test("crossing segments have distance 0", () => {
    expect(
      segmentsIntersect({ x: -1, y: 0 }, { x: 1, y: 0 }, { x: 0, y: -1 }, { x: 0, y: 1 }),
    ).toBe(true)
    expect(
      distPathsToPaths([[{ x: -1, y: 0 }, { x: 1, y: 0 }]], [[{ x: 0, y: 1 }, { x: 0, y: 2 }]]),
    ).toBeCloseTo(1, 9)
  })
})

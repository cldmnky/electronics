/// <reference types="bun-types" />
import { expect, test } from "bun:test"
import {
  boardEnvelopes, boxDistance, clearanceLines, fastenerClearance, nearestObstacle,
  type CircuitElement, type EnvelopeSpec, type Obstacle,
} from "./mechanical-clearance"

type Part = {
  ref: string
  layer: "top" | "bottom"
  courtyard?: { x: number; y: number; w: number; h: number }
  pads?: { x: number; y: number; w?: number; h?: number; layers?: ("top" | "bottom")[] }[]
}

/** Minimal built-circuit JSON: exactly the elements the check reads. */
const board = (parts: Part[], extra: CircuitElement[] = []): CircuitElement[] => {
  const elements: CircuitElement[] = []
  parts.forEach((p, i) => {
    elements.push({ type: "source_component", name: p.ref, source_component_id: `s${i}` })
    elements.push({ type: "pcb_component", pcb_component_id: `pc${i}`, source_component_id: `s${i}`, layer: p.layer })
    if (p.courtyard)
      elements.push({
        type: "pcb_courtyard_rect", pcb_courtyard_rect_id: `cr${i}`, pcb_component_id: `pc${i}`, layer: p.layer,
        center: { x: p.courtyard.x, y: p.courtyard.y }, width: p.courtyard.w, height: p.courtyard.h,
      })
    p.pads?.forEach((pad, n) => {
      elements.push({
        type: "pcb_smtpad", pcb_smtpad_id: `pad${i}_${n}`, pcb_component_id: `pc${i}`, shape: "rect",
        layer: pad.layers?.[0] ?? p.layer, layers: pad.layers, x: pad.x, y: pad.y,
        width: pad.w ?? 1, height: pad.h ?? 1,
      })
    })
  })
  return [...elements, ...extra]
}

const envelopes = (...specs: [EnvelopeSpec["face"], number][]): EnvelopeSpec[] =>
  specs.map(([face, od], i) => ({ hardware: "spacer", face, od, status: "limit", label: `spec${i}` }))

const hole = (x: number, y: number, diameter = 3.2) => ({ x, y, diameter })

test("a courtyard is the envelope, and a component without one falls back to its pads", () => {
  const env = boardEnvelopes(board([
    { ref: "A", layer: "bottom", courtyard: { x: 10, y: 0, w: 4, h: 2 } },
    { ref: "B", layer: "bottom", pads: [{ x: 0, y: 5, w: 2, h: 1 }] },
  ]))
  expect(env.kinds.courtyard).toBe(1)
  expect(env.kinds.pads).toBe(1)
  expect(env.unmodelled).toEqual([])
  const a = env.obstacles.find((o) => o.ref === "A")!
  expect([a.minX, a.maxX, a.minY, a.maxY]).toEqual([8, 12, -1, 1])
  expect(env.obstacles.find((o) => o.ref === "B")!.kind).toBe("pads")
})

test("a pads fallback never overrides a courtyard on the SAME face", () => {
  const env = boardEnvelopes(board([{
    ref: "A", layer: "top", courtyard: { x: 0, y: 0, w: 20, h: 20 }, pads: [{ x: 8, y: 8, w: 1, h: 1 }],
  }]))
  expect(env.obstacles.filter((o) => o.ref === "A")).toHaveLength(1)
  expect(env.obstacles[0]!.kind).toBe("courtyard")
})

test("component faces are respected: a top part is not an obstacle on the bottom face", () => {
  const elements = board([
    { ref: "TOP", layer: "top", courtyard: { x: 3, y: 0, w: 2, h: 2 } },
    { ref: "BOT", layer: "bottom", courtyard: { x: 6, y: 0, w: 2, h: 2 } },
  ])
  const { obstacles } = boardEnvelopes(elements)
  expect(nearestObstacle(obstacles, 0, 0, "top").obstacle!.ref).toBe("TOP")
  expect(nearestObstacle(obstacles, 0, 0, "bottom").obstacle!.ref).toBe("BOT")
  // Nearest is a distance, not a list order.
  expect(nearestObstacle(obstacles, 0, 0, "top").distance).toBeCloseTo(2, 9)
})

test("through-hole pads sit on both faces; a bottom body does not", () => {
  const elements = board([{
    ref: "TH", layer: "bottom", courtyard: { x: 0, y: 40, w: 2, h: 2 },
    pads: [{ x: 4, y: 0, w: 1, h: 1, layers: ["top", "bottom"] }],
  }])
  const { obstacles } = boardEnvelopes(elements)
  expect(nearestObstacle(obstacles, 0, 0, "top").obstacle!.ref).toBe("TH")
  expect(nearestObstacle(obstacles, 0, 0, "bottom").obstacle!.kind).toBe("courtyard")
})

test("box distance is signed: negative inside, zero on the edge, exact outside", () => {
  const o: Obstacle = { ref: "A", kind: "courtyard", face: "top", minX: -1, minY: -1, maxX: 1, maxY: 1 }
  expect(boxDistance(0, 0, o)).toBeCloseTo(-1, 9)
  expect(boxDistance(1, 0, o)).toBeCloseTo(0, 9)
  expect(boxDistance(3, 5, o)).toBeCloseTo(Math.hypot(2, 4), 9) // 2 in x, 4 in y
})

test("an empty board reports no obstacle rather than a zero distance", () => {
  const { obstacles } = boardEnvelopes([])
  const nearest = nearestObstacle(obstacles, 0, 0, "bottom")
  expect(nearest.obstacle).toBeNull()
  expect(nearest.distance).toBe(Infinity)
  const [row] = fastenerClearance({ board: "x", elements: [], holes: [hole(0, 0)], envelopes: envelopes(["top", 5]), budget: 0.45 })
  expect(row!.verdict).toBe("ok")
  expect(row!.limit).toBe(Infinity)
})

test("the row limit is derived from the measured distance, not from a fixed number", () => {
  // courtyard edge 2.95mm from the hole centre (the C25 distance on the base board)
  const elements = board([{ ref: "C25", layer: "bottom", courtyard: { x: 3.95, y: 0, w: 2, h: 2 } }])
  const run = (od: number) => fastenerClearance({
    board: "base", elements, holes: [hole(0, 0)], envelopes: envelopes(["bottom", od]), budget: 0.45,
  })[0]!
  const ok = run(5.0)
  expect(ok.distance).toBeCloseTo(2.95, 9) // courtyard edge, not its centre
  expect(ok.limit).toBeCloseTo(5.0, 9) // 2 x (2.95 - 0.45)
  expect(ok.verdict).toBe("ok")
  expect(ok.clearance).toBeCloseTo(0.45, 9)
  const tight = run(5.01)
  expect(tight.verdict).toBe("tight")
  expect(tight.clearance).toBeCloseTo(0.445, 9)
  const collision = run(6.0)
  expect(collision.verdict).toBe("collision")
  expect(collision.clearance).toBeCloseTo(-0.05, 9)
})

test("every hole is reported for every face envelope, deterministically", () => {
  const elements = board([{ ref: "A", layer: "bottom", courtyard: { x: 0, y: 6, w: 2, h: 2 } }])
  const rows = fastenerClearance({
    board: "front", elements, holes: [hole(0, 0), hole(10, 0)], envelopes: envelopes(["top", 5], ["bottom", 5]), budget: 0.45,
  })
  expect(rows).toHaveLength(4)
  expect(rows.map((r) => `${r.hole.x}/${r.face}`)).toEqual(["0/top", "0/bottom", "10/top", "10/bottom"])
  expect(rows[1]!.nearest!.ref).toBe("A")
  expect(rows[0]!.nearest).toBeNull() // the part is on the bottom face only
  expect(rows[2]!.nearest).toBeNull() // nothing near the second hole
  const lines = clearanceLines(rows)
  expect(lines[1]).toContain("OK")
  expect(lines[1]).toContain("A courtyard")
  expect(lines[2]).toContain("no component in reach")
})

// ---------------------------------------------------------------------------
// Non-finite input rejection: a NaN geometry must fail, never pass as "clear".
// ---------------------------------------------------------------------------
test("a non-finite coordinate is rejected instead of becoming a distance", () => {
  const withPad = (value: unknown) => board([{ ref: "P", layer: "top", pads: [{ x: value as number, y: 0, w: 1, h: 1 }] }])
  for (const bad of [Number.NaN, Number.POSITIVE_INFINITY, Number.NEGATIVE_INFINITY, undefined, "2"]) {
    expect(() => boardEnvelopes(withPad(bad))).toThrow(RangeError)
  }
  const withCourtyard = (patch: Record<string, unknown>) => {
    const elements = board([{ ref: "C", layer: "top", courtyard: { x: 0, y: 0, w: 2, h: 2 } }])
    return elements.map((e) => (e.type === "pcb_courtyard_rect" ? { ...e, ...patch } : e))
  }
  expect(() => boardEnvelopes(withCourtyard({ width: Number.NaN }))).toThrow(/courtyard width/)
  expect(() => boardEnvelopes(withCourtyard({ center: { x: 0, y: Number.POSITIVE_INFINITY } }))).toThrow(/courtyard y/)
  const elements = board([{ ref: "A", layer: "top", courtyard: { x: 0, y: 3, w: 2, h: 2 } }])
  const { obstacles } = boardEnvelopes(elements)
  expect(() => nearestObstacle(obstacles, Number.NaN, 0, "top")).toThrow(RangeError)
  expect(() => fastenerClearance({ board: "b", elements, holes: [hole(Number.NaN, 0)], envelopes: envelopes(["top", 5]), budget: 0.45 }))
    .toThrow(RangeError)
  expect(() => fastenerClearance({ board: "b", elements, holes: [hole(0, 0)], envelopes: envelopes(["top", 5]), budget: Number.NaN }))
    .toThrow(RangeError)
})

test("a shape the check cannot measure is rejected loudly", () => {
  const elements = board([{ ref: "A", layer: "top", courtyard: { x: 0, y: 0, w: 2, h: 2 } }])
  expect(() => boardEnvelopes(elements.map((e) => (e.type === "pcb_courtyard_rect" ? { ...e, type: "pcb_courtyard_circle" } : e))))
    .toThrow(/unsupported courtyard element/)
  const unknownPad = board([{ ref: "P", layer: "top", pads: [{ x: 0, y: 0 }] }])
    .map((e) => (e.type === "pcb_smtpad" ? { ...e, shape: "polygon" } : e))
  expect(() => boardEnvelopes(unknownPad)).toThrow(/unsupported pad shape/)
})

test("a component with no geometry at all is named, not silently ignored", () => {
  const env = boardEnvelopes(board([{ ref: "GHOST", layer: "top" }]))
  expect(env.unmodelled).toEqual(["GHOST"])
  expect(env.obstacles).toEqual([])
})

test("every pad shape used by the two boards is measured, conservatively", () => {
  const shapes: Record<string, Record<string, unknown>> = {
    rect: { width: 2, height: 1 },
    circle: { outer_diameter: 1.5 },
    rotated_pill: { width: 0.6, height: 1.6, ccw_rotation: 180 },
    oval: { outer_width: 2, outer_height: 2.6 },
    circular_hole_with_rect_pad: { rect_pad_width: 1.52, rect_pad_height: 1.52 },
  }
  for (const [shape, props] of Object.entries(shapes)) {
    const elements = [
      { type: "source_component", name: shape, source_component_id: "s" },
      { type: "pcb_component", pcb_component_id: "pc", source_component_id: "s", layer: "top" },
      { type: "pcb_plated_hole", pcb_plated_hole_id: "h", pcb_component_id: "pc", shape, x: 0, y: 0, layers: ["top", "bottom"], ...props },
    ]
    const { obstacles } = boardEnvelopes(elements)
    expect(obstacles.length).toBe(2) // both faces
    for (const o of obstacles) {
      // a conservative box never under-states the pad
      expect(o.maxX - o.minX).toBeGreaterThanOrEqual(shape === "rotated_pill" ? Math.hypot(0.6, 1.6) - 1e-9 : 0.6)
      expect(Number.isFinite(o.maxY)).toBe(true)
    }
  }
})

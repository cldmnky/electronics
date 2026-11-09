// Froemix board-to-board fastener envelope check, measured from BUILT geometry.
//
// The standoff holes are the same M3 column on both boards, so two different
// questions are asked at each hole and each face:
//
//   * a face that carries a SPACER end: does the spacer body clear the closest
//     component envelope?
//   * a face that carries a SCREW head/washer: does that head clear it?
//
// Nothing here is read from a picture or from a comment. Courtyards are the
// keepout the design already gates on, so a courtyard is the envelope of a
// component that has one; a component without a courtyard falls back to the
// union of its own copper pads on that face. A convex box is used, which can
// only OVER-state a component, never under-state it.
//
// Coordinates are validated: a NaN or Infinity coordinate throws instead of
// producing a comfortable-looking distance (all comparisons in JavaScript are
// false for NaN, so an unvalidated NaN would silently pass).

export type CircuitElement = Record<string, any>
export type BoardFace = "top" | "bottom"
export type ObstacleKind = "courtyard" | "pads"

export type Obstacle = {
  /** component reference (R12, CN1, ...) */
  ref: string
  kind: ObstacleKind
  face: BoardFace
  minX: number
  minY: number
  maxX: number
  maxY: number
}

export type Pt = { x: number; y: number }

const mm = (value: unknown, what: string): number => {
  if (typeof value !== "number" || !Number.isFinite(value))
    throw new RangeError(`${what} is not a finite millimetre: ${String(value)}`)
  return value
}

const box = (ref: string, kind: ObstacleKind, face: BoardFace, x: number, y: number, w: number, h: number): Obstacle => ({
  ref,
  kind,
  face,
  minX: mm(x - w / 2, `${ref} minX`),
  minY: mm(y - h / 2, `${ref} minY`),
  maxX: mm(x + w / 2, `${ref} maxX`),
  maxY: mm(y + h / 2, `${ref} maxY`),
})

/** Conservative square side for a rotated or pill-shaped pad. */
const circumscribed = (w: number, h: number): number => Math.hypot(w, h)

const facesOf = (element: CircuitElement): BoardFace[] => {
  const layers: unknown = element.layers ?? element.layer
  const list = Array.isArray(layers) ? layers : [layers]
  return list.filter((l): l is BoardFace => l === "top" || l === "bottom")
}

export type BoardEnvelopes = {
  obstacles: Obstacle[]
  /** components that produced no geometry at all on any face */
  unmodelled: string[]
  /** the kept-out kinds, for the printed report */
  kinds: Record<ObstacleKind, number>
}

/**
 * Every component envelope on a built board, per face. Throws on a courtyard
 * shape this check cannot measure, so a new footprint can never silently drop
 * an obstacle, and on a non-finite coordinate.
 */
export const boardEnvelopes = (elements: readonly CircuitElement[]): BoardEnvelopes => {
  const nameOf = new Map<string, string>()
  for (const e of elements) {
    if (e.type === "source_component" && typeof e.name === "string") nameOf.set(e.source_component_id, e.name)
  }
  const sourceOf = new Map<string, string>()
  for (const e of elements) {
    if (e.type !== "pcb_component") continue
    const name = nameOf.get(e.source_component_id)
    if (name) sourceOf.set(e.pcb_component_id, name)
  }

  const obstacles: Obstacle[] = []
  const hasCourtyard = new Set<string>() // `${ref}/${face}`
  const padBox = new Map<string, Obstacle>()

  for (const e of elements) {
    const ref = sourceOf.get(e.pcb_component_id)
    const type = String(e.type)
    if (type.startsWith("pcb_courtyard") && type !== "pcb_courtyard_rect")
      throw new Error(`unsupported courtyard element ${type} on ${ref ?? e.pcb_component_id}: teach mechanical-clearance.ts this shape`)
    if (type === "pcb_courtyard_rect") {
      if (!ref) throw new Error(`courtyard ${e.pcb_courtyard_rect_id} has no named component`)
      const face = facesOf(e)[0]
      if (!face) throw new Error(`courtyard of ${ref} has no top/bottom layer`)
      const centre = e.center ?? {}
      obstacles.push(box(ref, "courtyard", face, mm(centre.x, `${ref} courtyard x`), mm(centre.y, `${ref} courtyard y`),
        mm(e.width, `${ref} courtyard width`), mm(e.height, `${ref} courtyard height`)))
      hasCourtyard.add(`${ref}/${face}`)
      continue
    }
    if (type !== "pcb_smtpad" && type !== "pcb_plated_hole") continue
    if (!ref) continue
    const x = mm(e.x, `${ref} pad x`)
    const y = mm(e.y, `${ref} pad y`)
    const shape = String(e.shape ?? "rect")
    let w: number
    let h: number
    if (shape === "rect" || shape === "rotated_rect")
      [w, h] = [mm(e.width, `${ref} pad width`), mm(e.height, `${ref} pad height`)]
    else if (shape === "circle") {
      const d = e.outer_diameter ?? (typeof e.radius === "number" ? mm(e.radius, `${ref} radius`) * 2 : undefined)
      ;[w, h] = [mm(d, `${ref} pad diameter`), mm(d, `${ref} pad diameter`)]
    } else if (shape === "rotated_pill" || shape === "pill") {
      const side = circumscribed(mm(e.width, `${ref} pad width`), mm(e.height, `${ref} pad height`))
      ;[w, h] = [side, side]
    } else if (shape === "oval") {
      const sideW = circumscribed(mm(e.outer_width, `${ref} pad outer_width`), mm(e.outer_height, `${ref} pad outer_height`))
      ;[w, h] = [sideW, sideW]
    } else if (shape === "circular_hole_with_rect_pad") {
      ;[w, h] = [mm(e.rect_pad_width, `${ref} pad width`), mm(e.rect_pad_height, `${ref} pad height`)]
    } else throw new Error(`unsupported pad shape ${shape} on ${ref}: teach mechanical-clearance.ts this shape`)

    for (const face of facesOf(e)) {
      const key = `${ref}/${face}`
      const prev = padBox.get(key)
      padBox.set(key, prev
        ? { ...prev, minX: Math.min(prev.minX, x - w / 2), minY: Math.min(prev.minY, y - h / 2),
            maxX: Math.max(prev.maxX, x + w / 2), maxY: Math.max(prev.maxY, y + h / 2) }
        : box(ref, "pads", face, x, y, w, h))
    }
  }

  for (const [key, obstacle] of padBox) if (!hasCourtyard.has(key)) obstacles.push(obstacle)

  const withGeometry = new Set(obstacles.map((o) => o.ref))
  const unmodelled = [...new Set([...sourceOf.values()])].filter((ref) => !withGeometry.has(ref)).sort()
  return {
    obstacles,
    unmodelled,
    kinds: {
      courtyard: obstacles.filter((o) => o.kind === "courtyard").length,
      pads: obstacles.filter((o) => o.kind === "pads").length,
    },
  }
}

/** Signed distance from a point to a box: negative inside, 0 on the edge. */
export const boxDistance = (x: number, y: number, o: Obstacle): number => {
  const px = mm(x, "query x")
  const py = mm(y, "query y")
  const dx = Math.max(o.minX - px, 0, px - o.maxX)
  const dy = Math.max(o.minY - py, 0, py - o.maxY)
  if (dx === 0 && dy === 0) return -Math.min(px - o.minX, o.maxX - px, py - o.minY, o.maxY - py)
  return Math.hypot(dx, dy)
}

export type Nearest = { obstacle: Obstacle | null; distance: number }

/** Closest obstacle on one face; distance is Infinity when the face is empty. */
export const nearestObstacle = (obstacles: readonly Obstacle[], x: number, y: number, face: BoardFace): Nearest => {
  let best: Nearest = { obstacle: null, distance: Infinity }
  for (const o of obstacles) {
    if (o.face !== face) continue
    const d = boxDistance(x, y, o)
    // Ties keep the first hit, which is deterministic for a stable board order.
    if (d < best.distance) best = { obstacle: o, distance: d }
  }
  return best
}

/** One hardware envelope on one board face. */
export type EnvelopeSpec = {
  hardware: "spacer" | "screw"
  face: BoardFace
  od: number
  /** "limit" is the approved purchase limit; "pending" is an unselected part */
  status: "limit" | "pending"
  label: string
}

export type ClearanceRow = {
  board: string
  hole: Pt & { diameter: number }
  hardware: EnvelopeSpec["hardware"]
  status: EnvelopeSpec["status"]
  label: string
  face: BoardFace
  od: number
  nearest: Obstacle | null
  distance: number
  clearance: number
  limit: number
  verdict: "ok" | "tight" | "collision"
}

/** Hardware-vs-envelope rule, duplicated here so the module stays dependency-free. */
const verdictOf = (distance: number, od: number, budget: number): ClearanceRow["verdict"] => {
  const clearance = distance - od / 2
  if (!Number.isFinite(distance)) return "ok" // empty face: no component in reach
  if (clearance < 0) return "collision"
  return 2 * (distance - budget) >= od ? "ok" : "tight"
}

export type ClearanceInput = {
  board: string
  elements: readonly CircuitElement[]
  holes: readonly (Pt & { diameter: number })[]
  envelopes: readonly EnvelopeSpec[]
  budget: number
}

/** One row per hole x face-envelope pair, for every hole given. */
export const fastenerClearance = (input: ClearanceInput): ClearanceRow[] => {
  const budget = mm(input.budget, "budget")
  const { obstacles } = boardEnvelopes(input.elements)
  const rows: ClearanceRow[] = []
  for (const hole of input.holes)
    for (const spec of input.envelopes) {
      const nearest = nearestObstacle(obstacles, hole.x, hole.y, spec.face)
      rows.push({
        board: input.board,
        hole: { x: hole.x, y: hole.y, diameter: hole.diameter },
        hardware: spec.hardware,
        status: spec.status,
        label: spec.label,
        face: spec.face,
        od: spec.od,
        nearest: nearest.obstacle,
        distance: nearest.distance,
        clearance: nearest.distance - spec.od / 2,
        limit: Number.isFinite(nearest.distance) ? Math.max(0, 2 * (nearest.distance - budget)) : Infinity,
        verdict: verdictOf(nearest.distance, spec.od, budget),
      })
    }
  return rows
}

/** Printed table, one line per row, stable order. */
export const clearanceLines = (rows: readonly ClearanceRow[]): string[] =>
  rows.map((r) => {
    const where = `${r.board} (${r.hole.x.toFixed(2)}, ${r.hole.y.toFixed(2)}) ${r.face}`
    const nearest = r.nearest
      ? `${r.nearest.ref} ${r.nearest.kind} ${r.distance.toFixed(2)}mm`
      : "no component in reach"
    const clearance = Number.isFinite(r.clearance) ? `${r.clearance.toFixed(2)}mm` : "n/a"
    const limit = Number.isFinite(r.limit) ? r.limit.toFixed(2) : "none"
    return `${r.verdict.toUpperCase().padEnd(9)} ${where} ${r.hardware} OD${r.od.toFixed(1)} (${r.status}): ${nearest}; clearance ${clearance}, OD limit ${limit}`
  })

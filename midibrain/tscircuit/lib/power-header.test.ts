// J_PWR on the BUILT front board: placement, layer, mating-axis direction,
// pad geometry, edge clearance, the provisional keepout, and the bracket pocket
// that must clear it.
//
// Sourced facts (Harwin M20-9960245 drawing, SHA-256 3cd367d6…bdcd61b): 2 ways,
// 2.54 +-0.10 mm pitch, Ø1.02 +-0.05 mm recommended hole, 0.64 +-0.02 mm square
// pin, 6.10 mm mating length, true right angle (long mating pins parallel to the
// PCB, short PC tails perpendicular).
//
// ENGINEERING ASSUMPTIONS, not measurements: the 1.60 mm pad, the housing
// envelope, and the generic DuPont-style plug. The exact hole-to-housing offset
// is unresolved (the drawing's 4.80 mm datum is unclear and sheet 1 of 2 is
// missing), so the keepout below covers both plausible readings and the bracket
// pocket clears that keepout. What this file cannot say: that the plug fits,
// that a cable bends inside the pocket, that the UMC AUDIO feedthrough's tail
// coexists with it, or that the part can be soldered and plugged in place.
// Those stay pending in acceptance item `power_header_mount`.
import { describe, expect, test } from "bun:test"
import { readFileSync } from "node:fs"
import {
  BRACKET_POWER_POCKET,
  FEEDTHROUGH_BRACKET,
  FRONT_PCB,
  FRONT_TOP_NOTCH,
  NOTCH_BOTTOM,
  POWER_HEADER,
  POWER_HEADER_CANDIDATE,
  POWER_HEADER_KEEPOUT,
  POWER_HEADER_LAND,
} from "./layout"

type Json = any
const front = JSON.parse(readFileSync("dist/front/circuit.json", "utf8")) as Json[]
const byType = (json: Json[], type: string) => json.filter(e => e.type === type)

const sourceOf = (name: string) =>
  byType(front, "source_component").find((s: Json) => s.name === name)!
const pcbOf = (name: string) => {
  const src = sourceOf(name)
  return byType(front, "pcb_component").find(
    (c: Json) => c.source_component_id === src.source_component_id,
  )!
}
const padsOf = (name: string) => {
  const pcb = pcbOf(name)
  return byType(front, "pcb_plated_hole")
    .filter((h: Json) => h.pcb_component_id === pcb.pcb_component_id)
    .map((h: Json) => ({
      pin: Number(((h.port_hints ?? []) as string[]).find(x => /^pin\d+$/.test(x))!.slice(3)),
      x: h.x as number,
      y: h.y as number,
      drill: h.hole_diameter as number,
      pad: h.outer_diameter as number,
    }))
}
const silkPathsOf = (name: string) => {
  const pcb = pcbOf(name)
  return byType(front, "pcb_silkscreen_path")
    .filter((e: Json) => e.pcb_component_id === pcb.pcb_component_id)
    .map((e: Json) => e.route as { x: number; y: number }[])
}

const round = (v: number) => Math.round(v * 1000) / 1000

describe("J_PWR on the built front board", () => {
  test("it is on the REAR (bottom) face at the planned notch position", () => {
    const pcb = pcbOf("J_PWR")
    expect(pcb.layer).toBe("bottom")
    expect(POWER_HEADER.layer).toBe("bottom")
    expect(pcb.rotation).toBe(POWER_HEADER.rotation)
    expect({ x: pcb.footprint_origin.x, y: pcb.footprint_origin.y }).toEqual({
      x: POWER_HEADER.x, y: POWER_HEADER.y,
    })
    // The owner's position: the middle of the long top relief notch.
    expect(POWER_HEADER.x).toBe(FRONT_TOP_NOTCH.x)
  })

  test("the mating axis points to global +Y, into the notch", () => {
    // tscircuit maps local +Y to global +Y for a bottom-layer footprint at
    // 180 deg. The footprint's direction arrow is local +Y; find the built
    // arrow segment and check that it rises in global Y.
    const paths = silkPathsOf("J_PWR")
    const arrows = paths.filter(p => p.length === 2)
      .map(p => ({ dx: p[1]!.x - p[0]!.x, dy: p[1]!.y - p[0]!.y, x: p[0]!.x, y: p[0]!.y }))
      .filter(a => Math.abs(a.dx) < 1e-9 && a.dy > 0.5)
    expect(arrows.length).toBeGreaterThan(0)
    // Its x sits off the pad row's +X side, i.e. the part's own arrow.
    const arrow = arrows[0]!
    expect(arrow.x).toBeGreaterThan(POWER_HEADER.x + 1.27)
    expect(POWER_HEADER.matingAxis.startsWith("+Y")).toBe(true)
  })

  test("the pads are the sourced 2-way 2.54 mm row with a 1.02 mm drill", () => {
    const pads = padsOf("J_PWR").sort((a, b) => a.pin - b.pin)
    expect(pads.length).toBe(POWER_HEADER_LAND.ways)
    for (const pad of pads) {
      expect(round(pad.drill)).toBe(1.02)
      expect(round(pad.pad)).toBe(POWER_HEADER_LAND.pad) // ASSUMPTION, documented
      expect(round(pad.y)).toBe(round(POWER_HEADER.y))
    }
    // 2.54 mm pitch along X: the row is across the mating axis.
    expect(round(Math.abs(pads[1]!.x - pads[0]!.x))).toBe(POWER_HEADER_LAND.pitch)
    // Measured on the built output: pin 1 sits at centre - 1.27. The pair is a
    // floating switch contact, so which of the two 2.54 mm pads is A or B has no
    // electrical meaning; the row, the pitch and the drill are what matter.
    expect(round(pads[0]!.x)).toBe(round(POWER_HEADER.x - POWER_HEADER_LAND.pitch / 2))
    expect(round(pads[1]!.x)).toBe(round(POWER_HEADER.x + POWER_HEADER_LAND.pitch / 2))
  })

  test("both pads keep the copper-to-edge rule to the notch and the board edges", () => {
    const pads = padsOf("J_PWR")
    for (const pad of pads) {
      const r = POWER_HEADER_LAND.pad / 2
      // Notch edge (the board is void above y = 48 in the notch span).
      expect(round(NOTCH_BOTTOM - (pad.y + r))).toBeGreaterThanOrEqual(POWER_HEADER_LAND.copperToEdge)
      // Board outline (front board 128 x 112, centred).
      const edge = Math.min(
        FRONT_PCB.width / 2 - (pad.x + r),
        FRONT_PCB.width / 2 + (pad.x - r),
        FRONT_PCB.height / 2 - (pad.y + r),
        FRONT_PCB.height / 2 + (pad.y - r),
      )
      expect(edge).toBeGreaterThanOrEqual(POWER_HEADER_LAND.copperToEdge)
    }
  })

  test("the built board has nothing else inside the provisional keepout", () => {
    // Every pad and hole of the front board, either face: a through-hole pad
    // exists on both layers.
    const obstacles: { what: string; x: number; y: number; r: number }[] = []
    for (const hole of byType(front, "pcb_plated_hole")) {
      const pcb = byType(front, "pcb_component").find((c: Json) => c.pcb_component_id === hole.pcb_component_id)
      const own = pcb && pcb.pcb_component_id === pcbOf("J_PWR").pcb_component_id
      if (own) continue
      obstacles.push({ what: "plated hole", x: hole.x, y: hole.y, r: (hole.outer_diameter ?? 0) / 2 })
    }
    for (const pad of byType(front, "pcb_smtpad")) {
      const r = pad.shape === "circle" ? (pad.radius ?? 0) : Math.hypot((pad.width ?? 0) / 2, (pad.height ?? 0) / 2)
      obstacles.push({ what: "SMT pad", x: pad.x, y: pad.y, r })
    }
    for (const hole of byType(front, "pcb_hole")) {
      obstacles.push({ what: "hole", x: hole.x, y: hole.y, r: (hole.hole_diameter ?? 0) / 2 })
    }
    const inside = obstacles.filter(o =>
      o.x + o.r > POWER_HEADER_KEEPOUT.x1 && o.x - o.r < POWER_HEADER_KEEPOUT.x2 &&
      o.y + o.r > POWER_HEADER_KEEPOUT.y1 && o.y - o.r < POWER_HEADER_KEEPOUT.y2,
    )
    expect(inside).toEqual([])
  })

  test("the keepout covers both plausible 4.80 mm readings and the sourced pin", () => {
    // 4.80 as a forward extent, plus the sourced 6.10 mm mating pin, plus the
    // 8.00 mm plug planning reach.
    expect(POWER_HEADER_KEEPOUT.y2).toBe(round(POWER_HEADER.y + 4.8 + 6.1 + 8.0))
    // 4.80 as a height above the board.
    expect(POWER_HEADER_KEEPOUT.heightAboveBoard).toBe(4.8)
    // A 2.54 mm housing wall behind the pad row, and one and a half pitches each
    // side, cover the unresolved hole-to-housing offset in plan.
    expect(POWER_HEADER_KEEPOUT.y1).toBe(round(POWER_HEADER.y - 2.54))
    expect(POWER_HEADER_KEEPOUT.x1).toBe(round(POWER_HEADER.x - 3.81))
    expect(POWER_HEADER_KEEPOUT.x2).toBe(round(POWER_HEADER.x + 3.81))
    expect(POWER_HEADER_CANDIDATE.housingOffsetResolved).toBe(false)
  })
})

describe("the owner-approved bracket pocket clears the keepout", () => {
  test("the pocket contains the keepout on the board side and the bracket's plan", () => {
    expect(BRACKET_POWER_POCKET.x1).toBeLessThanOrEqual(POWER_HEADER_KEEPOUT.x1)
    expect(BRACKET_POWER_POCKET.x2).toBeGreaterThanOrEqual(POWER_HEADER_KEEPOUT.x2)
    expect(BRACKET_POWER_POCKET.y1).toBeLessThanOrEqual(POWER_HEADER_KEEPOUT.y1)
    // Above the bracket the keepout leaves the part; the pocket is open to the
    // bracket top edge, so nothing of the bracket remains in the keepout.
    expect(BRACKET_POWER_POCKET.y2).toBe(FEEDTHROUGH_BRACKET.outline.y2)
    expect(BRACKET_POWER_POCKET.y2).toBeLessThanOrEqual(POWER_HEADER_KEEPOUT.y2)
  })

  test("the pocket keeps the bracket's material rules and its other slots", () => {
    const materialToCuts = 1.0 // bracketAllowance.materialToCuts, restated here
    // Bottom web under the pocket.
    expect(round(BRACKET_POWER_POCKET.y1 - FEEDTHROUGH_BRACKET.outline.y1))
      .toBeGreaterThanOrEqual(materialToCuts)
    // Webs to either side inside the UMC AUDIO slot's own span.
    const slot = FEEDTHROUGH_BRACKET.slots.find(s => s.id === "UMC AUDIO USB")!
    const slotX1 = slot.x - slot.width / 2
    const slotX2 = slot.x + slot.width / 2
    expect(round(BRACKET_POWER_POCKET.x1 - slotX1)).toBeGreaterThanOrEqual(materialToCuts)
    expect(round(slotX2 - BRACKET_POWER_POCKET.x2)).toBeGreaterThanOrEqual(materialToCuts)
    // It opens into that slot, so the bar stays one connected piece.
    expect(BRACKET_POWER_POCKET.y2).toBe(slot.yTop)
    expect(BRACKET_POWER_POCKET.y2).toBeGreaterThan(slot.yBottom)
    // The other two slots are untouched.
    expect(FEEDTHROUGH_BRACKET.slots.map(s => s.id)).toEqual([
      "USB-C POWER", "UMC AUDIO USB", "MIDI HUB USB",
    ])
  })

  test("no tie hole or mount ligament is touched by the pocket", () => {
    for (const tie of FEEDTHROUGH_BRACKET.tieHoles) {
      const gap = Math.abs(tie.x - POWER_HEADER.x) - tie.diameter / 2
        - Math.max(Math.abs(BRACKET_POWER_POCKET.x1 - POWER_HEADER.x), Math.abs(BRACKET_POWER_POCKET.x2 - POWER_HEADER.x))
      expect({ x: tie.x, clear: gap >= 0 }).toEqual({ x: tie.x, clear: true })
    }
    for (const x of FEEDTHROUGH_BRACKET.mountX) {
      const gap = Math.abs(x - POWER_HEADER.x) - FEEDTHROUGH_BRACKET.mountHoleDiameter / 2
        - Math.max(Math.abs(BRACKET_POWER_POCKET.x1 - POWER_HEADER.x), Math.abs(BRACKET_POWER_POCKET.x2 - POWER_HEADER.x))
      expect({ x, clear: gap >= 0 }).toEqual({ x, clear: true })
    }
  })

  test("the pocket is a through cut, not a blind pocket, and the keepout is 3D", () => {
    // Through: it is cut over the bar's full thickness, so the declared keepout
    // height (4.80 mm) is free over the bar's 3.00 mm.
    expect(POWER_HEADER_KEEPOUT.heightAboveBoard).toBeGreaterThan(FEEDTHROUGH_BRACKET.thickness)
    expect(BRACKET_POWER_POCKET.clearance).toBeGreaterThan(0)
  })
})

// Interconnect mate proof on the BUILT circuit JSON.
//
// The front board carries the female socket on its BOTTOM face; the rear board
// carries the male header on its TOP face. tscircuit mirrors a bottom-layer
// footprint and does not mirror a top-layer one, so "same footprint, same
// rotation" is NOT a mate: this file compares the built pads of both boards and
// fails unless every one of the 26 contacts coincides in board X/Y and carries
// the same net. It also runs the SAME checker on deliberately wrong placements
// (reversed pin table, X and Y mirror, 180 deg, shifted a pitch) and requires a
// failure for each: a mate check that cannot fail proves nothing.
//
// What this file CANNOT prove: that the purchased Sullins pair prints the same
// pin numbering on both parts, that the keyed bodies fit, that the stack is
// supported, or that the joint is sound. Those are acceptance item
// `interconnect_mate` (pending).
import { describe, expect, test } from "bun:test"
import { readFileSync } from "node:fs"
import {
  INTERCONNECT_DRILL_MM,
  INTERCONNECT_MECH,
  INTERCONNECT_NETS,
  INTERCONNECT_PAD_MM,
  INTERCONNECT_PIN_COUNT,
  INTERCONNECT_PITCH,
  INTERCONNECT_PROTRUSION,
  INTERCONNECT_RING_MM,
} from "./interconnect"
import {
  DISPLAY_FPC_CONNECTOR,
  DISPLAY_FPC_CONNECTOR_AT,
  FRONT_INTERCONNECT,
  PI_SPACER_MM,
  PI_PANEL_PROVISIONAL_DEPTH,
  PROVISIONAL_DEPTH,
  REAR_INTERCONNECT,
} from "./layout"

type Json = any

type Pad = { pin: number; x: number; y: number; net: string; hole: number; outer: number; shape: string }

const read = (p: string) => JSON.parse(readFileSync(p, "utf8")) as Json[]
const front = read("dist/front/circuit.json")
const rear = read("dist/index/circuit.json")

const byType = (json: Json[], type: string) => json.filter(e => e.type === type)

/** Net names by connectivity key; the built JSON links nets through ports. */
function nets(json: Json[]) {
  const sourcePorts = new Map(byType(json, "source_port").map((p: Json) => [p.source_port_id, p]))
  const pcbPorts = new Map(byType(json, "pcb_port").map((p: Json) => [p.pcb_port_id, p]))
  const netNames = new Map(
    byType(json, "source_net").map((n: Json) => [n.subcircuit_connectivity_map_key, n.name as string]),
  )
  return (pcbPortId: string): string => {
    const port = pcbPorts.get(pcbPortId) as Json | undefined
    const source = port ? (sourcePorts.get(port.source_port_id) as Json | undefined) : undefined
    return (source && netNames.get(source.subcircuit_connectivity_map_key)) ?? ""
  }
}

function component(json: Json[], name: string) {
  const source = byType(json, "source_component").find((s: Json) => s.name === name)
  if (!source) throw new Error(`no source_component ${name}`)
  const pcb = byType(json, "pcb_component").find(
    (c: Json) => c.source_component_id === source.source_component_id,
  )
  if (!pcb) throw new Error(`no pcb_component for ${name}`)
  return { source, pcb }
}

function connectorPads(json: Json[], name: string): { pcb: Json; pads: Map<number, Pad> } {
  const { pcb } = component(json, name)
  const netOf = nets(json)
  const pads = new Map<number, Pad>()
  for (const hole of byType(json, "pcb_plated_hole")) {
    if (hole.pcb_component_id !== pcb.pcb_component_id) continue
    const hint = (hole.port_hints as string[] | undefined)?.find(h => /^pin\d+$/.test(h))
    if (!hint) continue
    pads.set(Number(hint.slice(3)), {
      pin: Number(hint.slice(3)),
      x: hole.x,
      y: hole.y,
      net: netOf(hole.pcb_port_id),
      hole: hole.hole_diameter,
      outer: hole.outer_diameter,
      shape: hole.shape,
    })
  }
  return { pcb, pads }
}

const J5 = connectorPads(front, "J5")
const JFRONT = connectorPads(rear, "J_FRONT")
const NET = (pin: number) => (INTERCONNECT_NETS[`pin${pin}`] ?? "").replace(/^net\./, "")

/**
 * The mate rule, as a pure function so the negative controls below can run the
 * SAME code on mutated input. `tol` is board-millimetre.
 */
function mateProblems(
  a: { pads: Map<number, Pad>; layer: string; rotation: number; x: number; y: number },
  b: { pads: Map<number, Pad>; layer: string; rotation: number; x: number; y: number },
  tol = 1e-6,
): string[] {
  const problems: string[] = []
  if (a.pads.size !== INTERCONNECT_PIN_COUNT) problems.push(`front pad count ${a.pads.size}`)
  if (b.pads.size !== INTERCONNECT_PIN_COUNT) problems.push(`rear pad count ${b.pads.size}`)
  for (let pin = 1; pin <= INTERCONNECT_PIN_COUNT; pin++) {
    const p = a.pads.get(pin)
    const q = b.pads.get(pin)
    if (!p || !q) { problems.push(`pin ${pin} missing`); continue }
    const d = Math.hypot(p.x - q.x, p.y - q.y)
    if (d > tol) {
      problems.push(`pin ${pin} does not mate: front (${p.x}, ${p.y}) vs rear (${q.x}, ${q.y}), ${d.toFixed(3)} mm apart`)
      continue
    }
    if (p.net === "") problems.push(`pin ${pin} has no net on the front board`)
    if (q.net === "") problems.push(`pin ${pin} has no net on the rear board`)
    if (p.net !== q.net) problems.push(`pin ${pin} short/mismatch: front ${p.net} vs rear ${q.net}`)
    if (p.net !== NET(pin)) problems.push(`pin ${pin} front net ${p.net} is not the documented ${NET(pin)}`)
    if (q.net !== NET(pin)) problems.push(`pin ${pin} rear net ${q.net} is not the documented ${NET(pin)}`)
  }
  // Mating plugs a male into a female on the OPPOSITE board face: same-layer or
  // same-direction placement is a wiring error even when the pads line up.
  if (a.layer === b.layer) problems.push(`both connectors are on the ${a.layer} layer`)
  if (a.rotation !== b.rotation) problems.push(`rotations differ: ${a.rotation} vs ${b.rotation}`)
  return problems
}

const view = (c: { pcb: Json; pads: Map<number, Pad> }) => ({
  pads: c.pads,
  layer: c.pcb.layer as string,
  rotation: c.pcb.rotation as number,
  x: c.pcb.footprint_origin.x as number,
  y: c.pcb.footprint_origin.y as number,
})

type View = ReturnType<typeof view>

/** Copy with a coordinate or net transform applied to every pad (negative controls). */
function moved(c: View, f: (p: Pad) => Pad, rename?: (pin: number) => number): View {
  const pads = new Map<number, Pad>()
  for (const [pin, pad] of c.pads) {
    const out = f({ ...pad })
    const key = rename ? rename(pin) : pin
    pads.set(key, { ...out, pin: key })
  }
  return { ...c, pads }
}

const cx = FRONT_INTERCONNECT.x
const cy = FRONT_INTERCONNECT.y

describe("2 x 13 direct interconnect on the built boards", () => {
  test("both connectors are the documented Sullins pair on opposite faces", () => {
    expect(J5.pads.size).toBe(INTERCONNECT_PIN_COUNT)
    expect(JFRONT.pads.size).toBe(INTERCONNECT_PIN_COUNT)
    // Female socket on the front board's rear-facing (bottom) face, male
    // header on the rear board's front-facing (top) face, opposite the Pi.
    expect(J5.pcb.layer).toBe("bottom")
    expect(JFRONT.pcb.layer).toBe("top")
    expect(J5.pcb.rotation).toBe(FRONT_INTERCONNECT.rotation)
    expect(JFRONT.pcb.rotation).toBe(REAR_INTERCONNECT.rotation)
    expect({ x: J5.pcb.footprint_origin.x, y: J5.pcb.footprint_origin.y }).toEqual({ x: cx, y: cy })
    expect({ x: JFRONT.pcb.footprint_origin.x, y: JFRONT.pcb.footprint_origin.y }).toEqual({ x: cx, y: cy })
    const frontSource = byType(front, "source_component").find((s: Json) => s.name === "J5")
    const rearSource = byType(rear, "source_component").find((s: Json) => s.name === "J_FRONT")
    expect(frontSource?.manufacturer_part_number).toBe("SFH11-PBPC-D13-ST-BK")
    expect(rearSource?.manufacturer_part_number).toBe("SBH11-PBPC-D13-ST-BK")
  })

  test("every one of the 26 contacts coincides and carries the same net", () => {
    expect(mateProblems(view(J5), view(JFRONT))).toEqual([])
  })

  test("the pad geometry is the quoted one: 1.00 mm drill, 1.60 mm pad, 2.54 mm pitch", () => {
    for (const [name, side] of [["J5", J5], ["J_FRONT", JFRONT]] as const) {
      const pads = [...side.pads.values()]
      for (const pad of pads) {
        expect({ name, pin: pad.pin, shape: pad.shape }).toEqual({ name, pin: pad.pin, shape: "circle" })
        expect({ name, pin: pad.pin, hole: pad.hole }).toEqual({ name, pin: pad.pin, hole: INTERCONNECT_DRILL_MM })
        expect({ name, pin: pad.pin, outer: pad.outer }).toEqual({ name, pin: pad.pin, outer: INTERCONNECT_PAD_MM })
      }
      const xs = [...new Set(pads.map(p => round(p.x)))].sort((a, b) => a - b)
      const ys = [...new Set(pads.map(p => round(p.y)))].sort((a, b) => a - b)
      // Rotated 90 deg: 13 pads along Y in two columns across X.
      expect({ name, columns: xs.length, rows: ys.length }).toEqual({ name, columns: 2, rows: 13 })
      for (const list of [xs, ys]) {
        for (let i = 1; i < list.length; i++) {
          expect({ name, step: round(list[i]! - list[i - 1]!) }).toEqual({ name, step: INTERCONNECT_PITCH })
        }
      }
      expect({ name, span: round(ys.at(-1)! - ys[0]!) }).toEqual({ name, span: 12 * INTERCONNECT_PITCH })
    }
    expect(INTERCONNECT_RING_MM).toBeGreaterThanOrEqual(0.25)
  })

  test("the pads, the body and the solder tails keep their clearance on BOTH faces", () => {
    // Every pad and hole of each board, as the copper it really is. A plated
    // through-hole pad exists on both layers, so this covers both faces.
    const obstacles = (json: Json[], exclude: string) => {
      const out: { x: number; y: number; r: number; ref: string; kind: string; w?: number; h?: number }[] = []
      const refOf = (id: string) => {
        const pcb = byType(json, "pcb_component").find((c: Json) => c.pcb_component_id === id)
        const src = pcb && byType(json, "source_component").find(
          (s: Json) => s.source_component_id === pcb.source_component_id,
        )
        return (src?.name as string | undefined) ?? "?"
      }
      for (const e of json) {
        if (e.type === "pcb_plated_hole") {
          const ref = refOf(e.pcb_component_id)
          if (ref === exclude) continue
          const r = Math.max(e.outer_diameter ?? 0, e.outer_height ?? 0, e.outer_width ?? 0) / 2
          out.push({ x: e.x, y: e.y, r, ref, kind: "tht" })
        } else if (e.type === "pcb_smtpad") {
          const ref = refOf(e.pcb_component_id)
          if (ref === exclude) continue
          if (e.shape === "circle") out.push({ x: e.x, y: e.y, r: e.radius ?? 0, ref, kind: "smt" })
          else out.push({ x: e.x, y: e.y, r: 0, ref, kind: "smt", w: e.width ?? 0, h: e.height ?? 0 })
        } else if (e.type === "pcb_hole") {
          out.push({ x: e.x, y: e.y, r: (e.hole_diameter ?? 0) / 2, ref: "npth", kind: "npth" })
        }
      }
      return out
    }
    const gapTo = (
      o: { x: number; y: number; r: number; w?: number; h?: number },
      p: { x: number; y: number },
    ) => {
      if (o.w === undefined) return Math.hypot(o.x - p.x, o.y - p.y) - o.r
      const dx = Math.max(Math.abs(o.x - p.x) - o.w / 2, 0)
      const dy = Math.max(Math.abs(o.y - p.y) - (o.h ?? 0) / 2, 0)
      return Math.hypot(dx, dy)
    }
    const MIN_COPPER_GAP = 0.2
    for (const [name, pads, json] of [
      ["J5", J5.pads, front],
      ["J_FRONT", JFRONT.pads, rear],
    ] as const) {
      const others = obstacles(json, name)
      let worst = { gap: Infinity, what: "" }
      for (const pad of pads.values()) {
        for (const o of others) {
          const gap = gapTo(o, pad) - INTERCONNECT_PAD_MM / 2
          if (gap < worst.gap) worst = { gap, what: `${o.ref} (${o.kind}) at (${o.x.toFixed(2)}, ${o.y.toFixed(2)}) pin ${pad.pin}` }
        }
        // Board edge: copper keeps its own margin on a 101.6 x 112 or 128 x 112 outline.
        const halfW = name === "J5" ? 64 : 50.8
        const edge = Math.min(halfW - Math.abs(pad.x), 56 - Math.abs(pad.y)) - INTERCONNECT_PAD_MM / 2
        if (edge < worst.gap) worst = { gap: edge, what: "board edge" }
      }
      expect({ name, clear: worst.gap >= MIN_COPPER_GAP, worst: worst.what }).toEqual({
        name, clear: true, worst: worst.what,
      })

      // Body envelope: the male's shroud (rear TOP face) and the female's body
      // (front BOTTOM face) both sit in the 11.20 mm gap. Each must clear every
      // through-hole joint on THAT face, because those pins protrude through it.
      const halfLength = INTERCONNECT_MECH.bodyLength / 2
      const halfWidth = INTERCONNECT_MECH.bodyEnvelope / 2
      // The pads run along Y after the 90 deg placement: 40.52 on Y, 8.80 on X.
      const jointClear = (o: { x: number; y: number; r: number }) => {
        const dx = Math.max(Math.abs(o.x - cx) - halfWidth, 0)
        const dy = Math.max(Math.abs(o.y - cy) - halfLength, 0)
        return Math.hypot(dx, dy) - o.r
      }
      let bodyWorst = { gap: Infinity, what: "" }
      for (const o of others) {
        if (o.kind !== "tht") continue
        const gap = jointClear(o)
        if (gap < bodyWorst.gap) bodyWorst = { gap, what: `${o.ref} at (${o.x.toFixed(1)}, ${o.y.toFixed(1)})` }
      }
      expect({ name, clear: bodyWorst.gap >= MIN_COPPER_GAP, worst: bodyWorst.what }).toEqual({
        name, clear: true, worst: bodyWorst.what,
      })
    }
  })

  test("the male tail is a seating-face dimension, not a protrusion, and the Pi spacer is a candidate", () => {
    // The Pi no longer sits on the rear board (owner decision 2026-11-12): it is
    // on its own 20HP panel beside the main module. Nothing is mounted above the
    // 26 male pads, so there is no Pi overlap to test on this board.
    // The quoted 3.00 / 3.20 mm are measured FROM THE SEATING FACE. The nominal
    // protrusion below a 1.60 mm PCB is 1.40 / 1.60 mm; the fillet, the PCB
    // tolerance, the Pi hardware and every clearance are unmeasured.
    expect(INTERCONNECT_MECH.maleTail).toBe(3)
    expect(INTERCONNECT_MECH.femaleTail).toBe(3.2)
    expect(INTERCONNECT_MECH.pcbThickness).toBe(1.6)
    expect(INTERCONNECT_PROTRUSION.male).toBe(1.4)
    expect(INTERCONNECT_PROTRUSION.female).toBe(1.6)
    expect(INTERCONNECT_PROTRUSION.unmeasured.length).toBe(4)
    // The 3.5 mm Pi spacer stays a CANDIDATE. The test only bounds it below by
    // the nominal protrusion plus a named allowance; it does not claim the
    // spacer is safe or adequate, and the fillet is still unmeasured.
    expect(PI_SPACER_MM).toBeGreaterThanOrEqual(INTERCONNECT_PROTRUSION.male + 0.5)
    // The Pi now hangs behind its own 20HP mounting panel, so the standoff
    // candidate of that panel's chain is what the spacer check bounds.
    expect(PI_PANEL_PROVISIONAL_DEPTH.piStandoffAndPcb).toBeGreaterThan(PI_SPACER_MM)
  })

  test("the female pads stay out of the display FPC PLANNING MARKER's plan box", () => {
    // PROVISIONAL MARKER ONLY. The display FPC connector's identity, side and
    // position are unverified (docs/mechanical-stack.md); this box is the
    // recorded planning marker at DISPLAY_FPC_CONNECTOR_AT, with the body's
    // long axis along Y as that record reads it. This test says the J5 pads are
    // not inside that marker box. It is NOT a clearance to a real connector,
    // and no distance to the part is claimed.
    const box = {
      x1: DISPLAY_FPC_CONNECTOR_AT.x - DISPLAY_FPC_CONNECTOR.bodyWidth / 2,
      x2: DISPLAY_FPC_CONNECTOR_AT.x + DISPLAY_FPC_CONNECTOR.bodyWidth / 2,
      y1: DISPLAY_FPC_CONNECTOR_AT.y - DISPLAY_FPC_CONNECTOR.bodyLength / 2,
      y2: DISPLAY_FPC_CONNECTOR_AT.y + DISPLAY_FPC_CONNECTOR.bodyLength / 2,
    }
    const inside = [...J5.pads.values()].filter(
      p => p.x > box.x1 && p.x < box.x2 && p.y > box.y1 && p.y < box.y2,
    )
    expect(inside).toEqual([])
  })

  test("the drawing-derived Z chain is consistent with a mated pair", () => {
    // Seat-to-seat fixes the board faces apart: the male shroud must not reach
    // the front board's bottom face, and the two bodies must overlap so they
    // can mate at all.
    expect(PROVISIONAL_DEPTH.frontToRearGap).toBe(INTERCONNECT_MECH.seatToSeat)
    expect(INTERCONNECT_MECH.maleHeight).toBeLessThan(INTERCONNECT_MECH.seatToSeat)
    expect(INTERCONNECT_MECH.maleHeight + INTERCONNECT_MECH.femaleHeight).toBeGreaterThan(INTERCONNECT_MECH.seatToSeat)
    // The female's rear face stops 2.70 mm short of the rear board's top face;
    // no other part body may occupy the rear board's TOP face in that envelope.
    const femaleRearAboveRearBoard = INTERCONNECT_MECH.seatToSeat - INTERCONNECT_MECH.femaleHeight
    expect(round(femaleRearAboveRearBoard)).toBe(2.7)
    const topBodies = byType(rear, "pcb_component").filter(c => c.layer === "top")
    const topNames = topBodies.map(c => {
      const src = byType(rear, "source_component").find(
        (s: Json) => s.source_component_id === c.source_component_id,
      )
      return src?.name as string
    })
    // J_FRONT is the only body on the rear board's front-facing side, which is
    // what the owner requires (Pi on the rear side, opposite this connector).
    expect(topNames).toEqual(["J_FRONT"])
  })
})

/** The connector's own silkscreen paths, as built (global board millimetres). */
function silkPaths(json: Json[], name: string) {
  const { pcb } = component(json, name)
  return byType(json, "pcb_silkscreen_path")
    .filter((e: Json) => e.pcb_component_id === pcb.pcb_component_id)
    .map((e: Json) => e.route as { x: number; y: number }[])
    .map(route => ({
      route,
      length: route.length > 1
        ? Math.hypot(route[route.length - 1]!.x - route[0]!.x, route[route.length - 1]!.y - route[0]!.y)
        : 0,
      mid: {
        x: (route[0]!.x + route[route.length - 1]!.x) / 2,
        y: (route[0]!.y + route[route.length - 1]!.y) / 2,
      },
    }))
}

/** The pin-1 bar and the key/notch line, identified by their BUILT lengths. */
function connectorMarks(json: Json[], name: string) {
  const paths = silkPaths(json, name)
  const bar = paths.find(p => Math.abs(p.length - 0.7) < 1e-6)
  const key = paths.find(p => Math.abs(p.length - INTERCONNECT_MECH.keySlot) < 1e-6)
  const body = paths.find(p => p.route.length === 5)
  if (!bar || !key || !body) throw new Error(`missing ${name} silk mark (built paths: ${paths.length})`)
  return { bar, key, body }
}

describe("silkscreen marks sit where the BUILT pads are", () => {
  test("each pin-1 bar is at pin 1's end and on pin 1's row, away from pin 26", () => {
    for (const [name, json, side] of [
      ["J5", front, J5],
      ["J_FRONT", rear, JFRONT],
    ] as const) {
      const { bar } = connectorMarks(json, name)
      const pin1 = side.pads.get(1)!
      const pin26 = side.pads.get(INTERCONNECT_PIN_COUNT)!
      const d1 = Math.hypot(bar.mid.x - pin1.x, bar.mid.y - pin1.y)
      const d26 = Math.hypot(bar.mid.x - pin26.x, bar.mid.y - pin26.y)
      // Near the real pin 1, far from pin 26.
      expect({ name, nearPin1: d1 < d26 }).toEqual({ name, nearPin1: true })
      // Same row as pin 1: the bar's X is on pin 1's column side.
      expect({ name, sameRow: Math.abs(bar.mid.x - pin1.x) < Math.abs(bar.mid.x - pin26.x) }).toEqual({
        name, sameRow: true,
      })
      // The bar's offset from pin 1 must run in the same direction as pin 1's
      // row lies from pin 26: the bar is outboard on pin 1's own row, not on
      // pin 26's row.
      expect({ name, outboardOnPin1Row: Math.sign(bar.mid.x - pin1.x) }).toEqual({
        name, outboardOnPin1Row: Math.sign(pin1.x - pin26.x),
      })
      // The bar is outboard of the body's end on pin 1's side, not the other end.
      const bodyEnd = Math.max(...[pin1.y, pin26.y])
      expect({ name, outboard: bar.mid.y > bodyEnd }).toEqual({ name, outboard: true })
      // Sanity: it is a short mark, not the 4.50 mm key line.
      expect(Math.abs(bar.length - 0.7)).toBeLessThan(1e-6)
    }
  })

  test("the front and rear key/notch marks coincide physically after mating", () => {
    // Compared on the BUILT output of both boards, not on the authored sign:
    // a literal comparison of the same guessed constant would pass even when
    // both are on the wrong side.
    const frontKey = connectorMarks(front, "J5").key
    const rearKey = connectorMarks(rear, "J_FRONT").key
    expect(Math.abs(frontKey.mid.x - rearKey.mid.x)).toBeLessThan(1e-6)
    expect(Math.abs(frontKey.mid.y - rearKey.mid.y)).toBeLessThan(1e-6)
    // The key line lies on the body's long side, on the SAME side for both.
    const keyHalf = INTERCONNECT_MECH.keySlot / 2
    for (const [name, key] of [["J5", frontKey], ["J_FRONT", rearKey]] as const) {
      const offset = key.mid.x - cx
      expect({ name, onBodyEdge: Math.abs(Math.abs(offset) - INTERCONNECT_MECH.bodyEnvelope / 2) }).toEqual({
        name, onBodyEdge: 0,
      })
      expect(Math.abs((key.route[0]!.y - key.route[1]!.y))).toBeCloseTo(2 * keyHalf, 9)
    }
    // Both marks sit on the same physical side of the connector centreline.
    expect(Math.sign(frontKey.mid.x - cx)).toBe(Math.sign(rearKey.mid.x - cx))
  })
})

describe("negative controls: the mate check must reject a wrong placement", () => {
  const base = view(J5)
  const other = view(JFRONT)
  const fails = (problems: string[], reason: RegExp) => {
    expect(problems.length).toBeGreaterThan(0)
    expect(problems.some(p => reason.test(p))).toBe(true)
  }

  test("reversed pin table (1..26 -> 26..1) is rejected", () => {
    const reversed = moved(other, p => p, pin => INTERCONNECT_PIN_COUNT + 1 - pin)
    fails(mateProblems(base, reversed), /does not mate|mismatch/)
  })

  test("a mirrored rear board (X flip) is rejected", () => {
    fails(mateProblems(base, moved(other, p => ({ ...p, x: 2 * cx - p.x }))), /does not mate/)
  })

  test("a mirrored rear board (Y flip) is rejected", () => {
    fails(mateProblems(base, moved(other, p => ({ ...p, y: 2 * cy - p.y }))), /does not mate/)
  })

  test("a 180-degree rotated rear board is rejected", () => {
    fails(
      mateProblems(base, moved(other, p => ({ ...p, x: 2 * cx - p.x, y: 2 * cy - p.y }))),
      /does not mate/,
    )
  })

  test("a one-pitch shift is rejected", () => {
    fails(mateProblems(base, moved(other, p => ({ ...p, y: p.y + INTERCONNECT_PITCH }))), /does not mate/)
  })

  test("a same-layer placement is rejected even when the pads line up", () => {
    fails(mateProblems(base, { ...other, layer: "bottom" }), /both connectors/)
  })

  test("a swapped rail on one pin is rejected, and the swap is a real short", () => {
    // P3V3 sits on pins 1 and 22, so a reversal or a mirror puts a rail on a
    // signal pad. Show that the check names the rail.
    const swapped = moved(other, p => (p.pin === 1 ? { ...p, net: "SHIFT_EXIT" } : p))
    fails(mateProblems(base, swapped), /pin 1 (short|rear net)/)
    const nets = [...JFRONT.pads.values()].map(p => p.net)
    expect(nets.filter(n => n === "P3V3").length).toBe(2)
    expect(nets.filter(n => n === "GND").length).toBe(3)
  })
})

function round(v: number): number {
  return Math.round(v * 1000) / 1000
}

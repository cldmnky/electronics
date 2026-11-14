// U5, the NAV switch: ALPS Alpine RKJXT1F42001 (owner decision 2026-11-14).
//
// Three things are checked here, all on the BUILT front board and on the two
// copies of the footprint, because a wrong terminal map on a 4-direction stick
// switch is invisible in the netlist and only shows up as a stick that navigates
// the wrong way.
//
//  1. Every terminal lands on the hole the ALPS mounting-hole drawing prints,
//     and the part is not mirrored: a footprint drawn the wrong way round fits
//     no board at all, and the hole pattern is chiral.
//  2. Each terminal carries the net the firmware expects, mapped BY FUNCTION
//     (direction A is the lever's "up", C is "down", B is "left", D is
//     "right"), never by pad order.
//  3. `library/cad/rkjxt1f42001/footprint.tsx` (the shared library's copy) and
//     `imports/RKJXT1F42001.tsx` (the copy the board build uses) still hold the
//     same coordinates, so the two cannot drift apart silently.
import { expect, test } from "bun:test"
import { readFileSync } from "node:fs"
import { RIGHT_CONTROLS } from "./layout"

const FRONT = new URL("../dist/front/circuit.json", import.meta.url)
const HERE = new URL(".", import.meta.url)
const PROJECT_FOOTPRINT = new URL("../imports/RKJXT1F42001.tsx", import.meta.url)
const LIBRARY_FOOTPRINT = new URL("../../../library/cad/rkjxt1f42001/footprint.tsx", HERE)

// The ALPS page-398 "PC board mounting hole dimensions" table, millimetres from
// the shaft axis. +X right, +Y up. Source: library/cad/rkjxt1f42001/SOURCE.md.
const TERMINALS: { pin: number; name: string; x: number; y: number; net: string }[] = [
  { pin: 1, name: "A", x: -1.5, y: -7.8, net: "NAV_UP" },
  { pin: 2, name: "B", x: 7.8, y: -1.5, net: "NAV_LEFT" },
  { pin: 3, name: "C", x: 1.5, y: 7.8, net: "NAV_DOWN" },
  { pin: 4, name: "D", x: -7.8, y: 1.5, net: "NAV_RIGHT" },
  { pin: 5, name: "com", x: -1.0, y: 5.78, net: "GND" },
  { pin: 6, name: "Push", x: 1.0, y: -6.98, net: "NAV_PRESS" },
  { pin: 7, name: "Ground", x: 6.86, y: -3.75, net: "GND" },
  { pin: 8, name: "EncoderA", x: -7.8, y: -1.5, net: "" },
  { pin: 9, name: "EncoderB", x: 7.8, y: 1.5, net: "" },
  { pin: 10, name: "EncoderCom", x: -1.5, y: 7.8, net: "" },
]
/** The position lug: a mechanical Ø1.1 hole with no net. */
const LUG = { x: -3.8, y: -1.5, diameter: 1.1 }

type El = Record<string, any>

const built = (): El[] => {
  try {
    return JSON.parse(readFileSync(FRONT, "utf8")) as El[]
  } catch (err) {
    throw new Error(
      `${FRONT.pathname} is missing: run \`bun run build\` before this test ` +
        `(make test does this first). ${String(err)}`,
    )
  }
}

const u5 = (els: El[]) => {
  const source = els.find((e) => e.type === "source_component" && e.name === "U5")
  expect(source).toBeDefined()
  const pcb = els.find(
    (e) => e.type === "pcb_component" && e.source_component_id === source!.source_component_id,
  )
  return { source: source!, pcb: pcb! }
}

const close = (a: number, b: number, tol = 1e-6) => Math.abs(a - b) <= tol

test("U5 is placed at the NAV control position, unrotated, on the top side", () => {
  const { pcb } = u5(built())
  expect({ x: pcb.footprint_origin.x, y: pcb.footprint_origin.y, r: pcb.rotation, layer: pcb.layer })
    .toEqual({ x: RIGHT_CONTROLS.NAV.x, y: RIGHT_CONTROLS.NAV.y, r: 0, layer: "top" })
})

test("every U5 terminal is on its printed hole and carries its functional net", () => {
  const els = built()
  const { source, pcb } = u5(els)
  const nets = new Map(
    els.filter((e) => e.type === "source_net").map((e) => [e.source_net_id, e.name]),
  )
  const ports = els.filter(
    (e) => e.type === "source_port" && e.source_component_id === source.source_component_id,
  )
  const traceNet = (portId: string) => {
    const trace = els.find(
      (e) =>
        e.type === "source_trace" &&
        (e.connected_source_port_ids ?? []).includes(portId),
    )
    const netId = (trace?.connected_source_net_ids ?? [])[0]
    return netId ? nets.get(netId) ?? "" : ""
  }
  const holeAt = (x: number, y: number) =>
    els.find(
      (e) =>
        e.type === "pcb_plated_hole" &&
        e.pcb_component_id === pcb.pcb_component_id &&
        close(e.x, x, 1e-6) &&
        close(e.y, y, 1e-6),
    )

  const holes = els.filter(
    (e) => e.type === "pcb_plated_hole" && e.pcb_component_id === pcb.pcb_component_id,
  )
  // Ten electrical holes: 1.0 mm drill, 1.5 mm pad (0.25 mm ring).
  expect(holes.length).toBe(TERMINALS.length)
  for (const h of holes) {
    expect({ hole: h.hole_diameter, pad: h.outer_diameter }).toEqual({ hole: 1, pad: 1.5 })
  }

  for (const t of TERMINALS) {
    const x = RIGHT_CONTROLS.NAV.x + t.x
    const y = RIGHT_CONTROLS.NAV.y + t.y
    const hole = holeAt(x, y)
    // 1. The hole is where the drawing prints it (this is the mirror test: a
    //    mirrored pattern puts no hole at these coordinates at all).
    expect({ terminal: t.name, atPrintedHole: !!hole }).toEqual({
      terminal: t.name,
      atPrintedHole: true,
    })
    expect({ terminal: t.name, hints: hole!.port_hints }).toEqual({
      terminal: t.name,
      hints: hole!.port_hints,
    })
    expect(hole!.port_hints).toContain(`pin${t.pin}`)
    // 2. The net, mapped by function.
    const port = ports.find((p) => p.pin_number === t.pin)
    expect({ terminal: t.name, pin: port?.pin_number }).toEqual({
      terminal: t.name,
      pin: t.pin,
    })
    expect({ terminal: t.name, port: port?.name }).toEqual({ terminal: t.name, port: t.name })
    expect({ terminal: t.name, net: traceNet(port!.source_port_id) }).toEqual({
      terminal: t.name,
      net: t.net,
    })
  }
})

test("the position lug is a non-plated Ø1.1 hole with no net", () => {
  const els = built()
  const { pcb } = u5(els)
  const x = RIGHT_CONTROLS.NAV.x + LUG.x
  const y = RIGHT_CONTROLS.NAV.y + LUG.y
  const hole = els.find(
    (e) =>
      e.type === "pcb_hole" &&
      e.pcb_component_id === pcb.pcb_component_id &&
      close(e.x, x) &&
      close(e.y, y),
  )
  expect({ lug: !!hole, diameter: hole?.hole_diameter }).toEqual({
    lug: true,
    diameter: LUG.diameter,
  })
  // It must not be a plated hole: a lug is not a terminal.
  const plated = els.find(
    (e) => e.type === "pcb_plated_hole" && close(e.x, x) && close(e.y, y),
  )
  expect(plated).toBeUndefined()
})

test("the ten terminals are the whole part: nothing else shares the NAV footprint", () => {
  const els = built()
  const { pcb } = u5(els)
  const xs = new Set(
    els
      .filter(
        (e) =>
          (e.type === "pcb_plated_hole" || e.type === "pcb_hole") &&
          e.pcb_component_id === pcb.pcb_component_id,
      )
      .map((e) => `${e.x.toFixed(3)},${e.y.toFixed(3)}`),
  )
  const want = new Set([
    ...TERMINALS.map(
      (t) =>
        `${(RIGHT_CONTROLS.NAV.x + t.x).toFixed(3)},${(RIGHT_CONTROLS.NAV.y + t.y).toFixed(3)}`,
    ),
    `${(RIGHT_CONTROLS.NAV.x + LUG.x).toFixed(3)},${(RIGHT_CONTROLS.NAV.y + LUG.y).toFixed(3)}`,
  ])
  expect([...xs].sort()).toEqual([...want].sort())
})

/** Every `pcbX`/`pcbY` pair in a footprint file, sorted, as "x,y" strings. */
const coordinates = (path: URL): string[] => {
  const text = readFileSync(path, "utf8")
  const out: string[] = []
  const re = /pcbX=\{?"?(-?[\d.]+)mm"?\}?[^>]*?pcbY=\{?"?(-?[\d.]+)mm"?/g
  for (const m of text.matchAll(re)) out.push(`${Number(m[1])},${Number(m[2])}`)
  const re2 = /pcbY=\{?"?(-?[\d.]+)mm"?\}?[^>]*?pcbX=\{?"?(-?[\d.]+)mm"?/g
  for (const m of text.matchAll(re2)) out.push(`${Number(m[2])},${Number(m[1])}`)
  return out.sort()
}

test("the board's footprint copy matches the shared library footprint", () => {
  const project = coordinates(PROJECT_FOOTPRINT)
  const library = coordinates(LIBRARY_FOOTPRINT)
  expect(project.length).toBeGreaterThan(0)
  expect(project).toEqual(library)
})

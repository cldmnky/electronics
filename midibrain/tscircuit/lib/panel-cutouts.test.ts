// The canonical panel apertures against the BUILT panel.
//
// `lib/layout.ts` exports `PANEL_APERTURES`, `PANEL_DRILLS` and
// `PANEL_MOUNT_HOLES`; `panel.circuit.tsx` renders them and
// `mechanicalExport()` forwards them to the STEP/STL mockups. This test is the
// guard that the three cannot drift apart: it compares the exported data with
// the circuit JSON tscircuit actually built, element by element, in order.
//
// The built file is `dist/panel/circuit.json`, gitignored and produced by
// `bun run build`. `make test` builds before it tests. A missing file FAILS
// here on purpose: a silent skip would hide a panel that no longer matches the
// CAD model (the stale-binary trap).
import { expect, test } from "bun:test"
import { existsSync, readFileSync } from "node:fs"
import { PANEL_APERTURES, PANEL_DRILLS, PANEL_MOUNT_HOLES } from "./layout"

const PANEL_JSON = new URL("../dist/panel/circuit.json", import.meta.url)

type Cutout = {
  type: "pcb_cutout"
  shape: "polygon" | "rect" | "circle"
  points?: { x: number; y: number }[]
  center?: { x: number; y: number }
  width?: number
  height?: number
  radius?: number
}
type Hole = { type: "pcb_hole"; x: number; y: number; hole_diameter: number }
type Element = Cutout | Hole | { type: string }

const readPanel = (): Element[] => {
  if (!existsSync(PANEL_JSON)) {
    throw new Error(
      `${PANEL_JSON.pathname} is missing: run \`bun run build\` before this test ` +
        "(make test does this first)",
    )
  }
  return JSON.parse(readFileSync(PANEL_JSON, "utf8")) as Element[]
}

/** Rounding tolerance for the geometry comparison, in mm. */
const TOL = 1e-6
const near = (a: number, b: number) => Math.abs(a - b) <= TOL

/** One ordered signature per element, so order and geometry both matter. */
const cutoutSignature = (c: Cutout): string => {
  if (c.shape === "polygon") {
    return `polygon ${(c.points ?? []).map(p => `${p.x.toFixed(6)},${p.y.toFixed(6)}`).join(" ")}`
  }
  if (c.shape === "rect") {
    return `rect ${c.center?.x.toFixed(6)},${c.center?.y.toFixed(6)} ${c.width?.toFixed(6)}x${c.height?.toFixed(6)}`
  }
  return `circle ${c.center?.x.toFixed(6)},${c.center?.y.toFixed(6)} r${c.radius?.toFixed(6)}`
}

/** The same signature for a canonical drill and a built hole, so the two lists
 * can be compared directly. */
const drillSignature = (d: { x: number; y: number; diameter: number }) =>
  `hole ${d.x.toFixed(6)},${d.y.toFixed(6)} d${d.diameter.toFixed(6)}`

test("the built panel exists and carries every canonical aperture and drill", () => {
  const elements = readPanel()
  const cutouts = elements.filter((e): e is Cutout => e.type === "pcb_cutout")
  const holes = elements.filter((e): e is Hole => e.type === "pcb_hole")
  expect(cutouts.length).toBe(PANEL_APERTURES.length)
  expect(holes.length).toBe(PANEL_DRILLS.length)
  // 20 openings (LCD, 3 feedthroughs, 6 buttons, 3 LEDs, encoder, POWER, NAV,
  // 4 jacks) and 14 holes (6 feedthrough screws, 4 standoffs, 4 rails).
  expect(cutouts.length).toBe(20)
  expect(holes.length).toBe(14)
})

test("every canonical aperture is built, in order and at its exact geometry", () => {
  const built = readPanel()
    .filter((e): e is Cutout => e.type === "pcb_cutout")
    .map(cutoutSignature)
  const expected = PANEL_APERTURES.map(a => {
    if (a.shape === "polygon") {
      return cutoutSignature({ type: "pcb_cutout", shape: "polygon", points: [...(a.points ?? [])] })
    }
    if (a.shape === "rect") {
      return cutoutSignature({
        type: "pcb_cutout", shape: "rect",
        center: { x: a.x ?? NaN, y: a.y ?? NaN }, width: a.width, height: a.height,
      })
    }
    return cutoutSignature({
      type: "pcb_cutout", shape: "circle",
      center: { x: a.x ?? NaN, y: a.y ?? NaN }, radius: a.radius,
    })
  })
  expect(built).toEqual(expected)
})

test("every canonical panel hole is built, in order and at its exact diameter", () => {
  const built = readPanel()
    .filter((e): e is Hole => e.type === "pcb_hole")
    .map(h => drillSignature({ x: h.x, y: h.y, diameter: h.hole_diameter }))
  expect(built).toEqual(PANEL_DRILLS.map(drillSignature))
})

test("PANEL_DRILLS is the feedthrough screws, then the standoffs, then the rails", () => {
  expect(PANEL_DRILLS.length).toBe(14)
  expect(PANEL_MOUNT_HOLES.length).toBe(8)
  expect(PANEL_DRILLS.slice(0, 6).map(d => d.name)).toEqual([
    "USB_0_-1", "USB_0_1", "USB_1_-1", "USB_1_1", "USB_2_-1", "USB_2_1",
  ])
  expect(PANEL_DRILLS.slice(6).map(d => d.name)).toEqual([
    "STAND_1", "STAND_2", "STAND_3", "STAND_4",
    "RACK_1", "RACK_2", "RACK_3", "RACK_4",
  ])
  // The diameters are the canonical ones; nothing is invented here.
  expect(new Set(PANEL_DRILLS.map(d => d.diameter))).toEqual(new Set([3.2, 3.5, 2.7]))
})

test("the canonical apertures cover every opening class with its own diameter", () => {
  const byName = new Map(PANEL_APERTURES.map(a => [a.name, a]))
  const radius = (name: string) => {
    const a = byName.get(name)
    if (!a || a.shape !== "circle") throw new Error(`${name} is not a circle aperture`)
    return a.radius ?? NaN
  }
  expect(radius("REC BUTTON")).toBe(6.2)   // 12.4 mm B3F
  expect(radius("REC LED")).toBe(2.0)      // 4.0 mm light pipe
  expect(radius("VALUE ENCODER")).toBe(3.6) // 7.2 mm bushing
  expect(radius("MIDI IN 1 JACK")).toBe(3.1) // 6.2 mm barrel
  const lcd = byName.get("LCD_APERTURE")
  expect(lcd?.shape).toBe("polygon")
  expect(lcd?.points?.length).toBe(28)
  const positions = PANEL_APERTURES.filter(a => a.shape === "circle").map(a => `${a.x},${a.y}`)
  // Control centres are the owner's and never move (docs/mechanical-stack.md).
  for (const p of ["52,30", "52,11", "52,-8", "52,-34", "52.5,-48", "-53,-34", "-29,-34", "-5,-34", "19,-34"]) {
    expect(positions).toContain(p)
  }
})

test("no aperture or drill was silently re-derived: values stay inside the panel", () => {
  const elements = readPanel()
  const board = elements.find(e => e.type === "pcb_board") as { width: number; height: number } | undefined
  expect(board).toBeDefined()
  const halfW = (board?.width ?? 0) / 2
  const halfH = (board?.height ?? 0) / 2
  for (const a of PANEL_APERTURES) {
    if (a.shape === "polygon") {
      for (const p of a.points ?? []) {
        expect(Math.abs(p.x)).toBeLessThan(halfW)
        expect(Math.abs(p.y)).toBeLessThan(halfH)
      }
      continue
    }
    expect(Math.abs(a.x ?? NaN)).toBeLessThan(halfW)
    expect(Math.abs(a.y ?? NaN)).toBeLessThan(halfH)
    if (a.shape === "circle") expect(a.radius ?? 0).toBeGreaterThan(0)
    if (a.shape === "rect") {
      expect(a.width ?? 0).toBeGreaterThan(0)
      expect(a.height ?? 0).toBeGreaterThan(0)
    }
  }
  for (const d of PANEL_DRILLS) {
    expect(Math.abs(d.x)).toBeLessThan(halfW)
    expect(Math.abs(d.y)).toBeLessThan(halfH)
    expect(d.diameter).toBeGreaterThan(0)
    expect(near(d.x, d.x)).toBe(true)
  }
})

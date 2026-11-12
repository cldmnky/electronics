// The 20HP Pi mounting panel: canonical geometry against the BUILT board.
//
// `lib/layout.ts` exports `PI_PANEL`, `PI_PANEL_PI_HOLES`,
// `PI_PANEL_RAIL_HOLES` and the Pi envelope; `pi-panel.circuit.tsx` renders
// them and `mechanicalExport()` forwards them to mechanical/layout.json. This
// test is the guard that those cannot drift apart: source-level rules (the
// 20HP width convention, the Doepfer rail-hole rule, the 58 x 49 Pi grid) plus
// a hole-by-hole comparison with the circuit JSON tscircuit actually built.
//
// The built file is `dist/pi-panel/circuit.json`, gitignored and produced by
// `bun run build`. `make test` builds before it tests. A missing file FAILS
// here on purpose (the stale-binary trap).
//
// Nothing here claims a mechanical fit: the plugs pass the panel edge in the
// panel plane by design, the standoff length is a candidate, and the harness
// lengths between the two rack positions are planning values. See
// docs/mechanical-stack.md.
import { expect, test } from "bun:test"
import { existsSync, readFileSync } from "node:fs"
import {
  EURORACK_GO_MAX_MODULE_DEPTH,
  PI_PANEL,
  PI_PANEL_LABELS,
  PI_PANEL_PI_BOX,
  PI_PANEL_PI_HOLES,
  PI_PANEL_PI_MOUNT,
  PI_PANEL_PROVISIONAL_DEPTH,
  PI_PANEL_RAIL_HOLES,
  PI_PANEL_TITLES,
  RAIL_HOLE_DIAMETER,
  RAIL_HOLES,
  panelTitleExtents,
  provisionalModuleDepthBehindPanel,
  provisionalPiPanelDepth,
} from "./layout"

const PI_PANEL_JSON = new URL("../dist/pi-panel/circuit.json", import.meta.url)
const LABEL_FONT_SIZE = 1.6 // pi-panel.circuit.tsx renders labels at this size

type Hole = { type: "pcb_hole"; x: number; y: number; hole_diameter: number }
type Element = Hole | { type: string; [key: string]: unknown }

const readBoard = (): Element[] => {
  if (!existsSync(PI_PANEL_JSON)) {
    throw new Error(
      `${PI_PANEL_JSON.pathname} is missing: run \`bun run build\` before this test ` +
        "(make test does this first)",
    )
  }
  return JSON.parse(readFileSync(PI_PANEL_JSON, "utf8")) as Element[]
}

test("the 20HP panel follows the panel width convention and the Doepfer rail rule", () => {
  expect(PI_PANEL.hp).toBe(20)
  // The PANEL convention (30HP): actual = N x 5.08 - 0.4.
  expect(PI_PANEL.width).toBeCloseTo(PI_PANEL.hp * 5.08 - 0.4, 6)
  expect(PI_PANEL.height).toBe(128.5)
  expect(PI_PANEL.thickness).toBe(1.6)
  // The RAIL_HOLES Doepfer rule: X spacing a multiple of 5.08 at the same
  // 7.42 mm centre inset. 86.36 = 17 x 5.08.
  const left = PI_PANEL_RAIL_HOLES.find(h => h.x < 0 && h.y < 0)!
  const right = PI_PANEL_RAIL_HOLES.find(h => h.x > 0 && h.y < 0)!
  const spacing = right.x - left.x
  expect(spacing).toBeCloseTo(17 * 5.08, 6)
  expect((PI_PANEL.width - spacing) / 2).toBeCloseTo(7.42, 6)
  // Y is the Doepfer 3U position, shared with the 30HP panel's rail holes.
  for (const h of PI_PANEL_RAIL_HOLES) {
    expect(Math.abs(h.y)).toBeCloseTo(61.25, 6)
    expect(RAIL_HOLES.some(r => r.y === h.y)).toBe(true)
  }
  expect(RAIL_HOLE_DIAMETER).toBe(3.2)
})

test("the Pi holes are the 58 x 49 grid, landscape and centred", () => {
  // Landscape: the Pi's 85 mm edge runs across the panel width, so the LONG
  // hole pitch is X. Rotating this swaps the cables toward the rails: do not.
  expect(PI_PANEL_PI_MOUNT.pitchX).toBe(58)
  expect(PI_PANEL_PI_MOUNT.pitchY).toBe(49)
  expect(PI_PANEL_PI_MOUNT.x).toBe(0)
  expect(PI_PANEL_PI_MOUNT.y).toBe(0)
  expect(PI_PANEL_PI_MOUNT.holeDiameter).toBe(2.7) // M2.5 clearance
  const xs = [...new Set(PI_PANEL_PI_HOLES.map(h => h.x))].sort((a, b) => a - b)
  const ys = [...new Set(PI_PANEL_PI_HOLES.map(h => h.y))].sort((a, b) => a - b)
  expect(xs).toEqual([-29, 29])
  expect(ys).toEqual([-24.5, 24.5])
  expect(PI_PANEL_PI_HOLES.length).toBe(4)
  // The drawn envelope is the conservative 85.6 x 56.5 bound (same source as
  // PI_PCB), not the drawing's 85 x 56.
  expect(PI_PANEL_PI_BOX.x2 - PI_PANEL_PI_BOX.x1).toBeCloseTo(85.6, 6)
  expect(PI_PANEL_PI_BOX.y2 - PI_PANEL_PI_BOX.y1).toBeCloseTo(56.5, 6)
  expect(PI_PANEL_PI_BOX.x1).toBeCloseTo(-42.8, 6)
  expect(PI_PANEL_PI_BOX.y2).toBeCloseTo(28.25, 6)
})

test("holes and the Pi envelope keep FR-4 ligaments", () => {
  // The Pi envelope keeps a margin per side; plugs insert in the PANEL PLANE
  // and may pass the panel edge by design (USB-C left, USB/RJ45 right). The
  // 7.8 mm per side is coverage, not a plug clearance claim.
  expect(PI_PANEL.width / 2 - PI_PANEL_PI_BOX.x2).toBeCloseTo(7.8, 6)
  for (const h of [...PI_PANEL_PI_HOLES, ...PI_PANEL_RAIL_HOLES]) {
    expect(Math.abs(h.x)).toBeLessThan(PI_PANEL.width / 2 - 1.6)
    expect(Math.abs(h.y)).toBeLessThan(PI_PANEL.height / 2 - 1.6)
  }
  // Pi holes sit well clear of the rail holes (different Y bands).
  for (const p of PI_PANEL_PI_HOLES) {
    for (const r of PI_PANEL_RAIL_HOLES) {
      const d = Math.hypot(p.x - r.x, p.y - r.y)
      expect(d).toBeGreaterThan(30)
    }
  }
})

test("the silkscreen labels stay inside the board at the layout level", () => {
  // The release Gerber-bbox gate is authoritative; this is the layout-level
  // bound using the same SILK_ADVANCE_RATIO the panel tests use.
  const limitX = PI_PANEL.width / 2 - 0.5
  const limitY = PI_PANEL.height / 2 - 0.5
  const items = [
    ...PI_PANEL_TITLES,
    ...PI_PANEL_LABELS.map(l => ({ ...l, fontSize: LABEL_FONT_SIZE })),
  ]
  for (const t of items) {
    const e = panelTitleExtents(t)
    expect(Math.abs(e.x1)).toBeLessThan(limitX)
    expect(Math.abs(e.x2)).toBeLessThan(limitX)
    expect(Math.abs(e.y1)).toBeLessThan(limitY)
    expect(Math.abs(e.y2)).toBeLessThan(limitY)
  }
  // The harness/orientation hints name the four Pi edges and pin 1.
  const texts = items.map(t => t.text)
  expect(texts).toContain("GPIO RIBBON to J_PI")
  expect(texts).toContain("DSI FPC to display")
  expect(texts).toContain("USB-C POWER / HDMI")
  expect(texts).toContain("USB + RJ45")
  expect(texts).toContain("1")
})

test("both depth chains stay inside the Go's upper row, not the lower row", () => {
  // Design check on the candidate chains, not a fit approval. With the NVMe
  // HAT candidate stack the Pi panel is over the 40 mm lower row, so both
  // modules claim the 62 mm upper row (docs/mechanical-stack.md).
  expect(PI_PANEL_PROVISIONAL_DEPTH.piStackRearward).toBe(30) // NVMe HAT retained
  expect(provisionalPiPanelDepth()).toBeCloseTo(5.1 + 30 + 10, 6)
  expect(provisionalPiPanelDepth()).toBeLessThanOrEqual(EURORACK_GO_MAX_MODULE_DEPTH.upperRow)
  expect(provisionalPiPanelDepth()).toBeGreaterThan(EURORACK_GO_MAX_MODULE_DEPTH.lowerRow)
  expect(provisionalModuleDepthBehindPanel()).toBeLessThanOrEqual(EURORACK_GO_MAX_MODULE_DEPTH.upperRow)
})

test("the built pi-panel matches the canonical holes in order and carries no parts", () => {
  const elements = readBoard()
  const board = elements.find(e => e.type === "pcb_board") as { width: number; height: number } | undefined
  expect(board).toBeDefined()
  expect(board!.width).toBeCloseTo(PI_PANEL.width, 3)
  expect(board!.height).toBeCloseTo(PI_PANEL.height, 3)

  const holes = elements.filter(e => e.type === "pcb_hole") as Hole[]
  expect(holes.length).toBe(8)
  const expected = [
    ...PI_PANEL_PI_HOLES.map(h => ({ x: h.x, y: h.y, d: PI_PANEL_PI_MOUNT.holeDiameter })),
    ...PI_PANEL_RAIL_HOLES.map(h => ({ x: h.x, y: h.y, d: RAIL_HOLE_DIAMETER })),
  ]
  expected.forEach((e, i) => {
    expect(holes[i].x).toBeCloseTo(e.x, 3)
    expect(holes[i].y).toBeCloseTo(e.y, 3)
    expect(holes[i].hole_diameter).toBeCloseTo(e.d, 3)
  })

  // Bare board: the fab pipeline fails if a component ever lands here, and
  // this is the test-level tripwire for the same rule.
  const parts = elements.filter(e =>
    ["pcb_smtpad", "pcb_plated_hole", "source_component", "pcb_component"].includes(e.type))
  expect(parts.length).toBe(0)

  const texts = elements.filter(e => e.type === "pcb_silkscreen_text")
  expect(texts.length).toBe(PI_PANEL_TITLES.length + PI_PANEL_LABELS.length)
})

// The 20HP Pi mounting panel: canonical geometry against the Raspberry Pi 5
// drawing and against the BUILT board.
//
// `lib/layout.ts` derives the Pi holes and outlines from the drawing's board
// constants (PI_BOARD, PI_HOLE_*, PI_PROTRUSION) through piBoardToPanel. The
// tests below check (a) those constants against the drawing values, (b) the
// mirror mapping and the panel geometry, and (c) the built board hole by hole.
//
// The built file is `dist/pi-panel/circuit.json`, produced by `bun run build`.
// A missing file FAILS on purpose (the stale-binary trap).
//
// Nothing here claims a mechanical fit: the standoff length is a candidate, the
// plugs' clearance in the rack is unmeasured, and the harness is planning only.
import { expect, test } from "bun:test"
import { existsSync, readFileSync } from "node:fs"
import {
  EURORACK_GO_MAX_MODULE_DEPTH,
  PI_BOARD,
  PI_HEADER_CENTRE_X,
  PI_HOLE_DIAMETER,
  PI_HOLE_INSET,
  PI_HOLE_PITCH,
  PI_PANEL,
  PI_PANEL_LABELS,
  PI_PANEL_PI_BOX,
  PI_PANEL_PI_ENVELOPE,
  PI_PANEL_PI_HOLES,
  PI_PANEL_PI_MOUNT,
  PI_PANEL_PROVISIONAL_DEPTH,
  PI_PANEL_RAIL_HOLES,
  PI_PANEL_TITLES,
  PI_PROTRUSION,
  RAIL_HOLE_DIAMETER,
  RAIL_HOLES,
  panelTitleExtents,
  piBoardToPanel,
  provisionalModuleDepthBehindPanel,
  provisionalPiPanelDepth,
} from "./layout"

const PI_PANEL_JSON = new URL("../dist/pi-panel/circuit.json", import.meta.url)
const LABEL_FONT_SIZE = 1.6 // pi-panel.circuit.tsx renders labels at this size
const PANEL_HALF_WIDTH = PI_PANEL.width / 2

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

test("the Pi board constants are the drawing's (RP-008347-DS-1)", () => {
  // Board edges: the dimension extension lines at 0 and 85 / 0 and 56.
  expect(PI_BOARD.length).toBe(85)
  expect(PI_BOARD.width).toBe(56)
  // Holes: Ø2.7 at 3.5 mm from the left and bottom edges, 58 x 49 pitch.
  expect(PI_HOLE_DIAMETER).toBe(2.7)
  expect(PI_HOLE_INSET).toBe(3.5)
  expect(PI_HOLE_PITCH).toEqual({ x: 58, y: 49 })
  // The pattern is NOT centred on the 85 mm edge: 3.5 mm on one end and 23.5 mm
  // on the other. Centring it was the defect in the first Pi panel. In Y the
  // pattern is symmetric, 3.5 mm at both edges.
  expect(PI_BOARD.length - (PI_HOLE_INSET + PI_HOLE_PITCH.x)).toBe(23.5)
  expect(PI_HOLE_INSET + PI_HOLE_PITCH.y + PI_HOLE_INSET).toBe(PI_BOARD.width)
  // The GPIO header is centred on the hole pattern.
  expect(PI_HEADER_CENTRE_X).toBe(32.5)
  // Connectors beyond the outline: USB-A/RJ45 3.3 mm, USB-C/HDMI 1.7 mm.
  expect(PI_PROTRUSION.right).toBe(3.3)
  expect(PI_PROTRUSION.bottom).toBe(1.7)
})

test("the Pi mirrors into the panel: component side away from the panel", () => {
  // The USB-A/RJ45 edge (board x = 85) is the panel's left edge, and the board
  // is centred on the panel origin.
  expect(piBoardToPanel(PI_BOARD.length, 0).x).toBeCloseTo(-42.5, 9)
  expect(piBoardToPanel(0, 0).x).toBeCloseTo(42.5, 9)
  // The GPIO header (board y = 56) is the panel's top edge.
  expect(piBoardToPanel(0, PI_BOARD.width).y).toBeCloseTo(28, 9)
  expect(piBoardToPanel(0, 0).y).toBeCloseTo(-28, 9)
  // The pattern centre (the header centre) is 10 mm toward +X.
  expect(PI_PANEL_PI_MOUNT.x).toBeCloseTo(10, 9)
  expect(PI_PANEL_PI_MOUNT.y).toBe(0)
})

test("the Pi holes are the drawing's pattern in the panel frame", () => {
  expect(PI_PANEL_PI_MOUNT.pitchX).toBe(58)
  expect(PI_PANEL_PI_MOUNT.pitchY).toBe(49)
  expect(PI_PANEL_PI_MOUNT.holeDiameter).toBe(2.7)
  expect(PI_PANEL_PI_HOLES.length).toBe(4)
  const xs = [...new Set(PI_PANEL_PI_HOLES.map(h => Math.round(h.x * 1000) / 1000))].sort((a, b) => a - b)
  const ys = [...new Set(PI_PANEL_PI_HOLES.map(h => Math.round(h.y * 1000) / 1000))].sort((a, b) => a - b)
  // x: 3.5 and 61.5 from the board's left edge, mirrored -> +39 and -19.
  expect(xs).toEqual([-19, 39])
  // y: 3.5 and 52.5 from the board's bottom edge -> -24.5 and +24.5.
  expect(ys).toEqual([-24.5, 24.5])
  // The four holes are the pattern centre +/- half the pitch.
  for (const h of PI_PANEL_PI_HOLES) {
    expect(Math.abs(h.x - PI_PANEL_PI_MOUNT.x)).toBeCloseTo(29, 9)
    expect(Math.abs(h.y)).toBeCloseTo(24.5, 9)
  }
})

test("the Pi board sits inside the panel with its connectors", () => {
  // Board outline: 85 x 56, centred on the panel.
  expect(PI_PANEL_PI_BOX.x2 - PI_PANEL_PI_BOX.x1).toBeCloseTo(85, 9)
  expect(PI_PANEL_PI_BOX.y2 - PI_PANEL_PI_BOX.y1).toBeCloseTo(56, 9)
  expect((PI_PANEL_PI_BOX.x1 + PI_PANEL_PI_BOX.x2) / 2).toBeCloseTo(0, 9)
  // Envelope with the connectors: USB-A/RJ45 reach -45.8, the other side +44.2.
  expect(PI_PANEL_PI_ENVELOPE.x1).toBeCloseTo(-45.8, 6)
  expect(PI_PANEL_PI_ENVELOPE.x2).toBeCloseTo(44.2, 6)
  expect(PI_PANEL_PI_ENVELOPE.y1).toBeCloseTo(-29.7, 6)
  expect(PI_PANEL_PI_ENVELOPE.y2).toBeCloseTo(28, 6)
  // Everything, connectors included, stays inside the panel with margin.
  expect(PANEL_HALF_WIDTH + PI_PANEL_PI_ENVELOPE.x1).toBeGreaterThan(4)
  expect(PANEL_HALF_WIDTH - PI_PANEL_PI_ENVELOPE.x2).toBeGreaterThan(4)
  expect(PI_PANEL.height / 2 + PI_PANEL_PI_ENVELOPE.y1).toBeGreaterThan(30)
  // Every hole keeps at least 3 mm of FR-4 to the board edge it sits in.
  for (const h of PI_PANEL_PI_HOLES) {
    expect(h.x - PI_PANEL_PI_BOX.x1).toBeGreaterThanOrEqual(3.5 - 1e-9)
    expect(PI_PANEL_PI_BOX.x2 - h.x).toBeGreaterThanOrEqual(3.5 - 1e-9)
    expect(h.y - PI_PANEL_PI_BOX.y1).toBeGreaterThanOrEqual(3.5 - 1e-9)
    expect(PI_PANEL_PI_BOX.y2 - h.y).toBeGreaterThanOrEqual(3.5 - 1e-9)
  }
})

test("the rail holes keep the Doepfer rule and clear the Pi holes", () => {
  // Rail X spacing: 17 x 5.08 = 86.36 at the same 7.42 mm centre inset.
  const left = PI_PANEL_RAIL_HOLES.find(h => h.x < 0 && h.y < 0)!
  const right = PI_PANEL_RAIL_HOLES.find(h => h.x > 0 && h.y < 0)!
  expect(right.x - left.x).toBeCloseTo(17 * 5.08, 6)
  expect((PI_PANEL.width - (right.x - left.x)) / 2).toBeCloseTo(7.42, 6)
  for (const h of PI_PANEL_RAIL_HOLES) expect(Math.abs(h.y)).toBeCloseTo(61.25, 6)
  expect(RAIL_HOLE_DIAMETER).toBe(3.2)
  expect(RAIL_HOLES.length).toBe(4)
  for (const p of PI_PANEL_PI_HOLES) {
    for (const r of PI_PANEL_RAIL_HOLES) {
      expect(Math.hypot(p.x - r.x, p.y - r.y)).toBeGreaterThan(30)
    }
  }
})

test("the silkscreen labels name the Pi edges and stay inside the board", () => {
  const limitX = PANEL_HALF_WIDTH - 0.5
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
  // Each edge is named where it sits: the header on +Y, the USB-C/HDMI edge on
  // -Y, the USB-A/RJ45 edge on -X and the microSD edge on +X.
  const at = (text: string) => PI_PANEL_LABELS.find(l => l.text.startsWith(text))!
  expect(at("GPIO").y).toBeGreaterThan(PI_PANEL_PI_BOX.y2)
  expect(at("USB-C").y).toBeLessThan(PI_PANEL_PI_ENVELOPE.y1)
  expect(at("USB-A").x).toBeLessThan(0)
  expect(at("microSD").x).toBeGreaterThan(0)
  // Pin 1 sits on the header's +X end, 24.13 mm from the header centre.
  expect(at("1").x).toBeCloseTo(PI_PANEL_PI_MOUNT.x + 24.13, 6)
})

test("both depth chains stay inside the Eurorack Go's upper row", () => {
  // Design check on the candidate chains, not a fit approval. The Pi module
  // sits beside the main module in the upper row (its 45.1 mm depth is over the
  // 40 mm lower row), so both modules claim the 62 mm upper row.
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

  // Bare board: the fab pipeline fails if a component ever lands here, and this
  // is the test-level tripwire for the same rule.
  const parts = elements.filter(e =>
    ["pcb_smtpad", "pcb_plated_hole", "source_component", "pcb_component"].includes(e.type))
  expect(parts.length).toBe(0)

  const texts = elements.filter(e => e.type === "pcb_silkscreen_text")
  expect(texts.length).toBe(PI_PANEL_TITLES.length + PI_PANEL_LABELS.length)
})

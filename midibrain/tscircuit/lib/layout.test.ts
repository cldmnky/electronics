import { expect, test } from "bun:test"
import { existsSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs"
import { tmpdir } from "node:os"
import { dirname, join } from "node:path"
import { renderMechanical, run } from "../scripts/export-mechanical"
import {
  BRACKET_ALLOWANCE,
  DEPTH_TARGET,
  DISPLAY_CONCEPT,
  DISPLAY_FORWARD_ENVELOPE,
  DISPLAY_STACK,
  DISPLAY_UNDERSIDE_GAP,
  FRONT_TOP_SMD_MAX_HEIGHT,
  LCD_CUTOUT_ALLOWANCE,
  LIGHT_PIPE,
  LIGHT_PIPE_REACH,
  MOUNT_HOLE_DIAMETER,
  NOTCH_BOUNDS,
  PANEL_TO_FRONT_BOARD_GAP,
  PROVISIONAL_Z,
  mechanicalExport,
  provisionalInstalledDepth,
  roundedRectOutline,
  DISPLAY_STANDOFFS,
  FEEDTHROUGH_BRACKET,
  FRONT_BOARD_HOLES,
  FRONT_COLUMNS,
  FRONT_CONCEPT,
  FRONT_INTERCONNECT,
  FRONT_PCB,
  FRONT_REAR_MOUNT,
  FRONT_TOP_NOTCH,
  JACK_Y,
  LCD_MOUNT_HOLES,
  LCD_WINDOW,
  midiJackX,
  LED_ID,
  LED_Y,
  PANEL,
  PANEL_CUTOUTS,
  PANEL_LCD_CUTOUT,
  PANEL_TITLES,
  PI_HOLES,
  PI_MOUNT,
  panelTitleExtents,
  PLAY_START,
  REAR_PCB,
  RIGHT_COLUMN,
  RIGHT_CONTROLS,
  TRANSPORT_COLUMN_PITCH,
  TRANSPORT_Y,
  USB_FEEDTHROUGHS,
  displayAnnotationXY,
} from "./layout"
import { PI_SPI, PICO_GPIO } from "./pin-map"

const point = (id: string) => {
  const p = FRONT_CONCEPT.find(p => p.id === id)
  if (!p) throw new Error(`missing control ${id}`)
  return p
}

test("the concept is 26HP and tentative points stay on panel", () => {
  expect(PANEL.width).toBeCloseTo(26 * 5.08, 6)
  expect(PANEL.height).toBe(128.5)
  expect(new Set(FRONT_CONCEPT.map(x => x.id)).size).toBe(FRONT_CONCEPT.length)
  for (const p of FRONT_CONCEPT) {
    expect(Math.abs(p.x)).toBeLessThan(PANEL.width / 2)
    expect(Math.abs(p.y)).toBeLessThan(PANEL.height / 2)
  }
})

test("display, window, cutout and mounting holes share one coordinate source", () => {
  const d = DISPLAY_CONCEPT
  // Owner-approved target: 2 mm left and 4 mm up from (-15, 7).
  expect(d.x).toBe(-17)
  expect(d.y).toBe(11)
  // Holes derive from the display centre plus the vendor-local offsets.
  expect(LCD_MOUNT_HOLES).toHaveLength(DISPLAY_STANDOFFS.length)
  LCD_MOUNT_HOLES.forEach((h, i) => {
    expect(h.x).toBeCloseTo(d.x + DISPLAY_STANDOFFS[i].x, 9)
    expect(h.y).toBeCloseTo(d.y + DISPLAY_STANDOFFS[i].y, 9)
  })
  // Asymmetric 58 x 49 mm pattern preserved.
  const xs = [...new Set(LCD_MOUNT_HOLES.map(h => h.x))].sort((a, b) => a - b)
  const ys = [...new Set(LCD_MOUNT_HOLES.map(h => h.y))].sort((a, b) => a - b)
  expect(xs[1] - xs[0]).toBeCloseTo(58, 6)
  expect(ys[1] - ys[0]).toBeCloseTo(49, 6)
  // Panel cutout centred on the display and sized over the module.
  expect(PANEL_LCD_CUTOUT.x).toBe(d.x)
  expect(PANEL_LCD_CUTOUT.y).toBe(d.y)
  expect(PANEL_LCD_CUTOUT.width).toBeGreaterThan(d.width)
  expect(PANEL_LCD_CUTOUT.height).toBeGreaterThan(d.height)
  // Active-area window keeps its 2.935 mm left offset and 0.5 mm/side clearance.
  expect(LCD_WINDOW.x).toBeCloseTo(d.x - 2.935, 6)
  expect(LCD_WINDOW.y).toBe(d.y)
  expect(LCD_WINDOW.width).toBeCloseTo(d.innerWidth + 1, 6)
  expect(LCD_WINDOW.height).toBeCloseTo(d.innerHeight + 1, 6)
})

test("right-hand controls share one centreline with equal 19 mm spacing", () => {
  const rc = RIGHT_CONTROLS
  expect(new Set([rc.TEMPO.x, rc.NAV.x, rc.SHIFT_EXIT.x]).size).toBe(1)
  expect(rc.TEMPO.x).toBe(RIGHT_COLUMN.x)
  expect(rc.TEMPO.y - rc.NAV.y).toBe(RIGHT_COLUMN.pitch)
  expect(rc.NAV.y - rc.SHIFT_EXIT.y).toBe(RIGHT_COLUMN.pitch)
  for (const [id, c] of [["TEMPO", rc.TEMPO], ["NAV", rc.NAV], ["SHIFT / EXIT", rc.SHIFT_EXIT]] as const) {
    expect(point(id)).toMatchObject({ x: c.x, y: c.y })
  }
  // Right of the display, on the panel and the front board.
  for (const c of [rc.TEMPO, rc.NAV, rc.SHIFT_EXIT]) {
    expect(c.x).toBeGreaterThan(DISPLAY_CONCEPT.x + DISPLAY_CONCEPT.width / 2)
    expect(Math.abs(c.x)).toBeLessThan(FRONT_PCB.width / 2)
    expect(Math.abs(c.y)).toBeLessThan(FRONT_PCB.height / 2)
  }
  // Interconnect header clears the control column courtyards:
  // B3F courtyard half-width 7.5 at x = 47; 2x13 socket courtyard half-width
  // ~4.75 rotated onto x. The build placement check re-verifies the pads.
  expect(FRONT_INTERCONNECT.x + 4.75).toBeLessThan(RIGHT_COLUMN.x - 7.5)
  expect(Math.abs(FRONT_INTERCONNECT.x)).toBeLessThan(FRONT_PCB.width / 2)
})

test("transport columns align with their LEDs and MIDI jacks", () => {
  expect(FRONT_COLUMNS.map(c => c.x)).toEqual([-53, -29, -5, 19])
  for (let i = 1; i < FRONT_COLUMNS.length; i++) {
    expect(FRONT_COLUMNS[i].x - FRONT_COLUMNS[i - 1].x).toBe(TRANSPORT_COLUMN_PITCH)
  }
  // PLAY START joins the right-hand control column, below SHIFT/EXIT.
  expect(PLAY_START.x).toBe(RIGHT_COLUMN.x)
  expect(point("PLAY START")).toMatchObject({ x: PLAY_START.x, y: TRANSPORT_Y })
  // All five transport buttons share one centreline.
  expect(new Set(["REC", "OVER DUB", "STOP", "PLAY", "PLAY START"].map(id => point(id).y)).size).toBe(1)
  // All four jacks share one centreline, below the buttons.
  expect(new Set(FRONT_COLUMNS.map(c => point(c.jack).y)).size).toBe(1)
  expect(JACK_Y).toBeLessThan(TRANSPORT_Y)
  for (const c of FRONT_COLUMNS) {
    expect(point(c.button)).toMatchObject({ x: c.x, y: TRANSPORT_Y })
    expect(point(c.jack)).toMatchObject({ x: c.x, y: JACK_Y })
  }
  // LEDs sit above REC / OVER DUB / PLAY only, centred on their buttons.
  expect(LED_Y - TRANSPORT_Y).toBe(10)
  for (const [button, led] of [["REC", "REC LED"], ["OVER DUB", "OVERDUB LED"], ["PLAY", "PLAY LED"]] as const) {
    expect(led).toBe(LED_ID[button as keyof typeof LED_ID])
    expect(point(led).x).toBeCloseTo(point(button).x, 9)
    expect(point(led).y).toBe(LED_Y)
    expect(point(led).y).toBeGreaterThan(point(button).y)
  }
})

test("3.5-inch DSI mounting holes match the Pi hole grid", () => {
  expect(PI_HOLES).toHaveLength(4)
  // Portrait Pi, shifted +6 east: holes at 6 ± 24.5 / ±29.
  expect([...new Set(PI_HOLES.map(p => p.x))].sort((a,b) => a-b)).toEqual([-18.5, 30.5])
  expect([...new Set(PI_HOLES.map(p => p.y))].sort((a,b) => a-b)).toEqual([-29, 29])
  // 56-wide Pi board spans X −22…+34: 7 mm clear of the Pico right edge
  // (X −29: 21 mm module centred on the −39.5 socket midpoint).
  expect((PI_MOUNT.x - 56 / 2) - (-39.5 + 21 / 2)).toBeCloseTo(7, 6)
  expect(DISPLAY_STANDOFFS).toHaveLength(4)
  const xs = [...new Set(DISPLAY_STANDOFFS.map(p => p.x))].sort((a,b) => a-b)
  const ys = [...new Set(DISPLAY_STANDOFFS.map(p => p.y))].sort((a,b) => a-b)
  expect(xs[1] - xs[0]).toBeCloseTo(58, 0)
  expect(ys[1] - ys[0]).toBeCloseTo(49, 0)
  expect(REAR_PCB.width).toBeCloseTo(20 * 5.08, 6)
  expect(REAR_PCB.height).toBe(112)
})

test("3.5-inch DSI display and requested control centres follow the concept", () => {
  const d = DISPLAY_CONCEPT
  expect(Math.abs(d.x) + d.width / 2).toBeLessThan(PANEL.width / 2)
  expect(Math.abs(d.y) + d.height / 2).toBeLessThan(PANEL.height / 2)
  expect(FRONT_PCB.width).toBeLessThan(PANEL.width)
  expect(REAR_PCB.width).toBeLessThan(FRONT_PCB.width)
  for (const p of FRONT_CONCEPT) {
    if (p.kind === "cable") {
      // Feedthroughs sit in the front board top relief opening.
      expect(p.y).toBeGreaterThanOrEqual(FRONT_TOP_NOTCH.y - FRONT_TOP_NOTCH.height / 2)
    } else {
      expect(Math.abs(p.x)).toBeLessThan(FRONT_PCB.width / 2)
      expect(Math.abs(p.y)).toBeLessThan(FRONT_PCB.height / 2)
    }
  }
  const transport = ["REC", "OVER DUB", "STOP", "PLAY", "PLAY START"]
  for (const id of transport) {
    const p = point(id)
    expect(p.y).toBeLessThan(d.y - d.height / 2)
  }
  for (let i = 1; i < transport.length; i++) {
    expect(point(transport[i - 1]).x).toBeLessThan(point(transport[i]).x)
  }
  for (const id of ["MIDI IN 1", "MIDI IN 2", "MIDI OUT 1", "MIDI OUT 2"]) {
    const p = point(id)
    // Jacks are in the bottom row, below the display and the five buttons.
    expect(p.y).toBeLessThan(-40)
  }
  for (const id of ["USB-C POWER", "UMC AUDIO USB", "MIDI HUB USB"]) {
    expect(point(id).y).toBeGreaterThan(d.y + d.height / 2)
  }
  expect(FRONT_TOP_NOTCH.y + FRONT_TOP_NOTCH.height / 2).toBe(FRONT_PCB.height / 2)
  const notchLeft = FRONT_TOP_NOTCH.x - FRONT_TOP_NOTCH.width / 2
  const notchRight = FRONT_TOP_NOTCH.x + FRONT_TOP_NOTCH.width / 2
  for (const x of FEEDTHROUGH_BRACKET.mountX) {
    expect(Math.abs(x)).toBeLessThan(FRONT_PCB.width / 2)
    expect(x < notchLeft || x > notchRight).toBe(true)
  }
  // Display annotations follow the display (display-local offsets).
  expect(displayAnnotationXY({ dx: 0, dy: 0 })).toEqual({ x: d.x, y: d.y })
  // These are centre/box relationships only, not component collision checks.
})

test("panel standoffs and the shared front-rear grid clear parts and edges", () => {
  // Panel-to-front standoffs live at the board corners with 1.65 mm edge
  // ligament to the 128 x 112 front board.
  expect(FRONT_BOARD_HOLES).toEqual([
    { x: -61, y: -53 }, { x: 61, y: -53 }, { x: -61, y: 53 }, { x: 61, y: 53 },
  ])
  for (const h of FRONT_BOARD_HOLES) {
    expect(FRONT_PCB.width / 2 - Math.abs(h.x)).toBeGreaterThanOrEqual(3)
    expect(FRONT_PCB.height / 2 - Math.abs(h.y)).toBeGreaterThanOrEqual(3)
  }
  // Bottom-left standoff head (M2.5, r2.5 at (-61, -53)) clears the J1 MIDI
  // jack courtyard (X -58..-48): 0.5 mm to the courtyard edge.
  const j1 = FRONT_COLUMNS[0]!
  expect(-58 - (-61 + 2.5)).toBeCloseTo(0.5, 6)
  expect(j1.x).toBe(-53)
  // Shared front-rear spacer grid: inside both boards, top pair below the
  // relief notch (bottom 48.0), bottom pair below the jack courtyards.
  expect(FRONT_REAR_MOUNT).toEqual([
    { x: -44, y: -52 }, { x: 44, y: -52 }, { x: -44, y: 43 }, { x: 44, y: 43 },
  ])
  for (const h of FRONT_REAR_MOUNT) {
    expect(Math.abs(h.x)).toBeLessThan(101.6 / 2 - 3)
    expect(Math.abs(h.y)).toBeLessThan(112 / 2 - 3)
  }
  for (const h of FRONT_REAR_MOUNT.filter(h => h.y > 0)) {
    expect(h.y + 1.35).toBeLessThan(FRONT_TOP_NOTCH.y - FRONT_TOP_NOTCH.height / 2)
  }
  for (const h of FRONT_REAR_MOUNT.filter(h => h.y < 0)) {
    expect(h.y).toBeLessThan(JACK_Y - 3)
  }
})

test("panel caption stays on the board (no center_left overflow)", () => {
  // The wordmark is drawn paths now (panel-art); this text primitive is the
  // jack-field caption, centred between the IN 2 and OUT 1 columns.
  expect(PANEL_TITLES).toHaveLength(1)
  const t = PANEL_TITLES[0]!
  expect(t.text).toBe("MIDI TRS TYPE A")
  const e = panelTitleExtents(t)
  expect(e.x1).toBeGreaterThan(-PANEL.width / 2 + 1)
  expect(e.x2).toBeLessThan(PANEL.width / 2 - 1)
  expect(e.y1).toBeGreaterThan(-PANEL.height / 2 + 1)
  expect(e.y2).toBeLessThan(PANEL.height / 2 - 1)
  expect(e.x2).toBeLessThan(midiJackX("MIDI OUT 1") - 2)
  expect(e.x1).toBeGreaterThan(midiJackX("MIDI IN 2") + 2)
  // Below the jack barrels, above the wordmark ink.
  expect(t.y).toBeLessThan(JACK_Y - 3)
  expect(t.y).toBeGreaterThan(-57)
})

test("allocated GPIO and physical pins are unique", () => {
  for (const [items, fields] of [[PICO_GPIO, ["gpio", "pin", "signal"]], [PI_SPI, ["bcm", "pin", "signal"]]] as const) {
    for (const field of fields) {
      const values = items.map(item => (item as any)[field])
      expect(new Set(values).size).toBe(values.length)
    }
  }
  expect(PICO_GPIO.find(p => p.signal === "SPI0_RX_MOSI")?.gpio).toBe(16)
  expect(PICO_GPIO.find(p => p.signal === "MIDI_OUT_2_UART1_TX")?.gpio).toBe(4)
  expect(PICO_GPIO.find(p => p.signal === "SHIFT_EXIT")?.gpio).toBe(28)
  expect(USB_FEEDTHROUGHS).toHaveLength(3)
  expect(new Set(USB_FEEDTHROUGHS.map(f => f.id)).size).toBe(3)
})
// ---- Pure geometry: LCD aperture -----------------------------------------
const segDist = (p: { x: number; y: number }, a: { x: number; y: number }, b: { x: number; y: number }) => {
  const dx = b.x - a.x
  const dy = b.y - a.y
  const len2 = dx * dx + dy * dy
  const t = len2 === 0 ? 0 : Math.max(0, Math.min(1, ((p.x - a.x) * dx + (p.y - a.y) * dy) / len2))
  return Math.hypot(p.x - (a.x + t * dx), p.y - (a.y + t * dy))
}
const polyDist = (p: { x: number; y: number }, points: readonly { x: number; y: number }[]) =>
  Math.min(...points.map((q, i) => segDist(p, q, points[(i + 1) % points.length]!)))

test("LCD aperture is expanded over the module, with an explicit corner relief", () => {
  const cut = PANEL_LCD_CUTOUT
  const d = DISPLAY_CONCEPT
  // 0.7 mm per side: 0.2 mm fabrication tolerance plus 0.5 mm unverified
  // registration/assembly allowance. The withdrawn 89.2 x 56.3 mm aperture left
  // only 0.165/0.15 mm per side, inside the +-0.2 mm fabrication tolerance.
  expect(LCD_CUTOUT_ALLOWANCE.perSide).toBe(0.7)
  expect(cut.width).toBeCloseTo(d.width + 2 * LCD_CUTOUT_ALLOWANCE.perSide, 6)
  expect(cut.height).toBeCloseTo(d.height + 2 * LCD_CUTOUT_ALLOWANCE.perSide, 6)
  expect(cut.width).toBeGreaterThan(89.2)
  expect(cut.height).toBeGreaterThan(56.3)
  // A routed inside corner cannot be sharp; the relief is explicit and is not
  // allowed to round the aperture back inside the 0.5 mm minimum.
  expect(cut.cornerRadius).toBeGreaterThanOrEqual(0.5)
})

test("actual segment clearance at every module corner is at least 0.5 mm", () => {
  const outline = roundedRectOutline(PANEL_LCD_CUTOUT)
  expect(outline).toHaveLength(4 * (6 + 1))
  // Counter-clockwise closed loop with the exact outer bounds.
  const xs = outline.map(p => p.x)
  const ys = outline.map(p => p.y)
  expect(Math.max(...xs) - Math.min(...xs)).toBeCloseTo(PANEL_LCD_CUTOUT.width, 6)
  expect(Math.max(...ys) - Math.min(...ys)).toBeCloseTo(PANEL_LCD_CUTOUT.height, 6)
  // The chords lie INSIDE the true corner arc, so the polygon is never larger
  // than the exact rounded rectangle. Measure the polygon, not the formula.
  const exact = LCD_CUTOUT_ALLOWANCE.cornerRadius -
    Math.abs(LCD_CUTOUT_ALLOWANCE.cornerRadius - LCD_CUTOUT_ALLOWANCE.perSide) * Math.SQRT2
  for (const sx of [-1, 1]) {
    for (const sy of [-1, 1]) {
      const corner = {
        x: DISPLAY_CONCEPT.x + sx * DISPLAY_CONCEPT.width / 2,
        y: DISPLAY_CONCEPT.y + sy * DISPLAY_CONCEPT.height / 2,
      }
      const measured = polyDist(corner, outline)
      expect(measured).toBeGreaterThanOrEqual(0.5)
      expect(measured).toBeLessThanOrEqual(exact + 1e-9)
    }
  }
})

test("LCD aperture stays clear of the panel edge and the control openings", () => {
  const cut = PANEL_LCD_CUTOUT
  expect(Math.abs(cut.x) + cut.width / 2).toBeLessThan(PANEL.width / 2 - 1)
  expect(Math.abs(cut.y) + cut.height / 2).toBeLessThan(PANEL.height / 2 - 1)
  // The aperture may not reach the LED, transport or control rows.
  expect(cut.y - cut.height / 2).toBeGreaterThan(LED_Y + PANEL_CUTOUTS.led / 2)
  expect(cut.y + cut.height / 2).toBeLessThan(USB_FEEDTHROUGHS[0]!.y - USB_FEEDTHROUGHS[0]!.cutoutHeight / 2)
  for (const c of [RIGHT_CONTROLS.TEMPO, RIGHT_CONTROLS.NAV, RIGHT_CONTROLS.SHIFT_EXIT]) {
    expect(c.x - PANEL_CUTOUTS.control / 2).toBeGreaterThan(cut.x + cut.width / 2)
  }
})

// ---- Pure geometry: bracket ----------------------------------------------
const ligament = (x: number) => Math.min(
  x - MOUNT_HOLE_DIAMETER / 2 - FEEDTHROUGH_BRACKET.outline.x1,
  FEEDTHROUGH_BRACKET.outline.x2 - x - MOUNT_HOLE_DIAMETER / 2,
)

test("bracket stock spans both mounts with a nut wall around them", () => {
  const b = FEEDTHROUGH_BRACKET
  expect(b.mountX).toEqual([-60, 55])
  // 3.0 mm from each mount centre to the outline: 1.65 mm of material outside a
  // D2.7 hole, which leaves 0.5 mm around an M2.5 nut (circumradius 2.5 mm).
  for (const x of b.mountX) expect(ligament(x)).toBeCloseTo(1.65, 6)
  expect(b.outline.width).toBeCloseTo(121, 6)
  expect(b.outline.height).toBeCloseTo(14.5, 6)
  expect(b.outline.x1).toBeGreaterThan(-PANEL.width / 2)
  expect(b.outline.x2).toBeLessThan(PANEL.width / 2)
  // Backed by the front board, and clear of the display below it.
  expect(b.outline.x1).toBeGreaterThan(-FRONT_PCB.width / 2)
  expect(b.outline.x2).toBeLessThan(FRONT_PCB.width / 2)
  expect(b.outline.y2).toBeLessThanOrEqual(FRONT_PCB.height / 2)
  expect(b.outline.y1).toBeGreaterThan(DISPLAY_CONCEPT.y + DISPLAY_CONCEPT.height / 2)
})

test("bracket slots follow the feedthrough cutouts and keep a printable web", () => {
  const b = FEEDTHROUGH_BRACKET
  expect(b.slots).toHaveLength(USB_FEEDTHROUGHS.length)
  b.slots.forEach((slot, i) => {
    const f = USB_FEEDTHROUGHS[i]!
    expect(slot.id).toBe(f.id)
    expect(slot.x).toBe(f.x)
    expect(slot.width).toBeCloseTo(f.cutoutWidth + 2 * BRACKET_ALLOWANCE.slotClearancePerSide, 6)
    // Inside the board notch in X: outside it the board is solid, so a slot
    // there could not pass a cable.
    expect(slot.x - slot.width / 2).toBeGreaterThanOrEqual(NOTCH_BOUNDS.left)
    expect(slot.x + slot.width / 2).toBeLessThanOrEqual(NOTCH_BOUNDS.right)
    // Open to the top edge.
    expect(slot.yTop).toBeCloseTo(b.outline.y2, 6)
    // Height clearance, not only width: the floor must clear the whole declared
    // panel opening plus the same per-side clearance. A fixed floor at y = 50
    // cut into even the declared openings (USB-C 49.7, USB-A 49.85).
    const openingBottom = f.y - f.cutoutHeight / 2
    expect(slot.yBottom).toBeLessThanOrEqual(openingBottom)
    expect(slot.yBottom).toBeCloseTo(openingBottom - BRACKET_ALLOWANCE.slotClearancePerSide, 6)
    // ... and it must stay inside the board notch: below it the board is solid.
    expect(slot.yBottom).toBeGreaterThanOrEqual(NOTCH_BOUNDS.bottom)
  })
  // The reviewed defect: 40 mm slots at x = -5 and +35 touched, giving a 0 mm
  // web. Slots may never touch, and the webs must be printable.
  const webs = b.slots.slice(1).map((s, i) => {
    const prev = b.slots[i]!
    return (s.x - s.width / 2) - (prev.x + prev.width / 2)
  })
  expect(webs).toHaveLength(2)
  for (const w of webs) {
    expect(w).toBeGreaterThan(BRACKET_ALLOWANCE.materialToCuts)
    expect(w).toBeGreaterThan(10)
  }
})

test("bracket slot floors stay above the notch bottom for every feedthrough", () => {
  // A future feedthrough that needs a deeper slot must fail here rather than be
  // silently clamped: the notch bottom is solid board.
  for (const f of USB_FEEDTHROUGHS) {
    const floor = f.y - f.cutoutHeight / 2 - BRACKET_ALLOWANCE.slotClearancePerSide
    expect(floor).toBeGreaterThan(NOTCH_BOUNDS.bottom)
  }
})

test("bracket clears the top front-rear spacer hardware and carries tie holes", () => {
  const b = FEEDTHROUGH_BRACKET
  const topPair = FRONT_REAR_MOUNT.filter(h => h.y > 0)
  expect(b.mountHeadClearance).toHaveLength(topPair.length)
  b.mountHeadClearance.forEach((head, i) => {
    expect(head.x).toBe(topPair[i]!.x)
    expect(head.y).toBe(topPair[i]!.y)
    // PROVISIONAL envelope: bigger than the D2.7 clearance hole, and off both
    // bracket mounts so the bar keeps its nut material. The real head is a gate.
    expect(head.diameter).toBeGreaterThan(MOUNT_HOLE_DIAMETER)
    for (const x of b.mountX) {
      expect(Math.hypot(head.x - x, head.y - b.mountY)).toBeGreaterThan(
        head.diameter / 2 + MOUNT_HOLE_DIAMETER / 2)
    }
  })
  // One tie hole per web: between the slots and outside the end slots.
  expect(b.tieHoles).toHaveLength(b.slots.length + 1)
  for (const tie of b.tieHoles) {
    for (const slot of b.slots) {
      const dx = Math.max(slot.x - slot.width / 2 - tie.x, 0, tie.x - (slot.x + slot.width / 2))
      expect(dx).toBeGreaterThan(0)
    }
    expect(tie.x - tie.diameter / 2).toBeGreaterThan(b.outline.x1)
    expect(tie.x + tie.diameter / 2).toBeLessThan(b.outline.x2)
  }
})

test("mechanical/layout.json matches the canonical export", () => {
  // The CAD script reads this file and must never restate a coordinate. If the
  // layout changed without regenerating it, this fails: run
  //   cd tscircuit && bun run scripts/export-mechanical.ts
  const text = readFileSync(new URL("../../mechanical/layout.json", import.meta.url), "utf8")
  expect(JSON.parse(text)).toEqual(mechanicalExport())
})

// ---- Provisional stack ----------------------------------------------------
test("provisional Z stack names its datum and claims no clearance", () => {
  expect(PROVISIONAL_Z.panelFront).toBe(0)
  expect(PROVISIONAL_Z.panelRear).toBe(PANEL.thickness)
  // Assumption A: the glass front sits flush with the panel front face.
  expect(PROVISIONAL_Z.displayFront).toBe(0)
  expect(PROVISIONAL_Z.displayPcb).toBeCloseTo(DISPLAY_FORWARD_ENVELOPE, 6)
  expect(PANEL_TO_FRONT_BOARD_GAP).toBeCloseTo(PROVISIONAL_Z.frontBoardTop - PROVISIONAL_Z.panelRear, 6)
  // One standoff definition only: 9.0 mm, above the vendor's 6.7 mm rear envelope.
  expect(DISPLAY_STACK.standoffHeight).toBe(9)
  expect(DISPLAY_STACK.rearEnvelopeDepth).toBeLessThan(DISPLAY_STACK.standoffHeight)
  expect(PROVISIONAL_Z.frontBoardTop).toBeCloseTo(PROVISIONAL_Z.displayPcb + DISPLAY_STACK.standoffHeight, 6)
})

test("display underside gap and light-pipe reach stay pending, not claimed", () => {
  // The tallest front-side part with a stated height is the MIDI receiver in the
  // Toshiba 11-4L1S body at 2.2 mm max installed. The provisional stack leaves
  // 2.3 mm, i.e. 0.1 mm more: that does NOT prove the display underside, whose
  // component map, FPC latch and module tolerance are unmapped.
  expect(DISPLAY_UNDERSIDE_GAP).toBeCloseTo(PROVISIONAL_Z.frontBoardTop - PROVISIONAL_Z.displayRearMost, 6)
  expect(FRONT_TOP_SMD_MAX_HEIGHT.height).toBe(2.2)
  expect(DISPLAY_UNDERSIDE_GAP - FRONT_TOP_SMD_MAX_HEIGHT.height).toBeCloseTo(0.1, 6)
  expect(DISPLAY_UNDERSIDE_GAP - FRONT_TOP_SMD_MAX_HEIGHT.height).toBeLessThan(0.5)
  // Light-pipe reach is unproven: no LED height and no seat allowance is stored.
  expect(LIGHT_PIPE.length).toBe(10.16)
  expect(LIGHT_PIPE_REACH.unaccounted).toBeCloseTo(PANEL_TO_FRONT_BOARD_GAP - LIGHT_PIPE.length, 6)
  expect("seatAllowance" in LIGHT_PIPE).toBe(false)
})

test("provisional depth chain stays inside the owner target", () => {
  expect(DEPTH_TARGET).toBe(110)
  expect(provisionalInstalledDepth()).toBeLessThanOrEqual(DEPTH_TARGET)
  // Rear board overhang is 13.2 mm per side when both boards share a centre.
  expect((FRONT_PCB.width - REAR_PCB.width) / 2).toBeCloseTo(13.2, 6)
})

// ---- Export script: generation and non-writing --check --------------------
const tempJson = () => {
  const dir = mkdtempSync(join(tmpdir(), "midibrain-layout-"))
  return join(dir, "layout.json")
}

test("export --check accepts fresh JSON and writes nothing", () => {
  const path = tempJson()
  try {
    writeFileSync(path, renderMechanical())
    const before = readFileSync(path, "utf8")
    const result = run(["--check"], path)
    expect(result.code).toBe(0)
    expect(result.wrote).toBe(false)
    expect(readFileSync(path, "utf8")).toBe(before)
  } finally {
    rmSync(dirname(path), { recursive: true, force: true })
  }
})

test("export --check rejects stale JSON and leaves it unchanged", () => {
  const path = tempJson()
  try {
    const stale = `${JSON.stringify({ stale: true }, null, 2)}\n`
    writeFileSync(path, stale)
    const result = run(["--check"], path)
    expect(result.code).toBe(1)
    expect(result.wrote).toBe(false)
    expect(result.message).toContain("stale")
    // The gate must not silently repair the tracked file.
    expect(readFileSync(path, "utf8")).toBe(stale)
  } finally {
    rmSync(dirname(path), { recursive: true, force: true })
  }
})

test("export --check fails when the JSON is missing", () => {
  const path = tempJson()
  try {
    const result = run(["--check"], path)
    expect(result.code).toBe(1)
    expect(result.wrote).toBe(false)
    expect(existsSync(path)).toBe(false)
  } finally {
    rmSync(dirname(path), { recursive: true, force: true })
  }
})

test("export --check rejects unknown arguments without writing", () => {
  const path = tempJson()
  try {
    writeFileSync(path, "unchanged\n")
    for (const args of [["--write"], ["--check", "--force"], ["--fix"], ["check"]]) {
      const result = run(args, path)
      expect(result.code).toBe(2)
      expect(result.wrote).toBe(false)
      expect(readFileSync(path, "utf8")).toBe("unchanged\n")
    }
  } finally {
    rmSync(dirname(path), { recursive: true, force: true })
  }
})

test("export with no argument writes the canonical JSON", () => {
  const path = tempJson()
  try {
    const result = run([], path)
    expect(result.code).toBe(0)
    expect(result.wrote).toBe(true)
    expect(readFileSync(path, "utf8")).toBe(renderMechanical())
    // And that render is what the tracked file holds.
    expect(readFileSync(new URL("../../mechanical/layout.json", import.meta.url), "utf8"))
      .toBe(renderMechanical())
  } finally {
    rmSync(dirname(path), { recursive: true, force: true })
  }
})

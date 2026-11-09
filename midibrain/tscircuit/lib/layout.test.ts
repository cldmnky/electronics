import { expect, test } from "bun:test"
import { existsSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs"
import { tmpdir } from "node:os"
import { dirname, join } from "node:path"
import { renderMechanical, run } from "../scripts/export-mechanical"
import {
  BRACKET_ALLOWANCE,
  DEPTH_TARGET,
  DISPLAY_CONCEPT,
  DISPLAY_ACTIVE_OFFSET_X,
  DISPLAY_DATUM,
  DISPLAY_BOSS_PATTERN,
  DISPLAY_STANDOFFS,
  DISPLAY_STANDOFF,
  DISPLAY_GLASS_FRONT_Z,
  DISPLAY_STACK,
  DISPLAY_VENDOR,
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
  RAIL_HOLE_DIAMETER,
  RAIL_HOLES,
  PANEL,
  PANEL_CUTOUTS,
  PANEL_LCD_CUTOUT,
  PANEL_LABELS,
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

/** Shortest distance from a point to a closed polygon, in mm. */
const polygonDistance = (px: number, py: number, pts: readonly { x: number; y: number }[]) => {
  let best = Infinity
  for (let i = 0; i < pts.length; i++) {
    const a = pts[i]!
    const b = pts[(i + 1) % pts.length]!
    const dx = b.x - a.x
    const dy = b.y - a.y
    const l2 = dx * dx + dy * dy
    const t = l2 === 0 ? 0 : Math.max(0, Math.min(1, ((px - a.x) * dx + (py - a.y) * dy) / l2))
    best = Math.min(best, Math.hypot(px - (a.x + t * dx), py - (a.y + t * dy)))
  }
  return best
}

test("the concept is 30HP at 152.0 mm and tentative points stay on panel", () => {
  // 30HP nominal is 30 x 5.08 = 152.4 mm. 152.0 mm is an engineering choice
  // with a 0.4 mm total (0.2 mm per side) nominal allowance - not an
  // owner-specified exact value and not a manufacturer mandate.
  expect(PANEL.width).toBe(152.0)
  expect(PANEL.height).toBe(128.5)
  expect(new Set(FRONT_CONCEPT.map(x => x.id)).size).toBe(FRONT_CONCEPT.length)
  for (const p of FRONT_CONCEPT) {
    expect(Math.abs(p.x)).toBeLessThan(PANEL.width / 2)
    expect(Math.abs(p.y)).toBeLessThan(PANEL.height / 2)
  }
})

test("display, window, cutout and mounting holes share one coordinate source", () => {
  const d = DISPLAY_CONCEPT
  // Display centre: y stays at the owner-approved 11; x moves -17 -> -9.5 so
  // the 106 mm module stays on the unchanged 128 mm board (left edge -62.5,
  // 1.5 mm ligament) while the value knob, moved 47 -> 52, keeps 2.25 mm clear
  // of the module's right edge 43.5. See docs/decisions.md (D2).
  expect(d.x).toBe(-9.5)
  expect(d.y).toBe(11)
  // Holes derive from the display centre plus the vendor-local offsets - the
  // SMTSO boss pattern, the only M2.5 threads the module offers.
  expect(LCD_MOUNT_HOLES).toHaveLength(DISPLAY_BOSS_PATTERN.length)
  LCD_MOUNT_HOLES.forEach((h, i) => {
    expect(h.x).toBeCloseTo(d.x + DISPLAY_BOSS_PATTERN[i].x, 9)
    expect(h.y).toBeCloseTo(d.y + DISPLAY_BOSS_PATTERN[i].y, 9)
  })
  // Asymmetric 99.30 x 61.80 mm pattern (3.35 / 3.10 edge insets).
  const xs = [...new Set(LCD_MOUNT_HOLES.map(h => h.x))].sort((a, b) => a - b)
  const ys = [...new Set(LCD_MOUNT_HOLES.map(h => h.y))].sort((a, b) => a - b)
  expect(xs[1] - xs[0]).toBeCloseTo(99.3, 6)
  expect(ys[1] - ys[0]).toBeCloseTo(61.8, 6)
  // The spacer arithmetic only closes on this pattern: planeSeparation 9.0 =
  // the 4.0 mm SMT boss projection + the 5.0 mm spacer. The inner plain holes
  // provide no thread, so the fitted pattern and the Z stack must agree.
  expect(DISPLAY_STANDOFF.spacerLength + DISPLAY_VENDOR.pcbRearToBossEnd)
    .toBeCloseTo(DISPLAY_STANDOFF.planeSeparation, 9)
  // Front-board hole webs: the LCD holes and the spacer/board holes are drill
  // features in the same plane; keep at least 1.0 mm of FR-4 between them (the
  // 0.50 mm web a (44, 43) spacer imposed is why that pair moved to ±46).
  for (const l of LCD_MOUNT_HOLES) {
    for (const m of [...FRONT_REAR_MOUNT, ...FRONT_BOARD_HOLES]) {
      const web = Math.hypot(l.x - m.x, l.y - m.y) - 2.15 - MOUNT_HOLE_DIAMETER / 2
      expect(web).toBeGreaterThan(1.0)
    }
  }
  // Panel cutout centred on the module and sized from the GLASS OUTLINE (D1):
  // the glass passes through the panel, so the window is LARGER than the
  // 106 x 68 module outline: 105.42 x 67.07 glass + 0.9 mm per side.
  expect(PANEL_LCD_CUTOUT.x).toBe(d.x)
  expect(PANEL_LCD_CUTOUT.y).toBe(d.y)
  expect(PANEL_LCD_CUTOUT.width).toBeGreaterThan(d.width)
  expect(PANEL_LCD_CUTOUT.height).toBeGreaterThan(d.height)
  expect(PANEL_LCD_CUTOUT.width - d.width).toBeCloseTo(2 * (LCD_CUTOUT_ALLOWANCE.perSide - (d.width - DISPLAY_VENDOR.glass.width) / 2), 2)
  expect(PANEL_LCD_CUTOUT.height - d.height).toBeCloseTo(2 * (LCD_CUTOUT_ALLOWANCE.perSide - (d.height - DISPLAY_VENDOR.glass.height) / 2), 2)
  // The active area is centred on the module outline on this drawing: no offset.
  expect(DISPLAY_ACTIVE_OFFSET_X).toBe(0)
  expect(LCD_WINDOW.x).toBeCloseTo(d.x + DISPLAY_ACTIVE_OFFSET_X, 6)
  expect(LCD_WINDOW.x).toBeCloseTo(d.x, 6)
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
  for (const [id, c] of [["VALUE", rc.TEMPO], ["NAV", rc.NAV], ["SHIFT / EXIT", rc.SHIFT_EXIT]] as const) {
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
  // LEDs sit BESIDE REC / OVER DUB / PLAY only (owner-authorised fallback 3:
  // the glass-derived window owns the row above the buttons).
  expect(LED_Y).toBe(TRANSPORT_Y)
  for (const [button, led] of [["REC", "REC LED"], ["OVER DUB", "OVERDUB LED"], ["PLAY", "PLAY LED"]] as const) {
    expect(led).toBe(LED_ID[button as keyof typeof LED_ID])
    expect(point(led).y).toBe(TRANSPORT_Y)
    // 12 mm to the right: 3.8 mm of FR-4 to the button opening and to the LED
    // opening, and 12 mm to the next column's button.
    expect(point(led).x - point(button).x).toBeCloseTo(12, 9)
    expect(point(led).x - PANEL_CUTOUTS.led / 2)
      .toBeGreaterThan(point(button).x + PANEL_CUTOUTS.button / 2 + 3)
  }
})

test("4.3-inch DSI mounting holes match the Pi hole grid", () => {
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
  expect(xs[1] - xs[0]).toBeCloseTo(58.0, 6)
  expect(ys[1] - ys[0]).toBeCloseTo(49.04, 6)
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
  // relief notch (bottom 48.0), bottom pair below the jack courtyards. The
  // top pair moved ±44 → ±45 to keep FR-4 web against the new SMTSO-pattern
  // display hole at (+40.15, +41.90); ±46 was tried and rejected (see layout.ts).
  expect(FRONT_REAR_MOUNT).toEqual([
    { x: -44, y: -52 }, { x: 44, y: -52 }, { x: -45, y: 43 }, { x: 45, y: 43 },
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
// The relief radius is fixed by the fabricator, not by the module.
const AUTOPROVISIONAL_CORNER = LCD_CUTOUT_ALLOWANCE.cornerRadius
const segDist = (p: { x: number; y: number }, a: { x: number; y: number }, b: { x: number; y: number }) => {
  const dx = b.x - a.x
  const dy = b.y - a.y
  const len2 = dx * dx + dy * dy
  const t = len2 === 0 ? 0 : Math.max(0, Math.min(1, ((p.x - a.x) * dx + (p.y - a.y) * dy) / len2))
  return Math.hypot(p.x - (a.x + t * dx), p.y - (a.y + t * dy))
}
const polyDist = (p: { x: number; y: number }, points: readonly { x: number; y: number }[]) =>
  Math.min(...points.map((q, i) => segDist(p, q, points[(i + 1) % points.length]!)))

test("LCD aperture is the glass outline plus the stated allowance", () => {
  const cut = PANEL_LCD_CUTOUT
  const glass = DISPLAY_VENDOR.glass
  // 0.9 mm per side: 0.2 mm fabrication tolerance + 0.2 mm vendor glass
  // tolerance (all on one side, worst case) + 0.5 mm unverified registration
  // and assembly allowance.
  expect(LCD_CUTOUT_ALLOWANCE.perSide).toBe(0.9)
  expect(DISPLAY_VENDOR.active.tolerance).toBe(0.2)
  expect(glass.width).toBeCloseTo(105.42, 6)
  expect(glass.height).toBeCloseTo(67.07, 6)
  expect(cut.width).toBeCloseTo(glass.width + 2 * LCD_CUTOUT_ALLOWANCE.perSide, 6)
  expect(cut.height).toBeCloseTo(glass.height + 2 * LCD_CUTOUT_ALLOWANCE.perSide, 6)
  expect(cut.width).toBeCloseTo(107.22, 6)
  expect(cut.height).toBeCloseTo(68.87, 6)
  // The glass is larger than the printed bezel opening the STEP model also
  // carries (95.54 x 54.36 = the active area + 0.25 mm per side), so sizing the
  // window from the glass always exposes the whole active area.
  const bezel = DISPLAY_VENDOR.bezel
  expect(bezel.width).toBeCloseTo(DISPLAY_VENDOR.active.width + 0.5, 2)
  expect(bezel.height).toBeCloseTo(DISPLAY_VENDOR.active.height + 0.5, 2)
  expect(cut.width).toBeGreaterThan(DISPLAY_VENDOR.active.width)
  expect(cut.height).toBeGreaterThan(DISPLAY_VENDOR.active.height)
  // A routed inside corner cannot be sharp; the relief is explicit.
  expect(cut.cornerRadius).toBeGreaterThanOrEqual(0.5)
  expect(cut.cornerRadius).toBe(AUTOPROVISIONAL_CORNER)
})

test("the glass passes through the panel, flush with its front face", () => {
  // D1 sizing means the window clears the glass on every side, so the glass
  // front face sits at the panel front face (Assumption A, restored) and the
  // panel occludes nothing. The vertical fit works because the LED light pipes
  // moved beside their buttons (fallback 3): the free band is 77.50 mm against
  // a 68.87 mm window.
  expect(PROVISIONAL_Z.displayGlassFront).toBe(0)
  expect(PANEL_LCD_CUTOUT.width).toBeGreaterThan(DISPLAY_VENDOR.glass.width)
  expect(PANEL_LCD_CUTOUT.height).toBeGreaterThan(DISPLAY_VENDOR.glass.height)
  const windowReveal = (PANEL_LCD_CUTOUT.width - DISPLAY_VENDOR.glass.width) / 2
  expect(windowReveal).toBeCloseTo(LCD_CUTOUT_ALLOWANCE.perSide, 6)
  // The panel still has to be thicker than the gap it occludes nothing of: the
  // occluded border is the glass edge, forward of the module PCB rear face.
  expect(PROVISIONAL_Z.panelRear).toBeLessThan(PROVISIONAL_Z.displayPcbRear)
})

test("the aperture polygon keeps its bounds and the fit band is not exceeded", () => {
  const outline = roundedRectOutline(PANEL_LCD_CUTOUT)
  expect(outline).toHaveLength(4 * (6 + 1))
  const xs = outline.map(p => p.x)
  const ys = outline.map(p => p.y)
  expect(Math.max(...xs) - Math.min(...xs)).toBeCloseTo(PANEL_LCD_CUTOUT.width, 6)
  expect(Math.max(...ys) - Math.min(...ys)).toBeCloseTo(PANEL_LCD_CUTOUT.height, 6)
  // Chords lie INSIDE the true corner arc, so the emitted polygon is never
  // larger than the exact rounded rectangle.
  for (const p of outline) {
    const relX = Math.abs(p.x - PANEL_LCD_CUTOUT.x)
    const relY = Math.abs(p.y - PANEL_LCD_CUTOUT.y)
    expect(relX).toBeLessThanOrEqual(PANEL_LCD_CUTOUT.width / 2 + 1e-9)
    expect(relY).toBeLessThanOrEqual(PANEL_LCD_CUTOUT.height / 2 + 1e-9)
  }
})

test("LCD aperture stays clear of the panel edge and the control openings", () => {
  const cut = PANEL_LCD_CUTOUT
  expect(Math.abs(cut.x) + cut.width / 2).toBeLessThan(PANEL.width / 2 - 1)
  expect(Math.abs(cut.y) + cut.height / 2).toBeLessThan(PANEL.height / 2 - 1)
  // The window may not reach the transport-button row, and it must stay below
  // the feedthrough cutouts and below their labels.
  expect(cut.y - cut.height / 2).toBeGreaterThan(TRANSPORT_Y + PANEL_CUTOUTS.button / 2 + 2)
  expect(cut.y + cut.height / 2).toBeLessThan(USB_FEEDTHROUGHS[0]!.y - USB_FEEDTHROUGHS[0]!.cutoutHeight / 2)
  const feedthroughLabel = PANEL_LABELS.filter(l => /^(USB-C POWER|UMC AUDIO|MIDI HUB)$/.test(l.text))
  expect(feedthroughLabel).toHaveLength(USB_FEEDTHROUGHS.length)
  for (const l of feedthroughLabel) {
    expect(l.y - 1.8 / 2).toBeGreaterThan(cut.y + cut.height / 2 + 1)
  }
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
  expect(b.outline.height).toBeCloseTo(14, 6)
  expect(b.outline.x1).toBeGreaterThan(-PANEL.width / 2)
  expect(b.outline.x2).toBeLessThan(PANEL.width / 2)
  // Backed by the front board, and clear of the display below it.
  expect(b.outline.x1).toBeGreaterThan(-FRONT_PCB.width / 2)
  expect(b.outline.x2).toBeLessThan(FRONT_PCB.width / 2)
  expect(b.outline.y2).toBeLessThanOrEqual(FRONT_PCB.height / 2)
  // The bar is REAR-side, so the front-side display envelope does not set its
  // bottom: it sits 1.0 mm below the top spacer row, which is what the bar has
  // to carry the head-clearance slots for.
  const topSpacer = Math.max(...FRONT_REAR_MOUNT.filter(h => h.y > 0).map(h => h.y))
  expect(b.outline.y1).toBeCloseTo(topSpacer - BRACKET_ALLOWANCE.spacerHeadWall, 6)
  expect(b.outline.height).toBeCloseTo(b.outline.y2 - b.outline.y1, 6)
  for (const h of FRONT_REAR_MOUNT) {
    if (h.y > 0) expect(h.y).toBeGreaterThan(b.outline.y1)
  }
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
test("provisional Z stack follows the vendor side view, with a stated datum", () => {
  const v = DISPLAY_VENDOR
  expect(PROVISIONAL_Z.panelFront).toBe(0)
  expect(PROVISIONAL_Z.panelRear).toBe(PANEL.thickness)
  // Assumption A: the glass front sits flush with the panel front face, which
  // the glass-derived aperture allows.
  expect(PROVISIONAL_Z.displayGlassFront).toBe(0)
  // 14.05 mm overall (wiki FAQ), split PROVISIONALLY into 10.05 glass front ->
  // PCB rear plus a 4.00 mm boss projection. The split is unmeasured and the
  // two quantities stay named separately so they cannot be conflated.
  expect(v.glassFrontToPcbRear).toBe(10.05)
  expect(v.pcbRearToBossEnd).toBe(4.0)
  expect(v.overallDepth).toBeCloseTo(14.05, 6)
  expect(v.overallDepth).toBeCloseTo(v.glassFrontToPcbRear + v.pcbRearToBossEnd, 6)
  expect(PROVISIONAL_Z.displayPcbRear).toBeCloseTo(10.05, 6)
  expect(PROVISIONAL_Z.displayRearMost).toBeCloseTo(14.05, 6)
  expect("rearEnvelopeDepth" in DISPLAY_STACK).toBe(false)
  // Plane separation and spacer length are separate quantities.
  expect(DISPLAY_STANDOFF.planeSeparation).toBe(9)
  expect(DISPLAY_STANDOFF.spacerLength).toBeCloseTo(5, 6)
  expect(DISPLAY_STANDOFF.spacerLength).toBeCloseTo(
    DISPLAY_STANDOFF.planeSeparation - v.pcbRearToBossEnd, 6)
  expect(DISPLAY_STACK.bossProjection).toBe(4)
  expect(PROVISIONAL_Z.frontBoardTop).toBeCloseTo(
    v.glassFrontToPcbRear + DISPLAY_STANDOFF.planeSeparation + DISPLAY_GLASS_FRONT_Z, 6)
  expect(PROVISIONAL_Z.frontBoardTop).toBeCloseTo(19.05, 6)
  expect(PROVISIONAL_Z.frontBoardRear).toBeCloseTo(20.65, 6)
  // Provisional-with-numbers: 19.05 - 1.60 = 17.45 mm, up from 14.10 mm. The
  // panel-to-board gap is a consequence of the design target, not evidence.
  expect(PANEL_TO_FRONT_BOARD_GAP).toBeCloseTo(17.45, 6)
  expect(PANEL_TO_FRONT_BOARD_GAP).toBeCloseTo(PROVISIONAL_Z.frontBoardTop - PROVISIONAL_Z.panelRear, 6)
  // The installed-depth chain still passes the owner's 110 mm target.
  expect(provisionalInstalledDepth()).toBeLessThan(DEPTH_TARGET)
})

test("display underside gap and light-pipe reach stay pending, not claimed", () => {
  // The stack leaves 5.0 mm from the front-board top face to the rear-most
  // module point: 2.8 mm over the tallest part with a stated height (MIDI
  // receiver in the Toshiba 11-4L1S body, 2.2 mm max installed). That still does
  // NOT prove the display underside, whose component map, FPC latch and module
  // tolerance are unmapped.
  expect(DISPLAY_UNDERSIDE_GAP).toBeCloseTo(PROVISIONAL_Z.frontBoardTop - PROVISIONAL_Z.displayRearMost, 6)
  expect(DISPLAY_UNDERSIDE_GAP).toBeCloseTo(5.0, 6)
  expect(FRONT_TOP_SMD_MAX_HEIGHT.height).toBe(2.2)
  expect(DISPLAY_UNDERSIDE_GAP - FRONT_TOP_SMD_MAX_HEIGHT.height).toBeCloseTo(2.8, 6)
  // Light-pipe reach is unproven and now far worse: a 10.16 mm pipe against a
  // 17.45 mm gap leaves 7.29 mm unexplained, so a longer pipe (or a different
  // optical path) is required. No LED height and no seat allowance is stored.
  expect(LIGHT_PIPE.length).toBe(10.16)
  expect(LIGHT_PIPE_REACH.unaccounted).toBeCloseTo(PANEL_TO_FRONT_BOARD_GAP - LIGHT_PIPE.length, 6)
  expect(LIGHT_PIPE_REACH.unaccounted).toBeCloseTo(7.29, 6)
  expect("seatAllowance" in LIGHT_PIPE).toBe(false)
})

test("provisional depth chain stays inside the owner target", () => {
  expect(DEPTH_TARGET).toBe(110)
  expect(provisionalInstalledDepth()).toBeLessThanOrEqual(DEPTH_TARGET)
  // Rear board overhang is 13.2 mm per side when both boards share a centre:
  // the front board stays 128 mm (D2), so no extra width was bought for the
  // 106 mm-wide module.
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

test("display patterns follow the drawing's asymmetric insets, never a mirrored pitch", () => {
  // Drawing Rev2.2 inner pattern: 25.62 mm from the LEFT edge and 22.38 mm
  // from the RIGHT (58.00 mm apart), 9.50 mm from the TOP and 9.46 mm from
  // the BOTTOM (49.04 mm apart). Both pairs are derived from the edge insets,
  // so the top/bottom asymmetry survives: y is +24.50 at the top and -24.54
  // at the bottom, NOT +-24.50.
  expect(DISPLAY_STANDOFFS.map(h => h.x)).toEqual([-27.38, 30.62, -27.38, 30.62])
  expect(DISPLAY_STANDOFFS.map(h => h.y)).toEqual([24.5, 24.5, -24.54, -24.54])
  const d = DISPLAY_CONCEPT
  expect(d.x - d.width / 2 + 25.62).toBeCloseTo(d.x - 27.38, 6)
  expect(d.x + d.width / 2 - 22.38).toBeCloseTo(d.x + 30.62, 6)
  expect(d.y + d.height / 2 - 9.5).toBeCloseTo(d.y + 24.5, 6)
  expect(d.y + d.height / 2 - 58.54).toBeCloseTo(d.y - 24.54, 6)
  // The fitted outer pattern (99.30 x 61.80, 3.35 / 3.10 insets): the
  // reference STEP names its corner features `SMTSO-M2_5-4ET`, M2.5 female
  // threads, so these are the mounting centres the front board carries.
  expect(DISPLAY_BOSS_PATTERN.map(h => h.x)).toEqual([-49.65, 49.65, -49.65, 49.65])
  expect(DISPLAY_BOSS_PATTERN.map(h => h.y)).toEqual([30.9, 30.9, -30.9, -30.9])
  expect(DISPLAY_BOSS_PATTERN[1]!.x - DISPLAY_BOSS_PATTERN[0]!.x).toBeCloseTo(99.3, 6)
  expect(DISPLAY_BOSS_PATTERN[0]!.y - DISPLAY_BOSS_PATTERN[2]!.y).toBeCloseTo(61.8, 6)
  for (const h of LCD_MOUNT_HOLES) {
    expect(Math.abs(h.x)).toBeLessThan(FRONT_PCB.width / 2 - 2)
    expect(Math.abs(h.y)).toBeLessThan(FRONT_PCB.height / 2 - 2)
  }
  // Physical first article is still pending.
  expect(DISPLAY_DATUM.firstArticlePending).toBe(true)
})

test("the module fits the front board and clears the owner-fixed control column", () => {
  const d = DISPLAY_CONCEPT
  // The board stays 128 mm (owner decision D2) and the module keeps a 1.5 mm
  // ligament on the left because the value knob moved 47 -> 52.
  expect(FRONT_PCB.width).toBe(128)
  expect(d.x - d.width / 2).toBe((-FRONT_PCB.width / 2) + 1.5)
  expect(RIGHT_COLUMN.x).toBe(52)
  expect((RIGHT_CONTROLS.TEMPO.x - 6.25) - (d.x + d.width / 2)).toBeCloseTo(2.25, 6)
  // The rail-hole edge and the window LEFT edge are separated in X only; they
  // sit at different Y, so the x projection is NOT a web. The real 2D distance
  // is measured from the emitted aperture polygon in the rail test below.
  const railHoleEdge = Math.abs(RAIL_HOLES[0]!.x) - RAIL_HOLE_DIAMETER / 2
  expect(railHoleEdge).toBeCloseTo(66.98, 6)
  const apertureLeftEdge = PANEL_LCD_CUTOUT.x - PANEL_LCD_CUTOUT.width / 2
  expect(railHoleEdge + apertureLeftEdge).toBeCloseTo(3.87, 2)
  expect(FRONT_PCB.width / 2 - (RIGHT_COLUMN.x + 6.25)).toBeCloseTo(5.75, 6)
})

test("the 30HP rail grid is on the 5.08 mm pitch and inside the panel", () => {
  // Doepfer A-100 rule (https://doepfer.de/a100_man/a100m_e.htm): rail-hole
  // spacing must be a multiple of 5.08 mm.
  const PITCH = 5.08
  const xs = [...new Set(RAIL_HOLES.map(h => h.x))].sort((a, b) => a - b)
  const ys = [...new Set(RAIL_HOLES.map(h => h.y))].sort((a, b) => a - b)
  expect(xs).toEqual([-68.58, 68.58])
  expect(ys).toEqual([-61.25, 61.25])
  const spacing = xs[1]! - xs[0]!
  expect(spacing).toBeCloseTo(137.16, 6)
  expect(spacing / PITCH).toBeCloseTo(27, 9)
  expect(Math.abs(spacing / PITCH - Math.round(spacing / PITCH))).toBeLessThan(1e-9)
  // The replaced 32HP grid was 147.56 mm = 29 x 5.08 + 0.24, i.e. off-grid.
  // Recorded as off-grid; this is NOT a claim that those holes fail the nuts.
  const replacedSpacing = 147.56
  expect(replacedSpacing / PITCH).toBeCloseTo(29.047, 3)
  expect(Math.abs(replacedSpacing / PITCH - Math.round(replacedSpacing / PITCH)))
    .toBeGreaterThan(0.04)
  // 152.0 mm panel: 7.42 mm centre inset and 5.82 mm hole edge to panel edge.
  expect(PANEL.width / 2 - Math.abs(xs[0]!)).toBeCloseTo(7.42, 6)
  expect(PANEL.width / 2 - (Math.abs(xs[0]!) + RAIL_HOLE_DIAMETER / 2)).toBeCloseTo(5.82, 6)
  // The Doepfer hole figure (A100FP.gif) draws a 7.5 mm horizontal inset; the
  // fixed grid on our estimated 152.0 mm width is 0.08 mm tighter. Recorded, not
  // corrected: neither value is a fitted measurement.
  const figureInset = 7.5
  const ourInset = PANEL.width / 2 - Math.abs(xs[0]!)
  expect(figureInset - ourInset).toBeCloseTo(0.08, 6)
  // The figure's vertical inset and hole size are what we use.
  expect(PANEL.height).toBe(128.5)
  // the figure's 3.0 mm is a CENTRE inset, like its 7.5 mm horizontal one
  expect(PANEL.height / 2 - Math.abs(ys[0]!)).toBeCloseTo(3.0, 6)
  expect(RAIL_HOLE_DIAMETER).toBe(3.2)
})

test("the panel width keeps the rail grid and a printable edge", () => {
  // Band from geometry: 5.0 mm of material outside each rail hole, up to the
  // 30HP nominal width 30 x 5.08 = 152.4 mm. 152.0 sits inside it.
  const halfSpan = Math.abs(RAIL_HOLES[0]!.x) + RAIL_HOLE_DIAMETER / 2
  const minWidth = 2 * (halfSpan + 5.0)
  const nominalWidth = 30 * 5.08
  expect(minWidth).toBeCloseTo(150.36, 6)
  expect(nominalWidth).toBeCloseTo(152.4, 6)
  expect(PANEL.width).toBeGreaterThanOrEqual(minWidth)
  expect(PANEL.width).toBeLessThanOrEqual(nominalWidth)
  // Mutations the contract rejects: too narrow, and wider than the nominal.
  expect(150.0).toBeLessThan(minWidth)
  expect(152.56).toBeGreaterThan(nominalWidth)
  for (const h of RAIL_HOLES) {
    expect(Math.abs(h.x) + RAIL_HOLE_DIAMETER / 2).toBeLessThan(PANEL.width / 2)
    expect(Math.abs(h.y) + RAIL_HOLE_DIAMETER / 2).toBeLessThan(PANEL.height / 2)
  }
})

test("the LCD aperture to rail-hole clearance is measured in two dimensions", () => {
  // The rail hole and the window LEFT edge differ by 3.87 mm in X only, and the
  // two features sit at different Y. That projection is NOT a web and NOT a
  // shortest clearance; the real distance comes from the emitted polygon.
  const aperture = roundedRectOutline(PANEL_LCD_CUTOUT)
  const edges = RAIL_HOLES.map(h => polygonDistance(h.x, h.y, aperture) - RAIL_HOLE_DIAMETER / 2)
  const nearest = Math.min(...edges)
  // Measured from the polygon: the upper-left rail hole is the closest one.
  expect(nearest).toBeCloseTo(15.423, 2)
  const railEdge = Math.abs(RAIL_HOLES[0]!.x) - RAIL_HOLE_DIAMETER / 2
  const windowLeftEdge = PANEL_LCD_CUTOUT.x - PANEL_LCD_CUTOUT.width / 2
  const projection = railEdge + windowLeftEdge
  expect(railEdge).toBeCloseTo(66.98, 6)
  expect(windowLeftEdge).toBeCloseTo(-63.11, 6)
  expect(projection).toBeCloseTo(3.87, 2)
  // The 2D figure is an order of magnitude larger than the projection: the two
  // features are far apart in Y, which is why the projection is meaningless.
  expect(nearest).toBeGreaterThan(projection + 10)
})

test("the panel-edge and rail-hole distances quoted in the docs are canonical", () => {
  // docs/mechanical-stack.md ("Display window, owner decision D1") quotes these
  // numbers. The two rail-hole figures are X PROJECTIONS at a different Y; the
  // 2D figures are reported beside them and both are asserted here, so the
  // paragraph cannot drift again.
  const r = RAIL_HOLE_DIAMETER / 2
  const panelEdge = PANEL.width / 2
  const railEdge = Math.abs(RAIL_HOLES[0]!.x) - r
  const windowLeft = PANEL_LCD_CUTOUT.x - PANEL_LCD_CUTOUT.width / 2
  const aperture = roundedRectOutline(PANEL_LCD_CUTOUT)
  // perpendicular to the vertical panel edge: a true 2D distance
  expect(panelEdge + windowLeft).toBeCloseTo(12.89, 2)
  // X projection only (the rail hole is at a different Y)
  expect(railEdge + windowLeft).toBeCloseTo(3.87, 2)
  // true 2D distance from the emitted polygon to the nearest rail-hole edge
  const nearest = Math.min(...RAIL_HOLES.map(h =>
    polygonDistance(h.x, h.y, aperture) - r))
  expect(nearest).toBeCloseTo(15.423, 2)
  // value knob body (Ø12.5 at x = 52, y = 30)
  const knobX = RIGHT_CONTROLS.TEMPO.x
  const knobY = RIGHT_CONTROLS.TEMPO.y
  const knobRight = knobX + 6.25
  expect(panelEdge - knobRight).toBeCloseTo(17.75, 2)
  expect(railEdge - knobRight).toBeCloseTo(8.73, 2)
  const knobToRail = Math.min(...RAIL_HOLES.map(h =>
    Math.hypot(knobX - h.x, knobY - h.y) - 6.25 - r))
  expect(knobToRail).toBeCloseTo(27.526, 2)
  // the projections are strictly smaller than the 2D figures, which is why the
  // paragraph must never call them webs
  expect(nearest).toBeGreaterThan(railEdge + windowLeft)
  expect(knobToRail).toBeGreaterThan(railEdge - knobRight)
})

test("the standoff-hole to rail-hole web is measured from the canon data", () => {
  let best = Infinity
  for (const s of FRONT_BOARD_HOLES) {
    for (const r of RAIL_HOLES) {
      best = Math.min(best, Math.hypot(s.x - r.x, s.y - r.y))
    }
  }
  // Nearest pair: standoff (-61, -53) and rail (-68.58, -61.25).
  expect(best).toBeCloseTo(11.204, 3)
  const web = best - MOUNT_HOLE_DIAMETER / 2 - RAIL_HOLE_DIAMETER / 2
  expect(web).toBeCloseTo(8.254, 3)
  // Camera-heads/nuts are NOT sized here: the hardware envelope is unmeasured
  // and stays a pending physical check (see docs/mechanical-stack.md).
  expect(web).toBeGreaterThan(0)
})

test("the aperture clears the transport row and the feedthrough label row", () => {
  // These are the two rows the glass-derived window has to live between. It
  // only fits because the LED light pipes moved beside their buttons: at
  // LED_Y = -24 the window bottom (-23.435) would have crossed the LED opening
  // top (-22.0) by 1.435 mm. A future enlargement must fail here rather than
  // silently overlap a control or a label.
  const cut = PANEL_LCD_CUTOUT
  expect(cut.y - cut.height / 2).toBeCloseTo(-23.435, 3)
  expect(cut.y - cut.height / 2).toBeGreaterThan(LED_Y + PANEL_CUTOUTS.led / 2)
  const buttonTop = TRANSPORT_Y + PANEL_CUTOUTS.button / 2
  expect(cut.y - cut.height / 2 - buttonTop).toBeCloseTo(4.365, 3)
  // The LED row is now on the transport centreline, so the whole row below the
  // window is the button row; the light pipes sit beside the buttons, not
  // between them and the window.
  expect(LED_Y).toBe(TRANSPORT_Y)
  // Feedthrough labels sit 6.0 mm below their cutouts (layout LABEL_OFFSET).
  const labelBottom = USB_FEEDTHROUGHS[0]!.y - 6.0 - 1.8 / 2
  expect(labelBottom - (cut.y + cut.height / 2)).toBeCloseTo(1.165, 2)
})

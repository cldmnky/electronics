import { expect, test } from "bun:test"
import {
  DISPLAY_CONCEPT,
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
import { expect, test } from "bun:test"
import {
  MIN_GLYPH_CLEARANCE,
  MIN_STROKE_WIDTH,
  SILK_EDGE_MARGIN,
  WAVE_AMPLITUDE,
  WAVE_GAP,
  WAVE_LENGTH,
  WAVE_STROKE,
  WORDMARK_CAP,
  WORDMARK_STROKE,
  WORDMARK_TEXT,
  brandArtPaths,
  brandBounds,
  minGlyphClearance,
} from "./panel-art"
import { FRONT_COLUMNS, JACK_Y, OUT_RING, PANEL, TRANSPORT_Y } from "./layout"

test("mnkybox wordmark ink stays on the panel", () => {
  const b = brandBounds(0, -59)
  expect(b.minX).toBeGreaterThan(-PANEL.width / 2 + SILK_EDGE_MARGIN)
  expect(b.maxX).toBeLessThan(PANEL.width / 2 - SILK_EDGE_MARGIN)
  expect(b.minY).toBeGreaterThan(-PANEL.height / 2 + SILK_EDGE_MARGIN)
  expect(b.maxY).toBeLessThan(PANEL.height / 2 - SILK_EDGE_MARGIN)
  // Strokes print at or above the JLCPCB floor.
  for (const g of brandArtPaths(0, -59)) {
    expect(g.stroke).toBeGreaterThanOrEqual(MIN_STROKE_WIDTH)
  }
})

test("waveform flanks are symmetric and clear the wordmark", () => {
  const groups = brandArtPaths(0, -59)
  expect(groups).toHaveLength(2)
  const waves = groups[1]!.paths
  expect(waves).toHaveLength(2)
  const [left, right] = [waves[0]!, waves[1]!]
  const lx = left.map(p => p.x), rx = right.map(p => p.x)
  // Mirror images about x = 0.
  expect(Math.min(...lx)).toBeCloseTo(-Math.max(...rx), 9)
  expect(Math.max(...lx)).toBeCloseTo(-Math.min(...rx), 9)
  // Inner start clears the wordmark ink, outer end stays in the bottom strip.
  const wordXs = groups[0]!.paths.flatMap(p => p.map(q => q.x))
  const wordHalf = (Math.max(...wordXs) - Math.min(...wordXs)) / 2
  expect(-Math.max(...lx)).toBeGreaterThanOrEqual(wordHalf + WAVE_GAP - 0.5)
  expect(Math.max(...rx)).toBeLessThanOrEqual(40)
  // Waveform runs sine -> triangle -> square: midline crossings plus peaks.
  const ly = left.map(p => p.y)
  expect(Math.max(...ly)).toBeCloseTo(-59 + WAVE_AMPLITUDE, 6)
  expect(Math.min(...ly)).toBeCloseTo(-59 - WAVE_AMPLITUDE, 6)
  expect(WAVE_STROKE).toBeGreaterThanOrEqual(MIN_STROKE_WIDTH)
  expect(WORDMARK_STROKE).toBeGreaterThanOrEqual(MIN_STROKE_WIDTH)
  expect(WORDMARK_TEXT).toBe("mnkybox")
  expect(WAVE_LENGTH).toBe(18)
})

test("wordmark glyphs do not touch", () => {
  expect(minGlyphClearance(WORDMARK_TEXT, WORDMARK_CAP, WORDMARK_STROKE))
    .toBeGreaterThan(MIN_GLYPH_CLEARANCE)
})

test("OUT rings clear barrels, buttons and labels", () => {
  const outer = OUT_RING.radius + OUT_RING.stroke / 2
  const inner = OUT_RING.radius - OUT_RING.stroke / 2
  expect(inner - 6.2 / 2).toBeGreaterThanOrEqual(0.3) // web to barrel hole
  const outs = FRONT_COLUMNS.filter(c => c.jack.startsWith("MIDI OUT"))
  expect(outs).toHaveLength(2)
  const top = JACK_Y + outer
  const bottom = JACK_Y - outer
  // Transport button cutouts above (r6.2 at TRANSPORT_Y).
  expect(TRANSPORT_Y - 6.2 - top).toBeGreaterThanOrEqual(0.3)
  // OUT labels below (1.8 mm text centred at JACK_Y - 6.5).
  expect(bottom - (JACK_Y - 6.5 - 0.9)).toBeGreaterThanOrEqual(0.3)
})

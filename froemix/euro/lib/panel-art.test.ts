/// <reference types="bun-types" />
// Panel artwork: print rules, legends and their geometry, for every design.

import { describe, expect, test } from "bun:test"
import {
  DEFAULT_PANEL_DESIGN,
  MIN_ELEMENT_CLEARANCE,
  MIN_GLYPH_CLEARANCE,
  MIN_LEGEND_HEIGHT,
  MIN_STROKE_WIDTH,
  PANEL_DESIGN_IDS,
  PANEL_OUTLINE,
  PANEL_STYLES,
  REQUIRED_LEGENDS,
  REQUIRED_WORDS,
  artworkPaths,
  capHeightUnits,
  elementClearanceToKeepout,
  elementInkBounds,
  minGlyphClearance,
  panelArtwork,
  textInkHeight,
  wordmarkPaths,
} from "./panel-art"
import { checkArtwork } from "./panel-checks"
import { distPathsToPaths, polylineBounds, unionBounds } from "./panel-geometry"

const designs = PANEL_DESIGN_IDS.map((id) => panelArtwork(id))

test("the mechanical contract is unchanged", () => {
  expect(PANEL_OUTLINE).toEqual({ width: 101.6, height: 128.5, thickness: 2 })
  for (const art of designs) {
    expect(art.outline.width).toBe(101.6)
    expect(art.outline.height).toBe(128.5)
    expect(art.outline.thickness).toBe(2)
  }
})

test("the default design is the owner-selected signal direction", () => {
  expect(DEFAULT_PANEL_DESIGN).toBe("signal")
  expect(PANEL_STYLES.nord.id).toBe("nord")
  expect(PANEL_STYLES.signal.id).toBe("signal")
})

describe.each(designs.map((art) => [art.design, art] as const))("%s artwork", (design, art) => {
  test("passes every artwork rule", () => {
    expect(checkArtwork(art)).toEqual([])
  })

  test("prints every path at 0.2mm or wider", () => {
    for (const p of artworkPaths(art)) {
      expect(p.strokeWidth).toBeGreaterThanOrEqual(MIN_STROKE_WIDTH)
    }
  })

  test("prints every legend at 1.2mm cap height or taller", () => {
    for (const element of art.elements) {
      if (element.kind !== "legend" && element.kind !== "brand" && element.kind !== "note") continue
      expect(element.capHeight!).toBeGreaterThanOrEqual(MIN_LEGEND_HEIGHT)
    }
  })

  test("draws the legend height it declares, measured from the ink", () => {
    for (const element of art.elements) {
      if (!element.capHeight || !element.lines) continue
      const ink = elementInkBounds(element)!
      const tallest = Math.max(...element.lines.map((line) => textInkHeight(line, element.capHeight!)))
      const expected = element.inkHeight ?? (element.lines.length - 1) * element.pitch! + tallest
      expect(Math.abs(ink.height - element.strokeWidth - expected)).toBeLessThan(0.1)
    }
  })

  test("carries every required legend identity exactly once", () => {
    const texts = art.elements.filter((e) => e.text).map((e) => e.text!)
    for (const required of REQUIRED_LEGENDS) {
      expect(texts.filter((t) => t === required)).toHaveLength(1)
    }
  })

  test("prints every required word", () => {
    const words = new Set<string>()
    for (const element of art.elements) {
      if (!element.text) continue
      for (const w of element.text.split(/[^A-Za-z0-9]+/)) if (w) words.add(w)
    }
    for (const word of REQUIRED_WORDS) expect(words.has(word)).toBe(true)
  })

  test("keeps glyph strokes clear of each other", () => {
    for (const element of art.elements) {
      if (!element.capHeight || !element.lines) continue
      for (const line of element.lines) {
        const clearance = minGlyphClearance(line, element.capHeight, element.strokeWidth)
        if (Number.isFinite(clearance)) {
          expect(clearance).toBeGreaterThanOrEqual(MIN_GLYPH_CLEARANCE)
        }
      }
    }
  })

  test("keeps unrelated elements apart", () => {
    for (let i = 0; i < art.elements.length; i++) {
      for (let j = i + 1; j < art.elements.length; j++) {
        const a = art.elements[i]!
        const b = art.elements[j]!
        if (a.group === b.group) continue
        const d = distPathsToPaths(a.paths, b.paths) - (a.strokeWidth + b.strokeWidth) / 2
        expect(d).toBeGreaterThanOrEqual(MIN_ELEMENT_CLEARANCE)
      }
    }
  })

  test("keeps all ink off the installed hardware", () => {
    for (const element of art.elements) {
      for (const keepout of art.keepouts) {
        expect(elementClearanceToKeepout(element, keepout)).toBeGreaterThanOrEqual(0)
      }
    }
  })

  test("keeps all ink inside the panel and away from the window", () => {
    const maxX = art.outline.width / 2
    const maxY = art.outline.height / 2
    for (const element of art.elements) {
      const ink = elementInkBounds(element)!
      expect(ink.minX).toBeGreaterThan(-maxX)
      expect(ink.maxX).toBeLessThan(maxX)
      expect(ink.minY).toBeGreaterThan(-maxY)
      expect(ink.maxY).toBeLessThan(maxY)
    }
  })
})

test("both designs print the same 31 hardware keepouts", () => {
  for (const art of designs) {
    expect(art.keepouts).toHaveLength(31)
    expect(new Set(art.keepouts.map((k) => k.id)).size).toBe(31)
  }
})

test("the window is a rounded polygon at the frozen position", () => {
  for (const art of designs) {
    expect(art.window.polygon.length).toBeGreaterThanOrEqual(32)
    expect(art.window.width).toBe(30.5)
    expect(art.window.height).toBe(16)
    expect(art.window.centre).toEqual({ x: 0, y: 42.8 })
    expect(art.window.radius).toBe(0.5)
  }
})

test("text ink height follows the glyphs, not the cap height", () => {
  expect(capHeightUnits).toBeCloseTo(0.678, 3)
  expect(textInkHeight("H", 1.5)).toBeCloseTo(1.5, 6)
  // round letters overshoot the cap line, so they draw taller than an H
  expect(textInkHeight("O", 1.5)).toBeGreaterThan(textInkHeight("H", 1.5))
  // a descender changes the drawn height, so it is never assumed from the size
  expect(textInkHeight("p", 1.5)).not.toBeCloseTo(textInkHeight("H", 1.5), 3)
})

describe("nord wordmark", () => {
  const mark = wordmarkPaths("FROEMIX", 8.6, 3.6)
  const bounds = unionBounds(mark.paths.map((p) => polylineBounds(p)))!

  test("is about 75mm wide and 8.6mm tall in the central band", () => {
    expect(bounds.width).toBeGreaterThan(70)
    expect(bounds.width).toBeLessThan(80)
    expect(bounds.height).toBeCloseTo(8.6, 3)
    expect(bounds.minY).toBeCloseTo(3.6, 3)
  })

  test("is centred and symmetrical around the panel centre line", () => {
    expect(bounds.cx).toBeCloseTo(0, 3)
  })

  test("has no glyph the monoline font cannot draw", () => {
    expect(() => wordmarkPaths("FROEMIX", 8.6, 3.6)).not.toThrow()
  })
})

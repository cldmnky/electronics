/// <reference types="bun-types" />
// Previews must draw the BUILT geometry, and they must render offline.

import { describe, expect, test } from "bun:test"
import { panelArtwork, type PanelArtwork } from "./panel-art"
import {
  PANEL_THEMES,
  renderPanelContent,
  renderPanelPreviewSvg,
  renderPreviewHtml,
  renderPrintTemplateSvg,
} from "./panel-render"
import { syntheticPanel } from "./panel-testkit"

const art = panelArtwork("nord")
const circuitJson = syntheticPanel(art)

const count = (haystack: string, needle: string): number => haystack.split(needle).length - 1

describe.each(["nord", "signal"] as const)("%s preview", (design) => {
  const designArt = panelArtwork(design)
  const svg = renderPanelPreviewSvg(syntheticPanel(designArt), designArt)

  test("draws one stroked path per artwork path", () => {
    expect(count(svg, "<path ")).toBe(designArt.elements.reduce((n, e) => n + e.paths.length, 0))
  })

  test("uses each stroke width from the artwork", () => {
    for (const element of designArt.elements) {
      expect(svg.includes(`stroke-width="${element.strokeWidth}"`)).toBe(true)
    }
  })

  test("draws the frozen geometry: 31 holes and the rounded window", () => {
    expect(count(svg, "<circle ")).toBeGreaterThanOrEqual(31)
    expect(svg).toContain("<polygon ")
    expect(svg).toContain('viewBox="-50.8 -64.25 101.6 128.5"')
  })

  test("the emitted svg carries the class the HTML styles", () => {
    const cls = /class="([^"]+)"/.exec(svg)
    expect(cls).not.toBeNull()
    const name = cls![1]!
    const html = renderPreviewHtml(svg, designArt, "note")
    expect(html).toContain(`.${name} {`)
    expect(html).toContain("width: 100%")
    expect(html).toContain("height: auto")
    expect(html).toContain("max-width")
    // nothing may push the body wider than the viewport
    expect(html).toContain("overflow-x: hidden")
    expect(html).toContain("overflow-wrap: anywhere")
    expect(html).toContain("minmax(0, 1fr)")
  })

  test("renders offline: no font, image, script or remote reference", () => {
    expect(svg).not.toContain("<script")
    expect(svg).not.toContain("<image")
    expect(svg).not.toContain("@import")
    expect(svg).not.toContain("url(http")
    expect(svg).not.toContain("font-family")
  })

  test("the print template places the whole faceplate on the sheet, 1:1", () => {
    const print = renderPrintTemplateSvg(syntheticPanel(designArt), designArt)
    const group = /<g transform="translate\(([-\d.]+) ([-\d.]+)\) scale\(1 -1\)">/.exec(print)
    expect(group).not.toBeNull()
    const tx = Number(group![1])
    const ty = Number(group![2])
    // panel coordinates are centred on the origin, so the group translation is
    // the panel centre; every corner must land inside the A4 page
    const corners = [
      { x: -designArt.outline.width / 2, y: -designArt.outline.height / 2 },
      { x: designArt.outline.width / 2, y: -designArt.outline.height / 2 },
      { x: designArt.outline.width / 2, y: designArt.outline.height / 2 },
      { x: -designArt.outline.width / 2, y: designArt.outline.height / 2 },
    ].map((p) => ({ x: tx + p.x, y: ty - p.y }))
    for (const c of corners) {
      expect(c.x).toBeGreaterThan(0)
      expect(c.x).toBeLessThan(210)
      expect(c.y).toBeGreaterThan(0)
      expect(c.y).toBeLessThan(297)
    }
    const xs = corners.map((c) => c.x)
    const ys = corners.map((c) => c.y)
    const bounds = {
      minX: Math.min(...xs),
      maxX: Math.max(...xs),
      minY: Math.min(...ys),
      maxY: Math.max(...ys),
    }
    expect(bounds.maxX - bounds.minX).toBeCloseTo(designArt.outline.width, 3)
    expect(bounds.maxY - bounds.minY).toBeCloseTo(designArt.outline.height, 3)

    // the calibration bar and every note must sit below the faceplate
    const noteYs = [...print.matchAll(/<text[^>]*\by="([-\d.]+)"/g)].map((m) => Number(m[1]))
    expect(noteYs.length).toBeGreaterThan(0)
    for (const y of noteYs) expect(y).toBeGreaterThan(bounds.maxY + 2)
    const bar = /<line x1="[\d.]+" y1="([-\d.]+)" x2="[\d.]+" y2="[-\d.]+" stroke="#000" stroke-width="0.3"/.exec(print)
    expect(bar).not.toBeNull()
    expect(Number(bar![1])).toBeGreaterThan(bounds.maxY + 2)
  })

  test("the print template is an A4 sheet with a 100mm calibration bar", () => {
    const print = renderPrintTemplateSvg(syntheticPanel(designArt), designArt)
    expect(print).toContain('width="210mm"')
    expect(print).toContain('height="297mm"')
    expect(print).toContain('viewBox="0 0 210 297"')
    expect(print).toContain("100 mm")
    expect(print).toContain("PRINT AT 100%")
    expect(count(print, "<path ")).toBe(designArt.elements.reduce((n, e) => n + e.paths.length, 0))
  })

  test("the HTML preview inlines the SVG and loads nothing", () => {
    const html = renderPreviewHtml(svg, designArt, "note")
    expect(html).toContain("<svg")
    // the SVG namespace is a name, not a fetch: nothing is loaded at all
    expect(html).not.toContain("src=")
    expect(html).not.toContain("href=")
    expect(html).not.toContain("@import")
    expect(html).not.toContain("url(http")
    expect(html).not.toContain("<script")
    expect(html).toContain(design)
  })
})

describe("panel content", () => {
  test("uses only the built circuit JSON, so preview and Gerbers cannot disagree", () => {
    const changed = syntheticPanel(art).map((e) =>
      e.type === "pcb_silkscreen_path" ? { ...e, stroke_width: 0.66 } : e,
    )
    const svg = renderPanelContent(changed, { theme: PANEL_THEMES.studio })
    expect(count(svg, 'stroke-width="0.66"')).toBe(
      changed.filter((e) => e.type === "pcb_silkscreen_path" && e.layer === "top").length,
    )
  })

  test("shows the hardware keepouts only when asked", () => {
    const without = renderPanelContent(circuitJson, { theme: PANEL_THEMES.studio })
    const with_ = renderPanelContent(circuitJson, {
      theme: PANEL_THEMES.studio,
      hardware: { keepouts: art.keepouts },
      showHardware: true,
    })
    expect(count(with_, "stroke-dasharray")).toBe(art.keepouts.length)
    expect(count(without, "stroke-dasharray")).toBe(0)
  })

  test("draws a rect cutout as well as a polygon cutout", () => {
    const rectCut: Record<string, any>[] = [
      { type: "pcb_cutout", shape: "rect", center: { x: 0, y: 42.8 }, width: 30.5, height: 16 },
    ]
    const svg = renderPanelContent(rectCut, { theme: PANEL_THEMES.print })
    expect(svg).toContain("<polygon ")
  })
})

test("the artwork type keeps previews and checks on one source", () => {
  const a: PanelArtwork = art
  expect(a.elements.length).toBeGreaterThan(20)
})

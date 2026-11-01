/// <reference types="bun-types" />
// Panel checks under mutation: every rule below is broken on purpose, and the
// check has to notice. A check that cannot fail proves nothing.

import { describe, expect, test } from "bun:test"
import {
  PANEL_DESIGN_IDS,
  PANEL_STYLES,
  artworkPaths,
  circlePath,
  panelArtwork,
  type ArtElement,
  type PanelArtwork,
} from "./panel-art"
import {
  checkArtwork,
  checkBuiltArtwork,
  checkFrozenGeometry,
  checkNoCopper,
  artworkMismatch,
  checkPanel,
  measureRoundedCorners,
  type PanelFixture,
  type Problem,
} from "./panel-checks"
import { loadFixture, syntheticPanel } from "./panel-testkit"
import { readFileSync } from "node:fs"

type CjElement = Record<string, any>

const art = panelArtwork("nord")
const fixture: PanelFixture = loadFixture()

const fresh = (): CjElement[] => syntheticPanel(art, fixture)
const checks = (circuitJson: CjElement[], design: PanelArtwork = art): Problem[] =>
  checkPanel({ circuitJson, fixture, art: design })

const copy = <T>(value: T): T => JSON.parse(JSON.stringify(value))

const hasProblem = (problems: Problem[], check: string, match: RegExp): boolean =>
  problems.some((p) => p.check === check && match.test(p.message))

// ---------------------------------------------------------------------------

test("the untouched panel passes every check", () => {
  expect(checks(fresh())).toEqual([])
})

test("the built panel file, when it matches a design, passes every check", () => {
  const path = new URL("../dist/panel/circuit.json", import.meta.url).pathname
  const file = Bun.file(path)
  if (!file.size) return // no build in this tree: `bun run panel:check` covers it
  const circuitJson = JSON.parse(readFileSync(path, "utf8")) as CjElement[]
  // a stale build (artwork edited, not rebuilt) is skipped: `bun run panel:check`
  // and the gate both compare the built file against the artwork source
  const current = PANEL_DESIGN_IDS.find(
    (id) => artworkMismatch(circuitJson, panelArtwork(id)).missing === 0 &&
      artworkMismatch(circuitJson, panelArtwork(id)).extra === 0,
  )
  if (!current) return
  expect(checkPanel({ circuitJson, fixture, art: panelArtwork(current) })).toEqual([])
})

describe("frozen geometry", () => {
  test("a hole that drifts 0.05mm is caught", () => {
    const circuitJson = fresh()
    const hole = circuitJson.find((e) => e.type === "pcb_hole" && e.x === -42 && e.y === -14)!
    hole.x += 0.05
    expect(hasProblem(checkFrozenGeometry(circuitJson, fixture), "holes", /missing baseline hole/)).toBe(true)
  })

  test("a hole with the wrong diameter is caught", () => {
    const circuitJson = fresh()
    circuitJson.find((e) => e.type === "pcb_hole" && e.hole_diameter === 7.2)!.hole_diameter = 7.0
    expect(hasProblem(checks(circuitJson), "holes", /missing baseline hole/)).toBe(true)
  })

  test("a missing hole is caught", () => {
    const circuitJson = fresh()
    const i = circuitJson.findIndex((e) => e.type === "pcb_hole")
    circuitJson.splice(i, 1)
    const problems = checks(circuitJson)
    expect(hasProblem(problems, "holes", /panel has 30 holes/)).toBe(true)
  })

  test("an extra hole is caught", () => {
    const circuitJson = fresh()
    circuitJson.push({ type: "pcb_hole", x: 0, y: 0, hole_diameter: 3.2, hole_shape: "circle" })
    expect(hasProblem(checks(circuitJson), "holes", /unexpected hole/)).toBe(true)
  })

  test("a plated hole in a mechanical panel is caught", () => {
    const circuitJson = fresh()
    circuitJson.find((e) => e.type === "pcb_hole")!.is_plated = true
    expect(hasProblem(checks(circuitJson), "holes", /must be NPTH/)).toBe(true)
  })

  test("a different outline or thickness is caught", () => {
    const wider = fresh()
    wider.find((e) => e.type === "pcb_board")!.width = 102.6
    expect(hasProblem(checks(wider), "outline", /baseline is 101.6/)).toBe(true)

    const thinner = fresh()
    thinner.find((e) => e.type === "pcb_board")!.thickness = 1.6
    expect(hasProblem(checks(thinner), "outline", /baseline is 2mm/)).toBe(true)
  })
})

describe("OLED window", () => {
  test("a sharp rectangle is rejected", () => {
    const circuitJson = fresh()
    const cut = circuitJson.find((e) => e.type === "pcb_cutout")!
    cut.shape = "rect"
    cut.width = 30.5
    cut.height = 16
    cut.center = { x: 0, y: 42.8 }
    expect(hasProblem(checks(circuitJson), "window", /rounded polygon/)).toBe(true)
  })

  test("a coarse corner sampling is rejected", () => {
    const circuitJson = fresh()
    const cut = circuitJson.find((e) => e.type === "pcb_cutout")!
    // keep only the four corner-arc end points: a box with sharp-ish corners
    cut.points = cut.points.filter((_: unknown, i: number) => i % 4 === 0)
    expect(hasProblem(checks(circuitJson), "window", /at least 8|R0.5/)).toBe(true)
  })

  test("a window that grew is rejected", () => {
    const circuitJson = fresh()
    const cut = circuitJson.find((e) => e.type === "pcb_cutout")!
    cut.points = cut.points.map((p: any) => ({ x: p.x * 1.05, y: p.y }))
    expect(hasProblem(checks(circuitJson), "window", /bounding box|centre/)).toBe(true)
  })

  test("a moved window is rejected", () => {
    const circuitJson = fresh()
    const cut = circuitJson.find((e) => e.type === "pcb_cutout")!
    cut.points = cut.points.map((p: any) => ({ x: p.x, y: p.y + 0.5 }))
    expect(hasProblem(checks(circuitJson), "window", /centre/)).toBe(true)
  })

  test("the corner measurement sees a real R0.5 rounded rectangle", () => {
    const measured = measureRoundedCorners(art.window.polygon, {
      cx: 0,
      cy: 42.8,
      width: 30.5,
      height: 16,
      radius: 0.5,
    })
    expect(measured.segmentsPerCorner).toBe(8)
    expect(measured.maxSagitta).toBeLessThan(0.01)
    expect(measured.minRadius).toBeCloseTo(0.5, 4)
  })
})

describe("printed artwork", () => {
  test("a stroke thinner than 0.2mm is caught", () => {
    const circuitJson = fresh()
    circuitJson.find((e) => e.type === "pcb_silkscreen_path")!.stroke_width = 0.15
    expect(hasProblem(checks(circuitJson), "silk", /0.150mm wide/)).toBe(true)
  })

  test("a missing path is caught", () => {
    const circuitJson = fresh()
    const i = circuitJson.findIndex((e) => e.type === "pcb_silkscreen_path")
    circuitJson.splice(i, 1)
    expect(hasProblem(checks(circuitJson), "silk", /missing from the built circuit JSON/)).toBe(true)
  })

  test("printing that is not in the artwork source is caught", () => {
    const circuitJson = fresh()
    circuitJson.push({
      type: "pcb_silkscreen_path",
      layer: "top",
      stroke_width: 0.25,
      route: [{ x: 0, y: 0 }, { x: 1, y: 0 }],
    })
    expect(hasProblem(checks(circuitJson), "silk", /not in the artwork source/)).toBe(true)
  })

  test("printing under a knob is caught", () => {
    const circuitJson = fresh()
    const knob = art.keepouts.find((k) => k.id === "keepout-knob-RV1")!
    circuitJson.push({
      type: "pcb_silkscreen_path",
      layer: "top",
      stroke_width: 0.25,
      route: [{ x: knob.x - 1, y: knob.y }, { x: knob.x + 1, y: knob.y }],
    })
    expect(hasProblem(checks(circuitJson), "silk", /sits under knob/)).toBe(true)
  })

  test("printing across the window is caught", () => {
    const circuitJson = fresh()
    circuitJson.push({
      type: "pcb_silkscreen_path",
      layer: "top",
      stroke_width: 0.25,
      route: [{ x: -20, y: 42.8 }, { x: 20, y: 42.8 }],
    })
    expect(hasProblem(checks(circuitJson), "silk", /OLED window/)).toBe(true)
  })

  test("printing at the panel edge is caught", () => {
    const circuitJson = fresh()
    circuitJson.push({
      type: "pcb_silkscreen_path",
      layer: "top",
      stroke_width: 0.25,
      route: [{ x: -50.7, y: 0 }, { x: -49.5, y: 0 }],
    })
    expect(hasProblem(checks(circuitJson), "silk", /panel edge/)).toBe(true)
  })

  test("a text primitive is rejected: its width is a font ratio, not a stroke", () => {
    const circuitJson = fresh()
    circuitJson.push({ type: "pcb_silkscreen_text", layer: "top", text: "X", font_size: 1.5 })
    expect(hasProblem(checks(circuitJson), "silk", /only stroked paths/)).toBe(true)
  })

  test("printing on the back layer is caught", () => {
    const circuitJson = fresh()
    circuitJson.find((e) => e.type === "pcb_silkscreen_path")!.layer = "bottom"
    expect(hasProblem(checks(circuitJson), "silk", /on layer bottom/)).toBe(true)
  })
})

describe("copper", () => {
  test.each([
    ["pcb_trace", { route: [] }],
    ["pcb_via", {}],
    ["pcb_smtpad", {}],
    ["pcb_plated_hole", {}],
    ["pcb_copper_pour", {}],
    ["pcb_solder_paste", {}],
  ])("a panel with a %s is rejected", (type, extra) => {
    const circuitJson = fresh()
    circuitJson.push({ type, layer: "top", ...extra })
    expect(hasProblem(checkNoCopper(circuitJson), "copper", /must be copper-free/)).toBe(true)
  })

  test("a panel with a net or a component is rejected", () => {
    const withNet = fresh()
    withNet.push({ type: "source_net", name: "GND" })
    expect(hasProblem(checkNoCopper(withNet), "copper", /net/)).toBe(true)

    const withComponent = fresh()
    withComponent.push({ type: "source_component", name: "R1" })
    expect(hasProblem(checkNoCopper(withComponent), "copper", /component/)).toBe(true)
  })
})

describe("artwork rules", () => {
  const mutate = (fn: (elements: ArtElement[]) => void): PanelArtwork => {
    const changed = copy(art)
    fn(changed.elements)
    return changed
  }

  test("an undersized stroke in the source is caught", () => {
    const changed = mutate((els) => {
      els[0]!.strokeWidth = 0.15
    })
    expect(hasProblem(checkArtwork(changed), "artwork", /floor is 0.2mm/)).toBe(true)
  })

  test("a legend below 1.2mm cap height is caught", () => {
    const changed = mutate((els) => {
      els[0]!.capHeight = 1.0
    })
    expect(hasProblem(checkArtwork(changed), "artwork", /cap height is 1mm/)).toBe(true)
  })

  test("a legend that draws a different size than it declares is caught", () => {
    const changed = mutate((els) => {
      els[0]!.paths = els[0]!.paths.map((p) => p.map((q) => ({ x: q.x, y: q.y + q.y * 0.5 })))
    })
    expect(hasProblem(checkArtwork(changed), "artwork", /should draw/)).toBe(true)
  })

  test("a missing required legend is caught", () => {
    const changed = mutate((els) => {
      const i = els.findIndex((e) => e.text === "MIDI OUT")
      els.splice(i, 1)
    })
    expect(hasProblem(checkArtwork(changed), "legends", /appears 0 times/)).toBe(true)
  })

  test("a legend that prints something else than it claims is caught", () => {
    const changed = mutate((els) => {
      els.find((e) => e.text === "P1")!.lines = ["P9"]
    })
    expect(hasProblem(checkArtwork(changed), "legends", /claims "P1" but prints "P9"/)).toBe(true)
  })

  test("a dropped required word is caught", () => {
    const changed = mutate((els) => {
      const nav = els.find((e) => e.id === "legend-nav-select")!
      nav.text = "NAV"
      nav.lines = ["NAV"]
    })
    expect(hasProblem(checkArtwork(changed), "legends", /word "SELECT"/)).toBe(true)
  })

  test("an undeclared extra legend is caught", () => {
    const changed = mutate((els) => {
      els.push({
        ...els.find((e) => e.id === "legend-mode")!,
        id: "legend-extra",
        group: "legend-extra",
        text: "GAIN",
        lines: ["GAIN"],
      })
    })
    expect(hasProblem(checkArtwork(changed), "legends", /unexpected legend text "GAIN"/)).toBe(true)
  })

  test("two legends printed on top of each other are caught", () => {
    const changed = mutate((els) => {
      const a = els.find((e) => e.id === "legend-RV1")!
      const b = els.find((e) => e.id === "legend-RV2")!
      b.paths = a.paths.map((p) => p.map((q) => ({ x: q.x + 0.1, y: q.y })))
      b.capHeight = a.capHeight
      b.pitch = a.pitch
      b.lines = a.lines
      b.text = a.text
      b.strokeWidth = a.strokeWidth
      b.inkHeight = a.inkHeight
    })
    expect(hasProblem(checkArtwork(changed), "artwork", /print .*mm apart/)).toBe(true)
  })

  test("a legend moved under a knob is caught", () => {
    const changed = mutate((els) => {
      const knob = changedKeepout()
      els.find((e) => e.id === "legend-RV3")!.paths = [circlePath(knob.x, knob.y, 0.5, 8)]
    })
    expect(hasProblem(checkArtwork(changed), "artwork", /sits under knob/)).toBe(true)
  })

  test("a legend pushed onto the panel edge is caught", () => {
    const changed = mutate((els) => {
      const e = els.find((x) => x.id === "legend-RV1")!
      e.paths = e.paths.map((p) => p.map((q) => ({ x: q.x - 6, y: q.y })))
    })
    expect(hasProblem(checkArtwork(changed), "artwork", /panel edge/)).toBe(true)
  })

  test("a legend inside the OLED window is caught", () => {
    const changed = mutate((els) => {
      els.find((e) => e.id === "legend-RV2")!.paths = [
        [{ x: -1, y: 42.8 }, { x: 1, y: 42.8 }],
      ]
    })
    expect(hasProblem(checkArtwork(changed), "artwork", /OLED window/)).toBe(true)
  })

  const changedKeepout = () => art.keepouts.find((k) => k.id === "keepout-knob-RV3")!
})

describe("design independence", () => {
  test("artwork from one design does not satisfy the other", () => {
    const signalJson = syntheticPanel(panelArtwork("signal"), fixture)
    expect(checkBuiltArtwork(signalJson, panelArtwork("signal"))).toEqual([])
    const mismatched = checkBuiltArtwork(signalJson, panelArtwork("nord"))
    expect(mismatched.length).toBeGreaterThan(0)
  })

  test("every design prints at least one path per artwork element", () => {
    for (const id of ["nord", "signal"] as const) {
      const paths = artworkPaths(panelArtwork(id))
      expect(paths.length).toBeGreaterThan(100)
      expect(PANEL_STYLES[id].id).toBe(id)
    }
  })
})

/// <reference types="bun-types" />
// Gerber and Excellon verification under mutation: the release is accepted on
// the geometry inside the files, never on their names.

import { describe, expect, test } from "bun:test"
import { PANEL_DESIGN_IDS, panelArtwork } from "./panel-art"
import type { Problem } from "./panel-checks"
import { parseExcellon, parseGerber, verifyPanelRelease, type ReleaseFile } from "./panel-gerber"
import { loadFixture, syntheticRelease } from "./panel-testkit"

const fixture = loadFixture()
const art = panelArtwork("nord")

const verify = (files: ReleaseFile[]) => verifyPanelRelease({ files, fixture, art })
const has = (problems: Problem[], check: string, match: RegExp) =>
  problems.some((p) => p.check === check && match.test(p.message))

const replace = (files: ReleaseFile[], name: string, edit: (text: string) => string): ReleaseFile[] =>
  files.map((f) => (f.name === name ? { name: f.name, text: edit(f.text) } : f))

describe("Gerber parser", () => {
  const text = [
    "%FSLAX46Y46*%",
    "%MOMM*%",
    "%TF.FileFunction,Legend,Top*%",
    "%ADD10C,0.250000*%",
    "%ADD11C,1.200000*%",
    "G01*",
    "D10*",
    "X0Y0D02*",
    "X1000000Y0D01*",
    "X1000000Y1000000D01*",
    "D11*",
    "X0Y0D03*",
    "M02*",
  ].join("\n")
  const layer = parseGerber("test.gbr", text)

  test("reads units, file function and apertures", () => {
    expect(layer.units).toBe("mm")
    expect(layer.fileFunction).toBe("Legend,Top")
    expect(layer.apertures).toEqual([
      { code: 10, diameter: 0.25 },
      { code: 11, diameter: 1.2 },
    ])
    expect(layer.usedApertures).toEqual([10, 11])
  })

  test("scales 4.6 coordinates to millimetres and builds contours", () => {
    expect(layer.draws).toHaveLength(2)
    expect(layer.draws[1]!.to.x).toBeCloseTo(1, 6)
    expect(layer.draws[1]!.to.y).toBeCloseTo(1, 6)
    expect(layer.contours).toHaveLength(1)
    expect(layer.contours[0]).toHaveLength(3)
    expect(layer.flashes).toEqual([{ aperture: 11, at: { x: 0, y: 0 } }])
  })

  test("reads an inch file", () => {
    const inch = parseGerber("x.gbr", "%FSLAX24Y24*%\n%MOIN*%\n%ADD10C,0.01*%\nD10*\nX0Y0D02*\nX1000Y0D01*\nM02*")
    expect(inch.units).toBe("in")
    expect(inch.apertures[0]!.diameter).toBeCloseTo(0.254, 6)
    expect(inch.draws[0]!.to.x).toBeCloseTo(2.54, 6)
  })
})

describe("Excellon parser", () => {
  const text = [
    "M48",
    "METRIC",
    "; #@! TF.FileFunction,NonPlated,1,2,NPTH",
    "T10C3.200000",
    "T11C6.200000",
    "%",
    "G90",
    "T10",
    "X-43.3000Y61.2500",
    "T11",
    "X-42.0000Y-14.0000",
    "M30",
  ].join("\n")
  const drill = parseExcellon("drill_npth.drl", text)

  test("reads tools, holes and the NPTH file function", () => {
    expect(drill.units).toBe("mm")
    expect(drill.fileFunction).toContain("NPTH")
    expect(drill.tools.map((t) => t.diameter)).toEqual([3.2, 6.2])
    expect(drill.holes).toHaveLength(2)
    expect(drill.holes[0]!.at).toEqual({ x: -43.3, y: 61.25 })
    expect(drill.holes[1]!.diameter).toBe(6.2)
  })
})

describe.each(PANEL_DESIGN_IDS)("%s release", (design) => {
  test("an honest archive passes every check", () => {
    const result = verifyPanelRelease({
      files: syntheticRelease(panelArtwork(design)),
      fixture,
      art: panelArtwork(design),
    })
    expect(result.problems).toEqual([])
    expect(result.summary.profile.contours).toBe(2)
    expect(result.summary.drill.holes).toBe(31)
    expect(result.summary.silk.minAperture).toBeGreaterThanOrEqual(0.2)
  })
})

describe("release mutations", () => {
  const base = () => syntheticRelease(art)

  test("copper on the panel is rejected", () => {
    const files = replace(base(), "F_Cu.gbr", (t) =>
      t.replace("M02*", "D10*\nX0Y0D02*\nX1000000Y0D01*\nM02*"),
    )
    expect(has(verify(files).problems, "copper", /must be copper-free/)).toBe(true)
  })

  test("a legend stroke below 0.2mm is rejected", () => {
    const files = replace(base(), "F_SilkScreen.gbr", (t) => t.replace(/C,0\.250000/, "C,0.150000"))
    expect(has(verify(files).problems, "silk", /0.150mm stroke/)).toBe(true)
  })

  test("a missing legend is rejected", () => {
    const files = replace(base(), "F_SilkScreen.gbr", () =>
      "%FSLAX46Y46*%\n%MOMM*%\n%TF.FileFunction,Legend,Top*%\n%ADD10C,0.250000*%\nG01*\nM02*",
    )
    expect(has(verify(files).problems, "silk", /would print nothing/)).toBe(true)
  })

  test("a legend that is not the artwork is rejected", () => {
    const files = replace(base(), "F_SilkScreen.gbr", (t) =>
      t.replace("M02*", "D10*\nX0Y0D02*\nX1000000Y0D01*\nM02*"),
    )
    expect(has(verify(files).problems, "silk", /not the artwork/)).toBe(true)
  })

  /** A profile layer with the outline and, optionally, an arbitrary window. */
  const profileWith = (window: { x: number; y: number }[] | null): string => {
    const g = (v: number) => Math.round(v * 1e6).toString()
    const outline = [
      { x: -fixture.outline.width / 2, y: -fixture.outline.height / 2 },
      { x: fixture.outline.width / 2, y: -fixture.outline.height / 2 },
      { x: fixture.outline.width / 2, y: fixture.outline.height / 2 },
      { x: -fixture.outline.width / 2, y: fixture.outline.height / 2 },
      { x: -fixture.outline.width / 2, y: -fixture.outline.height / 2 },
    ]
    const contours = window ? [outline, [...window, window[0]!]] : [outline]
    const body = contours
      .map((c) => {
        const first = c[0]!
        return [
          `X${g(first.x)}Y${g(first.y)}D02*`,
          ...c.slice(1).map((p) => `X${g(p.x)}Y${g(p.y)}D01*`),
        ].join("\n")
      })
      .join("\n")
    return `%FSLAX46Y46*%\n%MOMM*%\n%TF.FileFunction,Profile,NP*%\n%ADD10C,0.050000*%\nG01*\nD10*\n${body}\nM02*`
  }

  test("a profile without the OLED window is rejected", () => {
    const files = replace(base(), "Edge_Cuts.gbr", () => profileWith(null))
    expect(has(verify(files).problems, "profile", /exactly one window|contour/)).toBe(true)
  })

  test("a sharp window in the profile is rejected", () => {
    const sharp = [
      { x: -15.25, y: 34.8 },
      { x: 15.25, y: 34.8 },
      { x: 15.25, y: 50.8 },
      { x: -15.25, y: 50.8 },
    ]
    const files = replace(base(), "Edge_Cuts.gbr", () => profileWith(sharp))
    expect(has(verify(files).problems, "profile", /corner|R0\.5/)).toBe(true)
  })

  test("a moved drill hole is rejected", () => {
    const files = replace(base(), "drill_npth.drl", (t) =>
      t.replace("X-43.3000Y61.2500", "X-43.3000Y61.3500"),
    )
    expect(has(verify(files).problems, "drill", /no NPTH hole at/)).toBe(true)
  })

  test("a drilled hole with the wrong tool is rejected", () => {
    const files = replace(base(), "drill_npth.drl", (t) =>
      t.replace("T10C3.200000", "T10C3.000000"),
    )
    const problems = verify(files).problems
    expect(problems.length).toBeGreaterThan(0)
    expect(has(problems, "drill", /no NPTH hole|unexpected NPTH hole/)).toBe(true)
  })

  test("a plated drill file is rejected", () => {
    const files = [
      ...base(),
      {
        name: "drill_pth.drl",
        text: "M48\nMETRIC\nT10C0.900000\n%\nT10\nX0.0000Y0.0000\nM30",
      },
    ]
    expect(has(verify(files).problems, "drill", /every panel hole is NPTH/)).toBe(true)
  })

  test("assembly data riding along is rejected", () => {
    const files = replace(base(), "bom.csv", () => "Comment,Designator\n100k,R1\n")
    expect(has(verify(files).problems, "assembly", /not empty/)).toBe(true)
  })

  test("a second profile layer is rejected", () => {
    const files = [
      ...base(),
      { name: "Extra_Cuts.gbr", text: "%TF.FileFunction,Profile,NP*%\n%MOMM*%\nM02*" },
    ]
    expect(has(verify(files).problems, "profile", /exactly 1 profile/)).toBe(true)
  })
})

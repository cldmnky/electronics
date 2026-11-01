// Froemix front panel - Gerber and Excellon reading, and the release checks.
//
// The point of this file is that a release is not accepted because the ZIP has
// the right file names: the files are parsed, and the checks are on the drawn
// geometry and the units inside them.
//
//   * exactly one profile layer, and it draws the finished outline and the
//     rounded OLED window (R0.5), nothing else
//   * both copper layers are empty - the panel has no electrical function
//   * the top legend really is printed: many drawn segments, and every aperture
//     (which IS the printed stroke width) is at least 0.2mm
//   * the NPTH drill file holds all 31 holes at the frozen coordinates and tool
//     diameters, and no plated hole is drilled into a mechanical panel
//   * no assembly data rides along
//
// Not proven here: how JLCPCB's CAM turns 6.3mm+ holes into routed holes, and
// whether the finished panel fits real hardware. See doc/FRONT-PANEL.md.

import { MIN_STROKE_WIDTH, artworkPaths, type PanelArtwork } from "./panel-art"
import { measureRoundedCorners, type PanelFixture, type Problem } from "./panel-checks"
import { polylineBounds, type Pt } from "./panel-geometry"

export type Aperture = { code: number; diameter: number }

export type GerberLayer = {
  name: string
  fileFunction: string
  units: "mm" | "in"
  apertures: Aperture[]
  /** aperture codes that are actually selected while drawing or flashing */
  usedApertures: number[]
  draws: { aperture: number; from: Pt; to: Pt }[]
  flashes: { aperture: number; at: Pt }[]
  /** polylines built from D02 (move) + D01 (draw) runs */
  contours: Pt[][]
  unknownCommands: number
}

const fail = (check: string, message: string): Problem => ({ check, message })

const scaleFor = (units: "mm" | "in") => (units === "in" ? 25.4 : 1)

// ---------------------------------------------------------------------------
// Gerber (RS-274X)
// ---------------------------------------------------------------------------

export const parseGerber = (name: string, text: string): GerberLayer => {
  const apertures: Aperture[] = []
  const draws: GerberLayer["draws"] = []
  const flashes: GerberLayer["flashes"] = []
  const contours: Pt[][] = []
  const used = new Set<number>()
  let fileFunction = ""
  let units: "mm" | "in" = "mm"
  let xInt = 4
  let xDec = 6
  let yInt = 4
  let yDec = 6
  let aperture = 0
  let current: Pt[] | null = null
  let cursor: Pt | null = null
  let unknownCommands = 0

  const tokens: string[] = []
  for (let i = 0; i < text.length; ) {
    const ch = text[i]
    if (ch === "%") {
      const end = text.indexOf("%", i + 1)
      if (end < 0) break
      tokens.push(text.slice(i, end + 1))
      i = end + 1
    } else if (ch === "*" || ch === " " || ch === "\n" || ch === "\r" || ch === "\t") {
      i++
    } else {
      const end = text.indexOf("*", i)
      if (end < 0) {
        tokens.push(text.slice(i).trim())
        break
      }
      tokens.push(text.slice(i, end).trim())
      i = end
    }
  }

  for (const token of tokens) {
    if (!token) continue
    const scaled = (raw: number) => raw * scaleFor(units)

    const ext = token.startsWith("%")
    const body = ext
      ? token.slice(1, token.endsWith("%") ? -1 : undefined).replace(/\*$/, "")
      : token

    if (ext) {
      const fsMatch = /^FSLAX(\d)(\d)Y(\d)(\d)/.exec(body)
      if (fsMatch) {
        xInt = Number(fsMatch[1])
        xDec = Number(fsMatch[2])
        yInt = Number(fsMatch[3])
        yDec = Number(fsMatch[4])
        continue
      }
      if (body.startsWith("MOMM")) {
        units = "mm"
        continue
      }
      if (body.startsWith("MOIN")) {
        units = "in"
        continue
      }
      const add = /^ADD(\d+)C,([\d.]+)/.exec(body)
      if (add) {
        apertures.push({ code: Number(add[1]), diameter: scaled(Number(add[2])) })
        continue
      }
      const tf = /^TF\.FileFunction,?(.*)$/.exec(body)
      if (tf) {
        fileFunction = tf[1] ?? ""
        continue
      }
      continue // other attributes, aperture macros: not needed here
    }

    const sel = /^D(\d+)$/.exec(body)
    if (sel) {
      const code = Number(sel[1])
      if (code >= 10) {
        aperture = code
        used.add(code)
        continue
      }
    }
    const op = /^(?:X(-?\d+))?(?:Y(-?\d+))?(?:I-?\d+)?(?:J-?\d+)?D0([123])$/.exec(body)
    if (op) {
      const px = op[1] === undefined ? (cursor?.x ?? 0) : (Number(op[1]) / 10 ** xDec) * scaleFor(units)
      const py = op[2] === undefined ? (cursor?.y ?? 0) : (Number(op[2]) / 10 ** yDec) * scaleFor(units)
      const kind = op[3]
      if (kind === "2") {
        if (current && current.length > 1) contours.push(current)
        current = [{ x: px, y: py }]
      } else if (kind === "1") {
        const from = cursor ?? { x: px, y: py }
        draws.push({ aperture, from, to: { x: px, y: py } })
        if (!current) current = [from]
        current.push({ x: px, y: py })
      } else if (kind === "3") {
        flashes.push({ aperture, at: { x: px, y: py } })
      }
      cursor = { x: px, y: py }
      continue
    }
    if (/^[GM]\d+$/.test(body) || body.startsWith("G04") || /^0 /.test(body)) continue
    unknownCommands++
  }
  if (current && current.length > 1) contours.push(current)
  void xInt
  void yInt

  return {
    name,
    fileFunction,
    units,
    apertures,
    usedApertures: [...used].sort((a, b) => a - b),
    draws,
    flashes,
    contours,
    unknownCommands,
  }
}

// ---------------------------------------------------------------------------
// Excellon drill
// ---------------------------------------------------------------------------

export type DrillFile = {
  name: string
  fileFunction: string
  units: "mm" | "in"
  tools: Aperture[]
  holes: { tool: number; diameter: number; at: Pt }[]
}

export const parseExcellon = (name: string, text: string): DrillFile => {
  const tools: Aperture[] = []
  const holes: DrillFile["holes"] = []
  let units: "mm" | "in" = "mm"
  let fileFunction = ""
  let tool = 0
  for (const rawLine of text.split(/\r?\n/)) {
    const line = rawLine.trim()
    if (!line) continue
    if (/^METRIC/.test(line)) {
      units = "mm"
      continue
    }
    if (/^INCH/.test(line)) {
      units = "in"
      continue
    }
    if (line.startsWith(";")) {
      const ff = /TF\.FileFunction,(.*)$/.exec(line)
      if (ff) fileFunction = ff[1] ?? ""
      continue
    }
    const t = /^T(\d+)C([\d.]+)$/.exec(line)
    if (t) {
      tools.push({ code: Number(t[1]), diameter: Number(t[2]) * scaleFor(units) })
      continue
    }
    const sel = /^T(\d+)$/.exec(line)
    if (sel) {
      tool = Number(sel[1])
      continue
    }
    const xy = /^X(-?[\d.]+)Y(-?[\d.]+)$/.exec(line)
    if (xy) {
      const d = tools.find((t2) => t2.code === tool)?.diameter ?? 0
      holes.push({
        tool,
        diameter: d,
        at: { x: Number(xy[1]) * scaleFor(units), y: Number(xy[2]) * scaleFor(units) },
      })
      continue
    }
    // headers, G-codes, repeats: ignored on purpose
  }
  return { name, fileFunction, units, tools, holes }
}

// ---------------------------------------------------------------------------
// Release verification
// ---------------------------------------------------------------------------

export type ReleaseFile = { name: string; text: string }

export type ReleaseSummary = {
  layers: { name: string; fileFunction: string; units: string; draws: number; flashes: number; contours: number }[]
  drill: { name: string; holes: number; tools: { diameter: number; count: number }[] }
  silk: { contours: number; minAperture: number; apertures: number[] }
  profile: { contours: number; outline: string; window: string }
  ignored: string[]
}

const isTextLayer = (name: string) =>
  /\.(gbr|gko|drl|txt)$/i.test(name) || name === "bom.csv" || name === "pick_and_place.csv"

/**
 * Verify one exported panel ZIP. `files` is the archive contents, name -> text.
 * Every check reads inside the files; file names alone are never trusted.
 */
export const verifyPanelRelease = (opts: {
  files: ReleaseFile[]
  fixture: PanelFixture
  art: PanelArtwork
}): { problems: Problem[]; summary: ReleaseSummary } => {
  const problems: Problem[] = []
  const { fixture, art } = opts

  const gerbers: GerberLayer[] = []
  const drills: DrillFile[] = []
  const ignored: string[] = []
  for (const file of opts.files) {
    if (/\.drl$/i.test(file.name)) {
      drills.push(parseExcellon(file.name, file.text))
    } else if (/\.(gbr|gko)$/i.test(file.name)) {
      gerbers.push(parseGerber(file.name, file.text))
    } else if (isTextLayer(file.name)) {
      ignored.push(file.name)
    } else {
      ignored.push(file.name)
    }
  }

  const profiles = gerbers.filter((g) => g.fileFunction.startsWith("Profile"))
  const coppers = gerbers.filter((g) => g.fileFunction.startsWith("Copper"))
  const silkTop = gerbers.filter(
    (g) => g.fileFunction.startsWith("Legend") && g.fileFunction.includes("Top"),
  )
  const silkBottom = gerbers.filter(
    (g) => g.fileFunction.startsWith("Legend") && g.fileFunction.includes("Bot"),
  )
  const masks = gerbers.filter((g) => g.fileFunction.startsWith("Soldermask"))

  // --- profile
  let profileSummary = { contours: 0, outline: "missing", window: "missing" }
  if (profiles.length !== 1) {
    problems.push(
      fail("profile", `expected exactly 1 profile layer, found ${profiles.length}: ${profiles.map((p) => p.name).join(", ")}`),
    )
  } else {
    const profile = profiles[0]!
    if (profile.unknownCommands > 0) {
      problems.push(fail("profile", `${profile.name}: ${profile.unknownCommands} unparsed command(s)`))
    }
    const bounds = profile.contours.map((c) => polylineBounds(c))
    const outerIndex = bounds.reduce(
      (best, b, i) => (b && (!bounds[best] || b.width * b.height > bounds[best]!.width * bounds[best]!.height) ? i : best),
      0,
    )
    const outer = bounds[outerIndex]
    if (!outer) {
      problems.push(fail("profile", `${profile.name} draws no contour`))
    } else {
      if (Math.abs(outer.width - fixture.outline.width) > 0.01 || Math.abs(outer.height - fixture.outline.height) > 0.01) {
        problems.push(
          fail(
            "profile",
            `routed outline is ${outer.width.toFixed(3)} x ${outer.height.toFixed(3)}mm, the contract is ${fixture.outline.width} x ${fixture.outline.height}mm`,
          ),
        )
      }
      profileSummary.outline = `${outer.width.toFixed(3)} x ${outer.height.toFixed(3)}mm`
    }

    const windows = bounds.filter((b, i) => i !== outerIndex && b !== null)
    if (profile.contours.length !== 2) {
      problems.push(
        fail(
          "profile",
          `${profile.contours.length} profile contour(s); the panel needs the outline and exactly one window`,
        ),
      )
    }
    const windowBounds = windows[0]
    if (!windowBounds) {
      problems.push(fail("profile", "the profile layer has no OLED window contour"))
    } else {
      const w = fixture.window
      if (
        Math.abs(windowBounds.width - w.width) > 0.01 ||
        Math.abs(windowBounds.height - w.height) > 0.01 ||
        Math.abs(windowBounds.cx - w.center.x) > 0.01 ||
        Math.abs(windowBounds.cy - w.center.y) > 0.01
      ) {
        problems.push(
          fail(
            "profile",
            `routed window is ${windowBounds.width.toFixed(3)} x ${windowBounds.height.toFixed(3)}mm at (${windowBounds.cx.toFixed(3)}, ${windowBounds.cy.toFixed(3)}), the contract is ${w.width} x ${w.height}mm at (${w.center.x}, ${w.center.y})`,
          ),
        )
      }
      const polygon = profile.contours[bounds.indexOf(windowBounds)]!
      const corners = measureRoundedCorners(polygon, {
        cx: windowBounds.cx,
        cy: windowBounds.cy,
        width: windowBounds.width,
        height: windowBounds.height,
        radius: 0.5,
      })
      if (corners.segmentsPerCorner < 8 || corners.maxSagitta > 0.1) {
        problems.push(
          fail(
            "profile",
            `routed window corners: ${corners.segmentsPerCorner} chords, sagitta ${corners.maxSagitta.toFixed(4)}mm - the routed corner radius is not R0.5 or too coarse`,
          ),
        )
      }
      if (corners.minRadius < 0.45 || corners.maxRadius > 0.55) {
        problems.push(
          fail(
            "profile",
            `routed window corner radius is R${corners.minRadius.toFixed(3)}-R${corners.maxRadius.toFixed(3)}, must be R0.5`,
          ),
        )
      }
      profileSummary.window = `${windowBounds.width.toFixed(3)} x ${windowBounds.height.toFixed(3)}mm, R${corners.minRadius.toFixed(2)}`
    }
    profileSummary.contours = profile.contours.length
  }

  // --- copper must be empty
  if (coppers.length !== 2) {
    problems.push(fail("copper", `expected 2 copper layers, found ${coppers.length}`))
  }
  for (const copper of coppers) {
    if (copper.draws.length > 0 || copper.flashes.length > 0) {
      problems.push(
        fail(
          "copper",
          `${copper.name} draws ${copper.draws.length} trace(s) and ${copper.flashes.length} pad(s); the panel must be copper-free`,
        ),
      )
    }
  }

  // --- top legend: really printed, wide enough
  let silkSummary = { contours: 0, minAperture: 0, apertures: [] as number[] }
  if (silkTop.length !== 1) {
    problems.push(fail("silk", `expected exactly 1 top legend layer, found ${silkTop.length}`))
  } else {
    const silk = silkTop[0]!
    const used = silk.apertures.filter((a) => silk.usedApertures.includes(a.code))
    const diameters = used.map((a) => a.diameter).sort((a, b) => a - b)
    silkSummary = { contours: silk.contours.length, minAperture: diameters[0] ?? 0, apertures: diameters }
    if (silk.draws.length === 0) {
      problems.push(fail("silk", `${silk.name} is empty - the panel would print nothing`))
    }
    for (const d of diameters) {
      if (d < MIN_STROKE_WIDTH - 1e-6) {
        problems.push(
          fail("silk", `${silk.name} prints a ${d.toFixed(3)}mm stroke; the panel floor is ${MIN_STROKE_WIDTH}mm`),
        )
      }
    }
    const expected = artworkPaths(art).length
    if (silk.contours.length !== expected) {
      problems.push(
        fail(
          "silk",
          `${silk.name} prints ${silk.contours.length} polylines, the artwork has ${expected} - the export is not the artwork`,
        ),
      )
    }
  }
  if (silkBottom.length > 0 && silkBottom[0]!.draws.length > 0) {
    problems.push(fail("silk", `${silkBottom[0]!.name} prints on the back; the panel legend is top only`))
  }
  if (masks.length !== 2) {
    problems.push(fail("mask", `expected 2 mask layers, found ${masks.length}`))
  }

  // --- drill: NPTH only, all 31 holes, correct tool sizes
  const npth = drills.filter((d) => /NonPlated|NPTH/i.test(d.fileFunction) || /npth/i.test(d.name))
  const other = drills.filter((d) => !npth.includes(d))
  if (npth.length !== 1) {
    problems.push(fail("drill", `expected exactly 1 NPTH drill file, found ${npth.length}`))
  }
  if (other.some((d) => d.holes.length > 0)) {
    problems.push(
      fail(
        "drill",
        `a plated drill file holds ${other.map((d) => d.holes.length).join("/")} hole(s); every panel hole is NPTH`,
      ),
    )
  }
  let drillSummary = { name: "missing", holes: 0, tools: [] as { diameter: number; count: number }[] }
  if (npth.length === 1) {
    const drill = npth[0]!
    if (drill.units !== "mm") problems.push(fail("drill", `${drill.name} is not in millimetres`))
    drillSummary = {
      name: drill.name,
      holes: drill.holes.length,
      tools: drill.tools
        .map((t) => ({ diameter: t.diameter, count: drill.holes.filter((h) => h.tool === t.code).length }))
        .filter((t) => t.count > 0)
        .sort((a, b) => a.diameter - b.diameter),
    }
    const unmatched = [...drill.holes]
    for (const want of fixture.holes) {
      const i = unmatched.findIndex(
        (h) =>
          Math.abs(h.at.x - want.x) <= 0.001 &&
          Math.abs(h.at.y - want.y) <= 0.001 &&
          Math.abs(h.diameter - want.diameter) <= 0.001,
      )
      if (i < 0) {
        problems.push(
          fail(
            "drill",
            `no NPTH hole at (${want.x}, ${want.y}) with tool ${want.diameter}mm in ${drill.name}`,
          ),
        )
        continue
      }
      unmatched.splice(i, 1)
    }
    for (const extra of unmatched) {
      problems.push(
        fail(
          "drill",
          `unexpected NPTH hole at (${extra.at.x.toFixed(3)}, ${extra.at.y.toFixed(3)}) tool ${extra.diameter}mm`,
        ),
      )
    }
  }

  // --- no assembly data rides along
  for (const name of ["bom.csv", "pick_and_place.csv"]) {
    const file = opts.files.find((f) => f.name === name)
    if (file && file.text.trim().length > 0) {
      problems.push(
        fail("assembly", `${name} is not empty; the panel is fabricated, not assembled`),
      )
    }
  }

  return {
    problems,
    summary: {
      layers: gerbers.map((g) => ({
        name: g.name,
        fileFunction: g.fileFunction,
        units: g.units,
        draws: g.draws.length,
        flashes: g.flashes.length,
        contours: g.contours.length,
      })),
      drill: drillSummary,
      silk: silkSummary,
      profile: profileSummary,
      ignored,
    },
  }
}

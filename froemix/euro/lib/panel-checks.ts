// Froemix front panel - manufacturing checks.
//
// Pure functions over three inputs: the BUILT circuit JSON, the frozen baseline
// fixture (`fixtures/panel-baseline.json`) and the artwork data
// (`lib/panel-art.ts`). No I/O here, so `scripts/panel_check.ts` can run it on a
// built board, and the tests can run it on deliberately broken copies.
//
// What this proves, and what it cannot:
//   * every hole centre and diameter is exactly the frozen one, and the outline
//     and thickness are unchanged - drift is caught, not tolerated
//   * the OLED window keeps its 30.5 x 16.0mm box at (0, 42.8) and has R0.5
//     internal corners, sampled finely enough for JLCPCB to route
//   * every printed stroke is at least 0.2mm wide, every legend at least 1.2mm
//     tall (measured from drawn geometry), glyphs keep clear of each other
//   * no printing sits under a knob, nut, washer, LED or button, none crosses
//     the window, none leaves the panel
//   * the panel carries no copper and no nets
// It does NOT prove mechanical fit with real hardware, nor that JLCPCB accepts
// the files. Those stay in froemix/doc/FRONT-PANEL.md.

import { PANEL_REAR_STROKE, panelRearPaths } from "./panel-rear"
import {
  MIN_ELEMENT_CLEARANCE,
  MIN_GLYPH_CLEARANCE,
  MIN_LEGEND_HEIGHT,
  MIN_STROKE_WIDTH,
  SILK_EDGE_MARGIN,
  SILK_WINDOW_MARGIN,
  OPTIONAL_LEGENDS,
  PANEL_DESIGN_IDS,
  REQUIRED_LEGENDS,
  REQUIRED_WORDS,
  artworkPaths,
  elementClearanceToKeepout,
  elementInkBounds,
  minGlyphClearance,
  panelArtwork,
  textInkHeight,
  isPanelDesignId,
  type ArtElement,
  type PanelArtwork,
  type PanelDesignId,
} from "./panel-art"
import {
  boundsOf,
  distPathsToPaths,
  distPointToPolyline,
  distPolylineToPolygon,
  polylineBounds,
  type Bounds,
  type Pt,
} from "./panel-geometry"

export type Problem = { check: string; message: string }

/** Coordinate tolerance for the frozen geometry, in mm. */
export const GEOMETRY_TOLERANCE = 0.001

export type PanelFixture = {
  outline: { width: number; height: number; thickness: number; center: { x: number; y: number } }
  holes: { x: number; y: number; diameter: number }[]
  window: { shape: string; center: { x: number; y: number }; width: number; height: number }
}

type CjElement = Record<string, any>

const fail = (check: string, message: string): Problem => ({ check, message })

const near = (a: number, b: number, tol = GEOMETRY_TOLERANCE) => Math.abs(a - b) <= tol

/** Format a point from either the fixture or the built JSON, for messages. */
const fmt = (p: any) => `(${Number(p.x).toFixed(3)}, ${Number(p.y).toFixed(3)})`

// ---------------------------------------------------------------------------
// Rounded rectangle distance: the exact shape the OLED window must have
// ---------------------------------------------------------------------------

/**
 * Signed distance from a point to a rounded rectangle: negative inside, 0 on
 * the boundary. Used to prove the window polygon really is a 0.5mm-radius
 * rounded rectangle and not just a box of the right size.
 */
export const signedDistanceToRoundedRect = (
  p: Pt,
  rect: { cx: number; cy: number; width: number; height: number; radius: number },
): number => {
  const hw = rect.width / 2
  const hh = rect.height / 2
  const r = Math.min(rect.radius, hw, hh)
  const qx = Math.abs(p.x - rect.cx) - (hw - r)
  const qy = Math.abs(p.y - rect.cy) - (hh - r)
  const outside = Math.hypot(Math.max(qx, 0), Math.max(qy, 0))
  const inside = Math.min(Math.max(qx, qy), 0)
  return outside + inside - r
}

/** Four corner centres of a rounded rectangle, as (centre, quadrant) pairs. */
const cornerCentres = (rect: { cx: number; cy: number; width: number; height: number; radius: number }) => {
  const hw = rect.width / 2 - rect.radius
  const hh = rect.height / 2 - rect.radius
  return [
    { x: rect.cx + hw, y: rect.cy + hh, sx: 1, sy: 1 },
    { x: rect.cx - hw, y: rect.cy + hh, sx: -1, sy: 1 },
    { x: rect.cx - hw, y: rect.cy - hh, sx: -1, sy: -1 },
    { x: rect.cx + hw, y: rect.cy - hh, sx: 1, sy: -1 },
  ]
}

/**
 * Corner radius actually present in a closed polygon, plus the coarsest chord
 * sagitta found on its corners. A sharp rectangle has no vertices within
 * tolerance of the ideal arc, so `segmentsPerCorner` comes back as 0.
 */
export const measureRoundedCorners = (
  polygon: Pt[],
  rect: { cx: number; cy: number; width: number; height: number; radius: number },
): { segmentsPerCorner: number; maxSagitta: number; minRadius: number; maxRadius: number } => {
  let segmentsPerCorner = Infinity
  let maxSagitta = 0
  let minRadius = Infinity
  let maxRadius = 0
  for (const c of cornerCentres(rect)) {
    // vertices on this corner's arc, with their index in the closed contour
    const onArc: { v: Pt; i: number }[] = []
    polygon.forEach((v, i) => {
      const dx = (v.x - c.x) * c.sx
      const dy = (v.y - c.y) * c.sy
      if (dx < -1e-6 || dy < -1e-6) return
      const r = Math.hypot(dx, dy)
      if (Math.abs(r - rect.radius) > 0.02) return
      onArc.push({ v, i })
      if (r < minRadius) minRadius = r
      if (r > maxRadius) maxRadius = r
    })
    // only truly adjacent vertices are a chord: a closed contour repeats its
    // first point at the end, and that wrap-around pair spans the whole corner
    let chords = 0
    for (let k = 0; k < onArc.length - 1; k++) {
      const a = onArc[k]!
      const b = onArc[k + 1]!
      if (b.i !== a.i + 1) continue
      chords++
      const mid = { x: (a.v.x + b.v.x) / 2, y: (a.v.y + b.v.y) / 2 }
      const sagitta = rect.radius - Math.hypot(mid.x - c.x, mid.y - c.y)
      if (sagitta > maxSagitta) maxSagitta = sagitta
    }
    segmentsPerCorner = Math.min(segmentsPerCorner, chords)
  }
  return {
    segmentsPerCorner: Number.isFinite(segmentsPerCorner) ? segmentsPerCorner : 0,
    maxSagitta,
    minRadius: Number.isFinite(minRadius) ? minRadius : 0,
    maxRadius,
  }
}

// ---------------------------------------------------------------------------
// 1. Frozen mechanical geometry
// ---------------------------------------------------------------------------

export const checkFrozenGeometry = (circuitJson: CjElement[], fixture: PanelFixture): Problem[] => {
  const problems: Problem[] = []

  const boards = circuitJson.filter((e) => e.type === "pcb_board")
  if (boards.length !== 1) {
    problems.push(fail("outline", `expected exactly 1 pcb_board, found ${boards.length}`))
  } else {
    const board = boards[0]!
    const o = fixture.outline
    if (!near(board.width, o.width) || !near(board.height, o.height)) {
      problems.push(
        fail(
          "outline",
          `panel is ${board.width} x ${board.height}mm, baseline is ${o.width} x ${o.height}mm`,
        ),
      )
    }
    if (!near(board.thickness, o.thickness)) {
      problems.push(fail("outline", `panel thickness is ${board.thickness}mm, baseline is ${o.thickness}mm`))
    }
    const c = board.center ?? { x: 0, y: 0 }
    if (!near(c.x, o.center.x) || !near(c.y, o.center.y)) {
      problems.push(fail("outline", `panel centre moved to ${fmt(c)}, baseline is ${fmt(o.center)}`))
    }
  }

  // --- holes: exact multiset compare, no extras, no missing, no drift
  const holes = circuitJson.filter((e) => e.type === "pcb_hole")
  const unmatchedBuilt = [...holes]
  for (const want of fixture.holes) {
    const idx = unmatchedBuilt.findIndex(
      (h) =>
        near(h.x, want.x) && near(h.y, want.y) && near(h.hole_diameter ?? 0, want.diameter),
    )
    if (idx < 0) {
      const nearest = holes
        .map((h) => ({
          h,
          d: Math.hypot(h.x - want.x, h.y - want.y) + Math.abs((h.hole_diameter ?? 0) - want.diameter),
        }))
        .sort((a, b) => a.d - b.d)[0]
      const hint = nearest
        ? `; nearest built hole ${fmt(nearest.h)} d=${nearest.h.hole_diameter}mm`
        : "; the panel has no holes at all"
      problems.push(
        fail("holes", `missing baseline hole ${fmt(want)} d=${want.diameter}mm${hint}`),
      )
      continue
    }
    unmatchedBuilt.splice(idx, 1)
  }
  for (const extra of unmatchedBuilt) {
    problems.push(
      fail("holes", `unexpected hole ${fmt(extra)} d=${extra.hole_diameter}mm (not in the baseline)`),
    )
  }
  for (const h of holes) {
    if (h.hole_shape && h.hole_shape !== "circle") {
      problems.push(fail("holes", `hole ${fmt(h)} is ${h.hole_shape}, the panel only has round holes`))
    }
    if (h.is_plated === true) {
      problems.push(fail("holes", `hole ${fmt(h)} is plated; every panel hole must be NPTH`))
    }
  }
  if (holes.length !== fixture.holes.length) {
    problems.push(
      fail("holes", `panel has ${holes.length} holes, baseline has ${fixture.holes.length}`),
    )
  }

  // --- OLED window
  const cutouts = circuitJson.filter((e) => e.type === "pcb_cutout")
  if (cutouts.length !== 1) {
    problems.push(fail("window", `expected exactly 1 cutout, found ${cutouts.length}`))
    return problems
  }
  const cut = cutouts[0]!
  const want = fixture.window
  const rect = { cx: want.center.x, cy: want.center.y, width: want.width, height: want.height }

  if (cut.shape !== "polygon") {
    problems.push(
      fail(
        "window",
        `OLED window is a ${cut.shape} cutout; JLCPCB rejects sharp internal rectangles, so it must be a rounded polygon`,
      ),
    )
    return problems
  }
  const points: Pt[] = cut.points ?? []
  if (points.length < 12) {
    problems.push(fail("window", `window polygon has only ${points.length} points - corners cannot be rounded`))
  }
  const bounds = polylineBounds(points)
  if (!bounds) {
    problems.push(fail("window", "window polygon has no points"))
    return problems
  }
  if (!near(bounds.cx, want.center.x, 0.01) || !near(bounds.cy, want.center.y, 0.01)) {
    problems.push(
      fail(
        "window",
        `window centre is ${fmt({ x: bounds.cx, y: bounds.cy })}, baseline is ${fmt(want.center)}`,
      ),
    )
  }
  if (!near(bounds.width, want.width, 0.01) || !near(bounds.height, want.height, 0.01)) {
    problems.push(
      fail(
        "window",
        `window bounding box is ${bounds.width.toFixed(3)} x ${bounds.height.toFixed(3)}mm, baseline is ${want.width} x ${want.height}mm`,
      ),
    )
  }

  // the polygon must lie on the ideal rounded rectangle: proves shape + radius
  let worstDeviation = 0
  for (const p of points) {
    const d = Math.abs(signedDistanceToRoundedRect(p, { ...rect, radius: 0.5 }))
    if (d > worstDeviation) worstDeviation = d
  }
  if (worstDeviation > 0.05) {
    problems.push(
      fail(
        "window",
        `window polygon departs ${worstDeviation.toFixed(3)}mm from a R0.5 rounded rectangle (limit 0.05mm) - corner radius or shape is wrong`,
      ),
    )
  }
  const corners = measureRoundedCorners(points, { ...rect, radius: 0.5 })
  if (corners.segmentsPerCorner < 8) {
    problems.push(
      fail(
        "window",
        `window corners are sampled with ${corners.segmentsPerCorner} chords per 90 degrees, need at least 8`,
      ),
    )
  }
  if (corners.maxSagitta > 0.1) {
    problems.push(
      fail(
        "window",
        `window corner chord sagitta is ${corners.maxSagitta.toFixed(4)}mm, limit is 0.1mm`,
      ),
    )
  }
  if (corners.minRadius < 0.45 || corners.maxRadius > 0.55) {
    problems.push(
      fail(
        "window",
        `window corner radius measures ${corners.minRadius.toFixed(3)}-${corners.maxRadius.toFixed(3)}mm, must be R0.5`,
      ),
    )
  }

  return problems
}

// ---------------------------------------------------------------------------
// 2. No copper, no electrical content
// ---------------------------------------------------------------------------

const FORBIDDEN_TYPES = [
  "pcb_trace",
  "pcb_via",
  "pcb_smtpad",
  "pcb_plated_hole",
  "pcb_copper_pour",
  "pcb_copper_text",
  "pcb_solder_paste",
  "pcb_pad",
] as const

export const checkNoCopper = (circuitJson: CjElement[]): Problem[] => {
  const problems: Problem[] = []
  for (const type of FORBIDDEN_TYPES) {
    const found = circuitJson.filter((e) => e.type === type)
    if (found.length > 0) {
      problems.push(fail("copper", `panel carries ${found.length} ${type} element(s); it must be copper-free`))
    }
  }
  const components = circuitJson.filter((e) => e.type === "source_component" || e.type === "pcb_component")
  if (components.length > 0) {
    const names = components.map((c) => c.name ?? c.refdes ?? c.type).slice(0, 6)
    problems.push(fail("copper", `panel carries ${components.length} component(s): ${names.join(", ")}`))
  }
  const nets = circuitJson.filter((e) => e.type === "source_net" || e.type === "pcb_net")
  if (nets.length > 0) {
    problems.push(fail("copper", `panel declares ${nets.length} net(s); it has no electrical function`))
  }
  return problems
}

// ---------------------------------------------------------------------------
// 3. Built artwork: stroke width, margins, keepouts, and artwork equality
// ---------------------------------------------------------------------------

const PANEL_MARGIN = (art: PanelArtwork) => ({
  maxX: art.outline.width / 2 - SILK_EDGE_MARGIN,
  maxY: art.outline.height / 2 - SILK_EDGE_MARGIN,
})

const pathKey = (points: Pt[], strokeWidth: number): string =>
  points
    .map((p) => `${p.x.toFixed(3)},${p.y.toFixed(3)}`)
    .concat(strokeWidth.toFixed(3))
    .join("|")

/**
 * True when the built silk is exactly the artwork: same paths, same widths.
 * This is what makes "the preview draws the same geometry as the Gerbers" a
 * fact instead of a promise.
 */
export const artworkMismatch = (
  circuitJson: CjElement[],
  art: PanelArtwork,
): { missing: number; extra: number; built: number; art: number } => {
  const builtKeys = new Map<string, number>()
  for (const path of circuitJson.filter((e) => e.type === "pcb_silkscreen_path" && (e.layer ?? "top") === "top")) {
    const key = pathKey(path.route ?? [], path.stroke_width ?? 0)
    builtKeys.set(key, (builtKeys.get(key) ?? 0) + 1)
  }
  const artKeys = new Map<string, number>()
  for (const p of artworkPaths(art)) {
    const key = pathKey(p.path, p.strokeWidth)
    artKeys.set(key, (artKeys.get(key) ?? 0) + 1)
  }
  let missing = 0
  let extra = 0
  for (const [key, n] of artKeys) {
    const got = builtKeys.get(key) ?? 0
    if (got < n) missing += n - got
  }
  for (const [key, n] of builtKeys) {
    const want = artKeys.get(key) ?? 0
    if (n > want) extra += n - want
  }
  let built = 0
  for (const n of builtKeys.values()) built += n
  let total = 0
  for (const n of artKeys.values()) total += n
  return { missing, extra, built, art: total }
}

export const checkBuiltArtwork = (circuitJson: CjElement[], art: PanelArtwork): Problem[] => {
  const problems: Problem[] = []
  const silk = circuitJson.filter((e) => e.type === "pcb_silkscreen_path")
  if (silk.length === 0) {
    problems.push(fail("silk", "the panel has no printed artwork (no pcb_silkscreen_path elements)"))
  }
  const otherSilk = circuitJson.filter(
    (e) =>
      typeof e.type === "string" &&
      e.type.startsWith("pcb_silkscreen_") &&
      e.type !== "pcb_silkscreen_path",
  )
  if (otherSilk.length > 0) {
    const kinds = [...new Set(otherSilk.map((e) => e.type))]
    problems.push(
      fail("silk", `panel uses ${kinds.join(", ")}; only stroked paths give a guaranteed width`),
    )
  }

  // The rear is separate from the front-artwork comparison, but must match the
  // complete revision/date geometry exactly (including duplicate segments).
  const rear = silk.filter((e) => e.layer === "bottom")
  const expectedRear = panelRearPaths().map((p) => pathKey(p, PANEL_REAR_STROKE)).sort()
  const builtRear = rear.map((p) => pathKey(p.route ?? [], p.stroke_width ?? 0)).sort()
  if (JSON.stringify(expectedRear) !== JSON.stringify(builtRear)) {
    problems.push(fail("silk", "rear revision/date paths differ from the build identity"))
  }
  const margin = PANEL_MARGIN(art)
  const windowPolygon = art.window.polygon
  for (const path of silk) {
    const sw = path.stroke_width ?? 0
    const route: Pt[] = path.route ?? []
    if (route.length < 2) {
      problems.push(fail("silk", `path ${path.pcb_silkscreen_path_id} has ${route.length} points`))
      continue
    }
    if (!["top", "bottom"].includes(path.layer ?? "top")) {
      problems.push(fail("silk", `path ${path.pcb_silkscreen_path_id} is on layer ${path.layer}`))
    }
    if (sw < MIN_STROKE_WIDTH - 1e-9) {
      problems.push(
        fail(
          "silk",
          `path at ${fmt(route[0]!)} is ${sw.toFixed(3)}mm wide, the panel floor is ${MIN_STROKE_WIDTH}mm`,
        ),
      )
    }
    const b = polylineBounds(route)
    if (b && (b.minX - sw / 2 < -margin.maxX || b.maxX + sw / 2 > margin.maxX || b.minY - sw / 2 < -margin.maxY || b.maxY + sw / 2 > margin.maxY)) {
      problems.push(
        fail(
          "silk",
          `path at ${fmt(route[0]!)} reaches within ${SILK_EDGE_MARGIN}mm of the panel edge`,
        ),
      )
    }
    if (distPolylineToPolygon(route, windowPolygon) < SILK_WINDOW_MARGIN + sw / 2) {
      problems.push(fail("silk", `path at ${fmt(route[0]!)} crosses or crowds the OLED window`))
    }
    for (const keepout of art.keepouts) {
      if (distPointToPolyline(keepout, route) <= keepout.radius + sw / 2) {
        problems.push(fail("silk", `path at ${fmt(route[0]!)} sits under ${keepout.source}`))
      }
    }
  }

  // the printed data must BE the artwork: same paths, same widths
  const mismatch = artworkMismatch(circuitJson, art)
  if (mismatch.missing > 0) {
    problems.push(
      fail("silk", `${mismatch.missing} artwork path(s) are missing from the built circuit JSON`),
    )
  }
  if (mismatch.extra > 0) {
    problems.push(fail("silk", `${mismatch.extra} built path(s) are not in the artwork source`))
  }

  return problems
}

// ---------------------------------------------------------------------------
// 4. Artwork rules (independent of the build)
// ---------------------------------------------------------------------------

/** Drawn ink box of a legend, stroke included. */
export const legendInkHeight = (element: ArtElement): number => {
  const b = elementInkBounds(element)
  return b ? b.height : 0
}

export const checkArtwork = (art: PanelArtwork): Problem[] => {
  const problems: Problem[] = []
  const ids = new Set<string>()
  const margin = PANEL_MARGIN(art)

  for (const element of art.elements) {
    if (ids.has(element.id)) problems.push(fail("artwork", `duplicate element id ${element.id}`))
    ids.add(element.id)

    if (element.strokeWidth < MIN_STROKE_WIDTH - 1e-9) {
      problems.push(
        fail(
          "artwork",
          `${element.id} prints at ${element.strokeWidth}mm, the floor is ${MIN_STROKE_WIDTH}mm`,
        ),
      )
    }
    if (element.paths.length === 0) problems.push(fail("artwork", `${element.id} has no geometry`))

    const ink = elementInkBounds(element)
    if (!ink) continue
    if (ink.minX < -margin.maxX || ink.maxX > margin.maxX || ink.minY < -margin.maxY || ink.maxY > margin.maxY) {
      problems.push(fail("artwork", `${element.id} reaches within ${SILK_EDGE_MARGIN}mm of the panel edge`))
    }
    for (const path of element.paths) {
      if (distPolylineToPolygon(path, art.window.polygon) < SILK_WINDOW_MARGIN + element.strokeWidth / 2) {
        problems.push(fail("artwork", `${element.id} crosses or crowds the OLED window`))
      }
    }
    for (const keepout of art.keepouts) {
      if (elementClearanceToKeepout(element, keepout) < 0) {
        problems.push(fail("artwork", `${element.id} sits under ${keepout.source}`))
      }
    }

    // legends: measure what is actually drawn, not the nominal size
    if (element.kind === "legend" || element.kind === "brand" || element.kind === "note") {
      const cap = element.capHeight ?? 0
      const lines = element.lines?.length ?? 1
      const pitch = element.pitch ?? 0
      if (cap < MIN_LEGEND_HEIGHT - 1e-9) {
        problems.push(
          fail("artwork", `${element.id} cap height is ${cap}mm, the floor is ${MIN_LEGEND_HEIGHT}mm`),
        )
      }
      const tallest = Math.max(
        ...(element.lines ?? [element.text ?? ""]).map((line) => textInkHeight(line, cap)),
      )
      // geometry that is not from the alphabet (the monoline wordmark) declares
      // its own drawn height; everything else is measured against the glyphs
      const expected = element.inkHeight ?? (lines - 1) * pitch + tallest
      if (Math.abs(ink.height - element.strokeWidth - expected) > 0.15) {
        problems.push(
          fail(
            "artwork",
            `${element.id} draws ${(ink.height - element.strokeWidth).toFixed(3)}mm tall, ${lines} line(s) at ${cap}mm should draw ${expected.toFixed(3)}mm`,
          ),
        )
      }
      for (const line of element.lines ?? []) {
        const clearance = minGlyphClearance(line, cap, element.strokeWidth)
        if (Number.isFinite(clearance) && clearance < MIN_GLYPH_CLEARANCE - 1e-9) {
          problems.push(
            fail(
              "artwork",
              `${element.id} keeps only ${clearance.toFixed(3)}mm between neighbouring glyphs of ${JSON.stringify(line)}`,
            ),
          )
        }
      }
    }
  }

  // unrelated elements must not touch each other
  for (let i = 0; i < art.elements.length; i++) {
    for (let j = i + 1; j < art.elements.length; j++) {
      const a = art.elements[i]!
      const b = art.elements[j]!
      if (a.group === b.group) continue
      const d = distPathsToPaths(a.paths, b.paths) - (a.strokeWidth + b.strokeWidth) / 2
      if (d < MIN_ELEMENT_CLEARANCE - 1e-9) {
        problems.push(
          fail(
            "artwork",
            `${a.id} and ${b.id} print ${d.toFixed(3)}mm apart, they need ${MIN_ELEMENT_CLEARANCE}mm`,
          ),
        )
      }
    }
  }

  // every required legend identity is printed exactly once
  const texts = art.elements.filter((e) => e.text).map((e) => e.text!)
  for (const required of REQUIRED_LEGENDS) {
    const n = texts.filter((t) => t === required).length
    if (n !== 1) {
      problems.push(fail("legends", `legend ${JSON.stringify(required)} appears ${n} times, expected once`))
    }
  }
  const known = new Set<string>([...REQUIRED_LEGENDS, ...OPTIONAL_LEGENDS])
  for (const text of texts) {
    if (!known.has(text)) problems.push(fail("legends", `unexpected legend text ${JSON.stringify(text)}`))
  }

  // printed lines must spell the identity they claim
  for (const element of art.elements) {
    if (!element.text || !element.lines) continue
    const printed = element.lines.join(" ")
    if (printed !== element.text) {
      problems.push(
        fail(
          "legends",
          `${element.id} claims ${JSON.stringify(element.text)} but prints ${JSON.stringify(printed)}`,
        ),
      )
    }
  }

  // no legend may name a firmware function that has not been assigned
  const words = new Set<string>()
  for (const text of texts) {
    for (const word of text.split(/[^A-Za-z0-9]+/)) if (word) words.add(word)
  }
  for (const required of REQUIRED_WORDS) {
    if (!words.has(required)) {
      problems.push(fail("legends", `the panel does not print the word ${JSON.stringify(required)}`))
    }
  }

  return problems
}

// ---------------------------------------------------------------------------
// Which design is this file?
// ---------------------------------------------------------------------------

/**
 * Work out which panel design a built circuit JSON carries, from its printed
 * geometry - the built ink must match one design's artwork exactly. One source
 * of truth for the answer: the check script, the preview script and the release
 * script all call this, so a preview cannot be labelled with the wrong design.
 */
export const resolvePanelDesign = (
  circuitJson: CjElement[],
  requested?: string,
): { design: PanelDesignId | null; detected: PanelDesignId | null; problems: Problem[] } => {
  const problems: Problem[] = []
  let detected: PanelDesignId | null = null
  for (const id of PANEL_DESIGN_IDS) {
    const mismatch = artworkMismatch(circuitJson, panelArtwork(id))
    if (mismatch.missing === 0 && mismatch.extra === 0) {
      detected = id
      break
    }
  }

  if (requested !== undefined) {
    if (!isPanelDesignId(requested)) {
      problems.push(
        fail("design", `unknown design ${JSON.stringify(requested)}; use one of ${PANEL_DESIGN_IDS.join(", ")}`),
      )
      return { design: null, detected, problems }
    }
    if (detected !== requested) {
      const mismatch = artworkMismatch(circuitJson, panelArtwork(requested))
      problems.push(
        fail(
          "design",
          `the printed artwork in this file is ${detected ?? "neither known design"}, not ${requested} (${mismatch.missing} path(s) missing, ${mismatch.extra} unexpected) - refusing to label it as ${requested}`,
        ),
      )
      return { design: null, detected, problems }
    }
    return { design: requested, detected, problems }
  }

  if (detected) return { design: detected, detected, problems }
  problems.push(
    fail(
      "design",
      "cannot match the printed artwork to a panel design; rebuild the panel or pass --design",
    ),
  )
  return { design: null, detected: null, problems }
}

// ---------------------------------------------------------------------------
// 5. Everything, in one call
// ---------------------------------------------------------------------------

export const checkPanel = (opts: {
  circuitJson: CjElement[]
  fixture: PanelFixture
  art: PanelArtwork
}): Problem[] => [
  ...checkFrozenGeometry(opts.circuitJson, opts.fixture),
  ...checkNoCopper(opts.circuitJson),
  ...checkBuiltArtwork(opts.circuitJson, opts.art),
  ...checkArtwork(opts.art),
]

/** Panel outline box, for previews and print templates. */
export const outlineBounds = (art: PanelArtwork): Bounds =>
  boundsOf(
    -art.outline.width / 2,
    -art.outline.height / 2,
    art.outline.width / 2,
    art.outline.height / 2,
  )

// Froemix front panel - the artwork, as data.
//
// ONE source of truth for everything printed on the panel. This module holds no
// JSX and does no rendering: it turns the mechanical layout in `lib/layout.ts`
// plus a design style into polylines in panel millimetres (+X right, +Y up,
// origin at the panel centre). `panel.circuit.tsx` emits one
// `<silkscreenpath strokeWidth={...}>` per polyline, so the ink in the built
// circuit JSON is exactly what this file computed - the same paths the preview
// draws. No fonts, no text primitives: a stroked path cannot silently become a
// 0.099mm hairline the way `font_size * strokeWidthRatio` does.
//
// Two designs share one mechanical geometry:
//   `nord`   - the recommended default (NOT a selection the owner has made):
//              large monoline FROEMIX across the empty central band, hairline
//              zone rules, dial ticks in the top arc, small filled squares for
//              inputs and outlined squares for outputs.
//   `signal` - the bolder alternative: central wordmark flanked by a waveform,
//              bold rings on the outputs, corner brackets around the zones.
//
// The owner's design concepts are a visual reference only. Browser fonts and
// browser text metrics are not reproduced: every letter here is a single-stroke
// path from `@tscircuit/alphabet` (or a hand-placed monoline glyph), and every
// position is computed, so the checks can measure the ink exactly.

import {
  glyphAdvanceRatio,
  glyphWidthRatio,
  lineAlphabet,
  spaceWidthRatio,
} from "@tscircuit/alphabet"
import {
  BUTTON,
  HOLE,
  JACKS,
  JACK_LEDS,
  JOYSTICK,
  LED_AT,
  OLED,
  OLED_WINDOW,
  POTS,
  RAIL_HOLES,
} from "./layout"
import {
  distPathsToPaths,
  distPointToPolyline,
  polylineBounds,
  pt,
  roundedRectPolygon,
  unionBounds,
  type Bounds,
  type Pt,
} from "./panel-geometry"

// ---------------------------------------------------------------------------
// Mechanical contract (froemix/doc/FRONT-PANEL.md)
// ---------------------------------------------------------------------------

/** Finished panel outline; the base board stays 16HP behind it. */
export const PANEL_OUTLINE = { width: 101.6, height: 128.5, thickness: 2 } as const

/**
 * The OLED opening keeps its 30.5 x 16.0mm bounding box and its centre. Its
 * internal corners need R0.5 so JLCPCB can route it: `RectCutoutProps` has no
 * `cornerRadius` and core drops the field even when the circuit JSON schema
 * carries one, so the window is emitted as a rounded polygon instead.
 */
export const WINDOW_RADIUS = 0.5
/** 8 chords per 90 degree corner: 0.0024mm sagitta at R0.5, limit is 0.1mm. */
export const WINDOW_SEGMENTS_PER_CORNER = 8

export const panelWindow = () => {
  const centre = { x: OLED_WINDOW.x, y: OLED_WINDOW.y }
  const width = OLED.windowW
  const height = OLED.windowH
  const polygon = roundedRectPolygon({
    cx: centre.x,
    cy: centre.y,
    width,
    height,
    radius: WINDOW_RADIUS,
    segmentsPerCorner: WINDOW_SEGMENTS_PER_CORNER,
  })
  return { centre, width, height, radius: WINDOW_RADIUS, polygon }
}

// ---------------------------------------------------------------------------
// Print rules
// ---------------------------------------------------------------------------

/** JLCPCB's rigid-board floor is 0.15mm; the panel keeps a 0.2mm floor. */
export const MIN_STROKE_WIDTH = 0.2
/** JLCPCB's floor is 1.0mm character height; the panel keeps 1.2mm. */
export const MIN_LEGEND_HEIGHT = 1.2
/** Drawn clearance between two glyph strokes, and between unrelated elements. */
export const MIN_GLYPH_CLEARANCE = 0.1
export const MIN_ELEMENT_CLEARANCE = 0.3
/** Silk stays this far inside the routed outline. */
export const SILK_EDGE_MARGIN = 0.5
/** Silk stays this far from the OLED window opening. */
export const SILK_WINDOW_MARGIN = 0.5

/**
 * Extra letter spacing as a fraction of cap height. The alphabet's own pitch
 * leaves ~0.09 em between glyph ink, which is less than one stroke width, so
 * neighbouring letters would touch without it.
 */
export const LETTER_SPACING_RATIO = 0.22

/** Cap height of a glyph in alphabet units, measured from `H` (no overshoot). */
export const capHeightUnits = (() => {
  const h = lineAlphabet.H
  if (!h || h.length === 0) throw new Error("panel-art: alphabet has no H glyph")
  const ys = h.flatMap((s) => [s.y1, s.y2])
  return Math.max(...ys) - Math.min(...ys)
})()

// ---------------------------------------------------------------------------
// Hardware keepouts - what the printed legend must not sit under
// ---------------------------------------------------------------------------

/** Conservative installed-hardware diameters (doc/FRONT-PANEL.md). */
export const HARDWARE = {
  knobDiameter: 12.5,
  jackNutDiameter: 11.5,
  railWasherDiameter: 6.0,
  /** clearance around the small control holes: hole diameter + 0.6mm */
  holeClearance: 0.6,
} as const

export type Keepout = {
  id: string
  /** what the disc stands for, for failure messages */
  source: string
  x: number
  y: number
  radius: number
}

/**
 * Circular hardware keepouts. Every centre comes from `lib/layout.ts`, so a
 * moved control moves its keepout with it.
 */
export const hardwareKeepouts = (): Keepout[] => [
  ...POTS.map((p) => ({
    id: `keepout-knob-${p.name}`,
    source: `knob ${HARDWARE.knobDiameter}mm on ${p.name} (${p.label})`,
    x: p.x,
    y: p.y,
    radius: HARDWARE.knobDiameter / 2,
  })),
  ...JACKS.map((j) => ({
    id: `keepout-jack-${j.name}`,
    source: `jack nut ${HARDWARE.jackNutDiameter}mm on ${j.name} (${j.label})`,
    x: j.x,
    y: j.y,
    radius: HARDWARE.jackNutDiameter / 2,
  })),
  ...RAIL_HOLES.map((h, i) => ({
    id: `keepout-rail-${i}`,
    source: `rail washer ${HARDWARE.railWasherDiameter}mm on rail screw ${i + 1}`,
    x: h.x,
    y: h.y,
    radius: HARDWARE.railWasherDiameter / 2,
  })),
  ...[
    { id: "led", source: "panel LED", x: LED_AT.x, y: LED_AT.y, hole: HOLE.led },
    { id: "button", source: "MODE button", x: BUTTON.x, y: BUTTON.y, hole: HOLE.button },
    { id: "joystick", source: "navigation switch", x: JOYSTICK.x, y: JOYSTICK.y, hole: HOLE.joy },
    ...JACK_LEDS.map((l) => ({
      id: l.name.toLowerCase(),
      source: `jack LED ${l.name}`,
      x: l.x,
      y: l.y,
      hole: HOLE.jackLed,
    })),
  ].map((h) => ({
    id: `keepout-hole-${h.id}`,
    source: `${h.source} hole + ${HARDWARE.holeClearance}mm clearance`,
    x: h.x,
    y: h.y,
    radius: h.hole / 2 + HARDWARE.holeClearance / 2,
  })),
]

// ---------------------------------------------------------------------------
// Text: single-stroke ink from @tscircuit/alphabet
// ---------------------------------------------------------------------------

/**
 * Lay out one string as single-stroke polylines, glyph by glyph.
 *
 * `capHeight` is the printed height of a capital letter, in mm - the number the
 * manufacturing rules talk about. Glyph units are scaled so that `H` measures
 * exactly that, and placement is deferred to the caller, which anchors the
 * drawn ink box (never the nominal font size).
 */
export const textGlyphPaths = (
  text: string,
  capHeight: number,
): { char: string; paths: Pt[][] }[] => {
  const scale = capHeight / capHeightUnits
  const spacingUnits = (LETTER_SPACING_RATIO * capHeight) / scale
  const glyphs: { char: string; paths: Pt[][] }[] = []
  let cursor = 0
  for (const ch of text) {
    if (ch === " ") {
      glyphs.push({ char: ch, paths: [] })
      cursor += glyphAdvanceRatio[" "] ?? spaceWidthRatio
      continue
    }
    const glyph = lineAlphabet[ch]
    if (!glyph) throw new Error(`panel-art: the alphabet has no glyph for ${JSON.stringify(ch)}`)
    const paths: Pt[][] = glyph.map((s) => [
      pt((cursor + s.x1) * scale, s.y1 * scale),
      pt((cursor + s.x2) * scale, s.y2 * scale),
    ])
    glyphs.push({ char: ch, paths })
    cursor += (glyphAdvanceRatio[ch] ?? glyphWidthRatio) + spacingUnits
  }
  return glyphs
}

/** Lay out one string as single-stroke polylines, glyph order preserved. */
export const textPolylines = (text: string, capHeight: number): Pt[][] =>
  textGlyphPaths(text, capHeight).flatMap((g) => g.paths)

/**
 * Drawn ink height of one line, in mm: measured from the glyph outlines of the
 * characters actually used (round letters overshoot, `p` has a descender), not
 * from the cap height. The checks compare this with what the built paths draw.
 */
export const textInkHeight = (text: string, capHeight: number): number => {
  const scale = capHeight / capHeightUnits
  let min = Infinity
  let max = -Infinity
  for (const ch of text) {
    const glyph = lineAlphabet[ch]
    if (!glyph) continue
    for (const s of glyph) {
      min = Math.min(min, s.y1, s.y2)
      max = Math.max(max, s.y1, s.y2)
    }
  }
  return Number.isFinite(min) ? (max - min) * scale : 0
}

/** Drawn ink width of one line, in mm (first ink to last ink). */
export const textInkWidth = (text: string, capHeight: number): number => {
  const b = unionBounds(textPolylines(text, capHeight).map((p) => polylineBounds(p)))
  return b ? b.width : 0
}

/**
 * Smallest drawn gap between two neighbouring glyphs of one line, in mm.
 * Negative means the printed strokes touch or overlap. Measured from the same
 * geometry that gets printed, not from a font metric.
 */
export const minGlyphClearance = (text: string, capHeight: number, strokeWidth: number): number => {
  const glyphs = textGlyphPaths(text, capHeight)
  let best = Infinity
  for (let i = 0; i < glyphs.length - 1; i++) {
    const a = glyphs[i]!
    const b = glyphs[i + 1]!
    if (a.paths.length === 0 || b.paths.length === 0) continue
    const d = distPathsToPaths(a.paths, b.paths) - strokeWidth
    if (d < best) best = d
  }
  return best
}

export type Align = "center" | "left" | "right"

/** Move paths so the ink box centre (or one edge) lands on (x, y). */
export const placePaths = (paths: Pt[][], x: number, y: number, align: Align = "center"): Pt[][] => {
  const b = unionBounds(paths.map((p) => polylineBounds(p)))
  if (!b) return paths
  const from = align === "left" ? b.minX : align === "right" ? b.maxX : b.cx
  const dx = x - from
  const dy = y - b.cy
  return paths.map((p) => p.map((q) => pt(q.x + dx, q.y + dy)))
}

/** One printed line, anchored by its ink box. */
export const textPaths = (
  text: string,
  capHeight: number,
  x: number,
  y: number,
  align: Align = "center",
): Pt[][] => placePaths(textPolylines(text, capHeight), x, y, align)

// ---------------------------------------------------------------------------
// Artwork model
// ---------------------------------------------------------------------------

export type PanelDesignId = "nord" | "signal"

/** Recommended starting point for the owner's choice; not a selection. */
export const DEFAULT_PANEL_DESIGN: PanelDesignId = "nord"

export const PANEL_DESIGN_IDS: PanelDesignId[] = ["nord", "signal"]

export const isPanelDesignId = (v: string): v is PanelDesignId =>
  (PANEL_DESIGN_IDS as string[]).includes(v)

export type ArtKind =
  | "legend"
  | "brand"
  | "note"
  | "rule"
  | "bracket"
  | "tick"
  | "wave"
  | "ring"
  | "frame"
  | "mark"

/**
 * One printable element. `paths` is printed with one stroke width, one
 * `<silkscreenpath>` per path. `group` scopes collision checks: a bracket and
 * the legend it marks belong together, everything else must keep clear.
 */
export type ArtElement = {
  id: string
  group: string
  kind: ArtKind
  /** legend identity, exactly as it must stay human readable */
  text?: string
  /** printed lines, in draw order (two for a port legend) */
  lines?: string[]
  /** printed cap height (height of a capital letter) in mm */
  capHeight?: number
  /** baseline-to-baseline distance of a multi-line legend, in mm */
  pitch?: number
  /**
   * Intended drawn ink height before the stroke is added, in mm. Set for
   * geometry that is not from the alphabet (the monoline wordmark), so the
   * checks still measure the drawing against a declared intention.
   */
  inkHeight?: number
  strokeWidth: number
  paths: Pt[][]
}

export type PanelArtwork = {
  design: PanelDesignId
  outline: typeof PANEL_OUTLINE
  window: ReturnType<typeof panelWindow>
  keepouts: Keepout[]
  elements: ArtElement[]
}

/** Legend identities the panel must always carry, once each (doc/FRONT-PANEL.md). */
export const REQUIRED_LEGENDS = [
  "FROEMIX",
  "P1",
  "P2",
  "P3",
  "P4",
  "P5",
  "P6",
  "MODE",
  "AUDIO IN L",
  "AUDIO IN R",
  "AUDIO OUT L",
  "AUDIO OUT R",
  "SYNC IN",
  "SYNC OUT",
  "CV IN 1",
  "CV IN 2",
  "CV OUT 1",
  "CV OUT 2",
  "MIDI IN",
  "MIDI OUT",
] as const

/**
 * Legends a design may add on top of the required ones. Anything else printed
 * has to be named here first, so a legend cannot appear by accident.
 */
export const OPTIONAL_LEGENDS = [
  "NAV",
  "SELECT",
  "NAV/SELECT",
  "NAV / SELECT",
  "AUDIO",
  "SYNC",
  "CV",
  "MIDI",
  "STATUS",
  "REV 0.1",
  "daisy seed dsp - 20hp",
  "DAISY SEED DSP / 20 HP",
] as const

/**
 * Words every panel must print somewhere. "NAV" and "SELECT" may be two legends
 * or one, so they are checked as words. No legend names a firmware function:
 * none is assigned.
 */
export const REQUIRED_WORDS = [
  "FROEMIX",
  "P1",
  "P2",
  "P3",
  "P4",
  "P5",
  "P6",
  "NAV",
  "SELECT",
  "MODE",
  "AUDIO",
  "CV",
  "SYNC",
  "MIDI",
  "REV",
] as const

// ---------------------------------------------------------------------------
// Port data from lib/layout.ts
// ---------------------------------------------------------------------------

/** A port that sends signal out. */
export const isOutputJack = (label: string): boolean => /\bOUT\b/.test(label)

/** A port legend splits into its bus name and its direction. */
export const portLabelLines = (label: string): string[] => {
  const [bus, ...rest] = label.split(" ")
  const tail = rest.join(" ")
  return tail ? [bus, tail] : [label]
}

const jackByRef = (name: string) => {
  const jack = JACKS.find((j) => j.name === name)
  if (!jack) throw new Error(`panel-art: unknown jack ${name}`)
  return jack
}

/**
 * Bus zones, one per header. Both AUDIO pairs share a header and both CV pairs
 * share one; SYNC and MIDI own a pair each. `span` is the printed rule, which
 * stays inside the panel edge and clear of the rail washers.
 */
export const PORT_ZONES = [
  {
    id: "audio",
    text: "AUDIO",
    jacks: ["J3", "J4", "J5", "J6"],
    span: { nord: [-42.8, 9.2], signal: [-48.6, 15.6] },
  },
  {
    id: "sync",
    text: "SYNC",
    jacks: ["J7", "J8"],
    span: { nord: [24.4, 42.8], signal: [17.4, 48.6] },
  },
  {
    id: "cv",
    text: "CV",
    jacks: ["J9", "J10", "J11", "J12"],
    span: { nord: [-42.8, 9.2], signal: [-48.6, 15.6] },
  },
  {
    id: "midi",
    text: "MIDI",
    jacks: ["J13", "J14"],
    span: { nord: [24.4, 42.8], signal: [17.4, 48.6] },
  },
] as const

// ---------------------------------------------------------------------------
// Shared layout constants (artwork only - no hole moves with them)
// ---------------------------------------------------------------------------

const ROW = {
  /** zone header rule, above its jack row */
  headerDy: 7.7,
  /** ink-box centre of a pot legend, relative to its pot */
  potLabelDy: -9.6,
  /** ink-box centre of the NAV / SELECT and MODE legends */
  controlLabelDy: -9.6,
  /** two-line port legend: line pitch as a multiple of the cap height */
  linePitch: 1.8,
  /** bracket end tick length (signal) */
  bracketTick: 1.2,
  /** side of the in/out square mark (nord) */
  ioMarkSize: 1.2,
  /** gap between an in/out mark and its legend */
  ioMarkGap: 0.7,
} as const

/** Legend cap height used by both designs: above JLCPCB's 1.0mm, near the 1.5mm target. */
const LABEL_CAP = 1.5

// ---------------------------------------------------------------------------
// Design styles
// ---------------------------------------------------------------------------

export type PanelStyle = {
  id: PanelDesignId
  name: string
  note: string
  /** below-jack offset of the port legend ink box */
  portLabelDy: number
  portCap: number
  portStroke: number
  potCap: number
  potStroke: number
  controlCap: number
  controlStroke: number
  brand: { x: number; y: number; cap: number; stroke: number; mono: boolean }
  descriptor: { x: number; y: number; cap: number; stroke: number; rules: boolean; text: string }
  rev: { x: number; y: number; cap: number; stroke: number; align: Align }
  /** zone header treatment */
  header: {
    ruleStroke: number
    /** heading cap height */
    cap: number
    /** ink-centre offset from the rule, in mm (nord drops the word below the rule) */
    textDy: number
    /** gap each side of the heading, before the rule resumes */
    gap: number
    /** corner brackets at the zone ends (signal) */
    brackets: boolean
  }
  /** how inputs and outputs are distinguished without colour */
  ioMark: "squares" | "rings"
  groupStroke: number
  ringStroke: number
  potTicks: "arc" | "three"
  /** signal only */
  waveform: { y: number; amplitude: number; stroke: number; inner: number; outer: number } | null
}

export const PANEL_STYLES: Record<PanelDesignId, PanelStyle> = {
  nord: {
    id: "nord",
    name: "Nord",
    note: "recommended default: monoline wordmark across the central band, hairline rules, restrained marks",
    portLabelDy: -8.6,
    portCap: LABEL_CAP,
    portStroke: 0.25,
    potCap: LABEL_CAP,
    potStroke: 0.25,
    controlCap: LABEL_CAP,
    controlStroke: 0.25,
    brand: { x: 0, y: 3.6, cap: 8.6, stroke: 1.2, mono: true },
    descriptor: {
      x: 0,
      y: -61.45,
      cap: LABEL_CAP,
      stroke: 0.25,
      rules: false,
      text: "daisy seed dsp - 20hp",
    },
    rev: { x: 0, y: 60.75, cap: LABEL_CAP, stroke: 0.25, align: "center" },
    header: { ruleStroke: 0.25, cap: LABEL_CAP, textDy: 0.15, gap: 0.7, brackets: false },
    ioMark: "squares",
    groupStroke: 0.25,
    ringStroke: 0.25,
    potTicks: "arc",
    waveform: null,
  },
  signal: {
    id: "signal",
    name: "Signal",
    note: "bolder technical alternative: wordmark flanked by a waveform, output rings, zone brackets",
    portLabelDy: -9.1,
    portCap: 1.35,
    portStroke: 0.28,
    potCap: LABEL_CAP,
    potStroke: 0.3,
    controlCap: 1.4,
    controlStroke: 0.3,
    brand: { x: 0, y: 5.0, cap: 3.4, stroke: 0.75, mono: false },
    descriptor: {
      x: 0,
      y: -60.4,
      cap: 1.4,
      stroke: 0.3,
      rules: true,
      text: "DAISY SEED DSP / 20 HP",
    },
    rev: { x: 39.5, y: 57.0, cap: 1.3, stroke: 0.25, align: "right" },
    header: { ruleStroke: 0.5, cap: 1.3, textDy: 0, gap: 1.3, brackets: true },
    ioMark: "rings",
    groupStroke: 0.3,
    ringStroke: 0.55,
    potTicks: "three",
    waveform: { y: 5.0, amplitude: 0.8, stroke: 0.3, inner: 17.0, outer: 44.0 },
  },
}

// ---------------------------------------------------------------------------
// Drawing primitives
// ---------------------------------------------------------------------------

/** Closed circle as a polygon of chords: ink in a Gerber is a stroked path. */
export const circlePath = (cx: number, cy: number, radius: number, segments = 64): Pt[] => {
  const out: Pt[] = []
  for (let i = 0; i < segments; i++) {
    const a = (2 * Math.PI * i) / segments
    out.push(pt(cx + radius * Math.cos(a), cy + radius * Math.sin(a)))
  }
  return out
}

/** Stroke from radius `r0` to `r1` at an angle measured from +Y (12 o'clock). */
export const dialTick = (cx: number, cy: number, degFromTop: number, r0: number, r1: number): Pt[] => {
  const a = ((90 + degFromTop) * Math.PI) / 180
  return [
    pt(cx + r0 * Math.cos(a), cy + r0 * Math.sin(a)),
    pt(cx + r1 * Math.cos(a), cy + r1 * Math.sin(a)),
  ]
}

/** Stroke from radius `r0` to `r1` at an angle measured from +X. */
export const radialTick = (cx: number, cy: number, deg: number, r0: number, r1: number): Pt[] => {
  const a = (deg * Math.PI) / 180
  return [
    pt(cx + r0 * Math.cos(a), cy + r0 * Math.sin(a)),
    pt(cx + r1 * Math.cos(a), cy + r1 * Math.sin(a)),
  ]
}

/**
 * The `signal` motif: one line that runs sine, then triangle, then square - the
 * three shapes an oscillator can take. Sampled finely enough that a 0.3mm
 * stroke keeps the shape at 1:1.
 */
export const waveformPath = (x0: number, x1: number, y: number, amplitude: number): Pt[] => {
  const steps = Math.max(24, Math.round(Math.abs(x1 - x0) / 0.5))
  const points: Pt[] = []
  for (let i = 0; i <= steps; i++) {
    const x = x0 + ((x1 - x0) * i) / steps
    const t = (x - x0) / (x1 - x0)
    let v: number
    if (t < 1 / 3) {
      v = Math.sin(t * 3 * Math.PI * 4)
    } else if (t < 2 / 3) {
      const u = (t - 1 / 3) * 3
      v = (2 / Math.PI) * Math.asin(Math.sin(u * 3 * 2 * Math.PI))
    } else {
      const u = (t - 2 / 3) * 3
      v = Math.sin(u * 3 * 2 * Math.PI) >= 0 ? 1 : -1
    }
    points.push(pt(x, y + amplitude * v))
  }
  return points
}

/** An L corner (two arms) with its corner at (x, y) and arms of length `len`. */
export const cornerPath = (x: number, y: number, dx: -1 | 0 | 1, dy: -1 | 0 | 1, len: number): Pt[] => [
  pt(x + dx * len, y),
  pt(x, y),
  pt(x, y + dy * len),
]

/**
 * A square mark drawn with strokes, so it prints as a square and not as a
 * dot: a stroke has a round aperture, so a solid square is a short hatch and an
 * outlined square is a small closed contour.
 */
export const squareMark = (
  cx: number,
  cy: number,
  size: number,
  filled: boolean,
  strokeWidth: number,
): Pt[][] => {
  const h = size / 2
  if (!filled) {
    const s = h - strokeWidth / 2
    return [
      [pt(cx - s, cy - s), pt(cx + s, cy - s), pt(cx + s, cy + s), pt(cx - s, cy + s), pt(cx - s, cy - s)],
    ]
  }
  const count = 3
  const pitch = (size - strokeWidth) / (count - 1)
  const half = h - strokeWidth / 2
  const out: Pt[][] = []
  for (let i = 0; i < count; i++) {
    const y = cy - half + i * pitch
    out.push([pt(cx - half, y), pt(cx + half, y)])
  }
  return out
}

// ---------------------------------------------------------------------------
// Monoline wordmark glyphs (nord)
// ---------------------------------------------------------------------------

/**
 * Seven hand-placed single-stroke letterforms (F, R, O, E, M, I, X), cap height
 * 8 units, baseline at y = 8 in a y-down local frame. Coordinates are design
 * data, not a font: they are printed as they are written here, and the checks
 * measure the result.
 */
const MONO_GLYPHS: Record<string, { w: number; paths: Pt[][] }> = {
  F: {
    w: 7.5,
    paths: [
      [pt(0.65, 0), pt(0.65, 8)],
      [pt(0, 0.65), pt(7.5, 0.65)],
      [pt(0, 3.55), pt(5.9, 3.55)],
    ],
  },
  R: {
    w: 7.8,
    paths: [
      [pt(0.65, 0), pt(0.65, 8)],
      [pt(0, 0.65), pt(4.6, 0.65)],
      // semicircle bulging right, from (4.6,0.65) to (4.6,4.05), radius 1.7
      ...Array.from({ length: 11 }, (_, i) => {
        const a0 = -Math.PI / 2 + (Math.PI * i) / 11
        const a1 = -Math.PI / 2 + (Math.PI * (i + 1)) / 11
        return [
          pt(4.6 + 1.7 * Math.cos(a0), 2.35 + 1.7 * Math.sin(a0)),
          pt(4.6 + 1.7 * Math.cos(a1), 2.35 + 1.7 * Math.sin(a1)),
        ] as Pt[]
      }),
      [pt(0.65, 4.05), pt(4.6, 4.05)],
      [pt(3.4, 4.05), pt(6.85, 7.55)],
    ],
  },
  O: { w: 8.0, paths: [circlePath(4, 4, 3.35, 48)] },
  E: {
    w: 7.2,
    paths: [
      [pt(0.65, 0), pt(0.65, 8)],
      [pt(0, 0.65), pt(7.2, 0.65)],
      [pt(0, 4), pt(6.3, 4)],
      [pt(0, 7.35), pt(7.2, 7.35)],
    ],
  },
  M: {
    w: 10.6,
    paths: [[pt(0.65, 8), pt(0.65, 0), pt(5.3, 6.3), pt(9.95, 0), pt(9.95, 8)]],
  },
  I: { w: 1.3, paths: [[pt(0.65, 0), pt(0.65, 8)]] },
  X: {
    w: 8.6,
    paths: [
      [pt(0.55, 0.45), pt(8.05, 7.55)],
      [pt(8.05, 0.45), pt(0.55, 7.55)],
    ],
  },
}

/** Letter tracking in glyph units. */
const MONO_TRACKING = 3.2

/**
 * The `nord` wordmark: `FROEMIX` as monoline strokes, `capHeight` millimetres
 * tall, centred on (0, baselineY), about 75mm wide.
 */
export const wordmarkPaths = (
  text: string,
  capHeight: number,
  baselineY: number,
  centreX = 0,
): { paths: Pt[][]; width: number } => {
  const scale = capHeight / 8
  const glyphs = [...text].map((ch) => {
    const g = MONO_GLYPHS[ch]
    if (!g) throw new Error(`panel-art: the monoline wordmark has no glyph for ${JSON.stringify(ch)}`)
    return g
  })
  const total =
    glyphs.reduce((sum, g) => sum + g.w, 0) + MONO_TRACKING * (glyphs.length - 1)
  const paths: Pt[][] = []
  let cursor = -total / 2
  for (const g of glyphs) {
    for (const path of g.paths) {
      paths.push(
        path.map((p) => pt(centreX + (cursor + p.x) * scale, baselineY + (8 - p.y) * scale)),
      )
    }
    cursor += g.w + MONO_TRACKING
  }
  // centre the drawn ink, not the advance widths: the first and last glyph do
  // not fill their boxes, and an off-centre wordmark is visible on a panel
  const b = unionBounds(paths.map((p) => polylineBounds(p)))
  if (!b) return { paths, width: 0 }
  const dx = centreX - b.cx
  return {
    paths: paths.map((path) => path.map((p) => pt(p.x + dx, p.y))),
    width: b.width,
  }
}

// ---------------------------------------------------------------------------
// Artwork builder
// ---------------------------------------------------------------------------

type LabelSpec = {
  id: string
  group?: string
  kind?: ArtKind
  lines: string[]
  text: string
  capHeight: number
  strokeWidth: number
  x: number
  y: number
  align?: Align
  linePitch?: number
  inkHeight?: number
}

/**
 * A legend, printed on one or more lines. The identity in `text` is what the
 * checks match; the printed lines must join back into it, so the panel cannot
 * quietly print something else.
 */
const label = (spec: LabelSpec): ArtElement => {
  const lines = spec.lines
  const pitch = (spec.linePitch ?? ROW.linePitch) * spec.capHeight
  const blockHeight = (lines.length - 1) * pitch + spec.capHeight
  const top = spec.y + blockHeight / 2 - spec.capHeight / 2
  const paths: Pt[][] = []
  lines.forEach((text, i) => {
    paths.push(...textPaths(text, spec.capHeight, spec.x, top - i * pitch, spec.align ?? "center"))
  })
  return {
    id: spec.id,
    group: spec.group ?? spec.id,
    kind: spec.kind ?? "legend",
    text: spec.text,
    lines,
    capHeight: spec.capHeight,
    pitch,
    inkHeight: spec.inkHeight,
    strokeWidth: spec.strokeWidth,
    paths,
  }
}

/**
 * Zone header: a rule with the bus word in a gap. `nord` keeps it to a hairline
 * with the word tucked under it; `signal` adds a short bracket at each end.
 */
const zoneHeaders = (style: PanelStyle): ArtElement[] => {
  const out: ArtElement[] = []
  for (const zone of PORT_ZONES) {
    const jacks = zone.jacks.map(jackByRef)
    const y = jacks[0]!.y + ROW.headerDy
    const [left, right] = zone.span[style.id]
    const centre = (left + right) / 2
    const halfGap = textInkWidth(zone.text, style.header.cap) / 2 + style.header.gap
    const group = `zone-${zone.id}`
    out.push({
      id: `header-${zone.id}-rule-left`,
      group,
      kind: "rule",
      strokeWidth: style.header.ruleStroke,
      paths: [[pt(left, y), pt(centre - halfGap, y)]],
    })
    out.push({
      id: `header-${zone.id}-rule-right`,
      group,
      kind: "rule",
      strokeWidth: style.header.ruleStroke,
      paths: [[pt(centre + halfGap, y), pt(right, y)]],
    })
    out.push({
      id: `header-${zone.id}-label`,
      group,
      kind: "legend",
      text: zone.text,
      lines: [zone.text],
      capHeight: style.header.cap,
      pitch: 0,
      strokeWidth: style.groupStroke,
      paths: textPaths(zone.text, style.header.cap, centre, y + style.header.textDy),
    })
    if (style.header.brackets) {
      out.push({
        id: `header-${zone.id}-bracket-left`,
        group,
        kind: "bracket",
        strokeWidth: style.header.ruleStroke,
        paths: [cornerPath(left, y, 1, -1, ROW.bracketTick)],
      })
      out.push({
        id: `header-${zone.id}-bracket-right`,
        group,
        kind: "bracket",
        strokeWidth: style.header.ruleStroke,
        paths: [cornerPath(right, y, -1, -1, ROW.bracketTick)],
      })
    }
  }
  return out
}

/** Dial ticks in the top 90 degree arc, outside the knob silhouette (nord). */
const arcTicks = (style: PanelStyle): ArtElement[] =>
  POTS.map((pot) => {
    const paths: Pt[][] = []
    for (let deg = -45; deg <= 45; deg += 10) {
      paths.push(dialTick(pot.x, pot.y, deg, 6.55, deg === 0 ? 8.35 : 7.95))
    }
    return {
      id: `pot-ticks-${pot.name}`,
      group: `pot-marks-${pot.name}`,
      kind: "tick" as ArtKind,
      strokeWidth: style.groupStroke,
      paths,
    }
  })

/** Three ticks outside each knob, clear of fitted hardware (signal). */
const threeTicks = (style: PanelStyle): ArtElement[] =>
  POTS.map((pot) => ({
    id: `pot-ticks-${pot.name}`,
    group: `pot-marks-${pot.name}`,
    kind: "tick" as ArtKind,
    strokeWidth: style.groupStroke,
    paths: [
      radialTick(pot.x, pot.y, 60, 6.9, 8.3),
      radialTick(pot.x, pot.y, 90, 6.9, 8.4),
      radialTick(pot.x, pot.y, 120, 6.9, 8.3),
    ],
  }))

/** Arrow ticks around the navigation switch (nord). */
const navTicks = (style: PanelStyle): ArtElement => ({
  id: "nav-ticks",
  group: "nav-ticks",
  kind: "tick",
  strokeWidth: style.groupStroke,
  paths: [
    radialTick(JOYSTICK.x, JOYSTICK.y, 90, HOLE.joy / 2 + 0.8, 5.7),
    radialTick(JOYSTICK.x, JOYSTICK.y, 0, HOLE.joy / 2 + 0.8, 5.7),
    radialTick(JOYSTICK.x, JOYSTICK.y, 270, HOLE.joy / 2 + 0.8, 5.7),
    radialTick(JOYSTICK.x, JOYSTICK.y, 180, HOLE.joy / 2 + 0.8, 5.7),
  ],
})

/**
 * `nord`: a small square beside each port legend - solid for an input, an
 * outline for an output. Positioned from the measured legend ink, so it can
 * never drift into the text.
 */
const inputOutputSquares = (style: PanelStyle, portElements: ArtElement[]): ArtElement[] => {
  const out: ArtElement[] = []
  for (const jack of JACKS) {
    const legend = portElements.find((e) => e.id === `legend-${jack.name}`)
    if (!legend) continue
    const b = unionBounds(legend.paths.map((p) => polylineBounds(p)))
    if (!b) continue
    const output = isOutputJack(jack.label)
    // align with the first printed line, which is the top of the block
    const lineCentreY = b.maxY - (legend.capHeight ?? LABEL_CAP) / 2
    const cx = b.minX - ROW.ioMarkGap - ROW.ioMarkSize / 2
    out.push({
      id: `mark-${jack.name}`,
      group: `mark-${jack.name}`,
      kind: "mark",
      text: undefined,
      strokeWidth: output ? style.groupStroke : 0.42,
      paths: squareMark(cx, lineCentreY, ROW.ioMarkSize, !output, output ? style.groupStroke : 0.42),
    })
  }
  return out
}

/** `signal`: outputs carry a bold ring outside the nut; inputs a corner tick set. */
const jackMarks = (style: PanelStyle): ArtElement[] => {
  const out: ArtElement[] = []
  for (const jack of JACKS) {
    if (isOutputJack(jack.label)) {
      out.push({
        id: `ring-${jack.name}`,
        group: `mark-${jack.name}`,
        kind: "ring",
        strokeWidth: style.ringStroke,
        paths: [circlePath(jack.x, jack.y, 6.35)],
      })
    } else {
      const reach = 5.16
      const leg = 1.6
      for (const [sx, sy] of [
        [-1, -1],
        [1, -1],
        [-1, 1],
        [1, 1],
      ] as const) {
        const cx = jack.x + sx * reach
        const cy = jack.y + sy * reach
        out.push({
          id: `tick-${jack.name}-${sx}${sy}`,
          group: `mark-${jack.name}`,
          kind: "tick",
          strokeWidth: 0.35,
          paths: [cornerPath(cx, cy, -sx as -1 | 1, -sy as -1 | 1, leg)],
        })
      }
    }
  }
  return out
}

/** Hairline circle around a hole, drawn outside its hardware clearance. */
const holeRing = (id: string, x: number, y: number, radius: number, strokeWidth: number): ArtElement => ({
  id,
  group: id,
  kind: "ring",
  strokeWidth,
  paths: [circlePath(x, y, radius)],
})

/**
 * Build the artwork for one design. Pure: same input, same paths, so the
 * preview, the checks and the release see identical geometry.
 */
export const panelArtwork = (design: PanelDesignId = DEFAULT_PANEL_DESIGN): PanelArtwork => {
  if (!isPanelDesignId(design)) {
    throw new Error(
      `panel-art: unknown design ${JSON.stringify(design)}; expected one of ${PANEL_DESIGN_IDS.join(", ")}`,
    )
  }
  const style = PANEL_STYLES[design]
  const elements: ArtElement[] = []

  // --- port legends (two lines each; the full identity lives in `text`)
  const portElements = JACKS.map((jack) =>
    label({
      id: `legend-${jack.name}`,
      lines: portLabelLines(jack.label),
      text: jack.label,
      capHeight: style.portCap,
      strokeWidth: style.portStroke,
      x: jack.x,
      y: jack.y + style.portLabelDy,
    }),
  )
  elements.push(...portElements)

  // --- pot legends
  for (const pot of POTS) {
    elements.push(
      label({
        id: `legend-${pot.name}`,
        lines: [pot.label],
        text: pot.label,
        capHeight: style.potCap,
        strokeWidth: style.potStroke,
        x: pot.x,
        y: pot.y + ROW.potLabelDy,
      }),
    )
  }

  // --- navigation switch and MODE button
  elements.push(
    label({
      id: "legend-nav-select",
      lines: ["NAV / SELECT"],
      text: "NAV / SELECT",
      capHeight: style.controlCap,
      strokeWidth: style.controlStroke,
      x: JOYSTICK.x,
      y: JOYSTICK.y + ROW.controlLabelDy,
    }),
  )
  elements.push(
    label({
      id: "legend-mode",
      lines: [BUTTON.label],
      text: BUTTON.label,
      capHeight: style.controlCap,
      strokeWidth: style.controlStroke,
      x: BUTTON.x,
      y: BUTTON.y + ROW.controlLabelDy,
    }),
  )

  // --- brand: monoline wordmark (nord) or alphabet wordmark with waveform (signal)
  if (style.brand.mono) {
    const mark = wordmarkPaths("FROEMIX", style.brand.cap, style.brand.y)
    elements.push({
      id: "brand",
      group: "brand",
      kind: "brand",
      text: "FROEMIX",
      lines: ["FROEMIX"],
      capHeight: style.brand.cap,
      pitch: 0,
      inkHeight: style.brand.cap,
      strokeWidth: style.brand.stroke,
      paths: mark.paths,
    })
    // short rules on the wordmark baseline, one each side
    const y = style.brand.y
    elements.push({
      id: "brand-rule-left",
      group: "brand",
      kind: "rule",
      strokeWidth: style.groupStroke,
      paths: [[pt(-mark.width / 2 - 1.85 - 8.8, y), pt(-mark.width / 2 - 1.85, y)]],
    })
    elements.push({
      id: "brand-rule-right",
      group: "brand",
      kind: "rule",
      strokeWidth: style.groupStroke,
      paths: [[pt(mark.width / 2 + 1.85, y), pt(mark.width / 2 + 1.85 + 8.8, y)]],
    })
  } else {
    elements.push(
      label({
        id: "brand",
        kind: "brand",
        lines: ["FROEMIX"],
        text: "FROEMIX",
        capHeight: style.brand.cap,
        strokeWidth: style.brand.stroke,
        x: style.brand.x,
        y: style.brand.y,
      }),
    )
  }

  // --- descriptor and revision
  const descriptor = label({
    id: "note-descriptor",
    kind: "note",
    lines: [style.descriptor.text],
    text: style.descriptor.text,
    capHeight: style.descriptor.cap,
    strokeWidth: style.descriptor.stroke,
    x: style.descriptor.x,
    y: style.descriptor.y,
  })
  elements.push(descriptor)
  elements.push(
    label({
      id: "legend-rev",
      kind: "note",
      lines: ["REV 0.1"],
      text: "REV 0.1",
      capHeight: style.rev.cap,
      strokeWidth: style.rev.stroke,
      x: style.rev.x,
      y: style.rev.y,
      align: style.rev.align,
    }),
  )

  if (style.descriptor.rules) {
    const b = unionBounds(descriptor.paths.map((p) => polylineBounds(p)))!
    const y = style.descriptor.y
    elements.push({
      id: "descriptor-rule-left",
      group: "note-descriptor",
      kind: "rule",
      strokeWidth: style.descriptor.stroke,
      paths: [[pt(b.minX - 2.0 - 11.2, y), pt(b.minX - 2.0, y)]],
    })
    elements.push({
      id: "descriptor-rule-right",
      group: "note-descriptor",
      kind: "rule",
      strokeWidth: style.descriptor.stroke,
      paths: [[pt(b.maxX + 2.0, y), pt(b.maxX + 2.0 + 11.2, y)]],
    })
  }

  // --- the motif either side of the central wordmark (signal)
  if (style.waveform) {
    const w = style.waveform
    elements.push({
      id: "wave-left",
      group: "brand",
      kind: "wave",
      strokeWidth: w.stroke,
      paths: [waveformPath(-w.outer, -w.inner, w.y, w.amplitude)],
    })
    elements.push({
      id: "wave-right",
      group: "brand",
      kind: "wave",
      strokeWidth: w.stroke,
      paths: [waveformPath(w.inner, w.outer, w.y, w.amplitude)],
    })
  }

  // --- zone headers
  elements.push(...zoneHeaders(style))

  // --- hardware-area marks
  elements.push(...(style.potTicks === "arc" ? arcTicks(style) : threeTicks(style)))
  if (style.ioMark === "squares") {
    elements.push(navTicks(style))
    elements.push(...inputOutputSquares(style, portElements))
    elements.push(...JACK_LEDS.map((l) => holeRing(`ring-${l.name}`, l.x, l.y, 2.1, style.groupStroke)))
    elements.push(holeRing("ring-mode", BUTTON.x, BUTTON.y, 6.7, style.groupStroke))
    elements.push(holeRing("ring-status-led", LED_AT.x, LED_AT.y, 2.1, style.groupStroke))
    elements.push(
      label({
        id: "legend-status",
        lines: ["STATUS"],
        text: "STATUS",
        capHeight: style.controlCap,
        strokeWidth: style.controlStroke,
        x: LED_AT.x + 2.9,
        y: LED_AT.y,
        align: "left",
      }),
    )
    // hairline frame around the display window, drawn 0.8mm outside the opening
    const frame = {
      minX: OLED_WINDOW.x - OLED.windowW / 2 - 0.8,
      maxX: OLED_WINDOW.x + OLED.windowW / 2 + 0.8,
      minY: OLED_WINDOW.y - OLED.windowH / 2 - 0.8,
      maxY: OLED_WINDOW.y + OLED.windowH / 2 + 0.8,
    }
    elements.push({
      id: "frame-oled",
      group: "frame-oled",
      kind: "frame",
      strokeWidth: style.groupStroke,
      paths: [
        [
          pt(frame.minX, frame.minY),
          pt(frame.maxX, frame.minY),
          pt(frame.maxX, frame.maxY),
          pt(frame.minX, frame.maxY),
          pt(frame.minX, frame.minY),
        ],
      ],
    })
  } else {
    elements.push(...jackMarks(style))
  }

  return {
    design,
    outline: PANEL_OUTLINE,
    window: panelWindow(),
    keepouts: hardwareKeepouts(),
    elements,
  }
}

/** Every path of the artwork, flattened - used by the checks and the preview. */
export const artworkPaths = (art: PanelArtwork): { id: string; strokeWidth: number; path: Pt[] }[] =>
  art.elements.flatMap((e) =>
    e.paths.map((path, i) => ({ id: `${e.id}#${i}`, strokeWidth: e.strokeWidth, path })),
  )

/** Drawn ink box of one element, stroke included. */
export const elementInkBounds = (element: ArtElement): Bounds | null => {
  const b = unionBounds(element.paths.map((p) => polylineBounds(p)))
  if (!b) return null
  const h = element.strokeWidth / 2
  return {
    minX: b.minX - h,
    minY: b.minY - h,
    maxX: b.maxX + h,
    maxY: b.maxY + h,
    width: b.width + element.strokeWidth,
    height: b.height + element.strokeWidth,
    cx: b.cx,
    cy: b.cy,
  }
}

/** Clearance from an element's ink to a keepout disc (negative = inside). */
export const elementClearanceToKeepout = (element: ArtElement, keepout: Keepout): number => {
  let best = Infinity
  for (const path of element.paths) {
    const d = distPointToPolyline({ x: keepout.x, y: keepout.y }, path)
    if (d < best) best = d
  }
  return best - element.strokeWidth / 2 - keepout.radius
}

// Silkscreen acceptance for both boards, measured on the BUILT circuit JSON.
//
// No silkscreen claim is taken on trust here: every mark placed in
// `lib/silk.tsx` is measured against the pads, holes and ink the builder
// actually emitted (the Froemix `test_silk.test.ts` discipline). The suite
// covers the four owner asks - brand logo, hand-solder hints, pin-1 marks and
// polarity marks - plus the print rules (in-bounds, pad clearance, no text over
// text). It does NOT approve a physical print: the release parses the Legend
// Gerbers for the authoritative bounds.
import { expect, test } from "bun:test"
import { readFileSync } from "node:fs"
import {
  BOARD_SILK,
  BOARD_WORDMARK_STROKE,
  FRONT_SILK,
  OSHW_STROKE,
  PIN1_DOT_RADIUS,
  REAR_SILK,
  SILK_CLEARANCE,
  SILK_GERBER_GUARD,
  SILK_MARK_STROKE,
  oshwRoutes,
  silkInk,
  silkInkBounds,
  textBounds,
  wordmarkPaths,
  type BoardSilkSpec,
} from "./silk"
import { DISPLAY_CONCEPT, DISPLAY_VENDOR, LCD_MOUNT_HOLES, POWER_BUTTON } from "./layout"
import { MIN_STROKE_WIDTH, WORDMARK_TEXT, textInkWidth } from "./panel-art"
import { OSHW_ROW_PITCH } from "./oshw"

type Box = { x1: number; y1: number; x2: number; y2: number }
type Point = { x: number; y: number }

const circuit = (file: string) => JSON.parse(readFileSync(file, "utf8")) as any[]
const BOARDS: { spec: BoardSilkSpec; built: any[] }[] = [
  { spec: FRONT_SILK, built: circuit("dist/front/circuit.json") },
  { spec: REAR_SILK, built: circuit("dist/index/circuit.json") },
]

const box = (x1: number, y1: number, x2: number, y2: number): Box => ({ x1, y1, x2, y2 })
const boxDistance = (a: Box, b: Box): number =>
  Math.hypot(Math.max(b.x1 - a.x2, a.x1 - b.x2, 0), Math.max(b.y1 - a.y2, a.y1 - b.y2, 0))
const distanceToBox = (x: number, y: number, b: Box): number =>
  Math.hypot(Math.max(b.x1 - x, 0, x - b.x2), Math.max(b.y1 - y, 0, y - b.y2))
const boxesOverlap = (a: Box, b: Box): boolean => a.x1 < b.x2 && b.x1 < a.x2 && a.y1 < b.y2 && b.y1 < a.y2
/** Conservative segment-to-box distance; 0 when the two touch or cross. */
const segmentToBox = (a: Point, b: Point, target: Box): number => {
  const steps = Math.max(2, Math.ceil(Math.hypot(b.x - a.x, b.y - a.y) / 0.02))
  let best = Infinity
  for (let i = 0; i <= steps; i++) {
    const t = i / steps
    best = Math.min(best, distanceToBox(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, target))
  }
  return best
}

/** Axis-aligned extent of a pad or hole, including rotation and oval shapes. */
const openingBox = (pad: any): Box => {
  if (pad.type === "pcb_smtpad") {
    const angle = ((pad.ccw_rotation ?? 0) * Math.PI) / 180
    const halfW = (Math.abs(pad.width * Math.cos(angle)) + Math.abs(pad.height * Math.sin(angle))) / 2
    const halfH = (Math.abs(pad.width * Math.sin(angle)) + Math.abs(pad.height * Math.cos(angle))) / 2
    return box(pad.x - halfW, pad.y - halfH, pad.x + halfW, pad.y + halfH)
  }
  const size = Math.max(
    pad.outer_diameter ?? 0,
    pad.hole_diameter ?? 0,
    pad.outer_width ?? 0,
    pad.outer_height ?? 0,
    pad.hole_width ?? 0,
    pad.hole_height ?? 0,
    pad.rect_pad_width ?? 0,
    pad.rect_pad_height ?? 0,
  )
  return box(pad.x - size / 2, pad.y - size / 2, pad.x + size / 2, pad.y + size / 2)
}
// Mask openings only: SMT pads, plated holes and bare holes. Vias are tented by
// the fabricator (JLCPCB covers them by default), so silk over a covered via is
// a print-quality matter, not a mask-opening short; the DFM/CPL review owns it.
const OPENINGS = (built: any[]): Box[] =>
  built.filter((e) => ["pcb_smtpad", "pcb_plated_hole", "pcb_hole"].includes(e.type)).map(openingBox)

/** ref -> { source component, pads } for one built board. */
const parts = (built: any[]) => {
  const source = new Map(built.filter((e) => e.type === "source_component").map((e) => [e.source_component_id, e]))
  const pcbToSource = new Map(built.filter((e) => e.type === "pcb_component").map((e) => [e.pcb_component_id, e.source_component_id]))
  const out = new Map<string, { source: any; pads: any[] }>()
  for (const [pcbId, sourceId] of pcbToSource) {
    const component = source.get(sourceId)
    if (!component) continue
    out.set(component.name, { source: component, pads: [] })
  }
  for (const pad of built.filter((e) => ["pcb_smtpad", "pcb_plated_hole"].includes(e.type))) {
    const entry = out.get(source.get(pcbToSource.get(pad.pcb_component_id))?.name)
    if (entry) entry.pads.push(pad)
  }
  return out
}
const sourceNameOf = (built: any[], pcbComponentId: string): string | undefined => {
  const pcb = built.find((e) => e.type === "pcb_component" && e.pcb_component_id === pcbComponentId)
  if (!pcb) return undefined
  return built.find((e) => e.type === "source_component" && e.source_component_id === pcb.source_component_id)?.name
}

/** pad id -> source port hints ("anode", "cathode", "pin1", ...) for polarity checks. */
const portHints = (built: any[]) => {
  const sourcePort = new Map(built.filter((e) => e.type === "source_port").map((e) => [e.source_port_id, e]))
  const pcbPort = new Map(built.filter((e) => e.type === "pcb_port").map((e) => [e.pcb_port_id, e]))
  const out = new Map<string, string[]>()
  for (const pad of built.filter((e) => e.type === "pcb_smtpad" || e.type === "pcb_plated_hole")) {
    const port = pcbPort.get(pad.pcb_port_id)
    const hints: string[] = (port ? sourcePort.get(port.source_port_id)?.port_hints : undefined) ?? []
    out.set(pad.pcb_smtpad_id ?? pad.pcb_plated_hole_id, hints)
  }
  return out
}
const padHints = (hints: Map<string, string[]>, pad: any) => hints.get(pad.pcb_smtpad_id ?? pad.pcb_plated_hole_id) ?? []
const isPin1 = (pad: any): boolean => {
  const hints: string[] = pad.port_hints ?? []
  return hints.includes("pin1") || hints.includes("1")
}
const inkRadius = (mark: { source: "silk" | "footprint" }): number =>
  mark.source === "silk" ? PIN1_DOT_RADIUS + SILK_MARK_STROKE / 2 : 0.25 + 0.1 / 2

test("silkscreen ink stays inside both board outlines", () => {
  for (const { spec } of BOARDS) {
    // The release gate pads every Gerber mark by 0.5 mm on top of its own
    // 0.5 mm guard, so the coordinates must sit SILK_GERBER_GUARD inside.
    const guard = SILK_GERBER_GUARD
    const b = silkInkBounds(spec)
    expect(b).not.toBeNull()
    expect(b!.minX).toBeGreaterThanOrEqual(-spec.width / 2 + guard)
    expect(b!.maxX).toBeLessThanOrEqual(spec.width / 2 - guard)
    expect(b!.minY).toBeGreaterThanOrEqual(-spec.height / 2 + guard)
    expect(b!.maxY).toBeLessThanOrEqual(spec.height / 2 - guard)
    for (const mark of spec.texts) {
      const t = textBounds(mark)
      expect(t.minX).toBeGreaterThanOrEqual(-spec.width / 2 + guard)
      expect(t.maxX).toBeLessThanOrEqual(spec.width / 2 - guard)
      expect(t.minY).toBeGreaterThanOrEqual(-spec.height / 2 + guard)
      expect(t.maxY).toBeLessThanOrEqual(spec.height / 2 - guard)
    }
  }
})

test("every emitted text is centre-anchored, printable and on the documented side", () => {
  for (const { spec, built } of BOARDS) {
    const texts = built.filter((e) => e.type === "pcb_silkscreen_text")
    expect(texts.length).toBeGreaterThan(0)
    for (const t of texts) {
      // `center_left` lays glyphs backwards off the board on the bottom layer
      // (docs/decisions.md); `center` mirrors onto itself.
      expect(t.anchor_alignment).toBe("center")
      expect(t.font_size).toBeGreaterThanOrEqual(0.4)
    }
    for (const mark of spec.texts) {
      const hit = built.filter(
        (t) =>
          t.type === "pcb_silkscreen_text" &&
          t.text === mark.text &&
          Math.abs(t.anchor_position.x - mark.x) < 1e-6 &&
          Math.abs(t.anchor_position.y - mark.y) < 1e-6 &&
          Math.abs(t.font_size - mark.fontSize) < 1e-6 &&
          t.layer === spec.layer,
      )
      expect({ text: mark.text, hits: hit.length }).toEqual({ text: mark.text, hits: 1 })
    }
  }
})

test("board silk is exactly the table: no stray text, dot or artwork", () => {
  for (const { spec, built } of BOARDS) {
    const looseTexts = built
      .filter((e) => e.type === "pcb_silkscreen_text" && !e.pcb_component_id)
      .map((t) => `${t.text}@${t.anchor_position.x},${t.anchor_position.y}`)
    // The rear board's display-carrier marks are emitted from index.circuit.tsx
    // on its TOP face (the face the carrier bolts to); they are derived from the
    // same LCD_MOUNT_HOLES, so they belong in this table's whitelist.
    const carrierTexts = spec.board === "rear" ? [
      ...LCD_MOUNT_HOLES.map((h, i) =>
        `LCD${i + 1}@${h.x + (h.x < DISPLAY_CONCEPT.x ? 9 : -9)},${h.y + (h.y > DISPLAY_CONCEPT.y ? 5 : -5)}`),
      `DISPLAY CARRIER - 4x M2.5@${DISPLAY_CONCEPT.x},${DISPLAY_CONCEPT.y + DISPLAY_VENDOR.outline.height / 2 + 8.5}`,
    ] : []
    const expectedTexts = [...spec.texts.map((m) => `${m.text}@${m.x},${m.y}`), ...carrierTexts]
    expect(looseTexts.sort()).toEqual(expectedTexts.sort())

    const dots = built.filter((e) => e.type === "pcb_silkscreen_circle" && !e.pcb_component_id && e.radius === PIN1_DOT_RADIUS)
    const tableDots = spec.pin1.filter((m) => m.source === "silk")
    expect(dots).toHaveLength(tableDots.length)
    for (const mark of tableDots) {
      const hit = dots.filter(
        (d) =>
          Math.abs(d.center.x - mark.x) < 1e-6 &&
          Math.abs(d.center.y - mark.y) < 1e-6 &&
          d.radius === PIN1_DOT_RADIUS &&
          d.stroke_width === SILK_MARK_STROKE &&
          d.layer === spec.layer,
      )
      expect({ ref: mark.ref, hits: hit.length }).toEqual({ ref: mark.ref, hits: 1 })
    }

    const loosePaths = built.filter((e) => e.type === "pcb_silkscreen_path" && !e.pcb_component_id)
    const boardPaths = loosePaths.filter((p) => p.layer === spec.layer)
    expect(boardPaths).toHaveLength(wordmarkPaths(spec).length + oshwRoutes(spec).length)
    for (const p of boardPaths) {
      expect([spec.wordmark.stroke, spec.oshw.stroke]).toContain(p.stroke_width)
    }
    // The rear board's TOP-face carrier ticks are the only loose ink that is
    // not this module's: four L-shaped corner marks 4 mm outboard of the
    // module outline, from index.circuit.tsx.
    const offLayer = loosePaths.filter((p) => p.layer !== spec.layer)
    if (spec.board === "rear") {
      const ticks = [[-1, -1], [1, -1], [-1, 1], [1, 1]].map(([sx, sy]) => {
        const cx = DISPLAY_CONCEPT.x + sx * (DISPLAY_VENDOR.outline.width / 2 + 4)
        const cy = DISPLAY_CONCEPT.y + sy * (DISPLAY_VENDOR.outline.height / 2 + 4)
        return [{ x: cx, y: cy - sy * 6 }, { x: cx, y: cy }, { x: cx - sx * 6, y: cy }]
      })
      expect(offLayer).toHaveLength(ticks.length)
      for (const route of ticks) {
        const hit = offLayer.filter((p) => p.layer === "top" &&
          p.route.length === route.length &&
          p.route.every((q: Point, i: number) =>
            Math.abs(q.x - route[i]!.x) < 1e-6 && Math.abs(q.y - route[i]!.y) < 1e-6))
        expect(hit).toHaveLength(1)
        expect(hit[0]!.stroke_width).toBe(0.2)
      }
    } else {
      expect(offLayer).toHaveLength(0)
    }
  }
})

test("silkscreen keeps clearance from every pad and hole", () => {
  for (const { spec, built } of BOARDS) {
    const openings = OPENINGS(built)
    for (const mark of spec.texts) {
      const bounds = textBounds(mark)
      const t = box(bounds.minX, bounds.minY, bounds.maxX, bounds.maxY)
      for (const o of openings) {
        const gap = boxDistance(t, o)
        expect({ text: mark.text, pad: o, gap: gap >= SILK_CLEARANCE }).toEqual({
          text: mark.text,
          pad: o,
          gap: true,
        })
      }
    }
    for (const group of silkInk(spec)) {
      for (const route of group.paths) {
        for (let i = 1; i < route.length; i++) {
          for (const o of openings) {
            expect({ ink: "path", clear: segmentToBox(route[i - 1]!, route[i]!, o) - group.stroke / 2 >= SILK_CLEARANCE }).toEqual({
              ink: "path",
              clear: true,
            })
          }
        }
      }
    }
  }
})

test("no silkscreen text prints over text, or under the brand ink", () => {
  for (const { spec, built } of BOARDS) {
    const tableTexts = new Set(spec.texts.map((m) => m.text))
    const texts = built
      .filter((e) => e.type === "pcb_silkscreen_text")
      .map((t) => {
        const b = textBounds({ text: t.text, x: t.anchor_position.x, y: t.anchor_position.y, fontSize: t.font_size })
        return { text: t.text, layer: t.layer, box: box(b.minX, b.minY, b.maxX, b.maxY) }
      })
    let checkedPairs = 0
    for (let i = 0; i < texts.length; i++) {
      for (let j = i + 1; j < texts.length; j++) {
        if (texts[i]!.layer !== texts[j]!.layer) continue
        // Pairs this wave can move: at least one side is a table mark.
        if (!tableTexts.has(texts[i]!.text) && !tableTexts.has(texts[j]!.text)) continue
        checkedPairs++
        expect({
          pair: [texts[i]!.text, texts[j]!.text].sort(),
          overlap: boxesOverlap(texts[i]!.box, texts[j]!.box),
        }).toEqual({ pair: [texts[i]!.text, texts[j]!.text].sort(), overlap: false })
      }
    }
    expect(checkedPairs).toBeGreaterThan(0)
    // Brand ink must not run through a text box either.
    for (const group of silkInk(spec)) {
      for (const route of group.paths) {
        for (let i = 1; i < route.length; i++) {
          const segBox = box(
            Math.min(route[i - 1]!.x, route[i]!.x) - group.stroke / 2,
            Math.min(route[i - 1]!.y, route[i]!.y) - group.stroke / 2,
            Math.max(route[i - 1]!.x, route[i]!.x) + group.stroke / 2,
            Math.max(route[i - 1]!.y, route[i]!.y) + group.stroke / 2,
          )
          for (const t of texts) {
            if (t.layer !== spec.layer) continue
            expect({ text: t.text, underInk: boxesOverlap(segBox, t.box) }).toEqual({ text: t.text, underInk: false })
          }
        }
      }
    }
  }
})

test("every hand-soldered through-hole part has a ref and a hint at its own position", () => {
  for (const { spec, built } of BOARDS) {
    const byRef = parts(built)
    // Through-hole = at least one plated hole and no SMT pad: exactly the set
    // scripts/sourcing_bom.py routes to the hand-solder BOM.
    const tht = [...byRef.entries()]
      .filter(([, p]) => p.pads.some((pad) => pad.type === "pcb_plated_hole") && p.pads.every((pad) => pad.type !== "pcb_smtpad"))
      .map(([ref]) => ref)
      .sort()
    expect(tht).toEqual(
      spec.board === "front"
        ? ["J1", "J2", "J3", "J4", "J5", "J_PWR", "SW1", "SW2", "SW3", "SW4", "SW5", "SW6", "SW7", "U6"]
        : [
          // Bare plated probe points are through-hole "parts" with no SMT pad
          // and no BOM line (doNotPlace); each still prints its own net label.
          "J_FRONT", "J_PI", "J_PICO_L", "J_PICO_R",
          "TP1", "TP10", "TP11", "TP12", "TP13", "TP14", "TP15", "TP2",
          "TP3", "TP4", "TP5", "TP6", "TP7", "TP8", "TP9",
        ],
    )
    const texts = built.filter((e) => e.type === "pcb_silkscreen_text")
    for (const ref of tht) {
      // A reference designator: the auto refdes, or a table mark starting with
      // the ref (J1 THT / J5 SOCKET THT / U6: LUGS FIRST).
      expect({ ref, refPrinted: texts.some((t) => t.text === ref || t.text.startsWith(`${ref} `) || t.text.startsWith(`${ref}:`)) }).toEqual({
        ref,
        refPrinted: true,
      })
      const hints = spec.texts.filter((m) => m.role === "hint" && m.part === ref)
      // One or more hints per hand-soldered part. SW7 (the Pi power button)
      // carries three operating hints; J_PWR (a 2-pad right-angle header) carries
      // none and is identified by its printed refdes. Every hint that DOES exist
      // must sit at its own part, within 4 mm of its pads plus its own ink.
      if (ref !== "J_PWR") expect({ ref, someHint: hints.length > 0 }).toEqual({ ref, someHint: true })
      else expect({ ref, hints: hints.length }).toEqual({ ref, hints: 0 })
      const inkNear = built.filter(
        (e) =>
          e.pcb_component_id &&
          (e.type === "pcb_silkscreen_path" || e.type === "pcb_silkscreen_circle") &&
          sourceNameOf(built, e.pcb_component_id) === ref,
      )
      const inkBoxes = inkNear.flatMap((e) =>
        e.type === "pcb_silkscreen_path"
          ? e.route.map((p: Point) => box(p.x, p.y, p.x, p.y))
          : [box(e.center.x - e.radius, e.center.y - e.radius, e.center.x + e.radius, e.center.y + e.radius)],
      )
      const partBox = [...byRef.get(ref)!.pads.map(openingBox), ...inkBoxes].reduce(
        (acc, b) => box(Math.min(acc.x1, b.x1), Math.min(acc.y1, b.y1), Math.max(acc.x2, b.x2), Math.max(acc.y2, b.y2)),
        box(Infinity, Infinity, -Infinity, -Infinity),
      )
      // SW7's three operating hints sit in the free strip to the RIGHT of its
      // body (9...15 mm from its centre); every other hint is within 4 mm.
      const reach = ref === "SW7" ? 12 : 4
      for (const hint of hints) {
        expect({
          ref,
          text: hint.text,
          atPart:
            hint.x >= partBox.x1 - reach &&
            hint.x <= partBox.x2 + reach &&
            hint.y >= partBox.y1 - reach &&
            hint.y <= partBox.y2 + reach,
        }).toEqual({ ref, text: hint.text, atPart: true })
      }
    }
  }
})

test("every IC and multi-pin connector has a pin-1 mark beside pin 1", () => {
  for (const { spec, built } of BOARDS) {
    const byRef = parts(built)
    // A pin-1 mark is a printed dot (this table) or a printed "1" (same table);
    // both are measured against the built pads below.
    const marks = [
      ...spec.pin1.map((m) => {
        const r = m.source === "silk" ? PIN1_DOT_RADIUS + SILK_MARK_STROKE / 2 : 0.25 + 0.1 / 2
        return { ref: m.ref, x: m.x, y: m.y, ink: box(m.x - r, m.y - r, m.x + r, m.y + r), from: m.source }
      }),
      ...spec.texts
        .filter((t) => t.role === "pin1" && t.part)
        .map((t) => {
          const b = textBounds(t)
          return { ref: t.part!, x: t.x, y: t.y, ink: box(b.minX, b.minY, b.maxX, b.maxY), from: "text" as const }
        }),
    ]
    const needMark = [...byRef.entries()]
      .filter(([, p]) => {
        const ftype = p.source.ftype
        // ICs, transistors, connectors and the two multi-contact panel
        // controls (U5 the 5-way nav switch, U6 the encoder - modelled as
        // switches in their imports, so they are `simple_switch` here). A
        // 4-pad pushbutton is not in this set. Connectors with two pads (J_PWR)
        // ARE marked: the owner asked for pin 1 on every multi-pin part.
        const needsOrientation = ftype === "simple_chip" || ftype === "simple_transistor"
          || ftype === "simple_connector" || ftype === "simple_switch"
          || ftype === "simple_push_button"
        return needsOrientation && p.pads.length >= 2
      })
      .map(([ref]) => ref)
      .sort()
    const marked = [...new Set(marks.map((m) => m.ref))].sort()
    expect({ board: spec.board, marked }).toEqual({ board: spec.board, marked: needMark })
    for (const mark of marks) {
      const pads = byRef.get(mark.ref)!.pads
      const pin1 = pads.filter(isPin1)
      expect({ ref: mark.ref, pin1Pads: pin1.length }).toEqual({ ref: mark.ref, pin1Pads: 1 })
      const ranked = pads
        .map((pad) => ({ pad, d: boxDistance(mark.ink, openingBox(pad)) }))
        .sort((a, b) => a.d - b.d)
      expect({ ref: mark.ref, from: mark.from, nearestIsPin1: ranked[0]!.pad === pin1[0] }).toEqual({
        ref: mark.ref,
        from: mark.from,
        nearestIsPin1: true,
      })
      for (const o of OPENINGS(built)) {
        expect({ ref: mark.ref, from: mark.from, clear: boxDistance(mark.ink, o) >= SILK_CLEARANCE }).toEqual({
          ref: mark.ref,
          from: mark.from,
          clear: true,
        })
      }
    }
    // Each part needs at least one mark sitting right at pin 1; a header's
    // printed "1" may sit further out, its dot is the touching mark.
    for (const ref of needMark) {
      const { pads } = byRef.get(ref)!
      const pin1 = pads.filter(isPin1)[0]!
      const nearest = Math.min(...marks.filter((m) => m.ref === ref).map((m) => boxDistance(m.ink, openingBox(pin1))))
      expect({ ref, atPin1: nearest <= 1.2 }).toEqual({ ref, atPin1: true })
    }
  }
})

test("every diode prints its cathode band on the cathode side", () => {
  for (const { built } of BOARDS) {
    const byRef = parts(built)
    // Diodes only: the 0805 LED footprint draws a bracket around pin 1, which
    // is the ANODE on this part, so LED polarity is carried by the A/K letters
    // below instead of a band (see the LED test).
    const diodes = [...byRef.entries()].filter(([, p]) => p.source.ftype === "simple_diode")
    expect(diodes.length).toBeGreaterThan(0)
    const hints = portHints(built)
    for (const [ref, { pads }] of diodes) {
      const cathode = pads.filter((p) => padHints(hints, p).includes("cathode"))
      const anode = pads.filter((p) => padHints(hints, p).includes("anode"))
      expect({ ref, cathode: cathode.length, anode: anode.length }).toEqual({ ref, cathode: 1, anode: 1 })
      const verticalBars = built
        .filter((e) => e.type === "pcb_silkscreen_path" && sourceNameOf(built, e.pcb_component_id) === ref)
        .flatMap((e) => e.route.slice(1).map((p: Point, i: number) => ({ a: e.route[i] as Point, b: p })))
        .filter((seg) => Math.abs(seg.a.x - seg.b.x) < 1e-9 && Math.abs(seg.a.y - seg.b.y) > 0.3)
      expect({ ref, bars: verticalBars.length > 0 }).toEqual({ ref, bars: true })
      // The band (or the bar of the diode symbol) prints against the cathode
      // pad, on the cathode side of the symbol: the triangle mass sits on the
      // anode side, so this is existential, not "every bar".
      const cathodeBox = openingBox(cathode[0]!)
      const anodeBox = openingBox(anode[0]!)
      const bandOnCathode = verticalBars.some((seg) => {
        const bar = box(seg.a.x, Math.min(seg.a.y, seg.b.y), seg.a.x, Math.max(seg.a.y, seg.b.y))
        return boxDistance(bar, cathodeBox) < 1.0 && boxDistance(bar, cathodeBox) < boxDistance(bar, anodeBox)
      })
      expect({ ref, bandOnCathode }).toEqual({ ref, bandOnCathode: true })
    }
  }
})

test("the three indicator LEDs print anode and cathode letters by their pads", () => {
  for (const { spec, built } of BOARDS) {
    const leds = [...parts(built).entries()].filter(([, p]) => p.source.ftype === "simple_led")
    for (const [ref, { pads }] of leds) {
      const hints = portHints(built)
      const anode = pads.find((p) => padHints(hints, p).includes("anode"))!
      const cathode = pads.find((p) => padHints(hints, p).includes("cathode"))!
      const marks = spec.texts.filter((m) => m.role === "polarity" && m.part === ref)
      expect({ ref, marks: marks.map((m) => m.text).sort() }).toEqual({ ref, marks: ["A", "K"] })
      for (const mark of marks) {
        const toAnode = Math.hypot(mark.x - anode.x, mark.y - anode.y)
        const toCathode = Math.hypot(mark.x - cathode.x, mark.y - cathode.y)
        expect({ ref, text: mark.text, onOwnSide: mark.text === "A" ? toAnode < toCathode : toCathode < toAnode }).toEqual({
          ref,
          text: mark.text,
          onOwnSide: true,
        })
      }
    }
    // No polarized capacitor exists, so no +/- marks are due. This list is the
    // tripwire: a future electrolytic fails here and forces +/- marks into the
    // silk table instead of shipping unmarked.
    const caps = [...new Set([...parts(built).values()].filter((p) => p.source.ftype === "simple_capacitor").map((p) => p.source.manufacturer_part_number))]
    expect(caps.sort()).toEqual(["CC0603KRX7R9BB104", "CL31A106KBHNNNE"])
  }
})

test("the board wordmark reuses the panel letterforms at bold silk weight", () => {
  expect(WORDMARK_TEXT).toBe("mnkybox")
  expect(BOARD_WORDMARK_STROKE).toBe(0.7)
  expect(BOARD_WORDMARK_STROKE).toBeGreaterThanOrEqual(MIN_STROKE_WIDTH)
  expect(OSHW_STROKE).toBeGreaterThanOrEqual(MIN_STROKE_WIDTH)
  // The scanline rows must merge into a solid mark: pitch below the aperture.
  expect(OSHW_ROW_PITCH).toBeLessThan(OSHW_STROKE)
  for (const { spec } of BOARDS) {
    const paths = wordmarkPaths(spec)
    expect(paths.length).toBeGreaterThan(10)
    // The front marks are rotated 90 deg into the left strip; the rear ones are
    // upright. Measure the ink ACROSS its own spine and along it, not on a fixed
    // axis: the x projection of rotated ink is its cap height, not its width.
    const turned = Math.abs((spec.wordmark.rotation ?? 0) % 180) === 90
    const across = paths.flat().map((p) => (turned ? p.x : p.y))
    const along = paths.flat().map((p) => (turned ? p.y : p.x))
    // Same letterforms as the panel source: identical drawn ink width.
    expect(Math.max(...along) - Math.min(...along)).toBeCloseTo(textInkWidth(WORDMARK_TEXT, spec.wordmark.capHeight), 6)
    const oshw = oshwRoutes(spec)
    expect(oshw.length).toBeGreaterThan(20)
    const oshwAcross = oshw.flat().map((p) => (turned ? p.x : p.y))
    const oshwAlong = oshw.flat().map((p) => (turned ? p.y : p.x))
    // They share one spine and sit one after the other along it, never touching.
    expect(Math.abs((turned ? spec.oshw.x : spec.oshw.y) - (turned ? spec.wordmark.x : spec.wordmark.y))).toBeLessThan(0.01)
    const gap = Math.min(...along) - Math.max(...oshwAlong)
    expect(gap).toBeGreaterThan(1.5)
    expect(Math.max(...oshwAcross) - Math.min(...oshwAcross)).toBeLessThanOrEqual(spec.oshw.width)
  }
})

test("the panel POWER button has a label and a jumper tag on the front board", () => {
  const label = FRONT_SILK.texts.find((m) => m.text === "POWER" && m.role === "note")
  expect(label?.fontSize).toBe(1.4)
  // In the free strip to the RIGHT of SW7 (body ends x 59.25), above J5's
  // refdes; never on the button itself.
  expect(label!.x).toBeGreaterThan(POWER_BUTTON.x + 7.25)
  expect(Math.abs(label!.y - POWER_BUTTON.y)).toBeLessThan(3)
  expect(label!.text).toBe("POWER")
  const sw7 = FRONT_SILK.texts.filter((m) => m.role === "hint" && m.part === "SW7")
  expect(sw7.map(m => m.text).sort()).toEqual(["HOLD=OFF", "PRESS=ON", "SW7 THT"])
  const jpwr = FRONT_SILK.texts.filter((m) => m.role === "hint" && m.part === "J_PWR")
  expect(jpwr).toHaveLength(0)
  // The header itself gets a pin-1 dot; that is the current table.
  expect(FRONT_SILK.pin1.some((m) => m.ref === "J_PWR")).toBe(true)
})

test("the U6 hint matches the qualified process in docs/assembly.md", () => {
  const hint = FRONT_SILK.texts.find((m) => m.part === "U6" && m.role === "hint")
  expect(hint?.text).toBe("U6: LUGS FIRST")
  const assembly = readFileSync("../docs/assembly.md", "utf8")
  expect(assembly).toMatch(/lugs first/i)
  expect(assembly).toMatch(/preheat/i)
  expect(BOARD_SILK.front).toBe(FRONT_SILK)
  expect(BOARD_SILK.rear).toBe(REAR_SILK)
  expect(FRONT_SILK.layer).toBe("top")
  expect(REAR_SILK.layer).toBe("bottom")
})

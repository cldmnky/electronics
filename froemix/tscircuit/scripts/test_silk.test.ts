import { expect, test } from "bun:test"
import { readFileSync } from "node:fs"
import { wordmarkPaths as euroWordmarkPaths } from "../../euro/lib/panel-art"
import * as euroInfo from "../../euro/lib/build-info"
import { BOARD_REVISION, buildDate } from "../lib/build-info"
import { wordmarkPaths } from "../lib/wordmark"
import { oshwPaths } from "../lib/oshw"
import { BAR_HALF_LENGTH, CATHODE_BARS, DOT_RADIUS, MARKER_STROKE, PIN1_DOTS } from "../lib/polarity-markers"

const circuit: any[] = JSON.parse(readFileSync("dist/index/circuit.json", "utf8"))
const bottomTexts = circuit.filter(e => e.type === "pcb_silkscreen_text" && e.layer === "bottom")
const boardPaths = circuit.filter(e => e.type === "pcb_silkscreen_path" && e.layer === "bottom" && !e.pcb_component_id)
const key = (route: {x: number; y: number}[]) => route.map(p => `${p.x.toFixed(3)},${p.y.toFixed(3)}`).join("|")

test("base board prints the current revision, UTC date and hand-assembly notes", () => {
  for (const text of [`REV ${BOARD_REVISION}`, buildDate(), "HAND: CN1-4 H1 J1 J2", "C24 C25 U16 DAISY SOCKETS", "SET J1 BEFORE STACK"]) {
    expect(bottomTexts.filter(e => e.text === text)).toHaveLength(1)
  }
})

test("the base copy of the identity and wordmark matches the euro source", () => {
  expect(BOARD_REVISION).toBe(euroInfo.BOARD_REVISION)
  expect(buildDate()).toBe(euroInfo.buildDate())
  expect(wordmarkPaths("FROEMIX", 1.35, -46.5, -29.5)).toEqual(
    euroWordmarkPaths("FROEMIX", 1.35, -46.5, -29.5),
  )
})

test("the Nord wordmark and OSHW gear are emitted on the rear", () => {
  const expected = [
    ...euroWordmarkPaths("FROEMIX", 1.35, -46.5, -29.5).paths.map(path => path.map(p => ({ x: -59-p.x, y: p.y }))),
    ...oshwPaths(-34, -32, 7),
    ...CATHODE_BARS.map(({ x, y }) => [{ x, y: y - BAR_HALF_LENGTH }, { x, y: y + BAR_HALF_LENGTH }]),
  ].map(key).sort()
  const actual = boardPaths.map(p => key(p.route)).sort()
  expect(actual).toEqual(expected)
  expect(boardPaths.every(p => p.stroke_width === 0.2)).toBe(true)
  // The brand must remain above the J2 rear courtyard's top edge (-51.05).
  const barKeys = new Set(CATHODE_BARS.map(({ x, y }) => key([{ x, y: y - BAR_HALF_LENGTH }, { x, y: y + BAR_HALF_LENGTH }])))
  expect(Math.min(...boardPaths.filter(p => !barKeys.has(key(p.route))).flatMap(p => p.route.map((q: any) => q.y)))).toBeGreaterThan(-51.05 + 0.1)
})

test("polarity marks are emitted by pad 1 with clearance from bottom SMT pads", () => {
  const dots = circuit.filter(e => e.type === "pcb_silkscreen_circle" && e.layer === "bottom" && !e.pcb_component_id)
  expect(dots).toHaveLength(PIN1_DOTS.length)
  const pads = circuit.filter(e => e.type === "pcb_smtpad" && e.layer === "bottom")
  const holes = circuit.filter(e => ["pcb_via", "pcb_hole", "pcb_plated_hole"].includes(e.type))
  const sourceById = new Map(circuit.filter(e => e.type === "source_component").map(e => [e.source_component_id, e.name]))
  const refByPcbId = new Map(circuit.filter(e => e.type === "pcb_component").map(e => [e.pcb_component_id, sourceById.get(e.source_component_id)]))
  // Conservative axis-aligned bounding boxes include rotated pads and circles.
  const distance = (x: number, y: number, p: any) => {
    const w = p.width ?? 2 * p.radius
    const h = p.height ?? 2 * p.radius
    if (!Number.isFinite(w) || !Number.isFinite(h)) throw new Error(`Unknown pad geometry: ${p.pcb_smtpad_id}`)
    const angle = (p.ccw_rotation ?? 0) * Math.PI / 180
    const halfW = (Math.abs(w * Math.cos(angle)) + Math.abs(h * Math.sin(angle))) / 2
    const halfH = (Math.abs(w * Math.sin(angle)) + Math.abs(h * Math.cos(angle))) / 2
    return Math.hypot(Math.max(Math.abs(x - p.x) - halfW, 0), Math.max(Math.abs(y - p.y) - halfH, 0))
  }
  const check = (ref: string, x: number, y: number, inkRadius: number) => {
    const ranked = pads.map(p => ({ p, d: distance(x, y, p) })).sort((a, b) => a.d - b.d)
    expect(refByPcbId.get(ranked[0]!.p.pcb_component_id)).toBe(ref)
    expect(ranked[0]!.p.port_hints).toContain("pin1")
    // 0.20 mm ink-to-copper; allow for fab mask expansion as well.
    expect(ranked[0]!.d - inkRadius).toBeGreaterThanOrEqual(0.20)
    expect(ranked[0]!.d).toBeLessThan(0.75)
    for (const { p, d } of ranked) expect(d - (p.soldermask_margin ?? 0) - inkRadius).toBeGreaterThanOrEqual(0.10)
    for (const hole of holes) {
      // Enclose circular/rectangular plated holes conservatively; vias may be
      // exposed in the mask even when they are not used as assembly pads.
      const radius = Math.max(hole.outer_diameter ?? 0, hole.hole_diameter ?? 0,
        Math.hypot(hole.rect_pad_width ?? 0, hole.rect_pad_height ?? 0)) / 2
      expect(Math.hypot(x - hole.x, y - hole.y) - radius - inkRadius).toBeGreaterThanOrEqual(0.20)
    }
  }
  for (const { ref, x, y } of PIN1_DOTS) {
    expect(dots.filter(e => Math.abs(e.center.x - x) < 1e-6 && Math.abs(e.center.y - y) < 1e-6 && e.radius === DOT_RADIUS && e.stroke_width === MARKER_STROKE)).toHaveLength(1)
    check(ref, x, y, DOT_RADIUS + MARKER_STROKE / 2)
  }
  for (const { ref, x, y } of CATHODE_BARS) {
    for (const dy of [-BAR_HALF_LENGTH, 0, BAR_HALF_LENGTH]) check(ref, x, y + dy, MARKER_STROKE / 2)
  }
})

test("explicit build dates reject invalid calendar dates", () => {
  const previous = process.env.FROEMIX_BUILD_DATE
  try {
    process.env.FROEMIX_BUILD_DATE = "2025-02-29"
    expect(() => buildDate()).toThrow()
    process.env.FROEMIX_BUILD_DATE = "2024-02-29"
    expect(buildDate()).toBe("2024-02-29")
  } finally {
    if (previous === undefined) delete process.env.FROEMIX_BUILD_DATE
    else process.env.FROEMIX_BUILD_DATE = previous
  }
})

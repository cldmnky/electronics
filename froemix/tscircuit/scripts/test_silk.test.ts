import { expect, test } from "bun:test"
import { readFileSync } from "node:fs"
import { wordmarkPaths as euroWordmarkPaths } from "../../euro/lib/panel-art"
import * as euroInfo from "../../euro/lib/build-info"
import { BOARD_REVISION, buildDate } from "../lib/build-info"
import { wordmarkPaths } from "../lib/wordmark"
import { oshwPaths } from "../lib/oshw"

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
  ].map(key).sort()
  const actual = boardPaths.map(p => key(p.route)).sort()
  expect(actual).toEqual(expected)
  expect(boardPaths.every(p => p.stroke_width === 0.2)).toBe(true)
  // The brand must remain above the J2 rear courtyard's top edge (-51.05).
  expect(Math.min(...boardPaths.flatMap(p => p.route.map((q: any) => q.y)))).toBeGreaterThan(-51.05 + 0.1)
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

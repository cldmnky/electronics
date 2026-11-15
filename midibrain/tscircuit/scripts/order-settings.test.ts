// The four JLCPCB order dimensions against the PCB sources.
//
// `tscircuit/lib/layout.ts` is the authority for every board size, and
// `jlc-order.json` describes the same four boards that an order is placed for:
// a stale size there orders a board that does not exist. Nothing else compares
// them, because `scripts/sourcing_bom.py` checks the placement counts only and
// `scripts/order_sheet.py` prints the order file unchanged.
//
// These tests compare width, height and thickness, which are geometry. Finish,
// mask colour and assembly side stay order choices and are not checked here.
// This is a source-consistency check, not a fabrication approval: all 31
// physical acceptance items stay pending.
import { describe, expect, test } from "bun:test"
import { readFileSync } from "node:fs"
import { join } from "node:path"
import { FRONT_PCB, PANEL, PI_PANEL, REAR_PCB } from "../lib/layout"

const ORDER_PATH = join(import.meta.dir, "../../jlc-order.json")

interface OrderBoard {
  dimensions_mm?: number[]
  thickness_mm?: number
}

const order = JSON.parse(readFileSync(ORDER_PATH, "utf8")) as Record<
  string,
  { board?: OrderBoard }
>

const BOARDS = [
  { key: "front", constant: "FRONT_PCB", source: FRONT_PCB },
  { key: "rear", constant: "REAR_PCB", source: REAR_PCB },
  { key: "panel", constant: "PANEL", source: PANEL },
  { key: "pi-panel", constant: "PI_PANEL", source: PI_PANEL },
] as const

describe("jlc-order.json board sizes agree with lib/layout.ts", () => {
  for (const { key, constant, source } of BOARDS) {
    test(`${key}: size and thickness are ${constant}`, () => {
      const board = order[key]?.board
      expect(board).toBeDefined()
      expect({ dimensions_mm: board!.dimensions_mm }).toEqual({
        dimensions_mm: [source.width, source.height],
      })
      expect({ thickness_mm: board!.thickness_mm }).toEqual({
        thickness_mm: source.thickness,
      })
    })
  }

  test("every ordered board is one of the four checked constants", () => {
    // A fifth board in the order file would otherwise be ordered unchecked.
    const ordered = Object.keys(order).filter(name => !name.startsWith("_"))
    expect(ordered.sort()).toEqual(BOARDS.map(board => board.key).sort())
  })

  test("the rear board is the 148 mm board", () => {
    // Named explicitly: the rear size is the value this test exists for, so a
    // silent edit of both REAR_PCB and the order file still shows up here.
    expect(order.rear?.board?.dimensions_mm).toEqual([148, 112])
    expect(REAR_PCB.width).toBe(148)
  })
})

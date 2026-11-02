import { expect, test } from "bun:test"
import { readFileSync } from "node:fs"
import { BOARD_REVISION, buildDate } from "../../shared/build-info"

test("built euro board has the same rear revision and build date", () => {
  const circuit: any[] = JSON.parse(readFileSync("dist/index/circuit.json", "utf8"))
  const rear = circuit.filter(e => e.type === "pcb_silkscreen_text" && e.layer === "bottom")
  for (const text of [`FROEMIX EURO REV ${BOARD_REVISION}`, buildDate()]) {
    expect(rear.filter(e => e.text === text)).toHaveLength(1)
  }
})

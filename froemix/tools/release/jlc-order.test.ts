// The JLCPCB order settings must describe the files the release actually
// writes, and the SMT upload BOM/CPL must not contain through-hole parts.
//
// Why: the order page takes three uploads with confusingly similar names
// (`froemix-bom-smt.csv`, `froemix-bom-jlc.csv`, `froemix-bom-hand.csv`). The
// canonical settings pointed at the combined BOM for a while, which would have
// asked JLCPCB to place ten hand-fitted parts. This test ties the canonical
// settings to the release script's actual output names, to the through-hole
// list in both places, and to the BUILT circuit's designator count.
//
// It needs the built base board (`dist/index/circuit.json`); `make gate` or
// `make build` produces it.

import { describe, expect, test } from "bun:test"
import { existsSync, readFileSync } from "node:fs"
import { join } from "node:path"

/** froemix/ */
const ROOT = join(import.meta.dir, "..", "..")
const ORDER = JSON.parse(readFileSync(join(ROOT, "jlc-order.json"), "utf8"))
const RELEASE = readFileSync(join(ROOT, "tscircuit", "scripts", "release.sh"), "utf8")
const CIRCUIT = join(ROOT, "tscircuit", "dist", "index", "circuit.json")

if (!existsSync(CIRCUIT)) {
  throw new Error(
    `${CIRCUIT} is missing: run \`make build\` (or \`make gate\`) before the JLC order tests`,
  )
}

const circuit: any[] = JSON.parse(readFileSync(CIRCUIT, "utf8"))
const sourceComponents = circuit.filter((element) => element.type === "source_component")
const pcbComponents = circuit.filter((element) => element.type === "pcb_component")
const nameById = new Map<string, string>(
  sourceComponents.map((element) => [element.source_component_id, element.name] as [string, string]),
)
const componentIdByName = new Map<string, string>(
  sourceComponents.map((element) => [element.name, element.source_component_id] as [string, string]),
)
const placed = pcbComponents.filter((element) => !element.do_not_place)
/** The designators that reach the BOM and CPL: placed, not DNP. */
const designators: string[] = placed.map((element) => nameById.get(element.source_component_id) ?? "?")
const layerByName = new Map<string, string>(
  pcbComponents.map((element) => [nameById.get(element.source_component_id) ?? "?", element.layer ?? "?"] as [string, string]),
)

/** The through-hole set as the release script declares it. */
function releaseThroughHole(): string[] {
  const match = RELEASE.match(/^THT = \{([^}]*)\}/m)
  if (!match) throw new Error("tscircuit/scripts/release.sh no longer declares `THT = { ... }`")
  return [...match[1]!.matchAll(/"([A-Za-z0-9_]+)"/g)].map((entry) => entry[1]!)
}

describe("canonical order files", () => {
  test("the SMT upload BOM is the file the release writes", () => {
    expect(ORDER.assembly.files.bom).toBe("froemix-bom-smt.csv")
    // the release script must actually write that name
    expect(RELEASE).toContain('"froemix-bom-smt.csv"')
  })

  test("the CPL upload is the file the release writes", () => {
    expect(ORDER.assembly.files.cpl).toBe("froemix-cpl-jlc.csv")
    expect(RELEASE).toContain('"froemix-cpl-jlc.csv"')
  })

  test("combined and hand BOMs are marked as reference lists, not uploads", () => {
    const files = ORDER.assembly.files
    expect(files.bom_reference).toBe("froemix-bom-jlc.csv")
    expect(files.hand_parts_reference).toBe("froemix-bom-hand.csv")
    for (const reference of [files.bom_reference, files.hand_parts_reference]) {
      expect(reference).not.toBe(files.bom)
      expect(reference).not.toBe(files.cpl)
    }
    expect(RELEASE).toContain('"froemix-bom-jlc.csv"')
    expect(RELEASE).toContain('"froemix-bom-hand.csv"')
  })
})

describe("through-hole parts stay out of the SMT upload", () => {
  const tht = releaseThroughHole()

  test("the order settings and the release script agree on the through-hole list", () => {
    expect([...ORDER.assembly.through_hole_parts].sort()).toEqual([...tht].sort())
  })

  test("there are ten through-hole parts, and they all exist in the built circuit", () => {
    expect(tht.length).toBe(10)
    expect(ORDER.assembly.expected_counts.through_hole_excluded).toBe(tht.length)
    for (const ref of tht) expect(designators).toContain(ref)
  })

  test("the release excludes them from the CPL before writing it", () => {
    // `smt = [r for r in pnp_rows if r["Designator"] not in THT]` is the filter
    // the exported CPL comes from; a change to that line must fail here.
    expect(RELEASE).toMatch(/smt = \[r for r in pnp_rows if r\["Designator"\] not in THT\]/)
  })

  test("the release writes the SMT BOM from the SMT set only", () => {
    // `smt_refs = set(layer) - THT` feeds the grouped SMT BOM
    expect(RELEASE).toMatch(/smt_refs = set\(layer\) - THT/)
  })

  test("every part in the SMT set is on the bottom, single-sided", () => {
    // The single-sided contract: all 107 placements JLCPCB makes are on the
    // bottom. The through-hole parts are hand-fitted; they may sit on either
    // side (the mating headers on top, H1/J2/C24/C25/U16 hanging off the
    // bottom), which is exactly why they must not reach the SMT CPL.
    const smt = designators.filter((ref) => !tht.includes(ref))
    expect(smt.length).toBe(ORDER.assembly.expected_counts.cpl_smt_placements)
    for (const ref of smt) expect(layerByName.get(ref)).toBe("bottom")
  })
})

describe("counts add up against the built circuit", () => {
  const tht = releaseThroughHole()
  const expected = ORDER.assembly.expected_counts

  test("the built circuit carries every designator the order counts", () => {
    expect(designators.length).toBe(expected.bom_placements)
  })

  test("117 placements = 107 SMT + 10 through-hole", () => {
    expect(expected.bom_placements).toBe(117)
    expect(expected.cpl_smt_placements).toBe(107)
    expect(expected.through_hole_excluded).toBe(10)
    expect(expected.cpl_smt_placements + expected.through_hole_excluded).toBe(expected.bom_placements)
    expect(designators.length - tht.length).toBe(expected.cpl_smt_placements)
  })

  test("the release's own CPL guard uses the same SMT count", () => {
    expect(RELEASE).toContain("len(smt) != 107")
  })

  test("the Daisy module, fiducials and test points never reach the BOM or CPL", () => {
    expect(ORDER.assembly.do_not_place).toContain("DaisySeed1")
    for (const name of ORDER.assembly.do_not_place) expect(designators).not.toContain(name)
    const dnp = pcbComponents.filter((element) => element.do_not_place).map((element) => nameById.get(element.source_component_id))
    // Daisy + 4 fiducials + 8 test points: the difference between the 130
    // components on the board and the 117 in the order.
    expect(dnp.length).toBe(13)
    expect(sourceComponents.length - dnp.length).toBe(expected.bom_placements)
  })

  test("the built board's placed designators are unique", () => {
    expect(new Set(designators).size).toBe(designators.length)
  })
})

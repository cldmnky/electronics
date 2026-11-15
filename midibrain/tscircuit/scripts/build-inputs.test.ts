// `tsci build` picks its inputs from `includeBoardFiles` in
// tscircuit.config.json. The pinned CLI's DEFAULT is
//   ["**/*.board.tsx", "**/*.circuit.tsx", "**/*.circuit.json"]
// (verified in @tscircuit/cli 0.1.2454,
// dist/cli/main.js `DEFAULT_BOARD_FILE_PATTERNS`), so any `*.circuit.json` left
// lying beside the sources - e.g. the `front.circuit.circuit.json` a
// `tsci check`/`tsci export` run can drop in the CWD, which midibrain/.gitignore
// already ignores - was discovered as one more circuit to build. This project
// has five board sources; the config lists them explicitly so a generated file
// can never join the build.
//
// These assertions read the config and the working tree only; the build itself
// is the integration test (`bun run build` reports "Circuits 5 passed").
import { describe, expect, test } from "bun:test"
import { readdirSync, readFileSync } from "node:fs"
import { resolve } from "node:path"

const ROOT = resolve(import.meta.dir, "..")

/** The five intended circuit sources, in build order. */
const INTENDED = [
  "assembly.circuit.tsx",
  "front.circuit.tsx",
  "index.circuit.tsx",
  "panel.circuit.tsx",
  "pi-panel.circuit.tsx",
]

const config = JSON.parse(
  readFileSync(resolve(ROOT, "tscircuit.config.json"), "utf8"),
) as { includeBoardFiles?: string[] }

const patterns = config.includeBoardFiles ?? []

describe("tsci build inputs", () => {
  test("tscircuit.config.json exists and lists board files", () => {
    expect(patterns.length).toBeGreaterThan(0)
  })

  test("the build selects exactly the five intended circuit sources", () => {
    expect([...patterns].sort()).toEqual([...INTENDED].sort())
  })

  test("every listed input is a literal, existing .circuit.tsx path", () => {
    for (const pattern of patterns) {
      // A glob here would be how a generated file sneaks back in.
      expect(pattern).not.toMatch(/[*?[\]{}]/)
      expect(pattern.endsWith(".circuit.tsx")).toBe(true)
      expect(readdirSync(ROOT)).toContain(pattern)
    }
  })

  test("a new root-level board source cannot be silently unbuilt", () => {
    const rootSources = readdirSync(ROOT).filter(f => f.endsWith(".circuit.tsx"))
    expect([...rootSources].sort()).toEqual([...INTENDED].sort())
  })

  test("generated circuit JSON beside a source is not selected as an input", () => {
    // The names a generated build/check artifact takes. None may match an
    // include pattern: the list is literal, so equality is the whole rule.
    for (const generated of [
      "front.circuit.circuit.json",
      "panel.circuit.circuit.json",
      "dist/front/circuit.json",
      "front.circuit.json",
    ]) {
      expect(patterns.some(pattern => generated === pattern)).toBe(false)
      expect(patterns.some(pattern => generated.endsWith(pattern))).toBe(false)
    }
  })
})

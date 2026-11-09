// The release entry points must fail closed, and they must fail BEFORE they
// build, export or delete anything.
//
// Every test here puts a sentinel file where a release would write (fab/ for the
// base and euro releases, euro/dist/panel-release/ for a direct panel release),
// runs a real entry point with the real acceptance record (every item pending),
// and then proves three things:
//   * the entry point exited nonzero,
//   * it refused before the gate/build step (its banner never appears),
//   * both sentinels are still there, so nothing was deleted.
//
// The runs are fast because the acceptance gate is the first thing each entry
// point does; no network and no tscircuit build happens.

import { afterAll, beforeAll, describe, expect, test } from "bun:test"
import { spawnSync } from "node:child_process"
import { existsSync, mkdirSync, readFileSync, readdirSync, rmSync, writeFileSync } from "node:fs"
import { dirname, join } from "node:path"

/** froemix/ */
const ROOT = join(import.meta.dir, "..", "..")
const FAB = join(ROOT, "fab")
const EURO_FAB = join(FAB, "euro")
const PANEL_OUT = join(ROOT, "euro", "dist", "panel-release")
const SENTINEL = "the build exports and deletes nothing before acceptance"
const BUN = process.execPath

let createdDirs: string[] = []
let hadFab = false
let hadEuroFab = false
let hadPanelOut = false

function remember(path: string): void {
  if (!existsSync(path)) createdDirs.push(path)
}

beforeAll(() => {
  hadFab = existsSync(FAB)
  hadEuroFab = existsSync(EURO_FAB)
  hadPanelOut = existsSync(PANEL_OUT)
  remember(FAB)
  remember(EURO_FAB)
  remember(PANEL_OUT)
  mkdirSync(FAB, { recursive: true })
  mkdirSync(EURO_FAB, { recursive: true })
  mkdirSync(PANEL_OUT, { recursive: true })
  writeFileSync(join(FAB, "sentinel.txt"), SENTINEL + "\n")
  writeFileSync(join(EURO_FAB, "sentinel.txt"), SENTINEL + "\n")
  writeFileSync(join(PANEL_OUT, "sentinel.txt"), SENTINEL + "\n")
})

afterAll(() => {
  for (const sentinel of ["sentinel.txt"]) {
    for (const dir of [FAB, EURO_FAB, PANEL_OUT]) rmSync(join(dir, sentinel), { force: true })
  }
  // Leave the tree as it was: remove only the directories this test created.
  for (const dir of [PANEL_OUT, EURO_FAB, FAB]) {
    if (createdDirs.includes(dir) && existsSync(dir) && readdirSync(dir).length === 0) {
      rmSync(dir, { recursive: true, force: true })
    }
  }
  // euro/dist may have been created by the test; keep it only if it has files.
  const dist = join(ROOT, "euro", "dist")
  if (existsSync(dist) && readdirSync(dist).length === 0) rmSync(dist, { recursive: true, force: true })
})

function run(command: string, args: string[], cwd: string, env: Record<string, string | undefined> = {}) {
  const result = spawnSync(command, args, {
    cwd,
    encoding: "utf8",
    env: {
      ...process.env,
      // bun must be reachable inside the scripts they call
      PATH: `${dirname(BUN)}:${process.env.PATH ?? ""}`,
      ...env,
    },
  })
  return {
    status: result.status,
    stdout: result.stdout ?? "",
    stderr: result.stderr ?? "",
    output: `${result.stdout ?? ""}${result.stderr ?? ""}`,
  }
}

function expectUntouched(): void {
  for (const dir of [FAB, EURO_FAB, PANEL_OUT]) {
    expect(readFileSync(join(dir, "sentinel.txt"), "utf8")).toBe(SENTINEL + "\n")
  }
}

describe("production entry points refuse before they touch anything", () => {
  test("base board `bun run release` (tscircuit/package.json)", () => {
    const result = run(BUN, ["run", "release"], join(ROOT, "tscircuit"))
    expect(result.status).not.toBe(0)
    expect(result.output).toContain("RELEASE BLOCKED")
    expect(result.output).not.toContain("########## 1. gate")
    expectUntouched()
  })

  test("base board `bash scripts/release.sh`", () => {
    const result = run("bash", ["scripts/release.sh"], join(ROOT, "tscircuit"))
    expect(result.status).not.toBe(0)
    expect(result.output).toContain("RELEASE BLOCKED")
    expect(result.output).not.toContain("########## 1. gate")
    expectUntouched()
  })

  test("euro board `bash scripts/release.sh`", () => {
    const result = run("bash", ["scripts/release.sh"], join(ROOT, "euro"))
    expect(result.status).not.toBe(0)
    expect(result.output).toContain("RELEASE BLOCKED")
    expect(result.output).not.toContain("########## 1. euro gate")
    expectUntouched()
  })

  test("direct `bun run panel:release` (euro/scripts/panel_release.ts)", () => {
    const result = run(BUN, ["run", "panel:release"], join(ROOT, "euro"))
    expect(result.status).not.toBe(0)
    expect(result.output).toContain("PANEL RELEASE BLOCKED")
    expectUntouched()
  })

  test("root `make release`", () => {
    const result = run("make", ["release"], ROOT)
    expect(result.status).not.toBe(0)
    expect(result.output).toContain("BLOCKED")
    expect(result.output).not.toContain("########## 1. gate")
    expectUntouched()
  })
})

describe("FROEMIX_SKIP_ERC is refused by every production entry point", () => {
  test("base board release", () => {
    const result = run("bash", ["scripts/release.sh"], join(ROOT, "tscircuit"), { FROEMIX_SKIP_ERC: "1" })
    expect(result.status).not.toBe(0)
    expect(result.output).toContain("RELEASE REFUSED: FROEMIX_SKIP_ERC=1")
    expect(result.output).not.toContain("########## 1. gate")
    expectUntouched()
  })

  test("euro board release", () => {
    const result = run("bash", ["scripts/release.sh"], join(ROOT, "euro"), { FROEMIX_SKIP_ERC: "1" })
    expect(result.status).not.toBe(0)
    expect(result.output).toContain("RELEASE REFUSED: FROEMIX_SKIP_ERC=1")
    expect(result.output).not.toContain("########## 1. euro gate")
    expectUntouched()
  })

  test("root make release", () => {
    const result = run("make", ["release"], ROOT, { FROEMIX_SKIP_ERC: "1" })
    expect(result.status).not.toBe(0)
    expect(result.output).toContain("BLOCKED")
    expectUntouched()
  })
})

describe("the acceptance record itself", () => {
  test("ships every required item pending", () => {
    const state = JSON.parse(readFileSync(join(ROOT, "hardware-acceptance.json"), "utf8"))
    expect(state.items.length).toBeGreaterThan(0)
    for (const item of state.items) expect(item.state).toBe("pending")
  })

  test("`make acceptance-check` blocks and writes nothing", () => {
    const before = readdirSync(FAB).sort()
    const result = run("make", ["acceptance-check"], ROOT)
    expect(result.status).not.toBe(0)
    expect(result.output).toContain("still pending")
    expect(readdirSync(FAB).sort()).toEqual(before)
  })
})

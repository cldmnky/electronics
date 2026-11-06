#!/usr/bin/env bun
// Runs the `tsci check` reports for both boards and judges their text.
//
// CLI APIs used (see `bunx tsci check --help`, pinned @tscircuit/cli 0.1.2427):
//   bunx tsci check source|pin_specification|netlist|placement|shorts|pcb-style <entry>
// There is no `drc`/`erc` command and no exit-code contract beyond "non-zero on
// findings". The placement report exits 1 for informational notes while its own
// counters read "Errors: 0 / Warnings: 0", so the exit code alone cannot pass or
// fail a board: lib/reports.ts parses the counters, the warning list, the
// flagged components, the markers and the built circuit JSON.
//
// Both entry points are named explicitly (front.circuit.tsx, index.circuit.tsx)
// so a default-file change cannot silently check a different board.
//
// This is design/output consistency, not a measurement and not an approval.

import { spawnSync } from "node:child_process"
import { existsSync, readFileSync, writeFileSync } from "node:fs"
import { resolve } from "node:path"
import {
  analyseCircuitJson,
  analyseReport,
  BOARD_RULES,
  DECLARED_UNCONNECTED,
  parseFlaggedComponents,
  type CheckFormat,
  type JsonElement,
} from "../lib/reports"

const ROOT = resolve(import.meta.dir, "..")

const BOARDS = [
  { key: "front", entry: "front.circuit.tsx", dist: "dist/front/circuit.json" },
  { key: "rear", entry: "index.circuit.tsx", dist: "dist/index/circuit.json" },
] as const

const FORMATS: CheckFormat[] = [
  "source",
  "pin_specification",
  "netlist",
  "placement",
  "shorts",
  "pcb-style",
]

let failures = 0
let notes = 0
let placementFlags = 0
let declaredUnconnected = 0

function fail(text: string): void {
  failures++
  console.log(`FAIL ${text}`)
}

function note(text: string): void {
  notes++
  console.log(`NOTE ${text}`)
}

// The existing dist output is judged first: the reports are only meaningful
// for the boards that were built, and the build must not be trusted blindly.
for (const board of BOARDS) {
  const path = resolve(ROOT, board.dist)
  if (!existsSync(path)) {
    fail(`${board.key}: ${board.dist} missing — run the build first`)
    continue
  }
  let json: JsonElement[]
  try {
    json = JSON.parse(readFileSync(path, "utf8")) as JsonElement[]
  } catch (error) {
    fail(`${board.key}: ${board.dist} is not readable JSON: ${String(error)}`)
    continue
  }
  if (!Array.isArray(json) || json.length === 0) {
    fail(`${board.key}: ${board.dist} is empty`)
    continue
  }
  const outcome = analyseCircuitJson(json, DECLARED_UNCONNECTED[board.key] ?? {})
  declaredUnconnected += outcome.declaredUnconnected
  for (const text of outcome.notes) note(`${board.key}: ${text}`)
  for (const text of outcome.failures) fail(`${board.key}: ${text}`)
  console.log(`PASS ${board.key}: built circuit JSON has no error element, no undeclared `
    + `unconnected pin, ${outcome.declaredUnconnected} declared do-not-connect`)
}

for (const board of BOARDS) {
  const rules = BOARD_RULES[board.key]
  if (!rules) {
    fail(`${board.key}: no report rules in lib/reports.ts`)
    continue
  }
  for (const format of FORMATS) {
    const run = spawnSync("bunx", ["tsci", "check", format, board.entry], {
      cwd: ROOT,
      encoding: "utf8",
      maxBuffer: 64 * 1024 * 1024,
    })
    if (run.error) {
      fail(`${board.key}/${format}: could not run bunx tsci check: ${run.error.message}`)
      continue
    }
    const output = `${run.stdout ?? ""}${run.stderr ?? ""}`
    if (run.signal) {
      fail(`${board.key}/${format}: tool killed by ${run.signal}, no report to trust`)
      continue
    }
    const outcome = analyseReport(format, output, run.status, rules)
    if (format === "placement") placementFlags += parseFlaggedComponents(output).length
    for (const text of outcome.notes) note(`${board.key}: ${text}`)
    for (const text of outcome.failures) fail(`${board.key}/${format}: ${text}`)
    if (outcome.failures.length === 0) {
      console.log(`PASS ${board.key}/${format} (exit ${run.status})`)
    }
  }
}

console.log(`\n${failures} failure(s), ${notes} note(s). `
  + `Known notes are pinned in lib/reports.ts; the upstream autorouting warning `
  + `stays a manufacturing acceptance item.`)

// One line an order run can be read by: what was flagged, what the source
// declares as intentionally open, and whether anything failed. The detail above
// stays the evidence; nothing is filtered out of it.
const summary = `SUMMARY  placement: ${placementFlags} flagged`
  + ` · unconnected-by-design: ${declaredUnconnected} declared`
  + ` · errors: ${failures}`
console.log(summary)
try {
  writeFileSync(resolve(ROOT, "dist/reports-summary.txt"), `${summary}\n`)
} catch (error) {
  // The summary file is an order-run convenience; the checks above still decide.
  console.log(`NOTE could not write dist/reports-summary.txt: ${String(error)}`)
}
process.exit(failures === 0 ? 0 : 1)

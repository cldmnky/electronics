#!/usr/bin/env bun
// Froemix physical acceptance gate.
//
//   bun tools/release/gate.ts --check        production: fail on pending/invalid
//   bun tools/release/gate.ts --report       prototype: print state, fail only
//                                            on invalid or stale records
//   bun tools/release/gate.ts --fingerprint  print the hardware fingerprint
//
// Exit codes:
//   0  every required item accepted against the current fingerprint (--check),
//      or the record is structurally valid (--report),
//   1  blocked: pending items, invalid evidence, unknown/duplicate ids, stale
//      hardware fingerprint, missing hardware sources, a malformed record,
//   2  the command itself could not run (missing record file, missing sources).
//
// `--check` is what the production entry points call. It reads the record and
// the evidence and writes nothing: no build, no export, no deletion happens
// before it passes. There is no force, skip, waive or override flag, and the
// record has none either. `--report` exists so `make gate` can print the
// physical state without pretending the gate itself is a physical approval; it
// is never used by a release.

import { existsSync } from "node:fs"
import { join, relative } from "node:path"
import {
  FINGERPRINT_DIRS,
  FINGERPRINT_RECIPE,
  hardwareFingerprintReport,
  loadState,
  REQUIRED_ITEMS,
  SCHEMA,
  validateAcceptance,
} from "./acceptance"

const HERE = new URL(".", import.meta.url).pathname
/** tools/release/ -> froemix/ */
const PROJECT_ROOT = join(HERE, "..", "..")

interface Options {
  mode: "check" | "report" | "fingerprint"
  rootDir: string
  statePath: string
  evidenceDir?: string
}

function usage(): string {
  return [
    "usage: bun tools/release/gate.ts [--check | --report | --fingerprint]",
    "",
    "  --check        production gate: every required item accepted (default)",
    "  --report       print the physical acceptance state; fail only on invalid or",
    "                 stale records, not on pending items (prototype `make gate`)",
    "  --fingerprint  print the current hardware fingerprint",
    "  --root DIR     project root (default: froemix/)",
    "  --state FILE   acceptance record (default: <root>/hardware-acceptance.json)",
    "  --evidence DIR evidence directory (default: <root>/doc/evidence)",
  ].join("\n")
}

function parseArgs(argv: string[]): Options {
  const options: Options = {
    mode: "check",
    rootDir: PROJECT_ROOT,
    statePath: "",
  }
  let modeSeen = false
  for (let index = 0; index < argv.length; index += 1) {
    const argument = argv[index]!
    const next = () => {
      const value = argv[index + 1]
      if (value === undefined) throw new Error(`${argument} needs a value`)
      index += 1
      return value
    }
    switch (argument) {
      case "--check":
      case "--report":
      case "--fingerprint": {
        if (modeSeen) throw new Error("choose exactly one of --check, --report, --fingerprint")
        modeSeen = true
        options.mode = argument === "--check" ? "check" : argument === "--report" ? "report" : "fingerprint"
        break
      }
      case "--root":
        options.rootDir = next()
        break
      case "--state":
        options.statePath = next()
        break
      case "--evidence":
        options.evidenceDir = next()
        break
      case "-h":
      case "--help":
        console.log(usage())
        process.exit(0)
        break
      default:
        throw new Error(`unknown argument: ${argument}`)
    }
  }
  options.rootDir = join(options.rootDir)
  if (!options.statePath) options.statePath = join(options.rootDir, "hardware-acceptance.json")
  return options
}

function fingerprint(options: Options): number {
  const report = hardwareFingerprintReport(options.rootDir)
  if (report.missing.length > 0 || report.missingDirectories.length > 0) {
    console.error("release-gate: BLOCKED - the hardware fingerprint would be partial")
    for (const path of report.missing) console.error(`  missing mandatory source: ${path}`)
    for (const path of report.missingDirectories) console.error(`  missing source directory: ${path}`)
    console.error("A missing hardware source is never skipped; restore it before recording acceptance.")
    return 2
  }
  console.log(report.fingerprint)
  return 0
}

function main(argv: string[]): number {
  let options: Options
  try {
    options = parseArgs(argv)
  } catch (error) {
    console.error(`release-gate: ${(error as Error).message}`)
    console.error(usage())
    return 2
  }

  if (options.mode === "fingerprint") return fingerprint(options)

  const sources = hardwareFingerprintReport(options.rootDir)
  if (sources.missing.length > 0 || sources.missingDirectories.length > 0) {
    console.error("release-gate: BLOCKED - the hardware fingerprint cannot be computed")
    for (const path of sources.missing) console.error(`  missing mandatory source: ${path}`)
    for (const path of sources.missingDirectories) console.error(`  missing source directory: ${path}`)
    console.error("Acceptance cannot rest on a partial fingerprint. Restore the sources.")
    return 2
  }

  if (!existsSync(options.statePath)) {
    console.error(`release-gate: BLOCKED - no acceptance record at ${options.statePath}`)
    console.error("A missing record is not an empty record: create it with every item pending.")
    return 2
  }

  let state: unknown
  try {
    state = loadState(options.statePath)
  } catch (error) {
    console.error(`release-gate: BLOCKED - ${(error as Error).message}`)
    return 2
  }

  const result = validateAcceptance(state, {
    rootDir: options.rootDir,
    evidenceDir: options.evidenceDir,
    fingerprint: sources.fingerprint,
  })

  console.log(`release-gate: froemix hardware acceptance (${SCHEMA})`)
  console.log(`record:   ${relative(process.cwd(), options.statePath) || options.statePath}`)
  console.log(`evidence: ${options.evidenceDir ?? join(options.rootDir, "doc/evidence")}`)
  console.log(`fingerprint (${FINGERPRINT_RECIPE}): ${sources.fingerprint}`)
  console.log(`sources hashed: ${sources.files.length} files from ${FINGERPRINT_DIRS.length + 1} sets`)
  for (const line of result.lines) console.log(`  ${line}`)
  console.log(
    `accepted: ${result.accepted.length}/${REQUIRED_ITEMS.length}, pending: ${result.pending.length}`,
  )

  if (options.mode === "report") {
    const blocking = result.failures.filter((failure) => !failure.includes("still pending"))
    for (const failure of blocking) console.error(`  FAIL ${failure}`)
    if (blocking.length > 0) {
      console.error(`release-gate: INVALID - ${blocking.length} problem(s) in the acceptance record`)
      return 1
    }
    if (result.pending.length > 0) {
      console.log(
        `release-gate: PHYSICAL ACCEPTANCE NOT APPROVED - ${result.pending.length} item(s) pending. `
        + "This report is not a fabrication approval and no order may be placed from it.",
      )
    } else {
      console.log("release-gate: every required physical item is accepted against the current fingerprint")
    }
    return 0
  }

  if (result.failures.length > 0) {
    for (const failure of result.failures) console.error(`  FAIL ${failure}`)
    console.error(
      `release-gate: BLOCKED - ${result.failures.length} reason(s). Nothing was built, exported or deleted.`,
    )
    return 1
  }
  console.log("release-gate: PASS - every required physical item is accepted against the current fingerprint")
  return 0
}

if (import.meta.main) {
  process.exit(main(process.argv.slice(2)))
}

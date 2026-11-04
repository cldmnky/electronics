// Export the canonical mechanical data to mechanical/layout.json.
//
// tscircuit/lib/layout.ts is the single source of panel and mechanical
// coordinates. The 3D-printed bracket is built by Python/CadQuery, which must
// not restate any of those numbers, so this is the one and only bridge:
//
//   cd tscircuit && bun run scripts/export-mechanical.ts           # generate
//   cd tscircuit && bun run scripts/export-mechanical.ts --check   # verify only
//
// `--check` writes nothing. It compares the tracked file with a fresh render
// and exits non-zero when the file is missing or stale, so a gate can never
// repair a stale layout.json and pass with an uncommitted change. Any argument
// other than `--check` is rejected.
import { mkdirSync, readFileSync, writeFileSync } from "node:fs"
import { dirname, resolve } from "node:path"
import { mechanicalExport } from "../lib/layout"

export const DEFAULT_OUT = resolve(import.meta.dir, "../../mechanical/layout.json")

/** The exact bytes the tracked file must contain. */
export const renderMechanical = (): string => `${JSON.stringify(mechanicalExport(), null, 2)}\n`

export type ExportResult = { code: number; message: string; wrote: boolean }

const USAGE = "usage: bun run scripts/export-mechanical.ts [--check]"

/**
 * Run the export step. `out` exists so tests can point at a temporary file.
 * Never throws for a bad argument: the caller prints the message and exits.
 */
export const run = (args: readonly string[], out: string = DEFAULT_OUT): ExportResult => {
  const fresh = renderMechanical()

  if (args.length === 0) {
    mkdirSync(dirname(out), { recursive: true })
    writeFileSync(out, fresh)
    return { code: 0, message: `wrote ${out}`, wrote: true }
  }

  if (args.length === 1 && args[0] === "--check") {
    let existing: string
    try {
      existing = readFileSync(out, "utf8")
    } catch {
      return {
        code: 1,
        message: `${out} is missing; run the generator without --check and commit the result`,
        wrote: false,
      }
    }
    if (existing !== fresh) {
      return {
        code: 1,
        message: `${out} is stale against tscircuit/lib/layout.ts; ` +
          "run the generator without --check and commit the result",
        wrote: false,
      }
    }
    return { code: 0, message: `${out} is up to date`, wrote: false }
  }

  return { code: 2, message: `unknown argument ${args.join(" ")}\n${USAGE}`, wrote: false }
}

if (import.meta.main) {
  const result = run(process.argv.slice(2))
  const log = result.code === 0 ? console.log : console.error
  log(result.message)
  process.exit(result.code)
}

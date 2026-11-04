// Export the canonical mechanical data to mechanical/layout.json.
//
// tscircuit/lib/layout.ts is the single source of panel and mechanical
// coordinates. The 3D-printed bracket is built by Python/CadQuery, which must
// not restate any of those numbers, so this is the one and only bridge:
//
//   cd tscircuit && bun run scripts/export-mechanical.ts
//
// Commit the resulting file. `lib/layout.test.ts` fails when it is stale, and
// the CAD self-check reads it instead of parsing TypeScript.
import { mkdirSync, writeFileSync } from "node:fs"
import { dirname, resolve } from "node:path"
import { mechanicalExport } from "../lib/layout"

const out = resolve(import.meta.dir, "../../mechanical/layout.json")
mkdirSync(dirname(out), { recursive: true })
writeFileSync(out, `${JSON.stringify(mechanicalExport(), null, 2)}\n`)
console.log(`wrote ${out}`)

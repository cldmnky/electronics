#!/usr/bin/env bun
// Production release gate. release.sh runs this FIRST, before it deletes or
// writes anything in fab/. It fails closed: any pending or invalid physical
// item, a stale hardware fingerprint, a malformed state file or an override
// key stops the release with no export.
//
//   bun run scripts/release_gate.ts              # gate (exit 1 when blocked)
//   bun run scripts/release_gate.ts --fingerprint # print the current hash
//
// There is no flag that clears an unresolved item. `make test` is prototype
// verification; this gate is the production check, and only recorded evidence
// from a named reviewer moves an item to "accepted".

import { readFileSync } from "node:fs"
import { join, resolve } from "node:path"
import {
  computeHardwareFingerprint,
  FINGERPRINT_RECIPE,
  hardwareFingerprintReport,
  REQUIRED_ITEMS,
  SCHEMA,
  validateAcceptance,
} from "../lib/release-acceptance"

const MIDIBRAIN = resolve(import.meta.dir, "../..")
const STATE_PATH = join(MIDIBRAIN, "hardware-acceptance.json")

if (process.argv.includes("--fingerprint")) {
  try {
    console.log(computeHardwareFingerprint(MIDIBRAIN))
    process.exit(0)
  } catch (error) {
    console.error(`FAIL ${String(error)}`)
    process.exit(1)
  }
}

console.log("Production release gate (fabrication export is blocked until this passes)")
console.log(`state: ${STATE_PATH}`)
console.log(`items: ${REQUIRED_ITEMS.length} required, schema ${SCHEMA}, recipe ${FINGERPRINT_RECIPE}`)

const report = hardwareFingerprintReport(MIDIBRAIN)
if (report.missing.length > 0) {
  console.error(`FAIL hardware source(s) missing, so the fingerprint is partial: ${report.missing.join(", ")}`)
  console.error("\nRELEASE BLOCKED: the hardware revision cannot be identified.")
  console.error("Nothing was exported and fab/ was not changed.")
  process.exit(1)
}
const fingerprint = report.fingerprint
console.log(`hardware fingerprint: ${fingerprint} (${report.files.length} sources)`)

let state: unknown
try {
  state = JSON.parse(readFileSync(STATE_PATH, "utf8"))
} catch (error) {
  console.error(`FAIL cannot read ${STATE_PATH}: ${String(error)}`)
  console.error("\nRELEASE BLOCKED: no acceptance state, no export.")
  process.exit(1)
}

const result = validateAcceptance(state, { rootDir: MIDIBRAIN, fingerprint })
for (const line of result.lines) console.log(line)
if (result.failures.length > 0) {
  console.log("")
  for (const failure of result.failures) console.log(`FAIL ${failure}`)
  console.log(`\nRELEASE BLOCKED: ${result.failures.length} problem(s). `
    + `Nothing was exported and fab/ was not changed.`)
  if (result.failures.some(text => text.includes("fingerprint is stale"))) {
    console.log("The hardware changed: refresh hardware_fingerprint with "
      + "`bun run scripts/release_gate.ts --fingerprint` and re-check the evidence.")
  }
  console.log("Record measured evidence in docs/evidence/ and update "
    + "hardware-acceptance.json; see docs/acceptance.md.")
  process.exit(1)
}

console.log("\nRELEASE GATE PASSED: every required physical item is accepted "
  + "against the current hardware fingerprint.")
console.log("This still does not replace the first-article build and the "
  + "JLCPCB DFM/order preview.")
process.exit(0)

#!/usr/bin/env bun
// Called by the EXISTING euro release, no extra Makefile target.
import { mkdirSync, readFileSync, writeFileSync } from "node:fs"
import { join, resolve } from "node:path"
import { serialize } from "@jscad/stl-serializer"
import { mechanicalParts, partMeasurements } from "../lib/mechanical-parts"
import { ACTUATORS, CONNECTOR_Z, STACK, actuatorFit, measureStack } from "../lib/mechanical"
import { checkMechanicalNumbers } from "./mechanical_check"

const errors = checkMechanicalNumbers()
if (errors.length) throw new Error(errors.join("\n"))
const out = resolve(process.argv[2] ?? join(import.meta.dir, "../dist/mechanical"))
mkdirSync(out, { recursive: true })
for (const part of mechanicalParts()) {
  const text = serialize({ binary: false }, part.geometry).join("")
  writeFileSync(join(out, `${part.name}.stl`), text)
}
writeFileSync(join(out, "mechanical-manifest.json"), JSON.stringify({
  units: "mm",
  status: "dimensioned prototype; physical first-article acceptance required",
  stack: STACK,
  connectorDimensions: CONNECTOR_Z,
  nominal: measureStack(),
  actuatorDimensions: ACTUATORS,
  conservativeClearances: actuatorFit(),
  parts: partMeasurements(),
}, null, 2) + "\n")
writeFileSync(join(out, "hardware.csv"), [
  "Quantity,Item,Requirement",
  '4,M3 female-female spacer,"11.0 +/-0.1mm clear length; <=5.0mm OD; insulating preferred"',
  '8,M3 board fixing screw,"length to suit spacers; no bottoming; <=5mm head/washer at tight standoffs"',
  '2,J1 jumper shunt,"2.54mm; installed header+shunts <=9.5mm high; Type A pairs 3-5 and 4-6"',
  '6,Pot knob,"6mm knurled shaft; <=12.5mm diameter; fit actual ALPS shaft before batch"',
  '1,MODE plunger,"print mode-plunger.stl in translucent resin; finish barrel <=11.5mm"',
  '1,NAV cap,"print nav-cap.stl in tough resin; fit square pocket; removable silicone retention"',
].join("\n") + "\n")
writeFileSync(join(out, "MECHANICAL-STACK.md"), readFileSync(join(import.meta.dir, "../../doc/MECHANICAL-STACK.md")))
console.log(`Mechanical prototypes: ${out} (STL millimetres, manifest, hardware list and assembly/acceptance guide)`)

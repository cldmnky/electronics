#!/usr/bin/env bun
// Called by the EXISTING euro release, no extra Makefile target.
import { mkdirSync, readFileSync, writeFileSync } from "node:fs"
import { join, resolve } from "node:path"
import { serialize } from "@jscad/stl-serializer"
import { mechanicalParts, partMeasurements } from "../lib/mechanical-parts"
import {
  ACTUATORS, CONNECTOR_Z, FASTENERS, FIT_LIMITS, OLED_MODULE, PANEL_PARTS, PENDING_ACCEPTANCE, RACK,
  STACK, STANDOFF_FACES, UNRESOLVED, actuatorCornerLimits, measureStack, stackCornerLimits,
} from "../lib/mechanical"
import { checkMechanicalNumbers, pendingMechanicalNotes, rackNumbers } from "./mechanical_check"

const errors = checkMechanicalNumbers()
if (errors.length) throw new Error(errors.join("\n"))
const out = resolve(process.argv[2] ?? join(import.meta.dir, "../dist/mechanical"))
mkdirSync(out, { recursive: true })
for (const part of mechanicalParts()) {
  const text = serialize({ binary: false }, part.geometry).join("")
  writeFileSync(join(out, `${part.name}.stl`), text)
}

const stack = stackCornerLimits()
const fit = actuatorCornerLimits()
const rack = rackNumbers()

writeFileSync(join(out, "mechanical-manifest.json"), JSON.stringify({
  units: "mm",
  status: "dimensioned prototype; physical first-article acceptance required",
  stack: STACK,
  connectorDimensions: CONNECTOR_Z,
  nominal: measureStack(),
  toleranceCorners: {
    corners: stack.corners,
    insertionMin: stack.insertionMin,
    insertionMax: stack.insertionMax,
    bodyGapMin: stack.bodyGapMin,
    j1ClearanceMin: stack.j1ClearanceMin,
  },
  actuatorDimensions: ACTUATORS,
  // Worst value over every panel-gap / thickness / print / tilt corner.
  actuatorWorstCorners: { corners: fit.corners, ...fit.worst, limits: FIT_LIMITS },
  hardware: {
    ...FASTENERS,
    faces: STANDOFF_FACES,
    // The purchase limit is a policy value; the measured nearest-envelope
    // distances and the derived limit are printed by the gate, which reads both
    // built boards. This file must never quote an assumed distance.
    limitRule: "2 x (nearest component envelope - budget), measured per hole and face",
    selection: "PENDING: no verified head/washer at or below the purchase limit",
  },
  aperture: { ...OLED_MODULE, note: "window from lib/layout.ts; must not crop the active area" },
  panelParts: PANEL_PARTS,
  rack: { ...RACK, ...rack, status: "PENDING: rack/adjacent-module acceptance not performed" },
  unresolved: UNRESOLVED,
  pendingAcceptance: PENDING_ACCEPTANCE,
  pendingNotes: pendingMechanicalNotes(),
  parts: partMeasurements(),
}, null, 2) + "\n")

// Purchasing / preparation list. Every limit comes from the constants above, so
// a requirement line and the arithmetic that justified it cannot drift apart.
const q = (text: string) => `"${text}"`
const rows: [number, string, string][] = [
  [4, "M3 female-female spacer",
    `M3; ${FASTENERS.spacerLength.toFixed(1)} +/-${STACK.baseGapTolerance}mm clear length; OD <=${FASTENERS.hardwareDiameterMax.toFixed(1)}mm; insulating preferred; PENDING - source from RS/Electrokit/DigiKey with EU sales and measure`],
  [8, "M3 board fixing screw",
    `length to suit the ${FASTENERS.spacerLength.toFixed(1)}mm spacers; no bottoming; head/washer OD <=${FASTENERS.hardwareDiameterMax.toFixed(1)}mm; PENDING - an ISO 4762 head is ${FASTENERS.socketHeadDiameterIso4762}mm, an ISO 7089 washer ${FASTENERS.plainWasherDiameterIso7089M3}mm`],
  [2, "J1 jumper shunt",
    `2.54mm; installed J1 + shunts <=${STACK.j1HeightMax}mm high; Type A pairs 3-5 and 4-6; PENDING - no verified low-profile closed shunt`],
  [6, "Pot knob",
    `6mm knurled shaft; OD <=${PANEL_PARTS.potKnobDiameterMax}mm; fit the actual shaft before a batch; PENDING - not selected`],
  [2, "1x20 2.54mm female socket strip",
    "for the Daisy Seed; height, tails and total module depth must be measured; PENDING - no verified part"],
  [4, "Rack mounting screw",
    `${RACK.screw}; rack reference, not part of the module BOM; rail hole pitch and width are PENDING rack acceptance`],
  [1, "MODE plunger",
    `print mode-plunger.stl in translucent resin; finish the barrel <=${(ACTUATORS.button.barrelDiameter + ACTUATORS.printTolerance).toFixed(1)}mm; flange ${ACTUATORS.button.flangeHeight}mm behind the panel`],
  [1, "NAV cap",
    `print nav-cap.stl in tough resin; ${ACTUATORS.joystick.cupDiameter}mm cup, ${ACTUATORS.joystick.pocketWidth}mm square pocket; dry-fit the stem, removable silicone retention`],
]
writeFileSync(join(out, "hardware.csv"), [
  "Quantity,Item,Requirement",
  ...rows.map(([count, item, requirement]) => `${count},${q(item)},${q(requirement)}`),
].join("\n") + "\n")

writeFileSync(join(out, "MECHANICAL-STACK.md"), readFileSync(join(import.meta.dir, "../../doc/MECHANICAL-STACK.md")))
console.log(`Mechanical prototypes: ${out} (STL millimetres, manifest, hardware list and assembly/acceptance guide)`)
console.log(`  hardware limit OD${FASTENERS.hardwareDiameterMax}mm at a ${FASTENERS.budget}mm budget; ${UNRESOLVED.length} unresolved identities, `
  + `${PENDING_ACCEPTANCE.length} pending acceptance items`)

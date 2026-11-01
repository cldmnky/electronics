#!/usr/bin/env bun
// Run after BOTH boards have been built. No geometry is inferred from a picture.
import { readFileSync } from "node:fs"
import { join } from "node:path"
import { BASE_GAP, HOLE, PANEL_GAP } from "../lib/layout"
import { ACTUATORS, CONNECTOR_Z, HEADERS, SOCKETS, STACK, actuatorFit, checkStack, measureStack, nominalStack } from "../lib/mechanical"

type Element = Record<string, any>
const ROOT = join(import.meta.dir, "..")

export const checkMechanicalParts = (base: Element[], front: Element[]): string[] => {
  const errors: string[] = []
  for (const [board, expected, side] of [[base, HEADERS, "top"], [front, SOCKETS, "bottom"]] as const) {
    for (const [ref, spec] of Object.entries(expected)) {
      const src = board.find((e) => e.type === "source_component" && e.name === ref)
      const pc = src && board.find((e) => e.type === "pcb_component" && e.source_component_id === src.source_component_id)
      const codes = src?.supplier_part_numbers?.jlcpcb ?? []
      if (codes.length !== 1 || codes[0] !== spec.code || src?.manufacturer_part_number !== spec.mpn)
        errors.push(`${side} ${ref}: expected ${spec.code} / ${spec.mpn}; short/unknown substitutes need a new Z review`)
      if (pc?.layer !== side || pc?.do_not_place)
        errors.push(`${ref}: must be fitted on ${side}`)
      const ports = board.filter((e) => e.type === "source_port" && e.source_component_id === src?.source_component_id)
      if (ports.length !== spec.pins) errors.push(`${ref}: expected ${spec.pins} PCB pins, found ${ports.length}`)
    }
  }
  return errors
}

export const checkMechanicalNumbers = (): string[] => {
  const errors = checkStack()
  if (BASE_GAP !== STACK.baseGap || PANEL_GAP !== STACK.panelGap || HOLE.joy !== ACTUATORS.joystick.holeDiameter)
    errors.push("layout.ts disagrees with the mechanical assembly contract")
  // Worst-case opposing component tolerances; these numbers are independent
  // of the selected artwork. The insertion window is an engineering policy,
  // not a substitute for a connector manufacturer's mating qualification.
  const n = nominalStack()
  const insertionTolerance = STACK.baseGapTolerance + CONNECTOR_Z.headerShoulderTolerance
    + CONNECTOR_Z.postLengthTolerance + CONNECTOR_Z.socketHeightTolerance
  const m = measureStack(n)
  if (m.insertion - insertionTolerance < CONNECTOR_Z.insertionMin - 1e-9
      || m.insertion + insertionTolerance > CONNECTOR_Z.insertionMax + 1e-9)
    errors.push("worst-case header insertion is outside the acceptance window")
  if (m.bodyGap - STACK.baseGapTolerance - CONNECTOR_Z.headerShoulderTolerance - CONNECTOR_Z.socketHeightTolerance < 0)
    errors.push("connector insulators can collide at tolerance extremes")
  if (m.j1Clearance - STACK.baseGapTolerance < STACK.minInterboardClearance)
    errors.push("J1/shunts can hit the trimmed front-board tails")
  const a = actuatorFit()
  if (a.buttonProud < ACTUATORS.minProud || a.joystickProud < ACTUATORS.minProud)
    errors.push("actuator does not reach beyond the thickest/highest panel")
  for (const [name, clearance] of Object.entries(a)) {
    if (clearance <= 0) errors.push(`actuator ${name}: non-positive clearance ${clearance.toFixed(3)}mm`)
  }
  return errors
}

if (import.meta.main) {
  const read = (path: string): Element[] => {
    try {
      return JSON.parse(readFileSync(path, "utf8"))
    } catch {
      console.error(`FAIL cannot read ${path}. Build BOTH boards first: 'make build' (or run the base gate), then the euro gate.`)
      process.exit(1)
    }
  }
  const base = read(join(ROOT, "../tscircuit/dist/index/circuit.json"))
  const front = read(join(ROOT, "dist/index/circuit.json"))
  const errors = [...checkMechanicalParts(base, front), ...checkMechanicalNumbers()]
  for (const error of errors) console.error(`FAIL ${error}`)
  console.log("stack (surface-to-surface mm):", measureStack())
  console.log("actuator conservative clearances (mm):", actuatorFit())
  console.log("Assembly limits: 11mm spacers; CN4 cut to 7 ways; J1+shunts <=9.5mm; front tails <=1mm.")
  console.log("Physical fit/contact/retention/CAM acceptance remain first-article tests; not certified by this gate.")
  if (errors.length) process.exit(1)
  console.log("PASS - supplier identities, Z arithmetic and actuator design envelopes")
}

/// <reference types="bun-types" />
import { expect, test } from "bun:test"
import { booleans, geometries, measurements, primitives } from "@jscad/modeling"
import { serialize } from "@jscad/stl-serializer"
import { ACTUATORS, HEADERS, SOCKETS, STACK, actuatorFit, checkStack, measureStack, nominalStack } from "./mechanical"
import { mechanicalParts, modePlunger, navCap, partMeasurements } from "./mechanical-parts"
import { checkMechanicalNumbers, checkMechanicalParts } from "../scripts/mechanical_check"

test("the selected stack and actuator envelopes pass", () => {
  expect(checkMechanicalNumbers()).toEqual([])
  expect(measureStack().insertion).toBeCloseTo(3.3, 6)
  expect(measureStack().bodyGap).toBeCloseTo(0.7, 6)
  expect(measureStack().j1Clearance).toBeCloseTo(0.5, 6)
})

test("original short headers cannot be substituted even at the new gap", () => {
  expect(checkStack({ ...nominalStack(), shoulder: 2 })).toContainEqual(expect.stringMatching(/insertion/))
})

test("original 12mm spacers are rejected with the selected headers", () => {
  expect(checkStack({ ...nominalStack(), gap: 12 })).toContainEqual(expect.stringMatching(/insertion/))
})

test("E60 means 6mm total, not 6+2mm", () => {
  expect(checkStack({ ...nominalStack(), shoulder: 8 })).toContainEqual(expect.stringMatching(/bodies overlap/))
})

test("an over-height J1 or untrimmed control-board tail is rejected", () => {
  for (const mutation of [{ j1Height: 10 }, { frontTail: 2 }])
    expect(checkStack({ ...nominalStack(), ...mutation })).toContainEqual(expect.stringMatching(/J1/))
})

const boards = (): Record<string, any>[][] => [HEADERS, SOCKETS].map((parts, b) => Object.entries(parts).flatMap(([name, p], i) => [
  { type: "source_component", name, source_component_id: `s${i}`, manufacturer_part_number: p.mpn, supplier_part_numbers: { jlcpcb: [p.code] } },
  { type: "pcb_component", source_component_id: `s${i}`, layer: b ? "bottom" : "top" },
  ...Array.from({ length: p.pins }, (_, n) => ({ type: "source_port", source_component_id: `s${i}`, name: `pin${n + 1}` })),
]))

test("built supplier identity check accepts the exact selected parts", () => {
  const [base, front] = boards()
  expect(checkMechanicalParts(base!, front!)).toEqual([])
})

test("built identity check detects reverting only the LCSC/MPN to a short part", () => {
  const [base, front] = boards()
  Object.assign(base![0]!, { manufacturer_part_number: "A2005WV-16P", supplier_part_numbers: { jlcpcb: ["C225268"] } })
  expect(checkMechanicalParts(base!, front!).some((e) => e.includes("short/unknown"))).toBe(true)
})

test("wrong connector face, DNP and missing inner pins are detected", () => {
  for (const mutation of ["side", "dnp", "pin"]) {
    const [base, front] = boards()
    if (mutation === "side") front![1]!.layer = "top"
    if (mutation === "dnp") front![1]!.do_not_place = true
    if (mutation === "pin") base!.splice(3, 1)
    expect(checkMechanicalParts(base!, front!).length).toBeGreaterThan(0)
  }
})

test("bare actuators are too short; supplied extensions clear the thick panel", () => {
  const top = STACK.panelGap + STACK.panelThicknessMax
  expect(ACTUATORS.button.bareHeight).toBeLessThan(top)
  expect(ACTUATORS.joystick.bareHeight).toBeLessThan(top)
  const fit = actuatorFit()
  expect(fit.buttonProud).toBeGreaterThanOrEqual(ACTUATORS.minProud)
  expect(fit.joystickProud).toBeGreaterThanOrEqual(ACTUATORS.minProud)
  // Press travel must remain below this, or the captive lip preloads the switch.
  expect(fit.buttonFlangeClearance).toBeGreaterThanOrEqual(0.45)
  expect(fit.joystickRadialClearance).toBeGreaterThan(0.2)
  expect(fit.joystickHeadClearance).toBeGreaterThan(0)
  // Old 7mm NAV hole cannot carry the extended, tilted cup at tolerance limits.
  expect(fit.joystickRadialClearance - (ACTUATORS.joystick.holeDiameter - 7) / 2).toBeLessThan(0)
})

test("printable models are valid solids and serialise to non-empty STL", () => {
  for (const p of mechanicalParts()) {
    geometries.geom3.validate(p.geometry)
    expect(measurements.measureVolume(p.geometry)).toBeGreaterThan(50)
    const text = serialize({ binary: false }, p.geometry).join("")
    expect(text).toContain("facet normal")
    expect(text).toContain("endsolid")
  }
})

test("STL envelopes match the fit arithmetic", () => {
  const [mode, nav] = partMeasurements()
  expect(mode!.bounds[1][0] - mode!.bounds[0][0]).toBeCloseTo(ACTUATORS.button.flangeDiameter, 5)
  expect(mode!.bounds[1][2] - mode!.bounds[0][2]).toBeCloseTo(ACTUATORS.button.extension, 5)
  expect(nav!.bounds[1][2] - nav!.bounds[0][2]).toBeCloseTo(ACTUATORS.joystick.pocketDepth + ACTUATORS.joystick.extension, 5)
})

test("NAV pocket is blind, not a through hole or solid stem collision", () => {
  const p = ACTUATORS.joystick
  const stem = primitives.cuboid({ size: [p.stemWidth, p.stemWidth, p.pocketDepth - 0.1], center: [0, 0, p.pocketDepth / 2] })
  expect(measurements.measureVolume(booleans.intersect(navCap(), stem))).toBeCloseTo(0, 5)
  const roof = primitives.cuboid({ size: [1, 1, 0.2], center: [0, 0, p.pocketDepth + 0.2] })
  expect(measurements.measureVolume(booleans.intersect(navCap(), roof))).toBeCloseTo(0.2, 4)
})

test("MODE has a retaining flange larger than the hole and a sliding barrel smaller than it", () => {
  expect(ACTUATORS.button.flangeDiameter).toBeGreaterThan(12.2)
  expect(ACTUATORS.button.barrelDiameter + ACTUATORS.printTolerance).toBeLessThan(12.2 - ACTUATORS.holeUndersize)
  const middle = primitives.cuboid({ size: [1, 1, 0.2], center: [0, 0, 3] })
  expect(measurements.measureVolume(booleans.intersect(modePlunger(), middle))).toBeCloseTo(0.2, 4)
})

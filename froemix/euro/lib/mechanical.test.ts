/// <reference types="bun-types" />
import { expect, test } from "bun:test"
import { booleans, geometries, measurements, primitives } from "@jscad/modeling"
import { serialize } from "@jscad/stl-serializer"
import {
  ACTUATORS, CONNECTOR_Z, FASTENERS, FIT_LIMITS, HEADERS, MATED_PADS, OLED_MODULE, PANEL_PARTS,
  PENDING_ACCEPTANCE, RACK, RACK_GRID_TOLERANCE, SOCKETS, STACK, STANDOFF_FACES, STANDOFF_HOLE_XY, UNRESOLVED,
  actuatorCornerLimits, actuatorCorners, actuatorFit, checkStack, fastenerOdLimit, hardwareFit,
  measureStack, nominalFit, nominalStack, nonFinite, printedNavWalls, rackFit, rackGridErrors, stackCornerLimits, stackCorners,
} from "./mechanical"
import { mechanicalParts, modePlunger, navCap, partMeasurements } from "./mechanical-parts"
import { HOLE, OLED, PANEL_H, PANEL_W, RAIL_HOLE_X, RAIL_HOLE_Y } from "./layout"
import { readFileSync } from "node:fs"
import { join } from "node:path"
import { checkAperture, checkMechanicalNumbers, checkMechanicalParts, envelopeSpecs, pendingMechanicalNotes } from "../scripts/mechanical_check"

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

// ---------------------------------------------------------------------------
// Finite-input rejection. In JavaScript every comparison with NaN is false, so
// an unvalidated NaN would have PASSED every limit in checkStack.
// ---------------------------------------------------------------------------
test("non-finite stack inputs are named and rejected, never compared", () => {
  expect(nonFinite({ gap: 11, shoulders: "6" })).toEqual(["shoulders"])
  const bad: [string, unknown][] = [
    ["gap", Number.NaN], ["shoulder", Number.POSITIVE_INFINITY], ["post", Number.NEGATIVE_INFINITY],
    ["socket", Number.NaN], ["j1Height", Number.NaN], ["frontTail", undefined], ["gap", "11"],
  ]
  for (const [key, value] of bad) {
    const input = { ...nominalStack(), [key]: value } as unknown as ReturnType<typeof nominalStack>
    expect(checkStack(input)).toContainEqual(expect.stringMatching(new RegExp(`not a finite number: .*${key}`)))
    expect(() => measureStack(input)).toThrow(RangeError)
  }
  expect(() => actuatorFit({ ...nominalFit(), tiltDegrees: Number.NaN })).toThrow(/not finite millimetres: tiltDegrees/)
  expect(() => actuatorFit({ ...nominalFit(), printTolerance: Number.POSITIVE_INFINITY })).toThrow(RangeError)
  expect(() => fastenerOdLimit(Number.NaN)).toThrow(RangeError)
  expect(() => hardwareFit(2.95, Number.NaN)).toThrow(RangeError)
})

test("a NaN in one corner of the enumeration cannot hide behind a good corner", () => {
  // A missing tolerance must fail the enumeration, not shrink it silently.
  expect(() => actuatorCorners().map((c) => ({ ...c, panelGapMin: Number.NaN })).map(actuatorFit))
    .toThrow(RangeError)
})

// ---------------------------------------------------------------------------
// Exhaustive tolerance corners.
// ---------------------------------------------------------------------------
test("all 64 stack tolerance corners stay inside the acceptance window", () => {
  const corners = stackCorners()
  expect(corners.length).toBe(64)
  for (const corner of corners) {
    const m = measureStack(corner)
    expect(Number.isFinite(m.insertion + m.bodyGap + m.j1Clearance)).toBe(true)
    expect(m.insertion).toBeGreaterThanOrEqual(CONNECTOR_Z.insertionMin)
    expect(m.insertion).toBeLessThanOrEqual(CONNECTOR_Z.insertionMax)
    expect(m.bodyGap).toBeGreaterThanOrEqual(0)
    expect(m.j1Clearance).toBeGreaterThanOrEqual(STACK.minInterboardClearance)
    // every corner must be a complete, finite StackInput
    expect(nonFinite(corner)).toEqual([])
  }
  const limits = stackCornerLimits()
  expect(limits.corners).toBe(64)
  // The documented arithmetic, reproduced from the corners and not quoted.
  expect(limits.insertionMin).toBeCloseTo(2.55, 9)
  expect(limits.insertionMax).toBeCloseTo(4.05, 9)
  expect(limits.bodyGapMin).toBeCloseTo(0.15, 9)
  expect(limits.j1ClearanceMin).toBeCloseTo(0.4, 9)
  // The engineering acceptance window is a POLICY margin, and it is only 0.05mm.
  expect(limits.insertionMin - CONNECTOR_Z.insertionMin).toBeCloseTo(0.05, 9)
  expect(CONNECTOR_Z.insertionMax - limits.insertionMax).toBeCloseTo(0.05, 9)
})

test("each stack measure is monotonic in every tolerance it contains", () => {
  const increasing = (a: number, b: number) => expect(b).toBeGreaterThan(a)
  const base = nominalStack()
  for (const delta of [-STACK.baseGapTolerance, STACK.baseGapTolerance]) {
    const low = measureStack({ ...base, gap: base.gap + delta - 1e-6 })
    const high = measureStack({ ...base, gap: base.gap + delta + 1e-6 })
    increasing(low.bodyGap, high.bodyGap) // more gap: more body clearance
    increasing(low.j1Clearance, high.j1Clearance)
    expect(high.insertion).toBeLessThan(low.insertion) // more gap: less insertion
  }
  const taller = (key: "shoulder" | "post" | "socket") => {
    const low = measureStack({ ...base, [key]: base[key] - 1e-3 })
    const high = measureStack({ ...base, [key]: base[key] + 1e-3 })
    expect(high.insertion).toBeGreaterThan(low.insertion)
    expect(high.j1Clearance).toBeCloseTo(low.j1Clearance, 12)
    // bodyGap = gap - shoulder - socket: it does not contain the post length
    if (key === "post") expect(high.bodyGap).toBeCloseTo(low.bodyGap, 12)
    else expect(high.bodyGap).toBeLessThan(low.bodyGap)
  }
  taller("shoulder")
  taller("post")
  taller("socket")
  const lowJ1 = measureStack({ ...base, j1Height: base.j1Height - 1e-3 })
  const highJ1 = measureStack({ ...base, j1Height: base.j1Height + 1e-3 })
  expect(highJ1.j1Clearance).toBeLessThan(lowJ1.j1Clearance)
  expect(highJ1.insertion).toBeCloseTo(lowJ1.insertion, 12)
})

test("actuator corners keep every fit limit, with the small margins reported", () => {
  expect(actuatorCorners().length).toBe(32)
  const { corners, worst } = actuatorCornerLimits()
  expect(corners).toBe(32)
  for (const limit of FIT_LIMITS) expect(worst[limit.key]).toBeGreaterThanOrEqual(limit.min)
  // The NAV radial margin was 0.004mm above the old requirement at a 5.8mm cup;
  // the 5.6mm cup must leave a real margin, not a rounding one.
  expect(worst.joystickRadialClearance).toBeGreaterThan(ACTUATORS.minRadialClearance + 0.1)
  expect(worst.joystickRadialClearance).toBeCloseTo(0.305, 3)
  // The walls are judged at the worst of the INDEPENDENT cup/pocket/roof print
  // errors, so the corner is 0.275mm and not the nominal 0.396mm.
  expect(worst.joystickPocketCornerWallWorst).toBeGreaterThanOrEqual(ACTUATORS.minWall)
  expect(worst.joystickPocketCornerWallWorst).toBeCloseTo(0.275, 3)
  expect(worst.joystickPocketCornerWall).toBeCloseTo(0.396, 3)
  expect(worst.joystickPocketWallWorst).toBeCloseTo(1.0, 9)
  expect(worst.joystickRoofThicknessWorst).toBeCloseTo(ACTUATORS.joystick.cupRoof - ACTUATORS.printTolerance, 9)
})

// ---------------------------------------------------------------------------
// M3 hardware: the limit is derived from the nearest component envelope.
// ---------------------------------------------------------------------------
test("the hardware limit is derived from the measured nearest envelope", () => {
  expect(fastenerOdLimit(2.95)).toBeCloseTo(5.0, 9)
  expect(fastenerOdLimit(3.05)).toBeCloseTo(5.2, 9)
  expect(fastenerOdLimit(0.2)).toBe(0) // never negative
  const ok = hardwareFit(2.95, 5.0)
  expect(ok.verdict).toBe("ok")
  expect(ok.clearance).toBeCloseTo(0.45, 9)
  expect(ok.limit).toBeCloseTo(5.0, 9)
})

test("an ordinary 5.5mm M3 socket head does NOT satisfy the 5.0mm limit", () => {
  const head = hardwareFit(2.95, FASTENERS.socketHeadDiameterIso4762)
  expect(head.verdict).toBe("tight") // fits, but eats 0.25mm of the 0.45mm budget
  expect(head.clearance).toBeCloseTo(0.2, 9)
  expect(head.od).toBeGreaterThan(head.limit)
  // No face may present an unselected head as approved hardware.
  for (const spec of envelopeSpecs("base").concat(envelopeSpecs("front")))
    if (spec.od === FASTENERS.socketHeadDiameterIso4762) expect(spec.status).toBe("pending")
})

test("an ISO 7089 M3 plain washer collides at the tight standoff", () => {
  const washer = hardwareFit(2.95, FASTENERS.plainWasherDiameterIso7089M3)
  expect(washer.verdict).toBe("collision")
  expect(washer.clearance).toBeCloseTo(-0.55, 9)
})

test("every declared fastener face is checked and no limit face is pending", () => {
  const specs = envelopeSpecs("base").concat(envelopeSpecs("front"))
  expect(specs.filter((s) => s.status === "limit").every((s) => s.od === FASTENERS.hardwareDiameterMax)).toBe(true)
  // spacer ends and screw heads are both covered, on both boards
  expect(new Set(specs.filter((s) => s.status === "limit").map((s) => `${s.hardware}/${s.face}`)))
    .toEqual(new Set(["spacer/top", "screw/bottom", "spacer/bottom", "screw/top"]))
  expect(GLOBAL_STANDOFF_FACES()).toBe(STANDOFF_FACES.length)
  expect(STANDOFF_HOLE_XY.length).toBe(4)
})
const GLOBAL_STANDOFF_FACES = () => new Set(STANDOFF_FACES.map((s) => `${s.board}/${s.face}`)).size

test("unresolved supplier identities stay unresolved, with no guessed part", () => {
  expect(UNRESOLVED.length).toBeGreaterThanOrEqual(4)
  for (const u of UNRESOLVED) {
    expect(u.supplier).toBeNull()
    expect(u.status.length).toBeGreaterThan(10)
    expect(u.status).not.toMatch(/C\d{6,}|^\s*[A-Z0-9-]{8,}$/ms) // no LCSC code, no invented MPN
  }
  expect(PENDING_ACCEPTANCE.length).toBeGreaterThanOrEqual(6)
  expect(pendingMechanicalNotes().map((n) => n.id)).toContain("head-washer")
})

test("panel aperture never crops the OLED active area and stays inside the body", () => {
  expect(checkAperture()).toEqual([])
  expect(OLED.windowW - OLED_MODULE.activeW).toBeCloseTo(1.08, 9)
  expect(OLED.windowH - OLED_MODULE.activeH).toBeCloseTo(1.3, 9)
  expect(OLED_MODULE.activeW).toBeLessThan(OLED.bodyW)
  expect(OLED_MODULE.activeH).toBeLessThan(OLED.bodyH)
})

// ---------------------------------------------------------------------------
// Rack interface: the rail mounting holes are on the 5.08mm grid (owner
// approved); the panel width is still an open rack item.
// ---------------------------------------------------------------------------
const PANEL_FIXTURE = join(import.meta.dir, "../fixtures/panel-baseline.json")
const FROZEN_HOLES: { x: number; y: number; diameter: number }[] = JSON.parse(readFileSync(PANEL_FIXTURE, "utf8")).holes

test("rail mounting holes sit on the 5.08mm grid, symmetric about x = 0", () => {
  expect(2 * RAIL_HOLE_X).toBeCloseTo(17 * 5.08, 6) // 86.36mm
  expect(RAIL_HOLE_X).toBeCloseTo(43.18, 6)
  expect(2 * RAIL_HOLE_Y).toBeCloseTo(PANEL_H - 6, 9) // 122.50mm, 3.0mm from each end
  expect(PANEL_W / 2 - RAIL_HOLE_X).toBeCloseTo(7.62, 6) // 7.62mm edge offset
  const fit = rackFit({ panelWidth: PANEL_W, panelHeight: PANEL_H, holeDiameter: HOLE.rail, holeX: RAIL_HOLE_X, holeY: RAIL_HOLE_Y })
  expect(fit.pitchX).toBeCloseTo(86.36, 6)
  expect(Math.abs(fit.pitchMismatch)).toBeLessThan(RACK_GRID_TOLERANCE)
  expect(fit.pitchMultiple).toBe(17)
  expect(fit.pitchMargin).toBeCloseTo(fit.holeSlackPair, 6) // no grid error left to absorb
  expect(rackGridErrors({ panelWidth: PANEL_W, panelHeight: PANEL_H, holeDiameter: HOLE.rail, holeX: RAIL_HOLE_X, holeY: RAIL_HOLE_Y }))
    .toEqual([])
})

test("the old 86.60mm rail pair fails the grid rule", () => {
  const old = { panelWidth: PANEL_W, panelHeight: PANEL_H, holeDiameter: HOLE.rail, holeX: 43.3, holeY: RAIL_HOLE_Y }
  const fit = rackFit(old)
  expect(fit.pitchX).toBeCloseTo(86.6, 9)
  expect(fit.pitchMismatch).toBeCloseTo(0.24, 9)
  expect(fit.pitchMargin).toBeCloseTo(-0.04, 9) // hole take-up cannot absorb it
  const errors = rackGridErrors(old)
  expect(errors).toHaveLength(1)
  expect(errors[0]).toContain("off the 5.08mm grid")
  expect(fit.pitchError).toBeCloseTo(0.24, 9) // too wide
  // a too-NARROW pair is just as far off the grid and must not gain margin
  const narrow = rackFit({ ...old, holeX: 43.05 }) // 86.10mm
  expect(narrow.pitchX).toBeCloseTo(86.1, 9)
  expect(narrow.pitchError).toBeCloseTo(-0.26, 9)
  expect(narrow.pitchMargin).toBeCloseTo(-0.06, 9)
  expect(rackGridErrors({ ...old, holeX: 43.05 })).toHaveLength(1)
  // a vertical-end mistake and an asymmetric pair are caught too
  expect(rackGridErrors({ ...old, holeX: RAIL_HOLE_X, holeY: 61.0 })).toHaveLength(1)
})

test("the frozen panel fixture holds the four moved rail holes and nothing else moved", () => {
  const rail = FROZEN_HOLES.filter((h) => Math.abs(Math.abs(h.y) - RAIL_HOLE_Y) < 0.01 && h.diameter === 3.2)
  expect(rail).toHaveLength(4)
  for (const hole of rail) {
    expect(Math.abs(hole.x)).toBeCloseTo(Math.abs(RAIL_HOLE_X), 6)
    expect(Math.abs(hole.y)).toBeCloseTo(Math.abs(RAIL_HOLE_Y), 6)
  }
  // 27 frozen circular holes plus the separate OLED window; the count is read
  // from the fixture, never assumed from a note (older text said 31).
  expect(FROZEN_HOLES).toHaveLength(27)
  expect(FROZEN_HOLES.filter((h) => h.diameter === 3.2)).toHaveLength(4 + 2 + 1) // 4 rail, 2 jack LEDs, 1 indicator
  expect(FROZEN_HOLES.filter((h) => h.diameter === 6.2)).toHaveLength(12)
  expect(FROZEN_HOLES.filter((h) => h.diameter === 7.2)).toHaveLength(6)
  expect(FROZEN_HOLES.filter((h) => h.diameter === 12.2)).toHaveLength(1)
  expect(FROZEN_HOLES.filter((h) => h.diameter === 8)).toHaveLength(1)
  const revisions = JSON.parse(readFileSync(PANEL_FIXTURE, "utf8")).mechanicalRevisions
  expect(revisions).toHaveLength(3)
  expect(revisions[2].movedHoles).toHaveLength(4)
  expect(revisions[2].reason).toContain("86.36")
})

test("the 11mm panel gap is smaller than the selected jack body", () => {
  // Recorded so nobody reads the panel-gap window as proven jack clearance.
  expect(PANEL_PARTS.jackBodyHeight).toBeGreaterThan(STACK.panelGapMin)
  expect(pendingMechanicalNotes().map((n) => n.id)).toContain("jack-body")
  expect(pendingMechanicalNotes().map((n) => n.id)).toContain("rack-pitch-measured")
  expect(pendingMechanicalNotes().map((n) => n.id)).toContain("rack-width-measured")
})

// ---------------------------------------------------------------------------
// Built-board supplier identities.
// ---------------------------------------------------------------------------
const boards = (): Record<string, any>[][] => [HEADERS, SOCKETS].map((parts, b) => Object.entries(parts).flatMap(([name, p], i) => [
  { type: "source_component", name, source_component_id: `s${i}`, manufacturer_part_number: p.mpn, supplier_part_numbers: { jlcpcb: [p.code] } },
  { type: "pcb_component", source_component_id: `s${i}`, layer: b ? "bottom" : "top" },
  ...Array.from({ length: p.pins }, (_, n) => ({ type: "source_port", source_component_id: `s${i}`, name: `pin${n + 1}` })),
]))

test("built supplier identity check accepts the exact selected parts", () => {
  const [base, front] = boards()
  expect(checkMechanicalParts(base!, front!)).toEqual([])
  expect(MATED_PADS).toBe(39) // 16 + 16 + 7, derived from SOCKETS
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

test("a dropped mated pad changes the counted total, not just one pin", () => {
  const [base, front] = boards()
  front!.splice(3, 1) // one CN1 socket pad
  const errors = checkMechanicalParts(base!, front!)
  expect(errors.some((e) => e.includes(`${MATED_PADS - 1} mated pads`))).toBe(true)
})

// ---------------------------------------------------------------------------
// Printable actuators.
// ---------------------------------------------------------------------------
test("bare actuators are too short; supplied extensions clear the thick panel", () => {
  const top = STACK.panelGap + STACK.panelThicknessMax
  expect(ACTUATORS.button.bareHeight).toBeLessThan(top)
  expect(ACTUATORS.joystick.bareHeight).toBeLessThan(top)
  const fit = actuatorFit()
  expect(fit.buttonProud).toBeGreaterThanOrEqual(ACTUATORS.minProud)
  expect(fit.joystickProud).toBeGreaterThanOrEqual(ACTUATORS.minProud)
  // Press travel must remain below this, or the captive lip preloads the switch.
  expect(fit.buttonFlangeClearance).toBeGreaterThanOrEqual(0.45)
  expect(fit.joystickRadialClearance).toBeGreaterThan(ACTUATORS.minRadialClearance)
  expect(fit.joystickHeadClearance).toBeGreaterThan(0)
  // Old 7mm NAV hole cannot carry the extended, tilted cup at tolerance limits.
  expect(fit.joystickRadialClearance - (ACTUATORS.joystick.holeDiameter - 7) / 2).toBeLessThan(0)
})

test("the NAV cup keeps a printable stem pocket after the worst print error", () => {
  const p = ACTUATORS.joystick
  expect(p.cupDiameter).toBe(5.6)
  expect(p.cupDiameter - p.pocketWidth).toBeGreaterThan(2 * ACTUATORS.minWall)
  expect(p.cupDiameter - ACTUATORS.printTolerance).toBeGreaterThan(p.pocketWidth)
  expect(p.pocketDepth).toBeGreaterThan(p.cupRoof)
})

test("the 5.4mm cup FAILS the real corner wall, which is why it is not selected", () => {
  const p = ACTUATORS.joystick
  const rejected = printedNavWalls(5.4, p.pocketWidth, p.cupRoof, ACTUATORS.printTolerance)
  // (5.4 - 0.1 - (3.4 + 0.1) x sqrt2) / 2 = 0.175mm, below the 0.25mm policy
  expect(rejected.corner).toBeCloseTo(0.175, 3)
  expect(rejected.corner).toBeLessThan(ACTUATORS.minWall)
  // The nominal wall of that cup still LOOKED printable: the adverse errors are
  // what make the difference, so a nominal-only check would have passed it.
  expect((5.4 - p.pocketWidth) / 2).toBeGreaterThan(ACTUATORS.minWall)
  const selected = printedNavWalls(p.cupDiameter, p.pocketWidth, p.cupRoof, ACTUATORS.printTolerance)
  expect(selected.corner).toBeGreaterThanOrEqual(ACTUATORS.minWall)
  // The gate must judge the worst-case keys, never the nominal ones alone.
  expect(FIT_LIMITS.map((l) => l.key)).toEqual(expect.arrayContaining([
    "joystickPocketCornerWallWorst", "joystickPocketWallWorst", "joystickRoofThicknessWorst",
  ]))
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
  // The head (6.8mm) is wider than the cup, so it sets the X extent.
  expect(nav!.bounds[1][0] - nav!.bounds[0][0]).toBeCloseTo(ACTUATORS.joystick.headDiameter, 5)
  expect(ACTUATORS.joystick.cupDiameter).toBeLessThan(ACTUATORS.joystick.headDiameter)
  expect(nav!.bounds[1][2] - nav!.bounds[0][2]).toBeCloseTo(ACTUATORS.joystick.pocketDepth + ACTUATORS.joystick.extension, 5)
})

test("NAV pocket is blind, not a through hole or solid stem collision", () => {
  const p = ACTUATORS.joystick
  const stem = primitives.cuboid({ size: [p.stemWidth, p.stemWidth, p.pocketDepth - 0.1], center: [0, 0, p.pocketDepth / 2] })
  expect(measurements.measureVolume(booleans.intersect(navCap(), stem))).toBeCloseTo(0, 5)
  const roof = primitives.cuboid({ size: [1, 1, 0.2], center: [0, 0, p.pocketDepth + 0.2] })
  expect(measurements.measureVolume(booleans.intersect(navCap(), roof))).toBeCloseTo(0.2, 4)
})

test("the printed cap carries material across the whole 45 degree pocket corner", () => {
  const p = ACTUATORS.joystick
  const corner = p.pocketWidth * Math.SQRT2 / 2 // 2.404mm from the axis
  const mid = (corner + p.cupDiameter / 2) / 2
  const probe = (r: number) => {
    const d = r / Math.SQRT2
    return measurements.measureVolume(booleans.intersect(navCap(),
      primitives.cuboid({ size: [0.2, 0.2, 0.2], center: [d, d, 1] })))
  }
  expect(probe(mid)).toBeCloseTo(0.008, 4) // solid wall between pocket corner and cup
  expect(probe(corner - 0.2)).toBeCloseTo(0, 6) // pocket void, as designed
})

test("MODE has a retaining flange larger than the hole and a sliding barrel smaller than it", () => {
  expect(ACTUATORS.button.flangeDiameter).toBeGreaterThan(12.2)
  expect(ACTUATORS.button.barrelDiameter + ACTUATORS.printTolerance).toBeLessThan(12.2 - ACTUATORS.holeUndersize)
  const middle = primitives.cuboid({ size: [1, 1, 0.2], center: [0, 0, 3] })
  expect(measurements.measureVolume(booleans.intersect(modePlunger(), middle))).toBeCloseTo(0.2, 4)
})

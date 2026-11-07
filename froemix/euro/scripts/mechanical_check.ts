#!/usr/bin/env bun
// Run after BOTH boards have been built. No geometry is inferred from a picture.
import { readFileSync } from "node:fs"
import { join } from "node:path"
import { BASE_GAP, HOLE, OLED, OLED_WINDOW, PANEL_GAP, PANEL_H, PANEL_W, RAIL_HOLE_X, RAIL_HOLE_Y, STANDOFF_HOLES } from "../lib/layout"
import {
  ACTUATORS, CONNECTOR_Z, FASTENERS, FIT_LIMITS, HEADERS, MATED_PADS, OLED_MODULE, PANEL_PARTS,
  PENDING_ACCEPTANCE, RACK, SOCKETS, STACK, STANDOFF_FACES, STANDOFF_HOLE_DIAMETER, STANDOFF_HOLE_XY, UNRESOLVED,
  actuatorCornerLimits, actuatorFit, checkStack, fastenerOdLimit, measureStack, nominalFit, rackFit, rackGridErrors,
  stackCornerLimits,
} from "../lib/mechanical"
import { boardEnvelopes, clearanceLines, fastenerClearance, type CircuitElement, type EnvelopeSpec } from "../lib/mechanical-clearance"

type Element = Record<string, any>
const ROOT = join(import.meta.dir, "..")

export const checkMechanicalParts = (base: Element[], front: Element[]): string[] => {
  const errors: string[] = []
  let mated = 0
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
      if (side === "bottom") mated += ports.length
    }
  }
  // Derived from SOCKETS, so a dropped or split socket cannot shrink the
  // contract silently: the front board carries exactly this many mated pads.
  if (mated !== MATED_PADS) errors.push(`front board carries ${mated} mated pads, the contract has ${MATED_PADS}`)
  return errors
}

/** Panel aperture vs the OLED module drawing: the window must not crop it. */
export const checkAperture = (): string[] => {
  const errors: string[] = []
  if (OLED.windowW !== 30.5 || OLED.windowH !== 16.0)
    errors.push(`layout.ts panel window is ${OLED.windowW} x ${OLED.windowH}, the mechanical contract says 30.5 x 16.0`)
  if (OLED.windowW <= OLED_MODULE.activeW || OLED.windowH <= OLED_MODULE.activeH)
    errors.push(`panel window ${OLED.windowW} x ${OLED.windowH} crops the ${OLED_MODULE.activeW} x ${OLED_MODULE.activeH} active area`)
  if (OLED.windowW > OLED.bodyW || OLED.windowH > OLED.bodyH)
    errors.push(`panel window ${OLED.windowW} x ${OLED.windowH} is larger than the ${OLED.bodyW} x ${OLED.bodyH} module body`)
  if (OLED_WINDOW.x !== 0) errors.push(`OLED window centre x is ${OLED_WINDOW.x}, the module sits at x = 0`)
  return errors
}

export const checkMechanicalNumbers = (): string[] => {
  const errors = checkStack()
  if (BASE_GAP !== STACK.baseGap || PANEL_GAP !== STACK.panelGap || HOLE.joy !== ACTUATORS.joystick.holeDiameter)
    errors.push("layout.ts disagrees with the mechanical assembly contract")
  for (const [i, hole] of STANDOFF_HOLES.entries())
    if (hole.x !== STANDOFF_HOLE_XY[i]!.x || hole.y !== STANDOFF_HOLE_XY[i]!.y || HOLE.standoff !== STANDOFF_HOLE_DIAMETER)
      errors.push(`layout.ts standoff ${i} disagrees with the mechanical contract`)
  if (Math.abs(RAIL_HOLE_Y - (128.5 / 2 - 3.0)) > 1e-9)
    errors.push("layout.ts rail holes are not 3.0mm from the panel ends")
  // Rail mounting holes on the 5.08mm grid is an approved requirement; the
  // 101.6mm width against Doepfer's 101.30mm stays a pending rack item.
  errors.push(...rackGridErrors({
    panelWidth: 101.6, panelHeight: 128.5, holeDiameter: HOLE.rail, holeX: RAIL_HOLE_X, holeY: RAIL_HOLE_Y,
  }))

  // Tolerance corners, not the nominal numbers: the whole acceptance window.
  const stack = stackCornerLimits()
  if (stack.corners !== 64) errors.push(`expected 64 stack tolerance corners, generated ${stack.corners}`)
  if (stack.insertionMin < CONNECTOR_Z.insertionMin || stack.insertionMax > CONNECTOR_Z.insertionMax)
    errors.push(`worst-case mating insertion ${stack.insertionMin.toFixed(2)}..${stack.insertionMax.toFixed(2)}mm is outside the ${CONNECTOR_Z.insertionMin}–${CONNECTOR_Z.insertionMax}mm acceptance window`)
  if (stack.bodyGapMin < 0)
    errors.push(`connector insulators collide by ${(-stack.bodyGapMin).toFixed(2)}mm at the tolerance corners`)
  if (stack.j1ClearanceMin < STACK.minInterboardClearance)
    errors.push(`J1/shunts can hit the trimmed front-board tails: ${stack.j1ClearanceMin.toFixed(2)}mm < ${STACK.minInterboardClearance}mm`)

  // Actuator corners: worst value of every fit number must reach its limit.
  const fit = actuatorCornerLimits()
  if (fit.corners !== 32) errors.push(`expected 32 actuator tolerance corners, generated ${fit.corners}`)
  for (const limit of FIT_LIMITS) {
    const value = fit.worst[limit.key]
    if (!Number.isFinite(value)) errors.push(`actuator fit ${limit.key} is not a finite number`)
    else if (value < limit.min - 1e-9)
      errors.push(`actuator fit ${limit.key} is ${value.toFixed(3)}mm at the worst corner, limit ${limit.min}mm: ${limit.description}`)
  }
  errors.push(...checkAperture())
  return errors
}

/** The hardware envelopes checked on each board: limits first, then pending parts. */
export const envelopeSpecs = (board: "base" | "front"): EnvelopeSpec[] =>
  STANDOFF_FACES.filter((s) => s.board === board).flatMap((s): EnvelopeSpec[] => {
    const limit: EnvelopeSpec = {
      hardware: s.hardware,
      face: s.face,
      od: FASTENERS.hardwareDiameterMax,
      status: "limit",
      label: s.hardware === "spacer" ? "spacer end" : "screw head",
    }
    if (s.hardware !== "screw") return [limit]
    return [limit,
      { hardware: "screw", face: s.face, od: FASTENERS.socketHeadDiameterIso4762, status: "pending", label: "ISO 4762 M3 socket head (unselected)" },
      { hardware: "screw", face: s.face, od: FASTENERS.plainWasherDiameterIso7089M3, status: "pending", label: "ISO 7089 M3 plain washer (unselected)" }]
  })

const standoffHoles = (elements: Element[], board: string): { x: number; y: number; diameter: number }[] => {
  const holes = elements.filter((e) => e.type === "pcb_hole").map((e) => ({ x: e.x, y: e.y, diameter: e.hole_diameter }))
  if (holes.length !== STANDOFF_HOLE_XY.length) throw new Error(`${board}: expected ${STANDOFF_HOLE_XY.length} NPTH standoff holes, built ${holes.length}`)
  for (const [i, want] of STANDOFF_HOLE_XY.entries()) {
    const got = holes.find((h) => Math.abs(h.x - want.x) < 0.01 && Math.abs(h.y - want.y) < 0.01)
    if (!got) throw new Error(`${board}: no standoff hole at (${want.x}, ${want.y})`)
    if (Math.abs(got.diameter - STANDOFF_HOLE_DIAMETER) > 0.01)
      throw new Error(`${board}: standoff hole at (${want.x}, ${want.y}) is ${got.diameter}mm, not ${STANDOFF_HOLE_DIAMETER}mm`)
  }
  return holes
}

export type BoardClearance = {
  rows: ReturnType<typeof fastenerClearance>
  errors: string[]
  pending: string[]
  /** smallest OD limit any face allows: the purchase limit the boards justify */
  hardwareLimit: number
  envelopes: Record<string, { courtyard: number; pads: number; unmodelled: string[] }>
}

/**
 * Fastener envelopes measured from BOTH built boards, per hole and per face.
 * The nearest component envelope is whatever the board actually places: the
 * limit is derived from it, never assumed to be one fixed distance.
 */
export const boardClearance = (base: Element[], front: Element[]): BoardClearance => {
  const errors: string[] = []
  const pending: string[] = []
  const envelopes: BoardClearance["envelopes"] = {}
  const rows: BoardClearance["rows"] = []
  for (const [board, elements] of [["base", base], ["front", front]] as const) {
    const env = boardEnvelopes(elements)
    envelopes[board] = { ...env.kinds, unmodelled: env.unmodelled }
    let holes: { x: number; y: number; diameter: number }[]
    try {
      holes = standoffHoles(elements, board)
    } catch (error) {
      errors.push(String((error as Error).message))
      continue
    }
    rows.push(...fastenerClearance({
      board, elements, holes, envelopes: envelopeSpecs(board), budget: FASTENERS.budget,
    }))
  }
  for (const row of rows) {
    const nearest = row.nearest ? `${row.nearest.ref} ${row.nearest.kind} at ${row.distance.toFixed(2)}mm` : "no component in reach"
    const where = `${row.board} (${row.hole.x.toFixed(2)}, ${row.hole.y.toFixed(2)}) ${row.face}`
    if (row.status === "limit") {
      if (row.verdict === "collision")
        errors.push(`${where}: OD${row.od.toFixed(1)} ${row.hardware} collides with ${nearest}`)
      else if (row.verdict === "tight")
        errors.push(`${where}: OD${row.od.toFixed(1)} ${row.hardware} leaves ${row.clearance.toFixed(2)}mm to ${nearest}, less than the ${FASTENERS.budget.toFixed(2)}mm budget`)
    } else if (row.verdict !== "ok") {
      pending.push(`${where}: ${row.label} OD${row.od.toFixed(1)} ${row.verdict === "collision" ? "COLLIDES" : "does not keep the budget"} (${row.clearance.toFixed(2)}mm to ${nearest}) - this part may not be used as selected`)
    }
  }
  const hardwareLimit = rows.reduce((min, row) => Math.min(min, row.limit), Infinity)
  if (!Number.isFinite(hardwareLimit)) errors.push("no standoff face produced a hardware limit")
  else if (hardwareLimit < FASTENERS.hardwareDiameterMax - 1e-9)
    errors.push(`measured hardware limit is OD${hardwareLimit.toFixed(2)}mm, below the declared OD${FASTENERS.hardwareDiameterMax}mm purchase limit`)
  return { rows, errors, pending, hardwareLimit, envelopes }
}

/** Rack numbers for the frozen outline: reported, never "fixed" by moving a hole. */
export const rackNumbers = () => rackFit({
  panelWidth: PANEL_W,
  panelHeight: PANEL_H,
  holeDiameter: HOLE.rail,
  holeX: RAIL_HOLE_X,
  holeY: RAIL_HOLE_Y,
})

/**
 * Items no software result can close. Printed by the gate and written into the
 * release manifest, so a PASS is never read as physical approval.
 */
export const pendingMechanicalNotes = (): { id: string; text: string }[] => {
  const jack = PANEL_PARTS.jackBodyHeight - STACK.panelGapMin
  const rack = rackNumbers()
  return [
    ...UNRESOLVED.map((u) => ({ id: u.id, text: `${u.item}: ${u.status}` })),
    ...PENDING_ACCEPTANCE.map((p) => ({ id: p.id, text: `${p.check} - ${p.detail}` })),
    {
      id: "rack-pitch-measured",
      text: `mounting holes are ${rack.pitchX.toFixed(2)}mm apart (${rack.pitchMultiple} x ${RACK.hpPitch}mm = ${rack.pitchNearestGrid}mm): ${rack.pitchMismatch.toFixed(2)}mm off the grid, hole take-up ${rack.holeSlackPair.toFixed(2)}mm, margin ${rack.pitchMargin.toFixed(2)}mm`,
    },
    {
      id: "rack-width-measured",
      text: `panel is ${rack.panelOversize.toFixed(2)}mm wider than the Doepfer ${RACK.widthActual20hp}mm 20HP figure; ${RACK.panelThickness}mm panel, ${RACK.screw} screws, rail lip depth NOT known`,
    },
    {
      id: "jack-body",
      text: `jack body ${PANEL_PARTS.jackBodyHeight}mm exceeds the ${STACK.panelGapMin}mm minimum panel gap by ${jack.toFixed(2)}mm: the panel must not rest on a jack body`,
    },
    {
      id: "head-washer",
      text: `no head/washer is selected: the derived purchase limit is OD${FASTENERS.hardwareDiameterMax}mm, an ISO 4762 M3 head is ${FASTENERS.socketHeadDiameterIso4762}mm and an ISO 7089 M3 washer is ${FASTENERS.plainWasherDiameterIso7089M3}mm`,
    },
    {
      id: "mode-travel-limit",
      text: `MODE flange clearance is ${actuatorFit(nominalFit()).buttonFlangeClearance.toFixed(2)}mm at the minimum panel gap and the switch travel is not in the K6-1212A drawing`,
    },
  ]
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
  const clearance = boardClearance(base, front)
  errors.push(...clearance.errors)
  const worst = actuatorCornerLimits().worst
  const stack = stackCornerLimits()
  for (const error of errors) console.error(`FAIL ${error}`)
  console.log("stack, tolerance corners (surface-to-surface mm):",
    `insertion ${stack.insertionMin.toFixed(2)}..${stack.insertionMax.toFixed(2)}`,
    `body gap >= ${stack.bodyGapMin.toFixed(2)}`,
    `J1 clearance >= ${stack.j1ClearanceMin.toFixed(2)}`,
    `nominal ${JSON.stringify(measureStack())}`)
  console.log(`actuator worst corners (${FIT_LIMITS.length} limits, ${actuatorCornerLimits().corners} corners):`)
  for (const limit of FIT_LIMITS)
    console.log(`  ${limit.key.padEnd(24)} ${worst[limit.key].toFixed(3)}mm (min ${limit.min})`)
  console.log("hardware envelopes measured from both built boards (budget",
    `${FASTENERS.budget.toFixed(2)}mm, purchase limit OD${FASTENERS.hardwareDiameterMax}mm):`)
  for (const line of clearanceLines(clearance.rows)) console.log(`  ${line}`)
  console.log(`  component envelopes: base ${JSON.stringify(clearance.envelopes.base)}, front ${JSON.stringify(clearance.envelopes.front)}`)
  console.log(`  measured hardware OD limit: ${clearance.hardwareLimit.toFixed(2)}mm`)
  console.log(`mated pads on the front board: ${MATED_PADS}; OLED window border `
    + `${((OLED.windowW - OLED_MODULE.activeW) / 2).toFixed(2)}/${((OLED.windowH - OLED_MODULE.activeH) / 2).toFixed(2)}mm per edge`)
  const rack = rackNumbers()
  console.log(`rack interface: panel ${PANEL_W} x ${PANEL_H}mm, holes ${rack.pitchX.toFixed(2)} x ${rack.pitchY.toFixed(2)}mm, `
    + `grid ${rack.pitchMultiple} x ${RACK.hpPitch} = ${rack.pitchNearestGrid}mm, mismatch ${rack.pitchMismatch.toFixed(2)}mm, `
    + `hole take-up ${rack.holeSlackPair.toFixed(2)}mm, margin ${rack.pitchMargin.toFixed(2)}mm, `
    + `width against ${RACK.widthActual20hp}mm = ${rack.panelOversize.toFixed(2)}mm oversize`)
  if (rack.pitchMargin < 0 || rack.panelOversize > 0)
    console.log("  the frozen outline is NOT declared rack-compatible: see the named rack acceptance items above")
  if (RACK.railLipDepth === null)
    console.log("  rail lip depth unknown: the 128.5mm front PCB at a 9mm panel gap is NOT declared clear of the rails")
  console.log("PENDING (not approved by this gate):")
  for (const note of [...clearance.pending.map((p) => ({ id: "head/washer", text: p })), ...pendingMechanicalNotes()])
    console.log(`  [${note.id}] ${note.text}`)
  console.log("Assembly limits: 11mm spacers; CN4 cut to 7 ways; J1+shunts <=9.5mm; front tails <=1mm; hardware <=5.0mm OD.")
  console.log("Physical fit/contact/retention/CAM acceptance remain first-article tests; not certified by this gate.")
  if (errors.length) process.exit(1)
  console.log("PASS - supplier identities, Z arithmetic, actuator envelopes, aperture and measured fastener envelopes")
}

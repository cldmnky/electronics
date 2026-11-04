// Froemix mechanical assembly contract. mm; dimensions are NOT tscircuit Z props.
// Source drawings, tolerances and assembly limits: doc/MECHANICAL-STACK.md.
// Keep supplier identities here independent of the TSX: the gate checks the
// BUILT boards against them, so substituting a short header cannot pass silently.
//
// Every number below is fed to arithmetic that compares against a limit, so a
// NaN or an Infinity must never reach a comparison (in JavaScript every
// comparison with NaN is false, i.e. a silently PASSING check). `nonFinite`
// names the offending fields; the measure functions throw, the check functions
// return an error, and nothing is ever measured from a non-finite input.

export const STACK = {
  baseGap: 11.0, // clear distance: base TOP to control-board BOTTOM
  baseGapTolerance: 0.1,
  panelGap: 9.0, // control-board TOP to panel UNDERSIDE, assembly target
  // Covers the XKB jack drawing/assembly uncertainty; not a measured shoulder.
  panelGapMin: 8.5,
  panelGapMax: 9.6,
  panelThicknessMin: 1.8,
  panelThicknessMax: 2.2,
  frontLeadProjectionMax: 1.0, // trim through-hole tails facing the base
  j1HeightMax: 9.5, // installed header AND shunts, measured from base top
  minInterboardClearance: 0.25,
} as const

export const HEADERS = {
  CN1: { code: "C2883069", mpn: "X4612WV-16I-C40D28-E60", pins: 16 },
  CN2: { code: "C2883069", mpn: "X4612WV-16I-C40D28-E60", pins: 16 },
  // Buy the stocked 8-way strip; remove the LAST position (both mouldings +
  // pin) BEFORE fitting. The PCB remains seven-way, with pin 1 unchanged.
  CN4: { code: "C2883061", mpn: "X4612WV-08I-C40D28-E60", pins: 7 },
} as const

export const SOCKETS = {
  CN1: { code: "C22373851", mpn: "HC-PM200-4.3H-1x16PZ", pins: 16 },
  CN2: { code: "C22373851", mpn: "HC-PM200-4.3H-1x16PZ", pins: 16 },
  CN4: { code: "C22438139", mpn: "HX PM2.0-1x7P ZC", pins: 7 },
} as const

export const CONNECTOR_Z = {
  headerShoulder: 6.0, // XKB E=6: TOTAL of both insulators + their gap, NOT 6+2
  headerShoulderTolerance: 0.3,
  postLength: 4.0, // XKB C=4: exposed mating post above upper insulator
  postLengthTolerance: 0.2,
  socketHeight: 4.3,
  socketHeightTolerance: 0.15,
  // Engineering acceptance window, not a supplier-certified mating spec.
  // Verify contact/retention/bottoming with actual parts at first article.
  insertionMin: 2.5,
  insertionMax: 4.1,
} as const

export const RACK_GRID_TOLERANCE = 1e-6

/**
 * Horizontal mounting holes must sit on a whole 5.08mm multiple (Doepfer), and
 * the vertical pair 3.0mm from each 3U end. A pair off that grid cannot be
 * absorbed by two O3.2mm holes on nominal O3.0mm M3 screws, so it is an error,
 * not a note. The 101.6mm width stays a pending rack item: shrinking it is an
 * outline revision, not a mechanical-check change.
 */
export const rackGridErrors = (input: RackInput): string[] => {
  const fit = rackFit(input)
  const errors: string[] = []
  if (Math.abs(fit.pitchMismatch) > RACK_GRID_TOLERANCE)
    errors.push(`rail mounting holes are ${fit.pitchX.toFixed(3)}mm apart, ${fit.pitchMismatch.toFixed(3)}mm off the ${RACK.hpPitch}mm grid (nearest ${fit.pitchMultiple} x ${RACK.hpPitch} = ${fit.pitchNearestGrid}mm, hole take-up ${fit.holeSlackPair.toFixed(2)}mm)`)
  const spanY = fit.pitchY
  if (Math.abs(spanY - (input.panelHeight - 2 * 3.0)) > RACK_GRID_TOLERANCE)
    errors.push(`rail mounting holes span ${spanY.toFixed(3)}mm vertically; 3.0mm from each ${input.panelHeight}mm end gives ${(input.panelHeight - 6).toFixed(2)}mm`)
  return errors
}

/**
 * Panel-mounted parts behind the panel. Dimensions are drawing values, not
 * measurements of supplied parts; the panel is the seating jig for both.
 */
export const PANEL_PARTS = {
  // XKB PJ-301M-X-X (C381126) drawing: 8.8mm body above the PCB. This is
  // LARGER than STACK.panelGapMin, so at the minimum gap the panel would sit on
  // the jack body instead of its bushing shoulder: pending physical check.
  jackBodyHeight: 8.8,
  jackNutDiameterMax: 11.5,
  potKnobDiameterMax: 12.5,
  railWasherDiameterMax: 6.0,
} as const

/**
 * Board-to-board hardware at (±36.95, ±60.75) on both boards.
 *
 * The limiting dimension is set by the CLOSEST component envelope to a standoff
 * hole on the face the hardware sits on - measured from the built boards by
 * scripts/mechanical_check.ts, never assumed to be one fixed number. `budget` is
 * the assembly clearance that must survive; the resulting OD limit is derived
 * (`fastenerOdLimit`) and printed, so moving a part closer to a hole shows up as
 * a smaller limit instead of an unnoticed tighter fit.
 *
 * `hardwareDiameterMax` is the purchase limit for every part of the M3 column:
 * the spacer body and any screw head or washer on the free faces. A screw
 * head/washer is NOT selected yet, so the ISO values below are recorded and
 * reported as pending: an ordinary part can never be presented as approved
 * hardware by this file.
 */
export const FASTENERS = {
  thread: "M3",
  spacerLength: STACK.baseGap,
  hardwareDiameterMax: 5.0, // spacer body AND head/washer purchase limit
  budget: 0.45, // minimum retained clearance at the tightest standoff
  // Standard maxima of ordinary parts, kept for the pending decision only.
  socketHeadDiameterIso4762: 5.5,
  plainWasherDiameterIso7089M3: 7.0,
} as const

/**
 * OLED module aperture: the 30.5 x 16mm panel window (lib/layout.ts) and the
 * drawing's active area. The window must not crop the active area and must stay
 * inside the module body; bezel height and support are pending (below).
 */
export const OLED_MODULE = {
  activeW: 29.42, // HS13L02B4S01 drawing, active area
  activeH: 14.70,
} as const

/** Which face of which board each hardware envelope bears on. */
export type StandoffFace = {
  board: "base" | "front"
  face: "top" | "bottom"
  hardware: "spacer" | "screw"
}
export const STANDOFF_FACES: readonly StandoffFace[] = [
  // The spacer column stands between the boards: its ends touch base TOP and
  // front-board BOTTOM. Each board takes its own screw head on the free face.
  { board: "base", face: "top", hardware: "spacer" },
  { board: "base", face: "bottom", hardware: "screw" },
  { board: "front", face: "bottom", hardware: "spacer" },
  { board: "front", face: "top", hardware: "screw" },
] as const

/** Mated pads on the front board: CN1 + CN2 + CN4, derived, not hardcoded. */
export const MATED_PADS =
  SOCKETS.CN1.pins + SOCKETS.CN2.pins + SOCKETS.CN4.pins

/** M3 standoff hole centres, identical on both boards (layouts.ts: STANDOFF_HOLES). */
export const STANDOFF_HOLE_XY: readonly { x: number; y: number }[] = [
  { x: -36.95, y: 60.75 },
  { x: 36.95, y: 60.75 },
  { x: -36.95, y: -60.75 },
  { x: 36.95, y: -60.75 },
] as const
export const STANDOFF_HOLE_DIAMETER = 3.2

/**
 * Rack interface reference (Doepfer A-100 construction details, doepfer.de).
 * The panel and both PCBs are frozen at their nominal outlines; the numbers
 * below are what a rack test must close, NOT a licence to move a hole.
 */
export const RACK = {
  hpPitch: 5.08,
  widthNominal20hp: 101.6, // 20 x 5.08, the panel and both PCBs as drawn
  widthActual20hp: 101.30, // finished width on the Doepfer page
  panelHeight: 128.5,
  panelThickness: 2.0,
  screw: "M3 DIN 7985 pan head",
  threadHoleNominal: 3.0, // nominal M3 clearance in the rail / nut
  // Horizontal mounting holes must sit on a whole multiple of 5.08mm.
  holePitchRule: "N x 5.08mm",
  // No rail profile drawing or lip depth was available: a clearance claim for
  // the 128.5mm PCB 9mm behind the panel cannot be made from these numbers.
  railLipDepth: null,
} as const

export type RackInput = {
  panelWidth: number
  panelHeight: number
  holeDiameter: number
  holeX: number // |x| of one horizontal mounting hole
  holeY: number // |y| of one vertical mounting hole
}

/**
 * Rack numbers for a given panel outline. `holeX`/`holeY` are the frozen hole
 * coordinates, so a hole move changes these results instead of hiding them.
 */
export const rackFit = (input: RackInput) => {
  requireFinite("rackFit", input)
  const pitchX = 2 * Math.abs(input.holeX)
  const pitchY = 2 * Math.abs(input.holeY)
  const multX = Math.round(pitchX / RACK.hpPitch)
  const nearest = multX * RACK.hpPitch
  // A 3.2mm hole on a nominal 3.0mm M3 screw gives 0.1mm of radial slack per
  // hole, i.e. 0.2mm of centre-to-centre take-up across the pair.
  const holeSlackPair = pitchX === 0 ? 0 : 2 * ((input.holeDiameter - RACK.threadHoleNominal) / 2)
  return {
    panelOversize: Number((input.panelWidth - RACK.widthActual20hp).toFixed(3)),
    pitchX,
    pitchY,
    pitchMultiple: multX,
    pitchNearestGrid: Number(nearest.toFixed(2)),
    pitchMismatch: Number((pitchX - nearest).toFixed(3)),
    holeSlackPair: Number(holeSlackPair.toFixed(3)),
    // negative means the hole slack cannot absorb the grid mismatch
    pitchMargin: Number((holeSlackPair - (pitchX - nearest)).toFixed(3)),
  }
}

export const ACTUATORS = {
  button: {
    bareHeight: 7.3,
    heightTolerance: 0.2,
    extension: 5.8, // added height above the EXISTING illuminated cap
    flangeDiameter: 13.6,
    // Thin captive lip only: it must never preload the switch at minimum gap.
    flangeHeight: 0.4,
    barrelDiameter: 11.4, // slides in the unchanged 12.2mm panel hole
    // The K6-1212A drawing gives 7.30 +/-0.2mm overall height and NO travel
    // figure. Press travel is therefore unknown: pending first-article test.
    travelKnown: false,
  },
  joystick: {
    bareHeight: 10.0, // current XUNPU drawing, not the retired Alps switch
    heightTolerance: 0.3,
    pivotHeightMin: 5.8,
    stemWidth: 3.15,
    stemTolerance: 0.05, // XUNPU drawing: 3.15 +/-0.05mm square actuator
    pocketWidth: 3.4,
    pocketDepth: 2.0,
    // 5.4mm, not 5.8mm: the smaller cup keeps the same stem pocket, roof and
    // neck while the swept envelope gains 0.202mm of radial clearance in the
    // unchanged, frozen 8.00mm panel opening. No electronic part moves.
    cupDiameter: 5.4,
    cupRoof: 0.6,
    neckDiameter: 4.0,
    extension: 5.0,
    headDiameter: 6.8,
    headHeight: 1.5,
    // A design/test envelope, NOT an angle specified by the XUNPU datasheet.
    // First article must operate all directions without touching the panel.
    tiltDegrees: 8,
    holeDiameter: 8.0,
  },
  printTolerance: 0.1, // maximum diametral oversize / height error after finishing
  holeUndersize: 0.2, // regular-routing fit allowance, confirm with CAM
  minProud: 0.5,
  // Minimum printed wall between the stem pocket and the outside of the cap.
  // A resin/slicer assumption, not a supplier specification: the first article
  // must show the cup does not split when the cap is dry-fitted.
  minWall: 0.25,
  // Required radial clearance of the tilted, extended stem/cap inside the panel
  // opening, at the outer edge of the +/-8 degree design/test envelope.
  minRadialClearance: 0.2,
} as const

/**
 * Supplier and mechanical identities that are NOT resolved. They stay here,
 * structured and without a part number, so that a substituted or invented part
 * cannot pass as approved hardware. EU hand-part sources only: RS Components,
 * Electrokit or DigiKey with EU sales (froemix/AGENTS.md §9).
 */
export const UNRESOLVED = [
  {
    id: "standoff-fastener",
    item: "M3 female-female spacer, 8 fixing screws, head/washer",
    status:
      "spacer <=5.0mm OD; no verified head/washer <=5.0mm OD (an ISO 4762 M3 head is 5.5mm, an ISO 7089 M3 washer is 7.0mm); select and measure",
    supplier: null,
  },
  {
    id: "daisy-socket",
    item: "2x 1x20 2.54mm female socket strips for the Daisy Seed",
    status: "no verified part; height, tail fit and the Daisy's total projection are unmeasured",
    supplier: null,
  },
  {
    id: "j1-shunt",
    item: "2x 2.54mm jumper shunts for J1",
    status: "no verified low-profile closed shunt; installed J1 + shunts must measure <=9.5mm",
    supplier: null,
  },
  {
    id: "panel-nut-washer",
    item: "jack M6 nuts, pot nuts/washers, pot knob, rail washers",
    status: "envelope limits only (jack <=11.5mm, knob <=12.5mm, rail washer <=6.0mm); no part selected",
    supplier: null,
  },
] as const

/**
 * Acceptance checks that no software result can close. Each keeps its numbers
 * so a reader can see what is still open instead of reading a gate PASS as
 * physical approval.
 */
export const PENDING_ACCEPTANCE = [
  {
    id: "dry-mate",
    check: "dry-mate the three header/socket pairs with 11mm spacers",
    detail: "insertion arithmetic is 2.55..4.05mm; contact, retention and no bottoming are unproven",
  },
  {
    id: "mode-travel",
    check: "measure MODE switch travel against the flange clearance",
    detail: "K6-1212A gives no travel figure; travel must stay below the flange clearance or the lip preloads the switch",
  },
  {
    id: "nav-tilt",
    check: "operate the NAV actuator in all four directions and centre press",
    detail: "+/-8 degrees is a design/test envelope, not a XUNPU rating; radial clearance is computed, not measured",
  },
  {
    id: "jack-seat",
    check: "seat each jack and pot against the actual panel before soldering",
    detail: "the 8.8mm jack body is taller than the 8.5mm minimum panel gap, so the panel must not rest on a jack body",
  },
  {
    id: "oled-support",
    check: "confirm OLED module/bezel Z and support without stressing the glass",
    detail: "the window border is computed from drawing values; actual bezel height and support are unmeasured",
  },
  {
    id: "rack-pitch",
    check: "fit the panel to a real rack with BOTH sliding nuts and fixed threaded strips",
    detail:
      "the rail pair is now on the 5.08mm grid (86.36mm = 17 x 5.08) after the approved 0.12mm move, so the 0.20mm hole take-up is spare; screw-head clearance in the rail slot is still unmeasured",
  },
  {
    id: "rack-width",
    check: "place the finished 101.6mm panel and PCBs beside a real adjacent module",
    detail:
      "Doepfer gives 101.30mm for 20HP, so the panel is 0.30mm oversize; do not move frozen holes or shrink the panel without an owner outline revision",
  },
  {
    id: "rack-rail",
    check: "measure the actual rail profile against the 128.5mm front PCB at the 9mm panel gap",
    detail:
      "no rail drawing or lip depth is available, so rail-lip clearance behind the panel is NOT established and must not be declared clear",
  },
  {
    id: "cam-profile",
    check: "approve Gerber/profile/NPTH files with CAM",
    detail: "the 8.00mm NAV opening, 27 frozen panel holes, 2mm panel material and empty copper are unapproved",
  },
] as const

export type StackInput = {
  gap: number
  shoulder: number
  post: number
  socket: number
  j1Height: number
  frontTail: number
}

/**
 * Names of the entries that are not finite numbers (NaN, +/-Infinity, not a
 * number at all). Used to reject an input before any comparison happens.
 */
export const nonFinite = (values: Record<string, unknown>): string[] =>
  Object.entries(values)
    .filter(([, v]) => typeof v !== "number" || !Number.isFinite(v as number))
    .map(([k]) => k)

const requireFinite = (what: string, values: Record<string, unknown>): void => {
  const bad = nonFinite(values)
  if (bad.length) throw new RangeError(`${what}: not finite millimetres: ${bad.join(", ")}`)
}

export const nominalStack = (): StackInput => ({
  gap: STACK.baseGap,
  shoulder: CONNECTOR_Z.headerShoulder,
  post: CONNECTOR_Z.postLength,
  socket: CONNECTOR_Z.socketHeight,
  j1Height: STACK.j1HeightMax,
  frontTail: STACK.frontLeadProjectionMax,
})

/** Throws on a non-finite input: a NaN must never become a printed clearance. */
export const measureStack = (s: StackInput = nominalStack()) => {
  requireFinite("measureStack", s)
  return {
    insertion: s.shoulder + s.post + s.socket - s.gap,
    bodyGap: s.gap - s.shoulder - s.socket,
    j1Clearance: s.gap - s.j1Height - s.frontTail,
  }
}

export const checkStack = (s: StackInput = nominalStack()): string[] => {
  const bad = nonFinite(s)
  // Reject, do not compare: `NaN < 2.5` is false, so an unvalidated NaN would
  // have passed every limit below.
  if (bad.length) return [`stack input is not a finite number: ${bad.join(", ")}`]
  const m = measureStack(s)
  const errors: string[] = []
  if (m.insertion < CONNECTOR_Z.insertionMin || m.insertion > CONNECTOR_Z.insertionMax)
    errors.push(`mating insertion ${m.insertion.toFixed(2)}mm is outside ${CONNECTOR_Z.insertionMin}–${CONNECTOR_Z.insertionMax}mm`)
  if (m.bodyGap < 0) errors.push(`connector bodies overlap by ${(-m.bodyGap).toFixed(2)}mm`)
  if (m.j1Clearance < STACK.minInterboardClearance)
    errors.push(`J1/shunts vs control-board tails clearance ${m.j1Clearance.toFixed(2)}mm is too small`)
  return errors
}

/**
 * Every extreme of every dimensioned tolerance: 2^4 header/spacer corners, each
 * at the tallest acceptable J1 and the longest acceptable tail as well as at
 * zero. The corner extremes, not the nominal numbers, are what the gate and the
 * tests compare against the acceptance window.
 */
export const stackCorners = (): StackInput[] => {
  const out: StackInput[] = []
  for (const gap of [STACK.baseGap - STACK.baseGapTolerance, STACK.baseGap + STACK.baseGapTolerance])
    for (const shoulder of [
      CONNECTOR_Z.headerShoulder - CONNECTOR_Z.headerShoulderTolerance,
      CONNECTOR_Z.headerShoulder + CONNECTOR_Z.headerShoulderTolerance,
    ])
      for (const post of [
        CONNECTOR_Z.postLength - CONNECTOR_Z.postLengthTolerance,
        CONNECTOR_Z.postLength + CONNECTOR_Z.postLengthTolerance,
      ])
        for (const socket of [
          CONNECTOR_Z.socketHeight - CONNECTOR_Z.socketHeightTolerance,
          CONNECTOR_Z.socketHeight + CONNECTOR_Z.socketHeightTolerance,
        ])
          for (const j1Height of [0, STACK.j1HeightMax])
            for (const frontTail of [0, STACK.frontLeadProjectionMax])
              out.push({ gap, shoulder, post, socket, j1Height, frontTail })
  return out
}

/** Extremes each measured quantity reaches over `stackCorners()`. */
export const stackCornerLimits = () => {
  const measured = stackCorners().map(measureStack).filter((m) => {
    requireFinite("stackCornerLimits", m)
    return true
  })
  const pick = (key: keyof (typeof measured)[number], fn: (a: number, b: number) => number) =>
    measured.reduce((acc, m) => fn(acc, m[key]), measured[0]![key])
  return {
    corners: measured.length,
    insertionMin: pick("insertion", Math.min),
    insertionMax: pick("insertion", Math.max),
    bodyGapMin: pick("bodyGap", Math.min),
    j1ClearanceMin: pick("j1Clearance", Math.min),
  }
}

/**
 * Largest hardware OD that keeps `budget` of clearance to a component envelope
 * that is `nearestEnvelope` away from the standoff hole centre.
 */
export const fastenerOdLimit = (
  nearestEnvelope: number,
  budget: number = FASTENERS.budget,
): number => {
  requireFinite("fastenerOdLimit", { nearestEnvelope, budget })
  return Math.max(0, 2 * (nearestEnvelope - budget))
}

export type HardwareFit = { od: number; clearance: number; limit: number; verdict: "ok" | "tight" | "collision" }
/**
 * Compare one hardware envelope with one measured nearest-envelope distance.
 * `ok` keeps the policy budget, `tight` fits without touching but eats into it,
 * `collision` overlaps the component envelope.
 */
export const hardwareFit = (
  nearestEnvelope: number,
  od: number,
  budget: number = FASTENERS.budget,
): HardwareFit => {
  requireFinite("hardwareFit", { nearestEnvelope, od, budget })
  const clearance = nearestEnvelope - od / 2
  const limit = fastenerOdLimit(nearestEnvelope, budget)
  const verdict = clearance < 0 ? "collision" : od <= limit ? "ok" : "tight"
  return { od, clearance, limit, verdict }
}

/** Panel gap / thickness / print-quality inputs of the actuator fit. */
export type FitInput = {
  panelGapMin: number
  panelGapMax: number
  panelThicknessMin: number
  panelThicknessMax: number
  printTolerance: number
  holeUndersize: number
  tiltDegrees: number
}

export const nominalFit = (): FitInput => ({
  panelGapMin: STACK.panelGapMin,
  panelGapMax: STACK.panelGapMax,
  panelThicknessMin: STACK.panelThicknessMin,
  panelThicknessMax: STACK.panelThicknessMax,
  printTolerance: ACTUATORS.printTolerance,
  holeUndersize: ACTUATORS.holeUndersize,
  tiltDegrees: ACTUATORS.joystick.tiltDegrees,
})

export type FitResult = {
  /** how far each actuator stands beyond the panel top */
  buttonProud: number
  joystickProud: number
  /** radial / axial clearances */
  buttonFlangeClearance: number
  buttonRadialClearance: number
  joystickRadialClearance: number
  joystickHeadClearance: number
  joystickCupHoleClearance: number
  joystickPocketFit: number
  /** printed wall thicknesses of the NAV cap */
  joystickPocketWall: number
  joystickPocketCornerWall: number
  joystickRoofThickness: number
}

/** Throws on a non-finite input (see requireFinite). */
export const actuatorFit = (f: FitInput = nominalFit()): FitResult => {
  requireFinite("actuatorFit", f)
  const a = ACTUATORS
  const panelTop = f.panelGapMax + f.panelThicknessMax
  const angle = f.tiltDegrees * Math.PI / 180
  const cupTop = a.joystick.bareHeight + a.joystick.heightTolerance + a.joystick.cupRoof
  const cupSweep = (cupTop - a.joystick.pivotHeightMin) * Math.tan(angle)
    + (a.joystick.cupDiameter + f.printTolerance) / (2 * Math.cos(angle))
  const neckSweep = (panelTop - a.joystick.pivotHeightMin) * Math.tan(angle)
    + (a.joystick.neckDiameter + f.printTolerance) / (2 * Math.cos(angle))
  const holeRadius = (a.joystick.holeDiameter - f.holeUndersize) / 2
  const cupRadius = a.joystick.cupDiameter / 2
  return {
    buttonProud: a.button.bareHeight - a.button.heightTolerance + a.button.extension - f.printTolerance - panelTop,
    buttonFlangeClearance: f.panelGapMin - (a.button.bareHeight + a.button.heightTolerance + a.button.flangeHeight + f.printTolerance),
    // Press travel must stay below buttonFlangeClearance or the lip loads the switch.
    buttonRadialClearance: (12.2 - f.holeUndersize - a.button.barrelDiameter - f.printTolerance) / 2,
    joystickProud: a.joystick.bareHeight - a.joystick.heightTolerance + a.joystick.extension - f.printTolerance - panelTop,
    joystickRadialClearance: holeRadius - Math.max(cupSweep, neckSweep),
    joystickHeadClearance: a.joystick.bareHeight - a.joystick.heightTolerance
      + a.joystick.extension - a.joystick.headHeight - f.printTolerance
      - a.joystick.headDiameter / 2 * Math.sin(angle) - panelTop,
    // Straight (untilted) drop-in of the finished cup through the panel hole.
    joystickCupHoleClearance: holeRadius - (a.joystick.cupDiameter + f.printTolerance) / 2,
    // Printed pocket vs the real stem: a negative value is an interference fit.
    joystickPocketFit: (a.joystick.pocketWidth - f.printTolerance)
      - (a.joystick.stemWidth + a.joystick.stemTolerance),
    joystickPocketWall: cupRadius - a.joystick.pocketWidth / 2,
    // The thinnest printed wall is at the pocket corners, where the square
    // pocket reaches furthest from the axis.
    joystickPocketCornerWall: cupRadius - a.joystick.pocketWidth * Math.SQRT2 / 2,
    joystickRoofThickness: a.joystick.cupRoof,
  }
}

export type FitLimit = { key: keyof FitResult; min: number; description: string }

/** What each fit number must reach. Failure of any entry is a gate failure. */
export const FIT_LIMITS: readonly FitLimit[] = [
  { key: "buttonProud", min: ACTUATORS.minProud, description: "MODE plunger must stand proud of the highest/thickest panel" },
  { key: "joystickProud", min: ACTUATORS.minProud, description: "NAV head must stand proud of the highest/thickest panel" },
  { key: "buttonFlangeClearance", min: 0, description: "MODE flange must not touch the panel at the minimum gap" },
  { key: "buttonRadialClearance", min: 0, description: "MODE barrel must slide in the panel hole" },
  { key: "joystickRadialClearance", min: ACTUATORS.minRadialClearance, description: "tilted NAV cup/neck must clear the panel hole" },
  { key: "joystickHeadClearance", min: 0, description: "tilted NAV head must not touch the panel top" },
  { key: "joystickCupHoleClearance", min: 0, description: "NAV cup must pass the panel hole" },
  { key: "joystickPocketFit", min: 0, description: "NAV stem pocket must not be an interference fit" },
  { key: "joystickPocketWall", min: ACTUATORS.minWall, description: "NAV pocket side wall must be printable" },
  { key: "joystickPocketCornerWall", min: ACTUATORS.minWall, description: "NAV pocket corner wall must be printable" },
  { key: "joystickRoofThickness", min: ACTUATORS.minWall, description: "NAV pocket roof must be printable" },
] as const

/**
 * Every extreme of the fit inputs: 2^5 combinations of panel gap, panel
 * thickness, print tolerance, hole undersize and tilt angle.
 */
export const actuatorCorners = (): FitInput[] => {
  const out: FitInput[] = []
  for (const panelGap of [STACK.panelGapMin, STACK.panelGapMax])
    for (const thickness of [STACK.panelThicknessMin, STACK.panelThicknessMax])
      for (const printTolerance of [0, ACTUATORS.printTolerance])
        for (const holeUndersize of [0, ACTUATORS.holeUndersize])
          for (const tiltDegrees of [0, ACTUATORS.joystick.tiltDegrees])
            out.push({
              panelGapMin: panelGap,
              panelGapMax: panelGap,
              panelThicknessMin: thickness,
              panelThicknessMax: thickness,
              printTolerance,
              holeUndersize,
              tiltDegrees,
            })
  return out
}

/** Worst (smallest) value each fit number reaches over `actuatorCorners()`. */
export const actuatorCornerLimits = (): { corners: number; worst: FitResult } => {
  const fits = actuatorCorners().map(actuatorFit)
  const worst = { ...fits[0]! }
  for (const fit of fits)
    for (const key of Object.keys(fit) as (keyof FitResult)[])
      worst[key] = Math.min(worst[key], fit[key])
  requireFinite("actuatorCornerLimits", worst)
  return { corners: fits.length, worst }
}

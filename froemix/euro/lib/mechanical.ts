// Froemix mechanical assembly contract. mm; dimensions are NOT tscircuit Z props.
// Source drawings, tolerances and assembly limits: doc/MECHANICAL-STACK.md.
// Keep supplier identities here independent of the TSX: the gate checks the
// BUILT boards against them, so substituting a short header cannot pass silently.

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

export const ACTUATORS = {
  button: {
    bareHeight: 7.3,
    heightTolerance: 0.2,
    extension: 5.8, // added height above the EXISTING illuminated cap
    flangeDiameter: 13.6,
    // Thin captive lip only: it must never preload the switch at minimum gap.
    flangeHeight: 0.4,
    barrelDiameter: 11.4, // slides in the unchanged 12.2mm panel hole
  },
  joystick: {
    bareHeight: 10.0, // current XUNPU drawing, not the retired Alps switch
    heightTolerance: 0.3,
    pivotHeightMin: 5.8,
    stemWidth: 3.15,
    pocketWidth: 3.4,
    pocketDepth: 2.0,
    cupDiameter: 5.8,
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
} as const

export type StackInput = {
  gap: number
  shoulder: number
  post: number
  socket: number
  j1Height: number
  frontTail: number
}

export const nominalStack = (): StackInput => ({
  gap: STACK.baseGap,
  shoulder: CONNECTOR_Z.headerShoulder,
  post: CONNECTOR_Z.postLength,
  socket: CONNECTOR_Z.socketHeight,
  j1Height: STACK.j1HeightMax,
  frontTail: STACK.frontLeadProjectionMax,
})

export const measureStack = (s: StackInput = nominalStack()) => ({
  insertion: s.shoulder + s.post + s.socket - s.gap,
  bodyGap: s.gap - s.shoulder - s.socket,
  j1Clearance: s.gap - s.j1Height - s.frontTail,
})

export const checkStack = (s: StackInput = nominalStack()): string[] => {
  const m = measureStack(s)
  const errors: string[] = []
  if (m.insertion < CONNECTOR_Z.insertionMin || m.insertion > CONNECTOR_Z.insertionMax)
    errors.push(`mating insertion ${m.insertion.toFixed(2)}mm is outside ${CONNECTOR_Z.insertionMin}–${CONNECTOR_Z.insertionMax}mm`)
  if (m.bodyGap < 0) errors.push(`connector bodies overlap by ${(-m.bodyGap).toFixed(2)}mm`)
  if (m.j1Clearance < STACK.minInterboardClearance)
    errors.push(`J1/shunts vs control-board tails clearance ${m.j1Clearance.toFixed(2)}mm is too small`)
  return errors
}

export const actuatorFit = () => {
  const a = ACTUATORS
  const panelTop = STACK.panelGapMax + STACK.panelThicknessMax
  const angle = a.joystick.tiltDegrees * Math.PI / 180
  const cupTop = a.joystick.bareHeight + a.joystick.heightTolerance + a.joystick.cupRoof
  const cupSweep = (cupTop - a.joystick.pivotHeightMin) * Math.tan(angle)
    + (a.joystick.cupDiameter + a.printTolerance) / (2 * Math.cos(angle))
  const neckSweep = (panelTop - a.joystick.pivotHeightMin) * Math.tan(angle)
    + (a.joystick.neckDiameter + a.printTolerance) / (2 * Math.cos(angle))
  const holeRadius = (a.joystick.holeDiameter - a.holeUndersize) / 2
  return {
    buttonProud: a.button.bareHeight - a.button.heightTolerance + a.button.extension - a.printTolerance - panelTop,
    buttonFlangeClearance: STACK.panelGapMin - (a.button.bareHeight + a.button.heightTolerance + a.button.flangeHeight + a.printTolerance),
    // Press travel must stay below buttonFlangeClearance or the lip loads the switch.
    buttonRadialClearance: (12.2 - a.holeUndersize - a.button.barrelDiameter - a.printTolerance) / 2,
    joystickProud: a.joystick.bareHeight - a.joystick.heightTolerance + a.joystick.extension - a.printTolerance - panelTop,
    joystickRadialClearance: holeRadius - Math.max(cupSweep, neckSweep),
    joystickHeadClearance: a.joystick.bareHeight - a.joystick.heightTolerance
      + a.joystick.extension - a.joystick.headHeight - a.printTolerance
      - a.joystick.headDiameter / 2 * Math.sin(angle) - panelTop,
  }
}

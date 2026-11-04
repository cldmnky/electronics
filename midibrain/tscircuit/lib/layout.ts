// Millimetres, front view: +X right, +Y up. Illustrative planning only.
// Nothing here is an approved cutout, footprint or complete component envelope.
//
// SINGLE SOURCE OF LAYOUT COORDINATES. panel.circuit.tsx (panel openings and
// silkscreen labels), front.circuit.tsx (component placement and display
// annotations) and scripts/panel-preview.ts (concept artwork) import every
// position from this file; none of them may re-state a placement literal.
// All positions stay tentative pending the physical checks listed in
// docs/mechanical-stack.md.

export const PANEL = { width: 132.08, height: 128.5, thickness: 1.6 } as const // 26HP x 3U
// Preliminary board bounding boxes only. Positions/Z, cutouts, spacers and the
// actual stack remain unresolved; see docs/mechanical-stack.md.
export const FRONT_PCB = { width: 128, height: 112, thickness: 1.6 } as const
// Rear/base PCB is 20HP (101.6 mm) wide, mounted behind the display/Pi stack.
// The front panel remains 26HP; the extra 6HP is the right-hand control column.
export const REAR_PCB = { width: 20 * 5.08, height: 112, thickness: 1.6 } as const

// M2.5 screw clearance for board and panel standoff holes; M3 for the rails.
export const MOUNT_HOLE_DIAMETER = 2.7 as const
// Keep derived millimetre values free of floating-point noise, so the exported
// mechanical/layout.json and the tests compare exactly.
const mm3 = (v: number) => Math.round(v * 1000) / 1000
export const RAIL_HOLE_DIAMETER = 3.2 as const

// Panel-to-front-board standoffs (M2.5); the panel repeats these holes.
// Moved out from (±60, ±50) to (±61, ±53): the old bottom-left position put an
// M2.5 head inside the J1 MIDI-jack courtyard (jack X −58…−48 against head
// right edge −57.5), and the old top-left position put two M2.5 heads 3.5 mm
// apart (board standoff vs feedthrough-bracket mount). Edge ligament at the
// new spots is 1.65 mm to the 128 × 112 board edge — keepout, not copper.
export const FRONT_BOARD_HOLES = [
  { x: -61, y: -53 }, { x: 61, y: -53 }, { x: -61, y: 53 }, { x: 61, y: 53 },
] as const

// Front-to-rear-board spacer grid (M2.5, spacer length TBD ~18–20 mm pending
// the J5 IDC cable-bend mockup). Shared by both boards; the panel does NOT
// carry these (internal stack only). Top pair sits between the display top
// (39.0) and the relief-notch bottom (48.0) while clearing the U6 courtyard
// (top 38.4) and C5; bottom pair sits below the jack courtyards (bottom
// −54.9) with 2.65 mm edge ligament. See docs/mechanical-stack.md for the
// two residuals: top positions need bracket clearance holes at regen time,
// and (-44, 43) needs low-profile top hardware for the 3.5 mm display gap.
export const FRONT_REAR_MOUNT = [
  { x: -44, y: -52 }, { x: 44, y: -52 }, { x: -44, y: 43 }, { x: 44, y: 43 },
] as const

// Eurorack rail holes in the panel (M3 rack screws). Doepfer 3U, same rule as
// Froemix: X inset 7.5 mm, Y inset 3.0 mm from the panel edge.
export const RAIL_HOLES = [
  { x: -58.54, y: -61.25 }, { x: 58.54, y: -61.25 },
  { x: -58.54, y: 61.25 }, { x: 58.54, y: 61.25 },
] as const

// Pi 5 mounting holes, portrait (49 across X, 58 vertical — the reference
// 58 × 49 grid rotated so the 56 mm Pi edge runs along X). Centre is +6 east
// of the board origin: the Pico module's right edge sits at X −29, so a
// centred Pi (left edge −28) would overlap it by 1 mm and foul the Pico
// (10–12 mm tall) against the Pi PCB (2.5 mm spacer). At +6 the Pi spans
// X −22…+34: 7 mm clear of the Pico, 2.5 mm clear of U3/U4, 8.5 mm clear of
// J_FRONT, 3 mm clear of J_PI. All Pi links are flexible cables (DSI, GPIO
// ribbon, panel pigtails), so nothing else moves. R13–R18/Q3 now sit under
// the Pi PCB — fine for 0603/SOT at a 2.5 mm spacer, but verify the Pi 5
// microSD card (bottom side) against that zone; else use 5 mm spacers.
export const PI_MOUNT = { x: 6, y: 0, pitchX: 49, pitchY: 58 } as const
export const PI_HOLES = [-1, 1].flatMap(sx => [-1, 1].map(sy => ({
  x: PI_MOUNT.x + sx * PI_MOUNT.pitchX / 2,
  y: PI_MOUNT.y + sy * PI_MOUNT.pitchY / 2,
})))

// ---- 3.5-inch DSI LCD, Waveshare SKU 33087 ----
// Vendor drawing: 88.87 x 56.00 mm module, 75.80 x 45.36 mm active area,
// 58 x 49 mm Pi-compatible mounting pattern, 6.70 mm rear depth. DSI, 3.3 V
// and I2C touch share one 15-pin FPC. The display mounts on the FRONT board
// (no PCB cutout); the panel carries the cutout.
// Owner-approved centre (-17, 11): 2 mm left and 4 mm up from (-15, 7).
export const DISPLAY_CONCEPT = { x: -17, y: 11, width: 88.87, height: 56.0, innerWidth: 75.8, innerHeight: 45.36 } as const

// Display-local hole centres from the vendor drawing: module PCB 86.50 x
// 56.00 mm, holes at x = 25.00 / 83.00 mm from the PCB left and y = 3.50 /
// 52.50 mm from the PCB bottom, i.e. -18.25 / +39.75 mm and +-24.5 mm from
// the glass centre. Asymmetric in X: keep the offsets, never re-normalise.
export const DISPLAY_STANDOFFS = [
  { x: -18.25, y: -24.5 },
  { x: 39.75, y: -24.5 },
  { x: -18.25, y: 24.5 },
  { x: 39.75, y: 24.5 },
] as const

// PROVISIONAL display interface, ONE definition only. The earlier 3.5 mm value
// in this file and the 8-10 mm figure in docs/ described two different
// quantities at the same time; they cannot both hold, so 3.5 mm is withdrawn.
// standoffHeight = front-board top face to the display mounting PCB. 9.0 mm is
// the middle of the earlier 8-10 mm candidate range and is bounded below by
// the vendor's 6.7 mm rear envelope plus the unreleased FPC-latch height. It
// is a planning value with a stated Z datum (see PROVISIONAL_Z), not a
// measured clearance. The old separate 3 mm "additionalClearanceSpacer" is
// gone: it would double-count the same gap.
export const DISPLAY_STACK = {
  glass: 88.87,
  overallDepth: 10.7,
  rearEnvelopeDepth: 6.7,
  standoffHeight: 9.0,
  standoffDiameter: 3.0,
  piWidth: 85,
  piHeight: 56,
  piMountPitchLong: 58,
  piMountPitchShort: 49,
} as const
// Bezel and glass in front of the display mounting PCB.
export const DISPLAY_FORWARD_ENVELOPE =
  DISPLAY_STACK.overallDepth - DISPLAY_STACK.rearEnvelopeDepth // 4.0

// Front-board LCD standoff holes derive from the display centre plus the
// fixed display-local offsets above. Do not re-state their positions.
export const LCD_MOUNT_HOLES = DISPLAY_STANDOFFS.map(o => ({
  x: DISPLAY_CONCEPT.x + o.x,
  y: DISPLAY_CONCEPT.y + o.y,
}))

// Active area: 3.60 mm from the glass left and 9.44 mm from the right, so its
// centre is 2.935 mm left of the glass centre. The panel window adds 0.5 mm
// per side; the front-board silkscreen rectangle uses the same window.
const DISPLAY_ACTIVE_OFFSET_X = -2.935
const DISPLAY_WINDOW_CLEARANCE = 0.5
export const LCD_WINDOW = {
  x: DISPLAY_CONCEPT.x + DISPLAY_ACTIVE_OFFSET_X,
  y: DISPLAY_CONCEPT.y,
  width: DISPLAY_CONCEPT.innerWidth + 2 * DISPLAY_WINDOW_CLEARANCE,
  height: DISPLAY_CONCEPT.innerHeight + 2 * DISPLAY_WINDOW_CLEARANCE,
}

// Panel cutout for the LCD glass; centred on the display, sized over the
// 88.87 x 56.00 mm module with a PROVISIONAL allowance. The LCD itself mounts
// on the front board.
//
// Clearance per side: JLCPCB profile/cutout tolerance is +-0.2 mm; the
// module's own tolerance and the panel-to-module registration are NOT
// measured. 0.7 mm = 0.2 fab tolerance + 0.5 unverified registration and
// assembly allowance. Planning value, not a fit approval.
//
// Corners: a routed inside corner cannot be sharp, so the aperture is emitted
// as a rounded rectangle with an explicit relief radius. For per-side
// allowance c and corner radius r the clearance at a square module corner is
// r - |r - c| * sqrt(2). With c = 0.7 and r = 1.0 that is 0.576 mm, so no
// corner is tighter than the 0.5 mm target: the aperture is expanded, never
// rounded inward of the minimum. r = 1.0 mm is the PROVISIONAL router radius;
// confirm it with the fabricator. Do not shrink this aperture to the old
// 89.2 x 56.3 mm (0.165/0.15 mm per side) - that was inside fab tolerance.
export const LCD_CUTOUT_ALLOWANCE = { perSide: 0.7, cornerRadius: 1.0 } as const
export const PANEL_LCD_CUTOUT = {
  x: DISPLAY_CONCEPT.x,
  y: DISPLAY_CONCEPT.y,
  width: mm3(DISPLAY_CONCEPT.width + 2 * LCD_CUTOUT_ALLOWANCE.perSide), // 90.27
  height: mm3(DISPLAY_CONCEPT.height + 2 * LCD_CUTOUT_ALLOWANCE.perSide), // 57.40
  cornerRadius: LCD_CUTOUT_ALLOWANCE.cornerRadius,
} as const

// Rounded-rectangle outline in absolute panel coordinates, counter-clockwise.
// tscircuit has no rounded-rect cutout, so the panel emits this polygon. The
// straight chords lie INSIDE the true corner arc, i.e. toward the module, so
// the polygon is slightly SMALLER than the exact rounded rectangle: the
// approximation can only reduce the aperture, never enlarge it. The measured
// worst case with 6 segments per corner is 0.571 mm clearance at a square
// module corner (scripts/check_placement.ts recomputes it from the built
// panel), which stays above the 0.5 mm target. More segments narrow the gap
// further; fewer would eat into it.
export const roundedRectOutline = (
  rect: { x: number; y: number; width: number; height: number; cornerRadius: number },
  segmentsPerCorner = 6,
): { x: number; y: number }[] => {
  const hw = rect.width / 2
  const hh = rect.height / 2
  const r = Math.min(rect.cornerRadius, hw, hh)
  const corners = [
    { cx: rect.x + hw - r, cy: rect.y + hh - r, start: 0 },           // top right
    { cx: rect.x - hw + r, cy: rect.y + hh - r, start: Math.PI / 2 }, // top left
    { cx: rect.x - hw + r, cy: rect.y - hh + r, start: Math.PI },     // bottom left
    { cx: rect.x + hw - r, cy: rect.y - hh + r, start: 3 * Math.PI / 2 }, // bottom right
  ]
  const points: { x: number; y: number }[] = []
  for (const c of corners) {
    for (let i = 0; i <= segmentsPerCorner; i++) {
      const a = c.start + (Math.PI / 2) * (i / segmentsPerCorner)
      points.push({ x: c.cx + r * Math.cos(a), y: c.cy + r * Math.sin(a) })
    }
  }
  return points
}

// Front-board top-silkscreen engineering notes. Offsets are display-local, so
// the text follows the display; do not place these by absolute coordinates.
export const DISPLAY_ANNOTATIONS = [
  { text: "LCD 3.5in DSI 88.87 x 56.00", dx: 0, dy: 29, fontSize: 1.2 },
  { text: "ACTIVE 75.80 x 45.36", dx: DISPLAY_ACTIVE_OFFSET_X, dy: 26, fontSize: 1.0 },
  { text: "HOLES 58.00 x 49.00 / LEFT 25.00 / RIGHT 3.50", dx: 10, dy: -34, fontSize: 0.9 },
  { text: "DSI 15P", dx: 37, dy: 23, fontSize: 1.0 },
] as const
export const displayAnnotationXY = (a: { dx: number; dy: number }) => ({
  x: DISPLAY_CONCEPT.x + a.dx,
  y: DISPLAY_CONCEPT.y + a.dy,
})

// ---- PROVISIONAL Z stack ----
// Z datum: Z = 0 at the front (operator-side) face of the panel; +Z is
// rearward, into the rack cavity. Millimetres.
// NOT a verified clearance. The chain below rests on TWO explicit provisional
// design assumptions and one candidate part dimension. No value here is a
// measurement, and none of them certifies a gap.
//
// Assumption A: the display glass front sits flush with the panel front face.
// Assumption B: the display standoff is DISPLAY_STACK.standoffHeight (9.0 mm).
// Part dimension: the display is 10.7 mm overall with a 6.7 mm rear envelope
// (vendor). Bezel plus glass forward of the display PCB is therefore 4.0 mm.
//
export const DISPLAY_GLASS_FRONT_Z = 0 as const // Assumption A, not a measurement

export const PROVISIONAL_Z = {
  panelFront: 0,
  panelRear: PANEL.thickness,                                                                // 1.60
  displayFront: DISPLAY_GLASS_FRONT_Z,                                                       // 0.00 (assumed)
  displayPcb: mm3(DISPLAY_GLASS_FRONT_Z + DISPLAY_FORWARD_ENVELOPE),                          // 4.00
  displayRearMost: mm3(DISPLAY_GLASS_FRONT_Z + DISPLAY_FORWARD_ENVELOPE + DISPLAY_STACK.rearEnvelopeDepth), // 10.70
  frontBoardTop: mm3(DISPLAY_GLASS_FRONT_Z + DISPLAY_FORWARD_ENVELOPE + DISPLAY_STACK.standoffHeight),      // 13.00
  frontBoardRear: mm3(DISPLAY_GLASS_FRONT_Z + DISPLAY_FORWARD_ENVELOPE + DISPLAY_STACK.standoffHeight + FRONT_PCB.thickness), // 14.60
} as const
// The panel-to-board gap FALLS OUT of assumptions A and B. It is a
// consequence of a design target, not evidence for it, and it is not certified.
export const PANEL_TO_FRONT_BOARD_GAP = mm3(PROVISIONAL_Z.frontBoardTop - PROVISIONAL_Z.panelRear) // 11.40

// Light-pipe reach is NOT proven. The Bivar PLP2-400-C is 10.16 mm long, so it
// covers 10.16 mm of the 11.4 mm gap and the remainder must come from the 0805
// LED body plus how the pipe seats in a 1.6 mm panel. No LED height and no
// seating allowance is recorded here: both are unmeasured, and inventing them
// would fake a reach check. Pending: the Bivar drawing and the actual LED.
export const LIGHT_PIPE = { part: "Bivar PLP2-400-C", length: 10.16 } as const
export const LIGHT_PIPE_REACH = {
  gap: PANEL_TO_FRONT_BOARD_GAP,                            // 11.40
  pipeLength: LIGHT_PIPE.length,                            // 10.16
  unaccounted: mm3(PANEL_TO_FRONT_BOARD_GAP - LIGHT_PIPE.length), // 1.24, PENDING
} as const

// Tallest front-board top-side part with a manufacturer-stated height: the
// MIDI receiver in the Toshiba 11-4L1S body, maximum installed height
// 2.1 +-0.1 mm (drawing p.15). It does NOT prove the display underside: the
// display's own underside component map, its FPC latch and the module
// tolerance are unmapped. The provisional stack leaves only 0.1 mm to this
// part, so NO assembled clearance is claimed here. Named physical gate.
export const FRONT_TOP_SMD_MAX_HEIGHT = {
  part: "MIDI receiver, Toshiba 11-4L1S body",
  height: 2.2,
  source: "manufacturer drawing p.15, max installed height 2.1 +-0.1 mm",
} as const
export const DISPLAY_UNDERSIDE_GAP = mm3(PROVISIONAL_Z.frontBoardTop - PROVISIONAL_Z.displayRearMost) // 2.30

// Installed-depth target from the owner. Unchanged; every depth figure below
// is provisional and none of them releases the target.
export const DEPTH_TARGET = 110 as const

// Provisional rear chain, panel front face to the rear-most point. Candidates,
// not measurements.
export const PROVISIONAL_DEPTH = {
  frontToRearGap: 20,   // J5 IDC plug plus cable bend; candidate range 18-22
  piStandoffAndPcb: 4,  // Pi spacer plus Pi PCB
  piStackRearward: 30,  // Active Cooler + M.2 HAT+ + SSD; candidate range 20-30
} as const
export const provisionalInstalledDepth = (d = PROVISIONAL_DEPTH): number =>
  mm3(PROVISIONAL_Z.frontBoardRear + d.frontToRearGap + REAR_PCB.thickness + d.piStandoffAndPcb + d.piStackRearward)

// Front-board top opening for the panel-feedthrough bodies. The LCD is mounted
// on the front board; only the USB feedthroughs need this top-edge relief notch.
// The 3D-printed bracket spans the opening and mounts at these holes.
export const FRONT_TOP_NOTCH = { x: -2.5, y: 52, width: 105, height: 8 } as const

// Panel feedthroughs: exact vendor cutouts and mounting-hole spacing.
export const USB_FEEDTHROUGHS = [
  // SparkFun CAB-15455: 8 x 26.4 flange; 18 x 6 panel slot; no screw ears.
  { id: "USB-C POWER", x: -38, y: 53.5, cutoutWidth: 12.6, cutoutHeight: 7.6, holeSpacing: 18, holeDiameter: 3.2 },
  // Adafruit 908: 38.30 x 11.90 flange; 14.65 x 7.10 opening; 2x Ø3.5 on 30 mm centres.
  { id: "UMC AUDIO USB", x: -5, y: 53.5, cutoutWidth: 14.9, cutoutHeight: 7.3, holeSpacing: 30, holeDiameter: 3.5 },
  { id: "MIDI HUB USB", x: 35, y: 53.5, cutoutWidth: 14.9, cutoutHeight: 7.3, holeSpacing: 30, holeDiameter: 3.5 },
] as const

// ---- Feedthrough / strain-relief bracket (3D-printed PETG) ----
// PROVISIONAL geometry. Every position is DERIVED from the canonical panel,
// notch, feedthrough and mount data above; nothing here restates a coordinate.
// mechanical/layout.json is generated from this file by
// `bun run scripts/export-mechanical.ts`, and
// mechanical/feedthrough_bracket.py builds and self-checks the CAD model from
// that file, so the printed part cannot drift from the board data.
//
// The bracket is a 3 mm bar on the REAR side of the front board, spanning the
// top relief notch, carrying the feedthrough cable tails. The feedthrough
// flanges stay forward of it, between the panel and the board.
const NOTCH_LEFT = FRONT_TOP_NOTCH.x - FRONT_TOP_NOTCH.width / 2 // -55.0
export const NOTCH_RIGHT = FRONT_TOP_NOTCH.x + FRONT_TOP_NOTCH.width / 2 // 50.0
export const NOTCH_TOP = FRONT_TOP_NOTCH.y + FRONT_TOP_NOTCH.height / 2 // 56.0 = board top edge
export const NOTCH_BOTTOM = FRONT_TOP_NOTCH.y - FRONT_TOP_NOTCH.height / 2 // 48.0
export const NOTCH_BOUNDS = { left: NOTCH_LEFT, right: NOTCH_RIGHT, bottom: NOTCH_BOTTOM, top: NOTCH_TOP } as const

// Planning allowances with their reason. Change them here only.
export const BRACKET_ALLOWANCE = {
  thickness: 3.0,            // printed bar thickness (unchanged from the first draft)
  mountEdgeWall: 3.0,        // M2.5 nut circumradius ~2.5 mm + 0.5 mm material around it
  mountHeadClearance: 6.5,   // PROVISIONAL M2.5 head/washer envelope; measure the real hardware
  slotClearancePerSide: 1.5, // PROVISIONAL body, cable-bundle and screwdriver access
  displayClearance: 2.5,     // bracket bottom above the display top edge
  tieHoleDiameter: 3.2,      // 2.5 mm cable tie
  materialToCuts: 1.0,       // minimum remaining web between two cuts
} as const

// Bracket mounting holes: the front board carries the same two holes
// (`front.circuit.tsx`), 1.65 mm edge ligament to the 8 mm notch mouths.
const BRACKET_MOUNT_X = [-60, 55] as const
const BRACKET_MOUNT_Y = 46.5

const bracketOutline = {
  x1: mm3(Math.min(...BRACKET_MOUNT_X) - BRACKET_ALLOWANCE.mountEdgeWall), // -63.0
  x2: mm3(Math.max(...BRACKET_MOUNT_X) + BRACKET_ALLOWANCE.mountEdgeWall), // 58.0
  y1: mm3(DISPLAY_CONCEPT.y + DISPLAY_CONCEPT.height / 2 + BRACKET_ALLOWANCE.displayClearance), // 41.5
  y2: NOTCH_TOP, // 56.0
}

// Open-top cable slots, derived per feedthrough in BOTH directions. Width is
// the VENDOR PANEL CUTOUT plus access clearance; the previous 20/40/40 mm slots
// were flange-sized and touched each other, leaving a 0 mm web between two
// 40 mm slots at x = -5 and +35.
//
// Height: the slot floor is derived from each feedthrough's declared vertical
// opening plus the same per-side clearance, NOT from a fixed y. A fixed floor
// at y = 50 cut into even the declared panel openings (USB-C reaches
// 53.5 - 3.8 = 49.7, USB-A 53.5 - 3.65 = 49.85). The floor must also stay
// above the notch bottom (48.0), because below that the board is solid.
// All slots must satisfy slotFloorY(f) >= NOTCH_BOTTOM; layout.test.ts and
// mechanical/bracket_geometry.py both enforce it, so a future feedthrough that
// needs a deeper slot fails the checks instead of being silently clamped.
//
// This is still only the PANEL OPENING plus clearance. The rear body, flange
// and cable-bundle envelopes are unmeasured, and a panel opening does not prove
// them: that remains a named physical gate.
const bracketSlotFloorY = (f: { y: number; cutoutHeight: number }) =>
  mm3(f.y - f.cutoutHeight / 2 - BRACKET_ALLOWANCE.slotClearancePerSide)

const bracketSlots = USB_FEEDTHROUGHS.map(f => ({
  id: f.id,
  x: f.x,
  width: mm3(f.cutoutWidth + 2 * BRACKET_ALLOWANCE.slotClearancePerSide), // 15.6 / 17.9 / 17.9
  yBottom: bracketSlotFloorY(f), // 48.20 / 48.35 / 48.35
  yTop: bracketOutline.y2, // open to the top edge, so a cable can be laid in
}))
const bracketSlotEdges = bracketSlots.map(s => ({ left: s.x - s.width / 2, right: s.x + s.width / 2 }))

// The top front/rear spacer hardware (`FRONT_REAR_MOUNT`) sits in the same
// 3 mm layer behind the front board, so the bracket must clear it. The head
// envelope itself is unmeasured: a named physical gate.
const bracketHeadClearance = FRONT_REAR_MOUNT.filter(h => h.y > 0).map(h => ({
  x: h.x,
  y: h.y,
  diameter: BRACKET_ALLOWANCE.mountHeadClearance,
}))

// Cable ties sit in the webs between the slots (and between the end slots and
// the mount holes), never in a slot or in a mount ligament.
const bracketTieY = (bracketSlots[0]!.yBottom + bracketOutline.y2) / 2
const bracketTieHoles = [
  (BRACKET_MOUNT_X[0] + MOUNT_HOLE_DIAMETER / 2 + bracketSlotEdges[0]!.left) / 2,
  ...bracketSlotEdges.slice(1).map((e, i) => (bracketSlotEdges[i]!.right + e.left) / 2),
  (BRACKET_MOUNT_X[1] - MOUNT_HOLE_DIAMETER / 2 + bracketSlotEdges.at(-1)!.right) / 2,
].map(x => ({ x: mm3(x), y: mm3(bracketTieY), diameter: BRACKET_ALLOWANCE.tieHoleDiameter }))

export const FEEDTHROUGH_BRACKET = {
  mountX: BRACKET_MOUNT_X,
  mountY: BRACKET_MOUNT_Y,
  mountHoleDiameter: MOUNT_HOLE_DIAMETER,
  thickness: BRACKET_ALLOWANCE.thickness,
  outline: {
    ...bracketOutline,
    width: mm3(bracketOutline.x2 - bracketOutline.x1),   // 121.0
    height: mm3(bracketOutline.y2 - bracketOutline.y1),  // 14.5
  },
  slots: bracketSlots,
  mountHeadClearance: bracketHeadClearance,
  tieHoles: bracketTieHoles,
} as const

// ---- Shared mechanical export ----
// The single bridge to the CAD script. `bun run scripts/export-mechanical.ts`
// writes this object to mechanical/layout.json; layout.test.ts fails if that
// file is stale. Never read layout.ts from Python and never restate a
// coordinate in Python.
export const mechanicalExport = () => ({
  units: "mm" as const,
  note: "Generated from tscircuit/lib/layout.ts. Provisional planning data; not a verified clearance.",
  panel: PANEL,
  frontPcb: FRONT_PCB,
  rearPcb: REAR_PCB,
  display: { ...DISPLAY_CONCEPT, stack: DISPLAY_STACK },
  provisionalZ: PROVISIONAL_Z,
  displayUndersideGap: DISPLAY_UNDERSIDE_GAP,
  frontTopSmdMaxHeight: FRONT_TOP_SMD_MAX_HEIGHT,
  lightPipe: LIGHT_PIPE,
  lightPipeReach: LIGHT_PIPE_REACH,
  panelToFrontBoardGap: PANEL_TO_FRONT_BOARD_GAP,
  depthTarget: DEPTH_TARGET,
  provisionalDepth: PROVISIONAL_DEPTH,
  lcdCutout: { ...PANEL_LCD_CUTOUT, allowances: LCD_CUTOUT_ALLOWANCE },
  // The actual aperture polygon, so Python can re-measure the corner clearance
  // from the same geometry the panel emits.
  lcdCutoutOutline: roundedRectOutline(PANEL_LCD_CUTOUT),
  frontTopNotch: FRONT_TOP_NOTCH,
  notchBounds: NOTCH_BOUNDS,
  bracketAllowance: BRACKET_ALLOWANCE,
  bracket: FEEDTHROUGH_BRACKET,
  frontRearMount: FRONT_REAR_MOUNT,
  mountHoleDiameter: MOUNT_HOLE_DIAMETER,
  feedthroughs: USB_FEEDTHROUGHS,
})

// ---- Right-hand control column ----
// TEMPO, NAV and SHIFT/EXIT share one vertical centreline, right of the
// display and below the USB feedthroughs, with equal 19 mm spacing.
export const RIGHT_COLUMN = { x: 47, pitch: 19 } as const
const RIGHT_TEMPO_Y = 30
export const RIGHT_CONTROLS = {
  TEMPO: { x: RIGHT_COLUMN.x, y: RIGHT_TEMPO_Y },
  NAV: { x: RIGHT_COLUMN.x, y: RIGHT_TEMPO_Y - RIGHT_COLUMN.pitch },
  SHIFT_EXIT: { x: RIGHT_COLUMN.x, y: RIGHT_TEMPO_Y - 2 * RIGHT_COLUMN.pitch },
} as const

// PLAY START completes the transport row inside the right-hand control column,
// aligned beneath SHIFT/EXIT (no jack or LED in this column).
export const PLAY_START = { button: "PLAY START", x: RIGHT_COLUMN.x } as const

// ---- Transport / MIDI columns under the display ----
// Four equally spaced columns; each column shares its X between the transport
// button, its LED (REC / OVER DUB / PLAY only) and its MIDI jack. These are
// visual columns only; electrical assignments live in lib/pin-map.ts.
const TRANSPORT_LEFT_X = -53
export const TRANSPORT_COLUMN_PITCH = 24
export const TRANSPORT_Y = -34
export const LED_Y = TRANSPORT_Y + 10 // LEDs sit 10 mm above their buttons:
// button r6.2 + LED r2 needs >8.2 mm pitch for a routable FR4 web (1.8 mm)
export const JACK_Y = -47.5
const TRANSPORT_BUTTON_NAMES = ["REC", "OVER DUB", "STOP", "PLAY"] as const
const MIDI_JACK_NAMES = ["MIDI IN 1", "MIDI IN 2", "MIDI OUT 1", "MIDI OUT 2"] as const
export type TransportColumn = { button: string; jack: string; x: number }
export const FRONT_COLUMNS: readonly TransportColumn[] = TRANSPORT_BUTTON_NAMES.map((button, i) => ({
  button,
  jack: MIDI_JACK_NAMES[i] ?? `MIDI COLUMN ${i + 1}`,
  x: TRANSPORT_LEFT_X + i * TRANSPORT_COLUMN_PITCH,
}))
// LEDs above REC / OVER DUB / PLAY; ids shared with the concept artwork.
export const LED_ID = { REC: "REC LED", "OVER DUB": "OVERDUB LED", PLAY: "PLAY LED" } as const

export const transportButtonX = (button: string): number => {
  if (button === PLAY_START.button) return PLAY_START.x
  const col = FRONT_COLUMNS.find(c => c.button === button)
  if (!col) throw new Error(`unknown transport button ${button}`)
  return col.x
}
export const midiJackX = (jack: string): number => {
  const col = FRONT_COLUMNS.find(c => c.jack === jack)
  if (!col) throw new Error(`unknown MIDI jack ${jack}`)
  return col.x
}

// Front-board 2x13 interconnect (J5) to the rear-board J_FRONT. It sits in the
// corridor between the display and the right-hand control column: the header's
// ~41 mm courtyard cannot clear the column at the previous right-edge spot
// (x = 58) once the column moved onto x = 47 (courtyard overlap with TEMPO,
// SHIFT/EXIT and PLAY START). IDC plug height between the boards is unverified.
export const FRONT_INTERCONNECT = { x: 34, y: 10, rotation: 90 } as const

// ---- Concept points for the generated preview artwork ----
// Point positions only. Preview symbols do not represent physical part sizes.
export type FrontConceptKind = "cable" | "control" | "button" | "led" | "jack"
export type FrontConceptPoint = { id: string; x: number; y: number; kind: FrontConceptKind }
export const FRONT_CONCEPT: readonly FrontConceptPoint[] = [
  ...USB_FEEDTHROUGHS.map(f => ({ id: f.id, x: f.x, y: f.y, kind: "cable" as const })),
  { id: "TEMPO", ...RIGHT_CONTROLS.TEMPO, kind: "control" },
  { id: "NAV", ...RIGHT_CONTROLS.NAV, kind: "control" },
  { id: "SHIFT / EXIT", ...RIGHT_CONTROLS.SHIFT_EXIT, kind: "button" },
  ...FRONT_COLUMNS.map(c => ({ id: c.button, x: c.x, y: TRANSPORT_Y, kind: "button" as const })),
  { id: PLAY_START.button, x: PLAY_START.x, y: TRANSPORT_Y, kind: "button" },
  ...FRONT_COLUMNS.filter(c => c.button in LED_ID).map(c => ({
    id: LED_ID[c.button as keyof typeof LED_ID], x: c.x, y: LED_Y, kind: "led" as const,
  })),
  ...FRONT_COLUMNS.map(c => ({ id: c.jack, x: c.x, y: JACK_Y, kind: "jack" as const })),
]

// ---- Panel openings (front FR-4 panel) ----
export const PANEL_CUTOUTS = {
  button: 12.4, // B3F-4055 transport button, circular opening
  led: 4.0,     // Bivar PLP2-400-C light pipe over the 0805 LED
  control: 7.2, // PEC11R shaft / SKRHABE010 nav actuator
  jack: 6.2,    // PJ-301M-3-B barrel
} as const

// Signal-style bold rings around the MIDI OUT barrels (inputs stay plain).
// Ring band 3.625…4.175: 0.5 mm web to the 6.2 mm barrel hole, 2.8 mm to the
// transport button cutouts above, 1.1 mm to the OUT labels below.
export const OUT_RING = { radius: 3.9, stroke: 0.55 } as const

// ---- White-silkscreen panel labels ----
// Short panel text for the full concept ids, plus per-control label offsets;
// labels derive from the shared control positions above.
const FEEDTHROUGH_LABEL: Record<string, string> = {
  "USB-C POWER": "USB-C POWER",
  "UMC AUDIO USB": "UMC AUDIO",
  "MIDI HUB USB": "MIDI HUB",
}
const JACK_LABEL: Record<string, string> = {
  "MIDI IN 1": "IN 1", "MIDI IN 2": "IN 2", "MIDI OUT 1": "OUT 1", "MIDI OUT 2": "OUT 2",
}
const LABEL_OFFSET = {
  feedthrough: -9.5, // below the USB cutouts
  transport: -8,     // below the button cutouts, above the jack cutouts
  jack: -6.5,        // below the jack cutouts
  above: 7,          // above the TEMPO / NAV cutouts
  below: -7.5,       // below the SHIFT/EXIT cutout
}

// tscircuit2024 stroke-font advance, measured from built Gerber output:
// the 10-glyph 5 mm title spans 34.45 mm, i.e. ~0.69 x fontSize per glyph.
// The release Gerber-bbox gate is authoritative; this ratio only sizes the
// layout-level bounds check below. Never use anchorAlignment center_left:
// the exporter lays text LEFTWARD from the anchor (the 2026-11 title at
// x = -51 ran to X -85.45, off the 132.08 mm panel). Use center only.
export const SILK_ADVANCE_RATIO = 0.69 as const
// Bottom-strip caption. The module wordmark is drawn paths now (panel-art);
// this text primitive is the jack-field caption, centred on the jack columns
// (-53…+19 -> centre -17) at 1.2 mm: about +-6.2 wide, 3+ mm clear of the
// IN 2 / OUT 1 labels either side, 2.8 mm below the jack barrels.
export const PANEL_TITLES = [
  { text: "MIDI TRS TYPE A", x: -17, y: -54, fontSize: 1.2 },
] as const
export const panelTitleExtents = (t: { text: string; x: number; y: number; fontSize: number }) => {
  const w = t.text.length * t.fontSize * SILK_ADVANCE_RATIO
  return { x1: t.x - w / 2, y1: t.y - t.fontSize / 2, x2: t.x + w / 2, y2: t.y + t.fontSize / 2 }
}
export type PanelLabel = { text: string; x: number; y: number }
export const PANEL_LABELS: readonly PanelLabel[] = [
  ...USB_FEEDTHROUGHS.map(f => ({ text: FEEDTHROUGH_LABEL[f.id] ?? f.id, x: f.x, y: f.y + LABEL_OFFSET.feedthrough })),
  { text: "TEMPO", x: RIGHT_CONTROLS.TEMPO.x, y: RIGHT_CONTROLS.TEMPO.y + LABEL_OFFSET.above },
  { text: "NAV", x: RIGHT_CONTROLS.NAV.x, y: RIGHT_CONTROLS.NAV.y + LABEL_OFFSET.above },
  { text: "SHIFT / EXIT", x: RIGHT_CONTROLS.SHIFT_EXIT.x, y: RIGHT_CONTROLS.SHIFT_EXIT.y + LABEL_OFFSET.below },
  ...FRONT_COLUMNS.map(c => ({ text: c.button, x: c.x, y: TRANSPORT_Y + LABEL_OFFSET.transport })),
  { text: PLAY_START.button, x: PLAY_START.x, y: TRANSPORT_Y + LABEL_OFFSET.transport },
  ...FRONT_COLUMNS.map(c => ({ text: JACK_LABEL[c.jack] ?? c.jack, x: c.x, y: JACK_Y + LABEL_OFFSET.jack })),
]
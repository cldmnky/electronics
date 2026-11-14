// Millimetres, front view: +X right, +Y up. Illustrative planning only.
// Nothing here is an approved cutout, footprint or complete component envelope.
import { INTERCONNECT_MECH } from "./interconnect"
//
// SINGLE SOURCE OF LAYOUT COORDINATES. panel.circuit.tsx (panel openings and
// silkscreen labels), front.circuit.tsx (component placement and display
// annotations) and scripts/panel-preview.ts (concept artwork) import every
// position from this file; none of them may re-state a placement literal.
// All positions stay tentative pending the physical checks listed in
// docs/mechanical-stack.md.

// 30HP x 3U front panel, 152.0 mm ACTUAL width. Owner decision 2026-11-08: the
// 32HP / 162.56 mm panel of 2026-11-07 is REPLACED and superseded.
//
// 30HP nominal is 30 x 5.08 = 152.4 mm. 152.0 mm is an ENGINEERING CHOICE that
// leaves a 0.4 mm total nominal allowance (0.2 mm per side) against that
// nominal width. It is NOT an owner-specified exact value, NOT a manufacturer
// mandate and NOT a guaranteed fit.
//
// Primary sources: Doepfer A-100 mechanical information,
// https://doepfer.de/a100_man/a100m_e.htm and its hole figure,
// https://doepfer.de/a100_man/A100FP.gif. They state that actual panel widths
// are a few tenths of a millimetre smaller than the nominal HP width, that the
// rail-hole spacing must be a multiple of 5.08 mm, and the figure dimensions
// 128.5 mm height, 7.5 mm horizontal and 3.0 mm vertical hole inset, Ø3.2 mm
// holes. That page publishes NO 30HP width. 152.0 mm is therefore OUR ESTIMATE
// (0.4 mm under the 152.4 mm nominal), not a Doepfer-stated 30HP value, and it
// is not a guaranteed fit.
export const PANEL = { width: 152.0, height: 128.5, thickness: 1.6 } as const // 30HP x 3U, 152.0 actual
// Preliminary board bounding boxes only. Positions/Z, cutouts, spacers and the
// actual stack remain unresolved; see docs/mechanical-stack.md.
//
// The front board was 128 mm (owner decision D2 2026-11-07) and grows to
// 150 mm with the 2026-11-13 respin: the display leaves the board for a
// 106.5 x 68.9 mm opening, and the MIDI circuit has to live in the margins
// beside it. 150 mm leaves 1 mm per side inside the 152.0 mm panel. It carries the
// 106 mm-wide module only because the value-knob column moved 47 -> 52 mm: the
// module then spans -62.5..+43.5 (1.5 mm ligament to the board edge) and the
// knob body (Ø12.5 at x = 52, left edge 45.75) keeps 2.25 mm clear of it. The
// previous 136 mm proposal is withdrawn: growing the board forced 8HP of extra
// panel for no mechanical gain. Width only; the <=110 mm installed-depth rule
// is Z and is untouched.
export const FRONT_PCB = { width: 150, height: 112, thickness: 1.6 } as const
// Rear/base PCB, mounted behind the front board. Owner decision 2026-11-13:
// 148 mm wide (2 mm per side inside the 152.0 mm panel), replacing the 20HP
// (101.6 mm) board of 2026-11-07. Reason: the front board carries a display
// opening, so the only board material left for the direct 2 x 13 interconnect
// is the side margins, and those lie outside a 101.6 mm rear board. The extra
// width also puts all four display-carrier bolt points on the board. The panel
// itself is unchanged; the Pi mounting panel stays 101.2 mm.
export const REAR_PCB = { width: 148, height: 112, thickness: 1.6 } as const

// OWNER-DIRECTED, PROVISIONAL: the panel's FRONT/outer face sits 10.6 mm above
// the front board's top face (panel rear 9.0 mm). Basis: the ordered Thonkiconn
// WQP419GR jack seats the panel with its body shoulder - the 9.0 mm body length
// along the jack axis plus the 1.6 mm panel gives 10.6 - and the owner accepted
// 10.6 as the number. It is declared here, above DISPLAY_STANDOFF, because that
// block's spacer arithmetic is measured from the rear board, which this value
// moves. The long rationale is beside PROVISIONAL_Z below.
// NOT an accepted physical fit: the jack's actual seating, its thread runout and
// pitch, and the nut/washer stack are unmeasured (docs/mechanical-stack.md).
export const PANEL_TO_FRONT_BOARD_TOP = 10.6 as const

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
  { x: -61, y: -53 }, { x: 61, y: -53 }, { x: -61, y: 53 }, { x: 57, y: 41 },
] as const

// Front-to-rear-board spacer grid (M2.5, spacer length = the direct mating
// pair's 11.20 mm nominal seat-to-seat dimension, see PROVISIONAL_DEPTH and
// INTERCONNECT_MECH; it is a drawing-derived NOMINAL, not a measured gap).
// Shared by both boards; the panel does NOT
// carry these (internal stack only). Top pair sits between the display top
// (45.0) and the relief-notch bottom (48.0) while clearing the U6 courtyard
// (top 38.4) and C5; bottom pair sits below the jack courtyards (bottom
// −54.9) with 2.65 mm edge ligament. The top pair was moved ±44 → ±45 when the
// display mounting switched to the SMTSO boss pattern: the new display hole
// at (+40.15, +41.90) kept only 0.50 mm of FR-4 web to a (44, 43) spacer hole
// (centre distance 4.00 − half-diameters 3.50); at (±45, 43) the web is 1.47
// mm. ±46 was tried first and REJECTED: it shifted the rear-board autorouter
// to a via at (50.46, 6.56) violating the 0.20 mm copper-to-edge rule
// (0.118 mm), a build failure; ±45 clears the router and keeps every web
// (rear ligament 4.45 mm, C5 centre distance 5.83 mm, J_PWR pads 4.3 mm, the
// same class as at ±44). The bottom pair stays at ±44: the bottom display
// hole (+40.15, −19.90) is 32 mm above it and nothing else asked for the
// move. See docs/mechanical-stack.md for the residual: top positions need
// bracket clearance holes at regen time.
// 2026-11-13 (owner): the front-to-rear spacers are back, at positions chosen
// for the board the front one is NOW - a U with two 11.75/31.75 mm legs and a
// bottom bar that is full of jacks, clusters and buttons. Both boards must have
// material at every position:
//   x -69.0  front left leg (-75...-63.25), rear board (-74...74)  -> 1.35 mm web
//   x  71.5  front right leg (43.25...75), rear board edge at 74   -> 1.15 mm web
//   y  46.0  above J5/J_FRONT's courtyard (which ends at 40.51) and clear of the
//            relief cutout's top edge; bottom at -35.0 sits between the MIDI
//            clusters' band (-40.8) and the leg, both boards solid there.
// The old grid (±44/±45, -52/43) is dead: on the U board (-44,-52) lands on the
// jack row and (45,43) on the right leg's cut edge.
export const FRONT_REAR_MOUNT = [
  { x: -69, y: -35 }, { x: 71.5, y: -35 }, { x: -69, y: 46 }, { x: 71.5, y: 46 },
] as const

// Eurorack rail holes in the panel (M3 rack screws), Y inset 3.0 mm from the
// panel edge (unchanged; the Doepfer figure A100FP.gif dimensions 3.0 mm and
// Ø3.2 mm holes, matching these). The X spacing MUST be a multiple of 5.08 mm
// (Doepfer A-100 rule, https://doepfer.de/a100_man/a100m_e.htm): 137.16 =
// 27 x 5.08, so the holes sit at +/-68.58. On our 152.0 mm panel that is a
// 7.42 mm centre inset, and the 3.2 mm hole edge clears the panel edge by
// 5.82 mm. The figure draws a 7.5 mm edge inset, so this fixed-grid choice is
// 0.08 mm tighter than the figure: DISCLOSED, not adjusted, and neither value
// is a fitted measurement.
//
// The replaced 32HP panel used +/-73.78, i.e. a spacing of 147.56 mm =
// 29 x 5.08 + 0.24: 0.24 mm off the 5.08 mm grid. That is recorded as an
// off-grid value. It is NOT a claim that the old holes necessarily fail the
// sliding nuts.
//
// The rail-hole edge (-66.98) and the LCD window LEFT edge (-63.11) differ by
// 3.87 mm in X ONLY. Those two features sit at different Y, so that number is
// an x projection: it is NOT a web and NOT a shortest clearance. The real 2D
// distance is measured from the emitted aperture polygon in layout.test.ts,
// together with the standoff-hole to rail-hole web.
export const RAIL_HOLES = [
  { x: -68.58, y: -61.25 }, { x: 68.58, y: -61.25 },
  { x: -68.58, y: 61.25 }, { x: 68.58, y: 61.25 },
] as const

// ---- Raspberry Pi 5 board geometry (the Pi lives on the 20HP panel only) ----
// The Pi no longer sits on the rear board (owner decision 2026-11-12). This
// block is the single source for the Pi's board, hole and connector geometry;
// the Pi panel (PI_PANEL_*) and the tests that guard it read from here.
//
// SOURCE: Raspberry Pi 5 mechanical drawing RP-008347-DS-1, page 1 (sha256
// 5dd680d6c1f5e7aa9c7b020695e315d04aac862df82ff01d4e9041cd0668d7f2, retrieved
// 2026-11-12). The drawing says its dimensions are approximate and not
// production data. The values were read from the drawing's vector data: the
// dimension extension lines give the board edges (x 0 and 85, y 0 and 56), the
// tick marks give the hole centres (x 3.5 and 61.5, y 3.5 and 52.5), the GPIO
// header body is centred on the hole pattern (x 32.5), and the outline minus the
// board gives the connector protrusions. Expect about +/-0.1 mm.
//
// Board frame: origin at the left-bottom corner seen from the COMPONENT side.
// X runs along the 85 mm edge, Y along the 56 mm edge, and the GPIO header is on
// the top (Y = 56) edge.
export const PI_BOARD = { length: 85, width: 56, thickness: 1.6 } as const
/** M2.5 clearance hole (drawing dia 2.7). */
export const PI_HOLE_DIAMETER = 2.7 as const
/** Hole centres sit this far from the left and bottom edges. */
export const PI_HOLE_INSET = 3.5 as const
export const PI_HOLE_PITCH = { x: 58, y: 49 } as const
/**
 * Connectors that stand beyond the board outline. Left 1.7 (the drawing's left
 * edge, microSD side), right 3.3 (USB-A x2 and RJ45), bottom 1.7 (USB-C and
 * micro-HDMI). Top 0.
 */
export const PI_PROTRUSION = { left: 1.7, right: 3.3, bottom: 1.7, top: 0 } as const
/** The GPIO header and the hole pattern share the centre line x = 32.5 mm. */
export const PI_HEADER_CENTRE_X = PI_HOLE_INSET + PI_HOLE_PITCH.x / 2

// Mounting: the Pi hangs behind the panel with its COMPONENT SIDE AWAY from the
// panel, so the panel looks at the board from underneath and its X axis is
// mirrored. The board is centred on the panel origin.
//   panel X = PI_BOARD.length / 2 - board x   (the USB-A/RJ45 edge goes to -X)
//   panel Y = board y - PI_BOARD.width / 2    (the GPIO header goes to +Y)
export const piBoardToPanel = (bx: number, by: number) => ({
  x: PI_BOARD.length / 2 - bx,
  y: by - PI_BOARD.width / 2,
})

// ---- 20HP Pi mounting panel (owner decision 2026-11-12) ----
// The Pi 5 (with Active Cooler + M.2 HAT+ + SSD) no longer sits in the rack
// module: it bolts to the REAR face of its own pure-mechanical 20HP x 3U FR-4
// panel beside the main module. That is what keeps the main module shallow
// enough for the Behringer Eurorack Go rows (docs/mechanical-stack.md). The
// panel carries no copper and no components; JLCPCB fabricates it like the
// 30HP panel (same no-mask white-silk bare board). USB stays as-is on the 30HP
// panel - its three feedthroughs do not move - and the harness between the two
// modules is the three USB feedthrough cables, the display DSI FPC, the
// 40-wire GPIO ribbon to J_PI and the J_PWR power-button pigtail. Those are
// planning runs; no length is proven (docs/TODO.md).
//
// Width follows the PANEL convention (30HP): actual = N x 5.08 - 0.4, i.e.
// 101.2 mm for 20HP. The rail holes follow the RAIL_HOLES Doepfer rule: the X
// spacing is a multiple of 5.08 at the same 7.42 mm centre inset, so 86.36 =
// 17 x 5.08 at +/-43.18. Y is the Doepfer 3U position, +/-61.25.
export const PI_PANEL = { hp: 20, width: 101.2, height: 128.5, thickness: 1.6 } as const
export const PI_PANEL_RAIL_HOLES = [
  { x: -43.18, y: -61.25 }, { x: 43.18, y: -61.25 },
  { x: -43.18, y: 61.25 }, { x: 43.18, y: 61.25 },
] as const

// Pi 5 on the mounting panel: LANDSCAPE, the 85 mm edge across the panel. The
// 40-pin GPIO header runs along the panel's TOP edge (the ribbon toward J_PI
// leaves upward), USB-C and micro-HDMI face the BOTTOM edge, USB-A x2 and RJ45
// face the LEFT edge (panel -X), and the drawing's left edge (microSD side)
// faces the RIGHT edge. Those plugs insert in the panel plane. The M2.5 holes
// are the drawing's 58 x 49 pattern mapped through piBoardToPanel, so the
// pattern centre sits 10 mm toward +X, where the header is centred on it.
export const PI_PANEL_PI_MOUNT = {
  x: piBoardToPanel(PI_HEADER_CENTRE_X, 0).x,
  y: 0,
  pitchX: PI_HOLE_PITCH.x,
  pitchY: PI_HOLE_PITCH.y,
  holeDiameter: PI_HOLE_DIAMETER,
} as const
export const PI_PANEL_PI_HOLES = [
  piBoardToPanel(PI_HOLE_INSET, PI_HOLE_INSET),
  piBoardToPanel(PI_HOLE_INSET, PI_HOLE_INSET + PI_HOLE_PITCH.y),
  piBoardToPanel(PI_HOLE_INSET + PI_HOLE_PITCH.x, PI_HOLE_INSET),
  piBoardToPanel(PI_HOLE_INSET + PI_HOLE_PITCH.x, PI_HOLE_INSET + PI_HOLE_PITCH.y),
]
// The board outline in the panel frame (the mirror swaps the two X limits).
export const PI_PANEL_PI_BOX = {
  x1: piBoardToPanel(PI_BOARD.length, 0).x,
  x2: piBoardToPanel(0, 0).x,
  y1: piBoardToPanel(0, 0).y,
  y2: piBoardToPanel(0, PI_BOARD.width).y,
} as const
// The same outline including the connectors that stand beyond it.
export const PI_PANEL_PI_ENVELOPE = {
  x1: piBoardToPanel(PI_BOARD.length + PI_PROTRUSION.right, 0).x,
  x2: piBoardToPanel(-PI_PROTRUSION.left, 0).x,
  y1: piBoardToPanel(0, -PI_PROTRUSION.bottom).y,
  y2: piBoardToPanel(0, PI_BOARD.width + PI_PROTRUSION.top).y,
} as const

// ---- 4.3-inch DSI LCD, Waveshare "4.3inch DSI LCD" (ElectroKit art 4103 5687)
// Replaces the unsourced 3.5-inch module (SKU 33087). SAME 800 x 480 resolution,
// so the UI code is untouched. SOURCE OF RECORD (owner-supplied, verified
// against the vendor copy before use): assets/display-4.3-drawing.png, the
// Waveshare "Exterior-Size" dimension drawing, Rev2.2, rear view, unit mm,
// https://www.waveshare.com/img/devkit/LCD/4.3DP/Exterior-Size.jpg. The wiki
// page (https://www.waveshare.com/wiki/4.3inch_DSI_LCD) publishes the SAME
// drawing: a per-pixel comparison of the two images differs by 0.07/255 mean
// and 0.00 % of pixels by more than 40/255, so they are one drawing.
//
// What the drawing dimensions, and what it does NOT:
//   outline                106.00 x 68.00          (PCB, drawn)
//   corner pattern insets  25.62 left / 22.38 right, 9.50 top / 9.46 bottom
//                          -> 58.00 x 49.04 hole centres (drawn)
//   outer pattern          99.30 x 61.80 centres, i.e. 3.35 / 3.10 edge insets
//                          (drawn, no inset dimension: 106-99.30)/2 = 3.35,
//                          (68-61.80)/2 = 3.10
//   overall thickness      14.05                  (wiki FAQ answer)
//   hole diameter          NOT dimensioned on the drawing. Taken from the wiki
//                          3D model that ships with the same page
//                          (4.3inch_DSI_LCD_Drawing.zip -> 4_3inch-dsi-lcd-V1.stp):
//                          eight CIRCLE entities of radius 2.15 -> Ø4.30 at BOTH
//                          patterns, through the 1.6 mm PCB. Cross-check on the
//                          drawing image: 4.36 mm of bright bore across the
//                          corner-pattern hole at 7.11 px/mm. Kit screws are
//                          checked against this at first article.
//   glass outline          105.42 x 67.07, read from the STEP model (the front
//                          cover-glass plate: faces 1123/814, a 16-point loop at
//                          z = 2.900 / 0.000 in the glass frame). The front
//                          frame around it is 106.10 x 67.80 (faces 3725/3562),
//                          so nothing on the module beats the 106 x 68 PCB
//                          outline. mechanical/tools/measure_display_step.py
//                          reproduces both numbers from the reference STEP.
//                          The PANEL WINDOW is sized from the GLASS (owner
//                          decision D1), not from the PCB envelope.
//
// WHICH PATTERN IS THREADED (reconciled against the reference STEP, 2026-11-08).
// The drawing shows two 4-hole patterns. The STEP file carries
// `SMTSO-M2_5-4ET` - an M2.5 FEMALE-THREADED SMT standoff - at the 3.35 / 3.10
// corner pattern, while the 25.62 / 22.38 / 9.50 / 9.46 inner pattern is a
// plain plated through hole with nothing to thread into; and the module kit's
// screws/pillars are MatroM2.5 pieces. The FITTED pattern is therefore the
// OUTER boss pattern: DISPLAY_BOSS_PATTERN. This is the only choice whose
// spacer arithmetic holds: planeSeparation 9.0 = the 4.0 mm boss projection
// (DISPLAY_VENDOR.pcbRearToBossEnd, measured off those same SMT standoffs)
// + the 5.0 mm spacer. Mounting through the inner plain Ø4.3 holes would need
// hardware the module does not provide and would change the spacer to the full
// 9.0 mm. DISPLAY_STANDOFFS keeps the inner pattern as recorded alternative
// data (and the drawing's own inset figure) with the first article still
// physically confirming the threads; flipping back is a one-line swap in
// LCD_MOUNT_HOLES.
export const DISPLAY_DATUM = {
  sourceDrawing: "assets/display-4.3-drawing.png (Waveshare Exterior-Size, Rev2.2)",
  sourceDrawingUrl: "https://www.waveshare.com/img/devkit/LCD/4.3DP/Exterior-Size.jpg",
  sourceWiki: "https://www.waveshare.com/wiki/4.3inch_DSI_LCD",
  source3d: "4.3inch_DSI_LCD_Drawing.zip -> 4_3inch-dsi-lcd-V1.stp",
  drawingRev: "Rev2.2",
  firstArticlePending: true,
} as const

export const DISPLAY_VENDOR = {
  // Maximum cross-section of the module: nothing on it is larger than the PCB.
  outline: { width: 106.0, height: 68.0, thickness: 1.6 },
  // 800 x 0.1188 mm and 480 x 0.1122 mm: 95.04 x 53.856 mm.
  active: { width: 95.04, height: 53.856, tolerance: 0.2 },
  // The glass's printed bezel opening, read off the STEP model: a plane of
  // 95.54 x 54.36 mm, i.e. the active area grown by 0.25 mm per side. Carried
  // for the silkscreen and the touch-alignment note only; the panel aperture
  // is sized from `glass` (owner decision D1).
  bezel: { width: 95.54, height: 54.36 },
  // Cover-glass plate, front-face outline: the envelope that has to pass the
  // panel aperture. 105.42 x 67.07 (STEP faces 1123 / 814).
  glass: { width: 105.42, height: 67.07 },
  holeDiameter: 4.3, // STEP circles r2.15; NOT dimensioned on the drawing
  overallDepth: 14.05, // wiki FAQ: overall thickness
  // PROVISIONAL split of the 14.05 mm: glass front -> PCB rear 10.05, then the
  // 4.00 mm boss/standoff projection behind the PCB (same split as the withdrawn
  // 3.5-inch module). Neither number is measured on the new part; the first
  // article records both. Kept as two named quantities so they cannot be
  // conflated again.
  glassFrontToPcbRear: 10.05,
  pcbRearToBossEnd: 4.0,
  bossDiameter: 6.2, // SMT standoff body read off the drawing corner (~6.2 mm)
} as const

// ALTERNATIVE inner pattern (plain plated holes, no thread): hole centres
// 25.62 mm from the LEFT edge, 22.38 mm from the RIGHT edge, 9.50 mm from the
// TOP and 9.46 mm from the BOTTOM. The pattern is asymmetric left-right and
// top-bottom, so it is NOT mirrored: in the layout frame (module centred on
// the origin) the offsets are derived from the edge insets, never by negating
// a pitch. Kept 1) as the mounting-pattern fallback if the first article
// shows the SMT bosses are not threaded and 2) because these centres match
// exactly what the drawing dimensions.
export const DISPLAY_STANDOFFS = [
  { x: mm3(25.62 - DISPLAY_VENDOR.outline.width / 2), y: mm3(DISPLAY_VENDOR.outline.height / 2 - 9.5) },
  { x: mm3(DISPLAY_VENDOR.outline.width - 22.38 - DISPLAY_VENDOR.outline.width / 2), y: mm3(DISPLAY_VENDOR.outline.height / 2 - 9.5) },
  { x: mm3(25.62 - DISPLAY_VENDOR.outline.width / 2), y: mm3(DISPLAY_VENDOR.outline.height / 2 - 58.54) },
  { x: mm3(DISPLAY_VENDOR.outline.width - 22.38 - DISPLAY_VENDOR.outline.width / 2), y: mm3(DISPLAY_VENDOR.outline.height / 2 - 58.54) },
] as const // x -27.38 / +30.62, y +24.50 / -24.54

// FITTED outer 4-hole pattern (99.30 x 61.80 centres, 3.35 / 3.10 edge
// insets). The corner features that carry the module's own SMT standoffs sit
// here; the reference STEP names them `SMTSO-M2_5-4ET`, i.e. these are the
// ONLY M2.5 threads the module offers, so our spacers screw into them and the
// front board carries its holes at these centres (LCD_MOUNT_HOLES).
export const DISPLAY_BOSS_PATTERN = [
  { x: mm3(3.35 - DISPLAY_VENDOR.outline.width / 2), y: mm3(DISPLAY_VENDOR.outline.height / 2 - 3.1) },
  { x: mm3(DISPLAY_VENDOR.outline.width - 3.35 - DISPLAY_VENDOR.outline.width / 2), y: mm3(DISPLAY_VENDOR.outline.height / 2 - 3.1) },
  { x: mm3(3.35 - DISPLAY_VENDOR.outline.width / 2), y: mm3(DISPLAY_VENDOR.outline.height / 2 - 64.9) },
  { x: mm3(DISPLAY_VENDOR.outline.width - 3.35 - DISPLAY_VENDOR.outline.width / 2), y: mm3(DISPLAY_VENDOR.outline.height / 2 - 64.9) },
] as const // x -49.65 / +49.65, y +30.90 / -30.90

// Centre (-9.5, 11). The horizontal position is fixed by owner decision D2:
//   module left edge -62.5 vs FRONT_PCB left edge -64   -> 1.5 mm ligament
//   module right edge +43.5 vs knob body left 45.75     -> 2.25 mm clear
//   window left edge -63.11 vs M3 rail-hole edge -66.98 -> 3.87 mm in X ONLY,
//   at a different Y, so not a web; the real 2D distance is in layout.test.ts
//   window left edge to panel edge -76.0                -> 12.89 mm (30HP panel)
// width/height are the MODULE OUTLINE (the physical envelope behind the panel).
// innerWidth/innerHeight are the ACTIVE area; bezelWidth/bezelHeight are the
// printed border; glassWidth/glassHeight are what the panel aperture follows.
export const DISPLAY_CONCEPT = {
  // 2026-11-13: -9.5 -> -10.0. With the display opening cut, the encoder and NAV
  // pad fields (column at x = 52) reach x = 43.59, which left only 0.16 mm to the
  // opening edge at 43.75; the cutout rule needs 0.20. A 0.5 mm shift keeps the
  // module's 2.25 mm clearance to the knob body and gives the pads 0.66 mm.
  x: -10.0,
  y: 11,
  width: DISPLAY_VENDOR.outline.width,
  height: DISPLAY_VENDOR.outline.height,
  innerWidth: DISPLAY_VENDOR.active.width,
  innerHeight: DISPLAY_VENDOR.active.height,
  bezelWidth: DISPLAY_VENDOR.bezel.width,
  bezelHeight: DISPLAY_VENDOR.bezel.height,
  glassWidth: DISPLAY_VENDOR.glass.width,
  glassHeight: DISPLAY_VENDOR.glass.height,
  pcb: { width: DISPLAY_VENDOR.outline.width, height: DISPLAY_VENDOR.outline.height },
} as const

// PROVISIONAL display interface, unchanged in structure and re-stated for the
// new module: the spacer screws onto the module's mounting holes and sets the
// plane separation from the front-board top face to the display PCB REAR face.
// planeSeparation = 9.0 stays the provisional design target. spacerLength is
// derived, never re-typed: 9.0 - 4.0 (boss projection) = 5.0 mm.
// 2026-11-13: the module no longer sits on the front board. It bolts to a
// printed carrier fixed to the REAR board's front face, so both distances are
// measured from that face.
// The carrier spacer is now DERIVED from the owner-directed panel-to-board
// distance, not typed: the rear board's front face is PANEL_TO_FRONT_BOARD_TOP
// + the front board + the 11.20 mm interconnect seat-to-seat, and the display
// PCB's rear face comes from the glass-flush chain (0 + 10.05). Their difference
// is planeSeparation; minus the module's 4.00 mm boss projection it is the
// nominal carrier spacer that trims the glass plane to the panel front. At the
// 10.6 mm panel that is 13.35 and 9.35 mm (it was 12.75 and 8.75 at 10.0).
// NOTHING IS DRAWN FOR THE CARRIER YET: there is no carrier STL, its two
// outboard boss points (x -59.15) lie outside the rear board, and the spacer is
// a nominal length, not a fit (acceptance item `display_dsi`, `first_article`).
export const REAR_BOARD_FRONT_Z = mm3(PANEL_TO_FRONT_BOARD_TOP + FRONT_PCB.thickness + INTERCONNECT_MECH.seatToSeat) // 23.40
export const DISPLAY_PCB_REAR_Z = mm3(0 + DISPLAY_VENDOR.glassFrontToPcbRear) // 10.05
export const DISPLAY_STANDOFF = {
  planeSeparation: mm3(REAR_BOARD_FRONT_Z - DISPLAY_PCB_REAR_Z),                     // 13.35
  spacerLength: mm3(REAR_BOARD_FRONT_Z - DISPLAY_PCB_REAR_Z
    - DISPLAY_VENDOR.pcbRearToBossEnd),                                              // 9.35
} as const

export const DISPLAY_STACK = {
  outlineWidth: DISPLAY_VENDOR.outline.width,
  outlineHeight: DISPLAY_VENDOR.outline.height,
  activeWidth: DISPLAY_VENDOR.active.width,
  activeHeight: DISPLAY_VENDOR.active.height,
  bezelWidth: DISPLAY_VENDOR.bezel.width,
  bezelHeight: DISPLAY_VENDOR.bezel.height,
  pcbWidth: DISPLAY_VENDOR.outline.width,
  pcbHeight: DISPLAY_VENDOR.outline.height,
  glassWidth: DISPLAY_VENDOR.glass.width,
  glassHeight: DISPLAY_VENDOR.glass.height,
  overallDepth: DISPLAY_VENDOR.overallDepth,
  glassFrontToPcbRear: DISPLAY_VENDOR.glassFrontToPcbRear,
  bossProjection: DISPLAY_VENDOR.pcbRearToBossEnd,
  planeSeparation: DISPLAY_STANDOFF.planeSeparation,
  spacerLength: DISPLAY_STANDOFF.spacerLength,
  holeDiameter: DISPLAY_VENDOR.holeDiameter,
} as const
// Display depth behind the display PCB, to the rear-most point: the boss ends.
export const DISPLAY_BEHIND_PCB = DISPLAY_VENDOR.pcbRearToBossEnd // 4.0

// Front-board LCD standoff holes derive from the display centre plus the
// mounted display-local offsets above (the SMTSO boss pattern, see
// DISPLAY_BOSS_PATTERN). Do not re-state their positions.
export const LCD_MOUNT_HOLES = DISPLAY_BOSS_PATTERN.map(o => ({
  x: DISPLAY_CONCEPT.x + o.x,
  y: DISPLAY_CONCEPT.y + o.y,
}))

// The active area is centred on the module outline (the drawing's outer and
// inner hole patterns are the only asymmetry), so there is no active-area
// offset to carry. Kept as a named zero so nothing re-introduces the withdrawn
// 3.5-inch -2.905 mm sign.
export const DISPLAY_ACTIVE_OFFSET_X = 0 as const
const DISPLAY_WINDOW_CLEARANCE = 0.5 // silkscreen-only; illustrative
export const LCD_WINDOW = {
  x: DISPLAY_CONCEPT.x + DISPLAY_ACTIVE_OFFSET_X,
  y: DISPLAY_CONCEPT.y,
  width: mm3(DISPLAY_CONCEPT.innerWidth + 2 * DISPLAY_WINDOW_CLEARANCE),
  height: mm3(DISPLAY_CONCEPT.innerHeight + 2 * DISPLAY_WINDOW_CLEARANCE),
}

// Panel cutout for the LCD. Sized from the GLASS OUTLINE (105.42 x 67.07, the
// STEP cover-glass front face) plus LCD_CUTOUT_ALLOWANCE.perSide: the glass is
// what passes through the panel, so that is the envelope the aperture follows
// (owner decision D1). The 106 x 68 PCB envelope is not used - it is 0.29 /
// 0.47 mm larger per side and sits behind the panel anyway.
//
// The resulting window is 107.22 x 68.87, which does NOT fit the 65.10 mm band
// between the feedthrough cutout floor (y = +49.70) and the LED opening top
// (y = -22.00) that the earlier layout was built around. The authorised
// fallback (3) is therefore applied: the REC / OVER DUB / PLAY light pipes move
// from 10 mm ABOVE their buttons to 12 mm BESIDE them (LED_Y = TRANSPORT_Y).
// The free band becomes +49.70 -> transport-button opening top (-27.875) =
// 77.575 mm, and the window keeps 7.935 mm above (to the feedthrough labels,
// moved from y 44.0 to 47.5) and 2.865 mm below (to the button openings).
//
// Clearance per side, each term stated:
//   0.2 mm  JLCPCB profile/cutout tolerance (+-0.2)
//   0.2 mm  vendor glass tolerance (+-0.2; the whole tolerance is charged to
//           one side, the worst case for an off-centre glass)
//   0.5 mm  panel-to-module registration and assembly, NOT measured
//   = 0.9 mm per side. Planning value, not a fit approval.
//
// Corners: a routed inside corner cannot be sharp, so the aperture is emitted
// as a rounded rectangle with an explicit relief radius (unchanged rule).
export const LCD_CUTOUT_ALLOWANCE = { perSide: 0.9, cornerRadius: 1.0 } as const
export const PANEL_LCD_CUTOUT = {
  x: DISPLAY_CONCEPT.x,
  y: DISPLAY_CONCEPT.y,
  width: mm3(DISPLAY_CONCEPT.glassWidth + 2 * LCD_CUTOUT_ALLOWANCE.perSide),  // 107.22
  height: mm3(DISPLAY_CONCEPT.glassHeight + 2 * LCD_CUTOUT_ALLOWANCE.perSide), // 68.87
  cornerRadius: LCD_CUTOUT_ALLOWANCE.cornerRadius,
} as const

// 15-pin DSI FPC connector keep-out, on the module's REAR face (opposite the
// glass), at the rear-view drawing's right-hand long edge. This is the plug the
// DSI cable leaves from, so it is the one part of the module that is not simply
// a flat envelope the panel has to clear.
//
// Body: 21.412 x 4.826 mm. The STEP carries it as two extruded bodies (solids
// 106739 / 107488, 1.0 and 0.8 mm tall) and, unlike the PCB and glass, does not
// resolve their assembly placement for the text-only reader in
// mechanical/tools/measure_display_step.py - it reports every solid in its own
// frame, so the POSITION comes from the drawing at its 7.11 px/mm scale:
//   bar 244..398 px along the edge = 21.8 mm (STEP 21.412, agrees to 0.4 mm)
//   long-axis centre 33.3 mm from the top edge -> +0.7 mm from module centre
//   body 808..840 px = 4.5 mm deep (STEP 4.826), outer face 2.25 mm inboard
// Reading error on those three is about +-0.4 mm.
//
// Insets use the same no-mirror convention as DISPLAY_STANDOFFS (drawing
// insets are applied to the layout frame as drawn), so "right-hand edge" here
// is the drawing's, not the front view's left. The module edge is +53.0.
//
// It does not move the PCB this rev: the connector faces the front board's rear
// side and sits 20.5 mm inside the 128 mm board edge (module right edge +43.5
// against board edge +64.0), so the cable leaves over the front board rather
// than beside it. The identity, side and position of this connector are
// unverified (docs/mechanical-stack.md), so NO clearance from it is claimed:
// the DSI plug, its latch and the first bend are a first-article check against
// the front board's top-side parts, including the J5 female solder tails. The
// 2 x 13 link is a direct male/female pair and has no plug or ribbon.
export const DISPLAY_FPC_CONNECTOR = {
  pins: 15,
  face: "rear" as const,
  bodyLength: 21.412, // STEP extruded bodies; drawing bar 21.8
  bodyWidth: 4.826,   // STEP; drawing 4.5
  edgeInset: 2.25,    // outer body face, inboard of the module's right edge
  // module-local centre; +x is the drawing's right-hand edge side
  centerX: mm3(DISPLAY_VENDOR.outline.width - 2.25 - 4.826 / 2 - DISPLAY_VENDOR.outline.width / 2), // +48.337
  centerY: 0.7,       // 33.3 mm from the top edge (drawing)
} as const

// The same point in absolute panel/front-board coordinates: the module centre is
// at DISPLAY_CONCEPT, so this is where the DSI cable leaves the module.
export const DISPLAY_FPC_CONNECTOR_AT = {
  x: mm3(DISPLAY_CONCEPT.x + DISPLAY_FPC_CONNECTOR.centerX), // 38.837
  y: mm3(DISPLAY_CONCEPT.y + DISPLAY_FPC_CONNECTOR.centerY), // 11.7
} as const

// Rounded-rectangle outline in absolute panel coordinates, counter-clockwise.
// tscircuit has no rounded-rect cutout, so the panel emits this polygon. The
// straight chords lie INSIDE the true corner arc, i.e. toward the module, so
// the polygon is slightly SMALLER than the exact rounded rectangle: the
// approximation can only reduce the aperture, never enlarge it. The measured
// worst case with 6 segments per corner (scripts/check_placement.ts recomputes
// it from the built panel) stays above the 0.5 mm target. More segments narrow
// the gap further; fewer would eat into it.
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
  { text: "LCD 4.3in 106.00 x 68.00", dx: 0, dy: 26, fontSize: 1.2 },
  { text: "ACTIVE 95.04 x 53.86", dx: 0, dy: -30, fontSize: 0.9 },
  { text: "BEZEL 95.54 x 54.36", dx: 0, dy: -34, fontSize: 0.9 },
  { text: "MOUNT 99.30 x 61.80", dx: 0, dy: 21, fontSize: 0.9 },
  { text: "DSI 15P", dx: 20, dy: 13, fontSize: 1.0 },
] as const
export const displayAnnotationXY = (a: { dx: number; dy: number }) => ({
  x: DISPLAY_CONCEPT.x + a.dx,
  y: DISPLAY_CONCEPT.y + a.dy,
})

// ---- Front-board display opening ----
// At the 10.6 mm panel the module's body (glass front 0 -> rear-most 14.05 mm)
// crosses the front board's 10.6...12.2 mm plane, so no board material may sit
// under it. The opening is the vendor outline plus a uniform assembly
// allowance; the module's own X/Y position is set by the rear-board carrier and
// is unmeasured, so the allowance is a design margin, not a proven clearance.
export const DISPLAY_OPENING_CLEARANCE = { perSideX: 0.25, perSideY: 0.45 } as const
// The autorouter hugs the opening's edge at ~0.15 mm, under the 0.20 mm
// copper-to-cutout rule, so a keepout 1.0 mm larger than the opening pushes the
// router clear of it (0.3 mm was not enough); 0.5 keeps J_PWR's pads, which
// sit 0.75 mm above the opening, out of the keepout.
export const DISPLAY_OPENING_KEEPOUT_MARGIN = 0.5 as const
export const FRONT_DISPLAY_KEEPOUT = {
  x: DISPLAY_CONCEPT.x,
  y: DISPLAY_CONCEPT.y,
  width: mm3(DISPLAY_VENDOR.outline.width + 2 * (DISPLAY_OPENING_CLEARANCE.perSideX + DISPLAY_OPENING_KEEPOUT_MARGIN)),
  height: mm3(DISPLAY_VENDOR.outline.height + 2 * (DISPLAY_OPENING_CLEARANCE.perSideY + DISPLAY_OPENING_KEEPOUT_MARGIN)),
} as const

export const NOTCH_CHAMFER = 3.0
export const DISPLAY_OPENING_CHAMFER = 3.0

export const FRONT_DISPLAY_OPENING = {
  x: DISPLAY_CONCEPT.x,
  y: DISPLAY_CONCEPT.y,
  width: mm3(DISPLAY_VENDOR.outline.width + 2 * DISPLAY_OPENING_CLEARANCE.perSideX),   // 106.5
  height: mm3(DISPLAY_VENDOR.outline.height + 2 * DISPLAY_OPENING_CLEARANCE.perSideY), // 68.9
} as const

// Octagon: the rect with DISPLAY_OPENING_CHAMFER cut from each corner.
export const FRONT_DISPLAY_OPENING_POLYGON = (() => {
  const { x, y, width, height } = FRONT_DISPLAY_OPENING
  const c = DISPLAY_OPENING_CHAMFER
  const l = mm3(x - width / 2), r = mm3(x + width / 2)
  const b = mm3(y - height / 2), t = mm3(y + height / 2)
  return [
    { x: mm3(l + c), y: b }, { x: mm3(r - c), y: b }, { x: r, y: mm3(b + c) },
    { x: r, y: mm3(t - c) }, { x: mm3(r - c), y: t }, { x: mm3(l + c), y: t },
    { x: l, y: mm3(t - c) }, { x: l, y: mm3(b + c) },
  ] as const
})()

// 2026-11-13 (owner): the front board is a U. The 105 x 8 top relief notch and
// the display opening are now ONE cut: the opening runs from y -23.45 straight
// through the top edge at y 56, so the board keeps only two legs (11.75 mm left,
// 31.75 mm right) joined by the bottom bar. The USB feedthroughs (panel apertures
// at y 53.5, x -38 / -5 / 35) and their cable tails have more room this way than
// they had through the notch, which is why the notch is not needed at all.
// Everything that used to be checked against NOTCH_BOTTOM for clearance now has
// open air above it instead.
export const FRONT_BOARD_OPENING = {
  left: mm3(FRONT_DISPLAY_OPENING.x - FRONT_DISPLAY_OPENING.width / 2),   // -63.25
  right: mm3(FRONT_DISPLAY_OPENING.x + FRONT_DISPLAY_OPENING.width / 2),  //  43.25
  bottom: mm3(FRONT_DISPLAY_OPENING.y - FRONT_DISPLAY_OPENING.height / 2), // -23.45
  top: mm3(FRONT_PCB.height / 2 + 2),                                     //  58.00: past the edge
} as const

export const FRONT_BOARD_OPENING_POLYGON = (() => {
  const { left, right, bottom, top } = FRONT_BOARD_OPENING
  const c = DISPLAY_OPENING_CHAMFER
  return [
    { x: left, y: mm3(bottom + c) },
    { x: mm3(left + c), y: bottom },
    { x: mm3(right - c), y: bottom },
    { x: right, y: mm3(bottom + c) },
    { x: right, y: top },
    { x: left, y: top },
  ] as const
})()


// ---- PROVISIONAL Z stack ----
// Z datum: Z = 0 at the front (operator-side) face of the panel; +Z is
// rearward, into the rack cavity. Millimetres.
// NOT a verified clearance. The chain below rests on TWO explicit provisional
// design assumptions and one candidate part dimension. No value here is a
// measurement, and none of them certifies a gap.
//
// Assumption A is the 3.5-inch rule, restored for the 4.3-inch module now that
// the panel aperture follows the GLASS outline (PANEL_LCD_CUTOUT, 107.22 x
// 68.87): the glass passes THROUGH the aperture, so the glass front face can
// sit flush with the panel front face. The panel occludes nothing, so the
// earlier "glass behind the panel rear face" placement is withdrawn.
//
// Assumption B: the plane separation is DISPLAY_STANDOFF.planeSeparation
// (9.0 mm, front-board top face to display PCB rear face), which needs a 5.0 mm
// spacer because the module's mounting features already project 4.0 mm behind
// that plane.
// Part dimensions: the module is 14.05 mm overall (wiki FAQ); the 10.05 mm
// glass-front -> PCB-rear split is PROVISIONAL (14.05 - 4.00 boss projection)
// and the two numbers are kept separate so they cannot be conflated.
//
export const DISPLAY_GLASS_FRONT_Z = 0 as const // Assumption A, not a measurement

// Depths chain from the GLASS FRONT face, which now sits flush with the panel
// front face: 0.00 + 10.05 = 10.05 display PCB rear, + 4.00 = 14.05 rear-most.
// OWNER-DIRECTED, PROVISIONAL (2026-11-14): the panel front sits 10.6 mm above
// the front board's top face, so the panel rear is 9.0 mm above it. Basis: the
// ordered Thonkiconn WQP419GR jacks clamp the panel at their body shoulder - the
// jack drawing gives 9.0 mm of body along the axis, and 9.0 + the 1.6 mm panel
// = 10.6 mm. The chain below is derived from that ONE number, not the other way
// round: the display used to set it (assumptions A and B), and a 10.0 mm panel
// was the earlier reading of the same jack.
// Omron's B32 reference is 10.0 +-0.4 from the PCB TOP to the CAP TOP, so on
// this 10.6 mm panel a bought B32 cap top lands 0.60 mm BEHIND the panel front
// instead of the 1.0-2.0 mm OVER it that the drawing wants. See BUTTON_STACK;
// the bought cap is an alternative and does not suit this stack (nominal).
// NOT an accepted physical fit. The jack's seating, thread runout and nut/washer
// stack, the encoder's supplied hardware and the display seating are unmeasured;
// 10.6 is a design datum the first article has to confirm.
export const PROVISIONAL_Z = {
  panelFront: 0,
  panelRear: PANEL.thickness,                                                        // 1.60
  displayGlassFront: DISPLAY_GLASS_FRONT_Z,                                          // 0.00 (assumed flush)
  displayPcbRear: DISPLAY_PCB_REAR_Z,                                                // 10.05
  displayRearMost: mm3(DISPLAY_GLASS_FRONT_Z + DISPLAY_VENDOR.overallDepth),          // 14.05
  frontBoardTop: PANEL_TO_FRONT_BOARD_TOP,                                           // 10.60
  frontBoardRear: mm3(PANEL_TO_FRONT_BOARD_TOP + FRONT_PCB.thickness),                // 12.20
  rearBoardFront: REAR_BOARD_FRONT_Z,                                                // 23.40
  rearBoardRear: mm3(PANEL_TO_FRONT_BOARD_TOP + FRONT_PCB.thickness + INTERCONNECT_MECH.seatToSeat + REAR_PCB.thickness), // 25.00
} as const
// The panel-to-board gap FALLS OUT of the panel datum and the display chain. It is
// a consequence of a design target, not evidence for it, and it is not certified.
export const PANEL_TO_FRONT_BOARD_GAP = mm3(PROVISIONAL_Z.frontBoardTop - PROVISIONAL_Z.panelRear) // 9.00
export const JACK_CLAMPED_GAP = PANEL_TO_FRONT_BOARD_GAP // the jacks clamp the panel at 9.00

// The panel gap is set by the JACKS, not chosen. Their body shoulder and the
// nut/washer stack are the only thing that clamps the panel, so they fix the
// CLOSEST the panel can come to the front board - board top to panel REAR face.
// The panel's own thickness then adds on top of that to give the FRONT face, the
// surface the operator touches and the face every "does it reach the panel?"
// question is measured to. At 10.6 mm the jack's 4.5 mm of threaded barrel
// nominally spans 10.0...14.5 mm from the PCB (tip included), i.e. 3.9 mm stands
// in front of the panel front for the nut and washer - UNMEASURED (the drawing
// gives the barrel, not the seating, the runout, the pitch or the hardware).
// 8.3 mm on that drawing is the TERMINAL SPACING, not a body height; the earlier
// "body 8.3 + 4.5 thread = 12.8 bushing face" reading is withdrawn.
export const PANEL_STACK = {
  boardToPanelRear: JACK_CLAMPED_GAP,                          // 9.00, set by the jacks
  panelThickness: PANEL.thickness,                             // 1.60
  boardToPanelFront: mm3(JACK_CLAMPED_GAP + PANEL.thickness),  // 10.60
} as const


// Where a body at a given panel Z sits in the FRONT BOARD's own 3D frame.
// A cad model on that board is drawn with jscad +z along the board normal and
// its origin on the board's MID-plane (measured on the exported GLB, `tsci
// export -f glb`, node translations plus mesh bounds); the panel is on that
// board's top face, so +z points AWAY from panel +Z. Callers hand panel Z to
// this and get the jscad z the 3D renderer wants - it is what draws the printed
// parts and the assembled stack.
export const FRONT_BOARD_MID_PANEL_Z = mm3(PROVISIONAL_Z.frontBoardTop + FRONT_PCB.thickness / 2) // 11.40
export const frontBoardUpZ = (panelZ: number) => mm3(FRONT_BOARD_MID_PANEL_Z - panelZ)

// Light-pipe reach: the retired Bivar PLP2-400-C 10.16 mm reference and the
// 17.45 - 10.16 = 7.29 mm subtraction are WITHDRAWN and kept only as history
// (that 10.16 mm value is not in the current Bivar datasheet, its family Ø3.3 mm
// flange falls through the old Ø4.0 mm opening, and the subtraction compared
// different reference planes). The active candidate follows in the next block.
// OWNER-SELECTED CANDIDATE, NOT A PRODUCTION FIT: Signal Construct PGN25L165.
// Manufacturer drawing: https://static.rapidonline.com/pdf/07-1015_v1.pdf,
// SHA-256 5350ad1f0a795e0d688dc74e223239c82cfb3e27951244a10cb43873eeeaa46b,
// retrieved 2026-11-09. Dimensions read off that drawing: shaft Ø2.5 +-0.05 mm;
// x = 16.5 +-0.1 mm from the head underside to the tip; head Ø3 +-0.05 mm; head
// height 1.0 mm; three longitudinal ribs; mounting hole Ø2.6 -0.05 mm
// (2.55-2.60 mm). The owner's screenshot for this MPN states 17.5 mm overall,
// which is 16.5 shaft + 1.0 head. The datasheet states no user cutting
// instruction and no proven retention in 1.6 mm FR-4; neither is claimed here.
//
// The retired Bivar PLP2-400-C is HISTORICAL: the current Bivar datasheet does
// not show that 10.16 mm reference, and its family Ø3.3 mm flange falls through
// the old Ø4.0 mm opening. Do not use it as an active candidate.
export const LIGHT_PIPE = {
  part: "Signal Construct PGN25L165",
  status: "candidate, not a production fit",
  sourceUrl: "https://static.rapidonline.com/pdf/07-1015_v1.pdf",
  sourceSha256: "5350ad1f0a795e0d688dc74e223239c82cfb3e27951244a10cb43873eeeaa46b",
  shaftLength: 16.5,            // x, head underside -> tip
  shaftLengthTolerance: 0.1,    // +-0.1 mm on x
  overallLength: 17.5,          // owner screenshot for this MPN
  headHeight: 1.0,
  headDiameter: 3.0,            // +-0.05
  headDiameterTolerance: 0.05,
  shaftDiameter: 2.5,           // +-0.05
  shaftDiameterTolerance: 0.05,
  panelHoleDiameter: 2.6,       // -0.05, i.e. 2.55-2.60
  panelHoleToleranceMinus: 0.05,
  ribs: 3,
} as const

// Placing the head underside on the panel FRONT face is an ASSUMPTION, not a
// measurement: nothing has fitted the part. With Z = 0 at the panel front face
// and +Z rearward, the nominal tip plane is 16.5 mm and the front-board near face
// is 10.6 mm. The nominal signed tip-to-board distance is therefore
// board - tip = 10.6 - 16.5 = -5.9 mm: NEGATIVE means the 16.5 mm tip plane lies
// BEHIND the board plane, i.e. the pipe would penetrate the front board under the
// two assumptions. That is arithmetic between two assumed planes, NOT a
// measured interference, NOT an LED gap and NOT a fit: the LED height, the
// brightness/coupling, the retention and the stack tolerance are unknown, so
// `opticalGap` stays null and the status stays incomplete. No bound may be
// inferred from the +-0.1 mm shaft tolerance alone.
export const LIGHT_PIPE_REACH = {
  part: LIGHT_PIPE.part,
  datum: "panel FRONT face, Z = 0; +Z rearward",
  mountingAssumption: "head underside on the panel front face (ASSUMPTION)",
  shaftLength: LIGHT_PIPE.shaftLength,                 // 16.5
  overallLength: LIGHT_PIPE.overallLength,             // 17.5
  headHeight: LIGHT_PIPE.headHeight,                   // 1.0
  nominalTipZ: LIGHT_PIPE.shaftLength,                 // 16.5 from the panel front
  boardNearFaceZ: PROVISIONAL_Z.frontBoardTop,         // 10.60, provisional
  // SIGNED: board minus tip. Negative = the tip plane is REARWARD of the board
  // plane, so the pipe crosses it. The direction is part of the number.
  nominalTipToBoardPlane: mm3(PROVISIONAL_Z.frontBoardTop - LIGHT_PIPE.shaftLength), // -5.90
  opticalGap: null,                                    // UNKNOWN: LED height unmeasured
  // The 16.5 mm shaft is 5.9 mm too long for this stack: the tip plane passes the
  // front board. A nominal ~9.8 mm shaft (10.6 minus the ~0.8 mm eyeballed LED
  // body) is what the formula gives, and NO part is selected at that length.
  requiredShaftLength: mm3(PROVISIONAL_Z.frontBoardTop - 0.8), // 9.80, eyeballed LED height
  status: "incomplete - the selected part is too long for this stack",
  missing: [
    "LED body height above the front-board top face",
    "measured board standoff / stack tolerances",
    "proven retention in 1.6 mm FR-4 and the real panel hole tolerance",
    "brightness and optical coupling",
  ],
} as const

// Tallest front-board top-side part with a manufacturer-stated height: the
// MIDI receiver in the Toshiba 11-4L1S body, maximum installed height
// 2.1 +-0.1 mm (drawing p.15). It does NOT prove the display underside: the
// display's own underside component map, its FPC latch and the module
// tolerance are unmapped. The corrected stack leaves 5.0 mm from the
// front-board top face to the connector rear-most point (2.8 mm over this part
// alone), so NO assembled clearance is claimed here. Named physical gate.
export const FRONT_TOP_SMD_MAX_HEIGHT = {
  part: "MIDI receiver, Toshiba 11-4L1S body",
  height: 2.2,
  source: "manufacturer drawing p.15, max installed height 2.1 +-0.1 mm",
} as const
// Negative: at the 10.6 mm panel the module's rear 3.45 mm sits BEHIND the front
// board's top face, which is why the board carries an opening.
export const DISPLAY_UNDERSIDE_GAP = mm3(PROVISIONAL_Z.frontBoardTop - PROVISIONAL_Z.displayRearMost) // -3.45

// Installed-depth target from the owner. Unchanged; every depth figure below
// is provisional and none of them releases the target.
export const DEPTH_TARGET = 110 as const

// Provisional rear chain, panel front face to the rear-most point. Candidates,
// not measurements.
//
// frontToRearGap is no longer a cable-bend estimate: the direct 2 x 13 male/
// female pair fixes the distance between the front board's rear face and the
// rear board's front face to the drawing's seat-to-seat dimension, 11.20 mm
// +-0.20 (Sullins drawing 11091 sheet 2). It is a NOMINAL drawing value, not a
// measured gap, and it says nothing about board flatness, solder-joint height
// or the fitted pair.
//
// Provisional depth chains, ONE PER RACK MODULE. Candidates, not
// measurements. The Pi left the main module for PI_PANEL (owner decision
// 2026-11-12), so the main module's rear-most point is now the socketed Pico
// stack on the rear board's rear face, and the Pi stack belongs to the
// mounting panel's chain.
//
// Main module: panel front face -> its rear-most point. picoSocketStack is a
// candidate built from a standard 2.54 mm female header (~8.5 mm), the Pico 2
// board (Pico 2 datasheet: "1 mm thick PCB", figure/section 1), and a ~3.2 mm
// micro-USB body (UNMEASURED connector figure). harnessAllowance covers the
// 40-wire ribbon fold and the cable bends at the module exit (feedthrough
// bracket slots); unmeasured.
export const PROVISIONAL_DEPTH = {
  frontToRearGap: INTERCONNECT_MECH.seatToSeat,
  picoSocketStack: 12.7,
  harnessAllowance: 10,
} as const
// Pi mounting panel: its panel rear face -> the rear-most point of the Pi
// stack. piStandoffAndPcb is the candidate panel standoff + the Pi 1.6 mm PCB:
// the standoff must clear the Pi underside (microSD slot and its card), which
// is unmeasured. piStackRearward is the Active Cooler + M.2 HAT+ + SSD
// candidate 20-30 mm; the owner decision 2026-11-12 keeps the NVMe HAT.
// harnessAllowance covers the USB/DSI/ribbon/pigtail bends at the Pi; unmeasured.
export const PI_PANEL_PROVISIONAL_DEPTH = {
  piStandoffAndPcb: 5.1,
  piStackRearward: 30,
  harnessAllowance: 10,
} as const
// Behringer Eurorack Go module-depth limits (Thomann product page, the source
// docs/mechanical-stack.md cites): 40 mm lower row, 62 mm upper row. A DESIGN
// CHECK bound only: nothing here proves a fit; that is a first-article task.
export const EURORACK_GO_MAX_MODULE_DEPTH = { lowerRow: 40, upperRow: 62 } as const
// The Pi spacer of the RETAINED rear-board Pi option (the hole grid is still
// drilled): a CANDIDATE 3.5 mm, not a proven clearance. The male tail is
// 3.00 mm FROM THE SEATING FACE, i.e. a NOMINAL 1.40 mm protrusion below a
// 1.60 mm PCB (INTERCONNECT_PROTRUSION); the fillet height, the PCB tolerance,
// the Pi hardware and every clearance are unmeasured.
export const PI_SPACER_MM = 3.5 as const
export const provisionalInstalledDepth = (d = PROVISIONAL_DEPTH): number =>
  mm3(PROVISIONAL_Z.frontBoardRear + d.frontToRearGap + REAR_PCB.thickness + d.picoSocketStack + d.harnessAllowance)
// What a case row must swallow: the chain minus the panel plate itself.
export const provisionalModuleDepthBehindPanel = (total = provisionalInstalledDepth()): number =>
  mm3(total - PANEL.thickness)
export const provisionalPiPanelDepth = (d = PI_PANEL_PROVISIONAL_DEPTH): number =>
  mm3(d.piStandoffAndPcb + d.piStackRearward + d.harnessAllowance)

// Front-board top opening for the panel-feedthrough bodies. The LCD is mounted
// on the front board; only the USB feedthroughs need this top-edge relief notch.
// The 3D-printed bracket spans the opening and mounts at these holes.
export const FRONT_TOP_NOTCH = { x: -2.5, y: 52, width: 105, height: 8 } as const

// Panel feedthroughs: exact vendor cutouts and mounting-hole spacing.
// 2026-11-13 (owner): moved left and tightened. The pitch is NOT set by the
// cutouts but by the vendor flange screws, which are fixed by the parts: the
// nearest pair of holes between two neighbouring feedthroughs is
//   P - 9 - 15  (USB-C on 18 mm centres next to a 30 mm flange)  and
//   P - 15 - 15 (two 30 mm flanges),
// and a 1.00 mm FR-4 web between hole edges needs that to be >= 4.35 / 4.50 mm.
// Hence the 28.50 / 34.50 pitches below, not the 24 / 30 the cutouts alone would
// allow. All three sit inside the board's U-opening (-63.25...43.25) so their
// cable tails reach the Pi behind the front board.
export const USB_FEEDTHROUGHS = [
  // SparkFun CAB-15455: 8 x 26.4 flange; 18 x 6 panel slot; no screw ears.
  { id: "USB-C POWER", x: -52, y: 53.5, cutoutWidth: 12.6, cutoutHeight: 7.6, holeSpacing: 18, holeDiameter: 3.2 },
  // Adafruit 908: 38.30 x 11.90 flange; 14.65 x 7.10 opening; 2x Ø3.5 on 30 mm centres.
  { id: "UMC AUDIO USB", x: -23.5, y: 53.5, cutoutWidth: 14.9, cutoutHeight: 7.3, holeSpacing: 30, holeDiameter: 3.5 },
  { id: "MIDI HUB USB", x: 11, y: 53.5, cutoutWidth: 14.9, cutoutHeight: 7.3, holeSpacing: 30, holeDiameter: 3.5 },
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

// 2026-11-13: the top relief notch and the display opening are emitted as
// CHAMFERED polygons instead of plain rects. Reason is routing, not looks: with
// the 2 x 13 interconnect on the left, control nets have to round these
// cutouts, and a square internal corner leaves no legal path at 0.20 mm
// clearance (the autorouter clipped the notch corner at 0.10 mm). A chamfer
// moves the cutout away from the trace at exactly the corner it rounds.
// Enlarging the display opening at its corners is safe for the module: the
// module is 106.10 x 68.363 inside a 106.50 x 68.90 opening.

export const FRONT_TOP_NOTCH_POLYGON = [
  { x: NOTCH_LEFT, y: NOTCH_TOP },
  { x: NOTCH_LEFT, y: mm3(NOTCH_BOTTOM + NOTCH_CHAMFER) },
  { x: mm3(NOTCH_LEFT + NOTCH_CHAMFER), y: NOTCH_BOTTOM },
  { x: mm3(NOTCH_RIGHT - NOTCH_CHAMFER), y: NOTCH_BOTTOM },
  { x: NOTCH_RIGHT, y: mm3(NOTCH_BOTTOM + NOTCH_CHAMFER) },
  { x: NOTCH_RIGHT, y: NOTCH_TOP },
] as const
export const NOTCH_BOUNDS = { left: NOTCH_LEFT, right: NOTCH_RIGHT, bottom: NOTCH_BOTTOM, top: NOTCH_TOP } as const

// ---- Panel POWER header (J_PWR) ----
// PLACED on the front board's REAR (bottom) face at the top right, mating axis
// toward +Y. Owner decisions: a right-angle header on the rear face, a pocket
// in the printed bracket, and a provisional generic 2.54 mm DuPont-style plug.
//
// SOURCED (Harwin M20-9960245, verified ACTIVE; drawing SHA-256
// 3cd367d6b76e7c6be501578c87a4a9c89721e36a4a8bc36d5e8340646bdcd61b): 2 ways,
// 2.54 +-0.10 mm pitch, Ø1.02 +-0.05 mm recommended hole, 0.64 +-0.02 mm square
// pin, 6.10 mm mating length, and a true right angle: the long mating pins are
// PARALLEL to the PCB, the short 3.00 mm PC tails PERPENDICULAR to it.
//
// ENGINEERING ASSUMPTIONS, not measurements: the 1.60 mm pad (the drill is
// sourced, the pad is ours); and the housing envelope. The exact hole-to-housing
// offset is NOT established - the drawing's 4.80 mm datum is unclear and sheet 1
// of 2 is missing - so POWER_HEADER_KEEPOUT covers both plausible readings of
// 4.80 mm (as a height above the PCB and as a forward extent along the board)
// plus a 2.54 mm backward housing wall. The plug is generic and unmeasured; its
// 8.00 mm reach is a planning allowance.
export const POWER_HEADER_LAND = {
  ways: 2,
  pitch: 2.54,   // sourced
  drill: 1.02,   // sourced, +-0.05
  pad: 1.6,      // ASSUMPTION: 0.29 mm ring
  copperToEdge: 0.2, // project fabrication rule
} as const
export const POWER_HEADER = {
  // Owner decision 2026-11-13: J_PWR follows the POWER button to the top right
  // and no longer sits in the top relief notch - it does not have to be on an
  // edge at all. It stays on the REAR face so its plug lives in the 11.2 mm
  // board gap; the plug points +Y (up, under the switch body, between the
  // switch's two pad rows at x 59.75/71.75 - the 5.08 mm plug spans x 63.5...
  // 68.5, so it clears both).
  // Owner decision 2026-11-13: up, so it sits right under the POWER button
  // (SW7's courtyard ends at y 42.75) and above J5's courtyard (44.5 - 1.6 =
  // 42.9). 7.9 x 3.25 mm pocket between them, at x 60.05...67.95.
  x: 64,
  y: 44.5,
  rotation: 180, // bottom face: the footprint's local +Y maps to global +Y
  layer: "bottom" as const,
  matingAxis: "+Y",
} as const
// Deliberately conservative provisional keepout. It is the union of the
// plausible 4.80 mm readings, so the tests can prove the bracket pocket clears
// it. It is NOT a part envelope and NOT a measured plug.
export const POWER_HEADER_KEEPOUT = {
  x1: mm3(POWER_HEADER.x - 1.5 * POWER_HEADER_LAND.pitch), // 60.19
  x2: mm3(POWER_HEADER.x + 1.5 * POWER_HEADER_LAND.pitch), // 67.81
  y1: mm3(POWER_HEADER.y - POWER_HEADER_LAND.pitch),       // 41.96: 2.54 wall behind the pads
  y2: mm3(POWER_HEADER.y + 4.8 + 6.1 + 8.0),               // 63.40: 4.80 forward + 6.10 pin + 8.00 plug
  heightAboveBoard: 4.8,
  basis: "covers 4.80 mm as a height and as a forward extent, plus the sourced 6.10 mm pin and an 8.00 mm plug planning reach",
} as const

// Planning allowances with their reason. Change them here only.
export const BRACKET_ALLOWANCE = {
  thickness: 3.0,            // printed bar thickness (unchanged from the first draft)
  mountEdgeWall: 3.0,        // M2.5 nut circumradius ~2.5 mm + 0.5 mm material around it
  mountHeadClearance: 6.5,   // PROVISIONAL M2.5 head/washer envelope; measure the real hardware
  slotClearancePerSide: 1.5, // PROVISIONAL body, cable-bundle and screwdriver access
  spacerHeadWall: 1.0,       // bar bottom below the top spacer row (rear side)
  tieHoleDiameter: 3.2,      // 2.5 mm cable tie
  materialToCuts: 1.0,       // minimum remaining web between two cuts
} as const

// The J_PWR pocket is a cut in the bar, re-anchored to the header after the
// 2026-11-13 move to the top right. The keepout's own y1 (2.54 mm behind the
// pad row) is the pocket's bottom edge: it already contains that backward wall,
// and adding another 0.5 mm here would push the bar's bottom web into the J5
// socket body (top 40.26). The 0.50 mm cutter allowance is kept on X.
export const BRACKET_POWER_POCKET = {
  purpose: "J_PWR pocket: clears the provisional header/plug keepout",
  clearance: 0.5,
  x1: mm3(POWER_HEADER_KEEPOUT.x1 - 0.5), // 59.69
  x2: mm3(POWER_HEADER_KEEPOUT.x2 + 0.5), // 68.31
  y1: POWER_HEADER_KEEPOUT.y1,            // 41.96: contains the 2.54 mm wall
  y2: NOTCH_TOP,                          // 56: open to the bracket top edge
} as const

// Bracket mounts: one on each front-board leg. The old -60 mm mount sat over
// the U opening (-63.25...43.25), which removed the material under it. The
// mounts sit 3 mm inside the bracket's top edge, above the top front/rear
// spacer head at (-69, 46): at y 53 the D6.5 head envelope clears the D2.7
// hole by 2.83 mm. The left mount at x -66.5 is on the left leg
// (-75...-63.25); the right at 55 is on the right leg (43.25...75). The left
// x also leaves 1.07 mm of web to the first 3.2 mm tie hole and to the USB-C
// slot, over the 1.0 mm cut rule.
const BRACKET_MOUNT_X = [-66.5, 55] as const
const BRACKET_MOUNT_Y = mm3(NOTCH_TOP - BRACKET_ALLOWANCE.mountEdgeWall) // 53.0

const bracketOutline = {
  x1: mm3(Math.min(...BRACKET_MOUNT_X) - BRACKET_ALLOWANCE.mountEdgeWall), // -69.5
  x2: mm3(Math.max(
    Math.max(...BRACKET_MOUNT_X) + BRACKET_ALLOWANCE.mountEdgeWall, // 58.0
    BRACKET_POWER_POCKET.x2,                                        // 68.31
  )),
  // The bar is on the REAR side of the front board, so the front-side display
  // envelope does NOT constrain it. Its bottom edge clears the top spacer row
  // AND keeps a full material web below the J_PWR pocket; the J5 socket body
  // (top 40.26) then stays 0.70 mm clear of it.
  y1: mm3(Math.min(
    Math.min(...FRONT_REAR_MOUNT.filter(h => h.y > 0).map(h => h.y)) - BRACKET_ALLOWANCE.spacerHeadWall, // 45.0
    BRACKET_POWER_POCKET.y1 - BRACKET_ALLOWANCE.materialToCuts, // 40.96
  )),
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
// above the U opening's bottom (-23.45), because below that the board is solid.
// All slots must satisfy slotFloorY(f) >= FRONT_BOARD_OPENING.bottom;
// layout.test.ts and mechanical/bracket_geometry.py both enforce it, so a
// future feedthrough that needs a deeper slot fails the checks instead of being
// silently clamped.
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
// 3 mm layer behind the front board, so the bracket must clear every head whose
// centre lies under the bar's plan. The right spacer at (71.5, 46) is outside
// the bar (x2 68.31) and therefore needs no cut. The head envelope itself is
// unmeasured: a named physical gate.
const bracketHeadClearance = FRONT_REAR_MOUNT
  .filter(h => h.y > 0)
  .filter(h => h.x >= bracketOutline.x1 && h.x <= bracketOutline.x2 &&
               h.y >= bracketOutline.y1 && h.y <= bracketOutline.y2)
  .map(h => ({
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
    width: mm3(bracketOutline.x2 - bracketOutline.x1),   // 137.81
    height: mm3(bracketOutline.y2 - bracketOutline.y1),  // 15.04
  },
  slots: bracketSlots,
  mountHeadClearance: bracketHeadClearance,
  tieHoles: bracketTieHoles,
} as const

// ---- Panel POWER header (J_PWR) candidate facts ----
/**
 * The identified header candidate, as facts and as open questions.
 */
export const POWER_HEADER_CANDIDATE = {
  mpn: "Harwin M20-9960245",
  description: "2.54 mm pitch SIL horizontal PC tail pin header assembly, 2 ways, through-hole, right angle",
  drawingUrl: "https://www.harwin.com/products/M20-9960245/",
  drawingSha256: "3cd367d6b76e7c6be501578c87a4a9c89721e36a4a8bc36d5e8340646bdcd61b",
  retrieved: "2026-11-10",
  // Quoted on that drawing, not measured:
  drill: 1.02,
  drillTolerance: 0.05,
  pinSquare: 0.64,
  pinSquareTolerance: 0.02,
  matingLength: 6.1,
  pitch: 2.54,
  ways: 2,
  orientationResolved: true,
  orientationNote: "an independent architect reading resolved the orientation: the long 6.10 mm mating pins are parallel to the PCB and the short 3.00 mm PC tails perpendicular to it, a true right angle",
  heightAbovePcb: 2.54,
  housingOffsetResolved: false,
  housingOffsetNote: "the exact hole-to-housing offset is unresolved: the 4.80 mm datum is unclear and sheet 1 of 2 is missing; POWER_HEADER_KEEPOUT covers both plausible readings",
  sourcingVerified: false,
  sourcingNote: "digikey.se returned Cloudflare HTTP 403 on 2026-11-10, so SKU and stock are UNVERIFIED; the block is not bypassed and no stock is claimed",
  notSelected: [
    "an Electrokit breakaway strip was proposed by the researcher, not selected by the owner: do not substitute it",
    "the provisional mating plug is a generic DuPont-style housing, not a measured part",
  ],
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
  displayDatum: DISPLAY_DATUM,
  displayStandoff: DISPLAY_STANDOFF,
  provisionalZ: PROVISIONAL_Z,
  // The two 3D-printed front-panel parts: shape and position, panel
  // coordinates. printed_geometry.py reads this block instead of owning a copy.
  printedParts: PRINTED_PARTS,
  displayUndersideGap: DISPLAY_UNDERSIDE_GAP,
  frontTopSmdMaxHeight: FRONT_TOP_SMD_MAX_HEIGHT,
  lightPipe: LIGHT_PIPE,
  lightPipeReach: LIGHT_PIPE_REACH,
  panelToFrontBoardGap: PANEL_TO_FRONT_BOARD_GAP,
  depthTarget: DEPTH_TARGET,
  provisionalDepth: PROVISIONAL_DEPTH,
  lcdCutout: { ...PANEL_LCD_CUTOUT, allowances: LCD_CUTOUT_ALLOWANCE },
  fpcConnector: { ...DISPLAY_FPC_CONNECTOR, at: DISPLAY_FPC_CONNECTOR_AT },
  // The actual aperture polygon, so Python can re-measure the corner clearance
  // from the same geometry the panel emits.
  lcdCutoutOutline: roundedRectOutline(PANEL_LCD_CUTOUT),
  frontTopNotch: FRONT_TOP_NOTCH,
  notchBounds: NOTCH_BOUNDS,
  // The cut the front board actually emits: one U-shaped polygon. The
  // notch bounds above are retained planning data, not a board edge.
  frontBoardOpening: FRONT_BOARD_OPENING,
  frontBoardOpeningPolygon: FRONT_BOARD_OPENING_POLYGON,
  bracketAllowance: BRACKET_ALLOWANCE,
  // The owner-approved J_PWR pocket travels with the bracket data so the CAD
  // cuts it and the geometry checks see it; it is declared below this block.
  bracket: { ...FEEDTHROUGH_BRACKET, powerPocket: BRACKET_POWER_POCKET },
  frontRearMount: FRONT_REAR_MOUNT,
  mountHoleDiameter: MOUNT_HOLE_DIAMETER,
  feedthroughs: USB_FEEDTHROUGHS,
  // Every drilled hole the STEP/STL verification models need, so no CAD script
  // restates a coordinate. Adding a key here is additive: no position moves.
  railHoles: RAIL_HOLES,
  railHoleDiameter: RAIL_HOLE_DIAMETER,
  frontBoardHoles: FRONT_BOARD_HOLES,
  // The canonical panel openings, so the CAD panel cannot drift from the board.
  panelApertures: PANEL_APERTURES,
  panelDrills: PANEL_DRILLS,
  panelMountHoles: PANEL_MOUNT_HOLES,
  lcdMountHoles: LCD_MOUNT_HOLES,
  displayBossPattern: DISPLAY_BOSS_PATTERN,
  // The recorded ALTERNATIVE inner (plain-hole) pattern, module-local. It is
  // asymmetric in x (25.62 vs 22.38 mm insets), so it can DISTINGUISH the two
  // candidate in-plane rotations of the vendor model. It does NOT decide which
  // one is correct: this record uses the same no-flip premise that is itself
  // questioned (the drawing may be a rear view). The orientation stays pending
  // until a labelled front/rear photograph or the physical part is available; a
  // candidate that reproduces these positions is consistent only.
  displayStandoffs: DISPLAY_STANDOFFS,
  // The Pi 5 board in its OWN frame (drawing constants). The Pi panel's holes
  // are derived from these through piBoardToPanel; mechanical/step_assembly.py
  // re-derives them independently and fails if they disagree.
  piBoard: { ...PI_BOARD, holeInset: PI_HOLE_INSET, holePitch: PI_HOLE_PITCH,
    holeDiameter: PI_HOLE_DIAMETER, headerCentreX: PI_HEADER_CENTRE_X },
  piProtrusion: PI_PROTRUSION,
  piSpacer: PI_SPACER_MM,
  // The 20HP Pi mounting panel (owner decision 2026-11-12). It sits BESIDE the
  // main module in the same row (the added HP), with its own depth check.
  // Geometry only; the harness lengths stay planning values in the docs.
  piPanel: {
    ...PI_PANEL,
    railHoles: PI_PANEL_RAIL_HOLES,
    railHoleDiameter: RAIL_HOLE_DIAMETER,
    piMount: PI_PANEL_PI_MOUNT,
    piMountHoles: PI_PANEL_PI_HOLES,
    piBoardBox: PI_PANEL_PI_BOX,
    piEnvelope: PI_PANEL_PI_ENVELOPE,
    mirroredX: true,
  },
  piPanelProvisionalDepth: PI_PANEL_PROVISIONAL_DEPTH,
  eurorackGoMaxModuleDepth: EURORACK_GO_MAX_MODULE_DEPTH,
  // J_PWR plan and the bracket pocket plan: DATA ONLY. No footprint, no cut and
  // no board change follows from these keys.
  powerHeader: {
    placement: POWER_HEADER,
    land: POWER_HEADER_LAND,
    keepout: POWER_HEADER_KEEPOUT,
    pocket: BRACKET_POWER_POCKET,
    candidate: POWER_HEADER_CANDIDATE,
  },
  // Direct 2 x 13 interconnect: one placement for both boards plus the quoted
  // manufacturer model. The pads are not restated here - the circuit files own
  // the footprint and lib/interconnect.test.ts proves the built coordinates.
  interconnect: {
    front: FRONT_INTERCONNECT,
    rear: REAR_INTERCONNECT,
    seatToSeat: INTERCONNECT_MECH.seatToSeat,
    seatToSeatTolerance: INTERCONNECT_MECH.seatToSeatTolerance,
    bodyLength: INTERCONNECT_MECH.bodyLength,
    bodyEnvelope: INTERCONNECT_MECH.bodyEnvelope,
    maleHeight: INTERCONNECT_MECH.maleHeight,
    maleTail: INTERCONNECT_MECH.maleTail,
    femaleHeight: INTERCONNECT_MECH.femaleHeight,
    femaleTail: INTERCONNECT_MECH.femaleTail,
    frontBoardRearToRearBoardTop: PROVISIONAL_DEPTH.frontToRearGap,
    gapStatus: "nominal drawing dimension, not a measured gap; support and flatness unverified",
  },
})

// ---- Right-hand control column ----
// The value knob, NAV and SHIFT/EXIT share one vertical centreline, right of
// the display and below the USB feedthroughs, with equal 19 mm spacing. The
// knob is context-aware: the Pi applies its delta to the focused control, with
// tempo as the home context (docs/decisions.md, value-knob row). The panel
// legend therefore reads VALUE, not TEMPO - tempo is only what the knob edits
// when nothing else has focus. `RIGHT_CONTROLS.TEMPO` stays the position key
// so the mechanical layout data does not churn; it is the column, not a label.
// Owner decision D2 (2026-11-07): the column moved 47 -> 52 mm so the 128 mm
// front board can carry the 106 mm module (module right edge 43.5, knob body
// left edge 45.75, 2.25 mm clear). The knob body then keeps 5.75 mm to the
// board edge at 64.
// 2026-11-13 (owner): one column at x 52. It started as three evenly spaced
// controls at a 17 mm pitch; SW7 then moved up on its own (y 49) and TEMPO kept
// climbing (29 -> 33) as the owner made room below it, so the column is now
// explicit rather than an arithmetic progression: TEMPO 31, NAV 12, SHIFT/EXIT
// -5. Keep the explicit values - deriving NAV/SHIFT from TEMPO would drag them
// along every time TEMPO moves.
// ---- MIDI clusters (front board) ----
// ONE source of truth for where each opto/LED cluster sits. front.circuit.tsx
// renders each cluster inside a <group pcbX pcbY> and works in cluster-LOCAL
// coordinates, so moving a cluster is one number here and its silkscreen marks
// (lib/silk.tsx derives from these) can never be stranded on the old spot - they
// were, twice, while this board was being re-placed by hand.
export const MIDI_CLUSTERS = {
  // in1/in2 origin = the TLP2361 body centre; out1/out2 origin = the AO3401A.
  in1: { x: -16, y: -48 },
  in2: { x: 2, y: -48 },
  out1: { x: 20, y: -46.1 },
  out2: { x: 38, y: -46.1 },
} as const

export const RIGHT_COLUMN = { x: 52 } as const
// Owner decision 2026-11-13: SW7 sits alone at y 49 and TEMPO/NAV moved up to
// 26/9. TEMPO then moved up another 3 (to 29) so the 3.4 mm band below U6's
// courtyard (20.60) can take C5 and C6 side by side. The column keeps its 17 mm
// pitch: TEMPO 29, NAV 12, SHIFT/EXIT -5.
const RIGHT_TEMPO_Y = 31
export const RIGHT_CONTROLS = {
  TEMPO: { x: RIGHT_COLUMN.x, y: RIGHT_TEMPO_Y },
  NAV: { x: RIGHT_COLUMN.x, y: 12 },
  SHIFT_EXIT: { x: RIGHT_COLUMN.x, y: -5 },
} as const

// ---- 3D-printed front-panel parts (owner's two printed parts) ----
// One keytop for the NAV switch U5 (ALPS Alpine SKRHABE010) and one value knob
// for the encoder U6 (Bourns PEC11R-4015F-S0024). Both are printed plastic, so
// neither is a board part and neither has pads: they are drawn in the front
// board's 3D view as reference envelopes and BUILT by mechanical/printed/.
//
// This block is the single source of truth for their SHAPE AND POSITION.
// mechanical/layout.json carries it (mechanicalExport below), and
// mechanical/printed/printed_geometry.py reads it instead of keeping its own
// copy. Python still owns the datasheet inputs and the tolerance chain, and
// re-derives zFront/zRear from them: nav_keytop() and knob() fail if that
// derivation disagrees with the envelope stated here, so the two cannot drift.
//
// Z datum is the same as PROVISIONAL_Z: 0 at the panel FRONT face, +Z rearward.
//
// NOT A FIT MODEL: every number below is a design intent that the first article
// has to measure. The parts' own README lists what is assumed.
const NAV_KEYTOP_HEAD_HEIGHT = 4.0     // 3-5 mm proud of the panel front
const NAV_KEYTOP_SOCKET_DEPTH = 1.5    // under the ALPS 1.7 mm keytop fitting area
// ALPS SKRH drawing No.1: overall switch height above the mounting surface. The
// socket floor floats this far minus the free play above the front-board top.
const NAV_SWITCH_HEIGHT_ABOVE_BOARD = 5.0
const NAV_KEYTOP_AXIAL_FREE_PLAY = 0.15
const KNOB_HEIGHT = 16.0
// Panel front to the knob's rear face: the assumed supplied M7 nut + flat
// washer (2.50) plus 0.20 mm of air. The PEC11R datasheet states they are
// supplied but does not dimension them, so this is an assumption, not a fact.
const KNOB_REAR_STANDOFF = 2.7

export const PRINTED_PARTS = {
  navKeytop: {
    part: "ALPS Alpine SKRHABE010 keytop",
    model: "mechanical/printed/nav-keytop",
    // U5's centre: the keytop is coaxial with the switch stem.
    x: RIGHT_CONTROLS.NAV.x,
    y: RIGHT_CONTROLS.NAV.y,
    // Revolved envelope. Head and neck are one diameter because the head leads
    // when the cap is fitted through the panel aperture from the rear; the
    // flange then traps it on the panel's rear face.
    headHeight: NAV_KEYTOP_HEAD_HEIGHT,
    neckDiameter: 7.0,                 // 7.20 panel aperture - 0.20 sliding fit
    flangeDiameter: 9.6,
    flangeThickness: 1.2,
    shankDiameter: 5.6,
    socketBore: 3.2,                   // D3.00 stem + 0.20 clearance
    socketDepth: NAV_KEYTOP_SOCKET_DEPTH,
    zFront: mm3(PROVISIONAL_Z.panelFront - NAV_KEYTOP_HEAD_HEIGHT),                     // -4.00 head front
    zRear: mm3(PROVISIONAL_Z.frontBoardTop - NAV_SWITCH_HEIGHT_ABOVE_BOARD
      - NAV_KEYTOP_AXIAL_FREE_PLAY + NAV_KEYTOP_SOCKET_DEPTH),                          // 6.95 socket mouth
  },
  knob: {
    part: "Bourns PEC11R-4015F-S0024 value knob",
    model: "mechanical/printed/knob",
    // U6's centre: the knob is coaxial with the encoder shaft.
    x: RIGHT_CONTROLS.TEMPO.x,
    y: RIGHT_CONTROLS.TEMPO.y,
    bodyDiameter: 12.5,
    height: KNOB_HEIGHT,
    boreDiameter: 6.2,                 // D6.00 shaft + 0.20 clearance
    zRear: mm3(PROVISIONAL_Z.panelFront - KNOB_REAR_STANDOFF),                          // -2.70
    zFront: mm3(PROVISIONAL_Z.panelFront - KNOB_REAR_STANDOFF - KNOB_HEIGHT),           // -18.70
  },
} as const

// PLAY START completes the transport row inside the right-hand control column,
// aligned beneath SHIFT/EXIT (no jack or LED in this column).
export const PLAY_START = { button: "PLAY START", x: RIGHT_COLUMN.x } as const

// ---- Panel POWER button (owner-approved 2026-11-06) ----
// One B3F-4055-family momentary, bottom-right corner: it sits below and right
// of PLAY START (11.4 mm centre-to-centre) so nothing in the transport row can
// be pressed by accident, and it clears the M2.5 holes at (44, -52) and
// (61, -53) and the board edge by the margins asserted in layout.test.ts.
// The contact pair leaves the front board on J_PWR, the right-angle 1x2 header
// on the REAR face at the top notch (POWER_HEADER above); the owner's 6-inch
// F-F DuPont jumpers go straight to the Pi 5 remote PWR connector. No power
// circuitry, no interconnect conductor, no rear-board part: the direct 2 x 13
// pair keeps all three ground contacts.
// Owner decision 2026-11-13: POWER moves to the TOP RIGHT corner, freeing the
// bottom-right corner it held for the MIDI clusters.
//
// Geometry, measured on the footprints rather than assumed:
//  - the B3F-4055 is 14.5 x 10.8 mm (pads at +-6.25, 1.8 mm mounting holes at
//    +-4.5). The board above the display opening is only 56 - 45.45 = 10.55 mm,
//    so the switch cannot sit anywhere over the opening - it has to clear it in
//    X. Its left pad edge must be >= 43.25 + 0.2, i.e. body centre >= 50.7.
//  - at x 66 (body 60...72) it is fully clear of the opening, sits outboard of
//    the printed feedthrough bracket (which ends at x 58) and leaves 3 mm to the
//    board edge. The 12.25 mm B32-cap opening then fits the panel with 3.9 and
//    16.6 mm webs, so POWER keeps a normal cap - unlike the left margin, which
//    is 12.75 mm wide and could not.
//  - the top-right panel standoff used to be at (61, 53), inside this body; it
//    moved to (61, 41) (FRONT_BOARD_HOLES below).
// Owner decision 2026-11-13: SW7 sits ALONE further up the column (y 49, i.e.
// its courtyard 42.75...55.25, 0.75 mm inside the top edge) rather than one pitch
// above TEMPO. TEMPO and NAV keep their even 19 mm spacing; the power button
// reads as its own control at the top of the leg. Its panel legend prints ABOVE
// the opening (owner rule 2026-11-13); the offset is LABEL_OFFSET.button in the
// panel-label block below.
// y 48.9, not 49.0: the B3F-4055 footprint draws its body outline 6.5 mm above
// the part centre, so at 49.0 that line sat at y 55.50 - exactly the 0.5 mm
// guard the release silk gate allows, which it then failed on rounding. 48.9
// leaves 0.6 mm to the board edge and changes nothing an operator can feel.
export const POWER_BUTTON = { x: RIGHT_COLUMN.x, y: 48.9, rotation: 0 } as const

// ---- Transport / MIDI columns under the display ----
// Four equally spaced columns; each column shares its X between the transport
// button, its LED (REC / OVER DUB / PLAY only) and its MIDI jack. These are
// visual columns only; electrical assignments live in lib/pin-map.ts.
// 2026-11-13 (owner): the transport row is centred under the LCD cutout. The
// opening spans x -63.25...43.25, centre -10.00, so a 24 mm pitch puts REC /
// OVER DUB / PLAY / STOP on -46, -22, +2, +26. The LEDs and their 330R series
// resistors derive from these (ledPosition), and the jacks no longer share the
// column X, so nothing drags.
const TRANSPORT_LEFT_X = -46
export const TRANSPORT_COLUMN_PITCH = 24
export const TRANSPORT_Y = -34
// LEDs sit BESIDE their buttons, not above them (owner-authorised fallback 3,
// 2026-11-07): the glass-derived panel window reaches down to y -24.94, and a
// light pipe at y -24 would sit inside it. 12 mm to the right keeps a 3.8 mm
// FR-4 web to the 4.0 mm cutout and the 12.25 mm button opening beside it, and
// is clear of the next column's button (24 mm pitch).
export const LED_Y = TRANSPORT_Y
const LED_X_OFFSET = 12
// button half-side 6.125 + LED r1.3 needs under 12 mm for a routable FR4 web;
// 12 mm gives 4.6 mm to the square edge.
export const JACK_Y = -47.5
const TRANSPORT_BUTTON_NAMES = ["REC", "OVER DUB", "STOP", "PLAY"] as const
const MIDI_JACK_NAMES = ["MIDI IN 1", "MIDI IN 2", "MIDI OUT 1", "MIDI OUT 2"] as const
export type TransportColumn = { button: string; jack: string; x: number }
export const FRONT_COLUMNS: readonly TransportColumn[] = TRANSPORT_BUTTON_NAMES.map((button, i) => ({
  button,
  jack: MIDI_JACK_NAMES[i] ?? `MIDI COLUMN ${i + 1}`,
  x: TRANSPORT_LEFT_X + i * TRANSPORT_COLUMN_PITCH,
}))
// LEDs beside REC / OVER DUB / PLAY; ids shared with the concept artwork.
export const LED_ID = { REC: "REC LED", "OVER DUB": "OVERDUB LED", PLAY: "PLAY LED" } as const
export const ledPosition = (columnX: number) => ({ x: mm3(columnX + LED_X_OFFSET), y: LED_Y })

export const transportButtonX = (button: string): number => {
  if (button === PLAY_START.button) return PLAY_START.x
  const col = FRONT_COLUMNS.find(c => c.button === button)
  if (!col) throw new Error(`unknown transport button ${button}`)
  return col.x
}
// 2026-11-13 (owner, ergonomics): the MIDI jacks no longer share their X with
// the transport buttons. Four cables leave the board far better from one grouped
// block at the left of the bottom edge than from under SW1-SW4, where they foul
// the buttons and the hand pressing them. The WQP419GR's THT pads are only
// 3.10 mm wide and its courtyard 10.00 x 14.83 mm (both measured from the built
// footprint), so 12 mm pitch is legal: 2 mm between courtyards.
export const MIDI_JACK_PITCH = 12
export const MIDI_JACK_LEFT_X = -62
export const MIDI_JACK_X: Record<string, number> = Object.fromEntries(
  MIDI_JACK_NAMES.map((name, i) => [name, MIDI_JACK_LEFT_X + i * MIDI_JACK_PITCH]),
)
export const midiJackX = (jack: string): number => {
  const x = MIDI_JACK_X[jack]
  if (x === undefined) throw new Error(`unknown MIDI jack ${jack}`)
  return x
}

// Direct 2 x 13 interconnect: J5 (female, front board BOTTOM face) and J_FRONT
// (male, rear board TOP face) share this ONE position and rotation, so their 26
// pads coincide in board X/Y. The position is chosen from the BUILT geometry of
// both boards, not by eye:
//   * measured on the BUILT boards, the nearest copper EDGE-to-EDGE gap is
//     13.16 mm: the front socket's pad at (8.73, 12.62) to the D2 pad at
//     (-5.83, 12.00). On the rear board the same measure is 18.48 mm (J_FRONT
//     pad at (11.27, 10.08) to R17's pad at (31.18, 10.00));
//   * the 40.52 x 8.80 mm body envelope keeps >= 13 mm to the Pi mounting holes,
//     the four front-to-rear spacers and every through-hole solder joint on the
//     face it is mounted to (asserted on the built output, not by eye);
//   * the 26 pads stay outside the display FPC PLANNING MARKER's plan box. The
//     marker's identity and position are unverified, so that is a plan-geometry
//     statement about the marker, NOT a clearance to a real connector;
//   * the male tail is 3.00 mm from the connector seating face, i.e. a NOMINAL
//     1.40 mm protrusion beyond a 1.60 mm PCB (INTERCONNECT_PROTRUSION), and
//     no Pi board sits above those tails (the Pi is on its own panel). A 3.5 mm
//     Pi spacer is a CANDIDATE
//     only, not a safety claim: the fillet and clearance are unmeasured (see
//     PROVISIONAL_DEPTH).
// Superseded candidates: J5 at (34, 10) with J_FRONT there would have put the
// male body over the Pi hole at (30.5, 29) and its 26 holes through Q3,
// R16–R18; J5 at (45, 0) put the socket body on SW6's and U5's pads and under
// the DSI connector. Nothing is waived by moving the pair: the pad, body and
// tail clearances above are asserted by lib/interconnect.test.ts on the built
// circuit JSON.
// Moved 2026-11-13 with the display opening: (10, 5) is inside it, and the
// 40.52 mm body cannot sit in the bottom strip (32.55 mm tall, free bands 3.05
// and 1.1 mm). It sits at the bottom of the right-hand strip, below the four
// MIDI clusters stack BELOW it: y 31 puts the body at 10.74...51.26, inside the
// board edge
// (-56) and inside the 148 mm rear board. The clusters sit BELOW it and the
// jacks are on the bottom edge, so no MIDI net has to cross the display opening
// - the older y -30 position forced J2's ring trace to hug the cutout at
// 0.150 mm, under the 0.20 mm rule, and the autorouter ignored the keepout.
// A left-margin position was tried first and rejected: it dragged SW5-SW7, U5
// and U6 across the whole board.
// Owner decision 2026-11-13: the 2 x 13 interconnect moves to the LEFT of the
// display, i.e. into the 11.75 mm left strip (body 7.60 x 40.52 at 90 deg, so it
// fits with 1.7 / 2.4 mm margins). That empties the right strip for the POWER
// button and the four MIDI clusters. The rear board's J_FRONT follows this.
// Owner decision 2026-11-13: J5 sits slightly higher (16 -> 20) now that J_PWR
// moved up with it; its courtyard is -0.51...40.51 and the top pad row is at
// 37.78, leaving 3 mm to J_PWR's courtyard.
export const FRONT_INTERCONNECT = { x: 66, y: 20, rotation: 90 } as const
// The rear board carries the male at the SAME coordinate. One constant, not
// two: the coincidence test compares the BUILT pads.
export const REAR_INTERCONNECT = FRONT_INTERCONNECT

// ---- Concept points for the generated preview artwork ----
// Point positions only. Preview symbols do not represent physical part sizes.
export type FrontConceptKind = "cable" | "control" | "button" | "led" | "jack"
export type FrontConceptPoint = { id: string; x: number; y: number; kind: FrontConceptKind }
export const FRONT_CONCEPT: readonly FrontConceptPoint[] = [
  ...USB_FEEDTHROUGHS.map(f => ({ id: f.id, x: f.x, y: f.y, kind: "cable" as const })),
  { id: "VALUE", ...RIGHT_CONTROLS.TEMPO, kind: "control" },
  { id: "NAV", ...RIGHT_CONTROLS.NAV, kind: "control" },
  { id: "SHIFT / EXIT", ...RIGHT_CONTROLS.SHIFT_EXIT, kind: "button" },
  ...FRONT_COLUMNS.map(c => ({ id: c.button, x: c.x, y: TRANSPORT_Y, kind: "button" as const })),
  { id: PLAY_START.button, x: PLAY_START.x, y: TRANSPORT_Y, kind: "button" },
  ...FRONT_COLUMNS.filter(c => c.button in LED_ID).map(c => ({
    id: LED_ID[c.button as keyof typeof LED_ID], ...ledPosition(c.x), kind: "led" as const,
  })),
  ...FRONT_COLUMNS.map(c => ({ id: c.jack, x: midiJackX(c.jack), y: JACK_Y, kind: "jack" as const })),
]

// ---- Button key top: printed cap STANDARD, Omron/ARATAS B32-1380 alternative ----
// The standard key top for SW1-SW7 is the printed cap designed in
// BUTTON_CAP_PRINTED below. The bought B32-13x0 remains the documented
// alternative, and the facts below are its Omron B32 datasheet, page 3
// (B32-13x0 drawing), tolerance +/-0.4 unless stated. The B32-1380 is the RED
// 12 x 12 mm square key top; the black 12 x 12 mm equivalent is B32-1310 (page
// 1 table).
//
// THE 10.0 AND 1.0-2.0 DIMENSIONS ARE NOW VERIFIED FROM THE DRAWING'S VECTOR
// DATA (two independent reads of page 3): PCB top y 146.00, cap top y 105.17,
// panel top y 113.29. So:
//   * 10.0 +-0.4 mm is from the PCB TOP to the CAP TOP, NOT to the panel front;
//   * 1.0-2.0 mm is the CAP TOP's PROTRUSION OVER THE PANEL, not the panel
//     thickness.
// Cross-check on the round-cap drawing of the same sheet: 7.3 - 2.8 + 7.0 = 11.5,
// and that drawing dimensions 11.5 mm from the PCB to the cap top.
// The earlier "10.0 = PCB to panel FRONT" reading was WRONG and is withdrawn.
export const BUTTON_CAP = {
  part: "Omron/ARATAS B32-1380",
  colour: "red",
  topSquare: 12.0,
  topSquareTolerance: 0.15,
  // Verified reference: PCB top -> CAP TOP.
  referenceBoardToCapTop: 10.0,
  referenceBoardToCapTopTolerance: 0.4,
  // Verified reference: how far the cap top stands OVER the panel face.
  capProudAbovePanel: { min: 1.0, max: 2.0 },
  // The round-cap cross-check on the same sheet (7.3 - 2.8 + 7.0), also
  // PCB -> cap top. Recorded so the two drawings cannot drift apart.
  roundCapBoardToCapTop: 11.5,
  referenceCutoutSquare: 12.2,
  referenceCutoutTolerance: { plus: 0.05, minus: 0 },
  source: "https://omronfs.omron.com/en_US/ecb/products/pdf/en-b32.pdf (SHA-256 90834f79bcb19ed59fc242f0a005d8ca47fcdbf2f4583bc53695695bb675d28f)",
} as const

// A bought B32 cap on OUR stack. Z datum: the panel FRONT face is Z = 0, +Z
// rearward; the front board's top face is PROVISIONAL_Z.frontBoardTop = 10.6.
// The reference is PCB -> CAP TOP, so the bought cap's top plane is
//   Z = 10.6 - 10.0 = +0.6  ->  0.60 mm BEHIND the panel front, i.e. RECESSED
//   (band: 10.6 - 10.4 = 0.2 to 10.6 - 9.6 = 1.0, so 0.20...1.00 mm recessed).
// Omron's drawing expects the cap top 1.0-2.0 mm OVER the panel, so a bought B32
// does NOT suit this stack: it sits under the panel front instead of above it.
// That is a PANEL-SUITABILITY finding, not a measurement, and the printed cap is
// unaffected: its own protrusion is 0.80 mm and its own design.
export const BUTTON_STACK = {
  boardTopZ: PROVISIONAL_Z.frontBoardTop,                                   // 10.60
  panelFrontZ: PROVISIONAL_Z.panelFront,                                    // 0.00
  panelRearZ: PROVISIONAL_Z.panelRear,                                      // 1.60
  // Nominal bought-cap top plane, panel Z: positive = behind the panel front.
  referenceCapTopZ: mm3(PROVISIONAL_Z.frontBoardTop - BUTTON_CAP.referenceBoardToCapTop),
  referenceCapTopBelowPanelFront: mm3(PROVISIONAL_Z.frontBoardTop
    - BUTTON_CAP.referenceBoardToCapTop - PROVISIONAL_Z.panelFront),        // +0.60
  // The band, expressed the same way: least and most recessed cap top.
  referenceCapTopBelowPanelFrontBand: {
    min: mm3(PROVISIONAL_Z.frontBoardTop
             - (BUTTON_CAP.referenceBoardToCapTop + BUTTON_CAP.referenceBoardToCapTopTolerance)
             - PROVISIONAL_Z.panelFront),                                   // +0.20
    max: mm3(PROVISIONAL_Z.frontBoardTop
             - (BUTTON_CAP.referenceBoardToCapTop - BUTTON_CAP.referenceBoardToCapTopTolerance)
             - PROVISIONAL_Z.panelFront),                                   // +1.00
  },
  // Omron's drawings want the cap top this far OVER the panel; our bought cap
  // would be 0.60 mm BEHIND it, so the reference is not met on this stack.
  referenceCapProudAbovePanel: BUTTON_CAP.capProudAbovePanel,
  suitsThisStack: false,
  suitabilityNote: "NOMINAL finding only: a bought B32 cap top would sit 0.60 mm "
    + "behind the 10.6 mm panel front, while Omron's drawings put it 1.0-2.0 mm "
    + "beyond the panel face. Panel suitability and the actual fit stay UNVERIFIED "
    + "(no bought cap measured)",
} as const


// B3F-4055 overall envelope above the board, from its datasheet (the same
// document whose SHA-256 is recorded for the switch): a square 3.8 +-0.1 mm
// projected plunger on a 12 +-0.2 mm body, 7.3 +-0.2 mm total. Needed to place
// the key-top body in 3D. The body and plunger squares are carried here as well,
// because the printed cap's locator and its no-skirt shape are derived from them.
// (The B32 cap top IS dimensioned: see BUTTON_CAP, where 10.0 +-0.4 runs PCB top
// -> cap top. What is NOT dimensioned is where that top sits on OUR stack, which
// no bought cap has measured.)
export const BUTTON_SWITCH = {
  part: "Omron B3F-4055",
  heightAboveBoard: 7.3,
  heightTolerance: 0.2,
  bodySquare: 12.0,
  bodySquareTolerance: 0.2,
  plungerSquare: 3.8,
  plungerSquareTolerance: 0.1,
  source: "Omron B3F datasheet, SHA-256 be9cf69e5f43fb7689448a2097c19858e5643c0e2e49cd77d018881543e27a1f",
} as const

// Panel z of the switch's plunger face - sourced, so it stays: the B3F-4055 is
// 7.3 +-0.2 mm above the board. The printed cap's top IS ours to set from the fit
// (BUTTON_CAP_PRINTED below): the cap is a real printed part and
// mechanical/printed/button_cap.py builds button-cap.stl/.step, with the
// printed-part tests re-deriving these numbers. `control_actuator` remains
// PENDING: a design budget is not a measurement, and the recess is nominal
// clearance rather than a snap. The vendor B3F model already carries its own
// actuator, so the switch still renders.
export const BUTTON_SWITCH_TOP_Z = mm3(PROVISIONAL_Z.frontBoardTop - BUTTON_SWITCH.heightAboveBoard) // 3.30

// The printed cap's plate stands this far PROUD of the panel front.
const BUTTON_CAP_PROUD_ABOVE_PANEL_FRONT = 0.8

// A press part only counts as reaching the panel if its top gets PAST the rear
// face - flush with the FRONT face at minimum. This is the test the B32 caps kept
// failing by eye, so it is computed here for the test to assert against.
export const BUTTON_REACH = {
  switchTopAboveBoard: BUTTON_SWITCH.heightAboveBoard,                                     // 7.30
  switchTopToPanelRear: mm3(JACK_CLAMPED_GAP - BUTTON_SWITCH.heightAboveBoard),             // 1.70
  switchTopToPanelFront: mm3(PANEL_STACK.boardToPanelFront - BUTTON_SWITCH.heightAboveBoard), // 3.30
  // BY DESIGN for the printed cap, which is now the STANDARD for SW1-SW7:
  // 11.40 above the board (panel front + 0.80) minus the 7.30 plunger face.
  // This is the cap TOP above the switch, not the part height: the locator
  // recess makes the part 4.70 tall (see BUTTON_CAP_PRINTED). A bought B32-13x0
  // is the documented alternative and is NOT measured, so this is not its height.
  capExtensionAboveSwitch: mm3(PANEL_STACK.boardToPanelFront - BUTTON_SWITCH.heightAboveBoard + BUTTON_CAP_PROUD_ABOVE_PANEL_FRONT), // 4.10
} as const

// The 12.25 mm square panel opening is both the B32 reference cutout's top of
// band and the printed cap's assembly clearance. It is declared once here so the
// panel aperture and BUTTON_CAP_PRINTED's plate size cannot drift apart.
const BUTTON_PANEL_CUTOUT_SQUARE = 12.25
const BUTTON_CAP_CLEARANCE_TO_CUTOUT_PER_SIDE = 0.225
const BUTTON_CAP_LOCATOR_DEPTH = 0.6
const BUTTON_CAP_LOCATOR_CLEARANCE_PER_SIDE = 0.025

// Owner option 2026-11-13, now the STANDARD: print our own caps instead of
// buying B32-13x0s. Printing makes only the NOMINAL geometry ours to choose - the
// top stops being a property of a bought part. It does NOT remove the measurement
// problem: the printed fit, retention, installed height and press travel all stay
// first-article measurements (`control_actuator` is PENDING). The numbers:
//
//   plate underside  = 7.30  the B3F-4055 plunger face (so the cap rests ON the
//                            plunger and presses it, which a bought cap does too)
//   plate top        = 11.40 panel front + 0.80 proud, so the finger meets cap
//   plate size       = 11.80 x 11.80, 0.225 per side inside the 12.25 cutout
//   locator          = 3.85 x 3.85 x 0.60 recess in the underside over the
//                      3.80 plunger, 0.025 per side
//   part height      = 3.30 flush + 0.60 locator + 0.80 proud = 4.70
//
// The part is IMPLEMENTED in mechanical/printed/button_cap.py (CadQuery),
// exported as button-cap.stl/.step and re-derived by
// mechanical/printed/test_printed_parts.py. It is still PROVISIONAL until the
// first article measures the fit: that measurement is what confirms
// `control_actuator`. The shape is forced by the cutout and not chosen - the
// 12.25 aperture is only 0.125 per side larger than the 12.0 +-0.2 switch body,
// so NO skirt can reach down past the plunger face: a printed cap is necessarily
// a plate on the plunger, guided by the aperture.
// The 3.85 mm recess on a 3.80 +-0.1 mm plunger is NOMINAL CLEARANCE, not a snap:
// no interference, no detent and no printed spring are designed in, FDM holes
// usually print undersize, and the switch's moving collar and body top are not
// dimensioned on the recorded page - so retention and install-ability are
// UNKNOWN until a first article measures them (acceptance item
// `control_actuator`).
export const BUTTON_CAP_PRINTED = {
  implemented: true,
  undersideZ: BUTTON_SWITCH_TOP_Z,                                   // 3.30 panel z = the plunger face
  proudAbovePanelFront: BUTTON_CAP_PROUD_ABOVE_PANEL_FRONT,
  topZ: mm3(PROVISIONAL_Z.panelFront - BUTTON_CAP_PROUD_ABOVE_PANEL_FRONT), // -0.80 panel z = 11.40 above board
  // The locator recess makes the PART taller than the distance it has to span:
  // the plunger sits INSIDE the recess, so the cap's rear face stops
  // locatorDepth behind the plunger face. Height is therefore flush + locator +
  // proud = 3.30 + 0.60 + 0.80 = 4.70, not flush + proud = 4.10. Getting this
  // wrong puts the cap 0.6 mm below the panel front - the identical mistake the
  // 3D body made twice, so it is derived here instead of typed.
  locatorFloorZ: BUTTON_SWITCH_TOP_Z,                                // 3.30, on the plunger face
  rearFaceZ: mm3(BUTTON_SWITCH_TOP_Z + BUTTON_CAP_LOCATOR_DEPTH),    // 3.90, locatorDepth behind it
  height: mm3(BUTTON_SWITCH_TOP_Z + BUTTON_CAP_LOCATOR_DEPTH - (PROVISIONAL_Z.panelFront - BUTTON_CAP_PROUD_ABOVE_PANEL_FRONT)), // 4.70
  square: mm3(BUTTON_PANEL_CUTOUT_SQUARE - 2 * BUTTON_CAP_CLEARANCE_TO_CUTOUT_PER_SIDE), // 11.80
  locatorSquare: mm3(BUTTON_SWITCH.plungerSquare + 2 * BUTTON_CAP_LOCATOR_CLEARANCE_PER_SIDE), // 3.85
  locatorDepth: BUTTON_CAP_LOCATOR_DEPTH,
  clearanceToCutoutPerSide: BUTTON_CAP_CLEARANCE_TO_CUTOUT_PER_SIDE, // 0.225
  material: "PLA/PETG print, solid; first article must be measured",
} as const

// ---- Panel openings (front FR-4 panel) ----
export const PANEL_CUTOUTS = {
  // B3F-4055 buttons take a 12 x 12 mm key top. The STANDARD is the printed cap
  // in BUTTON_CAP_PRINTED (11.80 square); a bought Omron/ARATAS B32-13x0 is the
  // documented alternative. Omron's reference panel cutout for the B32 is
  // 12.2 +0.05/-0 mm square (B32 datasheet en-b32.pdf, SHA-256 90834f79...,
  // page 3). 12.25 mm is the top of that band, so the largest B32 cap
  // (12 +0.15 = 12.15 mm) clears by 0.05 mm per side and the printed plate
  // clears by 0.225 mm per side. The 1.6 mm panel is inside Omron's 1.0-2.0 mm
  // panel range. The square is a routed cut: confirm JLCPCB's routed tolerance
  // before ordering. THE CAP DOES NOT MATCH Omron's reference stack: see
  // BUTTON_STACK below.
  // Opening side length, mm (square).
  button: BUTTON_PANEL_CUTOUT_SQUARE, // 12.25
  // 2.6 is the owner-approved PROVISIONAL mounting hole for the candidate
  // Signal Construct PGN25L165 (manufacturer drawing, SHA-256 5350ad1f...:
  // mounting hole Ø2.6 -0.05 mm, i.e. 2.55-2.60). It is NOT a fit approval and
  // it is NOT the head: the head is Ø3 +-0.05 mm and sits on the panel FRONT
  // face under the placement assumption below. The earlier 4.0 mm opening was
  // sized for the retired Bivar PLP2 family whose Ø3.3 mm flange falls through
  // it; that family is no longer an active candidate.
  //
  // FABRICATION IS NOT GUARANTEED. The drawing's hole tolerance is 2.55-2.60 mm,
  // and the JLCPCB sources conflict: standard NPTH +-0.05 vs +-0.08, while the
  // capabilities page gives +0.13/-0.08 for through holes with no NPTH split
  // (https://jlcpcb.com/capabilities/pcb-capabilities,
  // https://jlcpcb.com/blog/npth-design-guide). No band is chosen as guaranteed
  // here. The exporter also emits each circular cutout in BOTH
  // drill_npth.drl and Edge_Cuts.gbr, and CAM drill/routing de-duplication is
  // unverified: the JLC viewer and support must confirm both how the duplicate
  // is processed and whether 2.55-2.60 mm can be held. The ribs are visible on
  // the drawing; grip is unknown. Retention in 1.6 mm FR-4 and the LED coupling
  // are unverified.
  led: 2.6,
  control: 7.2, // PEC11R M7 x 0.75 bushing (7.0 mm thread) / SKRHABE010 nav actuator
  jack: 6.2,    // PJ-301M-3-B barrel
} as const

// Signal-style bold rings around the MIDI OUT barrels (inputs stay plain).
// Ring band 3.625…4.175: 0.5 mm web to the 6.2 mm barrel hole, 2.8 mm to the
// transport button cutouts above, 1.1 mm to the OUT labels below.
export const OUT_RING = { radius: 3.9, stroke: 0.55 } as const

// ---- Canonical panel apertures and drills ----
// The panel's openings, in the order `panel.circuit.tsx` emits them. Every
// value is DERIVED from the constants above; this block adds no coordinate.
// `panel.circuit.tsx` and `mechanicalExport()` both consume it, so the built
// panel and the STEP/STL mockups cannot drift apart. Element order is part of
// the contract: `lib/panel-cutouts.test.ts` compares this list with the built
// `dist/panel/circuit.json` element by element, and any change to the order or
// the geometry fails there.
export type PanelDrill = { name: string; x: number; y: number; diameter: number }
export type PanelAperture = {
  shape: "polygon" | "rect" | "circle"
  name: string
  x?: number
  y?: number
  width?: number
  height?: number
  radius?: number
  points?: readonly { x: number; y: number }[]
  drills?: readonly PanelDrill[]
}

const feedthroughDrills = (f: (typeof USB_FEEDTHROUGHS)[number], i: number): PanelDrill[] =>
  f.holeSpacing > 0
    ? [-1, 1].map(sx => ({
        name: `USB_${i}_${sx}`,
        x: f.x + sx * f.holeSpacing / 2,
        y: f.y,
        diameter: f.holeDiameter,
      }))
    : []

export const PANEL_APERTURES: readonly PanelAperture[] = [
  // 1. LCD glass aperture: rounded rectangle with explicit corner relief.
  { shape: "polygon", name: "LCD_APERTURE", points: roundedRectOutline(PANEL_LCD_CUTOUT) },
  // 2. The three USB panel feedthroughs, each with its two panel screw holes.
  ...USB_FEEDTHROUGHS.map((f, i) => ({
    shape: "rect" as const,
    name: `USB_FEEDTHROUGH_${i}`,
    x: f.x,
    y: f.y,
    width: f.cutoutWidth,
    height: f.cutoutHeight,
    drills: feedthroughDrills(f, i),
  })),
  // 3. Transport buttons, then PLAY START and SHIFT/EXIT.
  ...FRONT_COLUMNS.map(c => ({
    shape: "rect" as const, name: `${c.button} BUTTON`,
    x: c.x, y: TRANSPORT_Y, width: PANEL_CUTOUTS.button, height: PANEL_CUTOUTS.button,
  })),
  { shape: "rect", name: "PLAY START BUTTON", x: PLAY_START.x, y: TRANSPORT_Y, width: PANEL_CUTOUTS.button, height: PANEL_CUTOUTS.button },
  { shape: "rect", name: "SHIFT/EXIT BUTTON", x: RIGHT_CONTROLS.SHIFT_EXIT.x, y: RIGHT_CONTROLS.SHIFT_EXIT.y, width: PANEL_CUTOUTS.button, height: PANEL_CUTOUTS.button },
  // 4. LED light-pipe openings, 12 mm beside REC / OVER DUB / PLAY.
  ...FRONT_COLUMNS.filter(c => c.button in LED_ID).map(c => {
    const p = ledPosition(c.x)
    return {
      shape: "circle" as const,
      name: LED_ID[c.button as keyof typeof LED_ID],
      x: p.x, y: p.y, radius: PANEL_CUTOUTS.led / 2,
    }
  }),
  // 5. Encoder, POWER button, NAV switch: the right-hand control column.
  { shape: "circle", name: "VALUE ENCODER", x: RIGHT_CONTROLS.TEMPO.x, y: RIGHT_CONTROLS.TEMPO.y, radius: PANEL_CUTOUTS.control / 2 },
  { shape: "rect", name: "POWER BUTTON", x: POWER_BUTTON.x, y: POWER_BUTTON.y, width: PANEL_CUTOUTS.button, height: PANEL_CUTOUTS.button },
  { shape: "circle", name: "NAV", x: RIGHT_CONTROLS.NAV.x, y: RIGHT_CONTROLS.NAV.y, radius: PANEL_CUTOUTS.control / 2 },
  // 6. MIDI jack barrels in the four transport columns.
  ...FRONT_COLUMNS.map(c => ({
    shape: "circle" as const, name: `${c.jack} JACK`,
    // The jack's OWN x, not the transport column's: since 2026-11-13 the jacks
    // are one grouped block at the left and no longer share the button X.
    x: midiJackX(c.jack), y: JACK_Y, radius: PANEL_CUTOUTS.jack / 2,
  })),
]

// Panel-to-front-board standoffs and Eurorack rail holes. `panel.circuit.tsx`
// emits these after the silkscreen, so they are kept out of PANEL_APERTURES and
// the built element order stays unchanged.
// 2026-11-13 (owner): the four STAND_ holes are gone. They only ever existed to
// pass a screw into a panel standoff on the front board, and the board's mounting
// holes were removed in the same pass ("we will revisit them later"). The four
// RACK_ holes stay - those are the Doepfer/3U rail fixings, which the module
// needs to be mounted at all.
export const PANEL_MOUNT_HOLES: readonly PanelDrill[] = [
  ...RAIL_HOLES.map((h, i) => ({ name: `RACK_${i + 1}`, x: h.x, y: h.y, diameter: RAIL_HOLE_DIAMETER })),
]

// Every drilled hole in the panel, flat, for the CAD model: the feedthrough
// screw holes first, then the standoffs and the rail holes.
export const PANEL_DRILLS: readonly PanelDrill[] = [
  ...PANEL_APERTURES.flatMap(a => a.drills ?? []),
  ...PANEL_MOUNT_HOLES,
]

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
/** PLAY START label row, above its button (see PANEL_LABELS). */
export const PLAY_START_LABEL_Y = -25.5

// Owner rule 2026-11-13: a control legend sits ABOVE or BELOW its own opening,
// never beside it. The whole panel uses one letterform and one silk web to the
// opening edge it faces, so `labelAbove` is the only "above" number.
export const PANEL_LABEL_FONT_SIZE = 1.8 as const
const LABEL_HALF_HEIGHT = PANEL_LABEL_FONT_SIZE / 2
// Silk web between the label box and the opening edge it faces.
const LABEL_WEB = 0.8
/** Distance from an opening's CENTRE to the centre of a label ABOVE it. */
const labelAbove = (openingExtent: number) => mm3(openingExtent / 2 + LABEL_HALF_HEIGHT + LABEL_WEB)

const LABEL_OFFSET = {
  // ABOVE the USB cutouts. Sized off the TALLEST one (the 7.6 mm USB-C slot,
  // top edge 57.30) so a growing cutout cannot reach the row: 59.0. The rail
  // holes at (+-68.58, +-61.25) sit outside this row's x span (-58.8...+16.0).
  feedthrough: labelAbove(Math.max(...USB_FEEDTHROUGHS.map(f => f.cutoutHeight))),
  transport: -8,     // BELOW the button cutouts, above the jack cutouts
  jack: -6.5,        // BELOW the jack cutouts, clear of the OUT ring band
  encoder: 7,        // ABOVE the VALUE / NAV cutouts
  // ABOVE the 12.25 mm button cutouts: POWER 49 -> 56.825, SHIFT/EXIT -5 ->
  // 2.825. SHIFT/EXIT fits between its own cutout (top edge 1.125) and the NAV
  // cutout (bottom edge 8.40), i.e. inside that 2.5...7.0 band.
  button: labelAbove(PANEL_CUTOUTS.button),
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
  // Below the jack captions, not beside them: at the new 12 mm jack pitch the
  // note overlapped the "OUT 2" caption by 0.3 mm.
  { text: "MIDI TRS TYPE A", x: -44, y: -58.5, fontSize: 1.2 },
] as const
// Pi mounting panel silkscreen: identification and harness/orientation hints.
// The panel is viewed from the FRONT, the Pi hangs behind it, so the side
// labels name the Pi edges that sit behind the panel. Widths are checked
// against the board by lib/pi-panel.test.ts with SILK_ADVANCE_RATIO.
export const PI_PANEL_TITLES = [
  { text: "mnkybox PI MOUNT", x: 0, y: 56, fontSize: 2.0 },
  { text: "20HP / 3U - Pi 5 + M.2 HAT", x: 0, y: 52, fontSize: 1.2 },
] as const
export const PI_PANEL_LABELS: readonly PanelLabel[] = [
  { text: "GPIO RIBBON to J_PI", x: 0, y: 33 },
  { text: "USB-C + micro-HDMI", x: 0, y: -34 },
  { text: "USB-A x2 + RJ45", x: -33, y: 0 },
  { text: "microSD", x: 33, y: 0 },
  // Pi pin 1 sits 24.13 mm from the header centre (the 40-pin span / 2), on the
  // outer row just above the board's top edge.
  { text: "1", x: PI_PANEL_PI_MOUNT.x + 24.13, y: 29 },
] as const
export const panelTitleExtents = (t: { text: string; x: number; y: number; fontSize: number }) => {
  const w = t.text.length * t.fontSize * SILK_ADVANCE_RATIO
  return { x1: t.x - w / 2, y1: t.y - t.fontSize / 2, x2: t.x + w / 2, y2: t.y + t.fontSize / 2 }
}
export type PanelLabel = { text: string; x: number; y: number }
export const PANEL_LABELS: readonly PanelLabel[] = [
  ...USB_FEEDTHROUGHS.map(f => ({ text: FEEDTHROUGH_LABEL[f.id] ?? f.id, x: f.x, y: f.y + LABEL_OFFSET.feedthrough })),
  // The knob is the generic value encoder (tempo only as its home context), so
  // the legend names the control, not one of the things it can edit. "VALUE" at
  // the 1.8 mm label font is 6.21 mm wide in a 19 mm column: no in-bounds risk.
  { text: "VALUE", x: RIGHT_CONTROLS.TEMPO.x, y: RIGHT_CONTROLS.TEMPO.y + LABEL_OFFSET.encoder },
  { text: "NAV", x: RIGHT_CONTROLS.NAV.x, y: RIGHT_CONTROLS.NAV.y + LABEL_OFFSET.encoder },
  // The two right-column buttons wear their legend ABOVE the opening.
  { text: "SHIFT / EXIT", x: RIGHT_CONTROLS.SHIFT_EXIT.x, y: RIGHT_CONTROLS.SHIFT_EXIT.y + LABEL_OFFSET.button },
  { text: "POWER", x: POWER_BUTTON.x, y: POWER_BUTTON.y + LABEL_OFFSET.button },
  ...FRONT_COLUMNS.map(c => ({ text: c.button, x: c.x, y: TRANSPORT_Y + LABEL_OFFSET.transport })),
  // Owner rule 2026-11-13 keeps PLAY START's legend ABOVE its button (the four
  // transport buttons carry theirs below). At y -25.5 the 1.8 mm text keeps
  // 1.2 mm to the LCD window bottom (-23.435) and 1.4 mm to the button opening
  // top (-27.875).
  { text: PLAY_START.button, x: PLAY_START.x, y: PLAY_START_LABEL_Y },
  ...FRONT_COLUMNS.map(c => ({ text: JACK_LABEL[c.jack] ?? c.jack, x: midiJackX(c.jack), y: JACK_Y + LABEL_OFFSET.jack })),
]
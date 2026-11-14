import { readFileSync } from "node:fs"
import {
  DISPLAY_CONCEPT,
  FEEDTHROUGH_BRACKET,
  FRONT_BOARD_HOLES,
  FRONT_BOARD_OPENING,
  FRONT_BOARD_OPENING_POLYGON,
  FRONT_COLUMNS,
  FRONT_INTERCONNECT,
  FRONT_PCB,
  FRONT_REAR_MOUNT,
  JACK_Y,
  LCD_MOUNT_HOLES,
  LED_ID,
  LED_Y,
  ledPosition,
  M3_CLEARANCE_HOLE_DIAMETER,
  MIDI_CLUSTERS,
  midiJackX,
  OUT_RING,
  PANEL,
  PANEL_CUTOUTS,
  PANEL_LABELS,
  PANEL_LCD_CUTOUT,
  PANEL_MOUNT_POINTS,
  PANEL_STANDOFF,
  PANEL_TITLES,
  PLAY_START,
  POWER_BUTTON,
  POWER_HEADER,
  RAIL_HOLES,
  REAR_INTERCONNECT,
  RIGHT_CONTROLS,
  roundedRectOutline,
  TRANSPORT_Y,
  transportButtonX,
  USB_FEEDTHROUGHS,
} from "../lib/layout"
import { brandArtPaths } from "../lib/panel-art"

// Placement check: verifies the BUILT circuit JSON (dist/) against the shared
// coordinate source in lib/layout.ts. Run `tsci build` first (make placement).
// This is a design/output consistency check, not a physical fit approval.

const close = (a: number, b: number, tol = 1e-6) => Math.abs(a - b) <= tol
let pass = 0
let fail = 0
const ok = (label: string, good: boolean) => {
  if (good) { pass++; console.log(`PASS ${label}`) }
  else { fail++; console.log(`FAIL ${label}`) }
}
const at = (label: string, got: { x: number; y: number } | undefined, want: { x: number; y: number }) =>
  ok(label, !!got && close(got.x, want.x) && close(got.y, want.y))
// Circle/rect cutouts carry .center; normalise them for at().
const centre = (c: any) => (c ? { x: c.center.x, y: c.center.y } : undefined)

const read = (p: string) => JSON.parse(readFileSync(p, "utf8")) as any[]
const byType = (json: any[], type: string) => json.filter(e => e.type === type)

// ---- built-board clearance helpers (panel-mount frame) ----
type Pt = { x: number; y: number }
const segPointDist = (p: Pt, a: Pt, b: Pt) => {
  const dx = b.x - a.x, dy = b.y - a.y
  const len2 = dx * dx + dy * dy
  const t = len2 === 0 ? 0 : Math.max(0, Math.min(1, ((p.x - a.x) * dx + (p.y - a.y) * dy) / len2))
  return Math.hypot(p.x - (a.x + t * dx), p.y - (a.y + t * dy))
}
const boxGap = (p: Pt, cx: number, cy: number, w: number, h: number) => {
  const dx = Math.max(Math.abs(p.x - cx) - w / 2, 0)
  const dy = Math.max(Math.abs(p.y - cy) - h / 2, 0)
  return Math.hypot(dx, dy)
}
/** Distance from p (a hole CENTRE of radius `ownRadius`) to the nearest copper,
 * pad or other hole edge on a built board. `own` is the pcb_hole being measured,
 * so it does not measure itself. */
const nearestBuiltFeature = (json: any[], p: Pt, own: Pt | undefined, ownRadius: number) => {
  let best = { distance: Infinity, what: "nothing" }
  const take = (distance: number, what: string) => {
    const gap = distance - ownRadius
    if (gap < best.distance) best = { distance: gap, what }
  }
  for (const e of json) {
    if (e.type === "pcb_smtpad") {
      if (e.shape === "circle") take(Math.hypot(p.x - e.x, p.y - e.y) - e.radius, "SMD pad")
      else take(boxGap(p, e.x, e.y, e.width, e.height), "SMD pad")
    } else if (e.type === "pcb_plated_hole") {
      if (e.shape === "circle") take(Math.hypot(p.x - e.x, p.y - e.y) - (e.outer_diameter ?? 0) / 2, "plated hole")
      else take(boxGap(p, e.x, e.y, e.outer_width ?? 0, e.outer_height ?? 0), "plated hole")
    } else if (e.type === "pcb_via") {
      take(Math.hypot(p.x - e.x, p.y - e.y) - (e.outer_diameter ?? 0) / 2, "via")
    } else if (e.type === "pcb_hole") {
      if (own && Math.abs(e.x - own.x) < 1e-6 && Math.abs(e.y - own.y) < 1e-6) continue
      take(Math.hypot(p.x - e.x, p.y - e.y) - e.hole_diameter / 2, "hole")
    } else if (e.type === "pcb_trace") {
      const route: Pt[] = e.route ?? []
      const half = (e.width ?? 0) / 2
      for (let i = 0; i + 1 < route.length; i++) take(segPointDist(p, route[i]!, route[i + 1]!) - half, "trace")
    }
  }
  return best
}
/** Distance from p to the nearest board cutout boundary, negative inside. */
const nearestCutoutEdge = (json: any[], p: Pt) => {
  let best = Infinity
  for (const c of byType(json, "pcb_cutout")) {
    const pts: Pt[] = c.points ?? []
    if (pts.length < 2) continue
    let inside = false
    for (let i = 0, j = pts.length - 1; i < pts.length; j = i++) {
      const a = pts[i]!, b = pts[j]!
      if ((a.y > p.y) !== (b.y > p.y) && p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x) inside = !inside
    }
    const d = Math.min(...pts.map((q, i) => segPointDist(p, q, pts[(i + 1) % pts.length]!)))
    best = Math.min(best, inside ? -d : d)
  }
  return best
}

const panel = read("dist/panel/circuit.json")
const front = read("dist/front/circuit.json")
const rear = read("dist/index/circuit.json")

// ---- Panel openings ----
const cutouts = byType(panel, "pcb_cutout")
const circles = (radius: number) => cutouts.filter(c => c.shape === "circle" && close(c.radius, radius))
const squares = (side: number) => cutouts.filter(c => c.shape === "rect" && close(c.width, side) && close(c.height, side))

// LCD glass aperture. tscircuit has no rounded-rect cutout, so the panel emits
// a rounded-rect polygon (see lib/layout.ts); this check proves the BUILT
// outline matches the shared size, sits on the display centre and still leaves
// the documented minimum clearance to a square-corner module at its rounded
// corners - the case the old 89.2 x 56.3 mm rect could not survive.
const MIN_LCD_CLEARANCE = 0.5
const segDist = (p: { x: number; y: number }, a: { x: number; y: number }, b: { x: number; y: number }) => {
  const dx = b.x - a.x
  const dy = b.y - a.y
  const len2 = dx * dx + dy * dy
  const t = len2 === 0 ? 0 : Math.max(0, Math.min(1, ((p.x - a.x) * dx + (p.y - a.y) * dy) / len2))
  return Math.hypot(p.x - (a.x + t * dx), p.y - (a.y + t * dy))
}
const polyDist = (p: { x: number; y: number }, points: { x: number; y: number }[]) =>
  Math.min(...points.map((q, i) => segDist(p, q, points[(i + 1) % points.length]!)))

const lcd = cutouts.find(c => c.shape === "polygon")
const lcdPoints: { x: number; y: number }[] = lcd?.points ?? []
const lcdBox = lcdPoints.reduce(
  (a, p) => ({ x1: Math.min(a.x1, p.x), y1: Math.min(a.y1, p.y), x2: Math.max(a.x2, p.x), y2: Math.max(a.y2, p.y) }),
  { x1: Infinity, y1: Infinity, x2: -Infinity, y2: -Infinity },
)
ok(`panel: LCD aperture is the rounded-rect polygon (${roundedRectOutline(PANEL_LCD_CUTOUT).length} points)`,
  lcdPoints.length === roundedRectOutline(PANEL_LCD_CUTOUT).length)
ok(`panel: LCD aperture ${PANEL_LCD_CUTOUT.width} x ${PANEL_LCD_CUTOUT.height} (bbox)`,
  lcdPoints.length > 0 && close(lcdBox.x2 - lcdBox.x1, PANEL_LCD_CUTOUT.width, 1e-6) && close(lcdBox.y2 - lcdBox.y1, PANEL_LCD_CUTOUT.height, 1e-6))
ok(`panel: LCD aperture centred on the display (${PANEL_LCD_CUTOUT.x}, ${PANEL_LCD_CUTOUT.y})`,
  lcdPoints.length > 0 && close((lcdBox.x1 + lcdBox.x2) / 2, PANEL_LCD_CUTOUT.x, 1e-6) && close((lcdBox.y1 + lcdBox.y2) / 2, PANEL_LCD_CUTOUT.y, 1e-6))
ok("panel: LCD aperture inside the panel outline",
  lcdPoints.length > 0 && lcdBox.x1 > -PANEL.width / 2 && lcdBox.x2 < PANEL.width / 2 && lcdBox.y1 > -PANEL.height / 2 && lcdBox.y2 < PANEL.height / 2)
// The aperture is sized from the GLASS outline (owner decision D1), so the
// corner clearance is measured at the glass corners: those are the features
// that pass through the panel. The 106 x 68 PCB corners are behind the panel
// and clear the polygon by more, so they need no separate check.
for (const [sx, sy] of [[-1, -1], [-1, 1], [1, -1], [1, 1]] as const) {
  const corner = {
    x: DISPLAY_CONCEPT.x + DISPLAY_CONCEPT.glassWidth / 2 * sx,
    y: DISPLAY_CONCEPT.y + DISPLAY_CONCEPT.glassHeight / 2 * sy,
  }
  const d = lcdPoints.length > 0 ? polyDist(corner, lcdPoints) : 0
  ok(`panel: LCD corner clearance ${d.toFixed(3)} mm >= ${MIN_LCD_CLEARANCE} at glass corner (${corner.x}, ${corner.y})`, d >= MIN_LCD_CLEARANCE)
}

// USB feedthrough cutouts and mounting holes.
for (const f of USB_FEEDTHROUGHS) {
  const cu = cutouts.find(c => c.shape === "rect" && close(c.center.x, f.x) && close(c.center.y, f.y))
  ok(`panel: ${f.id} cutout at (${f.x}, ${f.y})`, !!cu && close(cu.width, f.cutoutWidth) && close(cu.height, f.cutoutHeight))
  for (const sx of [-1, 1]) {
    const hx = f.x + sx * f.holeSpacing / 2
    const h = byType(panel, "pcb_hole").find(h => close(h.x, hx) && close(h.y, f.y))
    at(`panel: ${f.id} mounting hole (Ø${f.holeDiameter} on ${f.holeSpacing})`, h, { x: hx, y: f.y })
    ok(`panel: ${f.id} mounting hole Ø`, !!h && close(h.hole_diameter, f.holeDiameter))
  }
}

// Transport button openings: four columns + PLAY START + SHIFT/EXIT.
const buttonOpenings = [
  ...FRONT_COLUMNS.map(c => ({ x: c.x, y: TRANSPORT_Y })),
  { x: PLAY_START.x, y: TRANSPORT_Y },
  { x: RIGHT_CONTROLS.SHIFT_EXIT.x, y: RIGHT_CONTROLS.SHIFT_EXIT.y },
  // Panel POWER button, bottom-right corner away from the transport row.
  { x: POWER_BUTTON.x, y: POWER_BUTTON.y },
]
for (const p of buttonOpenings) {
  at(`panel: ${PANEL_CUTOUTS.button} mm square button opening`, centre(squares(PANEL_CUTOUTS.button).find(c => close(c.center.x, p.x) && close(c.center.y, p.y))), p)
}
ok(`panel: ${buttonOpenings.length} button openings, no extras`, squares(PANEL_CUTOUTS.button).length === buttonOpenings.length)

// LED light-pipe openings BESIDE REC / OVER DUB / PLAY (fallback 3): the
// glass-derived window owns the row above the transport buttons.
const ledColumns = FRONT_COLUMNS.filter(c => c.button in LED_ID)
for (const c of ledColumns) {
  const p = ledPosition(c.x)
  at(`panel: light pipe beside ${c.button} (LED_ID ${LED_ID[c.button as keyof typeof LED_ID]})`,
    centre(circles(PANEL_CUTOUTS.led / 2).find(o => close(o.center.x, p.x) && close(o.center.y, p.y))), p)
}
ok("panel: 3 light-pipe openings, no extras", circles(PANEL_CUTOUTS.led / 2).length === ledColumns.length)

// Encoder opening in the right-hand control column. NAV has had its OWN
// aperture since 2026-11-14 (owner decision: the ALPS RKJXT1F42001 leaves its
// Ø7.20 stationary collar inside the opening), so the two diameters are checked
// separately and neither may borrow the other's.
at(`panel: value-encoder opening Ø${PANEL_CUTOUTS.control}`,
  centre(circles(PANEL_CUTOUTS.control / 2).find(o => close(o.center.x, RIGHT_CONTROLS.TEMPO.x) && close(o.center.y, RIGHT_CONTROLS.TEMPO.y))),
  RIGHT_CONTROLS.TEMPO)
ok("panel: 1 value-encoder opening, no extras", circles(PANEL_CUTOUTS.control / 2).length === 1)
at(`panel: NAV opening Ø${PANEL_CUTOUTS.nav}`,
  centre(circles(PANEL_CUTOUTS.nav / 2).find(o => close(o.center.x, RIGHT_CONTROLS.NAV.x) && close(o.center.y, RIGHT_CONTROLS.NAV.y))),
  RIGHT_CONTROLS.NAV)
ok("panel: 1 NAV opening, no extras", circles(PANEL_CUTOUTS.nav / 2).length === 1)
ok(`panel: NAV opening Ø${PANEL_CUTOUTS.nav} clears the Ø7.2 collar by ${(PANEL_CUTOUTS.nav - 7.2) / 2} mm per side`,
  PANEL_CUTOUTS.nav - 7.2 >= 0.6)

// MIDI jack barrels, in their own grouped block at the left of the bottom edge.
for (const c of FRONT_COLUMNS) {
  const x = midiJackX(c.jack)
  at(`panel: ${c.jack} barrel Ø${PANEL_CUTOUTS.jack}`,
    centre(circles(PANEL_CUTOUTS.jack / 2).find(o => close(o.center.x, x) && close(o.center.y, JACK_Y))),
    { x, y: JACK_Y })
  ok(`panel: ${c.jack} barrel is off the transport column`,
    !circles(PANEL_CUTOUTS.jack / 2).some(o => close(o.center.x, c.x) && close(o.center.y, JACK_Y)))
}
ok(`panel: ${FRONT_COLUMNS.length} jack openings, no extras`, circles(PANEL_CUTOUTS.jack / 2).length === FRONT_COLUMNS.length)

// Panel holes: the four RACK fixings, the six feedthrough screws and the four
// STAND_ drills of the panel-mount frame (PANEL_MOUNT_POINTS).
const panelHoles = byType(panel, "pcb_hole")
for (const h of RAIL_HOLES) {
  at("panel: rail hole", panelHoles.find(hh => close(hh.x, h.x) && close(hh.y, h.y)), h)
}
for (const p of PANEL_MOUNT_POINTS) {
  const h = panelHoles.find(hh => close(hh.x, p.x) && close(hh.y, p.y))
  at(`panel: panel-mount screw hole (${p.x}, ${p.y})`, h, p)
  ok(`panel: panel-mount screw hole Ø${PANEL_STANDOFF.holeDiameter}`,
    !!h && close(h.hole_diameter, PANEL_STANDOFF.holeDiameter))
}
ok(`panel: ${RAIL_HOLES.length + USB_FEEDTHROUGHS.length * 2 + PANEL_MOUNT_POINTS.length} rail/feedthrough/standoff holes, no extras`,
  panelHoles.length === RAIL_HOLES.length + USB_FEEDTHROUGHS.length * 2 + PANEL_MOUNT_POINTS.length)
for (const h of FRONT_BOARD_HOLES) {
  ok(`panel: no panel-standoff hole at (${h.x}, ${h.y})`,
    !panelHoles.some(hh => close(hh.x, h.x) && close(hh.y, h.y)))
}

// Titles consume the shared PANEL_TITLES with center anchoring only.
for (const t of PANEL_TITLES) {
  const hit = byType(panel, "pcb_silkscreen_text").find(s =>
    s.text === t.text && close(s.anchor_position.x, t.x, 1e-4) &&
    close(s.anchor_position.y, t.y, 1e-4) && close(s.font_size, t.fontSize, 1e-4) &&
    s.anchor_alignment === "center" && s.layer === "top")
  ok(`panel: title "${t.text}" centred at (${t.x}, ${t.y})`, !!hit)
}
// Signal-style OUT rings: bold circles on the MIDI OUT barrels only.
const rings = byType(panel, "pcb_silkscreen_circle")
const outCols = FRONT_COLUMNS.filter(c => c.jack.startsWith("MIDI OUT"))
for (const c of outCols) {
  const x = midiJackX(c.jack)
  const r = rings.find(o => close(o.center.x, x, 1e-4) && close(o.center.y, JACK_Y, 1e-4))
  ok(`panel: OUT ring over ${c.jack}`, !!r && close(r.radius, OUT_RING.radius, 1e-4))
}
ok("panel: rings on OUT barrels only", rings.length === outCols.length)
// Brand artwork: every emitted path accounted for and inside the outline.
const artPaths = byType(panel, "pcb_silkscreen_path")
const expectedArtPaths = brandArtPaths(0, -59).reduce((n, g) => n + g.paths.length, 0)
ok(`panel: ${expectedArtPaths} brand paths emitted`, artPaths.length === expectedArtPaths)
for (const p of artPaths) {
  const xs = p.route.map((q: { x: number }) => q.x)
  const ys = p.route.map((q: { y: number }) => q.y)
  const inside = Math.min(...xs) >= -PANEL.width / 2 + 0.5 && Math.max(...xs) <= PANEL.width / 2 - 0.5 &&
    Math.min(...ys) >= -PANEL.height / 2 + 0.5 && Math.max(...ys) <= PANEL.height / 2 - 0.5
  ok(`panel: brand path in-bounds (x ${Math.min(...xs).toFixed(1)}..${Math.max(...xs).toFixed(1)})`, inside)
}
// Silkscreen labels derive from the shared control positions.
const texts = byType(panel, "pcb_silkscreen_text")
for (const l of PANEL_LABELS) {
  const t = texts.find(t => t.text === l.text && close(t.anchor_position.x, l.x, 1e-4) && close(t.anchor_position.y, l.y, 1e-4))
  ok(`panel: label "${l.text}" at (${l.x}, ${l.y})`, !!t)
}

// ---- Front board placements ----
const src = new Map(byType(front, "source_component").map(s => [s.source_component_id, s]))
const comp = (name: string) => {
  const id = byType(front, "source_component").find(s => s.name === name)?.source_component_id
  const c = byType(front, "pcb_component").find(c => c.source_component_id === id)
  return c ? { x: c.footprint_origin.x, y: c.footprint_origin.y, rotation: c.rotation } : undefined
}

// Transport switches: SW1..SW4 in the four columns, SW5 = PLAY START on the
// right-hand control column, SW6 = SHIFT/EXIT.
const TRANSPORT_SWITCHES = [
  { sw: "SW1", button: "REC" },
  { sw: "SW2", button: "OVER DUB" },
  { sw: "SW3", button: "STOP" },
  { sw: "SW4", button: "PLAY" },
  { sw: "SW5", button: "PLAY START" },
] as const
for (const { sw, button } of TRANSPORT_SWITCHES) {
  at(`front: ${sw} (${button})`, comp(sw), { x: transportButtonX(button), y: TRANSPORT_Y })
}
at("front: SW6 (SHIFT/EXIT)", comp("SW6"), RIGHT_CONTROLS.SHIFT_EXIT)
at("front: U5 (NAV)", comp("U5"), RIGHT_CONTROLS.NAV)
at("front: U6 (value knob)", comp("U6"), RIGHT_CONTROLS.TEMPO)
at("front: SW7 (panel POWER)", comp("SW7"), POWER_BUTTON)
ok("front: SW7 rotated 90 deg to clear both corner holes", comp("SW7")?.rotation === POWER_BUTTON.rotation)
at("front: J_PWR (Pi power-button header)", comp("J_PWR"), POWER_HEADER)

// Jacks in the four shared columns; sleeve decoupling caps follow their jacks.
for (const [i, c] of FRONT_COLUMNS.entries()) {
  at(`front: ${c.jack} jack`, comp(`J${i + 1}`), { x: midiJackX(c.jack), y: JACK_Y })
}
for (const [i, jack] of ["MIDI IN 1", "MIDI IN 2"].entries()) {
  const at1 = MIDI_CLUSTERS[i === 0 ? "in1" : "in2"]
  at(`front: ${jack} sleeve decoupling cap`, comp(`C${i + 1}`), { x: at1.x, y: at1.y + 6 })
}

// LEDs beside REC / OVER DUB / PLAY with their 330R series resistors 4.5 mm
// above (a side-by-side resistor reaches into the neighbouring button).
const LED_PARTS = [
  { led: "D3", button: "REC", res: "R13" },
  { led: "D4", button: "OVER DUB", res: "R14" },
  { led: "D5", button: "PLAY", res: "R15" },
] as const
for (const p of LED_PARTS) {
  at(`front: ${p.led} beside ${p.button}`, comp(p.led), ledPosition(transportButtonX(p.button)))
  at(`front: ${p.res} series resistor`, comp(p.res), { x: ledPosition(transportButtonX(p.button)).x, y: LED_Y + 4.5 })
}

// Direct 2 x 13 interconnect: the female socket on this board and the male
// header on the rear board share one placement, so their pads coincide.
at("front: J5 female interconnect", comp("J5"), { x: FRONT_INTERCONNECT.x, y: FRONT_INTERCONNECT.y })
ok(`front: J5 rotated ${FRONT_INTERCONNECT.rotation} deg`, comp("J5")?.rotation === FRONT_INTERCONNECT.rotation)
// Single-sided JLCPCB assembly: every front SMT footprint on top. Two THT parts
// sit on the rear face: J5 (female interconnect socket) and J_PWR (the
// right-angle panel power header at the top notch).
const FRONT_REAR_THT = ["J5", "J_PWR"]
for (const c of byType(front, "pcb_component")) {
  const name = src.get(c.source_component_id)?.name ?? c.pcb_component_id
  const want = FRONT_REAR_THT.includes(name) ? "bottom" : "top"
  ok(`front: ${name} on ${want} side`, c.layer === want)
}

// Non-plated Ø2.7 holes on the FRONT board: only the shared front-rear grid.
// The panel standoffs, the LCD mount holes and the bracket mounts are NOT
// drilled there any more (2026-11-13); the data is retained in lib/layout.ts.
const holes = byType(front, "pcb_hole").filter(h => close(h.hole_diameter, 2.7))
for (const h of FRONT_REAR_MOUNT) {
  at(`front: front-rear mount hole at (${h.x}, ${h.y})`, holes.find(hh => close(hh.x, h.x) && close(hh.y, h.y)), h)
}
ok(`front: exactly ${FRONT_REAR_MOUNT.length} Ø2.7 holes (the front-rear grid)`, holes.length === FRONT_REAR_MOUNT.length)
for (const h of FRONT_BOARD_HOLES) {
  ok(`front: no panel-standoff hole at (${h.x}, ${h.y})`,
    !holes.some(hh => close(hh.x, h.x) && close(hh.y, h.y)))
}
// The panel-mount frame: four Ø${M3_CLEARANCE_HOLE_DIAMETER} holes, the SAME
// PANEL_MOUNT_POINTS the panel drills, and no other M3 clearance hole here.
const m3Holes = byType(front, "pcb_hole").filter(h => close(h.hole_diameter, M3_CLEARANCE_HOLE_DIAMETER))
for (const p of PANEL_MOUNT_POINTS) {
  const h = m3Holes.find(hh => close(hh.x, p.x) && close(hh.y, p.y))
  at(`front: panel-mount hole at (${p.x}, ${p.y})`, h, p)
}
ok(`front: exactly ${PANEL_MOUNT_POINTS.length} Ø${M3_CLEARANCE_HOLE_DIAMETER} holes (the panel-mount frame)`,
  m3Holes.length === PANEL_MOUNT_POINTS.length)
// The soft rule the positions were chosen against, measured on the BUILT board:
// every Ø3.2 hole edge keeps 0.8 mm to any copper (pad, via, trace), any other
// hole and the U cutout. The pour is not listed: the build already gives the
// hole a 0.2 mm anti-pad ring, so no copper reaches the bore.
for (const p of PANEL_MOUNT_POINTS) {
  const near = nearestBuiltFeature(front, p, p, M3_CLEARANCE_HOLE_DIAMETER / 2)
  ok(`front: panel-mount hole (${p.x}, ${p.y}) keeps ${near.distance.toFixed(3)} mm >= 0.8 to ${near.what}`,
    near.distance >= 0.8)
  const cut = nearestCutoutEdge(front, p) - M3_CLEARANCE_HOLE_DIAMETER / 2
  ok(`front: panel-mount hole (${p.x}, ${p.y}) keeps ${cut.toFixed(3)} mm >= 0.8 to the U opening`,
    cut >= 0.8)
}
for (const h of LCD_MOUNT_HOLES) {
  ok(`front: no LCD mount hole at (${h.x}, ${h.y}) (the module hangs from the rear carrier)`,
    !holes.some(hh => close(hh.x, h.x) && close(hh.y, h.y)))
}
for (const x of FEEDTHROUGH_BRACKET.mountX) {
  ok(`front: no bracket mount hole at (${x}, ${FEEDTHROUGH_BRACKET.mountY})`,
    !holes.some(hh => close(hh.x, x) && close(hh.y, FEEDTHROUGH_BRACKET.mountY)))
}
// The rear board carries the shared grid AND the four display-carrier holes
// (LCD_MOUNT_HOLES, added in index.circuit.tsx); it carries no Pi holes.
const rearHoles = byType(rear, "pcb_hole").filter(h => close(h.hole_diameter, 2.7))
for (const h of FRONT_REAR_MOUNT) {
  at(`rear: shared mount hole at (${h.x}, ${h.y})`, rearHoles.find(hh => close(hh.x, h.x) && close(hh.y, h.y)), h)
}
for (const h of LCD_MOUNT_HOLES) {
  at(`rear: display-carrier hole at (${h.x}, ${h.y})`, rearHoles.find(hh => close(hh.x, h.x) && close(hh.y, h.y)), h)
}
ok("rear: only the front-rear grid and the display carrier holes; no Pi holes",
  rearHoles.length === FRONT_REAR_MOUNT.length + LCD_MOUNT_HOLES.length)
// Bracket consistency against the BUILT front board: both mounts sit on board
// material outside the U opening and inside the bracket outline, and the
// bracket outline is backed by the board. Only a top spacer whose centre lies
// under the bar needs a head clearance.
const b = FEEDTHROUGH_BRACKET.outline
for (const x of FEEDTHROUGH_BRACKET.mountX) {
  ok(`front: bracket mount (${x}, ${FEEDTHROUGH_BRACKET.mountY}) on board material, outside the U opening`,
    (x < FRONT_BOARD_OPENING.left || x > FRONT_BOARD_OPENING.right) &&
    x > b.x1 && x < b.x2 && FEEDTHROUGH_BRACKET.mountY > b.y1 && FEEDTHROUGH_BRACKET.mountY < b.y2)
}
ok(`front: bracket outline ${b.width} x ${b.height} at (${b.x1}..${b.x2}, ${b.y1}..${b.y2}) inside the front board`,
  b.x1 > -FRONT_PCB.width / 2 && b.x2 < FRONT_PCB.width / 2 && b.y1 > -FRONT_PCB.height / 2 && b.y2 <= FRONT_PCB.height / 2)
for (const h of FRONT_REAR_MOUNT.filter(hh => hh.y > 0)) {
  const underBar = h.x >= b.x1 && h.x <= b.x2 && h.y >= b.y1 && h.y <= b.y2
  const cleared = FEEDTHROUGH_BRACKET.mountHeadClearance.some(c => close(c.x, h.x) && close(c.y, h.y))
  ok(`front: top spacer (${h.x}, ${h.y}) ${underBar ? "has a head clearance" : "is outside the bracket"}`,
    underBar ? cleared : !cleared)
}

// Rear-board placements: the male interconnect sits at the shared point on the
// front-facing (top) side, opposite the Pi. No other body may share that side.
const rearSrc = new Map(byType(rear, "source_component").map(s => [s.source_component_id, s]))
const rearComp = (name: string) => {
  const id = byType(rear, "source_component").find(s => s.name === name)?.source_component_id
  const c = byType(rear, "pcb_component").find(c => c.source_component_id === id)
  return c ? { x: c.footprint_origin.x, y: c.footprint_origin.y, rotation: c.rotation, layer: c.layer } : undefined
}
at("rear: J_FRONT male interconnect", rearComp("J_FRONT"), { x: REAR_INTERCONNECT.x, y: REAR_INTERCONNECT.y })
ok(`rear: J_FRONT rotated ${REAR_INTERCONNECT.rotation} deg`, rearComp("J_FRONT")?.rotation === REAR_INTERCONNECT.rotation)
ok("rear: J_FRONT is on the TOP (front-facing) side, opposite the Pi", rearComp("J_FRONT")?.layer === "top")
ok("rear: J_FRONT and front J5 share one placement",
  REAR_INTERCONNECT.x === FRONT_INTERCONNECT.x && REAR_INTERCONNECT.y === FRONT_INTERCONNECT.y &&
  REAR_INTERCONNECT.rotation === FRONT_INTERCONNECT.rotation)
const rearTopBodies = byType(rear, "pcb_component").filter(c => c.layer === "top")
  .map(c => rearSrc.get(c.source_component_id)?.name)
ok(`rear: ${rearTopBodies.length} body on the front-facing side (J_FRONT only)`,
  rearTopBodies.length === 1 && rearTopBodies[0] === "J_FRONT")
// The 26 pads must coincide: same X/Y for the same pin, on both built boards.
const padsOf = (json: any[], name: string) => {
  const id = byType(json, "source_component").find(s => s.name === name)?.source_component_id
  const pcb = byType(json, "pcb_component").find(c => c.source_component_id === id)
  const out = new Map<number, { x: number; y: number }>()
  for (const hole of byType(json, "pcb_plated_hole")) {
    if (hole.pcb_component_id !== pcb?.pcb_component_id) continue
    const hint = (hole.port_hints ?? []).find((h: string) => /^pin\d+$/.test(h))
    if (hint) out.set(Number(hint.slice(3)), { x: hole.x, y: hole.y })
  }
  return out
}
const j5Pads = padsOf(front, "J5")
const jfPads = padsOf(rear, "J_FRONT")
const misaligned = [...Array(26)].map((_, i) => {
  const a = j5Pads.get(i + 1), b = jfPads.get(i + 1)
  return a && b && close(a.x, b.x) && close(a.y, b.y) ? null : i + 1
}).filter(pin => pin !== null)
ok(`both boards: all 26 interconnect pads coincide (${misaligned.length} mismatched)`, misaligned.length === 0)

// The front board emits ONE polygon cutout: the U-shaped display opening. The
// old rectangular top relief notch is no longer cut.
const frontCutouts = byType(front, "pcb_cutout")
const opening = frontCutouts.filter(c => c.shape === "polygon")
ok("front: exactly one polygon cutout (the U-shaped display opening)", opening.length === 1)
ok("front: no rectangular top relief notch", frontCutouts.every(c => c.shape !== "rect"))
const openingPoints: { x: number; y: number }[] = opening[0]?.points ?? []
ok(`front: U opening has ${FRONT_BOARD_OPENING_POLYGON.length} points`,
  openingPoints.length === FRONT_BOARD_OPENING_POLYGON.length)
for (const p of FRONT_BOARD_OPENING_POLYGON) {
  ok(`front: U opening vertex (${p.x}, ${p.y})`,
    openingPoints.some(q => close(q.x, p.x) && close(q.y, p.y)))
}
const openingXs = openingPoints.map(p => p.x)
const openingYs = openingPoints.map(p => p.y)
ok(`front: U opening spans x ${FRONT_BOARD_OPENING.left}..${FRONT_BOARD_OPENING.right}`,
  openingXs.length > 0 && close(Math.min(...openingXs), FRONT_BOARD_OPENING.left) && close(Math.max(...openingXs), FRONT_BOARD_OPENING.right))
ok(`front: U opening breaks through the top edge (bottom ${FRONT_BOARD_OPENING.bottom})`,
  openingYs.length > 0 && close(Math.min(...openingYs), FRONT_BOARD_OPENING.bottom) && Math.max(...openingYs) > FRONT_PCB.height / 2)
// The five DISPLAY_ANNOTATIONS are data only; the display left the board, so no
// annotation may print on it.
for (const a of ["LCD 4.3in 106.00 x 68.00", "ACTIVE 95.04 x 53.86", "BEZEL 95.54 x 54.36",
                 "MOUNT 99.30 x 61.80", "DSI 15P"]) {
  ok(`front: no display annotation "${a}" printed on the board`,
    !byType(front, "pcb_silkscreen_text").some(t => t.text === a))
}

console.log(`\n${pass} pass, ${fail} fail (design/output consistency only, not physical fit)`)
if (fail > 0) process.exit(1)
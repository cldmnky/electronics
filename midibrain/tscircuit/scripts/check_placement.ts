import { readFileSync } from "node:fs"
import {
  DISPLAY_ANNOTATIONS,
  DISPLAY_CONCEPT,
  FEEDTHROUGH_BRACKET,
  FRONT_BOARD_HOLES,
  FRONT_COLUMNS,
  FRONT_INTERCONNECT,
  FRONT_REAR_MOUNT,
  FRONT_TOP_NOTCH,
  JACK_Y,
  LCD_MOUNT_HOLES,
  LED_ID,
  LED_Y,
  ledPosition,
  midiJackX,
  PANEL_LCD_CUTOUT,
  PANEL_CUTOUTS,
  PI_HOLES,
  OUT_RING,
  PANEL,
  PANEL_LABELS,
  PANEL_TITLES,
  PLAY_START,
  POWER_BUTTON,
  POWER_HEADER,
  RAIL_HOLES,
  RIGHT_CONTROLS,
  TRANSPORT_Y,
  transportButtonX,
  USB_FEEDTHROUGHS,
  displayAnnotationXY,
  roundedRectOutline,
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

const panel = read("dist/panel/circuit.json")
const front = read("dist/front/circuit.json")
const rear = read("dist/index/circuit.json")

// ---- Panel openings ----
const cutouts = byType(panel, "pcb_cutout")
const circles = (radius: number) => cutouts.filter(c => c.shape === "circle" && close(c.radius, radius))

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
  at(`panel: button opening Ø${PANEL_CUTOUTS.button}`, centre(circles(PANEL_CUTOUTS.button / 2).find(c => close(c.center.x, p.x) && close(c.center.y, p.y))), p)
}
ok(`panel: ${buttonOpenings.length} button openings, no extras`, circles(PANEL_CUTOUTS.button / 2).length === buttonOpenings.length)

// LED light-pipe openings BESIDE REC / OVER DUB / PLAY (fallback 3): the
// glass-derived window owns the row above the transport buttons.
const ledColumns = FRONT_COLUMNS.filter(c => c.button in LED_ID)
for (const c of ledColumns) {
  const p = ledPosition(c.x)
  at(`panel: light pipe beside ${c.button} (LED_ID ${LED_ID[c.button as keyof typeof LED_ID]})`,
    centre(circles(PANEL_CUTOUTS.led / 2).find(o => close(o.center.x, p.x) && close(o.center.y, p.y))), p)
}
ok("panel: 3 light-pipe openings, no extras", circles(PANEL_CUTOUTS.led / 2).length === ledColumns.length)

// Encoder / nav openings in the right-hand control column.
for (const c of [RIGHT_CONTROLS.TEMPO, RIGHT_CONTROLS.NAV]) {
  at(`panel: control opening Ø${PANEL_CUTOUTS.control}`, centre(circles(PANEL_CUTOUTS.control / 2).find(o => close(o.center.x, c.x) && close(o.center.y, c.y))), c)
}
ok("panel: 2 control openings, no extras", circles(PANEL_CUTOUTS.control / 2).length === 2)

// MIDI jack barrels.
for (const c of FRONT_COLUMNS) {
  at(`panel: ${c.jack} barrel Ø${PANEL_CUTOUTS.jack}`, centre(circles(PANEL_CUTOUTS.jack / 2).find(o => close(o.center.x, c.x) && close(o.center.y, JACK_Y))), { x: c.x, y: JACK_Y })
}
ok(`panel: ${FRONT_COLUMNS.length} jack openings, no extras`, circles(PANEL_CUTOUTS.jack / 2).length === FRONT_COLUMNS.length)

// Standoff and rail holes.
const panelHoles = byType(panel, "pcb_hole")
for (const h of [...FRONT_BOARD_HOLES, ...RAIL_HOLES]) {
  at("panel: standoff/rail hole", panelHoles.find(hh => close(hh.x, h.x) && close(hh.y, h.y)), h)
}
ok(`panel: ${FRONT_BOARD_HOLES.length + RAIL_HOLES.length} standoff/rail holes, no extras`,
  panelHoles.length === FRONT_BOARD_HOLES.length + RAIL_HOLES.length + USB_FEEDTHROUGHS.length * 2)

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
  const r = rings.find(o => close(o.center.x, c.x, 1e-4) && close(o.center.y, JACK_Y, 1e-4))
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
  at(`front: ${jack} sleeve decoupling cap`, comp(`C${i + 1}`), { x: midiJackX(jack) - 7, y: JACK_Y + 5.5 })
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

// Interconnect header in the corridor between display and control column.
at("front: J5 interconnect", comp("J5"), { x: FRONT_INTERCONNECT.x, y: FRONT_INTERCONNECT.y })
ok(`front: J5 rotated ${FRONT_INTERCONNECT.rotation} deg`, comp("J5")?.rotation === FRONT_INTERCONNECT.rotation)
// Single-sided JLCPCB assembly: every front footprint on top, except J5
// (THT body mating rearward into the front-rear ribbon).
for (const c of byType(front, "pcb_component")) {
  const name = src.get(c.source_component_id)?.name ?? c.pcb_component_id
  const want = name === "J5" ? "bottom" : "top"
  ok(`front: ${name} on ${want} side`, c.layer === want)
}

// Non-plated Ø2.7 holes: panel standoffs, shared front-rear grid, LCD
// standoffs, bracket mounts.
const holes = byType(front, "pcb_hole").filter(h => close(h.hole_diameter, 2.7))
for (const h of [...FRONT_BOARD_HOLES, ...FRONT_REAR_MOUNT, ...LCD_MOUNT_HOLES]) {
  at(`front: Ø2.7 hole at (${h.x}, ${h.y})`, holes.find(hh => close(hh.x, h.x) && close(hh.y, h.y)), h)
}
// Rear board carries the same shared grid (its only mechanical home).
const rearHoles = byType(rear, "pcb_hole").filter(h => close(h.hole_diameter, 2.7))
for (const h of FRONT_REAR_MOUNT) {
  at(`rear: shared mount hole at (${h.x}, ${h.y})`, rearHoles.find(hh => close(hh.x, h.x) && close(hh.y, h.y)), h)
}
// Pi holes follow the portrait-shifted grid from the shared source.
for (const h of PI_HOLES) {
  at(`rear: Pi hole at (${h.x}, ${h.y})`, rearHoles.find(hh => close(hh.x, h.x) && close(hh.y, h.y)), h)
}
for (const x of FEEDTHROUGH_BRACKET.mountX) {
  at("front: bracket mount hole", holes.find(hh => close(hh.x, x) && close(hh.y, FEEDTHROUGH_BRACKET.mountY)), { x, y: FEEDTHROUGH_BRACKET.mountY })
}
// Bracket consistency against the BUILT front board: both mounts sit on board
// material outside the top relief notch and inside the bracket outline, and
// the bracket outline is fully backed by the board outline. The top
// front-to-rear spacer holes then necessarily fall inside the bracket, which
// is why the printed part carries the head-clearance slots from lib/layout.ts.
const notchLeft = FRONT_TOP_NOTCH.x - FRONT_TOP_NOTCH.width / 2
const notchRight = FRONT_TOP_NOTCH.x + FRONT_TOP_NOTCH.width / 2
const notchBottom = FRONT_TOP_NOTCH.y - FRONT_TOP_NOTCH.height / 2
const b = FEEDTHROUGH_BRACKET.outline
for (const x of FEEDTHROUGH_BRACKET.mountX) {
  const onBoard = holes.some(hh => close(hh.x, x) && close(hh.y, FEEDTHROUGH_BRACKET.mountY))
  ok(`front: bracket mount (${x}, ${FEEDTHROUGH_BRACKET.mountY}) on board, outside the notch`,
    onBoard && (x < notchLeft || x > notchRight) && FEEDTHROUGH_BRACKET.mountY < notchBottom &&
    x > b.x1 && x < b.x2 && FEEDTHROUGH_BRACKET.mountY > b.y1 && FEEDTHROUGH_BRACKET.mountY < b.y2)
}
ok(`front: bracket outline ${b.width} x ${b.height} at (${b.x1}..${b.x2}, ${b.y1}..${b.y2}) inside the front board`,
  b.x1 > -128 / 2 && b.x2 < 128 / 2 && b.y1 > -112 / 2 && b.y2 <= 112 / 2)
for (const h of FRONT_REAR_MOUNT.filter(hh => hh.y > 0)) {
  ok(`front: top spacer (${h.x}, ${h.y}) inside the bracket envelope, head clearance required`,
    h.x > b.x1 && h.x < b.x2 && h.y > b.y1 && h.y < b.y2)
}

// Top relief notch and display annotations.
const notch = byType(front, "pcb_cutout").find(c => c.shape === "rect")
ok("front: top relief notch",
  !!notch && close(notch.center.x, FRONT_TOP_NOTCH.x) && close(notch.center.y, FRONT_TOP_NOTCH.y) &&
  close(notch.width, FRONT_TOP_NOTCH.width) && close(notch.height, FRONT_TOP_NOTCH.height))
for (const a of DISPLAY_ANNOTATIONS) {
  const p = displayAnnotationXY(a)
  const t = byType(front, "pcb_silkscreen_text").find(t => t.text === a.text && t.layer === "top")
  ok(`front: display annotation "${a.text}" at (${p.x}, ${p.y})`,
    !!t && close(t.anchor_position.x, p.x, 1e-4) && close(t.anchor_position.y, p.y, 1e-4))
}

console.log(`\n${pass} pass, ${fail} fail (design/output consistency only, not physical fit)`)
if (fail > 0) process.exit(1)
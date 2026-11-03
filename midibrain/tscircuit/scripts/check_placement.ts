import { readFileSync } from "node:fs"
import {
  DISPLAY_ANNOTATIONS,
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
  midiJackX,
  PANEL_LCD_CUTOUT,
  PANEL_CUTOUTS,
  PI_HOLES,
  PANEL_LABELS,
  PLAY_START,
  RAIL_HOLES,
  RIGHT_CONTROLS,
  TRANSPORT_Y,
  transportButtonX,
  USB_FEEDTHROUGHS,
  displayAnnotationXY,
} from "../lib/layout"

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

// LCD glass cutout, centred on the display with the shared size.
const lcd = cutouts.find(c => c.shape === "rect" && close(c.center.x, PANEL_LCD_CUTOUT.x) && close(c.center.y, PANEL_LCD_CUTOUT.y))
ok(`panel: LCD cutout ${PANEL_LCD_CUTOUT.width} x ${PANEL_LCD_CUTOUT.height} at (${PANEL_LCD_CUTOUT.x}, ${PANEL_LCD_CUTOUT.y})`,
  !!lcd && close(lcd.width, PANEL_LCD_CUTOUT.width) && close(lcd.height, PANEL_LCD_CUTOUT.height))

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
]
for (const p of buttonOpenings) {
  at(`panel: button opening Ø${PANEL_CUTOUTS.button}`, centre(circles(PANEL_CUTOUTS.button / 2).find(c => close(c.center.x, p.x) && close(c.center.y, p.y))), p)
}
ok(`panel: ${buttonOpenings.length} button openings, no extras`, circles(PANEL_CUTOUTS.button / 2).length === buttonOpenings.length)

// LED light-pipe openings above REC / OVER DUB / PLAY.
const ledColumns = FRONT_COLUMNS.filter(c => c.button in LED_ID)
for (const c of ledColumns) {
  at(`panel: light pipe over ${c.button} (LED_ID ${LED_ID[c.button as keyof typeof LED_ID]})`,
    centre(circles(PANEL_CUTOUTS.led / 2).find(o => close(o.center.x, c.x) && close(o.center.y, LED_Y))), { x: c.x, y: LED_Y })
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
at("front: U6 (TEMPO)", comp("U6"), RIGHT_CONTROLS.TEMPO)

// Jacks in the four shared columns; sleeve decoupling caps follow their jacks.
for (const [i, c] of FRONT_COLUMNS.entries()) {
  at(`front: ${c.jack} jack`, comp(`J${i + 1}`), { x: midiJackX(c.jack), y: JACK_Y })
}
for (const [i, jack] of ["MIDI IN 1", "MIDI IN 2"].entries()) {
  at(`front: ${jack} sleeve decoupling cap`, comp(`C${i + 1}`), { x: midiJackX(jack) - 6, y: JACK_Y + 5.5 })
}

// LEDs above REC / OVER DUB / PLAY with their 330R series resistors 4 mm left.
const LED_PARTS = [
  { led: "D3", button: "REC", res: "R13" },
  { led: "D4", button: "OVER DUB", res: "R14" },
  { led: "D5", button: "PLAY", res: "R15" },
] as const
for (const p of LED_PARTS) {
  at(`front: ${p.led} above ${p.button}`, comp(p.led), { x: transportButtonX(p.button), y: LED_Y })
  at(`front: ${p.res} series resistor`, comp(p.res), { x: transportButtonX(p.button) - 4, y: LED_Y })
}

// Interconnect header in the corridor between display and control column.
at("front: J5 interconnect", comp("J5"), { x: FRONT_INTERCONNECT.x, y: FRONT_INTERCONNECT.y })
ok(`front: J5 rotated ${FRONT_INTERCONNECT.rotation} deg`, comp("J5")?.rotation === FRONT_INTERCONNECT.rotation)

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
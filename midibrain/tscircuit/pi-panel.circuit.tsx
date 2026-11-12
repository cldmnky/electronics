import { Fragment } from "react"
import {
  PI_PANEL,
  PI_PANEL_LABELS,
  PI_PANEL_PI_BOX,
  PI_PANEL_PI_HOLES,
  PI_PANEL_PI_MOUNT,
  PI_PANEL_RAIL_HOLES,
  PI_PANEL_TITLES,
  RAIL_HOLE_DIAMETER,
} from "./lib/layout"

// 20HP x 3U FR-4 Pi mounting panel (owner decision 2026-11-12). Fabricate as
// a bare board, exactly like the 30HP panel:
//   FR4 1.6 mm, 2 layers, NO solder mask, white silkscreen, ENIG or HASL.
// No copper, no components. The Pi 5 (with Active Cooler + M.2 HAT+ + SSD)
// bolts to the panel's REAR face on standoffs and hangs behind it in its own
// rack position; the main module (30HP panel + front/rear boards) then fits a
// Behringer Eurorack Go row. See docs/mechanical-stack.md.
//
// Every hole and every label imports from lib/layout.ts (PI_PANEL_*): one
// definition, consumed here and by mechanicalExport(). The holes are:
//   4 x M2.5 Pi mounting holes (58 x 49 grid, landscape, centred),
//   4 x Ø3.2 Eurorack rail holes (Doepfer 3U Y, 17 x 5.08 X spacing).
// The Pi outline on the silkscreen is the conservative 85.6 x 56.5 envelope,
// not the drawing's 85 x 56: same source and same bound as PI_PCB_BOX.
//
// EMISSION ORDER is part of the contract (like the 30HP panel): the hole
// group order is Pi mount holes then rail holes, and lib/pi-panel.test.ts
// compares the built dist/pi-panel/circuit.json against PI_PANEL_PI_HOLES /
// PI_PANEL_RAIL_HOLES element by element. Do not reorder or regroup.

export default () => (
  <board width={`${PI_PANEL.width}mm`} height={`${PI_PANEL.height}mm`} thickness={`${PI_PANEL.thickness}mm`} layers={2}>
    {/* Pi 5 mounting holes (M2.5 clearance), then the Eurorack rail holes. */}
    {PI_PANEL_PI_HOLES.map((h, i) => (
      <Fragment key={`pi-hole-${i}`}><hole name={`PI_H${i + 1}`} diameter={`${PI_PANEL_PI_MOUNT.holeDiameter}mm`} pcbX={`${h.x}mm`} pcbY={`${h.y}mm`} /></Fragment>
    ))}
    {PI_PANEL_RAIL_HOLES.map((h, i) => (
      <Fragment key={`rail-hole-${i}`}><hole name={`RACK_${i + 1}`} diameter={`${RAIL_HOLE_DIAMETER}mm`} pcbX={`${h.x}mm`} pcbY={`${h.y}mm`} /></Fragment>
    ))}
    {/* Pi envelope: which way round the Pi mounts (GPIO edge up, USB-C/HDMI
        left, USB/RJ45 right, DSI/PCIe FPC down). */}
    <silkscreenpath
      strokeWidth="0.15mm"
      route={[
        { x: PI_PANEL_PI_BOX.x1, y: PI_PANEL_PI_BOX.y1 },
        { x: PI_PANEL_PI_BOX.x2, y: PI_PANEL_PI_BOX.y1 },
        { x: PI_PANEL_PI_BOX.x2, y: PI_PANEL_PI_BOX.y2 },
        { x: PI_PANEL_PI_BOX.x1, y: PI_PANEL_PI_BOX.y2 },
        { x: PI_PANEL_PI_BOX.x1, y: PI_PANEL_PI_BOX.y1 },
      ]}
    />
    {PI_PANEL_TITLES.map((t, i) => (
      <Fragment key={`title-${i}`}><silkscreentext text={t.text} pcbX={`${t.x}mm`} pcbY={`${t.y}mm`} fontSize={`${t.fontSize}mm`} anchorAlignment="center" layer="top" /></Fragment>
    ))}
    {PI_PANEL_LABELS.map((l, i) => (
      <Fragment key={`label-${i}`}><silkscreentext text={l.text} pcbX={`${l.x}mm`} pcbY={`${l.y}mm`} fontSize="1.6mm" anchorAlignment="center" layer="top" /></Fragment>
    ))}
  </board>
)

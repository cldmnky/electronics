// Froemix euro board (FRONT board, 20HP) - jacks, pots, MIDI, display and the
// sockets that mate with the 16HP base board.
//
// Base board: froemix/tscircuit/ (electrical source of truth, 4 layers).
// This board: mechanical + panel I/O, 2 layers, all through-hole, hand-soldered.
// Stack-up, panel floorplan and open questions: froemix/doc/EURO-BOARD.md.
//
// Coordinates come from lib/layout.ts, which panel.circuit.tsx also imports, so
// every panel hole is above its part by construction.
//
// Sides: TOP faces the panel (jacks, pots, OLED, LED). BOTTOM faces the base
// board (the three 2.00mm sockets). Nothing tall on the bottom but the sockets.

import { Fragment } from "react"
import { BOARD_H, BOARD_W, BUTTON, STANDOFF_HOLES } from "./lib/layout"
import { BOARD_REVISION, buildDate } from "./lib/build-info"
import { Nets } from "./lib/nets"
import { MatingSockets } from "./sections/mating"
import { JacksSection } from "./sections/jacks"
import { PotsSection } from "./sections/pots"
import { JoystickSection } from "./sections/joystick"
import { ButtonSection } from "./sections/button"
import { JackLedsSection } from "./sections/jack-leds"
import { DisplaySection } from "./sections/display"

export default () => (
  <board
    width={`${BOARD_W}mm`}
    height={`${BOARD_H}mm`}
    layers={2}
    autorouter="auto-local"
    defaultTraceWidth="0.25mm"
    minTraceWidth="0.25mm"
    minTraceToPadEdgeClearance="0.1mm"
    minViaHoleDiameter="0.2mm"
    minViaPadDiameter="0.45mm"
    minBoardEdgeClearance="0.4mm"
    routeRemaining={false}
  >
    <Nets />
    {/* one global phase: this board is a fan-out from two sockets and does not
        need staged routing */}
    <autoroutingphase phaseIndex={0} />

    {/* M3 standoffs to the base board, at the base board's own hole positions */}
    {STANDOFF_HOLES.map((h, i) => (
      <Fragment key={`standoff-${i}`}><hole diameter="3.2mm" pcbX={`${h.x}mm`} pcbY={`${h.y}mm`} /></Fragment>
    ))}

    <MatingSockets />
    <JacksSection />
    <PotsSection />
    <JoystickSection />
    <ButtonSection />
    <JackLedsSection />
    <DisplaySection />

    {/* Board identity on the rear, clear of sockets and mounting holes. */}
    <silkscreentext text={`FROEMIX EURO REV ${BOARD_REVISION}`} layer="bottom"
      anchorAlignment="center" fontSize="1.7mm" pcbX="0mm" pcbY="-58mm" />
    <silkscreentext text={buildDate()} layer="bottom" anchorAlignment="center"
      fontSize="1.7mm" pcbX="0mm" pcbY="-60mm" />

    {/* GND plane on the bottom, same convention as the base board */}
    <copperpour
      connectsTo="net.GND"
      layer="bottom"
      clearance="0.3mm"
      boardEdgeMargin="0.5mm"
    />
  </board>
)

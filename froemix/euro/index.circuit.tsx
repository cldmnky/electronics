// Froemix euro board (FRONT board, 20HP) - jacks, pots, MIDI, display and the
// sockets that mate with the 16HP base board.
//
// Base board: froemix/tscircuit/ (electrical source of truth, 4 layers).
// This board: mechanical + panel I/O, 2 layers. 27 through-hole parts are hand
// soldered; 12 SMT parts (10x 0603, 2x SOD-323) are assembled by JLCPCB.
// Stack-up, panel floorplan and open questions: froemix/doc/EURO-BOARD.md.
//
// Coordinates come from lib/layout.ts, which panel.circuit.tsx also imports, so
// every panel hole is above its part by construction.
//
// Sides: TOP faces the panel (jacks, pots, OLED, LED). BOTTOM faces the base
// board (the three 2.00mm sockets). Nothing tall on the bottom but the sockets.

import { Fragment } from "react"
import { BOARD_H, BOARD_W, BUTTON, JACKS, PANEL_MOUNT_HOLES, STANDOFF_HOLES } from "./lib/layout"
import { BOARD_REVISION, buildDate } from "./lib/build-info"
import { Nets } from "./lib/nets"
import { MatingSockets } from "./sections/mating"
import { JacksSection } from "./sections/jacks"
import { PotsSection } from "./sections/pots"
import { JoystickSection } from "./sections/joystick"
import { ButtonSection } from "./sections/button"
import { JackLedsSection } from "./sections/jack-leds"
import { DisplaySection } from "./sections/display"
import { SilkSection } from "./sections/silk"

export default () => (
  <board
    width={`${BOARD_W}mm`}
    height={`${BOARD_H}mm`}
    thickness="1.6mm"
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
      <hole key={`standoff-${i}`} diameter="3.2mm" pcbX={`${h.x}mm`} pcbY={`${h.y}mm`} />
    ))}

    {/* Copper clearance for the OLED / panel support hardware on both faces.
        3mm radius also compensates for the pour's polygon chords. The holes
        themselves belong to the OLED footprint; the display may overlap. */}
    {PANEL_MOUNT_HOLES.map((h, i) => (
      <Fragment key={`oled-support-clearance-${i}`}>
        <keepout shape="circle" radius="3mm" pcbX={`${h.x}mm`} pcbY={`${h.y}mm`}
          layers={["top", "bottom"]} allowPlacements />
      </Fragment>
    ))}

    {/* Thonk's drawing calls for a 3mm barrel precaution. A 1.8mm radius
        keeps the 3mm disk clear even where the pour approximates a circle
        with chords. No extra hole. Each jack body may cover its own keepout. */}
    {JACKS.map((j) => (
      <Fragment key={`barrel-${j.name}`}>
        <keepout shape="circle" radius="1.8mm" pcbX={`${j.x}mm`} pcbY={`${j.y}mm`}
          layers={["top", "bottom"]} allowPlacements />
      </Fragment>
    ))}

    <MatingSockets />
    <JacksSection />
    <PotsSection />
    <JoystickSection />
    <ButtonSection />
    <JackLedsSection />
    <DisplaySection />
    <SilkSection />

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

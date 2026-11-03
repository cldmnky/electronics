import { Fragment } from "react"
import {
  FRONT_BOARD_HOLES,
  FRONT_COLUMNS,
  JACK_Y,
  LED_ID,
  LED_Y,
  MOUNT_HOLE_DIAMETER,
  PANEL,
  PANEL_CUTOUTS,
  PANEL_LABELS,
  PANEL_LCD_CUTOUT,
  PLAY_START,
  RAIL_HOLES,
  RAIL_HOLE_DIAMETER,
  RIGHT_CONTROLS,
  TRANSPORT_Y,
  USB_FEEDTHROUGHS,
} from "./lib/layout"

// 26HP x 3U FR-4 front panel. Fabricate as a bare board:
//   FR4 1.6 mm, 2 layers, NO solder mask, white silkscreen, ENIG or HASL.
// No copper, no components. LED openings take 4.0 mm light pipes (Bivar
// PLP2-400-C); the SMD LEDs remain on the front board behind them.
// Every opening and label position imports from lib/layout.ts.

// Transport columns plus PLAY START and SHIFT/EXIT take button openings.
const buttonCutouts = [
  ...FRONT_COLUMNS.map(c => ({ x: c.x, y: TRANSPORT_Y })),
  { x: PLAY_START.x, y: TRANSPORT_Y },
  { x: RIGHT_CONTROLS.SHIFT_EXIT.x, y: RIGHT_CONTROLS.SHIFT_EXIT.y },
]
// LED light-pipe openings sit 9 mm above their transport buttons.
const ledCutouts = FRONT_COLUMNS.filter(c => c.button in LED_ID).map(c => ({ x: c.x, y: LED_Y }))
// MIDI jack barrels in the four transport columns.
const jackCutouts = FRONT_COLUMNS.map(c => ({ x: c.x, y: JACK_Y }))

export default () => (
  <board width={`${PANEL.width}mm`} height={`${PANEL.height}mm`} thickness="1.6mm" layers={2}>
    {/* LCD glass cutout */}
    <cutout shape="rect" width={`${PANEL_LCD_CUTOUT.width}mm`} height={`${PANEL_LCD_CUTOUT.height}mm`} pcbX={`${PANEL_LCD_CUTOUT.x}mm`} pcbY={`${PANEL_LCD_CUTOUT.y}mm`} />
    {/* USB panel-feedthrough cutouts */}
    {USB_FEEDTHROUGHS.map((f, i) => (
      <Fragment key={`usb-${i}`}>
        <cutout shape="rect" width={`${f.cutoutWidth}mm`} height={`${f.cutoutHeight}mm`} pcbX={`${f.x}mm`} pcbY={`${f.y}mm`} />
        {f.holeSpacing > 0 && [-1, 1].map((sx) => (
          <Fragment key={`usb-hole-${i}-${sx}`}>
            <hole name={`USB_${i}_${sx}`} diameter={`${f.holeDiameter}mm`} pcbX={`${f.x + sx * f.holeSpacing / 2}mm`} pcbY={`${f.y}mm`} />
          </Fragment>
        ))}
      </Fragment>
    ))}
    {/* Transport buttons and LED light-pipe windows */}
    {buttonCutouts.map((h, i) => (
      <Fragment key={`btn-${i}`}><cutout shape="circle" radius={`${PANEL_CUTOUTS.button / 2}mm`} pcbX={`${h.x}mm`} pcbY={`${h.y}mm`} /></Fragment>
    ))}
    {ledCutouts.map((h, i) => (
      <Fragment key={`led-${i}`}><cutout shape="circle" radius={`${PANEL_CUTOUTS.led / 2}mm`} pcbX={`${h.x}mm`} pcbY={`${h.y}mm`} /></Fragment>
    ))}
    {/* Encoder and navigation switch in the right-hand control column */}
    <cutout shape="circle" radius={`${PANEL_CUTOUTS.control / 2}mm`} pcbX={`${RIGHT_CONTROLS.TEMPO.x}mm`} pcbY={`${RIGHT_CONTROLS.TEMPO.y}mm`} />
    <cutout shape="circle" radius={`${PANEL_CUTOUTS.control / 2}mm`} pcbX={`${RIGHT_CONTROLS.NAV.x}mm`} pcbY={`${RIGHT_CONTROLS.NAV.y}mm`} />
    {/* MIDI TRS jacks */}
    {jackCutouts.map((h, i) => (
      <Fragment key={`jack-${i}`}><cutout shape="circle" radius={`${PANEL_CUTOUTS.jack / 2}mm`} pcbX={`${h.x}mm`} pcbY={`${h.y}mm`} /></Fragment>
    ))}
    {/* Front-board standoffs */}
    {FRONT_BOARD_HOLES.map((h, i) => (
      <Fragment key={`stand-${i}`}><hole name={`STAND_${i + 1}`} diameter={`${MOUNT_HOLE_DIAMETER}mm`} pcbX={`${h.x}mm`} pcbY={`${h.y}mm`} /></Fragment>
    ))}
    {/* Eurorack M3 rail holes */}
    {RAIL_HOLES.map((h, i) => (
      <Fragment key={`rack-${i}`}><hole name={`RACK_${i + 1}`} diameter={`${RAIL_HOLE_DIAMETER}mm`} pcbX={`${h.x}mm`} pcbY={`${h.y}mm`} /></Fragment>
    ))}

    {/* White silkscreen graphics only; no copper and no solder mask. */}
    <silkscreenrect pcbX="0mm" pcbY="0mm" width="130.08mm" height="126.5mm" strokeWidth="0.3mm" layer="top" />
    <silkscreentext text="MIDI BRAIN" pcbX="-51mm" pcbY="-59mm" fontSize="5mm" anchorAlignment="center_left" layer="top" />
    <silkscreentext text="MIDI TRS TYPE A" pcbX="0mm" pcbY="-59mm" fontSize="1.8mm" anchorAlignment="center" layer="top" />
    {/* Control labels derive from the shared positions in lib/layout.ts. */}
    {PANEL_LABELS.map((l, i) => (
      <Fragment key={`label-${i}`}><silkscreentext text={l.text} pcbX={`${l.x}mm`} pcbY={`${l.y}mm`} fontSize="1.8mm" anchorAlignment="center" layer="top" /></Fragment>
    ))}
  </board>
)
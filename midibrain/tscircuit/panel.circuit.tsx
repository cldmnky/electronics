import { Fragment } from "react"
import { brandArtPaths } from "./lib/panel-art"
import {
  FRONT_BOARD_HOLES,
  FRONT_COLUMNS,
  JACK_Y,
  LED_ID,
  ledPosition,
  MOUNT_HOLE_DIAMETER,
  OUT_RING,
  PANEL,
  PANEL_CUTOUTS,
  PANEL_LABELS,
  PANEL_LCD_CUTOUT,
  PLAY_START,
  PANEL_TITLES,
  POWER_BUTTON,
  RAIL_HOLES,
  RAIL_HOLE_DIAMETER,
  RIGHT_CONTROLS,
  TRANSPORT_Y,
  USB_FEEDTHROUGHS,
  roundedRectOutline,
} from "./lib/layout"

// 26HP x 3U FR-4 front panel. Fabricate as a bare board:
//   FR4 1.6 mm, 2 layers, NO solder mask, white silkscreen, ENIG or HASL.
// No copper, no components. LED openings take 4.0 mm light pipes (Bivar
// PLP2-400-C); the SMD LEDs remain on the front board behind them.
// Every opening and label position imports from lib/layout.ts.

// The LCD aperture is a rounded rectangle, not a sharp rect: a routed inside
// corner cannot be sharp. The explicit 1.0 mm relief is emitted as a polygon,
// and its chords lie inside the true arc, which makes the aperture marginally
// smaller than the exact rounded rectangle. Measured segment clearance at a
// square module corner: 0.571 mm (scripts/check_placement.ts), above the
// 0.5 mm target. tscircuit has no rounded-rect cutout, so this is a polygon.
const lcdCutoutRoute = roundedRectOutline(PANEL_LCD_CUTOUT).map(p => ({ x: `${p.x}mm`, y: `${p.y}mm` }))

// Transport columns plus PLAY START and SHIFT/EXIT take button openings.
const buttonCutouts = [
  ...FRONT_COLUMNS.map(c => ({ x: c.x, y: TRANSPORT_Y })),
  { x: PLAY_START.x, y: TRANSPORT_Y },
  { x: RIGHT_CONTROLS.SHIFT_EXIT.x, y: RIGHT_CONTROLS.SHIFT_EXIT.y },
]
// LED light-pipe openings sit 12 mm beside their transport buttons (LED_Y is
// the transport row): the glass-derived window owns the row above.
const ledCutouts = FRONT_COLUMNS.filter(c => c.button in LED_ID).map(c => ledPosition(c.x))
// MIDI jack barrels in the four transport columns.
const jackCutouts = FRONT_COLUMNS.map(c => ({ x: c.x, y: JACK_Y }))

export default () => (
  <board width={`${PANEL.width}mm`} height={`${PANEL.height}mm`} thickness={`${PANEL.thickness}mm`} layers={2}>
    {/* LCD glass cutout: rounded-rect aperture with explicit corner relief */}
    <cutout shape="polygon" name="LCD_APERTURE" points={lcdCutoutRoute} />
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
    {/* Panel POWER button (owner-approved): same 12.4 mm B3F opening as the
        transport row, in the bottom-right corner away from it. */}
    <cutout shape="circle" radius={`${PANEL_CUTOUTS.button / 2}mm`} pcbX={`${POWER_BUTTON.x}mm`} pcbY={`${POWER_BUTTON.y}mm`} />
    <cutout shape="circle" radius={`${PANEL_CUTOUTS.control / 2}mm`} pcbX={`${RIGHT_CONTROLS.NAV.x}mm`} pcbY={`${RIGHT_CONTROLS.NAV.y}mm`} />
    {/* MIDI TRS jacks */}
    {jackCutouts.map((h, i) => (
      <Fragment key={`jack-${i}`}><cutout shape="circle" radius={`${PANEL_CUTOUTS.jack / 2}mm`} pcbX={`${h.x}mm`} pcbY={`${h.y}mm`} /></Fragment>
    ))}
    {/* Signal-style bold rings around the MIDI OUT barrels (inputs plain) */}
    {FRONT_COLUMNS.filter(c => c.jack.startsWith("MIDI OUT")).map((c, i) => (
      <Fragment key={`out-ring-${i}`}><silkscreencircle pcbX={`${c.x}mm`} pcbY={`${JACK_Y}mm`} radius={`${OUT_RING.radius}mm`} strokeWidth={`${OUT_RING.stroke}mm`} layer="top" /></Fragment>
    ))}
    {/* mnkybox wordmark + flanking waves, drawn paths (see lib/panel-art.ts) */}
    {brandArtPaths(0, -59).flatMap((g, i) => g.paths.map((p, j) => (
      <Fragment key={`brand-${i}-${j}`}><silkscreenpath layer="top" strokeWidth={`${g.stroke}mm`} route={p} /></Fragment>
    )))}
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
    {PANEL_TITLES.map((t, i) => (
      <Fragment key={`title-${i}`}><silkscreentext text={t.text} pcbX={`${t.x}mm`} pcbY={`${t.y}mm`} fontSize={`${t.fontSize}mm`} anchorAlignment="center" layer="top" /></Fragment>
    ))}
    {/* Control labels derive from the shared positions in lib/layout.ts. */}
    {PANEL_LABELS.map((l, i) => (
      <Fragment key={`label-${i}`}><silkscreentext text={l.text} pcbX={`${l.x}mm`} pcbY={`${l.y}mm`} fontSize="1.8mm" anchorAlignment="center" layer="top" /></Fragment>
    ))}
  </board>
)
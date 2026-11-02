// Froemix euro board - the FRONT PANEL (20HP x 3U, 2mm FR4).
//
// This is a mechanical board: no copper, no nets. Only the outline, the holes
// and the silkscreen. Every hole is read from lib/layout.ts, the same table the
// front board (`index.circuit.tsx`) places its parts from, so panel and board
// cannot drift apart. Stack-up and the open questions: froemix/doc/EURO-BOARD.md,
// material and order notes: froemix/doc/FRONT-PANEL.md.
//
// Printed artwork is NOT text: lib/panel-art.ts turns every legend into
// single-stroke polylines and this file emits one `<silkscreenpath>` per
// polyline with an explicit stroke width. `<silkscreentext>` is banned on a
// panel because its print width is `font_size * 0.09` (0.099mm at 1.1mm), which
// is thinner than JLCPCB will print reliably. Checks: lib/panel-checks.ts.
//
// Cutouts/holes:
//   4x M3 rail screws   (+/-43.3, +/-61.25)   Doepfer 3U
//   6x pot bushings     M7                     Alps RK09K
//  12x jack bushings    6.2mm                  XKB PJ-301M-2-B (mono) /
//                                               XKB PJ-301M-3-B (TRS)
//   2x CV jack LEDs      3.2mm
//   1x panel LED         3.2mm
//   1x navigation switch 7.0mm
//   1x MODE button      12.2mm
//   1x OLED window      30.5 x 16.0mm, R0.5 internal corners, rounded polygon

import { Fragment } from "react"
import { PANEL_REAR_STROKE, panelRearPaths } from "./lib/panel-rear"
import {
  BOARD_H,
  BOARD_W,
  BUTTON,
  HOLE,
  JACKS,
  JACK_LEDS,
  JOYSTICK,
  LED_AT,
  POTS,
  RAIL_HOLES,
} from "./lib/layout"
import {
  DEFAULT_PANEL_DESIGN,
  isPanelDesignId,
  panelArtwork,
  type PanelDesignId,
} from "./lib/panel-art"

/**
 * Selected design. The owner chose `signal`, which is the default;
 * `FROEMIX_PANEL_DESIGN=nord` (or `--inject-props '{"design":"nord"}'`) builds
 * the alternative artwork for comparison.
 */
const resolveDesign = (fromProps?: string): PanelDesignId => {
  const raw =
    fromProps ?? (typeof process === "undefined" ? undefined : process.env?.FROEMIX_PANEL_DESIGN)
  if (!raw) return DEFAULT_PANEL_DESIGN
  if (!isPanelDesignId(raw)) {
    throw new Error(
      `panel.circuit.tsx: unknown panel design ${JSON.stringify(raw)}; use "nord" or "signal"`,
    )
  }
  return raw
}

export default ({ design }: { design?: string } = {}) => {
  const art = panelArtwork(resolveDesign(design))

  return (
    <board
      width={`${BOARD_W}mm`}
      height={`${BOARD_H}mm`}
      thickness={`${art.outline.thickness}mm`}
    >
      {/* rack screws */}
      {RAIL_HOLES.map((h, i) => (
        <Fragment key={`rail-${i}`}>
          <hole diameter={`${HOLE.rail}mm`} pcbX={`${h.x}mm`} pcbY={`${h.y}mm`} />
        </Fragment>
      ))}

      {/* jack bushings */}
      {JACKS.map((j) => (
        <Fragment key={`jack-${j.name}`}>
          <hole diameter={`${HOLE.jack}mm`} pcbX={`${j.x}mm`} pcbY={`${j.y}mm`} />
        </Fragment>
      ))}

      {/* pot bushings */}
      {POTS.map((p) => (
        <Fragment key={`pot-${p.name}`}>
          <hole diameter={`${HOLE.pot}mm`} pcbX={`${p.x}mm`} pcbY={`${p.y}mm`} />
        </Fragment>
      ))}

      {/* UI joystick + illuminated MODE button */}
      <hole
        diameter={`${HOLE.joy}mm`}
        pcbX={`${JOYSTICK.x}mm`}
        pcbY={`${JOYSTICK.y}mm`}
      />
      <hole
        diameter={`${HOLE.button}mm`}
        pcbX={`${BUTTON.x}mm`}
        pcbY={`${BUTTON.y}mm`}
      />

      {/* CV-output activity LEDs, one above each CV output jack */}
      {JACK_LEDS.map((l) => (
        <Fragment key={`jackled-${l.name}`}>
          <hole diameter={`${HOLE.jackLed}mm`} pcbX={`${l.x}mm`} pcbY={`${l.y}mm`} />
        </Fragment>
      ))}

      {/* panel indicator LED */}
      <hole
        diameter={`${HOLE.led}mm`}
        pcbX={`${LED_AT.x}mm`}
        pcbY={`${LED_AT.y}mm`}
      />

      {/*
        1.3" display window. A rounded polygon, not a rect: R0.5 internal
        corners are routable, sharp ones are not, and `RectCutoutProps` has no
        corner radius. The bounding box (30.5 x 16.0 at 0,42.8) is unchanged.
      */}
      <cutout shape="polygon" points={art.window.polygon} />

      {/* printed artwork: one stroked path per line, never a text primitive */}
      {/* Reverse-side identity; explicitly stroked like the front artwork. */}
      {panelRearPaths().map((route, i) => (
        <Fragment key={`rear-id-${i}`}><silkscreenpath layer="bottom"
          strokeWidth={`${PANEL_REAR_STROKE}mm`} route={route} /></Fragment>
      ))}
      {art.elements.flatMap((element) =>
        element.paths.map((path, i) => (
          <Fragment key={`${element.id}-${i}`}>
            <silkscreenpath
              layer="top"
              strokeWidth={`${element.strokeWidth}mm`}
              route={path}
            />
          </Fragment>
        )),
      )}
    </board>
  )
}

import { Fragment } from "react"
import { brandArtPaths } from "./lib/panel-art"
import {
  FRONT_COLUMNS,
  JACK_Y,
  midiJackX,
  OUT_RING,
  PANEL,
  PANEL_APERTURES,
  PANEL_LABELS,
  PANEL_MOUNT_HOLES,
  PANEL_TITLES,
} from "./lib/layout"

// 26HP x 3U FR-4 front panel. Fabricate as a bare board:
//   FR4 1.6 mm, 2 layers, NO solder mask, white silkscreen, ENIG or HASL.
// No copper, no components. LED openings take 4.0 mm light pipes (Bivar
// PLP2-400-C); the SMD LEDs remain on the front board behind them.
// Every opening and label position imports from lib/layout.ts.

// Every aperture and every hole comes from lib/layout.ts (`PANEL_APERTURES`,
// `PANEL_MOUNT_HOLES`): one definition, consumed here and by
// `mechanicalExport()` for the STEP/STL mockups. The LCD aperture is a rounded
// rectangle because a routed inside corner cannot be sharp; its chords lie
// inside the true arc, so the aperture is marginally smaller than the exact
// rounded rectangle (0.571 mm segment clearance at a square module corner,
// scripts/check_placement.ts, above the 0.5 mm target). tscircuit has no
// rounded-rect cutout, so it is emitted as a polygon.
//
// The EMISSION ORDER below is part of the contract: lib/panel-cutouts.test.ts
// compares the built dist/panel/circuit.json with PANEL_APERTURES element by
// element. Do not reorder, rename or regroup these elements.

export default () => (
  <board width={`${PANEL.width}mm`} height={`${PANEL.height}mm`} thickness={`${PANEL.thickness}mm`} layers={2}>
    {/* Panel openings: LCD aperture, feedthroughs with their screw holes,
        buttons, LED light pipes, encoder, POWER, NAV and MIDI jacks. */}
    {PANEL_APERTURES.map((a, i) => (
      <Fragment key={`aperture-${i}`}>
        {a.shape === "polygon" && (
          <cutout shape="polygon" name={a.name} points={(a.points ?? []).map(p => ({ x: `${p.x}mm`, y: `${p.y}mm` }))} />
        )}
        {a.shape === "rect" && (
          <cutout shape="rect" width={`${a.width}mm`} height={`${a.height}mm`} pcbX={`${a.x}mm`} pcbY={`${a.y}mm`} />
        )}
        {a.shape === "circle" && (
          <cutout shape="circle" radius={`${a.radius}mm`} pcbX={`${a.x}mm`} pcbY={`${a.y}mm`} />
        )}
        {(a.drills ?? []).map((d, j) => (
          <Fragment key={`drill-${j}`}>
            <hole name={d.name} diameter={`${d.diameter}mm`} pcbX={`${d.x}mm`} pcbY={`${d.y}mm`} />
          </Fragment>
        ))}
      </Fragment>
    ))}
    {/* Signal-style bold rings around the MIDI OUT barrels (inputs plain) */}
    {FRONT_COLUMNS.filter(c => c.jack.startsWith("MIDI OUT")).map((c, i) => (
      <Fragment key={`out-ring-${i}`}><silkscreencircle pcbX={`${midiJackX(c.jack)}mm`} pcbY={`${JACK_Y}mm`} radius={`${OUT_RING.radius}mm`} strokeWidth={`${OUT_RING.stroke}mm`} layer="top" /></Fragment>
    ))}
    {/* mnkybox wordmark + flanking waves, drawn paths (see lib/panel-art.ts) */}
    {brandArtPaths(0, -59).flatMap((g, i) => g.paths.map((p, j) => (
      <Fragment key={`brand-${i}-${j}`}><silkscreenpath layer="top" strokeWidth={`${g.stroke}mm`} route={p} /></Fragment>
    )))}
    {/* Front-board standoffs, then the Eurorack M3 rail holes */}
    {PANEL_MOUNT_HOLES.map((h, i) => (
      <Fragment key={`panel-hole-${i}`}><hole name={h.name} diameter={`${h.diameter}mm`} pcbX={`${h.x}mm`} pcbY={`${h.y}mm`} /></Fragment>
    ))}

    {/* White silkscreen graphics only; no copper and no solder mask.
        A decorative full-panel border rect used to be drawn here (130.08 x
        126.5 mm). It was removed by owner request: the board edge is the outline,
        and an inner square border reads as a second, wrong panel edge. Do not
        reintroduce it. */}
    {PANEL_TITLES.map((t, i) => (
      <Fragment key={`title-${i}`}><silkscreentext text={t.text} pcbX={`${t.x}mm`} pcbY={`${t.y}mm`} fontSize={`${t.fontSize}mm`} anchorAlignment="center" layer="top" /></Fragment>
    ))}
    {/* Control labels derive from the shared positions in lib/layout.ts. */}
    {PANEL_LABELS.map((l, i) => (
      <Fragment key={`label-${i}`}><silkscreentext text={l.text} pcbX={`${l.x}mm`} pcbY={`${l.y}mm`} fontSize="1.8mm" anchorAlignment="center" layer="top" /></Fragment>
    ))}
  </board>
)
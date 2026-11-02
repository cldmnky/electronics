// Silkscreen text added directly in tscircuit: board name/revision/date plus a
// refdes for the parts where a hand-assembler actually needs one - all ICs and
// modules, all connectors, the two electrolytics and all diodes. The 0603/1206
// passives keep their existing silk outlines only: 111 refdes labels do not fit
// between the densely packed courtyards without printing over pads.
//
// Every label sits just outside its part's courtyard on the same side as the
// part, stays >=0.4mm from the board edge and does not cover another part's
// courtyard.
//
// Bottom-layer text is MIRRORED about its anchor at render/export time, so a
// "center_left" anchor runs the glyphs in -X and pushes them off the board (the
// old board notes reached x=-47.5mm on a board that ends at -40.45mm,
// DESIGN-REVIEW R12). Every bottom text is therefore anchored "center", which
// mirrors onto itself, and every anchor stays >=1.5mm inside the edge.
// `tools/check_silk.py` gates the actual gerber coordinates.

import { Fragment } from "react"
import { wordmarkPaths } from "../lib/wordmark"
import { BOARD_REVISION, buildDate } from "../lib/build-info"
import { oshwPaths } from "../lib/oshw"

type Refdes = {
  text: string
  x: number
  y: number
  layer: "top" | "bottom"
}

// Bottom artwork is deliberately outside J2's full courtyard. Paths mirror X
// in the bottom-layer Gerber so they read correctly when viewed from the rear.
const BRAND = { x: -29.5, y: -46.5 }
// Increasing board Y runs upward when the rear is viewed by the builder.
const HAND_SOLDER = [
  "SET J1 BEFORE STACK",
  "C24 C25 U16 DAISY SOCKETS",
  "HAND: CN1-4 H1 J1 J2",
]

const REFDES: Refdes[] = [
  // ICs / module
  { text: "U1", x: -14.65, y: 30.3, layer: "bottom" },
  { text: "U2", x: -20.4, y: 12.0, layer: "bottom" },
  { text: "U3", x: -5.23, y: 16.5, layer: "bottom" },
  // U11's label used to sit at (19.6, 12.0) and overlapped TP4's bare 1.2mm
  // test pad at (20.6, 12.0); moved up-left so it clears the pad and U11.
  { text: "U11", x: 19.0, y: 12.8, layer: "bottom" },
  { text: "U12", x: 1.2, y: 2.5, layer: "bottom" },
  { text: "U13", x: 4.88, y: 16.7, layer: "bottom" },
  { text: "U14", x: 3.97, y: -7.1, layer: "bottom" },
  { text: "U15", x: 21.4, y: -22.23, layer: "bottom" },
  // Keep the 2W module label clear of the relocated C27 and U16 courtyard.
  { text: "U16", x: 11.0, y: -57.1, layer: "bottom" },
  { text: "DaisySeed1", x: 29.5, y: 30.2, layer: "bottom" },
  // connectors
  { text: "CN1", x: -17.58, y: 57.5, layer: "top" },
  { text: "CN2", x: 12.42, y: -38.7, layer: "top" },
  { text: "CN3", x: 8.11, y: 57.5, layer: "top" },
  { text: "CN4", x: 24.47, y: 57.5, layer: "top" },
  { text: "J1", x: 26.16, y: -12.2, layer: "top" },
  { text: "J2", x: -18.5, y: -49.6, layer: "bottom" },
  { text: "H1", x: 24.5, y: 20.0, layer: "bottom" },
  // electrolytics (C24 at (28,-44.5), C25 at (33.5,-53.5): 8.4mm courtyards,
  // so their labels sit outside the body circle, not on it)
  { text: "C24", x: 28.0, y: -39.4, layer: "bottom" },
  { text: "C25", x: 33.5, y: -59.0, layer: "bottom" },
  // diodes
  { text: "D1", x: 24.02, y: -28.5, layer: "bottom" },
  { text: "D5", x: 13.8, y: -0.4, layer: "bottom" },
  // D3/D4 now sit on the bottom; labels are below their courtyards.
  { text: "D3", x: -0.06, y: -61.3, layer: "bottom" },
  { text: "D4", x: 6.44, y: -61.3, layer: "bottom" },
]

const BOARD_NOTES = [
  { text: `REV ${BOARD_REVISION}`, x: -28.5, y: -40.4 },
  { text: buildDate(), x: -28.5, y: -42.8 },
]

export const SilkSection = () => (
  <>
    {REFDES.map((ref) => (
      <Fragment key={`ref-${ref.text}`}><silkscreentext
        text={ref.text}
        fontSize="0.7mm"
        anchorAlignment="center"
        layer={ref.layer}
        pcbX={`${ref.x}mm`}
        pcbY={`${ref.y}mm`}
      /></Fragment>
    ))}
    {BOARD_NOTES.map((note) => (
      <Fragment key={`note-${note.text}`}><silkscreentext
        text={note.text}
        fontSize="1.7mm"
        anchorAlignment="center"
        layer="bottom"
        pcbX={`${note.x}mm`}
        pcbY={`${note.y}mm`}
      /></Fragment>
    ))}
    {/* Same monoline Nord wordmark as the panel, on the component-side rear. */}
    {wordmarkPaths("FROEMIX", 1.35, BRAND.y, BRAND.x).paths.map((route, i) => (
      <Fragment key={`brand-${i}`}><silkscreenpath layer="bottom" strokeWidth="0.2mm"
        route={route.map(p => ({ x: 2 * BRAND.x - p.x, y: p.y }))} /></Fragment>
    ))}
    {/* Community OSHW gear, not an OSHWA certification mark. */}
    {oshwPaths(-34, -32, 7).map((route, i) => (
      <Fragment key={`oshw-${i}`}><silkscreenpath layer="bottom" strokeWidth="0.2mm" route={route} /></Fragment>
    ))}
    {/* hand-assembly legend: the parts JLC does not place (see doc/ASSEMBLY.md) */}
    {HAND_SOLDER.map((line, i) => (
      <Fragment key={`hand-${i}`}><silkscreentext
        text={line}
        fontSize="1.7mm"
        anchorAlignment="center"
        layer="bottom"
        pcbX="-8mm"
        pcbY={`${-32.5 + i * 2}mm`}
      /></Fragment>
    ))}
  </>
)

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

type Refdes = {
  text: string
  x: number
  y: number
  layer: "top" | "bottom"
}

// Board logo + hand-assembly legend, drawn from silkscreen primitives so it
// lands in the gerbers with no image pipeline. Bottom layer: that is the face
// you see with the module in the rack (the top holds the mating headers).
// The badge is a 13x7mm rect at (-34,-52)..(-21,-45): four waveform bars in
// the left half, the wordmark and tagline centred in the right half.
const LOGO = { x: -34, y: -52 }
const LOGO_BARS = [
  { dx: 1.3, h: 2.0 },
  { dx: 2.4, h: 3.6 },
  { dx: 3.5, h: 4.5 },
  { dx: 4.6, h: 2.8 },
]

const HAND_SOLDER = [
  "HAND SOLDER: CN1-4 H1 J1 J2",
  "C24 C25 U16 + DAISY SEED MODULE",
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
  { text: "REV 0.1", x: -28.5, y: -42.8 },
  { text: "2024-08-10", x: -28.5, y: -40.4 },
]

export const SilkSection = () => (
  <>
    {REFDES.map((ref) => (
      <silkscreentext
        key={`ref-${ref.text}`}
        text={ref.text}
        fontSize="0.7mm"
        anchorAlignment="center"
        layer={ref.layer}
        pcbX={`${ref.x}mm`}
        pcbY={`${ref.y}mm`}
      />
    ))}
    {BOARD_NOTES.map((note) => (
      <silkscreentext
        key={`note-${note.text}`}
        text={note.text}
        fontSize="1.0mm"
        anchorAlignment="center"
        layer="bottom"
        pcbX={`${note.x}mm`}
        pcbY={`${note.y}mm`}
      />
    ))}
    {/* logo: badge + waveform bars + wordmark, bottom side */}
    <silkscreenrect
      pcbX={`${LOGO.x + 6.5}mm`}
      pcbY={`${LOGO.y + 3.5}mm`}
      width="13mm"
      height="7mm"
      layer="bottom"
      strokeWidth="0.3mm"
    />
    {LOGO_BARS.map((bar, i) => (
      <silkscreenline
        key={`logo-bar-${i}`}
        x1={`${LOGO.x + bar.dx}mm`}
        y1={`${LOGO.y + 3.5 - bar.h / 2}mm`}
        x2={`${LOGO.x + bar.dx}mm`}
        y2={`${LOGO.y + 3.5 + bar.h / 2}mm`}
        strokeWidth="0.45mm"
        layer="bottom"
      />
    ))}
    <silkscreentext
      text="FROEMIX"
      fontSize="1.4mm"
      anchorAlignment="center"
      layer="bottom"
      pcbX="-25.6mm"
      pcbY="-47.6mm"
    />
    <silkscreentext
      text="eurorack dsp"
      fontSize="0.7mm"
      anchorAlignment="center"
      layer="bottom"
      pcbX="-25.6mm"
      pcbY="-50mm"
    />
    {/* hand-assembly legend: the parts JLC does not place (see doc/ASSEMBLY.md) */}
    {HAND_SOLDER.map((line, i) => (
      <silkscreentext
        key={`hand-${i}`}
        text={line}
        fontSize="0.9mm"
        anchorAlignment="center"
        layer="bottom"
        pcbX="-8mm"
        pcbY={`${-33.2 + i * 1.6}mm`}
      />
    ))}
  </>
)

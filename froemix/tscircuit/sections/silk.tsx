// Silkscreen text added directly in tscircuit: board name/revision/date plus a
// refdes for the parts where a hand-assembler actually needs one - all ICs and
// modules, all connectors, the two electrolytics and all diodes. The 0603/1206
// passives keep their existing silk outlines only: 111 refdes labels do not fit
// between the densely packed courtyards without printing over pads.
//
// Every label sits just outside its part's courtyard on the same side as the
// part, stays >=0.4mm from the board edge and does not cover another part's
// courtyard.

type Refdes = {
  text: string
  x: number
  y: number
  layer: "top" | "bottom"
}

// Board logo + hand-assembly legend, drawn from silkscreen primitives so it
// lands in the gerbers with no image pipeline. Bottom layer: that is the face
// you see with the module in the rack (the top holds the mating headers).
// Placement was checked against every courtyard: 13x7mm free rect at
// (-34,-52) and a 26x3.4mm band at (-8,-34).
const LOGO = { x: -34, y: -52 }
const LOGO_BARS = [
  { dx: 1.6, h: 2.0 },
  { dx: 3.4, h: 4.2 },
  { dx: 5.2, h: 5.6 },
  { dx: 7.0, h: 4.2 },
  { dx: 8.8, h: 2.0 },
  { dx: 10.6, h: 3.2 },
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
  { text: "U11", x: 19.6, y: 12.0, layer: "bottom" },
  { text: "U12", x: 1.2, y: 2.5, layer: "bottom" },
  { text: "U13", x: 4.88, y: 16.7, layer: "bottom" },
  { text: "U14", x: 3.97, y: -7.1, layer: "bottom" },
  { text: "U15", x: 21.4, y: -22.23, layer: "bottom" },
  { text: "U16", x: 3.39, y: -43.8, layer: "bottom" },
  { text: "DaisySeed1", x: 29.5, y: 30.2, layer: "bottom" },
  // connectors
  { text: "CN1", x: -17.58, y: 57.5, layer: "top" },
  { text: "CN2", x: 12.42, y: -38.7, layer: "top" },
  { text: "CN3", x: 8.11, y: 57.5, layer: "top" },
  { text: "CN4", x: 24.47, y: 57.5, layer: "top" },
  { text: "J1", x: 26.16, y: -12.2, layer: "top" },
  { text: "J2", x: -18.5, y: -49.6, layer: "bottom" },
  { text: "H1", x: 24.5, y: 20.0, layer: "bottom" },
  // electrolytics
  { text: "C24", x: 23.9, y: -48.6, layer: "bottom" },
  { text: "C25", x: 11.95, y: -58.3, layer: "bottom" },
  // diodes
  { text: "D1", x: 24.02, y: -28.5, layer: "bottom" },
  { text: "D2", x: 29.26, y: -28.5, layer: "bottom" },
  { text: "D3", x: -0.06, y: -57.1, layer: "top" },
  { text: "D4", x: 6.44, y: -57.1, layer: "top" },
]

const BOARD_NOTES = [
  { text: "FROEMIX", x: -39.6, y: -40.5 },
  { text: "REV 0.1", x: -39.6, y: -43.0 },
  { text: "2024-08-10", x: -39.6, y: -45.5 },
]

export const SilkSection = () => (
  <>
    {REFDES.map((ref) => (
      <silkscreentext
        key={`ref-${ref.text}`}
        text={ref.text}
        fontSize="0.7mm"
        layer={ref.layer}
        pcbX={`${ref.x}mm`}
        pcbY={`${ref.y}mm`}
      />
    ))}
    {BOARD_NOTES.map((note) => (
      <silkscreentext
        key={`note-${note.text}`}
        text={note.text}
        fontSize="1.2mm"
        anchorAlignment="center_left"
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
        y1={`${LOGO.y + 1.4 + (5.6 - bar.h) / 2}mm`}
        x2={`${LOGO.x + bar.dx}mm`}
        y2={`${LOGO.y + 1.4 + (5.6 + bar.h) / 2}mm`}
        strokeWidth="0.45mm"
        layer="bottom"
      />
    ))}
    <silkscreentext
      text="FROEMIX"
      fontSize="1.5mm"
      anchorAlignment="center_left"
      layer="bottom"
      pcbX={`${LOGO.x + 0.9}mm`}
      pcbY={`${LOGO.y + 6.2}mm`}
    />
    <silkscreentext
      text="eurorack dsp"
      fontSize="0.8mm"
      anchorAlignment="center_left"
      layer="bottom"
      pcbX={`${LOGO.x + 0.9}mm`}
      pcbY={`${LOGO.y + 0.7}mm`}
    />
    {/* hand-assembly legend: the parts JLC does not place (see doc/ASSEMBLY.md) */}
    {HAND_SOLDER.map((line, i) => (
      <silkscreentext
        key={`hand-${i}`}
        text={line}
        fontSize="0.9mm"
        anchorAlignment="center_left"
        layer="bottom"
        pcbX="-8mm"
        pcbY={`${-33.2 + i * 1.6}mm`}
      />
    ))}
  </>
)

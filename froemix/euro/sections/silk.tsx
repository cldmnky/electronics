// Froemix euro front board - silkscreen.
//
// Two audiences, one per side:
//
//   BOTTOM (facing the base board, seen while the SMT parts are hand-fitted):
//     refdes + value for all 12 SMT parts. The 0603 footprints already print
//     their body dashes and the SOD-323 footprint prints the cathode bar on
//     pin 1, so the diode label only names the part.
//
//   TOP (facing the panel, seen while the 27 through-hole parts are fitted):
//     the pad letter of every jack terminal (S / R / T), the hand-assembly
//     rules for this board's traps, and the FROEMIX wordmark. The panel covers
//     this side in the finished module; it is assembly artwork, not a legend.
//
// The jack letters are the point of the exercise: the WQP518MA/WQP419GR vendor
// numbering is 1 = sleeve, 2 = ring, 3 = tip, while this board's TRS footprint
// is labelled pin1 = tip, pin2 = ring, pin3 = sleeve (lib/footprints.tsx).
// A builder who follows the numbers rather than the letters shorts a MIDI line
// to ground. The letters make "solder by function" readable on the board.
// Every other THT part already marks pin 1 with a square pad.
//
// Nothing here may print over a pad, a hole or the board edge:
// `scripts/check_silk.py` gates it.

import { Fragment } from "react"
import { BUTTON, JACKS, JACK_LEDS, LED_R_AT, type Jack } from "../lib/layout"
import { wordmarkPaths } from "../lib/panel-art"
import {
  LABEL_CAP,
  NOTE_CAP,
  NOTE_STROKE,
  PAD_CAP,
  SilkLine,
  SilkText,
} from "../lib/silk"

// ---------------------------------------------------------------------------
// Bottom: the 12 SMT parts
// ---------------------------------------------------------------------------

/**
 * One label per SMT part, centre-anchored on its printed ink box.
 *
 * The four ladder resistors R3-R6 sit on a 3.5mm pitch, which cannot hold a
 * "R4 2K2" string (5.4mm at 0.8mm cap) between neighbours, so the five ladder
 * labels alternate over two rows and each is joined to its part by a leader
 * tick. The rest are written beside their part and need no tick.
 */
type SmdLabel = { text: string; x: number; y: number; tickTo?: number }

const SMD_LABELS: SmdLabel[] = [
  // joystick pull-up (R2) and the four direction resistors (R3-R6)
  { text: "R2 10K", x: -33, y: 15.6, tickTo: 17.35 },
  { text: "R4 2K2", x: -26, y: 15.6, tickTo: 17.35 },
  { text: "R6 10K", x: -19, y: 15.6, tickTo: 17.35 },
  { text: "R3 1K", x: -29.5, y: 13.6, tickTo: 17.35 },
  { text: "R5 4K7", x: -22.5, y: 13.6, tickTo: 17.35 },
  // panel LED series resistor
  { text: "R1 1K", x: LED_R_AT.x, y: LED_R_AT.y - 3.2 },
  // MODE button series resistor and pull-up
  { text: "R7 1K", x: BUTTON.x + 5, y: 11 },
  { text: "R8 10K", x: BUTTON.x, y: 13.6 },
  // CV output LED chains: resistor and series diode, labelled on the outside
  { text: "R14 2K2", x: JACK_LEDS[0].x - 6.4, y: -24 },
  { text: "D6", x: JACK_LEDS[0].x - 3.1, y: -27.4 },
  { text: "R15 2K2", x: JACK_LEDS[1].x + 6.4, y: -24 },
  { text: "D7", x: JACK_LEDS[1].x + 3.1, y: -27.4 },
]

const SMD_NOTE = "SMD SIDE - 12 PARTS"

// ---------------------------------------------------------------------------
// Top: jack pad letters
// ---------------------------------------------------------------------------

/** Pad offsets from the barrel datum, mm (lib/footprints.tsx). */
const MONO_PADS = [
  { letter: "S", y: 6.48 }, // sleeve
  { letter: "T", y: -4.92 }, // tip; the +3.38 tip switch is unused and unmarked
] as const

const TRS_PADS = [
  { letter: "T", y: 4.92 },
  { letter: "R", y: -3.38 },
  { letter: "S", y: -6.48 },
] as const

const padLetters = (jack: Jack) => (jack.kind === "mono" ? MONO_PADS : TRS_PADS)

// ---------------------------------------------------------------------------
// Top: assembly rules
// ---------------------------------------------------------------------------

const TOP_NOTES = [
  "JACKS - SOLDER BY LETTER",
  "NOT BY VENDOR PIN NUMBER",
  "SQUARE PAD = PIN 1",
  "LED SQUARE PAD = ANODE (+)",
  "SOCKETS CN1 CN2 CN4",
  "FIT FROM BELOW",
]

const NOTES_X = -29.5
const NOTES_TOP = -23.2
const NOTES_PITCH = 2.45

const BRAND = { x: 26.5, baseline: -32.4, cap: 2.7 }

export const SilkSection = () => (
  <>
    {/* ---------------- bottom: SMT labels --------------- */}
    {SMD_LABELS.map((label) => (
      <Fragment key={`smd-${label.text}`}>
        <SilkText text={label.text} x={label.x} y={label.y} layer="bottom" />
        {label.tickTo === undefined ? null : (
          <SilkLine
            layer="bottom"
            from={{ x: label.x, y: label.y + LABEL_CAP + 0.35 }}
            to={{ x: label.x, y: label.tickTo }}
          />
        )}
      </Fragment>
    ))}
    <SilkText text={SMD_NOTE} x={0} y={-55.5} layer="bottom" cap={NOTE_CAP} stroke={NOTE_STROKE} />

    {/* ---------------- top: jack terminals --------------- */}
    {JACKS.map((jack) => (
      <Fragment key={`pads-${jack.name}`}>
        {padLetters(jack).map((pad) => (
          <SilkText
            key={`${jack.name}-${pad.letter}`}
            text={pad.letter}
            x={jack.x + 2.6}
            y={jack.y + pad.y - PAD_CAP / 2}
            layer="top"
            cap={PAD_CAP}
          />
        ))}
      </Fragment>
    ))}

    {/* ---------------- top: assembly rules --------------- */}
    {TOP_NOTES.map((note, i) => (
      <SilkText
        key={`note-${i}`}
        text={note}
        x={NOTES_X}
        y={NOTES_TOP - i * NOTES_PITCH}
        layer="top"
        cap={NOTE_CAP}
        stroke={NOTE_STROKE}
      />
    ))}

    {/* ---------------- top: wordmark --------------- */}
    {wordmarkPaths("FROEMIX", BRAND.cap, BRAND.baseline, BRAND.x).paths.map((route, i) => (
      <Fragment key={`brand-${i}`}>
        <silkscreenpath layer="top" strokeWidth="0.4mm" route={route} />
      </Fragment>
    ))}
  </>
)

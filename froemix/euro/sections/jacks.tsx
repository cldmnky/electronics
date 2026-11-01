// Froemix euro board (front board) - the 12 panel jacks.
//
// 10 mono PJ398SM (audio in/out, CV in/out, sync in/out) + 2 stereo PJ366ST for
// TRS MIDI. The mono jack's tip switch is left floating on purpose: these inputs
// are AC-coupled and buffered on the base board, so a normal-to-ground would add
// nothing and an output must not be shorted when the socket is empty.
//
// Sleeves are GND on every jack. The barrel (panel hole) is the footprint origin
// for the TRS jack; the mono jack's origin is its sleeve pad, 6.48mm away - both
// handled in lib/layout.ts.

import { JACKS, JACK_MONO_ORIGIN, atHole } from "../lib/layout"
import { FP_JACK_MONO, FP_JACK_TRS } from "../lib/footprints"

export const JacksSection = () => (
  <>
    {JACKS.map((jack) => {
      const at = jack.kind === "mono" ? atHole(jack, JACK_MONO_ORIGIN) : jack
      const schPinArrangement =
        jack.kind === "mono"
          ? { leftSide: [1, 3], rightSide: [2] }
          : { leftSide: [1, 3], rightSide: [2] }

      return jack.kind === "mono" ? (
        <connector
          key={jack.name}
          name={jack.name}
          footprint={FP_JACK_MONO}
          pinCount={3}
          pinLabels={{ pin1: "sleeve", pin2: "tip" }}
          mpn="WQP-PJ398SM"
          pcbX={`${at.x}mm`}
          pcbY={`${at.y}mm`}
          schPinArrangement={schPinArrangement}
          connections={{
            sleeve: "net.GND",
            tip: `net.${jack.net}`,
          }}
        />
      ) : (
        <connector
          key={jack.name}
          name={jack.name}
          footprint={FP_JACK_TRS}
          pinCount={3}
          pinLabels={{ pin1: "tip", pin2: "ring", pin3: "sleeve" }}
          mpn="WQP-PJ366ST"
          pcbX={`${at.x}mm`}
          pcbY={`${at.y}mm`}
          schPinArrangement={schPinArrangement}
          connections={{
            tip: `net.${jack.net}`,
            ring: `net.${jack.ringNet}`,
            sleeve: "net.GND",
          }}
        />
      )
    })}
  </>
)

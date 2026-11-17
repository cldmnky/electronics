// Froemix euro board (front board) - the 12 panel jacks.
//
// 10 mono QingPu/Thonkiconn WQP518MA (audio in/out, CV in/out, sync in/out)
// + 2 stereo WQP419GR for TRS MIDI - the same Thonkiconn family midibrain uses
// (owner decision 2026-11-16; library rows jack-35-wqp518ma / jack-35-wqp419gr,
// hand-soldered Exploding Shed packs, no LCSC codes). The mono jack's tip
// switch is left floating on purpose: these inputs are AC-coupled and buffered
// on the base board, so a normal-to-ground would add nothing and an output must
// not be shorted when the socket is empty.
//
// Sleeves are GND on every jack. The barrel (panel hole) is the footprint
// origin for both jacks, so lib/layout.ts holds one coordinate per part,
// shared with the panel.
//
// 3D: the viewer gets drawing-derived ENVELOPE models (cad/jack-*-envelope.obj);
// the vendor STEPs (clacktronics/AudioJacks MIT, pinned commit 14a88866) are
// held with provenance in library/cad/jack-35-wqp*/SOURCE.md but do not mesh in
// the tscircuit viewer (verified 2026-11-17), so they are not wired as cadModel.

import { JACKS, JACK_MONO_ORIGIN, atHole } from "../lib/layout"
import { ENVELOPE_JACK_MONO, ENVELOPE_JACK_TRS } from "../lib/cad-models"
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
          mpn="WQP518MA"
          cadModel={{
            objUrl: ENVELOPE_JACK_MONO,
            modelOriginPosition: { x: 0, y: 0, z: 0 },
          }}
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
          mpn="WQP419GR"
          cadModel={{
            objUrl: ENVELOPE_JACK_TRS,
            modelOriginPosition: { x: 0, y: 0, z: 0 },
          }}
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

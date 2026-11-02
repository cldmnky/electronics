// Froemix euro board - signal-activity LEDs on the two CV outputs.
//
// Zero pins and no firmware: the signal at the jack drives its own LED.
//
//   jack net -- 2.2k -- diode -->|-- LED -->|-- GND
//                (`X_D` node)   (`X_K` node)
//
// The series diode passes only positive excursions, so the LED never sees a
// reverse half-cycle of CV. At a 5V peak the LED gets
// (5 - 0.7 - 2.0)/2.2k = 1.0mA, which is a clear glow for a modern 3mm LED.
//
// The audio indicators (DL1-DL4) were removed from this revision. On an audio
// output the diode/LED branch sits after the base board's 1k output resistor,
// outside the op-amp feedback loop: it clipped the positive peak and added
// several percent THD. On an input it loaded the external source through a
// nonlinear 2.2k. The CV outputs are different - their 1k is inside the
// compensated feedback loop (DESIGN-REVIEW R09), so the LED current is
// corrected by the op-amp instead of becoming an audio-band error.
//
// The 2.2k and the diode are 0603/SOD-323 SMD on the BOTTOM (JLCPCB places
// them); the LED is 3mm THT on the TOP because it has to reach the panel.

import { Fragment } from "react"

import { JACK_LEDS } from "../lib/layout"
import { FP_LED_3MM, FP_R0603, FP_SOD323 } from "../lib/footprints"

export const JackLedsSection = () => (
  <>
    {JACK_LEDS.map((jl) => (
      <Fragment key={jl.name}>
        <resistor
          name={jl.resistor}
          resistance="2.2k"
          footprint={FP_R0603}
          mpn="0603WAF2201T5E"
          supplierPartNumbers={{ jlcpcb: ["C4190"] }}
          layer="bottom"
          pcbX={`${jl.x}mm`}
          pcbY="-24mm"
          connections={{ pin1: `net.${jl.net}`, pin2: `net.${jl.base}_D` }}
        />
        <diode
          name={jl.diode}
          footprint={FP_SOD323}
          mpn="1N4148WS"
          supplierPartNumbers={{ jlcpcb: ["C2128"] }}
          layer="bottom"
          pinLabels={{ pin1: "cathode", pin2: "anode" }}
          pcbX={`${jl.x}mm`}
          pcbY="-27mm"
          connections={{ anode: `net.${jl.base}_D`, cathode: `net.${jl.base}_K` }}
        />
        <led
          name={jl.name}
          footprint={FP_LED_3MM}
          mpn="TJ-L3FYTLDRMFCSFLC2R-A5"
          supplierPartNumbers={{ jlcpcb: ["C440523"] }}
          pinLabels={{ pin1: "anode", pin2: "cathode" }}
          pcbX={`${jl.x}mm`}
          pcbY={`${jl.y}mm`}
          connections={{ anode: `net.${jl.base}_K`, cathode: "net.GND" }}
        />
      </Fragment>
    ))}
  </>
)

// Froemix euro board - signal-activity LEDs on six jack signals.
//
// Zero pins and no firmware: the signal at the jack drives its own LED.
//
//   jack net -- 2.2k -- diode -->|-- LED -->|-- GND
//                (`X_D` node)   (`X_K` node)
//
// The series diode passes only positive excursions, so the LED never sees a
// reverse half-cycle of audio or CV. At a 5V peak the LED gets
// (5 - 0.7 - 2.0)/2.2k = 1.0mA, which is a clear glow for a modern 3mm LED.
//
// The 2.2k and the diode are 0603/SOD-323 SMD on the BOTTOM (JLCPCB places
// them); the LED is 3mm THT on the TOP because it has to reach the panel.

import { Fragment } from "react"

import { JACK_LEDS } from "../lib/layout"
import { FP_LED_3MM, FP_R0603, FP_SOD323 } from "../lib/footprints"

export const JackLedsSection = () => (
  <>
    {JACK_LEDS.map((jl, i) => {
      const isCv = jl.y < -20
      return (
        <Fragment key={jl.name}>
          <resistor
            key={`r-${jl.name}`}
            name={`R${10 + i}`}
            resistance="2.2k"
            footprint={FP_R0603}
            mpn="0603WAF2201T5E"
            supplierPartNumbers={{ jlcpcb: ["C4190"] }}
            layer="bottom"
            pcbX={`${jl.x}mm`}
            pcbY={isCv ? "-24mm" : "4mm"}
            connections={{ pin1: `net.${jl.net}`, pin2: `net.${jl.base}_D` }}
          />
          <diode
            key={`d-${jl.name}`}
            name={`D${2 + i}`}
            footprint={FP_SOD323}
            mpn="1N4148WS"
            supplierPartNumbers={{ jlcpcb: ["C2128"] }}
            layer="bottom"
            pinLabels={{ pin1: "cathode", pin2: "anode" }}
            pcbX={`${jl.x}mm`}
            pcbY={isCv ? "-27mm" : "7mm"}
            connections={{ anode: `net.${jl.base}_D`, cathode: `net.${jl.base}_K` }}
          />
          <led
            key={`l-${jl.name}`}
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
      )
    })}
  </>
)

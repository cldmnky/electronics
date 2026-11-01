// Froemix euro board - illuminated MODE button on M_ADC_7.
//
// The button's own LED is the pull-up, so the button costs no GPIO and needs no
// firmware to light up:
//
//   P3V3_A -- 1k -- LED(A) -- LED(K) --+-- M_ADC_7 (mux channel 7)
//                                     +-- switch -- GND
//
// Released: no current path, the LED is dark, the node floats to 3.3V through
// the 1k and the LED's leakage, so the ADC reads full scale.
// Pressed:  the node is pulled to 0V and the LED lights at (3.3 - 2.0)/1k =
// 1.3mA. Both states are unambiguous to a 12-bit ADC.

import { BUTTON } from "../lib/layout"
import { FP_BUTTON_12MM, FP_R0603 } from "../lib/footprints"

export const ButtonSection = () => (
  <>
    <connector
      name={BUTTON.name}
      footprint={FP_BUTTON_12MM}
      pinCount={4}
      mpn="TL1240"
      pinLabels={{ pin1: "sw1", pin2: "sw2", pin3: "ledA", pin4: "ledK" }}
      pcbX={`${BUTTON.x}mm`}
      pcbY={`${BUTTON.y}mm`}
      schPinArrangement={{ leftSide: ["sw1", "ledK"], rightSide: ["sw2", "ledA"] }}
      connections={{
        sw1: `net.${BUTTON.net}`,
        sw2: "net.GND",
        ledA: "net.BTN_LED",
        ledK: `net.${BUTTON.net}`,
      }}
    />

    <resistor
      name="R7"
      resistance={BUTTON.seriesR}
      footprint={FP_R0603}
      mpn="0603WAF1001T5E"
      layer="bottom"
      pcbX="33mm"
      pcbY="15.5mm"
      connections={{ pin1: "net.P3V3_A", pin2: "net.BTN_LED" }}
    />
  </>
)

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
      pinCount={6}
      mpn="K6-1212A02"
      supplierPartNumbers={{ jlcpcb: ["C692901"] }}
      pinLabels={{ pin1: "swA1", pin2: "swA2", pin3: "swB1", pin4: "swB2", pin5: "led1", pin6: "led2" }}
      pcbX={`${BUTTON.x}mm`}
      pcbY={`${BUTTON.y}mm`}
      schPinArrangement={{ leftSide: ["swA1", "swB1", "led1"], rightSide: ["swA2", "swB2", "led2"] }}
      connections={{
        // both pins of a rail are the same contact - wire them together
        swA1: `net.${BUTTON.net}`,
        swA2: `net.${BUTTON.net}`,
        swB1: "net.GND",
        swB2: "net.GND",
        led1: "net.BTN_LED",
        led2: `net.${BUTTON.net}`,
      }}
    />

    <resistor
      name="R7"
      resistance={BUTTON.seriesR}
      footprint={FP_R0603}
      mpn="0603WAF1001T5E"
      supplierPartNumbers={{ jlcpcb: ["C21190"] }}
      layer="bottom"
      pcbX="33mm"
      pcbY="13.5mm"
      connections={{ pin1: "net.P3V3_A", pin2: "net.BTN_LED" }}
    />
  </>
)

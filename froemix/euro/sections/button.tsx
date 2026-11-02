// Froemix euro board - illuminated MODE button on M_ADC_7.
//
// R8 supplies the pull-up; the button LED indicates a press without a GPIO:
//
//   P3V3_A -- 1k -- LED(A) -- LED(K) --+-- M_ADC_7 (mux channel 7)
//   P3V3_A -- 10k ----------------------+-- switch -- GND
//
// Released: the node approaches P3V3_A, subject to leakage and mux sampling.
// Pressed: nominally 0V, with approximately 1.3mA through the LED (assuming
// 2V forward voltage) plus 0.33mA through R8. Verify brightness and settling.

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

    {/* Real pull-up. The LED is only an indicator; relying on its leakage left
        the released mux node at an undefined intermediate voltage. */}
    <resistor
      name="R8"
      resistance={BUTTON.pullupR}
      footprint={FP_R0603}
      mpn="0603WAF1002T5E"
      supplierPartNumbers={{ jlcpcb: ["C25804"] }}
      layer="bottom"
      pcbX="28mm"
      pcbY="16.5mm"
      connections={{ pin1: "net.P3V3_A", pin2: `net.${BUTTON.net}` }}
    />
  </>
)

// Froemix euro board (front board) - 1.3" OLED module + panel indicator LED.
//
// A 1x7 2.54mm female socket is fitted on the control-board TOP. Its exact
// body height, mating pins, insertion and display Z are not yet qualified.
// CN4 is the base-board interface socket on the opposite side; the 7 signals
// run straight across between the 2 (doc/EURO-BOARD.md §4).
//
// Module pin order (its own header): 1 GND, 2 VCC, 3 SCL, 4 SDA, 5 RES, 6 DC,
// 7 CS - which is not CN4's order, hence the explicit wiring below.

import { LED_AT, LED_R_AT, OLED_AT } from "../lib/layout"
import { ENVELOPE_OLED_MODULE, LED_3MM_MODEL, R0603_MODEL } from "../lib/cad-models"
import { FP_LED_3MM, FP_OLED_1P3, FP_R0603 } from "../lib/footprints"

export const DisplaySection = () => (
  <>
    <connector
      name="DS1"
      footprint={FP_OLED_1P3}
      pinCount={7}
      mpn="HS13L02B4S01"
      supplierPartNumbers={{ jlcpcb: ["C42432559"] }}
      cadModel={{
        objUrl: ENVELOPE_OLED_MODULE,
        modelOriginPosition: { x: 0, y: 0, z: 0 },
      }}
      pcbX={`${OLED_AT.x}mm`}
      pcbY={`${OLED_AT.y}mm`}
      schPinArrangement={{ leftSide: [1, 3, 5, 7], rightSide: [2, 4, 6] }}
      connections={{
        pin1: "net.GND",
        pin2: "net.P3V3_D",
        pin3: "net.OLED_SCL",
        pin4: "net.OLED_SDA",
        pin5: "net.OLED_RES",
        pin6: "net.OLED_DC",
        pin7: "net.OLED_CS",
      }}
    />

    {/* panel indicator: `LED` (Daisy ADC_1) -> 1k -> LED -> GND, ~1.3mA */}
    <resistor
      name="R1"
      resistance="1k"
      footprint={FP_R0603}
      mpn="0603WAF1001T5E"
      supplierPartNumbers={{ jlcpcb: ["C21190"] }}
      cadModel={R0603_MODEL}
      layer="bottom"
      pcbX={`${LED_R_AT.x}mm`}
      pcbY={`${LED_R_AT.y}mm`}
      connections={{ pin1: "net.LED", pin2: "net.LED_ANODE" }}
    />
    <led
      name="D1"
      footprint={FP_LED_3MM}
      mpn="TJ-L3FYTLDRMFCSFLC2R-A5"
      supplierPartNumbers={{ jlcpcb: ["C440523"] }}
      cadModel={LED_3MM_MODEL}
      pcbX={`${LED_AT.x}mm`}
      pcbY={`${LED_AT.y}mm`}
      pinLabels={{ pin1: "anode", pin2: "cathode" }}
      connections={{ anode: "net.LED_ANODE", cathode: "net.GND" }}
    />
  </>
)

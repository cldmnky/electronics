// Froemix euro board (front board) - the 8 panel pots.
//
// 10k linear, wired as a divider across P3V3_A / GND, so the wiper can never
// leave 0-3.3V and the CD4051 mux on the base board (and the Seed ADC behind it)
// stays inside its range - froemix/AGENTS.md §4, "Mux input clamps".
// 10k keeps the source impedance the mux sees at <=2.5k and the total pot
// current at 2.6mA. See doc/EURO-BOARD.md §2.
//
// pin1 = CCW (GND), pin2 = wiper (M_ADC_n), pin3 = CW (3.3V), so turning
// clockwise raises the value the firmware reads.

import { POTS } from "../lib/layout"
import { FP_POT_RK09K_R90 } from "../lib/footprints"

export const PotsSection = () => (
  <>
    {POTS.map((pot) => (
      <potentiometer
        key={pot.name}
        name={pot.name}
        footprint={FP_POT_RK09K_R90}
        maxResistance="10k"
        mpn="RK09K1130A5R"
        pcbX={`${pot.x}mm`}
        pcbY={`${pot.y}mm`}
        schPinArrangement={{ leftSide: [1, 3], rightSide: [2] }}
        connections={{
          pin1: "net.GND",
          pin2: `net.${pot.net}`,
          pin3: "net.P3V3_A",
        }}
      />
    ))}
  </>
)

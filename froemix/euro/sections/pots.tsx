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
import { easyedaStep } from "../lib/cad-models"
import { FP_POT_RK09K_R90 } from "../lib/footprints"

export const PotsSection = () => (
  <>
    {POTS.map((pot) => (
      <potentiometer
        key={pot.name}
        name={pot.name}
        footprint={FP_POT_RK09K_R90}
        maxResistance="10k"
        mpn="RK09K1130A8G"
        supplierPartNumbers={{ jlcpcb: ["C470294"] }}
        /* The EasyEDA model's own frame sits 8.5mm below the board plane with
           the shaft axis at (0, 0.51) - measured from the STEP on 2026-11-17
           (lowest geometry is the mounting-lug/pin plane at z=8.5; bushing and
           shaft cylinders are concentric about (0, 0.51)). Without this the
           pots render floating 8.5mm above the board. */
        cadModel={{
          stepUrl: easyedaStep("C470294"),
          modelOriginPosition: { x: 0, y: 0.51, z: 8.5 },
        }}
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

// Froemix euro board - UI joystick (5-way multi-control switch) on M_ADC_6.
//
// One mux channel carries five controls as a resistor ladder:
//
//   P3V3_A -- 10k --+-- M_ADC_6 (mux channel 6)
//                   |
//                   +-- switch contact -- resistor -- GND, one per direction
//
// Levels at the ADC (ignoring the CD4051's ~250R Ron, a ~2% error):
//   idle 3.30V | press 0.00V | up 0.30V | down 0.82V | left 1.34V | right 1.98V
//
// The press contact has no resistor: it shorts the node to GND. All five
// contacts share the switch's common pin, so the ladder needs no extra net per
// direction beyond the resistor node.

import { JOYSTICK } from "../lib/layout"
import { FP_JOY_5WAY, FP_R0603 } from "../lib/footprints"

export const JoystickSection = () => (
  <>
    <connector
      name={JOYSTICK.name}
      footprint={FP_JOY_5WAY}
      pinCount={6}
      mpn="SKQUCAA010"
      pinLabels={{
        pin1: "common",
        pin2: "up",
        pin3: "down",
        pin4: "left",
        pin5: "right",
        pin6: "press",
      }}
      pcbX={`${JOYSTICK.x}mm`}
      pcbY={`${JOYSTICK.y}mm`}
      schPinArrangement={{
        leftSide: ["common", "press"],
        rightSide: ["up", "down", "left", "right"],
      }}
      connections={{
        common: `net.${JOYSTICK.net}`,
        up: "net.JOY_UP",
        down: "net.JOY_DOWN",
        left: "net.JOY_LEFT",
        right: "net.JOY_RIGHT",
        press: "net.GND",
      }}
    />

    {/* pull-up: idle level is full scale on the mux channel */}
    <resistor
      name="R2"
      resistance={JOYSTICK.pullup}
      footprint={FP_R0603}
      mpn="0603WAF1002T5E"
      layer="bottom"
      pcbX="-33mm"
      pcbY="18mm"
      connections={{ pin1: "net.P3V3_A", pin2: `net.${JOYSTICK.net}` }}
    />

    {/* ladder: one resistor per direction, each to GND */}
    {["up", "down", "left", "right"].map((dir, i) => (
      <resistor
        key={dir}
        name={`R${3 + i}`}
        resistance={JOYSTICK.ladder[i].r}
        footprint={FP_R0603}
        mpn="0603WAF1002T5E"
        layer="bottom"
        pcbX={`${-29.5 + i * 3.5}mm`}
        pcbY="18mm"
        connections={{ pin1: `net.JOY_${dir.toUpperCase()}`, pin2: "net.GND" }}
      />
    ))}
  </>
)

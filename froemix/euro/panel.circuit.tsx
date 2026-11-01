// Froemix euro board - the FRONT PANEL (20HP x 3U, 2mm FR4).
//
// This is a mechanical board: no copper, no nets. Only the outline, the holes
// and the silkscreen. Every hole is read from lib/layout.ts, the same table the
// front board (`index.circuit.tsx`) places its parts from, so panel and board
// cannot drift apart. Stack-up and the open questions: froemix/doc/EURO-BOARD.md.
//
// Cutouts/holes:
//   4x M3 rail screws   (+/-43.3, +/-61.25)   Doepfer 3U
//   8x pot bushings     M7                     Alpha 9mm
//  12x jack bushings    6.2mm                  PJ398SM (mono) / PJ320A (TRS)
//   1x 3mm LED
//   1x OLED window      30 x 15.5mm           1.3" 128x64 module

import {
  BOARD_H,
  BOARD_W,
  BUTTON,
  HOLE,
  JACKS,
  JACK_LEDS,
  JOYSTICK,
  LED_AT,
  OLED_WINDOW,
  OLED,
  POTS,
  RAIL_HOLES,
} from "./lib/layout"

export default () => (
  <board width={`${BOARD_W}mm`} height={`${BOARD_H}mm`} thickness="2mm">
    {/* rack screws */}
    {RAIL_HOLES.map((h, i) => (
      <hole key={`rail-${i}`} diameter={`${HOLE.rail}mm`} pcbX={`${h.x}mm`} pcbY={`${h.y}mm`} />
    ))}

    {/* jack bushings */}
    {JACKS.map((j) => (
      <hole
        key={`jack-${j.name}`}
        diameter={`${HOLE.jack}mm`}
        pcbX={`${j.x}mm`}
        pcbY={`${j.y}mm`}
      />
    ))}

    {/* pot bushings */}
    {POTS.map((p) => (
      <hole
        key={`pot-${p.name}`}
        diameter={`${HOLE.pot}mm`}
        pcbX={`${p.x}mm`}
        pcbY={`${p.y}mm`}
      />
    ))}

    {/* UI joystick + illuminated MODE button */}
    <hole
      diameter={`${HOLE.joy}mm`}
      pcbX={`${JOYSTICK.x}mm`}
      pcbY={`${JOYSTICK.y}mm`}
    />
    <hole
      diameter={`${HOLE.button}mm`}
      pcbX={`${BUTTON.x}mm`}
      pcbY={`${BUTTON.y}mm`}
    />

    {/* signal-activity LEDs, one above each watched jack */}
    {JACK_LEDS.map((l) => (
      <hole
        key={`jackled-${l.name}`}
        diameter={`${HOLE.jackLed}mm`}
        pcbX={`${l.x}mm`}
        pcbY={`${l.y}mm`}
      />
    ))}

    {/* panel indicator LED */}
    <hole
      diameter={`${HOLE.led}mm`}
      pcbX={`${LED_AT.x}mm`}
      pcbY={`${LED_AT.y}mm`}
    />

    {/* 1.3" display window */}
    <cutout
      shape="rect"
      width={`${OLED.windowW}mm`}
      height={`${OLED.windowH}mm`}
      pcbX={`${OLED_WINDOW.x}mm`}
      pcbY={`${OLED_WINDOW.y}mm`}
    />

    {/* labels under each jack and pot */}
    {JACKS.map((j) => (
      <silkscreentext
        key={`lbl-${j.name}`}
        text={j.label}
        fontSize="1.1mm"
        anchorAlignment="center"
        pcbX={`${j.x}mm`}
        pcbY={`${j.y - 8.6}mm`}
      />
    ))}
    {POTS.map((p) => (
      <silkscreentext
        key={`lbl-${p.name}`}
        text={p.label}
        fontSize="1.1mm"
        anchorAlignment="center"
        pcbX={`${p.x}mm`}
        pcbY={`${p.y - 9.6}mm`}
      />
    ))}

    {/* UI control labels */}
    <silkscreentext
      text={JOYSTICK.label}
      fontSize="1.2mm"
      anchorAlignment="center"
      pcbX={`${JOYSTICK.x}mm`}
      pcbY={`${JOYSTICK.y - 9.6}mm`}
    />
    <silkscreentext
      text={BUTTON.label}
      fontSize="1.2mm"
      anchorAlignment="center"
      pcbX={`${BUTTON.x}mm`}
      pcbY={`${BUTTON.y - 9.6}mm`}
    />

    <silkscreentext
      text="FROEMIX"
      fontSize="2.6mm"
      anchorAlignment="center"
      pcbX="0mm"
      pcbY="-59.8mm"
    />
    <silkscreentext
      text="daisy seed dsp - 20hp"
      fontSize="1.2mm"
      anchorAlignment="center"
      pcbX="0mm"
      pcbY="-62.4mm"
    />
    <silkscreentext
      text="REV 0.1"
      fontSize="1.2mm"
      anchorAlignment="center"
      pcbX="0mm"
      pcbY="61.6mm"
    />
  </board>
)

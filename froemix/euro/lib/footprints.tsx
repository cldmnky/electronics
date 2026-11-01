// Froemix euro board (front board) - footprints.
//
// Pad geometry follows the base board's `tscircuit/lib/footprints.tsx`
// convention: `<platedhole>`/`<smtpad>` with `portHints`, a silk outline and a
// courtyard. Through-hole diameters are assembly-critical, never round them.
//
// MIRRORING (froemix/AGENTS.md §5): a footprint on the BOTTOM side is mirrored
// about its origin - a pad at local x lands at `origin - x`. The three mating
// sockets therefore use pre-mirrored pad order (pin 1 at +X), so that after the
// flip pin 1 of the socket sits on pin 1 of the base board's header.
//
// n.b. every rect-pad plated hole carries holeOffsetX/Y="0mm": the copper pour
// pass in @tscircuit/core dies without them (froemix/AGENTS.md §3).

// --------------------------------------------------------------------------
// 2.00mm socket, 1xN - the mate of the base board's A2005WV pin headers.
// Same pad as the header (1.0mm drill, 1.52mm pad) so one drill table fits both
// boards and any 2.00mm socket family works. Bottom side: pin 1 at +X.
// --------------------------------------------------------------------------

const socketPads = (pins: number, mirrored: boolean) =>
  Array.from({ length: pins }, (_, i) => {
    const pin = i + 1
    const centre = ((pins - 1) * 2.0) / 2
    const x = -centre + i * 2.0
    return { pin, x: mirrored ? -x : x }
  })

export const FP_SOCKET_2P00_16 = (
  <footprint insertionDirection="from_above">
    {socketPads(16, true).map(({ pin, x }) =>
      pin === 1 ? (
        <platedhole
          key={pin}
          pcbX={`${x}mm`}
          pcbY="0mm"
          portHints={["pin1"]}
          shape="circular_hole_with_rect_pad"
          holeDiameter="1mm"
          rectPadWidth="1.52mm"
          rectPadHeight="1.52mm"
          holeOffsetX="0mm"
          holeOffsetY="0mm"
        />
      ) : (
        <platedhole
          key={pin}
          pcbX={`${x}mm`}
          pcbY="0mm"
          portHints={[`pin${pin}`]}
          shape="circle"
          holeDiameter="1mm"
          outerDiameter="1.52mm"
        />
      ),
    )}
    <silkscreenpath route={[{ x: 16.06, y: 1.01 }, { x: -16.07, y: 1.01 }]} />
    <silkscreenpath route={[{ x: -16.07, y: 1.01 }, { x: -16.07, y: -1.02 }]} />
    <silkscreenpath route={[{ x: -16.07, y: -1.02 }, { x: 16.06, y: -1.02 }]} />
    <silkscreenpath route={[{ x: 16.06, y: -1.02 }, { x: 16.06, y: 1.01 }]} />
    <silkscreenpath route={[{ x: 14, y: -1.02 }, { x: 14, y: 1.01 }]} />
    <courtyardrect pcbX="0mm" pcbY="0mm" width="32.63mm" height="2.53mm" />
  </footprint>
)

export const FP_SOCKET_2P00_7 = (
  <footprint insertionDirection="from_above">
    {socketPads(7, true).map(({ pin, x }) =>
      pin === 1 ? (
        <platedhole
          key={pin}
          pcbX={`${x}mm`}
          pcbY="0mm"
          portHints={["pin1"]}
          shape="circular_hole_with_rect_pad"
          holeDiameter="1mm"
          rectPadWidth="1.52mm"
          rectPadHeight="1.52mm"
          holeOffsetX="0mm"
          holeOffsetY="0mm"
        />
      ) : (
        <platedhole
          key={pin}
          pcbX={`${x}mm`}
          pcbY="0mm"
          portHints={[`pin${pin}`]}
          shape="circle"
          holeDiameter="1mm"
          outerDiameter="1.52mm"
        />
      ),
    )}
    <silkscreenpath route={[{ x: 7.06, y: 1.01 }, { x: -7.07, y: 1.01 }]} />
    <silkscreenpath route={[{ x: -7.07, y: 1.01 }, { x: -7.07, y: -1.02 }]} />
    <silkscreenpath route={[{ x: -7.07, y: -1.02 }, { x: 7.06, y: -1.02 }]} />
    <silkscreenpath route={[{ x: 7.06, y: -1.02 }, { x: 7.06, y: 1.01 }]} />
    <silkscreenpath route={[{ x: 5, y: -1.02 }, { x: 5, y: 1.01 }]} />
    <courtyardrect pcbX="0mm" pcbY="0mm" width="14.63mm" height="2.53mm" />
  </footprint>
)

// --------------------------------------------------------------------------
// 1.3" 128x64 SPI OLED module - 7 pads at 2.54mm along the header row, plus the
// module body as a courtyard so nothing else lands under the glass. Pins are
// numbered in the module's own order: 1 GND, 2 VCC, 3 SCL, 4 SDA, 5 RES,
// 6 DC, 7 CS.
// --------------------------------------------------------------------------

export const FP_OLED_1P3 = (
  <footprint insertionDirection="from_above">
    {Array.from({ length: 7 }, (_, i) => {
      const pin = i + 1
      const x = -7.62 + i * 2.54
      return pin === 1 ? (
        <platedhole
          key={pin}
          pcbX={`${x}mm`}
          pcbY="0mm"
          portHints={["pin1"]}
          shape="circular_hole_with_rect_pad"
          holeDiameter="1mm"
          rectPadWidth="1.6mm"
          rectPadHeight="1.6mm"
          holeOffsetX="0mm"
          holeOffsetY="0mm"
        />
      ) : (
        <platedhole
          key={pin}
          pcbX={`${x}mm`}
          pcbY="0mm"
          portHints={[`pin${pin}`]}
          shape="circle"
          holeDiameter="1mm"
          outerDiameter="1.6mm"
        />
      )
    })}
    {/* module body outline, 35.5 x 33mm hanging below the header row */}
    <silkscreenpath route={[{ x: -17.75, y: 0 }, { x: 17.75, y: 0 }]} />
    <silkscreenpath route={[{ x: -17.75, y: 0 }, { x: -17.75, y: -33 }]} />
    <silkscreenpath route={[{ x: -17.75, y: -33 }, { x: 17.75, y: -33 }]} />
    <silkscreenpath route={[{ x: 17.75, y: -33 }, { x: 17.75, y: 0 }]} />
    <courtyardrect pcbX="0mm" pcbY="-16.5mm" width="35.5mm" height="33mm" />
  </footprint>
)

// --------------------------------------------------------------------------
// 0603 resistor (KiCad R_0603_1608Metric)
// --------------------------------------------------------------------------

export const FP_R0603 = (
  <footprint insertionDirection="from_above">
    <smtpad portHints={["pin1"]} pcbX="-0.7875mm" pcbY="0mm" width="0.8mm" height="0.95mm" shape="rect" />
    <smtpad portHints={["pin2"]} pcbX="0.7875mm" pcbY="0mm" width="0.8mm" height="0.95mm" shape="rect" />
    <silkscreenpath route={[{ x: -0.325, y: 0.55 }, { x: 0.325, y: 0.55 }]} />
    <silkscreenpath route={[{ x: -0.325, y: -0.55 }, { x: 0.325, y: -0.55 }]} />
    <courtyardrect pcbX="0mm" pcbY="0mm" width="1.975mm" height="1.075mm" />
  </footprint>
)

// --------------------------------------------------------------------------
// 3mm THT LED (KiCad LED_D3.0mm). pin1 = cathode (K), pin2 = anode (A).
// --------------------------------------------------------------------------

export const FP_LED_3MM = (
  <footprint insertionDirection="from_above">
    <platedhole
      pcbX="-1.27mm"
      pcbY="0mm"
      portHints={["pin1"]}
      shape="circular_hole_with_rect_pad"
      holeDiameter="1mm"
      rectPadWidth="1.8mm"
      rectPadHeight="1.8mm"
      holeOffsetX="0mm"
      holeOffsetY="0mm"
    />
    <platedhole
      pcbX="1.27mm"
      pcbY="0mm"
      portHints={["pin2"]}
      shape="circle"
      holeDiameter="1mm"
      outerDiameter="1.8mm"
    />
    <silkscreencircle pcbX="0mm" pcbY="0mm" radius="1.5mm" />
    <courtyardrect pcbX="0mm" pcbY="0mm" width="4.2mm" height="3.2mm" />
  </footprint>
)

// --------------------------------------------------------------------------
// Thonkiconn PJ398SM mono 3.5mm jack (official KiCad footprint
// Jack_3.5mm_QingPu_WQP-PJ398SM_Vertical_CircularHoles, pads S/T/TN).
//
// Origin = the jack barrel (the panel hole), not pad S: the KiCad footprint's
// origin is 6.48mm away from the barrel, so every pad is shifted by that much
// and the Y axis is flipped (KiCad is Y-down). Padding the barrel at the origin
// is what lets lib/layout.ts hold one coordinate per part, shared with the panel.
//
// pin1 = S (sleeve), pin2 = T (tip), pin3 = TN (tip switch). The switch carries
// no port: it is a normal contact that this design does not use.
// --------------------------------------------------------------------------

export const FP_JACK_MONO = (
  <footprint insertionDirection="from_above">
    <platedhole
      pcbX="0mm"
      pcbY="6.48mm"
      portHints={["pin1"]}
      shape="circular_hole_with_rect_pad"
      holeDiameter="1.22mm"
      rectPadWidth="1.93mm"
      rectPadHeight="1.83mm"
      holeOffsetX="0mm"
      holeOffsetY="0mm"
    />
    <platedhole
      pcbX="0mm"
      pcbY="-4.92mm"
      portHints={["pin2"]}
      shape="circle"
      holeDiameter="1.43mm"
      outerDiameter="2.13mm"
    />
    {/* tip switch - no port, not used */}
    <platedhole
      pcbX="0mm"
      pcbY="3.38mm"
      shape="circle"
      holeDiameter="1.42mm"
      outerDiameter="2.13mm"
    />
    {/* body 9.0 x 10.45mm, barrel towards -Y */}
    <silkscreenpath route={[{ x: -4.5, y: -6.0 }, { x: 4.5, y: -6.0 }]} />
    <silkscreenpath route={[{ x: -4.5, y: -6.0 }, { x: -4.5, y: 4.45 }]} />
    <silkscreenpath route={[{ x: 4.5, y: -6.0 }, { x: 4.5, y: 4.45 }]} />
    <silkscreenpath route={[{ x: -4.5, y: 4.45 }, { x: 4.5, y: 4.45 }]} />
    <silkscreencircle pcbX="0mm" pcbY="0mm" radius="3mm" />
    <courtyardrect pcbX="0mm" pcbY="0.7mm" width="10mm" height="14.4mm" />
  </footprint>
)

// --------------------------------------------------------------------------
// Thonkiconn PJ366ST / WQP419GR stereo (TRS) 3.5mm jack - the TRS-MIDI socket.
// Author: clacktronics/AudioJacks, `Jack_3.5mm_QingPu_WQP-PJ366ST_Vertical`
// (legacy `(module ...)` file, numbers transcribed).
//
// Origin = the jack barrel. Pads are slotted (1.6 x 0.6mm) with 2.6 x 2.6mm
// pads; tscircuit models a slot as an oval plated hole.
// pin1 = T (tip), pin2 = R (ring), pin3 = S (sleeve).
// --------------------------------------------------------------------------

export const FP_JACK_TRS = (
  <footprint insertionDirection="from_above">
    <platedhole
      pcbX="0mm"
      pcbY="4.92mm"
      portHints={["pin1"]}
      shape="oval"
      holeWidth="1.6mm"
      holeHeight="0.6mm"
      outerWidth="2.6mm"
      outerHeight="2.6mm"
    />
    <platedhole
      pcbX="0mm"
      pcbY="-3.38mm"
      portHints={["pin2"]}
      shape="oval"
      holeWidth="1.6mm"
      holeHeight="0.6mm"
      outerWidth="2.6mm"
      outerHeight="2.6mm"
    />
    <platedhole
      pcbX="0mm"
      pcbY="-6.48mm"
      portHints={["pin3"]}
      shape="oval"
      holeWidth="1.6mm"
      holeHeight="0.6mm"
      outerWidth="3.1mm"
      outerHeight="2.1mm"
    />
    <silkscreenpath route={[{ x: -4.5, y: -4.47 }, { x: 4.5, y: -4.47 }]} />
    <silkscreenpath route={[{ x: -4.5, y: -4.47 }, { x: -4.5, y: 5.98 }]} />
    <silkscreenpath route={[{ x: 4.5, y: -4.47 }, { x: 4.5, y: 5.98 }]} />
    <silkscreenpath route={[{ x: -4.5, y: 5.98 }, { x: 4.5, y: 5.98 }]} />
    <silkscreencircle pcbX="0mm" pcbY="0mm" radius="3mm" />
    <courtyardrect pcbX="0mm" pcbY="-0.165mm" width="10mm" height="14.83mm" />
  </footprint>
)

// --------------------------------------------------------------------------
// Alps RK09K1130A5R 9mm single potentiometer, vertical (official KiCad
// footprint Potentiometer_Alps_RK09K_Single_Vertical).
//
// Origin = the shaft axis. In the KiCad library the origin is pad 1, 7.5mm in X
// and 2.5mm in Y away from the shaft; the pads below are therefore at -7.5mm in
// X. The two MP mounting lugs are slotted holes (2.1 x 1.8mm) and carry no port.
// pin1 = CCW, pin2 = wiper, pin3 = CW.
// --------------------------------------------------------------------------

export const FP_POT_RK09K = (
  <footprint insertionDirection="from_above">
    <platedhole
      pcbX="-7.5mm"
      pcbY="2.5mm"
      portHints={["pin1"]}
      shape="circular_hole_with_rect_pad"
      holeDiameter="1mm"
      rectPadWidth="1.8mm"
      rectPadHeight="1.8mm"
      holeOffsetX="0mm"
      holeOffsetY="0mm"
    />
    <platedhole
      pcbX="-7.5mm"
      pcbY="0mm"
      portHints={["pin2"]}
      shape="circle"
      holeDiameter="1mm"
      outerDiameter="1.8mm"
    />
    <platedhole
      pcbX="-7.5mm"
      pcbY="-2.5mm"
      portHints={["pin3"]}
      shape="circle"
      holeDiameter="1mm"
      outerDiameter="1.8mm"
    />
    {/* mounting lugs - no ports */}
    <platedhole
      pcbX="-0.5mm"
      pcbY="4.4mm"
      shape="oval"
      holeWidth="2.1mm"
      holeHeight="1.8mm"
      outerWidth="4mm"
      outerHeight="3mm"
    />
    <platedhole
      pcbX="-0.5mm"
      pcbY="-4.4mm"
      shape="oval"
      holeWidth="2.1mm"
      holeHeight="1.8mm"
      outerWidth="4mm"
      outerHeight="3mm"
    />
    {/* body 12 x 9.8mm, shaft towards -X */}
    <silkscreenpath route={[{ x: -6.5, y: -4.9 }, { x: 5.5, y: -4.9 }]} />
    <silkscreenpath route={[{ x: -6.5, y: -4.9 }, { x: -6.5, y: 4.9 }]} />
    <silkscreenpath route={[{ x: 5.5, y: -4.9 }, { x: 5.5, y: 4.9 }]} />
    <silkscreenpath route={[{ x: -6.5, y: 4.9 }, { x: 5.5, y: 4.9 }]} />
    <silkscreencircle pcbX="0mm" pcbY="0mm" radius="3mm" />
    <courtyardrect pcbX="0mm" pcbY="0mm" width="14.4mm" height="13.3mm" />
  </footprint>
)

// --------------------------------------------------------------------------
// The same Alps RK09K pot rotated +90 degrees so its 13.3mm body width lies
// along X: eight pots then fit at a 15mm pitch with their courtyards clear of
// each other and of the OLED module. tscircuit rotates courtyard *centres* but
// keeps their axes (froemix/AGENTS.md §3), so the rotation is baked into the
// pads/silk/courtyard here and every pot is placed with rotation 0.
//
// pin1 (CCW) is now at -X, pin2 (wiper) at -Y, pin3 (CW) at +X.
// --------------------------------------------------------------------------

export const FP_POT_RK09K_R90 = (
  <footprint insertionDirection="from_above">
    <platedhole
      pcbX="-2.5mm"
      pcbY="-7.5mm"
      portHints={["pin1"]}
      shape="circular_hole_with_rect_pad"
      holeDiameter="1mm"
      rectPadWidth="1.8mm"
      rectPadHeight="1.8mm"
      holeOffsetX="0mm"
      holeOffsetY="0mm"
    />
    <platedhole
      pcbX="0mm"
      pcbY="-7.5mm"
      portHints={["pin2"]}
      shape="circle"
      holeDiameter="1mm"
      outerDiameter="1.8mm"
    />
    <platedhole
      pcbX="2.5mm"
      pcbY="-7.5mm"
      portHints={["pin3"]}
      shape="circle"
      holeDiameter="1mm"
      outerDiameter="1.8mm"
    />
    <platedhole
      pcbX="-4.4mm"
      pcbY="-0.5mm"
      shape="oval"
      holeWidth="1.8mm"
      holeHeight="2.1mm"
      outerWidth="3mm"
      outerHeight="4mm"
    />
    <platedhole
      pcbX="4.4mm"
      pcbY="-0.5mm"
      shape="oval"
      holeWidth="1.8mm"
      holeHeight="2.1mm"
      outerWidth="3mm"
      outerHeight="4mm"
    />
    <silkscreenpath route={[{ x: -4.9, y: -6.5 }, { x: 4.9, y: -6.5 }]} />
    <silkscreenpath route={[{ x: -4.9, y: -6.5 }, { x: -4.9, y: 5.5 }]} />
    <silkscreenpath route={[{ x: 4.9, y: -6.5 }, { x: 4.9, y: 5.5 }]} />
    <silkscreenpath route={[{ x: -4.9, y: 5.5 }, { x: 4.9, y: 5.5 }]} />
    <silkscreencircle pcbX="0mm" pcbY="0mm" radius="3mm" />
    <courtyardrect pcbX="0mm" pcbY="-1.45mm" width="13.3mm" height="14.4mm" />
  </footprint>
)

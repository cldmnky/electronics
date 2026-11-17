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
    {/* Module: 35.4 x 33.5mm, header row 2.0mm from the connector-side edge
        (datasheet HS13L02B4S01). Body from +2.0 to -31.5mm. The module's four
        3.2mm mounting holes are NOT used: the module is soldered on its seven
        pins - add them if a bracket is ever wanted. */}
    <silkscreenpath route={[{ x: -17.7, y: 2 }, { x: 17.7, y: 2 }]} />
    <silkscreenpath route={[{ x: -17.7, y: 2 }, { x: -17.7, y: -31.5 }]} />
    <silkscreenpath route={[{ x: 17.7, y: 2 }, { x: 17.7, y: -31.5 }]} />
    <silkscreenpath route={[{ x: -17.7, y: -31.5 }, { x: 17.7, y: -31.5 }]} />
    {/* active area 29.42 x 14.70mm, centre 12.70mm below the header row */}
    <silkscreenpath route={[{ x: -14.7, y: -5.35 }, { x: 14.7, y: -5.35 }]} />
    <silkscreenpath route={[{ x: -14.7, y: -20.05 }, { x: 14.7, y: -20.05 }]} />
    <courtyardrect pcbX="0mm" pcbY="-14.75mm" width="35.4mm" height="33.5mm" />
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
// 3mm THT LED, geometry from KiCad LED_D3.0mm but pin1 = ANODE.
// The supplier part (LCSC C440523, TOGIALED 3mm red) maps pin 1 to the anode,
// and the build fails on a polarity mismatch, so the square pad (pin 1) marks
// the anode here - the opposite of the KiCad LED_D3.0mm convention.
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
// Mono 3.5mm Eurorack jack - QingPu/Thonkiconn `WQP518MA` (M6 x 0.5 bushing,
// PCB mount, 3 terminals: S, TN, T). Owner decision 2026-11-16: the front
// board uses the same Thonkiconn-family jacks as midibrain (library row
// `jack-35-wqp518ma`), replacing XKB `PJ-301M-2-B`.
//
// PAD LINE. Three pads in one line through the barrel datum: S at +6.48, TN at
// +3.38, T at -4.92mm, verified 2026-11-13 against clacktronics/AudioJacks
// (MIT) Jack_3.5mm_QingPu_WQP-PJ301M-12_Vertical and Thonk's PJ301M-12
// drawing (3.10 / 8.30 spacings); the same numbers as midibrain's stereo
// footprint (which sees the part rotated 180 degrees in-plane, S at -6.48).
//
// Slots, not round holes: the terminals are flat blades (0.5 x 1.5, 0.6 x 1.5
// and 0.6 x 1.3mm per Thonk's PCB layout), so the hole is 1.6 x 0.6mm and the
// land 2.6 x 2.6mm (sleeve 3.1 x 2.1) - the library CAD's table
// (library/cad/jack-35-wqp518ma). The retired PJ-301M-2-B pads were 1.2 x
// 2.0mm ovals, which a 1.5mm blade cannot enter.
//
// Origin = the jack barrel (the panel hole), not pad S: that is what lets
// lib/layout.ts hold one coordinate per part, shared with the panel.
//
// pin1 = S (sleeve), pin2 = T (tip), pin3 = TN (tip switch). The switch carries
// no port: it is a normal contact that this design does not use.
//
// Body outline is not drawn: the clacktronics CAD's outline is untested (its
// README says so) and midibrain's inspection of the sibling WQP419GR found a
// different body, so nothing is drawn rather than something wrong. The board
// silk still carries the refdes and the bushing circle.
//
// 3D: PJ301M-12.step from clacktronics/AudioJacks (MIT, SHA-256 in
// library/cad/jack-35-wqp518ma/SOURCE.md), pinned to commit 14a88866. It does
// not mesh in the tscircuit viewer (2026-11-17), so the sections use a
// drawing-derived envelope instead; the STEP remains the provenance copy for
// CAD tools.
// --------------------------------------------------------------------------

export const FP_JACK_MONO = (
  <footprint insertionDirection="from_above">
    <platedhole
      pcbX="0mm"
      pcbY="6.48mm"
      portHints={["pin1"]}
      shape="oval"
      holeWidth="1.6mm"
      holeHeight="0.6mm"
      outerWidth="3.1mm"
      outerHeight="2.1mm"
    />
    <platedhole
      pcbX="0mm"
      pcbY="-4.92mm"
      portHints={["pin2"]}
      shape="oval"
      holeWidth="1.6mm"
      holeHeight="0.6mm"
      outerWidth="2.6mm"
      outerHeight="2.6mm"
    />
    {/* tip switch - no port, not used */}
    <platedhole
      pcbX="0mm"
      pcbY="3.38mm"
      shape="oval"
      holeWidth="1.6mm"
      holeHeight="0.6mm"
      outerWidth="2.6mm"
      outerHeight="2.6mm"
    />
    <silkscreencircle pcbX="0mm" pcbY="0mm" radius="3mm" />
    <courtyardrect pcbX="0mm" pcbY="0.7mm" width="10mm" height="14.4mm" />
  </footprint>
)

// --------------------------------------------------------------------------
// Stereo (TRS) 3.5mm jack - the TRS-MIDI socket. QingPu/Thonkiconn `WQP419GR`
// (M6 x 0.5 bushing, PCB mount, 3 terminals: S, R, T). Owner decision
// 2026-11-16: same Thonkiconn family as midibrain (library row
// `jack-35-wqp419gr`), replacing XKB `PJ-301M-3-B`.
//
// Same slotted pads as the mono jack (see above): 1.6 x 0.6mm holes with
// 2.6 x 2.6mm lands (sleeve 3.1 x 2.1), from the library CAD table
// (library/cad/jack-35-wqp419gr) and Thonk's drawing's blade sizes.
//
// Origin = the jack barrel. The pads here are the part rotated 180 degrees
// in-plane relative to midibrain's FP_JACK_TRS bookkeeping: pin1 = T at +4.92,
// pin2 = R at -3.38, pin3 = S at -6.48. The froemix pin numbering is kept (this
// board is documented "solder by function"); midibrain labels the same pads
// S/R/T and connects by letter. Netlist-wise nothing changes.
//
// Body outline is not drawn: the clacktronics CAD's outline is untested (its
// README says so) and does not match the observed WQP419GR body (about
// 10 x 7.8mm with the barrel 4.5mm from one edge), so nothing is drawn rather
// than something wrong. The board silk still carries the refdes, the THT hint
// and the bushing circle.
//
// 3D: PJ366ST.step from clacktronics/AudioJacks (MIT, SHA-256 in
// library/cad/jack-35-wqp419gr/SOURCE.md), pinned to commit 14a88866. It does
// not mesh in the tscircuit viewer (2026-11-17), so the sections use a
// drawing-derived envelope instead; the STEP remains the provenance copy for
// CAD tools.
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

// --------------------------------------------------------------------------
// 5-way navigation switch, XUNPU `WS-1004-ARL10026` (LCSC C42377836).
//
// Numbers from the datasheet's "Layout" view: two pad columns 10.50mm apart
// in X, three rows in Y. The rows are NOT symmetric: the printed 2.80mm and
// 3.70mm dims stack from the top row over the middle row to the bottom row,
// so with the middle row (pins 2/5) at y=0 the pin 1/4 row is at +2.80mm and
// the pin 3/6 row at -3.70mm (6.50mm overall). Verified against the drawing
// geometry itself: the pad centres measure 2.795mm and 3.697mm. Functions from
// the printed function table - pin 1 is the COMMON pin and every direction
// closes to it:
//   F1 up    = 1-4     F2 left  = 1-2     F3 down = 1-6
//   F4 right = 1-5     F5 press = 1-3
//
// The earlier revision of this footprint used a symmetric 1.85mm row pitch
// (3.70mm read as the overall span); the real part does not fit that.
// The pad map does not say which side faces the panel, so up/down may swap on
// the fitted part - a mounting/firmware detail, not a board change. The row
// ORDER (2.80 short gap toward pins 1/4) must match the part as mounted; if
// the fitted part's gaps come out reversed, mirror the footprint in Y.
// Drills 1.3mm and 2.0mm square pads are the JLCEDA/EasyEDA Official Library
// footprint for C42377836 (SW-TH_WS-1004-ARL10026), cross-checked 2026-11-17;
// its positions match the datasheet exactly. Source declaration required by
// that library's terms: "JLCEDA/EasyEDA Official Library", https://lceda.cn/
// and https://easyeda.com. The first revision's 1.0mm drills were sized from a
// 0.70mm pin-width reading the vendor CAD does not confirm - 1.3mm takes the
// blades either way.
// --------------------------------------------------------------------------

export const FP_JOY_5WAY = (
  <footprint insertionDirection="from_above">
    {/* pin1 common */}
    <platedhole
      pcbX="-5.25mm"
      pcbY="2.8mm"
      portHints={["pin1"]}
      shape="circular_hole_with_rect_pad"
      holeDiameter="1.3mm"
      rectPadWidth="2mm"
      rectPadHeight="2mm"
      holeOffsetX="0mm"
      holeOffsetY="0mm"
    />
    {/* pin2 left (F2) */}
    <platedhole pcbX="-5.25mm" pcbY="0mm" portHints={["pin2"]} shape="circle" holeDiameter="1.3mm" outerDiameter="2mm" />
    {/* pin3 centre press (F5) */}
    <platedhole pcbX="-5.25mm" pcbY="-3.7mm" portHints={["pin3"]} shape="circle" holeDiameter="1.3mm" outerDiameter="2mm" />
    {/* pin4 up (F1) */}
    <platedhole pcbX="5.25mm" pcbY="2.8mm" portHints={["pin4"]} shape="circle" holeDiameter="1.3mm" outerDiameter="2mm" />
    {/* pin5 right (F4) */}
    <platedhole pcbX="5.25mm" pcbY="0mm" portHints={["pin5"]} shape="circle" holeDiameter="1.3mm" outerDiameter="2mm" />
    {/* pin6 down (F3) */}
    <platedhole pcbX="5.25mm" pcbY="-3.7mm" portHints={["pin6"]} shape="circle" holeDiameter="1.3mm" outerDiameter="2mm" />
    {/* body 10.2 x 10.2mm, 3.15mm button on top */}
    <silkscreenpath route={[{ x: -5.1, y: 5.1 }, { x: 5.1, y: 5.1 }]} />
    <silkscreenpath route={[{ x: -5.1, y: 5.1 }, { x: -5.1, y: -5.1 }]} />
    <silkscreenpath route={[{ x: 5.1, y: 5.1 }, { x: 5.1, y: -5.1 }]} />
    <silkscreenpath route={[{ x: -5.1, y: -5.1 }, { x: 5.1, y: -5.1 }]} />
    <silkscreencircle pcbX="0mm" pcbY="0mm" radius="1.6mm" />
    <courtyardrect pcbX="0mm" pcbY="0mm" width="12.5mm" height="10.5mm" />
  </footprint>
)

export const FP_BUTTON_12MM = (
  <footprint insertionDirection="from_above">
    {/* switch rail 1 (C1, C2) - the pair is internally connected.
        Rows are 5.00mm apart (±2.50), columns 12.10mm (±6.05): the datasheet
        PCB layout dims 12.10 / 5.00±0.1 / 8.70 / 13.50 and the drawing
        geometry confirms the nested chain. Cross-checked 2026-11-17 against
        the JLCEDA/EasyEDA Official Library footprint for C692901
        (SW-TH_6P-L12.0-W12.0-P5.00-LS12.1-TL; source declaration required by
        that library's terms: "JLCEDA/EasyEDA Official Library", https://lceda.cn/
        and https://easyeda.com): identical positions. The earlier revision had
        8.70 on the switch rows and 3.50 on the LED pins - the real part does
        not fit that. */}
    <platedhole
      pcbX="-6.05mm"
      pcbY="2.5mm"
      portHints={["pin1"]}
      shape="circular_hole_with_rect_pad"
      holeDiameter="1.3mm"
      rectPadWidth="2mm"
      rectPadHeight="2mm"
      holeOffsetX="0mm"
      holeOffsetY="0mm"
    />
    <platedhole pcbX="6.05mm" pcbY="2.5mm" portHints={["pin2"]} shape="circle" holeDiameter="1.3mm" outerDiameter="2mm" />
    {/* switch rail 2 (C3, C4) */}
    <platedhole pcbX="-6.05mm" pcbY="-2.5mm" portHints={["pin3"]} shape="circle" holeDiameter="1.3mm" outerDiameter="2mm" />
    <platedhole pcbX="6.05mm" pcbY="-2.5mm" portHints={["pin4"]} shape="circle" holeDiameter="1.3mm" outerDiameter="2mm" />
    {/* LED pins at 13.50mm spacing (±6.75), Ø1.0 holes: the LED leads are thin
        0.50 x 0.20mm flats (drawing side view), and the EasyEDA CAD puts the
        diode (symbol pins 5/6) on these pads. The 8.70 pair is NOT the LED -
        see the peg note below. Polarity is NOT in the datasheet: check the part
        before soldering. */}
    <platedhole pcbX="0mm" pcbY="-6.75mm" portHints={["pin5"]} shape="circle" holeDiameter="1mm" outerDiameter="2mm" />
    <platedhole pcbX="0mm" pcbY="6.75mm" portHints={["pin6"]} shape="circle" holeDiameter="1mm" outerDiameter="2mm" />
    {/* Locating pegs at (0, +/-4.35), Ø1.9 holes (drawing calls the holes
        Ø1.80; EasyEDA uses 1.9 NPTH). These plastic snap pegs have BULB heads
        and cannot pass the Ø0.80-1.0 holes: the 8.70mm pair is theirs, the
        13.50mm pair belongs to the LED (EasyEDA CAD, C692901). The first
        revision had LED and pegs swapped, which would not have fitted at all.
        They are plastic and unplated on the real part; they are modelled as
        plated holes here because the placement DRC rejects a board-level
        <hole> inside a footprint courtyard, and the pegs sit inside the pad
        span. No port, no net. */}
    <platedhole pcbX="0mm" pcbY="4.35mm" shape="circle" holeDiameter="1.9mm" outerDiameter="2.1mm" />
    <platedhole pcbX="0mm" pcbY="-4.35mm" shape="circle" holeDiameter="1.9mm" outerDiameter="2.1mm" />
    {/* body 12 x 12mm, cap dia 8.2mm on top */}
    <silkscreenpath route={[{ x: -6, y: 6 }, { x: 6, y: 6 }]} />
    <silkscreenpath route={[{ x: -6, y: 6 }, { x: -6, y: -6 }]} />
    <silkscreenpath route={[{ x: 6, y: 6 }, { x: 6, y: -6 }]} />
    <silkscreenpath route={[{ x: -6, y: -6 }, { x: 6, y: -6 }]} />
    <silkscreencircle pcbX="0mm" pcbY="0mm" radius="4.1mm" />
    <courtyardrect pcbX="0mm" pcbY="0mm" width="15.5mm" height="16mm" />
  </footprint>
)

// --------------------------------------------------------------------------
// SOD-323 diode (1N4148W) - pad-for-pad the base board's footprint
// (lib/footprints.tsx, FP_jclpcb_SOD_323_L1_8_W1_3_LS2_5_RD). pin1 = cathode.
// --------------------------------------------------------------------------

export const FP_SOD323 = (
  <footprint insertionDirection="from_above">
    <smtpad pcbX="-1.17mm" pcbY="0mm" portHints={["pin1"]} shape="rect" width="1mm" height="0.75mm" layer="top" />
    <smtpad pcbX="1.17mm" pcbY="0mm" portHints={["pin2"]} shape="rect" width="1mm" height="0.75mm" layer="top" />
    <silkscreenpath route={[{ x: -0.85, y: 0.73 }, { x: 0.91, y: 0.73 }]} />
    <silkscreenpath route={[{ x: -0.85, y: -0.73 }, { x: 0.91, y: -0.73 }]} />
    <silkscreenpath route={[{ x: -0.44, y: 0.73 }, { x: -0.44, y: -0.73 }]} />
    <courtyardrect pcbX="0mm" pcbY="0mm" width="3.84mm" height="1.96mm" />
  </footprint>
)

// Froemix base board - hand-maintained tscircuit source (see froemix/AGENTS.md).
// Ported once from the KiCad project in ../tmp by froemix/tools/kicad2tscircuit.py;
// that tool is retired as an editor, edit this file directly.

// Exact Froemix footprints: pads, drills, pad numbering and silkscreen are
// copied from the KiCad board (KiCad Y-down -> tscircuit Y-up), so the
// tscircuit board and the KiCad board agree pad for pad.
//
// Every `circular_hole_with_rect_pad` pad carries holeOffsetX/holeOffsetY="0mm"
// although zero is the documented default: @tscircuit/core 0.0.2242 copies the
// props straight into pcb_plated_hole.hole_offset_x/y and leaves them undefined
// when unset, and @tscircuit/circuit-json-util's getPlatedHolePolygon then
// computes a NaN hole centre -> NaN arc vertices -> the copper-pour
// connectivity pass dies inside @flatten-js/core ("undefined is not an object
// (evaluating 'node.item.key.low')"). Setting the props makes core emit 0 and
// the pour builds. Pad geometry is unchanged (0 IS the default), so drop the
// props once upstream default-fills them. Repro: ../doc/pour-repro/.

// 4ms_Connector:Pins_2x08_2.54mm_TH_EurorackPower_Shrouded   (from froemix.kicad_pcb)
export const FP_4ms_Connector_Pins_2x08_2_54mm_TH_EurorackPower_Shrouded = (
  <footprint insertionDirection="from_below">
    <platedhole pcbX="1.27mm" pcbY="-8.89mm" portHints={["pin1"]} shape="circular_hole_with_rect_pad" holeDiameter="1mm" rectPadWidth="1.7mm" rectPadHeight="1.7mm" rectBorderRadius="0.25mm" holeOffsetX="0mm" holeOffsetY="0mm" />
    <platedhole pcbX="-1.27mm" pcbY="-8.89mm" portHints={["pin2"]} shape="circle" holeDiameter="1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="1.27mm" pcbY="-6.35mm" portHints={["pin3"]} shape="circle" holeDiameter="1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="-1.27mm" pcbY="-6.35mm" portHints={["pin4"]} shape="circle" holeDiameter="1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="1.27mm" pcbY="-3.81mm" portHints={["pin5"]} shape="circle" holeDiameter="1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="-1.27mm" pcbY="-3.81mm" portHints={["pin6"]} shape="circle" holeDiameter="1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="1.27mm" pcbY="-1.27mm" portHints={["pin7"]} shape="circle" holeDiameter="1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="-1.27mm" pcbY="-1.27mm" portHints={["pin8"]} shape="circle" holeDiameter="1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="1.27mm" pcbY="1.27mm" portHints={["pin9"]} shape="circle" holeDiameter="1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="-1.27mm" pcbY="1.27mm" portHints={["pin10"]} shape="circle" holeDiameter="1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="1.27mm" pcbY="3.81mm" portHints={["pin11"]} shape="circle" holeDiameter="1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="-1.27mm" pcbY="3.81mm" portHints={["pin12"]} shape="circle" holeDiameter="1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="1.27mm" pcbY="6.35mm" portHints={["pin13"]} shape="circle" holeDiameter="1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="-1.27mm" pcbY="6.35mm" portHints={["pin14"]} shape="circle" holeDiameter="1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="1.27mm" pcbY="8.89mm" portHints={["pin15"]} shape="circle" holeDiameter="1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="-1.27mm" pcbY="8.89mm" portHints={["pin16"]} shape="circle" holeDiameter="1mm" outerDiameter="1.7mm" />
    <silkscreenpath route={[{ x: -4.56, y: 14.1 }, { x: 4.56, y: 14.1 }]} />
    <silkscreenpath route={[{ x: -4.56, y: -14.1 }, { x: -4.56, y: 14.1 }]} />
    <silkscreenpath route={[{ x: -3.25, y: 12.8 }, { x: 3.25, y: 12.8 }]} />
    <silkscreenpath route={[{ x: -3.25, y: -12.8 }, { x: -3.25, y: 12.8 }]} />
    <silkscreenpath route={[{ x: 3.25, y: 12.8 }, { x: 3.25, y: 2.05 }]} />
    <silkscreenpath route={[{ x: 3.25, y: 2.05 }, { x: 3.25, y: 2.05 }]} />
    <silkscreenpath route={[{ x: 3.25, y: 2.05 }, { x: 4.56, y: 2.05 }]} />
    <silkscreenpath route={[{ x: 3.25, y: -2.05 }, { x: 3.25, y: -12.8 }]} />
    <silkscreenpath route={[{ x: 3.25, y: -12.8 }, { x: -3.25, y: -12.8 }]} />
    <silkscreenpath route={[{ x: 4.56, y: 14.1 }, { x: 4.56, y: -14.1 }]} />
    <silkscreenpath route={[{ x: 4.56, y: -2.05 }, { x: 3.25, y: -2.05 }]} />
    <silkscreenpath route={[{ x: 4.56, y: -14.1 }, { x: -4.56, y: -14.1 }]} />
    <silkscreenpath route={[{ x: 4.651, y: -15.1 }, { x: -4.651, y: -15.1 }]} />
    <courtyardrect pcbX="0mm" pcbY="0mm" width="9.9mm" height="28.98mm" />
  </footprint>
)

// 4ms_Connector:Pins_2x08_2.54mm_TH_EurorackPower_Shrouded@R90   (from froemix.kicad_pcb (pre-rotated 90))
export const FP_4ms_Connector_Pins_2x08_2_54mm_TH_EurorackPower_Shrouded_R90 = (
  <footprint insertionDirection="from_below">
    <platedhole pcbX="-8.89mm" pcbY="-1.27mm" portHints={["pin1"]} shape="circular_hole_with_rect_pad" holeDiameter="1mm" rectPadWidth="1.7mm" rectPadHeight="1.7mm" rectBorderRadius="0.25mm" holeOffsetX="0mm" holeOffsetY="0mm" />
    <platedhole pcbX="-8.89mm" pcbY="1.27mm" portHints={["pin2"]} shape="circle" holeDiameter="1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="-6.35mm" pcbY="-1.27mm" portHints={["pin3"]} shape="circle" holeDiameter="1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="-6.35mm" pcbY="1.27mm" portHints={["pin4"]} shape="circle" holeDiameter="1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="-3.81mm" pcbY="-1.27mm" portHints={["pin5"]} shape="circle" holeDiameter="1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="-3.81mm" pcbY="1.27mm" portHints={["pin6"]} shape="circle" holeDiameter="1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="-1.27mm" pcbY="-1.27mm" portHints={["pin7"]} shape="circle" holeDiameter="1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="-1.27mm" pcbY="1.27mm" portHints={["pin8"]} shape="circle" holeDiameter="1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="1.27mm" pcbY="-1.27mm" portHints={["pin9"]} shape="circle" holeDiameter="1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="1.27mm" pcbY="1.27mm" portHints={["pin10"]} shape="circle" holeDiameter="1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="3.81mm" pcbY="-1.27mm" portHints={["pin11"]} shape="circle" holeDiameter="1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="3.81mm" pcbY="1.27mm" portHints={["pin12"]} shape="circle" holeDiameter="1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="6.35mm" pcbY="-1.27mm" portHints={["pin13"]} shape="circle" holeDiameter="1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="6.35mm" pcbY="1.27mm" portHints={["pin14"]} shape="circle" holeDiameter="1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="8.89mm" pcbY="-1.27mm" portHints={["pin15"]} shape="circle" holeDiameter="1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="8.89mm" pcbY="1.27mm" portHints={["pin16"]} shape="circle" holeDiameter="1mm" outerDiameter="1.7mm" />
    <silkscreenpath route={[{ x: 14.1, y: 4.56 }, { x: 14.1, y: -4.56 }]} />
    <silkscreenpath route={[{ x: -14.1, y: 4.56 }, { x: 14.1, y: 4.56 }]} />
    <silkscreenpath route={[{ x: 12.8, y: 3.25 }, { x: 12.8, y: -3.25 }]} />
    <silkscreenpath route={[{ x: -12.8, y: 3.25 }, { x: 12.8, y: 3.25 }]} />
    <silkscreenpath route={[{ x: 12.8, y: -3.25 }, { x: 2.05, y: -3.25 }]} />
    <silkscreenpath route={[{ x: 2.05, y: -3.25 }, { x: 2.05, y: -3.25 }]} />
    <silkscreenpath route={[{ x: 2.05, y: -3.25 }, { x: 2.05, y: -4.56 }]} />
    <silkscreenpath route={[{ x: -2.05, y: -3.25 }, { x: -12.8, y: -3.25 }]} />
    <silkscreenpath route={[{ x: -12.8, y: -3.25 }, { x: -12.8, y: 3.25 }]} />
    <silkscreenpath route={[{ x: 14.1, y: -4.56 }, { x: -14.1, y: -4.56 }]} />
    <silkscreenpath route={[{ x: -2.05, y: -4.56 }, { x: -2.05, y: -3.25 }]} />
    <silkscreenpath route={[{ x: -14.1, y: -4.56 }, { x: -14.1, y: 4.56 }]} />
    <silkscreenpath route={[{ x: -15.1, y: -4.651 }, { x: -15.1, y: 4.651 }]} />
    <courtyardrect pcbX="0mm" pcbY="0mm" width="28.98mm" height="9.9mm" />
  </footprint>
)

// Module:Electrosmith_Daisy_Seed   (from library/DaisyKiCad-main/Daisy-Boards.pretty/DAISY_SEED.kicad_mod)
export const FP_Module_Electrosmith_Daisy_Seed = (
  <footprint>
    <platedhole pcbX="0mm" pcbY="0mm" portHints={["pin1"]} shape="circular_hole_with_rect_pad" holeDiameter="1.1mm" rectPadWidth="1.7mm" rectPadHeight="1.7mm" holeOffsetX="0mm" holeOffsetY="0mm" />
    <platedhole pcbX="0mm" pcbY="-2.54mm" portHints={["pin2"]} shape="circle" holeDiameter="1.1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="0mm" pcbY="-5.08mm" portHints={["pin3"]} shape="circle" holeDiameter="1.1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="0mm" pcbY="-7.62mm" portHints={["pin4"]} shape="circle" holeDiameter="1.1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="0mm" pcbY="-10.16mm" portHints={["pin5"]} shape="circle" holeDiameter="1.1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="0mm" pcbY="-12.7mm" portHints={["pin6"]} shape="circle" holeDiameter="1.1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="0mm" pcbY="-15.24mm" portHints={["pin7"]} shape="circle" holeDiameter="1.1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="0mm" pcbY="-17.78mm" portHints={["pin8"]} shape="circle" holeDiameter="1.1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="0mm" pcbY="-20.32mm" portHints={["pin9"]} shape="circle" holeDiameter="1.1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="0mm" pcbY="-22.86mm" portHints={["pin10"]} shape="circle" holeDiameter="1.1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="0mm" pcbY="-25.4mm" portHints={["pin11"]} shape="circle" holeDiameter="1.1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="0mm" pcbY="-27.94mm" portHints={["pin12"]} shape="circle" holeDiameter="1.1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="0mm" pcbY="-30.48mm" portHints={["pin13"]} shape="circle" holeDiameter="1.1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="0mm" pcbY="-33.02mm" portHints={["pin14"]} shape="circle" holeDiameter="1.1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="0mm" pcbY="-35.56mm" portHints={["pin15"]} shape="circle" holeDiameter="1.1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="0mm" pcbY="-38.1mm" portHints={["pin16"]} shape="circle" holeDiameter="1.1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="0mm" pcbY="-40.64mm" portHints={["pin17"]} shape="circle" holeDiameter="1.1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="0mm" pcbY="-43.18mm" portHints={["pin18"]} shape="circle" holeDiameter="1.1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="0mm" pcbY="-45.72mm" portHints={["pin19"]} shape="circle" holeDiameter="1.1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="0mm" pcbY="-48.26mm" portHints={["pin20"]} shape="circle" holeDiameter="1.1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="15.24mm" pcbY="-48.26mm" portHints={["pin21"]} shape="circle" holeDiameter="1.1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="15.24mm" pcbY="-45.72mm" portHints={["pin22"]} shape="circle" holeDiameter="1.1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="15.24mm" pcbY="-43.18mm" portHints={["pin23"]} shape="circle" holeDiameter="1.1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="15.24mm" pcbY="-40.64mm" portHints={["pin24"]} shape="circle" holeDiameter="1.1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="15.24mm" pcbY="-38.1mm" portHints={["pin25"]} shape="circle" holeDiameter="1.1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="15.24mm" pcbY="-35.56mm" portHints={["pin26"]} shape="circle" holeDiameter="1.1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="15.24mm" pcbY="-33.02mm" portHints={["pin27"]} shape="circle" holeDiameter="1.1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="15.24mm" pcbY="-30.48mm" portHints={["pin28"]} shape="circle" holeDiameter="1.1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="15.24mm" pcbY="-27.94mm" portHints={["pin29"]} shape="circle" holeDiameter="1.1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="15.24mm" pcbY="-25.4mm" portHints={["pin30"]} shape="circle" holeDiameter="1.1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="15.24mm" pcbY="-22.86mm" portHints={["pin31"]} shape="circle" holeDiameter="1.1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="15.24mm" pcbY="-20.32mm" portHints={["pin32"]} shape="circle" holeDiameter="1.1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="15.24mm" pcbY="-17.78mm" portHints={["pin33"]} shape="circle" holeDiameter="1.1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="15.24mm" pcbY="-15.24mm" portHints={["pin34"]} shape="circle" holeDiameter="1.1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="15.24mm" pcbY="-12.7mm" portHints={["pin35"]} shape="circle" holeDiameter="1.1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="15.24mm" pcbY="-10.16mm" portHints={["pin36"]} shape="circle" holeDiameter="1.1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="15.24mm" pcbY="-7.62mm" portHints={["pin37"]} shape="circle" holeDiameter="1.1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="15.24mm" pcbY="-5.08mm" portHints={["pin38"]} shape="circle" holeDiameter="1.1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="15.24mm" pcbY="-2.54mm" portHints={["pin39"]} shape="circle" holeDiameter="1.1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="15.24mm" pcbY="0mm" portHints={["pin40"]} shape="circle" holeDiameter="1.1mm" outerDiameter="1.7mm" />
    <silkscreenpath route={[{ x: -1.58, y: 1.7 }, { x: -1.58, y: -0.7 }]} />
    <silkscreenpath route={[{ x: -1.28, y: 1.4 }, { x: 16.52, y: 1.4 }]} />
    <silkscreenpath route={[{ x: -1.28, y: -49.6 }, { x: -1.28, y: 1.4 }]} />
    <silkscreenpath route={[{ x: 0.72, y: 1.7 }, { x: -1.58, y: 1.7 }]} />
    <silkscreenpath route={[{ x: 1.2, y: 1.4 }, { x: 1.2, y: -49.6 }]} />
    <silkscreenpath route={[{ x: 3.62, y: 2.6 }, { x: 11.62, y: 2.6 }]} />
    <silkscreenpath route={[{ x: 3.62, y: 1.4 }, { x: 3.62, y: 2.6 }]} />
    <silkscreenpath route={[{ x: 11.62, y: 2.6 }, { x: 11.62, y: 1.4 }]} />
    <silkscreenpath route={[{ x: 14, y: 1.4 }, { x: 14, y: -49.6 }]} />
    <silkscreenpath route={[{ x: 16.52, y: 1.4 }, { x: 16.52, y: -49.6 }]} />
    <silkscreenpath route={[{ x: 16.52, y: -49.6 }, { x: -1.28, y: -49.6 }]} />
    <courtyardrect pcbX="7.62mm" pcbY="-23.505mm" width="18.74mm" height="53.01mm" />
  </footprint>
)

// Module:Electrosmith_Daisy_Seed@R90   (from library/DaisyKiCad-main/Daisy-Boards.pretty/DAISY_SEED.kicad_mod (pre-rotated 90))
export const FP_Module_Electrosmith_Daisy_Seed_R90 = (
  <footprint>
    <platedhole pcbX="0mm" pcbY="0mm" portHints={["pin1"]} shape="circular_hole_with_rect_pad" holeDiameter="1.1mm" rectPadWidth="1.7mm" rectPadHeight="1.7mm" holeOffsetX="0mm" holeOffsetY="0mm" />
    <platedhole pcbX="-2.54mm" pcbY="0mm" portHints={["pin2"]} shape="circle" holeDiameter="1.1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="-5.08mm" pcbY="0mm" portHints={["pin3"]} shape="circle" holeDiameter="1.1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="-7.62mm" pcbY="0mm" portHints={["pin4"]} shape="circle" holeDiameter="1.1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="-10.16mm" pcbY="0mm" portHints={["pin5"]} shape="circle" holeDiameter="1.1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="-12.7mm" pcbY="0mm" portHints={["pin6"]} shape="circle" holeDiameter="1.1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="-15.24mm" pcbY="0mm" portHints={["pin7"]} shape="circle" holeDiameter="1.1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="-17.78mm" pcbY="0mm" portHints={["pin8"]} shape="circle" holeDiameter="1.1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="-20.32mm" pcbY="0mm" portHints={["pin9"]} shape="circle" holeDiameter="1.1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="-22.86mm" pcbY="0mm" portHints={["pin10"]} shape="circle" holeDiameter="1.1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="-25.4mm" pcbY="0mm" portHints={["pin11"]} shape="circle" holeDiameter="1.1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="-27.94mm" pcbY="0mm" portHints={["pin12"]} shape="circle" holeDiameter="1.1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="-30.48mm" pcbY="0mm" portHints={["pin13"]} shape="circle" holeDiameter="1.1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="-33.02mm" pcbY="0mm" portHints={["pin14"]} shape="circle" holeDiameter="1.1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="-35.56mm" pcbY="0mm" portHints={["pin15"]} shape="circle" holeDiameter="1.1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="-38.1mm" pcbY="0mm" portHints={["pin16"]} shape="circle" holeDiameter="1.1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="-40.64mm" pcbY="0mm" portHints={["pin17"]} shape="circle" holeDiameter="1.1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="-43.18mm" pcbY="0mm" portHints={["pin18"]} shape="circle" holeDiameter="1.1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="-45.72mm" pcbY="0mm" portHints={["pin19"]} shape="circle" holeDiameter="1.1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="-48.26mm" pcbY="0mm" portHints={["pin20"]} shape="circle" holeDiameter="1.1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="-48.26mm" pcbY="-15.24mm" portHints={["pin21"]} shape="circle" holeDiameter="1.1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="-45.72mm" pcbY="-15.24mm" portHints={["pin22"]} shape="circle" holeDiameter="1.1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="-43.18mm" pcbY="-15.24mm" portHints={["pin23"]} shape="circle" holeDiameter="1.1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="-40.64mm" pcbY="-15.24mm" portHints={["pin24"]} shape="circle" holeDiameter="1.1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="-38.1mm" pcbY="-15.24mm" portHints={["pin25"]} shape="circle" holeDiameter="1.1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="-35.56mm" pcbY="-15.24mm" portHints={["pin26"]} shape="circle" holeDiameter="1.1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="-33.02mm" pcbY="-15.24mm" portHints={["pin27"]} shape="circle" holeDiameter="1.1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="-30.48mm" pcbY="-15.24mm" portHints={["pin28"]} shape="circle" holeDiameter="1.1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="-27.94mm" pcbY="-15.24mm" portHints={["pin29"]} shape="circle" holeDiameter="1.1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="-25.4mm" pcbY="-15.24mm" portHints={["pin30"]} shape="circle" holeDiameter="1.1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="-22.86mm" pcbY="-15.24mm" portHints={["pin31"]} shape="circle" holeDiameter="1.1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="-20.32mm" pcbY="-15.24mm" portHints={["pin32"]} shape="circle" holeDiameter="1.1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="-17.78mm" pcbY="-15.24mm" portHints={["pin33"]} shape="circle" holeDiameter="1.1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="-15.24mm" pcbY="-15.24mm" portHints={["pin34"]} shape="circle" holeDiameter="1.1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="-12.7mm" pcbY="-15.24mm" portHints={["pin35"]} shape="circle" holeDiameter="1.1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="-10.16mm" pcbY="-15.24mm" portHints={["pin36"]} shape="circle" holeDiameter="1.1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="-7.62mm" pcbY="-15.24mm" portHints={["pin37"]} shape="circle" holeDiameter="1.1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="-5.08mm" pcbY="-15.24mm" portHints={["pin38"]} shape="circle" holeDiameter="1.1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="-2.54mm" pcbY="-15.24mm" portHints={["pin39"]} shape="circle" holeDiameter="1.1mm" outerDiameter="1.7mm" />
    <platedhole pcbX="0mm" pcbY="-15.24mm" portHints={["pin40"]} shape="circle" holeDiameter="1.1mm" outerDiameter="1.7mm" />
    <silkscreenpath route={[{ x: 1.7, y: 1.58 }, { x: -0.7, y: 1.58 }]} />
    <silkscreenpath route={[{ x: 1.4, y: 1.28 }, { x: 1.4, y: -16.52 }]} />
    <silkscreenpath route={[{ x: -49.6, y: 1.28 }, { x: 1.4, y: 1.28 }]} />
    <silkscreenpath route={[{ x: 1.7, y: -0.72 }, { x: 1.7, y: 1.58 }]} />
    <silkscreenpath route={[{ x: 1.4, y: -1.2 }, { x: -49.6, y: -1.2 }]} />
    <silkscreenpath route={[{ x: 2.6, y: -3.62 }, { x: 2.6, y: -11.62 }]} />
    <silkscreenpath route={[{ x: 1.4, y: -3.62 }, { x: 2.6, y: -3.62 }]} />
    <silkscreenpath route={[{ x: 2.6, y: -11.62 }, { x: 1.4, y: -11.62 }]} />
    <silkscreenpath route={[{ x: 1.4, y: -14 }, { x: -49.6, y: -14 }]} />
    <silkscreenpath route={[{ x: 1.4, y: -16.52 }, { x: -49.6, y: -16.52 }]} />
    <silkscreenpath route={[{ x: -49.6, y: -16.52 }, { x: -49.6, y: 1.28 }]} />
    <courtyardrect pcbX="-23.505mm" pcbY="-7.62mm" width="53.01mm" height="18.74mm" />
  </footprint>
)

// jclpcb:C0603   (from froemix.kicad_pcb)
export const FP_jclpcb_C0603 = (
  <footprint>
    <smtpad pcbX="-0.7mm" pcbY="0mm" portHints={["pin1"]} shape="rect" width="0.8mm" height="0.9mm" layer="top" />
    <smtpad pcbX="0.7mm" pcbY="0mm" portHints={["pin2"]} shape="rect" width="0.8mm" height="0.9mm" layer="top" />
    <silkscreenpath route={[{ x: -1.41, y: 0.41 }, { x: -1.41, y: -0.39 }]} />
    <silkscreenpath route={[{ x: -0.3, y: 0.72 }, { x: -1.1, y: 0.72 }]} />
    <silkscreenpath route={[{ x: -0.3, y: -0.7 }, { x: -1.1, y: -0.7 }]} />
    <silkscreenpath route={[{ x: 0.28, y: 0.71 }, { x: 1.08, y: 0.71 }]} />
    <silkscreenpath route={[{ x: 0.28, y: -0.71 }, { x: 1.08, y: -0.71 }]} />
    <silkscreenpath route={[{ x: 1.39, y: 0.4 }, { x: 1.39, y: -0.4 }]} />
    <courtyardrect pcbX="-0.01mm" pcbY="0.005mm" width="3.3mm" height="1.93mm" />
  </footprint>
)

// jclpcb:C1206   (from froemix.kicad_pcb)
export const FP_jclpcb_C1206 = (
  <footprint>
    <smtpad pcbX="-1.59mm" pcbY="0mm" portHints={["pin1"]} shape="rect" width="1.49mm" height="1.73mm" layer="top" />
    <smtpad pcbX="1.59mm" pcbY="0mm" portHints={["pin2"]} shape="rect" width="1.49mm" height="1.73mm" layer="top" />
    <silkscreenpath route={[{ x: -2.56, y: -0.94 }, { x: -2.56, y: 0.94 }]} />
    <silkscreenpath route={[{ x: -2.41, y: 1.09 }, { x: -0.93, y: 1.09 }]} />
    <silkscreenpath route={[{ x: -0.93, y: -1.09 }, { x: -2.41, y: -1.09 }]} />
    <silkscreenpath route={[{ x: 0.93, y: -1.09 }, { x: 2.41, y: -1.09 }]} />
    <silkscreenpath route={[{ x: 2.41, y: 1.09 }, { x: 0.93, y: 1.09 }]} />
    <silkscreenpath route={[{ x: 2.56, y: -0.94 }, { x: 2.56, y: 0.94 }]} />
    <courtyardrect pcbX="0mm" pcbY="0mm" width="5.62mm" height="2.68mm" />
  </footprint>
)

// jclpcb:CAP-TH_BD8.0-P3.50-D0.6-FD   (from froemix.kicad_pcb)
// The KiCad footprint only marks polarity: no body outline, and a 6.5 x 3.18mm
// courtyard offset from the pad line. The selected part (LCSC C2749) is an
// 8mm-diameter radial can, so the body circle (r=4.0, centred on the midpoint
// of the pads) and an 8.4 x 8.4mm courtyard are added here to make the real
// keepout visible to the placement checker (DESIGN-REVIEW R11). Pads and the
// polarity marker are unchanged.
export const FP_jclpcb_CAP_TH_BD8_0_P3_50_D0_6_FD = (
  <footprint>
    <platedhole pcbX="-1.75mm" pcbY="0mm" portHints={["pin1"]} shape="circle" holeDiameter="1mm" outerDiameter="1.6mm" />
    <platedhole pcbX="1.75mm" pcbY="0mm" portHints={["pin2"]} shape="circle" holeDiameter="1mm" outerDiameter="1.6mm" />
    <silkscreencircle pcbX="0mm" pcbY="0mm" radius="4mm" strokeWidth="0.1mm" />
    <silkscreenpath route={[{ x: -2.83, y: 0.53 }, { x: -2.83, y: 1.88 }]} />
    <silkscreenpath route={[{ x: -2.2, y: 1.2 }, { x: -3.45, y: 1.2 }]} />
    <courtyardrect pcbX="0mm" pcbY="0mm" width="8.6mm" height="8.6mm" />
  </footprint>
)

// jclpcb:HDR-TH_12P-P2.54-H-M-R2-C6-S2.54-W12.5   (from froemix.kicad_pcb)
export const FP_jclpcb_HDR_TH_12P_P2_54_H_M_R2_C6_S2_54_W12_5 = (
  <footprint>
    <platedhole pcbX="6.35mm" pcbY="1.27mm" portHints={["pin1"]} shape="circular_hole_with_rect_pad" holeDiameter="1.1mm" rectPadWidth="1.8mm" rectPadHeight="1.8mm" holeOffsetX="0mm" holeOffsetY="0mm" />
    <platedhole pcbX="6.35mm" pcbY="-1.27mm" portHints={["pin2"]} shape="circle" holeDiameter="1.1mm" outerDiameter="1.8mm" />
    <platedhole pcbX="3.81mm" pcbY="1.27mm" portHints={["pin3"]} shape="circle" holeDiameter="1.1mm" outerDiameter="1.8mm" />
    <platedhole pcbX="3.81mm" pcbY="-1.27mm" portHints={["pin4"]} shape="circle" holeDiameter="1.1mm" outerDiameter="1.8mm" />
    <platedhole pcbX="1.27mm" pcbY="1.27mm" portHints={["pin5"]} shape="circle" holeDiameter="1.1mm" outerDiameter="1.8mm" />
    <platedhole pcbX="1.27mm" pcbY="-1.27mm" portHints={["pin6"]} shape="circle" holeDiameter="1.1mm" outerDiameter="1.8mm" />
    <platedhole pcbX="-1.27mm" pcbY="1.27mm" portHints={["pin7"]} shape="circle" holeDiameter="1.1mm" outerDiameter="1.8mm" />
    <platedhole pcbX="-1.27mm" pcbY="-1.27mm" portHints={["pin8"]} shape="circle" holeDiameter="1.1mm" outerDiameter="1.8mm" />
    <platedhole pcbX="-3.81mm" pcbY="1.27mm" portHints={["pin9"]} shape="circle" holeDiameter="1.1mm" outerDiameter="1.8mm" />
    <platedhole pcbX="-3.81mm" pcbY="-1.27mm" portHints={["pin10"]} shape="circle" holeDiameter="1.1mm" outerDiameter="1.8mm" />
    <platedhole pcbX="-6.35mm" pcbY="1.27mm" portHints={["pin11"]} shape="circle" holeDiameter="1.1mm" outerDiameter="1.8mm" />
    <platedhole pcbX="-6.35mm" pcbY="-1.27mm" portHints={["pin12"]} shape="circle" holeDiameter="1.1mm" outerDiameter="1.8mm" />
    <silkscreenpath route={[{ x: -7.62, y: -2.77 }, { x: 7.62, y: -2.77 }]} />
    <silkscreenpath route={[{ x: -7.62, y: -5.27 }, { x: -7.62, y: -2.77 }]} />
    <silkscreenpath route={[{ x: -7.62, y: -5.27 }, { x: 7.62, y: -5.27 }]} />
    <silkscreenpath route={[{ x: -6.77, y: -10.85 }, { x: -6.77, y: -5.27 }]} />
    <silkscreenpath route={[{ x: -6.35, y: -11.27 }, { x: -6.77, y: -10.85 }]} />
    <silkscreenpath route={[{ x: -5.92, y: -5.27 }, { x: -5.92, y: -10.85 }]} />
    <silkscreenpath route={[{ x: -5.92, y: -10.85 }, { x: -6.35, y: -11.27 }]} />
    <silkscreenpath route={[{ x: -4.23, y: -10.85 }, { x: -4.23, y: -5.27 }]} />
    <silkscreenpath route={[{ x: -3.81, y: -11.27 }, { x: -4.23, y: -10.85 }]} />
    <silkscreenpath route={[{ x: -3.38, y: -5.27 }, { x: -3.38, y: -10.85 }]} />
    <silkscreenpath route={[{ x: -3.38, y: -10.85 }, { x: -3.81, y: -11.27 }]} />
    <silkscreenpath route={[{ x: -1.69, y: -10.85 }, { x: -1.69, y: -5.27 }]} />
    <silkscreenpath route={[{ x: -1.27, y: -11.27 }, { x: -1.69, y: -10.85 }]} />
    <silkscreenpath route={[{ x: -0.84, y: -5.27 }, { x: -0.84, y: -10.85 }]} />
    <silkscreenpath route={[{ x: -0.84, y: -10.85 }, { x: -1.27, y: -11.27 }]} />
    <silkscreenpath route={[{ x: 0.85, y: -10.85 }, { x: 0.85, y: -5.27 }]} />
    <silkscreenpath route={[{ x: 1.27, y: -11.27 }, { x: 0.85, y: -10.85 }]} />
    <silkscreenpath route={[{ x: 1.7, y: -5.27 }, { x: 1.7, y: -10.85 }]} />
    <silkscreenpath route={[{ x: 1.7, y: -10.85 }, { x: 1.27, y: -11.27 }]} />
    <silkscreenpath route={[{ x: 3.39, y: -10.85 }, { x: 3.39, y: -5.27 }]} />
    <silkscreenpath route={[{ x: 3.81, y: -11.27 }, { x: 3.39, y: -10.85 }]} />
    <silkscreenpath route={[{ x: 4.24, y: -5.27 }, { x: 4.24, y: -10.85 }]} />
    <silkscreenpath route={[{ x: 4.24, y: -10.85 }, { x: 3.81, y: -11.27 }]} />
    <silkscreenpath route={[{ x: 5.93, y: -10.85 }, { x: 5.93, y: -5.27 }]} />
    <silkscreenpath route={[{ x: 6.35, y: -11.27 }, { x: 5.93, y: -10.85 }]} />
    <silkscreenpath route={[{ x: 6.78, y: -5.27 }, { x: 6.78, y: -10.85 }]} />
    <silkscreenpath route={[{ x: 6.78, y: -10.85 }, { x: 6.35, y: -11.27 }]} />
    <silkscreenpath route={[{ x: 7.62, y: -5.27 }, { x: 7.62, y: -2.77 }]} />
    <courtyardrect pcbX="0mm" pcbY="-4.55mm" width="15.74mm" height="13.94mm" />
  </footprint>
)

// jclpcb:HDR-TH_12P-P2.54-H-M-R2-C6-S2.54-W12.5@R90   (from froemix.kicad_pcb (pre-rotated 90))
export const FP_jclpcb_HDR_TH_12P_P2_54_H_M_R2_C6_S2_54_W12_5_R90 = (
  <footprint insertionDirection="from_x_pos">
    <platedhole pcbX="1.27mm" pcbY="-6.35mm" portHints={["pin1"]} shape="circular_hole_with_rect_pad" holeDiameter="1.1mm" rectPadWidth="1.8mm" rectPadHeight="1.8mm" holeOffsetX="0mm" holeOffsetY="0mm" />
    <platedhole pcbX="-1.27mm" pcbY="-6.35mm" portHints={["pin2"]} shape="circle" holeDiameter="1.1mm" outerDiameter="1.8mm" />
    <platedhole pcbX="1.27mm" pcbY="-3.81mm" portHints={["pin3"]} shape="circle" holeDiameter="1.1mm" outerDiameter="1.8mm" />
    <platedhole pcbX="-1.27mm" pcbY="-3.81mm" portHints={["pin4"]} shape="circle" holeDiameter="1.1mm" outerDiameter="1.8mm" />
    <platedhole pcbX="1.27mm" pcbY="-1.27mm" portHints={["pin5"]} shape="circle" holeDiameter="1.1mm" outerDiameter="1.8mm" />
    <platedhole pcbX="-1.27mm" pcbY="-1.27mm" portHints={["pin6"]} shape="circle" holeDiameter="1.1mm" outerDiameter="1.8mm" />
    <platedhole pcbX="1.27mm" pcbY="1.27mm" portHints={["pin7"]} shape="circle" holeDiameter="1.1mm" outerDiameter="1.8mm" />
    <platedhole pcbX="-1.27mm" pcbY="1.27mm" portHints={["pin8"]} shape="circle" holeDiameter="1.1mm" outerDiameter="1.8mm" />
    <platedhole pcbX="1.27mm" pcbY="3.81mm" portHints={["pin9"]} shape="circle" holeDiameter="1.1mm" outerDiameter="1.8mm" />
    <platedhole pcbX="-1.27mm" pcbY="3.81mm" portHints={["pin10"]} shape="circle" holeDiameter="1.1mm" outerDiameter="1.8mm" />
    <platedhole pcbX="1.27mm" pcbY="6.35mm" portHints={["pin11"]} shape="circle" holeDiameter="1.1mm" outerDiameter="1.8mm" />
    <platedhole pcbX="-1.27mm" pcbY="6.35mm" portHints={["pin12"]} shape="circle" holeDiameter="1.1mm" outerDiameter="1.8mm" />
    <silkscreenpath route={[{ x: -2.77, y: 7.62 }, { x: -2.77, y: -7.62 }]} />
    <silkscreenpath route={[{ x: -5.27, y: 7.62 }, { x: -2.77, y: 7.62 }]} />
    <silkscreenpath route={[{ x: -5.27, y: 7.62 }, { x: -5.27, y: -7.62 }]} />
    <silkscreenpath route={[{ x: -10.85, y: 6.77 }, { x: -5.27, y: 6.77 }]} />
    <silkscreenpath route={[{ x: -11.27, y: 6.35 }, { x: -10.85, y: 6.77 }]} />
    <silkscreenpath route={[{ x: -5.27, y: 5.92 }, { x: -10.85, y: 5.92 }]} />
    <silkscreenpath route={[{ x: -10.85, y: 5.92 }, { x: -11.27, y: 6.35 }]} />
    <silkscreenpath route={[{ x: -10.85, y: 4.23 }, { x: -5.27, y: 4.23 }]} />
    <silkscreenpath route={[{ x: -11.27, y: 3.81 }, { x: -10.85, y: 4.23 }]} />
    <silkscreenpath route={[{ x: -5.27, y: 3.38 }, { x: -10.85, y: 3.38 }]} />
    <silkscreenpath route={[{ x: -10.85, y: 3.38 }, { x: -11.27, y: 3.81 }]} />
    <silkscreenpath route={[{ x: -10.85, y: 1.69 }, { x: -5.27, y: 1.69 }]} />
    <silkscreenpath route={[{ x: -11.27, y: 1.27 }, { x: -10.85, y: 1.69 }]} />
    <silkscreenpath route={[{ x: -5.27, y: 0.84 }, { x: -10.85, y: 0.84 }]} />
    <silkscreenpath route={[{ x: -10.85, y: 0.84 }, { x: -11.27, y: 1.27 }]} />
    <silkscreenpath route={[{ x: -10.85, y: -0.85 }, { x: -5.27, y: -0.85 }]} />
    <silkscreenpath route={[{ x: -11.27, y: -1.27 }, { x: -10.85, y: -0.85 }]} />
    <silkscreenpath route={[{ x: -5.27, y: -1.7 }, { x: -10.85, y: -1.7 }]} />
    <silkscreenpath route={[{ x: -10.85, y: -1.7 }, { x: -11.27, y: -1.27 }]} />
    <silkscreenpath route={[{ x: -10.85, y: -3.39 }, { x: -5.27, y: -3.39 }]} />
    <silkscreenpath route={[{ x: -11.27, y: -3.81 }, { x: -10.85, y: -3.39 }]} />
    <silkscreenpath route={[{ x: -5.27, y: -4.24 }, { x: -10.85, y: -4.24 }]} />
    <silkscreenpath route={[{ x: -10.85, y: -4.24 }, { x: -11.27, y: -3.81 }]} />
    <silkscreenpath route={[{ x: -10.85, y: -5.93 }, { x: -5.27, y: -5.93 }]} />
    <silkscreenpath route={[{ x: -11.27, y: -6.35 }, { x: -10.85, y: -5.93 }]} />
    <silkscreenpath route={[{ x: -5.27, y: -6.78 }, { x: -10.85, y: -6.78 }]} />
    <silkscreenpath route={[{ x: -10.85, y: -6.78 }, { x: -11.27, y: -6.35 }]} />
    <silkscreenpath route={[{ x: -5.27, y: -7.62 }, { x: -2.77, y: -7.62 }]} />
    <courtyardrect pcbX="-4.55mm" pcbY="0mm" width="13.94mm" height="15.74mm" />
  </footprint>
)

// jclpcb:HDR-TH_16P-P2.00-V-M   (from froemix.kicad_pcb)
export const FP_jclpcb_HDR_TH_16P_P2_00_V_M = (
  <footprint insertionDirection="from_above">
    <platedhole pcbX="-15mm" pcbY="0mm" portHints={["pin1"]} shape="circular_hole_with_rect_pad" holeDiameter="1mm" rectPadWidth="1.52mm" rectPadHeight="1.52mm" holeOffsetX="0mm" holeOffsetY="0mm" />
    <platedhole pcbX="-13mm" pcbY="0mm" portHints={["pin2"]} shape="circle" holeDiameter="1mm" outerDiameter="1.52mm" />
    <platedhole pcbX="-11mm" pcbY="0mm" portHints={["pin3"]} shape="circle" holeDiameter="1mm" outerDiameter="1.52mm" />
    <platedhole pcbX="-9mm" pcbY="0mm" portHints={["pin4"]} shape="circle" holeDiameter="1mm" outerDiameter="1.52mm" />
    <platedhole pcbX="-7mm" pcbY="0mm" portHints={["pin5"]} shape="circle" holeDiameter="1mm" outerDiameter="1.52mm" />
    <platedhole pcbX="-5mm" pcbY="0mm" portHints={["pin6"]} shape="circle" holeDiameter="1mm" outerDiameter="1.52mm" />
    <platedhole pcbX="-3mm" pcbY="0mm" portHints={["pin7"]} shape="circle" holeDiameter="1mm" outerDiameter="1.52mm" />
    <platedhole pcbX="-1mm" pcbY="0mm" portHints={["pin8"]} shape="circle" holeDiameter="1mm" outerDiameter="1.52mm" />
    <platedhole pcbX="1mm" pcbY="0mm" portHints={["pin9"]} shape="circle" holeDiameter="1mm" outerDiameter="1.52mm" />
    <platedhole pcbX="3mm" pcbY="0mm" portHints={["pin10"]} shape="circle" holeDiameter="1mm" outerDiameter="1.52mm" />
    <platedhole pcbX="5mm" pcbY="0mm" portHints={["pin11"]} shape="circle" holeDiameter="1mm" outerDiameter="1.52mm" />
    <platedhole pcbX="7mm" pcbY="0mm" portHints={["pin12"]} shape="circle" holeDiameter="1mm" outerDiameter="1.52mm" />
    <platedhole pcbX="9mm" pcbY="0mm" portHints={["pin13"]} shape="circle" holeDiameter="1mm" outerDiameter="1.52mm" />
    <platedhole pcbX="11mm" pcbY="0mm" portHints={["pin14"]} shape="circle" holeDiameter="1mm" outerDiameter="1.52mm" />
    <platedhole pcbX="13mm" pcbY="0mm" portHints={["pin15"]} shape="circle" holeDiameter="1mm" outerDiameter="1.52mm" />
    <platedhole pcbX="15mm" pcbY="0mm" portHints={["pin16"]} shape="circle" holeDiameter="1mm" outerDiameter="1.52mm" />
    <silkscreenpath route={[{ x: -16.07, y: 1.01 }, { x: 16.06, y: 1.01 }]} />
    <silkscreenpath route={[{ x: -16.07, y: -1.02 }, { x: -16.07, y: 1.01 }]} />
    <silkscreenpath route={[{ x: -16.07, y: -1.02 }, { x: 16.06, y: -1.02 }]} />
    <silkscreenpath route={[{ x: -14, y: 1.01 }, { x: -14, y: -1.02 }]} />
    <silkscreenpath route={[{ x: 16.06, y: 1.01 }, { x: 16.06, y: -1.02 }]} />
    <courtyardrect pcbX="-0.005mm" pcbY="-0.005mm" width="32.63mm" height="2.53mm" />
  </footprint>
)

// jclpcb:HDR-TH_6P-P2.54-V-M-R2-C3-S2.54-1   (from froemix.kicad_pcb)
export const FP_jclpcb_HDR_TH_6P_P2_54_V_M_R2_C3_S2_54_1 = (
  <footprint insertionDirection="from_above">
    <platedhole pcbX="-2.54mm" pcbY="-1.27mm" portHints={["pin1"]} shape="circular_hole_with_rect_pad" holeDiameter="1.05mm" rectPadWidth="1.8mm" rectPadHeight="1.8mm" holeOffsetX="0mm" holeOffsetY="0mm" />
    <platedhole pcbX="-2.54mm" pcbY="1.27mm" portHints={["pin2"]} shape="circle" holeDiameter="1.05mm" outerDiameter="1.8mm" />
    <platedhole pcbX="0mm" pcbY="-1.27mm" portHints={["pin3"]} shape="circle" holeDiameter="1.05mm" outerDiameter="1.8mm" />
    <platedhole pcbX="0mm" pcbY="1.27mm" portHints={["pin4"]} shape="circle" holeDiameter="1.05mm" outerDiameter="1.8mm" />
    <platedhole pcbX="2.54mm" pcbY="-1.27mm" portHints={["pin5"]} shape="circle" holeDiameter="1.05mm" outerDiameter="1.8mm" />
    <platedhole pcbX="2.54mm" pcbY="1.27mm" portHints={["pin6"]} shape="circle" holeDiameter="1.05mm" outerDiameter="1.8mm" />
    <silkscreenpath route={[{ x: -3.81, y: 2.54 }, { x: -3.81, y: -2.54 }]} />
    <silkscreenpath route={[{ x: -3.81, y: 0 }, { x: -1.27, y: 0 }]} />
    <silkscreenpath route={[{ x: -3.81, y: -2.54 }, { x: 3.81, y: -2.54 }]} />
    <silkscreenpath route={[{ x: -1.27, y: 0 }, { x: -1.27, y: -2.54 }]} />
    <silkscreenpath route={[{ x: 3.81, y: 2.54 }, { x: -3.81, y: 2.54 }]} />
    <silkscreenpath route={[{ x: 3.81, y: -2.54 }, { x: -1.05, y: -2.54 }]} />
    <silkscreenpath route={[{ x: 3.81, y: -2.54 }, { x: 3.81, y: 2.54 }]} />
    <courtyardrect pcbX="0mm" pcbY="0mm" width="8.12mm" height="5.58mm" />
  </footprint>
)

// jclpcb:HDR-TH_7P-P2.00-V-M   (from froemix.kicad_pcb)
export const FP_jclpcb_HDR_TH_7P_P2_00_V_M = (
  <footprint insertionDirection="from_above">
    <platedhole pcbX="-6mm" pcbY="0mm" portHints={["pin1"]} shape="circular_hole_with_rect_pad" holeDiameter="0.9mm" rectPadWidth="1.52mm" rectPadHeight="1.4mm" holeOffsetX="0mm" holeOffsetY="0mm" />
    <platedhole pcbX="-4mm" pcbY="0mm" portHints={["pin2"]} shape="circle" holeDiameter="0.9mm" outerDiameter="1.4mm" />
    <platedhole pcbX="-2mm" pcbY="0mm" portHints={["pin3"]} shape="circle" holeDiameter="0.9mm" outerDiameter="1.4mm" />
    <platedhole pcbX="0mm" pcbY="0mm" portHints={["pin4"]} shape="circle" holeDiameter="0.9mm" outerDiameter="1.4mm" />
    <platedhole pcbX="2mm" pcbY="0mm" portHints={["pin5"]} shape="circle" holeDiameter="0.9mm" outerDiameter="1.4mm" />
    <platedhole pcbX="4mm" pcbY="0mm" portHints={["pin6"]} shape="circle" holeDiameter="0.9mm" outerDiameter="1.4mm" />
    <platedhole pcbX="6mm" pcbY="0mm" portHints={["pin7"]} shape="circle" holeDiameter="0.9mm" outerDiameter="1.4mm" />
    <silkscreenpath route={[{ x: -4.97, y: 1.02 }, { x: -4.97, y: -0.89 }]} />
    <courtyardrect pcbX="-0.03mm" pcbY="0.065mm" width="13.96mm" height="2.41mm" />
  </footprint>
)

// jclpcb:HDR-TH_8P-P2.00-V-M   (from froemix.kicad_pcb)
export const FP_jclpcb_HDR_TH_8P_P2_00_V_M = (
  <footprint insertionDirection="from_above">
    <platedhole pcbX="-7mm" pcbY="0mm" portHints={["pin1"]} shape="circular_hole_with_rect_pad" holeDiameter="0.9mm" rectPadWidth="1.52mm" rectPadHeight="1.4mm" holeOffsetX="0mm" holeOffsetY="0mm" />
    <platedhole pcbX="-5mm" pcbY="0mm" portHints={["pin2"]} shape="circle" holeDiameter="0.9mm" outerDiameter="1.4mm" />
    <platedhole pcbX="-3mm" pcbY="0mm" portHints={["pin3"]} shape="circle" holeDiameter="0.9mm" outerDiameter="1.4mm" />
    <platedhole pcbX="-1mm" pcbY="0mm" portHints={["pin4"]} shape="circle" holeDiameter="0.9mm" outerDiameter="1.4mm" />
    <platedhole pcbX="1mm" pcbY="0mm" portHints={["pin5"]} shape="circle" holeDiameter="0.9mm" outerDiameter="1.4mm" />
    <platedhole pcbX="3mm" pcbY="0mm" portHints={["pin6"]} shape="circle" holeDiameter="0.9mm" outerDiameter="1.4mm" />
    <platedhole pcbX="5mm" pcbY="0mm" portHints={["pin7"]} shape="circle" holeDiameter="0.9mm" outerDiameter="1.4mm" />
    <platedhole pcbX="7mm" pcbY="0mm" portHints={["pin8"]} shape="circle" holeDiameter="0.9mm" outerDiameter="1.4mm" />
    <silkscreenpath route={[{ x: -5.97, y: 1.02 }, { x: -5.97, y: -0.89 }]} />
    <courtyardrect pcbX="-0.03mm" pcbY="0.065mm" width="15.96mm" height="2.41mm" />
  </footprint>
)

// jclpcb:L0603   (from froemix.kicad_pcb)
export const FP_jclpcb_L0603 = (
  <footprint>
    <smtpad pcbX="-0.7mm" pcbY="0mm" portHints={["pin1"]} shape="rect" width="0.8mm" height="0.86mm" layer="top" />
    <smtpad pcbX="0.7mm" pcbY="0mm" portHints={["pin2"]} shape="rect" width="0.8mm" height="0.86mm" layer="top" />
    <silkscreenpath route={[{ x: -1.43, y: 0.51 }, { x: -1.28, y: 0.67 }]} />
    <silkscreenpath route={[{ x: -1.43, y: -0.5 }, { x: -1.43, y: 0.51 }]} />
    <silkscreenpath route={[{ x: -1.28, y: 0.67 }, { x: -0.51, y: 0.67 }]} />
    <silkscreenpath route={[{ x: -1.28, y: -0.65 }, { x: -1.43, y: -0.5 }]} />
    <silkscreenpath route={[{ x: -1.28, y: -0.65 }, { x: -0.51, y: -0.65 }]} />
    <silkscreenpath route={[{ x: 1.25, y: 0.67 }, { x: 0.49, y: 0.67 }]} />
    <silkscreenpath route={[{ x: 1.25, y: 0.67 }, { x: 1.41, y: 0.51 }]} />
    <silkscreenpath route={[{ x: 1.25, y: -0.65 }, { x: 0.49, y: -0.65 }]} />
    <silkscreenpath route={[{ x: 1.41, y: 0.51 }, { x: 1.41, y: -0.5 }]} />
    <silkscreenpath route={[{ x: 1.41, y: -0.5 }, { x: 1.25, y: -0.65 }]} />
    <courtyardrect pcbX="-0.01mm" pcbY="0.01mm" width="3.34mm" height="1.82mm" />
  </footprint>
)

// jclpcb:OPTO-SMD-5_L3.7-W4.6-P1.27-LS7.0-BR   (from froemix.kicad_pcb)
export const FP_jclpcb_OPTO_SMD_5_L3_7_W4_6_P1_27_LS7_0_BR = (
  <footprint>
    <smtpad pcbX="3.15mm" pcbY="-1.27mm" portHints={["pin1"]} shape="pill" width="2mm" height="0.65mm" radius="0.325mm" layer="top" />
    <smtpad pcbX="3.17mm" pcbY="1.27mm" portHints={["pin3"]} shape="pill" width="2mm" height="0.65mm" radius="0.325mm" layer="top" />
    <smtpad pcbX="-3.15mm" pcbY="1.27mm" portHints={["pin4"]} shape="pill" width="2mm" height="0.65mm" radius="0.325mm" layer="top" />
    <smtpad pcbX="-3.18mm" pcbY="0mm" portHints={["pin5"]} shape="pill" width="2mm" height="0.65mm" radius="0.325mm" layer="top" />
    <smtpad pcbX="-3.15mm" pcbY="-1.27mm" portHints={["pin6"]} shape="pill" width="2mm" height="0.65mm" radius="0.325mm" layer="top" />
    <silkscreenpath route={[{ x: -2.3, y: 1.9 }, { x: 2.3, y: 1.9 }]} />
    <silkscreenpath route={[{ x: -2.3, y: 1.79 }, { x: -2.3, y: 1.9 }]} />
    <silkscreenpath route={[{ x: -2.3, y: 0.74 }, { x: -2.3, y: 0.52 }]} />
    <silkscreenpath route={[{ x: -2.3, y: -0.52 }, { x: -2.3, y: -0.75 }]} />
    <silkscreenpath route={[{ x: -2.3, y: -1.8 }, { x: -2.3, y: -1.9 }]} />
    <silkscreenpath route={[{ x: -2.3, y: -1.9 }, { x: -2.3, y: -1.8 }]} />
    <silkscreenpath route={[{ x: -2.3, y: -1.9 }, { x: -0.51, y: -1.9 }]} />
    <silkscreenpath route={[{ x: 2.3, y: 1.8 }, { x: 2.3, y: 1.9 }]} />
    <silkscreenpath route={[{ x: 2.3, y: -0.75 }, { x: 2.3, y: 0.74 }]} />
    <silkscreenpath route={[{ x: 2.3, y: -1.9 }, { x: 0.51, y: -1.9 }]} />
    <silkscreenpath route={[{ x: 2.3, y: -1.9 }, { x: 2.3, y: -1.8 }]} />
    <courtyardrect pcbX="-0.005mm" pcbY="0mm" width="7.5mm" height="5.04mm" />
  </footprint>
)

// jclpcb:PWRM-TH_4P-L11.5-W6.0-P2.54_B1503S   (from froemix.kicad_pcb)
export const FP_jclpcb_PWRM_TH_4P_L11_5_W6_0_P2_54_B1503S = (
  <footprint>
    <platedhole pcbX="-3.81mm" pcbY="0mm" portHints={["pin1"]} shape="circle" holeDiameter="1mm" outerDiameter="1.8mm" />
    <platedhole pcbX="-1.27mm" pcbY="0mm" portHints={["pin2"]} shape="circle" holeDiameter="1mm" outerDiameter="1.8mm" />
    <platedhole pcbX="1.27mm" pcbY="0mm" portHints={["pin3"]} shape="circle" holeDiameter="1mm" outerDiameter="1.8mm" />
    <platedhole pcbX="3.81mm" pcbY="0mm" portHints={["pin4"]} shape="circle" holeDiameter="1mm" outerDiameter="1.8mm" />
    <silkscreenpath route={[{ x: -5.75, y: 5.43 }, { x: 5.75, y: 5.43 }]} />
    <silkscreenpath route={[{ x: -5.75, y: -0.59 }, { x: -5.75, y: 5.41 }]} />
    <silkscreenpath route={[{ x: -5.75, y: -0.59 }, { x: -4.78, y: -0.59 }]} />
    <silkscreenpath route={[{ x: -2.85, y: -0.59 }, { x: -2.24, y: -0.59 }]} />
    <silkscreenpath route={[{ x: -0.31, y: -0.59 }, { x: 0.3, y: -0.59 }]} />
    <silkscreenpath route={[{ x: 2.23, y: -0.59 }, { x: 2.84, y: -0.59 }]} />
    <silkscreenpath route={[{ x: 4.77, y: -0.59 }, { x: 5.75, y: -0.59 }]} />
    <silkscreenpath route={[{ x: 5.75, y: 5.41 }, { x: 5.75, y: -0.59 }]} />
    <courtyardrect pcbX="0mm" pcbY="2.265mm" width="12mm" height="6.83mm" />
  </footprint>
)

// jclpcb:R0603   (from froemix.kicad_pcb)
export const FP_jclpcb_R0603 = (
  <footprint>
    <smtpad pcbX="-0.75mm" pcbY="0mm" portHints={["pin1"]} shape="rect" width="0.81mm" height="0.86mm" layer="top" />
    <smtpad pcbX="0.75mm" pcbY="0mm" portHints={["pin2"]} shape="rect" width="0.81mm" height="0.86mm" layer="top" />
    <silkscreenpath route={[{ x: -1.39, y: 0.66 }, { x: -0.43, y: 0.66 }]} />
    <silkscreenpath route={[{ x: -1.39, y: -0.66 }, { x: -1.39, y: 0.66 }]} />
    <silkscreenpath route={[{ x: -0.43, y: -0.66 }, { x: -1.39, y: -0.66 }]} />
    <silkscreenpath route={[{ x: 0.43, y: -0.66 }, { x: 1.39, y: -0.66 }]} />
    <silkscreenpath route={[{ x: 1.39, y: 0.66 }, { x: 0.43, y: 0.66 }]} />
    <silkscreenpath route={[{ x: 1.39, y: -0.66 }, { x: 1.39, y: 0.66 }]} />
    <courtyardrect pcbX="0mm" pcbY="0mm" width="3.28mm" height="1.82mm" />
  </footprint>
)

// jclpcb:R1206   (from froemix.kicad_pcb)
export const FP_jclpcb_R1206 = (
  <footprint>
    <smtpad pcbX="-1.48mm" pcbY="0mm" portHints={["pin1"]} shape="rect" width="1.21mm" height="1.7mm" layer="top" />
    <smtpad pcbX="1.48mm" pcbY="0mm" portHints={["pin2"]} shape="rect" width="1.21mm" height="1.7mm" layer="top" />
    <silkscreenpath route={[{ x: -2.31, y: 1.08 }, { x: -0.95, y: 1.08 }]} />
    <silkscreenpath route={[{ x: -2.31, y: -1.08 }, { x: -2.31, y: 1.08 }]} />
    <silkscreenpath route={[{ x: -0.95, y: -1.08 }, { x: -2.31, y: -1.08 }]} />
    <silkscreenpath route={[{ x: 0.95, y: -1.08 }, { x: 2.31, y: -1.08 }]} />
    <silkscreenpath route={[{ x: 2.31, y: 1.08 }, { x: 0.95, y: 1.08 }]} />
    <silkscreenpath route={[{ x: 2.31, y: -1.08 }, { x: 2.31, y: 1.08 }]} />
    <courtyardrect pcbX="0mm" pcbY="0mm" width="5.12mm" height="2.66mm" />
  </footprint>
)

// jclpcb:R2512   (authored here, no KiCad counterpart: the 1W 22R/33R MIDI
// OUT parts. 2512 body 6.35x3.2mm, pads 1.5x2.8mm on a 5.2mm centre spacing,
// silk drawn the way the jclpcb resistor footprints draw it - outside the pads,
// 0.23mm clear of the pad edge - and a 7.2x3.7mm courtyard, i.e. 0.25mm of
// margin on every side of the 6.35x3.2mm body (the old 3.3mm height left only
// 0.05mm above/below it). tools/check_footprints.py asserts these pads against
// TSCIRCUIT_ONLY_FOOTPRINTS in tools/kicad2tscircuit.py, keep the three in sync.)
export const FP_jclpcb_R2512 = (
  <footprint>
    <smtpad pcbX="-2.6mm" pcbY="0mm" portHints={["pin1"]} shape="rect" width="1.5mm" height="2.8mm" layer="top" />
    <smtpad pcbX="2.6mm" pcbY="0mm" portHints={["pin2"]} shape="rect" width="1.5mm" height="2.8mm" layer="top" />
    <silkscreenpath route={[{ x: -3.575, y: 1.63 }, { x: -1.925, y: 1.63 }]} />
    <silkscreenpath route={[{ x: -3.575, y: -1.63 }, { x: -3.575, y: 1.63 }]} />
    <silkscreenpath route={[{ x: -1.925, y: -1.63 }, { x: -3.575, y: -1.63 }]} />
    <silkscreenpath route={[{ x: 1.925, y: -1.63 }, { x: 3.575, y: -1.63 }]} />
    <silkscreenpath route={[{ x: 3.575, y: 1.63 }, { x: 1.925, y: 1.63 }]} />
    <silkscreenpath route={[{ x: 3.575, y: -1.63 }, { x: 3.575, y: 1.63 }]} />
    <courtyardrect pcbX="0mm" pcbY="0mm" width="7.2mm" height="3.7mm" />
  </footprint>
)

// jclpcb:SOD-123_L2.7-W1.6-LS3.7-RD-1   (from froemix.kicad_pcb)
export const FP_jclpcb_SOD_123_L2_7_W1_6_LS3_7_RD_1 = (
  <footprint>
    <smtpad pcbX="-1.7mm" pcbY="0mm" portHints={["pin1"]} shape="rect" width="1.2mm" height="0.95mm" layer="top" />
    <smtpad pcbX="1.7mm" pcbY="0mm" portHints={["pin2"]} shape="rect" width="1.2mm" height="0.95mm" layer="top" />
    <silkscreenpath route={[{ x: -1.64, y: 0.85 }, { x: -1.64, y: 0.85 }]} />
    <silkscreenpath route={[{ x: -1.64, y: -0.85 }, { x: -1.64, y: -0.85 }]} />
    <silkscreenpath route={[{ x: -1.35, y: 0.85 }, { x: -1.64, y: 0.85 }]} />
    <silkscreenpath route={[{ x: -1.35, y: 0.85 }, { x: 1.35, y: 0.85 }]} />
    <silkscreenpath route={[{ x: -1.35, y: -0.85 }, { x: -1.64, y: -0.85 }]} />
    <silkscreenpath route={[{ x: -0.28, y: 0 }, { x: -0.61, y: 0 }]} />
    <silkscreenpath route={[{ x: -0.25, y: 0.51 }, { x: -0.25, y: -0.51 }]} />
    <silkscreenpath route={[{ x: -0.18, y: 0 }, { x: 0.41, y: 0.41 }]} />
    <silkscreenpath route={[{ x: 0.41, y: 0.41 }, { x: 0.41, y: -0.41 }]} />
    <silkscreenpath route={[{ x: 0.41, y: 0 }, { x: 0.72, y: 0 }]} />
    <silkscreenpath route={[{ x: 0.41, y: -0.41 }, { x: -0.18, y: 0 }]} />
    <silkscreenpath route={[{ x: 1.35, y: 0.85 }, { x: 1.35, y: 0.71 }]} />
    <silkscreenpath route={[{ x: 1.35, y: -0.71 }, { x: 1.35, y: -0.85 }]} />
    <silkscreenpath route={[{ x: 1.35, y: -0.85 }, { x: -1.35, y: -0.85 }]} />
    <courtyardrect pcbX="0mm" pcbY="0mm" width="5.1mm" height="2.2mm" />
  </footprint>
)

// jclpcb:SOD-323_L1.8-W1.3-LS2.5-RD   (from froemix.kicad_pcb)
export const FP_jclpcb_SOD_323_L1_8_W1_3_LS2_5_RD = (
  <footprint>
    <smtpad pcbX="-1.17mm" pcbY="0mm" portHints={["pin1"]} shape="rect" width="1mm" height="0.75mm" layer="top" />
    <smtpad pcbX="1.17mm" pcbY="0mm" portHints={["pin2"]} shape="rect" width="1mm" height="0.75mm" layer="top" />
    <silkscreenpath route={[{ x: -0.85, y: 0.73 }, { x: 0.91, y: 0.73 }]} />
    <silkscreenpath route={[{ x: -0.85, y: -0.73 }, { x: 0.91, y: -0.73 }]} />
    <silkscreenpath route={[{ x: -0.44, y: 0.73 }, { x: -0.44, y: -0.73 }]} />
    <silkscreenpath route={[{ x: 0.91, y: 0.73 }, { x: 0.91, y: 0.53 }]} />
    <silkscreenpath route={[{ x: 0.91, y: -0.73 }, { x: 0.91, y: -0.52 }]} />
    <courtyardrect pcbX="0mm" pcbY="0mm" width="3.84mm" height="1.96mm" />
  </footprint>
)

// jclpcb:SOIC-8_L4.9-W3.9-P1.27-LS6.0-BL   (from froemix.kicad_pcb)
export const FP_jclpcb_SOIC_8_L4_9_W3_9_P1_27_LS6_0_BL = (
  <footprint>
    <smtpad pcbX="-1.91mm" pcbY="-2.77mm" portHints={["pin1"]} shape="pill" width="0.59mm" height="2.05mm" radius="0.295mm" layer="top" />
    <smtpad pcbX="-0.63mm" pcbY="-2.77mm" portHints={["pin2"]} shape="pill" width="0.59mm" height="2.05mm" radius="0.295mm" layer="top" />
    <smtpad pcbX="0.63mm" pcbY="-2.77mm" portHints={["pin3"]} shape="pill" width="0.59mm" height="2.05mm" radius="0.295mm" layer="top" />
    <smtpad pcbX="1.9mm" pcbY="-2.77mm" portHints={["pin4"]} shape="pill" width="0.59mm" height="2.05mm" radius="0.295mm" layer="top" />
    <smtpad pcbX="1.9mm" pcbY="2.77mm" portHints={["pin5"]} shape="pill" width="0.59mm" height="2.05mm" radius="0.295mm" layer="top" />
    <smtpad pcbX="0.63mm" pcbY="2.77mm" portHints={["pin6"]} shape="pill" width="0.59mm" height="2.05mm" radius="0.295mm" layer="top" />
    <smtpad pcbX="-0.63mm" pcbY="2.77mm" portHints={["pin7"]} shape="pill" width="0.59mm" height="2.05mm" radius="0.295mm" layer="top" />
    <smtpad pcbX="-1.91mm" pcbY="2.77mm" portHints={["pin8"]} shape="pill" width="0.59mm" height="2.05mm" radius="0.295mm" layer="top" />
    <silkscreenpath route={[{ x: -2.53, y: 1.52 }, { x: 2.53, y: 1.52 }]} />
    <silkscreenpath route={[{ x: -2.53, y: -1.52 }, { x: -2.53, y: 1.52 }]} />
    <silkscreenpath route={[{ x: 2.53, y: 1.52 }, { x: 2.53, y: -1.52 }]} />
    <silkscreenpath route={[{ x: 2.53, y: -1.52 }, { x: -2.53, y: -1.52 }]} />
    <courtyardrect pcbX="0mm" pcbY="0mm" width="5.56mm" height="8.09mm" />
  </footprint>
)

// jclpcb:SOP-16_L9.9-W3.9-P1.27-LS6.1-BL   (from froemix.kicad_pcb)
export const FP_jclpcb_SOP_16_L9_9_W3_9_P1_27_LS6_1_BL = (
  <footprint>
    <smtpad pcbX="-4.44mm" pcbY="-2.6mm" portHints={["pin1"]} shape="pill" width="0.6mm" height="1.6mm" radius="0.3mm" layer="top" />
    <smtpad pcbX="-3.17mm" pcbY="-2.6mm" portHints={["pin2"]} shape="pill" width="0.6mm" height="1.6mm" radius="0.3mm" layer="top" />
    <smtpad pcbX="-1.9mm" pcbY="-2.6mm" portHints={["pin3"]} shape="pill" width="0.6mm" height="1.6mm" radius="0.3mm" layer="top" />
    <smtpad pcbX="-0.63mm" pcbY="-2.6mm" portHints={["pin4"]} shape="pill" width="0.6mm" height="1.6mm" radius="0.3mm" layer="top" />
    <smtpad pcbX="0.64mm" pcbY="-2.6mm" portHints={["pin5"]} shape="pill" width="0.6mm" height="1.6mm" radius="0.3mm" layer="top" />
    <smtpad pcbX="1.91mm" pcbY="-2.6mm" portHints={["pin6"]} shape="pill" width="0.6mm" height="1.6mm" radius="0.3mm" layer="top" />
    <smtpad pcbX="3.18mm" pcbY="-2.6mm" portHints={["pin7"]} shape="pill" width="0.6mm" height="1.6mm" radius="0.3mm" layer="top" />
    <smtpad pcbX="4.44mm" pcbY="-2.6mm" portHints={["pin8"]} shape="pill" width="0.6mm" height="1.6mm" radius="0.3mm" layer="top" />
    <smtpad pcbX="4.44mm" pcbY="2.6mm" portHints={["pin9"]} shape="pill" width="0.6mm" height="1.6mm" radius="0.3mm" layer="top" />
    <smtpad pcbX="3.18mm" pcbY="2.6mm" portHints={["pin10"]} shape="pill" width="0.6mm" height="1.6mm" radius="0.3mm" layer="top" />
    <smtpad pcbX="1.91mm" pcbY="2.6mm" portHints={["pin11"]} shape="pill" width="0.6mm" height="1.6mm" radius="0.3mm" layer="top" />
    <smtpad pcbX="0.64mm" pcbY="2.6mm" portHints={["pin12"]} shape="pill" width="0.6mm" height="1.6mm" radius="0.3mm" layer="top" />
    <smtpad pcbX="-0.63mm" pcbY="2.6mm" portHints={["pin13"]} shape="pill" width="0.6mm" height="1.6mm" radius="0.3mm" layer="top" />
    <smtpad pcbX="-1.9mm" pcbY="2.6mm" portHints={["pin14"]} shape="pill" width="0.6mm" height="1.6mm" radius="0.3mm" layer="top" />
    <smtpad pcbX="-3.17mm" pcbY="2.6mm" portHints={["pin15"]} shape="pill" width="0.6mm" height="1.6mm" radius="0.3mm" layer="top" />
    <smtpad pcbX="-4.44mm" pcbY="2.6mm" portHints={["pin16"]} shape="pill" width="0.6mm" height="1.6mm" radius="0.3mm" layer="top" />
    <silkscreenpath route={[{ x: -5, y: 2 }, { x: -5, y: 0.75 }]} />
    <silkscreenpath route={[{ x: -5, y: 2 }, { x: -4.98, y: 2 }]} />
    <silkscreenpath route={[{ x: -5, y: -2 }, { x: -5, y: -0.75 }]} />
    <silkscreenpath route={[{ x: -5, y: -2 }, { x: -4.98, y: -2 }]} />
    <silkscreenpath route={[{ x: -3.92, y: 2 }, { x: -3.71, y: 2 }]} />
    <silkscreenpath route={[{ x: -3.92, y: -2 }, { x: -3.71, y: -2 }]} />
    <silkscreenpath route={[{ x: -2.65, y: 2 }, { x: -2.44, y: 2 }]} />
    <silkscreenpath route={[{ x: -2.65, y: -2 }, { x: -2.44, y: -2 }]} />
    <silkscreenpath route={[{ x: -1.38, y: 2 }, { x: -1.17, y: 2 }]} />
    <silkscreenpath route={[{ x: -1.38, y: -2 }, { x: -1.17, y: -2 }]} />
    <silkscreenpath route={[{ x: -0.11, y: 2 }, { x: 0.1, y: 2 }]} />
    <silkscreenpath route={[{ x: -0.11, y: -2 }, { x: 0.1, y: -2 }]} />
    <silkscreenpath route={[{ x: 1.16, y: 2 }, { x: 1.37, y: 2 }]} />
    <silkscreenpath route={[{ x: 1.16, y: -2 }, { x: 1.37, y: -2 }]} />
    <silkscreenpath route={[{ x: 2.43, y: 2 }, { x: 2.64, y: 2 }]} />
    <silkscreenpath route={[{ x: 2.43, y: -2 }, { x: 2.64, y: -2 }]} />
    <silkscreenpath route={[{ x: 3.7, y: 2 }, { x: 3.91, y: 2 }]} />
    <silkscreenpath route={[{ x: 3.7, y: -2 }, { x: 3.91, y: -2 }]} />
    <silkscreenpath route={[{ x: 4.97, y: 2 }, { x: 5, y: 2 }]} />
    <silkscreenpath route={[{ x: 4.97, y: -2 }, { x: 5, y: -2 }]} />
    <silkscreenpath route={[{ x: 5, y: -2 }, { x: 5, y: 2 }]} />
    <courtyardrect pcbX="0mm" pcbY="0mm" width="10.5mm" height="7.3mm" />
  </footprint>
)

// jclpcb:SOT-23-3_L2.9-W1.6-P1.90-LS2.8-BR   (from froemix.kicad_pcb)
export const FP_jclpcb_SOT_23_3_L2_9_W1_6_P1_90_LS2_8_BR = (
  <footprint>
    <smtpad pcbX="1.24mm" pcbY="-0.95mm" portHints={["pin1"]} shape="rect" width="1.07mm" height="0.6mm" layer="top" />
    <smtpad pcbX="1.24mm" pcbY="0.95mm" portHints={["pin2"]} shape="rect" width="1.07mm" height="0.6mm" layer="top" />
    <smtpad pcbX="-1.24mm" pcbY="0mm" portHints={["pin3"]} shape="rect" width="1.07mm" height="0.6mm" layer="top" />
    <silkscreenpath route={[{ x: -0.88, y: 1.54 }, { x: -0.88, y: 0.49 }]} />
    <silkscreenpath route={[{ x: -0.88, y: -1.54 }, { x: -0.88, y: -0.49 }]} />
    <silkscreenpath route={[{ x: 0.88, y: 1.54 }, { x: -0.88, y: 1.54 }]} />
    <silkscreenpath route={[{ x: 0.88, y: 0.46 }, { x: 0.88, y: -0.46 }]} />
    <silkscreenpath route={[{ x: 0.88, y: -1.54 }, { x: -0.88, y: -1.54 }]} />
    <courtyardrect pcbX="0mm" pcbY="0mm" width="4.05mm" height="3.58mm" />
  </footprint>
)

// jclpcb:SOT-23-3_L3.0-W1.7-P0.95-LS2.9-BR   (from froemix.kicad_pcb)
export const FP_jclpcb_SOT_23_3_L3_0_W1_7_P0_95_LS2_9_BR = (
  <footprint>
    <smtpad pcbX="1.1mm" pcbY="-0.95mm" portHints={["pin1"]} shape="rect" width="1.04mm" height="0.53mm" layer="top" />
    <smtpad pcbX="1.1mm" pcbY="0.95mm" portHints={["pin2"]} shape="rect" width="1.04mm" height="0.53mm" layer="top" />
    <smtpad pcbX="-1.1mm" pcbY="0mm" portHints={["pin3"]} shape="rect" width="1.04mm" height="0.53mm" layer="top" />
    <silkscreenpath route={[{ x: -0.86, y: 1.53 }, { x: -0.86, y: 0.49 }]} />
    <silkscreenpath route={[{ x: -0.86, y: -1.53 }, { x: -0.86, y: -0.49 }]} />
    <silkscreenpath route={[{ x: 0.86, y: 1.53 }, { x: -0.86, y: 1.53 }]} />
    <silkscreenpath route={[{ x: 0.86, y: 0.46 }, { x: 0.86, y: -0.46 }]} />
    <silkscreenpath route={[{ x: 0.86, y: -1.53 }, { x: -0.86, y: -1.53 }]} />
    <courtyardrect pcbX="0mm" pcbY="0mm" width="3.74mm" height="3.56mm" />
  </footprint>
)

// jclpcb:TSSOP-14_L5.0-W4.4-P0.65-LS6.4-BL   (from froemix.kicad_pcb)
export const FP_jclpcb_TSSOP_14_L5_0_W4_4_P0_65_LS6_4_BL = (
  <footprint>
    <smtpad pcbX="-1.95mm" pcbY="-2.8mm" portHints={["pin1"]} shape="rect" width="0.4mm" height="1.7mm" layer="top" />
    <smtpad pcbX="-1.3mm" pcbY="-2.8mm" portHints={["pin2"]} shape="rect" width="0.4mm" height="1.7mm" layer="top" />
    <smtpad pcbX="-0.65mm" pcbY="-2.8mm" portHints={["pin3"]} shape="rect" width="0.4mm" height="1.7mm" layer="top" />
    <smtpad pcbX="0mm" pcbY="-2.8mm" portHints={["pin4"]} shape="rect" width="0.4mm" height="1.7mm" layer="top" />
    <smtpad pcbX="0.65mm" pcbY="-2.8mm" portHints={["pin5"]} shape="rect" width="0.4mm" height="1.7mm" layer="top" />
    <smtpad pcbX="1.3mm" pcbY="-2.8mm" portHints={["pin6"]} shape="rect" width="0.4mm" height="1.7mm" layer="top" />
    <smtpad pcbX="1.95mm" pcbY="-2.8mm" portHints={["pin7"]} shape="rect" width="0.4mm" height="1.7mm" layer="top" />
    <smtpad pcbX="1.95mm" pcbY="2.8mm" portHints={["pin8"]} shape="rect" width="0.4mm" height="1.7mm" layer="top" />
    <smtpad pcbX="1.3mm" pcbY="2.8mm" portHints={["pin9"]} shape="rect" width="0.4mm" height="1.7mm" layer="top" />
    <smtpad pcbX="0.65mm" pcbY="2.8mm" portHints={["pin10"]} shape="rect" width="0.4mm" height="1.7mm" layer="top" />
    <smtpad pcbX="0mm" pcbY="2.8mm" portHints={["pin11"]} shape="rect" width="0.4mm" height="1.7mm" layer="top" />
    <smtpad pcbX="-0.65mm" pcbY="2.8mm" portHints={["pin12"]} shape="rect" width="0.4mm" height="1.7mm" layer="top" />
    <smtpad pcbX="-1.3mm" pcbY="2.8mm" portHints={["pin13"]} shape="rect" width="0.4mm" height="1.7mm" layer="top" />
    <smtpad pcbX="-1.95mm" pcbY="2.8mm" portHints={["pin14"]} shape="rect" width="0.4mm" height="1.7mm" layer="top" />
    <silkscreenpath route={[{ x: -2.52, y: 1.6 }, { x: -2.52, y: 2.25 }]} />
    <silkscreenpath route={[{ x: -2.52, y: 0.69 }, { x: -2.52, y: 1.6 }]} />
    <silkscreenpath route={[{ x: -2.52, y: -1.6 }, { x: -2.52, y: -0.68 }]} />
    <silkscreenpath route={[{ x: -2.52, y: -1.6 }, { x: -2.52, y: -2.25 }]} />
    <silkscreenpath route={[{ x: -2.5, y: 2.25 }, { x: -2.39, y: 2.25 }]} />
    <silkscreenpath route={[{ x: -2.39, y: -2.25 }, { x: -2.5, y: -2.25 }]} />
    <silkscreenpath route={[{ x: 2.38, y: 2.25 }, { x: 2.5, y: 2.25 }]} />
    <silkscreenpath route={[{ x: 2.5, y: 2.25 }, { x: 2.5, y: -2.25 }]} />
    <silkscreenpath route={[{ x: 2.5, y: -2.25 }, { x: 2.38, y: -2.25 }]} />
    <courtyardrect pcbX="-0.01mm" pcbY="0mm" width="5.52mm" height="7.8mm" />
  </footprint>
)

import type { ChipProps } from "@tscircuit/props"

// LIBRARY COPY OF THIS FOOTPRINT: library/cad/rkjxt1f42001/footprint.tsx
// (the shared component library owns the part; its SOURCE.md carries the
// provenance and what was verified). This file is the same footprint expressed
// inside the board build, so the two cannot drift silently:
// `lib/rkjxt-nav.test.ts` compares the pad coordinates of both files.

// ALPS Alpine RKJXT1F42001: 4-direction stick switch with centre push and an
// encoder, 11 through-holes (10 electrical + 1 position lug).
//
// AUTHORED from the ALPS drawing, not imported from any vendor CAD. The ALPS
// page 398 drawing "PC board mounting hole dimensions (viewed from mounting
// side)" prints the hole pattern; the terminal NAMES it prints are A, B, C, D,
// com, Push, Ground terminal, Encoder A, Encoder B, Encoder com, plus the
// non-electrical "For position lug" hole. ALPS prints no pin NUMBERS, so the
// pin1..pin10 numbering below is this library's own, in the order the names are
// listed above. See SOURCE.md beside this file.
//
// Coordinates are millimetres in the footprint frame: +X right, +Y up, origin
// on the shaft axis at the PCB seating plane. Every value is read from the
// drawing at 0.4 mm/pt (15.6 mm across the pattern), and cross-checked against
// two local, non-redistributable STEP models - see SOURCE.md.
const pinLabels = {
  pin1: ["A"],             // direction A   (lever A per the output relation chart)
  pin2: ["B"],             // direction B
  pin3: ["C"],             // direction C
  pin4: ["D"],             // direction D
  pin5: ["com"],           // common of the four direction contacts
  pin6: ["Push"],          // centre-push contact
  pin7: ["Ground"],        // "Ground terminal" (metal body)
  pin8: ["EncoderA"],      // encoder phase A - unused on this board
  pin9: ["EncoderB"],      // encoder phase B - unused on this board
  pin10: ["EncoderCom"]    // encoder common  - unused on this board
} as const

/** Hole pattern, mm from the shaft axis. `hole` is 1.0 mm plated except the
 *  position lug, which the drawing marks as a separate 1.1 mm hole. */
const HOLES = {
  A: { x: -1.5, y: -7.8 },
  B: { x: 7.8, y: -1.5 },
  C: { x: 1.5, y: 7.8 },
  D: { x: -7.8, y: 1.5 },
  com: { x: -1.0, y: 5.78 },
  Push: { x: 1.0, y: -6.98 },
  Ground: { x: 6.86, y: -3.75 },
  EncoderA: { x: -7.8, y: -1.5 },
  EncoderB: { x: 7.8, y: 1.5 },
  EncoderCom: { x: -1.5, y: 7.8 }
} as const
const LUG = { x: -3.8, y: -1.5 } as const

// 1.0 mm drawn hole with a 1.5 mm pad: a 0.25 mm annular ring, above the
// JLCPCB PTH floor. The drawn terminal posts are flat blades 0.7 x 0.25 mm.
const HOLE_MM = 1.0
const PAD_MM = 1.5
const LUG_HOLE_MM = 1.1

export const RKJXT1F42001 = (props: ChipProps<typeof pinLabels>) => {
  return (
    <switch
      type="spst"
      pinLabels={pinLabels}
      manufacturerPartNumber="RKJXT1F42001"
      footprint={
        <footprint>
          <platedhole portHints={["pin1"]} shape="circle" holeDiameter={`${HOLE_MM}mm`} outerDiameter={`${PAD_MM}mm`} pcbX={`${HOLES.A.x}mm`} pcbY={`${HOLES.A.y}mm`} />
          <platedhole portHints={["pin2"]} shape="circle" holeDiameter={`${HOLE_MM}mm`} outerDiameter={`${PAD_MM}mm`} pcbX={`${HOLES.B.x}mm`} pcbY={`${HOLES.B.y}mm`} />
          <platedhole portHints={["pin3"]} shape="circle" holeDiameter={`${HOLE_MM}mm`} outerDiameter={`${PAD_MM}mm`} pcbX={`${HOLES.C.x}mm`} pcbY={`${HOLES.C.y}mm`} />
          <platedhole portHints={["pin4"]} shape="circle" holeDiameter={`${HOLE_MM}mm`} outerDiameter={`${PAD_MM}mm`} pcbX={`${HOLES.D.x}mm`} pcbY={`${HOLES.D.y}mm`} />
          <platedhole portHints={["pin5"]} shape="circle" holeDiameter={`${HOLE_MM}mm`} outerDiameter={`${PAD_MM}mm`} pcbX={`${HOLES.com.x}mm`} pcbY={`${HOLES.com.y}mm`} />
          <platedhole portHints={["pin6"]} shape="circle" holeDiameter={`${HOLE_MM}mm`} outerDiameter={`${PAD_MM}mm`} pcbX={`${HOLES.Push.x}mm`} pcbY={`${HOLES.Push.y}mm`} />
          <platedhole portHints={["pin7"]} shape="circle" holeDiameter={`${HOLE_MM}mm`} outerDiameter={`${PAD_MM}mm`} pcbX={`${HOLES.Ground.x}mm`} pcbY={`${HOLES.Ground.y}mm`} />
          <platedhole portHints={["pin8"]} shape="circle" holeDiameter={`${HOLE_MM}mm`} outerDiameter={`${PAD_MM}mm`} pcbX={`${HOLES.EncoderA.x}mm`} pcbY={`${HOLES.EncoderA.y}mm`} />
          <platedhole portHints={["pin9"]} shape="circle" holeDiameter={`${HOLE_MM}mm`} outerDiameter={`${PAD_MM}mm`} pcbX={`${HOLES.EncoderB.x}mm`} pcbY={`${HOLES.EncoderB.y}mm`} />
          <platedhole portHints={["pin10"]} shape="circle" holeDiameter={`${HOLE_MM}mm`} outerDiameter={`${PAD_MM}mm`} pcbX={`${HOLES.EncoderCom.x}mm`} pcbY={`${HOLES.EncoderCom.y}mm`} />
          {/* "For position lug" - mechanical locator, non-plated, no net. */}
          <hole pcbX={`${LUG.x}mm`} pcbY={`${LUG.y}mm`} diameter={`${LUG_HOLE_MM}mm`} />

          {/* 17 x 17 mm body, 14.5 mm across the flats (drawing "4-17",
              "4-14.5"). Only the four corner chamfers are drawn: the 1.5 mm
              pads at +-7.8 reach 8.55 mm, i.e. past the 8.5 mm body edge, so a
              full outline (or a mark on the +Y edge) would sit on copper. The
              designator is printed from lib/silk.tsx, not from here, and the
              part is keyed in rotation by its position lug. */}
          <silkscreenpath route={[
            { x: -7.25, y: 4.55 }, { x: -4.55, y: 7.25 }
          ]} strokeWidth="0.15mm" />
          <silkscreenpath route={[
            { x: 7.25, y: 4.55 }, { x: 4.55, y: 7.25 }
          ]} strokeWidth="0.15mm" />
          <silkscreenpath route={[
            { x: 7.25, y: -4.55 }, { x: 4.55, y: -7.25 }
          ]} strokeWidth="0.15mm" />
          <silkscreenpath route={[
            { x: -7.25, y: -4.55 }, { x: -4.55, y: -7.25 }
          ]} strokeWidth="0.15mm" />

          {/* The stationary collar, D7.20 on the axis, from 8.6 mm to 10.5 mm
              above the plane - the feature the panel opening has to clear. */}
          <silkscreencircle pcbX="0mm" pcbY="0mm" radius="3.6mm" strokeWidth="0.15mm" />
          {/* No cadModel: no redistributable STEP exists for this part
              (SOURCE.md). An omitted model leaves the 3D view empty instead of
              pointing at a vendor file that may not be redistributed. */}
          <courtyardrect pcbX="0mm" pcbY="0mm" width="17.2mm" height="17.2mm" />
        </footprint>
      }
      {...props}
    />
  )
}

// Harwin M20-9960245, 2.54 mm pitch SIL horizontal PC tail pin header, 2 ways,
// right angle. Primary page https://www.harwin.com/products/M20-9960245/ and
// drawing SHA-256 3cd367d6b76e7c6be501578c87a4a9c89721e36a4a8bc36d5e8340646bdcd61b,
// retrieved 2026-11-10.
//
// SOURCED on that drawing: 2 ways, 2.54 +-0.10 mm pitch, Ø1.02 +-0.05 mm
// recommended PCB hole, 0.64 +-0.02 mm square pin, 6.10 mm mating length, and a
// true right angle: the long mating pins run PARALLEL to the PCB and the short
// 3.00 mm PC tails PERPENDICULAR to it.
//
// NOT SOURCED, and recorded as engineering assumptions in lib/layout.ts:
//  * the 1.60 mm pad (the drill is the manufacturer's; the pad is ours);
//  * the housing envelope. The exact hole-to-housing offset is unresolved - the
//    drawing's 4.80 mm datum is unclear and sheet 1 of 2 is missing - so the
//    SLOTTED KEEPOUT in lib/layout.ts (POWER_HEADER_KEEPOUT) covers both
//    plausible readings and is tested against the built board and the bracket.
//    This footprint's silkscreen shows only the housing's BOARD-SIDE footprint;
//    the part overhangs the notch, and silk cannot print off the board.
//
// LOCAL FRAME: the mating axis is local -Y and the board-side housing is local
// +Y, because tscircuit mirrors the bottom layer: measured on the BUILT output,
// this footprint at (bottom, 180 deg) maps local (x, y) to global (x, -y). So
// local -Y points to global +Y, i.e. the pins point into the notch as the owner
// requires. `lib/power-header.test.ts` proves that on the BUILT output (layer,
// rotation, pad row, direction marker), never from this comment.
import type { ConnectorProps } from "@tscircuit/props"

const pinLabels = { pin1: ["pin1"], pin2: ["pin2"] } as const

export const M20_9960245 = (props: ConnectorProps) => {
  return (
    <connector
      pinCount={2}
      pinLabels={pinLabels}
      manufacturerPartNumber="M20-9960245"
      footprint={<footprint insertionDirection="from_above">
        <platedhole portHints={["pin1"]} pcbX="-1.27mm" pcbY="0mm" shape="circle" outerDiameter="1.6mm" holeDiameter="1.02mm" />
        <platedhole portHints={["pin2"]} pcbX="1.27mm" pcbY="0mm" shape="circle" outerDiameter="1.6mm" holeDiameter="1.02mm" />
        {/* Housing footprint on the board only: back edge 2.54 mm behind the pad
            row, sides 3.81 mm from the centre, front edge 0.40 mm in front.
            The mating side (+Y) is marked by the arrow. */}
        <silkscreenpath
          route={[
            { x: -3.81, y: 2.54 }, { x: 3.81, y: 2.54 },
            { x: 3.81, y: -0.4 }, { x: -3.81, y: -0.4 }, { x: -3.81, y: 2.54 },
          ]}
          strokeWidth="0.15mm"
        />
        {/* Pin-1 mark: a short bar beside pin 1, on the board. */}
        <silkscreenpath route={[{ x: -1.62, y: 1.7 }, { x: -0.92, y: 1.7 }]} strokeWidth="0.4mm" />
        {/* Mating-direction arrow: local -Y, i.e. global +Y once placed. */}
        <silkscreenpath route={[{ x: 2.9, y: 1.4 }, { x: 2.9, y: -0.2 }]} strokeWidth="0.25mm" />
        <silkscreenpath route={[{ x: 2.45, y: 0.25 }, { x: 2.9, y: -0.2 }]} strokeWidth="0.25mm" />
        <silkscreenpath route={[{ x: 3.35, y: 0.25 }, { x: 2.9, y: -0.2 }]} strokeWidth="0.25mm" />
        {/* 2026-11-13: printed to the LEFT of the body. Above it, "J_PWR" landed inside
              the POWER button's body once both moved to the top-right corner. */}
        <silkscreentext text="{NAME}" pcbX="-7.5mm" pcbY="0mm" anchorAlignment="center" fontSize="0.8mm" />
        {/* Courtyard: the housing's board-side footprint only. The overhang and
            the plug are covered by the layout keepout tests, not by this box. */}
        <courtyardoutline
          outline={[
            { x: -3.95, y: 2.7 }, { x: 3.95, y: 2.7 },
            { x: 3.95, y: -0.55 }, { x: -3.95, y: -0.55 },
          ]}
        />
      </footprint>}
      {...props}
    />
  )
}

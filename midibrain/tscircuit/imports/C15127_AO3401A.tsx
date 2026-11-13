import type { TransistorProps } from "@tscircuit/props"

// Alpha & Omega AO3401A, 30 V P-channel MOSFET, SOT-23, LCSC/JLCPCB C15127.
// Primary source: AOS AO3401A datasheet Rev 3.1 (December 2023):
//   * page 1 SOT-23 top view: pin 1 = G (gate, dot end), pin 2 = S (source),
//     pin 3 = D (drain); VDS -30 V, VGS +-12 V, ID -4 A at 25 C (-3.2 A at
//     70 C), IDM -27 A;
//   * page 2: RDS(ON) 60 mOhm typical / 85 mOhm maximum at VGS = -2.5 V and
//     ID = -2.5 A, TJ = 25 C; 47/60 mOhm at VGS = -4.5 V; gate threshold
//     VGS(th) -0.5 V min, -0.9 V typ, -1.3 V max at ID = -250 uA;
//     Ciss 645 pF, Qg 7 nC at VGS = -4.5 V.
// The gate resistor and gate-source pull-up are separate parts on the board,
// not part of this import.
const pinLabels = {
  pin1: "gate",
  pin2: "source",
  pin3: "drain",
} as const

// The element type and the terminal labels are fixed by this part, so callers
// cannot override them; only placement and connections are passed in.
type Props = Omit<TransistorProps<string>, "type" | "pinLabels">

export const C15127_AO3401A = (props: Props) => {
  return (
    <transistor
      type="mosfet"
      pinLabels={pinLabels}
      manufacturerPartNumber="AO3401A"
      supplierPartNumbers={{
        jlcpcb: ["C15127"],
      }}

      /* 3D model: tscircuit model CDN copy of the JLCPCB/EasyEDA model for
         C15127, fetched 2026-11-13 (HTTP 200, 2774137 bytes). Verified against this
         footprint before use: the STEP bounding box is 2.506 x 2.904 x 1.373 mm, which matches the
         pad geometry here (pads 1.14 and 1.81 mm from the origin on 0.95 pitch), z runs 0..1.373 so the body sits ON the board,
         and the axes already agree with the footprint's - hence no
         pcbRotationOffset and no modelOriginPosition fudge. */
      cadModel={{
        objUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C15127.obj",
        stepUrl: "https://modelcdn.tscircuit.com/easyeda_models/assets/C15127.step",
        pcbRotationOffset: 0,
        modelOriginPosition: { x: 0, y: 0, z: 0 },
      }}
      footprint={
        <footprint>
          <smtpad portHints={["pin1"]} pcbX="-1.1375mm" pcbY="0.95mm" width="1.325mm" height="0.6mm" shape="rect" />
          <smtpad portHints={["pin2"]} pcbX="-1.1375mm" pcbY="-0.95mm" width="1.325mm" height="0.6mm" shape="rect" />
          <smtpad portHints={["pin3"]} pcbX="1.1375mm" pcbY="0mm" width="1.325mm" height="0.6mm" shape="rect" />
          <silkscreenpath route={[{ x: -0.7, y: 1.45 }, { x: 0.7, y: 1.45 }, { x: 0.7, y: -1.45 }, { x: -0.7, y: -1.45 }, { x: -0.7, y: 1.45 }]} />
          {/* No pin-1 dot here: the gate index mark is board silkscreen
              (lib/silk.tsx PIN1) so both boards verify it against the built
              pin-1 pad in one place. The old dot at (-1.9, -1.2) sat beside
              pin 2, not pin 1. */}
          <courtyardrect pcbX="0mm" pcbY="0mm" width="4.1mm" height="3.0mm" />
        </footprint>
      }
      {...props}
    />
  )
}

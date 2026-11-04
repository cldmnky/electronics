import type { ChipProps } from "@tscircuit/props"

// JCET / Jiangsu Changjiang 2N7002, SOT-23 N-channel enhancement MOSFET,
// LCSC/JLCPCB C8545. Primary source: "SOT-23 Plastic-Encapsulate MOSFETS
// 2N7002", J,Sep,2016, page 1 pin list (1. GATE, 2. SOURCE, 3. DRAIN) and
// page 4 SOT-23 package outline (e = 0.95 TYP, e1 = 1.800-2.000, b = 0.300-
// 0.500, D = 2.800-3.000). Land pattern is the KiCad SOT-23 geometry already
// used by this project (pads 1.325 x 0.600 mm, rows 2.275 mm apart, lead
// pair 1.900 mm apart), re-numbered to the datasheet.
//
// The generic tscircuit <mosfet> element binds pin 1 to drain and pin 3 to
// gate (mosfetPins in @tscircuit/props), which is the opposite of this
// package. This exact part therefore declares the numbering explicitly, so a
// pad labelled pin1 is the gate copper.
const pinLabels = {
  pin1: ["gate"],
  pin2: ["source"],
  pin3: ["drain"],
} as const

export const C8545_2N7002 = (props: ChipProps<typeof pinLabels>) => {
  return (
    <chip
      pinLabels={pinLabels}
      manufacturerPartNumber="2N7002"
      supplierPartNumbers={{
        jlcpcb: ["C8545"],
      }}
      footprint={
        <footprint>
          <smtpad portHints={["pin1"]} pcbX="-1.1375mm" pcbY="0.95mm" width="1.325mm" height="0.6mm" shape="rect" />
          <smtpad portHints={["pin2"]} pcbX="-1.1375mm" pcbY="-0.95mm" width="1.325mm" height="0.6mm" shape="rect" />
          <smtpad portHints={["pin3"]} pcbX="1.1375mm" pcbY="0mm" width="1.325mm" height="0.6mm" shape="rect" />
          <silkscreenpath route={[{ x: -0.7, y: 1.45 }, { x: 0.7, y: 1.45 }, { x: 0.7, y: -1.45 }, { x: -0.7, y: -1.45 }, { x: -0.7, y: 1.45 }]} />
          {/* Pin 1 (gate) index mark, emitted for orientation checks. */}
          <silkscreencircle pcbX="-1.9mm" pcbY="-1.2mm" radius="0.25mm" />
          <courtyardrect pcbX="0mm" pcbY="0mm" width="4.1mm" height="3.0mm" />
        </footprint>
      }
      {...props}
    />
  )
}

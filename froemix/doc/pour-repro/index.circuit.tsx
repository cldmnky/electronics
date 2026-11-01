// Minimal repro: a copper pour dies when the poured net has a
// `circular_hole_with_rect_pad` plated hole. See README.md in this folder.
//
// Run:  ln -s ../../tscircuit/node_modules node_modules && bunx tsci build
// Crash: TypeError: undefined is not an object (evaluating 'node.item.key.low')
// Fixed: add holeOffsetX="0mm" holeOffsetY="0mm" to the rect-pad platedhole.
export default () => (
  <board width="30mm" height="20mm" layers={2} minBoardEdgeClearance="0.4mm">
    <net name="GND" isGroundNet />
    <net name="VCC" isPowerNet />
    <chip
      name="P1"
      pinCount={2}
      pcbX="-6mm"
      pcbY="0mm"
      footprint={
        <footprint>
          <platedhole shape="circular_hole_with_rect_pad" holeDiameter="0.9mm" rectPadWidth="1.52mm" rectPadHeight="1.4mm" portHints={["pin1"]} />
          <platedhole shape="circle" holeDiameter="0.9mm" outerDiameter="1.4mm" portHints={["pin2"]} pcbX="4mm" />
        </footprint>
      }
      connections={{ pin1: "net.GND", pin2: "net.VCC" }}
    />
    <resistor name="R1" resistance="10k" footprint="0603" pcbX="6mm" pcbY="0mm" layer="bottom" connections={{ pin1: "net.GND", pin2: "net.VCC" }} />
    <copperpour connectsTo="net.GND" layer="bottom" clearance="0.3mm" boardEdgeMargin="0.5mm" />
  </board>
)

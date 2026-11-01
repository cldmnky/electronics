// Fabrication aids added directly in tscircuit (not part of the KiCad port):
// four bottom-side assembly fiducials and eight bottom-side test points.
//
// Fiducials: 1mm copper pad with a 2mm soldermask opening, in the four corner
// keepouts beside the M3 holes (>=3.6mm from each hole centre, so the <=5mm-OD
// standoff hardware never touches them, and inside the board edge clearance).
// They are bare copper, so they are marked doNotPlace and stay out of the BOM.
//
// Test points: 1.2mm SMD pads on the BOTTOM (the assembled side), one per rail
// and one per debug signal, placed next to an existing pad of the same net so
// the ordinary router can tie them in without long stubs. TPs are bare copper,
// not placed parts.

const FIDUCIALS = [
  { name: "FID1", x: -33.0, y: 63.0 },
  { name: "FID2", x: 33.0, y: 63.0 },
  { name: "FID3", x: -33.0, y: -63.0 },
  { name: "FID4", x: 33.0, y: -63.0 },
] as const

const TEST_POINTS = [
  { name: "TP1", net: "P5V", x: 14.5, y: -42.5, near: "C27/P5V" },
  { name: "TP2", net: "P3V3_D", x: 2.5, y: 55.5, near: "R51/R57 pull-ups" },
  { name: "TP3", net: "P3V3_A", x: -8.0, y: 30.2, near: "U1.VDD" },
  { name: "TP4", net: "VCC", x: 20.6, y: 12.0, near: "U11.VPLUS/C14" },
  { name: "TP5", net: "VEE", x: 19.8, y: 17.2, near: "U11.VMINUS/C15" },
  { name: "TP6", net: "N10V_REF", x: 0.0, y: -1.0, near: "U14.ANODE" },
  { name: "TP7", net: "SYNC_IN", x: 0.0, y: 21.5, near: "R27.pin2" },
  { name: "TP8", net: "MULTIPLEX_ADC_1", x: -12.75, y: 19.5, near: "U1.X" },
] as const

export const FabricationSection = () => (
  <>
    {FIDUCIALS.map((fid) => (
      // NOTE: <fiducial> builds its pad with is_covered_with_solder_mask=true in
      // this tscircuit version, i.e. a mask-covered copper dot that an assembly
      // camera cannot see. A one-pad <footprint> with an explicit mask opening
      // is the same geometry and actually exposed.
      <chip
        key={fid.name}
        name={fid.name}
        doNotPlace={true}
        layer="bottom"
        pcbX={`${fid.x}mm`}
        pcbY={`${fid.y}mm`}
        footprint={
          <footprint>
            <smtpad
              portHints={["pin1"]}
              shape="circle"
              radius="0.5mm"
              layer="bottom"
              coveredWithSolderMask={false}
              solderMaskMargin="0.5mm"
              noSolderPaste
            />
          </footprint>
        }
      />
    ))}
    {TEST_POINTS.map((tp) => (
      <testpoint
        key={tp.name}
        name={tp.name}
        footprintVariant="pad"
        padShape="circle"
        padDiameter="1.2mm"
        layer="bottom"
        doNotPlace={true}
        pcbX={`${tp.x}mm`}
        pcbY={`${tp.y}mm`}
        connections={{ pin1: `net.${tp.net}` }}
      />
    ))}
  </>
)

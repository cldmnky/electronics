// Froemix euro board (front board) - the three mating sockets.
//
// These sit on the BOTTOM, facing the base board's top-side 2.00mm pin headers.
// Coordinates are the base board's frozen connector positions (froemix/AGENTS.md
// §3) and the pad order is pre-mirrored in the footprint, so pin 1 of a socket
// lands on pin 1 of its header. See lib/footprints.tsx.
//
// CN3 (micro SD) is deliberately not mated - doc/EURO-BOARD.md §6.

import { CN1_NETS, CN2_NETS, CN4_NETS, MATE } from "../lib/layout"
import { FP_SOCKET_2P00_16, FP_SOCKET_2P00_7 } from "../lib/footprints"

/** { pin1: "net.X", pin2: ... } from a pin -> net table; null = leave open. */
const wire = (nets: readonly (string | null)[]) => {
  const out: Record<string, string> = {}
  nets.forEach((net, i) => {
    if (net) out[`pin${i + 1}`] = `net.${net}`
  })
  return out
}

export const MatingSockets = () => (
  <>
    {/* CN1 - MIDI, muxed pot inputs, LED, rails */}
    <connector
      name="CN1"
      footprint={FP_SOCKET_2P00_16}
      pinCount={MATE.CN1.pins}
      mpn="HC-PM200-4.3H-1x16PZ"
      supplierPartNumbers={{ jlcpcb: ["C22373851"] }}
      layer="bottom"
      pcbX={`${MATE.CN1.x}mm`}
      pcbY={`${MATE.CN1.y}mm`}
      schPinArrangement={{
        leftSide: [1, 3, 5, 7, 9, 11, 13, 15],
        rightSide: [2, 4, 6, 8, 10, 12, 14, 16],
      }}
      connections={wire(CN1_NETS)}
    />

    {/* CN2 - the jack bus. pins 11-15 are unconnected on the base board too
        (encoder reserve), so they stay open here. */}
    <connector
      name="CN2"
      footprint={FP_SOCKET_2P00_16}
      pinCount={MATE.CN2.pins}
      mpn="HC-PM200-4.3H-1x16PZ"
      supplierPartNumbers={{ jlcpcb: ["C22373851"] }}
      layer="bottom"
      pcbX={`${MATE.CN2.x}mm`}
      pcbY={`${MATE.CN2.y}mm`}
      schPinArrangement={{
        leftSide: [1, 3, 5, 7, 9, 11, 13, 15],
        rightSide: [2, 4, 6, 8, 10, 12, 14, 16],
      }}
      connections={wire(CN2_NETS)}
    />

    {/* CN4 - display, wired straight to the OLED header row */}
    <connector
      name="CN4"
      footprint={FP_SOCKET_2P00_7}
      pinCount={MATE.CN4.pins}
      mpn="HX PM2.0-1x7P ZC"
      supplierPartNumbers={{ jlcpcb: ["C22438139"] }}
      layer="bottom"
      pcbX={`${MATE.CN4.x}mm`}
      pcbY={`${MATE.CN4.y}mm`}
      schPinArrangement={{ leftSide: [1, 3, 5, 7], rightSide: [2, 4, 6] }}
      connections={wire(CN4_NETS)}
    />
  </>
)

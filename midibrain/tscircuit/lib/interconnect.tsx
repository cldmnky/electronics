// 2 x 13 direct front/rear interconnect: ONE source for the mating pair, the
// net table and the manufacturer dimensions.
//
// The front board carries the FEMALE straight socket on its bottom face; the
// rear board carries the MALE straight box header on its top face, so the male
// sits on the side opposite the Raspberry Pi. Both boards use the SAME pad
// coordinate source here, and lib/interconnect.test.ts proves on the BUILT
// circuit JSON that every one of the 26 pads coincides in board X/Y and
// carries the same net. Physical fit, keying and continuity are NOT proven:
// acceptance item `interconnect_mate` stays pending.
//
// MANUFACTURER SOURCES (quoted values only; the PDFs are not committed):
//   * Sullins drawing 11091, revision J, "BOX HEADER, .100[2.54] CC/RS, ST,
//     SBH11-_BPC-D__-ST-BK", https://drawings-pdf.s3.amazonaws.com/11091.pdf,
//     retrieved 2026-11-10, SHA-256
//     f35a7148d89c27bea77b50ed43b2399b28ff5d88fbd4205b5d2130f878557c20.
//     Sheet 1: recommended PCB layout .039[1.00] drill on a .100[2.54] grid,
//     .025[0.64] square pin, .118[3.00] tail, body height .346[8.80] /
//     overall .358[9.10], key slot .177[4.50], and for 13 positions per row
//     A = 30.48 mm (contact span), B = 40.52 mm and C = 38.32 mm (insulator).
//     Sheet 2: the straight SFH11 socket mates this header, and the dimension
//     across the two opposed PCB seating faces of the mated pair is
//     .441[11.20] (title-block tolerance .XXX +-.008 [0.20]).
//   * Sullins 2019 catalog pages 122/123 (local /tmp/sullins-2019-catalog.pdf,
//     source https://www.sullinscorp.com/wp-content/uploads/2018/10/2019-CATALOG.pdf,
//     SHA-256 44e4b4485b961a87cc23a190224b282764adff2a05daffd38d4bab94a84ebf77):
//     the SBH11 series lists a 13-position variant and a "Centered Polarizing
//     Notch"; the SFH11 series lists 13 positions and a centred polarizing key
//     (removed by the -M181 modification); each series mates the other. The
//     SFH11 straight view dimensions its 0.40 x 0.60 mm tail and 3.20 mm tail
//     length; NO manufacturer-recommended female PCB hole is published on
//     these pages.
//
// CONSEQUENCES THAT ARE ASSUMPTIONS, NOT APPROVALS:
//   * The male's 1.00 mm drill is the manufacturer's recommendation. The SAME
//     1.00 mm drill is a PROVISIONAL design assumption for the female, whose
//     recommended hole is not published; its quoted 0.40 x 0.60 mm blade
//     fits, but that is our arithmetic, not a manufacturer statement.
//   * The drawing pages read do not print the pin numbering. The boards are
//     built so that the same PIN NUMBER of both connectors lands on the same
//     physical pad, which is what a keyed mating pair must do; the purchased
//     pair's numbering is confirmed only by the pre-power all-26 continuity
//     check in `interconnect_mate`.
//   * DigiKey
//     https://www.digikey.se/en/products/detail/sullins-connector-solutions/SBH11-PBPC-D13-ST-BK/1990066
//     was observed once in a live browser session on 2026-11-10 (the parent saw
//     the male page list SFH11-PBPC-D13-ST-BK as a mating product). No snapshot
//     is committed, so this is a CHECKED-DATE SOURCE LEAD only: stock is subject
//     to reconfirmation, it is not an approved DigiKey source, and no
//     availability is claimed. Conrad is the owner's target for non-SMT parts
//     and the owner's DigiKey exception covers switches and caps only.
import type { ConnectorProps } from "@tscircuit/props"

/** Contact pitch in both directions, mm (drawings: .100[2.54]). */
export const INTERCONNECT_PITCH = 2.54
/** Pins per row and total contacts. */
export const INTERCONNECT_ROWS = 2
export const INTERCONNECT_PINS_PER_ROW = 13
export const INTERCONNECT_PIN_COUNT = INTERCONNECT_ROWS * INTERCONNECT_PINS_PER_ROW
/** Contact span (drawing A) contains 12 pitches: 30.48 mm. */
export const INTERCONNECT_CONTACT_SPAN = 12 * INTERCONNECT_PITCH

/**
 * Plated hole and pad. Drill 1.00 mm is the male's manufacturer
 * recommendation; for the female it is a PROVISIONAL assumption (no published
 * recommendation). Pad 1.60 mm keeps a 0.30 mm annular ring, above the
 * project's 0.25 mm through-hole target.
 */
export const INTERCONNECT_DRILL_MM = 1.0
export const INTERCONNECT_PAD_MM = 1.6
export const INTERCONNECT_RING_MM = (INTERCONNECT_PAD_MM - INTERCONNECT_DRILL_MM) / 2

/** Quoted manufacturer dimensions, mm. Nothing here is a measured fit. */
export const INTERCONNECT_MECH = {
  /** Sheet 2 of drawing 11091: opposed PCB seating faces of the mated pair. */
  seatToSeat: 11.2,
  /** Title-block tolerance on a .XXX inch dimension: +-0.008 in = +-0.20 mm. */
  seatToSeatTolerance: 0.2,
  /** Male body length for 13 positions per row (drawing B): 40.52 mm. */
  bodyLength: 40.52,
  /** Male insulator length (drawing C): 38.32 mm. */
  insulatorLength: 38.32,
  /**
   * PLANNING ENVELOPE across the two rows. The pages read dimension the male
   * top view .256[6.50] and the female body .276[7.00] without a section view
   * that reconciles them; the female must enter the male shroud, so the male's
   * outer width is not smaller than 7.00 mm. This envelope uses the male's
   * declared .346[8.80] as a CONSERVATIVE upper bound so no clearance is
   * silently waived. It is an envelope, not a measured body width.
   */
  bodyEnvelope: 8.8,
  /** Male height above its PCB (drawing .358[9.10]; body .346[8.80]). */
  maleHeight: 9.1,
  /**
   * Male tail length MEASURED FROM THE CONNECTOR SEATING FACE (drawing
   * .118[3.00]) - NOT the protrusion below the PCB. The nominal protrusion is
   * tail minus the PCB thickness; see maleProtrusion.
   */
  maleTail: 3.0,
  /** Female body height above its PCB (catalog SFH11 straight .335[8.50]). */
  femaleHeight: 8.5,
  /** Female tail length from its seating face (catalog SFH11 straight .126[3.20]). */
  femaleTail: 3.2,
  /** NOMINAL board thickness both tails are measured against (both boards). */
  pcbThickness: 1.6,
  /** Male square pin (drawing .025[0.64]). */
  pinSquare: 0.64,
  /** Male centred key slot (drawing .177[4.50]). */
  keySlot: 4.5,
  /** Female tail blade, per catalog page 123: 0.40 x 0.60 mm. */
  femaleTailThickness: 0.4,
} as const

/**
 * NOMINAL protrusion of each tail beyond a 1.60 mm PCB: the drawing dimensions
 * are from the seating face, so this is arithmetic, not a measured protrusion.
 * The solder fillet, the actual PCB thickness tolerance, the Pi mounting
 * hardware and every clearance stay unmeasured.
 */
export const INTERCONNECT_PROTRUSION = {
  male: Math.round((INTERCONNECT_MECH.maleTail - INTERCONNECT_MECH.pcbThickness) * 1000) / 1000,   // 1.40
  female: Math.round((INTERCONNECT_MECH.femaleTail - INTERCONNECT_MECH.pcbThickness) * 1000) / 1000, // 1.60
  basis: "tail length from the seating face minus the 1.60 mm nominal PCB",
  unmeasured: ["solder fillet height", "PCB thickness tolerance", "Pi spacer and hardware", "every clearance"],
} as const

export const INTERCONNECT_PARTS = {
  male: {
    mpn: "SBH11-PBPC-D13-ST-BK",
    description: "Sullins .100 in shrouded box header, dual row, 13 per row, straight, keyed",
    drawingUrl: "https://drawings-pdf.s3.amazonaws.com/11091.pdf",
    drawingSha256: "f35a7148d89c27bea77b50ed43b2399b28ff5d88fbd4205b5d2130f878557c20",
    drawingRevision: "J",
    retrieved: "2026-11-10",
    drillSource: "manufacturer recommended PCB layout, drawing 11091 sheet 1",
  },
  female: {
    mpn: "SFH11-PBPC-D13-ST-BK",
    description: "Sullins .100 in female header, dual row, 13 per row, straight, centred polarizing key",
    drawingUrl: "https://drawings-pdf.s3.amazonaws.com/11091.pdf",
    drawingSha256: "f35a7148d89c27bea77b50ed43b2399b28ff5d88fbd4205b5d2130f878557c20",
    drawingRevision: "J (mating pair, sheet 2)",
    retrieved: "2026-11-10",
    drillSource: "PROVISIONAL design assumption: no female PCB hole is published on the drawing pages read",
  },
} as const

/**
 * Footprint pad table, expressed in the FEMALE footprint's own frame (before
 * the board's layer transform). The male table is this one with X negated:
 * the male sits on the opposite board face, where tscircuit does NOT apply the
 * bottom-layer mirror, so negating X makes pin n of both parts land on the
 * same physical point. Do not "simplify" that away - the coincidence test in
 * lib/interconnect.test.ts fails when it is missing.
 */
export const INTERCONNECT_PAD_TABLE: readonly { pin: number; x: number; y: number }[] =
  Array.from({ length: INTERCONNECT_PINS_PER_ROW }, (_, k) => [
    { pin: 2 * k + 1, x: -INTERCONNECT_CONTACT_SPAN / 2 + INTERCONNECT_PITCH * k, y: -INTERCONNECT_PITCH / 2 },
    { pin: 2 * k + 2, x: -INTERCONNECT_CONTACT_SPAN / 2 + INTERCONNECT_PITCH * k, y: INTERCONNECT_PITCH / 2 },
  ]).flat()

/**
 * Net carried by each physical contact. ONE meaning per pin on both boards:
 * the same number is the same net, which is what makes the direct mate legal.
 * Superseded wording: the 2 x 13 link is no longer a ribbon cable.
 */
export const INTERCONNECT_NETS: Readonly<Record<string, string>> = {
  pin1: "net.P3V3", pin2: "net.GND", pin3: "net.MIDI_IN_1_RX", pin4: "net.MIDI_OUT_1_TX",
  pin5: "net.MIDI_IN_2_RX", pin6: "net.MIDI_OUT_2_TX", pin7: "net.NAV_UP", pin8: "net.NAV_DOWN",
  pin9: "net.NAV_LEFT", pin10: "net.NAV_RIGHT", pin11: "net.NAV_PRESS", pin12: "net.ENC_A",
  pin13: "net.ENC_B", pin14: "net.ENC_SW", pin15: "net.REC", pin16: "net.OVER_DUB",
  pin17: "net.STOP", pin18: "net.PLAY", pin19: "net.PLAY_START", pin20: "net.REC_LED",
  pin21: "net.OVERDUB_LED", pin22: "net.P3V3", pin23: "net.PLAY_LED", pin24: "net.GND",
  pin25: "net.GND", pin26: "net.SHIFT_EXIT",
} as const

export type InterconnectSide = "male" | "female"

/**
 * Shared footprint body for both connectors. `side` selects the pad table
 * mirror and the key/notch drawing; everything else (holes, pad, pitch) is the
 * same part geometry, so the two cannot drift apart.
 */
export const InterconnectFootprint = ({ side }: { side: InterconnectSide }) => {
  const male = side === "male"
  // The key/notch mark uses the SAME local Y on both footprints. tscircuit maps
  // local +Y to global -X on BOTH layers (bottom: (x,y) -> (-y, -x); top:
  // (x,y) -> (-y, +x)), so identical local signs land on the same physical
  // long side and the built marks coincide. Inverting the sign on one side put
  // them on opposite sides, which the built-geometry test now catches.
  // WHICH physical side the real key sits on is not verifiable from the pages
  // read: the key position relative to pin 1 stays a physical check.
  const keyY = INTERCONNECT_MECH.bodyEnvelope / 2
  const pads = INTERCONNECT_PAD_TABLE.map(({ pin, x, y }) => (
    <platedhole
      portHints={[`pin${pin}`]}
      pcbX={`${(male ? -x : x).toFixed(4)}mm`}
      pcbY={`${y.toFixed(4)}mm`}
      shape="circle"
      outerDiameter={`${INTERCONNECT_PAD_MM}mm`}
      holeDiameter={`${INTERCONNECT_DRILL_MM}mm`}
    />
  ))
  const halfLength = INTERCONNECT_MECH.bodyLength / 2
  const halfWidth = INTERCONNECT_MECH.bodyEnvelope / 2
  // Silk outline of the body, drawn as a closed path (the same primitive the
  // imported XFCN footprint used, so no new element kind is introduced).
  const bodyOutline = [
    { x: -halfLength, y: -halfWidth }, { x: halfLength, y: -halfWidth },
    { x: halfLength, y: halfWidth }, { x: -halfLength, y: halfWidth },
    { x: -halfLength, y: -halfWidth },
  ]
  const keyHalf = INTERCONNECT_MECH.keySlot / 2
  const keyPath = [
    { x: -keyHalf, y: keyY }, { x: keyHalf, y: keyY },
  ]
  return (
    <footprint insertionDirection="from_above">
      {pads}
      <silkscreenpath route={bodyOutline} strokeWidth="0.15mm" />
      {/* Centred polarizing key / notch, on the same physical long side. */}
      <silkscreenpath route={keyPath} strokeWidth="0.5mm" />
      {/* Pin-1 marker: a short bar just outboard of the body, at pin 1's end
          and on pin 1's row. Pin 1 is at local x = -halfLength for the female
          and +halfLength for the male (the pad table is mirrored in X), and in
          the local -Y row on both, because the local X sign differs while the
          row does not. The old signs put the bar at the pin-26 end on the wrong
          row; lib/interconnect.test.ts now measures the BUILT bar against the
          BUILT pin 1 and pin 26 pads. */}
      <silkscreenpath
        route={[
          { x: (male ? 1 : -1) * (halfLength + 0.6), y: -(halfWidth + 0.9) },
          { x: (male ? 1 : -1) * (halfLength + 0.6), y: -(halfWidth + 0.2) },
        ]}
      />
      <fabricationnotepath
        route={[...bodyOutline, { x: -halfLength, y: -halfWidth }]}
        strokeWidth="0mm"
        isFilled
        hasStroke={false}
      />
      <silkscreentext text="{NAME}" pcbX="0mm" pcbY="5.6mm" anchorAlignment="center" fontSize="0.8mm" />
      <courtyardoutline
        outline={[
          { x: -halfLength - 0.25, y: -halfWidth - 0.25 },
          { x: halfLength + 0.25, y: -halfWidth - 0.25 },
          { x: halfLength + 0.25, y: halfWidth + 0.25 },
          { x: -halfLength - 0.25, y: halfWidth + 0.25 },
        ]}
      />
    </footprint>
  )
}

const connectorProps = (side: InterconnectSide, props: ConnectorProps) => ({
  pinCount: INTERCONNECT_PIN_COUNT,
  pinLabels: Object.fromEntries(
    Array.from({ length: INTERCONNECT_PIN_COUNT }, (_, i) => [`pin${i + 1}`, [`pin${i + 1}`]]),
  ),
  manufacturerPartNumber: INTERCONNECT_PARTS[side].mpn,
  ...props,
})

/** Female straight socket, SFH11-PBPC-D13-ST-BK (front board, bottom face). */
export const SFH11_PBPC_D13_ST_BK = (props: ConnectorProps) => (
  <connector {...connectorProps("female", props)} footprint={<InterconnectFootprint side="female" />} />
)

/** Male straight box header, SBH11-PBPC-D13-ST-BK (rear board, top face). */
export const SBH11_PBPC_D13_ST_BK = (props: ConnectorProps) => (
  <connector {...connectorProps("male", props)} footprint={<InterconnectFootprint side="male" />} />
)

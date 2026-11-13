// Rear/base board probe points (test points), ONE source for the JSX and the
// checks. Fifteen bare plated through-holes on nets that already exist: no part
// is added to the circuit, no rail is bridged and no measurement is claimed.
// `scripts/check_electrical.py` restates this table and measures the BUILT pads
// (name, net, layers, drill, pad, ring, position); it does not import this file,
// so a change here still has to appear in the built output.
//
// Why a plated through-hole and not `<testpoint footprintVariant="through_hole"
// padDiameter=...>`: in the installed @tscircuit/core the through-hole variant
// ignores `padDiameter` and derives the pad from the drill alone
// (`platedhole_d<hole>`, pad = 1.5 x drill), so a 0.8 mm drill produced a
// 0.2 mm ring here - below the project's 0.25 mm PTH target. The pad is
// therefore authored explicitly with a footprinter string that carries both
// numbers: `platedhole_d0.8mm_pd1.6mm` = 0.80 mm drill, 1.60 mm pad, 0.40 mm
// annular ring. That ring is measured on the built JSON, not assumed.
//
// Bare copper has no supplier part: a probe point is not an assembly part, so
// there is no manufacturer part number and no LCSC C-number. A BOM line with a
// placeholder number would be a fabrication claim, so `check_electrical.py`
// fails if one appears.
//
// NOT a measurement and NOT an approval. Physical probe access with the stack
// assembled is unverified: see docs/ELECTRICAL-VERIFICATION.md and acceptance
// item `probe_point_access`.

/** Drill of every probe point, in mm (also a valid JLCPCB plated drill). */
export const PROBE_HOLE_DIAMETER_MM = 0.8
/** Copper pad diameter of every probe point, in mm. */
export const PROBE_PAD_DIAMETER_MM = 1.6
/** Annular ring that follows: 0.40 mm. */
export const PROBE_RING_MM = (PROBE_PAD_DIAMETER_MM - PROBE_HOLE_DIAMETER_MM) / 2
/** The project's PTH annular-ring target, restated for the checks. */
export const PROBE_MIN_RING_MM = 0.25
/** Board-edge distance every probe pad edge must keep, in mm. */
export const PROBE_EDGE_MM = 2.0
/** Minimum centre-to-centre spacing between probe pads, in mm. */
export const PROBE_MIN_PITCH_MM = 3.0
/** Footprinter string that carries the drill AND the pad, unlike padDiameter. */
export const PROBE_FOOTPRINT = `platedhole_d${PROBE_HOLE_DIAMETER_MM}mm_pd${PROBE_PAD_DIAMETER_MM}mm`

export type ProbeGroup = "logic" | "midi"

export type ProbePoint = {
  /** Reference designator printed next to the pad. */
  readonly ref: string
  /** Net the pad is connected to, without the `net.` prefix. */
  readonly net: string
  readonly group: ProbeGroup
  readonly x: number
  readonly y: number
  /** Short silkscreen label; the full meaning is in the docs table. */
  readonly label: string
  /**
   * What the pad is actually on: the exact net and the side of the series
   * resistor or buffer. Restated in the docs so a probe is never described as
   * the far side of a buffer it is not on.
   */
  readonly signal: string
}

// Positions are board millimetres, origin at the board centre. They sit in the
// empty top-left block (logic: two columns 5 mm apart, 5 rows 3 mm apart) and
// in the open pocket right of the Pico USB plug (MIDI: two rows of 3 mm pitch).
// No two pads are closer than 3 mm anywhere. Neither block is under the Pico
// module, the J_PI body or either ribbon connector; see PROBE_KEEPOUTS.
// The built-pad checker separately checks spacing from mounting holes. Cable
// clearance and actual probe access remain subject to physical qualification. The nearest neighbour of every pad is
// measured by `lib/probe-points.test.ts` and by `check_electrical.py` on the
// built board.
// The MIDI block is two rows: the upper row carries TX1/RX1/TX2, the lower row
// RX2 and the local ground. Both rows use 3 mm pitch in both directions.
const MIDI_ROW_UPPER_Y = -30
const MIDI_ROW_LOWER_Y = -33
/** Distance from a probe pad centre to its label, in mm. */
export const PROBE_LABEL_OFFSET_MM = 2.6

export const PROBE_POINTS: readonly ProbePoint[] = [
  {
    ref: "TP1", net: "P5V", group: "logic", x: -40, y: 52.5, label: "5V",
    signal: "P5V: Pi header pin 2/4 and D3 anode (the Pi 5 V rail, before D3; not Pico VSYS)",
  },
  {
    ref: "TP2", net: "P3V3", group: "logic", x: -35, y: 52.5, label: "3V3",
    signal: "P3V3: Pico module pin 36 (3V3 OUT) and the board's 3.3 V rail; the Pico's SMPS output",
  },
  {
    ref: "TP3", net: "PI_3V3", group: "logic", x: -40, y: 49.5, label: "PI3V3",
    signal: "PI_3V3: Pi header pin 1 only, then R19 (100 ohm) to net OE; NOT the Pico 3V3 rail",
  },
  {
    ref: "TP4", net: "RUN", group: "logic", x: -35, y: 49.5, label: "RUN",
    signal: "RUN: Pico module pin 30 (RUN), Q3 drain and R18 (10 k to P3V3)",
  },
  {
    ref: "TP5", net: "SPI_SCLK_PICO", group: "logic", x: -40, y: 46.5, label: "SCLK",
    signal: "SPI_SCLK_PICO: Pico module pin 24 (GPIO18), Pico side of series resistor R14 (100 ohm)",
  },
  {
    ref: "TP6", net: "SPI_CS_PICO", group: "logic", x: -35, y: 46.5, label: "CS",
    signal: "SPI_CS_PICO: Pico module pin 22 (GPIO17), Pico side of series resistor R15 (100 ohm)",
  },
  {
    ref: "TP7", net: "SPI_MOSI_PICO", group: "logic", x: -40, y: 43.5, label: "MOSI",
    signal: "SPI_MOSI_PICO: Pico module pin 21 (GPIO16), Pico side of series resistor R13 (100 ohm)",
  },
  {
    ref: "TP8", net: "SPI_MISO_PICO", group: "logic", x: -35, y: 43.5, label: "MISO",
    signal: "SPI_MISO_PICO: Pico module pin 25 (GPIO19) and U3 input A (buffer input, Pico side)",
  },
  {
    ref: "TP9", net: "READY_PICO", group: "logic", x: -40, y: 40.5, label: "READY",
    signal: "READY_PICO: Pico module pin 26 (GPIO20) and U4 input A (buffer input, Pico side)",
  },
  {
    ref: "TP10", net: "GND", group: "logic", x: -35, y: 40.5, label: "GND",
    signal: "GND: local return beside the logic probe field",
  },
  {
    ref: "TP11", net: "MIDI_OUT_1_TX", group: "midi", x: -33, y: MIDI_ROW_UPPER_Y, label: "TX1",
    signal: "MIDI_OUT_1_TX: Pico module pin 1 (UART0 TX); the front board takes it to R5 (100 ohm) pin 1, Q1 gate side is R5 pin 2",
  },
  {
    ref: "TP12", net: "MIDI_IN_1_RX", group: "midi", x: -30, y: MIDI_ROW_UPPER_Y, label: "RX1",
    signal: "MIDI_IN_1_RX: Pico module pin 2 (UART0 RX); the front board drives it from U1 pin 5 (TLP2361 VO) with R3 (2.2 k) pull-up",
  },
  {
    ref: "TP13", net: "MIDI_OUT_2_TX", group: "midi", x: -27, y: MIDI_ROW_UPPER_Y, label: "TX2",
    signal: "MIDI_OUT_2_TX: Pico module pin 6 (UART1 TX); the front board takes it to R6 (100 ohm) pin 1, Q2 gate side is R6 pin 2",
  },
  {
    ref: "TP14", net: "MIDI_IN_2_RX", group: "midi", x: -33, y: MIDI_ROW_LOWER_Y, label: "RX2",
    signal: "MIDI_IN_2_RX: Pico module pin 7 (UART1 RX); the front board drives it from U2 pin 5 (TLP2361 VO) with R4 (2.2 k) pull-up",
  },
  {
    ref: "TP15", net: "GND", group: "midi", x: -30, y: MIDI_ROW_LOWER_Y, label: "GND",
    signal: "GND: local return beside the MIDI probe points (TP11-TP14)",
  },
] as const

export type Box = { readonly x1: number; readonly y1: number; readonly x2: number; readonly y2: number }

/**
 * Planning envelopes a probe PAD must stay out of so a probe tip can reach it
 * with the stack assembled. The pad edge (not only its centre) is tested by
 * `lib/probe-points.test.ts`. Every box is provisional: it is a documented
 * clearance argument, not a measurement.
 */
export const PROBE_KEEPOUTS: Readonly<Record<string, Box & { readonly note: string }>> = {
  pico_module: {
    x1: -50.2, y1: -25.5, x2: -28.8, y2: 25.5,
    note: "Pico 2 module 51 x 21 mm over the 2 x 20 x 2.54 mm sockets (Pico 2 datasheet 3, Figure 3); module body only",
  },
  pico_usb_plug: {
    x1: -43.5, y1: -45, x2: -35.5, y2: -25.5,
    note: "micro-USB plug envelope in front of the Pico's USB end (module pin 1 end, y = -25.5); the cable is used for Pico programming and the USB case of acceptance item scope_spi",
  },
  j_pi_body: {
    x1: -26, y1: 45, x2: 26, y2: 53,
    note: "J_PI 2 x 20 header body and its 40-pin GPIO ribbon",
  },
  j_front_body: {
    // Male SBH11 body envelope 40.52 x 8.80 mm at the direct interconnect's
    // shared placement (10, 5) rotated 90 deg (lib/layout.ts).
    x1: 5.6, y1: -15.3, x2: 14.4, y2: 25.3,
    note: "J_FRONT 2 x 13 male header body of the direct mate (no ribbon)",
  },
} as const

/** Font size of every probe label, in mm (readable after assembly). */
export const PROBE_LABEL_FONT_MM = 0.7

/**
 * Where a probe label prints, relative to its pad. The logic field labels sit
 * 2.6 mm to the right of the pad; the MIDI upper-row labels sit above the row
 * and the lower-row labels below it, so no label ever lands between two pads.
 * `lib/silk.test.ts` and `lib/probe-points.test.ts` measure the resulting
 * clearance against every pad, hole and other mark; this module is the single
 * source for the placement.
 */
export const probeLabelPosition = (point: ProbePoint): { x: number; y: number } => {
  if (point.group === "logic") return { x: point.x + PROBE_LABEL_OFFSET_MM, y: point.y }
  return point.y === MIDI_ROW_UPPER_Y
    ? { x: point.x, y: point.y + PROBE_LABEL_OFFSET_MM }
    : { x: point.x, y: point.y - PROBE_LABEL_OFFSET_MM }
}

/** Probe pads in board order; the JSX emits exactly this set. */
export const PROBE_POINT_REFS: readonly string[] = PROBE_POINTS.map(p => p.ref)

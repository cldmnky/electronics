// MIDI current-loop analysis and the datasheet values it is checked against.
// Everything here is a calculation from published limits plus stated
// tolerances. Nothing here is a measurement, and no SPICE run is a hardware
// qualification.
//
// Primary sources (files and hashes recorded in docs/ELECTRICAL-VERIFICATION.md):
//   * MIDI 1.0 Electrical Specification Update (CA-033), 2014: 31.25 kbaud
//     8N1, opto-isolated input, receiver must accept a 5 mA sender, 3.3 V and
//     5 V output cases.
//   * MIDI TRS adapters, Type A: tip = DIN 5, ring = DIN 4, sleeve = DIN 2.
//   * Toshiba TLP2361 datasheet Rev.5.0 (2015-11-02).
//   * Toshiba TLP2362 datasheet Rev.6.0 (2016-01-19): the part this replaces.
//
// The receiver load is this design's own 220 ohm on the ring (DIN 4) side plus
// the external sender's two 220 ohm (5 V) resistors. The loop current comes
// from the sender supply, all three resistors and the opto LED forward drop.

export type OptoSpec = {
  readonly mpn: string
  readonly lcsc: string
  /** Threshold input current, guaranteed maximum, in A. */
  readonly ifhlMaxA105: number
  readonly ifhlMaxA125: number
  /** Recommended on-state input current range, in A. */
  readonly ifOnMinA: number
  readonly ifOnMaxA: number
  /** Absolute maximum continuous input current, in A. */
  readonly ifAbsMaxA: number
  /** Input forward current at which the VF limits below are specified, in A. */
  readonly vfRatedCurrentA: number
  /** Input forward voltage limits at vfRatedCurrentA, in V. */
  readonly vfMin: number
  readonly vfTyp: number
  readonly vfMax: number
  /** Input forward voltage temperature coefficient, in V/C (typical). */
  readonly vfTempCoeffPerC: number
  readonly supplyMinV: number
  readonly supplyMaxV: number
  /** Maximum package height, in mm. */
  readonly heightMaxMm: number
  readonly package: string
  readonly outputType: "open collector" | "totem pole"
  /** Guaranteed output levels at VCC = 3.3 V. */
  readonly vohMinAt20uA: number
  readonly volMaxAt3m2mA: number
  readonly propagationMaxNs: number
  readonly source: string
}

export const TLP2361: OptoSpec = {
  mpn: "TLP2361(TPL,E",
  lcsc: "C107626",
  ifhlMaxA105: 1.3e-3,
  ifhlMaxA125: 1.6e-3,
  ifOnMinA: 2e-3,
  ifOnMaxA: 6e-3,
  ifAbsMaxA: 10e-3,
  vfRatedCurrentA: 2e-3,
  vfMin: 1.35,
  vfTyp: 1.5,
  vfMax: 1.65,
  vfTempCoeffPerC: -2.0e-3,
  supplyMinV: 2.7,
  supplyMaxV: 5.5,
  heightMaxMm: 2.2,
  package: "Toshiba 11-4L1S (SO6)",
  outputType: "totem pole",
  vohMinAt20uA: 3.2,
  volMaxAt3m2mA: 0.4,
  propagationMaxNs: 80,
  source: "Toshiba TLP2361 datasheet Rev.5.0 (2015-11-02), pages 2-5 and 15",
}

export const TLP2362: OptoSpec = {
  mpn: "TLP2362(TPL,E",
  lcsc: "C37658",
  ifhlMaxA105: 5.0e-3,
  ifhlMaxA125: 5.0e-3,
  // Recommended on-state input current 7.5-14 mA (page 3): the ~4.4 mA this
  // loop can deliver is outside the recommended range, not just below the
  // threshold.
  ifOnMinA: 7.5e-3,
  ifOnMaxA: 14e-3,
  ifAbsMaxA: 25e-3,
  vfRatedCurrentA: 10e-3,
  vfMin: 1.45,
  vfTyp: 1.55,
  vfMax: 1.7,
  vfTempCoeffPerC: -2.0e-3,
  supplyMinV: 2.7,
  supplyMaxV: 5.5,
  // Same Toshiba 11-4L1S package as the TLP2361; the 2362 package drawing was
  // not re-read in this pass, so the height is taken from the TLP2361 drawing.
  heightMaxMm: 2.2,
  package: "Toshiba 11-4L1S (SO6)",
  outputType: "open collector",
  vohMinAt20uA: 0,
  volMaxAt3m2mA: 0.6,
  propagationMaxNs: 100,
  source: "Toshiba TLP2362 datasheet Rev.6.0 (2016-01-19), pages 2-5",
}

// TRS Type A contact assignment. The input receiver draws current from the
// ring (DIN 4, the sender's positive side) and returns it on the tip
// (DIN 5, the sender's zero side). The output sources current on the ring and
// sinks it on the tip. Do not swap these.
export const MIDI_TYPE_A = {
  tip: 5,
  ring: 4,
  sleeve: 2,
} as const

// This design's receiver resistor (front board R1/R2) and the output resistors.
export const MIDI_RECEIVER_OHM = 220
export const MIDI_OUT_SERIES_OHM = { source: 22, sink: 33 } as const

export type SenderCorner = {
  readonly name: string
  /** Open-circuit sender supply, before tolerance. */
  readonly supplyV: number
  readonly supplyTolerance: number
  /** Each of the two sender series resistors, in ohm. */
  readonly seriesOhm: number
  readonly resistorTolerance: number
  readonly source: string
}

// MIDI 1.0 5 V sender: 5 V nominal with two 220 ohm resistors.
export const SENDER_5V: SenderCorner = {
  name: "MIDI 1.0 5 V sender",
  supplyV: 5.0,
  supplyTolerance: 0.05,
  seriesOhm: 220,
  resistorTolerance: 0.01,
  source: "MIDI 1.0 CA-033 output circuit, 2 x 220 ohm",
}

// MIDI CA-033 3.3 V sender: 3.3 V with 33 ohm series resistors.
export const SENDER_3V3: SenderCorner = {
  name: "MIDI CA-033 3.3 V sender",
  supplyV: 3.3,
  supplyTolerance: 0.05,
  seriesOhm: 33,
  resistorTolerance: 0.01,
  source: "MIDI 1.0 CA-033 3.3 V output circuit, 2 x 33 ohm",
}

// Forward-voltage bounds used by the calculation.
//   * Min-current corner: the datasheet VF maximum (1.65 V) is specified at
//     IF = 2 mA only. At the ~4 mA this loop actually runs at, VF is higher,
//     so the calculation uses VF_ASSUMED_HIGH (1.85 V) and treats it as an
//     assumption to confirm, not as a guaranteed limit.
//   * Max-current corner: the datasheet VF minimum (1.35 V at 2 mA) is used,
//     which is conservative because VF rises with current.
export const VF_ASSUMED_HIGH_V = 1.85

export type LoopResult = {
  /** Minimum possible steady-state LED current, in A. */
  readonly minA: number
  /** Maximum possible steady-state LED current, in A. */
  readonly maxA: number
}

/** Worst-case current-loop bounds for one sender corner and one opto LED. */
export const loopCurrent = (
  sender: SenderCorner,
  opto: OptoSpec,
  vfMinCurrentCorner: number = VF_ASSUMED_HIGH_V,
): LoopResult => {
  const vLow = sender.supplyV * (1 - sender.supplyTolerance)
  const vHigh = sender.supplyV * (1 + sender.supplyTolerance)
  const rLow = sender.seriesOhm * (1 - sender.resistorTolerance) * 2 +
    MIDI_RECEIVER_OHM * (1 - sender.resistorTolerance)
  const rHigh = sender.seriesOhm * (1 + sender.resistorTolerance) * 2 +
    MIDI_RECEIVER_OHM * (1 + sender.resistorTolerance)
  return {
    minA: (vLow - vfMinCurrentCorner) / rHigh,
    maxA: (vHigh - opto.vfMin) / rLow,
  }
}

/**
 * Guaranteed turn-on margin: how many times the minimum loop current exceeds
 * the datasheet's guaranteed maximum threshold. Below 1 the receiver is not
 * guaranteed to see a low input at all.
 */
export const thresholdMargin = (minLoopA: number, ifhlMaxA: number): number =>
  minLoopA / ifhlMaxA

/** Worst-case input current as a fraction of the absolute maximum rating. */
export const absoluteCurrentUse = (maxLoopA: number, opto: OptoSpec): number =>
  maxLoopA / opto.ifAbsMaxA

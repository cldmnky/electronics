// MIDI current-loop analysis and the datasheet values it is checked against.
// Everything here is a calculation from published limits plus stated
// tolerances. Nothing here is a measurement, and no SPICE run is a hardware
// qualification.
//
// Primary sources (files and hashes recorded in docs/ELECTRICAL-VERIFICATION.md):
//   * MIDI 1.0 Electrical Specification Update (CA-033), 2014, pages 2-4:
//     31.25 kbaud 8N1, 5 mA loop, receiver must turn on below 5 mA,
//     transmitter resistors VTX +5 V +-10% with RA 220 ohm 5% / RC 220 ohm 5%,
//     VTX +3.3 V +-5% with RA 33 ohm 5% / RC 10 ohm 5%, and the legacy
//     receiver model (LED VF up to 1.9 V plus 220 ohm; PC900V needs 4 mA worst
//     case at RD = 280 ohm, VRX = 5 V).
//   * MIDI TRS adapters, Type A: tip = DIN 5, ring = DIN 4, sleeve = DIN 2.
//   * Toshiba TLP2361 datasheet Rev.5.0 (2015-11-02).
//   * Toshiba TLP2362 datasheet Rev.6.0 (2016-01-19): the part this replaces.
//
// The receiver load is this design's own 220 ohm on the ring (DIN 4) side plus
// the external sender's two series resistors (RA on the DIN 4 side, RC on the
// DIN 5 return side). The loop current comes from the sender supply, the
// sender driver saturation drop, all three resistors and the opto LED forward
// drop.

export type OptoSpec = {
  readonly mpn: string
  readonly lcsc: string
  /** Threshold input current, guaranteed maximum, in A. */
  readonly ifhlMaxA105: number
  readonly ifhlMaxA125: number
  /**
   * Threshold used for design checks: the full-temperature (125 C) guaranteed
   * maximum, which is the binding number for a product that may run hot.
   */
  readonly ifhlGuaranteedA: number
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
  ifhlGuaranteedA: 1.6e-3,
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
  ifhlGuaranteedA: 5.0e-3,
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

// This design's receiver resistor (front board R1/R2, a 1 % part) and the
// output resistors.
export const MIDI_RECEIVER_OHM = 220
export const MIDI_RECEIVER_TOLERANCE = 0.01
// Output resistors. CA-033 asks for RA 33 ohm on the ring and RC 10 ohm on
// the tip for a 3.3 V transmitter; this design deliberately uses 33 ohm on the
// ring and 22 ohm on the tip. See OUTPUT_STAGE_DEVIATION for the reason.
export const MIDI_OUT_SERIES_OHM = { ring: 33, tip: 22 } as const
// The 2512 output resistors are 25121WF330JT4E (33 ohm) and 25121WF220JT4E
// (22 ohm). Per the Uniroyal thick-film datasheet (Feb 12 2019 V.3, section 2:
// 14-code system, 5th-6th "1W" = 1 W, 7th code F = +-1 %, 8th-11th "330J" =
// 330 x 10^-1) the tolerance is +-1 %, so the output stage uses 0.01.
export const MIDI_OUT_SERIES_TOLERANCE = 0.01
// Gate network of the P-channel switch: 100 ohm series from the UART pin and
// a 2.2 kOhm pull-up from gate to source (the 3.3 V rail).
export const MIDI_OUT_GATE_OHM = { series: 100, pullup: 2200 } as const
export const MIDI_OUT_GATE_TOLERANCE = 0.01

// Alpha & Omega AO3401A, LCSC C15127. Primary source: AOS datasheet Rev 3.1
// (December 2023), page 1 pinout and page 2 static parameters.
export const AO3401A = {
  mpn: "AO3401A",
  lcsc: "C15127",
  package: "SOT-23",
  pinout: { gate: 1, source: 2, drain: 3 },
  vdsMaxV: -30,
  vgsMaxV: 12,
  idMaxA: -4.0,
  rdsOnMaxOhm: 0.085,      // at VGS = -2.5 V, ID = -2.5 A, TJ = 25 C
  rdsOnTypOhm: 0.060,
  rdsOnTestVgsV: -2.5,
  vgsThresholdMinV: -0.5,
  vgsThresholdTypV: -0.9,
  vgsThresholdMaxV: -1.3,
  cissF: 645e-12,
  qg4v5C: 7e-9,
  source: "AOS AO3401A datasheet Rev 3.1 (December 2023)",
} as const

// Board rail and the RP2350 limits the gate network works against. The rail is
// the Pico 3V3 output at -5 %; VOL is the RP2350 VOL maximum at IOVDD 3.3 V
// (table 1683), RPD its minimum internal pull-down at 3.3 V.
export const OUTPUT_GATE_DRIVE = {
  railMinV: 3.135,
  railMaxV: 3.465,
  gpioVolMaxV: 0.5,
  gpioPullDownMinOhm: 36000,
  rp2350VolMaxV: 0.5,
  rp2350PullDownMinOhm: 36000,
  source: "RP2350 datasheet table 1683: VOL max 0.5 V, RPD 36-113 kOhm at IOVDD 3.3 V",
} as const

/**
 * Gate-source voltage magnitude of the high-side switch while the UART pin
 * drives the gate low. Series 100 ohm and pull-up 2.2 kOhm at +-1 %, the
 * weakest rail and the highest GPIO low level.
 */
export const gateOnVgs = (): number => {
  const pullup = MIDI_OUT_GATE_OHM.pullup * (1 - MIDI_OUT_GATE_TOLERANCE)
  const series = MIDI_OUT_GATE_OHM.series * (1 + MIDI_OUT_GATE_TOLERANCE)
  const { railMinV, gpioVolMaxV } = OUTPUT_GATE_DRIVE
  const gateV = gpioVolMaxV + (railMinV - gpioVolMaxV) * series / (pullup + series)
  return railMinV - gateV
}

/**
 * Gate-source voltage magnitude with the UART pin high-impedance and only the
 * RP2350 internal pull-down holding the gate away from the source. This is the
 * off-state leakage risk: it must stay below the gate threshold magnitude.
 */
export const gateOffVgs = (): number => {
  // Worst-case off state: strongest rail, largest pull-up and series
  // resistors, weakest pull-down. A larger rail or pull-up raises |VGS|.
  const pullup = MIDI_OUT_GATE_OHM.pullup * (1 + MIDI_OUT_GATE_TOLERANCE)
  const series = MIDI_OUT_GATE_OHM.series * (1 + MIDI_OUT_GATE_TOLERANCE)
  const pulldown = OUTPUT_GATE_DRIVE.gpioPullDownMinOhm
  const rail = OUTPUT_GATE_DRIVE.railMaxV
  const gateV = rail * pulldown / (pulldown + pullup + series)
  return rail - gateV
}

// CA-033 asks for RA 33 ohm and RC 10 ohm at 3.3 V. The tip resistor is
// permanently grounded, so a 10 ohm part would carry 3.465^2 / 9.5 = 1.24 W in
// a tip-to-ground fault, far above the 0.25 W CA-033 assumes for RC. This
// design deliberately uses 22 ohm there with a 1 W 2512 part.
export const OUTPUT_STAGE_DEVIATION = {
  ca033RingOhm: 33,
  ca033TipOhm: 10,
  implementedRingOhm: MIDI_OUT_SERIES_OHM.ring,
  implementedTipOhm: MIDI_OUT_SERIES_OHM.tip,
  resistorRatedW: 1.0,
  ca033ResistorRatedW: 0.25,
  reason: "RC is permanently grounded: 10 ohm would dissipate about 1.26 W in a tip fault",
} as const

/** Worst-case dissipation in one output resistor, in W. */
export type FaultPower = {
  readonly ringToGroundW: number
  readonly tipToGroundW: number
  readonly ringTipShortRingW: number
  readonly ringTipShortTipW: number
}

/**
 * Fault dissipation at the strongest rail. A ring-to-ground fault sees only the
 * ring resistor, a tip-to-ground fault only the tip resistor, and a ring-tip
 * short sees both in series at -tolerance, which is the largest current.
 */
export const outputFaultPower = (
  railV: number = 3.465,
  rdsOnOhm: number = AO3401A.rdsOnMaxOhm,
): FaultPower => {
  const ring = MIDI_OUT_SERIES_OHM.ring * (1 - MIDI_OUT_SERIES_TOLERANCE)
  const tip = MIDI_OUT_SERIES_OHM.tip * (1 - MIDI_OUT_SERIES_TOLERANCE)
  const ringCurrent = railV / (ring + rdsOnOhm)
  const tipCurrent = railV / (tip + rdsOnOhm)
  const loopCurrent = railV / (ring + tip + rdsOnOhm)
  return {
    ringToGroundW: ringCurrent * ringCurrent * ring,
    tipToGroundW: tipCurrent * tipCurrent * tip,
    ringTipShortRingW: loopCurrent * loopCurrent * ring,
    ringTipShortTipW: loopCurrent * loopCurrent * tip,
  }
}

// CA-033 limits used by the transmitter (output) analysis.
export const CA033 = {
  nominalLoopA: 5e-3,
  /** The receiver must turn on below this current. */
  receiverMustTurnOnBelowA: 5e-3,
  /** LED maximum forward drop in a legacy receiver, in V. */
  legacyReceiverLedMaxVf: 1.9,
  /** Receiver resistor seen by the transmitter, in ohm. */
  legacyReceiverOhm: 220,
  legacyReceiverOhmTolerance: 0.05,
  /** PC900V worst-case forward current at RD = 280 ohm, VRX = 5 V, in A. */
  pc900vWorstCaseCurrentA: 4e-3,
  /** Minimum transmitter voltage for a 5 mA loop: 0.005 * 220 + 1.9, in V. */
  minimumTransmitterV: 3.0,
  source: "MIDI 1.0 CA-033 (2014) page 4",
} as const

/**
 * Current this design's MIDI output can push through a legacy receiver
 * (LED plus 220 ohm). The P-channel switch sources through the 33 ohm ring
 * resistor and the receiver returns through the 22 ohm tip resistor.
 */
export const transmitterLoopCurrent = (opts: {
  /** Supply at the switch source, in V. */
  readonly railV: number
  /** Switch series drop, in V: RDS(ON) times current, or a PNP VCE(sat). */
  readonly dropV: number
  /** Receiver LED forward voltage, in V. */
  readonly vfReceiverV: number
  readonly receiverTolerance?: number
  /** Ring and tip resistor values, defaulting to this design's stage. */
  readonly ringOhm?: number
  readonly tipOhm?: number
}): number => {
  const ring = opts.ringOhm ?? MIDI_OUT_SERIES_OHM.ring
  const tip = opts.tipOhm ?? MIDI_OUT_SERIES_OHM.tip
  const seriesOhm = (ring + tip) * (1 + MIDI_OUT_SERIES_TOLERANCE)
  const receiverOhm = CA033.legacyReceiverOhm *
    (1 + (opts.receiverTolerance ?? CA033.legacyReceiverOhmTolerance))
  return (opts.railV - opts.dropV - opts.vfReceiverV) / (seriesOhm + receiverOhm)
}

/**
 * Worst-case legacy-receiver current of this stage: weakest rail, highest LED
 * forward drop, every resistor at +tolerance, RDS(ON) at its 85 mOhm limit.
 * The voltage drop across RDS(ON) is included through the small extra term.
 */
export const legacyReceiverWorstCurrent = (): number => {
  const seriesOhm = (MIDI_OUT_SERIES_OHM.ring + MIDI_OUT_SERIES_OHM.tip) *
    (1 + MIDI_OUT_SERIES_TOLERANCE)
  const receiverOhm = CA033.legacyReceiverOhm * (1 + CA033.legacyReceiverOhmTolerance)
  return (OUTPUT_GATE_DRIVE.railMinV - CA033.legacyReceiverLedMaxVf) /
    (seriesOhm + receiverOhm + AO3401A.rdsOnMaxOhm)
}

/**
 * Current the replaced PNP stage (22 ohm ring, 33 ohm tip) delivered into the
 * same receiver. Kept for the negative test that records the old deficit.
 */
export const legacyReceiverCurrentOldPnp = (): number =>
  transmitterLoopCurrent({
    railV: OUTPUT_GATE_DRIVE.railMinV,
    dropV: 0.25,
    vfReceiverV: CA033.legacyReceiverLedMaxVf,
    ringOhm: 22,
    tipOhm: 33,
  })

// RP2350 digital input limits, IOVDD = 3.3 V. Primary source: RP2350
// datasheet table 1683 (pdf page 1337, printed page 1336).
export const RP2350_INPUT = {
  vihMinV: 2.0,
  vilMaxV: 0.8,
  leakageMaxA: 1e-6,
  source: "RP2350 datasheet table 1683, IOVDD = 3.3 V: VIH min 2.0 V, VIL max 0.8 V, input leakage 1 uA",
} as const

export type SenderCorner = {
  readonly name: string
  /** Open-circuit sender supply, before tolerance. */
  readonly supplyV: number
  readonly supplyTolerance: number
  /** Sender series resistor on the DIN 4 (ring) side, in ohm. */
  readonly raOhm: number
  /** Sender series resistor on the DIN 5 (tip) return side, in ohm. */
  readonly rcOhm: number
  readonly resistorTolerance: number
  /**
   * Sender output driver saturation drop, low and high bounds, in V. CA-033
   * does not specify this; the range is an explicit assumption about the
   * external device and must be measured on real senders. The low bound is
   * 0 V because no minimum loss is guaranteed: a low-loss or P-channel
   * sender can drop almost nothing, so the maximum-current bound assumes it.
   */
  readonly driverDropMinV: number
  readonly driverDropMaxV: number
  readonly source: string
}

// CA-033 figure 1, 5 V column: VTX +5 V +-10%, RA 220 ohm 5% 0.25 W,
// RC 220 ohm 5% 0.25 W.
export const SENDER_5V: SenderCorner = {
  name: "CA-033 +5 V sender",
  supplyV: 5.0,
  supplyTolerance: 0.10,
  raOhm: 220,
  rcOhm: 220,
  resistorTolerance: 0.05,
  driverDropMinV: 0,
  driverDropMaxV: 0.3,
  source: "MIDI 1.0 CA-033 (2014) page 2, figure 1: +5 V +-10%, RA and RC 220 ohm 5%",
}

// CA-033 figure 1 and page 4, 3.3 V column: VTX +3.3 V +-5%, RA 33 ohm 5%
// 0.5 W, RC 10 ohm 5% 0.25 W. RA and RC are not equal.
export const SENDER_3V3: SenderCorner = {
  name: "CA-033 +3.3 V sender",
  supplyV: 3.3,
  supplyTolerance: 0.05,
  raOhm: 33,
  rcOhm: 10,
  resistorTolerance: 0.05,
  driverDropMinV: 0,
  driverDropMaxV: 0.3,
  source: "MIDI 1.0 CA-033 (2014) pages 2 and 4: +3.3 V +-5%, RA 33 ohm 5%, RC 10 ohm 5%",
}

// Forward-voltage bounds used by the calculation. Two different numbers,
// kept apart on purpose:
//   * VF_MAX_AT_2MA_V = 1.65 V is the datasheet TLP2361 maximum, specified at
//     IF = 2 mA, Ta = 25 C. It is NOT a bound at the ~3.7 mA this loop runs at.
//   * VF_ASSUMED_HIGH_V = 1.85 V is the assumed upper bound at that higher
//     current, used for the minimum-current corner. It is an assumption to
//     confirm, not a guaranteed limit.
// The maximum-current corner uses the datasheet VF minimum (1.35 V at 2 mA),
// which is conservative because VF rises with current.
export const VF_MAX_AT_2MA_V = 1.65
export const VF_ASSUMED_HIGH_V = 1.85

export type LoopResult = {
  /** Minimum possible steady-state LED current, in A. */
  readonly minA: number
  /** Maximum possible steady-state LED current, in A. */
  readonly maxA: number
}

/**
 * Worst-case current-loop bounds for one sender corner and one opto LED.
 *
 * Minimum: weakest supply, highest sender driver drop, highest assumed LED
 * drop, all resistors at +tolerance. Maximum: strongest supply, lowest driver
 * drop, lowest datasheet LED drop, all resistors at -tolerance. The receiver
 * resistor is this design's 1 % part, the sender resistors are the 5 % parts
 * CA-033 specifies.
 */
export const loopCurrent = (
  sender: SenderCorner,
  opto: OptoSpec,
  vfMinCurrentCorner: number = VF_ASSUMED_HIGH_V,
): LoopResult => {
  const vLow = sender.supplyV * (1 - sender.supplyTolerance)
  const vHigh = sender.supplyV * (1 + sender.supplyTolerance)
  const senderOhm = sender.raOhm + sender.rcOhm
  const rLow = senderOhm * (1 - sender.resistorTolerance) +
    MIDI_RECEIVER_OHM * (1 - MIDI_RECEIVER_TOLERANCE)
  const rHigh = senderOhm * (1 + sender.resistorTolerance) +
    MIDI_RECEIVER_OHM * (1 + MIDI_RECEIVER_TOLERANCE)
  return {
    minA: (vLow - sender.driverDropMaxV - vfMinCurrentCorner) / rHigh,
    maxA: (vHigh - sender.driverDropMinV - opto.vfMin) / rLow,
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

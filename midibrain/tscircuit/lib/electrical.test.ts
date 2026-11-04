import { describe, expect, test } from "bun:test"
import {
  BOARD_POLICY,
  BYPASS_POLICY,
  idcPadPosition,
  padDistanceMm,
  perimeterPadPosition,
  TRACE_POLICY,
} from "./electrical"
import {
  absoluteCurrentUse,
  AO3401A,
  CA033,
  gateOffVgs,
  gateOnVgs,
  legacyReceiverCurrentOldPnp,
  legacyReceiverWorstCurrent,
  loopCurrent,
  outputFaultPower,
  OUTPUT_STAGE_DEVIATION,
  MIDI_OUT_GATE_OHM,
  MIDI_OUT_SERIES_OHM,
  MIDI_RECEIVER_OHM,
  RP2350_INPUT,
  SENDER_3V3,
  SENDER_5V,
  thresholdMargin,
  TLP2361,
  TLP2362,
  transmitterLoopCurrent,
  VF_ASSUMED_HIGH_V,
  type SenderCorner,
} from "./electrical-midi"

// These tests are calculations and rule checks. They are not measurements and
// they do not build the boards (no dist/ is read). Every expected number below
// is worked out from the datasheet limits written next to it, so loosening a
// tolerance, a threshold or a required voltage drop in the module makes a test
// fail instead of quietly improving the margin.

// Hand arithmetic, CA-033 figure 1 columns:
//   5 V sender:    VTX 4.50/5.50 V (5 V +-10 %), RA 220 + RC 220 ohm at 5 %,
//                  receiver 220 ohm at 1 %. rLow = 440*0.95 + 220*0.99 = 635.8,
//                  rHigh = 440*1.05 + 220*1.01 = 684.2.
//                  min = (4.50 - 0.30 - 1.85) / 684.2 = 3.4347 mA
//                  max = (5.50 - 0.10 - 1.35) / 635.8 = 6.3699 mA
//   3.3 V sender:  VTX 3.135/3.465 V (3.3 V +-5 %), RA 33 + RC 10 ohm at 5 %,
//                  receiver 220 ohm at 1 %. rLow = 43*0.95 + 217.8 = 258.65,
//                  rHigh = 43*1.05 + 222.2 = 267.35.
//                  min = (3.135 - 0.30 - 1.85) / 267.35 = 3.6843 mA
//                  max = (3.465 - 0.10 - 1.35) / 258.65 = 7.7905 mA
const five = loopCurrent(SENDER_5V, TLP2361)
const three = loopCurrent(SENDER_3V3, TLP2361)

describe("MIDI input current loop, worst case", () => {
  test("5 V CA-033 sender corners", () => {
    expect(five.minA).toBeCloseTo(3.4347e-3, 6)
    expect(five.maxA).toBeCloseTo(6.3699e-3, 6)
  })

  test("3.3 V CA-033 sender corners use RA 33 ohm and RC 10 ohm", () => {
    expect(SENDER_3V3.raOhm).toBe(33)
    expect(SENDER_3V3.rcOhm).toBe(10)
    expect(three.minA).toBeCloseTo(3.6843e-3, 6)
    expect(three.maxA).toBeCloseTo(7.7905e-3, 6)
  })

  test("TLP2361 is guaranteed to switch on the full-temperature threshold", () => {
    // 3.4347 / 1.6 = 2.147 at 125 C, and 3.4347 / 1.3 = 2.642 at 105 C.
    expect(thresholdMargin(five.minA, TLP2361.ifhlGuaranteedA)).toBeCloseTo(2.1467, 4)
    expect(thresholdMargin(three.minA, TLP2361.ifhlGuaranteedA)).toBeCloseTo(2.3027, 4)
    expect(TLP2361.ifhlGuaranteedA).toBe(TLP2361.ifhlMaxA125)
    expect(thresholdMargin(five.minA, TLP2361.ifhlGuaranteedA)).toBeGreaterThan(2)
    expect(thresholdMargin(three.minA, TLP2361.ifhlGuaranteedA)).toBeGreaterThan(2)
  })

  test("TLP2362 has no guaranteed margin at the same current", () => {
    // 3.4347 / 5.0 = 0.687: below one, so turn-on is not guaranteed at all,
    // and the current is far below the 7.5 mA recommended on-state minimum.
    expect(thresholdMargin(five.minA, TLP2362.ifhlGuaranteedA)).toBeCloseTo(0.6869, 4)
    expect(thresholdMargin(five.minA, TLP2362.ifhlGuaranteedA)).toBeLessThan(1)
    expect(five.minA).toBeLessThan(TLP2362.ifOnMinA)
  })

  test("maximum current against the absolute rating, and the known deviations", () => {
    // 6.3699 / 10 = 0.637 and 7.7905 / 10 = 0.779: inside the absolute maximum.
    expect(absoluteCurrentUse(five.maxA, TLP2361)).toBeCloseTo(0.637, 3)
    expect(absoluteCurrentUse(three.maxA, TLP2361)).toBeCloseTo(0.779, 3)
    expect(five.maxA).toBeLessThan(TLP2361.ifAbsMaxA)
    expect(three.maxA).toBeLessThan(TLP2361.ifAbsMaxA)
    // Both corners sit above the 6 mA recommended on-state maximum. Recorded
    // as deviations; high-current and temperature qualification is pending.
    expect(five.maxA).toBeGreaterThan(TLP2361.ifOnMaxA)
    expect(three.maxA).toBeGreaterThan(TLP2361.ifOnMaxA)
  })

  test("the VF bound is an assumption, not a datasheet guarantee", () => {
    expect(VF_ASSUMED_HIGH_V).toBeGreaterThan(TLP2361.vfMax)
    const optimistic = loopCurrent(SENDER_5V, TLP2361, TLP2361.vfMax)
    expect(optimistic.minA).toBeGreaterThan(five.minA)
  })
})

// The reference cases above must catch a loosened input. Each "wrong" variant
// below is a plausible mistake; the numbers it produces are pinned so that a
// silent change to the model cannot hide behind a passing test.
describe("current model catches loosened inputs", () => {
  test("a 5 % instead of 10 % supply tolerance changes the result", () => {
    const loose: SenderCorner = { ...SENDER_5V, supplyTolerance: 0.05 }
    // (4.75 - 0.30 - 1.85) / 684.2 = 3.8001 mA
    expect(loopCurrent(loose, TLP2361).minA).toBeCloseTo(3.8001e-3, 6)
    expect(loopCurrent(loose, TLP2361).minA).not.toBeCloseTo(five.minA, 6)
  })

  test("1 % instead of 5 % sender resistor tolerance changes the result", () => {
    const loose: SenderCorner = { ...SENDER_5V, resistorTolerance: 0.01 }
    // rHigh = 440*1.01 + 222.2 = 666.6 -> (4.50-0.30-1.85)/666.6 = 3.5255 mA
    expect(loopCurrent(loose, TLP2361).minA).toBeCloseTo(3.5255e-3, 6)
    expect(loopCurrent(loose, TLP2361).minA).not.toBeCloseTo(five.minA, 6)
  })

  test("ignoring the sender driver drop changes the result", () => {
    const noDrop: SenderCorner = { ...SENDER_5V, driverDropMaxV: 0 }
    // (4.50 - 0.00 - 1.85) / 684.2 = 3.8731 mA
    expect(loopCurrent(noDrop, TLP2361).minA).toBeCloseTo(3.8731e-3, 6)
    expect(loopCurrent(noDrop, TLP2361).minA).not.toBeCloseTo(five.minA, 6)
    // The minimum corner must use the upper driver-drop bound, not the lower.
    expect(SENDER_5V.driverDropMaxV).toBeGreaterThan(SENDER_5V.driverDropMinV)
  })

  test("the 105 C threshold would overstate the margin", () => {
    const at105 = thresholdMargin(five.minA, TLP2361.ifhlMaxA105)
    // 3.4347 / 1.3 = 2.642 against the guaranteed 125 C figure of 2.147.
    expect(at105).toBeCloseTo(2.6421, 4)
    expect(at105).toBeGreaterThan(thresholdMargin(five.minA, TLP2361.ifhlGuaranteedA))
  })
})

describe("MIDI output: AO3401A high-side stage", () => {
  test("the switch is the AO3401A with gate 1, source 2, drain 3", () => {
    expect(AO3401A.lcsc).toBe("C15127")
    expect(AO3401A.pinout).toEqual({ gate: 1, source: 2, drain: 3 })
    // The 85 mOhm maximum is specified at VGS = -2.5 V, TJ = 25 C only.
    expect(AO3401A.rdsOnMaxOhm).toBe(0.085)
    expect(AO3401A.rdsOnTestVgsV).toBe(-2.5)
  })

  test("gate drive reaches the RDS(ON) test point at the weakest rail", () => {
    // pull-up 2200*0.99 = 2178, series 100*1.01 = 101,
    // gate = 0.5 + (3.135-0.5)*101/2279 = 0.6168, |VGS| = 2.5182 V
    expect(gateOnVgs()).toBeCloseTo(2.5182, 4)
    expect(gateOnVgs()).toBeGreaterThan(Math.abs(AO3401A.rdsOnTestVgsV))
    expect(gateOnVgs()).toBeLessThan(AO3401A.vgsMaxV)
  })

  test("off state stays below the gate threshold magnitude at 25 C only", () => {
    // 3.135 * 36000 / (36000 + 2178 + 101) = 2.9484 V at the gate,
    // |VGS| = 0.1867 V against a -0.5 V minimum threshold magnitude.
    expect(gateOffVgs()).toBeCloseTo(0.1867, 3)
    expect(gateOffVgs()).toBeLessThan(Math.abs(AO3401A.vgsThresholdMinV))
    // No full-temperature off guarantee is claimed: the threshold magnitude
    // falls with temperature and the datasheet gives no -40..125 C limit here.
    expect(Math.abs(AO3401A.vgsThresholdMinV)).toBe(0.5)
  })

  test("legacy receiver current now exceeds the PC900V requirement", () => {
    // (3.135 - 1.90) / (55*1.05 + 220*1.05 + 0.085) = 4.2758 mA
    expect(legacyReceiverWorstCurrent()).toBeCloseTo(4.2758e-3, 6)
    expect(legacyReceiverWorstCurrent()).toBeGreaterThan(CA033.pc900vWorstCaseCurrentA)
    // Robust to a hot RDS(ON) assumption: 0.15 ohm still gives 4.2748 mA.
    const hot = (3.135 - 1.9) / ((55 * 1.05) + (220 * 1.05) + 0.15)
    expect(hot).toBeCloseTo(4.2748e-3, 6)
    expect(hot).toBeGreaterThan(CA033.pc900vWorstCaseCurrentA)
  })

  test("the replaced PNP stage falls short of the same requirement", () => {
    // (3.135 - 0.25 - 1.90) / (55*1.05 + 220*1.05) = 3.4113 mA with the 5 %
    // 2512 parts; the model uses the same 5 % tolerance for both stages.
    expect(legacyReceiverCurrentOldPnp()).toBeCloseTo(3.4113e-3, 6)
    expect(legacyReceiverCurrentOldPnp()).toBeLessThan(CA033.pc900vWorstCaseCurrentA)
  })

  test("fault dissipation per resistor stays inside the 1 W 2512 rating", () => {
    const fault = outputFaultPower()
    // ring 33 ohm 5 % -> 31.35: 3.465 / (31.35 + 0.085) = 110.22 mA,
    // P = 0.3809 W
    expect(fault.ringToGroundW).toBeCloseTo(0.3809, 3)
    // tip 22 ohm 5 % -> 20.90: 3.465 / (20.90 + 0.085) = 165.14 mA,
    // P = 0.5701 W
    expect(fault.tipToGroundW).toBeCloseTo(0.5701, 3)
    // ring-tip short uses the LOW total resistance 52.25: 63.54 mA through the
    // pair, so the per-part dissipation falls to 0.1375 W and 0.0916 W
    expect(fault.ringTipShortRingW).toBeCloseTo(0.1375, 3)
    expect(fault.ringTipShortTipW).toBeCloseTo(0.0916, 3)
    for (const watts of Object.values(fault)) {
      expect(watts).toBeLessThan(OUTPUT_STAGE_DEVIATION.resistorRatedW)
    }
  })

  test("a 10 ohm CA-033 tip resistor would be unsafe, which justifies the deviation", () => {
    // CA-033 RC = 10 ohm: 3.465^2 / 9.5 = 1.2638 W in a tip fault.
    const ca033TipFault = Math.pow(3.465, 2) / (OUTPUT_STAGE_DEVIATION.ca033TipOhm * 0.95)
    expect(ca033TipFault).toBeCloseTo(1.2638, 3)
    expect(ca033TipFault).toBeGreaterThan(OUTPUT_STAGE_DEVIATION.ca033ResistorRatedW)
    expect(OUTPUT_STAGE_DEVIATION.implementedTipOhm).toBe(22)
    expect(OUTPUT_STAGE_DEVIATION.implementedRingOhm).toBe(33)
  })
})

describe("RP2350 digital input levels", () => {
  test("the opto output levels clear the RP2350 limits with margin", () => {
    // Table 1683 at IOVDD 3.3 V: VIH min 2.0 V, VIL max 0.8 V.
    expect(RP2350_INPUT.vihMinV).toBe(2.0)
    expect(RP2350_INPUT.vilMaxV).toBe(0.8)
    expect(TLP2361.vohMinAt20uA - RP2350_INPUT.vihMinV).toBeCloseTo(1.2, 6)
    expect(RP2350_INPUT.vilMaxV - TLP2361.volMaxAt3m2mA).toBeCloseTo(0.4, 6)
    // 1 uA of input leakage against the 2.2 k pull-up is 2.2 mV.
    expect(RP2350_INPUT.leakageMaxA * 2200).toBeCloseTo(2.2e-3, 6)
  })
})

describe("MIDI Type A contact assignment", () => {
  test("the receiver resistor is on the ring side and the output sink on the tip", () => {
    // Constants that the boards and the checker both rely on: a 220 ohm
    // receiver, the 33 ohm ring resistor and the 22 ohm tip resistor, plus the
    // 100 ohm / 2.2 kOhm gate network. The electrical meaning of the TRS to
    // DIN mapping is verified on the built circuit by
    // scripts/check_electrical.py, which walks the net graph.
    expect(MIDI_RECEIVER_OHM).toBe(220)
    expect(MIDI_OUT_SERIES_OHM.ring).toBe(33)
    expect(MIDI_OUT_SERIES_OHM.tip).toBe(22)
    expect(MIDI_OUT_GATE_OHM.series).toBe(100)
    expect(MIDI_OUT_GATE_OHM.pullup).toBe(2200)
  })
})

describe("board rules", () => {
  test("fabrication limits are at or above the JLCPCB 2-layer capability", () => {
    // Independent fab numbers: 0.15 mm minimum trace/space, 0.20 mm minimum
    // plated drill, 0.15 mm minimum SMD pad edge-to-edge gap.
    const mm = (v: string) => Number.parseFloat(v)
    expect(mm(BOARD_POLICY.minTraceWidth)).toBeGreaterThanOrEqual(0.15)
    expect(mm(BOARD_POLICY.minViaHoleDiameter)).toBeGreaterThanOrEqual(0.2)
    expect(mm(BOARD_POLICY.minViaPadDiameter)).toBeGreaterThanOrEqual(0.45)
    expect(mm(BOARD_POLICY.minPadEdgeToPadEdgeClearance)).toBeGreaterThanOrEqual(0.15)
    // Annular ring on a via: (pad - drill) / 2 must be at least 0.1 mm.
    expect((mm(BOARD_POLICY.minViaPadDiameter) - mm(BOARD_POLICY.minViaHoleDiameter)) / 2)
      .toBeGreaterThanOrEqual(0.1)
    expect(TRACE_POLICY.signalNominalMm).toBeGreaterThan(TRACE_POLICY.signalFloorMm)
  })

  test("bypass distance helper measures centre to centre", () => {
    expect(padDistanceMm({ x: 0, y: 0 }, { x: 3, y: 4 })).toBe(5)
    expect(BYPASS_POLICY.railPadToPinMaxMm).toBeLessThan(BYPASS_POLICY.groundStraightMaxMm)
  })
})

describe("IDC pad numbering", () => {
  test("2 x N numbering alternates rows, as the Pi header and ribbon do", () => {
    expect(idcPadPosition(1)).toEqual({ x: 0, y: 0 })
    expect(idcPadPosition(2)).toEqual({ x: 0, y: 2.54 })
    expect(idcPadPosition(3)).toEqual({ x: 2.54, y: 0 })
    expect(idcPadPosition(4)).toEqual({ x: 2.54, y: 2.54 })
    // Pin 40 of a 2 x 20 header sits at the far end of the second row:
    // 19 * 2.54 = 48.26 mm from pin 1.
    expect(idcPadPosition(40)).toEqual({ x: 48.26, y: 2.54 })
    expect(idcPadPosition(26)).toEqual({ x: 30.48, y: 2.54 })
  })

  test("perimeter numbering lands pin 3 in a different place", () => {
    // This is the failure the generic 2-row footprints caused: pin 3 would
    // sit back along the first row instead of starting the second pair.
    const idc = idcPadPosition(3)
    const perimeter = perimeterPadPosition(3, 20)
    expect(perimeter).not.toEqual(idc)
    expect(perimeter).toEqual({ x: 5.08, y: 0 })
    // Pair 20/21 is likewise wrong under perimeter numbering, not just pair 2.
    expect(perimeterPadPosition(21, 20)).toEqual({ x: 48.26, y: 2.54 })
    expect(idcPadPosition(21)).toEqual({ x: 25.4, y: 0 })
  })
})

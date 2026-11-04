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
  loopCurrent,
  MIDI_OUT_SERIES_OHM,
  MIDI_RECEIVER_OHM,
  SENDER_3V3,
  SENDER_5V,
  thresholdMargin,
  TLP2361,
  TLP2362,
  VF_ASSUMED_HIGH_V,
} from "./electrical-midi"

// These tests are calculations and rule checks. They are not measurements and
// they do not build the boards (no dist/ is read).

describe("MIDI input current loop, worst case", () => {
  // Arithmetic, independent of the implementation:
  //   5 V sender: low 4.75 V, high 5.25 V; resistors 2 x 220 ohm + 220 ohm.
  //   rLow  = 220*0.99*3            = 653.4 ohm
  //   rHigh = 220*1.01*3            = 666.6 ohm
  //   min   = (4.75 - 1.85) / 666.6 = 4.3504 mA   (1.85 V assumed VF bound)
  //   max   = (5.25 - 1.35) / 653.4 = 5.9688 mA   (1.35 V = VF min at 2 mA)
  const five = loopCurrent(SENDER_5V, TLP2361)
  test("5 V sender corners", () => {
    expect(five.minA).toBeCloseTo(4.3504e-3, 6)
    expect(five.maxA).toBeCloseTo(5.9688e-3, 6)
  })

  test("TLP2361 is guaranteed to switch with a 5 V sender", () => {
    // Margin against the datasheet threshold: 4.3504 / 1.3 = 3.35x
    expect(thresholdMargin(five.minA, TLP2361.ifhlMaxA105)).toBeCloseTo(3.3465, 4)
    expect(thresholdMargin(five.minA, TLP2361.ifhlMaxA105)).toBeGreaterThan(2)
  })

  test("TLP2362 has no guaranteed margin at the same current", () => {
    // 4.3504 / 5.0 = 0.87x: below one, so turn-on is not guaranteed at all.
    expect(thresholdMargin(five.minA, TLP2362.ifhlMaxA105)).toBeCloseTo(0.8701, 4)
    expect(thresholdMargin(five.minA, TLP2362.ifhlMaxA105)).toBeLessThan(1)
    // It is also outside the TLP2362 recommended on-state range (7.5-14 mA).
    expect(five.minA).toBeLessThan(TLP2362.ifOnMinA)
  })

  test("3.3 V CA-033 sender corners", () => {
    //   3.3 V sender: low 3.135 V, high 3.465 V; 2 x 33 ohm + 220 ohm.
    //   rLow  = 33*0.99*2 + 220*0.99   = 283.14 ohm
    //   rHigh = 33*1.01*2 + 220*1.01   = 288.86 ohm
    //   min   = (3.135 - 1.85) / 288.86 = 4.4485 mA
    //   max   = (3.465 - 1.35) / 283.14 = 7.4698 mA
    const three = loopCurrent(SENDER_3V3, TLP2361)
    expect(three.minA).toBeCloseTo(4.4485e-3, 6)
    expect(three.maxA).toBeCloseTo(7.4698e-3, 6)
    expect(thresholdMargin(three.minA, TLP2361.ifhlMaxA105)).toBeGreaterThan(2)
  })

  test("maximum current against the absolute rating, and the known deviation", () => {
    const three = loopCurrent(SENDER_3V3, TLP2361)
    // 5.9688 / 10 = 0.60, 7.4698 / 10 = 0.75: both inside the absolute maximum.
    expect(absoluteCurrentUse(five.maxA, TLP2361)).toBeCloseTo(0.597, 3)
    expect(absoluteCurrentUse(three.maxA, TLP2361)).toBeCloseTo(0.747, 3)
    expect(five.maxA).toBeLessThan(TLP2361.ifAbsMaxA)
    expect(three.maxA).toBeLessThan(TLP2361.ifAbsMaxA)
    // Within the recommended maximum up to the 5 V sender, above it at the
    // 3.3 V corner. Recorded as a deviation, not silently accepted.
    expect(five.maxA).toBeLessThan(TLP2361.ifOnMaxA)
    expect(three.maxA).toBeGreaterThan(TLP2361.ifOnMaxA)
  })

  test("the assumed VF bound is not a datasheet guarantee", () => {
    // The datasheet VF maximum is specified at 2 mA only. Using it at the
    // ~4.4 mA this loop runs at would overstate the margin, so the minimum
    // corner uses a larger, explicitly assumed bound.
    expect(VF_ASSUMED_HIGH_V).toBeGreaterThan(TLP2361.vfMax)
    const optimistic = loopCurrent(SENDER_5V, TLP2361, TLP2361.vfMax)
    expect(optimistic.minA).toBeGreaterThan(five.minA)
  })
})

describe("MIDI Type A contact assignment", () => {
  test("the receiver resistor is on the ring side and the output sink on the tip", () => {
    // Constants that the boards and the checker both rely on: a 220 ohm
    // receiver and 22/33 ohm output resistors. The electrical meaning of the
    // TRS to DIN mapping is verified on the built circuit by
    // scripts/check_electrical.py, which walks the net graph.
    expect(MIDI_RECEIVER_OHM).toBe(220)
    expect(MIDI_OUT_SERIES_OHM.source).toBe(22)
    expect(MIDI_OUT_SERIES_OHM.sink).toBe(33)
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

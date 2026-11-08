// Source-level checks for the rear-board probe points. The BUILT pads are
// measured independently by scripts/check_electrical.py; this file proves the
// placement intent: the approved net set, the ring target, the keepout
// envelopes, no two pads on top of each other, and label ink that cannot land
// on a pad.
//
// Nothing here is a measurement or a physical-access approval.

import { expect, test } from "bun:test"
import { REAR_PCB } from "./layout"
import {
  PROBE_EDGE_MM,
  PROBE_FOOTPRINT,
  PROBE_HOLE_DIAMETER_MM,
  PROBE_KEEPOUTS,
  PROBE_LABEL_FONT_MM,
  PROBE_MIN_PITCH_MM,
  PROBE_MIN_RING_MM,
  PROBE_PAD_DIAMETER_MM,
  PROBE_POINTS,
  PROBE_RING_MM,
  probeLabelPosition,
} from "./probe-points"

const PAD_RADIUS = PROBE_PAD_DIAMETER_MM / 2
const SILK_CLEARANCE = 0.2
const SILK_ADVANCE_RATIO = 0.69

const labelBox = (point: (typeof PROBE_POINTS)[number]) => {
  const at = probeLabelPosition(point)
  const halfWidth = (point.label.length * PROBE_LABEL_FONT_MM * SILK_ADVANCE_RATIO) / 2
  const halfHeight = PROBE_LABEL_FONT_MM / 2
  return {
    x1: at.x - halfWidth, x2: at.x + halfWidth,
    y1: at.y - halfHeight, y2: at.y + halfHeight,
  }
}

const pointBoxGap = (x: number, y: number, b: { x1: number; x2: number; y1: number; y2: number }): number =>
  Math.hypot(Math.max(b.x1 - x, 0, x - b.x2), Math.max(b.y1 - y, 0, y - b.y2))

test("the probe net set is exactly the approved one", () => {
  // Owner list: P5V, separate Pico 3V3 and Pi 3V3, RUN, SPI CLK/CS/MOSI/MISO
  // and READY, MIDI TX1/TX2/RX1/RX2, and a local GND beside each group.
  const nets = PROBE_POINTS.map(p => p.net).sort()
  expect(nets).toEqual([
    "GND", "GND", "MIDI_IN_1_RX", "MIDI_IN_2_RX", "MIDI_OUT_1_TX",
    "MIDI_OUT_2_TX", "P3V3", "P5V", "PI_3V3", "READY_PICO", "RUN",
    "SPI_CS_PICO", "SPI_MISO_PICO", "SPI_MOSI_PICO", "SPI_SCLK_PICO",
  ])
  expect(PROBE_POINTS.filter(p => p.group === "logic")).toHaveLength(10)
  expect(PROBE_POINTS.filter(p => p.group === "midi")).toHaveLength(5)
})

test("the Pico and Pi 3V3 rails stay on separate probe points and separate nets", () => {
  const pico = PROBE_POINTS.find(p => p.net === "P3V3")!
  const pi = PROBE_POINTS.find(p => p.net === "PI_3V3")!
  expect(pico.ref).not.toBe(pi.ref)
  expect(pico.net).not.toBe(pi.net)
  // The pads are 5 mm apart and are not joined by the data: each entry names
  // exactly one net.
  expect(Math.hypot(pico.x - pi.x, pico.y - pi.y)).toBeGreaterThanOrEqual(3)
  expect(pico.signal).toContain("Pico module pin 36")
  expect(pi.signal.toLowerCase()).toContain("not the pico 3v3 rail")
})

test("every point is a plated through-hole that meets the 0.25 mm ring target", () => {
  expect(PROBE_HOLE_DIAMETER_MM).toBe(0.8)
  expect(PROBE_PAD_DIAMETER_MM).toBe(1.6)
  expect(PROBE_RING_MM).toBeCloseTo(0.4, 9)
  expect(PROBE_RING_MM).toBeGreaterThanOrEqual(PROBE_MIN_RING_MM)
  // The footprint string must carry the pad as well as the drill: the
  // through-hole testpoint variant derives the pad from the drill alone and
  // ignores padDiameter, which is why this string exists.
  expect(PROBE_FOOTPRINT).toBe("platedhole_d0.8mm_pd1.6mm")
  expect(PROBE_FOOTPRINT).toContain(`_pd${PROBE_PAD_DIAMETER_MM}mm`)
})

test("no probe pad sits in a documented mechanical envelope", () => {
  for (const point of PROBE_POINTS) {
    for (const [name, box] of Object.entries(PROBE_KEEPOUTS)) {
      const gap = pointBoxGap(point.x, point.y, box)
      expect({ ref: point.ref, keepout: name, clear: gap > PAD_RADIUS })
        .toEqual({ ref: point.ref, keepout: name, clear: true })
    }
  }
})

test("the source board-edge target is the same 2.0 mm the checker enforces", () => {
  expect(PROBE_EDGE_MM).toBe(2.0)
})

test("probe pads keep a real gap from each other and from the board edge", () => {
  for (let i = 0; i < PROBE_POINTS.length; i++) {
    const a = PROBE_POINTS[i]!
    for (let j = i + 1; j < PROBE_POINTS.length; j++) {
      const b = PROBE_POINTS[j]!
      const pitch = Math.hypot(a.x - b.x, a.y - b.y)
      // Every pair is at least 3.0 mm apart (logic 5.0/3.0 mm, MIDI 3.0 mm),
      // which leaves at least 1.4 mm of copper-free gap for a probe tip.
      expect({ a: a.ref, b: b.ref, ok: pitch >= PROBE_MIN_PITCH_MM }).toEqual({ a: a.ref, b: b.ref, ok: true })
    }
    const halfW = REAR_PCB.width / 2
    const halfH = REAR_PCB.height / 2
    expect(a.x - PAD_RADIUS).toBeGreaterThanOrEqual(-halfW + PROBE_EDGE_MM)
    expect(a.x + PAD_RADIUS).toBeLessThanOrEqual(halfW - PROBE_EDGE_MM)
    expect(a.y - PAD_RADIUS).toBeGreaterThanOrEqual(-halfH + PROBE_EDGE_MM)
    expect(a.y + PAD_RADIUS).toBeLessThanOrEqual(halfH - PROBE_EDGE_MM)
  }
})

test("probe labels cannot print on a pad or on each other", () => {
  for (const point of PROBE_POINTS) {
    const box = labelBox(point)
    for (const other of PROBE_POINTS) {
      const gap = pointBoxGap(other.x, other.y, box) - PAD_RADIUS
      expect({ label: point.ref, pad: other.ref, clear: gap >= SILK_CLEARANCE })
        .toEqual({ label: point.ref, pad: other.ref, clear: true })
    }
    for (const other of PROBE_POINTS) {
      if (other.ref === point.ref) continue
      const b = labelBox(other)
      const overlap = box.x1 < b.x2 && b.x1 < box.x2 && box.y1 < b.y2 && b.y1 < box.y2
      expect({ a: point.ref, b: other.ref, overlap }).toEqual({ a: point.ref, b: other.ref, overlap: false })
    }
  }
})

test("every probe point says which net it is on and which side of a buffer or resistor", () => {
  for (const point of PROBE_POINTS) {
    expect(point.signal.startsWith(point.net)).toBe(true)
  }
  // The SPI and READY points are on the Pico side of the series parts, not
  // the Pi side: name the part so a probe is never described wrongly.
  expect(PROBE_POINTS.find(p => p.net === "SPI_SCLK_PICO")!.signal).toContain("R14")
  expect(PROBE_POINTS.find(p => p.net === "SPI_CS_PICO")!.signal).toContain("R15")
  expect(PROBE_POINTS.find(p => p.net === "SPI_MOSI_PICO")!.signal).toContain("R13")
  expect(PROBE_POINTS.find(p => p.net === "SPI_MISO_PICO")!.signal).toContain("U3 input A")
  expect(PROBE_POINTS.find(p => p.net === "READY_PICO")!.signal).toContain("U4 input A")
  // MIDI channel 2 is a different net and a different front-board stage:
  // TX2 reaches R6 pin 1 (Q2 gate side is R6 pin 2), RX2 comes from U2 pin 5
  // with the R4 pull-up. Channel 1's parts must not appear in those rows.
  const tx2 = PROBE_POINTS.find(p => p.net === "MIDI_OUT_2_TX")!
  const rx2 = PROBE_POINTS.find(p => p.net === "MIDI_IN_2_RX")!
  expect(tx2.signal).toContain("module pin 6")
  expect(tx2.signal).toContain("R6")
  expect(tx2.signal).not.toContain("R5")
  expect(rx2.signal).toContain("module pin 7")
  expect(rx2.signal).toContain("U2 pin 5")
  expect(rx2.signal).toContain("R4")
  expect(rx2.signal).not.toContain("U1")
  // The two channel-1 rows keep their own identity.
  expect(PROBE_POINTS.find(p => p.net === "MIDI_OUT_1_TX")!.signal).toContain("R5")
  expect(PROBE_POINTS.find(p => p.net === "MIDI_IN_1_RX")!.signal).toContain("U1 pin 5")
})

// Board-level electrical design rules. One source for the fabrication limits
// that the two boards pass to <board>. The independent checker
// (scripts/check_electrical.py) re-derives these from the emitted circuit JSON;
// it does not import this file, so a change here still has to show up in the
// built output.
//
// JLCPCB 2-layer capability (published rules, recheck at order):
//   * minimum trace/space 0.15 mm; this project uses >= 0.20 mm signal.
//   * minimum plated drill 0.20 mm; this project uses 0.20 mm drills with a
//     0.45 mm via pad (0.125 mm annular ring).
//   * minimum SMD pad edge-to-pad edge 0.15 mm.
export const BOARD_POLICY = {
  minTraceWidth: "0.2mm",
  defaultTraceWidth: "0.25mm",
  minViaHoleDiameter: "0.2mm",
  minViaPadDiameter: "0.45mm",
  minPadEdgeToPadEdgeClearance: "0.15mm",
  minTraceToPadEdgeClearance: "0.15mm",
  minBoardEdgeClearance: "0.2mm",
} as const

// Signal floor and the wider copper preference for the 3.3 V rail and ground.
// The router only guarantees the floor; the preference is what the build is
// measured against, so a regression is visible in the reported metrics.
export const TRACE_POLICY = {
  signalFloorMm: 0.2,
  signalNominalMm: 0.25,
  powerPreferredMm: 0.5,
} as const

// Local bypass placement rule for an SOT-23-5 logic buffer.
//   railPadToPinMaxMm: the cap pad on the supply net must sit within 3 mm
//   (centre to centre) of the buffer VCC pad.
//   groundStraightMaxMm: the cap ground pad must sit within 5 mm of the buffer
//   GND pad; the return itself is the ground plane, so this is a straight-line
//   upper bound, not a routed length.
export const BYPASS_POLICY = {
  railPadToPinMaxMm: 3.0,
  groundStraightMaxMm: 5.0,
} as const

// Distance from the bypass pad to the local return, in mm.
export const padDistanceMm = (
  a: { x: number; y: number },
  b: { x: number; y: number },
): number => Math.hypot(a.x - b.x, a.y - b.y)

// Conventional 2 x N IDC / box-header pad position for physical pin n, in the
// connector's own frame: odd pins in one row, even pins in the other, pairs
// (1,2), (3,4), ... counting along the long axis. This is the numbering the
// Raspberry Pi 40-pin header and a 2 x 13 ribbon both use.
export const idcPadPosition = (pin: number, pitchMm = 2.54): { x: number; y: number } => {
  if (!Number.isInteger(pin) || pin < 1) throw new Error(`pin must be a positive integer, got ${pin}`)
  return {
    x: Math.floor((pin - 1) / 2) * pitchMm,
    y: ((pin - 1) % 2) * pitchMm,
  }
}

// The numbering a generic single-row "pinrow" footprint emits when it is used
// for a 2 x N connector: pins run down one row and back along the other. Kept
// here so the difference is explicit and testable, not implied.
export const perimeterPadPosition = (pin: number, pinsPerRow: number, pitchMm = 2.54): { x: number; y: number } => {
  if (!Number.isInteger(pin) || pin < 1 || pin > pinsPerRow * 2) throw new Error(`pin out of range: ${pin}`)
  return pin <= pinsPerRow
    ? { x: (pin - 1) * pitchMm, y: 0 }
    : { x: (pinsPerRow * 2 - pin) * pitchMm, y: pitchMm }
}

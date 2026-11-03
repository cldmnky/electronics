// Bottom-side board coordinates in mm (not mirrored viewing coordinates).
// Dots identify pad 1; diode bars identify the cathode (also pad 1).
// Each position is checked against the built pads in scripts/test_silk.test.ts.
export const PIN1_DOTS = [
  { ref: "U1", x: -10.21, y: 21.8 },
  { ref: "U2", x: -14.2, y: 7.8 },
  { ref: "U3", x: -3.28, y: 7.8 },
  { ref: "U11", x: 18.26, y: 7.9 },
  { ref: "U12", x: 6.8, y: -1.69 },
  { ref: "U13", x: 6.79, y: 7.5 },
  { ref: "U14", x: 2.87, y: -6.4 },
  { ref: "U15", x: 23.37, y: -24.4 },
  { ref: "Q1", x: 13.805, y: 2.77 },
  { ref: "Q2", x: -32.24, y: 39.15 },
] as const

export const CATHODE_BARS = [
  { ref: "D1", x: 26.24, y: -26.76 },
  { ref: "D3", x: 2.79, y: -59.01 },
  { ref: "D4", x: 9.29, y: -59.01 },
  { ref: "D5", x: 16.02, y: 1.4 },
] as const

export const DOT_RADIUS = 0.15
export const MARKER_STROKE = 0.2
export const BAR_HALF_LENGTH = 0.3

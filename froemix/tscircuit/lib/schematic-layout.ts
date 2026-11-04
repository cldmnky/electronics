// Froemix base board - hand-maintained tscircuit source (see froemix/AGENTS.md).
//
// Frozen schematic positions for the two analog sheets.
//
// Why a data map: the Audio and CV sheets are laid out by hand now, inside a
// <group schRelative> that switches the schematic auto-layout off (proven on the
// pinned core: explicit schX/schY come back unchanged, pcbX/pcbY are untouched
// and the group is not a subcircuit). These numbers are the positions the
// installed core produced on its own before the freeze, so the drawing keeps
// looking like the reviewed one and only the offending parts move from here.
//
// The map is schematic-only. Nothing here is a PCB coordinate, and no component
// footprint, pad, net or board position changes with it.

export const AUDIO_LAYOUT: Record<string, { x: number; y: number }> = {
  C10: { x: -2.765, y: -2.146 },
  C11: { x: -1.525, y: -2.146 },
  C12: { x: 2.562, y: -5.285 },
  C13: { x: -2.225, y: -6.605 },
  C2: { x: 0, y: 2.68 },
  C3: { x: 2.968, y: -0.1 },
  C4: { x: -5.245, y: -2.746 },
  C5: { x: -4.005, y: -2.746 },
  C6: { x: -3.027, y: 0.785 },
  C7: { x: 2.622, y: 1.885 },
  C8: { x: 2.968, y: -2.8 },
  C9: { x: 0, y: -6.08 },
  R1: { x: -2.318, y: 2.67 },
  R10: { x: -4.608, y: 3.809 },
  R11: { x: 5.225, y: -2.5 },
  R12: { x: 0.032, y: -7.965 },
  R13: { x: 5.225, y: -4.31 },
  R14: { x: 2.322, y: -7.17 },
  R15: { x: -2.318, y: -8.49 },
  R16: { x: 4.88, y: -6.12 },
  R17: { x: 4.672, y: -7.93 },
  R18: { x: -4.543, y: -6.53 },
  R19: { x: 6.962, y: -7.925 },
  R2: { x: 5.285, y: -0.55 },
  R20: { x: -4.608, y: -8.34 },
  R3: { x: 5.285, y: 1.26 },
  R4: { x: -0.463, y: 4.565 },
  R5: { x: 2.317, y: 3.77 },
  R6: { x: -5.405, y: 0.189 },
  R7: { x: -5.345, y: 1.999 },
  R8: { x: 4.667, y: 3.77 },
  R9: { x: 6.897, y: 3.07 },
  U2: { x: 0, y: 0 },
  U3: { x: 0, y: -3.4 },
}

export const CV_LAYOUT: Record<string, { x: number; y: number }> = {
  C14: { x: -2.875, y: -2.098 },
  C15: { x: -2.875, y: -3.908 },
  C16: { x: 2.867, y: 3.3 },
  C17: { x: -4.915, y: -2.098 },
  C18: { x: -2.875, y: -5.718 },
  C19: { x: -2.47, y: 2 },
  C20: { x: -1.489, y: 7.005 },
  C21: { x: 2.968, y: 1.34 },
  C33: { x: -0.673, y: -6.08 },
  C34: { x: -5.078, y: -3.982 },
  D5: { x: -5.66, y: -0.068 },
  Q1: { x: -3.565, y: 0.01 },
  R21: { x: 3, y: -0.545 },
  R22: { x: -5.62, y: 1.997 },
  R23: { x: -7.54, y: 0.278 },
  R24: { x: 3.12, y: -2.355 },
  R25: { x: -3.449, y: 3.927 },
  R26: { x: 3.06, y: -4.165 },
  R27: { x: -5.294, y: 3.877 },
  R28: { x: 5.59, y: -1.455 },
  R29: { x: 5.47, y: -3.265 },
  R30: { x: 5.185, y: 3.225 },
  R31: { x: 1.655, y: -6.005 },
  R32: { x: -5.04, y: -5.867 },
  R33: { x: 2.779, y: 5.185 },
  R34: { x: 2.049, y: 6.995 },
  R35: { x: 4.065, y: -5.975 },
  R36: { x: -3.12, y: -7.677 },
  R37: { x: 1.715, y: -7.815 },
  R38: { x: -1.31, y: -9.482 },
  R39: { x: 1.1, y: -9.625 },
  R40: { x: -7.465, y: -4.995 },
  R41: { x: -3.714, y: 5.737 },
  R42: { x: 5.345, y: 1.26 },
  R43: { x: 4.984, y: 5.035 },
  R44: { x: 7.129, y: 5.03 },
  R45: { x: 4.005, y: -7.785 },
  R46: { x: -5.47, y: -7.677 },
  U11: { x: 0, y: 0 },
  U12: { x: 0, y: -3.4 },
  U13: { x: -0.05, y: 3 },
  U14: { x: -0.391, y: 5.2 },
}

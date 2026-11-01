// Froemix euro board (front board, 20HP) - panel/board layout, single source of truth.
//
// Both `index.circuit.tsx` (the front board) and `panel.circuit.tsx` (the panel)
// import this file, so a hole in the panel and its part on the board can never
// drift apart. Coordinates are millimetres in the tscircuit frame: origin at the
// board centre, +X right, +Y up (towards the top edge of the panel).
//
// Everything here is a mechanical contract with the base board - the mating
// socket positions are the base board's frozen connector coordinates
// (froemix/AGENTS.md §3), unchanged, because both boards are centred on the
// same panel. See froemix/doc/EURO-BOARD.md for the stack-up.

export const PANEL_W = 101.6 // 20HP x 5.08mm
export const PANEL_H = 128.5 // 3U
export const BOARD_W = PANEL_W
export const BOARD_H = PANEL_H

export type Pt = { x: number; y: number }
export type Side = "top" | "bottom"

/** Doepfer 3U: rail screws 7.5mm in from each side, 3.0mm from top and bottom. */
export const RAIL_HOLE_X = PANEL_W / 2 - 7.5 // +/-43.3
export const RAIL_HOLE_Y = PANEL_H / 2 - 3.0 // +/-61.25
export const RAIL_HOLES: Pt[] = [
  { x: -RAIL_HOLE_X, y: RAIL_HOLE_Y },
  { x: RAIL_HOLE_X, y: RAIL_HOLE_Y },
  { x: -RAIL_HOLE_X, y: -RAIL_HOLE_Y },
  { x: RAIL_HOLE_X, y: -RAIL_HOLE_Y },
]

/**
 * M3 standoffs board-to-board. Same XY as the base board's own mounting holes,
 * so one M3 screw/spacer column serves both boards (see doc/EURO-BOARD.md §1).
 */
export const STANDOFF_HOLES: Pt[] = [
  { x: -36.95, y: 60.75 },
  { x: 36.95, y: 60.75 },
  { x: -36.95, y: -60.75 },
  { x: 36.95, y: -60.75 },
]

/**
 * Mating sockets, on the BOTTOM of this board, facing the base board's top-side
 * pin headers. Coordinates are copied from the base board and must not change.
 *
 * CN3 (micro SD) is deliberately NOT mated - see doc/EURO-BOARD.md §7.
 */
export const MATE = {
  CN1: { x: -17.58, y: 59.64, pins: 16, pitch: 2.0 },
  CN2: { x: 12.42, y: -36.36, pins: 16, pitch: 2.0 },
  CN3: { x: 8.11, y: 59.63, pins: 8, pitch: 2.0 }, // reserved, not mated
  CN4: { x: 24.5, y: 59.63, pins: 7, pitch: 2.0 }, // built base board uses 24.5
} as const

/** Net carried by each pin of the CN1 / CN2 sockets (base board pinout). */
export const CN1_NETS = [
  "J_MIDI_IN_TIP",
  "J_MIDI_IN_RING",
  "J_MIDI_OUT_TIP",
  "J_MIDI_OUT_RING",
  "M_ADC_0",
  "M_ADC_1",
  "M_ADC_2",
  "M_ADC_3",
  "M_ADC_4",
  "M_ADC_5",
  "M_ADC_6",
  "M_ADC_7",
  "LED",
  "P3V3_D",
  "P3V3_A",
  "GND",
] as const

/** CN2 pins 11-15 are unconnected on the base board (encoder reserve). */
export const CN2_NETS = [
  "J_AUDIO_IN_L",
  "J_AUDIO_IN_R",
  "J_AUDIO_OUT_L",
  "J_AUDIO_OUT_R",
  "J_SYNC_IN",
  "J_SYNC_OUT",
  "J_CV_IN_1",
  "J_CV_IN_2",
  "J_CV_OUT_1",
  "J_CV_OUT_2",
  null, // pin 11 - reserved
  null, // pin 12 - reserved
  null, // pin 13 - reserved
  null, // pin 14 - reserved
  null, // pin 15 - reserved
  "GND",
] as const

/** CN4 (OLED, 1x7 P2.0): the front board carries the module itself. */
export const CN4_NETS = [
  "GND",
  "P3V3_D",
  "OLED_CS",
  "OLED_SCL",
  "OLED_DC",
  "OLED_SDA",
  "OLED_RES",
] as const

// ---------------------------------------------------------------------------
// Front-panel parts. Panel holes and board footprints both read these.
// ---------------------------------------------------------------------------

export type Pot = { name: string; net: string; label: string; x: number; y: number }

/**
 * 6 pots, 2x3 either side of the display, wired as 0-3.3V dividers across
 * P3V3_A / GND so the mux inputs can never leave the CD4051's 0-3.3V range
 * (froemix/AGENTS.md §4 "Mux input clamps").
 *
 * M_ADC_6 and M_ADC_7 are no longer pots: they carry the UI joystick and the
 * illuminated button (see JOYSTICK / BUTTON). A switch contact is a valid mux
 * input - it is a resistor to GND - so the panel controls cost no extra pins.
 */
export const POTS: Pot[] = [
  { name: "RV1", net: "M_ADC_0", label: "P1", x: -43, y: 45 },
  { name: "RV2", net: "M_ADC_1", label: "P2", x: -28, y: 45 },
  { name: "RV3", net: "M_ADC_2", label: "P3", x: 28, y: 45 },
  { name: "RV4", net: "M_ADC_3", label: "P4", x: 43, y: 45 },
  { name: "RV5", net: "M_ADC_4", label: "P5", x: -43, y: 25 },
  { name: "RV6", net: "M_ADC_5", label: "P6", x: 43, y: 25 },
]

/**
 * UI joystick: 5 momentary contacts (up, down, left, right, press) on ONE mux
 * channel, M_ADC_6, as a resistor ladder. Each direction connects a different
 * resistor from the channel node to GND; a 10k pull-up to P3V3_A sets the idle
 * level. Levels seen by the ADC (3.3V rail, ignoring the CD4051's ~200R):
 *
 *   idle 3.300V  press 0.000V  up 0.300V  down 0.595V  left 1.055V  right 1.650V
 *
 * 12-bit ADC: the closest pair is press/up at ~370 counts apart. The four
 * resistor values are all LCSC basic parts already used on the base board
 * (1k C21190, 2.2k C4190, 4.7k C23162, 10k C25804), so the ladder adds no new
 * sourcing question.
 */
export const JOYSTICK = {
  name: "SW1",
  net: "M_ADC_6",
  label: "UI",
  x: -28,
  y: 25,
  pullup: "10k",
  /** direction -> series resistor to GND */
  ladder: [
    { dir: "up", r: "1k", mpn: "0603WAF1001T5E", lcsc: "C21190" },
    { dir: "down", r: "2.2k", mpn: "0603WAF2201T5E", lcsc: "C4190" },
    { dir: "left", r: "4.7k", mpn: "0603WAF4701T5E", lcsc: "C23162" },
    { dir: "right", r: "10k", mpn: "0603WAF1002T5E", lcsc: "C25804" },
    { dir: "press", r: "0", mpn: "", lcsc: "" }, // direct to GND, no part
  ],
} as const

/**
 * Illuminated button on M_ADC_7. The button's own LED doubles as the pull-up:
 *
 *   P3V3_A -- 1k -- LED(A) -- LED(K) -- node(M_ADC_7) -- switch -- GND
 *
 * Not pressed: no current path, the LED is dark and the node floats up to 3.3V
 * through the 1k + LED leakage, so the ADC reads full scale.
 * Pressed: the node is 0V and the LED lights at (3.3 - 2.0)/1k = 1.3mA.
 * The button therefore shows its own state with no GPIO and no firmware.
 */
export const BUTTON = {
  name: "SW2",
  net: "M_ADC_7",
  label: "MODE",
  x: 28,
  y: 25,
  seriesR: "1k",
} as const

export type Jack = {
  name: string
  /** tip (mono and TRS) */
  net: string
  /** ring, TRS MIDI jacks only */
  ringNet?: string
  label: string
  kind: "mono" | "trs"
  x: number
  y: number
}

/**
 * 10 mono jacks (PJ398SM) + 2 stereo TRS jacks (PJ366ST) for MIDI. Two rows of
 * six, 16.8mm pitch - a 10mm jack nut leaves 6.8mm between nuts.
 */
export const JACKS: Jack[] = [
  // row 1 (upper)
  { name: "J3", net: "J_AUDIO_IN_L", label: "AUDIO IN L", kind: "mono", x: -42, y: -14 },
  { name: "J4", net: "J_AUDIO_IN_R", label: "AUDIO IN R", kind: "mono", x: -25.2, y: -14 },
  { name: "J5", net: "J_AUDIO_OUT_L", label: "AUDIO OUT L", kind: "mono", x: -8.4, y: -14 },
  { name: "J6", net: "J_AUDIO_OUT_R", label: "AUDIO OUT R", kind: "mono", x: 8.4, y: -14 },
  { name: "J7", net: "J_SYNC_IN", label: "SYNC IN", kind: "mono", x: 25.2, y: -14 },
  { name: "J8", net: "J_SYNC_OUT", label: "SYNC OUT", kind: "mono", x: 42, y: -14 },
  // row 2 (lower)
  { name: "J9", net: "J_CV_IN_1", label: "CV IN 1", kind: "mono", x: -42, y: -47 },
  { name: "J10", net: "J_CV_IN_2", label: "CV IN 2", kind: "mono", x: -25.2, y: -47 },
  { name: "J11", net: "J_CV_OUT_1", label: "CV OUT 1", kind: "mono", x: -8.4, y: -47 },
  { name: "J12", net: "J_CV_OUT_2", label: "CV OUT 2", kind: "mono", x: 8.4, y: -47 },
  { name: "J13", net: "J_MIDI_IN_TIP", ringNet: "J_MIDI_IN_RING", label: "MIDI IN", kind: "trs", x: 25.2, y: -47 },
  { name: "J14", net: "J_MIDI_OUT_TIP", ringNet: "J_MIDI_OUT_RING", label: "MIDI OUT", kind: "trs", x: 42, y: -47 },
]

export type JackLed = {
  name: string
  /** the jack net this LED watches */
  net: string
  /** net-name stem; the chain uses `${base}_D` (diode anode) and `${base}_K` (LED anode) */
  base: string
  x: number
  y: number
}

/**
 * Signal-activity LEDs on the six jack signals that matter most. Zero pins, no
 * firmware: the jack signal itself drives the LED through a diode (positive
 * excursions only), a 2.2k resistor and a 3mm LED.
 *
 *   jacknet -- |< -- 2.2k -- LED -- GND
 *
 * At 5V peak: (5 - 0.7 - 2.0)/2.2k = 1.0mA. The series diode keeps the reverse
 * half-cycle off the LED.
 */
export const JACK_LEDS: JackLed[] = [
  { name: "DL1", net: "J_AUDIO_IN_L", base: "INL", x: -42, y: -3 },
  { name: "DL2", net: "J_AUDIO_IN_R", base: "INR", x: -25.2, y: -3 },
  { name: "DL3", net: "J_AUDIO_OUT_L", base: "OUTL", x: -8.4, y: -3 },
  { name: "DL4", net: "J_AUDIO_OUT_R", base: "OUTR", x: 8.4, y: -3 },
  { name: "DL5", net: "J_CV_OUT_1", base: "CV1", x: -8.4, y: -30 },
  { name: "DL6", net: "J_CV_OUT_2", base: "CV2", x: 8.4, y: -30 },
]

/**
 * 1.3" 128x64 SPI OLED (SH1106/SSD1306 module, 35.5 x 33mm PCB, 7-pin 2.54
 * header). The module is soldered flat to the TOP of the board on a 1x7
 * footprint, directly under a 30 x 15.5mm window in the panel. It is not
 * plugged into CN4: CN4 is the base board's OLED header, and the 7 signals are
 * wired straight across from the mating socket to the module - that keeps the
 * module at panel height instead of 11mm further back.
 */
export const OLED = {
  // module origin = centre of the 7-pin header row
  pinPitch: 2.54,
  pins: 7,
  bodyW: 35.5,
  bodyH: 33.0,
  windowW: 30.0,
  windowH: 15.5,
  windowOffsetY: -16.5, // window centre, relative to the header row
} as const

/** Header row centre; the module body hangs below it. */
export const OLED_AT: Pt = { x: 0, y: 57.0 }

/** Panel window centre (the glass), derived from the module position. */
export const OLED_WINDOW: Pt = {
  x: OLED_AT.x,
  y: OLED_AT.y + OLED.windowOffsetY,
}

/** Panel indicator LED (3mm THT) on the base board's `LED` net + series R. */
export const LED_AT: Pt = { x: -30, y: 57.0 }
export const LED_R_AT: Pt = { x: -34.5, y: 56.0 }

/** Panel hole diameters (bushing + clearance). */
export const HOLE = {
  jack: 6.2, // PJ398SM / PJ366ST 6.0mm bushing
  pot: 7.2, // Alps 9mm, M7 bushing
  led: 3.2,
  jackLed: 3.2,
  rail: 3.2, // M3
  standoff: 3.2, // M3 clear
  button: 12.2, // 12mm illuminated tactile
  joy: 7.0, // clears the 6.5mm collar; the 3.2mm stem and its knob sit above
} as const

/** Board-to-panel distance at which the panel sits on the pot/jack bushings. */
export const PANEL_GAP = 9.0
/** Euro board to base board spacing (M3 spacers, 2.00mm socket stack height). */
export const BASE_GAP = 12.0

/**
 * Footprint origins vs panel-hole positions.
 *  - the PJ398SM mono jack's origin is its sleeve pad in the KiCad library, but
 *    the footprint in lib/footprints.tsx is normalised to the barrel, so no
 *    offset is needed;
 *  - the PJ366ST TRS jack, both pots and the LEDs are authored with the origin
 *    on their axis.
 */
export const JACK_MONO_ORIGIN = { x: 0, y: 0 } // footprint origin IS the barrel
export const POT_ORIGIN = { x: 0, y: 0 } // rotated footprint, origin = shaft

/** Place a part whose footprint origin is off its panel hole. */
export const atHole = (hole: Pt, offset = { x: 0, y: 0 }): Pt => ({
  x: hole.x + offset.x,
  y: hole.y + offset.y,
})

/** Every part that needs a hole in the panel. Used by check_mating.py. */
export const frontPanelParts = () => [
  ...POTS.map((p) => ({ ...p, kind: "pot" as const })),
  ...JACKS.map((j) => ({ ...j, kind: "jack" as const })),
  ...JACK_LEDS.map((l) => ({ ...l, kind: "jackLed" as const })),
  { ...JOYSTICK, kind: "joystick" as const },
  { ...BUTTON, kind: "button" as const },
]

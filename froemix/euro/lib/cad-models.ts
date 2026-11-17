// 3D models for the front board's parts. Every component carries a cadModel;
// this module centralises where the models come from.
//
// PROVENANCE / LICENSE.
//
// - The models fetched from modelcdn.tscircuit.com/easyeda_models below are
//   from the JLCEDA/EasyEDA Official Library. Copyright and related rights in
//   that library (device parameters, schematic symbols, PCB footprints, 3D
//   models, modules and related data) belong to JLCEDA/EasyEDA. Source
//   declaration required by that library's terms: "JLCEDA/EasyEDA Official
//   Library", https://lceda.cn/ and https://easyeda.com. Commercial use in
//   circuit designs is permitted; the copyright and this source notice must
//   not be removed or obscured.
// - The two jack models are NOT EasyEDA: clacktronics/AudioJacks (MIT), held
//   in library/cad/jack-35-wqp518ma/ and library/cad/jack-35-wqp419gr/ with
//   SOURCE.md provenance (URL, retrieval date, SHA-256, licence).
// - The 1x7 socket (C22438139) and the OLED module (C42432559) have NO vendor
//   CAD anywhere reachable (no EasyEDA component exists for either LCSC code).
//   They get drawing-derived ENVELOPE models in cad/*.obj: dimensioned boxes,
//   not vendor geometry. The OLED envelope is the module PCB only - the
//   glass/bezel Z is not vendor-documented (doc/MECHANICAL-STACK.md) and stays
//   a first-article measurement.
//
// Every model is a rendering aid. Mechanical acceptance comes from
// lib/mechanical.ts's numbers and first-article measurements, never from a
// STEP file.
//
// MODEL URLS MUST BE ABSOLUTE http(s). The viewer fetches each model relative
// to the app origin; the dev server does NOT serve project files on that route
// (verified 2026-11-17: /cad/*.obj -> 404 in the browser, while the IDE's
// /api/files/get source API is a different endpoint that model loading does not
// use). The envelope OBJs therefore live in froemix/euro/cad/ and are fetched
// from this repo's raw URLs, so an edited envelope only re-renders after the
// commit is pushed (GitHub raw can 404 for a short propagation window on a
// fresh file).
//
// The Thonkiconn jack vendor STEPs (clacktronics/AudioJacks, MIT, commit
// 14a88866 - the same files midibrain pins) are held with provenance in
// library/cad/jack-35-wqp518ma/ and library/cad/jack-35-wqp419gr/ but DO NOT
// MESH in the tscircuit 3D viewer (they fetch with 200 yet render nothing,
// while every EasyEDA STEP in the same scene renders). The viewer therefore
// gets envelope OBJs for the jacks too: body box 9.0 x 10.45mm from the library
// CAD F.Fab outline, bushing Ø6.4 from the STEP's own cylinder radii, body top
// 6.5, shoulder 9.0 + 0.6mm thread stub.

/** EasyEDA/JLCEDA model for an LCSC C-number (verified reachable 2026-11-16). */
export const easyedaStep = (code: string) =>
  `https://modelcdn.tscircuit.com/easyeda_models/assets/${code}.step`
export const easyedaObj = (code: string) =>
  `https://modelcdn.tscircuit.com/easyeda_models/assets/${code}.obj`

// MEASURED MODEL TRANSFORMS (tsci import, 2026-11-17). tsci import reports the
// pcbRotationOffset and modelOriginPosition each EasyEDA model needs relative to
// the EasyEDA footprint; these are those numbers, adapted where this project's
// footprint frame differs from EasyEDA's:
//   - pot (RK09K): EasyEDA's pads are at y=-3.10 and lugs at +3.90; ours are at
//     -7.50 / -0.50 (= their frame shifted by +4.40), so the y origin moves by
//     -4.40. The lug positions cross-check the shift exactly.
//   - WS-1004: EasyEDA's rows are 3.25/0.45/-3.25; ours are 2.80/0/-3.70 (= a
//     -0.45 shift), so the y origin moves by -0.45.
//   K6, LED, diode, resistor and the 1x16 socket share EasyEDA's frame; their
//   numbers are copied unchanged. Pads-only differences (e.g. the resistor
//   pitch 1.5067 vs our 1.575) do not affect the model frame.
// Without these the parts render rotated or sunk/floating - the first import
// pass had SW2 at the wrong rotation and its base below the board, and the pots
// offset 4mm.

const cadModel = (code: string, pcbRotationOffset: number, x: number, y: number, z: number) => ({
  objUrl: easyedaObj(code),
  stepUrl: easyedaStep(code),
  pcbRotationOffset,
  modelOriginPosition: { x, y, z },
})

/** RK09K pot, footprint = our shaft-origin R90 variant. */
export const POT_MODEL = cadModel("C470294", 0, -0.0000127, -7.1884869, 12.499992)
/** WS-1004 5-way switch, footprint = our row layout. */
export const NAV_SWITCH_MODEL = cadModel("C42377836", 0, 0.0000127, -0.4500127, -3.1493238)
/** K6-1212A02 illuminated button. */
export const BUTTON_MODEL = cadModel("C692901", 270, 0, 0.0000254, -1.9500066)
/** 3mm LED. */
export const LED_3MM_MODEL = cadModel("C440523", 0, 0.0081121, 0.0299974, -0.000007)
/** SOD-323 diode. */
export const SOD323_MODEL = cadModel("C2128", 0, 0, 0.0001143, 0)
/** 0603 resistor. */
export const R0603_MODEL = cadModel("C21190", 90, -0.005, 0, -0.01)
/** HC-PM200 2.00mm 1x16 socket. */
export const SOCKET_2P00_16_MODEL = cadModel("C22373851", 0, -0.0000254, 0.0635, -0.0000056)

/** Drawing-derived envelope boxes (see the provenance note above). */
export const ENVELOPE_1X7_SOCKET =
  "https://raw.githubusercontent.com/cldmnky/electronics/main/froemix/euro/cad/hx-pm200-1x7-envelope.obj"
export const ENVELOPE_OLED_MODULE =
  "https://raw.githubusercontent.com/cldmnky/electronics/main/froemix/euro/cad/hs13l02b4s01-envelope.obj"
export const ENVELOPE_JACK_MONO =
  "https://raw.githubusercontent.com/cldmnky/electronics/main/froemix/euro/cad/jack-mono-envelope.obj"
export const ENVELOPE_JACK_TRS =
  "https://raw.githubusercontent.com/cldmnky/electronics/main/froemix/euro/cad/jack-trs-envelope.obj"

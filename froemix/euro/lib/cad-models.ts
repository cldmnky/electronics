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

/** EasyEDA/JLCEDA model for an LCSC C-number (verified reachable 2026-11-16). */
export const easyedaStep = (code: string) =>
  `https://modelcdn.tscircuit.com/easyeda_models/assets/${code}.step`

/** Drawing-derived envelope boxes (see the provenance note above). */
export const ENVELOPE_1X7_SOCKET = "cad/hx-pm200-1x7-envelope.obj"
export const ENVELOPE_OLED_MODULE = "cad/hs13l02b4s01-envelope.obj"

/** The two Thonkiconn-family jacks: shared library CAD, MIT. */
export const JACK_MONO_STEP = "../../library/cad/jack-35-wqp518ma/PJ301M-12.step"
export const JACK_TRS_STEP = "../../library/cad/jack-35-wqp419gr/PJ366ST.step"

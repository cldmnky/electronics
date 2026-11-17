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
// WHY SOME URLS POINT AT RAW GITHUB: the tscircuit 3D view fetches models over
// http(s) - relative/local file paths are NOT served by the dev app (verified
// 2026-11-17: /cad/*.obj and ../../library/... both 404 from the dev server),
// so the viewer showed no jack or envelope models. The jack STEPs stay in
// library/cad/ as the provenance copies (SOURCE.md, SHA-256) and are fetched
// from the same upstream commit midibrain pins; the envelope OBJs live in this
// repo (froemix/euro/cad/) and are fetched from the raw URL of the tracked
// path. If a file is edited, its raw URL keeps working once the commit is
// pushed.

/** EasyEDA/JLCEDA model for an LCSC C-number (verified reachable 2026-11-16). */
export const easyedaStep = (code: string) =>
  `https://modelcdn.tscircuit.com/easyeda_models/assets/${code}.step`

/** Drawing-derived envelope boxes (see the provenance note above). */
export const ENVELOPE_1X7_SOCKET =
  "https://raw.githubusercontent.com/cldmnky/electronics/main/froemix/euro/cad/hx-pm200-1x7-envelope.obj"
export const ENVELOPE_OLED_MODULE =
  "https://raw.githubusercontent.com/cldmnky/electronics/main/froemix/euro/cad/hs13l02b4s01-envelope.obj"

/** The two Thonkiconn-family jacks. The vendor STEPs (clacktronics/AudioJacks,
 * MIT, commit 14a88866 - the same files midibrain pins) are held with provenance
 * in library/cad/jack-35-wqp518ma/ and library/cad/jack-35-wqp419gr/ but DO NOT
 * MESH in the tscircuit 3D viewer (verified 2026-11-17: the files fetch with
 * 200 yet no geometry appears, while every EasyEDA STEP renders). The viewer
 * therefore gets envelope OBJs: body box 9.0 x 10.45mm from the library CAD
 * F.Fab outline, bushing Ø6.4 from the STEP's own cylinder radii, body top 6.5
 * and 9.0 shoulder + 0.6mm thread stub. */
export const ENVELOPE_JACK_MONO =
  "https://raw.githubusercontent.com/cldmnky/electronics/main/froemix/euro/cad/jack-mono-envelope.obj"
export const ENVELOPE_JACK_TRS =
  "https://raw.githubusercontent.com/cldmnky/electronics/main/froemix/euro/cad/jack-trs-envelope.obj"

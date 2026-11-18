// 3.5 mm stereo TRS jack for the four MIDI sockets. Preferred part:
// QingPu/Thonkiconn WQP419GR (M6 x 0.5 bushing, PCB mount, 3 terminals).
// Origin is the barrel/panel-hole centre.
//
// PIN MAP, verified 2026-11-13 against clacktronics/AudioJacks (MIT):
// AudioJacks.pretty/Jack_3.5mm_QingPu_WQP-PJ366ST_Vertical.kicad_mod, which
// lists S at 6.48, R at 3.38 and T at -4.92 mm from the barrel datum in KiCad
// (Y-down) coordinates. Transcribed to tscircuit (Y-up) that is exactly the
// three pads below: pin1 = S at -6.48, pin2 = R at -3.38, pin3 = T at +4.92.
// Pads are therefore labelled by their electrical function - S / R / T - and
// the connections in front.circuit.tsx use those letters, not numbers.
//
// The vendor drawing (QingPu "Thonkiconn Stereo (WQP419GR)", PCB LAYOUT TOP
// VIEW) puts the three pads at 4.92 / 3.38 / 6.48 mm from the barrel datum,
// spacings 8.30 and 3.10, with blades 0.5 x 1.5 (pin 3), 0.6 x 1.5 (pin 2) and
// 0.6 x 1.3 (pin 1); its schematic marks pin 1 as the SLEEVE. The Clacktronics
// footprint and its named STEP solids (tip at +4.92, ring at -3.38, sleeve at
// -6.48) give the rest: 1 = sleeve, 2 = ring, 3 = tip. That numbering was
// recorded the other way round until 2026-11-18; the drawing does not letter
// pins 2 and 3, so the tip/ring assignment still rests on that CAD naming until
// a delivered jack is buzzed out (docs/TODO.md, `jack_retention`). The pad
// names S / R / T below are the authority here: numbering the pads 1/2/3 is
// what invited the tip/sleeve swap this note exists to prevent.
//
// 3D: PJ366ST.step from the same repository (MIT, SHA-256 in
// library/cad/jack-35-wqp419gr/SOURCE.md), pinned to commit 14a88866. That
// library's own README says it is not yet complete and its parts are untested,
// so the model is a rendering aid, not a mechanical proof.
export const FP_JACK_TRS = (
  <footprint insertionDirection="from_above">
    {/* Slots, not round holes: the terminals are flat blades. The QingPu
        "Thonkiconn Stereo (WQP419GR)" drawing sizes them 0.5 x 1.5, 0.6 x 1.5
        and 0.6 x 1.3 mm with a +/-0.15 mm general tolerance, so the library
        CAD's 1.6 x 0.6 mm obround has no width clearance for the 0.6 mm ring
        and sleeve blades and its rounded ends do not contain the blade
        corners. The slots are 2.4 x 1.2 mm with lands enlarged to keep
        >=0.3 mm nominal rings - the same dimensions froemix shipped after the
        same finding (froemix/euro/lib/footprints.tsx, 2026-11-17). Finished
        slot fit is still a first-article check: plating and routing tolerance
        are not modelled here.

        The body outline and barrel ring that used to be drawn here were the
        retired part's; both crossed the new lands, and the WQP419GR body is a
        different shape (about 10 x 7.8 mm with the barrel 4.5 mm from one
        edge), so nothing is drawn rather than something wrong. The board silk
        still carries the refdes, the THT hint and the pin-1 dot. */}
    <platedhole pcbX="0mm" pcbY="-6.48mm" portHints={["pin1"]} shape="oval" holeWidth="2.4mm" holeHeight="1.2mm" outerWidth="3.1mm" outerHeight="2.1mm" />
    <platedhole pcbX="0mm" pcbY="-3.38mm" portHints={["pin2"]} shape="oval" holeWidth="2.4mm" holeHeight="1.2mm" outerWidth="3mm" outerHeight="2.6mm" />
    <platedhole pcbX="0mm" pcbY="4.92mm" portHints={["pin3"]} shape="oval" holeWidth="2.4mm" holeHeight="1.2mm" outerWidth="3mm" outerHeight="2.6mm" />
    <courtyardrect pcbX="0mm" pcbY="-0.165mm" width="10mm" height="14.83mm" />
  </footprint>
)

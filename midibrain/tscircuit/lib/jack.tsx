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
// The vendor drawing numbers the terminals 1 = sleeve, 2 = tip, 3 = ring, and
// the KiCad footprint's pad *names* (S/R/T) are the authority here; assigning
// numbers to the three pads invites the tip/sleeve swap this note exists to
// prevent. A 3 mm barrel keepout (the drawing's "PCB HOLE" note) is marked on
// the Clacktronics footprint but is NOT cut here: the jack sits near the board
// edge and no clearance to the outline has been checked.
//
// 3D: PJ366ST.step from the same repository (MIT, SHA-256 in
// library/cad/jack-35-wqp419gr/SOURCE.md), pinned to commit 14a88866. That
// library's own README says it is not yet complete and its parts are untested,
// so the model is a rendering aid, not a mechanical proof.
export const FP_JACK_TRS = (
  <footprint insertionDirection="from_above">
    {/* Slots, not round holes: the terminals are flat blades. Thonk's own PCB
        layout ("Thonkiconn Stereo WQP419GR", "PCB LAYOUT (TOP VIEW)") sizes them
        0.5 x 1.5, 0.6 x 1.5 and 0.6 x 1.3 mm, so the hole must be wider than
        1.5 mm in one direction and does not need 2 mm in the other. The retired
        PJ-301M-3-B pads were a 1.2 x 2.0 mm oval, which a 1.5 mm blade cannot
        enter. 1.6 x 0.6 mm with a 2.6 x 2.6 land (sleeve 3.1 x 2.1) is the
        library CAD's table (library/cad/jack-35-wqp419gr) and matches the
        drawing's blade sizes.

        The body outline and barrel ring that used to be drawn here were the
        retired part's; both crossed the new lands, and the WQP419GR body is a
        different shape (about 10 x 7.8 mm with the barrel 4.5 mm from one
        edge), so nothing is drawn rather than something wrong. The board silk
        still carries the refdes, the THT hint and the pin-1 dot. */}
    <platedhole pcbX="0mm" pcbY="-6.48mm" portHints={["pin1"]} shape="oval" holeWidth="1.6mm" holeHeight="0.6mm" outerWidth="3.1mm" outerHeight="2.1mm" />
    <platedhole pcbX="0mm" pcbY="-3.38mm" portHints={["pin2"]} shape="oval" holeWidth="1.6mm" holeHeight="0.6mm" outerWidth="2.6mm" outerHeight="2.6mm" />
    <platedhole pcbX="0mm" pcbY="4.92mm" portHints={["pin3"]} shape="oval" holeWidth="1.6mm" holeHeight="0.6mm" outerWidth="2.6mm" outerHeight="2.6mm" />
    <courtyardrect pcbX="0mm" pcbY="-0.165mm" width="10mm" height="14.83mm" />
  </footprint>
)

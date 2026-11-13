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
    <platedhole pcbX="0mm" pcbY="-6.48mm" portHints={["pin1"]} shape="oval" holeWidth="1.2mm" holeHeight="2mm" outerWidth="2mm" outerHeight="2.6mm" />
    <platedhole pcbX="0mm" pcbY="-3.38mm" portHints={["pin2"]} shape="oval" holeWidth="1.2mm" holeHeight="2mm" outerWidth="2mm" outerHeight="2.6mm" />
    <platedhole pcbX="0mm" pcbY="4.92mm" portHints={["pin3"]} shape="oval" holeWidth="1.2mm" holeHeight="2mm" outerWidth="2mm" outerHeight="2.6mm" />
    <silkscreenpath route={[{ x: -4.5, y: -4.47 }, { x: 4.5, y: -4.47 }]} />
    <silkscreenpath route={[{ x: -4.5, y: -4.47 }, { x: -4.5, y: 5.98 }]} />
    <silkscreenpath route={[{ x: 4.5, y: -4.47 }, { x: 4.5, y: 5.98 }]} />
    <silkscreenpath route={[{ x: -4.5, y: 5.98 }, { x: 4.5, y: 5.98 }]} />
    <silkscreencircle pcbX="0mm" pcbY="0mm" radius="3mm" />
    <courtyardrect pcbX="0mm" pcbY="-0.165mm" width="10mm" height="14.83mm" />
  </footprint>
)

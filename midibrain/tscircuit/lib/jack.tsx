// 3.5 mm stereo TRS jack, XKB PJ-301M-3-B, same part/footprint as Froemix.
// Origin is the barrel/panel-hole centre. pin1 = tip, pin2 = ring, pin3 = sleeve.
export const FP_JACK_TRS = (
  <footprint insertionDirection="from_above">
    <platedhole pcbX="0mm" pcbY="4.92mm" portHints={["pin1"]} shape="oval" holeWidth="1.2mm" holeHeight="2mm" outerWidth="2mm" outerHeight="2.6mm" />
    <platedhole pcbX="0mm" pcbY="-3.38mm" portHints={["pin2"]} shape="oval" holeWidth="1.2mm" holeHeight="2mm" outerWidth="2mm" outerHeight="2.6mm" />
    <platedhole pcbX="0mm" pcbY="-6.48mm" portHints={["pin3"]} shape="oval" holeWidth="1.2mm" holeHeight="2mm" outerWidth="2mm" outerHeight="2.6mm" />
    <silkscreenpath route={[{ x: -4.5, y: -4.47 }, { x: 4.5, y: -4.47 }]} />
    <silkscreenpath route={[{ x: -4.5, y: -4.47 }, { x: -4.5, y: 5.98 }]} />
    <silkscreenpath route={[{ x: 4.5, y: -4.47 }, { x: 4.5, y: 5.98 }]} />
    <silkscreenpath route={[{ x: -4.5, y: 5.98 }, { x: 4.5, y: 5.98 }]} />
    <silkscreencircle pcbX="0mm" pcbY="0mm" radius="3mm" />
    <courtyardrect pcbX="0mm" pcbY="-0.165mm" width="10mm" height="14.83mm" />
  </footprint>
)

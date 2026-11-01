// Froemix base board - hand-maintained tscircuit source (see froemix/AGENTS.md).
// Originally ported from the KiCad project in ../tmp; that port is frozen.

import { MainSection } from "./sections/main"
import { MidiSection } from "./sections/midi"
import { AudioSection } from "./sections/audio"
import { CvSection } from "./sections/cv"
import { PowerSection } from "./sections/power"
import { Nets } from "./sections/nets"
import { FabricationSection } from "./sections/fabrication"
import { SilkSection } from "./sections/silk"

// Froemix base board - Daisy Seed based Eurorack mixer / DSP module
// Board outline carried over from froemix.kicad_pcb: 80.9 x 128.5mm (16HP x 3U)
// 4 layers like the KiCad board (signal / GND / PWR / signal).
// Width policy: the board floor is 0.25mm (stricter than the 0.15mm signal
// minimum), so no copper is thinner than 0.25mm - the autorouter only honors a
// per-net nominal width and falls back to the board minimum, so a lower board
// floor would let GND/power rails drop to 0.15mm. Signal nets are nominal
// 0.25mm, power/GND/reset 0.25-0.6mm (see sections/nets.tsx).
export default () => (
  <board width="80.9mm" height="128.5mm" layers={4} autorouter="auto-local"
    defaultTraceWidth="0.25mm" minTraceWidth="0.25mm" minTraceToPadEdgeClearance="0.1mm"
    minViaHoleDiameter="0.2mm" minViaPadDiameter="0.45mm" minBoardEdgeClearance="0.4mm"
    routeRemaining={false}>
    <Nets />
    {/* one global phase: split phases leave unrouted ports on this board */}
    <autoroutingphase phaseIndex={0} />
    {/* M3 mounting holes, 3.5mm from each board edge */}
    <hole diameter="3.2mm" pcbX="-36.95mm" pcbY="60.75mm" />
    <hole diameter="3.2mm" pcbX="36.95mm" pcbY="60.75mm" />
    <hole diameter="3.2mm" pcbX="-36.95mm" pcbY="-60.75mm" />
    <hole diameter="3.2mm" pcbX="36.95mm" pcbY="-60.75mm" />
    <MainSection />
    <MidiSection />
    <AudioSection />
    <CvSection />
    <PowerSection />
    <FabricationSection />
    <SilkSection />
    {/* GND plane on the bottom: 0.3mm to every pad/trace, 0.5mm to the edge
        (the board minimum is 0.4mm). See AGENTS.md §3 for the holeOffsetX
        prerequisite that keeps this from crashing the build. */}
    <copperpour connectsTo="net.GND" layer="bottom" clearance="0.3mm" boardEdgeMargin="0.5mm" />
  </board>
)

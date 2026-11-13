// ONE STACKED VIEW OF THE ASSEMBLED MODULE.
//
// The owner asked to see the 30HP panel, the 150 mm front/UI board and the
// 148 mm rear/base board together, at their real relative Z, so the stack can be
// inspected as one thing. This file is that view: a single `<assembly.device>`
// whose 3D output (`bunx tsci export assembly.circuit.tsx -f glb`, or
// `tsci build --3d-png`) carries all three bodies.
//
// WHY THE PANEL AND THE REAR BOARD ARE NOT `<board>` ELEMENTS HERE. Measured on
// this tscircuit version (0.0.3169) with throwaway probe circuits and the
// exported GLB, not assumed:
//
//   * A `<board>` cannot be nested in another board. `<mountedboard>` is the
//     same construction with a wrapper and fails identically:
//       Fatal error [circuit_generation_failed]:
//       Nested boards are not supported: found board "unnamed_board1" inside
//       board "unnamed_board1"
//     `mountedboard` also has no page in the current docs nav
//     (https://docs.tscircuit.com/elements/mountedboard returns 404).
//
//   * Several `<board>`s DO build and render inside one `<assembly.device>`
//     (https://docs.tscircuit.com/elements/assembly-device,
//     https://docs.tscircuit.com/guides/advanced-assembly), but every board's
//     centre plane is pinned to Z = 0: pcbX/pcbY/pcbRotation move a board in
//     plan only, and a board carrying `mountedTo` does not move at all - the
//     mounted PART moves to meet it instead ("Mounting keeps the PCB in the XY
//     plane at Z = 0", https://docs.tscircuit.com/guides/advanced-assembly, and
//     "A connected assembly can have one PCB anchor"). Probe: three boards with
//     pcbY offsets 0 / 0 / -30 mm all rendered with worldY -0.80..0.80.
//
// So the Z stack from PROVISIONAL_Z is expressed the one way the platform
// supports: the front/UI board is the single real `<board>` (its centre plane
// is the assembly's Z = 0), and the panel and the rear/base board are
// `<assembly.printedpart>` reference cages at their layout.ts Z. Those are
// mechanical bodies: no pads, no footprint, no schematic symbol, so this file
// contributes no BOM and no placement.
//
// REFERENCE ENVELOPE, NOT A FIT MODEL. Each cage is the board's outline and
// thickness from lib/layout.ts, drawn as twelve 0.5 mm rods so the parts inside
// stay visible; the panel's LCD aperture is cut out of it. Nothing here is a
// substitute for mechanical/step_assembly.py, which composes the same stack as
// real STEP solids and is what the mechanical checks measure - this file is the
// inspection view inside tscircuit's own 3D renderer.
import { assembly } from "tscircuit"
import FrontBoard from "./front.circuit"
import {
  frontBoardUpZ,
  PANEL,
  PANEL_LCD_CUTOUT,
  PROVISIONAL_Z,
  REAR_PCB,
} from "./lib/layout"

// The frame here is the front/UI board's own (lib/layout.ts frontBoardUpZ): the
// front board is this assembly's anchor, so it is the board whose mid-plane is
// the assembly's Z = 0.
const ROD = 0.5
type JscadShape = Record<string, unknown>

// Twelve rods along a box's edges: a wireframe of the board outline that leaves
// the board behind it readable.
const wireframe = (width: number, height: number, thickness: number): JscadShape => {
  const hx = width / 2 - ROD / 2
  const hy = height / 2 - ROD / 2
  const hz = thickness / 2 - ROD / 2
  const shapes: JscadShape[] = []
  const rod = (size: number[], at: number[]) => shapes.push({
    type: "translate",
    vector: at,
    shape: { type: "cuboid", size },
  })
  for (const sy of [-1, 1]) for (const sz of [-1, 1]) rod([width, ROD, ROD], [0, sy * hy, sz * hz])
  for (const sx of [-1, 1]) for (const sz of [-1, 1]) rod([ROD, height, ROD], [sx * hx, 0, sz * hz])
  for (const sx of [-1, 1]) for (const sy of [-1, 1]) rod([ROD, ROD, thickness], [sx * hx, sy * hy, 0])
  return { type: "union", shapes }
}

// A board-shaped cage between two panel-Z planes, centred on the same X/Y as the
// front board (all three are concentric in the module).
const boardCage = (
  width: number, height: number, zFront: number, zRear: number,
): JscadShape => ({
  type: "translate",
  vector: [0, 0, (frontBoardUpZ(zFront) + frontBoardUpZ(zRear)) / 2],
  shape: wireframe(width, height, Math.abs(frontBoardUpZ(zRear) - frontBoardUpZ(zFront))),
})

// The panel's display window, so the stack view shows where the module looks
// through. Cut 4x the panel thickness deep so it passes through the cage; the
// corner radius is squared off, because this is an inspection view.
const lcdWindow = (): JscadShape => ({
  type: "translate",
  vector: [PANEL_LCD_CUTOUT.x, PANEL_LCD_CUTOUT.y, 0],
  shape: {
    type: "cuboid",
    size: [PANEL_LCD_CUTOUT.width, PANEL_LCD_CUTOUT.height, PANEL.thickness * 4],
  },
})

const panelEnvelope = {
  type: "subtract",
  shapes: [
    boardCage(PANEL.width, PANEL.height, PROVISIONAL_Z.panelFront, PROVISIONAL_Z.panelRear),
    lcdWindow(),
  ],
}

const rearBoardEnvelope = boardCage(
  REAR_PCB.width, REAR_PCB.height, PROVISIONAL_Z.rearBoardFront, PROVISIONAL_Z.rearBoardRear,
)

export default () => (
  <assembly.device name="MIDI_BRAIN_STACK">
    {/* The front board arrives with its own two printed parts (PRINT_NAV_KEYTOP
        and PRINT_KNOB); they sit on the panel's front face, in front of this
        stack, so they are not repeated here. */}
    <FrontBoard />
    <assembly.printedpart
      name="PANEL_30HP"
      color="#9aa4ad"
      cadModel={{
        jscad: panelEnvelope,
        positionOffset: { x: 0, y: 0, z: 0 }, // concentric with the front board
        showAsTranslucentModel: true,
      }}
    />
    <assembly.printedpart
      name="REAR_BASE_PCB"
      color="#3f7a5a"
      cadModel={{
        jscad: rearBoardEnvelope,
        positionOffset: { x: 0, y: 0, z: 0 }, // concentric with the front board
        showAsTranslucentModel: true,
      }}
    />
  </assembly.device>
)

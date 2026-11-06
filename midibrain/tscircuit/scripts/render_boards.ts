// Regenerate the in-repo board renders (midibrain/assets/*.png) from the
// current sources, so silkscreen and placement changes are reviewable without
// opening the project in `tsci dev`.
//
//   cd midibrain/tscircuit && bun run render:boards
//
// Two artefacts per board, both generated and both safe to delete:
//   assets/front-pcb.svg / .png   front/UI board, viewed from the front
//   assets/base-pcb.svg  / .png   rear/base board, viewed from the REAR
// The rear board is flipped horizontally: every part and every silkscreen mark
// is on its bottom layer, and `tsci export -f pcb-svg` draws the bottom layer
// as seen through the board, so an unflipped render prints the wordmark
// mirrored. The flip matches the view a builder has when working on the rear
// board, which is what the silkscreen hints are for.
// The SVGs are gitignored (regenerable); the PNGs are tracked because the
// README links them. These are routed prototype renders, not fabrication
// output: `make order` / `make release` own the fab/ exports.
import { mkdirSync } from "node:fs"
import { dirname, join } from "node:path"
import sharp from "sharp"

const ROOT = join(import.meta.dir, "..", "..")
const ASSETS = join(ROOT, "assets")
const SVG_WIDTH = 800
const SVG_HEIGHT = 600

const BOARDS = [
  { entry: "front.circuit.tsx", svg: "front-pcb.svg", png: "front-pcb.png", viewFrom: "front" },
  { entry: "index.circuit.tsx", svg: "base-pcb.svg", png: "base-pcb.png", viewFrom: "rear" },
] as const

mkdirSync(ASSETS, { recursive: true })

for (const board of BOARDS) {
  const svgPath = join(ASSETS, board.svg)
  const pngPath = join(ASSETS, board.png)
  mkdirSync(dirname(pngPath), { recursive: true })
  const exported = Bun.spawnSync({
    cmd: ["bunx", "tsci", "export", board.entry, "-f", "pcb-svg", "-o", svgPath],
    cwd: join(ROOT, "tscircuit"),
    stdout: "pipe",
    stderr: "pipe",
  })
  if (exported.exitCode !== 0) {
    throw new Error(`${board.entry}: pcb-svg export failed\n${exported.stderr.toString()}`)
  }
  const render = sharp(svgPath).resize(SVG_WIDTH, SVG_HEIGHT)
  const viewed = board.viewFrom === "rear" ? render.flop() : render
  await viewed.png().toFile(pngPath)
  console.log(`${board.entry} -> assets/${board.svg}, assets/${board.png} (viewed from the ${board.viewFrom})`)
}

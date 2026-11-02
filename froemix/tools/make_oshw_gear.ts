// Generate a polyline approximation of the gear-only path in OSHWA's SVG.
// Run from froemix/tscircuit: bun ../tools/make_oshw_gear.ts
// Source and attribution: assets/oshw-logo-filled-black.svg and assets/README.md.
import { readFileSync, writeFileSync } from "node:fs"
import SVGPathCommander from "../tscircuit/node_modules/svg-path-commander"

const svg = readFileSync("../assets/oshw-logo-filled-black.svg", "utf8")
const path = [...svg.matchAll(/<path[^>]* d="([^"]+)"/g)][0]?.[1]
if (!path) throw new Error("official gear path missing")
const commands = SVGPathCommander.normalizePath(path)
const out: [number, number][] = []
let x = 0, y = 0
for (const c of commands) {
  if (c[0] === "M" || c[0] === "L") {
    x = c[1]; y = c[2]; out.push([x, y])
  } else if (c[0] === "C") {
    const [x0, y0] = [x, y]
    for (let i = 1; i <= 8; i++) {
      const t = i / 8, q = 1 - t
      out.push([
        q*q*q*x0 + 3*q*q*t*c[1] + 3*q*t*t*c[3] + t*t*t*c[5],
        q*q*q*y0 + 3*q*q*t*c[2] + 3*q*t*t*c[4] + t*t*t*c[6],
      ])
    }
    x = c[5]; y = c[6]
  } else throw new Error(`Unexpected SVG path command ${c[0]}`)
}
// An even-odd scanline fill gives real silkscreen strokes, not an SVG image
// object that the PCB exporter might silently omit. At 7mm wide the 0.15mm
// pitch overlaps the 0.2mm aperture. Gear shape remains in the artwork license.
const points = out.map(([px, py]) => [Number(px.toFixed(3)), Number(py.toFixed(3))])
const source = `// Generated from the OSHW gear by tools/make_oshw_gear.ts; see assets/README.md.\nexport const OSHW_OUTLINE: readonly (readonly [number, number])[] = ${JSON.stringify(points)}\n`
writeFileSync("../shared/oshw-outline.ts", source)
console.log(`Generated ${out.length} outline vertices`)

// Froemix front panel - synthetic inputs for the panel tests.
//
// TEST SUPPORT ONLY: nothing in the board imports this file. It builds a
// circuit JSON and a Gerber/drill archive that mirror what `tsci build` and
// `tsci export --format gerbers` produce, so the checks can be mutation-tested
// (move a hole, thin a stroke, add copper) without re-running a build.

import { readFileSync } from "node:fs"
import { join } from "node:path"
import { artworkPaths, type PanelArtwork } from "./panel-art"
import type { PanelFixture } from "./panel-checks"
import type { ReleaseFile } from "./panel-gerber"

type CjElement = Record<string, any>

const HERE = new URL(".", import.meta.url).pathname

export const loadFixture = (): PanelFixture =>
  JSON.parse(readFileSync(join(HERE, "..", "fixtures", "panel-baseline.json"), "utf8")) as PanelFixture

/** A circuit JSON with the frozen geometry, the rounded window and the artwork. */
export const syntheticPanel = (art: PanelArtwork, fixture = loadFixture()): CjElement[] => {
  const elements: CjElement[] = [
    {
      type: "pcb_board",
      pcb_board_id: "pcb_board_0",
      center: { x: fixture.outline.center.x, y: fixture.outline.center.y },
      width: fixture.outline.width,
      height: fixture.outline.height,
      thickness: fixture.outline.thickness,
      num_layers: 2,
    },
  ]
  fixture.holes.forEach((h, i) => {
    elements.push({
      type: "pcb_hole",
      pcb_hole_id: `pcb_hole_${i}`,
      x: h.x,
      y: h.y,
      hole_diameter: h.diameter,
      hole_shape: "circle",
    })
  })
  elements.push({
    type: "pcb_cutout",
    pcb_cutout_id: "pcb_cutout_0",
    shape: "polygon",
    points: art.window.polygon.map((p) => ({ x: p.x, y: p.y })),
  })
  artworkPaths(art).forEach((p, i) => {
    elements.push({
      type: "pcb_silkscreen_path",
      pcb_silkscreen_path_id: `pcb_silkscreen_path_${i}`,
      layer: "top",
      stroke_width: p.strokeWidth,
      route: p.path.map((q) => ({ x: q.x, y: q.y })),
    })
  })
  return elements
}

const mm = (v: number): string => Math.round(v * 1e6).toString()

/** Gerber body that draws one closed contour per path, as the exporter does. */
const gerberPaths = (paths: { strokeWidth: number; path: { x: number; y: number }[] }[]): string => {
  const widths = [...new Set(paths.map((p) => p.strokeWidth))].sort((a, b) => a - b)
  const apertures = widths.map((w, i) => `%ADD${10 + i}C,${w.toFixed(6)}*%`).join("\n")
  const body = paths
    .map((p) => {
      const code = 10 + widths.indexOf(p.strokeWidth)
      const first = p.path[0]!
      const rest = p.path.slice(1)
      return [
        `D${code}*`,
        `X${mm(first.x)}Y${mm(first.y)}D02*`,
        ...rest.map((q) => `X${mm(q.x)}Y${mm(q.y)}D01*`),
        `X${mm(first.x)}Y${mm(first.y)}D01*`,
      ].join("\n")
    })
    .join("\n")
  return `%FSLAX46Y46*%\n%MOMM*%\nG01*\nG75*\n${apertures}\n${body}\nM02*`
}

/** A panel archive that should pass every release check. */
export const syntheticRelease = (art: PanelArtwork, fixture = loadFixture()): ReleaseFile[] => {
  const w = fixture.outline.width
  const h = fixture.outline.height
  const outline = [
    { x: -w / 2, y: -h / 2 },
    { x: w / 2, y: -h / 2 },
    { x: w / 2, y: h / 2 },
    { x: -w / 2, y: h / 2 },
    { x: -w / 2, y: -h / 2 },
  ]
  const window = [...art.window.polygon, art.window.polygon[0]!]
  const profile = `%FSLAX46Y46*%\n%MOMM*%\n%TF.FileFunction,Profile,NP*%\n%ADD10C,0.050000*%\nG01*\nD10*\n`
    .concat(
      [outline, window]
        .map((contour) => {
          const first = contour[0]!
          return [
            `X${mm(first.x)}Y${mm(first.y)}D02*`,
            ...contour.slice(1).map((q) => `X${mm(q.x)}Y${mm(q.y)}D01*`),
          ].join("\n")
        })
        .join("\n"),
    )
    .concat("\nM02*")

  const holes = fixture.holes
  const tools = [...new Set(holes.map((x) => x.diameter))].sort((a, b) => a - b)
  const drill = [
    "M48",
    "METRIC",
    "; #@! TF.FileFunction,NonPlated,1,2,NPTH",
    ...tools.map((d, i) => `T${10 + i}C${d.toFixed(6)}`),
    "%",
    "G90",
    "G05",
    ...tools.map((d, i) => {
      const code = 10 + i
      const at = holes.filter((x) => x.diameter === d)
      return [`T${code}`, ...at.map((p) => `X${p.x.toFixed(4)}Y${p.y.toFixed(4)}`)].join("\n")
    }),
    "M30",
  ].join("\n")

  const maskFlashes = holes
    .map((p) => `X${mm(p.x)}Y${mm(p.y)}D03*`)
    .join("\n")

  return [
    {
      name: "F_Cu.gbr",
      text: "%FSLAX46Y46*%\n%MOMM*%\n%TF.FileFunction,Copper,L1,Top*%\n%ADD10C,0.100000*%\nG01*\nM02*",
    },
    {
      name: "B_Cu.gbr",
      text: "%FSLAX46Y46*%\n%MOMM*%\n%TF.FileFunction,Copper,L2,Bot*%\n%ADD10C,0.100000*%\nG01*\nM02*",
    },
    {
      name: "F_SilkScreen.gbr",
      text: `%TF.FileFunction,Legend,Top*%\n${gerberPaths(artworkPaths(art))}`,
    },
    {
      name: "F_Mask.gbr",
      text: `%FSLAX46Y46*%\n%MOMM*%\n%TF.FileFunction,Soldermask,Top*%\n%ADD10C,3.200000*%\nG01*\nD10*\n${maskFlashes}\nM02*`,
    },
    {
      name: "B_Mask.gbr",
      text: `%FSLAX46Y46*%\n%MOMM*%\n%TF.FileFunction,Soldermask,Bot*%\n%ADD10C,3.200000*%\nG01*\nD10*\n${maskFlashes}\nM02*`,
    },
    { name: "Edge_Cuts.gbr", text: profile },
    { name: "drill_npth.drl", text: drill },
    { name: "bom.csv", text: "" },
    { name: "pick_and_place.csv", text: "" },
  ]
}

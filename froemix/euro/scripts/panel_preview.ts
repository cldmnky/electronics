#!/usr/bin/env bun
// Froemix front panel - previews and the 1:1 print template.
//
//   bun run panel:preview                        render the built panel into dist/panel-release/preview
//   bun run panel:preview --out panel            write into a folder you intend to commit
//   bun run panel:preview --variants             build and render every design
//   bun run panel:preview --circuit <file> --design signal   render a saved build, checking the design
//
// Reads the BUILT circuit JSON. Nothing here draws from a second copy of the
// artwork, and nothing loads a font, an image, a script or the network, so the
// output is the same offline.
//
// The design is resolved from the printed geometry itself
// (`resolvePanelDesign`), never assumed: a signal build cannot be written out
// under a nord filename.

import { spawnSync } from "node:child_process"
import { existsSync, mkdirSync, mkdtempSync, readFileSync, writeFileSync } from "node:fs"
import { tmpdir } from "node:os"
import { join } from "node:path"
import { PANEL_DESIGN_IDS, panelArtwork, type PanelDesignId } from "../lib/panel-art"
import { resolvePanelDesign, type Problem } from "../lib/panel-checks"
import { renderPanelPreviewSvg, renderPreviewHtml, renderPrintTemplateSvg } from "../lib/panel-render"

const HERE = new URL(".", import.meta.url).pathname
const ROOT = join(HERE, "..")
const DEFAULT_CIRCUIT = join(ROOT, "dist", "panel", "circuit.json")
const DEFAULT_OUT = join(ROOT, "dist", "panel-release", "preview")

type CjElement = Record<string, any>

const buildDesign = (design: PanelDesignId): string => {
  const run = spawnSync(process.execPath, ["x", "--no-install", "tsci", "build", "panel.circuit.tsx"], {
    cwd: ROOT,
    env: { ...process.env, FROEMIX_PANEL_DESIGN: design },
    encoding: "utf8",
  })
  if (run.status !== 0) {
    console.error(run.stdout ?? "")
    console.error(run.stderr ?? "")
    throw new Error(`tsci build failed for design ${design}`)
  }
  const dir = mkdtempSync(join(tmpdir(), "froemix-panel-preview-"))
  const copy = join(dir, `${design}.circuit.json`)
  writeFileSync(copy, readFileSync(join(ROOT, "dist", "panel", "circuit.json")))
  return copy
}

/**
 * Render the three files for one built circuit. The design is taken from the
 * built ink (or from `requested`, which must then match it), so the file name
 * and the drawing can never describe different designs.
 */
export const renderPreviewFiles = (
  circuitJson: CjElement[],
  outDir: string,
  requested?: string,
): { design: PanelDesignId | null; files: string[]; problems: Problem[] } => {
  const resolved = resolvePanelDesign(circuitJson, requested)
  if (!resolved.design || resolved.problems.length > 0) {
    return { design: null, files: [], problems: resolved.problems }
  }
  const art = panelArtwork(resolved.design)
  mkdirSync(outDir, { recursive: true })
  const files: string[] = []

  const previewSvg = renderPanelPreviewSvg(circuitJson, art)
  const previewPath = join(outDir, `panel-${art.design}-preview.svg`)
  writeFileSync(previewPath, previewSvg)
  files.push(previewPath)

  const printPath = join(outDir, `panel-${art.design}-print-1to1.svg`)
  writeFileSync(printPath, renderPrintTemplateSvg(circuitJson, art))
  files.push(printPath)

  const htmlPath = join(outDir, `panel-${art.design}-preview.html`)
  writeFileSync(
    htmlPath,
    renderPreviewHtml(
      previewSvg,
      art,
      "A drawing is not a fit check: print the 1:1 template, measure the calibration bar, then check the rack, the knobs, the nuts and the OLED module against real parts.",
    ),
  )
  files.push(htmlPath)

  return { design: art.design, files, problems: [] }
}

export const main = (argv: string[]): number => {
  const outArg = argv.indexOf("--out")
  const outDir = outArg >= 0 ? argv[outArg + 1]! : DEFAULT_OUT
  const circuitArg = argv.indexOf("--circuit")
  const circuitPath = circuitArg >= 0 ? argv[circuitArg + 1]! : DEFAULT_CIRCUIT
  const designArg = argv.indexOf("--design")
  const requested = designArg >= 0 ? argv[designArg + 1]! : undefined
  const variants = argv.includes("--variants")

  mkdirSync(outDir, { recursive: true })

  if (variants) {
    for (const design of PANEL_DESIGN_IDS) {
      const built = buildDesign(design)
      const result = renderPreviewFiles(JSON.parse(readFileSync(built, "utf8")), outDir, design)
      if (result.problems.length > 0) {
        for (const p of result.problems) console.error(`  FAIL [${p.check}] ${p.message}`)
        console.error(`panel preview failed for design ${design}`)
        return 1
      }
      for (const f of result.files) console.log(`wrote ${f}`)
    }
    // leave dist/ holding the default design again
    const fallback = buildDesign(panelArtwork().design)
    void fallback
    console.log(`note: dist/panel/circuit.json rebuilt for the default design (${panelArtwork().design})`)
    return 0
  }

  if (!existsSync(circuitPath)) {
    console.error(`missing ${circuitPath} - run \`bun run build\` first`)
    return 1
  }
  const result = renderPreviewFiles(JSON.parse(readFileSync(circuitPath, "utf8")), outDir, requested)
  if (result.problems.length > 0) {
    for (const p of result.problems) console.error(`  FAIL [${p.check}] ${p.message}`)
    console.error("panel preview refused: the design does not match the input")
    return 1
  }
  for (const f of result.files) console.log(`wrote ${f} (${result.design})`)
  return 0
}

if (import.meta.main) {
  process.exit(main(process.argv.slice(2)))
}

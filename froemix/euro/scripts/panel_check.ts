#!/usr/bin/env bun
// Froemix front panel - manufacturing check.
//
//   bun run panel:check              check the built dist/panel/circuit.json
//   bun run panel:check --variants   build and check every panel design
//
// Exit code 1 on any problem. Runs no network access and writes nothing except
// a temporary directory when --variants rebuilds the panel.

import { spawnSync } from "node:child_process"
import { existsSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs"
import { tmpdir } from "node:os"
import { join } from "node:path"
import {
  DEFAULT_PANEL_DESIGN,
  PANEL_DESIGN_IDS,
  isPanelDesignId,
  panelArtwork,
  type PanelArtwork,
  type PanelDesignId,
} from "../lib/panel-art"
import {
  checkArtwork,
  checkBuiltArtwork,
  checkFrozenGeometry,
  checkNoCopper,
  artworkMismatch,
  measureRoundedCorners,
  resolvePanelDesign,
  type PanelFixture,
  type Problem,
} from "../lib/panel-checks"
import { polylineBounds } from "../lib/panel-geometry"

const HERE = new URL(".", import.meta.url).pathname
const ROOT = join(HERE, "..")
const FIXTURE_PATH = join(ROOT, "fixtures", "panel-baseline.json")
export const DEFAULT_CIRCUIT = join(ROOT, "dist", "panel", "circuit.json")

type CjElement = Record<string, any>

const readJson = (path: string): any => JSON.parse(readFileSync(path, "utf8"))

/**
 * Which design the built JSON came from. Delegated to `resolvePanelDesign` in
 * `lib/panel-checks.ts`, so the check script, the preview script and the release
 * script cannot disagree about the answer.
 */
export const detectDesign = (circuitJson: CjElement[]): { design: PanelDesignId | null; problems: Problem[] } => {
  const resolved = resolvePanelDesign(circuitJson)
  return { design: resolved.design, problems: resolved.problems }
}

export type PanelReport = {
  design: PanelDesignId
  art: PanelArtwork
  fixture: PanelFixture
  circuitJson: CjElement[]
  problems: Problem[]
  summary: {
    outline: string
    holes: number
    window: string
    silkPaths: number
    minStroke: number
    coppers: number
  }
}

/** Run every panel check against one built circuit JSON. No I/O. */
export const checkBuiltPanel = (
  circuitJson: CjElement[],
  opts: { design?: PanelDesignId } = {},
): PanelReport => {
  const fixture = readJson(FIXTURE_PATH) as PanelFixture
  const resolved = resolvePanelDesign(circuitJson, opts.design)
  const design = resolved.design ?? DEFAULT_PANEL_DESIGN
  const art = panelArtwork(design)

  const problems: Problem[] = [
    ...resolved.problems,
    ...checkFrozenGeometry(circuitJson, fixture),
    ...checkNoCopper(circuitJson),
    ...checkBuiltArtwork(circuitJson, art),
    ...checkArtwork(art),
  ]

  const board = circuitJson.find((e) => e.type === "pcb_board")
  const silk = circuitJson.filter((e) => e.type === "pcb_silkscreen_path")
  const cut = circuitJson.find((e) => e.type === "pcb_cutout")
  const points = (cut?.points ?? []) as { x: number; y: number }[]
  const b = polylineBounds(points)
  const corners = b
    ? measureRoundedCorners(points, { cx: b.cx, cy: b.cy, width: b.width, height: b.height, radius: 0.5 })
    : null

  return {
    design,
    art,
    fixture,
    circuitJson,
    problems,
    summary: {
      outline: board ? `${board.width} x ${board.height}mm, ${board.thickness}mm thick` : "missing",
      holes: circuitJson.filter((e) => e.type === "pcb_hole").length,
      window: b
        ? `${b.width.toFixed(3)} x ${b.height.toFixed(3)}mm at (${b.cx.toFixed(3)}, ${b.cy.toFixed(3)}), R${corners!.minRadius.toFixed(3)}-${corners!.maxRadius.toFixed(3)}, ${corners!.segmentsPerCorner} chords/corner, sagitta ${corners!.maxSagitta.toFixed(4)}mm`
        : "missing",
      silkPaths: silk.length,
      minStroke: silk.length > 0 ? Math.min(...silk.map((e) => e.stroke_width ?? 0)) : 0,
      coppers:
        circuitJson.filter((e) => e.type === "pcb_trace" || e.type === "pcb_smtpad" || e.type === "pcb_via").length,
    },
  }
}

export const printReport = (label: string, report: PanelReport): void => {
  const s = report.summary
  console.log(`${label} (design ${report.design})`)
  console.log(`  outline   ${s.outline}`)
  console.log(`  holes     ${s.holes} round NPTH, ${s.holes === report.fixture.holes.length ? "matching the baseline exactly" : `baseline has ${report.fixture.holes.length}`}`)
  console.log(`  window    ${s.window}`)
  console.log(`  silk      ${s.silkPaths} paths, narrowest stroke ${s.minStroke.toFixed(3)}mm`)
  console.log(`  copper    ${s.coppers} copper elements`)
}

const buildVariant = (design: PanelDesignId): string => {
  const out = mkdtempSync(join(tmpdir(), "froemix-panel-"))
  const run = spawnSync(process.execPath, ["x", "--no-install", "tsci", "build", "panel.circuit.tsx"], {
    cwd: ROOT,
    env: { ...process.env, FROEMIX_PANEL_DESIGN: design },
    encoding: "utf8",
  })
  if (run.status !== 0) {
    console.error(`build of the ${design} panel failed:`)
    console.error(run.stdout ?? "")
    console.error(run.stderr ?? "")
    throw new Error(`tsci build failed for design ${design}`)
  }
  const built = join(ROOT, "dist", "panel", "circuit.json")
  const copy = join(out, `${design}.circuit.json`)
  writeFileSync(copy, readFileSync(built))
  return copy
}

export const main = (argv: string[]): number => {
  const variants = argv.includes("--variants")
  const circuitArg = argv.indexOf("--circuit")
  const circuitPath = circuitArg >= 0 ? argv[circuitArg + 1]! : DEFAULT_CIRCUIT

  console.log("panel check - froemix 20HP faceplate")
  let failed = 0
  const cleaned: string[] = []

  const runOne = (label: string, path: string, design?: PanelDesignId) => {
    if (!existsSync(path)) {
      console.error(`\n${label}: missing ${path} - run \`bun run build\` first`)
      failed++
      return
    }
    const report = checkBuiltPanel(readJson(path), { design })
    console.log()
    printReport(label, report)
    if (report.problems.length > 0) {
      failed += report.problems.length
      for (const p of report.problems) console.error(`  FAIL [${p.check}] ${p.message}`)
    } else {
      console.log("  PASS")
    }
  }

  if (variants) {
    for (const design of PANEL_DESIGN_IDS) {
      const path = buildVariant(design)
      cleaned.push(path.replace(/\/[^/]+$/, ""))
      runOne(`${path.startsWith(tmpdir()) ? "built" : ""} variant ${design}`, path, design)
    }
    // leave dist/ holding the default design again
    buildVariant(DEFAULT_PANEL_DESIGN)
  } else {
    runOne("built panel", circuitPath)
  }

  // artwork rules for every design, built or not
  for (const design of PANEL_DESIGN_IDS) {
    const problems = checkArtwork(panelArtwork(design))
    if (problems.length > 0) {
      failed += problems.length
      console.error(`\nartwork ${design}: ${problems.length} problem(s)`)
      for (const p of problems) console.error(`  FAIL [${p.check}] ${p.message}`)
    } else {
      console.log(`artwork ${design}: PASS`)
    }
  }

  for (const dir of cleaned) rmSync(dir, { recursive: true, force: true })

  console.log()
  if (failed > 0) {
    console.error(`PANEL CHECK FAILED (${failed} problem(s))`)
    return 1
  }
  console.log("PANEL CHECK PASSED")
  return 0
}

if (import.meta.main) {
  process.exit(main(process.argv.slice(2)))
}

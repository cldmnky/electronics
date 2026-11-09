#!/usr/bin/env bun
// Froemix front panel - release.
//
//   bun run panel:release                 fabricate-ready panel, selected signal design
//   bun run panel:release --design nord   the alternative artwork
//
// What it does, in order:
//   1. builds and checks EVERY panel design, so a variant cannot rot
//   2. renders broker previews and 1:1 print templates from the BUILT JSON
//   3. exports the selected design as a panel-only Gerber/drill ZIP
//   4. unpacks that ZIP and verifies the geometry inside the files, not the names
//   5. writes a manifest next to the artefacts
//
// It releases the PANEL ONLY: no board, no BOM, no placement data. Everything
// lands in dist/panel-release/ (gitignored) and nothing outside it is touched.
// Nothing here commits, pushes or places an order.
//
// This is a production entry point, so it is gated: `checkPhysicalAcceptance`
// runs first and refuses before dist/panel-release/ is created, written or
// deleted. Mechanical prototypes stay available through mechanical_release.ts;
// they are not production certification. See ../../doc/acceptance.md.

import { createHash } from "node:crypto"
import { spawnSync } from "node:child_process"
import { existsSync, mkdirSync, mkdtempSync, readFileSync, readdirSync, rmSync, statSync, writeFileSync } from "node:fs"
import { tmpdir } from "node:os"
import { join, relative } from "node:path"
import {
  DEFAULT_PANEL_DESIGN,
  PANEL_DESIGN_IDS,
  isPanelDesignId,
  panelArtwork,
  type PanelDesignId,
} from "../lib/panel-art"
import { checkBuiltPanel } from "./panel_check"
import { verifyPanelRelease } from "../lib/panel-gerber"
import {
  hardwareFingerprintReport,
  loadState,
  validateAcceptance,
} from "../../tools/release/acceptance"
import { renderPanelPreviewSvg, renderPreviewHtml, renderPrintTemplateSvg } from "../lib/panel-render"

const HERE = new URL(".", import.meta.url).pathname
const ROOT = join(HERE, "..")
const OUT = join(ROOT, "dist", "panel-release")
const ENTRY = "panel.circuit.tsx"

type CjElement = Record<string, any>

const sha256 = (path: string): string =>
  createHash("sha256").update(readFileSync(path)).digest("hex")

const build = (design: PanelDesignId, extra: string[] = []): void => {
  const run = spawnSync(process.execPath, ["x", "--no-install", "tsci", "build", ENTRY, ...extra], {
    cwd: ROOT,
    env: { ...process.env, FROEMIX_PANEL_DESIGN: design },
    encoding: "utf8",
  })
  if (run.status !== 0) {
    console.error(run.stdout ?? "")
    console.error(run.stderr ?? "")
    throw new Error(`tsci build failed for the ${design} panel`)
  }
}

const readBuilt = (): CjElement[] => JSON.parse(readFileSync(join(ROOT, "dist", "panel", "circuit.json"), "utf8"))

const exportGerbers = (design: PanelDesignId, zipPath: string): void => {
  rmSync(zipPath, { force: true })
  const run = spawnSync(
    process.execPath,
    ["x", "--no-install", "tsci", "export", ENTRY, "--format", "gerbers", "--output", zipPath],
    { cwd: ROOT, env: { ...process.env, FROEMIX_PANEL_DESIGN: design }, encoding: "utf8" },
  )
  if (run.status !== 0 || !existsSync(zipPath)) {
    console.error(run.stdout ?? "")
    console.error(run.stderr ?? "")
    throw new Error(`tsci export --format gerbers failed for the ${design} panel`)
  }
}

const unzip = (zipPath: string, dir: string): void => {
  mkdirSync(dir, { recursive: true })
  const run = spawnSync(
    "python3",
    ["-c", "import sys, zipfile; zipfile.ZipFile(sys.argv[1]).extractall(sys.argv[2])", zipPath, dir],
    { encoding: "utf8" },
  )
  if (run.status !== 0) {
    console.error(run.stdout ?? "")
    console.error(run.stderr ?? "")
    throw new Error(`could not unpack ${zipPath}`)
  }
}

const walk = (dir: string): string[] => {
  const out: string[] = []
  for (const entry of readdirSync(dir)) {
    const full = join(dir, entry)
    if (statSync(full).isDirectory()) out.push(...walk(full))
    else out.push(full)
  }
  return out
}

/**
 * Physical acceptance guard for this production entry point. Runs the same
 * validator as `tools/release/gate.ts --check`, before anything is written or
 * deleted, and returns the reasons when it blocks.
 */
export const checkPhysicalAcceptance = (root: string): { ok: boolean; problems: string[] } => {
  const sources = hardwareFingerprintReport(root)
  const problems: string[] = []
  for (const path of sources.missing) problems.push(`missing hardware source: ${path}`)
  for (const path of sources.missingDirectories) problems.push(`missing hardware source directory: ${path}`)
  if (problems.length > 0) {
    problems.push("the hardware fingerprint would be partial, so no acceptance can be checked")
    return { ok: false, problems }
  }
  const statePath = join(root, "hardware-acceptance.json")
  if (!existsSync(statePath)) {
    problems.push(`no acceptance record at ${statePath}`)
    return { ok: false, problems }
  }
  let state: unknown
  try {
    state = loadState(statePath)
  } catch (error) {
    problems.push((error as Error).message)
    return { ok: false, problems }
  }
  const result = validateAcceptance(state, { rootDir: root, fingerprint: sources.fingerprint })
  problems.push(...result.failures)
  return { ok: problems.length === 0, problems }
}

export const main = (argv: string[]): number => {
  const designArg = argv.indexOf("--design")
  const requested = designArg >= 0 ? argv[designArg + 1]! : (process.env.FROEMIX_PANEL_DESIGN ?? DEFAULT_PANEL_DESIGN)
  if (!isPanelDesignId(requested)) {
    console.error(`unknown design ${JSON.stringify(requested)}; expected one of ${PANEL_DESIGN_IDS.join(", ")}`)
    return 1
  }
  const design: PanelDesignId = requested

  // Fail closed BEFORE the first write or delete: a physical item is pending or
  // the record is invalid, so dist/panel-release/ stays exactly as it is.
  const acceptance = checkPhysicalAcceptance(join(ROOT, ".."))
  if (!acceptance.ok) {
    for (const problem of acceptance.problems) console.error(`  FAIL ${problem}`)
    console.error(
      `\nPANEL RELEASE BLOCKED: physical acceptance is not complete (${acceptance.problems.length} reason(s)).`,
    )
    console.error("Nothing was built, exported or deleted; dist/panel-release/ is untouched.")
    console.error("A pending item is normal today: see ../doc/acceptance.md and ../doc/evidence/README.md.")
    return 1
  }

  mkdirSync(OUT, { recursive: true })
  // release.sh reads this so the file names it copies cannot drift from the
  // design panel_release.ts actually exported.
  writeFileSync(join(OUT, "selected-design.txt"), `${design}\n`)

  console.log("########## 1. build and check every design ##########")
  let problems = 0
  const variants: Record<string, { silkPaths: number; holes: number }> = {}
  for (const variant of PANEL_DESIGN_IDS) {
    build(variant)
    const circuitJson = readBuilt()
    const dir = join(OUT, variant)
    mkdirSync(dir, { recursive: true })
    writeFileSync(join(dir, "circuit.json"), JSON.stringify(circuitJson, null, 2))
    const report = checkBuiltPanel(circuitJson, { design: variant })
    variants[variant] = { silkPaths: report.summary.silkPaths, holes: report.summary.holes }
    if (report.problems.length > 0) {
      problems += report.problems.length
      console.error(`  ${variant}: ${report.problems.length} problem(s)`)
      for (const p of report.problems) console.error(`    FAIL [${p.check}] ${p.message}`)
    } else {
      console.log(
        `  ${variant}: PASS (${report.summary.holes} holes, ${report.summary.silkPaths} silk paths, narrowest stroke ${report.summary.minStroke.toFixed(3)}mm)`,
      )
    }
  }
  if (problems > 0) {
    console.error(`\nPANEL RELEASE ABORTED: ${problems} check problem(s)`)
    return 1
  }

  console.log("\n########## 2. previews and 1:1 print templates ##########")
  const previewDir = join(OUT, "preview")
  mkdirSync(previewDir, { recursive: true })
  for (const variant of PANEL_DESIGN_IDS) {
    const circuitJson: CjElement[] = JSON.parse(readFileSync(join(OUT, variant, "circuit.json"), "utf8"))
    const art = panelArtwork(variant)
    const svg = renderPanelPreviewSvg(circuitJson, art)
    writeFileSync(join(previewDir, `panel-${variant}-preview.svg`), svg)
    writeFileSync(join(previewDir, `panel-${variant}-print-1to1.svg`), renderPrintTemplateSvg(circuitJson, art))
    writeFileSync(
      join(previewDir, `panel-${variant}-preview.html`),
      renderPreviewHtml(
        svg,
        art,
        "Illustration only: dashed circles are conservative installed-hardware keepouts. Print the 1:1 template at 100%, check the calibration bar, then check the rack, knobs, nuts and OLED module against real parts.",
      ),
    )
    console.log(`  ${variant}: preview.svg, print-1to1.svg, preview.html`)
  }

  console.log(`\n########## 3. export (design: ${design}) ##########`)
  build(design)
  const zipPath = join(OUT, `froemix-panel-${design}-gerbers.zip`)
  exportGerbers(design, zipPath)
  console.log(`  ${relative(ROOT, zipPath)} (${(statSync(zipPath).size / 1024).toFixed(1)} KB)`)

  console.log("\n########## 4. verify the exported geometry ##########")
  const work = mkdtempSync(join(tmpdir(), "froemix-panel-release-"))
  unzip(zipPath, work)
  const files = walk(work).map((full) => ({
    name: relative(work, full),
    text: readFileSync(full, "utf8"),
  }))
  const built = readBuilt()
  const verified = verifyPanelRelease({ files, fixture: checkBuiltPanel(built).fixture, art: panelArtwork(design) })
  for (const layer of verified.summary.layers) {
    console.log(
      `  ${layer.name.padEnd(18)} ${layer.fileFunction.padEnd(22)} ${layer.units}  draws ${String(layer.draws).padStart(5)}  flashes ${String(layer.flashes).padStart(3)}  contours ${layer.contours}`,
    )
  }
  console.log(
    `  ${verified.summary.drill.name.padEnd(18)} NPTH ${verified.summary.drill.holes} holes, tools ${verified.summary.drill.tools
      .map((t) => `${t.diameter}mm x${t.count}`)
      .join(", ")}`,
  )
  console.log(
    `  profile            outline ${verified.summary.profile.outline}, window ${verified.summary.profile.window}, ${verified.summary.profile.contours} contour(s)`,
  )
  console.log(
    `  top legend         ${verified.summary.silk.contours} polylines, apertures ${verified.summary.silk.apertures.map((d) => `${d.toFixed(3)}mm`).join(", ")}`,
  )
  if (verified.problems.length > 0) {
    for (const p of verified.problems) console.error(`  FAIL [${p.check}] ${p.message}`)
    rmSync(work, { recursive: true, force: true })
    console.error(`\nPANEL RELEASE FAILED: ${verified.problems.length} export problem(s)`)
    return 1
  }
  console.log("  PASS - outline, window, legend, empty copper and NPTH holes all verified inside the files")

  const manifest = {
    panel: "froemix 20HP faceplate",
    design,
    designNote:
      design === DEFAULT_PANEL_DESIGN
        ? "owner-selected direction for this revision"
        : "alternative direction, not selected for this revision",
    material: "FR4 2.0mm, standard black solder mask, white top legend, no copper, no assembly",
    outline: panelArtwork(design).outline,
    window: {
      width: panelArtwork(design).window.width,
      height: panelArtwork(design).window.height,
      center: panelArtwork(design).window.centre,
      cornerRadius: panelArtwork(design).window.radius,
    },
    variants,
    zip: {
      file: relative(OUT, zipPath),
      sha256: sha256(zipPath),
      files: files.map((f) => f.name).sort(),
    },
    checks: { panelCheck: "passed", exportVerification: "passed" },
    orderRemarks:
      "Mechanical Eurorack faceplate, no circuit. FR4 2.0mm, black solder mask, white top legend. Both copper layers are intentionally empty; please confirm copper-free manufacture. All holes are non-plated. Please confirm finished diameters and routing of the large NPTH holes, the OLED corner radius and the routed outer profile. Do not add a barcode/mark on the front. Please provide production files for approval.",
  }
  // one record per design: two releases can sit side by side in dist/ without
  // either file claiming to describe the other
  const manifestName = `panel-release-${design}.json`
  writeFileSync(join(OUT, manifestName), JSON.stringify(manifest, null, 2) + "\n")
  rmSync(work, { recursive: true, force: true })

  console.log("\n########## 5. artefacts ##########")
  for (const full of walk(OUT).sort()) {
    const size = statSync(full).size
    console.log(`  ${relative(OUT, full).padEnd(44)} ${(size / 1024).toFixed(1)} KB`)
  }
  console.log(`  folder: ${OUT}`)
  console.log("\nPANEL RELEASE COMPLETE - fabrication only; no BOM, no placement data, no order placed.")
  return 0
}

if (import.meta.main) {
  process.exit(main(process.argv.slice(2)))
}

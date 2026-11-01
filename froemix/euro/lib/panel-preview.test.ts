/// <reference types="bun-types" />
// The preview script must label a build with the design it actually carries.
// These tests feed it whole circuit JSON files, not single functions, so the
// file-name-versus-geometry mistake cannot come back.

import { afterAll, describe, expect, test } from "bun:test"
import { mkdtempSync, readdirSync, readFileSync, rmSync } from "node:fs"
import { tmpdir } from "node:os"
import { join } from "node:path"
import { panelArtwork } from "../lib/panel-art"
import { resolvePanelDesign } from "../lib/panel-checks"
import { syntheticPanel } from "../lib/panel-testkit"
import { renderPreviewFiles } from "../scripts/panel_preview"

const dirs: string[] = []
const outDir = (): string => {
  const dir = mkdtempSync(join(tmpdir(), "froemix-panel-preview-test-"))
  dirs.push(dir)
  return dir
}
afterAll(() => {
  for (const dir of dirs) rmSync(dir, { recursive: true, force: true })
})

const nordJson = syntheticPanel(panelArtwork("nord"))
const signalJson = syntheticPanel(panelArtwork("signal"))

describe("resolvePanelDesign", () => {
  test("reads the design from the printed geometry", () => {
    expect(resolvePanelDesign(nordJson).design).toBe("nord")
    expect(resolvePanelDesign(signalJson).design).toBe("signal")
  })

  test("refuses a file that claims another design", () => {
    const resolved = resolvePanelDesign(signalJson, "nord")
    expect(resolved.design).toBeNull()
    expect(resolved.detected).toBe("signal")
    expect(resolved.problems[0]!.message).toContain("not nord")
  })

  test("accepts a matching design", () => {
    expect(resolvePanelDesign(signalJson, "signal").problems).toEqual([])
  })

  test("rejects an unknown design name", () => {
    expect(resolvePanelDesign(nordJson, "field").problems[0]!.message).toContain("unknown design")
  })

  test("refuses artwork that matches nothing", () => {
    const broken = nordJson.filter((e) => e.type !== "pcb_silkscreen_path")
    const resolved = resolvePanelDesign(broken)
    expect(resolved.design).toBeNull()
    expect(resolved.problems[0]!.message).toContain("cannot match")
  })
})

describe("panel_preview.ts on real file contents", () => {
  test("writes signal geometry only under signal file names", () => {
    const dir = outDir()
    const result = renderPreviewFiles(signalJson, dir)
    expect(result.problems).toEqual([])
    expect(result.design).toBe("signal")
    const names = readdirSync(dir).sort()
    expect(names).toEqual([
      "panel-signal-preview.html",
      "panel-signal-preview.svg",
      "panel-signal-print-1to1.svg",
    ])
    // the wordmark of the signal design only appears in a signal file
    const svg = readFileSync(join(dir, "panel-signal-preview.svg"), "utf8")
    expect(svg.length).toBeGreaterThan(1000)
    expect(names.some((n) => n.includes("nord"))).toBe(false)
  })

  test("refuses to render a signal build as nord, and writes nothing", () => {
    const dir = outDir()
    const result = renderPreviewFiles(signalJson, dir, "nord")
    expect(result.files).toEqual([])
    expect(result.design).toBeNull()
    expect(readdirSync(dir)).toEqual([])
  })

  test("renders nord under nord file names", () => {
    const dir = outDir()
    const result = renderPreviewFiles(nordJson, dir, "nord")
    expect(result.problems).toEqual([])
    expect(result.design).toBe("nord")
    expect(readdirSync(dir).sort()[0]).toBe("panel-nord-preview.html")
  })

  test("refuses a build whose artwork matches no design", () => {
    const dir = outDir()
    const result = renderPreviewFiles(nordJson.slice(0, 2), dir)
    expect(result.design).toBeNull()
    expect(result.problems.length).toBeGreaterThan(0)
    expect(readdirSync(dir)).toEqual([])
  })
})

import { afterEach, beforeEach, describe, expect, test } from "bun:test"
import { appendFileSync, mkdirSync, mkdtempSync, realpathSync, rmSync, symlinkSync, writeFileSync } from "node:fs"
import { tmpdir } from "node:os"
import { dirname, join } from "node:path"
import {
  computeHardwareFingerprint,
  FINGERPRINT_DIRS,
  hardwareFingerprintReport,
  REQUIRED_FILES,
  REQUIRED_IDS,
  REQUIRED_ITEMS,
  resolveEvidence,
  SCHEMA,
  validateAcceptance,
} from "./acceptance"

let root: string
let fingerprint: string

function write(rel: string, content: string): void {
  const path = join(root, rel)
  mkdirSync(dirname(path), { recursive: true })
  writeFileSync(path, content)
}

function evidenceText(id: string): string {
  return [
    `# Evidence: ${id}`,
    "",
    `- item: ${id}`,
    "- date: 2026-01-01",
    "- reviewer: A. Person",
    "- hardware fingerprint: recorded in hardware-acceptance.json",
    "",
    "## Measurement",
    "",
    "| Check | Method | Result | Requirement |",
    "|---|---|---|---|",
    "| Standoff stack | caliper | 11.02 mm | 11 mm clear |",
    "",
    "Padding so the file is a realistic report, not a one-line claim. ".repeat(4),
    "",
  ].join("\n")
}

beforeEach(() => {
  root = mkdtempSync(join(tmpdir(), "froemix-acceptance-"))
  for (const rel of REQUIRED_FILES) write(rel, `// ${rel} fixture\n`)
  for (const { dir } of FINGERPRINT_DIRS) write(join(dir, "fixture.tsx"), "// section fixture\n")
  write("doc/evidence/README.md", "# evidence\n")
  for (const id of REQUIRED_IDS) write(`doc/evidence/${id}.md`, evidenceText(id))
  fingerprint = computeHardwareFingerprint(root)
})

afterEach(() => {
  rmSync(root, { recursive: true, force: true })
})

type Item = Record<string, unknown>

function item(id: string, overrides: Item = {}): Item {
  return {
    id,
    state: "accepted",
    evidence: `doc/evidence/${id}.md`,
    reviewer: "A. Person",
    reviewed_at: "2026-01-01",
    source_fingerprint: fingerprint,
    ...overrides,
  }
}

function stateWith(overrides: Record<string, Item> = {}, extra: Item = {}) {
  return {
    schema: SCHEMA,
    hardware_fingerprint: fingerprint,
    items: REQUIRED_IDS.map((id) => item(id, overrides[id] ?? {})),
    ...extra,
  }
}

const validate = (state: unknown) =>
  validateAcceptance(state, { rootDir: root, fingerprint, today: "2026-06-01" })

const failureText = (state: unknown) => validate(state).failures.join(" | ")

describe("required item list", () => {
  test("covers every physical item the plan names", () => {
    for (const id of [
      "stack_11mm", "daisy_socket", "fasteners", "nav_actuator", "mode_switch",
      "jacks_pots", "panel_rack", "oled_support", "eu_hand_parts", "p5v_rail",
      "cv_calibration", "cv_cable_load", "mux_sync", "midi_input", "midi_output",
      "ground_tie", "esd", "dfm_cpl", "first_article",
    ]) {
      expect(REQUIRED_IDS).toContain(id)
    }
  })

  test("ids are unique", () => {
    expect(new Set(REQUIRED_IDS).size).toBe(REQUIRED_IDS.length)
  })

  test("every item has a requirement that says what to measure", () => {
    for (const item of REQUIRED_ITEMS) {
      expect(item.title.length).toBeGreaterThanOrEqual(3)
      expect(item.requirement.length).toBeGreaterThan(40)
    }
  })

  test("midi_output also requires primary-datasheet verification of the GPIO limits", () => {
    // Bench current alone cannot prove an absolute per-pin rating: the
    // STM32H750 limits (20 mA, VOL 0.4 V, VOH) and the reset state are
    // assumptions until a primary ST datasheet is checked.
    const item = REQUIRED_ITEMS.find((entry) => entry.id === "midi_output")!
    expect(item).toBeDefined()
    expect(item.requirement).toMatch(/datasheet/i)
    expect(item.requirement).toMatch(/STM32H750/)
    expect(item.requirement).toMatch(/20 mA/)
    expect(item.requirement).toMatch(/reset state/i)
    expect(item.requirement).toMatch(/assumptions?/i)
  })

  test("every item is still in the unchanged 19-item list", () => {
    expect(REQUIRED_IDS.length).toBe(19)
    expect(REQUIRED_IDS).toContain("midi_output")
  })

  test("no requirement invents a measurement or an approved part", () => {
    // Requirements may state limits ("<= 300 mA") but must not contain a
    // measured result, a part number or a supplier approval.
    for (const item of REQUIRED_ITEMS) {
      expect(item.requirement).not.toMatch(/\bC\d{4,}\b/)
      expect(item.requirement.toLowerCase()).not.toContain("approved mpn is")
    }
  })
})

describe("validator", () => {
  test("a well-formed synthetic fixture passes", () => {
    const result = validate(stateWith())
    expect(result.failures).toEqual([])
    expect(result.pending).toEqual([])
    expect(result.accepted.length).toBe(REQUIRED_ITEMS.length)
  })

  test("the shipped state file is all pending", () => {
    const state = JSON.parse(
      require("node:fs").readFileSync(join(import.meta.dir, "..", "..", "hardware-acceptance.json"), "utf8"),
    )
    expect(state.schema).toBe(SCHEMA)
    expect(state.items.length).toBe(REQUIRED_ITEMS.length)
    for (const entry of state.items) expect(entry.state).toBe("pending")
    // ...and against the tree it ships with, the record validates with only
    // the pending failures (nothing malformed, no stale fingerprint).
    const projectRoot = join(import.meta.dir, "..", "..")
    const result = validateAcceptance(state, {
      rootDir: projectRoot,
      fingerprint: computeHardwareFingerprint(projectRoot),
    })
    const other = result.failures.filter((failure) => !failure.includes("still pending"))
    expect(other).toEqual([])
  })

  test("pending items block the release", () => {
    const state = stateWith()
    state.items = state.items.map((entry) => ({ id: entry.id, state: "pending" }))
    const result = validate(state)
    expect(result.pending.length).toBe(REQUIRED_ITEMS.length)
    expect(result.failures.join(" ")).toContain("still pending")
  })

  test("a missing required item fails", () => {
    const state = stateWith()
    state.items = state.items.filter((entry) => entry.id !== "p5v_rail")
    expect(failureText(state)).toContain("missing required item: p5v_rail")
  })

  test("an unknown item id fails", () => {
    const state = stateWith()
    state.items.push(item("made_up_item"))
    expect(failureText(state)).toContain("unknown item id: made_up_item")
  })

  test("a duplicate item id fails", () => {
    const state = stateWith()
    state.items.push(item("esd"))
    expect(failureText(state)).toContain("duplicate item id: esd")
  })

  test("a wrong schema fails", () => {
    expect(failureText(stateWith({}, { schema: "something-else" }))).toContain("schema must be")
  })

  test("items must be an array", () => {
    expect(failureText(stateWith({}, { items: {} }))).toContain("items must be an array")
  })

  test("a blanket override key at the top level fails", () => {
    for (const key of ["override", "force", "skip_items", "waive", "bypass", "allow_unverified", "exempt"]) {
      expect(failureText(stateWith({}, { [key]: true }))).toContain("blanket override key")
    }
  })

  test("a blanket override key inside an item fails", () => {
    expect(failureText(stateWith({ stack_11mm: { force: true } }))).toContain("blanket override key")
  })

  test("an unknown key fails", () => {
    expect(failureText(stateWith({}, { status: "done" }))).toContain('unknown top-level key: "status"')
    expect(failureText(stateWith({ esd: { signed_off: true } }))).toContain('unknown item key: "signed_off"')
  })

  test("a stale hardware fingerprint fails", () => {
    expect(failureText(stateWith({}, { hardware_fingerprint: "0".repeat(64) }))).toContain(
      "hardware fingerprint is stale",
    )
  })

  test("a hardware fingerprint that is not a sha256 fails", () => {
    expect(failureText(stateWith({}, { hardware_fingerprint: "abc" }))).toContain("64-character sha256")
  })

  test("an accepted item needs the current source fingerprint", () => {
    expect(failureText(stateWith({ esd: { source_fingerprint: "0".repeat(64) } }))).toContain(
      "source_fingerprint",
    )
  })

  test("an accepted item without evidence fails", () => {
    expect(failureText(stateWith({ esd: { evidence: undefined } }))).toContain("evidence path missing")
  })

  test("a missing evidence file fails", () => {
    rmSync(join(root, "doc/evidence/esd.md"))
    expect(failureText(stateWith())).toContain("does not exist")
  })

  test("an empty evidence file fails", () => {
    writeFileSync(join(root, "doc/evidence/esd.md"), "")
    expect(failureText(stateWith())).toContain("empty")
  })

  test("a short evidence file is not evidence", () => {
    writeFileSync(join(root, "doc/evidence/esd.md"), "esd ok 4 kV\n")
    expect(failureText(stateWith())).toContain("too short")
  })

  test("a path traversal fails", () => {
    write("secret.md", evidenceText("esd"))
    expect(failureText(stateWith({ esd: { evidence: "doc/evidence/../secret.md" } }))).toContain(
      'must not contain ".."',
    )
  })

  test("an absolute path fails", () => {
    const absolute = join(root, "doc/evidence/esd.md")
    expect(failureText(stateWith({ esd: { evidence: absolute } }))).toContain("must be relative")
  })

  test("a symlinked evidence file fails", () => {
    const outside = join(root, "outside.md")
    writeFileSync(outside, evidenceText("esd"))
    rmSync(join(root, "doc/evidence/esd.md"))
    symlinkSync(outside, join(root, "doc/evidence/esd.md"))
    expect(failureText(stateWith())).toContain("symlink")
  })

  test("ordinary regular evidence resolves", () => {
    const resolved = resolveEvidence(root, join(root, "doc/evidence"), "doc/evidence/esd.md")
    expect(resolved.error).toBeUndefined()
    expect(realpathSync(resolved.abs!)).toBe(realpathSync(join(root, "doc/evidence/esd.md")))
  })

  test("a symlinked doc/ directory fails even though the file is regular", () => {
    // The review case: doc/ points outside the project; the final .md is a
    // regular file inside the outside directory, so the old lstat-only check
    // passed it while the evidence lived outside the project.
    const outside = join(root, "outside-land")
    mkdirSync(join(outside, "evidence"), { recursive: true })
    writeFileSync(join(outside, "evidence", "esd.md"), evidenceText("esd"))
    for (const id of REQUIRED_IDS) writeFileSync(join(outside, "evidence", `${id}.md`), evidenceText(id))
    rmSync(join(root, "doc"), { recursive: true, force: true })
    symlinkSync(outside, join(root, "doc"))
    expect(failureText(stateWith())).toContain("symlinked directory")
  })

  test("a symlinked doc/evidence directory fails", () => {
    const outside = join(root, "outside-evidence")
    mkdirSync(outside, { recursive: true })
    for (const id of REQUIRED_IDS) writeFileSync(join(outside, `${id}.md`), evidenceText(id))
    rmSync(join(root, "doc/evidence"), { recursive: true, force: true })
    symlinkSync(outside, join(root, "doc/evidence"))
    expect(failureText(stateWith())).toContain("symlinked directory")
  })

  test("a symlinked doc/ pointing inside the project is still rejected", () => {
    // The no-symlink rule below the project root is unconditional: evidence is
    // a file a reviewer wrote, not a link someone can repoint later.
    const inside = join(root, "doc-real")
    mkdirSync(join(inside, "evidence"), { recursive: true })
    for (const id of REQUIRED_IDS) writeFileSync(join(inside, "evidence", `${id}.md`), evidenceText(id))
    rmSync(join(root, "doc"), { recursive: true, force: true })
    symlinkSync(inside, join(root, "doc"))
    expect(failureText(stateWith())).toContain("symlinked directory")
  })

  test("a caller-supplied evidenceDir outside the project is not a jail", () => {
    // A directory that is genuinely outside the project tree: passing it as the
    // evidence directory must not legitimise a file there.
    const outside = mkdtempSync(join(tmpdir(), "froemix-external-evidence-"))
    writeFileSync(join(outside, "esd.md"), evidenceText("esd"))
    try {
      const resolved = resolveEvidence(root, outside, "doc/evidence/esd.md")
      expect(resolved.error).toContain("evidence directory must be inside the project")

      const result = validateAcceptance(stateWith(), {
        rootDir: root,
        evidenceDir: outside,
        fingerprint,
        today: "2026-06-01",
      })
      expect(result.failures.join(" ")).toContain("evidence directory must be inside the project")
    } finally {
      rmSync(outside, { recursive: true, force: true })
    }
  })

  test("a caller-supplied evidenceDir that is a symlink to outside is rejected", () => {
    const outside = mkdtempSync(join(tmpdir(), "froemix-external-link-"))
    writeFileSync(join(outside, "esd.md"), evidenceText("esd"))
    const link = join(root, "evidence-link")
    symlinkSync(outside, link)
    try {
      expect(resolveEvidence(root, link, "doc/evidence/esd.md").error)
        .toContain("evidence directory must be inside the project")
    } finally {
      rmSync(link, { force: true })
      rmSync(outside, { recursive: true, force: true })
    }
  })

  test("a symlink above the project root is allowed", () => {
    // macOS /tmp is a symlink; a project reached through one is legitimate.
    const link = join(root, "root-link")
    symlinkSync(root, link)
    const evidenceDir = join(link, "doc/evidence")
    const resolved = resolveEvidence(link, evidenceDir, "doc/evidence/esd.md")
    expect(resolved.error).toBeUndefined()
    const result = validateAcceptance(stateWith(), {
      rootDir: link,
      evidenceDir,
      fingerprint,
      today: "2026-06-01",
    })
    expect(result.failures).toEqual([])
  })

  test("a directory is not evidence", () => {
    rmSync(join(root, "doc/evidence/esd.md"))
    mkdirSync(join(root, "doc/evidence/esd.md"), { recursive: true })
    expect(failureText(stateWith())).toContain("regular file")
  })

  test("evidence outside doc/evidence fails", () => {
    write("doc/other/esd.md", evidenceText("esd"))
    expect(failureText(stateWith({ esd: { evidence: "doc/other/esd.md" } }))).toContain(
      "directly inside doc/evidence/",
    )
  })

  test("an evidence path that escapes the configured evidence dir fails", () => {
    // Same file, but the caller configured a different directory: the resolved
    // path must stay inside it.
    const result = resolveEvidence(join(root, "doc"), join(root, "doc/evidence-elsewhere"), "evidence/esd.md")
    expect(result.error).toBeDefined()
  })

  test("a placeholder reviewer fails", () => {
    for (const name of ["TBD", "n/a", "-", "unknown"]) {
      expect(failureText(stateWith({ esd: { reviewer: name } }))).toContain("named reviewer")
    }
  })

  test("an invalid date fails", () => {
    expect(failureText(stateWith({ esd: { reviewed_at: "2026-13-01" } }))).toContain("real date")
    expect(failureText(stateWith({ esd: { reviewed_at: "01/02/2026" } }))).toContain("real date")
    expect(failureText(stateWith({ esd: { reviewed_at: "2026-02-30" } }))).toContain("real date")
  })

  test("a future date fails", () => {
    expect(failureText(stateWith({ esd: { reviewed_at: "2026-12-31" } }))).toContain("not in the future")
  })

  test("evidence without a measured value and unit fails", () => {
    writeFileSync(
      join(root, "doc/evidence/esd.md"),
      "# Evidence: esd\n\n- item: esd\n\nIt looked fine when I checked it by hand, twice.\n"
      + "Padding ".repeat(40) + "\n",
    )
    expect(failureText(stateWith())).toContain("no value with a unit")
  })

  test("evidence that does not name its item fails", () => {
    writeFileSync(
      join(root, "doc/evidence/esd.md"),
      "# Evidence: something else\n\nA gap of 12.4 mm was measured. ".repeat(6),
    )
    expect(failureText(stateWith())).toContain('does not name item "esd"')
  })

  test("an evidence sha256 mismatch fails and a match passes", () => {
    expect(failureText(stateWith({ esd: { evidence_sha256: "0".repeat(64) } }))).toContain(
      "sha256 does not match",
    )
    const digest = new Bun.CryptoHasher("sha256").update(evidenceText("esd")).digest("hex")
    expect(validate(stateWith({ esd: { evidence_sha256: digest } })).failures).toEqual([])
  })
})

describe("hardware fingerprint", () => {
  test("is a sha256", () => {
    expect(fingerprint).toMatch(/^[0-9a-f]{64}$/)
  })

  test("changes when a base-board source changes", () => {
    appendFileSync(join(root, "tscircuit/index.circuit.tsx"), "// moved R9\n")
    expect(computeHardwareFingerprint(root)).not.toBe(fingerprint)
  })

  test("changes when a euro-board or panel source changes", () => {
    appendFileSync(join(root, "euro/index.circuit.tsx"), "// moved a jack\n")
    expect(computeHardwareFingerprint(root)).not.toBe(fingerprint)
    appendFileSync(join(root, "euro/panel.circuit.tsx"), "// moved a hole\n")
    expect(computeHardwareFingerprint(root)).not.toBe(fingerprint)
  })

  test("a file ADDED under a hashed directory changes it", () => {
    // A new section, footprint import or CAD helper must invalidate old
    // evidence even though no existing file changed. euro/scripts is .ts only,
    // so use the directory's own extension.
    for (const { dir, extensions } of FINGERPRINT_DIRS) {
      const name = `a-new-entry${extensions[0]}`
      const before = computeHardwareFingerprint(root)
      write(join(dir, name), "// new\n")
      expect(computeHardwareFingerprint(root)).not.toBe(before)
      rmSync(join(root, dir, name))
      expect(computeHardwareFingerprint(root)).toBe(before)
    }
  })

  test("a section or lib change changes it", () => {
    appendFileSync(join(root, "tscircuit/sections/fixture.tsx"), "// silk move\n")
    expect(computeHardwareFingerprint(root)).not.toBe(fingerprint)
  })

  test("unit tests and snapshots do not change it", () => {
    write("euro/lib/panel-art.test.ts", "// a test\n")
    write("tscircuit/lib/thing.spec.tsx", "// a spec\n")
    write("tools/test_something.py", "# a python test\n")
    expect(computeHardwareFingerprint(root)).toBe(fingerprint)
  })

  test("docs, evidence and the acceptance record do not change it", () => {
    appendFileSync(join(root, "doc/evidence/README.md"), "\nmore notes\n")
    write("doc/acceptance.md", "# acceptance\n")
    write("hardware-acceptance.json", "{}\n")
    write("froemix/README.md", "# readme\n")
    expect(computeHardwareFingerprint(root)).toBe(fingerprint)
  })

  test("generated output does not change it", () => {
    write("tscircuit/dist/index/circuit.json", "[]\n")
    write("fab/froemix-gerbers.zip", "zip\n")
    write("euro/checks/check.png", "png\n")
    write("tscircuit/__snapshots__/index.circuit-pcb.snap.svg", "<svg/>\n")
    expect(computeHardwareFingerprint(root)).toBe(fingerprint)
  })

  for (const [label, rel] of [
    ["the panel/CAD algorithm", "euro/scripts/panel_release.ts"],
    ["the JLCPCB order settings", "jlc-order.json"],
    ["the curated sourcing selection", "tools/sourcing-extras.csv"],
    ["the 3D-model map", "tools/lcsc_models.json"],
    ["the part maps in kicad2tscircuit.py", "tools/kicad2tscircuit.py"],
    ["the approved mechanical baseline fixture", "euro/fixtures/panel-baseline.json"],
    ["a package file", "euro/package.json"],
    ["a lock file", "tscircuit/bun.lock"],
  ] as const) {
    test(`changes when ${label} changes`, () => {
      expect(computeHardwareFingerprint(root)).toBe(fingerprint)
      appendFileSync(join(root, rel), "\n# mutation\n")
      expect(computeHardwareFingerprint(root)).not.toBe(fingerprint)
    })

    test(`reports a missing ${label}`, () => {
      rmSync(join(root, rel))
      const report = hardwareFingerprintReport(root)
      expect(report.missing).toContain(rel)
      expect(() => computeHardwareFingerprint(root)).toThrow(rel)
    })
  }

  test("a missing mandatory entry file fails instead of shrinking the print", () => {
    rmSync(join(root, "euro/panel.circuit.tsx"))
    expect(hardwareFingerprintReport(root).missing).toEqual(["euro/panel.circuit.tsx"])
    expect(() => computeHardwareFingerprint(root)).toThrow(/euro\/panel\.circuit\.tsx/)
  })

  test("a missing source directory fails instead of shrinking the print", () => {
    rmSync(join(root, "euro/sections"), { recursive: true, force: true })
    const report = hardwareFingerprintReport(root)
    expect(report.missingDirectories).toEqual(["euro/sections"])
    expect(() => computeHardwareFingerprint(root)).toThrow(/euro\/sections/)
  })

  test("a rail-hole or hole-position revision of the baseline fixture changes it", () => {
    // The owner-approved panel rail-hole correction is exactly this kind of
    // revision: the fixture is the mechanical contract behind physical
    // acceptance, so it must invalidate old evidence.
    appendFileSync(join(root, "euro/fixtures/panel-baseline.json"), " ")
    expect(computeHardwareFingerprint(root)).not.toBe(fingerprint)
  })

  test("the frozen KiCad reference and the simulation decks are not hardware sources", () => {
    // tmp/ is a frozen port baseline, tools/sim/ are SPICE decks: neither is
    // fabricated, so neither may invalidate physical evidence.
    write("tmp/froemix.kicad_sch", "(kicad_sch)\n")
    write("tools/sim/audio_in.cir", "* deck\n")
    expect(computeHardwareFingerprint(root)).toBe(fingerprint)
  })
})

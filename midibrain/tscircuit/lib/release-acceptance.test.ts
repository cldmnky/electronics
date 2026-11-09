import { afterEach, beforeEach, describe, expect, test } from "bun:test"
import { mkdtempSync, mkdirSync, symlinkSync, writeFileSync, appendFileSync, rmSync } from "node:fs"
import { tmpdir } from "node:os"
import { join, dirname } from "node:path"
import {
  computeHardwareFingerprint,
  FINGERPRINT_FILES,
  hardwareFingerprintReport,
  REQUIRED_IDS,
  REQUIRED_ITEMS,
  SCHEMA,
  validateAcceptance,
} from "./release-acceptance"

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
    "",
    "## Measurement",
    "",
    "| Check | Method | Result | Requirement |",
    "|---|---|---|---|",
    "| Gap | caliper | 12.4 mm | 10.0 mm |",
    "",
    "Padding so the file is a realistic report, not a one-line claim. ".repeat(4),
    "",
  ].join("\n")
}

beforeEach(() => {
  root = mkdtempSync(join(tmpdir(), "acceptance-"))
  for (const rel of FINGERPRINT_FILES) write(rel, `// ${rel} fixture\n`)
  write("tscircuit/imports/PJ_313D.tsx", "// footprint fixture\n")
  write("docs/evidence/README.md", "# evidence\n")
  for (const id of REQUIRED_IDS) write(`docs/evidence/${id}.md`, evidenceText(id))
  fingerprint = computeHardwareFingerprint(root)
})

afterEach(() => {
  rmSync(root, { recursive: true, force: true })
})

interface ItemOverride {
  [key: string]: unknown
}

function item(id: string, overrides: ItemOverride = {}): Record<string, unknown> {
  return {
    id,
    state: "accepted",
    evidence: `docs/evidence/${id}.md`,
    reviewer: "A. Person",
    reviewed_at: "2026-01-01",
    source_fingerprint: fingerprint,
    ...overrides,
  }
}

function stateWith(overrides: Record<string, ItemOverride> = {}, extra: Record<string, unknown> = {}) {
  return {
    schema: SCHEMA,
    hardware_fingerprint: fingerprint,
    items: REQUIRED_IDS.map(id => item(id, overrides[id] ?? {})),
    ...extra,
  }
}

const validate = (state: unknown) =>
  validateAcceptance(state, { rootDir: root, fingerprint, today: "2026-06-01" })

describe("required item list", () => {
  test("covers the plan's physical items", () => {
    for (const id of ["zstack", "control_actuator", "jack_retention", "light_pipes",
      "display_dsi", "pi_microsd", "cooler_hat_ssd", "case_fit", "usb_c_5a_path",
      "usb_pd_droop_thermal", "encoder_solder_process", "midi_threshold",
      "midi_receiver_high_current_thermal", "midi_out_legacy", "midi_interop", "midi_current_faults",
      "scope_spi", "scope_power_sequences", "scope_thermal", "scope_dfm_cpl",
      "autoroute_upstream_warning", "first_article"]) {
      expect(REQUIRED_IDS).toContain(id)
    }
  })
  test("ids are unique", () => {
    expect(new Set(REQUIRED_IDS).size).toBe(REQUIRED_IDS.length)
  })
})

describe("validator", () => {
  test("a well-formed synthetic fixture passes", () => {
    const result = validate(stateWith())
    expect(result.failures).toEqual([])
    expect(result.pending).toEqual([])
  })

  test("pending items block the release", () => {
    const state = stateWith()
    state.items = state.items.map(entry => ({ id: entry.id, state: "pending" }))
    const result = validate(state)
    expect(result.pending.length).toBe(REQUIRED_ITEMS.length)
    expect(result.failures.join(" ")).toContain("still pending")
  })

  test("a missing required item fails", () => {
    const state = stateWith()
    state.items = state.items.filter(entry => entry.id !== "zstack")
    const result = validate(state)
    expect(result.failures.join(" ")).toContain("missing required item: zstack")
  })

  test("an unknown item id fails", () => {
    const state = stateWith()
    state.items.push(item("made_up_item"))
    expect(validate(state).failures.join(" ")).toContain("unknown item id: made_up_item")
  })

  test("a duplicate item id fails", () => {
    const state = stateWith()
    state.items.push(item("zstack"))
    expect(validate(state).failures.join(" ")).toContain("duplicate item id: zstack")
  })

  test("a wrong schema fails", () => {
    const state = stateWith({}, { schema: "something-else" })
    expect(validate(state).failures.join(" ")).toContain("schema must be")
  })

  test("items must be an array", () => {
    const state = stateWith({}, { items: {} })
    expect(validate(state).failures.join(" ")).toContain("items must be an array")
  })

  test("a blanket override key at the top level fails", () => {
    for (const key of ["override", "force", "allow_unverified", "skip_items", "waive"]) {
      const state = stateWith({}, { [key]: true })
      expect(validate(state).failures.join(" ")).toContain("blanket override key")
    }
  })

  test("a blanket override key inside an item fails", () => {
    const state = stateWith({ zstack: { force: true } })
    expect(validate(state).failures.join(" ")).toContain("blanket override key")
  })

  test("an unknown key fails", () => {
    const state = stateWith({}, { status: "done" })
    expect(validate(state).failures.join(" ")).toContain('unknown top-level key: "status"')
  })

  test("a stale hardware fingerprint fails", () => {
    const state = stateWith({}, { hardware_fingerprint: "0".repeat(64) })
    expect(validate(state).failures.join(" ")).toContain("hardware fingerprint is stale")
  })

  test("a hardware fingerprint that is not a sha256 fails", () => {
    const state = stateWith({}, { hardware_fingerprint: "abc" })
    expect(validate(state).failures.join(" ")).toContain("64-character sha256")
  })

  test("an accepted item without evidence fails", () => {
    const state = stateWith({ zstack: { evidence: undefined } })
    expect(validate(state).failures.join(" ")).toContain("evidence path missing")
  })

  test("a missing evidence file fails", () => {
    rmSync(join(root, "docs/evidence/zstack.md"))
    expect(validate(stateWith()).failures.join(" ")).toContain("does not exist")
  })

  test("an empty evidence file fails", () => {
    writeFileSync(join(root, "docs/evidence/zstack.md"), "")
    expect(validate(stateWith()).failures.join(" ")).toContain("empty")
  })

  test("a path traversal fails", () => {
    write("secret.md", evidenceText("zstack"))
    const state = stateWith({ zstack: { evidence: "docs/evidence/../secret.md" } })
    expect(validate(state).failures.join(" ")).toContain('must not contain ".."')
  })

  test("an absolute path fails", () => {
    const state = stateWith({ zstack: { evidence: join(root, "docs/evidence/zstack.md") } })
    expect(validate(state).failures.join(" ")).toContain("must be relative")
  })

  test("a symlink escape fails", () => {
    const outside = join(root, "outside.md")
    writeFileSync(outside, evidenceText("zstack"))
    rmSync(join(root, "docs/evidence/zstack.md"))
    symlinkSync(outside, join(root, "docs/evidence/zstack.md"))
    expect(validate(stateWith()).failures.join(" ")).toContain("symlink")
  })

  test("a directory is not evidence", () => {
    rmSync(join(root, "docs/evidence/zstack.md"))
    mkdirSync(join(root, "docs/evidence/zstack.md"), { recursive: true })
    expect(validate(stateWith()).failures.join(" ")).toContain("regular file")
  })

  test("evidence outside docs/evidence fails", () => {
    write("docs/other/zstack.md", evidenceText("zstack"))
    const state = stateWith({ zstack: { evidence: "docs/other/zstack.md" } })
    expect(validate(state).failures.join(" ")).toContain("directly inside docs/evidence/")
  })

  test("a placeholder reviewer fails", () => {
    const state = stateWith({ zstack: { reviewer: "TBD" } })
    expect(validate(state).failures.join(" ")).toContain("named reviewer")
  })

  test("an invalid date fails", () => {
    expect(validate(stateWith({ zstack: { reviewed_at: "2026-13-01" } })).failures.join(" "))
      .toContain("real date")
    expect(validate(stateWith({ zstack: { reviewed_at: "01/02/2026" } })).failures.join(" "))
      .toContain("real date")
  })

  test("a future date fails", () => {
    const state = stateWith({ zstack: { reviewed_at: "2026-12-31" } })
    expect(validate(state).failures.join(" ")).toContain("not in the future")
  })

  test("an accepted item needs the current source fingerprint", () => {
    const state = stateWith({ zstack: { source_fingerprint: "0".repeat(64) } })
    expect(validate(state).failures.join(" ")).toContain("source_fingerprint")
  })

  test("evidence without a measurement fails", () => {
    writeFileSync(join(root, "docs/evidence/zstack.md"),
      "# Evidence: zstack\n\n- item: zstack\n\nIt looked fine when I checked it by hand, twice.\n"
      + "Padding ".repeat(40) + "\n")
    expect(validate(stateWith()).failures.join(" ")).toContain("no value with a unit")
  })

  test("evidence that does not name its item fails", () => {
    writeFileSync(join(root, "docs/evidence/zstack.md"),
      "# Evidence: something else\n\nA gap of 12.4 mm was measured. ".repeat(6))
    expect(validate(stateWith()).failures.join(" ")).toContain('does not name item "zstack"')
  })

  test("an evidence sha256 mismatch fails and a match passes", () => {
    const bad = validate(stateWith({ zstack: { evidence_sha256: "0".repeat(64) } }))
    expect(bad.failures.join(" ")).toContain("sha256 does not match")
    const digest = new Bun.CryptoHasher("sha256")
      .update(evidenceText("zstack")).digest("hex")
    const good = validate(stateWith({ zstack: { evidence_sha256: digest } }))
    expect(good.failures).toEqual([])
  })
})

describe("hardware fingerprint", () => {
  test("changes when a PCB source changes", () => {
    appendFileSync(join(root, "tscircuit/front.circuit.tsx"), "// moved R9\n")
    expect(computeHardwareFingerprint(root)).not.toBe(fingerprint)
  })

  test("changes when a probe-point pad or position changes", () => {
    expect(FINGERPRINT_FILES).toContain("tscircuit/lib/probe-points.ts")
    appendFileSync(join(root, "tscircuit/lib/probe-points.ts"), "// moved TP7\n")
    expect(computeHardwareFingerprint(root)).not.toBe(fingerprint)
  })

  test("changes when the board electrical policy changes", () => {
    expect(FINGERPRINT_FILES).toContain("tscircuit/lib/electrical.ts")
    appendFileSync(join(root, "tscircuit/lib/electrical.ts"), "// tighter rule\n")
    expect(computeHardwareFingerprint(root)).not.toBe(fingerprint)
  })

  test("changes when the MIDI electrical model changes", () => {
    expect(FINGERPRINT_FILES).toContain("tscircuit/lib/electrical-midi.ts")
    appendFileSync(join(root, "tscircuit/lib/electrical-midi.ts"), "// lower IF\n")
    expect(computeHardwareFingerprint(root)).not.toBe(fingerprint)
  })

  test("changes when a footprint import changes", () => {
    appendFileSync(join(root, "tscircuit/imports/PJ_313D.tsx"), "// pad change\n")
    expect(computeHardwareFingerprint(root)).not.toBe(fingerprint)
  })

  test("does not change for a documentation-only change", () => {
    appendFileSync(join(root, "docs/evidence/README.md"), "\nmore notes\n")
    write("docs/acceptance.md", "# acceptance\n")
    expect(computeHardwareFingerprint(root)).toBe(fingerprint)
  })

  test("changes when the mechanical layout export changes", () => {
    expect(FINGERPRINT_FILES).toContain("mechanical/layout.json")
    appendFileSync(join(root, "mechanical/layout.json"), " ")
    expect(computeHardwareFingerprint(root)).not.toBe(fingerprint)
  })

  test("changes when the mechanical exporter changes", () => {
    expect(FINGERPRINT_FILES).toContain("tscircuit/scripts/export-mechanical.ts")
    appendFileSync(join(root, "tscircuit/scripts/export-mechanical.ts"), "// tweak\n")
    expect(computeHardwareFingerprint(root)).not.toBe(fingerprint)
  })

  // A CAD-algorithm, order-setting or sourcing-selection change can invalidate
  // physical/DFM evidence even when the TS layout is identical.
  for (const [label, rel] of [
    ["the feedthrough bracket generator", "mechanical/feedthrough_bracket.py"],
    ["the bracket geometry module", "mechanical/bracket_geometry.py"],
    ["the STEP/STL assembly builder", "mechanical/step_assembly.py"],
    ["the STEP collision geometry module", "mechanical/step_geometry.py"],
    ["the light-pipe reach computation", "mechanical/light_pipe_reach.py"],
    ["the JLCPCB order settings", "jlc-order.json"],
    ["the curated sourcing selection", "tscircuit/scripts/sourcing-extras.csv"],
  ] as const) {
    test(`changes when ${label} changes`, () => {
      expect(FINGERPRINT_FILES).toContain(rel)
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

  test("a missing hardware source fails loudly", () => {
    rmSync(join(root, "tscircuit/panel.circuit.tsx"))
    expect(() => computeHardwareFingerprint(root)).toThrow()
  })

  test("a missing mechanical source is reported, not skipped", () => {
    rmSync(join(root, "mechanical/layout.json"))
    const report = hardwareFingerprintReport(root)
    expect(report.missing).toEqual(["mechanical/layout.json"])
    expect(report.fingerprint).not.toBe(fingerprint)
    expect(() => computeHardwareFingerprint(root)).toThrow(/mechanical\/layout.json/)
  })
})

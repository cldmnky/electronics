// Machine-readable physical acceptance state and its validator.
//
// Why this exists: `make test` / `make gate` prove that the sources, netlist,
// placement, geometry and simulation agree. They cannot prove that an 11 mm
// spacer stack closes, that a knob fits its bushing, that the 5 V module stays
// under 300 mA on the bench, or that JLCPCB's CAM matches the gerbers. The
// production releases therefore refuse to build, export or delete anything in
// fab/ until every required physical item is ACCEPTED with recorded evidence
// that names a reviewer, a date, the hardware revision it looked at, and at
// least one measurement with a unit.
//
// Rules that make this a gate and not a formality:
//  * every required item id must be present exactly once; unknown ids fail;
//  * "accepted" needs an evidence file in doc/evidence/ (relative path, no
//    traversal, no absolute path, no symlink, not a directory, not empty, at
//    least 200 bytes), a named reviewer, a real past date, a measurement with a
//    unit, and the CURRENT hardware fingerprint both at the top level and on
//    the item;
//  * the hardware fingerprint hashes the canonical hardware sources (both
//    boards' entry files, sections/ and lib/ trees, the panel/CAD algorithms,
//    the packages and locks, the JLCPCB order settings and the curated
//    sourcing/part maps). A hardware change makes old evidence stale; a docs,
//    test or generated-output change does not;
//  * a missing mandatory source fails instead of being skipped, so no
//    acceptance can rest on a partial fingerprint; a file added under a hashed
//    directory changes the fingerprint;
//  * there is no override, force, skip, waive, bypass or exempt key anywhere,
//    at the top level or per item: unknown keys are errors. No flag can clear
//    unresolved hardware.
//
// This validator does not know whether a measurement is true. It enforces that
// a named person recorded one against a specific revision. Inventing evidence
// is a human decision no script can prevent - which is why the state file is
// reviewed in the same commit as the acceptance claim.

import { createHash } from "node:crypto"
import { existsSync, lstatSync, readFileSync, readdirSync, realpathSync, statSync } from "node:fs"
import { join, resolve, sep } from "node:path"

export const SCHEMA = "froemix-hardware-acceptance/1"

export interface RequiredItem {
  id: string
  title: string
  /** What the evidence has to show. A requirement is not evidence. */
  requirement: string
}

/**
 * Every item here is a physical qualification that no software check can make.
 * All of them are PENDING in froemix/hardware-acceptance.json: no bench
 * measurement has been recorded for this project. Do not invent one, and do not
 * remove an item to make a release pass.
 */
export const REQUIRED_ITEMS: RequiredItem[] = [
  {
    id: "stack_11mm",
    title: "Complete 11 mm stack and contact engagement",
    requirement: "Assembled base + euro board pair: 11 mm clear spacers, header shoulder and post contact engagement, front-board rear tails <= 1 mm and J1 + shunts <= 9.5 mm, with the installed depth measured.",
  },
  {
    id: "daisy_socket",
    title: "Daisy 1x20 socket, depth and USB access",
    requirement: "Both 1x20 female Daisy sockets fitted: exact part identity, installed height, insert/remove force, clearance to the euro board and rack, and USB access with the Daisy seated.",
  },
  {
    id: "fasteners",
    title: "<= 5 mm OD fasteners with actual head/washer MPNs",
    requirement: "The real screw-head and washer MPNs and their measured head/washer OD, with clearance to C25, CN1 and the J2 courtyard recorded; the 5 mm OD limit is a requirement until the actual hardware is measured.",
  },
  {
    id: "nav_actuator",
    title: "NAV actuator tilt and retention",
    requirement: "NAV actuator reach through the panel, tilt/travel, retention and return force measured on the printed part and the fitted switch.",
  },
  {
    id: "mode_switch",
    title: "MODE switch travel and preload",
    requirement: "MODE switch travel, preload, actuation force, cap fit and panel hole clearance measured on the real switch and cap.",
  },
  {
    id: "jacks_pots",
    title: "Jack and pot bushing threads and knobs",
    requirement: "Jack and pot bushing thread, panel hole fit, nut/washer seating and knob fit measured on the real parts, including the 3.5 mm jack nose length.",
  },
  {
    id: "panel_rack",
    title: "Panel and rack rail fit",
    requirement: "20HP panel in a real 3U rack: rail engagement, screw positions, panel flatness and clearance to neighbouring modules measured.",
  },
  {
    id: "oled_support",
    title: "OLED support",
    requirement: "OLED module identity, standoff height, window alignment, flex-cable path and clearance to U16 measured with the module fitted.",
  },
  {
    id: "eu_hand_parts",
    title: "EU hand-parts identity",
    requirement: "Every hand-soldered part and loose hardware item identified by an approved EU supplier (RS Components, Electrokit or DigiKey with EU sales) and the exact MPN; no LCSC reference number treated as an approved source.",
  },
  {
    id: "p5v_rail",
    title: "U16 5 V rail <= 300 mA, transients and thermal",
    requirement: "P5V loaded measurement <= 300 mA, start-up and load-step transients, and U16 temperature at the recorded ambient and worst-case load.",
  },
  {
    id: "cv_calibration",
    title: "Analog CV calibration and DAC buffer off",
    requirement: "CV in and CV out calibrated against a reference at the recorded points, with the firmware DAC output buffer confirmed DISABLED (unbuffered 0..VDDA) on the built unit.",
  },
  {
    id: "cv_cable_load",
    title: "CV out stable with <= 10 nF cable/load",
    requirement: "CV out full-scale step and sine response with <= 10 nF cable/load capacitance per output, with overshoot and settle time recorded.",
  },
  {
    id: "mux_sync",
    title: "Mux and sync, powered/unpowered/back-fed",
    requirement: "U1 mux and the sync input in every power order: powered, unpowered and back-fed, with no damage, latch-up or current into an unpowered rail, recorded per rail order.",
  },
  {
    id: "midi_input",
    title: "MIDI receiver threshold and high-current corners",
    requirement: "Receiver threshold against the CA-033 receiver requirement, high-current input current and fault temperature measured at the recorded supply and ambient.",
  },
  {
    id: "midi_output",
    title: "MIDI output legacy headroom, edges and faults",
    requirement: "Transmitter current into legacy receiver loads, rise/fall edges and sustained ring/tip fault temperatures measured; CA-033 compliance is not inferred from a receiver-only calculation.",
  },
  {
    id: "ground_tie",
    title: "Ground/chassis tie",
    requirement: "The single-point ground-to-chassis scheme decided and measured: tie point, tie resistance and the rack/chassis connection recorded.",
  },
  {
    id: "esd",
    title: "ESD",
    requirement: "ESD test on every exposed jack, panel opening and connector, with the applied level, the observed result and the protection parts recorded.",
  },
  {
    id: "dfm_cpl",
    title: "DFM and CPL review",
    requirement: "DFM/CPL review of the exported gerbers, BOM and JLCPCB placement preview for the base board, the euro board and the panel, with findings recorded before any order.",
  },
  {
    id: "first_article",
    title: "First article",
    requirement: "First-article boards and panel built, inspected and measured against the full acceptance list before any production order.",
  },
]

export const REQUIRED_IDS = REQUIRED_ITEMS.map((item) => item.id)

const ITEM_KEYS = new Set([
  "id", "state", "evidence", "evidence_sha256", "reviewer", "reviewed_at",
  "source_fingerprint", "note",
])
const TOP_KEYS = new Set(["schema", "hardware_fingerprint", "items", "note"])
const OVERRIDE_KEYS = /(override|force|skip|waive|bypass|unverified|exempt|accept_all|ignore)/i
const PLACEHOLDER = /^(tbd|todo|n\/?a|unknown|none|-+|\.+)$/i
const ISO_DATE = /^\d{4}-\d{2}-\d{2}$/
const MEASUREMENT = /\b\d+(?:[.,]\d+)?\s?(?:mm|cm|µm|um|mA|µA|uA|mV|µV|ms|µs|μs|us|ns|min|MHz|kHz|Hz|kg|g|N|N·?m|°C|degC|V|A|W|mW|s|h|%|ohm|kΩ|MΩ|Ω|dB|pF|nF|µF|uF)(?![\p{L}\p{N}_])/u

/** Bumped whenever the file set below changes shape; evidence records it. */
export const FINGERPRINT_RECIPE = "froemix-hardware-1"

/**
 * Mandatory hardware sources. They must exist: a missing entry file fails the
 * gate instead of shrinking the fingerprint silently.
 */
export const REQUIRED_FILES = [
  // base board
  "tscircuit/index.circuit.tsx",
  "tscircuit/package.json",
  "tscircuit/bun.lock",
  // euro board + panel
  "euro/index.circuit.tsx",
  "euro/panel.circuit.tsx",
  "euro/package.json",
  "euro/bun.lock",
  // CAD algorithms that turn the sources into fabrication output
  "euro/scripts/panel_release.ts",
  "euro/scripts/panel_check.ts",
  "euro/scripts/panel_preview.ts",
  "euro/scripts/mechanical_release.ts",
  "euro/scripts/mechanical_check.ts",
  // the approved mechanical baseline: hole positions, rail holes, panel artwork
  // and stack numbers are a hardware contract, so a revision of this file must
  // make old evidence stale exactly like a board change
  "euro/fixtures/panel-baseline.json",
  // fabrication order settings
  "jlc-order.json",
  // curated sourcing and part maps (kicad2tscircuit carries EXTRA_COMPONENTS,
  // PART_OVERRIDES, the footprint map and the 3D-model map)
  "tools/kicad2tscircuit.py",
  "tools/lcsc_models.json",
  "tools/sourcing-extras.csv",
]

/**
 * Hashing every file under these directories is what makes "a file added
 * changes the fingerprint" true for new sections, new footprint imports and new
 * CAD helpers. Tests are filtered out (see isHashedFile).
 */
export const FINGERPRINT_DIRS = [
  { dir: "tscircuit/sections", extensions: [".tsx"] },
  { dir: "tscircuit/lib", extensions: [".ts", ".tsx"] },
  { dir: "euro/sections", extensions: [".tsx"] },
  { dir: "euro/lib", extensions: [".ts", ".tsx"] },
  { dir: "euro/scripts", extensions: [".ts"] },
  // every baseline-approval fixture is hardware definition; a new fixture file
  // must change the fingerprint too
  { dir: "euro/fixtures", extensions: [".json"] },
]

/** Generated output and tests are deliberately not hardware definition. */
const EXCLUDED_NAMES = [
  /\.test\.tsx?$/,
  /\.spec\.tsx?$/,
  /^test_.*\.py$/,
  /\.snap\.(svg|png)$/,
]

function isHashedFile(name: string): boolean {
  return !EXCLUDED_NAMES.some((pattern) => pattern.test(name))
}

export interface FingerprintReport {
  /** Hash over the hardware sources that are present. */
  fingerprint: string
  /** Mandatory sources that are absent; the gate fails on these. */
  missing: string[]
  /** Directories that are absent; the gate fails on these too. */
  missingDirectories: string[]
  files: string[]
}

/** Paths of every hashed file, sorted, relative to rootDir. */
export function fingerprintFiles(rootDir: string): string[] {
  const files = new Set<string>(REQUIRED_FILES)
  for (const { dir, extensions } of FINGERPRINT_DIRS) {
    const absolute = join(rootDir, dir)
    if (!existsSync(absolute)) continue
    for (const name of readdirSync(absolute)) {
      if (!isHashedFile(name)) continue
      if (!extensions.some((extension) => name.endsWith(extension))) continue
      if (!statSync(join(absolute, name)).isFile()) continue
      files.add(`${dir}/${name}`)
    }
  }
  return [...files].sort()
}

/**
 * SHA-256 over "sha256(path contents)  path" lines, sorted by path, plus the
 * mandatory sources and directories that are missing. A missing source is
 * never skipped silently: the gate fails, so no acceptance can rest on a
 * partial fingerprint.
 */
export function hardwareFingerprintReport(rootDir: string): FingerprintReport {
  const files = fingerprintFiles(rootDir)
  const lines: string[] = []
  const missing: string[] = []
  const present: string[] = []
  for (const rel of files) {
    const path = join(rootDir, rel)
    if (!existsSync(path)) {
      // Only the mandatory entry files can be missing; the directory scan only
      // returns files that exist.
      missing.push(rel)
      continue
    }
    const digest = createHash("sha256").update(readFileSync(path)).digest("hex")
    lines.push(`${digest}  ${rel}\n`)
    present.push(rel)
  }
  const missingDirectories = FINGERPRINT_DIRS
    .filter(({ dir }) => !existsSync(join(rootDir, dir)))
    .map(({ dir }) => dir)
  return {
    fingerprint: createHash("sha256").update(lines.join("")).digest("hex"),
    missing,
    missingDirectories,
    files: present,
  }
}

/** Throws when a hardware source is missing (used by `--fingerprint`). */
export function computeHardwareFingerprint(rootDir: string): string {
  const report = hardwareFingerprintReport(rootDir)
  if (report.missing.length > 0) {
    throw new Error(`hardware source missing for the fingerprint: ${report.missing.join(", ")}`)
  }
  if (report.missingDirectories.length > 0) {
    throw new Error(`hardware source directory missing for the fingerprint: ${report.missingDirectories.join(", ")}`)
  }
  return report.fingerprint
}

export interface AcceptanceContext {
  rootDir: string
  /** Absolute path of the evidence directory; defaults to <root>/doc/evidence. */
  evidenceDir?: string
  fingerprint: string
  /** Today as YYYY-MM-DD; a reviewed_at after this fails. */
  today?: string
}

export interface ValidationResult {
  failures: string[]
  lines: string[]
  /** Items still pending, with their titles. */
  pending: RequiredItem[]
  accepted: RequiredItem[]
}

type EvidenceResult = { abs: string; error?: undefined } | { abs?: undefined; error: string }

/** True when `child` is `parent` or lies below it, after realpath resolution. */
function isInsideReal(parent: string, child: string): boolean {
  return child === parent || child.startsWith(parent + sep)
}

function realOrNull(path: string): string | null {
  try {
    return realpathSync(path)
  } catch {
    return null
  }
}

/**
 * Resolve one evidence path, refusing anything that leaves the project.
 *
 * The lexical checks alone are not enough: `doc/` or `doc/evidence/` can be a
 * symlink to a directory outside the project, while the final `.md` file looks
 * like an ordinary regular file. So this walks every component BELOW the real
 * project root, rejects a symlink anywhere on that path, and then proves the
 * real file is still inside the real evidence directory and the real project
 * root. A symlink ABOVE the project root is fine - macOS `/tmp` is one - because
 * the walk starts at the real root, not at its lexical spelling.
 *
 * A caller-supplied `evidenceDir` cannot widen the jail: it must itself resolve
 * inside the project root.
 */
export function resolveEvidence(rootDir: string, evidenceDir: string, rel: unknown): EvidenceResult {
  if (typeof rel !== "string" || rel.trim() === "") {
    return { error: "evidence path missing" }
  }
  if (rel.startsWith("/") || /^[A-Za-z]:[\\/]/.test(rel)) {
    return { error: `evidence path must be relative to the project, got ${rel}` }
  }
  if (rel.split(/[\\/]/).includes("..")) {
    return { error: `evidence path must not contain "..": ${rel}` }
  }
  if (!/^doc\/evidence\/[A-Za-z0-9._-]+\.md$/.test(rel)) {
    return { error: `evidence must be a .md file directly inside doc/evidence/: ${rel}` }
  }

  const projectRootAbs = resolve(rootDir)
  const projectRootReal = realOrNull(projectRootAbs)
  if (projectRootReal === null) {
    return { error: `project root does not exist: ${rootDir}` }
  }

  // The evidence directory must live inside the project. This is what stops a
  // caller-supplied directory from becoming an allowed jail for an external
  // file, and it also covers a symlinked doc/ that points outside.
  const evidenceDirAbs = resolve(evidenceDir)
  const evidenceDirReal = realOrNull(evidenceDirAbs)
  if (evidenceDirReal === null) {
    return { error: `evidence directory does not exist: ${evidenceDir}` }
  }
  if (!isInsideReal(projectRootReal, evidenceDirReal)) {
    return { error: `evidence directory must be inside the project: ${evidenceDir}` }
  }

  const abs = resolve(projectRootAbs, rel)
  if (abs !== evidenceDirAbs && !abs.startsWith(evidenceDirAbs + sep)) {
    return { error: `evidence path escapes the evidence directory: ${rel}` }
  }


  // Walk the components BELOW the real project root: `doc`, `doc/evidence`, and
  // never follow a symlink on the way.
  const segments = rel.split("/")
  const fileName = segments[segments.length - 1]!
  let current = projectRootReal
  for (const segment of segments.slice(0, -1)) {
    current = join(current, segment)
    const component = lstatOrNull(current)
    if (component === null) {
      return { error: `evidence path component does not exist: ${rel}` }
    }
    if (component.isSymbolicLink()) {
      return { error: `evidence path must not contain a symlinked directory: ${rel}` }
    }
    if (!component.isDirectory()) {
      return { error: `evidence path component is not a directory: ${rel}` }
    }
  }
  const filePath = join(current, fileName)

  const fileStat = lstatOrNull(filePath)
  if (fileStat === null) {
    return { error: `evidence file does not exist: ${rel}` }
  }
  if (fileStat.isSymbolicLink()) return { error: `evidence must not be a symlink: ${rel}` }
  if (!fileStat.isFile()) return { error: `evidence must be a regular file: ${rel}` }
  if (fileStat.size === 0) return { error: `evidence file is empty: ${rel}` }

  // Defence in depth: the resolved file must still be inside the real evidence
  // directory and the real project root.
  const fileReal = realOrNull(filePath)
  if (fileReal === null) {
    return { error: `evidence file does not resolve: ${rel}` }
  }
  if (!isInsideReal(evidenceDirReal, fileReal)) {
    return { error: `evidence path escapes the evidence directory: ${rel}` }
  }
  if (!isInsideReal(projectRootReal, fileReal)) {
    return { error: `evidence must resolve inside the project: ${rel}` }
  }
  return { abs: filePath }
}

function lstatOrNull(path: string) {
  try {
    return lstatSync(path)
  } catch {
    return null
  }
}

function validDate(value: unknown, today: string): boolean {
  if (typeof value !== "string" || !ISO_DATE.test(value)) return false
  const parsed = Date.parse(`${value}T00:00:00Z`)
  if (Number.isNaN(parsed)) return false
  const normalised = new Date(parsed).toISOString().slice(0, 10)
  if (normalised !== value) return false
  return value <= today
}

export function todayString(): string {
  return new Date().toISOString().slice(0, 10)
}

export function validateAcceptance(state: unknown, ctx: AcceptanceContext): ValidationResult {
  const failures: string[] = []
  const lines: string[] = []
  const pending: RequiredItem[] = []
  const accepted: RequiredItem[] = []
  const evidenceDir = ctx.evidenceDir ?? join(ctx.rootDir, "doc/evidence")
  const today = ctx.today ?? todayString()

  if (typeof state !== "object" || state === null || Array.isArray(state)) {
    return { failures: ["acceptance state is not a JSON object"], lines, pending, accepted }
  }
  const record = state as Record<string, unknown>
  for (const key of Object.keys(record)) {
    if (OVERRIDE_KEYS.test(key)) {
      failures.push(`blanket override key is not allowed: "${key}"`)
    } else if (!TOP_KEYS.has(key)) {
      failures.push(`unknown top-level key: "${key}"`)
    }
  }
  if (record.schema !== SCHEMA) {
    failures.push(`schema must be "${SCHEMA}", got ${JSON.stringify(record.schema)}`)
  }
  if (typeof record.hardware_fingerprint !== "string" || !/^[0-9a-f]{64}$/.test(record.hardware_fingerprint)) {
    failures.push("hardware_fingerprint must be a 64-character sha256 hex string")
  } else if (record.hardware_fingerprint !== ctx.fingerprint) {
    failures.push(
      "hardware fingerprint is stale: the hardware sources changed since this acceptance record "
      + `(recorded ${String(record.hardware_fingerprint).slice(0, 12)}..., `
      + `current ${ctx.fingerprint.slice(0, 12)}...). Re-run every item against the new revision; `
      + "`--fingerprint` prints the current value.",
    )
  }

  const items = record.items
  if (!Array.isArray(items)) {
    failures.push("items must be an array")
    return { failures, lines, pending, accepted }
  }

  const seen = new Map<string, Record<string, unknown>>()
  for (const [index, entry] of items.entries()) {
    if (typeof entry !== "object" || entry === null || Array.isArray(entry)) {
      failures.push(`items[${index}] is not an object`)
      continue
    }
    const item = entry as Record<string, unknown>
    const id = item.id
    if (typeof id !== "string" || id.trim() === "") {
      failures.push(`items[${index}] has no id`)
      continue
    }
    for (const key of Object.keys(item)) {
      if (OVERRIDE_KEYS.test(key)) {
        failures.push(`${id}: blanket override key is not allowed: "${key}"`)
      } else if (!ITEM_KEYS.has(key)) {
        failures.push(`${id}: unknown item key: "${key}"`)
      }
    }
    if (seen.has(id)) {
      failures.push(`duplicate item id: ${id}`)
      continue
    }
    seen.set(id, item)
  }

  for (const required of REQUIRED_ITEMS) {
    const item = seen.get(required.id)
    if (!item) {
      failures.push(`missing required item: ${required.id}`)
      continue
    }
    const stateValue = item.state
    if (stateValue === "pending") {
      pending.push(required)
      lines.push(`PENDING  ${required.id}: ${required.title}`)
      continue
    }
    if (stateValue !== "accepted") {
      failures.push(`${required.id}: state must be "pending" or "accepted", got ${JSON.stringify(stateValue)}`)
      continue
    }

    const missing: string[] = []
    const reviewer = item.reviewer
    if (typeof reviewer !== "string" || reviewer.trim() === "" || PLACEHOLDER.test(reviewer.trim())) {
      missing.push("a named reviewer (not a placeholder)")
    }
    if (!validDate(item.reviewed_at, today)) {
      missing.push(`reviewed_at as a real date (YYYY-MM-DD, not in the future): ${JSON.stringify(item.reviewed_at)}`)
    }
    if (item.source_fingerprint !== ctx.fingerprint) {
      missing.push("source_fingerprint equal to the current hardware fingerprint")
    }

    const evidence = resolveEvidence(ctx.rootDir, evidenceDir, item.evidence)
    if (evidence.error !== undefined) {
      missing.push(evidence.error)
    } else {
      const content = readFileSync(evidence.abs, "utf8")
      if (content.trim().length < 200) {
        missing.push(`${String(item.evidence)} is too short to be evidence (<200 bytes)`)
      }
      if (!content.includes(required.id)) {
        missing.push(`${String(item.evidence)} does not name item "${required.id}"`)
      }
      if (!MEASUREMENT.test(content)) {
        missing.push(`${String(item.evidence)} records no value with a unit (for example "12.4 mm", "118 mA")`)
      }
      if (item.evidence_sha256 !== undefined) {
        const digest = createHash("sha256").update(content).digest("hex")
        if (item.evidence_sha256 !== digest) {
          missing.push(`${String(item.evidence)} sha256 does not match evidence_sha256`)
        }
      }
    }

    if (missing.length > 0) {
      failures.push(`${required.id}: accepted but ${missing.join("; ")}`)
      lines.push(`INVALID  ${required.id}: ${required.title}`)
    } else {
      accepted.push(required)
      lines.push(
        `ACCEPTED ${required.id}: ${required.title} `
        + `(reviewer ${String(item.reviewer)}, ${String(item.reviewed_at)}, ${String(item.evidence)})`,
      )
    }
  }

  for (const id of seen.keys()) {
    if (!REQUIRED_IDS.includes(id)) {
      failures.push(`unknown item id: ${id}`)
    }
  }

  if (pending.length > 0) {
    failures.push(
      `${pending.length} required physical item(s) still pending: ` + pending.map((item) => item.id).join(", "),
    )
  }
  return { failures, lines, pending, accepted }
}

/** Read and parse the acceptance record. A malformed file is a failure. */
export function loadState(path: string): unknown {
  const text = readFileSync(path, "utf8")
  try {
    return JSON.parse(text)
  } catch (error) {
    throw new Error(`${path} is not valid JSON: ${(error as Error).message}`)
  }
}

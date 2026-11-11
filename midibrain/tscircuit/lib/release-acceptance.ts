// Machine-readable physical acceptance state and its validator.
//
// Why: the fab exports used to be produced by `make release` after `make test`
// alone. `make test` proves that the software, netlist and geometry are
// self-consistent; it cannot prove that a light pipe fits. So the release step
// now refuses to delete or write anything in fab/ until every required physical
// item is accepted with recorded evidence that names a reviewer, a date and the
// hardware revision it looked at.
//
// Rules that make this a gate and not a formality:
//  * every item id in REQUIRED_ITEMS must be present, exactly once;
//  * "accepted" needs an evidence file inside docs/evidence/ (relative path, no
//    traversal, no absolute path, no symlink, not a directory, not empty), a
//    named reviewer, a real past date, and the current hardware fingerprint;
//  * the evidence file must name its item and contain at least one recorded
//    measurement with a unit — a prose opinion is not evidence;
//  * the hardware fingerprint is a hash of the canonical PCB/panel sources, so
//    a PCB or panel change invalidates an old acceptance while a docs-only
//    change (including this acceptance record) does not;
//  * there is no override, force, skip or waive key: unknown keys anywhere are
//    an error, so no blanket flag can clear unresolved hardware.
//
// This validator does not know whether a measurement is true. It enforces that
// a named person recorded one against a specific revision. Inventing evidence
// is a human decision that no script can prevent — and that is why the file is
// reviewed in the same commit as the acceptance claim.

import { createHash } from "node:crypto"
import { readFileSync, readdirSync, lstatSync, existsSync } from "node:fs"
import { join, resolve, sep } from "node:path"

export const SCHEMA = "midibrain-hardware-acceptance/1"

export interface RequiredItem {
  id: string
  title: string
  /** What the evidence has to show. */
  requirement: string
}

export const REQUIRED_ITEMS: RequiredItem[] = [
  {
    id: "zstack",
    title: "Complete Z stack",
    requirement: "Measured board, standoff, display, Pi, HAT, cooler and panel thicknesses that add up to a stack, with the installed depth recorded.",
  },
  {
    id: "control_actuator",
    title: "Control actuators",
    requirement: "Knob, button caps and the 5-way nav actuator fitted and measured against the panel: shaft, detent, cap height and direction legends. Include a sample continuity check on each B3F-4055: the Omron B3F drawing (page 6) shows pins 1-2 internally common and 3-4 internally common, with the switch opening/closing between those pairs, and the boards wire pin 1 to ground and pin 3 to the signal. That pairing is a source-design reading, not a sample measurement, so every fitted switch needs its pair checked on the bench.",
  },
  {
    id: "jack_retention",
    title: "Jack retention",
    requirement: "TRS jack barrel, nut and panel-hole fit, tip/ring/sleeve continuity and pull-out retention measured on the sample.",
  },
  {
    id: "light_pipes",
    title: "Light pipes",
    requirement: "Fitted Bivar PLP2-400-C length and LED-to-pipe gap measured; LED visible through each of the three panel holes.",
  },
  {
    id: "display_dsi",
    title: "Display and DSI cable",
    requirement: "Waveshare 4.3 inch DSI panel mounted on the front board at the fitted SMTSO boss pattern with one of the kit DSI cables (50 mm or 120 mm, 15-pin to 22-pin) routed, bend radius and latch verified, and the display underside clearance recorded.",
  },
  {
    id: "display_boss_pattern",
    title: "Display mount threads",
    requirement: "The fitted display mounting confirmed on the physical module: the SMTSO-M2.5 bosses (99.30 x 61.80 pattern) take the spacer screws and the 58.00 x 49.04 plain holes do not, with the plane separation 9.0 mm = 4.0 mm boss projection + 5.0 mm spacer measured on the built stack.",
  },
  {
    id: "display_fpc_idc",
    title: "Display FPC coexistence",
    requirement: "The display FPC plug, its latch and the first cable bend coexist with the front board's top-side parts in the assembled stack: the FPC stack rear-most <= ~12.9 mm against the front board top face 19.05 mm, the routed bend at the display's right edge recorded per the route contract in docs/mechanical-stack.md, and the clearance to the J5 female solder tails and their joints recorded (the quoted 3.20 mm is from the SEATING FACE, so the nominal protrusion below a 1.60 mm PCB is 1.60 mm; fillet and PCB tolerance unmeasured). The recorded FPC connector position is a planning marker only: its identity, side and location are unverified, so no distance from it is claimed. The 2 x 13 link is a direct male/female pair with no plug and no ribbon.",
  },
  {
    id: "interconnect_mate",
    title: "Direct 2 x 13 interconnect mate, stack support and continuity",
    requirement: "The direct 2 x 13 pair fitted and measured before any power is applied: front female Sullins SFH11-PBPC-D13-ST-BK on the front board's bottom face and rear male SBH11-PBPC-D13-ST-BK on the rear board's top face opposite the Pi, with the seat-to-seat board gap measured against the 11.20 +-0.20 mm drawing dimension, the spacer/support that holds that gap recorded with its hardware, the centred polarizing key engaging in one orientation only, all 26 contacts continuity-tested pin for pin against the net list with each pair recorded on its own, the 1.00 mm drill recorded as the male manufacturer recommendation and as a PROVISIONAL assumption for the female (no published female hole), and the male's solder tail recorded as a 3.00 mm SEATING-FACE dimension, i.e. a NOMINAL 1.40 mm protrusion below a 1.60 mm PCB, with the fillet, the PCB tolerance, the Pi hardware and the actual clearance to the Pi PCB and spacer measured (the 3.5 mm Pi spacer is a candidate, not a proven clearance). The measured gap, the keying result and the 26 continuity records are required evidence; this item stays pending until they exist.",
  },
  {
    id: "module_label_sku_read",
    title: "Module label SKU read",
    requirement: "The delivered display module label/SKU read and matched the selected part (Waveshare 4.3inch DSI LCD, ElectroKit 4103 5687) that the drawing and STEP model were sourced from.",
  },
  {
    id: "touch_window_alignment",
    title: "Touch/panel window alignment",
    requirement: "With the display seated in the 107.22 x 68.87 panel window, touch input stays aligned with the visible UI: glass centred in the cutout and touch coordinates agree with the active-area silkscreen window.",
  },
  {
    id: "pi_microsd",
    title: "Pi microSD access",
    requirement: "microSD card inserted and removed with the Pi in the stack and the case closed; recorded clearance.",
  },
  {
    id: "cooler_hat_ssd",
    title: "Cooler, M.2 HAT and SSD",
    requirement: "Active Cooler, M.2 HAT+ and NVMe SSD stacked and measured for height, airflow path and connector clearance.",
  },
  {
    id: "case_fit",
    title: "Case fit",
    requirement: "30HP/3U panel (152.0 mm actual), 20HP base board and every rail/standoff position fitted in the rack case at the recorded installed depth.",
  },
  {
    id: "usb_c_5a_path",
    title: "USB-C 5 A path",
    requirement: "An exact 5 A rated panel feedthrough selected from a primary source (CAB-15455 is 3 A and rejected for this path) with the rating recorded.",
  },
  {
    id: "usb_pd_droop_thermal",
    title: "USB-C PD, droop and thermal",
    requirement: "Full-load PD negotiation, cable voltage droop and connector/feedthrough temperature measured in both orientations.",
  },
  {
    id: "encoder_solder_process",
    title: "Encoder solder process",
    requirement: "Bourns PEC11R process qualified: selective/wave soldering or a validated manual process with recorded profile and inspection.",
  },
  {
    id: "power_header_mount",
    title: "Panel power header on the rear face at the notch",
    requirement: "J_PWR fitted as a right-angle through-hole 1 x 2 header on the front board's REAR (bottom) face near x = -2.5 mm, with its PCB pads on board material below the top relief notch (y < 48 mm) and its pins pointing outward into the notch, using a purchased header whose manufacturer drawing records the body, tail and pin direction. The owner approved a pocket in the printed feedthrough bracket and a provisional generic 2.54 mm DuPont-style female plug on 2026-11-10. The footprint is placed (POWER_HEADER and POWER_HEADER_KEEPOUT in tscircuit/lib/layout.ts, imports/M20_9960245.tsx) and the owner-approved bracket pocket BRACKET_POWER_POCKET is cut by mechanical/feedthrough_bracket.py. No measured housing offset, plug or cable data exists yet: the 4.80 mm datum is unresolved (sheet 1 of 2 missing), the 1.60 mm pad and 8.00 mm plug reach are engineering assumptions, and the plug is a generic DuPont-style housing. The candidate is Harwin M20-9960245, verified ACTIVE with drawing SHA-256 3cd367d6b76e7c6be501578c87a4a9c89721e36a4a8bc36d5e8340646bdcd61b (Ø1.02 +-0.05 mm hole, 2.54 mm pitch, 0.64 +-0.02 mm square pin, 6.10 mm mating length); an independent architect reading fixed the orientation (long mating pins parallel to the PCB, short PC tails perpendicular), so the evidence must record the measured housing offset, the printed pocket, and that the plug and cable fit inside the declared keepout in 3D. Sourcing is unverified: the DigiKey listing was observed once in a live browser on 2026-11-10 and no snapshot is committed, digikey.se returns Cloudflare HTTP 403 to scripted clients, so stock is subject to reconfirmation, this is a checked-date source lead and not an approved DigiKey source, no availability is claimed and the block must not be bypassed; an Electrokit breakaway strip was proposed but not selected by the owner and must not be substituted; the mating plug is a generic DuPont-style housing, so its measured size is required. Evidence must record the header MPN with its drawing, the pad row and body, the plug housing envelope and cable bend, the pocket cut size with the bracket's remaining material, and the clearance to the UMC AUDIO feedthrough's cable tail in that same slot. It must also record the two switch contacts continuity-tested from SW7 through J_PWR to the Pi 5 remote PWR header BEFORE power, that each fitted B3F-4055 closes between its 1-2 and 3-4 common pairs (Omron B3F drawing page 6; a source reading, not a sample), that SW7 is open at rest, and access for soldering, plugging and unplugging with the assembly state. The Pi end of the jumper is NOT a connector: the Raspberry Pi documentation page https://raw.githubusercontent.com/raspberrypi/documentation/master/documentation/asciidoc/computers/raspberry-pi/power-button.adoc (retrieved 2026-11-10) and its official images/j2.jpg show J2 as TWO BARE PADS between the RTC battery connector and the board edge. A female DuPont jumper cannot connect to bare pads, so the Pi end needs a correctly fitted header or a qualified wired connection; its exact pitch, part, clearance, insulation and assembly method are pending, no JST-SH or pad pitch may be assumed, and the wiring must be dry-contact only. Hardware fit is pending: the part is placed and the bracket pocket is cut, but the housing offset, the plug, the pocket print and every clearance are unmeasured.",
  },
  {
    id: "midi_threshold",
    title: "MIDI input threshold",
    requirement: "Measured receiver threshold against the MIDI CA-033 <5 mA requirement at the recorded supply and temperature.",
  },
  {
    id: "midi_receiver_high_current_thermal",
    title: "Receiver high-current and temperature corners",
    requirement: "TLP2361 input current and junction temperature measured at worst-case high-rail/low-VF source corners, with the recorded ambient: recommended 2-6 mA, absolute continuous 10 mA at 25 C derated -0.13 mA/C above 110 C (8.05 mA at 125 C), and the 220 ohm choice justified by measurement.",
  },
  {
    id: "midi_out_legacy",
    title: "MIDI OUT legacy headroom and fault ratings",
    requirement: "Measure low-rail transmitter current into legacy receiver loads, rise/fall time below 2 us, and sustained ring/tip fault temperatures. Review the implemented topology and resistor values against CA-033; do not infer compliance from a receiver-only calculation.",
  },
  {
    id: "midi_interop",
    title: "MIDI interoperability",
    requirement: "Two real MIDI devices (not loopback) exchange note, clock and sysex traffic in both directions with the recorded equipment.",
  },
  {
    id: "midi_current_faults",
    title: "MIDI current-loop faults",
    requirement: "Short, open, reverse and over-current cases on both MIDI inputs and outputs measured against the protection parts. The current analysis covers the board rail only: an externally driven +-12 V mispatch on a MIDI input or output is NOT modelled, so no survival and no exact fault current may be claimed. The owner must first choose the voltage/fault-survival requirement, because the test can be destructive.",
  },
  {
    id: "scope_spi",
    title: "Scoped SPI and READY",
    requirement: "Logic-analyser or scope capture of SPI0 clock/data and READY at the target rate, including the Pico USB and Pi-off cases.",
  },
  {
    id: "scope_power_sequences",
    title: "Scoped power sequencing",
    requirement: "Power-up and power-down order for Pi, Pico, display and 3.3 V rails captured with no backfeed or latch-up. Include the Pi powered with the Pico unpowered and the Pico absent: that case is not assumed safe and its behaviour must be measured.",
  },
  {
    id: "scope_thermal",
    title: "Thermal soak",
    requirement: "Closed-rack thermal soak at the recorded ambient and load, with cooler noise and NVMe temperature recorded.",
  },
  {
    id: "probe_point_access",
    title: "Probe point access",
    requirement: "With the assembly state recorded for each measurement (front board fitted or off, Pi fitted or removed, Pico socketed, Pico USB cable fitted or not), every rear-board probe point TP1-TP15 reached with the intended probe and read on the net the board declares; the pad, the side used, the assembly state and any obstruction recorded. In-rack access is not assumed.",
  },
  {
    id: "abrupt_power_loss",
    title: "Abrupt power loss",
    requirement: "Abrupt removal and re-application of power recorded on the full stack: rail decay order and time captured, Pico/Pi/display state, write integrity of the microSD card or NVMe SSD after a loss during a recording, and any backfeed or latch-up seen. No graceful-shutdown behaviour is assumed.",
  },
  {
    id: "scope_dfm_cpl",
    title: "DFM and CPL review",
    requirement: "JLCPCB DFM/order-preview review of copper, mask, silkscreen, drill and CPL rotation on the engineering prototype material, with findings recorded.",
  },
  {
    id: "autoroute_upstream_warning",
    title: "Upstream autorouting warning",
    requirement: "The tscircuit warning 'Could not save autorouting paths' resolved, or its effect on the routed copper reviewed and recorded.",
  },
  {
    id: "first_article",
    title: "First article",
    requirement: "First-article boards built, inspected and tested against the acceptance list before any production order.",
  },
]

export const REQUIRED_IDS = REQUIRED_ITEMS.map(item => item.id)

const ITEM_KEYS = new Set([
  "id", "state", "evidence", "evidence_sha256", "reviewer", "reviewed_at",
  "source_fingerprint", "note",
])
const TOP_KEYS = new Set(["schema", "hardware_fingerprint", "items", "note"])
const OVERRIDE_KEYS = /(override|force|skip|waive|bypass|unverified|exempt)/i
const PLACEHOLDER = /^(tbd|todo|n\/?a|unknown|none|-+|\.+)$/i
const ISO_DATE = /^\d{4}-\d{2}-\d{2}$/
const MEASUREMENT = /\b\d+(?:[.,]\d+)?\s?(?:mm|cm|mA|mV|ms|µs|μs|us|ns|min|MHz|kHz|Hz|kg|°C|degC|V|A|W|s|h|%|ohm|kΩ|Ω|dB)(?![\p{L}\p{N}_])/u

export const FINGERPRINT_RECIPE = "midibrain-hardware-1"

/** The canonical PCB/panel sources; docs and tests are excluded on purpose. */
export const FINGERPRINT_FILES = [
  "tscircuit/front.circuit.tsx",
  "tscircuit/index.circuit.tsx",
  "tscircuit/panel.circuit.tsx",
  "tscircuit/lib/layout.ts",
  "tscircuit/lib/pin-map.ts",
  // The direct 2 x 13 interconnect: part numbers, the pad table, the quoted
  // manufacturer dimensions and the net table all define the hardware.
  "tscircuit/lib/interconnect.tsx",
  "tscircuit/lib/jack.tsx",
  "tscircuit/lib/panel-art.ts",
  // Board silkscreen artwork prints on the PCB, so it is hardware definition
  // for the same reason the panel artwork is: brand marks, hand-solder hints,
  // pin-1 and polarity marks all reach the fabrication output.
  "tscircuit/lib/silk.tsx",
  // Probe-point pads, positions and their labels are board geometry too.
  "tscircuit/lib/probe-points.ts",
  // The board design rules and the MIDI electrical model feed the netlist,
  // the BOM values and the fabrication rules, so a change is a hardware change.
  "tscircuit/lib/electrical.ts",
  "tscircuit/lib/electrical-midi.ts",
  "tscircuit/lib/oshw.ts",
  "tscircuit/lib/oshw-outline.ts",
  "tscircuit/package.json",
  "tscircuit/bun.lock",
  // The canonical TS-to-CAD bridge and its generated layout are hardware
  // definition too: a change to either must invalidate an old acceptance.
  "tscircuit/scripts/export-mechanical.ts",
  "mechanical/layout.json",
  // The CAD algorithm, the fabrication order settings and the curated
  // cable/actuator selection are physical evidence too: changing them can
  // invalidate a DFM/CPL or fit result even when lib/layout.ts is unchanged.
  "mechanical/feedthrough_bracket.py",
  "mechanical/bracket_geometry.py",
  // The STEP/STL verification model is the same kind of source: it decides
  // which envelopes are drawn, which axis is treated as unknown and whether a
  // collision result is definitive, and it builds the mockups a first article
  // is checked against. A change to any of these must invalidate old evidence.
  "mechanical/step_assembly.py",
  "mechanical/step_geometry.py",
  "mechanical/light_pipe_reach.py",
  // The vendor-model inspector produced the display landmark planes the
  // placement depends on. Changing what it measures can invalidate a first
  // article result, so it is a hardware source too.
  "mechanical/tools/inspect_display_step.py",
  "jlc-order.json",
  "tscircuit/scripts/sourcing-extras.csv",
]

/** imports/*.tsx are footprints; a new import must change the fingerprint. */
function importFiles(rootDir: string): string[] {
  const dir = join(rootDir, "tscircuit/imports")
  if (!existsSync(dir)) return []
  return readdirSync(dir).filter(name => name.endsWith(".tsx")).sort()
    .map(name => `tscircuit/imports/${name}`)
}

export function fingerprintFiles(rootDir: string): string[] {
  return [...FINGERPRINT_FILES, ...importFiles(rootDir)].sort()
}

export interface FingerprintReport {
  /** Hash over the hardware sources that are present. */
  fingerprint: string
  /** Sources that should exist but do not; the gate fails on these. */
  missing: string[]
  files: string[]
}

/**
 * SHA-256 over "sha256(path contents)  path" lines, sorted by path, and the
 * list of expected sources that are absent. A missing source is never skipped
 * silently: the gate fails, so no acceptance can rest on a partial fingerprint.
 */
export function hardwareFingerprintReport(rootDir: string): FingerprintReport {
  const files = fingerprintFiles(rootDir)
  const lines: string[] = []
  const missing: string[] = []
  const present: string[] = []
  for (const rel of files) {
    const path = join(rootDir, rel)
    if (!existsSync(path)) {
      missing.push(rel)
      continue
    }
    const digest = createHash("sha256").update(readFileSync(path)).digest("hex")
    lines.push(`${digest}  ${rel}\n`)
    present.push(rel)
  }
  return {
    fingerprint: createHash("sha256").update(lines.join("")).digest("hex"),
    missing,
    files: present,
  }
}

/** Throws when a hardware source is missing (used by the `--fingerprint` call). */
export function computeHardwareFingerprint(rootDir: string): string {
  const report = hardwareFingerprintReport(rootDir)
  if (report.missing.length > 0) {
    throw new Error(`hardware source missing for the fingerprint: ${report.missing.join(", ")}`)
  }
  return report.fingerprint
}

export interface AcceptanceContext {
  rootDir: string
  evidenceDir?: string
  fingerprint: string
  today?: string
}

export interface ValidationResult {
  failures: string[]
  lines: string[]
  /** Items still pending, with their titles. */
  pending: RequiredItem[]
}

type EvidenceResult = { abs: string; error?: undefined } | { abs?: undefined; error: string }

function resolveEvidence(rootDir: string, evidenceDir: string, rel: unknown): EvidenceResult {
  if (typeof rel !== "string" || rel.trim() === "") {
    return { error: "evidence path missing" }
  }
  if (rel.startsWith("/") || /^[A-Za-z]:[\\/]/.test(rel)) {
    return { error: `evidence path must be relative to the project, got ${rel}` }
  }
  if (rel.split(/[\\/]/).includes("..")) {
    return { error: `evidence path must not contain "..": ${rel}` }
  }
  if (!/^docs\/evidence\/[A-Za-z0-9._-]+\.md$/.test(rel)) {
    return { error: `evidence must be a .md file directly inside docs/evidence/: ${rel}` }
  }
  const abs = resolve(rootDir, rel)
  const dir = resolve(evidenceDir)
  if (abs !== dir && !abs.startsWith(dir + sep)) {
    return { error: `evidence path escapes the evidence directory: ${rel}` }
  }
  let st
  try {
    st = lstatSync(abs)
  } catch {
    return { error: `evidence file does not exist: ${rel}` }
  }
  if (st.isSymbolicLink()) return { error: `evidence must not be a symlink: ${rel}` }
  if (!st.isFile()) return { error: `evidence must be a regular file: ${rel}` }
  if (st.size === 0) return { error: `evidence file is empty: ${rel}` }
  return { abs }
}

function validDate(value: unknown, today: string): boolean {
  if (typeof value !== "string" || !ISO_DATE.test(value)) return false
  const parsed = Date.parse(`${value}T00:00:00Z`)
  if (Number.isNaN(parsed)) return false
  const normalised = new Date(parsed).toISOString().slice(0, 10)
  if (normalised !== value) return false
  return value <= today
}

function todayString(): string {
  return new Date().toISOString().slice(0, 10)
}

export function validateAcceptance(state: unknown, ctx: AcceptanceContext): ValidationResult {
  const failures: string[] = []
  const lines: string[] = []
  const pending: RequiredItem[] = []
  const evidenceDir = ctx.evidenceDir ?? join(ctx.rootDir, "docs/evidence")
  const today = ctx.today ?? todayString()

  if (typeof state !== "object" || state === null || Array.isArray(state)) {
    return { failures: ["acceptance state is not a JSON object"], lines, pending }
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
    failures.push("hardware fingerprint is stale: the PCB/panel sources changed "
      + `since this acceptance record (recorded ${String(record.hardware_fingerprint).slice(0, 12)}..., `
      + `current ${ctx.fingerprint.slice(0, 12)}...)`)
  }

  const items = record.items
  if (!Array.isArray(items)) {
    failures.push("items must be an array")
    return { failures, lines, pending }
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
        missing.push(`${String(item.evidence)} records no value with a unit (e.g. "12.4 mm")`)
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
      lines.push(`ACCEPTED ${required.id}: ${required.title} `
        + `(reviewer ${String(item.reviewer)}, ${String(item.reviewed_at)}, ${String(item.evidence)})`)
    }
  }

  for (const id of seen.keys()) {
    if (!REQUIRED_IDS.includes(id)) {
      failures.push(`unknown item id: ${id}`)
    }
  }

  if (pending.length > 0) {
    failures.push(`${pending.length} required physical item(s) still pending: `
      + pending.map(item => item.id).join(", "))
  }
  return { failures, lines, pending }
}

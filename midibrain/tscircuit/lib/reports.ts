// Parsers and rules for the `tsci check` reports and the built circuit JSON.
//
// Why this exists: `bunx tsci check placement front.circuit.tsx` exits 1 while
// its own counters say "Errors: 0 / Warnings: 0" — the exit code comes from
// informational notes ("rotate R9 180 degrees"). A release gate that trusted
// the exit code would fail on a harmless note; a gate that ignored it would
// hide a real problem. So the report text is parsed against a short allowlist:
//
//  * every report must print its counters, and errors must be 0;
//  * pin_specification may print only the known passive-control warnings
//    (9 on the front board) — a new warning or any error fails;
//  * placement may flag only the pinned informational rotations;
//  * shorts/pcb-style must print their "no issue" marker;
//  * an exit code other than 0 is accepted only for the pinned placement notes,
//    and zero output, a crash line, a missing marker or missing counters fails.
//
// The known upstream autorouting line ("Could not save autorouting paths ...")
// stays visible: it is reported, never counted as a pass, and it is a required
// manufacturing acceptance item (see lib/release-acceptance.ts).
//
// Nothing here approves fabrication: these are design/output consistency
// checks, not measurements.

export interface ParsedCounts {
  errors: number
  warnings: number
}

/** "Errors: 0" / "Warnings: 2"; null when the report did not print them. */
export function parseCounts(output: string): ParsedCounts | null {
  const errors = /^Errors:\s*(\d+)\s*$/m.exec(output)
  const warnings = /^Warnings:\s*(\d+)\s*$/m.exec(output)
  if (!errors || !warnings) return null
  return { errors: Number(errors[1]), warnings: Number(warnings[1]) }
}

/** Bullet lines that follow the "Warnings: N" counter. */
export function parseWarningList(output: string): string[] {
  const lines = output.split("\n")
  const start = lines.findIndex(line => /^Warnings:\s*\d+\s*$/.test(line))
  if (start < 0) return []
  const out: string[] = []
  for (const line of lines.slice(start + 1)) {
    if (/^Errors:/.test(line)) break
    const hit = /^-\s*(.+)$/.exec(line.trim())
    if (hit) out.push(hit[1].trim())
  }
  return out
}

export interface FlaggedComponent {
  ref: string
  issues: string[]
}

/** Components listed under "flagged components:" with their issue lines. */
export function parseFlaggedComponents(output: string): FlaggedComponent[] {
  const out: FlaggedComponent[] = []
  let inFlagged = false
  let current: FlaggedComponent | null = null
  for (const line of output.split("\n")) {
    const trimmed = line.trim()
    if (trimmed === "flagged components:") {
      inFlagged = true
      continue
    }
    if (!inFlagged) continue
    if (trimmed === "placement drc:" || /^Errors:/.test(trimmed)) break
    // Unindented "- R9" starts a component; indented "  - ..." is its issue.
    if (/^- \S+$/.test(line)) {
      current = { ref: line.slice(2).trim(), issues: [] }
      out.push(current)
      continue
    }
    const issue = /^\s+- (.*)$/.exec(line)
    if (issue && current) current.issues.push(issue[1].trim())
  }
  return out
}

/** The upstream autorouting warning that must stay visible and gated. */
export const KNOWN_UPSTREAM_PATTERNS: RegExp[] = [
  /^Could not save autorouting paths for \S+: Routes contain a junction or endpoint that cannot select a unique PCB port$/,
]

export function knownUpstreamLines(output: string): string[] {
  return output.split("\n").map(line => line.trim())
    .filter(line => KNOWN_UPSTREAM_PATTERNS.some(re => re.test(line)))
}

/** Crash / tool-failure signatures that must never be read as a pass. */
export const CRASH_PATTERNS: RegExp[] = [
  /^\s*error:/im,
  /\bUncaught\b/,
  /\bTypeError\b/,
  /\bReferenceError\b/,
  /\bENOENT\b/,
  /\bCannot find module\b/,
  /^\s*at .*\(.*:\d+:\d+\)$/m,
]

export type CheckFormat =
  | "source"
  | "pin_specification"
  | "netlist"
  | "placement"
  | "shorts"
  | "pcb-style"

export interface BoardReportRules {
  /** Formats that print "Errors/Warnings" counters. */
  counters: CheckFormat[]
  /** Warning bullet lines allowed in pin_specification (exact text). */
  allowedPinWarnings: RegExp[]
  /** Placement notes allowed on this board (informational only). */
  allowedPlacementRotations: string[]
  /** Marker text every shorts / pcb-style run must print. */
  markers: Partial<Record<CheckFormat, string>>
}

/** The nine passive-control warnings the front board prints today. */
const PASSIVE_CONTROL_PIN_WARNINGS: RegExp[] = [
  /^source_component_pins_underspecified_warning: All pins on (U5|U6|J5) are underspecified \(no pinAttributes set\)$/,
  /^source_no_power_pin_defined_warning: (U5|U6|J5) has no pin with requires_power=true$/,
  /^source_no_ground_pin_defined_warning: (U5|U6|J5) has no pin with requires_ground=true$/,
]

export const BOARD_RULES: Record<string, BoardReportRules> = {
  front: {
    counters: ["source", "pin_specification", "netlist", "placement"],
    allowedPinWarnings: PASSIVE_CONTROL_PIN_WARNINGS,
    // Informational: the pads are joined on the far side. The build output and
    // the placement consistency check own the real positions.
    allowedPlacementRotations: ["C2", "R9", "R10"],
    markers: {
      shorts: "No shorts detected in front.circuit.tsx",
      "pcb-style": "No PCB style issues detected in front.circuit.tsx",
    },
  },
  rear: {
    counters: ["source", "pin_specification", "netlist", "placement"],
    allowedPinWarnings: [],
    allowedPlacementRotations: [],
    markers: {
      shorts: "No shorts detected in index.circuit.tsx",
      "pcb-style": "No PCB style issues detected in index.circuit.tsx",
    },
  },
}

export interface ReportOutcome {
  failures: string[]
  notes: string[]
}

const ROTATION_ISSUE = /^(\S+) connections cross the routing path between its pads\. Suggested move: rotate \1 180 degrees\.$/

/**
 * Judge one `tsci check <format> <entry>` run.
 * `rule` is the format's rule for this board, `build` is the built JSON kind.
 */
export function analyseReport(
  format: CheckFormat,
  output: string,
  exitCode: number | null,
  rules: BoardReportRules,
): ReportOutcome {
  const failures: string[] = []
  const notes: string[] = []
  const label = `${format}`

  if (!output || output.trim().length === 0) {
    failures.push(`${label}: no output at all (tool produced nothing)`)
    return { failures, notes }
  }
  const crash = CRASH_PATTERNS.filter(re => re.test(output))
  if (crash.length > 0) {
    failures.push(`${label}: output looks like a crash (${crash.map(String).join(", ")})`)
    return { failures, notes }
  }

  for (const line of knownUpstreamLines(output)) {
    notes.push(`${label}: known upstream warning, still a manufacturing gate item: ${line}`)
  }

  const counted = rules.counters.includes(format) || parseCounts(output) !== null
  let statusOk = true
  if (counted) {
    const counts = parseCounts(output)
    if (!counts) {
      failures.push(`${label}: report printed no "Errors:"/"Warnings:" counters`)
      statusOk = false
    } else {
      if (counts.errors !== 0) {
        failures.push(`${label}: ${counts.errors} error(s) reported`)
        statusOk = false
      }
      if (format === "pin_specification") {
        const allowed = parseWarningList(output).filter(
          line => !rules.allowedPinWarnings.some(re => re.test(line)))
        if (allowed.length > 0) {
          failures.push(`${label}: ${allowed.length} unexpected warning(s): ${allowed.join(" | ")}`)
          statusOk = false
        }
        if (counts.warnings > parseWarningList(output).length) {
          failures.push(`${label}: counter says ${counts.warnings} warning(s) but only `
            + `${parseWarningList(output).length} line(s) were printed`)
          statusOk = false
        }
      } else if (counts.warnings !== 0) {
        failures.push(`${label}: ${counts.warnings} warning(s) reported`)
        statusOk = false
      }
    }
  } else {
    const marker = rules.markers[format]
    if (!marker) {
      failures.push(`${label}: no rule for this format (missing marker or counters)`)
      statusOk = false
    } else if (!output.includes(marker)) {
      failures.push(`${label}: expected "${marker}"`)
      statusOk = false
    } else {
      notes.push(`${label}: ${marker}`)
    }
  }

  if (format === "placement") {
    const flagged = parseFlaggedComponents(output)
    if (flagged.length === 0 && /^flagged components:$/m.test(output)) {
      failures.push(`${label}: "flagged components:" section is empty or unparseable`)
      statusOk = false
    }
    for (const component of flagged) {
      notes.push(`${label}: flagged ${component.ref}: ${component.issues.join(" / ")}`)
      if (!rules.allowedPlacementRotations.includes(component.ref)) {
        failures.push(`${label}: unexpected flagged component ${component.ref}`)
        statusOk = false
        continue
      }
      if (component.issues.length === 0) {
        failures.push(`${label}: flagged ${component.ref} without an issue line`)
        statusOk = false
      }
      for (const issue of component.issues) {
        const hit = ROTATION_ISSUE.exec(issue)
        if (!hit || hit[1] !== component.ref) {
          failures.push(`${label}: ${component.ref} has an unexpected issue: ${issue}`)
          statusOk = false
        }
      }
    }
  }

  // Exit code: 1 is accepted only when nothing above failed and the only
  // content is the pinned informational set. Anything else must fail.
  if (exitCode !== 0) {
    if (format !== "placement") {
      failures.push(`${label}: exit code ${exitCode} (only informational placement notes may exit 1)`)
    } else if (!statusOk) {
      failures.push(`${label}: exit code ${exitCode} with failing report content`)
    } else if (!/^flagged components:$/m.test(output)) {
      failures.push(`${label}: exit code ${exitCode} without the pinned informational section`)
    } else {
      notes.push(`${label}: exit code ${exitCode} is from the pinned informational notes only`)
    }
  }
  return { failures, notes }
}

// ---------------------------------------------------------------------------
// Built circuit JSON
// ---------------------------------------------------------------------------

export interface JsonRules {
  /** Pins that are intentionally unconnected, per board key. */
  allowUnconnected: Record<string, string[]>
}

/**
 * Pins the design leaves unconnected on purpose, with the port name as the
 * netlist sees it. Tactile switches pair pins 1-2 and 3-4 internally (so pin 2
 * and pin 4 need no net); the Pi and Pico headers carry unused GPIO and ground
 * pins. A pin that connects later simply disappears from this list. A *new*
 * unconnected pin fails the check, which is how a lost net is caught.
 */
export const UNCONNECTED_ALLOWLIST: Record<string, Record<string, string[]>> = {
  front: {
    SW1: ["pin2", "pin4"],
    SW2: ["pin2", "pin4"],
    SW3: ["pin2", "pin4"],
    SW4: ["pin2", "pin4"],
    SW5: ["pin2", "pin4"],
    SW6: ["pin2", "pin4"],
  },
  rear: {
    J_PICO_R: ["pin15", "pin17", "pin20"],
    J_PI: ["pin3", "pin5", "pin7", "pin8", "pin10", "pin11", "pin12", "pin13",
      "pin15", "pin16", "pin17", "pin26", "pin27", "pin28", "pin29", "pin31",
      "pin32", "pin33", "pin35", "pin36", "pin37", "pin38", "pin40"],
  },
}

export interface JsonElement {
  type?: string
  message?: string
  [key: string]: unknown
}

/** Any emitted error element, plus pins without a trace outside the allowlist. */
export function analyseCircuitJson(
  json: JsonElement[],
  allowUnconnected: Record<string, string[]>,
): ReportOutcome {
  const failures: string[] = []
  const notes: string[] = []

  const errors = json.filter(e => typeof e.type === "string" && /error/.test(e.type))
  for (const error of errors) {
    failures.push(`circuit JSON ${error.type}: ${String(error.message ?? "(no message)")}`)
  }

  const components = new Map<string, string>()
  for (const element of json) {
    if (element.type === "source_component" && typeof element.source_component_id === "string") {
      components.set(element.source_component_id, String(element.name ?? element.source_component_id))
    }
  }
  const ports = new Map<string, { ref: string; pin: string }>()
  for (const element of json) {
    if (element.type !== "source_port" || typeof element.source_port_id !== "string") continue
    const ref = components.get(String(element.source_component_id)) ?? String(element.source_component_id)
    const hints = element.port_hints
    const firstHint = Array.isArray(hints) ? hints[0] : undefined
    ports.set(element.source_port_id, { ref, pin: String(element.name ?? firstHint ?? "?") })
  }

  const unconnected: { ref: string; pin: string }[] = []
  for (const element of json) {
    if (element.type !== "source_pin_missing_trace_warning") continue
    const port = ports.get(String(element.source_port_id))
    if (!port) {
      failures.push(`unconnected pin: port ${String(element.source_port_id)} is not in the JSON`)
      continue
    }
    unconnected.push(port)
  }
  for (const { ref, pin } of unconnected) {
    const allowed = allowUnconnected[ref]
    if (!allowed || !allowed.includes(pin)) {
      failures.push(`unconnected pin ${ref}.${pin} outside the pinned allowlist `
        + `(update UNCONNECTED_ALLOWLIST only for a documented design decision)`)
      continue
    }
    notes.push(`unconnected by design: ${ref}.${pin}`)
  }
  const seen = new Set(unconnected.map(u => `${u.ref}.${u.pin}`))
  for (const [ref, pins] of Object.entries(allowUnconnected)) {
    const left = pins.filter(pin => !seen.has(`${ref}.${pin}`))
    if (left.length > 0) notes.push(`${ref}: allowlisted pins now connected: ${left.join(", ")}`)
  }
  return { failures, notes }
}

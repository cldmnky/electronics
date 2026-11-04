import { describe, expect, test } from "bun:test"
import {
  analyseCircuitJson,
  analyseReport,
  BOARD_RULES,
  knownUpstreamLines,
  parseCounts,
  parseFlaggedComponents,
  parseWarningList,
  UNCONNECTED_ALLOWLIST,
} from "./reports"

const PLACEMENT_LOG = `  worst issues:
- occupied: 16.643% (2386.002mm^2 of 14336mm^2)
- J1: 0.72mm inside bottom edge

flagged components:
- C2
  source placement: placement_mode=none
  resolved placement: center=(-36mm, -42mm) on top
  board edge status: 13.525mm inside bottom edge
  issues:
  - C2 connections cross the routing path between its pads. Suggested move: rotate C2 180 degrees.
- R9
  source placement: placement_mode=none
  issues:
  - R9 connections cross the routing path between its pads. Suggested move: rotate R9 180 degrees.
- R10
  source placement: placement_mode=none
  issues:
  - R10 connections cross the routing path between its pads. Suggested move: rotate R10 180 degrees.

placement drc:
Errors: 0
Warnings: 0
`

const PIN_LOG = `Errors: 0
Warnings: 9
- source_component_pins_underspecified_warning: All pins on U5 are underspecified (no pinAttributes set)
- source_component_pins_underspecified_warning: All pins on U6 are underspecified (no pinAttributes set)
- source_component_pins_underspecified_warning: All pins on J5 are underspecified (no pinAttributes set)
- source_no_power_pin_defined_warning: U5 has no pin with requires_power=true
- source_no_power_pin_defined_warning: U6 has no pin with requires_power=true
- source_no_power_pin_defined_warning: J5 has no pin with requires_power=true
- source_no_ground_pin_defined_warning: U5 has no pin with requires_ground=true
- source_no_ground_pin_defined_warning: U6 has no pin with requires_ground=true
- source_no_ground_pin_defined_warning: J5 has no pin with requires_ground=true
`

describe("parseCounts", () => {
  test("reads both counters", () => {
    expect(parseCounts("Errors: 0\nWarnings: 3\n")).toEqual({ errors: 0, warnings: 3 })
  })
  test("returns null when a counter is missing", () => {
    expect(parseCounts("Errors: 0\n")).toBeNull()
    expect(parseCounts("")).toBeNull()
  })
})

describe("parseWarningList", () => {
  test("keeps the bullet lines only", () => {
    const list = parseWarningList(PIN_LOG)
    expect(list.length).toBe(9)
    expect(list[0]).toContain("All pins on U5")
  })
})

describe("parseFlaggedComponents", () => {
  test("reads refs and their issue lines, ignoring the worst-issue list", () => {
    const flagged = parseFlaggedComponents(PLACEMENT_LOG)
    expect(flagged.map(f => f.ref)).toEqual(["C2", "R9", "R10"])
    expect(flagged[0].issues).toEqual([
      "C2 connections cross the routing path between its pads. Suggested move: rotate C2 180 degrees.",
    ])
  })
  test("a report without the section is empty", () => {
    expect(parseFlaggedComponents("Errors: 0\nWarnings: 0\n")).toEqual([])
  })
})

describe("known upstream warning", () => {
  test("is captured for the report, never dropped silently", () => {
    const line = "Could not save autorouting paths for subcircuit_source_group_0: Routes contain a junction or endpoint that cannot select a unique PCB port"
    expect(knownUpstreamLines(`blah\n${line}\nNo shorts detected in front.circuit.tsx`))
      .toEqual([line])
  })
})

describe("analyseReport placement", () => {
  test("exit 1 with only the pinned rotations passes", () => {
    const r = analyseReport("placement", PLACEMENT_LOG, 1, BOARD_RULES.front)
    expect(r.failures).toEqual([])
    expect(r.notes.join("\n")).toContain("pinned informational notes")
  })
  test("rear board has no allowed rotation", () => {
    const r = analyseReport("placement", PLACEMENT_LOG, 1, BOARD_RULES.rear)
    expect(r.failures.join(" ")).toContain("unexpected flagged component C2")
  })
  test("an unexpected issue text fails", () => {
    const log = PLACEMENT_LOG.replace(
      "rotate R9 180 degrees.",
      "rotate R9 180 degrees. Pads overlap a via.",
    )
    const r = analyseReport("placement", log, 1, BOARD_RULES.front)
    expect(r.failures.join(" ")).toContain("unexpected issue")
  })
  test("a real error in the counters fails even when the exit code is 1", () => {
    const log = PLACEMENT_LOG.replace("Errors: 0", "Errors: 1")
    const r = analyseReport("placement", log, 1, BOARD_RULES.front)
    expect(r.failures.join(" ")).toContain("1 error(s) reported")
  })
  test("exit 1 without the informational section fails", () => {
    const log = "Errors: 0\nWarnings: 0\n"
    const r = analyseReport("placement", log, 1, BOARD_RULES.front)
    expect(r.failures.join(" ")).toContain("without the pinned informational section")
  })
  test("an empty flagged section fails", () => {
    const log = "flagged components:\n\nErrors: 0\nWarnings: 0\n"
    const r = analyseReport("placement", log, 0, BOARD_RULES.front)
    expect(r.failures.join(" ")).toContain("empty or unparseable")
  })
})

describe("analyseReport counters", () => {
  test("zero output fails", () => {
    const r = analyseReport("source", "   \n", 0, BOARD_RULES.front)
    expect(r.failures.join(" ")).toContain("no output")
  })
  test("a crash-looking output fails", () => {
    const r = analyseReport("source", "error: Cannot find module 'x'\n    at y (/a/b.ts:1:2)\n", 1, BOARD_RULES.front)
    expect(r.failures.join(" ")).toContain("crash")
  })
  test("missing counters fail", () => {
    const r = analyseReport("source", "building...\n", 0, BOARD_RULES.front)
    expect(r.failures.join(" ")).toContain("no \"Errors:\"")
  })
  test("a warning on a zero-warning format fails", () => {
    const r = analyseReport("source", "Errors: 0\nWarnings: 1\n", 0, BOARD_RULES.front)
    expect(r.failures.join(" ")).toContain("1 warning(s) reported")
  })
  test("nonzero exit on a counted format fails", () => {
    const r = analyseReport("netlist", "Errors: 0\nWarnings: 0\n", 1, BOARD_RULES.front)
    expect(r.failures.join(" ")).toContain("exit code 1")
  })
})

describe("analyseReport pin_specification", () => {
  test("the nine known passive warnings pass", () => {
    const r = analyseReport("pin_specification", PIN_LOG, 0, BOARD_RULES.front)
    expect(r.failures).toEqual([])
  })
  test("a subset (after a fix) still passes", () => {
    const log = "Errors: 0\nWarnings: 1\n- source_no_ground_pin_defined_warning: J5 has no pin with requires_ground=true\n"
    const r = analyseReport("pin_specification", log, 0, BOARD_RULES.front)
    expect(r.failures).toEqual([])
  })
  test("a new warning fails", () => {
    const log = PIN_LOG.replace(
      "- source_component_pins_underspecified_warning: All pins on J5 are underspecified (no pinAttributes set)",
      "- source_component_pins_underspecified_warning: All pins on J5 are underspecified (no pinAttributes set)\n- source_component_duplicate_name_warning: two components are called J5",
    ).replace("Warnings: 9", "Warnings: 10")
    const r = analyseReport("pin_specification", log, 0, BOARD_RULES.front)
    expect(r.failures.join(" ")).toContain("unexpected warning")
  })
  test("the rear board allows no warning", () => {
    const r = analyseReport("pin_specification", PIN_LOG, 0, BOARD_RULES.rear)
    expect(r.failures.join(" ")).toContain("unexpected warning")
  })
  test("counter larger than printed lines fails", () => {
    const log = "Errors: 0\nWarnings: 3\n- source_no_ground_pin_defined_warning: J5 has no pin with requires_ground=true\n"
    const r = analyseReport("pin_specification", log, 0, BOARD_RULES.front)
    expect(r.failures.join(" ")).toContain("counter says 3")
  })
})

describe("analyseReport markers", () => {
  test("shorts marker required", () => {
    const ok = analyseReport("shorts", "No shorts detected in front.circuit.tsx\n", 0, BOARD_RULES.front)
    expect(ok.failures).toEqual([])
    const bad = analyseReport("shorts", "1 short found\n", 0, BOARD_RULES.front)
    expect(bad.failures.join(" ")).toContain("expected")
  })
  test("pcb-style marker required", () => {
    const bad = analyseReport("pcb-style", "long odd-angle run at R9\n", 0, BOARD_RULES.front)
    expect(bad.failures.join(" ")).toContain("No PCB style issues detected")
  })
})

describe("analyseCircuitJson", () => {
  const board = (ref: string, pin: string) => {
    const json = [
      { type: "source_component", source_component_id: `c_${ref}`, name: ref },
      { type: "source_port", source_port_id: `p_${ref}_${pin}`, source_component_id: `c_${ref}`, name: pin },
      { type: "source_pin_missing_trace_warning", source_port_id: `p_${ref}_${pin}` },
    ]
    return json
  }

  test("allowlisted unconnected pin is a note", () => {
    const r = analyseCircuitJson(board("SW1", "pin2"), UNCONNECTED_ALLOWLIST.front)
    expect(r.failures).toEqual([])
    expect(r.notes.join(" ")).toContain("SW1.pin2")
  })
  test("a newly unconnected pin fails", () => {
    const r = analyseCircuitJson(board("SW3", "pin1"), UNCONNECTED_ALLOWLIST.front)
    expect(r.failures.join(" ")).toContain("SW3.pin1 outside the pinned allowlist")
  })
  test("an emitted error element fails", () => {
    const r = analyseCircuitJson(
      [{ type: "circuit_error", message: "net without a port" }],
      UNCONNECTED_ALLOWLIST.front,
    )
    expect(r.failures.join(" ")).toContain("circuit_error")
  })
  test("a warning about an unknown port fails", () => {
    const r = analyseCircuitJson(
      [{ type: "source_pin_missing_trace_warning", source_port_id: "p_missing" }],
      UNCONNECTED_ALLOWLIST.front,
    )
    expect(r.failures.join(" ")).toContain("not in the JSON")
  })
  test("a pin that became connected is only a note", () => {
    const r = analyseCircuitJson([], UNCONNECTED_ALLOWLIST.rear)
    expect(r.failures).toEqual([])
    expect(r.notes.length).toBeGreaterThan(0)
  })
})

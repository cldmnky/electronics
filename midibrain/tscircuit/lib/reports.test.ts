import { describe, expect, test } from "bun:test"
import {
  analyseCircuitJson,
  analyseReport,
  BOARD_RULES,
  knownUpstreamLines,
  parseCounts,
  parseFlaggedComponents,
  parseWarningList,
  DECLARED_UNCONNECTED,
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

// The rotation allowance is empty for both real boards now (C2/R9/R10 were
// rotated in the source), so the parser machinery is exercised through a rule
// fixture and the real config is tested for "any flag fails".
const ROTATION_ALLOWED = { ...BOARD_RULES.front, allowedPlacementRotations: ["C2", "R9", "R10"] }

describe("analyseReport placement", () => {
  test("a rotation allowance passes when the rule grants it", () => {
    const r = analyseReport("placement", PLACEMENT_LOG, 1, ROTATION_ALLOWED)
    expect(r.failures).toEqual([])
    expect(r.notes.join("\n")).toContain("pinned informational notes")
  })
  test("the real front board config has no rotation allowance any more", () => {
    expect(BOARD_RULES.front.allowedPlacementRotations).toEqual([])
    const r = analyseReport("placement", PLACEMENT_LOG, 1, BOARD_RULES.front)
    expect(r.failures.join(" ")).toContain("unexpected flagged component C2")
  })
  test("a clean placement report passes on the real config", () => {
    const log = "placement summary: no placement issues\n\nboard-edge status:\n- J1: 0.72mm inside bottom edge\n\nErrors: 0\nWarnings: 0\n"
    const r = analyseReport("placement", log, 0, BOARD_RULES.front)
    expect(r.failures).toEqual([])
    expect(parseFlaggedComponents(log)).toEqual([])
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
    const r = analyseReport("placement", log, 1, ROTATION_ALLOWED)
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
  test("a design warning on any format fails", () => {
    const r = analyseReport(
      "source",
      "Errors: 0\nWarnings: 1\n- source_property_ignored_warning: Datasheet information for chip \"U3\" could not be fetched: Datasheet API did not respond within 5 seconds\n"
        .replace("source_property_ignored_warning: Datasheet", "source_component_duplicate_name_warning: Datasheet"),
      0,
      BOARD_RULES.front,
    )
    expect(r.failures.join(" ")).toContain("1 warning(s) reported")
  })
  test("a datasheet fetch timeout is a visible note, not a failure", () => {
    const line = "- source_property_ignored_warning: Datasheet information for chip "
      + "\"U3\" could not be fetched: Datasheet API did not respond within 5 seconds; "
      + "pin attributes may not be populated. Pin attributes may not be populated."
    const r = analyseReport("source", `Errors: 0\nWarnings: 1\n${line}\n`, 0, BOARD_RULES.front)
    expect(r.failures).toEqual([])
    expect(r.notes.join(" ")).toContain("environmental (network)")
  })
  test("a counter that disagrees with the printed lines fails", () => {
    const r = analyseReport("source", "Errors: 0\nWarnings: 1\n", 0, BOARD_RULES.front)
    expect(r.failures.join(" ")).toContain("counter says 1 warning(s)")
  })
  test("nonzero exit on a counted format fails", () => {
    const r = analyseReport("netlist", "Errors: 0\nWarnings: 0\n", 1, BOARD_RULES.front)
    expect(r.failures.join(" ")).toContain("exit code 1")
  })
})

describe("analyseReport pin_specification", () => {
  test("a clean pin report passes on the real config", () => {
    const r = analyseReport("pin_specification", "Errors: 0\nWarnings: 0\n", 0, BOARD_RULES.front)
    expect(r.failures).toEqual([])
    expect(BOARD_RULES.front.allowedPinWarnings).toEqual([])
  })
  test("the former passive-control warnings now fail on both boards", () => {
    // U5/U6 are switches with declared passive pins, so these lines are noise
    // the source no longer produces: any reappearance is a real finding.
    for (const rules of [BOARD_RULES.front, BOARD_RULES.rear]) {
      const r = analyseReport("pin_specification", PIN_LOG, 0, rules)
      expect(r.failures.join(" ")).toContain("unexpected warning")
    }
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

const EXPECT_SW1 = { SW1: ["pin2", "pin4"] }

describe("analyseCircuitJson", () => {
  // A built board: one component, its ports, and the do-not-connect flag the
  // source declaration produces.
  const board = (ref: string, pins: { pin: string; doNotConnect?: boolean }[]) => {
    const json: any[] = [{ type: "source_component", source_component_id: `c_${ref}`, name: ref }]
    for (const { pin, doNotConnect } of pins) {
      json.push({
        type: "source_port",
        source_port_id: `p_${ref}_${pin}`,
        source_component_id: `c_${ref}`,
        name: pin,
        ...(doNotConnect ? { do_not_connect: true } : {}),
      })
    }
    return json
  }

  test("a declared open pin is a note and is counted", () => {
    const r = analyseCircuitJson(
      board("SW1", [{ pin: "pin2", doNotConnect: true }, { pin: "pin4", doNotConnect: true }]),
      EXPECT_SW1,
    )
    expect(r.failures).toEqual([])
    expect(r.notes.join(" ")).toContain("SW1 pin2")
    expect(r.declaredUnconnected).toBe(2)
  })
  test("a missing-trace warning fails even when it is a declared pin", () => {
    // The source declaration is what silences the builder: a warning means the
    // pin was never declared, so it fails rather than being filtered.
    const json = [
      ...board("SW1", [{ pin: "pin2" }, { pin: "pin4", doNotConnect: true }]),
      { type: "source_pin_missing_trace_warning", source_port_id: "p_SW1_pin2" },
    ]
    const r = analyseCircuitJson(json, EXPECT_SW1)
    expect(r.failures.join(" ")).toContain("SW1.pin2 has no net and is not declared")
  })
  test("a declaration without a documented decision fails", () => {
    const r = analyseCircuitJson(board("SW3", [{ pin: "pin1", doNotConnect: true }]), { SW1: ["pin2", "pin4"] })
    expect(r.failures.join(" ")).toContain("without a documented decision: pin1")
  })
  test("a declaration that disappeared fails", () => {
    const r = analyseCircuitJson(board("SW1", [{ pin: "pin4", doNotConnect: true }]), EXPECT_SW1)
    expect(r.failures.join(" ")).toContain("no longer declared do-not-connect: pin2")
  })
  test("an emitted error element fails", () => {
    const r = analyseCircuitJson(
      [{ type: "circuit_error", message: "net without a port" }],
      DECLARED_UNCONNECTED.front,
    )
    expect(r.failures.join(" ")).toContain("circuit_error")
  })
  test("a warning about an unknown port fails", () => {
    const r = analyseCircuitJson(
      [{ type: "source_pin_missing_trace_warning", source_port_id: "p_missing" }],
      DECLARED_UNCONNECTED.front,
    )
    expect(r.failures.join(" ")).toContain("not in the JSON")
  })
})

#!/usr/bin/env python3
"""Guard the mux-input contract (DESIGN-REVIEW R14) on the euro board.

The base board's CD4051 and the Seed ADC pins behind it are 3.3 V-only and have
no clamps on this revision. That is only safe because every M_ADC_* net is a
closed island of panel controls referenced to this board's own P3V3_A/GND:

    pot:      P3V3_A -- 10k -- wiper --> M_ADC_n, other end GND
    joystick: P3V3_A -- 10k -- common --> M_ADC_6, direction ladder to GND
    button:   P3V3_A -- 10k --------------------- M_ADC_7 -- switch --> GND
              P3V3_A -- 1k -- BTN_LED -- LED --> M_ADC_7 (indicator only)

This check walks each M_ADC_* net through the allowed panel-control parts and
fails if the island reaches anything else - another rail, an external jack, or
an unknown component. Without it, a front-board edit could wire a +/-12 V or
5 V signal into the mux and the base board has nothing to stop the damage.

Run from froemix/euro/ after `tsci build`; exit 1 lists every violation.
"""

from __future__ import annotations

import json
import sys
from collections import defaultdict

CIRCUIT = "dist/index/circuit.json"
MUX = [f"M_ADC_{i}" for i in range(8)]

# Panel controls that may sit on a mux net, and the nets their other pins are
# allowed to reach. CN1 is the mating connector: an allowed endpoint, never
# traversed (traversing it would walk into the whole base board).
CONTROLS = {f"RV{i}" for i in range(1, 7)} | {"SW1", "SW2"} | {f"R{i}" for i in range(2, 9)}
ENDPOINTS = {"CN1"}
RAILS = {"GND", "P3V3_A"}
ALLOWED_NETS = set(MUX) | {"P3V3_A", "GND", "JOY_UP", "JOY_DOWN", "JOY_LEFT", "JOY_RIGHT", "BTN_LED"}


def load(path: str):
    with open(path) as handle:
        return json.load(handle)


def net_members(circuit):
    """{net name: {(ref, pin)}} from the source traces, plus the reverse map."""
    refs = {
        e["source_component_id"]: e["name"]
        for e in circuit
        if e["type"] == "source_component"
    }
    ports = {
        e["source_port_id"]: (refs.get(e["source_component_id"]), str(e["pin_number"]))
        for e in circuit
        if e["type"] == "source_port"
    }
    nets = {e["source_net_id"]: e["name"] for e in circuit if e["type"] == "source_net"}
    members: dict[str, set] = defaultdict(set)
    for e in circuit:
        if e["type"] != "source_trace":
            continue
        names = [nets.get(n) for n in e.get("connected_source_net_ids") or []]
        pins = [ports.get(p) for p in e.get("connected_source_port_ids") or []]
        for name in names:
            if not name:
                continue
            for pin in pins:
                if pin and pin[0]:
                    members[name].add(pin)
    return members


def validate(circuit):
    """Return violations for a built circuit; also exercised by mutation tests."""
    members = net_members(circuit)
    by_ref: dict[str, set] = defaultdict(set)
    for name, pins in members.items():
        for ref, _ in pins:
            by_ref[ref].add(name)

    problems: list[str] = []
    for net in MUX:
        if net not in members:
            problems.append(f"{net}: net does not exist")
            continue
        # Walk the island. Only panel controls are traversed; CN1 ends it.
        seen_nets: set[str] = set()
        queue = [net]
        while queue:
            current = queue.pop()
            if current in seen_nets:
                continue
            seen_nets.add(current)
            if current in RAILS:
                # rails are checked (they are in ALLOWED_NETS) but never walked:
                # GND/P3V3_A contain every grounded part on the board.
                continue
            for ref, pin in sorted(members.get(current, ())):
                if ref in ENDPOINTS:
                    continue
                if ref not in CONTROLS:
                    problems.append(
                        f"{net}: reaches {ref}.{pin} via {current}, which is not an "
                        f"allowed panel control or the CN1 endpoint")
                    continue
                for other in by_ref[ref] - {current}:
                    queue.append(other)
        stray = seen_nets - ALLOWED_NETS
        if stray:
            problems.append(
                f"{net}: island reaches {', '.join(sorted(stray))}, outside "
                f"P3V3_A/GND and the panel-control nets")

    # The MODE input needs a real resistor pull-up, not LED leakage. This also
    # catches a future edit that removes R8 while leaving the button circuit.
    components = {
        e["name"]: e for e in circuit
        if e["type"] == "source_component" and e.get("name")
    }
    for removed in ("DL1", "DL2", "DL3", "DL4", "R10", "R11", "R12", "R13",
                    "D2", "D3", "D4", "D5"):
        if removed in components:
            problems.append(
                f"{removed}: audio activity LED parts must stay removed; the "
                f"direct LED load distorts the audio jacks")

    r8 = components.get("R8")
    if not r8:
        problems.append("R8: 10k MODE pull-up is missing")
    else:
        if r8.get("resistance") != 10000:
            problems.append(f"R8: expected 10k, found {r8.get('resistance')!r}")
        if by_ref.get("R8") != {"M_ADC_7", "P3V3_A"}:
            problems.append(f"R8: expected P3V3_A/M_ADC_7, found {sorted(by_ref.get('R8', ()))}")

    return problems


def main() -> None:
    problems = validate(load(CIRCUIT))
    if problems:
        print("mux-input contract VIOLATED (DESIGN-REVIEW R14):")
        for problem in problems:
            print(f"  {problem}")
        sys.exit(1)
    print(f"mux inputs OK: {len(MUX)} M_ADC nets are closed islands of "
          f"{len(CONTROLS)} panel controls referenced only to P3V3_A/GND "
          f"(endpoint CN1); MODE has its own 10k pull-up")


if __name__ == "__main__":
    main()

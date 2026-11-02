#!/usr/bin/env python3
"""Check the built base-board SMT placement contract, not its source comments."""
import json
import sys
from collections import defaultdict
from pathlib import Path

EXPECTED_SMT_PLACEMENTS = 107


def check(circuit):
    sources = {e["source_component_id"]: e for e in circuit if e["type"] == "source_component"}
    components = {e["pcb_component_id"]: e for e in circuit if e["type"] == "pcb_component"}
    pads = defaultdict(list)
    for element in circuit:
        if element["type"] == "pcb_smtpad" and element.get("pcb_component_id"):
            pads[element["pcb_component_id"]].append(element)

    errors, placements = [], []
    for component_id, component_pads in pads.items():
        component = components.get(component_id)
        if component is None:
            errors.append(f"SMT pad has unknown component {component_id}")
            continue
        if component.get("do_not_place"):
            continue  # fiducials and test points are not machine placements
        source = sources.get(component.get("source_component_id"), {})
        ref = source.get("name", component_id)
        placements.append(ref)
        if component.get("layer") != "bottom":
            errors.append(f"{ref}: component layer is {component.get('layer')}, expected bottom")
        for pad in component_pads:
            if pad.get("layer") != "bottom":
                errors.append(f"{ref}: SMT pad {pad.get('pcb_smtpad_id')} is not on bottom")
        if not source.get("supplier_part_numbers", {}).get("jlcpcb"):
            errors.append(f"{ref}: missing JLCPCB supplier part number")
    if len(placements) != EXPECTED_SMT_PLACEMENTS:
        errors.append(f"expected {EXPECTED_SMT_PLACEMENTS} SMT placements, found {len(placements)}")
    return errors


def main():
    path = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent / "dist/index/circuit.json"
    circuit = json.loads(path.read_text())
    errors = check(circuit)
    if errors:
        print("Single-sided SMT check FAILED:", *errors, sep="\n  ", file=sys.stderr)
        return 1
    print(f"Single-sided SMT: {EXPECTED_SMT_PLACEMENTS} placed components, all pads and components on bottom")
    return 0


if __name__ == "__main__":
    sys.exit(main())

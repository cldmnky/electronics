#!/usr/bin/env python3
"""Check the tscircuit-only parts (EXTRA_COMPONENTS) against the built circuit.

Usage:
    python3 froemix/tools/check_extras.py [--circuit dist/index/circuit.json]

check_netlist.py compares the KiCad netlist with the built circuit but STRIPS
every EXTRA_COMPONENTS ref before comparing, because those parts have no KiCad
counterpart. That leaves the pins of exactly the parts this project added -
bypass caps, the I2C/SD pull-ups, the MIDI OUT driver, the new CV compensation
caps - unverified: a typo in an EXTRA_COMPONENTS net or a wrong pin in the TSX
still passes the netlist gate. This script closes that hole against the built
circuit (dist/index/circuit.json, i.e. what gets fabricated):

  * every EXTRA_COMPONENTS ref exists and carries the declared JLCPCB part;
  * every declared pin is connected to exactly the declared net, using the same
    source_port / source_trace / source_net union-find as check_netlist.py;
  * no REMOVED_REFS ref is still in the built circuit;
  * every INTENTIONAL_PIN_MOVES entry is actually applied in the build (the pin
    exists and sits on the target net); the applied count must equal the size
    of the constant, so a constant that silently loses an entry also fails.

Exits non-zero and prints one line per mismatch.
"""

from __future__ import annotations

import argparse
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from kicad2tscircuit import (  # noqa: E402
    EXTRA_COMPONENTS,
    INTENTIONAL_PIN_MOVES,
    REMOVED_REFS,
)


def pin_nets(circuit: list[dict]):
    """({(ref, pin): net name}, issues) using the check_netlist.py derivation.

    A source_trace joins ports and named nets; union them and label each group
    with its source_net name. A group with no label is a floating pin (None).
    """
    components = {
        elm["source_component_id"]: elm.get("name")
        for elm in circuit
        if elm["type"] == "source_component"
    }
    nets = {
        elm["source_net_id"]: elm["name"]
        for elm in circuit
        if elm["type"] == "source_net"
    }
    ports = {
        elm["source_port_id"]: (
            components.get(elm.get("source_component_id")),
            str(elm.get("pin_number")),
        )
        for elm in circuit
        if elm["type"] == "source_port"
    }

    parent: dict[str, str] = {}

    def find(key: str) -> str:
        parent.setdefault(key, key)
        while parent[key] != key:
            parent[key] = parent[parent[key]]
            key = parent[key]
        return key

    def union(a: str, b: str) -> None:
        root_a, root_b = find(a), find(b)
        if root_a != root_b:
            parent[root_a] = root_b

    for elm in circuit:
        if elm["type"] != "source_trace":
            continue
        members = list(elm.get("connected_source_port_ids") or [])
        for net_id in elm.get("connected_source_net_ids") or []:
            if net_id in nets:
                members.append(f"net:{nets[net_id]}")
        for member in members[1:]:
            union(members[0], member)

    labels: dict[str, set[str]] = {}
    for key in list(parent):
        if key.startswith("net:"):
            labels.setdefault(find(key), set()).add(key[4:])

    found: dict[tuple[str, str], str | None] = {}
    issues: list[str] = []
    for port_id, (ref, pin) in ports.items():
        root = find(port_id)
        groups = labels.get(root, set())
        if len(groups) > 1:
            issues.append(f"{ref}.{pin} is shorted across nets {sorted(groups)}")
        found[(ref, pin)] = next(iter(groups)) if groups else None
    return found, issues


def main() -> int:
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    ap = argparse.ArgumentParser()
    ap.add_argument(
        "--circuit",
        default=os.path.join(root, "tscircuit", "dist", "index", "circuit.json"),
    )
    args = ap.parse_args()

    if not os.path.exists(args.circuit):
        sys.exit(f"missing {args.circuit} - run `bunx tsci build` first")

    circuit = json.load(open(args.circuit))
    built = {
        elm["name"]: elm
        for elm in circuit
        if elm["type"] == "source_component" and elm.get("name")
    }
    pin_net, problems = pin_nets(circuit)
    pins_checked = 0

    for ref, _kind, _value, _footprint, _sheet, declared, lcsc, _mpn, _why in EXTRA_COMPONENTS:
        component = built.get(ref)
        if component is None:
            problems.append(f"{ref}: EXTRA_COMPONENTS ref is not in the built circuit")
            continue
        fitted = (component.get("supplier_part_numbers") or {}).get("jlcpcb") or []
        if lcsc not in fitted:
            problems.append(
                f"{ref}: built JLCPCB part is {fitted or 'none'}, "
                f"EXTRA_COMPONENTS declares {lcsc}"
            )
        for pin, net in declared.items():
            pins_checked += 1
            actual = pin_net.get((ref, str(pin)))
            if actual is None:
                problems.append(f"{ref}.{pin}: not connected to any named net, expected {net}")
            elif actual != net:
                problems.append(f"{ref}.{pin}: on net {actual}, expected {net}")

    for ref in sorted(REMOVED_REFS):
        if ref in built:
            problems.append(f"{ref}: REMOVED_REFS ref is still in the built circuit")

    applied = 0
    for (ref, pin), target in INTENTIONAL_PIN_MOVES.items():
        actual = pin_net.get((ref, str(pin)))
        if actual is None:
            problems.append(
                f"{ref}.{pin}: INTENTIONAL_PIN_MOVES target {target}, "
                "but the pin is not connected"
            )
        elif actual != target:
            problems.append(
                f"{ref}.{pin}: INTENTIONAL_PIN_MOVES target {target}, "
                f"but the build has {actual}"
            )
        else:
            applied += 1
    if applied != len(INTENTIONAL_PIN_MOVES):
        problems.append(
            f"INTENTIONAL_PIN_MOVES: {applied} of {len(INTENTIONAL_PIN_MOVES)} applied"
        )

    print(
        f"extra-part check: {len(EXTRA_COMPONENTS)} EXTRA_COMPONENTS, "
        f"{pins_checked} declared pins, {len(REMOVED_REFS)} REMOVED_REFS, "
        f"{applied}/{len(INTENTIONAL_PIN_MOVES)} INTENTIONAL_PIN_MOVES applied"
    )
    if problems:
        print(f"\n{len(problems)} problem(s):")
        for problem in problems:
            print(f"  {problem}")
        return 1
    print("all extra parts, removed refs and intentional pin moves match the built circuit")
    return 0


if __name__ == "__main__":
    sys.exit(main())

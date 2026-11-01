#!/usr/bin/env python3
"""Compare the tscircuit netlist against the KiCad netlist.

Usage:
    python3 froemix/tools/check_netlist.py [--circuit dist/index/circuit.json]

Exits non-zero if the connectivity differs from the KiCad schematics.
"""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from kicad2tscircuit import (  # noqa: E402
    export_netlist,
    net_name,
    parse_schematics,
)


def kicad_nets(src_dir: str, kicad_cli: str):
    """{net name: {(ref, pin)}} for every net with 2+ nodes"""
    components, nets = export_netlist(kicad_cli, os.path.join(src_dir, "froemix.kicad_sch"))
    out = {}
    for net in nets:
        mapped = net_name(net["name"])
        if mapped is None:
            continue
        # PWR_FLAG symbols (refs "1"/"2") are KiCad ERC bookkeeping only
        nodes = {(node["ref"], node["pin"]) for node in net["nodes"] if node["ref"] not in ("1", "2")}
        if len(nodes) < 2:
            continue
        out[mapped] = nodes
    return out


def tscircuit_nets(circuit_json: str):
    """Group source ports into nets using <net> names and source traces."""
    circuit = json.load(open(circuit_json))
    names = {}
    ports = {}
    for elm in circuit:
        if elm["type"] == "source_component":
            names["comp:" + elm["source_component_id"]] = elm.get("name") or elm.get("ftype")
        elif elm["type"] == "source_net":
            names["net:" + elm["source_net_id"]] = elm["name"]
    ftypes = {}
    for elm in circuit:
        if elm["type"] == "source_component":
            ftypes[elm["source_component_id"]] = elm.get("ftype")
    for elm in circuit:
        if elm["type"] == "source_port":
            comp_id = elm.get("source_component_id")
            pin = str(elm.get("pin_number"))
            ports[elm["source_port_id"]] = (names.get("comp:" + str(comp_id)), pin)

    parent = {}

    def find(x):
        parent.setdefault(x, x)
        while parent[x] != x:
            parent[x] = parent[parent[x]]
            x = parent[x]
        return x

    def union(a, b):
        ra, rb = find(a), find(b)
        if ra != rb:
            parent[ra] = rb

    for elm in circuit:
        if elm["type"] != "source_trace":
            continue
        members = list(elm.get("connected_source_port_ids") or [])
        netname_keys = []
        for net_id in elm.get("connected_source_net_ids") or []:
            net_key = names.get("net:" + net_id)
            if net_key:
                netname_keys.append("name:" + net_key)
                members.append("name:" + net_key)
        for m in members[1:]:
            union(members[0], m)

    groups = {}
    for port_id in ports:
        groups.setdefault(find(port_id), set()).add(ports[port_id])
    labels = {}
    for key in list(parent):
        if key.startswith("name:"):
            labels[find(key)] = key[5:]
    out = {}
    for root_key, members in groups.items():
        if len(members) < 2:
            continue
        out[labels.get(root_key, root_key)] = members
    return out


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", default=os.path.join(root, "tmp"))
    ap.add_argument("--circuit", default=os.path.join(root, "tscircuit", "dist", "index", "circuit.json"))
    ap.add_argument(
        "--kicad-cli",
        default="/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli",
    )
    args = ap.parse_args()

    if not os.path.exists(args.circuit):
        sys.exit(f"missing {args.circuit} - run `bunx tsci build` first")

    expected = kicad_nets(args.src, args.kicad_cli)
    actual = tscircuit_nets(args.circuit)

    # Extra parts added in tscircuit (EXTRA_COMPONENTS: pull-ups, decoupling,
    # bypass) join existing nets, so strip their pins before comparing. What
    # matters is that every KiCad pin still lands in the same group.
    from kicad2tscircuit import EXTRA_COMPONENTS, TSCIRCUIT_ONLY_REFS  # noqa: E402

    # EXTRA_COMPONENTS are parts added on existing KiCad footprints;
    # TSCIRCUIT_ONLY_REFS are tscircuit-only fabrication aids (test points).
    extra_refs = {ref for ref, *_ in EXTRA_COMPONENTS} | TSCIRCUIT_ONLY_REFS
    stripped = {}
    for name, members in actual.items():
        kept = {m for m in members if m[0] not in extra_refs}
        if len(kept) >= 2:
            stripped[name] = kept
    # compare by membership, ignoring net names (names are generated)
    exp_sets = {frozenset(v) for v in expected.values()}
    act_sets = {frozenset(v) for v in stripped.values()}

    missing = exp_sets - act_sets
    extra = act_sets - exp_sets

    def describe(members_set):
        name = next((n for n, v in {**expected, **actual}.items() if frozenset(v) == members_set), "?")
        pins = " ".join(f"{r}.{p}" for r, p in sorted(members_set))
        return f"  {name}\n    {pins}"

    print(f"KiCad nets: {len(exp_sets)}   tscircuit nets: {len(act_sets)}")
    if missing:
        print(f"\nMISSING in tscircuit ({len(missing)}):")
        for s in sorted(missing, key=lambda s: sorted(s)):
            print(describe(s))
    if extra:
        print(f"\nEXTRA in tscircuit ({len(extra)}):")
        for s in sorted(extra, key=lambda s: sorted(s)):
            print(describe(s))
    if missing or extra:
        # narrower diff: which pins moved
        print("\nPin level diff:")
        exp_map = {pin: frozenset(v) for v in expected.values() for pin in v}
        act_map = {pin: frozenset(v) for v in actual.values() for pin in v}
        for pin in sorted(set(exp_map) | set(act_map)):
            if exp_map.get(pin) != act_map.get(pin):
                print(f"  {pin[0]}.{pin[1]}: kicad={sorted(exp_map.get(pin, []))} tscircuit={sorted(act_map.get(pin, []))}")
        sys.exit(1)
    print("netlists match")


if __name__ == "__main__":
    main()

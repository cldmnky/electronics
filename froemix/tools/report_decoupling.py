#!/usr/bin/env python3
"""Report the decoupling distance of every cap/IC pair on the board.

Usage:
    python3 froemix/tools/report_decoupling.py [--circuit dist/index/circuit.json]

DESIGN-REVIEW R06 rewrote this report because the old one answered the wrong
question: it took the shortest copper path from *any* cap pad to *any* pad of
the IC, without net identity, layer or the supply pin. A ground leg to a
grounded signal input, or a path that only exists on another layer, could be
reported as decoupling.

What it does now, reading the BUILT circuit (`dist/index/circuit.json`):

  * builds one copper graph per net, keyed by (x, y, layer). Wires join points
    on their layer, a via joins the two layers it spans at its own point, and
    a multi-layer (THT) pad joins its layers vertically. Two nets are never
    bridged and two layers are only joined where copper really joins them.
  * for each (cap, IC) pair it measures the RAIL LEG: from the cap pad on the
    IC's supply net to the IC pin on that same supply net - the pin identity
    comes from the netlist (name + number), never from "any pad".
  * the GND RETURN is reported separately: the cap's GND pad to the IC's GND
    return pin (a pin whose name is GND/VSS/DGND/VMINUS/VEE), restricted to the
    GND net. A decoupling loop is rail leg + GND return; the rail leg alone is
    not the loop inductance. When the IC's return pin exists but sits on a
    DIFFERENT rail (a dual-supply op-amp's VMINUS on VEE) the GND leg is '-' and
    no fallback is attempted: the only pins such a part has on GND are grounded
    signal inputs (TL074 IN1P...), which are not the return and used to be
    credited with a 4.7 mm "loop". The fallback to the nearest pad on GND stays
    for a part with no named return pin at all (U14, a shunt reference).
  * the bottom GND pour is NOT in the graph, so GND numbers are upper bounds.

Every powered IC on the board is covered (see PAIRS). Informational only: it
prints a table and always exits 0. It is not part of `bun run gate`; re-run it
after a placement or routing change and update the distances in AGENTS.md §3.
"""

from __future__ import annotations

import argparse
import heapq
import json
import math
import os

GND_NET = "GND"

# Pin names that mean "supply return" - the pin the decoupling loop closes on.
# Signal pins tied to GND (U13's INA_P, the TL074 non-inverting inputs) are the
# same copper but not the return pin, and the report must not credit them: it
# used to quote U13 -> INA_P, which is a grounded signal input.
GROUND_PIN_NAMES = ("GND", "VSS", "DGND", "VMINUS", "VEE")

# Every powered IC and the capacitor(s) that bypass each of its supply nets.
# (IC, [(supply net, [caps])]). The supply pin is then whatever pin of that IC
# sits on the supply net - taken from the netlist, printed with name + number.
PAIRS = [
    ("U1", [("P3V3_A", ["C31"])]),                     # CD4051, VDD = pin 16
    ("U2", [("VCC", ["C4"]), ("VEE", ["C5"])]),        # TL074 quad, dual supply
    ("U3", [("VCC", ["C10"]), ("VEE", ["C11"])]),
    ("U11", [("VCC", ["C14"]), ("VEE", ["C15"])]),
    ("U12", [("VCC", ["C17"]), ("VEE", ["C18"])]),
    ("U13", [("P3V3_A", ["C19"])]),                    # MCP6002, VDD = pin 8
    ("U14", [("N10V_REF", ["C20"])]),                  # LM4040 shunt reference
    ("U15", [("P3V3_D", ["C28"])]),                    # TLP2362, VCC = pin 6
    ("DaisySeed1", [("P3V3_D", ["C29", "C30"]), ("P5V", ["C1"])]),
    ("U16", [("U16_VIN_P", ["C32"])]),                 # DC/DC input bypass
]

# Two nodes closer than this on the same layer are the same point of copper.
SNAP_MM = 0.001
# How far from its nominal pad centre a port node may sit in the graph.
PORT_SNAP_MM = 0.02


def load(path: str):
    with open(path) as handle:
        return json.load(handle)


def node_key(x: float, y: float, layer: str):
    return (round(x / SNAP_MM) * SNAP_MM, round(y / SNAP_MM) * SNAP_MM, layer)


def add_edge(graph: dict, a, b, length: float) -> None:
    graph.setdefault(a, {})
    graph.setdefault(b, {})
    if length < graph[a].get(b, math.inf):
        graph[a][b] = length
        graph[b][a] = length


def net_names(circuit_json) -> dict[str, str]:
    return {e["source_net_id"]: e["name"] for e in circuit_json if e.get("type") == "source_net"}


def port_nets(circuit_json) -> dict[str, set[str]]:
    """{pcb-ish source_port_id: {net name}} from the source traces."""
    nets = net_names(circuit_json)
    out: dict[str, set[str]] = {}
    for trace in circuit_json:
        if trace.get("type") != "source_trace":
            continue
        names = {nets[n] for n in trace.get("connected_source_net_ids") or [] if n in nets}
        for port_id in trace.get("connected_source_port_ids") or []:
            out.setdefault(port_id, set()).update(names)
    return out


def ports_by_ref(circuit_json):
    """{refdes: [port dict]} with x/y, layers, pin name/number and nets."""
    names = {e["source_component_id"]: e.get("name")
             for e in circuit_json if e.get("type") == "source_component"}
    pcb_ref = {e["pcb_component_id"]: names.get(e["source_component_id"])
               for e in circuit_json if e.get("type") == "pcb_component"}
    source = {e["source_port_id"]: e for e in circuit_json if e.get("type") == "source_port"}
    nets = port_nets(circuit_json)
    out: dict[str, list[dict]] = {}
    for element in circuit_json:
        if element.get("type") != "pcb_port":
            continue
        refdes = pcb_ref.get(element.get("pcb_component_id"))
        origin = source.get(element.get("source_port_id"), {})
        if not refdes:
            continue
        out.setdefault(refdes, []).append({
            "pcb_port_id": element["pcb_port_id"],
            "x": element["x"],
            "y": element["y"],
            "layers": element.get("layers") or [],
            "pin_number": origin.get("pin_number"),
            "pin_name": origin.get("name"),
            "nets": nets.get(element.get("source_port_id"), set()),
        })
    return out


def copper_graphs(circuit_json) -> dict[str, dict]:
    """{net name: {node: {node: length}}}; one graph per net, never mixed."""
    nets = net_names(circuit_json)
    graphs: dict[str, dict] = {}
    for trace in circuit_json:
        if trace.get("type") != "pcb_trace":
            continue
        net = nets.get(trace.get("connection_name"))
        if not net:
            # a trace without a source net cannot be attributed to one; leaving
            # it out keeps two nets from being bridged through a shared pad
            continue
        graph = graphs.setdefault(net, {})
        previous = None
        for point in trace.get("route") or []:
            if point.get("route_type") == "wire":
                here = node_key(point["x"], point["y"], point["layer"])
                if previous is not None and previous[3] == point["layer"]:
                    length = math.hypot(point["x"] - previous[0], point["y"] - previous[1])
                    add_edge(graph, previous[2], here, length)
                previous = (point["x"], point["y"], here, point["layer"])
            else:  # via: joins from_layer and to_layer at one point
                low = node_key(point["x"], point["y"], point["from_layer"])
                high = node_key(point["x"], point["y"], point["to_layer"])
                add_edge(graph, low, high, 0.0)
                previous = (point["x"], point["y"], high, point["to_layer"])

    # multi-layer (THT) pads are one piece of copper through the board
    nets_of_port = port_nets(circuit_json)
    for element in circuit_json:
        if element.get("type") != "pcb_port":
            continue
        layers = element.get("layers") or []
        if len(layers) < 2:
            continue
        first = node_key(element["x"], element["y"], layers[0])
        for net in nets_of_port.get(element.get("source_port_id"), ()):
            graph = graphs.get(net)
            if graph is None:
                continue
            for layer in layers[1:]:
                add_edge(graph, first, node_key(element["x"], element["y"], layer), 0.0)
    return graphs


def find_node(graph: dict, port: dict):
    """The graph node of a pad, snapped within PORT_SNAP_MM on one of its layers."""
    if not graph:
        return None
    candidates = []
    for layer in port["layers"] or [None]:
        exact = node_key(port["x"], port["y"], layer)
        if exact in graph:
            return exact
    for node in graph:
        if port["layers"] and node[2] not in port["layers"]:
            continue
        distance = math.hypot(node[0] - port["x"], node[1] - port["y"])
        if distance <= PORT_SNAP_MM:
            candidates.append((distance, node))
    if not candidates:
        return None
    return min(candidates)[1]


def shortest_path(graph: dict, start, goal) -> float | None:
    if graph is None or start is None or goal is None:
        return None
    best = {start: 0.0}
    queue = [(0.0, start)]
    seen = set()
    while queue:
        distance, node = heapq.heappop(queue)
        if node in seen:
            continue
        seen.add(node)
        if node == goal:
            return distance
        for neighbour, length in graph[node].items():
            candidate = distance + length
            if candidate < best.get(neighbour, math.inf):
                best[neighbour] = candidate
                heapq.heappush(queue, (candidate, neighbour))
    return None


def label(port: dict) -> str:
    number = port.get("pin_number")
    name = port.get("pin_name") or ""
    if number is None:
        return name or "?"
    return f"pin{number} {name}".strip()


def measure(graphs, ports, ref: str, net: str, cap: str, prefer_pin_names=()):
    """(rail leg mm, cap pad label, IC pad label) or (None, reason, None).

    prefer_pin_names narrows the IC pads to those whose source-port name says
    what the pin is (see GROUND_PIN_NAMES); when the IC has none of them, every
    pad on the net stays a candidate and the nearest one wins.
    """
    cap_ports = [p for p in ports.get(cap, []) if net in p["nets"]]
    ic_ports = [p for p in ports.get(ref, []) if net in p["nets"]]
    preferred = [p for p in ic_ports
                 if (p.get("pin_name") or "").upper() in prefer_pin_names]
    if preferred:
        ic_ports = preferred
    if not cap_ports:
        return None, f"{cap} has no pad on {net}", None
    if not ic_ports:
        return None, f"{ref} has no pin on {net}", None
    graph = graphs.get(net)
    best = None
    for cap_port in cap_ports:
        start = find_node(graph, cap_port)
        for ic_port in ic_ports:
            distance = shortest_path(graph, start, find_node(graph, ic_port))
            if distance is not None and (best is None or distance < best[0]):
                best = (distance, cap_port, ic_port)
    if best is None:
        return None, f"no routed {net} path", None
    return best[0], f"pin{best[1]['pin_number']}", label(best[2])


def supply_return_pins(ports, ref: str) -> list[dict]:
    """The pads of `ref` whose pin name says supply return, on any net."""
    return [p for p in ports.get(ref, [])
            if (p.get("pin_name") or "").upper() in GROUND_PIN_NAMES]


def measure_gnd(graphs, ports, ref: str, cap: str):
    """Cap GND pad -> the IC's GND return pin, restricted to the GND net.

    A part whose named return pin is on another rail has no GND leg to report:
    the TL074's return is VMINUS (VEE), and its only pins on GND are grounded
    signal inputs, so falling back to the nearest GND pad would quote the input
    wiring as a decoupling loop. '-' (None) is the honest answer; the rail legs
    already cover that loop. Parts with no named return pin at all (U14, a
    shunt reference) still fall back to the nearest pad on GND.
    """
    returns = supply_return_pins(ports, ref)
    if returns and not any(GND_NET in p["nets"] for p in returns):
        return None, None, None
    return measure(graphs, ports, ref, GND_NET, cap, GROUND_PIN_NAMES)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--circuit", default="dist/index/circuit.json")
    args = parser.parse_args()
    if not os.path.exists(args.circuit):
        raise SystemExit(f"missing {args.circuit} - run `bunx tsci build` first")

    circuit_json = load(args.circuit)
    graphs = copper_graphs(circuit_json)
    ports = ports_by_ref(circuit_json)

    print("Decoupling report - routed copper length along the BUILT traces")
    print(f"  circuit: {args.circuit}")
    print("  rail leg  = cap pad on the IC's supply net -> the IC pin on that same net")
    print("  GND leg   = cap GND pad -> the IC's GND pin; the loop is rail leg + GND leg")
    print("  one graph per net: nets are never bridged, layers only join at vias and")
    print("  THT pads; the bottom GND pour is not in the graph (GND numbers are upper bounds)")
    print()
    header = f"{'IC':11s} {'rail':10s} {'cap':4s} {'cap pad -> IC pin':24s} {'rail leg':>9s} {'GND leg':>9s}"
    print(header)
    print("-" * len(header))

    missing = []
    for ref, rails in PAIRS:
        for net, caps in rails:
            for cap in caps:
                rail, cap_pad, ic_pin = measure(graphs, ports, ref, net, cap)
                gnd, _, gnd_pin = measure_gnd(graphs, ports, ref, cap)
                if rail is None:
                    missing.append((cap, ref, cap_pad))
                    rail_text = "     n/a"
                else:
                    rail_text = f"{rail:9.2f}"
                if gnd is None:
                    gnd_text = "        -" if gnd_pin is None else "     n/a"
                else:
                    gnd_text = f"{gnd:6.2f} {gnd_pin.replace('pin', 'p')}"
                pad_text = f"{cap_pad or '?'} -> {ic_pin or '?'}"
                print(f"{ref:11s} {net:10s} {cap:4s} {pad_text:24s} {rail_text} {gnd_text}")

    print()
    note = (f"{len(PAIRS)} powered ICs, "
            f"{sum(len(c) for _, rails in PAIRS for _, c in rails)} capacitors")
    print(note)
    for cap, ref, reason in missing:
        print(f"  no measurement for {cap} -> {ref}: {reason}")
    print("\nU2/U3/U11/U12 are dual-supply TL074s: their supply return is VMINUS on")
    print("VEE, so they have no GND return pin and the GND leg column is '-' - the two")
    print("rail legs are the two loops, and their only pins on GND are grounded inputs.")
    print("A decoupling loop needs both legs; a rail leg alone is not a loop inductance.")


if __name__ == "__main__":
    main()

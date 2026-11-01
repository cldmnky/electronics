#!/usr/bin/env python3
"""Report the decoupling distance of every cap/IC pair listed in froemix/AGENTS.md.

Usage:
    python3 froemix/tools/report_decoupling.py [--circuit dist/index/circuit.json]

Distance = shortest copper path from any cap pad centre to any pad centre of the
IC it decouples, walking `pcb_trace` segments and vias in the built circuit JSON.
That is the loop the cap actually has to close, so it is the number the layout
rules in AGENTS.md §3 are written against (centre-to-centre distances are much
smaller and hide detours). Coordinates are snapped to 0.05mm so that coincident
trace endpoints and via centres join into one graph node.

Informational only: this prints a table and always exits 0. It is not part of
`bun run gate`; re-run it after any placement or routing change and update the
distances in AGENTS.md §3 if they moved.
"""

from __future__ import annotations

import argparse
import heapq
import json
import math
import os

# (cap, IC) pairs from AGENTS.md §3
PAIRS = [
    ("C4", "U2"),
    ("C5", "U2"),
    ("C10", "U3"),
    ("C11", "U3"),
    ("C14", "U11"),
    ("C15", "U11"),
    ("C17", "U12"),
    ("C18", "U12"),
    ("C19", "U13"),
    ("C20", "U14"),
    ("C28", "U15"),
    ("C1", "DaisySeed1"),
    ("C29", "DaisySeed1"),
    ("C30", "DaisySeed1"),
]

SNAP_MM = 0.05
MAX_NODE_SNAP_MM = 1.0


def load(path: str):
    with open(path) as handle:
        return json.load(handle)


def copper_graph(circuit_json):
    """{node: {node: length}} over pcb_trace segment endpoints."""
    graph: dict[tuple[float, float], dict[tuple[float, float], float]] = {}

    def key(x: float, y: float) -> tuple[float, float]:
        return (round(x / SNAP_MM) * SNAP_MM, round(y / SNAP_MM) * SNAP_MM)

    for element in circuit_json:
        if element.get("type") != "pcb_trace":
            continue
        route = element.get("route") or []
        for start, end in zip(route, route[1:]):
            a, b = key(start["x"], start["y"]), key(end["x"], end["y"])
            length = math.hypot(end["x"] - start["x"], end["y"] - start["y"])
            for src, dst in ((a, b), (b, a)):
                if graph.setdefault(src, {}).get(dst, math.inf) > length:
                    graph[src][dst] = length
    return graph


def component_maps(circuit_json):
    """{refdes: pcb_component_id}, {refdes: pad elements}"""
    names = {
        e["source_component_id"]: e.get("name")
        for e in circuit_json
        if e.get("type") == "source_component"
    }
    pcb_ids = {}
    for element in circuit_json:
        if element.get("type") != "pcb_component":
            continue
        refdes = names.get(element["source_component_id"])
        if refdes:
            pcb_ids.setdefault(refdes, element["pcb_component_id"])
    pads: dict[str, list] = {}
    for element in circuit_json:
        if element.get("type") in ("pcb_smtpad", "pcb_plated_hole"):
            pads.setdefault(element["pcb_component_id"], []).append(element)
    return pcb_ids, pads


def nearest_node(graph, x: float, y: float):
    best = None
    for node in graph:
        distance = math.hypot(node[0] - x, node[1] - y)
        if best is None or distance < best[0]:
            best = (distance, node)
    if best is None or best[0] > MAX_NODE_SNAP_MM:
        return None
    return best[1]


def shortest_path(graph, start, goal) -> float | None:
    if start is None or goal is None:
        return None
    seen = set()
    queue = [(0.0, start)]
    best: dict[tuple[float, float], float] = {start: 0.0}
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


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--circuit", default="dist/index/circuit.json")
    args = parser.parse_args()
    if not os.path.exists(args.circuit):
        raise SystemExit(f"missing {args.circuit} - run `bunx tsci build` first")

    circuit_json = load(args.circuit)
    graph = copper_graph(circuit_json)
    pcb_ids, pads = component_maps(circuit_json)

    print(f"{'cap':5s} {'ic':11s} {'copper (mm)':>11s}")
    for cap, ic in PAIRS:
        if cap not in pcb_ids or ic not in pcb_ids:
            print(f"{cap:5s} {ic:11s} {'missing':>11s}")
            continue
        best = None
        for cap_pad in pads.get(pcb_ids[cap], []):
            start = nearest_node(graph, cap_pad["x"], cap_pad["y"])
            for ic_pad in pads.get(pcb_ids[ic], []):
                goal = nearest_node(graph, ic_pad["x"], ic_pad["y"])
                distance = shortest_path(graph, start, goal)
                if distance is not None and (best is None or distance < best):
                    best = distance
        print(f"{cap:5s} {ic:11s} {best:11.1f}" if best is not None else f"{cap:5s} {ic:11s} {'no path':>11s}")


if __name__ == "__main__":
    main()

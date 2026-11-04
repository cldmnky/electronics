#!/usr/bin/env python3
"""Negative-mutation tests for scripts/check_electrical.py.

Each test copies the built circuit JSON, breaks one physical or electrical
fact, and asserts that the checker FAILS and names the right check. A check
that no mutation can break is not a check, so these tests also prove that the
sample defects they model are detected:

  * 2 x N header pads re-numbered around the perimeter while every net keeps
    its logical pin name (the original footprint bug);
  * Q3 pad 1 = drain and pad 3 = gate (the generic MOSFET mapping);
  * the local bypass capacitor of a logic buffer missing;
  * MIDI Type A ring/tip swapped on an input jack;
  * a trace thinner than the board rule, a via pad below the drill rule, a
    missing ground pour, and the old TLP2362 put back on the BOM.

Run from tscircuit/ after `bun run build`:
    python3 scripts/test_electrical.py
"""

from __future__ import annotations

import copy
import importlib.util
import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DIST = ROOT / "dist"
CHECKER = Path(__file__).resolve().parent / "check_electrical.py"


def load_checker_module():
    spec = importlib.util.spec_from_file_location("check_electrical", CHECKER)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


CE = load_checker_module()


def run_checker(dist: Path) -> subprocess.CompletedProcess:
    return subprocess.run(
        [sys.executable, str(CHECKER), "--dist", str(dist)],
        capture_output=True, text=True,
    )


def load(dist: Path, board: str) -> list[dict]:
    with (dist / board / "circuit.json").open() as fh:
        return json.load(fh)


def save(dist: Path, board: str, circuit: list[dict]) -> None:
    with (dist / board / "circuit.json").open("w") as fh:
        json.dump(circuit, fh)


def port_pin_map(circuit: list[dict]) -> dict[str, dict[int, str]]:
    """component name -> {physical pin number: source_port_id}."""
    components = {c["source_component_id"]: c for c in circuit if c["type"] == "source_component"}
    result: dict[str, dict[int, str]] = {}
    for port in circuit:
        if port["type"] != "source_port":
            continue
        comp = components.get(port.get("source_component_id"))
        if not comp:
            continue
        result.setdefault(comp.get("name"), {})[port.get("pin_number")] = port["source_port_id"]
    return result


def swap_connectivity(circuit: list[dict], component: str, pin_a: int, pin_b: int) -> None:
    """Give two pads each other's net, leaving pad positions untouched."""
    ports = port_pin_map(circuit)
    a, b = ports[component][pin_a], ports[component][pin_b]
    by_id = {p["source_port_id"]: p for p in circuit if p["type"] == "source_port"}
    key_a = by_id[a].get("subcircuit_connectivity_map_key")
    key_b = by_id[b].get("subcircuit_connectivity_map_key")
    by_id[a]["subcircuit_connectivity_map_key"] = key_b
    by_id[b]["subcircuit_connectivity_map_key"] = key_a


def swap_pad_positions(circuit: list[dict], component: str, pin_a: int, pin_b: int) -> None:
    """Move two pads to each other's coordinates, leaving nets on their pins."""
    components = {c["source_component_id"]: c for c in circuit if c["type"] == "source_component"}
    pcb = {c["pcb_component_id"]: c for c in circuit if c["type"] == "pcb_component"}
    ports = port_pin_map(circuit)
    wanted = {ports[component][pin_a], ports[component][pin_b]}
    pcb_ports = {p["pcb_port_id"]: p for p in circuit if p["type"] == "pcb_port"}
    pads = [
        pad for pad in circuit
        if pad["type"] in ("pcb_plated_hole", "pcb_smtpad")
        and components.get(pcb.get(pad.get("pcb_component_id"), {}).get("source_component_id"), {}).get("name") == component
        and pcb_ports.get(pad.get("pcb_port_id"), {}).get("source_port_id") in wanted
    ]
    if len(pads) != 2:
        raise AssertionError(f"expected 2 pads for {component} pins {pin_a}/{pin_b}, found {len(pads)}")
    (pad_a, pad_b) = pads
    pad_a["x"], pad_b["x"] = pad_b["x"], pad_a["x"]
    pad_a["y"], pad_b["y"] = pad_b["y"], pad_a["y"]


def cases() -> list[tuple[str, str, callable]]:
    """(name, expected text in the failing output, mutation function)."""

    def perimeter_header(dist: Path) -> None:
        # Re-number J_PI around the perimeter, as a generic 2-row pinrow
        # footprint does: pins 1..20 down one row, 21..40 back along the other.
        circuit = load(dist, "index")
        pads = [p for p in circuit if p["type"] == "pcb_plated_hole"]
        ports = port_pin_map(circuit)
        pcb_ports = {p["pcb_port_id"]: p for p in circuit if p["type"] == "pcb_port"}
        components = {c["pcb_component_id"]: c for c in circuit if c["type"] == "pcb_component"}
        src = {c["source_component_id"]: c for c in circuit if c["type"] == "source_component"}
        j_pi = [p for p in pads
                if src.get(components[p["pcb_component_id"]]["source_component_id"], {}).get("name") == "J_PI"]
        if len(j_pi) != 40:
            raise AssertionError(f"expected 40 J_PI pads, found {len(j_pi)}")
        by_pin = {}
        for pad in j_pi:
            pin = next(p for p, sid in ports["J_PI"].items()
                       if sid == pcb_ports[pad["pcb_port_id"]]["source_port_id"])
            by_pin[pin] = pad
        xs = sorted({round(p["x"], 3) for p in j_pi})
        ys = sorted({round(p["y"], 3) for p in j_pi})
        for pin, pad in by_pin.items():
            index = pin - 1 if pin <= 20 else 40 - pin
            pad["x"] = xs[index]
            pad["y"] = ys[0] if pin <= 20 else ys[1]
        save(dist, "index", circuit)

    def q3_drain_gate(dist: Path) -> None:
        circuit = load(dist, "index")
        swap_connectivity(circuit, "Q3", 1, 3)
        save(dist, "index", circuit)

    def bypass_missing(dist: Path) -> None:
        # C13 is the local bypass of U3 on the rear board.
        circuit = load(dist, "index")
        keep = []
        components = {c["source_component_id"]: c for c in circuit if c["type"] == "source_component"}
        pcb = {c["pcb_component_id"]: c for c in circuit if c["type"] == "pcb_component"}
        for element in circuit:
            if element["type"] == "source_component" and element.get("name") == "C13":
                continue
            if element["type"] in ("pcb_smtpad", "pcb_solder_paste"):
                comp = components.get(pcb.get(element.get("pcb_component_id"), {}).get("source_component_id"), {})
                if comp.get("name") == "C13":
                    continue
            keep.append(element)
        save(dist, "index", keep)

    def midi_ring_tip_swapped(dist: Path) -> None:
        circuit = load(dist, "front")
        swap_connectivity(circuit, "J1", 1, 2)
        save(dist, "front", circuit)

    def thin_trace(dist: Path) -> None:
        circuit = load(dist, "front")
        for element in circuit:
            if element["type"] == "pcb_trace":
                for point in element.get("route", []):
                    if point.get("route_type") == "wire":
                        point["width"] = 0.15
                break
        save(dist, "front", circuit)

    def small_via_pad(dist: Path) -> None:
        circuit = load(dist, "index")
        for element in circuit:
            if element["type"] == "pcb_via":
                element["outer_diameter"] = 0.3
                break
        save(dist, "index", circuit)

    def pour_missing(dist: Path) -> None:
        circuit = load(dist, "front")
        circuit = [e for e in circuit if e["type"] != "pcb_copper_pour"]
        save(dist, "front", circuit)

    def old_opto(dist: Path) -> None:
        circuit = load(dist, "front")
        for element in circuit:
            if element["type"] == "source_component" and element.get("name") == "U1":
                element["supplier_part_numbers"] = {"jlcpcb": ["C37658"]}
                element["manufacturer_part_number"] = "TLP2362(TPL,E"
        save(dist, "front", circuit)

    return [
        ("header perimeter numbering keeps net names", "J_PI pads use conventional 2 x 20 numbering", perimeter_header),
        ("Q3 pad 1 drain / pad 3 gate", "Q3 pad 1 = gate", q3_drain_gate),
        ("missing local bypass", "C13", bypass_missing),
        ("MIDI IN ring/tip swapped", "Type A polarity", midi_ring_tip_swapped),
        ("trace below the board rule", "at least 0.2 mm wide", thin_trace),
        ("via pad below the drill rule", "via pad is at least 0.45 mm", small_via_pad),
        ("ground pour removed", "GND pour on the bottom layer", pour_missing),
        ("TLP2362 put back on the BOM", "TLP2361", old_opto),
    ]


def main() -> int:
    if not (DIST / "index" / "circuit.json").is_file() or not (DIST / "front" / "circuit.json").is_file():
        print("FAIL: dist/ is missing; run `bun run build` in tscircuit/ first", file=sys.stderr)
        return 1

    failures = 0

    baseline = run_checker(DIST)
    if baseline.returncode != 0:
        print("FAIL baseline: the unmutated build does not pass the checker")
        print(baseline.stdout[-3000:])
        return 1
    print(f"PASS baseline: unmutated build passes ({baseline.stdout.strip().splitlines()[-1]})")

    for name, expected, mutate in cases():
        with tempfile.TemporaryDirectory(prefix="midibrain-electrical-") as tmp:
            work = Path(tmp) / "dist"
            shutil.copytree(DIST, work)
            mutate(work)
            proc = run_checker(work)
            output = proc.stdout + proc.stderr
            ok = proc.returncode != 0 and expected in output
            if ok:
                print(f"PASS negative: {name} is rejected")
            else:
                failures += 1
                print(f"FAIL negative: {name} was not rejected by '{expected}'")
                print(output[-2500:])

    # The point of the header mutation: net names alone cannot see it, so the
    # pad-to-net table of the mutated build still passes while the geometry
    # check fails. Prove both halves of that statement.
    with tempfile.TemporaryDirectory(prefix="midibrain-electrical-") as tmp:
        work = Path(tmp) / "dist"
        shutil.copytree(DIST, work)
        for name, _, mutate in cases():
            if name.startswith("header perimeter"):
                mutate(work)
                break
        proc = run_checker(work)
        output = proc.stdout
        geometry_failed = "FAIL rear: J_PI pads use conventional 2 x 20 numbering" in output
        names_passed = "PASS index: Pi 40-pin header (J_PI) pad-to-net table" in output
        if geometry_failed and names_passed:
            print("PASS negative: perimeter numbering is caught by coordinates, not by net names")
        else:
            failures += 1
            print("FAIL negative: perimeter numbering was not isolated to the coordinate check")
            print(output[-2500:])

    total = len(cases()) + 2
    print(f"\n{total - failures} passed, {failures} failed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())

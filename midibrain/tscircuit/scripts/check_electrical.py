#!/usr/bin/env python3
"""Independent electrical verification of the built MIDI Brain circuit JSON.

Reads ``dist/index/circuit.json`` (rear/base board) and
``dist/front/circuit.json`` (front/UI board) and checks the PHYSICAL pad
geometry, not only logical net names:

  * 2 x N connectors (J_PI, J_FRONT, J5) carry conventional alternating pad
    numbering. A perimeter-numbered footprint can still carry the right net on
    the right logical pin; only the pad coordinates expose it.
  * every pad of the Pi header, the Pico sockets, the front/rear interconnect
    and the four TRS jacks carries the net its physical position requires.
  * Q3 is a 2N7002 with pad 1 = gate, pad 2 = source, pad 3 = drain.
  * the MIDI receivers are TLP2361 (C107626) with pad 1 = anode, 3 = cathode,
    4 = GND, 5 = VO, 6 = VCC, and no BOM line still uses the TLP2362.
  * MIDI TRS Type A polarity through the net graph: ring (DIN 4) is the
    current-source side and tip (DIN 5) the sink side, input and output.
  * each 3.3 V logic buffer has its own 100 nF bypass near its VCC pad.
  * a ground pour sits on the intended layer of each board, the parts stay on
    one side, trace widths and vias meet the board policy, no pad pair is
    closer than the fabrication rule and no routing error is emitted.
  * the analytical worst-case MIDI loop current has margin against the
    installed opto threshold, and the replaced part would not have.

This is a design/output check. It is not a measurement, not a DRC substitute
and not a fabrication release.

Usage: python3 scripts/check_electrical.py [--dist DIR] [--quiet]
Exit code 0 = all checks pass, 1 = at least one check failed.
"""

from __future__ import annotations

import argparse
import json
import math
import sys
from pathlib import Path

# ---------------------------------------------------------------------------
# Expected connectivity, written from the pin maps and the MIDI specification.
# ---------------------------------------------------------------------------

# Raspberry Pi 5 40-pin header, physical positions. Only the pins this design
# uses are listed; every other pin must stay unconnected.
PI_HEADER = {
    1: "PI_3V3",      # Pi 3.3 V, used only as an OE/control reference
    2: "P5V",         # 5 V from the Pi: bulk-capped only, never a Pico supply
    4: "P5V",
    6: "GND",
    9: "GND",
    14: "GND",
    18: "READY_PI",   # BCM24, Pico to Pi status
    19: "SPI_MOSI_PI",
    20: "GND",
    21: "SPI_MISO_PI",
    22: "RESET_PI",   # BCM25, reset request
    23: "SPI_SCLK_PI",
    24: "SPI_CS_PI",
    25: "GND",
    30: "GND",
    34: "GND",
    39: "GND",
}

# Pico 2 module physical pins 1-20 (J_PICO_L, one module side) and 21-40
# (J_PICO_R). The Pico module numbers its own pins around the perimeter, so a
# single-row socket is the correct footprint here.
PICO_LEFT = {
    1: "MIDI_OUT_1_TX", 2: "MIDI_IN_1_RX", 3: "GND", 4: "ENC_SW",
    5: "PLAY_START", 6: "MIDI_OUT_2_TX", 7: "MIDI_IN_2_RX", 8: "GND",
    9: "NAV_UP", 10: "NAV_DOWN", 11: "NAV_LEFT", 12: "NAV_RIGHT",
    13: "GND", 14: "NAV_PRESS", 15: "ENC_A", 16: "ENC_B",
    17: "REC", 18: "GND", 19: "OVER_DUB", 20: "STOP",
}
PICO_RIGHT = {
    1: "SPI_MOSI_PICO", 2: "SPI_CS_PICO", 3: "GND", 4: "SPI_SCLK_PICO",
    5: "SPI_MISO_PICO", 6: "READY_PICO", 7: "PLAY", 8: "GND",
    9: "PLAY_LED", 10: "RUN", 11: "REC_LED", 12: "OVERDUB_LED",
    13: "GND", 14: "SHIFT_EXIT", 16: "P3V3", 18: "GND", 19: "VSYS",
}

# 2 x 13 front/rear interconnect. Identical on both boards: straight ribbon.
INTERCONNECT = {
    1: "P3V3", 2: "GND", 3: "MIDI_IN_1_RX", 4: "MIDI_OUT_1_TX",
    5: "MIDI_IN_2_RX", 6: "MIDI_OUT_2_TX", 7: "NAV_UP", 8: "NAV_DOWN",
    9: "NAV_LEFT", 10: "NAV_RIGHT", 11: "NAV_PRESS", 12: "ENC_A",
    13: "ENC_B", 14: "ENC_SW", 15: "REC", 16: "OVER_DUB",
    17: "STOP", 18: "PLAY", 19: "PLAY_START", 20: "REC_LED",
    21: "OVERDUB_LED", 22: "P3V3", 23: "PLAY_LED", 24: "GND",
    25: "GND", 26: "SHIFT_EXIT",
}

# TRS Type A: tip = DIN 5, ring = DIN 4, sleeve = DIN 2. The PJ-301M-3-B
# footprint places pin 1 = tip, pin 2 = ring, pin 3 = sleeve.
TRS_CONTACT = {1: "tip", 2: "ring", 3: "sleeve"}

# Pads that are deliberately open in this prototype (unused Pico GPIO and the
# second contact pair of each 4-pin tact switch).
ALLOWED_OPEN = {
    ("index", "J_PICO_R", 15),
    ("index", "J_PICO_R", 17),
    ("index", "J_PICO_R", 20),
}
for _sw in ("SW1", "SW2", "SW3", "SW4", "SW5", "SW6"):
    ALLOWED_OPEN.add(("front", _sw, 2))
    ALLOWED_OPEN.add(("front", _sw, 4))

# Toshiba TLP2361 datasheet Rev.5.0 (2015-11-02).
TLP2361 = {
    "lcsc": "C107626",
    "ifhl_max_a": 1.3e-3,        # guaranteed maximum, Ta <= 105 C
    "if_on_min_a": 2e-3,         # recommended on-state current
    "if_on_max_a": 6e-3,
    "if_abs_max_a": 10e-3,
    "vf_min_at_2ma": 1.35,
    "vf_max_at_2ma": 1.65,
    "height_max_mm": 2.2,        # package drawing: 2.1 +-0.1 mm
    "voh_min_at_20ua": 3.2,
}
# Toshiba TLP2362 Rev.6.0 (2016-01-19): the replaced part.
TLP2362 = {
    "lcsc": "C37658",
    "ifhl_max_a": 5.0e-3,
    "if_on_min_a": 7.5e-3,
    "if_on_max_a": 14e-3,
}

# MIDI loop constants: this design's receiver resistor and the sender corners.
RECEIVER_OHM = 220
VF_ASSUMED_HIGH = 1.85   # above the 2 mA datasheet bound; an assumption
SENDERS = {
    "5 V sender, 2 x 220 ohm": {"v": 5.0, "tol": 0.05, "r": 220, "rtol": 0.01},
    "3.3 V sender, 2 x 33 ohm": {"v": 3.3, "tol": 0.05, "r": 33, "rtol": 0.01},
}

# Fabrication and design policy, checked against the emitted copper.
MIN_TRACE_MM = 0.20
MIN_VIA_DRILL_MM = 0.20
MIN_VIA_PAD_MM = 0.45
MIN_PAD_GAP_MM = 0.15
RAIL_PAD_TO_PIN_MAX_MM = 3.0
GROUND_STRAIGHT_MAX_MM = 5.0
IDC_PITCH_MM = 2.54

# Rear board: parts on the bottom, ground plane on top. Front board: parts on
# the top, ground plane on the bottom.
EXPECTED_POUR = {"index": "top", "front": "bottom"}
EXPECTED_SMT_SIDE = {"index": "bottom", "front": "top"}
THT_PARTS = {"J5", "J_PI", "J_FRONT", "J_PICO_L", "J_PICO_R", "J1", "J2", "J3", "J4",
             "SW1", "SW2", "SW3", "SW4", "SW5", "SW6", "U5", "U6"}


# ---------------------------------------------------------------------------
# Geometry
# ---------------------------------------------------------------------------

def _rotated_rect(cx: float, cy: float, w: float, h: float, rotation: float) -> list[tuple[float, float]]:
    theta = math.radians(rotation)
    cos, sin = math.cos(theta), math.sin(theta)
    corners = []
    for dx, dy in ((-w / 2, -h / 2), (w / 2, -h / 2), (w / 2, h / 2), (-w / 2, h / 2)):
        corners.append((cx + dx * cos - dy * sin, cy + dx * sin + dy * cos))
    return corners


def pad_polygon(pad: dict) -> list[tuple[float, float]]:
    """Convex polygon covering a pad, used for clearance measurement.

    Circle pads become their bounding square: the pad is then larger than it
    really is, so a measured gap is never larger than the true gap.
    """
    x, y = pad["x"], pad["y"]
    if pad.get("outer_diameter") is not None:
        d = pad["outer_diameter"]
        return _rotated_rect(x, y, d, d, 0.0)
    if pad.get("outer_width") is not None:
        return _rotated_rect(x, y, pad["outer_width"], pad.get("outer_height", pad["outer_width"]), 0.0)
    if pad.get("rect_pad_width") is not None:
        return _rotated_rect(x, y, pad["rect_pad_width"], pad["rect_pad_height"],
                             pad.get("rect_ccw_rotation", 0.0))
    if pad.get("hole_width") is not None:
        return _rotated_rect(x, y, pad["hole_width"], pad.get("hole_height", pad["hole_width"]), 0.0)
    return _rotated_rect(x, y, pad.get("width", 0.0), pad.get("height", 0.0),
                         pad.get("ccw_rotation", 0.0))


def _axes(poly: list[tuple[float, float]]) -> list[tuple[float, float]]:
    axes = []
    for i in range(len(poly)):
        x1, y1 = poly[i]
        x2, y2 = poly[(i + 1) % len(poly)]
        nx, ny = -(y2 - y1), (x2 - x1)
        length = math.hypot(nx, ny)
        if length > 0:
            axes.append((nx / length, ny / length))
    return axes


def polygon_gap(a: list[tuple[float, float]], b: list[tuple[float, float]]) -> float:
    """Lower bound of the distance between two convex polygons (0 if they overlap).

    Two convex polygons are disjoint when at least one face normal of either
    separates them. The largest such gap is a lower bound of the true distance,
    which makes a minimum-clearance rule sound (it can only under-report).
    """
    separated = False
    gap = 0.0
    for axis in _axes(a) + _axes(b):
        pa = [axis[0] * x + axis[1] * y for x, y in a]
        pb = [axis[0] * x + axis[1] * y for x, y in b]
        if max(pa) <= min(pb):
            separated = True
            gap = max(gap, min(pb) - max(pa))
        elif max(pb) <= min(pa):
            separated = True
            gap = max(gap, min(pa) - max(pb))
    return gap if separated else 0.0


# ---------------------------------------------------------------------------
# Circuit model
# ---------------------------------------------------------------------------

class BoardModel:
    """Pads with physical coordinates, physical pin numbers and net names."""

    def __init__(self, board: str, circuit: list[dict]) -> None:
        self.board = board
        self.circuit = circuit
        self.elements: dict[str, list[dict]] = {}
        for element in circuit:
            self.elements.setdefault(element["type"], []).append(element)

        self.net_name = {}
        for net in self.elements.get("source_net", []):
            key = net.get("subcircuit_connectivity_map_key")
            if key:
                self.net_name[key] = net.get("name")

        components = {c["source_component_id"]: c for c in self.elements.get("source_component", [])}
        self.components_by_name = {c.get("name"): c for c in components.values() if c.get("name")}
        pcb_components = {c["pcb_component_id"]: c for c in self.elements.get("pcb_component", [])}
        source_ports = {p["source_port_id"]: p for p in self.elements.get("source_port", [])}
        pcb_ports = {p["pcb_port_id"]: p for p in self.elements.get("pcb_port", [])}

        self.pcb_of: dict[str, dict] = {}
        for pcb in pcb_components.values():
            self.pcb_of[pcb.get("source_component_id")] = pcb

        self.pads: dict[str, dict[int, dict]] = {}
        self.pads_by_net: dict[str, list[dict]] = {}
        for pad_type in ("pcb_plated_hole", "pcb_smtpad"):
            for pad in self.elements.get(pad_type, []):
                pcb = pcb_components.get(pad.get("pcb_component_id"))
                if not pcb:
                    continue
                src = components.get(pcb.get("source_component_id"))
                if not src:
                    continue
                port = pcb_ports.get(pad.get("pcb_port_id"))
                src_port = source_ports.get(port.get("source_port_id")) if port else None
                pin = src_port.get("pin_number") if src_port else None
                net = self.net_name.get(src_port.get("subcircuit_connectivity_map_key")) if src_port else None
                layers = pad.get("layers") or [pad.get("layer")]
                entry = {
                    "component": src.get("name"),
                    "pin": pin,
                    "net": net,
                    "x": pad["x"],
                    "y": pad["y"],
                    "layers": set(layers),
                    "shape": pad.get("shape"),
                    "raw": pad,
                }
                self.pads.setdefault(entry["component"], {})[pin] = entry
                if net:
                    self.pads_by_net.setdefault(net, []).append(entry)

    def pad(self, component: str, pin: int) -> dict | None:
        return self.pads.get(component, {}).get(pin)

    def component_pins(self, component: str) -> dict[int, dict]:
        return self.pads.get(component, {})

    def pins_on_net(self, net: str) -> set[tuple[str, int]]:
        return {(p["component"], p["pin"]) for p in self.pads_by_net.get(net, [])}

    def all_pads(self) -> list[dict]:
        return [pad for pins in self.pads.values() for pad in pins.values()]

    def smt_side_mismatch(self) -> list[str]:
        want = EXPECTED_SMT_SIDE[self.board]
        bad = []
        for name, comp in self.components_by_name.items():
            if name in THT_PARTS:
                continue
            pcb = self.pcb_of.get(comp["source_component_id"])
            if pcb and pcb.get("layer") != want:
                bad.append(f"{name} on {pcb.get('layer')}")
        return bad


class Checker:
    def __init__(self, dist: Path, quiet: bool = False) -> None:
        self.dist = dist
        self.quiet = quiet
        self.passed = 0
        self.failed: list[str] = []

    def check(self, label: str, ok: bool, detail: str = "") -> bool:
        text = label + (f": {detail}" if detail else "")
        if ok:
            self.passed += 1
            if not self.quiet:
                print(f"PASS {text}")
        else:
            self.failed.append(text)
            print(f"FAIL {text}")
        return ok

    def note(self, text: str) -> None:
        print(f"NOTE {text}")

    def load(self, board: str) -> BoardModel:
        path = self.dist / board / "circuit.json"
        if not path.is_file():
            raise SystemExit(f"missing {path}; run `bun run build` in tscircuit/ first")
        with path.open() as fh:
            return BoardModel(board, json.load(fh))


# ---------------------------------------------------------------------------
# Connector geometry
# ---------------------------------------------------------------------------

def idc_numbering_problems(pads: dict[int, dict], count: int) -> tuple[list[str], dict]:
    """Check conventional 2 x N alternating numbering on real pad coordinates."""
    problems: list[str] = []
    if len(pads) != count:
        problems.append(f"expected {count} pads, found {len(pads)}")
    got = sorted(p for p in pads if isinstance(p, int))
    if got != list(range(1, count + 1)):
        problems.append(f"pad numbers are not 1..{count}")
        return problems, {}

    xs = [pads[p]["x"] for p in got]
    ys = [pads[p]["y"] for p in got]
    long_axis = "x" if max(xs) - min(xs) >= max(ys) - min(ys) else "y"
    short_axis = "y" if long_axis == "x" else "x"
    columns = sorted({round(pads[p][long_axis], 3) for p in got})
    rows = sorted({round(pads[p][short_axis], 3) for p in got})
    if len(rows) != 2:
        problems.append(f"expected 2 rows across {short_axis}, found {rows}")
        return problems, {}
    if abs((rows[1] - rows[0]) - IDC_PITCH_MM) > 0.02:
        problems.append(f"row spacing {rows[1] - rows[0]:.3f} mm is not {IDC_PITCH_MM} mm")
    if len(columns) != count // 2:
        problems.append(f"expected {count // 2} columns along {long_axis}, found {len(columns)}")
    for a, b in zip(columns, columns[1:]):
        if abs((b - a) - IDC_PITCH_MM) > 0.02:
            problems.append(f"column pitch {b - a:.3f} mm is not {IDC_PITCH_MM} mm")

    row_of_one = round(pads[1][short_axis], 3)
    other_row = [r for r in rows if r != row_of_one][0]
    # Pin 1 may sit at either end of the long axis, but pin 3 must advance one
    # column in the same direction for every later pair.
    direction = 1.0 if pads[3][long_axis] > pads[1][long_axis] else -1.0
    ordered = columns if direction > 0 else list(reversed(columns))
    for pin in range(1, count + 1):
        pad = pads[pin]
        want_column = (pin - 1) // 2
        want_row = row_of_one if pin % 2 else other_row
        column = round(pad[long_axis], 3)
        column_index = ordered.index(column) if column in ordered else -1
        if column_index != want_column or abs(pad[short_axis] - want_row) > 0.02:
            problems.append(
                f"pin {pin} sits in column {column_index} of row {pad[short_axis]:.2f}; "
                f"conventional numbering needs column {want_column} of row {want_row:.2f} "
                "(this is what perimeter numbering looks like)"
            )
            break
    return problems, {"rows": rows, "columns": len(columns), "long_axis": long_axis}


def check_pad_table(checker: Checker, model: BoardModel, component: str, table: dict[int, str],
                    label: str, count: int) -> None:
    pads = model.component_pins(component)
    if not pads:
        checker.check(f"{model.board}: {component} present", False, "component or pads missing")
        return
    problems = []
    for pin in range(1, count + 1):
        pad = pads.get(pin)
        want = table.get(pin)
        if pad is None:
            problems.append(f"pin {pin} missing")
            continue
        if want is None:
            if pad["net"] is not None:
                problems.append(f"pin {pin} carries {pad['net']!r} but must stay open")
            continue
        if pad["net"] != want:
            problems.append(f"pin {pin} on {pad['net']!r}, expected {want!r}")
    checker.check(f"{model.board}: {label} pad-to-net table", not problems, "; ".join(problems[:6]))


# ---------------------------------------------------------------------------
# MIDI current loop
# ---------------------------------------------------------------------------

def loop_bounds(sender: dict) -> tuple[float, float]:
    v_low = sender["v"] * (1 - sender["tol"])
    v_high = sender["v"] * (1 + sender["tol"])
    r_low = sender["r"] * (1 - sender["rtol"]) * 2 + RECEIVER_OHM * (1 - sender["rtol"])
    r_high = sender["r"] * (1 + sender["rtol"]) * 2 + RECEIVER_OHM * (1 + sender["rtol"])
    return (v_low - VF_ASSUMED_HIGH) / r_high, (v_high - TLP2361["vf_min_at_2ma"]) / r_low


def check_midi_current(checker: Checker) -> None:
    lows = {}
    for name, sender in SENDERS.items():
        low, high = loop_bounds(sender)
        lows[name] = low
        checker.check(
            f"MIDI loop, {name}: worst-case low current is twice the TLP2361 threshold",
            low > 2 * TLP2361["ifhl_max_a"],
            f"{low * 1e3:.2f} mA vs {TLP2361['ifhl_max_a'] * 1e3:.1f} mA, margin {low / TLP2361['ifhl_max_a']:.2f}x",
        )
        checker.check(
            f"MIDI loop, {name}: worst-case high current stays below the TLP2361 absolute maximum",
            high < TLP2361["if_abs_max_a"],
            f"{high * 1e3:.2f} mA vs {TLP2361['if_abs_max_a'] * 1e3:.0f} mA",
        )
        if high > TLP2361["if_on_max_a"]:
            checker.note(
                f"MIDI loop, {name}: {high * 1e3:.2f} mA is above the recommended "
                f"{TLP2361['if_on_max_a'] * 1e3:.0f} mA on-state current; recorded as a deviation, "
                "high-current and temperature qualification pending")
    worst = min(lows.values())
    checker.check(
        "MIDI loop: the replaced TLP2362 would not be guaranteed to switch at this current",
        worst < TLP2362["ifhl_max_a"],
        f"{worst * 1e3:.2f} mA is below its {TLP2362['ifhl_max_a'] * 1e3:.1f} mA threshold "
        f"and its {TLP2362['if_on_min_a'] * 1e3:.1f} mA recommended minimum",
    )


# ---------------------------------------------------------------------------
# Board checks
# ---------------------------------------------------------------------------

def check_bypass(checker: Checker, model: BoardModel, buffer_name: str, cap_name: str) -> None:
    cap = model.components_by_name.get(cap_name, {})
    capacitance = cap.get("capacitance") or 0.0
    checker.check(
        f"{model.board}: {cap_name} is a 100 nF bypass from LCSC C14663",
        abs(capacitance - 100e-9) < 1e-12 and (cap.get("supplier_part_numbers") or {}).get("jlcpcb") == ["C14663"],
        f"{cap.get('display_capacitance')} {cap.get('supplier_part_numbers')}",
    )

    cap_pads = model.component_pins(cap_name)
    buffer_pads = model.component_pins(buffer_name)
    rail = [p for p in cap_pads.values() if p["net"] == "P3V3"]
    ground = [p for p in cap_pads.values() if p["net"] == "GND"]
    vcc = next((p for p in buffer_pads.values() if p["net"] == "P3V3"), None)
    gnd = next((p for p in buffer_pads.values() if p["net"] == "GND"), None)
    if not (rail and ground and vcc and gnd):
        checker.check(f"{model.board}: {cap_name} sits between {buffer_name} VCC and GND", False,
                      "missing a P3V3 or GND pad on the capacitor or the buffer")
        return
    rail_d = min(math.hypot(r["x"] - vcc["x"], r["y"] - vcc["y"]) for r in rail)
    gnd_d = min(math.hypot(g["x"] - gnd["x"], g["y"] - gnd["y"]) for g in ground)
    checker.check(
        f"{model.board}: {cap_name} supply pad is within {RAIL_PAD_TO_PIN_MAX_MM} mm of the {buffer_name} VCC pad",
        rail_d <= RAIL_PAD_TO_PIN_MAX_MM, f"{rail_d:.2f} mm")
    checker.check(
        f"{model.board}: {cap_name} ground pad is within {GROUND_STRAIGHT_MAX_MM} mm of the {buffer_name} GND pad",
        gnd_d <= GROUND_STRAIGHT_MAX_MM, f"{gnd_d:.2f} mm straight line, plane return")


def check_rear(checker: Checker, rear: BoardModel) -> None:
    check_pad_table(checker, rear, "J_PI", PI_HEADER, "Pi 40-pin header (J_PI)", 40)
    problems, geometry = idc_numbering_problems(rear.component_pins("J_PI"), 40)
    checker.check("rear: J_PI pads use conventional 2 x 20 numbering", not problems,
                  "; ".join(problems[:3]) or f"{geometry.get('columns')} columns, rows {geometry.get('rows')}")

    check_pad_table(checker, rear, "J_FRONT", INTERCONNECT, "front interconnect (J_FRONT)", 26)
    problems, geometry = idc_numbering_problems(rear.component_pins("J_FRONT"), 26)
    checker.check("rear: J_FRONT pads use conventional 2 x 13 numbering", not problems,
                  "; ".join(problems[:3]) or f"{geometry.get('columns')} columns, rows {geometry.get('rows')}")

    check_pad_table(checker, rear, "J_PICO_L", PICO_LEFT, "Pico pins 1-20 (J_PICO_L)", 20)
    check_pad_table(checker, rear, "J_PICO_R", PICO_RIGHT, "Pico pins 21-40 (J_PICO_R)", 20)
    for socket in ("J_PICO_L", "J_PICO_R"):
        pads = rear.component_pins(socket)
        xs = [p["x"] for p in pads.values()]
        ys = [p["y"] for p in pads.values()]
        span_x, span_y = max(xs) - min(xs), max(ys) - min(ys)
        checker.check(f"rear: {socket} is one 1 x 20 row, not a 2-row block",
                      abs(min(span_x, span_y)) < 0.01 and max(span_x, span_y) > 40,
                      f"x span {span_x:.2f} mm, y span {span_y:.2f} mm")

    q3 = rear.components_by_name.get("Q3", {})
    checker.check("rear: Q3 is the JCET 2N7002 (LCSC C8545)",
                  (q3.get("supplier_part_numbers") or {}).get("jlcpcb") == ["C8545"]
                  and q3.get("manufacturer_part_number") == "2N7002",
                  f"{q3.get('manufacturer_part_number')} {q3.get('supplier_part_numbers')}")
    pads = rear.component_pins("Q3")
    want = {1: "RESET_GATE", 2: "GND", 3: "RUN"}
    problems = [f"pad {pin} on {pads.get(pin, {}).get('net')!r}, expected {net!r}"
                for pin, net in want.items() if pads.get(pin, {}).get("net") != net]
    checker.check("rear: Q3 pad 1 = gate (RESET_GATE), pad 2 = source (GND), pad 3 = drain (RUN)",
                  not problems, "; ".join(problems))
    if len(pads) == 3:
        p1, p2, p3 = pads[1], pads[2], pads[3]
        pair = math.hypot(p1["x"] - p2["x"], p1["y"] - p2["y"])
        mid = ((p1["x"] + p2["x"]) / 2, (p1["y"] + p2["y"]) / 2)
        rows = math.hypot(mid[0] - p3["x"], mid[1] - p3["y"])
        checker.check("rear: Q3 pads match a SOT-23 land pattern (1.9 mm lead pair, 2.275 mm rows)",
                      abs(pair - 1.9) < 0.05 and abs(rows - 2.275) < 0.05,
                      f"pair {pair:.3f} mm, rows {rows:.3f} mm")

    for buffer_name, cap_name in (("U3", "C13"), ("U4", "C14")):
        check_bypass(checker, rear, buffer_name, cap_name)

    # B5819W: pin 1 = cathode, pin 2 = anode (as imported). Conduction must run
    # from the Pi 5 V rail into VSYS.
    d3 = rear.component_pins("D3")
    checker.check("rear: D3 conducts P5V (anode, pad 2) to VSYS (cathode, pad 1)",
                  d3.get(1, {}).get("net") == "VSYS" and d3.get(2, {}).get("net") == "P5V",
                  f"pad1 {d3.get(1, {}).get('net')}, pad2 {d3.get(2, {}).get('net')}")

    for name, (plus, minus) in {
        "R19": ("PI_3V3", "OE"),
        "R20": ("OE", "GND"),
        "R16": ("RESET_PI", "RESET_GATE"),
        "R17": ("RESET_GATE", "GND"),
        "R18": ("RUN", "P3V3"),
    }.items():
        nets = {p["net"] for p in rear.component_pins(name).values()}
        checker.check(f"rear: {name} connects {plus} and {minus}", nets == {plus, minus}, f"{sorted(n for n in nets if n)}")

    separate = (rear.pad("J_PI", 1) or {}).get("net") != (rear.pad("J_PICO_R", 16) or {}).get("net")
    checker.check("rear: Pi 3.3 V reference and Pico 3V3 output stay separate nets", separate,
                  f"Pi pin 1 {(rear.pad('J_PI', 1) or {}).get('net')!r}, "
                  f"Pico pin 36 {(rear.pad('J_PICO_R', 16) or {}).get('net')!r}")


def check_front(checker: Checker, front: BoardModel) -> None:
    check_pad_table(checker, front, "J5", INTERCONNECT, "front interconnect (J5)", 26)
    problems, geometry = idc_numbering_problems(front.component_pins("J5"), 26)
    checker.check("front: J5 pads use conventional 2 x 13 numbering", not problems,
                  "; ".join(problems[:3]) or f"{geometry.get('columns')} columns, rows {geometry.get('rows')}")

    for index, jack in enumerate(("J1", "J2", "J3", "J4"), start=1):
        pads = front.component_pins(jack)
        kind = "IN" if index <= 2 else "OUT"
        problems = []
        for pin, contact in TRS_CONTACT.items():
            pad = pads.get(pin)
            if pad is None:
                problems.append(f"pin {pin} missing")
            elif kind == "OUT" and contact == "sleeve":
                # The output sleeve is grounded by design (MIDI OUT topology).
                if pad["net"] != "GND":
                    problems.append(f"sleeve (pin 3) carries {pad['net']!r}, expected GND")
            elif not (pad["net"] or "").startswith("MIDI_"):
                problems.append(f"{contact} (pin {pin}) carries {pad['net']!r}")
        checker.check(f"front: {jack} pin 1 = tip, pin 2 = ring, pin 3 = sleeve", not problems,
                      "; ".join(problems))

    for channel in (1, 2):
        check_midi_in(checker, front, channel)
        check_midi_out(checker, front, channel)

    for name, want in (("R1", 220), ("R2", 220), ("R9", 22), ("R10", 22), ("R11", 33), ("R12", 33)):
        got = (front.components_by_name.get(name) or {}).get("resistance")
        checker.check(f"front: {name} is {want} ohm", got is not None and abs(got - want) < 1e-6, f"{got} ohm")

    stale = [name for name, comp in front.components_by_name.items()
             if (comp.get("supplier_part_numbers") or {}).get("jlcpcb") == [TLP2362["lcsc"]]
             or "TLP2362" in str(comp.get("manufacturer_part_number", ""))]
    checker.check("front: no component still uses or names the TLP2362", not stale, ", ".join(stale))


def check_midi_in(checker: Checker, front: BoardModel, channel: int) -> None:
    jack, opto, res = f"J{channel}", f"U{channel}", f"R{channel}"
    ring, tip = front.pad(jack, 2), front.pad(jack, 1)
    problems: list[str] = []
    if not ring or not tip or ring["net"] is None or tip["net"] is None:
        checker.check(f"front: MIDI IN {channel} Type A polarity", False, "jack pads are unconnected")
    else:
        ring_pins = front.pins_on_net(ring["net"])
        tip_pins = front.pins_on_net(tip["net"])
        res_pads = front.component_pins(res)
        ring_side = [pin for pin, pad in res_pads.items() if pad["net"] == ring["net"]]
        if not ring_side:
            problems.append(f"{res} is not on the ring (DIN 4) net {ring['net']!r}")
        if (jack, 2) not in ring_pins:
            problems.append("jack ring is not on its own net")
        other = [pad for pin, pad in res_pads.items() if pad["net"] != ring["net"]]
        if not other or other[0]["net"] is None:
            problems.append(f"{res} has no second net")
        else:
            anode_pins = front.pins_on_net(other[0]["net"])
            if (opto, 1) not in anode_pins:
                problems.append(f"opto anode (pad 1) is not on {other[0]['net']!r}")
            if (opto, 1) in tip_pins:
                problems.append("opto anode sits on the tip net: Type A polarity is reversed")
        if (opto, 3) not in tip_pins:
            problems.append(f"opto cathode (pad 3) is not on the tip (DIN 5) net {tip['net']!r}")
        checker.check(
            f"front: MIDI IN {channel} Type A polarity (ring/DIN 4 through {res} to anode, tip/DIN 5 to cathode)",
            not problems, "; ".join(problems[:4]))

    comp = front.components_by_name.get(opto, {})
    checker.check(f"front: MIDI IN {channel} receiver is the TLP2361 (LCSC C107626)",
                  (comp.get("supplier_part_numbers") or {}).get("jlcpcb") == [TLP2361["lcsc"]],
                  f"{comp.get('manufacturer_part_number')} {comp.get('supplier_part_numbers')}")

    pads = front.component_pins(opto)
    problems = []
    if pads.get(4, {}).get("net") != "GND":
        problems.append(f"pad 4 on {pads.get(4, {}).get('net')!r}, expected GND")
    if pads.get(6, {}).get("net") != "P3V3":
        problems.append(f"pad 6 on {pads.get(6, {}).get('net')!r}, expected P3V3")
    if pads.get(5, {}).get("net") != f"MIDI_IN_{channel}_RX":
        problems.append(f"pad 5 on {pads.get(5, {}).get('net')!r}, expected MIDI_IN_{channel}_RX")
    checker.check(f"front: MIDI IN {channel} opto pinout (4 GND, 5 VO to the UART, 6 VCC)",
                  not problems, "; ".join(problems))


def check_midi_out(checker: Checker, front: BoardModel, channel: int) -> None:
    jack, transistor = f"J{channel + 2}", f"Q{channel}"
    source_res, sink_res = f"R{channel + 8}", f"R{channel + 10}"
    ring, tip = front.pad(jack, 2), front.pad(jack, 1)
    problems: list[str] = []
    if not ring or not tip or ring["net"] is None or tip["net"] is None:
        checker.check(f"front: MIDI OUT {channel} Type A polarity", False, "jack pads are unconnected")
        return

    src_pads = front.component_pins(source_res)
    if ring["net"] not in {p["net"] for p in src_pads.values()}:
        problems.append(f"{source_res} is not on the ring (DIN 4) net {ring['net']!r}")
    else:
        collector_net = next((p["net"] for p in src_pads.values() if p["net"] != ring["net"]), None)
        if (transistor, 3) not in front.pins_on_net(collector_net):
            problems.append(f"PNP collector (pad 3) is not on {collector_net!r}")
        if ring["net"] == tip["net"]:
            problems.append("ring and tip are shorted")

    sink_pads = front.component_pins(sink_res)
    if tip["net"] not in {p["net"] for p in sink_pads.values()}:
        problems.append(f"{sink_res} is not on the tip (DIN 5) net {tip['net']!r}")
    else:
        return_net = next((p["net"] for p in sink_pads.values() if p["net"] != tip["net"]), None)
        if return_net != "GND":
            problems.append(f"{sink_res} returns to {return_net!r}, expected GND")

    emitter = front.pad(transistor, 2)
    if not emitter or emitter["net"] != "P3V3":
        problems.append(f"PNP emitter (pad 2) on {(emitter or {}).get('net')!r}, expected P3V3")
    checker.check(
        f"front: MIDI OUT {channel} Type A polarity (ring/DIN 4 sourced via {source_res} 22 ohm, "
        f"tip/DIN 5 sunk via {sink_res} 33 ohm)",
        not problems, "; ".join(problems[:4]))


def check_copper(checker: Checker, model: BoardModel) -> None:
    thin = []
    for trace in model.elements.get("pcb_trace", []):
        for point in trace.get("route", []):
            if point.get("route_type") == "wire" and point.get("width", 0) + 1e-9 < MIN_TRACE_MM:
                thin.append(f"{point.get('width')} mm")
    checker.check(f"{model.board}: every trace segment is at least {MIN_TRACE_MM} mm wide", not thin,
                  ", ".join(sorted(set(thin))[:3]))

    vias = model.elements.get("pcb_via", [])
    bad_drill = [v for v in vias if v.get("hole_diameter", 0) + 1e-9 < MIN_VIA_DRILL_MM]
    bad_pad = [v for v in vias if v.get("outer_diameter", 0) + 1e-9 < MIN_VIA_PAD_MM]
    checker.check(f"{model.board}: all {len(vias)} vias use at least a {MIN_VIA_DRILL_MM} mm drill",
                  not bad_drill, f"{len(bad_drill)} below")
    checker.check(f"{model.board}: every via pad is at least {MIN_VIA_PAD_MM} mm", not bad_pad,
                  f"{len(bad_pad)} below")

    # Pad-to-pad clearance across the whole board, using polygon gaps.
    pads = model.all_pads()
    polygons = [(pad, pad_polygon(pad["raw"])) for pad in pads]
    worst = None
    for i, (pad_a, poly_a) in enumerate(polygons):
        for pad_b, poly_b in polygons[i + 1:]:
            if pad_a["component"] == pad_b["component"]:
                continue
            if pad_a["layers"].isdisjoint(pad_b["layers"]):
                continue
            gap = polygon_gap(poly_a, poly_b)
            if worst is None or gap < worst[0]:
                worst = (gap, pad_a, pad_b)
    if worst:
        checker.check(
            f"{model.board}: no two pads on the same layer are closer than {MIN_PAD_GAP_MM} mm",
            worst[0] >= MIN_PAD_GAP_MM,
            f"closest {worst[0]:.3f} mm between {worst[1]['component']}.{worst[1]['pin']} "
            f"and {worst[2]['component']}.{worst[2]['pin']}")

    offsets = [p for p in model.elements.get("pcb_plated_hole", [])
               if p.get("shape") == "circular_hole_with_rect_pad"
               and (abs(p.get("hole_offset_x", 0)) > 1e-9 or abs(p.get("hole_offset_y", 0)) > 1e-9)]
    checker.check(f"{model.board}: rect-pad plated holes keep the hole concentric (pour-safe)", not offsets,
                  f"{len(offsets)} offset holes")

    errors = [e for e in model.circuit if "error" in e.get("type", "")]
    checker.check(f"{model.board}: no routing or DRC error is emitted", not errors,
                  ", ".join(sorted({e["type"] for e in errors})))

    unexpected = []
    for warning in model.elements.get("source_pin_missing_trace_warning", []):
        message = warning.get("message") or ""
        allowed = any(message == f"Port pin{pin} on {comp} is missing a trace"
                      for (board, comp, pin) in ALLOWED_OPEN if board == model.board)
        if not allowed:
            unexpected.append(message)
    checker.check(f"{model.board}: only allow-listed pins are unconnected", not unexpected,
                  "; ".join(sorted(set(unexpected))[:4]))

    pours = model.elements.get("pcb_copper_pour", [])
    pour_nets = {p.get("source_net_id") for p in pours}
    net_names = {n["name"] for n in model.elements.get("source_net", []) if n["source_net_id"] in pour_nets}
    layers = {p.get("layer") for p in pours}
    checker.check(f"{model.board}: GND pour on the {EXPECTED_POUR[model.board]} layer",
                  bool(pours) and layers == {EXPECTED_POUR[model.board]} and net_names == {"GND"},
                  f"layers {sorted(layers)}, nets {sorted(net_names)}")

    mismatched = model.smt_side_mismatch()
    checker.check(f"{model.board}: SMD parts stay on the {EXPECTED_SMT_SIDE[model.board]} side", not mismatched,
                  ", ".join(mismatched[:5]))

    board = (model.elements.get("pcb_board") or [{}])[0]
    checker.check(f"{model.board}: board is still 2 layers", board.get("num_layers") == 2,
                  f"num_layers={board.get('num_layers')}")
    rules = {
        "min_trace_width": MIN_TRACE_MM,
        "min_via_hole_diameter": MIN_VIA_DRILL_MM,
        "min_via_pad_diameter": MIN_VIA_PAD_MM,
        "min_pad_edge_to_pad_edge_clearance": MIN_PAD_GAP_MM,
    }
    wrong = [f"{key}={board.get(key)}" for key, want in rules.items()
             if board.get(key) is None or board[key] + 1e-9 < want]
    checker.check(f"{model.board}: board carries the fabrication rules (trace, via, pad gap)", not wrong,
                  ", ".join(wrong) or str({k: board.get(k) for k in rules}))


def main() -> int:
    parser = argparse.ArgumentParser(description="Independent electrical checks on the built circuit JSON")
    parser.add_argument("--dist", default="dist", help="directory holding the built circuit JSON")
    parser.add_argument("--quiet", action="store_true")
    args = parser.parse_args()

    checker = Checker(Path(args.dist), quiet=args.quiet)
    rear = checker.load("index")
    front = checker.load("front")

    check_rear(checker, rear)
    check_front(checker, front)
    for model in (rear, front):
        check_copper(checker, model)

    problems = []
    for pin in range(1, 27):
        a, b = rear.pad("J_FRONT", pin), front.pad("J5", pin)
        if not a or not b:
            problems.append(f"pin {pin} missing on one board")
        elif a["net"] != b["net"]:
            problems.append(f"pin {pin}: rear {a['net']!r} vs front {b['net']!r}")
    checker.check("both boards: J_FRONT and J5 agree pin for pin (straight ribbon)", not problems,
                  "; ".join(problems[:4]))

    check_midi_current(checker)

    print(f"\n{checker.passed} checks passed, {len(checker.failed)} failed")
    if checker.failed:
        print("Failed checks:")
        for failure in checker.failed:
            print(f"  - {failure}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())

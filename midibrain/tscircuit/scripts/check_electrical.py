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
import re
import sys
from pathlib import Path

# ---------------------------------------------------------------------------
# Expected connectivity, written from the pin maps and the MIDI specification.
# ---------------------------------------------------------------------------

# Raspberry Pi 5 40-pin header, physical positions. Only the pins this design
# uses are listed; every other pin must stay unconnected.
PI_HEADER = {
    1: "PI_3V3",      # Pi 3.3 V, used only as an OE/control reference
    2: "P5V",         # 5 V from the Pi, through D3 into Pico VSYS
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

# 2 x 13 front/rear interconnect. Identical on both boards: a DIRECT mating
# pair (front female SFH11 socket on the bottom face, rear male SBH11 header on
# the top face), so the same pin number must carry the same net on the same
# physical pad. There is no plug and no ribbon.
INTERCONNECT = {
    1: "P3V3", 2: "GND", 3: "MIDI_IN_1_RX", 4: "MIDI_OUT_1_TX",
    5: "MIDI_IN_2_RX", 6: "MIDI_OUT_2_TX", 7: "NAV_UP", 8: "NAV_DOWN",
    9: "NAV_LEFT", 10: "NAV_RIGHT", 11: "NAV_PRESS", 12: "ENC_A",
    13: "ENC_B", 14: "ENC_SW", 15: "REC", 16: "OVER_DUB",
    17: "STOP", 18: "PLAY", 19: "PLAY_START", 20: "REC_LED",
    21: "OVERDUB_LED", 22: "P3V3", 23: "PLAY_LED", 24: "GND",
    25: "GND", 26: "SHIFT_EXIT",
}

# TRS Type A: tip = DIN 5, ring = DIN 4, sleeve = DIN 2.
#
# The contact map is keyed by PAD NUMBER and must follow the footprint, not the
# part's terminal numbering. `lib/jack.tsx` is the authority: it labels the three
# pads by function (S / R / T) for the QingPu/Thonkiconn WQP419GR, whose vendor
# drawing numbers them 1 = sleeve, 2 = tip, 3 = ring - the reverse of the
# retired PJ-301M-3-B assignment. Each physical pad keeps its net across that
# change (tip at +4.92, ring at -3.38, sleeve at -6.48 mm from the barrel), so a
# number-only check is exactly how a tip/sleeve swap would slip through.
TRS_CONTACT = {1: "sleeve", 2: "ring", 3: "tip"}
# Contact name -> pad number on the current footprint, derived (never re-typed).
TRS_PAD = {contact: pin for pin, contact in TRS_CONTACT.items()}

# Pins that may be open, derived from the connectivity tables above: every
# header pin that no table entry uses, plus the second contact pair of each
# 4-pin tactile switch. A pin that SHOULD be connected is therefore never
# allowed to be open, which is how a lost net is caught.
OPEN_BY_DESIGN: dict[str, dict[str, set[int]]] = {
    "index": {
        "J_PI": set(range(1, 41)) - set(PI_HEADER),
        "J_FRONT": set(range(1, 27)) - set(INTERCONNECT),
        "J_PICO_L": set(range(1, 21)) - set(PICO_LEFT),
        "J_PICO_R": set(range(1, 21)) - set(PICO_RIGHT),
    },
    "front": {
        "J5": set(range(1, 27)) - set(INTERCONNECT),
        # Both contact pairs 2 and 4 of every B3F-4055 are the duplicated
        # internal contacts; SW7 is the panel POWER momentary.
        **{sw: {2, 4} for sw in ("SW1", "SW2", "SW3", "SW4", "SW5", "SW6", "SW7")},
    },
}

# Kept for callers that only need the flat list.
ALLOWED_OPEN = {(board, comp, pin) for board, comps in OPEN_BY_DESIGN.items()
                for comp, pins in comps.items() for pin in pins}

# Toshiba TLP2361 datasheet Rev.5.0 (2015-11-02).
TLP2361 = {
    "lcsc": "C107626",
    "ifhl_max_a": 1.3e-3,        # guaranteed maximum, Ta <= 105 C
    "ifhl_guaranteed_a": 1.6e-3,  # guaranteed maximum over -40..125 C (used here)
    "if_on_min_a": 2e-3,         # recommended on-state current
    "if_on_max_a": 6e-3,
    # Absolute maximum ratings (page 3): IF 10 mA at Ta = 25 C, derating
    # -0.13 mA/C for Ta >= 110 C, so the permitted continuous current at
    # 125 C is 10 - 0.13 x (125 - 110) = 8.05 mA. 10 mA is NOT a
    # full-temperature limit and is never used as one here.
    "if_abs_max_a": 10e-3,
    "if_abs_derating_above_c": 110.0,
    "if_abs_derating_per_c": 0.13e-3,
    "vf_min_at_2ma": 1.35,
    "vf_max_at_2ma": 1.65,
    "height_max_mm": 2.2,        # package drawing: 2.1 +-0.1 mm
    "voh_min_at_20ua": 3.2,
    "vol_max_at_3.2ma": 0.4,     # VOL <= 0.4 V at IO = 3.2 mA sink
}
# Toshiba TLP2362 Rev.6.0 (2016-01-19): the replaced part.
TLP2362 = {
    "lcsc": "C37658",
    "ifhl_max_a": 5.0e-3,
    "ifhl_guaranteed_a": 5.0e-3,
    "if_on_min_a": 7.5e-3,
    "if_on_max_a": 14e-3,
}

# MIDI loop constants: this design's receiver resistor and the sender corners
# from MIDI 1.0 CA-033 (2014) figure 1, page 2.
#   +5 V  +-10 %: RA 220 ohm 5 %, RC 220 ohm 5 %
#   +3.3 V +-5 %: RA 33 ohm 5 %,  RC 10 ohm 5 %  (RA and RC are not equal)
RECEIVER_OHM = 220
RECEIVER_TOLERANCE = 0.01        # this design's 1 % part
# Two forward-voltage numbers, kept apart:
#   VF_MAX_AT_2MA is the TLP2361 datasheet maximum, specified at IF = 2 mA only.
#   VF_ASSUMED_HIGH is the assumed upper bound at the ~3.7 mA this loop runs
#   at, used for the minimum-current corner. It is an assumption, not a limit.
VF_MAX_AT_2MA = 1.65
VF_ASSUMED_HIGH = 1.85
# drop_min is 0 V: no minimum sender loss is guaranteed (a low-loss sender can
# drop almost nothing), so the maximum-current bound assumes zero loss.
SENDERS = {
    "CA-033 +5 V sender (RA/RC 220 ohm 5%)": {
        "v": 5.0, "tol": 0.10, "ra": 220, "rc": 220, "rtol": 0.05,
        "drop_min": 0.0, "drop_max": 0.3,
    },
    "CA-033 +3.3 V sender (RA 33 / RC 10 ohm 5%)": {
        "v": 3.3, "tol": 0.05, "ra": 33, "rc": 10, "rtol": 0.05,
        "drop_min": 0.0, "drop_max": 0.3,
    },
}

# Reference values, worked out by hand from the limits above. The model must
# reproduce them: a loosened tolerance, a lower threshold or a dropped driver
# loss changes a value and is reported as a failure.
REFERENCE_LOOP_A = {
    "CA-033 +5 V sender (RA/RC 220 ohm 5%)": (3.4347e-3, 6.5272e-3),
    "CA-033 +3.3 V sender (RA 33 / RC 10 ohm 5%)": (3.6843e-3, 8.1771e-3),
}
REFERENCE_LOOP_TOL = 0.002
# Minimum-current margin against the 125 C threshold: 3.4347/1.6 and 3.6843/1.6.
REFERENCE_MARGIN = {
    "CA-033 +5 V sender (RA/RC 220 ohm 5%)": 2.1467,
    "CA-033 +3.3 V sender (RA 33 / RC 10 ohm 5%)": 2.3027,
}

# CA-033 page 4 transmitter requirements and this design's output stage.
CA033 = {
    "nominal_loop_a": 5e-3,
    "receiver_turn_on_below_a": 5e-3,
    "legacy_led_max_vf": 1.9,
    "legacy_receiver_ohm": 220,
    "legacy_receiver_tolerance": 0.05,
    "pc900v_worst_case_a": 4e-3,
    "minimum_transmitter_v": 3.0,
    "ca033_ring_ohm": 33,
    "ca033_tip_ohm": 10,
    "ca033_resistor_w": 0.25,
}

# Output stage: AO3401A high-side P-channel switch. AOS datasheet Rev 3.1.
AO3401A = {
    "mpn": "AO3401A",
    "lcsc": "C15127",
    "rds_on_max_ohm": 0.085,     # VGS = -2.5 V, ID = -2.5 A, TJ = 25 C
    "rds_on_test_vgs_v": -2.5,
    "vgs_threshold_min_v": -0.5,
    "vgs_max_v": 12,
}
# 2512 output resistors 25121WF330JT4E/25121WF220JT4E: 7th code F = +-1 % per
# the Uniroyal thick-film datasheet (Feb 12 2019 V.3, section 2). 0603 gate
# network is 1 %. GATE_DRIVE carries both rails: weakest for drive-on,
# strongest for the off-state worst case.
OUT_SERIES = {"ring": 33, "tip": 22, "tolerance": 0.01, "rated_w": 1.0}
GATE_NETWORK = {"series": 100, "pullup": 2200, "tolerance": 0.01}
GATE_DRIVE = {"rail_min_v": 3.135, "rail_max_v": 3.465, "gpio_vol_max_v": 0.5, "pull_down_min_ohm": 36000}

# Hand-calculated reference values for the output stage.
REFERENCE_GATE_ON_V = 2.5182
REFERENCE_GATE_OFF_V = 0.2100
REFERENCE_LEGACY_NEW_A = 4.3086e-3
REFERENCE_LEGACY_OLD_A = 3.4374e-3
# Fault names are the electrical fault, not a topology guess: the ring is the
# current-source side, so a ring-to-ground fault pulls current through RA; the
# tip is the sink side with RC to ground, so the fault that heats RC is the tip
# connected to the POSITIVE rail (wrong-wired plug or a short to a rail), not
# the tip connected to ground.
REFERENCE_FAULT_W = {
    "ring_to_ground": 0.3656,
    "tip_to_positive_rail": 0.5470,
    "ring_tip_ring": 0.1319,
    "ring_tip_tip": 0.0879,
}
REFERENCE_CA033_TIP_FAULT_W = 1.2638

# RP2350 digital input limits at IOVDD = 3.3 V: datasheet table 1683,
# pdf page 1337 (printed 1336).
RP2350 = {"vih_min_v": 2.0, "vil_max_v": 0.8, "leak_max_a": 1e-6}

# Fabrication and design policy, checked against the emitted copper.
MIN_TRACE_MM = 0.20
MIN_VIA_DRILL_MM = 0.20
MIN_VIA_PAD_MM = 0.45
MIN_PAD_GAP_MM = 0.15
UNCONNECTED = re.compile(r"^Port pin(\d+) on (\S+) is missing a trace$")
RAIL_PAD_TO_PIN_MAX_MM = 3.0
GROUND_STRAIGHT_MAX_MM = 5.0
IDC_PITCH_MM = 2.54
# Direct 2 x 13 mating pair: 1.00 mm drill is the MALE's manufacturer
# recommendation (Sullins drawing 11091 sheet 1); for the female it is a
# PROVISIONAL design assumption (no female PCB hole is published on the pages
# read). 1.60 mm pad keeps a 0.30 mm ring.
INTERCONNECT_DRILL_MM = 1.0
INTERCONNECT_PAD_MM = 1.6

# Rear board: parts on the bottom, ground plane on top. Front board: parts on
# the top, ground plane on the bottom.
EXPECTED_POUR = {"index": "top", "front": "bottom"}
EXPECTED_SMT_SIDE = {"index": "bottom", "front": "top"}
# Through-hole parts: the box headers, the Pico sockets, the TRS jacks, the
# tactile switches and the encoder. U5 (SKRHABE010) is an SMD part.
# J_PWR (right-angle 1 x 2) and SW7 (panel POWER) are THT too: J_PWR sits on the
# REAR (bottom) face at the top notch, so without it here the single-sided SMT
# rule would wrongly flag it.
THT_PARTS = {"J5", "J_PI", "J_FRONT", "J_PICO_L", "J_PICO_R", "J1", "J2", "J3", "J4",
             "SW1", "SW2", "SW3", "SW4", "SW5", "SW6", "SW7", "J_PWR", "U6"}

# ---------------------------------------------------------------------------
# Probe points (rear board only): bare plated through-holes with no part.
# ---------------------------------------------------------------------------
# Restated from the owner list and tscircuit/lib/probe-points.ts - this checker
# never imports the design source, so a changed pad has to appear in the built
# JSON to pass. Every entry is the exact net the pad must carry and the exact
# position; the net names are the source_net names the builder emits.
PROBE_POINTS = {
    # ref:  (net, x, y)
    "TP1": ("P5V", -40.0, 52.5),
    "TP2": ("P3V3", -35.0, 52.5),
    "TP3": ("PI_3V3", -40.0, 49.5),
    "TP4": ("RUN", -35.0, 49.5),
    "TP5": ("SPI_SCLK_PICO", -40.0, 46.5),
    "TP6": ("SPI_CS_PICO", -35.0, 46.5),
    "TP7": ("SPI_MOSI_PICO", -40.0, 43.5),
    "TP8": ("SPI_MISO_PICO", -35.0, 43.5),
    "TP9": ("READY_PICO", -40.0, 40.5),
    "TP10": ("GND", -35.0, 40.5),
    "TP11": ("MIDI_OUT_1_TX", -33.0, -30.0),
    "TP12": ("MIDI_IN_1_RX", -30.0, -30.0),
    "TP13": ("MIDI_OUT_2_TX", -27.0, -30.0),
    "TP14": ("MIDI_IN_2_RX", -33.0, -33.0),
    "TP15": ("GND", -30.0, -33.0),
}
PROBE_HOLE_MM = 0.8
PROBE_PAD_MM = 1.6
PROBE_MIN_RING_MM = 0.25
PROBE_PAD_RADIUS_MM = PROBE_PAD_MM / 2
PROBE_MIN_PITCH_MM = 3.0          # the source places every pair at 3.0 mm or more
PROBE_EDGE_MM = 2.0
PROBE_PART_GAP_MM = 2.0           # pad edge to another component's pad/hole
PROBE_POSITION_TOL_MM = 0.01
# Planning envelopes a probe PAD must stay out of; same boxes as
# lib/probe-points.ts, restated here on purpose. Sources: Pico 2 datasheet
# (51 x 21 mm module, micro-USB at the pin-1 end), J_PI / J_FRONT body plus
# connector space. The Pi 5 no longer sits on the rear board (2026-11-12), so
# it has no keepout here.
PROBE_KEEPOUTS = {
    "pico_module": (-50.2, -25.5, -28.8, 25.5),
    "pico_usb_plug": (-43.5, -45.0, -35.5, -25.5),
    "j_pi_body": (-26.0, 45.0, 26.0, 53.0),
    # Male SBH11 body envelope (40.52 x 8.80 mm) at the direct interconnect's
    # shared placement (10, 5) rotated 90 deg.
    "j_front_body": (5.6, -15.3, 14.4, 25.3),
}


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
            allowed = OPEN_BY_DESIGN.get(model.board, {}).get(component, set())
            if pad["net"] is not None:
                problems.append(f"pin {pin} carries {pad['net']!r} but must stay open")
            elif pin not in allowed:
                problems.append(f"pin {pin} is open but not declared open by design")
            continue
        if pad["net"] != want:
            problems.append(f"pin {pin} on {pad['net']!r}, expected {want!r}")
    checker.check(f"{model.board}: {label} pad-to-net table", not problems, "; ".join(problems[:6]))


# ---------------------------------------------------------------------------
# MIDI current loop
# ---------------------------------------------------------------------------

def loop_bounds(sender: dict) -> tuple[float, float]:
    """Worst-case loop current: weak corner down, strong corner up.

    The sender driver drop is included at both corners; CA-033 does not
    specify it, so the bounds are stated assumptions.
    """
    v_low = sender["v"] * (1 - sender["tol"])
    v_high = sender["v"] * (1 + sender["tol"])
    series = sender["ra"] + sender["rc"]
    r_low = series * (1 - sender["rtol"]) + RECEIVER_OHM * (1 - RECEIVER_TOLERANCE)
    r_high = series * (1 + sender["rtol"]) + RECEIVER_OHM * (1 + RECEIVER_TOLERANCE)
    low = (v_low - sender["drop_max"] - VF_ASSUMED_HIGH) / r_high
    high = (v_high - sender["drop_min"] - TLP2361["vf_min_at_2ma"]) / r_low
    return low, high


def check_midi_current(checker: Checker) -> None:
    # The design point is the full-temperature guarantee, not the 105 C figure.
    checker.check(
        "MIDI loop: the threshold used is the full-temperature guarantee",
        abs(TLP2361["ifhl_guaranteed_a"] - 1.6e-3) < 1e-9
        and TLP2361["ifhl_guaranteed_a"] >= TLP2361["ifhl_max_a"],
        f"{TLP2361['ifhl_guaranteed_a'] * 1e3:.1f} mA over -40..125 C; "
        f"the 105 C figure is {TLP2361['ifhl_max_a'] * 1e3:.1f} mA",
    )
    # Absolute maximum, page 3: 10 mA at Ta = 25 C, derating -0.13 mA/C for
    # Ta >= 110 C. 10 mA is NOT a full-temperature limit; the 125 C figure is
    # 10 - 0.13 x (125 - 110) = 8.05 mA. Checked as data so the model cannot
    # silently quote the 25 C number as the hot one.
    tlp_abs_125_a = (TLP2361["if_abs_max_a"]
                     - TLP2361["if_abs_derating_per_c"]
                     * (125 - TLP2361["if_abs_derating_above_c"]))
    checker.check(
        "MIDI loop: the TLP2361 absolute maximum is derated for 125 C",
        abs(tlp_abs_125_a - 8.05e-3) < 1e-9,
        f"{tlp_abs_125_a * 1e3:.2f} mA at 125 C (abs max 10 mA is a 25 C value; "
        "-0.13 mA/C above 110 C)",
    )
    lows = {}
    for name, sender in SENDERS.items():
        low, high = loop_bounds(sender)
        lows[name] = low
        reference_low, reference_high = REFERENCE_LOOP_A[name]
        # Cross-entry reference case: the model must reproduce the hand
        # calculation. A loosened tolerance, a lower threshold or a dropped
        # driver loss moves the number and fails here.
        matches = (abs(low - reference_low) <= REFERENCE_LOOP_TOL * reference_low
                   and abs(high - reference_high) <= REFERENCE_LOOP_TOL * reference_high)
        checker.check(
            f"MIDI loop, {name}: model matches the independent reference case",
            matches,
            f"model {low * 1e3:.3f}/{high * 1e3:.3f} mA, reference "
            f"{reference_low * 1e3:.3f}/{reference_high * 1e3:.3f} mA",
        )
        threshold = TLP2361["ifhl_guaranteed_a"]
        margin = low / threshold
        reference_margin = REFERENCE_MARGIN[name]
        checker.check(
            f"MIDI loop, {name}: worst-case low current is twice the 125 C TLP2361 threshold",
            low > 2 * threshold,
            f"{low * 1e3:.2f} mA vs {threshold * 1e3:.1f} mA, margin {margin:.2f}x",
        )
        checker.check(
            f"MIDI loop, {name}: margin matches the independent reference case",
            abs(margin - reference_margin) <= REFERENCE_LOOP_TOL * reference_margin,
            f"model {margin:.4f}x, reference {reference_margin:.4f}x",
        )
        checker.check(
            f"MIDI loop, {name}: worst-case high current stays below the 25 C TLP2361 absolute maximum",
            high < TLP2361["if_abs_max_a"],
            f"{high * 1e3:.2f} mA vs {TLP2361['if_abs_max_a'] * 1e3:.0f} mA at 25 C, "
            f"{high / tlp_abs_125_a * 100:.0f} % of the {tlp_abs_125_a * 1e3:.2f} mA 125 C derated limit",
        )
        if high > TLP2361["if_on_max_a"]:
            checker.note(
                f"MIDI loop, {name}: {high * 1e3:.2f} mA is above the recommended "
                f"{TLP2361['if_on_max_a'] * 1e3:.0f} mA on-state current; recorded as a deviation, "
                "high-current and temperature qualification pending")
        if high > tlp_abs_125_a:
            checker.note(
                f"MIDI loop, {name}: the model bound {high * 1e3:.2f} mA is above the "
                f"{tlp_abs_125_a * 1e3:.2f} mA absolute maximum at 125 C. The comparison is "
                "illustrative only: the model uses the 2 mA VF minimum, the CA-033 sender corners "
                "and an assumed 0 V sender drop, so it is not a measurement and no safe ambient is "
                "derived. Physical current and temperature qualification pending (acceptance item "
                "midi_receiver_high_current_thermal)")
    worst = min(lows.values())
    checker.check(
        "MIDI loop: the replaced TLP2362 would not be guaranteed to switch at this current",
        worst < TLP2362["ifhl_guaranteed_a"],
        f"{worst * 1e3:.2f} mA is below its {TLP2362['ifhl_guaranteed_a'] * 1e3:.1f} mA threshold "
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


def board_box(model: BoardModel) -> tuple[float, float]:
    board = (model.elements.get("pcb_board") or [{}])[0]
    return board.get("width", 0.0), board.get("height", 0.0)


def box_distance(x: float, y: float, box: tuple[float, float, float, float]) -> float:
    """Distance from a point to an axis-aligned box; 0 inside the box."""
    x1, y1, x2, y2 = box
    return math.hypot(max(x1 - x, 0.0, x - x2), max(y1 - y, 0.0, y - y2))


def probe_pads(model: BoardModel) -> dict[str, list[dict]]:
    """ref -> the built pads of each simple_test_point component."""
    out: dict[str, list[dict]] = {}
    for name, comp in model.components_by_name.items():
        if comp.get("ftype") != "simple_test_point":
            continue
        out[name] = [entry["raw"] for entry in model.component_pins(name).values()]
    return out


def check_probe_points(checker: Checker, rear: BoardModel) -> None:
    """The rear board's probe points, measured on the built pads.

    Fail-closed: the set must be exactly the approved pads, each one a plated
    through-hole on both copper layers with the authored 0.80 mm drill and
    1.60 mm pad (0.40 mm ring, above the 0.25 mm target), on the exact net and
    at the exact position. Nothing here proves that a probe tip can reach the
    pad inside the assembled stack: that is acceptance item
    `probe_point_access` and stays pending.
    """
    found = probe_pads(rear)
    checker.check(
        "rear: the probe-point set is exactly the approved pads",
        sorted(found) == sorted(PROBE_POINTS),
        f"built {sorted(found)}, expected {sorted(PROBE_POINTS)}",
    )

    # doNotPlace is the only treatment that keeps bare copper out of the BOM and
    # the CPL without inventing a manufacturer part number. It must cover the
    # probe points and nothing else.
    dnp = {name for name, comp in rear.components_by_name.items()
           if rear.pcb_of.get(comp["source_component_id"], {}).get("do_not_place")}
    checker.check("rear: exactly the probe points are doNotPlace (no BOM line, no part number)",
                  dnp == set(PROBE_POINTS), f"doNotPlace {sorted(dnp)}")

    built: dict[str, dict] = {}
    for ref, (net, x, y) in PROBE_POINTS.items():
        pads = found.get(ref, [])
        if len(pads) != 1:
            checker.check(f"rear: {ref} has exactly one pad", False, f"{len(pads)} pads")
            continue
        pad = pads[0]
        built[ref] = pad
        hole = pad.get("hole_diameter")
        outer = pad.get("outer_diameter")
        ring = (outer - hole) / 2 if isinstance(outer, (int, float)) and isinstance(hole, (int, float)) else None
        checker.check(f"rear: {ref} is a circular plated through-hole on both copper layers",
                      pad.get("shape") == "circle"
                      and set(pad.get("layers") or []) == {"top", "bottom"},
                      f"shape {pad.get('shape')}, layers {pad.get('layers')}")
        checker.check(f"rear: {ref} uses the authored {PROBE_HOLE_MM} mm drill and {PROBE_PAD_MM} mm pad",
                      hole is not None and outer is not None
                      and abs(hole - PROBE_HOLE_MM) < 1e-9 and abs(outer - PROBE_PAD_MM) < 1e-9,
                      f"drill {hole}, pad {outer}")
        checker.check(f"rear: {ref} annular ring is at least {PROBE_MIN_RING_MM} mm",
                      ring is not None and ring >= PROBE_MIN_RING_MM - 1e-9,
                      f"{ring if ring is None else round(ring, 3)} mm")
        entry = rear.pads.get(ref, {}).get(_probe_pin(rear, ref), {})
        checker.check(f"rear: {ref} is on net {net}", entry.get("net") == net, f"net {entry.get('net')!r}")
        checker.check(f"rear: {ref} sits at ({x}, {y}) mm",
                      abs(pad["x"] - x) <= PROBE_POSITION_TOL_MM and abs(pad["y"] - y) <= PROBE_POSITION_TOL_MM,
                      f"({pad['x']:.3f}, {pad['y']:.3f})")
        comp = rear.components_by_name.get(ref, {})
        suppliers = comp.get("supplier_part_numbers") or {}
        checker.check(f"rear: {ref} carries no supplier or manufacturer part number (bare copper)",
                      not suppliers.get("jlcpcb") and not comp.get("manufacturer_part_number"),
                      f"{suppliers} {comp.get('manufacturer_part_number')}")

    # Connectivity, not only a net label: a routed trace must start at each pad
    # centre, and the net must already carry other pads. A single-pad net would
    # be an isolated anchor, and this design does not add one.
    trace_points: list[tuple[float, float]] = []
    for trace in rear.elements.get("pcb_trace", []):
        for point in trace.get("route", []):
            if "x" in point:
                trace_points.append((point["x"], point["y"]))
            else:
                trace_points.append((point["start"]["x"], point["start"]["y"]))
                trace_points.append((point["end"]["x"], point["end"]["y"]))
    for ref, pad in built.items():
        near = (min(math.hypot(pad["x"] - x, pad["y"] - y) for x, y in trace_points)
                if trace_points else float("inf"))
        pin = _probe_pin(rear, ref)
        net = rear.pads.get(ref, {}).get(pin, {}).get("net")
        others = [entry for entry in rear.pads_by_net.get(net or "", [])
                  if entry["component"] != ref]
        checker.check(f"rear: {ref} is on a routed, already-existing net",
                      near <= 0.01 and len(others) >= 1,
                      f"nearest trace {near:.3f} mm, {len(others)} other pad(s) on {net!r}")

    # Pitches, edges and the envelope keepouts, from the built coordinates.
    refs = sorted(built)
    worst_pitch = None
    for i, a in enumerate(refs):
        for b in refs[i + 1:]:
            pitch = math.hypot(built[a]["x"] - built[b]["x"], built[a]["y"] - built[b]["y"])
            if worst_pitch is None or pitch < worst_pitch[0]:
                worst_pitch = (pitch, a, b)
    checker.check(f"rear: no two probe pads are closer than {PROBE_MIN_PITCH_MM} mm",
                  worst_pitch is None or worst_pitch[0] >= PROBE_MIN_PITCH_MM - 1e-9,
                  "none paired" if worst_pitch is None
                  else f"closest {worst_pitch[0]:.2f} mm between {worst_pitch[1]} and {worst_pitch[2]}")

    width, height = board_box(rear)
    edge_problems = []
    for ref, pad in built.items():
        for edge in (pad["x"] - PROBE_PAD_RADIUS_MM + width / 2,
                     width / 2 - pad["x"] - PROBE_PAD_RADIUS_MM,
                     pad["y"] - PROBE_PAD_RADIUS_MM + height / 2,
                     height / 2 - pad["y"] - PROBE_PAD_RADIUS_MM):
            if edge < PROBE_EDGE_MM:
                edge_problems.append(f"{ref} {edge:.2f} mm")
    checker.check(f"rear: every probe pad keeps {PROBE_EDGE_MM} mm to the board edge",
                  not edge_problems, ", ".join(edge_problems[:4]))

    keepout_problems = []
    for ref, pad in built.items():
        for name, box in PROBE_KEEPOUTS.items():
            gap = box_distance(pad["x"], pad["y"], box)
            if gap <= PROBE_PAD_RADIUS_MM:
                keepout_problems.append(f"{ref} inside {name}")
    checker.check("rear: no probe pad sits in a Pico/Pi/connector envelope",
                  not keepout_problems, ", ".join(keepout_problems[:4]))

    # Physical probing: a pad edge must clear another component's pad or a bare
    # hole by PROBE_PART_GAP_MM, so a probe body does not land on a part.
    others = []
    for pad in rear.all_pads():
        if str(pad["component"]).startswith("TP"):
            continue
        others.append((f"{pad['component']}.{pad['pin']}", pad_polygon(pad["raw"])))
    for hole in rear.elements.get("pcb_hole", []):
        others.append((f"hole({hole['x']:.1f},{hole['y']:.1f})",
                       _rotated_rect(hole["x"], hole["y"], hole["hole_diameter"], hole["hole_diameter"], 0.0)))
    clearance = []
    for ref, pad in built.items():
        poly = pad_polygon(pad)
        best = None
        for label, other in others:
            gap = polygon_gap(poly, other)
            if best is None or gap < best[0]:
                best = (gap, label)
        clearance.append((ref, best))
    for ref, best in clearance:
        checker.check(f"rear: {ref} keeps {PROBE_PART_GAP_MM} mm from the nearest other pad or hole",
                      best is not None and best[0] >= PROBE_PART_GAP_MM - 1e-9,
                      "no other pad" if best is None else f"{best[0]:.2f} mm to {best[1]}")

    # The two 3.3 V rails stay separate: different nets on the probe pads, and
    # no component anywhere on the board bridges PI_3V3 to P3V3.
    pi = built.get("TP3")
    pico = built.get("TP2")
    pi_net = rear.pads.get("TP3", {}).get(_probe_pin(rear, "TP3"), {}).get("net")
    pico_net = rear.pads.get("TP2", {}).get(_probe_pin(rear, "TP2"), {}).get("net")
    checker.check("rear: the PI_3V3 and P3V3 probe pads are on different nets",
                  pi is not None and pico is not None and pi_net == "PI_3V3" and pico_net == "P3V3",
                  f"{pi_net!r} vs {pico_net!r}")
    bridging = [name for name, pins in rear.pads.items()
                if {"PI_3V3", "P3V3"} <= {p["net"] for p in pins.values() if p["net"]}]
    checker.check("rear: no component has ports on both PI_3V3 and P3V3 (rails not coupled)",
                  not bridging, f"bridging components {bridging}")
    checker.note("rear: probe pads are bare copper on nets that already exist; physical probe "
                 "access inside the assembled stack is unverified (acceptance item probe_point_access)")


def _probe_pin(model: BoardModel, ref: str) -> int | None:
    """The single physical pin number of a probe point, or None when absent."""
    pins = model.component_pins(ref)
    return next(iter(pins)) if len(pins) == 1 else None


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
        checker.check(f"front: {jack} pads by function ({', '.join(f'{c} = pin {p}' for p, c in TRS_CONTACT.items())})",
                      not problems, "; ".join(problems))

    for channel in (1, 2):
        check_midi_in(checker, front, channel)
        check_midi_out(checker, front, channel)

    # Output stage: 33 ohm ring and 22 ohm tip (R9/R11, R10/R12); the receiver
    # stays at 220 ohm on both inputs.
    for name, want in (("R1", 220), ("R2", 220), ("R9", 33), ("R10", 33), ("R11", 22), ("R12", 22)):
        got = (front.components_by_name.get(name) or {}).get("resistance")
        checker.check(f"front: {name} is {want} ohm", got is not None and abs(got - want) < 1e-6, f"{got} ohm")

    stale = [name for name, comp in front.components_by_name.items()
             if (comp.get("supplier_part_numbers") or {}).get("jlcpcb") == [TLP2362["lcsc"]]
             or "TLP2362" in str(comp.get("manufacturer_part_number", ""))]
    checker.check("front: no component still uses or names the TLP2362", not stale, ", ".join(stale))


def check_midi_in(checker: Checker, front: BoardModel, channel: int) -> None:
    jack, opto, res = f"J{channel}", f"U{channel}", f"R{channel}"
    ring, tip = front.pad(jack, TRS_PAD["ring"]), front.pad(jack, TRS_PAD["tip"])
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
    jack, switch = f"J{channel + 2}", f"Q{channel}"
    gate_res, pullup_res = f"R{channel + 4}", f"R{channel + 6}"
    ring_res, tip_res = f"R{channel + 8}", f"R{channel + 10}"
    ring, tip = front.pad(jack, TRS_PAD["ring"]), front.pad(jack, TRS_PAD["tip"])
    problems: list[str] = []
    if not ring or not tip or ring["net"] is None or tip["net"] is None:
        checker.check(f"front: MIDI OUT {channel} Type A polarity", False, "jack pads are unconnected")
        return

    comp = front.components_by_name.get(switch, {})
    checker.check(
        f"front: MIDI OUT {channel} switch is the AO3401A (LCSC C15127)",
        (comp.get("supplier_part_numbers") or {}).get("jlcpcb") == [AO3401A["lcsc"]]
        and comp.get("manufacturer_part_number") == AO3401A["mpn"],
        f"{comp.get('manufacturer_part_number')} {comp.get('supplier_part_numbers')}",
    )
    pads = front.component_pins(switch)
    gate_net = (pads.get(1) or {}).get("net")
    drain_net = (pads.get(3) or {}).get("net")
    if (pads.get(2) or {}).get("net") != "P3V3":
        problems.append(f"pad 2 (source) on {(pads.get(2) or {}).get('net')!r}, expected P3V3")
    if not gate_net:
        problems.append("pad 1 (gate) is unconnected")
    if not drain_net:
        problems.append("pad 3 (drain) is unconnected")
    checker.check(f"front: MIDI OUT {channel} pad 1 = gate, pad 2 = source (3.3 V), pad 3 = drain",
                  not problems, "; ".join(problems))

    # Gate network: 100 ohm in series from the UART pin, 2.2 kOhm to the rail.
    tx_net = f"MIDI_OUT_{channel}_TX"
    series = front.component_pins(gate_res)
    series_nets = {p["net"] for p in series.values()}
    if series_nets != {tx_net, gate_net}:
        problems.append(f"{gate_res} sits between {sorted(n for n in series_nets if n)}, "
                        f"expected {tx_net} and {gate_net}")
    pullup = front.component_pins(pullup_res)
    if {p["net"] for p in pullup.values()} != {gate_net, "P3V3"}:
        problems.append(f"{pullup_res} does not connect the gate to P3V3")
    checker.check(f"front: MIDI OUT {channel} gate network (100 ohm series, 2.2 kOhm to 3.3 V)",
                  not problems, "; ".join(problems[:3]))

    # Output resistors: 33 ohm on the ring from the drain, 22 ohm on the tip.
    problems = []
    ring_pads = front.component_pins(ring_res)
    if {p["net"] for p in ring_pads.values()} != {drain_net, ring["net"]}:
        problems.append(f"{ring_res} is not between the drain and the ring")
    tip_pads = front.component_pins(tip_res)
    if {p["net"] for p in tip_pads.values()} != {tip["net"], "GND"}:
        problems.append(f"{tip_res} is not between the tip and GND")
    if ring["net"] == tip["net"]:
        problems.append("ring and tip are shorted")
    checker.check(
        f"front: MIDI OUT {channel} Type A polarity (ring/DIN 4 from the switch drain via "
        f"{ring_res}, tip/DIN 5 to GND via {tip_res})",
        not problems, "; ".join(problems[:3]))

    for name, want in ((gate_res, 100), (pullup_res, 2200), (ring_res, 33), (tip_res, 22)):
        got = (front.components_by_name.get(name) or {}).get("resistance")
        checker.check(f"front: {name} is {want} ohm", got is not None and abs(got - want) < 1e-6, f"{got} ohm")


def check_output_stage(checker: Checker) -> None:
    """Analytical checks of the AO3401A high-side output stage."""
    pullup_on = GATE_NETWORK["pullup"] * (1 - GATE_NETWORK["tolerance"])
    pullup_off = GATE_NETWORK["pullup"] * (1 + GATE_NETWORK["tolerance"])
    series = GATE_NETWORK["series"] * (1 + GATE_NETWORK["tolerance"])
    rail = GATE_DRIVE["rail_min_v"]
    pullup = pullup_on

    gate_on = rail - (GATE_DRIVE["gpio_vol_max_v"] +
                      (rail - GATE_DRIVE["gpio_vol_max_v"]) * series / (pullup + series))
    checker.check("MIDI OUT: gate drive matches the reference case",
                  abs(gate_on - REFERENCE_GATE_ON_V) <= 0.002 * REFERENCE_GATE_ON_V,
                  f"|VGS| {gate_on:.4f} V, reference {REFERENCE_GATE_ON_V:.4f} V")
    checker.check("MIDI OUT: gate drive reaches the RDS(ON) test point (-2.5 V)",
                  gate_on >= abs(AO3401A["rds_on_test_vgs_v"]),
                  f"|VGS| {gate_on:.3f} V vs {abs(AO3401A['rds_on_test_vgs_v']):.1f} V, "
                  "so the 85 mOhm maximum applies at 25 C")
    checker.check("MIDI OUT: gate drive stays inside the +-12 V gate rating",
                  gate_on < AO3401A["vgs_max_v"], f"|VGS| {gate_on:.3f} V")

    rail_off = GATE_DRIVE["rail_max_v"]
    gate_off = rail_off - rail_off * GATE_DRIVE["pull_down_min_ohm"] / (
        GATE_DRIVE["pull_down_min_ohm"] + pullup_off + series)
    checker.check("MIDI OUT: off-state gate voltage matches the reference case",
                  abs(gate_off - REFERENCE_GATE_OFF_V) <= 0.005 * REFERENCE_GATE_OFF_V,
                  f"|VGS| {gate_off:.4f} V, reference {REFERENCE_GATE_OFF_V:.4f} V")
    checker.check("MIDI OUT: off state stays below the threshold magnitude (25 C only)",
                  gate_off < abs(AO3401A["vgs_threshold_min_v"]),
                  f"|VGS| {gate_off:.3f} V vs {abs(AO3401A['vgs_threshold_min_v']):.1f} V minimum "
                  "threshold magnitude. No full-temperature off guarantee.")
    checker.note("MIDI OUT: off-state and gate-drive behaviour is a 25 C calculation from the "
                 "datasheet limits. Full-temperature off state and switching edges from Qg typical "
                 "data alone are pending bench work")

    # Legacy receiver current, new stage and the replaced PNP stage.
    series_ohm = (OUT_SERIES["ring"] + OUT_SERIES["tip"]) * (1 + OUT_SERIES["tolerance"])
    receiver_ohm = CA033["legacy_receiver_ohm"] * (1 + CA033["legacy_receiver_tolerance"])
    new_a = (rail - CA033["legacy_led_max_vf"]) / (
        series_ohm + receiver_ohm + AO3401A["rds_on_max_ohm"])
    old_series = (22 + 33) * (1 + OUT_SERIES["tolerance"])
    old_a = (rail - 0.25 - CA033["legacy_led_max_vf"]) / (old_series + receiver_ohm)
    checker.check("MIDI OUT: legacy-receiver current matches the reference case",
                  abs(new_a - REFERENCE_LEGACY_NEW_A) <= 0.002 * REFERENCE_LEGACY_NEW_A,
                  f"{new_a * 1e3:.4f} mA, reference {REFERENCE_LEGACY_NEW_A * 1e3:.4f} mA")
    checker.check("MIDI OUT: legacy-receiver worst case now meets the CA-033 PC900V current",
                  new_a >= CA033["pc900v_worst_case_a"],
                  f"{new_a * 1e3:.2f} mA vs {CA033['pc900v_worst_case_a'] * 1e3:.1f} mA "
                  f"({new_a / CA033['pc900v_worst_case_a'] * 100:.0f} %), RDS(ON) at 85 mOhm, 25 C")
    checker.check("MIDI OUT: the replaced PNP stage would still fall short (negative reference)",
                  abs(old_a - REFERENCE_LEGACY_OLD_A) <= 0.002 * REFERENCE_LEGACY_OLD_A
                  and old_a < CA033["pc900v_worst_case_a"],
                  f"{old_a * 1e3:.4f} mA, reference {REFERENCE_LEGACY_OLD_A * 1e3:.4f} mA, "
                  f"below {CA033['pc900v_worst_case_a'] * 1e3:.1f} mA")
    # A hot RDS(ON) does not change the conclusion.
    hot_a = (rail - CA033["legacy_led_max_vf"]) / (series_ohm + receiver_ohm + 0.15)
    checker.check("MIDI OUT: the conclusion holds with a 150 mOhm hot RDS(ON) assumption",
                  hot_a >= CA033["pc900v_worst_case_a"], f"{hot_a * 1e3:.2f} mA")

    # Fault dissipation per resistor at the strongest rail.
    rail_max = 3.465
    ring_min = OUT_SERIES["ring"] * (1 - OUT_SERIES["tolerance"])
    tip_min = OUT_SERIES["tip"] * (1 - OUT_SERIES["tolerance"])
    rds = AO3401A["rds_on_max_ohm"]
    i_ring = rail_max / (ring_min + rds)
    i_tip = rail_max / (tip_min + rds)
    i_loop = rail_max / (ring_min + tip_min + rds)
    fault = {
        "ring_to_ground": i_ring * i_ring * ring_min,
        "tip_to_positive_rail": i_tip * i_tip * tip_min,
        "ring_tip_ring": i_loop * i_loop * ring_min,
        "ring_tip_tip": i_loop * i_loop * tip_min,
    }
    for key, watts in fault.items():
        checker.check(f"MIDI OUT: fault power {key.replace('_', ' ')} matches the reference case",
                      abs(watts - REFERENCE_FAULT_W[key]) <= 0.01 * REFERENCE_FAULT_W[key],
                      f"{watts:.4f} W, reference {REFERENCE_FAULT_W[key]:.4f} W")
        checker.check(f"MIDI OUT: fault power {key.replace('_', ' ')} inside the 1 W 2512 rating",
                      watts < OUT_SERIES["rated_w"], f"{watts:.3f} W of {OUT_SERIES['rated_w']:.0f} W")
    checker.note("MIDI OUT: 1 W 2512 derating above 70 C is not read from a primary resistor "
                 "datasheet in this pass; the research agent verifies it")

    # Deviation from CA-033: the reference wants RC = 10 ohm.
    ca033_tip_fault = rail_max * rail_max / (CA033["ca033_tip_ohm"] * 0.95)
    checker.check("MIDI OUT: a CA-033 10 ohm tip resistor would be unsafe (deviation justified)",
                  abs(ca033_tip_fault - REFERENCE_CA033_TIP_FAULT_W) <= 0.01 * REFERENCE_CA033_TIP_FAULT_W
                  and ca033_tip_fault > OUT_SERIES["rated_w"],
                  f"{ca033_tip_fault:.4f} W in a tip-to-positive-rail fault against a "
                  f"{CA033['ca033_resistor_w']:.2f} W CA-033 part; this design uses "
                  f"{OUT_SERIES['tip']} ohm 1 W instead of {CA033['ca033_tip_ohm']} ohm")


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
    open_count = 0
    expected_open = OPEN_BY_DESIGN.get(model.board, {})
    for warning in model.elements.get("source_pin_missing_trace_warning", []):
        message = warning.get("message") or ""
        match = UNCONNECTED.match(message)
        if not match:
            unexpected.append(message)
            continue
        pin, component = int(match.group(1)), match.group(2)
        if pin in expected_open.get(component, set()):
            open_count += 1
        else:
            unexpected.append(message)
    checker.check(f"{model.board}: no pin that must be connected is unconnected",
                  not unexpected,
                  "; ".join(sorted(set(unexpected))[:4])
                  or f"{open_count} pins open by design")

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
    check_probe_points(checker, rear)
    check_front(checker, front)
    for model in (rear, front):
        check_copper(checker, model)

    # Direct mate: the two connectors are on opposite board faces and tscircuit
    # mirrors the bottom one, so equal net names alone do not prove a mate. Every
    # corresponding pad must sit at the same board X/Y as well.
    problems = []
    for pin in range(1, 27):
        a, b = rear.pad("J_FRONT", pin), front.pad("J5", pin)
        if not a or not b:
            problems.append(f"pin {pin} missing on one board")
            continue
        if a["net"] != b["net"]:
            problems.append(f"pin {pin}: rear {a['net']!r} vs front {b['net']!r}")
        gap = math.hypot(a["x"] - b["x"], a["y"] - b["y"])
        if gap > 1e-6:
            problems.append(f"pin {pin} pads are {gap:.3f} mm apart")
        for label, pad in (("rear", a), ("front", b)):
            hole = pad["raw"].get("hole_diameter")
            pad_d = pad["raw"].get("outer_diameter")
            if abs((hole or 0) - INTERCONNECT_DRILL_MM) > 1e-9:
                problems.append(f"pin {pin}: {label} drill {hole} mm is not {INTERCONNECT_DRILL_MM} mm")
            if abs((pad_d or 0) - INTERCONNECT_PAD_MM) > 1e-9:
                problems.append(f"pin {pin}: {label} pad {pad_d} mm is not {INTERCONNECT_PAD_MM} mm")
    checker.check("both boards: J_FRONT and J5 mate pin for pin on the same pads (direct pair)",
                  not problems, "; ".join(problems[:4]))

    check_midi_current(checker)
    check_output_stage(checker)

    print(f"\n{checker.passed} checks passed, {len(checker.failed)} failed")
    if checker.failed:
        print("Failed checks:")
        for failure in checker.failed:
            print(f"  - {failure}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())

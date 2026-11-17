#!/usr/bin/env python3
"""Check built control-board hole geometry and the jack barrel copper exclusion.

This reads the built circuit JSON, not the footprint source. It does not certify
physical fit or the PCB manufacturer's finished hole tolerances. Run after build:
    python3 scripts/check_fab_mechanics.py
"""
import json
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
BOARD = os.path.join(HERE, "../dist/index/circuit.json")
EPS = 1e-4
BARREL_RADIUS = 1.5  # Thonk drawing's 3mm diameter precaution


def close(a, b):
    return abs(a - b) <= EPS


def point_segment(x, y, a, b):
    dx, dy = b[0] - a[0], b[1] - a[1]
    if dx == 0 and dy == 0:
        return math.hypot(x - a[0], y - a[1])
    t = max(0, min(1, ((x - a[0]) * dx + (y - a[1]) * dy) / (dx * dx + dy * dy)))
    return math.hypot(x - a[0] - t * dx, y - a[1] - t * dy)


def ring_points(ring):
    return [(p["x"], p["y"]) for p in ring["vertices"]]


def in_ring(x, y, points):
    inside = False
    for a, b in zip(points, points[1:] + points[:1]):
        if (a[1] > y) != (b[1] > y):
            crossing = (b[0] - a[0]) * (y - a[1]) / (b[1] - a[1]) + a[0]
            if x < crossing:
                inside = not inside
    return inside


def disk_intersects_pour(x, y, radius, pour):
    """Test a disk against the actual emitted BREP and its holes."""
    if pour.get("shape") != "brep" or not pour.get("brep_shape"):
        raise ValueError("unsupported pour: cannot verify jack copper exclusion")
    brep = pour["brep_shape"]
    rings = [ring_points(brep["outer_ring"])] + [ring_points(r) for r in brep.get("inner_rings", [])]
    if any(len(r) < 3 for r in rings):
        raise ValueError("invalid pour ring")
    if in_ring(x, y, rings[0]) and not any(in_ring(x, y, r) for r in rings[1:]):
        return True
    for ring in rings:
        if any(point_segment(x, y, a, b) < radius - EPS for a, b in zip(ring, ring[1:] + ring[:1])):
            return True
    return False


def verify(board):
    errors = []
    sources = {e["source_component_id"]: e for e in board if e["type"] == "source_component"}
    comps = {sources[e["source_component_id"]]["name"]: e for e in board
             if e["type"] == "pcb_component" and e.get("source_component_id") in sources}
    pads = {}
    for e in board:
        if e["type"] == "pcb_plated_hole":
            pads.setdefault(e.get("pcb_component_id"), []).append(e)
    keepouts = [e for e in board if e["type"] == "pcb_keepout"]
    copper_pads = [e for e in board if e["type"] in ("pcb_plated_hole", "pcb_smtpad")]
    pours = [e for e in board if e["type"] == "pcb_copper_pour"]
    traces = [e for e in board if e["type"] == "pcb_trace"]
    vias = [e for e in board if e["type"] == "pcb_via"]
    if not pours:
        errors.append("no built copper pour to verify")

    # A rectangular blade must fit the rounded slot, not just its bounding box.
    # The Thonk mono drawing gives 1.5 x 0.6mm and +/-0.15mm general tolerance;
    # JLC's published plated-slot size tolerance is -0.08mm. Verify the worst
    # assumed blade against the worst assumed finished obround, on paper only.
    slot_l, slot_w = 2.4 - 0.08, 1.2 - 0.08
    blade_l, blade_w = 1.5 + 0.15, 0.6 + 0.15
    rect_l = slot_l - slot_w + 2 * math.sqrt((slot_w / 2) ** 2 - (blade_w / 2) ** 2)
    if blade_w >= slot_w or blade_l >= rect_l:
        errors.append("jack slot does not contain the assumed worst-case rectangular blade")

    for n in range(3, 15):
        ref = f"J{n}"
        c = comps.get(ref)
        if not c:
            errors.append(f"{ref}: missing")
            continue
        pos = c["footprint_origin"]
        x, y = pos["x"], pos["y"]
        ph = pads.get(c["pcb_component_id"], [])
        if len(ph) != 3 or any(h.get("shape") != "oval" or
                not close(h.get("hole_width", 0), 2.4) or not close(h.get("hole_height", 0), 1.2) or
                (h.get("outer_width", 0) - 2.4) / 2 < 0.18 - EPS or
                (h.get("outer_height", 0) - 1.2) / 2 < 0.18 - EPS for h in ph):
            errors.append(f"{ref}: expected three 2.4 x 1.2mm plated slots with adequate rings")
        # These are the original barrel-relative centres and electrical ports.
        # The unnumbered mono switched contact deliberately stays unused.
        positions = [(6.48, "pin1"), (-4.92, "pin2"), (3.38, None)] if n <= 12 else [
            (4.92, "pin1"), (-3.38, "pin2"), (-6.48, "pin3")]
        for dy, pin in positions:
            found = [h for h in ph if close(h["x"], x) and close(h["y"], y + dy)]
            if len(found) != 1 or (pin is not None and pin not in found[0].get("port_hints", [])) or (
                    pin is None and found and found[0].get("pcb_port_id")):
                errors.append(f"{ref}: incorrect terminal centre or pin at relative y={dy}")
        matching = [k for k in keepouts if k.get("shape") == "circle" and
                    close(k["center"]["x"], x) and close(k["center"]["y"], y) and
                    k.get("radius", 0) >= 1.8 - EPS and set(k.get("layers", [])) >= {"top", "bottom"}]
        if len(matching) != 1:
            errors.append(f"{ref}: expected one two-layer barrel keepout")
        for trace in traces:
            route = trace.get("route", [])
            for a, b in zip(route, route[1:]):
                if a.get("route_type") != "wire" or b.get("route_type") != "wire" or a.get("layer") != b.get("layer"):
                    continue
                width = max(a.get("width", 0), b.get("width", 0))
                if point_segment(x, y, (a["x"], a["y"]), (b["x"], b["y"])) < BARREL_RADIUS + width / 2 - EPS:
                    errors.append(f"{ref}: trace {trace.get('pcb_trace_id')} enters the barrel disk")
                    break
        for via in vias:
            if math.hypot(x - via["x"], y - via["y"]) < BARREL_RADIUS + via["outer_diameter"] / 2 - EPS:
                errors.append(f"{ref}: via enters the barrel disk")
        # Use a conservative bounding rectangle for each copper pad. This
        # also covers PTH lands; allowPlacements must not hide a pad collision.
        for p in copper_pads:
            shape = p.get("shape")
            if p["type"] == "pcb_smtpad":
                if shape == "circle":
                    w = h = p.get("radius", 0) * 2
                else:
                    w, h = p.get("width", 0), p.get("height", 0)
            elif shape == "circle":
                w = h = p.get("outer_diameter", 0)
            elif shape == "oval":
                w, h = p.get("outer_width", 0), p.get("outer_height", 0)
            elif shape == "circular_hole_with_rect_pad":
                w, h = p.get("rect_pad_width", 0), p.get("rect_pad_height", 0)
            else:
                errors.append(f"{ref}: unsupported copper pad shape {shape}")
                continue
            if w <= 0 or h <= 0:
                errors.append(f"{ref}: cannot verify unmeasured pad {p.get('pcb_smtpad_id', p.get('pcb_plated_hole_id'))}")
                continue
            angle = math.radians(p.get("ccw_rotation", p.get("rect_ccw_rotation", 0)) or 0)
            w, h = (abs(w * math.cos(angle)) + abs(h * math.sin(angle)),
                    abs(w * math.sin(angle)) + abs(h * math.cos(angle)))
            dx = max(0, abs(p["x"] - x) - w / 2)
            dy = max(0, abs(p["y"] - y) - h / 2)
            if math.hypot(dx, dy) < BARREL_RADIUS - EPS:
                errors.append(f"{ref}: copper pad enters the barrel disk")
        for pour in pours:
            try:
                if disk_intersects_pour(x, y, BARREL_RADIUS, pour):
                    errors.append(f"{ref}: {pour.get('layer')} copper pour enters the barrel disk")
            except (KeyError, ValueError) as exc:
                errors.append(f"{ref}: cannot check copper pour: {exc}")

    for n in range(1, 7):
        ref = f"RV{n}"
        c = comps.get(ref)
        if not c:
            errors.append(f"{ref}: missing")
            continue
        ph = pads.get(c["pcb_component_id"], [])
        lugs = [p for p in ph if not p.get("pcb_port_id")]
        if len(lugs) != 2 or any(p.get("shape") != "circle" or
                not close(p.get("hole_diameter", 0), 3) or not close(p.get("outer_diameter", 0), 4)
                for p in lugs):
            errors.append(f"{ref}: expected two 3mm round plated lug holes")
        origin = c["footprint_origin"]
        for dx in (-4.4, 4.4):
            if not any(close(p["x"], origin["x"] + dx) and close(p["y"], origin["y"] - 0.5) for p in lugs):
                errors.append(f"{ref}: incorrect lug centre at relative x={dx}")
        for lug in lugs:
            x, y = lug["x"], lug["y"]
            edge = lug.get("outer_diameter", 0) / 2 + 0.1  # board trace/pad clearance
            for trace in traces:
                route = trace.get("route", [])
                for a, b in zip(route, route[1:]):
                    if a.get("route_type") != "wire" or b.get("route_type") != "wire" or a.get("layer") != b.get("layer"):
                        continue
                    width = max(a.get("width", 0), b.get("width", 0))
                    if point_segment(x, y, (a["x"], a["y"]), (b["x"], b["y"])) < edge + width / 2 - EPS:
                        errors.append(f"{ref}: trace too close to lug copper")
                        break
            for via in vias:
                if math.hypot(x - via["x"], y - via["y"]) < edge + via["outer_diameter"] / 2 - EPS:
                    errors.append(f"{ref}: via too close to lug copper")
            for pour in pours:
                try:
                    if disk_intersects_pour(x, y, edge, pour):
                        errors.append(f"{ref}: {pour.get('layer')} pour too close to lug copper")
                except (KeyError, ValueError) as exc:
                    errors.append(f"{ref}: cannot check lug copper clearance: {exc}")

    # Check built switch pin centres and their actual source connectivity, not
    # just the common hole spacing. A green mux gate alone cannot catch a swap.
    net_keys = {e.get("name"): e.get("subcircuit_connectivity_map_key") for e in board
                if e["type"] == "source_net"}
    switch_pins = {
        "SW1": [(1, -5.25, 2.8, 1.3, "M_ADC_6"), (2, -5.25, 0, 1.3, "JOY_LEFT"),
                (3, -5.25, -3.7, 1.3, "GND"), (4, 5.25, 2.8, 1.3, "JOY_UP"),
                (5, 5.25, 0, 1.3, "JOY_RIGHT"), (6, 5.25, -3.7, 1.3, "JOY_DOWN")],
        "SW2": [(1, -6.05, 2.5, 1.3, "M_ADC_7"), (2, 6.05, 2.5, 1.3, "M_ADC_7"),
                (3, -6.05, -2.5, 1.3, "GND"), (4, 6.05, -2.5, 1.3, "GND"),
                (5, 0, 6.75, 1.0, "BTN_LED"), (6, 0, -6.75, 1.0, "M_ADC_7")],
    }
    for ref, positions in switch_pins.items():
        c = comps.get(ref)
        if not c:
            errors.append(f"{ref}: missing")
            continue
        origin = c["footprint_origin"]
        ph = pads.get(c["pcb_component_id"], [])
        ports = [e for e in board if e["type"] == "source_port" and
                 e.get("source_component_id") == c["source_component_id"]]
        for pin, dx, dy, diameter, net in positions:
            found = [p for p in ph if f"pin{pin}" in p.get("port_hints", [])]
            sp = [p for p in ports if p.get("pin_number") == pin]
            if len(found) != 1 or not close(found[0]["x"], origin["x"] + dx) or not close(found[0]["y"], origin["y"] + dy) or not close(found[0].get("hole_diameter", 0), diameter):
                errors.append(f"{ref}: incorrect pin{pin} hole position or diameter")
            pcb_port = next((p for p in board if p["type"] == "pcb_port" and found and
                             p.get("pcb_port_id") == found[0].get("pcb_port_id")), None)
            if len(sp) != 1 or not net_keys.get(net) or sp[0].get("subcircuit_connectivity_map_key") != net_keys[net] or not pcb_port or pcb_port.get("source_port_id") != sp[0].get("source_port_id"):
                errors.append(f"{ref}: pin{pin} is not on {net}")

    sw = comps.get("SW2")
    if not sw:
        errors.append("SW2: missing")
    else:
        ph = pads.get(sw["pcb_component_id"], [])
        pegs = [p for p in ph if not p.get("pcb_port_id")]
        if len(pegs) != 2 or any(p.get("shape") != "circle" or
                not close(p.get("hole_diameter", 0), 1.9) or
                p.get("outer_diameter", 0) - p.get("hole_diameter", 0) < 0.5 - EPS
                for p in pegs):
            errors.append("SW2: peg holes need 1.9mm drills and >=0.25mm nominal PTH rings")
        origin = sw["footprint_origin"]
        for dy in (-4.35, 4.35):
            if not any(close(p["x"], origin["x"]) and close(p["y"], origin["y"] + dy) for p in pegs):
                errors.append(f"SW2: incorrect peg centre at relative y={dy}")
    return errors


if __name__ == "__main__":
    try:
        with open(BOARD) as f:
            errors = verify(json.load(f))
    except (OSError, ValueError, KeyError) as exc:
        sys.exit(f"FAIL cannot verify {BOARD}: {exc}")
    for error in errors:
        print("FAIL", error)
    if errors:
        sys.exit(f"FAIL fabrication geometry: {len(errors)} problem(s)")
    print("PASS - jack slots/barrel copper, pot lug holes/clearance, switch pins and SW2 peg rings")
    print("Physical first-article fit and CAM acceptance remain required.")

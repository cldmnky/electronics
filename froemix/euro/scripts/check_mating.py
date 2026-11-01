#!/usr/bin/env python3
"""Euro board mechanical + connectivity gate.

Reads the BUILT circuit JSON of both boards and proves three things:

1. The three mating sockets sit exactly on the base board's frozen connector
   coordinates, pin for pin, on the bottom side. The sockets are pre-mirrored
   footprints (pin 1 at +X in footprint space), so this also catches a broken
   mirror: pin 1 must land on the base board's pin 1.
2. Every front-panel part is where lib/layout.ts says it is, and every panel
   hole is above its part - the panel and the board read the same table, and
   this is what proves it.
3. The board routed completely: 0 unconnected ports, 0 trace errors.

Run `tsci build` first (scripts/gate.sh does).
"""

import json
import sys

BOARD = "dist/index/circuit.json"
PANEL = "dist/panel/circuit.json"

# Frozen connector contract with the base board (froemix/AGENTS.md §3). The
# base board is the authority; these are its built coordinates.
MATE = {
    "CN1": {"x": -17.58, "y": 59.64, "pins": 16},
    "CN2": {"x": 12.42, "y": -36.36, "pins": 16},
    "CN4": {"x": 24.5, "y": 59.63, "pins": 7},
}
PITCH = 2.0

FAILS = []


def fail(msg):
    FAILS.append(msg)
    print(f"  FAIL {msg}")


def load(path):
    with open(path) as f:
        return json.load(f)


def index(cj):
    """source_component name -> list of {name, x, y, layer} ports."""
    comps = {e["source_component_id"]: e for e in cj if e["type"] == "source_component"}
    srcports = {e["source_port_id"]: e for e in cj if e["type"] == "source_port"}
    pcbcomps = {e["pcb_component_id"]: e for e in cj if e["type"] == "pcb_component"}
    out = {}
    centres = {}
    for cid, c in comps.items():
        if c.get("name"):
            out.setdefault(c["name"], [])
            centres[c["name"]] = None
    for pcid, pc in pcbcomps.items():
        src = comps.get(pc.get("source_component_id"), {})
        if src.get("name"):
            # footprint_origin is the pcbX/pcbY we placed the part at; `center`
            # is the bounding-box centre and is not the placement point.
            centres[src["name"]] = pc.get("footprint_origin") or pc.get("center")
    for e in cj:
        if e["type"] != "pcb_port":
            continue
        sp = srcports.get(e.get("source_port_id"))
        if not sp:
            continue
        comp = comps.get(sp.get("source_component_id"), {})
        name = comp.get("name")
        if not name:
            continue
        out.setdefault(name, []).append(
            {"name": sp.get("name"), "x": e["x"], "y": e["y"],
             "layers": e.get("layers", []), }
        )
    return out, centres


# ---------------------------------------------------------------- 1. mating
board = load(BOARD)
ports, centres = index(board)
print("mating sockets (bottom side, onto the base board headers):")
for ref, spec in MATE.items():
    half = (spec["pins"] - 1) * PITCH / 2
    got = {}
    for p in ports.get(ref, []):
        n = p["name"]
        if not n.startswith("pin"):
            continue
        try:
            got[int(n[3:])] = p
        except ValueError:
            pass
    if len(got) != spec["pins"]:
        fail(f"{ref}: expected {spec['pins']} pads, found {len(got)}")
        continue
    for pin in (1, spec["pins"]):
        want_x = spec["x"] - half if pin == 1 else spec["x"] + half
        p = got[pin]
        if abs(p["x"] - want_x) > 0.01 or abs(p["y"] - spec["y"]) > 0.01:
            fail(
                f"{ref} pin{pin} at ({p['x']:.3f}, {p['y']:.3f}), "
                f"base board has ({want_x:.3f}, {spec['y']:.3f}) - mirror or offset is wrong"
            )
    # every pin must be 2.00mm apart on the row
    ys = {round(p["y"], 2) for p in got.values()}
    if len(ys) != 1:
        fail(f"{ref}: pads are not on one row (y = {sorted(ys)})")
    print(f"  {ref}: {spec['pins']} pads, pin1 x={got[1]['x']:.2f}, "
          f"pin{spec['pins']} x={got[spec['pins']]['x']:.2f}, y={spec['y']:.2f}")

# the sockets must be on the bottom
for e in board:
    if e["type"] != "pcb_component":
        continue
    src = next((s for s in board
                if s["type"] == "source_component"
                and s["source_component_id"] == e.get("source_component_id")), {})
    if src.get("name") in MATE and e.get("layer") != "bottom":
        fail(f"{src['name']}: layer is {e.get('layer')}, must be bottom")

# ------------------------------------------------- 2. panel <-> board alignment
panel = load(PANEL)
p_holes = [e for e in panel if e["type"] == "pcb_hole"]
p_cut = [e for e in panel if e["type"] == "pcb_cutout"]


def hole_at(x, y, tol=0.01):
    return any(abs(h["x"] - x) < tol and abs(h["y"] - y) < tol for h in p_holes)


import os
import re  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
LAYOUT = os.path.join(HERE, "..", "lib", "layout.ts")
lay = open(LAYOUT).read()

# Every front-panel part is one `{ name: "X", ... x: <n>, y: <n> ... }` block in
# lib/layout.ts. Read them all - pots, jacks, jack LEDs, the joystick and the
# button - without executing TypeScript.
def objects(text):
    """Every top-level {...} block, brace-matched (the joystick has a nested ladder)."""
    out, i = [], 0
    while True:
        i = text.find("{", i)
        if i < 0:
            return out
        depth, j = 0, i
        while j < len(text):
            if text[j] == "{":
                depth += 1
            elif text[j] == "}":
                depth -= 1
                if depth == 0:
                    break
            j += 1
        out.append(text[i:j + 1])
        i = j + 1


PARTS = []
for body in objects(lay):
    mn = re.search(r"\bname:\s*\"([^\"]+)\"", body)
    mx = re.search(r"\bx:\s*(-?[\d.]+)", body)
    my = re.search(r"\by:\s*(-?[\d.]+)", body)
    if mn and mx and my:
        PARTS.append({"name": mn.group(1), "x": float(mx.group(1)), "y": float(my.group(1))})

print(f"front-panel parts, board vs panel ({len(PARTS)} declared):")
checked = 0
claimed = []
for part in PARTS:
    c = centres.get(part["name"])
    if c is None:
        fail(f"{part['name']}: declared in layout.ts but not placed on the board")
        continue
    checked += 1
    if abs(c["x"] - part["x"]) > 0.01 or abs(c["y"] - part["y"]) > 0.01:
        fail(f"{part['name']} placed at ({c['x']:.2f}, {c['y']:.2f}), layout says "
             f"({part['x']:.2f}, {part['y']:.2f}) - footprint origin is not the panel hole")
    if not hole_at(part["x"], part["y"]):
        fail(f"{part['name']}: no panel hole at ({part['x']:.2f}, {part['y']:.2f})")
    claimed.append((part["x"], part["y"]))
print(f"  {checked} parts checked")

# visible panel LED: placed at LED_AT in sections/display.tsx, not a table row
led_at = re.search(r"(?<!O)LED_AT: Pt = \{ x: (-?[\d.]+), y: (-?[\d.]+) \}", lay)
if led_at:
    lx, ly = float(led_at.group(1)), float(led_at.group(2))
    if not hole_at(lx, ly):
        fail(f"panel indicator LED: no hole at ({lx:.2f}, {ly:.2f})")
    claimed.append((lx, ly))

# Doepfer rail screws: 7.5mm in from each side, 3.0mm from top and bottom
panel_w = float(re.search(r"PANEL_W = ([\d.]+)", lay).group(1))
panel_h = float(re.search(r"PANEL_H = ([\d.]+)", lay).group(1))
rail_x, rail_y = panel_w / 2 - 7.5, panel_h / 2 - 3.0
for rx in (-rail_x, rail_x):
    for ry in (-rail_y, rail_y):
        if not hole_at(rx, ry):
            fail(f"no rail screw hole at ({rx:.2f}, {ry:.2f})")
        claimed.append((rx, ry))

orphans = [h for h in p_holes
           if not any(abs(h["x"] - cx) < 0.01 and abs(h["y"] - cy) < 0.01 for cx, cy in claimed)]
if orphans:
    fail(f"{len(orphans)} panel holes have no part: "
         + ", ".join(f"({h['x']:.1f},{h['y']:.1f})" for h in orphans[:6]))
print(f"  {len(p_holes)} panel holes, all claimed")

# display window
oled_y = float(re.search(r"OLED_AT: Pt = \{ x: (-?[\d.]+), y: (-?[\d.]+) \}", lay).group(2))
win_dy = float(re.search(r"windowOffsetY: (-?[\d.]+)", lay).group(1))
if p_cut:
    wx, wy = 0.0, oled_y + win_dy
    cut = p_cut[0]
    cx = cut.get("center", {}).get("x", cut.get("x"))
    cy = cut.get("center", {}).get("y", cut.get("y"))
    if abs(cx - wx) > 0.01 or abs(cy - wy) > 0.01:
        fail(f"panel window at ({cx:.2f}, {cy:.2f}), layout says ({wx:.2f}, {wy:.2f})")
    else:
        print(f"  display window ({cut['width']} x {cut['height']}mm) at ({wx:.2f}, {wy:.2f})")

# --------------------------------------------- 3. connectivity of the board
unconnected = [e for e in board if e["type"] == "pcb_port_not_connected_error"]
trace_err = [e for e in board
             if e["type"] in ("pcb_trace_error", "pcb_trace_missing_error")]
traces = [e for e in board if e["type"] == "pcb_trace"]
vias = sum(1 for e in board if e["type"] == "pcb_via")
copper = 0.0
for t in traces:
    pts = [p for p in t.get("route", []) if p.get("route_type") == "wire"]
    for a, b in zip(pts, pts[1:]):
        copper += ((b["x"] - a["x"]) ** 2 + (b["y"] - a["y"]) ** 2) ** 0.5
print(f"routing: {len(traces)} traces, {vias} vias, {copper:.0f}mm copper, "
      f"{len(unconnected)} unconnected ports, {len(trace_err)} trace errors")
if unconnected:
    fail(f"{len(unconnected)} unconnected ports")
if trace_err:
    fail(f"{len(trace_err)} trace errors")

print()
if FAILS:
    print(f"FAILED ({len(FAILS)} problems)")
    sys.exit(1)
print("PASS - mating, panel alignment and routing all check out")

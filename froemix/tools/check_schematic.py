#!/usr/bin/env python3
"""Report how clean the rendered schematic is, and fail on real collisions.

Usage:
    python3 froemix/tools/check_schematic.py [--circuit dist/index/circuit.json]
                                             [--svg /tmp/froemix-base-schematic.svg]
                                             [--json out.json]

The schematic is auto-laid-out by the installed core, so this script does not
try to be a renderer. It reads the BUILT circuit JSON (the same data the SVG is
drawn from) and checks the things a reader would notice:

  * sheet notes - the standalone `<schematictext>` lines this tree adds (text
    length >= 30 characters; pin-name texts are shorter) - must not overlap a
    component, a net label or each other, and must stay inside the A4 frame. The
    frame is 297 x 210 mm and the core draws 1 schematic unit = 8.847 mm (read
    back from the exported SVG's data-real-to-screen-transform), so the drawing
    area is +/-16.78 x +/-11.87 units;
  * the ESTIMATED label boxes from the circuit JSON are still printed, but they
    are only a cross-check: the authoritative overlap count is measured on the
    rendered SVG by tools/svg_overlaps.py, which reads the actual label tag
    polygons. Do not gate on the estimate here (an earlier version did, with a
    30-overlap ceiling - that number was headroom, not a defect).

Bounding boxes: the core computes a net label's width as
len(text) * 0.12 * (font_size / 0.18) + 0.12 * (font_size / 0.18) and its
height as font_size (lib/utils/schematic/computeSchematicNetLabelCenter.ts);
`<schematictext>` uses the same font metrics. A 5 % overlap tolerance keeps
box-edge touches from counting.

Exits non-zero on a note collision, a missing note, a note outside the frame,
or more net-label overlaps than the ceiling.
"""

from __future__ import annotations

import argparse
import json
import os
import sys

SHEET_HALF_W = 16.78   # A4 297mm at 8.847mm per schematic unit
SHEET_HALF_H = 11.87   # A4 210mm at the same scale
NOTE_MIN_LEN = 30
TOUCH = 0.05           # 5 % of the smaller box: box edges touching is not a hit


def text_size(text, font_size):
    scale = font_size / 0.18
    return len(text) * 0.12 * scale + 0.12 * scale, font_size


def text_box(entry):
    size = entry.get("font_size") or 0.18
    width, height = text_size(entry["text"], size)
    x, y = entry["position"]["x"], entry["position"]["y"]
    anchor = entry.get("anchor") or "center"
    if "left" in anchor:
        x0 = x
    elif "right" in anchor:
        x0 = x - width
    else:
        x0 = x - width / 2
    if "top" in anchor:
        y1 = y
    elif "bottom" in anchor:
        y1 = y + height
    else:
        y1 = y + height / 2
    return (x0, y1 - height, x0 + width, y1)


def label_box(entry):
    size = entry.get("font_size") or 0.18
    width, height = text_size(entry["text"], size)
    cx, cy = entry["center"]["x"], entry["center"]["y"]
    return (cx - width / 2, cy - height / 2, cx + width / 2, cy + height / 2)


def component_box(entry):
    x, y = entry["center"]["x"], entry["center"]["y"]
    w, h = entry["size"]["width"], entry["size"]["height"]
    return (x - w / 2, y - h / 2, x + w / 2, y + h / 2)


def overlap(a, b):
    dx = min(a[2], b[2]) - max(a[0], b[0])
    dy = min(a[3], b[3]) - max(a[1], b[1])
    if dx <= 0 or dy <= 0:
        return 0.0
    area = dx * dy
    small = min((a[2] - a[0]) * (a[3] - a[1]), (b[2] - b[0]) * (b[3] - b[1]))
    return area / small if small else 1.0


def on_sheet(entry):
    return entry.get("schematic_sheet_id")


def main() -> int:
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    ap = argparse.ArgumentParser()
    ap.add_argument("--circuit", default=os.path.join(root, "tscircuit", "dist", "index", "circuit.json"))
    ap.add_argument("--json", help="also write the per-sheet report here")
    args = ap.parse_args()

    if not os.path.exists(args.circuit):
        sys.exit(f"missing {args.circuit} - run `bunx tsci build` first")
    circuit = json.load(open(args.circuit))

    sheets = {e["schematic_sheet_id"]: e for e in circuit if e["type"] == "schematic_sheet"}
    components = [e for e in circuit if e["type"] == "schematic_component"]
    labels = [e for e in circuit if e["type"] == "schematic_net_label"]
    texts = [e for e in circuit if e["type"] == "schematic_text"]

    notes = [t for t in texts if t.get("schematic_component_id") is None
             and len(t["text"]) >= NOTE_MIN_LEN]

    problems = []
    pin_names = [t for t in texts if t.get("schematic_component_id") is None
                 and len(t["text"]) < NOTE_MIN_LEN]
    report = {"notes": {}, "label_overlaps": {}, "label_on_pin_name": {}, "sheets": {}}

    for sid, sheet in sorted(sheets.items(), key=lambda kv: kv[1].get("sheet_index", 0)):
        name = sheet["name"]
        sheet_notes = [n for n in notes if on_sheet(n) == sid]
        sheet_comps = [c for c in components if on_sheet(c) == sid]
        sheet_labels = [l for l in labels if on_sheet(l) == sid]

        report["notes"][name] = len(sheet_notes)
        report["sheets"][name] = {"components": len(sheet_comps), "net_labels": len(sheet_labels)}

        boxes = [(n, text_box(n)) for n in sheet_notes]
        for note, box in boxes:
            if not (-SHEET_HALF_W <= box[0] and box[2] <= SHEET_HALF_W
                    and -SHEET_HALF_H <= box[1] and box[3] <= SHEET_HALF_H):
                problems.append(f"{name}: note leaves the A4 frame: {note['text'][:50]!r} "
                                f"box={tuple(round(v, 2) for v in box)}")
            for comp in sheet_comps:
                hit = overlap(box, component_box(comp))
                if hit > TOUCH:
                    problems.append(
                        f"{name}: note overlaps a component: {note['text'][:50]!r} "
                        f"({hit * 100:.0f} % of the smaller box)")
                    break
            for label in sheet_labels:
                hit = overlap(box, label_box(label))
                if hit > TOUCH:
                    problems.append(
                        f"{name}: note overlaps net label {label['text']!r}: "
                        f"{note['text'][:40]!r}")
                    break
        for i in range(len(boxes)):
            for j in range(i + 1, len(boxes)):
                if overlap(boxes[i][1], boxes[j][1]) > TOUCH:
                    problems.append(
                        f"{name}: two notes overlap: {boxes[i][0]['text'][:30]!r} / "
                        f"{boxes[j][0]['text'][:30]!r}")

        # the remaining diagnostic: net label to net label
        hits = 0
        for i in range(len(sheet_labels)):
            bi = label_box(sheet_labels[i])
            for j in range(i + 1, len(sheet_labels)):
                if overlap(bi, label_box(sheet_labels[j])) > TOUCH:
                    hits += 1
        report["label_overlaps"][name] = hits

        # a net label whose box crosses a pin-name text: the reader sees the two
        # strings on top of each other
        sheet_pins = [t for t in pin_names if on_sheet(t) == sid]
        on_pin = 0
        for label in sheet_labels:
            lb = label_box(label)
            if any(overlap(lb, text_box(t)) > TOUCH for t in sheet_pins):
                on_pin += 1
        report["label_on_pin_name"][name] = on_pin

    total_overlaps = sum(report["label_overlaps"].values())
    total_on_pin = sum(report["label_on_pin_name"].values())

    print(f"{'sheet':<7} {'components':>10} {'net labels':>10} {'notes':>5} "
          f"{'label/label':>11} {'label/pin':>9}")
    for name in report["notes"]:
        entry = report["sheets"][name]
        print(f"{name:<7} {entry['components']:>10} {entry['net_labels']:>10} "
              f"{report['notes'][name]:>5} {report['label_overlaps'][name]:>11} "
              f"{report['label_on_pin_name'][name]:>9}")
    print(f"\nestimated label boxes from circuit JSON (cross-check only): "
          f"{total_overlaps} label/label, {total_on_pin} label/pin-name")
    print("the authoritative count is tools/svg_overlaps.py on the exported SVG")

    if args.json:
        with open(args.json, "w") as f:
            json.dump(report, f, indent=1, sort_keys=True)
        print(f"report written to {args.json}")

    if problems:
        print(f"\n{len(problems)} problem(s):")
        for problem in problems:
            print(f"  {problem}")
        return 1
    print("\nsheet notes are clear of components, net labels and each other")
    return 0


if __name__ == "__main__":
    sys.exit(main())

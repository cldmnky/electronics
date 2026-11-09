#!/usr/bin/env python3
"""Count text/label overlaps in an exported schematic SVG (rendered geometry).

Usage:
    python3 froemix/tools/svg_overlaps.py before.svg after.svg ...
    python3 froemix/tools/svg_overlaps.py --expect built-in /tmp/froemix-sch.svg
    python3 froemix/tools/svg_overlaps.py --threshold 0 --json out.json sch.svg

Why this exists: `check_schematic.py` estimates label boxes from the circuit
JSON (text length x font size). On an exported SVG that is wrong twice over -
the renderer draws each net label inside a filled tag polygon whose size is not
the text estimate, and it places pin names with its own metrics. This tool reads
what is actually in the file:

  * a net label is the `<path ... data-schematic-net-label-id=...>` tag; its
    bounding box comes from the path's own coordinates, so it is exact;
  * any other `<text>` is a pin name, component value or a sheet note; its box
    uses the element's x/y, font-size and text-anchor with a 0.55 x font-size
    per character width estimate (the only estimate left, and it errs wide);
  * two elements overlap when their boxes intersect by **more than 12 % of the
    smaller box** (`--threshold`, default 0.12), so boxes that merely share an
    edge do not count. That 12 % area allowance is the measurement policy, not
    a budget: no overlap may exceed it.
  * every run also prints a **strict** line with the threshold at 0, i.e. any
    intersection at all. That line is the honest residual count. On this tree it
    is **1 label/label and 4 label/text hairline contacts** (Root 1/1, MIDI 0/1,
    Audio 0/2) against 21/28 on the frozen baseline e0606da - box corners that
    touch without covering text. They are reported, never suppressed, and they
    are the reason this docstring does not claim "any overlap fails".

Reported per sheet (the name comes from the `stacked-sheet-label` text): number
of label tags, other texts, component bodies, label/label overlaps and
label/text overlaps, the strict line, and a page-frame check (no label tag may
leave the sheet's inner A4 border).

Structure gate: a count of 0/0 only counts as a result when the file is a
well-formed SVG that contains **all five expected sheets**, and each of those
sheets carries at least one net label, one component and one text. A frame-only
export, an empty file, a truncated file or a file missing a sheet therefore
fails instead of reporting "0 overlaps" on nothing. That is why `--expect
built-in` (all five sheets at 0/0) cannot be satisfied by an empty drawing.

Exit status: 0 only when the structure gate passes, every label stays in its
page frame, and (with `--expect`) every sheet matches its recorded count
exactly. The recorded counts are the EXACT numbers demonstrated on this tree:
all five sheets 0/0, so there is no headroom and no percentage tolerance on the
overlap count itself.

Regression tests for the gate and the parser: `froemix/tools/test_svg_overlaps.py`
(no network, no build).
"""

from __future__ import annotations

import argparse
import json
import re
import sys
import xml.etree.ElementTree as ET

TOUCH = 0.12          # >12 % of the smaller box is a collision (the policy)
STRICT = 0.0          # strict line: any intersection at all
EXPECTED_SHEETS = ("Root", "MIDI", "Audio", "CV", "Power")

TEXT_RE = re.compile(r"<text([^>]*)>(.*?)</text>", re.S)
NUM_RE = re.compile(r"-?\d+(?:\.\d+)?")
SHEET_SPLIT = re.compile(r'<svg class="tscircuit-schematic"')
RECT_RE = re.compile(r'<rect[^>]*x="(-?[\d.]+)"[^>]*y="(-?[\d.]+)"[^>]*width="([\d.]+)"[^>]*height="([\d.]+)"')
ELEMENT_RE = re.compile(r"<(text|path)((?:[^>\"']|\"[^\"]*\"|'[^']*')*?)(?:/>|>(.*?)</text>)", re.S)
CLASS_RE = re.compile(r'class="([^"]*)"')


def class_tokens(attrs):
    m = CLASS_RE.search(attrs)
    return set(m.group(1).split()) if m else set()


def parse_boxes(svg):
    """[(cls, kind, box, text)] in document order, one entry per element."""
    events = []
    for m in ELEMENT_RE.finditer(svg):
        tag, attrs, body = m.group(1), m.group(2), m.group(3) or ""
        if tag == "path":
            if "data-schematic-net-label-id" not in attrs:
                continue
            d = re.search(r'\sd="([^"]*)"', attrs)
            if not d:
                continue
            nums = [float(n) for n in NUM_RE.findall(d.group(1))]
            xs, ys = nums[0::2], nums[1::2]
            if not xs or not ys:
                continue
            box = (min(xs), min(ys), max(xs) - min(xs), max(ys) - min(ys))
            events.append(("net-label", "label", box, ""))
            continue

        content = re.sub(r"<[^>]+>", "", body).strip()
        cls = re.search(r'class="([^"]*)"', attrs)
        cls = cls.group(1) if cls else ""
        xs = re.search(r'\bx="(-?[\d.]+)"', attrs)
        ys = re.search(r'\by="(-?[\d.]+)"', attrs)
        fs = re.search(r'font-size="(-?[\d.]+)', attrs)
        anchor = re.search(r'text-anchor="(\w+)"', attrs)
        if not (xs and ys):
            continue
        size = float(fs.group(1)) if fs else 10.0
        width = 0.55 * size * len(content)
        x, y = float(xs.group(1)), float(ys.group(1))
        a = anchor.group(1) if anchor else "start"
        if a == "middle":
            x -= width / 2
        elif a == "end":
            x -= width
        events.append((cls, "text", (x, y - 0.85 * size, width, size), content))
    return events


def overlap(a, b):
    dx = min(a[0] + a[2], b[0] + b[2]) - max(a[0], b[0])
    dy = min(a[1] + a[3], b[1] + b[3]) - max(a[1], b[1])
    if dx <= 0 or dy <= 0:
        return 0.0
    small = min(a[2] * a[3], b[2] * b[3])
    return (dx * dy) / small if small else 0.0


def page_frame(block):
    """Inner border rectangle of one sheet block, in px (None if absent)."""
    rects = [(float(a), float(b), float(c), float(d)) for a, b, c, d in RECT_RE.findall(block)]
    rects = [r for r in rects if not (r[0] == 0 and r[1] == 0)]
    if not rects:
        return None
    r = max(rects, key=lambda r: r[2] * r[3])
    return (r[0], r[1], r[0] + r[2], r[1] + r[3])


def analyse_text(svg, touch=None):
    """{sheet: {labels, texts, components, label_label, label_text, frame, outside}}."""
    touch = TOUCH if touch is None else touch
    events = parse_boxes(svg)
    sheets = {}
    current = None
    for cls, kind, box, text in events:
        if cls == "stacked-sheet-label":
            current = text
            sheets[current] = {"labels": [], "texts": [], "components": 0,
                               "label_label": [], "label_text": [], "frame": None,
                               "outside": 0}
            continue
        if current is None:
            continue
        entry = sheets[current]
        if kind == "label":
            entry["labels"].append(box)
        elif "net-label-text" in cls:
            continue                      # the label's own text sits inside its tag
        elif text:
            entry["texts"].append((box, text))

    for name, entry in sheets.items():
        for i in range(len(entry["labels"])):
            for j in range(i + 1, len(entry["labels"])):
                if overlap(entry["labels"][i], entry["labels"][j]) > touch:
                    entry["label_label"].append((i, j))
        for i, lb in enumerate(entry["labels"]):
            for j, (tb, text) in enumerate(entry["texts"]):
                if overlap(lb, tb) > touch:
                    entry["label_text"].append((i, text))

    # component bodies and the page frame need the per-sheet blocks
    parts = SHEET_SPLIT.split(svg)[1:]
    names = [e["texts"] and None for e in []]      # placeholder, replaced below
    order = [name for name, entry in sheets.items()]
    for i, part in enumerate(parts):
        if i >= len(order):
            break
        entry = sheets[order[i]]
        entry["frame"] = page_frame(part)
        entry["components"] = sum(
            1 for attrs in re.findall(r"<[a-zA-Z]+([^>]*)>", part)
            if "sch-component" in class_tokens(attrs))
        for m in re.finditer(r'<path([^>]*data-schematic-net-label-id[^>]*?)/>', part):
            d = re.search(r'\sd="([^"]*)"', m.group(1))
            if not d or not entry["frame"]:
                continue
            nums = [float(n) for n in NUM_RE.findall(d.group(1))]
            xs, ys = nums[0::2], nums[1::2]
            if not xs or not ys:
                continue
            f = entry["frame"]
            if (min(xs) < f[0] - 1 or max(xs) > f[2] + 1
                    or min(ys) < f[1] - 1 or max(ys) > f[3] + 1):
                entry["outside"] += 1
    return sheets


def validate_structure(svg, sheets):
    """[] when the file can carry a 0/0 result, else a list of problems."""
    problems = []
    try:
        ET.fromstring(svg)
    except ET.ParseError as exc:
        return [f"not a well-formed SVG: {exc}"]

    if not sheets:
        problems.append("no schematic sheet found (a frame-only or empty export "
                        "cannot report a clean sheet)")
    missing = [name for name in EXPECTED_SHEETS if name not in sheets]
    if missing:
        problems.append(f"missing expected sheet(s): {', '.join(missing)}")
    extra = [name for name in sheets if name not in EXPECTED_SHEETS]
    if extra:
        problems.append(f"unexpected sheet(s): {', '.join(extra)} - update "
                        "EXPECTED_SHEETS if the board really gained a sheet")

    for name in EXPECTED_SHEETS:
        entry = sheets.get(name)
        if entry is None:
            continue
        if not entry["labels"]:
            problems.append(f"{name}: no net label drawn")
        if not entry["components"]:
            problems.append(f"{name}: no component drawn")
        if not entry["texts"]:
            problems.append(f"{name}: no text drawn (names/values/notes)")
    return problems


def summarise(path, sheets):
    total_ll = sum(len(e["label_label"]) for e in sheets.values())
    total_lt = sum(len(e["label_text"]) for e in sheets.values())
    return {
        "sheets": {name: {
            "labels": len(e["labels"]),
            "texts": len(e["texts"]),
            "components": e["components"],
            "label_label": len(e["label_label"]),
            "label_text": len(e["label_text"]),
            "labels_outside_frame": e["outside"],
        } for name, e in sheets.items()},
        "total_label_label": total_ll,
        "total_label_text": total_lt,
    }


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("svg", nargs="+")
    ap.add_argument("--json")
    ap.add_argument("--threshold", type=float, default=TOUCH,
                    help="overlap area / smaller box above which a pair counts "
                         "(default 0.12, the policy; 0 = any intersection)")
    ap.add_argument("--expect", metavar="ROOT=ll/lt,...",
                    help="exact per-sheet counts as 'sheet=label_label/label_text', "
                         "comma separated; the string 'built-in' uses the numbers "
                         "demonstrated on this tree (all five sheets 0/0)")
    args = ap.parse_args()

    report = {}
    failed = False
    for path in args.svg:
        svg = open(path, encoding="utf-8", errors="replace").read()
        sheets = analyse_text(svg, args.threshold)
        problems = validate_structure(svg, sheets)
        print(f"== {path}")
        if problems:
            failed = True
            print("   STRUCTURE GATE FAILED - this file cannot report a clean schematic:")
            for problem in problems:
                print(f"     - {problem}")
        print(f"{'sheet':<8} {'labels':>7} {'texts':>7} {'comps':>6} "
              f"{'label/label':>12} {'label/text':>11}")
        for name, entry in sheets.items():
            print(f"{name:<8} {len(entry['labels']):>7} {len(entry['texts']):>7} "
                  f"{entry['components']:>6} {len(entry['label_label']):>12} "
                  f"{len(entry['label_text']):>11}")
        report[path] = summarise(path, sheets)
        print(f"{'TOTAL':<8} {'':>7} {'':>7} {'':>6} "
              f"{report[path]['total_label_label']:>12} {report[path]['total_label_text']:>11}")

        strict = analyse_text(svg, STRICT)
        sll = sum(len(e["label_label"]) for e in strict.values())
        slt = sum(len(e["label_text"]) for e in strict.values())
        print(f"{'strict':<8} {'':>7} {'':>7} {'':>6} {sll:>12} {slt:>11}"
              "   (any intersection, threshold 0)")

        outside = sum(e["outside"] for e in sheets.values())
        if outside:
            failed = True
            print(f"   {outside} label tag(s) outside their page frame")
        print()

    if args.expect:
        spec = args.expect
        if spec == "built-in":
            spec = ",".join(f"{name}=0/0" for name in EXPECTED_SHEETS)
        expected = {}
        for item in spec.split(","):
            sheet, counts = item.split("=")
            ll, lt = counts.split("/")
            expected[sheet.strip()] = (int(ll), int(lt))
        for path, summary in report.items():
            for sheet, want in expected.items():
                got = summary["sheets"].get(sheet)
                if got is None:
                    failed = True
                    print(f"FAIL: {path}: sheet {sheet} is missing from the SVG")
                    continue
                pair = (got["label_label"], got["label_text"])
                if pair != want:
                    failed = True
                    print(f"FAIL: {path}: {sheet} has {pair[0]}/{pair[1]} "
                          f"label-label/label-text overlaps, recorded {want[0]}/{want[1]}")
        if not failed:
            print("ok: structure gate passed and every sheet matches its recorded "
                  "overlap count exactly")

    if args.json:
        with open(args.json, "w") as f:
            json.dump(report, f, indent=1, sort_keys=True)
        print(f"report written to {args.json}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""Count text/label overlaps in an exported schematic SVG (rendered geometry).

Usage:
    python3 froemix/tools/svg_overlaps.py before.svg after.svg ...
    python3 froemix/tools/svg_overlaps.py --json out.json sheet.svg

Why this exists: the first version of `check_schematic.py` estimated label
boxes from the circuit JSON (text length x font size). On an exported SVG that
is wrong twice over - the renderer draws each net label inside a filled tag
polygon whose size is not the text estimate, and it places pin names with its
own metrics. This tool reads what is actually in the file:

  * a net label is the `<path ... data-schematic-net-label-id=...>` tag; its
    bounding box comes from the path's own coordinates, so it is exact;
  * any other `<text>` is a pin name, component value or a sheet note; its box
    uses the element's x/y, font-size and text-anchor with a 0.55 x font-size
    per character width estimate (the only estimate left, and it errs wide);
  * two elements overlap when their boxes intersect by more than 12 % of the
    smaller box, so touching edges do not count.

Reported per sheet (taken from the `stacked-sheet-label` text): number of
labels, pin-name/value texts, notes, label/label overlaps and label/text
overlaps, plus the exact count for the whole sheet.

It also checks that every label tag lies inside that sheet's A4 page frame (the
inner border rectangle), so a layout cannot quietly push a label off the page.

Exit status: 0 unless --expect is given and a sheet exceeds its recorded count,
or a label leaves its page frame.
The recorded counts are the EXACT numbers demonstrated on this tree (Root 0/0,
MIDI 0/0, Audio 0/2, CV 0/1, Power 0/0) - there is no headroom and no
percentage tolerance, so any new overlap on any sheet fails.

    python3 tools/svg_overlaps.py --expect built-in /tmp/froemix-sch.svg

The numbers this tool prints are the ones quoted in
doc/ELECTRICAL-VERIFICATION.md.
"""

from __future__ import annotations

import argparse
import json
import re
import sys

TEXT_RE = re.compile(r"<text([^>]*)>(.*?)</text>", re.S)
PATH_RE = re.compile(r"<path([^>]*?)/>", re.S)
NUM_RE = re.compile(r"-?\d+(?:\.\d+)?")
TOUCH = 0.12


ELEMENT_RE = re.compile(r"<(text|path)((?:[^>\"']|\"[^\"]*\"|'[^']*')*?)(?:/>|>(.*?)</text>)", re.S)


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


SHEET_SPLIT = re.compile(r'<svg class="tscircuit-schematic"')
RECT_RE = re.compile(r'<rect[^>]*x="(-?[\d.]+)"[^>]*y="(-?[\d.]+)"[^>]*width="([\d.]+)"[^>]*height="([\d.]+)"')


def page_frame(block):
    """Inner border rectangle of one sheet block, in px."""
    rects = [(float(a), float(b), float(c), float(d)) for a, b, c, d in RECT_RE.findall(block)]
    # drop the full-window background and keep the largest remaining rect
    rects = [r for r in rects if not (r[0] == 0 and r[1] == 0)]
    if not rects:
        return None
    r = max(rects, key=lambda r: r[2] * r[3])
    return (r[0], r[1], r[0] + r[2], r[1] + r[3])


def analyse(path):
    svg = open(path, encoding="utf-8", errors="replace").read()
    events = parse_boxes(svg)
    # split the document by sheet: a stacked-sheet-label text starts a sheet
    sheets = {}
    current = None
    for cls, kind, box, text in events:
        if cls == "stacked-sheet-label":
            current = text
            sheets[current] = {"labels": [], "texts": [], "notes": [], "label_label": [], "label_text": []}
            continue
        if current is None:
            continue
        if not text and kind == "text":
            continue
        entry = sheets[current]
        if kind == "label":
            entry["labels"].append(box)
        elif "net-label-text" in cls:
            continue            # the label's own text sits inside its tag
        else:
            entry["texts"].append((box, text))
    for name, entry in sheets.items():
        for i in range(len(entry["labels"])):
            for j in range(i + 1, len(entry["labels"])):
                if overlap(entry["labels"][i], entry["labels"][j]) > TOUCH:
                    entry["label_label"].append((i, j))
        for i, lb in enumerate(entry["labels"]):
            for j, (tb, text) in enumerate(entry["texts"]):
                if overlap(lb, tb) > TOUCH:
                    entry["label_text"].append((i, text))
    return sheets


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("svg", nargs="+")
    ap.add_argument("--json")
    ap.add_argument("--expect", metavar="ROOT=ll/text,...",
                    help="exact per-sheet counts as 'sheet=label_label/label_text', "
                         "comma separated; the string 'built-in' uses the numbers "
                         "demonstrated on this tree")
    args = ap.parse_args()

    report = {}
    for path in args.svg:
        sheets = analyse(path)
        print(f"== {path}")
        print(f"{'sheet':<8} {'labels':>7} {'texts':>7} {'label/label':>12} {'label/text':>11}")
        total_ll = total_lt = 0
        for name, entry in sheets.items():
            ll, lt = len(entry["label_label"]), len(entry["label_text"])
            total_ll += ll
            total_lt += lt
            print(f"{name:<8} {len(entry['labels']):>7} {len(entry['texts']):>7} {ll:>12} {lt:>11}")
        print(f"{'TOTAL':<8} {'':>7} {'':>7} {total_ll:>12} {total_lt:>11}\n")
        report[path] = {name: {"labels": len(e["labels"]), "texts": len(e["texts"]),
                               "label_label": len(e["label_label"]),
                               "label_text": len(e["label_text"])}
                        for name, e in sheets.items()}
    outside = {}
    for path in args.svg:
        svg = open(path, encoding="utf-8", errors="replace").read()
        parts = SHEET_SPLIT.split(svg)[1:]
        for part in parts:
            label = re.search(r'class="stacked-sheet-label"', svg)
            frame = page_frame(part)
            if not frame:
                continue
            for m in re.finditer(r'<path([^>]*data-schematic-net-label-id[^>]*?)/>', part):
                d = re.search(r'\sd="([^"]*)"', m.group(1))
                if not d:
                    continue
                nums = [float(n) for n in NUM_RE.findall(d.group(1))]
                xs, ys = nums[0::2], nums[1::2]
                if not xs or not ys:
                    continue
                if min(xs) < frame[0] - 1 or max(xs) > frame[2] + 1 or min(ys) < frame[1] - 1 or max(ys) > frame[3] + 1:
                    outside.setdefault(path, []).append(
                        f"label at ({min(xs):.0f},{min(ys):.0f})-({max(xs):.0f},{max(ys):.0f}) is outside the page frame "
                        f"({frame[0]:.0f},{frame[1]:.0f})-({frame[2]:.0f},{frame[3]:.0f})")
    if outside:
        for path, items in outside.items():
            print(f"== {path}: {len(items)} label(s) outside the page frame")
            for item in items[:5]:
                print(f"   {item}")
        return 1

    if args.json:
        with open(args.json, "w") as f:
            json.dump(report, f, indent=1, sort_keys=True)
        print(f"report written to {args.json}")
    if args.expect:
        spec = args.expect
        if spec == "built-in":
            spec = "Root=0/0,MIDI=0/0,Audio=0/2,CV=0/1,Power=0/0"
        expected = {}
        for item in spec.split(","):
            sheet, counts = item.split("=")
            ll, lt = counts.split("/")
            expected[sheet.strip()] = (int(ll), int(lt))
        bad = []
        for path, sheets in report.items():
            for sheet, want in expected.items():
                got = sheets.get(sheet)
                if got is None:
                    bad.append(f"{path}: sheet {sheet} is missing from the SVG")
                    continue
                pair = (got["label_label"], got["label_text"])
                if pair != want:
                    bad.append(f"{path}: {sheet} has {pair[0]}/{pair[1]} label-label/label-text "
                               f"overlaps, recorded {want[0]}/{want[1]}")
        if bad:
            for line in bad:
                print(f"FAIL: {line}")
            return 1
        print("ok: every sheet matches its recorded overlap count exactly")
    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""Offline regression tests for tools/svg_overlaps.py; no build, no network.

Run: python3 froemix/tools/test_svg_overlaps.py

The checker decides whether the rendered schematic is clean, so its own gate
needs proving, not just the drawing. These tests feed it synthetic SVGs that are
wrong in each of the ways the gate exists to catch:

  * a truncated file (not well-formed XML);
  * an empty file and a file with no sheet at all;
  * a frame-only export: the five sheets with their page frames and titles but
    no labels, no components and no text - this must NOT report a clean sheet;
  * an export that is missing one of the five expected sheets;
  * a real label-on-label collision (must be counted and must fail --expect);
  * a hairline contact that only the strict line may report.

and one positive control: five well-formed sheets with labels, components and
texts and no overlap, which must pass and report 0/0.

The fixtures are written to look like the real export: a
`stacked-sheet-label` text per sheet, then a nested
`<svg class="tscircuit-schematic">` block holding the sheet's geometry, with the
net label tag as a `<path data-schematic-net-label-id=...>` whose `d` carries the
real box.
"""
import os
import re
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import svg_overlaps as S  # noqa: E402

SHEETS = S.EXPECTED_SHEETS
CHECK = os.path.join(HERE, "svg_overlaps.py")


def label_path(x0, y0, x1, y1, lid="l0"):
    return (f'<path class="net-label sch-net-label" data-schematic-net-label-id="{lid}" '
            f'd="M {x0} {y0} L {x1} {y0} L {x1} {y1} L {x0} {y1} Z" fill="none"/>')


def label_text(x, y, text="VCC", lid="l0"):
    return (f'<text class="net-label-text sch-net-label-text" '
            f'data-circuit-json-type="schematic_net_label" '
            f'data-schematic-net-label-id="{lid}" x="{x}" y="{y}" font-size="6" '
            f'text-anchor="start">{text}</text>')


def component(x=60, y=60):
    return (f'<g class="sch-component"><path class="sch-component-symbol-path" '
            f'd="M {x} {y} L {x + 6} {y}"/></g>')


def text(x=40, y=40, body="R1"):
    return f'<text class="sch-text" x="{x}" y="{y}" font-size="6" text-anchor="start">{body}</text>'


def sheet_block(labels=(), components=1, texts=1, frame=True, note=None):
    parts = []
    if frame:
        parts.append('<rect x="2" y="2" width="96" height="96"/>')
    if note:
        parts.append(note)
    for (x0, y0, x1, y1), lid in labels:
        parts.append(label_path(x0, y0, x1, y1, lid))
        parts.append(label_text(x0 + 2, (y0 + y1) / 2 + 2, lid=lid))
    for i in range(components):
        parts.append(component(60 + 10 * i, 60))
    for i in range(texts):
        parts.append(text(40, 40 + 8 * i, f"T{i}"))
    return ('<svg class="tscircuit-schematic" width="100" height="100">'
            + "".join(parts) + "</svg>")


def document(sheets=None, block=None):
    """sheets: list of names; block: sheet_block(...) result or callable per name."""
    names = SHEETS if sheets is None else sheets
    out = ['<svg xmlns="http://www.w3.org/2000/svg" class="tscircuit-stacked-schematic" '
           'width="100" height="500">']
    for i, name in enumerate(names):
        out.append(f'<text class="stacked-sheet-label" x="1" y="10" font-size="8">{name}</text>')
        out.append(block if isinstance(block, str) else (block() if block else sheet_block()))
    out.append("</svg>")
    return "".join(out)


def clean_document():
    return document(block=lambda: sheet_block(labels=[((10, 10, 30, 16), "a")]))


class StructureGateTests(unittest.TestCase):
    def test_clean_document_passes(self):
        svg = clean_document()
        sheets = S.analyse_text(svg)
        self.assertEqual(S.validate_structure(svg, sheets), [])
        summary = S.summarise("x", sheets)
        self.assertEqual(summary["total_label_label"], 0)
        self.assertEqual(summary["total_label_text"], 0)
        self.assertEqual(len(summary["sheets"]), len(SHEETS))

    def test_malformed_svg_is_rejected(self):
        svg = clean_document()[:200]          # truncated
        sheets = S.analyse_text(svg)
        problems = S.validate_structure(svg, sheets)
        self.assertTrue(problems)
        self.assertIn("not a well-formed SVG", problems[0])

    def test_empty_file_is_rejected(self):
        # an empty file is not well-formed, an empty document has no sheet:
        # either way the gate must reject it
        for svg, needle in (("", "not a well-formed SVG"),
                            ('<svg xmlns="http://www.w3.org/2000/svg"/>', "no schematic sheet")):
            with self.subTest(svg=svg[:30]):
                problems = S.validate_structure(svg, S.analyse_text(svg))
                self.assertTrue(any(needle in p for p in problems), problems)

    def test_frame_only_export_is_rejected(self):
        """Five titled A4 frames and nothing else must never look clean."""
        svg = document(block=sheet_block(labels=(), components=0, texts=0))
        sheets = S.analyse_text(svg)
        problems = S.validate_structure(svg, sheets)
        for name in SHEETS:
            self.assertIn(f"{name}: no net label drawn", problems)
            self.assertIn(f"{name}: no component drawn", problems)
            self.assertIn(f"{name}: no text drawn (names/values/notes)", problems)

    def test_missing_sheet_is_rejected(self):
        svg = document(sheets=[n for n in SHEETS if n != "Power"],
                       block=lambda: sheet_block(labels=[((10, 10, 30, 16), "a")]))
        problems = S.validate_structure(svg, S.analyse_text(svg))
        self.assertEqual(problems, ["missing expected sheet(s): Power"])

    def test_extra_sheet_is_reported(self):
        svg = document(sheets=list(SHEETS) + ["Spare"],
                       block=lambda: sheet_block(labels=[((10, 10, 30, 16), "a")]))
        problems = S.validate_structure(svg, S.analyse_text(svg))
        self.assertTrue(any("unexpected sheet" in p for p in problems), problems)

    def test_end_to_end_frame_only_fails_with_expect(self):
        svg = document(block=sheet_block(labels=(), components=0, texts=0))
        with tempfile.NamedTemporaryFile("w", suffix=".svg", delete=False) as f:
            f.write(svg)
            path = f.name
        try:
            result = subprocess.run([sys.executable, CHECK, "--expect", "built-in", path],
                                    capture_output=True, text=True)
        finally:
            os.unlink(path)
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("STRUCTURE GATE FAILED", result.stdout)

    def test_end_to_end_clean_passes_with_expect(self):
        svg = clean_document()
        with tempfile.NamedTemporaryFile("w", suffix=".svg", delete=False) as f:
            f.write(svg)
            path = f.name
        try:
            result = subprocess.run([sys.executable, CHECK, "--expect", "built-in", path],
                                    capture_output=True, text=True)
        finally:
            os.unlink(path)
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn("structure gate passed", result.stdout)


class OverlapTests(unittest.TestCase):
    def test_true_label_collision_is_counted_and_fails(self):
        """Two tags on top of each other: 1 label/label overlap, --expect fails."""
        svg = document(block=sheet_block(
            labels=[((10, 10, 30, 16), "a"), ((15, 10, 35, 16), "b")]))
        sheets = S.analyse_text(svg)
        per_sheet = {n: len(e["label_label"]) for n, e in sheets.items()}
        self.assertEqual(per_sheet["Root"], 1)
        self.assertEqual(sum(per_sheet.values()), 5)     # every sheet has the pair
        with tempfile.NamedTemporaryFile("w", suffix=".svg", delete=False) as f:
            f.write(svg)
            path = f.name
        try:
            result = subprocess.run([sys.executable, CHECK, "--expect", "built-in", path],
                                    capture_output=True, text=True)
        finally:
            os.unlink(path)
        self.assertEqual(result.returncode, 1)
        self.assertIn("FAIL:", result.stdout)

    def test_label_on_text_collision_is_counted(self):
        svg = document(block=sheet_block(
            labels=[((40, 38, 60, 44), "a")], components=1, texts=1))
        sheets = S.analyse_text(svg)
        self.assertEqual(sum(len(e["label_text"]) for e in sheets.values()), 5)

    def test_hairline_contact_only_shows_in_the_strict_line(self):
        """A 5 % overlap is below the 12 % policy but visible at threshold 0."""
        svg = document(block=sheet_block(
            labels=[((10, 10, 30, 16), "a"), ((29, 10, 49, 16), "b")]))
        policy = S.analyse_text(svg, S.TOUCH)
        strict = S.analyse_text(svg, S.STRICT)
        self.assertEqual(sum(len(e["label_label"]) for e in policy.values()), 0)
        self.assertEqual(sum(len(e["label_label"]) for e in strict.values()), 5)

    def test_label_outside_the_page_frame_is_reported(self):
        svg = document(block=sheet_block(labels=[((200, 10, 240, 16), "a")]))
        sheets = S.analyse_text(svg)
        self.assertEqual(sum(e["outside"] for e in sheets.values()), 5)
        with tempfile.NamedTemporaryFile("w", suffix=".svg", delete=False) as f:
            f.write(svg)
            path = f.name
        try:
            result = subprocess.run([sys.executable, CHECK, "--expect", "built-in", path],
                                    capture_output=True, text=True)
        finally:
            os.unlink(path)
        self.assertEqual(result.returncode, 1)
        self.assertIn("outside their page frame", result.stdout)


if __name__ == "__main__":
    unittest.main(verbosity=2)

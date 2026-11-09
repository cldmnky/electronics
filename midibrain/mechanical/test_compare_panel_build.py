#!/usr/bin/env python3
"""Tests for the before/after build comparison tool.

The tool is the guard on any panel-source change, so a false "identical" would
be worse than no tool at all. These tests build small before/after fixtures and
prove the tool reports a difference when - and only when - there is one.

    cd midibrain && python3 -m unittest discover -s mechanical -p 'test_*.py'
"""
from __future__ import annotations

import json
import sys
import unittest
from pathlib import Path
from tempfile import TemporaryDirectory

# The tool lives in mechanical/tools/, beside the other run-by-hand helpers;
# tests live at the top level so `unittest discover -s mechanical` finds them.
sys.path.insert(0, str(Path(__file__).resolve().parent / "tools"))

import compare_panel_build as cpb  # noqa: E402

GERBER_HEAD = """%TF.GenerationSoftware,tscircuit*%
%TF.CreationDate,{stamp}*%
G04 Created by tscircuit (builder) date {stamp}*
G04 Layer: F_SilkScreen*
%ADD10C,0.150000*%
D10*
X-10000Y-5000D02*
X10000Y-5000D01*
M02*
"""

DRILL = """; DRILL file {{tscircuit}} date {stamp}
; FORMAT={{-:-/ absolute / metric / decimal}}
; #@! TF.CreationDate,{stamp}
; #@! TF.GenerationSoftware,tscircuit
M48
METRIC
T10C3.200000
X-73.78Y-61.25
M30
"""


def write_gerbers(root: Path, stamp: str, silk_x: int = 10000) -> Path:
    root.mkdir(parents=True, exist_ok=True)
    (root / "F_SilkScreen.gbr").write_text(GERBER_HEAD.format(stamp=stamp)
                                          .replace("X10000", f"X{silk_x}"))
    (root / "drill_npth.drl").write_text(DRILL.format(stamp=stamp))
    return root


def panel_json(cutoffs: list[dict] | None = None, warnings: list[dict] | None = None) -> list:
    elements: list[dict] = [
        {"type": "pcb_board", "width": 162.56, "height": 128.5, "thickness": 1.6},
        {"type": "pcb_cutout", "shape": "circle", "center": {"x": -53.0, "y": -34.0}, "radius": 6.2},
        {"type": "pcb_cutout", "shape": "rect", "center": {"x": -38.0, "y": 53.5},
         "width": 12.6, "height": 7.6},
        {"type": "pcb_hole", "x": -47.0, "y": 53.5, "hole_diameter": 3.2},
    ]
    if cutoffs:
        elements.extend(cutoffs)
    elements.append({"type": "source_project_metadata",
                     "source_filesystem_md5_hash": "aaaa"})
    if warnings:
        elements.extend(warnings)
    return elements


class GerberCompareTest(unittest.TestCase):
    def test_timestamps_alone_are_not_a_difference(self) -> None:
        with TemporaryDirectory() as tmp:
            before = write_gerbers(Path(tmp) / "before", "2026-11-08T22:21:07.284Z")
            after = write_gerbers(Path(tmp) / "after", "2026-11-09T03:04:05.678Z")
            self.assertEqual(cpb.compare_gerbers(before, after), [])

    def test_a_moved_coordinate_is_reported_with_its_line(self) -> None:
        with TemporaryDirectory() as tmp:
            before = write_gerbers(Path(tmp) / "before", "2026-11-08T22:21:07.284Z")
            after = write_gerbers(Path(tmp) / "after", "2026-11-08T22:21:07.284Z",
                                  silk_x=10001)
            problems = cpb.compare_gerbers(before, after)
            self.assertEqual(len(problems), 1)
            self.assertIn("F_SilkScreen.gbr", problems[0])
            self.assertIn("differs", problems[0])

    def test_a_drill_change_is_reported(self) -> None:
        with TemporaryDirectory() as tmp:
            before = write_gerbers(Path(tmp) / "before", "2026-11-08T22:21:07.284Z")
            after = write_gerbers(Path(tmp) / "after", "2026-11-08T22:21:07.284Z")
            text = (after / "drill_npth.drl").read_text().replace("T10C3.200000", "T10C3.100000")
            (after / "drill_npth.drl").write_text(text)
            problems = cpb.compare_gerbers(before, after)
            self.assertEqual(len(problems), 1)
            self.assertIn("drill_npth.drl", problems[0])

    def test_a_missing_file_is_reported(self) -> None:
        with TemporaryDirectory() as tmp:
            before = write_gerbers(Path(tmp) / "before", "x")
            after = write_gerbers(Path(tmp) / "after", "x")
            (after / "drill_npth.drl").unlink()
            problems = cpb.compare_gerbers(before, after)
            self.assertTrue(any("file set differs" in p for p in problems))

    def test_an_empty_directory_is_reported(self) -> None:
        with TemporaryDirectory() as tmp:
            before = Path(tmp) / "before"
            after = Path(tmp) / "after"
            before.mkdir()
            after.mkdir()
            self.assertTrue(cpb.compare_gerbers(before, after))


class JsonCompareTest(unittest.TestCase):
    def test_identical_geometry_passes(self) -> None:
        with TemporaryDirectory() as tmp:
            a = Path(tmp) / "a.json"
            b = Path(tmp) / "b.json"
            data = panel_json()
            a.write_text(json.dumps(data))
            b.write_text(json.dumps(data))
            self.assertEqual(cpb.compare_json(a, b), [])

    def test_the_source_hash_is_exempt(self) -> None:
        with TemporaryDirectory() as tmp:
            a = Path(tmp) / "a.json"
            b = Path(tmp) / "b.json"
            a.write_text(json.dumps(panel_json()))
            data = panel_json()
            data[-1]["source_filesystem_md5_hash"] = "bbbb"
            b.write_text(json.dumps(data))
            self.assertEqual(cpb.compare_json(a, b), [])

    def test_warning_order_and_ids_are_exempt_but_messages_are_not(self) -> None:
        warnings = [
            {"type": "source_component_availability_warning",
             "source_component_availability_warning_id": "w1", "message": "C1"},
            {"type": "source_component_availability_warning",
             "source_component_availability_warning_id": "w2", "message": "C2"},
        ]
        with TemporaryDirectory() as tmp:
            a = Path(tmp) / "a.json"
            b = Path(tmp) / "b.json"
            a.write_text(json.dumps(panel_json(warnings=warnings)))
            reordered = [dict(warnings[1], source_component_availability_warning_id="z9"),
                         dict(warnings[0], source_component_availability_warning_id="z8")]
            b.write_text(json.dumps(panel_json(warnings=reordered)))
            self.assertEqual(cpb.compare_json(a, b), [])

            changed = [dict(warnings[0], message="C9"), warnings[1]]
            b.write_text(json.dumps(panel_json(warnings=changed)))
            problems = cpb.compare_json(a, b)
            self.assertTrue(any("warning set differs" in p for p in problems))

    def test_a_changed_radius_is_reported(self) -> None:
        with TemporaryDirectory() as tmp:
            a = Path(tmp) / "a.json"
            b = Path(tmp) / "b.json"
            a.write_text(json.dumps(panel_json()))
            data = panel_json()
            data[1]["radius"] = 6.3
            b.write_text(json.dumps(data))
            problems = cpb.compare_json(a, b)
            self.assertTrue(problems)
            self.assertIn("radius", " ".join(problems))

    def test_a_changed_diameter_is_reported(self) -> None:
        with TemporaryDirectory() as tmp:
            a = Path(tmp) / "a.json"
            b = Path(tmp) / "b.json"
            a.write_text(json.dumps(panel_json()))
            data = panel_json()
            data[3]["hole_diameter"] = 3.3
            b.write_text(json.dumps(data))
            problems = cpb.compare_json(a, b)
            self.assertIn("hole_diameter", " ".join(problems))

    def test_a_reordered_element_list_is_reported(self) -> None:
        """Element order is part of the contract: a swap renumbers every id."""
        with TemporaryDirectory() as tmp:
            a = Path(tmp) / "a.json"
            b = Path(tmp) / "b.json"
            data = panel_json()
            a.write_text(json.dumps(data))
            swapped = [data[0], data[2], data[1], *data[3:]]
            b.write_text(json.dumps(swapped))
            problems = cpb.compare_json(a, b)
            self.assertTrue(problems)
            self.assertIn("differs in", " ".join(problems))

    def test_an_added_or_removed_element_is_reported(self) -> None:
        with TemporaryDirectory() as tmp:
            a = Path(tmp) / "a.json"
            b = Path(tmp) / "b.json"
            a.write_text(json.dumps(panel_json()))
            data = panel_json(cutoffs=[{"type": "pcb_cutout", "shape": "circle",
                                        "center": {"x": 0, "y": 0}, "radius": 1.0}])
            b.write_text(json.dumps(data))
            problems = cpb.compare_json(a, b)
            self.assertTrue(any("count" in p for p in problems))


class CommandLineTest(unittest.TestCase):
    def test_a_missing_argument_is_a_usage_error(self) -> None:
        with self.assertRaises(SystemExit) as caught:
            cpb.main([])
        self.assertEqual(caught.exception.code, 2)

    def test_a_missing_path_is_a_usage_error(self) -> None:
        with TemporaryDirectory() as tmp:
            # A path that does not exist is a usage error, not a pass.
            self.assertEqual(cpb.main(["--gerbers", str(Path(tmp) / "nope"), tmp]), 2)
            self.assertEqual(cpb.main(["--json", str(Path(tmp) / "nope.json"),
                                       str(Path(tmp) / "other.json")]), 2)

    def test_identical_inputs_return_zero(self) -> None:
        with TemporaryDirectory() as tmp:
            before = write_gerbers(Path(tmp) / "before", "x")
            after = write_gerbers(Path(tmp) / "after", "y")
            a = Path(tmp) / "a.json"
            b = Path(tmp) / "b.json"
            a.write_text(json.dumps(panel_json()))
            b.write_text(json.dumps(panel_json()))
            self.assertEqual(cpb.main(["--gerbers", str(before), str(after),
                                       "--json", str(a), str(b)]), 0)

    def test_a_difference_returns_one(self) -> None:
        with TemporaryDirectory() as tmp:
            before = write_gerbers(Path(tmp) / "before", "x")
            after = write_gerbers(Path(tmp) / "after", "y", silk_x=1)
            self.assertEqual(cpb.main(["--gerbers", str(before), str(after)]), 1)


if __name__ == "__main__":
    unittest.main()

#!/usr/bin/env python3
"""The viewer manifest must place the stack where the layout says it is.

    cd midibrain && python3 -m unittest discover -s mechanical -p 'test_*.py'

Why this test exists
--------------------
`mechanical/viewer/serve_manifest.py` is the only place that converts a board's
own STEP frame into the panel frame for the browser viewer, and the viewer's own
frame is a flip of the panel frame (scene z = -panel z). Both conversions are
easy to get wrong in a way nothing else notices: a board shown at the wrong z
still loads, still rotates and still looks like a stack.

So this test runs the generator on a throw-away root and pins the placement
against `mechanical/layout.json`: the front board on the merged 10.60…12.20 mm
band, the rear board on 23.40…25.00, the panel on 0…1.60, and the display
carrier's placeholder across the derived 9.35 mm gap. The numbers are read from
the layout, never typed twice.
"""
from __future__ import annotations

import json
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
PROJECT = HERE.parent
SERVE = HERE / "viewer" / "serve_manifest.py"


def run_manifest(models: list[str]) -> dict:
    """Generate the manifest for a temporary root holding the real layout."""
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        (root / "mechanical").mkdir(parents=True)
        shutil.copy(PROJECT / "mechanical" / "layout.json",
                    root / "mechanical" / "layout.json")
        directory = root / "mechanical" / "viewer" / "models"
        directory.mkdir(parents=True)
        for stem in models:
            (directory / f"{stem}.step").write_text("ISO-10303-21;\n", encoding="utf-8")
        out = root / "manifest.json"
        result = subprocess.run([sys.executable, str(SERVE), str(root), str(out)],
                                capture_output=True, text=True, check=False)
        if result.returncode != 0:
            raise AssertionError(f"serve_manifest.py failed: {result.stderr}")
        return json.loads(out.read_text(encoding="utf-8"))


class ManifestPlacementTest(unittest.TestCase):
    """Every board lands on the panel band the layout declares."""

    @classmethod
    def setUpClass(cls) -> None:
        cls.layout = json.loads((PROJECT / "mechanical" / "layout.json").read_text())
        cls.manifest = run_manifest(["panel", "front", "rear"])

    def by_label(self) -> dict:
        return {entry["label"].split(": ")[-1].split(".step")[0]: entry
                for entry in self.manifest["models"]}

    def test_each_board_sits_on_its_own_panel_band(self) -> None:
        z = self.layout["provisionalZ"]
        thickness = self.layout["frontPcb"]["thickness"]
        half = thickness / 2.0
        expected = {
            "panel": ("panelFront", z["panelFront"]),
            "front": ("frontBoardTop", z["frontBoardTop"]),
            "rear": ("rearBoardFront", z["rearBoardFront"]),
        }
        entries = self.by_label()
        self.assertEqual(sorted(entries), ["front", "panel", "rear"])
        for stem, (key, top) in expected.items():
            entry = entries[stem]
            self.assertAlmostEqual(entry["panelTopZ"], top, places=3,
                                   msg=f"{stem} is not on {key}")
            # The model is centred on the board's mid-plane with +z out of its
            # parts side, so it carries a z flip and a half-thickness offset.
            self.assertEqual(entry["scaleZ"], -1, stem)
            self.assertAlmostEqual(entry["positionZ"], -(top + half), places=3)

    def test_the_merged_bands_are_the_ones_the_docs_state(self) -> None:
        z = self.layout["provisionalZ"]
        self.assertAlmostEqual(z["frontBoardTop"], 10.60, places=6)
        self.assertAlmostEqual(z["frontBoardRear"], 12.20, places=6)
        self.assertAlmostEqual(z["rearBoardFront"], 23.40, places=6)
        self.assertAlmostEqual(z["rearBoardRear"], 25.00, places=6)
        # In the viewer's flipped frame the panel is nearest the camera and the
        # stack recedes: 0…-1.60, -10.60…-12.20, -23.40…-25.00.
        entries = self.by_label()
        self.assertAlmostEqual(entries["panel"]["positionZ"] + 0.8, 0.0, places=3)
        self.assertAlmostEqual(entries["front"]["panelTopZ"], 10.60, places=3)
        self.assertAlmostEqual(entries["rear"]["panelTopZ"], 23.40, places=3)

    def test_the_carrier_placeholder_spans_the_derived_gap(self) -> None:
        carrier = self.manifest["carrier"]
        gap = carrier["rearBoardFront"] - carrier["moduleRear"]
        self.assertAlmostEqual(gap, 9.35, places=6)
        self.assertAlmostEqual(gap, self.layout["displayStandoff"]["spacerLength"], places=6)
        self.assertAlmostEqual(carrier["moduleRear"], self.layout["provisionalZ"]["displayRearMost"],
                               places=6)
        self.assertEqual(len(carrier["bosses"]), 4)

    def test_the_manifest_names_the_boards_it_places(self) -> None:
        # A stale stem (a renamed export) would silently drop a board from the
        # stack instead of showing it somewhere wrong.
        self.assertEqual(sorted(entry["label"] for entry in self.manifest["models"]),
                         ["viewer: front.step", "viewer: panel.step", "viewer: rear.step"])


if __name__ == "__main__":
    unittest.main()

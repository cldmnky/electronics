#!/usr/bin/env python3
"""Real emitted-Gerber check: panel silkscreen ink vs every panel opening.

A label anchor is not ink. This test exports the panel's Legend Gerber, parses
the strokes the exporter actually wrote, and measures the 2D clearance from every
stroke edge to every physical opening and drill - the LCD polygon, the three
rectangular feedthrough cutouts, all circular openings and the 14 drills.

    cd midibrain && python3 -m unittest discover -s mechanical -p 'test_*.py'

The real-export test is REQUIRED: if ``bunx``, ``tscircuit/node_modules`` or the
fresh output is missing it FAILS with a message that says how to fix it. It never
skips, because a skipped clearance check is a missing regression.

To run only the pure parser and geometry tests (no bun, no export):

    python3 -m unittest mechanical.test_panel_silk_gerber.GerberParserTest \
        mechanical.test_panel_silk_gerber.ClearanceGeometryTest \
        mechanical.test_panel_silk_gerber.ZipSafetyTest

Regression history: with the PLAY START label at y -42 the ink overlapped the
POWER BUTTON opening by 0.862 mm (measured on the emitted file, not estimated).
The mutation test below re-creates that geometry from the real export and must
fail again if it ever returns.
"""
from __future__ import annotations

import json
import math
import os
import shutil
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import unittest

import gerber_silk as gs
import step_geometry as sg

ROOT = Path(__file__).resolve().parent.parent
TSCIRCUIT = ROOT / "tscircuit"
PANEL_JSON = TSCIRCUIT / "dist" / "panel" / "circuit.json"
MIN_CLEARANCE_MM = 0.3
# The PLAY START label row before the fix, and its current row.
OLD_LABEL_Y = -42.0
LABEL_SHIFT_MM = -16.5          # -25.5 -> -42
OLD_OVERLAP_MM = -0.862         # measured on the old emitted Gerber
BORDER_RECT_MM = (130.08, 126.5)

# The Makefile passes MIDIBRAIN_BUNX so `make test BUN=/abs/path/bun` works even
# when bun is not on PATH.
BUNX = (os.environ.get("MIDIBRAIN_BUNX")
        or shutil.which("bunx") or shutil.which("bun"))


def require_export_tools() -> str:
    """Return the bunx path, or FAIL: a skipped clearance check is a missing test."""
    usable = bool(BUNX) and (Path(BUNX).is_file() or shutil.which(BUNX) is not None)
    if not usable:
        raise AssertionError(
            "panel silk export needs bunx on PATH; install bun or add it to PATH. "
            "This check must not be skipped - it is the only stroke-level silk regression. "
            "For the pure parser/geometry tests only, run "
            "`python3 -m unittest mechanical.test_panel_silk_gerber.GerberParserTest "
            "mechanical.test_panel_silk_gerber.ClearanceGeometryTest "
            "mechanical.test_panel_silk_gerber.ZipSafetyTest`")
    if not (TSCIRCUIT / "node_modules").is_dir():
        raise AssertionError(
            "tscircuit/node_modules is missing; run `cd tscircuit && bun install`. "
            "This check must not be skipped - it is the only stroke-level silk regression")
    return BUNX


def safe_extract(zip_path: Path, dest: Path) -> list[str]:
    """Extract ONLY plain relative file members. Refuse anything else.

    Rejected: absolute paths, Windows drive prefixes, backslashes, any ``..`` or
    empty path segment, and directory (non-file) entries. A Gerber archive from a
    tool is not a trusted input.
    """
    names: list[str] = []
    with zipfile.ZipFile(zip_path) as zf:
        for member in zf.infolist():
            name = member.filename
            if member.is_dir():
                raise gs.GerberError(f"archive member {name!r} is a directory entry")
            if name.startswith("/") or name.startswith("\\") or "\\" in name:
                raise gs.GerberError(f"archive member {name!r} is an absolute/backslash path")
            if len(name) >= 2 and name[1] == ":":
                raise gs.GerberError(f"archive member {name!r} has a drive prefix")
            parts = name.split("/")
            if any(part in ("", ".", "..") for part in parts):
                raise gs.GerberError(f"archive member {name!r} has an unsafe path segment")
            names.append(name)
        zf.extractall(dest, members=names)
    return names


def export_panel_silk(out_dir: Path) -> Path:
    """Export the panel Gerbers into out_dir and return the silk file path."""
    bunx = require_export_tools()
    out_dir.mkdir(parents=True, exist_ok=True)
    zip_path = out_dir / "panel.zip"
    result = subprocess.run(
        [bunx, "tsci", "export", "panel.circuit.tsx", "-f", "gerbers", "-o", str(zip_path)],
        cwd=TSCIRCUIT, capture_output=True, text=True, timeout=600)
    if result.returncode != 0 or not zip_path.is_file():
        raise RuntimeError(f"gerber export failed (exit {result.returncode}): "
                           f"{result.stderr[-800:]}")
    safe_extract(zip_path, out_dir / "gerbers")
    silk = out_dir / "gerbers" / "F_SilkScreen.gbr"
    if not silk.is_file() or silk.stat().st_size == 0:
        raise RuntimeError(f"the fresh export has no usable {silk}")
    return silk


def export_panel_circuit_json(out_dir: Path) -> Path:
    """Export the panel circuit JSON FRESH from the current sources."""
    bunx = require_export_tools()
    out_dir.mkdir(parents=True, exist_ok=True)
    path = out_dir / "panel-circuit.json"
    result = subprocess.run(
        [bunx, "tsci", "export", "panel.circuit.tsx", "-f", "circuit-json", "-o", str(path)],
        cwd=TSCIRCUIT, capture_output=True, text=True, timeout=600)
    if result.returncode != 0 or not path.is_file() or path.stat().st_size == 0:
        raise RuntimeError(f"circuit-json export failed (exit {result.returncode}): "
                           f"{result.stderr[-800:]}")
    return path


MINIMAL = """%FSLAX46Y46*%
%MOMM*%
%ADD10C,0.300000*%
D10*
X0Y0D02*
X1000000Y0D01*
M02*
"""


class GerberParserTest(unittest.TestCase):
    """The parser accepts the exported subset and FAILS on anything else."""

    def _parse(self, text: str) -> list[gs.Stroke]:
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "silk.gbr"
            path.write_text(text)
            return gs.parse_gerber_silk(path)

    def test_a_minimal_file_yields_one_stroke_with_its_aperture_width(self) -> None:
        strokes = self._parse(MINIMAL)
        self.assertEqual(len(strokes), 1)
        self.assertAlmostEqual(strokes[0].width, 0.3, places=6)
        self.assertAlmostEqual(strokes[0].x1, 0.0, places=6)
        self.assertAlmostEqual(strokes[0].x2, 1.0, places=6)

    def test_a_bare_coordinate_repeats_the_previous_operation(self) -> None:
        """RS-274X modal: D01 then a bare block DRAWS again, it does not move."""
        text = MINIMAL.replace("X1000000Y0D01*",
                               "X1000000Y0D01*\nX2000000Y0*")
        strokes = self._parse(text)
        self.assertEqual(len(strokes), 2, "the repeated D01 must not be dropped")
        self.assertAlmostEqual(strokes[1].x1, 1.0, places=6)
        self.assertAlmostEqual(strokes[1].x2, 2.0, places=6)
        self.assertEqual(strokes[1].kind, "segment")

    def test_a_bare_coordinate_after_a_move_still_moves(self) -> None:
        text = MINIMAL.replace("X0Y0D02*", "X0Y0D02*\nX5000000Y5000000*")
        self._parse(text)  # must not raise, must not draw
        self.assertEqual(len(self._parse(text)), 1)

    def test_the_first_coordinate_block_needs_an_operation(self) -> None:
        with self.assertRaises(gs.GerberError):
            self._parse(MINIMAL.replace("X0Y0D02*", "X0Y0*"))

    def test_a_missing_coordinate_is_rejected_not_guessed_as_zero(self) -> None:
        # X without Y, Y without X, and a move with only one axis: all rejected
        for broken in ("X1000000D01*", "Y1000000D01*", "X1000000D02*"):
            with self.assertRaises(gs.GerberError, msg=broken):
                self._parse(MINIMAL.replace("X1000000Y0D01*", broken))
        with self.assertRaises(gs.GerberError):
            self._parse(MINIMAL.replace("X0Y0D02*", "X0D02*"))

    def test_a_flash_becomes_a_dot(self) -> None:
        strokes = self._parse(MINIMAL.replace("X1000000Y0D01*", "X500000Y500000D03*"))
        self.assertEqual(len(strokes), 1)
        self.assertEqual(strokes[0].kind, "flash")
        self.assertAlmostEqual(strokes[0].x1, strokes[0].x2, places=6)

    def test_an_arc_raises_instead_of_being_measured_wrongly(self) -> None:
        """No arc centre, no honest measurement: refuse, never guess."""
        for command in ("G03X2000000Y0I1000000J0D01*", "G02X2000000Y0D01*", "G03*"):
            with self.assertRaises(gs.GerberError, msg=command):
                self._parse(MINIMAL.replace("X1000000Y0D01*", command))

    def test_empty_file_raises(self) -> None:
        with self.assertRaises(gs.GerberError):
            self._parse("")

    def test_missing_format_statement_raises(self) -> None:
        with self.assertRaises(gs.GerberError):
            self._parse(MINIMAL.replace("%FSLAX46Y46*%\n", ""))

    def test_missing_end_of_file_raises(self) -> None:
        with self.assertRaises(gs.GerberError):
            self._parse(MINIMAL.replace("M02*\n", ""))

    def test_an_unknown_command_raises(self) -> None:
        for extra in ("G36*", "G37*", "%AB*%", "X1Y1D04*", "G74*", "P1000X0Y0*"):
            with self.assertRaises(gs.GerberError, msg=extra):
                self._parse(MINIMAL.replace("X1000000Y0D01*", f"{extra}\nX1000000Y0D01*"))

    def test_clear_polarity_raises(self) -> None:
        with self.assertRaises(gs.GerberError):
            self._parse(MINIMAL.replace("%ADD10C", "%LPC*%\n%ADD10C"))

    def test_a_macro_aperture_used_for_drawing_raises(self) -> None:
        text = MINIMAL.replace("%ADD10C,0.300000*%",
                               "%AMMAC*%\n0 test*\n1,1,0.3,0,0*%\n%ADD10MAC,1*%")
        with self.assertRaises(gs.GerberError):
            self._parse(text)

    def test_an_undefined_aperture_raises(self) -> None:
        with self.assertRaises(gs.GerberError):
            self._parse(MINIMAL.replace("D10*\n", "D11*\n"))

    def test_a_coordinate_before_the_format_raises(self) -> None:
        with self.assertRaises(gs.GerberError):
            self._parse(MINIMAL.replace("%FSLAX46Y46*%\n", "", 1).replace(
                "X1000000Y0D01*", "X1Y0D01*"))

    def test_millimetres_are_required(self) -> None:
        with self.assertRaises(gs.GerberError):
            self._parse(MINIMAL.replace("%MOMM*%", "%MOIN*%"))

    def test_attributes_and_comments_carry_no_ink(self) -> None:
        text = MINIMAL.replace("D10*", "G04 comment*\n%TF.FileFunction,Legend,Top*%\n%TD*%\nD10*")
        self.assertEqual(len(self._parse(text)), 1)


class ZipSafetyTest(unittest.TestCase):
    """The export archive is untrusted input: only plain relative files."""

    def _zip(self, tmp: str, name: str, *, directory: bool = False) -> Path:
        path = Path(tmp) / "evil.zip"
        with zipfile.ZipFile(path, "w") as zf:
            if directory:
                zf.writestr(name, "")
            else:
                zf.writestr(name, "x")
        return path

    def _good_zip(self, tmp: str) -> Path:
        path = Path(tmp) / "good.zip"
        with zipfile.ZipFile(path, "w") as zf:
            zf.writestr("F_SilkScreen.gbr", "x")
            zf.writestr("bom.csv", "y")
        return path

    def test_a_plain_archive_extracts_with_its_member_list(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            names = safe_extract(self._good_zip(tmp), Path(tmp) / "out")
            self.assertEqual(sorted(names), ["F_SilkScreen.gbr", "bom.csv"])
            self.assertTrue((Path(tmp) / "out" / "F_SilkScreen.gbr").is_file())

    def test_absolute_and_backslash_paths_are_refused(self) -> None:
        for name in ("/etc/passwd", r"\\server\share\x.gbr", r"dir\x.gbr"):
            with tempfile.TemporaryDirectory() as tmp:
                with self.assertRaises(gs.GerberError, msg=name):
                    safe_extract(self._zip(tmp, name), Path(tmp) / "out")

    def test_a_drive_prefix_is_refused(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaises(gs.GerberError):
                safe_extract(self._zip(tmp, "C:/windows/x.gbr"), Path(tmp) / "out")

    def test_backtracking_is_refused(self) -> None:
        for name in ("../x.gbr", "a/../../x.gbr", "a//b.gbr", "./x.gbr"):
            with tempfile.TemporaryDirectory() as tmp:
                with self.assertRaises(gs.GerberError, msg=name):
                    safe_extract(self._zip(tmp, name), Path(tmp) / "out")

    def test_a_directory_entry_is_refused(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaises(gs.GerberError):
                safe_extract(self._zip(tmp, "gerbers/", directory=True), Path(tmp) / "out")


class ClearanceGeometryTest(unittest.TestCase):
    """Geometry used by the clearance, including tangent and zero cases."""

    def test_point_to_segment(self) -> None:
        self.assertAlmostEqual(gs.point_segment_distance(0.0, 1.0, -5.0, 0.0, 5.0, 0.0), 1.0)
        self.assertAlmostEqual(gs.point_segment_distance(6.0, 0.0, -5.0, 0.0, 5.0, 0.0), 1.0)
        # a degenerate segment is a point: distance is the point distance
        self.assertAlmostEqual(gs.point_segment_distance(2.0, 1.0, 1.0, 1.0, 1.0, 1.0), 1.0)

    def test_segment_to_segment(self) -> None:
        self.assertEqual(gs.segment_segment_distance((0, 0), (1, 0), (0.5, -1), (0.5, 1)), 0.0)
        self.assertAlmostEqual(
            gs.segment_segment_distance((0, 0), (1, 0), (0, 2), (1, 2)), 2.0)
        # tangent: end to end
        self.assertAlmostEqual(
            gs.segment_segment_distance((0, 0), (1, 0), (1, 0), (2, 0)), 0.0)

    def test_segment_to_rect_and_polygon(self) -> None:
        self.assertAlmostEqual(gs.segment_rect_distance((0, 0), (0, 0), 1, 1, 3, 3), math.sqrt(2))
        self.assertEqual(gs.segment_rect_distance((2, 2), (2, 2), 1, 1, 3, 3), 0.0)
        self.assertEqual(gs.segment_rect_distance((-1, 2), (5, 2), 1, 1, 3, 3), 0.0)
        square = [(0, 0), (4, 0), (4, 4), (0, 4)]
        self.assertEqual(gs.segment_polygon_distance((-1, 2), (5, 2), square), 0.0)
        self.assertAlmostEqual(gs.segment_polygon_distance((2, 6), (2, 7), square), 2.0)

    def test_clearance_subtracts_the_stroke_radius(self) -> None:
        stroke = gs.Stroke(0.0, 0.5, 1.0, 0.5, 0.3, "segment")
        circle = gs.Opening("c", "circle", x=0.0, y=0.0, radius=0.2)
        # centreline 0.5 from the centre, minus 0.2 radius, minus 0.15 stroke
        self.assertAlmostEqual(gs.stroke_clearance(stroke, circle), 0.15, places=6)

    def test_tangent_stroke_touches_at_zero(self) -> None:
        stroke = gs.Stroke(-1.0, 0.35, 1.0, 0.35, 0.3, "segment")
        circle = gs.Opening("c", "circle", x=0.0, y=0.0, radius=0.2)
        self.assertAlmostEqual(gs.stroke_clearance(stroke, circle), 0.0, places=9)

    def test_violations_report_only_below_the_limit(self) -> None:
        near = gs.Stroke(0.0, 0.21, 1.0, 0.21, 0.3, "segment")
        far = gs.Stroke(0.0, 5.0, 1.0, 5.0, 0.3, "segment")
        circle = gs.Opening("c", "circle", x=0.0, y=0.0, radius=0.2)
        self.assertEqual(len(gs.violations([far], [circle], 0.3)), 0)
        bad = gs.violations([near], [circle], 0.3)
        self.assertEqual(len(bad), 1)
        self.assertAlmostEqual(bad[0].clearance, -0.14, places=6)

    def test_openings_come_from_the_canonical_data(self) -> None:
        layout = sg.load_layout()
        openings = gs.openings_from_layout(layout)
        self.assertEqual(len(openings), len(layout["panelApertures"]) + len(layout["panelDrills"]))
        self.assertEqual(sum(1 for o in openings if o.kind == "polygon"), 1)
        self.assertEqual(sum(1 for o in openings if o.kind == "rect"), 3)
        self.assertEqual(sum(1 for o in openings if o.kind == "circle"), 30)


class EmittedPanelSilkTest(unittest.TestCase):
    """The real exported Legend Gerber of THIS tree."""

    @classmethod
    def setUpClass(cls) -> None:
        cls._tmp = tempfile.TemporaryDirectory()
        cls.silk = export_panel_silk(Path(cls._tmp.name))
        cls.strokes = gs.parse_gerber_silk(cls.silk)
        cls.layout = sg.load_layout()
        cls.openings = gs.openings_from_layout(cls.layout)

    @classmethod
    def tearDownClass(cls) -> None:
        cls._tmp.cleanup()

    def test_every_glyph_stroke_is_parsed(self) -> None:
        drawn = self.silk.read_text(errors="replace").count("D01*")
        self.assertEqual(len(self.strokes), drawn,
                         "every D01 in the file must become a measured stroke")
        self.assertGreater(len(self.strokes), 500)

    def test_every_stroke_lies_inside_the_panel(self) -> None:
        half_w = self.layout["panel"]["width"] / 2
        half_h = self.layout["panel"]["height"] / 2
        for s in self.strokes:
            self.assertLessEqual(max(abs(s.x1), abs(s.x2)) + s.radius, half_w + 0.01)
            self.assertLessEqual(max(abs(s.y1), abs(s.y2)) + s.radius, half_h + 0.01)

    def test_the_minimum_clearance_is_at_least_0_3_mm(self) -> None:
        worst = gs.minimum_clearance(self.strokes, self.openings)
        self.assertGreaterEqual(worst.clearance, MIN_CLEARANCE_MM,
                                f"worst is {worst.clearance:.3f} mm at stroke "
                                f"{worst.stroke_index} vs {worst.opening}")
        self.assertEqual(gs.violations(self.strokes, self.openings, MIN_CLEARANCE_MM), [])

    def test_the_old_border_strokes_are_gone(self) -> None:
        # the removed border was a rectangle at x +-65.04 and y +-63.25
        half_w, half_h = BORDER_RECT_MM[0] / 2, BORDER_RECT_MM[1] / 2
        border = [s for s in self.strokes
                  if (abs(abs(s.x1) - half_w) < 0.05 and abs(abs(s.x2) - half_w) < 0.05)
                  or (abs(abs(s.y1) - half_h) < 0.05 and abs(abs(s.y2) - half_h) < 0.05)]
        self.assertEqual(border, [], "the decorative square border came back")

    def test_the_fresh_circuit_json_has_no_printed_border(self) -> None:
        """Board OUTLINE may exist; the decorative printed BORDER must not.

        Exported fresh from the current sources, so a stale dist cannot pass it.
        """
        fresh = export_panel_circuit_json(Path(self._tmp.name) / "circuit")
        elements = json.loads(fresh.read_text())
        rects = [e for e in elements if e.get("type") == "pcb_silkscreen_rect"]
        self.assertEqual(rects, [], "a printed silkscreen border was reintroduced")
        board = [e for e in elements if e.get("type") == "pcb_board"]
        self.assertEqual(len(board), 1, "the board outline itself must still exist")
        self.assertAlmostEqual(board[0]["width"], self.layout["panel"]["width"], places=3)
        self.assertAlmostEqual(board[0]["height"], self.layout["panel"]["height"], places=3)
        # and the moved label is where the docs say it is
        labels = [e for e in elements
                  if e.get("type") == "pcb_silkscreen_text" and e.get("text") == "PLAY START"]
        self.assertEqual(len(labels), 1)
        self.assertAlmostEqual(labels[0]["anchor_position"]["y"], -25.5, places=6)

    def test_the_led_cutouts_are_emitted_in_both_layers(self) -> None:
        """A FACT about our export: circular cutouts appear as drills AND routes.

        This is not a capability claim. It is why the JLC viewer and support
        must confirm CAM de-duplication before ordering (docs/mechanical-stack.md).
        """
        import re
        gerbers = self.silk.parent
        edges = (gerbers / "Edge_Cuts.gbr").read_text(errors="replace")
        drill = (gerbers / "drill_npth.drl").read_text(errors="replace")
        radius = 1.3  # nominal Ø2.6
        # Numeric, so the exporter's leading-zero style cannot make this fragile.
        arcs = [(int(a), int(b), int(c))
                for a, b, c in re.findall(r"X(-?\d+)Y(-?\d+)I(-?\d+)J-?\d+D01\*", edges)]
        scaled = [(x / 1e6, y / 1e6, i / 1e6) for x, y, i in arcs]
        for x in (-41.0, -17.0, 31.0):
            endpoints = {(round(px, 3), round(py, 3))
                         for px, py, i in scaled
                         if abs(abs(i) - radius) < 1e-6 and abs(py + 34.0) < 1e-6
                         and abs(px - (x - radius)) < 1e-6}
            self.assertTrue(endpoints, f"no routed arc of radius {radius} at x {x}")
            self.assertIn(f"X{x:.4f}Y-34.0000", drill,
                          "the LED circle is drilled in the NPTH file")
        self.assertIn("C2.600000", drill, "the NPTH tool is the nominal 2.6 mm")

    def test_the_play_start_label_no_longer_crosses_the_power_button(self) -> None:
        # every stroke in the label band must clear the POWER BUTTON opening
        band = [s for s in self.strokes
                if 44.0 <= (s.x1 + s.x2) / 2 <= 60.0
                and -27.5 <= (s.y1 + s.y2) / 2 <= -23.5]
        self.assertGreater(len(band), 20, "the PLAY START label band was not found")
        power = next(o for o in self.openings if o.name == "POWER BUTTON")
        worst = min(gs.stroke_clearance(s, power) for s in band)
        self.assertGreaterEqual(worst, MIN_CLEARANCE_MM)

    def test_mutation_the_old_label_row_fails(self) -> None:
        """The exact regression: shift the label strokes back to y -42."""
        band = [s for s in self.strokes
                if 44.0 <= (s.x1 + s.x2) / 2 <= 60.0
                and -27.5 <= (s.y1 + s.y2) / 2 <= -23.5]
        moved = [gs.Stroke(s.x1, s.y1 + LABEL_SHIFT_MM, s.x2, s.y2 + LABEL_SHIFT_MM,
                           s.width, s.kind) for s in band]
        power = next(o for o in self.openings if o.name == "POWER BUTTON")
        worst = min(gs.stroke_clearance(s, power) for s in moved)
        self.assertLess(worst, 0.0, "the old label row must overlap the POWER BUTTON")
        self.assertAlmostEqual(worst, OLD_OVERLAP_MM, delta=0.05)
        self.assertTrue(gs.violations(moved, [power], MIN_CLEARANCE_MM))


class NegativeControlTest(unittest.TestCase):
    """A clean file passes and an overlapping one fails: the check has teeth."""

    def _parse(self, text: str) -> list[gs.Stroke]:
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "silk.gbr"
            path.write_text(text)
            return gs.parse_gerber_silk(path)

    def test_a_well_spaced_label_passes(self) -> None:
        # a label 20 mm above the opening
        strokes = self._parse(MINIMAL.replace("X0Y0D02*", "X0Y20000000D02*")
                              .replace("X1000000Y0D01*", "X1000000Y20000000D01*"))
        circle = gs.Opening("c", "circle", x=0.5, y=0.0, radius=6.2)
        self.assertGreaterEqual(gs.stroke_clearance(strokes[0], circle), MIN_CLEARANCE_MM)

    def test_a_stroke_over_the_opening_fails(self) -> None:
        # a label INSIDE the opening: the same geometry the old row produced
        strokes = self._parse(MINIMAL.replace("X0Y0D02*", "X0Y-48000000D02*")
                              .replace("X1000000Y0D01*", "X1000000Y-48000000D01*"))
        circle = gs.Opening("c", "circle", x=0.5, y=-48.0, radius=6.2)
        self.assertLess(gs.stroke_clearance(strokes[0], circle), 0.0)


if __name__ == "__main__":
    unittest.main()

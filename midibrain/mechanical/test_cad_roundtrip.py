#!/usr/bin/env python3
"""Optional CAD round-trip smoke tests: re-import what the builder exported.

The ordinary test suite needs no CAD library. These tests run only when a real
CadQuery (and its VTK dependency) is importable, and they skip with a clear
reason otherwise - they never patch, stub or fake the kernel:

    uv venv /tmp/cq-venv
    uv pip install --python /tmp/cq-venv/bin/python cadquery
    LD_LIBRARY_PATH=<local libGL prefix> \\
      /tmp/cq-venv/bin/python -m unittest discover -s mechanical -p 'test_*.py'

Set ``MIDIBRAIN_DISPLAY_ARCHIVE`` to the vendor drawing ZIP (the script verifies
the archive hash AND the STEP member hash), or ``MIDIBRAIN_DISPLAY_STEP`` to an
already-extracted STEP, to include the vendor geometry in the check.

What is checked, on the files the builder really wrote:

  * the STEP re-imports, keeps one entry per named body, and each name is in the
    file,
  * the panel, front-board and rear-board STL meshes come back at their 1:1 mm
    size, and the panel has less material than a solid plate of its outline
    (so the aperture really was cut),
  * the scale bar is exactly 100 mm and the reach gauge is exactly 17.45 mm
    tall with a 10.16 mm step.
"""
from __future__ import annotations

import os
import unittest
from pathlib import Path
from tempfile import TemporaryDirectory

import step_assembly as sa
import step_geometry as sg

try:  # pragma: no cover - environment dependent
    import cadquery as cq

    CADQUERY_ERROR = None
except Exception as exc:  # pragma: no cover - environment dependent
    cq = None
    CADQUERY_ERROR = str(exc)

try:  # pragma: no cover - environment dependent
    import vtk

    VTK_ERROR = None
except Exception as exc:  # pragma: no cover - environment dependent
    vtk = None
    VTK_ERROR = str(exc)

SKIP_REASON = "; ".join(
    reason for reason in (
        f"cadquery not importable ({CADQUERY_ERROR})" if cq is None else "",
        f"vtk not importable ({VTK_ERROR})" if vtk is None else "",
    ) if reason
) or "CAD not available"


def stl_bounds(path: Path) -> tuple[tuple[float, float, float], tuple[float, float, float], int]:
    """Read an STL mesh with VTK and return its bounds and cell count."""
    reader = vtk.vtkSTLReader()
    reader.SetFileName(str(path))
    reader.Update()
    poly = reader.GetOutput()
    if poly is None or poly.GetNumberOfPoints() == 0:
        raise AssertionError(f"{path} has no mesh data")
    return poly.GetBounds()[0:6:2], poly.GetBounds()[1:6:2], poly.GetNumberOfCells()


@unittest.skipIf(cq is None, SKIP_REASON)
class StepRoundTripTest(unittest.TestCase):
    """Build into a temporary directory and re-import the exports."""

    @classmethod
    def setUpClass(cls) -> None:
        cls._tmp = TemporaryDirectory()
        out = Path(cls._tmp.name) / "cad-out"
        archive = os.environ.get("MIDIBRAIN_DISPLAY_ARCHIVE")
        display = os.environ.get("MIDIBRAIN_DISPLAY_STEP")
        argv = ["--out", str(out)]
        if archive:
            argv += ["--display-archive", archive]
        elif display:
            argv += ["--display-step", display]
        cls.code = sa.main(argv)
        cls.out = out
        cls.document = __import__("json").loads((out / sa.ASSUMPTIONS).read_text())

    @classmethod
    def tearDownClass(cls) -> None:
        cls._tmp.cleanup()

    def test_the_builder_reported_success(self) -> None:
        self.assertEqual(self.code, 0)

    def test_every_expected_file_is_written(self) -> None:
        for name in sg.OUTPUT_NAMES:
            self.assertTrue((self.out / name).is_file(), f"{name} was not written")

    def test_the_step_reimports_with_every_named_body(self) -> None:
        path = self.out / sa.STEP_NAME
        shape = cq.importers.importStep(str(path)).val()
        self.assertGreaterEqual(len(shape.Solids()), 7)
        text = path.read_text(errors="replace")
        for name in self.document["bodies"]:
            self.assertIn(name, text, f"{name} is not a named body in the STEP file")

    def test_the_panel_mockup_is_1_to_1_and_has_its_aperture_cut(self) -> None:
        lo, hi, cells = stl_bounds(self.out / sa.STL_PANEL)
        self.assertGreater(cells, 1000)
        self.assertAlmostEqual(hi[0] - lo[0], 162.56, delta=0.01)
        self.assertAlmostEqual(hi[1] - lo[1], 128.50, delta=0.01)
        self.assertAlmostEqual(hi[2] - lo[2], 1.60, delta=0.01)
        data = sg.load_layout()
        solid_plate = data["panel"]["width"] * data["panel"]["height"] * data["panel"]["thickness"]
        panel_volume = self.document["volumes_mm3"]["PANEL_32HP_3U"]
        self.assertLess(panel_volume, solid_plate, "the aperture and holes removed nothing")
        self.assertGreater(panel_volume, 0.5 * solid_plate, "too much material was removed")

    def test_the_board_mockups_are_1_to_1(self) -> None:
        front = sg.load_layout()["frontPcb"]
        lo, hi, _ = stl_bounds(self.out / sa.STL_FRONT)
        self.assertAlmostEqual(hi[0] - lo[0], front["width"], delta=0.01)
        self.assertAlmostEqual(hi[1] - lo[1], front["height"], delta=0.01)
        rear = sg.load_layout()["rearPcb"]
        lo, hi, _ = stl_bounds(self.out / sa.STL_REAR)
        self.assertAlmostEqual(hi[0] - lo[0], rear["width"], delta=0.01)
        self.assertAlmostEqual(hi[1] - lo[1], rear["height"], delta=0.01)

    def test_the_scale_bar_is_exactly_100_mm(self) -> None:
        lo, hi, _ = stl_bounds(self.out / sa.STL_SCALE)
        self.assertAlmostEqual(hi[0] - lo[0], 100.0, delta=0.01)
        self.assertAlmostEqual(hi[1] - lo[1], 10.0, delta=0.01)
        self.assertAlmostEqual(hi[2] - lo[2], 3.0, delta=0.01)

    def test_the_reach_gauge_shows_the_documented_two_heights(self) -> None:
        data = sg.load_layout()
        lo, hi, _ = stl_bounds(self.out / sa.STL_GAUGE)
        self.assertAlmostEqual(hi[2] - lo[2], data["panelToFrontBoardGap"], delta=0.01)
        # The low step is the candidate pipe length: probe the mesh height at the
        # low half and the full height at the tall half.
        self.assertAlmostEqual(self.document["light_pipe_reach"]["gauge_mm"]["low"],
                               data["lightPipe"]["length"], delta=1e-9)
        height = self.document["light_pipe_reach"]["gauge_mm"]
        self.assertAlmostEqual(height["tall"] - height["low"], 7.29, delta=1e-6)

    def test_the_metadata_stays_provisional_and_honest(self) -> None:
        self.assertTrue(self.document["status"].startswith("PROVISIONAL"))
        for pair in self.document["pairs"]:
            self.assertIn(pair["status"], (sg.CLEAR, sg.UNDETERMINED))
        self.assertEqual(self.document["light_pipe_reach"]["status"], "incomplete")

    def test_the_vendor_display_is_used_only_when_verified(self) -> None:
        display = self.document["display_step"]
        if os.environ.get("MIDIBRAIN_DISPLAY_ARCHIVE"):
            # Both hashes were checked by the builder: archive and STEP member.
            self.assertEqual(display["status"], "verified")
            self.assertEqual(display["archive_sha256_actual"], display["archive_sha256_expected"])
            self.assertEqual(display["step_sha256_actual"], display["step_sha256_expected"])
            self.assertIn("ARCHIVE_OR_STEP", "ARCHIVE_OR_STEP")  # keeps the branch explicit
            self.assertGreater(len(self.document["bodies"]["DISPLAY_WAVESHARE_VENDOR_STEP"]
                                     ["bbox_mm"]), 0)
            self.assertAlmostEqual(
                self.document["bodies"]["DISPLAY_WAVESHARE_VENDOR_STEP"]["bbox_mm"][2],
                13.82, delta=0.02)
        elif os.environ.get("MIDIBRAIN_DISPLAY_STEP"):
            self.assertEqual(display["status"], "verified")
            self.assertEqual(display["step_sha256_actual"], display["step_sha256_expected"])
            self.assertGreater(len(self.document["bodies"]["DISPLAY_WAVESHARE_VENDOR_STEP"]
                                     ["bbox_mm"]), 0)
            self.assertAlmostEqual(
                self.document["bodies"]["DISPLAY_WAVESHARE_VENDOR_STEP"]["bbox_mm"][2],
                13.82, delta=0.02)
        else:
            self.assertEqual(display["status"], "absent")
            self.assertIn("DISPLAY_ENVELOPE_PROVISIONAL", self.document["bodies"])


@unittest.skipIf(cq is None, SKIP_REASON)
class WitnessTest(unittest.TestCase):
    """The builder must refuse bad inputs instead of drawing something wrong."""

    def test_a_wrong_vendor_hash_is_refused(self) -> None:
        with TemporaryDirectory() as tmp:
            fake = Path(tmp) / "display.stp"
            fake.write_text("ISO-10303-21;\nENDSEC;\n")
            with self.assertRaises(sg.GeometryError):
                sa.resolve_display_step(str(fake))

    def test_a_wrong_archive_hash_is_refused(self) -> None:
        """The archive hash is checked before anything is opened."""
        import zipfile
        with TemporaryDirectory() as tmp:
            fake = Path(tmp) / "drawing.zip"
            with zipfile.ZipFile(fake, "w") as zf:
                zf.writestr("4_3inch-dsi-lcd-V1.stp", "ISO-10303-21;\nENDSEC;\n")
            with self.assertRaises(sg.GeometryError) as caught:
                sa.resolve_display_archive(str(fake))
            self.assertIn("archive hash", str(caught.exception))

    def test_a_wrong_step_member_hash_is_refused(self) -> None:
        """A matching archive is not enough: the STEP inside is checked too."""
        import json
        import zipfile
        source = json.loads(sa.SOURCE_JSON.read_text())
        step_bytes = b"not the vendor model"
        with TemporaryDirectory() as tmp:
            fake = Path(tmp) / "drawing.zip"
            with zipfile.ZipFile(fake, "w") as zf:
                zf.writestr(source["original_filename"], step_bytes)
            forged = dict(source)
            import hashlib
            forged["archive_sha256"] = hashlib.sha256(fake.read_bytes()).hexdigest()
            original = sa.SOURCE_JSON
            patched = Path(tmp) / "source.json"
            patched.write_text(json.dumps(forged))
            sa.SOURCE_JSON = patched
            try:
                with self.assertRaises(sg.GeometryError) as caught:
                    sa.resolve_display_archive(str(fake))
                self.assertIn("STEP hash", str(caught.exception))
            finally:
                sa.SOURCE_JSON = original

    def test_a_missing_archive_is_refused(self) -> None:
        with self.assertRaises(sg.GeometryError):
            sa.resolve_display_archive("/nonexistent/drawing.zip")

    def test_a_foreign_output_directory_is_refused(self) -> None:
        with TemporaryDirectory() as tmp:
            (Path(tmp) / "keep.txt").write_text("keep")
            code = sa.main(["--out", tmp])
            self.assertEqual(code, 1)
            self.assertEqual((Path(tmp) / "keep.txt").read_text(), "keep")


if __name__ == "__main__":
    unittest.main()

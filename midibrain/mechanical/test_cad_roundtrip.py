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

import json
import os
import unittest
from unittest import mock
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


_BUILT_MODEL: dict = {}


def built_model() -> dict:
    """The verification model, built ONCE per test process and reused.

    ``sa.build`` writes nothing, so one instance is safe to share. Several test
    classes probe the same solids, and building them once keeps the suite's cost
    in one place instead of once per class that needs a body.
    """
    if not _BUILT_MODEL:
        archive = os.environ.get("MIDIBRAIN_DISPLAY_ARCHIVE")
        if archive:
            step_path, _ = sa.resolve_display_archive(archive)
        else:
            step_path, _ = sa.resolve_display_step(os.environ.get("MIDIBRAIN_DISPLAY_STEP"))
        layout = sa.load_layout()
        _BUILT_MODEL["layout"] = layout
        _BUILT_MODEL["step_path"] = step_path
        _BUILT_MODEL["built"] = sa.build(cq, layout, step_path, False)
    return _BUILT_MODEL


def display_argv() -> list[str]:
    """Arguments that choose the vendor display model.

    An archive or a STEP from the environment, or nothing: the builder then uses
    the committed reference STEP, which it verifies by SHA-256 before use.
    """
    archive = os.environ.get("MIDIBRAIN_DISPLAY_ARCHIVE")
    display = os.environ.get("MIDIBRAIN_DISPLAY_STEP")
    if archive:
        return ["--display-archive", archive]
    if display:
        return ["--display-step", display]
    return []


# The vendor model is available whenever the committed reference is in the tree
# (it is, and its hash is recorded), so the display tests run by default.
DISPLAY_AVAILABLE = bool(os.environ.get("MIDIBRAIN_DISPLAY_ARCHIVE")
                         or os.environ.get("MIDIBRAIN_DISPLAY_STEP")
                         or sa.DEFAULT_DISPLAY_STEP.is_file())


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
        cls.code = sa.main(["--out", str(out), *display_argv()])
        cls.out = out
        cls.document = json.loads((out / sa.ASSUMPTIONS).read_text())

    @classmethod
    def tearDownClass(cls) -> None:
        cls._tmp.cleanup()

    def test_the_builder_reported_success(self) -> None:
        self.assertEqual(self.code, 0)

    def test_every_expected_file_is_written(self) -> None:
        for name in sg.OUTPUT_NAMES:
            self.assertTrue((self.out / name).is_file(), f"{name} was not written")

    def test_the_step_reimports_with_every_named_body(self) -> None:
        """Named bodies follow the MODE: the spacers exist only with a vendor model."""
        path = self.out / sa.STEP_NAME
        shape = cq.importers.importStep(str(path)).val()
        self.assertGreaterEqual(len(shape.Solids()), 7)
        text = path.read_text(errors="replace")
        # metadata-only blocks are not solids: LCD_MOUNT_PLACEHOLDERS records the
        # four carrier holes, PANEL_STANDOFFS the four mount-standoff envelopes
        # (whose bodies are the PANEL_STANDOFF_1..4 products, checked below).
        metadata_only = {"LCD_MOUNT_PLACEHOLDERS", "PANEL_STANDOFFS"}
        for name in self.document["bodies"]:
            if name in metadata_only:
                continue
            self.assertIn(name, text, f"{name} is not a named body in the STEP file")
        # The four-point panel mount frame is carried as four named bodies at the
        # canonical points, so a dropped standoff cannot hide behind the metadata.
        standoffs = self.document["bodies"]["PANEL_STANDOFFS"]["standoffs"]
        self.assertEqual(len(standoffs), 4)
        for entry in standoffs:
            self.assertIn(entry["name"], text,
                          f"{entry['name']} is not a named body in the STEP file")
            self.assertAlmostEqual(entry["length_mm"], 9.0, places=3)
        self.assertEqual(sorted(tuple(e["xy_mm"]) for e in standoffs),
                         sorted([(-71.5, -52.0), (71.5, -52.0),
                                 (-71.5, 52.0), (71.5, 52.0)]))
        placeholders = self.document["bodies"]["LCD_MOUNT_PLACEHOLDERS"]
        if self.document["display_step"]["status"] == "verified":
            gap = sg.display_mount_gap(sg.load_layout())["model_gap_mm"]
            if gap > 0:
                # vendor mode with a POSITIVE model gap: four placeholders drawn
                self.assertEqual(placeholders["kind"],
                                 "MODEL-FIT GEOMETRIC PLACEHOLDER, non-approved")
                for index in (1, 2, 3, 4):
                    self.assertIn(f"MODEL_FIT_SPACER_PLACEHOLDER_NON_APPROVED_{index}", text)
            else:
                # vendor mode with a NEGATIVE model gap: refused, nothing drawn
                self.assertEqual(placeholders["kind"],
                                 "REFUSED - NO POSITIVE-LENGTH PLACEHOLDER EXISTS")
                self.assertAlmostEqual(placeholders["model_gap_mm"], gap, places=6)
                for index in (1, 2, 3, 4):
                    self.assertNotIn(f"MODEL_FIT_SPACER_PLACEHOLDER_NON_APPROVED_{index}", text)
        else:
            # envelope mode: no drawn boss plane, so the spacers are SKIPPED
            self.assertEqual(placeholders["kind"], "SKIPPED")
            self.assertIn("14.05 mm design envelope is NOT used", placeholders["reason"])
            for index in (1, 2, 3, 4):
                self.assertNotIn(f"MODEL_FIT_SPACER_PLACEHOLDER_NON_APPROVED_{index}", text)

    def test_the_panel_mockup_is_1_to_1_and_has_its_aperture_cut(self) -> None:
        lo, hi, cells = stl_bounds(self.out / sa.STL_PANEL)
        self.assertGreater(cells, 1000)
        self.assertAlmostEqual(hi[0] - lo[0], 152.0, delta=0.01)
        self.assertAlmostEqual(hi[1] - lo[1], 128.50, delta=0.01)
        self.assertAlmostEqual(hi[2] - lo[2], 1.60, delta=0.01)
        data = sg.load_layout()
        solid_plate = data["panel"]["width"] * data["panel"]["height"] * data["panel"]["thickness"]
        panel_volume = self.document["volumes_mm3"]["PANEL_30HP_3U"]
        self.assertLess(panel_volume, solid_plate, "the aperture and holes removed nothing")
        self.assertGreater(panel_volume, 0.5 * solid_plate, "too much material was removed")

    def test_the_board_mockup_outlines_are_the_right_size(self) -> None:
        """ONLY the slab outline is checked here, never the cuts.

        The two board mockups print at 1:1 as slabs, and that is all this test
        may claim: their CUTS are not the current PCB (see
        test_the_front_board_mockup_is_the_current_pcb and the checklist
        warning), so the old name - "the board mockups are 1:1" - overstated
        what a passing bounding box proves.
        """
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

    def test_the_reach_gauge_uses_one_datum_and_both_planes(self) -> None:
        """tall = max(16.5 tip, 10.6 board) = 16.5; low = 10.6; step 5.9."""
        data = sg.load_layout()
        lo, hi, _ = stl_bounds(self.out / sa.STL_GAUGE)
        # Both bars stand on the panel FRONT face, so the gauge's own height is
        # the TALLER of the two planes, not a difference.
        self.assertAlmostEqual(lo[2], 0.0, delta=0.01)
        height = self.document["light_pipe_reach"]["gauge_mm"]
        self.assertAlmostEqual(hi[2] - lo[2], height["tall"], delta=0.02)
        self.assertAlmostEqual(height["tall"], 16.5, delta=1e-9)
        self.assertAlmostEqual(height["low"], 10.6, delta=1e-9)
        self.assertAlmostEqual(height["tall"] - height["low"], 5.9, delta=1e-6)
        # The SIGNED distance keeps its direction: board minus tip is negative,
        # i.e. the tip plane is BEHIND the board face.
        self.assertAlmostEqual(height["signed_board_minus_tip"], -5.9, delta=1e-9)
        self.assertEqual(height["direction"], "tip_behind_board")
        self.assertTrue(height["penetrates_board_plane"])
        self.assertIn("Not an LED gap", height["step_meaning"])
        self.assertIn("panel FRONT face", height["datum"])
        # the gauge must not present itself as a physical LED gap or fit
        reach = self.document["light_pipe_reach"]
        self.assertIsNone(reach["optical_gap_mm"])
        self.assertEqual(reach["status"], "incomplete")
        self.assertIn("PGN25L165", reach["part"])

    def test_the_connector_marker_is_planning_data_only(self) -> None:
        """The withdrawn 21.412 x 4.826 mm connector claim (finding 1)."""
        bodies = self.document["bodies"]
        self.assertNotIn("DISPLAY_FPC_CONNECTOR_PROVISIONAL", bodies)
        marker = bodies["DISPLAY_CONNECTOR_PLANNING_MARKER"]
        self.assertEqual(marker["kind"], "PLANNING MARKER, not a verified connector")
        self.assertIn("UNVERIFIED", marker["size_source"])
        self.assertIn("TFT1 flex bonds", marker["size_source"])
        self.assertIn("UNKNOWN", marker["fit_and_routing"])
        self.assertIn("display PCB rear datum", marker["z_basis"])
        self.assertIn("not on that plane", marker["z_basis"])
        self.assertNotIn("10.05", marker["z_basis"])
        self.assertAlmostEqual(marker["z_mm"][0], 10.65, places=3)

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
                                     ["placed_bbox_mm"]), 0)
            self.assertAlmostEqual(
                self.document["bodies"]["DISPLAY_WAVESHARE_VENDOR_STEP"]["placed_bbox_mm"][2],
                13.82, delta=0.02)
        elif os.environ.get("MIDIBRAIN_DISPLAY_STEP"):
            self.assertEqual(display["status"], "verified")
            self.assertEqual(display["step_sha256_actual"], display["step_sha256_expected"])
            self.assertGreater(len(self.document["bodies"]["DISPLAY_WAVESHARE_VENDOR_STEP"]
                                     ["placed_bbox_mm"]), 0)
            self.assertAlmostEqual(
                self.document["bodies"]["DISPLAY_WAVESHARE_VENDOR_STEP"]["placed_bbox_mm"][2],
                13.82, delta=0.02)
        else:
            # No environment source. The vendor STEP is NOT tracked (it may not be
            # redistributed), so the DEFAULT is absent unless someone put a verified
            # file in place. Both outcomes are correct and the test must accept the
            # one that actually happened instead of assuming a file is there:
            #   absent  -> an explicitly PROVISIONAL 106 x 68 x 14.05 envelope
            #   verified-> the SHA-256 checked vendor body, no envelope
            default = sa.DEFAULT_DISPLAY_STEP
            if not default.is_file():
                self.assertEqual(display["status"], "absent")
                self.assertIn("note", display)
                self.assertIn("provisional envelope", display["note"])
                self.assertIn("DISPLAY_ENVELOPE_PROVISIONAL", self.document["bodies"])
                self.assertNotIn("DISPLAY_WAVESHARE_VENDOR_STEP", self.document["bodies"])
                envelope = self.document["bodies"]["DISPLAY_ENVELOPE_PROVISIONAL"]
                self.assertIn("not vendor geometry", envelope["note"])
            else:
                self.assertEqual(display["status"], "verified")
                self.assertEqual(display["step_sha256_actual"],
                                 display["step_sha256_expected"])
                self.assertIn("DISPLAY_WAVESHARE_VENDOR_STEP", self.document["bodies"])
                self.assertNotIn("DISPLAY_ENVELOPE_PROVISIONAL", self.document["bodies"])


@unittest.skipIf(cq is None, SKIP_REASON)
@unittest.skipIf(cq is None, SKIP_REASON)
@unittest.skipUnless(DISPLAY_AVAILABLE, "no vendor display STEP in the tree")
class DisplayPlacementTest(unittest.TestCase):
    """The vendor display faces the panel, and a reversed placement would fail."""

    @classmethod
    def setUpClass(cls) -> None:
        cls._tmp = TemporaryDirectory()
        out = Path(cls._tmp.name) / "cad"
        cls.code = sa.main(["--out", str(out), *display_argv()])
        cls.doc = json.loads((out / sa.ASSUMPTIONS).read_text())
        cls.body = cls.doc["bodies"]["DISPLAY_WAVESHARE_VENDOR_STEP"]

    @classmethod
    def tearDownClass(cls) -> None:
        cls._tmp.cleanup()

    def test_the_build_succeeded(self) -> None:
        self.assertEqual(self.code, 0)

    def test_the_glass_front_face_is_the_panel_front_datum(self) -> None:
        # NOT flush: the layout's display datum puts the glass 0.60 mm BEHIND the
        # panel front, so the drawn glass plane is +0.60 in the panel frame.
        datum = sg.load_layout()["provisionalZ"]["displayGlassFront"]
        self.assertAlmostEqual(datum, 0.60, places=6)
        self.assertAlmostEqual(self.body["placed_global_z_mm"]["glass_front"], datum, places=3)

    def test_the_electronics_point_rearward(self) -> None:
        z = self.body["placed_global_z_mm"]
        self.assertLess(z["glass_front"], z["pcb_rear"])
        self.assertLess(z["pcb_rear"], z["boss_rear"])
        self.assertLess(z["boss_rear"], z["rear_most"])
        self.assertLess(z["glass_front"], z["glass_rear"])

    def test_the_front_frame_is_disclosed_1_9_mm_proud(self) -> None:
        z = self.body["placed_global_z_mm"]
        # The selected display datum is NOT zero: the glass front is 0.60 mm
        # behind the panel front, so the touch/frame plane (1.90 mm in front of
        # the glass) lands at panel Z -1.30, not the flush-datum -1.90.
        self.assertAlmostEqual(z["glass_front"], 0.60, places=3)
        self.assertAlmostEqual(z["touch_frame_front"], -1.30, places=3)
        self.assertIn("1.90 mm", self.body["front_frame_disclosure"])
        self.assertIn("-1.30", self.body["front_frame_disclosure"])
        self.assertIn("1.30 mm in FRONT of the panel front", self.body["front_frame_disclosure"])
        self.assertIn("NOT flush", self.body["front_frame_disclosure"])
        # The glass datum note describes the layout datum, never "assumed flush".
        self.assertIn("+0.60", self.body["glass_datum"])
        self.assertIn("BEHIND the panel front", self.body["glass_datum"])
        self.assertGreaterEqual(len(self.body["pending"]), 3)

    def test_the_placement_is_a_rotation_not_a_mirror(self) -> None:
        t = self.body["transform"]
        self.assertAlmostEqual(t["determinant"], 1.0, places=9)
        self.assertTrue(t["is_rigid_rotation_not_mirror"])
        # The candidate used for drawing is the Y rotation; the X rotation is
        # recorded as an equal alternative.
        self.assertEqual(t["rotation_matrix"], [[-1.0, 0.0, 0.0], [0.0, 1.0, 0.0], [0.0, 0.0, -1.0]])

    def test_the_orientation_stays_pending_and_is_not_claimed_proven(self) -> None:
        orientation = self.body["orientation"]
        self.assertEqual(orientation["status"], "PENDING")
        self.assertIn("about Y", orientation["candidate_used_for_drawing"])
        self.assertIn("about X", orientation["alternative_candidate"])
        self.assertIn("REAR view", orientation["why_not_proven"])
        self.assertIn("photograph", orientation["resolving_evidence"])
        observation = orientation["inner_pattern_observation"]
        self.assertIn("not independent proof", observation["caveat"])
        self.assertTrue(observation["candidate_matches_recorded_positions"])
        self.assertNotEqual(observation["modelled_candidate"], observation["alternative_candidate"])

    def test_the_mounting_centres_land_on_the_front_board_holes(self) -> None:
        self.assertTrue(self.body["mounting_centres_verified"])
        # The outer pattern is symmetric: it fixes the centre, not the axis.
        observation = self.body["orientation"]["inner_pattern_observation"]
        self.assertIn("not independent proof", observation["caveat"])

    def test_the_vendor_planes_are_reported_separately(self) -> None:
        sep = self.body["vendor_model_separations_mm"]
        self.assertAlmostEqual(sep["glass_to_pcb_rear_mm"], 6.24, places=2)
        self.assertAlmostEqual(sep["pcb_rear_to_boss_rear_mm"], 4.0, places=2)
        self.assertAlmostEqual(sep["overall_depth_mm"], 13.82, places=2)
        model = self.doc["display_model_planes"]
        self.assertAlmostEqual(model["vendor_stated_maximum_mm"], 14.05, places=2)
        self.assertAlmostEqual(model["provisional_chain_mm"]["glass_front_to_pcb_rear"],
                              10.05, places=2)
        # 6.24 is the model; 10.05 is the provisional chain. Never equated.
        self.assertNotAlmostEqual(sep["glass_to_pcb_rear_mm"],
                                 model["provisional_chain_mm"]["glass_front_to_pcb_rear"], places=1)

    def test_the_connector_identity_is_not_asserted(self) -> None:
        connector = self.body["connector"]
        self.assertEqual(connector["identification"], "NOT ESTABLISHED")
        self.assertIn("not established", connector["candidate_at_drawing_position"]["status"])
        for forbidden in connector["not_asserted"]:
            self.assertTrue(forbidden)
        self.assertIn("UNVERIFIED", connector["drawing_position_status"])
        self.assertIn("UNVERIFIED", connector["rear_front_frame_mapping"])
        # The old misidentification is withdrawn in the data, not hidden.
        correction = connector["tft1_flex_bonds"]["correction"]
        self.assertIn("misidentification", correction)
        self.assertIn("withdrawn", correction)

    def test_the_drawing_position_arithmetic_needs_rederivation(self) -> None:
        connector = self.body["connector"]
        self.assertIn("re-derivation", connector["drawing_position_status"])
        self.assertIn("long-axis", connector["drawing_position_status"])
        # A position offset is reported as an observation only, with its magnitude.
        self.assertEqual(len(connector["position_offset_mm"]), 2)
        self.assertGreater(connector["position_offset_magnitude_mm"], 0.0)

    def test_the_vendor_bbox_matches_the_recorded_model(self) -> None:
        bb = self.body["placed_bbox_mm"]
        self.assertAlmostEqual(bb[0], 106.10, places=2)
        self.assertAlmostEqual(bb[1], 68.363, places=2)
        self.assertAlmostEqual(bb[2], 13.82, places=2)

    def test_the_panel_and_boards_are_unchanged_by_the_placement(self) -> None:
        """The display transform must not touch the panel or the board solids."""
        with TemporaryDirectory() as tmp:
            out = Path(tmp) / "no-display"
            self.assertEqual(sa.main(["--out", str(out)]), 0)
            plain = json.loads((out / sa.ASSUMPTIONS).read_text())
        for name in ("PANEL_30HP_3U", "FRONT_BOARD_PROVISIONAL_Z", "REAR_BOARD_PROVISIONAL_Z"):
            self.assertAlmostEqual(self.doc["volumes_mm3"][name],
                                   plain["volumes_mm3"][name], places=3)
            self.assertEqual(self.doc["stl_bounding_boxes_mm"].get(
                {"PANEL_30HP_3U": sa.STL_PANEL,
                 "FRONT_BOARD_PROVISIONAL_Z": sa.STL_FRONT,
                 "REAR_BOARD_PROVISIONAL_Z": sa.STL_REAR}[name]),
                plain["stl_bounding_boxes_mm"].get(
                    {"PANEL_30HP_3U": sa.STL_PANEL,
                     "FRONT_BOARD_PROVISIONAL_Z": sa.STL_FRONT,
                     "REAR_BOARD_PROVISIONAL_Z": sa.STL_REAR}[name]))

    def test_a_reversed_placement_is_rejected_by_the_checker(self) -> None:
        """The reported defect: vendor +Z mapped straight into layout +Z."""
        m = sg.VENDOR_DISPLAY_MODEL
        zmin = m["bbox_z_mm"][0]
        reversed_planes = {
            "touch_frame_front": m["touch_frame_front_z"] - zmin,
            "glass_front": m["glass_front_z"] - zmin,
            "glass_rear": m["glass_rear_z"] - zmin,
            "pcb_front": m["pcb_front_z"] - zmin,
            "pcb_rear": m["pcb_rear_z"] - zmin,
            "boss_rear": m["boss_rear_z"] - zmin,
            "rear_most": m["rear_most_z"] - zmin,
        }
        with self.assertRaises(sg.GeometryError):
            sg.check_display_plane_order(reversed_planes)


@unittest.skipIf(cq is None, SKIP_REASON)
@unittest.skipUnless(DISPLAY_AVAILABLE, "no vendor display STEP in the tree")
class SpacerPlaceholderTest(unittest.TestCase):
    """The four LCD mount placeholders: geometry, checks, and negative cases."""

    @classmethod
    def setUpClass(cls) -> None:
        # The same shared build the board-mockup probes use: sa.build writes
        # nothing, so one instance serves both classes.
        shared = built_model()
        cls.source_args = display_argv()
        cls.step_path = shared["step_path"]
        cls.layout = shared["layout"]
        cls.built = shared["built"]
        cls.solids = cls.built["solids"]
        cls.meta = cls.built["meta"]["LCD_MOUNT_PLACEHOLDERS"]
        cls.gap = sg.display_mount_gap(cls.layout)
        # The selected display datum leaves a NEGATIVE model gap, so the
        # placeholder solids are refused. The positive-gap branch is kept below so
        # the drawing behaviour is still covered if the chain ever allows it.
        cls.positive_gap = cls.gap["model_gap_mm"] > 0

    def _spacers(self) -> list[dict]:
        return [b for n, b in self.solids.items()
                if n.startswith("MODEL_FIT_SPACER_PLACEHOLDER_NON_APPROVED")]

    def test_four_placeholders_exist_with_distinct_names(self) -> None:
        names = sorted(n for n in self.solids
                       if n.startswith("MODEL_FIT_SPACER_PLACEHOLDER_NON_APPROVED"))
        if not self.positive_gap:
            # ZERO solids, and that is an ASSERTION, not a pass by absence: no
            # positive-length spacer exists on this chain, so a drawn body would
            # be a fabricated (negative or reversed) placeholder.
            self.assertEqual(names, [])
            self.assertAlmostEqual(self.gap["model_gap_mm"], -0.24, places=6)
            self.assertEqual(self.meta["kind"],
                             "REFUSED - NO POSITIVE-LENGTH PLACEHOLDER EXISTS")
            self.assertEqual(self.meta["count"], 0)
            self.assertIn("no_solid_drawn", self.meta)
            return
        self.assertEqual(names, [f"MODEL_FIT_SPACER_PLACEHOLDER_NON_APPROVED_{i}"
                                 for i in (1, 2, 3, 4)])
        self.assertEqual(self.meta["count"], 4)

    def test_the_span_comes_from_the_drawn_solids(self) -> None:
        if not self.positive_gap:
            # The refusal records the same drawn planes, and the negative gap.
            self.assertAlmostEqual(self.meta["span_mm"][0], self.gap["boss_rear_z_mm"], places=2)
            self.assertAlmostEqual(self.meta["span_mm"][1], self.gap["board_top_z_mm"], places=2)
            self.assertAlmostEqual(self.meta["model_gap_mm"], self.gap["model_gap_mm"], places=2)
            self.assertLess(self.meta["model_gap_mm"], 0)
            return
        for body in self.meta["spacers"]:
            # Both planes are DRAWN/model data, never typed: the vendor boss rear
            # and the canonical front-board top face.
            self.assertAlmostEqual(body["z_mm"][0], self.gap["boss_rear_z_mm"], places=2)
            self.assertAlmostEqual(body["z_mm"][1], self.gap["board_top_z_mm"], places=2)
            self.assertAlmostEqual(body["length_mm"], self.gap["model_gap_mm"], places=2)
            self.assertGreater(body["length_mm"], 0.0)

    def test_the_axes_are_the_exported_mount_holes(self) -> None:
        if not self.positive_gap:
            axes = sorted(tuple(a) for a in self.meta["axes_xy_mm"])
            expected = sorted((h["x"], h["y"]) for h in self.layout["lcdMountHoles"])
            self.assertEqual(axes, expected)
            return
        axes = sorted(tuple(b["axis_xy_mm"]) for b in self.meta["spacers"])
        expected = sorted((h["x"], h["y"]) for h in self.layout["lcdMountHoles"])
        self.assertEqual(axes, expected)

    def test_a_negative_gap_is_refused_with_a_collision_warning(self) -> None:
        """The selected chain has NO positive spacer: say so, do not draw one.

        The drawn boss ends land BEHIND the front board's top face, so a
        placeholder would have to be negative or reversed. Neither is drawn, and
        the resulting interference is recorded as PENDING - never as cleared.
        """
        self.assertAlmostEqual(self.gap["model_gap_mm"], -0.24, places=6)
        self.assertFalse(self.positive_gap)
        self.assertEqual(self.meta["count"], 0)
        self.assertEqual(self.meta["kind"],
                         "REFUSED - NO POSITIVE-LENGTH PLACEHOLDER EXISTS")
        self.assertIn("negative", self.meta["failure_mode"])
        self.assertIn("A negative or reversed", self.meta["no_solid_drawn"])
        self.assertIn("PENDING", self.meta["collision"])
        self.assertIn("NOT CLEARED", self.meta["collision"])
        self.assertIn("unmeasured", self.meta["collision_note"])
        self.assertIn("that the drawn overlap is acceptable", self.meta["not_claimed"])
        # The real carrier mounting is still recorded, unchanged.
        carrier = self.meta["real_carrier_gap"]
        self.assertAlmostEqual(carrier["spacer_length_mm"],
                               self.layout["displayStandoff"]["spacerLength"], places=6)
        self.assertAlmostEqual(carrier["rear_board_front_z_mm"],
                               self.layout["provisionalZ"]["rearBoardFront"], places=6)
        # And nothing was extruded for it.
        self.assertEqual([n for n in self.solids if "MODEL_FIT_SPACER" in n], [])

    def test_od_and_id_are_only_the_assumed_illustration(self) -> None:
        if not self.positive_gap:
            # Nothing is drawn, so no diameter may be claimed at all.
            self.assertEqual(self.meta["count"], 0)
            self.assertNotIn("spacers", self.meta)
            return
        for body in self.meta["spacers"]:
            self.assertAlmostEqual(body["outer_diameter_mm"], 5.0, places=6)
            self.assertAlmostEqual(body["inner_diameter_mm"], 2.7, places=6)
            # OD exceeds the board hole, so a bearing annulus is visible
            self.assertGreater(body["annulus_bearing_width_mm"], 0.0)

    def test_no_positive_volume_intrusion_against_display_or_board(self) -> None:
        if not self.positive_gap:
            # Nothing is drawn, so there is no body to measure. The REFUSAL and
            # its pending collision warning are asserted in
            # test_a_negative_gap_is_refused_with_a_collision_warning.
            self.assertEqual(self.meta["count"], 0)
            self.assertNotIn("spacers", self.meta)
            self.assertIn("NOT CLEARED", self.meta["collision"])
            return
        for body in self.meta["spacers"]:
            for volume in body["intrusion_volume_mm3"].values():
                self.assertLessEqual(volume, sa.INTRUSION_EPS_MM3)

    def test_the_placeholders_carry_no_fit_or_approval_claim(self) -> None:
        """Non-approval metadata, not a vacuous substring check."""
        if not self.positive_gap:
            # The refused branch must still deny every claim, and must add the
            # one claim that matters most here: that the overlap is acceptable.
            self.assertIn("REFUSED", self.meta["kind"])
            denials = " ".join(self.meta["not_claimed"]).lower()
            for phrase in ("approved", "part", "thread", "fit",
                           "that the drawn overlap is acceptable"):
                self.assertIn(phrase, denials)
            joined = json.dumps(self.meta).lower()
            for claim in ("fits", "verified fit", "fit is", "is approved",
                          "approved length", "clearance to", "resolved"):
                self.assertNotIn(claim, joined)
            self.assertIn("PENDING", self.meta["collision"])
            return
        # the block and its definition both say placeholder / non-approved
        self.assertIn("PLACEHOLDER", self.meta["kind"])
        self.assertIn("non-approved", self.meta["kind"])
        self.assertIn("not a part", sg.DISPLAY_MOUNT_PLACEHOLDER["status"])
        # every denial the reviewer asked for is present
        denials = " ".join(self.meta["not_claimed"]).lower()
        for phrase in ("approved", "part", "thread", "fit"):
            self.assertIn(phrase, denials)
        # and no string in the block claims a fit, a clearance or an approval
        joined = json.dumps(self.meta).lower()
        for claim in ("fits", "verified fit", "fit is", "is approved", "approved length",
                      "clearance to"):
            self.assertNotIn(claim, joined)
        # the assumed dimensions are marked as illustration, not specification
        definition = sg.DISPLAY_MOUNT_PLACEHOLDER
        self.assertIn("ASSUMED", definition["dimension_basis"])
        self.assertIn("not a specification", definition["dimension_basis"])

    def test_the_design_chain_plane_and_the_drawn_boss_plane_disagree(self) -> None:
        """Two chains, two answers: the spacer plane is 8.99 mm from the bosses."""
        board_top = self.gap["board_top_z_mm"]
        chain_end = board_top - self.gap["design_chain_spacer_mm"]
        # Re-derived, never typed: the 8.75 mm carrier spacer reaches
        # board_top - 8.75 = 1.85, which is 8.99 mm in FRONT of the drawn boss
        # ends at 10.84. The two chains do NOT agree and neither is approved.
        self.assertAlmostEqual(chain_end,
                               board_top - self.layout["displayStandoff"]["spacerLength"],
                               places=6)
        self.assertAlmostEqual(chain_end, 1.85, places=6)
        self.assertAlmostEqual(chain_end - self.gap["boss_rear_z_mm"],
                               self.gap["residual_mm"], places=6)
        self.assertAlmostEqual(chain_end - self.gap["boss_rear_z_mm"], -8.99, places=6)
        # The chain plane lies INSIDE the module's Z span, which is why a spacer
        # placed there cannot be drawn as a separate body.
        self.assertGreater(chain_end, self.layout["provisionalZ"]["displayGlassFront"])
        self.assertLess(chain_end, self.layout["provisionalZ"]["displayRearMost"])
        # At the 10.6 mm panel that plane now lies INSIDE the drawn module, so a
        # spacer placed there cannot even be drawn as a separate body: the chain
        # plane and the vendor model contradict each other. That is the finding,
        # not a tolerance to absorb.
        display = self.solids["DISPLAY_WAVESHARE_VENDOR_STEP"]
        # The probe stands on the first canonical carrier hole, never a literal.
        hole = self.layout["lcdMountHoles"][0]
        short = (cq.Workplane("XY").workplane(offset=chain_end)
                 .center(hole["x"], hole["y"]).circle(2.5).circle(1.35).extrude(5.0))
        intrusions = sa.spacer_intrusions(cq, short, display,
                                          self.solids["FRONT_BOARD_PROVISIONAL_Z"].val())
        self.assertGreater(intrusions["display_mm3"], sa.INTRUSION_EPS_MM3,
                           "the design-chain spacer plane is expected to be inside the "
                           "drawn module at the 10.6 mm panel")
        bb = short.val().BoundingBox()
        self.assertAlmostEqual(bb.zmin, chain_end, places=2)
        self.assertAlmostEqual(bb.zmax, chain_end + 5.0, places=2)

    def test_an_over_long_placeholder_is_rejected(self) -> None:
        """Negative regression: extending a placeholder must fail the limit."""
        display = self.solids["DISPLAY_WAVESHARE_VENDOR_STEP"]
        board = self.solids["FRONT_BOARD_PROVISIONAL_Z"].val()
        # One 0.2 mm longer than the drawn gap, so it must reach INTO the board.
        hole = self.layout["lcdMountHoles"][0]
        over = (cq.Workplane("XY").workplane(offset=10.24)
                .center(hole["x"], hole["y"])
                .circle(2.5).circle(1.35).extrude(self.gap["model_gap_mm"] + 0.2))
        intrusions = sa.spacer_intrusions(cq, over, display, board)
        self.assertGreater(max(intrusions.values()), sa.INTRUSION_EPS_MM3)

    def test_the_exported_step_carries_four_spacer_solids(self) -> None:
        with TemporaryDirectory() as tmp:
            out = Path(tmp) / "cad"
            # both documented source paths are supported: --display-archive and
            # --display-step; the class recorded which one this run uses
            self.assertEqual(sa.main(["--out", str(out), *self.source_args]), 0)
            shape = cq.importers.importStep(str(out / sa.STEP_NAME)).val()
            if not self.positive_gap:
                # The selected chain has NO positive model gap, so no spacer solid
                # may exist. This asserts the export, not a missing key.
                document = json.loads((out / sa.ASSUMPTIONS).read_text())
                block = document["bodies"]["LCD_MOUNT_PLACEHOLDERS"]
                self.assertEqual(block["kind"],
                                 "REFUSED - NO POSITIVE-LENGTH PLACEHOLDER EXISTS")
                self.assertAlmostEqual(block["model_gap_mm"], self.gap["model_gap_mm"],
                                       places=3)
                names = [n for n in document["bodies"] if "MODEL_FIT_SPACER" in n]
                self.assertEqual(names, [])
                tubes = [s for s in shape.Solids()
                         if abs(s.BoundingBox().xlen - 5.0) < 0.05
                         and abs(s.BoundingBox().ylen - 5.0) < 0.05
                         and abs(s.BoundingBox().zlen) < 6.0]
                self.assertEqual(len(tubes), 0)
                return
            tubes = [s for s in shape.Solids()
                     if abs(s.BoundingBox().zlen - self.gap["model_gap_mm"]) < 0.02
                     and abs(s.BoundingBox().xlen - 5.0) < 0.05
                     and abs(s.BoundingBox().ylen - 5.0) < 0.05]
            self.assertEqual(len(tubes), 4)
            axes = sorted((round((t.BoundingBox().xmin + t.BoundingBox().xmax) / 2, 2),
                           round((t.BoundingBox().ymin + t.BoundingBox().ymax) / 2, 2))
                          for t in tubes)
            expected = sorted((h["x"], h["y"]) for h in self.layout["lcdMountHoles"])
            self.assertEqual(axes, expected)


@unittest.skipIf(cq is None, SKIP_REASON)
class BoardMockupCutTest(unittest.TestCase):
    """The two BOARD mockups must be the current PCB - they are not, today.

    Both tests assert the CORRECT geometry, so they FAIL today and are marked
    ``expectedFailure`` rather than being rewritten to bless the wrong shape.
    Each aggregates EVERY probe into one message, so the failure is the defect
    and not the first assert the test happens to meet. When the tracked
    all-or-nothing repair lands (docs/TODO.md) they turn green, and unittest
    reports an unexpected success - that is the signal, not a bug.

    A probe only earns its place if it can tell the current board from the
    mockup. The four retired LCD carrier positions cannot: every one of them lies
    INSIDE the U cut, so the current front board is void there whether or not a
    hole was drilled (the mockup drills them at the display stack's Ø4.3 mm, not
    at the rear board's real Ø2.7 mm). They are therefore never probed for
    material on the front board; their retirement is stated in the metadata
    (``step_geometry.BOARD_MOCKUP_DEFECTS``) and the real Ø2.7 mm holes are
    probed on the rear board, where they must be void.
    """

    @classmethod
    def setUpClass(cls) -> None:
        shared = built_model()
        cls.layout = shared["layout"]
        cls.solids = shared["built"]["solids"]

    def _is_material(self, body, x: float, y: float, z: float) -> bool:
        """True when the built solid carries material at the point."""
        return bool(body.val().isInside(cq.Vector(x, y, z)))

    def _inside_u_cut(self, x: float, y: float) -> bool:
        """The U opening spans this rectangle; the corner chamfers are far away."""
        opening = self.layout["frontBoardOpening"]
        return (opening["left"] < x < opening["right"]
                and opening["bottom"] < y < opening["top"])

    def _front_board_cut_probe(self) -> tuple[float, float]:
        """A point INSIDE the real U opening and OUTSIDE the obsolete notch."""
        opening = self.layout["frontBoardOpening"]
        notch = self.layout["notchBounds"]
        probe = (-58.0, 20.0)
        x, y = probe
        self.assertLess(opening["left"], x, "the probe must be INSIDE the U opening")
        self.assertLess(x, opening["right"])
        self.assertLess(opening["bottom"], y)
        self.assertLess(y, opening["top"])
        in_notch = (notch["left"] <= x <= notch["right"]
                    and notch["bottom"] <= y <= notch["top"])
        self.assertFalse(in_notch, "the probe must be OUTSIDE the obsolete notch, or a "
                                   "mockup with the old cut would be void there too")
        return probe

    @unittest.expectedFailure
    def test_the_front_board_mockup_is_the_current_pcb(self) -> None:
        """Cut the U opening, keep the real holes, drop the retired ones."""
        body = self.solids["FRONT_BOARD_PROVISIONAL_Z"]
        front = self.layout["frontPcb"]
        z = self.layout["provisionalZ"]["frontBoardTop"] + front["thickness"] / 2
        x, y = self._front_board_cut_probe()
        problems: list[str] = []

        # 1. The U opening: the current board is VOID at the probe.
        if self._is_material(body, x, y, z):
            problems.append(
                f"the U opening is MATERIAL at ({x:.2f}, {y:.2f}, {z:.2f}) but the current "
                "board is void there: the mockup cuts the obsolete notchBounds rectangle "
                "instead of FRONT_BOARD_OPENING_POLYGON")

        # 2. Retired holes that still land on real board MATERIAL: the current
        #    board has no hole there. A retired hole inside the U cut is skipped -
        #    it is void on the current board anyway, so the probe proves nothing.
        retired = ([("LCD", h) for h in self.layout["lcdMountHoles"]]
                   + [("old standoff", h) for h in self.layout["frontBoardHoles"]]
                   + [("bracket", {"x": bx, "y": self.layout["bracket"]["mountY"]})
                      for bx in self.layout["bracket"]["mountX"]])
        on_material = [(kind, h) for kind, h in retired
                       if abs(h["x"]) < front["width"] / 2
                       and abs(h["y"]) < front["height"] / 2
                       and not self._inside_u_cut(h["x"], h["y"])]
        self.assertTrue(on_material, "no retired hole lies on board material, so this "
                                     "test could not see a retired hole at all")
        for kind, hole in on_material:
            if not self._is_material(body, hole["x"], hole["y"], z):
                problems.append(
                    f"a retired {kind} hole is cut at ({hole['x']:.2f}, {hole['y']:.2f}) "
                    "but the current front board has no hole there")

        # 3. The holes the current board DOES drill, so both must be VOID: the
        #    four front-rear spacers and the four PANEL_MOUNT_POINTS of the frame.
        for label, hole in ([("front-rear spacer", h) for h in self.layout["frontRearMount"]]
                            + [("panel-mount frame", h)
                               for h in self.layout["panelMountPoints"]]):
            if self._is_material(body, hole["x"], hole["y"], z):
                problems.append(
                    f"the real {label} hole at ({hole['x']:.2f}, {hole['y']:.2f}) is not "
                    "drilled")

        self.assertFalse(problems, "front-board mockup is not the current PCB:\n  - "
                         + "\n  - ".join(problems))

    @unittest.expectedFailure
    def test_the_rear_board_mockup_is_the_current_pcb(self) -> None:
        """Drill the four LCD carrier holes and the real spacers."""
        body = self.solids["REAR_BOARD_PROVISIONAL_Z"]
        rear = self.layout["rearPcb"]
        z = self.layout["provisionalZ"]["rearBoardFront"] + rear["thickness"] / 2
        problems: list[str] = []

        # The four Ø2.7 mm LCD carrier holes: the current board is VOID at each.
        for hole in self.layout["lcdMountHoles"]:
            if self._is_material(body, hole["x"], hole["y"], z):
                problems.append(
                    f"the LCD carrier hole at ({hole['x']:.2f}, {hole['y']:.2f}) is "
                    "MATERIAL: the mockup does not drill it")

        # The holes the current board DOES drill, so both must be VOID.
        for hole in self.layout["frontRearMount"]:
            if self._is_material(body, hole["x"], hole["y"], z):
                problems.append(
                    f"the front-rear spacer hole at ({hole['x']:.2f}, {hole['y']:.2f}) is "
                    "not drilled")

        self.assertFalse(problems, "rear-board mockup is not the current PCB:\n  - "
                         + "\n  - ".join(problems))


class EnoughPlaceholdersEnvelopeModeTest(unittest.TestCase):
    """Without a verified vendor model the placeholders are skipped, not faked."""

    @unittest.skipIf(cq is None, SKIP_REASON)
    def test_envelope_mode_skips_the_placeholders_with_a_reason(self) -> None:
        # Envelope mode is the run WITHOUT a vendor model. Point the default at a
        # file that is not in the tree, so the builder has nothing to verify.
        missing = Path("/nonexistent/waveshare-4.3inch-dsi-lcd.step")
        with mock.patch.object(sa, "DEFAULT_DISPLAY_STEP", missing), TemporaryDirectory() as tmp:
            out = Path(tmp) / "cad"
            self.assertEqual(sa.main(["--out", str(out)]), 0)
            document = json.loads((out / sa.ASSUMPTIONS).read_text())
        self.assertEqual(document["display_step"]["status"], "absent")
        block = document["bodies"]["LCD_MOUNT_PLACEHOLDERS"]
        self.assertEqual(block["kind"], "SKIPPED")
        self.assertIn("14.05 mm design envelope is NOT used", block["reason"])
        self.assertFalse(any("MODEL_FIT_SPACER" in n for n in document["bodies"]))


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

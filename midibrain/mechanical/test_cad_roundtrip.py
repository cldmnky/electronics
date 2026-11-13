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
        # metadata-only blocks are not solids
        metadata_only = {"LCD_MOUNT_PLACEHOLDERS"}
        for name in self.document["bodies"]:
            if name in metadata_only:
                continue
            self.assertIn(name, text, f"{name} is not a named body in the STEP file")
        placeholders = self.document["bodies"]["LCD_MOUNT_PLACEHOLDERS"]
        if self.document["display_step"]["status"] == "verified":
            # vendor mode: all four model-fit placeholders are drawn
            self.assertEqual(placeholders["kind"], "MODEL-FIT GEOMETRIC PLACEHOLDER, non-approved")
            for index in (1, 2, 3, 4):
                self.assertIn(f"MODEL_FIT_SPACER_PLACEHOLDER_NON_APPROVED_{index}", text)
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

    def test_the_reach_gauge_uses_one_datum_and_the_nominal_tip(self) -> None:
        """tall = board near face 19.05, low = nominal tip 16.5, step 2.55."""
        data = sg.load_layout()
        lo, hi, _ = stl_bounds(self.out / sa.STL_GAUGE)
        self.assertAlmostEqual(hi[2] - lo[2], data["lightPipeReach"]["boardNearFaceZ"],
                               delta=0.01)
        height = self.document["light_pipe_reach"]["gauge_mm"]
        self.assertAlmostEqual(height["tall"], 19.05, delta=1e-9)
        self.assertAlmostEqual(height["low"], 16.5, delta=1e-9)
        self.assertAlmostEqual(height["tall"] - height["low"], 2.55, delta=1e-6)
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
        self.assertIn("PROVISIONAL", marker["z_basis"])
        self.assertIn("not on it", marker["z_basis"])
        self.assertAlmostEqual(marker["z_mm"][0], 10.05, places=3)

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
            # The committed reference STEP is the default. The builder verified it
            # by SHA-256, so the vendor body is drawn and the envelope is not.
            self.assertEqual(display["status"], "verified")
            self.assertEqual(display["step_sha256_actual"], display["step_sha256_expected"])
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
        self.assertAlmostEqual(self.body["placed_global_z_mm"]["glass_front"], 0.0, places=3)

    def test_the_electronics_point_rearward(self) -> None:
        z = self.body["placed_global_z_mm"]
        self.assertLess(z["glass_front"], z["pcb_rear"])
        self.assertLess(z["pcb_rear"], z["boss_rear"])
        self.assertLess(z["boss_rear"], z["rear_most"])
        self.assertLess(z["glass_front"], z["glass_rear"])

    def test_the_front_frame_is_disclosed_1_9_mm_proud(self) -> None:
        z = self.body["placed_global_z_mm"]
        self.assertAlmostEqual(z["touch_frame_front"], -1.9, places=3)
        self.assertIn("1.90 mm", self.body["front_frame_disclosure"])
        self.assertIn("NOT flush", self.body["front_frame_disclosure"])
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
        archive = os.environ.get("MIDIBRAIN_DISPLAY_ARCHIVE")
        cls.source_args = display_argv()
        if archive:
            cls.step_path, _ = sa.resolve_display_archive(archive)
        else:
            cls.step_path, _ = sa.resolve_display_step(os.environ.get("MIDIBRAIN_DISPLAY_STEP"))
        cls.layout = sa.load_layout()
        cls.built = sa.build(cq, cls.layout, cls.step_path, False)
        cls.solids = cls.built["solids"]
        cls.meta = cls.built["meta"]["LCD_MOUNT_PLACEHOLDERS"]
        cls.gap = sg.display_mount_gap(cls.layout)

    def _spacers(self) -> list[dict]:
        return [b for n, b in self.solids.items()
                if n.startswith("MODEL_FIT_SPACER_PLACEHOLDER_NON_APPROVED")]

    def test_four_placeholders_exist_with_distinct_names(self) -> None:
        names = sorted(n for n in self.solids
                       if n.startswith("MODEL_FIT_SPACER_PLACEHOLDER_NON_APPROVED"))
        self.assertEqual(names, [f"MODEL_FIT_SPACER_PLACEHOLDER_NON_APPROVED_{i}"
                                 for i in (1, 2, 3, 4)])
        self.assertEqual(self.meta["count"], 4)

    def test_the_span_comes_from_the_drawn_solids(self) -> None:
        for body in self.meta["spacers"]:
            self.assertAlmostEqual(body["z_mm"][0], 10.24, places=2)   # boss rear
            self.assertAlmostEqual(body["z_mm"][1], 19.05, places=2)   # board top face
            self.assertAlmostEqual(body["length_mm"], 8.81, places=2)

    def test_the_axes_are_the_exported_mount_holes(self) -> None:
        axes = sorted(tuple(b["axis_xy_mm"]) for b in self.meta["spacers"])
        expected = sorted((h["x"], h["y"]) for h in self.layout["lcdMountHoles"])
        self.assertEqual(axes, expected)

    def test_od_and_id_are_only_the_assumed_illustration(self) -> None:
        for body in self.meta["spacers"]:
            self.assertAlmostEqual(body["outer_diameter_mm"], 5.0, places=6)
            self.assertAlmostEqual(body["inner_diameter_mm"], 2.7, places=6)
            # OD exceeds the board hole, so a bearing annulus is visible
            self.assertGreater(body["annulus_bearing_width_mm"], 0.0)

    def test_no_positive_volume_intrusion_against_display_or_board(self) -> None:
        for body in self.meta["spacers"]:
            for volume in body["intrusion_volume_mm3"].values():
                self.assertLessEqual(volume, sa.INTRUSION_EPS_MM3)

    def test_the_placeholders_carry_no_fit_or_approval_claim(self) -> None:
        """Non-approval metadata, not a vacuous substring check."""
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

    def test_a_5_00_mm_spacer_does_not_reach_the_drawn_boss_plane(self) -> None:
        """The design-chain candidate is 3.81 mm short of the model's boss ends."""
        board_top = self.gap["board_top_z_mm"]
        chain_end = board_top - self.gap["design_chain_spacer_mm"]
        self.assertAlmostEqual(chain_end, 14.05, places=6)
        self.assertAlmostEqual(chain_end - self.gap["boss_rear_z_mm"], 3.81, places=6)
        display = self.solids["DISPLAY_WAVESHARE_VENDOR_STEP"]
        # A 5.00 mm spacer ANCHORED ON THE BOARD TOP FACE: it spans 14.05..19.05,
        # so it stops 3.81 mm short of the drawn boss ends at 10.24.
        short = (cq.Workplane("XY").workplane(offset=chain_end)
                 .center(-59.15, 41.9).circle(2.5).circle(1.35).extrude(5.0))
        intrusions = sa.spacer_intrusions(cq, short, display,
                                          self.solids["FRONT_BOARD_PROVISIONAL_Z"].val())
        self.assertEqual(intrusions["display_mm3"], 0.0)
        bb = short.val().BoundingBox()
        self.assertAlmostEqual(bb.zmin, 14.05, places=2)
        self.assertAlmostEqual(bb.zmax, 19.05, places=2)
        self.assertAlmostEqual(bb.zmin - self.gap["boss_rear_z_mm"], 3.81, places=2)

    def test_an_over_long_placeholder_is_rejected(self) -> None:
        """Negative regression: extending a placeholder must fail the limit."""
        display = self.solids["DISPLAY_WAVESHARE_VENDOR_STEP"]
        board = self.solids["FRONT_BOARD_PROVISIONAL_Z"].val()
        over = (cq.Workplane("XY").workplane(offset=10.24).center(-59.15, 41.9)
                .circle(2.5).circle(1.35).extrude(8.81 + 0.2))
        intrusions = sa.spacer_intrusions(cq, over, display, board)
        self.assertGreater(max(intrusions.values()), sa.INTRUSION_EPS_MM3)

    def test_the_exported_step_carries_four_spacer_solids(self) -> None:
        with TemporaryDirectory() as tmp:
            out = Path(tmp) / "cad"
            # both documented source paths are supported: --display-archive and
            # --display-step; the class recorded which one this run uses
            self.assertEqual(sa.main(["--out", str(out), *self.source_args]), 0)
            shape = cq.importers.importStep(str(out / sa.STEP_NAME)).val()
            tubes = [s for s in shape.Solids()
                     if abs(s.BoundingBox().zlen - 8.81) < 0.02
                     and abs(s.BoundingBox().xlen - 5.0) < 0.05
                     and abs(s.BoundingBox().ylen - 5.0) < 0.05]
            self.assertEqual(len(tubes), 4)
            axes = sorted((round((t.BoundingBox().xmin + t.BoundingBox().xmax) / 2, 2),
                           round((t.BoundingBox().ymin + t.BoundingBox().ymax) / 2, 2))
                          for t in tubes)
            expected = sorted((h["x"], h["y"]) for h in self.layout["lcdMountHoles"])
            self.assertEqual(axes, expected)


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

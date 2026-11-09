#!/usr/bin/env python3
"""Pure-geometry tests for the STEP/STL verification models.

No CAD dependency: these run everywhere.

    cd midibrain && python3 -m unittest discover -s mechanical -p 'test_*.py'

Each test states the rule it protects. The mutation tests at the bottom take the
reviewed finding - a 0.45 mm XY projection between the Adafruit 908 flange and
the front board - and prove that this module never reports it as a 3D collision
and never reports the assembly clear while the flange depth is unknown.
"""
from __future__ import annotations

import copy
import json
import unittest
from pathlib import Path
from tempfile import TemporaryDirectory

import step_geometry as sg


def layout() -> dict:
    return sg.load_layout()


def box(name: str, x: tuple[float, float], y: tuple[float, float], z: tuple[float, float] | None,
        *, assumed: tuple[str, ...] = (), provenance: str = sg.PROVENANCE["canonical"]) -> sg.Envelope:
    return sg.Envelope(name=name, provenance=provenance,
                       x=sg.Span(*x), y=sg.Span(*y),
                       z=None if z is None else sg.Span(*z),
                       assumed_axes=assumed)


class CanonicalDataTest(unittest.TestCase):
    """The bridge must carry every coordinate the CAD model cuts."""

    def test_layout_json_carries_the_hole_sets(self) -> None:
        data = layout()
        for key in ("railHoles", "railHoleDiameter", "frontBoardHoles", "lcdMountHoles",
                    "displayBossPattern", "piHoles", "piMount", "frontRearMount",
                    "mountHoleDiameter", "lcdCutoutOutline", "notchBounds", "bracket"):
            self.assertIn(key, data, f"{key} is missing from mechanical/layout.json")
        self.assertEqual(data["railHoleDiameter"], 3.2)
        self.assertEqual(len(data["railHoles"]), 4)
        self.assertEqual(len(data["frontBoardHoles"]), 4)
        self.assertEqual(len(data["lcdMountHoles"]), 4)
        self.assertEqual(len(data["piHoles"]), 4)

    def test_the_canonical_panel_features_are_complete(self) -> None:
        """20 apertures and 14 drills: every opening the panel board renders."""
        data = layout()
        apertures = data["panelApertures"]
        drills = data["panelDrills"]
        self.assertEqual(len(apertures), 20)
        self.assertEqual(len(drills), 14)
        self.assertEqual(len(data["panelMountHoles"]), 8)
        names = [a["name"] for a in apertures]
        self.assertEqual(names[0], "LCD_APERTURE")
        self.assertEqual(len(set(names)), len(names), "aperture names must be unique")
        for required in ("USB_FEEDTHROUGH_0", "REC BUTTON", "PLAY START BUTTON",
                         "SHIFT/EXIT BUTTON", "REC LED", "VALUE ENCODER",
                         "POWER BUTTON", "NAV", "MIDI IN 1 JACK", "MIDI OUT 2 JACK"):
            self.assertIn(required, names)
        self.assertEqual([d["name"] for d in drills],
                         ["USB_0_-1", "USB_0_1", "USB_1_-1", "USB_1_1",
                          "USB_2_-1", "USB_2_1", "STAND_1", "STAND_2", "STAND_3",
                          "STAND_4", "RACK_1", "RACK_2", "RACK_3", "RACK_4"])

    def test_the_canonical_panel_diameters_are_the_documented_ones(self) -> None:
        """Diameters come from PANEL_CUTOUTS; nothing is re-derived here."""
        data = layout()
        by_name = {a["name"]: a for a in data["panelApertures"]}
        self.assertEqual(by_name["REC BUTTON"]["radius"], 6.2)   # 12.4 mm B3F
        self.assertEqual(by_name["REC LED"]["radius"], 2.0)      # 4.0 mm light pipe
        self.assertEqual(by_name["VALUE ENCODER"]["radius"], 3.6)  # 7.2 mm bushing
        self.assertEqual(by_name["MIDI IN 1 JACK"]["radius"], 3.1)  # 6.2 mm barrel
        self.assertEqual({d["diameter"] for d in data["panelDrills"]}, {3.2, 3.5, 2.7})

    def test_every_canonical_panel_feature_sits_inside_the_panel(self) -> None:
        data = layout()
        half_w, half_h = data["panel"]["width"] / 2, data["panel"]["height"] / 2
        for aperture in data["panelApertures"]:
            if aperture["shape"] == "polygon":
                for point in aperture["points"]:
                    self.assertLess(abs(point["x"]), half_w)
                    self.assertLess(abs(point["y"]), half_h)
                continue
            self.assertLess(abs(aperture["x"]), half_w)
            self.assertLess(abs(aperture["y"]), half_h)
        for drill in data["panelDrills"]:
            self.assertLess(abs(drill["x"]), half_w)
            self.assertLess(abs(drill["y"]), half_h)
            self.assertGreater(drill["diameter"], 0)

    def test_the_panel_control_centres_are_unmoved(self) -> None:
        """The owner's control centres, straight from the canonical data."""
        data = layout()
        centres = {(a.get("x"), a.get("y")) for a in data["panelApertures"]
                   if a["shape"] == "circle"}
        for centre in {(-53, -34), (-29, -34), (-5, -34), (19, -34), (52, -34),
                       (52.5, -48), (52, 30), (52, 11), (52, -8), (-53, -47.5)}:
            self.assertIn(centre, centres, f"control centre {centre} moved")

    def test_rail_holes_are_the_panel_corners(self) -> None:
        data = layout()
        half_w = data["panel"]["width"] / 2
        half_h = data["panel"]["height"] / 2
        for hole in data["railHoles"]:
            self.assertLess(abs(hole["x"]), half_w)
            self.assertLess(abs(hole["y"]), half_h)
        xs = sorted(h["x"] for h in data["railHoles"])
        self.assertEqual(xs, [-73.78, -73.78, 73.78, 73.78])

    def test_lcd_mount_holes_are_the_display_boss_pattern(self) -> None:
        data = layout()
        d = data["display"]
        for hole in data["lcdMountHoles"]:
            self.assertAlmostEqual(abs(hole["x"] - d["x"]), 49.65, places=6)
            self.assertAlmostEqual(abs(hole["y"] - d["y"]), 30.90, places=6)

    def test_flange_source_records_no_depth(self) -> None:
        """A depth in the constant would be an invented dimension."""
        self.assertIsNone(sg.ADAFRUIT_908["depth_mm"])
        self.assertIn("product-files/908", sg.ADAFRUIT_908["source_url"])
        self.assertEqual(sg.ADAFRUIT_908["flange_width_mm"], 38.30)
        self.assertEqual(sg.ADAFRUIT_908["flange_height_mm"], 11.90)
        self.assertEqual(sg.ADAFRUIT_908["mount_pitch_mm"], 30.0)


class SpanTest(unittest.TestCase):
    def test_inverted_and_non_finite_spans_are_errors(self) -> None:
        for lo, hi in ((1.0, 0.0), (0.0, float("nan")), (float("inf"), 1.0)):
            with self.assertRaises(sg.GeometryError):
                sg.Span(lo, hi)

    def test_around_rejects_a_negative_size(self) -> None:
        with self.assertRaises(sg.GeometryError):
            sg.Span.around(0.0, -1.0)
        self.assertEqual(sg.Span.around(10.0, 2.0), sg.Span(9.0, 11.0))

    def test_unbounded_span_cannot_prove_a_separation(self) -> None:
        unbounded = sg.Span(0.0, None)
        self.assertFalse(unbounded.bounded)
        self.assertIsNone(unbounded.gap(sg.Span(5.0, 6.0)))
        self.assertIsNone(unbounded.overlap(sg.Span(-1.0, 1.0)))

    def test_gap_and_overlap_are_signed(self) -> None:
        a, b = sg.Span(0.0, 1.0), sg.Span(2.0, 3.0)
        self.assertAlmostEqual(a.gap(b), 1.0)
        self.assertAlmostEqual(a.overlap(b), -1.0)
        self.assertAlmostEqual(sg.Span(0.0, 2.0).overlap(sg.Span(1.0, 3.0)), 1.0)


class ClassifyTest(unittest.TestCase):
    """The three-way rule: clear, collision, undetermined."""

    def test_disjoint_x_is_clear_whatever_the_other_axes_do(self) -> None:
        a = box("A", (0, 10), (0, 10), (0, 10))
        b = box("B", (11, 20), (0, 10), (0, 10))
        verdict = sg.classify(a, b)
        self.assertEqual(verdict.status, sg.CLEAR)
        self.assertAlmostEqual(verdict.clearance_mm, 1.0)
        self.assertEqual(verdict.decisive_axis, "x")
        self.assertTrue(verdict.definitive)

    def test_disjoint_y_is_clear(self) -> None:
        a = box("A", (0, 10), (0, 10), (0, 10))
        b = box("B", (0, 10), (12, 20), (0, 10))
        verdict = sg.classify(a, b)
        self.assertEqual(verdict.status, sg.CLEAR)
        self.assertAlmostEqual(verdict.clearance_mm, 2.0)
        self.assertEqual(verdict.decisive_axis, "y")

    def test_disjoint_z_is_clear(self) -> None:
        a = box("A", (0, 10), (0, 10), (0, 10))
        b = box("B", (0, 10), (0, 10), (15, 20))
        verdict = sg.classify(a, b)
        self.assertEqual(verdict.status, sg.CLEAR)
        self.assertAlmostEqual(verdict.clearance_mm, 5.0)
        self.assertEqual(verdict.decisive_axis, "z")

    def test_a_separating_documented_axis_wins_over_an_unknown_one(self) -> None:
        a = box("A", (0, 10), (0, 10), (0, 10))
        b = box("B", (11, 20), (0, 10), None)
        verdict = sg.classify(a, b)
        self.assertEqual(verdict.status, sg.CLEAR)
        self.assertAlmostEqual(verdict.clearance_mm, 1.0)
        self.assertIn("z", verdict.undetermined_axes)

    def test_positive_overlap_on_all_three_axes_is_a_collision(self) -> None:
        a = box("A", (0, 10), (0, 10), (0, 10))
        b = box("B", (5, 20), (8, 20), (9, 20))
        verdict = sg.classify(a, b)
        self.assertEqual(verdict.status, sg.COLLISION)
        # The shallowest overlap is 1.0 mm in z.
        self.assertAlmostEqual(verdict.overlap_mm, 1.0)
        self.assertTrue(verdict.definitive)

    def test_one_unknown_axis_makes_the_result_undetermined(self) -> None:
        a = box("A", (0, 10), (0, 10), (0, 10))
        b = box("B", (5, 20), (5, 20), None)
        verdict = sg.classify(a, b)
        self.assertEqual(verdict.status, sg.UNDETERMINED)
        self.assertIsNone(verdict.clearance_mm)
        self.assertEqual(verdict.undetermined_axes, ("z",))
        self.assertTrue(any("fail closed" in r for r in verdict.reasons))

    def test_an_unbounded_axis_is_treated_as_unknown(self) -> None:
        a = box("A", (0, 10), (0, 10), (0, 10))
        b = sg.Envelope("B", sg.PROVENANCE["unknown"], sg.Span(5, 20), sg.Span(5, 20),
                        sg.Span(9.0, None))
        verdict = sg.classify(a, b)
        self.assertEqual(verdict.status, sg.UNDETERMINED)
        self.assertEqual(verdict.undetermined_axes, ("z",))

    def test_touching_is_contact_not_a_collision(self) -> None:
        a = box("A", (0, 10), (0, 10), (0, 10))
        b = box("B", (0, 10), (0, 10), (10, 20))
        verdict = sg.classify(a, b)
        self.assertEqual(verdict.status, sg.CLEAR)
        self.assertEqual(verdict.clearance_mm, 0.0)
        self.assertEqual(verdict.decisive_axis, "z")

    def test_a_candidate_axis_never_produces_a_definitive_result(self) -> None:
        """An illustrative Z volume must not become a fit verdict, either way."""
        a = box("A", (0, 10), (0, 10), (0, 10))
        colliding = box("B", (5, 20), (5, 20), (5, 15), assumed=("z",))
        verdict = sg.classify(a, colliding)
        self.assertEqual(verdict.status, sg.UNDETERMINED)
        self.assertIsNone(verdict.overlap_mm)
        self.assertTrue(verdict.illustrative)

        separated = box("C", (5, 20), (5, 20), (15, 25), assumed=("z",))
        verdict = sg.classify(a, separated)
        # X and Y overlap, so the only separating axis is the candidate one.
        self.assertEqual(verdict.status, sg.UNDETERMINED)

    def test_a_documented_axis_still_proves_clear_beside_a_candidate_axis(self) -> None:
        a = box("A", (0, 10), (0, 10), (0, 10))
        b = box("B", (20, 30), (0, 10), (5, 15), assumed=("z",))
        verdict = sg.classify(a, b)
        self.assertEqual(verdict.status, sg.CLEAR)
        self.assertAlmostEqual(verdict.clearance_mm, 10.0)
        self.assertEqual(verdict.decisive_axis, "x")


class FlangeProjectionTest(unittest.TestCase):
    """The 0.45 mm finding, exactly, and how it moves."""

    def test_both_908_flanges_are_projected(self) -> None:
        overlaps = sg.flange_overlaps(layout())
        self.assertEqual([o.feedthrough_id for o in overlaps],
                         ["UMC AUDIO USB", "MIDI HUB USB"])
        for o in overlaps:
            self.assertAlmostEqual(o.flange.width, 38.30, places=6)
            self.assertAlmostEqual(o.flange.height, 11.90, places=6)
            self.assertFalse(o.depth_known)
            self.assertIn("not a collision", o.z_note)

    def test_umc_flange_overlaps_the_board_by_exactly_0_45_mm(self) -> None:
        umc = sg.flange_overlaps(layout())[0]
        self.assertEqual(len(umc.overlap_rects), 1, "one strip at the lower flange edge")
        rect = umc.overlap_rects[0]
        self.assertAlmostEqual(rect.y1, 47.55, places=6)   # 53.5 - 11.90/2
        self.assertAlmostEqual(rect.y2, 48.00, places=6)   # the notch bottom
        self.assertAlmostEqual(rect.height, 0.45, places=6)
        self.assertAlmostEqual(rect.x1, -24.15, places=6)
        self.assertAlmostEqual(rect.x2, 14.15, places=6)
        self.assertAlmostEqual(umc.max_overlap_y_mm, 0.45, places=6)
        self.assertAlmostEqual(umc.overlap_area_mm2, 38.30 * 0.45, places=6)

    def test_the_plus_35_flange_also_overlaps_beside_the_notch(self) -> None:
        hub = sg.flange_overlaps(layout())[1]
        self.assertEqual(len(hub.overlap_rects), 2)
        lower, right = hub.overlap_rects
        self.assertAlmostEqual(lower.height, 0.45, places=6)
        self.assertAlmostEqual(right.x1, 50.0, places=6)   # notch right edge
        self.assertAlmostEqual(right.x2, 54.15, places=6)  # 35 + 38.30/2
        self.assertAlmostEqual(right.height, 8.0, places=6)
        self.assertAlmostEqual(hub.overlap_area_mm2, 50.435, places=6)

    def test_the_projection_is_undetermined_in_three_dimensions(self) -> None:
        reports = {f"{p.a}|{p.b}": p for p in sg.pair_reports(layout())}
        for name in ("FEEDTHROUGH_UMC_AUDIO_USB_FLANGE|FRONT_BOARD_PROVISIONAL_Z",
                     "FEEDTHROUGH_MIDI_HUB_USB_FLANGE|FRONT_BOARD_PROVISIONAL_Z",
                     "FEEDTHROUGH_USB_C_POWER_FLANGE|FRONT_BOARD_PROVISIONAL_Z",
                     "FEEDTHROUGH_UMC_AUDIO_USB_FLANGE|PANEL_32HP_3U"):
            self.assertEqual(reports[name].verdict.status, sg.UNDETERMINED, name)
            self.assertIsNone(reports[name].verdict.overlap_mm)
            self.assertIn("z", reports[name].verdict.undetermined_axes, name)

    def test_display_clears_the_front_board_under_the_provisional_stack(self) -> None:
        reports = {f"{p.a}|{p.b}": p for p in sg.pair_reports(layout())}
        verdict = reports[
            "DISPLAY_WAVESHARE_4_3_DSI_PROVISIONAL_Z|FRONT_BOARD_PROVISIONAL_Z"].verdict
        self.assertEqual(verdict.status, sg.CLEAR)
        self.assertAlmostEqual(verdict.clearance_mm, 5.0, places=6)
        self.assertEqual(verdict.decisive_axis, "z")

    def test_flange_clears_the_display_because_y_is_disjoint(self) -> None:
        reports = {f"{p.a}|{p.b}": p for p in sg.pair_reports(layout())}
        verdict = reports[
            "FEEDTHROUGH_UMC_AUDIO_USB_FLANGE|DISPLAY_WAVESHARE_4_3_DSI_PROVISIONAL_Z"].verdict
        self.assertEqual(verdict.status, sg.CLEAR)
        self.assertAlmostEqual(verdict.clearance_mm, 2.55, places=6)

    def test_the_two_boards_keep_the_candidate_gap(self) -> None:
        reports = {f"{p.a}|{p.b}": p for p in sg.pair_reports(layout())}
        verdict = reports["FRONT_BOARD_PROVISIONAL_Z|REAR_BOARD_PROVISIONAL_Z"].verdict
        self.assertEqual(verdict.status, sg.CLEAR)
        self.assertAlmostEqual(verdict.clearance_mm, 20.0, places=6)

    def test_the_usb_c_envelope_is_explicitly_unknown(self) -> None:
        """No verified 5 A part exists, so no envelope may be drawn for it."""
        env = sg.assembly_envelopes(layout())
        usb_c = env["FEEDTHROUGH_USB_C_POWER_FLANGE"]
        self.assertEqual(usb_c.provenance, sg.PROVENANCE["unknown"])
        self.assertIsNone(usb_c.x)
        self.assertIsNone(usb_c.y)
        self.assertIsNone(usb_c.z)
        self.assertIn("candidate not approved", usb_c.note)

    def test_a_fabricated_flange_depth_is_refused(self) -> None:
        fabricated = dict(sg.ADAFRUIT_908)
        fabricated["depth_mm"] = 4.0
        with self.assertRaises(sg.GeometryError):
            sg.flange_overlaps(layout(), fabricated)


class FlangeMutationTest(unittest.TestCase):
    """The 0.45 mm overlap is geometry, not a threshold: mutate it and watch it move."""

    def test_raising_the_feedthrough_by_0_45_mm_removes_the_bottom_strip(self) -> None:
        data = copy.deepcopy(layout())
        for f in data["feedthroughs"]:
            f["y"] += 0.45
        umc = sg.flange_overlaps(data)[0]
        self.assertEqual(umc.overlap_rects, (),
                         "the flange lower edge now sits exactly on the notch bottom")
        self.assertAlmostEqual(umc.overlap_area_mm2, 0.0)
        self.assertEqual(umc.max_overlap_y_mm, 0.0)

    def test_raising_the_feedthrough_by_0_44_mm_leaves_a_0_01_mm_strip(self) -> None:
        data = copy.deepcopy(layout())
        for f in data["feedthroughs"]:
            f["y"] += 0.44
        umc = sg.flange_overlaps(data)[0]
        self.assertEqual(len(umc.overlap_rects), 1)
        self.assertAlmostEqual(umc.max_overlap_y_mm, 0.01, places=9)
        self.assertAlmostEqual(umc.overlap_area_mm2, 0.383, places=9)

    def test_lowering_the_feedthrough_by_0_45_mm_doubles_nothing_but_adds_0_9(self) -> None:
        data = copy.deepcopy(layout())
        for f in data["feedthroughs"]:
            f["y"] -= 0.45
        umc = sg.flange_overlaps(data)[0]
        self.assertAlmostEqual(umc.max_overlap_y_mm, 0.90, places=6)

    def test_a_raised_notch_bottom_grows_the_same_overlap(self) -> None:
        """The overlap is read from the notch in the data, never hardcoded."""
        data = copy.deepcopy(layout())
        data["notchBounds"]["bottom"] = 49.0
        umc = sg.flange_overlaps(data)[0]
        self.assertAlmostEqual(umc.max_overlap_y_mm, 1.45, places=6)

    def test_a_lowered_notch_bottom_removes_the_overlap(self) -> None:
        data = copy.deepcopy(layout())
        data["notchBounds"]["bottom"] = 47.0
        umc = sg.flange_overlaps(data)[0]
        self.assertEqual(umc.overlap_rects, ())

    def test_a_widened_notch_removes_the_plus_35_side_strip(self) -> None:
        data = copy.deepcopy(layout())
        data["notchBounds"]["right"] = 55.0
        hub = sg.flange_overlaps(data)[1]
        self.assertEqual(len(hub.overlap_rects), 1)
        self.assertAlmostEqual(hub.max_overlap_y_mm, 0.45, places=6)
        self.assertAlmostEqual(hub.overlap_area_mm2, 38.30 * 0.45, places=6)


class CoordinateDriftTest(unittest.TestCase):
    """A drifted coordinate must fail loudly, never produce a different fit."""

    def test_a_drifted_screw_pitch_is_refused(self) -> None:
        data = copy.deepcopy(layout())
        for f in data["feedthroughs"]:
            if f["id"] == "UMC AUDIO USB":
                f["holeSpacing"] = 28.0
        with self.assertRaises(sg.GeometryError):
            sg.flange_overlaps(data)

    def test_a_drifted_panel_opening_is_refused(self) -> None:
        data = copy.deepcopy(layout())
        for f in data["feedthroughs"]:
            if f["id"] == "MIDI HUB USB":
                f["cutoutWidth"] = 16.0
        with self.assertRaises(sg.GeometryError):
            sg.flange_overlaps(data)

    def test_a_missing_feedthrough_is_refused(self) -> None:
        data = copy.deepcopy(layout())
        data["feedthroughs"] = [f for f in data["feedthroughs"] if f["id"] != "UMC AUDIO USB"]
        with self.assertRaises(sg.GeometryError):
            sg.flange_overlaps(data)

    def test_a_drifted_panel_thickness_moves_the_panel_envelope(self) -> None:
        data = copy.deepcopy(layout())
        data["provisionalZ"]["panelRear"] = 2.4
        env = sg.assembly_envelopes(data)
        self.assertAlmostEqual(env["PANEL_32HP_3U"].z.hi, 2.4)

    def test_envelope_z_follows_the_provisional_chain(self) -> None:
        env = sg.assembly_envelopes(layout())
        self.assertAlmostEqual(env["FRONT_BOARD_PROVISIONAL_Z"].z.lo, 19.05)
        self.assertAlmostEqual(env["FRONT_BOARD_PROVISIONAL_Z"].z.hi, 20.65)
        self.assertAlmostEqual(env["DISPLAY_WAVESHARE_4_3_DSI_PROVISIONAL_Z"].z.hi, 14.05)
        self.assertAlmostEqual(env["REAR_BOARD_PROVISIONAL_Z"].z.lo, 40.65)
        self.assertAlmostEqual(env["PANEL_32HP_3U"].z.hi, 1.6)

    def test_the_light_pipe_is_not_drawn_as_a_placed_body(self) -> None:
        env = sg.assembly_envelopes(layout())
        pipe = env["LIGHT_PIPE_PLP2_400_C"]
        self.assertIsNone(pipe.z, "the reach is an interval, not a modelled body")
        self.assertIn("light_pipe_reach.py", pipe.note)


class OutputDirectoryTest(unittest.TestCase):
    """An explicit output directory, and nothing else ever touched."""

    def test_the_default_directory_is_the_documented_one(self) -> None:
        self.assertEqual(sg.DEFAULT_OUTPUT_DIR.name, "cad-out")
        self.assertIn("mechanical", str(sg.DEFAULT_OUTPUT_DIR))

    def test_a_file_is_refused(self) -> None:
        with TemporaryDirectory() as tmp:
            target = Path(tmp) / "not-a-dir"
            target.write_text("x")
            with self.assertRaises(sg.GeometryError):
                sg.resolve_output_dir(target)

    def test_the_filesystem_root_and_the_project_root_are_refused(self) -> None:
        with self.assertRaises(sg.GeometryError):
            sg.resolve_output_dir("/")
        with self.assertRaises(sg.GeometryError):
            sg.resolve_output_dir(sg.PROJECT_ROOT)

    def test_a_foreign_file_blocks_the_write(self) -> None:
        with TemporaryDirectory() as tmp:
            (Path(tmp) / "important.txt").write_text("keep me")
            with self.assertRaises(sg.GeometryError):
                sg.resolve_output_dir(tmp)
            # Nothing was deleted, and --force names the override explicitly.
            self.assertEqual((Path(tmp) / "important.txt").read_text(), "keep me")
            self.assertEqual(sg.resolve_output_dir(tmp, force=True), Path(tmp).resolve())

    def test_an_empty_directory_and_our_own_outputs_are_accepted(self) -> None:
        with TemporaryDirectory() as tmp:
            self.assertEqual(sg.resolve_output_dir(tmp), Path(tmp).resolve())
            for name in sg.OUTPUT_NAMES:
                (Path(tmp) / name).write_text("generated")
            self.assertEqual(sg.resolve_output_dir(tmp), Path(tmp).resolve())

    def test_resolving_creates_nothing(self) -> None:
        with TemporaryDirectory() as tmp:
            target = Path(tmp) / "new-output"
            self.assertEqual(sg.resolve_output_dir(target), target.resolve())
            self.assertFalse(target.exists())

    def test_a_relative_path_is_resolved_from_the_working_directory(self) -> None:
        import os
        with TemporaryDirectory() as tmp:
            cwd = os.getcwd()
            try:
                os.chdir(tmp)
                self.assertEqual(sg.resolve_output_dir("relative-out"),
                                 (Path(tmp) / "relative-out").resolve())
            finally:
                os.chdir(cwd)


class AssumptionsDocumentTest(unittest.TestCase):
    """The metadata must carry the unknowns, not hide them."""

    def test_the_document_is_json_and_marked_provisional(self) -> None:
        doc = sg.assumptions_document(layout())
        text = json.dumps(doc, indent=2)
        self.assertIn("midibrain-cad-assumptions/1", text)
        self.assertEqual(doc["units"], "mm")
        self.assertTrue(doc["status"].startswith("PROVISIONAL"))
        self.assertIn("not a collision", doc["collision_rule"])
        self.assertGreaterEqual(len(doc["unknowns"]), 5)

    def test_the_flange_projection_is_recorded_with_its_source(self) -> None:
        doc = sg.assumptions_document(layout())
        self.assertEqual(doc["flange_source"]["flange_width_mm"], 38.30)
        self.assertIsNone(doc["flange_source"]["depth_mm"])
        self.assertEqual(len(doc["flange_projections"]), 2)
        umc = doc["flange_projections"][0]
        self.assertEqual(umc["status"], sg.UNDETERMINED)
        self.assertAlmostEqual(umc["max_overlap_y_mm"], 0.45, places=6)
        self.assertAlmostEqual(umc["board_overlap_area_mm2"], 38.30 * 0.45, places=6)

    def test_no_pair_is_reported_as_a_collision_or_as_a_proven_fit(self) -> None:
        doc = sg.assumptions_document(layout())
        self.assertTrue(doc["pairs"])
        for pair in doc["pairs"]:
            self.assertIn(pair["status"], (sg.CLEAR, sg.UNDETERMINED), pair["a"])
            if pair["status"] == sg.CLEAR:
                self.assertIsNotNone(pair["clearance_mm"], pair)
        # The flange never gets a clear or colliding verdict against the board:
        # its depth is not in the drawing.
        for pair in doc["pairs"]:
            if "FLANGE" in pair["a"] and pair["b"].startswith("FRONT_BOARD"):
                self.assertEqual(pair["status"], sg.UNDETERMINED, pair["a"])
                self.assertFalse(pair["definitive"])
                self.assertIn("z", pair["undetermined_axes"])

    def test_the_light_pipe_stays_incomplete(self) -> None:
        doc = sg.assumptions_document(layout())
        self.assertEqual(doc["light_pipe"]["status"], "incomplete")
        self.assertAlmostEqual(doc["light_pipe"]["gap_minus_length_arithmetic_mm"],
                               7.29, places=6)

    def test_the_light_pipe_gauge_is_named_for_its_purpose(self) -> None:
        doc = sg.assumptions_document(layout())
        self.assertTrue(any("LED" in u for u in doc["unknowns"]))

    def test_every_envelope_names_its_provenance(self) -> None:
        doc = sg.assumptions_document(layout())
        self.assertTrue(doc["envelopes"])
        for envelope in doc["envelopes"]:
            self.assertIn(envelope["provenance"], sg.PROVENANCE.values(), envelope["name"])

    def test_the_print_checklist_is_practical_and_has_no_approval_claim(self) -> None:
        text = sg.print_checklist_markdown()
        self.assertIn("100.00 mm", text)
        self.assertIn("PROVISIONAL", text)
        self.assertIn("28", text)
        self.assertNotIn("approved", text.lower())
        self.assertGreaterEqual(len(sg.PRINT_CHECKLIST), 6)


class UsbcCandidateTest(unittest.TestCase):
    """A non-approved candidate envelope: opt-in, and never a verdict."""

    def test_the_candidate_is_absent_unless_it_is_asked_for(self) -> None:
        env = sg.assembly_envelopes(layout())
        self.assertNotIn("USB_C_CANDIDATE_FLANGE_NON_APPROVED", env)
        doc = sg.assumptions_document(layout())
        self.assertFalse(doc["usbc_candidate"]["enabled"])
        self.assertEqual(len(doc["pairs"]), 7)

    def test_the_candidate_records_advertised_dimensions_and_no_pitch(self) -> None:
        candidate = sg.USBC_CANDIDATE
        self.assertEqual(candidate["flange_width_mm"], 25.0)
        self.assertEqual(candidate["flange_height_mm"], 10.2)
        self.assertEqual(candidate["flange_thickness_mm"], 5.0)
        self.assertEqual(candidate["overall_length_mm"], 305.0)
        self.assertEqual(candidate["rear_body_length_mm"], 35.0)
        # The 13.4 mm annotation is ambiguous: it must not become a pitch.
        self.assertIsNone(candidate["mount_pitch_mm"])
        self.assertIsNone(candidate["current_rating"])
        self.assertIn("NON-APPROVED", candidate["status"])
        self.assertIn("amazon.se/dp/B0GF22WM9T", candidate["source_url"])

    def test_the_candidate_envelope_marks_every_axis_as_a_candidate(self) -> None:
        env = sg.assembly_envelopes(layout(), include_usbc_candidate=True)
        flange = env["USB_C_CANDIDATE_FLANGE_NON_APPROVED"]
        self.assertEqual(flange.assumed_axes, ("x", "y", "z"))
        self.assertEqual(flange.provenance, sg.PROVENANCE["candidate"])
        self.assertAlmostEqual(flange.x.length, 25.0, places=6)
        self.assertAlmostEqual(flange.y.length, 10.2, places=6)
        self.assertAlmostEqual(flange.z.lo, 0.0, places=6)
        self.assertAlmostEqual(flange.z.hi, 5.0, places=6)
        self.assertAlmostEqual(flange.x.centre, -38.0, places=6)  # panel cutout centre

    def test_the_candidate_never_produces_a_definitive_result(self) -> None:
        reports = {f"{p.a}|{p.b}": p for p in
                   sg.pair_reports(layout(), include_usbc_candidate=True)}
        verdict = reports[
            "USB_C_CANDIDATE_FLANGE_NON_APPROVED|FRONT_BOARD_PROVISIONAL_Z"].verdict
        self.assertEqual(verdict.status, sg.UNDETERMINED)
        self.assertFalse(verdict.definitive)
        self.assertIsNone(verdict.overlap_mm)
        self.assertIsNone(verdict.clearance_mm)

    def test_the_candidate_does_not_move_the_panel_cutout(self) -> None:
        before = copy.deepcopy(layout()["feedthroughs"])
        sg.assumptions_document(layout(), include_usbc_candidate=True)
        self.assertEqual(layout()["feedthroughs"], before)
        usb_c = next(f for f in before if f["id"] == "USB-C POWER")
        self.assertEqual((usb_c["cutoutWidth"], usb_c["cutoutHeight"], usb_c["holeSpacing"]),
                         (12.6, 7.6, 18.0))

    def test_the_metadata_states_what_is_not_modelled(self) -> None:
        doc = sg.assumptions_document(layout(), include_usbc_candidate=True)
        block = doc["usbc_candidate"]
        self.assertTrue(block["enabled"])
        self.assertIn("rear body", block["not_modelled"])
        self.assertEqual(block["panel_cutout_unchanged"],
                         {"width": 12.6, "height": 7.6, "pitch": 18.0})
        self.assertIn("ambiguous", block["note"])


if __name__ == "__main__":
    unittest.main()

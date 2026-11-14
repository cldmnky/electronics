#!/usr/bin/env python3
"""Tests for the canonical panel feature plan and the reach-gauge model.

    cd midibrain && python3 -m unittest discover -s mechanical -p 'test_*.py'

Why these tests exist
---------------------
The CAD assembly insists on ``20 apertures + 14 drills = 34`` panel
features. The fourteen drills are the six feedthrough screw holes, the four RACK
rail fixings and the four STAND_ panel-mount screws of the PANEL_MOUNT_POINTS
frame. Lowering the number would hide a future loss, so the check validates
IDENTITIES, ORDER, SHAPE, POSITION and the drills' own derivation, and these
tests prove that a dropped, duplicated, renamed or re-shaped feature is still
refused - including a dropped or renamed STAND_ drill, which is the hole the
panel mount frame is carried by.

The gauge model is tested in BOTH plane orders, because the current stack has the
pipe tip BEHIND the board face (16.5 mm against 10.6 mm) and the earlier code
raised instead of drawing that case.
"""
from __future__ import annotations

import copy
import math
import unittest

import bracket_geometry  # noqa: F401  (makes the sibling modules importable)
import step_assembly as sa
import step_geometry as sg


def layout() -> dict:
    return sg.load_layout()


def mutated(**changes) -> dict:
    """A deep copy of the canonical data with named features changed."""
    data = copy.deepcopy(layout())
    if "drop_aperture" in changes:
        data["panelApertures"] = [a for a in data["panelApertures"]
                                  if a["name"] != changes["drop_aperture"]]
    if "rename_aperture" in changes:
        for a in data["panelApertures"]:
            if a["name"] == changes["rename_aperture"]:
                a["name"] = changes["new_name"]
    if "duplicate_aperture" in changes:
        for a in list(data["panelApertures"]):
            if a["name"] == changes["duplicate_aperture"]:
                twin = copy.deepcopy(a)
                twin["name"] = twin["name"] + " COPY"
                data["panelApertures"].append(twin)
    if "drop_drill" in changes:
        data["panelDrills"] = [d for d in data["panelDrills"]
                               if d["name"] != changes["drop_drill"]]
    if "rename_drill" in changes:
        for d in data["panelDrills"]:
            if d["name"] == changes["rename_drill"]:
                d["name"] = changes["new_name"]
    if "resize_aperture" in changes:
        for a in data["panelApertures"]:
            if a["name"] == changes["resize_aperture"]:
                a["width"] = changes["width"]
                a["height"] = changes["width"]
    if "move_aperture" in changes:
        for a in data["panelApertures"]:
            if a["name"] == changes["move_aperture"]:
                a["x"] = changes["x"]
    return data


class PanelIdentityTest(unittest.TestCase):
    """20 apertures + 14 drills, matched by identity, order and shape."""

    def test_the_plan_is_the_current_canonical_set(self) -> None:
        plan = sa.panel_feature_plan(layout())
        self.assertEqual(plan["apertures"], 20)
        self.assertEqual(plan["drills"], 14)
        self.assertEqual(plan["total"], 34)
        names = [f["name"] for f in plan["features"] if f["kind"] == "aperture"]
        self.assertEqual(names, list(sa.PANEL_APERTURE_IDENTITIES))
        drills = [f["name"] for f in plan["features"] if f["kind"] == "drill"]
        self.assertEqual(drills, list(sa.PANEL_DRILL_IDENTITIES))
        # The count is DERIVED from the two canonical arrays, never typed.
        data = layout()
        self.assertEqual(plan["total"],
                         len(data["panelApertures"]) + len(data["panelDrills"]))

    def test_the_drill_set_is_the_feedthrough_rack_and_stand_frame(self) -> None:
        """Six feedthrough screws + four rails + four panel mounts, by name."""
        identities = list(sa.PANEL_DRILL_IDENTITIES)
        self.assertEqual(len(identities), 14)
        self.assertEqual([n for n in identities if n.startswith("USB_")],
                         ["USB_0_-1", "USB_0_1", "USB_1_-1", "USB_1_1",
                          "USB_2_-1", "USB_2_1"])
        self.assertEqual([n for n in identities if n.startswith("RACK_")],
                         ["RACK_1", "RACK_2", "RACK_3", "RACK_4"])
        self.assertEqual([n for n in identities if n.startswith("STAND_")],
                         ["STAND_1", "STAND_2", "STAND_3", "STAND_4"])

    def test_the_stand_drills_are_the_panel_mount_frame(self) -> None:
        """The four STAND_ holes sit on PANEL_MOUNT_POINTS, at the M3 clearance."""
        data = layout()
        stand = {d["name"]: d for d in data["panelDrills"]
                 if str(d["name"]).startswith("STAND_")}
        points = data["panelMountPoints"]
        self.assertEqual(len(stand), 4)
        self.assertEqual(len(points), 4)
        for index, point in enumerate(points, start=1):
            drill = stand[f"STAND_{index}"]
            self.assertAlmostEqual(float(drill["x"]), float(point["x"]), places=9)
            self.assertAlmostEqual(float(drill["y"]), float(point["y"]), places=9)
            self.assertAlmostEqual(float(drill["diameter"]),
                                   float(data["panelStandoff"]["holeDiameter"]),
                                   places=9)
        # ONE frame, symmetric in BOTH axes about the panel centre.
        self.assertEqual(sorted(float(p["x"]) for p in points), [-71.5, -71.5, 71.5, 71.5])
        self.assertEqual(sorted(float(p["y"]) for p in points), [-52.0, -52.0, 52.0, 52.0])

    def test_the_panel_standoff_length_is_the_derived_nominal(self) -> None:
        """9.00 mm = the 10.60 mm panel front datum minus the 1.60 mm panel."""
        data = layout()
        standoff = data["panelStandoff"]
        self.assertAlmostEqual(float(standoff["length"]),
                               float(data["panelToFrontBoardGap"]), places=9)
        self.assertAlmostEqual(
            float(standoff["length"]),
            float(data["provisionalZ"]["frontBoardTop"])
            - float(data["provisionalZ"]["panelRear"]), places=9)
        self.assertAlmostEqual(float(standoff["length"]), 9.0, places=9)
        self.assertEqual(standoff["family"], "M3 threaded, adjustable")

    def test_the_seven_button_openings_are_the_switch_set(self) -> None:
        plan = sa.panel_feature_plan(layout())
        buttons = [f["name"] for f in plan["features"]
                   if f["kind"] == "aperture" and f["name"].endswith("BUTTON")]
        self.assertEqual(sorted(buttons), sorted(sa.PANEL_BUTTON_IDENTITIES))
        self.assertEqual(len(buttons), 7, "SW1-SW7: one geometry, seven openings")

    def test_every_feature_carries_its_cut_volume_and_probes(self) -> None:
        plan = sa.panel_feature_plan(layout())
        thickness = layout()["panel"]["thickness"]
        for feature in plan["features"]:
            self.assertGreater(feature["area_mm2"], 0.0)
            # Both sides are rounded to 6 decimals in the plan, so compare with a
            # 1e-4 mm^3 absolute tolerance rather than a decimal-place count.
            self.assertAlmostEqual(feature["cut_volume_mm3"],
                                   feature["area_mm2"] * thickness, delta=1e-4)
            self.assertEqual(len(feature["void"]), 2)
            # a circle/rect probe is always inside the panel stock
            self.assertLess(abs(feature["void"][0]), layout()["panel"]["width"] / 2)
            self.assertLess(abs(feature["void"][1]), layout()["panel"]["height"] / 2)

    def test_the_lcd_polygon_is_the_rounded_rectangle(self) -> None:
        plan = sa.panel_feature_plan(layout())
        lcd = [f for f in plan["features"] if f["name"] == "LCD_APERTURE"][0]
        self.assertEqual(lcd["spec"]["shape"], "polygon")
        self.assertEqual(len(lcd["spec"]["points"]), sa.PANEL_LCD_OUTLINE_POINTS)
        cut = layout()["lcdCutout"]
        # The shoelace area of a rounded rectangle is the rectangle minus the
        # corner relief: smaller than w*h and larger than w*h minus the corners.
        rect = cut["width"] * cut["height"]
        self.assertLess(lcd["area_mm2"], rect)
        self.assertGreater(lcd["area_mm2"], 0.98 * rect)

    def test_a_dropped_aperture_is_refused(self) -> None:
        with self.assertRaises(bracket_geometry.GeometryError) as raised:
            sa.panel_feature_plan(mutated(drop_aperture="NAV"))
        self.assertIn("expected set/order", str(raised.exception))

    def test_a_renamed_aperture_is_refused(self) -> None:
        with self.assertRaises(bracket_geometry.GeometryError) as raised:
            sa.panel_feature_plan(mutated(rename_aperture="NAV", new_name="NAV SWITCH"))
        self.assertIn("expected set/order", str(raised.exception))

    def test_a_duplicated_aperture_is_refused(self) -> None:
        with self.assertRaises(bracket_geometry.GeometryError):
            sa.panel_feature_plan(mutated(duplicate_aperture="REC BUTTON"))

    def test_a_dropped_drill_is_refused(self) -> None:
        with self.assertRaises(bracket_geometry.GeometryError):
            sa.panel_feature_plan(mutated(drop_drill="RACK_4"))

    def test_a_dropped_stand_drill_is_refused(self) -> None:
        """A panel-mount hole may not disappear silently."""
        with self.assertRaises(bracket_geometry.GeometryError) as raised:
            sa.panel_feature_plan(mutated(drop_drill="STAND_2"))
        self.assertIn("expected set/order", str(raised.exception))

    def test_a_renamed_stand_drill_is_refused(self) -> None:
        with self.assertRaises(bracket_geometry.GeometryError) as raised:
            sa.panel_feature_plan(mutated(rename_drill="STAND_3",
                                          new_name="STANDOFF_3"))
        self.assertIn("expected set/order", str(raised.exception))

    def test_a_moved_stand_drill_is_refused(self) -> None:
        """The four holes are one frame: a moved hole is not the canonical set."""
        data = layout()
        data["panelMountHoles"] = [
            dict(hole, x=hole["x"] + 2.0) if hole["name"] == "STAND_1" else hole
            for hole in data["panelMountHoles"]]
        data["panelDrills"] = [
            dict(drill, x=drill["x"] + 2.0) if drill["name"] == "STAND_1" else drill
            for drill in data["panelDrills"]]
        with self.assertRaises(bracket_geometry.GeometryError):
            sa.panel_feature_plan(data)

    def test_a_resized_button_opening_is_refused(self) -> None:
        with self.assertRaises(bracket_geometry.GeometryError) as raised:
            sa.panel_feature_plan(mutated(resize_aperture="REC BUTTON", width=12.0))
        self.assertIn("12.25", str(raised.exception))

    def test_the_plan_carries_each_feature_position(self) -> None:
        """The plan's probe carries the aperture position to the CAD layer.

        This does NOT prove that a moved control is rejected: identity alone
        cannot see a move. It proves the plan hands the position through, which is
        what the CAD build's per-cut volume and void/material probes compare
        against. The built board's own positions are pinned by the canonical
        export tests (`panel-cutouts.test.ts`), not here.
        """
        moved = mutated(move_aperture="NAV", x=53.0)
        plan = sa.panel_feature_plan(moved)
        nav = [f for f in plan["features"] if f["name"] == "NAV"][0]
        self.assertAlmostEqual(nav["void"][0], 53.0, places=9)


class ReachGaugeModelTest(unittest.TestCase):
    """The gauge draws the two nominal planes in EITHER order, with a sign."""

    def test_the_current_stack_puts_the_tip_behind_the_board(self) -> None:
        dims = sa.reach_gauge_dimensions(layout())
        self.assertAlmostEqual(dims["tall"], 16.5, places=9)
        self.assertAlmostEqual(dims["low"], 10.6, places=9)
        self.assertAlmostEqual(dims["step"], 5.9, places=9)
        self.assertAlmostEqual(dims["signed_board_minus_tip"], -5.9, places=9)
        self.assertEqual(dims["direction"], "tip_behind_board")
        self.assertTrue(dims["penetrates_board_plane"])
        # the step is a magnitude and can never be negative
        self.assertGreaterEqual(dims["step"], 0.0)

    def test_a_pipe_in_front_of_the_board_is_the_same_model(self) -> None:
        """Plane order 2: a short pipe must not be refused any more."""
        data = copy.deepcopy(layout())
        data["lightPipeReach"]["nominalTipZ"] = 8.0
        dims = sa.reach_gauge_dimensions(data)
        self.assertAlmostEqual(dims["tall"], 10.6, places=9)
        self.assertAlmostEqual(dims["low"], 8.0, places=9)
        self.assertAlmostEqual(dims["step"], 2.6, places=9)
        self.assertAlmostEqual(dims["signed_board_minus_tip"], 2.6, places=9)
        self.assertEqual(dims["direction"], "tip_in_front_of_board")
        self.assertFalse(dims["penetrates_board_plane"])

    def test_a_flush_pipe_is_reported_as_flush_and_not_as_a_fit(self) -> None:
        data = copy.deepcopy(layout())
        data["lightPipeReach"]["nominalTipZ"] = data["lightPipeReach"]["boardNearFaceZ"]
        dims = sa.reach_gauge_dimensions(data)
        self.assertEqual(dims["step"], 0.0)
        self.assertEqual(dims["signed_board_minus_tip"], 0.0)
        self.assertEqual(dims["direction"], "tip_flush_with_board")
        self.assertFalse(dims["penetrates_board_plane"])
        self.assertIn("Not an LED gap", dims["step_meaning"])

    def test_the_gauge_never_claims_a_gap(self) -> None:
        dims = sa.reach_gauge_dimensions(layout())
        text = dims["step_meaning"].lower()
        self.assertIn("not an led gap", text)
        self.assertIn("not a fit", text)
        self.assertNotIn("optical gap is", text)

    def test_impossible_planes_are_refused(self) -> None:
        data = copy.deepcopy(layout())
        data["lightPipeReach"]["nominalTipZ"] = 0.0
        with self.assertRaises(bracket_geometry.GeometryError):
            sa.reach_gauge_dimensions(data)


if __name__ == "__main__":
    unittest.main()

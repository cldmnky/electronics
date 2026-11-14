#!/usr/bin/env python3
"""Tests for the three printed front-panel parts.

Run from this directory (no CAD needed for anything but the rebuild tests):

    cd mechanical/printed && python3 -m unittest test_printed_parts -v
    cd mechanical/printed && /tmp/cq-venv/bin/python -m unittest test_printed_parts -v

Each test states the rule it protects. The mutation tests at the bottom take a
reviewed input - the panel aperture, the ALPS keytop fitting area, the shaft
flat, the display opening or the printed B3F cap block in ``lib/layout.ts`` -
and prove that the current rules REJECT the wrong value instead of quietly
exporting a part that does not fit.
"""
from __future__ import annotations

import math
import unittest
from dataclasses import replace
from pathlib import Path

import printed_geometry as pg

HERE = Path(__file__).resolve().parent


def layout() -> dict:
    return pg.load_layout()


def layout_source() -> pg.LayoutSource:
    return pg.layout_source()


def keytop() -> pg.NavKeytop:
    return pg.nav_keytop(layout(), layout_source())


def knob() -> pg.Knob:
    return pg.knob(layout(), layout_source())


class CanonicalDataTest(unittest.TestCase):
    """The parts read the generated export and the canonical TS source."""

    def test_layout_json_is_the_generated_export(self) -> None:
        data = layout()
        self.assertEqual(data["units"], "mm")
        self.assertIn("Generated from tscircuit/lib/layout.ts", data["note"])
        for key in ("panel", "provisionalZ", "panelApertures", "panelMountHoles",
                    "frontPcb", "display"):
            self.assertIn(key, data)

    def test_missing_layout_file_fails_loudly(self) -> None:
        with self.assertRaises(pg.GeometryError):
            pg.load_layout(Path("/nonexistent/layout.json"))

    def test_missing_layout_source_fails_loudly(self) -> None:
        with self.assertRaises(pg.GeometryError):
            pg.read_layout_ts(Path("/nonexistent/layout.ts"))

    def test_panel_stack_is_the_documented_one(self) -> None:
        stack = pg.panel_stack(layout())
        self.assertAlmostEqual(stack.panel_front_z, 0.0, places=9)
        self.assertAlmostEqual(stack.panel_rear_z, 1.6, places=9)
        self.assertAlmostEqual(stack.board_top_z, 10.6, places=9)
        self.assertAlmostEqual(stack.gap, 9.0, places=9)

    def test_apertures_come_from_the_export_and_match_the_source(self) -> None:
        data, src = layout(), layout_source()
        for name in ("NAV", "VALUE ENCODER"):
            ap = pg.aperture(data, name)
            self.assertAlmostEqual(ap.diameter, src.panel_control_aperture, places=6,
                                   msg=f"{name} aperture does not match lib/layout.ts")
        self.assertAlmostEqual(pg.aperture(data, "NAV").diameter, 7.2, places=9)

    def test_display_opening_edge_is_derived_from_the_source(self) -> None:
        src = layout_source()
        self.assertAlmostEqual(src.display_centre_x, -10.0, places=9)
        self.assertAlmostEqual(src.display_opening_width, 106.5, places=9)
        self.assertAlmostEqual(src.display_opening_right, 43.25, places=9)
        self.assertAlmostEqual(src.display_opening_right,
                               src.front_board_opening_right, places=3,
                               msg="the parsed opening edge disagrees with the comment "
                                   "lib/layout.ts prints beside it")
        self.assertAlmostEqual(src.knob_column_x, 52.0, places=9)
        self.assertAlmostEqual(src.knob_body_diameter, 12.5, places=9)

    def test_export_and_source_agree_today(self) -> None:
        """A stale export is reported; the parts never silently use it."""
        notices = pg.layout_export_notices(layout(), layout_source())
        self.assertEqual(notices, [],
                         "mechanical/layout.json is out of date against lib/layout.ts")


class NavKeytopTest(unittest.TestCase):
    """The ALPS SKRHABE010 keytop rules."""

    def setUp(self) -> None:
        self.data = layout()
        self.g = keytop()

    def test_the_whole_cap_assembles_through_the_aperture(self) -> None:
        """Everything in front of the flange is the leading end: it must pass."""
        self.assertAlmostEqual(self.g.neck_diameter, 7.0, places=9)
        self.assertGreater(self.g.aperture_diameter, self.g.neck_diameter)
        self.assertAlmostEqual(self.g.aperture_clearance, 0.2, places=9)

    def test_flange_traps_the_cap_on_the_panel_rear_face(self) -> None:
        self.assertGreater(self.g.flange_diameter, self.g.aperture_diameter)
        self.assertAlmostEqual(self.g.flange_front_z, self.g.stack.panel_rear_z, places=9)
        self.assertAlmostEqual(self.g.bearing_ring, 1.2, places=9)
        self.assertGreaterEqual(self.g.bearing_ring, pg.PRINT_MIN_WALL)

    def test_socket_clears_the_stem_inside_the_alps_fitting_area(self) -> None:
        self.assertAlmostEqual(self.g.socket_bore, 3.2, places=9)
        self.assertGreater(self.g.socket_bore, self.g.stem_diameter)
        self.assertAlmostEqual(self.g.socket_depth, 1.5, places=9)
        self.assertLessEqual(self.g.socket_depth, 1.7 + 1e-9)
        self.assertGreaterEqual(self.g.socket_wall, pg.PRINT_MIN_WALL)

    def test_cap_has_axial_free_play_at_rest(self) -> None:
        self.assertAlmostEqual(self.g.free_play, 0.15, places=9)
        self.assertGreaterEqual(self.g.free_play, 0.1 - 1e-9)
        self.assertLessEqual(self.g.free_play, 0.2 + 1e-9)
        # The socket must still reach past the stem top, or it just rests on it.
        self.assertGreater(self.g.stem_engagement, 1.0)
        self.assertAlmostEqual(self.g.socket_mouth_z, 6.95, places=9)

    def test_head_is_three_to_five_mm_proud_of_the_panel_front(self) -> None:
        self.assertGreaterEqual(self.g.head_height, 3.0 - 1e-9)
        self.assertLessEqual(self.g.head_height, 5.0 + 1e-9)
        self.assertAlmostEqual(self.g.head_front_z, -4.0, places=9)

    def test_cap_fits_the_panel_to_board_gap(self) -> None:
        self.assertLess(self.g.socket_mouth_z, self.g.stack.board_top_z)
        self.assertGreaterEqual(self.g.switch_body_clearance, 0.5)
        self.assertGreaterEqual(self.g.gap_below, 0.5)
        # The flange hangs in the gap, so it cannot be deeper than the gap.
        self.assertLess(self.g.flange_rear_z, self.g.stack.board_top_z)

    def test_flange_hangs_clear_of_the_panel_hardware(self) -> None:
        clearances = pg.panel_feature_clearances(
            self.data, self.g.centre, self.g.flange_diameter / 2, skip=self.g.aperture.name)
        self.assertTrue(clearances, "no panel feature was compared with the flange")
        self.assertGreater(clearances[0][1], 0.0,
                           f"flange overlaps {clearances[0][0]}")


class KnobTest(unittest.TestCase):
    """The Bourns PEC11R value-knob rules."""

    def setUp(self) -> None:
        self.k = knob()

    def test_bore_accepts_the_d6_shaft(self) -> None:
        self.assertAlmostEqual(self.k.shaft_diameter, 6.0, places=9)
        self.assertAlmostEqual(self.k.bore_diameter, 6.2, places=9)
        self.assertAlmostEqual(self.k.bore_clearance, 0.2, places=9)
        self.assertGreaterEqual(self.k.wall, pg.PRINT_MIN_WALL)

    def test_bore_flat_clears_the_shaft_flat(self) -> None:
        # Datasheet flat depth C = 0.5 from a D6.0 shaft: the plane is 2.5 from the axis.
        self.assertAlmostEqual(self.k.shaft_flat_plane, 2.5, places=9)
        self.assertAlmostEqual(self.k.bore_flat_plane, 2.6, places=9)
        self.assertAlmostEqual(self.k.bore_flat_clearance, 0.1, places=9)
        self.assertGreater(self.k.bore_flat_clearance, 0.0)

    def test_knob_height_is_in_band_and_covers_the_shaft_tip(self) -> None:
        self.assertGreaterEqual(self.k.height, 12.0 - 1e-9)
        self.assertLessEqual(self.k.height, 16.0 + 1e-9)
        self.assertAlmostEqual(self.k.shaft_tip_z, -17.9, places=9)
        self.assertAlmostEqual(self.k.front_z, -18.7, places=9)
        self.assertGreater(self.k.tip_cover, 0.0,
                           "the shaft would stand proud of the knob's front face")

    def test_rear_face_clears_the_panel_front_and_the_nut(self) -> None:
        self.assertAlmostEqual(self.k.rear_z, -2.7, places=9)
        self.assertAlmostEqual(self.k.rear_clearance, 2.7, places=9)
        self.assertGreaterEqual(self.k.nut_clearance, 0.0)
        self.assertLess(self.k.rear_z, self.k.stack.panel_front_z)

    def test_knob_keeps_the_layout_display_clearance(self) -> None:
        self.assertAlmostEqual(self.k.left_edge, 45.75, places=9)
        self.assertAlmostEqual(self.k.display_opening_right, 43.25, places=9)
        self.assertAlmostEqual(self.k.display_clearance, pg.KNOB_TO_DISPLAY_MIN, places=9)
        self.assertAlmostEqual(self.k.display_clearance, 2.5, places=9)

    def test_grub_screw_lands_on_the_shaft_flat(self) -> None:
        self.assertAlmostEqual(self.k.grub_z, -8.7, places=9)
        self.assertGreater(self.k.grub_pilot_length, 2.0)
        self.assertGreater(self.k.grub_tip_protrusion, 0.0,
                           "an M3 set screw would not reach the shaft")
        # The screw and the bore flat share the flat's normal, so the screw bears
        # on the flat wherever the encoder is clocked.
        self.assertAlmostEqual(self.k.flat_angle_deg, pg.KNOB_FLAT_ANGLE_DEG, places=9)

    def test_grip_keeps_the_nominal_od_on_both_axes(self) -> None:
        for axis in (0.0, 90.0, 180.0, 270.0):
            self.assertTrue(self.k.has_land_at(axis),
                            f"a flute trough on the {axis} deg axis shrinks the printed OD")
        self.assertTrue(self.k.has_land_at(self.k.flat_angle_deg))
        self.assertAlmostEqual(self.k.flute_cutter_radius, 7.4, places=9)


def printed_button_cap() -> pg.ButtonCap:
    return pg.button_cap(layout(), layout_source())


class ButtonCapTest(unittest.TestCase):
    """The printed B3F-4055 button-cap rules (7 off, SW1-SW7)."""

    def setUp(self) -> None:
        self.data = layout()
        self.src = layout_source()
        self.g = printed_button_cap()

    def test_derived_dimensions_match_the_layout_source(self) -> None:
        bc = self.src.button_cap
        self.assertTrue(bc.implemented, "lib/layout.ts still marks the cap as intent")
        self.assertAlmostEqual(self.g.cutout_square, bc.cutout_square, places=9)
        self.assertAlmostEqual(self.g.plate_square, bc.square, places=9)
        self.assertAlmostEqual(self.g.locator_square, bc.locator_square, places=9)
        self.assertAlmostEqual(self.g.locator_depth, bc.locator_depth, places=9)
        self.assertAlmostEqual(self.g.proud_above_panel_front,
                               bc.proud_above_panel_front, places=9)
        self.assertAlmostEqual(self.g.plunger_face_z, bc.underside_z, places=9)
        self.assertAlmostEqual(self.g.plunger_face_z, bc.locator_floor_z, places=9)
        self.assertAlmostEqual(self.g.top_z, bc.top_z, places=9)
        self.assertAlmostEqual(self.g.rear_face_z, bc.rear_face_z, places=9)
        self.assertAlmostEqual(self.g.height, bc.height, places=9)
        self.assertAlmostEqual(self.g.cap_extension_above_switch,
                               bc.cap_extension_above_switch, places=9)
        self.assertAlmostEqual(self.g.clearance_per_side,
                               bc.clearance_to_cutout_per_side, places=9)
        self.assertAlmostEqual(self.g.locator_clearance_per_side,
                               bc.locator_clearance_per_side, places=9)

    def test_the_part_is_the_designed_plate(self) -> None:
        self.assertAlmostEqual(self.g.plate_square, 11.8, places=9)
        self.assertAlmostEqual(self.g.locator_square, 3.85, places=9)
        self.assertAlmostEqual(self.g.locator_depth, 0.6, places=9)
        self.assertAlmostEqual(self.g.height, 4.7, places=9)
        self.assertEqual(self.g.count, 7)
        # The part is a plain plate minus a plain recess: the analytic volume is
        # an independent check on the mesh and STEP volumes.
        self.assertAlmostEqual(
            self.g.volume,
            11.8 ** 2 * 4.7 - 3.85 ** 2 * 0.6, places=6)

    def test_plate_clears_the_12_25_cutout(self) -> None:
        self.assertAlmostEqual(self.g.cutout_square, 12.25, places=9)
        self.assertAlmostEqual(self.g.clearance_per_side, 0.225, places=9)
        self.assertAlmostEqual(
            self.g.plate_square + 2 * self.g.clearance_per_side,
            self.g.cutout_square, places=9)
        # No skirt can pass the panel: the plate also fits inside the 12.0 body.
        self.assertLess(self.g.plate_square, self.g.switch_body_square)

    def test_locator_clears_the_3_8_plunger(self) -> None:
        self.assertAlmostEqual(self.g.plunger_square, 3.8, places=9)
        self.assertAlmostEqual(self.g.locator_clearance_per_side, 0.025, places=9)
        self.assertAlmostEqual(
            self.g.locator_square,
            self.g.plunger_square + 2 * self.g.locator_clearance_per_side, places=9)

    def test_top_is_proud_of_the_panel_front(self) -> None:
        self.assertAlmostEqual(self.g.plunger_face_z, 3.3, places=9)
        self.assertAlmostEqual(self.g.rear_face_z, 3.9, places=9)
        self.assertAlmostEqual(self.g.top_z, -0.8, places=9)
        self.assertAlmostEqual(self.g.proud_above_panel_front, 0.8, places=9)
        self.assertLess(self.g.top_z, self.g.stack.panel_front_z)
        self.assertAlmostEqual(self.g.top_above_board, 11.4, places=9)
        self.assertAlmostEqual(self.g.cap_extension_above_switch, 4.1, places=9)
        self.assertGreaterEqual(self.g.cap_extension_above_switch,
                                self.g.switch_top_to_panel_front)

    def test_height_is_the_three_term_sum(self) -> None:
        self.assertAlmostEqual(self.g.three_term_height, 4.7, places=9)
        self.assertAlmostEqual(self.g.height, self.g.three_term_height, places=9)
        self.assertAlmostEqual(
            self.g.height,
            self.g.cap_extension_above_switch + self.g.locator_depth, places=9)

    def test_no_geometry_hangs_below_the_plunger_face_beyond_the_locator(self) -> None:
        # The locator ring is the only material behind the plunger face: the cap
        # rear face is exactly locatorDepth behind it, and nothing reaches past
        # that plane into the switch body.
        self.assertAlmostEqual(
            self.g.rear_face_z,
            self.g.plunger_face_z + self.g.locator_depth, places=9)
        self.assertLess(self.g.rear_face_z, self.g.stack.board_top_z)
        self.assertAlmostEqual(self.g.envelope.z[0], self.g.top_z, places=9)
        self.assertAlmostEqual(self.g.envelope.z[1], self.g.rear_face_z, places=9)
        # The builder's probe set carries the same rule into the solid: every
        # sample on the switch side of the rear face must be void.
        import button_cap as builder
        probes = {name: (want, points) for name, want, points in builder.probe_points(self.g)}
        self.assertIn("nothing hangs below the cap rear face into the switch", probes)
        want, points = probes["nothing hangs below the cap rear face into the switch"]
        self.assertFalse(want)
        self.assertTrue(points)
        for _, _, z in points:
            self.assertGreater(z, self.g.rear_face_z)
        # ... and nothing exists in the 0.225 mm band outside the plate either:
        # that is the no-skirt rule the 12.25 mm cutout forces.
        self.assertIn("no skirt in the cutout clearance band", probes)
        want, points = probes["no skirt in the cutout clearance band"]
        self.assertFalse(want)
        for x, y, _ in points:
            self.assertTrue(abs(x) > self.g.plate_square / 2
                            or abs(y) > self.g.plate_square / 2)

    def test_every_button_opening_is_the_same_cutout(self) -> None:
        self.assertEqual(self.g.count, 7)
        self.assertEqual({ap.width for ap in self.g.apertures}, {12.25})
        self.assertEqual({ap.height for ap in self.g.apertures}, {12.25})

    def test_validate_reports_the_provisional_gates(self) -> None:
        notices = pg.validate_button_cap(self.g, self.data)
        text = " ".join(notices)
        self.assertIn("control_actuator", text)
        self.assertIn("7 off", text)
        self.assertIn("B32-13x0", text)


class MutationTest(unittest.TestCase):
    """The reviewed inputs, warped: every rule below must REJECT the part."""

    def test_shrinking_the_aperture_makes_the_neck_no_longer_fit(self) -> None:
        g = keytop()
        shrunk = replace(g, aperture=pg.Circle(g.aperture.x, g.aperture.y,
                                               g.neck_diameter, g.aperture.name))
        with self.assertRaises(pg.GeometryError) as raised:
            pg.validate_nav_keytop(shrunk, layout())
        self.assertIn("sliding fit", str(raised.exception))

    def test_a_socket_tighter_than_the_stem_fails(self) -> None:
        g = replace(keytop(), socket_bore=pg.NAV_STEM_DIAMETER)
        with self.assertRaises(pg.GeometryError):
            pg.validate_nav_keytop(g, layout())

    def test_a_socket_deeper_than_the_alps_fitting_area_fails(self) -> None:
        base = keytop()
        g = replace(base, socket_mouth_z=base.socket_floor_z + 1.75)
        with self.assertRaises(pg.GeometryError) as raised:
            pg.validate_nav_keytop(g, layout())
        self.assertIn("keytop fitting area", str(raised.exception))

    def test_zero_free_play_fails(self) -> None:
        base = keytop()
        g = replace(base, socket_floor_z=base.stem_top_z)
        with self.assertRaises(pg.GeometryError) as raised:
            pg.validate_nav_keytop(g, layout())
        self.assertIn("free play", str(raised.exception))

    def test_a_cap_that_seats_on_the_switch_body_fails(self) -> None:
        base = keytop()
        g = replace(base, socket_mouth_z=base.switch_body_top_z - 0.1)
        with self.assertRaises(pg.GeometryError):
            pg.validate_nav_keytop(g, layout())

    def test_a_head_taller_than_five_mm_fails(self) -> None:
        g = replace(keytop(), head_height=5.5)
        with self.assertRaises(pg.GeometryError):
            pg.validate_nav_keytop(g, layout())

    def test_a_thin_socket_wall_fails(self) -> None:
        g = replace(keytop(), shank_diameter=3.6)
        with self.assertRaises(pg.GeometryError):
            pg.validate_nav_keytop(g, layout())

    def test_a_bore_without_clearance_fails(self) -> None:
        k = replace(knob(), bore_diameter=pg.ENC_SHAFT_DIAMETER)
        with self.assertRaises(pg.GeometryError):
            pg.validate_knob(k)

    def test_a_bore_flat_through_the_shaft_fails(self) -> None:
        k = replace(knob(), bore_flat_plane=2.4)
        with self.assertRaises(pg.GeometryError):
            pg.validate_knob(k)

    def test_a_knob_that_leaves_the_shaft_proud_fails(self) -> None:
        k = replace(knob(), height=12.0)
        with self.assertRaises(pg.GeometryError) as raised:
            pg.validate_knob(k)
        self.assertIn("shaft tip", str(raised.exception))

    def test_a_knob_that_reaches_into_the_display_rule_fails(self) -> None:
        k = replace(knob(), column_x=51.0)
        with self.assertRaises(pg.GeometryError) as raised:
            pg.validate_knob(k)
        self.assertIn("display opening edge", str(raised.exception))

    def test_a_flute_pattern_that_eats_the_nominal_od_fails(self) -> None:
        k = replace(knob(), flute_count=18)
        with self.assertRaises(pg.GeometryError):
            pg.validate_knob(k)

    def test_a_stale_export_does_not_move_the_part(self) -> None:
        """The export's display position must not feed the clearance rule."""
        data = layout()
        stale = pg.load_layout()
        stale["display"]["x"] = -9.5
        notices = pg.layout_export_notices(stale, layout_source())
        self.assertTrue(any("STALE EXPORT" in notice for notice in notices))
        self.assertEqual(pg.knob(data, layout_source()), pg.knob(stale, layout_source()),
                         "a stale display position changed the printed knob")


class ButtonCapMutationTest(unittest.TestCase):
    """The printed B3F cap inputs, warped: every rule must REJECT the part."""

    def test_a_plate_wider_than_the_cutout_fails(self) -> None:
        g = replace(printed_button_cap(), plate_square=12.30)
        with self.assertRaises(pg.GeometryError) as raised:
            pg.validate_button_cap(g, layout())
        self.assertIn("cutout", str(raised.exception))

    def test_a_locator_tighter_than_the_plunger_fails(self) -> None:
        g = replace(printed_button_cap(), locator_square=3.80)
        with self.assertRaises(pg.GeometryError) as raised:
            pg.validate_button_cap(g, layout())
        self.assertIn("plunger", str(raised.exception))

    def test_a_cap_that_does_not_reach_the_panel_front_fails(self) -> None:
        g = replace(printed_button_cap(), proud_above_panel_front=-0.1)
        with self.assertRaises(pg.GeometryError) as raised:
            pg.validate_button_cap(g, layout())
        self.assertIn("panel front", str(raised.exception))

    def test_a_cap_that_is_not_on_the_plunger_face_fails(self) -> None:
        g = replace(printed_button_cap(), plunger_face_z=3.0)
        with self.assertRaises(pg.GeometryError) as raised:
            pg.validate_button_cap(g, layout())
        self.assertIn("plunger face", str(raised.exception))

    def test_a_part_without_the_locator_depth_fails(self) -> None:
        g = replace(printed_button_cap(), locator_depth=0.0)
        with self.assertRaises(pg.GeometryError) as raised:
            pg.validate_button_cap(g, layout())
        self.assertIn("locator depth", str(raised.exception))

    def test_a_stale_button_opening_fails(self) -> None:
        stale = layout()
        for entry in stale["panelApertures"]:
            if entry.get("name") == "REC BUTTON":
                entry["width"] = 12.0
                entry["height"] = 12.0
        with self.assertRaises(pg.GeometryError) as raised:
            pg.button_cap(stale, layout_source())
        self.assertIn("printed cap cannot fit every switch opening",
                      str(raised.exception))

    def test_a_contradictory_height_comment_fails(self) -> None:
        text = pg.read_layout_ts().replace("// 4.70", "// 4.80", 1)
        with self.assertRaises(pg.GeometryError) as raised:
            pg.parse_layout_ts(text)
        self.assertIn("height", str(raised.exception))

    def test_a_missing_button_cap_block_fails_loudly(self) -> None:
        text = pg.read_layout_ts().replace(
            "export const BUTTON_CAP_PRINTED = {",
            "export const BUTTON_CAP_PRINTED_RETIRED = {", 1)
        with self.assertRaises(pg.GeometryError) as raised:
            pg.parse_layout_ts(text)
        self.assertIn("BUTTON_CAP_PRINTED", str(raised.exception))


class LayoutSourceMutationTest(unittest.TestCase):
    """The display-opening rule must follow lib/layout.ts, not a constant."""

    def mutated(self, *replacements: tuple[str, str]) -> str:
        text = pg.read_layout_ts()
        for old, new in replacements:
            self.assertIn(old, text, f"the mutation target {old!r} is not in lib/layout.ts")
            text = text.replace(old, new, 1)
        return text

    def test_widening_the_opening_fails_the_display_clearance(self) -> None:
        # perSideX 0.25 -> 0.5 widens the opening to 107.0 mm, so its right edge
        # moves to 43.5 and the knob only keeps 2.25 mm.
        text = self.mutated(("perSideX: 0.25", "perSideX: 0.5"),
                            ("perSideX),   // 106.5", "perSideX),   // 107.0"),
                            ("//  43.25", "//  43.5"))
        src = pg.parse_layout_ts(text)
        self.assertAlmostEqual(src.display_opening_right, 43.5, places=9)
        k = pg.knob(layout(), src)
        self.assertAlmostEqual(k.display_clearance, 2.25, places=9)
        with self.assertRaises(pg.GeometryError) as raised:
            pg.validate_knob(k)
        self.assertIn("display opening edge", str(raised.exception))

    def test_moving_the_display_without_updating_the_comment_fails(self) -> None:
        text = self.mutated(("  x: -10.0,", "  x: -9.5,"))
        with self.assertRaises(pg.GeometryError) as raised:
            pg.parse_layout_ts(text)
        self.assertIn("43.75", str(raised.exception))

    def test_widening_the_opening_without_updating_the_comment_fails(self) -> None:
        text = self.mutated(("perSideX: 0.25", "perSideX: 0.5"))
        with self.assertRaises(pg.GeometryError) as raised:
            pg.parse_layout_ts(text)
        self.assertIn("FRONT_DISPLAY_OPENING.width", str(raised.exception))

    def test_a_missing_opening_block_fails_loudly(self) -> None:
        text = self.mutated(("export const FRONT_DISPLAY_OPENING = {",
                             "export const FRONT_DISPLAY_OPENING_RETIRED = {"))
        with self.assertRaises(pg.GeometryError) as raised:
            pg.parse_layout_ts(text)
        self.assertIn("FRONT_DISPLAY_OPENING", str(raised.exception))

    def test_a_missing_knob_body_note_fails_loudly(self) -> None:
        text = self.mutated(("knob body (\u00d812.5 at x = 52", "value control at x = 52"))
        with self.assertRaises(pg.GeometryError) as raised:
            pg.parse_layout_ts(text)
        self.assertIn("knob body", str(raised.exception))


class ArtefactTest(unittest.TestCase):
    """The committed STL files, re-read without a CAD kernel."""

    def check(self, name: str, geometry: pg.NavKeytop | pg.Knob) -> pg.StlReport:
        report = pg.verify_stl(HERE / name, geometry.envelope)
        # A part whose cuts silently failed would be its own bounding cylinder.
        r = (geometry.envelope.x[1] - geometry.envelope.x[0]) / 2
        h = geometry.envelope.z[1] - geometry.envelope.z[0]
        stock = math.pi * r * r * h
        self.assertGreater(report.volume, 0.0)
        self.assertLess(report.volume, 0.9 * stock,
                        f"{name} encloses as much as its bounding cylinder: the cuts "
                        "did not happen")
        self.assertGreater(report.volume, 0.2 * stock,
                        f"{name} is thinner than any part of the design")
        return report

    def test_nav_keytop_stl_is_one_watertight_shell_of_the_right_size(self) -> None:
        report = self.check("nav-keytop.stl", keytop())
        self.assertEqual(report.shells, 1)
        self.assertEqual(report.open_edges, 0)
        self.assertEqual(report.non_manifold_edges, 0)
        self.assertAlmostEqual(report.bbox[2], 7.2, delta=0.01)
        self.assertAlmostEqual(report.bbox[5], 6.95, delta=0.01)

    def test_knob_stl_is_one_watertight_shell_of_the_right_size(self) -> None:
        report = self.check("knob.stl", knob())
        self.assertEqual(report.shells, 1)
        self.assertEqual(report.open_edges, 0)
        self.assertEqual(report.non_manifold_edges, 0)
        self.assertAlmostEqual(report.bbox[0], 45.75, delta=0.01)
        self.assertAlmostEqual(report.bbox[5], -2.7, delta=0.01)

    def test_button_cap_stl_is_one_watertight_shell_of_the_right_size(self) -> None:
        g = printed_button_cap()
        report = pg.verify_stl(HERE / "button-cap.stl", g.envelope, volume=g.volume)
        self.assertEqual(report.shells, 1)
        self.assertEqual(report.open_edges, 0)
        self.assertEqual(report.non_manifold_edges, 0)
        half = g.plate_square / 2
        self.assertAlmostEqual(report.bbox[0], -half, delta=0.01)
        self.assertAlmostEqual(report.bbox[1], half, delta=0.01)
        self.assertAlmostEqual(report.bbox[2], -half, delta=0.01)
        self.assertAlmostEqual(report.bbox[3], half, delta=0.01)
        self.assertAlmostEqual(report.bbox[4], g.top_z, delta=0.01)
        self.assertAlmostEqual(report.bbox[5], g.rear_face_z, delta=0.01)
        # The recess is small, so the mesh must stay just under the plate's
        # bounding volume - but not equal to it, which would mean the cut failed.
        self.assertLess(report.volume, g.plate_square ** 2 * g.height)
        self.assertGreater(report.volume, 0.9 * g.plate_square ** 2 * g.height)

    def test_a_missing_or_foreign_stl_fails_loudly(self) -> None:
        with self.assertRaises(pg.GeometryError):
            pg.inspect_stl(HERE / "no-such-part.stl")


try:  # pragma: no cover - environment dependent
    import cadquery as cq

    CQ_ERROR = None
except Exception as exc:  # pragma: no cover - environment dependent
    cq = None
    CQ_ERROR = str(exc)

SKIP = f"cadquery not importable ({CQ_ERROR})"


@unittest.skipIf(cq is None, SKIP)
class RebuildTest(unittest.TestCase):
    """Rebuild both parts and compare with what is committed."""

    def rebuild(self, module_name: str, geometry, specimen: str):
        module = __import__(module_name)
        stock, features = module.build_solid(cq, geometry)
        body, removed = pg.apply_cuts(cq, stock, features)
        report = pg.check_solid(cq, body, geometry.envelope, removed)
        mesh = pg.inspect_stl(HERE / specimen)
        self.assertEqual(report.solids, 1)
        self.assertEqual(report.shells, 1)
        self.assertAlmostEqual(report.volume, mesh.volume,
                               delta=max(0.01 * report.volume, 0.05),
                               msg=f"the committed {specimen} is not the rebuilt part")
        for what, want_material, points in module.probe_points(geometry):
            pg.probe_material(cq, body, points, want_material, what)
        return report

    def test_rebuilt_nav_keytop_matches_the_committed_files(self) -> None:
        report = self.rebuild("nav_keytop", keytop(), "nav-keytop.stl")
        step_volume = pg.verify_step(cq, HERE / "nav-keytop.step", report.volume)
        self.assertAlmostEqual(step_volume, report.volume, delta=0.05)

    def test_rebuilt_knob_matches_the_committed_files(self) -> None:
        report = self.rebuild("knob", knob(), "knob.stl")
        step_volume = pg.verify_step(cq, HERE / "knob.step", report.volume)
        self.assertAlmostEqual(step_volume, report.volume, delta=0.05)

    def test_rebuilt_button_cap_matches_the_committed_files(self) -> None:
        report = self.rebuild("button_cap", printed_button_cap(), "button-cap.stl")
        step_volume = pg.verify_step(cq, HERE / "button-cap.step", report.volume)
        self.assertAlmostEqual(step_volume, report.volume, delta=0.05)


if __name__ == "__main__":
    unittest.main()

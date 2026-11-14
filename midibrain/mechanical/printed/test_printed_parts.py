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

# CadQuery is OPTIONAL: the pure-geometry rules, the committed-STL checks and the
# artefact/git checks run without it, and only the rebuild and built-solid tests
# are skipped. The probe sits at the top of the module so every decorated class
# can use it.
try:
    import cadquery as cq  # noqa: F401  (used by the CadQuery-dependent tests)

    CQ_ERROR = None
except Exception as exc:  # pragma: no cover - environment dependent
    cq = None
    CQ_ERROR = str(exc)

SKIP = f"cadquery not importable ({CQ_ERROR})"


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
        # The board face is the owner-directed datum: the panel front sits
        # 10.6 mm above it, so the part chain must never carry its own copy.
        self.assertAlmostEqual(stack.board_top_z, stack.panel_front_z + 10.6, places=9)

    def test_apertures_come_from_the_export_and_match_the_source(self) -> None:
        data, src = layout(), layout_source()
        # NAV has had its OWN aperture since 2026-11-14: the replacement switch's
        # Ø7.20 collar stands inside it, so it is no longer the shared control
        # size that the value encoder keeps.
        self.assertAlmostEqual(pg.aperture(data, "NAV").diameter, src.panel_nav_aperture, places=6,
                               msg="NAV aperture does not match lib/layout.ts PANEL_CUTOUTS.nav")
        self.assertAlmostEqual(pg.aperture(data, "VALUE ENCODER").diameter,
                               src.panel_control_aperture, places=6,
                               msg="VALUE ENCODER aperture does not match PANEL_CUTOUTS.control")
        self.assertAlmostEqual(src.panel_nav_aperture, 8.0, places=9)
        self.assertAlmostEqual(src.panel_control_aperture, 7.2, places=9)
        self.assertGreater(src.panel_nav_aperture, src.panel_control_aperture)

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
    """The ALPS RKJXT1F42001 cap rules (owner decision 2026-11-14)."""

    def setUp(self) -> None:
        self.data = layout()
        self.g = keytop()

    def test_the_cap_sits_on_the_projected_shaft_in_front_of_the_panel(self) -> None:
        # 17.4 above the PCB, board top at 10.60 -> the shaft tip is at Z -6.80.
        self.assertAlmostEqual(self.g.shaft_tip_z, -6.8, places=9)
        # The whole cap stays in front of the panel front plane: it never enters
        # the opening (the switch's own collar does).
        self.assertLess(self.g.socket_mouth_z, self.g.stack.panel_front_z)
        self.assertAlmostEqual(self.g.rear_face_above_panel_front, 2.95, places=9)
        self.assertGreater(self.g.rear_face_above_panel_front,
                           pg.NAV_CENTRE_PUSH_TRAVEL + pg.NAV_CENTRE_PUSH_TOLERANCE)

    def test_the_bore_clears_the_flatted_shaft(self) -> None:
        self.assertAlmostEqual(self.g.socket_bore, 2.9, places=9)
        self.assertAlmostEqual(self.g.shaft_diameter, 2.5, places=9)
        self.assertAlmostEqual(self.g.shaft_across_flat, 1.8, places=9)
        loose = pg.nav_socket_clearance(self.g.socket_bore)
        self.assertAlmostEqual(loose["shaft_round_side_mm"], 0.2, places=9)
        # The flat is 0.55 mm from the axis, so the bore is looser on that side.
        self.assertAlmostEqual(loose["flat_plane_from_axis_mm"], 0.55, places=9)
        self.assertAlmostEqual(loose["shaft_flat_side_mm"], 0.9, places=9)
        self.assertFalse(loose["keys_on_the_flat"])
        self.assertGreaterEqual(self.g.socket_wall, pg.PRINT_MIN_WALL)

    def test_cap_has_axial_free_play_at_rest(self) -> None:
        self.assertAlmostEqual(self.g.free_play, 0.15, places=9)
        self.assertGreaterEqual(self.g.free_play, 0.1 - 1e-9)
        self.assertLessEqual(self.g.free_play, 0.2 + 1e-9)
        # The bore must reach well down the shaft, not just kiss its tip.
        self.assertGreater(self.g.shaft_in_socket, self.g.shaft_diameter)
        self.assertAlmostEqual(self.g.socket_floor_z, -6.95, places=9)
        self.assertAlmostEqual(self.g.socket_mouth_z, -2.95, places=9)
        self.assertAlmostEqual(self.g.shaft_in_socket, 3.85, places=9)

    def test_head_is_a_finger_pad_of_three_to_five_mm(self) -> None:
        self.assertGreaterEqual(self.g.head_height, 3.0 - 1e-9)
        self.assertLessEqual(self.g.head_height, 5.0 + 1e-9)
        self.assertAlmostEqual(self.g.head_front_z, -10.95, places=9)
        self.assertGreater(self.g.head_diameter, self.g.shank_diameter)
        self.assertGreaterEqual(self.g.head_ring, pg.PRINT_MIN_WALL - pg.EPS_MM)

    def test_the_total_protrusion_is_the_shaft_plus_the_head(self) -> None:
        # 6.80 mm of shaft stands proud of the 10.60 mm panel front, so the cap
        # cannot be within the old 3-5 mm intent: 10.95 mm in total, derived.
        self.assertAlmostEqual(self.g.proud_of_panel, 10.95, places=9)
        self.assertAlmostEqual(self.g.overall_length, 8.0, places=9)

    def test_the_switch_heights_are_the_drawing_values(self) -> None:
        self.assertAlmostEqual(pg.NAV_HOUSING_TOP_ABOVE_PCB, 8.6, places=9)
        self.assertAlmostEqual(pg.NAV_COLLAR_TOP_ABOVE_PCB, 10.5, places=9)
        self.assertAlmostEqual(pg.NAV_SHAFT_TIP_ABOVE_PCB, 17.4, places=9)
        # The collar pokes 1.5 mm into the opening from the panel REAR face and
        # stays 0.1 mm short of its front face.
        self.assertAlmostEqual(self.g.collar_top_z, 0.1, places=9)
        self.assertAlmostEqual(self.g.stack.panel_front_z - self.g.collar_top_z, -0.1, places=9)
        self.assertAlmostEqual(self.g.collar_clearance, 3.05, places=9)
        # The tabs sit at the panel REAR plane: a zero-gap contact RISK, never
        # a clearance. Asserted so a future edit cannot turn it into one.
        self.assertAlmostEqual(pg.NAV_TAB_TOP_ABOVE_PCB, 9.0, places=9)
        self.assertAlmostEqual(self.g.stack.board_top_z - pg.NAV_TAB_TOP_ABOVE_PCB,
                               self.g.stack.panel_rear_z, places=9)

    def test_the_hand_stays_clear_of_the_panel_hardware(self) -> None:
        clearances = pg.panel_feature_clearances(
            self.data, self.g.centre, self.g.head_diameter / 2, skip=self.g.aperture.name)
        self.assertTrue(clearances, "no panel feature was compared with the cap")
        self.assertGreater(clearances[0][1], 0.0,
                           f"the head overlaps {clearances[0][0]}")

    def test_the_nav_opening_is_its_own_size_not_the_encoder_hole(self) -> None:
        src = layout_source()
        self.assertAlmostEqual(src.panel_nav_aperture, 8.0, places=9)
        self.assertAlmostEqual(src.panel_control_aperture, 7.2, places=9)
        self.assertAlmostEqual(self.g.aperture_diameter, 8.0, places=9)
        # 0.40 mm per side over the Ø7.20 collar, before the routed tolerance.
        self.assertGreater((self.g.aperture_diameter - pg.NAV_COLLAR_DIAMETER) / 2, 0.35)


class KnobTest(unittest.TestCase):
    """The Bourns PEC11R value-knob rules."""

    def setUp(self) -> None:
        self.k = knob()

    def test_bore_accepts_the_d6_shaft(self) -> None:
        self.assertAlmostEqual(self.k.shaft_diameter, 6.0, places=9)
        self.assertAlmostEqual(self.k.bore_diameter, 6.2, places=9)
        self.assertAlmostEqual(self.k.bore_clearance, 0.2, places=9)
        self.assertGreaterEqual(self.k.wall, pg.PRINT_MIN_WALL)

    def test_bore_flat_follows_the_sourced_shaft_flat(self) -> None:
        # SOURCED: page 2 `4.5 +0/-0.05` spans the shaft's top surface to the flat
        # on a D6 +-0.1 shaft, so the flat plane is 6.0 - 4.5 = 1.5 mm from the
        # axis. Page 3 `C 0.5` is the TIP CHAMFER, not the flat depth: the earlier
        # 2.5 mm reading is withdrawn as an error.
        self.assertAlmostEqual(self.k.shaft_flat_plane, 1.5, places=9)
        self.assertAlmostEqual(pg.ENC_SHAFT_TIP_CHAMFER_C, 0.5, places=9)
        self.assertAlmostEqual(pg.ENC_FLAT_FROM_OPPOSITE_SURFACE, 4.5, places=9)
        self.assertAlmostEqual(pg.ENC_FLAT_PLANE_TOLERANCE, 0.1, places=9)
        # The bore's flat still follows the shaft's flat with half the bore
        # clearance, which is the module's own rule.
        self.assertAlmostEqual(self.k.bore_flat_plane, 1.6, places=9)
        self.assertAlmostEqual(self.k.bore_flat_clearance, 0.1, places=9)
        self.assertGreater(self.k.bore_flat_clearance, 0.0)

    def test_knob_height_is_in_band_and_covers_the_shaft_tip(self) -> None:
        self.assertGreaterEqual(self.k.height, 12.0 - 1e-9)
        self.assertLessEqual(self.k.height, 16.0 + 1e-9)
        # L = 15.0 is measured from the SHOULDER (panel Z 4.10), not from the
        # bushing end: the tip is at -10.90, not the withdrawn -18.50.
        self.assertAlmostEqual(self.k.bushing_shoulder_z, 4.1, places=9)
        self.assertAlmostEqual(self.k.shaft_tip_z, -10.9, places=9)
        self.assertAlmostEqual(self.k.front_z, -18.7, places=9)
        self.assertAlmostEqual(self.k.tip_cover, 7.8, places=9)
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

    def test_grub_screw_reach_is_reported_as_a_range(self) -> None:
        self.assertAlmostEqual(self.k.grub_z, -8.7, places=9)
        self.assertGreater(self.k.grub_pilot_length, 2.0)
        # The screw and the bore flat share the flat's normal, so the screw bears
        # on the flat wherever the encoder is clocked.
        self.assertAlmostEqual(self.k.flat_angle_deg, pg.KNOB_FLAT_ANGLE_DEG, places=9)
        # A flush-only tip check cannot prove the screw misses the flat, because
        # an M3 screw may stand proud of the OD. The model reports the range.
        screw = self.k.set_screw
        self.assertAlmostEqual(screw["reach_required_mm"], 0.1, places=9)
        self.assertAlmostEqual(screw["pilot_length_mm"], 4.65, places=9)
        self.assertAlmostEqual(screw["flush_protrusion_mm"], -0.65, places=9)
        self.assertFalse(screw["reaches_flush"])
        # The screw must turn INTO the OD, not stand proud: the outer end sits
        # 0.75 mm BELOW the knob's outer surface when the tip reaches the flat.
        self.assertAlmostEqual(screw["recess_needed_mm"], 0.75, places=9)
        self.assertAlmostEqual(screw["outer_end_below_od_mm"], 0.75, places=9)
        self.assertTrue(screw["reaches_if_recessed"])
        self.assertAlmostEqual(screw["engagement_if_recessed_mm"], 3.9, places=9)
        # The key size is NOT assumed: the 2.50 mm in this module is the tap drill.
        self.assertIn("UNVERIFIED", screw["key_access"])
        self.assertIn("TAP DRILL", screw["key_access"].upper())
        self.assertIn("hex key size is not recorded", screw["key_access"])
        # A longer screw reaches with its outer end flush: a MEASURED choice, not
        # a claim that either screw clamps.
        longer = pg.knob_set_screw_model(self.k, 5.0)
        self.assertTrue(longer["reaches_flush"])
        self.assertAlmostEqual(longer["flush_protrusion_mm"], 0.35, places=9)
        for claim in ("clamping force", "thread strength in plastic"):
            self.assertIn(claim, screw["not_claimed"])

    def test_grip_keeps_the_nominal_od_on_both_axes(self) -> None:
        for axis in (0.0, 90.0, 180.0, 270.0):
            self.assertTrue(self.k.has_land_at(axis),
                            f"a flute trough on the {axis} deg axis shrinks the printed OD")
        self.assertTrue(self.k.has_land_at(self.k.flat_angle_deg))
        self.assertAlmostEqual(self.k.flute_cutter_radius, 7.4, places=9)


class EncoderStackTest(unittest.TestCase):
    """The Bourns stack, parameterised: L15 in the BOM, L20 as a lead only.

    Every number here comes from the sourced shoulder datum (page 2: 6.5 mm
    board-to-shoulder; page 3: L from that shoulder, LB threaded bushing, F from
    the flat's start to the tip, nut 2.0 +-0.2 high over 10.0 +-0.5 across flats,
    washer 0.5 thick, 12.0 +-0.3 OD, 7.1 +-0.3 bore, general linear tolerance
    +-0.3 below 10 mm).
    """

    def setUp(self) -> None:
        self.k = knob()

    def test_the_selected_part_tip_and_bushing_planes(self) -> None:
        enc = self.k.encoder
        self.assertAlmostEqual(enc["shoulder_above_pcb_mm"], 6.5, places=9)
        self.assertAlmostEqual(enc["bushing_tip_above_pcb_mm"], 11.5, places=9)
        self.assertAlmostEqual(enc["shaft_tip_above_pcb_mm"], 21.5, places=9)
        self.assertAlmostEqual(enc["flat_start_above_pcb_mm"], 14.5, places=9)
        self.assertAlmostEqual(enc["panel_front_above_pcb_mm"], 10.6, places=9)

    def test_the_selected_part_cannot_be_nutted(self) -> None:
        enc = self.k.encoder
        self.assertAlmostEqual(enc["thread_past_panel_front_mm"], 0.9, places=9)
        self.assertAlmostEqual(enc["nut_and_washer_nominal_mm"], 2.5, places=9)
        self.assertAlmostEqual(enc["spun_down_spare_nominal_mm"], -1.6, places=9)
        self.assertLess(enc["spun_down_spare_nominal_mm"], 0.0)
        # The washed-out claim "the nut lands" must not come back: the notice says
        # BLOCKER and the encoder stays in the BOM unchanged.
        text = " ".join(pg.validate_knob(self.k))
        self.assertIn("BLOCKER encoder_nut_stack", text)
        self.assertIn("CANNOT be nutted", text)

    def test_the_nut_and_washer_tolerances_are_carried(self) -> None:
        self.assertAlmostEqual(pg.ENC_NUT_THICKNESS, 2.0, places=9)
        self.assertAlmostEqual(pg.ENC_NUT_THICKNESS_TOLERANCE, 0.2, places=9)
        self.assertAlmostEqual(pg.ENC_NUT_ACROSS_FLATS, 10.0, places=9)
        self.assertAlmostEqual(pg.ENC_NUT_ACROSS_FLATS_TOLERANCE, 0.5, places=9)
        self.assertAlmostEqual(pg.ENC_WASHER_THICKNESS, 0.5, places=9)
        self.assertAlmostEqual(pg.ENC_WASHER_OUTSIDE_DIAMETER, 12.0, places=9)
        self.assertAlmostEqual(pg.ENC_WASHER_OUTSIDE_DIAMETER_TOLERANCE, 0.3, places=9)
        self.assertAlmostEqual(pg.ENC_WASHER_BORE, 7.1, places=9)
        self.assertAlmostEqual(pg.ENC_WASHER_BORE_TOLERANCE, 0.3, places=9)
        # The washer has no tolerance of its own: the general <10 mm class applies,
        # so it must never be presented as an exactly-toleranced part.
        self.assertAlmostEqual(pg.ENC_WASHER_THICKNESS_TOLERANCE,
                               pg.ENC_GENERAL_LINEAR_TOLERANCE, places=9)
        self.assertAlmostEqual(self.k.encoder["nut_and_washer_worst_mm"], 3.0, places=9)

    def test_the_push_travel_is_not_guaranteed(self) -> None:
        enc = self.k.encoder
        self.assertAlmostEqual(enc["knob_clearance_nominal_mm"], 0.2, places=9)
        self.assertAlmostEqual(enc["knob_clearance_worst_mm"], -0.3, places=9)
        self.assertAlmostEqual(enc["push_shortfall_nominal_mm"], 0.3, places=9)
        self.assertAlmostEqual(enc["push_shortfall_worst_mm"], 0.8, places=9)
        self.assertAlmostEqual(pg.ENC_PUSH_TRAVEL, 0.5, places=9)
        self.assertAlmostEqual(pg.ENC_PUSH_TRAVEL_TOLERANCE, 0.3, places=9)
        text = " ".join(pg.validate_knob(self.k))
        self.assertIn("BLOCKER control_travel", text)
        # It must not say the switch actuates or does not actuate.
        self.assertIn("does NOT say the switch will or will not actuate", text)

    def test_the_round_shaft_cannot_pass_the_d_bore(self) -> None:
        enc = self.k.encoder
        self.assertAlmostEqual(enc["round_shaft_into_d_bore_mm"], 1.2, places=9)
        self.assertTrue(enc["d_bore_blocks_round_shaft"])
        self.assertAlmostEqual(enc["d_bore_round_interference_mm"], 1.4, places=9)
        text = " ".join(pg.validate_knob(self.k))
        self.assertIn("BLOCKER knob_d_bore", text)
        self.assertIn("cannot be assembled", text)

    def test_the_l20_candidate_is_parameterised_and_not_selected(self) -> None:
        enc = self.k.encoder_candidate_l20
        self.assertAlmostEqual(enc["shaft_length_L_mm"], 20.0, places=9)
        self.assertAlmostEqual(enc["thread_length_LB_mm"], 7.0, places=9)
        self.assertAlmostEqual(enc["flat_length_F_mm"], 10.0, places=9)
        self.assertAlmostEqual(enc["thread_past_panel_front_mm"], 2.9, places=9)
        self.assertAlmostEqual(enc["spun_down_spare_nominal_mm"], 0.4, places=9)
        # 0.4 nominal is NOT a worst-case fit once the general tolerance is applied.
        # Every RECORDED term at its worst: shoulder and LB at the +-0.3 mm
        # general tolerance, nut +0.2 and washer +0.3. The panel thickness
        # tolerance is not recorded and is excluded, so this is a bound of the
        # known terms (not the old optimistic -0.1).
        self.assertAlmostEqual(enc["thread_past_panel_front_worst_mm"], 2.3, places=9)
        self.assertAlmostEqual(enc["spun_down_spare_worst_mm"], -0.7, places=9)
        self.assertLess(enc["spun_down_spare_worst_mm"], 0.0)
        self.assertEqual(enc["threshold_terms_worst_mm"]["panel_thickness_tolerance"],
                         "NOT recorded")
        # and the round-section problem gets worse, not better
        self.assertAlmostEqual(enc["round_shaft_into_d_bore_mm"], 3.2, places=9)
        candidate = pg.ENC_CANDIDATE_LONGER_BUSHING
        self.assertIn("CANDIDATE ONLY", candidate["status"])
        self.assertIn("NOT SELECTED", candidate["status"])
        self.assertIn("C143788", candidate["lead_only_lcsc"])
        self.assertIn("403", candidate["main_shop"])
        self.assertIn("gate", candidate["main_shop"])

    def test_only_sourced_flat_lengths_are_accepted(self) -> None:
        self.assertAlmostEqual(pg.enc_flat_length(15.0), 7.0, places=9)
        self.assertAlmostEqual(pg.enc_flat_length(20.0), 10.0, places=9)
        with self.assertRaises(pg.GeometryError):
            pg.enc_flat_length(17.5)

    def test_the_through_bore_is_declared(self) -> None:
        self.assertTrue(self.k.bore_through)
        # The tip is below the TOP of a through bore, which is not a solid cover.
        self.assertAlmostEqual(self.k.tip_cover, 7.8, places=9)
        self.assertIn("THROUGH BORE", " ".join(pg.validate_knob(self.k)).upper())


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
        # an independent check on the mesh and STEP volumes, and it is DERIVED
        # from the part's own numbers, not typed.
        self.assertAlmostEqual(
            self.g.volume,
            self.g.plate_square ** 2 * self.g.height
            - self.g.locator_square ** 2 * self.g.locator_depth, places=6)

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
        # 10.6 board face - 7.30 plunger = 3.30 plunger face; +0.60 locator =
        # 3.90 rear face; the cap top is 0.80 in front of the panel front.
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


class FitSpecimenTest(unittest.TestCase):
    """Four locator sizes, nothing else changed, no retention claimed."""

    def test_the_specimen_sizes_are_the_documented_ones(self) -> None:
        import button_cap as builder
        self.assertEqual(builder.SPECIMEN_LOCATORS, (3.80, 3.85, 3.90, 4.00))
        names = [p.name for _, p in builder.specimen_paths(builder.SPECIMEN_DIR)]
        self.assertEqual(names, [f"button-cap-specimen-{s:.2f}.stl"
                                 for s in (3.80, 3.85, 3.90, 4.00)])

    def test_a_specimen_is_the_standard_cap_with_one_number_changed(self) -> None:
        from dataclasses import replace
        standard = printed_button_cap()
        for size in (3.80, 3.85, 3.90, 4.00):
            specimen = replace(standard, locator_square=size)
            self.assertAlmostEqual(specimen.locator_square, size, places=9)
            # every other printed dimension is the standard part's
            self.assertAlmostEqual(specimen.plate_square, standard.plate_square, places=9)
            self.assertAlmostEqual(specimen.height, standard.height, places=9)
            self.assertAlmostEqual(specimen.locator_depth, standard.locator_depth, places=9)
            # and the recess is still a clearance, never an interference, at the
            # nominal 3.80 plunger
            self.assertGreaterEqual(specimen.locator_clearance_per_side, 0.0)

    def test_no_specimen_is_presented_as_a_snap(self) -> None:
        import button_cap as builder
        from pathlib import Path
        source = Path(builder.__file__).read_text(encoding="utf-8").lower()
        for line in source.splitlines():
            if "snap" in line:
                low = line.lower()
                self.assertTrue("not a snap" in low or "no snap" in low
                                or "not a retention" in low or "never a snap" in low
                                or "cannot accept" in low
                                or "is a snap or a retention" in low,
                                f"a snap claim appears without a denial: {line}")


class ClaimRecurrenceTest(unittest.TestCase):
    """The claims this review corrected must not creep back into output or docs.

    The rule is about CURRENT claims only: a line that carries an explicit
    WITHDRAWN/SUPERSEDED/history label is allowed to mention the old wording,
    because that is how the record stays honest.
    """

    # Phrases that were FALSE and are now forbidden in active text.
    FORBIDDEN = (
        "closed by design",
        "little enough to hold",
        "cannot fall through",
        "never sees a bending moment",
        "cap top remains undimensioned",
        "bought part's undimensioned property",
        "above omron's 10.0",
        "removes the whole measurement problem",
        "board to panel front",
        "slides on cold",
        "6.5 mm too long",
        "no supports, no bridge",
    )
    HISTORY_LABELS = ("withdrawn", "superseded", "history", "historical", "was withdrawn")

    def _docs(self) -> list[Path]:
        repo = HERE.parent.parent
        return [repo / "mechanical/printed/README.md",
                repo / "mechanical/reference/sources-midibrain.md",
                repo / "docs/mechanical-stack.md",
                repo / "docs/mechanical-cad.md",
                repo / "docs/assembly.md",
                repo / "docs/decisions.md",
                repo / "docs/TODO.md",
                repo / "docs/JLC-ASSEMBLY.md",
                repo / "mechanical/viewer/README.md"]

    def test_the_generated_notices_carry_no_corrected_claim(self) -> None:
        texts = {
            "button cap": " ".join(pg.validate_button_cap(printed_button_cap(),
                                                          layout())),
            "nav keytop": " ".join(pg.validate_nav_keytop(keytop(), layout())),
            "knob": " ".join(pg.validate_knob(knob())),
        }
        for part, text in texts.items():
            low = text.lower()
            for phrase in self.FORBIDDEN:
                self.assertNotIn(phrase, low, f"{part} notice repeats '{phrase}'")

    def test_the_active_docs_carry_no_corrected_claim(self) -> None:
        for path in self._docs():
            self.assertTrue(path.is_file(), f"{path} is missing")
            for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
                low = line.lower()
                if any(label in low for label in self.HISTORY_LABELS):
                    continue
                for phrase in self.FORBIDDEN:
                    self.assertNotIn(phrase, low,
                                     f"{path.name}:{number} repeats '{phrase}'")

    def test_the_verified_b32_reference_is_recorded(self) -> None:
        register = (HERE.parent / "reference" / "sources-midibrain.md").read_text()
        self.assertIn("PCB TOP -> CAP TOP", register)
        self.assertIn("PCB top y 146.00", register)
        self.assertIn("7.3 - 2.8 + 7.0 = 11.5", register)
        self.assertIn("protrusion over the panel", register)
        stack = (HERE.parent.parent / "docs" / "mechanical-stack.md").read_text()
        self.assertIn("0.60 mm BEHIND", stack)
        self.assertIn("1.0 to 2.0 mm", stack)

    def test_the_current_derived_numbers_are_the_ones_quoted(self) -> None:
        g = keytop()
        c = printed_button_cap()
        self.assertAlmostEqual(c.height, 4.70, places=9)
        self.assertAlmostEqual(c.top_above_board, 11.40, places=9)
        self.assertAlmostEqual(c.ring_to_body_rest, 2.40, places=9)
        self.assertAlmostEqual(c.ring_to_body_at_flush, 1.60, places=9)
        self.assertAlmostEqual(g.overall_length, 8.0, places=9)
        self.assertAlmostEqual(g.proud_of_panel, 10.95, places=9)
        self.assertAlmostEqual(g.collar_clearance, 3.05, places=9)
        data = layout()
        # 12.75 / 8.75: the owner's carrier pair. Reaching flush EXTENDS the
        # 8.75 mm spacer to 9.35 mm; it is never cut shorter.
        self.assertAlmostEqual(data["displayStandoff"]["spacerLength"], 8.75, places=9)
        self.assertAlmostEqual(data["displayStandoff"]["planeSeparation"], 12.75, places=9)
        self.assertAlmostEqual(data["provisionalZ"]["displayGlassFront"], 0.6, places=9)
        self.assertAlmostEqual(data["lightPipeReach"]["nominalTipToBoardPlane"], -5.9, places=9)
        self.assertIsNone(data["lightPipeReach"]["opticalGap"])


class MutationTest(unittest.TestCase):
    """The reviewed inputs, warped: every rule below must REJECT the part."""

    def test_a_socket_tighter_than_the_shaft_fails(self) -> None:
        g = replace(keytop(), socket_bore=pg.NAV_SHAFT_DIAMETER)
        with self.assertRaises(pg.GeometryError):
            pg.validate_nav_keytop(g, layout())

    def test_zero_free_play_fails(self) -> None:
        base = keytop()
        g = replace(base, socket_floor_z=base.shaft_tip_z)
        with self.assertRaises(pg.GeometryError) as raised:
            pg.validate_nav_keytop(g, layout())
        self.assertIn("free play", str(raised.exception))

    def test_a_cap_that_reaches_into_the_panel_opening_fails(self) -> None:
        base = keytop()
        g = replace(base, socket_mouth_z=base.stack.panel_rear_z - 0.2)
        with self.assertRaises(pg.GeometryError) as raised:
            pg.validate_nav_keytop(g, layout())
        self.assertIn("aperture", str(raised.exception))

    def test_a_cap_that_meets_the_panel_at_full_travel_fails(self) -> None:
        base = keytop()
        g = replace(base, socket_mouth_z=base.stack.panel_front_z - 0.5)
        with self.assertRaises(pg.GeometryError) as raised:
            pg.validate_nav_keytop(g, layout())
        self.assertIn("push travel", str(raised.exception))

    def test_a_bore_that_barely_reaches_the_shaft_tip_fails(self) -> None:
        base = keytop()
        g = replace(base, socket_floor_z=base.shaft_tip_z - 0.125,
                    socket_mouth_z=base.shaft_tip_z + 0.375)
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
        # The visible end of the shaft is at -10.90 (L from the shoulder). A knob
        # whose front face is behind that tip would leave the metal bare.
        k = replace(knob(), height=12.0, rear_z=1.2)
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
        # D7.00 head in X; the Y extent is the same because the cap is revolved.
        self.assertAlmostEqual(report.bbox[1] - report.bbox[0], 7.0, delta=0.01)
        self.assertAlmostEqual(report.bbox[3] - report.bbox[2], 7.0, delta=0.01)
        self.assertAlmostEqual(report.bbox[0], keytop().centre[0] - 3.5, delta=0.01)
        # Head front (shaft tip -6.80, free play 0.15, head 4.00) and socket
        # mouth (shaft tip + 4.00 - 0.15): the part hangs in front of the panel.
        self.assertAlmostEqual(report.bbox[5], keytop().socket_mouth_z, delta=0.01)
        self.assertAlmostEqual(report.bbox[5], -2.95, delta=0.01)
        self.assertAlmostEqual(report.bbox[4], keytop().head_front_z, delta=0.01)
        self.assertAlmostEqual(report.bbox[4], -10.95, delta=0.01)

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


def tracked_artefacts(repo_root: Path, names) -> tuple[list[str], list[str]]:
    """(tracked, untracked) for the printed artefacts, by PROJECT-relative path.

    The paths are relative to the repository the git index belongs to, i.e.
    ``mechanical/printed/x.stl`` when ``repo_root`` is the ``midibrain`` project
    directory. A path prefixed with ``midibrain/`` is wrong there: git resolves it
    against its top level and reports a tracked file as missing.
    """
    import subprocess
    repo = str(repo_root)
    tracked: list[str] = []
    untracked: list[str] = []
    for name in names:
        rel = f"mechanical/printed/{name}"
        result = subprocess.run(["git", "ls-files", "--error-unmatch", rel],
                                cwd=repo, capture_output=True, text=True, check=False)
        (tracked if result.returncode == 0 else untracked).append(name)
    return tracked, untracked


def git_ignored(repo_root: Path, rel: str) -> bool:
    """True when git ignores a path. Used to prove an artefact cannot hide."""
    import subprocess
    result = subprocess.run(["git", "check-ignore", "-q", rel],
                            cwd=str(repo_root), capture_output=True, text=True, check=False)
    return result.returncode == 0


class ArtefactStageTest(unittest.TestCase):
    """The generated artefacts must exist, and a clean checkout must carry them.

    Two of the six (the button caps) are NEW here; the four actuator files were
    already tracked. ``.gitignore`` ignores ``*.stl``/``*.step`` for the
    OTHER generated CAD, so the six committed part files carry explicit negation
    rules: that makes an unstaged artefact show up in ``git status`` instead of
    disappearing behind the broad ignore. Nothing in this class relaxes because
    of unrelated edits in the tree.
    """

    PARTS = ("nav-keytop.stl", "nav-keytop.step", "knob.stl", "knob.step",
             "button-cap.stl", "button-cap.step")
    # The four that were already tracked before this change.
    PREVIOUSLY_TRACKED = ("nav-keytop.stl", "nav-keytop.step", "knob.stl", "knob.step")
    # The two default artefacts added by this change.
    NEW_DEFAULT = ("button-cap.stl", "button-cap.step")

    @property
    def repo(self) -> Path:
        return HERE.parent.parent

    def test_every_generated_artefact_is_present(self) -> None:
        for name in self.PARTS:
            path = HERE / name
            self.assertTrue(path.is_file(),
                            f"{name} is missing: build it with "
                            f"{name.split('-')[0].replace('-', '_')}.py")
            self.assertGreater(path.stat().st_size, 0)

    def test_the_artefacts_are_not_hidden_by_gitignore(self) -> None:
        """A generated part file must never be silently ignored.

        This is the file-specific check that replaces the old "is the tree dirty"
        allowance: an unrelated edit cannot make a missing artefact look fine,
        because the assertion is about the artefact's own path.
        """
        for name in self.PARTS:
            self.assertFalse(git_ignored(self.repo, f"mechanical/printed/{name}"),
                             f"mechanical/printed/{name} is gitignored: an omitted "
                             "default artefact would vanish instead of showing in "
                             "`git status`")

    def test_the_previously_tracked_artefacts_are_still_tracked(self) -> None:
        tracked, untracked = tracked_artefacts(self.repo, self.PREVIOUSLY_TRACKED)
        self.assertEqual(untracked, [],
                         f"these actuator artefacts are no longer tracked: {untracked}")

    def test_the_new_default_artefacts_are_tracked_or_visible(self) -> None:
        """Either tracked, or present and VISIBLE to git (never ignored)."""
        tracked, untracked = tracked_artefacts(self.repo, self.NEW_DEFAULT)
        self.assertEqual(tracked + untracked, list(self.NEW_DEFAULT))
        for name in untracked:
            self.assertFalse(git_ignored(self.repo,
                                         f"mechanical/printed/{name}"),
                             f"{name} is neither tracked nor visible to git")
        if untracked:
            print(":: warning: these default artefacts are not staged yet; the "
                  f"parent must run `git add mechanical/printed/{{*.stl,*.step}}`: "
                  f"{untracked}")

    def test_a_clean_fixture_separates_tracked_from_omitted(self) -> None:
        """The check itself: staged files pass, an omitted file is reported.

        A temporary repository, so the answer does not depend on this working
        tree. This is the regression for the path bug: with the wrong prefix the
        staged file was reported as missing.
        """
        import subprocess
        import tempfile

        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "mechanical/printed").mkdir(parents=True)
            for name in ("knob.stl", "button-cap.stl"):
                (root / "mechanical/printed" / name).write_text("solid probe\n")
            init = subprocess.run(["git", "init", "-q", str(root)],
                                  capture_output=True, text=True, check=False)
            if init.returncode != 0:
                self.fail(f"git init failed: {init.stderr}")
            subprocess.run(["git", "add", "mechanical/printed/knob.stl"],
                           cwd=str(root), capture_output=True, text=True, check=False)
            tracked, untracked = tracked_artefacts(root, ("knob.stl", "button-cap.stl"))
            self.assertEqual(tracked, ["knob.stl"],
                             "a STAGED file must be reported as tracked")
            self.assertEqual(untracked, ["button-cap.stl"],
                             "an OMITTED file must be reported, not silently passed")


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

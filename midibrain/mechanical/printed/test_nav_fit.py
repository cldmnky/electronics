#!/usr/bin/env python3
"""Tests for the NAV keytop motion model and its variant.

    cd mechanical/printed && python3 -m unittest test_nav_fit -v

The model exists because the owner asked two questions that cannot be answered
by assertion:

  * does the cap FOLLOW the stem inside the 7.2 mm aperture, or does it bind?
  * is a narrower/crowned neck a measurable improvement, or just a different
    shape?

Every unmeasured input (stem tilt, lateral movement, pivot, print and hole error)
is a RANGE here, so the tests check the model at the nominal point, at the stated
bounds, in all four lateral directions and on the centre push, and they include
negative controls that must be rejected. Nothing in this file claims that the cap
grips, that sliding clearance retains it, or that the joystick works.
"""
from __future__ import annotations

import unittest

import nav_fit as nf
import printed_geometry as pg


def keytop() -> pg.NavKeytop:
    return pg.nav_keytop()


def inputs() -> nf.NavFitInputs:
    g = keytop()
    return nf.NavFitInputs(
        hole_diameter=g.aperture_diameter,
        panel_front_z=g.stack.panel_front_z,
        panel_rear_z=g.stack.panel_rear_z,
        head_diameter=g.neck_diameter,
        flange_diameter=g.flange_diameter,
        socket_bore=g.socket_bore,
        stem_diameter=g.stem_diameter,
        socket_depth=g.socket_depth,
        axial_free_play=g.free_play,
    )


class ProfileTest(unittest.TestCase):
    """Both parts are the same code path: a stack of axial bands."""

    def test_the_standard_part_is_one_band_over_the_panel(self) -> None:
        profile = nf.standard_profile(keytop())
        self.assertEqual(len(profile.bands), 1)
        self.assertAlmostEqual(profile.bands[0].z0, 0.0, places=9)
        self.assertAlmostEqual(profile.bands[0].z1, 1.6, places=9)
        self.assertAlmostEqual(profile.bands[0].diameter, 7.0, places=9)
        self.assertEqual(profile.status, "STANDARD")

    def test_the_variant_has_two_lands_and_a_waist(self) -> None:
        profile = nf.variant_profile(keytop())
        self.assertEqual([b.name for b in profile.bands],
                         ["front land", "waist", "rear land"])
        lands = [b for b in profile.bands if b.name.endswith("land")]
        waist = [b for b in profile.bands if b.name == "waist"][0]
        for band in lands:
            self.assertAlmostEqual(band.diameter, 7.0, places=9)
        self.assertLess(waist.diameter, 7.0)
        self.assertAlmostEqual(profile.width(), 1.6, places=9)
        # The variant is never the standard, and it says so in its own status.
        self.assertIn("EXPERIMENTAL", profile.status)
        self.assertNotEqual(profile.status, nf.standard_profile(keytop()).status)

    def test_a_band_wider_than_the_head_cannot_be_fitted(self) -> None:
        g = keytop()
        with self.assertRaises(nf.NavFitError):
            nf.NavProfile(name="bad", head_diameter=7.0,
                          bands=(nf.NeckBand("wide", 0.0, 1.6, 7.2),))

    def test_a_waist_that_is_not_narrower_is_refused(self) -> None:
        with self.assertRaises(nf.NavFitError):
            nf.variant_profile(keytop(), waist_diameter=7.0)

    def test_overlapping_bands_are_refused(self) -> None:
        with self.assertRaises(nf.NavFitError):
            nf.NavProfile(name="bad", head_diameter=7.0,
                          bands=(nf.NeckBand("a", 0.0, 1.0, 7.0),
                                 nf.NeckBand("b", 0.5, 1.6, 6.8)))


class ClearanceTest(unittest.TestCase):
    """The lands are what guide the part, and each one is reported."""

    def test_the_standard_has_0_10_mm_per_side_at_both_faces(self) -> None:
        bands = nf.land_clearance(nf.standard_profile(keytop()), inputs(), 0.0, 0.0)
        self.assertEqual(len(bands), 1)
        self.assertAlmostEqual(bands[0]["radial_clearance_mm"], 0.10, places=6)

    def test_the_variant_keeps_the_same_clearance_at_both_faces(self) -> None:
        """The variant must not buy waist relief by loosening the guidance."""
        standard = nf.land_clearance(nf.standard_profile(keytop()), inputs(), 0.0, 0.0)
        variant = nf.land_clearance(nf.variant_profile(keytop()), inputs(), 0.0, 0.0)
        lands = [b for b in variant if b["band"].endswith("land")]
        self.assertEqual(len(lands), 2)
        for land in lands:
            self.assertAlmostEqual(land["radial_clearance_mm"],
                                   standard[0]["radial_clearance_mm"], places=9)

    def test_a_local_defect_jams_the_standard_but_not_the_variant(self) -> None:
        """The measurable difference: 0.30 mm of relief in the middle 0.80 mm."""
        i = inputs()
        standard = nf.local_defect(nf.standard_profile(keytop()), i, 0.15, 0.8)
        variant = nf.local_defect(nf.variant_profile(keytop()), i, 0.15, 0.8)
        self.assertTrue(standard["jam"], "a 0.15 mm mid-panel defect must jam a "
                                         "full-length D7.00 neck")
        self.assertFalse(variant["jam"])
        self.assertAlmostEqual(variant["clearance_mm"] - standard["clearance_mm"],
                               0.30, places=6)

    def test_print_and_hole_error_reduce_the_clearance(self) -> None:
        i = inputs()
        good = nf.land_clearance(nf.standard_profile(keytop()), i, 0.0, 0.0)
        bad = nf.land_clearance(nf.standard_profile(keytop()), i, 0.05, 0.10)
        self.assertAlmostEqual(good[0]["radial_clearance_mm"]
                               - bad[0]["radial_clearance_mm"], 0.15, places=6)


class DirectionTest(unittest.TestCase):
    """Four lateral directions and the centre push, with negative controls."""

    def test_all_eight_directions_report_both_faces(self) -> None:
        # A NON-ZERO lateral input, or every offset is 0 and a one-sided report
        # would look symmetric.
        result = nf.evaluate(nf.standard_profile(keytop()), inputs(), stem_lateral_mm=0.05)
        self.assertEqual(sorted(result["directions"]), sorted(nf.DIRECTIONS))
        for direction, value in result["directions"].items():
            for face in ("front", "rear"):
                self.assertIn(f"{face}_face_offset_mm", value, direction)
                self.assertIn(f"{face}_face_clearance_mm", value, direction)
                self.assertIn(f"{face}_face_margin_mm", value, direction)
        # The margin magnitude is symmetric; the OFFSET carries the direction's
        # own sign, which is what makes a one-sided "+X only" report visible.
        margins = {d: v["front_face_margin_mm"] for d, v in result["directions"].items()}
        offsets = {d: v["front_face_offset_mm"] for d, v in result["directions"].items()}
        self.assertAlmostEqual(margins["+X"], margins["-X"], places=9)
        self.assertGreater(offsets["+X"], 0.0)
        self.assertLess(offsets["-X"], 0.0)

    def test_the_combined_face_displacement_is_what_decides(self) -> None:
        """Three cases the old separate tilt/lateral test called "no binding".

        The displacement at a face is lateral + tan(tilt)*(z - pivot). At 0.10 mm
        per side and a 1.6 mm panel, a tilt alone already collides.
        """
        i = inputs()
        for tilt, lateral in ((6.0, 0.09), (1.0, 0.15), (5.5, 0.0)):
            result = nf.evaluate(nf.standard_profile(keytop()), i,
                                 stem_tilt_deg=tilt, stem_lateral_mm=lateral)
            self.assertTrue(result["any_binding"],
                            f"tilt {tilt} deg with lateral {lateral} mm must collide")
            self.assertIn("FIRST-ORDER COLLISION", result["verdict"])
        # The 5 deg example is NOT collision-free once the oblique section is
        # included: the centreline offset at the rear face is 0.140 mm against a
        # 0.100 mm clearance, and the tilted-section growth adds to that.
        edge = nf.evaluate(nf.standard_profile(keytop()), i, stem_tilt_deg=5.0,
                           stem_lateral_mm=0.0)
        self.assertAlmostEqual(edge["directions"]["+X"]["rear_face_offset_mm"],
                               0.140, places=3)
        self.assertGreater(edge["directions"]["+X"]["oblique_section_growth_mm"]["rear"],
                           0.0)
        self.assertLess(edge["directions"]["+X"]["rear_face_margin_mm"], 0.0)
        self.assertTrue(edge["any_binding"])

    def test_the_pivot_changes_the_verdict(self) -> None:
        """Same tilt, two pivots: the face offsets must differ."""
        i = inputs()
        low = nf.evaluate(nf.standard_profile(keytop()), i, stem_tilt_deg=3.0,
                          stem_lateral_mm=0.0, pivot_z=0.0)
        high = nf.evaluate(nf.standard_profile(keytop()), i, stem_tilt_deg=3.0,
                           stem_lateral_mm=0.0, pivot_z=1.6)
        self.assertNotAlmostEqual(low["face_offsets_mm"]["rear"],
                                  high["face_offsets_mm"]["rear"], places=6)
        # A pivot at the FRONT face puts all the tilt displacement at the rear.
        self.assertAlmostEqual(low["face_offsets_mm"]["front"], 0.0, places=6)
        self.assertGreater(low["face_offsets_mm"]["rear"], 0.0)

    def test_the_nominal_model_reports_no_first_order_collision(self) -> None:
        result = nf.evaluate(nf.standard_profile(keytop()), inputs())
        self.assertFalse(result["any_binding"])
        # NOT a physical conclusion: the model is a first-order bound.
        self.assertTrue(result["verdict"].startswith("no first-order collision"))
        self.assertIn("ILLUSTRATIVE", result["verdict"])
        self.assertIn("FIRST-ORDER BOUND ONLY", result["model_status"])

    def test_the_cap_binds_when_the_stem_moves_further_than_its_clearance(self) -> None:
        """Negative control: the model must reject an input it cannot follow."""
        result = nf.evaluate(nf.standard_profile(keytop()), inputs(), stem_lateral_mm=0.15)
        self.assertTrue(result["any_binding"])
        self.assertIn("FIRST-ORDER COLLISION", result["verdict"])

    def test_an_oversized_neck_is_refused_by_the_model(self) -> None:
        # Negative control 1: a neck wider than the aperture cannot be assembled
        # from the rear, so the model must refuse to evaluate it at all.
        oversized = nf.NavProfile(name="oversized", head_diameter=7.3,
                                  bands=(nf.NeckBand("panel", 0.0, 1.6, 7.3),))
        with self.assertRaises(nf.NavFitError):
            nf.evaluate(oversized, inputs())
        # Negative control 2: a neck that prints into the hole binds instead of
        # guiding, and 0.12 mm of print growth exceeds the 0.10 mm per side.
        grown = nf.evaluate(nf.standard_profile(keytop()), inputs(), print_shrink=0.12)
        self.assertTrue(grown["any_binding"])

    def test_centre_push_reports_lost_motion_not_pre_load(self) -> None:
        result = nf.evaluate(nf.standard_profile(keytop()), inputs())
        centre = result["centre_push"]
        self.assertAlmostEqual(centre["free_play_mm"], 0.15, places=9)
        self.assertAlmostEqual(centre["travel_required_mm"], nf.TRAVEL_CENTRE_MM, places=9)
        # A positive gap is LOST MOTION at the start of the press, not pre-load.
        self.assertAlmostEqual(centre["lost_motion_mm"], 0.15, places=9)
        self.assertAlmostEqual(centre["pre_load_mm"], 0.0, places=9)
        self.assertFalse(centre["socket_floor_touches_stem_at_rest"])
        self.assertNotIn("blocks_travel", centre)
        # The cap must move the free play AND the switch travel: their SUM.
        self.assertAlmostEqual(centre["required_cap_displacement_mm"], 0.30, places=9)
        self.assertAlmostEqual(centre["required_cap_displacement_mm"],
                               centre["free_play_mm"] + centre["travel_required_mm"],
                               places=9)
        # Availability needs a MEASURED cap-travel limit; without one it is UNKNOWN.
        self.assertIsNone(centre["cap_travel_limit_mm"])
        self.assertIn("UNKNOWN", centre["cap_travel_available"])

    def test_cap_travel_availability_needs_an_explicit_limit(self) -> None:
        from dataclasses import replace as _replace
        short = _replace(inputs(), cap_travel_limit_mm=0.25)
        self.assertIn("NO", nf.evaluate(nf.standard_profile(keytop()), short)
                      ["centre_push"]["cap_travel_available"])
        enough = _replace(inputs(), cap_travel_limit_mm=0.35)
        self.assertIn("YES", nf.evaluate(nf.standard_profile(keytop()), enough)
                      ["centre_push"]["cap_travel_available"])

    def test_a_negative_free_play_is_pre_load(self) -> None:
        """The only case that pre-loads the switch is a floor at/below the stem."""
        from dataclasses import replace as _replace
        i = _replace(inputs(), axial_free_play=-0.05)
        centre = nf.evaluate(nf.standard_profile(keytop()), i)["centre_push"]
        self.assertAlmostEqual(centre["pre_load_mm"], 0.05, places=9)
        self.assertAlmostEqual(centre["lost_motion_mm"], 0.0, places=9)
        self.assertTrue(centre["socket_floor_touches_stem_at_rest"])

    def test_the_report_states_what_is_not_claimed(self) -> None:
        result = nf.evaluate(nf.standard_profile(keytop()), inputs())
        text = " ".join(result["not_claimed"]).lower()
        for phrase in ("grips", "retained", "retention", "measured"):
            self.assertIn(phrase, text)
        # The denial list itself must contain the claims the owner forbade, so
        # the wording cannot creep in as an assertion elsewhere.
        joined = " ".join(result["not_claimed"]).lower()
        for claim in ("sliding clearance yields retention", "that the joystick works",
                      "measured stem tilt"):
            self.assertIn(claim, joined)


class StemTipUncertaintyTest(unittest.TestCase):
    """The ALPS tip is a rounded square, so the socket is looser than 0.10/side."""

    def test_the_tip_dimensions_are_the_drawing_no_2_ones(self) -> None:
        self.assertAlmostEqual(pg.NAV_STEM_TIP_SQUARE, 1.95, places=9)
        self.assertAlmostEqual(pg.NAV_STEM_TIP_SQUARE_TOLERANCE, 0.05, places=9)
        self.assertAlmostEqual(pg.NAV_STEM_TIP_CORNER_RADIUS, 0.5, places=9)
        self.assertAlmostEqual(pg.NAV_STEM_SIDE_VIEW_DIAMETER, 3.0, places=9)

    def test_the_looseness_is_a_range_not_a_single_clearance(self) -> None:
        loose = pg.nav_socket_looseness(3.2)
        self.assertAlmostEqual(loose["if_tip_is_circle_3_00_mm"], 0.10, places=6)
        self.assertAlmostEqual(loose["if_tip_is_square_1_95_across_flats_mm"], 0.625,
                               places=6)
        self.assertAlmostEqual(loose["if_tip_is_square_corner_radius_mm"], 1.1718,
                               places=4)
        self.assertAlmostEqual(loose["if_tip_is_square_at_corners_mm"], 0.4282, places=4)
        self.assertAlmostEqual(loose["looseness_min_mm"], 0.10, places=6)
        self.assertAlmostEqual(loose["looseness_max_mm"], 0.625, places=6)
        # more than 6x wider than the single "0.10 per side" claim
        self.assertGreater(loose["looseness_max_mm"] / loose["looseness_min_mm"], 6.0)

    def test_the_rotation_centre_dimension_is_quoted_not_interpreted(self) -> None:
        self.assertAlmostEqual(nf.STEM_ROTATION_CENTRE_LABEL_MM, 0.49, places=9)
        # The label is NOT used as a pivot: inserting it into the PANEL frame would
        # invent a measured pivot on a switch that sits near PCB Z 10.6.
        self.assertNotEqual(nf.STEM_PIVOT_Z_MM[1], nf.STEM_ROTATION_CENTRE_LABEL_MM)
        self.assertIn("HYPOTHETICAL", nf.STEM_PIVOT_DATUM)
        self.assertIn("NOT converted", nf.STEM_PIVOT_DATUM)
        result = nf.evaluate(nf.standard_profile(keytop()), inputs())
        self.assertEqual(result["stem_pivot_datum"], nf.STEM_PIVOT_DATUM)
        text = pg.nav_socket_looseness(3.2)["rotation_centre_0_49_dimension"]
        self.assertIn("0.49", text)
        self.assertIn("NOT established", text)

    def test_the_drawing_directions_are_the_diagonals_and_are_checked(self) -> None:
        for diagonal in ("+X+Y", "+X-Y", "-X+Y", "-X-Y"):
            self.assertIn(diagonal, nf.DIRECTIONS)
        result = nf.evaluate(nf.standard_profile(keytop()), inputs())
        for diagonal in ("+X+Y", "+X-Y", "-X+Y", "-X-Y"):
            self.assertIn(diagonal, result["directions"])

    def test_the_travel_measurement_height_is_the_specified_one(self) -> None:
        # The exact part's product page states the height VERBATIM, so no claim may
        # say the measurement point is unspecified.
        self.assertIn("4.3mm height from the bottom of the switch",
                      nf.TRAVEL_MEASUREMENT_POINT)
        self.assertIn("SPECIFIED", nf.TRAVEL_MEASUREMENT_POINT)
        self.assertNotIn("NOT stated", nf.TRAVEL_MEASUREMENT_POINT)
        result = nf.evaluate(nf.standard_profile(keytop()), inputs())
        self.assertIn("4.3mm height", result["directional_travel"]["travel_measurement_point"])

    def test_the_socket_looseness_is_carried_into_the_travel_report(self) -> None:
        result = nf.evaluate(nf.standard_profile(keytop()), inputs())
        travel = result["directional_travel"]
        self.assertAlmostEqual(travel["socket_looseness_min_mm"], 0.10, places=6)
        self.assertAlmostEqual(travel["socket_looseness_max_mm"], 0.625, places=6)
        low, high = travel["stick_slip_range_mm"]
        self.assertLessEqual(low, high)
        # The range is CLAMPED at zero: a negative "lag" is no lag at all, and a
        # report that printed one would be meaningless.
        self.assertAlmostEqual(low, max(0.0, 0.25 - 0.625), places=4)
        self.assertAlmostEqual(high, 0.25 - 0.10, places=4)


class AnalysisTest(unittest.TestCase):
    """The comparison the owner asked for, and its honesty labels."""

    def test_the_analysis_covers_both_parts_and_both_ends_of_each_range(self) -> None:
        a = nf.analyse(keytop())
        for key in ("standard_nominal", "standard_worst_case",
                    "variant_nominal", "variant_worst_case"):
            self.assertIn(key, a)
            for direction in nf.DIRECTIONS:
                self.assertIn(direction, a[key]["directions"])
        # the ranges themselves are recorded, so a reader can see what is unknown
        for name in ("stem_tilt_deg_range", "stem_lateral_mm_range",
                     "print_shrink_mm_range", "hole_shrink_mm_range"):
            self.assertIn(name, a["inputs"])

    def test_the_variant_does_not_claim_a_better_tilt_limit(self) -> None:
        a = nf.analyse(keytop())
        self.assertAlmostEqual(a["variant_nominal"]["tilt_limit_deg"],
                               a["standard_nominal"]["tilt_limit_deg"], places=9)

    def test_the_printed_report_names_the_defect_comparison(self) -> None:
        text = " ".join(nf.report(keytop()))
        self.assertIn("local", text)
        self.assertIn("NOT measured", text)
        self.assertIn("standard", text)
        self.assertIn("variant", text)


if __name__ == "__main__":
    unittest.main()

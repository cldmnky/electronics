#!/usr/bin/env python3
"""Tests for the light-pipe reach interval computation.

No CAD dependency:

    cd midibrain && python3 unittest discover -s mechanical -p 'test_*.py'

The rules under test:

  * with the documented data only, the result is INCOMPLETE - no verdict and no
    bound of any kind,
  * an unknown FRONT reference offset (the plane a front flange or seat sets)
    can never yield a verdict,
  * the reported 7.29 mm is arithmetic (gap minus length) and nothing more,
  * no inequality between the two reference offsets is imposed,
  * a positive shortfall interval is "too short", a non-positive one "reaches",
    and an interval that straddles zero is "undetermined uncertainty",
  * impossible or malformed inputs raise; they never clamp to a value.
"""
from __future__ import annotations

import inspect
import re
import unittest

import light_pipe_reach as lp


def assert_interval_close(case: unittest.TestCase, actual: lp.Interval,
                          lo: float, hi: float, places: int = 9) -> None:
    """Compare interval bounds, tolerating float arithmetic noise only."""
    case.assertAlmostEqual(actual.lo, lo, places=places)
    case.assertAlmostEqual(actual.hi, hi, places=places)


def documented(**overrides) -> lp.ReachInputs:
    """The recorded data, optionally completed with candidate measurements."""
    fields = {
        "board_top_to_panel_rear_mm": lp.Interval.point(17.45),
        "gap_tolerance_mm": None,
        "pipe_length_mm": lp.Interval.point(10.16),
        "pipe_length_tolerance_mm": None,
        "rear_reference_offset_mm": None,
        "front_reference_offset_mm": None,
    }
    fields.update(overrides)
    return lp.ReachInputs(**fields)


def complete(**overrides) -> lp.ReachInputs:
    """Every input recorded; overrides name the values under test."""
    fields = {
        "board_top_to_panel_rear_mm": lp.Interval.point(17.45),
        "gap_tolerance_mm": lp.Interval.point(0.0),
        "pipe_length_mm": lp.Interval.point(10.16),
        "pipe_length_tolerance_mm": lp.Interval.point(0.0),
        "rear_reference_offset_mm": lp.Interval.point(0.0),
        "front_reference_offset_mm": lp.Interval.point(0.0),
    }
    fields.update(overrides)
    return lp.ReachInputs(**fields)


class IntervalTest(unittest.TestCase):
    def test_arithmetic_carries_the_direction(self) -> None:
        a, b = lp.Interval(10.0, 12.0), lp.Interval(1.0, 2.0)
        self.assertEqual(a + b, lp.Interval(11.0, 14.0))
        self.assertEqual(a - b, lp.Interval(8.0, 11.0))

    def test_helpers(self) -> None:
        i = lp.Interval.around(10.0, 0.5)
        self.assertEqual(i, lp.Interval(9.5, 10.5))
        self.assertAlmostEqual(i.nominal, 10.0)
        self.assertAlmostEqual(i.width, 1.0)
        self.assertTrue(i.contains(9.5))
        self.assertFalse(i.contains(10.6))

    def test_invalid_intervals_raise(self) -> None:
        for lo, hi in ((1.0, 0.0), (float("nan"), 1.0), (0.0, float("inf"))):
            with self.assertRaises(lp.ReachInputError):
                lp.Interval(lo, hi)
        with self.assertRaises(lp.ReachInputError):
            lp.Interval(True, True)  # a bool is not a length
        with self.assertRaises(lp.ReachInputError):
            lp.Interval.around(10.0, -0.1)


class DocumentedOnlyTest(unittest.TestCase):
    """The recorded data cannot produce a verdict, and must not pretend to."""

    def test_the_default_result_is_incomplete(self) -> None:
        result = lp.compute_reach(lp.ReachInputs.from_layout())
        self.assertEqual(result.status, lp.INCOMPLETE)
        self.assertFalse(result.complete)
        self.assertIsNone(result.verdict)
        self.assertIsNone(result.shortfall_mm)
        self.assertIsNone(result.required_length_mm)
        self.assertEqual(result.missing, ("gap_tolerance_mm", "pipe_length_tolerance_mm",
                                          "rear_reference_offset_mm",
                                          "front_reference_offset_mm"))

    def test_the_recorded_number_is_arithmetic_only(self) -> None:
        result = lp.compute_reach(lp.ReachInputs.from_layout())
        self.assertAlmostEqual(result.nominal_gap_minus_length_mm, 7.29, places=6)
        assert_interval_close(self, result.gap_minus_length_mm, 7.29, 7.29)
        self.assertTrue(any("arithmetic only" in n for n in result.notes))

    def test_the_recorded_number_matches_the_canonical_export(self) -> None:
        data = lp.load_layout()
        result = lp.compute_reach(lp.ReachInputs.from_layout(data))
        reach = data["lightPipeReach"]
        self.assertAlmostEqual(result.nominal_gap_minus_length_mm, reach["unaccounted"],
                               places=6)
        self.assertAlmostEqual(reach["gap"] - reach["pipeLength"], 7.29, places=6)

    def test_no_result_text_claims_a_bound(self) -> None:
        """No output may call 7.29 mm a bound, a worst case or a shortfall."""
        results = [
            lp.compute_reach(lp.ReachInputs.from_layout()),
            lp.compute_reach(complete()),
            lp.compute_reach(complete(rear_reference_offset_mm=lp.Interval(0.0, 2.0))),
        ]
        text = "\n".join(
            note for result in results for note in result.notes
        ) + "\n" + "\n".join(lp.report()) + "\n" + (lp.__doc__ or "")
        for claim in lp.FORBIDDEN_CLAIMS:
            self.assertNotIn(claim, text.lower(), f"the output claims '{claim}'")

    def test_the_module_has_no_verdict_path_without_both_reference_planes(self) -> None:
        source = inspect.getsource(lp)
        # The only assignment of a non-incomplete status sits after the guard on
        # the missing inputs; nothing else may set SUFFICIENT/INSUFFICIENT.
        self.assertEqual(source.count("status = INSUFFICIENT"), 1)
        self.assertEqual(source.count("status = SUFFICIENT"), 1)
        self.assertLess(source.index("if missing:"), source.index("status = INSUFFICIENT"))


class UnknownReferencePlaneTest(unittest.TestCase):
    """An unknown reference plane can never yield a verdict."""

    def test_an_unknown_front_offset_never_yields_a_verdict(self) -> None:
        result = lp.compute_reach(documented(
            gap_tolerance_mm=lp.Interval.point(0.0),
            pipe_length_tolerance_mm=lp.Interval.point(0.0),
            rear_reference_offset_mm=lp.Interval.point(0.0),
            front_reference_offset_mm=None,
        ))
        self.assertEqual(result.status, lp.INCOMPLETE)
        self.assertIsNone(result.verdict)
        self.assertIsNone(result.shortfall_mm)
        self.assertIsNone(result.required_length_mm)
        self.assertEqual(result.missing, ("front_reference_offset_mm",))

    def test_an_unknown_front_offset_keeps_a_short_pipe_unjudged(self) -> None:
        """The obvious short pipe is still not declared too short."""
        result = lp.compute_reach(lp.ReachInputs.from_layout())
        self.assertNotEqual(result.status, lp.INSUFFICIENT)
        self.assertNotEqual(result.status, lp.SUFFICIENT)
        self.assertIsNone(result.verdict)

    def test_an_unknown_rear_offset_never_yields_a_verdict(self) -> None:
        result = lp.compute_reach(documented(
            gap_tolerance_mm=lp.Interval.point(0.0),
            pipe_length_tolerance_mm=lp.Interval.point(0.0),
            front_reference_offset_mm=lp.Interval.point(0.0),
            rear_reference_offset_mm=None,
        ))
        self.assertEqual(result.status, lp.INCOMPLETE)
        self.assertEqual(result.missing, ("rear_reference_offset_mm",))
        self.assertIsNone(result.verdict)

    def test_both_unknown_offsets_are_named(self) -> None:
        result = lp.compute_reach(documented(
            gap_tolerance_mm=lp.Interval.point(0.0),
            pipe_length_tolerance_mm=lp.Interval.point(0.0),
        ))
        self.assertEqual(result.missing,
                         ("rear_reference_offset_mm", "front_reference_offset_mm"))

    def test_an_unknown_tolerance_alone_keeps_the_result_incomplete(self) -> None:
        result = lp.compute_reach(documented(
            gap_tolerance_mm=None,
            pipe_length_tolerance_mm=lp.Interval.point(0.05),
            rear_reference_offset_mm=lp.Interval.point(0.0),
            front_reference_offset_mm=lp.Interval.point(0.0),
        ))
        self.assertEqual(result.status, lp.INCOMPLETE)
        self.assertEqual(result.missing, ("gap_tolerance_mm",))


class CompleteResultTest(unittest.TestCase):
    def test_the_bare_arithmetic_case_is_short(self) -> None:
        result = lp.compute_reach(complete())
        self.assertEqual(result.status, lp.INSUFFICIENT)
        assert_interval_close(self, result.required_length_mm, 17.45, 17.45)
        assert_interval_close(self, result.shortfall_mm, 7.29, 7.29)

    def test_a_pipe_as_long_as_the_gap_reaches(self) -> None:
        result = lp.compute_reach(complete(pipe_length_mm=lp.Interval.point(17.45)))
        self.assertEqual(result.status, lp.SUFFICIENT)
        assert_interval_close(self, result.shortfall_mm, 0.0, 0.0)

    def test_a_positive_front_offset_shortens_the_requirement(self) -> None:
        """A front flange or recess that stops the pipe short needs less length."""
        result = lp.compute_reach(complete(front_reference_offset_mm=lp.Interval.point(2.0)))
        assert_interval_close(self, result.required_length_mm, 15.45, 15.45)
        assert_interval_close(self, result.shortfall_mm, 5.29, 5.29)

    def test_a_negative_front_offset_lengthens_the_requirement(self) -> None:
        """A pipe that must stand proud of the panel rear face needs more length."""
        result = lp.compute_reach(complete(front_reference_offset_mm=lp.Interval.point(-1.0)))
        assert_interval_close(self, result.required_length_mm, 18.45, 18.45)

    def test_a_positive_rear_offset_lengthens_the_requirement(self) -> None:
        """A pipe seated up over the LED needs more length, not less."""
        result = lp.compute_reach(complete(rear_reference_offset_mm=lp.Interval.point(1.5)))
        assert_interval_close(self, result.required_length_mm, 18.95, 18.95)

    def test_a_rear_offset_larger_than_an_led_height_is_accepted(self) -> None:
        """No inequality between the offsets is assumed: there is no evidence for one."""
        result = lp.compute_reach(complete(
            rear_reference_offset_mm=lp.Interval.point(5.0),
            front_reference_offset_mm=lp.Interval.point(0.0),
        ))
        assert_interval_close(self, result.required_length_mm, 22.45, 22.45)
        self.assertEqual(result.status, lp.INSUFFICIENT)

    def test_tolerances_that_straddle_zero_are_undetermined(self) -> None:
        result = lp.compute_reach(complete(
            pipe_length_mm=lp.Interval.point(17.45),
            gap_tolerance_mm=lp.Interval.point(0.9),
            pipe_length_tolerance_mm=lp.Interval.point(0.9),
            rear_reference_offset_mm=lp.Interval.point(0.0),
            front_reference_offset_mm=lp.Interval.point(0.0),
        ))
        self.assertEqual(result.status, lp.UNCERTAIN)
        self.assertLess(result.shortfall_mm.lo, 0.0)
        self.assertGreater(result.shortfall_mm.hi, 0.0)
        self.assertTrue(any("straddles zero" in n for n in result.notes))

    def test_the_widest_tolerance_governs(self) -> None:
        result = lp.compute_reach(complete(gap_tolerance_mm=lp.Interval(0.1, 0.3),
                                           pipe_length_tolerance_mm=lp.Interval.point(0.05)))
        assert_interval_close(self, result.required_length_mm, 17.15, 17.75)
        assert_interval_close(self, result.shortfall_mm, 6.94, 7.64)

    def test_an_offset_range_widens_the_requirement(self) -> None:
        result = lp.compute_reach(complete(
            rear_reference_offset_mm=lp.Interval(0.0, 2.0),
            front_reference_offset_mm=lp.Interval(-0.5, 0.5),
        ))
        assert_interval_close(self, result.required_length_mm, 16.95, 19.95)
        self.assertEqual(result.status, lp.INSUFFICIENT)


class InvalidInputTest(unittest.TestCase):
    def test_a_non_positive_pipe_length_raises(self) -> None:
        for length in (0.0, -1.0):
            with self.assertRaises(lp.ReachInputError):
                lp.compute_reach(complete(pipe_length_mm=lp.Interval.point(length)))

    def test_a_negative_gap_raises(self) -> None:
        with self.assertRaises(lp.ReachInputError):
            lp.compute_reach(complete(board_top_to_panel_rear_mm=lp.Interval(-1.0, 2.0)))

    def test_a_negative_tolerance_raises(self) -> None:
        with self.assertRaises(lp.ReachInputError):
            lp.compute_reach(complete(gap_tolerance_mm=lp.Interval(-0.1, 0.1)))
        with self.assertRaises(lp.ReachInputError):
            lp.compute_reach(complete(pipe_length_tolerance_mm=lp.Interval(-0.1, 0.1)))

    def test_a_tolerance_that_erases_the_pipe_raises(self) -> None:
        with self.assertRaises(lp.ReachInputError):
            lp.compute_reach(complete(pipe_length_tolerance_mm=lp.Interval.point(10.16)))

    def test_an_inverted_interval_in_an_input_raises(self) -> None:
        with self.assertRaises(lp.ReachInputError):
            lp.compute_reach(complete(rear_reference_offset_mm=lp.Interval(2.0, 1.0)))


class ReportTest(unittest.TestCase):
    def test_the_report_names_the_missing_inputs_and_the_arithmetic(self) -> None:
        lines = "\n".join(lp.report())
        self.assertIn("17.45 mm", lines)
        self.assertIn("10.16 mm", lines)
        self.assertIn("7.29 mm", lines)
        self.assertIn("arithmetic only", lines)
        self.assertIn("incomplete", lines)
        self.assertIn("front_reference_offset_mm", lines)
        self.assertNotIn("too short", lines)

    def test_the_source_carries_no_light_pipe_inequality(self) -> None:
        """No code may compare an LED height with a seat or force one to zero."""
        source = inspect.getsource(lp)
        self.assertNotRegex(source, re.compile(r"led_height"))
        self.assertNotRegex(source, re.compile(r"seat_depth"))


if __name__ == "__main__":
    unittest.main()

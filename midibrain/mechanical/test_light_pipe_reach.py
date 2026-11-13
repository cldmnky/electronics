#!/usr/bin/env python3
"""Tests for the light-pipe optical-gap computation (PGN25L165 candidate).

    cd midibrain && python3 -m unittest discover -s mechanical -p 'test_*.py'

The rules under test:

  * the datum is the panel FRONT face with +Z rearward;
  * the nominal tip plane is the SHAFT length 16.5, not the overall 17.5;
  * the nominal tip-to-board-plane distance is -6.50 mm (the provisional board
    face is 10.00, the tip 16.5) and is NOT an LED gap;
  * the optical gap is None and the status incomplete while any plane or the
    stack tolerance is unrecorded,
  * an LED rises TOWARD the panel, so its height SUBTRACTS from the board plane;
  * a complete result is arithmetic only and never a fit or optical verdict;
  * impossible or malformed inputs raise.
"""
from __future__ import annotations

import inspect
import json

import unittest
from pathlib import Path

import light_pipe_reach as lp

REFERENCE = Path(__file__).resolve().parent / "reference" / "pgn25l165.source.json"


class IntervalTest(unittest.TestCase):
    def test_arithmetic_carries_the_direction(self) -> None:
        a, b = lp.Interval(10.0, 12.0), lp.Interval(1.0, 2.0)
        self.assertEqual(a + b, lp.Interval(11.0, 14.0))
        self.assertEqual(a - b, lp.Interval(8.0, 11.0))

    def test_helpers(self) -> None:
        i = lp.Interval.around(16.5, 0.1)
        self.assertEqual(i, lp.Interval(16.4, 16.6))
        self.assertAlmostEqual(i.nominal, 16.5)
        self.assertAlmostEqual(i.width, 0.2)

    def test_invalid_intervals_raise(self) -> None:
        for lo, hi in ((1.0, 0.0), (float("nan"), 1.0), (0.0, float("inf"))):
            with self.assertRaises(lp.ReachInputError):
                lp.Interval(lo, hi)
        with self.assertRaises(lp.ReachInputError):
            lp.Interval(True, True)
        with self.assertRaises(lp.ReachInputError):
            lp.Interval.around(10.0, -0.1)


class DocumentedOnlyTest(unittest.TestCase):
    """The recorded data cannot produce a gap, and must not pretend to."""

    def test_the_default_result_is_incomplete_with_no_gap(self) -> None:
        result = lp.compute_reach(lp.ReachInputs.from_layout())
        self.assertEqual(result.status, lp.INCOMPLETE)
        self.assertFalse(result.complete)
        self.assertIsNone(result.optical_gap_mm)
        self.assertEqual(result.missing, ("head_underside_offset_mm", "led_height_mm",
                                          "stack_tolerance_mm"))
        self.assertIsNone(result.verdict)

    def test_the_nominal_planes_are_still_reported(self) -> None:
        result = lp.compute_reach(lp.ReachInputs.from_layout())
        self.assertAlmostEqual(result.nominal_tip_z_mm, 16.5, places=6)
        # 10.00 mm provisional board face - 16.5 mm shaft = -6.50 mm: the tip is
        # BEHIND the board plane, i.e. the selected part is too long for this stack
        self.assertAlmostEqual(result.nominal_tip_to_board_plane_mm, -6.5, places=6)
        self.assertLess(result.nominal_tip_to_board_plane_mm, 0.0)
        # a MEASURED seat does not exist yet, so the measured planes are None
        self.assertIsNone(result.tip_z_mm)
        self.assertIsNone(result.tip_to_board_plane_mm)

    def test_the_tip_plane_is_the_shaft_not_the_overall_length(self) -> None:
        layout = lp.load_layout()
        self.assertAlmostEqual(lp.nominal_tip_z(layout), 16.5, places=6)
        self.assertNotAlmostEqual(lp.nominal_tip_z(layout),
                                  float(layout["lightPipe"]["overallLength"]), places=2)
        self.assertAlmostEqual(float(layout["lightPipe"]["overallLength"])
                               - lp.nominal_tip_z(layout), 1.0, places=6)

    def test_tip_to_board_is_not_called_an_led_gap(self) -> None:
        result = lp.compute_reach(lp.ReachInputs.from_layout())
        self.assertAlmostEqual(result.nominal_tip_to_board_plane_mm, -6.5, places=6)
        self.assertIsNone(result.optical_gap_mm, "-6.50 mm must never be reported as an LED gap")

    def test_there_is_no_gap_minus_length_field(self) -> None:
        result = lp.compute_reach(lp.ReachInputs.from_layout())
        for forbidden in ("nominal_gap_minus_length_mm", "gap_minus_length_mm"):
            self.assertFalse(hasattr(result, forbidden),
                             f"{forbidden} is the withdrawn subtraction")

    def test_the_module_carries_no_withdrawn_subtraction(self) -> None:
        # A withdrawn number may appear ONLY inside a paragraph marked as
        # history, or on a code line that calls it retired/withdrawn.
        doc = lp.__doc__ or ""
        history = [p for p in doc.split("\n\n") if "WITHDRAWN" in p.upper()]
        self.assertTrue(history, "the module must keep an explicit history paragraph")
        for stale in ("10.16", "7.29", "PLP2", "unaccounted"):
            self.assertIn(stale, " ".join(history),
                          f"the withdrawn '{stale}' must be named in the history")
            elsewhere = doc
            for paragraph in history:
                elsewhere = elsewhere.replace(paragraph, "")
            self.assertNotIn(stale, elsewhere,
                             f"the withdrawn '{stale}' appears outside the history paragraph")
        # code and strings outside the docstring: every occurrence must say so
        code = inspect.getsource(lp).replace(doc, "")
        for number, line in enumerate(code.splitlines(), 1):
            for stale in ("10.16", "7.29", "PLP2", "unaccounted"):
                if stale in line:
                    low = line.lower()
                    self.assertTrue("retired" in low or "withdrawn" in low,
                                    f"code line {number} uses '{stale}' without saying so")
        # and no field or attribute carries the old names
        for name in dir(lp.ReachResult):
            self.assertNotIn("unaccounted", name)
            self.assertNotIn("gap_minus_length", name)

    def test_no_output_claims_a_bound_or_a_verdict(self) -> None:
        text = "\n".join(lp.report()) + "\n" + (lp.__doc__ or "")
        low = " ".join(text.lower().split())
        for claim in lp.FORBIDDEN_CLAIMS:
            self.assertNotIn(claim, low, f"the output claims '{claim}'")
        self.assertIn("assumption", low)


class StaleActiveReferenceTest(unittest.TestCase):
    """No ACTIVE code may still carry the retired Bivar numbers.

    The history paragraphs may name them, because they explain the withdrawal.
    Everywhere else - docstrings that describe a live tool, comments above live
    code, checklist lines, envelope names - the retired numbers are banned.
    """

    FILES = {
        "step_assembly.py": "the CAD builder",
        "step_geometry.py": "the envelope/assumptions module",
        "light_pipe_reach.py": "the reach computation",
    }
    RETIRED = ("10.16", "7.29", "PLP2")

    def _retired_hits(self, text: str) -> list[tuple[int, str]]:
        hits = []
        for number, line in enumerate(text.splitlines(), 1):
            low = line.lower()
            for stale in self.RETIRED:
                if stale not in line:
                    continue
                if "retired" in low or "withdrawn" in low or "history" in low:
                    continue
                hits.append((number, line.strip()[:110]))
        return hits

    def test_no_active_code_line_names_a_retired_value(self) -> None:
        root = Path(__file__).resolve().parent
        for name, what in self.FILES.items():
            text = (root / name).read_text()
            # the module docstring is the allowed history home
            module_doc = text.split('"""')
            body = text
            if len(module_doc) >= 3:
                body = text.replace(module_doc[1], "")
            hits = self._retired_hits(body)
            self.assertEqual(hits, [], f"{what} ({name}) still names a retired value in "
                                       f"active code: {hits}")

    def test_the_gauge_is_described_with_the_current_planes(self) -> None:
        source = (Path(__file__).resolve().parent / "step_assembly.py").read_text()
        # the header and the gauge docstring must name the current numbers
        self.assertIn("19.05", source)
        self.assertIn("16.5", source)
        self.assertIn("2.55", source)
        self.assertNotIn("17.45 / 10.16", source)
        # the gauge docstring says explicitly what the step is not
        doc = source[source.index("def build_reach_gauge"):]
        doc = doc[:doc.index('"""', doc.index('"""') + 3)]
        self.assertIn("NOT an LED gap", doc)
        self.assertIn("NOT a fit", doc)
        self.assertIn("panel FRONT face", doc)

    def test_the_envelope_carries_the_active_candidate_only(self) -> None:
        import step_geometry as sg
        env = sg.assembly_envelopes(sg.load_layout())
        self.assertNotIn("LIGHT_PIPE_PLP2_400_C", env)
        self.assertIn("LIGHT_PIPE_PGN25L165_PLACEHOLDER", env)
        pipe = env["LIGHT_PIPE_PGN25L165_PLACEHOLDER"]
        self.assertIsNone(pipe.z, "no body may be modelled for the pipe")


class SignConventionTest(unittest.TestCase):
    """+Z is rearward, so an LED above the board SUBTRACTS from the board plane."""

    def _inputs(self, led_height: float) -> lp.ReachInputs:
        return lp.ReachInputs(
            head_underside_offset_mm=lp.Interval.point(0.0),
            shaft_length_mm=lp.Interval.point(16.5),
            board_near_face_z_mm=lp.Interval.point(19.05),
            led_height_mm=lp.Interval.point(led_height),
            stack_tolerance_mm=lp.Interval.point(0.0),
        )

    def test_a_taller_led_reduces_the_gap(self) -> None:
        flush = lp.compute_reach(self._inputs(0.0)).optical_gap_mm.nominal
        tall = lp.compute_reach(self._inputs(0.5)).optical_gap_mm.nominal
        self.assertAlmostEqual(flush, 2.55, places=6)
        self.assertAlmostEqual(tall, 2.05, places=6)
        self.assertLess(tall, flush, "the LED must SUBTRACT from the board plane")

    def test_the_documented_model_docstring_states_the_negative_direction(self) -> None:
        doc = " ".join((lp.__doc__ or "").lower().split())
        self.assertIn("negative", doc)
        self.assertIn("toward the panel", doc)


class CompleteBranchTest(unittest.TestCase):
    """The generic interval branch: all planes supplied, arithmetic only."""

    def _complete(self, **overrides) -> lp.ReachInputs:
        fields = {
            "head_underside_offset_mm": lp.Interval.point(0.0),
            "shaft_length_mm": lp.Interval.around(16.5, 0.1),
            "board_near_face_z_mm": lp.Interval.point(19.05),
            "led_height_mm": lp.Interval.around(0.6, 0.1),
            "stack_tolerance_mm": lp.Interval.point(0.0),
        }
        fields.update(overrides)
        return lp.ReachInputs(**fields)

    def test_a_complete_case_returns_an_interval_and_no_verdict(self) -> None:
        result = lp.compute_reach(self._complete())
        self.assertEqual(result.status, lp.ARITHMETIC_ONLY)
        self.assertTrue(result.complete)
        self.assertIsNone(result.verdict, "arithmetic is never a fit verdict")
        # tip 16.5 +-0.1, LED 0.6 +-0.1 above 19.05 -> gap = 2.55 - 0.6 = 1.95 -+0.2
        self.assertAlmostEqual(result.optical_gap_mm.lo, 1.75, places=6)
        self.assertAlmostEqual(result.optical_gap_mm.hi, 2.15, places=6)

    def test_a_measured_stack_tolerance_widens_the_gap(self) -> None:
        tight = lp.compute_reach(self._complete())
        loose = lp.compute_reach(self._complete(stack_tolerance_mm=lp.Interval.point(0.3)))
        self.assertLess(loose.optical_gap_mm.lo, tight.optical_gap_mm.lo)
        self.assertGreater(loose.optical_gap_mm.hi, tight.optical_gap_mm.hi)

    def test_the_nominal_interval_matches_the_closed_form(self) -> None:
        result = lp.compute_reach(self._complete())
        self.assertAlmostEqual(result.tip_z_mm.nominal, 16.5, places=6)
        self.assertAlmostEqual(result.tip_to_board_plane_mm.nominal, 2.55, places=6)
        self.assertAlmostEqual(result.optical_gap_mm.nominal, 1.95, places=6)

    def test_a_negative_gap_is_reported_as_arithmetic_not_a_collision(self) -> None:
        result = lp.compute_reach(self._complete(led_height_mm=lp.Interval.point(3.0)))
        self.assertLess(result.optical_gap_mm.hi, 0.0)
        self.assertIsNone(result.verdict)


class InvalidInputTest(unittest.TestCase):
    def test_a_non_positive_shaft_raises(self) -> None:
        with self.assertRaises(lp.ReachInputError):
            lp.compute_reach(lp.ReachInputs(lp.Interval.point(0.0), lp.Interval.point(0.0),
                                            lp.Interval.point(19.05), None, None))

    def test_a_negative_led_height_raises(self) -> None:
        with self.assertRaises(lp.ReachInputError):
            lp.compute_reach(lp.ReachInputs(lp.Interval.point(0.0), lp.Interval.point(16.5),
                                            lp.Interval.point(19.05),
                                            lp.Interval(-0.1, 0.5), None))

    def test_a_negative_stack_tolerance_raises(self) -> None:
        with self.assertRaises(lp.ReachInputError):
            lp.compute_reach(lp.ReachInputs(lp.Interval.point(0.0), lp.Interval.point(16.5),
                                            lp.Interval.point(19.05),
                                            lp.Interval.point(0.5), lp.Interval(-0.1, 0.1)))

    def test_an_inverted_interval_raises(self) -> None:
        with self.assertRaises(lp.ReachInputError):
            lp.compute_reach(lp.ReachInputs(lp.Interval.point(0.0), lp.Interval(17.0, 16.0),
                                            lp.Interval.point(19.05), None, None))


class ReportTest(unittest.TestCase):
    def test_the_report_names_the_part_and_the_planes(self) -> None:
        lines = "\n".join(lp.report())
        self.assertIn("PGN25L165", lines)
        self.assertIn("16.50 mm", lines)
        self.assertIn("17.50 mm", lines)
        # the current board face (10.00) puts the tip 6.50 mm BEHIND the plane
        self.assertIn("-6.50 mm", lines)
        self.assertIn("None", lines)
        self.assertIn("incomplete", lines)
        self.assertIn("NOT an LED gap", lines)


class SourceMetadataTest(unittest.TestCase):
    """The reference record pins the source; no third-party PDF is committed."""

    def test_the_reference_record_matches_the_canonical_model(self) -> None:
        ref = json.loads(REFERENCE.read_text())
        layout = lp.load_layout()
        pipe = layout["lightPipe"]
        self.assertEqual(ref["sha256"], pipe["sourceSha256"])
        self.assertEqual(ref["source_url"], pipe["sourceUrl"])
        dims = ref["drawing_dimensions_mm"]
        self.assertAlmostEqual(dims["shaft_length_x_head_underside_to_tip"],
                               pipe["shaftLength"], places=6)
        self.assertAlmostEqual(dims["shaft_length_tolerance"], pipe["shaftLengthTolerance"], places=6)
        self.assertAlmostEqual(dims["head_diameter"], pipe["headDiameter"], places=6)
        self.assertAlmostEqual(dims["shaft_diameter"], pipe["shaftDiameter"], places=6)
        self.assertAlmostEqual(dims["head_height"], pipe["headHeight"], places=6)
        self.assertAlmostEqual(dims["mounting_hole_diameter"], pipe["panelHoleDiameter"], places=6)
        self.assertEqual(dims["longitudinal_ribs"], pipe["ribs"])
        self.assertAlmostEqual(ref["owner_screenshot_statement"]["overall_length_mm"],
                               pipe["overallLength"], places=6)

    def test_no_third_party_pdf_is_committed(self) -> None:
        reference_dir = REFERENCE.parent
        pdfs = list(reference_dir.glob("*.pdf"))
        self.assertEqual(pdfs, [], "do not commit the manufacturer's PDF")

    def test_the_record_states_the_fabrication_gap(self) -> None:
        """The hole tolerance is an OPEN question, never a promised capability."""
        ref = json.loads(REFERENCE.read_text())
        fab = ref["fabrication_note"]
        self.assertIn("NOT GUARANTEED", fab["status"])
        self.assertEqual(fab["drawing_hole_range_mm"], [2.55, 2.6])
        self.assertIn("jlcpcb.com/capabilities/pcb-capabilities", fab["jlcpcb_sources"]["capabilities"])
        self.assertIn("jlcpcb.com/blog/npth-design-guide", fab["jlcpcb_sources"]["npth_design_guide"])
        conflict = fab["conflict"].lower()
        for band in ("+-0.05", "+-0.08", "+0.13/-0.08"):
            self.assertIn(band, conflict)
        self.assertIn("none is selected", conflict)
        self.assertIn("BOTH", fab["duplicate_emission"])
        self.assertIn("de-duplication", fab["duplicate_emission"])
        self.assertGreaterEqual(len(fab["required_confirmation"]), 2)
        self.assertTrue(any("grip" in u for u in fab["unknowns"]))

    def test_the_model_asserts_nominal_geometry_not_a_capability(self) -> None:
        layout = lp.load_layout()
        leds = [a for a in layout["panelApertures"] if a["name"].endswith(" LED")]
        for aperture in leds:
            # the intended nominal geometry, and nothing about a tolerance band
            self.assertAlmostEqual(aperture["radius"] * 2, 2.6, places=9)
            self.assertNotIn("tolerance", json.dumps(aperture).lower())
        # the reach model carries no manufacturing promise, and it records WHY it
        # is incomplete: the tip plane is behind the board plane
        reach = layout["lightPipeReach"]
        self.assertIsNone(reach["opticalGap"])
        self.assertTrue(reach["status"].startswith(lp.INCOMPLETE), reach["status"])
        self.assertIn("too long for this stack", reach["status"])
        self.assertLess(reach["nominalTipToBoardPlane"], 0.0)
        self.assertLess(reach["requiredShaftLength"], reach["shaftLength"])

    def test_the_record_states_the_provisional_status_and_gaps(self) -> None:
        ref = json.loads(REFERENCE.read_text())
        self.assertIn("Provisional candidate", ref["status"])
        self.assertIn("PROVISIONAL", ref["distributor_note"])
        self.assertIn("no physical fit", ref["status"].lower())
        not_stated = " ".join(ref["not_stated_by_the_manufacturer"]).lower()
        self.assertIn("cutting instruction", not_stated)
        self.assertIn("retention", not_stated)


if __name__ == "__main__":
    unittest.main()

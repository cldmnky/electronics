#!/usr/bin/env python3
"""Tests for fab_check.py (run: python3 tscircuit/scripts/test_fab_check.py).

Synthetic circuit JSON only: no build is needed. Each test targets one limit or
one missing-data case, so a future edit cannot quietly relax a published value
or turn a missing measurement into a pass.
"""
from __future__ import annotations

import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import fab_check  # noqa: E402


def board_json(**kw):
    base = {"type": "pcb_board", "width": 128, "height": 112, "thickness": 1.6,
            "num_layers": 2, "material": "fr4",
            "min_board_edge_clearance": 0.2,
            "min_trace_to_pad_edge_clearance": 0.1,
            "min_pad_edge_to_pad_edge_clearance": 0.2,
            "min_trace_to_hole_edge_clearance": 0.2}
    base.update(kw)
    return base


def trace(width=0.15):
    return {"type": "pcb_trace", "pcb_trace_id": "t1",
            "route": [{"route_type": "wire", "width": width, "start": {}, "end": {}}]}


def via(hole=0.2, outer=0.45):
    return {"type": "pcb_via", "pcb_via_id": "v1", "hole_diameter": hole,
            "outer_diameter": outer}


def pth_circle(hole=1.0, outer=1.5):
    return {"type": "pcb_plated_hole", "pcb_plated_hole_id": "ph1",
            "shape": "circle", "hole_diameter": hole, "outer_diameter": outer}


def pth_rect(hole=1.0, w=1.5, h=1.5):
    return {"type": "pcb_plated_hole", "pcb_plated_hole_id": "ph2",
            "shape": "circular_hole_with_rect_pad", "hole_diameter": hole,
            "rect_pad_width": w, "rect_pad_height": h}


def pth_slot(hw=1.2, hh=2.0, ow=2.0, oh=2.6):
    return {"type": "pcb_plated_hole", "pcb_plated_hole_id": "ph3",
            "shape": "oval", "hole_width": hw, "hole_height": hh,
            "outer_width": ow, "outer_height": oh}


def npth(d=0.9):
    return {"type": "pcb_hole", "pcb_hole_id": "h1", "hole_diameter": d}


def smtpad(w=0.6, h=0.8):
    return {"type": "pcb_smtpad", "pcb_smtpad_id": "p1", "width": w, "height": h}


def good_board(**overrides):
    """A board that passes every published limit and every project target."""
    parts = {"board": board_json(), "trace": trace(), "via": via(),
             "pth": pth_circle(), "npth": npth(), "smtpad": smtpad()}
    parts.update(overrides)
    data = []
    for key, value in parts.items():
        if value is None:
            continue
        if isinstance(value, list):
            data.extend(value)
        else:
            data.append(value)
    return data


def failures(result):
    return " | ".join(result.failures)


class PublishedLimitsTest(unittest.TestCase):
    def test_good_board_passes_even_in_strict_mode(self):
        r = fab_check.check_board("t", good_board(), strict_targets=True)
        self.assertTrue(r.ok, failures(r))

    def test_trace_below_published_width_fails(self):
        r = fab_check.check_board("t", good_board(trace=trace(0.09)))
        self.assertFalse(r.ok)
        self.assertIn("min trace width", failures(r))

    def test_via_pad_below_published_minimum_fails(self):
        r = fab_check.check_board("t", good_board(via=via(0.2, 0.24)))
        self.assertFalse(r.ok)
        self.assertIn("min via pad", failures(r))

    def test_via_ring_below_published_floor_fails(self):
        r = fab_check.check_board("t", good_board(via=via(0.2, 0.28)))
        self.assertFalse(r.ok)
        self.assertIn("min via ring", failures(r))

    def test_via_at_published_floor_is_reported_but_not_a_failure(self):
        # 0.2/0.3 mm: legal published via floor, below the project target.
        r = fab_check.check_board("t", good_board(via=via(0.2, 0.3)))
        self.assertTrue(r.ok, failures(r))
        self.assertTrue(any("via target" in t for t in r.targets))
        self.assertTrue(any("published floor" in t for t in r.infos))

    def test_strict_mode_fails_the_small_via_target(self):
        r = fab_check.check_board("t", good_board(via=via(0.2, 0.3)),
                                  strict_targets=True)
        self.assertFalse(r.ok)
        self.assertIn("via target", failures(r))

    def test_npth_below_published_minimum_fails(self):
        # 0.3 mm passes the old 0.20 mm constant; the published limit is 0.50.
        r = fab_check.check_board("t", good_board(npth=npth(0.3)))
        self.assertFalse(r.ok)
        self.assertIn("min NPTH", failures(r))
        self.assertIn("0.500", failures(r))

    def test_npth_at_published_minimum_passes(self):
        r = fab_check.check_board("t", good_board(npth=npth(0.5)))
        self.assertTrue(r.ok, failures(r))

    def test_pth_annular_below_absolute_minimum_fails(self):
        r = fab_check.check_board("t", good_board(pth=pth_circle(1.0, 1.3)))
        self.assertFalse(r.ok)
        self.assertIn("min PTH annular ring", failures(r))

    def test_pth_annular_between_absolute_and_recommended_warns(self):
        r = fab_check.check_board("t", good_board(pth=pth_circle(1.0, 1.4)))
        self.assertTrue(r.ok, failures(r))
        self.assertTrue(any("absolute minimum" in t for t in r.warnings))

    def test_rect_pad_ring_is_measured(self):
        r = fab_check.check_board("t", good_board(pth=pth_rect(1.0, 1.5, 1.5)))
        self.assertTrue(r.ok, failures(r))
        self.assertIn("min ring 0.250", " ".join(r.infos))

    def test_plated_slot_below_published_width_fails(self):
        r = fab_check.check_board("t", good_board(pth=pth_slot(0.4, 2.0, 1.0, 2.6)))
        self.assertFalse(r.ok)
        self.assertIn("min plated slot", failures(r))

    def test_pth_drill_outside_published_range_fails(self):
        r = fab_check.check_board("t", good_board(pth=pth_circle(0.10, 0.9)))
        self.assertFalse(r.ok)
        self.assertIn("min PTH drill", failures(r))

    def test_edge_rule_below_published_fails(self):
        r = fab_check.check_board(
            "t", good_board(board=board_json(min_board_edge_clearance=0.1)))
        self.assertFalse(r.ok)
        self.assertIn("board-edge copper rule", failures(r))

    def test_pad_rule_below_published_is_a_target_not_a_limit_claim(self):
        r = fab_check.check_board(
            "t", good_board(board=board_json(
                min_pad_edge_to_pad_edge_clearance=0.1)))
        self.assertTrue(r.ok, failures(r))
        self.assertTrue(any("DFM preview" in t for t in r.targets))
        self.assertFalse(any("DFM preview" in t for t in r.failures))

    def test_unknown_plated_shape_fails_instead_of_passing(self):
        weird = {"type": "pcb_plated_hole", "pcb_plated_hole_id": "x",
                 "shape": "hexagon"}
        r = fab_check.check_board("t", good_board(pth=weird))
        self.assertFalse(r.ok)
        self.assertIn("unsupported shape", failures(r))


class MissingDataTest(unittest.TestCase):
    def test_no_board_element_fails(self):
        r = fab_check.check_board("t", [trace()])
        self.assertFalse(r.ok)
        self.assertIn("no pcb_board", failures(r))

    def test_no_vias_fails_instead_of_using_zero(self):
        r = fab_check.check_board("t", good_board(via=None))
        self.assertFalse(r.ok)
        self.assertIn("no pcb_via", failures(r))

    def test_no_traces_fails(self):
        r = fab_check.check_board("t", good_board(trace=None))
        self.assertFalse(r.ok)
        self.assertIn("no pcb_trace", failures(r))

    def test_missing_thickness_fails(self):
        r = fab_check.check_board("t", good_board(board=board_json(thickness=None)))
        self.assertFalse(r.ok)
        self.assertIn("thickness missing", failures(r))

    def test_nonstandard_thickness_fails(self):
        r = fab_check.check_board("t", good_board(board=board_json(thickness=1.5)))
        self.assertFalse(r.ok)
        self.assertIn("published FR4 set", failures(r))

    def test_trace_without_width_fails(self):
        bad = {"type": "pcb_trace", "pcb_trace_id": "t9",
               "route": [{"route_type": "wire"}]}
        r = fab_check.check_board("t", good_board(trace=bad))
        self.assertFalse(r.ok)
        self.assertIn("no width", failures(r))

    def test_via_without_outer_diameter_fails(self):
        bad = {"type": "pcb_via", "pcb_via_id": "v9", "hole_diameter": 0.2}
        r = fab_check.check_board("t", good_board(via=bad))
        self.assertFalse(r.ok)
        self.assertIn("missing hole/outer diameter", failures(r))

    def test_hole_without_diameter_fails(self):
        bad = {"type": "pcb_hole", "pcb_hole_id": "h9"}
        r = fab_check.check_board("t", good_board(npth=bad))
        self.assertFalse(r.ok)
        self.assertIn("no diameter", failures(r))


if __name__ == "__main__":
    unittest.main(verbosity=2)

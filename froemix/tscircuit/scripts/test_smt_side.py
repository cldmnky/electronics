#!/usr/bin/env python3
"""Negative tests for the single-sided placement gate."""
import copy
import unittest
from unittest.mock import patch

from check_smt_side import check


class SingleSideTests(unittest.TestCase):
    def setUp(self):
        self.circuit = [
            {"type": "source_component", "source_component_id": "s1", "name": "D3",
             "supplier_part_numbers": {"jlcpcb": ["C8598"]}},
            {"type": "pcb_component", "pcb_component_id": "c1", "source_component_id": "s1",
             "layer": "bottom", "do_not_place": False},
            {"type": "pcb_smtpad", "pcb_smtpad_id": "p1", "pcb_component_id": "c1", "layer": "bottom"},
            {"type": "pcb_component", "pcb_component_id": "fid", "do_not_place": True, "layer": "top"},
            {"type": "pcb_smtpad", "pcb_smtpad_id": "fidpad", "pcb_component_id": "fid", "layer": "top"},
        ]
        self.expected = patch("check_smt_side.EXPECTED_SMT_PLACEMENTS", 1)
        self.expected.start()
        self.addCleanup(self.expected.stop)

    def test_bottom_passes_and_do_not_place_is_excluded(self):
        self.assertEqual(check(self.circuit), [])

    def test_top_component_fails(self):
        circuit = copy.deepcopy(self.circuit)
        circuit[1]["layer"] = "top"
        self.assertTrue(any("component layer" in e for e in check(circuit)))

    def test_top_pad_fails(self):
        circuit = copy.deepcopy(self.circuit)
        circuit[2]["layer"] = "top"
        self.assertTrue(any("SMT pad" in e for e in check(circuit)))

    def test_missing_part_number_and_placement_fail(self):
        circuit = copy.deepcopy(self.circuit)
        circuit[0].pop("supplier_part_numbers")
        self.assertTrue(any("supplier part" in e for e in check(circuit)))
        circuit[1]["do_not_place"] = True
        self.assertTrue(any("expected 1 SMT" in e for e in check(circuit)))


if __name__ == "__main__":
    unittest.main()

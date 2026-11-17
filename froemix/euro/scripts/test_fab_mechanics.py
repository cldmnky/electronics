#!/usr/bin/env python3
"""Mutation tests for the built-board fabrication geometry checks."""
import copy
import json
import unittest
from check_fab_mechanics import BOARD, verify


class FabMechanicsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        with open(BOARD) as f:
            cls.board = json.load(f)

    def changed(self):
        return copy.deepcopy(self.board)

    def component(self, board, ref):
        src = next(e for e in board if e["type"] == "source_component" and e.get("name") == ref)
        return next(e for e in board if e["type"] == "pcb_component" and e.get("source_component_id") == src["source_component_id"])

    def test_current_board_passes(self):
        self.assertEqual(verify(self.board), [])

    def test_old_narrow_jack_slot_fails(self):
        b = self.changed()
        c = self.component(b, "J3")
        hole = next(e for e in b if e["type"] == "pcb_plated_hole" and e.get("pcb_component_id") == c["pcb_component_id"])
        hole["hole_width"], hole["hole_height"] = 1.6, 0.6
        self.assertTrue(any("J3" in e and "slots" in e for e in verify(b)))

    def test_shifted_jack_terminal_fails(self):
        b = self.changed()
        c = self.component(b, "J3")
        hole = next(e for e in b if e["type"] == "pcb_plated_hole" and e.get("pcb_component_id") == c["pcb_component_id"])
        hole["y"] += 0.1
        self.assertTrue(any("J3" in e and "centre or pin" in e for e in verify(b)))

    def test_missing_keepout_fails(self):
        b = self.changed()
        k = next(e for e in b if e["type"] == "pcb_keepout" and e["center"] == {"x": -42, "y": -14})
        b.remove(k)
        self.assertTrue(any("J3" in e and "keepout" in e for e in verify(b)))

    def test_trace_crossing_the_disk_fails(self):
        b = self.changed()
        b.append({"type": "pcb_trace", "pcb_trace_id": "injected", "route": [
            {"route_type": "wire", "layer": "top", "x": -44, "y": -14, "width": .25},
            {"route_type": "wire", "layer": "top", "x": -40, "y": -14, "width": .25},
        ]})
        self.assertTrue(any("J3" in e and "trace injected" in e for e in verify(b)))

    def test_via_inside_disk_fails(self):
        b = self.changed()
        b.append({"type": "pcb_via", "x": -42, "y": -14, "outer_diameter": .45})
        self.assertTrue(any("J3" in e and "via" in e for e in verify(b)))

    def test_pad_in_disk_fails(self):
        b = self.changed()
        b.append({"type": "pcb_smtpad", "pcb_smtpad_id": "injected", "shape": "rect",
                  "layer": "top", "x": -42, "y": -14, "width": .5, "height": .5})
        self.assertTrue(any("J3" in e and "copper pad" in e for e in verify(b)))

    def test_pour_in_disk_fails(self):
        b = self.changed()
        b.append({"type": "pcb_copper_pour", "shape": "brep", "layer": "bottom", "brep_shape": {
            "outer_ring": {"vertices": [{"x": -42.1, "y": -14.1}, {"x": -41.9, "y": -14.1},
                                        {"x": -41.9, "y": -13.9}, {"x": -42.1, "y": -13.9}]},
            "inner_rings": []}})
        self.assertTrue(any("J3" in e and "copper pour" in e for e in verify(b)))

    def test_pour_edge_clipping_disk_fails(self):
        b = self.changed()
        b.append({"type": "pcb_copper_pour", "shape": "brep", "layer": "bottom", "brep_shape": {
            "outer_ring": {"vertices": [{"x": -40.6, "y": -14.1}, {"x": -40.4, "y": -14.1},
                                        {"x": -40.4, "y": -13.9}, {"x": -40.6, "y": -13.9}]},
            "inner_rings": []}})
        self.assertTrue(any("J3" in e and "copper pour" in e for e in verify(b)))

    def test_trace_too_close_to_pot_lug_fails(self):
        b = self.changed()
        b.append({"type": "pcb_trace", "pcb_trace_id": "injected-lug", "route": [
            {"route_type": "wire", "layer": "top", "x": -47.4, "y": 44.5, "width": .25},
            {"route_type": "wire", "layer": "top", "x": -47.4, "y": 45.5, "width": .25},
        ]})
        self.assertTrue(any("RV1" in e and "trace too close" in e for e in verify(b)))

    def test_short_pot_slot_fails(self):
        b = self.changed()
        c = self.component(b, "RV1")
        hole = next(e for e in b if e["type"] == "pcb_plated_hole" and e.get("pcb_component_id") == c["pcb_component_id"] and not e.get("pcb_port_id"))
        hole.update({"shape": "oval", "hole_width": 1.8, "hole_height": 2.1})
        self.assertTrue(any("RV1" in e and "lug" in e for e in verify(b)))

    def test_sw2_led_pin_swap_fails(self):
        b = self.changed()
        c = self.component(b, "SW2")
        holes = [e for e in b if e["type"] == "pcb_plated_hole" and e.get("pcb_component_id") == c["pcb_component_id"]]
        a = next(h for h in holes if "pin5" in h.get("port_hints", []))
        d = next(h for h in holes if "pin6" in h.get("port_hints", []))
        a["y"], d["y"] = d["y"], a["y"]
        self.assertTrue(any("SW2" in e and "pin5" in e for e in verify(b)))

    def test_sw1_direction_net_swap_fails(self):
        b = self.changed()
        c = self.component(b, "SW1")
        p = next(e for e in b if e["type"] == "source_port" and e.get("source_component_id") == c["source_component_id"] and e.get("pin_number") == 4)
        p["subcircuit_connectivity_map_key"] = "wrong"
        self.assertTrue(any("SW1" in e and "JOY_UP" in e for e in verify(b)))

    def test_small_peg_ring_fails(self):
        b = self.changed()
        c = self.component(b, "SW2")
        hole = next(e for e in b if e["type"] == "pcb_plated_hole" and e.get("pcb_component_id") == c["pcb_component_id"] and not e.get("pcb_port_id"))
        hole["outer_diameter"] = 2.1
        self.assertTrue(any("SW2" in e and "rings" in e for e in verify(b)))


if __name__ == "__main__":
    unittest.main(verbosity=2)

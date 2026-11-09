#!/usr/bin/env python3
"""Offline negative tests for check_elec.py; no network, no build needed.

Run: python3 froemix/tools/test_elec.py

The built circuit is the fixture. Each test copies it, breaks exactly one thing
that check_elec.py is supposed to catch, and asserts that the check fails - so a
check that silently stops working is a failing test, not a green run. The last
two tests cover the positive direction: the real fixture passes, and the numbers
the documentation quotes are the numbers the model produces.
"""
import json
import os
import subprocess
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
CIRCUIT = os.path.join(ROOT, "tscircuit", "dist", "index", "circuit.json")
CHECK = os.path.join(HERE, "check_elec.py")


def component(circuit, ref):
    for elm in circuit:
        if elm["type"] == "source_component" and elm.get("name") == ref:
            return elm
    raise AssertionError(f"{ref} not in the built circuit")


def net_ids(circuit):
    return {e["name"]: e["source_net_id"] for e in circuit if e["type"] == "source_net"}


def port(circuit, ref, pin):
    for elm in circuit:
        if elm["type"] != "source_port" or str(elm.get("pin_number")) != str(pin):
            continue
        comp = next((c for c in circuit if c["type"] == "source_component"
                     and c["source_component_id"] == elm.get("source_component_id")), None)
        if comp and comp.get("name") == ref:
            return elm
    raise AssertionError(f"{ref}.{pin} port not found")


def port_trace(circuit, ref, pin):
    p = port(circuit, ref, pin)
    for elm in circuit:
        if elm["type"] == "source_trace" and p["source_port_id"] in (elm.get("connected_source_port_ids") or []):
            return elm
    raise AssertionError(f"no source_trace for {ref}.{pin}")


def swap_pin_nets(circuit, first, second):
    """Exchange the nets of two pins - the classic wiring mistake."""
    a, b = port_trace(circuit, *first), port_trace(circuit, *second)
    a["connected_source_net_ids"], b["connected_source_net_ids"] = (
        b.get("connected_source_net_ids"), a.get("connected_source_net_ids"))


@unittest.skipUnless(os.path.exists(CIRCUIT), "needs dist/index/circuit.json (bunx tsci build)")
class ElectricalCheckTests(unittest.TestCase):
    def setUp(self):
        with open(CIRCUIT) as f:
            self.circuit = json.load(f)

    def run_check(self, circuit):
        import tempfile
        with tempfile.NamedTemporaryFile("w", suffix=".json", delete=False) as f:
            json.dump(circuit, f)
            path = f.name
        try:
            result = subprocess.run([sys.executable, CHECK, "--circuit", path, "--quiet"],
                                    capture_output=True, text=True)
        finally:
            os.unlink(path)
        return result.returncode, result.stdout + result.stderr

    def assert_fails(self, circuit, needle):
        code, output = self.run_check(circuit)
        self.assertNotEqual(code, 0, "check_elec passed a circuit it must reject")
        self.assertIn(needle, output, f"expected {needle!r} in:\n{output}")

    # --- the fixture itself -------------------------------------------------
    def test_built_circuit_passes(self):
        code, output = self.run_check(self.circuit)
        self.assertEqual(code, 0, output)

    def test_documented_numbers(self):
        """The figures doc/ELECTRICAL-VERIFICATION.md quotes come from this model."""
        sys.path.insert(0, HERE)
        import check_elec as c
        values = {ref: c.value_of(self.circuit, ref, "resistance")
                  for ref in ("R47", "R58", "R59", "R60", "R61")}
        out = c.numbers(values)
        self.assertAlmostEqual(out["i_out_worst"] * 1e3, 4.31, places=2)
        self.assertAlmostEqual(out["i_in_worst"] * 1e3, 5.34, places=2)
        self.assertAlmostEqual(out["ifhl_margin"], 3.34, places=2)
        self.assertAlmostEqual(out["p_r59_zero_rds"] * 1e3, 367.5, places=1)
        self.assertAlmostEqual(out["p_r60_tip_fault"] * 1e3, 551.2, places=1)
        self.assertAlmostEqual(out["i_fet_ring_fault"] * 1e3, 1.42, places=2) \
            if "i_fet_ring_fault" in out else None
        self.assertAlmostEqual(out["p_fet_ring_fault"] * 1e3, 1.42, places=2)
        self.assertAlmostEqual(out["i_gate_peak"] * 1e3, 19.44, places=2)
        # resistor tolerance corners, not nominal: 180*(1+1%) against 2.2k*(1-1%)
        self.assertAlmostEqual(out["vgs_on"], 2.5243, places=4)
        self.assertAlmostEqual(out["vgs_off"], 0.3703, places=4)
        self.assertAlmostEqual(out["i_r61_dc"] * 1e3, 1.44, places=2)

    # --- parts -------------------------------------------------------------
    def test_old_optocoupler_is_rejected(self):
        component(self.circuit, "U15")["supplier_part_numbers"] = {"jlcpcb": ["C37658"]}
        self.assert_fails(self.circuit, "U15: built JLCPCB part is ['C37658']")

    def test_old_transistor_mpn_is_rejected(self):
        component(self.circuit, "Q2")["manufacturer_part_number"] = "MMBT3906LT1G"
        self.assert_fails(self.circuit, "Q2: built MPN is 'MMBT3906LT1G'")

    # --- physical pin map --------------------------------------------------
    def test_swapped_gate_and_drain_is_rejected(self):
        swap_pin_nets(self.circuit, ("Q2", 1), ("Q2", 3))
        self.assert_fails(self.circuit, "Q2.1: on net NET_Q2_C")

    def test_optocoupler_anode_and_cathode_swapped_is_rejected(self):
        swap_pin_nets(self.circuit, ("U15", 1), ("U15", 3))
        self.assert_fails(self.circuit, "U15.1: on net NET_D1_A")

    def test_tip_and_ring_resistors_swapped_is_rejected(self):
        component(self.circuit, "R59")["resistance"] = 22
        component(self.circuit, "R60")["resistance"] = 33
        self.assert_fails(self.circuit, "R59 is 22 ohm")

    def test_100r_gate_resistor_is_rejected(self):
        component(self.circuit, "R58")["resistance"] = 100
        self.assert_fails(self.circuit, "R58 is 100 ohm")

    def test_10k_gate_pullup_is_rejected(self):
        component(self.circuit, "R61")["resistance"] = 10000
        self.assert_fails(self.circuit, "R61 is 10000 ohm")

    # --- the model itself --------------------------------------------------
    def test_tip_resistor_too_small_fails_the_fault_check(self):
        """10 ohm at the tip would be 1.20 W in a 1 W 2512, as CA-033 warns."""
        sys.path.insert(0, HERE)
        import check_elec as c
        values = {"R47": 220, "R58": 180, "R59": 33, "R60": 10, "R61": 2200}
        out = c.numbers(values)
        self.assertGreater(out["p_r60_tip_fault"], c.RATED_W_2512)

    def test_gate_resistor_too_small_fails_the_pin_bound(self):
        sys.path.insert(0, HERE)
        import check_elec as c
        values = {"R47": 220, "R58": 100, "R59": 33, "R60": 22, "R61": 2200}
        out = c.numbers(values)
        self.assertGreater(out["i_gate_peak"], c.ASSUMED_GPIO_ABS_MAX)

    def test_stronger_pullup_would_not_hold_the_fet_off(self):
        """A 10k pull-up leaves too little divider current: |VGS| rises."""
        sys.path.insert(0, HERE)
        import check_elec as c
        values = {"R47": 220, "R58": 4700, "R59": 33, "R60": 22, "R61": 10000}
        out = c.numbers(values)
        self.assertLess(out["vgs_on"], 2.5)


if __name__ == "__main__":
    unittest.main(verbosity=2)

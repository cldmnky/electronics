#!/usr/bin/env python3
"""Negative tests on fresh built JSON, never mutate the actual build/source."""
import copy
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent
BASE = json.loads((ROOT / "../tscircuit/dist/index/circuit.json").read_text())
FRONT = json.loads((ROOT / "dist/index/circuit.json").read_text())


def source(cj, name):
    return next(e for e in cj if e["type"] == "source_component" and e.get("name") == name)


def component(cj, name):
    sid = source(cj, name)["source_component_id"]
    return next(e for e in cj if e["type"] == "pcb_component" and e["source_component_id"] == sid)


def port(cj, name, pin):
    sid = source(cj, name)["source_component_id"]
    return next(e for e in cj if e["type"] == "source_port" and e["source_component_id"] == sid and e["name"] == pin)


class MatingMutations(unittest.TestCase):
    def run_check(self, base=None, front=None):
        with tempfile.TemporaryDirectory(prefix="froemix-mating-test-") as temp:
            args = [sys.executable, str(ROOT / "scripts/check_mating.py")]
            for flag, data in (("base", base), ("board", front)):
                if data is not None:
                    path = Path(temp) / f"{flag}.json"
                    path.write_text(json.dumps(data))
                    args += [f"--{flag}", str(path)]
            return subprocess.run(args, capture_output=True, text=True)

    def assert_rejected(self, *, base=None, front=None, message):
        result = self.run_check(base, front)
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertIn(message, result.stdout + result.stderr)

    def test_current_boards_pass(self):
        result = self.run_check()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_interior_pin_drift_not_only_endpoints(self):
        front = copy.deepcopy(FRONT)
        pid = port(front, "CN1", "pin8")["source_port_id"]
        next(e for e in front if e["type"] == "pcb_port" and e["source_port_id"] == pid)["x"] += 0.1
        self.assert_rejected(front=front, message="frozen coordinate")

    def test_base_pad_can_drift_independently(self):
        base = copy.deepcopy(BASE)
        pid = port(base, "CN4", "pin4")["source_port_id"]
        next(e for e in base if e["type"] == "pcb_port" and e["source_port_id"] == pid)["y"] += 0.1
        self.assert_rejected(base=base, message="pads do not coincide")

    def test_pin_net_can_change_without_moving_it(self):
        front = copy.deepcopy(FRONT)
        pid = port(front, "CN1", "pin8")["source_port_id"]
        trace = next(e for e in front if e["type"] == "source_trace" and pid in e.get("connected_source_port_ids", []))
        trace["connected_source_net_ids"] = [next(e["source_net_id"] for e in front if e["type"] == "source_net" and e["name"] == "GND")]
        self.assert_rejected(front=front, message="net mismatch")

    def test_header_on_wrong_base_face(self):
        base = copy.deepcopy(BASE)
        component(base, "CN2")["layer"] = "bottom"
        self.assert_rejected(base=base, message="base header CN2: layer must be top")

    def test_front_control_on_wrong_face(self):
        front = copy.deepcopy(FRONT)
        component(front, "SW2")["layer"] = "bottom"
        self.assert_rejected(front=front, message="panel control/module must face top")

    def test_wrong_standoff_diameter(self):
        front = copy.deepcopy(FRONT)
        next(e for e in front if e["type"] == "pcb_hole")["hole_diameter"] = 2.5
        self.assert_rejected(front=front, message="missing 3.2mm standoff")

    def test_visible_led_drift(self):
        front = copy.deepcopy(FRONT)
        component(front, "D1")["footprint_origin"]["x"] += 1
        self.assert_rejected(front=front, message="indicator footprint origin")


if __name__ == "__main__":
    unittest.main()

#!/usr/bin/env python3
"""Tests for the before/after build comparison tool.

The tool is the guard on any panel-source change, so a false "identical" would
be worse than no tool at all. These tests build small before/after fixtures and
prove the tool reports a difference when - and only when - there is one.

    cd midibrain && python3 -m unittest discover -s mechanical -p 'test_*.py'
"""
from __future__ import annotations

import json
import sys
import unittest
from pathlib import Path
from tempfile import TemporaryDirectory

# The tool lives in mechanical/tools/, beside the other run-by-hand helpers;
# tests live at the top level so `unittest discover -s mechanical` finds them.
sys.path.insert(0, str(Path(__file__).resolve().parent / "tools"))

import compare_panel_build as cpb  # noqa: E402

GERBER_HEAD = """%TF.GenerationSoftware,tscircuit*%
%TF.CreationDate,{stamp}*%
G04 Created by tscircuit (builder) date {stamp}*
G04 Layer: F_SilkScreen*
%ADD10C,0.150000*%
D10*
X-10000Y-5000D02*
X10000Y-5000D01*
M02*
"""

DRILL = """; DRILL file {{tscircuit}} date {stamp}
; FORMAT={{-:-/ absolute / metric / decimal}}
; #@! TF.CreationDate,{stamp}
; #@! TF.GenerationSoftware,tscircuit
M48
METRIC
T10C3.200000
X-68.58Y-61.25
M30
"""


def write_gerbers(root: Path, stamp: str, silk_x: int = 10000) -> Path:
    root.mkdir(parents=True, exist_ok=True)
    (root / "F_SilkScreen.gbr").write_text(GERBER_HEAD.format(stamp=stamp)
                                          .replace("X10000", f"X{silk_x}"))
    (root / "drill_npth.drl").write_text(DRILL.format(stamp=stamp))
    return root


def panel_json(cutoffs: list[dict] | None = None, warnings: list[dict] | None = None) -> list:
    elements: list[dict] = [
        {"type": "pcb_board", "width": 152.0, "height": 128.5, "thickness": 1.6},
        {"type": "pcb_cutout", "shape": "circle", "center": {"x": -53.0, "y": -34.0}, "radius": 6.2},
        {"type": "pcb_cutout", "shape": "rect", "center": {"x": -38.0, "y": 53.5},
         "width": 12.6, "height": 7.6},
        {"type": "pcb_hole", "x": -47.0, "y": 53.5, "hole_diameter": 3.2},
    ]
    if cutoffs:
        elements.extend(cutoffs)
    elements.append({"type": "source_project_metadata",
                     "source_filesystem_md5_hash": "aaaa"})
    if warnings:
        elements.extend(warnings)
    return elements


def linked_json(net_ids: tuple[str, str] = ("source_net_4", "source_net_7"),
                via_ids: tuple[str, str] = ("pcb_via_136", "pcb_via_137"),
                component_ids: tuple[str, str] = ("source_component_12", "source_component_13"),
                link_swap: bool = False) -> list:
    """A document with identifiers that are LINKED across elements.

    Two vias, each carrying its own id and pointing at a component and a net.
    ``link_swap`` makes the second via point at the first via's net, which is a
    changed association with no geometry change.
    """
    first_net, second_net = net_ids
    first_via, second_via = via_ids
    first_comp, second_comp = component_ids
    net_of_second = first_net if link_swap else second_net
    return [
        {"type": "pcb_board", "width": 152.0, "height": 128.5, "thickness": 1.6},
        {"type": "source_net", "source_net_id": first_net, "name": "GND"},
        {"type": "source_net", "source_net_id": second_net, "name": "VSYS"},
        {"type": "source_component", "source_component_id": first_comp, "name": "U1"},
        {"type": "source_component", "source_component_id": second_comp, "name": "U2"},
        {"type": "pcb_via", "pcb_via_id": first_via, "x": 1.0, "y": 2.0,
         "outer_diameter": 0.6, "hole_diameter": 0.3,
         "source_net_id": first_net, "source_component_id": first_comp},
        {"type": "pcb_via", "pcb_via_id": second_via, "x": 9.0, "y": 2.0,
         "outer_diameter": 0.6, "hole_diameter": 0.3,
         "source_net_id": net_of_second, "source_component_id": second_comp},
        {"type": "source_project_metadata", "source_filesystem_md5_hash": "aaaa"},
    ]


class GerberCompareTest(unittest.TestCase):
    def test_timestamps_alone_are_not_a_difference(self) -> None:
        with TemporaryDirectory() as tmp:
            before = write_gerbers(Path(tmp) / "before", "2026-11-08T22:21:07.284Z")
            after = write_gerbers(Path(tmp) / "after", "2026-11-09T03:04:05.678Z")
            self.assertEqual(cpb.compare_gerbers(before, after), [])

    def test_a_moved_coordinate_is_reported_with_its_line(self) -> None:
        with TemporaryDirectory() as tmp:
            before = write_gerbers(Path(tmp) / "before", "2026-11-08T22:21:07.284Z")
            after = write_gerbers(Path(tmp) / "after", "2026-11-08T22:21:07.284Z",
                                  silk_x=10001)
            problems = cpb.compare_gerbers(before, after)
            self.assertEqual(len(problems), 1)
            self.assertIn("F_SilkScreen.gbr", problems[0])
            self.assertIn("differs", problems[0])

    def test_a_drill_change_is_reported(self) -> None:
        with TemporaryDirectory() as tmp:
            before = write_gerbers(Path(tmp) / "before", "2026-11-08T22:21:07.284Z")
            after = write_gerbers(Path(tmp) / "after", "2026-11-08T22:21:07.284Z")
            text = (after / "drill_npth.drl").read_text().replace("T10C3.200000", "T10C3.100000")
            (after / "drill_npth.drl").write_text(text)
            problems = cpb.compare_gerbers(before, after)
            self.assertEqual(len(problems), 1)
            self.assertIn("drill_npth.drl", problems[0])

    def test_a_missing_file_is_reported(self) -> None:
        with TemporaryDirectory() as tmp:
            before = write_gerbers(Path(tmp) / "before", "x")
            after = write_gerbers(Path(tmp) / "after", "x")
            (after / "drill_npth.drl").unlink()
            problems = cpb.compare_gerbers(before, after)
            self.assertTrue(any("file set differs" in p for p in problems))

    def test_an_empty_directory_is_reported(self) -> None:
        with TemporaryDirectory() as tmp:
            before = Path(tmp) / "before"
            after = Path(tmp) / "after"
            before.mkdir()
            after.mkdir()
            self.assertTrue(cpb.compare_gerbers(before, after))


class JsonCompareTest(unittest.TestCase):
    def test_identical_geometry_passes(self) -> None:
        with TemporaryDirectory() as tmp:
            a = Path(tmp) / "a.json"
            b = Path(tmp) / "b.json"
            data = panel_json()
            a.write_text(json.dumps(data))
            b.write_text(json.dumps(data))
            self.assertEqual(cpb.compare_json(a, b), [])

    def test_the_source_hash_is_exempt(self) -> None:
        with TemporaryDirectory() as tmp:
            a = Path(tmp) / "a.json"
            b = Path(tmp) / "b.json"
            a.write_text(json.dumps(panel_json()))
            data = panel_json()
            data[-1]["source_filesystem_md5_hash"] = "bbbb"
            b.write_text(json.dumps(data))
            self.assertEqual(cpb.compare_json(a, b), [])

    def test_warning_order_and_ids_are_exempt_but_messages_are_not(self) -> None:
        warnings = [
            {"type": "source_component_availability_warning",
             "source_component_availability_warning_id": "w1", "message": "C1"},
            {"type": "source_component_availability_warning",
             "source_component_availability_warning_id": "w2", "message": "C2"},
        ]
        with TemporaryDirectory() as tmp:
            a = Path(tmp) / "a.json"
            b = Path(tmp) / "b.json"
            a.write_text(json.dumps(panel_json(warnings=warnings)))
            reordered = [dict(warnings[1], source_component_availability_warning_id="z9"),
                         dict(warnings[0], source_component_availability_warning_id="z8")]
            b.write_text(json.dumps(panel_json(warnings=reordered)))
            self.assertEqual(cpb.compare_json(a, b), [])

            changed = [dict(warnings[0], message="C9"), warnings[1]]
            b.write_text(json.dumps(panel_json(warnings=changed)))
            problems = cpb.compare_json(a, b)
            self.assertTrue(any("warning set differs" in p for p in problems))

    def test_a_changed_radius_is_reported(self) -> None:
        with TemporaryDirectory() as tmp:
            a = Path(tmp) / "a.json"
            b = Path(tmp) / "b.json"
            a.write_text(json.dumps(panel_json()))
            data = panel_json()
            data[1]["radius"] = 6.3
            b.write_text(json.dumps(data))
            problems = cpb.compare_json(a, b)
            self.assertTrue(problems)
            self.assertIn("radius", " ".join(problems))

    def test_a_changed_diameter_is_reported(self) -> None:
        with TemporaryDirectory() as tmp:
            a = Path(tmp) / "a.json"
            b = Path(tmp) / "b.json"
            a.write_text(json.dumps(panel_json()))
            data = panel_json()
            data[3]["hole_diameter"] = 3.3
            b.write_text(json.dumps(data))
            problems = cpb.compare_json(a, b)
            self.assertIn("hole_diameter", " ".join(problems))

    def test_a_reordered_element_list_is_reported(self) -> None:
        """Element order is part of the contract: a swap renumbers every id."""
        with TemporaryDirectory() as tmp:
            a = Path(tmp) / "a.json"
            b = Path(tmp) / "b.json"
            data = panel_json()
            a.write_text(json.dumps(data))
            swapped = [data[0], data[2], data[1], *data[3:]]
            b.write_text(json.dumps(swapped))
            problems = cpb.compare_json(a, b)
            self.assertTrue(problems)
            self.assertIn("differs in", " ".join(problems))

    def test_an_added_or_removed_element_is_reported(self) -> None:
        with TemporaryDirectory() as tmp:
            a = Path(tmp) / "a.json"
            b = Path(tmp) / "b.json"
            a.write_text(json.dumps(panel_json()))
            data = panel_json(cutoffs=[{"type": "pcb_cutout", "shape": "circle",
                                        "center": {"x": 0, "y": 0}, "radius": 1.0}])
            b.write_text(json.dumps(data))
            problems = cpb.compare_json(a, b)
            self.assertTrue(any("count" in p for p in problems))


class IdentifierInvariantTest(unittest.TestCase):
    """What the comparison proves about identifiers, and what it rejects."""

    def _compare(self, a: list, b: list) -> list[str]:
        with TemporaryDirectory() as tmp:
            pa = Path(tmp) / "a.json"
            pb = Path(tmp) / "b.json"
            pa.write_text(json.dumps(a))
            pb.write_text(json.dumps(b))
            return cpb.compare_json(pa, pb)

    def test_a_consistent_renumbering_is_absorbed(self) -> None:
        """Every identifier moves by a constant; every link is unchanged."""
        a = linked_json()
        b = linked_json(net_ids=("source_net_104", "source_net_107"),
                        via_ids=("pcb_via_1136", "pcb_via_1137"),
                        component_ids=("source_component_112", "source_component_113"))
        self.assertEqual(self._compare(a, b), [])

    def test_a_changed_net_link_fails(self) -> None:
        """The required negative: only a referenced net changes."""
        problems = self._compare(linked_json(), linked_json(link_swap=True))
        self.assertTrue(problems, "a changed net link must fail the comparison")
        self.assertIn("source_net_id", " ".join(problems))

    def test_a_changed_component_link_fails(self) -> None:
        a = linked_json()
        b = linked_json()
        b[5] = dict(b[5], source_component_id="source_component_13")
        problems = self._compare(a, b)
        self.assertTrue(problems, "a changed component link must fail")
        self.assertIn("source_component_id", " ".join(problems))

    def test_exchanging_labels_is_absorbed_but_exchanging_links_fails(self) -> None:
        """A label carries no meaning; where it points does."""
        a = linked_json()
        swapped_labels = linked_json(via_ids=("pcb_via_137", "pcb_via_136"))
        self.assertEqual(self._compare(a, swapped_labels), [],
                         "exchanging two labels is the same relabelling")
        # Both vias now point at the other's net: every label is unchanged, the
        # association is not. That must fail.
        swapped_links = linked_json()
        swapped_links[5] = dict(swapped_links[5], source_net_id="source_net_7")
        swapped_links[6] = dict(swapped_links[6], source_net_id="source_net_4")
        problems = self._compare(a, swapped_links)
        self.assertTrue(problems, "exchanging two link targets must fail")
        self.assertIn("source_net_id", " ".join(problems))

    def test_a_non_numeric_identifier_must_match_exactly(self) -> None:
        a = linked_json()
        b = linked_json()
        b.append({"type": "pcb_port", "pcb_port_id": "available-net-orientation-11-GND",
                  "x": 0.0, "y": 0.0})
        self.assertTrue(self._compare(a, b))
        c = linked_json()
        c.append({"type": "pcb_port", "pcb_port_id": "available-net-orientation-12-GND",
                  "x": 0.0, "y": 0.0})
        self.assertEqual(self._compare(a, a), [])
        b2 = linked_json()
        b2.append({"type": "pcb_port", "pcb_port_id": "available-net-orientation-11-GND",
                   "x": 0.0, "y": 0.0})
        self.assertEqual(self._compare(b2, b2), [])
        # same position, different non-numeric id -> fails
        d = linked_json()
        d.append({"type": "pcb_port", "pcb_port_id": "available-net-orientation-11-GND",
                  "x": 0.0, "y": 0.0})
        e = linked_json()
        e.append({"type": "pcb_port", "pcb_port_id": "available-net-orientation-99-GND",
                  "x": 0.0, "y": 0.0})
        problems = self._compare(d, e)
        # These ids have no trailing _<digits>, so they are NOT normalised: a
        # changed non-numeric identifier must fail.
        self.assertTrue(problems, "a non-numeric identifier must match exactly")
        f = linked_json()
        f.append({"type": "pcb_port", "pcb_port_id": "available-net-orientation-GND",
                  "x": 0.0, "y": 0.0})
        g = linked_json()
        g.append({"type": "pcb_port", "pcb_port_id": "available-net-orientation-VSYS",
                  "x": 0.0, "y": 0.0})
        self.assertTrue(self._compare(f, g))

    def test_the_documented_invariant_is_what_is_implemented(self) -> None:
        doc = " ".join((cpb.__doc__ or "").lower().split())
        self.assertIn("occurrence pattern", doc)
        self.assertIn("not a netlist check", doc)
        self.assertIn("does not establish link integrity", doc.replace("**", ""))
        self.assertIn("top-level field", doc)
        # and no blanket identifier exemption exists in the code
        import inspect
        source = inspect.getsource(cpb)
        self.assertNotIn("EXEMPT_SUFFIXES", source)


class IdentifierLimitationTest(unittest.TestCase):
    """The three documented limits, demonstrated - not hidden.

    These tests PASS when the limitation is real. If a future change makes one of
    them detectable, the test fails and the docstring must be updated with it.
    """

    def _compare(self, a: list, b: list) -> list[str]:
        with TemporaryDirectory() as tmp:
            pa = Path(tmp) / "a.json"
            pb = Path(tmp) / "b.json"
            pa.write_text(json.dumps(a))
            pb.write_text(json.dumps(b))
            return cpb.compare_json(pa, pb)

    def test_limit_1_a_changed_singleton_dangling_reference_is_absorbed(self) -> None:
        """A token used once is compared by position, so relabelling it is invisible."""
        a = [{"type": "pcb_trace", "pcb_trace_id": "pcb_trace_1", "x": 0.0,
              "source_net_id": "source_net_99"}]      # no source_net_99 anywhere
        b = [{"type": "pcb_trace", "pcb_trace_id": "pcb_trace_1", "x": 0.0,
              "source_net_id": "source_net_42"}]      # also dangling, different label
        self.assertEqual(self._compare(a, b), [],
                         "DOCUMENTED LIMIT: a changed singleton reference is absorbed")

    def test_limit_1b_a_singleton_reference_can_also_become_defined(self) -> None:
        a = [{"type": "pcb_trace", "pcb_trace_id": "pcb_trace_1", "source_net_id": "source_net_99"}]
        b = [{"type": "pcb_trace", "pcb_trace_id": "pcb_trace_1", "source_net_id": "source_net_99"},
             {"type": "source_net", "source_net_id": "source_net_99", "name": "GND"}]
        # an added element is of course detected - but that is the element, not the link
        self.assertTrue(self._compare(a, b))
        # with equal element counts, the singleton link may still change silently
        c = [{"type": "pcb_trace", "pcb_trace_id": "pcb_trace_1", "source_net_id": "source_net_99"},
             {"type": "source_net", "source_net_id": "source_net_99", "name": "GND"}]
        d = [{"type": "pcb_trace", "pcb_trace_id": "pcb_trace_1", "source_net_id": "source_net_98"},
             {"type": "source_net", "source_net_id": "source_net_98", "name": "GND"}]
        self.assertEqual(self._compare(c, d), [],
                         "DOCUMENTED LIMIT: a consistent singleton relabelling is absorbed")

    def test_limit_2_a_compound_singleton_token_change_is_absorbed(self) -> None:
        """Any value ending _<digits> is one token, compound names included."""
        a = [{"type": "schematic_component",
              "schematic_port_id": "schematic_port_3-schematic_port_11"}]
        b = [{"type": "schematic_component",
              "schematic_port_id": "schematic_port_3-schematic_port_19"}]
        self.assertEqual(self._compare(a, b), [],
                         "DOCUMENTED LIMIT: a compound singleton change keeps the same token")
        # changing the leading number is NOT absorbed, because the namespace changes
        c = [{"type": "schematic_component",
              "schematic_port_id": "schematic_port_7-schematic_port_11"}]
        self.assertTrue(self._compare(a, c),
                        "a different compound namespace is still detected")

    def test_limit_3_nested_identifiers_are_compared_raw(self) -> None:
        """Nested dicts and arrays are not normalised: renumbering them FAILS."""
        a = [{"type": "pcb_component", "pcb_component_id": "pcb_component_1",
              "fields": {"source_net_id": "source_net_4"}}]
        b = [{"type": "pcb_component", "pcb_component_id": "pcb_component_1",
              "fields": {"source_net_id": "source_net_7"}}]
        self.assertTrue(self._compare(a, b),
                        "DOCUMENTED LIMIT: a nested change is raw-compared (and detected)")
        # a consistent renumbering inside a nested object is a FALSE FAILURE
        c = [{"type": "pcb_component", "pcb_component_id": "pcb_component_1",
              "fields": {"source_net_id": "source_net_104"}}]
        d = [{"type": "pcb_component", "pcb_component_id": "pcb_component_1",
              "fields": {"source_net_id": "source_net_107"}}]
        self.assertTrue(self._compare(c, d) if c == d else self._compare(a, c),
                        "a nested identifier is compared raw, so it is never normalised")
        # arrays inside an element are raw as well
        e = [{"type": "x", "pads": [{"source_net_id": "source_net_4"}]}]
        f = [{"type": "x", "pads": [{"source_net_id": "source_net_104"}]}]
        self.assertTrue(self._compare(e, f))

    def test_the_docstring_states_all_three_limits(self) -> None:
        doc = " ".join((cpb.__doc__ or "").lower().split())
        self.assertIn("singleton reference", doc)
        self.assertIn("compound", doc)
        self.assertIn("nested dicts and arrays are compared raw", doc)
        self.assertIn("not a netlist check", doc)
        self.assertIn("does not establish link integrity", doc.replace("**", ""))
        self.assertIn("a token that occurs once can be relabelled", doc)
        # the old overclaim must be gone
        self.assertNotIn("so a changed link always fails", doc)
        self.assertNotIn("every identifier relationship", doc)


class CommandLineTest(unittest.TestCase):
    def test_a_missing_argument_is_a_usage_error(self) -> None:
        with self.assertRaises(SystemExit) as caught:
            cpb.main([])
        self.assertEqual(caught.exception.code, 2)

    def test_a_missing_path_is_a_usage_error(self) -> None:
        with TemporaryDirectory() as tmp:
            # A path that does not exist is a usage error, not a pass.
            self.assertEqual(cpb.main(["--gerbers", str(Path(tmp) / "nope"), tmp]), 2)
            self.assertEqual(cpb.main(["--json", str(Path(tmp) / "nope.json"),
                                       str(Path(tmp) / "other.json")]), 2)

    def test_identical_inputs_return_zero(self) -> None:
        with TemporaryDirectory() as tmp:
            before = write_gerbers(Path(tmp) / "before", "x")
            after = write_gerbers(Path(tmp) / "after", "y")
            a = Path(tmp) / "a.json"
            b = Path(tmp) / "b.json"
            a.write_text(json.dumps(panel_json()))
            b.write_text(json.dumps(panel_json()))
            self.assertEqual(cpb.main(["--gerbers", str(before), str(after),
                                       "--json", str(a), str(b)]), 0)

    def test_a_difference_returns_one(self) -> None:
        with TemporaryDirectory() as tmp:
            before = write_gerbers(Path(tmp) / "before", "x")
            after = write_gerbers(Path(tmp) / "after", "y", silk_x=1)
            self.assertEqual(cpb.main(["--gerbers", str(before), str(after)]), 1)


if __name__ == "__main__":
    unittest.main()

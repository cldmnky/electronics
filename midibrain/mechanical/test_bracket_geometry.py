#!/usr/bin/env python3
"""Pure-geometry tests for the feedthrough bracket and the LCD aperture.

No CAD dependency: these run everywhere.

    cd midibrain && python3 -m unittest discover -s mechanical -p 'test_*.py'

Each test states the rule it protects. The regression tests at the bottom take
the reviewed defects (hardcoded bracket positions, touching 40 mm slots, an
undersized LCD aperture) and prove that the current rules REJECT them.
"""
from __future__ import annotations

import copy
import math
import unittest
from pathlib import Path

import bracket_geometry as bg


def layout() -> dict:
    return bg.load_layout()


class CanonicalDataTest(unittest.TestCase):
    def test_layout_json_is_the_generated_export(self) -> None:
        data = layout()
        self.assertEqual(data["units"], "mm")
        self.assertIn("Generated from tscircuit/lib/layout.ts", data["note"])
        # The bridge must carry the bracket, and every position Python uses.
        for key in ("bracket", "frontBoardOpening", "frontRearMount", "feedthroughs",
                    "lcdCutout", "lcdCutoutOutline", "frontPcb", "panel"):
            self.assertIn(key, data)

    def test_missing_layout_file_fails_loudly(self) -> None:
        with self.assertRaises(bg.GeometryError):
            bg.load_layout(Path("/nonexistent/layout.json"))

    def test_bracket_positions_follow_the_canonical_mount_data(self) -> None:
        """The mounts and slots are the canonical data, not a private copy."""
        data = layout()
        self.assertEqual(list(data["bracket"]["mountX"]),
                         [-66.5, 55], "bracket mounts drifted from the canonical data")
        for slot, feedthrough in zip(data["bracket"]["slots"], data["feedthroughs"]):
            self.assertEqual(slot["x"], feedthrough["x"], "slot left its feedthrough centre")
            self.assertEqual(slot["id"], feedthrough["id"])

    def test_bracket_mounts_land_on_solid_board_beside_the_u_opening(self) -> None:
        """A mount over the U opening would have no board to bolt to."""
        data = layout()
        opening, front = data["frontBoardOpening"], data["frontPcb"]
        mount_y = data["bracket"]["mountY"]
        self.assertLess(abs(mount_y), front["height"] / 2)
        for x in data["bracket"]["mountX"]:
            self.assertTrue(x < opening["left"] or x > opening["right"],
                            f"mount x {x} is over the U opening")
            self.assertLess(abs(x), front["width"] / 2)


class BracketStockTest(unittest.TestCase):
    """The printed bar must span both mounts and leave material around them."""

    def setUp(self) -> None:
        self.data = layout()
        self.g = bg.bracket_geometry(self.data)

    def test_outline_spans_both_mounts_with_the_documented_wall(self) -> None:
        wall = self.data["bracketAllowance"]["mountEdgeWall"]
        for mount in self.g.mounts:
            # The right mount sits well inside the pocket-extended outline; the
            # rule is a MINIMUM 3 mm centre-to-edge wall, not an exact one.
            self.assertGreaterEqual(mount.clearance_to_edge(self.g.outline),
                                    wall - mount.radius - 1e-9,
                                    msg=f"mount ({mount.x}, {mount.y}) wall")
        # The left mount is the one against the edge: exactly the 3 mm wall.
        self.assertAlmostEqual(self.g.mounts[0].clearance_to_edge(self.g.outline),
                               wall - self.g.mounts[0].radius, places=6)

    def test_outline_is_backed_by_the_front_board(self) -> None:
        self.assertTrue(self.g.outline.clamp_to(self.g.board))

    def test_mount_hole_sits_on_the_bracket(self) -> None:
        for mount in self.g.mounts:
            self.assertTrue(self.g.outline.contains_point(mount.x, mount.y))

    def test_material_exists_around_every_mount(self) -> None:
        """A nut needs material, not a cut. Sample a ring just outside the hole."""
        radius = self.g.mounts[0].radius + 1.15  # inside the 1.65 mm ligament
        for mount in self.g.mounts:
            for x, y in mount.ring(radius, samples=12):
                self.assertTrue(self.g.outline.contains_point(x, y),
                                f"({x:.2f}, {y:.2f}) is off the bar at mount {mount.x}")
                for slot in self.g.slots:
                    self.assertNotEqual(slot.contains_point(x, y), True,
                                        f"({x:.2f}, {y:.2f}) is inside a slot")
                for head in self.g.head_clearance:
                    self.assertGreater(math.hypot(x - head.x, y - head.y), head.radius,
                                       f"({x:.2f}, {y:.2f}) is inside a head clearance")
                for tie in self.g.tie_holes:
                    self.assertGreater(math.hypot(x - tie.x, y - tie.y), tie.radius,
                                       f"({x:.2f}, {y:.2f}) is inside a tie hole")

    def test_bracket_bottom_covers_the_top_spacer_row(self) -> None:
        # The bar is rear-side, so the front-side display envelope does not
        # constrain it; its bottom edge has to sit below the top spacer heads.
        wall = self.data["bracketAllowance"]["spacerHeadWall"]
        top = max(h["y"] for h in self.data["frontRearMount"] if h["y"] > 0)
        self.assertLessEqual(self.g.outline.y1, top - wall + 1e-9)
        for h in self.data["frontRearMount"]:
            if h["y"] > 0:
                self.assertGreater(h["y"], self.g.outline.y1)

    def test_bracket_fits_the_panel(self) -> None:
        panel = self.data["panel"]
        self.assertLess(abs(self.g.outline.x1), panel["width"] / 2)
        self.assertLess(abs(self.g.outline.x2), panel["width"] / 2)


class BracketSlotTest(unittest.TestCase):
    """Slots follow the vendor cutouts and never merge into one another."""

    def setUp(self) -> None:
        self.data = layout()
        self.g = bg.bracket_geometry(self.data)

    def test_slot_count_matches_the_feedthroughs(self) -> None:
        self.assertEqual(len(self.g.slots), 3)
        self.assertEqual(self.g.slot_ids, ["USB-C POWER", "UMC AUDIO USB", "MIDI HUB USB"])

    def test_slot_width_is_cutout_plus_access_clearance(self) -> None:
        clear = self.data["bracketAllowance"]["slotClearancePerSide"]
        for slot, feedthrough in zip(self.g.slots, self.data["feedthroughs"]):
            self.assertAlmostEqual(slot.width, feedthrough["cutoutWidth"] + 2 * clear, places=6)
            self.assertAlmostEqual(slot.centre[0], feedthrough["x"], places=9)

    def test_slot_floor_covers_the_declared_panel_opening(self) -> None:
        """Height clearance, not only width: a fixed floor at y = 50 was too high."""
        clear = self.data["bracketAllowance"]["slotClearancePerSide"]
        for slot, feedthrough, slot_id in zip(self.g.slots, self.data["feedthroughs"], self.g.slot_ids):
            opening_bottom = feedthrough["y"] - feedthrough["cutoutHeight"] / 2
            self.assertLessEqual(slot.y1, opening_bottom,
                                 f"slot {slot_id} floor cuts into the declared opening")
            self.assertAlmostEqual(slot.y1, opening_bottom - clear, places=6)
        # The withdrawn fixed floor at y = 50 is above the USB-C opening bottom.
        self.assertGreater(50.0, 53.5 - 7.6 / 2)

    def test_slot_floor_stays_inside_the_u_opening(self) -> None:
        for slot, slot_id in zip(self.g.slots, self.g.slot_ids):
            self.assertGreaterEqual(slot.y1, self.g.board_opening.y1 - 1e-9,
                                    f"slot {slot_id} floor is over solid board material")

    def test_slot_webs_are_printable(self) -> None:
        """The reviewed defect: 40 mm slots at x = -5 and +35 touched (0 mm web)."""
        widths = self.g.slot_web_widths()
        self.assertEqual(len(widths), 2)
        for width in widths:
            self.assertGreater(width, self.g.material_to_cuts,
                               f"slot web {width:.2f} mm is not printable")
            self.assertGreater(width, 5.0, f"slot web {width:.2f} mm is too thin to trust")

    def test_slots_stay_inside_the_u_opening(self) -> None:
        """Outside the U opening the board is solid, so a slot there blocks the cable."""
        for slot, slot_id in zip(self.g.slots, self.g.slot_ids):
            self.assertGreaterEqual(slot.x1, self.g.board_opening.x1 - 1e-9, slot_id)
            self.assertLessEqual(slot.x2, self.g.board_opening.x2 + 1e-9, slot_id)

    def test_slots_open_to_the_top_edge_and_stop_above_the_opening_floor(self) -> None:
        for slot in self.g.slots:
            self.assertGreaterEqual(slot.y2, self.g.outline.y2 - 1e-9, "slot is not open")
            self.assertGreater(slot.y1, self.g.board_opening.y1, "slot floor blocks the cable path")
            self.assertTrue(slot.clamp_to(self.g.outline))

    def test_slot_floor_keeps_material_to_the_mount_holes(self) -> None:
        for slot in self.g.slots:
            for mount in self.g.mounts:
                self.assertGreater(mount.clearance_to_rect(slot), 0.0)


class BracketHeadClearanceTest(unittest.TestCase):
    """The top front-to-rear spacer hardware sits in the same 3 mm layer."""

    def setUp(self) -> None:
        self.data = layout()
        self.g = bg.bracket_geometry(self.data)

    def test_every_top_front_rear_mount_under_the_bar_has_clearance(self) -> None:
        top = [h for h in self.data["frontRearMount"] if h["y"] > 0]
        self.assertEqual(len(top), 2)
        under = [h for h in top if self.g.outline.contains_point(h["x"], h["y"])]
        # Only the left spacer is under the bar; the right one (71.5) is outside.
        self.assertEqual([h["x"] for h in under], [-69])
        self.assertEqual([h.x for h in self.g.head_clearance], [h["x"] for h in under])
        for head, mount in zip(self.g.head_clearance, under):
            self.assertAlmostEqual(head.x, mount["x"], places=9)
            self.assertAlmostEqual(head.y, mount["y"], places=9)

    def test_clearance_does_not_merge_with_a_bracket_mount(self) -> None:
        for head in self.g.head_clearance:
            for mount in self.g.mounts:
                self.assertGreater(head.clearance_to(mount), 0.0)

    def test_clearance_covers_the_mount_hole_and_the_head(self) -> None:
        """A D6.5 slot covers an M2.5 clearance hole plus a <6.5 mm head/washer."""
        for head in self.g.head_clearance:
            self.assertGreater(head.diameter, self.data["mountHoleDiameter"])
            self.assertGreater(head.diameter, 5.0)

    def test_clearance_does_not_run_past_the_top_or_far_side(self) -> None:
        # A head exactly at the bar's left end breaks out of that side as an
        # OPEN slot (the validate() notice). It must not break out of the far
        # side or past the top edge.
        for head in self.g.head_clearance:
            self.assertLess(head.x + head.radius, self.g.outline.x2)
            self.assertLess(head.x - head.radius, self.g.outline.x1)
            self.assertLessEqual(head.y + head.radius, self.g.outline.y2)


class BracketTieHoleTest(unittest.TestCase):
    def setUp(self) -> None:
        self.data = layout()
        self.g = bg.bracket_geometry(self.data)

    def test_tie_holes_sit_in_webs(self) -> None:
        self.assertEqual(len(self.g.tie_holes), len(self.g.slots) + 1)
        for tie in self.g.tie_holes:
            self.assertGreater(tie.clearance_to_edge(self.g.outline), self.g.material_to_cuts)
            for slot in self.g.slots:
                self.assertGreater(tie.clearance_to_rect(slot), 0.0,
                                   f"tie hole at x {tie.x:.2f} sits in a slot")
            for mount in self.g.mounts:
                self.assertGreater(tie.clearance_to(mount), 0.0)

    def test_tie_holes_are_between_the_slots(self) -> None:
        inside = [tie.x for tie in self.g.tie_holes
                  if any(s.x1 < tie.x < s.x2 for s in self.g.slots)]
        self.assertEqual(inside, [], "a tie hole is inside a slot")


class PowerPocketTest(unittest.TestCase):
    """Owner-approved J_PWR pocket: through, open, and it keeps the bar whole."""

    def setUp(self) -> None:
        self.data = layout()
        self.g = bg.bracket_geometry(self.data)
        self.pocket = self.g.power_pocket
        self.assertIsNotNone(self.pocket, "the J_PWR pocket is missing from the layout")

    def test_pocket_is_open_to_the_top_edge_and_inside_the_outline(self) -> None:
        o = self.g.outline
        self.assertGreaterEqual(self.pocket.y2, o.y2 - 1e-9)
        self.assertTrue(self.pocket.clamp_to(o))

    def test_pocket_opens_to_the_top_edge_and_clears_every_slot(self) -> None:
        self.assertGreaterEqual(self.pocket.y2, self.g.outline.y2 - 1e-9)
        # The 2026-11-13 header move retired the "opens into UMC AUDIO" rule.
        for slot, slot_id in zip(self.g.slots, self.g.slot_ids):
            self.assertTrue(self.pocket.x2 <= slot.x1 + 1e-9
                            or self.pocket.x1 >= slot.x2 - 1e-9,
                            f"the pocket reaches the {slot_id} slot")

    def test_pocket_keeps_the_material_rule_below_it(self) -> None:
        o = self.g.outline
        self.assertGreaterEqual(self.pocket.y1 - o.y1 + 1e-9, self.g.material_to_cuts,
                                "the bar below the pocket is too thin: it would be cut in two")
        self.assertGreater(self.pocket.x1 - o.x1, self.g.material_to_cuts)

    def test_pocket_contains_the_declared_provisional_keepout(self) -> None:
        keepout = self.g.power_keepout
        self.assertIsNotNone(keepout)
        self.assertLessEqual(self.pocket.x1, keepout.x1)
        self.assertGreaterEqual(self.pocket.x2, keepout.x2)
        self.assertLessEqual(self.pocket.y1, keepout.y1)
        # The keepout height is the declared 4.80 bound, taller than the bar.
        self.assertGreater(self.data["powerHeader"]["keepout"]["heightAboveBoard"],
                           self.g.thickness)

    def test_pocket_touches_no_tie_hole_mount_or_other_slot(self) -> None:
        for tie in self.g.tie_holes:
            self.assertGreater(tie.clearance_to_rect(self.pocket), 0.0)
        for m in self.g.mounts:
            self.assertGreater(m.clearance_to_rect(self.pocket), 0.0)
        for slot, slot_id in zip(self.g.slots, self.g.slot_ids):
            if slot_id == "UMC AUDIO USB":
                continue
            self.assertTrue(self.pocket.x2 <= slot.x1 + 1e-9
                            or self.pocket.x1 >= slot.x2 - 1e-9,
                            f"the pocket reaches the {slot_id} slot")


class PowerPocketRejectionTest(unittest.TestCase):
    """Drift the pocket and prove the rules refuse it (no silent waiver)."""

    def _with_pocket(self, **changes: float) -> dict:
        data = copy.deepcopy(layout())
        data["bracket"]["powerPocket"].update(changes)
        return data

    def test_a_pocket_that_cuts_the_bar_in_two_is_refused(self) -> None:
        data = self._with_pocket(y1=layout()["bracket"]["outline"]["y1"] + 0.5)
        with self.assertRaises(bg.GeometryError):
            bg.validate(bg.bracket_geometry(data), data)

    def test_a_pocket_eating_the_slot_web_is_refused(self) -> None:
        data = self._with_pocket(x2=4.0)
        with self.assertRaises(bg.GeometryError):
            bg.validate(bg.bracket_geometry(data), data)

    def test_a_pocket_that_misses_the_keepout_is_refused(self) -> None:
        keepout = layout()["powerHeader"]["keepout"]
        data = self._with_pocket(y1=keepout["y1"] + 1.0)
        with self.assertRaises(bg.GeometryError):
            bg.validate(bg.bracket_geometry(data), data)

    def test_a_blind_pocket_is_refused(self) -> None:
        data = self._with_pocket(y2=50.0)
        with self.assertRaises(bg.GeometryError):
            bg.validate(bg.bracket_geometry(data), data)


class CutIntersectionTest(unittest.TestCase):
    def test_no_two_cuts_intersect(self) -> None:
        g = bg.bracket_geometry(layout())
        for name_a, name_b, clearance in g.cut_clearances():
            self.assertGreater(clearance, -1e-9,
                               f"cuts {name_a} and {name_b} intersect ({clearance:.2f} mm)")


class LcdApertureTest(unittest.TestCase):
    """The aperture is expanded, with an explicit corner relief, not rounded inward."""

    def setUp(self) -> None:
        self.data = layout()
        self.cut = self.data["lcdCutout"]
        self.outline = [(p["x"], p["y"]) for p in self.data["lcdCutoutOutline"]]
        self.module = self.data["display"]

    def _segment_distance(self, px: float, py: float, ax: float, ay: float,
                          bx: float, by: float) -> float:
        dx, dy = bx - ax, by - ay
        length2 = dx * dx + dy * dy
        t = 0.0 if length2 == 0 else max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / length2))
        return math.hypot(px - (ax + t * dx), py - (ay + t * dy))

    def _polygon_distance(self, px: float, py: float) -> float:
        n = len(self.outline)
        return min(self._segment_distance(px, py, *self.outline[i], *self.outline[(i + 1) % n])
                   for i in range(n))

    def test_aperture_follows_the_glass_outline_not_the_module_outline(self) -> None:
        allowance = self.cut["allowances"]["perSide"]
        # display.width/height are the 106 x 68 MODULE outline (the drawing's
        # PCB); the aperture is derived from the 105.42 x 67.07 GLASS front-face
        # outline read off the reference STEP (owner decision D1), so the glass
        # passes through the panel and the window is LARGER than the PCB.
        self.assertAlmostEqual(self.module["width"], 106.0, places=6)
        self.assertAlmostEqual(self.module["height"], 68.0, places=6)
        self.assertAlmostEqual(self.module["glassWidth"], 105.42, places=6)
        self.assertAlmostEqual(self.module["glassHeight"], 67.07, places=6)
        self.assertAlmostEqual(self.cut["width"], self.module["glassWidth"] + 2 * allowance, places=6)
        self.assertAlmostEqual(self.cut["height"], self.module["glassHeight"] + 2 * allowance, places=6)
        # The printed bezel opening (95.54 x 54.36) is smaller than the glass,
        # so a glass-sized aperture always exposes the whole active area.
        self.assertGreater(self.cut["width"], self.module["bezelWidth"])
        self.assertGreater(self.cut["height"], self.module["bezelHeight"])

    def test_allowance_covers_fabrication_and_lens_tolerance(self) -> None:
        # 0.2 fabrication + 0.2 vendor lens OD + 0.5 unverified registration.
        self.assertGreaterEqual(self.cut["allowances"]["perSide"], 0.2 + 0.2 + 0.5 - 1e-9)

    def test_corner_relief_is_explicit_and_manufacturable(self) -> None:
        self.assertGreaterEqual(self.cut["cornerRadius"], 0.5)

    def test_aperture_edges_sit_one_allowance_outside_the_glass(self) -> None:
        half_w = self.cut["width"] / 2
        allowance = self.cut["allowances"]["perSide"]
        # The extreme right-hand vertices lie on the straight edge.
        right = max(x for x, _ in self.outline)
        self.assertAlmostEqual(right - (self.module["x"] + self.module["glassWidth"] / 2),
                               allowance, places=6)
        self.assertAlmostEqual(right, self.cut["x"] + half_w, places=6)
        # The window reveals 0.9 mm of glass edge per side and 0.61 mm of the
        # 106 mm PCB envelope: (107.22 - 106) / 2.
        self.assertAlmostEqual((self.cut["width"] - self.module["width"]) / 2, 0.61, places=6)

    def test_polygon_is_closed_and_convex_in_bounds(self) -> None:
        self.assertEqual(len(self.outline), 4 * (6 + 1))
        xs = [x for x, _ in self.outline]
        ys = [y for _, y in self.outline]
        self.assertAlmostEqual(max(xs) - min(xs), self.cut["width"], places=6)
        self.assertAlmostEqual(max(ys) - min(ys), self.cut["height"], places=6)


class RegressionTest(unittest.TestCase):
    """Take each reviewed defect and prove the current rules reject it."""

    def test_touching_slots_are_rejected(self) -> None:
        data = copy.deepcopy(layout())
        # Two 40 mm slots at x = -5 and +35 touch exactly at x = +15.
        for feedthrough in data["feedthroughs"]:
            if feedthrough["id"] in ("UMC AUDIO USB", "MIDI HUB USB"):
                feedthrough["cutoutWidth"] = 40 - 2 * data["bracketAllowance"]["slotClearancePerSide"]
        with self.assertRaises(bg.GeometryError):
            bg.validate(bg.bracket_geometry(data), data)

    def test_old_hardcoded_mount_positions_are_rejected(self) -> None:
        data = copy.deepcopy(layout())
        data["bracket"]["mountX"] = [-61.5, 40]  # the old Python constants
        with self.assertRaises(bg.GeometryError):
            bg.validate(bg.bracket_geometry(data), data)

    def test_slot_outside_the_u_opening_is_rejected(self) -> None:
        data = copy.deepcopy(layout())
        data["bracket"]["slots"][0]["x"] = -70.0  # over solid board material
        with self.assertRaises(bg.GeometryError):
            bg.validate(bg.bracket_geometry(data), data)

    def test_missing_head_clearance_is_rejected(self) -> None:
        data = copy.deepcopy(layout())
        data["bracket"]["mountHeadClearance"] = []
        with self.assertRaises(bg.GeometryError):
            bg.validate(bg.bracket_geometry(data), data)

    def test_fixed_slot_floor_at_50_is_rejected(self) -> None:
        data = copy.deepcopy(layout())
        for slot in data["bracket"]["slots"]:
            slot["yBottom"] = 50.0
        with self.assertRaises(bg.GeometryError):
            bg.validate(bg.bracket_geometry(data), data)

    def test_slot_floor_below_the_u_opening_is_rejected(self) -> None:
        data = copy.deepcopy(layout())
        for slot in data["bracket"]["slots"]:
            slot["yBottom"] = data["frontBoardOpening"]["bottom"] - 0.5
        with self.assertRaises(bg.GeometryError):
            bg.validate(bg.bracket_geometry(data), data)

    def test_tie_hole_inside_a_slot_is_rejected(self) -> None:
        data = copy.deepcopy(layout())
        data["bracket"]["tieHoles"][1]["x"] = data["feedthroughs"][1]["x"]
        with self.assertRaises(bg.GeometryError):
            bg.validate(bg.bracket_geometry(data), data)

    def test_bracket_outside_the_board_is_rejected(self) -> None:
        data = copy.deepcopy(layout())
        data["bracket"]["outline"]["x2"] = data["frontPcb"]["width"] / 2 + 2
        with self.assertRaises(bg.GeometryError):
            bg.validate(bg.bracket_geometry(data), data)


class DisplayEnvelopeTest(unittest.TestCase):
    """Separate glass and PCB envelopes, and the corrected Z chain."""

    def setUp(self) -> None:
        self.data = layout()
        self.vendor = self.data["display"]
        self.z = self.data["provisionalZ"]

    def test_module_envelope_and_active_area_are_separate(self) -> None:
        # Drawing Rev2.2: outline 106.00 x 68.00, active area 95.04 x 53.856
        # (800 x 0.1188, 480 x 0.1122), bezel opening 95.54 x 54.36.
        self.assertAlmostEqual(self.vendor["width"], 106.0, places=6)
        self.assertAlmostEqual(self.vendor["height"], 68.0, places=6)
        self.assertAlmostEqual(self.vendor["innerWidth"], 95.04, places=6)
        self.assertAlmostEqual(self.vendor["innerHeight"], 53.856, places=6)
        self.assertAlmostEqual(self.vendor["bezelWidth"], 95.54, places=6)
        self.assertAlmostEqual(self.vendor["bezelHeight"], 54.36, places=6)

    def test_depth_chain_matches_the_vendor_side_view(self) -> None:
        stack = self.vendor["stack"]
        # 14.05 mm overall (wiki FAQ), split PROVISIONALLY into 10.05 glass
        # front -> PCB rear plus a 4.0 mm boss projection. The split is
        # unmeasured and stays two named quantities.
        self.assertAlmostEqual(stack["glassFrontToPcbRear"], 10.05, places=6)
        self.assertAlmostEqual(stack["bossProjection"], 4.0, places=6)
        self.assertAlmostEqual(stack["overallDepth"], 14.05, places=6)
        self.assertNotIn("rearEnvelopeDepth", stack)

    def test_spacer_length_and_plane_separation_are_separate(self) -> None:
        stack = self.vendor["stack"]
        # The spacer now runs from the REAR board's front face (the module left
        # the front board on 2026-11-13). 12.75 - 4.00 boss = 8.75 mm.
        self.assertAlmostEqual(stack["planeSeparation"], 12.75, places=6)
        self.assertAlmostEqual(stack["spacerLength"], 8.75, places=6)
        self.assertAlmostEqual(stack["spacerLength"],
                               stack["planeSeparation"] - stack["bossProjection"], places=6)

    def test_mounting_pattern_follows_the_drawing_insets(self) -> None:
        # 25.62 / 22.38 and 9.50 / 9.46 edge insets: 58.00 x 49.04, asymmetric
        # top/bottom, so the offsets are derived from the insets, never mirrored.
        module = self.data["display"]
        # 2026-11-13: -10.0 moves the module 0.5 mm left so the encoder/NAV pad
        # fields clear the new display opening by 0.66 mm.
        self.assertAlmostEqual(module["x"], -10.0, places=6)
        self.assertAlmostEqual(module["y"], 11.0, places=6)
        datum = self.data["displayDatum"]
        self.assertIn("display-4.3-drawing.png", datum["sourceDrawing"])
        self.assertTrue(datum["firstArticlePending"])

    def test_z_values_follow_the_standoff_datum(self) -> None:
        stack = self.vendor["stack"]
        # Assumption A: the glass front is flush with the panel front face,
        # which the glass-sized aperture allows (D1).
        self.assertAlmostEqual(self.z["displayGlassFront"], 0.0, places=6)
        self.assertAlmostEqual(self.z["displayPcbRear"], 10.05, places=6)
        self.assertAlmostEqual(self.z["displayRearMost"], 14.05, places=6)
        # The panel-to-board distance is the owner's 10.0 mm input; the display
        # hangs from the REAR board's front face, so the chain starts there:
        # rearBoardFront - displayRearMost = the 8.75 mm carrier spacer.
        self.assertAlmostEqual(self.z["frontBoardTop"], 10.0, places=6)
        self.assertAlmostEqual(self.z["rearBoardFront"] - self.z["displayRearMost"],
                               stack["spacerLength"], places=6)
        self.assertAlmostEqual(self.data["panelToFrontBoardGap"], 8.4, places=6)
        self.assertAlmostEqual(self.data["displayUndersideGap"], -4.05, places=6)
        self.assertAlmostEqual(self.data["displayUndersideGap"],
                               self.z["frontBoardTop"] - self.z["displayRearMost"], places=6)

    def test_the_light_pipe_model_is_the_pgn25_candidate(self) -> None:
        """The retired 10.16 / 7.29 subtraction must not come back."""
        reach = self.data["lightPipeReach"]
        pipe = self.data["lightPipe"]
        self.assertNotIn("unaccounted", reach)
        self.assertIn("PGN25L165", pipe["part"])
        self.assertAlmostEqual(pipe["shaftLength"], 16.5, places=6)
        self.assertAlmostEqual(pipe["overallLength"], 17.5, places=6)
        self.assertAlmostEqual(reach["nominalTipZ"], pipe["shaftLength"], places=6)
        self.assertAlmostEqual(reach["nominalTipToBoardPlane"], -6.5, places=6)
        self.assertIsNone(reach["opticalGap"])
        self.assertTrue(reach["status"].startswith("incomplete"))
        self.assertNotIn("seatAllowance", pipe)


class ProvisionalDataTest(unittest.TestCase):
    """Unverified numbers must stay visible, and must not read as measurements."""

    def test_z_stack_carries_a_datum_and_an_assumption(self) -> None:
        data = layout()
        self.assertEqual(data["provisionalZ"]["panelFront"], 0)
        self.assertEqual(data["provisionalZ"]["panelRear"], data["panel"]["thickness"])
        self.assertAlmostEqual(data["panelToFrontBoardGap"],
                               data["provisionalZ"]["frontBoardTop"] - data["provisionalZ"]["panelRear"],
                               places=6)

    def test_light_pipe_reach_is_recorded_as_pending(self) -> None:
        data = layout()
        reach = data["lightPipeReach"]
        # One datum: the tip plane is the SHAFT length, and the board plane is
        # provisional. The step is arithmetic, never an LED gap.
        self.assertAlmostEqual(reach["nominalTipZ"], data["lightPipe"]["shaftLength"], places=6)
        self.assertAlmostEqual(reach["boardNearFaceZ"], data["provisionalZ"]["frontBoardTop"],
                               places=6)
        self.assertAlmostEqual(reach["nominalTipToBoardPlane"],
                               reach["boardNearFaceZ"] - reach["nominalTipZ"], places=6)
        self.assertIsNone(reach["opticalGap"])
        self.assertTrue(reach["status"].startswith("incomplete"))
        self.assertTrue(reach["missing"])
        # No LED height or seat allowance may be invented here.
        self.assertNotIn("seatAllowance", data["lightPipe"])

    def test_display_underside_gap_is_not_a_clearance_claim(self) -> None:
        data = layout()
        gap = data["displayUndersideGap"]
        tallest = data["frontTopSmdMaxHeight"]["height"]
        # -4.05 mm to the connector rear-most point: the module's rear 4.05 mm
        # sits BEHIND the front board's top face, which is why the board carries
        # an opening. Still pending: the display underside map, the FPC latch and
        # the module tolerance.
        self.assertAlmostEqual(gap - tallest, -6.25, places=6)
        self.assertEqual(data["frontTopSmdMaxHeight"]["height"], 2.2)

    def test_depth_chain_stays_inside_the_owner_target(self) -> None:
        data = layout()
        z, depth = data["provisionalZ"], data["provisionalDepth"]
        # The Pi left the module for the 20HP Pi mounting panel (owner decision
        # 2026-11-12), so the module chain ends at the socketed Pico stack.
        total = (z["frontBoardRear"] + depth["frontToRearGap"] + data["rearPcb"]["thickness"]
                 + depth["picoSocketStack"] + depth["harnessAllowance"])
        self.assertLessEqual(total, data["depthTarget"])
        # Both module chains sit inside the Eurorack Go's 62 mm upper row on
        # the candidate values, and the Pi panel is over the 40 mm lower row
        # with the NVMe HAT candidate stack. DESIGN CHECKS, not fit approvals.
        go = data["eurorackGoMaxModuleDepth"]
        self.assertLessEqual(total - data["panel"]["thickness"], go["upperRow"])
        pi = data["piPanelProvisionalDepth"]
        pi_depth = pi["piStandoffAndPcb"] + pi["piStackRearward"] + pi["harnessAllowance"]
        self.assertLessEqual(pi_depth, go["upperRow"])
        self.assertGreater(pi_depth, go["lowerRow"])
        # Both boards share a centre. 2026-11-13: the front board grew to 150 mm
        # and the rear board to 148 mm, so the front overhangs by 1.0 mm per side.
        self.assertAlmostEqual((data["frontPcb"]["width"] - data["rearPcb"]["width"]) / 2, 1.0, places=6)


if __name__ == "__main__":
    unittest.main()

#!/usr/bin/env python3
"""The viewer manifest must place the stack where the layout says it is.

    cd midibrain && python3 -m unittest discover -s mechanical -p 'test_*.py'

Why this test exists
--------------------
`mechanical/viewer/serve_manifest.py` is the only place that converts a board's
own STEP frame into the panel frame for the browser viewer, and the viewer's own
frame is a flip of the panel frame (scene z = -panel z). Both conversions are
easy to get wrong in a way nothing else notices: a board shown at the wrong z
still loads, still rotates and still looks like a stack.

So this test runs the generator on a throw-away root and pins the placement
against `mechanical/layout.json`: the front board on the merged 10.60…12.20 mm
band, the rear board on 23.40…25.00, the panel on 0…1.60, and the display
carrier's placeholder across the derived 9.35 mm gap. The numbers are read from
the layout, never typed twice.
"""
from __future__ import annotations

import hashlib
import json
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
PROJECT = HERE.parent
SERVE = HERE / "viewer" / "serve_manifest.py"


def run_manifest(models: list[str], reference: dict | None = None) -> dict:
    """Generate the manifest for a temporary root holding the real layout.

    `reference` optionally writes a vendor STEP under mechanical/reference/ and a
    source JSON beside it. With `reference["record_sha"]=True` the recorded
    SHA-256 is the file's own, which is the VERIFIED branch; otherwise the
    recorded hash is deliberately wrong and the generator must fall back to the
    unverified placement.
    """
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        (root / "mechanical").mkdir(parents=True)
        shutil.copy(PROJECT / "mechanical" / "layout.json",
                    root / "mechanical" / "layout.json")
        directory = root / "mechanical" / "viewer" / "models"
        directory.mkdir(parents=True)
        for stem in models:
            (directory / f"{stem}.step").write_text("ISO-10303-21;\n", encoding="utf-8")
        if reference is not None:
            ref_dir = root / "mechanical" / "reference"
            ref_dir.mkdir(parents=True)
            step = ref_dir / "waveshare-4.3inch-dsi-lcd.step"
            step.write_bytes(reference["bytes"])
            recorded = (hashlib.sha256(step.read_bytes()).hexdigest()
                        if reference["record_sha"] else "0" * 64)
            (ref_dir / "waveshare-4.3inch-dsi-lcd.source.json").write_text(
                json.dumps({"step_sha256": recorded}), encoding="utf-8")
        out = root / "manifest.json"
        result = subprocess.run([sys.executable, str(SERVE), str(root), str(out)],
                                capture_output=True, text=True, check=False)
        if result.returncode != 0:
            raise AssertionError(f"serve_manifest.py failed: {result.stderr}")
        return json.loads(out.read_text(encoding="utf-8"))


class ManifestPlacementTest(unittest.TestCase):
    """Every board lands on the panel band the layout declares."""

    @classmethod
    def setUpClass(cls) -> None:
        cls.layout = json.loads((PROJECT / "mechanical" / "layout.json").read_text())
        cls.manifest = run_manifest(["panel", "front", "rear"])

    def by_label(self) -> dict:
        return {entry["label"].split(": ")[-1].split(".step")[0]: entry
                for entry in self.manifest["models"]}

    def test_each_board_sits_on_its_own_panel_band(self) -> None:
        z = self.layout["provisionalZ"]
        thickness = self.layout["frontPcb"]["thickness"]
        half = thickness / 2.0
        expected = {
            "panel": ("panelFront", z["panelFront"]),
            "front": ("frontBoardTop", z["frontBoardTop"]),
            "rear": ("rearBoardFront", z["rearBoardFront"]),
        }
        entries = self.by_label()
        self.assertEqual(sorted(entries), ["front", "panel", "rear"])
        for stem, (key, top) in expected.items():
            entry = entries[stem]
            self.assertAlmostEqual(entry["panelTopZ"], top, places=3,
                                   msg=f"{stem} is not on {key}")
            # The model is centred on the board's mid-plane with +z out of its
            # parts side, so it carries a z flip and a half-thickness offset.
            self.assertEqual(entry["scaleZ"], -1, stem)
            self.assertAlmostEqual(entry["positionZ"], -(top + half), places=3)

    def test_the_merged_bands_are_the_ones_the_docs_state(self) -> None:
        z = self.layout["provisionalZ"]
        self.assertAlmostEqual(z["frontBoardTop"], 10.60, places=6)
        self.assertAlmostEqual(z["frontBoardRear"], 12.20, places=6)
        self.assertAlmostEqual(z["rearBoardFront"], 23.40, places=6)
        self.assertAlmostEqual(z["rearBoardRear"], 25.00, places=6)
        # In the viewer's flipped frame the panel is nearest the camera and the
        # stack recedes: 0…-1.60, -10.60…-12.20, -23.40…-25.00.
        entries = self.by_label()
        self.assertAlmostEqual(entries["panel"]["positionZ"] + 0.8, 0.0, places=3)
        self.assertAlmostEqual(entries["front"]["panelTopZ"], 10.60, places=3)
        self.assertAlmostEqual(entries["rear"]["panelTopZ"], 23.40, places=3)

    def test_the_carrier_placeholder_spans_the_derived_gap(self) -> None:
        carrier = self.manifest["carrier"]
        gap = carrier["rearBoardFront"] - carrier["moduleRear"]
        self.assertAlmostEqual(gap, 8.75, places=6)
        self.assertAlmostEqual(gap, self.layout["displayStandoff"]["spacerLength"], places=6)
        self.assertAlmostEqual(carrier["moduleRear"], self.layout["provisionalZ"]["displayRearMost"],
                               places=6)
        self.assertEqual(len(carrier["bosses"]), 4)

    def test_the_manifest_names_the_boards_it_places(self) -> None:
        # A stale stem (a renamed export) would silently drop a board from the
        # stack instead of showing it somewhere wrong.
        self.assertEqual(sorted(entry["label"] for entry in self.manifest["models"]),
                         ["viewer: front.step", "viewer: panel.step", "viewer: rear.step"])


class DisplayAlignmentTest(unittest.TestCase):
    """The display datum is nonzero, and SCENE z is the NEGATIVE of panel z.

    The regression this pins: `frontSceneZ` used to be assigned from the
    positive panel-frame `displayGlassFront`, so the module was drawn 0.60 mm on
    the wrong side of the panel front. With the selected glass at panel z +0.60
    the scene value must be -0.60, and for the VERIFIED vendor STEP the bbox
    front is the touch/frame plane 1.90 mm forward of the glass, i.e. scene
    z +1.30.
    """

    @classmethod
    def setUpClass(cls) -> None:
        cls.layout = json.loads((PROJECT / "mechanical" / "layout.json").read_text())

    def display_entry(self, manifest: dict) -> dict:
        for entry in manifest["models"]:
            if entry["label"].startswith("reference:"):
                return entry
        raise AssertionError("the reference model is missing from the manifest")

    def test_the_glass_datum_is_nonzero_and_flips_sign(self) -> None:
        glass = self.layout["provisionalZ"]["displayGlassFront"]
        self.assertAlmostEqual(glass, 0.60, places=6)  # NONZERO on purpose
        entry = self.display_entry(run_manifest([], reference={"bytes": b"vendor", "record_sha": True}))
        self.assertAlmostEqual(entry["align"]["glassPanelZ"], 0.60, places=6)
        self.assertAlmostEqual(entry["align"]["glassSceneZ"], -0.60, places=6)
        # The bug this guards: the scene value must not equal the panel value.
        self.assertNotAlmostEqual(entry["align"]["glassSceneZ"], glass, places=6)

    def test_the_verified_model_is_placed_on_its_frame_plane(self) -> None:
        entry = self.display_entry(run_manifest([], reference={"bytes": b"vendor", "record_sha": True}))
        self.assertTrue(entry["align"]["modelVerified"])
        self.assertFalse(entry["align"]["bboxFrontIsGlass"])
        # 1.90 mm of touch/frame stands in front of the glass: panel z -1.30,
        # scene z +1.30.
        self.assertAlmostEqual(entry["align"]["frameAheadOfGlassMm"], 1.90, places=6)
        self.assertAlmostEqual(entry["align"]["frontSceneZ"], 1.30, places=6)
        self.assertIn("touch/frame", entry["align"]["placementStatus"])
        # The in-plane rotation stays a CANDIDATE; no orientation is claimed.
        self.assertIn("CANDIDATE", entry["align"]["orientation"]["status"])

    def test_an_unverified_step_gets_no_frame_offset(self) -> None:
        # A STEP whose hash is not the recorded one must NOT get the 1.90 mm
        # offset, and the manifest must say the glass plane is unproven.
        entry = self.display_entry(run_manifest([], reference={"bytes": b"other", "record_sha": False}))
        self.assertFalse(entry["align"]["modelVerified"])
        # UNKNOWN, not "yes": nothing establishes that the bbox front is glass.
        self.assertIsNone(entry["align"]["bboxFrontIsGlass"])
        self.assertAlmostEqual(entry["align"]["frameAheadOfGlassMm"], 0.0, places=6)
        self.assertAlmostEqual(entry["align"]["frontSceneZ"], -0.60, places=6)
        self.assertIn("UNVERIFIED", entry["align"]["placementStatus"])

    def test_the_offset_comes_from_the_verified_model_constants(self) -> None:
        # Not retyped: the generator imports these planes from step_geometry.
        sys.path.insert(0, str(PROJECT / "mechanical"))
        from step_geometry import VENDOR_DISPLAY_MODEL
        self.assertAlmostEqual(
            VENDOR_DISPLAY_MODEL["touch_frame_front_z"] - VENDOR_DISPLAY_MODEL["glass_front_z"],
            1.90, places=6)


class ViewerGuardTest(unittest.TestCase):
    """Source-level guards on the browser viewer and its manifest.

    There is no JS test harness in this repo, so these pin the few decisions that
    are cheap to lose and expensive to get wrong. Each assertion names the
    behaviour it protects; a comment or a console line is not enough.
    """

    MAIN_JS = (PROJECT / "mechanical" / "viewer" / "main.js").read_text(encoding="utf-8")
    README = (PROJECT / "mechanical" / "viewer" / "README.md").read_text(encoding="utf-8")

    def test_the_encoder_correction_is_refused_without_a_board(self) -> None:
        self.assertIn("if (!model.board)", self.MAIN_JS)
        self.assertIn("encoder correction refused", self.MAIN_JS)
        self.assertIn("carriesFrontBoard === false", self.MAIN_JS)

    def test_the_assembly_manifest_entry_names_the_front_board(self) -> None:
        # cad-out/assembly.step carries the front board; the manifest is what
        # lets the viewer correct the encoder there.
        entry = self.assembly_entry()
        self.assertEqual(entry["board"]["kind"], "front")
        self.assertTrue(entry["board"]["carriesFrontBoard"])
        self.assertEqual(entry["board"]["width"], 150)

    def test_no_encoder_model_is_a_failure(self) -> None:
        # "not-applicable" must never be counted as a failure.
        self.assertIn('e.status === "guard-failed"', self.MAIN_JS)
        self.assertIn('e.status === "ambiguous"', self.MAIN_JS)
        self.assertNotIn("readout.encoders.filter((e) => e.applied && !e.ok)", self.MAIN_JS)

    def test_a_failed_guard_reverts_the_shift(self) -> None:
        self.assertIn("shift reverted", self.MAIN_JS)
        self.assertIn("mesh.position.z = originalZ", self.MAIN_JS)

    def test_warnings_survive_the_board_list(self) -> None:
        # renderToggles() clears #models, so the warning must not live there.
        self.assertIn('document.getElementById("hud").appendChild(el)', self.MAIN_JS)
        self.assertNotIn('document.getElementById("models").prepend(el)', self.MAIN_JS)

    def test_the_placed_box_is_read_after_placement(self) -> None:
        self.assertIn("const placed = new THREE.Box3().setFromObject(group)", self.MAIN_JS)
        self.assertNotIn("placedBboxFrontSceneZ: +b.max.z", self.MAIN_JS)

    def test_the_readme_does_not_claim_a_demonstrated_fit(self) -> None:
        self.assertIn("makes **no physical-fit claim whatsoever**", self.README)
        self.assertIn("UNRESOLVED", self.README)
        self.assertIn("21.50", self.README)
        self.assertIn("27.50", self.README)

    def test_the_readme_states_the_orientation_caveat(self) -> None:
        self.assertIn("candidate only", self.README)
        self.assertIn("pending", self.README)

    def assembly_entry(self) -> dict:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "mechanical" / "cad-out").mkdir(parents=True)
            (root / "mechanical" / "layout.json").write_text(
                (PROJECT / "mechanical" / "layout.json").read_text(encoding="utf-8"),
                encoding="utf-8")
            (root / "mechanical" / "cad-out" / "assembly.step").write_text(
                "ISO-10303-21;\n", encoding="utf-8")
            out = root / "manifest.json"
            result = subprocess.run([sys.executable, str(SERVE), str(root), str(out)],
                                    capture_output=True, text=True, check=False)
            if result.returncode != 0:
                raise AssertionError(result.stderr)
            manifest = json.loads(out.read_text(encoding="utf-8"))
        for entry in manifest["models"]:
            if entry["label"].startswith("assembly:"):
                return entry
        raise AssertionError("no assembly entry")


if __name__ == "__main__":
    unittest.main()

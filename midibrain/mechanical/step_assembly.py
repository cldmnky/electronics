#!/usr/bin/env python3
"""Build the MIDI Brain STEP verification assembly and the printable mockups.

Everything comes from ``mechanical/layout.json`` (generated from
``tscircuit/lib/layout.ts``) plus the vendor drawing dimensions recorded in
``mechanical/reference/``. Nothing here is a released drawing, a fit approval or
a measurement: the parts are named after what is and is not known, and the
assumptions file next to the exports states the unknowns.

Outputs, all written to one directory (default ``mechanical/cad-out``, set it
with ``--out``; the script never deletes anything and refuses a directory that
holds files it does not write):

    midibrain-step-assembly.step   named solids: panel, front board, rear board,
                                   display (vendor STEP when available), flange
                                   and FPC envelopes, optional USB-C candidate.
                                   The front and rear board solids are STALE -
                                   see the board mockups below
    mockup-panel.stl               1:1 panel mockup (its 20 apertures and 14
                                   drills are validated against the canonical
                                   feature plan: still a print check, not a fit)
    mockup-front-board.stl         DO NOT PRINT / DO NOT USE FOR FIT - its cuts are
                                   not the current PCB: an obsolete notch
                                   rectangle instead of the U opening, four Ø4.3 mm
                                   holes at retired LCD positions, the four old
                                   standoff holes (-61,-53) (61,-53) (-61,53)
                                   (57,41) and the two bracket holes (-66.5,53)
                                   (55,53) that this board does not have (raised by
                                   the 2026-11-14 review; see docs/TODO.md)
    mockup-rear-board.stl          DO NOT PRINT / DO NOT USE FOR FIT - it lacks the
                                   four Ø2.7 mm LCD carrier holes the current board
                                   carries (same review)
    scale-check-100mm.stl          100.00 x 10.00 x 3.00 mm printer scale bar
    midibrain-pi-panel.step        the 20HP Pi mounting panel and a placeholder
                                   Pi 5 board behind it (its own frame)
    mockup-pi-panel.stl            1:1 Pi mounting panel mockup
    light-pipe-reach-gauge.stl     same-datum step gauge on the two NOMINAL
                                   planes: the PGN25L165 tip (16.5 mm) and the
                                   provisional board face (10.6 mm), in either
                                   order - tall 16.5, low 10.6, step 5.9 and the
                                   SIGNED board-minus-tip -5.9 (the tip plane lies
                                   behind the board face). NOT an LED gap and NOT
                                   a fit (see reach_gauge_dimensions)
    assumptions.json               provenance, unknowns and pair verdicts
    print-checklist.md             the physical print check

CAD exports are generated files outside git: ``*.step`` and ``*.stl`` are
ignored by ``midibrain/.gitignore``. Never commit them.

CadQuery, in a private environment (never the system Python):

    uv venv /tmp/cq-venv
    uv pip install --python /tmp/cq-venv/bin/python cadquery
    /tmp/cq-venv/bin/python mechanical/step_assembly.py --out /tmp/midibrain-cad

The OpenCascade kernel links against the platform OpenGL libraries even for a
geometry-only run. If CadQuery reports ``ImportError: libGL.so.1``, unpack the
distribution packages in a local prefix and point ``LD_LIBRARY_PATH`` at it. Do
not substitute a stub library: it hides a broken environment.

The vendor display STEP is not in the checkout (third-party binary, gitignored).
The script verifies the vendor data itself, against the hashes recorded in
``mechanical/reference/waveshare-4.3inch-dsi-lcd.source.json``, and STOPS on a
mismatch. Two ways in:

    curl -sSLO https://www.waveshare.net/w/upload/2/28/4.3inch_DSI_LCD_Drawing.zip
    # checks the archive hash AND the STEP member hash inside it:
    mechanical/step_assembly.py --display-archive 4.3inch_DSI_LCD_Drawing.zip
    # or, for an already-extracted file, checks the STEP hash only:
    mechanical/step_assembly.py --display-step 4_3inch-dsi-lcd-V1.stp

Without either, the display is a named provisional envelope and the metadata says
so. Neither form writes the third-party binary into the tree: the extracted STEP
goes to a private temporary directory.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import sys
import tempfile
import zipfile
from pathlib import Path

from bracket_geometry import GeometryError, load_layout
from light_pipe_reach import ReachInputs, compute_reach
from step_geometry import (
    BOARD_MOCKUP_DEFECTS,
    BOARD_MOCKUP_DISCLAIMER,
    BOARD_MOCKUP_REPAIR,
    DISPLAY_MOUNT_PLACEHOLDER,
    DISPLAY_ORIENTATION,
    VENDOR_DISPLAY_MODEL,
    assembly_envelopes,
    assumptions_document,
    check_display_plane_order,
    display_mount_gap,
    display_rotation,
    inner_pattern_candidates,
    placed_display_planes,
    print_checklist_markdown,
    resolve_output_dir,
    rotation_determinant,
    vendor_model_separations,
)

HERE = Path(__file__).resolve().parent
SOURCE_JSON = HERE / "reference" / "waveshare-4.3inch-dsi-lcd.source.json"
DEFAULT_DISPLAY_STEP = HERE / "reference" / "waveshare-4.3inch-dsi-lcd.step"

STEP_NAME = "midibrain-step-assembly.step"
STL_PANEL = "mockup-panel.stl"
STL_FRONT = "mockup-front-board.stl"
STL_REAR = "mockup-rear-board.stl"
STL_SCALE = "scale-check-100mm.stl"
STL_GAUGE = "light-pipe-reach-gauge.stl"
PI_PANEL_STEP = "midibrain-pi-panel.step"
STL_PI_PANEL = "mockup-pi-panel.stl"
PI_PANEL_BODY = "PI_PANEL_20HP_3U"
PI_BOARD_BODY = "PI5_BOARD_PLACEHOLDER_NON_APPROVED"
COINCIDENCE_TOL_MM = 0.01
ASSUMPTIONS = "assumptions.json"
CHECKLIST = "print-checklist.md"

VOLUME_EPS = 1e-6


def fail(message: str) -> None:
    print(f"FAIL {message}", file=sys.stderr)
    raise SystemExit(1)


def volume_of(shape) -> float:
    """Volume of a Workplane or a raw Shape, in mm^3."""
    return shape.val().Volume() if hasattr(shape, "val") else shape.Volume()


def sha256_of(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def resolve_display_step(explicit: str | None) -> tuple[Path | None, dict]:
    """Return the verified vendor STEP, or (None, info) when it is absent.

    A file that does not match the recorded SHA-256 is never used: the model
    would silently change the geometry the panel aperture was measured from.
    """
    source = json.loads(SOURCE_JSON.read_text()) if SOURCE_JSON.is_file() else {}
    expected = source.get("step_sha256")
    info = {
        "path": None,
        "status": "absent",
        "step_sha256_expected": expected,
        "step_sha256_actual": None,
        "source": source.get("archive_url"),
    }
    path = Path(explicit) if explicit else DEFAULT_DISPLAY_STEP
    if not path.is_file():
        info["note"] = (
            f"{path} is not in the tree; download {source.get('archive_url')} and pass "
            "--display-step. The display is modelled as a provisional envelope.")
        return None, info
    actual = sha256_of(path)
    info["step_sha256_actual"] = actual
    if expected and actual != expected:
        raise GeometryError(
            f"{path} sha256 {actual} does not match the recorded {expected}; "
            "refusing to build geometry from an unverified vendor model")
    info["path"] = str(path)
    info["status"] = "verified"
    info["note"] = "vendor STEP, SHA-256 verified against the recorded source"
    return path, info


def resolve_display_archive(archive: str) -> tuple[Path, dict]:
    """Verify the vendor ZIP and its STEP member, then extract the STEP.

    Two SHA-256 checks, both against
    ``mechanical/reference/waveshare-4.3inch-dsi-lcd.source.json``: the archive
    itself and the ``.stp`` member inside it. A mismatch on either stops the
    build - the panel aperture was measured from that model, so a different file
    would change the geometry the model is checked against. The extraction goes
    to a private temporary directory; the third-party binary is never committed.
    """
    source = json.loads(SOURCE_JSON.read_text())
    archive_sha = source.get("archive_sha256")
    step_sha = source.get("step_sha256")
    member_name = source.get("original_filename")
    path = Path(archive)
    info = {
        "archive": str(path),
        "archive_sha256_expected": archive_sha,
        "archive_sha256_actual": None,
        "step_sha256_expected": step_sha,
        "step_sha256_actual": None,
        "member": member_name,
    }
    if not path.is_file():
        raise GeometryError(f"{path} is not a file")
    actual_archive = sha256_of(path)
    info["archive_sha256_actual"] = actual_archive
    if not archive_sha or actual_archive != archive_sha:
        raise GeometryError(
            f"{path} sha256 {actual_archive} does not match the recorded archive hash "
            f"{archive_sha}; refusing to use an unverified vendor archive")
    with zipfile.ZipFile(path) as zf:
        members = [n for n in zf.namelist() if Path(n).name == member_name]
        if not members:
            raise GeometryError(f"{path} does not contain {member_name}")
        data = zf.read(members[0])
    actual_step = hashlib.sha256(data).hexdigest()
    info["step_sha256_actual"] = actual_step
    if not step_sha or actual_step != step_sha:
        raise GeometryError(
            f"{member_name} sha256 {actual_step} does not match the recorded STEP hash "
            f"{step_sha}; refusing to build geometry from an unverified vendor model")
    scratch = Path(tempfile.mkdtemp(prefix="midibrain-display-step-"))
    step_path = scratch / member_name
    step_path.write_bytes(data)
    info["extracted_to"] = str(step_path)
    info["status"] = "verified"
    info["note"] = ("vendor archive AND its STEP member, both SHA-256 verified against "
                    "the recorded source")
    return step_path, info


def planar_z_planes(cq, shape) -> dict[float, dict]:
    """Planar faces whose normal is +-Z, grouped by their Z plane."""
    planes: dict[float, dict] = {}
    for face in shape.Faces():
        if face.geomType() != "PLANE":
            continue
        if abs(abs(face.normalAt().z) - 1.0) > 1e-6:
            continue
        key = round(face.Center().z, 4)
        fb = face.BoundingBox()
        entry = planes.setdefault(key, {"area": 0.0, "x": [1e9, -1e9], "y": [1e9, -1e9]})
        entry["area"] += face.Area()
        entry["x"] = [min(entry["x"][0], fb.xmin), max(entry["x"][1], fb.xmax)]
        entry["y"] = [min(entry["y"][0], fb.ymin), max(entry["y"][1], fb.ymax)]
    return planes


def boss_centres(shape) -> list[tuple[float, float]]:
    """Centres of the 5.5 mm SMT standoff bodies (the mounting bosses)."""
    out: list[tuple[float, float]] = []
    for solid in shape.Solids():
        b = solid.BoundingBox()
        if 5.0 <= b.xlen <= 6.0 and 5.0 <= b.ylen <= 6.0 and 5.0 <= b.zlen <= 6.0:
            c = solid.Center()
            out.append((round(c.x, 4), round(c.y, 4)))
    return sorted(out)


def check_vendor_model(shape) -> dict:
    """Assert the imported model IS the model the recorded numbers describe.

    The SHA-256 pins the file; this pins the geometry the placement depends on,
    so a substituted or re-exported file cannot silently move the display.
    """
    b = shape.BoundingBox()
    m = VENDOR_DISPLAY_MODEL
    problems = []
    if abs(b.zmin - m["bbox_z_mm"][0]) > 0.02 or abs(b.zmax - m["bbox_z_mm"][1]) > 0.02:
        problems.append(f"z bbox is {b.zmin:.3f}..{b.zmax:.3f}, expected "
                        f"{m['bbox_z_mm'][0]:.3f}..{m['bbox_z_mm'][1]:.3f}")
    planes = planar_z_planes(None, shape)
    for name, key in (("glass front", "glass_front_z"), ("touch/frame", "touch_frame_front_z"),
                      ("PCB rear", "pcb_rear_z")):
        z = round(m[key], 4)
        if z not in planes:
            problems.append(f"no planar face at the {name} plane z={z}")
    if round(m["glass_front_z"], 4) in planes:
        g = planes[round(m["glass_front_z"], 4)]
        if abs((g["x"][1] - g["x"][0]) - 105.42) > 0.05 or abs((g["y"][1] - g["y"][0]) - 67.07) > 0.05:
            problems.append("the glass plane is not 105.42 x 67.07 mm")
    bosses = boss_centres(shape)
    if len(bosses) != 8:
        problems.append(f"expected 8 SMT bosses, found {len(bosses)}")
    if problems:
        raise GeometryError("the vendor display model does not match its recorded planes: "
                            + "; ".join(problems))
    return {"bbox": [b.xlen, b.ylen, b.zlen], "bosses": len(bosses)}


def _placed_note(layout: dict, found: dict, subject: str) -> str:
    """`subject` with its PLACED panel Z and its side of the panel front."""
    panel = float(layout["provisionalZ"]["panelFront"])
    z = float(found["glass_front"])
    side = "BEHIND" if z > panel else ("IN FRONT of" if z < panel else "flush with")
    return (f"{subject}, panel Z = {z:+.2f}: {abs(z - panel):.2f} mm {side} the panel front "
            f"face (Z = {panel:+.2f}). Illustrative placement, not a fit")


def _ahead_of_panel(layout: dict, found: dict) -> float:
    """How far the placed touch/frame plane stands in FRONT of the panel front."""
    panel = float(layout["provisionalZ"]["panelFront"])
    return round(panel - float(found["touch_frame_front"]), 3)


def place_display(cq, shape, layout: dict) -> tuple[object, dict]:
    """Place the vendor model: glass front at the layout's display datum, electronics rearward.

    Z is forced: the vendor +Z points out of the glass, the layout +Z points
    rearward, so any placement must reverse Z. The IN-PLANE AXIS IS NOT SETTLED
    (`step_geometry.DISPLAY_ORIENTATION`, status PENDING). The model is drawn
    with the 180 degree Y candidate because a drawing must pick one; the 180
    degree X candidate is recorded as an equal alternative. X and Y are then set
    from source geometry: the outer SMT boss pattern centre, which equals the PCB
    outline centre, is moved onto the module centre in lib/layout.ts. That
    pattern is symmetric, so it fixes the centre only.
    """
    check_vendor_model(shape)
    d = layout["display"]
    m = VENDOR_DISPLAY_MODEL

    rotated = shape.rotate((0, 0, 0), (0, 1, 0), 180)
    planes = planar_z_planes(cq, rotated)
    # After the rotation the glass front plane sits at -(vendor glass z).
    glass_z_rotated = round(-m["glass_front_z"], 4)
    if glass_z_rotated not in planes:
        raise GeometryError(
            f"the rotated model has no glass plane at z={glass_z_rotated}; refusing to place it")
    # PROVISIONAL_Z.displayGlassFront is derived from the rear-board carrier
    # chain; drawing the model there keeps this assembly on the layout datum.
    dz = layout["provisionalZ"]["displayGlassFront"] - glass_z_rotated

    bosses = boss_centres(rotated)
    if len(bosses) != 8:
        raise GeometryError(f"expected 8 SMT bosses after rotation, found {len(bosses)}")
    xs = sorted({b[0] for b in bosses})
    ys = sorted({b[1] for b in bosses})
    # The outer pattern is the widest pair in each axis and is symmetric: its
    # centre is the module centre.
    outer = [(x, y) for x, y in bosses
             if abs(abs(x) - m["mount_pattern_mm"][0] / 2) < 0.02]
    if len(outer) != 4:
        raise GeometryError(
            f"could not identify the four outer boss positions, found {outer}")
    cx = sum(p[0] for p in outer) / 4
    cy = sum(p[1] for p in outer) / 4
    if abs(abs(cx) - 0.0) > 0.02:
        raise GeometryError(f"the outer boss pattern x centre is {cx:.3f}, expected 0")
    if abs((max(xs) - min(xs)) - m["mount_pattern_mm"][0]) > 0.05 or \
            abs((max(ys) - min(ys)) - m["mount_pattern_mm"][1]) > 0.05:
        raise GeometryError("the rotated boss pattern is not 99.30 x 61.80 mm")
    dx = d["x"] - cx
    dy = d["y"] - cy
    placed = rotated.translate((dx, dy, dz))

    # ---- verification on the placed solid ----
    expected = placed_display_planes(layout)
    check_display_plane_order(expected)
    placed_planes = planar_z_planes(cq, placed)
    found = {}
    for name, key in (("glass_front", "glass_front_z"), ("glass_rear", "glass_rear_z"),
                      ("touch_frame_front", "touch_frame_front_z"),
                      ("pcb_front", "pcb_front_z"), ("pcb_rear", "pcb_rear_z"),
                      ("boss_rear", "boss_rear_z"), ("rear_most", "rear_most_z")):
        z = round(expected[name], 3)
        match = [p for p in placed_planes if abs(p - z) < 0.02]
        if not match and name != "rear_most":
            raise GeometryError(
                f"the placed model has no plane at {name} z={z}: the placement moved it")
        found[name] = z

    placed_bosses = [(round(x, 3), round(y, 3)) for x, y in boss_centres(placed)]
    expected_mounts = [(round(h["x"], 3), round(h["y"], 3)) for h in layout["lcdMountHoles"]]
    for mount in expected_mounts:
        nearest = min(placed_bosses, key=lambda b: (b[0] - mount[0]) ** 2 + (b[1] - mount[1]) ** 2)
        if max(abs(nearest[0] - mount[0]), abs(nearest[1] - mount[1])) > 0.05:
            raise GeometryError(
                f"boss {mount} is {nearest} after placement: the mounting pattern does not "
                "land on the front-board hole pattern")

    # The inner plain-hole pattern is asymmetric, so it distinguishes the two
    # candidate rotations. Matching the recorded DISPLAY_STANDOFFS proves only
    # that the modelled candidate is consistent with the layout's own no-flip
    # premise - NOT that the axis is correct. Both candidate patterns are
    # recorded; the status stays PENDING.
    pattern = inner_pattern_candidates()
    used = "about_y"
    expected_inner = sorted((round(d["x"] + o["x"], 3), round(d["y"] + o["y"], 3))
                            for o in layout["displayStandoffs"])
    modelled_inner = sorted((round(d["x"] + x, 3), round(d["y"] + y, 3))
                            for x, y in pattern["candidates"][used])
    premise_consistent = len(modelled_inner) == 4 and all(
        max(abs(a[0] - b[0]), abs(a[1] - b[1])) <= 0.05
        for a, b in zip(modelled_inner, expected_inner))

    # ---- connector: observation only, no identification -------------------
    # The two 21.412 x 4.826 mm bodies belong to TFT1_ASM (verified by XCAF
    # label paths), so they are TFT flex bonds and NOT the panel connector. An
    # earlier revision of this file called them the FPC connector and reported a
    # ~24 mm disagreement with the drawing; that was a misidentification.
    fpc_expected = layout["fpcConnector"]["at"]
    cam = m["display_cam"]
    # Y candidate: x -> -x, y kept. The centre is a POSITION COMPARISON only.
    candidate_placed = {
        "x": round(dx - (cam["x_mm"][0] + cam["x_mm"][1]) / 2, 3),
        "y": round(dy + (cam["y_mm"][0] + cam["y_mm"][1]) / 2, 3),
    }
    candidate_offset = [round(candidate_placed["x"] - fpc_expected["x"], 3),
                        round(candidate_placed["y"] - fpc_expected["y"], 3)]
    candidate_offset_magnitude = round(
        (candidate_offset[0] ** 2 + candidate_offset[1] ** 2) ** 0.5, 3)

    separations = vendor_model_separations()
    info = {
        "source": str(VENDOR_DISPLAY_MODEL["source"]),
        "transform": {
            "rotation": "180 deg about the Y axis: (x, y, z) -> (-x, y, -z)",
            "rotation_matrix": [list(row) for row in display_rotation()],
            "determinant": round(rotation_determinant(), 6),
            "is_rigid_rotation_not_mirror": abs(rotation_determinant() - 1.0) < 1e-9,
            "translation_mm": [round(dx, 4), round(dy, 4), round(dz, 4)],
            "xy_datum": ("outer SMT boss pattern centre (= PCB outline centre), placed on the "
                         "module centre from lib/layout.ts"),
        },
        "vendor_frame_checks": {
            "bbox_z_mm": list(VENDOR_DISPLAY_MODEL["bbox_z_mm"]),
            "glass_front_z": m["glass_front_z"],
            "touch_frame_front_z": m["touch_frame_front_z"],
            "pcb_rear_z": m["pcb_rear_z"],
            "boss_rear_z": m["boss_rear_z"],
            "rear_most_z": m["rear_most_z"],
            "bosses": 8,
            "outer_pattern_mm": list(m["mount_pattern_mm"]),
            "inner_pattern_x": list(m["inner_hole_x"]),
            "inner_pattern_y": list(m["inner_hole_y"]),
        },
        "vendor_model_separations_mm": separations,
        "placed_global_z_mm": {k: round(v, 3) for k, v in found.items()},
        # Both descriptions come from the layout datum and the PLACED planes, so a
        # datum change cannot leave a stale number behind. Panel frame: Z = 0 at
        # the panel FRONT, +Z rearward.
        "glass_datum": _placed_note(
            layout, found, "the vendor glass FRONT face at the layout display datum "
            "(PROVISIONAL_Z.displayGlassFront)"),
        "front_frame_disclosure": (
            "the vendor touch/frame forward face is "
            f"{round(m['touch_frame_front_z'] - m['glass_front_z'], 3):.2f} mm IN FRONT of the "
            f"glass front face in the model, so this placement puts it at panel Z = "
            f"{found['touch_frame_front']:+.2f}, i.e. "
            f"{_ahead_of_panel(layout, found):.2f} mm in FRONT of the panel front face "
            f"(Z = {float(layout['provisionalZ']['panelFront']):+.2f}). It is NOT flush with "
            "the panel and NOT a fit: the real frame/touch layer, the aperture relief and "
            "the seating are unmeasured"),
        "mounting_centres_verified": True,
        "orientation": {
            "status": DISPLAY_ORIENTATION["status"],
            "candidate_used_for_drawing": DISPLAY_ORIENTATION["candidate_used_for_drawing"],
            "alternative_candidate": DISPLAY_ORIENTATION["alternative_candidate"],
            "z_reversal_required": DISPLAY_ORIENTATION["z_reversal_required"],
            "why_not_proven": DISPLAY_ORIENTATION["why_not_proven"],
            "resolving_evidence": DISPLAY_ORIENTATION["resolving_evidence"],
            "inner_pattern_observation": {
                "vendor_offsets_xy": pattern["vendor_offsets_xy"],
                "modelled_candidate": pattern["candidates"]["about_y"],
                "alternative_candidate": pattern["candidates"]["about_x"],
                "recorded_drawing_positions": expected_inner,
                "candidate_matches_recorded_positions": premise_consistent,
                "caveat": pattern["caveat"],
            },
            "not_asserted": DISPLAY_ORIENTATION["not_asserted"],
        },
        "connector": {
            "identification": "NOT ESTABLISHED",
            "tft1_flex_bonds": {
                "names": list(m["tft1_flex_bonds"]["names"]),
                "label_path": m["tft1_flex_bonds"]["label_path"],
                "local_span_mm": list(m["tft1_flex_bonds"]["local_span_mm"][:2]),
                "placed_centre_xy_mm": list(m["tft1_flex_bonds"]["placed_centre_xy_mm"]),
                "correction": (
                    "these two bodies are TFT flex bonds inside TFT1_ASM. An earlier "
                    "revision of this file called them the DSI FPC connector and reported a "
                    "~24 mm disagreement with the drawing; that was a misidentification and "
                    "is withdrawn"),
            },
            "candidate_at_drawing_position": {
                "name": cam["name"],
                "label_path": cam["label_path"],
                "vendor_bounds_x_mm": list(cam["x_mm"]),
                "vendor_bounds_y_mm": list(cam["y_mm"]),
                "vendor_bounds_z_mm": list(cam["z_mm"]),
                "vendor_size_mm": list(cam["size_mm"]),
                "placed_centre_xy_mm": [candidate_placed["x"], candidate_placed["y"]],
                "status": cam["status"],
            },
            "recorded_design_position_xy_mm": [fpc_expected["x"], fpc_expected["y"]],
            "position_offset_mm": candidate_offset,
            "position_offset_magnitude_mm": candidate_offset_magnitude,
            "drawing_position_status": (
                "UNVERIFIED. The recorded position and the 21.412 x 4.826 mm body size are "
                "drawing-derived; the long-axis wording is disputed, so the drawing "
                "position/length arithmetic needs independent re-derivation"),
            "rear_front_frame_mapping": (
                "UNVERIFIED: which face of the module the drawing shows is not established"),
            "not_asserted": [
                "that DISPLAY_CAM is the 15-pin DSI connector",
                "a pin count, an electrical pinout or a physical side",
                "that the model and the drawing agree",
            ],
        },
        "status": ("ILLUSTRATIVE: the vendor model placed to show the stack. No physical "
                   "fit is claimed; every physical gate stays pending"),
    }
    m2 = placed.BoundingBox()
    info["placed_bbox_mm"] = [m2.xlen, m2.ylen, m2.zlen]
    info["pending"] = [
        "in-plane orientation: 180 deg about X and 180 deg about Y are both admissible",
        "front touch/frame plane 1.90 mm forward of the glass datum in this placement",
        "connector identity, pin count, electrical side and its real clearance",
        "the drawing's position/length arithmetic (long-axis wording disputed)",
        "the real glass, touch layer and frame thicknesses at the panel aperture",
    ]
    if not premise_consistent:
        raise GeometryError(
            "the modelled orientation candidate no longer matches the layout's recorded "
            "inner-hole premise; this is a premise-consistency guard, not a proof: "
            f"{modelled_inner} vs {expected_inner}")
    return placed, info


# Boolean noise at a touching face. A placeholder that only touches measures
# about 1e-5 mm^3; a REAL intrusion of 1 um over the 13.9 mm^2 annulus measures
# about 0.014 mm^3. The limit sits between them, so noise passes and any real
# penetration fails.
INTRUSION_EPS_MM3 = 1e-3


def spacer_intrusions(cq, spacer, display_shape, board_shape) -> dict:
    """Positive-volume overlap of one placeholder against the display and board.

    Touching faces give at most boolean noise (about 1e-5 mm^3 measured); a real
    intrusion is orders of magnitude larger. The raw volumes are reported so a
    reader can see which case applies.
    """
    return {
        "display_mm3": round(spacer.val().intersect(display_shape).Volume(), 6),
        "board_mm3": round(spacer.val().intersect(board_shape).Volume(), 6),
    }


def boss_at(shape, axis: tuple[float, float]) -> tuple[float, float, float, float] | None:
    """Bounding box (x0, x1, y0, y1) of the SMT boss body nearest an axis."""
    best = None
    for solid in shape.Solids():
        b = solid.BoundingBox()
        if not (5.0 <= b.xlen <= 6.0 and 5.0 <= b.ylen <= 6.0 and 5.0 <= b.zlen <= 6.0):
            continue
        cx, cy = (b.xmin + b.xmax) / 2, (b.ymin + b.ymax) / 2
        distance = ((cx - axis[0]) ** 2 + (cy - axis[1]) ** 2) ** 0.5
        if best is None or distance < best[0]:
            best = (distance, b.xmin, b.xmax, b.ymin, b.ymax)
    if best is None or best[0] > 0.05:
        return None
    return (best[1], best[2], best[3], best[4])


# ---------------------------------------------------------------------------
# Canonical panel features: 20 apertures + 14 drills, checked by IDENTITY
# ---------------------------------------------------------------------------
# The panel model cuts exactly the features the panel BOARD emits, in the same
# order: PANEL_APERTURES (20) and PANEL_DRILLS (14 = the six feedthrough screw
# holes, the four RACK fixings and the four STAND_ panel-mount screws). The two
# tuples below are the EXPECTED IDENTITIES, written out rather than counted, so a
# dropped, duplicated, renamed or re-shaped feature fails here instead of quietly
# shrinking the model.
#
# The four STAND_ drills are the PANEL_MOUNT_POINTS frame at (+-71.5, +-52) mm:
# M3 clearance through BOTH the panel and the front board, carried by the
# adjustable PANEL_STANDOFF. They were dropped together with the board mounting
# holes on 2026-11-13 and came back with the four-point frame decision, so
# 20 + 14 = 34 features is the current set. Losing one of them again is an
# identity change, not a tolerance: nothing here may be loosened to absorb it.
#
# The order is part of the identity: panel.circuit.tsx emits these features in
# this sequence, and lib/panel-cutouts.test.ts compares the built board with the
# same arrays. A reordered export would be a different board.
PANEL_APERTURE_IDENTITIES: tuple[str, ...] = (
    "LCD_APERTURE",
    "USB_FEEDTHROUGH_0", "USB_FEEDTHROUGH_1", "USB_FEEDTHROUGH_2",
    "REC BUTTON", "OVER DUB BUTTON", "STOP BUTTON", "PLAY BUTTON",
    "PLAY START BUTTON", "SHIFT/EXIT BUTTON",
    "REC LED", "OVERDUB LED", "PLAY LED",
    "VALUE ENCODER", "POWER BUTTON", "NAV",
    "MIDI IN 1 JACK", "MIDI IN 2 JACK", "MIDI OUT 1 JACK", "MIDI OUT 2 JACK",
)
PANEL_DRILL_IDENTITIES: tuple[str, ...] = (
    "USB_0_-1", "USB_0_1", "USB_1_-1", "USB_1_1", "USB_2_-1", "USB_2_1",
    "RACK_1", "RACK_2", "RACK_3", "RACK_4",
    "STAND_1", "STAND_2", "STAND_3", "STAND_4",
)
# The seven rectangular B3F-4055 openings: one geometry, seven switches.
PANEL_BUTTON_IDENTITIES: tuple[str, ...] = (
    "REC BUTTON", "OVER DUB BUTTON", "STOP BUTTON", "PLAY BUTTON",
    "PLAY START BUTTON", "SHIFT/EXIT BUTTON", "POWER BUTTON",
)
# The round openings, with the diameter the canonical data must carry: the three
# LED light pipes, the two 7.2 mm control holes and the four 6.2 mm jack barrels.
PANEL_CIRCLE_DIAMETERS: dict[str, float] = {
    "REC LED": 2.6, "OVERDUB LED": 2.6, "PLAY LED": 2.6,
    "VALUE ENCODER": 7.2, "NAV": 7.2,
    "MIDI IN 1 JACK": 6.2, "MIDI IN 2 JACK": 6.2,
    "MIDI OUT 1 JACK": 6.2, "MIDI OUT 2 JACK": 6.2,
}
# The B3F-4055 opening: 12.25 mm square (Omron's 12.2 +0.05/-0 reference, top of
# band, and the printed cap's assembly clearance - lib/layout.ts PANEL_CUTOUTS).
PANEL_BUTTON_SQUARE = 12.25
# The LCD aperture's outline: the rounded rectangle from PANEL_LCD_CUTOUT.
PANEL_LCD_OUTLINE_POINTS = 28


def _polygon_area(points: list[dict]) -> float:
    """Shoelace area of a closed polygon given as {x, y} points (mm^2)."""
    total = 0.0
    n = len(points)
    for i in range(n):
        a, b = points[i], points[(i + 1) % n]
        total += float(a["x"]) * float(b["y"]) - float(b["x"]) * float(a["y"])
    return abs(total) / 2.0


def _feature_spec(aperture: dict) -> dict:
    """The exact tool specification for one canonical aperture, validated."""
    name = str(aperture["name"])
    shape = aperture.get("shape")
    if shape == "polygon":
        if name != "LCD_APERTURE":
            raise GeometryError(f"unexpected polygon aperture {name}")
        points = aperture["points"]
        if len(points) != PANEL_LCD_OUTLINE_POINTS:
            raise GeometryError(
                f"the LCD aperture has {len(points)} outline points, expected "
                f"{PANEL_LCD_OUTLINE_POINTS}: the corner relief changed shape")
        return {"shape": "polygon",
                "points": [(float(p["x"]), float(p["y"])) for p in points]}
    if shape == "rect":
        width, height = float(aperture["width"]), float(aperture["height"])
        if name in PANEL_BUTTON_IDENTITIES:
            if (abs(width - PANEL_BUTTON_SQUARE) > 1e-6
                    or abs(height - PANEL_BUTTON_SQUARE) > 1e-6):
                raise GeometryError(
                    f"button opening {name} is {width} x {height} mm, not the "
                    f"{PANEL_BUTTON_SQUARE} mm square the panel emits")
        elif not name.startswith("USB_FEEDTHROUGH_"):
            raise GeometryError(f"unexpected rectangular aperture {name}")
        return {"shape": "rect", "x": float(aperture["x"]), "y": float(aperture["y"]),
                "width": width, "height": height}
    if shape == "circle":
        diameter = 2.0 * float(aperture["radius"])
        expected = PANEL_CIRCLE_DIAMETERS.get(name)
        if expected is None:
            raise GeometryError(f"unexpected circular aperture {name}")
        if abs(diameter - expected) > 1e-6:
            raise GeometryError(
                f"aperture {name} is D{diameter:.2f} mm, not the canonical "
                f"D{expected:.2f} mm")
        return {"shape": "circle", "x": float(aperture["x"]), "y": float(aperture["y"]),
                "radius": diameter / 2}
    raise GeometryError(f"panel aperture {name} has an unknown shape {shape!r}")


def _spec_area(spec: dict) -> float:
    shape = spec["shape"]
    if shape == "circle":
        return math.pi * spec["radius"] ** 2
    if shape == "rect":
        return spec["width"] * spec["height"]
    return _polygon_area([{"x": x, "y": y} for x, y in spec["points"]])


def _spec_probes(spec: dict) -> tuple[tuple[float, float], tuple[float, float]]:
    """(void probe at the centre, material probe just outside the cut)."""
    shape = spec["shape"]
    if shape == "circle":
        cx, cy, r = spec["x"], spec["y"], spec["radius"]
        return (cx, cy), (cx + r + 0.2, cy)
    if shape == "rect":
        cx, cy = spec["x"], spec["y"]
        return (cx, cy), (cx + spec["width"] / 2 + 0.2, cy)
    xs = [x for x, _ in spec["points"]]
    ys = [y for _, y in spec["points"]]
    cx, cy = (min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2
    return (cx, cy), (cx, max(ys) + 0.2)


def _inside_spec(point: tuple[float, float], spec: dict) -> bool:
    """True when an XY point is inside a feature, with a 0.05 mm margin."""
    x, y = point
    shape = spec["shape"]
    if shape == "circle":
        return math.hypot(x - spec["x"], y - spec["y"]) <= spec["radius"] + 0.05
    if shape == "rect":
        return (abs(x - spec["x"]) <= spec["width"] / 2 + 0.05
                and abs(y - spec["y"]) <= spec["height"] / 2 + 0.05)
    # Convex rounded rectangle: compare against the outline's bounding box.
    xs = [px for px, _ in spec["points"]]
    ys = [py for _, py in spec["points"]]
    return (min(xs) - 0.05 <= x <= max(xs) + 0.05
            and min(ys) - 0.05 <= y <= max(ys) + 0.05)


def panel_feature_plan(layout: dict) -> dict:
    """Validate every canonical panel feature and return the cut plan.

    Nothing is counted from the data it is checking: the identities come from the
    tuples above, the shapes from the categories, and the drills from the union of
    the apertures' own ``drills`` plus ``panelMountHoles``. Any disagreement is a
    GeometryError, never a smaller model.

    Each planned feature carries its exact tool specification, the material/void
    probe points and the volume it must remove from the panel stock, so the built
    panel is compared with the DECLARED shape rather than only counted.
    """
    apertures = layout["panelApertures"]
    drills = layout["panelDrills"]
    names = [str(a.get("name", "")) for a in apertures]
    if names != list(PANEL_APERTURE_IDENTITIES):
        raise GeometryError(
            "the canonical panel apertures are not the expected set/order: "
            f"{names} != {list(PANEL_APERTURE_IDENTITIES)}; a dropped, duplicated, "
            "renamed or reordered aperture would silently change the panel model")
    drill_names = [str(d.get("name", "")) for d in drills]
    if drill_names != list(PANEL_DRILL_IDENTITIES):
        raise GeometryError(
            "the canonical panel drills are not the expected set/order: "
            f"{drill_names} != {list(PANEL_DRILL_IDENTITIES)}")

    # The drill array must be the apertures' own drills plus the mount holes, so a
    # drill edited in one place only is caught instead of drawn twice or lost.
    derived_drills: list[dict] = []
    for aperture in apertures:
        derived_drills.extend(aperture.get("drills") or [])
    derived_drills.extend(layout["panelMountHoles"])

    def key(entry: dict) -> tuple:
        return (str(entry["name"]), round(float(entry["x"]), 6),
                round(float(entry["y"]), 6), round(float(entry["diameter"]), 6))

    if sorted(key(d) for d in derived_drills) != sorted(key(d) for d in drills):
        raise GeometryError(
            "panelDrills is not the apertures' own drills plus panelMountHoles: "
            f"{sorted(key(d) for d in drills)} != "
            f"{sorted(key(d) for d in derived_drills)}")

    # Each mount drill must be the point it is drilled from: the four STAND_
    # drills are the PANEL_MOUNT_POINTS frame the front board and the standoffs
    # are also placed at, and the four RACK_ drills are the RAIL_HOLES. Identity
    # and order cannot see a move, so position is compared here: a pair drifted
    # apart in the export would drill the panel somewhere the standoff is not.
    def positions(entries: list[dict]) -> list[tuple]:
        return [(round(float(e["x"]), 6), round(float(e["y"]), 6))
                for e in entries]

    stand_drills = [d for d in drills if str(d["name"]).startswith("STAND_")]
    rack_drills = [d for d in drills if str(d["name"]).startswith("RACK_")]
    if positions(stand_drills) != positions(list(layout["panelMountPoints"])):
        raise GeometryError(
            "the STAND_ panel drills are not the PANEL_MOUNT_POINTS frame: "
            f"{positions(stand_drills)} != {positions(list(layout['panelMountPoints']))}")
    if positions(rack_drills) != positions(list(layout["railHoles"])):
        raise GeometryError(
            "the RACK_ panel drills are not the RAIL_HOLES: "
            f"{positions(rack_drills)} != {positions(list(layout['railHoles']))}")

    half_w = float(layout["panel"]["width"]) / 2
    half_h = float(layout["panel"]["height"]) / 2
    thickness = float(layout["panel"]["thickness"])
    features: list[dict] = []

    def add(name: str, kind: str, spec: dict) -> None:
        void, outside = _spec_probes(spec)
        if abs(void[0]) >= half_w or abs(void[1]) >= half_h:
            raise GeometryError(f"panel {kind} {name} at {void} is off the panel")
        area = _spec_area(spec)
        if area <= 0.0:
            raise GeometryError(f"panel {kind} {name} has no area")
        features.append({
            "name": name,
            "kind": kind,
            "spec": spec,
            "area_mm2": round(area, 6),
            "cut_volume_mm3": round(area * thickness, 6),
            "void": void,
            "outside": outside,
        })

    for aperture in apertures:
        add(str(aperture["name"]), "aperture", _feature_spec(aperture))
    for drill in drills:
        radius = float(drill["diameter"]) / 2
        if radius <= 0.0:
            raise GeometryError(f"panel drill {drill['name']} has no diameter")
        add(str(drill["name"]), "drill",
            {"shape": "circle", "x": float(drill["x"]), "y": float(drill["y"]),
             "radius": radius})

    # A probe point outside one feature must not land inside another, or the
    # material check would be measuring a different feature.
    for feature in features:
        for other in features:
            if other is feature:
                continue
            if _inside_spec(feature["outside"], other["spec"]):
                feature["outside"] = None
                break

    return {
        "features": features,
        "apertures": len(apertures),
        "drills": len(drills),
        "total": len(features),
        "note": (f"{len(apertures)} canonical apertures and {len(drills)} canonical "
                 "drills, matched by identity and order against PANEL_APERTURES / "
                 "PANEL_DRILLS; the same arrays lib/panel-cutouts.test.ts compares "
                 "with the built panel JSON"),
    }


def panel_cut_tool(cq, feature: dict, envelope):
    """The cutting solid for one planned feature, through the whole panel stock."""
    zlo, zhi = envelope.z.lo - 1.0, envelope.z.hi + 1.0
    spec = feature["spec"]
    shape = spec["shape"]
    if shape == "circle":
        return _cylinder(cq, spec["x"], spec["y"], zlo, zhi, spec["radius"])
    if shape == "rect":
        return _box(cq, spec["x"] - spec["width"] / 2, spec["y"] - spec["height"] / 2,
                    zlo, spec["x"] + spec["width"] / 2,
                    spec["y"] + spec["height"] / 2, zhi)
    return (cq.Workplane("XY").workplane(offset=zlo)
            .polyline(spec["points"]).close().extrude(zhi - zlo))


def _box(cq, x1: float, y1: float, z1: float, x2: float, y2: float, z2: float):
    """Axis-aligned box from two corners."""
    return (cq.Workplane("XY").workplane(offset=z1)
            .center((x1 + x2) / 2, (y1 + y2) / 2)
            .rect(abs(x2 - x1), abs(y2 - y1))
            .extrude(z2 - z1))


def _cylinder(cq, x: float, y: float, z1: float, z2: float, radius: float):
    return (cq.Workplane("XY").workplane(offset=z1).center(x, y)
            .circle(radius).extrude(z2 - z1))


def probe_panel_point(cq, body, point: tuple[float, float], z: float, want_material: bool,
                      what: str) -> None:
    """Check one XY point of a panel at mid-thickness for material or void."""
    inside = body.val().isInside(cq.Vector(point[0], point[1], z))
    if inside != want_material:
        raise GeometryError(
            f"{what}: ({point[0]:.2f}, {point[1]:.2f}, {z:.2f}) is "
            f"{'material' if inside else 'void'}, expected "
            f"{'material' if want_material else 'void'}")


def build(cq, layout: dict, display_step: Path | None, include_usbc: bool) -> dict:
    """Build every solid and return name -> object, ready to export."""
    pz = layout["provisionalZ"]
    depth = layout["provisionalDepth"]
    env = assembly_envelopes(layout, include_usbc_candidate=include_usbc)

    def box(x1, y1, z1, x2, y2, z2):
        """Axis-aligned box from two corners (one implementation, module level)."""
        return _box(cq, x1, y1, z1, x2, y2, z2)

    def cylinder(x, y, z1, z2, radius):
        return _cylinder(cq, x, y, z1, z2, radius)

    def span_box(e, grow: float = 1.0):
        """Box for an envelope, grown in Z so a cut passes through it."""
        return box(e.x.lo, e.y.lo, e.z.lo - grow, e.x.hi, e.y.hi, e.z.hi + grow)

    solids: dict[str, object] = {}
    meta: dict[str, dict] = {}

    # ---- panel --------------------------------------------------------
    e = env["PANEL_30HP_3U"]
    panel_wp = span_box(e, grow=0.0)
    # Every panel opening and every panel drill comes from the canonical
    # PANEL_APERTURES / PANEL_DRILLS arrays in layout.ts, the same data the
    # panel board renders and lib/panel-cutouts.test.ts compares with the built
    # panel JSON. Nothing is restated here; the EXPECTED identities and the
    # per-feature shape are validated by panel_feature_plan().
    plan = panel_feature_plan(layout)
    mid_z = (e.z.lo + e.z.hi) / 2
    for feature in plan["features"]:
        label = f"{feature['kind']} {feature['name']}"
        tool = panel_cut_tool(cq, feature, e)
        before = panel_wp.val()
        removed = before.intersect(tool.val()).Volume()
        if removed <= VOLUME_EPS:
            raise GeometryError(
                f"panel cut '{label}' removes nothing: it is outside the stock, a "
                "duplicate of another feature, or the feature is the wrong size")
        expected = feature["cut_volume_mm3"]
        if abs(removed - expected) > max(1e-6, 1e-4 * expected):
            raise GeometryError(
                f"panel cut '{label}' removes {removed:.6f} mm^3, but the declared "
                f"{feature['spec']['shape']} of {feature['area_mm2']:.6f} mm^2 "
                f"through the {e.z.hi - e.z.lo:.2f} mm panel is {expected:.6f} mm^3: "
                "the cut shape does not match the canonical feature")
        # The centre must be material before the cut and void after it; a point
        # just outside the cut must still be material. Together these pin the
        # feature's position, not only its volume.
        probe_panel_point(cq, panel_wp, feature["void"], mid_z, True,
                          f"panel {label} centre before the cut")
        panel_wp = panel_wp.cut(tool)
        probe_panel_point(cq, panel_wp, feature["void"], mid_z, False,
                          f"panel {label} cut")
        if feature["outside"] is not None:
            probe_panel_point(cq, panel_wp, feature["outside"], mid_z, True,
                              f"panel {label} surrounding material")
    if panel_wp.solids().size() != 1:
        raise GeometryError(f"the panel is {panel_wp.solids().size()} solids after its cuts")
    solids["PANEL_30HP_3U"] = panel_wp
    meta["PANEL_30HP_3U"] = {
        "features": [f"{f['kind']} {f['name']}" for f in plan["features"]],
        "feature_count": plan["total"],
        "apertures": plan["apertures"],
        "drills": plan["drills"],
        "note": plan["note"],
    }

    # ---- front board --------------------------------------------------
    e = env["FRONT_BOARD_PROVISIONAL_Z"]
    n = layout["notchBounds"]
    front_wp = (box(e.x.lo, e.y.lo, e.z.lo, e.x.hi, e.y.hi, e.z.hi)
                .cut(box(n["left"], n["bottom"], e.z.lo - 1.0, n["right"], n["top"] + 1.0,
                         e.z.hi + 1.0)))
    holes = [(f"LCD mount hole {i}", h, layout["display"]["stack"]["holeDiameter"])
             for i, h in enumerate(layout["lcdMountHoles"])]
    holes += [(f"standoff hole {i}", h, layout["mountHoleDiameter"])
              for i, h in enumerate(layout["frontBoardHoles"])]
    holes += [(f"front-rear mount {i}", h, layout["mountHoleDiameter"])
              for i, h in enumerate(layout["frontRearMount"])]
    holes += [(f"bracket mount {i}", {"x": x, "y": layout["bracket"]["mountY"]},
               layout["bracket"]["mountHoleDiameter"])
              for i, x in enumerate(layout["bracket"]["mountX"])]
    # The panel-mount frame: the SAME points the panel drills, at the M3
    # clearance the standoff carries (derive, never restate).
    holes += [(f"panel-mount hole {i}", h, layout["panelStandoff"]["holeDiameter"])
              for i, h in enumerate(layout["panelMountPoints"])]
    for label, hole, diameter in holes:
        tool = cylinder(hole["x"], hole["y"], e.z.lo - 1.0, e.z.hi + 1.0, diameter / 2)
        if front_wp.val().intersect(tool.val()).Volume() <= VOLUME_EPS:
            raise GeometryError(f"front-board cut '{label}' removes nothing")
        front_wp = front_wp.cut(tool)
    if front_wp.solids().size() != 1:
        raise GeometryError(f"the front board is {front_wp.solids().size()} solids")
    solids["FRONT_BOARD_PROVISIONAL_Z"] = front_wp
    # These are the cuts this script APPLIES, not the current PCB's cuts. The
    # block below states the difference so no reader can mistake the solid for
    # the board; the repair is recorded in docs/TODO.md and deliberately not
    # attempted here (a partial repair would look current and not be).
    meta["FRONT_BOARD_PROVISIONAL_Z"] = {
        "status": BOARD_MOCKUP_DISCLAIMER,
        "cuts_applied": [f"obsolete notch rectangle (notchBounds x {n['left']:g}...{n['right']:g}, "
                         f"y {n['bottom']:g}...{n['top']:g})"] + [h[0] for h in holes],
        "not_the_current_pcb": list(BOARD_MOCKUP_DEFECTS["mockup-front-board.stl"]),
        "repair": list(BOARD_MOCKUP_REPAIR),
    }

    # ---- panel mount standoffs (M3, adjustable) -------------------------
    # Four annuli between the panel REAR face and the front-board TOP face, at
    # the PANEL_MOUNT_POINTS both boards are drilled at. Every number comes from
    # the exported PANEL_STANDOFF block; the drawn Z span is checked against its
    # length so the model cannot drift from the layout. These are ENVELOPES: the
    # panel plane is set by the jack bushings, so nothing here is a fit.
    standoff = layout["panelStandoff"]
    z_rear = float(layout["panel"]["thickness"])     # panel rear face
    z_top = float(pz["frontBoardTop"])               # front-board top face
    if abs((z_top - z_rear) - float(standoff["length"])) > 1e-6:
        raise GeometryError(
            f"PANEL_STANDOFF.length {standoff['length']} is not the drawn "
            f"{z_top - z_rear:.2f} mm panel-rear -> board-top span")
    od = float(standoff["bodyDiameter"])
    od_hole = float(standoff["holeDiameter"])
    if od <= od_hole:
        raise GeometryError(
            f"standoff OD {od} does not exceed the M3 clearance {od_hole}")
    mount_meta = []
    for index, point in enumerate(layout["panelMountPoints"], start=1):
        name = f"PANEL_STANDOFF_{index}"
        spacer = (cq.Workplane("XY").workplane(offset=z_rear)
                  .center(float(point["x"]), float(point["y"]))
                  .circle(od / 2).circle(od_hole / 2)
                  .extrude(z_top - z_rear))
        intrusions = {
            "panel_mm3": round(spacer.val().intersect(panel_wp.val()).Volume(), 6),
            "board_mm3": round(spacer.val().intersect(front_wp.val()).Volume(), 6),
        }
        if max(intrusions.values()) > INTRUSION_EPS_MM3:
            raise GeometryError(f"{name} intrudes: {intrusions}")
        solids[name] = spacer
        mount_meta.append({
            "name": name,
            "xy_mm": [point["x"], point["y"]],
            "z_mm": [z_rear, z_top],
            "length_mm": round(z_top - z_rear, 3),
            "outer_diameter_mm": od,
            "inner_diameter_mm": od_hole,
            "intrusion_volume_mm3": intrusions,
        })
    meta["PANEL_STANDOFFS"] = {
        "kind": "M3 ADJUSTABLE STANDOFF ENVELOPE, non-approved",
        "count": len(mount_meta),
        "span_mm": [z_rear, z_top],
        "standoffs": mount_meta,
        "panel_plane_datum": standoff["panelPlaneDatum"],
        "not_claimed": [
            "a part, part number or supplier",
            "a measured jack-bushing gap (the 9.00 mm is the jack seating-shoulder nominal)",
            "thread engagement, torque or assembly order",
        ],
    }

    # ---- rear board ---------------------------------------------------
    e = env["REAR_BOARD_PROVISIONAL_Z"]
    rear_wp = box(e.x.lo, e.y.lo, e.z.lo, e.x.hi, e.y.hi, e.z.hi)
    rear_holes = [(f"front-rear mount {i}", h, layout["mountHoleDiameter"])
                  for i, h in enumerate(layout["frontRearMount"])]
    for label, hole, diameter in rear_holes:
        tool = cylinder(hole["x"], hole["y"], e.z.lo - 1.0, e.z.hi + 1.0, diameter / 2)
        if rear_wp.val().intersect(tool.val()).Volume() <= VOLUME_EPS:
            raise GeometryError(f"rear-board cut '{label}' removes nothing")
        rear_wp = rear_wp.cut(tool)
    solids["REAR_BOARD_PROVISIONAL_Z"] = rear_wp
    meta["REAR_BOARD_PROVISIONAL_Z"] = {
        "status": BOARD_MOCKUP_DISCLAIMER,
        "cuts_applied": [h[0] for h in rear_holes],
        "not_the_current_pcb": list(BOARD_MOCKUP_DEFECTS["mockup-rear-board.stl"]),
        "repair": list(BOARD_MOCKUP_REPAIR),
        "note": (f"Z from PROVISIONAL_Z plus the candidate "
                 f"{depth['frontToRearGap']:.2f} mm gap; the slab outline and Z are "
                 "current, the CUTS are not"),
    }

    # ---- display ------------------------------------------------------
    if display_step is not None:
        shape = cq.importers.importStep(str(display_step)).val()
        moved, placement = place_display(cq, shape, layout)
        solids["DISPLAY_WAVESHARE_VENDOR_STEP"] = moved
        placement["solids"] = len(moved.Solids())
        meta["DISPLAY_WAVESHARE_VENDOR_STEP"] = placement
    else:
        e = env["DISPLAY_WAVESHARE_4_3_DSI_PROVISIONAL_Z"]
        solids["DISPLAY_ENVELOPE_PROVISIONAL"] = span_box(e, grow=0.0)
        meta["DISPLAY_ENVELOPE_PROVISIONAL"] = {
            "note": ("no verified vendor STEP was supplied, so this is the 106 x 68 mm module "
                     "outline between the layout's display datum "
                     f"(PROVISIONAL_Z.displayGlassFront = "
                     f"{float(layout['provisionalZ']['displayGlassFront']):+.2f} mm, i.e. "
                     f"{float(layout['provisionalZ']['displayGlassFront']):.2f} mm behind the "
                     f"panel front) and the {float(layout['provisionalZ']['displayRearMost']):.2f} mm "
                     "rear-most envelope; it is not vendor geometry"),
        }

    # ---- LCD mount placeholders (model-fit, non-approved) ----------------
    # Four annuli that span the gap measured from the drawn solids: the vendor
    # SMT boss rear plane to the front-board top face. They are GEOMETRIC
    # PLACEHOLDERS: not a part, not a fit, not a thread, not an approved length.
    # Without a verified vendor model there is no model-derived boss plane, so
    # they are skipped rather than fabricated from the 14.05 mm envelope.
    mount = display_mount_gap(layout)
    if display_step is not None:
        board_bb = front_wp.val().BoundingBox()
        board_top_z = round(board_bb.zmin, 4)
        if abs(board_top_z - mount["board_top_z_mm"]) > 0.01:
            raise GeometryError(
                f"the drawn front board top face is Z {board_top_z}, not the recorded "
                f"{mount['board_top_z_mm']}; the placeholder span would be wrong")
        boss_rear_z = round(placement["placed_global_z_mm"]["boss_rear"], 4)
        if abs(boss_rear_z - mount["boss_rear_z_mm"]) > 0.01:
            raise GeometryError(
                f"the drawn boss rear plane is Z {boss_rear_z}, not the recorded "
                f"{mount['boss_rear_z_mm']}")
        length = round(board_top_z - boss_rear_z, 4)
        if abs(length - mount["model_gap_mm"]) > 0.01:
            raise GeometryError(f"the placeholder length {length} is not the model gap "
                                f"{mount['model_gap_mm']}")
        # A spacer needs a POSITIVE span. At the selected 0.60 mm display datum the
        # drawn boss ends land behind the front board's top face, so no positive
        # length exists on this chain: the solids are REFUSED, nothing is extruded
        # (a negative or reversed box would draw an overlap as if it were a part)
        # and the resulting collision risk is recorded as PENDING, not cleared.
        if length <= 1e-9:
            meta["LCD_MOUNT_PLACEHOLDERS"] = {
                "kind": "REFUSED - NO POSITIVE-LENGTH PLACEHOLDER EXISTS",
                "count": 0,
                "axes_xy_mm": [[axis[0], axis[1]] for axis in mount["axes_xy"]],
                "span_mm": [boss_rear_z, board_top_z],
                "model_gap_mm": length,
                "failure_mode": ("the model gap is negative: the drawn boss ends are BEHIND the "
                                 "front board's top face, so the front-board mounting arithmetic "
                                 "has no positive spacer length"),
                "no_solid_drawn": ("nothing is extruded for this block. A negative or reversed "
                                   "box would draw an overlap as if it were a part"),
                "collision": "PENDING - UNVERIFIED COLLISION RISK, NOT CLEARED",
                "collision_note": (
                    "the module's boss ends and rear-most plane lie inside the front board's "
                    "Z band, and the U opening's XY containment of every module feature is "
                    "unmeasured. Nothing here proves the interference harmless; measure the "
                    "real boss plane and the opening against the module before any fit claim"),
                "real_carrier_gap": {
                    "rear_board_front_z_mm": float(layout["provisionalZ"]["rearBoardFront"]),
                    "display_rear_most_z_mm": float(layout["provisionalZ"]["displayRearMost"]),
                    "spacer_length_mm": float(layout["displayStandoff"]["spacerLength"]),
                    "note": ("the carrier bolts to the REAR board; that gap is unchanged and "
                             "is the only mounting this model shows"),
                },
                "not_claimed": [
                    "an approved spacer length",
                    "a part, part number or supplier",
                    "a standoff diameter or a thread",
                    "any physical fit",
                    "that the drawn overlap is acceptable",
                ],
                "first_article_tasks": mount["first_article_tasks"],
            }
            print("  LCD mount placeholders REFUSED: the model gap is "
                  f"{length:+.2f} mm (no positive spacer exists); collision risk PENDING")
            length = None
        od, idia = mount["outer_diameter_mm"], mount["inner_diameter_mm"]
        hole = float(layout["display"]["stack"]["holeDiameter"])
        if length is not None and od / 2 <= hole / 2:
            raise GeometryError(
                f"OD {od} does not exceed the board hole {hole}: no bearing annulus would show")
        spacer_meta = []
        for index, axis in enumerate([] if length is None else mount["axes_xy"], start=1):
            name = f"{DISPLAY_MOUNT_PLACEHOLDER['name_prefix']}_{index}"
            spacer = (cq.Workplane("XY").workplane(offset=boss_rear_z)
                      .center(axis[0], axis[1])
                      .circle(od / 2).circle(idia / 2)
                      .extrude(length))
            bb = spacer.val().BoundingBox()
            if abs(bb.zmin - boss_rear_z) > 0.01 or abs(bb.zmax - board_top_z) > 0.01:
                raise GeometryError(f"{name} does not span the model gap")
            intrusions = spacer_intrusions(cq, spacer, moved, front_wp.val())
            if max(intrusions.values()) > INTRUSION_EPS_MM3:
                raise GeometryError(
                    f"{name} intrudes: {intrusions} exceeds the {INTRUSION_EPS_MM3} mm^3 "
                    "boolean-noise limit")
            boss = boss_at(moved, axis)
            if boss is None:
                raise GeometryError(f"{name}: no SMT boss body found on axis {axis}")
            if not (boss[0] - 1e-6 <= axis[0] - od / 2 and axis[0] + od / 2 <= boss[1] + 1e-6
                    and boss[2] - 1e-6 <= axis[1] - od / 2 and axis[1] + od / 2 <= boss[3] + 1e-6):
                raise GeometryError(
                    f"{name}: OD {od} is not contained by the boss body "
                    f"x {boss[0]:.2f}..{boss[1]:.2f} y {boss[2]:.2f}..{boss[3]:.2f}")
            solids[name] = spacer
            spacer_meta.append({
                "name": name,
                "axis_xy_mm": [axis[0], axis[1]],
                "z_mm": [round(bb.zmin, 3), round(bb.zmax, 3)],
                "length_mm": round(bb.zmax - bb.zmin, 3),
                "outer_diameter_mm": od,
                "inner_diameter_mm": idia,
                "boss_body_xy_mm": [round(boss[0], 3), round(boss[1], 3),
                                    round(boss[2], 3), round(boss[3], 3)],
                "intrusion_volume_mm3": intrusions,
                "board_hole_diameter_mm": hole,
                "annulus_bearing_width_mm": round((od - hole) / 2, 3),
            })
        if length is not None:
            meta["LCD_MOUNT_PLACEHOLDERS"] = {
                "kind": "MODEL-FIT GEOMETRIC PLACEHOLDER, non-approved",
                "count": len(spacer_meta),
                "span_mm": [boss_rear_z, board_top_z],
                "spacers": spacer_meta,
                "not_claimed": [
                    "an approved spacer length",
                    "a part, part number or supplier",
                    "a standoff diameter or a thread",
                    "thread engagement, torque or assembly order",
                    "any physical fit",
                ],
                "first_article_tasks": mount["first_article_tasks"],
                "intrusion_limit_mm3": INTRUSION_EPS_MM3,
                "intrusion_note": ("the reported volumes are boolean overlaps. Touching faces "
                                   "measure about 1e-5 mm^3; the limit separates that noise from "
                                   "a real penetration"),
            }
    else:
        meta["LCD_MOUNT_PLACEHOLDERS"] = {
            "kind": "SKIPPED",
            "reason": ("no verified vendor display STEP was supplied, so there is no drawn "
                       "boss plane. The 14.05 mm design envelope is NOT used to fabricate "
                       "one"),
        }

    # ---- display connector PLANNING MARKER ------------------------------
    # LEGACY, UNVERIFIED planning data from lib/layout.ts (drawing-derived).
    # This is NOT a verified connector: the 21.412 x 4.826 mm size has no
    # reliable source in the vendor model (the bodies that carry that size are
    # TFT1 flex bonds, see VENDOR_DISPLAY_MODEL["tft1_flex_bonds"]), and the
    # marker sits on the layout's display PCB rear datum
    # (PROVISIONAL_Z.displayPcbRear), not on the vendor model's drawn PCB rear
    # plane (6.24 mm below its glass).
    # Routing, connector identity and fit are unknown.
    fpc = layout["fpcConnector"]
    vendor_pcb_rear = placed_display_planes(layout)["pcb_rear"]
    fpc_wp = box(fpc["at"]["x"] - fpc["bodyWidth"] / 2,
                 fpc["at"]["y"] - fpc["bodyLength"] / 2,
                 pz["displayPcbRear"],
                 fpc["at"]["x"] + fpc["bodyWidth"] / 2,
                 fpc["at"]["y"] + fpc["bodyLength"] / 2,
                 pz["displayPcbRear"] + 1.8)
    solids["DISPLAY_CONNECTOR_PLANNING_MARKER"] = fpc_wp
    meta["DISPLAY_CONNECTOR_PLANNING_MARKER"] = {
        "kind": "PLANNING MARKER, not a verified connector",
        "data_source": ("legacy drawing-derived values in tscircuit/lib/layout.ts "
                        "(DISPLAY_FPC_CONNECTOR / DISPLAY_FPC_CONNECTOR_AT)"),
        "size_mm": [fpc["bodyLength"], fpc["bodyWidth"], 1.8],
        "size_source": ("UNVERIFIED. No reliable source: the vendor model's 21.412 x 4.826 mm "
                        "bodies are TFT1 flex bonds (EXTRUDED-106739 / EXTRUDED-107488), not "
                        "this connector"),
        "position_xy_mm": [fpc["at"]["x"], fpc["at"]["y"]],
        "position_source": ("UNVERIFIED drawing-derived record; the long-axis wording is "
                            "disputed and the position/length arithmetic needs independent "
                            "re-derivation"),
        "z_mm": [round(pz["displayPcbRear"], 3), round(pz["displayPcbRear"] + 1.8, 3)],
        "z_basis": ("the layout's display PCB rear datum (PROVISIONAL_Z.displayPcbRear = "
                    f"{round(pz['displayPcbRear'], 3):.2f} mm panel Z, i.e. the display glass "
                    "datum plus the provisional glass-to-PCB-rear split). The drawn vendor "
                    f"model puts its own PCB rear face at panel Z = {vendor_pcb_rear:.2f} mm, "
                    "so this marker is not on that plane"),
        "fit_and_routing": "UNKNOWN: no clearance, bend radius or cable route is claimed",
    }

    # ---- Adafruit 908 flange volumes ----------------------------------
    for name in ("FEEDTHROUGH_UMC_AUDIO_USB_FLANGE", "FEEDTHROUGH_MIDI_HUB_USB_FLANGE"):
        e = env[name]
        # The drawing gives no depth, so no Z volume is drawn. The plan outline
        # is exported as a thin sheet at the panel rear face, which is a marker,
        # not a body: the metadata says the 3D result is undetermined.
        sheet = box(e.x.lo, e.y.lo, pz["panelRear"], e.x.hi, e.y.hi, pz["panelRear"] + 0.2)
        solids[name + "_PLAN_MARKER"] = sheet
        meta[name + "_PLAN_MARKER"] = {
            "note": ("plan outline of the Adafruit 908 flange at its documented size, drawn as "
                     "a 0.2 mm marker on the panel rear face because the drawing states NO "
                     "body depth; it is not the flange volume and it produces no verdict"),
        }

    # ---- opt-in USB-C candidate ---------------------------------------
    if include_usbc:
        e = env["USB_C_CANDIDATE_FLANGE_NON_APPROVED"]
        solids["USB_C_CANDIDATE_FLANGE_NON_APPROVED"] = span_box(e, grow=0.0)
        meta["USB_C_CANDIDATE_FLANGE_NON_APPROVED"] = {
            "note": ("NON-APPROVED candidate: advertised flange 25 x 10.2 x 5 mm from a "
                     "marketplace listing. No mounting pitch, current rating or PD result is "
                     "verified, and the panel cutout is unchanged"),
        }

    return {"solids": solids, "meta": meta, "envelopes": env}


def build_scale_check(cq):
    """A 100.00 x 10.00 x 3.00 mm bar: the printer scale reference."""
    return (cq.Workplane("XY").box(100.0, 10.0, 3.0)
            .translate((50.0, 0.0, 1.5)))


def reach_gauge_dimensions(layout: dict) -> dict:
    """The two same-datum planes the gauge shows, and the SIGNED distance.

    Both heights start at the panel FRONT face (Z = 0, +Z rearward):

      tall  = max(tip plane, board plane)     the taller of the two planes
      low   = min(tip plane, board plane)     the shorter one
      step  = tall - low                       magnitude only, always >= 0
      signed = board plane - tip plane         NEGATIVE means the tip plane lies
                                               BEHIND the board face, i.e. the
                                               pipe crosses it

    EITHER ORDER IS VALID MODEL GEOMETRY. The earlier version raised whenever the
    pipe was not in front of the board, which rejected the only case the current
    stack has (16.5 mm tip vs a 10.6 mm board face) and could not draw it at all.

    What the numbers are NOT: the step is not an LED gap, not an optical result and
    not a fit. The head underside on the panel front face is an ASSUMPTION, the LED
    height, the retention in 1.6 mm FR-4 and the stack tolerances are unmeasured, so
    `opticalGap` stays null in the reach document.
    """
    tip = float(layout["lightPipeReach"]["nominalTipZ"])              # 16.5 shaft length
    board = float(layout["lightPipeReach"]["boardNearFaceZ"])         # 10.6 board face
    if tip <= 0.0 or board <= 0.0:
        raise GeometryError(f"the gauge needs two positive planes, got tip {tip} "
                            f"and board {board}")
    tall, low = max(tip, board), min(tip, board)
    signed = round(board - tip, 6)
    order = "tip_behind_board" if signed < 0 else (
        "tip_flush_with_board" if signed == 0 else "tip_in_front_of_board")
    return {
        "datum": "panel FRONT face, Z = 0, +Z rearward",
        "tall": tall,
        "low": low,
        "step": round(tall - low, 6),
        "signed_board_minus_tip": signed,
        "direction": order,
        "penetrates_board_plane": signed < 0,
        "step_meaning": ("the distance between the nominal pipe tip plane and the "
                         "provisional board face. Not an LED gap, not a fit, not a "
                         "measured stack value"),
    }


def build_reach_gauge(cq, layout: dict):
    """A same-datum step gauge that draws BOTH plane orders.

    Two bars start on the panel FRONT face: the TALL bar reaches the taller of the
    two planed heights, the SHORT bar the other one, so the step is always
    ``|board - tip|``. The gauge is a viewing and print aid for the two NOMINAL
    planes; it is not a fit, an optical gap or a measured stack.
    """
    dims = reach_gauge_dimensions(layout)
    tall, low = dims["tall"], dims["low"]
    width, depth = 20.0, 8.0
    tall_body = (cq.Workplane("XY").box(depth, width / 2, tall)
                 .translate((depth / 2, -width / 4, tall / 2)))
    low_body = (cq.Workplane("XY").box(depth, width / 2, low)
                .translate((depth / 2, width / 4, low / 2)))
    gauge = tall_body.union(low_body)
    if gauge.solids().size() != 1:
        raise GeometryError(f"the reach gauge is {gauge.solids().size()} solids, expected 1")
    bb = gauge.val().BoundingBox()
    if abs(bb.zlen - tall) > 1e-6:
        raise GeometryError(f"the gauge height {bb.zlen:.3f} is not the taller plane "
                            f"{tall:.3f}")
    if abs(bb.zmin) > 1e-6:
        raise GeometryError(f"the gauge starts at Z {bb.zmin:.3f}, not the panel front 0")
    if low <= 0.0:
        raise GeometryError(f"the shorter plane {low:.3f} is not above the gauge's datum")
    return gauge


# ---------------------------------------------------------------------------
# The 20HP Pi mounting panel (owner decision 2026-11-12): a verification model in
# its own frame. The panel, and a PLACEHOLDER Pi board hung behind it on
# standoffs. The Pi board's holes are derived HERE in the board frame from the
# drawing constants and mapped through the mirror. They must coincide with the
# panel's holes in layout.json, so a mirror or pattern error fails the build.
# ---------------------------------------------------------------------------
def pi_board_to_panel(layout: dict, bx: float, by: float) -> tuple[float, float]:
    """Component side AWAY from the panel: the board X is mirrored and both axes
    are centred. Must match piBoardToPanel in tscircuit/lib/layout.ts."""
    b = layout["piBoard"]
    return (b["length"] / 2 - bx, by - b["width"] / 2)


def board_hole_centres(layout: dict) -> list[tuple[float, float]]:
    """The Pi 5's four mounting-hole centres in the BOARD frame (drawing)."""
    b = layout["piBoard"]
    i = b["holeInset"]
    px, py = b["holePitch"]["x"], b["holePitch"]["y"]
    return [(i, i), (i, i + py), (i + px, i), (i + px, i + py)]


def build_pi_panel(cq, layout: dict) -> dict:
    """Return the panel and placeholder Pi board solids and the checks made."""
    pp = layout["piPanel"]
    b = layout["piBoard"]
    width, height, thick = float(pp["width"]), float(pp["height"]), float(pp["thickness"])
    board_z = thick + float(layout["piSpacer"])   # candidate standoff, unmeasured
    d_pi = float(pp["piMount"]["holeDiameter"])
    d_rail = float(pp["railHoleDiameter"])

    def cylinder(x, y, z1, z2, radius):
        return (cq.Workplane("XY").workplane(offset=z1).center(x, y)
                .circle(radius).extrude(z2 - z1))

    # Z datum as the rest of the model: the panel front face is Z = 0, +Z rearward.
    panel = cq.Workplane("XY").box(width, height, thick, centered=(True, True, False))
    for h in pp["piMountHoles"]:
        panel = panel.cut(cylinder(h["x"], h["y"], -1.0, thick + 1.0, d_pi / 2))
    for h in pp["railHoles"]:
        panel = panel.cut(cylinder(h["x"], h["y"], -1.0, thick + 1.0, d_rail / 2))
    if panel.solids().size() != 1:
        raise GeometryError(f"the Pi panel is {panel.solids().size()} solids")

    board = (cq.Workplane("XY")
             .box(b["length"], b["width"], b["thickness"], centered=(True, True, False))
             .translate((0, 0, board_z)))
    derived = []
    for bx, by in board_hole_centres(layout):
        x, y = pi_board_to_panel(layout, bx, by)
        derived.append((x, y))
        board = board.cut(cylinder(x, y, board_z - 1.0,
                                   board_z + b["thickness"] + 1.0, b["holeDiameter"] / 2))

    # Independent check: every board hole derived from the drawing lands on a
    # panel hole, and the panel has no hole the board does not use.
    panel_holes = [(float(h["x"]), float(h["y"])) for h in pp["piMountHoles"]]
    worst = 0.0
    for dx, dy in derived:
        nearest = min(math.hypot(dx - px, dy - py) for px, py in panel_holes)
        worst = max(worst, nearest)
        if nearest > COINCIDENCE_TOL_MM:
            raise GeometryError(
                f"Pi hole derived from the drawing at ({dx:.3f}, {dy:.3f}) misses every "
                f"panel hole by {nearest:.3f} mm: the mirror or the pattern is wrong")
    if len(panel_holes) != len(derived):
        raise GeometryError("the panel and the Pi board carry different hole counts")

    # The board must sit inside the panel with its holes clear of the edges.
    half_w, half_h = width / 2, height / 2
    for x, y in derived:
        if abs(x) >= half_w or abs(y) >= half_h:
            raise GeometryError("a Pi hole lies outside the panel")
    if b["length"] / 2 >= half_w or b["width"] / 2 >= half_h:
        raise GeometryError("the Pi board does not fit inside the panel")

    # The connector envelope recorded in layout.json must match the mirror too.
    env = pp["piEnvelope"]
    env_x1, _ = pi_board_to_panel(layout, b["length"] + layout["piProtrusion"]["right"], 0)
    env_x2, _ = pi_board_to_panel(layout, -layout["piProtrusion"]["left"], 0)
    if abs(env_x1 - env["x1"]) > COINCIDENCE_TOL_MM or abs(env_x2 - env["x2"]) > COINCIDENCE_TOL_MM:
        raise GeometryError("piEnvelope in layout.json disagrees with the mirrored connector edges")

    return {
        "solids": {PI_PANEL_BODY: panel, PI_BOARD_BODY: board},
        "checks": {
            "mirrored_x": True,
            "board_holes_derived_from_drawing": [list(map(lambda v: round(v, 3), p)) for p in derived],
            "worst_hole_offset_mm": round(worst, 6),
            "coincidence_tolerance_mm": COINCIDENCE_TOL_MM,
            "board_z_mm": round(board_z, 3),
            "standoff_mm_candidate": float(layout["piSpacer"]),
        },
    }


def export_assembly(cq, solids: dict, path: Path) -> None:
    assy = cq.Assembly()
    palette = {
        "PANEL": cq.Color(0.30, 0.32, 0.35, 1.0),
        "FRONT_BOARD": cq.Color(0.10, 0.45, 0.25, 1.0),
        "REAR_BOARD": cq.Color(0.10, 0.35, 0.55, 1.0),
        "DISPLAY": cq.Color(0.65, 0.70, 0.75, 1.0),
        "FEEDTHROUGH": cq.Color(0.85, 0.60, 0.15, 1.0),
        "USB_C": cq.Color(0.85, 0.30, 0.30, 1.0),
    }

    def colour_for(name: str):
        for prefix, colour in palette.items():
            if name.startswith(prefix):
                return colour
        return cq.Color(0.5, 0.5, 0.5, 1.0)

    for name in sorted(solids):
        assy.add(solids[name], name=name, color=colour_for(name))
    assy.save(str(path), exportType="STEP")
    text = path.read_text(errors="replace")
    missing = [name for name in solids if name not in text]
    if missing:
        raise GeometryError(f"the STEP file does not name {missing}; named solids are required")


def verify_roundtrip(cq, path: Path, body_names: list[str]) -> dict:
    """Re-import the STEP: it must keep one solid per named body, with volume."""
    shape = cq.importers.importStep(str(path)).val()
    solids = shape.Solids()
    total = shape.Volume()
    if total <= 0:
        raise GeometryError("the re-imported STEP has zero volume")
    if len(solids) < len(body_names):
        raise GeometryError(
            f"the re-imported STEP has {len(solids)} solids, fewer than the "
            f"{len(body_names)} named bodies")
    bb = shape.BoundingBox()
    return {"solids": len(solids), "volume_mm3": total,
            "bbox_mm": [bb.xlen, bb.ylen, bb.zlen]}


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--out", default=None,
                        help="output directory (default: mechanical/cad-out)")
    parser.add_argument("--force", action="store_true",
                        help="allow an output directory that holds other files; nothing is deleted")
    parser.add_argument("--display-step", default=None,
                        help="vendor display STEP to verify and use")
    parser.add_argument("--display-archive", default=None,
                        help="vendor drawing ZIP: verifies the archive hash AND the "
                             "STEP hash inside it, then extracts it to a temp dir")
    parser.add_argument("--usbc-candidate-envelope", action="store_true",
                        help="add the NON-APPROVED candidate USB-C flange envelope")
    args = parser.parse_args(argv)
    if args.display_step and args.display_archive:
        parser.error("give --display-step or --display-archive, not both")

    try:
        out = resolve_output_dir(args.out, force=args.force)
    except GeometryError as exc:
        print(f"FAIL {exc}", file=sys.stderr)
        return 1

    try:
        import cadquery as cq
    except ImportError as exc:  # pragma: no cover - environment dependent
        print(f"CadQuery is not importable ({exc}).\n"
              "Install it in a private environment, do not touch the system Python:\n"
              "  uv venv /tmp/cq-venv\n"
              "  uv pip install --python /tmp/cq-venv/bin/python cadquery\n"
              "See the module docstring for the OpenGL library note.", file=sys.stderr)
        return 1

    layout = load_layout()
    try:
        if args.display_archive:
            display_step, display_info = resolve_display_archive(args.display_archive)
        else:
            display_step, display_info = resolve_display_step(args.display_step)
        built = build(cq, layout, display_step, args.usbc_candidate_envelope)
        gauge = build_reach_gauge(cq, layout)
        scale = build_scale_check(cq)
        pi_panel = build_pi_panel(cq, layout)
    except GeometryError as exc:
        print(f"FAIL {exc}", file=sys.stderr)
        return 1

    out.mkdir(parents=True, exist_ok=True)
    solids = built["solids"]
    step_path = out / STEP_NAME
    export_assembly(cq, solids, step_path)
    roundtrip = verify_roundtrip(cq, step_path, sorted(solids))

    stls = {}
    for name, file_name in (("PANEL_30HP_3U", STL_PANEL),
                            ("FRONT_BOARD_PROVISIONAL_Z", STL_FRONT),
                            ("REAR_BOARD_PROVISIONAL_Z", STL_REAR)):
        cq.exporters.export(solids[name], str(out / file_name))
        bb = solids[name].val().BoundingBox()
        stls[file_name] = {"mm": [bb.xlen, bb.ylen, bb.zlen]}
    cq.exporters.export(scale, str(out / STL_SCALE))
    stls[STL_SCALE] = {"mm": [100.0, 10.0, 3.0]}
    cq.exporters.export(gauge, str(out / STL_GAUGE))
    gbb = gauge.val().BoundingBox()
    stls[STL_GAUGE] = {"mm": [gbb.xlen, gbb.ylen, gbb.zlen]}

    pi_step = out / PI_PANEL_STEP
    export_assembly(cq, pi_panel["solids"], pi_step)
    pi_roundtrip = verify_roundtrip(cq, pi_step, sorted(pi_panel["solids"]))
    cq.exporters.export(pi_panel["solids"][PI_PANEL_BODY], str(out / STL_PI_PANEL))
    pbb = pi_panel["solids"][PI_PANEL_BODY].val().BoundingBox()
    stls[STL_PI_PANEL] = {"mm": [pbb.xlen, pbb.ylen, pbb.zlen]}

    document = assumptions_document(layout,
                                    include_usbc_candidate=args.usbc_candidate_envelope)
    document["outputs"] = {
        "directory": str(out),
        "files": [STEP_NAME, PI_PANEL_STEP] + sorted(stls) + [ASSUMPTIONS, CHECKLIST],
    }
    document["display_step"] = display_info
    document["pi_panel"] = {**pi_panel["checks"],
                            "bodies": sorted(pi_panel["solids"]),
                            "step_roundtrip": pi_roundtrip}
    # The pure document already carries the mount block and the body legend;
    # add what only the built solids know.
    document["display_mount_placeholders"]["bodies"] = built["meta"].get(
        "LCD_MOUNT_PLACEHOLDERS", {})
    document["bodies"] = built["meta"]
    document["step_roundtrip"] = roundtrip
    document["volumes_mm3"] = {name: round(volume_of(solids[name]), 3)
                              for name in sorted(solids)}
    document["stl_bounding_boxes_mm"] = stls
    reach = compute_reach(ReachInputs.from_layout(layout))
    document["light_pipe_reach"] = {
        "part": layout["lightPipe"]["part"],
        "candidate_status": layout["lightPipe"]["status"],
        "shaft_length_mm": layout["lightPipe"]["shaftLength"],
        "overall_length_mm": layout["lightPipe"]["overallLength"],
        "head_height_mm": layout["lightPipe"]["headHeight"],
        "nominal_tip_z_mm": reach.nominal_tip_z_mm,
        "nominal_tip_to_board_plane_mm": reach.nominal_tip_to_board_plane_mm,
        "optical_gap_mm": reach.optical_gap_mm,
        "status": reach.status,
        "missing": list(reach.missing),
        "gauge_mm": reach_gauge_dimensions(layout),
    }
    (out / ASSUMPTIONS).write_text(json.dumps(document, indent=2) + "\n")
    (out / CHECKLIST).write_text(print_checklist_markdown())

    print("== MIDI Brain STEP verification model (PROVISIONAL) ==")
    print(f"output directory      {out}")
    print(f"display vendor STEP   {display_info['status']}")
    print(f"STEP solids           {len(solids)} named bodies, "
          f"{roundtrip['solids']} solids on re-import, {roundtrip['volume_mm3']:.1f} mm^3")
    print(f"STEP bbox             x {roundtrip['bbox_mm'][0]:.2f} "
          f"y {roundtrip['bbox_mm'][1]:.2f} z {roundtrip['bbox_mm'][2]:.2f} mm")
    print(f"Pi panel model        {len(pi_panel['solids'])} bodies, Pi holes coincide with the "
          f"drawing-derived holes within {pi_panel['checks']['worst_hole_offset_mm']:.4f} mm")
    for file_name in [STEP_NAME, PI_PANEL_STEP] + sorted(stls):
        print(f"  wrote {out / file_name}")
    print(f"{BOARD_MOCKUP_DISCLAIMER}: {STL_FRONT} and {STL_REAR} are NOT the "
          "current PCB geometry; their cuts do not follow the board sources.")
    for file_name, defects in BOARD_MOCKUP_DEFECTS.items():
        for defect in defects:
            print(f"  {file_name}: {defect}")
    print("  the panel mockup, the Pi panel mockup, the scale bar and the light-pipe "
          "gauge are unaffected; see the print checklist and docs/TODO.md")
    print("pair verdicts:")
    for pair in document["pairs"]:
        print(f"  {pair['a']} vs {pair['b']}: {pair['status']}"
              + (f" (clearance {pair['clearance_mm']:.2f} mm)" if pair["clearance_mm"] is not None else ""))
    print("NOT a released drawing and NOT a fit approval: see assumptions.json.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

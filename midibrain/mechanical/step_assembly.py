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
                                   and FPC envelopes, optional USB-C candidate
    mockup-panel.stl               1:1 panel mockup
    mockup-front-board.stl         1:1 front/UI board mockup
    mockup-rear-board.stl          1:1 rear/base board mockup
    scale-check-100mm.stl          100.00 x 10.00 x 3.00 mm printer scale bar
    light-pipe-reach-gauge.stl     17.45 / 10.16 mm step gauge (see below)
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
import sys
import tempfile
import zipfile
from pathlib import Path

from bracket_geometry import GeometryError, load_layout
from light_pipe_reach import ReachInputs, compute_reach
from step_geometry import (
    DISPLAY_ORIENTATION,
    VENDOR_DISPLAY_MODEL,
    assembly_envelopes,
    assumptions_document,
    check_display_plane_order,
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


def place_display(cq, shape, layout: dict) -> tuple[object, dict]:
    """Place the vendor model: glass front on the panel front face, electronics rearward.

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
    dz = 0.0 - glass_z_rotated

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
    expected = placed_display_planes()
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
        "glass_datum": ("the vendor glass FRONT face at the panel front face, Z = 0 "
                        "(assumption A); illustrative placement, not a fit"),
        "front_frame_disclosure": (
            "the vendor touch/frame forward face is 1.90 mm IN FRONT of the glass front "
            "face in the model, so this placement puts it at global Z = -1.90 mm, i.e. "
            "1.90 mm proud of the panel front face. It is NOT flush with the panel and "
            "NOT a fit: the real frame/touch layer, the aperture relief and the seating "
            "are unmeasured"),
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


def build(cq, layout: dict, display_step: Path | None, include_usbc: bool) -> dict:
    """Build every solid and return name -> object, ready to export."""
    pz = layout["provisionalZ"]
    depth = layout["provisionalDepth"]
    env = assembly_envelopes(layout, include_usbc_candidate=include_usbc)

    def box(x1, y1, z1, x2, y2, z2):
        """Axis-aligned box from two corners, grown a little where a cut needs it."""
        return (cq.Workplane("XY").workplane(offset=z1)
                .center((x1 + x2) / 2, (y1 + y2) / 2)
                .rect(abs(x2 - x1), abs(y2 - y1))
                .extrude(z2 - z1))

    def cylinder(x, y, z1, z2, radius):
        return (cq.Workplane("XY").workplane(offset=z1).center(x, y)
                .circle(radius).extrude(z2 - z1))

    def span_box(e, grow: float = 1.0):
        """Box for an envelope, grown in Z so a cut passes through it."""
        return box(e.x.lo, e.y.lo, e.z.lo - grow, e.x.hi, e.y.hi, e.z.hi + grow)

    solids: dict[str, object] = {}
    meta: dict[str, dict] = {}

    # ---- panel --------------------------------------------------------
    e = env["PANEL_32HP_3U"]
    panel_wp = span_box(e, grow=0.0)
    # Every panel opening and every panel drill comes from the canonical
    # PANEL_APERTURES / PANEL_DRILLS arrays in layout.ts, the same data the
    # panel board renders and lib/panel-cutouts.test.ts compares with the built
    # panel JSON. Nothing is restated here.
    cuts = []
    for aperture in layout["panelApertures"]:
        name = aperture["name"]
        if aperture["shape"] == "polygon":
            tool = (cq.Workplane("XY").workplane(offset=e.z.lo - 1.0)
                    .polyline([(p["x"], p["y"]) for p in aperture["points"]]).close()
                    .extrude((e.z.hi - e.z.lo) + 2.0))
            cuts.append((f"aperture {name}", tool))
        elif aperture["shape"] == "rect":
            cuts.append((f"aperture {name}",
                         box(aperture["x"] - aperture["width"] / 2,
                             aperture["y"] - aperture["height"] / 2,
                             e.z.lo - 1.0,
                             aperture["x"] + aperture["width"] / 2,
                             aperture["y"] + aperture["height"] / 2,
                             e.z.hi + 1.0)))
        elif aperture["shape"] == "circle":
            cuts.append((f"aperture {name}",
                         cylinder(aperture["x"], aperture["y"], e.z.lo - 1.0,
                                  e.z.hi + 1.0, aperture["radius"])))
        else:  # pragma: no cover - the data union has three shapes
            raise GeometryError(f"panel aperture {name} has an unknown shape")
    for drill in layout["panelDrills"]:
        cuts.append((f"drill {drill['name']}",
                     cylinder(drill["x"], drill["y"], e.z.lo - 1.0, e.z.hi + 1.0,
                              drill["diameter"] / 2)))

    expected_cuts = len(layout["panelApertures"]) + len(layout["panelDrills"])
    if expected_cuts != 34:
        raise GeometryError(
            f"the canonical panel data holds {expected_cuts} features, expected 34 "
            "(20 apertures + 14 drills); the panel model would silently lose geometry")

    for label, tool in cuts:
        if panel_wp.val().intersect(tool.val()).Volume() <= VOLUME_EPS:
            raise GeometryError(f"panel cut '{label}' removes nothing: it is outside the stock")
        panel_wp = panel_wp.cut(tool)
    if panel_wp.solids().size() != 1:
        raise GeometryError(f"the panel is {panel_wp.solids().size()} solids after its cuts")
    solids["PANEL_32HP_3U"] = panel_wp
    meta["PANEL_32HP_3U"] = {
        "features": [label for label, _ in cuts],
        "feature_count": len(cuts),
        "note": ("20 canonical apertures and 14 canonical drills, the same arrays "
                 "lib/panel-cutouts.test.ts compares with the built panel JSON"),
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
    for label, hole, diameter in holes:
        tool = cylinder(hole["x"], hole["y"], e.z.lo - 1.0, e.z.hi + 1.0, diameter / 2)
        if front_wp.val().intersect(tool.val()).Volume() <= VOLUME_EPS:
            raise GeometryError(f"front-board cut '{label}' removes nothing")
        front_wp = front_wp.cut(tool)
    if front_wp.solids().size() != 1:
        raise GeometryError(f"the front board is {front_wp.solids().size()} solids")
    solids["FRONT_BOARD_PROVISIONAL_Z"] = front_wp
    meta["FRONT_BOARD_PROVISIONAL_Z"] = {"features": ["top relief notch"] + [h[0] for h in holes]}

    # ---- rear board ---------------------------------------------------
    e = env["REAR_BOARD_PROVISIONAL_Z"]
    rear_wp = box(e.x.lo, e.y.lo, e.z.lo, e.x.hi, e.y.hi, e.z.hi)
    rear_holes = [(f"front-rear mount {i}", h, layout["mountHoleDiameter"])
                  for i, h in enumerate(layout["frontRearMount"])]
    rear_holes += [(f"Pi mount hole {i}", h, layout["mountHoleDiameter"])
                   for i, h in enumerate(layout["piHoles"])]
    for label, hole, diameter in rear_holes:
        tool = cylinder(hole["x"], hole["y"], e.z.lo - 1.0, e.z.hi + 1.0, diameter / 2)
        if rear_wp.val().intersect(tool.val()).Volume() <= VOLUME_EPS:
            raise GeometryError(f"rear-board cut '{label}' removes nothing")
        rear_wp = rear_wp.cut(tool)
    solids["REAR_BOARD_PROVISIONAL_Z"] = rear_wp
    meta["REAR_BOARD_PROVISIONAL_Z"] = {
        "features": [h[0] for h in rear_holes],
        "note": f"Z from PROVISIONAL_Z plus the candidate {depth['frontToRearGap']:.2f} mm gap",
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
                     "outline between the assumed flush glass front and the 14.05 mm overall "
                     "depth; it is not vendor geometry"),
        }

    # ---- display connector PLANNING MARKER ------------------------------
    # LEGACY, UNVERIFIED planning data from lib/layout.ts (drawing-derived).
    # This is NOT a verified connector: the 21.412 x 4.826 mm size has no
    # reliable source in the vendor model (the bodies that carry that size are
    # TFT1 flex bonds, see VENDOR_DISPLAY_MODEL["tft1_flex_bonds"]), and the
    # marker sits on the PROVISIONAL design-chain plane (displayPcbRear =
    # 10.05 mm), not on the vendor model's drawn PCB rear plane (6.24 mm).
    # Routing, connector identity and fit are unknown.
    fpc = layout["fpcConnector"]
    vendor_pcb_rear = placed_display_planes()["pcb_rear"]
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
        "z_basis": ("the PROVISIONAL design-chain plane (assumption A + B + the 10.05 mm "
                    f"glass-to-PCB-rear split). The drawn vendor model puts its PCB rear face "
                    f"at global Z = {vendor_pcb_rear:.2f} mm, so this marker is not on it"),
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


def build_reach_gauge(cq, layout: dict):
    """A step gauge whose two heights are the two documented numbers.

    17.45 mm is the provisional panel-to-board gap; 10.16 mm is the candidate
    pipe length; the step between them is the 7.29 mm that nothing accounts for.
    No text is engraved, and no dimension is claimed as measured.
    """
    gap = float(layout["panelToFrontBoardGap"])
    pipe = float(layout["lightPipe"]["length"])
    if pipe >= gap:
        raise GeometryError(
            f"the candidate pipe ({pipe} mm) is not shorter than the gap ({gap} mm); "
            "the gauge would have no step to show")
    width, depth = 20.0, 8.0
    tall = (cq.Workplane("XY").box(depth, width / 2, gap)
            .translate((depth / 2, -width / 4, gap / 2)))
    low = (cq.Workplane("XY").box(depth, width / 2, pipe)
           .translate((depth / 2, width / 4, pipe / 2)))
    gauge = tall.union(low)
    if gauge.solids().size() != 1:
        raise GeometryError(f"the reach gauge is {gauge.solids().size()} solids, expected 1")
    bb = gauge.val().BoundingBox()
    if abs(bb.zlen - gap) > 1e-6:
        raise GeometryError(f"the gauge height {bb.zlen:.3f} is not the gap {gap:.3f}")
    return gauge


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
    except GeometryError as exc:
        print(f"FAIL {exc}", file=sys.stderr)
        return 1

    out.mkdir(parents=True, exist_ok=True)
    solids = built["solids"]
    step_path = out / STEP_NAME
    export_assembly(cq, solids, step_path)
    roundtrip = verify_roundtrip(cq, step_path, sorted(solids))

    stls = {}
    for name, file_name in (("PANEL_32HP_3U", STL_PANEL),
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

    document = assumptions_document(layout,
                                    include_usbc_candidate=args.usbc_candidate_envelope)
    document["outputs"] = {
        "directory": str(out),
        "files": [STEP_NAME] + sorted(stls) + [ASSUMPTIONS, CHECKLIST],
    }
    document["display_step"] = display_info
    document["bodies"] = built["meta"]
    document["step_roundtrip"] = roundtrip
    document["volumes_mm3"] = {name: round(volume_of(solids[name]), 3)
                              for name in sorted(solids)}
    document["stl_bounding_boxes_mm"] = stls
    reach = compute_reach(ReachInputs.from_layout(layout))
    document["light_pipe_reach"] = {
        "status": reach.status,
        "missing": list(reach.missing),
        "gap_minus_length_arithmetic_mm": round(reach.nominal_gap_minus_length_mm, 6),
        "gauge_mm": {"tall": float(layout["panelToFrontBoardGap"]),
                     "low": float(layout["lightPipe"]["length"]),
                     "step": round(float(layout["panelToFrontBoardGap"])
                                   - float(layout["lightPipe"]["length"]), 6)},
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
    for file_name in [STEP_NAME] + sorted(stls):
        print(f"  wrote {out / file_name}")
    print("pair verdicts:")
    for pair in document["pairs"]:
        print(f"  {pair['a']} vs {pair['b']}: {pair['status']}"
              + (f" (clearance {pair['clearance_mm']:.2f} mm)" if pair["clearance_mm"] is not None else ""))
    print("NOT a released drawing and NOT a fit approval: see assumptions.json.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

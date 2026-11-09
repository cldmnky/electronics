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
    assembly_envelopes,
    assumptions_document,
    print_checklist_markdown,
    resolve_output_dir,
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
    d = layout["display"]
    if display_step is not None:
        shape = cq.importers.importStep(str(display_step)).val()
        bb = shape.BoundingBox()
        moved = shape.moved(cq.Location(cq.Vector(
            d["x"] - (bb.xmin + bb.xmax) / 2,
            d["y"] - (bb.ymin + bb.ymax) / 2,
            0.0 - bb.zmin)))
        mb = moved.BoundingBox()
        solids["DISPLAY_WAVESHARE_VENDOR_STEP"] = moved
        meta["DISPLAY_WAVESHARE_VENDOR_STEP"] = {
            "source": str(display_step),
            "placement": ("bounding-box centre to the module centre and bounding-box minimum Z "
                          "to the panel front face (assumption A); the vendor model's own Z "
                          "origin is not documented"),
            "bbox_mm": [mb.xlen, mb.ylen, mb.zlen],
            "solids": len(moved.Solids()),
            "note": ("vendor geometry as supplied; the reference model is 13.82 mm deep, while "
                     "the stack uses the vendor's stated 14.05 mm maximum"),
        }
    else:
        e = env["DISPLAY_WAVESHARE_4_3_DSI_PROVISIONAL_Z"]
        solids["DISPLAY_ENVELOPE_PROVISIONAL"] = span_box(e, grow=0.0)
        meta["DISPLAY_ENVELOPE_PROVISIONAL"] = {
            "note": ("no verified vendor STEP was supplied, so this is the 106 x 68 mm module "
                     "outline between the assumed flush glass front and the 14.05 mm overall "
                     "depth; it is not vendor geometry"),
        }

    # ---- display FPC connector (provisional) --------------------------
    fpc = layout["fpcConnector"]
    fpc_wp = box(fpc["at"]["x"] - fpc["bodyWidth"] / 2,
                 fpc["at"]["y"] - fpc["bodyLength"] / 2,
                 pz["displayPcbRear"],
                 fpc["at"]["x"] + fpc["bodyWidth"] / 2,
                 fpc["at"]["y"] + fpc["bodyLength"] / 2,
                 pz["displayPcbRear"] + 1.8)
    solids["DISPLAY_FPC_CONNECTOR_PROVISIONAL"] = fpc_wp
    meta["DISPLAY_FPC_CONNECTOR_PROVISIONAL"] = {
        "note": ("body 21.412 x 4.826 mm on the display PCB rear face, 1.8 mm of stack from "
                 "the STEP's two extruded bodies; the placement on the rear face is the "
                 "drawing's, not a measured assembly transform"),
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

#!/usr/bin/env python3
"""Pure geometry for the three 3D-printed MIDI Brain front-panel parts.

Part 1 ``NAV keytop`` caps the ALPS Alpine SKRHABE010 5-way switch (U5).
Part 2 ``value knob`` caps the Bourns PEC11R-4015F-S0024 encoder (U6).
Part 3 ``B3F button cap`` is printed seven times, one for each Omron B3F-4055
switch (SW1-SW7).

Every dimension is either a named input with its source in the comment beside
it, or an explicit ASSUMPTION collected in ``ASSUMPTIONS`` and repeated in this
directory's README. Nothing is a silently picked fit-hardware number.

Sources
-------
``lib/layout.ts`` (canonical PCB/panel source, parsed because the panel
aperture, the display opening, the knob column and the printed B3F cap all live
there):

  * ``PANEL_CUTOUTS.control`` -- the shared 7.2 mm control aperture.
  * ``PANEL_CUTOUTS.button`` -- the 12.25 mm square B3F panel opening.
  * ``DISPLAY_CONCEPT.x`` + ``FRONT_DISPLAY_OPENING.width`` -- the front-board
    display opening's right edge, the edge the layout's own 2.50 mm
    knob-body rule is measured from.
  * ``RIGHT_COLUMN.x`` and the ``knob body (D12.5 at x = 52)`` note -- the
    owner-approved knob body diameter and column.
  * ``BUTTON_SWITCH``, ``BUTTON_REACH`` and ``BUTTON_CAP_PRINTED`` -- the
    B3F-4055 envelope, the printed cap's plate/locator sizes, its proud
    height and the by-design cap top. The button cap has no single panel
    position (one geometry, seven switches), so its shape is declared once
    there and its seven openings are read from the export below.

``mechanical/layout.json`` (generated export of the same source; carries the
panel stack, the real aperture list and the printed parts' envelope):

  * ``panel.thickness``, ``provisionalZ.panelFront/panelRear/frontBoardTop``
  * ``panelApertures`` entries ``NAV``, ``VALUE ENCODER`` and the seven
    ``* BUTTON`` openings
  * ``printedParts`` -- the shape and position of the NAV keytop and the value
    knob, i.e. the flange, shank, socket bore, head height, socket depth, knob
    body, height, bore and the two Z planes of each part. This module does not
    keep a second copy of any of those numbers; it re-derives the Z planes from
    the datasheet and tolerance chain below and fails when the two disagree.

``lib/layout.ts`` owns the display opening and the knob column, so those are
parsed from the source and never read from the export's ``display`` block:
``layout_export_notices`` reports a STALE EXPORT notice whenever the two
disagree, and the part geometry is unaffected either way because the knob OD is
fixed by the owner-approved layout note, not by the display position.

Datasheets (read and hashed by the reviewer, not committed):

  * ALPS Alpine SKRH series catalog, ``product_catalog_mu_02_skrh_en_34aeb8c2e3.pdf``
    (tech.alpsalpine.com, SHA-256 starts 83a37124, "Update 2510").
  * Bourns PEC11R series datasheet, SHA-256
    ``ac88f657e611bc82fe79b102426fb5abcb7f40740540f0c681727e2d74784491``
    (the hash already recorded in ``docs/decisions.md``).
  * Omron B3F datasheet, SHA-256
    ``be9cf69e5f43fb7689448a2097c19858e5643c0e2e49cd77d018881543e27a1f``:
    B3F-4055 12 +-0.2 mm square body, 3.8 +-0.1 mm projected plunger,
    7.3 +-0.2 mm above the board. The B32 cap top IS dimensioned (PCB top ->
    cap top, 10.0 +-0.4): what is missing there is where it would sit on THIS
    stack, which no bought cap has measured. That, and the owner's choice, are why
    the printed cap is the standard.

Z datum: the panel FRONT face is Z = 0 and +Z points REARWARD, the same datum
``lib/layout.ts`` and ``mechanical/layout.json`` use. The front board's top face
is therefore at Z = ``frontBoardTop`` and the panel occupies the two Z planes
``panelRear`` .. ``panelFront``.

No CAD library is imported here, so the design rules run on any machine:

    cd mechanical/printed && python3 -m unittest test_printed_parts -v

The solids are built by ``nav_keytop.py``, ``knob.py`` and ``button_cap.py``
(CadQuery).

Status: PROVISIONAL printed parts, not released drawings. Assembly-height and
print-fit inputs are assumptions until the first article is measured; the list
is in ASSUMPTIONS below and in README.md.
"""
from __future__ import annotations

import json
import math
import re
import struct
from dataclasses import dataclass
from pathlib import Path

HERE = Path(__file__).resolve().parent
LAYOUT_JSON = HERE.parent / "layout.json"
LAYOUT_TS = HERE.parent.parent / "tscircuit" / "lib" / "layout.ts"

# Dimension comparison tolerance: two millimetre values compared for equality.
EPS_MM = 1e-6

# The thinnest wall this part may have (mm). ASSUMPTION: a 0.4 mm-nozzle FDM
# print with two perimeters needs >= 0.8 mm of wall.
PRINT_MIN_WALL = 0.8


class GeometryError(AssertionError):
    """A design rule failed. Never caught to hide a failure."""


def need(condition: bool, message: str) -> None:
    if not condition:
        raise GeometryError(message)


# ---------------------------------------------------------------------------
# Datasheet inputs
# ---------------------------------------------------------------------------
# ALPS Alpine SKRH series catalog (SHA-256 83a37124..., "Update 2510").
# SKRHABE010 product-list row "7.35 x 7.5 x 1.8": body height above the PCB.
NAV_BODY_HEIGHT_ABOVE_MOUNTING = 1.8
# Drawing No.2 (the SKRHABE010 row) side view: overall height above the mounting
# surface.
NAV_HEIGHT_ABOVE_MOUNTING = 5.0
# Drawing No.2: stem diameter (Ø3 side view).
NAV_STEM_DIAMETER = 3.0
# Drawing No.2: vertical callout "1.7 max." on the keytop fitting area -- the
# tallest keytop socket ALPS qualifies over the stem.
NAV_KEYTOP_FITTING_AREA_MAX = 1.7
# Drawing No.2: travel, 4-direction and centre push.
# The product page for the EXACT part says, verbatim:
#   "4-directional operating force and travel are measured at 4.3mm height from
#    the bottom of the switch."
# (https://tech.alpsalpine.com/e/products/detail/SKRHABE010/). So the height at
# which the 0.25 mm 4-direction figure is measured IS specified: 4.3 mm from the
# bottom of the switch. The direction of the four arrows is still drawn at +-45
# deg to the part's axes, and no centre-push measurement height is stated.
NAV_TRAVEL_DIRECTION = 0.25
NAV_TRAVEL_CENTRE = 0.15
NAV_TRAVEL_MEASUREMENT_HEIGHT_MM = 4.3
NAV_TRAVEL_MEASUREMENT_NOTE = (
    'ALPS product page for SKRHABE010, verbatim: "4-directional operating force '
    'and travel are measured at 4.3mm height from the bottom of the switch."')
# The same page also carries, under notes COMMON to the series/models:
#   "Do not apply a force from the side of the stem"
# quoted here WITH that context. It sits beside a 4-direction control, so its
# applicability and the permitted force/method are NOT interpreted here: they are
# questions for the supplier or the sample, not a prohibition invented in code.
NAV_SIDE_FORCE_NOTE = ("ALPS product page, notes common to the series/models, "
                       "verbatim: 'Do not apply a force from the side of the stem'. "
                       "Applicability and the permitted force/method are UNRESOLVED "
                       "and must be clarified with ALPS or on a sample")
# The exact SKRHABE010 row points at DRAWING No.2 (the version with guide
# bosses), and that drawing's stem tip is NOT a plain Ø3 circle:
#   * the top view shows a rotated ROUNDED SQUARE at the tip, "□1.95 +-0.05" with
#     "R0.5" corners;
#   * the side view marks "Ø3".
# Which dimension controls the MATING surface (the square tip, the round stem
# above it, or the circumscribed circle of the tip) is NOT resolved here. A Ø3.2
# circular socket is therefore somewhere between a snug 0.10 mm per side fit and
# a very loose one, and no grip may be inferred from either.
NAV_STEM_TIP_SQUARE = 1.95
NAV_STEM_TIP_SQUARE_TOLERANCE = 0.05
NAV_STEM_TIP_CORNER_RADIUS = 0.5
NAV_STEM_SIDE_VIEW_DIAMETER = 3.0
# A sectioning of the locally downloaded ALPS `SKRHAB.STEP` (a TEMPORARY vendor
# file that is NOT committed and must not be redistributed) agrees with the
# drawing and settles the question: on an axis Z, the stem tip is at Z 5.0 above
# the mounting face, horizontal slices from Z 3.5 to Z 4.9 are a ROUNDED SQUARE
# of XY extent +-1.184 mm, and the Ø3 CIRCULAR section appears lower, at Z 2.5.
# These are MODEL geometry, not a measured or guaranteed tolerance.
NAV_STEP_TIP_EXTENT_MM = 1.184          # +-XY extent of the tip slices
NAV_STEP_SQUARE_SLICE_Z = (3.5, 4.9)    # above the mounting face
NAV_STEP_CIRCULAR_SECTION_Z = 2.5       # above the mounting face
NAV_STEP_PROVENANCE = ("vendor STEP sectioning, temporary local file, NOT committed "
                       "and NOT redistributable; model geometry only")

# Bourns PEC11R series datasheet (SHA-256 ac88f657...), PEC11R-4015F-S0024.
# PRIMARY SOURCE, read on page 2 (the `PEC11R-4xxxF-Sxxxx` drawing, bottom) and
# page 3 (the shaft table) of the manufacturer PDF:
#   * the nominal PC board to body/bushing SHOULDER distance is 6.5 mm;
#   * L = 15.0 mm is measured FROM THE SHOULDER to the shaft tip and it INCLUDES
#     the threaded bushing - it is not 15 mm of shaft beyond the bushing;
#   * for L = 15 the table gives LB = 5.0 mm of threaded bushing and F = 7.0 mm
#     (LB = 7 belongs to L = 20);
#   * the supplied mounting nut is 2.0 +-0.2 mm and the flat washer 0.5 mm - the
#     datasheet DOES dimension them.
# The earlier reading here (15 mm of shaft beyond a 7.0 mm thread) was wrong and
# is withdrawn; it made the bushing look 2.0 mm longer than it is.
ENC_BODY_TO_MOUNTING_SURFACE = 6.5
ENC_THREAD_LENGTH = 5.0                       # LB for L = 15 (page 3 table)
ENC_SHAFT_LENGTH = 15.0                       # L, from the SHOULDER to the tip
ENC_SHAFT_DIAMETER = 6.0
ENC_SHAFT_DIAMETER_TOLERANCE = 0.1            # page 2: D6 +-0.1
# ---- the shaft flat: SOURCED, and the earlier reading was WRONG ----
# An independent visual review of page 2 / page 3 established the arrows:
#   * `C 0.5` (page 3) points at the TIP CHAMFER. It is NOT the flat depth.
#   * `4.5 +0/-0.05` (page 2) spans the shaft's TOP SURFACE to the flat, on the
#     same D6 +-0.1 shaft. The flat therefore leaves (6.0 - 4.5) = 1.5 mm of
#     material, i.e. its plane is 1.50 mm from the AXIS, not 2.50 mm.
#     With D = 6.0 +-0.1 (R 3.0 +-0.05) and 4.5 +0/-0.05, the plane ranges
#     3.05 - 4.45 = 1.60 min-material ... 2.95 - 4.50 = 1.55 ... i.e. 1.40 to
#     1.55 mm from the axis; the modelled nominal is 1.50 and the tolerance is
#     carried as +-0.10 (the sum of the two contributions).
# The modelled 2.50 is withdrawn as an error, not kept as an equal alternative.
# The FIT is still unverified: nothing here says the knob slides on, clamps or
# actuates.
ENC_SHAFT_TIP_CHAMFER_C = 0.5                 # page 3 "C", the tip chamfer
ENC_FLAT_FROM_OPPOSITE_SURFACE = 4.5          # page 2, +0/-0.05
ENC_FLAT_ACROSS_TOLERANCE = 0.05
ENC_SHAFT_FLAT_DEPTH = round(ENC_SHAFT_DIAMETER - ENC_FLAT_FROM_OPPOSITE_SURFACE, 3)  # 1.5
ENC_FLAT_PLANE_MODEL = round(ENC_SHAFT_FLAT_DEPTH, 3)          # 1.50 from the axis
ENC_FLAT_PLANE_TOLERANCE = round(ENC_SHAFT_DIAMETER_TOLERANCE / 2
                                 + ENC_FLAT_ACROSS_TOLERANCE, 3)  # +-0.10
# Page 3, column F, runs from the BEGINNING of the flat to the shaft tip:
# F = 7.0 for L = 15 and F = 10.0 for L = 20. The flat therefore starts
# L - F from the shoulder: 8.0 mm for L15 (14.5 above the PCB) and 10.0 mm for
# L20 (16.5 above the PCB).
ENC_FLAT_LENGTH_F = 7.0                       # L = 15
ENC_FLAT_LENGTH_F_BY_SHAFT = {15.0: 7.0, 20.0: 10.0}
ENC_FLAT_START_ABOVE_PCB = round(ENC_BODY_TO_MOUNTING_SURFACE
                                 + (ENC_SHAFT_LENGTH - ENC_FLAT_LENGTH_F), 3)  # 14.5


def enc_flat_length(shaft_length: float) -> float:
    """Page 3 column F for a shaft length: 7.0 at L = 15, 10.0 at L = 20."""
    try:
        return ENC_FLAT_LENGTH_F_BY_SHAFT[float(shaft_length)]
    except KeyError:
        raise GeometryError(
            f"no page-3 F value is recorded for L = {shaft_length}: only L = 15 "
            "(F 7.0) and L = 20 (F 10.0) are sourced. Do not interpolate a "
            "dimension the datasheet prints as a table entry") from None
# Supplied hardware, dimensioned on page 3. The washer 0.5 mm carries no stated
# tolerance of its own; page 3 prints a general linear tolerance of +-0.3 mm for
# dimensions under 10 mm, so 0.5 must NOT be presented as an exactly-toleranced
# part and no retention margin may be claimed from it.
ENC_NUT_ACROSS_FLATS = 10.0
ENC_NUT_ACROSS_FLATS_TOLERANCE = 0.5
ENC_WASHER_OUTSIDE_DIAMETER = 12.0
ENC_WASHER_OUTSIDE_DIAMETER_TOLERANCE = 0.3
ENC_WASHER_BORE = 7.1
ENC_WASHER_BORE_TOLERANCE = 0.3
ENC_GENERAL_LINEAR_TOLERANCE = 0.3            # page 3, < 10 mm class
ENC_PUSH_TRAVEL = 0.5                         # centre-push travel
ENC_PUSH_TRAVEL_TOLERANCE = 0.3
# Supplied hardware, dimensioned by the datasheet: one mounting nut and one flat
# washer. The 2.5 mm nominal stack is derived, and its uncertainty follows the
# nut's +-0.2 mm tolerance.
ENC_NUT_THICKNESS = 2.0
ENC_NUT_THICKNESS_TOLERANCE = 0.2
ENC_WASHER_THICKNESS = 0.5
ENC_WASHER_THICKNESS_TOLERANCE = ENC_GENERAL_LINEAR_TOLERANCE
ENC_SUPPLIED_NUT_AND_WASHER = ("one mounting nut 2.0 +-0.2 mm high, 10.0 +-0.5 mm "
                               "across flats, and one flat washer 0.5 mm thick, "
                               "12.0 +-0.3 mm OD, 7.1 +-0.3 mm bore, all dimensioned "
                               "on page 3")
# Owner-visible design incompatibility at the 10.6 mm panel: only
# 6.5 + 5.0 = 11.5 mm of the encoder stands above the board, i.e. 0.9 mm past the
# panel FRONT face, while the supplied nut plus washer need 2.5 mm. The part as
# ordered cannot be nutted at this stack. NO encoder, PCB or panel change is made
# here: the remedy is the owner's decision.
ENC_CANDIDATE_LONGER_BUSHING = {
    "part": "Bourns PEC11R-4020F-S0024",
    "basis": "order-code table: L = 20 with LB = 7 of threaded bushing",
    "would_give": "6.5 + 7.0 = 13.5 mm above the board = 2.9 mm past the panel front, "
                  "i.e. only 0.4 mm NOMINAL beyond the 2.5 mm nut + washer. With the "
                  "page-2/page-3 general +-0.3 mm linear tolerance on BOTH the shoulder "
                  "and LB, plus the nut's +-0.2 mm and the washer's +-0.3 mm, the recorded "
                  "terms give 2.3 mm of thread against 3.0 mm of hardware: -0.7 mm. The "
                  "panel-thickness tolerance is unrecorded, so even that is not a "
                  "guaranteed bound",
    "lead_only_lcsc": "C143788 - https://www.lcsc.com/product-detail/C143788.html "
                      "(0 detents, 24 pulses). A temporary snapshot showed 39 units, "
                      "which is NOT a stock guarantee",
    "main_shop": "NONE VERIFIED: an exact Electrokit query returned no products, "
                 "digikey.se returned HTTP 403 and a Conrad manual search was not "
                 "completed. No ELECTROKIT/CONRAD/DIGIKEY article number is verified, "
                 "so the component-library gate for a preferred or alternate part "
                 "cannot pass today",
    "status": "CANDIDATE ONLY, NOT SELECTED and NOT in the BOM: the ordering code, "
              "stock, detent option and availability are unverified, no main-shop SKU "
              "exists, and the owner has not approved a substitution",
}


def encoder_stack(panel_front_above_pcb: float,
                  knob_rear_above_panel_front: float | None = None,
                  shaft_length: float = ENC_SHAFT_LENGTH,
                  thread_length: float = ENC_THREAD_LENGTH,
                  flat_length: float | None = None,
                  bore_flat_plane: float | None = None) -> dict:
    """The whole PEC11R stack, PARAMETERISED, in "above the PCB" coordinates.

    Every length comes from the page 2 / page 3 datum: the body/bushing SHOULDER
    is 6.5 mm above the PCB, and both the bushing length LB and the shaft length L
    are measured from that shoulder. Nothing here is a fit verdict: the function
    returns nominal AND worst-case numbers, so a caller has to say which one it
    is quoting.

    Two candidate encoders and two flat readings can be evaluated by passing
    ``shaft_length``/``thread_length`` and ``bore_flat_plane``: the L = 15 /
    LB = 5 part that is in the BOM, and the unapproved L = 20 / LB = 7 one.
    """
    if flat_length is None:
        flat_length = enc_flat_length(shaft_length)
    step = ENC_BODY_TO_MOUNTING_SURFACE
    shoulder = step                                          # 6.5 above the PCB
    bushing_tip = round(shoulder + thread_length, 3)
    shaft_tip = round(shoulder + shaft_length, 3)
    flat_start = round(shoulder + (shaft_length - flat_length), 3)   # 14.5 for L15/F7
    nut_stack = round(ENC_NUT_THICKNESS + ENC_WASHER_THICKNESS, 3)
    nut_stack_worst = round(ENC_NUT_THICKNESS + ENC_NUT_THICKNESS_TOLERANCE
                            + ENC_WASHER_THICKNESS + ENC_WASHER_THICKNESS_TOLERANCE, 3)
    # A WORST CASE must carry every term that can move, not only the nut: the
    # shoulder (6.5) and the bushing length LB are dimensions like any other, so
    # each takes the page-3 general +-0.3 mm linear tolerance. The panel thickness
    # is a separate, unrecorded tolerance and is NOT included - that is why these
    # numbers are labelled "worst case of the RECORDED terms".
    gen = ENC_GENERAL_LINEAR_TOLERANCE
    thread_worst = round((shoulder - gen) + (thread_length - gen)
                         - panel_front_above_pcb, 3)
    out = {
        "shaft_length_L_mm": shaft_length,
        "thread_length_LB_mm": thread_length,
        "flat_length_F_mm": flat_length,
        "shoulder_above_pcb_mm": round(shoulder, 3),
        "bushing_tip_above_pcb_mm": bushing_tip,
        "shaft_tip_above_pcb_mm": shaft_tip,
        "flat_start_above_pcb_mm": flat_start,
        "panel_front_above_pcb_mm": round(panel_front_above_pcb, 3),
        "thread_past_panel_front_mm": round(bushing_tip - panel_front_above_pcb, 3),
        "nut_and_washer_nominal_mm": nut_stack,
        "nut_and_washer_worst_mm": nut_stack_worst,
        "spun_down_spare_nominal_mm": round(bushing_tip - panel_front_above_pcb
                                            - nut_stack, 3),
        # Of the RECORDED terms only: shoulder, LB, nut and washer at their
        # tolerances. The panel thickness tolerance is not recorded and is
        # therefore excluded - so this is an honest bound of what IS known, not a
        # guaranteed clearance.
        "threshold_terms_worst_mm": {
            "shoulder_mm": round(shoulder - gen, 3),
            "thread_length_LB_mm": round(thread_length - gen, 3),
            "panel_front_mm": round(panel_front_above_pcb, 3),
            "nut_mm": round(ENC_NUT_THICKNESS + ENC_NUT_THICKNESS_TOLERANCE, 3),
            "washer_mm": round(ENC_WASHER_THICKNESS + ENC_WASHER_THICKNESS_TOLERANCE, 3),
            "panel_thickness_tolerance": "NOT recorded",
        },
        "thread_past_panel_front_worst_mm": thread_worst,
        "spun_down_spare_worst_mm": round(thread_worst - nut_stack_worst, 3),
        "push_travel_mm": ENC_PUSH_TRAVEL,
        "push_travel_tolerance_mm": ENC_PUSH_TRAVEL_TOLERANCE,
        "general_linear_tolerance_mm": ENC_GENERAL_LINEAR_TOLERANCE,
    }
    if knob_rear_above_panel_front is not None:
        # The nut and washer sit ON the panel front face, so their stack and the
        # knob's rear face are compared above THAT datum, not above the PCB.
        knob_rear_above_pcb = round(panel_front_above_pcb + knob_rear_above_panel_front, 3)
        clearance = round(knob_rear_above_panel_front - nut_stack, 3)
        clearance_worst = round(knob_rear_above_panel_front - nut_stack_worst, 3)
        out.update({
            "knob_rear_above_panel_front_mm": round(knob_rear_above_panel_front, 3),
            "knob_rear_above_pcb_mm": knob_rear_above_pcb,
            "knob_clearance_nominal_mm": clearance,
            "knob_clearance_worst_mm": clearance_worst,
            # What is left of the declared centre travel before the knob face
            # meets the hardware. Negative or small means the full travel cannot
            # be guaranteed - it never means the switch will not actuate.
            "push_clearance_nominal_mm": clearance,
            "push_shortfall_nominal_mm": round(ENC_PUSH_TRAVEL - clearance, 3),
            "push_shortfall_worst_mm": round(ENC_PUSH_TRAVEL - clearance_worst, 3),
            # The shaft is ROUND below the flat. That round part must pass the
            # D-shaped section of the bore before the flat can engage.
            "round_shaft_into_d_bore_mm": round(max(0.0, flat_start - knob_rear_above_pcb), 3),
        })
    if bore_flat_plane is not None:
        out.update({
            "bore_flat_plane_mm": bore_flat_plane,
            "shaft_round_radius_mm": ENC_SHAFT_DIAMETER / 2,
            "d_bore_blocks_round_shaft": ENC_SHAFT_DIAMETER / 2 > bore_flat_plane,
            "d_bore_round_interference_mm": round(
                max(0.0, ENC_SHAFT_DIAMETER / 2 - bore_flat_plane), 3),
        })
    return out

# Omron B3F datasheet (SHA-256 be9cf69e...), B3F-4055.
# 12 +-0.2 mm square body, 3.8 +-0.1 mm projected plunger, 7.3 +-0.2 mm from the
# board to the plunger face. The same numbers are carried in lib/layout.ts
# BUTTON_SWITCH; button_cap() refuses to build when the two disagree.
B3F_HEIGHT_ABOVE_BOARD = 7.3
B3F_HEIGHT_TOLERANCE = 0.2
B3F_BODY_SQUARE = 12.0
B3F_BODY_SQUARE_TOLERANCE = 0.2
B3F_PLUNGER_SQUARE = 3.8
B3F_PLUNGER_SQUARE_TOLERANCE = 0.1
# Page 3 "Operating Characteristics", the 12 x 12 mm models row B3F-4@@5:
# pretravel PT = 0.3 +0.2/-0.1 mm, measured TO THE OPERATING POINT. Overtravel
# and total travel are NOT published on that page, so this number says how far the
# plunger must move to switch, never how much room the cap has beyond it.
B3F_PRETRAVEL = 0.3
B3F_PRETRAVEL_PLUS = 0.2
B3F_PRETRAVEL_MINUS = 0.1
# Page 6 also dimensions the FIXED BODY height: 4.3 +-0.2 mm. So the body top is
# known (4.3 above the board) and the printed cap's locator ring clearance to it
# can be computed NOMINALLY. The MOVING COLLAR around the plunger and the
# overtravel remain unmeasured.
B3F_BODY_HEIGHT = 4.3
B3F_BODY_HEIGHT_TOLERANCE = 0.2


# ---------------------------------------------------------------------------
# ASSUMPTIONS: printed/assembly tolerances, never a fit-critical free pick
# ---------------------------------------------------------------------------
@dataclass(frozen=True)
class Assumption:
    name: str
    value: float | str
    note: str


# NAV keytop. The aperture, stem and heights come from the sources above; each
# number below is a print or assembly tolerance that the first article has to
# confirm (README.md, "first article"). The printed SHAPE (flange, shank, socket
# bore, head height, socket depth) is not declared here: lib/layout.ts owns it in
# PRINTED_PARTS and this module reads it from the export.
NAV_APERTURE_FIT_TOTAL = 0.20          # 0.10 mm per side, printed sliding fit in the panel hole
NAV_AXIAL_FREE_PLAY = 0.15             # 0.10-0.20 mm band; the cap must not pre-load the switch
NAV_HEAD_EDGE_CHAMFER = 0.80           # print + finger friendly
NAV_FLANGE_REAR_CHAMFER = 0.60         # 45 deg, removes the flange overhang
NAV_SOCKET_MOUTH_CHAMFER = 0.40        # lead-in over the stem
NAV_ASSEMBLY_CLEARANCE_MIN = 0.50      # cap bottom to the switch body top

# Value knob.
# SOURCED, not assumed: page 3 dimensions the supplied nut (2.0 +-0.2) and the
# flat washer (0.5). What remains an assumption is the knob's rear standoff
# (2.70 = this stack + 0.20 mm of air).
KNOB_NUT_AND_WASHER_STACK = round(ENC_NUT_THICKNESS + ENC_WASHER_THICKNESS, 3)  # 2.50
KNOB_REAR_AIR = 0.20                   # air between the nut and the knob rear face
KNOB_BORE_MOUTH_CHAMFER = 0.50         # lead-in onto the shaft
KNOB_EDGE_CHAMFER_FRONT = 0.80
KNOB_EDGE_CHAMFER_REAR = 0.40
KNOB_FLAT_ANGLE_DEG = 0.0              # the shaft's flat faces +X (board right)
KNOB_FLUTE_COUNT = 16                  # grip; 22.5 deg pitch keeps lands on both axes
KNOB_FLUTE_RADIUS = 1.60
KNOB_FLUTE_DEPTH = 0.45
KNOB_GRUB_SCREW = "M3 x 4 set screw, flat point"
KNOB_GRUB_SCREW_LENGTH = 4.00          # M3 x 4: 3.65 mm of it sits in the tapped wall
KNOB_GRUB_PILOT_DIAMETER = 2.50        # M3 x 0.5 tap drill
KNOB_GRUB_TIP_FROM_REAR = 6.00         # screw axis distance from the knob rear face
KNOB_TO_DISPLAY_MIN = 2.50             # the layout's own knob-body clearance rule

ASSUMPTIONS: tuple[Assumption, ...] = (
    Assumption("NAV_APERTURE_FIT_TOTAL", NAV_APERTURE_FIT_TOTAL,
               "0.10 mm per side sliding fit in the 7.2 mm panel aperture; "
               "measure the printed neck on the first article"),
    Assumption("NAV_AXIAL_FREE_PLAY", NAV_AXIAL_FREE_PLAY,
               "0.10-0.20 mm band: the socket floor must not touch the stem top "
               "at rest, or the switch is pre-loaded"),
    Assumption("NAV_HEAD_EDGE_CHAMFER", NAV_HEAD_EDGE_CHAMFER, "print + finger edge break"),
    Assumption("NAV_FLANGE_REAR_CHAMFER", NAV_FLANGE_REAR_CHAMFER,
               "45 deg: the flange rear face is the part's only overhang"),
    Assumption("NAV_SOCKET_MOUTH_CHAMFER", NAV_SOCKET_MOUTH_CHAMFER,
               "lead-in so the socket finds the D3.0 stem at assembly"),
    Assumption("PRINT_MIN_WALL", PRINT_MIN_WALL,
               "0.4 mm nozzle, two perimeters; every wall is checked against it"),
    Assumption("KNOB_NUT_AND_WASHER_STACK", KNOB_NUT_AND_WASHER_STACK,
               "SOURCED on datasheet page 3: mounting nut 2.0 +-0.2 mm plus flat "
               "washer 0.5 mm. The tolerance follows the nut, so the stack is "
               "2.5 +-0.2 - and it is this stack that the 0.9 mm of exposed "
               "bushing thread at the 10.6 mm panel cannot take (see validate_knob)"),
    Assumption("KNOB_REAR_AIR", KNOB_REAR_AIR, "air gap between nut and knob"),
    Assumption("KNOB_BORE_MOUTH_CHAMFER", KNOB_BORE_MOUTH_CHAMFER, "lead-in onto the shaft"),
    Assumption("KNOB_EDGE_CHAMFER_FRONT", KNOB_EDGE_CHAMFER_FRONT, "print + finger edge break"),
    Assumption("KNOB_EDGE_CHAMFER_REAR", KNOB_EDGE_CHAMFER_REAR, "print edge break"),
    Assumption("KNOB_FLAT_ANGLE_DEG", KNOB_FLAT_ANGLE_DEG,
               "UNVERIFIED: the encoder's clocking on the front board is not in "
               "the sources, so the shaft flat is assumed to face +X. If the first "
               "article shows another clocking, change this one constant and "
               "regenerate - the bore flat and the grub screw follow it together"),
    Assumption("KNOB_FLUTE_COUNT", KNOB_FLUTE_COUNT,
               "grip feature; 22.5 deg pitch keeps a full-radius land on +X, +Y, "
               "-X and -Y so the printed OD is the nominal 12.5 mm"),
    Assumption("KNOB_FLUTE_RADIUS", KNOB_FLUTE_RADIUS, "finger notch radius"),
    Assumption("KNOB_FLUTE_DEPTH", KNOB_FLUTE_DEPTH, "finger notch depth"),
    Assumption("KNOB_GRUB_SCREW", KNOB_GRUB_SCREW,
               "retention: a press fit alone is not acceptable on a metal shaft"),
    Assumption("KNOB_GRUB_PILOT_DIAMETER", KNOB_GRUB_PILOT_DIAMETER,
               "M3 x 0.5 tap drill; the knob is tapped after printing"),
    Assumption("KNOB_GRUB_TIP_FROM_REAR", KNOB_GRUB_TIP_FROM_REAR,
               "screw axis 6.0 mm from the knob rear face, on the flat's normal"),
    Assumption("KNOB_TO_DISPLAY_MIN", KNOB_TO_DISPLAY_MIN,
               "the layout's own rule: 2.50 mm from the knob body to the "
               "front-board display opening edge"),
)


# ---------------------------------------------------------------------------
# Canonical data
# ---------------------------------------------------------------------------
def load_layout(path: Path | str = LAYOUT_JSON) -> dict:
    """Read ``mechanical/layout.json``. Fail loudly, never invent a default."""
    p = Path(path)
    if not p.is_file():
        raise GeometryError(
            f"{p} is missing. Generate it with: "
            "cd tscircuit && bun run scripts/export-mechanical.ts")
    return json.loads(p.read_text())


def read_layout_ts(path: Path | str = LAYOUT_TS) -> str:
    """Read the canonical ``lib/layout.ts`` source text."""
    p = Path(path)
    if not p.is_file():
        raise GeometryError(f"{p} is missing: the display opening and the knob "
                            "column are defined there and cannot be assumed")
    return p.read_text()


def printed_part(layout: dict, key: str) -> dict:
    """One printed part's envelope from the export. Never invent a dimension."""
    block = layout.get("printedParts")
    if not isinstance(block, dict):
        raise GeometryError(
            "mechanical/layout.json has no 'printedParts' block: lib/layout.ts "
            "PRINTED_PARTS is the single source of both printed parts' shape and "
            "position. Regenerate with: cd tscircuit && "
            "bun run scripts/export-mechanical.ts")
    part = block.get(key)
    if not isinstance(part, dict):
        raise GeometryError(
            f"mechanical/layout.json has no printedParts.{key}; the printed parts "
            "read their envelope from PRINTED_PARTS in lib/layout.ts")
    return part


def part_z(part: dict, key: str) -> float:
    """One Z plane of a printed part, panel front = 0 and +Z rearward (mm)."""
    if key not in part:
        raise GeometryError(f"printed part {part.get('part')!r} has no {key} Z plane")
    return float(part[key])


# ---------------------------------------------------------------------------
# Panel stack
# ---------------------------------------------------------------------------
@dataclass(frozen=True)
class PanelStack:
    """The three Z planes the parts live between (mm, +Z rearward)."""

    panel_front_z: float
    panel_rear_z: float
    board_top_z: float

    @property
    def thickness(self) -> float:
        return self.panel_rear_z - self.panel_front_z

    @property
    def gap(self) -> float:
        """Clear height between the panel rear face and the front board."""
        return self.board_top_z - self.panel_rear_z


def panel_stack(layout: dict | None = None) -> PanelStack:
    data = layout if layout is not None else load_layout()
    z = data["provisionalZ"]
    stack = PanelStack(float(z["panelFront"]), float(z["panelRear"]),
                       float(z["frontBoardTop"]))
    need(stack.thickness > 0.0, "the panel has no thickness in layout.json")
    need(abs(stack.thickness - float(data["panel"]["thickness"])) <= EPS_MM,
         f"panel thickness {data['panel']['thickness']} disagrees with the "
         f"provisionalZ panel faces ({stack.thickness:.3f})")
    need(stack.gap > 0.0,
         f"the panel rear face {stack.panel_rear_z} is not in front of the board "
         f"top {stack.board_top_z}")
    return stack


@dataclass(frozen=True)
class Circle:
    """A round panel aperture or hole in panel coordinates (mm)."""

    x: float
    y: float
    diameter: float
    name: str = ""

    @property
    def radius(self) -> float:
        return self.diameter / 2


@dataclass(frozen=True)
class Rect:
    """A rectangular panel aperture in panel coordinates (mm)."""

    x: float
    y: float
    width: float
    height: float
    name: str = ""


def aperture(layout: dict, name: str) -> Circle:
    """One panel aperture by name. Never invent the diameter."""
    for entry in layout["panelApertures"]:
        if entry.get("name") != name:
            continue
        if entry.get("shape") != "circle":
            raise GeometryError(f"panel aperture {name} is not a circle: {entry.get('shape')}")
        return Circle(float(entry["x"]), float(entry["y"]),
                      2.0 * float(entry["radius"]), name)
    raise GeometryError(f"panel aperture {name} is not in layout.json")


def panel_feature_clearances(layout: dict, centre: tuple[float, float],
                             radius: float, skip: str) -> list[tuple[str, float]]:
    """Material-free distance from a round outline to every other panel feature.

    Polygons (only the LCD aperture, which follows the module glass) are left to
    the display-opening rule; everything round or rectangular is measured.
    """
    cx, cy = centre
    out: list[tuple[str, float]] = []
    for entry in layout["panelApertures"]:
        if entry.get("name") == skip:
            continue
        shape = entry.get("shape")
        if shape == "circle":
            d = math.hypot(cx - entry["x"], cy - entry["y"]) - radius - entry["radius"]
        elif shape == "rect":
            dx = max(abs(cx - entry["x"]) - entry["width"] / 2, 0.0)
            dy = max(abs(cy - entry["y"]) - entry["height"] / 2, 0.0)
            d = math.hypot(dx, dy) - radius
        else:
            continue
        out.append((entry["name"], d))
    for hole in layout["panelMountHoles"]:
        d = math.hypot(cx - hole["x"], cy - hole["y"]) - radius - hole["diameter"] / 2
        out.append((hole["name"], d))
    # The front board's M2.5 standoff holes, which the panel repeats.
    hole_d = float(layout["mountHoleDiameter"]) / 2
    for i, hole in enumerate(layout["frontBoardHoles"]):
        d = math.hypot(cx - hole["x"], cy - hole["y"]) - radius - hole_d
        out.append((f"front board hole {i}", d))
    return sorted(out, key=lambda item: item[1])


# ---------------------------------------------------------------------------
# lib/layout.ts source parse (display opening, knob column, printed B3F cap)
# ---------------------------------------------------------------------------
@dataclass(frozen=True)
class ButtonCapLayout:
    """The printed B3F button-cap block of ``lib/layout.ts``.

    Inputs and derived source values are kept together so ``button_cap()`` can
    re-derive the whole Z chain from the panel stack and the datasheet and
    refuse any disagreement. ``implemented`` is the design-status flag: the
    generator refuses to build while it is false.
    """

    implemented: bool
    cutout_square: float
    square: float
    locator_square: float
    locator_depth: float
    proud_above_panel_front: float
    underside_z: float
    locator_floor_z: float
    rear_face_z: float
    top_z: float
    height: float
    clearance_to_cutout_per_side: float
    locator_clearance_per_side: float
    cap_extension_above_switch: float
    switch_height_above_board: float
    switch_height_tolerance: float
    switch_body_square: float
    switch_body_square_tolerance: float
    switch_plunger_square: float
    switch_plunger_square_tolerance: float


@dataclass(frozen=True)
class LayoutSource:
    """The values that only ``lib/layout.ts`` carries, with their cross-checks."""

    display_centre_x: float
    display_outline_width: float
    display_opening_clearance_x: float
    display_opening_width: float
    display_opening_right: float
    front_board_opening_right: float
    knob_column_x: float
    knob_body_diameter: float
    knob_body_diameter_column_x: float
    panel_control_aperture: float
    button_cap: ButtonCapLayout


def _ts_block(source: str, header: str) -> str:
    start = source.find(header)
    if start < 0:
        raise GeometryError(
            f"{header!r} is not in lib/layout.ts any more. The printed parts read "
            "the display opening and the knob column from there; re-derive them "
            "from the new source instead of hardcoding a value.")
    end = source.find("} as const", start)
    if end < 0:
        raise GeometryError(f"{header!r} has no closing '}} as const' in lib/layout.ts")
    return source[start:end]


def _ts_number(text: str, pattern: str, what: str) -> float:
    match = re.search(pattern, text, re.MULTILINE)
    if match is None:
        raise GeometryError(
            f"cannot parse {what} from lib/layout.ts with {pattern!r}; the source "
            "changed shape, so re-derive the printed-part inputs instead of "
            "falling back to a stale number")
    return float(match.group(1))


def _ts_flag(text: str, pattern: str, what: str) -> bool:
    match = re.search(pattern, text, re.MULTILINE)
    if match is None:
        raise GeometryError(
            f"cannot parse {what} from lib/layout.ts with {pattern!r}; the source "
            "changed shape, so re-derive the printed-part inputs instead of "
            "falling back to a stale value")
    return match.group(1) == "true"


def _ts_const_number(text: str, name: str) -> float:
    """One named numeric top-level ``const`` in ``lib/layout.ts``."""
    match = re.search(rf"^const\s+{re.escape(name)}\s*=\s*(-?\d+(?:\.\d+)?)",
                      text, re.MULTILINE)
    if match is None:
        raise GeometryError(
            f"cannot parse const {name} from lib/layout.ts; the printed B3F cap "
            "reads its clearances from that named constant, so re-derive it "
            "instead of falling back to a stale number")
    return float(match.group(1))


def parse_layout_ts(source: str) -> LayoutSource:
    """Parse and cross-check every layout.ts value the printed parts need.

    Each value is derived from the source expression AND compared with the
    number the source itself prints in its trailing comment. A silent edit to
    either side fails here rather than producing a plausible-but-wrong part.
    """
    concept = _ts_block(source, "export const DISPLAY_CONCEPT = {")
    centre_x = _ts_number(concept, r"^\s*x:\s*(-?\d+(?:\.\d+)?),$", "DISPLAY_CONCEPT.x")

    vendor = _ts_block(source, "export const DISPLAY_VENDOR = {")
    outline_w = _ts_number(vendor, r"outline:\s*\{\s*width:\s*(\d+(?:\.\d+)?)",
                           "DISPLAY_VENDOR.outline.width")

    clearance = _ts_block(source, "export const DISPLAY_OPENING_CLEARANCE = {")
    per_side_x = _ts_number(clearance, r"perSideX:\s*(\d+(?:\.\d+)?)",
                            "DISPLAY_OPENING_CLEARANCE.perSideX")

    opening = _ts_block(source, "export const FRONT_DISPLAY_OPENING = {")
    opening_w_documented = _ts_number(
        opening, r"width:\s*mm3\([^)]*\),\s*//\s*(\d+(?:\.\d+)?)",
        "FRONT_DISPLAY_OPENING.width comment")
    opening_w = round(outline_w + 2 * per_side_x, 3)
    need(abs(opening_w - opening_w_documented) <= 1e-3,
         f"FRONT_DISPLAY_OPENING.width: the expression gives {opening_w:.3f} mm but "
         f"the source comment documents {opening_w_documented:.3f} mm")

    board_opening = _ts_block(source, "export const FRONT_BOARD_OPENING = {")
    right_documented = _ts_number(
        board_opening, r"right:\s*mm3\([^)]*\),\s*//\s*(\d+(?:\.\d+)?)",
        "FRONT_BOARD_OPENING.right comment")
    right = round(centre_x + opening_w / 2, 3)
    need(abs(right - right_documented) <= 1e-3,
         f"FRONT_BOARD_OPENING.right: DISPLAY_CONCEPT.x + width/2 gives {right:.3f} mm "
         f"but the source comment documents {right_documented:.3f} mm")

    column = _ts_block(source, "export const RIGHT_COLUMN = {")
    knob_x = _ts_number(column, r"x:\s*(\d+(?:\.\d+)?)", "RIGHT_COLUMN.x")

    match = re.search(r"knob body \((?:\u00d8|D)(\d+(?:\.\d+)?) at x = (\d+(?:\.\d+)?)[,)]",
                      source)
    if match is None:
        raise GeometryError(
            "the owner-approved 'knob body (D12.5 at x = 52)' note is gone from "
            "lib/layout.ts; the printed knob's diameter is derived from it and "
            "must not be guessed")
    knob_od, knob_od_x = float(match.group(1)), float(match.group(2))
    need(abs(knob_od_x - knob_x) <= EPS_MM,
         f"the knob-body note places the column at x {knob_od_x} but RIGHT_COLUMN.x "
         f"is {knob_x}")

    cutouts = _ts_block(source, "export const PANEL_CUTOUTS = {")
    control = _ts_number(cutouts, r"^\s*control:\s*(\d+(?:\.\d+)?),", "PANEL_CUTOUTS.control")
    button_cutout = _ts_number(
        cutouts, r"^\s*button:\s*[^\n]*?,\s*//\s*(\d+(?:\.\d+)?)",
        "PANEL_CUTOUTS.button comment")

    # ---- Printed B3F button cap ----
    # The part is one geometry printed seven times, so its shape is declared
    # once in BUTTON_CAP_PRINTED rather than in PRINTED_PARTS (which carries a
    # single x/y position per part). button_cap() re-derives every plane from
    # the panel stack and the B3F datasheet and fails when the source comments
    # disagree, so a typo cannot export a cap of the wrong height.
    cap = _ts_block(source, "export const BUTTON_CAP_PRINTED = {")
    reach = _ts_block(source, "export const BUTTON_REACH = {")
    switch = _ts_block(source, "export const BUTTON_SWITCH = {")
    button_cap = ButtonCapLayout(
        implemented=_ts_flag(cap, r"^\s*implemented:\s*(true|false),",
                             "BUTTON_CAP_PRINTED.implemented"),
        cutout_square=button_cutout,
        square=_ts_number(cap, r"^\s*square:\s*[^\n]*?,\s*//\s*(\d+(?:\.\d+)?)",
                          "BUTTON_CAP_PRINTED.square comment"),
        locator_square=_ts_number(
            cap, r"^\s*locatorSquare:\s*[^\n]*?,\s*//\s*(\d+(?:\.\d+)?)",
            "BUTTON_CAP_PRINTED.locatorSquare comment"),
        locator_depth=_ts_const_number(source, "BUTTON_CAP_LOCATOR_DEPTH"),
        proud_above_panel_front=_ts_const_number(
            source, "BUTTON_CAP_PROUD_ABOVE_PANEL_FRONT"),
        underside_z=_ts_number(
            cap, r"undersideZ:\s*BUTTON_SWITCH_TOP_Z,\s*//\s*(\d+(?:\.\d+)?) panel z",
            "BUTTON_CAP_PRINTED.undersideZ comment"),
        locator_floor_z=_ts_number(
            cap, r"locatorFloorZ:\s*BUTTON_SWITCH_TOP_Z,\s*//\s*(\d+(?:\.\d+)?)",
            "BUTTON_CAP_PRINTED.locatorFloorZ comment"),
        rear_face_z=_ts_number(
            cap, r"rearFaceZ:\s*[^\n]*?,\s*//\s*(\d+(?:\.\d+)?)",
            "BUTTON_CAP_PRINTED.rearFaceZ comment"),
        top_z=_ts_number(
            cap, r"topZ:\s*[^\n]*?,\s*//\s*(-?\d+(?:\.\d+)?) panel z",
            "BUTTON_CAP_PRINTED.topZ comment"),
        height=_ts_number(cap, r"^\s*height:\s*[^\n]*?,\s*//\s*(\d+(?:\.\d+)?)",
                          "BUTTON_CAP_PRINTED.height comment"),
        clearance_to_cutout_per_side=_ts_const_number(
            source, "BUTTON_CAP_CLEARANCE_TO_CUTOUT_PER_SIDE"),
        locator_clearance_per_side=_ts_const_number(
            source, "BUTTON_CAP_LOCATOR_CLEARANCE_PER_SIDE"),
        cap_extension_above_switch=_ts_number(
            reach, r"capExtensionAboveSwitch:\s*[^\n]*?,\s*//\s*(\d+(?:\.\d+)?)",
            "BUTTON_REACH.capExtensionAboveSwitch comment"),
        switch_height_above_board=_ts_number(
            switch, r"^\s*heightAboveBoard:\s*(\d+(?:\.\d+)?),",
            "BUTTON_SWITCH.heightAboveBoard"),
        switch_height_tolerance=_ts_number(
            switch, r"^\s*heightTolerance:\s*(\d+(?:\.\d+)?),",
            "BUTTON_SWITCH.heightTolerance"),
        switch_body_square=_ts_number(
            switch, r"^\s*bodySquare:\s*(\d+(?:\.\d+)?),", "BUTTON_SWITCH.bodySquare"),
        switch_body_square_tolerance=_ts_number(
            switch, r"^\s*bodySquareTolerance:\s*(\d+(?:\.\d+)?),",
            "BUTTON_SWITCH.bodySquareTolerance"),
        switch_plunger_square=_ts_number(
            switch, r"^\s*plungerSquare:\s*(\d+(?:\.\d+)?),",
            "BUTTON_SWITCH.plungerSquare"),
        switch_plunger_square_tolerance=_ts_number(
            switch, r"^\s*plungerSquareTolerance:\s*(\d+(?:\.\d+)?),",
            "BUTTON_SWITCH.plungerSquareTolerance"),
    )

    # The block's own arithmetic: plate = cutout - 2 clearance, locator = plunger
    # + 2 clearance, rear = floor + depth, height = rear - top. A comment edited
    # without its expression (or the reverse) fails here, not in the STL.
    need(abs((button_cap.cutout_square - button_cap.square) / 2
             - button_cap.clearance_to_cutout_per_side) <= 1e-3,
         "BUTTON_CAP_PRINTED.clearanceToCutoutPerSide: (cutout - square)/2 gives "
         f"{(button_cap.cutout_square - button_cap.square) / 2:.3f} mm but the source "
         f"declares {button_cap.clearance_to_cutout_per_side:.3f} mm")
    need(abs((button_cap.locator_square - button_cap.switch_plunger_square) / 2
             - button_cap.locator_clearance_per_side) <= 1e-3,
         "BUTTON_CAP_PRINTED.locatorClearancePerSide: (locator - plunger)/2 gives "
         f"{(button_cap.locator_square - button_cap.switch_plunger_square) / 2:.3f} mm "
         f"but the source declares {button_cap.locator_clearance_per_side:.3f} mm")
    need(abs((button_cap.underside_z + button_cap.locator_depth)
             - button_cap.rear_face_z) <= 1e-3,
         "BUTTON_CAP_PRINTED.rearFaceZ: undersideZ + locatorDepth gives "
         f"{button_cap.underside_z + button_cap.locator_depth:.3f} mm but the source "
         f"declares {button_cap.rear_face_z:.3f} mm")
    need(abs((button_cap.rear_face_z - button_cap.top_z) - button_cap.height) <= 1e-3,
         "BUTTON_CAP_PRINTED.height: rearFaceZ - topZ gives "
         f"{button_cap.rear_face_z - button_cap.top_z:.3f} mm but the source declares "
         f"{button_cap.height:.3f} mm")

    return LayoutSource(
        display_centre_x=centre_x,
        display_outline_width=outline_w,
        display_opening_clearance_x=per_side_x,
        display_opening_width=opening_w,
        display_opening_right=right,
        front_board_opening_right=right_documented,
        knob_column_x=knob_x,
        knob_body_diameter=knob_od,
        knob_body_diameter_column_x=knob_od_x,
        panel_control_aperture=control,
        button_cap=button_cap,
    )


def layout_source(source: str | None = None) -> LayoutSource:
    return parse_layout_ts(read_layout_ts() if source is None else source)


def layout_export_notices(layout: dict, src: LayoutSource) -> list[str]:
    """Report where ``mechanical/layout.json`` is stale against ``lib/layout.ts``.

    The stale values are reported, never used for a fit rule: the display
    clearance is computed from ``lib/layout.ts``.
    """
    out: list[str] = []
    display = layout["display"]
    if (abs(float(display["x"]) - src.display_centre_x) > EPS_MM
            or abs(float(display["width"]) - src.display_outline_width) > EPS_MM):
        out.append(
            f"STALE EXPORT: mechanical/layout.json has display x {display['x']} "
            f"width {display['width']}, lib/layout.ts has x {src.display_centre_x} "
            f"width {src.display_outline_width}. The knob display-clearance rule "
            "uses lib/layout.ts; the export value is reported for information only "
            "(regenerate with: cd tscircuit && bun run scripts/export-mechanical.ts)")
    return out


# ---------------------------------------------------------------------------
# Part 1: NAV keytop for the ALPS Alpine SKRHABE010
# ---------------------------------------------------------------------------
@dataclass(frozen=True)
class NavKeytop:
    """NAV keytop inputs. Derived positions are properties, never stored twice.

    The part is a pin: the head (in front of the panel) and the neck (in the
    aperture) are one D7.00 diameter, because the head is the LEADING feature
    when the cap is fitted through the aperture from the rear. A flange
    (D9.60) then traps it on the panel rear face. Anything wider than the
    aperture in front of the flange could never be assembled through a
    one-piece panel.
    """

    stack: PanelStack
    aperture: Circle
    stem_diameter: float
    stem_top_z: float
    switch_body_top_z: float
    fitting_area_max: float
    head_height: float
    neck_diameter: float
    flange_diameter: float
    flange_thickness: float
    shank_diameter: float
    socket_bore: float
    socket_floor_z: float
    socket_mouth_z: float
    head_edge_chamfer: float
    flange_rear_chamfer: float
    mouth_chamfer: float

    @property
    def centre(self) -> tuple[float, float]:
        return (self.aperture.x, self.aperture.y)

    @property
    def aperture_diameter(self) -> float:
        return self.aperture.diameter

    @property
    def aperture_clearance(self) -> float:
        """Diametral clearance of the neck in the aperture (mm)."""
        return self.aperture_diameter - self.neck_diameter

    @property
    def head_front_z(self) -> float:
        return self.stack.panel_front_z - self.head_height

    @property
    def neck_length(self) -> float:
        return self.stack.thickness

    @property
    def flange_front_z(self) -> float:
        """The flange face that bears on the panel REAR face."""
        return self.stack.panel_rear_z

    @property
    def flange_rear_z(self) -> float:
        return self.flange_front_z + self.flange_thickness

    @property
    def shank_length(self) -> float:
        return self.socket_mouth_z - self.flange_rear_z

    @property
    def socket_depth(self) -> float:
        return self.socket_mouth_z - self.socket_floor_z

    @property
    def free_play(self) -> float:
        """Axial gap between the socket floor and the stem top at rest (mm)."""
        return self.stem_top_z - self.socket_floor_z

    @property
    def stem_engagement(self) -> float:
        """Length of stem inside the socket when the flange is seated (mm)."""
        return self.socket_mouth_z - self.stem_top_z

    @property
    def bearing_ring(self) -> float:
        """Radial width of the flange's bearing face outside the aperture (mm)."""
        return (self.flange_diameter - self.aperture_diameter) / 2

    @property
    def socket_wall(self) -> float:
        return (self.shank_diameter - self.socket_bore) / 2

    @property
    def switch_body_clearance(self) -> float:
        """Socket mouth to the switch body top: the cap must not seat on it."""
        return self.switch_body_top_z - self.socket_mouth_z

    @property
    def gap_below(self) -> float:
        return self.stack.board_top_z - self.socket_mouth_z

    @property
    def overall_length(self) -> float:
        return self.socket_mouth_z - self.head_front_z

    @property
    def envelope(self) -> "Envelope":
        """Intended solid envelope in panel coordinates (mm)."""
        r_flange = self.flange_diameter / 2
        return Envelope(
            x=(self.aperture.x - r_flange, self.aperture.x + r_flange),
            y=(self.aperture.y - r_flange, self.aperture.y + r_flange),
            z=(self.head_front_z, self.socket_mouth_z),
        )


def nav_keytop(layout: dict | None = None, source: str | LayoutSource | None = None) -> NavKeytop:
    """Derive the keytop from the sources. Raises rather than guessing."""
    data = layout if layout is not None else load_layout()
    src = source if isinstance(source, LayoutSource) else layout_source(source)
    stack = panel_stack(data)
    ap = aperture(data, "NAV")
    need(abs(ap.diameter - src.panel_control_aperture) <= EPS_MM,
         f"mechanical/layout.json aperture {ap.name} is D{ap.diameter:.2f} but "
         f"lib/layout.ts PANEL_CUTOUTS.control is {src.panel_control_aperture:.2f}: "
         "the export is stale, regenerate it before printing this part")
    part = printed_part(data, "navKeytop")
    need(abs(float(part["x"]) - ap.x) <= EPS_MM and abs(float(part["y"]) - ap.y) <= EPS_MM,
         f"PRINTED_PARTS.navKeytop sits at ({part['x']}, {part['y']}) but the panel "
         f"aperture is at ({ap.x}, {ap.y}): the keytop must be coaxial with U5")

    # The neck is the aperture fit: the printed D must follow the panel hole.
    neck = ap.diameter - NAV_APERTURE_FIT_TOTAL
    need(abs(neck - float(part["neckDiameter"])) <= EPS_MM,
         f"PRINTED_PARTS.navKeytop.neckDiameter is {part['neckDiameter']} but a "
         f"D{ap.diameter:.2f} aperture with a {NAV_APERTURE_FIT_TOTAL:.2f} mm sliding "
         f"fit gives {neck:.2f}: the two sources disagree")

    # The Z chain is re-derived here from the datasheet and the tolerance band and
    # must land on the envelope lib/layout.ts declares. Change either side alone
    # and this fails instead of exporting a cap the 3D view does not show.
    head_front_z = stack.panel_front_z - float(part["headHeight"])
    socket_floor_z = stack.board_top_z - NAV_HEIGHT_ABOVE_MOUNTING - NAV_AXIAL_FREE_PLAY
    socket_mouth_z = socket_floor_z + float(part["socketDepth"])
    for label, derived, declared in (("zFront", head_front_z, part_z(part, "zFront")),
                                     ("zRear", socket_mouth_z, part_z(part, "zRear"))):
        need(abs(derived - declared) <= EPS_MM,
             f"PRINTED_PARTS.navKeytop.{label} is {declared:.3f} mm but the datasheet "
             f"and tolerance chain gives {derived:.3f} mm (panel stack "
             f"{stack.panel_front_z:.2f}/{stack.panel_rear_z:.2f}/{stack.board_top_z:.2f}, "
             f"head {part['headHeight']}, socket {part['socketDepth']}): update "
             "lib/layout.ts or the assumptions, they must agree")

    return NavKeytop(
        stack=stack,
        aperture=ap,
        stem_diameter=NAV_STEM_DIAMETER,
        stem_top_z=stack.board_top_z - NAV_HEIGHT_ABOVE_MOUNTING,
        switch_body_top_z=stack.board_top_z - NAV_BODY_HEIGHT_ABOVE_MOUNTING,
        fitting_area_max=NAV_KEYTOP_FITTING_AREA_MAX,
        head_height=float(part["headHeight"]),
        neck_diameter=float(part["neckDiameter"]),
        flange_diameter=float(part["flangeDiameter"]),
        flange_thickness=float(part["flangeThickness"]),
        shank_diameter=float(part["shankDiameter"]),
        socket_bore=float(part["socketBore"]),
        socket_floor_z=socket_floor_z,
        socket_mouth_z=socket_mouth_z,
        head_edge_chamfer=NAV_HEAD_EDGE_CHAMFER,
        flange_rear_chamfer=NAV_FLANGE_REAR_CHAMFER,
        mouth_chamfer=NAV_SOCKET_MOUTH_CHAMFER,
    )


def nav_socket_looseness(socket_bore: float) -> dict:
    """Radial looseness of the socket over the SKRHABE010 stem tip, BOTH readings.

    Drawing No.2 puts a rotated rounded square at the stem tip (``1.95 +-0.05``
    across flats with ``R0.5`` corners) and marks ``Ø3`` in its side view. A
    circular socket cannot be "0.10 mm per side" against both:

      * if the mating feature is the Ø3 circle: (3.20 - 3.00)/2 = 0.10 mm;
      * if it is the 1.95 square ACROSS FLATS: (3.20 - 1.95)/2 = 0.625 mm;
      * at that square's CORNERS the tip reaches
        (1.95/2 - 0.5) * sqrt(2) + 0.5 = 1.172 mm, so (3.20 - 2.343)/2 = 0.428 mm.

    The result is a RANGE, and which reading applies is a measurement (which
    feature the stem actually presents in the socket, and at what rotation).
    """
    if socket_bore <= 0.0:
        raise GeometryError(f"socket bore {socket_bore} is not positive")
    radius = socket_bore / 2
    half_flat = NAV_STEM_TIP_SQUARE / 2
    corner_radius = (half_flat - NAV_STEM_TIP_CORNER_RADIUS) * math.sqrt(2) \
        + NAV_STEM_TIP_CORNER_RADIUS
    circle = radius - NAV_STEM_SIDE_VIEW_DIAMETER / 2
    across_flats = radius - half_flat
    at_corners = radius - corner_radius
    step = radius - NAV_STEP_TIP_EXTENT_MM
    values = {
        "socket_bore_mm": socket_bore,
        "if_tip_is_circle_3_00_mm": round(circle, 4),
        "if_tip_is_square_1_95_across_flats_mm": round(across_flats, 4),
        "if_tip_is_square_corner_radius_mm": round(corner_radius, 4),
        "if_tip_is_square_at_corners_mm": round(at_corners, 4),
        # Independent reading of the VENDOR MODEL: +-1.184 mm at the tip.
        "if_tip_is_rounded_square_step_slice_mm": round(step, 4),
        "step_tip_extent_mm": NAV_STEP_TIP_EXTENT_MM,
        "step_provenance": NAV_STEP_PROVENANCE,
        "step_vs_drawing_corner_delta_mm": round(abs(NAV_STEP_TIP_EXTENT_MM
                                                     - corner_radius), 4),
        "tip_square_tolerance_mm": NAV_STEM_TIP_SQUARE_TOLERANCE,
        "looseness_min_mm": round(min(circle, across_flats, at_corners, step), 4),
        "looseness_max_mm": round(max(circle, across_flats, at_corners, step), 4),
        "rotation_centre_0_49_dimension": ("Drawing No.2 labels 'Center of stem "
                                           "rotation 0.49'; the dimension's "
                                           "endpoints and its sign in the PCB "
                                           "plane are NOT established here"),
    }
    return values


def nav_socket_engagement(socket_mouth_z: float, socket_floor_z: float,
                          stem_top_z: float, mounting_face_z: float) -> dict:
    """Which part of the stem the socket actually surrounds.

    Measured from the MOUNTING FACE (the board's top face), the vendor model puts
    the rounded square from Z 3.5 to Z 4.9 and the Ø3 circular section at Z 2.5.
    The socket spans mouth z to floor z in panel Z, i.e. the same heights measured
    from the board face, so the engagement can be compared with the shape bands
    DIRECTLY: if the socket only covers the square band, its Ø3.2 circular bore
    never touches the Ø3 collar and there is no cylindrical guide at all.
    """
    mount = mounting_face_z
    top = round(mount - socket_floor_z, 4)          # highest point of the socket
    bottom = round(mount - socket_mouth_z, 4)       # lowest point (the mouth)
    square = NAV_STEP_SQUARE_SLICE_Z
    return {
        "socket_above_mounting_face_mm": [bottom, top],
        "socket_engages_stem_length_mm": round(top - bottom, 4),
        "stem_tip_above_mounting_face_mm": round(mount - stem_top_z, 4),
        "square_band_above_mounting_face_mm": list(square),
        "circular_section_above_mounting_face_mm": NAV_STEP_CIRCULAR_SECTION_Z,
        "surrounds_rounded_square": bottom < square[1] and top > square[0],
        "reaches_the_circular_collar": bottom <= NAV_STEP_CIRCULAR_SECTION_Z,
        "provenance": NAV_STEP_PROVENANCE,
    }


def validate_nav_keytop(g: NavKeytop, layout: dict | None = None) -> list[str]:
    """Check every keytop rule. Returns notices; raises GeometryError on failure."""
    notices: list[str] = []

    # 1. The cap is assembled through the panel hole: the head leads, so the
    #    head and the neck are one diameter and both must clear the aperture.
    need(0.1 - EPS_MM <= g.aperture_clearance <= 0.2 + EPS_MM,
         f"neck D{g.neck_diameter:.2f} in the D{g.aperture_diameter:.2f} aperture "
         f"gives {g.aperture_clearance:.2f} mm diametral clearance, outside the "
         "0.10-0.20 mm sliding fit")
    # 2. The flange is the retainer: wider than the aperture, and bearing on it.
    need(g.flange_diameter > g.aperture_diameter + 2 * PRINT_MIN_WALL,
         f"flange D{g.flange_diameter:.2f} leaves only {g.bearing_ring:.2f} mm of "
         f"bearing ring on the D{g.aperture_diameter:.2f} aperture")
    need(g.bearing_ring >= PRINT_MIN_WALL,
         f"bearing ring {g.bearing_ring:.2f} mm is under the {PRINT_MIN_WALL} mm wall rule")
    # 3. The socket must clear the stem, stay inside the ALPS fitting area and
    #    leave printable walls.
    need(g.socket_bore > g.stem_diameter,
         f"socket D{g.socket_bore:.2f} does not clear the D{g.stem_diameter:.1f} stem")
    need(0.1 - EPS_MM <= g.socket_bore - g.stem_diameter <= 0.4 + EPS_MM,
         f"socket bore clearance {g.socket_bore - g.stem_diameter:.2f} mm is outside "
         "0.10-0.40 mm: a tighter bore will not print, a looser one will not hold")
    need(g.socket_depth <= g.fitting_area_max + EPS_MM,
         f"socket depth {g.socket_depth:.2f} mm exceeds the ALPS {g.fitting_area_max:.1f} mm "
         "keytop fitting area")
    need(g.socket_depth >= 1.0,
         f"socket depth {g.socket_depth:.2f} mm is too shallow to guide the stem")
    need(g.socket_wall >= PRINT_MIN_WALL,
         f"socket wall {g.socket_wall:.2f} mm is under the {PRINT_MIN_WALL} mm rule")
    # 4. At rest the socket floor must float above the stem top.
    need(0.1 - EPS_MM <= g.free_play <= 0.2 + EPS_MM,
         f"axial free play {g.free_play:.2f} mm is outside 0.10-0.20 mm: the switch "
         "is either pre-loaded or the cap rattles")
    need(g.stem_engagement >= 1.0,
         f"the socket only reaches {g.stem_engagement:.2f} mm past the stem top")
    # 5. The head is 3-5 mm proud, and the finger must not reach the panel.
    need(3.0 - EPS_MM <= g.head_height <= 5.0 + EPS_MM,
         f"head is {g.head_height:.2f} mm proud of the panel front, outside 3-5 mm")
    # 6. The part must fit the 8.4 mm gap without seating on the switch body.
    need(g.switch_body_clearance >= NAV_ASSEMBLY_CLEARANCE_MIN,
         f"the cap bottom is {g.switch_body_clearance:.2f} mm above the switch body "
         f"top, under the {NAV_ASSEMBLY_CLEARANCE_MIN:.2f} mm assembly clearance")
    need(g.gap_below > 0.0,
         f"the cap reaches Z {g.socket_mouth_z:.2f}, below the front board top "
         f"{g.stack.board_top_z:.2f}")
    need(g.socket_mouth_z < g.stack.board_top_z,
         "the cap is longer than the panel-to-board gap")
    # 7. The flange hangs in the gap beside the panel hardware.
    data = layout if layout is not None else load_layout()
    clearances = panel_feature_clearances(data, g.centre, g.flange_diameter / 2,
                                          skip=g.aperture.name)
    if clearances:
        nearest, distance = clearances[0]
        need(distance > 0.0,
             f"the flange overlaps panel feature {nearest} by {-distance:.2f} mm")
        notices.append(f"flange clearance to the nearest panel feature ({nearest}) "
                       f"is {distance:.2f} mm")
    # 8. The socket is a CIRCLE over a ROUNDED-SQUARE tip: report the range.
    loose = nav_socket_looseness(g.socket_bore)
    engage = nav_socket_engagement(g.socket_mouth_z, g.socket_floor_z, g.stem_top_z,
                                   g.stack.board_top_z)
    notices.append(
        f"socket ENGAGEMENT (vendor-model shape): the bore spans Z "
        f"{engage['socket_above_mounting_face_mm'][0]:.2f}..."
        f"{engage['socket_above_mounting_face_mm'][1]:.2f} mm above the mounting "
        f"face, which is the rounded-square band {engage['square_band_above_mounting_face_mm']} "
        f"and NOT the Ø3 collar at Z "
        f"{engage['circular_section_above_mounting_face_mm']:.1f}. So the D"
        f"{g.socket_bore:.2f} bore surrounds the SQUARE, not a cylinder: nominal "
        f"radial play {loose['if_tip_is_rounded_square_step_slice_mm']:.2f} mm at "
        f"the tip's farthest points and more toward its flats, and the collar is "
        f"never reached. There is no cylindrical guide, no press fit and no grip "
        "to claim - MEASURE the real part")
    notices.append(
        f"socket looseness over the stem tip is a RANGE, not "
        f"{g.socket_bore - g.stem_diameter:.2f} mm per side: drawing No.2 shows a "
        f"rotated rounded square at the tip ({NAV_STEM_TIP_SQUARE:.2f} +-"
        f"{NAV_STEM_TIP_SQUARE_TOLERANCE:.2f} across flats, R{NAV_STEM_TIP_CORNER_RADIUS:.1f} "
        f"corners) and marks Ø{NAV_STEM_SIDE_VIEW_DIAMETER:.1f} in its side view. "
        f"Against the D{g.socket_bore:.2f} socket that is "
        f"{loose['looseness_min_mm']:.2f}...{loose['looseness_max_mm']:.2f} mm per "
        "side depending on which feature mates and at what rotation. MEASURE the "
        "real engagement; a circular bore cannot be claimed as a gripping fit")
    # 9. Travel datum and the manufacturer's side-force note, both quoted.
    notices.append(
        f"travel datum: {NAV_TRAVEL_MEASUREMENT_NOTE} The printed cap's engagement "
        f"({g.stem_engagement:.2f} mm over the tip) is measured at a different height "
        f"from that {NAV_TRAVEL_MEASUREMENT_HEIGHT_MM:.1f} mm figure, so no travel "
        "value may be transferred to the cap without a measurement")
    notices.append(
        f"ALPS side-force note: {NAV_SIDE_FORCE_NOTE}. The printed cap is a 4-way "
        "control surface, so whether the note restricts a finger pushing the cap "
        "sideways must be clarified with the supplier or on a sample - it is NOT "
        "treated here as a proven prohibition or as a proven permission")
    # 10. Physical gates the model cannot settle stay visible.
    notices.append(
        f"cap free play {g.free_play:.2f} mm equals the {NAV_TRAVEL_CENTRE:.2f} mm "
        "centre-push travel: the first 0.15 mm of a push closes the gap before the "
        "switch moves (not verified on a sample)")
    notices.append(
        "PROVISIONAL: the keytop presses onto the D3.0 stem with a 0.10 mm/side "
        "clearance and is held axially by the stem fit and by the flange under the "
        "panel; whether it stays on the stem when the panel is lifted off must be "
        "checked on the first article")
    notices.append(
        "PROVISIONAL: the switch's moving-stem collar above the 1.7 mm keytop "
        "fitting area is not dimensioned in the catalog, so the socket floor "
        "clearance to anything other than the stem top is unverified")
    return notices


def describe_nav_keytop(g: NavKeytop) -> list[str]:
    """The keytop's dimension table, as printed by the generator."""
    return [
        f"panel stack: front Z {g.stack.panel_front_z:.2f}, rear Z "
        f"{g.stack.panel_rear_z:.2f}, board top Z {g.stack.board_top_z:.2f} "
        f"(gap {g.stack.gap:.2f} mm)",
        f"aperture {g.aperture.name}: D{g.aperture_diameter:.2f} at "
        f"({g.aperture.x:.2f}, {g.aperture.y:.2f})  [mechanical/layout.json]",
        f"head/neck: D{g.neck_diameter:.2f}, head {g.head_height:.2f} proud "
        f"(Z {g.head_front_z:.2f}..{g.stack.panel_front_z:.2f}), neck "
        f"{g.neck_length:.2f} through the panel, aperture clearance "
        f"{g.aperture_clearance:.2f} mm",
        f"flange: D{g.flange_diameter:.2f} x {g.flange_thickness:.2f} thick "
        f"(Z {g.flange_front_z:.2f}..{g.flange_rear_z:.2f}), bearing ring "
        f"{g.bearing_ring:.2f} mm",
        f"shank: D{g.shank_diameter:.2f} x {g.shank_length:.2f} long "
        f"(Z {g.flange_rear_z:.2f}..{g.socket_mouth_z:.2f})",
        f"socket: D{g.socket_bore:.2f} x {g.socket_depth:.2f} deep "
        f"(Z {g.socket_floor_z:.2f}..{g.socket_mouth_z:.2f}), wall "
        f"{g.socket_wall:.2f} mm",
        f"stem: D{g.stem_diameter:.1f}, top Z {g.stem_top_z:.2f}, engagement "
        f"{g.stem_engagement:.2f} mm, axial free play {g.free_play:.2f} mm",
        f"switch body top Z {g.switch_body_top_z:.2f}: cap clearance "
        f"{g.switch_body_clearance:.2f} mm; overall length {g.overall_length:.2f} mm",
    ]


# ---------------------------------------------------------------------------
# PROTOTYPE NAV socket matched to the rounded-square stem
# ---------------------------------------------------------------------------
# The installed part keeps its D3.2 CIRCULAR socket (unchanged). These are
# PRINTABLE PROTOTYPES: the same head, neck, flange and shank, with the socket
# cut as a SQUARE matched to the tip that the vendor model actually shows
# (1.95 +-0.05 across flats, R0.5 corners, +-1.184 mm extent). A square bore on a
# square tip is the only shape that can guide this stem at all; a circular bore
# cannot.
#
# They are labelled specimens, not a chosen design: three clearance sizes plus one
# ROCK variant, so the first article decides between them by measurement. Nothing
# here is released, and the installed `nav-keytop.stl` does not change.
@dataclass(frozen=True)
class NavProtoSpec:
    """One numbered prototype: a square socket, optionally a narrower neck."""

    name: str
    square_mm: float
    corner_radius_mm: float
    socket_depth_mm: float
    neck_diameter_mm: float
    status: str = "PROTOTYPE, not the installed part"


# Square across flats, chosen around the 1.95 mm tip: 0.025 / 0.075 / 0.125 mm
# per side. The corner radius is 0.6 mm: the tip's corners are R0.5, so every
# specimen clears them by at least 0.1 mm (a 0.5 mm corner on both would need an
# exact match to slide, which no FDM print achieves).
NAV_PROTO_SQUARES = (2.00, 2.10, 2.20)
NAV_PROTO_CORNER_RADIUS = 0.6
NAV_PROTO_SOCKET_DEPTH = 1.4
NAV_PROTO_LEAD_IN = 0.30         # square lead-in at the mouth, printed on the bed
NAV_PROTO_ROCK_NECK = 6.8        # 0.20 mm per side in the 7.2 mm aperture


def nav_proto_specimens(g: "NavKeytop") -> list[NavProtoSpec]:
    """The four printable prototypes, each validated before it can be built."""
    specs = [
        NavProtoSpec(name=f"square-{square:.2f}", square_mm=square,
                     corner_radius_mm=NAV_PROTO_CORNER_RADIUS,
                     socket_depth_mm=NAV_PROTO_SOCKET_DEPTH,
                     neck_diameter_mm=g.neck_diameter)
        for square in NAV_PROTO_SQUARES
    ]
    specs.append(NavProtoSpec(
        name=f"rock-square-{NAV_PROTO_SQUARES[1]:.2f}",
        square_mm=NAV_PROTO_SQUARES[1],
        corner_radius_mm=NAV_PROTO_CORNER_RADIUS,
        socket_depth_mm=NAV_PROTO_SOCKET_DEPTH,
        neck_diameter_mm=NAV_PROTO_ROCK_NECK,
        status="PROTOTYPE, ROCK variant: narrower neck, NOT the installed part",
    ))
    for spec in specs:
        validate_nav_proto(spec, g)
    return specs


def nav_proto_numbers(spec: NavProtoSpec, g: "NavKeytop") -> dict:
    """Everything a reader needs to compare one prototype with the installed part."""
    half = spec.square_mm / 2
    corner = (half - spec.corner_radius_mm) * math.sqrt(2) + spec.corner_radius_mm
    aperture_clearance = g.aperture_diameter - spec.neck_diameter_mm
    thickness = g.stack.thickness
    # Tilt available before both faces of the neck touch: atan(cl_total/thickness).
    tilt = math.degrees(math.atan(aperture_clearance / thickness))
    # Free rotation of a square socket over the square tip before the flats meet.
    per_side = (spec.square_mm - NAV_STEM_TIP_SQUARE) / 2
    backlash = math.degrees(math.atan(2 * per_side / spec.square_mm))
    floor_z = nav_proto_floor_z(g)
    mouth_z = nav_proto_rear_z(g, spec)      # a BLIND socket: the mouth IS the rear face
    mount = g.stack.board_top_z
    return {
        "name": spec.name,
        "status": spec.status,
        "square_across_flats_mm": spec.square_mm,
        "corner_radius_mm": spec.corner_radius_mm,
        "clearance_per_side_mm": round(per_side, 4),
        "corner_clearance_mm": round(spec.corner_radius_mm
                                     - NAV_STEM_TIP_CORNER_RADIUS, 4),
        "circumscribed_radius_mm": round(corner, 4),
        "socket_wall_to_shank_mm": round(g.shank_diameter / 2 - corner, 4),
        "socket_depth_mm": round(nav_proto_rear_z(g, spec) - floor_z, 4),
        "socket_floor_z": round(floor_z, 4),
        "socket_rear_face_z": round(nav_proto_rear_z(g, spec), 4),
        "lead_in_depth_mm": round(min(NAV_PROTO_LEAD_IN, nav_proto_rear_z(g, spec)
                                      - floor_z), 4),
        "installed_part_mouth_z": round(g.socket_mouth_z, 4),
        "neck_diameter_mm": spec.neck_diameter_mm,
        "aperture_clearance_mm": round(aperture_clearance, 4),
        "tilt_before_binding_deg": round(tilt, 3),
        "square_rotation_backlash_deg": round(backlash, 3),
        "socket_above_mounting_face_mm": [round(mount - mouth_z, 3),
                                          round(mount - floor_z, 3)],
        "square_tip_band_above_mounting_face_mm": list(NAV_STEP_SQUARE_SLICE_Z),
        "only_engages_the_square_tip": bool(
            (mount - mouth_z) >= NAV_STEP_SQUARE_SLICE_Z[0]),
        "reaches_the_circular_collar": bool(
            (mount - mouth_z) <= NAV_STEP_CIRCULAR_SECTION_Z),
        "prints_mouth_down_on_the_bed": True,
        "not_claimed": [
            "that the socket grips, retains or centres the cap",
            "that the square/radius/rotation match the supplied part",
            "that the cap cannot rock or press the switch",
            "any measured engagement; the stem shape is vendor MODEL geometry",
        ],
    }


def nav_proto_floor_z(g: "NavKeytop") -> float:
    """The prototype's socket floor: the stem top less the intended free play.

    The free play is the SAME as the installed part's, so the switch is not
    pre-loaded by a prototype either.
    """
    return round(g.stem_top_z - NAV_AXIAL_FREE_PLAY, 4)


def nav_proto_rear_z(g: "NavKeytop", spec: NavProtoSpec) -> float:
    """The prototype's own REAR face, i.e. the blind socket's mouth.

    The installed part's rear face is at ``socket_mouth_z`` 6.95 because its
    socket is 1.50 deep. A prototype that promises a 1.40 mm socket must therefore
    END 0.10 mm EARLIER; reusing the installed stock would cut 1.50 mm instead and
    silently shift the socket 0.10 mm down the stem.
    """
    return round(nav_proto_floor_z(g) + spec.socket_depth_mm, 4)


def nav_proto_envelope(g: "NavKeytop", spec: NavProtoSpec) -> "Envelope":
    """The prototype's intended solid envelope: its OWN rear face, not the installed one."""
    r = g.flange_diameter / 2
    return Envelope(
        x=(g.centre[0] - r, g.centre[0] + r),
        y=(g.centre[1] - r, g.centre[1] + r),
        z=(g.head_front_z, nav_proto_rear_z(g, spec)),
    )


def validate_nav_proto(spec: NavProtoSpec, g: "NavKeytop") -> None:
    """Refuse a prototype that cannot be printed or cannot reach the tip."""
    need(0.02 <= (spec.square_mm - NAV_STEM_TIP_SQUARE) / 2 <= 0.30,
         f"{spec.name}: {spec.square_mm:.2f} mm square gives "
         f"{(spec.square_mm - NAV_STEM_TIP_SQUARE) / 2:.3f} mm per side over the "
         f"{NAV_STEM_TIP_SQUARE:.2f} mm tip, outside the 0.02-0.30 mm prototype band")
    need(spec.corner_radius_mm >= NAV_STEM_TIP_CORNER_RADIUS + 0.05,
         f"{spec.name}: corner R{spec.corner_radius_mm:.2f} does not clear the tip's "
         f"R{NAV_STEM_TIP_CORNER_RADIUS:.1f} corners")
    need(spec.corner_radius_mm >= 0.4,
         f"{spec.name}: a corner under R0.4 is a hairline in a 0.4 mm print")
    half = spec.square_mm / 2
    corner = (half - spec.corner_radius_mm) * math.sqrt(2) + spec.corner_radius_mm
    need(g.shank_diameter / 2 - corner >= PRINT_MIN_WALL,
         f"{spec.name}: only {g.shank_diameter / 2 - corner:.2f} mm of wall between "
         f"the socket corner and the D{g.shank_diameter:.2f} shank")
    need(1.0 <= spec.socket_depth_mm <= NAV_KEYTOP_FITTING_AREA_MAX,
         f"{spec.name}: socket depth {spec.socket_depth_mm:.2f} mm is outside "
         f"1.0-{NAV_KEYTOP_FITTING_AREA_MAX:.1f} mm")
    need(spec.neck_diameter_mm <= g.aperture_diameter - 0.2 + EPS_MM,
         f"{spec.name}: neck D{spec.neck_diameter_mm:.2f} leaves less than 0.10 mm "
         f"per side in the D{g.aperture_diameter:.2f} aperture")
    floor_z = nav_proto_floor_z(g)
    mouth_z = nav_proto_rear_z(g, spec)
    above = round(g.stack.board_top_z - mouth_z, 4)
    need(above >= NAV_STEP_SQUARE_SLICE_Z[0],
         f"{spec.name}: the socket mouth reaches {above:.2f} mm above the mounting "
         f"face, below the square tip band {NAV_STEP_SQUARE_SLICE_Z}: it would swallow "
         "the Ø3 collar and could not guide the tip")
    need(mouth_z < g.stack.board_top_z,
         f"{spec.name}: the socket mouth is behind the front board")
    need(spec.socket_depth_mm < g.socket_depth,
         f"{spec.name}: a {spec.socket_depth_mm:.2f} mm socket is not shorter than the "
         f"installed {g.socket_depth:.2f} mm, so its rear face would not move")


# ---------------------------------------------------------------------------
# Part 2: value knob for the Bourns PEC11R-4015F-S0024
# ---------------------------------------------------------------------------
@dataclass(frozen=True)
class Knob:
    """Value-knob inputs. Derived positions and fits are properties."""

    stack: PanelStack
    aperture: Circle
    display_opening_right: float
    column_x: float
    body_diameter: float
    shaft_diameter: float
    shaft_length: float
    shaft_flat_depth: float
    bushing_shoulder_z: float
    height: float
    rear_z: float
    bore_diameter: float
    bore_flat_plane: float
    flat_angle_deg: float
    flute_count: int
    flute_radius: float
    flute_depth: float
    grub_pilot_diameter: float
    grub_z: float
    edge_chamfer_front: float
    edge_chamfer_rear: float
    bore_mouth_chamfer: float

    @property
    def centre(self) -> tuple[float, float]:
        return (self.column_x, self.aperture.y)

    @property
    def front_z(self) -> float:
        return self.rear_z - self.height

    @property
    def bushing_tip_z(self) -> float:
        """The bushing's threaded END, Z datum of the panel front (mm).

        Page 2: the 6.5 mm body-to-shoulder distance plus the LB thread length
        gives the bushing tip; L is measured from the same shoulder to the shaft
        tip, so the shaft protrudes L - LB beyond the bushing end.
        """
        return self.bushing_shoulder_z - ENC_THREAD_LENGTH

    @property
    def shaft_tip_z(self) -> float:
        """The free end of the shaft, Z datum of the panel front (mm).

        L = 15.0 mm is measured FROM THE SHOULDER (page 3 table), not from the
        bushing end: the earlier 15 mm-beyond-the-bushing reading is withdrawn.
        """
        return self.bushing_shoulder_z - self.shaft_length

    @property
    def shaft_proud(self) -> float:
        """Shaft length in front of the panel front face (mm)."""
        return self.stack.panel_front_z - self.shaft_tip_z

    @property
    def thread_proud(self) -> float:
        """Threaded bushing in front of the panel FRONT face (mm).

        0.9 mm at the 10.6 mm panel. This is the length the mounting nut and the
        washer have to share, so it is compared with the supplied stack below.
        """
        return round(self.stack.panel_front_z - self.bushing_tip_z, 6)

    @property
    def shaft_round_radius(self) -> float:
        return self.shaft_diameter / 2

    @property
    def bore_through(self) -> bool:
        """The bore is a THROUGH bore: nothing solid covers the shaft tip.

        knob.py cuts the bore from one end to the other (and 1 mm past both
        faces), so no material sits over the shaft tip. A claim like "the tip
        ends inside the knob" must therefore mean "7.8 mm below the TOP of a
        through bore", never "7.8 mm of plastic over the tip".
        """
        return True

    @property
    def encoder(self) -> dict:
        """The modelled PEC11R stack: L15/LB5, knob rear and bore flat as built."""
        return encoder_stack(self.stack.board_top_z - self.stack.panel_front_z,
                             knob_rear_above_panel_front=round(-self.rear_z, 3),
                             bore_flat_plane=self.bore_flat_plane)

    @property
    def encoder_candidate_l20(self) -> dict:
        """The UNAPPROVED L = 20 / LB = 7 candidate, same knob and bore."""
        return encoder_stack(self.stack.board_top_z - self.stack.panel_front_z,
                             knob_rear_above_panel_front=round(-self.rear_z, 3),
                             shaft_length=20.0, thread_length=7.0,
                             flat_length=enc_flat_length(20.0),
                             bore_flat_plane=self.bore_flat_plane)

    @property
    def nut_stack(self) -> float:
        """The supplied nut plus washer, from the datasheet (mm)."""
        return KNOB_NUT_AND_WASHER_STACK

    @property
    def nut_stack_shortfall(self) -> float:
        """Positive when the supplied nut/washer cannot sit on the exposed thread.

        DESIGN INCOMPATIBILITY, not a tolerance: with 0.9 mm of bushing past the
        panel front and 2.5 mm of supplied hardware, the nut cannot reach the
        panel face at all. No encoder, PCB or panel change is made here - the
        remedy is the owner's decision (a longer-bushing code such as
        PEC11R-4020F-S0024 is recorded as an UNVERIFIED candidate only).
        """
        return round(self.nut_stack - self.thread_proud, 6)

    @property
    def tip_cover(self) -> float:
        """How far the shaft tip sits BELOW the top of the THROUGH bore (mm).

        The bore runs from face to face, so this is not a depth of plastic over
        the shaft: it is only the distance from the shaft tip to the knob's front
        FACE along an open bore.
        """
        return self.shaft_tip_z - self.front_z

    @property
    def rear_clearance(self) -> float:
        """Gap from the panel front face to the knob rear face (mm)."""
        return self.stack.panel_front_z - self.rear_z

    @property
    def nut_clearance(self) -> float:
        """Gap from the assumed supplied nut/washer stack to the knob rear face."""
        return self.rear_clearance - KNOB_NUT_AND_WASHER_STACK

    @property
    def shaft_flat_plane(self) -> float:
        """Distance from the axis to the shaft's flat (mm), from the datasheet."""
        return self.shaft_diameter / 2 - self.shaft_flat_depth

    @property
    def bore_flat_clearance(self) -> float:
        """Gap between the bore's flat and the shaft's flat (mm)."""
        return self.bore_flat_plane - self.shaft_flat_plane

    @property
    def bore_clearance(self) -> float:
        return self.bore_diameter - self.shaft_diameter

    @property
    def wall(self) -> float:
        """Thinnest wall of the knob body: bore to OD (mm)."""
        return (self.body_diameter - self.bore_diameter) / 2

    @property
    def grub_pilot_length(self) -> float:
        """Pilot hole length from the OD to the bore flat (mm)."""
        return self.body_diameter / 2 - self.bore_flat_plane

    @property
    def grub_tip_protrusion(self) -> float:
        """Protrusion past the BORE's flat plane with the screw flush at the OD.

        Negative means the screw does not even reach the bore's own flat plane
        when its outer end is flush with the knob's outer diameter.
        """
        return round(KNOB_GRUB_SCREW_LENGTH - self.grub_pilot_length, 3)

    @property
    def set_screw(self) -> dict:
        """The set screw's ACTUAL range along its own axis, not a flush-only tip.

        An M3 screw may also stand proud of the knob's outer surface, so a
        flush-only check cannot prove that it misses the shaft's flat. This
        model reports the whole range:

          * ``reach_required_mm``  bore flat plane -> shaft flat plane
          * ``flush_protrusion_mm`` how far an ``L`` mm screw reaches past the
            bore's flat plane when its outer end is flush with the OD
          * ``proud_needed_mm``     how far it must stand proud to touch the flat
          * ``engagement_at_proud_mm`` plastic thread engaged in that position

        It does NOT predict clamping force, torque or that the plastic thread
        survives; those are unverified.
        """
        return knob_set_screw_model(self)

    @property
    def left_edge(self) -> float:
        return self.column_x - self.body_diameter / 2

    @property
    def display_clearance(self) -> float:
        """Knob body to the front-board display opening edge (mm)."""
        return self.left_edge - self.display_opening_right

    @property
    def flute_pitch_deg(self) -> float:
        return 360.0 / self.flute_count

    @property
    def flute_phase_deg(self) -> float:
        """Angle of the first flute. Lands sit half a pitch away, on the flat."""
        return self.flat_angle_deg - self.flute_pitch_deg / 2

    def has_land_at(self, angle_deg: float) -> bool:
        """True when the printed OD keeps its full radius at ``angle_deg``."""
        pitch = self.flute_pitch_deg
        offset = (angle_deg - self.flute_phase_deg - pitch / 2) % pitch
        return min(offset, pitch - offset) <= 1e-6

    @property
    def flute_cutter_radius(self) -> float:
        """Radius of the flute cutter's centre circle (mm)."""
        return self.body_diameter / 2 + self.flute_radius - self.flute_depth

    @property
    def envelope(self) -> "Envelope":
        r = self.body_diameter / 2
        return Envelope(
            x=(self.column_x - r, self.column_x + r),
            y=(self.aperture.y - r, self.aperture.y + r),
            z=(self.front_z, self.rear_z),
        )


def knob(layout: dict | None = None, source: str | LayoutSource | None = None) -> Knob:
    """Derive the knob from the sources. Raises rather than guessing."""
    data = layout if layout is not None else load_layout()
    src = source if isinstance(source, LayoutSource) else layout_source(source)
    stack = panel_stack(data)
    ap = aperture(data, "VALUE ENCODER")
    need(abs(ap.x - src.knob_column_x) <= EPS_MM,
         f"mechanical/layout.json puts the value encoder at x {ap.x} but "
         f"lib/layout.ts RIGHT_COLUMN.x is {src.knob_column_x}: the export is stale")
    need(abs(ap.diameter - src.panel_control_aperture) <= EPS_MM,
         f"mechanical/layout.json aperture {ap.name} is D{ap.diameter:.2f} but "
         f"lib/layout.ts PANEL_CUTOUTS.control is {src.panel_control_aperture:.2f}: "
         "the export is stale, regenerate it before printing this part")
    part = printed_part(data, "knob")
    need(abs(float(part["x"]) - ap.x) <= EPS_MM and abs(float(part["y"]) - ap.y) <= EPS_MM,
         f"PRINTED_PARTS.knob sits at ({part['x']}, {part['y']}) but the panel "
         f"aperture is at ({ap.x}, {ap.y}): the knob must be coaxial with U6")
    need(abs(float(part["bodyDiameter"]) - src.knob_body_diameter) <= EPS_MM,
         f"PRINTED_PARTS.knob.bodyDiameter is {part['bodyDiameter']} but the "
         f"layout's knob-body note says {src.knob_body_diameter}: the two sources "
         "disagree")

    # The Z chain: the rear face clears the assumed supplied nut + washer, the
    # front face is one knob height further forward. Both must match the
    # envelope lib/layout.ts declares.
    rear = stack.panel_front_z - (KNOB_NUT_AND_WASHER_STACK + KNOB_REAR_AIR)
    front = rear - float(part["height"])
    for label, derived, declared in (("zRear", rear, part_z(part, "zRear")),
                                     ("zFront", front, part_z(part, "zFront"))):
        need(abs(derived - declared) <= EPS_MM,
             f"PRINTED_PARTS.knob.{label} is {declared:.3f} mm but the assumed "
             f"nut/washer stack gives {derived:.3f} mm: update lib/layout.ts or "
             f"the assumptions, they must agree")

    # Page 2: 6.5 mm from the board to the body/bushing SHOULDER. L and LB are
    # both measured from that shoulder, so the bushing tip and the shaft tip are
    # two separate planes and neither is the shoulder itself.
    bushing_shoulder = stack.board_top_z - ENC_BODY_TO_MOUNTING_SURFACE
    shaft_flat = ENC_SHAFT_DIAMETER / 2 - ENC_SHAFT_FLAT_DEPTH
    bore = float(part["boreDiameter"])
    return Knob(
        stack=stack,
        aperture=ap,
        display_opening_right=src.display_opening_right,
        column_x=src.knob_column_x,
        body_diameter=float(part["bodyDiameter"]),
        shaft_diameter=ENC_SHAFT_DIAMETER,
        shaft_length=ENC_SHAFT_LENGTH,
        shaft_flat_depth=ENC_SHAFT_FLAT_DEPTH,
        bushing_shoulder_z=bushing_shoulder,
        height=float(part["height"]),
        rear_z=rear,
        bore_diameter=bore,
        bore_flat_plane=shaft_flat + (bore - ENC_SHAFT_DIAMETER) / 2,
        flat_angle_deg=KNOB_FLAT_ANGLE_DEG,
        flute_count=KNOB_FLUTE_COUNT,
        flute_radius=KNOB_FLUTE_RADIUS,
        flute_depth=KNOB_FLUTE_DEPTH,
        grub_pilot_diameter=KNOB_GRUB_PILOT_DIAMETER,
        grub_z=rear - KNOB_GRUB_TIP_FROM_REAR,
        edge_chamfer_front=KNOB_EDGE_CHAMFER_FRONT,
        edge_chamfer_rear=KNOB_EDGE_CHAMFER_REAR,
        bore_mouth_chamfer=KNOB_BORE_MOUTH_CHAMFER,
    )


def knob_set_screw_model(k: Knob, screw_length: float = KNOB_GRUB_SCREW_LENGTH) -> dict:
    """The set screw's reach along its own axis, for one screw length.

    ``proud_needed`` is the distance the screw must stand out of the knob's outer
    surface for its tip to touch the shaft's flat. A positive value above
    ``max_proud_mm`` means the screw cannot reach the flat at all at that length;
    anything in between is mechanically POSSIBLE, with unverified clamp strength
    and an exposed screw tip at the grip.
    """
    if screw_length <= 0.0:
        raise GeometryError(f"screw length {screw_length} is not positive")
    reach = round(k.bore_flat_plane - k.shaft_flat_plane, 3)
    flush = round(screw_length - k.grub_pilot_length, 3)
    # To advance the tip, the screw moves INWARD: its outer end goes BELOW the OD
    # surface. A "proud" screw would move the tip the WRONG way.
    recess_needed = round(max(0.0, reach - flush), 3)
    engagement = round(k.grub_pilot_length - recess_needed, 3)
    max_recess = 1.0         # beyond this the key has no usable purchase
    return {
        "screw": KNOB_GRUB_SCREW if screw_length == KNOB_GRUB_SCREW_LENGTH
                  else f"M3 x {screw_length:.1f} set screw, flat point",
        "screw_length_mm": screw_length,
        "pilot_length_mm": round(k.grub_pilot_length, 3),
        "bore_flat_plane_mm": round(k.bore_flat_plane, 3),
        "shaft_flat_plane_mm": round(k.shaft_flat_plane, 3),
        "reach_required_mm": reach,
        "flush_protrusion_mm": flush,
        # A NEGATIVE flush protrusion means the tip is short of the bore flat; the
        # screw must be turned IN by this much. The outer end is then RECESSED
        # below the knob's outer surface, which is what changes the key access.
        "recess_needed_mm": recess_needed,
        "outer_end_below_od_mm": recess_needed,
        "reaches_flush": flush >= reach,
        "reaches_if_recessed": flush >= reach or recess_needed <= max_recess,
        "max_recess_mm": max_recess,
        "engagement_if_recessed_mm": engagement,
        "key_access": ("UNVERIFIED: the selected set screw's hex key size is not "
                       "recorded, the 2.50 mm figure in this module is the M3x0.5 TAP "
                       "DRILL, not a key across-flats, and a recessed screw leaves the "
                       "key working inside a printed D2.50 pilot"),
        "status": ("reaches the shaft flat with the screw flush"
                   if flush >= reach else
                   (f"needs the screw turned {recess_needed:.2f} mm INTO the OD, or a "
                    "longer screw"
                    if recess_needed <= max_recess else
                    "does not reach the shaft flat at this length")),
        "not_claimed": ["clamping force", "thread strength in plastic",
                        "torque or vibration resistance", "the printed bore fit",
                        "the set screw's key size or that a key can reach it"],
    }


def validate_knob(k: Knob) -> list[str]:
    """Check every knob rule. Returns notices; raises GeometryError on failure."""
    notices: list[str] = []

    # 1. The bore must accept the shaft cold: round part and flat.
    need(k.bore_clearance > 0.0,
         f"bore D{k.bore_diameter:.2f} does not clear the D{k.shaft_diameter:.1f} shaft")
    need(0.1 - EPS_MM <= k.bore_clearance <= 0.4 + EPS_MM,
         f"bore clearance {k.bore_clearance:.2f} mm is outside 0.10-0.40 mm")
    need(k.bore_flat_clearance > 0.0,
         f"the bore's flat at {k.bore_flat_plane:.2f} mm from the axis collides with "
         f"the shaft's flat at {k.shaft_flat_plane:.2f} mm")
    need(abs(k.bore_flat_clearance - k.bore_clearance / 2) <= EPS_MM,
         f"the bore flat clearance {k.bore_flat_clearance:.2f} mm does not match the "
         f"bore's round clearance {k.bore_clearance / 2:.2f} mm per side")
    need(k.wall >= PRINT_MIN_WALL,
         f"knob wall {k.wall:.2f} mm is under the {PRINT_MIN_WALL} mm rule")
    # 2. Height: inside the owner band, and the shaft must end inside the knob.
    need(12.0 - EPS_MM <= k.height <= 16.0 + EPS_MM,
         f"knob height {k.height:.2f} mm is outside the 12-16 mm band")
    need(k.tip_cover > 0.0,
         f"the shaft tip (Z {k.shaft_tip_z:.2f}) stands {abs(k.tip_cover):.2f} mm proud "
         f"of the knob's front face (Z {k.front_z:.2f})")
    # 3. The rear face must clear the panel front and the supplied nut/washer.
    need(k.rear_clearance > 0.0,
         f"the knob rear face is {k.rear_clearance:.2f} mm behind the panel front face")
    need(k.nut_clearance >= 0.0,
         f"the knob rear face is {k.nut_clearance:.2f} mm from the assumed nut/washer "
         f"stack ({KNOB_NUT_AND_WASHER_STACK:.2f} mm)")
    # 4. The layout's own packaging rule: 2.50 mm from the knob body to the
    #    front-board display opening edge.
    need(k.display_clearance >= KNOB_TO_DISPLAY_MIN - EPS_MM,
         f"knob body to display opening edge is {k.display_clearance:.2f} mm, under "
         f"the layout's {KNOB_TO_DISPLAY_MIN:.2f} mm rule (left edge {k.left_edge:.2f}, "
         f"opening right edge {k.display_opening_right:.2f})")
    # 5. Retention: the grub screw must land on the shaft flat, and the pilot
    #    must be a through wall, not a blind dimple.
    need(k.grub_pilot_length >= 2.0,
         f"only {k.grub_pilot_length:.2f} mm of tapped wall behind the flat")
    # The flat is INSIDE the bore, or the screw would clamp the shaft's ROUND
    # surface - which is not what the flat is for. The screw's reach is reported
    # below as a range: a flush-only tip check cannot prove it misses the flat.
    need(k.shaft_flat_plane < k.bore_flat_plane,
         f"the shaft flat plane {k.shaft_flat_plane:.2f} mm does not sit inside the "
         f"bore's flat plane {k.bore_flat_plane:.2f} mm")
    need(k.grub_z < k.rear_z - 1.0 and k.grub_z > k.front_z + 1.0,
         f"the grub screw at Z {k.grub_z:.2f} is not inside the knob body")
    # 6. The grip must not eat the nominal OD: a full-radius land must survive
    #    on both axes, or the printed OD is smaller than the layout assumes.
    pitch = k.flute_pitch_deg
    need(k.has_land_at(k.flat_angle_deg),
         f"the flute pattern leaves no full-radius land on the flat angle "
         f"({k.flute_phase_deg:.3f} deg phase, {pitch:.3f} deg pitch)")
    for axis in (0.0, 90.0, 180.0, 270.0):
        need(k.has_land_at(axis),
             f"no full-radius land on the {axis:.0f} deg axis: the printed OD would "
             "be smaller than the layout's knob body")
    # 7. DESIGN INCOMPATIBILITY, reported and gated, never hidden: the sourced
    #    bushing shows only 0.9 mm of thread past the panel front at 10.6 mm, and
    #    the supplied nut plus washer need 2.5 mm.
    need(k.thread_proud > 0.0,
         f"the encoder bushing ends at Z {k.bushing_tip_z:.2f}, behind the panel "
         f"front Z {k.stack.panel_front_z:.2f}: nothing of the M7 thread reaches "
         "the panel front, so the panel cannot be clamped at all")
    if k.nut_stack_shortfall > 0.0:
        notices.append(
            f"BLOCKER encoder_nut_stack: the PEC11R-4015F-S0024 shows "
            f"{k.thread_proud:.2f} mm of threaded bushing in front of the panel "
            f"front ({ENC_BODY_TO_MOUNTING_SURFACE:.1f} mm board-to-shoulder + "
            f"{ENC_THREAD_LENGTH:.1f} mm LB, against the panel FRONT face datum), "
            f"but the supplied nut ({ENC_NUT_THICKNESS:.1f} "
            f"+-{ENC_NUT_THICKNESS_TOLERANCE:.1f}) plus washer "
            f"({ENC_WASHER_THICKNESS:.1f}) need {k.nut_stack:.2f} mm. Shortfall "
            f"{k.nut_stack_shortfall:.2f} mm: the panel CANNOT be nutted on this "
            "encoder at this stack. No encoder, PCB or panel change is made here; "
            f"the recorded candidate {ENC_CANDIDATE_LONGER_BUSHING['part']} "
            f"({ENC_CANDIDATE_LONGER_BUSHING['basis']}, "
            f"{ENC_CANDIDATE_LONGER_BUSHING['would_give']}) is UNVERIFIED and NOT "
            "selected until the owner approves a remedy")
    # 8. The D-shaped bore must pass the ROUND part of the shaft, and the knob
    #    must have room for the declared centre travel before it meets hardware.
    enc = k.encoder
    if enc["d_bore_blocks_round_shaft"] and enc["round_shaft_into_d_bore_mm"] > 0.0:
        notices.append(
            f"BLOCKER knob_d_bore: the bore's flat is at {k.bore_flat_plane:.2f} mm "
            f"from the axis, but the shaft is ROUND (R{enc['shaft_round_radius_mm']:.2f}) "
            f"from the shoulder up to {enc['flat_start_above_pcb_mm']:.2f} mm above the "
            f"PCB, and the knob rear face is at {enc['knob_rear_above_pcb_mm']:.2f} mm. "
            f"So {enc['round_shaft_into_d_bore_mm']:.2f} mm of ROUND shaft has to pass "
            f"the D-section, which it cannot: "
            f"{enc['d_bore_round_interference_mm']:.2f} mm radial interference. The "
            "part as drawn cannot be assembled onto the shaft. NO knob, encoder or "
            "stack change is made here - the remedy is the owner's decision")
    if enc["push_shortfall_nominal_mm"] > 0.0:
        notices.append(
            f"BLOCKER control_travel: the knob rear face is "
            f"{enc['knob_clearance_nominal_mm']:.2f} mm above the supplied nut + "
            f"washer (nominal {enc['nut_and_washer_nominal_mm']:.2f}, worst case "
            f"{enc['nut_and_washer_worst_mm']:.2f} with the page-3 tolerances), but the "
            f"encoder's centre-push travel is {enc['push_travel_mm']:.2f} "
            f"+-{enc['push_travel_tolerance_mm']:.2f} mm. Nominal shortfall "
            f"{enc['push_shortfall_nominal_mm']:.2f} mm, worst case "
            f"{enc['push_shortfall_worst_mm']:.2f} mm: the FULL travel is not "
            "guaranteed to be available. This does NOT say the switch will or will "
            "not actuate - the actuation position and the hardware tolerances must "
            "be measured")
    # 9. The remaining physical gates stay visible.
    notices.append(
        f"SOURCED supplied nut/washer stack {KNOB_NUT_AND_WASHER_STACK:.2f} "
        f"+-{ENC_NUT_THICKNESS_TOLERANCE:.2f} mm (nut {ENC_NUT_THICKNESS:.1f} + washer "
        f"{ENC_WASHER_THICKNESS:.1f}, datasheet page 3): the knob rear face sits "
        f"{k.rear_clearance:.2f} mm in front of the panel front on an ASSUMED "
        f"{KNOB_REAR_AIR:.2f} mm of air above that stack, and the shaft tip ends "
        f"{k.tip_cover:.2f} mm BELOW THE TOP OF A THROUGH BORE (the bore is cut "
        "through, so no material covers the tip). The knob geometry is PROVISIONAL "
        "until the stack above the nut is settled")
    screw = k.set_screw
    notices.append(
        f"set screw REACH (unverified clamp): reach required "
        f"{screw['reach_required_mm']:.2f} mm (bore flat {screw['bore_flat_plane_mm']:.2f} "
        f"-> shaft flat {screw['shaft_flat_plane_mm']:.2f}); an "
        f"{screw['screw_length_mm']:.0f} mm screw flush at the OD reaches "
        f"{screw['flush_protrusion_mm']:+.2f} mm past the bore flat, so it "
        f"{screw['status']}. Turning it IN leaves the outer end "
        f"{screw['outer_end_below_od_mm']:.2f} mm below the OD with "
        f"{screw['engagement_if_recessed_mm']:.2f} mm of plastic thread engaged. "
        f"Key access: {screw['key_access']}. No clamp force, thread strength or "
        "retention is claimed")
    notices.append(
        f"UNVERIFIED shaft clocking: the bore flat and the M3 grub screw are placed "
        f"at {k.flat_angle_deg:.0f} deg (the flat facing +X) because the encoder's "
        "rotation on the front board is not in the sources")
    notices.append(
        "PROVISIONAL: the knob's 2.50 mm display clearance is a plan-view rule from "
        "lib/layout.ts; the knob itself is entirely in front of the panel (Z "
        f"{k.front_z:.2f}..{k.rear_z:.2f}) and cannot touch the module")
    return notices


def describe_knob(k: Knob) -> list[str]:
    """The knob's dimension table, as printed by the generator."""
    return [
        f"encoder: board-to-shoulder {ENC_BODY_TO_MOUNTING_SURFACE:.1f} -> shoulder Z "
        f"{k.bushing_shoulder_z:.2f}; LB {ENC_THREAD_LENGTH:.1f} threaded bushing -> "
        f"tip Z {k.bushing_tip_z:.2f}, i.e. {k.thread_proud:.2f} mm past the panel "
        f"front (the supplied nut + washer need {k.nut_stack:.2f} mm: shortfall "
        f"{k.nut_stack_shortfall:.2f} mm)",
        f"shaft: L {k.shaft_length:.1f} from the shoulder (not from the bushing end), "
        f"D{k.shaft_diameter:.2f}, tip Z {k.shaft_tip_z:.2f} "
        f"({k.shaft_proud:.2f} mm proud), flat {k.shaft_flat_depth:.2f} deep -> "
        f"plane {k.shaft_flat_plane:.2f} from the axis",
        f"knob body: D{k.body_diameter:.2f} x {k.height:.2f} tall, Z "
        f"{k.front_z:.2f}..{k.rear_z:.2f}  [D and column from lib/layout.ts]",
        f"bore: D{k.bore_diameter:.2f} through, flat plane {k.bore_flat_plane:.2f} "
        f"from the axis at {k.flat_angle_deg:.0f} deg, wall {k.wall:.2f} mm",
        f"shaft tip cover: {k.tip_cover:.2f} mm inside the front face; rear face "
        f"{k.rear_clearance:.2f} mm in front of the panel, "
        f"{k.nut_clearance:.2f} mm past the assumed nut/washer",
        f"grip: {k.flute_count} x R{k.flute_radius:.2f} flutes {k.flute_depth:.2f} "
        f"deep (cutter centres on R{k.flute_cutter_radius:.2f})",
        f"grub screw: {KNOB_GRUB_SCREW} in a D{k.grub_pilot_diameter:.2f} pilot "
        f"{k.grub_pilot_length:.2f} long at Z {k.grub_z:.2f}",
        f"display opening right edge {k.display_opening_right:.2f} "
        f"[lib/layout.ts]: knob left edge {k.left_edge:.2f}, clearance "
        f"{k.display_clearance:.2f} mm (rule {KNOB_TO_DISPLAY_MIN:.2f})",
    ]


# ---------------------------------------------------------------------------
# Part 3: printed B3F-4055 button cap (SW1-SW7)
# ---------------------------------------------------------------------------
def button_apertures(layout: dict, cutout_square: float) -> tuple[Rect, ...]:
    """Every panel opening that takes the printed B3F cap, in aperture order.

    They are the rectangular ``* BUTTON`` entries in the export. A future panel
    opening of another size is a contradiction, not something to filter out:
    every SW1-SW7 cap is the same part.
    """
    out: list[Rect] = []
    for entry in layout["panelApertures"]:
        name = str(entry.get("name", ""))
        if entry.get("shape") != "rect" or not name.endswith("BUTTON"):
            continue
        width, height = float(entry["width"]), float(entry["height"])
        need(abs(width - cutout_square) <= EPS_MM and abs(height - cutout_square) <= EPS_MM,
             f"panel opening {name} is {width:.2f} x {height:.2f} mm but "
             f"PANEL_CUTOUTS.button is {cutout_square:.2f} mm square: the printed "
             "cap cannot fit every switch opening")
        out.append(Rect(float(entry["x"]), float(entry["y"]), width, height, name))
    return tuple(out)


@dataclass(frozen=True)
class ButtonCap:
    """B3F-4055 button-cap inputs. Derived planes and fits are properties.

    The part is a flat plate on the switch plunger with a square locator recess
    in its rear face. It has NO skirt: the 12.25 mm panel opening is only
    0.125 mm per side larger than the 12.0 mm switch body, so a skirt could not
    pass the panel and could not be assembled. The same geometry is printed
    seven times; the seven panel openings are read from the export.
    """

    stack: PanelStack
    cutout_square: float
    switch_height_above_board: float
    switch_height_tolerance: float
    switch_body_square: float
    switch_body_square_tolerance: float
    plunger_square: float
    plunger_square_tolerance: float
    plate_square: float
    locator_square: float
    locator_depth: float
    proud_above_panel_front: float
    plunger_face_z: float
    apertures: tuple[Rect, ...]

    @property
    def count(self) -> int:
        return len(self.apertures)

    @property
    def top_z(self) -> float:
        """The cap's finger face (panel Z)."""
        return self.stack.panel_front_z - self.proud_above_panel_front

    @property
    def rear_face_z(self) -> float:
        """The cap's switch-side face, locatorDepth behind the plunger face."""
        return self.plunger_face_z + self.locator_depth

    @property
    def height(self) -> float:
        return self.rear_face_z - self.top_z

    @property
    def top_above_board(self) -> float:
        return self.stack.board_top_z - self.top_z

    @property
    def switch_top_to_panel_front(self) -> float:
        """The flush reach the cap must beat to be pressable (mm)."""
        return self.stack.board_top_z - self.switch_height_above_board - self.stack.panel_front_z

    @property
    def cap_extension_above_switch(self) -> float:
        """Cap top above the B3F-4055 plunger face (mm)."""
        return self.top_above_board - self.switch_height_above_board

    @property
    def three_term_height(self) -> float:
        """flush + locator + proud: the derivation the layout test asserts."""
        return ((self.plunger_face_z - self.stack.panel_front_z)
                + self.locator_depth + self.proud_above_panel_front)

    @property
    def clearance_per_side(self) -> float:
        return (self.cutout_square - self.plate_square) / 2

    @property
    def locator_clearance_per_side(self) -> float:
        return (self.locator_square - self.plunger_square) / 2

    @property
    def recess_wall(self) -> float:
        return (self.plate_square - self.locator_square) / 2

    @property
    def plate_above_recess(self) -> float:
        """Plate material between the recess floor and the cap top (mm)."""
        return self.plunger_face_z - self.top_z

    @property
    def body_top_z(self) -> float:
        """The B3F-4@@5 FIXED body top, panel Z (page 6: 4.3 +-0.2 above the board).

        The body top IS dimensioned; the MOVING COLLAR around the plunger is not.
        """
        return round(self.stack.board_top_z - B3F_BODY_HEIGHT, 3)

    @property
    def ring_to_body_rest(self) -> float:
        """Locator ring (cap rear face) to the fixed body top, at rest (mm)."""
        return round(self.body_top_z - self.rear_face_z, 3)

    @property
    def ring_to_body_at_flush(self) -> float:
        """The same clearance after the full press to flush with the panel (mm)."""
        return round(self.ring_to_body_rest - self.press_to_flush, 3)

    @property
    def pretravel_to_operate(self) -> float:
        """The B3F-4@@5 pretravel range to the operating point (mm)."""
        return round(B3F_PRETRAVEL + B3F_PRETRAVEL_PLUS, 3)

    @property
    def press_to_flush(self) -> float:
        """Cap travel available before its top reaches the panel FRONT face (mm).

        The cap top stands ``proud`` above the panel front, and the plate is wider
        than the 12.25 mm cutout, so a finger cannot push it past flush: this is
        the whole travel budget the cap can deliver, whatever the switch allows.
        """
        return self.proud_above_panel_front

    @property
    def press_margin(self) -> float:
        """Travel left after the plunger reaches its operating point (mm).

        Positive is a design margin, NOT a clearance proof: the 0.60 mm locator
        ring's clearance to the switch body top and the switch's overtravel are
        not dimensioned on the recorded page.
        """
        return round(self.press_to_flush - self.pretravel_to_operate, 3)

    @property
    def volume(self) -> float:
        """Exact volume of a plain plate minus a plain recess (mm^3)."""
        return (self.plate_square ** 2 * self.height
                - self.locator_square ** 2 * self.locator_depth)

    @property
    def envelope(self) -> "Envelope":
        """Intended solid envelope, centred on the origin (mm)."""
        r = self.plate_square / 2
        return Envelope(x=(-r, r), y=(-r, r), z=(self.top_z, self.rear_face_z))


def button_cap(layout: dict | None = None,
               source: str | LayoutSource | None = None) -> ButtonCap:
    """Derive the printed B3F cap from the sources. Raises rather than guessing."""
    data = layout if layout is not None else load_layout()
    src = source if isinstance(source, LayoutSource) else layout_source(source)
    stack = panel_stack(data)
    bc = src.button_cap

    need(bc.implemented,
         "BUTTON_CAP_PRINTED.implemented is false: the printed B3F cap is still "
         "design intent in lib/layout.ts, so it must not be exported as a part")

    # The datasheet constants live here and in lib/layout.ts BUTTON_SWITCH. One
    # spelling, two carriers: a disagreement is a bug, not a tolerance.
    for name, got, want in (
        ("heightAboveBoard", bc.switch_height_above_board, B3F_HEIGHT_ABOVE_BOARD),
        ("heightTolerance", bc.switch_height_tolerance, B3F_HEIGHT_TOLERANCE),
        ("bodySquare", bc.switch_body_square, B3F_BODY_SQUARE),
        ("bodySquareTolerance", bc.switch_body_square_tolerance, B3F_BODY_SQUARE_TOLERANCE),
        ("plungerSquare", bc.switch_plunger_square, B3F_PLUNGER_SQUARE),
        ("plungerSquareTolerance", bc.switch_plunger_square_tolerance,
         B3F_PLUNGER_SQUARE_TOLERANCE),
    ):
        need(abs(got - want) <= EPS_MM,
             f"lib/layout.ts BUTTON_SWITCH.{name} is {got} but the B3F datasheet "
             f"constant in printed_geometry.py is {want}: fix one of them")

    # The cap's plate and locator are DERIVED from the cutout/plunger and the
    # declared clearances, then compared with the source comments.
    plate = bc.cutout_square - 2 * bc.clearance_to_cutout_per_side
    need(abs(plate - bc.square) <= EPS_MM,
         f"PANEL_CUTOUTS.button {bc.cutout_square:.2f} - 2 x "
         f"{bc.clearance_to_cutout_per_side:.3f} gives a {plate:.2f} plate but "
         f"BUTTON_CAP_PRINTED.square documents {bc.square:.2f}")
    locator = B3F_PLUNGER_SQUARE + 2 * bc.locator_clearance_per_side
    need(abs(locator - bc.locator_square) <= EPS_MM,
         f"B3F plunger {B3F_PLUNGER_SQUARE:.2f} + 2 x "
         f"{bc.locator_clearance_per_side:.3f} gives a {locator:.2f} locator but "
         f"BUTTON_CAP_PRINTED.locatorSquare documents {bc.locator_square:.2f}")

    plunger_face = stack.board_top_z - B3F_HEIGHT_ABOVE_BOARD
    top = stack.panel_front_z - bc.proud_above_panel_front
    rear = plunger_face + bc.locator_depth
    height = rear - top
    extension = (stack.board_top_z - top) - B3F_HEIGHT_ABOVE_BOARD
    for label, derived, declared in (
        ("undersideZ", plunger_face, bc.underside_z),
        ("locatorFloorZ", plunger_face, bc.locator_floor_z),
        ("topZ", top, bc.top_z),
        ("rearFaceZ", rear, bc.rear_face_z),
        ("height", height, bc.height),
        ("capExtensionAboveSwitch", extension, bc.cap_extension_above_switch),
    ):
        need(abs(derived - declared) <= EPS_MM,
             f"BUTTON_CAP_PRINTED.{label} is {declared:.3f} mm but the panel stack "
             f"({stack.panel_front_z:.2f}/{stack.panel_rear_z:.2f}/{stack.board_top_z:.2f}) "
             f"and the B3F datasheet ({B3F_HEIGHT_ABOVE_BOARD:.2f} +-"
             f"{B3F_HEIGHT_TOLERANCE:.2f} above the board) give {derived:.3f} mm: "
             "update lib/layout.ts or the datasheet constants, they must agree")

    apertures = button_apertures(data, bc.cutout_square)
    need(len(apertures) == 7,
         f"expected 7 B3F-4055 button openings (SW1-SW7), found {len(apertures)} "
         "in mechanical/layout.json: update the printed-cap quantity and BOM "
         "together, not the geometry")

    return ButtonCap(
        stack=stack,
        cutout_square=bc.cutout_square,
        switch_height_above_board=B3F_HEIGHT_ABOVE_BOARD,
        switch_height_tolerance=B3F_HEIGHT_TOLERANCE,
        switch_body_square=B3F_BODY_SQUARE,
        switch_body_square_tolerance=B3F_BODY_SQUARE_TOLERANCE,
        plunger_square=B3F_PLUNGER_SQUARE,
        plunger_square_tolerance=B3F_PLUNGER_SQUARE_TOLERANCE,
        plate_square=plate,
        locator_square=locator,
        locator_depth=bc.locator_depth,
        proud_above_panel_front=bc.proud_above_panel_front,
        plunger_face_z=plunger_face,
        apertures=apertures,
    )


def validate_button_cap(g: ButtonCap, layout: dict | None = None) -> list[str]:
    """Check every printed B3F cap rule. Returns notices; raises on failure."""
    notices: list[str] = []
    data = layout if layout is not None else load_layout()

    # 1. Seven switches, one geometry: every opening is the same 12.25 square.
    need(g.count == 7,
         f"expected 7 B3F-4055 button openings (SW1-SW7), found {g.count}")
    apertures = button_apertures(data, g.cutout_square)
    need(len(apertures) == g.count,
         f"mechanical/layout.json has {len(apertures)} button openings but the "
         f"cap geometry was built for {g.count}")
    for ap in g.apertures:
        need(abs(ap.width - g.cutout_square) <= EPS_MM
             and abs(ap.height - g.cutout_square) <= EPS_MM,
             f"panel opening {ap.name} is {ap.width:.2f} x {ap.height:.2f} mm, not "
             f"the {g.cutout_square:.2f} mm square the cap is designed for")

    # 2. The plate must pass the panel cutout and stay inside the switch body.
    need(g.plate_square < g.cutout_square,
         f"plate {g.plate_square:.2f} does not fit the {g.cutout_square:.2f} cutout")
    need(g.clearance_per_side > 0.0,
         f"plate {g.plate_square:.2f} has no clearance in the "
         f"{g.cutout_square:.2f} cutout")
    need(abs(g.plate_square + 2 * g.clearance_per_side - g.cutout_square) <= EPS_MM,
         f"plate {g.plate_square:.2f} + 2 x {g.clearance_per_side:.3f} is not the "
         f"{g.cutout_square:.2f} cutout")
    need(g.plate_square <= g.switch_body_square,
         f"plate {g.plate_square:.2f} is wider than the "
         f"{g.switch_body_square:.2f} switch body: the panel cutout is only "
         "0.125 mm per side larger than the body, so a plate or skirt outside "
         "the body cannot be assembled")

    # 3. The locator must clear the plunger and leave printable walls.
    need(g.locator_square > g.plunger_square,
         f"locator {g.locator_square:.2f} does not clear the "
         f"{g.plunger_square:.2f} plunger")
    need(g.locator_clearance_per_side > 0.0,
         f"locator {g.locator_square:.2f} has no clearance on the "
         f"{g.plunger_square:.2f} plunger")
    need(g.recess_wall >= PRINT_MIN_WALL,
         f"recess wall {g.recess_wall:.2f} mm is under the {PRINT_MIN_WALL} mm rule")
    need(g.plate_above_recess >= PRINT_MIN_WALL,
         f"plate above the recess is {g.plate_above_recess:.2f} mm, under the "
         f"{PRINT_MIN_WALL} mm rule")

    # 4. The recess floor is the plunger face; the cap presses the switch.
    switch_top = g.stack.board_top_z - g.switch_height_above_board
    need(abs(g.plunger_face_z - switch_top) <= EPS_MM,
         f"the recess floor is at Z {g.plunger_face_z:.2f} but the B3F-4055 "
         f"plunger face is at Z {switch_top:.2f}")
    need(g.plunger_face_z > g.stack.panel_front_z,
         "the plunger face is in front of the panel front")
    need(g.plunger_face_z < g.stack.board_top_z,
         "the plunger face is behind the front board")

    # 5. The cap must stand PROUD and the part is the three-term sum, not two.
    need(g.top_z < g.stack.panel_front_z,
         f"cap top Z {g.top_z:.2f} does not stand proud of the panel front "
         f"(Z {g.stack.panel_front_z:.2f})")
    need(g.proud_above_panel_front > 0.0,
         f"proud height {g.proud_above_panel_front:.2f} mm is not positive")
    need(g.cap_extension_above_switch >= g.switch_top_to_panel_front - EPS_MM,
         f"cap top is {g.cap_extension_above_switch:.2f} mm above the switch, "
         f"under the {g.switch_top_to_panel_front:.2f} mm flush reach: the finger "
         "would find the cutout, not the cap")
    need(abs(g.height - g.three_term_height) <= EPS_MM,
         f"part height {g.height:.2f} mm is not flush + locator + proud "
         f"({g.three_term_height:.2f} mm): the locator recess makes the part "
         "taller than the distance it spans")
    need(g.locator_depth > 0.0,
         f"locator depth {g.locator_depth:.2f} mm is not positive")

    # 6. Nothing hangs below the cap rear face into the switch body.
    need(g.rear_face_z > g.plunger_face_z,
         f"cap rear face Z {g.rear_face_z:.2f} is in front of the plunger face "
         f"Z {g.plunger_face_z:.2f}: the recess opens the wrong way")
    need(g.rear_face_z < g.stack.board_top_z,
         f"cap rear face Z {g.rear_face_z:.2f} reaches the board top "
         f"Z {g.stack.board_top_z:.2f}")
    need(abs(g.rear_face_z - (g.plunger_face_z + g.locator_depth)) <= EPS_MM,
         f"cap rear face Z {g.rear_face_z:.2f} is not the plunger face plus the "
         f"{g.locator_depth:.2f} locator depth")

    # 7. Physical gates the model cannot settle stay visible.
    notices.append(
        f"locator-ring clearance, NOMINAL: the {g.locator_depth:.2f} mm ring reaches "
        f"Z {g.rear_face_z:.2f}; the B3F-4@@5 fixed body top is at Z "
        f"{g.body_top_z:.2f} (page 6: body {B3F_BODY_HEIGHT:.1f} +-"
        f"{B3F_BODY_HEIGHT_TOLERANCE:.1f} mm above the board), so the ring clears the "
        f"body by {g.ring_to_body_rest:.2f} mm at rest and "
        f"{g.ring_to_body_at_flush:.2f} mm at flush, i.e. after "
        f"{g.press_to_flush:.2f} mm of cap movement (full physical travel is NOT "
        "established). The MOVING COLLAR "
        "around the plunger and the overtravel are NOT dimensioned, so this is a "
        "nominal budget and not a proven clearance")
    notices.append(
        f"PROVISIONAL: the cap is located by a {g.locator_square:.2f} mm recess on "
        f"the {g.plunger_square:.2f} +-{g.plunger_square_tolerance:.2f} mm plunger "
        f"({g.locator_clearance_per_side:.3f} mm per side nominal). There is no snap "
        "or skirt, so axial retention is the printed fit alone: check that it stays "
        "on the plunger when the panel is lifted and does not rattle. FDM holes "
        "usually print undersize - measure before changing the recess")
    notices.append(
        f"PROVISIONAL: the cap top is {g.proud_above_panel_front:.2f} mm proud of "
        f"the panel front by design (Z {g.top_z:.2f}, {g.top_above_board:.2f} above "
        "the board). `control_actuator` is PENDING: a design budget is not a "
        "measurement, and the first article must record the installed cap height, "
        "the retention and the press travel")
    # 8. The press budget, from the one sourced number (page 3 pretravel).
    notices.append(
        f"press budget: the cap top is {g.press_to_flush:.2f} mm proud of the panel "
        f"front, so it can travel {g.press_to_flush:.2f} mm before it is flush; the "
        f"B3F-4@@5 pretravel to the operating point is {B3F_PRETRAVEL:.1f} "
        f"+{B3F_PRETRAVEL_PLUS:.1f}/-{B3F_PRETRAVEL_MINUS:.1f} mm, i.e. "
        f"{pretravel_band()[0]:.2f}-{pretravel_band()[1]:.2f} mm (page 3, 'Operating "
        f"Characteristics'). Nominal margin {g.press_margin:.2f} mm. This is a "
        "DESIGN budget, not a clearance proof: overtravel and total travel are not "
        "published there, the moving collar is undimensioned, and the ring-to-body "
        "numbers above are nominal")
    notices.append(
        f"{g.count} off, one per B3F-4055 (SW1-SW7). The printed cap is the "
        "STANDARD; the bought Omron B32-13x0 is the documented alternative, whose "
        "reference 10.0 +-0.4 is PCB top -> CAP TOP (verified), so a bought cap top "
        "would sit 0.60 mm behind this panel front rather than 1.0-2.0 mm over it. "
        "No JLCPCB/SMT BOM row is added for a panel accessory")
    return notices


def pretravel_band() -> tuple[float, float]:
    """The published B3F-4@@5 pretravel band to the operating point (mm)."""
    return (round(B3F_PRETRAVEL - B3F_PRETRAVEL_MINUS, 3),
            round(B3F_PRETRAVEL + B3F_PRETRAVEL_PLUS, 3))


def describe_button_cap(g: ButtonCap) -> list[str]:
    """The printed B3F cap's dimension table, as printed by the generator."""
    return [
        f"panel stack: front Z {g.stack.panel_front_z:.2f}, rear Z "
        f"{g.stack.panel_rear_z:.2f}, board top Z {g.stack.board_top_z:.2f}",
        f"button openings: {g.count} x {g.cutout_square:.2f} mm square "
        "[mechanical/layout.json panelApertures]",
        f"B3F-4055: body {g.switch_body_square:.2f} +-"
        f"{g.switch_body_square_tolerance:.2f}, plunger {g.plunger_square:.2f} +-"
        f"{g.plunger_square_tolerance:.2f}, {g.switch_height_above_board:.2f} +-"
        f"{g.switch_height_tolerance:.2f} above the board; plunger face Z "
        f"{g.plunger_face_z:.2f}",
        f"plate: {g.plate_square:.2f} x {g.plate_square:.2f} x {g.height:.2f} tall "
        f"(Z {g.top_z:.2f}..{g.rear_face_z:.2f}), {g.proud_above_panel_front:.2f} "
        f"proud, clearance {g.clearance_per_side:.3f} per side in the "
        f"{g.cutout_square:.2f} cutout",
        f"locator: {g.locator_square:.2f} x {g.locator_square:.2f} x "
        f"{g.locator_depth:.2f} deep, floor Z {g.plunger_face_z:.2f}, rear face Z "
        f"{g.rear_face_z:.2f}, clearance {g.locator_clearance_per_side:.3f} per "
        f"side on the {g.plunger_square:.2f} plunger",
        f"height: {g.three_term_height:.2f} = "
        f"{g.plunger_face_z - g.stack.panel_front_z:.2f} flush + "
        f"{g.locator_depth:.2f} locator + {g.proud_above_panel_front:.2f} proud; "
        f"cap top {g.top_above_board:.2f} above the board = "
        f"{g.cap_extension_above_switch:.2f} above the switch",
        f"volume: {g.volume:.1f} mm^3 (plate - locator recess); {g.count} off",
    ]


# ---------------------------------------------------------------------------
# STL inspection: watertight, single shell, envelope, volume
# ---------------------------------------------------------------------------
# Binary STL: 80-byte header, uint32 triangle count, then 50 bytes per triangle.
_STL_HEADER = 80
_STL_TRIANGLE = 50
# Vertex quantisation for edge hashing: 1e-5 mm, far coarser than a float32
# coordinate's 2e-6 mm resolution and far finer than any tessellation spacing.
_VERTEX_SCALE = 100000.0


@dataclass(frozen=True)
class Envelope:
    """An axis-aligned box in panel coordinates (mm)."""

    x: tuple[float, float]
    y: tuple[float, float]
    z: tuple[float, float]

    def mismatches(self, bbox: tuple[float, float, float, float, float, float],
                   tolerance: float) -> list[str]:
        names = ("x", "y", "z")
        out: list[str] = []
        for i, name in enumerate(names):
            low, high = bbox[2 * i], bbox[2 * i + 1]
            for label, got, want in (("min", low, getattr(self, name)[0]),
                                     ("max", high, getattr(self, name)[1])):
                if abs(got - want) > tolerance:
                    out.append(f"{name} {label} is {got:.4f} mm, intended {want:.4f} mm "
                               f"(off by {got - want:+.4f} mm)")
        return out

    def lines(self) -> str:
        return (f"x {self.x[0]:.2f}..{self.x[1]:.2f}, "
                f"y {self.y[0]:.2f}..{self.y[1]:.2f}, "
                f"z {self.z[0]:.2f}..{self.z[1]:.2f}")


@dataclass(frozen=True)
class StlReport:
    """What the reload of an exported STL says about the mesh."""

    path: Path
    triangles: int
    vertices: int
    bbox: tuple[float, float, float, float, float, float]
    open_edges: int
    non_manifold_edges: int
    repeated_directed_edges: int
    shells: int
    volume: float

    def lines(self) -> list[str]:
        return [
            f"{self.path.name}: {self.triangles} triangles, {self.vertices} vertices, "
            f"{self.shells} shell(s)",
            f"  bbox x {self.bbox[0]:.3f}..{self.bbox[1]:.3f}, "
            f"y {self.bbox[2]:.3f}..{self.bbox[3]:.3f}, "
            f"z {self.bbox[4]:.3f}..{self.bbox[5]:.3f}",
            f"  edges: {self.open_edges} open, {self.non_manifold_edges} non-manifold, "
            f"{self.repeated_directed_edges} repeated directed -> "
            f"{'watertight' if self.open_edges == 0 and self.non_manifold_edges == 0 else 'NOT watertight'}",
            f"  mesh volume {self.volume:.1f} mm^3",
        ]


def read_stl(path: Path | str) -> list[tuple[tuple[float, float, float], ...]]:
    """Read a binary STL (what CadQuery writes) into a list of triangles."""
    p = Path(path)
    if not p.is_file():
        raise GeometryError(f"{p} is missing: build it with {p.stem.split('-')[0]}.py")
    data = p.read_bytes()
    if len(data) < _STL_HEADER + 4:
        raise GeometryError(f"{p} is too short to be an STL ({len(data)} bytes)")
    count = struct.unpack_from("<I", data, _STL_HEADER)[0]
    expected = _STL_HEADER + 4 + _STL_TRIANGLE * count
    if len(data) != expected:
        raise GeometryError(
            f"{p} is not a binary STL: {len(data)} bytes, but {count} triangles need "
            f"{expected}. Export with CadQuery's binary STL writer.")
    triangles = []
    for i in range(count):
        values = struct.unpack_from("<12f", data, _STL_HEADER + 4 + _STL_TRIANGLE * i)
        triangles.append((values[3:6], values[6:9], values[9:12]))
    return triangles


def _key(vertex: tuple[float, float, float]) -> tuple[int, int, int]:
    return (int(round(vertex[0] * _VERTEX_SCALE)),
            int(round(vertex[1] * _VERTEX_SCALE)),
            int(round(vertex[2] * _VERTEX_SCALE)))


def inspect_stl(path: Path | str) -> StlReport:
    """Reload an STL and report manifoldness, shells, envelope and volume."""
    triangles = read_stl(path)
    if not triangles:
        raise GeometryError(f"{path} holds no triangles")

    vertices: dict[tuple[int, int, int], None] = {}
    bbox = [math.inf, -math.inf, math.inf, -math.inf, math.inf, -math.inf]
    volume = 0.0
    undirected: dict[tuple[tuple[int, int, int], tuple[int, int, int]], int] = {}
    directed: dict[tuple[tuple[int, int, int], tuple[int, int, int]], int] = {}
    parent: dict[int, int] = {}

    def find(i: int) -> int:
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i

    def union(a: int, b: int) -> None:
        ra, rb = find(a), find(b)
        if ra != rb:
            parent[rb] = ra

    for index, tri in enumerate(triangles):
        parent[index] = index
        keys = []
        for vertex in tri:
            key = _key(vertex)
            vertices[key] = None
            keys.append(key)
            for axis in range(3):
                bbox[2 * axis] = min(bbox[2 * axis], vertex[axis])
                bbox[2 * axis + 1] = max(bbox[2 * axis + 1], vertex[axis])
        a, b, c = (tri[0], tri[1], tri[2])
        volume += (a[0] * (b[1] * c[2] - b[2] * c[1])
                   - a[1] * (b[0] * c[2] - b[2] * c[0])
                   + a[2] * (b[0] * c[1] - b[1] * c[0])) / 6.0
        for u, v in ((keys[0], keys[1]), (keys[1], keys[2]), (keys[2], keys[0])):
            undirected[(u, v) if u <= v else (v, u)] = \
                undirected.get((u, v) if u <= v else (v, u), 0) + 1
            directed[(u, v)] = directed.get((u, v), 0) + 1

    # Shells: triangles that share an edge belong to the same connected piece.
    edge_owner: dict[tuple[tuple[int, int, int], tuple[int, int, int]], int] = {}
    for index, tri in enumerate(triangles):
        keys = [_key(v) for v in tri]
        for u, v in ((keys[0], keys[1]), (keys[1], keys[2]), (keys[2], keys[0])):
            edge = (u, v) if u <= v else (v, u)
            if edge in edge_owner:
                union(index, edge_owner[edge])
            else:
                edge_owner[edge] = index
    shells = len({find(i) for i in range(len(triangles))})

    return StlReport(
        path=Path(path),
        triangles=len(triangles),
        vertices=len(vertices),
        bbox=(bbox[0], bbox[1], bbox[2], bbox[3], bbox[4], bbox[5]),
        open_edges=sum(1 for n in undirected.values() if n == 1),
        non_manifold_edges=sum(1 for n in undirected.values() if n > 2),
        repeated_directed_edges=sum(1 for n in directed.values() if n > 1),
        shells=shells,
        volume=volume,
    )


def verify_stl(path: Path | str, envelope: Envelope,
               volume: float | None = None,
               bbox_tolerance: float = 0.01,
               volume_tolerance: float = 0.01) -> StlReport:
    """Reload an exported STL and prove it is the part it claims to be."""
    report = inspect_stl(path)
    need(report.open_edges == 0,
         f"{report.path.name} is not watertight: {report.open_edges} edges are used "
         "by one triangle only")
    need(report.non_manifold_edges == 0,
         f"{report.path.name} is not manifold: {report.non_manifold_edges} edges are "
         "shared by three or more triangles")
    need(report.repeated_directed_edges == 0,
         f"{report.path.name} has {report.repeated_directed_edges} repeated directed "
         "edges: inconsistent triangle orientation")
    need(report.shells == 1,
         f"{report.path.name} holds {report.shells} shells, expected exactly 1")
    problems = envelope.mismatches(report.bbox, bbox_tolerance)
    need(not problems,
         f"{report.path.name} does not match the intended envelope "
         f"({envelope.lines()}): " + "; ".join(problems))
    if volume is not None:
        need(abs(report.volume - volume) <= max(volume_tolerance * volume, 0.05),
             f"{report.path.name} mesh encloses {report.volume:.1f} mm^3, the solid "
             f"{volume:.1f} mm^3 "
             f"({100.0 * abs(report.volume - volume) / volume:.2f}% off)")
    return report


# ---------------------------------------------------------------------------
# Solid self-checks. These take the CadQuery module as an argument, so this
# module stays importable (and testable) without a CAD kernel; nav_keytop.py
# and knob.py share them instead of each carrying a copy.
# ---------------------------------------------------------------------------
# A cut that removes less than this is a defect, not a boolean result (mm^3).
VOLUME_EPS = 1e-6


@dataclass(frozen=True)
class SolidReport:
    """What the built solid says about itself, before anything is exported."""

    volume: float
    bbox: tuple[float, float, float, float, float, float]
    solids: int
    shells: int
    removed: tuple[tuple[str, float], ...]

    def lines(self) -> list[str]:
        out = [f"solid: {self.solids} body, {self.shells} shell, volume "
               f"{self.volume:.1f} mm^3",
               f"  bbox x {self.bbox[0]:.3f}..{self.bbox[1]:.3f}, "
               f"y {self.bbox[2]:.3f}..{self.bbox[3]:.3f}, "
               f"z {self.bbox[4]:.3f}..{self.bbox[5]:.3f}"]
        out.extend(f"  cut {name}: removed {removed:.2f} mm^3"
                   for name, removed in self.removed)
        return out


def apply_cuts(cq, stock, features: list[tuple[str, object]]):
    """Cut every named feature and prove that each one removed material."""
    removed: list[tuple[str, float]] = []
    for name, tool in features:
        taken = stock.val().intersect(tool.val()).Volume()
        need(taken > VOLUME_EPS,
             f"cut '{name}' removes nothing from the stock ({taken:.9f} mm^3): "
             "the feature is outside the material")
        before = stock.val().Volume()
        stock = stock.cut(tool)
        after = stock.val().Volume()
        need(before - after > VOLUME_EPS,
             f"cut '{name}' did not reduce the volume ({before:.3f} -> {after:.3f} mm^3)")
        removed.append((name, before - after))
    for name, tool in features:
        left = stock.val().intersect(tool.val()).Volume()
        need(left <= VOLUME_EPS,
             f"material left in '{name}' after the cut ({left:.6f} mm^3)")
    return stock, tuple(removed)


def check_solid(cq, body, envelope: Envelope, removed: tuple[tuple[str, float], ...] = (),
                bbox_tolerance: float = 0.01) -> SolidReport:
    """Prove the built solid is one valid, watertight body of the intended size."""
    solids = body.solids().size()
    need(solids == 1, f"the part is {solids} separate solids, expected 1")
    shape = body.val()
    need(shape.isValid(), "the built solid is not a valid BRep")
    shells = len(shape.Shells())
    need(shells == 1, f"the built solid has {shells} shells, expected 1 (watertight)")
    box = shape.BoundingBox()
    bbox = (box.xmin, box.xmax, box.ymin, box.ymax, box.zmin, box.zmax)
    problems = envelope.mismatches(bbox, bbox_tolerance)
    need(not problems,
         f"the solid does not match the intended envelope ({envelope.lines()}): "
         + "; ".join(problems))
    return SolidReport(volume=shape.Volume(), bbox=bbox, solids=solids, shells=shells,
                       removed=removed)


def probe_material(cq, body, points: list[tuple[float, float, float]], want_material: bool,
                   what: str) -> None:
    """Check sampled points for material. Keeps every probe >= 0.1 mm off a face."""
    shape = body.val()
    for x, y, z in points:
        inside = shape.isInside(cq.Vector(x, y, z))
        need(inside == want_material,
             f"{what}: ({x:.2f}, {y:.2f}, {z:.2f}) is "
             f"{'material' if inside else 'void'}, expected "
             f"{'material' if want_material else 'void'}")


def export_stl_and_step(cq, body, stl_path: Path, step_path: Path,
                        tolerance: float = 0.02, angular_tolerance: float = 0.05) -> None:
    """Write both artefacts with an explicitly fine tessellation."""
    cq.exporters.export(body, str(stl_path), tolerance=tolerance,
                        angularTolerance=angular_tolerance)
    cq.exporters.export(body, str(step_path))


def verify_step(cq, path: Path | str, volume: float, tolerance: float = 1e-3) -> float:
    """Reload the STEP and prove it is the same single solid."""
    p = Path(path)
    need(p.is_file(), f"{p} was not written")
    shape = cq.importers.importStep(str(p))
    solids = shape.solids().size()
    need(solids == 1, f"{p.name} comes back as {solids} solids, expected 1")
    got = shape.val().Volume()
    need(abs(got - volume) <= max(tolerance * volume, 0.05),
         f"{p.name} encloses {got:.1f} mm^3 on re-import, the built solid "
         f"{volume:.1f} mm^3")
    return got

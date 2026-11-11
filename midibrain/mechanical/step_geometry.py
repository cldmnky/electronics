#!/usr/bin/env python3
"""Pure geometry rules for the STEP/STL verification models.

Every coordinate comes from ``mechanical/layout.json``, which is generated from
``tscircuit/lib/layout.ts`` by ``bun run scripts/export-mechanical.ts``. This
module restates no position and imports no CAD library, so every rule here is
tested on any machine:

    cd midibrain && python3 -m unittest discover -s mechanical -p 'test_*.py'

``mechanical/step_assembly.py`` is the only file that needs CadQuery; it uses
the functions here to name each solid, to state what is known about it, and to
refuse a definitive collision result where an axis is unmeasured.

THE RULE THIS MODULE EXISTS FOR
--------------------------------
Two solids can collide only if their extents intersect on all three axes. A
documented XY overlap is NOT a collision: it is a projection. The Adafruit 908
flange is 11.90 mm tall on a panel centre at y = 53.5, so it spans y
47.55 ... 59.45 while the front board's relief notch starts at y = 48. That is a
0.45 mm XY overlap. Whether the two bodies touch in 3D depends on the flange's
depth below the panel, and the vendor PDF publishes no depth. So the result is
UNDETERMINED, not clear and not a collision. There is no threshold in this file
that turns an unknown depth into a verdict.

Provenance of every number is carried on the envelope, so a candidate value used
to draw a picture can never produce a definitive fit result.
"""
from __future__ import annotations

import math
from dataclasses import dataclass
from pathlib import Path

from bracket_geometry import EPS, GeometryError, Rect, load_layout

# ---------------------------------------------------------------------------
# Provenance labels. A verdict quotes these, so a reader can see what the
# geometry rests on.
# ---------------------------------------------------------------------------
PROVENANCE = {
    "vendor-step": "vendor STEP, SHA-256 verified against mechanical/reference/*.source.json",
    "canonical": "tscircuit/lib/layout.ts via mechanical/layout.json (planning value)",
    "documented": "published drawing dimension, quoted with its source",
    "provisional-z": "PROVISIONAL_Z chain: documentary assumptions A and B, not a measurement",
    "candidate": "candidate value, chosen to draw a picture; no source states it",
    "unknown": "no value available; the axis is not modelled",
}

# The Adafruit 908 USB-A panel feedthrough, as the primary drawing states it.
# Primary source: the product's own mechanical drawing. The drawing gives the
# flange and the screw pitch and NO body depth, so `depth_mm` is None on
# purpose: a guessed depth would turn the flange fit into a fake verdict.
ADAFRUIT_908 = {
    "part": "Adafruit 908, USB A female panel-mount extension cable",
    "source_url": "https://cdn-shop.adafruit.com/product-files/908/USB+A+Female.pdf",
    "retrieved": "2026-11-08",
    "flange_width_mm": 38.30,   # along X, the long axis
    "flange_height_mm": 11.90,  # along Y
    "mount_pitch_mm": 30.0,     # X, between the two panel screw holes
    "panel_opening_width_mm": 14.9,
    "panel_opening_height_mm": 7.3,
    # NOT published: the drawing gives no flange seat plane and no body depth.
    # It stays None so no fit verdict can be built on a guessed number.
    "depth_mm": None,
}


# ---------------------------------------------------------------------------
# The vendor display model's own planes.
# ---------------------------------------------------------------------------
# Measured from the hash-verified vendor STEP with a real CAD kernel
# (mechanical/tools/inspect_display_step.py), 2026-11-08. These are MODEL
# planes: they are NOT the vendor's stated 14.05 mm maximum overall depth and
# they are NOT the provisional 10.05 + 4.00 mm design chain. The three sets of
# numbers are kept apart on purpose, because they answer different questions.
#
# Vendor frame: +Z points FORWARD out of the glass, +X points the opposite way
# to the layout frame, +Y agrees with the layout frame (proved by the inner
# plain-hole pattern insets below).
VENDOR_DISPLAY_MODEL = {
    "source": "mechanical/reference/waveshare-4.3inch-dsi-lcd.step (SHA-256 verified)",
    "tool": "mechanical/tools/inspect_display_step.py, CadQuery 2.8.0",
    "inspected": "2026-11-08",
    "solids": 394,
    "bbox_z_mm": (-9.020, 4.800),
    "bbox_xy_mm": (106.100, 68.363),
    "glass_front_z": 2.900,          # 43H-800480-IPS cover glass, front face
    "glass_rear_z": 0.000,           # glass is 2.9 mm thick in the model
    "touch_frame_front_z": 4.800,    # front touch/frame, 1.9 mm in front of the glass
    "pcb_front_z": -1.740,
    "pcb_rear_z": -3.340,
    "boss_rear_z": -7.340,           # SMTSO boss ends, 4.00 mm behind the PCB rear
    "rear_most_z": -9.020,           # rear-most modelled feature, not a boss
    "mount_pattern_mm": (99.300, 61.800),   # outer SMTSO pattern, symmetric
    "mount_pattern_centre_xy": (0.000, -2.370),  # = the PCB outline centre
    # Inner plain-hole pattern (the drawing's 25.62/22.38 and 9.50/9.46 insets).
    # It is asymmetric, so it decides the in-plane orientation of the module.
    "inner_hole_x": (-30.616, 27.384),
    "inner_hole_y": (-26.912, 22.138),
    # Where the two 21.412 x 4.826 bodies actually live. Verified by XCAF label
    # path: they are TFT flex bonds inside TFT1_ASM, NOT a panel connector.
    "tft1_flex_bonds": {
        "names": ("EXTRUDED-106739", "EXTRUDED-107488"),
        "label_path": ("4_3INCH-DSI-LCD-PCBA_ASM > 4_3INCH_DSI_LCD_V2_2_ASM > "
                       "PCB_ASM > TFT1_ASM"),
        "local_span_mm": (21.412, 4.826, (1.000, 0.800)),
        "placed_centre_xy_mm": (-24.316, 5.755),
        "note": ("an earlier revision called these the DSI FPC connector and reported a "
                 "~24 mm disagreement with the drawing. That was a misidentification"),
    },
    # The body nearest the drawing-derived connector position, by NAME only. The
    # name is not proof of a pinout, a pin count or an electrical side, and the
    # mapping is not independent evidence.
    "display_cam": {
        "name": "DISPLAY_CAM",
        "label_path": "4_3INCH-DSI-LCD_ASM > 4_3INCH-DSI-LCD-PCBA_ASM > DISPLAY_CAM",
        "x_mm": (-51.975, -47.153),
        "y_mm": (-13.570, 8.830),
        "z_mm": (-9.020, -3.340),
        "size_mm": (4.822, 22.400, 5.680),
        "status": "UNVERIFIED CANDIDATE: identity, pin count and electrical side not established",
    },
}


def vendor_model_separations() -> dict:
    """The vendor model's own separations, in mm, from its measured planes."""
    m = VENDOR_DISPLAY_MODEL
    return {
        "overall_depth_mm": round(m["bbox_z_mm"][1] - m["bbox_z_mm"][0], 3),
        "frame_forward_of_glass_mm": round(m["touch_frame_front_z"] - m["glass_front_z"], 3),
        "glass_to_pcb_rear_mm": round(m["glass_front_z"] - m["pcb_rear_z"], 3),
        "pcb_thickness_mm": round(m["pcb_front_z"] - m["pcb_rear_z"], 3),
        "pcb_rear_to_boss_rear_mm": round(m["pcb_rear_z"] - m["boss_rear_z"], 3),
        "boss_rear_to_rear_most_mm": round(m["boss_rear_z"] - m["rear_most_z"], 3),
    }


# In-plane orientation: PENDING. The Z reversal is forced (the vendor +Z points
# out of the glass, the layout +Z points rearward); the choice of in-plane axis
# is NOT settled, because a rotation that reverses Z must mirror one in-plane
# axis and the drawing's own view direction is not established.
DISPLAY_ORIENTATION = {
    "status": "PENDING",
    "candidate_used_for_drawing": "180 deg about Y: (x, y, z) -> (-x, y, -z)",
    "alternative_candidate": "180 deg about X: (x, y, z) -> (x, -y, -z)",
    "z_reversal_required": ("the vendor frame's +Z points out of the glass; the layout's "
                            "+Z points rearward, so Z must be reversed by any placement"),
    "why_not_proven": (
        "the drawing shows readable rear-side silkscreen and may be a REAR view, so moving "
        "drawing left/right straight into the front-view layout may mirror the inner-hole "
        "pattern. The layout's own DISPLAY_STANDOFFS uses that no-flip premise, so the "
        "candidate that reproduces it is consistent with the layout, not evidence about "
        "the real part. The outer SMTSO pattern is symmetric and cannot decide the axis"),
    "resolving_evidence": (
        "a real front/rear labelled photograph of the delivered module, or the physical "
        "part offered to the front board"),
    "not_asserted": ["which in-plane axis is correct", "front/rear mapping of the drawing"],
}

# The two admissible rigid placements. Both reverse Z; they differ only in which
# in-plane axis they mirror. Neither is claimed correct.
def rotation_candidates() -> dict[str, tuple[tuple[float, float, float], ...]]:
    return {
        "about_y": ((-1.0, 0.0, 0.0),
                    (0.0, 1.0, 0.0),
                    (0.0, 0.0, -1.0)),
        "about_x": ((1.0, 0.0, 0.0),
                    (0.0, -1.0, 0.0),
                    (0.0, 0.0, -1.0)),
    }


def display_rotation() -> tuple[tuple[float, float, float], ...]:
    """The PROVISIONAL candidate the model is drawn with: 180 deg about Y.

    This is a candidate for drawing only. `DISPLAY_ORIENTATION` states the X and
    Y ambiguity and the evidence that would settle it; no test and no document
    may call this axis proven.
    """
    return rotation_candidates()["about_y"]


def rotation_determinant(matrix=None) -> float:
    """Determinant of a 3x3 matrix. +1 is a rotation, -1 would be a mirror."""
    m = display_rotation() if matrix is None else matrix
    a, b, c = m[0]
    d, e, f = m[1]
    g, h, i = m[2]
    return a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g)


def apply_rotation(point: tuple[float, float, float],
                   candidate: str = "about_y") -> tuple[float, float, float]:
    m = rotation_candidates()[candidate]
    x, y, z = point
    return tuple(m[r][0] * x + m[r][1] * y + m[r][2] * z for r in range(3))  # type: ignore[return-value]


def inner_pattern_candidates() -> dict:
    """Both candidate inner-hole patterns, with the caveat that neither is proof.

    The inner plain-hole pattern is asymmetric in X (25.62 vs 22.38 mm insets),
    so it distinguishes the candidates. It does NOT identify the correct one:
    the layout's recorded positions carry the same no-flip premise that the
    drawing's view direction is being questioned for.
    """
    m = VENDOR_DISPLAY_MODEL
    cx, cy = m["mount_pattern_centre_xy"]
    offsets = [(x - cx, y - cy) for x in m["inner_hole_x"] for y in m["inner_hole_y"]]
    patterns = {}
    for name in rotation_candidates():
        patterns[name] = sorted(
            (round(apply_rotation((x, y, 0.0), name)[0], 3),
             round(apply_rotation((x, y, 0.0), name)[1], 3)) for x, y in offsets)
    return {
        "vendor_offsets_xy": sorted((round(x, 3), round(y, 3)) for x, y in offsets),
        "candidates": patterns,
        "status": "PENDING",
        "caveat": ("a candidate matching the recorded DISPLAY_STANDOFFS is consistent with "
                   "the layout only: the layout uses the same no-flip premise, so it is not "
                   "independent proof"),
    }


# Z = 0 at the panel FRONT face, +Z rearward.
DISPLAY_GLASS_DATUM_Z = 0.0


def placed_display_planes() -> dict[str, float]:
    """Where the vendor model's planes land when its glass front is at Z = 0.

    Derived from the measured vendor planes, never typed in: the placement puts
    the glass front face on the panel front face (assumption A) and this is
    what follows for every other plane.
    """
    m = VENDOR_DISPLAY_MODEL
    def place(vendor_z: float) -> float:
        return round(DISPLAY_GLASS_DATUM_Z + (m["glass_front_z"] - vendor_z), 6)
    return {
        "touch_frame_front": place(m["touch_frame_front_z"]),  # negative: in FRONT of the panel
        "glass_front": place(m["glass_front_z"]),
        "glass_rear": place(m["glass_rear_z"]),
        "pcb_front": place(m["pcb_front_z"]),
        "pcb_rear": place(m["pcb_rear_z"]),
        "boss_rear": place(m["boss_rear_z"]),
        "rear_most": place(m["rear_most_z"]),
    }


def check_display_plane_order(planes: dict[str, float]) -> None:
    """Fail when the display is reversed: glass must be forward of the PCB.

    Z increases rearward, so a correct placement has

        touch_frame_front < glass_front <= glass_rear < pcb_front < pcb_rear
                          < boss_rear <= rear_most

    A placement that maps the bounding-box minimum to Z = 0 puts the boss ends
    in front of the glass and fails here.
    """
    required = ("touch_frame_front", "glass_front", "glass_rear", "pcb_front",
                "pcb_rear", "boss_rear", "rear_most")
    missing = [k for k in required if k not in planes]
    if missing:
        raise GeometryError(f"display plane report is incomplete: {missing}")
    pairs = [("touch_frame_front", "glass_front"), ("glass_front", "glass_rear"),
             ("glass_rear", "pcb_front"), ("pcb_front", "pcb_rear"),
             ("pcb_rear", "boss_rear"), ("boss_rear", "rear_most")]
    for front, back in pairs:
        if not planes[front] < planes[back]:
            raise GeometryError(
                f"the display is reversed: {front} ({planes[front]:.3f} mm) is not "
                f"forward of {back} ({planes[back]:.3f} mm). The glass must face the "
                "panel and the electronics must point rearward.")


# ---------------------------------------------------------------------------
# A one-dimensional extent, in millimetres.
# ---------------------------------------------------------------------------
@dataclass(frozen=True)
class Span:
    """Closed interval on one axis in mm. ``None`` bounds mean unbounded.

    An unbounded span cannot prove a separation, so it is treated as unknown by
    :func:`classify`: the result is undetermined, never clear.
    """

    lo: float | None
    hi: float | None

    def __post_init__(self) -> None:
        for name, value in (("lo", self.lo), ("hi", self.hi)):
            if value is not None and not math.isfinite(value):
                raise GeometryError(f"span {name} is not a finite number: {value!r}")
        if self.lo is not None and self.hi is not None and self.lo > self.hi:
            raise GeometryError(f"span is inverted or empty: {self.lo} > {self.hi}")

    @classmethod
    def around(cls, centre: float, size: float) -> "Span":
        """Span of ``size`` centred on ``centre``."""
        if not math.isfinite(centre) or not math.isfinite(size):
            raise GeometryError("centre and size must be finite")
        if size < 0:
            raise GeometryError(f"size must not be negative: {size}")
        return cls(centre - size / 2, centre + size / 2)

    @property
    def bounded(self) -> bool:
        return self.lo is not None and self.hi is not None

    @property
    def length(self) -> float | None:
        return None if not self.bounded else self.hi - self.lo  # type: ignore[operator]

    @property
    def centre(self) -> float | None:
        return None if not self.bounded else (self.lo + self.hi) / 2  # type: ignore[operator]

    def overlap(self, other: "Span") -> float | None:
        """Overlap depth (positive when the spans share material)."""
        if not (self.bounded and other.bounded):
            return None
        return min(self.hi, other.hi) - max(self.lo, other.lo)  # type: ignore[type-var]

    def gap(self, other: "Span") -> float | None:
        """Separation (positive when the spans are apart, 0 when touching)."""
        if not (self.bounded and other.bounded):
            return None
        return max(self.lo - other.hi, other.lo - self.hi)  # type: ignore[operator]


@dataclass(frozen=True)
class Envelope:
    """An axis-aligned box for one body, with what is known about each axis.

    ``assumed_axes`` names the axes whose value is a candidate used to draw the
    model. A verdict that depends on an assumed axis is downgraded to
    undetermined; it is never presented as a fit result.
    """

    name: str
    provenance: str
    x: Span | None
    y: Span | None
    z: Span | None
    assumed_axes: tuple[str, ...] = ()
    note: str = ""

    def axis(self, axis: str) -> Span | None:
        return getattr(self, axis)

    def spans(self) -> dict[str, Span | None]:
        return {"x": self.x, "y": self.y, "z": self.z}

    @property
    def illustrative(self) -> bool:
        """True when any axis is a candidate value, so no verdict is definitive."""
        return bool(self.assumed_axes)


# ---------------------------------------------------------------------------
# Collision classification.
# ---------------------------------------------------------------------------
CLEAR = "clear"
COLLISION = "collision"
UNDETERMINED = "undetermined"


@dataclass(frozen=True)
class AxisAssessment:
    axis: str
    relation: str           # "disjoint" | "touching" | "overlap" | "unknown"
    amount_mm: float | None  # gap when disjoint/touching, overlap depth when overlap
    assumed: bool
    reason: str


@dataclass(frozen=True)
class CollisionVerdict:
    status: str
    clearance_mm: float | None   # smallest proven separation, when clear
    overlap_mm: float | None     # smallest overlap depth, when a collision
    decisive_axis: str | None
    axes: tuple[AxisAssessment, ...]
    undetermined_axes: tuple[str, ...]
    reasons: tuple[str, ...]
    illustrative: bool           # a candidate axis drove the downgrade or the result

    @property
    def definitive(self) -> bool:
        return self.status in (CLEAR, COLLISION)


def classify(a: Envelope, b: Envelope, *, eps: float = EPS) -> CollisionVerdict:
    """Classify two envelopes as clear, collision or undetermined.

    Rules, in this order:

    1. A disjoint axis PROVES that the bodies cannot touch, so the result is
       clear whatever the other axes say - unless that axis is a candidate
       value, in which case the separation is not evidence and the result is
       undetermined.
    2. Overlap on all three axes with a positive depth is a collision.
    3. Touching (zero overlap) on any axis is contact, not a collision: clear
       with 0 mm clearance.
    4. Anything else - an axis with no value, or an unbounded one - is
       undetermined. This is the fail-closed rule.
    """
    axes: list[AxisAssessment] = []
    reasons: list[str] = []
    undetermined: list[str] = []
    illustrative = False

    for axis in ("x", "y", "z"):
        sa, sb = a.axis(axis), b.axis(axis)
        assumed = axis in a.assumed_axes or axis in b.assumed_axes
        if sa is None or sb is None:
            missing = [e.name for e, s in ((a, sa), (b, sb)) if s is None]
            axes.append(AxisAssessment(axis, "unknown", None, assumed,
                                       f"no value for {' and '.join(missing)}"))
            undetermined.append(axis)
            continue
        if not (sa.bounded and sb.bounded):
            axes.append(AxisAssessment(axis, "unknown", None, assumed,
                                       "an unbounded extent cannot prove a separation"))
            undetermined.append(axis)
            continue
        gap = sa.gap(sb)
        assert gap is not None
        if gap > eps:
            axes.append(AxisAssessment(axis, "disjoint", gap, assumed,
                                       f"{a.name} and {b.name} are {gap:.3f} mm apart in {axis}"))
        elif gap >= -eps:
            axes.append(AxisAssessment(axis, "touching", 0.0, assumed,
                                       f"{a.name} and {b.name} touch in {axis} (0 mm gap)"))
        else:
            overlap = sa.overlap(sb)
            assert overlap is not None
            axes.append(AxisAssessment(axis, "overlap", overlap, assumed,
                                       f"{a.name} and {b.name} overlap {overlap:.3f} mm in {axis}"))

    disjoint = [x for x in axes if x.relation == "disjoint"]
    touching = [x for x in axes if x.relation == "touching"]
    overlapping = [x for x in axes if x.relation == "overlap"]

    def assumed_blocker(axis_name: str) -> bool:
        nonlocal illustrative
        illustrative = True
        if axis_name not in undetermined:
            # the axis has a value, but it is a candidate: report it as untrusted
            undetermined.append(axis_name)
        reasons.append(
            f"axis {axis_name} is a candidate value, not a documented one, so the "
            f"result stays undetermined (see the envelope note)")
        return True

    if disjoint:
        # A separating axis proves the bodies cannot touch whatever the other
        # axes say - but only when that axis is a documented value. If every
        # separating axis is a candidate value, the separation is not evidence.
        proven = [x for x in disjoint if not x.assumed]
        if not proven:
            for x in disjoint:
                assumed_blocker(x.axis)
            return CollisionVerdict(UNDETERMINED, None, None, disjoint[0].axis, tuple(axes),
                                    tuple(undetermined), tuple(reasons), illustrative)
        first = max(proven, key=lambda x: x.amount_mm or 0.0)
        reasons.append(f"disjoint in {first.axis}: the bodies cannot intersect")
        return CollisionVerdict(CLEAR, first.amount_mm, None, first.axis, tuple(axes),
                                tuple(undetermined), tuple(reasons), illustrative)

    if len(overlapping) == 3:
        deepest = min(overlapping, key=lambda x: x.amount_mm or 0.0)
        assumed = [x for x in overlapping if x.assumed]
        if assumed:
            for x in assumed:
                assumed_blocker(x.axis)
            return CollisionVerdict(UNDETERMINED, None, None, None, tuple(axes),
                                    tuple(undetermined), tuple(reasons), illustrative)
        reasons.append("all three axes overlap with a positive depth: the bodies intersect")
        return CollisionVerdict(COLLISION, None, deepest.amount_mm, None, tuple(axes),
                                tuple(undetermined), tuple(reasons), illustrative)

    contact = [x for x in touching if not x.assumed]
    unknown = [x for x in axes if x.relation == "unknown"]
    if len(overlapping) + len(touching) + len(unknown) == 3 and unknown:
        for x in unknown:
            reasons.append(f"axis {x.axis} has no value ({x.reason}): fail closed as undetermined")
        return CollisionVerdict(UNDETERMINED, None, None, None, tuple(axes),
                                tuple(undetermined), tuple(reasons), illustrative)

    if contact:
        first_contact = max(contact, key=lambda x: x.amount_mm or 0.0)
        reasons.append(f"contact in {first_contact.axis}: zero overlap is not a collision")
        return CollisionVerdict(CLEAR, 0.0, None, first_contact.axis, tuple(axes),
                                tuple(undetermined), tuple(reasons), illustrative)

    if touching:
        assumed_blocker(touching[0].axis)
        return CollisionVerdict(UNDETERMINED, None, None, touching[0].axis, tuple(axes),
                                tuple(undetermined), tuple(reasons), illustrative)

    reasons.append("no axis proves a separation and not all axes prove an overlap: undetermined")
    return CollisionVerdict(UNDETERMINED, None, None, None, tuple(axes),
                            tuple(undetermined), tuple(reasons), illustrative)


# ---------------------------------------------------------------------------
# LCD mount: the model gap, the design-chain candidate and the residual.
# ---------------------------------------------------------------------------
# The four LCD mount axes come from the exported layout (lcdMountHoles). The
# SPAN comes from the drawn solids: the vendor model's SMT boss rear plane and
# the drawn front-board top face. Two different numbers exist and they must not
# be conflated:
#
#   model gap    boss rear -> board top face        8.81 mm   (drawn solids)
#   chain spacer the existing design assumption      5.00 mm   (9.0 - 4.0)
#   residual     what a 5.00 mm spacer misses       3.81 mm   (14.05 vs 10.24)
#
# Neither number is an approved dimension and the two chains do NOT agree.
DISPLAY_MOUNT_PLACEHOLDER = {
    "name_prefix": "MODEL_FIT_SPACER_PLACEHOLDER_NON_APPROVED",
    "status": "GEOMETRIC PLACEHOLDER ONLY - non-approved, not a part, not a fit",
    "outer_diameter_mm": 5.0,   # assumed, illustration only
    "inner_diameter_mm": 2.7,   # assumed clearance, illustration only
    "dimension_basis": ("OD 5.0 and ID 2.7 are ASSUMED for the drawing only. They are not a "
                        "specification, not a part number, and not a thread call-out. OD exceeds "
                        "the board's 4.3 mm LCD hole so the placeholder shows a bearing annulus"),
}


def display_mount_gap(layout: dict | None = None) -> dict:
    """The model-derived mount gap, the design-chain candidate and the residual.

    Everything here is arithmetic on measured model planes plus one documented
    design assumption. It is NOT an approved spacer length: the correct length,
    the standoff diameter, the glass seating, the mounting thread and the
    assembly sequence are all first-article tasks.
    """
    data = layout if layout is not None else load_layout()
    planes = placed_display_planes()
    boss_rear = planes["boss_rear"]                 # 10.24, model boss ends
    board_top = float(data["provisionalZ"]["frontBoardTop"])   # 19.05
    chain = float(data["displayStandoff"]["spacerLength"])     # 5.00 design assumption
    gap = round(board_top - boss_rear, 3)                       # 8.81
    chain_end = round(board_top - chain, 3)                     # 14.05
    residual = round(chain_end - boss_rear, 3)                  # 3.81
    return {
        "axes_xy": [(h["x"], h["y"]) for h in data["lcdMountHoles"]],
        "boss_rear_z_mm": round(boss_rear, 3),
        "board_top_z_mm": round(board_top, 3),
        "model_gap_mm": gap,
        "design_chain_spacer_mm": round(chain, 3),
        "design_chain_display_end_z_mm": chain_end,
        "residual_mm": residual,
        "chains_agree": False,
        "chains_agree_note": ("the 5.00 mm design candidate and the 8.81 mm model gap are two "
                              "different chains; they do not agree and neither is approved"),
        "spacer_length_mm": gap,
        "length_status": "model-derived placeholder length, NOT an approved dimension",
        "outer_diameter_mm": DISPLAY_MOUNT_PLACEHOLDER["outer_diameter_mm"],
        "inner_diameter_mm": DISPLAY_MOUNT_PLACEHOLDER["inner_diameter_mm"],
        "dimension_status": "OD and ID are assumed for illustration, no dimensional claim",
        "first_article_tasks": [
            "the correct spacer length (measure the real display boss plane and board face)",
            "the standoff diameter and the glass seating plane",
            "the mounting thread (M2.5?) and its engagement depth",
            "the assembly order and the torque/retention method",
        ],
    }


# ---------------------------------------------------------------------------
# Plan-view helpers: the flange projection.
# ---------------------------------------------------------------------------
def intersect_rect(a: Rect, b: Rect, *, eps: float = EPS) -> Rect | None:
    """Overlap of two rectangles, or None when they are apart or only touch."""
    x1, x2 = max(a.x1, b.x1), min(a.x2, b.x2)
    y1, y2 = max(a.y1, b.y1), min(a.y2, b.y2)
    if x2 - x1 <= eps or y2 - y1 <= eps:
        return None
    return Rect(x1, y1, x2, y2)


def subtract_rect(outer: Rect, cut: Rect, *, eps: float = EPS) -> list[Rect]:
    """``outer`` minus ``cut`` as up to four disjoint rectangles."""
    keep = intersect_rect(outer, cut, eps=eps)
    if keep is None:
        return [outer]
    pieces = [
        Rect(outer.x1, outer.y1, outer.x2, keep.y1),
        Rect(outer.x1, keep.y2, outer.x2, outer.y2),
        Rect(outer.x1, keep.y1, keep.x1, keep.y2),
        Rect(keep.x2, keep.y1, outer.x2, keep.y2),
    ]
    return [p for p in pieces if p.width > eps and p.height > eps]


def board_plan_rects(layout: dict) -> list[Rect]:
    """The front board's plan area, split by the top relief notch."""
    front = layout["frontPcb"]
    board = Rect(-front["width"] / 2, -front["height"] / 2,
                 front["width"] / 2, front["height"] / 2)
    n = layout["notchBounds"]
    notch = Rect(n["left"], n["bottom"], n["right"], n["top"])
    return subtract_rect(board, notch)


@dataclass(frozen=True)
class FlangeOverlap:
    """The projected overlap of one documented flange with board material."""

    feedthrough_id: str
    flange: Rect
    overlap_rects: tuple[Rect, ...]
    depth_known: bool
    z_note: str

    @property
    def overlap_area_mm2(self) -> float:
        return sum(r.width * r.height for r in self.overlap_rects)

    @property
    def max_overlap_x_mm(self) -> float:
        return max((r.width for r in self.overlap_rects), default=0.0)

    @property
    def max_overlap_y_mm(self) -> float:
        return max((r.height for r in self.overlap_rects), default=0.0)


ADAFRUIT_908_FEEDTHROUGH_IDS = ("UMC AUDIO USB", "MIDI HUB USB")

# Owner-supplied candidate USB-C panel cable. NOT an approved part: the source
# is a marketplace listing and a photograph of it, the seller's own text gives
# four different power figures (240 W in the title, 120 W in the bullet points,
# 100 W in the table, 5 A separately), no manufacturer datasheet exists, and
# pass-through, PD negotiation and 5 A behaviour are all unverified. The
# dimensions below are what the supplied image annotates; that image leaves the
# 13.4 mm annotation ambiguous, so this constant records NO mounting pitch.
USBC_CANDIDATE = {
    "part": "YINGAYOU-104 USB-C panel cable 0.3 m (marketplace listing, NOT verified)",
    "source_url": "https://www.amazon.se/dp/B0GF22WM9T",
    "supplied_image": "/workspace/home/.pi-pocket/uploads/22896/mw8dggt1-9b43e7ed-image.png",
    "retrieved": "2026-11-08",
    "flange_width_mm": 25.0,        # advertised, image annotation
    "flange_height_mm": 10.2,       # advertised, image annotation
    "flange_thickness_mm": 5.0,     # advertised, image annotation
    "overall_length_mm": 305.0,     # advertised, image annotation
    "rear_body_length_mm": 35.0,    # advertised; cross-section NOT stated
    "mount_pitch_mm": None,         # the 13.4 mm annotation is ambiguous
    "current_rating": None,         # seller text conflicts; nothing verified
    "status": "NON-APPROVED candidate; no fit, current, PD or pass-through claim",
}


def usbc_candidate_envelope(layout: dict) -> Envelope:
    """The advertised USB-C flange, as a NON-APPROVED candidate volume.

    Opt-in only. Every axis is marked assumed, so no verdict that depends on it
    can be definitive: the flange sits on the recorded panel opening centre, its
    seat plane and its depth behind the panel are unknown, and the seller
    publishes no mounting pitch. Only the flange is modelled; the 35 mm rear
    body has no stated cross-section, so drawing it would be invented geometry.
    """
    f = next((x for x in layout["feedthroughs"] if x["id"] == "USB-C POWER"), None)
    if f is None:
        raise GeometryError("the USB-C POWER feedthrough is missing from mechanical/layout.json")
    return Envelope(
        name="USB_C_CANDIDATE_FLANGE_NON_APPROVED",
        provenance=PROVENANCE["candidate"],
        x=Span.around(f["x"], USBC_CANDIDATE["flange_width_mm"]),
        y=Span.around(f["y"], USBC_CANDIDATE["flange_height_mm"]),
        # Advertised flange thickness, placed at the panel FRONT face: the
        # listing does not say which side of the panel the flange sits on.
        z=Span(0.0, USBC_CANDIDATE["flange_thickness_mm"]),
        assumed_axes=("x", "y", "z"),
        note=("NON-APPROVED candidate envelope from a marketplace listing and its "
              "photograph; dimensions advertised, not measured. No mounting pitch is "
              "recorded (the 13.4 mm annotation is ambiguous), the current rating is "
              "contested in the seller's own text, and no fit or 5 A claim is made"),
    )


def flange_overlaps(layout: dict, flange: dict = ADAFRUIT_908) -> list[FlangeOverlap]:
    """Project the Adafruit 908 flange onto the front board for every 908.

    The flange rectangle is placed on the CANONICAL feedthrough centre, so a
    moved feedthrough moves the flange. The recorded panel opening and screw
    pitch are checked against the drawing, so a drifted layout fails instead of
    quietly producing a different fit.
    """
    if flange["depth_mm"] is not None:
        raise GeometryError(
            "the flange fit must not use a depth that is not in the source drawing; "
            "update ADAFRUIT_908 and the acceptance item before using one")
    parts = board_plan_rects(layout)
    by_id = {f["id"]: f for f in layout["feedthroughs"]}
    out: list[FlangeOverlap] = []
    for feedthrough_id in ADAFRUIT_908_FEEDTHROUGH_IDS:
        f = by_id.get(feedthrough_id)
        if f is None:
            raise GeometryError(
                f"feedthrough {feedthrough_id!r} is missing from mechanical/layout.json; "
                "the 908 flange projection cannot be computed")
        for key, expected in (("cutoutWidth", flange["panel_opening_width_mm"]),
                              ("cutoutHeight", flange["panel_opening_height_mm"]),
                              ("holeSpacing", flange["mount_pitch_mm"])):
            if abs(f[key] - expected) > 1e-6:
                raise GeometryError(
                    f"{feedthrough_id} {key} is {f[key]}, but the Adafruit 908 drawing says "
                    f"{expected}; the flange projection would be built on drifted data")
        rect = Rect(f["x"] - flange["flange_width_mm"] / 2,
                    f["y"] - flange["flange_height_mm"] / 2,
                    f["x"] + flange["flange_width_mm"] / 2,
                    f["y"] + flange["flange_height_mm"] / 2)
        pieces = []
        for part in parts:
            hit = intersect_rect(rect, part)
            if hit is not None:
                pieces.append(hit)
        out.append(FlangeOverlap(
            feedthrough_id=f["id"],
            flange=rect,
            overlap_rects=tuple(pieces),
            depth_known=False,
            z_note=("the drawing states no flange/body depth, so the Z extent is unknown; "
                    "the overlap above is an XY projection, not a collision"),
        ))
    return out


# ---------------------------------------------------------------------------
# The envelopes of the verification assembly, in absolute panel coordinates.
# ---------------------------------------------------------------------------
def assembly_envelopes(layout: dict, *, include_usbc_candidate: bool = False) -> dict[str, Envelope]:
    """Build every envelope the STEP model draws, with its provenance.

    Z datum: 0 is the panel front face, +Z points rearward (docs/mechanical-stack.md).
    """
    panel, front, rear = layout["panel"], layout["frontPcb"], layout["rearPcb"]
    pz = layout["provisionalZ"]
    depth = layout["provisionalDepth"]
    d = layout["display"]

    rear_front_z = pz["frontBoardRear"] + depth["frontToRearGap"]
    rear_rear_z = rear_front_z + rear["thickness"]

    env: dict[str, Envelope] = {}

    env["PANEL_30HP_3U"] = Envelope(
        name="PANEL_30HP_3U",
        provenance=PROVENANCE["canonical"],
        x=Span(-panel["width"] / 2, panel["width"] / 2),
        y=Span(-panel["height"] / 2, panel["height"] / 2),
        z=Span(pz["panelFront"], pz["panelRear"]),
        note="front panel outline; the LCD aperture, feedthrough cutouts and holes are cut from it",
    )

    # assumed_axes: the Z span rests on the unmeasured PROVISIONAL_Z chain, so any
    # verdict that depends on it must downgrade to undetermined. XY is canonical.
    env["FRONT_BOARD_PROVISIONAL_Z"] = Envelope(
        name="FRONT_BOARD_PROVISIONAL_Z",
        provenance=PROVENANCE["provisional-z"],
        x=Span(-front["width"] / 2, front["width"] / 2),
        y=Span(-front["height"] / 2, front["height"] / 2),
        z=Span(pz["frontBoardTop"], pz["frontBoardRear"]),
        assumed_axes=("z",),
        note=("top face at the PROVISIONAL_Z value: assumptions A and B, not a measurement. "
              "The Z axis is a candidate value, so no Z-based verdict is definitive"),
    )

    env["REAR_BOARD_PROVISIONAL_Z"] = Envelope(
        name="REAR_BOARD_PROVISIONAL_Z",
        provenance=PROVENANCE["provisional-z"],
        x=Span(-rear["width"] / 2, rear["width"] / 2),
        y=Span(-rear["height"] / 2, rear["height"] / 2),
        z=Span(rear_front_z, rear_rear_z),
        assumed_axes=("z",),
        note=("Z from PROVISIONAL_Z plus the front-to-rear gap "
              f"({depth['frontToRearGap']:.2f} mm). That gap is now the direct 2 x 13 "
              "mating pair's NOMINAL seat-to-seat drawing dimension, not a measured gap: "
              "board flatness, support, solder-joint height and the fitted pair are "
              "unverified, so the Z axis is a candidate value and no Z-based verdict is "
              "definitive"),
    )

    env["DISPLAY_WAVESHARE_4_3_DSI_PROVISIONAL_Z"] = Envelope(
        name="DISPLAY_WAVESHARE_4_3_DSI_PROVISIONAL_Z",
        provenance=PROVENANCE["provisional-z"],
        x=Span.around(d["x"], d["width"]),
        y=Span.around(d["y"], d["height"]),
        z=Span(pz["displayGlassFront"], pz["displayRearMost"]),
        assumed_axes=("z",),
        note=("XY from the module outline (106 x 68) and therefore documented; Z spans the "
              "glass front (assumed flush, assumption A) to the 14.05 mm vendor overall depth, "
              "so the Z axis is a candidate value and no Z-based verdict is definitive"),
    )

    for f in layout["feedthroughs"]:
        name = f"FEEDTHROUGH_{f['id'].replace(' ', '_').replace('-', '_')}_FLANGE"
        if abs(f["holeSpacing"] - ADAFRUIT_908["mount_pitch_mm"]) < 1e-6 \
                and abs(f["cutoutWidth"] - ADAFRUIT_908["panel_opening_width_mm"]) < 1e-6:
            env[name] = Envelope(
                name=name,
                provenance=PROVENANCE["documented"],
                x=Span.around(f["x"], ADAFRUIT_908["flange_width_mm"]),
                y=Span.around(f["y"], ADAFRUIT_908["flange_height_mm"]),
                z=None,
                assumed_axes=(),
                note=("X/Y from the Adafruit 908 drawing; the drawing states NO depth, so Z "
                      "is unknown and every 3D result stays undetermined"),
            )
        else:
            env[name] = Envelope(
                name=name,
                provenance=PROVENANCE["unknown"],
                x=None,
                y=None,
                z=None,
                assumed_axes=(),
                note=("PROVISIONAL, candidate not approved: no manufacturer-verified 5 A USB-C "
                      "panel part exists yet, so neither flange nor body is modelled. The "
                      "recorded panel cutout (12.6 x 7.6 mm, 18 mm pitch) is unchanged and a "
                      "panel opening does not prove the rear body"),
            )

    if include_usbc_candidate:
        env["USB_C_CANDIDATE_FLANGE_NON_APPROVED"] = usbc_candidate_envelope(layout)

    # NO BODY IS MODELLED for the light pipe: its seated plane, the LED height, the
    # retention and the stack tolerances are unmeasured, so every axis is unknown.
    # The name carries the ACTIVE candidate; the retired Bivar reference is history.
    env["LIGHT_PIPE_PGN25L165_PLACEHOLDER"] = Envelope(
        name="LIGHT_PIPE_PGN25L165_PLACEHOLDER",
        provenance=PROVENANCE["unknown"],
        x=None,
        y=None,
        z=None,
        note=("candidate Signal Construct PGN25L165: shaft x 16.5 +-0.1, overall 17.5, head "
              "Ø3, mounting hole Ø2.6 -0.05. NO body is modelled and no axis is known; the "
              "nominal tip plane (16.5 from the panel front) and the tip-to-board-plane "
              "distance (2.55) are arithmetic, not an LED gap. The optical gap is None; see "
              "mechanical/light_pipe_reach.py"),
    )
    return env


# The STEP/STL body legend. Every drawn body appears here with what it means and
# what its status is, so the visualization cannot imply more than it shows.
BODY_LEGEND = (
    {"name": "PANEL_30HP_3U", "meaning": "bare 30HP panel, 152.0 mm actual",
     "status": "provisional model, not a released drawing"},
    {"name": "FRONT_BOARD_PROVISIONAL_Z", "meaning": "128 x 112 mm front/UI board",
     "status": "XY canonical, Z provisional (assumption A and B)"},
    {"name": "REAR_BOARD_PROVISIONAL_Z", "meaning": "101.6 x 112 mm rear/base board",
     "status": "XY canonical, Z provisional (candidate gap)"},
    {"name": "DISPLAY_WAVESHARE_VENDOR_STEP",
     "meaning": "vendor display model, SHA-256 verified",
     "status": "illustrative placement; in-plane axis PENDING"},
    {"name": "DISPLAY_ENVELOPE_PROVISIONAL",
     "meaning": "106 x 68 mm display envelope used when no vendor model is supplied",
     "status": "envelope, not vendor geometry"},
    {"name": "MODEL_FIT_SPACER_PLACEHOLDER_NON_APPROVED_1..4",
     "meaning": "four annuli spanning the model gap, to show the LCD mount",
     "status": "GEOMETRIC PLACEHOLDER, non-approved, not a part"},
    {"name": "DISPLAY_CONNECTOR_PLANNING_MARKER",
     "meaning": "legacy connector record drawn as a box on the provisional 10.05 plane",
     "status": "unverified planning data, not a verified connector"},
    {"name": "FEEDTHROUGH_*_FLANGE_PLAN_MARKER",
     "meaning": "Adafruit 908 flange outline on the panel rear face",
     "status": "plan marker; the drawing publishes no depth"},
    {"name": "USB_C_CANDIDATE_FLANGE_NON_APPROVED",
     "meaning": "opt-in advertised USB-C flange",
     "status": "NON-APPROVED candidate, no fit claim"},
)

# Pairs whose relationship the metadata reports. Order is fixed so the file is
# byte-stable across runs.
COLLISION_PAIRS: tuple[tuple[str, str], ...] = (
    ("FEEDTHROUGH_UMC_AUDIO_USB_FLANGE", "FRONT_BOARD_PROVISIONAL_Z"),
    ("FEEDTHROUGH_MIDI_HUB_USB_FLANGE", "FRONT_BOARD_PROVISIONAL_Z"),
    ("FEEDTHROUGH_USB_C_POWER_FLANGE", "FRONT_BOARD_PROVISIONAL_Z"),
    ("FEEDTHROUGH_UMC_AUDIO_USB_FLANGE", "PANEL_30HP_3U"),
    ("FEEDTHROUGH_UMC_AUDIO_USB_FLANGE", "DISPLAY_WAVESHARE_4_3_DSI_PROVISIONAL_Z"),
    ("DISPLAY_WAVESHARE_4_3_DSI_PROVISIONAL_Z", "FRONT_BOARD_PROVISIONAL_Z"),
    ("FRONT_BOARD_PROVISIONAL_Z", "REAR_BOARD_PROVISIONAL_Z"),
)


@dataclass(frozen=True)
class PairReport:
    a: str
    b: str
    verdict: CollisionVerdict
    a_provenance: str
    b_provenance: str


def pair_reports(layout: dict | None = None, *,
                 include_usbc_candidate: bool = False) -> list[PairReport]:
    """Classify every named pair, never raising on an undetermined result.

    The candidate USB-C flange adds one pair (it against the front board) when
    it is included. Its axes are candidates, so that pair is always undetermined.
    """
    data = layout if layout is not None else load_layout()
    env = assembly_envelopes(data, include_usbc_candidate=include_usbc_candidate)
    pairs = list(COLLISION_PAIRS)
    if include_usbc_candidate:
        pairs.append(("USB_C_CANDIDATE_FLANGE_NON_APPROVED", "FRONT_BOARD_PROVISIONAL_Z"))
    out: list[PairReport] = []
    for a, b in pairs:
        out.append(PairReport(a, b, classify(env[a], env[b]),
                              env[a].provenance, env[b].provenance))
    return out


# ---------------------------------------------------------------------------
# Output directory guard: an explicit directory, and nothing else touched.
# ---------------------------------------------------------------------------
OUTPUT_NAMES: tuple[str, ...] = (
    "midibrain-step-assembly.step",
    "mockup-panel.stl",
    "mockup-front-board.stl",
    "mockup-rear-board.stl",
    "scale-check-100mm.stl",
    "light-pipe-reach-gauge.stl",
    "assumptions.json",
    "print-checklist.md",
)

DEFAULT_OUTPUT_DIR = Path(__file__).resolve().parent / "cad-out"
PROJECT_ROOT = Path(__file__).resolve().parent.parent


def resolve_output_dir(path: str | Path | None = None, *, force: bool = False,
                       default: Path = DEFAULT_OUTPUT_DIR,
                       project_root: Path = PROJECT_ROOT) -> Path:
    """Return the directory to write CAD exports into, or raise.

    The directory must be explicit (default: ``mechanical/cad-out``), must not
    be an existing file, the filesystem root or the project root, and must hold
    nothing but this script's own outputs unless ``force`` is given. Nothing is
    ever deleted: a foreign file is a refusal to write, not a cleanup.
    """
    out = Path(default if path is None else path).expanduser()
    out = out.resolve() if out.is_absolute() else (Path.cwd() / out).resolve()

    if out == Path(out.anchor):
        raise GeometryError(f"refusing to write CAD exports into the filesystem root: {out}")
    if out == project_root.resolve():
        raise GeometryError(f"refusing to write CAD exports into the project root: {out}")
    if out.parent == out:
        raise GeometryError(f"refusing to use {out} as an output directory")
    if out.exists() and not out.is_dir():
        raise GeometryError(f"{out} exists and is not a directory")
    if out.is_dir():
        foreign = sorted(p.name for p in out.iterdir() if p.name not in OUTPUT_NAMES)
        if foreign and not force:
            raise GeometryError(
                f"{out} holds {len(foreign)} file(s) this script does not write "
                f"({', '.join(foreign[:5])}); use an empty directory or pass --force")
    return out


def assumptions_document(layout: dict | None = None, *,
                         include_usbc_candidate: bool = False) -> dict:
    """The assumptions metadata written beside the CAD exports.

    Every claim in it is either quoted from a source or marked unverified. It
    never reports a collision result that the geometry cannot prove.
    """
    data = layout if layout is not None else load_layout()
    env = assembly_envelopes(data, include_usbc_candidate=include_usbc_candidate)
    flange_source = {
        "part": ADAFRUIT_908["part"],
        "source_url": ADAFRUIT_908["source_url"],
        "retrieved": ADAFRUIT_908["retrieved"],
        "flange_width_mm": ADAFRUIT_908["flange_width_mm"],
        "flange_height_mm": ADAFRUIT_908["flange_height_mm"],
        "mount_pitch_mm": ADAFRUIT_908["mount_pitch_mm"],
        "depth_mm": ADAFRUIT_908["depth_mm"],
        "note": "the drawing gives the flange and the screw pitch and no body depth",
    }
    overlaps = [
        {
            "feedthrough": o.feedthrough_id,
            "flange_x_mm": [round(o.flange.x1, 6), round(o.flange.x2, 6)],
            "flange_y_mm": [round(o.flange.y1, 6), round(o.flange.y2, 6)],
            "board_overlap_rects_mm": [[round(r.x1, 6), round(r.y1, 6),
                                        round(r.x2, 6), round(r.y2, 6)]
                                       for r in o.overlap_rects],
            "board_overlap_area_mm2": round(o.overlap_area_mm2, 6),
            "max_overlap_x_mm": round(o.max_overlap_x_mm, 6),
            "max_overlap_y_mm": round(o.max_overlap_y_mm, 6),
            "status": UNDETERMINED,
            "status_reason": o.z_note,
        }
        for o in flange_overlaps(data)
    ]
    envelopes = []
    for name in sorted(env):
        e = env[name]
        envelopes.append({
            "name": name,
            "provenance": e.provenance,
            "x_mm": [e.x.lo, e.x.hi] if e.x else None,
            "y_mm": [e.y.lo, e.y.hi] if e.y else None,
            "z_mm": [e.z.lo, e.z.hi] if e.z else None,
            "assumed_axes": list(e.assumed_axes),
            "note": e.note,
        })
    pairs = [
        {
            "a": p.a,
            "b": p.b,
            "status": p.verdict.status,
            "definitive": p.verdict.definitive,
            "clearance_mm": p.verdict.clearance_mm,
            "overlap_mm": p.verdict.overlap_mm,
            "decisive_axis": p.verdict.decisive_axis,
            "undetermined_axes": list(p.verdict.undetermined_axes),
            "reasons": list(p.verdict.reasons),
            "a_provenance": p.a_provenance,
            "b_provenance": p.b_provenance,
        }
        for p in pair_reports(data, include_usbc_candidate=include_usbc_candidate)
    ]
    usbc_candidate = {
        "enabled": include_usbc_candidate,
        "type": "NON-APPROVED candidate envelope, opt-in only",
        "part": USBC_CANDIDATE["part"],
        "source_url": USBC_CANDIDATE["source_url"],
        "supplied_image": USBC_CANDIDATE["supplied_image"],
        "advertised_dimensions_mm": {
            "flange_width": USBC_CANDIDATE["flange_width_mm"],
            "flange_height": USBC_CANDIDATE["flange_height_mm"],
            "flange_thickness": USBC_CANDIDATE["flange_thickness_mm"],
            "overall_length": USBC_CANDIDATE["overall_length_mm"],
            "rear_body_length": USBC_CANDIDATE["rear_body_length_mm"],
        },
        "mount_pitch_mm": USBC_CANDIDATE["mount_pitch_mm"],
        "current_rating": USBC_CANDIDATE["current_rating"],
        "status": USBC_CANDIDATE["status"],
        "not_modelled": "the 35 mm rear body: its cross-section is not stated",
        "panel_cutout_unchanged": {
            "width": next(f["cutoutWidth"] for f in data["feedthroughs"]
                          if f["id"] == "USB-C POWER"),
            "height": next(f["cutoutHeight"] for f in data["feedthroughs"]
                           if f["id"] == "USB-C POWER"),
            "pitch": next(f["holeSpacing"] for f in data["feedthroughs"]
                          if f["id"] == "USB-C POWER"),
        },
        "note": ("NON-APPROVED: the 13.4 mm annotation in the supplied image is "
                 "ambiguous and is NOT a mounting pitch; the seller's own text gives "
                 "240 W, 120 W, 100 W and 5 A, and no manufacturer datasheet, PD or "
                 "pass-through result exists"),
    }
    return {
        "schema": "midibrain-cad-assumptions/1",
        "units": "mm",
        "source": "mechanical/layout.json (generated from tscircuit/lib/layout.ts)",
        "z_datum": "Z = 0 at the panel front face; +Z points rearward",
        "status": "PROVISIONAL: verification model only. No fit, clearance or collision is approved.",
        "collision_rule": (
            "clear only when one axis proves a separation; collision only when all three "
            "axes overlap with positive depth and no axis is a candidate value; otherwise "
            "undetermined. A documented XY overlap is a projection, not a collision, and an "
            "axis with no documented value can never produce a fit verdict"),
        "assumptions": [
            {"id": "A", "text": "the display glass front sits flush with the panel front face (Z = 0)",
             "status": "unmeasured"},
            {"id": "B", "text": "display plane separation 9.0 mm from the front-board top face to "
                                "the display PCB rear face", "status": "unmeasured"},
            {"id": "C", "text": "display overall depth 14.05 mm, split 10.05 + 4.00 mm",
             "status": "vendor maximum; the split is derived, not measured"},
            {"id": "D", "text": "front-to-rear board gap 20 mm", "status": "candidate range 18-22 mm"},
            {"id": "E", "text": "Adafruit 908 flange is drawn between the panel and the board",
             "status": "the drawing states no depth; the flange Z volume is NOT modelled"},
        ],
        "unknowns": [
            "Adafruit 908 flange depth, seat plane and body envelope",
            "SparkFun CAB-15455 flange and body envelope",
            "the display connector identity, pin count, electrical side and its real "
            "position (the model's DISPLAY_CAM name is not proof)",
            "M2.5 screw-head and washer envelope",
            "display underside component map and FPC latch height",
            "LED position, LED height and light-pipe seat allowance",
            "measured panel, board and standoff thicknesses",
        ],
        "display_mount_placeholders": {
            **display_mount_gap(data),
            "placeholder_definition": DISPLAY_MOUNT_PLACEHOLDER,
            "arithmetic_vs_proof": (
                "the 8.81 mm span and the 3.81 mm residual are ARITHMETIC on the drawn "
                "model planes plus one design assumption. They are not a proof of fit, not "
                "an approved dimension and not a hardware selection"),
        },
        "body_legend": list(BODY_LEGEND),
        "display_model_planes": {
            # Three separate things, never interchangeable.
            "vendor_model": {
                "source": VENDOR_DISPLAY_MODEL["source"],
                "tool": VENDOR_DISPLAY_MODEL["tool"],
                "inspected": VENDOR_DISPLAY_MODEL["inspected"],
                "frame": "vendor frame: +Z points out of the glass, +X is mirrored vs the layout frame",
                "planes_z_mm": {
                    "glass_front": VENDOR_DISPLAY_MODEL["glass_front_z"],
                    "glass_rear": VENDOR_DISPLAY_MODEL["glass_rear_z"],
                    "touch_frame_front": VENDOR_DISPLAY_MODEL["touch_frame_front_z"],
                    "pcb_front": VENDOR_DISPLAY_MODEL["pcb_front_z"],
                    "pcb_rear": VENDOR_DISPLAY_MODEL["pcb_rear_z"],
                    "boss_rear": VENDOR_DISPLAY_MODEL["boss_rear_z"],
                    "rear_most": VENDOR_DISPLAY_MODEL["rear_most_z"],
                },
                "separations_mm": vendor_model_separations(),
                "note": ("these are the MODEL's own planes. They are not the vendor's stated "
                         "14.05 mm maximum and not the provisional 10.05 + 4.00 mm design "
                         "chain; no equation may be written between the three"),
            },
            "vendor_stated_maximum_mm": 14.05,
            "vendor_stated_maximum_source": "Waveshare wiki FAQ, overall thickness",
            "provisional_chain_mm": {
                "glass_front_to_pcb_rear": 10.05,
                "boss_projection": 4.00,
                "status": "PROVISIONAL design assumption (docs/mechanical-stack.md); it does "
                          "not reproduce the model's 6.24 mm glass-to-PCB separation and is "
                          "not a measurement",
            },
            "placed_illustrative_global_z_mm": placed_display_planes(),
            "placement": {
                "rotation_candidate_used": DISPLAY_ORIENTATION["candidate_used_for_drawing"],
                "rotation_alternative": DISPLAY_ORIENTATION["alternative_candidate"],
                "rotation_determinant": round(rotation_determinant(), 6),
                "glass_datum": "vendor glass front face at the panel front face, Z = 0",
                "disclosure": ("the model's touch/frame forward face is 1.90 mm in front of "
                               "the glass, so it sits at global Z = -1.90 mm: 1.90 mm proud "
                               "of the panel front face. Not flush, not a fit"),
            },
            "orientation": {
                "status": DISPLAY_ORIENTATION["status"],
                "why_not_proven": DISPLAY_ORIENTATION["why_not_proven"],
                "resolving_evidence": DISPLAY_ORIENTATION["resolving_evidence"],
                "not_asserted": DISPLAY_ORIENTATION["not_asserted"],
                "inner_pattern_candidates": inner_pattern_candidates(),
            },
            "connector": {
                "identification": "NOT ESTABLISHED",
                "tft1_flex_bonds": VENDOR_DISPLAY_MODEL["tft1_flex_bonds"],
                "candidate_at_drawing_position": VENDOR_DISPLAY_MODEL["display_cam"],
                "drawing_position": ("UNVERIFIED; the long-axis wording is disputed and the "
                                     "position/length arithmetic needs independent "
                                     "re-derivation"),
                "rear_front_frame_mapping": "UNVERIFIED",
            },
            "status": "ILLUSTRATIVE placement; every display gate stays pending",
        },
        "board_face_convention": {
            "requested": ("both boards' CAD-bottom faces toward the panel"),
            "implemented": False,
            "status": "PENDING owner decision",
            "conflict": (
                "the bare board models are plain slabs with no top/bottom feature marker. The "
                "existing front/UI board carries its SMT parts and the control actuators on "
                "top, and the rear/base board already has its SMT parts on the bottom. Moving "
                "either board's CAD bottom toward the panel is a real PCB layer/side "
                "reassignment, not a display transform: it would change the assembly, the "
                "silkscreen and every part placement"),
            "action": ("no PCB source, SMT side or stackup was changed. The request needs an "
                       "owner decision and a PCB revision before any model follows it"),
        },
        "flange_source": flange_source,
        "flange_projections": overlaps,
        "envelopes": envelopes,
        "pairs": pairs,
        "usbc_candidate": usbc_candidate,
        "light_pipe": {
            # One datum (panel FRONT face, +Z rearward), one parameterisation.
            # The nominal tip plane is the SHAFT length, not the overall length,
            # and the 2.55 mm to the board plane is arithmetic, not an LED gap.
            "part": data["lightPipe"]["part"],
            "candidate_status": data["lightPipe"]["status"],
            "source_url": data["lightPipe"]["sourceUrl"],
            "source_sha256": data["lightPipe"]["sourceSha256"],
            "shaft_length_mm": data["lightPipe"]["shaftLength"],
            "shaft_tolerance_mm": data["lightPipe"]["shaftLengthTolerance"],
            "overall_length_mm": data["lightPipe"]["overallLength"],
            "head_height_mm": data["lightPipe"]["headHeight"],
            "head_diameter_mm": data["lightPipe"]["headDiameter"],
            "shaft_diameter_mm": data["lightPipe"]["shaftDiameter"],
            "panel_hole_diameter_mm": data["lightPipe"]["panelHoleDiameter"],
            "nominal_tip_z_mm": data["lightPipeReach"]["nominalTipZ"],
            "nominal_tip_to_board_plane_mm": data["lightPipeReach"]["nominalTipToBoardPlane"],
            "optical_gap_mm": None,
            "status": "incomplete",
            "reason": ("the seated head plane, the LED height, the retention in 1.6 mm FR-4 "
                       "and the stack tolerances are unmeasured. 16.5 is the nominal tip "
                       "plane and 2.55 is the nominal tip-to-board-plane distance; neither "
                       "is an LED gap or a fit. The retired 10.16 mm / 7.29 mm subtraction "
                       "is withdrawn, not replaced by another subtraction"),
        },
    }


PRINT_CHECKLIST: tuple[str, ...] = (
    "Print at 1:1, 100 % scale, in millimetres. Do not let the slicer rescale.",
    "Measure the scale-check-100mm.stl bar with calipers: it must be 100.00 mm long. "
    "A 1 % scale error is 1 mm on the panel and rejects the print.",
    "Lay the panel mockup on the real panel drawing, front face up, and check the "
    "LCD aperture against the module glass (105.42 x 67.07 mm).",
    "Try every real part in the panel mockup: six buttons, three 4 mm light pipes, "
    "four MIDI jack barrels, the encoder bushing, NAV and POWER. All 20 apertures "
    "and 14 drills come from the canonical panel data.",
    "Offer the real display to the aperture from behind: the glass must pass with "
    "0.9 mm clearance per side. Record what you measure.",
    "Fit the Adafruit 908 flanges through their panel openings and measure the depth "
    "from flange seat to the rear-most body point. That number is missing today.",
    "Lay the front-board mockup over the real display bosses and check the four "
    "LCD mount holes; the top relief notch must clear all three feedthrough bodies.",
    "Check the light-pipe gauge on its own datum: the tall 19.05 mm end is the "
    "provisional board near face and the 16.5 mm end is the nominal PGN25L165 tip "
    "plane. The 2.55 mm step is arithmetic, NOT an LED gap and NOT a fit.",
    "Offer the real display to the panel aperture from behind and measure the touch "
    "layer, frame and glass: the model places the touch/frame plane 1.90 mm in FRONT "
    "of the glass datum, so the panel relief and seating are still unknown.",
    "Do NOT print or buy the four MODEL_FIT_SPACER_PLACEHOLDER_NON_APPROVED bodies: they "
    "are 5.0/2.7 mm geometric placeholders over the model's 8.81 mm gap, not a part. "
    "Measure the real boss plane, board face, standoff diameter and thread first.",
    "Do not record any result as accepted: every mockup is a print check, not "
    "dimensional evidence. Enter measurements in docs/evidence/ with a date and name.",
)


def print_checklist_markdown() -> str:
    lines = [
        "# MIDI Brain CAD mockups - physical print checklist",
        "",
        "PROVISIONAL. These mockups contain planning geometry from `mechanical/layout.json`,",
        "not measurements. A print check is not a fit approval.",
        "",
    ]
    lines += [f"{i}. {item}" for i, item in enumerate(PRINT_CHECKLIST, 1)]
    lines += ["", "Status: no item below is accepted. All 28 acceptance items stay pending.", ""]
    return "\n".join(lines)


if __name__ == "__main__":  # pragma: no cover - manual run
    for report in pair_reports():
        verdict = report.verdict
        print(f"{report.a} vs {report.b}: {verdict.status}"
              f" (clearance {verdict.clearance_mm}, overlap {verdict.overlap_mm})")
        for reason in verdict.reasons:
            print(f"    {reason}")
    for o in flange_overlaps(load_layout()):
        print(f"{o.feedthrough_id}: flange {o.flange.width:.2f} x {o.flange.height:.2f} mm, "
              f"board overlap {o.overlap_area_mm2:.3f} mm^2, "
              f"max X {o.max_overlap_x_mm:.2f} / Y {o.max_overlap_y_mm:.3f} mm -> UNDETERMINED")

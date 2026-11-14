#!/usr/bin/env python3
"""NAV keytop motion model: does the cap FOLLOW the stem inside the panel hole?

The owner asked for a measurable improvement of the NAV joystick cap, and for the
question "does it stay on?" to be analysed rather than asserted. This module is
the analysis. It is PURE geometry: no CadQuery, so it runs everywhere.

What it does NOT do
-------------------
It does not measure anything. Stem tilt, stem lateral movement, the pivot of that
movement, the printed shrinkage and the switch travel are all UNMEASURED, so they
are INPUTS with a range, never constants. Every result is a function of those
inputs, and every result is labelled "nominal" or "at the stated bound". Nothing
here says the joystick will work, that the cap grips, or that a sliding fit
retains anything.

The question it answers
----------------------
The cap is guided by a neck in a 7.2 mm panel hole, 1.6 mm deep. The stem is
D3.0 in a D3.2 socket, and the socket is blind (0.15 mm of axial free play). When
the stem tilts and translates, the cap must tilt and translate with it WITHOUT
the neck jamming in the hole. Two things limit that:

  * TILT: two bearing lands, one at each panel face, 1.6 mm apart. If each land
    has radial clearance cl, the cap can tilt by about atan((cl_front + cl_rear) /
    thickness) before both lands bind.
  * LATERAL: each land has cl of lateral freedom. A cap with a narrower neck has
    more lateral freedom and LESS guidance, so "narrower" is not automatically
    better - it trades binding risk against rock.

The variant
-----------
``NavProfile`` describes the neck as a stack of (z0, z1, diameter) bands, so both
parts are the same code path:

  * STANDARD: one band, D7.00 over the whole 1.6 mm panel thickness.
  * VARIANT (EXPERIMENTAL, not the standard): two 0.40 mm lands at D7.00 and a
    0.80 mm waist at D6.40 in between, plus a 0.50 mm lead-in at the head edge.

Why the variant is the candidate: a printed 1.6 mm long D7.00 cylinder needs the
whole bore of the hole to be clear. FDM over-extrusion and an elephant's foot put
the tightest material at ONE face, while a drilled/routed FR-4 hole can be
undersize anywhere along its length. The waist relieves the middle 0.80 mm by
0.30 mm radially, so only the two 0.40 mm lands have to clear, and the lands are
where the hole is measured. The TILT limit is unchanged, because both lands keep
the standard diameter: the variant trades nothing there. Whether that matters on
a real part is a first-article measurement.

Both profiles are evaluated in FOUR lateral directions (because the panel hole
and the print are not axisymmetric in their errors) and for the CENTRE PUSH, and
negative controls prove the model rejects an oversized neck and a bad print.
"""
from __future__ import annotations

import math
from dataclasses import dataclass, field

from printed_geometry import GeometryError


class NavFitError(ValueError):
    """An impossible input, or a profile that cannot be built."""


# ---------------------------------------------------------------------------
# Unmeasured inputs: ranges, never constants
# ---------------------------------------------------------------------------
# Every field here is a PARAMETER RANGE the first article has to replace with a
# measurement. The nominal value is only a placeholder inside the range.
STEM_TILT_DEG = (0.0, 0.0, 1.0)          # (min, nominal, max) - ALPS catalog: none
STEM_LATERAL_MM = (0.0, 0.0, 0.15)       # stem side movement, not published
# Drawing No.2 labels "Center of stem rotation 0.49". Its ENDPOINTS and its sign
# are NOT established, so it is NOT used as a pivot here: inserting it into the
# PANEL frame (front 0, rear 1.6) would silently invent a measured pivot, and the
# switch itself sits near PCB Z 10.6. The model therefore takes a pivot that is
# EXPLICITLY HYPOTHETICAL in the panel frame, and the label is recorded beside it.
STEM_ROTATION_CENTRE_LABEL_MM = 0.49
STEM_PIVOT_DATUM = ("HYPOTHETICAL panel-frame pivot (panel front = 0, rear = 1.6). "
                    "The ALPS 'Center of stem rotation 0.49' label is NOT converted: "
                    "its endpoints and sign are unverified")
STEM_PIVOT_Z_MM = (0.0, 0.0, 1.6)
PRINT_SHRINK_MM = (0.0, 0.0, 0.10)       # radial print error on the neck (FDM)
HOLE_SHRINK_MM = (0.0, 0.0, 0.10)        # radial FR-4 hole error (drill/rout)
# ALPS SKRH catalog drawing No.1: 0.25 mm in the 4 directions, 0.15 mm centre.
# The 4-direction figure's measurement HEIGHT is SPECIFIED by the exact part's
# product page: "4-directional operating force and travel are measured at 4.3mm
# height from the bottom of the switch." The drawing's four arrows sit at +-45
# deg to the part's axes, so the model checks the AXES and the DIAGONALS: a
# direction the drawing names must not be the only one that passes.
TRAVEL_DIRECTION_MM = 0.25
TRAVEL_CENTRE_MM = 0.15
TRAVEL_MEASUREMENT_POINT = ("SPECIFIED: the ALPS product page for SKRHABE010 says, "
                            "verbatim, '4-directional operating force and travel are "
                            "measured at 4.3mm height from the bottom of the switch.' "
                            "The centre-push figure has no stated height")
DIRECTIONS = ("+X", "-X", "+Y", "-Y", "+X+Y", "+X-Y", "-X+Y", "-X-Y")


@dataclass(frozen=True)
class NeckBand:
    """One axial band of the neck: a diameter over a Z span (panel Z, +Z rear)."""

    name: str
    z0: float
    z1: float
    diameter: float

    def __post_init__(self) -> None:
        if self.z1 <= self.z0:
            raise NavFitError(f"{self.name}: z1 {self.z1} is not behind z0 {self.z0}")
        if self.diameter <= 0.0:
            raise NavFitError(f"{self.name}: diameter must be positive")


@dataclass(frozen=True)
class NavProfile:
    """A neck profile: bands between the panel FRONT and REAR faces.

    ``head_diameter`` is the leading diameter that must pass the aperture from the
    REAR, so it may never exceed the aperture. ``lead_in`` is a chamfer at the
    head's edge; it reduces the entering diameter by ``lead_in`` over that length.
    """

    name: str
    bands: tuple[NeckBand, ...]
    head_diameter: float
    lead_in: float = 0.0
    status: str = "STANDARD"

    def __post_init__(self) -> None:
        if not self.bands:
            raise NavFitError(f"{self.name}: no bands")
        spans = sorted((b.z0, b.z1) for b in self.bands)
        for (a0, a1), (b0, b1) in zip(spans, spans[1:]):
            if b0 < a1 - 1e-9:
                raise NavFitError(f"{self.name}: bands overlap ({a0}..{a1} and {b0}..{b1})")
        for band in self.bands:
            if band.diameter > self.head_diameter + 1e-9:
                raise NavFitError(
                    f"{self.name}: {band.name} D{band.diameter} is wider than the head "
                    f"D{self.head_diameter}, so it could not be fitted through the aperture")

    def width(self) -> float:
        return sum(b.z1 - b.z0 for b in self.bands)

    def bands_at(self, z: float) -> tuple[NeckBand, ...]:
        return tuple(b for b in self.bands if b.z0 - 1e-9 <= z <= b.z1 + 1e-9)

    def diameter_at(self, z: float) -> float:
        for band in self.bands:
            if band.z0 - 1e-9 <= z <= b_z1(band) + 1e-9:
                return band.diameter
        raise NavFitError(f"{self.name}: no band at z {z}")

    @property
    def min_diameter(self) -> float:
        return min(b.diameter for b in self.bands)

    @property
    def max_diameter(self) -> float:
        return max(b.diameter for b in self.bands)


def b_z1(band: NeckBand) -> float:
    return band.z1


@dataclass(frozen=True)
class NavFitInputs:
    """Every unmeasured number the analysis needs, with its range."""

    hole_diameter: float
    panel_front_z: float
    panel_rear_z: float
    head_diameter: float
    flange_diameter: float
    socket_bore: float
    stem_diameter: float
    socket_depth: float
    axial_free_play: float
    stem_tilt_deg: tuple[float, float, float] = STEM_TILT_DEG
    stem_lateral_mm: tuple[float, float, float] = STEM_LATERAL_MM
    stem_pivot_z_mm: tuple[float, float, float] = STEM_PIVOT_Z_MM
    print_shrink_mm: tuple[float, float, float] = PRINT_SHRINK_MM
    hole_shrink_mm: tuple[float, float, float] = HOLE_SHRINK_MM
    travel_direction_mm: float = TRAVEL_DIRECTION_MM
    travel_centre_mm: float = TRAVEL_CENTRE_MM
    # A measured or explicitly assumed cap-travel limit. None means "not known",
    # and then no availability claim is made.
    cap_travel_limit_mm: float | None = None

    @property
    def thickness(self) -> float:
        return self.panel_rear_z - self.panel_front_z

    @property
    def socket_clearance(self) -> float:
        return self.socket_bore - self.stem_diameter

    @property
    def socket_looseness(self) -> dict:
        from printed_geometry import nav_socket_looseness
        return nav_socket_looseness(self.socket_bore)


def _range(value: tuple[float, float, float], what: str) -> tuple[float, float, float]:
    lo, nominal, hi = value
    if not (lo <= nominal <= hi):
        raise NavFitError(f"{what}: {value} is not an ordered (min, nominal, max) range")
    return value


def standard_profile(g) -> NavProfile:
    """The part that is committed today: one D7.00 band over the panel thickness."""
    return NavProfile(
        name="standard: one band over the panel",
        bands=(NeckBand("panel", g.stack.panel_front_z, g.stack.panel_rear_z,
                        g.neck_diameter),),
        head_diameter=g.neck_diameter,
        lead_in=0.0,
        status="STANDARD",
    )


def variant_profile(g, waist_diameter: float = 6.4, land: float = 0.40,
                    lead_in: float = 0.50) -> NavProfile:
    """The PROPOSED variant: two lands, a relieved waist, a lead-in.

    All three numbers are CHOICES, not datasheet values, and the variant is
    EXPERIMENTAL: it is not the standard, nothing has been printed with it, and
    the first article has to compare it with the standard part.
    """
    if waist_diameter >= g.neck_diameter:
        raise NavFitError(f"the waist D{waist_diameter} is not narrower than the neck "
                          f"D{g.neck_diameter}: it would relieve nothing")
    if not 0.05 <= lead_in <= g.neck_diameter / 2:
        raise NavFitError(f"lead-in {lead_in} is not a printable edge break")
    z0, z1 = g.stack.panel_front_z, g.stack.panel_rear_z
    if 2 * land >= (z1 - z0):
        raise NavFitError(f"two {land} mm lands do not fit the {z1 - z0} mm panel")
    return NavProfile(
        name=f"variant: lands {land} mm at D{g.neck_diameter}, waist D{waist_diameter}",
        bands=(NeckBand("front land", z0, z0 + land, g.neck_diameter),
               NeckBand("waist", z0 + land, z1 - land, waist_diameter),
               NeckBand("rear land", z1 - land, z1, g.neck_diameter)),
        head_diameter=g.neck_diameter,
        lead_in=lead_in,
        status="EXPERIMENTAL VARIANT, not the standard",
    )


def land_clearance(profile: NavProfile, inputs: NavFitInputs,
                   print_shrink: float, hole_shrink: float) -> list[dict]:
    """Radial clearance of every band, at a given print and hole error.

    Positive = the band clears the hole; negative = interference, i.e. the part
    cannot enter. The two lands are what the part is guided by, so each band is
    reported separately: that is what makes a waist meaningful.
    """
    out = []
    for band in profile.bands:
        effective_neck = band.diameter + 2.0 * print_shrink
        effective_hole = inputs.hole_diameter - 2.0 * hole_shrink
        out.append({
            "band": band.name,
            "z_mm": [band.z0, band.z1],
            "diameter_mm": band.diameter,
            "effective_neck_mm": round(effective_neck, 4),
            "effective_hole_mm": round(effective_hole, 4),
            "radial_clearance_mm": round((effective_hole - effective_neck) / 2, 4),
        })
    return out


def tilt_limit_deg(profile: NavProfile, inputs: NavFitInputs,
                   print_shrink: float = 0.0, hole_shrink: float = 0.0) -> float:
    """The angle at which two bands, one at each panel face, both touch.

    ``atan((cl_front + cl_rear) / thickness)`` where cl is the radial clearance of
    the band at that face. A profile whose middle is narrower does not change this
    number: the two faces still set it.
    """
    clearances = land_clearance(profile, inputs, print_shrink, hole_shrink)
    front = [c for c in clearances if abs(c["z_mm"][0] - inputs.panel_front_z) < 1e-9]
    rear = [c for c in clearances if abs(c["z_mm"][1] - inputs.panel_rear_z) < 1e-9]
    if not front or not rear:
        raise NavFitError(f"{profile.name}: no band reaches each panel face, so the "
                          "profile cannot be guided by both faces")
    cl_front = front[0]["radial_clearance_mm"]
    cl_rear = rear[0]["radial_clearance_mm"]
    if cl_front <= 0.0 or cl_rear <= 0.0:
        return 0.0
    return math.degrees(math.atan((cl_front + cl_rear) / inputs.thickness))


def evaluate(profile: NavProfile, inputs: NavFitInputs, *,
             print_shrink: float | None = None,
             hole_shrink: float | None = None,
             stem_tilt_deg: float | None = None,
             stem_lateral_mm: float | None = None,
             pivot_z: float | None = None) -> dict:
    """One configuration: can the cap follow the stem without jamming?

    Nominal values are used when a keyword is omitted. The result reports the
    coincidence of the four lateral directions, the centre push, and a
    ``binds`` flag per direction. It never reports a grip or a retention.
    """
    print_shrink = inputs.print_shrink_mm[1] if print_shrink is None else print_shrink
    hole_shrink = inputs.hole_shrink_mm[1] if hole_shrink is None else hole_shrink
    tilt = inputs.stem_tilt_deg[1] if stem_tilt_deg is None else stem_tilt_deg
    lateral = inputs.stem_lateral_mm[1] if stem_lateral_mm is None else stem_lateral_mm

    if profile.head_diameter > inputs.hole_diameter + 1e-9:
        raise NavFitError(f"{profile.name}: the head D{profile.head_diameter} cannot "
                          f"pass the D{inputs.hole_diameter} aperture")

    bands = land_clearance(profile, inputs, print_shrink, hole_shrink)
    limit = tilt_limit_deg(profile, inputs, print_shrink, hole_shrink)
    # Lateral freedom of the tightest land. It is NOT the binding test on its own:
    # a tilted cap offsets its two ends in OPPOSITE directions, so the two ends
    # must be checked separately at the height they pass through the panel.
    lateral_freedom = min(c["radial_clearance_mm"] for c in bands)
    # HYPOTHETICAL panel-frame pivot; see STEM_PIVOT_DATUM. Nothing converts the
    # ALPS 0.49 label into this frame.
    pivot = inputs.stem_pivot_z_mm[1] if pivot_z is None else pivot_z

    # The cap is a rigid body: its axis passes through (lateral, pivot) at angle
    # `tilt`. Its offset at a height z is  lateral + tan(tilt) * (z - pivot).
    # That single expression is the physics; testing "lateral" and "tilt"
    # independently lets the model report no binding while the geometry collides.
    def offset_at(z: float) -> float:
        return lateral + math.tan(math.radians(tilt)) * (z - pivot)

    front_band = [c for c in bands
                  if abs(c["z_mm"][0] - inputs.panel_front_z) < 1e-9][0]
    rear_band = [c for c in bands
                 if abs(c["z_mm"][1] - inputs.panel_rear_z) < 1e-9][0]
    front_clearance = front_band["radial_clearance_mm"]
    rear_clearance = rear_band["radial_clearance_mm"]
    front_offset = offset_at(inputs.panel_front_z)
    rear_offset = offset_at(inputs.panel_rear_z)

    # A TILTED cylinder does not meet the panel plane in a circle: its section is
    # an ellipse, so the radius in the tilt direction grows from r to r/cos(tilt).
    # At 5 deg with r 3.5 that is +0.0134 mm - the same order as the clearance
    # itself - so it is SUBTRACTED here. The model is therefore a FIRST-ORDER
    # centreline + oblique-section bound, not an exact contact solution.
    def growth(r_mm: float) -> float:
        if tilt == 0.0:
            return 0.0
        return r_mm * (1.0 / math.cos(math.radians(abs(tilt))) - 1.0)

    front_growth = growth(front_band["diameter_mm"] / 2)
    rear_growth = growth(rear_band["diameter_mm"] / 2)
    front_clearance_eff = round(front_clearance - front_growth, 4)
    rear_clearance_eff = round(rear_clearance - rear_growth, 4)

    directions = {}
    for direction in DIRECTIONS:
        # The four axes and the four diagonals are geometric mirrors: the SIGN of
        # the offset is the only thing a direction changes. That is why every one
        # is reported - a single "+X only" report hides nothing, but it also
        # proves nothing about the others.
        sign = 1.0 if direction.startswith("+") else -1.0
        front = sign * front_offset
        rear = sign * rear_offset
        directions[direction] = {
            "front_face_offset_mm": round(front, 4),
            "front_face_clearance_mm": front_clearance_eff,
            "front_face_margin_mm": round(front_clearance_eff - abs(front), 4),
            "rear_face_offset_mm": round(rear, 4),
            "rear_face_clearance_mm": rear_clearance_eff,
            "rear_face_margin_mm": round(rear_clearance_eff - abs(rear), 4),
            "oblique_section_growth_mm": {
                "front": round(front_growth, 4), "rear": round(rear_growth, 4)},
            "tilt_required_deg": round(tilt, 4),
            "tilt_available_deg": round(limit, 4),
            "tilt_margin_deg": round(limit - tilt, 4),
            "first_order_collision": (front_clearance_eff - abs(front)) < 0.0
                                     or (rear_clearance_eff - abs(rear)) < 0.0,
        }

    # Centre push. At rest the socket floor floats `free_play` above the stem top,
    # so the FIRST part of any press only takes up that gap: it is LOST MOTION,
    # not pre-load. Pre-load exists only when the gap is zero or negative, i.e.
    # when the floor already touches the stem (free_play <= 0), and then the
    # switch is loaded before anyone presses it.
    # The cap does not have a fixed motion budget shared with the switch: it must
    # move the free play AND then the switch's travel, so the requirement is their
    # SUM. Whether that much cap motion is AVAILABLE needs a measured/assumed cap
    # travel limit; without one the answer is UNKNOWN, not "blocked".
    required_displacement = round(max(0.0, inputs.axial_free_play)
                                  + inputs.travel_centre_mm, 4)
    if inputs.cap_travel_limit_mm is None:
        available = "UNKNOWN: no measured or assumed cap-travel limit"
    elif inputs.cap_travel_limit_mm >= required_displacement:
        available = "YES at the stated cap-travel limit"
    else:
        available = "NO at the stated cap-travel limit"
    centre = {
        "free_play_mm": round(inputs.axial_free_play, 4),
        "travel_required_mm": inputs.travel_centre_mm,
        "lost_motion_mm": round(max(0.0, inputs.axial_free_play), 4),
        "pre_load_mm": round(max(0.0, -inputs.axial_free_play), 4),
        # free play + switch travel: the cap's total displacement requirement
        "required_cap_displacement_mm": required_displacement,
        "cap_travel_limit_mm": inputs.cap_travel_limit_mm,
        "cap_travel_available": available,
        "socket_floor_touches_stem_at_rest": inputs.axial_free_play <= 0.0,
    }

    # Directional travel: the stem moves up to 0.25 mm sideways. The cap follows
    # only if the neck's lateral freedom covers it; the socket's looseness over
    # the REAL stem tip adds an uncertainty that is much wider than the nominal
    # 0.10 mm per side (drawing No.2's tip is a rounded square, not a circle).
    loose = inputs.socket_looseness
    directional = {
        "stem_travel_mm": inputs.travel_direction_mm,
        "travel_measurement_point": TRAVEL_MEASUREMENT_POINT,
        "neck_lateral_freedom_mm": round(lateral_freedom, 4),
        "socket_clearance_mm": round(inputs.socket_clearance, 4),
        "socket_looseness_min_mm": loose["looseness_min_mm"],
        "socket_looseness_max_mm": loose["looseness_max_mm"],
        "follows_within_neck_mm": round(lateral_freedom - inputs.travel_direction_mm, 4),
        # With the LOOSEST reading of the tip the socket eats the whole 0.25 mm
        # travel, so the cap can lag the stem: report the number, claim nothing.
        "stick_slip_range_mm": [
            round(max(0.0, inputs.travel_direction_mm - loose["looseness_max_mm"]), 4),
            round(max(0.0, inputs.travel_direction_mm - loose["looseness_min_mm"]), 4),
        ],
    }

    return {
        "profile": profile.name,
        "profile_status": profile.status,
        "panel_thickness_mm": round(inputs.thickness, 4),
        "head_diameter_mm": profile.head_diameter,
        "hole_diameter_mm": inputs.hole_diameter,
        "print_shrink_mm": print_shrink,
        "hole_shrink_mm": hole_shrink,
        "stem_tilt_deg": tilt,
        "stem_lateral_mm": lateral,
        "stem_pivot_z_mm": round(pivot, 4),
        "stem_pivot_datum": STEM_PIVOT_DATUM,
        "face_offsets_mm": {"front": round(front_offset, 4),
                            "rear": round(rear_offset, 4)},
        "bands": bands,
        "tightest_radial_clearance_mm": round(lateral_freedom, 4),
        "tilt_limit_deg": round(limit, 4),
        "directions": directions,
        "centre_push": centre,
        "directional_travel": directional,
        "any_binding": any(d["first_order_collision"] for d in directions.values()),
        "verdict": ("FIRST-ORDER COLLISION at the stated inputs"
                    if any(d["first_order_collision"] for d in directions.values())
                    else "no first-order collision at the stated inputs "
                         "(ILLUSTRATIVE bound, not a physical conclusion)"),
        "model_status": ("FIRST-ORDER BOUND ONLY: a centreline offset plus the "
                         "oblique-section radius growth r(1/cos tilt - 1). It is not "
                         "an exact contact solution and it cannot certify that the "
                         "cap does not bind or rock"),
        "not_claimed": [
            "that the cap grips or is retained by the stem",
            "that sliding clearance yields retention",
            "that the joystick works, or that any direction actuates",
            "any measured stem tilt, lateral movement, pivot or print shrinkage",
            "any FDM dimensional result: the shrink terms are parameters",
        ],
    }


def local_defect(profile: NavProfile, inputs: NavFitInputs, defect_mm: float,
                 z: float, print_shrink: float = 0.0,
                 hole_shrink: float = 0.0) -> dict:
    """One local radial error at one Z: does it jam the neck there?

    A real hole or print is not uniformly wrong: an elephant's foot, a burr or a
    locally undersize FR-4 hole is a defect at ONE Z, not along the whole 1.6 mm.
    The band at that Z decides whether the cap still passes.
    """
    if not 0.0 <= z <= inputs.thickness:
        raise NavFitError(f"defect at z {z} is outside the {inputs.thickness} mm panel")
    bands = profile.bands_at(inputs.panel_front_z + z)
    if not bands:
        raise NavFitError(f"{profile.name}: no band at z {z}")
    band = bands[0]
    clearance = ((inputs.hole_diameter - 2.0 * hole_shrink)
                 - (band.diameter + 2.0 * print_shrink)) / 2
    return {
        "z_mm": round(z, 3),
        "band": band.name,
        "band_diameter_mm": band.diameter,
        "clearance_mm": round(clearance, 4),
        "local_growth_mm": round(defect_mm, 3),
        "jam": (clearance - defect_mm) < 0.0,
    }


def analyse(g, variant_waist: float = 6.4, variant_land: float = 0.40,
            defect_mm: float = 0.15) -> dict:
    """The whole comparison: standard vs variant, nominal and at the bounds."""
    from printed_geometry import load_layout, nav_keytop, panel_stack
    data = load_layout() if g is None else None
    g = g if g is not None else nav_keytop(data)
    inputs = NavFitInputs(
        hole_diameter=g.aperture_diameter,
        panel_front_z=g.stack.panel_front_z,
        panel_rear_z=g.stack.panel_rear_z,
        head_diameter=g.neck_diameter,
        flange_diameter=g.flange_diameter,
        socket_bore=g.socket_bore,
        stem_diameter=g.stem_diameter,
        socket_depth=g.socket_depth,
        axial_free_play=g.free_play,
    )
    standard = standard_profile(g)
    variant = variant_profile(g, variant_waist, variant_land)
    print_shrink_hi = inputs.print_shrink_mm[2]
    hole_shrink_hi = inputs.hole_shrink_mm[2]
    tilt_hi = inputs.stem_tilt_deg[2]
    lateral_hi = inputs.stem_lateral_mm[2]
    return {
        "inputs": {
            "hole_diameter_mm": inputs.hole_diameter,
            "panel_thickness_mm": round(inputs.thickness, 4),
            "socket_clearance_mm": round(inputs.socket_clearance, 4),
            "stem_tilt_deg_range": list(inputs.stem_tilt_deg),
            "stem_lateral_mm_range": list(inputs.stem_lateral_mm),
            "stem_pivot_z_mm_range": list(inputs.stem_pivot_z_mm),
            "print_shrink_mm_range": list(inputs.print_shrink_mm),
            "hole_shrink_mm_range": list(inputs.hole_shrink_mm),
            "stem_rotation_centre_label_mm": STEM_ROTATION_CENTRE_LABEL_MM,
            "stem_pivot_datum": STEM_PIVOT_DATUM,
            "travel_measurement_point": TRAVEL_MEASUREMENT_POINT,
            "socket_looseness": inputs.socket_looseness,
        },
        "standard_nominal": evaluate(standard, inputs),
        "standard_worst_case": evaluate(standard, inputs, print_shrink=print_shrink_hi,
                                        hole_shrink=hole_shrink_hi, stem_tilt_deg=tilt_hi,
                                        stem_lateral_mm=lateral_hi),
        "variant_nominal": evaluate(variant, inputs),
        "variant_worst_case": evaluate(variant, inputs, print_shrink=print_shrink_hi,
                                       hole_shrink=hole_shrink_hi, stem_tilt_deg=tilt_hi,
                                       stem_lateral_mm=lateral_hi),
        "standard_profile": standard,
        "variant_profile": variant,
        # One local defect in the MIDDLE of the panel: the standard part has the
        # full D7.00 there, the variant has 0.30 mm of radial relief.
        "defect_mm": defect_mm,
        "standard_mid_defect": local_defect(standard, inputs, defect_mm,
                                            inputs.thickness / 2),
        "variant_mid_defect": local_defect(variant, inputs, defect_mm,
                                           inputs.thickness / 2),
    }


def report(g=None) -> list[str]:
    """Print-ready lines for the analysis."""
    a = analyse(g)
    lines = [
        f"hole D{a['inputs']['hole_diameter_mm']:.2f} in a "
        f"{a['inputs']['panel_thickness_mm']:.2f} mm panel; nominal socket clearance "
        f"{a['inputs']['socket_clearance_mm']:.2f} mm",
        f"socket looseness over the REAL tip "
        f"{a['inputs']['socket_looseness']['looseness_min_mm']:.2f}..."
        f"{a['inputs']['socket_looseness']['looseness_max_mm']:.2f} mm per side "
        "(drawing No.2: rounded square 1.95 +-0.05 at the tip, R0.5 corners, Ø3 in "
        "the side view); stem rotation centre label 0.49, endpoints unverified",
        f"standard (nominal): tilt limit {a['standard_nominal']['tilt_limit_deg']:.3f} deg, "
        f"tightest lateral freedom "
        f"{a['standard_nominal']['tightest_radial_clearance_mm']:.3f} mm, "
        f"{a['standard_nominal']['verdict']}",
        "model: FIRST-ORDER bound only (centreline offset + oblique-section radius "
        "growth); no physical no-binding conclusion is claimed",
        f"variant  (nominal): tilt limit {a['variant_nominal']['tilt_limit_deg']:.3f} deg, "
        f"tightest lateral freedom "
        f"{a['variant_nominal']['tightest_radial_clearance_mm']:.3f} mm, "
        f"{a['variant_nominal']['verdict']}",
        f"standard (bounds): {a['standard_worst_case']['verdict']}",
        f"variant  (bounds): {a['variant_worst_case']['verdict']}",
        f"one local {a['defect_mm']:.2f} mm radial defect at mid-panel: standard "
        f"{a['standard_mid_defect']['clearance_mm']:+.3f} mm -> "
        f"{'JAM' if a['standard_mid_defect']['jam'] else 'passes'}, variant "
        f"{a['variant_mid_defect']['clearance_mm']:+.3f} mm -> "
        f"{'JAM' if a['variant_mid_defect']['jam'] else 'passes'}",
    ]
    for direction, value in a["standard_nominal"]["directions"].items():
        lines.append(f"  standard {direction}: front margin "
                     f"{value['front_face_margin_mm']:+.3f} mm (offset "
                     f"{value['front_face_offset_mm']:+.3f}), rear margin "
                     f"{value['rear_face_margin_mm']:+.3f} mm (offset "
                     f"{value['rear_face_offset_mm']:+.3f})")
    lines.append("NOT measured and NOT a verdict: the stem tilt, its lateral "
                 "movement and pivot, the print shrinkage, the FR-4 hole error and "
                 "any grip or retention")
    return lines


if __name__ == "__main__":  # pragma: no cover - manual run
    for line in report():
        print(line)

#!/usr/bin/env python3
"""Light-pipe optical gap: one datum, one parameterisation, no length fallback.

HISTORY - ALL OF THIS IS WITHDRAWN AND UNUSED: the retired subtraction used the
retired Bivar PLP2-400-C length 10.16 mm and the 17.45 mm panel-rear gap, and
printed the withdrawn 7.29 mm difference as the withdrawn "unaccounted" figure.
That subtraction compared two different reference planes, and the withdrawn
10.16 mm reference is not in the current Bivar datasheet. Nothing replaces it:
17.45 - 16.5 or 17.45 - 17.5 would be equally meaningless.

The owner-selected candidate is the Signal Construct PGN25L165 (drawing
https://static.rapidonline.com/pdf/07-1015_v1.pdf, SHA-256
5350ad1f0a795e0d688dc74e223239c82cfb3e27951244a10cb43873eeeaa46b):
shaft Ø2.5 ±0.05 mm, x = 16.5 ±0.1 mm from the head underside to the tip, head
Ø3 ±0.05 mm, head height 1.0 mm, mounting hole Ø2.6 −0.05 mm (2.55-2.60 mm). The
owner's screenshot for this MPN states 17.5 mm overall, which is 16.5 + 1.0.

Datum: Z = 0 at the panel FRONT face; +Z points rearward (into the rack).

    head underside plane   H = 0 + head_underside_offset      ASSUMPTION: 0
    pipe tip plane         T = H + shaft_length                nominal 16.5
    board near face        B = PROVISIONAL_Z.frontBoardTop     10.6, owner-directed
                                                               and provisional
    LED top plane          L = B - led_height                  the LED rises
                                                               TOWARD the panel,
                                                               i.e. in -Z, so its
                                                               offset is NEGATIVE
    optical gap            g = L - T
    signed tip to board    t = B - T                           nominal -5.9: NEGATIVE
                                                               means the tip plane is
                                                               REARWARD of the board
                                                               face, i.e. the pipe
                                                               would cross it

`optical_gap` is **None** and the status is **incomplete** until the head
underside plane, the shaft length with its tolerance, the board near face, the
LED height and the stack tolerances are all recorded. There is no "gap minus
pipe length" step: this module cannot produce one.

Even a complete result is labelled ``arithmetic_only``. It says nothing about
brightness, coupling, retention in 1.6 mm FR-4, the real panel hole tolerance or
any physical fit. The +-0.1 mm shaft tolerance alone never yields a bound: the
stack tolerance is unknown.

``tip_to_board_plane_mm`` is SIGNED as board minus tip, so the direction survives:
at the current stack the 16.5 mm tip plane lies 5.9 mm behind the 10.6 mm board face
under the two assumed planes. That is arithmetic, not a measured interference, not
an optical gap and not a fit; ``compute_reach`` never returns one.

Units: millimetres. No CAD library, so the rules run everywhere.
"""
from __future__ import annotations

import math
from dataclasses import dataclass

from bracket_geometry import load_layout

# Status vocabulary. `incomplete` is the normal state today.
INCOMPLETE = "incomplete"
ARITHMETIC_ONLY = "arithmetic_only"

# Claim PHRASES that would turn arithmetic into a fit claim. Bare words such as
# "bound" are allowed in a disclaimer ("never yields a bound"); these phrases are
# not.
FORBIDDEN_CLAIMS = ("is guaranteed", "will fit", "fits the panel", "fits the led",
                    "clearance is verified", "therefore fits", "at most", "at least",
                    "worst case", "best case", "proven fit")


class ReachInputError(ValueError):
    """An input is physically impossible or not a finite interval."""


@dataclass(frozen=True)
class Interval:
    """Closed interval ``[lo, hi]`` in mm, inclusive on both ends."""

    lo: float
    hi: float

    def __post_init__(self) -> None:
        for name, value in (("lo", self.lo), ("hi", self.hi)):
            if not isinstance(value, (int, float)) or isinstance(value, bool):
                raise ReachInputError(f"{name} must be a number, got {value!r}")
            if not math.isfinite(float(value)):
                raise ReachInputError(f"{name} must be finite, got {value!r}")
        if self.lo > self.hi:
            raise ReachInputError(f"interval is inverted: {self.lo} > {self.hi}")

    @classmethod
    def point(cls, value: float) -> "Interval":
        return cls(value, value)

    @classmethod
    def around(cls, nominal: float, tolerance: float) -> "Interval":
        """``nominal +- tolerance``; a negative tolerance is an error."""
        if tolerance < 0:
            raise ReachInputError(f"tolerance must not be negative: {tolerance}")
        return cls(nominal - tolerance, nominal + tolerance)

    @property
    def nominal(self) -> float:
        return (self.lo + self.hi) / 2

    @property
    def width(self) -> float:
        return self.hi - self.lo

    def __add__(self, other: "Interval") -> "Interval":
        return Interval(self.lo + other.lo, self.hi + other.hi)

    def __sub__(self, other: "Interval") -> "Interval":
        return Interval(self.lo - other.hi, self.hi - other.lo)

    def contains(self, value: float, eps: float = 0.0) -> bool:
        return self.lo - eps <= value <= self.hi + eps

    def __str__(self) -> str:
        return f"[{self.lo:.3f}, {self.hi:.3f}] mm"


@dataclass(frozen=True)
class ReachInputs:
    """Every plane the optical gap needs, each independently unknown-able.

    ``head_underside_offset_mm``  panel FRONT face -> head underside, +Z rearward
                                  (None = the seated position is not measured)
    ``shaft_length_mm``           head underside -> tip, nominal 16.5
    ``board_near_face_z_mm``      panel FRONT face -> the PCB face the LED sits on
    ``led_height_mm``             LED body height above that face, rising TOWARD
                                  the panel, so it SUBTRACTS from the board plane
    ``stack_tolerance_mm``        measured +/- of the whole stack, not the shaft
    """

    head_underside_offset_mm: Interval | None
    shaft_length_mm: Interval | None
    board_near_face_z_mm: Interval | None
    led_height_mm: Interval | None
    stack_tolerance_mm: Interval | None

    @classmethod
    def from_layout(cls, layout: dict | None = None) -> "ReachInputs":
        """The documented values only: the seated plane, LED and tolerances are unknown."""
        data = layout if layout is not None else load_layout()
        pipe = data["lightPipe"]
        return cls(
            head_underside_offset_mm=None,
            shaft_length_mm=Interval.around(float(pipe["shaftLength"]),
                                           float(pipe["shaftLengthTolerance"])),
            board_near_face_z_mm=Interval.point(float(data["provisionalZ"]["frontBoardTop"])),
            led_height_mm=None,
            stack_tolerance_mm=None,
        )


@dataclass(frozen=True)
class ReachResult:
    """Nominal planes are documented numbers; measured planes are intervals/None.

    ``nominal_*`` come from the documented model plus the seated-position
    ASSUMPTION. ``tip_z_mm`` / ``tip_to_board_plane_mm`` require a MEASURED seat
    and stay None until one exists. ``optical_gap_mm`` also needs the LED height
    and the stack tolerance.
    """

    status: str
    complete: bool
    missing: tuple[str, ...]
    nominal_tip_z_mm: float
    nominal_tip_to_board_plane_mm: float
    tip_z_mm: Interval | None
    tip_to_board_plane_mm: Interval | None
    optical_gap_mm: Interval | None
    notes: tuple[str, ...]

    @property
    def verdict(self) -> None:
        """Always None: this module never returns a fit or optical verdict."""
        return None


def nominal_tip_z(layout: dict | None = None) -> float:
    """The nominal tip plane under the head-underside-on-panel-front ASSUMPTION."""
    data = layout if layout is not None else load_layout()
    return round(float(data["lightPipe"]["shaftLength"]), 6)


def nominal_tip_to_board_plane(layout: dict | None = None) -> float:
    """Nominal tip to the provisional board near face - arithmetic, not a gap to an LED."""
    data = layout if layout is not None else load_layout()
    return round(float(data["provisionalZ"]["frontBoardTop"])
                 - float(data["lightPipe"]["shaftLength"]), 6)


def compute_reach(inputs: ReachInputs) -> ReachResult:
    """Compute the planes, and the optical gap ONLY when every input is recorded."""
    if inputs.shaft_length_mm is not None and inputs.shaft_length_mm.lo <= 0:
        raise ReachInputError(f"shaft length must be positive: {inputs.shaft_length_mm}")
    if inputs.led_height_mm is not None and inputs.led_height_mm.lo < 0:
        raise ReachInputError(f"LED height must not be negative: {inputs.led_height_mm}")
    if inputs.stack_tolerance_mm is not None and inputs.stack_tolerance_mm.lo < 0:
        raise ReachInputError(
            f"a stack tolerance magnitude cannot be negative: {inputs.stack_tolerance_mm}")

    missing: list[str] = []
    for name, value in (
        ("head_underside_offset_mm", inputs.head_underside_offset_mm),
        ("shaft_length_mm", inputs.shaft_length_mm),
        ("board_near_face_z_mm", inputs.board_near_face_z_mm),
        ("led_height_mm", inputs.led_height_mm),
        ("stack_tolerance_mm", inputs.stack_tolerance_mm),
    ):
        if value is None:
            missing.append(name)

    notes = [
        "the datum is the panel FRONT face, Z = 0, with +Z rearward",
        "the head underside on the panel front face is an ASSUMPTION, not a measurement",
        "the LED rises TOWARD the panel, so its height SUBTRACTS from the board plane",
        "no 'gap minus pipe length' subtraction exists: the retired 7.29 mm compared "
        "different reference planes and is withdrawn",
    ]

    # Planes that are computable without the unknowns.
    tip = None
    tip_to_board = None
    if inputs.head_underside_offset_mm is not None and inputs.shaft_length_mm is not None:
        tip = inputs.head_underside_offset_mm + inputs.shaft_length_mm
    if tip is not None and inputs.board_near_face_z_mm is not None:
        tip_to_board = inputs.board_near_face_z_mm - tip

    nominal_tip = None
    nominal_to_board = None
    if inputs.shaft_length_mm is not None:
        nominal_tip = round(inputs.shaft_length_mm.nominal, 6)
        if inputs.board_near_face_z_mm is not None:
            nominal_to_board = round(inputs.board_near_face_z_mm.nominal - nominal_tip, 6)

    if missing:
        notes.append("no optical gap: " + ", ".join(missing) + " unrecorded")
        return ReachResult(status=INCOMPLETE, complete=False, missing=tuple(missing),
                           nominal_tip_z_mm=nominal_tip, nominal_tip_to_board_plane_mm=nominal_to_board,
                           tip_z_mm=tip, tip_to_board_plane_mm=tip_to_board,
                           optical_gap_mm=None, notes=tuple(notes))

    assert tip is not None and tip_to_board is not None
    assert inputs.led_height_mm is not None
    assert inputs.stack_tolerance_mm is not None
    # LED top plane: the board face MINUS the LED height (the LED comes toward us).
    led_top = inputs.board_near_face_z_mm - inputs.led_height_mm
    gap = led_top - tip
    # A measured stack tolerance widens the gap uncertainty.
    tol = inputs.stack_tolerance_mm.hi
    gap = Interval(gap.lo - tol, gap.hi + tol)
    notes.append("ARITHMETIC ONLY: brightness, coupling, retention, the real hole "
                 "tolerance and any physical fit are unmeasured")
    return ReachResult(status=ARITHMETIC_ONLY, complete=True, missing=(),
                       nominal_tip_z_mm=nominal_tip, nominal_tip_to_board_plane_mm=nominal_to_board,
                       tip_z_mm=tip, tip_to_board_plane_mm=tip_to_board,
                       optical_gap_mm=gap, notes=tuple(notes))


def report(data: dict | None = None) -> list[str]:
    """Print-ready lines for the documented-only inputs."""
    layout = data if data is not None else load_layout()
    result = compute_reach(ReachInputs.from_layout(layout))
    pipe = layout["lightPipe"]
    lines = [
        f"candidate part                         {pipe['part']}",
        f"shaft length x (head underside->tip)   {pipe['shaftLength']:.2f} mm "
        f"+-{pipe['shaftLengthTolerance']:.2f}",
        f"overall length (16.5 shaft + 1.0 head) {pipe['overallLength']:.2f} mm",
        f"head OD / shaft OD / panel hole        {pipe['headDiameter']:.2f} / "
        f"{pipe['shaftDiameter']:.2f} / {pipe['panelHoleDiameter']:.2f} mm",
        f"nominal tip plane from panel front     {nominal_tip_z(layout):.2f} mm (ASSUMPTION: "
        "head underside on the panel front face)",
        f"nominal tip to board near face         {nominal_tip_to_board_plane(layout):.2f} mm "
        "(arithmetic, NOT an LED gap)",
        f"optical gap                            {result.optical_gap_mm} "
        f"({result.status})",
    ]
    lines += [f"missing input                          {name}" for name in result.missing]
    lines += [f"note                                   {note}" for note in result.notes]
    return lines


if __name__ == "__main__":  # pragma: no cover - manual run
    for line in report():
        print(line)
    print("No optical gap and no fit is claimed. The LED height, retention in 1.6 mm "
          "FR-4, the real panel hole tolerance and the stack tolerances are unmeasured.")

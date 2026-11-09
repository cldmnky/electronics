#!/usr/bin/env python3
"""Light-pipe reach: a parametric interval computation with an explicit hole in it.

The two recorded numbers are 17.45 mm (panel rear face to the front-board top
face, from the PROVISIONAL_Z chain) and 10.16 mm (the candidate Bivar
PLP2-400-C). Their difference is 17.45 - 10.16 = **7.29 mm**. That is arithmetic
and nothing more: it is NOT a shortfall, NOT a bound and NOT a verdict, because
the two reference planes the pipe actually spans are unknown:

  * where the pipe's REAR face sits (on the LED top, on the board top face, in a
    board hole, or on a spacer), and
  * where the pipe's FRONT face must sit (the panel rear face, a counterbore or
    a front flange inside the panel, or proud of the panel).

A part with a front flange or a front seat changes the FRONT reference plane, so
the plane separation is not automatically the distance the pipe must cover. No
inequality between the offsets is assumed: nothing here forces the rear seat to
be smaller than an LED height, or the front seat to be zero.

Until every offset and tolerance is measured, the result is ``incomplete`` and
``shortfall_mm`` is None. There is no code path that turns an unknown reference
plane into "the pipe is too short" or into a bound.

Geometry, with the project datum (Z = 0 at the panel FRONT face, +Z rearward):

    panel rear face        P = 1.60 mm        (panel thickness, documented)
    front-board top face   B = 19.05 mm       (PROVISIONAL_Z, not measured)
    gap                    g = B - P = 17.45 mm
    pipe rear face         R = B + rear_offset_mm      (signed, unknown)
    pipe front face        F = P + front_offset_mm     (signed, unknown)
    required length        L_req = R - F = g + rear_offset - front_offset
    shortfall              s = L_req - pipe_length

``rear_offset_mm`` is measured REARWARD (+Z) from the board top face: 0 means the
pipe rests on the board, a positive value means it sits up over the LED, a
negative value means it enters a board hole. ``front_offset_mm`` is measured
REARWARD (+Z) from the panel rear face: 0 means the pipe must reach the panel
rear face, positive means a flange or recess stops it short, negative means the
pipe must stand proud of the panel rear face.

Units: millimetres throughout.

    cd midibrain && python3 mechanical/light_pipe_reach.py

No CAD library is imported, so the rules run everywhere.
"""
from __future__ import annotations

import math
from dataclasses import dataclass

from bracket_geometry import load_layout

# Status vocabulary. `incomplete` is a first-class result, not an error.
INCOMPLETE = "incomplete"
SUFFICIENT = "reach_sufficient"
INSUFFICIENT = "reach_insufficient"
UNCERTAIN = "undetermined_uncertainty"

# The wording this module must never use, because it would claim knowledge of an
# unmeasured reference plane. tests/test_light_pipe_reach.py enforces this.
FORBIDDEN_CLAIMS = ("largest", "smallest", "upper bound", "lower bound",
                    "at most", "at least", "worst case", "best case")


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
    """Every term the reach depends on, each with its own measurement status.

    ``board_top_to_panel_rear_mm``  the documented gap g (17.45 mm provisional)
    ``gap_tolerance_mm``            measured +/- of g; None = unrecorded
    ``pipe_length_mm``              candidate pipe overall length (10.16 mm)
    ``pipe_length_tolerance_mm``    vendor/measured +/- of that length; None = unrecorded
    ``rear_reference_offset_mm``    R - B, signed, +Z from the board top face;
                                    None = the rear reference plane is unmeasured
    ``front_reference_offset_mm``   F - P, signed, +Z from the panel rear face;
                                    None = the front reference plane is unmeasured
    """

    board_top_to_panel_rear_mm: Interval
    gap_tolerance_mm: Interval | None
    pipe_length_mm: Interval
    pipe_length_tolerance_mm: Interval | None
    rear_reference_offset_mm: Interval | None
    front_reference_offset_mm: Interval | None

    @classmethod
    def from_layout(cls, layout: dict | None = None) -> "ReachInputs":
        """The documented values only: no reference plane, no tolerance."""
        data = layout if layout is not None else load_layout()
        reach = data["lightPipeReach"]
        return cls(
            board_top_to_panel_rear_mm=Interval.point(float(reach["gap"])),
            gap_tolerance_mm=None,
            pipe_length_mm=Interval.point(float(reach["pipeLength"])),
            pipe_length_tolerance_mm=None,
            rear_reference_offset_mm=None,
            front_reference_offset_mm=None,
        )


@dataclass(frozen=True)
class ReachResult:
    """The reach, and what could not be computed."""

    status: str
    complete: bool
    missing: tuple[str, ...]
    nominal_gap_minus_length_mm: float
    gap_minus_length_mm: Interval
    required_length_mm: Interval | None
    shortfall_mm: Interval | None
    notes: tuple[str, ...]

    @property
    def verdict(self) -> str | None:
        """None while the result is incomplete: no verdict is possible."""
        return None if self.status == INCOMPLETE else self.status


def compute_reach(inputs: ReachInputs) -> ReachResult:
    """Compute the required pipe length as an interval, or report it incomplete.

    required length = gap + rear offset - front offset
    shortfall       = required length - pipe length   (positive = too short)

    A result is ``complete`` only when the gap, the pipe length, both tolerances
    and both reference offsets are recorded. Until then ``shortfall_mm`` is None
    and no bound of any kind is reported.
    """
    if inputs.pipe_length_mm.lo <= 0:
        raise ReachInputError(f"pipe length must be positive: {inputs.pipe_length_mm}")
    if inputs.board_top_to_panel_rear_mm.lo < 0:
        raise ReachInputError(
            "the distance from the panel rear face to the board top face cannot be "
            f"negative: {inputs.board_top_to_panel_rear_mm}")
    if inputs.pipe_length_tolerance_mm is not None and inputs.pipe_length_tolerance_mm.lo < 0:
        raise ReachInputError(
            f"a tolerance magnitude cannot be negative: {inputs.pipe_length_tolerance_mm}")
    if inputs.gap_tolerance_mm is not None and inputs.gap_tolerance_mm.lo < 0:
        raise ReachInputError(
            f"a tolerance magnitude cannot be negative: {inputs.gap_tolerance_mm}")

    # Arithmetic only: the documented gap minus the candidate length. This is
    # the recorded 7.29 mm and it carries no claim about any reference plane.
    gap_minus_length = inputs.board_top_to_panel_rear_mm - inputs.pipe_length_mm

    missing: list[str] = []
    for name, value in (
        ("gap_tolerance_mm", inputs.gap_tolerance_mm),
        ("pipe_length_tolerance_mm", inputs.pipe_length_tolerance_mm),
        ("rear_reference_offset_mm", inputs.rear_reference_offset_mm),
        ("front_reference_offset_mm", inputs.front_reference_offset_mm),
    ):
        if value is None:
            missing.append(name)

    notes = [
        "the gap is the PROVISIONAL_Z chain (assumptions A and B), not a measurement",
        "the nominal difference is arithmetic only: the documented gap minus the "
        "candidate pipe length, with no reference plane assumed",
    ]

    if missing:
        notes.append("no reach verdict: " + ", ".join(missing) + " unrecorded")
        return ReachResult(
            status=INCOMPLETE,
            complete=False,
            missing=tuple(missing),
            nominal_gap_minus_length_mm=gap_minus_length.nominal,
            gap_minus_length_mm=gap_minus_length,
            required_length_mm=None,
            shortfall_mm=None,
            notes=tuple(notes),
        )

    assert inputs.gap_tolerance_mm is not None
    assert inputs.pipe_length_tolerance_mm is not None
    assert inputs.rear_reference_offset_mm is not None
    assert inputs.front_reference_offset_mm is not None

    # Each recorded tolerance is a +/- magnitude around its documented value.
    gap = Interval(inputs.board_top_to_panel_rear_mm.lo - inputs.gap_tolerance_mm.hi,
                   inputs.board_top_to_panel_rear_mm.hi + inputs.gap_tolerance_mm.hi)
    pipe = Interval(inputs.pipe_length_mm.lo - inputs.pipe_length_tolerance_mm.hi,
                    inputs.pipe_length_mm.hi + inputs.pipe_length_tolerance_mm.hi)
    if pipe.lo <= 0:
        raise ReachInputError(
            f"the pipe length tolerance reaches a non-positive length: {pipe}")

    # Required = gap + rear offset - front offset. Interval arithmetic carries
    # the sign of each offset; no inequality between the offsets is imposed.
    required = (gap + inputs.rear_reference_offset_mm) - inputs.front_reference_offset_mm
    shortfall = required - pipe

    if shortfall.lo > 0.0:
        status = INSUFFICIENT
        notes.append("every value in the shortfall interval is positive: the pipe "
                     "cannot span the recorded planes")
    elif shortfall.hi <= 0.0:
        status = SUFFICIENT
        notes.append("every value in the shortfall interval is zero or negative: the "
                     "pipe spans the recorded planes in the modelled range")
    else:
        status = UNCERTAIN
        notes.append("the shortfall interval straddles zero: the recorded ranges decide "
                     "the result, so the reach is not proven")
    return ReachResult(
        status=status,
        complete=True,
        missing=(),
        nominal_gap_minus_length_mm=gap_minus_length.nominal,
        gap_minus_length_mm=gap_minus_length,
        required_length_mm=required,
        shortfall_mm=shortfall,
        notes=tuple(notes),
    )


def report(data: dict | None = None) -> list[str]:
    """Print-ready lines for the documented-only inputs."""
    layout = data if data is not None else load_layout()
    result = compute_reach(ReachInputs.from_layout(layout))
    lines = [
        f"panel-to-board gap (PROVISIONAL_Z)  {layout['lightPipeReach']['gap']:.2f} mm",
        f"candidate pipe length                {layout['lightPipeReach']['pipeLength']:.2f} mm",
        f"gap minus length (arithmetic only)   {result.nominal_gap_minus_length_mm:.2f} mm",
        f"status                               {result.status}",
    ]
    lines += [f"missing input                        {name}" for name in result.missing]
    lines += [f"note                                 {note}" for note in result.notes]
    return lines


if __name__ == "__main__":  # pragma: no cover - manual run
    for line in report():
        print(line)
    print("No reach verdict is possible until both reference planes and the tolerances "
          "are measured. 7.29 mm is arithmetic, not a bound.")

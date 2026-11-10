#!/usr/bin/env python3
"""Parse the panel's Legend Gerber and measure silk-to-opening clearance.

Why this module exists: a text anchor position is not ink. The exporter renders
each glyph as real strokes in a Gerber file, and only that file shows where the
ink actually lands. This parser reads the emitted ``F_SilkScreen.gbr`` and turns
it into stroked segments, so a clearance can be measured against the real
openings instead of a label anchor or a font-height guess.

Supported Gerber subset - exactly what the tscircuit exporter emits for this
panel, nothing more:

  ``%FSLAX<int><dec>Y<int><dec>*%``  coordinate format (both axes must agree)
  ``%MOMM*%``                        millimetres only
  ``%AD D<n> C,<diameter>*%``        circular aperture (the silk pen)
  ``%AM<name>*% ... %``              aperture macro *definition* (skipped; a
                                     macro used for drawing raises)
  ``%LPD*%`` / ``%LPC*%``            dark (fine) / clear (raises)
  ``G04 ...*``                       comment
  ``G01*``                           linear interpolation
  ``G75*``                           multi-quadrant arc mode declaration (no ink)
  ``G74*``, ``G02*``, ``G03*``       UNSUPPORTED: raise. The exporter emits no
                                     arcs, and sampling one without its centre
                                     would measure a wrong chord
  ``D<n>*``                          select aperture (n >= 10)
  ``X..Y..D01*``                     draw a stroked segment
  ``X..Y..D02*``                     move
  ``X..Y..D03*``                     flash the aperture (a dot)
  ``X..Y..``                         MODAL: repeats the previous D01/D02/D03
  ``M02*``                           end of file

Two rules keep this honest, both tested:

  * a coordinate block must carry BOTH X and Y. A missing axis is never guessed
    as 0;
  * a coordinate block without an explicit D01/D02/D03 repeats the PREVIOUS
    operation, as RS-274X defines. It is not assumed to be a move: guessing a
    move would silently drop ink.

Anything else raises :class:`GerberError`. That is deliberate: a command this
parser does not understand could carry ink, and silently ignoring it would make
the clearance result meaningless.

Clearance model: each segment is a stadium - the centreline swept by a circle of
the aperture radius - and each opening is a circle, an axis-aligned rectangle or
a polygon (the LCD aperture). The clearance is the 2D distance from the opening
boundary to the stroke centreline minus the aperture radius. Negative means the
ink lies over the opening.
"""
from __future__ import annotations

import math
import re
from dataclasses import dataclass
from pathlib import Path

EPS = 1e-9

class GerberError(AssertionError):
    """The Gerber file is unsupported or malformed. Never ignored."""


@dataclass(frozen=True)
class Stroke:
    """One piece of ink: a stroked segment or a flashed dot, in millimetres."""

    x1: float
    y1: float
    x2: float
    y2: float
    width: float
    kind: str  # "segment" | "flash"

    @property
    def radius(self) -> float:
        return self.width / 2


@dataclass(frozen=True)
class Opening:
    """A panel opening: a circle, an axis-aligned rectangle or a polygon."""

    name: str
    kind: str  # "circle" | "rect" | "polygon"
    x: float = 0.0
    y: float = 0.0
    radius: float = 0.0
    x1: float = 0.0
    y1: float = 0.0
    x2: float = 0.0
    y2: float = 0.0
    points: tuple[tuple[float, float], ...] = ()


# ---------------------------------------------------------------------------
# Parsing.
# ---------------------------------------------------------------------------
FS_RE = re.compile(r"^%FSLAX(\d)(\d)Y(\d)(\d)\*%$")
AD_C_RE = re.compile(r"^%ADD(\d+)C,([0-9.]+)\*%$")
AD_OTHER_RE = re.compile(r"^%ADD(\d+)([A-Z]+),")
COORD_RE = re.compile(r"^X(-?\d+)Y(-?\d+)(?:D0([123]))?\*$")
SINGLE_D_RE = re.compile(r"^D(\d+)\*$")
G_RE = re.compile(r"^G(\d+)\*$")
MACRO_START_RE = re.compile(r"^%AM([^*]+)\*%?$")


def parse_gerber_silk(path: str | Path) -> list[Stroke]:
    """Parse a Legend Gerber into strokes. Raise on anything unsupported."""
    text = Path(path).read_text(errors="replace")
    if not text.strip():
        raise GerberError(f"{path} is empty")
    if "M02*" not in text:
        raise GerberError(f"{path} has no M02 end-of-file command: truncated export?")

    # Macros are definitions only. Strip them before line parsing, but refuse a
    # macro that is later used for drawing (checked through the aperture table).
    macro_names: set[str] = set()
    without_macros: list[str] = []
    in_macro = False
    for raw in text.splitlines():
        line = raw.strip()
        if not line:
            continue
        if in_macro:
            if line.endswith("%"):
                in_macro = False
            continue
        match = MACRO_START_RE.match(line)
        if match:
            macro_names.add(match.group(1))
            in_macro = True
            continue
        without_macros.append(line)

    scale = None
    unit = None
    apertures: dict[int, float] = {}
    macro_apertures: dict[int, str] = {}
    current: int | None = None
    point: tuple[float, float] | None = None
    last_operation: str | None = None
    polarity_dark = True
    strokes: list[Stroke] = []

    def mm(raw: str | None, axis: str) -> float:
        if raw is None:
            return 0.0
        assert scale is not None
        return int(raw) * scale

    for line in without_macros:
        if line.startswith("G04"):
            continue
        match = FS_RE.match(line)
        if match:
            if match.group(1) != match.group(3) or match.group(2) != match.group(4):
                raise GerberError(f"mixed-axis coordinate format is unsupported: {line}")
            scale = 10.0 ** -int(match.group(2))
            continue
        if line == "%MOMM*%":
            unit = "mm"
            continue
        if line.startswith("%MO"):
            raise GerberError(f"only millimetres are supported: {line}")
        if line == "%LPD*%":
            polarity_dark = True
            continue
        if line == "%LPC*%":
            raise GerberError("clear polarity is unsupported: it would remove ink")
        if line.startswith("%TF") or line.startswith("%TA") or line.startswith("%TO"):
            continue  # file / aperture / object attributes: no ink
        if line == "%TD*%":
            continue  # end of an attribute block: no ink
        if line.startswith("%AM"):
            raise GerberError(f"unexpected macro body outside a macro block: {line}")
        match = AD_C_RE.match(line)
        if match:
            apertures[int(match.group(1))] = float(match.group(2))
            continue
        match = AD_OTHER_RE.match(line)
        if match:
            macro_apertures[int(match.group(1))] = match.group(2)
            continue
        if line.startswith("%"):
            raise GerberError(f"unsupported extended command: {line}")
        match = G_RE.match(line)
        if match:
            code = int(match.group(1))
            if code == 1:
                pass  # linear interpolation: the only supported mode
            elif code in (2, 3):
                raise GerberError(
                    "arc interpolation (G02/G03) is unsupported: the panel exporter emits "
                    "straight segments only, and an arc measured as a chord would be wrong")
            elif code == 75:
                pass  # multi-quadrant arc mode: the default
            elif code == 74:
                raise GerberError("single-quadrant arc mode (G74) is unsupported")
            else:
                raise GerberError(f"unsupported G-code: {line}")
            continue
        match = SINGLE_D_RE.match(line)
        if match:
            code = int(match.group(1))
            if code < 10:
                raise GerberError(f"D0{code} outside a coordinate block is unsupported: {line}")
            if code in macro_apertures:
                raise GerberError(
                    f"aperture D{code} is macro '{macro_apertures[code]}', which this parser "
                    "cannot measure: refusing to guess its ink")
            if code not in apertures:
                raise GerberError(f"aperture D{code} is selected but never defined")
            current = code
            continue
        match = COORD_RE.match(line)
        if match:
            if scale is None:
                raise GerberError(f"coordinate before a %FS format statement: {line}")
            if unit != "mm":
                raise GerberError(f"coordinate before a %MOMM statement: {line}")
            if current is None:
                raise GerberError(f"coordinate before any aperture selection: {line}")
            x = mm(match.group(1), "x")
            y = mm(match.group(2), "y")
            op = match.group(3)
            if op is None:
                # RS-274X: a coordinate block without an operation code repeats
                # the last one. Inheriting a D01 keeps the ink; assuming a move
                # here would silently drop a drawn segment.
                if last_operation is None:
                    raise GerberError(
                        f"coordinate block with no previous operation: {line}")
                op = last_operation
            else:
                last_operation = op
            if not polarity_dark:  # pragma: no cover - LPC already raises
                raise GerberError("clear polarity is unsupported")
            if op == "1":
                if point is None:
                    raise GerberError(f"draw with no current point: {line}")
                strokes.append(Stroke(point[0], point[1], x, y,
                                      apertures[current], "segment"))
                point = (x, y)
            elif op == "2":
                point = (x, y)
            else:
                strokes.append(Stroke(x, y, x, y, apertures[current], "flash"))
                point = (x, y)
            continue
        if line == "M02*":
            break
        raise GerberError(f"unsupported or unparsed Gerber command: {line}")

    if scale is None:
        raise GerberError(f"{path} has no %FS coordinate format statement")
    if not strokes:
        raise GerberError(f"{path} contains no ink (no D01 or D03)")
    return strokes


# ---------------------------------------------------------------------------
# Geometry.
# ---------------------------------------------------------------------------
def point_segment_distance(px: float, py: float, ax: float, ay: float,
                           bx: float, by: float) -> float:
    dx, dy = bx - ax, by - ay
    length2 = dx * dx + dy * dy
    t = 0.0 if length2 <= EPS else max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / length2))
    return math.hypot(px - (ax + t * dx), py - (ay + t * dy))


def segment_segment_distance(a1, a2, b1, b2) -> float:
    """Shortest distance between two closed segments."""
    def cross(o, p, q):
        return (p[0] - o[0]) * (q[1] - o[1]) - (p[1] - o[1]) * (q[0] - o[0])

    def straddle(p1, p2, p3, p4):
        d1 = cross(p3, p4, p1)
        d2 = cross(p3, p4, p2)
        d3 = cross(p1, p2, p3)
        d4 = cross(p1, p2, p4)
        return d1 * d2 < 0 and d3 * d4 < 0

    if straddle(a1, a2, b1, b2):
        return 0.0
    return min(point_segment_distance(*a1, *b1, *b2),
               point_segment_distance(*a2, *b1, *b2),
               point_segment_distance(*b1, *a1, *a2),
               point_segment_distance(*b2, *a1, *a2))


def segment_rect_distance(a1, a2, x1, y1, x2, y2) -> float:
    """Distance from a segment to an axis-aligned rectangle; 0 when inside."""
    if x1 <= a1[0] <= x2 and y1 <= a1[1] <= y2:
        return 0.0
    corners = [(x1, y1), (x2, y1), (x2, y2), (x1, y2)]
    edges = list(zip(corners, corners[1:] + corners[:1]))
    return min(segment_segment_distance(a1, a2, e[0], e[1]) for e in edges)


def segment_polygon_distance(a1, a2, points) -> float:
    """Distance from a segment to a closed polygon boundary (0 when crossing)."""
    edges = list(zip(points, points[1:] + points[:1]))
    return min(segment_segment_distance(a1, a2, e[0], e[1]) for e in edges)


def stroke_opening_distance(stroke: Stroke, opening: Opening) -> float:
    """Boundary distance from the stroke centreline to the opening outline."""
    a1, a2 = (stroke.x1, stroke.y1), (stroke.x2, stroke.y2)
    if opening.kind == "circle":
        return point_segment_distance(opening.x, opening.y, *a1, *a2) - opening.radius
    if opening.kind == "rect":
        return segment_rect_distance(a1, a2, opening.x1, opening.y1,
                                     opening.x2, opening.y2)
    if opening.kind == "polygon":
        return segment_polygon_distance(a1, a2, list(opening.points))
    raise GerberError(f"unsupported opening kind {opening.kind!r}")


def stroke_clearance(stroke: Stroke, opening: Opening) -> float:
    """Ink-free distance from the stroke EDGE to the opening outline."""
    return stroke_opening_distance(stroke, opening) - stroke.radius


def openings_from_layout(layout: dict) -> list[Opening]:
    """Every physical panel opening and drill, straight from the canonical data."""
    out: list[Opening] = []
    for aperture in layout["panelApertures"]:
        name = aperture["name"]
        if aperture["shape"] == "polygon":
            out.append(Opening(name=name, kind="polygon",
                               points=tuple((p["x"], p["y"]) for p in aperture["points"])))
        elif aperture["shape"] == "rect":
            hw, hh = aperture["width"] / 2, aperture["height"] / 2
            out.append(Opening(name=name, kind="rect",
                               x1=aperture["x"] - hw, y1=aperture["y"] - hh,
                               x2=aperture["x"] + hw, y2=aperture["y"] + hh))
        else:
            out.append(Opening(name=name, kind="circle",
                               x=aperture["x"], y=aperture["y"],
                               radius=aperture["radius"]))
    for drill in layout["panelDrills"]:
        out.append(Opening(name=f"drill {drill['name']}", kind="circle",
                           x=drill["x"], y=drill["y"], radius=drill["diameter"] / 2))
    return out


@dataclass(frozen=True)
class Clearance:
    stroke_index: int
    opening: str
    clearance: float


def minimum_clearance(strokes: list[Stroke], openings: list[Opening]) -> Clearance:
    """The worst silk-to-opening clearance over every stroke and opening."""
    worst = Clearance(-1, "", math.inf)
    for index, stroke in enumerate(strokes):
        for opening in openings:
            clearance = stroke_clearance(stroke, opening)
            if clearance < worst.clearance:
                worst = Clearance(index, opening.name, clearance)
    if worst.stroke_index < 0:
        raise GerberError("no strokes or no openings: nothing was measured")
    return worst


def violations(strokes: list[Stroke], openings: list[Opening],
               limit: float) -> list[Clearance]:
    out: list[Clearance] = []
    for index, stroke in enumerate(strokes):
        for opening in openings:
            clearance = stroke_clearance(stroke, opening)
            if clearance < limit - EPS:
                out.append(Clearance(index, opening.name, clearance))
    return sorted(out, key=lambda c: c.clearance)

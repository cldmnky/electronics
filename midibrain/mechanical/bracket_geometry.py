#!/usr/bin/env python3
"""Pure geometry for the MIDI Brain feedthrough / strain-relief bracket.

Every coordinate comes from ``mechanical/layout.json``, which is generated from
``tscircuit/lib/layout.ts`` by ``bun run scripts/export-mechanical.ts``. This
module never restates a position, and it imports no CAD library, so the rules
below can be tested on any machine:

    python3 -m unittest discover -s mechanical -p 'test_*.py'

CadQuery is only needed to build the solid (``feedthrough_bracket.py``).

Status: PROVISIONAL. The bracket is a planning model. Two inputs are still
unmeasured: the M2.5 screw-head/washer envelope (the head-clearance slots) and
the printed-hole fit for an M2.5 screw. See docs/MECHANICAL-VERIFICATION.md.
"""
from __future__ import annotations

import json
import math
from dataclasses import dataclass
from pathlib import Path

LAYOUT_PATH = Path(__file__).resolve().parent / "layout.json"

# Tolerance for the arithmetic in this file (mm).
EPS = 1e-9


class GeometryError(AssertionError):
    """A design rule failed. Never caught to hide a failure."""


@dataclass(frozen=True)
class Rect:
    """Axis-aligned rectangle in millimetres, front-view panel coordinates."""

    x1: float
    y1: float
    x2: float
    y2: float

    @property
    def width(self) -> float:
        return self.x2 - self.x1

    @property
    def height(self) -> float:
        return self.y2 - self.y1

    @property
    def centre(self) -> tuple[float, float]:
        return ((self.x1 + self.x2) / 2, (self.y1 + self.y2) / 2)

    def contains_point(self, x: float, y: float, margin: float = 0.0) -> bool:
        return (self.x1 + margin <= x <= self.x2 - margin
                and self.y1 + margin <= y <= self.y2 - margin)

    def overlaps(self, other: "Rect", gap: float = 0.0) -> bool:
        """True when the two rectangles are closer than ``gap`` (0 = touching)."""
        return not (self.x2 + gap <= other.x1 or other.x2 + gap <= self.x1
                    or self.y2 + gap <= other.y1 or other.y2 + gap <= self.y1)

    def clamp_to(self, outer: "Rect", gap: float = 0.0) -> bool:
        """True when this rectangle fits inside ``outer`` with ``gap`` to spare."""
        return (self.x1 >= outer.x1 + gap - EPS and self.x2 <= outer.x2 - gap + EPS
                and self.y1 >= outer.y1 + gap - EPS and self.y2 <= outer.y2 - gap + EPS)


@dataclass(frozen=True)
class Circle:
    x: float
    y: float
    diameter: float

    @property
    def radius(self) -> float:
        return self.diameter / 2

    def centre_distance(self, other: "Circle") -> float:
        return math.hypot(self.x - other.x, self.y - other.y)

    def clearance_to(self, other: "Circle") -> float:
        """Material-free distance between the two cut outlines (negative = overlap)."""
        return self.centre_distance(other) - self.radius - other.radius

    def clearance_to_rect(self, rect: Rect) -> float:
        """Signed distance from this circle's edge to the rectangle (negative = overlap)."""
        dx = max(rect.x1 - self.x, 0.0, self.x - rect.x2)
        dy = max(rect.y1 - self.y, 0.0, self.y - rect.y2)
        return math.hypot(dx, dy) - self.radius

    def clearance_to_edge(self, outer: Rect) -> float:
        """Distance from this circle's edge to the nearest outline edge."""
        return min(self.x - self.radius - outer.x1, outer.x2 - self.x - self.radius,
                   self.y - self.radius - outer.y1, outer.y2 - self.y - self.radius)

    def ring(self, radius: float, samples: int = 8) -> list[tuple[float, float]]:
        return [(self.x + radius * math.cos(2 * math.pi * i / samples),
                 self.y + radius * math.sin(2 * math.pi * i / samples))
                for i in range(samples)]


def load_layout(path: Path | str = LAYOUT_PATH) -> dict:
    """Read the generated canonical data. Fail loudly, never invent a default."""
    p = Path(path)
    if not p.is_file():
        raise GeometryError(
            f"{p} is missing. Generate it with: "
            "cd tscircuit && bun run scripts/export-mechanical.ts")
    return json.loads(p.read_text())


@dataclass(frozen=True)
class BracketGeometry:
    """Bracket features in panel coordinates, derived from layout.json."""

    outline: Rect
    thickness: float
    mounts: list[Circle]
    slots: list[Rect]
    slot_ids: list[str]
    head_clearance: list[Circle]
    tie_holes: list[Circle]
    notch: Rect
    board: Rect
    spacer_top: float
    material_to_cuts: float

    # -- derived checks -----------------------------------------------------
    def slot_webs(self) -> list[tuple[float, float]]:
        """X span of material between adjacent slots, left to right."""
        edges = sorted((s.x1, s.x2) for s in self.slots)
        return [(edges[i][1], edges[i + 1][0]) for i in range(len(edges) - 1)]

    def slot_web_widths(self) -> list[float]:
        return [b - a for a, b in self.slot_webs()]

    def mount_ligament(self, mount: Circle) -> float:
        """Narrowest remaining material between a mount hole and any edge/cut."""
        return min(
            mount.clearance_to_edge(self.outline),
            *[mount.clearance_to_rect(s) for s in self.slots],
            *[mount.clearance_to(c) for c in self.head_clearance],
            *[mount.clearance_to(c) for c in self.tie_holes],
        )

    def cut_clearances(self) -> list[tuple[str, str, float]]:
        """Pairwise clearance between every two cut features, with their names."""
        named: list[tuple[str, object]] = []
        for i, c in enumerate(self.mounts):
            named.append((f"mount{i}", c))
        for i, s in enumerate(self.slots):
            named.append((f"slot{i}", s))
        for i, c in enumerate(self.head_clearance):
            named.append((f"head{i}", c))
        for i, c in enumerate(self.tie_holes):
            named.append((f"tie{i}", c))
        out: list[tuple[str, str, float]] = []
        for i in range(len(named)):
            for j in range(i + 1, len(named)):
                (na, a), (nb, b) = named[i], named[j]
                if isinstance(a, Circle) and isinstance(b, Circle):
                    d = a.clearance_to(b)
                elif isinstance(a, Circle):
                    d = a.clearance_to_rect(b)  # type: ignore[arg-type]
                elif isinstance(b, Circle):
                    d = b.clearance_to_rect(a)  # type: ignore[arg-type]
                else:
                    d = -self._rect_overlap(a, b)  # type: ignore[arg-type]
                out.append((na, nb, d))
        return out

    @staticmethod
    def _rect_overlap(a: Rect, b: Rect) -> float:
        """Overlap depth of two rectangles along the axis that separates them."""
        dx = min(a.x2, b.x2) - max(a.x1, b.x1)
        dy = min(a.y2, b.y2) - max(a.y1, b.y1)
        return min(dx, dy)


def bracket_geometry(layout: dict | None = None) -> BracketGeometry:
    """Build the bracket geometry from the generated canonical data."""
    data = layout if layout is not None else load_layout()
    b = data["bracket"]
    o = b["outline"]
    notch = data["notchBounds"]
    front = data["frontPcb"]
    slots = [Rect(s["x"] - s["width"] / 2, s["yBottom"], s["x"] + s["width"] / 2, s["yTop"])
             for s in b["slots"]]
    return BracketGeometry(
        outline=Rect(o["x1"], o["y1"], o["x2"], o["y2"]),
        thickness=b["thickness"],
        mounts=[Circle(x, b["mountY"], b["mountHoleDiameter"]) for x in b["mountX"]],
        slots=slots,
        slot_ids=[s["id"] for s in b["slots"]],
        head_clearance=[Circle(c["x"], c["y"], c["diameter"]) for c in b["mountHeadClearance"]],
        tie_holes=[Circle(c["x"], c["y"], c["diameter"]) for c in b["tieHoles"]],
        notch=Rect(notch["left"], notch["bottom"], notch["right"], notch["top"]),
        board=Rect(-front["width"] / 2, -front["height"] / 2, front["width"] / 2, front["height"] / 2),
        # The bar is on the REAR side of the front board, so the front-side
        # display envelope does not constrain it; the constraint is the top
        # front/rear spacer row whose heads the bar must clear.
        spacer_top=max(h["y"] for h in data["frontRearMount"] if h["y"] > 0),
        material_to_cuts=data["bracketAllowance"]["materialToCuts"],
    )


def describe(g: BracketGeometry) -> list[str]:
    """Human-readable derived numbers, printed by the CAD self-check."""
    lines = [
        f"outline {g.outline.width:.2f} x {g.outline.height:.2f} mm "
        f"(x {g.outline.x1:.2f}..{g.outline.x2:.2f}, y {g.outline.y1:.2f}..{g.outline.y2:.2f}), "
        f"thickness {g.thickness:.2f} mm",
        "slots: " + ", ".join(
            f"{i} x {s.centre[0]:.2f} w {s.width:.2f} (y {s.y1:.2f}..{s.y2:.2f})"
            for i, s in zip(g.slot_ids, g.slots)),
        "slot webs: " + ", ".join(f"{w:.2f} mm" for w in g.slot_web_widths()),
        "mount ligament: " + ", ".join(
            f"({m.x:.2f}, {m.y:.2f}) {g.mount_ligament(m):.2f} mm" for m in g.mounts),
        "head clearance: " + ", ".join(
            f"({c.x:.2f}, {c.y:.2f}) D{c.diameter:.2f}" for c in g.head_clearance),
        "tie holes: " + ", ".join(
            f"({c.x:.2f}, {c.y:.2f}) D{c.diameter:.2f}" for c in g.tie_holes),
    ]
    return lines


def validate(g: BracketGeometry, layout: dict | None = None) -> list[str]:
    """Check every design rule. Returns warnings; raises GeometryError on failure.

    Warnings are rules that a physically unmeasured input can still change, so
    they are reported and never silently pass as measurements.
    """
    data = layout if layout is not None else load_layout()
    allowance = data["bracketAllowance"]
    notices: list[str] = []

    def need(condition: bool, message: str) -> None:
        if not condition:
            raise GeometryError(message)

    # 1. The printed part must span both board mounts with material around them.
    for m in g.mounts:
        need(g.outline.contains_point(m.x, m.y), f"mount ({m.x}, {m.y}) is off the bracket")
        ligament = m.clearance_to_edge(g.outline)
        need(ligament + EPS >= allowance["mountEdgeWall"] - m.radius,
             f"mount ({m.x}, {m.y}) edge ligament {ligament:.2f} mm is under the "
             f"{allowance['mountEdgeWall'] - m.radius:.2f} mm wall")
        need(g.mount_ligament(m) > 0.0,
             f"mount ({m.x}, {m.y}) has no material around it (ligament "
             f"{g.mount_ligament(m):.2f} mm)")

    # 2. The part must be backed by the front board, so it can be bolted flat.
    need(g.outline.clamp_to(g.board), "bracket outline is not inside the front board outline")

    # 3. Slots: sized from the vendor cutouts, inside the notch in X (board void),
    #    and separated by a printable web. Touching slots are the old defect.
    need(len(g.slots) == len(data["feedthroughs"]), "slot count does not match the feedthrough count")
    for slot, feedthrough, slot_id in zip(g.slots, data["feedthroughs"], g.slot_ids):
        need(abs(slot.centre[0] - feedthrough["x"]) < EPS,
             f"slot {slot_id} is not on its feedthrough centre")
        expected = feedthrough["cutoutWidth"] + 2 * allowance["slotClearancePerSide"]
        need(abs(slot.width - expected) < 1e-6,
             f"slot {slot_id} width {slot.width:.2f} is not cutout + 2 x clearance ({expected:.2f})")
        need(g.notch.x1 - EPS <= slot.x1 and slot.x2 <= g.notch.x2 + EPS,
             f"slot {slot_id} reaches past the board notch, where the board is solid")
        need(slot.y2 >= g.notch.y2 - EPS,
             f"slot {slot_id} is not open to the bracket top edge")
        # Height, not only width: the slot floor must clear the whole declared
        # panel opening plus the same per-side clearance, and must stay inside
        # the board notch (below the notch bottom the board is solid).
        expected_floor = (feedthrough["y"] - feedthrough["cutoutHeight"] / 2
                          - allowance["slotClearancePerSide"])
        need(abs(slot.y1 - expected_floor) < 1e-6,
             f"slot {slot_id} floor {slot.y1:.2f} is not the declared opening "
             f"{feedthrough['y'] - feedthrough['cutoutHeight'] / 2:.2f} less "
             f"{allowance['slotClearancePerSide']:.2f} mm clearance ({expected_floor:.2f})")
        need(slot.y1 > g.notch.y1 - EPS,
             f"slot {slot_id} floor {slot.y1:.2f} is below the notch bottom "
             f"{g.notch.y1:.2f}, where the board is solid")
        need(slot.clamp_to(g.outline), f"slot {slot_id} is not inside the bracket outline")
    for width in g.slot_web_widths():
        need(width > g.material_to_cuts + EPS,
             f"slot web {width:.2f} mm is under the {g.material_to_cuts:.2f} mm minimum")

    # 4. The top front-to-rear spacer hardware sits in the same 3 mm layer, so
    #    the bracket must clear it, and must not lose its own mount material.
    top_mounts = [h for h in data["frontRearMount"] if h["y"] > 0]
    need(len(g.head_clearance) == len(top_mounts),
         "head-clearance count does not match the top front-rear mount count")
    for head, mount in zip(g.head_clearance, top_mounts):
        need(abs(head.x - mount["x"]) < EPS and abs(head.y - mount["y"]) < EPS,
             f"head clearance is not on mount ({mount['x']}, {mount['y']})")
        need(head.x - head.radius > g.outline.x1 and head.x + head.radius < g.outline.x2,
             f"head clearance at x {head.x:.2f} runs off the bracket side")
        need(head.y + head.radius <= g.outline.y2,
             f"head clearance at y {head.y:.2f} runs past the bracket top edge")
        for m in g.mounts:
            need(head.clearance_to(m) > 0.0,
                 f"head clearance at ({head.x:.2f}, {head.y:.2f}) merges with a bracket mount")
        if head.y - head.radius < g.outline.y1:
            notices.append(
                f"head clearance at ({head.x:.2f}, {head.y:.2f}) breaks out of the bottom "
                f"edge as an open slot (bottom {head.y - head.radius:.2f} < {g.outline.y1:.2f})")

    # 5. Cable ties sit in webs, never in a slot or a mount ligament.
    need(len(g.tie_holes) == max(len(g.slots) + 1, 2), "expected one tie hole per slot web end")
    for tie in g.tie_holes:
        need(tie.clearance_to_edge(g.outline) > g.material_to_cuts,
             f"tie hole ({tie.x:.2f}, {tie.y:.2f}) is too close to the bracket edge")
        for slot in g.slots:
            need(tie.clearance_to_rect(slot) > 0.0,
                 f"tie hole ({tie.x:.2f}, {tie.y:.2f}) is inside or touching a slot")
        for m in g.mounts:
            need(tie.clearance_to(m) > 0.0,
                 f"tie hole ({tie.x:.2f}, {tie.y:.2f}) touches a mount ligament")

    # 6. Bracket bottom sits below the top spacer row (so the bar carries the
    #    head-clearance slots) and the part stays inside the panel.
    need(g.outline.y1 <= g.spacer_top - allowance["spacerHeadWall"] + EPS,
         f"bracket bottom {g.outline.y1:.2f} does not reach the top spacer row")
    panel = data["panel"]
    need(abs(g.outline.x1) < panel["width"] / 2 and abs(g.outline.x2) < panel["width"] / 2,
         "bracket is wider than the panel")

    # 7. No two cuts may intersect: a merged cut removes structure the design
    #    counts on, and the CAD boolean would hide it.
    for na, nb, clearance in g.cut_clearances():
        need(clearance > 0.0 - EPS, f"cuts {na} and {nb} intersect (clearance {clearance:.2f} mm)")

    # 8. Physical gates that the model cannot settle stay visible.
    notices.append(
        "PROVISIONAL: slot width and floor come from the VENDOR PANEL OPENINGS plus "
        "clearance; the rear body, flange and cable-bundle envelopes are unmeasured "
        "and a panel opening does not prove them")
    notices.append(
        "PROVISIONAL: M2.5 head/washer envelope "
        f"D{allowance['mountHeadClearance']:.2f} is assumed, not measured; the head slots "
        "are a named physical gate")
    notices.append(
        f"PROVISIONAL: printed mount hole D{g.mounts[0].diameter:.2f} is the board clearance "
        "diameter, not a verified FDM fit for an M2.5 screw")
    return notices


if __name__ == "__main__":  # pragma: no cover - manual run
    geo = bracket_geometry()
    for line in describe(geo):
        print(line)
    for line in validate(geo):
        print("NOTICE", line)
    print("geometry checks pass")

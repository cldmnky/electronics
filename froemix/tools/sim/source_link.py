#!/usr/bin/env python3
"""Prove that a SPICE deck still models the tscircuit source it claims to model.

A deck is a hand transcription, so it can silently drift from the board. This
module links both sides:

  * the source side is parsed from the canonical tscircuit files
    (``froemix/tscircuit/sections/*.tsx`` and, for the front board,
    ``froemix/euro/sections/*.tsx`` plus ``froemix/euro/lib/layout.ts``); the
    SI literal parser is ``froemix/tools/check_values.py``, so a value means the
    same thing here as in gate step 7.
  * the deck side is parsed as SPICE: element name, nodes, value/subcircuit.
  * ``DECK_LINKS`` names, for every deck element that stands for a real fitted
    part, the source reference, the board it comes from and the source pin each
    deck node stands for. ``DECK_NETS`` checks the deck's net names against the
    pins of a source part where the deck is behavioural rather than
    element-for-element (the CD4051).

A check therefore fails when a source VALUE changes, when a source CONNECTION
changes, when the deck's own value or node list changes, and when a deck gains
an element that no source part explains. Parts that are deliberately not
source parts (supplies, stimuli, case loads, model internals) must be declared
in ``ASSUMPTIONS`` with a reason; the reason is printed with the table.

What is NOT covered (kept visible on purpose): the MIDI decks and the new
MIDI_OUT/MIDI_IN topology are reserved until their approved constants land
(they are owned by another change). Until then this module makes no claim about
``sections/midi.tsx`` and no deck reads it. ``sections/midi.tsx`` can change
without a failure here -- that is a known, stated gap, not a silent pass.

Run directly to check, or with ``--draft DECK`` to print draft table rows:

    python3 source_link.py            # check, exit 1 on drift
    python3 source_link.py --draft audio_in
"""

from __future__ import annotations

import argparse
import fnmatch
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path

HERE = Path(__file__).resolve().parent
FROEMIX = HERE.parent.parent
sys.path.insert(0, str(HERE.parent))
import check_values  # noqa: E402  (reuse the one SI-literal parser)
from simlib import SimError, spice_number  # noqa: E402

BASE_SECTIONS = FROEMIX / "tscircuit" / "sections"
EURO_SECTIONS = FROEMIX / "euro" / "sections"
EURO_LAYOUT = FROEMIX / "euro" / "lib" / "layout.ts"

TOLERANCE = 0.005  # 0.5 %, the same window check_values.py uses

ELEMENT_RE = re.compile(
    r"<(resistor|capacitor|inductor|diode|led|chip|transistor|connector|potentiometer)\b(.*?)/>",
    re.S,
)
VALUE_PROP = {"resistor": "resistance", "capacitor": "capacitance", "inductor": "inductance"}
PIN_PAIR_RE = re.compile(r'"([A-Za-z0-9_]+)"\s*:\s*"net\.([A-Za-z0-9_]+)"')
BARE_PAIR_RE = re.compile(r'\b([A-Za-z0-9_]+)\s*:\s*"net\.([A-Za-z0-9_]+)"')
ATTRIBUTE_RE = re.compile(r'([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(?:"([^"]*)"|\{([^}]*)\})')


# --------------------------------------------------------------------------
# source side
# --------------------------------------------------------------------------

class LinkError(RuntimeError):
    """The deck and the source disagree."""


@dataclass
class Component:
    ref: str
    kind: str
    value_text: str | None
    value: float | None
    pins: dict[str, str | None]
    file: str


@dataclass
class Board:
    name: str
    components: dict[str, Component] = field(default_factory=dict)
    layout: dict[str, dict] = field(default_factory=dict)
    jack_leds: list[dict] = field(default_factory=list)
    files: list[str] = field(default_factory=list)


def _object_at(text: str, open_index: int) -> str:
    """Body of the {...} or [...] that starts at ``open_index``."""
    opener = text[open_index]
    closer = {"{": "}", "[": "]"}[opener]
    depth = 0
    for index in range(open_index, len(text)):
        char = text[index]
        if char == opener:
            depth += 1
        elif char == closer:
            depth -= 1
            if depth == 0:
                return text[open_index + 1:index]
    raise LinkError(f"unbalanced {opener} in {EURO_LAYOUT.name}")


def load_layout_constants(path: Path = EURO_LAYOUT) -> tuple[dict[str, dict], list[dict]]:
    """(exported objects, JACK_LEDS entries) from euro/lib/layout.ts.

    Only flat ``key: "literal"`` entries are read; they carry the values the
    front-board decks transcribe (the button resistors, the jack-LED chain).
    """
    text = path.read_text()
    objects: dict[str, dict] = {}
    for match in re.finditer(r"export const ([A-Z][A-Z0-9_]*)\s*(?::[^=]+)?=\s*([{\[])", text):
        name, opener = match.group(1), match.group(2)
        if opener != "{":
            continue
        body = _object_at(text, match.start(2))
        objects[name] = dict(re.findall(r'(\w+)\s*:\s*"([^"]*)"', body))
    jack_leds: list[dict] = []
    array = re.search(r"export const JACK_LEDS\s*(?::[^=]+)?=\s*\[", text)
    if array:
        body = _object_at(text, array.end() - 1)
        for entry in re.finditer(r"\{([^{}]*)\}", body):
            pairs = dict(re.findall(r'(\w+)\s*:\s*"([^"]*)"', entry.group(1)))
            if "resistor" in pairs:
                jack_leds.append(pairs)
    return objects, jack_leds


def _resolve_expression(constants: dict[str, dict], expression: str) -> str | None:
    """``BUTTON.seriesR`` -> ``"1k"``; unknown expressions stay None."""
    match = re.match(r"\s*([A-Za-z_][A-Za-z0-9_]*)\.([A-Za-z_][A-Za-z0-9_]*)\s*$", expression)
    if not match:
        return None
    return constants.get(match.group(1), {}).get(match.group(2))


def load_board(board: str = "base", sections: Path | None = None,
               layout: Path | None = None) -> Board:
    """Parse the canonical tscircuit source into components and pin nets."""
    if board == "base":
        directory = sections or BASE_SECTIONS
        result = Board(name="base")
    elif board == "euro":
        directory = sections or EURO_SECTIONS
        result = Board(name="euro")
        result.layout, result.jack_leds = load_layout_constants(layout or EURO_LAYOUT)
    else:
        raise LinkError(f"unknown board {board!r}")
    if not directory.is_dir():
        raise LinkError(f"source directory not found: {directory}")

    for path in sorted(directory.glob("*.tsx")):
        source = path.read_text()
        result.files.append(path.name)
        for element in ELEMENT_RE.finditer(source):
            kind, body = element.group(1), element.group(2)
            props: dict[str, str] = {}
            expressions: dict[str, str] = {}
            for attr in ATTRIBUTE_RE.finditer(body):
                key, literal, expression = attr.group(1), attr.group(2), attr.group(3)
                if literal is not None:
                    props[key] = literal
                else:
                    expressions[key] = expression
            ref = props.get("name") or (expressions.get("name") or "").strip("\"'")
            if not ref or "{" in ref:
                continue
            prop = VALUE_PROP.get(kind)
            value_text = props.get(prop) if prop else None
            if prop and prop in expressions:
                value_text = _resolve_expression(result.layout, expressions[prop])
            value = None
            if prop and value_text:
                try:
                    value = check_values.parse_literal(value_text, kind)
                except ValueError as exc:
                    raise LinkError(f"{path.name}: {ref}: {exc}") from None
            pins: dict[str, str | None] = {}
            for pin, net in PIN_PAIR_RE.findall(body):
                pins[pin] = net
            for pin, net in BARE_PAIR_RE.findall(body):
                pins.setdefault(pin, net)
            for pin in re.findall(r'\b([A-Za-z0-9_]+)\s*:\s*`net\.', body):
                pins.setdefault(pin, None)  # a template connection: not static
            result.components[ref] = Component(ref=ref, kind=kind, value_text=value_text,
                                              value=value, pins=pins, file=path.name)
    if board == "euro":
        _add_jack_led_components(result, directory)
    if not result.components:
        raise LinkError(f"no components parsed from {directory}")
    return result


def _add_jack_led_components(board: Board, sections: Path) -> None:
    """The jack-LED resistors are named through layout.ts, so build them here.

    ``sections/jack-leds.tsx`` names each part as ``name={jl.resistor}``, which
    a static parse cannot resolve. Both halves are canonical (the ref and net
    from ``JACK_LEDS``, the value from the resistor literal in that section), so
    the pair is reconstructed and checked like any other part.
    """
    path = sections / "jack-leds.tsx"
    if not path.is_file():
        return
    literals = re.findall(r'resistance="([^"]+)"', path.read_text())
    if not literals:
        return
    literal = literals[0] if len(set(literals)) == 1 else None
    if literal is None:
        return
    for entry in board.jack_leds:
        ref = entry.get("resistor")
        if not ref:
            continue
        try:
            value = check_values.parse_literal(literal, "resistor")
        except ValueError:
            continue
        pins = {"pin1": entry.get("net"), "pin2": entry.get("base", "") + "_D"}
        board.components[ref] = Component(ref=ref, kind="resistor", value_text=literal,
                                          value=value, pins=pins, file=path.name)


def load_boards() -> tuple[Board, Board]:
    return load_board("base"), load_board("euro")


# --------------------------------------------------------------------------
# deck side
# --------------------------------------------------------------------------

@dataclass
class DeckElement:
    name: str
    nodes: list[str]
    value: str | None
    block: str

    @property
    def kind(self) -> str:
        letter = self.name[0].upper()
        return {"R": "resistor", "C": "capacitor", "L": "inductor", "D": "diode",
                "X": "subcircuit", "V": "source", "I": "source", "B": "behavioural",
                "S": "switch"}.get(letter, "other")


def load_deck(path: Path) -> dict[str, DeckElement]:
    """Every element of a deck, keyed by name (subcircuit internals included)."""
    lines: list[str] = []
    for raw in Path(path).read_text().splitlines():
        stripped = raw.strip()
        if not stripped or stripped.startswith("*"):
            continue
        if stripped.startswith("+"):
            if not lines:
                raise LinkError(f"{path.name}: continuation line before any element")
            lines[-1] += " " + stripped[1:].strip()
            continue
        lines.append(stripped)

    elements: dict[str, DeckElement] = {}
    block = "top"
    in_control = False
    for line in lines:
        tokens = line.split()
        head = tokens[0]
        if head.startswith("."):
            directive = head[1:].lower()
            if directive == "control":
                in_control = True
            elif directive == "endc":
                in_control = False
            elif directive == "subckt":
                block = tokens[1]
            elif directive in ("ends", "end"):
                block = "top"
            continue
        if in_control:
            continue
        if "PARAMS:" in tokens:
            tokens = tokens[:tokens.index("PARAMS:")]
        if len(tokens) < 2:
            continue
        name = tokens[0]
        if name in elements:
            raise LinkError(f"{path.name}: element {name} defined twice")
        letter = name[0].upper()
        if letter == "X":
            nodes, value = tokens[1:-1], tokens[-1]
        elif letter == "Q":
            nodes, value = tokens[1:4], (tokens[4] if len(tokens) > 4 else None)
        elif letter in "RCLDI":
            nodes, value = tokens[1:3], (tokens[3] if len(tokens) > 3 else None)
        elif letter == "S":
            nodes, value = tokens[1:5], (tokens[5] if len(tokens) > 5 else None)
        else:
            nodes, value = tokens[1:3], (" ".join(tokens[3:]) or None)
        elements[name] = DeckElement(name=name, nodes=nodes, value=value, block=block)
    if not elements:
        raise LinkError(f"{path.name}: no elements parsed")
    return elements


# --------------------------------------------------------------------------
# the linkage tables
# --------------------------------------------------------------------------

@dataclass(frozen=True)
class Link:
    """One deck element that stands for a fitted part in the source.

    ``pairs`` maps each deck node to a source pin, in deck order, so a swapped
    connection is caught and not just a changed node set. The spec is either a
    pin name on ``ref`` or a ``(ref, pin)`` tuple on the same board. ``None``
    means the part is a subcircuit internal: its value is checked, its
    connectivity is carried by the instance the table links instead.
    ``partial`` marks a node list the deck models approximately (a virtual
    ground written as 0 V, a current-sense node): only the declared leading
    pairs are checked, and ``note`` says why.
    """

    spice: str
    ref: str
    pairs: tuple[tuple[str, tuple[str, ...] | str], ...] | None = None
    board: str = "base"
    partial: bool = False
    note: str = ""


def L(spice: str, ref: str, pairs=None, board: str = "base",
      partial: bool = False, note: str = "") -> Link:
    return Link(spice=spice, ref=ref, pairs=tuple(pairs) if pairs else None,
                board=board, partial=partial, note=note)


@dataclass(frozen=True)
class ParamLink:
    """A subcircuit instance parameter that carries a fitted value.

    The CV subcircuits take their resistors as parameters, so the instance
    line -- not the parameterised element inside the subcircuit -- is what must
    follow the source.
    """

    spice: str
    params: tuple[tuple[str, str], ...]
    board: str = "base"


def PL(spice: str, params: dict[str, str], board: str = "base") -> ParamLink:
    return ParamLink(spice=spice, params=tuple(sorted(params.items())), board=board)


GROUND_NODE = "0"
GROUND_NET = "GND"

# Model internals shared by every deck (the op-amp subcircuit body and the
# shunt-reference subcircuit body). These are not fitted parts by design.
GLOBAL_ASSUMPTIONS: dict[str, str] = {
    "RIN": "op-amp subcircuit input resistance (model internal)",
    "G1": "op-amp subcircuit transconductance (model internal)",
    "RP": "op-amp subcircuit A0 = gm*RP (model internal)",
    "CP": "op-amp subcircuit GBW pole (model internal)",
    "B1": "op-amp subcircuit output clamp (model internal)",
    "RZ": "LM4040 subcircuit dynamic impedance (model internal)",
    "VS": "LM4040 subcircuit reference voltage (model internal)",
    "V*": "supplies and stimuli (no fitted part has a V designator)",
}

# Supplies, stimuli, case networks and model parameters that are not source
# parts. Exact names, or fnmatch patterns (``VCH*``, ``DZ*``), each with the
# reason that is printed with the table.
ASSUMPTIONS: dict[str, dict[str, str]] = {
    "audio_in.cir": {
        "RSRC": "source-impedance case resistor (not fitted)",
        "RSR1": "case copy of R1: linked by value to R1",
        "RSR4": "case copy of R4: linked by value to R4",
        "CSC2": "case copy of C2: linked by value to C2",
        "XSRC": "op-amp inside the source-impedance case",
        "XSRC1K": "1k source-impedance case instance",
        "XSRC10K": "10k source-impedance case instance",
        "VSRC": "source-impedance case stimulus",
        "V1": "AC + transient stimulus at the jack",
    },
    "audio_out.cir": {
        "VLOAD": "load-case stimulus (stands for NET_C8_2, the U3A output)",
        "RLD16": "case copy of R16", "RLD17": "case copy of R17",
        "CLD12": "case copy of C12", "RLDRS": "case copy of R19",
        "RLDRL": "case load (not fitted)",
        "XLDOP": "op-amp inside the load case",
        "XLOAD*": "load cases: 100k / 50k / 10k / open",
        "VOLDSRC": "old-gain negative-case stimulus",
        "XOLD": "pre-R08 gain-10 chain (negative case, not fitted)",
        "ROR*": "pre-R08 chain resistors (negative case, not fitted)",
        "COR*": "pre-R08 chain capacitors (negative case, not fitted)",
        "XOR1": "op-amp inside the old-gain case",
        "XOR2": "op-amp inside the old-gain case",
        "RLDOLD": "old-gain case load (not fitted)",
        "RLOAD": "100 k eurorack input impedance (case load, not fitted)",
        "V1": "AC + transient stimulus at the jack",
    },
    "cv_inout.cir": {
        "VP33": "P3V3_A supply", "VIN": "CV jack stimulus", "VDAC": "DAC stimulus",
        "VREF": "ideal -10 V reference (ref_bias.cir covers the real LM4040)",
        "VPIDEAL": "unclamped op-amp rail", "VMIDEAL": "unclamped op-amp rail",
        "VSTEP*": "step stimuli",
        "XIN*": "CV-in instances (ideal / 1 % / 0.1 % corners / fitted MCP6002)",
        "XOUT*": "CV-out instances (load, DAC-source, tolerance, no-follower)",
        "XS_*": "CV-out load-capacitance step instances, small signal",
        "XF_*": "CV-out load-capacitance step instances, full scale",
        "XOP": "op-amp inside the CVIN subcircuit",
        "XOP1": "op-amp inside the CVOUT subcircuit",
        "XOP2": "op-amp inside the CVOUT subcircuit",
        "XOPB": "U11A/U11D follower inside the CVOUT subcircuit",
        "R30": "CV-in input resistor inside CVIN: value carried by the instance "
               "parameters (DECK_PARAMS), so it is checked there",
        "R33": "CV-in feedback inside CVIN (instance parameter, see DECK_PARAMS)",
        "R34": "CV-in offset resistor inside CVIN (instance parameter)",
        "R31": "CV-out input resistor inside CVOUT (instance parameter)",
        "R35": "CV-out stage-1 feedback inside CVOUT (instance parameter)",
        "R37": "CV-out stage-2 input inside CVOUT (instance parameter)",
        "R40": "CV-out stage-2 feedback inside CVOUT (instance parameter)",
        "RSDAC": "DAC source resistance case (0 / 10.3k / 13k / 16k)",
        "CFB": "C33/C34 value case (0 / 100p)",
        "RLOAD": "eurorack input impedance case",
        "CLOAD": "cable load capacitance case (0 / 1n / 10n / 100n)",
    },
    "ref_bias.cir": {
        "VRAMP": "start-up ramp stimulus",
        "VDUM": "dummy source: two-point sweep reads the load cases",
        "VSENSE": "0 V sense source for the cathode current",
        "VSENSE*": "0 V sense sources for the ramp and load cases",
        "DZ*": "illustrative breakdown-knee reference (load/start-up cases)",
        "R41*": "load/start-up case bias resistors (not fitted)",
        "R34*": "load/start-up case summing-resistor loads (not fitted)",
        "R43*": "load/start-up case summing-resistor loads (not fitted)",
        "C20*": "load/start-up case bypass capacitors (not fitted)",
        "RL*": "extra load cases (not fitted)",
        "VEE": "supply sweep",
    },
    "sync_io.cir": {
        "VIN": "sync input stimulus", "VSYNCOUT": "sync output stimulus",
        "VP33": "P3V3_A supply", "RLOAD": "100k eurorack input",
        "R22N": "negative case: same input without D5 (not fitted)",
        "R23N": "negative case: same input without D5 (not fitted)",
        "R25N": "negative case: same input without D5 (not fitted)",
        "R27N": "negative case: same input without D5 (not fitted)",
        "Q1N": "negative case: unprotected input transistor (not fitted)",
    },
    "mux_adc.cir": {
        "VCH*": "eight channel-voltage stimuli (0.1 V apart)",
        "RSP*": "pot wiper source resistance (2.5 k = 10 k pot mid-travel)",
        "RONS*": "CD4051 ON resistance as a visible resistor (models.MUX_RON)",
        "S?": "one ideal switch per channel (behavioural CD4051)",
        "BSEL?": "1-of-8 decode from the three control levels",
        "CS": "ADC sample capacitance (models.ADC_SAMPLE_CAPACITANCE)",
        "RLEAK": "ADC leakage assumption (1 Meg)",
        "VP?": "channel-select stimuli (the firmware scan)",
        "X*": "settling cases (10 k and 100 k pot source resistance)",
        "VCHAN": "settling-case channel stimulus",
        "RSP": "settling-case pot resistance", "RON": "settling-case ON resistance",
        "SSW": "settling-case switch", "VSEL": "settling-case switch control",
        "CSM": "settling-case sample capacitor", "RLM": "settling-case leakage",
    },
    "front_audio.cir": {
        "VOUT": "idealised base-board output source (4.667 Vpk into 1 k)",
        "VIN": "idealised base-board input source (5 Vpk through 1 k)",
        "ROUTN": "1 k series output resistor case", "ROUTO": "1 k series output resistor case",
        "RLOADN": "100 k eurorack input", "RLOADO": "100 k eurorack input",
        "ROLDOUT": "REMOVED audio jack-LED branch (negative case, not fitted)",
        "ROLDIN": "REMOVED audio jack-LED branch (negative case, not fitted)",
        "DIN": "REMOVED audio jack-LED diode (negative case)",
        "DOUT": "REMOVED audio jack-LED diode (negative case)",
        "DLEDOUT": "REMOVED audio jack-LED (negative case)",
        "DLEDIN": "REMOVED audio jack-LED (negative case)",
        "RINN": "base-board input impedance case", "RINO": "base-board input impedance case",
        "RBASEINN": "base-board input impedance case", "RBASEINO": "base-board input impedance case",
    },
    "front_cv_led.cir": {
        "VP": "supply", "VM": "supply", "VIN": "ideal -4.99 V first-stage step",
        "RIN*": "modelled input resistor of the last stage (source: R37/R38)",
        "XOP*": "op-amp instance of the modelled last stage",
        "RL?": "100 k eurorack input", "CL?": "cable load capacitance case",
        "D?": "front-board jack-LED diode / LED (illustrative models)",
        "RLED*": "front-board jack-LED series resistor (source: euro R15)",
        "RSP*": "case copy of the pot resistance",
    },
    "front_mode.cir": {
        "VCC": "P3V3_A supply", "VCCN": "supply for the negative case",
        "D1": "button LED (illustrative model)",
        "ILEAK": "1 uA leakage assumption (mux/ADC leakage, not measured)",
        "D1P": "button LED, pressed case",
        "RSW": "switch resistance case", "SAMPLE": "ADC sample switch model",
        "VCTRL": "sample-switch control",
        "CADC": "100 pF sample-and-hold capacitance assumption",
        "R7N": "negative case: LED only, no pull-up (not fitted)",
        "D1N": "negative case: LED only (not fitted)",
        "ILEAKN": "negative case: same 1 uA leakage assumption",
        "R8P": "pull-up in the pressed case (source: R8 value)",
        "R7P": "LED series resistor in the pressed case (source: R7 value)",
    },
}

# Deck net -> (ref, pin, board): the deck's behavioural model uses the board's
# own net names, so a changed source connection must show up here.
DECK_NETS: dict[str, list[tuple[str, str, str, str]]] = {
    "mux_adc.cir": [
        ("MULTIPLEX_ADC_1", "U1", "X", "base"),
        ("MULTIPLEX_S0", "U1", "A", "base"),
        ("MULTIPLEX_S1", "U1", "B", "base"),
        ("MULTIPLEX_S2", "U1", "C", "base"),
    ] + [(f"M_ADC_{n}", "U1", f"X{n}", "base") for n in range(8)],
}

DECK_PARAMS: dict[str, list[ParamLink]] = {
    "cv_inout.cir": [
        PL("XIN_MCP", {"RIN": "R30", "RF": "R33", "RREF": "R34"}),
        PL("XOUT_NOM", {"R1": "R31", "RF1": "R35", "R2": "R37", "RF2": "R40"}),
        PL("XS_10N_C", {"CF": "C34"}),
    ],
}

DECK_LINKS: dict[str, list[Link]] = {
    "audio_in.cir": [
        L("R1", "R1", [("J_AUDIO_IN_L", "pin1"), ("U2A_IN_N", "pin2")]),
        L("R4", "R4", [("U2A_IN_N", "pin1"), ("NET_C2_2", "pin2")]),
        L("C2", "C2", [("U2A_IN_N", "pin1"), ("NET_C2_2", "pin2")]),
        L("R5", "R5", [("NET_C2_2", "pin1"), ("U2B_IN_N", "pin2")]),
        L("R8", "R8", [("U2B_IN_N", "pin1"), ("NET_C7_2", "pin2")]),
        L("C7", "C7", [("U2B_IN_N", "pin1"), ("NET_C7_2", "pin2")]),
        L("R9", "R9", [("NET_C7_2", "pin1"), ("AUDIO_IN_L", "pin2")]),
        L("XU2A", "U2", [("0", "IN1P"), ("U2A_IN_N", "IN1N"), ("NET_C2_2", "OUT1"),
                         ("VCC", "VPLUS"), ("VEE", "VMINUS")]),
        L("XU2B", "U2", [("0", "IN2P"), ("U2B_IN_N", "IN2N"), ("NET_C7_2", "OUT2"),
                         ("VCC", "VPLUS"), ("VEE", "VMINUS")]),
        L("RSR1", "R1"), L("RSR4", "R4"), L("CSC2", "C2"),
    ],
    "audio_out.cir": [
        L("R11", "R11", [("AUDIO_OUT_L", "pin1"), ("U3A_IN_N", "pin2")]),
        L("R13", "R13", [("U3A_IN_N", "pin1"), ("NET_C8_2", "pin2")]),
        L("C8", "C8", [("U3A_IN_N", "pin1"), ("NET_C8_2", "pin2")]),
        L("R16", "R16", [("NET_C8_2", "pin1"), ("U3B_IN_N", "pin2")]),
        L("R17", "R17", [("U3B_IN_N", "pin1"), ("NET_C12_2", "pin2")]),
        L("C12", "C12", [("U3B_IN_N", "pin1"), ("NET_C12_2", "pin2")]),
        L("R19", "R19", [("NET_C12_2", "pin1"), ("J_AUDIO_OUT_L", "pin2")]),
        L("XU3A", "U3", [("0", "IN1P"), ("U3A_IN_N", "IN1N"), ("NET_C8_2", "OUT1"),
                         ("VCC", "VPLUS"), ("VEE", "VMINUS")]),
        L("XU3B", "U3", [("0", "IN2P"), ("U3B_IN_N", "IN2N"), ("NET_C12_2", "OUT2"),
                         ("VCC", "VPLUS"), ("VEE", "VMINUS")]),
        L("RLD16", "R16"), L("RLD17", "R17"), L("CLD12", "C12"), L("RLDRS", "R19"),
    ],
    "cv_inout.cir": [
        L("C16", "C16"),
        L("XIN_MCP", "U13", [("J_CV_IN_1", ("R30", "pin1")),
                             ("CVIN_MCP", ("net", "CV_IN_1")),
                             ("P3V3_A", "VDD"), ("0", "VSS"),
                             ("N10V_REF", ("R34", "pin1"))]),
        L("R46", "R46"),
        L("XOUT_NOM", "U11", [("CV_OUT_1", "IN1P"),
                              ("CVOUT_NOM", ("net", "U11A_IN_N")),
                              ("VCC", "VPLUS"), ("VEE", "VMINUS")]),
    ],
    "ref_bias.cir": [
        L("R41", "R41", [("N10V_REF", "pin1"), ("VEE", "pin2")]),
        L("C20", "C20", [("N10V_REF", "pin2"), ("0", "pin1")]),
        L("R34", "R34", [("N10V_REF", "pin1")], partial=True,
          note="other end is the U13 summing virtual ground, modelled as 0 V"),
        L("R43", "R43", [("N10V_REF", "pin1")], partial=True,
          note="other end is the U13 summing virtual ground, modelled as 0 V"),
        L("XU14", "U14", [("N10V_REF", "ANODE")], partial=True,
          note="the cathode is behind a 0 V sense source, as before"),
    ],
    "sync_io.cir": [
        L("R22", "R22", [("J_SYNC_IN", "pin1"), ("NET_Q1_1", "pin2")]),
        L("D5", "D5", [("0", "anode"), ("NET_Q1_1", "cathode")]),
        L("R23", "R23", [("NET_Q1_1", "pin2"), ("0", "pin1")]),
        L("R25", "R25", [("P3V3_A", "pin2"), ("NET_Q1_3", "pin1")]),
        L("R27", "R27", [("NET_Q1_3", "pin1"), ("SYNC_IN", "pin2")]),
        L("Q1", "Q1", [("NET_Q1_3", "collector"), ("NET_Q1_1", "base"), ("0", "emitter")]),
        L("R21", "R21", [("SYNC_OUT", "pin1"), ("U11B_IN_N", "pin2")]),
        L("R24", "R24", [("U11B_IN_N", "pin1"), ("NET_R24_2", "pin2")]),
        L("R26", "R26", [("NET_R24_2", "pin1"), ("U11C_IN_N", "pin2")]),
        L("R28", "R28", [("U11C_IN_N", "pin1"), ("NET_R28_2", "pin2")]),
        L("R29", "R29", [("NET_R28_2", "pin1"), ("J_SYNC_OUT", "pin2")]),
        L("XU11B", "U11", [("0", "IN2P"), ("U11B_IN_N", "IN2N"), ("NET_R24_2", "OUT2"),
                           ("VCC", "VPLUS"), ("VEE", "VMINUS")]),
        L("XU11C", "U11", [("0", "IN3P"), ("U11C_IN_N", "IN3N"), ("NET_R28_2", "OUT3"),
                           ("VCC", "VPLUS"), ("VEE", "VMINUS")]),
    ],
    "mux_adc.cir": [],
    "front_audio.cir": [],
    "front_cv_led.cir": [
        L("RIN1", "R37"), L("RF1", "R40"), L("CF1", "C34"), L("RS1", "R46"),
        L("RIN2", "R38"), L("RF2", "R39"), L("CF2", "C33"), L("RS2", "R45"),
        L("RIN3", "R39"), L("RF3", "R39"), L("CF3", "C33"), L("RS3", "R45"),
        L("RLED", "R15", board="euro"), L("RLED3", "R15", board="euro"),
    ],
    "front_mode.cir": [
        L("R7", "R7", board="euro"), L("R8", "R8", board="euro"),
    ],
}


# --------------------------------------------------------------------------
# checks
# --------------------------------------------------------------------------

def _net_of(deck_node: str) -> str:
    return GROUND_NET if deck_node == GROUND_NODE else deck_node


def _reason(deck: str, name: str) -> str | None:
    for pattern, reason in GLOBAL_ASSUMPTIONS.items():
        if fnmatch.fnmatchcase(name, pattern):
            return reason
    for pattern, reason in ASSUMPTIONS.get(deck, {}).items():
        if fnmatch.fnmatchcase(name, pattern):
            return reason
    return None


def _component(boards: tuple[Board, Board], ref: str, board: str) -> Component | None:
    for candidate in boards:
        if candidate.name != board:
            continue
        if ref in candidate.components:
            return candidate.components[ref]
    return None


def check_deck(deck_path: Path, links: list[Link], deck: str,
               boards: tuple[Board, Board]) -> list[str]:
    """Every disagreement between one deck and the source, as text."""
    problems: list[str] = []
    elements = load_deck(deck_path)
    linked: set[str] = set()
    for link in links:
        element = elements.get(link.spice)
        if element is None:
            problems.append(f"{deck_path.name}: linked element {link.spice} is gone "
                            f"(it stands for {link.ref})")
            continue
        linked.add(link.spice)
        component = _component(boards, link.ref, link.board)
        if component is None:
            problems.append(f"{deck_path.name}: {link.spice} links to {link.ref}, "
                            f"which is not in the {link.board} source")
            continue

        if element.kind in ("resistor", "capacitor", "inductor"):
            if not element.value or "{" in element.value:
                problems.append(f"{deck_path.name}: {link.spice} has no literal value "
                                f"({element.value!r})")
            elif component.value is None:
                problems.append(f"{deck_path.name}: {link.spice} links to {link.ref}, "
                                f"whose source value could not be parsed "
                                f"({component.value_text!r} in {component.file})")
            else:
                try:
                    deck_value = spice_number(element.value, f"{deck_path.name}:{link.spice}")
                except SimError as exc:
                    problems.append(str(exc))
                else:
                    if not check_values.close(component.value, deck_value):
                        problems.append(
                            f"{deck_path.name}: {link.spice} = {element.value} but the source "
                            f"{link.ref} in {component.file} is {component.value_text} "
                            f"({component.value:g} vs {deck_value:g})")

        if link.pairs is not None:
            checks: list[tuple[str, str, str]] = []  # (deck node, expected net, shown)
            for deck_node, spec in link.pairs:
                if isinstance(spec, tuple) and spec[0] == "net":
                    wanted = spec[1]
                    board = boards[[b.name for b in boards].index(link.board)]
                    if not any(wanted in (comp.pins.get(pin) for pin in comp.pins)
                               for comp in board.components.values()):
                        problems.append(f"{deck_path.name}: {link.spice} stands for the "
                                        f"source net {wanted}, which no longer exists")
                    # a renamed instance node: presence is checked, not the name
                    checks.append((deck_node, None, f"source net {wanted}"))
                    continue
                if isinstance(spec, tuple):
                    owner_ref, pin = spec
                    owner = _component(boards, owner_ref, link.board)
                    if owner is None:
                        problems.append(f"{deck_path.name}: {link.spice}: the link names "
                                        f"{owner_ref}.{pin}, which is not in the source")
                        continue
                    net, shown = owner.pins.get(pin), f"{owner_ref} pin {pin}"
                else:
                    net, shown = component.pins.get(spec), f"{link.ref} pin {spec}"
                if net is None:
                    problems.append(f"{deck_path.name}: {link.spice}: source {shown} is "
                                    f"not connected -- source connection changed")
                    continue
                checks.append((deck_node, net, shown))

            if link.partial:
                # only the declared nodes are checked; `note` says why the rest
                # of the node list is a model approximation
                for deck_node, net, shown in checks:
                    if deck_node not in element.nodes:
                        problems.append(f"{deck_path.name}: {link.spice} no longer has the "
                                        f"node {deck_node} ({shown})")
                    elif net is not None and _net_of(deck_node) != net:
                        problems.append(f"{deck_path.name}: {link.spice} node {deck_node} "
                                        f"should be net {net} ({shown})")
            else:
                declared = [node for node, _, _ in checks]
                polar = element.kind in ("diode", "subcircuit")
                if (element.nodes != declared) if polar else (sorted(element.nodes) != sorted(declared)):
                    problems.append(
                        f"{deck_path.name}: {link.spice} connects {element.nodes}, the link "
                        f"declares {declared} for {link.ref} -- the deck's connection changed")
                for deck_node, net, shown in checks:
                    if net is not None and deck_node in element.nodes \
                            and _net_of(deck_node) != net:
                        problems.append(
                            f"{deck_path.name}: {link.spice} node {deck_node} should be net "
                            f"{net} ({shown}) -- source connection changed")

    for name, element in elements.items():
        if name in linked or _reason(deck, name):
            continue
        if element.kind in ("resistor", "capacitor", "inductor", "diode", "subcircuit"):
            problems.append(f"{deck_path.name}: element {name} is neither linked to a "
                            f"source part nor declared in ASSUMPTIONS")
    return problems


def check_deck_params(deck_path: Path, links: list[ParamLink],
                      boards: tuple[Board, Board]) -> list[str]:
    """Instance parameters that carry fitted values must match the source."""
    problems: list[str] = []
    text = Path(deck_path).read_text()
    lines = {line.split()[0]: line for line in text.splitlines()
             if line.strip() and not line.lstrip().startswith(("*", "."))}
    for link in links:
        line = lines.get(link.spice)
        if line is None:
            problems.append(f"{deck_path.name}: instance {link.spice} is gone")
            continue
        params = dict(re.findall(r"([A-Za-z_]\w*)\s*=\s*([^\s]+)", line.split("PARAMS:")[-1])) \
            if "PARAMS:" in line else {}
        for param, ref in link.params:
            raw = params.get(param)
            component = _component(boards, ref, link.board)
            if raw is None:
                problems.append(f"{deck_path.name}: {link.spice} no longer sets {param} "
                                f"(it carries {ref})")
                continue
            if component is None or component.value is None:
                problems.append(f"{deck_path.name}: {link.spice} {param} links to {ref}, "
                                "which has no parsed source value")
                continue
            try:
                value = spice_number(raw, f"{deck_path.name}:{link.spice}.{param}")
            except SimError as exc:
                problems.append(str(exc))
                continue
            if not check_values.close(component.value, value):
                problems.append(f"{deck_path.name}: {link.spice} {param} = {raw} but the "
                                f"source {ref} is {component.value_text} "
                                f"({component.value:g} vs {value:g})")
    return problems


def check_deck_nets(deck_path: Path, entries: list[tuple[str, str, str, str]],
                    boards: tuple[Board, Board]) -> list[str]:
    problems: list[str] = []
    elements = load_deck(deck_path)
    known = set(elements)
    for net, ref, pin, board in entries:
        component = _component(boards, ref, board)
        if component is None:
            problems.append(f"{deck_path.name}: net check needs {ref} from the {board} source")
            continue
        actual = component.pins.get(pin)
        if actual != net:
            problems.append(f"{deck_path.name}: net {net} should be {ref} pin {pin} in the "
                            f"source, which carries {actual!r} -- source connection changed")
        if not any(net in element.nodes for element in elements.values()):
            problems.append(f"{deck_path.name}: net check declares {net} but no deck "
                            "element uses that node")
    del known
    return problems


def check_mux_rails(board: Board) -> list[str]:
    """U1's rails are an assumption of the behavioural mux, so check them."""
    problems: list[str] = []
    u1 = board.components.get("U1")
    if u1 is None or "VDD" not in u1.pins:
        return ["base: U1 (the CD4051) is gone -- mux_adc.cir models it"]
    for pin, net in (("VDD", "P3V3_A"), ("VSS", "GND"), ("VEE", "GND"), ("INH", "GND")):
        if u1.pins.get(pin) != net:
            problems.append(f"base: U1 pin {pin} is {u1.pins.get(pin)!r}, mux_adc.cir "
                            f"assumes {net}")
    return problems


def check_front_board(board: Board) -> list[str]:
    """The front board's documented removals and its jack-LED population."""
    problems: list[str] = []
    if not board.jack_leds:
        problems.append("euro: JACK_LEDS is empty -- front_cv_led.cir models a chain "
                        "that does not exist")
    nets = {entry.get("net", "") for entry in board.jack_leds}
    if not {"J_CV_OUT_1", "J_CV_OUT_2"} <= nets:
        problems.append("euro: the jack LEDs no longer sit on both CV outputs "
                        f"(found {sorted(nets)})")
    for entry in board.jack_leds:
        net = entry.get("net", "")
        if net.startswith("J_AUDIO"):
            problems.append(f"euro: jack LED {entry.get('name')} sits on {net}; the audio "
                            "jack LEDs were removed (THD) and front_audio.cir proves why")
        if not entry.get("base"):
            problems.append(f"euro: jack LED {entry.get('name')} has no base net stem")
    button = board.layout.get("BUTTON", {})
    for key in ("seriesR", "pullupR"):
        if key not in button:
            problems.append(f"euro BUTTON.{key} not found -- front_mode.cir transcribes it")
    return problems


def check_all(base_sections: Path | None = None, euro_sections: Path | None = None,
              deck_dir: Path | None = None, euro_layout: Path | None = None) -> list[str]:
    boards = (load_board("base", sections=base_sections),
              load_board("euro", sections=euro_sections, layout=euro_layout))
    directory = deck_dir or HERE
    problems: list[str] = []
    for deck, links in DECK_LINKS.items():
        path = directory / deck
        if not path.is_file():
            problems.append(f"deck {deck} is listed in DECK_LINKS but not found")
            continue
        problems.extend(check_deck(path, links, deck, boards))
        if deck in DECK_PARAMS:
            problems.extend(check_deck_params(path, DECK_PARAMS[deck], boards))
        if deck in DECK_NETS:
            problems.extend(check_deck_nets(path, DECK_NETS[deck], boards))
    problems.extend(check_mux_rails(boards[0]))
    problems.extend(check_front_board(boards[1]))
    return problems


# --------------------------------------------------------------------------
# drafting helper (development aid, not part of the checks)
# --------------------------------------------------------------------------

def draft(deck: str) -> None:
    base, euro = load_boards()
    elements = load_deck(HERE / deck)
    print(f"# draft links for {deck}")
    for name, element in elements.items():
        if element.kind not in ("resistor", "capacitor", "inductor", "diode", "subcircuit"):
            continue
        best = None
        for board in (base, euro):
            if name in board.components:
                best = (board.name, board.components[name])
                break
        if best is None:
            print(f"    # {name}: no source part of that name ({element.kind})")
            continue
        board_name, component = best
        pin_pairs = []
        for node in element.nodes:
            matches = [pin for pin, net in component.pins.items() if net == _net_of(node)]
            pin_pairs.append((node, matches[0] if matches else "?"))
        print(f"    L({name!r}, {component.ref!r}, {pin_pairs!r}, board={board_name!r}),")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--base-sections", type=Path, default=None)
    parser.add_argument("--euro-sections", type=Path, default=None)
    parser.add_argument("--euro-layout", type=Path, default=None)
    parser.add_argument("--deck-dir", type=Path, default=None)
    parser.add_argument("--draft", metavar="DECK")
    args = parser.parse_args(argv)
    if args.draft:
        draft(args.draft)
        return 0
    problems = check_all(args.base_sections, args.euro_sections, args.deck_dir,
                         args.euro_layout)
    if problems:
        print(f"source linkage: {len(problems)} problem(s)")
        for problem in problems:
            print(f"  {problem}")
        return 1
    links = sum(len(links) for links in DECK_LINKS.values())
    nets = sum(len(entries) for entries in DECK_NETS.values())
    print(f"source linkage: {len(DECK_LINKS)} decks, {links} linked elements, "
          f"{nets} net links, {len(ASSUMPTIONS)} documented assumption tables")
    return 0


if __name__ == "__main__":
    sys.exit(main())

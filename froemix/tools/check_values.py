#!/usr/bin/env python3
"""Check the passive values in the tscircuit source against the built circuit.

Usage:
    python3 froemix/tools/check_values.py [--circuit dist/index/circuit.json]
                                          [--sections tscircuit/sections]

DESIGN-REVIEW R07: the installed value parser silently truncates RKM literals
(`4K7` -> 4k, `49K9` -> 49k), so the built resistance, the schematic and the
exported JLCPCB BOM comment all disagreed with the selected 4.7k/49.9k parts
and the parts gate still passed. This check closes that hole from two sides:

  1. literal vs build - every `<resistor|capacitor|inductor>` value literal in
     sections/*.tsx is converted to SI (ohm / farad / henry) and compared with
     the numeric value in the BUILT circuit.json within 0.5%. RKM-in-the-middle
     literals are rejected outright: they are exactly the strings the parser
     mis-reads, so the source must spell them `4.7k` / `49.9k` style.
  2. literal vs MPN - when the built component carries a
     `manufacturer_part_number` from a known passive family, the EIA
     digit+multiplier code in the MPN is decoded (0603WAF4701T5E -> 470 x 10^1
     = 4.7k) and compared with the literal. An MPN that does not match a known
     family is skipped silently: MPNs are not a general value syntax.

Components whose value is a prop expression (`resistance={...}`) are skipped -
there is no literal to check.

Exits non-zero and lists every mismatch.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys

# SI prefixes. The literal carries the prefix and (optionally) the unit
# designator; bare numbers are in the component's base unit.
PREFIX = {
    "p": 1e-12,
    "n": 1e-9,
    "u": 1e-6,
    "µ": 1e-6,
    "μ": 1e-6,
    "m": 1e-3,
    "": 1.0,
    "k": 1e3,
    "K": 1e3,
    "M": 1e6,
    "G": 1e9,
}

VALUE_PROP = {"resistor": "resistance", "capacitor": "capacitance", "inductor": "inductance"}
BASE_UNIT = {"resistor": "ohm", "capacitor": "farad", "inductor": "henry"}
# unit designator a literal may carry in front of the number's prefix
UNIT_LETTERS = {"resistor": ("", "R", "Ω"), "capacitor": ("", "F"), "inductor": ("", "H")}

LITERAL_RE = re.compile(r"^(\d+(?:\.\d+)?)\s*([pnuµmkKMG]?)\s*([A-Za-zΩ]*)$")
# `4K7` / `49K9` / `2R2`: digits, a multiplier letter, more digits. The
# installed parser drops everything after the letter.
RKM_RE = re.compile(r"^(\d+)([pnuµmkKMGRr])(\d+)\s*[A-Za-zΩ]*$")

ELEMENT_RE = re.compile(r"<(resistor|capacitor|inductor)\b(.*?)/>", re.S)
ATTR_RE = re.compile(r'([A-Za-z_][A-Za-z0-9_]*)\s*=\s*("(?:[^"]*)"|\{)')

# EIA digit+multiplier codes per MPN family: (regex with the code in group 1,
# component kind, code unit in base units). Only families whose code position
# is unambiguous are listed; everything else is skipped.
MPN_FAMILIES = [
    # UNI-ROYAL thick-film chip resistors: 0603WAF4701T5E, 1206W4F1004T5E,
    # 25121WF330JT4E (330 = 33 ohm).
    (re.compile(r"^(?:0201|0402|0603|0805|1206|1210|2010|2512)[A-Z0-9]{1,3}F"
                r"(\d{3,4}|R\d{2,3})[A-Z0-9]*$"), "resistor", 1.0),
    # YAGEO/Phycomp MLCC: CC0603KRX7R9BB104 -> last three digits in pF.
    (re.compile(r"^CC(?:0201|0402|0603|0805|1206|1210)[A-Z0-9]+(\d{3})$"),
     "capacitor", 1e-12),
    # Samsung CL series: CL31A106KBHNNNE -> 106 = 10 uF.
    (re.compile(r"^CL\d{2}[A-Z](\d{3})[A-Z][A-Z0-9]*$"), "capacitor", 1e-12),
    # 0603B103K500NT-style MLCC: size, dielectric, three-digit pF code.
    (re.compile(r"^(?:0402|0603|0805|1206)[A-Z](\d{3})[A-Z][A-Z0-9]*$"),
     "capacitor", 1e-12),
    # TDK MLZ/MLP power inductors: MLZ1608A2R2WT000 -> 2R2 = 2.2 uH.
    (re.compile(r"^ML[A-Z]\d{4}[A-Z](\d+R\d+)[A-Z][A-Z0-9]*$"), "inductor", 1e-6),
]


def parse_literal(text: str, kind: str) -> float:
    """'4.7k' / '100nF' / '2.2uH' -> base-unit value; raises ValueError."""
    literal = text.strip()
    if not literal:
        raise ValueError("empty value")
    rkm = RKM_RE.match(literal)
    if rkm:
        whole, marker, frac = rkm.groups()
        suggestion = f"{whole}.{frac}{'k' if marker == 'K' else marker}"
        raise ValueError(
            f"RKM literal {literal!r} is truncated by the installed value parser "
            f"(write {suggestion})"
        )
    match = LITERAL_RE.match(literal)
    if not match:
        raise ValueError(f"cannot read {literal!r} as a {BASE_UNIT[kind]} value")
    number, prefix, unit = match.groups()
    if unit not in UNIT_LETTERS[kind]:
        allowed = "/".join(u for u in UNIT_LETTERS[kind] if u) or "no unit"
        raise ValueError(f"{literal!r}: unit {unit!r} does not fit a {kind} ({allowed})")
    return float(number) * PREFIX[prefix]


def code_value(code: str) -> float | None:
    """EIA digit+multiplier code -> multiplier count ('4701' -> 4700)."""
    if "R" in code:
        whole, _, frac = code.partition("R")
        return float(f"{whole or 0}.{frac}")
    if len(code) == 3:
        return int(code[:2]) * 10 ** int(code[2])
    if len(code) == 4:
        return int(code[:3]) * 10 ** int(code[3])
    return None


def mpn_value(mpn: str, kind: str) -> float | None:
    """Manufacturer part number -> base-unit value, or None if it does not parse."""
    for pattern, family_kind, unit in MPN_FAMILIES:
        if family_kind != kind:
            continue
        match = pattern.match(mpn.strip().upper())
        if not match:
            continue
        count = code_value(match.group(1))
        return None if count is None else count * unit
    return None


def built_value(entry: dict, kind: str) -> tuple[float | None, str]:
    """(value, how) for the built component; inductors are stored as text."""
    raw = entry.get(VALUE_PROP[kind])
    if isinstance(raw, bool) or raw is None:
        return None, "missing"
    if isinstance(raw, (int, float)):
        return float(raw), "number"
    if isinstance(raw, str):
        try:
            return parse_literal(raw, kind), "text"
        except ValueError:
            return None, "unreadable"
    return None, "missing"


def parse_sections(section_dir: str):
    """[{'ref','kind','literal','expression','file'}] for every passive element."""
    found = []
    for name in sorted(os.listdir(section_dir)):
        if not name.endswith(".tsx"):
            continue
        path = os.path.join(section_dir, name)
        source = open(path).read()
        for element in ELEMENT_RE.finditer(source):
            kind, body = element.group(1), element.group(2)
            props = {}
            expressions = set()
            for attr in ATTR_RE.finditer(body):
                key, raw = attr.group(1), attr.group(2)
                if raw == "{":
                    expressions.add(key)
                else:
                    props[key] = raw[1:-1]
            ref = props.get("name")
            if not ref:
                continue
            prop = VALUE_PROP[kind]
            found.append({
                "ref": ref,
                "kind": kind,
                "literal": props.get(prop),
                "expression": prop in expressions,
                "file": name,
            })
    return found


def close(expected: float, got: float) -> bool:
    return abs(got - expected) <= abs(expected) * 0.005


def main() -> None:
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    ap = argparse.ArgumentParser()
    ap.add_argument("--circuit", default=os.path.join(root, "tscircuit", "dist", "index", "circuit.json"))
    ap.add_argument("--sections", default=os.path.join(root, "tscircuit", "sections"))
    args = ap.parse_args()

    circuit = json.load(open(args.circuit))
    built = {e["name"]: e for e in circuit
             if e["type"] == "source_component" and e.get("name")}

    source = parse_sections(args.sections)
    problems = []
    checked = with_mpn = expressions = 0
    for entry in source:
        ref, kind = entry["ref"], entry["kind"]
        if entry["literal"] is None:
            if entry["expression"]:
                expressions += 1
            else:
                problems.append((ref, f"no {VALUE_PROP[kind]} literal in {entry['file']}"))
            continue
        try:
            literal_value = parse_literal(entry["literal"], kind)
        except ValueError as error:
            problems.append((ref, f"{entry['file']}: {error}"))
            continue

        component = built.get(ref)
        if component is None:
            problems.append((ref, "not in the built circuit - run `bun run build`"))
            continue

        value, how = built_value(component, kind)
        checked += 1
        if value is None:
            problems.append((ref, f"built {VALUE_PROP[kind]} is {how!r} - cannot compare"))
        elif not close(literal_value, value):
            problems.append((ref, f"source {entry['literal']} ({literal_value:g} {BASE_UNIT[kind]}) "
                                  f"but the build has {value:g} ({how})"))

        mpn = component.get("manufacturer_part_number")
        if mpn:
            expected = mpn_value(mpn, kind)
            if expected is not None:
                with_mpn += 1
                if not close(expected, literal_value):
                    problems.append((ref, f"source {entry['literal']} ({literal_value:g} {BASE_UNIT[kind]}) "
                                          f"but MPN {mpn} is {expected:g} {BASE_UNIT[kind]}"))

    print(f"value check: {len(source)} passive literals, {checked} compared with the build, "
          f"{with_mpn} cross-checked against an MPN, {expressions} prop expressions skipped")
    if problems:
        print(f"\n{len(problems)} problem(s):")
        for ref, message in problems:
            print(f"  {ref}: {message}")
        sys.exit(1)
    print("all source literals match the built values and the manufacturer part numbers")


if __name__ == "__main__":
    main()

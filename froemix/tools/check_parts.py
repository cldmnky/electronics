#!/usr/bin/env python3
"""Check the JLCPCB parts against the schematic.

Usage:
    python3 froemix/tools/check_parts.py [--circuit dist/index/circuit.json] [--offline]

For every component with a JLCPCB (LCSC) part number this looks the part up in
the JLCPCB parts API and compares it with what the schematic says:

  * does the part number exist?
  * does the part type match (resistor/capacitor/inductor/diode/...)?
  * does the part value match the KiCad value (100k, 47pF, 2.2uH, ...)?
  * does the package match the footprint (0603 vs 0603, ...)?

Parts already owned (marked doNotPlace) and parts without an LCSC number are
listed but not treated as errors. Lookups are cached in .parts-cache.json.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
import time
import urllib.error
import urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from kicad2tscircuit import TSCIRCUIT_ONLY_REFS, build_components  # noqa: E402
from kicad_cli import KicadCliError, resolve_kicad_cli  # noqa: E402

JLC_API = "https://jlcpcb.com/api/overseas-pcb-order/v1/shoppingCart/smtGood/selectSmtComponentList"

SI = {"p": 1e-12, "n": 1e-9, "u": 1e-6, "µ": 1e-6, "μ": 1e-6, "m": 1e-3, "": 1, "k": 1e3, "K": 1e3, "M": 1e6, "G": 1e9}

VALUE_RE = re.compile(r"(?<![\d.])(\d+(?:\.\d+)?)\s*([pnuµμmMkKG]?)\s*(Ω|Ohm|ohms|R|F|H)\b")
RKM_RE = re.compile(r"^(\d+)([RKM])(\d*)$")


def parse_value(text: str, unit: str) -> float | None:
    """'100kΩ' / '47pF' / '2.2uH' / '49K9' / '33R' -> base unit value"""
    text = text.replace("Ohm", "Ω").replace("ohms", "Ω")
    candidates = []
    for match in VALUE_RE.finditer(text):
        number, prefix, u = match.groups()
        u = {"Ω": "R"}.get(u, u)
        if u != unit:
            continue
        candidates.append(float(number) * SI.get(prefix, 1))
    if candidates:
        return candidates[0]
    stripped = text.strip().replace("Ω", "R").replace("ohms", "R").replace(" ", "")
    rkm = RKM_RE.match(stripped)
    if rkm and unit == "R":
        whole, marker, frac = rkm.groups()
        return float(f"{whole}.{frac}") * SI[marker] if frac else float(whole) * SI[marker]
    rkm = RKM_RE.match(stripped[:-1] + "R" if stripped.endswith("R") else stripped)
    return None


def kicad_value(value: str, kind: str) -> float | None:
    """KiCad value string -> base unit value (ohm / farad / henry)"""
    unit = {"R": "R", "C": "F", "L": "H"}.get(kind)
    if not unit:
        return None
    return parse_value(value, unit)


TYPE_WORDS = {
    "R": ("resistor", "resistors"),
    "C": ("capacitor", "capacitors", "mlcc"),
    "L": ("inductor", "inductors", "ferrite"),
    "D": ("diode", "diodes", "rectifier", "schottky"),
    "Q": ("transistor", "transistors", "bjt", "mosfet"),
    "J": ("connector", "connectors", "header", "socket", "terminal"),
    "U": None,  # chips: many categories, only the package is checked
}

PACKAGE_FROM_FOOTPRINT = [
    (re.compile(r"(?:^|[^0-9])(0201|0402|0603|0805|1206|1210|2010|2512)(?:$|[^0-9])"), None),
]


def normalize_pkg(value: str | None) -> str:
    return re.sub(r"[^a-z0-9]", "", (value or "").lower())


def package_of_footprint(name: str) -> str | None:
    tail = name.split(":")[-1]
    match = re.search(r"(?:^|[^0-9])(0201|0402|0603|0805|1206|1210|2010|2512)(?:$|[^0-9])", tail)
    if match:
        return match.group(1)
    match = re.search(r"(SOT-?23|SOD-?123|SOD-?323|TSSOP-?14|SOIC-?8|SOP-?16)", tail, re.I)
    if match:
        return match.group(1).replace("-", "").lower()
    return None


class PartsAPI:
    def __init__(self, cache_path: str, offline: bool = False):
        self.cache_path = cache_path
        self.offline = offline
        self.cache = {}
        if os.path.exists(cache_path):
            try:
                self.cache = json.load(open(cache_path))
            except json.JSONDecodeError:
                self.cache = {}

    def save(self):
        json.dump(self.cache, open(self.cache_path, "w"), indent=1, sort_keys=True)

    def lookup(self, code: str) -> dict | None:
        if code in self.cache:
            return self.cache[code]
        if self.offline:
            return None
        payload = json.dumps({"currentPage": 1, "pageSize": 5, "keyword": code}).encode()
        request = urllib.request.Request(
            JLC_API, data=payload, headers={"Content-Type": "application/json"}
        )
        try:
            with urllib.request.urlopen(request, timeout=25) as response:
                body = json.load(response)
        except (urllib.error.URLError, TimeoutError) as error:
            print(f"  ! lookup for {code} failed: {error}", file=sys.stderr)
            return None
        finally:
            time.sleep(0.25)
        items = ((body.get("data") or {}).get("componentPageInfo") or {}).get("list") or []
        item = next((i for i in items if i.get("componentCode") == code), items[0] if items else None)
        if item:
            self.cache[code] = {
                "code": item.get("componentCode"),
                "type": item.get("componentTypeEn") or item.get("firstSortName") or "",
                "describe": item.get("describe") or "",
                "erp": item.get("erpComponentName") or "",
                "model": item.get("componentModelEn") or "",
                "brand": item.get("componentBrandEn") or "",
                "package": item.get("componentSpecificationEn") or "",
                "stock": item.get("stockCount"),
                "assembly": item.get("assemblyComponentFlag"),
            }
        else:
            self.cache[code] = {"code": code, "missing": True}
        self.save()
        return self.cache[code]


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    ap = argparse.ArgumentParser()
    ap.add_argument("--circuit", default=os.path.join(root, "tscircuit", "dist", "index", "circuit.json"))
    ap.add_argument("--src", default=os.path.join(root, "tmp"))
    ap.add_argument("--pcb", default=os.path.join(root, "tmp", "froemix.kicad_pcb"))
    # Portable default (explicit path, KICAD_CLI, PATH, macOS bundle): the
    # hard-coded macOS path made this check unusable on any other host.
    ap.add_argument("--kicad-cli", default=None)
    ap.add_argument("--offline", action="store_true")
    ap.add_argument("--cache", default=os.path.join(root, "tscircuit", ".parts-cache.json"))
    args = ap.parse_args()
    try:
        args.kicad_cli = resolve_kicad_cli(args.kicad_cli)
    except KicadCliError as exc:
        sys.exit(f"cannot run check:parts: {exc}")

    # what tscircuit ended up with (supplier parts, DNP)
    circuit = json.load(open(args.circuit))
    produced = {e.get("name"): dict(e) for e in circuit if e["type"] == "source_component"}
    names = {e["source_component_id"]: e.get("name") for e in circuit if e["type"] == "source_component"}
    for elm in circuit:
        if elm["type"] == "pcb_component" and elm.get("do_not_place"):
            entry = produced.get(names.get(elm["source_component_id"]))
            if entry is not None:
                entry["do_not_place"] = True
    # the schematic model (kind, value, KiCad footprint)
    model, _, _, _ = build_components(args.src, args.pcb, args.kicad_cli)
    bom = {comp.ref: comp for comp in model}

    api = PartsAPI(args.cache, args.offline)
    rows, problems = [], []
    seen = {}
    for name, comp in sorted(produced.items()):
        if name in TSCIRCUIT_ONLY_REFS:
            # bare-copper fabrication aids (test points) have no BOM part
            continue
        meta = bom.get(name)
        code = ((comp.get("supplier_part_numbers") or {}).get("jlcpcb") or [None])[0]
        kind = meta.kind if meta else name[:1].upper()
        value = (meta.value if meta else "") or ""
        footprint = (meta.kicad_footprint if meta else "") or ""
        if not code:
            if not comp.get("do_not_place"):
                problems.append((name, "no LCSC/JLCPCB part number"))
            continue
        info = seen.get(code) or api.lookup(code)
        seen[code] = info
        if not info or info.get("missing"):
            problems.append((name, f"{code} does not resolve to a JLCPCB part"))
            continue
        label = f"{info['describe'] or info['erp']}"
        rows.append((name, code, info.get("model", ""), info.get("package", ""), info.get("stock"), label[:60]))

        words = (info.get("type") or "") + " " + (info.get("describe") or "")
        expected = TYPE_WORDS.get(kind)
        if expected and not any(w in words.lower() for w in expected):
            problems.append((name, f"{code} looks like '{info.get('type')}', expected a {kind}"))
        want = kicad_value(value, kind)
        if want is not None:
            got = parse_value(info.get("erp") or "", {"R": "R", "C": "F", "L": "H"}[kind])
            if got is not None and abs(got - want) > max(want * 0.02, 1e-15):
                problems.append((name, f"{code} is {got:g}, schematic says {value} ({want:g})"))
            elif got is None:
                problems.append((name, f"{code}: cannot read a value from '{info.get('erp')}'"))
        want_pkg = normalize_pkg(package_of_footprint(footprint))
        got_pkg = normalize_pkg(info.get("package"))
        if want_pkg and got_pkg and want_pkg != got_pkg and want_pkg not in got_pkg and got_pkg not in want_pkg:
            problems.append((name, f"{code} package {got_pkg!r} vs footprint {footprint!r}"))

    width = max((len(r[0]) for r in rows), default=6)
    for name, code, model, package, stock, label in rows:
        print(f"{name:<{width}}  {code:<9} {package:<8} {(str(stock) or ''):>8}  {model:<22} {label}")

    print()
    print(f"{len(rows)} components with a JLCPCB part, {len(set(r[1] for r in rows))} unique parts")
    if problems:
        print(f"\n{len(problems)} problem(s):")
        for name, message in problems:
            print(f"  {name}: {message}")
        sys.exit(1)
    print("all JLCPCB parts match the schematic")


if __name__ == "__main__":
    main()

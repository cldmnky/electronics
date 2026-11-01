#!/usr/bin/env python3
"""Convert the Froemix KiCad project into tscircuit source.

Usage:
    python3 froemix/tools/kicad2tscircuit.py [--kicad-cli PATH] [--src DIR] [--out DIR]

The script
  1. exports a netlist from the KiCad schematics with `kicad-cli sch export netlist`
  2. parses the .kicad_sch files for symbol placement, pin names and pin types
  3. writes tscircuit TSX: index.circuit.tsx, sections/*.tsx, lib/*.tsx

Schematic sheets, reference designators, values, footprints, supplier parts and
the schematic placement are carried over from KiCad.  Everything under
`tscircuit/sections` and `tscircuit/lib` is generated - edit this script (or take
over the generated files) rather than hand patching.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys

# --------------------------------------------------------------------------- #
# sexpr parsing
# --------------------------------------------------------------------------- #


def tokenize(text: str):
    toks = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c in "()":
            toks.append(c)
            i += 1
        elif c == '"':
            j, buf = i + 1, []
            while j < n:
                if text[j] == "\\":
                    buf.append({"n": "\n", "t": "\t"}.get(text[j + 1], text[j + 1]))
                    j += 2
                    continue
                if text[j] == '"':
                    break
                buf.append(text[j])
                j += 1
            toks.append(("v", "".join(buf)))
            i = j + 1
        elif c.isspace():
            i += 1
        else:
            j = i
            while j < n and not text[j].isspace() and text[j] not in "()":
                j += 1
            toks.append(("v", text[i:j]))
            i = j
    return toks


def parse_sexpr(text: str):
    toks = tokenize(text)
    pos = 0

    def helper():
        nonlocal pos
        assert toks[pos] == "("
        pos += 1
        out = []
        while toks[pos] != ")":
            if toks[pos] == "(":
                out.append(helper())
            else:
                out.append(toks[pos][1])
                pos += 1
        pos += 1
        return out

    return helper()


def children(node, key):
    return [c for c in node if isinstance(c, list) and c and c[0] == key]


def child(node, key):
    found = children(node, key)
    return found[0] if found else None


# --------------------------------------------------------------------------- #
# KiCad data extraction
# --------------------------------------------------------------------------- #


def export_netlist(kicad_cli: str, sch: str) -> list:
    out = os.path.join("/tmp", "froemix.net")
    subprocess.run(
        [kicad_cli, "sch", "export", "netlist", "--format", "kicadsexpr", "-o", out, sch],
        check=True,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    tree = parse_sexpr(open(out, encoding="utf-8").read())
    components = []
    for comp in children(child(tree, "components"), "comp"):
        props = {}
        for prop in children(comp, "property"):
            name, value = child(prop, "name"), child(prop, "value")
            if name:
                props[name[1]] = value[1] if value and len(value) > 1 else ""
        libsource = child(comp, "libsource")
        libpart = None
        if libsource:
            part = child(libsource, "part")
            libpart = part[1] if part else None
        pins = []
        units = child(comp, "units")
        if units:
            for unit in children(units, "unit"):
                unit_name = child(unit, "name")
                for pin in children(child(unit, "pins"), "pin"):
                    num = child(pin, "num")
                    pins.append((unit_name[1] if unit_name else "A", num[1] if num else None))
        components.append(
            {
                "ref": child(comp, "ref")[1],
                "value": (child(comp, "value") or [None, None])[1],
                "footprint": (child(comp, "footprint") or [None, ""])[1],
                "libpart": libpart,
                "props": props,
                "pins": pins,
            }
        )
    nets = []
    for net in children(child(tree, "nets"), "net"):
        nodes = []
        for node in children(net, "node"):
            nodes.append(
                {
                    "ref": child(node, "ref")[1],
                    "pin": child(node, "pin")[1],
                    "function": (child(node, "pinfunction") or [None, ""])[1],
                    "type": (child(node, "pintype") or [None, ""])[1],
                }
            )
        nets.append({"name": child(net, "name")[1], "nodes": nodes})
    return components, nets


def symbol_pins(sym, unit):
    """pins of one unit of a lib_symbol definition"""
    pins = []
    for sub in children(sym, "symbol"):
        name = sub[1] if len(sub) > 1 and isinstance(sub[1], str) else ""
        parts = name.rsplit("_", 2)
        sub_unit = None
        if len(parts) >= 2:
            try:
                sub_unit = int(parts[-2])
            except ValueError:
                sub_unit = None
        if sub_unit not in (0, unit):
            continue
        for pin in children(sub, "pin"):
            pins.append(
                {
                    "number": (child(pin, "number") or [None, None])[1],
                    "name": (child(pin, "name") or [None, ""])[1],
                    "type": pin[1] if len(pin) > 1 else "",
                }
            )
    return pins


def parse_schematics(src_dir: str):
    """returns (placements, lib pins)

    placements: ref -> [{file, lib_id, x, y, rotation, unit}]
    libs: lib_symbol_id -> {pin_number: {name, type, unit}}
    """
    placements: dict[str, list] = {}
    libs: dict[str, dict] = {}
    for fname in sorted(os.listdir(src_dir)):
        if not fname.endswith(".kicad_sch"):
            continue
        tree = parse_sexpr(open(os.path.join(src_dir, fname), encoding="utf-8").read())
        lib_symbols = child(tree, "lib_symbols")
        if lib_symbols:
            for sym in children(lib_symbols, "symbol"):
                sym_name = sym[1]
                units = set()
                for sub in children(sym, "symbol"):
                    name = sub[1] if len(sub) > 1 and isinstance(sub[1], str) else ""
                    parts = name.rsplit("_", 2)
                    if len(parts) >= 2:
                        try:
                            units.add(int(parts[-2]))
                        except ValueError:
                            pass
                units = sorted(u for u in units if u > 0) or [1]
                pins = {}
                for unit in units:
                    for pin in symbol_pins(sym, unit):
                        pins[pin["number"]] = {
                            "name": pin["name"],
                            "type": pin["type"],
                            "unit": unit,
                        }
                libs.setdefault(sym_name, pins)
        for sym in children(tree, "symbol"):
            lib_id = child(sym, "lib_id")
            at = child(sym, "at")
            if not lib_id or not at:
                continue
            props = {}
            for prop in children(sym, "property"):
                if len(prop) > 2 and isinstance(prop[1], str):
                    props[prop[1]] = prop[2]
            ref = props.get("Reference")
            if not ref or ref.startswith("#"):
                continue  # power symbols / flags
            unit_node = child(sym, "unit")
            placements.setdefault(ref, []).append(
                {
                    "file": fname,
                    "lib_id": lib_id[1],
                    "x": float(at[1]),
                    "y": float(at[2]),
                    "rotation": float(at[3]) if len(at) > 3 else 0.0,
                    "unit": int(unit_node[1]) if unit_node else 1,
                }
            )
    return placements, libs


# --------------------------------------------------------------------------- #
# name mapping
# --------------------------------------------------------------------------- #

NET_RENAME = {
    "+12V": "P12V",
    "+5V": "P5V",
    "+3V3_A": "P3V3_A",
    "+3V3_D": "P3V3_D",
    "-12V": "N12V",
    "-10V_REF": "N10V_REF",
    # auto generated KiCad net names that would otherwise collide or read badly
    **{f"Net-(U{u}{c}--)": f"U{u}{c}_IN_N" for u in (2, 3, 11, 12) for c in "ABCD"},
    "Net-(U13--INA)": "U13_INA_N",
    "Net-(U13--INB)": "U13_INB_N",
    "Net-(U16-+VIN)": "U16_VIN_P",
    "Net-(U16--VIN)": "U16_VIN_N",
}

POWER_NET_RE = re.compile(r"^(P12V|N12V|P5V|P3V3_[AD]|N10V_REF|VCC|VEE)$")

# Phase-0 (power backbone) nets: nominal trace width in mm. Everything else
# routes in later phases at the board default.
POWER_TRACE_WIDTH = {
    "VCC": 0.6, "VEE": 0.4, "GND": 0.6, "P5V": 0.5,
    "P12V": 0.5, "N12V": 0.5, "P3V3_A": 0.4, "P3V3_D": 0.4,
    "N10V_REF": 0.3,
}

# Parts the user already owns; excluded from JLCPCB assembly
DNP_REFS = {"DaisySeed1"}

# 3D bodies for the dev-server / snapshot 3D view. Our footprints are exact
# KiCad copies, so tscircuit renders no bodies without models. These are the
# exact EasyEDA/LCSC models per part (`tsci import` output, harvested into
# tools/lcsc_models.json), with pcbRotationOffset corrected for the rotation
# between EasyEDA's footprints and ours (verified: our TSSOP/SOIC pad 1 sits
# top-left, EasyEDA's bottom-left). Parts without a transferable model
# (J1/J2/H1 headers, Daisy Seed) stay pad-only until STEP models are linked.
LCSC_MODELS_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "lcsc_models.json")


def lcsc_models():
    if not os.path.exists(LCSC_MODELS_PATH):
        return {}
    return json.load(open(LCSC_MODELS_PATH))

# Board sides. TOP faces the euro (front) board, BOTTOM faces out the back.
# The euro board closes over the top at header height (~10mm), so the top
# holds ONLY the mating headers and low-profile SMD: every tall body (Daisy
# module, electrolytics, SIP module, shrouded/box headers, right-angle PMOD)
# hangs off the back into the rack cavity, where depth is plentiful. USB,
# the Eurorack ribbon and the PMOD module all stay reachable from behind.
TOP_REFS = {
    "CN1", "CN2", "CN3", "CN4", "J1",  # mating interface to the euro board
    "C22", "C23", "C26", "C27",  # rail decoupling (1206, low profile)
    "L1", "L2",  # ferrites (0603)
    "D3", "D4",  # Schottkys (SOD-123)
}


def side_of(ref: str, sheet: str) -> str | None:
    """Layer for a component: None (top/front) or "bottom" (back)."""
    if ref in TOP_REFS:
        return None
    return "bottom"

# BOM fixes applied on top of the KiCad data (the KiCad project still carries the
# old values). Remove an entry once the schematic has been updated in KiCad.
PART_OVERRIDES = {
    "R23": {
        "lcsc": "C17927",
        "mpn": "1206W4F1004T5E",
        "brand": "UNI-ROYAL(Uniroyal Elec)",
        "why": "schematic points at C728283 = 1mOhm 1W current sense part, R23 is 1M",
    },
    "C6": {
        "lcsc": "C107054",
        "mpn": "CC0603JRNPO9BN561",
        "brand": "YAGEO",
        "why": "CL10C561JB8NNNC (C55393) is discontinued, 0 stock",
    },
    "C7": {
        "lcsc": "C107054",
        "mpn": "CC0603JRNPO9BN561",
        "brand": "YAGEO",
        "why": "CL10C561JB8NNNC (C55393) is discontinued, 0 stock",
    },
    "J2": {
        "lcsc": "C3406",
        "mpn": "",
        "brand": "BOOMELE(Boom Precision Elec)",
        "why": "no JLCPCB part number in the KiCad BOM; 2x8 2.54mm IDC box header",
    },
    # A2005WV headers: the same 2.00mm headers, but in stock. A2005WV-7P had 9 pcs
    # and A2005WV-8P 131 pcs left at JLCPCB.
    "CN4": {
        "lcsc": "C22438290",
        "mpn": "HX PZ2.0-1x7P ZZ",
        "brand": "hanxia",
        "why": "A2005WV-7P (C225259) has 9 pcs in stock",
    },
    "CN3": {
        "lcsc": "C22438291",
        "mpn": "HX PZ2.0-1x8P ZZ",
        "brand": "hanxia",
        "why": "A2005WV-8P (C225260) has 131 pcs in stock",
    },
    # H1 PMOD: 2x6 2.54mm right-angle. Body depth 8.5mm instead of the 12.5mm the
    # KiCad footprint draws, same 2.54mm pad grid.
    "H1": {
        "lcsc": "C22373932",
        "mpn": "HC-PM254-8.5H-2x6PW",
        "brand": "Hong Cheng",
        "why": "2.54-2*6PWZ (C60565) has 62 pcs in stock",
    },
    # U16 is fed differentially VCC-VEE (~23.4V). The B1205S (12V in,
    # 10.8-13.2V range) would be destroyed; the Patch reference uses a 24V
    # isolator in the same wiring. Same SIP4 footprint/pinout.
    "U16": {
        "lcsc": "C3031175",
        "mpn": "B2405S-1WR3",
        "brand": "MORNSUN",
        "why": "B1205S-1WR3 is 12V in (10.8-13.2V) but sees VCC-VEE ~23.4V; B2405S-1WR3 is 24V in (21.6-26.4V)",
    },
    # TLP2362 VO is open-collector: 33k gives sloppy rise times at 31.25kbps.
    # 2.2k is the proven center value for 3.3V MIDI.
    "R48": {
        "lcsc": "C4190",
        "mpn": "0603WAF2201T5E",
        "brand": "UNI-ROYAL(Uniroyal Elec)",
        "value": "2.2K",
        "why": "TLP2362 open-collector pull-up too weak at 33k for MIDI; 2.2k",
    },
    # Sync/gate out series R: 5V/100R = 50mA into a short exceeds what a TL074
    # can source. 1k like the audio/CV outs.
    "R29": {
        "lcsc": "C21190",
        "mpn": "0603WAF1001T5E",
        "brand": "UNI-ROYAL(Uniroyal Elec)",
        "value": "1K",
        "why": "100R gate out overloads the TL074 on short; 1k like the other outs",
    },
    # Audio out, DESIGN-REVIEW R08: a 1Vrms full-scale Seed output through the
    # 100k/10k stage asks for 14.1V peak and the TL074 clips at about 10.1V.
    # 33k makes the first stage x3.3 (4.62V peak at the jack), the 10k input R
    # and the unity second stage are unchanged.
    "R13": {
        "lcsc": "C4216",
        "mpn": "0603WAF3302T5E",
        "brand": "UNI-ROYAL(Uniroyal Elec)",
        "value": "33K",
        "why": "audio-out x10 clips at full scale; x3.3 fits the rails, R08",
    },
    "R14": {
        "lcsc": "C4216",
        "mpn": "0603WAF3302T5E",
        "brand": "UNI-ROYAL(Uniroyal Elec)",
        "value": "33K",
        "why": "audio-out x10 clips at full scale; x3.3 fits the rails, R08",
    },
    # CV-in feedback caps: 100nF across 33k = 48Hz LPF, kills audio-rate FM.
    # The Patch reference uses 10nF-class parts here (480Hz). Base part C57112.
    "C16": {
        "lcsc": "C57112",
        "mpn": "0603B103K500NT",
        "brand": "YAGEO",
        "value": "10nF",
        "why": "100nF feedback = 48Hz LPF on CV in; 10nF = 480Hz",
    },
    "C21": {
        "lcsc": "C57112",
        "mpn": "0603B103K500NT",
        "brand": "YAGEO",
        "value": "10nF",
        "why": "100nF feedback = 48Hz LPF on CV in; 10nF = 480Hz",
    },
    # CV-in summer ratios: 33k/200k map +/-5V onto exactly 0/3.3V, so the MCP6002
    # clips both endpoints. 30.1k/187k move 0..3.3V in to 0.104..3.114V, which
    # leaves 100mV of rail headroom while keeping the 0.301V/V scale (R30/R44 are
    # still 100k). DESIGN-REVIEW R10.
    "R33": {
        "lcsc": "C23000",
        "mpn": "0603WAF3012T5E",
        "brand": "UNI-ROYAL(Uniroyal Elec)",
        "value": "30.1K",
        "why": "CV-in summer ratio for rail headroom, design-review R10",
    },
    "R34": {
        "lcsc": "C22896",
        "mpn": "0603WAF1873T5E",
        "brand": "UNI-ROYAL(Uniroyal Elec)",
        "value": "187K",
        "why": "CV-in summer offset leg for rail headroom, design-review R10",
    },
    "R42": {
        "lcsc": "C23000",
        "mpn": "0603WAF3012T5E",
        "brand": "UNI-ROYAL(Uniroyal Elec)",
        "value": "30.1K",
        "why": "CV-in summer ratio for rail headroom, design-review R10",
    },
    "R43": {
        "lcsc": "C22896",
        "mpn": "0603WAF1873T5E",
        "brand": "UNI-ROYAL(Uniroyal Elec)",
        "value": "187K",
        "why": "CV-in summer offset leg for rail headroom, design-review R10",
    },
}

# Components missing from the KiCad schematics, added here so the tscircuit
# board is correct. Each entry becomes a real component in the generated
# project. Mirror them into KiCad and remove them here when convenient.
# (ref, kind, value, kicad footprint, sheet, {pin: net}, lcsc, mpn, why)
EXTRA_COMPONENTS = [
    # TLP2362 datasheet: 0.1uF bypass pin6-pin4 within 1cm, mandatory.
    ("C28", "C", "100nF", "jclpcb:C0603", "midi",
     {"1": "P3V3_D", "2": "GND"}, "C14663", "CC0603KRX7R9BB104",
     "TLP2362 VCC bypass, datasheet-mandatory"),
    # P3V3_D has no decoupling on the base board (only P3V3_A has C19).
    ("C29", "C", "100nF", "jclpcb:C0603", "main",
     {"1": "P3V3_D", "2": "GND"}, "C14663", "CC0603KRX7R9BB104",
     "P3V3_D decoupling at the Daisy/SD/OLED headers"),
    ("C30", "C", "10uF", "jclpcb:C1206", "main",
     {"1": "P3V3_D", "2": "GND"}, "C13585", "CL31A106KBHNNNE",
     "P3V3_D bulk for SD/OLED spikes"),
    # U1 (CD4051) ran on P3V3_A with no local bypass - C19 sits at U13, 25.6mm
    # away. DESIGN-REVIEW R06.
    ("C31", "C", "100nF", "jclpcb:C0603", "main",
     {"1": "P3V3_A", "2": "GND"}, "C14663", "CC0603KRX7R9BB104",
     "U1 VDD bypass, local to pin 16"),
    # U16 input capacitor, YLPTEC B_S-1WR3 datasheet Table 1 (24VDC -> Cin
    # 1uF/50V). C26 sits on VCC/VEE before L1/L2, so it is not this cap.
    # DESIGN-REVIEW R13.
    ("C32", "C", "1uF", "jclpcb:C0603", "power",
     {"1": "U16_VIN_P", "2": "U16_VIN_N"}, "C559769", "CC0603KRX7R9BB105",
     "U16 input bypass across VIN_P/VIN_N, datasheet table 1"),
    # CV out load-pole compensation, DESIGN-REVIEW R09 follow-up: the CV-out
    # feedback is taken after the 1k series resistor, so a capacitive load sits
    # inside the TL074 loop (18 deg phase margin at 1nF, ringing at 10nF).
    # 100pF across the 100k feedback resistor puts the compensation zero at
    # 15.9kHz, the worst-case 1k/10nF load pole, and caps the CV bandwidth at
    # ~16kHz. Pin 1 matches the R39/R40 pin 1 (the summing node).
    ("C33", "C", "100pF", "jclpcb:C0603", "cv",
     {"1": "U12D_IN_N", "2": "J_CV_OUT_2"}, "C14665", "CC0603JRNPO9BN101",
     "CV out channel 2 feedback compensation across R39, 15.9kHz zero"),
    ("C34", "C", "100pF", "jclpcb:C0603", "cv",
     {"1": "U12B_IN_N", "2": "J_CV_OUT_1"}, "C14665", "CC0603JRNPO9BN101",
     "CV out channel 1 feedback compensation across R40, 15.9kHz zero"),
    # I2C needs pull-ups; the Patch reference has 4K7 to +3V3_D.
    ("R51", "R", "4K7", "jclpcb:R0603", "main",
     {"1": "SCL", "2": "P3V3_D"}, "C23162", "0603WAF4701T5E",
     "I2C SCL pull-up, cf. Patch R77"),
    ("R52", "R", "4K7", "jclpcb:R0603", "main",
     {"1": "SDA", "2": "P3V3_D"}, "C23162", "0603WAF4701T5E",
     "I2C SDA pull-up, cf. Patch R89/R91"),
    # SD DAT/CMD need pull-ups; the Patch reference has 47K x5.
    ("R53", "R", "47K", "jclpcb:R0603", "main",
     {"1": "SD_DAT0", "2": "P3V3_D"}, "C25819", "0603WAF4702T5E",
     "SD DAT0 pull-up, cf. Patch R6"),
    ("R54", "R", "47K", "jclpcb:R0603", "main",
     {"1": "SD_DAT1", "2": "P3V3_D"}, "C25819", "0603WAF4702T5E",
     "SD DAT1 pull-up, cf. Patch R7"),
    ("R55", "R", "47K", "jclpcb:R0603", "main",
     {"1": "SD_DAT2", "2": "P3V3_D"}, "C25819", "0603WAF4702T5E",
     "SD DAT2 pull-up, cf. Patch R3"),
    ("R56", "R", "47K", "jclpcb:R0603", "main",
     {"1": "SD_DAT3", "2": "P3V3_D"}, "C25819", "0603WAF4702T5E",
     "SD DAT3 pull-up, cf. Patch R4"),
    ("R57", "R", "47K", "jclpcb:R0603", "main",
     {"1": "SD_CMD", "2": "P3V3_D"}, "C25819", "0603WAF4702T5E",
     "SD CMD pull-up, cf. Patch R5"),
    # MIDI OUT, DESIGN-REVIEW R02: the fitted 33R 0603 parts (C23140, 100mW)
    # dissipated 3.3^2/33 = 330mW into a grounded ring (367mW at 3.465V over
    # 32.67R) and R50 sat between the MCU pin and the jack. R59/R60 are the 1W
    # 2512 replacement behind Q2+R58, which move the MCU pin out of the fault
    # path - an emitter follower would not do, the collector has to sink the
    # loop current. Q2 pin1/2/3 = base/emitter/collector.
    #
    # The jack is TRS Type A (tip = DIN pin 4 = current source, the MIDI
    # Association default; the front board wires it straight through), so R59
    # 22R sources P3V3_D into the TIP and R60 33R is the RING sink behind Q2.
    # 22R keeps the source >=4.5mA into a worst-case receiver (1.235V over
    # 275.6R = 4.48mA) and dissipates 3.3^2/22 = 0.50W into a grounded tip;
    # R60 stays 33R because a 3.3V fault on the ring is 94mA through Q2 (10R
    # would be ~300mA).
    ("Q2", "Q", "MMBT3904", "jclpcb:SOT-23-3_L2.9-W1.6-P1.90-LS2.8-BR", "midi",
     {"1": "NET_Q2_B", "2": "GND", "3": "NET_Q2_C"}, "C181119", "MMBT3904",
     "MIDI OUT open-collector driver, keeps the MCU pin off the jack"),
    ("R58", "R", "4K7", "jclpcb:R0603", "midi",
     {"1": "USART1_TX", "2": "NET_Q2_B"}, "C23162", "0603WAF4701T5E",
     "MIDI OUT driver base resistor for Q2"),
    ("R59", "R", "22R", "jclpcb:R2512", "midi",
     {"1": "J_MIDI_OUT_TIP", "2": "P3V3_D"}, "C118189", "25121WF220JT4E",
     "MIDI OUT Type A tip current source 22R 1W 2512, replaces R49"),
    ("R60", "R", "33R", "jclpcb:R2512", "midi",
     {"1": "J_MIDI_OUT_RING", "2": "NET_Q2_C"}, "C53728", "25121WF330JT4E",
     "MIDI OUT Type A ring current sink 33R 1W 2512, replaces R50"),
    # SYNC IN reverse base-emitter clamp, DESIGN-REVIEW R04: Q1 is MMBT3904 with
    # the emitter at GND and its base fed from J_SYNC_IN through R22 100k, so a
    # -10V patch pulls the base to -9.1V against a 6V emitter-base rating. The
    # diode sits across base-emitter with the cathode on the base (pad 1 of the
    # SOD-323 is the cathode), so a negative patch clamps at about -0.6V and the
    # 100k only has to supply that diode current. Q1's positive behaviour is
    # unchanged (the diode is reverse biased for any base voltage above GND).
    ("D5", "D", "1N4148WS", "jclpcb:SOD-323_L1.8-W1.3-LS2.5-RD", "cv",
     {"1": "NET_Q1_1", "2": "GND"}, "C2128", "1N4148WST4",
     "SYNC IN reverse base-emitter clamp across Q1"),
]

# Parts removed from tscircuit/ on purpose; the KiCad project is frozen, so the
# check scripts drop these refs from the KiCad side before comparing instead of
# failing them as missing. See doc/DESIGN-REVIEW.md:
#   D2     - R01: directly in parallel with the U15 LED in the forward
#            direction, so it shunts the optocoupler and MIDI IN never works.
#            D1 (antiparallel) is the reverse-protection diode and stays.
#   R49,   - R02: replaced by the 1W 2512 R59/R60 behind the Q2 driver, whose
#   R50      connectivity deliberately differs (R50 no longer touches
#            USART1_TX).
REMOVED_REFS = {"D2", "R49", "R50"}  # removed on purpose, see doc/DESIGN-REVIEW.md R01

# Pins that deliberately moved to another net after the KiCad project was
# frozen. check_netlist.py applies these to the KiCad-side netlist before
# comparing (remove the pin from its old net, add it to the named net, then drop
# nets with fewer than two nodes); every other pin stays strictly compared.
# Refs and pins are strings exactly as they appear in the KiCad netlist.
#   U11.3,  U11.12 - R03: the two spare TL074 channels become unity-gain
#                     followers on the DAC outputs, so their non-inverting
#                     inputs leave GND and take the DAC nets (the OUT/IN-
#                     straps are unchanged, pins 1/2 and 13/14).
#   R31.1,  R32.1  - R03: the 33k CV-out summing resistors are fed from the
#                     follower outputs instead of directly from the DAC (which
#                     with its internal buffer disabled is a ~13k source).
#   R39.2,  R40.2  - R09: the CV-out feedback is picked up at the jack side of
#                     the 1k short-protection resistors, so the 1k sits inside
#                     the loop and the gain no longer depends on the load.
INTENTIONAL_PIN_MOVES = {
    ("U11", "3"): "CV_OUT_1",
    ("U11", "12"): "CV_OUT_2",
    ("R31", "1"): "U11A_IN_N",
    ("R32", "1"): "U11D_IN_N",
    ("R39", "2"): "J_CV_OUT_2",
    ("R40", "2"): "J_CV_OUT_1",
}

# Nets and footprints that exist ONLY in tscircuit/ (no KiCad counterpart).
# TSCIRCUIT_ONLY_NETS are created by the R02 driver: Q2 splits USART1_TX into
# the base net and the collector net, both internal to the MIDI OUT stage.
# TSCIRCUIT_ONLY_FOOTPRINTS holds the pad geometry (number, width, height, x, y
# in the footprint frame, before the bottom-side mirror) of footprints authored
# in tscircuit/lib/footprints.tsx because the KiCad library has no counterpart;
# check_footprints compares against this table, so keep the two pad-for-pad
# equal. R2512 is the 1W 33R land pattern: 1.5x2.8mm pads, 5.2mm centres.
TSCIRCUIT_ONLY_NETS = {"NET_Q2_B", "NET_Q2_C"}
TSCIRCUIT_ONLY_FOOTPRINTS = {
    "jclpcb:R2512": [("1", 1.5, 2.8, -2.6, 0.0), ("2", 1.5, 2.8, 2.6, 0.0)],
}

# Fabrication aids that exist ONLY in tscircuit/ and have no KiCad counterpart
# (8 bottom-side test points; the 4 fiducials are board-level pads with no
# source component). The check scripts strip/skip these refs instead of failing
# them as "not in the KiCad board"; nothing here is emitted by the generator.
# Fabrication aids added directly in tscircuit (no KiCad counterpart): test
# points and fiducials. check_footprints verifies these structurally (one round
# pad of the expected size) instead of against the KiCad board.
TSCIRCUIT_ONLY_REFS = {f"TP{i}" for i in range(1, 9)} | {f"FID{i}" for i in range(1, 5)}


def net_name(kicad_name: str) -> str | None:
    """KiCad net name -> tscircuit net name ([A-Za-z0-9_] only)."""
    if kicad_name.startswith("unconnected-"):
        return None
    if kicad_name in NET_RENAME:
        return NET_RENAME[kicad_name]
    name = kicad_name
    if name.startswith("Net-(") and name.endswith(")"):
        inner = name[5:-1]
        inner = inner.replace("+", "").replace("-", "_").replace("Pad", "")
        name = "NET_" + inner
    name = re.sub(r"[^A-Za-z0-9_]", "_", name)
    name = re.sub(r"_{2,}", "_", name).strip("_")
    if not name or name[0].isdigit():
        name = "N_" + name
    return name


def label(name: str) -> str:
    """pin name -> pin label ([A-Za-z0-9_] only)."""
    out = re.sub(r"[^A-Za-z0-9_]", "_", name)
    out = re.sub(r"_{2,}", "_", out).strip("_")
    return out


# Footprint mapping: KiCad footprint -> footprinter string.
# --- footprints -------------------------------------------------------------
# Every component gets the exact KiCad footprint: geometry is read from the board
# file (which embeds the full pad data), or from a .kicad_mod override.

FOOTPRINT_OVERRIDES = {
    # official Electrosmith Daisy footprints, checked in under tmp/library. The
    # copy embedded in froemix.kicad_pcb is pad-for-pad identical to this file.
    "Module:Electrosmith_Daisy_Seed": "library/DaisyKiCad-main/Daisy-Boards.pretty/DAISY_SEED.kicad_mod",
}


def num(value: float, digits: int = 4) -> str:
    text = f"{round(float(value), digits):f}".rstrip("0").rstrip(".")
    return text if text not in ("", "-0") else "0"


def mm(value: float) -> str:
    return f"{num(value)}mm"


def fp_const_name(kicad_name: str) -> str:
    parts = re.split(r"[^A-Za-z0-9]+", kicad_name)
    return "FP_" + "_".join(p for p in parts if p)


# Footprints used with a nonzero pcbRotation. tscircuit rotates courtyard
# *centers* but keeps their *axes*, skewing every rotated courtyard, so these
# footprints are pre-rotated instead and placed with rotation 0.
PRE_ROTATED_FOOTPRINTS = {
    # J2 shroud: long axis along the bottom edge
    "4ms_Connector:Pins_2x08_2.54mm_TH_EurorackPower_Shrouded": 90,
    # H1 right-angle PMOD: opening faces the right board edge
    "jclpcb:HDR-TH_12P-P2.54-H-M-R2-C6-S2.54-W12.5": 90,
    # Daisy Seed laid horizontally under the connector band: as a 53x19 bar it
    # frees the whole lower board for the analog chains (was a 19x53 column in
    # the middle, forcing long routes and leaving the lower right idle)
    "Module:Electrosmith_Daisy_Seed": 90,
}


def _rot_point(x: float, y: float, angle: int) -> tuple[float, float]:
    angle %= 360
    if angle == 90:
        return (-y, x)
    if angle == 180:
        return (-x, -y)
    if angle == 270:
        return (y, -x)
    return (x, y)


class FootprintLibrary:
    """KiCad footprints -> tscircuit <footprint> elements."""

    def __init__(self, src_dir: str, pcb_path: str):
        self.defs: dict[str, list] = {}
        self.origin: dict[str, str] = {}
        tree = parse_sexpr(open(pcb_path, encoding="utf-8").read())
        for fp in children(tree, "footprint"):
            if fp[1] not in self.defs:
                self.defs[fp[1]] = fp
                self.origin[fp[1]] = os.path.basename(pcb_path)
        for kicad_name, rel_path in FOOTPRINT_OVERRIDES.items():
            path = os.path.join(src_dir, rel_path)
            if not os.path.exists(path):
                continue
            self.defs[kicad_name] = parse_sexpr(open(path, encoding="utf-8").read())
            self.origin[kicad_name] = rel_path
        for kicad_name, angle in PRE_ROTATED_FOOTPRINTS.items():
            if kicad_name in self.defs:
                vname = f"{kicad_name}@R{angle}"
                self.defs[vname] = self._rotated(self.defs[kicad_name], angle)
                self.origin[vname] = self.origin[kicad_name] + f" (pre-rotated {angle})"

    @staticmethod
    def _rotated(fp, angle: int):
        """Deep copy of a footprint tree with all geometry rotated CCW."""
        import copy
        fp = copy.deepcopy(fp)
        for pad in children(fp, "pad"):
            at = child(pad, "at")
            x, y = _rot_point(float(at[1]), float(at[2]), angle)
            at[1], at[2] = num(x), num(y)
            if len(at) > 3:
                at[3] = num((float(at[3]) + angle) % 360)
            size = child(pad, "size")
            if size and angle % 180:
                size[1], size[2] = size[2], size[1]
        for key in ("fp_line", "fp_rect", "fp_circle", "fp_arc"):
            for node in children(fp, key):
                for pkey in ("start", "end", "center", "mid"):
                    point = child(node, pkey)
                    if point:
                        x, y = _rot_point(float(point[1]), float(point[2]), angle)
                        point[1], point[2] = num(x), num(y)
        return fp

    def variant_base(self, name: str) -> str:
        return name.split("@R")[0] if "@R" in name else name

    def variant_for(self, kicad_name: str) -> str:
        """Footprint name to use for a component (pre-rotated variant if any)."""
        for base, angle in PRE_ROTATED_FOOTPRINTS.items():
            if base == kicad_name:
                return f"{base}@R{angle}"
        return kicad_name

    # -- board + sizes ----------------------------------------------------
    def load_board(self, pcb_path: str):
        """board outline size, footprint sizes and footprint origins"""
        tree = parse_sexpr(open(pcb_path, encoding="utf-8").read())
        xs, ys = [], []
        for node in children(tree, "gr_line") + children(tree, "gr_rect"):
            layer = child(node, "layer")
            if not layer or layer[1] != "Edge.Cuts":
                continue
            for key in ("start", "end"):
                point = child(node, key)
                if point:
                    xs.append(float(point[1]))
                    ys.append(float(point[2]))
        self.board = (round(max(xs) - min(xs), 2), round(max(ys) - min(ys), 2)) if xs else (0.0, 0.0)
        self.sizes = {}
        self.centers = {}
        self.keepout = {}
        for name, fp in self.defs.items():
            lo_x = lo_y = float("inf")
            hi_x = hi_y = float("-inf")
            for pad in children(fp, "pad"):
                at, size = child(pad, "at"), child(pad, "size")
                px, py = float(at[1]), float(at[2])
                pw, ph = float(size[1]), float(size[2])
                if round(float(at[3]) if len(at) > 3 else 0.0) % 180:
                    pw, ph = ph, pw
                lo_x, hi_x = min(lo_x, px - pw / 2), max(hi_x, px + pw / 2)
                lo_y, hi_y = min(lo_y, py - ph / 2), max(hi_y, py + ph / 2)
            for layer in ("F.SilkS", "F.CrtYd"):
                for path in self._graphics(fp, layer):
                    lo_x = min(lo_x, min(px for px, _ in path))
                    hi_x = max(hi_x, max(px for px, _ in path))
                    lo_y = min(lo_y, min(py for _, py in path))
                    hi_y = max(hi_y, max(py for _, py in path))
            if lo_x == float("inf"):
                self.sizes[name], self.centers[name] = (1.0, 1.0), (0.0, 0.0)
                self.keepout[name] = (1.0, 1.0)
                continue
            self.sizes[name] = (round(hi_x - lo_x, 4), round(hi_y - lo_y, 4))
            # tscircuit positions a footprint by its local origin, so the offset
            # from the origin to the centre of the geometry must be subtracted
            self.centers[name] = (round((hi_x + lo_x) / 2, 4), round(-(hi_y + lo_y) / 2, 4))
            # courtyard keepout for packing/collision tests: real F.CrtYd box
            # if present, else the pads/silk bbox + 0.25mm/side the generator
            # derives, plus a 0.2mm margin. Packing by footprint size alone
            # lets wider courtyards touch (C14/C15 did).
            paths = [p for p in self._graphics(fp, "F.CrtYd") if len(p) >= 2]
            if paths:
                xs = [x for path in paths for x, _ in path]
                ys = [y for path in paths for _, y in path]
                kcx, kcy = (max(xs) + min(xs)) / 2, -((max(ys) + min(ys)) / 2)
                kw, kh = max(xs) - min(xs) + 0.2, max(ys) - min(ys) + 0.2
            else:
                kcx, kcy = (hi_x + lo_x) / 2, -((hi_y + lo_y) / 2)
                kw, kh = hi_x - lo_x + 0.7, hi_y - lo_y + 0.7
            self.keepout[name] = (round(kcx, 4), round(kcy, 4),
                                  round(kw, 4), round(kh, 4))
        return self.board

    def size(self, kicad_name: str):
        return self.sizes.get(kicad_name, (2.0, 2.0))

    def keepout_box(self, kicad_name: str):
        # variants are registered before load_board runs, so their keepout is
        # computed from the already-rotated geometry. Returns
        # (center_x, center_y, width, height) in footprint frame.
        return self.keepout.get(kicad_name, (0.0, 0.0, 2.0, 2.0))

    def center(self, kicad_name: str):
        return self.centers.get(kicad_name, (0.0, 0.0))

    def has(self, kicad_name: str) -> bool:
        return kicad_name in self.defs

    def const(self, kicad_name: str) -> str:
        # TSCIRCUIT_ONLY_FOOTPRINTS live in tscircuit/lib/footprints.tsx only, so
        # there is nothing to transcribe - the const name is all the generator
        # needs (the footprint definition itself is a hand-maintained delta).
        if kicad_name not in self.defs and kicad_name not in TSCIRCUIT_ONLY_FOOTPRINTS:
            sys.exit(f"footprint {kicad_name} not found in the KiCad board/library")
        return fp_const_name(kicad_name)

    # -- pads -------------------------------------------------------------
    def _pad(self, pad, indent: int) -> list[str]:
        pad_number, pad_type, pad_shape = pad[1], pad[2], pad[3]
        at = child(pad, "at")
        x, y = float(at[1]), float(at[2])
        rotation = float(at[3]) if len(at) > 3 else 0.0
        width, height = (float(v) for v in child(pad, "size")[1:3])
        rot = round(rotation) % 360
        if rot in (90, 270):  # every pad on this board is axis aligned
            width, height = height, width
        elif rot not in (0, 180):
            sys.exit(f"unsupported pad rotation {rotation} on pad {pad_number}")
        drill = child(pad, "drill")
        layers = child(pad, "layers") or ["layers"]
        layer = "bottom" if any(str(l) == "B.Cu" for l in layers[1:]) else "top"
        rratio = child(pad, "roundrect_rratio")
        radius_ratio = float(rratio[1]) if rratio else 0.0
        pad_ = " " * indent
        hints = 'portHints={["pin%s"]}' % pad_number
        pos = f'pcbX="{mm(x)}" pcbY="{mm(-y)}"'

        if pad_type in ("thru_hole", "np_thru_hole"):
            dims = [float(d) for d in drill[1:] if d != "oval"] if drill else [min(width, height) / 2]
            hole_w = hole_h = dims[0]
            if len(dims) >= 2:
                hole_w, hole_h = dims[0], dims[1]
            if pad_type == "np_thru_hole":
                if abs(hole_w - hole_h) < 1e-6:
                    return [f'{pad_}<hole {pos} diameter="{mm(hole_w)}" />']
                return [
                    f'{pad_}<hole {pos} shape="pill" width="{mm(max(hole_w, hole_h))}" '
                    f'height="{mm(min(hole_w, hole_h))}" />'
                ]
            if pad_shape == "circle":
                return [
                    f'{pad_}<platedhole {pos} {hints} shape="circle" '
                    f'holeDiameter="{mm(hole_w)}" outerDiameter="{mm(max(width, height))}" />'
                ]
            if pad_shape in ("rect", "roundrect", "oval"):
                radius = f' rectBorderRadius="{mm(min(width, height) * radius_ratio)}"' if radius_ratio else ""
                return [
                    f'{pad_}<platedhole {pos} {hints} shape="circular_hole_with_rect_pad" '
                    f'holeDiameter="{mm(hole_w)}" rectPadWidth="{mm(width)}" '
                    f'rectPadHeight="{mm(height)}"{radius} />'
                ]
            sys.exit(f"unsupported through-hole pad shape {pad_shape}")

        if pad_shape == "circle":
            return [f'{pad_}<smtpad {pos} {hints} shape="circle" radius="{mm(width / 2)}" layer="{layer}" />']
        if pad_shape == "oval":
            return [
                f'{pad_}<smtpad {pos} {hints} shape="pill" width="{mm(width)}" height="{mm(height)}" '
                f'radius="{mm(min(width, height) / 2)}" layer="{layer}" />'
            ]
        radius = f' rectBorderRadius="{mm(min(width, height) * radius_ratio)}"' if radius_ratio else ""
        return [
            f'{pad_}<smtpad {pos} {hints} shape="rect" width="{mm(width)}" '
            f'height="{mm(height)}"{radius} layer="{layer}" />'
        ]

    # -- graphics ---------------------------------------------------------
    def _graphics(self, fp, layer_name: str):
        paths = []
        for node in children(fp, "fp_line"):
            layer = child(node, "layer")
            if not layer or layer[1] != layer_name:
                continue
            start, end = child(node, "start"), child(node, "end")
            paths.append([(float(start[1]), float(start[2])), (float(end[1]), float(end[2]))])
        for node in children(fp, "fp_rect"):
            layer = child(node, "layer")
            if not layer or layer[1] != layer_name:
                continue
            start, end = child(node, "start"), child(node, "end")
            x1, y1, x2, y2 = float(start[1]), float(start[2]), float(end[1]), float(end[2])
            paths.append([(x1, y1), (x2, y1), (x2, y2), (x1, y2), (x1, y1)])
        return paths

    def _definition(self, kicad_name: str) -> str:
        fp = self.defs[kicad_name]
        lines = [f"// {kicad_name}   (from {self.origin[kicad_name]})"]
        lines.append(f"export const {fp_const_name(kicad_name)} = (")
        direction = FOOTPRINT_INSERTION.get(kicad_name) or FOOTPRINT_INSERTION.get(self.variant_base(kicad_name))
        lines.append(f'  <footprint insertionDirection="{direction}">' if direction else "  <footprint>")
        for pad in children(fp, "pad"):
            lines.extend(self._pad(pad, 4))
        for path in self._graphics(fp, "F.SilkS"):
            route = ", ".join(f"{{ x: {num(x)}, y: {num(-y)} }}" for x, y in path)
            lines.append(f"    <silkscreenpath route={{[{route}]}} />")
        courtyard = [p for p in self._graphics(fp, "F.CrtYd") if len(p) >= 2]
        if not courtyard:
            # KiCad convention: courtyard = pads/silkscreen + 0.25mm clearance
            lo_x = min((float(child(p, "at")[1]) - float(child(p, "size")[1]) / 2) for p in children(fp, "pad"))
            hi_x = max((float(child(p, "at")[1]) + float(child(p, "size")[1]) / 2) for p in children(fp, "pad"))
            lo_y = min((float(child(p, "at")[2]) - float(child(p, "size")[2]) / 2) for p in children(fp, "pad"))
            hi_y = max((float(child(p, "at")[2]) + float(child(p, "size")[2]) / 2) for p in children(fp, "pad"))
            for path in self._graphics(fp, "F.SilkS"):
                lo_x = min(lo_x, min(px for px, _ in path))
                hi_x = max(hi_x, max(px for px, _ in path))
                lo_y = min(lo_y, min(py for _, py in path))
                hi_y = max(hi_y, max(py for _, py in path))
            pad, clearance = 0.25, 0.25
            courtyard = [[(lo_x - clearance, lo_y - clearance), (hi_x + clearance, hi_y + clearance)]]
        if courtyard:
            xs = [x for path in courtyard for x, _ in path]
            ys = [-y for path in courtyard for _, y in path]
            lines.append(
                f'    <courtyardrect pcbX="{mm((min(xs) + max(xs)) / 2)}" pcbY="{mm((min(ys) + max(ys)) / 2)}" '
                f'width="{mm(max(xs) - min(xs))}" height="{mm(max(ys) - min(ys))}" />'
            )
        lines.append("  </footprint>")
        lines.append(")")
        return "\n".join(lines)

    def tsx(self) -> str:
        header = "\n".join(
            [
                HEADER,
                "// Exact Froemix footprints: pads, drills, pad numbering and silkscreen are",
                "// copied from the KiCad board (KiCad Y-down -> tscircuit Y-up), so the",
                "// tscircuit board and the KiCad board agree pad for pad.",
                "",
            ]
        )
        return header + "\n" + "\n\n".join(self._definition(n) for n in sorted(self.defs)) + "\n"


# --- curated pin labels -----------------------------------------------------
# KiCad symbol pin names are unusable for multi unit op-amps / generic parts,
# so the datasheet pin names live here (pin number -> label).
PIN_LABELS = {
    "Amplifier_Operational:TL074": {
        "1": "OUT1", "2": "IN1N", "3": "IN1P", "4": "VPLUS", "5": "IN2P",
        "6": "IN2N", "7": "OUT2", "8": "OUT3", "9": "IN3N", "10": "IN3P",
        "11": "VMINUS", "12": "IN4P", "13": "IN4N", "14": "OUT4",
    },
    "4xxx:4051": {
        "1": "X4", "2": "X6", "3": "X", "4": "X7", "5": "X5", "6": "INH",
        "7": "VEE", "8": "VSS", "9": "C", "10": "B", "11": "A", "12": "X3",
        "13": "X0", "14": "X1", "15": "X2", "16": "VDD",
    },
    "jclpcb:MCP6002T-I_SN": {
        "1": "OUTA", "2": "INA_N", "3": "INA_P", "4": "VSS",
        "5": "INB_P", "6": "INB_N", "7": "OUTB", "8": "VDD",
    },
    "jclpcb:LM4040C10IDBZR": {"1": "CATHODE", "2": "ANODE", "3": "NC"},
    "jclpcb:TLP2362(TPL,E(T": {
        "1": "ANODE", "3": "CATHODE", "4": "GND", "5": "VO", "6": "VCC",
    },
    "jclpcb:B1205S-1WR3_C5365649": {
        "1": "VIN_N", "2": "VIN_P", "3": "VOUT_N", "4": "VOUT_P",
    },
    "jclpcb:MMBT3904_C181119": {"1": "B", "2": "E", "3": "C"},
    "MCU_Module:Electrosmith_Daisy_Seed_Rev4": {
        "1": "USB_ID", "2": "SD_DAT3", "3": "SD_DAT2", "4": "SD_DAT1",
        "5": "SD_DAT0", "6": "SD_CMD", "7": "SD_CLK", "8": "SPI1_CS",
        "9": "SPI1_SCK", "10": "SPI1_POCI", "11": "SPI1_PICO", "12": "I2C1_SCL",
        "13": "I2C1_SDA", "14": "USART1_TX", "15": "USART1_RX", "16": "AUDIO_IN_1",
        "17": "AUDIO_IN_2", "18": "AUDIO_OUT_1", "19": "AUDIO_OUT_2", "20": "AGND",
        "21": "V3V3_A", "22": "ADC_0", "23": "ADC_1", "24": "ADC_2", "25": "ADC_3",
        "26": "ADC_4", "27": "ADC_5", "28": "ADC_6", "29": "DAC_OUT2",
        "30": "DAC_OUT1", "31": "SAI2_MCLK", "32": "SAI2_SD_B", "33": "SAI2_SD_A",
        "34": "SAI2_FS", "35": "SAI2_SCK", "36": "USB_D_N", "37": "USB_D_P",
        "38": "V3V3_D", "39": "VIN", "40": "DGND",
    },
}

# Name of the exported const in lib/pins.ts for each labelled symbol
PIN_CONST = {
    "Amplifier_Operational:TL074": "TL074_PINS",
    "4xxx:4051": "CD4051_PINS",
    "jclpcb:MCP6002T-I_SN": "MCP6002_PINS",
    "jclpcb:LM4040C10IDBZR": "LM4040_PINS",
    "jclpcb:TLP2362(TPL,E(T": "TLP2362_PINS",
    "jclpcb:B1205S-1WR3_C5365649": "B1205S_PINS",
    "jclpcb:MMBT3904_C181119": "MMBT3904_PINS",
    "MCU_Module:Electrosmith_Daisy_Seed_Rev4": "DAISY_SEED_PINS",
}

# Multi unit symbols are drawn as one <schematicbox> per KiCad unit
MULTI_UNIT = {"Amplifier_Operational:TL074": True}

PIN_ATTRIBUTE = {
    "input": "requiresPower",
    "power_in": "requiresPower",
    "output": "providesPower",
    "power_out": "providesPower",
}

# --------------------------------------------------------------------------- #
# sheets
# --------------------------------------------------------------------------- #

# Sheet descriptions are cosmetic; sheets themselves are discovered from the root
# schematic, so adding a sheet to the KiCad project needs no change here.
SHEET_DESCRIPTIONS = {
    "froemix.kicad_sch": "root sheet: Daisy Seed, ADC multiplexer and board connectors",
    "power.kicad_sch": "Eurorack +-12V entry, +5V rail and +/-10V rails",
    "audio.kicad_sch": "stereo audio in/out buffers",
    "cv.kicad_sch": "sync in/out, CV in attenuverters, CV out drivers",
    "midi.kicad_sch": "MIDI TRS in/out (opto isolated)",
}


def discover_sheets(src_dir: str, root_file: str = "froemix.kicad_sch"):
    """[(sheet name, display name, kicad file, description)] in KiCad sheet order"""
    root = parse_sexpr(open(os.path.join(src_dir, root_file), encoding="utf-8").read())
    sheets = [(root_file, "root", SHEET_DESCRIPTIONS.get(root_file, ""))]
    for sheet in children(root, "sheet"):
        name = file_name = None
        for prop in children(sheet, "property"):
            if len(prop) > 2 and prop[1] == "Sheetname":
                name = prop[2]
            if len(prop) > 2 and prop[1] == "Sheetfile":
                file_name = prop[2]
        if not file_name:
            continue
        sheets.append((file_name, name or file_name.replace(".kicad_sch", ""), SHEET_DESCRIPTIONS.get(file_name, "")))
    out = []
    for index, (file_name, display, description) in enumerate(sheets):
        slug = re.sub(r"[^a-z0-9]+", "_", display.lower()).strip("_") or f"sheet{index}"
        slug = "main" if file_name == root_file else slug
        out.append((slug, display.title() if display.islower() else display, file_name, description))
    return out

# KiCad A4 sheet: 297 x 210 mm, tscircuit schematic sheets are centered on 0,0
SHEET_W, SHEET_H = 297.0, 210.0


def sch_pos(x: float, y: float) -> tuple[float, float]:
    return round(x - SHEET_W / 2, 2), round(SHEET_H / 2 - y, 2)


# --------------------------------------------------------------------------- #
# generation
# --------------------------------------------------------------------------- #


def q(s: str) -> str:
    return '"' + s.replace('"', '\\"') + '"'


def ts_obj(pairs, indent=6):
    pad = " " * indent
    if not pairs:
        return "{}"
    body = "".join(f"{pad}  {q(k)}: {v},\n" for k, v in pairs)
    return "{\n" + body + pad + "}"


def normalize_value(value: str, kind: str) -> str:
    if not value:
        return ""
    v = value.strip()
    if kind == "R":
        if v.endswith("R"):
            return v[:-1]
        if v.endswith("K"):
            return v[:-1] + "k"
        return v
    return v


class Component:
    def __init__(self, comp, sheet, placements, libs, footprints):
        self.ref = comp["ref"]
        self.value = comp["value"] or ""
        self.sheet = sheet
        self.kicad_footprint = comp["footprint"]
        self.lib_id = (placements.get(self.ref) or [{}])[0].get("lib_id") or ""
        self.props = comp["props"]
        self.libpart = comp["libpart"] or ""
        self.pins = sorted({p[1] for p in comp["pins"]}, key=lambda s: int(s) if s.isdigit() else 0)
        self.units = placements.get(self.ref, [])
        self.lib_pins = libs.get(self.lib_id, {})
        self.footprints = footprints
        self.pcb = None
        self.lcsc = (self.props.get("LCSC Part") or self.props.get("JLCPCB ID") or "").strip().upper()
        if not re.fullmatch(r"C[0-9]+", self.lcsc or ""):
            self.lcsc = ""
        self.mpn = self._mpn()
        override = PART_OVERRIDES.get(self.ref)
        if override:
            self.lcsc = override["lcsc"]
            if override["mpn"]:
                self.mpn = override["mpn"]
            if override.get("value"):
                self.value = override["value"]
        self.kind = self._kind()
        self.side = side_of(self.ref, sheet)
        self.nets = {}  # pin number -> tscircuit net name
        self.labels = PIN_LABELS.get(self.lib_id) or self._auto_labels()
        # connectors with plain numeric pin names keep their numeric pins
        self.use_labels = self.lib_id in PIN_LABELS or any(
            not re.fullmatch(rf"P{pin}", lab) for pin, lab in self.labels.items()
        )

    def _mpn(self) -> str:
        for key in ("BOM_Manufacturer Part", "Part Number"):
            value = (self.props.get(key) or "").strip()
            if not value or value.upper() == "PWR_FLAG":
                continue
            if "SchematicLib" in value or "Schematic" in value:
                continue
            return value
        return ""

    # -- helpers ----------------------------------------------------------
    def _kind(self):
        prefix = re.match(r"[A-Za-z]+", self.ref).group(0)
        if prefix == "R":
            return "R"
        if prefix == "C":
            return "C"
        if prefix == "L":
            return "L"
        if prefix == "D":
            return "D"
        if prefix == "Q":
            return "Q"
        if prefix in ("CN", "J", "H"):
            return "J"
        return "U"

    def _auto_labels(self):
        labels = {}
        for pin in self.pins:
            name = self.lib_pins.get(pin, {}).get("name") or ""
            labels[pin] = label(name) if name and not name.isdigit() else f"P{pin}"
        # resolve duplicates
        seen = {}
        for pin, lab in labels.items():
            if lab in seen.values():
                labels[pin] = f"{lab}_{pin}"
            seen[pin] = labels[pin]
        return labels

    @property
    def labelled(self):
        return bool(self.lib_id in PIN_LABELS)

    def pin_label(self, pin: str) -> str:
        return self.labels.get(pin, f"pin{pin}")

    def unit_pins(self, unit: int):
        return [p for p in self.pins if self.lib_pins.get(p, {}).get("unit") == unit]

    def footprint_expr(self) -> str:
        return self.footprints.const(self.footprints.variant_for(self.kicad_footprint))

    def connections(self):
        """[(connection key, net expression)]"""
        if self.kind == "Q":
            terminals = {"1": "base", "2": "emitter", "3": "collector"}
            return [
                (terminals[pin], q(f"net.{self.nets[pin]}"))
                for pin in self.pins
                if pin in self.nets and pin in terminals
            ]
        if self.kind == "D":
            # KiCad diode footprints number pad 1 as the cathode; tell tscircuit
            # about it with pinLabels so the pads map 1:1.
            pairs = []
            anode = self.nets.get("2")
            cathode = self.nets.get("1")
            if anode:
                pairs.append(("anode", q(f"net.{anode}")))
            if cathode:
                pairs.append(("cathode", q(f"net.{cathode}")))
            return pairs
        pairs = []
        for pin in self.pins:
            net = self.nets.get(pin)
            if not net:
                continue
            key = self.pin_label(pin) if (self.kind in ("U", "J") and self.use_labels) else f"pin{pin}"
            pairs.append((key, q(f"net.{net}")))
        return pairs

    def supplier_notes(self):
        bits = []
        if self.lcsc:
            bits.append(f"LCSC {self.lcsc}")
        if self.mpn:
            bits.append(self.mpn)
        if self.ref in DNP_REFS:
            bits.append("fitted by hand (DNP for JLC assembly)")
        return bits

    def supplier_props(self):
        if self.ref in DNP_REFS:
            return [("doNotPlace", "true")]
        props = []
        if self.lcsc:
            props.append(("supplierPartNumbers", "{ jlcpcb: [%s] }" % q(self.lcsc)))
        if self.mpn:
            props.append(("manufacturerPartNumber", q(self.mpn)))
        return props


def place_on_grid(comps, footprints, board_w: float, board_h: float,
                  margin: float = 3.0, spacing: float = 2.5):
    """Shelf-pack the footprints inside the board outline (rows, largest first)."""
    if not board_w or not board_h:
        return
    left, top = -board_w / 2 + margin, board_h / 2 - margin
    ordered = sorted(
        comps,
        key=lambda c: (-footprints.size(c.kicad_footprint)[1], -footprints.size(c.kicad_footprint)[0]),
    )
    x, y = left, top
    row_height = 0.0
    for comp in ordered:
        width, height = footprints.size(comp.kicad_footprint)
        if x + width > board_w / 2 - margin and x > left:
            x = left
            y -= row_height + spacing
            row_height = 0.0
        ox, oy = footprints.center(comp.kicad_footprint)
        comp.pcb = (round(x + width / 2 - ox, 4), round(y - height / 2 - oy, 4), 0, None)
        x += width + spacing
        row_height = max(row_height, height)


# Base-board floorplan. Zones are (x0, x1, y0, y1) in board coordinates
# (board 80.9 x 128.5, center origin). Refs are assigned to zones by sheet
# and function; each zone is shelf-packed tallest-first.
LAYOUT_ZONES = {
    # --- mechanical: the two 16-pin board-to-board headers sit at OPPOSITE ends
    # so the euro/front board is supported at both ends instead of cantilevered
    # off one edge. The four M3 corner holes are the standoff pattern.
    "conn": (-34.0, 34.0, 52.0, 61.0),      # CN1, CN3, CN4, J1 (top edge)
    "conn2": (-4.0, 30.0, -44.0, -35.0),    # CN2 (audio/CV jacks), bottom edge
    # Daisy Seed laid horizontal (rotated 90) under the top connector row,
    # 53 x 19 - short stubs to CN1 and the analog chains below
    "daisy": (-27.0, 27.0, 30.5, 50.0),
    # 10mm band between the Seed's upper pad row (y 48.15) and the connector
    # pads (y 58.8): series resistors and pull-ups of the connector nets belong
    # there, on the line they tap, instead of below the Seed (every stub from
    # below has to cross the Seed's footprint).
    "topband": (-33.0, 30.0, 49.4, 58.4),
    # mux + decoupling under the Seed, near its ADC pins
    "center": (-20.0, 0.0, 17.0, 29.5),
    # strip directly under the Seed's lower (analog) pad row (y 32.91): the
    # resistors that tie the Seed's DAC/ADC pins into the CV and audio chains
    # belong here, on the Seed side of their op-amp, not at the far end of the
    # chain where the packer used to drop them (DAC_OUT1 -> R32 was 62mm).
    # x stops at 24.5: H1's 14mm keepout box starts at x 25.75.
    "daisyface": (0.0, 24.5, 23.8, 29.2),
    # analog chains in wide bands: audio left, CV middle, MIDI + H1 right
    "audio": (-37.0, -16.0, -34.0, 16.0),
    "cv": (2.0, 20.0, -34.0, 16.0),
    "midi": (22.0, 37.0, -34.0, -13.0),  # bottom-right island (top-right tried:
    # 10mm column cannot fit U15's 9.1mm row + passives beside Daisy/H1)
    "h1": (22.0, 37.0, -12.0, 16.0),
    # power: entry (J2, pinned to the bottom edge) + rails bottom right
    "pwr": (-34.0, 34.0, -61.5, -45.0),
}

# Shelf pitch per zone. The analog chains sit in rows of bottom-side SMD, so a
# looser pitch there buys the autorouter bottom-layer room (fewer inner-layer
# dips = fewer vias); the zones without slack keep the 1.2mm default.
ZONE_SPACING = {
    # "audio": 2.2,
    # "cv": 1.5,
}

# Mating direction per footprint. Straight pin/box headers mate from above
# the PCB (J2's body hangs off the back, so its ribbon mates from below).
# The H1 variant is pre-rotated so its opening faces +X in its own frame.
FOOTPRINT_INSERTION = {
    "jclpcb:HDR-TH_16P-P2.00-V-M": "from_above",
    "jclpcb:HDR-TH_8P-P2.00-V-M": "from_above",
    "jclpcb:HDR-TH_7P-P2.00-V-M": "from_above",
    "jclpcb:HDR-TH_6P-P2.54-V-M-R2-C3-S2.54-1": "from_above",
    "4ms_Connector:Pins_2x08_2.54mm_TH_EurorackPower_Shrouded": "from_below",
    "4ms_Connector:Pins_2x08_2.54mm_TH_EurorackPower_Shrouded@R90": "from_below",
    "jclpcb:HDR-TH_12P-P2.54-H-M-R2-C6-S2.54-W12.5@R90": "from_x_pos",
}


def layout_zone(ref: str, sheet: str) -> str:
    # CN1 (MIDI/mux) top, CN2 (audio/CV jack bus) bottom: opposite-end support
    if ref == "CN2":
        return "conn2"
    if ref == "H1":
        return "h1"
    if ref in ("CN1", "CN3", "CN4"):
        return "conn"
    if ref == "DaisySeed1":
        return "daisy"
    if ref in ("R21", "R27", "R31", "R32", "R33", "R42", "C16", "C21"):
        return "daisyface"
    if ref == "U1" or ref in ("C1", "C29", "C30"):
        return "center"
    if ref in ("R58", "R59", "R60", "R51", "R52", "R53", "R54", "R55",
                "R56", "R57"):
        return "topband"
    if sheet == "audio":
        return "audio"
    if sheet == "cv":
        return "cv"
    if sheet == "midi":
        return "midi"
    if sheet == "power":
        return "pwr"
    return "center"


# Exact placements that zone packing cannot express (edge connectors go AT
# their edge, not wherever the packer drops them).
# Zone packing puts H1 at the CV-zone top-left (proven clear of bottom copper
# and on-board). Parking it at the right edge collides with U13's pads and the
# board edge, so it stays packed; the "face nearest edge" placement warning is
# a heuristic false positive (the part correctly faces +X).
LAYOUT_PINNED = {
    # Eurorack power ribbon leaves the left-of-centre bottom edge; x -18.5
    # keeps J2's courtyard clear of the M3 standoff at (-36.95, -60.75)
    # (a 5.5mm-OD standoff reaches x -34.2, use <=5mm OD hardware anyway)
    "J2": (-18.5, -56.0),
    # Daisy rail bulk just below the lower pad row (pins 38/39 at y 32.91):
    # C29+C30 serve P3V3_D (pin 38), C1 serves P5V/VIN (pin 39)
    "C29": (-22.5, 29.6),
    "C30": (-4.0, 29.2),
    "C1": (-28.0, 25.5),
    # H1 PMOD by the right edge (mouth +X faces it, nearest edge: silences the
    # accessible-orientation heuristic too). x 30.5 keeps its 14mm keepout off
    # C14/C15's slots right of U11 and off U13; y 20 keeps M3 holes clear.
    "H1": (28.5, 20.0),
    # Series resistors / pull-ups of the top-edge connector nets sit in the
    # topband between the Seed's upper pad row (pads at y 48.15, courtyard ends
    # 49.0) and the connector pads (58.8). Each one is pinned near the midpoint
    # of the two pads it joins, so its stub is a stop on an existing trace
    # instead of a spur crossing the Seed's footprint (SD_DAT0 alone cost
    # 104mm routed for a 25mm span). x > 10 is left clear for the CN4 ->
    # SPI1/I2C0 fanout and the audio pins, x < -22 for the MIDI_IN pair.
    "R56": (-18.0, 51.8),  # SD_DAT3: Seed -21.36 <-> CN3.pin2 3.14
    "R55": (-14.5, 51.8),  # SD_DAT2: Seed -18.82 <-> CN3.pin3 5.14
    "R54": (-11.0, 51.8),  # SD_DAT1: Seed -16.28 <-> CN3.pin4 7.14
    "R53": (-7.5, 51.8),  # SD_DAT0: Seed -13.74 <-> CN3.pin5 9.14
    "R57": (-4.0, 51.8),  # SD_CMD: Seed -11.20 <-> CN3.pin7 13.14
    "R51": (6.5, 51.8),  # SCL: Seed 4.04 <-> H1 29.77 (pull-up)
    "R52": (10.0, 51.8),  # SDA: Seed 6.58 <-> H1 29.77 (pull-up)
    # R50 is in series on Seed.USART1_TX <-> CN1.pin3 (J_MIDI_OUT_TIP) and
    # R49 pulls CN1.pin4 (J_MIDI_OUT_RING) to P3V3_D; both sat in the midi
    # island 120mm from CN1. Upper row so the R50 leg does not run across the
    # SD pull-up row.
    "R50": (-1.0, 55.4),
    "R49": (-29.0, 51.8),
}

# Decoupling caps pinned next to their IC: cap -> (ic, side). The offset is
# computed from both footprints' sizes plus 1mm clearance, so a pinned cap is
# clear of its own IC by construction; DRC verifies against neighbors.
DECOUPLING = {
    # TL074 pin 4 (V+) sits on the y -2.8 row, pin 11 (V-) on y +2.8, so the
    # VCC cap goes below centre and the VEE cap above (not the reverse).
    "C4": ("U2", "right", -1.5), "C5": ("U2", "right", 1.5),
    "C10": ("U3", "right", -1.5), "C11": ("U3", "right", 1.5),
    "C14": ("U11", "right", -1.5), "C15": ("U11", "right", 1.5),
    "C17": ("U12", "right", -1.5), "C18": ("U12", "right", 1.5),
    "C19": ("U13", "right", 0.0), "C20": ("U14", "right", 0.0),
    "C28": ("U15", "right", 0.0),
    # NOTE: C1/C29/C30 (Daisy rail bulk) and C26 (rail bulk, opposite face
    # from U16) are hand-pinned in LAYOUT_PINNED instead: C1/C29/C30 sit just
    # below Daisy's lower pad row at the P5V/P3V3_D pins (38/39), which the
    # zone/side machinery cannot express.
}


def keepout_at(comp, footprints, x: float, y: float):
    """Courtyard keepout box (board coordinates) of `comp` at origin (x, y)."""
    fp_name = footprints.variant_for(comp.kicad_footprint)
    kcx, kcy, kw, kh = footprints.keepout_box(fp_name)
    if comp.side == "bottom":
        # tscircuit mirrors bottom footprints about their origin
        kcx = -kcx
    return (x + kcx - kw / 2, y + kcy - kh / 2, x + kcx + kw / 2, y + kcy + kh / 2)


def placed_box(comp, footprints):
    """Courtyard keepout box of a placed component in board coordinates."""
    x, y, _rotation, _layer = comp.pcb
    return keepout_at(comp, footprints, x, y)


def _overlaps(a, b, clearance=0.3):
    return (a[0] - clearance < b[2] and b[0] - clearance < a[2]
            and a[1] - clearance < b[3] and b[1] - clearance < a[3])


def pin_candidates(anchored, cap_comp, side: str, jog: float, footprints):
    """Footprint origins to pin `cap_comp` beside `anchored` (DECOUPLING).

    The primary candidate (directly beside the anchor, offset by `jog`) comes
    first; the remaining ones are fallbacks in a fixed order.
    """
    akcx, akcy, akw, akh = footprints.keepout_box(
        footprints.variant_for(anchored.kicad_footprint))
    if anchored.side == "bottom":
        akcx = -akcx
    ckcx, ckcy, ckw, ckh = footprints.keepout_box(
        footprints.variant_for(cap_comp.kicad_footprint))
    if cap_comp.side == "bottom":
        ckcx = -ckcx
    cl = 0.5
    x, y, _rotation, _layer = anchored.pcb
    ax, ay = x + akcx, y + akcy  # anchor keepout center, board frame
    if side in ("right", "left"):
        sgn = 1 if side == "right" else -1
        return [(ax + sgn * (akw / 2 + ckw / 2 + cl) - ckcx, ay + jog - ckcy),
                (ax - ckcx, ay + akh / 2 + ckh / 2 + cl + abs(jog) - ckcy),
                (ax - ckcx, ay - akh / 2 - ckh / 2 - cl - abs(jog) - ckcy),
                (ax - sgn * (akw / 2 + ckw / 2 + cl) - ckcx, ay + jog - ckcy)]
    cands = [(ax + jog - ckcx, ay - akh / 2 - ckh / 2 - cl - ckcy),
             (ax + jog - ckcx, ay + akh / 2 + ckh / 2 + cl - ckcy)]
    if "left" in side:
        cands.insert(0, (ax - akw / 2 - ckw / 2 - cl + jog - ckcx,
                         ay - akh / 2 - ckh / 2 - cl - ckcy))
    if "right" in side:
        cands.insert(0, (ax + akw / 2 + ckw / 2 + cl + jog - ckcx,
                         ay - akh / 2 - ckh / 2 - cl - ckcy))
    return cands


def pin_decoupling(comps, footprints):
    by_ref = {c.ref: c for c in comps}
    for cap, (ic, side, jog) in DECOUPLING.items():
        if cap not in by_ref or ic not in by_ref:
            sys.exit(f"decoupling pair {cap}->{ic} missing")
        anchored = by_ref[ic]
        cap_comp = by_ref[cap]
        if anchored.pcb is None or cap_comp.pcb is None:
            continue
        # candidates are footprint origins; keepout_at() already carries the
        # bottom mirror, so the placed keepout box follows along directly
        x0, x1, y0, y1 = LAYOUT_ZONES[layout_zone(cap, cap_comp.sheet)]
        occupiers = [placed_box(c, footprints) for c in comps if c.ref != cap]
        for ox, oy in pin_candidates(anchored, cap_comp, side, jog, footprints):
            box = keepout_at(cap_comp, footprints, ox, oy)
            if not (x0 <= box[0] and box[2] <= x1 and y0 <= box[1] and box[3] <= y1):
                continue
            if any(_overlaps(box, o) for o in occupiers):
                continue
            plat = cap_comp.pcb
            cap_comp.pcb = (round(ox, 4), round(oy, 4), 0, plat[3])
            break
        # else: keep the shelf-packed position (safe default, still same zone)


def reserve_decoupling_slots(anchors, caps_of_ic, by_ref, zone: str,
                             occupied: list, reserved: list, footprints):
    """Keep the primary decoupling slot of the given anchor(s) free for caps.

    Called right after each anchor is placed (never after all of them: a
    later anchor would otherwise squat on an earlier anchor's cap slot).
    `occupied`/`reserved` grow in place; returns {cap_ref: (x, y)}.
    """
    x0, x1, y0, y1 = LAYOUT_ZONES[zone]
    slots = {}
    for anchor in anchors:
        for cap_ref in caps_of_ic.get(anchor.ref, ()):
            cap_comp = by_ref[cap_ref]
            if cap_comp.pcb is not None:  # already placed by its own zone
                continue
            if layout_zone(cap_ref, cap_comp.sheet) != zone:
                continue
            side, jog = DECOUPLING[cap_ref][1:]
            ox, oy = pin_candidates(anchor, cap_comp, side, jog, footprints)[0]
            slot = keepout_at(cap_comp, footprints, ox, oy)
            if not (x0 <= slot[0] and slot[2] <= x1 and y0 <= slot[1] and slot[3] <= y1):
                continue  # primary candidate is outside the zone: keep fallback
            if any(_overlaps(slot, o) for o in occupied):
                continue
            reserved.append(slot)
            occupied.append(slot)
            slots[cap_ref] = (ox, oy)
    return slots


def place_by_zones(comps, footprints, board_w: float, board_h: float,
                   spacing: float = 1.2):
    """Pack each floorplan zone separately (shelf pack, tallest first).

    Two kinds of parts are placed ahead of the shelf packing and only reserve
    space: pinned parts (LAYOUT_PINNED) and the primary decoupling slot of every
    DECOUPLING pair (reserved as soon as the zone's ICs are down, so the packer
    does not take the spot the bypass cap needs).

    `ZONE_SPACING` loosens the shelf pitch in the zones that have height to
    spare: every net between two bottom-side SMD pads has to escape on the
    bottom layer, so dense rows force the autorouter to dip to an inner layer
    (two vias) for otherwise short hops.
    """
    by_ref = {c.ref: c for c in comps}
    spacing_default = spacing
    caps_of_ic = {}
    for cap, (ic, _side, _jog) in DECOUPLING.items():
        caps_of_ic.setdefault(ic, []).append(cap)
    for comp in comps:
        if comp.ref in LAYOUT_PINNED:
            px, py = LAYOUT_PINNED[comp.ref]
            _, _, rotation, layer = comp.pcb or (0, 0, 0, comp.side)
            comp.pcb = (px, py, rotation, layer)
    reserved = [placed_box(c, footprints) for c in comps if c.ref in LAYOUT_PINNED]

    by_zone: dict[str, list] = {}
    for comp in comps:
        if comp.ref in LAYOUT_PINNED:
            continue  # placed above, only reserves space now
        by_zone.setdefault(layout_zone(comp.ref, comp.sheet), []).append(comp)
    for zone, members in by_zone.items():
        x0, x1, y0, y1 = LAYOUT_ZONES[zone]
        spacing = ZONE_SPACING.get(zone, spacing_default)
        items = []
        for comp in members:
            fp_name = footprints.variant_for(comp.kicad_footprint)
            # pack by courtyard keepout (not footprint size): narrower cells
            # let wider courtyards touch (C14/C15 did)
            kcx, kcy, kw, kh = footprints.keepout_box(fp_name)
            rotation = 0
            if comp.side == "bottom":
                # tscircuit mirrors bottom footprints about their origin
                # (verified: asymmetric pads land at origin - x)
                kcx = -kcx
            items.append((comp, kw, kh, kcx, kcy, rotation))
        # ICs/connectors first (tallest-first anchors the rows), then passives
        # in net-affinity order so feedback loops and dividers land together
        # instead of in reference-designator order.
        big = sorted([t for t in items if t[0].kind in ("U", "J", "Q")],
                     key=lambda t: (-t[2], -t[1], t[0].ref))
        small = [t for t in items if t[0].kind not in ("U", "J", "Q")]
        placed_nets = {n for t in big for n in t[0].nets.values()}
        ordered_small = []
        while small:
            small.sort(key=lambda t: (-len(set(t[0].nets.values()) & placed_nets), t[0].ref))
            nxt = small.pop(0)
            ordered_small.append(nxt)
            placed_nets.update(nxt[0].nets.values())
        items = big + ordered_small
        anchor_count = len(big)
        occupied = list(reserved)
        slots: dict[str, tuple[float, float]] = {}
        x, y = x0, y1
        row_height = 0.0
        for index, (comp, width, height, ox, oy, rotation) in enumerate(items):
            if comp.ref in slots:
                # the packer kept this spot free for the cap: use it as is
                px, py = slots[comp.ref]
                comp.pcb = (round(px, 4), round(py, 4), rotation, None)
                continue
            while True:
                if x + width > x1 and x > x0:
                    x, y, row_height = x0, y - row_height - spacing, 0.0
                if y - height < y0:
                    sys.exit(f"zone {zone} overflow: {comp.ref} does not fit")
                cell = (x, y - height, x + width, y)
                blockers = [b for b in reserved if _overlaps(cell, b, 0.0)]
                if not blockers:
                    break
                # slide right of the reserved boxes; a gap too narrow for the
                # part means the row is done, so start the next one below
                right = max(b[2] for b in blockers) + spacing
                if right + width <= x1:
                    x = right
                else:
                    x, y, row_height = x0, y - row_height - spacing, 0.0
            comp.pcb = (round(x + width / 2 - ox, 4),
                        round(y - height / 2 - oy, 4), rotation, None)
            occupied.append(placed_box(comp, footprints))
            x += width + spacing
            row_height = max(row_height, height)
            if index < anchor_count:
                # reserve this anchor's cap slots the moment it is placed, so
                # a later anchor can never squat on them (reserving after all
                # anchors left U11 sitting on U13's C19 slot)
                for cap_ref, pos in reserve_decoupling_slots(
                        [comp], caps_of_ic, by_ref, zone,
                        occupied, reserved, footprints).items():
                    slots[cap_ref] = pos


_LCSC_MODEL_MAP: dict | None = None


def cadmodel_value(lcsc: str, indent: int) -> str | None:
    """cadModel={{...}} value from the harvested EasyEDA model blocks."""
    global _LCSC_MODEL_MAP
    if _LCSC_MODEL_MAP is None:
        _LCSC_MODEL_MAP = lcsc_models()
    block = _LCSC_MODEL_MAP.get(lcsc or "")
    if not block:
        return None
    pad = " " * indent + "  "
    body = ("\n" + pad).join(line.strip() for line in block.splitlines())
    return "{\n" + pad + body + "\n" + " " * indent + "}"


def emit_component(comp: Component, indent=4) -> list[str]:
    pad = " " * indent
    lines = []
    notes = [comp.value]
    if comp.libpart and comp.libpart != comp.value:
        notes.append(comp.libpart)
    notes.extend(comp.supplier_notes())
    if getattr(comp, "extra_why", None):
        notes.append(comp.extra_why + " (added in tscircuit, mirror into KiCad)")
    notes.append(comp.kicad_footprint)
    lines.append(f"{pad}{{/* {comp.ref} {' - '.join(n for n in notes if n)} */}}")

    # Schematic placement is left to tscircuit's layout engine: KiCad page
    # coordinates are not on the same scale as tscircuit schematic symbols.
    pos = ""

    props = [("name", q(comp.ref)), ("footprint", comp.footprint_expr())]

    if comp.kind == "Q":
        props = [
            ("name", q(comp.ref)),
            ("type", q("npn")),
            ("footprint", comp.footprint_expr()),
            ("pinLabels", '{ pin1: "base", pin2: "emitter", pin3: "collector" }'),
        ]
    elif comp.kind == "R":
        props.insert(1, ("resistance", q(normalize_value(comp.value, "R"))))
    elif comp.kind == "C":
        props.insert(1, ("capacitance", q(normalize_value(comp.value, "C"))))
        if comp.kicad_footprint.startswith("jclpcb:CAP-TH"):
            props.append(("polarized", "true"))
    elif comp.kind == "L":
        props.insert(1, ("inductance", q(normalize_value(comp.value, "L"))))
    elif comp.kind == "D":
        props.append(("pinLabels", '{ pin1: "cathode", pin2: "anode" }'))
        if "B5819" in comp.kicad_footprint or "B5819" in comp.libpart:
            props.append(("variant", q("schottky")))
    elif comp.kind == "J":
        props.append(("pinCount", str(len(comp.pins))))
        if comp.use_labels:
            props.append(("pinLabels", ts_obj([(f"pin{p}", q(comp.pin_label(p))) for p in comp.pins])))
        props.append(("schPinArrangement", arrangement_expr(comp)))
    elif comp.kind == "U":
        if comp.lib_id in PIN_CONST:
            props.append(("pinLabels", PIN_CONST[comp.lib_id]))
        elif comp.use_labels:
            props.append(("pinLabels", ts_obj([(f"pin{p}", q(comp.pin_label(p))) for p in comp.pins])))
        attrs = []
        for pin in comp.pins:
            ptype = comp.lib_pins.get(pin, {}).get("type", "")
            net = comp.nets.get(pin, "")
            flags = []
            if ptype in PIN_ATTRIBUTE:
                flags.append("%s: true" % PIN_ATTRIBUTE[ptype])
            if net == "GND" or net.endswith("_GND"):
                flags.append("requiresGround: true")
            if flags:
                attrs.append((comp.pin_label(pin), "{ %s }" % ", ".join(flags)))
        if attrs:
            props.append(("pinAttributes", ts_obj(attrs)))
        props.append(("schPinArrangement", arrangement_expr(comp)))
    if comp.pcb:
        x, y, rotation, _layer = comp.pcb
        props.append(("pcbX", f'"{num(x)}mm"'))
        props.append(("pcbY", f'"{num(y)}mm"'))
        if rotation:
            props.append(("pcbRotation", f'"{num(rotation)}deg"'))
        if comp.side:
            props.append(("layer", q(comp.side)))
    props.extend(comp.supplier_props())
    if comp.ref not in DNP_REFS:
        cadmodel = cadmodel_value(comp.lcsc, indent)
        if cadmodel:
            props.append(("cadModel", cadmodel))
    conns = comp.connections()
    if conns:
        props.append(("connections", ts_obj(conns)))

    def jsx_attr(key, value):
        if value.startswith('"') and value.endswith('"') and '"' not in value[1:-1]:
            return f"{key}={value}"
        return f"{key}=" + "{" + value + "}"

    props_src = "".join(f"\n{pad}  {jsx_attr(key, value)}" for key, value in props)
    tag = {
        "R": "resistor", "C": "capacitor", "L": "inductor", "D": "diode",
        "Q": "transistor", "J": "connector", "U": "chip",
    }[comp.kind]
    lines.append(f"{pad}<{tag}{props_src}{pos} />")

    return lines


def arrangement_expr(comp: Component) -> str:
    def pin_ref(pin):
        if comp.use_labels:
            return q(comp.pin_label(pin))
        return pin

    left, right = [], []
    for pin in comp.pins:
        ptype = comp.lib_pins.get(pin, {}).get("type", "")
        if ptype in ("output", "power_out"):
            right.append(pin_ref(pin))
        elif ptype in ("input", "power_in"):
            left.append(pin_ref(pin))
        else:
            (left if len(left) <= len(right) else right).append(pin_ref(pin))
    sides = []
    if left:
        sides.append("leftSide: [%s]" % ", ".join(left))
    if right:
        sides.append("rightSide: [%s]" % ", ".join(right))
    return "{ " + ", ".join(sides) + " }"


def emit_unit_box(comp: Component, unit, indent: int) -> list[str]:
    pad = " " * indent
    pins = comp.unit_pins(unit["unit"])
    if not pins:
        return []
    left = [p for p in pins if comp.lib_pins.get(p, {}).get("type") in ("input", "power_in", "passive")]
    right = [p for p in pins if p not in left]
    if not right and len(left) > 1:
        left, right = left[:-1], [left[-1]]
    x, y = sch_pos(unit["x"], unit["y"])
    height = round(0.5 + 0.6 * max(len(left), len(right), 1), 2)
    labels = {f"pin{i + 1}": q(comp.pin_label(p)) for i, p in enumerate(left + right)}
    arrange = []
    if left:
        arrange.append("leftSide: [%s]" % ", ".join(q(f"pin{i + 1}") for i in range(len(left))))
    if right:
        arrange.append(
            "rightSide: [%s]"
            % ", ".join(q(f"pin{len(left) + i + 1}") for i in range(len(right)))
        )
    name = f"{comp.ref}{chr(ord('A') + unit['unit'] - 1)}"
    src = (
        f'{pad}<schematicbox name={q(name)} chipRef={{".{comp.ref}"}} '
        f"width={{2.6}} height={{{height}}}\n"
        f"{pad}  pinLabels=" + "{" + ts_obj(list(labels.items()), indent + 2) + "}\n"
        f"{pad}  schPinArrangement={{{{ {', '.join(arrange)} }}}}\n"
        f"{pad}  schX={{{x}}} schY={{{y}}} />"
    )
    return [src]


HEADER = "// Generated by froemix/tools/kicad2tscircuit.py - do not edit by hand.\n"


def write(path: str, content: str):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as fh:
        fh.write(content)


def build_components(src_dir: str, pcb_file: str, kicad_cli: str):
    """Netlist + placements + footprints -> Component objects and the net map.

    Shared by the generator and the verification scripts so that all of them look
    at exactly the same model.
    """
    components, nets = export_netlist(kicad_cli, os.path.join(src_dir, "froemix.kicad_sch"))
    placements, libs = parse_schematics(src_dir)
    footprints = FootprintLibrary(src_dir, pcb_file)
    board_w, board_h = footprints.load_board(pcb_file)

    # net bookkeeping ------------------------------------------------------
    net_map = {}
    for net in nets:
        mapped = net_name(net["name"])
        if mapped is None or len(net["nodes"]) < 2:
            continue
        if mapped in net_map.values():
            sys.exit(f"net name collision: {net['name']} -> {mapped}")
        net_map[net["name"]] = mapped
    used = set()
    for net in nets:
        mapped = net_map.get(net["name"])
        if not mapped:
            continue
        for node in net["nodes"]:
            used.add(node["ref"])

    # components -----------------------------------------------------------
    sheets = discover_sheets(src_dir)
    sheet_of_file = {sheet[2]: sheet[0] for sheet in sheets}
    comps = []
    for raw in components:
        if raw["ref"] in ("1", "2"):  # PWR_FLAG symbols
            continue
        if raw["ref"] not in used:
            continue
        sheet_file = raw["props"].get("Sheetfile", "froemix.kicad_sch")
        sheet = sheet_of_file.get(sheet_file)
        if sheet is None:
            sys.exit(f"unknown sheet for {raw['ref']}: {sheet_file}")
        comp = Component(raw, sheet, placements, libs, footprints)
        for net in nets:
            mapped = net_map.get(net["name"])
            if not mapped:
                continue
            for node in net["nodes"]:
                if node["ref"] == comp.ref:
                    comp.nets[node["pin"]] = mapped
        comps.append(comp)

    # parts: inherit the JLCPCB part of the first component with the same symbol
    lcsc_by_libpart = {}
    for comp in comps:
        if comp.lcsc:
            lcsc_by_libpart.setdefault(comp.libpart or comp.ref, comp.lcsc)
    for comp in comps:
        if not comp.lcsc:
            comp.lcsc = lcsc_by_libpart.get(comp.libpart or comp.ref, "")

    # extra components missing from KiCad (pull-ups, decoupling, bypass). They
    # join existing nets, apart from the MIDI OUT driver, whose two internal nets
    # are TSCIRCUIT_ONLY_NETS and exist in tscircuit/ only. The netlist check
    # compares the KiCad side with REMOVED_REFS dropped and the extra pins
    # stripped, so the KiCad netlist stays the reference for every other pin.
    for ref, kind, value, fp_name, sheet, pin_nets, lcsc, mpn, why in EXTRA_COMPONENTS:
        if not footprints.has(fp_name) and fp_name not in TSCIRCUIT_ONLY_FOOTPRINTS:
            sys.exit(f"extra part {ref}: footprint {fp_name} not found")
        for pin, net in pin_nets.items():
            if net not in net_map.values() and net not in TSCIRCUIT_ONLY_NETS:
                sys.exit(f"extra part {ref}: net {net} does not exist")
        raw = {
            "ref": ref,
            "value": value,
            "footprint": fp_name,
            "libpart": f"EXTRA_{ref}",
            "props": {"LCSC Part": lcsc, "BOM_Manufacturer Part": mpn} if lcsc else {},
            "pins": [("A", pin) for pin in pin_nets],
        }
        comp = Component(raw, sheet, placements, libs, footprints)
        comp.nets = dict(pin_nets)
        comp.extra_why = why
        comps.append(comp)

    # PCB placement -------------------------------------------------------
    # The KiCad board is mid-edit: every footprint sits outside its Edge.Cuts
    # outline (only the partial routing is on the board), so its placement is not
    # reused. Components are packed per floorplan zone (LAYOUT_ZONES) instead -
    # a starting point for real placement, which keeps the DRC free of overlap
    # errors. place_on_grid is kept for quick experiments.
    place_by_zones(comps, footprints, board_w, board_h)
    pin_decoupling(comps, footprints)
    return comps, net_map, footprints, sheets


def generate(src_dir: str, out_dir: str, kicad_cli: str, pcb_file: str):
    comps, net_map, footprints, sheets = build_components(src_dir, pcb_file, kicad_cli)

    # lib/pins.ts ---------------------------------------------------------
    pin_lines = [HEADER, "// Pin labels (KiCad pin number -> label) for the parts\n// that are modelled as <chip> elements.\n"]
    for lib_id, labels in PIN_LABELS.items():
        const = PIN_CONST[lib_id]
        pin_lines.append(f"export const {const} = {{")
        for pin in sorted(labels, key=int):
            pin_lines.append(f"  pin{pin}: {q(labels[pin])},")
        pin_lines.append("} as const\n")
    write(os.path.join(out_dir, "lib", "pins.ts"), "\n".join(pin_lines))

    # lib/footprints.tsx --------------------------------------------------
    write(os.path.join(out_dir, "lib", "footprints.tsx"), footprints.tsx())

    # nets.tsx ------------------------------------------------------------
    net_lines = [
        HEADER,
        "// One <net> per KiCad net. KiCad names are not valid tscircuit net\n"
        "// names (only [A-Za-z0-9_]), the original name is kept in a comment.\n",
        "export const Nets = () => (",
        "  <>",
    ]
    for kicad_name, mapped in sorted(net_map.items(), key=lambda kv: (not POWER_NET_RE.match(kv[1]), kv[1])):
        flags = ""
        if kicad_name == "GND":
            flags = " isGroundNet"
        elif POWER_NET_RE.match(mapped):
            flags = " isPowerNet"
        width = POWER_TRACE_WIDTH.get(mapped)
        if width:
            flags += f' nominalTraceWidth="{width}mm" routingPhaseIndex={{0}}'
        else:
            # single global phase: frozen phase-0 power walls blocked signal
            # routing, so everything solves together instead
            flags += ' routingPhaseIndex={0}'
        net_lines.append(f'    {{/* {kicad_name} */}}<net name="{mapped}"{flags} />')
    net_lines.append("  </>")
    net_lines.append(")")
    write(os.path.join(out_dir, "sections", "nets.tsx"), "\n".join(net_lines) + "\n")

    # sections ------------------------------------------------------------
    section_exports = []
    for index, (name, display, kicad_file, description) in enumerate(sheets):
        members = sorted(
            (c for c in comps if c.sheet == name),
            key=lambda c: (re.match(r"[A-Za-z]+", c.ref).group(0), int(re.sub(r"\D", "", c.ref) or 0)),
        )
        pin_imports = sorted({PIN_CONST[c.lib_id] for c in members if c.lib_id in PIN_CONST})
        fp_imports = sorted({footprints.const(footprints.variant_for(c.kicad_footprint)) for c in members})
        lines = [HEADER, f"// {display} sheet: {description}", f"// Source: tmp/{kicad_file}\n"]
        if pin_imports:
            lines.append('import { %s } from "../lib/pins"' % ", ".join(pin_imports))
        if fp_imports:
            lines.append('import { %s } from "../lib/footprints"' % ", ".join(fp_imports))
        if pin_imports or fp_imports:
            lines.append("")
        func = name.capitalize() + "Section"
        lines.append(f"export const {func} = () => (")
        lines.append(f'  <schematicsheet name="{name}" displayName="{display}" sheetIndex={{{index}}}>')
        for comp in members:
            lines.extend(emit_component(comp, indent=4))
        lines.append("  </schematicsheet>")
        lines.append(")")
        write(os.path.join(out_dir, "sections", f"{name}.tsx"), "\n".join(lines) + "\n")
        section_exports.append((func, name))

    # index.circuit.tsx ---------------------------------------------------
    index = [HEADER]
    for func, _ in section_exports:
        index.append(f'import {{ {func} }} from "./sections/{"main" if func == "MainSection" else func[:-7].lower()}"')
    index.append('import { Nets } from "./sections/nets"\n')
    index.append("// Froemix base board - Daisy Seed based Eurorack mixer / DSP module")
    index.append("// Board outline carried over from froemix.kicad_pcb: 80.9 x 128.5mm (16HP x 3U)")
    index.append("// 4 layers like the KiCad board (signal / GND / PWR / signal).")
    index.append("export default () => (")
    # JLCPCB 4-layer 1oz capabilities (with margin over their minimums):
    # min trace/space 0.09mm, min via drill 0.15mm, min annular ring 0.10mm.
    index.append('  <board width="80.9mm" height="128.5mm" layers={4} autorouter="auto-local"')
    index.append('    defaultTraceWidth="0.2mm" minTraceWidth="0.1mm" minTraceToPadEdgeClearance="0.1mm"')
    index.append('    minViaHoleDiameter="0.2mm" minViaPadDiameter="0.45mm" minBoardEdgeClearance="0.4mm"')
    index.append('    routeRemaining={false}>')
    index.append("    <Nets />")
    index.append("    {/* routing phases run in order; only phased nets route while routeRemaining is false */}")
    index.append("    <autoroutingphase phaseIndex={0} />")
    index.append('    {/* M3 mounting holes, 3.5mm from each board edge */}')
    for hole_x, hole_y in [(-36.95, 60.75), (36.95, 60.75), (-36.95, -60.75), (36.95, -60.75)]:
        index.append(f'    <hole diameter="3.2mm" pcbX="{num(hole_x)}mm" pcbY="{num(hole_y)}mm" />')
    # NOTE: no GND copper pours for now. Any <copperpour> deterministically
    # crashes the build once the Daisy Seed sits on the bottom (Flatten
    # boolean-op bug in tscircuit core: `node.item.key.low` undefined).
    # Minimal repro: Daisy-bottom + one pour, full board. Re-add after upstream
    # fix; until then GND is routed explicitly (4-layer board).
    for func, _ in section_exports:
        index.append(f"    <{func} />")
    index.append("  </board>")
    index.append(")")
    write(os.path.join(out_dir, "index.circuit.tsx"), "\n".join(index) + "\n")

    print(f"components: {len(comps)}   nets: {len(net_map)}   sheets: {len(sheets)}")


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))  # froemix/
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", default=os.path.join(root, "tmp"))
    ap.add_argument("--out", default=os.path.join(root, "tscircuit"))
    ap.add_argument("--pcb", default=os.path.join(root, "tmp", "froemix.kicad_pcb"))
    ap.add_argument(
        "--kicad-cli",
        default=shutil.which("kicad-cli")
        or "/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli",
    )
    args = ap.parse_args()
    generate(args.src, args.out, args.kicad_cli, args.pcb)


if __name__ == "__main__":
    main()

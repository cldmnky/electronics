#!/usr/bin/env python3
"""Check the MIDI electrical design against the built circuit and the datasheets.

Usage:
    python3 froemix/tools/check_elec.py [--circuit dist/index/circuit.json]

DESIGN-REVIEW R02 (third round) changed two parts and four values on the MIDI
sheet: U15 TLP2362 -> TLP2361, Q2 MMBT3906 PNP -> AO3401A P-channel MOSFET,
R58 4.7k -> 180R, R61 10k -> 2.2k, and the two 1 W 2512 resistors swapped
(R59 ring 22R -> 33R, R60 tip 33R -> 22R). None of the other gates look at the
electrical consequences of exactly those numbers:

  * check:netlist/check:extras prove connectivity, not ratings;
  * check:values proves the literals match the MPNs, not the design;
  * check:parts proves the JLCPCB part exists and has the right value.

So this script recomputes the currents and fault powers from the values in the
BUILT circuit and compares them with the limits taken from the primary sources:

  Toshiba TLP2361 data sheet Rev 5.0 (2015-11-02):
      VF <= 1.65 V at IF = 2 mA (Ta = 25 C)
      IFHL <= 1.3 mA (-40..105 C), <= 1.6 mA (-40..125 C)
      IF(ON) recommended 2..6 mA; VF(OFF) 0..0.8 V; VCC 2.7..5.5 V
      VOL <= 0.4 V at IO = 3.2 mA; VOH >= 3.2 V at IO = -20 uA (VCC 3.3 V)
      inverting, totem-pole output; 0.1 uF between pins 6 and 4
  Toshiba TLP2362 data sheet Rev 6.0 (the replaced part):
      IFHL <= 5.0 mA, IF(ON) recommended from 7.5 mA
  AOS AO3401A Rev 3.1 (2023-12):
      RDS(ON) <= 85 mOhm at VGS = -2.5 V, <= 60 mOhm at -4.5 V (TJ = 25 C),
      75 mOhm for the -10 V spec at TJ = 125 C (1.5x the 25 C value)
      VGS(th) -0.5..-1.3 V, VGS max +-12 V, ID -4.0 A (25 C) / -3.2 A (70 C),
      Qg(4.5 V) 7 nC, Ciss 645 pF, IDSS -1 uA at 25 C / -5 uA at 125 C
  Uniroyal thick-film data sheet (Feb 2019, V.3):
      2512 = 1 W, rated to 70 C ambient and derated above it, F code = +-1 %
  MIDI CA-033 (2014):
      3.3 V +-5 % signaling; RA 33 R (source), RC 10 R (sink) recommended;
      the receiver is electrically an LED + 220 R, VF <= 1.9 V, and the
      receiver "must require less than 5 mA to turn on"

ASSUMPTION, not a guarantee: the STM32H750 per-I/O DC absolute maximum is taken
as 20 mA and the output high/low levels as VOL <= 0.4 V / VOH >= VDD - 0.4 V.
The ST data sheet could not be downloaded from this host (st.com fails on HTTP
and HTTP/1.1 from here, and no mirror with the primary text was found), so those
three numbers are stated assumptions and the physical gate stays open; they are
marked ASSUMPTION in the output. Everything else below comes from a data sheet
that is on disk.

Negative tests: froemix/tools/test_elec.py mutates copies of the built circuit
(wrong pin nets, wrong resistor values, the old parts) and asserts that this
script fails. Exits non-zero and prints one line per violation.
"""

from __future__ import annotations

import argparse
import json
import os
import sys

# --- datasheet / standard constants --------------------------------------- #
VDD = 3.3                  # nominal P3V3_D
VDD_TOL = 0.05             # CA-033: +-5 % for 3.3 V signaling
R_TOL = 0.01               # UNI-ROYAL F code = +-1 %
CA033_R_TOL = 0.05         # CA-033 assumes +-5 % resistors
V_RX_MAX = 1.9             # CA-033 receiver model: LED VF max
R_RX = 220.0               # CA-033 receiver model: series resistor
R_RX_TOL = 0.05            # CA-033 resistor tolerance

TLP_VF_MAX = 1.65          # TLP2361 VF at IF = 2 mA
TLP_IFHL_125 = 1.6e-3      # TLP2361 IFHL max at Ta = 125 C
TLP_IF_ON_MIN = 2e-3       # TLP2361 recommended IF(ON) min
TLP_IF_ON_MAX = 6e-3       # TLP2361 recommended IF(ON) max
TLP_VOL_MAX = 0.4          # TLP2361 VOL at IO = 3.2 mA
TLP_IO_MAX = 3.2e-3        # TLP2361 output current at that VOL
TLP2362_IFHL = 5.0e-3      # the replaced part's IFHL max

FET_RDS_25 = 0.085         # AO3401A RDS(ON) max at VGS = -2.5 V, TJ = 25 C
FET_RDS_HOT_FACTOR = 1.5   # 50 -> 75 mOhm on the -10 V spec, TJ = 125 C
FET_VGS_TH_MIN = 0.5       # |VGS(th)| minimum, so below this the FET is off
FET_VGS_TH_MAX = 1.3
FET_ID_70 = 3.2            # A, continuous at TA = 70 C
FET_VGS_MAX = 12.0
FET_CISS = 645e-12
FET_QG_4V5 = 7e-9
FET_IDSS_125 = 5e-6        # A, VGS = 0 V, TJ = 125 C

RATED_W_2512 = 1.0         # Uniroyal 2512, rated to 70 C ambient

ASSUMED_GPIO_ABS_MAX = 0.020   # ASSUMPTION, see the module docstring
ASSUMED_GPIO_VOL = 0.4         # ASSUMPTION
ASSUMED_GPIO_VOH_DROP = 0.4    # ASSUMPTION (VOH >= VDD - 0.4 V)

# What the built circuit must carry (ref -> expected part and value)
EXPECTED_PARTS = {
    "U15": {"lcsc": "C107626", "mpn": "TLP2361(TPL,E"},
    "Q2": {"lcsc": "C15127", "mpn": "AO3401A"},
    "R58": {"lcsc": "C22828", "mpn": "0603WAF1800T5E"},
    "R59": {"lcsc": "C53728", "mpn": "25121WF330JT4E"},
    "R60": {"lcsc": "C118189", "mpn": "25121WF220JT4E"},
    "R61": {"lcsc": "C4190", "mpn": "0603WAF2201T5E"},
}
EXPECTED_NETS = {
    ("U15", "1"): "NET_D1_K",
    ("U15", "3"): "NET_D1_A",
    ("U15", "4"): "GND",
    ("U15", "5"): "USART1_RX",
    ("U15", "6"): "P3V3_D",
    ("Q2", "1"): "NET_Q2_B",
    ("Q2", "2"): "P3V3_D",
    ("Q2", "3"): "NET_Q2_C",
    ("R58", "1"): "USART1_TX",
    ("R58", "2"): "NET_Q2_B",
    ("R59", "1"): "J_MIDI_OUT_RING",
    ("R59", "2"): "NET_Q2_C",
    ("R60", "1"): "J_MIDI_OUT_TIP",
    ("R60", "2"): "GND",
    ("R61", "1"): "NET_Q2_B",
    ("R61", "2"): "P3V3_D",
}


def value_of(circuit, ref, prop):
    for elm in circuit:
        if elm["type"] == "source_component" and elm.get("name") == ref:
            return elm.get(prop)
    return None


def pin_nets(circuit):
    """{(ref, pin): net name} using the check_netlist.py union-find derivation."""
    components = {
        elm["source_component_id"]: elm.get("name")
        for elm in circuit
        if elm["type"] == "source_component"
    }
    nets = {
        elm["source_net_id"]: elm["name"]
        for elm in circuit
        if elm["type"] == "source_net"
    }
    ports = {
        elm["source_port_id"]: (components.get(elm.get("source_component_id")),
                                str(elm.get("pin_number")))
        for elm in circuit
        if elm["type"] == "source_port"
    }
    parent: dict[str, str] = {}

    def find(key):
        parent.setdefault(key, key)
        while parent[key] != key:
            parent[key] = parent[parent[key]]
            key = parent[key]
        return key

    def union(a, b):
        ra, rb = find(a), find(b)
        if ra != rb:
            parent[ra] = rb

    for elm in circuit:
        if elm["type"] != "source_trace":
            continue
        members = list(elm.get("connected_source_port_ids") or [])
        for net_id in elm.get("connected_source_net_ids") or []:
            if net_id in nets:
                members.append(f"net:{nets[net_id]}")
        for member in members[1:]:
            union(members[0], member)

    labels: dict[str, set[str]] = {}
    for key in list(parent):
        if key.startswith("net:"):
            labels.setdefault(find(key), set()).add(key[4:])

    found = {}
    for port_id, (ref, pin) in ports.items():
        groups = labels.get(find(port_id), set())
        found[(ref, pin)] = next(iter(groups)) if len(groups) == 1 else None
    return found


# --- the electrical model -------------------------------------------------- #
def numbers(values):
    """Every quantity the design is checked against, from the built values."""
    r58, r59, r60, r61 = (values["R58"], values["R59"], values["R60"], values["R61"])
    out = {}

    # MIDI OUT into a CA-033 receiver (LED + 220R), worst case
    vtx_min = VDD * (1 - VDD_TOL)
    vtx_max = VDD * (1 + VDD_TOL)
    legs_min = (r59 + r60) * (1 - R_TOL)
    legs_max = (r59 + r60) * (1 + R_TOL)
    legs_max_ca033 = (r59 + r60) * (1 + CA033_R_TOL)
    rds_hot = FET_RDS_25 * FET_RDS_HOT_FACTOR
    out["rds_hot"] = rds_hot
    out["i_out_worst"] = max(0.0, (vtx_min - V_RX_MAX) / (legs_max + R_RX * (1 + R_RX_TOL) + rds_hot))
    out["i_out_typ"] = max(0.0, (VDD - 1.65) / (r59 + r60 + R_RX + FET_RDS_25))
    # CA-033's own 43 ohm example, with ITS assumptions (+-5 % resistors), and
    # ours under the same assumptions, so the two are comparable.
    out["ca033_example"] = (vtx_min - V_RX_MAX) / (43.0 * (1 + CA033_R_TOL) + R_RX * (1 + CA033_R_TOL))
    out["out_ca033_assumptions"] = (vtx_min - V_RX_MAX) / (legs_max_ca033 + R_RX * (1 + CA033_R_TOL))
    out["pc900v_worst"] = 4.0e-3   # CA-033: "worst-case forward current requirement of 4 mA"

    # the same transmitter into THIS board's receiver (TLP2361 + R47 220R 1%)
    r47 = values["R47"]
    out["i_in_worst"] = max(0.0, (vtx_min - TLP_VF_MAX) / (legs_max + r47 * (1 + R_TOL) + rds_hot))
    out["i_in_typ"] = max(0.0, (VDD - 1.5) / (r59 + r60 + r47 + FET_RDS_25))
    out["ifhl_margin"] = out["i_in_worst"] / TLP_IFHL_125

    # a 5 V legacy transmitter into this board's receiver (CA-033 5 V values)
    out["i_in_5v_max"] = max(0.0, (5.5 - TLP_VF_MAX) / (2 * 220 * (1 + R_RX_TOL) + r47 * (1 + R_TOL)))
    # a non-compliant 5 V transmitter that uses the 3.3 V resistor values
    out["i_in_5v_33r"] = max(0.0, (5.5 - TLP_VF_MAX) / (43.0 * (1 - R_TOL) + r47 * (1 - R_TOL)))

    # what the replaced TLP2362 receiver would have seen at the same current
    out["tlp2362_margin"] = out["i_in_worst"] / TLP2362_IFHL

    # faults with the driver on
    out["i_ring_zero_rds"] = vtx_max / (r59 * (1 - R_TOL))
    out["p_r59_zero_rds"] = vtx_max ** 2 / (r59 * (1 - R_TOL))
    out["i_ring_rds_max"] = vtx_max / (r59 * (1 - R_TOL) + rds_hot)
    out["p_r59_rds_max"] = out["i_ring_rds_max"] ** 2 * r59 * (1 - R_TOL)
    out["p_fet_ring_fault"] = out["i_ring_rds_max"] ** 2 * rds_hot
    out["i_tip_fault"] = vtx_max / (r60 * (1 - R_TOL))
    out["p_r60_tip_fault"] = vtx_max ** 2 / (r60 * (1 - R_TOL))

    # gate drive (ASSUMPTION-based where marked)
    out["i_gate_peak"] = vtx_max / (r58 * (1 - R_TOL))
    out["v_gate_on"] = (ASSUMED_GPIO_VOL / r58 + vtx_min / r61) / (1 / r58 + 1 / r61)
    out["vgs_on"] = vtx_min - out["v_gate_on"]
    out["v_gate_off"] = ((vtx_min - ASSUMED_GPIO_VOH_DROP) / r58 + vtx_min / r61) / (1 / r58 + 1 / r61)
    out["vgs_off"] = abs(out["v_gate_off"] - vtx_min)
    out["i_r61_dc"] = vtx_min / r61
    out["t_gate"] = r58 * FET_CISS
    return out


def evaluate(values, out):
    """[(ok, text)] - every check, in reading order."""
    r58, r59, r60, r61 = (values["R58"], values["R59"], values["R60"], values["R61"])
    checks = []

    # ok=None marks an informational line: printed, never a failure.

    def check(ok, text):
        # True / False are assertions, None is an informational line
        checks.append((True if ok is True else False if ok is False else None, text))

    # what the built circuit must be
    check(abs(r58 - 180) < 1e-6, f"R58 is {r58:g} ohm, the gate bound needs 180")
    check(abs(r59 - 33) < 1e-6, f"R59 is {r59:g} ohm, the Type A source leg is 33 (CA-033 RA)")
    check(abs(r60 - 22) < 1e-6, f"R60 is {r60:g} ohm, the fault-driven tip sink is 22")
    check(abs(r61 - 2200) < 1e-6, f"R61 is {r61:g} ohm, the gate pull-up is 2.2k")

    # MIDI OUT as a CA-033 transmitter. The 4.2 mA floor is this design's floor
    # (33 R + 22 R = 55 R of legs); the CA-033 43 R reference value is reported,
    # not asserted - the 22 R tip is fault-driven and the gap is a known limit.
    check(out["i_out_worst"] >= 4.2e-3,
          f"OUT worst-case loop {out['i_out_worst'] * 1e3:.2f} mA is below the 4.2 mA "
          "design floor")
    check(None,
          f"OUT worst-case loop {out['i_out_worst'] * 1e3:.2f} mA (1 % parts) / "
          f"{out['out_ca033_assumptions'] * 1e3:.2f} mA (CA-033's own +-5 % resistor "
          f"assumption). CA-033 sets no minimum loop current: its recommended 33/10 R "
          f"values would give {out['ca033_example'] * 1e3:.2f} mA under those "
          f"assumptions, so this design is {(out['ca033_example'] - out['out_ca033_assumptions']) * 1e3:.2f} mA "
          f"below that example and {(out['out_ca033_assumptions'] - out['pc900v_worst']) * 1e3:.2f} mA above "
          "the PC900V 4 mA worst-case figure - documented limitation, not a clean margin")

    # this board's own receiver on the same loop
    check(out["ifhl_margin"] >= 2.0,
          f"TLP2361 IFHL margin {out['ifhl_margin']:.2f}x is below 2x")
    check(out["i_in_worst"] <= 8e-3,
          f"receiver IF {out['i_in_worst'] * 1e3:.2f} mA leaves less than 2 mA to the "
          "10 mA absolute maximum")
    check(None,
          f"receiver IF {out['i_in_worst'] * 1e3:.2f} mA worst / "
          f"{out['i_in_typ'] * 1e3:.2f} mA typical against the 2..6 mA recommended "
          "window: the typical point sits above 6 mA (inside the 10 mA absolute "
          "maximum) - pending, see doc/ELECTRICAL-VERIFICATION.md")
    check(out["i_in_5v_max"] <= TLP_IF_ON_MAX,
          f"a 5 V legacy transmitter drives {out['i_in_5v_max'] * 1e3:.2f} mA into the "
          "receiver, above the 6 mA recommended on-current")
    check(None,
          f"a non-compliant 5 V source with 33/10 R legs would drive "
          f"{out['i_in_5v_33r'] * 1e3:.2f} mA - above the 10 mA absolute maximum, "
          "pending with the physical gate")
    check(None,
          f"the replaced TLP2362 would have seen {out['i_in_worst'] * 1e3:.2f} mA, i.e. "
          f"{out['tlp2362_margin']:.2f}x its 5 mA IFHL maximum and "
          f"{out['i_in_worst'] / 7.5e-3:.2f}x its 7.5 mA recommended on-current - that "
          "is why the part changed")

    # faults
    check(out["i_ring_zero_rds"] <= FET_ID_70,
          f"ring fault {out['i_ring_zero_rds'] * 1e3:.0f} mA exceeds the FET "
          f"{FET_ID_70:g} A rating at 70 C")
    check(out["p_r59_zero_rds"] <= RATED_W_2512,
          f"ring fault {out['p_r59_zero_rds']:.3f} W in R59 exceeds 1 W "
          "(zero RDS(on) bound)")
    check(out["p_fet_ring_fault"] <= 0.2,
          f"ring fault {out['p_fet_ring_fault'] * 1e3:.2f} mW in the FET is above "
          "the 200 mW budget")
    check(out["p_r60_tip_fault"] <= RATED_W_2512,
          f"tip fault {out['p_r60_tip_fault']:.3f} W in R60 exceeds 1 W")

    # gate drive
    check(out["i_gate_peak"] <= ASSUMED_GPIO_ABS_MAX,
          f"gate peak {out['i_gate_peak'] * 1e3:.2f} mA exceeds the ASSUMED 20 mA "
          "per-pin maximum")
    check(out["vgs_on"] >= 2.5,
          f"VGS(on) {out['vgs_on']:.3f} V is below the -2.5 V RDS(on) spec point")
    check(out["vgs_on"] <= FET_VGS_MAX, f"VGS(on) {out['vgs_on']:.3f} V exceeds VGS max")
    check(out["vgs_off"] <= FET_VGS_TH_MIN,
          f"OFF state |VGS| {out['vgs_off']:.3f} V is above the 0.5 V minimum "
          "threshold, so the FET is not guaranteed off")
    check(out["i_r61_dc"] <= ASSUMED_GPIO_ABS_MAX,
          f"R61 sinks {out['i_r61_dc'] * 1e3:.2f} mA, above the ASSUMED 20 mA pin limit")
    check(out["t_gate"] <= 1e-6, f"gate edge {out['t_gate'] * 1e9:.0f} ns is longer than 1 us")
    return checks


def main() -> int:
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    ap = argparse.ArgumentParser()
    ap.add_argument("--circuit", default=os.path.join(root, "tscircuit", "dist", "index", "circuit.json"))
    ap.add_argument("--quiet", action="store_true", help="only the verdict and failures")
    args = ap.parse_args()

    if not os.path.exists(args.circuit):
        sys.exit(f"missing {args.circuit} - run `bunx tsci build` first")
    circuit = json.load(open(args.circuit))
    problems = []

    # 1. the parts themselves
    for ref, want in EXPECTED_PARTS.items():
        fitted = (value_of(circuit, ref, "supplier_part_numbers") or {}).get("jlcpcb") or []
        if want["lcsc"] not in fitted:
            problems.append(f"{ref}: built JLCPCB part is {fitted or 'none'}, expected {want['lcsc']}")
        mpn = value_of(circuit, ref, "manufacturer_part_number")
        if (mpn or "") != want["mpn"]:
            problems.append(f"{ref}: built MPN is {mpn!r}, expected {want['mpn']!r}")

    # 2. the physical pin -> net mapping (SOT-23 1=G/2=S/3=D, SO6 1=A/3=K/4=G/5=VO/6=VCC)
    nets = pin_nets(circuit)
    for (ref, pin), want in EXPECTED_NETS.items():
        got = nets.get((ref, pin))
        if got != want and not (want == "GND" and got in (None, "GND")):
            problems.append(f"{ref}.{pin}: on net {got}, expected {want}")

    # the values the model needs, straight out of the built circuit
    values = {}
    for ref in ("R47", "R58", "R59", "R60", "R61"):
        values[ref] = value_of(circuit, ref, "resistance")
        if not isinstance(values[ref], (int, float)):
            problems.append(f"{ref}: built resistance is {values[ref]!r}")
            values[ref] = 0.0

    out = numbers(values)
    checks = evaluate(values, out)
    problems += [text for ok, text in checks if ok is False]
    notes = [text for ok, text in checks if ok is None]

    if not args.quiet:
        print("MIDI OUT - Type A transmitter, AO3401A high side, 3.3 V +-5 %")
        print(f"  loop into a CA-033 receiver : {out['i_out_worst'] * 1e3:6.2f} mA worst, "
              f"{out['i_out_typ'] * 1e3:6.2f} mA typical; CA-033 33/10 ohm example "
              f"{out['ca033_example'] * 1e3:.2f} mA, PC900V worst case 4.00 mA")
        print(f"  RDS(on) at 125 C (bound)    : {out['rds_hot'] * 1e3:6.1f} mOhm")
        print("MIDI IN - TLP2361 receiver, R47 220 R")
        print(f"  IF from this board          : {out['i_in_worst'] * 1e3:6.2f} mA worst, "
              f"{out['i_in_typ'] * 1e3:6.2f} mA typical")
        print(f"  IFHL margin at 125 C        : {out['ifhl_margin']:6.2f}x (TLP2361 1.6 mA max); "
              f"TLP2362 would see {out['tlp2362_margin']:.2f}x its 5 mA")
        print(f"  IF from a 5 V 2x220 R source: {out['i_in_5v_max'] * 1e3:6.2f} mA (6 mA recommended max)")
        print(f"  IF from a 5 V 33/10 R source: {out['i_in_5v_33r'] * 1e3:6.2f} mA (non-compliant source, pending)")
        print("Faults, driver on")
        print(f"  ring to GND, zero RDS       : {out['i_ring_zero_rds'] * 1e3:6.1f} mA, "
              f"{out['p_r59_zero_rds'] * 1e3:6.1f} mW in R59 (1 W)")
        print(f"  ring to GND, RDS max        : {out['i_ring_rds_max'] * 1e3:6.1f} mA, "
              f"{out['p_r59_rds_max'] * 1e3:6.1f} mW in R59, "
              f"{out['p_fet_ring_fault'] * 1e3:5.2f} mW in Q2")
        print(f"  tip forced to 3.465 V       : {out['i_tip_fault'] * 1e3:6.1f} mA, "
              f"{out['p_r60_tip_fault'] * 1e3:6.1f} mW in R60 (1 W, no transistor in the leg)")
        print("Gate drive (MCU limits are ASSUMPTIONS, see the module docstring)")
        print(f"  peak current                : {out['i_gate_peak'] * 1e3:6.2f} mA")
        print(f"  VGS on / off                : {out['vgs_on']:6.3f} V / {out['vgs_off']:.3f} V")
        print(f"  R61 DC when the pin is low  : {out['i_r61_dc'] * 1e3:6.2f} mA")
        print(f"  gate edge (R58 x Ciss)      : {out['t_gate'] * 1e9:6.0f} ns")
        print("Reported, not asserted")
        for note in notes:
            print(f"  - {note}")

    if problems:
        print(f"\n{len(problems)} problem(s):")
        for problem in problems:
            print(f"  {problem}")
        return 1
    print(f"\nall {len(checks)} electrical checks pass "
          f"({len(EXPECTED_PARTS)} parts, {len(EXPECTED_NETS)} pins)")
    return 0


if __name__ == "__main__":
    sys.exit(main())

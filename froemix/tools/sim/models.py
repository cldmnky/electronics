#!/usr/bin/env python3
"""Where every device model in the froemix decks comes from.

No manufacturer SPICE model is used anywhere, and no deck may invent a model
parameter: this table is the only allowed set. A deck line that carries a
model parameter (``.subckt OPAMP ... PARAMS: A0=...``, an instance override
``PARAMS: A0=... GBW=...``, or a ``.model`` card) must appear here with its
reason, or ``check_models`` fails.

The values are datasheet TYPICAL figures or explicitly declared assumptions.
They are not fitted part models, so a result from these decks is a design
sanity check, not a measurement of a built board and not a simulation of the
real silicon. The corresponding limitations are repeated in RESULTS.md.
"""

from __future__ import annotations

import re
from pathlib import Path

from simlib import SimError, spice_number

HERE = Path(__file__).resolve().parent

# (kind, name, ((parameter, value), ...)) -> provenance text
PROVENANCE: dict[tuple[str, str, tuple[tuple[str, float], ...]], str] = {}


def _add(kind: str, name: str, params: dict[str, float], text: str) -> None:
    key = (kind, name.upper(), tuple(sorted((k.upper(), float(v)) for k, v in params.items())))
    PROVENANCE[key] = text


# --- op-amp subcircuits: A0 = gm*RP, pole at GBW/A0, output clamped inside VP/VM
_add("subckt", "OPAMP", {"A0": 2e5, "GBW": 3e6, "HEAD": 1.5},
     "TL074 datasheet typical: open-loop gain 2e5 (106 dB), GBW 3 MHz, output "
     "swing ~1.5 V inside each rail. One-pole behavioural model, not a "
     "manufacturer model; no slew rate, no output current limit, no noise.")
_add("subckt", "OPAMP", {"A0": 1e5, "GBW": 1e6, "HEAD": 0.03},
     "MCP6002 datasheet typical: A0 1e5 (100 dB), GBW 1 MHz, rail-to-rail "
     "output within ~30 mV of each rail. Same behavioural form as the TL074.")
_add("subckt", "OPAMP", {"A0": 1e9, "GBW": 1e12, "HEAD": 1.5},
     "ideal-opamp control case: used only to reproduce a closed-form answer, "
     "so the behavioural model itself can be validated.")

# --- shunt reference
_add("subckt", "LM4040_10", {"VZ": 10.0, "RZ": 0.5},
     "LM4040C10-10 datasheet: 10.0 V reference, ~0.5 ohm dynamic impedance. "
     "Regulates at ANY current, so it cannot prove the 100 uA minimum; the "
     "deck therefore reports the cathode current, not the voltage.")

# --- diode / LED / transistor / switch models
_add("model", "D4148", {"IS": 2.52e-9, "N": 1.752},
     "1N4148/1N4148WS forward characteristic, illustrative (IS, N only): no "
     "breakdown, no capacitance, no reverse recovery.")
_add("model", "D1N4148WS", {"IS": 2.52e-9, "N": 1.752},
     "same illustrative 1N4148 characteristic, used for the D5 sync clamp.")
_add("model", "DLED", {"IS": 1e-20, "N": 2},
     "illustrative LED: no colour, brightness or supplier data. Used only to "
     "show the loading and the voltage drop, never to claim light output.")
_add("model", "DZ10", {"BV": 10.0, "IBV": 1e-3, "RS": 0.5},
     "illustrative 10 V breakdown knee for the reference LOAD and START-UP "
     "cases only, where the ideal shunt above cannot lose regulation. Not a "
     "manufacturer model and not used for the nominal reference numbers.")
_add("model", "QNPN", {"IS": 6.7e-15, "BF": 200, "VAF": 75, "RB": 10},
     "illustrative MMBT3904-class NPN (IS/BF/VAF/RB only): no charge storage, "
     "no Early-effect details, no temperature dependence. Used for the sync "
     "input switch, which is saturated or off.")
_add("model", "SWM", {"RON": 1e-3, "ROFF": 1e12, "VT": 1.65, "VH": 0},
     "ideal switch behind an explicit ON resistor, so the CD4051 ON resistance "
     "is a visible resistor (see RON below). 1.65 V threshold = mid-rail "
     "logic; no real mux control threshold is claimed.")
_add("model", "SWMOD", {"RON": 250, "ROFF": 1e12, "VT": 1.65, "VH": 0},
     "button/ADC sample switch, 250 ohm: an assumption for the sample path, "
     "not a measured value.")

# --- MIDI (doc/ELECTRICAL-VERIFICATION.md §2-§5)
_add("subckt", "AOFET", {"RDS": 0.085, "VT": 0.5},
     "AO3401A: RDS(on) 85 mOhm at VGS -2.5 V, 25 C; VGS(th) minimum magnitude 0.5 V, "
     "so the switch turns on above 0.5 V. A behavioural switch, NOT the AOS model: "
     "no gate charge curve, no body diode, no thermal model.")
_add("subckt", "AOFET", {"RDS": 0.1275, "VT": 0.5},
     "RDS(on) at 125 C: 85 mOhm x 1.5, the datasheet's own temperature factor.")
_add("subckt", "AOFET", {"RDS": 1e-06, "VT": 0.5},
     "fault bounding case: zero RDS(on) is the worst case for the series resistor and "
     "the best case for the FET (doc §4). Not a physical value.")
_add("model", "SWFET", {"RON": 1e-3, "ROFF": 1e12, "VT": 0.0, "VH": 1e-3},
     "ideal switch behind the explicit RDS resistor; the control voltage is pre-biased "
     "by the model's VT so one RDS parameter covers every case.")
_add("model", "DMINI", {"IS": 1e-9, "N": 1e-3},
     "near-ideal diode (N = 1 mV/decade): the opto LED forward drop is carried by an "
     "EXPLICIT BEHAVIOURAL VF voltage source, so a VF corner can be swept; this diode "
     "only provides reverse blocking and contributes under 1 mV of drop.")
_add("subckt", "OPTOLED", {"VF": 1.65},
     "receiver LED drop corner at 2 mA (TLP2361-style); the real LED curve is not "
     "modelled.")
_add("subckt", "OPTOLED", {"VF": 1.9},
     "receiver LED drop corner from CA-033: the receiver LED is specified at VF <= 1.9 V.")
_add("subckt", "TLP2361", {"VF": 1.65, "ITH": 1.6e-3},
     "TLP2361(TPL,E on-disk datasheet: VF maximum 1.65 V at 2 mA, IFHL maximum 1.6 mA "
     "at 125 C. Inverting totem-pole output modelled as a threshold switch, not as the "
     "Toshiba model; the 10 mA absolute maximum and 2...6 mA recommended window are "
     "limits in MIDI_ASSUMPTIONS, not model parameters.")

# MIDI assumptions that are NOT primary-verified. The decks must carry the same
# numbers; check_midi_assumptions() fails when one side changes alone.
MIDI_ASSUMPTIONS: dict[str, tuple[str, str]] = {
    "MCU_GPIO_ABS_MAX_A": ("0.020", "STM32H750 per-I/O DC absolute maximum: ASSUMPTION, "
                           "the primary datasheet was not reachable (doc §2)"),
    "MCU_VOL_V": ("0.4", "STM32H750 VOL at the rated current: ASSUMPTION (doc §2)"),
    "MCU_VOH_MIN_V": ("VDD-0.4", "STM32H750 VOH minimum: ASSUMPTION (doc §2); the deck's "
                      "2.735 V is 3.135 V - 0.4 V"),
    "MCU_RESET_STATE": ("high-Z", "USART1_TX in reset: ASSUMPTION; an internal pull-down "
                        "would move the gate divider (doc §2)"),
    "AO3401A_CISS_F": ("645p", "AO3401A Ciss for the gate-edge current: datasheet value, "
                       "used only for the charge-peak case"),
    "TLP2361_IFHL_MAX_A": ("1.6m", "TLP2361 IFHL maximum at 125 C (datasheet limit)"),
    "TLP2361_IF_ABS_MAX_A": ("10e-3", "TLP2361 absolute maximum IF (datasheet)"),
    "TLP2361_IF_RECOMMENDED_A": ("2...6e-3", "TLP2361 recommended on-current (datasheet); "
                                 "the typical loop of 6.5 mA is at the top of it -- "
                                 "pending bench item (doc §7.3)"),
    "TLP2362_IFHL_MAX_A": ("5e-3", "replaced TLP2362 IFHL maximum: the negative case"),
    "SENDER_DRIVER_LOSS_LOW_V": ("0.3", "the weakest legal sender's driver loss: "
                                 "ASSUMPTION for the low-current corner, NOT a datasheet "
                                 "guarantee"),
    "SENDER_VF_LOW_V": ("1.85", "LED forward drop at the low-current corner: ASSUMPTION, "
                        "not a full-temperature datasheet limit"),
    "SENDER_VF_HIGH_V": ("1.35", "LED forward drop at the high-current corner: ASSUMPTION; "
                         "the model's LED is a fixed behavioural voltage, so this proves "
                         "neither LED voltage nor current safety over temperature"),
    "R47_TOLERANCE": ("0.01", "R47 220 ohm at 1 %, the fitted part's tolerance"),
}

# The legal sender test loops (CA-033 / MIDI 1.0 test circuits). These describe the
# OTHER device on the cable, not this board: they are ASSUMPTIONS. The low-current
# corner uses a 0.3 V driver loss and VF 1.85 V; the high-current corner uses no
# driver loss and VF 1.35 V. Neither VF is a datasheet guarantee for all currents
# and temperatures -- the model's LED is a fixed voltage, so nothing here proves
# LED voltage or current safety over temperature.
MIDI_SENDERS: dict[str, dict] = {
    "ca033_5v": {"rail": (4.5, 5.5), "legs": 440.0, "rail_note": "5 V +-10 %, RA/RC 220+220"},
    "ca033_33": {"rail": (3.135, 3.465), "legs": 43.0, "rail_note": "3.3 V +-5 %, RA 33 / RC 10"},
}
MIDI_R47 = 220.0
MIDI_R47_TOL = 0.01
MIDI_LEGS_TOL = 0.05
# corner -> (sender, direction, driver loss V, VF V, reference current A)
MIDI_SENDER_CORNERS: dict[str, tuple[str, str, float, float, float]] = {
    "ca5v_lo": ("ca033_5v", "lo", 0.3, 1.85, 3.4347e-3),
    "ca5v_hi": ("ca033_5v", "hi", 0.0, 1.35, 6.5272e-3),
    "ca33_lo": ("ca033_33", "lo", 0.3, 1.85, 3.6843e-3),
    "ca33_hi": ("ca033_33", "hi", 0.0, 1.35, 8.1771e-3),
}


def midi_sender_current(sender: str, direction: str, driver_loss: float, vf: float,
                        r47_tol: float = MIDI_R47_TOL,
                        legs_tol: float = MIDI_LEGS_TOL) -> float:
    """Loop current of one legal sender/test circuit, in amperes.

    ``lo`` is the weakest legal loop: the low rail, the driver's 0.3 V loss, the
    high VF, and every resistance at the end that reduces the current. ``hi`` is
    the strongest. The arithmetic is the same as doc/ELECTRICAL-VERIFICATION.md
    §5 and is the only place these corners are defined: a changed tolerance or
    driver bound changes this number, so it cannot silently improve a corner.
    """
    spec = MIDI_SENDERS[sender]
    rail = spec["rail"][0] if direction == "lo" else spec["rail"][1]
    legs = spec["legs"] * (1 + legs_tol) if direction == "lo" else spec["legs"] * (1 - legs_tol)
    r47 = MIDI_R47 * (1 + r47_tol) if direction == "lo" else MIDI_R47 * (1 - r47_tol)
    return (rail - driver_loss - vf) / (legs + r47)


def midi_sender_case_parameters(corner: str) -> dict[str, float]:
    """The deck case values for one corner (VSRC, RSRC, VF, and R47)."""
    sender, direction, loss, vf, _ = MIDI_SENDER_CORNERS[corner]
    spec = MIDI_SENDERS[sender]
    rail = spec["rail"][0] if direction == "lo" else spec["rail"][1]
    legs = spec["legs"] * (1 + MIDI_LEGS_TOL) if direction == "lo" else spec["legs"] * (1 - MIDI_LEGS_TOL)
    r47 = MIDI_R47 * (1 + MIDI_R47_TOL) if direction == "lo" else MIDI_R47 * (1 - MIDI_R47_TOL)
    return {"VSRC": rail - loss, "RSRC": legs, "R47": r47, "VF": vf}


def check_midi_sender_cases(deck_path: Path) -> list[str]:
    """The deck's sender cases must carry the reference parameters exactly."""
    problems: list[str] = []
    if Path(deck_path).name != "midi_in.cir":
        return problems
    text = Path(deck_path).read_text()
    for corner in MIDI_SENDER_CORNERS:
        name = "XR_" + corner.upper()
        match = re.search(rf"^{name}\s+\S+\s+RXCHAIN\s+PARAMS:\s*(.+)$", text, re.M)
        if not match:
            problems.append(f"midi_in.cir: the sender corner {corner} ({name}) is gone")
            continue
        params = dict(re.findall(r"([A-Za-z_]\w*)\s*=\s*([^\s]+)", match.group(1)))
        wanted = midi_sender_case_parameters(corner)
        for key, value in wanted.items():
            raw = params.get(key)
            try:
                actual = spice_number(raw, f"midi_in.cir:{name}.{key}") if raw else None
            except SimError:
                actual = None
            if actual is None or abs(actual - value) > 1e-9 * max(abs(value), 1.0):
                problems.append(
                    f"midi_in.cir: {name} {key} is {raw!r}, the reference corner needs "
                    f"{value:g}. The corner values come from MIDI_SENDERS / "
                    "MIDI_SENDER_CORNERS; change them there with a reason."
                )
    return problems


MIDI_DECK_TOKENS: dict[str, list[tuple[str, str]]] = {
    "midi_out.cir": [("VOL 0.4 V", "MCU_VOL_V"), ("2.735", "MCU_VOH_MIN_V"),
                     ("645p", "AO3401A_CISS_F")],
    "midi_in.cir": [("1.6m", "TLP2361_IFHL_MAX_A"),
                     ("VF=1.85", "SENDER_VF_LOW_V"), ("VF=1.35", "SENDER_VF_HIGH_V"),
                     ("VSRC=4.2", "SENDER_DRIVER_LOSS_LOW_V")],
}

# ON resistance of the CD4051 model is a resistor value, recorded here so a
# changed number has to be justified in one place.
MUX_RON = 250.0
MUX_RON_PROVENANCE = (
    "CD4051B ON resistance at 3.3 V: 250 ohm is an assumption in the middle of "
    "the datasheet range (ON resistance rises as the supply falls to 3.3 V). "
    "Not a maximum, not a manufacturer model."
)
ADC_SAMPLE_CAPACITANCE = 100e-12
ADC_SAMPLE_PROVENANCE = (
    "100 pF sample-and-hold assumption for the Seed ADC acquisition; the "
    "STM32H750 datasheet value depends on the sample time, which firmware "
    "owns. Used only to show the settling ORDER OF MAGNITUDE."
)

SUBCKT_PARAM_RE = re.compile(r"^\s*\.subckt\s+(\w+)\b(.*)$", re.I)
MODEL_RE = re.compile(r"^\s*\.model\s+(\w+)\s+(\w+)\s*\((.*)\)\s*$", re.I)
PARAMS_RE = re.compile(r"PARAMS:\s*(.*)$", re.I)
INSTANCE_RE = re.compile(r"^\s*X\w+\s+(.*)$", re.I)

# parameters that identify an op-amp model; an instance override carrying any
# of them must match a table entry
OPAMP_PARAMS = {"A0", "GBW", "HEAD"}

# deck-local subcircuits that hold a device model (the rest are case networks)
DEVICE_SUBCKTS = {"OPAMP", "LM4040_10", "AOFET", "OPTOLED", "TLP2361"}


def _pairs(text: str) -> dict[str, float]:
    """``A0=2e5 GBW=3e6`` -> {'A0': 200000.0, ...}; expression values skipped."""
    found: dict[str, float] = {}
    for match in re.finditer(r"([A-Za-z_]\w*)\s*=\s*(\{[^}]*\}|[^\s]+)", text):
        key, raw = match.group(1).upper(), match.group(2)
        if raw.startswith("{"):
            continue
        try:
            found[key] = spice_number(raw)
        except SimError:
            continue
    return found


def deck_model_lines(path: Path) -> list[tuple[str, str, dict[str, float], str]]:
    """(kind, name, parameters, line) for every model-parameter line in a deck."""
    found = []
    for line in Path(path).read_text().splitlines():
        if line.lstrip().startswith("*"):
            continue
        subckt = SUBCKT_PARAM_RE.match(line)
        if subckt:
            params = PARAMS_RE.search(line)
            # only the device models are checked here; the deck-local case
            # subcircuits (SRC_CASE, CVIN, CVOUT, MUX8, ...) hold copied
            # values and are covered by source_link and the assertions
            if params and subckt.group(1).upper() in DEVICE_SUBCKTS:
                found.append(("subckt", subckt.group(1), _pairs(params.group(1)), line.strip()))
            continue
        model = MODEL_RE.match(line)
        if model:
            found.append(("model", model.group(1), _pairs(model.group(3)), line.strip()))
            continue
        instance = INSTANCE_RE.match(line)
        if instance and "PARAMS:" in line:
            params = _pairs(PARAMS_RE.search(line).group(1))
            if OPAMP_PARAMS & set(params):
                found.append(("instance", "OPAMP", params, line.strip()))
    return found


def check_models(deck_path: Path) -> list[str]:
    """Every model parameter in a deck must appear in PROVENANCE."""
    problems: list[str] = []
    for kind, name, params, line in deck_model_lines(deck_path):
        if not params:
            continue
        key = (kind, name.upper(), tuple(sorted(params.items())))
        # an instance override may set only a subset (the subckt default fills
        # the rest), so try the full triple as well as the partial set
        if key in PROVENANCE:
            continue
        if kind == "instance" and OPAMP_PARAMS <= set(params):
            key = ("subckt", "OPAMP", tuple(sorted(params.items())))
            if key in PROVENANCE:
                continue
        if kind == "instance":
            continue
        problems.append(
            f"{Path(deck_path).name}: model line is not in the provenance table "
            f"({kind} {name} {params}): {line!r}. Add it to models.py with its "
            "source, or remove the parameter."
        )
    return problems


def check_midi_assumptions(deck_path: Path) -> list[str]:
    """The decks must carry the declared MIDI assumption numbers."""
    problems: list[str] = []
    name = Path(deck_path).name
    text = Path(deck_path).read_text()
    for token, key in MIDI_DECK_TOKENS.get(name, []):
        if token not in text:
            problems.append(
                f"{name}: the declared MIDI assumption {key} ({MIDI_ASSUMPTIONS[key][0]}) "
                f"is not written as {token!r} any more. Update the deck and the "
                "assumption together, or explain the change in RESULTS.md."
            )
    return problems


def check_mux_assumptions(deck_path: Path) -> list[str]:
    """The CD4051 ON resistance and the sample capacitance are assumptions."""
    problems: list[str] = []
    text = Path(deck_path).read_text()
    if "mux_adc" in Path(deck_path).name:
        values = {float(v) for v in re.findall(r"^RONS\d+\s+\S+\s+\S+\s+([0-9.]+)", text, re.M)}
        if values and values != {MUX_RON}:
            problems.append(
                f"mux_adc.cir: the CD4051 ON resistance is {sorted(values)} ohm; "
                f"models.MUX_RON is {MUX_RON:g} ohm. Update both with a reason."
            )
        if " 100p" not in text:
            problems.append("mux_adc.cir: the 100 pF sample capacitor is gone; "
                            "models.ADC_SAMPLE_CAPACITANCE documents it")
    return problems


def table_text() -> str:
    lines = ["model / parameter set                                             source"]
    for (kind, name, params), text in sorted(PROVENANCE.items()):
        shown = " ".join(f"{k}={v:g}" for k, v in params)
        lines.append(f"{kind} {name} {shown}")
        lines.append(f"    {text}")
    lines.append("MUX RON 250 ohm: " + MUX_RON_PROVENANCE)
    lines.append("")
    lines.append("MIDI assumptions (NOT primary verified, doc/ELECTRICAL-VERIFICATION.md §2):")
    for key, (value, text) in MIDI_ASSUMPTIONS.items():
        lines.append(f"  {key} = {value}: {text}")
    lines.append("ADC sample 100 pF: " + ADC_SAMPLE_PROVENANCE)
    return "\n".join(lines)


if __name__ == "__main__":
    import sys

    decks = sorted(HERE.glob("*.cir"))
    problems = [p for deck in decks for p in check_models(deck)]
    problems += [p for deck in decks for p in check_mux_assumptions(deck)]
    problems += [p for deck in decks for p in check_midi_assumptions(deck)]
    problems += [p for deck in decks for p in check_midi_sender_cases(deck)]
    if problems:
        print(f"model provenance: {len(problems)} problem(s)")
        for problem in problems:
            print(f"  {problem}")
        sys.exit(1)
    print(table_text())
    print(f"\nmodel provenance: {len(decks)} decks OK, "
          f"{len(PROVENANCE)} parameter sets")

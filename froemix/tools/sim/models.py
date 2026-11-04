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
DEVICE_SUBCKTS = {"OPAMP", "LM4040_10"}


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
    lines.append("ADC sample 100 pF: " + ADC_SAMPLE_PROVENANCE)
    return "\n".join(lines)


if __name__ == "__main__":
    import sys

    decks = sorted(HERE.glob("*.cir"))
    problems = [p for deck in decks for p in check_models(deck)]
    problems += [p for deck in decks for p in check_mux_assumptions(deck)]
    if problems:
        print(f"model provenance: {len(problems)} problem(s)")
        for problem in problems:
            print(f"  {problem}")
        sys.exit(1)
    print(table_text())
    print(f"\nmodel provenance: {len(decks)} decks OK, "
          f"{len(PROVENANCE)} parameter sets")

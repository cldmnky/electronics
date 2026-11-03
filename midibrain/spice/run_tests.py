#!/usr/bin/env python3
"""Run the simplified MIDI Brain SPICE design checks.

These are resistor-network/polarity models, not datasheet-accurate TLP2362,
SN74LVC1G126, 2N7002 or Schottky models. They do not prove hardware timing,
isolation, EMC or thermal performance.
"""
from __future__ import annotations
import math
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
MEAS = re.compile(r"^\s*([a-zA-Z0-9_]+)\s*=\s*([-+0-9.eE]+)\s*$")


def run(name: str) -> dict[str, float]:
    proc = subprocess.run(
        ["ngspice", "-b", "-o", f"/tmp/midibrain-{name}.log", f"{name}.cir"],
        cwd=ROOT, text=True, capture_output=True,
    )
    log = Path(f"/tmp/midibrain-{name}.log").read_text()
    if proc.returncode != 0 or "Error:" in log:
        raise RuntimeError(f"{name}: ngspice failed\n{log[-3000:]}")
    values = {}
    for line in log.splitlines():
        m = MEAS.match(line)
        if m:
            values[m.group(1)] = float(m.group(2))
    if not values:
        raise RuntimeError(f"{name}: no .meas results\n{log[-3000:]}")
    return values


def check(label: str, condition: bool, detail: str) -> None:
    if not condition:
        raise AssertionError(f"{label}: {detail}")
    print(f"PASS {label}: {detail}")


def main() -> int:
    out = run("midi_out")
    check("MIDI OUT idle current", abs(out["i_idle"]) < 100e-6, f"{out['i_idle']*1e3:.3f} mA")
    check("MIDI OUT active current", 4e-3 <= out["i_active"] <= 7e-3, f"{out['i_active']*1e3:.3f} mA")
    check("MIDI OUT active ring voltage", out["v_ring_active"] > 2.5, f"{out['v_ring_active']:.3f} V")

    inn = run("midi_in")
    check("MIDI IN loop current", 4e-3 <= abs(inn["i_active"]) <= 10e-3, f"{abs(inn['i_active'])*1e3:.3f} mA")
    check("MIDI IN receiver low", inn["v_active"] < 0.8, f"{inn['v_active']:.3f} V")
    check("MIDI IN idle current", abs(inn["i_idle"]) < 100e-6, f"{abs(inn['i_idle'])*1e3:.3f} mA")
    check("MIDI IN receiver high", inn["v_idle"] > 3.0, f"{inn['v_idle']:.3f} V")

    power = run("power_backfeed")
    check("Pi 5 V mode VSYS", power["v_pi_mode"] > 4.5, f"{power['v_pi_mode']:.3f} V")
    check("Pico USB mode VSYS", power["v_usb_mode"] > 4.5, f"{power['v_usb_mode']:.3f} V")
    check("Pico USB backfeed into Pi", abs(power["i_usb_back"]) < 100e-6, f"{abs(power['i_usb_back'])*1e6:.3f} uA")
    check("Pi backfeed into Pico USB", abs(power["i_pi_back"]) < 100e-6, f"{abs(power['i_pi_back'])*1e6:.3f} uA")

    reset = run("reset")
    check("Pico RUN asserted", reset["v_run_on"] < 0.5, f"{reset['v_run_on']:.3f} V")
    check("Pico RUN released", reset["v_run_off"] > 3.0, f"{reset['v_run_off']:.3f} V")

    oe = run("spi_oe")
    check("SPI buffers enabled with Pi", oe["v_oe_on"] > 3.0, f"{oe['v_oe_on']:.3f} V")
    check("SPI buffers disabled without Pi", oe["v_oe_off"] < 0.1, f"{oe['v_oe_off']:.3f} V")
    print("PASS: simplified SPICE design checks (not hardware validation)")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        raise SystemExit(1)

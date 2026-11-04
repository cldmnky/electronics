#!/usr/bin/env python3
"""Run the simplified MIDI Brain SPICE design checks.

These are resistor-network and behavioural models. They are not datasheet
SPICE models of the TLP2361, SN74LVC1G126, 2N7002 or the B5819W, and they do
not prove timing, isolation, EMC, thermal behaviour or anything measured on
hardware. The MIDI input model does use the datasheet threshold current and a
diode fitted to the datasheet forward voltage, so a threshold that the loop
current cannot reach makes the check fail.
"""
from __future__ import annotations

import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent
MEAS = re.compile(r"^\s*([a-zA-Z0-9_]+)\s*=\s*([-+0-9.eE]+)\s*$")
LOG_DIR: Path | None = None


def log_dir() -> Path:
    """One private directory per run, so parallel checkouts cannot collide."""
    global LOG_DIR
    if LOG_DIR is None:
        LOG_DIR = Path(tempfile.mkdtemp(prefix="midibrain-spice-"))
    return LOG_DIR


def run(name: str) -> dict[str, float]:
    log = log_dir() / f"{name}.log"
    proc = subprocess.run(
        ["ngspice", "-b", "-o", str(log), f"{name}.cir"],
        cwd=ROOT, text=True, capture_output=True,
    )
    text = log.read_text() if log.is_file() else ""
    if proc.returncode != 0 or "Error:" in text or not text:
        raise RuntimeError(
            f"{name}: ngspice failed (exit {proc.returncode})\n"
            f"command: ngspice -b -o {log} {name}.cir\n"
            f"stdout:\n{proc.stdout[-2000:]}\nstderr:\n{proc.stderr[-2000:]}\n"
            f"ngspice log ({log}):\n{text[-3000:]}"
        )
    values = {}
    for line in text.splitlines():
        match = MEAS.match(line)
        if match:
            values[match.group(1)] = float(match.group(2))
    if not values:
        raise RuntimeError(f"{name}: no .meas results in {log}\n{text[-3000:]}")
    return values


def check(label: str, condition: bool, detail: str) -> None:
    if not condition:
        raise AssertionError(f"{label}: {detail}")
    print(f"PASS {label}: {detail}")


def main() -> int:
    if shutil.which("ngspice") is None:
        print("FAIL: the `ngspice` executable is not on PATH, so no SPICE check can run. "
              "Install the ngspice package (Debian/Ubuntu: `apt install ngspice`) "
              "and re-run `make test`.", file=sys.stderr)
        return 1
    # AO3401A high-side smoke model: gate network 100 ohm / 2.2 kOhm, 33 ohm
    # ring, 22 ohm tip, 220 ohm receiver, 1.9 V LED. Not the AOS model.
    out = run("midi_out")
    check("MIDI OUT idle loop current", abs(out["i_loop_idle"]) < 100e-6,
          f"{abs(out['i_loop_idle'])*1e3:.4f} mA")
    check("MIDI OUT active loop current", 4e-3 <= abs(out["i_loop_active"]) <= 7e-3,
          f"{abs(out['i_loop_active'])*1e3:.3f} mA")
    check("MIDI OUT active ring voltage", out["v_ring_active"] > 2.5, f"{out['v_ring_active']:.3f} V")
    check("MIDI OUT gate held at the rail while idle (switch off)",
          out["v_gate_idle"] > 3.2, f"{out['v_gate_idle']:.3f} V")
    check("MIDI OUT gate pulled low while active (switch on)",
          out["v_gate_active"] < 0.5,
          f"{out['v_gate_active']:.3f} V, |VGS| {3.3 - out['v_gate_active']:.2f} V")

    # Nominal 5 V Type A sender, TLP2361 threshold (1.3 mA max).
    inn = run("midi_in")
    check("MIDI IN loop current, nominal 5 V sender", 4e-3 <= abs(inn["i_active"]) <= 10e-3,
          f"{abs(inn['i_active'])*1e3:.3f} mA")
    check("MIDI IN receiver low with the nominal sender", inn["v_active"] < 0.8, f"{inn['v_active']:.3f} V")
    check("MIDI IN idle current", abs(inn["i_idle"]) < 100e-6, f"{abs(inn['i_idle'])*1e3:.3f} mA")
    check("MIDI IN receiver high when idle", inn["v_idle"] > 3.0, f"{inn['v_idle']:.3f} V")

    # Weakest 5 V sender and the highest datasheet forward voltage: the
    # TLP2361 threshold is still reached.
    weak = run("midi_in_weak")
    check("MIDI IN weak-sender loop current", 4e-3 <= abs(weak["i_active"]) <= 10e-3,
          f"{abs(weak['i_active'])*1e3:.3f} mA")
    check("MIDI IN receiver still low at the weakest sender and highest VF",
          weak["v_active"] < 0.8, f"{weak['v_active']:.3f} V")

    # Same current, replaced TLP2362 threshold (5.0 mA max): the receiver does
    # not switch, which is the defect the part change removes.
    old = run("midi_in_2362")
    check("TLP2362 would not switch at the weak-sender current",
          abs(old["i_active"] - weak["i_active"]) < 1e-6 and old["v_active"] > 3.0,
          f"{abs(old['i_active'])*1e3:.3f} mA loop current, output stays {old['v_active']:.3f} V high "
          "against a 5.0 mA threshold")

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

    print(f"PASS: simplified SPICE design checks (not hardware validation); logs in {log_dir()}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        raise SystemExit(1)
    except RuntimeError as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        raise SystemExit(1)

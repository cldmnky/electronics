#!/usr/bin/env python3
"""Turn the ngspice raw output in froemix/tools/sim/out/ into a measured-vs-expected table.

Usage:
    cd froemix/tools/sim && bash run.sh          # decks + this script
    cd froemix/tools/sim && python3 analyze.py   # analyse existing out/ files

Reads the `wrdata` files written by the four decks (`*.dat`) and prints the
table that RESULTS.md transcribes. `run.sh` writes the same text to
out/measured.txt so a re-run can be diffed against the committed numbers.

wrdata writes (scale, value) pairs, one pair per requested vector, so the
value of vector i is column 2*i+1 and the x axis repeats in the even columns.

Informational only: prints a table, always exits 0, and is not part of
`bun run gate` (the gate never needs a SPICE simulator).
"""

from __future__ import annotations

import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "out")


def read_vectors(path: str) -> tuple[list[float], list[list[float]]]:
    """Return (x, [vector0, vector1, ...]) from a wrdata file."""
    x: list[float] = []
    columns: list[list[float]] = []
    with open(path) as handle:
        for line in handle:
            fields = line.split()
            if not fields:
                continue
            values = [float(field) for field in fields]
            x.append(values[0])
            for index, value in enumerate(values[1::2]):
                while len(columns) <= index:
                    columns.append([])
                columns[index].append(value)
    return x, columns


def minus3db(x: list[float], gain_db: list[float]) -> float | None:
    """First frequency where the response falls 3 dB below its low-frequency plateau."""
    target = gain_db[0] - 3.0
    for index in range(1, len(x)):
        if gain_db[index] <= target:
            # interpolate in log frequency, the sweep is logarithmic
            log_f0, log_f1 = math.log10(x[index - 1]), math.log10(x[index])
            g0, g1 = gain_db[index - 1], gain_db[index]
            return 10 ** (log_f0 + (target - g0) / (g1 - g0) * (log_f1 - log_f0))
    return None


def at(x: list[float], values: list[float], frequency: float) -> float:
    index = min(range(len(x)), key=lambda i: abs(math.log(x[i] / frequency)))
    return values[index]


def audio(name: str, title: str, extra_labels: list[str]) -> None:
    path = os.path.join(OUT, f"{name}_ac.dat")
    x, columns = read_vectors(path)
    print(f"\n## {title}")
    print(f"{'quantity':34s} {'measured':>12s}")
    gain = columns[0]
    print(f"{'DC gain':34s} {10 ** (gain[0] / 20):12.5f}")
    print(f"{'DC gain (dB)':34s} {gain[0]:12.3f}")
    f3 = minus3db(x, gain)
    print(f"{'f(-3 dB) Hz':34s} {f3:12.0f}")
    print(f"{'gain at 1 kHz (dB)':34s} {at(x, gain, 1000):12.3f}")
    print(f"{'gain at 20 kHz':34s} {10 ** (at(x, gain, 20000) / 20):12.5f}")
    print(f"{'gain at 20 kHz (dB)':34s} {at(x, gain, 20000):12.3f}")
    for index, label in enumerate(extra_labels, start=1):
        print(f"{label:34s} {columns[index][0]:12.3f}   (DC gain, dB)")


def cv_in_out() -> None:
    print("\n## CV in (J_CV_IN_1 -> CV_IN_1, U13 MCP6002 on 3.3 V)")
    print("   expected: +/-5 V -> 3.1146 / 0.1046 V, scale 0.3010 V/V, ~100 mV inboard of the rails")

    x, columns = read_vectors(os.path.join(OUT, "cv_in_dc.dat"))
    labels = [
        "IDEAL",
        "1%_N5HI",
        "1%_N5LO",
        "1%_P5HI",
        "1%_P5LO",
        "0.1%_N5HI",
        "0.1%_N5LO",
        "0.1%_P5HI",
        "0.1%_P5LO",
        "MCP6002",
    ]
    print(f"{'instance':12s} {'V(Vin=-5V)':>11s} {'V(Vin=+5V)':>11s} {'scale V/V':>10s} {'scale err':>10s} {'cents/oct':>10s}")
    for index, label in enumerate(labels):
        low = columns[index][0]  # Vin = -5 V, first sweep point
        high = columns[index][-1]  # Vin = +5 V, last sweep point
        scale = (low - high) / 10.0
        error = scale / 0.3010 - 1
        print(f"{label:12s} {low:11.5f} {high:11.5f} {scale:10.5f} {error * 100:9.3f}% {error * 1200:10.1f}")
    print("   cents/octave = 1200 * fractional scale error; the fixed part of the\n"
          "   error (the CV-IN offset term) is not in this column -- see RESULTS.md")

    print("\n## CV out (CV_OUT_1 -> J_CV_OUT_1, U12 TL074, U11A follower, load sweep)")
    print("   expected: 0.000 / 4.990 V and scale 1.5121 V/V for EVERY load (R40/R39")
    print("   sense past the 1k series R, so 100k/50k/10k/open all land on the same number)")
    x, columns = read_vectors(os.path.join(OUT, "cv_out_dc.dat"))
    labels = ["NOM", "OPEN", "50K", "10K", "DAC13K", "1%_MAX", "1%_MIN", "0.1%_MAX", "0.1%_MIN"]
    nominal = 49.9 / 33 * 100 / 100  # 1.51212: the 1k is inside the loop, no load divider
    print(f"{'instance':12s} {'V(Vdac=0V)':>11s} {'V(Vdac=3.3V)':>12s} {'scale V/V':>10s} {'scale err':>10s} {'cents/oct':>10s}")
    for index, label in enumerate(labels):
        low = columns[index][0]
        high = columns[index][-1]
        scale = (high - low) / 3.3
        error = scale / nominal - 1
        print(f"{label:12s} {low:11.5f} {high:12.5f} {scale:10.5f} {error * 100:9.3f}% {error * 1200:10.1f}")
    opamp = columns[len(labels)][-1]
    print(f"{'10K, U12B out':12s} {'':11s} {opamp:12.5f}   (before the 1k: 1k * (Iload + Ifb) above the jack)")


def step_metrics(x: list[float], y: list[float]) -> dict:
    """Overshoot / settling / ringing of a step from 0 at t = 1us."""
    step = 1e-6
    tail = [value for t, value in zip(x, y) if t >= x[-1] - 0.1 * (x[-1] - step)]
    final = sum(tail) / len(tail)
    peak = max(y)
    overshoot = (peak - final) / abs(final) * 100.0 if final else 0.0
    low, high = 0.99 * final, 1.01 * final
    outside = [t for t, value in zip(x, y) if t > step and not (low <= value <= high)]
    settled = "never" if outside and outside[-1] > x[-1] - 0.05 * (x[-1] - step) else (
        f"{outside[-1] * 1e6:.0f}us" if outside else "<1us")
    ring = [value for t, value in zip(x, y) if t >= x[-1] - 0.25 * (x[-1] - step)]
    ripple = (max(ring) - min(ring)) if ring else 0.0
    ripple_pct = ripple / abs(final) * 100.0 if final else 0.0
    if ripple_pct > 1.0:
        rings = f"rings, {ripple * 1000:.0f}mV pp"
    elif ripple_pct > 0.1:
        rings = f"damped ring, {ripple * 1000:.1f}mV pp"
    else:
        rings = "no ring"
    return {
        "final": final,
        "overshoot": overshoot,
        "settled": settled,
        "rings": rings,
    }


def cv_out_stability() -> None:
    loads = ["1nF", "10nF", "100nF"]
    for name, title in (("small", "CV out load-capacitance step, 0.1V DAC (linear)"),
                        ("full", "CV out load-capacitance step, 3.3V DAC (full scale)")):
        x, columns = read_vectors(os.path.join(OUT, f"cv_out_step_{name}.dat"))
        print(f"\n## {title}")
        print(f"{'load':6s} {'CF':4s} {'overshoot':>10s} {'settle 1%':>10s}  {'ringing':22s}")
        for index, load in enumerate(loads):
            for offset, label in ((0, "0"), (1, "100p")):
                metrics = step_metrics(x, columns[index * 2 + offset])
                print(f"{load:6s} {label:4s} {metrics['overshoot']:9.1f}% "
                      f"{metrics['settled']:>10s}  {metrics['rings']:22s}")
    print("   the 100pF column is the fitted C33/C34 (across R39/R40); 0 is the")
    print("   old board. Full-scale 100nF is limited by the opamp output clamp")
    print("   charging 100nF through the 1k (see RESULTS.md).")


def ref_bias() -> None:
    print("\n## -10 V reference (U14 LM4040C10, R41 2.2k, VEE sweep)")
    print("   expected: 0.809 mA at VEE = -12.0 V, worst case 0.445 mA at -11.2 V")
    x, columns = read_vectors(os.path.join(OUT, "ref_bias_dc.dat"))
    vref, current = columns[0], columns[1]
    print(f"{'VEE':>7s} {'V(N10V_REF)':>12s} {'I_cathode uA':>13s}")
    for wanted in (-11.2, -11.7, -12.0, -12.2):
        index = min(range(len(x)), key=lambda i: abs(x[i] - wanted))
        print(f"{x[index]:7.2f} {vref[index]:12.5f} {abs(current[index]) * 1e6:13.1f}")
    worst = min(abs(value) for value in current)
    print(f"minimum over the whole sweep: {worst * 1e6:.1f} uA  (LM4040-10 needs 100 uA, brief asks >125 uA)")


def main() -> int:
    missing = [name for name in ("audio_in_ac", "audio_out_ac", "cv_in_dc", "cv_out_dc", "cv_out_step_small", "cv_out_step_full", "ref_bias_dc") if not os.path.exists(os.path.join(OUT, f"{name}.dat"))]
    if missing:
        print(f"missing raw output: {', '.join(missing)} -- run `bash run.sh` first", file=sys.stderr)
        return 0
    audio("audio_in", "Audio in (J_AUDIO_IN_L -> AUDIO_IN_L, U2 TL074)", ["stage 1 out NET_C2_2"])
    audio("audio_out", "Audio out (AUDIO_OUT_L -> J_AUDIO_OUT_L, U3 TL074, 100k load)", ["U3B out NET_C12_2 (pre-R19)"])
    cv_in_out()
    cv_out_stability()
    ref_bias()
    return 0


if __name__ == "__main__":
    sys.exit(main())

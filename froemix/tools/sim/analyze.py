#!/usr/bin/env python3
"""Print the measured-vs-expected table from a directory of ngspice output.

    cd froemix/tools/sim && python3 analyze.py            # the committed out/
    python3 analyze.py --dir /tmp/run/audio_in/out        # a fresh run directory

This is the human-readable report; ``run_checks.py`` is the authority that
ASSERTS the limits and exits nonzero. analyze.py never runs ngspice and never
writes anything: it reads ``wrdata`` files that are already on disk. The
committed ``out/`` directory is history (the numbers RESULTS.md quotes), so a
table printed from it describes the deck as it was, not as it is -- use a run
directory from ``bash run.sh`` for current numbers.

A missing or unreadable file is an error (exit 2), not an empty table: the
strict reader in simlib.py rejects missing rows, NaN/Inf values and ragged
columns, so this report cannot silently show a broken simulation as numbers.
"""

from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path

import simlib

HERE = Path(__file__).resolve().parent


def read_vectors(path: Path) -> tuple[list[float], list[list[float]]]:
    """(scale, [vector, ...]) from a wrdata file in the scale/value pair layout."""
    return simlib.read_wrdata(path, layout="pairs")


def audio(directory: Path, name: str, title: str, extra_labels: list[str]) -> None:
    x, columns = read_vectors(directory / f"{name}_ac.dat")
    print(f"\n## {title}")
    print(f"{'quantity':34s} {'measured':>12s}")
    gain = columns[0]
    print(f"{'DC gain':34s} {10 ** (gain[0] / 20):12.5f}")
    print(f"{'DC gain (dB)':34s} {gain[0]:12.3f}")
    f3 = simlib.minus3db(x, gain, name)
    print(f"{'f(-3 dB) Hz':34s} {f3:12.0f}")
    print(f"{'gain at 1 kHz (dB)':34s} {simlib.value_at(x, gain, 1000, name, log=True):12.3f}")
    at20 = simlib.value_at(x, gain, 20000, name, log=True)
    print(f"{'gain at 20 kHz':34s} {10 ** (at20 / 20):12.5f}")
    print(f"{'gain at 20 kHz (dB)':34s} {at20:12.3f}")
    for index, label in enumerate(extra_labels, start=1):
        print(f"{label:34s} {columns[index][0]:12.3f}   (DC gain, dB)")


def cv_in_out(directory: Path) -> None:
    print("\n## CV in (J_CV_IN_1 -> CV_IN_1, U13 MCP6002 on 3.3 V)")
    print("   expected: +/-5 V -> 3.1146 / 0.1046 V, scale 0.3010 V/V")
    x, columns = read_vectors(directory / "cv_in_dc.dat")
    labels = ["IDEAL", "1%_N5HI", "1%_N5LO", "1%_P5HI", "1%_P5LO",
              "0.1%_N5HI", "0.1%_N5LO", "0.1%_P5HI", "0.1%_P5LO", "MCP6002"]
    print(f"{'instance':12s} {'V(Vin=-5V)':>11s} {'V(Vin=+5V)':>11s} {'scale V/V':>10s} "
          f"{'scale err':>10s} {'cents/oct':>10s}")
    for index, label in enumerate(labels):
        low = columns[index][0]
        high = columns[index][-1]
        scale = (low - high) / 10.0
        error = scale / 0.3010 - 1
        print(f"{label:12s} {low:11.5f} {high:11.5f} {scale:10.5f} {error * 100:9.3f}% "
              f"{error * 1200:10.1f}")

    print("\n## CV out (CV_OUT_1 -> J_CV_OUT_1, U12 TL074, U11A follower, load sweep)")
    print("   expected: 0.000 / 4.990 V and scale 1.5121 V/V for EVERY load")
    x, columns = read_vectors(directory / "cv_out_dc.dat")
    labels = ["NOM", "OPEN", "50K", "10K", "DAC13K", "1%_MAX", "1%_MIN", "0.1%_MAX",
              "0.1%_MIN", "NOUT", "DAC10K3", "DAC16K", "NO-follower"]
    nominal = 49.9 / 33
    print(f"{'instance':12s} {'V(0V)':>11s} {'V(3.3V)':>12s} {'scale V/V':>10s} "
          f"{'scale err':>10s} {'cents/oct':>10s}")
    if len(columns) < len(labels):
        print(f"   ({len(labels) - len(columns)} newer instance column(s) absent from this "
              "run: buffered/unbuffered DAC and no-follower cases)")
    for index, label in enumerate(labels[:len(columns)]):
        low = columns[index][0]
        high = columns[index][-1]
        scale = (high - low) / 3.3
        error = scale / nominal - 1
        print(f"{label:12s} {low:11.5f} {high:12.5f} {scale:10.5f} {error * 100:9.3f}% "
              f"{error * 1200:10.1f}")


def cv_in_pole(directory: Path) -> None:
    x, columns = read_vectors(directory / "cv_in_ac.dat")
    gain = columns[0]
    print("\n## CV in bandwidth (C16 10nF across R33 30.1k: pole at 529 Hz)")
    print(f"{'frequency':>12s} {'gain dB':>10s} {'gain':>10s}")
    for frequency in (10.0, 100.0, 529.0, 1000.0, 10000.0):
        value = simlib.value_at(x, gain, frequency, "cv_in_ac", log=True)
        print(f"{frequency:12.0f} {value:10.3f} {10 ** (value / 20):10.4f}")


def cv_out_stability(directory: Path) -> None:
    loads = ["1nF", "10nF", "100nF"]
    for name, title in (("small", "CV out load-capacitance step, 0.1 V DAC"),
                        ("full", "CV out load-capacitance step, 3.3 V DAC")):
        t, columns = read_vectors(directory / f"cv_out_step_{name}.dat")
        print(f"\n## {title}")
        print(f"{'load':6s} {'CF':5s} {'overshoot':>10s} {'settle 1%':>10s}  {'ringing':22s}")
        for index, load in enumerate(loads):
            for offset, label in ((0, "0"), (1, "100p")):
                metrics = simlib.step_metrics(t, columns[index * 2 + offset])
                settled = metrics["settle"]
                settle_text = "never" if settled == float("inf") else f"{settled * 1e6:.0f}us"
                ripple = metrics["ripple_pct"]
                rings = ("rings" if ripple > 1.0 else
                         "damped ring" if ripple > 0.1 else "no ring")
                print(f"{load:6s} {label:5s} {metrics['overshoot']:9.1f}% "
                      f"{settle_text:>10s}  {rings:8s} {metrics['ripple'] * 1e3:8.2f}mVpp")
    print("   the 100pF column is the fitted C33/C34 (across R39/R40); 0 is the")
    print("   board before the compensation. Above 10 nF the loop is outside its")
    print("   spec and the full-scale step is limited by charging through the 1k.")


def ref_bias(directory: Path) -> None:
    print("\n## -10 V reference (U14 LM4040C10, R41 2.2k, VEE sweep)")
    print("   expected: (12-10)/2.2k - 2 x 10 V/187k = 802 uA at VEE = -12.0 V;")
    print("   worst case 438 uA at -11.2 V. The committed out/ predates the fitted")
    print("   187k legs and therefore reads 809 uA.")
    x, columns = read_vectors(directory / "ref_bias_dc.dat")
    vref, current = columns[0], columns[1]
    print(f"{'VEE':>7s} {'V(N10V_REF)':>12s} {'I_cathode uA':>13s}")
    for wanted in (-11.2, -11.7, -12.0, -12.2):
        index = simlib.index_at(x, wanted, "ref_bias")
        print(f"{x[index]:7.2f} {vref[index]:12.5f} {abs(current[index]) * 1e6:13.1f}")
    worst = min(abs(value) for value in current)
    print(f"minimum over the whole sweep: {worst * 1e6:.1f} uA "
          "(LM4040-10 needs 100 uA, the brief asks for 125 uA)")


REQUIRED = ("audio_in_ac", "audio_out_ac", "cv_in_dc", "cv_out_dc",
            "cv_out_step_small", "cv_out_step_full", "ref_bias_dc")
# newer sections are printed only when the run directory has them (the
# committed out/ predates the CV-in bandwidth sweep)
OPTIONAL = ("cv_in_ac",)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--dir", type=Path, default=HERE / "out",
                        help="directory holding the wrdata files (default: out/)")
    args = parser.parse_args()
    directory = args.dir
    missing = [name for name in REQUIRED if not (directory / f"{name}.dat").is_file()]
    if missing:
        print(f"missing raw output in {directory}: {', '.join(missing)}", file=sys.stderr)
        print("run `bash run.sh` and analyse its run directory, or pass --dir", file=sys.stderr)
        return 2
    print(f"# analog output from {directory}")
    if os.path.samefile(directory, HERE / "out"):
        print("# note: out/ is COMMITTED HISTORY; run_checks.py asserts the current decks")
    audio(directory, "audio_in", "Audio in (J_AUDIO_IN_L -> AUDIO_IN_L, U2 TL074)",
          ["stage 1 out NET_C2_2"])
    audio(directory, "audio_out", "Audio out (AUDIO_OUT_L -> J_AUDIO_OUT_L, U3 TL074, 100k load)",
          ["U3B out NET_C12_2 (pre-R19)"])
    cv_in_out(directory)
    if (directory / "cv_in_ac.dat").is_file():
        cv_in_pole(directory)
    else:
        print("\n## CV in bandwidth: no cv_in_ac.dat in this directory (older run)")
    cv_out_stability(directory)
    ref_bias(directory)
    return 0


if __name__ == "__main__":
    sys.exit(main())

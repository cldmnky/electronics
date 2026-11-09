#!/usr/bin/env python3
"""The asserted limits for every froemix analog deck.

Each check carries the requirement it stands for, the measured number and where
the limit comes from (a part value, a datasheet figure or a documented design
rule). A check fails when the measured number leaves the limit -- there is no
"print and continue" path.

These are design checks against behavioural models, not measurements of a
built board. See models.py for model provenance and RESULTS.md for the list of
effects that are NOT modelled.
"""

from __future__ import annotations

from dataclasses import dataclass

import models
import simlib
from simlib import DeckRun


@dataclass
class Check:
    deck: str
    name: str
    measured: str
    limit: str
    ok: bool
    source: str = ""

    def line(self) -> str:
        mark = "PASS" if self.ok else "FAIL"
        return f"  {mark} {self.name:38s} {self.measured:>16s}  [{self.limit}]"


def band(deck: str, name: str, value: float, low: float, high: float, unit: str,
         source: str, fmt: str = "{:.6g}") -> Check:
    ok = low <= value <= high
    measured = (fmt.format(value) + unit) if value is not None else "n/a"
    limit = f"{fmt.format(low)}..{fmt.format(high)}{unit}"
    return Check(deck, name, measured, limit, ok, source)


def gain_of(db_value: float) -> float:
    return 10 ** (db_value / 20.0)


# --------------------------------------------------------------------------
# audio in
# --------------------------------------------------------------------------

def audio_in(run: DeckRun) -> list[Check]:
    deck = "audio_in"
    checks: list[Check] = []
    x, columns = run.dat("audio_in_ac")
    gain_db, stage1_db = columns[0], columns[1]

    checks.append(band(deck, "DC gain at the jack", gain_of(gain_db[0]), 0.0995, 0.1005,
                       "", "R4/R1 x R8/R5 = 0.1000"))
    checks.append(band(deck, "stage 1 DC gain (NET_C2_2)", gain_of(stage1_db[0]),
                       0.995, 1.005, "", "R4/R1 = 1.000"))
    f3 = simlib.minus3db(x, gain_db, "audio_in")
    checks.append(band(deck, "f(-3 dB)", f3 / 1000, 19.0, 20.6, " kHz",
                       "two poles 33.86 kHz and 28.42 kHz -> 19.7 kHz measured",
                       fmt="{:.3f}"))
    checks.append(band(deck, "gain at 20 kHz", gain_of(simlib.value_at(x, gain_db, 20000,
                                                                     "audio_in", log=True)),
                       0.0683, 0.0725, "", "cascade of both 47pF/560pF poles = 0.0704"))
    checks.append(band(deck, "gain at 1 kHz (dB)", run.measure("gain_1k"), -20.11, -19.91,
                       " dB", "0.1000 = -20.00 dB"))
    checks.append(band(deck, "10 Vpk headroom, peak", run.measure("headroom_max"),
                       0.99, 1.01, " V", "0.100 x 10 Vpk, no clipping"))
    checks.append(band(deck, "10 Vpk headroom, trough", run.measure("headroom_min"),
                       -1.01, -0.99, " V", "symmetric, no clipping"))

    t, vectors = run.dat("audio_in_tran")
    distortion = simlib.thd(t, vectors[0], 1000.0, 5e-3, 10e-3)
    checks.append(band(deck, "THD at 10 Vpk, 1 kHz", distortion, 0.0, 1.0, " %",
                       "linear chain: a 1.0 Vpk output is 9 V inside the swing",
                       fmt="{:.4f}"))
    checks.append(band(deck, "gain, 1k source", gain_of(run.measure("gain_src1k")),
                       0.985, 0.995, "", "100k/(100k+1k) = 0.9901"))
    checks.append(band(deck, "gain, 10k source", gain_of(run.measure("gain_src10k")),
                       0.904, 0.914, "", "100k/(100k+10k) = 0.9091"))
    return checks


# --------------------------------------------------------------------------
# audio out
# --------------------------------------------------------------------------

def audio_out(run: DeckRun) -> list[Check]:
    deck = "audio_out"
    checks: list[Check] = []
    x, columns = run.dat("audio_out_ac")
    at_jack_db, pre_db = columns[0], columns[1]

    checks.append(band(deck, "DC gain before R19", gain_of(pre_db[0]), 3.2835, 3.3165,
                       "", "3.3 x 1 = 3.300"))
    checks.append(band(deck, "DC gain at the jack (100k)", gain_of(at_jack_db[0]),
                       3.2510, 3.2836, "", "3.300 x 100k/101k = 3.2673"))
    f3 = simlib.minus3db(x, at_jack_db, "audio_out")
    checks.append(band(deck, "f(-3 dB)", f3 / 1000, 29.0, 32.5, " kHz",
                       "two poles 102.6 kHz and 33.86 kHz -> 30.9 kHz (30.0 with TL074)",
                       fmt="{:.3f}"))
    checks.append(band(deck, "gain at 20 kHz",
                       gain_of(simlib.value_at(x, at_jack_db, 20000, "audio_out", log=True)),
                       2.70, 2.80, "", "2.76 ideal, TL074 lands lower"))
    checks.append(band(deck, "headroom, peak at 1 Vrms in", run.measure("peak_max"),
                       4.55, 4.68, " V", "3.2673 x 1.414 Vpk = 4.62 Vpk"))
    checks.append(band(deck, "headroom, trough at 1 Vrms in", run.measure("peak_min"),
                       -4.68, -4.55, " V", "symmetric"))
    checks.append(band(deck, "old gain-10 case, clamped peak", run.measure("old_max"),
                       10.0, 10.6, " V", "pre-R08 100k feedback must hit the clamp, not "
                       "silently pass"))
    checks.append(band(deck, "old gain-10 case, clamped trough", run.measure("old_min"),
                       -10.6, -10.0, " V", "symmetric clamp"))

    t, vectors = run.dat("audio_out_tran")
    distortion = simlib.thd(t, vectors[0], 1000.0, 5e-3, 10e-3)
    checks.append(band(deck, "THD at 1 Vrms, 1 kHz", distortion, 0.0, 1.0, " %",
                       "linear chain, output 4.6 Vpk inside the swing", fmt="{:.4f}"))

    lx, loads = run.dat("audio_out_load")
    names = ["100k", "50k", "10k", "open"]
    # the case chain holds stage 2 only, so the jack gain is RL/(RL+1k)
    expected = [0.9901, 0.9804, 0.9091, 1.0000]
    for index, (name, want) in enumerate(zip(names, expected)):
        value = gain_of(simlib.value_at(lx, loads[index], 1000, "audio_out load", log=True))
        checks.append(band(deck, f"load {name}: gain at the jack", value,
                           want * 0.993, want * 1.007, "", f"RL/(RL+1k) = {want:.4f}"))
        pre = gain_of(simlib.value_at(lx, loads[index + 4], 1000, "audio_out load", log=True))
        checks.append(band(deck, f"load {name}: gain before R19", pre, 0.995, 1.005, "",
                           "the 1k is outside the loop; the stage-2 output does not move"))
    return checks


# --------------------------------------------------------------------------
# CV in / CV out
# --------------------------------------------------------------------------

def cv_inout(run: DeckRun) -> list[Check]:
    deck = "cv_inout"
    checks: list[Check] = []

    # --- CV in, endpoints from .meas, then the corners from the sweep
    ideal_n5 = run.measure("cvin_at_n5")
    ideal_p5 = run.measure("cvin_at_p5")
    mcp_n5 = run.measure("cvin_mcp_at_n5")
    mcp_p5 = run.measure("cvin_mcp_at_p5")
    checks.append(band(deck, "CV in at -5 V", ideal_n5, 3.1096, 3.1196, " V",
                       "-R33/R30 x -5 V + R33/R34 x 10 V = 3.1146 V"))
    checks.append(band(deck, "CV in at +5 V", ideal_p5, 0.0996, 0.1096, " V",
                       "= 0.1046 V, ~100 mV inside the 3.3 V rail"))
    scale = (ideal_n5 - ideal_p5) / 10.0
    checks.append(band(deck, "CV in scale", scale, 0.3001, 0.3019, " V/V",
                       "R33/R30 = 30.1k/100k = 0.3010", fmt="{:.5f}"))
    checks.append(band(deck, "CV in, fitted MCP6002 at -5 V", mcp_n5, ideal_n5 - 0.01,
                       ideal_n5 + 0.01, " V", "no clipping: 30 mV rail headroom"))
    checks.append(band(deck, "CV in, fitted MCP6002 at +5 V", mcp_p5, ideal_p5 - 0.01,
                       ideal_p5 + 0.01, " V", "no clipping at the low rail"))

    x, columns = run.dat("cv_in_dc")
    corners = ["1%_N5HI", "1%_N5LO", "1%_P5HI", "1%_P5LO",
               "0.1%_N5HI", "0.1%_N5LO", "0.1%_P5HI", "0.1%_P5LO"]
    for index, name in enumerate(corners, start=1):
        high = max(columns[index])
        low = min(columns[index])
        checks.append(band(deck, f"corner {name}: inside the rails", high, 0.0, 3.24, " V",
                           "0.06 V clear of both rails at 1 %; MCP6002 needs 30 mV",
                           fmt="{:.4f}"))
        checks.append(band(deck, f"corner {name}: low endpoint", low, 0.06, 3.24, " V",
                           "worst 1 % corner is 0.0742 V", fmt="{:.4f}"))
        corner_scale = (high - low) / 10.0
        tolerance = 0.021 if name.startswith("1%") else 0.0021
        checks.append(band(deck, f"corner {name}: scale error",
                           abs(corner_scale / 0.3010 - 1) * 100, 0.0,
                           tolerance * 100, " %", "RF/RIN tolerance (2x the part tolerance)",
                           fmt="{:.3f}"))

    checks.append(band(deck, "CV in gain at 10 Hz (dB)", run.measure("cvin_gain_10hz"),
                       -10.48, -10.38, " dB", "0.3010 = -10.43 dB"))
    checks.append(band(deck, "CV in gain at 1 kHz (dB)", run.measure("cvin_gain_1k"),
                       -17.9, -16.0, " dB", "C16 10n with R33 30.1k: pole at 529 Hz"))

    # --- CV out
    out_max = run.measure("cvout_at_max")
    checks.append(band(deck, "CV out at DAC full scale", out_max, 4.975, 5.005, " V",
                       "(49.9/33) x (100/100) x 3.3 V = 4.9899 V", fmt="{:.5f}"))
    checks.append(band(deck, "CV out at DAC 0 V",
                       min(run.dat("cv_out_dc")[1][0]), -0.002, 0.002, " V",
                       "no offset with an ideal op-amp", fmt="{:.5f}"))
    for label, measured in (("open", run.measure("cvout_open_at_max")),
                            ("10k", run.measure("cvout_10k_at_max"))):
        checks.append(band(deck, f"CV out into {label}: load independence",
                           abs(measured - out_max) * 1000, 0.0, 0.2, " mV",
                           "R09: the 1k is inside the loop", fmt="{:.4f}"))

    _, columns = run.dat("cv_out_dc")
    nominal = max(columns[0])
    unbuffered = [max(columns[10]), max(columns[11])]
    for label, value in zip(("13k", "10.3k", "16k"), [max(columns[4])] + unbuffered):
        checks.append(band(deck, f"unbuffered DAC {label}: jack level",
                           abs(value - nominal) * 1000, 0.0, 1.0, " mV",
                           "the follower's input is the only thing the DAC drives",
                           fmt="{:.4f}"))
    no_follower = max(columns[12]) / 3.3
    checks.append(band(deck, "no follower (negative case) scale", no_follower, 1.063, 1.107,
                       " V/V", "49.9k/(33k+13k) = 1.085, i.e. 28 % lost", fmt="{:.4f}"))
    # corners: vectors 5/6 are the 1 % extremes, 7/8 the 0.1 % ones; each
    # vector's full-scale value is its maximum (the sweep starts at 0 V)
    one_percent = [max(columns[index]) for index in (5, 6)]
    tenth_percent = [max(columns[index]) for index in (7, 8)]
    checks.append(band(deck, "1 % corner spread, high", max(one_percent), 5.15, 5.21, " V",
                       "+4.04 % worst case = 5.194 V (a documented finding, not a pass "
                       "claim for the 0..5 V window)", fmt="{:.4f}"))
    checks.append(band(deck, "1 % corner spread, low", min(one_percent), 4.77, 4.83, " V",
                       "4.794 V worst case with 1 % parts", fmt="{:.4f}"))
    checks.append(band(deck, "0.1 % corner spread, high", max(tenth_percent), 4.995, 5.015,
                       " V", "0.1 % parts: +/-0.40 % = 4.970..5.010 V", fmt="{:.4f}"))
    checks.append(band(deck, "0.1 % corner spread, low", min(tenth_percent), 4.965, 4.985,
                       " V", "0.1 % parts: 4.970 V worst case", fmt="{:.4f}"))

    # --- CV out load-capacitance stability (the <=10 nF spec)
    for name, title in (("small", "0.1 V step"), ("full", "3.3 V step")):
        t, vectors = run.dat(f"cv_out_step_{name}")
        for index, load in enumerate(("1nF", "10nF", "100nF")):
            compensated = simlib.step_metrics(t, vectors[index * 2 + 1])
            uncompensated = simlib.step_metrics(t, vectors[index * 2])
            if name == "small":
                checks.append(band(deck, f"{title} {load} without C33/C34: overshoot",
                                   uncompensated["overshoot"], 0.0, 95.0, " %",
                                   "uncompensated loop rings (negative case)",
                                   fmt="{:.1f}"))
            if load in ("1nF", "10nF"):
                checks.append(band(deck, f"{title} {load} with C33/C34: overshoot",
                                   compensated["overshoot"], 0.0, 2.0, " %",
                                   "spec is <=10 nF of cable/load capacitance",
                                   fmt="{:.1f}"))
                checks.append(band(deck, f"{title} {load} with C33/C34: 1 % settling",
                                   compensated["settle"] * 1e6, 0.0, 100.0, " us",
                                   "compensated loop settles inside 100 us",
                                   fmt="{:.0f}"))
    small_t, small = run.dat("cv_out_step_small")
    full_t, full = run.dat("cv_out_step_full")
    checks.append(band(deck, "0.1 V step 100 nF with C33/C34: overshoot",
                       simlib.step_metrics(small_t, small[5])["overshoot"], 0.0, 3.0,
                       " %", "above the 10 nF spec: marginal but stable", fmt="{:.1f}"))
    checks.append(band(deck, "3.3 V step 100 nF (above spec): overshoot",
                       simlib.step_metrics(full_t, full[5])["overshoot"], 30.0, 70.0,
                       " %", "negative case: the current-limited charge through the 1k "
                       "overshoots, so the 10 nF spec is real", fmt="{:.1f}"))
    checks.append(band(deck, "0.1 V step 1 nF without C33/C34: overshoot",
                       simlib.step_metrics(small_t, small[0])["overshoot"], 40.0, 70.0,
                       " %", "negative case: removing C33/C34 is detected", fmt="{:.1f}"))
    return checks


# --------------------------------------------------------------------------
# -10 V reference
# --------------------------------------------------------------------------

def ref_bias(run: DeckRun) -> list[Check]:
    deck = "ref_bias"
    checks: list[Check] = []
    x, columns = run.dat("ref_bias_dc")
    vref, current = columns[0], columns[1]

    at_minus12 = simlib.value_at(x, vref, -12.0, "ref_bias")
    checks.append(band(deck, "V(N10V_REF) at VEE = -12 V", at_minus12, -10.02, -9.98, " V",
                       "LM4040C10 with a 2.2k bias resistor", fmt="{:.5f}"))
    worst = min(abs(value) for value in current)
    checks.append(band(deck, "shunt current, worst low rail", worst * 1e6, 125.0, 1500.0,
                       " uA", "LM4040 minimum 100 uA, the brief asks for 125 uA",
                       fmt="{:.1f}"))
    checks.append(band(deck, "shunt current at VEE = -12 V",
                       abs(simlib.value_at(x, current, -12.0, "ref_bias")) * 1e6,
                       780.0, 830.0, " uA", "(12-10)/2.2k - 2 x 10 V/187k = 802 uA",
                       fmt="{:.1f}"))
    checks.append(band(deck, "extra 22k load: V(N10V_REF)", run.measure("load22_v"),
                       -10.05, -9.90, " V", "0.45 mA extra must stay inside regulation",
                       fmt="{:.4f}"))
    checks.append(band(deck, "extra 22k load: shunt current",
                       abs(run.measure("load22_i")) * 1e6, 125.0, 500.0, " uA",
                       "still above the minimum operating current", fmt="{:.1f}"))
    checks.append(band(deck, "extra 2.2k load (fault): |V(N10V_REF)|",
                       abs(run.measure("overload_v")), 0.0, 9.0, " V",
                       "negative case: a 4.5 mA load breaks regulation", fmt="{:.3f}"))
    checks.append(band(deck, "start-up at 1 ms (VEE still low): |V|",
                       abs(run.measure("start_early")), 0.0, 5.0, " V",
                       "the reference cannot regulate before its bias current exists",
                       fmt="{:.3f}"))
    checks.append(band(deck, "start-up settled by 7 ms", run.measure("start_final"),
                       -10.05, -9.94, " V", "regulation reached after the ramp",
                       fmt="{:.4f}"))
    peak = max(abs(run.measure("start_i_min")), abs(run.measure("start_i_max")))
    checks.append(band(deck, "start-up peak cathode current", peak * 1e3, 0.0, 15.0, " mA",
                       "LM4040 absolute maximum operating current", fmt="{:.3f}"))
    return checks


# --------------------------------------------------------------------------
# sync in / sync out
# --------------------------------------------------------------------------

def sync_io(run: DeckRun) -> list[Check]:
    deck = "sync_io"
    checks: list[Check] = []
    x, columns = run.dat("sync_in_dc")
    base, sync_in, base_noclamp = columns[0], columns[1], columns[2]

    at_minus10 = simlib.value_at(x, base, -10.0, "sync_io")
    at_minus10_free = simlib.value_at(x, base_noclamp, -10.0, "sync_io")
    checks.append(band(deck, "base clamp at -10 V in", at_minus10, -0.75, -0.35, " V",
                       "D5 clamps the base (DESIGN-REVIEW R04)", fmt="{:.4f}"))
    checks.append(band(deck, "base without D5 at -10 V in", at_minus10_free, -9.5, -5.0,
                       " V", "negative case: without D5 the base reaches -9.1 V, outside "
                       "Q1's 6 V emitter-base rating", fmt="{:.4f}"))
    clamp_current = (-10.0 - at_minus10) / 100e3
    checks.append(band(deck, "clamp current through R22", abs(clamp_current) * 1e6, 0.0,
                       200.0, " uA", "100k limits the fault current", fmt="{:.1f}"))
    checks.append(band(deck, "SYNC_IN at +5 V in", simlib.value_at(x, sync_in, 5.0, "sync_io"),
                       -0.01, 0.4, " V", "Q1 saturates, so the ADC pin reads LOW",
                       fmt="{:.4f}"))
    checks.append(band(deck, "SYNC_IN at -5 V in",
                       simlib.value_at(x, sync_in, -5.0, "sync_io"), 3.0, 3.31, " V",
                       "Q1 off, R25 pulls the ADC pin HIGH", fmt="{:.4f}"))
    checks.append(band(deck, "SYNC_IN maximum over a square wave", run.measure("sync_max"),
                       3.0, 3.31, " V", "ADC pin range 0..3.3 V", fmt="{:.4f}"))
    checks.append(band(deck, "SYNC_IN minimum over a square wave", run.measure("sync_min"),
                       -0.01, 0.4, " V", "ADC pin range 0..3.3 V", fmt="{:.4f}"))
    checks.append(band(deck, "SYNC out gain before R29",
                       run.measure("syncout_pre_max") / 3.3, 1.5045, 1.5196, " V/V",
                       "(49.9/33) x (100/100) = 1.5121", fmt="{:.4f}"))
    checks.append(band(deck, "SYNC out gain at the jack",
                       run.measure("syncout_at_max") / 3.3, 1.4865, 1.5076, " V/V",
                       "1.5121 x 100k/101k = 1.4971", fmt="{:.4f}"))
    checks.append(band(deck, "SYNC out at a 3.3 V logic level",
                       run.measure("syncout_at_max"), 4.90, 4.98, " V",
                       "inside the TL074 swing", fmt="{:.4f}"))
    return checks


# --------------------------------------------------------------------------
# CD4051 mux and ADC settling
# --------------------------------------------------------------------------

MUX_CODES = ("code0", "code1", "code2", "code3", "code4", "code5", "code6", "code7")
MUX_DIVIDER = 1e6 / (1e6 + 2500 + 250)


def mux_adc(run: DeckRun) -> list[Check]:
    """Every channel must be selected exactly once, within 2 mV of its input."""
    deck = "mux_adc"
    checks: list[Check] = []
    expected = [0.1 * (number + 1) * MUX_DIVIDER for number in range(8)]
    seen: dict[int, str] = {}
    for code in MUX_CODES:
        measured = run.measure(code)
        nearest = min(range(8), key=lambda n: abs(expected[n] - measured))
        if nearest in seen:
            checks.append(Check(deck, f"{code}: channel decode", f"{measured:.5f} V",
                                "one channel per code",
                                False, f"channel {nearest} was already selected by "
                                f"{seen[nearest]}; the 1-of-8 decode is broken"))
            continue
        seen[nearest] = code
        checks.append(band(deck, f"{code}: selects channel {nearest}", measured,
                           expected[nearest] - 0.002, expected[nearest] + 0.002, " V",
                           f"channel {nearest} at {0.1 * (nearest + 1):.1f} V through "
                           "2.5 k + 250 ohm into 1 Meg", fmt="{:.5f}"))
    checks.append(Check(deck, "all eight channels reached", f"{len(seen)}/8",
                        "8/8", len(seen) == 8,
                        "the scan must visit every channel once; a stuck or shorted "
                        "decode shows up here"))
    checks.append(band(deck, "10k pot: error 10 us after the change",
                       abs(run.measure("pot10k_10us")) * 1000, 0.0, 1.0, " mV",
                       "tau = 2.75 k x 100 pF = 275 ns", fmt="{:.4f}"))
    checks.append(band(deck, "100k pot: error 10 us after the change",
                       abs(run.measure("pot100k_10us")) * 1000, 10.0, 4000.0, " mV",
                       "tau = 25.25 k x 100 pF = 2.5 us: does NOT settle in 10 us",
                       fmt="{:.1f}"))
    checks.append(band(deck, "100k pot: error at 40 us",
                       abs(run.measure("pot100k_40us")) * 1000, 0.0, 1.0, " mV",
                       "the documented limit: a 100k pot needs a longer scan window",
                       fmt="{:.4f}"))
    return checks


# --------------------------------------------------------------------------
# front board
# --------------------------------------------------------------------------

def front_audio(run: DeckRun) -> list[Check]:
    deck = "front_audio"
    checks: list[Check] = []
    t, vectors = run.dat("front_audio", layout="vectors")
    labels = ["output without the LED branch", "output with the removed branch",
              "input without the LED branch", "input with the removed branch"]
    for index, label in enumerate(labels):
        distortion = simlib.thd(t, vectors[index], 1000.0, 0.010, 0.020)
        if index % 2 == 0:
            checks.append(band(deck, f"THD: {label}", distortion, 0.0, 0.02, " %",
                               "the fitted board has no audio jack-LED branch",
                               fmt="{:.4f}"))
        else:
            checks.append(band(deck, f"THD: {label}", distortion, 1.0, 30.0, " %",
                               "negative case: the removed 2.2k/diode/LED branch added "
                               "several percent of THD", fmt="{:.3f}"))
    return checks


def front_cv_led(run: DeckRun) -> list[Check]:
    deck = "front_cv_led"
    checks: list[Check] = []
    t, vectors = run.dat("front_cv_led", layout="vectors")
    bare = run.measure("bare_jack")
    led = run.measure("led_jack")
    checks.append(band(deck, "CV jack without the LED branch", bare, 4.90, 5.10, " V",
                       "-4.99 V through the compensated last stage", fmt="{:.4f}"))
    checks.append(band(deck, "CV jack with the LED branch", led, 4.90, 5.10, " V",
                       "the 2.2k/diode/LED leave the DC level alone", fmt="{:.4f}"))
    checks.append(band(deck, "LED effect on the DC level", abs(led - bare) * 1000, 0.0, 10.0,
                       " mV", "the LED branch draws ~1 mA without shifting the jack",
                       fmt="{:.3f}"))
    diode = simlib.interpolate(t, vectors[3], 0.8e-3, "front_cv_led")
    led_current = (led - diode) / 2200.0
    checks.append(band(deck, "LED current at 5 V", led_current * 1e3, 0.1, 3.0, " mA",
                       "(5 - 0.7 - 2.0)/2.2k = 1.0 mA by design", fmt="{:.3f}"))
    checks.append(band(deck, "100 nF load (above the 10 nF spec): peak",
                       run.measure("over_spec_jack"), 6.0, 8.0, " V",
                       "negative case: above the spec the loop must ring -- this is why "
                       "the spec exists", fmt="{:.4f}"))
    return checks


def front_mode(run: DeckRun) -> list[Check]:
    deck = "front_mode"
    checks: list[Check] = []
    released = run.measure("released")
    pressed = run.measure("pressed")
    checks.append(band(deck, "released MODE node", released, 3.27, 3.31, " V",
                       "10k pull-up (euro BUTTON.pullupR) with 1 uA leakage",
                       fmt="{:.5f}"))
    checks.append(band(deck, "pressed MODE node", pressed, -0.001, 0.01, " V",
                       "switch contact to GND", fmt="{:.6f}"))
    t, vectors = run.dat("front_mode", layout="vectors")
    led_node = simlib.interpolate(t, vectors[5], 10e-6, "front_mode")
    checks.append(band(deck, "LED current when pressed", (3.3 - led_node) / 1000.0 * 1e3,
                       0.2, 3.0, " mA", "1k series resistor (euro BUTTON.seriesR)",
                       fmt="{:.3f}"))
    checks.append(band(deck, "100 pF sample 9 us after connecting",
                       abs(run.measure("sampled") - released) * 1000, 0.0, 10.0, " mV",
                       "250 ohm switch, 100 pF sample capacitor: settles", fmt="{:.3f}"))
    checks.append(band(deck, "released node without R8 (negative case)",
                       run.measure("nopullup"), 0.5, 2.0, " V",
                       "negative case: the LED alone leaves an invalid level -- this is "
                       "why R8 exists", fmt="{:.4f}"))
    return checks


# --------------------------------------------------------------------------
# MIDI OUT -- AO3401A high-side driver (doc/ELECTRICAL-VERIFICATION.md §3/§4)
# --------------------------------------------------------------------------

# case resistor values the currents and powers are computed with; they mirror
# the values the deck passes to TXCASE (source_link.py links them to R58/R59/
# R60/R61 and models.py records the assumptions)
MIDI_TX = {
    "r58": 180.0, "r58_worst": 178.2, "r58_rejected": 99.0,
    "r61": 2200.0, "r59": 33.0, "r59_tol": 33.33, "r59_worst": 32.67,
    "r60": 22.0, "r60_tol": 22.22, "r60_worst": 21.78,
    "rds": 0.085, "rds_hot": 0.1275,
}


def midi_out(run: DeckRun) -> list[Check]:
    deck = "midi_out"
    checks: list[Check] = []
    tx = MIDI_TX

    vgs_on = run.measure("on_gate") - run.measure("on_rail")
    checks.append(band(deck, "VGS(on) at the documented corner", vgs_on, -2.56, -2.49,
                       " V", "doc §3: rail 3.135 V, VOL 0.4 V, R58 180/R61 2.2k -> "
                       "-2.528 V, just inside the -2.5 V RDS(on) point", fmt="{:.4f}"))
    vgs_tol = run.measure("ontol_gate") - run.measure("ontol_rail")
    checks.append(band(deck, "VGS(on) with 1 % resistor extremes", vgs_tol, -2.56, -2.45,
                       " V", "same corner with R58 +1 % and R61 -1 %", fmt="{:.4f}"))
    vgs_main = run.measure("main_gate") - 3.3
    checks.append(band(deck, "VGS(on) on the fitted chain", vgs_main, -2.74, -2.62, " V",
                       "3.3 V rail, VOL 0.4 V, fitted 180/2.2k = -2.681 V", fmt="{:.4f}"))
    vgs_off = run.measure("off_rail") - run.measure("off_gate")
    checks.append(band(deck, "|VGS| off at UART VOH min", vgs_off, 0.33, 0.41, " V",
                       "doc §3: 0.370 V, below the 0.5 V minimum threshold", fmt="{:.4f}"))
    checks.append(band(deck, "|VGS| off with the pin high-Z (reset)",
                       abs(run.measure("reset_rail") - run.measure("reset_gate")), 0.0, 0.01,
                       " V", "R61 holds the gate at the source when the pin is high-Z",
                       fmt="{:.5f}"))
    checks.append(band(deck, "Q2 leakage when off", run.measure("off_tip") / tx["r60"] * 1e6,
                       0.0, 1.0, " uA", "a switch model with ROFF 1e12; the real FET "
                       "leakage is not modelled", fmt="{:.4f}"))
    checks.append(band(deck, "loop current, nominal",
                       run.measure("main_tip") / tx["r60"] * 1e3, 5.94, 6.06, " mA",
                       "doc §3: (3.3-1.65)/(55+220+0.085) = 6.00 mA", fmt="{:.4f}"))
    checks.append(band(deck, "loop current, worst CA-033 receiver",
                       run.measure("ontol_tip") / tx["r60_tol"] * 1e3, 4.25, 4.37, " mA",
                       "doc §3: 4.31 mA with every tolerance", fmt="{:.4f}"))
    checks.append(band(deck, "loop current, worst case vs the 4 mA target",
                       run.measure("ontol_tip") / tx["r60_tol"] * 1e3, 4.0, 6.0, " mA",
                       "the legacy PC900 reference target is 4 mA; CA-033's 5 mA is NOT "
                       "a guaranteed minimum and is not asserted", fmt="{:.4f}"))
    checks.append(band(deck, "loop current, typical into this board's receiver",
                       run.measure("typrx_tip") / tx["r60"] * 1e3, 6.42, 6.85, " mA",
                       "doc §5: 6.54 mA at the 3.465 V rail with VF 1.65 V", fmt="{:.4f}"))

    # the pin has already switched to 0 V, so the current is the gate voltage
    # across R58 (the gate capacitance holds the gate near the rail)
    edge = run.measure("pk_gate")
    checks.append(band(deck, "gate charge peak, fitted R58", edge / tx["r58_worst"] * 1e3,
                       19.0, 19.6, " mA", "doc §3: 3.465/(180x0.99) = 19.44 mA -- the "
                       "value the 180 R was chosen for", fmt="{:.3f}"))
    rejected = run.measure("pk_gate100") / tx["r58_rejected"] * 1e3
    checks.append(band(deck, "gate charge peak with the rejected 100 R (negative)",
                       rejected, 33.8, 35.2, " mA", "doc §3: 34.6 mA with 100 R, above the "
                       "20 mA per-I/O ASSUMPTION -- why 180 R", fmt="{:.3f}"))
    checks.append(band(deck, "gate settled inside one bit", run.measure("pk_gate_settled"),
                       0.24, 0.28, " V", "180 x 645 pF = 116 ns; one bit is 32 us",
                       fmt="{:.4f}"))

    ring_current = run.measure("ringf_drain") / tx["r59_worst"]
    checks.append(band(deck, "ring fault, RDS = 0: current", ring_current * 1e3, 104.0,
                       108.0, " mA", "doc §4: 106.1 mA, zero RDS(on) is the worst case "
                       "for R59", fmt="{:.2f}"))
    checks.append(band(deck, "ring fault, RDS = 0: R59 dissipation",
                       ring_current ** 2 * tx["r59_worst"], 0.355, 0.380, " W",
                       "doc §4: 0.368 W of the 1 W 2512 rating", fmt="{:.4f}"))
    hot_current = run.measure("ringh_drain") / tx["r59_worst"]
    checks.append(band(deck, "ring fault, RDS 127.5 mOhm: R59 dissipation",
                       hot_current ** 2 * tx["r59_worst"], 0.352, 0.377, " W",
                       "doc §4: 0.365 W", fmt="{:.4f}"))
    checks.append(band(deck, "ring fault, RDS 127.5 mOhm: Q2 dissipation",
                       hot_current ** 2 * tx["rds_hot"] * 1e3, 1.2, 1.7, " mW",
                       "doc §4: 1.42 mW -- the FET is not the weak part", fmt="{:.3f}"))
    tip_current = run.measure("tipf_tip") / tx["r60_worst"]
    checks.append(band(deck, "tip forced to the rail: current", tip_current * 1e3, 156.0,
                       162.0, " mA", "doc §4: 159.1 mA, no MOSFET in that leg",
                       fmt="{:.2f}"))
    checks.append(band(deck, "tip forced to the rail: R60 dissipation",
                       tip_current ** 2 * tx["r60_worst"], 0.535, 0.568, " W",
                       "doc §4: 0.551 W of the 1 W rating (a 10 R tip leg would be "
                       "1.20 W and fail)", fmt="{:.4f}"))
    checks.append(band(deck, "1 W part rating is not exceeded in any fault",
                       max(ring_current ** 2 * tx["r59_worst"],
                           tip_current ** 2 * tx["r60_worst"]), 0.0, 1.0, " W",
                       "2512 1 W to 70 C; derating above that is a bench item",
                       fmt="{:.4f}"))
    return checks


# --------------------------------------------------------------------------
# MIDI IN -- TLP2361 receiver (doc/ELECTRICAL-VERIFICATION.md §5)
# --------------------------------------------------------------------------

MIDI_RX = {
    "r47": 220.0, "r47_tol": 222.2, "r47_ca": 231.0, "ith": 1.6e-3,
    "if_recommended_max": 6e-3,
    # 10 mA at 25 C only; the 125 C figure is models.tlp2361_if_abs_max(125)
    "if_abs_max_25c": 10e-3,
    "tlp2362_ith": 5e-3, "tlp2362_recommended": 7.5e-3,
}


def midi_in(run: DeckRun) -> list[Check]:
    deck = "midi_in"
    checks: list[Check] = []
    rx = MIDI_RX

    own = (run.measure("own_ring") - run.measure("own_anode")) / rx["r47_tol"] * 1e3
    checks.append(band(deck, "own transmitter, worst case", own, 5.20, 5.45, " mA",
                       "doc §5: 5.34 mA = (3.135-1.65)/(55x1.01+220x1.01+0.1275)",
                       fmt="{:.4f}"))
    checks.append(band(deck, "threshold margin over IFHL 1.6 mA", own / (rx["ith"] * 1e3),
                       3.0, 4.0, " x", "doc §5: 3.34x the 125 C maximum threshold",
                       fmt="{:.3f}"))
    typical = (run.measure("typ_ring") - run.measure("typ_anode")) / rx["r47"] * 1e3
    checks.append(band(deck, "typical loop at the high rail", typical, 6.42, 6.85, " mA",
                       "doc §5: 6.54 mA, inside the 6 mA recommended point",
                       fmt="{:.4f}"))
    derated = models.tlp2361_if_abs_max(125.0) * 1e3
    checks.append(band(deck, "typical loop vs the 125 C derated maximum", typical, 0.0,
                       derated, " mA", "datasheet page 3 absolute maximum derated to "
                       "125 C = 8.05 mA; the 2...6 mA recommended point is a separate, "
                       "softer limit and is exceeded -- pending bench item (doc §7.3)",
                       fmt="{:.4f}"))
    ca033 = (run.measure("ca_ring") - run.measure("ca_anode")) / rx["r47_ca"] * 1e3
    checks.append(band(deck, "CA-033 receiver corner", ca033, 4.19, 4.39, " mA",
                       "doc §5: 4.31 mA, the same number midi_out.cir delivers",
                       fmt="{:.4f}"))
    five = (run.measure("v5_ring") - run.measure("v5_anode")) / rx["r47"] * 1e3
    checks.append(band(deck, "5 V legacy transmitter (2 x 220 R)", five, 5.50, 5.77, " mA",
                       "doc §5: 5.63 mA at 5.25 V and 418 ohm", fmt="{:.4f}"))
    noncompliant = (run.measure("leg_ring") - run.measure("leg_anode")) / rx["r47"] * 1e3
    checks.append(band(deck, "non-compliant 5 V transmitter (33/10 legs)", noncompliant,
                       14.3, 15.2, " mA", "doc §5/§7.4: 14.8 mA -- above the 25 C 10 mA "
                       "absolute maximum and above the 125 C 8.05 mA figure; nothing on "
                       "this board clamps it", fmt="{:.4f}"))
    checks.append(band(deck, "non-compliant source exceeds the 25 C absolute maximum",
                       noncompliant, rx["if_abs_max_25c"] * 1e3, 30.0, " mA",
                       "negative case: this deck must show the pending overstress, not "
                       "hide it (it also exceeds the derated 125 C figure)",
                       fmt="{:.4f}"))

    reverse = run.measure("rev_anode") - run.measure("rev_cathode")
    checks.append(band(deck, "reverse polarity: LED reverse voltage", abs(reverse), 0.30,
                       0.80, " V", "D1 conducts instead of the LED", fmt="{:.4f}"))
    reverse_current = (run.measure("rev_ring") - run.measure("rev_anode")) / rx["r47_tol"]
    checks.append(band(deck, "reverse polarity: current through D1",
                       abs(reverse_current) * 1e3, 0.0, 12.0, " mA",
                       "R47 plus the sender's own resistance limit it; D1 is a 150 mA part",
                       fmt="{:.3f}"))
    checks.append(band(deck, "idle loop: VO stays high (fail-safe)", run.measure("idle_vo"),
                       3.2, 3.31, " V", "R48 2.2k pull-up; TLP2361 VO high with no current",
                       fmt="{:.4f}"))
    checks.append(band(deck, "receiving: VO is low", run.measure("main_vo"), 0.0, 0.4, " V",
                       "TLP2361 is inverting: current on -> start bit -> VO low",
                       fmt="{:.4f}"))
    checks.append(band(deck, "LED forward drop at the fitted corner",
                       run.measure("main_anode") - run.measure("main_cathode"), 1.62, 1.69,
                       " V", "VF 1.65 V (datasheet maximum at 2 mA) plus the model's "
                       "diode and sense drops", fmt="{:.4f}"))
    # --- the weakest and strongest LEGAL sender loops (models.MIDI_SENDERS).
    # The receiver path (R47, LED, D1) is the fitted board; the sender is the
    # other device and is an explicit assumption.
    prefixes = {"ca5v_lo": "ca5lo", "ca5v_hi": "ca5hi",
                "ca33_lo": "ca33lo", "ca33_hi": "ca33hi"}
    for corner, (sender, direction, loss, vf, reference) in models.MIDI_SENDER_CORNERS.items():
        params = models.midi_sender_case_parameters(corner)
        prefix = prefixes[corner]
        measured = (run.measure(f"{prefix}_ring") - run.measure(f"{prefix}_anode")) \
            / params["R47"]
        band_width = reference * 0.002
        checks.append(band(deck, f"sender {corner}: loop current (reference)",
                           measured, reference - band_width, reference + band_width, " A",
                           f"models.midi_sender_current({sender!r}, {direction!r}, "
                           f"{loss} V driver loss, VF {vf} V) = {reference * 1e3:.4f} mA; "
                           "0.2 % band so a changed tolerance or bound cannot pass",
                           fmt="{:.6f}"))
    weak = {}
    for corner in ("ca5v_lo", "ca33_lo"):
        params = models.midi_sender_case_parameters(corner)
        prefix = prefixes[corner]
        weak[corner] = (run.measure(f"{prefix}_ring") - run.measure(f"{prefix}_anode")) \
            / params["R47"]
        checks.append(band(deck, f"sender {corner}: TLP2361 switches at 125 C",
                           weak[corner] / rx["ith"], 1.5, 10.0, " x",
                           "IFHL maximum 1.6 mA at 125 C; the weakest legal loop must "
                           "exceed it with margin", fmt="{:.3f}"))
        checks.append(band(deck, f"sender {corner}: old TLP2362 does NOT switch "
                                 "(negative)", weak[corner], 0.0, rx["tlp2362_ith"], " A",
                           "the replaced part needs <= 5 mA; the weakest legal loop is "
                           "below it -- the guaranteed-margin defect", fmt="{:.6f}"))
    strong = {}
    for corner in ("ca5v_hi", "ca33_hi"):
        params = models.midi_sender_case_parameters(corner)
        prefix = prefixes[corner]
        strong[corner] = (run.measure(f"{prefix}_ring")
                          - run.measure(f"{prefix}_anode")) / params["R47"]

    # TWO limits with primary provenance: 10 mA at 25 C (datasheet page 3) and
    # 8.05 mA at 125 C from the manufacturer's own -0.13 mA/C derating above
    # 110 C. The recommended 2...6 mA window is a third, separate limit.
    checks.append(band(deck, "sender ca5v_hi: inside the 125 C derated maximum",
                       strong["ca5v_hi"], 0.0, derated * 1e-3, " A",
                       "6.53 mA is inside 8.05 mA (models.tlp2361_if_abs_max(125))",
                       fmt="{:.6f}"))
    checks.append(band(deck, "sender ca33_hi: inside the 25 C absolute maximum",
                       strong["ca33_hi"], 0.0, models.TLP2361_IF_ABS_MAX_25C_A, " A",
                       "8.18 mA is inside the 10 mA 25 C figure", fmt="{:.6f}"))
    checks.append(band(deck, "sender ca33_hi: EXCEEDS the 125 C derated maximum "
                             "(negative, open risk)",
                       strong["ca33_hi"], derated * 1e-3, 0.020, " A",
                       "8.18 mA is outside 8.05 mA: an unresolved high-current/"
                       "temperature risk, not a pass -- models.tlp2361_if_abs_max(125)",
                       fmt="{:.6f}"))
    checks.append(band(deck, "sender ca5v_hi: above the 6 mA recommended current "
                             "(open item)",
                       strong["ca5v_hi"], 6.0e-3, 0.020, " A",
                       "recommended 2...6 mA is separate from the absolute maximum; the "
                       "strong legal loops exceed it (bench item, doc §7.3)",
                       fmt="{:.6f}"))

    # ILLUSTRATION ONLY: VF 1.15 V = the 25 C minimum plus the TYPICAL -2 mV/C
    # coefficient over 100 C. The datasheet gives no hot VF bound, so this shows
    # the trend and is not a guaranteed corner.
    params = models.midi_sender_case_parameters("ca33_hi")
    hot = (run.measure("hot_ring") - run.measure("hot_anode")) / params["R47"]
    hot_reference = models.midi_sender_current("ca033_33", "hi", 0.0,
                                               models.tlp2361_vf_illustrative(125.0))
    checks.append(band(deck, "illustrative hot VF 1.15 V: loop current", hot,
                       hot_reference * 0.997, hot_reference * 1.003, " A",
                       "typical-coefficient ASSUMPTION, not a guaranteed corner: 8.95 mA",
                       fmt="{:.6f}"))
    checks.append(band(deck, "illustrative hot VF: outside the 125 C derated maximum",
                       hot, derated * 1e-3, 0.020, " A",
                       "the trend moves further outside the derated limit; this proves "
                       "nothing about LED safety over temperature", fmt="{:.6f}"))
    checks.append(band(deck, "illustrative hot VF: inside the 25 C absolute maximum",
                       hot, 0.0, models.TLP2361_IF_ABS_MAX_25C_A, " A",
                       "still inside the 25 C figure, which is why one limit is not "
                       "enough", fmt="{:.6f}"))
    return checks


DECK_CHECKS = {
    "midi_in": midi_in,
    "midi_out": midi_out,
    "audio_in": audio_in,
    "audio_out": audio_out,
    "cv_inout": cv_inout,
    "ref_bias": ref_bias,
    "sync_io": sync_io,
    "mux_adc": mux_adc,
    "front_audio": front_audio,
    "front_cv_led": front_cv_led,
    "front_mode": front_mode,
}

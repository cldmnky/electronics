#!/usr/bin/env python3
"""Clock-jitter statistics for midi_seqtap CSV captures.

Reads the CSV written by tap/midi_seqtap.c (same CLOCK_MONOTONIC time base for
every source) and separates two populations:

  emission  clock bytes written by the mnkybox app's own sequencer ports
            (client >= 128) - the Pi-side schedule, no wire involved;
  arrival   clock bytes read back from a kernel/hardware port (client < 128),
            i.e. bytes that crossed a USB cable and came back on a real MIDI
            wire.

For each source it reports the inter-byte interval distribution against the
nominal 120 BPM / 24 ppqn interval, and for every emission->arrival pair it
reports the pass-through delay, which is the only number in this file that
contains the USB mirror jitter plus cable/device latency.

usage: clock_jitter.py <capture.csv> [--json out.json]
"""
import argparse
import bisect
import json
import statistics
import sys
from collections import Counter, defaultdict

CLOCK = "f8"
NOMINAL_US = 60.0e6 / (120 * 24)  # 120.00 BPM, 24 ppqn -> 20833.33 us
GAP_US = 1_000_000  # an interval this long is a parked transport, not a late byte
APP_CLIENT_MIN = 128


def pct(values, p):
    if not values:
        return None
    ordered = sorted(values)
    k = (len(ordered) - 1) * (p / 100.0)
    lo = int(k)
    hi = min(lo + 1, len(ordered) - 1)
    return ordered[lo] + (ordered[hi] - ordered[lo]) * (k - lo)


def stats(values):
    if not values:
        return {"n": 0}
    return {
        "n": len(values),
        "min": min(values),
        "avg": statistics.fmean(values),
        "p50": pct(values, 50),
        "p95": pct(values, 95),
        "p99": pct(values, 99),
        "max": max(values),
        "stdev": statistics.pstdev(values) if len(values) > 1 else 0.0,
    }


def load(path):
    rows = []
    with open(path, encoding="ascii") as handle:
        header = handle.readline().strip().split(",")
        for line in handle:
            parts = line.rstrip("\n").split(",")
            if len(parts) != len(header):
                continue
            row = dict(zip(header, parts))
            row["mono_us"] = int(row["mono_us"])
            row["bytes"] = row["hex"].split() if row["hex"] else []
            rows.append(row)
    return rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("csv")
    ap.add_argument("--json")
    ap.add_argument("--label", default="")
    args = ap.parse_args()

    rows = load(args.csv)
    if not rows:
        print("no events captured", file=sys.stderr)
        return 1
    t0 = min(r["mono_us"] for r in rows)
    span_s = (max(r["mono_us"] for r in rows) - t0) / 1e6

    by_src = defaultdict(list)
    bytes_by_src = Counter()
    for row in rows:
        src = row["src"]
        by_src[src].append(row)
        for b in row["bytes"]:
            bytes_by_src[(src, b)] += 1

    emit_srcs = sorted(s for s in by_src if int(s.split(":")[0]) >= APP_CLIENT_MIN)
    recv_srcs = sorted(s for s in by_src if int(s.split(":")[0]) < APP_CLIENT_MIN)

    report = {
        "capture": args.csv,
        "label": args.label,
        "span_s": round(span_s, 3),
        "rows": len(rows),
        "nominal_interval_us": NOMINAL_US,
        "sources": {},
        "clock": {},
        "pairs": [],
    }

    print(f"capture {args.csv}: {len(rows)} events over {span_s:.1f} s")
    print("\nper-source byte histogram (top 4):")
    for src in sorted(by_src, key=lambda s: (int(s.split(":")[0]), s)):
        top = [(b, n) for (s, b), n in bytes_by_src.items() if s == src]
        top.sort(key=lambda x: -x[1])
        hist = " ".join(f"{b}x{n}" for b, n in top[:4])
        kind = "app" if int(src.split(":")[0]) >= APP_CLIENT_MIN else "hw"
        print(f"  {src:>8} {kind} n={len(by_src[src]):6d}  {hist}")
        report["sources"][src] = {"kind": kind, "n": len(by_src[src]), "bytes": dict(top)}

    # Pure clock arrival/emission streams per source.
    clock_times = {}
    for src, src_rows in by_src.items():
        times = [r["mono_us"] for r in src_rows if CLOCK in r["bytes"]]
        if len(times) >= 3:
            clock_times[src] = times

    for src in sorted(clock_times, key=lambda s: (int(s.split(":")[0]), s)):
        times = clock_times[src]
        all_deltas = [b - a for a, b in zip(times, times[1:])]
        # A transport STOP silences the mirror for as long as the transport is
        # parked; those are gaps, not jitter, so they are reported separately and
        # kept out of the interval distribution.
        gaps = [d for d in all_deltas if d > GAP_US]
        deltas = [d for d in all_deltas if d <= GAP_US]
        if not deltas:
            continue
        median_interval = statistics.median(deltas)
        kind = "app-emit" if int(src.split(":")[0]) >= APP_CLIENT_MIN else "hw-arrival"
        entry = {
            "kind": kind,
            "clock_n": len(times),
            "clock_rate_per_s": round(len(times) / span_s, 3),
            "interval_us": stats(deltas),
            "abs_deviation_from_nominal_us": stats([abs(d - NOMINAL_US) for d in deltas]),
            "period_jitter_us": stats([d - median_interval for d in deltas]),
            "gaps_over_1s": [{"at_s": round(v / 1e6, 3), "len_s": round(g / 1e6, 3)}
                             for g, v in [(g, times[i + 1]) for i, g in
                                          enumerate(all_deltas) if g > GAP_US]],
        }
        report["clock"][src] = entry
        iv = entry["interval_us"]
        print(
            f"\n{src} ({kind}) clock: n={len(times)} rate={entry['clock_rate_per_s']}/s "
            f"interval min/avg/p95/max = {iv['min']:.0f}/{iv['avg']:.0f}/"
            f"{iv['p95']:.0f}/{iv['max']:.0f} us  (nominal {NOMINAL_US:.0f})"
        )
        pj = entry["period_jitter_us"]
        ad = entry["abs_deviation_from_nominal_us"]
        print(
            f"   period jitter vs median: avg={pj['avg']:.1f} p95={pj['p95']:.0f} "
            f"max={pj['max']:.0f} us stdev={pj['stdev']:.1f}"
        )
        print(
            f"   |interval-nominal|: avg={ad['avg']:.1f} p95={ad['p95']:.0f} "
            f"p99={ad['p99']:.0f} max={ad['max']:.0f} us"
        )
        for gap in entry["gaps_over_1s"]:
            print(f"   gap: {gap['len_s']} s ending at t+{gap['at_s']:.1f} s")

    # Burst analysis. The app fans one clock byte out to every output port in a
    # single pass, so one scheduling event shows up as a burst of near-
    # simultaneous app-port events followed (over a real cable path) by arrivals
    # at kernel ports. Grouping by a 5 ms gap separates the two populations
    # without assuming which output port feeds which input cable.
    bursts = []
    current = []
    for row in sorted(rows, key=lambda r: r["mono_us"]):
        if CLOCK not in row["bytes"]:
            continue
        if current and row["mono_us"] - current[-1]["mono_us"] > 5000:
            bursts.append(current)
            current = []
        current.append(row)
    if current:
        bursts.append(current)

    spreads = []
    delays = defaultdict(list)
    burst_starts = []
    for burst in bursts:
        app_times = [r["mono_us"] for r in burst if int(r["src"].split(":")[0]) >= APP_CLIENT_MIN]
        if not app_times:
            continue
        start = min(app_times)
        burst_starts.append(start)
        spreads.append(max(app_times) - start)
    burst_starts.sort()
    # Every arrival is attributed to the latest burst that started before it and
    # less than 15 ms earlier (three quarters of one clock interval), so a cable
    # path of a few ms stays unambiguous.
    for burst in bursts:
        for row in burst:
            client = int(row["src"].split(":")[0])
            if client >= APP_CLIENT_MIN:
                continue
            t = row["mono_us"]
            i = bisect.bisect_left(burst_starts, t)
            candidates = [burst_starts[j] for j in (i - 1, i) if 0 <= j < len(burst_starts)]
            if not candidates:
                continue
            start = min(candidates, key=lambda s: abs(t - s))
            delta = t - start
            if -2000 <= delta <= 15000:
                delays[row["src"]].append(delta)

    report["bursts"] = {
        "n": len(bursts),
        "app_fanout_spread_us": stats(spreads),
        "arrival_delay_us": {s: stats(v) for s, v in delays.items()},
    }
    print(f"\nbursts: {len(bursts)} clock bursts in {span_s:.1f} s")
    bs = report["bursts"]["app_fanout_spread_us"]
    if bs["n"]:
        print(
            f"  app fan-out spread (13 ports, same byte): min/avg/p95/max = "
            f"{bs['min']:.0f}/{bs['avg']:.0f}/{bs['p95']:.0f}/{bs['max']:.0f} us"
        )
    for src in sorted(delays, key=lambda s: (int(s.split(":")[0]), s)):
        ds = report["bursts"]["arrival_delay_us"][src]
        print(
            f"  arrival {src:>7}: n={ds['n']} delay-from-burst-start "
            f"min/avg/p95/max = {ds['min']:.0f}/{ds['avg']:.0f}/{ds['p95']:.0f}/{ds['max']:.0f} us"
        )

    # Whole-capture rate accuracy: the acceptance item is about timing error, so
    # report the mean interval against the nominal one as well.
    tempo_err = []
    for src, entry in report["clock"].items():
        iv = entry.get("interval_us", {})
        if iv.get("n", 0) > 100 and "avg" in iv:
            err = (iv["avg"] - NOMINAL_US) / NOMINAL_US * 100.0
            entry["tempo_error_percent"] = err
            tempo_err.append((src, err))
    if tempo_err:
        worst = max(tempo_err, key=lambda kv: abs(kv[1]))
        print(
            f"\nmean-interval tempo error: worst source {worst[0]} {worst[1]:+.4f} % "
            f"({len(tempo_err)} clock sources, nominal {NOMINAL_US:.2f} us)"
        )

    if args.json:
        with open(args.json, "w", encoding="utf-8") as handle:
            json.dump(report, handle, indent=2)
        print(f"\nwrote {args.json}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

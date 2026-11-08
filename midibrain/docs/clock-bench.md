# MIDI-clock bench: TRS vs USB acceptance criteria

Status: **proposed targets, unmeasured.** This page is the bench session's
checklist for the current Pico firmware (`firmware/README.md`, acceptance rows
1-17) and the Pi-side USB reconstruction path. It does not record a measurement
and it does not approve hardware. `make release` stays gated on
`hardware-acceptance.json`.

## Two different clocks, two different row sets

The product emits clock on two physically different paths. They fail for
different reasons, so a single "clock is good" number is not an acceptance
result.

| Path | What it is | Where it is measured |
|---|---|---|
| **TRS clock** | The Pico's own physical UART outputs (OUT1/OUT2, 24 PPQN, `0xF8`). The Pico's deadline grid and priority TX own this path; the Pi is not in the loop. | Logic analyser probes on both TRS output jacks, during a `--bench-clock` run. |
| **USB clock** | The Pi-side reconstruction: Pico tick frames over SPI -> `UsbClock` discipline -> ALSA/USB MIDI OUT to the DAW. The Pi owns scheduling, so app/DAW/UI load and the 1 ms wake-up path matter here. | Read-only ALSA-sequencer tap (`pi/bench/tap/midi_seqtap.c`) on the `mnkybox` output ports, same run. |

TRS rows use the firmware README's numbering (1-17). USB rows are a **separate
U-row set**; they are not TRS rows and must never be reported as passing a TRS
target.

## TRS row set (Pico physical outputs) - targets from firmware/README.md

These are the review's rows 1-17, restated here so the bench session and the
logic analyser work from one page.

| # | Check | Target |
|---|---|---|
| 1 | Tempo accuracy | <= 0.1 % error over the row window |
| 2 | Clock timing error | <= 1 ms max edge error vs the exact 24-PPQN grid, both outputs |
| 3 | Dropped clock | 0 dropped `F8` on either output |
| 4 | Soak under load | clean for the full profile window at every tempo |
| 5 | TX priority | `F8` overtakes queued note bytes; realtime interleaves mid-message |
| 6 | Overflow safety | no partial message, no hung note |
| 7 | Transport placement | FA/FB/FC once, in order, not delayed by clock backlog |
| 8 | SPI aborts recovered | 0 watchdog restarts; `spiabort` and `spiok` both advance, clock keeps running |
| 9 | Clock under SPI faults | 0 dropped `F8` while the SPI stream is broken |
| 10 | App-message loss recovery | 0 lost Note Offs; `retries` bounded; exactly-once delivery |
| 11-16 | External follow | lock, steady state, jitter, wander, holdover, smooth resync (see firmware README for the simulated values) |
| 17 | Clocked FX timing parity | FX event released at its tick grid deadline, never early; `F8` always first; no FX dropped |

## U-row set (Pi USB reconstruction) - PROPOSED, UNMEASURED

Defined here as the separate Pi-side row set. Measure all U rows in the same
bench session as the TRS rows, under **FULL DAW load** (audio engine running,
CUDA-free NVMe recording, UI churn, SPI traffic, note/CC load). "FULL load" is
part of the criterion: an idle USB clock number is not the product case.

| # | Check | Proposed target under FULL DAW load | Method |
|---|---|---|---|
| U1 | USB clock tempo accuracy | mean interval error <= 0.1 % over the row window, per tempo | tap interval mean vs 60/(24*bpm) |
| U2 | USB clock jitter | \|interval - nominal\| p95 <= 0.5 ms, max <= 2.0 ms | tap per-port interval distribution |
| U3 | USB clock drops | 0 missing `F8` in the app-emit stream; `clockSkipped == 0` | tap count vs expected ticks; app status |
| U4 | Transport latency | START/STOP/CONTINUE reaches the USB port <= 2 ms after the Pico transport event; never behind more than one clock byte | tap event order/timestamps |
| U5 | Link health during the run | `noteOffDrops == 0`, `overruns == 0`, `retries` bounded by the injected abort rate, no watchdog restart | `[stat]` before/after + app diagnostics |
| U6 | Clocked-FX parity on USB | an FX event generated for tick N never precedes tick N's `F8`, and its late bound stays <= 1 ms + one MIDI byte | tap ordering at the app output |

U2's 0.5 ms p95 is deliberately tighter than the existing measured no-Pico
1 ms-poll result (p95 0.97-1.24 ms) because the Linux free-run path now has a
fine timer thread; the row exists to keep that improvement honest. U4 and U6
are ordering/placement rows, not jitter rows.

## How to measure (same session, both paths)

1. **Stop the app.** `--bench-clock` opens the real SPI link and audio device;
   the script refuses if another `mnkybox` is running.
2. **Start the analyser first.** Probe TRS OUT1 and OUT2 on the Pico's UART TX
   pins/jacks (24 PPQN, 31.25 kbaud), sample at >= 1 MHz. Record the analyser
   host wall clock with `date +%s.%N` and note the capture start in the bench
   log. Per-tempo windows are minutes (quick) or 2 h (full), so a few ms of
   host-clock alignment is irrelevant.
3. **Run the driver** (see `pi/bench/README.md`):
   `sh pi/bench/run/06-clock-acceptance.sh quick` (180 s per tempo) or `full`
   (2 h per tempo, owner's session). It prints
   `[bench] MARK iso=... mono_us=... event=tempo.begin|tempo.measure.start|tempo.end|run.start|run.end`
   for every tempo change. The analyser session splits its capture at those
   wall-clock offsets; `mono_us` is this host's steady clock and is only an
   alignment aid, never a wire measurement.
4. **Collect the app side.** In the same run the driver samples
   `LinkClient::status()` every 50 ms: tick count, Pico-stamp arrival lateness,
   `clockSkipped`, `bad`, `retries`, `noteOffDrops`, `overruns`. The results
   table prints one row per tempo and output, shaped like the `clock_sim`
   matrix (the app-observed columns are shared by both outputs until the
   analyser edge data is folded in).
5. **Collect the firmware side.** The driver opens `/dev/ttyACM*` (or
   `MNKYBOX_PICO_CONSOLE`) and parses the 1 Hz `[stat]` line before/after every
   tempo. The log carries `[bench] stat.delta spiok=... spiabort=...` lines;
   these feed TRS rows 3, 8, 9 and U5.
6. **Collect the USB side.** Run the read-only tap for the same window
   (`sh pi/bench/run/01-tap-clock.sh <seconds>` or the long form) on the
   `mnkybox` output ports. Use `tap/clock_jitter.py` for U1/U2 and the raw CSV
   for U3/U4/U6 ordering. The tap never writes a port and leaves no client.
7. **Fold the two captures into one row per (tempo, output).** TRS edge numbers
   come from the analyser; USB numbers from the tap; link/app numbers from the
   driver log. Do not copy a number from one path into the other.

## Recording

A bench result names the profile, the tempo list, the capture lengths, the
analyser sample rate and the tap window. Every value carries a unit. Until a
run is recorded here and in `docs/evidence/`, every row above stays
**unmeasured**.

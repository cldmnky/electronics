# mnkybox Pico firmware (RP2350 / Pico 2)

`pico/` holds the on-target firmware plus host-testable shared headers.
`protocol.hpp` (framing/CRC) and `midi.hpp` (parser, event codecs) have no Pico
SDK dependency; `protocol_test.cpp` runs them on the host under `make test`.
The clock grid and MIDI TX priority queue are `clock_scheduler.hpp` and
`midi_tx_scheduler.hpp` (also pure logic, same host test). `main.cpp` is the
RP2350 application; `pins.h` is generated from
`tscircuit/lib/pin-map.ts` (never hand-edit it).

## Toolchain setup (once per machine)

```sh
# ARM toolchain + Pico SDK live outside the repo:
mkdir -p ~/pico && cd ~/pico
curl -sSL -o toolchain.tar.xz \
  "https://developer.arm.com/-/media/Files/downloads/gnu/13.3.rel1/binrel/arm-gnu-toolchain-13.3.rel1-darwin-arm64-arm-none-eabi.tar.xz"
tar xf toolchain.tar.xz
git clone --depth 1 https://github.com/raspberrypi/pico-sdk.git
(cd pico-sdk && git submodule update --init --depth 1 lib/tinyusb)  # USB console
```

## Build

```sh
export PICO_SDK_PATH=$HOME/pico/pico-sdk
export PATH=$HOME/pico/arm-gnu-toolchain-*/bin:$PATH
# from midibrain/:
make firmware   # -> firmware/pico/build/midibrain.uf2
```

`make test` does **not** build firmware (SDK-dependent); it runs the host
codec/parser tests, the `pins.h` freshness check, the hardware gates and SPICE.

## Flash

1. Hold BOOTSEL, plug in Pico USB (or tap reset with BOOTSEL held).
2. Copy `firmware/pico/build/midibrain.uf2` onto the `RP2350` drive (Pico 2 shows `RP2350`, not `RPI-RP2`).
3. Debug console: USB serial (115200), boot banner + 1 Hz `[stat]` lines.

## What the firmware owns

- Both MIDI UARTs at 31250 baud (IRQ rings with per-byte timestamps, parser
  with running status + realtime interleave; SysEx skipped in v1).
- Debounced panel inputs (1 kHz scan), quadrature tempo encoder (SHIFT = fine),
  three semantic LEDs (armed-blink / recording-solid / play-on-roll, Pi-overridable
  via LED_SET with arm sync on mask bit 7).
- Always-on local MIDI clock (24 ppq from boot at the current tempo,
  START/CONTINUE/STOP bytes on both UARTs) driven by encoder tempo or Pi
  SET_TEMPO, plus timestamped tick frames per tick and transport-state
  announcements so the Pi can mirror phase and rolling state to USB.
  Panel PLAY/PLAY_START/STOP drive transport locally (no Pi round-trip).
- SPI0 slave @1 MHz to the Pi: IRQ-driven 32-byte exchanges (CSn edge + SPI
  RX interrupt, preloaded MISO response), HELLO/ACK handshake with
  ROSC-derived session, per-frame app ACKs, sequence dedup, READY pin, 8 s
  watchdog. The main loop never blocks on the master.

## Clock design (deadline grid)

- The 24-PPQN grid is absolute. A tick at 120.00 bpm is 20833.333... us, so
  `clock_scheduler.hpp` carries the period as an exact rational number of
  nanoseconds: `250'000'000'000 / bpm_x100` ns, with the division remainder in
  a phase accumulator. The grid arithmetic carries no rounded microsecond
  interval; only the final alarm deadline is rounded up to the 1 MHz timer.
- Every next pulse is computed from the PREVIOUS SCHEDULED deadline, never
  from `now` or from an alarm callback's return. `main.cpp` re-arms with
  `add_alarm_at(from_us_since_boot(nextDeadlineUs()))`; `nextDeadlineUs()` is
  the nanosecond grid rounded UP to the 1 MHz alarm tick. ISR entry latency and
  callback execution time therefore delay one edge but do not accumulate as
  tempo drift.
- A tempo change rebases the period at the pending deadline: the already
  scheduled tick is not moved or dropped, and later ticks use the new exact
  rational period from that point. External-clock follow supplies its smoothed
  period and phase through the same absolute grid.
- External follow is a phase-locked loop over that grid. The interval EMA
  (1/8) stays the frequency estimator; each accepted F8 also measures phase
  against the pending deadline. The proportional term rebases the pending
  deadline by 0.25 of the phase error per tick, clamped to 200 us and to one
  slew step per generated tick (two input edges may fall between output ticks
  under jitter). The integral term trims the period by 1/128 of the low-passed
  phase error per tick, clamped to 0.1 %, so it can never be audible. A lost
  source coasts at the last locked period; on return the phase error is slewed
  out across ticks, never re-anchored to an input edge (no time-skip). Follow
  never gates or stops the clock; `tempoDiv`, `syncDelayMs` and the
  EV_CLOCK_ROUTE policy are unchanged.
- If the callback runs past the next grid point, `skipToAfter()` steps over
  whole missed ticks (bounded to 4096) instead of emitting a catch-up burst.
  The grid phase is unchanged; skipped ticks are counted in `[stat] miss=`.
- Transport semantics are unchanged: the Pico always sends clock; START (FA)
  resets the tick index, CONTINUE/STOP (FB/FC) and START are emitted at the
  event instant on the realtime priority class, and the per-output clock gate,
  `tempoDiv` and `syncDelayMs` policy is untouched. Transport bytes bypass the
  clock delay line and divider, as before.

## MIDI TX design (non-blocking priority queue)

- `midi_tx_scheduler.hpp` is one bounded queue pair per TRS output. The clock
  alarm only enqueues; it never calls a blocking UART function. The UART TX
  interrupt writes the data register directly while the hardware reports FIFO
  space (never a blocking UART helper), then disables the TX interrupt when the
  queue is empty.
- Priority classes: realtime (0xF8 CLOCK, 0xFA START, 0xFB CONTINUE, 0xFC STOP)
  in a dedicated single-byte queue, and normal 1..3-byte messages in complete
  message slots. A realtime byte is selected before every normal-message byte,
  so it can interleave BETWEEN the bytes of a longer message (legal MIDI 1.0,
  and required so clock is not stuck behind a 3-byte note).
- Both queues are preallocated: 32 normal messages and 96 realtime bytes per
  output (the realtime depth covers the full 500 ms sync-delay line at 300 bpm),
  no heap in the hot path. Overflow never emits a partial message. A
  normal enqueue that finds the queue full drops the OLDEST queued message that
  has not started transmitting and is not a Note Off (0x8n or 0x9n velocity 0).
  If every queued message is a Note Off (or already in flight), the new message
  is refused instead; a queued Note Off is never sacrificed. Realtime overflow
  drops the new realtime byte and counts it.
- Clocked MIDI FX output (arp, repeater, LFO, delay taps, generator chains) is
  TIMESTAMPED, not emitted at the main loop's convenience. The route engines
  run in the main loop, but every event they generate carries the absolute grid
  deadline (`clock_tick_deadline_ns`) of the tick being serviced and enters
  `MidiTxScheduler::enqueueAt()`. The scheduler owns a third bounded queue (32
  deadline-ordered messages per output, no heap) and a grid-time pump:
  `releaseDue(grid_now_ns)` promotes each due event into the normal class ahead
  of ordinary not-yet-started messages, but never ahead of a realtime byte.
  `nextByte()` still selects realtime first, so F8/FA/FC/FB is never displaced
  and a byte in flight is never cut. The clock alarm enqueues its F8 and then
  pumps the queue with the same tick deadline, so a clock-derived note for tick
  N can never run ahead of tick N's F8; the worst residual is the scheduler's
  byte-boundary budget (tick byte + the remaining bytes of the one message
  already on the wire, <= 3 byte times = 0.96 ms), not main-loop scheduling.
  Realtime statuses are refused on the timed path. The shared `midifx` core is
  untouched: only the emission timing changed.
- Diagnostics: the 1 Hz `[stat]` line reports `mtx=n+r/n+r` (normal + realtime
  depth for OUT1/OUT2), `fxq=n+n` (pending timed FX messages per output),
  `fxdrop` (timed FX messages dropped/refused), `mtxdrop` (whole normal
  messages dropped), `mtxrej` (new messages refused because only Note Offs were
  queued), `mtxrt` (realtime bytes dropped) and `miss` (clock grid ticks
  skipped).

## SPI link design (non-blocking)

- The old slave called `spi_write_read_blocking()` from the main loop. A Linux
  transfer killed mid-frame left it waiting for clocks that never came, so
  MIDI routing, panel scans and route engines froze until the 8 s watchdog
  rebooted the Pico (the clock alarm and UART ISRs kept running, but nothing
  else did).
- `spi_slave.hpp` is the host-tested transaction engine. The CSn falling edge
  starts a frame, the SPI RX interrupt feeds each MOSI byte and refills the TX
  FIFO from the preloaded response, the rising edge ends it. The main loop
  only polls a 100 us byte-gap timeout and drains the completed-frame mailbox;
  no path waits on the master.
- Resynchronisation: a short transaction (CSn deasserted before 32 bytes, or
  no clock for 100 us) discards the partial frame and counts `spiabort`. Bytes
  that do not start an `MB`/version-1 header are ignored (`spigarb`) until a
  valid header; a complete frame that fails CRC/format is dropped
  (`spibad`). The next transaction is a fresh frame boundary — no reboot.
- App reliability: every accepted (or deduped) CONTROL_EVENT is acked with
  type ACK, length 2, payload = the Pi sequence. The Pi (pi/link/link.cpp)
  holds a control frame at its queue head until that ack returns, replaying
  the same (session, sequence); the receiver's dedup applies it exactly once.
  A lost ack/aborted transfer therefore costs one retry, not a lost Note Off.
  A full 16-slot queue evicts the oldest whole non-Note-Off frame to admit a
  Note Off (never the reverse); only a queue made entirely of Note Offs can
  refuse another one (`noteOffDrops`).
- Diagnostics: `[stat]` adds `spiabort` (short/gap-aborted transactions),
  `spibad` (CRC/format drops), `spigarb` (ignored out-of-frame bytes), `spiok`
  (completed frames), `spiak` (app acks queued) and `spiov` (frames the main
  loop had not drained). Pi-side `PicoLink::Stats` adds `retries`, `acks`,
  `noteOffDrops` and `evicted`.

## Clock bench acceptance (PROPOSED TARGETS, UNMEASURED)

The review's targets, recorded as a checklist for real hardware. None of these
has been measured; they require a logic analyzer on both TRS outputs and the
full system (Pico + Pi + DAW + NVMe + SPI traffic).

| # | Check | Proposed target | Method | Status |
|---|---|---|---|---|
| 1 | Tempo accuracy | <= 0.1% error | 2 h capture on both TRS outs at 20/60/120/180/300 bpm; compare N-tick span to the exact grid | unmeasured |
| 2 | Clock timing error | <= 1 ms max edge error | Per-edge timestamp histogram against the ideal 24-PPQN grid, both outputs | unmeasured |
| 3 | Dropped clock | 0 dropped F8 | Count F8 bytes in 2 h captures; check `[stat] miss=` and realtime drops stay 0 | unmeasured |
| 4 | Soak under load | 2 h clean at each tempo | MIDI in/out traffic + SPI link + DAW + NVMe recording concurrently | unmeasured |
| 5 | TX priority | F8 overtakes 10 queued note bytes; realtime interleaves mid-message | Host test covers the policy; confirm on the wire with an analyzer during a burst | unmeasured (host-verified) |
| 6 | Overflow safety | Never a partial message or hung note | Host test covers whole-message drops and Note Off protection; stress with an analyzer | unmeasured (host-verified) |
| 7 | Transport placement | FA/FB/FC once, in order, not delayed by clock backlog | Analyzer on both outs while clock and note traffic are saturated | unmeasured |
| 8 | SPI aborts recovered | 0 watchdog restarts; every abort resyncs on the next frame | Inject 1-in-8 short transfers for 2 h; compare `[stat] spiabort`/`spiok` and confirm clock keeps running | unmeasured (host-verified: abort/garbage/CRC stages in `spi_slave_test`) |
| 9 | Clock path under SPI faults | 0 dropped F8 during aborts, CRC failures and garbage | 2 h capture both outs while the SPI stream is deliberately broken | unmeasured (host-verified: 300 F8 emitted, 0 drops across 100 incidents) |
| 10 | App-message loss recovery | 0 lost Note Offs; retries bounded; exactly-once delivery | Delay/kill the Linux drain, then audit hanging notes and `retries`/`acks`/`spiov` counters | unmeasured (host-verified: lost frame, lost ACK, 16-slot overflow) |
| 11 | External follow lock | Lock within 3 beats from free-run | Emulated source at 120 bpm with an 8 ms initial phase offset; count beats until 24 consecutive ticks are within 1 ms | simulated: 1.34 beats |
| 12 | Follow steady-state phase | Residual converges to ~0 under clean input | Same run after settling; max \|generated - ideal\| | simulated: 0.002 ms residual |
| 13 | Follow under input jitter | Tempo <= 0.1 %, phase bounded, per-edge slew <= 200 us | Emulated +/-1 and +/-2 ms uniform arrival jitter, 12 s | simulated: tempo 0.004 %, phase 0.84/1.53 ms, interval step 0.36/0.51 ms (1-3 edges clustered per output tick) |
| 14 | Follow under wander | Tracks slow drift, phase bounded | Emulated +/-1.5 bpm sinusoid over 8 s | simulated: 0.455 ms max phase after 2 s |
| 15 | Follow holdover | Frequency stable through a dropout, clock never stops | 550 ms hard dropout; count holdover pulses and interval error | simulated: 26 pulses, 0.0016 % error, no gap |
| 16 | Follow smooth resync | Resync within a few beats, no time-skip on TRS | 121 bpm source returns after the dropout; time to 2 ms and max interval step | simulated: 0.02 beats, 0.196 ms max step, no gap |
| 17 | Clocked FX timing parity | FX event released at its tick's grid deadline; F8 never displaced | Host harness: FX note stamped at tick N, F8 first; 2 h load matrix with FX traffic | simulated: note +0.320 ms (1 byte) typical, +1.320 ms structural worst (tick byte + 2 remaining bytes of an in-flight message), never early, F8 always first, 0 FX dropped |
| 18 | Single-core full load | <= 1 ms clock error, 0 dropped F8, 0 lost FX | clock_sim one-core model, 2 h x 20/60/120/180/300 bpm x both outputs, note/CC + SPI churn (1 abort in 8) + clocked FX traffic | simulated: 0.360 ms max clock error, 0.0000 % tempo error, 0 dropped F8, 0 dropped FX |

## DECISION: single core is sufficient, dual-core deferred (P2)

The review's rule was "two cores only if load tests demand it". The full load
matrix was run on ONE core's model (the same `clock_sim` alarm/UART/SPI models
plus the firmware `MidiTxScheduler` timed-release path and clocked FX traffic):
2 h per tempo, both TRS outputs, note/CC traffic every beat, a 32-byte SPI
transaction every 50 ms with one aborted transfer in eight, and a clocked FX
step (note-on + gate note-off, both timestamped) every eighth note. Virtual
time, so every scenario is deterministic and exhaustive rather than sampled.

```
tempo  out  F8_pulses  tempo_err%  clock_max_ms  FX_msgs  fx_max_ms  fx_min_ms  F8_drop  FX_drop  verdict
 20.0  1        57601     0.0000       0.3602     9600     1.3202     0.3210        0        0  PASS
 20.0  2        57601     0.0000       0.3602     9600     1.3202     0.3210        0        0  PASS
 60.0  1       172801     0.0000       0.3602    28800     1.3202     0.3202        0        0  PASS
 60.0  2       172801     0.0000       0.3602    28800     1.3202     0.3202        0        0  PASS
120.0  1       345601     0.0000       0.3602    57600     1.3202     0.3202        0        0  PASS
120.0  2       345601     0.0000       0.3602    57600     1.3202     0.3202        0        0  PASS
180.0  1       518401     0.0000       0.3602    86400     1.3202     0.3202        0        0  PASS
180.0  2       518401     0.0000       0.3602    86400     1.3202     0.3202        0        0  PASS
300.0  1       864001     0.0000       0.3602   144000     1.3202     0.3202        0        0  PASS
300.0  2       864001     0.0000       0.3602   144000     1.3202     0.3202        0        0  PASS
```

Verdict: **single core sufficient - dual-core deferred.** Worst-case clock
error is 0.360 ms (budget 1 ms) and 0 F8 were dropped; tempo error is 0.0000 %
over 2 h. The FX column is the deadline-to-first-byte error of a clock-derived
message: 0.320 ms typical (one tick byte ahead of it) and 1.320 ms worst case,
which is exactly the byte-boundary budget (alarm phase + tick byte + the two
remaining bytes of a 3-byte message already on the wire). It is a MIDI framing
bound, not a second timing regime: no FX event was early, none was dropped, and
the F8 for every tick was always ahead of its clock-derived events. The
measured worst case leaves > 0.6 ms of margin on the clock budget, so a second
core would add a queue/lock boundary to the hot path without buying accuracy.
The `RP2350`'s second core stays unused; revisit only if a bench run with the
real Pi, NVMe and USB stack shows `[stat] miss=` (skipped grid ticks) or clock
error > 1 ms with `fxq` at its 32-message ceiling.

Run the host side with `make host` (or `make test`) from `midibrain/`. The
host test simulates 500,000 clock ticks at 120 bpm against the exact rational
grid and exercises TX priority, realtime interleave, timed FX release and
overflow in `protocol_test.cpp`. `fx_timing_test.cpp` measures the timestamped
release against the virtual wire and runs the full-load matrix above

See `docs/protocol.md` for the link contract, the CONTROL_EVENT registry and
the prototype limitations (no SysEx forwarding, no scheduled-output timeline,
master Must hold CS across full 32-byte transfers with >= 10 us setup).

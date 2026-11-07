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
- SPI0 slave @1 MHz to the Pi: CSn-polled 32-byte exchanges, HELLO/ACK
  handshake with ROSC-derived session, sequence dedup, READY pin, 8 s watchdog.

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
  period through the same absolute grid.
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
- Clocked MIDI FX output (arp, repeater, LFO, generator chains, routed sends)
  goes through the same `midi_emit()` path, so a clock-derived note cannot block
  the clock path.
- Diagnostics: the 1 Hz `[stat]` line reports `mtx=n+r/n+r` (normal + realtime
  depth for OUT1/OUT2), `mtxdrop` (whole normal messages dropped), `mtxrej`
  (new messages refused because only Note Offs were queued), `mtxrt` (realtime
  bytes dropped) and `miss` (clock grid ticks skipped).

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

Run the host side with `make host` (or `make test`) from `midibrain/`. The
host test simulates 500,000 clock ticks at 120 bpm against the exact rational
grid and exercises TX priority, realtime interleave and overflow in
`protocol_test.cpp`.

See `docs/protocol.md` for the link contract, the CONTROL_EVENT registry and
the prototype limitations (no SysEx forwarding, no scheduled-output timeline,
master Must hold CS across full 32-byte transfers with >= 10 us setup).

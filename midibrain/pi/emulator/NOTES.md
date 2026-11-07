# emulator/ — headless backends + selftest (wave 2)

Works (verified headless on this macOS dev machine):

- `SynthBank` generates one deterministic mono voice (sine/noise/silent) per
  `audio::Engine` input channel and hands its block pointers to `process()`.
- `runScenario()` scripts a ~5 s session against the real core modules:
  `EmulatorTransport` + `PicoLink` handshake (Linked event, session id, boot
  tempo 120.00, 279 ticks, tempo echo 140.00, no LinkLost), MIDI loopback
  (injected note-on -> `nextEvent` -> `Router` -> TRS OUT 1 via `sendMidi` plus
  USB OUT, channel mask and 0xF8 pass), a 2 s two-armed take through `Engine` +
  `Recorder` (3 float32 WAVs, 96 000 frames, 0 drops), the FX send, and a WAV
  verify that parses RIFF/fmt/fact/data and checks sample content.

Stubbed / deliberate:

- `ClapEffect` is a local `audio::IEffect` -> `clap::Host` bridge (the app layer
  owns the production adapter). With no loadable module the FX stage runs a
  local `StubGain` and prints a loud `SKIP`; CTest stays green.
- No display, no JUCE, no ALSA/CoreAudio device, no RtMidi: nothing here proves
  clock accuracy, latency or hardware behaviour. Pi follow-up: same scenario on
  `LinuxSpidevTransport`, real ALSA I/O, measured tick jitter and drop rate.

## Clock-emulation harness (phase 1: hardware models + acceptance matrix)

`clock_sim.{hpp,cpp}` + `clock_sim_test.cpp` make the expert MIDI-clock review
(2026-11-07) CI-verifiable. Pure C++20 host code, deterministic virtual time,
no wall-clock sleeps; the full 5-tempo x 2-output x 2 h matrix runs in ~1 s.
`mnky_clock_sim_test` is registered in CTest and the repository `make test`
`host` target compiles/runs it; `mnky_selftest` also runs it and prints the
matrix. Nothing here proves hardware: it proves what the modelled RP2350/MIDI
rules do with the current firmware design, and phase 2 re-runs the matrix
against the real scheduler through the seam below.

Time base is integer 1/9 ps ticks: every 24-PPQN interval at 20/60/120/180/300
bpm is exact, so the deadline path is asserted against `k*interval` with no
rounding drift. A 2 h run uses 6.5e16 ticks (int64).

### What each model asserts

| Model | Assertion |
|---|---|
| `Scheduler` | deterministic discrete-event order; each dispatch adds modelled interrupt latency (0.2 us + 0..40 us host/SPI churn jitter) and callback execution cost (20 us clock callback). |
| `AlarmModel` | exact `add_alarm_in_us` split. Positive return re-arms from callback end: at 120 bpm the legacy path drifts **+6859 ms over 2 h** (345 271 pulses; exec + latency + 1 us integer period). Negative return re-arms from the previous deadline: **max error 0.2 us over 2 h**, i.e. zero accumulated drift. |
| `UartTxModel` | 31250 baud / 320 us per byte, 32-byte TX FIFO, two outputs. Blocking `putc` puts a clock byte behind 10 queued note bytes = **3.199 ms > 1 ms goal**; the 34th byte blocks on a full FIFO. Priority TX emits F8/FA/FC/FB at the next byte boundary, interleaves inside a multi-byte note-on per MIDI 1.0, and keeps framing intact. Overflow is whole-message only and evicts a Note On/CC before a queued Note Off (dropped Note Off = FAIL). |
| `ExternalClockSource` / `ClockFollower` | injectable +/-ms jitter, slow wander, 550 ms hard dropout and a tempo step; reference PLL policy (EMA interval + bounded 200 us/tick phase slew) measures lock time, steady-state phase error (0.95 ms with +/-1 ms jitter), wander (0.42 ms), holdover (0 % frequency error, 26 pulses generated through the gap) and smooth resync (0.19 ms max interval step, no jump). |
| `SpiLinkModel` / `AppSendQueue` | 32-byte frame codec (CRC16-CCITT-FALSE, session, sequence, padding), dedup of repeated frames, mid-frame abort recovery without the 8 s watchdog (and a negative control that the watchdog does fire after 8 s of silence), 16-slot outgoing queue drop-newest overflow, app ack/retry applied once, Note-Off priority. |
| Acceptance matrix | 2 h per tempo at 20/60/120/180/300 bpm, both TRS outputs, note/CC traffic per beat + a 50 ms SPI transaction stream (1 in 8 aborted) + bounded host churn. Targets: tempo error <= 0.1 %, max clock timing error <= 1 ms, 0 dropped F8, 0 watchdog restarts. |

### Acceptance matrix output (values from simulation)

```
tempo  out  pulses   tempo_err%  max_err_ms  F8_drop  notes    cc      spi_tx  spi_abort  recovered  verdict
 20.0  1      57601     0.0000      0.3602        0     4800    2400  144020      18002      18002  PASS
 20.0  2      57601     0.0000      0.3602        0     4800    2400  144020      18002      18002  PASS
 60.0  1     172801     0.0000      0.3602        0    14400    7200  144020      18002      18002  PASS
 60.0  2     172801     0.0000      0.3602        0    14400    7200  144020      18002      18002  PASS
120.0  1     345601     0.0000      0.3602        0    28800   14400  144020      18002      18002  PASS
120.0  2     345601     0.0000      0.3602        0    28800   14400  144020      18002      18002  PASS
180.0  1     518401     0.0000      0.3602        0    43200   21600  144020      18002      18002  PASS
180.0  2     518401     0.0000      0.3602        0    43200   21600  144020      18002      18002  PASS
300.0  1     864001     0.0000      0.3602        0    72000   36000  144020      18002      18002  PASS
300.0  2     864001     0.0000      0.3602        0    72000   36000  144020      18002      18002  PASS
```

The 0.36 ms worst case is one in-flight non-realtime byte (320 us) plus 40 us
host/SPI churn jitter: realtime still waits for the current byte, never for the
rest of a message. This is the table the bench run must reproduce on the logic
analyser; it is not a hardware measurement.

### Phase-2 seam contract (firmware scheduler implements these)

Defined in `clock_sim.hpp` under `mnky::emulator::clocksim::seam`; units are
1/9 ps, the firmware adapter quantises to the RP2350 1 MHz timer tick.

- `TimeSource::now()` - monotonic capture time.
- `Alarm::scheduleAfter(delay, Callback)` - `Callback` returns `Ps(Ps now)` with
  `add_alarm_in_us` semantics: **>0 next alarm at callback end + return**
  (accumulates execution cost), **<0 next alarm at previous scheduled deadline +
  |return|**, 0 stops. The firmware's current `clock_tick` returns a positive
  value and therefore sits on the drifting side of this contract.
- `UartTx::sendRealtime(byte)` / `sendMessage(span)` - realtime jumps ahead at
  the next byte boundary and may interleave inside a message; non-realtime
  messages are atomic; `wireTime()` is the last byte's wire start.
- `SpiLink::transfer(mosi, miso)` - one 32-byte CSn transaction; short transfers
  model a killed Linux transfer and must recover without a watchdog restart.
- `ExternalClock::onPulse(capture, phaseReference)` - implemented by the
  firmware follow policy; `phaseReference` is the jitter-free ideal tick time.

To re-run the matrix against firmware code, add adapters that implement these
four interfaces over `add_alarm_in_us`, the UART TX path, the SPI slave path and
`ClockFollow`, then point `runClockSimulation()` at them. `clock_sim_test.cpp`
is the acceptance entry; keep its exit code as the gate.

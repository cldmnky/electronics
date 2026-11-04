# midi/ - router, USB MIDI, clock discipline, buttons, MIDI learn (JUCE-free)

Works (host-verified here): `router.{hpp,cpp}` 3-in/3-out matrix, routes default
off, channel mask (bit n = channel n+1, `0xFFFF` = all) on channel voice only,
transpose on note on/off only with out-of-range notes dropped (never clamped),
realtime/system-common never channel-masked or transposed. `midifx.{hpp,cpp}`
is the portable Wave-1 MIDI FX pipe engine (types 1..11, positional params from
`pi/session/session.hpp`, bypass, deterministic per-route RNG, Wave-2 rejection)
described below. `clock.{hpp,cpp}`: `UsbClock` is pure
logic - the offset EMA follows the fastest recent delivery so arrival is never
mistaken for the tick stamp, creeping up for clock drift; the phase is a local
Pi timer slewed from each `EV_CLOCK_TICK` and re-pegged (not burst-fired) after a
stall; emits only while rolling. `usb.{hpp,cpp}`: RtMidi 6.1.0 wrapper, device
index = enumeration index, optional virtual `mnkybox` ports (`device` 0xFF),
callback -> `MidiEvent` + `piNowUs()`, UsbOut fans out to every open output.
`buttons.hpp`: `mapControl` over `mb::CTL_*`, so the registry stays in firmware.
`learn.{hpp,cpp}`: `LearnTable` binds (cc 0..127, channel 1..16) to one opaque
dotted target path and rejects bad ranges, empty targets and duplicate
(cc, ch) pairs; `LearnEngine` holds one `Setter(double 0..1)` per path and
`dispatch()` accepts 3-byte control changes only, scaling `data[2]/127`,
channel-isolated and source-port agnostic. Unmapped CCs, non-CC/truncated
messages are ignored; a bound CC with no registered setter still counts as
matched. Re-registering a path replaces its setter, an empty setter unregisters
it, and `clear()` drops bindings but keeps target setters. Bindings are
`{cc, ch, target, track}`, mirroring `session::MidiMapEntry`, so the app copies
`listBindings()` into the session `midimap` section; this module never parses
or resolves target paths.

`test_learn.cpp`: bind/list order, bounds, duplicate rejection, remove, value
scaling at 0/64/127, unmapped and non-CC/truncated messages, channel
isolation, target replacement/unregister and clear. Not done: learn-mode
capture state (the app chooses which CC becomes a binding), persistence code
(the session layer owns save/load) and thread synchronization (one MIDI
thread owns the engine).

## MIDI FX (Wave-1) data path and side split

The iron rule is that one route's pipe chain is evaluated exactly once, on
exactly one side. Firmware owns every TRS->TRS chain; the Pi owns every route
that touches USB. This is the path the code implements:

1. TRS IN byte: Pico UART IRQ -> ring -> `drain_midi()` -> `MidiParser`. The
   selected clock source's 0xF8 is consumed for follow; FA/FB/FC are consumed
   on every TRS input (transport-follow policy); every other message becomes
   `EV_MIDI_MSG` to the Pi (`queue_midi`). The firmware has no local
   TRS->TRS forwarding path in this tree.
2. Pi link thread `LinkClient::drainEvents()` maps `EV_MIDI_MSG` to
   `MidiEvent{source = TrsIn1/2}` -> `MidiGlue::handleInput()` -> monitor ring,
   MIDI learn and `Router::route()` (guarded). USB IN arrives on the RtMidi
   callback and takes the same `handleInput()` path, so `UsbIn` is merged
   before the matrix.
3. `MidiGlue::pump()` flushes the outbox on the link thread: `UsbOut` -> RtMidi
   fan-out; `TrsOut1/2` -> `PicoLink::sendMidi()` = `EV_MIDI_SEND` (0x15) ->
   Pico `midi_emit()` straight to the UART. 0x15 is the unprocessed immediate
   path: it never enters the Pico's pipe chains.
4. Pi-generated clock is one-way and never re-enters the matrix: the USB
   mirror calls `MidiGlue::sendUsbClock()` (RtMidi send, not `handleInput`),
   and TRS clock/transport are generated on the Pico itself. `UsbTransportFollow`
   maps USB FA/FB/FC to `EV_TRANSPORT`, also outside the pipe engine. There is
   no EV_MIDI_SEND loopback: the Pi does not feed its own sends back into
   `handleInput()`, and a Pico->Pi kind echoed back is ignored.
5. Pico FX chain: `route_pipes[src][dst]` for the four physical TRS pairs,
   configured by `EV_MIDI_ROUTE` (0x18) + `EV_MIDI_ROUTE_PIPE` (0x19) and
   evaluated only for `EV_MIDI_ROUTE_SEND` (0x1A) in `firmware/pico/main.cpp`.
   `0x1A` emits only on the physical destination and drops if the route is off.
6. Pi FX chain: `Router` stores a `PipeChain` per route but evaluates it only
   when `piOwnsPipeChain(src,dst)` is true (`src == UsbIn || dst == UsbOut`).
   A TRS->TRS chain is stored as the mirror the link programs into the Pico
   and is never evaluated here. Order per destination: channel mask -> pipe
   chain -> source transpose. The mask therefore still wins over a pipe that
   would rotate a blocked channel into an allowed one, and transpose sees the
   pipe's output note (not the other way round).
7. `NoteLength` (type 10) is Wave 1 but time-based: `Router::route()` uses
   `ev.stampUs / 1000` and `Router::tick(nowMs)` flushes due note-offs, so the
   caller must drive `tick()` from its pump loop for timely offs. The app glue
   does not call it yet (app lane).

Integration gaps owned outside this lane (reported, not fixed here):
- `pi/app/link_client.cpp` still forwards every TRS destination through
  `sendMidi()` (0x15). Once a TRS->TRS route has pipes, the app must send those
  bytes with `EV_MIDI_ROUTE_SEND` (0x1A) so the Pico evaluates the chain. Until
  then such a chain is configured on the Pico but applied to no bytes: zero
  evaluations, never two. `PicoLink` deliberately has no 0x1A sender in this
  wave (lane scope: EV_MIDI_ROUTE config only). If the firmware worker instead
  makes the Pico forward TRS->TRS locally, the app must stop forwarding those
  routes over 0x15; the ownership split in `piOwnsPipeChain` does not change.
- `session::apply()` does not copy `RouteState::pipes` into `Router::setPipes()`
  yet, and the app does not push the TRS chain to the link. Session/app lane.
- Transpose order asymmetry on TRS->TRS: `Router` applies the source transpose
  when it forwards those bytes, and the Pico's chain then sees the transposed
  note. The effective order there is mask -> transpose -> firmware pipes, while
  a Pi-owned route is mask -> pipes -> transpose as specified. Fixing it means
  either moving transpose into the Pico chain or applying it after
  `EV_MIDI_ROUTE_SEND`; neither is in this wave, and no fake order is claimed
  here.
- The firmware worker's `firmware/pico/midi_pipe.hpp` passes Wave-2 ids (12..16)
  through unchanged; this Pi engine rejects evaluation with a reason. Wire
  types/limits match `firmware/pico/protocol.hpp` byte-for-byte, so only the
  evaluation policy differs. Convergence follow-up: firmware should adopt
  `midifx.*` as the single engine when Wave 2 lands.
- The 0x18/0x19 wire has no RNG seed field. The Pi engine takes a per-route
  seed, but the Pico seeds `route_pipes` from its own session/time at boot
  (`firmware/pico/main.cpp` main()), so a TRS route's Chance/VelocityRandom
  stream cannot be replayed after a Pico reboot. A deterministic TRS stream
  needs a wire extension; none is invented here.

## midifx engine

`test_midifx.cpp` covers each Wave-1 pipe with boundary values (Filter kind
mask, NoteRange note/velocity edges + swap/clamp, CcRemap endpoints + rounding
+ single-CC span, ChannelRemap any/clamp, ScaleQuantize tie-down/root/user
mask/0/127 wrap, Chance 0/100 and seeded determinism, VelocityRandom
window/floor/clamp/determinism, Dispatcher rotation and off pairing,
Sustain latch/release/64-event cap, NoteLength scheduling/suppression/later
pipes, Transform all match/output kinds and partial config), bypass and chain
order, the 8-pipe/10-param configure limits, and Wave-2 configure-tolerated /
evaluation-rejected behaviour. `Router` tests in `test_midi.cpp` add the
Pi-owned USB-route cases, mask->pipes->transpose order, Wave-2 statistics and
the no-double-processing check: a piped TRS->TRS message crossing the Pi is
delivered unprocessed and `pipeDiagnostics().evaluations` stays 0.

Not done: Wave 2 (all five clocked types), per-route seeding from the session
(the Router default is a fixed deterministic constant plus `setPipeSeed`), and
persistence (session owns it). The log seam for a rejected chain is
`Router::pipeDiagnostics()` (`rejected`, `lastRejectReason`, `lastRejectPipe`);
the app owns the actual log line.

`test_midi.cpp`: routing matrix, merge/fan-out, channel mask, transpose, USB
route pipes and the Pi/firmware side split, button table, RtMidi plumbing, and
clock discipline under 0..6 ms deterministic arrival jitter plus
duplicate/stale ticks, phase spacing and stall recovery.

Not done: SysEx and >3-byte messages are dropped (protocol v1 limit); routing
has no persistence and the app must enable routes; tempo comes from `EV_TEMPO`
only (not derived from tick intervals); `popDue` reports timing - the caller
sends `kClockByte`/transport bytes. Pi follow-ups: build RtMidi with
`__LINUX_ALSA__` + libasound, re-run the test on the Pi 5, and measure real SPI
and USB latency/jitter: nothing here is a hardware timing measurement.
`CMakeLists.txt` builds `mnky_midi` + `test_midi` + `test_midifx` (CTest names
`test_midi`, `test_midifx`) when configured standalone, and reuses the root
build's `mnky_rtmidi` target when the setup team's `pi/CMakeLists.txt` adds
this directory. Build note: `c++` in this shell is Homebrew GCC 14, which cannot
parse the Apple framework headers under `-Werror`; use `/usr/bin/c++` (Apple
clang) - the midi build line is zero-warning and green there.

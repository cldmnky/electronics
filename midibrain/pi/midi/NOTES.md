# midi/ - router, USB MIDI, clock discipline, buttons, MIDI learn (JUCE-free)

Works (host-verified here): `router.{hpp,cpp}` 3-in/3-out matrix, routes default
off, channel mask (bit n = channel n+1, `0xFFFF` = all) on channel voice only,
transpose on note on/off only with out-of-range notes dropped (never clamped),
realtime/system-common always passing. `clock.{hpp,cpp}`: `UsbClock` is pure
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

`test_midi.cpp`: routing matrix, merge/fan-out, channel mask, transpose, button
table, RtMidi plumbing, and clock discipline under 0..6 ms deterministic arrival
jitter plus duplicate/stale ticks, phase spacing and stall recovery.

Not done: SysEx and >3-byte messages are dropped (protocol v1 limit); routing
has no persistence and the app must enable routes; tempo comes from `EV_TEMPO`
only (not derived from tick intervals); `popDue` reports timing - the caller
sends `kClockByte`/transport bytes. Pi follow-ups: build RtMidi with
`__LINUX_ALSA__` + libasound, re-run the test on the Pi 5, and measure real SPI
and USB latency/jitter: nothing here is a hardware timing measurement.
`CMakeLists.txt` builds `mnky_midi` + `test_midi` (CTest name `test_midi`) when
configured standalone, and reuses the root build's `mnky_rtmidi` target when the
setup team's `pi/CMakeLists.txt` adds this directory. Build note: `c++` in this
shell is Homebrew GCC 14, which cannot parse the Apple framework headers under
`-Werror`; use `/usr/bin/c++` (Apple clang) - the documented midi build line,
including its 11 PASS lines, is zero-warning and green there.

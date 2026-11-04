# midi/ - router, USB MIDI, clock discipline, buttons (JUCE-free)

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

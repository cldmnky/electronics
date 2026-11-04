# link/ — Pico SPI link core (JUCE-free)
Works (host-tested, `PASS` lines from `test_link.cpp`):
- `EmulatorTransport`: scripted Pico answering HELLO with ACK, re-announcing
  state/tempo on (re)connect, emitting tick frames from a per-transaction device
  clock, queueing injected buttons/MIDI and capturing `MIDI_SEND`s. Mirrors
  `firmware/pico/main.cpp` idle fill and `(session, sequence)` dedup; answers
  STATUS_REQ with live or scripted values and honours reboot/reset-link.
- `PicoLink`: handshake, own session id, sequence allocation, remote dedup,
  >= 1 ms paced `poll()`, event drain (`MidiIn/Control/Tempo/Tick/State/
  Linked/LinkLost`), `onTick`, `Stats`. Pico session change (reboot or
  `0x23` RESET_LINK) => `LinkLost`, outbound flush, re-handshake.
- Control plane: `statusRequest()`/`lastStatus()` (0x20/0x21 liveness and
  counters), `rebootPico()` (0x22 normal or USB boot), `resetLink()` (0x23
  new session without rebooting).
- Wire format never redefined: `link.hpp` includes the firmware
  `protocol.hpp`/`midi.hpp` and uses `mb::encode/decode` plus `EV_*`/`CTL_*`.
Stubbed / unverified:
- `LinuxSpidevTransport` (1 MHz, mode 0, full-32B-or-fail) and `ReadyGpio`
  (BCM 24) compile under `__linux__` only; elsewhere ctors throw. Unrun on Pi.
- READY is exposed but unused (transactions are paced by time, not the pin);
  no ACK/retry/credit/queue recovery either: overruns are counted only.
Pi follow-ups: check spidev CS timing against the Pico's 10 us arming window,
confirm the GPIO v2 chardev line request on the Pi 5 kernel, measure the tick
stamp offset before wiring `midi/UsbClock` to `onTick`.

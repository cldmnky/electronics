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
- MIDI FX route config: `midiRoute(src,dst,on)` (0x18) and
  `midiRoutePipe(src,dst,index,type,bypass,offset,params,count)` (0x19) for the
  four physical TRS routes only (src/dst 0 = IN1/OUT1, 1 = IN2/OUT2; USB routes
  are Pi-side and rejected). The sender mirrors the firmware's
  `MidiPipeChain::setPipeChunk` shape rules before queueing, so its cache can
  never describe a chain the Pico refused. Each route's last successfully
  queued chain is re-sent after HELLO in wire order (0x18 first, then chunked
  params of at most 5 per frame), covering Pico power cycles and RESET_LINK;
  a new `midiRoute()` starts a fresh chain and clears the cached pipes, exactly
  like firmware `begin()`. `EmulatorTransport` records the applied chain for
  tests (`midiRoute(src,dst)`), and `reboot()` resets it as RAM would.
- Routed UART emit: `sendMidiRoute(src,dst,data,n)` queues
  `EV_MIDI_ROUTE_SEND` (0x1A) through `mb::pack_midi_route_send`, so the Pico
  evaluates the (src,dst) chain configured by 0x18/0x19 before emitting on that
  physical TRS output. src/dst are 0/1 and the message is 1..3 bytes; a disabled
  route drops it on the firmware. Per-message, no resend cache. `sendMidi`
  (0x15) stays the unprocessed immediate path.
- Wire format never redefined: `link.hpp` includes the firmware
  `protocol.hpp`/`midi.hpp` and uses `mb::encode/decode` plus `EV_*`/`CTL_*`.
  The 0x18/0x19 payloads come from `mb::pack_midi_route*`, matched against the
  firmware worker's `firmware/pico/protocol.hpp` (read-only).
Stubbed / unverified:
- `LinuxSpidevTransport` (1 MHz, mode 0, full-32B-or-fail) and `ReadyGpio`
  (BCM 24) compile under `__linux__` only; elsewhere ctors throw. Unrun on Pi.
- READY is exposed but unused (transactions are paced by time, not the pin);
  no ACK/retry/credit/queue recovery either: overruns are counted only.
Pi follow-ups: check spidev CS timing against the Pico's 10 us arming window,
confirm the GPIO v2 chardev line request on the Pi 5 kernel, measure the tick
stamp offset before wiring `midi/UsbClock` to `onTick`.

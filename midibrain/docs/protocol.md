# Framed communication: Pico firmware link + host-tested codec

`firmware/pico/protocol.hpp` is the shared framing core (host-tested and
compiled into the Pico firmware unchanged): fixed 32-byte frames,
little-endian integers, CRC-16/CCITT-FALSE. `firmware/pico/midi.hpp` adds the
MIDI parser and the CONTROL_EVENT payload registry, also host-tested.
`firmware/pico/main.cpp` is the RP2350 (Pico 2) application. `protocol_test.cpp` runs
the codec, parser and event round-trips on the host under `make test`.

## Frame layout

| Byte(s) | Contents |
|---|---|
| 0–1 | ASCII `MB` |
| 2 | version 1 |
| 3 | type: 1 HELLO, 2 ACK, 3 CONTROL_EVENT |
| 4–5 | sequence |
| 6 | payload length 0–18 |
| 7 | reserved, zero |
| 8–11 | nonzero session identifier (sender's own) |
| 12–29 | payload and zero padding |
| 30–31 | CRC-16/CCITT-FALSE over bytes 0–29 (poly 0x1021, init 0xffff, no reflection or final xor) |

The receiver rejects bad magic/version, unknown types, nonzero reserved/padding,
bad CRC, invalid length and zero session.

## SPI transaction contract (Pico is slave, Pi is master)

- SPI0 mode 0, MSB first, **1 MHz nominal on both ends** (validate higher rates
  on hardware before raising).
- One transaction = one CSn assertion carrying **exactly 32 clocks/bytes**;
  the master waits **>= 10 us after asserting CSn** (slave arms on the edge),
  then deasserts. An aborted transfer hangs the slave until its 8 s watchdog
  reboots it — the master must guarantee full transfers.
- Full duplex: every transaction exchanges one frame each way. Idle fillers
  are **repeats of the last frame**; both sides dedup by (session, sequence)
  and never re-apply duplicates. Sequence increments on new frames only.
- **READY** (Pico GP20 out): high while Pico frames wait, and until the Pi's
  first HELLO (the boot HELLO is always pending). The master should pace
  transactions (>= 1 ms apart) so the Pico main loop can process each receive
  before the next lands; 32 B/ms still far exceeds MIDI bandwidth.

## Session and handshake

- Each side mints its own nonzero session id (Pico: 32 ROSC RANDOMBIT samples,
  timer fallback). Frames carry the sender's id.
- Pico boots unlinked, queues HELLO (sequence 0), raises READY. On the Pi's
  HELLO it records `pi_session`, marks link up and queues an empty ACK.
  A changed `pi_session` means Pi reboot: remote-duplicate tracking resets.
- Link-up in this revision = Pi HELLO seen. No retry/credit/flow protocol yet;
  queue overflows are counted (`txov`, `rxov`) and reported on the USB console,
  not recovered.

## CONTROL_EVENT registry (payload[0] = kind)

Pico → Pi:

| Kind | Bytes | Meaning |
|---|---|---|
| `0x01` MIDI | port, size, 3 data bytes, stamp_us LE32 | Timestamped MIDI IN message (length 10) |
| `0x02` control | id, state/delta | Button press (1) / release (0); `0x10` ENCODER_TURN carries int8 delta (length 3) |
| `0x04` tempo | bpm×100 LE16 | Local tempo after encoder or Pi change (length 3) |

Control ids: 1 NAV_UP, 2 NAV_DOWN, 3 NAV_LEFT, 4 NAV_RIGHT, 5 NAV_PRESS,
6 ENCODER_SW, 10 REC, 11 OVER_DUB, 12 STOP, 13 PLAY, 14 PLAY_START,
15 SHIFT_EXIT.

Pi → Pico:

| Kind | Bytes | Meaning |
|---|---|---|
| `0x10` SET_TEMPO | bpm×100 LE16 | Clamped 20–300 bpm; Pico echoes EV_TEMPO |
| `0x11` TRANSPORT | 0 stop, 1 start, 2 continue | START resets tick count; bytes always emitted on both UARTs |
| `0x12` CLOCK_ENABLE | 0/1 | Gates 24 ppq tick generation (transport bytes unaffected) |
| `0x13` LED_SET | mask, states | Bits 0/1/2 = REC/OVERDUB/PLAY; local buttons mirror by default |
| `0x15` MIDI_SEND | port, size, bytes | Immediate UART emit (no scheduled timeline yet) |

## Pico clock and controls

- Clock: dynamic one-shot alarms at 60e6/(bpm·24) us, so tempo changes apply
  on the next tick; 0xF8 on both UARTs only while enabled + rolling.
- Encoder detent = 4 quadrature edges (±1.00 bpm, ±0.10 with SHIFT held).
- Buttons/nav active-low, 10 ms debounce, 1 kHz scan; UART RX is IRQ-driven
  into 128-deep stamped rings per port.
- LEDs default to mirroring REC/OVERDUB/PLAY presses; LED_SET overwrites.

## Prototype limitations (not yet implemented)

SysEx forwarding (skipped), scheduled-output musical timeline (MIDI_SEND is
immediate), ACK/retry/credit reliability, queue recovery, CS-abort recovery
beyond the watchdog, SPI rates above 1 MHz, clock jitter measurement, and
thermal/EMC behaviour. The framed loopback, debounce, parser and event tests
are host simulations, not hardware validation.

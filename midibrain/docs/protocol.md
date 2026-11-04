# Framed communication: Pico firmware link + host-tested codec

`firmware/pico/protocol.hpp` is the shared framing core (host-tested and
compiled into the Pico firmware unchanged): fixed 32-byte frames,
little-endian integers, CRC-16/CCITT-FALSE. `firmware/pico/midi.hpp` adds the
MIDI parser and the CONTROL_EVENT payload registry (including the `0x20`–`0x23`
control plane), also host-tested.
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
- A Pi `0x23` RESET_LINK asks the Pico to mint a fresh session and re-announce
  state/tempo without rebooting; tempo, transport state and tick count
  survive. The Pi treats the new session exactly like a Pico reboot:
  `LinkLost`, outbound flush, re-handshake, `Linked`.
- Link-up in this revision = Pi HELLO seen. No retry/credit/flow protocol yet;
  queue overflows are counted (`txov`, `rxov`), printed on the Pico USB
  console and reported to the Pi in the `0x21` status response; they are not
  recovered.

## CONTROL_EVENT registry (payload[0] = kind)

Pico → Pi:

| Kind | Bytes | Meaning |
|---|---|---|
| `0x01` MIDI | port, size, 3 data bytes, stamp_us LE32 | Timestamped MIDI IN message (length 10) |
| `0x02` control | id, state/delta | Button press (1) / release (0); `0x10` ENCODER_TURN carries int8 delta (length 3) |
| `0x04` tempo | bpm×100 LE16 | Local tempo after encoder or Pi change (length 3) |
| `0x05` tick | tick LE32, Pico stamp_us LE32 | Clock tick notice + tick time, every tick while rolling (length 9) |
| `0x21` STATUS_RESP | uptime_s LE32, ticks LE32, txq depth, txov LE16, rxov LE16, flags | Answer to `0x20` (length 15). Flag bit 0 = Pi HELLO seen (link up), bit 1 = rolling, bits 2–7 zero. `txov` counts outbound-frame queue overflows; `rxov` is the sum of both UART RX ring overruns (both saturate at 65535). |

Control ids: 1 NAV_UP, 2 NAV_DOWN, 3 NAV_LEFT, 4 NAV_RIGHT, 5 NAV_PRESS,
6 ENCODER_SW, 10 REC, 11 OVER_DUB, 12 STOP, 13 PLAY, 14 PLAY_START,
15 SHIFT_EXIT. Payload[2] is 1 pressed / 0 released, except ENCODER_TURN
(signed delta) and SHIFT_EXIT, which additionally reports 2 while a hold
is past 500 ms (long-press). The app enters shift mode live on the 2;
a short press (1 then 0, no 2) means exit. Release always reports 0.

Pi → Pico:

| Kind | Bytes | Meaning |
|---|---|---|
| `0x10` SET_TEMPO | bpm×100 LE16 | Clamped 20–300 bpm; Pico echoes EV_TEMPO |
| `0x11` TRANSPORT | 0 stop, 1 start, 2 continue | START resets tick count; bytes always emitted on both UARTs |
| `0x12` CLOCK_ENABLE | 0/1 | Accepted but ignored: the clock is always on (deprecated) |
| `0x06` state | rolling 0/1, tick LE32 | Transport state at the transition; sent on transitions, boot and Pi (re)connect (length 6) |
| `0x13` LED_SET | mask, states | Bits 0/1/2 = REC/OVERDUB/PLAY partial overwrite (latches an override); mask bit 7 = sync arm state from states bits 0/1 instead |
| `0x15` MIDI_SEND | port, size, bytes | Immediate UART emit (no scheduled timeline yet) |
| `0x16` MODE | 0 local, 1 manual | Panel-transport authority; resets to local on boot/HELLO |
| `0x20` STATUS_REQ | none | Liveness/status probe; the Pico answers with `0x21` (length 1) |
| `0x22` REBOOT | mode: 0 normal firmware reboot, 1 USB boot | 0 restarts the application from flash via the watchdog; 1 enters BOOTSEL for the firmware-update flow (length 2). No ACK: mode 0 shows up as a session change, mode 1 as link silence. |
| `0x23` RESET_LINK | none | Mint a fresh Pico session and re-announce state/tempo without rebooting; the Pi re-handshakes (length 1) |

Control-plane use on the Pi: `0x20` STATUS_REQ is the liveness probe. Send it
when the link has been quiet and use the uptime/ticks/counters to distinguish
an idle Pico from a wedged one; a host-side watchdog can treat repeated
missing or stale responses as a link failure. `0x22` mode 0 is recovery —
restart the Pico firmware when the link cannot be resynced. Mode 1 enters
BOOTSEL/USB boot for the later Pico firmware-update flow: the Pi must expect
the SPI link to disappear until the Pico is flashed and restarted. `0x23`
RESET_LINK is link resync — force a new Pico session and a state/tempo
re-announce without losing tempo, transport or tick state, e.g. after a
Pi-side link error or stale duplicate-tracking state. A Pico reboot or a
`0x23` RESET_LINK always appears to the Pi as a session change (`LinkLost`
then `Linked`), so the host can use the same reconnect path for all of them.

## USB clock mirror (Pi side, pending implementation)

The Pico is the single clock master for TRS and USB. The Pi must NOT mirror
ticks naively (SPI + Linux scheduling latency would put millisecond jitter on
the USB wire). Instead it disciplines its own USB clock generator:

- **Offset mapping**: on each `0x05` tick, `offset = pi_now - stamp -
  latency_est` (EMA over recent ticks; tempo from `0x04`/tick intervals).
  Never mistake delivery time for tick time — the stamp is authoritative.
- **USB output**: schedule clock bytes on Pi high-resolution timers at mapped
  `tick_time + k·interval`; correct drift per tick frame. Transport bytes go
  out on the same schedule. Sustained rate tops out at 120 ticks/s (300 bpm),
  well inside the ≥1 ms master pacing budget.
- **Recording alignment**: TRS stamps arrive in the Pico domain; add the
  current offset to place them on the Pi timeline. This is the measured
  mapping the link was designed around.
- **Transport inference**: the tick stream never stops, so transport comes
  from `0x06` state frames (rolling flag + count at the transition), not from
  stream silence. Tick count reset + rolling ⇒ START; rolling flag clear ⇒
  STOPPED (ticks still flow for timeline continuity); count continues ⇒
  CONTINUE. USB mirror emits only while rolling.
- **Overflow**: tick frames are redundant by construction — on queue overflow
  the Pico drops newest and counts (`txov`); the Pi interpolates across gaps.

## Panel transport authority (local default, Pi can take over)

In local mode (default) PLAY, PLAY_START and STOP drive transport on press:
PLAY starts when stopped (idempotent) and is ignored while rolling,
PLAY_START always restarts from tick 0, STOP always stops. REC/OVER_DUB stay
Pi policy (events only). The button EV_CONTROL still goes out for UI and
record policy, so the Pi learns every press — and therefore **must not echo
TRANSPORT for a state the Pico already announced**: hold off ~50 ms after a
transport-button event and skip the command if a matching `0x06` state frame
arrived. An echoed START would reset ticks and re-emit 0xFA milliseconds
after the local start: an audible double-start downstream. EV_TRANSPORT
remains the Pi's authoritative override (it always executes, even while
rolling).

`0x16` TRANSPORT_MODE switches authority: 0 local (default), 1 manual. In
manual mode buttons send EV_CONTROL only and the Pico waits for EV_TRANSPORT
— this is how the Pi implements count-ins and conditional starts, which are
impossible when the Pico acts first. The mode resets to local on Pico boot
and on Pi (re)connect (HELLO); the Pi re-sends MODE after HELLO when its
policy needs manual. Local keeps buttons responsive (~10 ms, works headless);
manual trades that for full Pi policy at ~15–30 ms plus Linux jitter.

## Indicator semantics (front-panel LEDs)

REC/OVERDUB are solid while armed+rolling (recording), blink at ~1 Hz while
armed but stopped (record-ready), off when disarmed; PLAY follows transport
rolling. Arming toggles on REC/OVERDUB press edges — a rule the Pi shares,
so both sides toggle on the same deduped press edge and stay in step.
`EV_LED_SET` without bit 7 partially overwrites the luminous mask and latches
an override (local refresh pauses until the next panel-driven change);
with bit 7 it syncs arm state instead. State frames carry arm bits
(`0x06[6]`, valid when length ≥ 7) on every transition, boot, hello and
arm toggle, so the Pi mirror converges without polling.

## Pico clock and controls

- Clock: always on from boot at the current tempo (default 120.00). Dynamic
  one-shot alarms at 60e6/(bpm·24) us, so tempo changes apply on the next
  tick; 0xF8 flows on both UARTs regardless of transport state. Transport
  commands still emit their bytes (START resets the tick count) and announce
  state; STOP no longer halts ticks.
- Encoder detent = 4 quadrature edges (±1.00 bpm, ±0.10 with SHIFT held).
- Buttons/nav active-low, 10 ms debounce, 1 kHz scan; UART RX is IRQ-driven
  into 128-deep stamped rings per port.
- LEDs default to mirroring REC/OVERDUB/PLAY presses; LED_SET overwrites.

## Prototype limitations (not yet implemented)

SysEx forwarding (content skipped; running-status/system-common/realtime/undefined-byte handling is byte-exact per the midi.hpp comments, with EOX preserving running status), scheduled-output musical timeline (MIDI_SEND is
immediate), ACK/retry/credit reliability, queue recovery, CS-abort recovery
beyond the watchdog, SPI rates above 1 MHz, clock jitter measurement, and
thermal/EMC behaviour. The framed loopback, debounce, parser and event tests
are host simulations, not hardware validation.

# Framed communication: current host-only demonstration

`firmware/pico/protocol.hpp` is portable C++ shared-code research, **not firmware**, a Linux driver or an established final transport protocol. `protocol_test.cpp` checks a framed encode/decode loopback and a simulated, debounced navigation press/release on the host. No Pi, Pico, physical button or SPI transaction is involved.

Current test frame is fixed 32 bytes; multi-byte integers are little-endian:

| Byte(s) | Contents |
|---|---|
| 0–1 | ASCII `MB` |
| 2 | version 1 |
| 3 | type: 1 HELLO, 2 ACK, 3 CONTROL_EVENT |
| 4–5 | sequence |
| 6 | payload length 0–18 |
| 7 | reserved, zero |
| 8–11 | nonzero session identifier |
| 12–29 | payload and zero padding |
| 30–31 | CRC-16/CCITT-FALSE over bytes 0–29 (poly 0x1021, init 0xffff, no reflection or final xor) |

The receiver rejects bad magic/version, unknown types, nonzero reserved/padding, bad CRC, invalid length and zero session. A test CONTROL_EVENT uses two bytes: control ID 1 (NAV_UP), state 1 (pressed). This does **not** define a final control ID registry or reliable delivery semantics. Sequence wrap, session establishment and uniqueness, retries, ACK payload semantics, resets, duplicate handling, boot identity, queue capacity and overflow/error reporting are not implemented.

The full planned protocol requires version negotiation; boot/session identities in both directions; acknowledgments; queue credits; CRC/error counters; bounded queues; explicit overrun/reconnect handling; monotonic timestamp exchange; and scheduled musical events. SPI is full duplex and peripheral responses require a transaction strategy, CS/READY handshake, DMA servicing, frame-resynchronisation and timing validation. A Pico timer must generate local clock events from a measured mapping to the Pi musical timeline, not forward late USB ticks. Clock accuracy, jitter and recovery after reset must be measured on hardware.

The proposed HyperPixel PIM470 uses the GPIO header and its manufacturer says to disable SPI. Retaining this framing over a USB serial link would be a **new architecture decision**, not evidence that USB meets latency or clock targets.

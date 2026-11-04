Report written to `/tmp/comprotocol-scout-report.md` (264 lines). No files written under the repo; no commits; nothing staged. Full content below.

---

# ComProtocol / PktMgr scout report (read-only, ideas only)

Source studied: `MusicTribeOpenSource/0700-AAM` @ `ComProtocol/{README.md,PktMgr/Defines.hpp,PktMgr/PktMgr.hpp,PktMgr/PktMgr.cpp}` plus its in-repo consumers (`Kick/modules/input_events/input_events.cpp`, `Kick/modules/contracts/dcl/*`, `Kick/modules/hardware/source/kick_FirmwareUpdate.cpp`, `Kick/apps/kick_device_control_test/`). `ComProtocol` is AGPL-3.0-or-later (`ComProtocol/LICENSE`); nothing below is copied from it — this is my own description of behaviour, with a few short quoted identifiers for traceability.

Repo state: `cldmnky/electronics` @ `e0606da`, branch `main`. No writes, no commits, no staged files; scratch clone at `/tmp/aam-proto`.

## 1. Packet format, field by field

Frames are built by `CPktMgr::BuildAndTransmitPacket` and parsed by a byte-at-a-time state machine in `CPktMgr::PushByteToRxFifo` (`PktMgr.cpp`).

| Offset | Field | Size | Content / constraints |
|---|---|---|---|
| 0 | start marker | 1 | ASCII `'M'` (0x4D) |
| 1 | start marker | 1 | ASCII `'T'` (0x54) — vendor initials, not a length/version marker |
| 2 | packet type | 1 | `enumPacketType`: SYSTEM, BUTTON, LED, PAD, KNOBS, MIDI, CV, SLIDER (0–7) |
| 3 | direction | 1 | 0 = request, 1 = response |
| 4 | command id | 1 | per-type sub-opcode (sys, led, pad, knob, slider command enums) |
| 5 | size | 1 | **payload length + 1** (the +1 counts the trailing CRC byte); `0` means "no payload" and jumps straight to CRC |
| 6 … 5+size-1 | payload | 0…254 | opaque bytes, no type-independent framing |
| last | CRC | 1 | CRC-8 over bytes 0…(5+payload), CRC byte excluded |

Facts worth carrying forward:

- The CRC is 8-bit MSB-first: init `0xff`, poly `0x31` (x^8+x^5+x^4+1, CRC-8/NRSC-5), no reflection, no final xor, computed by `CPktMgr::CrcGen` over the accumulated header+payload; the CRC byte is never fed back in.
- **No sequence number, no timestamp, no session id, no protocol-version byte.**
- The size field excludes itself but includes the CRC, and the transport layer re-derives frame length independently (`byteArray[5] + 7` in `input_events.cpp`), i.e. two places encode the same "+1" convention. Any drift here is a silent desync; it is the classic "length counts what?" hazard.
- Buffers are inconsistent with the 8-bit length field: the host constructs the manager with a 20-byte RX/TX buffer (`ComConfig.hpp`: `BUFFER_RX_SIZE 20`), the class also carries an unused private `ui8RxDataFifo[30]` and a 40-byte transmit buffer, and the wire field can express 254 payload bytes. Effective payload is therefore ~19 bytes, not the 254 the format allows.
- Values are pushed as `int` (`std::vector<int> data`) but narrowed to one byte on the wire with no error path, so any app value > 255 is truncated silently and no endianness rule exists (there are no multi-byte fields on the wire at all).
- Parsing is incremental and stateful: 9 states (`INIT_1, INIT_2, TYPE, DIR, ID, SIZE, DATA, CRC, EOF`), a `static std::vector<char>` CRC-input accumulator, and a `volatile bool` completion flag the caller polls. One byte per call — it cannot consume a whole frame at once.
- Robustness bug (worth *not* copying): `INIT_2` never falls back to `INIT_1` when the second byte is not `'T'`, so a stray `'M'` mid-stream parks the receiver until the caller forces a clear. Recovery is caller-driven (`ClearReceiver()`), not self-healing.

## 2. Type / registry organisation

Three layers, none of them a table:

1. **Wire type** — 8-value `enumPacketType` plus a 2-value `enumPacketDir`, both inside `CPktMgr`. Types are just an enum cast on receive: an out-of-range type byte is accepted and delivered upward, so the codec performs no validation of the type space.
2. **Command space** — `contracts::DeviceControlContracts` in `Kick/modules/contracts/dcl/dcl_contracts.h`: one enum per category (`SysCmd`, `BtnCmd`, `LedCmd`, `PadCmd`, `KnobCmd`, `SliderCmd`) plus large identifier enums for physical elements (53-entry `BtnLedSequence`, `PadSequence`, `KnobSequence`, `PadColourIds`, `PadButtonState`…). This is the de-facto "contract" shared by both ends: type = category, cmd = operation, identifier = positional `data[0..n]`.
3. **Dispatch** — `contracts::DclContractsHelper` is a bag of static boolean predicates, each a conjunction over `(pktType, pktCmd[, data[0]])`, e.g. `isSystemCommInit()` requires type SYSTEM and cmd CHECK_COMM_STATUS and `data[0] == 1`. Callers chain these in `if/else` ladders; payload fields are read through positional accessors (`getButtonCode() -> data[0]`, `getButtonState() -> data[1]`, …) with no length checks.

Consequences: no per-type argument schema, no length validation, no serializer/deserializer registry, no generated code. A new message means editing a shared enum, adding predicates *and* adding accessors — three places that must agree, with nothing enforcing agreement.

The same `HWMsg_t {pktType, pktDir, pktCmd(int), data(vector<int>)}` envelope is also JSON-serialized over a unix socket between the UI process and the process that owns the serial port, which is why the in-process struct is wider (`int`) than the wire (byte) — a lossy adaptation that costs nothing at the wire but hides range bugs.

## 3. Reliability mechanisms — essentially absent at the frame layer

- **No ACK, no retry, no retransmit, no sequence numbers, no window/credit.** A request/response *bit* exists, and there is a fully commented-out ACK construction in the receive path (`input_events.cpp` around the `if( rxMsg.pktDir == EN_PKT_REQUEST )` branch, with "Ack Msg" commented out); the response branch is literally `// Do Nothing` ("If the data is Ack (response) for the App message, ignore"). So the design anticipated an ack layer and then shipped without one.
- **Integrity only:** one CRC-8 per frame. A corrupted frame is dropped silently — no retransmit hook, no error frame, no counter.
- **No flow control.** TX is a lock-free queue (`moodycamel::ConcurrentQueue`) drained by a dedicated thread that writes to the serial port; the only backpressure is the queue growing. RX bytes arrive from a JUCE `SerialPortInputStream` change callback on another thread and are pushed into the same object's state machine. Shared receive state (`m_enFrmState`, FIFO indices, `m_enPktDir`) is `volatile`, not atomic, and the FIFO indices have no wraparound handling — correctness rests on the transport wiring being single-reader/single-writer.
- **No frame timeout.** The constructor takes a per-byte timeout in ms (60) and the parameter is *never stored or used*; a truncated frame leaves the parser parked until the caller clears it.
- **Reliability is pushed up to the application:** liveness is a ping/pong pair in the SYSTEM command space (`EN_SYS_CHECK_COMM_STATUS` with `data[0] == 1` "comm init" and `data[0] == 2` "comm live", answered by the UI, plus `EN_SYS_COMM_INIT` when the port opens); teardown is `EN_SYS_RECONNECT` (close + reopen) and `EN_SYS_SHUTDOWN`; and bulk transfer is a stop-and-wait state machine in the same space — `EN_SW_UPDATE_{INITIATE, BEGIN_TRANSFER, DATA, DATABASE, END, ACK, FAILURE, IDLE}` — carrying Intel-HEX *record* lines whose own record checksum plus MCU-side flash verification provide the real end-to-end integrity. The frame layer contributes nothing to it.

## 4. Versioning / negotiation

- **No protocol version on the wire.** A version byte does not exist; the receiver cannot tell firmware-revision drift from corruption.
- Version is discovered by an out-of-band SYSTEM command: `EN_SYS_GET_SW_VER` returns `data[0] = major`, `data[1] = minor`, which the UI concatenates into a display string and stores in preferences (`kick_AppHome.cpp`: `setSurfaceVersion(...)`). Nothing compares it, no capability/feature bits are exchanged, no mismatch causes a refusal — it is a status readout, not negotiation.
- Handshake is session/liveness only: open port → send `COMM_INIT(connected)` → UI answers `COMM_INIT` → periodic `CHECK_COMM_STATUS` 1/2 ping-pong.
- A compiled-in `typeVersionNo {main, sub, bug, dummy}` struct plus a `FLASH_VALID` magic in `Defines.hpp` identify firmware images; it is not transmitted.
- Transport parameters are hard-coded rather than negotiated: `/dev/ttymxc0`, 230400 baud, 8N1, no flow control, port path also hard-coded.

## 5. Transport coupling

- The declared abstraction is a single-value enum: `enumComChannel { EN_COM_CHANNEL_USB, EN_NUM_COM_CHANNELS }`. There is exactly one channel; the plural `EN_NUM_COM_CHANNELS` is scaffolding.
- The real transport (in `input_events.cpp`) is a **JUCE serial port** — USB CDC-ACM on the device, compiled in only under `JUCE_LINUX || __linux__`. So "USB" means a byte-stream serial device, not USB packets.
- `CPktMgr` never touches I/O: `PushByteToRxFifo`, `PopUint8FromRxFifo`, `BuildAndTransmitPacket(msg, std::vector<char>&)`, `CrcGen`, plus an `IsFrameRcvComplete()`/`SetFrameRcvComplete()` flag pair, with caller-supplied buffers. Transport-agnostic in code, but shaped by a **byte-stream** assumption: ordered delivery, asynchronous unsolicited traffic, parser-driven resync, no MTU, no transaction boundary, no clocking.
- A second hop sits above it: `HWMsg_t` is JSON-serialized over a unix-socket IPC between the UI app and the process owning the serial port (hence `int` fields).
- Implication for us: nothing in PktMgr assumes who initiates. On master-clocked SPI the slave must always have a frame ready and every transaction is symmetric; PktMgr has no notion of "the frame for this transaction", only a send queue and a receive queue with independent lifetimes.

## 6. Error reporting

- `enumComStatus { SUCCESS, TIMEOUT, INIT_INVALID, CHECKSUM_ERROR, CMD_UNKNOWN }` is **declared and never referenced anywhere in the repo** (no setter, no getter, no counter). Same for `enumReturnCode {SUCCESS, ERROR}` in `Defines.hpp`. A full error taxonomy was designed and then not wired.
- `BuildAndTransmitPacket` returns `void`; invalid input (unknown type, oversized payload, value > 255) produces a frame anyway or truncates silently. A caller cannot distinguish success from failure.
- On CRC failure the frame is dropped; the only trace is a `DBG("Data Frame Error")` behind `#if defined(DEBUG_PRINTS)`. No stats getters exist (no rx/tx/drop/crc counters), unlike frame-level counters in our design.
- Error semantics that *do* exist are application-level conventions expressed *as data*: `EN_SW_UPDATE_FAILURE` as a system subcommand, `data[0] == 1|2` liveness codes, and a UI-side assumption that certain `data[]` indices exist (no bounds checks on `data[0]`/`data[1]`, so a short frame reads past the vector).

## 7. Delta list vs our `midibrain` link

Ours (context): 32-byte fixed frames, `MB` + version 1 + type + seq16 + len + session32 + 18-byte payload + CRC-16/CCITT-FALSE; Pico = SPI slave / clock master; HELLO/ACK handshake, boot HELLO always pending, dedup by (session, sequence), idle filler = repeat last frame, READY line out; `txov`/`rxov` counters; no retry/credit layer by design; CONTROL_EVENT registry (payload[0] = kind) in `midi.hpp`.

What ComProtocol does that we do not:

1. **Direction bit (request vs response) in every header.** [defer] — with no retry layer there is nothing to pair, and a symmetric exchange already carries its own answer; revisit only if we add a true command/response op set.
2. **Application-level liveness ping/pong + reconnect/shutdown commands as first-class protocol members.** [adopt] — cheap and genuinely missing: a small control-plane kind range (e.g. `0x20+`: status, ask-reboot, reset-link) gives the Pi a way to distinguish "idle" from "wedged slave" without a new frame format. Our session change tells us the Pico rebooted; it does not let us *ask* anything.
3. **Two-level dispatch, type in the header and command adjacent to it, so filters like "is this a system message" work without touching the payload.** [reject] — our `Type` + `payload[0]` kind is the same two levels for one extra byte inside the CRC-covered region; moving kind into the header would only duplicate it.
4. **Large shared "contracts" header defining every id/state/colour constant both ends use.** [defer] — right idea, wrong scale for a prototype; if the Pi-side code starts redefining control ids, promote `midi.hpp` constants into one shared header instead of copying the 53-button enum style.
5. **Firmware-update command space with a chunked, acked transfer state machine and per-record checksums in the payload.** [defer] — out of prototype scope; the reusable pattern is "control-plane kind range with its own ack/state machine layered above the frame codec", not the specific hex-record flow.
6. **Variable-length frames with an explicit size field.** [reject] — fixed 32-byte frames are the reason our SPI slave needs no parser state and every CS assertion is exactly one frame; a length field would add a renegotiated invariant for no gain at ≤18-byte payloads.
7. **Byte-stream resync parser (magic scan + 9-state machine) with the transport duplicating framing.** [reject] — SPI chip-select gives framing for free; a parser would mostly mask master bugs, and their dual framing is where their off-by-one convention lives.
8. **8-bit CRC.** [reject] — ours is CRC-16/CCITT-FALSE over a fixed 30-byte prefix; stronger, simpler to verify, with a known check value.
9. **Open type space (unknown type bytes delivered upward for the app to sort out).** [reject] — we reject unknown types in `decode`, which is what we want when both ends are flashed together and a mismatch means a bug.
10. **Envelope struct reuse across codec and JSON IPC.** [defer] — one SPI hop, no inter-process fan-out in our prototype; widening our payload type to `int` would only reintroduce the silent truncation hazard their narrowing does.
11. **Declared-but-unwired error taxonomy** (`enumComStatus`, unused byte timeout, `void` transmit). [reject] as design, [adopt] as a rule — do not declare error paths we do not exercise: our docs already promise `txov`/`rxov`; make sure a host test asserts they increment, otherwise they rot exactly like `EN_COMM_STATUS`.
12. **A commented-out ACK layer as evidence of intent.** [reject] — confirmation that a link of this shape survives without retry; our answer (last-frame repeat + (session, sequence) dedup + READY) is strictly more than they run in production.

What we already do that ComProtocol does not (keep; do not regress): in-band protocol version byte, 32-bit session id, 16-bit sequence with duplicate suppression, canonical form checks (reserved byte zero, zero padding), CRC-16, fixed-size payload with a compile-time bound, and host-side unit tests for all of it.

## 8. Test strategy observed

- **No unit tests for PktMgr in this repo.** `ComProtocol` is vendored as a separate repository via `Kick/3rdParty.cmake` (`ExternalProject` + `juce_add_modules(...)`), so whatever tests exist are upstream and not visible here.
- The only thing that exercises the codec is `Kick/apps/kick_device_control_test/` (`kick_dcl_test_main.cpp`, ~174 lines): a JUCE GUI app that constructs one instance of *every* helper message (reconnect, shutdown, LED on/off, pad data, blink, tempo, bank, colour-by-velocity, …) and fires them through the IPC layer at a running device-control process, then quits. Assertions are `DBG` log lines and human eyes on hardware — an integration smoke harness, not a test.
- The firmware-update path has `#ifdef DEBUG_FWUPDATE` tracing only; correctness rests on hex-record checksums and MCU-side verification living in firmware that is not here.
- CI exists (`Kick/CI/azure-app-pipeline.yaml`, `azure_build.sh`) but invokes no protocol test; no fuzzing, no round-trip property tests, no negative decoder tests.

Contrast with ours: `firmware/pico/protocol_test.cpp` is a host `assert` suite covering the CRC-16 check value for "123456789" (0x29b1), encode/decode round trip, and negative cases — flipped payload byte, wrong version byte, length 19, nonzero reserved byte, valid-CRC-but-nonzero-padding, zero session, invalid type, oversized payload, plus debouncer and MIDI-parser simulations. For the codec that is a stronger story than the entire Kick repo's, and it is the piece their design lacks.

## Bottom line for the prototype

The frame layer is roughly what ours already is minus the discipline: no version, no sequence, no session, an 8-bit CRC, a length field with two definitions of itself, and buffers smaller than the field allows. Its real lesson is organisational: a **shared contracts header for ids and states**, and a **reserved control-plane command range** for liveness, reconnect, shutdown and bulk transfer, each with a tiny state machine above the codec. Of the deltas above only item 2 (liveness/control-plane kinds) and the discipline in item 11 are worth acting on here; ACK/retry, variable length and stream resync should stay out.

---

Implemented a read-only scout report; no code changes were requested or made.
Changed files: `/tmp/comprotocol-scout-report.md` (only; repo untouched).
Validation: verified the repo has 0 staged files and 0 modified tracked files after all reads (`git status --porcelain` shows only a pre-existing untracked `.pi/settings.json`, timestamped before this session's report write and not created by me — I performed zero write/edit calls against the repo path).
Open risks/questions: none blocking; note the AGPL provenance constraint on any future reuse.
Recommended next step: parent decides whether [adopt] items 2 and 11 warrant a follow-up design task against `midibrain/firmware/pico/`.
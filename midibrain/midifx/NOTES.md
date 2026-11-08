# midifx — shared MIDI FX core, pipe schema and codegen

One pipe engine for both builds: the Pico firmware and the Pi compile
`core/midifx.hpp` unchanged, so pipe behavior is defined once. Fixed capacity
throughout: no heap, no exceptions, no RTTI. The behavior DSL is an approved
non-goal.

## What is where

| Path | Role |
|---|---|
| `core/midifx.hpp` | The single pipe engine: all 16 types, fixed-capacity, header-only so the firmware (one TU) and the Pi (any TU) compile the same source. |
| `core/timing.hpp` | 24-PPQN tick math, LFO wave tables, bounded note sets, arp/harmony helpers, one clocked-pipe runtime slot. |
| `pipes.json` | Single source of truth for pipe metadata: id, key, name, short name, category, **role** (`generator`/`modifier`, the route-legality field), docs line and `params[]` (`key`, `label`, `kind`, `min`/`max` or `choices`, `default`, `unit`). |
| `wire-order.txt` | **Hand-frozen** `0x19` positional ABI. The generator refuses to emit when `pipes.json` no longer matches it. |
| `gen/gen_params.py` | python3-stdlib generator + `--check` mode. Regenerate with `python3 midifx/gen/gen_params.py`. |
| `gen/generated/midifx_params.hpp` | Generated constexpr tables for **both** builds: `kSchemaVersion`, param tables (positional `index`, keyed metadata) and `pipeDefaultParam()`. |
| `gen/generated/midifx_ui.hpp` | Host-only editor metadata (label, range, unit, choices, format, editor kind per positional parameter) that the Pi app reads generically, so a pipe type needs no app-side table. |
| `gen/generated/schema_tests.cpp` | Generated golden-consistency checks (unique keys, defaults in range, `index == array position`). |
| `gen/generated/pipes-docs.md` | Generated docs table fragments (per type and per param). |
| `tests/test_core.cpp` | The one behavior suite for both builds: per-pipe expectations, channel scope, config validation, the dropped-event counter, seed determinism and the schema defaults that bind `pipes.json` to observed behavior. |
| `tests/test_schema.cpp` | Compiles the generated checks and pins the frozen wire positions/schema version. |
| `tests/test_codegen.py` | Regenerate-and-diff alarm: stale output fails, a reordered or out-of-range schema is refused. |

## Core contract

- `PipeChain` owns one route chain. `begin(true)` enables it (a fresh chain is
  disabled, matching the firmware's RAM-only routes); `append` /
  `setPipeChunk` are the `0x19` configuration path (chunked positional params);
  `configure()` is the whole-chain replace path and enables the chain in one
  step (the Pi's router, not the engine, owns route enable).
- The engine owns **no clock and no transport state**. Every call injects the
  caller's timeline: `nowMs` (monotonic ms, wrap-safe) and, for `clockTick()`,
  an absolute 24-PPQN `tickIndex`. The Pico feeds its alarm tick counter; the Pi
  feeds the tick derived from its disciplined USB-clock phase. A transport
  START that resets the tick counter is handled by the grid resync inside
  `stepSequencer()`/`stepLfo()`.
- A pipe's positional `params` ARE the session `params[]` order and the `0x19`
  offsets. A missing offset uses the generated schema default, an extra offset
  is ignored. `setPipeChunk` enforces the wire's chunk rules.
- `PipeConfig::chMask` is the per-pipe channel scope: matching channels enter
  the pipe, every other message passes that pipe unchanged in stream order;
  `0xFFFF` = all, `0` = inert. The generated schema owns the default
  (`generated::kChannelScope`). The mask travels on the `0x19` offset-0 chunk
  (see "Wire format" below), so a Pico-owned TRS chain applies it too; a
  continuation chunk keeps the established scope.
- Capacity caps are explicit: 8 pipes/chain, 10 params/pipe, 64
  events/output evaluation, 64 pending note-offs, 16 held / 16 sounding notes
  per clocked pipe, 32 arp pattern entries, 24 harmonizer refcounts, 32 delay
  echoes/pipe. `PipeOutput::dropped` counts events past the output cap; a
  caller that ignores it silently loses events.
- Stateless pipes (1..11) transform the routed message in place. Clocked pipes
  (12..16) also evaluate on the caller's tick grid; every generated event
  re-enters the chain after the pipe that produced it.

## Pipe roles and the two pseudo-source route kinds

Every pipe carries `role` in `pipes.json`. The generated table exposes
`generated::PipeRole`, `generated::pipeIsGenerator(id)` and `pipeRoleName()`;
both route engines use the same predicate, so role legality cannot drift
between the Pico and the Pi.

| Pipe | Role | Why |
|---|---|---|
| CcLfo (14) | **generator** | Emits its own CC stream on the tick grid; consumes no input. |
| Arp (12) | modifier | Arpeggiates held notes from the input; no held notes, no output. |
| Repeater (13) | modifier | Retriggers held input notes; no held notes, no output. |
| Harmonizer (15) | modifier | Adds voices to incoming notes. |
| MidiDelay (16) | modifier | Echoes incoming events. |
| Filter..Transform (1..11) | modifier | Every stateless pipe filters, maps or schedules an incoming message. |

A **generator route** (`session::kSrcGenerator` = -1) evaluates a chain whose
HEAD is generator-role: `PipeChain::headIsGenerator()`, the Pico
`RouteChains::setPipeChunk` and the Pi `Router::setGeneratorPipes` all refuse a
modifier head. The chain emits on `clockTick()` with no `process()` input. The
Pico feeds its 24-PPQN alarm tick (`drain_route_engines()`); the Pi feeds its
disciplined USB-clock phase through `Router::clockTick(tickIndex, nowMs, …)`.
A modifier after the head shapes the generated stream because generated events
re-enter the chain after the producing pipe (`runGenerated`).

An **output insert** (`session::kSrcOutputInsert` = -2) is the last stage on a
destination: a routed message runs its source route chain, then insert slot 0,
then 1, … in route order before the wire. The Pico route graph
(`firmware/pico/route_chain.hpp`) and the Pi `Router` implement the same order
for immediate messages and for time-generated output (NoteLength/MidiDelay
echoes, clocked pipes, generator output). Inserts are per destination, not per
source, so their state is shared by every sender to that output. Ordering is
observable and tested: a Transform at slot 0 followed by an inverse Transform
at slot 1 proves slot order, and a MidiDelay insert delays the routed note/CC
stream while the dry events still pass.

`role` is route metadata, not wire data: it changes which chain head is legal,
never a positional parameter, so `schemaVersion` stays 1. The generated
`schema_tests.cpp` pins that a generator is always a clocked pipe.

MidiDelay echoes only note-on/note-off/CC. It does not move realtime clock
bytes: the firmware's clock scheduler emits those before routes, and the
per-output `0x1B` `syncDelayMs` remains the clock-shift mechanism. That is the
route split behind "timing on a sync channel"; echoing realtime from MidiDelay
would be an engine decision, not a route-kind detail.

## Firmware integration

- `firmware/pico/pipe_event.hpp` is the `MidiMessage → PipeEvent` adapter (no
  behavior), used by `main.cpp` and the host wire test.
- `main.cpp` uses `mnky::midifx::PipeChain`/`PipeOutput` directly and
  static_asserts the wire limits against the core. The boot banner reports
  `midi-fx-schema=v<kSchemaVersion>` (the constant both builds consume).
- `firmware/pico/protocol_test.cpp` keeps the wire/panel/clock suites and the
  `0x18/0x19/0x1A/0x1B` round trips; the pipe behavior expectations live in
  `midifx/tests/test_core.cpp`.

### Firmware size budget (pico2, MinSizeRel, arm-none-eabi-size)

| text | data | bss |
|---|---|---|
| 57316 | 0 | 125452 |

Section detail: `.text` 40248, `.rodata` 9264, `.data` 3648, `.bss` 125180 (the
`size` total adds the remaining zero-initialised sections). The bss is
dominated by the route graph: `RouteChains` holds 14 `PipeChain`
instances (4 TRS routes, 2 generator routes, 8 insert slots) at 8416 B each =
117824 B, plus the per-output TX schedulers and the UART/SPI rings. `.rodata`
carries the generated schema tables and their strings (labels/docs/choices).
Worst-case engine stack is `runFrom` 592 B (measured with arm-none-eabi
`-fstack-usage`; the per-pipe scope gate appends in place, so it adds no
buffer), well inside the Pico's 2 KB stack. 2 MB flash and 520 KB SRAM on the
RP2350 leave headroom. No heap, no dynamic allocation; `PipeConfig` is 48 B, a
`PipeChain` 8416 B.

### Wire format: `0x19` channel scope

`EV_MIDI_ROUTE_PIPE` ends with a 2-byte `chMask` LE16 after the positional
params: header 8 bytes, `2*count` param bytes, then the mask
(`length = 10 + 2*count`). An 18-byte payload therefore carries at most
`route_pipe_param_frame_max` = **4** params per frame, so a five-param pipe
needs two frames (the Pi app chunks at 4). The mask takes effect on the
offset-0 chunk; the firmware's `setPipeChunk(..., chMask)` ignores it on a
continuation, like the bypass byte. The decoder also accepts the mask-less form
(`length = 8 + 2*count`, up to 5 params), which means `chMask = 0xFFFF`. An
older decoder rejects the mask-bearing length as a bad frame, so Pi and Pico
builds move together.

End-to-end path: `session::MidiPipe::chMask` -> `LinkClient`
(`pi/app/link_client.cpp`) -> `PicoLink::midiRoutePipe(..., chMask)` /
`mb::pack_midi_route_pipe(..., chMask)` (`pi/link/link.cpp`) -> firmware
`mb::unpack_midi_route_pipe` -> `PipeChain::setPipeChunk` -> `runFrom` scope
gate. `kMidiRoutePipeCarriesChannelScope` is `true`; the resend cache keeps the
mask, so a Pico reboot/RESET_LINK reproduces it. `pipes.json` carries the field
as `channelScope` and the generator emits `kChannelScope` (key, range, default,
docs) next to the param tables; the firmware static_asserts the wire default
(`route_pipe_channel_mask_default`) matches it.

## Schema, wire compat and codegen

- `schemaVersion` is `1`. A change to a param, default or positional order is a
  deliberate edit: bump `schemaVersion` in `pipes.json`, edit `wire-order.txt`
  if the order moved, regenerate, and commit the generated diff.
- `gen_params.py --check` is the drift gate: it fails loudly when the committed
  generated files do not match `pipes.json`/`wire-order.txt`. `make midifx`
  (and therefore `make host`) runs it, the unittest alarms and both suites.
- The generated test asserts defaults are inside their range, keys are unique,
  and every `ParamSpec::index` equals its array position. The frozen wire
  positions are pinned again in `tests/test_schema.cpp`.
- Both builds read the same generated header. The firmware uses
  `generated::pipeDefaultParam()` as the missing-param fallback; the Pi UI reads
  `generated::kPipes` plus the host-only `generated/midifx_ui.hpp` for
  editors/labels/ranges/units. The firmware never parses JSON and never includes
  the UI header.

## Schema decisions

The schema takes `session.hpp` as authority for order, kind and ranges and the
engine behavior as authority for defaults (a default is what a missing `0x19`
offset does; changing it changes live behavior). Where the sources disagreed,
the schema settled on these values:

1. **Chance is a drop chance.** `percent` drops note-ons. The schema label is
   **"DROP CHANCE"**; "PASS CHANCE" would invert the meaning.
2. **Arp division default `0`** (`1/4`): the engine fallback, so a missing
   offset keeps its behavior.
3. **Arp swing default `50`** (straight).
4. **Arp gate default `50`** (%).
5. **Repeater division default `0`** (`1/4`).
6. **CcLfo division default `0`** (`1/4`).
7. **Harmonizer interval defaults `0`** for `i1..i4`; voices default `0`, so the
   pipe is silent until configured.
8. **MidiDelay defaults `0/0/0` ms/repeats/falloff** (off).
9. **NoteLength max `60000` ms** (the engine clamp; `session.hpp` states no
   maximum).
10. **Transform 14-bit fields**: the engine accepts bend match/out values up to
    `16383`; schema max `16383` (kind-dependent behavior is documented, not
    encoded).
11. **Per-pipe channel scope (`chMask`)** travels on the wire: `0x19` carries
    the mask after the params and the firmware applies it through
    `setPipeChunk`. It costs the 4-param chunk limit described above.

The schema has one `default`, defined as the missing-param behavior: it is what
`pipeDefaultParam()` returns for an offset that never arrived.

## Test ownership

`test_core.cpp` covers every expectation of the Pi suite
`pi/midi/test_midifx.cpp`: the per-pipe channel-scope section (`channelScope()`),
the `seed()`/`empty()` accessors, chance clamping, the
reconfigure-rewinds-the-stream rule, the 8-pipe/9-pipe and `configure(nullptr)`
limits, the empty-chain malformed-size rule, chain order, the NoteLength
deadline replacement and 60 s clamp, the swapped Transform window, the sustain
output-cap counter and the Transform conversions. The Pi links this core through
`pi/midi/midifx.hpp`, so every type evaluates on both sides.

Route-kind expectations are split by what each test owns: `test_core.cpp` owns
pipe behavior and the generator-role table; the firmware `protocol_test.cpp`
owns the 0x18/0x19 pseudo-source wire, generator-head refusal, insert order and
the no-input generator path (`RouteChains`); `pi/midi/test_midi.cpp` owns the Pi
Router equivalents (ownership by dst, generator `clockTick`, insert slot order,
generator-through-insert, MidiDelay insert timing); `pi/link/test_link.cpp`
owns pseudo-source configuration and resend; `pi/session/test_session.cpp` owns
apply/capture forwarding. No expectation is duplicated across those suites; the
core suite runs once.

## Validation

Wire/scope tests: `firmware/pico/protocol_test.cpp` round-trips a mask-bearing
`0x19` frame and a mask-less 5-param frame, and drives a scoped Filter through
two chunks; `pi/link/test_link.cpp` checks the encoder length, the mask bytes,
the emulator state and the resend after reboot; `test_schema.cpp` plus the
generated checks pin `kChannelScope` (0..65535, default 0xFFFF).

```
python3 midifx/gen/gen_params.py --check          # generated tables current
python3 -m unittest discover -s midifx/tests -p 'test_*.py'
/usr/bin/c++ -std=c++20 -Wall -Wextra -Werror -pedantic -I. midifx/tests/test_core.cpp
/usr/bin/c++ -std=c++20 -Wall -Wextra -Werror -pedantic -I. midifx/tests/test_schema.cpp
make midifx      # all of the above, wired into `make host`
make host        # firmware protocol_test (wire + route kinds) + midifx suites
make firmware    # PICO_BOARD=pico2 (RouteChains: generator/insert chains)
```

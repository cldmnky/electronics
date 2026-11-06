# midifx — shared MIDI FX core, pipe schema and codegen

Status: **phases 1–2 of `docs/midifx-library-plan.md` implemented**. The behavior
DSL stays out of scope. The Pi-side adoption of the core is deliberately
deferred (a parallel worker owns `pi/midi`); nothing in `pi/` is changed here.

## What is where

| Path | Role |
|---|---|
| `core/midifx.hpp` | The single pipe engine: all 16 types, fixed-capacity, no heap, no exceptions/RTTI. Header-only so the firmware (one TU) and the Pi (any TU) compile the same source. |
| `core/timing.hpp` | 24-PPQN tick math, LFO wave tables, bounded note sets, arp/harmony helpers, one clocked-pipe runtime slot. |
| `pipes.json` | Single source of truth for pipe metadata: id, key, name, short name, category, docs line and `params[]` (`key`, `label`, `kind`, `min`/`max` or `choices`, `default`, `unit`). |
| `wire-order.txt` | **Hand-frozen** `0x19` positional ABI. The generator refuses to emit when `pipes.json` no longer matches it. |
| `gen/gen_params.py` | python3-stdlib generator + `--check` mode. Regenerate with `python3 midifx/gen/gen_params.py`. |
| `gen/generated/midifx_params.hpp` | Generated constexpr tables for **both** builds: `kSchemaVersion`, param tables (positional `index`, keyed metadata) and `pipeDefaultParam()`. |
| `gen/generated/schema_tests.cpp` | Generated golden-consistency checks (unique keys, defaults in range, `index == array position`). |
| `gen/generated/pipes-docs.md` | Generated docs table fragments (per type and per param). |
| `tests/test_core.cpp` | The ONE behavior suite: the firmware expectations (moved out of `firmware/pico/protocol_test.cpp`) plus the Pi engine's diagnostics, per-pipe channel-scope expectations and schema-default checks. |
| `tests/test_schema.cpp` | Compiles the generated checks and pins the frozen wire positions/schema version. |
| `tests/test_codegen.py` | Regenerate-and-diff alarm: stale output fails, a reordered or out-of-range schema is refused. |

## Core contract (phase 1)

- `PipeChain` owns one route chain. `begin(true)` enables it (a fresh chain is
  disabled, matching the firmware's RAM-only routes); `append` /
  `setPipeChunk` are the `0x19` configuration path (chunked positional params);
  `configure()` is the whole-chain replace path the Pi will use and enables the
  chain in one step (its router, not the engine, owns route enable).
- The engine owns **no clock and no transport state**. Every call injects the
  caller's timeline: `nowMs` (monotonic ms, wrap-safe) and, for `clockTick()`,
  an absolute 24-PPQN `tickIndex`. The Pico feeds its alarm tick counter; the Pi
  will feed the tick derived from its disciplined USB-clock phase. A transport
  START that resets the tick counter is handled by the grid resync inside
  `stepSequencer()`/`stepLfo()`.
- A pipe's positional `params` ARE the session `params[]` order and the `0x19`
  offsets. A missing offset uses the generated schema default, an extra offset
  is ignored. `setPipeChunk` keeps the firmware's exact chunk rules.
- `PipeConfig::chMask` implements the per-pipe channel scope (matching channels
  enter the pipe, every other message passes that pipe unchanged in stream
  order; `0xFFFF` = all, `0` = inert). The generated schema owns the default
  (`generated::kChannelScope`). The mask travels on the `0x19` offset-0 chunk
  (see "Wire format" below), so a Pico-owned TRS chain applies it too; a
  continuation chunk keeps the established scope.
- Capacity caps are unchanged and explicit: 8 pipes/chain, 10 params/pipe,
  64 events/output evaluation, 64 pending note-offs, 16 held / 16 sounding
  notes per clocked pipe, 32 arp pattern entries, 24 harmonizer refcounts,
  32 delay echoes/pipe. `PipeOutput::dropped` counts events past the output cap
  (the firmware previously truncated silently).
- Wave-2 behavior is live in the core (the Pi's old parse-only rejection was a
  temporary guard; it disappears when `pi/midi` adopts the core).

## Firmware integration

- `firmware/pico/midi_pipe.hpp` and `midi_clock_pipes.hpp` are **deleted**. The
  core is the only implementation; `firmware/pico/pipe_event.hpp` is a 20-line
  `MidiMessage → PipeEvent` adapter (no behavior), used by `main.cpp` and the
  host wire test.
- `main.cpp` uses `mnky::midifx::PipeChain`/`PipeOutput` directly and
  static_asserts the wire limits against the core. The boot banner reports
  `midi-fx-schema=v<kSchemaVersion>` (the constant both builds consume).
- `firmware/pico/protocol_test.cpp` keeps the wire/panel/clock suites and the
  `0x18/0x19/0x1A/0x1B` round trips; the pipe behavior expectations moved to
  `midifx/tests/test_core.cpp`.

### Measured firmware size delta (pico2, MinSizeRel, arm-none-eabi-size)

| Build | text | data | bss |
|---|---|---|---|
| before (branch main, old headers) | 44152 | 0 | 39972 |
| after (shared core + generated schema + 0x19 chMask) | 50176 | 0 | 40100 |
| **delta** | **+6024** | **0** | **+128** |

Section detail: `.text` 34800 → 34984 (**+184 bytes of code**: the per-pipe
`chMask` gate, the schema-default lookup and the wire mask decode), `.rodata`
1744 → 7584 (**+5840 bytes**), `.bss` 39972 → 40100 (**+128 bytes** = 4 chunks
x 8 pipes x 4 bytes for `PipeConfig::chMask` and its alignment). Worst-case
engine stack is
`runFrom` 592 B (measured with arm-none-eabi `-fstack-usage`; the per-pipe
scope gate appends in place, so it adds no buffer), the same order as the old
engine under the Pico's 2 KB stack. The flash delta is the
generated schema tables and their strings (labels/docs/choices) linked into the
firmware; 2 MB flash on the RP2350 leaves ample headroom. No heap, no dynamic
allocation; `PipeConfig` is 48 B, a `PipeChain` 8416 B (was 8292 B).

### Wire format addendum: `0x19` channel scope

`EV_MIDI_ROUTE_PIPE` now ends with a 2-byte `chMask` LE16 after the positional
params: header 8 bytes, `2*count` param bytes, then the mask
(`length = 10 + 2*count`). An 18-byte payload therefore carries at most
`route_pipe_param_frame_max` = **4** params per frame (the old 5-param chunk
needs one more frame; the Pi app chunks at 4). The mask takes effect on the
offset-0 chunk; the firmware's `setPipeChunk(..., chMask)` ignores it on a
continuation, and a continuation frame's mask is ignored like its bypass byte.
Backward tolerance is in the decoder: a legacy frame (`length = 8 + 2*count`,
up to 5 params, no mask) still decodes and means `chMask = 0xFFFF`. A new Pi
with old firmware fails closed (the old decoder rejects the new length as a bad
frame), so Pi and Pico builds move together.

End-to-end path: `session::MidiPipe::chMask` -> `LinkClient`
(`pi/app/link_client.cpp`) -> `PicoLink::midiRoutePipe(..., chMask)` /
`mb::pack_midi_route_pipe(..., chMask)` (`pi/link/link.cpp`) -> firmware
`mb::unpack_midi_route_pipe` -> `PipeChain::setPipeChunk` -> `runFrom` scope
gate. `kMidiRoutePipeCarriesChannelScope` is now `true`; the resend cache keeps
the mask, so a Pico reboot/RESET_LINK reproduces it. `pipes.json` carries the
field as `channelScope` and the generator emits `kChannelScope` (key, range,
default, docs) next to the param tables; the firmware static_asserts the wire
default (`route_pipe_channel_mask_default`) matches it.

## Schema, wire compat and codegen (phase 2)

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

## Schema vs source disagreements (findings)

The schema takes `session.hpp` as authority for order, kind and ranges and the
current engine behavior as authority for defaults (a default is what a missing
`0x19` offset does; changing it would change live behavior). Disagreements:

1. **Chance is a drop chance.** `session.hpp`/engine: `percent` drops note-ons.
   `pipe_params.cpp` labelled it **"PASS CHANCE"** (implies the opposite). The
   schema label is **"DROP CHANCE"**.
2. **Arp division default**: engine fallback `0` (`1/4`); UI table `1`
   (`1/8`); the plan's illustrative JSON example showed `2` (`1/16`). Schema
   uses `0` to preserve missing-param behavior.
3. **Arp swing default**: engine `50` (straight); UI table `0`. Schema `50`.
4. **Arp gate default**: engine `50` (%); UI table `100`; plan example `50`.
   Schema `50`.
5. **Repeater division default**: engine `0` (`1/4`); UI table `1` (`1/8`).
   Schema `0`.
6. **CcLfo division default**: engine `0` (`1/4`); UI table `1` (`1/8`).
   Schema `0`.
7. **Harmonizer interval defaults**: engine `0` for `i1..i4`; UI table
   `0,4,7,12`. Schema `0` (voices default `0`, so silent until configured).
8. **MidiDelay defaults**: engine `delayMs 0 / repeats 0 / falloff 0` (off);
   UI table `250 ms / 1 / 50 %`. Schema `0/0/0`.
9. **NoteLength maximum**: engine clamps at `60000 ms`; UI table tops out at
   `5000 ms`; `session.hpp` states no maximum. Schema max `60000`.
10. **Transform 14-bit fields**: the engine accepts bend match/out values up to
    `16383`; the UI table caps match/out fields at `127`. Schema max `16383`
    (kind-dependent behavior is documented, not encoded).
11. **Per-pipe channel scope (`chMask`) is now on the wire** (closed in the
    phase-2 addendum): `0x19` carries the mask after the params, the firmware
    applies it through `setPipeChunk`, and `kMidiRoutePipeCarriesChannelScope`
    is true. The cost is a 4-param chunk limit (legacy 5-param frames are still
    decoded as all-channels); the removed `MidiGlue` "not applied on the Pico"
    warning was the previous state.
12. **Plan example vs behavior** (not a code disagreement): the plan's example
    `pipes.json` listed Arp defaults `div 2`, `gate 50`, `swing 50`; it is
    marked "Example", and the schema uses the behavior defaults above.

The UI table also conflates "default for a newly added pipe" with "missing
wire-param behavior". The schema has one `default`, chosen as the missing-param
behavior; deciding whether phase 3 keeps that as the new-pipe default or adds a
separate UI default is a phase-3 decision (out of scope here).

## Parity with `pi/midi/test_midifx.cpp`

Every expectation in the Pi suite is covered by `tests/test_core.cpp`: the
per-pipe channel-scope section was ported verbatim (`channelScope()`), together
with the `seed()`/`empty()` accessors, chance clamping and the
reconfigure-rewinds-the-stream rule, the 8-pipe/9-pipe and `configure(nullptr)`
limits, the empty-chain malformed-size rule, chain order, the NoteLength
deadline replacement and 60 s clamp, the swapped Transform window, the sustain
output-cap counter and the Transform conversions. The one Pi-only section not
ported is `wave2Rejection()`: it asserts the Pi's temporary Wave-2 refusal,
which the core replaces with the live Wave-2 engine (the plan's "free win").
That rejection disappears when `pi/midi` adopts the core.

## Deferred / open

- **Pi adoption**: `pi/midi/midifx.*` still has the Wave-1 copy and the Wave-2
  rejection. Adopting the core is a link/include change plus calling
  `begin(true)`/`configure()`; the Pi suite can then run `test_core.cpp`.
- **Schema version in the status frame**: the plan asks for the version to be
  reported in status. The firmware reports it in the boot log; a real status
  field needs `protocol.hpp` + `pi/link` changes (read-only in this lane), so it
  is left for the Pi-link work item.
- **`pipe_params.cpp` is retired** (phase 3): `gen_params.py` emits
  `generated/midifx_ui.hpp` (label, range, unit, choices, format, editor kind per
  positional parameter) and `pi/app/pipe_ui.cpp` is a generic adapter over it, so
  a new pipe type needs no app table and no QML. `tests/test_codegen.py` proves
  that with a throwaway fixture type (`tests/fixtures/test-pipe-type.json`) that
  is never shipped.
- No behavior DSL (approved non-goal).

## Validation

Wire/scope tests: `firmware/pico/protocol_test.cpp` round-trips a mask-bearing
`0x19` frame and a legacy mask-less 5-param frame, and drives a scoped Filter
through two chunks; `pi/link/test_link.cpp` checks the encoder length, the
mask bytes, the emulator state and the resend after reboot; `test_schema.cpp`
plus the generated checks pin `kChannelScope` (0..65535, default 0xFFFF).

```
python3 midifx/gen/gen_params.py --check          # generated tables current
python3 -m unittest discover -s midifx/tests -p 'test_*.py'
/usr/bin/c++ -std=c++20 -Wall -Wextra -Werror -pedantic -I. midifx/tests/test_core.cpp
/usr/bin/c++ -std=c++20 -Wall -Wextra -Werror -pedantic -I. midifx/tests/test_schema.cpp
make midifx      # all of the above, wired into `make host`
make host        # firmware protocol_test + midifx suites
make firmware    # PICO_BOARD=pico2
```

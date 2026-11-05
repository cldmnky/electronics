# MIDI FX library plan — shared core + JSON pipe schema

Status: **approved for implementation (phases 1–2 queued 2026-11-06)**.
Owner decisions recorded below; behavior DSL explicitly out of scope.

## Problem

Pipe knowledge is duplicated in four places that can drift:

| What | Where |
|---|---|
| Param order/ranges (positional ints) | `pi/session/session.hpp` comment tables |
| Wave-1 + Wave-2 behavior | `firmware/pico/midi_pipe.hpp` + `midi_clock_pipes.hpp` |
| Wave-1 behavior (second copy) | `pi/midi/midifx.*` (Wave-2 parse-only Pi-side) |
| UI param editors | `pi/app/pipe_params.cpp` (hand-written tables) |

Docs (`protocol.md`, `USER-MANUAL.md`) restate param meanings again.

## Target architecture (3 artifacts)

1. **`midibrain/midifx/` — shared C++ core** (single behavior implementation):
   pure, dependency-free, fixed-memory (firmware discipline: no unbounded
   allocation, no exceptions), compiled by both `firmware/pico/` (arm-none-eabi)
   and `pi/midi/` (host). All 16 pipe behaviors with injected tick/transport
   context (Pico feeds alarm ticks; Pi feeds the disciplined USB-clock phase).
   Win: Wave-2 pipes become active on Pi-owned USB chains (today parse-only).

2. **`midifx/pipes.json` — pipe definition schema** (single source of truth):
   per type: id, key, name, category (stateless/clocked), docs line, and
   `params[]: {key, label, kind (int/bool/enum), min, max, default, unit,
   choices}`. Example:

   ```json
   { "id": 12, "key": "arp", "category": "clocked",
     "params": [
       {"key": "div", "kind": "enum", "choices": ["1/4","1/8","1/16","1/32"], "default": 2},
       {"key": "type", "kind": "enum", "choices": ["up","down","updown","random","chord"], "default": 0},
       {"key": "octaves", "min": 1, "max": 4, "default": 1},
       {"key": "swing", "min": 0, "max": 100, "unit": "%", "default": 50},
       {"key": "accent", "min": 0, "max": 100, "unit": "%", "default": 0},
       {"key": "gate", "min": 1, "max": 100, "unit": "%", "default": 50}] }
   ```

3. **Codegen at build time — no JSON parsing on the Pico.** A host script
   (`midifx/gen/`, python3) turns `pipes.json` into: constexpr param tables
   for BOTH builds (same generated file — cannot diverge), UI metadata that
   retires hand-written `pipe_params.cpp` (editors, labels, units, ranges,
   auto MIDI-learn paths `midifx.<route>.p<idx>.<key>`), the docs tables,
   and consistency tests (defaults in range, keys unique, wire order stable).
   Firmware never parses JSON at runtime (flash/RAM/crash surface); both
   sides read identical generated data.

## Deliberate non-goal: behavior DSL

"Define pipes in JSON" covers interfaces/config, NOT behavior. A declarative
behavior DSL (mini PatchEngine) is rejected: behavior (arp scheduling, LFO
tables, delay queues) is where correctness bugs live — it deserves C++ with
one test suite, not an interpreter with its own tests running on a device
with no debugger. Future option only if users need custom pipe types.

## Key decisions

- **Session JSON shape unchanged** (`{t, bypass, chm, params[]}`); the schema
  describes it, does not extend it. No session v3. `chm` stays a field;
  schema params stay positional = wire order for `0x19`.
- **Wire compat**: schema order IS `0x19` param order (up to 10 int16).
  Tolerance mirrors the session reader: missing params default, extra params
  ignored, schema version constant generated into both builds and reported
  in status.
- **Hot params vs structure**: param changes apply live (arp rate mid-play);
  chain structure edits (add/remove/reorder) keep current state-reset
  semantics. UI distinguishes the two (live vs "resets chain" warning).
- **Presets (P2)**: chain export/import as JSON files (Midihub pipeline-file
  analog); per-route chains travel as named files inside projects.

## Migration phases

1. **Converge** (heaviest): extract `midifx/core/` from
   `pi/midi/midifx.*` + `firmware/pico/midi_pipe*.hpp`; both sides call it;
   existing test suites green at every step (they encode identical
   expectations). Pi-side adoption may lag one lane-free window if a
   parallel worker owns `pi/midi` — the core must compile standalone so
   adoption is a link change.
2. **Schema + codegen**: author `pipes.json` from current tables; generate
   C++ tables + tests; verify generated == current behavior (golden tests
   from the UF2 family-ID lesson).
3. **Schema-driven UI**: `pipe_params.cpp` retires; editors/units/learn
   paths generated; Wave-2 params editable everywhere.
4. **Presets**: chain export/import.
5. **Free win from phase 1**: Wave-2 pipes on Pi-owned USB chains.

## Risks

- Toolchain delta (arm-none-eabi GCC vs Apple clang): conservative C++20
  subset — the existing headers already live in both worlds; follow style.
- Firmware budget: bss ~40 KB with chain state; MEASURE the convergence
  delta (report, don't guess).
- Codegen drift: CI test = regenerate + diff must be empty.
- Mid-migration divergence: freeze both implementations during phase 1;
  port with tests green per commit.

Report written (read-only scout; nothing touched in the `electronics` repo).

Implemented: architecture scout of AudIO (Behringer KICK / BE 0700-AAM) — cloned to `/tmp/aam-audio`, read `AudIO/README.md`, `AudIO/CMakeLists.txt`, `AudIO/.clang-format`, the core (`IProcessable`, `Object`, `Buffer`, `MemoryPool`, `ObjectFactory`, `Design`/`DesignImpl`, `objectMeta`, `Chain`, `Bus`, `PatchQueue`/`SPSCQueue`, `MeterEngine`, `RampedParameter`, device layer), the Airwindows registration path, the `Daw/` layer, all five `Example/` programs, and the `Kick/` host seams; compared against `midibrain/pi/ARCHITECTURE.md`. Report saved to `/tmp/aud-audio-scout-report.md` (251 lines) and reproduced below.

Changed files: none in the repo (`git status` shows only the pre-existing untracked `.pi/settings.json`, harness config, not mine); one new file `/tmp/aud-audio-scout-report.md`.

Validation: `git status --porcelain` in `.../electronics` → only `?? .pi/settings.json`; `wc -l` on report = 251; every claim traced to a specific file/class during reading.

---

# AudIO (Behringer KICK / BE 0700-AAM) — architecture scout report

Source studied read-only: `git clone --depth 1 https://github.com/MusicTribeOpenSource/0700-AAM.git /tmp/aam-audio`
(AudIO at `AudIO/AudIO/`, host app at `Kick/`). Nothing written to the local `electronics` repo. **License: AudIO is
AGPL-3.0-or-later** (`AudIO/LICENSE`, per-file SPDX headers); bundled Airwindows effects are MIT. This report is architecture
description in my own words; the only verbatim material is two short interface/registration declarations (< 5 lines each,
path cited). Nothing here is a transplant source.

---

## 1. Module map (one line per major part)

| Path | Role |
|---|---|
| `AudIO/AudIO.h` | JUCE-module declaration + compile-time feature switches (`AUDIO_DAW_CORE`, `AUDIO_OBJECT_*` per-object toggles); acts as the umbrella header. |
| `AudIO/CMakeLists.txt` | Three static libs (`AudIO_Core`, `DAW_Core`, `AirWindows_plugins`); C++17, and on UNIX `-w -ffast-math -Ofast` (warnings off, fast-math on). |
| `Includes/` | Core vocabulary: `IProcessable`, `Object` (base node), `Buffer<float>` (port/patch model), `MemoryPool<float>` (fixed audio-block pool), `ObjectFactory` (type→creator registry + metadata), `RampedParameter`, `Design` (public graph API), `DeviceManager`/`IAudioDevice`, `AudioDataTypes` (`ParamInfo`, `ObjectInfo`, `Meter`, `BusOrChain` param enum). |
| `Core/` | Graph engine: `DesignImpl` (private impl of `Design`), `objectMeta` (node = Object + Buffer + adjacency), `Chain` (serial insert chain), `Bus` (stereo summing node), `IChain` (chain/bus shared interface), `insertMeta`, `PatchQueue` (SPSC patch-command queue), `SPSCQueue`, `MeterEngine`, `Console` (debug REPL thread). |
| `Objects/` | Leaf DSP nodes: `SineWave`, `ADSR`, `Mixer`, `VolumeAndPan`, `LevelMeter`, `WhiteNoise`, `SFSynth` (TinySoundFont), `PlayBack`, `AudioRecorder`, `AudioSampler` — each self-registers. |
| `Dsp/`, `Devices/` | DSP primitives (`adsr.h`, `filter.h`, `lfo.*`, `pan.h` with 7 pan laws) and the example device layer (`minisdl_audio.c` + `StereoOut`/`StereoInOut` `IAudioDevice` impls). |
| `Daw/` | Mixer/DAW layer: `DawEngine` (a `Design` + bus manager + devices + recorder + sequences), `DawBus`/`DawBusManager`, `DawRouting` (bus types/widths/IDs/sends), `DawInserts` (4 insert slots, mixin), `DawTime`/`DawSequence`/`DawTrack`/`DawNote` (compiled out by default), `DawRecorder`. |
| `airwindows/` | 329 Airwindows effects, each in its own namespace, textually included into two TUs (171 + 158); `audioeffectx.h` adapts the VST2-shaped effect API to `AudIO::Object`. |
| `Example/` | `BasicUsage`, `DynamicPatching` (live re-patching every 5 s), `Subdesign` (nested `Design` as a node), `Daw`, `Synth` (ImGui + minisdl standalone host). |
| `AudIO/.clang-format` | 4-space indent, custom brace wrapping (braces on their own line for functions/classes/if/else), spaces inside parens, `ColumnLimit: 0` (no wrapping), `NamespaceIndentation: All`, `Standard: c++17`. |

Layering: `Object` (leaf) → `Design` (graph) → `Daw*` (console-style mixer built only from Design primitives) → app
(`Kick/`, JUCE, ~20 modules under `Kick/modules/`).

---

## 2. Effect / object interface shape (described, not copied)

Two levels, both tiny. **Processing contract** (`Includes/IProcessable.h`) — `prepareToPlay(sampleRate, blockSize)`,
`process(Buffer<float>&)`, `teardown()`. The single `Buffer` argument carries *both* the node's input port pointers and its
output port pointers, so a node reads inputs and writes outputs through one object; in-place processing is allowed and
common.

**Node contract** (`Includes/AudioObject.h`, class `Object : IProcessable`) adds identity (monotonic
`uint32_t` id, `getType()` string, mutable name); shape (`inputCount()`/`outputCount()`, optional
`canResize()`/`resize()` — only buses use it); time base (`sampleRate()`, `blockSize()`, `blockRate()`); and
parameters: index-addressed (`enum { kParam… , kParamMax }` per class), `getParameter(int)`,
`setParameter(int, float)`, optional `getParameterName/Label/Display(int, char*)`, `getMin/getMax`, and
`getParameters()` returning `vector<ParamInfo>{name, unit, displayValue, value}` (display strings regenerated
per call — UI thread only).

Roles restated: *prepare* sizes state for the fixed block; *process* writes its own outputs from its own inputs;
*setParameter* is callable from any thread and only writes smoothing state (§5); *getParameters* is the introspection
surface a generic UI uses to build a knob page without knowing the effect type.

**Registration** (`Includes/ObjectFactory.h`): a macro instantiates a static registrar that inserts into a
singleton factory map `type-string → std::function<unique_ptr<Object>()>`, plus metadata
`ObjectInfo{Manufacturer, Type, Category, Name}` and two multimaps for browsing by manufacturer/category (that
is how the >300-effect picker UI is populated). Registration is static-init-time; `Create(type)` is what
chains, buses and DAW slots call.

Fair-use excerpts (structure only):

```cpp
// AudIO/AudIO/Includes/IProcessable.h
struct IProcessable {
    virtual void prepareToPlay( uint32_t Samplerate, uint32_t BlockSize ) = 0;
    virtual void process( Buffer<float>& Buffer )                         = 0;
    virtual void teardown() {};
};
```
```cpp
// AudIO/AudIO/airwindows/audioeffectx.h  (line 16)
#define REG_AIRWIN( Name, Cat ) REGISTER_OBJECT_WITH_ARGS( #Name, Name, 1, "AirWindows", "Plugin", #Cat );
```

**Third-party adapter.** `AudioEffectX : AudIO::Object` turns a VST2-shaped effect (`processReplacing(float**
in, float** out, int n)`, stereo, index-addressed normalized params) into a node: `process` passes the
node's output pointer array as *both* in and out (in-place), `getParameters()` fills `ParamInfo` from the
effect's own name/unit/display getters, effect bodies stay stock Airwindows sources. So "300+ effects" costs
almost no glue: namespace-per-effect (symbol isolation), two TUs (compiler memory), one registration macro
per effect carrying its category string.

---

## 3. Graph / bus / chain data model

**Node.** `objectMeta` = `{ Object&, Buffer<float>, unordered_map<objectMeta*, unordered_map<dstCh, srcCh>> }`; the
adjacency map is *output-side* ("my output channel X feeds that node's input channel Y").

**Buffer / patch.** `Buffer<float>` holds per port a `shared_ptr<float*>` plus a "patched" flag; sample memory
always comes from the global `MemoryPool<float>` (fixed `poolSize × blockSize` floats, ref-counted, zeroed on
acquire). Patching literally means "make my input port point at your output port's block", so the graph is an
aliasing pointer graph over a preallocated pool — no copies, no per-block routing tables; `getInputs()`/
`getOutputs()` are cached `float**` arrays refreshed on patch.

**Design** (`Includes/Design.h` + `Core/DesignImpl.*`) owns the graph. Public API groups: nodes
(`createObject(type)`, `createObject(unique_ptr<Object>)`, `removeObject`, `getObject`); edges (`patch(src,dst)`
channel-matched, `patch(src,srcCh,dst,dstCh)`, `unpatch`); containers (`createChain(stereo)`, `createBus()`,
`insertDesign(shared_ptr<Design>)` for nesting); bus sends (`sendToBus(bus,obj,amount)`, `removeFromBus`,
`setBusInputGain`, `getBusConnectedInputs`); inserts (`createInsert(chainOrBusId,type)`, `addInsert(…,pos =
POS_END)`, `removeInsert…`); device seams (`setDesignInput(objId)`/`setDesignOutput(objId)`/`clear…`); mix
params shared by chain and bus (`BusOrChain`: bypass, mute, gain, pan, stereo/downmix L/R); metering
(`addMeter`, `readMeter`, `enableInput/OutputMetering`).

**Ordering.** `patch()` is rejected with rollback if it would create a cycle (DFS colouring over the adjacency
maps). A successful edit sets `_dirty`; when dirty, the audio thread runs a DFS post-order topological sort
into a flat `vector<objectMeta*> _dfs`, which is the processing order.

**Chain.** An ordered `list<insertMeta>` (node + its own Buffer) auto-patched head-to-tail and
head-to-own-input / tail-to-own-output on every insert/remove. `Chain` is itself an `Object` (2→2), so it can
be a graph node or the insert strip inside a bus; it owns ramped gain/pan/mute/bypass/downmix parameters
applied to its output *after* the inserts.

**Bus.** `Bus : IChain, Object` with `Object(0, 2, 0)`: sources attach through `sendToBus`, which finds a free
*stereo pair* in the bus's input buffer (growing it by +2 and its gain table when full), records
`src → firstMonoChannel`, and stores a per-input gain. `Bus::process` = `stereoMixer` (clear, then add each
source pair × gain) → run the internal `Chain` → chain output patched onto the bus object's own outputs. So a
bus = summing junction + insert chain + fader/pan/mute, and a chain = insert strip + fader.

**Design I/O seam.** `setDesignInput(obj)` stores an atomic pointer to that node's *input* Buffer and ramps a
0→1 gain; input/output pointers are only nulled once the ramped gain actually reached 0
(`handleClearDesignInput/Output`) — the mechanism for click-free graph switching. Design bypass = "ramp gain
to 0, then wire inputs through to outputs" (bypass = pass-through, not silence); output gain 0 = silence.

**DAW layer on top** (`Daw/`): `DawBusManager(numMonoIns, numMonoOuts, numSendReturns, numSubmix)` builds
in/out/send-return/submix/aux buses from Design chains/buses, with a static per-category ID registry
(`getStereoInID(pos)`, `getSendReturnID(pos)`, …) and name↔ID maps. `DawBus` = `DawRouting` (type, width,
send gains, connect helpers) + `DawInserts` (fixed 4 slots) + a wrapper around one Design chain-or-bus id;
`DawEngine : Design` adds the bus manager, `DeviceManager`, recorder/metronome/preview objects, memory stats,
sequences/tracks and the `AUDIO_*` sizing constants. Fixed numbers: `MAX_INSERTS = 4`,
`POS_END = 0xFFFFFFFF`, pool = 2500 blocks × 480 samples (~4.8 MB) @ 48 kHz in `DawEngine`.

---

## 4. Preset + state handling

- AudIO has **no session/preset format and no graph serialization**. Topology, names, sends, and bus layout
  are runtime-only.
- Per-insert state is a flat float vector: `DawInserts::serializeEffect(pos, vector<float>&)` emits
  `[bypassFlag, param0 … paramN]`; `deserializeEffect` writes the flag then
  `min(values-1, currentParamCount)` parameters. Nothing else: no effect-type id, no parameter names, no
  version, no sample-rate-dependent values. Effect identity is the **display-name string** (`addEffect(name,
  position)` → factory lookup), so loading = "create by name, then push the float vector".
- Persistence is entirely the host app's job. In `Kick/`,
  `modules/engine/audio_engine/engine/mt_audio_inserts_types.cpp` walks the slots into a `juce::ValueTree`
  (`INSERTS` → per-slot `name` + `state` as a var array, with `UndoManager`) and reads it back the same way;
  undo, project files and migration live there. Airwindows effects do implement VST-style
  `getChunk`/`setChunk` float blobs, but `serializeEffect` does not use them.
- Consequence: state is positional and version-fragile. A clean-room engine should put
  `{stableEffectId, version, paramId → value}` in the blob instead of an anonymous float array.

---

## 5. Realtime rules (what the design actually guarantees, and where it leaks)

**Threads.** Exactly one audio thread: device callback (`minisdl`/SDL in examples, JUCE in Kick) → `DeviceManager` → the
active `IProcessable` (atomic pointer, so the whole design is swappable by a store) → `Design::process`. Everything else
(UI, patch editing, persistence, the `Console` REPL thread) is control-side.

**Block model.** Block size is fixed at engine construction (480 @ 48 kHz for the product; device requests are clamped via
`min(BlockSize, BLOCK_SIZE)`). All audio memory comes from the pre-partitioned pool, so every port buffer is a constant-size
block and no node allocates during steady-state processing.

**Per-block sequence** (`DesignImpl::process`): consume ramped gains → apply deferred clear / design-input / design-output
requests → drain the remove queue → topological sort *if dirty* → call `process()` on each node in `_dfs` order → copy the
design-output node's blocks into the device output with output gain → run `MeterEngine`.

**Control → audio communication.** Graph edits: `PatchQueue<float>` is a process-wide SPSC ring (64 entries) of
`{dest Buffer*, src float*, destChannel, input|output}`; pushes queue while the engine runs and apply inline when it does
not; drained at the top of the block. Structural edits: node removal goes through a 16-entry SPSC id queue, with the actual
`erase`/destruction deferred to the audio thread's next block. Parameters: mostly plain `float` members of
`RampedParameter` written by control and read by audio; booleans/indices gating branches use `std::atomic` (bypass, mute,
metering enables, recorder indices). No formal acquire/release protocol anywhere.

**Smoothing.** Per block, not per sample. `RampedParameter<T>` keeps `current/target/stepSize` and `getNext(blockSize)`
advances once per block (default 12000 steps ≈ 250 ms at 48 kHz); the whole block is then scaled by one constant per
channel. Bypass/mute = "ramp gain to 0, then early-out branch" (copy inputs→outputs for bypass, clear outputs for mute) —
which is why clicks are rare despite coarse granularity.

**Known leaks (do not copy):**
- Audio thread mutates and destroys containers: the sort can `resize()` the order vector; `handleRemove` erases from
  `unordered_map`/`list` and destroys objects (and their pool buffers) on the audio thread.
- The object map is mutated by the control thread (`insertObject`) while the audio thread iterates it in
  `topologicalSort` — unsynchronized. The DAW layer papers over this with `std::this_thread::sleep_for(20 ms)` after each
  patch ("allow time for the design to propagate"): a pacing convention, not a guarantee.
- `MemoryPool::allocate_` finds a free block by linear scan; fine at pool scale but O(n) inside a control call, and the
  pool is a process-wide singleton initialized by the embedder.
- Object ids come from a `static` counter declared in a header (internal linkage per TU), so a second TU that constructs
  nodes (the Airwindows TU) can mint colliding ids.
- Build flags `-Ofast -ffast-math -w`: relaxed FP semantics, no warning signal.
- Metering: `LevelMeter` runs on the audio thread, but `MeterEngine` round-robins meters across blocks (~20 Hz refresh,
  `_metersPerBlock` recomputed on membership change) to bound cost; the UI reads without locks (benign for floats, no
  seqlock).

---

## 6. Test strategy

**There is none in AudIO** — no unit tests, no framework, no assertions beyond `assert` in the pool, and CI
(`Kick/CI/azure_build.sh`) only builds firmware + app. Verification is manual and example-driven: `BasicUsage` (two designs,
bus sends, a deliberate cycle attempt expecting rejection, meters, Console REPL); `DynamicPatching` (re-routes sources
between buses every 5 s while audio runs — the de facto stress test for the patch queue and the 20 ms pacing); `Subdesign`
(nested `Design` nodes as objects); `Daw` (sequences/tracks, device start, meter dumping); `Synth` (standalone ImGui host on
`minisdl`, i.e. an engine-without-JUCE smoke test). Diagnostics are stdout: `MemoryPool::getUsed()/getPeakUsed()/
getFreeMemoryPercentage()`, `Design::dumpMeter`, `DawEngine::getUsedMemory()`.

Contrast with our side (`midibrain/pi/ARCHITECTURE.md`): we already mandate `test_<mod>.cpp` assert-mains next to the code
plus a headless emulator selftest, which AudIO lacks entirely. The gap to close is therefore not "more tests" in the abstract
but tests at the seams AudIO leaves untested: patch/command ordering, cycle rejection, pool exhaustion, drop counters,
solo/mute matrix, state round-trip.

---

## 7. Top 5 ideas directly reusable by our clean-room C++20 engine

1. **Node-owned block buffers over one pre-partitioned pool, patched by pointer.** Give every graph node
   (strip insert chain, bus mixer, FX return) a `Buffer` of `float*` ports whose memory comes from a single
   pool of `N × kBlockSize` blocks allocated at engine start. Patching = assign a pointer; processing = walk
   a flat order vector. Direct fit: 18 strip nodes + 2 FX-bus nodes + master, each with its own
   pre-allocated block, zero allocation on the audio thread, and `Recorder` taps become pointer reads
   instead of copies.
2. **A tiny command FIFO drained at block start + a dirty flag for re-ordering.** Keep graph edits off the
   audio thread as *commands* (connect, disconnect, insert, remove, set-send) applied at the top of
   `process()`, and recompute the processing order only when the topology flag is set. Adopt AudIO's shape
   (SPSC ring, fixed capacity, "apply inline if not running") but **not** its container mutation: keep node
   storage control-thread-owned and hand the audio thread an immutable order array (double-buffered), so we
   get ordering without the unsynchronized `unordered_map` walk and without the 20 ms sleeps.
3. **Ramped parameter + ramped bypass/mute, consumed once per block.** One small `Ramp{current,target,step}`
   with `next(nSamples)` called at block rate is enough to kill clicks with a fixed block size, and
   "ramp gain to 0 then early-out (bypass = pass-through, mute = clear)" is a clean, branch-friendly pattern.
   Reuse it for our strip fader/pan/send ramps, FX return level, and especially for **graph I/O switching**:
   ramp out the old tap, then null the pointer, then ramp in the new one — that is exactly the click-free
   take-arm / FX-bypass behaviour we need around the multitrack recorder.
4. **Insert slots as a fixed array with atomic bypass + a serialized state blob — but with a stable id and
   version.** AudIO's 4-slot `DawInserts` (fixed array, per-slot atomic bypass, "process in place on the
   caller's buffer", `serialize = [bypass, params…]`) maps 1:1 onto our two FX buses and any per-strip
   insert. Keep the mechanism, fix the state model: prepend `{effectIdString, schemaVersion, paramCount}`
   and address parameters by a stable per-effect index, so a saved project survives us reordering or adding
   parameters.
5. **Bus as a first-class node with a per-source stereo gain table and a `sendToBus(obj, amount)` API.**
   AudIO's bus (free stereo-pair allocation, `src → channelPair` map, per-input gain, sum → insert chain →
   fader/pan) is almost exactly our 2 FX buses with per-strip send amounts, and it also gives us the
   solo/mute trick worth copying: **solo is not a per-strip audio flag but a group-wide recomputation**
   (`DawBus::setSolo` walks the bus group and sets mute on everything else). For 18 strips + 2 buses that is
   an O(20) recompute on a UI event instead of per-sample gating — much simpler than our current
   "any-solo ⇒ only solos audible" test inside the process loop, and it composes with FX returns.

Honourable mentions: `MeterEngine`'s round-robin metering (bound cost by measuring a subset of taps per block at ~20 Hz);
factory `ObjectInfo{manufacturer, type, category, name}` metadata (free CLAP-browser categories); the `AudioEffectX`-style
adapter as the template for wrapping our CLAP `Host` as a plain `IEffect` so buses stay plugin-agnostic; `pan.h`'s 7 pan laws
(we fix constant-power — a per-strip pan-law enum is cheap).

**Anti-patterns to leave behind:** process-wide singleton pool and per-TU id counter; audio-thread container
mutation/destruction; `sleep_for(20 ms)` as a synchronization primitive; positional untyped state blobs; per-block-only gain
granularity where a per-sample ramp is nearly free; `-Ofast -ffast-math -w` as the build baseline (we are `-Werror`, which is
the right default for a 20-node engine).

---

Open risks/questions: (a) AudIO has zero tests, so several realtime claims are inferred from code reading, not from observed behaviour — notably the unsynchronized `_objects` walk on the audio thread and the per-TU id counter; (b) `AUDIO_NUM_INPUTS/SEND_RETURNS/SUBMIX` macros used by `Kick/modules/engine/audio_engine/engine/mt_audio_manager.h` are not defined anywhere in the published tree (build-time or withheld), so KICK's exact bus counts could not be confirmed; (c) Airwindows effect count 329 is from registration-macro counts, not a runtime enumeration.

Recommended next step: if we adopt ideas 1–2, have the parent fold "node-owned pool buffers + command FIFO + immutable order array" into `pi/ARCHITECTURE.md`'s `audio/` contract before implementation, since it changes `Engine::process`'s ownership story relative to the current `IEffect::process(float*const*, …)` signature.
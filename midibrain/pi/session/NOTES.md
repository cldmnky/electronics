# mnkybox session core — notes
Works: `mnky_session` saves/loads the fixed UTF-8 JSON object (version 1) with a
hand-rolled writer/parser (no JSON dependency). `load()` returns
`LoadResult{ok,error,snapshot}` and never throws; version != 1, malformed JSON,
wrong-typed known fields and files over 4 MiB are rejected with a reason.
Unknown fields are ignored and missing fields take documented defaults (all
channels, unity mixer, 120 BPM, empty FX buses). Real-file round-trip tests run
in a fresh mkdtemp directory.
`capture()` reads Router + Engine (+ optional FX slots) into a canonical
Snapshot; `apply()` replaces router/mixer state wholesale and updates FX only
when the id loads. An empty FX id clears the slot; an unavailable effect is
skipped (tolerant) and keeps the live slot. FX parameter indices are the
effect's stable order, owned by the FX team; extra saved params are ignored.
Core getters added for read access (no behavior change):
- `audio::Engine::channel(int) const` and `masterGain() const` — live strip/master reads.
The app FX slot must implement `session::FxSlotTarget` (effectId / loadEffect /
clearEffect / paramCount / paramValue / setParamValue / bypassed / setBypassed).

## Additive v2 keys (no version bump: absent == legacy state)
- `mixer.tracks` — ordered `[{"slot":n,"name":"..."}]` list of the slots that
  exist and their display order. Absent = all audio::kMaxInputs slots in order
  (also the default value of `MixerState::tracks`); `[]` = empty project. A slot
  outside 0..audio::kMaxInputs-1 or a repeated slot = load error with a named
  reason; names are truncated
  to `kMaxTrackNameBytes` (64) at a UTF-8 boundary. `channels[]` stays the fixed
  capacity store, so a hidden slot keeps its settings. App-owned until the
  engine has a dynamic track list: `capture()` reports every slot, so the
  entry count follows audio::kMaxInputs.
- `mixer.channels[i].insert` — `{"id","bypass","params"}`, the same shape as an
  `fx.bus` entry (empty id = none, params are doubles in the effect's stable
  order). Engine state as of audio wave 9: `apply()` forwards it to
  `Engine::setTrackInsert()` and `capture()` reads the applied values back. The
  app's mixer model still writes the snapshot before save (project_io.cpp),
  like the dynamic track list.
- `router.routes[i].src` = -1 (`kSrcGenerator`, generator chain, no input) or -2
  (`kSrcOutputInsert`, chain in front of every message routed to `dst`). Route
  src is range-checked as -2..31, dst as 0..31. The router has no pseudo port:
  `apply()` forwards a pseudo entry to `Router::setGenerator*` /
  `Router::setOutputInsert*` when `piOwnsPipeChain` says the destination is
  Pi-owned (UsbOut or a dynamic USB id) and skips a TRS destination (the Pico
  owns it). `capture()` reads the enabled Pi-side generator/insert chains and
  their pipes back, in slot order; a TRS destination is never in this view.
  A real port pair's pipe list remains app-owned.

Pi follow-ups: create `~/Music/mnkybox-sessions` at startup; apply
`transport.bpm_x100` to `midi::UsbClock` + `PicoLink::setTempo`; save via
temp-file + rename and fsync for power loss; decide whether CLAP state blobs
belong beside params; add schema migration when version 2 appears.

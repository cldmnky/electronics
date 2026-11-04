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
Pi follow-ups: create `~/Music/mnkybox-sessions` at startup; apply
`transport.bpm_x100` to `midi::UsbClock` + `PicoLink::setTempo`; save via
temp-file + rename and fsync for power loss; decide whether CLAP state blobs
belong beside params; add schema migration when version 2 appears.

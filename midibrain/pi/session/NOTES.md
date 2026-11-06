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
- `mixer.channels[i].fxSlots` — the per-track FX rack (`session::kTrackFxSlots` =
  2): `[{"id","bypass","wet","params"}, {...}]`, index 0 = FX1, index 1 =
  FX2, run in series post-EQ pre-fader. `wet` is a 0..1 wet/dry mix (clamped
  on read, default 1.0); `params` are doubles in the effect's stable order,
  the same contract as `fx.bus`. The encoder always writes both slots and
  always writes `wet`, so a saved rack round-trips exactly.
- `mixer.channels[i].insert` — DEPRECATED: the old single per-track insert
  `{"id","bypass","params"}` (empty id = none, params are doubles in the
  effect's stable order). A file without `fxSlots` reads it as FX1; a file
  with `fxSlots` ignores it for the rack (the slot wins) and the key is kept
  only for old files and old readers. Engine state as of audio wave 11:
  `apply()` forwards both `fxSlots` to `Engine::setTrackFxSlot()` and
  `capture()` reads both back (built-in values clamped as the DSP holds them,
  wet clamped); the deprecated `insert` is captured as the legacy FX1 view and
  still applies to FX1 for an in-memory snapshot that predates the rack.
  `send` stays in the format and round-trips, but is also
  deprecated: the FX1/FX2 knobs bind to `fxSlots[i].wet`, and a legacy file's
  `send[i]` values migrate into those wet levels when `wet` is absent.
- `router.routes[i].src` = -1 (`kSrcGenerator`, generator chain, no input) or -2
  (`kSrcOutputInsert`, chain in front of every message routed to `dst`). Route
  src is range-checked as -2..31, dst as 0..31. The router has no pseudo port:
  `apply()` forwards a pseudo entry to `Router::setGenerator*` /
  `Router::setOutputInsert*` when `piOwnsPipeChain` says the destination is
  Pi-owned (UsbOut or a dynamic USB id) and skips a TRS destination (the Pico
  owns it). `capture()` reads the enabled Pi-side generator/insert chains and
  their pipes back, in slot order; a TRS destination is never in this view.
  A real port pair's pipe list remains app-owned.
- `mixer.channels[i].mode` — track mode 0..3 with the values of
  `audio::MonitorMode`: 0 = PLAY (take only), 1 = ARM (smart/auto), 2 =
  OVERDUB (input + take layer), 3 = LIVE (input only). It is the canonical
  field for the W1 UI words. `monitor` is the pre-W1 spelling of the same
  values and stays encoded for old readers: the encoder writes both from
  `mode`, and the reader derives `mode` from `monitor` when `mode` is absent
  (a legacy file therefore loads unchanged, both fields equal). `apply()`
  forwards `mode` to `Engine::setChannel()`, `capture()` fills both from the
  engine's `MonitorMode`.
- `mixer.channels[i].recSource` — recorder tap point 0..2 (default 0): 0 = PRE
  (post-gain, pre-EQ; the historical tap and the legacy value), 1 = POST
  (post-EQ + fader, pre-FX rack), 2 = POSTFX (post-FX rack + fader). The tap is
  the live input only; take/clip audio never enters a recording. The session
  reader clamps to 0..2. Engine state: `apply()` forwards it through
  `Channel::recSource` and `capture()` reads `Engine::recSource()` back.
- `song` — the arrangement (`SongState`):
  `{"sections":[{"name","bars"}],"clips":[SongClip],"lengthBars","grid"}`.
  A clip is a takes.edit region plus placement: `track` (0..kMaxTracks-1),
  `start` (beats), `take`, `offset`, `length`, `gainDb`, `fadeIn`, `fadeOut`,
  `normalize`, `loop`, `muted`. Section names truncate to 16 bytes at a UTF-8
  boundary, `bars` clamps to >= 1, `grid` to 0..3 (bar/half/quarter/sixteenth),
  `lengthBars` to >= 0 (0 = derive from the last clip), negative beats to 0
  and `gainDb` to -96..+12. `track` and `take` are required identity keys; a
  track outside 0..63 is a load error instead of a silently moved clip. An
  empty song is valid. A file without `song` migrates a non-empty legacy
  `takes.edit` into clips on their tracks at `start` 0, preserving region
  order; a file with `song` wins, even with an empty clip list. (This also
  made the `takes.active`, `takes.loop` and `takes.edit` sub-keys independent:
  an edit-only legacy file now loads its regions.) The clip lanes are engine
  state: `apply()` forwards them per track (an empty lane clears that track)
  and `capture()` reads the applied lanes back in track order. `sections`,
  `lengthBars` and `grid` stay app-owned; `capture()` leaves them at defaults.

Pi follow-ups: create `~/Music/mnkybox-sessions` at startup; apply
`transport.bpm_x100` to `midi::UsbClock` + `PicoLink::setTempo`; save via
temp-file + rename and fsync for power loss; decide whether CLAP state blobs
belong beside params; add schema migration when version 2 appears.

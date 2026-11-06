# Audio song arranger — design plan

Status: **design approved for later implementation** (plan only, 2026-11-06).
Companion to `track-recording-plan.md`; reuses its take/track model
unchanged. Owner ask: "think about a audio song arranger as well, how
would that work?"

## 0. What it is

A song timeline: place the takes you recorded as **CLIPS** on a per-track
grid, arrange them into **SECTIONS** (intro/verse/chorus), and press PLAY
to hear the *song* — with punch-in recording INTO the arrangement. The
gap it fills: today takes play from position 0 with per-track loops; there
is no way to say "this beat plays bars 1-8, then the fill takes over."

## 1. The key insight: a clip already exists in the contracts

`session::TakeRegion` = {take, offsetBeats, lengthBeats, gain, fades,
normalize} — region machinery, fades and the engine renderer (offset →
frames, fade ramps, newest-wins) are already built and tested. A **CLIP is
a TakeRegion placed at a timeline position**:

```
Clip = { track, startBeats, region: TakeRegion, loop: bool, muted: bool }
Song = { sections: [{name, bars}], clips: [...], lengthBars, grid: 1|1/2|1/4|1/16 }
```

The existing per-track `takes.edit` list is simply "one clip at beat 0";
migration = rewrite each region as a clip at 0. Nothing is thrown away.

## 2. Playback semantics

- **SONG mode ON** (transport toggle, next to LOOP): the playhead walks the
  song; each track renders the clip covering `playhead` (clips on one track
  never overlap — placement snaps/rejects overlaps); a track with no clip
  under the playhead is silent unless LIVE/ARM (input passes per its mode).
- **Edges**: 5 ms auto-fades at clip boundaries by default (click-free
  switching), user fades win when set.
- **SECTIONS**: named markers at bar boundaries (INTRO 8, VERSE 16 …);
  section list popup; **duplicate section** = the arranger's core verb
  (build VERSE 1, duplicate, tweak VERSE 2); **loop section** for
  arranging (playhead loops current section while editing).
- **Record INTO the song**: PUNCH IN/OUT live on the song timeline
  (arrangement-relative); recording engaged between them writes a new take
  and **auto-places it as a clip at the punch position** (that is the
  "record the drumbeat, assign it to a track" workflow done live).
- **SONG mode OFF**: today's free behavior (per-track take loops) — the
  toggle is the single precedence rule; nothing about take loops changes.
- **Bounce**: `RENDER SONG` writes the arranged output to a stereo WAV
  (a master take; reuses the render machinery).

## 3. UI (800x480)

- **ARRANGE tab** (tab row returns to 9: Mixer, Routing, Recorder, System,
  PROJ, MIDI, EQ, XY, ARRANGE). It is a *workspace* like Mixer — a modal
  popup would undersell it (unlike per-take EDIT, which stays a popup).
- Layout: left column = track rows (only tracks that carry clips + the
  selected track, 32 px rows, name + mode word + mini-waveform); top =
  section band (colored bars, names); body = clip blocks (rounded, name,
  mini-waveform fill, loop arrow badge); playhead line; bottom strip =
  grid selector, zoom, section ops (ADD/DUPLICATE/LOOP SECTION), RENDER.
- Interactions: tap clip = select + `...` actions (OPEN in edit popup,
  MOVE, TRIM, DUPLICATE, GAIN/FADE, MUTE, DELETE); drag = move with
  grid-snap; drag edges = trim; long-press = duplicate; tap empty = playhead
  jump (Bluebox rule); two-finger pinch = zoom (Z-/Z+ keep their buttons).
- Panel: NAV walks clips spatially (row = track, column = grid step);
  PRESS opens the clip popup; SHIFT = section ops; EDIT-mode knob nudges
  the selected clip's position/length with value popups (the knob is the
  mover — natural fit with the value-knob work).

## 4. Engine delta (small, honest)

- TakePlayer: multi-clip-per-track scheduling — at each block, the
  covering clip selects which region renders (a per-track clip index
  advanced at block boundaries; edits double-buffer the index so editing
  while playing is safe).
- Song position = transport tick (beats) — exists; section/clip lookup is
  arithmetic, no allocation.
- CPU: unchanged ceiling — one voice per track either way (clips select
  the source of the voice; they do not multiply voices). The 64-track
  budget table stands.

## 5. What we are explicitly NOT building (scope cuts)

| Cut | Why |
|---|---|
| MIDI clips / note sequencing | This is an AUDIO arranger; MIDI stays in the pipes/router domain |
| Pattern/scene clip launcher | Live performance launching is a different product (deliberate rejection, as with the sampler scope) |
| Tempo automation / tempo map | Song tempo is the transport tempo v1; per-section tempo is a small later add |
| Time-stretch / pitch | Clips play at native rate; the 48 kHz rule stands |
| Clip-level FX | FX live on the track (per-track rack) — arrangement never touches signal flow |
| Section-conditional MIDI (program changes) | future note, not v1 |

## 6. Worked example (the owner's beats → song)

1. Record `drums` (PAIR 01/02, COUNT-IN 2 bars) → take `drums-001`.
2. `NEW TRACK FROM THIS TAKE` → track `beat` (PLAY mode).
3. Record `bass` on IN 03 → take `bass-001` → `NEW TRACK FROM TAKE`.
4. ARRANGE: add section VERSE (8 bars); place `beat-001` clip at bar 1
   (loop on), `bass-001` clip at bar 3; duplicate VERSE → bar 9; drop a
   fill clip at bar 8 on `beat`.
5. Section-loop VERSE, jam a synth line on a LIVE track; PUNCH IN at bar 5
   → new take auto-appears as a clip at bar 5.
6. PLAY from the top: the song runs; RENDER SONG → `song-001.wav` master
   take.

## 7. Session + contract deltas

- New v2 section `song`: `sections[]`, `clips[]` (clip = TakeRegion +
  `track`, `startBeats`, `loop`, `muted`), `lengthBars`, `grid`.
- Migration: `takes.edit` regions → clips at 0 (additive decode).
- Transport: `songMode` (bool) alongside the loop toggles.
- Punch fields: reinterpret as song-relative when songMode is on (document;
  no new fields).

## 8. Waves (when green-lited)

1. **A1**: session `song` section + migration + engine clip scheduler +
   boundary fades; headless tests (clip switching at boundaries, overlap
   rejection, section loop arithmetic).
2. **A2**: ARRANGE tab surface (grid, clips, sections, playhead, pan/zoom),
   clip popup actions, drag/trim/duplicate with snap.
3. **A3**: punch-into-song (auto-clip placement), RENDER SONG bounce,
   section duplicate/loop, panel NAV/EDIT paths, `--ui-taps` scenarios +
   manual chapter.

## 9. Acceptance

- The §6 example as a scripted `--ui-taps` scenario + a hardware run.
- Clip boundaries click-free (audition + waveform inspection of a bounce).
- Overlap rejected with a clear message; edits while playing never glitch.
- `--ui-taps` covers every new control; ASCII strings; 44 px; slider law.

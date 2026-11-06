# MIDI Brain DAW — workflow plan (tracks, recording, takes, song)

Status: **approved for later implementation** (plan only, 2026-11-06).
Replaces and supersedes `track-recording-plan.md` and
`song-arranger-plan.md` (merged here, then deleted). Owner pains that
started this, verbatim: "I cant get a audiotrack working, recorded wav's
stay in each track, i cant delete them, the wav edit should be a popup in
the recording screen, the waveform display is really small" — plus the
workflow asks: "record either with a count in from the beginning, or punch
in", "take the recording and create an audio track", and "a audio song
arranger".

Reference workflows (ideas, clean-room): 1010music Bluebox v1.1.2 (takes,
Active semantics, punch, File menu) and the Akai MPC (count-in, pad/audio
tracks, song mode). This is our instrument's own design.

---

## 1. What this instrument is

A **clip-based audio DAW in a rack**: 64 tracks of audio, each track a
virtual channel (live input or recorded material), takes as the unit of
material, clips arranging takes into songs, everything real-time and
touch/panel driven. The recorder is not a sampler or a sequencer — it is
the place where audio becomes material and material becomes a song.

### Principles (the rules every screen obeys)

1. **State is visible in 2 seconds** — what a track is doing (LIVE/PLAY/
   ARM/OVERDUB) is a word + a color, never a letter alone.
2. **Every action announces itself** — record, save, delete, mode changes
   all land in the notice slot; no silent no-ops.
3. **Select ≠ load** — choosing a thing never destroys the current thing.
4. **Touch and panel are equals** — the value knob is the mover for
   whatever has focus; every control reachable both ways.
5. **Tight space = knob card or popup chip, never a slider** (spec §6 law).
6. **Screens show state; popups make changes** — with one exception: real
   workspaces (Mixer, Arranger) are tabs.

## 2. The mental model

| Object | What it is | What it is not |
|---|---|---|
| **Track** | A virtual channel: input binding (mono/pair/NONE) OR take content; EQ, FX1/FX2 rack, sends, output | Not a physical input — 64 tracks may share 18 inputs |
| **Take** | A recorded WAV (48 kHz) owned by a project, named `NN-<name>-<n>` | Not bound to one track forever (assignable) |
| **Clip** | A take (or region of one) placed at a position on a track's song lane | Not a copy — points at the file |
| **Song** | Sections + clip placements + length/grid | Not a pattern launcher |

Relationships: a track PLAYS clips (song mode) or its Active take (free
mode); recording makes takes; takes become clips (by placement or by the
NEW TRACK FROM TAKE flow); clips never overlap on a track.

## 3. Workflows (the point of the document)

### W1 · First recording — target: 7 taps
1. `ADD TRACK` (popup: name + input + output) → strip appears.
2. Mode → `ARM` (one tap; status: `TRACK 02 ARMED - input 03/04`).
3. `REC` (always-visible transport) → records; status `REC 00:04 - 02-bass-001`.
4. `STOP` → `TAKE SAVED 02-bass-001 - mode PLAY to hear it`, mode auto → `PLAY`.
5. `PLAY` → hear the take. Meter shows take, not input.
Rules: STOP auto-selects PLAY; armed never silently shadows a take (the
`ARM MUTES TAKE` hazard is replaced by explicit modes); 48 kHz import
refusals state their reason.

### W2 · Count-in and punch (owner ask)
RECMODE on the transport (long-press REC): `NOW` / `COUNT-IN n bars` /
`PUNCH`. COUNT-IN rolls click with the recorder silent, engages at the
downbeat (bar 1 or PLAY FROM). PUNCH rolls from PLAY FROM (or bar 1) and
engages between PUNCH IN/OUT. During each: the status line states exactly
what will happen and when (`COUNT-IN 2 bars - recording at bar 1`,
`PUNCH - recording at bar 9 of 13`).

### W2b · Record source: PRE / POST / POST+FX (owner, 2026-11-06)

"we should be able to choose if we want to record pre post or post, with
effects" — the recorder tap becomes a per-track choice of where in the
channel chain the take is captured:

| REC SOURCE | Tap point on the chain (input -> gain -> EQ -> FX1 -> FX2 -> fader) | Use |
|---|---|---|
| `PRE` | after gain, before EQ (today's default; the dry capture) | material for re-amping later |
| `POST` | through EQ + fader, before the FX rack | the track's mix sound, effects-free |
| `POST+FX` | through EQ + the FX rack + fader | printing the channel sound (FX on the recording) |

- **Per track** (default `PRE`), settable from the ARM area popup and the
  TRACK FX popup; the status line names it while recording:
  `REC 00:04 - 02-bass-001 (POST+FX)`.
- **Invariant kept**: the tap is the LIVE INPUT signal processed through
  that track's chain — take/clip audio NEVER enters a recording (no
  bounce-into-self; rendering is the explicit RENDER path).
- Interaction with modes: ARM and OVERDUB record the chosen tap; OVERDUB
  monitoring still plays the old take alongside (newest-wins layers).
- Count-in does not write; PUNCH writes the chosen tap between the marks.
- The meter shows what is recorded (post-tap), so "what you see is what
  you bake" — except PRE, where the meter intentionally shows the
  processed signal while the take is dry (hint: `meter shows the mix -
  recording is PRE`).

### W3 · Record → create an audio track (owner's drums case)
1. `drums` track, input PAIR 01/02, COUNT-IN 2 bars, ARM → record → STOP.
2. Take row `...` → `NEW TRACK FROM THIS TAKE` → name `beat`.
3. New track appears: input NONE, mode PLAY, `TAKE` badge + mini-waveform;
   MOVE (default; source deactivates) or KEEP ON SOURCE.
4. `beat` is a full track: EQ, FX1/FX2 rack, sends; ARM it later to
   overdub onto it.
Also: `RENDER REGION TO TRACK` from the edit popup bakes IN..OUT into a
new take + the same flow.
Contract fix required: `resolveTakeKey` stops treating the filename track
prefix as binding (it refuses `01-...` on track 3 today); prefix becomes
convention, assignment is session state.

### W4 · Manage takes (owner: "i cant delete them")
Take list on the Recorder primary with per-row `...` actions: `ACTIVE`,
`EDIT`, `RENAME` (file + mapping), `DELETE` (disk + project; confirm names
the file: `DELETE 02-bass-003.wav from disk? This cannot be undone.`),
`REMOVE` (project only), `IMPORT` (pointer + 48 kHz check). Disk line:
`177.6 GiB free - 4 takes - 12.4 MiB`. No SHIFT detour required for any
of it (SHIFT keeps punch/loop + advanced).

### W5 · Edit a take — popup, big waveform (owner ask)
EDIT tab retires; take row `EDIT` (or mini-waveform tap) opens a modal:
header `EDIT 02-bass-003 0:07`, waveform **800×260** (mirrored fill,
playhead, IN/OUT handles), zoom + pinch, ops strip (TRIM/SPLIT/GAIN/FADE/
NORMALIZE/DELETE REGION/UNDO) as knob cards with live preview, footer
`IN 1:2:00 OUT 2:2:00 (4 beats)`. Peaks from `.peaks` sidecars at high
resolution (the "really small" fix); `SCANNING` badge instead of dead
text. Loop/punch markers shown; RENDER = RENDER REGION TO TRACK.

### W6 · Arrange a song
ARRANGE tab (row = 9 tabs with EDIT gone): section band (INTRO/VERSE…
with **duplicate section** as the core verb), track lanes with clip
blocks (drag/trim/duplicate, grid-snap), playhead jump on tap, clip `...`
→ the EDIT popup. SONG mode toggle governs playback (ON = arrangement,
5 ms auto-fades at clip boundaries; OFF = free take loops). PUNCH points
are song-relative in SONG mode — punching records INTO the arrangement
and the take auto-places as a clip at the punch position. The value knob
moves/trims the focused clip on the panel.

### W7 · Bounce and export
`RENDER SONG` → master stereo WAV take (reuses render machinery); takes
copy out as project folders (existing file-management rules).

### W8 · Play the set
All PLAY-mode tracks perform from their clips/takes; LIVE tracks process
input; ARM tracks wait for REC. Nothing armed = nothing recorded; mode
words make that verifiable at a glance.

## 4. Missing functionality (the build inventory)

| Area | Delta | Where |
|---|---|---|
| Track modes | LIVE/PLAY/ARM/OVERDUB + auto-PLAY-on-stop + §hint strings | app (+ engine MonitorMode mapping exists) |
| RECMODE | NOW/COUNT-IN/PUNCH scheduling at measure boundaries; click-only roll during count-in | app + recorder scheduling |
| REC SOURCE | per-track PRE / POST / POST+FX tap selection (default PRE), status naming | engine (tap-point routing) + app |
| Take manager | per-row actions, delete/remove semantics, import 48 kHz check, disk line | app |
| Take → track | NEW TRACK FROM TAKE / MOVE/KEEP / RENDER REGION TO TRACK | app + render (engine has region render) |
| resolveTakeKey | drop filename-prefix binding | app (contract note) |
| Edit popup | EDIT tab → modal; big waveform renderer; zoom; ops preview | app (+ peaks renderer upgrade) |
| Song | `song` session section (sections, clips = TakeRegion + position, grid, length), migration from takes.edit | session + engine |
| Clip scheduler | per-track clip selection at block boundaries, double-buffered index, boundary fades | engine |
| SONG mode + sections | transport toggle, section loop/duplicate, punch-into-song auto-place, RENDER SONG | app + engine (render exists) |
| Unify take names | one identity helper across strip/recorder/lanes/files (review C2 — partially landed) | app |

## 5. Usability rules (accumulated law, all findings from review + owner)

- Slider law: tight space ⇒ KnobChip/popup chip; sliders only for wide
  faders. Value popups at top band (never under the finger), 1.5 s TTL.
- Selector popups replace cycle steppers; press-to-select buttons show
  chevrons; destructive actions name their object in the confirm.
- Notices: `showNotice(kind,text)` — info 6 s, warn 10 s, error until the
  next domain success; severity colors (accent/red only).
- Mode words everywhere (`LIVE/PLAY/ARM/OVERDUB`), monitor legend once:
  `MON input · AUTO smart · BOTH layer · PLAY takes`.
- Color contract: green solo, gray/dim mute, red arm/record, cyan focus.
- Panel parity: modal traps (keypad/confirm swallow NAV/press), single TAB
  ring stop + grid, EDIT-mode knob = focused value, SHIFT chip for faces.
- 44 px minimum; ASCII strings; contrast floors (measured).
- Discoverability copy (ship exactly): `tap ^ in a strip header for the
  full channel`, `SHIFT ▸ takes + punch`, `tap a meter to clear its clip
  hold`, `TAKE SAVED 02-bass-001 - mode PLAY to hear it`, `ARM MUTES TAKE -
  tap to switch to PLAY`, `TRACK 02 is LIVE - no take is active`,
  `import refused: 44.1 kHz file - convert to 48 kHz first`.

## 6. Contracts and migration

- session: track `mode` (derived from monitor 1:1 at migration); `song`
  section (sections/clips/length/grid); `takes.edit` → clips at 0;
  fxSlots (in flight) untouched by this plan.
- engine: record scheduling (measure-boundary start/stop), clip scheduler,
  RENDER SONG; take playback/fades/regions already exist (the plan leans
  on them). REC SOURCE extends the existing `tapPoint` policy: the tap
  moves to the chosen chain point per track (gain-only / EQ+fader /
  +FX-rack); the live-input-only invariant of taps is preserved.
- files: filename convention `NN-<name>-<n>` stays, but the resolver
  treats it as convention (assignment = state). 48 kHz rule everywhere.

## 7. Waves (implementation order)

1. **W1 — the broken path**: track modes + auto-PLAY + RECMODE
   (NOW/COUNT-IN/PUNCH) + REC SOURCE (PRE/POST/POST+FX) + hint strings.
   Acceptance: W1+W2+W2b work headless (`--ui-taps` scenarios) and on
   hardware.
2. **W2 — material management**: take manager (delete/remove/import/
   rename) + take→track + RENDER REGION TO TRACK + resolveTakeKey
   relaxation. Acceptance: W3+W4 vignettes.
3. **W3 — edit popup + waveforms**: EDIT tab removal, big-waveform popup,
   zoom, ops + preview, mini-waveforms in take rows. Acceptance: W5.
4. **W4 — song core**: session `song` + migration, engine clip scheduler +
   boundary fades, SONG mode, sections. Acceptance: clip switching
   click-free, overlap rejected, edits-while-playing safe.
5. **W5 — arranger UI + bounce**: ARRANGE tab, clip ops, punch-into-song
   auto-place, RENDER SONG, panel paths, `--ui-taps` + manual chapter.
   Acceptance: W6+W7; the drums vignette end to end.

## 8. Acceptance vignettes (scripted + hardware)

- **V1 drums → song** (the owner's case, §W3+W6): PAIR 01/02 COUNT-IN →
  record → NEW TRACK FROM TAKE → arrange → punch a fill → RENDER SONG.
- **V2 manage**: record 3 takes, delete the middle, rename, import a 48k
  file, reject a 44.1k file with the exact message.
- **V3 edit**: open popup, zoom, trim + normalize + undo, RENDER REGION.
- Every new control covered by `--ui-taps`; every state string as written.

## 9. Open questions (decide during W1)

1. Auto-PLAY-on-stop: always, or only when the track was ARM (not OVERDUB)?
   (Recommend: always for ARM; OVERDUB stays in OVERDUB.)
2. Clip boundary auto-fade default 5 ms — fixed or user-settable per song?
3. Audition: tap-and-hold a take row plays it to the cue bus — include in
   W2 or defer? (Recommend include: it is how you choose takes.)
4. Should SONG mode remember per project (persisted) or default off?
5. Section tempo: v1 is one tempo per song; per-section tempo is a small
   add if the jam workflow wants it (note in W5 backlog).

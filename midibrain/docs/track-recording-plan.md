# Track & Recording Setup — implementation plan

Status: **approved for later implementation** (plan only, 2026-11-06).
Owner pain, verbatim: "right now it is not working, I cant get a audiotrack
working, recorded wav's stay in each track, i cant delete them, the wav edit
should be a popup in the recording screen, the waveform display is really
small so we need to improve that."

Reference model: 1010music Bluebox user manual v1.1.2 (the linked PDF;
workflow citations below are behavior ideas, clean-room as always).

---

## 0. What is actually wrong (diagnosis)

| Owner pain | Root cause (verified in review + code) |
|---|---|
| "can't get an audio track working" | The record→play path has a hidden precondition: an ARMED track monitors live input and MUTES its take (engine: `!ch.arm` gate). After recording, the strip stays armed and nothing says so. The user hears the room and concludes recording failed. Compounded by: `ROLLING` wording, no PLAY affordance in the take area, no "did it record?" confirmation beyond a log line. (Review A1/A6; playback bug triage 2026-11-05.) |
| "recorded wavs stay in each track" | Takes accumulate per track with no management surface reachable from where you record. Delete/rename/ACTIVE live on a SHIFT face behind an ambiguous selection model (review A3: row-tap LOADS, destructive actions target an "invisible" selection). No disk-visible per-track file list on the Recorder primary face. |
| "i cant delete them" | DELETE requires: know SHIFT face exists → long-press tab → selection survived face switch → confirm. Any break in that chain = silent no-op. Also delete semantics are unclear (project ref vs file on disk). |
| "wav edit should be a popup in the recording screen" | EDIT is a separate tab; the edit surface is divorced from the take list that motivates editing. User must know the file name and switch contexts. |
| "waveform display is really small" | Timeline renders 6 lanes in 480 px (~60 px/lane) with sidecar thumbnails ~20 px tall; no dedicated detail view; ops preview area says "NO PEAKS" when sidecar missing. |

## 1. The Bluebox workflow (what we are matching)

From the reference manual (behavior spec, not copied text):

1. **Record**: arm tracks (red frame) → REC+PLAY → STOP. Most recent
   recording is **automatically Active**. Take files land in the project
   folder named `<track#>-<trackname>-<take#>`.
2. **Play**: a track plays its Active WAV when it is **not armed**. Arm +
   Active = record instead. Overlapping active layers: **newest wins**
   where they overlap; older mutes underneath.
3. **Manage** (EDIT Config screen): per-track file list with Active
   toggles; File menu = **Add** (pointer import, file stays where it is),
   **Remove** (from project only), **Rename** (keeps mapping), **Delete**
   (from disk, no undo, confirm).
4. **Timeline**: tracks top-to-bottom, time left-to-right, bars for active
   WAVs, playback cursor you can grab; PLAY FROM / PUNCH IN / PUNCH OUT by
   measure; playback continues from the last touch point.
5. **Rules**: 48 kHz WAV only (refuse others with a reason); record
   pre/post-fader per destination; Main-mix recording exists as a
   first-class take.

## 2. Target model (MIDI Brain)

### 2.1 Track playback states — one explicit, visible mode

Replace the implicit (arm × active-take × monitor) matrix with a single
**track mode** the user sets from the strip / Recorder / popout:

| Mode | Live input | Take playback | Record | Use |
|---|---|---|---|---|
| `LIVE` | yes | no | on REC+PLAY | soundcheck, live processing |
| `PLAY` | no | yes (Active take) | no | listening, performing from takes |
| `ARM` | yes (with cue) | no | yes on REC+PLAY | recording a take |
| `OVERDUB` | yes | yes (Active, same track) | yes (new take, newest-wins layering) | MPC-style layering |

- One tap cycles or a popup selects; state is legible in 2 s (color + word,
  never one-letter marks alone). Monitor modes (`MON/AUTO/BOTH/PLAY`) fold
  into this — keep the engine's `MonitorMode` as the mapping
  (LIVE→On, PLAY→Off, ARM→Auto-armed, OVERDUB→On+playing).
- The old ARM-only flow maps to `ARM`; nothing in the engine breaks — this
  is state presentation + gating policy, not new DSP.

### 2.2 The audiotrack working path (target: 7 taps, first run)

1. `ADD TRACK` (popup: name + input + output) → strip appears.
2. Set mode `ARM` (one tap) → status line: `TRACK 02 ARMED - input 03/04`.
3. `REC` (touch transport, always visible) → `REC 00:04 - take 02-bass-001`.
4. `STOP` → status: `TAKE SAVED 02-bass-001 - mode PLAY to hear it`.
5. Mode auto-switches to **`PLAY`** on stop (Bluebox's auto-Active +
   our fix: the take becomes audible immediately on next PLAY).
6. `PLAY` → you hear the take. Meter shows take, not input.
7. Repeat: `ARM` again for the next take; take list grows on the Recorder.

Rules that make this safe: STOP after recording auto-selects PLAY mode;
armed tracks never silently shadow takes (the review A1 hazard dies with
the explicit mode); every transition announces itself in the notice slot.

### 2.3 Take lifecycle & management (per track)

Recorder primary face carries a **take list for the selected track**:
rows = `02-bass-003  0:07  1.1 MiB  [ACTIVE]`. Per row (or per-row `⋯` +
swipe/long-press actions, per review A3 select semantics):

- **ACTIVE** — sets the row Active for playback (Bluebox semantics).
- **EDIT** — opens the waveform edit popup (§2.4) with this take loaded.
- **RENAME** — keypad; renames the file on disk AND keeps the mapping.
- **DELETE** — confirm naming the file (`DELETE 02-bass-003.wav?`), removes
  the file from disk AND the project; recovery = none (honest, like
  Bluebox), but the confirm is unmissable.
- **REMOVE** — project-only removal (file stays on disk; re-importable).
- **IMPORT** — pointer import with the 48 kHz header check (review D7) and
  `PTR` badge for imported files.
- Disk line: `177.6 GiB free - 4 takes  -  12.4 MiB` (per project).

### 2.4 Waveform edit = POPUP on the Recorder screen

The EDIT tab **retires** (tab row = 8: Mixer, Routing, Recorder, System,
Projects, Monitor, EQ, XY). The edit surface becomes a **modal popup**
launched from a take row `EDIT` button (and from the timeline teaser):

- **Popup layout (800×480)**: header `EDIT 02-bass-003  0:07`; waveform
  region ~ 800×260 px (the "small waveform" fix — the whole width, big);
  mirrored fill + playhead + IN/OUT handles; zoom `Z-/Z+` and pinch (now
  meaningful at this size); bottom = ops strip (TRIM/SPLIT/GAIN/FADE/
  NORMALIZE/DELETE REGION/UNDO) as knob-chip cards per the slider law;
  footer = time readout `IN 1:2:00 OUT 2:2:00 (4 beats)`.
- Peaks: draw from `.peaks` sidecar at high resolution (the 256-frame
  buckets render at ≥2× height; a second coarse level for zoomed-out);
  if missing, background scan with `SCANNING` badge — never "NO PEAKS"
  dead text (review A14/D9).
- Preview: ops preview against the peaks/waveform before commit; RENDER
  (bake to new take) stays hidden until implemented (review A14).
- Loop/punch markers render as guides; editing them stays on their own
  popups (spec non-overlap).

### 2.5 Waveform rendering spec (the "really small" fix)

| Surface | Today | Target |
|---|---|---|
| EDIT timeline lanes | ~60 px/lane, 20 px thumbs | gone (popup replaces) |
| Edit popup waveform | n/a | 800×260 main, mirrored fill, playhead shade |
| Recorder take rows | text-only | 2-row rows: name/meta + 44 px mini-waveform strip (tappable → popup) |
| Ops preview pane | "NO PEAKS" | live before/after waveform |
| Channel popout | none | optional mini-waveform of the track's Active take behind the meter area (stretch) |

Peaks pipeline: sidecar already exists (256-frame min/max + coarse); add a
render layer that draws mirrored polylines with a peak-hold top line and an
accent playhead; cache scaled paths per zoom level.

### 2.6 Data / contract deltas (small)

- `session`: track `mode` (0..3) — migrate from `monitor` 1:1 (keep the
  field, add `mode` or derive); takes list already per-track; nothing else.
- engine: no DSP change required (MonitorMode covers the matrix); optional:
  `setTrackMode` convenience mapping.
- app: take-manager model (per-track lists already exist via scanTakes),
  edit popup reusing the timeline view-model (the EDIT tab's guts move into
  the popup — mostly relocation).

## 2.7 Record sources and start modes (owner, 2026-11-06)

"Flexible way to record from a channel/mixer track": every mixer track is a
potential recording source. Its input binding defines the captured
channels — a stereo pair (`PAIR 01/02`) records a 2-channel take. No new
model needed; make the affordance explicit: `REC` on the strip (and the
take list) names the source in its status (`recording 01/02 -> drums-001`).

Record START has three modes (transport-level, persisted per project):

| RECMODE | Behavior | Status line while armed |
|---|---|---|
| `NOW` | REC+PLAY records immediately from the current position | `REC 00:04 - drums-001` |
| `COUNT-IN` | N bars of click (metronome count-in, `click.bars`) with the transport rolling but the recorder NOT writing; recording engages at the downbeat (bar 1, or PLAY FROM if enabled) | `COUNT-IN 2 bars - recording at bar 1` |
| `PUNCH` | playback rolls from PLAY FROM (or bar 1); recording engages at PUNCH IN and stops at PUNCH OUT (existing punch fields); what plays before/after is the take layers | `PUNCH - recording at bar 9 of 13` |

Implementation notes: the recorder start/stop is scheduled from the
transport tick (measure boundaries already computed for punch); during
count-in the click renders but the recorder tap is not written (the click
is monitoring-only, as today); in PUNCH mode REC arms and waits (one
predictable rule: arming + PLAY = roll, engage at the boundary); count-in
and PUNCH are mutually exclusive per start, selectable from the transport
strip's REC-area popup (long-press REC = RECMODE popup) and the Recorder
face.

## 2.8 Take -> track: create an audio track from a recording (owner, 2026-11-06)

The owner's exact case: "recording a drumbeat on chan 1/2 in stereo, then
once recorded I want to be able to create a track and assign the recording
to it." This is the virtual-channel payoff (docs/track-recording-plan.md
model: a track whose content is a take, input = NONE).

Flow (from the take row `...` menu and the edit popup):
1. `NEW TRACK FROM THIS TAKE` -> keypad for the name (default = take stem).
2. Creates the next free slot: mode `PLAY`, input `NONE`, output `MAIN`,
   ACTIVE take = the recording; the strip appears in the mixer with a
   mini-waveform and a `TAKE` source badge.
3. `MOVE` (default): the source track deactivates the take (it stays in its
   file list, not playing); `KEEP ON SOURCE` option leaves it active on
   both (useful for A/B).
4. The new track is a full track: EQ, FX1/FX2 rack, sends, popout, and can
   be ARM'd later to overdub onto it (newest-wins layering).

Variant (folds the hidden RENDER op back in): `RENDER REGION TO TRACK` from
the edit popup bakes IN..OUT of the current take into a new 48k take
(normalized per the ops settings) and runs the same new-track flow.

**Contract delta (must-fix for this to work):** `RecorderViewModel::
resolveTakeKey` currently refuses a take whose filename track prefix does
not match the target track (`01-...wav` cannot be ACTIVE on track 3) — the
filename convention was made load-bearing. Relax it: filename prefix is a
convention, the binding is session state; assignment may optionally rename
the file to the target namespace (`NN-<name>-<take#>.wav`). Prefer the
relaxation (rename-on-assign loses the original name history).

## 2.9 The owner's workflow, end to end (acceptance vignette)

1. `ADD TRACK` `drums`, input `PAIR 01/02`, output `MAIN`.
2. RECMODE `COUNT-IN` 2 bars; mode `ARM`.
3. REC -> two bars of click -> records the beat -> STOP.
   Status: `TAKE SAVED drums-001 - mode PLAY to hear it` (auto-PLAY).
4. Take row `...` -> `NEW TRACK FROM THIS TAKE` -> name `beat` -> the new
   track appears playing the waveform.
5. Original `drums` track returns to `LIVE` for another pass; `beat` plays
   through the mix with its own FX1 (delay) on it.

## 3. Screen-by-screen summary

1. **RECORDER primary**: track selector row (4/page), big status line
   (mode word + take + time), transport-visible, take list with mini
   waveforms + per-row actions, disk line, `START TAKE` = REC in ARM mode.
2. **RECORDER SHIFT**: punch/loop (unchanged) + IMPORT/REMOVE/RENAME/
   DELETE (kept, but duplicated as row actions so SHIFT is optional).
3. **STRIP**: mode button (LIVE/PLAY/ARM/OVERDUB word + color) replaces the
   ARM/MONO cluster's ambiguity; hint `tap mode to switch · hold = monitor
   detail`.
4. **EDIT popup**: §2.4.
5. **CHN POPOUT**: mode selector + take mini-waveform + `EDIT` + `TRACK FX`.

## 4. Hints / wording (ship these strings)

- After recording: `TAKE SAVED 02-bass-001 - switch to PLAY to hear it`.
- On armed strip with active take: `ARM MUTES TAKE - tap to switch to PLAY`.
- Take row empty state: `no takes yet - press REC in ARM mode`.
- Delete confirm: `DELETE 02-bass-003.wav from disk? This cannot be undone.`
- Remove confirm: `Remove from project? The file stays on disk.`
- Import reject: `import refused: 44.1 kHz file - convert to 48 kHz first`.
- Play with no active take: `TRACK 02 is LIVE - no take is active`.

## 5. Implementation waves (when green-lit)

1. **W1 (state model + the broken path)**: track modes + auto-PLAY-on-stop +
   RECMODE (NOW/COUNT-IN/PUNCH) scheduling + all §4 strings + review A1/A6
   fixes if not landed. Acceptance: the 7-tap path + the count-in start
   work headless (--ui-taps) and on hardware.
2. **W2 (take manager + take->track)**: per-row actions, delete/remove
   semantics, import 48 kHz check, disk line, NEW TRACK FROM TAKE +
   MOVE/KEEP, resolveTakeKey relaxation. Acceptance: the §2.9 vignette.
3. **W3 (edit popup + waveforms)**: EDIT tab removal, popup with big
   waveform, zoom, ops strip, preview, RENDER REGION TO TRACK, mini-
   waveforms in take rows; peaks renderer upgrade.
4. **W4 (polish)**: channel popout integration, mode-color legend, manual
   rewrite of §13 + tab table to 8 tabs, `--ui-taps` coverage of every new
   control.

## 6. Acceptance

- `--ui-taps`: the 7-tap path + take-manager actions + edit popup as
  scripted scenarios; zero QML warnings.
- Hardware: record 3 takes on the UMC; delete the middle one; rename;
  edit popup on the newest; PLAY hears the active take after one tap.
- Manual §13 rewritten to match (including the Bluebox-style take table).

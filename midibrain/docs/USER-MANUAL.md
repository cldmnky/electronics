# MIDI Brain - User Manual

The complete instrument: an 18-track Eurorack recorder, mixer and MIDI hub
in 26HP. Touchscreen when you want it, panel controls when you don't.

Two computers share the work behind the panel. The Raspberry Pi 5 runs the
app - mixing, recording, effects, projects. The socketed Pico 2 owns
everything timing-critical - MIDI ports, panel switches, the master clock -
over an internal SPI link that keeps time even when the app is busy. Audio
conversion lives outside the module: a UMC1820 (plus ADA8200 for the full
18 channels) over USB, independently powered, captured at 48 kHz / 24-bit
to NVMe.

Workflow debts are acknowledged: project and take handling learn from the
1010music Bluebox, input monitoring and the sampling workflow from the Akai
MPC. What follows describes this instrument's own behavior, completely and
in the present tense.

## Table of contents

1. Setup - mount, power, audio, MIDI, first boot
2. Meters, inputs, names, gain
3. Controls overview - joystick, buttons, encoder, touch, LEDs
4. Modes at a glance
5. Transport strip
6. MIXER - four strips a page
7. TRACK - one track, everything
8. EQ - four bands per track and the main
9. FX RACK - two effect slots per track, X/Y grid
10. EDIT - timeline, regions, trim and chop
10A. ARRANGE - sections, clips, song mode, render
11. PROJ - projects and templates
12. SYSTEM SHIFT - output, metronome, MIDI learn, TOOLS
13. Recording and playing takes
14. USB audio streaming
15. File management and backup
16. MIDI clock sync
17. Firmware update
18. Common tasks cookbook
19. Troubleshooting
Appendix A - specifications

---

## 1. Setup

**Mount.** 26HP, 3U, <=110 mm deep - check case depth before the rails.
Four screws, standard pattern.

**Power.** The module draws no rail power. Feed the front USB-C inlet from
a 5 V high-current supply over a qualified cable. Thin phone cables brown
out under full load (18 tracks + screen + NVMe) and corrupt takes - when in
doubt, replace the cable first.

**Audio.** UMC1820 -> Pi USB. For channels 17-18, connect the ADA8200's
S/PDIF to the UMC1820 and slave the ADA8200 to it. Unsynced S/PDIF clicks
once per drift cycle and no software can fix it.

**First boot is empty.** No demo project ships. The mixer starts with no
tracks and PROJ lists only the folders you create: tap ADD TRACK, name the
channel, pick its input and record (section 13). Load a project from PROJ
once you have one.

**MIDI.** TRS Type-A IN/OUT plus a powered USB hub on the data feedthrough
for controllers and interfaces. The Pico generates the clock on both ports;
the Pi mirrors it to USB (section 16).

## 2. Meters, inputs, names, gain

**Meters.** Every strip meters after its gain, with a clip crown at the top.
A crown stays lit for a couple of seconds and clears itself - staging 18
channels is a glance, not a chore. Tapping a strip's meter brings up the
monitor legend (`MON input - AUTO smart - BOTH layer - PLAY take`), so the
letter under the meter never has to be guessed.

**Inputs are shared.** A track is a virtual channel, not a socket. Any
track can read any device input: INPUT SOURCE lists NONE, IN 01..18 and the
nine stereo pairs PAIR 01/02 .. 17/18, and a pick applies immediately. Two
tracks may read the same input on purpose - a DI compressed two ways, or
one side of a pair on its own fader. A stereo pair is one track that owns
both channels and shares monitor, arm and take state; pan spreads the pair.
Inputs are shared, record arm is not: arm one track per physical input for
a take, or you record the same signal twice.

**Names.** Hold a strip name for about 0.6 s, or open the channel popout and
tap RENAME, then type on the keypad - QWERTY and number rows, 48
characters, OK commits, CANCEL keeps the old name. The name follows the
track into the recorder, the EDIT lanes and new take filenames; a track you
never named shows `IN 03`.

**Gain.** Two knobs on one gain: the strip fader and TRACK's TRIM (section
7). The engine applies their sum, so ride the fader for the mix and use the
trim to bring a hot or quiet source into the fader's useful travel. Gain is
upstream of the rack, the cue and OUT3&4 sends, and the recorder taps. Set it
first with the fader at unity: loud passages should tickle the top of the
meter without lighting the crown.

## 3. Controls overview

- **NAV cluster**: joystick up/down/left/right + press. Moves the focus
  ring, activates, edits values. Hold a direction and it repeats (400 ms
  first, 80 ms after) for scrolling lists and slewing values. There is no
  spatial jump: up and left both step back, down and right both step
  forward. The ring starts at one `TAB` stop - press it and the tabs open
  as a grid; pick one and the grid closes with the ring on that screen.
- **SHIFT/EXIT**: short press = back / dismiss / leave EDIT; hold ~0.5 s
  = the tab's SHIFT face (section 4). On touch the `SHIFT >` chip in the
  transport strip does the same job. Inside a SHIFT face, LEFT/RIGHT change
  tab; outside it, LEFT/RIGHT move the ring.
- **Value knob (TEMPO)**: smooth and endless - no detents, no end stops. It
  edits the focused value only while that value is in EDIT, or while a
  config popup is open; otherwise the turn trims tempo, one BPM per detent
  and 0.1 BPM with SHIFT held, and pops the tempo readout. PRESS enters
  EDIT on a focused value and PRESS again leaves it; every change applies
  as you make it, and only a config popup's CANCEL restores the captured
  snapshot. There are no macros and no Q-Link: the knob is the focused
  value, or tempo. Beat-synced delay, metronome and punch measures follow
  tempo instantly.
- **Transport**: REC, OVERDUB, STOP, PLAY, PLAY START. REC+PLAY rolls
  recording on armed tracks. LEDs: REC/OVERDUB blink armed-stopped, solid
  armed-recording; PLAY lit while rolling. The same three actions are always
  on the touchscreen as glyphs in the transport strip.
- **Touch**: drag sliders (you may start the drag and finish outside the
  frame), tap toggles, multi-finger drags on independent controls. Every
  change raises a popup readout (`Center`/`L30`/`R45`, dB, ms) that fades
  ~1.5 s after you stop.
- **Modals trap the panel.** With the keypad or a confirmation open, NAV
  directions do nothing, NAV-PRESS and the knob press accept, the knob moves
  the keypad cursor, and SHIFT cancels. Nothing behind the modal can be
  armed, muted or loaded by accident. Hold BACK on the keypad for 0.6 s to
  clear the line.
- Touch and panel share one focus - whichever you touch last owns it.

## 4. Modes at a glance

| Tab | Primary face | SHIFT face |
|---|---|---|
| Mixer | four strips a page (MIXER) or one track (TRACK) | per-strip GAIN, FX1/FX2 wet, CUE, OUT34, monitor; TRACK: input, output, send source, monitor |
| Routing | patchbay wires, per-output clock and transport gates | FILTER / TRANSPOSE / CLOCK OUT, and PIPES for MIDI FX chains |
| Recorder | take transport, 18 capture slots, take list | take manager, take loop, punch markers |
| System | link, clock, transport, audio backend, log | metronome, send source, monitor, comp, clock source, MIDI LEARN, TOOLS, PANIC |
| PROJ | project list, SAVE, load | NEW, RENAME, DELETE, templates |
| MIDI | in/out activity, counters, event log | none |
| EQ | band grid for a track or the main | band editor: gain, Q, frequency, type |
| XY | the selected track's FX1 and FX2 grids | none |
| ARRANGE | section band, clip lanes, loop band, PLAYHEAD seek | section add/duplicate/loop, LEN and LOOP, grid and zoom, RENDER SONG |

There is no FX tab: the tab row holds nine tabs (Mixer, Routing, Recorder,
System, PROJ, MIDI, EQ, XY, ARRANGE). Effects live inside the track that
owns them - every track carries its own two-slot rack, opened from the
channel popout (section 9). The take editor is the W5 EDIT popup, not a
tab.

SHIFT gives a tab a second face. On the panel, hold SHIFT: after ~0.5 s the
tab's second face comes up and LEFT/RIGHT step through tabs while the key
is held; releasing SHIFT returns to the primary face. On touch, tap the
`SHIFT >` chip in the transport strip (or long-press the active tab) to
switch the face and keep it up until you tap `< PRIMARY`. MIDI, XY and
EDIT have no second face: the chip is dimmed there and tapping it says
`NO SHIFT FACE ON THIS TAB`.

Transport stays live in all modes. Dialogs and popups trap focus until
OK/Cancel/EXIT, and trap the panel the same way (section 3).

## 5. Transport strip (every screen)

Left to right: the state word, position, tempo, the no-Pico badge, the touch
transport, the notice slot, the SHIFT chip, CLICK and the unsaved dot.

`STOP | 001:3:15 / 00:01 | 120.00 BPM | [NO PICO] | play stop rec | notice | SHIFT > | CLICK | dot`

**State and position.** The state word is `STOP`, `ROLL` or `REC`; the
position reads bars:beats:ticks plus the elapsed time computed from that
position.

**Tempo.** The BPM readout is a button with a chevron (it reads `120.00
BPM`). It opens the TEMPO popup: a 40-240 BPM slider, -1 and +1, TAP, DONE,
with the hint `drag or +-1 - TAP three times for tap tempo`. TAP three times
and the tempo comes from the interval between the taps. The knob sets the
same value from the panel.

**No-Pico badge.** `NO PICO` appears in red only when the run has no Pico
linked. The SYSTEM screen names exactly what that costs: TRS MIDI and TRS
clock, panel controls (touch only), LEDs and clock-source follow are absent;
audio, USB MIDI, the recorder, takes, the mixer, the FX racks, EQ, X/Y,
projects and the Pi clock to USB OUT stay live.

**Touch transport.** PLAY, STOP and REC are always there, 44 px, after the
no-Pico badge. Play is accent while rolling, stop is outlined while stopped,
rec is record red and pulses while the recorder is actually writing. With a
Pico linked they sit outside the panel ring - the panel's own buttons own
transport - but they never disappear, so a bench or demo session can run
takes with no panel harness.

**SHIFT chip.** `SHIFT >` switches the current tab to its second face and
reads `< PRIMARY` while that face is up. It is the touch twin of holding
SHIFT.

**Notice slot.** Transient messages live here: playback warnings, import
refusals, `PANIC sent (CC123, all 16 channels)`, `playback: 2 armed track(s)
monitor input, not their takes`. Info clears after 6 s, a warning after 10 s,
an error stays until the same kind of action succeeds.

**CLICK.** Short tap toggles the metronome; hold it for 0.6 s to open the
metronome popup. The label tells the mode when it is not on-play: `CLICK
REC` or `CLICK OFF`. Toggling never changes the mode - a rec-only click
stays rec-only. The popup holds mode (off, record only, on play), output
(`MAIN` / `PHONES` / `OUT3&4`), level and count-in bars (section 12).

**The dot.** The far-right dot marks unsaved work; the same dot sits on the
PROJ tab and on its SAVE button (section 11).

## 6. MIXER - four strips a page

**You see** your session as a dynamic list of tracks, four strips to a page.
ADD TRACK appends a track and opens the keypad for its name; the pager reads
`TRACKS 1-4 / 8` and steps with `<` and `>`. The list grows to 64 tracks and
only real tracks appear. MASTER rides the right edge with its own fader and
a stereo meter pair.

Each strip carries the track name, its pan readout and a `^` expand button
in the header; a tall gain fader with the post-gain meter and clip crown;
the PAN knob chip; the INPUT SOURCE selector; and M / S / ARM along the
base. Tapping a strip body selects it (cyan border). The letter under the
meter is the monitor mode (P, A, B, M - section 13).

**Touch workflow.** Drag any strip's fader; drags stay live past the strip
frame, and two fingers adjust two strips at once - ride a vocal against a
bassline with both hands. Tapping M, S or ARM flips that strip's state with
the colour contract: solo green, mute gray, arm red, focus cyan. A clip
crown clears itself after a couple of seconds, so a glance after a loud
passage tells you exactly which channel needs trimming. Tap `^` for the
full channel (section 7); tap the name to select it, hold it to rename it.

**SHIFT face** turns the same four strips into configure strips: GAIN, FX1,
FX2, CUE and OUT34 knob chips plus the monitor button (`M PLAY`, `M AUTO`,
`M BOTH`, `M MON`). Every value applies while you drag. FX1 and FX2 are the
wet levels of that track's two rack slots - whatever each slot holds (section
9); CUE feeds the cue bus; OUT34 feeds outputs 3 and 4. PRE or POST for CUE,
OUT34 and REC is one setting for the whole mixer (SYSTEM SHIFT, section 12)
and the legend at the foot of the face spells the whole state language out:
`solo green - mute gray - arm red - MON input - AUTO smart - BOTH layer -
PLAY take`.

**Panel workflow.** The ring starts at the TAB stop and then walks the
screen. PRESS on a toggle flips it; PRESS on a value enters EDIT and the
directions or the knob slew it with a popup readout; PRESS confirms, EXIT
walks away and the value stays as set. Use the `<` and `>` buttons to change
page - the ring has no spatial jump across banks.

**Session.** Soundcheck: ADD TRACK per source, name them, pick inputs, set
GAIN on the SHIFT face so loud passages tickle the top of the meter without
lighting the crown, then mix on the strip faders. Nothing here is a mode you
have to leave: the meters keep running while you work.

## 7. TRACK - one track, everything

**Two views of one tab.** MIXER and TRACK buttons at the top switch between
the many-tracks face and the one-track face. Everything below is the same
engine state seen from the other side.

**TRACK primary.** A selector with the four tracks of the current page, then
VOL and PAN knob chips and the REC / MUTE / SOLO buttons. The selector
follows the same pages as the mixer bank, so page 2 of the mixer is the same
four tracks here.

**TRACK SHIFT.** The assignment and configure face: INPUT (source plus
MONO/PAIR), OUTPUT destination, GAIN, FX1, FX2, CUE and OUT34 knob chips,
then the SENDS popup (PRE/POST for CUE, OUT34 and REC) and the MONITOR popup
(the same four modes as the strip). FX1 and FX2 are the wet levels of this
track's two rack slots; the slots themselves are configured in the channel
popout's TRACK FX popup (section 9).

**The channel popout is the whole channel.** Tap `^` in a strip header and
the full channel opens as one surface: name, RENAME and CLOSE; the gain
fader with its meter and monitor letter; PAN; MUTE, SOLO and ARM; TRIM;
INPUT SOURCE and MONO/PAIR; OUTPUT; FX1 and FX2 (the rack slot wets), CUE
and OUT34; the four-way INPUT MONITOR; TRACK FX, which opens the two slot
cards (section 9); the EQ band summary; and REMOVE TRACK at the bottom,
which asks first and leaves the slot's settings and takes in place. This is
the screen to open when a channel misbehaves - every control that can
silence it is on it.

**Touch workflow.** Everything drags in place and applies immediately. The
pan readout is a pill (`C`, `L 30`, `R 45`) so centeredness is a fact, not a
guess. Hold the name (or tap RENAME) to rename without losing the slot,
routing or takes.

**Panel workflow.** Up/down walks the face's controls; PRESS edits; the knob
slews; EXIT backs out. The channel popout traps the ring the same way, so
REMOVE TRACK is reachable from the panel too.

**Session.** A channel giving trouble: open the channel popout, check INPUT
MONITOR (doubling? -> AUTO), check OUTPUT (silence? -> not Main), ride TRIM
against the clip crown. Rename it while you are there - the name is what you
will look for in the file list.

## 8. EQ - four bands per track and the main

**You see** the response grid: log-frequency left to right, +/-dB up and
down, four nodes with the selected one pink and the summed curve white. The
toolbar holds the subject (`MAIN` or the track steppers with the subject
readout), the band chip (`BAND 3 Param`) and FULLSCREEN. Corners show Gain,
Q-or-shape, Frequency and Type for the selected band.

**Touch workflow.** Drag a node and frequency x gain follow your finger; the
curve redraws live. Tap the band chip to step to the next band. Type cycles
None -> LCut -> LShelf -> Param -> HShelf -> HCut. Fullscreen hides the
bridge for fingertip precision on small nodes; the readout flips below your
finger there.

**Panel workflow.** The ring walks the nodes and the corner chips. On a
node, PRESS enters EDIT: left/right moves it in frequency, up/down in gain
(Q on a cut band, which has no gain axis), and SHIFT takes 4x steps; the
knob nudges frequency. Gain, Q, frequency and type are also plain rows on
the EQ SHIFT face if you prefer numbers. Cut filters ignore gain and say so
- the popup reads `LCut - 80 Hz`, never a meaningless dB.

**Session.** High-pass everything that isn't bass (LCut, Q~1), notch the
room out of the vocal with a narrow Param, shelf air onto the main - all
while the take rolls, because EQ is live, never a render step.

## 9. FX RACK - two effect slots per track, X/Y grid

**Effects belong to tracks.** Every track owns a two-slot rack, `FX1` and
`FX2`, and each slot holds its own effect with its own settings and its own
wet level. There is no global FX bus and no FX tab: open the channel popout
and tap TRACK FX to get this track's two slot cards. The FX1 and FX2 knobs on
the mixer SHIFT and TRACK SHIFT faces (and in the channel popout) are the
same two wet levels, one touch away, which is what makes the rack playable
from the mixer. The knobs used to be send amounts; their values carried over
to the wet levels.

**Two slots, in series.** FX1 feeds FX2, after the track's EQ and before its
fader. An EQ cut therefore shapes what the delay repeats, and the fader rides
the whole chain. Any pairing is allowed: delay into reverb on the lead,
reverb into delay on a pad, a plugin into a delay, or two plugins. A slot set
to `NONE` is a wire - the signal passes straight through.

**Picking an effect.** Each slot card holds the effect selector (`NONE`,
`DELAY`, `REVERB` or the CLAP plugin loaded into that slot), the WET level,
BYPASS and that effect's own parameters. The choice is per track and per
slot: one track can be wet while the next stays dry, and delay and reverb can
both live on the same track once each.

**DELAY** settings: TIME, FEEDBACK, BP CUTOFF, BP WIDTH, BEAT ON/OFF with its
division, and PING ON/OFF.

**REVERB** settings: PREDELAY, DECAY, DAMPING, SIZE and MODEL. The model list
holds four: Core, Room, Hall and Plate.

**A plugin slot** hosts a CLAP plugin: LOAD, SAVE, EJECT, BYPASS and the
plugin's own parameter list, which gets the full height of the slot card.

**Touch workflow.** Knobs drag, values apply while you drag, BYPASS and BEAT
and PING toggle, and the MODEL chip opens the model list with the change
applied at once - no apply step. The strip knobs are the fastest way to play
the rack: ride FX1 and FX2 while you mix and the tail follows.

**Panel workflow.** The ring walks the slot cards; PRESS on a value enters
EDIT and the knob slews it; BYPASS and BEAT/PING are PRESS toggles. The
channel popout traps the ring like every other modal.

**The X/Y face** (XY tab) is the selected track's rack under your fingers: a
delay in a slot maps X to cutoff and Y to width, a reverb maps X to size and
Y to level. The two grids can be worked at once, and FROZEN holds the tail
where you left it. Select the track whose rack you want first (the mixer
strip or the channel popout) - the grids follow the selected track, so a
move here never touches another track's effects.

**Session.** Dial a dotted-eighth delay into FX1 and a plate into FX2 on the
lead, ride the strip's FX1 and FX2 to place the tail, then freeze the reverb
over the breakdown and unfreeze on the drop. The same two slots on the next
track are a different pair - the rack is per track.

## 10. EDIT - timeline, regions, trim and chop

**You see** six tracks per page, time left to right, waveform thumbnails
drawn from the peak sidecars, the playhead, and the loop brace and punch
markers (display only; they are edited on RECORDER SHIFT). A lane label
reads `02 bass`. The toolbar is P< / P> (track page), V< / V> (scroll), Z- /
Z+ (zoom), IN, OUT, OPS and UNDO.

**Pick the take first.** Tap a lane label to open `TAKE FOR TRACK 02`: the
ACTIVE take, the track's other takes, imported pointers and `LIVE INPUT`.
Picking one makes it ACTIVE and repaints the lanes; the bottom hint says
`TAP A LANE LABEL TO PICK ITS TAKE`. A track with no take says `NO ACTIVE
TAKE` in its lane.

**Regions.** Drag on a waveform to select, or set the edges exactly: IN puts
the in-point at the cursor, OUT the out-point, and the header shows the
selection in bars:beats. OPS opens REGION OPS for that selection: TRIM,
SPLIT, GAIN, FADES, NORMALIZE, DELETE and CLEAR. The popup previews on the
waveform when the take has a peak sidecar; when it does not, the preview
block offers RENDER PEAKS instead. RENDER is not available in this build.
UNDO steps back one edit.

**Nothing is destructive until you say so.** Edits are a region list
rendered at playback - the take file is never rewritten - so UNDO, picking a
different take and walking away are always safe.

**Touch workflow.** Drag a region, zoom with Z- and Z+, page through the
tracks with P< and P>, split at the cursor repeatedly to chop a break, then
alternate mutes for the arrangement.

**Panel workflow.** NAV moves the cursor (hold SHIFT for coarse bars), and
the lane and cursor column draw their own ring, so no stop is invisible.
PRESS on the cursor sets IN, then OUT; PRESS on IN or OUT sets that edge at
the cursor and enters EDIT for a bars:beats nudge. OPS opens the trapped op
list (up/down, PRESS, adjust the value, PRESS to apply, EXIT to walk away).
Nothing commits on focus change alone.

**Session.** The MPC habit, end to end: record a pass, pick the take, trim
the heads, normalize, chop the break, keep two takes for two arrangements.
Files are never rewritten by edits - only a render would write new audio,
and it is not in this build.

## 10A. ARRANGE - sections, clips, song mode, render

**You see** the arrangement as a section band across the top and one lane
per track that has clips (plus the selected track), time left to right. A
section marker shows its name and bar count; a coloured border identifies
it. A clip block is a rounded rectangle with its take name and mini
waveform, a loop arrow when it repeats, and a dimmed/red-underlined style
when muted. The 2 px accent line is the playhead; the tinted band is the
loop window.

**SONG mode.** The `SONG` chip in the transport strip governs playback.
`SONG ON` means the transport reads the song timeline and the clip lanes
play; `SONG` (off) means the classic free Active-take and take-loop path,
exactly as before. The choice is stored per project (the app-side
`arrange.mnkybox` sidecar until the session song object carries it). The
transport's bar:beat readout is the song position in both cases; with SONG
on it is the position the arrangement is playing.

**Sections.** `ADD SECTION` appends a four-bar section. `DUP SECTION` is
the core arranger verb: it copies the selected section (marker, bars and
every clip that starts inside it) immediately after itself and shifts every
later section and clip right by the section length. `LOOP SECTION` sets
the loop window to the selected section. `RENAME` opens the shared keypad
for the section name. Tapping a section selects it; when a section loop is
already armed, tapping another section moves the loop with the selection.

**Length and loop.** `LEN 20:0` is the song length in bars:beats. Tap it to
open the LEN stepper: `BARS -/+` are coarse (four beats), `BEATS -/+` are
fine and grid-snapped, `DONE` closes. The length is independent of the
sections; `LOOP` toggles the loop window on/off. With the loop window at
its default (whole song, end 0) the playhead wraps at `LEN`; a section loop
wraps at that section's edges. The loop band flashes when the playhead
wraps. The playhead itself is a transport seek: tap an empty lane or a bar
position and playback (or the next PLAY from stopped) starts from the
touched point; the Pico keeps its own clock, the app carries the song
position as a signed tick offset.

**Clips.** Tap a clip to select it, then `CLIP ...` for its actions:
`OPEN EDIT` opens the W5 take editor for the clip's take; `MOVE` arms the
clip for the touch drag and the panel value knob (`POS 12.0 bars`);
`TRIM` arms the edge drags and `SHIFT`+knob (`LEN 4.0 bars`); `DUPLICATE`
places a copy in the next free grid position; `GAIN/FADE` opens three
KnobChip cards (gain, fade in, fade out); `MUTE` keeps the block and
silences it; `DELETE` asks first and removes only the placement - the take
file stays on disk. Drag the body to move (grid-snapped) and the left/right
16 px edges to trim. A long press duplicates. `BAR`, `1/2`, `1/4`, `1/16`
set the snap grid; `Z-`/`Z+` and a two-finger pinch zoom. Clips on one
track never overlap: an edit that would overlap is refused with a notice.

**Panel.** NAV walks clips spatially: LEFT/RIGHT move along a track by grid
step, UP/DOWN move to the nearest clip on the adjacent track. PRESS opens
the clip's `...` actions; `MOVE`/`TRIM` leave the ring in EDIT on that clip,
where the value knob changes `POS` (move) or, with SHIFT held, `LEN`
(trim). The SHIFT face holds the section verbs and the grid/zoom controls.
The keypad, confirmations and the clip sheet trap the panel while open.

**Punch into song.** With SONG on, the Recorder SHIFT punch markers are
song positions, counted from the active loop start (loop-relative). Set
PLAY FROM/PUNCH IN/PUNCH OUT as usual and roll RECMODE PUNCH: the take opens
at PUNCH IN, closes at PUNCH OUT (the transport keeps rolling), and the
written take is placed as a clip at the punch position on its recorded
track. The notice names it: `TAKE SAVED -> clip at bar 9`. Punch engages
once per REC press; it does not re-arm on a loop wrap. With SONG off the
session punch fields keep their original absolute-measure meaning.

**RENDER SONG.** `RENDER SONG` writes `song-<date>.wav` (stereo, 48 kHz,
the song length) into the take folder and lists it in the Recorder as a
`MASTER` take (selecting it makes it that track's ACTIVE take; it never
changes automatically). The notice says `SONG RENDERED song-<date>.wav -
master mix rendered (main bus)`. The file is the ordinary master path: per
track gain, EQ, FX1/FX2 rack, fader, pan, mute/solo and destination, then
master gain, the main compressor and Main EQ. Tracks routed to OUT3&4 or
PHONES do not reach the main bus and are not in the file; the click is
forced off. Stop a running take before rendering.

**Worked example - V1 drums to song.** PAIR 01/02, `COUNT-IN 2 bars`, ARM
the drums track, REC, play the part, STOP. Take row `...` -> `NEW TRACK
FROM THIS TAKE` -> name `beat`. In ARRANGE: `ADD SECTION` -> `INTRO`;
`DUP SECTION` -> `VERSE`; rename it; move the clip blocks into place. Set
`LEN` to the song length, arm `LOOP SECTION` on the verse, tap the empty
lane at bar 9, set PUNCH IN 9 / PUNCH OUT 11 and record the fill; the take
appears as a clip at bar 9. `RENDER SONG` gives the master bounce.

## 11. PROJ - projects and templates

**You see** the project list: name, stamp, the current marker, the dirty
dot, and SAVE on the left. A project is a folder under
`~/Music/mnkybox-projects` holding the session file, its takes and their
peak sidecars.

**Load and select are separate.** Tapping a row loads that project (and asks
first when the current one is dirty). The `...` target at the row's right
edge only selects it, so RENAME and DELETE never require loading something
first. The header hint reads `tap loads - ... selects`.

**SAVE** writes the current project. A session that has never been saved
gets a folder named `Project-<date>-<time>`.

**SHIFT face** is project management: the same list (five rows) above NEW,
RENAME, DELETE and the template actions. RENAME and DELETE act on the
selected row and name it in the confirm dialog - `DELETE PROJECT`, then
`Delete Project-20261106-114731?` - so you can see what you are about to
lose. With nothing selected both buttons dim and tapping them asks for a
selection first. Loading the current project reverts it to the saved file.

**Templates** are settings-only session files. SAVE TEMPLATE marks the
current project as one, NEW TEMPLATE starts a template from the current
settings, CLEAR FLAG turns the mark off. Loading a template brings the rig
back and keeps the current take registry unless the template carries one.

**Session.** The rehearsal ends: SAVE. New song, same rig: load the
template, select it, RENAME it, record. The dirty dot means save before you
switch or sleep.

## 12. SYSTEM SHIFT - output, metronome, MIDI learn, TOOLS

SYSTEM's primary face is status: the Pico link, clock and transport, the
audio backend and device list, the log tail. Its SHIFT face is the configure
surface, split into three pages plus PANIC.

**Page 1 - metronome, send sources, monitor, comp.** One summary button per
group; each opens a popup whose changes apply immediately and whose CANCEL
restores what you opened with:

- `CLICK` - mode (off, record only, on play), output (`MAIN` / `PHONES` /
  `OUT3&4`), level and count-in bars. This is the same metronome the
  transport strip's CLICK button drives (section 5).
- `SEND SOURCE` - PRE or POST per destination: CUE, OUT34, REC. Gain is
  always applied, so PRE is before the fader and POST after it. FX1 and FX2
  are not in this list: those knobs are the rack's wet levels (section 9).
- `MONITOR` - phones source CUE or MAIN, cue style FADER or TOGGLE, OUT3&4
  STEREO or MONO.
- `COMP` - the main-bus compressor: on/off, PEAK or RMS, threshold, ratio,
  attack, release and makeup.

This page is where the output stage is configured, because there is no
separate MAIN screen. Levels themselves live on the MASTER strip (section
6); phones source, cue style, OUT3&4 mode and the compressor live here. A
clock-source row on this page chooses the internal clock or follow (section
16).

**Page 2 - MIDI LEARN.** ARM CAPTURE, then move the control you want to map:
the next control you touch or edit becomes the target - the status line
names it, `target Mixer 02 GAIN - move a control on your controller` - and
the next CC that arrives completes the binding. Every binding row reads `CC
21 ch1 -> Mixer 02 GAIN` with REMOVE, one source per target. The manual
picker below the button (`PICK TARGET MANUALLY`) still lists the same
targets by name. An empty list means there are no bindings yet: ARM CAPTURE
and move a knob on your controller.

**Page 3 - TOOLS.** Take-volume free space with the low-space banner and the
take directory, screen brightness 5-100 % (with the note when the display
cannot be dimmed), and the APP and FIRMWARE versions. There are no update
entries: firmware is flashed over USB (section 17).

**PANIC** sends CC123 on all 16 channels and says so in the notice slot.

**Device outputs.** The three DEVICE OUT CHANNEL pairs on the primary face
are saved with the project, but the audio backend does not map them onto
interface channels yet, and the screen says so. Treat them as a stored
intent, not a routing.

## 13. Recording and playing takes

**Arm and roll.** Arm tracks (ARM on a strip, or the ARM button on the
recorder screen), then REC+PLAY on the panel, REC in the transport strip, or
START TAKE on the recorder. STOP cuts the take. The recorder shows the take
list for the selected track, that track's ACTIVE take and the disk free
space; its header reads `TRACK 02 - bass` and the selector buttons carry the
same name under their number.

**After a take.** The take is written to the project folder, named from the
track (`02-bass-003.wav`, or `02-ch02-003.wav` for an unnamed slot) and
becomes the track's ACTIVE take. A strip that is still armed keeps
monitoring its input, so it plays the room, not the take - and says so. The
recorder reads `TAKE SAVED - ARM is still on, so this track monitors the
input. Disarm (tap ARM) to hear the take.`, the take row carries `ARMED -
MUTES TAKE`, and the strip shows `ARM MUTES TAKE` for a few seconds.
Tapping the chip or ARM disarms and the take plays. PLAY with an armed strip
that has an active take raises `playback: 2 armed track(s) monitor input,
not their takes` in the notice slot. That is the most common "the recorder
failed" moment; it did not.

**Monitor per track.** Four modes, chosen in the channel popout, on TRACK
SHIFT or on the mixer SHIFT strip: `PLAY` plays the take and never the
input; `AUTO` monitors the input while armed (or while the track has no
take) and the take otherwise; `BOTH` layers the input over the take; `MON`
monitors the input only. The letter under a mixer meter is the mode's first
letter.

**Overdub and record source.** Record some tracks while others play back
takes - newest wins where layers overlap, older material mutes underneath.
The record source is PRE or POST from the SEND SOURCE popup; gain always
applies.

**Punch and loop.** Punch markers are edited on RECORDER SHIFT and shown on
the timeline. The take loop (also RECORDER SHIFT) loops a beat window from
the take start for the selected track; an end at or below the start loops
the whole take.

**The take manager.** The recorder lists takes per track - ACTIVE, imported
pointers (PTR) and the rest, newest first. Tapping a row makes that take the
track's ACTIVE take; the `...` target at the row's right edge only selects
it for the SHIFT actions. RECORDER SHIFT manages the selected take: ACTIVE,
LIVE (back to the input), RENAME (file and registry move together), DELETE
(the file goes away, final) and IMPORT (section 15).

**All 18 at once.** Arm every capture track and record: each armed track
writes its own WAV in the same pass. The strips, the take list and the
transport stay live while it runs, so the mix is a performance, not a
setup.

## 14. USB audio streaming

The module does **not** appear as a USB audio device. This is a hardware
limit of the Raspberry Pi 5, measured and documented in `pi/bench/REPORT.md`:
the Pi 5 exposes no USB peripheral controller on any user-facing port (the
USB-C inlet is power input; `dwc2`/OTG exists only on CM4/CM5 carriers), so
class-compliant gadget audio is not achievable regardless of software.

What works instead:

- **Takes to disk**: every armed track records to NVMe as WAV; copy the
  project folder to the computer (section 15). This is the intended "send
  the mix to the DAW" path.
- **Outboard loop**: route Main/OUT3&4 to a hardware interface connected to
  the computer - the UMC1820's outputs can feed a DAW input directly.
- **Future hardware** (not in this revision): a CM5 carrier or a USB-audio
  gadget daughterboard would restore the original streaming goal; the app
  already keeps the 2-in/2-out routing concept (`deviceOut`, USB return
  mapping) so the feature becomes possible without redesign.

## 15. File management and backup

Projects are folders: session file + take WAVs + peak sidecars. Imported
files stay put as pointers - move one and its take breaks until re-imported.

**Import.** RECORDER SHIFT -> IMPORT asks for a WAV path and reads the
header before anything is added: `take.wav - 48 kHz - 2 ch - 0:12`. Only
48 kHz files are accepted; a 44.1 kHz file is refused with `import refused:
44.1 kHz file - convert to 48 kHz first` and is never converted silently.

**Rename and delete.** A take rename moves the file and the registry entry
together; imported pointers cannot be renamed (rename the original). Delete
removes the WAV from the take folder and is final.

**Peaks.** The `.peaks` sidecar is the preview cache for the waveform. It
travels with the take folder; a take without one can render it from the
EDIT ops popup (section 10).

Copy whole folders to back up. The folder is self-contained: session,
audio, peaks.

## 16. MIDI clock sync

The Pico is the clock master: it sends 24 ppqn clock from boot and
START/CONTINUE/STOP on the outputs you leave gated on (TRS OUT 1, TRS OUT 2
and, via the Pi, USB OUT). Tempo edits ride out live. Follow never stops the
clock - it only retunes it, so downstream gear never sees a gap.

**Clock source.** With a Pico linked, SYSTEM SHIFT chooses where the clock
comes from: the internal generator, or following an incoming stream on TRS
IN 1 or TRS IN 2. Follow retunes tempo and phase to the incoming clock; if
the source disappears the generator coasts at its last estimate. Beat-synced
delay, the metronome and punch measures all lock to the running tempo,
whether that tempo is internal or followed.

**Transport.** Incoming start/stop can drive the transport only when
transport follow is enabled; this build stores that setting but has no
control for it yet, so the transport starts from the panel or the
touchscreen, and a take starts the moment you press REC.

**USB.** The Pi mirrors the Pico's clock and transport to USB MIDI, so a
computer follows the same clock as the rack. Thru is not provided: route,
filter and merge in the patchbay instead.

## 17. Firmware update

App updates ship as image releases; the running APP version is in SYSTEM
SHIFT -> TOOLS. The Pico is flashed over its own USB, not from the app: hold
BOOTSEL while connecting it (or tap reset with BOOTSEL held), then copy
`midibrain.uf2` onto the `RP2350` drive; the Pico restarts into its normal
firmware and rejoins the link. Back up projects first. If a flash is
interrupted, the Pico stays in BOOTSEL and the same copy retries it; audio,
takes and projects keep running meanwhile, while the panel and TRS are away.

## 18. Common tasks cookbook

- **Balance the mix**: MIXER faders to even meters, TRIM on TRACK SHIFT for
  a source that sits outside its fader's travel. Gain stages, Vol mixes.
- **First take**: ADD TRACK, hold the strip name to name it, pick INPUT
  SOURCE, ARM, START TAKE, STOP TAKE, then tap ARM off to hear it.
- **Rename instead of rebuild**: hold the strip name (or channel RENAME) -
  the slot, its routing and its takes stay.
- **Two effects, one track**: open the channel popout -> TRACK FX, set FX1
  to DELAY and FX2 to REVERB, then ride the strip's FX1 and FX2 knobs for
  their wet; FX1 feeds FX2.
- **One wet snare in a dry kit**: give that track a REVERB in FX2 and dial
  its wet up; every other track stays dry because the rack is per track.
- **Private cue, public main**: phones = CUE in SYSTEM SHIFT, per-track CUE
  sends up on what you need to hear, engineer keeps Main.
- **Outboard loop**: OUT3&4 mono, pre-fader sends out, returns into spare
  inputs as new tracks.
- **Chop a break**: EDIT, tap a lane label to pick the take, drag a region,
  IN/OUT for exact edges, OPS -> SPLIT at each hit, UNDO if a chop is wrong.
- **New song, same rig**: load the template project, select it, RENAME it,
  record.
- **Map a controller**: SYSTEM SHIFT -> MIDI LEARN, ARM CAPTURE, touch the
  control you want, move a CC; the row reads `CC 21 ch1 -> Mixer 02 GAIN`,
  REMOVE to unbind.
- **Play the set**: disarm every track, verify each track's ACTIVE take,
  PLAY. Nothing armed is nothing recorded.

## 19. Troubleshooting

- **Dead module**: supply and cable first - brownouts corrupt takes.
- **Meters move, silence**: mute? solo on another strip? output destination?
  phones = CUE with cue down? monitor MON? UMC outputs 1&2 connected?
- **Recorded, but playback is the room**: the strip is still armed; an armed
  strip monitors its input and mutes its take (section 13). Tap ARM.
- **Doubling**: monitor BOTH -> AUTO.
- **Effect on a track that should be dry**: that track's rack slot is wet;
  open TRACK FX and set the slot to NONE or drop its wet (section 9).
- **X/Y grid moves nothing**: the grids follow the selected track - select
  it first, and a slot set to NONE has nothing to bend.
- **Clicks on 17/18**: ADA8200 sync (section 1), not the mix.
- **MIDI slop**: clock-critical gear on TRS, not USB.
- **Click in a take**: impossible by design - the click goes to an output,
  not into the takes; what you hear is the room. Use phones.
- **Import refused**: the file is not 48 kHz; convert it (section 15).
- **Encoder changes tempo instead of the value**: press it once to enter
  EDIT on the focused value first.
- **Empty EDIT lanes**: the track has no ACTIVE take; pick one on the lane
  label.
- **Disk full**: the banner is in SYSTEM SHIFT -> TOOLS; stop cleanly,
  archive or delete (delete is final).
- **Lost edits**: saving is manual - the dirty dot means save before you
  switch or sleep.

## Appendix A - specifications

26HP/3U, <=110 mm installed, zero rail power (external 5 V USB-C).
800x480 touch DSI. 18 inputs / 20 outputs through the UMC1820 (+ ADA8200
over S/PDIF), 48 kHz/24-bit, NVMe capture of every armed track. 2x TRS MIDI
in/out (Type A) + USB MIDI hub. Pico 2 panel/clock controller + Pi 5 app
host, internal SPI. REC / OVERDUB / STOP / PLAY / PLAY START + TEMPO / NAV /
SHIFT / EXIT, 3 semantic LEDs on light pipes, MIDI clock master with follow.
USB audio streaming is absent by hardware (section 14).

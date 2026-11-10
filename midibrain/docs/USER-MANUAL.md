# MIDI Brain - User Manual

The complete instrument: an 18-input, 64-track Eurorack recorder, mixer and MIDI hub
in 30HP. Touchscreen when you want it, panel controls when you don't.

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
1A. The POWER button and safe shutdown
2. Meters, inputs, names, gain
3. Controls overview - joystick, buttons, encoder, touch, LEDs
4. Modes at a glance
5. Transport strip
6. MIXER - three strips a page + Master
7. CHANNEL - the track's detail view
8. EQ - four bands per track and the main
9. FX RACK - two effect slots per track, X/Y grid
10. EDIT - timeline, regions, trim and chop
10A. ARRANGE - sections, clips, song mode, render
11. PROJ - projects and templates
12. SYSTEM SHIFT - summaries, diagnostics, MIDI learn
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

**Mount.** 30HP (152.0 mm), 3U, <=110 mm deep - check case depth before the rails.
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

## 1A. The POWER button and safe shutdown

The panel POWER button (bottom right of the front panel) drives the Pi's
power input. It has no hard off switch - the module draws standby power
while plugged in.

**Press** while off: the Pi boots. **Press** while running: a graceful
shutdown request. The app saves the open project and every dirty state,
then fills the screen with `SAFE TO POWER OFF`. When that screen is up the
system has halted cleanly and you may cut power (or leave it - standby draw
is accepted by design). The screen clears itself after a few seconds or at
a touch.

If a save ever fails the screen says `SAVE FAILED - DO NOT POWER OFF`
instead - fix the storage problem before pulling power, the work is still
in memory.

**Press twice** (or hold): the PMIC force-off - the last resort when the
system is wedged. Nothing is saved; use it only when the graceful path is
not responding.

Closing the app window on a desktop session saves through the same path,
quietly - no shutdown screen.

---

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
  forward. The ring walks the
  persistent destinations (Mixer, Recorder, Arrange, More) - PRESS on
  More opens the page grid (Routing, Projects, MIDI, EQ, XY, System),
  pick one and the grid closes with the ring on that screen.
- **SHIFT/EXIT**: short press = back / dismiss / leave EDIT; hold ~0.5 s
  = the tab's SHIFT face (section 4). On touch the `CONFIGURE` toggle in the
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

Left to right: the state word, the position (two lines), tempo, the SONG
tag, the RECMODE badge, the big transport glyphs, the feedback well, the
Configure toggle and the unsaved dot.

**State and position.** The state word is `STOP`, `ROLL` or `REC`. The
position reads `012:3:02` in large type with the elapsed time under it.

**Tempo.** The BPM value with a chevron opens the TEMPO popup (40-240
slider, -1/+1, TAP three times for tap tempo). The panel knob sets the
same value.

**SONG tag.** `SONG ON` means playback follows the arrangement (section
10A); `SONG OFF` means the classic free take path. Tap it to switch; the
choice is stored in the project.

**RECMODE badge.** Shows `NOW`, `COUNT-IN` or `PUNCH` whenever it changes
what pressing REC will do. It is always visible; the full choice lives in
the options sheet.

**Touch transport.** PLAY, STOP and REC are large (64 px) and always
present - the panel's own buttons own transport when a Pico is linked, but
a bench session runs from the screen too. **STOP while stopped returns to
bar 1** (notice `BACK TO START`) - the classic hardware move. SONG mode
keeps that zero across START; free mode always begins at bar 1.

**Feedback well.** The dark slot right of centre is the one place values
and notices appear: a parameter you just changed, a notice like `TAKE
SAVED`, or `SAVE FAILED`. Values and notices replace each other here - no
more hunting for tiny transient text.

**Configure toggle.** `CONFIGURE` switches the current screen to its
second face; it reads `PRIMARY` to come back. The physical SHIFT button
does exactly the same, and the return label never truncates.

**Options sheet.** The transport's less frequent choices live here:
RECMODE (NOW / COUNT-IN / PUNCH), CLICK mode (off / record only / on
play), CLICK output (MAIN / PHONES / OUT3&4) and the metronome setup.
Closing the sheet keeps every change - nothing to confirm.

**The dot.** The far-right dot marks unsaved work.

## 6. MIXER - three strips a page + Master

**You see** your session as three fat strips per bank plus a compact
MASTER column on the right. `ADD TRACK` appends a track and opens the
keypad for its name; the pager reads `TRACKS 1-3 / 8` and steps with `<`
and `>` - tap the pager to open the bank grid and jump straight to any
bank. The list grows to 64 tracks and only real tracks appear.

Each strip is a performance surface: the track name (tap to select, hold
to rename), a `CH` button that opens the channel view (section 7), the
gain value in large type, a tall fader beside a real level meter (taper,
peak hold and a clip crown), and large M / S / ARM switches along the
base. When the track's mode is not plain PLAY, a readable pill says so -
`OVERDUB`, `LIVE` - instead of a tiny selector.

**Touch workflow.** Riding a fader never opens the channel view: the name,
the CH button and the fader are three separate touch surfaces. Two
fingers adjust two strips at once - ride a vocal against a bassline with
both hands. Mute gray, solo green, arm red, selection cyan. A clip crown
clears itself after a couple of seconds, so a glance after a loud passage
tells you which channel needs trimming.

**What left the strip.** Pan, input assignment, track mode and sends live
in the channel view (section 7) - the strip keeps only what you touch
while playing. The SHIFT face (CONFIGURE) carries the wet sends for the
focused strip: FX1, FX2, CUE and OUT34.

**Panel workflow.** The ring walks the strips in view order (the mixer
face's ring is the mixer's own - not the track list's). PRESS on a toggle
flips it; PRESS on a value enters EDIT and the directions or the knob
slew it with a popup readout. The bank grid takes PRESS to pick a page
and EXIT to close.

**Session.** Soundcheck: ADD TRACK per source, name them, open CH to pick
the input and set gain so loud passages tickle the top of the meter, then
mix on the strip faders. The meters keep running while you work.

## 7. CHANNEL - the track's detail view

The `CH` button on a strip (or PRESS on the strip's expand stop from the
panel) opens the channel view for that track: everything that does not
belong on the performance strip.

**Pan** (knob chip with a value card), **input assignment** (the pair/
source selectors), **output destination**, **track mode** (PLAY / ARM /
OVERDUB / LIVE - the same words the strip pill shows), **rec source**
(PRE / POST / POST FX), the **FX slots** entry (section 9) and, at the
bottom, separated from the everyday controls, **REMOVE TRACK**.

Sends (FX1, FX2, CUE, OUT34) adjust on the strip's SHIFT face while the
channel view stays a tap away. Every value applies as you change it -
there is no Apply button anywhere in this app.

The view keeps its own panel ring (so every control is reachable by
joystick and encoder), and closing it restores the panel focus to
whatever opened it.

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
The plugin list is discovered at startup from `CLAP_PATH` and the standard
locations (`~/.clap`, `/usr/lib/clap`, `/usr/local/lib/clap`, and the
macOS Audio Plug-Ins CLAP folders). If the machine has no CLAP plugins
the list says so honestly instead of offering dead entries - DELAY and
REVERB are built in and always work. A plugin that fails to load is
reported as `NOT LOADED` in the slot rather than failing silently, and
switching an effect on a slot replaces it cleanly at any time.

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

**You see** the take at full width: a large waveform (about 254 px tall),
the playhead, the selection with its IN and OUT grab tabs, and beneath it
a single row of controls: the operation selector, the parameters of the
selected operation, and the verb row.

**Operations.** One selector lists them all - TRIM, SPLIT, GAIN, FADES,
NORMALIZE, DELETE, CLEAR and friends. Only the selected operation's
parameters show (a knob chip or the IN/OUT readout), so the screen stays
quiet. `APPLY <OP>` names the exact verb it will run - it is the one big
button, and it does the thing. `UNDO` sits right beside it and steps back
through the edit history. `RENDER REGION` renders the selection to a new
take. Everything is reachable in fullscreen too - the verb row travels
with the waveform.

**Precision without fat fingers.** IN is a grab tab on the waveform's top
edge, OUT on the bottom edge - they can sit microns apart and never cover
one another, and dragging one never moves the other. For exact work use
the numeric/encoder path: PRESS on IN or OUT and nudge bars:beats with
the knob. Two fingers pinch to zoom the waveform (nothing else moves -
the popup stays put).

**Nothing is destructive until you say so.** Edits are a region list
rendered at playback - the take file is never rewritten - so UNDO,
picking a different take and walking away are always safe.

**Panel workflow.** NAV moves the cursor (SHIFT for coarse bars). PRESS
sets IN, then OUT; PRESS on IN or OUT enters EDIT for a bars:beats nudge.
The operation list is a trapped ring; PRESS applies, EXIT walks away.

**Session.** The MPC habit, end to end: record a pass, trim the heads,
normalize, chop the break, keep two takes for two arrangements.

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
exactly as before. The choice is stored with the project's session data. The
transport's bar:beat readout is the song position in both cases; with SONG
on it is the position the arrangement is playing.

**Sections.** `ADD SECTION` appends a four-bar section. `DUP SECTION` is
the core arranger verb: it copies the selected section (marker, bars and
every clip that starts inside it) immediately after itself and shifts every
later section and clip right by the section length. The SECTIONS sheet holds them all - `ADD`, `DUP`, `LOOP`, `LEN` and
`RENAME` - as one row of large buttons. Tapping a section selects it; when a section loop is
already armed, tapping another section moves the loop with the selection.

**Length and loop.** `LEN 20:0` is the song length in bars:beats. Tap it to
open the LEN stepper: `BARS -/+` are coarse (four beats), `BEATS -/+` are
fine and grid-snapped, `DONE` closes. The length is independent of the
sections; `LOOP` toggles the loop window on/off. With the loop window at
its default (whole song, end 0) the playhead wraps at `LEN`; a section loop
wraps at that section's edges. On every wrap the loop band and the
transport position pulse together, so the cycle restart is visible from
across the room. The playhead itself is a transport seek: tap an empty lane or a bar
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
16 px edges to trim. A long press duplicates. The `GRID` selector cycles the snap grid (BAR, 1/2, 1/4, 1/16); `Z-`/`Z+` and a two-finger pinch zoom. Clips on one
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

**RENDER SONG.** The SONG sheet holds `RENDER SONG`, `LEN` and `LOOP`; `RENDER SONG` writes `song-<date>.wav` (stereo, 48 kHz,
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

## 12. SYSTEM SHIFT - summaries, diagnostics, MIDI learn

SYSTEM's primary face is a readable summary of the machine: audio device
and sample rate, clock source and state, Pico connection, and storage
(free space and where takes go). Everything is legible across the room.
The raw protocol counters, device lists and the log tail live behind the
`DIAGNOSTICS` button - the troubleshooting data is all still there, just
not default. Its SHIFT face is the configure surface, split into three
pages plus PANIC.

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
cannot be dimmed), and the APP version. The FIRMWARE row reads "not reported by the link status"
until the protocol carries a version field. There are no update entries:
firmware is flashed over USB (section 17).

**PANIC** sends CC123 on all 16 channels and says so in the notice slot.

**Device outputs.** The three DEVICE OUT CHANNEL pairs on the primary face
are saved with the project, but the audio backend does not map them onto
interface channels yet, and the screen says so. Treat them as a stored
intent, not a routing.

## 13. Recording and playing takes

**Arm and roll.** Arm tracks (ARM on a strip or the big ARM button on the
recorder), then REC+PLAY on the panel, REC in the transport strip, or
START TAKE on the recorder. STOP cuts the take. The recorder leads with
what matters: the recording state and elapsed time in large type, the
selected track as one big button (`02 - bass`; tap it for the track
picker, or step with prev/next), the big ARM and START/STOP TAKE actions,
a readable recording-source summary, and the take list below. Take rows
are tall and carry identity, duration, the ACTIVE state and EDIT; the
full filename, timestamp, storage path and file size live in the row's
details disclosure. `ARMED` and `RECORDING` are told apart by words and
an icon as well as colour.

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

30HP/3U (152.0 mm), <=110 mm installed, zero rail power (external 5 V USB-C).
800x480 touch DSI. 18 inputs / 20 outputs through the UMC1820 (+ ADA8200
over S/PDIF), 48 kHz/24-bit, NVMe capture of every armed track. 2x TRS MIDI
in/out (Type A) + USB MIDI hub. Pico 2 panel/clock controller + Pi 5 app
host, internal SPI. REC / OVERDUB / STOP / PLAY / PLAY START + VALUE / NAV /
SHIFT / EXIT, 3 semantic LEDs on light pipes, MIDI clock master with follow.
USB audio streaming is absent by hardware (section 14).

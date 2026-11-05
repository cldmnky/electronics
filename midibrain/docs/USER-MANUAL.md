# MIDI Brain — User Manual

The complete instrument: an 18-track Eurorack recorder, mixer and MIDI hub
in 26HP. Touchscreen when you want it, panel controls when you don't.

Two computers share the work behind the panel. The Raspberry Pi 5 runs the
app — mixing, recording, effects, projects. The socketed Pico 2 owns
everything timing-critical — MIDI ports, panel switches, the master clock —
over an internal SPI link that keeps time even when the app is busy. Audio
conversion lives outside the module: a UMC1820 (plus ADA8200 for the full
18 channels) over USB, independently powered, captured at 48 kHz / 24-bit
to NVMe.

Workflow debts are acknowledged: project and take handling learn from the
1010music Bluebox, input monitoring and the sampling workflow from the Akai
MPC. What follows describes this instrument's own behavior, completely and
in the present tense.

## Table of contents

1. Setup — mount, power, audio, MIDI, first boot
2. Meter bridge, input mapping, labels, gain
3. Controls overview — joystick, buttons, encoder, tempo, touch, LEDs
4. Modes at a glance
5. Transport strip
6. MIXER — one setting across tracks
7. TRACK — one track, everything, plus Q-Link macros
8. MAIN — outputs, phones, compressor
9. EQ — four bands per track and the main
10. FX — delay, reverb, X/Y grids
11. EDIT — timeline, regions, trim and chop
12. PROJ — projects, templates, tempo, metronome, MIDI learn
13. TOOLS — disk, brightness, versions, updates
14. Recording and playing takes
15. USB audio streaming
16. File management and backup
17. MIDI clock sync
18. Firmware update
19. Common tasks cookbook
20. Troubleshooting
Appendix A — specifications

---

## 1. Setup

**Mount.** 26HP, 3U, ≤110 mm deep — check case depth before the rails.
Four screws, standard pattern.

**Power.** The module draws no rail power. Feed the front USB-C inlet from
a 5 V high-current supply over a qualified cable. Thin phone cables brown
out under full load (18 tracks + screen + NVMe) and corrupt takes — when
in doubt, replace the cable first.

**Audio.** UMC1820 → Pi USB. For channels 17–18, connect the ADA8200's
S/PDIF to the UMC1820 and slave the ADA8200 to it. Unsynced S/PDIF clicks
once per drift cycle and no software can fix it. A demo project is on the
drive: load it, PLAY, hear takes.

**MIDI.** TRS Type-A IN/OUT plus a powered USB hub on the data
feedthrough for controllers and interfaces. The Pico is always clock
master on TRS; the Pi disciplines USB clock from the Pico's ticks.

## 2. Meter bridge, mapping, labels, gain

**Meter bridge layout.** Show 6, 9, 12 or all 18 tracks, in single or
double rows. Fewer tracks = bigger strips, bigger touch targets, more
characters in track names. Find it on MIXER SHIFT.

**Map inputs to tracks.** TRACK SHIFT shows the mapping grid: touch a
track, then touch one input (mono) or two side-by-side inputs (stereo
pair 1/2, 3/4, …). Each input feeds exactly one track — mapping it to a
second track unmaps the first. A stereo pair shares monitor, arm and take
state; pan spreads the pair.

**Labels.** Touch a track header to rename via the keypad (ABC/123, case
toggle, touch or joystick cursor). Dense layouts truncate to fit — the
full name still travels into take filenames.

**Gain.** Gain trims the input before everything (sends, faders, taps).
Set it first with Vol at unity: loud passages should tickle the top of
the meter without lighting the red clip-hold LED. Clip memory holds until
you touch the meter — staging 18 channels is a glance, not a chore.

## 3. Controls overview

- **NAV cluster**: joystick up/down/left/right + press. Moves the focus
  ring, activates, edits values. Hold a direction and it repeats (400 ms
  first, 80 ms after) for scrolling lists and slewing values.
- **SHIFT/EXIT**: short press = back / dismiss / leave EDIT; hold ≈0.5 s
  = SHIFT modifier for alternate faces and coarse adjust.
- **TEMPO**: tap repeatedly to tap-tempo (4 taps set it); SHIFT+TEMPO
  nudges ±1 BPM; hold to open the tempo popup. Beat-synced delay,
  metronome and punch measures follow instantly.
- **Encoder**: edits whatever has focus (the Q-Link habit: focus a
  control, turn the knob). SHIFT+encoder = fine steps. Four assignable
  macro slots per project bind the encoder to any parameter — filter
  cutoff for a build, cue level for a gig — with the binding shown
  beside the tempo.
- **Transport**: REC, OVERDUB, STOP, PLAY, PLAY START. REC+PLAY rolls
  recording on armed tracks. LEDs: REC/OVERDUB blink armed-stopped,
  solid armed-recording; PLAY lit while rolling.
- **Touch**: drag sliders (you may start the drag and finish outside the
  frame), tap toggles, multi-finger drags on independent controls. Every
  change raises a popup readout (`Center`/`L30`/`R45`, dB, ms) that fades
  ~1.5 s after you stop.
- Touch and panel share one focus — whichever you touch last owns it.

## 4. Modes at a glance

| Tab | Primary face | SHIFT face |
|---|---|---|
| Mixer | one setting × tracks | gains, sends, monitor |
| Track | one track × settings | input/output, pre/post |
| Routing | patchbay wires | filters, transpose |
| FX | delay + reverb params | returns, X/Y grids |
| Recorder | transport + take list | take tools, punch, loop |
| System | status + backend | metronome, TOOLS |
| Projects | list, save/load | templates, keypad ops |

SHIFT+LEFT/RIGHT switches tabs anywhere. Transport stays live in all
modes. Dialogs and popups trap focus until OK/Cancel/EXIT.

## 5. Transport strip (every screen)

`STOP | 001:3:15 / 00:01 | 120.00 BPM | CLICK` — state, position
(measures:beats / time), tempo (tap TEMPO or touch to edit), metronome
master. CLICK enables the click; the mode decides audibility (§12).

## 6. MIXER — one setting across tracks

**You see** the meter bridge: nine strips filling the screen (bank 1–9 or
10–18, switched at the top), each with a live meter, track name, clip-hold
LED at the crown, and M / S / ARM toggles at the base. MASTER rides the
right edge with its own tall meters. Along the bottom, the setting picker:
Vol, Pan, Gain, FX1, FX2, CUE, OUT3&4. Whatever is picked is what every
strip shows — one setting, nine tracks, zero menu diving.

**Touch workflow.** Tap a setting, then drag any strip's fader directly.
Drags stay live past the strip frame, and two fingers adjust two strips
at once — ride a vocal against a bassline with both hands. Tapping M, S
or ARM flips that strip's state with the color contract: green solo, gray
mute, red arm, pink focus. A clip LED stays lit until you tap its meter,
so a glance after a loud passage tells you exactly which channel needs
trimming.

**Panel workflow.** Joystick moves the cyan ring strip to strip (spatial:
left/right across the bank, up/down between setting rows where they
exist); PRESS on a toggle flips it; PRESS on a fader enters EDIT and the
ring goes amber — now up/down (or the encoder) slews the value with hold
repeat, the popup reads `−9.0 dB` or `L30`, PRESS confirms, EXIT abandons.
Bank switch is a dedicated top control, SHIFT+LEFT/RIGHT never leaves the
tab by accident mid-take.

**SHIFT face** holds everything per-track that isn't the picked setting:
gains, all four sends, INPUT MON. Same strips, second page — the primary
face never scrolls.

**Session.** Soundcheck: pick Gain, walk the bank with the joystick,
trim each strip until meters dance below clip, tap each clip LED clear.
Then Vol, and the mix is up in under a minute.

## 7. TRACK — one track, everything, plus Q-Link macros

**You see** a single channel strip grown to full height: big meter left,
Vol fader, Gain, Pan with its Center/position readout, FX1/FX2/CUE/OUT3&4
send knobs, REC/MUTE/SOLO, INPUT MON (Off/Auto/On), and the four macro
slots. A track selector across the top jumps between channels; the strip
shows the focused track's name large enough to read across the room.

**Touch workflow.** Everything drags in place; the pan knob shows `Center`
bold when detented so centeredness is a fact, not a guess. Macros display
their live binding (`CUTOFF · ch2`) and drag like any knob.

**Panel workflow.** Up/down walks the strip's controls, left/right jumps
strips without leaving the tab; PRESS edits, encoder slews, EXIT backs
out to the strip, EXIT again to Mixer. Binding a macro: focus the macro
slot, PRESS, then touch or focus the target parameter anywhere in the
app — the slot learns it Q-Link style and shows the binding. SHIFT+PRESS
on a macro clears it.

**SHIFT face**: input source (mono channel or stereo pair 1/2, 3/4, …),
output destination (Main / OUT3&4 / phones), pre/post per destination. A
stereo pair shares monitor, arm and take state; pan spreads the pair.

**Session.** A channel giving trouble: open TRACK, check INPUT MON
(doubling? → Auto), check output destination (silence? → not Main),
ride Gain against the clip LED, bind the problem control to a macro for
the rest of the night.

## 8. MAIN — outputs, phones, compressor

**You see** four output blocks — MAIN 1&2, OUT3&4 (or OUT3 + OUT4 in mono
mode), PHONES — each with level fader, mute, and live meter, plus the
compressor block. No scrolling: the whole output stage fits one face.

**Touch workflow.** Drag a fader; tap MUTE. Phones source toggles CUE /
Main — flip to Main to check what the room hears, back to CUE for your
private mix. OUT3&4 mode toggles stereo pair / dual mono; in mono mode
the block splits into two independent sends for outboard loops.

**Panel workflow.** Left/right across blocks, up/down level-or-mute,
PRESS edits the fader, encoder slews. Comp parameters are knobs with
popup readouts (`−18.0 dB`, `4:1`, `10 ms`) — threshold and makeup get
coarse SHIFT steps because you set them by ear, fast.

**Session.** Soundcheck the room on MAIN, build your private mix on CUE
with phones sourced to CUE, glue the mains with the comp (Peak for
control, RMS for music), and never wonder whether the comp touched the
outboard sends — it can't; OUT3&4 are pre-comp by design.

## 9. EQ — four bands per track and the main

**You see** the response grid: log-frequency left-to-right, ±dB
up-and-down, four nodes with the selected one pink, the summed curve
white. Corners show Gain / Q-or-shape / Frequency / Type for the
selected band. Track selector (or the Main meter) picks the subject.

**Touch workflow.** Drag a node and frequency × gain follow your finger;
the curve redraws live. Type cycles None → LCut → LShelf → Param →
HShelf → HCut. Fullscreen toggle hides the bridge for fingertip
precision on small nodes.

**Panel workflow.** Left/right moves the node in frequency, up/down in
gain (Q on SHIFT+up/down); A-button steps bands; PRESS on a corner value
enters EDIT for encoder slewing with popup readouts. Cut filters ignore
gain and say so — the popup reads `LCut · 80 Hz`, never a meaningless dB.

**Session.** High-pass everything that isn't bass (LCut, Q≈1), notch the
room out of the vocal with a narrow Param, shelf air onto the mix bus —
all while the take rolls, because EQ is live, never a render step.

## 10. FX — delay, reverb, X/Y grids

**You see** two effect blocks side by side. FX1: Time (musical increments
when Beat is on — it syncs to the clock whether or not you're rolling),
Feedback, Cutoff, Width, Beat, Filter, Ping. FX2: model browser (15),
Time, Level, high/low input cuts, pre-delay, Freeze. Returns ride the
SHIFT face.

**Touch workflow.** Knobs drag; Beat/Filter/Ping/Freeze toggle; model
browser scrolls and previews (the tail changes as you browse — selection
is instant, no apply step). The X/Y grids are the performance face: delay
X = cutoff / Y = width, reverb X = size / Y = level. One finger bends a
tail mid-phrase; two fingers bend both effects at once.

**Panel workflow.** Ring walks the knobs; PRESS edits with encoder + hold
repeat; Freeze is a PRESS toggle so you can punch it rhythmically from
the joystick. On the grids the joystick becomes the finger: EDIT moves
the point, SHIFT moves it coarse, PRESS drops it and exits.

**Session.** Dial a dotted-eighth on FX1 with Beat on, feed it from the
vocal's send, freeze the reverb tail over the breakdown, unfreeze on the
drop — all without looking at your hands.

## 11. EDIT — timeline, regions, trim and chop

**You see** tracks top-to-bottom, time left-to-right, waveform thumbnails
drawn from cached peak data (hour-long takes scroll smoothly), the
playhead, and loop/punch markers displayed but edited on their own
screens. The selected region shades; the ops popup lists Trim, Split,
Gain, Fade in/out, Normalize, Delete region, Clear edits, Render — each
previewing on the waveform before it commits, with undo behind it.

**Touch workflow.** Drag on a waveform to select; pinch-buttons zoom
time; tap a take in the per-track list to load it to the viewer; preview
streams the selection to the cue bus while the mix plays on. Split at
the cursor repeatedly to chop a break into regions, then audition each
region to cue and keep the winners.

**Panel workflow.** NAV moves the cursor (SHIFT = coarse bars), PRESS
sets in-point, PRESS again sets out-point, EDIT adjusts the nearest edge
with popup times in bars:beats, EXIT clears the selection. The ops popup
is a trapped focus list — up/down, PRESS to arm the op, adjust its value
(dB, ms), PRESS to commit, EXIT to walk away. Nothing commits on focus
change alone.

**Session.** The MPC habit, end to end: record a pass, trim the heads,
normalize, chop the break, audition chops to cue, ACTIVE the keepers
across two tracks, alternate mutes for the arrangement. Files are never
rewritten — the edit list renders at playback — until you Render an
explicit keeper.

## 12. PROJ — projects, templates, tempo, metronome, MIDI learn

**You see** the project list: names, dirty dots (unsaved work), the
current marker, stamps. NEW / SAVE / RENAME / DELETE open the keypad
(full QWERTY, joystick-navigable, encoder moves the cursor). SAVE
disables when there's nothing to lose; loading the current project
reverts it; loading with a dirty project asks first.

**SHIFT face**: templates (save-without-takes, new-from-template),
tempo (BPM, tap source, count-in bars), metronome popup (mode
Off/Rec/On-play, output Main/phones/OUT3&4, gain), MIDI-learn list
(`channel:CC → target`, touch-hold clears, one source per target).

**Session.** Rehearsal ends: SAVE. New song same rig: new-from-template,
rename, record. Controller mapping night: ARM CAPTURE, touch targets
across Mixer/TRACK/MAIN/EQ/FX, move knobs, SAVE — the whole control
surface persists with the project.

## 13. TOOLS — disk, brightness, versions, updates

**You see** NVMe free space with a low-space banner mirrored to the
transport area, brightness + fine (for flicker), app and Pico versions,
and update entries. One screen, glanced at once a night: space for the
set, light for the room, versions for the support email.

## 14. Recording and playing takes

Arm strips (or the Main meter for the mix), REC+PLAY, STOP to cut. Record
some tracks while others play back takes — newest wins where layers
overlap, older material mutes underneath. Rec source per destination
(pre/post fader, gain always applied). Punch drops in/out on measure
markers. Loop plays a take or a beat range. Monitor per track: Auto
hears input when armed or when nothing plays, the take otherwise; On
layers deliberately; Off is takes-only. Record all 18 plus Main at once
for the full-session capture; takes land named and ACTIVE.

## 15. USB audio streaming

The module does **not** appear as a USB audio device. This is a hardware
limit of the Raspberry Pi 5, measured and documented in
`pi/bench/REPORT.md`: the Pi 5 exposes no USB peripheral controller on
any user-facing port (the USB-C inlet is power input; `dwc2`/OTG exists
only on CM4/CM5 carriers), so class-compliant gadget audio is not
achievable regardless of software.

What works instead:

- **Takes to disk**: the Main bus and all 18 channels record to NVMe as
  WAV; copy the project folder to the computer (§16). This is the
  intended "send the mix to the DAW" path.
- **Outboard loop**: route Main/OUT3&4 to a hardware interface connected
  to the computer — the UMC1820's outputs can feed a DAW input directly.
- **Future hardware** (not in this revision): a CM5 carrier or a USB-audio
  gadget daughterboard would restore the original streaming goal; the
  app already keeps the 2-in/2-out routing concept (`deviceOut`, USB
  return mapping) so the feature becomes possible without redesign.

## 16. File management and backup

Projects are folders: session file + take WAVs + peak sidecars. Imported
files stay put as pointers — move one and its take breaks until
re-imported. Rename takes in the manager (file and registry move
together). Copy whole folders to back up. 48 kHz only at import: refused
with a reason, never silently converted.

## 17. MIDI clock sync

The Pico sends clock on TRS and USB whenever PLAY is active; tempo edits
ride out live. Incoming clock on either port is followed automatically —
tempo, beat delay, click and punch measures lock to it. With tracks armed,
REC then the external Play starts the take on the downbeat. Thru is not
provided: route, filter and merge in the patchbay instead.

## 18. Firmware update

App updates ship as image releases (version in TOOLS). Pico updates come
from the Pi: drop the file, verify, the Pico reboots into its updater
with progress on screen, then rejoins the link. Back up projects first.
If an update ever strands the Pico, its isolated USB reprograms it with
the rack on.

## 19. Common tasks cookbook

- **Balance the mix**: Mixer Gain per strip to even meters, then Vol to
  taste — gain stages, Vol mixes, never the reverse.
- **Private cue, public main**: phones = CUE, per-track cue faders up on
  what you need to hear, engineer keeps Main.
- **Outboard loop**: OUT3&4 mono, pre-fader sends out, returns into spare
  inputs as new tracks.
- **Build drop, one knob**: macro-bind filter cutoff + FX2 send, SHIFT
  face for the bindings, perform on the primary.
- **Chop a break**: EDIT split at each hit, audition regions to cue,
  ACTIVE the keepers across two tracks, alternate mutes.
- **New song, same rig**: new-from-template, rename, record.
- **Play the set**: disarm all, verify ACTIVE takes, PLAY. Nothing armed
  is nothing recorded.

## 20. Troubleshooting

- **Dead module**: supply and cable first — brownouts corrupt takes.
- **Meters move, silence**: mute? output destination? phones=CUE with cue
  down? monitor Off? UMC outputs 1&2 connected?
- **Doubling**: monitor On → Auto.
- **Clicks on 17/18**: ADA8200 sync (§1), not the mix.
- **MIDI slop**: clock-critical gear on TRS, not USB.
- **Click in a take**: impossible by design — it's the room; use phones.
- **Disk full**: banner, clean stop, archive or delete (delete is final).
- **Lost edits**: saving is manual — dirty dot means save before you
  switch or sleep.

## Appendix A — specifications

26HP/3U, ≤110 mm installed, zero rail power (external 5 V USB-C).
800×480 touch DSI. 18-in/20-out USB audio, 48 kHz/24-bit, NVMe capture
(18 tracks + Main simultaneously). 2× TRS MIDI in/out (Type A) + USB
MIDI hub. Pico 2 panel/clock controller + Pi 5 app host, internal SPI.
REC / OVERDUB / STOP / PLAY / PLAY START + TEMPO / NAV / SHIFT / EXIT,
3 semantic LEDs on light pipes, class-compliant USB audio, MIDI clock
master with follow.

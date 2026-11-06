# Interaction model: NAVIGATE / EDIT / SHIFT (panel + touch)

Owner spec, **implemented** (firmware `firmware/pico/`, app `pi/app/`).
The panel and the touchscreen are equals: every control is reachable both
ways, and the two never disagree about state.

## Panel inputs

Joystick = NAV switch (hold-repeat 400/80 ms), click = NAV_PRESS, EXIT =
SHIFT/EXIT short press, SHIFT = hold past 500 ms (`EV_CONTROL` state 2),
encoder = value knob (focus-follows; SHIFT+encoder = fine steps). The
encoder is the context-aware value knob: it edits whatever has focus in
EDIT mode, tempo otherwise.

## Modes and transitions

| From \ To | NAVIGATE | EDIT | SHIFT |
|---|---|---|---|
| NAVIGATE | — (joystick moves focus ring) | NAV_PRESS on a focused value | SHIFT long-hold |
| EDIT | EXIT short press, or NAV_PRESS confirm | — (joystick/encoder adjusts) | SHIFT long-hold (abandons edit) |
| SHIFT | EXIT short press | — (exit to NAVIGATE first) | redundant long-hold = no-op |

EXIT short press in NAVIGATE dismisses an open dialog, else no-op.
Transport (PLAY/PLAY_START/STOP/REC/OVERDUB) is live in every mode
(`docs/protocol.md`). SHIFT+LEFT/RIGHT switches tabs inside SHIFT faces.

## Touch twins (parity rules)

- **SHIFT face entry**: long-press a tab (500 ms) opens that tab's SHIFT
  face; the persistent `SHIFT ▸` chip in the transport strip toggles the
  same state (reads `◂ PRIMARY` when active; refused on tabs without a
  second face). Short tap on a tab = navigate + leave a shift face.
- **Modals trap the panel**: while keypad/confirm/popups are open, NAV is
  inert, PRESS/encoder-push confirms, the encoder edits the buffer; the
  focus list is empty so hidden stops cannot activate.
- **Value feedback**: every change raises the top-band value popup (1.5 s);
  it never covers the finger or the tab row.
- Touching any control syncs panel focus to it.

## Focus model

Every interactive control has a stable `focusId`; the ring is drawn as a
2 px accent outline (EDIT = thicker/amber). The tab row is a single ring
stop (press = tab grid). HUD position is shown in the transport strip.
Modals replace the list while open (see parity rule above).

## SHIFT faces per tab

| Tab | SHIFT face |
|---|---|
| Mixer | gains, sends, monitor legend |
| Track | input/output/monitor, pre/post |
| Routing | filters, transpose, MIDI FX pipes (generator/insert chains) |
| Recorder | takes + punch/loop |
| System | OUTPUT, metronome, MIDI learn, TOOLS |
| Projects | templates + keypad ops |
| Monitor / EQ / XY | none (refused with a message) |

# Panel interaction model: NAVIGATE / EDIT / SHIFT

Owner spec. Firmware side (SHIFT long-hold event) is implemented in
`firmware/pico/`; the app state machine is queued behind the JLCPCB/JACK work.
Joystick = NAV switch, click = NAV_PRESS, EXIT = SHIFT/EXIT short press,
long-press = SHIFT held past 500 ms (`EV_CONTROL` state 2 while held).

## Modes and transitions

| From \ To | NAVIGATE | EDIT | SHIFT |
|---|---|---|---|
| NAVIGATE | — (joystick moves focus ring) | NAV_PRESS click on focused element | SHIFT long-hold |
| EDIT | EXIT short press, or NAV_PRESS confirm | — (joystick adjusts focused value) | SHIFT long-hold (abandons edit) |
| SHIFT | EXIT short press | — (no direct path; exit to NAVIGATE first) | redundant long-hold is a no-op |

EXIT short press in NAVIGATE dismisses an open dialog, else no-op. Transport
buttons (PLAY/PLAY_START/STOP/REC/OVER_DUB) stay live in every mode with the
mapping in `docs/protocol.md`. SHIFT+encoder is fine-tempo in every mode.

## Focus model

- Every screen exposes an ordered focus ring of focusable elements (sliders,
  toggles, arm buttons, tab stops for subtabs). NAV_UP/DOWN moves within the
  ring (wrap), NAV_LEFT/RIGHT moves across columns where the layout has them.
- The focused element draws the theme focus ring (brightness + outline, never
  color alone). Focus persists per screen across tab switches.
- NAV_PRESS on a toggle flips it in place (no EDIT mode needed); on a value
  element it enters EDIT.

## Encoder mapping

- NAVIGATE and SHIFT: tempo (coarse; SHIFT-held: fine, as today).
- EDIT: the focused value (full step; SHIFT-held: fine step). Leaving EDIT
  keeps the value; nothing auto-saves (dirty dot covers it).

## SHIFT alternate screens

Long-press replaces the tab row with four alternate actions (same tab bar,
shift styling); EXIT short press restores it. NAV + PRESS triggers:

| Action | Behaviour |
|---|---|
| Save | Save current state to the current/default session file (session core) |
| Panic | CC123 on all 16 channels of every USB + TRS output; transport and clock keep running |
| Tap | Tempo from 3+ taps (2 s timeout resets); each tap echoes current estimate; EXIT abandons |
| Click | Toggle metronome click enabled (+ dirty flag); no audio change beyond the existing click path |

NAV in SHIFT mode moves across these four; joystick never adjusts in SHIFT
(no EDIT inside SHIFT). A second long-press while in SHIFT is a no-op.

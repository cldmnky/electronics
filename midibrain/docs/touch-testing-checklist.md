# Touch-testing checklist (physical panel — owner's fingers)

The tap gate exercises control **centres** only. Passing every gate does not
establish that adjacent controls are comfortable, that a finger does not
occlude what it drags, or that drags land precisely. This checklist closes
the redesign (items #1–#9 of the UI handoff) on the real 800×480 panel
(~75.8 × 45.36 mm active area). Nothing is "finished" until this pass is done.

## Global (every screen)

- [ ] Fat-finger comfort: adjacent controls (8 px gap by design) never cross-trigger
- [ ] Long track/plugin/take names elide readably everywhere
- [ ] Empty project, no-takes, and NO PICO states all legible and actionable
- [ ] Selected state is visually distinct from panel focus at a glance
- [ ] Modal shield: tapping a popup's top band never falls through to the tab row

## Stage 1 — nav + transport

- [ ] 64 px transport glyphs reachable with thumb **while playing**
- [ ] SONG tag (48×22) tap accuracy
- [ ] Transport options sheet reachable from the right edge
- [ ] More sheet page selection comfortable
- [ ] Configure ↔ PRIMARY toggle feels confident (no accidental double-toggles)
- [ ] RECMODE badge legible at arm's length (it changes what Record does)

## Stage 2 — mixer

- [ ] Riding a fader never opens the channel sheet by accident
- [ ] M / S / ARM thumb reach on all three strips + Master
- [ ] Meter readability: 3-segment taper + peak hold in daylight at ~60 cm
- [ ] OVERDUB/LIVE pill (14 px) readable at arm's length; does not push the
      gain readout off-centre on 3-character track names
- [ ] Master column (112 px): both 32 px meters legible; fader knob easy to
      grab next to the strip's ARM

## Stage 3 — arrange + edit

- [ ] 48 px IN/OUT grab tabs: finger occlusion when In and Out are close
- [ ] Drag precision at 1-bar zoom (encoder path as the fallback)
- [ ] Clip long-press: duplicate vs move/trim disambiguation
- [ ] Panel ring at the mirrored `arrange.grid.*` stops and `arrange.render`
      (press renders, touch opens the sheet)

## Stage 4 — recorder + sheets

- [ ] REC reach while playing
- [ ] Sheet Close accuracy (large, predictable position)
- [ ] Keypad typing feel (touch + panel entry paths)
- [ ] 64 px take rows comfortable; details disclosure reachable

## Stage 5 — routing + system

- [ ] Wire tap accuracy (widened tolerance vs neighbouring wires)
- [ ] Selected route + both endpoints highlight reads clearly
- [ ] System summaries legible at 60 cm; Diagnostics discoverable from System

## Panel parity (joystick / encoder / SHIFT / EXIT)

- [ ] Joystick walks every screen's controls in a sane order
- [ ] Press enters/activates; encoder edits the focused value everywhere
- [ ] SHIFT toggles each screen's second face; EXIT unwinds every overlay
- [ ] Renames and IMPORT TAKE complete fully from the panel (G7)

## Hardware follow-ups (gates cannot certify)

- [ ] Pi-side `--unittests` + `--ui-taps` run (CLAP discovery on the device:
      which plugins its search finds; DELAY/REVERB audible there)
- [ ] POWER button: press = on/graceful shutdown, long-press = PMIC force
      (app hook: flush + SAFE TO POWER OFF screen)

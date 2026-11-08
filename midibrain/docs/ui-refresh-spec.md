# UI refresh spec — declutter, modern, graphic

Owner direction (2026-11-06): "the UI is a bit cluttered and could be more
modern. Use popups more where we do selections on a button, integrate nice
graphics." This spec turns that into executable rules. Qt 6.4-compatible
QML only; 800x480; touch-first; panel focus ring + value popups survive
every change.

## 1. Declutter: summary + popup, everywhere

Rule: **a screen shows state; popups make changes.** Any horizontal row of
>2 mutually-exclusive selection buttons collapses into:

- one summary button (live readout, e.g. `MONITOR  AUTO`, `CH 1-4`,
  `EQ  PARAM`), which opens the existing `ConfigPopup` with the full
  choices; OK/Cancel semantics as shipped (touch-outside never dismisses).

Candidates (audit list — collapse every one found):
- INPUT MON 4-way (becomes `MONITOR <mode>` summary)
- SEND SOURCE pre/post matrix rows
- MAIN COMP block (already popup-able per earlier steer — make primary
  face show only `COMP  RMS -18.0 4:1` summary)
- METRONOME block (summary `CLICK  ON PLAY · PHONES · -9.0`)
- EQ type corners (already popup — remove the SHIFT-face 6-button row)
- Device-out pickers, meter density, backend/device lists (already lists —
  keep, but give the closed state a one-line summary)
- Pipe chMask (16 toggles already live in a popup — summary readout
  `CH 1-4` on the pipe row is the pattern)

Primary faces keep: meters, faders, transport, take list, wires, grids.

## 2. Modern: theme pass

- **Spacing**: 8 px base grid; 16 px screen margins; 8 px control gaps.
- **Type scale**: 11/13/16/22 (labels/body/section/display); tab labels 14;
  one weight story (400 body, 600 emphasis); no ALL-CAPS paragraphs.
- **Surfaces**: three-tone depth (bg #0e1116, panel #161b22, raised #1f2630)
  with 1 px hairlines #2a323d; radius 6 (controls) / 10 (panels/popups).
- **Accent discipline**: cyan = focus/active/primary action; red = record/
  danger only; green = healthy/running only. Nothing else colored.
- **Motion**: 120 ms popup scale-fade from its summary button; 80 ms face
  crossfade on SHIFT toggles; value popups unchanged (already good).
- **Buttons**: primary/secondary/ghost hierarchy (filled cyan / outlined /
  text); pressed state already exists — restyle to 8% lighten.

## 3. Graphics (QML Canvas/SVG, no new deps)

- **Transport glyphs**: play ▶, stop ■, rec ● (drawn, not font-dependent)
  beside/instead of text labels in the touch strip; REC dot pulses while
  recording.
- **Knob rendering**: circular knob + arc value indicator for parameter
  controls (FX, EQ corners, comp) — drag semantics unchanged; faders stay
  faders (mixer/TRACK).
- **Meters**: 3 dB-tapered gradient fills, 10 ms peak-hold dot, clip LED as
  a red crown that latches (already latches — restyle).
- **Waveform thumbs**: antialiased polyline from .peaks (already drawn —
  add mirrored fill + playhead shade in EDIT).
- **Empty states**: one-line muted hints (`no takes yet — press START TAKE`).
- **App icon/wordmark**: small mnkybox mark on the System/Projects header.

## 4. Non-negotiables (regression guards)

- Every interactive control keeps its focusId; ring + value popups work;
  --ui-taps driver must pass on every screen after the refresh.
- 44 px minimum touch targets; ASCII-only user-visible strings (existing
  policy); contrast floors from the old theme notes (7:1 / 4.5:1 / 4.5:1 /
  3:1) verified for the new tones.
- Screenshots 39-*.png per screen (before/after documented in the report).

## 5. Verification

macOS build + tests + --ui-taps clean + offscreen 39-*.png set; one Pi
pass for real-render sanity (offscreen only — never the live desktop).

## 6. The slider law (owner rule, 2026-11-06)

**Tight space => knob card or popup chip, never a slider.** Sliders exist
only for dedicated wide faders (mixer strip fader, master fader, channel
popout gain). Every other numeric value in a tight cell uses `KnobChip`
(arc + label + value) or a popup-edited value chip; enum/bool values use
popup selectors. `ConfigSlider` is deleted and must not return.

## 7. Sizing policy

The active area is **95.04 x 53.856 mm** at 800 x 480 (Waveshare 4.3inch DSI
LCD: 0.1188 x 0.1122 mm per pixel), so **800 px / 95.04 mm = 8.42 px/mm** and one
logical pixel is ~0.119 mm. The px targets are unchanged - it is the same 800 x
480 raster - but every millimetre figure is now larger, because the 4.3-inch
panel replaces the 3.5-inch one (75.8 x 45.36 mm at 0.095 mm/px). The policy is
measured in millimetres, not in "it fits":

| Token | px | Physical | Role |
|---|---|---|---|
| `touchPrimary` | 64 | ~7.6 mm | frequent controls (transport, destination tiles, primary verbs) |
| `touchSecondary` | 56 | ~6.7 mm | secondary controls and list/choice tiles where practical |
| `touch` | 44 | ~5.2 mm | legacy floor only; screens must migrate, not stay here |
| `gap` | 8 | ~0.95 mm | base grid between controls |
| `fontLabel` | 17 | | primary labels, nav destinations |
| `fontSecondary` | 14 | | secondary information, chips, feedback |
| `fontValue` | 24 | | important transport/recording values (22-28 band) |

Rule: **never solve overflow by shrinking an essential font.** Remove,
group, or relocate a control first (into a sheet, a summary button or the
options sheet). The four-destination nav row and the transport strip use these tokens
today; every other screen keeps its existing fixed arithmetic, so
`Style.touch` is deliberately not raised globally.

Destinations keep the existing tab identifiers: Mixer 0, Routing 1,
Recorder 2, System 3, Projects 4, MIDI 5, EQ 6, XY 7, Arrange 8. The nav shows
four persistent destinations (Mixer, Recorder, Arrange, More) and More opens
the six remaining pages through the existing tab-grid mechanism. The panel
ring is updated in the same change: `nav.*` replaces the dead `tab.N` touch
ids, and `transport.tempo` / `transport.shift` join the shell ring.

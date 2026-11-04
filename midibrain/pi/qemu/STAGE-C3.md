# Stage C3: UI fix loop (mixer banking, ASCII glyphs, take-list rescan)

Fixes the three C2 findings in `pi/app` and re-verifies them on the Linux target
at the real panel size. Code changes are limited to `midibrain/pi/app/`; new
screenshots are `qemu/screenshots/08-*.jpg` … `17-system.jpg`. `STAGE-C2.md` is
untouched. Nothing committed or pushed.

App instance used for every shot below: `mnkybox --emulator`, guest build
`~/pi-build-ccache` (`RelWithDebInfo`, ccache), Xvfb `:99` restarted at
**800x480x24** so root coordinates == window coordinates (window lands at 0,0;
JUCE centres an 800x480 window in an 800x480 screen). Clicks were sent with
`xdotool mousemove X Y click 1` on that full-screen geometry, derived from each
screen's `resized()`.

## 1. Mixer overflow → two banks of nine (`screens/MixerScreen.*`)

The old screen kept a `juce::Viewport` and scrolled 18 × 80 px strips behind a
pinned 112 px master. At 800 px that showed 8.6 strips: IN 09 half-visible and
its MUTE/SOLO/ARM labels cut at the viewport edge, exactly the C2 finding.

Chosen fix: banking (option A of the two offered) — 9 strips + master per bank.

| Where | Change |
| --- | --- |
| `screens/MixerScreen.hpp:18` | `inline constexpr int kStripsPerBank = 9;` (18 inputs = 2 banks) |
| `screens/MixerScreen.hpp:74-81` | `setBank()` / `layoutStrips()` / `syncBankButtons()`, `juce::TextButton bankButtons_[2]`, `int bank_`; `juce::Viewport viewport_` removed |
| `screens/MixerScreen.cpp:14-17` | `kBankCount`, `static_assert(kNumStrips == kBankCount * kStripsPerBank)` |
| `screens/MixerScreen.cpp:203-215` | ctor: strips go straight into `stripHolder_`; two bank buttons "1-9" / "10-18" |
| `screens/MixerScreen.cpp:218-232` | `resized()`: right column = 44 px bank row on top of the always-visible master; the two buttons are 55 x 44 (full `style::kTouch` height, 1 px gap) |
| `screens/MixerScreen.cpp:234-254` | `layoutStrips()`: column edges are `width*column/9`, so the nine strips plus the 112 px master fill 800 px with no gap and no clipping; strips of the other bank are `setVisible(false)` |
| `screens/MixerScreen.cpp:256-269` | `setBank()` + `syncBankButtons()` (active bank tinted `style::accent`) |
| `screens/MixerScreen.cpp:276-282` | `setSelectedStrip()` follows the selection across the bank edge (panel NAV left/right no longer selects an off-screen strip) |
| `ui_style.hpp:9-10` | unused `kFaderWidth` removed (widths are derived from the panel now) |
| `screens/MixerScreen.cpp:13, 28, 40` | `kStripValueWidth = 58`: gain/pan read-outs are sized for the 76 px strip, not the old 80 px one |

Bonus label fix found while checking "zero clipped labels": the send letters
A/B sit in a `Label`; JUCE's fitted-text layout squashes or ellipsizes a single
letter in a narrow box (rendered as a ~6x3 px blob before, as "..." once squashing
was disabled). Measured in the guest: 16 px column → ellipsis, 20 px → clean
letter. `screens/MixerScreen.cpp:64-69, 116-125` now uses a 20 px letter column,
font 12, `setMinimumHorizontalScale(1.0f)` so a future shrink fails visibly
instead of turning into a blob.

Proof: `screenshots/08-mixer-bank1.jpg` (IN 01-09 + MASTER, "1-9" active) and
`screenshots/09-mixer-bank2.jpg` (IN 10-18 + MASTER, "10-18" active). No strip
is cut, no label clipped, master meters live in both banks.

## 2. Non-ASCII UI strings → ASCII (`grep`-driven sweep)

`perl -ne 'print if /[^\x00-\x7f]/'` over `pi/app/**/*.{cpp,hpp}` now returns
nothing. Replaced (middle dot -> `-`, em dash -> `--`, ellipsis -> `...`,
multiplication sign -> `x`):

| File:line | Before → after |
| --- | --- |
| `screens/RoutingScreen.cpp:129` | `ROUTING  ·  source × destination…` → `ROUTING  -  source x destination, channel filter, transpose` |
| `screens/RecorderScreen.cpp:33,34` | take row `3 files  ·  00:34  ·  25.0 MiB` → `3 files  -  00:34  -  25.0 MiB` |
| `screens/RecorderScreen.cpp:70` | directory button `…` → `...` (the C2 `â(t` glyph) |
| `screens/RecorderScreen.cpp:157,159` | `120.00 BPM  ·  tick N` / `  ·  link down` → `  -  tick N` / `  -  link down` |
| `screens/RecorderScreen.cpp:168,174,175,185` | status/disk lines: `recording take  ·  …`, `last take … · 3 files ·`, `· drops N` → `  -  ` |
| `screens/FxScreen.cpp:26,107,113` | `pick plugin…` → `pick plugin...`; `no plugin — pick a .clap` → `no plugin -- pick a .clap`; `  ·  ` → `  -  ` |
| `main.cpp:1` | header comment `mnkybox — JUCE…` → `mnkybox - JUCE…` |

`pi/app/NOTES.md` still contains prose em dashes; it is developer documentation,
never rendered, so it was left alone.

Proof: `screenshots/10-routing.jpg` (header renders as `ROUTING - source x
destination, …`), `screenshots/11-fx.jpg` (`no plugin -- pick a .clap`, `send
idle; return passes the bus through`), `screenshots/12-recorder.jpg` and
`17-system.jpg` (`120.00 BPM - tick N`, `disk 11.4 GiB free of 19.5 GiB - drops 0`,
`...` button instead of `â(t`).

## 3. Stale take list after stop (`screens/RecorderScreen.cpp:231-239`)

Root cause was **not** a missing rescan: `MainComponent::stopTake()` already
calls `recorderScreen_->takeStateChanged()` → `scanTakes()`. The model was being
refilled correctly but never repainted. `juce::ListBox::updateContent()` only
re-lays-out rows and repaints rows whose bounds changed (JUCE 9.0.5,
`juce_ListBox.cpp:647`); the row count stays the same for a live take, so the row
kept the paint from the scan made at **start** time, when the three WAVs exist
but their 48-byte headers are still stdio-buffered (size 0 on disk) — hence
`3 files - 00:00 - 0 B` while the status line, fed from the in-memory
`audio::Recorder::Take`, showed `3 files - 00:34`.

Fix: `takeList_.repaint();` after `updateContent()`, with a comment explaining
why (the periodic ~1.3 s rescan in `refresh()` now also lands on screen).

Proof, one UI-driven take in the current build (`take-20261104T034401Z`):

| Shot | Row for the new take | Status line |
| --- | --- | --- |
| `14-recording.jpg` (2.5 s in) | `3 files - 00:00 - 1.3 MiB` | `recording take - /home/mnky/Music/mnkybox-takes` |
| `15-take-stopped.jpg` (~0.6 s after STOP) | `3 files - 00:06 - 4.2 MiB` | `last take take-20261104T034401Z - 3 files - 00:06` |
| `16-take-done.jpg` (2 s later, stable) | `3 files - 00:06 - 4.2 MiB` | same |

Disk check of that take (header-parsed in the guest):

```
ch00.wav: fmt=3 ch=1 rate=48000 frames=275072 dur=5.73s bytes=1100288 file=1100344 riffOK=True
ch01.wav: fmt=3 ch=1 rate=48000 frames=275072 dur=5.73s bytes=1100288 file=1100344 riffOK=True
master.wav: fmt=3 ch=2 rate=48000 frames=275072 dur=5.73s bytes=2200576 file=2200632 riffOK=True
```

`fmt=3` = IEEE float32, 48 kHz, mono channels + stereo master, RIFF/data sizes
patched on close, `drops 0` in the UI and in `/tmp/mnkybox-app.log`
(`recorder: take … 3 files, 275072 frames, 0 dropped blocks`).

## 4. Re-verification

| Check | Result |
| --- | --- |
| Host build (`cmake --build /tmp/mnky-app-build --target mnkybox`, macOS clang, `-Wall -Wextra -Werror` on app sources) | exit 0, zero warnings (`/tmp/ui-fix-host-final.log` has no `warning:`/`error:` line) |
| Host `mnkybox --unittests` | `PASS mnkybox app logic: 76 checks, 0 failures` |
| Guest rebuild (`~/pi-build-ccache`, ccache warm) | `[100%] Built target mnkybox`, incremental 5-6 s, no warnings |
| Guest screenshots | 08-17, all 800x480, one app instance |
| Mixer | both banks fit, master pinned, labels legible (`08`, `09`) |
| Routing / FX / Recorder / System | no mojibake (`10`, `11`, `12`, `17`) |
| Take list after stop | updates within one repaint of STOP (`14` → `15`) |

Files changed (7, all under `midibrain/pi/app/`):
`main.cpp`, `ui_style.hpp`, `screens/MixerScreen.hpp`, `screens/MixerScreen.cpp`,
`screens/RecorderScreen.cpp`, `screens/RoutingScreen.cpp`, `screens/FxScreen.cpp`.

Screenshots added: `08-mixer-bank1`, `09-mixer-bank2`, `10-routing`, `11-fx`,
`12-recorder`, `13-recorder-armed`, `14-recording`, `15-take-stopped`,
`16-take-done`, `17-system` (all `.jpg`, 800x480).

Method note for the next stage: the guest Xvfb is now `800x480x24`; window-relative
coordinates that worked here (root == window):
Mixer bank buttons `(715,70)` / `(772,70)`; tabs `y=26`, centers Routing `148`,
FX `245`, Recorder `343`, System `445`; Recorder arm cells `y=120`, x `49 + 87*n`;
START/STOP `(75,208)`.

## 5. Remaining nits / open items

- **Bank order is manual.** The panel encoder can move the selection across the
  bank edge (which switches the bank), but there is no "show the selected strip"
  button: after the panel arms strip 12 the mixer stays on the last chosen bank
  until NAV moves the selection. Fine for now, worth a revisit with the Pico.
- **Bank switching itself was only exercised by UI clicks** (XTEST), not by a
  real touch panel; tap-vs-click and multi-touch remain untested, as in C2.
- **`13-recorder-armed.jpg`** shows arm 1+2 as the only armed cells; the strip
  selection highlight on the Mixer screen is still on IN 01 for both banks
  (selection is per-strip, not per-bank) — cosmetic, unchanged behaviour.
- **Recorder transport row** still shows `STOPPED 016:4:05` (bar:beat:tick from
  the Pico link) even when nothing has rolled since app start; that is the
  emulator transport's idle tick, not a UI bug.
- **`STAGE-C2.md`** still describes the 1024x600 method and the pre-fix verdicts;
  it is parent-owned and was not edited. Its Method section should be read
  together with §4 here.
- No panel/audio/MIDI timing claim is made here; this stage only proves layout,
  glyph rendering and the take-list refresh on the Linux target.

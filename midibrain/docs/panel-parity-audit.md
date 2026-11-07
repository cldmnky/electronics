# mnkybox panel-parity AUDIT

Read-only audit. No repository files were modified (`git status --porcelain` empty). All build/probe artifacts live in `/tmp`.

## 0. Verdict (severity-ordered)

The touch path is green (`--ui-taps`: **864 taps, 0 QML warnings, 0 failures**). The panel path is not:

| # | Severity | Defect | Evidence |
|---|---|---|---|
| G1 | **Blocker** | Mixer ring face is chosen by the *selected track's mode* (`selectedMode()`), not the MIXER/TRACK view; with the default ARM mode the MIXER surface's ring is the 16-stop TRACK ring, so the strips/faders cannot be focused. The ring is also never rebuilt on view/track changes. | `app_controller.cpp:3639`, `:3824`; probe8/probe10 |
| G2 | **Blocker** | PROJECTS SHIFT face is entered but its ring is **empty** (`case 5:` used for a tab that is 4; tab 5 is MIDI and has no face). NEW/RENAME/DELETE/template verbs are unreachable. | `app_controller.cpp:3795`; probe4 `tab 4 … faceRing=0` |
| G3 | **Blocker** | Bank-page grid is a **hard lockout**: `bank.page.N` / `bank.page.close` have no PRESS handler and EXIT does not close it. Panel is trapped until touch. | `app_controller.cpp:3406-3413`; probe3/probe9 |
| G4 | **Blocker** | TRACK FX popup CLOSE (ring stop 0) **segfaults** on PRESS: `parts[2]` read with `parts.size()==2`. EXIT also does not close the popup → hard lockout. | `app_controller.cpp:1911`; probe3 repro (2×) |
| G5 | High | EXIT does not close the **channel popout**, **TRACK FX popup** or **bank grid**. | `updateShiftState` `:1698-1721`; probe3 matrix |
| G6 | High | `isEditableFocus` uses substring tests: `"pan"` matches `mixer.ex**pan**d.N`, so PRESS on the channel-expand stop enters EDIT instead of opening the popout → the **whole ChannelPopout (incl. REMOVE TRACK) is panel-unreachable**. | `app_controller.cpp:1745`; probe10 stop 6 `press:edit` |
| G7 | High | Keypad is a dead modal for the panel: no character can be entered (encoder = cursor only), and its keys are not ring stops. Renames, ADD TRACK naming and IMPORT TAKE paths cannot be completed from the panel. | `Keypad.qml:78-100`, `perform()` `:1291` |
| G8 | High | Ring **dead stops** (focus id with no visible widget): `track.mode`, `mixer.select.N`, `channel.monitor`, `edit.value`, `shift.eq.band.*` (shadow ids), plus `midimon.port.N` (no declared `focusId` property). | probe1/probe2; grep |
| G9 | High | Focus is **not restored** after any popup: config/tempo/tab grid/take sheet/bank grid/length popup all return to index 0 (or 1). Only tab switches restore. | probe4 |
| G10 | High | ARRANGE clip block is a **nav trap**: from a clip all four directions are spatial (`neighborClip`), so `arrange.clipmenu` and `arrange.render` can never be reached by walking. | `:1515-1543` |
| G11 | Medium | Choice cards classified as "editable" do not pick on PRESS (`config.mode.N`, `out.N`, `phones.N`, `cuestyle.N`, `out34.N`, `pipeparam.N`) — PRESS enters EDIT and only the encoder cycles. | `isEditableFocus` `:1726-1748`; probe6 |
| G12 | Medium | Panel SHIFT/`transport.tempo`/`transport.shift` focus ids never join the ring; `shift.monitor.clocksource.N` (a real 3-way persisted control) is unreachable; `shift.recorder.mode`, `recorder.takesheet`, `take.sheet.edit.N`, `xy.bank/track/pick`, `channel.mode.N`, `channel.recsource`, `system.devout.saved`, `config.renderregion` unreachable. | probe1 vs QML grep |
| G13 | Medium | Encoder-press (`EncoderSw`) means **toggle ARM** on primary screens but "activate" on faces/popups — not "confirm". | `performPanelFocus` default branch `:1608-1612` |
| G14 | Medium | Focus can move **off-viewport** in the recorder take list and projects list; neither list scrolls to the focused row. | `RecorderScreen.qml:194`, `ProjectsScreen.qml:99` |
| G15 | Low | Dead code that lies about parity: `ConfigPopup` `takeedit` kind/ring branch (kind not openable), `config.renderregion` focus id not in any ring, `ToolsScreen.qml` not referenced anywhere (`tools.page.N`), `Main.qml` `tab.N`, `transport.nopico`. | grep/CMake |

Everything below is the detail behind this list.

---

## 1. Method & evidence

* Read all 42 QML files (`focusId`, tap handlers, visibility gates) and the C++ ring builder (`rebuildFocusIds`, `performPanelFocus`, `activateFocus`, `adjustFocused`, `isEditableFocus`, `updateShiftState`, `setTab`, `showShiftFace`).
* Built the app from this worktree in `/tmp/panel-audit-build` (`cmake … -DFETCHCONTENT_SOURCE_DIR_CLAP=/tmp/pi-build/_deps/clap-src`, Apple clang, Qt 6.12) — **build exit 0**.
* Ran `--ui-taps` on the same binary → **864 taps, 0 warnings, 0 failures, exit 0** (baseline: touch parity is green).
* Wrote 10 read-only probe harnesses in `/tmp` (`panel_probe.cpp` … `panel_probe10.cpp`) that link the built app objects (all but `main.cpp.o`) and drive `AppController` through the real intent path (`perform(Intent)`; `#define private public` in the probe only, no repo edit). They dump live rings and per-stop PRESS/encoder behaviour. Outputs: `/tmp/panel_probe_out.txt` … `/tmp/panel_probe10` (stdout).
* `noPico=false` (a real panel) unless stated; 4 seeded tracks so mixer strips exist; fresh temp `HOME` per run.

Caveat: a fresh session has **no takes/clips/projects**, so the take-edit popup, clip sheet and clip stops were verified by code reading, not by probe; everything else in the matrix is empirical.

---

## 2. How the panel works today

* **Intents** (`logic.hpp:29`): `NavUp/Down/Left/Right`, `NavPress`, `EncoderSw` (press), `EncoderTurn(amount)`, `Shift`, transport kinds. `AppController::perform` routes NAV/PRESS to `performPanelFocus`; `EncoderTurn` goes to keypad-cursor / tempo / `adjustFocused`.
* **Ring** (`rebuildFocusIds`, `:3397`): a flat `std::vector<QString>` of focus ids, one per context. **One writer, context priority:**
  `keypad|confirm` → `tab grid` → `bank grid` → `edit popup` → `arrange clip sheet` → `arrange LEN` → `take sheet` → `tempo` → `config popup` → `TRACK FX` → `channel popout` → `SHIFT face` → `primary screen`.
* **Movement** (`moveFocus`, `:1654`): UP/LEFT = −1, DOWN/RIGHT = +1 (a 1-D ring), except tab grid ±5, ARRANGE clips (spatial), EQ/XY grids in EDIT (arrows are the control).
* **EDIT**: on primary/shift screens, PRESS on an id in `isEditableFocus` switches `panelMode="edit"`; the encoder then calls `adjustFocused`. In the config/clip/face modal branches the same rule applies; in the config popup the encoder also works without entering EDIT (`perform()` `:1305`).
* **SHIFT**: `updateShiftState` `:1681` — a held SHIFT frame ≥500 ms after the press enters the face (`setPanelMode("shift")`); releasing SHIFT leaves the face. Tabs **5 (MIDI) and 7 (XY)** have no face (`tabHasShiftFace`, `:496`) and refuse with a notice. In the face, LEFT/RIGHT switch tabs and UP/DOWN walk the face ring.
* **EXIT**: short release = unwind overlays / leave EDIT (`:1697`); long release = leave the SHIFT face.
* **Focus memory**: only `focusByScreen_[tab]` (per-tab index) is preserved. `openConfig` resets `focusIndex_=0` (`:1185`); no overlay saves/restores.
* **Modal traps**: keypad/confirm make NAV/PRESS inert (`performPanelFocus` early return `:1357`). Note the *ring is not actually emptied* there unless some other event rebuilds (measured `focusCount=16` with the keypad open) — behaviour is safe, the comment is wrong.

---

## 3. Reachability matrix

Legend: **B** = built-in to every primary ring; **✎** = encoder edits; **✗** = encoder does nothing; **⌀** = ring id with no visible QML widget; **▣** = QML control with a `focusId` that is not in the ring.

### 3.1 Shell (every primary screen, ring positions 0..n)

| Ring | id | Widget | PRESS | Encoder | Notes |
|---|---|---|---|---|---|
| 0 | `tab.menu` | tab row border (Main.qml:64) | opens 2×5 tab grid | — | the only tab stop; `tab.N` (touch target) is ▣ |
| 1..3 | `transport.click`, `transport.recmode`, `transport.song` | TouchButton / Text | toggle / RECMODE popup / SONG | — | present on all 9 tabs |
| — | `transport.play/stop/rec` | only in `--no-pico` | play/stop/rec | — | on a panel these are B only when `noPico_` (`:3813`) |
| — | `transport.tempo` | TouchButton (TransportStrip.qml:72) | never | — | ▣; tempo popup is unreachable on a panel (encoder still changes tempo) |
| — | `transport.shift` | TouchButton (:201) | never | — | ▣ (long-hold is the panel gesture) |
| — | `transport.nopico` | chip (:85) | never | — | ▣ (no-pico only) |
| — | value/notice slot | — | — | — | not interactive |

`Pane` SHIFT faces deliberately omit the tab/transport stops (LEFT/RIGHT switch tabs while the face is held).

### 3.2 Mixer (tab 0) — **two faces × two views, and the ring does not follow the view**

Measured rings with 4 tracks (probe8, probe10):

**MIXER view, selected track in PLAY** → MIXER ring, 47 stops:
`tab.menu, transport.click, recmode, song, mixer.mode.0, mixer.mode.1,` then per strip `mixer.expand.N, mixer.trackmode.N, mixer.select.N, mixer.in.N, mixer.gain.N, mixer.pan.N, mixer.mute.N, mixer.solo.N, mixer.arm.N`, then `mixer.add, mixer.master, mixer.bank.0, mixer.bank.pages, mixer.bank.1`.

**MIXER view, selected track ARM/OVERDUB/LIVE (default = ARM)** → TRACK ring, 16 stops:
`tab.menu, transport.click, recmode, song, mixer.mode.0, mixer.mode.1, track.select.<slot>×4, track.gain, track.pan, track.mode, track.arm, track.mute, track.solo`.

| Control | Ring | Widget | PRESS | Encoder |
|---|---|---|---|---|
| strip body / name tap (select) | `mixer.select.N` ⌀ | **no widget** (only a `focused` property, MixerStrip.qml:32) | selects strip | — |
| expand chevron | `mixer.expand.N` | ✓ (MixerStrip.qml:76) | **EDIT (bug G6)**, popout never opens | ✗ |
| track-mode pill | `mixer.trackmode.N` | ✓ (:166) | cycles mode + popup | ✗ |
| input source | `mixer.in.N` | ✓ (:198) | EDIT | ✎ cycles source |
| fader / pan | `mixer.gain.N` / `mixer.pan.N` | ✓ (:100/:149) | EDIT | ✎ |
| M / S / ARM | `mixer.mute.N` / `mixer.solo.N` / `mixer.arm.N` | ✓ (:216/:226/:236) | toggle | ✗ |
| ADD TRACK | `mixer.add` | ✓ (:85) | opens ADD TRACK config | ✗ |
| master fader | `mixer.master` | ✓ (:185) | EDIT | ✎ |
| page < / TRACKS / > | `mixer.bank.0 / .pages / .1` | ✓ (:55/:67/:75) | step / **opens bank grid (lockout G3)** / step | ✗ (pages: ✎ steps) |
| view toggle MIXER/TRACK | `mixer.mode.0/1` | ✓ (:33/:42) | switches view — **ring not rebuilt** (G1) | ✗ |
| TRACK view selector | `track.select.N` | ✓ only in TRACK view (:289) | selects | ✗ |
| TRACK expand chevron | — | ✓ (:304 `track.expand`) | **▣ never in ring** | — |
| TRACK vol / pan | `track.gain` / `track.pan` | ✓ (:330/:341) | EDIT | ✎ |
| TRACK mode | `track.mode` ⌀ | **no widget** (mode word is `recorder.mode`-style only on Recorder) | opens track-mode popup | ✗ |
| TRACK mute/solo/arm | `track.mute/solo/arm` | ✓ (:368/:379/:357) | toggle | ✗ |

**MIXER SHIFT, PLAY** (24): per strip `shift.mixer.gain.N, .fx1.N, .fx2.N, .cue.N, .out34.N, .monitor.N` — all have widgets (MixerShiftStrip) and ✎ except **`shift.mixer.monitor.N` press=no-op, encoder=no-op** (dead; touch cycles the monitor mode).
**TRACK SHIFT, ARM** (11): `shift.track.input, .input.pair, .output, .gain, .fx1, .fx2, .cue, .out34, shift.monitor.sends, shift.track.mode, shift.track.fx` — all widgets, all ✎ where value, press actions work.

SHIFT reachable? Mixer: yes (long-hold). Both faces exist.

### 3.3 Routing (tab 1)

**Primary** (13 with this device set): `tab.menu, transport.*3, routing.reset, routing.shiftpipes, routing.node.in.<port>×4, routing.node.out.<port>×3`.

| Control | Ring | Widget | PRESS | Encoder |
|---|---|---|---|---|
| RESET | `routing.reset` | HoldResetButton (RoutingScreen.qml:181) | confirm dialog | — |
| SHIFT PIPES | `routing.shiftpipes` | ✓ (:167) | enters face + pipes page | — |
| input nodes | `routing.node.in.<port>` | ✓ (:282) | select input | ✗ |
| output nodes | `routing.node.out.<port>` | ✓ (:351) | select/toggle wire | ✗ |
| patchbay wire tap | — | TapHandler (:405) | — | ▣ (reachable as input→output presses) |

**SHIFT / FILTER page** (60): `shift.routing.reset, page.0, page.1,` then per filter source (3): `shift.routing.mask.<src>.0..15, shift.routing.transpose.<src>`, then `shift.clock.clock.<out>, shift.clock.transport.<out>` for TRS1/TRS2/USB. All widgets present; masks press-toggle; transpose ✎; clock gates press-toggle. `shift.routing.filter.prev/next` are **▣** (ring omits them — page count>1 unreachable from the panel).

**SHIFT / PIPES page** (11 + selected row): `reset, page.0, page.1, src.name, dst.name, [insert.route, insert.add, insert.chain.N…], [insert.on/up/down/remove.<slot>], add.type, add, scope, chain, select.N×pipes, [bypass/up/down/remove.<selected>], param.<selected>.<k>…`. All widgets present; params ✎; lists auto-scroll to the focused row (`RoutingScreen.qml:873/1049`) — the best-behaved face in the app.

SHIFT reachable? yes. EXIT: yes.

### 3.4 Recorder (tab 2)

**Primary** (24 + 2/take): `tab.menu, transport.*3, recorder.track.0..17, recorder.mode, recorder.start, [recorder.take.N, recorder.take.menu.N]`.

| Control | Ring | Widget | PRESS | Encoder |
|---|---|---|---|---|
| track selector 01..18 | `recorder.track.N` | ✓ (RecorderScreen.qml:85) | selects | — |
| mode word | `recorder.mode` | ✓ (:131) | cycle mode (long-press list is touch) | — |
| START TAKE | `recorder.start` | ✓ (:141) | start/stop | — |
| ACTIVE readout → take sheet | — | ✓ (:52 `recorder.takesheet`) | **▣ unreachable** | — |
| take row body | `recorder.take.N` | ✓ (:206) | makes take ACTIVE | — |
| row mini-wave | — | ✓ (:250 `recorder.take.wave.N`) | ▣ opens EDIT popup | — |
| row EDIT | — | ✓ (:286 `recorder.take.edit.N`) | ▣ opens EDIT popup | — |
| row "..." | `recorder.take.menu.N` | ✓ (:327) | TAKE actions (ACTIVE/EDIT/RENAME/DELETE/REMOVE/IMPORT/NEW TRACK) | — |

**SHIFT** (14): `import, live, activate, rename, delete, loop.enable, loop.start, loop.end, punch.{playfrom,in,out}.{enable,measure}` — all widgets; loop start/end and punch measure ✎; `.dec/.inc` buttons **▣** (duplicates of the knob); `shift.recorder.mode` **▣ unreachable**. Punch rows have extra `.measure.dec/.inc` ▣.

Rows 19..N are ring stops even though only ~4 fit on screen and the `ListView` does not scroll to the focus (G14). EXIT leaves the take sheet ✓.

### 3.5 System (tab 3)

**Primary** (11): `system.backend, input, buffer, devout.main, devout.out34, devout.phones, refresh` — all widgets, first six ✎. `system.devout.saved` **▣** (info button).
**SHIFT** (8): `shift.system.panic, page.0/1/2`, + page 0 `shift.monitor.summary.{metronome,sends,monitor,comp}`; page 1 `shift.learn.{target,arm,clear,binding.N}`; page 2 `shift.tools.brightness`.
**Gap:** page 0’s `shift.monitor.clocksource.0..2` (MonitorScreen.qml:53, persisted `clock.source`) is **▣ unreachable** — a real output setting the panel can never change. `shift.learn.binding.N` and `shift.learn.clear` delete on a single press with no confirmation (risk).

### 3.6 Projects (tab 4)

**Primary** (5 + rows): `tab.menu, transport.*3, projects.save, projects.row.N`.
**SHIFT: 0 stops.** `case 5:` (`:3795`) is the wrong tab; `kProjectsTabIndex == 4`. The QML face is fully built (ProjectsScreen.qml:196-340) and `tabHasShiftFace(4)` is true, so the panel enters an empty face.
Additional flow gap: there is no panel stop for the "..." row-select (`ProjectsScreen.qml:146-168`), and the SHIFT mirror list rows (":305") have a bare `TapHandler` with no ring id. `projects.row.N` PRESS loads the project, so RENAME/DELETE can never have a `selectedRow` set by the panel.

### 3.7 MIDI MONITOR (tab 5)

**Primary** (12): `midimon.clock.filter, midimon.reset, midimon.port.0..5` — no SHIFT face (refused). The port row itself has **no `focusId` property** (`MidiMonitorScreen.qml:70-78` compares `app.focusId` inline), so the ring stop draws no focus and `--ui-taps` never taps it; PRESS does show the port summary.

### 3.8 EQ (tab 6)

**Primary** (16): `eq.subject.main, eq.subject.track, eq.band.select, eq.node.0..3, eq.band.gain, eq.band.q, eq.band.freq, eq.band.type, eq.fullscreen`.
Press: MAIN selects, TRACK readout ✎ steps subject (the ± buttons are ▣, duplicated by the encoder), band chip steps band, node selects band + EDIT, gain/q/freq EDIT (✎), type opens the EQ-type popup, fullscreen toggles. EDIT + arrows nudge the node (`performGridEdit`).
**SHIFT** (6): `shift.eq.subject.main, shift.eq.subject.track, shift.eq.band.gain/q/freq, shift.eq.band.type`. The three band rows are **shadow ids** (`shadowFocusId`, EqScreen.qml:501-533) — visible focus, but no `focusId` property, so the gate cannot see them; `edit.value` has the same pattern.

### 3.9 XY (tab 7)

**Primary** (9): `xy.delay, xy.reverb, xy.freeze, xy.delay.params, xy.reverb.params`. No SHIFT face (refused); the two grids are the whole screen.
**Missing:** `xy.bank.0/1`, `xy.track.N`, `xy.delay.pick`, `xy.reverb.pick` (XyScreen.qml:81/89/109/298) are all **▣**. Consequence: on an unbound slot, `xy.*.params` only shows a note (`openFxSlotParams`), so **assigning an effect on the XY screen is panel-impossible** (indirect route: Mixer → TRACK FX → slot select).

### 3.10 ARRANGE (tab 8)

**Primary** (17 + sections + clips): `arrange.section.N…`, `arrange.addsection, dupsection, loopsection, rename, length, loop, grid.0..3, zoomout, zoomin, arrange.clip.N…, arrange.clipmenu (only with a selection), arrange.render`.
All have widgets; sections press-select; clips are **unnavigable past** (spatial nav trap, G10); `arrange.clipmenu`/`render` unreachable once a clip is focused. Ring not rebuilt on `selectClip` (no connection) → `clipmenu` appears only after some other rebuild.
**SHIFT** (13): `shift.arrange.{addsection,dupsection,loopsection,rename,length,loop,grid.0..3,zoomout,zoomin,render}` — all widgets.
**LEN popup** (5): `arrange.length.decbar/incbar/decbeat/incbeat/done` — all widgets, buttons only.
Missing: lane header (`selectStrip`, ArrangeScreen.qml:315) and empty-lane seek (:366) are **▣**.

### 3.11 Overlays

| Overlay | Ring | Encoder | EXIT closes? | Gaps |
|---|---|---|---|---|
| Tab grid | `tabgrid.0…8` (walk +5) | — | ✓ | none; press jumps |
| Bank grid | `bank.page.N…`, `bank.page.close` | — | **NO** | **no PRESS handler at all → lockout (G3)** |
| Tempo popup | `tempo.done` only | ✎ tempo (any focus) | ✓ | ±1/TAP/CLOSE are ▣ |
| Config popup | per kind (see below) | ✎ values | ✓ | choice-card press, order, focus restore |
| Channel popout | 17 stops: `channel.close, rename, gain, trim, pan, fx1, fx2, cue, out34, mute, solo, arm, monitor⌀, input, output, fx, remove` | ✎ gain/trim/pan/fx1/fx2/cue/out34 | **NO** | `channel.monitor` has no widget; `channel.mode.0..3`, `channel.recsource` ▣; rename opens the keypad |
| TRACK FX | `trackfx.close, fx1.select, fx1.wet, [fx1.bypass], fx1.param.N…, fx2.select, fx2.wet, [fx2.bypass], fx2.param.N…, trackfx.recsource` | ✎ wet/param | **NO** | **close PRESS crash (G4)**; `.select` press only enters EDIT (selector list is touch-only, QML comment admits it) |
| Take sheet | `take.sheet.live, take.sheet.row.N…, take.sheet.close` | — | ✓ | `take.sheet.edit.N` ▣ (`:762`) |
| EDIT popup | 24: `edit.close, zoom.out, zoom.in, edit.in, edit.out, edit.op.0..7, edit.value, edit.apply, edit.undo, edit.render` | ✎ in/out/value/op/zoom | ✓ | `edit.value` ⌀ (value lives on the op card) |
| ARRANGE clip sheet | `arrange.clip.action.0..N-1, [gain, fadein, fadeout, pane.back], arrange.clip.close` | ✎ gain/fades | ✓ | none (pane only when open) |
| LEN popup | 5 stops | — | ✓ | none |
| Keypad | **none** (trap by design) | cursor only | ✓ cancel | **cannot type (G7)**; 60+ `keypad.*` ids never reachable |
| Confirm | **none** (trap) | — | ✓ cancel | PRESS = accept (two-button dialog collapsed to press/exit) |
| Value popup | — | — | — | non-interactive (correct) |

**Config kinds measured** (ring → QML all present unless noted):
`metronome` 10 (mode.0/out.0/mode.1/out.1/mode.2/out.2 — interleaved vs the visual two rows; gain, bars, ok, cancel) · `sends` 12 (per row pre/post ×5 + ok/cancel) · `trackmode` 9 · `recmode` 6 · `takeactions` 9 · `monitor` 8 · `eqtype` 8 · `delay` 14 · `reverb` 9 · `timelineops` 12 (+`config.renderregion` ▣) · `inputsource` 30 · `outputdest` 6 · `fxslot` 6 · `pipesource` N · `pipedest` N · `pipetype` 18 · `pipeparam` 4 (needs a selected pipe) · `chainmenu` 4 (ids are `shift.routing.pipes.chain.*` — naming only) · `delaydivision` 10 · `reverbmodel` 6 · `addtrack` 28 · `pipechannels` 23 · `comp` 10 · `takeedit` — **kind not openable** (dead ring branch + dead QML column).

PRESS on a config card: **picks** for `send.*`, `trackmode.N`, `recsource.N`, `recmode.N`, `eqtype.N`, `delaydivision.N`, `reverbmodel.N`, `input.N`, `output.N`, `fxslot.N`, `addtrack.*`, `comp.on`; **enters EDIT instead of picking** for `config.mode.N`, `out.N`, `phones.N`, `cuestyle.N`, `out34.N`, `pipeparam.N` (G11).

---

## 4. Gap list (grouped by kind)

**A. Missing focus ids (QML control never enters the ring)** — `recorder.takesheet`, `recorder.take.wave.N`, `recorder.take.edit.N`, `take.sheet.edit.N`, `shift.recorder.mode`, `shift.monitor.clocksource.0..2`, `shift.routing.filter.prev/next` (when >1 page), `channel.mode.0..3`, `channel.recsource`, `xy.bank.0/1`, `xy.track.N`, `xy.delay.pick`, `xy.reverb.pick`, `track.expand`, `system.devout.saved`, `config.renderregion`, `mixer.add/bank/master` only when the ring face is right, `punch.*.measure.dec/inc`, `loop.*.dec/inc`, `tools.page.N` (dead screen), `keypad.*` (by design, but see G7).

**B. Ring id with no widget (dead stop)** — `track.mode`, `mixer.select.N`, `channel.monitor`, `edit.value`, `shift.eq.band.gain/q/freq` (shadow property only), `midimon.port.N` (inline comparison only). Plus `config.pipeparam.N` when opened without a pipe (ring not built at all then — but that context can’t be reached from a card either).

**C. Wrong/gated ring face** — Mixer view vs `selectedMode()` (G1), ring staleness on `trackMode`/`trackModeChanged`/`trackChanged`/`tracksChanged`/`selectClip`, Projects `case 5:` (G2).

**D. Encoder cannot edit a focused value** — `shift.mixer.monitor.N` (dead), `channel.mode.N`/`channel.recsource` (not in ring), `config.mode/out/phones/cuestyle/out34/pipeparam` choice cards (encoder works, PRESS doesn’t pick), `mixer.trackmode` (press cycles; encoder no-op by design), `track.mode` (dead id), `eq.band.type` (opens list; encoder doesn’t cycle — the `cycleType` is only on the ± buttons).

**E. Screens without SHIFT parity** — MIDI (5) and XY (7) have no face (honest notice, but the transport SHIFT button and the tab long-press still “suggest” one); PROJECTS (4) has a face whose panel ring is empty (worst of both).

**F. Modal traps / EXIT** — bank grid (total), TRACK FX (press-close crashes; EXIT inert), channel popout (EXIT inert), keypad (no typing), confirm (cancel only via EXIT), ARRANGE clip block (no ring escape).

**G. Focus-restoration bugs** — after config popup (index 9 → 0), tempo (14 → 0), tab grid (3 → 0), take sheet (6 → 1), bank grid (5 → 1), LEN popup (5 → 4); channel popout restores (10 → 10) because it doesn’t reset the index.

**H. Off-screen focus** — recorder take list, projects list (no `positionViewAtIndex` on `app.focusId`; routing pipe lists do this correctly and can be copied).

**I. Consistency** — `EncoderSw` = ARM toggle on primary screens vs “activate” in faces/popups; `PRY` on `mixer.expand` is hijacked by the `"pan"` substring; `shift.learn.clear`/binding rows delete without confirmation; `config` kind ring order (metronome) does not follow the visual rows.

---

## 5. `--panel-taps`: the mechanical gate

**Shape.** A new headless mode that mirrors `--ui-taps` but injects **Intents** instead of mouse events, walks every ring, and asserts the parity contract. It must live beside `ui_tap_runner` (new `panel_tap_runner.{hpp,cpp}`) and must not modify `ui_tap_runner`.

**Hook points (exact):**

1. `main.cpp` (`:2118-2135` argument parse): add `const bool wantsPanelTaps = arguments.contains("--panel-taps");`; include it in the `QT_QUICK_BACKEND=software` / `QT_QPA_PLATFORM=offscreen` and temporary-`HOME` blocks (same trade as `--ui-taps`); reject `--panel-taps` together with `--ui-taps`/`--screenshots`. After the window is up (`:2246`), construct `PanelTapRunner runner(&controller, window); runner.start();`. Install the same QML message handler (`UiTapRunner::messageHandler` logic can be duplicated or factored into a shared helper; do not touch the tap runner).
2. **Intent seam.** `AppController::perform` is `private` (`app_controller.hpp:388`). Add a narrow seam rather than making it public API: `friend class PanelTapRunner;` (already the pattern for a test driver) or a documented `void dispatchPanelIntent(const Intent&)` used only when the runner is active. The runner also needs `isEditableFocus`, `focusIds_`, `focusIndex_`, `setPanelFocusIndex` — make the runner a friend so the probe-style access is not repeated.
3. **Deterministic fixture seeding** (before `controller.start()`): write one tiny valid WAV + a `.peaks` sidecar into the temp take dir, adopt it (`RecorderViewModel::adoptRecordedTake` / import), and add two song clips + one section so the take sheet, EDIT popup, clip sheet and clip stops are non-empty. Without this, half the matrix is untestable.
4. **CTest**: `add_test(NAME mnkybox_panel_taps COMMAND mnkybox --panel-taps)` in `app/CMakeLists.txt` next to `mnkybox_app_logic`, with a pass criterion of exit 0.

**Scenario model (mirrors the probe contexts):**

```
struct PanelScenario {
  QString name;
  std::function<void()> enter;              // tab+view, SHIFT, or open overlay
  QStringList expectedRing;                 // golden table (null = derive only)
  enum class Trap { None, Keypad, Confirm }; // NAV must be inert, PRESS=accept
  bool shiftFace; bool exitMustClose;       // EXIT contract
};
```

**Per scenario, in order — each failure increments a counter and prints `panel-taps: FAIL <scenario>: <reason>`:**

1. **Ring walk = permutation.** Snapshot the ring by pressing `NavDown` `n+1` times from index 0; assert: `focusCount()==n`, the first `n` ids are pairwise distinct, the `(n+1)`-th equals the first, and no id is empty. With a golden `expectedRing`, assert set+order equality so that adding a QML control without a ring stop fails immediately.
2. **Every ring stop is a visible, on-screen target.** Walk the loaded QML tree (reuse `collectTargets` logic: items with a non-empty `focusId` property), map the id to items, assert `isVisible()`, `width>0 && height>0`, and the item’s mapped rect is inside the 800×480 window and not behind a higher-z overlay. This single check kills G8 (`track.mode`, `mixer.select.N`, `channel.monitor`, `edit.value`, `midimon.port.N`) and G14 (off-screen rows).
3. **Reverse reconciliation.** For each context, every *visible* `focusId` item must be in that context’s ring or on an explicit allowlist (`tab.N`, `keypad.*` while trapped, `transport.play/stop/rec` in pico mode, no-pico chip, touch-only duplicates). This kills G12.
4. **PRESS semantics.** For each stop, record a model fingerprint (a small typed snapshot: mixer per-slot gain/pan/cue/out34/wet/mode/in/out, master, track*, config draft fields, eq/xy/recorder/session/system/tools/routing/arrange/timeline values, popup+overlay flags, `panelMode`) before and after `NavPress`, then assert the id’s declared class:
   * `editable` → `panelMode=="edit"` and no model change,
   * `action` → the documented field changed (or the documented overlay opened/closed),
   * `choice` → the stopped value equals the id’s index,
   * `dead` → **zero** stops may be classified dead (fail if any).
5. **Encoder.** For each `editable` stop: `EncoderTurn(+1)` must change exactly the expected field (compare fingerprints), `EncoderTurn(−1)` must restore it (reversibility), and at the clamp it may not change. A stop whose `+1` and `−1` both no-op fails (catches `shift.mixer.monitor.N`).
6. **SHIFT.** For each tab: two held frames 600 ms apart must enter the face iff `tabHasShiftFace(tab)`; a face on a face-tab must have a non-empty ring and the ring must be walked clean; tabs 5/7 must keep the primary ring and `panelMode!="shift"`; one EXIT tap must leave the face and land in `navigate`.
7. **EXIT unwind.** For every overlay (tab grid, bank grid, tempo, config kinds, channel, TRACK FX, take sheet, EDIT popup, clip sheet, LEN popup, keypad, confirm): open → one EXIT tap → assert it closed (and the ring is the underlying one, focused on a visible target). Add a **long-hold-EXIT** case for the SHIFT face.
8. **Modal traps.** With keypad/confirm open: assert `NavDown/Up/Left/Right` do **not** change `focusId`; PRESS behaves as accept (keypad: the QML `onKeypadAcceptRequested` handler must run — pump the event loop); EXIT cancels. Do not assert `focusCount==0` (the current code keeps a stale ring; if the fix empties it, assert either is acceptable but be explicit).
9. **Crash = failure.** The runner runs in-process; a SIGSEGV fails the process (non-zero exit). Add explicit per-stop `PRESS` coverage of the TRACK FX popup so G4 can never regress silently.
10. **QML warnings fail the run.** Install a message handler identical in effect to `UiTapRunner::isQmlWarning` (file endswith `.qml`, category `qml`, `TypeError`/`ReferenceError`/`is not a function`/`non-existent property`/`Unable to assign`/`QML `), collect them, print each as `panel-taps: QML: …`, and exit 1 if the list is non-empty.
11. **Output/exit:** `panel-taps: <n> scenario(s), <k> stop(s), <f> failure(s)`, plus a machine-readable table of each context’s ring (`panel-taps: ring <context>: id1, id2, …`) so the golden tables can be diffed. Exit 0 only when failures == 0 and warnings == 0.

**Why this catches the current defects:** G1/G2/G6/G8/G9/G10/G11/G12/G14 are ring-vs-scene or fingerprint mismatches; G3/G4/G5 are EXIT/PRESS coverage; G7 is the trap scenario plus the reverse reconciliation (`keypad.*` visible but not in a ring).

---

## 6. Fix list mapped to the redesign stages

### Stage 1 — navigation / transport shell
1. **F1 (G1):** `rebuildFocusIds` mixer branch must key on `mixerVm_->trackMode()` (the MIXER/TRACK view), not `selectedMode()`; add `trackModeChanged`, `trackChanged`, `tracksChanged`, `tracksChanged` (strip add/remove) and selection changes to the rebuild set. Replace the literal `switch (tab_)` cases with the named constants and a `static_assert`/table so a tab can never fall in the wrong case again.
2. **F2 (G2):** `case 5:` → `case kProjectsTabIndex:` (or `case 4`) in the SHIFT switch; add a test that every `tabHasShiftFace(tab)` tab yields a non-empty face ring.
3. **F3 (G3):** handle `bank.page.N` → `chooseBankPage(N)` and `bank.page.close` → `closeBankGrid()` in `activateFocus`; add `bankGridOpen_` to the EXIT unwind and to the grid-nav branch of `performPanelFocus` (UP/DOWN ±columns, LEFT/RIGHT ±1).
4. **F4 (G5):** extend `updateShiftState`’s short-press chain with `fxVm_->open() → closeTrackFx()`, `mixerVm_->channelOpen() → closeChannel()`, `bankGridOpen_ → closeBankGrid()` (order: above the screen-level popups).
5. **F5 (G12):** promote `transport.tempo` (opens tempo popup) to a ring stop; decide `transport.shift`/`transport.nopico` (ring stop or drop the `focusId`); add `shift.monitor.clocksource.0..2` to the System SHIFT ring.
6. **F6 (G7):** make the keypad panel-complete. Minimum: ring over the key rows (2-D nav 10-wide) with PRESS = key, EXIT = cancel, and keep the encoder-cursor behaviour; or a dedicated “character wheel” on the encoder with `SHIFT` = cursor. At minimum the four action keys (CANCEL / DEL / CLEAR / DONE) must be reachable, and the limitation must not be silent.
7. **F7 (G9):** save `focusIndex_`/`focusId_` per overlay on open and restore on close (prefer restoring by id when it still exists, else the saved index, else 0). Same for `openConfig`’s `focusIndex_=0`.
8. **F8 (G13):** decide `EncoderSw` semantics once: “confirm focused control” (`activateFocus` / leave EDIT) everywhere; if ARM-toggle is required, make it a ring stop (e.g., a strip ARM stop) instead of an invisible global.
9. **F9 (G8):** make `focusCount()` truthful for the modals (empty ring or a documented trap) and stop advertising a stale ring.

### Stage 2 — mixer
10. **F10 (G6):** replace `isEditableFocus`’s `contains("gain")/("pan")/...` with exact/prefix sets (or a table keyed by a parsed id); add a unit test in `logic_tests.cpp` covering `mixer.expand.N` ≠ editable. This alone re-opens the ChannelPopout.
11. **F11 (G8):** give `mixer.select.N` a real `focusId` on the strip body, or drop the stop and let `mixer.expand.N` be the strip selector.
12. **F12 (G8):** add a real widget for `track.mode` (a MODE button in TRACK view) or drop the stop; add `track.expand` to the TRACK ring (channel entry from the TRACK view).
13. **F13:** `shift.mixer.monitor.N` — add activate (cycle monitor mode) + encoder (cycle) or remove the stop; keep QML and ring in lock-step.
14. **F14 (G12):** add `channel.mode.0..3`, `channel.recsource` to the channel-popout ring; remove `channel.monitor` or add the control; F4 closes it.
15. **F15 (G4):** guard the `trackfx` branch (`parts.size() >= 3`) and handle `trackfx.close`/`.select` (select should open the fxslot list, not just EDIT); F4 closes it with EXIT.
16. **F16:** the channel popout ring should follow the popout’s visual order (currently close/rename first, then the fader column; acceptable but document it).

### Stage 3 — ARRANGE / EDIT
17. **F17 (G10):** break the clip nav trap: e.g. UP/DOWN move between lanes but a second UP on the topmost lane / DOWN on the bottom lane moves the ring past the clip block, or add explicit `arrange.clipmenu`/`arrange.render` stops before the clips; also rebuild on `selectClip`.
18. **F18:** add `arrange.lane.N` (select strip) and `arrange.playhead` (seek) ring stops, or remove their tap affordances from the “everything is panel-reachable” claim.
19. **F19 (G8):** give `edit.value` a real focus target (a value chip inside the selected op card); give the EQ SHIFT rows a real `focusId` (not `shadowFocusId`).
20. **F20:** EDIT op cards: make the encoder on `eq.band.type`/`edit.op` cycle rather than no-op where the QML ± buttons already cycle.

### Stage 4 — recorder / sheets
21. **F21 (G12):** add `recorder.takesheet`, `recorder.take.wave.N`, `recorder.take.edit.N` to the recorder ring (or delete their `focusId`s and rely on the row menu); add `take.sheet.edit.N` and handle it in `activateFocus`.
22. **F22:** add `shift.recorder.mode` to the Recorder SHIFT ring.
23. **F23:** decide on the duplicate `punch.*.measure.dec/inc` and `loop.*.dec/inc` ids (ring stops or drop the `focusId`s).
24. **F24 (G14):** auto-scroll the recorder/projects `ListView` to the focused row (copy `RoutingScreen.qml:873` `positionViewAtIndex` pattern keyed on `app.focusId`).
25. **F25:** take rename/import depend on the keypad fix (F6); take EDIT is reachable through the row menu, keep that as the documented path.

### Stage 5 — everything else
26. **F26 (G2/G12):** Projects: add a row-select ring stop or a two-step PRESS (select → load), and ring the SHIFT mirror rows; after F2 the verbs become usable.
27. **F27 (G11):** split `isEditableFocus` into *value steppers* (PRESS = EDIT) and *choice cards* (PRESS = pick the stopped option); fix the metronome ring order to match the visual rows.
28. **F28:** MIDI port rows must declare `property string focusId` so the focus ring is visible and `--ui-taps` covers them.
29. **F29:** XY: add `xy.bank.0/1`, `xy.track.N`, `xy.<grid>.pick` to the ring (or remove the ids); decide whether XY/MIDI get a SHIFT face or the transport SHIFT button is hidden on face-less tabs.
30. **F30:** `config.takeedit` dead branch + QML column, `config.renderregion`, `ToolsScreen.qml`, `tab.N`: delete dead code or ring it; add `config.pipeparam` guard when no pipe is selected.
31. **F31:** learn-binding removal and CLEAR should confirm (same `requestConfirm` path as DELETE) or require a long press.
32. **F32:** land `--panel-taps` as a CTest gate and keep the golden ring tables in the runner so every stage’s fixes are locked in.

---

## 7. Residual risks / open questions

* The take-edit popup, clip sheet and clip stops were verified by code reading only (no take/clip fixture in a fresh session). The gate design requires fixture seeding; the fix stages should run it.
* `routing.node.in.<port>` uses device port ids: on USB hotplug the ring identity changes. The code comments call this intentional; the gate must re-enumerate after priming, not hard-code the ids.
* The panel ring is 1-D while every screen is 2-D. The owner contract says “joystick navigates between elements within a screen”; this audit assumes 1-D order is acceptable (it is what `moveFocus` implements). If 2-D spatial navigation is required, it is a redesign, not a gap fix.
* `--ui-taps` is documented as a gate but is not a CTest; `--panel-taps` should be registered so it actually runs.
* Product decision needed: is single-press destructive learn-clear/binding-remove acceptable? (Listed as risk, not fixed silently.)
* The skip of the current track-mode pill: `nextTrackMode` cycles 1→2→3→0, so pressing the pill on the focused strip can land the track in PLAY and flip the ring face (F1 makes this harmless by keying on the view and rebuilding).

---

## 8. Reproduction appendix

```
# build (own dir, no repo change)
cmake -S <worktree>/midibrain/pi -B /tmp/panel-audit-build \
      -DFETCHCONTENT_SOURCE_DIR_CLAP=/tmp/pi-build/_deps/clap-src -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build /tmp/panel-audit-build -j8 --target mnkybox          # exit 0

# touch baseline
/tmp/panel-audit-build/app/mnkybox --ui-taps                       # 864 taps, 0 warnings, 0 failures, exit 0

# panel probes (link the built objects minus main.cpp.o; see /tmp/panel_probe*.cpp)
/tmp/panel_probe     # ring dump for all 9 tabs × faces + overlays   -> /tmp/panel_probe_out.txt
/tmp/panel_probe2    # per-stop PRESS + encoder for all contexts      -> /tmp/panel_probe2_out.txt
/tmp/panel_probe3    # EXIT unwind matrix (channel/fx/bank fail), trackfx.close crash
/tmp/panel_probe4    # SHIFT enter/leave, EDIT exit, focus restoration
/tmp/panel_probe5/8  # mixer view × track-mode cross product (face bug + staleness)
/tmp/panel_probe6    # list-choice PRESS semantics
/tmp/panel_probe7    # routing pipes page, pipeparam/pipechannels, bank grid with 20 tracks
/tmp/panel_probe9    # bank-grid lockout, tempo/confirm/keypad traps
/tmp/panel_probe10   # mixer ring per-stop PRESS/encoder table
```

Key measured lines:
* `tab 4 … faceRing=0` (Projects SHIFT empty), `bank grid … closedByExit=0`, `PRESS bank.page.close … bankGridOpen=1`, `PRESS trackfx stop 0` → SIGSEGV, `system stop=9 → after=0 restored=0`, `MIXER view mode=PLAY ring=47` vs `mode=ARM ring=16`.
* `view=MIXER mode=0` ring contains `mixer.expand.0` and `isEditableFocus("mixer.expand.0")==true` (the `"pan"` collision).

---
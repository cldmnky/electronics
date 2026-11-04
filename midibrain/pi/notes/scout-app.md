Report written to `/tmp/kick-architecture-scout.md` (265 lines). Full text:

---

# Behringer KICK (BE 0700-AAM) — architecture scout for mnkybox

READ-ONLY. Sources: cloned `MusicTribeOpenSource/0700-AAM` to `/tmp/aam-app` (the Kick tree is **not** in `cldmnky/electronics`; that repo has `froemix/` and `midibrain/` only). Licence of Kick/AudIO/ComProtocol: **AGPL-3.0-or-later**. Everything below is my own description of ideas; no code or prose was copied. JUCE 9.0.5 reference read from our build cache (`/tmp/pi-build/_deps/juce-src`). Our app read from `midibrain/pi/app/` (+ `pi/ARCHITECTURE.md`); note that `pi/app/ui_style.hpp` + a new `theme.hpp` were being edited by another worker while I read, so treat the `pi/app` comparisons as of that read, not as current HEAD.

---

## 1. App module map

Repo top level: `Kick/` (the app, JUCE), `AudIO/` (engine lib), `ComProtocol/` (hardware protocol), `Linux/` (Buildroot image). Build: CMake + CPM, C++17, JUCE fetched and patched (`Kick/patches/juce_multitouch_7.0.12.patch`), options for VST/simulator/multitouch-demo/ASAN/Perfetto (`Kick/CMakeLists.txt`). Notably `-w -ffast-math -Ofast`; no warnings-as-errors.

**Processes** (`Kick/apps/`, all JUCE apps except `midiPort`, each deriving from `contracts::IPCApplication`): `kick_main` (supervisor: heartbeat monitor, exit-message handler, logger), `kick_gui` (the touch UI, kiosk fullscreen), `kick_audio_engine`, `kick_audio_model`, `kick_device_control` (DCL — owns the MCU/serial link), `midi`, `midiPort` (ttymidi), plus `kick` (single-process build: UI + engine + adapters, no IPC), `kickem` (simulator), `kickvst`, `multitouch` (XI2 demo), `kick_device_control_test` (manual IPC harness).

**Modules** (`Kick/modules/`, each a JUCE module with an explicit dependency list):
- `common`, `contracts` (59 files), `ipc` (47) — cross-process plumbing: socket server, connection manager, message dispatcher, handler store, process manager/executor/restarter, JSON helpers; `contracts` holds per-process message-type enums, key strings and config.
- `input_events` — serial/PktMgr hardware packets in and out (`dispatchHardwareEvent`).
- `hardware` — firmware update, I2C, USB functionfs, Wi-Fi firmware, HW prefs, resources.
- `engine` (147) — `audio_engine`, `audio_program`, `audio_midi`, `audio_grouping`, `audio_file_manager`, `project` (settings + XML manager), `objects/sampler`; `interface/` is the big `MTAudioInterface` facade.
- `audio_interface` — two-line shim that pulls in `AudIO_juce`.
- UI layers, bottom-up: `kick_ui_common` (Screen enum, `IScreen`, constants, radio-group ids) → `kick_ui_controls` (73 widget files) → `kick_ui_components` (pad button/grid, tabs, menu, I/O component, keyboard, XY grid) → `kick_ui_header` (mode header + metronome popup) → `kick_ui_selectors`, `kick_ui_settings` (audio/cv/ethernet/hardware/info/midi-sync/wifi pages), `kick_ui_channel_mixer` (17: one strip type per bus role) → `kick_ui` (167: screens, "modes", audio components for grid/sequencer/sampler/looper/sample-editor/XYFX, dialogs, recent project, SW update). `program_edit_ui` (80) is a separate per-instrument editor module (`sources/{drum,keygroup,audio,clip,midi,cvgate,common}`). `kick_leds`, `kick_ui_systemResources`.

Contrast with ours: 34 files in one dir, `Screen` base + `AppContext` + `MixerModel` + glue, no module split, single process, no IPC. Their layering (controls/components/header/screens as separate units with declared deps) is the borrowable part; the process split is not.

## 2. Screen / navigation / state architecture

**One router component, all screens alive.** `KickHome` (`kick_ui/dialogs/kick_AppHome.{h,cpp}`) is a singleton `Component` + `AsyncUpdater` that owns `std::map<Screen, unique_ptr<IScreen>>` and a `std::stack<Screen>` back stack. Every screen is constructed once in the constructor (a `screenAdder` lambda registers ~28 screens) and stays alive; navigation only toggles visibility. `load()` emits a "closed" signal, validates the requested `Screen` against the map (unknown → `HomeListView`), then `triggerAsyncUpdate()`; the async callback hides all screens, shows the target, sizes it to the area left after the optional header/status bar, and emits "loaded" — keeping component show/hide out of hardware-event callbacks.

**Screen contract.** `kick_ui_common/source/kick_IScreen.h`: `enum class Screen` (~40 values in two families — `Home*` for the main tabs, `Menu*` for modal modes) plus `IScreen` with virtual `requiresControlBar()`, `requiresStatusBar()`, `requiresHandleCommand()`, `canClose()`, `knobCommand(knob, value)`, `handleCommand(HWMsg)`, `closeScreen(next)`; cross-cutting events go through sigslot signals (`sigScreenOpen/Close`, `onScreenLoaded/Closed`, `onScreenData`). Screens are also `juce::Component`s; the shared top bar and the status bar are owned by the router and shown/hidden per screen flags, while each screen owns its own `KickModeHeader` (back, play/stop, metronome, automation, new project, settings, plus arbitrary added buttons).

**Two navigation inputs, one enum.** Touch navigation (menu tiles via `KickMenuComponent`, tab bars via `KickTabbedComponent`, header back button) and physical panel buttons both end in the same `Screen` values. `KickHome::handleCommand` is the single dispatcher and its priority order is: system/handshake packets → global nav buttons (`EN_MENU`, `EN_MAINMODE`, `EN_GRID`, `EN_PREF`) → mode buttons (`EN_FX`, `EN_STEP_SEQ`, `EN_LOOPER`, `EN_SAMPLER`, `EN_LINK_EDIT`, …) → focused component (`ui::getComponentInFocus()->handleCommand`) → current screen if it asked for commands. Knob packets go to the KnobEdit screen unless the app is in knob-control mode. Physical pads and bank buttons are handled alongside. So a hardware key and an on-screen tile are the same action.

**State model: one global facade, ValueTree underneath.** `mt::getAudioInterface()` (`engine/interface/source/mt_audio_interface.h`, ~600 lines) is a singleton model facade: project load/save, BPM/length, a *global selection* (`MTSelected` = sequence + track + program), CRUD for programs/sequences/tracks, pads/samples/mutes/groups, inserts, recorder, MIDI send, playhead/transport, undo manager. Views implement `MTAudioInterfaceListener` and/or connect to sigslot signals (`bankChanged`, `playStateChanged`, `onBar/onBeat/onTick`, `onTrackMetersUpdated`, …). No per-screen model: screens read the same global selection and must refresh on its signals (hence the `visibilityChanged`/`onLoad` hooks everywhere).

Persisted/editable values are `juce::ValueTree` nodes addressed by `mt::MTIDs` symbols (`engine/interface/source/mt_audio_ids.h`: `BUSES/BUS/IN/OUT/ROUTE/SEND/SUBMIX`, `MIDIPort`, `BankLSB/MSB`, `ProgCH`, `CV*`, `INSERTS`, `GROUPING`, …), exposed as `CachedValue<T>` on the typed wrappers in `mt_audio_interface_types.h`. The UI binds to `juce::Value` through the app's own attachment layer (`kick_ui_controls/source/kick_attachments.h`, `kick_attachedControls.h`): a generic `Value::Listener` base with a bounds-checker function, plus `*WithAttachedValue` control aliases (slider/rotary/number/toggle/label/tempo/toggle-options). Not APVTS — the engine is not a plugin — but the same idea, with a custom two-way bridge. Undo is a shared `juce::UndoManager`.

Settings/prefs are further singletons (`mt::getSettings()`, `mt::HwPref::getHwPref()`).

## 3. Touch patterns

**Design resolution and scaling.** `kick_ui_controls/source/kick_layout.h` declares 1024x600 app and screen size, `_scaledX/_scaledY` macros for absolute metrics, and named rects: `header` 76 px, `statusBar` 64 px (bottom), `tabBar` 54, `tabRow` 110, `title` 20, dialog = 63.5% x 86.7% of the screen. Inside screens, layout is mostly *relative*: ~1100 `setBoundsRelative` calls vs ~550 absolute `setBounds` (grep counts across `modules/`), frequently written as fractions of the 1022 px inner width. The result is one screen definition that stretches to any panel size.

**Target sizes.** Recurring minimum is 44 px: header icon buttons are 44/1022 of the width, list rows 60 px (`kick_listbox`) with 34 px for dense secondary lists, step-sequencer rows ≈50 px (8 rows inside ~460 px), mixer pad rows 107 px (`kick_PadMixer.h ROW_HEIGHT`). Chrome is generous: 140 px of 600 (~23%) is permanent header + status bar.

**Gestures are hand-rolled, one per need:**
- double-tap: manual timestamp compare, 400 ms window, in `KickNumberT` (`kick_number.h`) to open the numeric keypad popup;
- long-press: 500 ms threshold + 5 px drag slop + timer in `RecentItems` (`kick_ui/recent_project/kick_RecentProject.h`) to pop Load/Remove actions over a list row;
- drag-to-scroll: `ScrollOnDragMode::all` on essentially every viewport (`kick_viewport.cpp`, `kick_listbox.cpp`, `kick_table.cpp`, MIDI-control tabs, file browser, program-edit grids) and explicitly `never` on the sequencer grid where drag means edit (`kick_AppGrid.cpp`);
- pinch-zoom: only in the sequencer, implemented per finger in `GridMagnifyTool` (`kick_ui/audio_components/gridview/kick_GridMagnifyTool.cpp`): a map of finger index → point, and when two fingers are down it scales by the distance ratio against the initial distance, anchored at the midpoint, while one finger pans.
- Also: unbounded mouse movement is enabled on the main mouse source in `kick_gui_main.cpp` so a drag past a control or screen edge does not clamp (matters for fader/knob drags).

**The JUCE multitouch patch.** `Kick/patches/juce_multitouch_7.0.12.patch` (7.8k lines, mostly a whole-file reindent) backports XInput2 touch into JUCE 7.0.12's X11 backend: an XI2 capability check (XInputExtension, version ≥ 2.2), `XISelectEvents` for touch begin/update/end, a per-device → per-finger index map via `MultiTouchMapper`, per-finger `handleMouseEvent(InputSourceType::touch, …, touchIndex)` dispatch with synthesized press/release transitions so a touch behaves like a left-button drag, `MouseInputSourceList::addSource()` creating touch sources and `canUseTouch()` reporting true, plus a small filter to swallow the synthetic mouse motion X11 still emits for a touch. It is clearly a local WIP: debug `std::cout` on every touch event, a hand-written "filtered position" flag in the window-system struct.

**Does JUCE 9 still need it? No.** Our pinned JUCE 9.0.5 already contains the same feature, cleaner: `modules/juce_gui_basics/native/juce_XWindowSystem_linux.cpp` has an `XInputHelpers` namespace (`setupXI2` with the same ≥2.2 check, `registerForXI2Events` selecting XI_TouchBegin/Update/End on `XIAllMasterDevices`), `XWindowSystem::canUseMultiTouch()`, `MouseInputSourceList::addSource()` adding touch sources, and the same per-finger dispatch through `MultiTouchMapper` with press/release synthesis and offscreen release. It is behind `JUCE_USE_XINPUT`, default 1 (`juce_gui_basics.h`), and the XI2 entry points are resolved at runtime (`juce_XSymbols_linux.*`), so no `-lXi` link is needed — only XInput2 headers at build time. Caveats: this is the X11/XWayland path only (a bare KMS/DRM or native-Wayland backend gives no XI2), and multi-touch is only useful if the app reads it. **Our app reads none of it**: no `event.source.isTouch()` / `getIndex()` anywhere in `pi/app`, so today every gesture is single-pointer — we get the capability for free and simply do not use it.

## 4. Parameter-widget patterns

**Declarative configuration.** Every widget is `KickControlBase<SomeJuceComponent>` plus `mt::MTProperties` (`kick_ui_controls/source/mt_properties.h`), which stores presentation in the component's `NamedValueSet`: title/value proportions and justifications, font heights, corner radius, per-element colours, margins/padding, border, "show knob", prefix/postfix/units, pad char/length, timer speed, scroll progression. A widget is configured by property setters, not by subclassing — screens read as layout + property lists.

**One input abstraction for touch, encoders, faders and buttons.** `KickControlsProcessor` (`kick_ui_controls/source/kick_ControlBase.h`) declares `sliderMoved(normalized)`, `knobRotated(delta)`, `knobClicked()`, `bankPressed(i)`, `padPressed(no,state,vel,at)`, plus a default `handleCommand(HWMsg)` that decodes DCL hardware packets (encoder CW/CCW, encoder press, +/-, motorized fader percentage, pads, banks) into those calls. Every widget derives from it, so the same control responds identically to a finger drag, a physical encoder, a hardware fader and a panel button with no per-widget glue. Widgets also push back to hardware (`sendPercentValueToHardware`) — two-way binding, so motorized faders/LED rings follow the UI.

**The widget set** (`kick_ui_controls/source/`): `KickSlider` (default style `LinearBar`, split title/value proportions, `getNormalized/setNormalized`, `onDragStop`), `KickRotary`, `KickSliderWithTitle`, `KickNumber` (tap opens a `KickNumPad` popup with Apply/Back), `KickToggleOptions` (segmented multi-state, encoder driven too), `KickListBox`/`KickTable`, `KickGainSlider` (custom dB scale with markings, drag + double-tap reset), `KickOffsetControl`, `KickCyclicStateControl`, `KickTempo`, `KickNoteSelector`, `KickMeterVerticalWithMarks`/`KickLevelMeter`, `KickVirtualKeyboard`, `KickProgressBar`, `KickToggleButton`/`KickImageButton`/`KickColorButton`, `KickLabel` family. A `Theme` object indexes colour banks by role (background/title/value/highlight/selection/outline) so all widgets stay coherent.

**Focus for the 4 assignable knobs.** `ui::getComponentInFocus()` / `clearFocusComponent()`; the focused control receives navigation packets, and the KnobEdit screen assigns a physical knob to a parameter (long-press the LINK button to clear). Big-number styling everywhere: `DEFAULT_MAX_FONT_SIZE 24`, LinearBar sliders that draw name and value inside the bar.

## 5. MIDI / routing / recording UX model

**I/O is a reusable strip component, not a dialog.** `KickIOComponent` (`kick_ui_components/source/kick_IOComponent.h`) bundles input selector, output selector, gain, pan, record arm, monitor on/level, a 4-line inserts summary and a stereo meter, and emits `inputChanged/outputChanged/gainChanged/panSliderChanged/recordButtonStateChanged/monitorStateChanged` plus an inserts-click callback. The same component is dropped into mixer tabs, the program editor and the sampler, so routing is edited on the strip you are already looking at rather than in a separate routing page.

**Mixer screens are data-driven tabs over a handler interface.** `kick_ui/modes/mixers/kick_MixerHandlerInterface.h`: a `KickTabbedComponent` whose tabs are data (`tabName`, `multiTabCount`, per-tab `PadButtonType` list), and a handler subclass supplies `getInputs()`, `getOutputs()`, `getIoOptions1()`, level range/display markings/centre, button labels and a `refreshContent()` called from `visibilityChanged`. Concrete handlers per program type (drum pad mixer, keygroup pad mixer, channel mixer).

**One strip type per bus role.** `kick_ui_channel_mixer/source/`: `kick_audio_track_mix`, `kick_track_midi_mix`, `kick_program_mix`, `kick_sends_mix`, `kick_returns_mix`, `kick_submixes_mix`, `kick_master_route_mix`, `kick_output_track_mix`. The routing model is made visible by giving each role its own strip widget rather than one generic node view.

**Model side mirrors that vocabulary.** `mt_audio_interface_types.h`: buses with four sends (`send1..send4` as `CachedValue<GainConstrainer>`), a `routedTo` name, `record` + `RecordState`, an insert chain interface (`MTIInsertsInterface`: add/bypass/params/info), per-track `MTMeterData`, and a global `MTPlayhead`/transport. `mt_audio_ids.h` shows MIDI routing (port, bank LSB/MSB, program change, MIDI-learn, CC table) and CV/gate routing (`CVNote/CVPort/CVGate/CVModWheel/CVVelocity/CVStartNote`) as ordinary properties of the same objects — routing is properties, not a separate graph editor.

**Recording always ends in a decision.** The recorder is a first-class object on the audio interface (`enableRecorder`, `setupRecorder(seconds, gain, pan, looping, overdub)`, `startRecorder(mode, direction)`, `stopRecorder`, `getRecorderProgress`, input/output meters, `MTRecorderListener` start/stop/pause/length callbacks). The UI wraps it in a dedicated Looper screen (`kick_ui/audio_components/looper`) and a Sampler screen whose take review is a keep/discard component (`kick_KeepDiscardSampleComponent`) followed by a save-sample dialog — a take is never silently kept or dropped.

**Transport is global and always on screen.** `AppStatusbar` shows STP position + bar:beat:tick + a playhead progress bar; `KickModeHeader` carries play/stop, metronome, automation, new project, back. Both are owned by the router, not by screens.

**Hardware event path.** `input_events` (PktMgr packets over serial) → `contracts::DclSignals` → `KickHome::handleCommand`; the DCL process owns the MCU link, and in the split build the GUI receives the same events over IPC (`contracts/dcl`). The UI never touches the port.

## 6. Test strategy

There is essentially none, and that is a deliberate/legacy choice worth naming:
- No unit-test framework: no Catch2/gtest/`juce::UnitTest`, no test target, no CTest anywhere in `Kick/modules` or `Kick/apps` (grep). `apps/kick_device_control_test` is a *manual* IPC harness binary (sends reconnect/shutdown to the DCL and prints), not an assertion suite.
- Substitutes: the simulator/host apps (`kickem` with `apps/kickem/simulator`, the single-process `kick`, `kickvst`, `kick_gui`), the `multitouch` demo app as the on-device probe for the XI2 patch, and `demos/Demo 01..05` — five `.kickPrj` projects plus screenshots used as fixtures for manual visual checks.
- CI (`CI/azure_build.sh`, `CI/azure-app-pipeline.yaml`) only cross-compiles the aarch64 Buildroot image and the app weekly; no test stage.
- Verification in practice is logging + on-device eyeballing: `juce::Logger`/NanoLog, a log IPC process, and a heartbeat monitor with process restarter in `kick_main`.

Our `pi/app` is ahead (`logic_tests.cpp` under CTest `mnkybox_app_logic`, `--unittests`); the borrowable idea is their "demo project as fixture" practice, not their testing model.

## 7. Top 5 concrete UI/UX improvements for our 800x480 touch DAW

1. Turn on drag-to-scroll everywhere it belongs — `Viewport::setScrollOnDragMode(ScrollOnDragMode::all)` on the mixer strip holder, FX parameter list and any take list, `never` only where drag edits content; nothing in `pi/app` sets it today, so an 18-strip mixer is finger-unscrollable.
2. Add one persistent bottom transport/status strip (bar:beat:tick, rolling, tempo, rec state, master meters) shared by all five screens instead of transport living only in Recorder — KICK spends ~23% of its height on permanent chrome and that is what makes it feel like an instrument.
3. Route every parameter through one input abstraction (touch drag + panel buttons + nudge) with a visible focus ring, mirroring KICK's `sliderMoved/knobRotated/knobClicked`; our intents today only reach tab/strip/arm/transport, so panel buttons cannot edit a value.
4. Standardize three gestures with named constants — 400 ms double-tap for numeric entry, 500 ms long-press (5 px slop) for row actions, pinch-zoom only where zoom means something — implemented per finger via `event.source.isTouch()/getIndex()` (free on our JUCE 9, no patch), keeping single tap as the only primary action.
5. Make each parameter a big LinearBar-style control that draws name and value in one bar (24 px-class font, role-coloured state) and reuse a single I/O strip component (in/out select, gain, pan, arm, inserts, meter) across Mixer/Routing/FX, so routing is edited in place and we buy back vertical pixels at 480 px.
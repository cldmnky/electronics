# TODO — missing functionality

This is the single list of work that is **not implemented or not yet verified**.
It is not a plan, a schedule or a promise. Each row records what exists in the
tree today, what is missing, and what closes it. When an item lands, delete the
row; do not mark it "done" here (git history and the code carry that).

Physical acceptance is machine-readable in `hardware-acceptance.json` and is
enforced by `make release`; the rows below restate those items so the whole
gap list is in one place. Evidence format: `docs/evidence/README.md`.

## Hardware — physical acceptance (all 26 items pending)

`make gate` prints the current state; every item is `pending` until measured
evidence exists against the current hardware fingerprint.

| Item | Exists today | Missing | Definition of done |
|---|---|---|---|
| `zstack` | Provisional Z chain in `tscircuit/lib/layout.ts` (`PROVISIONAL_Z`, 20.65 mm front-board rear, 14.05 mm display, 5.0 mm spacer) | Measured board, standoff, display, Pi, HAT, cooler and panel thicknesses; installed depth | Evidence file with each measured thickness and the installed depth |
| `control_actuator` | Candidates in `assembly.md`; panel openings from `layout.ts` | Knob, button caps and 5-way nav fitted and measured: shaft, detent, cap height, direction legends | Measurements and fitted-photo reference |
| `jack_retention` | XKB PJ-301M-3-B selected; 6.2 mm panel opening | Barrel/nut/panel fit, tip/ring/sleeve continuity, pull-out force on the sample | Continuity and retention results |
| `light_pipes` | `LIGHT_PIPE_REACH`: PLP2-400-C 10.16 mm vs 17.45 mm gap, 7.29 mm unaccounted | A pipe (or optical path) that reaches, fitted and measured | Fitted length and LED-to-pipe gap; all three LEDs visible |
| `display_dsi` | 4.3-inch DSI module selected; 120 mm cable is the design pick; route contract in `mechanical-stack.md` | Module mounted, cable routed, bend radius and latch checked, underside clearance recorded | Measurements per the route contract |
| `display_boss_pattern` | `DISPLAY_BOSS_PATTERN` (99.30 × 61.80, SMTSO-M2.5) is the fitted pattern in `layout.ts` | Physical confirmation that the bosses take the spacer screws and the 58 × 49.04 holes do not; 9.0 mm plane separation measured | Two direct measurements |
| `display_fpc_idc` | FPC connector at (38.84, 11.7); Z/route contract in `mechanical-stack.md` | FPC plug + J5 IDC plug/ribbon coexist in the assembled stack | Route and clearances recorded |
| `module_label_sku_read` | Selected part: Waveshare 4.3inch DSI LCD, ElectroKit 4103 5687 | Read the label on the delivered module and match it | Label/SKU recorded |
| `touch_window_alignment` | 107.22 × 68.87 mm window; active area 95.04 × 53.856 mm | Touch mapping checked with the display seated | Corner/edge alignment recorded |
| `pi_microsd` | Pi held on a 58 × 49 mm grid on the rear board | microSD insert/remove with the Pi in the stack and case closed | Clearance recorded |
| `cooler_hat_ssd` | Full-size M.2 HAT+ and Active Cooler are candidates | Stack height, airflow and connector clearance measured | Measurements recorded |
| `case_fit` | 32HP/3U panel, 20HP rear board, rail holes ±73.78 / ±61.25 | Every rail/standoff position fitted in the case | Installed-depth and fit record |
| `usb_c_5a_path` | CAB-15455 is 3 A and rejected for the 5 A path | An exact 5 A rated panel feedthrough from a primary source | Part, rating and source recorded |
| `usb_pd_droop_thermal` | 27 W Pi PSU is a candidate only | Full-load PD negotiation, droop and temperature in both orientations | Measurements in both orientations |
| `encoder_solder_process` | PEC11R-4015F-S0024 (C143789), THT; bench technique in `assembly.md`; Bourns does not recommend hand soldering | Selective/wave or validated manual process with profile and inspection | Process record per `encoder_solder_process` |
| `midi_threshold` | TLP2361 receiver, 220 Ω input; SPICE only | Measured threshold against the CA-033 < 5 mA requirement | Measurement at recorded supply/temperature |
| `midi_receiver_high_current_thermal` | 2–6 mA recommended, 10 mA absolute, derating above 110 °C | Input current and junction temperature at worst-case high-rail/low-VF corners | Measurements and 220 Ω justification |
| `midi_out_legacy` | AO3401A high-side output stage; SPICE current checks | Legacy receiver loads measured: low-rail current, rise/fall < 2 µs, fault temperatures; independent topology review | Measurements plus review record |
| `midi_interop` | Host parser/route tests only | Two real MIDI devices exchanging note, clock and sysex both directions | Equipment list and traffic log |
| `midi_current_faults` | Protection parts in the netlist | Short, open, reverse and over-current cases on both inputs and outputs | Fault matrix results |
| `scope_spi` | SPI0 mode 0, 1 MHz, IRQ-driven 32-byte frames; host reliability tests | Scope/logic-analyser capture incl. Pico-USB and Pi-off cases | Capture at the target rate |
| `scope_power_sequences` | Power branch designed; SPICE backfeed checks | Power-up/down order for Pi, Pico, display and 3.3 V rails | Capture with no backfeed or latch-up |
| `scope_thermal` | None | Closed-rack soak at recorded ambient/load; cooler noise; NVMe temperature | Soak record |
| `scope_dfm_cpl` | `fab_check.py` compares geometry with published limits; six `tsci check` reports pinned | JLCPCB DFM/order-preview review of copper, mask, silk, drill and CPL rotation on prototype material | Review findings recorded |
| `autoroute_upstream_warning` | Known tscircuit line "Could not save autorouting paths ... unique PCB port" appears on both boards; printed as a note by `check_reports.ts` | Resolve upstream or review its effect on the routed copper | Resolution or recorded copper review |
| `first_article` | None | First-article boards built, inspected and tested against the full list | First-article report |

## Pi app (`pi/app`, `pi/audio`, `pi/session`, `pi/update`)

| Item | Exists today | Missing | Definition of done |
|---|---|---|---|
| Device-out channel mapping | Session stores `deviceOut` pairs and per-track `out34` sends (`pi/session/session.hpp`); engine exposes OUT3&4 buffers (`pi/audio/engine.hpp`) | The audio backend does not map the stored DEVICE OUT pairs onto interface channels; the UI says so (`USER-MANUAL.md` §12) | Device-out channels audible on the selected UMC1820 outputs and round-tripped |
| Take audition | Take selection loads the ACTIVE take; `recorder_view_model.cpp` calls selection "an audition" | No tap-and-hold preview to CUE/phones without loading the take | Preview verb on the take row, to CUE only, no disk change |
| Firmware version readout | `ToolsViewModel::firmwareVersion()` exists, but returns "not reported by the link status (no version field)" | A firmware version field in the link status and its display | Real version shown after link-up |
| In-app Pico update | `pi/update` host-tested core + `pico-update` CLI (`pi/update/NOTES.md`); manual BOOTSEL copy works | The app does not drive `0x22` BOOTSEL, flash, restart and re-link; no UI flow | Update from SYSTEM → TOOLS, with progress and link-return check |
| VOL and GAIN persistence | Session stores one combined `mixer.channels[].gain` (`pi/app/NOTES.md`, `project_io.cpp`) | Independent fader volume and input-trim persistence | Both values survive save/load independently |
| Transport follow control | Firmware already follows incoming START/STOP/CONTINUE per source when the mask bit is set (`clock_follow.hpp` `transportMask`, `main.cpp`); the session stores `clock.transportFollow[]` (`system_view_model.cpp`, `logic_tests.cpp`) | No app control to set the mask; `USER-MANUAL.md` §16 says so | A control that enables/disables incoming transport per source, persisted |
| Touch/panel physical pass | `touch-testing-checklist.md` covers every screen; `--ui-taps` passes offscreen | The owner's fingers on the real 800×480 panel | Every checklist box ticked on the fitted panel |
| Pi hardware run | Studio Pi 5 run recorded in `pi/app/NOTES.md`; no Pico attached | `--unittests`/`--ui-taps` on the device, CLAP discovery, DELAY/REVERB audible | Device run recorded; no QML warnings |

## Firmware (`firmware/pico`)

| Item | Exists today | Missing | Definition of done |
|---|---|---|---|
| SysEx forwarding | Parser skips SysEx content and preserves running status (`midi.hpp`) | Long SysEx is dropped in v1 | Bounded SysEx pass-through on both TRS ports with a host test |
| Scheduled MIDI_SEND | Route/pipe output is timestamped at the tick grid (`main.cpp`, P2); `MIDI_SEND` is still immediate (`protocol.md` limitations) | A musical timeline for app-scheduled sends | `MIDI_SEND` accepted on the grid without displacing `F8` |
| Credit-based flow control | App path is stop-and-wait per control frame | A credit/window scheme for sustained control traffic | Bounded in-flight frames with no loss under load |
| SPI above 1 MHz | 1 MHz nominal on both ends | Validated higher-rate SPI | Scope capture and link tests at the higher rate |
| Dual-core split | Measured verdict: single core sufficient; dual-core deferred (`firmware/README.md`) | Nothing required — recorded as a rejected/deferred option, not a gap | Keep the measurement; revisit only if the load matrix changes |

## Docs

| Item | Exists today | Missing | Definition of done |
|---|---|---|---|
| `pi/README.md` status | Status section says "W3 in flight, W4/W5 next" and "nothing here has been run on Pi hardware yet" | It contradicts the shipped song/arrange work and the studio-Pi run in `pi/app/NOTES.md`; the file is owned by the pi lane | Status rewritten from `pi/app/NOTES.md` |
| Acceptance evidence | `hardware-acceptance.json`, `docs/evidence/README.md`, `make gate` | All 26 items pending | Same as the hardware table above |

## Pi app — open items (from the inline-docs sweep)

Code / build hygiene:
- `pi/cmake/juce_compile_smoke.cpp` is dead JUCE scaffolding — delete it or
  restore the pin it once validated; it proves nothing today.
- `pi/clap/test/clap_stub/` is an ABI stand-in superseded by the fetched CLAP
  1.2.10 headers — delete, or keep explicitly as an offline fallback.
- `Qt6::Multimedia` is still in find_package/link but nothing uses it — drop
  the dependency.

Missing functionality:
- RENDER region edit op is refused at runtime ("until the core render call
  exists") while RENDER SONG works — the per-region render is the gap.
- Undo covers region ops; REDO is unbuilt (optional by design — revisit with
  the owner's touch-testing feedback).
- CLAP host: no automation recording (plugin→host events counted and
  dropped); no event loop for `request_callback`.
- `link` Linux spidev/ReadyGpio transports are unvalidated on Pi hardware.
- `update/` UF2/BOOTSEL core has not been run against a real Pico 2 in
  BOOTSEL mode.
- `bench/REPORT.md` numbers come from earlier captures; the `--bench-clock`
  full profile (~10 h) is unmeasured; the UMC1820 round trip is deferred.
- The 16-instance CLAP admission ceiling is a rule of thumb, not a measured
  CPU budget on the Pi.
- Session format: `monitor`/`send[]` deprecated spellings still round-trip;
  a v3 cleanup needs a migration path.
- `mixer_model.hpp` claims "the engine has no cue / out34 processing yet"
  but `pi/audio/engine.hpp` has both buses — verify the app path is wired
  end to end, then delete the stale comment.

## Firmware + midifx — open items (from the inline-docs sweep)

- Bench acceptance rows 1-10 (firmware/README.md) are unmeasured on hardware;
  rows 5/6/8/9/10 are host-verified only and 11-18 are simulated — every row
  needs the logic-analyzer capture (the bench runner drives it).
- Schema version is protocol-bound but not implemented: `0x21` STATUS_RESP
  carries no schema-version field; only the boot log prints
  `midi-fx-schema=v1`.
- ACK scope divergence (needs a decision): docs/protocol.md says a length-2
  ACK is returned for every accepted CONTROL_EVENT; the firmware
  (`frame_needs_ack()`) and the Pi emulator acknowledge only `0x15
  EV_MIDI_SEND` and `0x1A EV_MIDI_ROUTE_SEND`. Either narrow the doc or widen
  the code.
- `EV_CLOCK_ENABLE` (0x12) is a compatibility no-op (validated and ignored;
  the clock is always on) so older Pi builds keep working — retire it when
  the Pi side stops sending it.
- midifx pipe schema: a distinct "default for a newly added pipe" (separate
  from the missing-parameter default) would be a new schema field if wanted.

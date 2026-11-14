# TODO — missing functionality

This is the single list of work that is **not implemented or not yet verified**.
It is not a plan, a schedule or a promise. Each row records what exists in the
tree today, what is missing, and what closes it. When an item lands, delete the
row from the open tables and add one line to **Closed** below with its evidence
(a commit, a build result or an evidence file) - the row itself is what would go
stale.

Physical acceptance is machine-readable in `hardware-acceptance.json` and is
enforced by `make release`; the rows below restate those items so the whole
gap list is in one place. Evidence format: `docs/evidence/README.md`.

## Closed by the 10 mm panel respin (2026-11-13, branch `wip/10mm-panel`)

Recorded here so nobody re-opens them from an old note; the sources are
`tscircuit/lib/layout.ts` and the commits named.

| Item | What closed it |
|---|---|
| Panel-to-board distance | **10.0 mm** from the front board's top face, owner decision 2026-11-13, set by the ordered Thonkiconn jacks. `PANEL_TO_FRONT_BOARD_TOP`; commit `45ddd1c`. The 19.05 mm display-driven chain is superseded. |
| Actuator shortfall | `BUTTON_STACK.stackDepthExcess` is now **0.00 mm** against Omron's 10.0 ±0.4 reference: the ~9 mm cap/jack/encoder bushing gap is gone. Commit `45ddd1c`. The bought B32 cap top is still undimensioned; the printed cap now sets its own top by design (see the printed button cap fit row). |
| Jack bushing vs panel rear | The WQP419GR bushing face (~12.8 mm above the PCB) now reaches past the 8.4 mm panel rear, so the jacks can be nutted. Same commit. |
| Display mounting | The module left the front board: 106.50 × 68.90 opening, printed carrier on the **rear** board at `LCD_MOUNT_HOLES`, 8.75 mm adjustable spacers (12.75 mm plane separation). Commits `45ddd1c`, `f351b74`, `02a643b`. |
| Board sizes | Front **150 × 112**, rear **148 × 112** (`FRONT_PCB`/`REAR_PCB`); all four display boss points now land on the rear board. Commits `5423317`, `45ddd1c`. |
| Top relief notch | The separate 105 × 8 mm notch is gone: the display opening and the notch are ONE chamfered cut through the top edge (commit `e2adc4a`). |
| Power button and J_PWR placement | SW7 at (52, 49) with its silk legend to its right (`POWER`, `PRESS=ON`, `HOLD=OFF`, `SW7 THT` at x 67), J_PWR on the rear face at (64, 44.5) pointing +Y. Panel POWER opening and `POWER` legend followed. Commits `4ec13c5`, `9e87086`, `02a643b`. |
| MIDI jacks vs transport buttons | Jacks are one 12 mm-pitch block at x −62/−50/−38/−26; the transport row is centred under the opening at −46/−22/+2/+26 (24 mm pitch). Ergonomic owner decision: cables under SW1–SW4 fouled the buttons. Commits `cf26c42`, `e16bce9`. |
| MIDI cluster placement | in1 (−16,−48) in2 (2,−48) out1 (20,−46.1) out2 (38,−46.1), one origin per cluster (`MIDI_CLUSTERS`). Commits `1470ed1`, `dc6f065`, `4ec13c5`. |
| USB feedthroughs | Moved to x −52 / −23.5 / +11 at y 53.5; pitch set by the vendor flange screws. Commit `e16bce9`. |
| Panel standoff holes | Back as the four-point frame `PANEL_MOUNT_POINTS` at (±71.5, ±52), drilled in the panel (`STAND_1…4`) and the front board (`STANDOFF_1…4`); `PANEL_STANDOFF` carries the 8.40 mm nominal. They were removed in `e16bce9` and restored by the frame decision. |
| Front/rear spacer holes | Back on BOTH boards at (−69,−35) (71.5,−35) (−69,46) (71.5,46). Commit `02a643b`. |
| Rear-board silkscreen | LCD1–4 labels and four L-shaped register ticks on the TOP face, derived from `LCD_MOUNT_HOLES`. Commit `02a643b`. |
| U1/U2 and Q1/Q2 3D models | `cadModel` STEP/OBJ added for the TLP2361 (C107626) and AO3401A (C15127) imports, each verified against its own pad geometry before use (bounding boxes 7.000 × 3.837 × 2.469 mm and 2.506 × 2.904 × 1.373 mm, body on the board, no rotation offset). Present in the working tree, not yet committed. |
| Front-board silkscreen | Rebuilt for the U board: wordmark/OSHW/legends turned 90 deg in the left strip, pin-1 marks incl. U1/U2 and D1/D2 cathodes, `C1-C6: MLCC, NO POLARITY`. Commits `db34ce4`, `02a643b`. |
| toolchain | tscircuit 0.0.3169, CLI 0.1.2454 (commit `b9140b7`), four circuits build with 0 DRC errors (commit `02a643b`). |
| Routing measurement | `tscircuit/scripts/measure_routing.py` reports copper length, per-net length/vertices/layer changes, minimum copper-to-cutout approach, and the via inventory. Commits `1493ce6`, `03f3229`. |

## Hardware — respin follow-up (open, 2026-11-13)

These are the gaps the respin itself created or left behind. None of them is a
physical acceptance item yet; each has a concrete next action.

| Item | Exists today | Missing | Next action |
|---|---|---|---|
| Tests not re-derived | `tsci build` is green (four circuits, 0 DRC errors, commit `02a643b`). The geometry tests (`lib/layout.test.ts`, `lib/power-header.test.ts`, `lib/panel-cutouts.test.ts`, `lib/interconnect.test.ts`, `scripts/check_placement.ts`, the mechanical freshness check) still pin pre-respin centres, apertures, holes and cluster geometry. | `make test` is **red** and must not be claimed green. A review agent is auditing which assertions are stale versus which found a real defect. | Re-derive the affected assertions from `lib/layout.ts` (never the reverse), then run the full `make test` and record the result. Commit `6cee601` is explicitly "not green - do not merge". |
| `mechanical/layout.json` and the printed bracket are stale | `export-mechanical.ts` and `mechanical/feedthrough_bracket.py` read the canonical data, but the committed `layout.json` was last written at commit `6cee601`: it still holds feedthroughs at −38/−5/+35, the old spacer grid and the old `BRACKET_POWER_POCKET` (x −6.81…1.81). | Any STEP/STL mockup or bracket printed from it is the pre-respin part. The J_PWR pocket now derives to x 59.69…68.31, i.e. outside the bar (which ends at x 58), so the pocket as recorded no longer exists. | Run `bun run scripts/export-mechanical.ts`, regenerate the bar (slots follow `USB_FEEDTHROUGHS`), re-run the bracket self-check, and only then print. |
| Display carrier drawing and spacer verification | `DISPLAY_STANDOFF` (12.75 plane separation / 8.75 spacer) and the four Ø2.7 carrier holes on the rear board at `LCD_MOUNT_HOLES`; rear-board silk ticks and LCD1–4 labels. | No carrier outline, thickness, boss height or fastening is drawn; the 8.75 mm is a design value that has to trim the glass plane to the panel front; the module's boss pattern and glass thickness are unverified (`display_boss_pattern`). | Draw the carrier in the mechanical model; check it against the opening, the bracket bar and the rear-board parts; then measure a fitted sample (`display_boss_pattern`, `control_actuator`). |
| Light pipe length | `LIGHT_PIPE_REACH` records that the selected PGN25L165 (16.5 mm shaft) is **6.5 mm too long** for the 10.0 mm stack; ~9.2 mm is needed on an eyeballed 0.8 mm LED height. | No part selected for the new stack; the drawing states no cutting instruction and no proven FR-4 retention. | Select a ~9 mm pipe family (or get a documented cut process), then measure LED-to-pipe coupling and retention under `light_pipes`. |
| JLC CAM confirmation for the panel | Panel now emits 20 apertures and 14 drills (six feedthrough screws + four rail holes + four `STAND_` panel-mount screws). Openings include 12.25 mm routed squares, three Ø2.6 light-pipe holes (2.55–2.60 mm required) and a 107.22 × 68.87 rounded aperture. | No fabricator confirmation of the routed-square tolerance, the Ø2.6 hole band, the duplicate drill/routing feature (each circular cutout appears in both `drill_npth.drl` and `Edge_Cuts.gbr`) or the maskless copperless stack. | Put the built panel in the JLC viewer, confirm with JLC support, and record the answer under `light_pipes` / `scope_dfm_cpl`. |
| LCSC stock at order time | Every SMD line has a C-number; the encoder C143789 (PEC11R-4015F-S0024) is THT. No stock figure in this repo is current. | Stock/price for each C-number, and the THT/qualified-process parts, were last checked before the respin; the rejected alternatives (C143791 24-detent, 0 stock) are not substitutes. | Recheck every line in `tscircuit/scripts/sourcing-extras.csv` and the bench BOM at order time; do not infer stock from a catalogue C-number. |
| Panel-to-board fastening | **Decided and drilled**: four M3 standoffs at `PANEL_MOUNT_POINTS` (±71.5, ±52), Ø3.2 in both boards, 8.40 mm nominal, adjustable (`PANEL_STANDOFF`). They stop the panel rocking about the jack row; the jacks still set the panel plane. | The standoff body/thread, the fitted length and the jack-bushing tolerance behind the 8.40 mm are unmeasured, and nothing is bought. | Measure the board-top → panel-rear gap on the assembled stack, then choose the standoff (threaded, trimmed/shimmied) and record it under `case_fit`. |

## Hardware — physical acceptance (all 28 items pending)

`make gate` prints the current state; every item is `pending` until measured
evidence exists against the current hardware fingerprint.

| Item | Exists today | Missing | Definition of done |
|---|---|---|---|
| `zstack` | Provisional Z chain in `tscircuit/lib/layout.ts` (`PROVISIONAL_Z`: panel front 0, front-board top 10.0, front-board rear 11.6, rear-board front 22.8, rear-board rear 24.4; display 14.05 deep; carrier spacers 8.75 / plane separation 12.75) | Measured board, spacer, display, carrier, Pi, HAT, cooler and panel thicknesses; installed depth | Evidence file with each measured thickness and the installed depth |
| `control_actuator` | Candidates in `assembly.md`; panel openings from `layout.ts` | Knob, button caps and 5-way nav fitted and measured: shaft, detent, cap height, direction legends | Measurements and fitted-photo reference |
| `jack_retention` | XKB PJ-301M-3-B selected; 6.2 mm panel opening | Barrel/nut/panel fit, tip/ring/sleeve continuity, pull-out force on the sample | Continuity and retention results |
| `light_pipes` | Candidate **Signal Construct PGN25L165** (drawing SHA-256 `5350ad1f…`): shaft x 16.5 ±0.1, overall 17.5, head Ø3, mounting hole Ø2.6 −0.05; three Ø2.6 nominal panel openings 12 mm beside REC / OVER DUB / PLAY. Optical gap **null**. The part is **6.5 mm too long** for the 10.0 mm stack (`LIGHT_PIPE_REACH`) | A ~9.2 mm-class part (or a documented cut) and a measured seated plane, LED height, retention in 1.6 mm FR-4, the real hole tolerance, stack tolerance and optical coupling. The retired Bivar 10.16 mm / 7.29 mm subtraction is withdrawn and not replaced | Fitted length and LED-to-pipe distance measured; all three LEDs visible; evidence records the numbers |
| `display_dsi` | 4.3-inch DSI module selected; 120 mm cable is the design pick; route contract in `mechanical-stack.md` | Module mounted, cable routed, bend radius and latch checked, underside clearance recorded | Measurements per the route contract |
| `display_boss_pattern` | `DISPLAY_BOSS_PATTERN` (99.30 × 61.80, SMTSO-M2.5) is the fitted pattern in `layout.ts`, carried on the rear board as `LCD_MOUNT_HOLES` | Physical confirmation that the bosses take the spacer screws and the 58 × 49.04 holes do not; the 12.75 mm plane separation / 8.75 mm spacer measured from the rear board's front face | Two direct measurements |
| `display_fpc_idc` | FPC connector recorded as planning data at (38.84, 11.7); J5/J_FRONT now at (66, 20) rotation 90, right of the opening; route contract in `mechanical-stack.md` | FPC plug/latch/bend coexist with the carrier, the rear-board parts and the J5 female solder tails (no interconnect plug or ribbon exists) | Route and clearances recorded |
| `interconnect_mate` | Direct 2 × 13 pair: J5 female SFH11 on the front board's bottom face, J_FRONT male SBH11 on the rear board's top face; 1.00 mm drill (male-recommended, provisional for the female); geometry proven on the built JSON by `lib/interconnect.test.ts` | Fitted pair: 11.20 ±0.20 mm seat-to-seat gap, support, keying and all-26 pre-power continuity | Measurements and 26 continuity records; hardware fit unverified |
| `module_label_sku_read` | Selected part: Waveshare 4.3inch DSI LCD, ElectroKit 4103 5687 | Read the label on the delivered module and match it | Label/SKU recorded |
| `touch_window_alignment` | 107.22 × 68.87 mm window; active area 95.04 × 53.856 mm | Touch mapping checked with the display seated | Corner/edge alignment recorded |
| `pi_microsd` | Pi held on its 20HP mounting panel on the 58 × 49 pattern, beside the main module | microSD insert/remove with the Pi in the stack and case closed | Clearance recorded |
| `cooler_hat_ssd` | Full-size M.2 HAT+ and Active Cooler are candidates | Stack height, airflow and connector clearance measured | Measurements recorded |
| `case_fit` | 30HP/3U panel (152.0 mm), 148 mm rear board, rail holes ±68.58 / ±61.25 (spacing 137.16 = 27 × 5.08); main module 45.5 mm behind the panel back on paper | Every rail and locating feature fitted in the case; the panel-to-board fastening is not decided yet | Installed-depth and fit record |
| `usb_c_5a_path` | CAB-15455 is 3 A and rejected for the 5 A path | An exact 5 A rated panel feedthrough from a primary source | Part, rating and source recorded |
| `usb_pd_droop_thermal` | 27 W Pi PSU is a candidate only | Full-load PD negotiation, droop and temperature in both orientations | Measurements in both orientations |
| `encoder_solder_process` | PEC11R-4015F-S0024 (C143789), THT; bench technique in `assembly.md`; Bourns does not recommend hand soldering | Selective/wave or validated manual process with profile and inspection | Process record per `encoder_solder_process` |
| `midi_threshold` | TLP2361 receiver, 220 Ω input; SPICE only | Measured threshold against the CA-033 < 5 mA requirement | Measurement at recorded supply/temperature |
| `midi_receiver_high_current_thermal` | 2–6 mA recommended, 10 mA absolute at 25 °C derated −0.13 mA/°C above 110 °C (8.05 mA at 125 °C). The 3.3 V model corner (8.18 mA) is above that 125 °C limit, but the comparison is illustrative (model current from a 2 mA VF minimum and an assumed sender drop): no safe ambient is derived | Actual input current and junction temperature at worst-case high-rail/low-VF corners, with the recorded ambient | Measurements and 220 Ω justification |
| `midi_out_legacy` | AO3401A high-side output stage; SPICE current checks | Legacy receiver loads measured: low-rail current, rise/fall < 2 µs, fault temperatures; independent topology review | Measurements plus review record |
| `midi_interop` | Host parser/route tests only | Two real MIDI devices exchanging note, clock and sysex both directions | Equipment list and traffic log |
| `midi_current_faults` | Protection parts in the netlist | Short, open, reverse and over-current cases on both inputs and outputs | Fault matrix results |
| `scope_spi` | SPI0 mode 0, 1 MHz, IRQ-driven 32-byte frames; host reliability tests | Scope/logic-analyser capture incl. Pico-USB and Pi-off cases | Capture at the target rate |
| `scope_power_sequences` | Power branch designed; SPICE backfeed checks | Power-up/down order for Pi, Pico, display and 3.3 V rails | Capture with no backfeed or latch-up |
| `scope_thermal` | None | Closed-rack soak at recorded ambient/load; cooler noise; NVMe temperature | Soak record |
| `probe_point_access` | 15 bare plated through-holes TP1–TP15 on the rear board (`lib/probe-points.ts`), geometry and nets checked by `check_electrical.py`; no probe access measured | Access with the stack assembled, per pad and per assembly state; in-rack access is not assumed | Recorded assembly state, pad, side and obstruction for each point |
| `abrupt_power_loss` | Nothing measured; firmware has no power-fail write guard (nothing claimed) | Abrupt power removal/re-application on the full stack, with rail decay and storage write integrity recorded | Scope capture of the rails plus post-loss write-integrity result |
| `scope_dfm_cpl` | `fab_check.py` compares geometry with published limits; six `tsci check` reports pinned | JLCPCB DFM/order-preview review of copper, mask, silk, drill and CPL rotation on prototype material | Review findings recorded |
| `autoroute_upstream_warning` | Known tscircuit line "Could not save autorouting paths ... unique PCB port" appears on both boards; printed as a note by `check_reports.ts` | Resolve upstream or review its effect on the routed copper | Resolution or recorded copper review |
| `first_article` | None | First-article boards built, inspected and tested against the full list | First-article report |

## Hardware — design gaps (not acceptance items)

| Item | Exists today | Missing | Definition of done |
|---|---|---|---|
| NVMe HAT vs the GPIO ribbon | The harness plan needs the Pi's 40-pin header for the ribbon to `J_PI`, and the Pi panel depth chain assumes the Active Cooler + M.2 HAT+ + SSD stack (owner decision 2026-11-12) | The fitted M.2 HAT+'s MPN and its connection method are not chosen: an HAT that occupies the GPIO header leaves the ribbon nowhere to land (the Pi 5's PCIe FPC is the alternative route for NVMe). No pass-through/stacking hardware is selected | A chosen HAT MPN with its primary-source drawing showing the GPIO header free (or a qualified pass-through/stacking arrangement), recorded in decisions.md and in the `pi_panel_harness` evidence |
| Inter-module harness cables | Six planning runs between the two modules (3 USB feedthrough cables, DSI FPC, 40-wire ribbon, J_PWR pigtail); the kit DSI cables are 50/120 mm and likely too short | No cable set is purchased or measured: the DSI extension's length, contact orientation (A/A vs A/B), 15-to-22-pin adaptation, touch function and signal integrity over the fitted length are unverified; the USB-C power feed must be 5 A-rated end to end; the ribbon length and the pigtail length are planning values | Cable MPNs and measured lengths recorded, DSI picture + touch demonstrated on the fitted run, 5 V droop measured at Pi load, and the bundle strain-relieved through the bracket slots (`pi_panel_harness`) |
| Pi 5 pin-1 row | The Pi drawing gives the header and hole positions but not which row carries pin 1. In the layout frame `J_PI` pin 1 is on its -Y row, so a ribbon could land rotated | Confirm the Pi 5 pin-1 row from the Raspberry Pi pinout documentation and the mounting-panel orientation | Row mapping recorded under `pi_panel_harness` |
| Pi 5 drawing: unidentified Ø3 feature | The drawing marks a Ø3 mm feature at about (61.5, 46.5) mm from the board's bottom-left corner. The Pi panel does not use it | Identify it from Raspberry Pi documentation; do not use it as a mount unless confirmed | Identified and recorded |
| Pi 5 DSI and PCIe connector positions | The drawing does not dimension the DSI, CSI or PCIe connectors, so the Pi-side ends of the DSI and PCIe runs are not located | Take the positions from Raspberry Pi documentation before routing the DSI harness | Positions recorded under `pi_panel_harness` |
| Module placement in the row | The Pi module's x-position in the row is not decided. `J_PI` stays at x = 0 on the rear board, so the ribbon's lateral route is unset | Choose the module position, then route the ribbon and record its length | Placement and ribbon length recorded in decisions.md |
| Front-board 3D bodies | `J1`–`J4` carry the pinned Clacktronics PJ366ST model (unverified against a delivered jack, see the jack row above); `DISP1` is a translucent envelope from `DISPLAY_STACK`; `J5` has no `cadModel`, so tscircuit draws a default box. The SFH11 body is documented at 8.5 mm | Source 3D models with provenance under `library/cad`, or mark them as placeholders in renders | Models attached and heights match the datasheets |
| MIDI jack footprint and 3D model | **Partly closed 2026-11-13.** `tscircuit/lib/jack.tsx` now uses the preferred part (QingPu/Thonkiconn **WQP419GR**, M6 x 0.5) with the pads labelled by function **S / R / T** and connected by letter in `front.circuit.tsx`; a STEP model is attached (`cadModel`, pinned Clacktronics commit) and `library/cad/jack-35-wqp419gr/` holds the KiCad footprint, the STEP and a `SOURCE.md` with the MIT licence and SHA-256. | The vendor drawing numbers terminals 1 = sleeve, 2 = tip, 3 = ring while the pads are physically S 6.48 / R 3.38 / T -4.92 mm from the barrel datum (verified from clacktronics/AudioJacks and agreeing with Thonk's PJ301M-12 drawing), so numbering them 1/2/3 invited a tip/sleeve swap. Still open: (a) the Clacktronics library self-describes as "not yet complete" with Testing Status "Untested", so the model and footprint are unproven against a physical jack; (b) the drawing's 3 mm barrel keepout is not cut (jack sits near the board edge); (c) `mass_erase`-free SWD rules aside, nothing about the jack has been measured; (d) froemix keeps the old pin1/2/3 labels because that board is at fab; (e) **closed 2026-11-13 from the vendor drawing**: the board cut a 1.2 x 2.0 mm oval, but Thonk's own "Thonkiconn Stereo (WQP419GR)" PCB layout sizes the terminal blades 0.5 x 1.5 (ring pad), 0.6 x 1.5 and 0.6 x 1.3 mm, and a 1.5 mm blade cannot enter a 1.2 mm hole. `FP_JACK_TRS` now cuts 1.6 x 0.6 slots with a 2.6 x 2.6 land (sleeve 3.1 x 2.1), the same table as the library CAD; the built drill shows 12 jack slots beside the 2 encoder lugs. Still unmeasured: whether the printed slot accepts the ordered part's legs (the drawing gives the blade, not its tolerance) - buzz a delivered jack; (e2) **closed 2026-11-13 by the 10 mm panel**: the jack body is 8.3 mm and the M6 x 0.5 bushing thread 4.5 mm, so the bushing face reaches about 12.8 mm above the board while the panel's rear face is now 8.4 mm above it, so the nut lands; (f) the model, footprint and pad line are transcribed from MIT CAD whose own README marks the parts untested - upstream `main` is the same commit as the pin (14a88866), so there is no drift, but there is also no vendor drawing. | A physical jack fitted and buzzed out (barrel to S = 0 ohm, plug tip to T, plug ring to R) with the measurement recorded, then either confirm the model or correct it; decide the 3 mm keepout against the board outline; re-run `bun run gate` |
| Pico 2 SWD access | Pad order documented from the Pico 2 datasheet (SWCLK, GND, SWDIO on the module's lower edge, Figure 2, page 5); the debug pads are separate from the 40-pin sockets | No access implemented and none claimed: no wire gauge, insulation, strain relief, connector, probe ground reference or procedure has been selected, so a safe hand-wired link to the module's debug pads is unproven. No anchor pads were added, because isolated copper is not access | A qualified procedure (materials, strain relief, ground reference, polarity checks) with a measured first connection, or an explicit decision not to provide SWD |
| Display in-plane orientation axis | `step_geometry.DISPLAY_ORIENTATION` records both candidates (180° about Y and about X); the model is drawn with the Y candidate and no test or document claims either axis proven | The drawing shows readable rear-side silkscreen and may be a rear view, so matching `DISPLAY_STANDOFFS` is consistency with the layout's own no-flip premise, not independent proof. The outer SMT pattern is symmetric | A real front/rear **labelled photograph** of the delivered module, or the physical part offered to the front board; then record the axis and delete this row |
| Printed button cap fit | Every B3F-4055 (SW1–SW7) takes the **printed cap** `mechanical/printed/button-cap.stl`, 7 off (standard); the bought B32-1380 is the documented alternative. The panel opening is 12.25 mm square. The cap is an 11.80 mm plate, 3.85 × 3.85 × 0.60 mm locator, 4.10 mm tall, top 0.80 mm proud; `BUTTON_CAP_PRINTED`, `BUTTON_REACH` and the printed-part tests carry the geometry | The 0.025 mm/side locator fit, axial retention (no snap or skirt), the 0.60 mm locator ring's clearance to the switch body top (body height not recorded) and the routed-square tolerance at JLCPCB are unverified; the installed cap height and press travel are unmeasured | A first article of 7 caps: plate/cutout fit, plunger fit and retention, installed cap height and press travel, body-top clearance (`control_actuator`) |
| PGN25 hole fabrication and the duplicate drill/routing feature | The panel carries three nominal Ø2.6 mm holes at the unchanged LED axes; the CAD and tests assert that nominal geometry only | JLCPCB sources conflict on NPTH tolerance (±0.05 vs ±0.08, and +0.13/−0.08 for through holes with no NPTH split), and the exporter writes each circular cutout into BOTH `drill_npth.drl` and `Edge_Cuts.gbr` with CAM de-duplication unverified. The drawing wants 2.55–2.60 mm | JLC viewer and support confirm how the duplicate is processed and whether 2.55–2.60 mm can be held; then the result is recorded under `light_pipes` with the measured holes |
| Board face convention (owner decision) | Requested: both boards' CAD-bottom faces toward the panel. Not implemented; no board source, SMT side or stackup changed | The bare board models are plain slabs with no top/bottom marker. The front/UI board carries SMT parts and control actuators on top; the rear/base board already has SMT parts on the bottom. Turning either over is a real PCB layer/side reassignment | An owner decision on the board face convention; if it changes, a PCB revision and a CAD model follow. No compliance is claimed until then |
| `power_header_mount` | J_PWR is the Harwin M20-9960245 right-angle 1 x 2 header on the front board's REAR face at `POWER_HEADER` **(64, 44.5)**, rotation 180, mating axis +Y - it moved with the POWER button on 2026-11-13 and no longer sits in a top relief notch (there is none). `POWER_HEADER_KEEPOUT` covers both readings of the 4.80 mm datum; the owner approved (2026-11-10) a pocket in the printed bracket and a provisional generic 2.54 mm DuPont-style plug. Sourcing is **UNVERIFIED** (digikey.se Cloudflare 403; no SKU or stock claimed); the Electrokit breakaway strip was proposed, not selected. The Pi end is two bare pads (`J2`), so a fitted header or a qualified wired connection is needed. **The bracket pocket is obsolete**: `BRACKET_POWER_POCKET` now derives to x 59.69...68.31, outside the bar (ends x 58). | Measured housing offset (the 4.80 mm datum), measured plug housing and cable bend, access for soldering/plugging in the right leg, bracket/bar regeneration with any pocket re-derived, switch continuity to the Pi PWR pads, and a PCB source check | Open: (1) the 4.80 mm datum is unresolved (sheet 1 of 2 missing); (2) the pad (1.60 mm) and plug reach (8.00 mm) are engineering assumptions; (3) the plug is a generic DuPont-style housing; (4) the bracket must be re-derived after the move; (5) stock/SKU unverified. Do not claim a fit |
| MIDI external mispatch (±12 V) | The SPICE/analytical checks model the board rail only: a +-12 V source driven INTO a MIDI input or output (wrong cable, phantom power, a miswired interface) is not modelled. No survival and no exact fault current is claimed | Owner chooses a voltage/fault-survival requirement, then a qualified (possibly destructive) test is designed | Covered by acceptance item `midi_current_faults`; do not design or run the destructive test before the requirement exists |
| ESD and EMC | No ESD/EMC design, test or compliance work exists: the boards carry no TVS array or common-mode choke beyond the MIDI parts, and nothing was tested | Commercial-product EMC/ESD compliance is NOT established: no standards claim may be made | Owner decision needed before any EMC/ESD claim or test plan |
| Pi J2 power-button connection | Pi 5 J2 is two bare pads (Raspberry Pi power-button documentation and `images/j2.jpg`, retrieved 2026-11-10); the J_PWR side takes the owner's F-F DuPont pair | A correctly fitted header or a qualified wired connection at the Pi end, with pitch, part, clearance, insulation, strain relief and assembly method recorded; dry-contact only | Covered by acceptance item `power_header_mount`; no JST-SH or pad pitch may be assumed |
| Probe-point side access | 15 plated through-holes on both copper layers, outside the documented Pico/Pi/connector envelopes | The pad is reachable from both faces in principle; no probe has been tried on a built board. The rear face carries the parts, the front face faces the front board | Covered by acceptance item `probe_point_access`; delete this row when that evidence exists |

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
| Acceptance evidence | `hardware-acceptance.json`, `docs/evidence/README.md`, `make gate` | All 28 items pending | Same as the hardware table above |

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

## Queued: panel stack becomes 10.6 mm (owner decision, 2026-11-14)

The Thonkiconn jacks are the anchor, not a design target: they are exactly
9.0 mm from the front board's top face to the shoulder the panel rear rests
on, and the panel is 1.6 mm, so

    board top -> panel REAR    9.00   (jack-seated)
    panel thickness          + 1.60
                            --------
    board top -> panel FRONT  10.60

The code currently has this backwards: it treats 10.0 as the board-to-panel-
front reference and derives the rear gap as 8.40 (10.0 - 1.6). Change the
anchor and let everything chain from it:

- `JACK_CLAMPED_GAP` 8.40 -> **9.00**, and define it from the Thonkiconn
  seating face rather than as `PANEL_TO_FRONT_BOARD_TOP - PANEL.thickness`
  (that subtraction is what made the jack a consequence instead of a cause).
- `PANEL_TO_FRONT_BOARD_TOP` 10.0 -> **10.60** = the gap plus the panel.
- Every value derived from the chain moves with it, re-derived and NOT typed:
  - printed B3F cap 4.10 -> **4.70** tall, top 10.80 -> **11.40** above board
  - `BUTTON_REACH`: switch-to-panel-rear 1.10 -> **1.70**, to-front 2.70 ->
    **3.30** (the reach assertion thresholds follow; do not relax them)
  - four new panel mounts: standoff 8.40 -> **9.00**
  - display chain: PCB rear 10.05 -> **10.65**, rear-most 14.05 -> **14.65**
  - rear board 22.80 -> **23.40** front, 24.40 -> **25.00** rear
  - interconnect seat-to-seat stays 11.20 +- 0.20; the spacers remain the
    adjustment, so this change must not be absorbed there
- Re-pin every pinned test value (precedent 9dd96dd re-derived 25 python
  pins), regenerate `mechanical/layout.json`, then `make mechanical`,
  `make mechanical-steps`, `make order`, and refresh the viewer screenshot.
- Documentation to update with the new chain: `docs/mechanical-stack.md`,
  `docs/assembly.md`, `docs/decisions.md`, `docs/panel.md`, `README.md`, and
  `mechanical/printed/README.md` (cap height and first-article checks).

ASSUMPTION TO CONFIRM: the 9.0 mm is the board top to the shoulder the panel
REAR sits on, i.e. it excludes the jack nut and washer. If Thonkiconn's drawing
gives 9.0 as an overall body height and the panel seats on the nut instead, the
effective gap is 9.0 minus nut/washer thickness, and the whole chain shifts by
that amount - confirm before cutting a panel.

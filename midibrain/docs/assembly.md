# Candidate components and assembly

**Prototype instructions, not a purchasing or production release.** Use the
canonical tscircuit source, `jlc-order.json`, the strict
`tscircuit/scripts/sourcing-extras.csv`, and the reviewed BOM/CPL together.
Recheck exact MPN, package, supplier and stock before ordering. All physical
acceptance items remain pending.

## Board split and assembly sides

| Board | Contents | Assembly |
| --- | --- | --- |
| Front/UI, 128 × 112 mm | MIDI inputs/outputs, six transport/shift switches, three LEDs, NAV, encoder and the J5 female interconnect | SMT on top/panel side. The J5 THT female socket body faces rearward (it is the rear-facing half of the direct 2 × 13 mate); other THT controls face the panel. |
| Rear/base, 101.6 × 112 mm | Pico sockets, the Pi GPIO header (now the harness socket), the J_FRONT male interconnect, SPI buffers, reset and power branch | SMT on bottom/rear side; THT headers/sockets fitted separately. J_FRONT is the only body on the front-facing top side. The Pi hole grid stays drilled as a retained option; the Pi itself mounts on the panel below. |
| Pi mounting panel, 101.2 × 128.5 mm (20HP/3U) | Holds the Pi 5 + Active Cooler + M.2 HAT+ + NVMe SSD on its REAR face | Bare FR-4, no parts. Bench: M2.5 standoffs (candidate 3.5 mm) through the 58 × 49 grid, then the six harness runs to the main module (see below). |
| Panel, 30HP/3U (152.0 × 128.5 mm) — button opening **UNRESOLVED** (provisional Ø12.4 mm, unsourced; see docs/mechanical-stack.md) | FR-4 1.6 mm, white legends, cutouts/light-pipe holes | No components or copper; no copper finish is required. Confirm no-solder-mask fabrication with JLCPCB. |

The encoder is **not** an unconditional hand-solder line item: the selected
THT part has no catalog SMD detentless 24 PPR equivalent, and Bourns does not
recommend hand soldering it. See its process requirement below. All THT parts
stay out of the SMT CPL.

## SMD candidates

| Function | MPN / value | LCSC | Package |
| --- | --- | --- | --- |
| MIDI receiver | Toshiba TLP2361(TPL,E) | C107626 | 11-4L1S |
| MIDI output switch | AO3401A P-channel MOSFET | C15127 | SOT-23; pad 1 gate, 2 source, 3 drain |
| MIDI LED reverse diode | 1N4148WS | C2128 | SOD-323 |
| MISO/READY buffer | SN74LVC1G126DBVR | C7834 | SOT-23-5 |
| Reset FET | JCET 2N7002 | C8545 | SOT-23; pins 1/2/3 = G/S/D |
| Pi 5 V → Pico VSYS diode | B5819W | C8598 | SOD-123 |
| SPI series | 100 Ω, 0603WAF1000T5E | C22775 | 0603 |
| MIDI input | 220 Ω, 0603WAF2200T5E | C22962 | 0603 |
| RX pull-up | 2.2 kΩ, 0603WAF2201T5E | C4190 | 0603 |
| Pull-up/gate | 10 kΩ, 0603WAF1002T5E | C25804 | 0603 |
| Pull-down | 100 kΩ, 0603WAF1003T5E | C25803 | 0603 |
| Indicator series | 330 Ω, 0603WAF3300T5E | C23138 | 0603 |
| MIDI output ring series | 33 Ω, 25121WF330JT4E, 1 W | C53728 | 2512 |
| MIDI output tip series | 22 Ω, 25121WF220JT4E, 1 W | C118189 | 2512 |
| Bypass | 100 nF, CC0603KRX7R9BB104 | C14663 | 0603 |
| Bulk | 10 µF, CL31A106KBHNNNE | C13585 | 1206 |
| REC / OVER DUB LEDs | KT-0805R | C2295 | 0805 |
| PLAY LED | KT-0805G | C2297 | 0805 |
| NAV | Alps SKRHABE010 | C139794 | SMD 8-pad |

TLP2361 is inverting **totem-pole**, unlike the old TLP2362 open-collector
receiver. The retained pull-up and 100 nF local bypass are intentional. Its
2–6 mA recommended input-current range and high-current/temperature corners
still need qualification; do not reduce the 220 Ω input resistor casually.

## THT, modules and loose hardware

| Item | Candidate / requirement |
| --- | --- |
| J1–J4 | XKB PJ-301M-3-B, C381127. Vertical TRS; 6.2 mm panel holes. Verify actual tip/ring/sleeve and bushing/nut retention. |
| SW1–SW7 | Omron B3F-4055, C84931 (same part as before; **no PCB or part change**). SW5 is PLAY START, SW6 is SHIFT/EXIT, SW7 is POWER. **Sourcing exception (owner, 2026-11-09, SOURCING ONLY):** DigiKey SW414-ND is allowed for these switches and for compatible B32-13xx caps; SMT stays on JLCPCB and every other non-SMT part targets Conrad. Caps: B32-13xx are compatible, **no colours chosen**. The B32 nominal seated height (10 mm from the PCB) is unverified and does not bridge the provisional 19.05 mm board near face to the panel front. Through-panel actuators, extension, cap retention, the button cutouts and `control_actuator` remain pending. |
| U6 / encoder | Bourns PEC11R-4015F-S0024, C143789. Detentless (0 detents), 24 pulses/rev, push switch, THT with two anchor lugs. **THT fallback:** the 2026-11-06 JLC/LCSC search found no SMD encoder with 0 detents, 24+ PPR and through-hole anchor lugs (the SMD PEC11S/PEC11J/EC11J families are 30 detents/15 pulses), and a lug-less SMD knob encoder is not accepted. Request JLCPCB THT/hand-solder assembly for U6 if offered; otherwise use the bench technique below. Bourns does not recommend hand soldering PEC11R, so the process is qualified rather than assumed. |
| J_PI | XFCN BH254V-40P (C48687632), alternating pad numbering. The 40-pin GPIO ribbon is still a cable (sourcing row `CAB_GPIO`). |
| J5 / J_FRONT (direct 2 × 13 mate) | **J5 female** Sullins SFH11-PBPC-D13-ST-BK on the front board's bottom face; **J_FRONT male** Sullins SBH11-PBPC-D13-ST-BK on the rear board's top face opposite the Pi. There is **no plug and no ribbon**: the socket plugs straight onto the header. Both are THT hand-solder parts with no LCSC number. Drawings: Sullins 11091 rev J, https://drawings-pdf.s3.amazonaws.com/11091.pdf, retrieved 2026-11-10, SHA-256 `f35a7148d89c27bea77b50ed43b2399b28ff5d88fbd4205b5d2130f878557c20`; sheet 1 recommends a Ø1.00 mm drill for the male and quotes a .025[0.64] square pin, .118[3.00] tail, .346[8.80]/.358[9.10] height and, for 13 positions per row, A 30.48 / B 40.52 / C 38.32 mm; sheet 2 shows the straight SFH11 mating it with .441[11.20] across the opposed PCB seating faces. Catalog pages 122/123 (SHA-256 `44e4b4485b961a87cc23a190224b282764adff2a05daffd38d4bab94a84ebf77`) list 2 × 13 variants and the centred key/notch. **The female's PCB hole is not published**: Ø1.00 mm is our provisional assumption for it (its quoted tail is 0.40 × 0.60 mm). **The pages read do not print the pin numbering, and the key's side relative to pin 1 is not verifiable from them**: the boards put the same pin number on the same physical pad and draw both key/notch marks on the same physical long side (checked on the built output), and the pre-power all-26 continuity check plus a physical key fit confirm the purchased pair. DigiKey is a **source lead** (checked 2026-11-10), not an approved substitution; the owner's DigiKey exception covers switches and caps only, and Conrad is the target for non-SMT parts. Never fit a ribbon or a second keyed variant without re-checking the mate. |
| Pico sockets | Two removable 1×20, 2.54 mm sockets. Exact procurement and installed height must match the module and first article. |
| TP1–TP15 (probe points) | **Fifteen bare plated through-holes on the rear board: no part, nothing to fit or buy.** They are declared `doNotPlace`, so they have no BOM and no CPL line; do not fill them with a header or a wire. Each carries its net name on the silkscreen (`5V`, `3V3`, `PI3V3`, `RUN`, `SCLK`, `CS`, `MOSI`, `MISO`, `READY`, `GND`, `TX1`, `RX1`, `TX2`, `RX2`, `GND`) and its `TP` reference. Their use and the unverified physical access are described in `ELECTRICAL-VERIFICATION.md` and acceptance item `probe_point_access`. |
| Display | Waveshare 4.3inch DSI LCD, ElectroKit 4103 5687, with the kit's 120 mm 15-pin-to-Pi-5-22-pin DSI cable (50 mm is too short for the route contract; the 160 mm cable is 15-pin-to-15-pin and does not fit DSI1). |
| Display spacers | Provisional **5 mm beyond the 4 mm boss ends**, giving 9 mm PCB-plane separation. Do not buy a 9 mm spacer from the plane-gap number. Exact parts and fit remain pending. |
| Panel/front spacers | Length/MPN pending. The current flush-glass target gives a 17.45 mm panel-rear-to-front-PCB-top gap, not a measured assembly. |
| Front/rear spacers | The direct 2 × 13 pair fixes the gap: **11.20 ±0.20 mm seat-to-seat** (drawing 11091 sheet 2). Buy the spacer only after the gap is measured on the built pair; support, flatness and the fitted length are unverified, and a nominal drawing value is not a measured gap. |
| Pi spacers | **Candidate 3.5 mm only, not a proven minimum and not a safety claim.** Since 2026-11-12 the Pi sits on the 20HP mounting panel, so this spacer covers the retained rear-board Pi option only; the mounting panel's own standoffs (panel rear face to the Pi underside) use the same 3.5 mm candidate and are measured under `pi_panel_harness`. The male J_FRONT tail is 3.00 mm from the connector seating face, i.e. a nominal 1.40 mm protrusion beyond a 1.60 mm rear PCB, over the Pi PCB area. The solder fillet height, the PCB tolerance, the Pi's own connector/cooler stack, the microSD clearance and service access are all unverified. |
| J_PWR (panel power header) | **Owner requirement 2026-11-10: right-angle THT 1 x 2 header on the front board's REAR/bottom face at the middle of the top relief notch (x ≈ −2.5 mm), pads on board material below y = 48 mm, pins pointing outward into the notch, so the mating plug lives in the notch void instead of on the board face.** Candidate: Harwin **M20-9960245**, "2.54 mm pitch SIL horizontal PC tail pin header assembly", through-hole 2-way right angle. Research verified the part as **ACTIVE** on the primary page https://www.harwin.com/products/M20-9960245/ and the drawing `/tmp/M20-996-drawing.pdf`, SHA-256 `3cd367d6b76e7c6be501578c87a4a9c89721e36a4a8bc36d5e8340646bdcd61b`, retrieved 2026-11-10: recommended PCB hole **Ø1.02 ±0.05 mm**, 2.54 mm pitch, 0.64 ±0.02 mm square pin, 6.10 mm mating length. **Placed** on 2026-11-10: the independent architect reading fixed the orientation — the long 6.10 mm mating pins run parallel to the PCB and the short 3.00 mm PC tails perpendicular to it, a true right angle — so the footprint exists at `POWER_HEADER` (x −2.5 mm, y 47.00 mm = the notch 48 mm less the 1.60 mm pad radius and the 0.20 mm edge rule, rear face, rotation 180°, mating axis global +Y). **The exact hole-to-housing offset is still unresolved** (the drawing's 4.80 mm datum is unclear and sheet 1 of 2 is missing): `POWER_HEADER_KEEPOUT` covers both plausible readings of 4.80 mm — as a height above the PCB and as a forward extent along the board — plus a 2.54 mm rear wall and an 8.00 mm generic-plug planning reach. Engineering assumptions, gated and not measured: the 1.60 mm pad (drill is sourced), the housing envelope, the plug. The bracket carries the owner-approved through pocket `BRACKET_POWER_POCKET` (x −6.81…1.81 mm, y 43.96…56 mm) that clears that keepout and keeps 1.96 mm below and 2.14/7.14 mm beside it. Sourcing is **UNVERIFIED**: digikey.se returned Cloudflare HTTP 403 on 2026-11-10; the block is not bypassed and no SKU or stock is claimed. An Electrokit breakaway strip was proposed by the researcher, **not selected by the owner** — do not substitute it. The mating plug is the owner-approved **generic DuPont-style housing**, not a measured size. **Owner approved 2026-11-10: a pocket in the printed bracket, and a provisional generic 2.54 mm DuPont-style female plug** (its housing, cable bend and clearance are unmeasured). **Implemented on 2026-11-10**: the footprint `tscircuit/imports/M20_9960245.tsx` from `POWER_HEADER`, the keepout `POWER_HEADER_KEEPOUT`, and the through pocket `BRACKET_POWER_POCKET` (x −6.81…1.81 mm, y 43.96…56 mm) cut by `mechanical/feedthrough_bracket.py`; its CAD self-check reports one connected solid, no material inside the declared keepout, and 1.96 mm of bar below the pocket. The UMC AUDIO feedthrough's cable tail shares that slot, so its access and the plug envelope stay **unverified**, as do the housing offset and the printed pocket fit. The switch-pair nets `PWR_SW_A`/`PWR_SW_B` stay isolated from the 2 × 13 interconnect, ground and both 3.3 V rails (`lib/mounting-sides.test.ts`); `lib/power-header.test.ts` checks the built board and the pocket. **Pi end:** the Raspberry Pi power-button documentation (https://raw.githubusercontent.com/raspberrypi/documentation/master/documentation/asciidoc/computers/raspberry-pi/power-button.adoc, retrieved 2026-11-10) and its official `images/j2.jpg` show J2 as **two bare pads** between the RTC battery connector and the board edge, so the owner's F-F DuPont jumper connects at the J_PWR side only. The Pi end needs a correctly fitted header or a qualified wired connection; its pitch, part, clearance, insulation and assembly method are pending, no JST-SH or pad pitch is assumed, and the wiring must carry the dry contact pair only. Acceptance item `power_header_mount` holds it open. |
| Light pipes | Owner-selected **candidate** Signal Construct PGN25L165 (shaft x 16.5 ±0.1, overall 17.5, head Ø3, mounting hole Ø2.6 −0.05); three Ø2.6 nominal panel holes. Not a fit: the seated plane, LED height, retention in 1.6 mm FR-4, hole tolerance and stack tolerances are unmeasured. The retired Bivar PLP2-400-C 10.16 mm reference and the 7.29 mm subtraction are withdrawn. No purchase before the `light_pipes` acceptance item. The 2.55-2.60 mm hole is NOT guaranteed: JLCPCB's NPTH bands conflict (+-0.05 / +-0.08, plus +0.13/-0.08 for through holes with no NPTH split) and the panel emits each circular cutout in both the NPTH drill file and Edge_Cuts; JLC viewer and support must confirm the de-duplication and the achievable hole size. |
| Knob / button / NAV caps | Exact parts or printed actuators pending shaft, detent, travel and reach checks. |
| Bracket | PETG model generated from `mechanical/layout.json`: 121 × 14.5 × 3 mm, with slots 15.6/17.9/17.9 mm. CAD checks pass; printed fit, fasteners and cable retention do not yet have physical acceptance. |
| Bracket fasteners | Two M2.5 screw/nut sets and four cable ties; verify length, head/washer envelope and printed-hole fit. |
| Rack screws | Four M3 screws for the selected case/rail profile. |

See [mechanical-stack.md](mechanical-stack.md) for the display datums, the Z chain and the remaining physical checks. The panel aperture is 107.22 × 68.87 mm with explicit corner relief; it is still a provisional tolerance design.

## U6 encoder: THT fallback and bench technique

U6 is the Bourns PEC11R-4015F-S0024 (C143789): 0 detents, 24 pulses per 360°,
push switch, THT signal pins and two THT anchor lugs. SMT panel controls are
acceptable in principle - the NAV switch is SMT and takes repeated presses -
but the knob applies torque. The 2026-11-06 JLC/LCSC catalog search found no SMD
encoder with 0 detents, 24+ pulses/rev and through-hole anchor lugs: the SMD
PEC11S, PEC11J and EC11J families are 30 detents/15 pulses, the PEC11R `K`
suffix is a knurled shaft rather than an SMD terminal, and the `B2` EC11E
variant is still THT. A pure-SMD knob encoder without anchor lugs is not
accepted. U6 therefore stays THT; request JLCPCB THT/hand-solder assembly for it
when offered and use the bench technique below only when that service is
unavailable.

Bourns does not recommend hand soldering the PEC11R, so the fallback is a
qualified process, not an assumption:

1. **Solder the two mounting lugs first** and treat them as the mechanical
   anchor: full fillets on both lugs, inspected, before any signal pin is
   touched. The lugs carry the knob's push and rotational loads, not the five
   electrical pins.
2. **Preheat the pad area** (hot plate or board preheater) and then solder the
   three encoder signal pins and the two switch pins with short, quick joints.
   Keep iron dwell minimal on each pin so the PBT housing never soaks heat; do
   not reflow the part to reposition it.
3. **Inspect for cold joints**, bridges and pin movement under magnification,
   and recheck the lug fillets after the knob is fitted and cycled.
4. `encoder_solder_process` in `hardware-acceptance.json` closes from this
   first-article inspection: record the iron temperature, preheat setting, dwell
   and inspection result in `docs/evidence/` against the hardware fingerprint.
   Until that record exists the item stays open and no process is claimed.

## Cable and power sourcing

The complete itemized cable list is the strict sourcing CSV, including DSI,
the Pi GPIO ribbon, Pico USB, audio/hub USB feedthroughs, ADAT, S/PDIF,
Type A MIDI leads and the supplies. It is the release generator's input; do not
maintain another divergent shopping list here.

Since 2026-11-12 the Pi lives behind its own 20HP mounting panel, so six runs
cross between the two rack positions (plan 150–350 mm): the three USB
feedthrough cables (the feedthroughs stay on the 30HP panel), the display DSI
FPC, the 40-wire GPIO ribbon to `J_PI`, and the J_PWR pigtail to the Pi's bare
`J2` pads. The kit DSI cables (50/120 mm) may not span it: the fitted cable or
extension, its 15→22 adaptation and its touch/signal result are recorded under
`pi_panel_harness` before the harness is trusted.

- **USB-C power:** SparkFun CAB-15455 is 305 mm, USB 2.0 and **3 A**. It is
  rejected for the product's 5 A path. Its flange/cutout is an inherited
  reference, not approval for a new power part. Select an exact suitably rated
  feedthrough/cable chain; do not assume a 5 A PSU upgrades a 3 A cable.
- The official Raspberry Pi 27 W, 5.1 V/5 A PSU remains a supply candidate.
  PD/current advertisement, both USB-C orientations, voltage drop, temperature
  and retention need measurement with the actual complete chain.
- Audio/hub feedthroughs are Adafruit 908, USB 2.0, 340 mm. Check flange/body
  overlap, cable bend, strain relief and Pi-port assignments on the mock-up.
- Display video/touch use DSI, not display HDMI or touch USB. DSI and internal
  cables must not support the Pi mechanically.
- MIDI cables are Type A: tip = DIN 5, ring = DIN 4, sleeve = DIN 2. Test the
  exact jacks/adapters. Input sleeves have no direct DC path to local ground.

## Assembly and release sequence

1. Pass `make test` and inspect its notes. This is prototype verification only.
2. Obtain explicit owner approval for an engineering prototype and follow the
   process in [acceptance.md](acceptance.md); it is not a production approval.
3. Review actual Gerbers, drill, mask, assembly side, polarity and CPL rotation;
   verify exact purchased parts and qualified encoder soldering process.
4. Assemble current-limited first articles. Record power, MIDI, SPI, thermal,
   mechanical and retention evidence against the hardware fingerprint.
5. Resolve every required item in `hardware-acceptance.json`. Only then can
   `make release` create a production packet.

Expected counts in `jlc-order.json` are checked against generated BOM/CPL, not
used as evidence by themselves. Bare panel no-mask fabrication and final DFM
remain pending; a successful export is not fabricator acceptance.

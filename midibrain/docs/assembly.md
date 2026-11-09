# Candidate components and assembly

**Prototype instructions, not a purchasing or production release.** Use the
canonical tscircuit source, `jlc-order.json`, the strict
`tscircuit/scripts/sourcing-extras.csv`, and the reviewed BOM/CPL together.
Recheck exact MPN, package, supplier and stock before ordering. All physical
acceptance items remain pending.

## Board split and assembly sides

| Board | Contents | Assembly |
| --- | --- | --- |
| Front/UI, 128 × 112 mm | MIDI inputs/outputs, six transport/shift switches, three LEDs, NAV, encoder and J5 | SMT on top/panel side. J5 THT body faces rearward; other THT controls face the panel. |
| Rear/base, 101.6 × 112 mm | Pico sockets, Pi/front ribbons, SPI buffers, reset and power branch | SMT on bottom/rear side; THT headers/sockets fitted separately. |
| Panel, 30HP/3U (152.0 × 128.5 mm) — button opening **UNRESOLVED** (provisional Ø12.4 mm, unsourced; see docs/mechanical-stack.md) | FR-4 1.6 mm, white legends, cutouts/light-pipe holes | No components or copper; no copper finish is required. Confirm no-solder-mask fabrication with JLCPCB. |

The encoder is **not** an unconditional hand-solder line item: the selected
THT part has no catalog SMD detentless 24 PPR equivalent, and Bourns does not
recommend hand soldering it. See its process requirement below. All THT parts
stay out of the SMT CPL.

## SMD candidates

| Function | MPN / value | LCSC | Package |
| --- | --- | --- | --- |
| MIDI receiver | Toshiba TLP2361(TPL,E) | C107626 | 11-4L1S |
| MIDI output PNP | MMBT3906LT1G | C53444 | SOT-23 |
| MIDI LED reverse diode | 1N4148WS | C2128 | SOD-323 |
| MISO/READY buffer | SN74LVC1G126DBVR | C7834 | SOT-23-5 |
| Reset FET | JCET 2N7002 | C8545 | SOT-23; pins 1/2/3 = G/S/D |
| Pi 5 V → Pico VSYS diode | B5819W | C8598 | SOD-123 |
| SPI series | 100 Ω, 0603WAF1000T5E | C22775 | 0603 |
| MIDI input | 220 Ω, 0603WAF2200T5E | C22962 | 0603 |
| RX pull-up | 2.2 kΩ, 0603WAF2201T5E | C4190 | 0603 |
| PNP base | 4.7 kΩ, 0603WAF4701T5E | C23162 | 0603 |
| Pull-up/gate | 10 kΩ, 0603WAF1002T5E | C25804 | 0603 |
| Pull-down | 100 kΩ, 0603WAF1003T5E | C25803 | 0603 |
| Indicator series | 330 Ω, 0603WAF3300T5E | C23138 | 0603 |
| MIDI ring / tip | 22 Ω / 33 Ω, 1 W | C118189 / C53728 | 2512 |
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
| SW1–SW6 | Omron B3F-4055, C84931. SW5 is PLAY START; SW6 is SHIFT/EXIT. Through-panel actuators and reach are pending. |
| U6 / encoder | Bourns PEC11R-4015F-S0024, C143789. Detentless (0 detents), 24 pulses/rev, push switch, THT with two anchor lugs. **THT fallback:** the 2026-11-06 JLC/LCSC search found no SMD encoder with 0 detents, 24+ PPR and through-hole anchor lugs (the SMD PEC11S/PEC11J/EC11J families are 30 detents/15 pulses), and a lug-less SMD knob encoder is not accepted. Request JLCPCB THT/hand-solder assembly for U6 if offered; otherwise use the bench technique below. Bourns does not recommend hand soldering PEC11R, so the process is qualified rather than assumed. |
| J_PI / J_FRONT / J5 | XFCN BH254V-40P / BH254V-26P. Use alternating IDC numbering and the exact body/keying represented by the selected imports; continuity-test the purchased ribbons. |
| Pico sockets | Two removable 1×20, 2.54 mm sockets. Exact procurement and installed height must match the module and first article. |
| TP1–TP15 (probe points) | **Fifteen bare plated through-holes on the rear board: no part, nothing to fit or buy.** They are declared `doNotPlace`, so they have no BOM and no CPL line; do not fill them with a header or a wire. Each carries its net name on the silkscreen (`5V`, `3V3`, `PI3V3`, `RUN`, `SCLK`, `CS`, `MOSI`, `MISO`, `READY`, `GND`, `TX1`, `RX1`, `TX2`, `RX2`, `GND`) and its `TP` reference. Their use and the unverified physical access are described in `ELECTRICAL-VERIFICATION.md` and acceptance item `probe_point_access`. |
| Display | Waveshare 4.3inch DSI LCD, ElectroKit 4103 5687, with the kit's 120 mm 15-pin-to-Pi-5-22-pin DSI cable (50 mm is too short for the route contract; the 160 mm cable is 15-pin-to-15-pin and does not fit DSI1). |
| Display spacers | Provisional **5 mm beyond the 4 mm boss ends**, giving 9 mm PCB-plane separation. Do not buy a 9 mm spacer from the plane-gap number. Exact parts and fit remain pending. |
| Panel/front spacers | Length/MPN pending. The current flush-glass target gives a 17.45 mm panel-rear-to-front-PCB-top gap, not a measured assembly. |
| Front/rear spacers | Length/MPN pending the keyed 2×13 plug and bend mock-up; provisional 18–22 mm range. |
| Pi spacers | Candidate 2.5 or 5 mm; actual microSD/SMT clearance and service access determine the choice. |
| Light pipes | **Part selection pending.** PLP2-400-C is a 10.16 mm reference, not approved for the provisional 17.45 mm gap; reach, seating and the LED envelope must be established before purchase. Three 4 mm panel holes remain. |
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
Pi GPIO and front ribbons, Pico USB, audio/hub USB feedthroughs, ADAT, S/PDIF,
Type A MIDI leads and the supplies. It is the release generator's input; do not
maintain another divergent shopping list here.

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

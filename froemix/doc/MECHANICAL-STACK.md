# Froemix mechanical stack — corrected assembly contract

This supersedes the old 12mm-spacer / “8.5mm socket” description. Coordinates,
connector sides, electrical nets and both PCB outlines are unchanged except for
the **four rail mounting holes**, which moved 0.12mm inward each (86.60 →
86.36mm, owner-approved; §6). Panel machining changes: the **NAV Ø7.0 → Ø8.0mm**
opening at (−28, 25) for the extended actuator's swept envelope, and **removal of
the four audio activity-LED holes** at y = −3 with their LEDs (the direct LED
load distorted the audio jacks). The other diameters and the OLED opening
(30.5 × 16mm, R0.5, at 0, 42.8) are unchanged.

**Status: dimensioned design and printable prototypes, not physical approval.**
No parts were measured and no rack/CAM order was approved by the software gate.
The gate now measures the fastener envelopes from the BUILT boards and prints
every item it cannot close (`lib/mechanical.ts`: `UNRESOLVED`,
`PENDING_ACCEPTANCE`). A gate PASS is not a physical approval.

## 1. Findings rechecked against the selected parts

| Finding | Result / action |
| --- | --- |
| 12mm clear spacers with 2mm-shoulder male / 4.3mm female | Real fault: the male tip is 1.7mm short of even entering the socket. Replaced the three mated base headers with 6mm-shoulder XKB parts; clear spacers are now 11mm. |
| Reduce the gap to 6.3mm without another change | Rejected: the base-top J1 header/shunts would obstruct the front board. |
| CN4 “0.03mm drift” | Not a fault. Both footprint origins are **(24.50, 59.63)**; the base pad bounding-box centre is 24.47 because its pin-1 pad is wider. Moving the front socket to 24.47 would introduce a real error. CN3's corresponding origin is 8.14, bbox centre 8.11. Neither connector was moved. |
| 3.2mm LED holes let the selected LEDs fall through | Incorrect for C440523: its drawing gives a **3.8mm flange**, 3mm lens, ±0.25mm general tolerance. Holes remain 3.2mm. They are clearance holes, not snap-in holders; set and solder the LED height with the panel as a jig. |
| Bare MODE cap too short | Confirmed: 7.3 ±0.2mm above the PCB. Supplied a captive translucent plunger, **5.8mm added height**, rather than lifting a switch on leads that are too short for a riser. |
| Bare NAV stem/cap unspecified | Current XUNPU drawing gives 10mm overall height and 3.15mm square stem, not the old Alps assumption. Added a dimensioned cap/neck and enlarged only its panel hole. |
| J1 inaccessible after stacking | Expected service constraint, not an XY mistake. Select/test Type A/B before stacking; separate the boards to change it. No extra front-board MIDI selector or new electrical path was introduced. |
| Standoff/hardware limit was quoted two ways (5mm in the purchase list, 5.5mm here) | Real inconsistency. The limit is now **derived**: 2 × (nearest component envelope − 0.45mm budget), measured on both built boards at every hole and face. The tightest face is the **base board bottom at (36.95, −60.75)**, where C25's courtyard is **2.95mm** away, so the limit is **OD5.00mm** for the spacer body AND for any head/washer. An ordinary **ISO 4762 M3 socket head is 5.5mm** (0.20mm left, below the 0.45mm budget) and an **ISO 7089 M3 washer is 7.0mm** (−0.55mm, a collision). No head/washer is approved; the selection stays pending. |
| NAV radial margin was only 0.004mm above the 0.2mm requirement | Real finding at the old 5.8mm cup under the ±8° test envelope. The cup is now **5.4mm** with the same pocket, roof and neck: radial clearance rises from 0.204 to **0.406mm** and no electronic part, hole or outline moves. |
| Selected jack body is taller than the minimum panel gap | Real: the XKB drawing gives an **8.8mm body** against an **8.5mm** `panelGapMin`, so at the minimum gap the panel would rest on a jack body. The panel is the seating jig; a first article must confirm no bowing. Recorded as a pending item, not fixed by guessing a taller stack. |
| Rack width and mounting-hole pitch | **Hole pitch fixed by owner approval:** the pair was **86.60mm**, 0.24mm off the Doepfer 5.08mm grid, while two Ø3.2 holes give only 0.20mm of take-up. Each hole moved 0.12mm inward to **±43.18 = 86.36mm = 17 × 5.08mm**; no control, connector, standoff, diameter or outline changed and the width stays 101.6mm. **Width still open:** 101.60mm against Doepfer's **101.30mm** is 0.30mm oversize and awaits a rack test. See §6. |

## 2. Parts and board-to-board Z dimensions

The source is `euro/lib/mechanical.ts`; the TSX part identities are checked
against it from **both built boards**, rather than trusting a comment.

| Board / refs | Purchase part | Preparation / dimensions |
| --- | --- | --- |
| Base CN1, CN2 | XKB `X4612WV-16I-C40D28-E60`, **C2883069**, two | 1×16, 2mm pitch, 0.5mm square pins; **E=6mm total shoulder**, C=4mm exposed post, D=2.8mm solder tail. Two plastic mouldings, not 6mm plus another 2mm. |
| Base CN4 | XKB `X4612WV-08I-C40D28-E60`, **C2883061**, one | Buy the stocked 8-way strip. **Remove the LAST pin and its cell in BOTH mouldings**, dress the cut, retain the original pin-1 end. Fit the resulting seven-way strip to the existing seven holes. Do not leave an eighth metal stub or plastic overhang. |
| Front CN1, CN2 | Hong Cheng `HC-PM200-4.3H-1x16PZ`, C22373851 | Existing 4.3 ±0.15mm socket body, 2.8mm solder tails. |
| Front CN4 | Hanxia `HX PM2.0-1x7P ZC`, C22438139 | Existing seven-way 4.3 ±0.15mm socket. |
| Board spacers ×4 | **11.0 ±0.1mm clear length**, M3 female/female | **≤5.0mm OD** insulating columns preferred, at (±36.95, ±60.75). The 5.0mm figure is derived from the measured 2.95mm nearest envelope plus the 0.45mm budget (§4). Source from RS, Electrokit or DigiKey with EU sales; confirm dimensions. |
| Board screws ×8 | M3, head/washer **≤5.0mm OD** | **Not selected.** A 5.5mm ISO 4762 head and a 7.0mm ISO 7089 washer do not meet the limit; see §4. Use only a measured part at or below the printed limit. |

C2883060 is a seven-way member of the same family, but did not resolve in the
JLC catalogue when selected. The BOM deliberately buys the stocked eight-way
C2883061 and the guide specifies its preparation; it does not silently pass an
eight-pin part off as a seven-pin catalogue item. Do not substitute an ordinary
2mm-high header or a 2.54mm-pitch strip. CN3 stays short and **unmated**.

The former C225268/C22438290 CAD bodies were removed from the three changed
headers: showing their short bodies would falsely endorse the new stack in 3D.
The land patterns and drills remain identical. These headers are currently
pad-only in STEP/3D; the dimension contract below, not that rendering, governs Z.

### Surface datums (nominal)

Positive depth is into the rack. Panel **underside** is depth zero; its front
face is −2mm. Each PCB is nominally 1.6mm thick.

```text
panel underside                       0.0
control PCB TOP                       9.0   panel-facing controls/OLED
control PCB BOTTOM                   10.6   sockets face toward the base
                                    | 11.0mm clear spacer |
base PCB TOP                         21.6   headers/J1 face toward control PCB
base PCB BOTTOM                      23.2   Daisy sockets, converter, power, tall parts
```

The Daisy plugs into two hand-soldered 1×20 female socket strips on the base
bottom, not directly into the PCB. Their part and the resulting Daisy projection
are not yet measured. Check socket contact/tail fit, nearby component clearance,
front-side solder tails, total rack depth and rear USB access with actual parts.
The sockets and two J1 shunts are separate purchases, not in the board BOM.

Do not confuse the **11mm surface gap** with the **12.6mm PCB centre spacing**.
The original stack drawing omitted the control PCB thickness.

```text
socket entry above base TOP = gap − socket height = 11 − 4.3 = 6.7mm
male pin tip above base TOP = shoulder + post       = 6 + 4 = 10.0mm
pin insertion              = tip − entry          = 3.3mm
insulator face clearance   = gap − 6 − 4.3         = 0.7mm
```

With E ±0.3, post ±0.2, socket ±0.15 and spacer ±0.1mm, arithmetic insertion is
**2.55…4.05mm** and minimum insulator separation is **0.15mm**, both reproduced
from the **64 generated tolerance corners** rather than quoted by hand. The gate's
2.5…4.1mm insertion interval is an **engineering acceptance policy**, not a
supplier-certified contact/retention/bottoming specification: the policy margin
is only **0.05mm at each end**, so nothing else may be added to this stack
without widening the policy or reducing a tolerance. A dry first-article mate
must establish those properties; never draw the boards together by screws
against a hard stop or a pin that has bottomed out.

### Opposed parts, solder tails and J1

- Trim the front board's rear-facing component leads/solder tails to **≤1mm**
  projection. Inspect for sharp stubs and solder spikes before stacking.
- The complete base-top **J1 header + installed shunts must be ≤9.5mm high**.
  Measure it; use ordinary closed low-profile shunts, not tall pull-tab types.
  At the 10.9mm minimum gap this leaves **≥0.4mm** to a 1mm front-board tail.
- Fit Type A **3–5 and 4–6** by default; Type B is **4–2 and 3–1**. Test with
  the front jack and a meter before closing the stack. No diagonal shunts.
- J1 is covered by the front PCB. To change MIDI input type, power off, remove
  the module from the rack and separate the boards. Do not cut an access hole
  through the overlying sync jack. CN3 remains deliberately unused; the Daisy
  SD/USB are approached from the rear, not through the front PCB.

## 3. Panel actuators (printable parts included in release)

`make release` includes `fab/euro/mechanical/`:

- `mode-plunger.stl` — mm units, **13.6mm retaining flange ×0.4mm**, 11.4mm
  sliding barrel, **5.8mm total height**. The bottom rests on the existing
  K6-1212A02 cap. Install the flange **behind** the panel before closing the
  front assembly. Print in translucent resin to transmit the original red LED;
  finish the barrel to ≤11.5mm. It slides in the unchanged 12.2mm opening.
  The flange prevents it falling out; no adhesive, pin extensions or raised
  switch is required. Check free return, no preload and reliable full travel.
- `nav-cap.stl` — mm units, **3.4mm square ×2mm blind pocket**, **5.4mm cup**,
  4mm neck, 6.8mm head; 7mm overall height, **5mm added above the bare stem**.
  Print in tough resin. Dry fit the actual square stem; use a tiny removable
  silicone dab for retention, never force the cap down or wick glue into the
  switch. Its small head stays within the revised panel artwork keepout.
- `mechanical-manifest.json` — dimensions, bounds, volumes, datum limits and
  calculated clearances, so a printer does not need to interpret a screenshot.
- `hardware.csv` and this guide — non-board shopping/preparation list; **not**
  a JLC SMT upload. Custom printed parts are fabricated from these files, not
  represented as fictitious LCSC components.

The cap opening is now **8mm**, not 7mm. `panel-baseline.json` records this one
explicit revision while retaining the original source hashes as history.
Nord's navigation ticks move outward to preserve ink clearance; no control or
connector centre is moved.

The fit calculation conservatively covers a panel-under-PCB distance of
**8.5…9.6mm**, panel thickness **1.8…2.2mm**, print dimensional error 0.1mm and
hole undersize 0.2mm. The nominal panel gap remains **9mm**. The selected XKB
jack drawing gives an 8.8mm body, with additional bushing geometry: it does
**not** justify copying a retired PJ398SM's “exact 9mm shoulder” claim. Assemble
jacks/pots against the actual panel before soldering; reject a forced or bowed
PCB and measure the resulting gap.

At the highest/thickest panel the supplied MODE plunger is ≥1.0mm proud and NAV
≥2.8mm proud; the MODE lip keeps ≥0.5mm to the panel at the worst-case gap, so
the measured switch travel must stay below 0.5mm or the lip will preload it. The
K6-1212A drawing gives no travel figure, so that limit is **pending**, not
approved, and the flange geometry is unchanged. The NAV swept-envelope check uses
**±8° as a design/test envelope**, not a XUNPU datasheet rating, and now reports
**0.406mm** minimum radial clearance against a 0.2mm requirement. Reducing the cup
from 5.8 to 5.4mm bought that margin without touching the pocket, the roof, the
panel hole or any electronic part:

| NAV cap dimension | Value | Limit |
| --- | --- | --- |
| cup diameter | 5.4mm | ≥ pocket + 2 × 0.25mm wall |
| pocket side wall | 1.00mm | 0.25mm (print assumption) |
| pocket **corner** wall (thinnest) | 0.296mm | 0.25mm |
| pocket roof | 0.60mm | 0.25mm |
| pocket vs 3.15 ±0.05mm stem | 0.10mm total | > 0 (silicone retention) |
| cup through the 8.00mm hole, untilted | 1.15mm radial | > 0 |

The 0.25mm minimum wall is a **resin/slicer assumption, not a supplier figure**;
the first article must show the corner does not split. A smaller cup would thin
that corner before it bought another 0.25mm of clearance, so 5.4mm is the end of
this trade. Test full operation of all four directions and centre press with the
real switch: if its required travel or tilt exceeds these envelopes, the design
is not qualified merely because the software tests pass.

## 4. Hardware and optical limits

### Board-to-board hardware: the limit is measured, not assumed

`scripts/mechanical_check.ts` reads the BUILT circuit JSON of **both** boards and
finds, for every one of the four standoff holes and for each face, the closest
component envelope that the hardware on that face can hit. A component's own
courtyard is its envelope; a component without one (fiducials, test points) uses
the union of its pads on that face, which can only over-state it. Layers are
respected: a part on the bottom face is not an obstacle for a head on the top
face, while a through-hole pad is on both.

The spacer column stands between the boards, so its ends bear on **base top** and
**front bottom**; each board takes its own screw head on its free face.

| Board | Hole (x, y) | Face | Hardware | Nearest envelope | Distance | OD5.0 clearance | Derived OD limit |
| --- | --- | --- | --- | --- | --- | --- | --- |
| base | (−36.95, 60.75) | top | spacer end | CN1 courtyard | 3.05mm | 0.55mm | 5.20mm |
| base | (36.95, 60.75) | top | spacer end | CN4 courtyard | 5.50mm | 3.00mm | 10.10mm |
| base | (−36.95, −60.75) | top | spacer end | J2 pads | 9.10mm | 6.60mm | 17.30mm |
| base | (36.95, −60.75) | top | spacer end | C25 pads | 6.51mm | 4.01mm | 12.12mm |
| **base** | **(36.95, −60.75)** | **bottom** | **screw head** | **C25 courtyard** | **2.95mm** | **0.45mm** | **5.00mm** |
| base | (−36.95, 60.75) | bottom | screw head | CN1 pads | 3.63mm | 1.13mm | 6.35mm |
| base | (36.95, 60.75) | bottom | screw head | FID2 pads | 3.87mm | 1.37mm | 6.84mm |
| base | (−36.95, −60.75) | bottom | screw head | FID3 pads | 3.87mm | 1.37mm | 6.84mm |
| front | (−36.95, 60.75) | bottom | spacer end | CN1 courtyard | 3.06mm | 0.56mm | 5.21mm |
| front | (36.95, 60.75) | bottom | spacer end | CN4 courtyard | 5.14mm | 2.64mm | 9.37mm |
| front | (−36.95, −60.75) | bottom | spacer end | J9 pads | 7.96mm | 5.46mm | 15.01mm |
| front | (36.95, −60.75) | bottom | spacer end | J14 pads | 6.58mm | 4.08mm | 12.26mm |
| front | (−36.95, 60.75) | top | screw head | CN1 pads | 3.63mm | 1.13mm | 6.35mm |
| front | (36.95, 60.75) | top | screw head | CN4 pads | 5.70mm | 3.20mm | 10.50mm |
| front | (−36.95, −60.75) | top | screw head | J9 courtyard | 7.25mm | 4.75mm | 13.60mm |
| front | (36.95, −60.75) | top | screw head | J14 courtyard | 6.17mm | 3.67mm | 11.44mm |

Consequences, which the gate prints and which are **not** approvals:

- The purchase limit is **OD5.0mm** for the spacer body and for every head or
  washer, because 5.0mm leaves exactly the 0.45mm budget at the tightest face.
- The `C25` and `CN1` distances set that limit, not a general rule: the next
  tightest face allows 5.20mm and most allow much more.
- An ordinary **ISO 4762 M3 socket head (5.5mm)** fits with **0.20mm** and so
  misses the budget; an **ISO 7089 M3 plain washer (7.0mm)** *collides* with C25
  at **−0.55mm**. The gate reports both as pending parts that may not be used as
  selected. Select a measured head, or re-open the 0.45mm budget as a documented
  decision (it is a policy margin, not a supplier number).
- If a part later moves closer to a standoff hole, the derived limit drops below
  5.0mm and the gate fails: the limit cannot silently become a tighter fit.
- A courtyard drawn over a standoff hole is caught as a collision at every OD.

- **Knobs:** 6mm knurled shaft for the selected ALPS pot, **≤12.5mm diameter**.
  Pots are on 15mm centres, leaving ≥2.5mm between these knob envelopes. A
  particular knob/washer is not selected by a preview; fit the actual knob and
  check labels, rotation stops and shaft engagement before buying a batch.
- **Jack nuts:** ≤11.5mm OD envelope; **rail washers:** ≤6mm OD envelope. These
  are purchasing limits matching artwork checks, not measurements of supplied
  nuts. Check bushing thread engagement through a 2.2mm panel with actual
  nuts/washers. Do not assume a replacement jack shares its predecessor's Z.
- **LEDs:** retain Ø3.2mm holes for C440523 (Ø3.8mm flange). Do not seat the LED
  bodies against the PCB: their lens height alone does not span the panel gap.
  Use the panel as the height/alignment jig, tack the leads, check visibility
  and flange/lead clearance, then solder. Keep the bodies supported while
  soldering; holes are not friction-fit LED retainers.
- **OLED:** C42432559 / HS13L02B4S01, 35.4 ×33.5mm PCB. Header (0,55.5), window
  (0,42.8), offset −12.7mm. Nominal active area 29.42 ×14.70mm leaves 0.54/0.65mm
  per edge in the 30.5 ×16mm opening. Confirm the module/bezel and support it
  without stressing glass or hanging the panel on its pins. Actual Z/bezel and
  viewing-angle approval cannot be extracted from a seven-pad footprint.
- **Rack:** 101.6mm is the 20HP pitch, and the mounting-hole pitch must be a
  whole multiple of 5.08mm — the pair is now **86.36mm** (17 × 5.08) after the
  approved 0.12mm move. The **101.60mm width against Doepfer's 101.30mm is still
  open** (§6). Do not shrink a PCB or move another frozen hole to hide that; it
  would need its own owner revision.

## 5. Verification and first-article acceptance

`make gate` builds both boards in order. Euro's gate now checks all **39 pads**
(not just the end pads), all 34 connected pin-net assignments and five reserved
positions, actual top/bottom faces, four matching Ø3.2mm standoffs, all panel
control origins/faces and D1. It reads the **27 frozen panel holes** (count,
position and diameter) from `fixtures/panel-baseline.json`, so the count comes
from the audited fixture and not from a stale document figure; the OLED window
is a separate cutout. `mechanical_check.ts` adds the generated numbers:
**64 stack tolerance corners** (insertion 2.55…4.05mm, body gap ≥0.15mm, J1
clearance ≥0.40mm), **32 actuator corners**, the **measured fastener envelopes
of both boards** (§4), the OLED aperture border and the rack numbers (§6).
Negative tests move an **interior** pad, drop a mated pad, remove or resize a
frozen panel hole, add an unaudited hole, alter a net, flip a face, change a
standoff and shift the indicator, and the unit tests feed NaN/Infinity into
every measured input to prove it is rejected rather than compared. Part-ID/Z
tests reject short headers, old 12mm spacers, over-height J1 and untrimmed
leads. Printable meshes are validated as closed solids, and a probe through the
printed NAV corner proves the wall exists; geometry tests do not inspect
material strength, finish or printed-part shrinkage.

Before paying for a batch:

1. Dry-mate the selected three header/socket pairs with **11mm** spacers; check
   continuity of all used pins, retention and no bottoming. Confirm CN4 was
   prepared to seven ways. Do not substitute a short part from an old order.
2. Measure J1+shunts ≤9.5mm and front-board tails ≤1mm. Set/test the MIDI type.
3. Print/finish both actuator prototypes, assemble through the real panel, and
   test spring return, no preload, full switch travel and cap retention.
4. Jig and solder the LEDs at the panel height; check polarity on SW2 with a
   current-limited LED test before soldering. The K6 drawing doesn't number
   LED +/− positions reliably; this is still a physical polarity check.
5. Confirm OLED alignment/support, knob/nut/washer envelopes and thread depth.
6. Test adjacent-module and rack-rail fit with the actual panel/PCBs and
   socketed Daisy; measure its total depth and check rear USB access. Print the
   1:1 template at actual size and measure its 100mm calibration bar first.
7. Approve actual Gerber/profile/NPTH production files with CAM, including the
   new Ø8mm NAV opening, 2mm material and empty copper.

None of these physical checkboxes is marked done by this software change.

## 6. Rack interface — hole pitch fixed, width and rail profile open

Reference: Doepfer's A-100 construction details (`doepfer.de/a100_man/a100m_e.htm`):
**128.5mm** panel height, **2mm** panel material, **M3 DIN 7985 pan head**
screws, and mounting holes on a **whole multiple of 5.08mm**. The gate prints
these numbers from the frozen outline (`rackFit` in `lib/mechanical.ts`):

| Quantity | Design | Rack reference | Result |
| --- | --- | --- | --- |
| panel / PCB width | 101.60mm (20 × 5.08) | 101.30mm finished for 20HP | **0.30mm oversize — OPEN** |
| horizontal mounting holes | **86.36mm** apart (±43.18) | 17 × 5.08 = 86.36mm | **on the grid, 7.62mm edge offset** |
| hole take-up | two Ø3.2mm holes on nominal Ø3.0mm M3 | 0.10mm radial each | 0.20mm total, **now entirely spare** |
| vertical hole spacing | 122.50mm (3.0mm from each end) | 3U 128.5mm outline | unchanged |
| rail lip depth behind the panel | front PCB is 128.5mm high at a 9mm panel gap | not published | **unknown: no clearance may be claimed** |

### Approved mechanical revision: rail mounting holes

The pair was **86.60mm** (±43.30, a 7.50mm edge offset), i.e. **0.24mm off the
5.08mm grid**, while two Ø3.2mm holes on nominal Ø3.0mm M3 screws take up only
**0.20mm** — a 0.04mm shortfall that fixed threaded strips may not absorb. The
owner approved a **rail-hole-only** change: each hole moved **0.12mm inward** to
**±43.18mm = 17 × 5.08mm = 86.36mm**, a nominal **7.62mm** edge offset.

- `lib/layout.ts` `RAIL_HOLE_X` derives it (`PANEL_W / 2 - 7.62`) and records the
  approval; `fixtures/panel-baseline.json` holds a third `mechanicalRevisions`
  entry with all four `from`/`to` positions and the reason.
- Preserved: the 23 other circular holes and their diameters, the OLED window
  (30.5 × 16mm, R0.5), every control and connector position, the four standoff
  holes (±36.95, ±60.75) on both boards, and the 101.6mm width and 128.5mm height.
- The gate now fails if the pair leaves the grid (`rackGridErrors`), and a unit
  test feeds the old ±43.30mm pair in and expects a failure.

The width is **not** changed: 101.60mm against Doepfer's 101.30mm remains a
pending rack item, together with the rail profile. Moving a hole or trimming the
outline changes the frozen panel geometry and artwork, so each needs its own
owner revision with a fixture update — not a mechanical-check change.

Rack and adjacent-module acceptance (named, per item, in `PENDING_ACCEPTANCE`):

1. `rack-pitch` — fit the panel to a real rack with **both** sliding nuts and a
   fixed threaded strip; grid and take-up are now correct, but head clearance in
   the rail slot is still unmeasured.
2. `rack-width` — place the finished panel and both PCBs beside a real adjacent
   module at routing tolerance (0.30mm oversize against the Doepfer figure).
3. `rack-rail` — measure the actual rail profile and lip depth against the
   128.5mm front PCB at the 9mm panel gap.

The 2mm panel material and the M3 DIN 7985 screws are reference dimensions only;
this change touches no mains, frame or power wiring.

## 7. Unresolved supplier identities

Kept structured, without a part number, in `lib/mechanical.ts` (`UNRESOLVED`) so
a guessed part cannot pass as approved hardware. EU hand-part sources only (RS
Components, Electrokit, DigiKey with EU sales, `froemix/AGENTS.md` §9):

| Item | Status |
| --- | --- |
| M3 spacers, 8 screws, head/washer | limit OD5.0mm; no verified head/washer at or below it (§4) |
| 2× 1×20 socket strips for the Daisy Seed | no verified part; height, tails and total depth unmeasured |
| 2× 2.54mm J1 shunts | no verified low-profile closed shunt; J1+shunts ≤9.5mm |
| jack nuts, pot nuts/washers, pot knobs, rail washers | envelope limits only, no part selected |

## Drawing references

Research used manufacturer drawings retrieved from LCSC/JLC, not generic header
heights. Product pages carry the current datasheet; catalogue stock can change.

- [XKB E60 16-way C2883069](https://www.lcsc.com/product-detail/C2883069.html):
  drawing `X-X4612WV-XXI-C40D28-E60`, one page, E/C/D = 6/4/2.8mm.
- [XKB E60 8-way C2883061](https://www.lcsc.com/product-detail/C2883061.html):
  same family, cut-to-seven preparation above.
- [Hong Cheng socket C22373851](https://www.lcsc.com/product-detail/C22373851.html):
  drawing HC-PM200-4.3H-1x16PZ, H=4.3 ±0.15mm.
- [Hanxia socket C22438139](https://www.lcsc.com/product-detail/C22438139.html):
  HX PM2.0-1x7P ZC, H=4.3 ±0.15mm.
- [Old CJT C225268](https://www.lcsc.com/product-detail/C225268.html): A2005
  catalogue page A-115 (PDF page 28), 2mm shoulder / 4mm post.
- [XKB jack C381126](https://www.lcsc.com/product-detail/C381126.html):
  PJ-301M-X-X, body 8.8mm, M6×0.5 thread, not a PJ398SM shoulder specification.
- [Hroparts C692901](https://www.lcsc.com/product-detail/C692901.html): K6-1212A,
  7.30 ±0.2mm overall height, 8.2mm cap; 3.3mm leads (not a 5mm riser solution).
- [XUNPU C42377836](https://www.lcsc.com/product-detail/C42377836.html):
  WS-1004-ARL10026, 10mm overall, 5.9mm body, 3.15 ±0.05mm square actuator.
- [TOGIALED C440523](https://www.lcsc.com/product-detail/C440523.html):
  package dimensions PDF page 2, lens Ø3 / flange Ø3.8 / height 5.3mm.

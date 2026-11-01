# Froemix front panel — design and JLCPCB order guide

This document is the panel-specific design and manufacturing record. The
20HP faceplate sits in front of the existing control board. It is **not** an
assembly panel containing multiple PCBs. No electronic parts are fitted to the
faceplate and no stencil, BOM or pick-and-place file is needed for it.

**Recommendation:** 2.0mm FR4, standard black solder mask, white silkscreen,
routed outline, non-plated control holes. This preserves the existing nominal
panel thickness and the tscircuit-only workflow. It is a design recommendation,
not a placed order or an approval of the mechanical stack.

## Materials investigated

| Material / process | Appearance and trade-off | Decision |
| --- | --- | --- |
| 2.0mm FR4 PCB | Black mask with white print; bare laminate visible at cut edges. Compatible with tscircuit Gerbers. Finished thickness tolerance is ±10% (1.8–2.2mm). | Recommended first article. |
| 1.6mm FR4 PCB | Same printing process, but thinner and less stiff. Changes nut thread engagement and the front-to-control relationship. | Not substituted silently for the existing 2mm design. |
| Aluminium-core PCB | A thermal PCB, not an anodised instrument faceplate. One-layer process with material/finish restrictions. JLCPCB specifies HASL for aluminium PCB finish. | No advantage for this copper-free design; requires a different process review. |
| 2mm sheet aluminium | JLCCNC offers 5052 sheet aluminium, anodising, silkscreen and laser marking. A more conventional premium metal faceplate. | A future alternative: separate JLCCNC quote and mechanical CAD/drawing, not this Gerber ZIP. |
| Multi-colour PCB print | JLCPCB's published process requires EasyEDA Pro colour-production files, a white PCB, 2/4 layers, 1oz and ENIG. A PNG preview does not turn ordinary Gerbers into colour artwork. | Not compatible with this tscircuit-only release. |

A black mask does **not** imply a guaranteed matte or anodised finish. Specify
standard **Black**, not an unsupported matte finish. White ink is the only
printed colour. Metal hardware, illuminated LEDs and an OLED image in a preview
are illustrations, not extra printing layers.

## Design direction

Two monochrome directions are being compared: **Nord**, a restrained studio
instrument, and **Signal**, a bolder technical instrument. Both must retain the
same geometry. The final visual choice is the owner's; neither changes the
firmware or the PCB layout.

- Keep `FROEMIX` prominent. Group audio, CV, sync and MIDI ports clearly.
- Distinguish outputs without relying on colour: outlines, brackets or line
  weight must remain legible with patch cables fitted.
- Keep the 6 assignable pots labelled P1–P6. Navigation is NAV/SELECT and the
  separate illuminated button is MODE. Do not invent fixed firmware functions.
- Keep printed legends outside the installed knobs, nuts, washers and caps.
  Preview hardware sizes are conservative assumptions, not a selected knob BOM.
- Use at least 0.2mm artwork strokes and generous readable legends (target
  1.5mm). JLCPCB's rigid-board floor is 0.15mm stroke and 1.0mm character height.
- No exposed-copper decoration, added nets, mounting pads or electrical ties.

## Geometry contract

All 31 existing circular holes retain their centres and diameters from
`euro/lib/layout.ts`. The existing outer size remains 101.6 × 128.5mm and
nominal thickness remains 2mm. The base board remains 16HP. Do not reposition a
control or a mating connector to fit artwork.

The OLED opening retains its **30.5 × 16.0mm bounding box** and centre
**(0, 42.8)mm**. Its formerly sharp internal corners need a routing radius of at
least **0.5mm**. Rounded internal corners are the one intentional mechanical
change in this panel revision. They must appear in the actual profile export,
not only in a mockup.

### What the manufacturing files mean

- **Top silkscreen:** the white graphic/legend, not the preview's background.
- **Top/bottom mask:** black is an order setting; mask data defines openings.
- **Copper:** deliberately empty on both sides. No electrical function.
- **NPTH drill:** round, unplated control and rail holes.
- **One profile layer:** outside boundary and the rounded OLED opening. The
  installed tscircuit exporter emits a 0.05mm profile centreline. JLCPCB
  recommends 0.15mm; verify CAM follows the drawn centreline and the checked
  bounding dimensions, rather than treating the line width as a cutout allowance.

JLCPCB says holes at or above 6.3mm are CNC-routed from a smaller drilled hole.
The exporter can describe circular holes in the NPTH file. This does not prove
how CAM will process them. Ask CAM to confirm the 7.0, 7.2 and 12.2mm holes are
routed to their finished sizes. If CAM requests profile circles instead, change
the export and rerun the geometry checks; do not duplicate a hole in both files.

## Build, preview and export

From `froemix/euro/` (Bun must be on PATH):

```sh
bun install --frozen-lockfile
bun run panel:build                      # Nord, the recommended default
bun run panel:check --variants           # check both artwork directions
bun run panel:test                       # manufacturing-check mutation tests
bun run panel:preview --variants         # SVG / HTML / A4 print templates
bun run panel:release                    # checked Nord Gerber ZIP
bun run panel:release --design signal    # checked Signal alternative
bun run gate                            # existing board gate plus panel checks
```

The release command builds both alternatives, then exports the selected design
through the tscircuit CLI. It parses the **actual ZIP contents** to check the
outline, rounded window, silkscreen paths, NPTH coordinates and diameters,
metric units, empty copper and absence of assembly data. It writes:

- `euro/dist/panel-release/froemix-panel-<design>-gerbers.zip` — the fabrication
  upload, not the electronic control board's Gerbers.
- `euro/dist/panel-release/panel-release.json` — selected style, SHA-256,
  geometry, check results and the order-remarks text. A later release updates
  this manifest for the newly selected design; check the filename before upload.
- `euro/dist/panel-release/preview/panel-<design>-preview.svg` / `.html` — the
  exact built print paths, with conservative hardware outlines for reference.
- `euro/dist/panel-release/preview/panel-<design>-print-1to1.svg` — A4 dimensional
  template with a 100mm calibration bar. Print at actual size, not fit-to-page.

`lib/panel-art.ts` holds the two designs and their artwork primitives. The
production panel uses stroked paths, including all lettering; it does not rely
on installed system fonts. SVG preview effects and comparison-study tweaks are
not extra production colours or fabrication changes.

## Order settings

| Setting | Selection / check |
| --- | --- |
| Service | PCB fabrication only; separate order item from the electronic boards |
| Material / layers | FR4, 2-layer file import, both copper layers intentionally empty |
| Size | 101.6 × 128.5mm; verify the quoted dimensions after ZIP upload |
| Thickness | 2.0mm; account for the 1.8–2.2mm finished tolerance |
| Solder mask / legend | Standard Black / White, ordinary single-colour legend |
| Finish | Lead-free HASL if a finish is required by the form; no exposed copper benefits from ENIG |
| Outline | Routed; no V-scoring or breakaway tabs on the finished faceplate |
| Precision | Ask CAM about ±0.1mm routing. Published precision routing also requires at least 3 tooling holes ≥1.5mm in different corners; confirm whether the existing 4 rack holes qualify. Otherwise use the stated ±0.2mm regular-routing tolerance in fit checks. |
| Assembly / stencil | None |
| Marking | No added barcode/serial number on the front. JLCPCB says no mark is added by default; old `JLCJLCJLCJLC` placeholders are obsolete. |
| Quantity | A small first-article order before a larger run; obtain the live quote rather than relying on advertised starting prices |
| Production-file review | Request CAM/production-file confirmation and inspect it before approving fabrication |

**Paste into the order remarks**, not merely a PDF inside the upload ZIP:

> Mechanical Eurorack faceplate, no circuit. FR4 2.0mm, black solder mask,
> white top legend. Both copper layers are intentionally empty; please confirm
> copper-free manufacture. All holes are non-plated. Please confirm finished
> diameters and routing of the large NPTH holes, the OLED corner radius and the
> routed outer profile. Do not add a barcode/mark on the front. Please provide
> production files for approval.

The official guidance does not explicitly guarantee copper-free panel orders.
Empty copper files are intentional, not missing tracks. Acceptance remains a
CAM question. Do not add an arbitrary copper plane as a workaround: that changes
insulation/grounding and needs a separate review.

## Before paying for a batch

A passing circuit or artwork test does not establish real mechanical fit.

1. Print the dimensional template at **100% / actual size**, with no fit-to-page.
   Measure its calibration bar with a ruler before checking actual parts.
2. Check rack fit. 20HP is a **101.6mm pitch**, not proof that a 101.6mm finished
   panel fits next to another module at the worst routing tolerance. The existing
   width is preserved; decide any narrower finished width with the actual rack.
3. Check rack screw locations, nut/washer diameter and knob diameter against the
   real hardware. Preserve readable labels after assembly.
4. Check jack/pot bushing thread engagement through a **2.2mm worst-case** panel.
   Confirm the control PCB's 128.5mm height clears the rack rails; that pre-existing
   board dimension is not changed by this artwork revision.
5. The current `PANEL_GAP` is **9.0mm**. The documented button cap is **7.3mm**
   above the control PCB, already 1.7mm short of the nominal panel underside.
   Solve the cap/riser/reach mechanically; adding decoration does not solve it.
6. Check the actual **current XUNPU** navigation switch and cap through the
   opening. Old Alps joystick height statements elsewhere are not measurements
   of this replacement part.
7. Check OLED glass, bezel and rounded opening alignment with the actual module.
   Verify viewing angle, module support and the clearance behind the panel.
8. Review the actual Gerber silk/profile and all NPTH holes in the uploaded CAM
   preview. Confirm the white graphic is top-facing and not mirrored.

## Review status

The implementation has automated panel-artwork and fabrication-file checks, plus
manual inspection of an independently rendered Gerber silk/profile/drill view.
The control-hole centres and diameters were compared directly with the original
panel export, not only with a second copy of the new layout data.

An independent reviewer agent timed out without delivering a review. The PR is
therefore a **draft with independent review pending**. This is separate from the
CAM acceptance and physical-fit checks above. A passing test suite is not a
substitute for any of those reviews.

## Official sources

Research checked against the current official pages on 2026-11-01. Availability,
options, quoting and manufacturing limits can change; the order confirmation
and approved production file take precedence.

- [Rigid PCB capabilities](https://jlcpcb.com/capabilities/pcb-capabilities): FR4
  thicknesses, ±10% thickness tolerance, black mask, 1mm text / 0.15mm stroke,
  drill/routing limits, no sharp rectangular internal corners, routing tolerance
  and precision-tooling conditions.
- [Ordering instructions](https://jlcpcb.com/help/article/instructions-for-ordering):
  cutouts on the same profile layer, recommended 0.15mm profile line, silk rules,
  mask interpretation, and the requirement to put important notes in the remarks
  field rather than supplementary files inside the Gerber ZIP.
- [Multi-colour process](https://jlcpcb.com/help/article/how-to-design-multi-color-silkscreen-using-easyeda):
  EasyEDA-only colour files and supported white-board / ENIG options.
- [PCB marking](https://jlcpcb.com/help/article/how-to-mark-on-pcb): default no mark;
  order-number placeholder discontinued since May 2026.
- [Metal-core PCB service](https://jlcpcb.com/pcb-fabrication/metal-core-pcb):
  aluminium-core PCB process, distinct from a sheet-metal faceplate.
- [JLCCNC sheet-metal service](https://jlccnc.com/sheet-metal-fabrication): 5052
  aluminium and anodising, silkscreen and laser-marking alternatives.

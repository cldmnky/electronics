# JLCPCB fabrication check: limits, geometry and reports

Status: **prototype geometry check.** It compares the built circuit JSON with
JLCPCB's published capabilities and prints the `tsci check` reports. It is **not
a CAM/DFM approval**, not a first-article inspection and not a fabrication
release. `make release` additionally requires the physical acceptance items in
[acceptance.md](acceptance.md).

## Primary source and the limits used

Source: <https://jlcpcb.com/capabilities/pcb-capabilities>, retrieved
2026-11-04; saved copy SHA-256
`5c5e26084f12287b271ee8219a8e4b2ad1adb35bddd71d261c3cf61854f75dcb`.
The boards are 2-layer, 1 oz, 1.6 mm FR-4.

| Item | Published value (quoted) | Used in `fab_check.py` |
|---|---|---|
| Track width / spacing (1 oz) | "Min. track width and spacing (1 oz) 0.10 / 0.10 mm ... 1- and 2-layer" | min trace 0.10 mm |
| Via hole / pad | "Min. Via hole size/diameter 0.15/0.25mm" | 0.15 / 0.25 mm |
| Preferred via hole | "Preferred Min. Via hole size: 0.2mm"; "0.2mm or 0.25mm hole size with via diameter less than 0.45mm, will cost more" | project target 0.20 / 0.45 mm |
| PTH annular ring (2-layer, 1 oz) | "Recommended 0.25 mm or above; absolute minimum 0.18 mm" | 0.18 mm fail, 0.25 mm target |
| Plated drill range (2-layer) | "Drill Diameter ... 2-layer: 0.15 - 6.3 mm" | 0.15–6.3 mm |
| Plated slot width (2-layer) | "Min. Plated Slots Width 2-layer: 0.5mm" | 0.50 mm |
| Non-plated hole | "Min. Non-plated holes 0.50mm" | **0.50 mm** (the old 0.20 mm constant was not a published limit) |
| Copper to routed edge | "Copper clearance from routed board edges: >=0.2 mm" | board-edge rule 0.20 mm |
| Pad to track (1 oz) | "Pad to track clearance (1oz) 0.1mm" | 0.10 mm |
| SMD pad to pad, different nets | "SMD pad to pad clearance (different nets) 0.15mm" | 0.15 mm |
| Board thickness set | "Thickness for FR4 are: 0.4/0.6/0.8/1.0/1.2/1.6/2.0 mm" | 1.60 mm |
| Minimum board size | "Minimum Dimensions FR4 ...: 3 x 3 mm" | 3 × 3 mm |

Two severities are printed: **FAIL** is below a published value or missing data;
**TARGET** is inside the published floor but below this project's target, or a
design rule that is looser than a published clearance. `make test` reports both;
`make release` (`fab_check.py --strict-targets`) fails on both. The via floor is
deliberately separated from the PTH annular rule: a 0.2/0.3 mm via has a 0.05 mm
ring, which is legal at the published *via* floor — it is not a PTH annular
claim and it is not HDI.

## Geometry results

`python3 tscircuit/scripts/fab_check.py`:

| Item | Front/UI | Rear/base | Limit |
|---|---|---|---|
| Size | 128 × 112 mm | 101.6 × 112 mm | ≥ 3 × 3 mm |
| Layers / thickness | 2 / 1.6 mm | 2 / 1.6 mm | 2 / 1.6 mm |
| Routed elements | 91 traces, 88 vias, 76 SMT pads, 69 plated holes, 28 NPTH | 83 traces, 207 vias, 43 SMT pads, 106 plated holes, 8 NPTH | required data |
| Min trace | 0.150 mm | 0.150 mm | ≥ 0.10 mm |
| Via drill / pad / ring | 0.200 / 0.300 / 0.050 mm | 0.200 / 0.300 / 0.050 mm | ≥ 0.15 / 0.25 / 0.05 mm |
| Via target | 0.200 / 0.300 mm is **below** 0.20 / 0.45 mm target | same | TARGET |
| PTH drill | 1.020–1.300 mm (55 round) + 14 oblong slots | 1.000 mm (106) | 0.15–6.3 mm |
| PTH annular ring | 0.290 mm | 0.250 mm | ≥ 0.18 mm (recommend 0.25) |
| Plated slot width | 1.20 mm | — | ≥ 0.50 mm |
| NPTH | 0.900–2.700 mm | 2.700 mm | ≥ 0.50 mm |
| Min SMT pad | 0.600 mm | 0.600 mm | ≥ 0.25 mm (project floor) |
| Board-edge rule | 0.20 mm | 0.20 mm | ≥ 0.20 mm |

Open TARGET items (both boards): the via pad is 0.30 mm, below the 0.45 mm
target, so the electrical work is expected to retarget 0.20 mm drill / 0.45 mm
pad (ring 0.125 mm). The SMD pad-to-pad **rule** is 0.10 mm, below the published
0.15 mm different-net value; a rule is not measured copper, so it must be
confirmed in the JLCPCB DFM preview (acceptance item `scope_dfm_cpl`).

Missing data fails instead of passing quietly: no board element, no traces, no
vias, no pads, no plated holes, a route segment or pad without a size, or an
unknown plated-hole shape all count as failures.

## `tsci check` reports

`bun run scripts/check_reports.ts` runs six reports on each board with both entry
files named explicitly (`front.circuit.tsx`, `index.circuit.tsx`), then judges
the text — not the exit code:

| Report | Front/UI | Rear/base |
|---|---|---|
| source | 0 errors, 0 warnings | 0 errors, 0 warnings |
| pin_specification | 0 errors, 9 warnings (passive U5/U6/J5) | 0 errors, 0 warnings |
| netlist | 0 errors, 0 warnings | 0 errors, 0 warnings |
| placement | 0 errors, 0 warnings; **exit code 1** from three informational "rotate 180 degrees" notes (C2, R9, R10) | 0 errors, 0 warnings, exit 0 |
| shorts | "No shorts detected" | "No shorts detected" |
| pcb-style | "No PCB style issues detected" | "No PCB style issues detected" |

- The nine front `pin_specification` warnings are the known passive-control ones
  (U5 nav switch, U6 encoder, J5 interconnect declare no power/ground role). A
  **new** warning or any error fails; the set is pinned in
  `tscircuit/lib/reports.ts`.
- The placement exit code is parsed, not trusted: only the pinned informational
  rotations may exit 1, and only when the counters read 0/0. Zero output, a
  crash line, a missing counter, a missing marker, an unexpected flagged
  component, an unexpected issue text or a non-zero exit on any other report
  fails.
- The upstream autorouter line "Could not save autorouting paths ... unique PCB
  port" still appears on both boards. It is printed as a known note and remains
  a manufacturing acceptance item (`autoroute_upstream_warning`); it is not
  counted as a pass.
- The built `dist/` JSON is also checked for emitted error elements and for
  unconnected pins. The pins the design leaves open (tact switch pin 2/4 pairs,
  unused Pi/Pico header pins) are pinned by refdes and pin; a new unconnected
  pin fails.

## Silkscreen

The LCD assembly marks are 0.15 mm lines / 0.9–1.2 mm text. Library-generated
reference designators use 0.4 mm text and some footprint outlines use 0.10 mm
strokes; JLCPCB's standard silkscreen process may thin or omit those, so do not
treat 0.4 mm library text as production documentation. `make release` parses the
exported Legend Gerbers and fails if any silk leaves the board outline (0.5 mm
guard).

## Front panel

`panel.circuit.tsx` builds a bare 26HP × 3U FR-4 panel: no copper, no components,
no solder mask, white silkscreen, ENIG. It has the LCD/control/USB/TRS cutouts and
three 4 mm LED light-pipe holes. This is a **panel PCB**, not part of the
electrical netlist. Verify the "no solder mask both sides" option with JLCPCB
before ordering.

## Not covered

No solder-mask aperture, plating thickness, drill-to-copper registration, panel
rail tolerance, impedance, EMC, assembly clearance or first-article fit is
checked here. The USB-C 5 A feedthrough, the display sample, the light-pipe
length, the encoder solder process and every mechanical clearance remain
physical tests.

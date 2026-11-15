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

`python3 tscircuit/scripts/fab_check.py` compares one build with the limits
above. The autorouter is re-run on every build, so trace, via and route counts
are build-dependent; do not quote a count from an older run. Quote a number
only from a run of this script on the tree in front of you.

Invariants that the script checks on every run:

| Item | Front/UI | Rear/base | Limit |
|---|---|---|---|
| Size | **150 × 112 mm** (U-shaped) | **148 × 112 mm** | ≥ 3 × 3 mm |
| Layers / thickness | 2 / 1.6 mm | 2 / 1.6 mm | 2 / 1.6 mm |
| Track width and spacing | checked on the routed copper | checked on the routed copper | ≥ 0.10 mm |
| Via drill / pad | project target 0.20 / 0.45 mm (`TARGET_VIA_DRILL` in the script) | same | ≥ 0.15 / 0.25 mm |
| PTH annular ring | checked on the built pads | checked on the built pads | ≥ 0.18 mm (recommend 0.25) |
| Plated drill range | checked per hole | checked per hole | 0.15–6.3 mm |
| Plated slot width | checked per slot | none designed | ≥ 0.50 mm |
| NPTH | checked per hole | checked per hole | ≥ 0.50 mm |
| Copper to routed edge | 0.20 mm rule | 0.20 mm rule | ≥ 0.20 mm |
| Smallest SMT pad | project floor | project floor | ≥ 0.25 mm (project floor, not a published limit) |

The rear board's probe points are authored as `platedhole_d0.8mm_pd1.6mm`
(0.80 mm drill, 1.60 mm pad, 0.40 mm ring, bare copper, `doNotPlace`);
`tscircuit/lib/probe-points.ts` is their source. The SMD pad-to-pad and
track-to-pad **rules** are settings, not measured copper, so the actual copper
still has to be confirmed in the JLCPCB DFM preview (acceptance item
`scope_dfm_cpl`).

Missing data fails instead of passing quietly: no board element, no traces, no
vias, no pads, no plated holes, a route segment or pad without a size, or an
unknown plated-hole shape all count as failures.

## `tsci check` reports

`bun run scripts/check_reports.ts` runs six reports on each board with both entry
files named explicitly (`front.circuit.tsx`, `index.circuit.tsx`), then judges
the text - not the exit code:

| Report | What the gate requires on each board |
|---|---|
| source | no error and no warning |
| pin_specification | no error and no warning |
| netlist | no error and no warning |
| placement | no error, no warning, no flagged rotation outside the pinned set |
| shorts | "No shorts detected" |
| pcb-style | "No PCB style issues detected" |

- The allowed warning set is empty in `tscircuit/lib/reports.ts`: U5/U6 are
  declared as switches with per-pin attributes, so no passive-control warning
  exists. A **new** warning or any error fails.
- The placement exit code is parsed, not trusted: only pinned informational
  rotations may exit 1, and only when the counters read 0/0. The pinned set is
  empty. Zero output, a crash line, a missing counter, a missing marker, an
  unexpected flagged component, an unexpected issue text or a non-zero exit on
  any other report fails.
- The upstream autorouter line "Could not save autorouting paths ... unique PCB
  port" appears on both boards. It is printed as a known note and remains a
  manufacturing acceptance item (`autoroute_upstream_warning`); it is not
  counted as a pass.
- The built `dist/` JSON is also checked for emitted error elements and for
  unconnected pins. The pins the design leaves open (tact switch pin 2/4 pairs,
  unused Pi/Pico header pins) are pinned by refdes and pin in the script, so a
  new unconnected pin fails instead of joining the set silently.

## Silkscreen

The LCD assembly marks are 0.15 mm lines / 0.9–1.2 mm text. Library-generated
reference designators use 0.4 mm text and some footprint outlines use 0.10 mm
strokes; JLCPCB's standard silkscreen process may thin or omit those, so do not
treat 0.4 mm library text as production documentation. `make release` parses the
exported Legend Gerbers and fails if any silk leaves the board outline (0.5 mm
guard).

## Front panel

`panel.circuit.tsx` builds a bare 30HP × 3U FR-4 panel (152.0 × 128.5 mm): no copper, no components,
no solder mask, white silkscreen, ENIG. It has the LCD/control/USB/TRS cutouts and
three 4 mm LED light-pipe holes. This is a **panel PCB**, not part of the
electrical netlist. Verify the "no solder mask both sides" option with JLCPCB
before ordering.

## Not covered

No solder-mask aperture, plating thickness, drill-to-copper registration, panel
rail tolerance, impedance, EMC, assembly clearance or first-article fit is
checked here. The USB-C 5 A feedthrough, the display sample, the light-pipe
length, the encoder solder process and every mechanical clearance remain
physical tests. The probe points are checked as copper only: that a probe tip
can reach them inside the assembled stack is **not** verified (acceptance item
`probe_point_access`).

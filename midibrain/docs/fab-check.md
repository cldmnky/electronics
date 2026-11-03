# JLCPCB DRC/ERC and fabrication check

Reference: [JLCPCB FR4 fabrication capabilities](https://jlcpcb.com/pcb-fabrication/fr4-pcb), retrieved for this review. The boards are 2-layer, 1 oz, 1.6 mm FR-4.

## JLCPCB limits used

| Limit | JLCPCB 2-layer, 1 oz |
|---|---|
| Minimum track width/spacing | 0.10 / 0.10 mm |
| Minimum via hole / pad | 0.15 / 0.25 mm |
| Minimum plated slot | 0.50 mm |
| Minimum plated drill | 0.15 mm |
| Minimum non-plated hole | 0.20 mm |
| Copper-to-board-edge rule used | 0.20 mm |
| Board thickness | 0.2–3.2 mm |
| Board size | ≥ 3 × 3 mm |

## Automated results

`python3 scripts/fab_check.py`:

| Board | Size | Layers | Thickness | Min trace | Min via hole/pad | Via ring | Plated hole/slot | SMT pad |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| Front/UI | 128 × 112 | 2 | 1.6 | 0.150 | 0.200 / 0.300 | 0.050 | 1.020 / 1.200 | 0.600 |
| Rear/base | 101.6 × 112 | 2 | 1.6 | 0.150 | 0.200 / 0.300 | 0.050 | 1.000 / — | 0.600 |

All copper/board checks pass with the limits above. Board-edge rule is set to **0.20 mm** on both boards.

`tsci check` results:

| Check | Front/UI | Rear/base |
|---|---|---|
| source | 0 errors | 0 errors |
| netlist | 0 errors | 0 errors |
| pin specification | 0 errors, 9 warnings (passive U5/U6/J5) | 0 errors |
| placement | no placement issues | no placement issues |
| shorts | no shorts | no shorts |
| PCB style | no style issues | no style issues |

The front `pin_specification` warnings are because the passive nav switch, encoder and interconnect do not declare power/ground roles; the TLP2362 pins were explicitly attributed. The rear schematic-placement checker still reports label/alignment suggestions, not electrical errors. The autorouter emits `Could not save autorouting paths ... unique PCB port` on both boards; the final boards have routed copper and the shorts check is clean, but this warning must be reviewed before release.

## Silkscreen

The LCD assembly marks are 0.15 mm lines / 0.9–1.2 mm text. Library-generated reference designators use 0.4 mm text and some footprint outlines use 0.10 mm strokes. JLCPCB’s standard silkscreen process may thin or omit those; the important LCD dimensions are deliberately larger. Do not treat the 0.4 mm library text as production documentation.

## Front panel

`panel.circuit.tsx` builds a bare 26HP × 3U FR-4 panel: no copper, no components, no solder mask, white silkscreen, ENIG. It has the LCD/control/USB/TRS cutouts and three 4 mm LED light-pipe holes. This is a **panel PCB**, not part of the electrical netlist. Verify the “no solder mask both sides” option with JLCPCB before ordering.

## Not covered

This is not a JLCPCB CAM/DFM approval. It does not verify solder-mask apertures, annular-ring plating, drill-to-copper registration, panel rail tolerances, assembly clearances, impedance, EMC or first-article fit. The USB-C 5 A feedthrough, display sample, light-pipe length and all mechanical clearances remain physical tests.

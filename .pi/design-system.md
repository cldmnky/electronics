# Froemix — instrument panel design system

The existing brand is **FROEMIX**, a Daisy Seed DSP instrument. The base board is 16HP; the front board and faceplate are 20HP. Preserve all control positions from `froemix/euro/lib/layout.ts`.

```json
{
  "name": "Froemix / Instrument",
  "panel": {"widthMm": 101.6, "heightMm": 128.5, "thicknessMm": 2},
  "colors": {"substrate": "#141719", "ink": "#f2f0e9", "hardware": "#b8bec2", "screenDemo": "#b5dfe2", "canvas": "#e4e0d8"},
  "manufacturing": {"inkColors": 1, "minStrokeMm": 0.2, "minLegendHeightMm": 1.2, "recommendedLegendHeightMm": 1.5, "edgeClearanceMm": 0.75, "hardwareClearanceMm": 0.6},
  "typography": {"brand": "geometric uppercase lettering", "legends": "uppercase single-stroke sans", "numerals": "tabular instrument numerals"},
  "spacingMm": {"fine": 0.5, "small": 1, "medium": 2, "large": 4},
  "preview": {"canvasMarginPx": 24, "cornerRadiusPx": 18, "minimumTouchPx": 44}
}
```

## Components and rules

- The printed design uses **one white silkscreen layer on black solder mask**, not preview gradients or OLED colors. The researched manufacturing limits can tighten these defaults.
- Reuse the exact geometry of all 31 existing holes and the OLED opening. Do not move electronic parts or change the mating connectors for styling.
- Show 6 generic controls as P1–P6, navigation as NAV/SELECT, and the separate button as MODE. Do not invent firmware features.
- Distinguish input from output by shapes, line density, or a white outline frame, never by color alone. Group audio, CV, sync, and MIDI visibly.
- Keep legends legible after jack nuts, knobs, washers and caps are installed. Default hardware masks are conservative assumptions, not measured component dimensions.
- Use a coherent hierarchy: a prominent FROEMIX wordmark, short section headings, clear individual port legends, and small rear-side revision information.
- Add pot tick marks outside the knob silhouettes where space allows. Do not imply calibrated gains or voltage limits.
- Previews must separate bare panel and assembled hardware. Any OLED image is an illustrative mockup, not firmware delivered by this change.
- Use pure SVG in millimetres for printable previews. Production artwork must originate from tscircuit/data primitives, not a screenshot or external font.
- Include a 1:1 print check and keep unresolved physical-fit questions visible in ordering notes. Do not call a panel order-ready before these are checked.
- Artifact designs use responsive HTML, meaningful `data-pid` attributes and 3–8 tweak controls. Two design directions share one artifact group.

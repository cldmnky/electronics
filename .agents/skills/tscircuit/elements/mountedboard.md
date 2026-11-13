# `<mountedboard />` — NOT SUPPORTED (local correction)

**Do not use this element. The previous contents of this file were wrong.**

This file used to document `<mountedboard>` with `boardToBoardDistance` and
`mountOrientation="faceUp"` around a nested `<board>`. Both the documentation
and the element are wrong for the installed tscircuit.

## Evidence (measured 2026-11-13, tscircuit 0.0.3169)

| Check | Result |
|---|---|
| `https://docs.tscircuit.com/elements/mountedboard` | **HTTP 404** — there is no such page. Confirmed against `https://docs.tscircuit.com/sitemap.xml`, which lists the whole element set. |
| Board nested inside a board | `Fatal error [circuit_generation_failed]: Nested boards are not supported: found board "unnamed_board1" inside board "unnamed_board1"` |
| Layout prop `boardToBoardDistance` | Does not exist on `<board>`. The board props table lists `mountedTo`, `mountGap`, `mountRotation`, `mountOrientation` and **no Z/height offset**. |

## What to use instead

Multi-board *fabrication* panels are `<panel>` / `<subpanel>`. For a 3D
mechanical assembly with several boards at different Z, read:

- `https://docs.tscircuit.com/guides/advanced-assembly` — "Mounting keeps the
  PCB in the XY plane **at Z = 0**", and "A connected assembly can have **one
  PCB anchor**". Other boards cannot be stacked as additional PCB anchors.
- `https://docs.tscircuit.com/elements/assembly-printedpart` — a mechanical body
  with **no footprint and no schematic symbol**. This is the correct primitive
  for a printed part, a spacer, or a board-shaped envelope that is not itself a
  PCB.
- `https://docs.tscircuit.com/elements/assembly-subassembly` (`cadassembly`
  alias) and `https://docs.tscircuit.com/elements/assembly-part`.
- `https://docs.tscircuit.com/elements/cadmodel` — `modelUrl`, `stepUrl`,
  `positionOffset`, `rotationOffset`, `zOffsetFromSurface`, `size`,
  `showAsTranslucentModel`, `modelUnitToMmScale`.

A worked example of the supported pattern — one board as the Z = 0 anchor plus
the other layers as `assembly.printedpart` wireframe envelopes at their real Z —
is `midibrain/tscircuit/assembly.circuit.tsx`.

## Why this note lives in a vendored file

The skill under `.agents/skills/tscircuit/` is vendored, so a re-vendor will
overwrite this correction. When that happens, check whether
`docs.tscircuit.com/elements/mountedboard` exists and whether nested boards
build; if they do, delete this file and restore the upstream content.

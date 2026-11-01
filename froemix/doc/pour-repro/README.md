# Copper pour crash — minimal repro and workaround

`index.circuit.tsx` in this folder is a ~28-line standalone repro for the
tscircuit copper-pour failure that used to block the GND plane on the froemix
base board. It reproduces on this tree's exact dependency set.

## Versions (reproduced on this tree)

| package | version |
| --- | --- |
| `tscircuit` | 0.0.3044 |
| `@tscircuit/cli` | 0.1.2427 |
| `@tscircuit/core` | 0.0.2242 (also fails on 0.0.2245, the latest) |
| `@tscircuit/props` | 0.0.715 |
| `@tscircuit/checks` | 0.0.267 (also fails on 0.0.269) |
| `@tscircuit/copper-pour-solver` | 0.0.67 (latest) |
| `@tscircuit/circuit-json-util` | 0.0.128 |
| `@flatten-js/core` | 1.6.15 |
| `flatbush` | 4.7.0 |

bun 1.3.3, node v22.22.2. The crash is not platform-specific — it is pure
JavaScript in the render phase.

## Run it

```bash
cd froemix/doc/pour-repro
ln -s ../../tscircuit/node_modules node_modules   # reuse the installed deps
bunx tsci build
```

Expected on this tree: a fatal error and `Circuits 0 passed 1 failed`.

```
TypeError: undefined is not an object (evaluating 'node.item.key.low')
    at minmax_tree_calc_distance (@flatten-js/core/dist/main.mjs:9168:83)
    at shape2planarSet (@flatten-js/core/dist/main.mjs:9197:53)
    at distanceTo (@flatten-js/core/dist/main.mjs:8375:61)
    at copperPolygonsTouch (@tscircuit/circuit-json-util/dist/index.js:3218:139)
    at findFloatingCopper (@tscircuit/core/dist/index.js:159043:11)
    at Board_doInitialPcbCopperPourCleanup (@tscircuit/core/dist/index.js:159078:47)
Fatal error [circuit_generation_failed]: undefined is not an object (evaluating 'node.item.key.low')
```

`findFloatingCopper` is the "delete copper islands that touch nothing" pass that
runs right after the pour solver. It is not the manifold-3d boolean pipeline
(`copper-pour-solver` 0.0.67 is already the new engine) — it is the older
flatten-js union-find over pad/trace/via/pour polygons.

## Root cause (verified)

`@tscircuit/core` emits `pcb_plated_hole` **without** `hole_offset_x` /
`hole_offset_y` whenever the props are unset:

```js
hole_offset_x: props.holeOffsetX,     // undefined, not 0
hole_offset_y: props.holeOffsetY,
```

(its own zod schema declares `hole_offset_x: z.number().default(0)`, but the
element that reaches the circuit JSON is not defaulted — on this tree **all 129**
`pcb_plated_hole` elements lack both keys).

`@tscircuit/circuit-json-util`'s `getPlatedHolePolygon` then does

```js
const center = { x: pad.x + pad.hole_offset_x, y: pad.y + pad.hole_offset_y };
...
subtractHole(outer, circlePolygon(center, pad.hole_diameter / 2));
```

so `center` is `{NaN, NaN}` and the subtracted hole is a polygon whose arcs have
NaN endpoints. `Polygon.box` becomes all-NaN, and the flatten-js interval tree
built over those edges cannot form keys → `node.item.key` is `undefined` →
the TypeError above. Note the shape gate: only `circular_hole_with_rect_pad`,
`pill_hole_with_rect_pad`, `rotated_pill_hole_with_rect_pad` and
`hole_with_polygon_pad` take that branch. A plain `circle` plated hole builds its
polygon from `pad.x/pad.y` directly and is unaffected — which is why the failure
needs *both* a pour on a net that owns a rect-pad plated hole *and* at least one
more conductor on that net (a pour plus one pad alone builds fine; the crash
needs a second polygon to compare against).

Instrumented evidence from the frozen base board (crash before the workaround):

```
=== copperPolygonsTouch THREW: undefined is not an object (evaluating 'node.item.key.low')
A: {"ctor":"Polygon","faces":2,"badShapes":[{"name":"Arc","vals":[null,null,null,null]}],"box":[null,null,null,null],"empty":false}
conductor: {"kind":"platedhole","id":"pcb_plated_hole_40","layers":["top","bottom","inner1","inner2"]}
```

`pcb_plated_hole_40` is CN4 pin1 — the only `circular_hole_with_rect_pad` pad on
the GND net (the other seven rect pads are CN1/CN2/CN3 pin1, Daisy pin1, H1
pin1, J1 pin1, J2 pin1, none of them GND).

## Workaround (in use on the base board)

Give the plated hole explicit zero offsets — zero *is* the documented default, so
pad geometry does not change:

```tsx
<platedhole shape="circular_hole_with_rect_pad" holeDiameter="0.9mm"
  rectPadWidth="1.52mm" rectPadHeight="1.4mm" portHints={["pin1"]}
  holeOffsetX="0mm" holeOffsetY="0mm" />
```

With that line added to `index.circuit.tsx` here the build passes and writes 1
`pcb_copper_pour`. On the real board the same change on the 10 rect-pad plated
holes gives 8 pour regions on the bottom and a 12/12 green `bun run gate`
(`froemix/AGENTS.md` §3).

## Proper upstream fix (either is fine)

1. `@tscircuit/core`: default `hole_offset_x`/`hole_offset_y` to `0` when
   emitting `pcb_plated_hole` (the zod schema already says so) — this is the
   same class of bug as core#3905 / core#3976 ("omitted hole offsets → NaN hole
   vertices reach the Flatten interval tree"), which were fixed for the
   pill/rect plated hole **solver** path but not for this connectivity pass.
2. `@tscircuit/circuit-json-util` `getPlatedHolePolygon`: use
   `pad.hole_offset_x ?? 0` / `pad.hole_offset_y ?? 0` (and/or validate the
   result: `Polygon.box` with NaN members should throw a named error instead of
   producing a poisoned interval tree).

Related, still true on this toolchain: `<board automaticPoursEnabled />` is a
silent no-op — the prop exists in `@tscircuit/props` but `@tscircuit/core`
0.0.2242/0.0.2245 never reads it and produces 0 `pcb_copper_pour` elements. An
explicit `<copperpour>` is the only way to get a pour.

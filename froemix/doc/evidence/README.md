# Evidence for the physical acceptance items

`froemix/hardware-acceptance.json` lists every physical item that must be
accepted before a **production** release may build, export or delete anything in
`froemix/fab/`. An item is accepted only when it names an evidence file from
this directory, and that file records a real measurement.

No evidence file exists yet. No bench measurement has been recorded for this
project, so every item is `pending` and every production release stops at the
acceptance gate. That is the intended state, not a bug.

## What a valid evidence file contains

One Markdown file per item, named `doc/evidence/<item-id>.md` (the id exactly as
it appears in `hardware-acceptance.json`, no subdirectories, no symlinks). The
file must:

1. name the item id in its text;
2. contain at least one recorded value with a unit (`12.4 mm`, `118 mA`,
   `41.2 °C`, `0.8 V`, `3.1 N`, …);
3. be a regular, non-empty file of at least 200 bytes inside this directory;
4. be referenced by `"evidence"` in the acceptance record, together with
   `"reviewer"`, `"reviewed_at"` (`YYYY-MM-DD`, not in the future) and
   `"source_fingerprint"` equal to the record's `hardware_fingerprint`.

The validator also accepts an optional `"evidence_sha256"` per item; when it is
present, the file content must match it.

Rejected on purpose: an absolute path, a path containing `..`, a symlink, a
directory, an empty file, a file outside `doc/evidence/`, a placeholder reviewer
(`TBD`, `n/a`, `-`), a future or impossible date, and an evidence file that
records no value with a unit. A prose opinion is not evidence, and a
requirement copied into an evidence file is not evidence either.

## How the hardware fingerprint works

`bun tools/release/gate.ts --fingerprint` (from `froemix/`) prints the current
value. It is a SHA-256 over the hardware definition:

- the base board: `tscircuit/index.circuit.tsx`, every `.tsx` in
  `tscircuit/sections/`, every `.ts`/`.tsx` in `tscircuit/lib/`;
- the euro board and panel: `euro/index.circuit.tsx`,
  `euro/panel.circuit.tsx`, `euro/sections/*.tsx`, `euro/lib/*.ts(x)`;
- the panel and CAD algorithms: `euro/scripts/*.ts`
  (`panel_release.ts`, `panel_check.ts`, `panel_preview.ts`,
  `mechanical_release.ts`, `mechanical_check.ts`);
- both `package.json` files and both `bun.lock` files;
- the fabrication order settings (`jlc-order.json`);
- the curated sourcing and part maps (`tools/kicad2tscircuit.py` — which
  carries `EXTRA_COMPONENTS`, `PART_OVERRIDES` and the footprint/3D-model maps —
  `tools/lcsc_models.json`, `tools/sourcing-extras.csv`).

A file **added** under one of the hashed directories changes the value, so a new
section or a new footprint import cannot slip in under old evidence. A
**missing** mandatory file or directory fails the gate instead of shrinking the
fingerprint.

Not hashed on purpose: tests (`*.test.ts`, `test_*.py`), documentation
(`doc/`, `README`s), generated output (`dist/`, `fab/`, `checks/`,
`__snapshots__/`), the acceptance record itself, the frozen KiCad reference
(`tmp/`) and the SPICE decks (`tools/sim/`). Changing any of those does not
invalidate physical evidence; changing anything above does, because a
CAD-algorithm, order-setting or part-selection change can move a measured or
DFM result even when the board files are identical.

## Template

```markdown
# Evidence: stack_11mm

- item: stack_11mm
- hardware fingerprint: <from `bun tools/release/gate.ts --fingerprint`>
- date: YYYY-MM-DD
- reviewer: <name>
- sample: <board revision, serial/batch, parts used>

## Measurement

| Check | Method | Result | Requirement |
|---|---|---|---|
| Spacer stack, base to euro | caliper, 4 corners | <value> mm | 11 mm clear |
| Header contact engagement | visual + pull test | <value> mm | full post engagement |
| Front-board rear tails | caliper | <value> mm | <= 1 mm |
| J1 + shunts | caliper | <value> mm | <= 9.5 mm |

## Notes

- What was measured, with which instrument, at which load/ambient, and what
  remains uncertain.
```

## Rules

- Do not invent, estimate or copy a measurement. If it was not measured, the
  item stays pending.
- Separate the **recorded measurement** from the **requirement**. The table in
  `doc/acceptance.md` states requirements; this directory holds what was
  actually measured.
- Keep third-party primary sources compact: a URL, a retrieval date, a SHA-256
  and the quoted specification are enough. Do not commit whole third-party
  documents.
- An engineering prototype can produce the material that later becomes
  evidence; it does not accept an item by itself. See
  [acceptance.md](../acceptance.md).

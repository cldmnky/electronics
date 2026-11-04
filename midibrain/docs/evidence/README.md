# Evidence for the physical acceptance items

`midibrain/hardware-acceptance.json` lists every physical item that must be
accepted before a **production** release can export fabrication files. An item
is accepted only when it names an evidence file from this directory.

## What a valid evidence file contains

One Markdown file per item, named `docs/evidence/<item-id>.md` (lower-case item
id, no subdirectories, no symlinks). The file must:

1. name the item id in its text,
2. contain at least one recorded value with a unit (for example `12.4 mm`,
   `3.1 mA`, `41.2 °C`, `0.8 V`),
3. be a regular non-empty file inside this directory, and
4. be referenced by `"evidence"` in the acceptance state, with `"reviewer"`,
   `"reviewed_at"` (YYYY-MM-DD, not in the future) and `"source_fingerprint"`
   equal to the state's `hardware_fingerprint`.

The validator also accepts an optional `"evidence_sha256"` per item; when it is
present the file content must match it.

## How the hardware fingerprint works

`bun run scripts/release_gate.ts --fingerprint` prints the current value. It is
a SHA-256 over the canonical PCB and panel sources (`tscircuit/*.circuit.tsx`,
`tscircuit/lib/layout.ts`, `pin-map.ts`, `jack.tsx`, `panel-art.ts`,
`tscircuit/imports/*.tsx`, `tscircuit/package.json`, `tscircuit/bun.lock`) and
the mechanical TS-to-CAD bridge (`tscircuit/scripts/export-mechanical.ts`,
`mechanical/layout.json`), the CAD algorithm (`mechanical/feedthrough_bracket.py`,
`mechanical/bracket_geometry.py`), the fabrication order settings
(`jlc-order.json`) and the curated cable/hardware selection
(`tscircuit/scripts/sourcing-extras.csv`).
A change to any of those makes old evidence stale, so a stale report cannot
approve changed hardware. A documentation-only commit (including an acceptance
record) does not change the fingerprint.

## Template

```markdown
# Evidence: jack_retention

- item: jack_retention
- hardware fingerprint: <from --fingerprint>
- date: YYYY-MM-DD
- reviewer: <name>
- sample: <part number and batch/serial>

## Measurement

| Check | Method | Result | Requirement |
|---|---|---|---|
| Barrel depth at panel | caliper | 5.98 mm | 6.0 mm nominal |
| Pull-out force | spring scale | 24 N | >= 10 N |

## Notes

- What was measured, with which instrument, and what remains uncertain.
```

## Rules

- Do not invent or copy a measurement. No file here exists yet, because no
  bench measurement has been recorded for this project.
- Separate the **recorded measurement** from the **requirement**. A requirement
  is not evidence.
- Keep third-party primary sources compact: a URL, a retrieval date, a SHA-256
  and the quoted specification are enough. Do not commit whole third-party
  documents.
- A prototype/engineering review may produce the material that later becomes
  evidence, but it does not accept an item by itself. See
  [acceptance.md](../acceptance.md).

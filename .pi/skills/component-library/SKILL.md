---
name: component-library
description: Manage the repository-wide electronics component library in library/. Use when adding or updating a reusable part, sourcing from DigiKey Sweden, Electrokit or Conrad Sweden, recording a datasheet, footprint or STEP model, matching a project BOM, or promoting a draft part to preferred or alternate. Covers the CSV schema, CAD provenance, shop article numbers and the validation gates.
---

# Component library

`library/components.csv` is the shared parts list for every project in this
repository. `library/parts_library.py` validates it, searches it, and matches
project BOMs against it. The library owns the part identity, the shop article
numbers, the CAD assets and the sourcing history; a project must not keep a
private copy of the same part.

Read `library/README.md` (columns and rules) and `library/cad/README.md`
(asset layout and verification) before editing. The root `AGENTS.md` states
the same contract at a higher level.

## Non-negotiables

- One row is one orderable part. Keep the `id` stable; record `manufacturer`
  and `mpn` when they are known. Never invent an MPN, a pinout or a footprint.
- Never copy an article number between shops. A Conrad article, a DigiKey
  part number and an Electrokit `Art.no` are different namespaces.
- A price is a dated snapshot, not a quote. Confirm price, VAT, minimum order
  and stock at checkout. Do not record stock as a permanent fact.
- `draft` is a lead: do not design it in and do not buy it. If an existing
  project already uses it, close the datasheet/CAD gate before the next order.
  `preferred` and `alternate` are the only design-ready states. `avoid`
  records a rejection.
- A `preferred`/`alternate` row needs a datasheet, a footprint (except a
  panel-only `knob`), a STEP model, `cad_source`, `cad_license`, and at least
  one main-shop SKU. The tool enforces this; do not weaken the check to
  promote a part.
- LCSC is stock bought in and a JLCPCB assembly source, not a main source for
  a new hand-assembly design. The three main shops are peers: use whichever
  stocks the exact part. Electrokit is usually fastest, DigiKey has the
  broadest catalogue, and Conrad is strongest for control and panel hardware.
- Do not scrape Conrad: it answers automated clients with a Cloudflare
  challenge. Record its article numbers by hand from the product page.
- Do not put credentials or vendor CAD with a non-redistributable license in
  the repository. Record the URL in `cad_source` and keep the part `draft`.
- Check `@tscircuit/common` (MIT) before hand-writing a tscircuit wrapper. It
  is the reference for pin labels, footprinter strings, custom footprints and
  `cadModel`; its `jlcpcb` numbers are not main-shop sourcing evidence. See
  `references/tscircuit-common.md`.

## Adding or updating a part

### 1. Check what already exists

```bash
python3 library/parts_library.py check
python3 library/parts_library.py find "10k"
python3 library/parts_library.py find "PJ-301M"
```

`find` searches id, description, manufacturer, MPN, value, package, rating and
notes. If a row already exists, update it rather than adding a near-duplicate.

### 2. Source the part

Print the shop search links for one term or for the pending list:

```bash
python3 library/parts_library.py links "potentiometer 10k"
python3 library/parts_library.py links "PJ-301M"
python3 library/parts_library.py links --from library/searches.txt
python3 library/parts_library.py links --from library/searches.txt --shop conrad
```

Open the exact product page at each main shop. Find the variant that matches
the package, value, tolerance, rating and pinout. A family page is not enough;
the article number must be the exact orderable variant.

For DigiKey, the optional live Product Information v4 query in
`froemix/tools/source_parts.py --digikey-live` can return candidates. It needs
credentials in the environment and is not an approval. Never write credentials
to a file or to the library.

### 3. Capture the facts

For every shop that stocks the part, record:

- exact shop article number and product URL;
- price and currency on the date read, with the shop's VAT convention
  (Electrokit includes VAT; DigiKey Sweden and Conrad exclude it; LCSC is EUR);
- minimum order quantity;
- `manufacturer` and `mpn` from the page or datasheet;
- `datasheet` URL (prefer the manufacturer's PDF);
- `rating`, `tolerance`, `value`, `package` exactly as the BOM should call
  them;
- a note for anything unusual: variant, packaging, stock observation,
  substitute relationship, missing shop.

If a shop does not stock the part, leave its three columns empty. Do not fill
them with another shop's number.

### 4. Add the CAD

Every ready part needs a footprint and a STEP model under `library/cad/`
(a panel-only `knob` needs the STEP model but no PCB footprint). CSV paths are
relative to `library/` (for example
`cad/res-10k-025w/footprint.kicad_mod`).

Preferred source order:

1. the manufacturer's CAD for the exact MPN;
2. a vendor/shop CAD download whose license allows redistribution;
3. the KiCad official libraries for a genuinely generic THT package
   (`/Applications/KiCad/KiCad.app/Contents/SharedSupport/footprints` and
   `.../3dmodels` on macOS);
4. a tscircuit-native wrapper modeled on `@tscircuit/common` when the part
   will be placed from tscircuit;
5. author the footprint from the datasheet when nothing trustworthy exists.

For a tscircuit wrapper, use a `.tsx` footprint with `pinLabels`,
`manufacturerPartNumber`, `supplierPartNumbers` and `cadModel`. Download the
`stepUrl` model into `library/cad/<part-id>/model.step`; do not leave the
library dependent on a remote model. Pin the `@tscircuit/common` version you
read and record it, the MIT license and your changes in `SOURCE.md`.

Use a shared file under `cad/footprints/` and `cad/step/` only when the same
package is truly interchangeable. Otherwise use `cad/<part-id>/` with
`footprint.kicad_mod` (or `.tsx`/`.json`) and `model.step`.

Write `SOURCE.md` in the asset folder, or beside a shared asset, and include:

- part identity (`manufacturer` + `mpn`);
- datasheet URL and retrieval date;
- footprint and STEP source URLs or local library paths;
- license and whether redistribution is allowed;
- SHA-256 for both files (`shasum -a 256 ...`);
- what was verified, and what remains unverified.

Set `cad_source` (an `http(s)` URL) and `cad_license` in the CSV for every row
that has CAD.
Read `library/cad/README.md` for the full checklist and template.

### 5. Add the row and run the gates

Add the row with `status` `draft`, then:

```bash
python3 library/parts_library.py check
python3 library/test_parts_library.py
```

`check` validates ids, categories, statuses, prices, MOQs, dates, shop pairs,
main-shop coverage, asset paths, file suffixes, the STEP header and CAD
provenance. Fix errors; do not edit the checker to make a row pass.

### 6. Verify, then promote

Before changing `draft` to `preferred` or `alternate`, verify against the exact
datasheet/part:

- pin numbering, pin 1 and polarity;
- pitch, pad shape/size, drill, annular ring and pad numbering;
- courtyard, body outline, height and keep-outs;
- lead spacing/diameter for THT, or the mated connector and stack height;
- connector/FPC orientation, mounting holes and cable clearance for modules;
- STEP body, origin, axis, units (mm) and orientation;
- stock/MOQ at the chosen main shop and the current price snapshot.

Record the result in `SOURCE.md` and `notes`. If something is still unknown,
leave the row `draft`; that is a valid outcome. The tool does not prove
geometry or electrical suitability.

## Using the library in a project

Match a BOM before ordering:

```bash
python3 library/parts_library.py bom path/to/bom.csv
python3 library/parts_library.py bom path/to/bom.csv --shop electrokit
python3 library/parts_library.py bom path/to/bom.csv --shop digikey
python3 library/parts_library.py bom path/to/bom.csv --shop conrad
```

A line is `matched`, `ambiguous` (choose one deliberately) or `unmatched`.
With `--shop`, the tool prints a paste-ready order list and raises quantities
to each part's MOQ; it reports `NO SKU` and exits 2 when a line cannot be
filled. Never let a script ignore exit status 2. Do not hand-edit a generated
BOM to hide an unmatched line: add the missing library row or change the
design.

When a project uses a library part, use the library's footprint/STEP (or the
tscircuit equivalent derived from it) and the library's MPN/shop data. If the
project needs a variant, add a new library row with its own id and CAD; do not
mutate the shared part to fit one board.

## tscircuit and `@tscircuit/common`

The repository uses tscircuit. Before writing a part wrapper or a common
subcircuit by hand, read
[`references/tscircuit-common.md`](references/tscircuit-common.md) and check
the upstream source at <https://github.com/tscircuit/common> (MIT).

- Use it for board-level blocks such as `Microcontroller_RP2040`,
  `PowerBoost_MT3608`, `AudioAmplifier3W_PAM8403`, the Xiao/Pro Micro/CM5
  carriers and the `TscircuitLogo`.
- Use its single-part wrappers (`SS34`, `AO3401A`, `RP2040`, etc.) as the
  pattern for a library `footprint.tsx`: pin labels, exact footprint,
  manufacturer part number, `cadModel`.
- Pin the package version in `package.json` and record it in `SOURCE.md`.
  Keep the MIT notice when copying a substantial portion.
- Its `supplierPartNumbers.jlcpcb` values are JLCPCB/LCSC references. They do
  not fill the library's `digikey_sku`, `electrokit_sku` or `conrad_sku`
  columns.
- A common subcircuit is a design block, not an approved BOM. Verify the exact
  parts, footprint, assembly side, main-shop availability and price snapshot
  before ordering.

## Shop notes

- **Electrokit** — public catalogue, SEK including VAT, fast Swedish delivery.
  Good first stop for generic THT passives, displays, headers and dev boards.
- **DigiKey Sweden** — public storefront, SEK excluding VAT, broad catalogue.
  The API is optional and requires a developer application; results are
  candidates, not approvals.
- **Conrad Sweden** — widest control/panel range, SEK excluding VAT, account
  needed to order, Cloudflare blocks automated search. Use the printed search
  link and copy the article number manually.
- **LCSC** — EUR unit prices, delivery from Shenzhen, useful for JLCPCB
  assembly and small connectors the Swedish shops lack. Not a same-week
  source and not a main source for a new hand-built design.

`library/searches.txt` holds search terms for parts the list does not hold yet.
Add a term when it will save the next search; keep comments and one term per
line.

## Done criteria for a library change

- `python3 library/parts_library.py check` exits 0 with no errors.
- `python3 library/test_parts_library.py` passes.
- The new/updated row has a unique id, exact shop article numbers, a dated
  price snapshot and no cross-shop copying.
- A ready row has a datasheet, footprint, STEP, `cad_source`, `cad_license`
  and a main-shop SKU; the files exist, the STEP header is valid, and a
  `SOURCE.md` sits beside the footprint or STEP.
- `SOURCE.md` records provenance and what was verified.
- The project BOM matches the row, or the unmatched line is documented as
  unresolved.

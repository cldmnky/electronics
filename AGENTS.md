# Electronics repository contract

This repository holds several hardware projects (`froemix/`, `midibrain/`) and
one shared component library (`library/`). Read the project's own
`AGENTS.md` before touching it; the rules here apply to every project and to
the library.

## Shared component library

`library/components.csv` is the single source of truth for reusable
through-hole parts. A project BOM chooses from it; it does not keep a private
copy of the same part. When a project needs a part that is missing, add it to
the library first, then use it from the project.

The **main suppliers** are:

1. **Electrokit** (`electrokit.com`) — fast Swedish source, prices include VAT.
2. **DigiKey Sweden** (`digikey.se`) — widest catalogue, prices exclude VAT.
3. **Conrad Sweden** (`conrad.se`) — widest range of control hardware and
   panel parts, prices exclude VAT; its site blocks automated clients, so
   article numbers are recorded by hand.

**LCSC** is a stock source for parts already bought in (mostly small
connectors and control parts) and for JLCPCB assembly. It is not a main source
for a new hand-assembly design.

### Rules

- **One row is one orderable part.** Keep the `id` stable. Record
  `manufacturer` and `mpn` when the shop or datasheet gives them. Never copy an
  article number from one shop to another, and never invent an MPN.
- **A price is a snapshot.** `updated` is the date it was read. Confirm price,
  VAT, minimum order and stock at checkout. Never treat a library price or
  stock figure as a quote or a live fact.
- **Status is a gate.**
  `preferred` — use for new designs; `alternate` — use when the preferred part
  is unavailable; `draft` — a lead, not approved for a new design; close an
  existing use before the next order; `avoid` — retained so the rejection is
  not repeated.
  A `preferred` or `alternate` row must carry a datasheet, a footprint (except
  panel-only `knob` parts), a STEP model, CAD provenance, and at least one
  **main-shop** article number. A `draft` may be incomplete; that is what
  makes it a draft.
- **Every ready part has CAD.** Footprints and STEP models live under
  `library/cad/` and the CSV paths are relative to `library/`. A footprint is
  not accepted from a name alone: verify pin numbering, polarity, pitch, pads,
  drills, courtyard and body against the exact ordered part. A STEP model must
  match the same body, origin, axis and units. Do not invent geometry.
- **Use the tscircuit ecosystem before writing a wrapper by hand.**
  `@tscircuit/common` (MIT, <https://github.com/tscircuit/common>) holds
  tscircuit-native boards and subcircuits with pin labels, supplier part
  numbers, footprinter/custom footprints and `cadModel.stepUrl` values. Use it
  as the reference for a tscircuit footprint or subcircuit, pin the package
  version, and record the source. Its `jlcpcb` numbers are JLCPCB/LCSC
  references, not DigiKey/Electrokit/Conrad sourcing evidence; the library
  still needs the main-shop article numbers and its own STEP file.
- **Provenance and license are part of the part.** `cad_source` is the URL of
  the page or file the CAD came from; `cad_license` names the license. Do not
  commit vendor CAD that forbids redistribution. Do not commit whole datasheets;
  record the URL. See `library/cad/README.md`.
- **A search result is a lead.** The designer checks the datasheet, ratings,
  footprint and mated connector before ordering. The library tool creates
  search links and matches BOMs; it does not approve a substitute, scrape a
  shop, or place an order.
- **BOM matching is exact on value and ranked by footprint.** A project BOM
  must match a library row (`matched`), not silently substitute. Two candidates
  are `ambiguous`; a missing row is `unmatched`. Update the library or the BOM
  deliberately.

### Changing the library

1. Search with
   `python3 library/parts_library.py find TEXT` and
   `python3 library/parts_library.py links TEXT` (or
   `--from library/searches.txt`).
2. Capture the exact MPN/manufacturer, datasheet URL, shop article number,
   price, minimum order, currency/VAT convention and the date.
3. Add or update the footprint and STEP model under `library/cad/`, plus
   `SOURCE.md` with URL, retrieval date, license, SHA-256 and what was
   verified. Use the manufacturer's CAD first, a redistributable vendor file
   second, the KiCad official libraries for a genuinely generic THT package,
   and a tscircuit-native `.tsx` wrapper (modeled on `@tscircuit/common`) when
   the part will be placed from tscircuit. Download the STEP model locally
   instead of leaving a build dependent on a remote `stepUrl`.
4. Add the row as `draft`, then run:
   ```bash
   python3 library/parts_library.py check
   python3 library/test_parts_library.py
   ```
5. Promote to `preferred` or `alternate` only after the datasheet, footprint,
   STEP and mated-part checks pass. The tool rejects a promotion that is
   missing CAD, provenance, or a main-shop SKU.

The detailed workflow, shop behaviour, tscircuit patterns and CAD checklist
are in the `component-library` skill at
`.pi/skills/component-library/SKILL.md` (see also
`references/tscircuit-common.md`). `library/README.md` is the column
reference.

## Purchased stock

`inventory.csv` (repo root) records **parts that have been bought**, one row
per order line, with the shop's own part number, MPN, quantity, unit price,
currency and VAT convention. `INVENTORY.md` describes the schema, the two orders
recorded so far and the known problems with the current stock.

- **A stock row is not an approval.** It means "this is on the shelf". A part may
  only be designed into a board when it also has a `library/components.csv` row
  that passed the library gate. `library/` owns identity and CAD; the inventory
  owns quantities and order history.
- Every row carries `status` and `notes`. The note states the verification state
  ("body 4.3 mm, matches drawing") or the trap ("too short for an 11 mm gap"). A
  row without that is worse than no row.
- Never copy an article number between shops and never invent an MPN. A search
  result stays out of the inventory until the part arrives.
- When a stock part is fitted, link it: `intended_for` names the project and the
  reference designators, and the library row records the shop SKUs.

Connector and interconnect knowledge (pitch families, mating-height arithmetic,
pin-order traps, IDC rules, debug headers, 3.5 mm jack families) lives in the
`connectors` skill at `.pi/skills/connectors/SKILL.md`. Read it before choosing
or replacing any connector.

## Project boundaries

- `froemix/AGENTS.md` and `midibrain/AGENTS.md` are the project contracts;
  their build, release and safety rules override general advice here.
- Do not edit another project's generated fab output to fix a library issue.
- Do not modify `froemix/` while working on `midibrain/` unless the task says
  so.
- Keep experiments and generated artifacts inside the project that owns them.

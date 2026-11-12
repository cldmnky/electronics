# Component library

One list of through-hole parts to reuse across projects: resistors,
capacitors, diodes, LEDs, transistors, DIP parts, headers, character and OLED
displays, and through-hole control hardware (pots, switches, knobs, jacks).
Every part has a stable `id` and its own facts. A ready part also carries a
PCB footprint, a STEP model, a datasheet and the article number it has at each
shop that stocks it. The **main sources** are the three EU ones; LCSC holds
stock that was bought in and is not a main source:

| key | shop | market | currency | role |
| --- | --- | --- | --- | --- |
| `conrad` | Conrad Electronic | Sweden, `conrad.se` | SEK, excluding VAT | main |
| `digikey` | DigiKey Sweden | Sweden, `digikey.se` | SEK, excluding VAT | main |
| `electrokit` | Electrokit | Sweden, `electrokit.com` (English store) | SEK, including VAT | main |
| `lcsc` | LCSC | Shenzhen, `lcsc.com` (JLCPCB group) | EUR, unit price | stock bought in |

The shop column holds the **article number** the shop prints: a Conrad article
number, a DigiKey part number such as `NE555P-ND`, an Electrokit `Art.no` or an
LCSC part number such as `C470294`. Leave it empty when the shop does not stock
the part. Never copy an article number between shops.

Two rows may share category, value and package when they are alternatives, for
example a red 3 mm LED from Electrokit and the one bought in from LCSC. The
`id`, the shop columns and the `status` tell them apart.

The file is `library/components.csv`, one row per part. It is plain CSV so a
spreadsheet opens it and a script reads it. `library/parts_library.py` checks
it, searches it, and matches a project BOM against it.

| column | meaning |
| --- | --- |
| `id` | stable internal name; lowercase letters, digits, dot and dash |
| `category` | one of the categories the checker accepts (`resistor`, `cap-ceramic`, `cap-electrolytic`, `cap-film`, `diode`, `led`, `transistor`, `ic`, `header`, `display`, `jack`, `potentiometer`, `knob`, `switch`, `power`) |
| `description` | human description of the exact part |
| `manufacturer`, `mpn` | exact product identity when known; never guessed |
| `value`, `package`, `rating`, `tolerance` | the facts a BOM and a datasheet use; `value` is the exact BOM match key |
| `datasheet` | an `http(s)` URL or a library-relative file |
| `footprint`, `step` | library-relative CAD paths (see [CAD assets](#cad-assets)) |
| `cad_source`, `cad_license` | URL where the CAD came from and whether it may be redistributed |
| `<shop>_sku`, `<shop>_price_*`, `<shop>_moq` | one article number, one dated price snapshot and one minimum order per shop |
| `status`, `updated`, `notes` | the gate, the date the price was read, and anything a future reader needs |

`footprint` is the library CAD file, not the KiCad footprint string in a
project BOM. BOM matching uses `value` and `package`; the CAD file is what a
project imports after the part is chosen.

## Rules

1. **Through-hole first.** The library serves hand assembly: THT resistors,
   capacitors, diodes, LEDs, TO-92 and DIP parts, headers, character displays,
   and THT control hardware. Add an SMD part only when no THT part fits, and
   say so in `notes`.
2. **One row is one orderable part.** The `id` names the part, not the shop.
   `manufacturer` and `mpn` name the exact product when the shop or datasheet
   gives them. `value` and `package` are what a BOM calls it. Two rows must not
   share category, value and package. When a family page offers several values
   (a pot page with 1 k to 1 M), use one row per value you design in.
3. **A price is a snapshot.** `updated` holds the date you read it. The check
   warns after 365 days. Confirm price, VAT, minimum order and stock at
   checkout; never quote a library price to a customer.
4. **`status` says how to use the part:**
   `preferred` — use it for new designs;
   `alternate` — use it when the preferred part is out of stock;
   `draft` — found, not checked yet, so buy nothing and design nothing in;
   if a project already uses it, close the gate before the next order;
   `avoid` — keep it visible, do not design it in.
   A `preferred` or `alternate` row is **ready to use** only when it carries a
   datasheet, a footprint (except a panel-only `knob`), a STEP model, CAD
   provenance and at least one main-shop article number. A `draft` may omit
   any of them; that is what makes it a draft. `avoid` may also be incomplete;
   it records why a part was rejected.
5. **A search result is a lead, not an approval.** Adding a part to the BOM is
   a decision for the designer. Check the datasheet, the footprint, the
   ratings and the mated connector before ordering.
6. **CAD is evidence, not decoration.** A footprint must match the exact
   ordered part: pin numbering, polarity, pitch, pad and drill sizes, courtyard
   and body envelope. A STEP model must match the same body and origin. Never
   invent a footprint, a pinout or a 3D model because a part is convenient.
   When no trustworthy CAD exists, leave the part `draft`.
7. **Record provenance and respect the license.** Every CAD asset points at
   the page or file it came from in `cad_source` and names its license in
   `cad_license`. Do not copy vendor CAD into this repository unless the
   license allows redistribution. Do not commit whole datasheets; record the
   URL instead.

## The list as it stands

The list holds 56 parts, all `draft`, recorded on 2026-11-09: 39 with an
Electrokit article number, 17 bought in from LCSC, and one of the Electrokit
parts also from Conrad. Each article number was checked against its own shop
page or, for LCSC, against the order that was placed. The **DigiKey column is
still empty**: fill it from the search links, in the same way as Conrad.

26 rows now carry an `mpn`, 9 of them from the part value and 17 from the LCSC
order notes; 4 rows name a manufacturer (`ALPS`, `XUNPU`, `XKB`). **No row yet
carries a datasheet, footprint or STEP model**, so no row may be promoted to
`preferred` or `alternate` until those assets are added and verified. The
first promotion is a real task, not a status edit.

- **Electrokit** covers the most rows in the current list: THT carbon film
  resistors, ceramic and electrolytic capacitors, diodes, LEDs, TO-92
  transistors, DIP op-amps and timers, headers, and OLED plus character
  displays.
- **Conrad** has the widest catalogue, including pots, switches, knobs and the
  parts Electrokit does not carry, but it answers automated clients with a
  Cloudflare challenge (HTTP 403). So this repository does not scrape it: the
  tool prints a search link for every part and for every term in
  `library/searches.txt`, and the owner copies the article number by hand.
- **DigiKey Sweden** carries the parts the Swedish consumer shops lack, with
  a Swedish storefront and SEK prices. Its Product Information v4 API is the
  one live query this repository can make; see `froemix/doc/EU-SOURCING.md`.
- **Electrokit** gaps: no 20x4 character LCD, no 1 uF 50 V radial (the list
  uses 1 uF 100 V), and ceramic capacitors only at 22, 33, 47 and 100 nF.
- **LCSC** fills the gaps that Swedish shops leave in small connectors: 2 mm
  headers and sockets, 2x3/2x6/2x8 headers, a 12x12 mm tactile switch, an XUNPU
  5-way navigation switch, ALPS RK09 pots, 3.5 mm PJ-301M jacks, a SH1106
  1.3 inch OLED and a 24 V to 5 V isolated DC-DC module. Delivery comes from
  China, so it is a stock source, not a same-week source.

## CAD assets

Ready parts keep their footprint and STEP model under `library/cad/`. CSV
paths are **relative to `library/`**, so `cad/footprints/R_Axial...kicad_mod`
resolves to `library/cad/footprints/R_Axial...kicad_mod`.

```text
library/cad/
├── README.md                 provenance, naming, verification checklist
├── footprints/               shared generic footprints
│   └── R_Axial_DIN0207_...kicad_mod
├── step/                     matching shared STEP models
│   └── R_Axial_DIN0207_....step
└── <part-id>/                vendor- or part-specific assets
    ├── footprint.kicad_mod   or footprint.tsx / footprint.json
    ├── model.step
    └── SOURCE.md             URL, date, license, SHA-256, verification
```

Shared assets are right for generic packages that really are the same part
(a DIN0207 resistor body, a 2.54 mm pin header). Use a per-part folder when the
body, pinout or panel hardware is vendor-specific. See `library/cad/README.md`
for the checklist and the `SOURCE.md` template.

A footprint may be a tscircuit-native `.tsx` wrapper instead of a KiCad file.
Use `@tscircuit/common` (MIT, <https://github.com/tscircuit/common>) as the
reference for pin labels, footprinter strings, custom footprints and
`cadModel` data. Pin the package version you read, keep the MIT attribution,
and download the `stepUrl` model into `library/cad/` rather than leaving a
project build dependent on a remote URL. The `jlcpcb` supplier numbers in
`@tscircuit/common` are JLCPCB/LCSC references; they do not replace the
DigiKey/Electrokit/Conrad article numbers this library records.

The tool checks that a recorded path stays inside `library/`, that the file
exists, that a footprint has an allowed suffix (`.kicad_mod`, `.tsx`, `.json`)
and that a STEP file starts with the `ISO-10303-21` header. It cannot prove the
geometry fits; that is the datasheet check a person performs before promoting
the row.

## Accounts

Browsing and searching need no account at any shop. Ordering does.

- **Conrad Sweden** — a customer account at checkout (business or private).
  Automated search needs more: a business account and activation on
  `developer.conrad.com`. The portal sits behind a login, and `conrad.se`
  answers bots with a Cloudflare challenge, so this repository records Conrad
  article numbers by hand.
- **DigiKey Sweden** — an account at checkout. The live API needs an
  application with client-credentials access, registered at
  `developer.digikey.com`.
- **Electrokit** — an account at checkout; the catalogue is public.
- **LCSC** — an account at checkout; the catalogue is public. LCSC ships from
  Shenzhen, so confirm the lead time in the order before you rely on a part.

## Use

From the repository root:

```bash
python3 library/parts_library.py check
python3 library/parts_library.py find 10k
python3 library/parts_library.py links "potentiometer 10k"
python3 library/parts_library.py links --from library/searches.txt --shop conrad
python3 library/parts_library.py bom path/to/bom.csv
python3 library/parts_library.py bom path/to/bom.csv --shop conrad
python3 library/test_parts_library.py
```

`check` validates the rows, the shop data, the CAD paths, the STEP header and
the `SOURCE.md` provenance for ready rows. It prints how many rows have
identity and CAD, so a promotion that quietly lost its model is visible.
`find` shows the MPN, datasheet and CAD paths next to the shop article
numbers; `--format json` returns every column.

`links` prints a search link for each shop, so a term opens in the right
storefront. `--from FILE` reads one term per line; `library/searches.txt` holds
the terms for parts the list does not hold yet (pots, switches, knobs,
capacitors, connectors). Add a term there when you want a link next time.

`bom` matches each BOM line to the library by `value`, narrowed by `Footprint`.
A footprint only narrows the list when the library `package` text appears in
it, because a package may be prose ("axial 0.25 W") where the BOM holds a
KiCad footprint name; a prose package therefore never loses the part. A line
gets `matched`, `ambiguous` (two rows fit, so choose one) or `unmatched`. With
`--shop`, the tool prints a paste-ready order list with the quantity raised to
each part's minimum order. It prints no order line for a line it cannot fill;
it reports that line as `NO SKU` with the reason. The exit status is 2 while
any line is not `matched`, or while a chosen shop has no article number for
one, so a build script can stop.

## Adding a part

1. Get a link with `links TEXT` or `links --from library/searches.txt`, or use
   `python3 library/parts_library.py find TEXT` to check the part is not
   already recorded.
2. Open the product page at DigiKey Sweden, Electrokit and Conrad Sweden.
   Copy the exact MPN, manufacturer, shop article number, price, minimum order
   and the date you read them. Prices are snapshots; stock is not recorded as
   a permanent fact.
3. Get the datasheet URL and the footprint plus STEP model. Prefer the
   manufacturer's CAD, then a vendor CAD download whose license allows reuse,
   then the KiCad official libraries for a genuinely generic THT package, then
   a tscircuit-native `.tsx` wrapper modeled on `@tscircuit/common`. Verify pin
   numbering, pitch, pads, drills, courtyard and body against the datasheet.
   Put the files under `library/cad/` and write `SOURCE.md`.
4. Add the row with `status` `draft`, then run `check`.
5. Read the datasheet, compare the mated parts and the mechanical envelope,
   then change `status` to `preferred` or `alternate`. A promotion without CAD
   and a main-shop article number fails `check` by design.

Prices and article numbers belong to the shops. This file records them for
design reuse only. The detailed workflow, shop notes and CAD checklist live in
the `component-library` skill under `.pi/skills/component-library/`.

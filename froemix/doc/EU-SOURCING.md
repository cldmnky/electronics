# Hand-assembly EU sourcing

The **primary suppliers** are Conrad Sweden (`conrad.se`) and Electrokit.
RS Components and DigiKey with EU sales stay as **alternates**. JLCPCB/LCSC may still supply the SMT placements that JLC
assembles. An LCSC identifier in a generated hand BOM is a design reference,
**not evidence of an approved source**.

Hand-fitted parts belong in one place, `library/components.csv`, so the same
part is bought the same way in every project. `library/parts_library.py`
checks that list, searches it, and matches a project BOM against it. See
`library/README.md` for the columns, the rules and the accounts. This document
covers the shops and the per-project sourcing run.

`tools/source_parts.py` helps research the generated base and euro **hand BOMs**.
It creates search links for all five storefronts; optionally it queries
the official DigiKey Product Information v4 keyword API. It does not scrape any
shop, place orders, approve substitutions, or assert that a product is in
stock. No public self-service product API was confirmed for the other shops;
those entries are marked `manual_search`. Website URLs may change.

## Accounts and delivery

Searching and browsing need no account at any of the three primary shops.
Buying does:

- **Conrad Sweden** — a customer account at checkout, business or private. The
  storefront shows B2B prices excluding VAT. Conrad answers automated clients
  with a Cloudflare challenge (HTTP 403), so article numbers are recorded by
  hand here, not scraped.
- **Electrokit** — the English storefront is public, prices in SEK including
  VAT, delivery inside Sweden.
- **LCSC** — a public catalogue with unit prices in EUR and delivery from
  Shenzhen. The parts bought in for the build are in `library/components.csv`
  with their LCSC part numbers.

Automated Conrad data needs more than an account: a business account plus
activation on `developer.conrad.com`. That portal sits behind a login and no
public endpoint documentation was confirmed, so this repository records
Conrad article numbers by hand.

## Usage

From `froemix/`:

```bash
make release  # regenerate BOTH hand BOMs from the intended board revision
python3 tools/source_parts.py --output /tmp/froemix-sourcing.json
python3 tools/source_parts.py --format csv --output /tmp/froemix-sourcing.csv
python3 tools/source_parts.py --suppliers conrad,electrokit --format csv --output /tmp/froemix-primary.csv
python3 tools/source_parts.py --query NE555 --query "3.5mm jack" --suppliers conrad
python3 tools/test_source_parts.py
```

`fab/` is ignored build output, can be stale, and is not a source of truth.
Run release on the intended commit before searching; the tool cannot prove the
BOM matches the current source. The panel is fabrication-only, not a hand BOM.
The defaults read `fab/froemix-bom-hand.csv`, `fab/euro/euro-bom-hand.csv` and
`tools/sourcing-extras.csv`. Override with `--base-bom PATH`, `--euro-bom PATH`,
or `--extras PATH`. Missing/malformed inputs fail rather than silently omitting
a board. Edit or copy the extras CSV to include the required quantities for
your build: it currently includes Daisy socket strips, J1 shunts and spacers,
but is **not a complete mechanical shopping list**. Daisy Seed, knobs, screws,
washers, rack hardware, tools and consumables may also be required.

JSON output has one item per BOM row (designators and quantity preserved)
plus extras, including a UTC generation timestamp. `--format csv` produces a
spreadsheet-friendly row for each supplier and candidate (or search link); CSV
is a flattened view and does not include the JSON report metadata. `query_required` means neither the MPN nor the comment provided a
usable search: e.g. `C65114` is only an LCSC code. Research it manually or
supply an appropriate query in a copy of the input BOM. `manual_search` is a
link only—not a search result, stock check or approved match. BOM quantities
are for one board; multiply for your order and allow spares. `--query TEXT`
searches one text without reading a BOM, and `--suppliers LIST` limits the
report to a comma-separated subset of `rs,digikey,electrokit,conrad`.

## Shared part library

`library/components.csv` holds the parts to reuse: through-hole parts,
capacitors and character displays, each with the article number at Conrad
Sweden, Electrokit and LCSC, one price snapshot and one date. It is not a
BOM. It is the list a new BOM is chosen from.

```bash
python3 library/parts_library.py check
python3 library/parts_library.py find "100n"
python3 library/parts_library.py links "PJ398SM"
python3 library/parts_library.py bom fab/froemix-bom-hand.csv --shop electrokit
python3 library/test_parts_library.py
```

A price in the library is what one person read on one day, in the shop's own
currency and tax convention (Conrad excludes VAT, Electrokit includes it). Confirm it at checkout. A row marked `draft` is recorded but not
yet checked by a person; `preferred` means it passed a datasheet and footprint
check.

### Optional: live DigiKey search

Register an application for **Product Information v4** and client-credentials
access at [DigiKey's developer portal](https://developer.digikey.com/documentation).
Only provide secrets through the process environment. For example, in bash:

```bash
read -r -p 'DigiKey client ID: ' DIGIKEY_CLIENT_ID
read -r -s -p 'DigiKey client secret: ' DIGIKEY_CLIENT_SECRET; echo
export DIGIKEY_CLIENT_ID DIGIKEY_CLIENT_SECRET
python3 tools/source_parts.py --digikey-live --limit 5 --output /tmp/froemix-sourcing-live.json
unset DIGIKEY_CLIENT_ID DIGIKEY_CLIENT_SECRET
```

Live mode contacts DigiKey's production OAuth and keyword endpoints; API
registration, permission, connectivity and quotas are required. The token is
held in memory only, and credentials/token are not written into the report.
The request locale is **Sweden / English / SEK / ship-to Sweden**; confirm
EU sales, shipping origin, VAT, lead time and pricing at checkout. Marketplace,
packaging, minimum order quantity and stock conditions can vary. Other EU
countries require checking their storefront and local terms. Each live response
is a **candidate**, not a recommendation; `exact_query_match` compares the text
only and is not a compatibility or manufacturer-authenticity check.

`results`, `no_results`, and `error` are distinct. A live API/auth/rate-limit
error is recorded and the process exits nonzero, not treated as no stock. To
avoid repeated failed calls, subsequent uncached queries are marked unqueried.
`--limit` is the first page, not an exhaustive catalog search; no automatic
substitute is selected. Recheck any price/stock before purchasing. Only mocked
responses are used in the tests; live API access must be verified with your own
credentials.

## Review before ordering

- Review exact MPN, manufacturer, **each variation/package**, MOQ, EU
  destination, lead time, lifecycle and quantity. Stock and price are snapshots.
- Match **electrical ratings and PCB footprint**, including pin numbering,
  polarity, drill and body envelope. For audio jacks check switch contacts and
  panel bushing; for the OLED check pinout, interface, 3.3 V, dimensions and
  controller/firmware.
- Board-to-board headers and sockets must be reviewed **as a mated pair** for
  the 11 mm stack: post/shoulder lengths, socket contact depth, insertion,
  retention, body clearance and tail projection all matter. Matching pitch
  alone is insufficient. CN4 requires the documented seven-position preparation.
- U16 alternatives require full pinout, input range, isolation, output current,
  minimum load, startup and body/footprint verification. Do not buy a similar
  part number as a presumed drop-in.
- Check the separate 1×20 Daisy sockets and two shunts against
  `doc/MECHANICAL-STACK.md`; fit and rack depth are not proven by the tool.
- If no suitable part is available from the approved suppliers, leave it
  **unresolved** and assess a board/panel redesign; do not silently substitute.

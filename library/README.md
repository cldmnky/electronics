# Component library

One list of through-hole parts to reuse across projects: resistors,
capacitors, diodes, LEDs, transistors, DIP parts, headers, character and OLED
displays, and through-hole control hardware (pots, switches, knobs, jacks).
Every part has a stable `id`, its own facts, and the article number it carries
at the two primary EU suppliers:

| key | shop | market | currency |
| --- | --- | --- | --- |
| `conrad` | Conrad Electronic | Sweden, `conrad.se` | SEK, excluding VAT |
| `electrokit` | Electrokit | Sweden, `electrokit.com` (English store) | SEK, including VAT |

The shop column holds the **article number** the shop prints. Leave it empty
when the shop does not stock the part. Never copy an article number between
shops.

The file is `library/components.csv`, one row per part. It is plain CSV so a
spreadsheet opens it and a script reads it. `library/parts_library.py` checks
it, searches it, and matches a project BOM against it.

## Rules

1. **Through-hole first.** The library serves hand assembly: THT resistors,
   capacitors, diodes, LEDs, TO-92 and DIP parts, headers, character displays,
   and THT control hardware. Add an SMD part only when no THT part fits, and
   say so in `notes`.
2. **One row is one orderable part.** The `id` names the part, not the shop.
   `value` and `package` are what a BOM calls it. Two rows must not share
   category, value and package. When a family page offers several values (a
   pot page with 1 k to 1 M), use one row per value you design in.
3. **A price is a snapshot.** `updated` holds the date you read it. The check
   warns after 365 days. Confirm price, VAT, minimum order and stock at
   checkout; never quote a library price to a customer.
4. **`status` says how to use the part:**
   `preferred` — use it for new designs;
   `alternate` — use it when the preferred part is out of stock;
   `draft` — found, not checked yet, so buy nothing;
   `avoid` — keep it visible, do not design it in.
5. **A search result is a lead, not an approval.** Adding a part to the BOM is
   a decision for the designer. Check the datasheet, the footprint, the
   ratings and the mated connector before ordering.

## The list as it stands

The first pass holds 39 parts, all `draft`, recorded on 2026-11-09: all 39
carry an Electrokit article number and one of them also carries a Conrad
article number. Each article number was checked against its own shop page.

- **Electrokit** is the broadest source: THT carbon film resistors, ceramic and
  electrolytic capacitors, diodes, LEDs, TO-92 transistors, DIP op-amps and
  timers, headers, and OLED plus character displays.
- **Conrad** has the widest catalogue, including pots, switches, knobs and the
  parts Electrokit does not carry, but it answers automated clients with a
  Cloudflare challenge (HTTP 403). So this repository does not scrape it: the
  tool prints a search link for every part and for every term in
  `library/searches.txt`, and the owner copies the article number by hand.
- **Electrokit** gaps: no 20x4 character LCD, no 1 uF 50 V radial (the list
  uses 1 uF 100 V), and ceramic capacitors only at 22, 33, 47 and 100 nF.

## Accounts

Browsing and searching need no account at either shop. Ordering does.

- **Conrad Sweden** — a customer account at checkout (business or private).
  Automated search needs more: a business account and activation on
  `developer.conrad.com`. The portal sits behind a login, and `conrad.se`
  answers bots with a Cloudflare challenge, so this repository records Conrad
  article numbers by hand.
- **Electrokit** — an account at checkout; the catalogue is public.

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

1. Get a link with `links TEXT` or `links --from library/searches.txt`.
2. Copy the article number, the price and the date of the page.
3. Add the row with `status` `draft`, then run `check`.
4. Check the datasheet, then change `status` to `preferred` or `alternate`.

Prices and article numbers belong to the shops. This file records them for
design reuse only.

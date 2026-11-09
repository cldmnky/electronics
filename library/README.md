# Component library

One list of through-hole parts to reuse across projects: resistors,
capacitors, diodes, LEDs, transistors, DIP parts, headers, character and OLED
displays, and the eurorack hardware the 3U-Shop sells (jacks, pots, knobs,
switches). Every part has a stable `id`, its own facts, and the article number
it carries at each of the three primary EU suppliers:

| key | shop | market | currency |
| --- | --- | --- | --- |
| `conrad` | Conrad Electronic | Sweden, `conrad.se` | SEK, excluding VAT |
| `electrokit` | Electrokit | Sweden, `electrokit.com` (English store) | SEK, including VAT |
| `u3shop` | 3U-Shop | Germany, `3u-shop.de` (German, eurorack parts) | EUR, including VAT |

The shop column holds the **article number** the shop prints. 3U-Shop prints
none, so its column holds the product **slug** from the URL, for example
`pj398sm-3-5mm-monobuchse`, which `parts_library.py` turns back into
`https://www.3u-shop.de/p/<slug>`.

Shipping limits belong to sourcing, not to the part: 3U-Shop ships B2C inside
Germany and Switzerland only; a business order to another EU country needs an
e-mail to `post@3u-shop.de`. Conrad Sweden and Electrokit ship inside Sweden.

The file is `library/components.csv`, one row per part. It is plain CSV so a
spreadsheet opens it and a script reads it. `library/parts_library.py` checks
it, searches it, and matches a project BOM against it.

## Rules

1. **Through-hole first.** The library serves hand assembly: THT resistors,
   capacitors, diodes, LEDs, TO-92 and DIP parts, headers, character displays,
   and THT eurorack hardware. Add an SMD part only when no THT part fits, and
   say so in `notes`.
2. **One row is one orderable part.** The `id` names the part, not the shop.
   `value` and `package` are what a BOM calls it. Two rows must not share
   category, value and package.
3. **A price is a snapshot.** `updated` holds the date you read it. The check
   warns after 365 days. Confirm price, VAT, minimum order and stock at
   checkout; never quote a library price to a customer.
4. **A part is listed only at a shop where you saw it.** Leave the SKU empty
   when the shop does not stock it. Do not copy an article number between
   shops.
5. **`status` says how to use the part:**
   `preferred` — use it for new designs;
   `alternate` — use it when the preferred part is out of stock;
   `draft` — found, not checked yet, so buy nothing;
   `avoid` — keep it visible, do not design it in.
6. **A search result is a lead, not an approval.** Adding a part to the BOM is
   a decision for the designer. Check the datasheet, the footprint, the
   ratings and the mated connector before ordering.
7. **A part that one shop does not ship to you is still a part.** Keep the
   row; leave that shop's columns empty and note the delivery limit.

## The list as it stands

The first pass holds 46 parts, all `draft`, recorded on 2026-11-09: 39 with an
Electrokit article number, 7 with a 3U-Shop product, 1 with a Conrad article
number. Each one was checked against its own shop page.

What the shops actually stock shapes the list:

- **Electrokit** is the broadest source: THT carbon film resistors, ceramic and
  electrolytic capacitors, diodes, LEDs, TO-92 transistors, DIP op-amps and
timers, headers, and OLED plus character displays.
- **3U-Shop** sells eurorack hardware, not a general catalogue: jacks,
  potentiometers, knobs and switches. It has no LCD and no resistor range.
- **Conrad** has the widest catalogue but answers automated clients with a
  Cloudflare challenge, so only one row carries a Conrad article number. Fill
  the rest by hand from the search links `parts_library.py` prints.

The gaps that need a decision: Electrokit stocks only 22, 33, 47 and 100 nF
ceramic capacitors with a 5 mm pitch, has no 1 uF 50 V radial (the list uses
1 uF 100 V instead), and lists no 20x4 character LCD at all.

## Accounts

Browsing and searching need no account at any of the three shops. Ordering
does:

- **Conrad Sweden** — a customer account at checkout (business or private).
  Automated search needs more: a business account and activation on
  `developer.conrad.com`. The portal sits behind a login, and `conrad.se`
  answers bots with a Cloudflare challenge, so this repository records Conrad
  article numbers by hand.
- **Electrokit** — an account at checkout; the catalogue is public.
- **3U-Shop** — an account at checkout; the catalogue is public and German.
  B2C delivery goes to Germany and Switzerland only. A business order from
  Sweden needs an e-mail to `post@3u-shop.de` first.

## Use

From the repository root:

```bash
python3 library/parts_library.py check
python3 library/parts_library.py find 10k
python3 library/parts_library.py links "PJ301M-12"
python3 library/parts_library.py bom path/to/bom.csv
python3 library/parts_library.py bom path/to/bom.csv --shop conrad
python3 library/test_parts_library.py
```

`bom` matches each BOM line to the library by `value`, narrowed by `Footprint`.
A footprint only narrows the list when the library `package` text appears in
it, because a package may be prose ("axial 0.25 W") where the BOM holds a
KiCad footprint name; a prose package therefore never loses the part. A line
gets `matched`, `ambiguous` (two rows fit, so choose one) or `unmatched`. With `--shop`, the tool prints a paste-ready order list with the
quantity raised to each part's minimum order. It prints no order line for a
line it cannot fill; it reports that line as `NO SKU` with the reason. The
exit status is 2 while any line is not `matched`, or while a chosen shop has no
article number for one, so a build script can stop.

## Adding a part

1. Search the three shops for the part (see `links`).
2. Copy the article number, the price and the date of the page.
3. Add the row with `status` `draft`, then run `check`.
4. Check the datasheet, then change `status` to `preferred` or `alternate`.

Prices and article numbers belong to the shops. This file records them for
design reuse only.

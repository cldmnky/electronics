# Component inventory (purchased stock)

`inventory.csv` in this directory is the record of **parts that have been
bought**, one row per order line, with the shop's own part number, the
manufacturer part number, quantity, unit price, currency and whether that price
includes VAT. Nothing here is fitted yet: every row carries
`status = stocked`.

This is deliberately **not** `library/components.csv`. The difference matters:

| | `inventory.csv` (this) | `library/components.csv` |
| --- | --- | --- |
| What it is | a purchase record, qty on the shelf | the reusable part catalogue |
| Gate | none — the shop's data as ordered | `parts_library.py check`, CAD + provenance + main-shop SKU |
| A row means | "I own this" | "this part is approved for a new design" |
| Grows by | adding an order | promoting a verified part |

A part becomes reusable only when it gets a library row. The library row then
points at the shop article numbers recorded here, and the inventory row stops
being the only place the part exists. Until that happens, a part may not be
designed into a new board.

## Orders recorded

| Source | Order reference | Date | Lines | Pieces | Total | VAT |
| --- | --- | --- | --- | --- | --- | --- |
| DigiKey Sweden | `2026-11-13T025511` | 2026-11-13 | 13 | 47 | 762.30 SEK | excl. VAT (add 25 % at checkout) |
| LCSC | `WM2611030774` | 2026-11-13 | 17 | 148 | 59.14 EUR | excl. VAT |

Source files: `~/Downloads/2026-11-13T025511.csv` (DigiKey cart export) and
`~/Downloads/LCSC__WM2611030774_20261113172334.csv`. Prices are a snapshot from
those files; confirm before reordering.

## Known problems in the current stock

These were found while checking the orders against the boards. They are recorded
here so nobody discovers them at the soldering bench:

1. **Harwin `M22-251*` 2.00 mm male headers are too short for this stack.** Pin
   tip sits ~6.3 mm above the PCB (4.0 mm pin + ~2.3 mm body). The froemix
   boards are 11 mm apart, so the socket entry is at 6.7 mm — the pin does not
   reach. Use the XKB `X4612WV` lines (C2883069 / C2883061) instead; the Harwin
   parts are only useful for short board-to-board gaps.
2. **`M22-6540842R` (2.00 mm SMD right-angle receptacle) has no home.** It is
   SMD right-angle; CN3 is deliberately unmated and its pads are vertical THT.
3. **`SBH11-PBPC-D08-ST-BK` is a vertical 2×8**; J2 is a right-angle shrouded
   2×8 (4ms footprint). It will not sit on the pads.
4. **J1 needs 2 low-profile shunts per board — none were ordered.** Header plus
   shunts must stay ≤9.5 mm or the front board cannot seat.
5. **`PJ-301M-2-B` / `-3-B` are solder-lug jacks, not 3-pin THT jacks.** One
   bottom tag, two rearward tags. Wire them to the euro board's pads, or buy the
   Thonkiconn family (`WQP518MA` mono / `WQP419GR` stereo, M6 × 0.5) and drop
   them onto the pads directly.
6. **`310-43-107-41-001000` (DS1 socket) is discontinued at DigiKey** — only two
   were available. Any 2.79 mm-body machined 1×7 does the same job.
7. **Daisy sockets:** 4 × 1×20 Würth were ordered; a board needs **two**, so
   that is two boards' worth, not five.
8. **The ST-LINK-V3MINIE speaks STDC14**, not the Seed's 2×5 Cortex pads: it
   needs an STDC14 → 10-pin adapter (unless the supplied cable ends in 10 pins)
   plus a `FTSH-105-01-F-DV` header or a pogo clip on the Seed's `J2`.

## Adding an order

1. Append one row per line to `inventory.csv` — `source`, `order_ref`,
   `order_date`, `shop_part` (the shop's own code: DigiKey part number, LCSC
   C-number), `mpn`, `manufacturer`, `description`, `package_or_series`, `qty`,
   `unit_price`, `currency`, `vat`, `status`, `intended_for`, `notes`.
2. Put the state of verification and any trap in `notes` — "matches drawing X
   dimension Y" or "too short for an 11 mm gap". A row without that is a row
   someone will mis-trust later.
3. When a part is used on a board, add or update its row in
   `library/components.csv` and change the inventory `intended_for` to the
   project and reference designator.
4. Never copy an article number from one shop to another, and never invent an
   MPN. If only a search result exists, the row stays out of the inventory until
   the part arrives and the number can be read off the bag.

`.pi/skills/connectors/SKILL.md` holds the reusable knowledge about these part
families (pitch, mating heights, pin order traps, connector types).

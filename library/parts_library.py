#!/usr/bin/env python3
"""Keep one reusable list of through-hole parts and source it from EU shops.

The list is `library/components.csv`: through-hole parts, capacitors and
character displays, bought from Electrokit, DigiKey Sweden and Conrad Sweden.
A row holds the part's own facts and the article number it carries at each
shop. LCSC holds stock that was bought in; it is not a main source. Every
price is a recorded snapshot with a date, never a live quote.

Browsing and searching need no shop account. Ordering does. Automated data
needs a vendor API; only the tool in froemix/tools has one (DigiKey).

Python standard library only. No network, no scraping, no credentials.
"""
from __future__ import annotations

import argparse
import csv
import io
import json
import re
import sys
from datetime import date
from pathlib import Path
from urllib.parse import quote

ROOT = Path(__file__).resolve().parents[1]
LIBRARY = ROOT / "library/components.csv"

# One entry per primary supplier. `search` holds {q}, the escaped search text.
SHOPS = {
    "conrad": {
        "name": "Conrad Sweden",
        "currency": "SEK",
        "search": "https://www.conrad.se/sv/search.html?search={q}",
        "sku": "conrad_sku",
        "price": "conrad_price_sek",
        "moq": "conrad_moq",
    },
    "digikey": {
        "name": "DigiKey Sweden",
        "currency": "SEK",
        "search": "https://www.digikey.se/en/products/result?keywords={q}",
        "sku": "digikey_sku",
        "price": "digikey_price_sek",
        "moq": "digikey_moq",
    },
    "electrokit": {
        "name": "Electrokit",
        "currency": "SEK",
        "search": "https://www.electrokit.com/en/search.php?keyword={q}&search=",
        "sku": "electrokit_sku",
        "price": "electrokit_price_sek",
        "moq": "electrokit_moq",
    },
    "lcsc": {
        # Stock bought in from JLCPCB in Shenzhen; longer delivery than Sweden.
        "name": "LCSC",
        "currency": "EUR",
        "search": "https://www.lcsc.com/search?q={q}",
        "sku": "lcsc_sku",
        "price": "lcsc_price_eur",
        "moq": "lcsc_moq",
    },
}

COLUMNS = [
    "id", "category", "description", "value", "package", "rating", "tolerance",
    "conrad_sku", "conrad_price_sek", "conrad_moq",
    "digikey_sku", "digikey_price_sek", "digikey_moq",
    "electrokit_sku", "electrokit_price_sek", "electrokit_moq",
    "lcsc_sku", "lcsc_price_eur", "lcsc_moq",
    "datasheet", "status", "updated", "notes",
]

CATEGORIES = {
    "resistor", "cap-ceramic", "cap-electrolytic", "cap-film",
    "diode", "led", "transistor", "ic", "header", "display",
    "jack", "potentiometer", "knob", "switch", "power",
}

STATUSES = ("preferred", "alternate", "draft", "avoid")

REQUIRED = ("id", "category", "description", "status", "updated")

MAX_AGE_DAYS = 365


class LibraryError(Exception):
    pass


def norm(text: str) -> str:
    return re.sub(r"\s+", " ", (text or "").strip()).casefold()


def search_url(shop: str, text: str) -> str:
    """Search link for one shop. Escaping keeps every character of the text."""
    try:
        template = SHOPS[shop]["search"]
    except KeyError:
        raise LibraryError(f"unknown shop: {shop} (choose from: {', '.join(SHOPS)})") from None
    return template.format(q=quote(text, safe=""))


def product_url(shop: str, sku: str) -> str:
    """Where to look for one recorded article: its shop page, or a search."""
    if not sku.strip():
        raise LibraryError(f"{shop}: no sku to look up")
    sku = sku.strip()
    if shop == "lcsc" and re.fullmatch(r"C\d+", sku, re.I):
        return f"https://www.lcsc.com/product-detail/{sku.upper()}.html"
    return search_url(shop, sku)


def parse_date(text: str, context: str) -> date:
    try:
        return date.fromisoformat(text.strip())
    except (AttributeError, ValueError) as exc:
        raise LibraryError(f"{context}: updated must be YYYY-MM-DD, not {text!r}") from exc


def parse_price(text: str, context: str) -> float | None:
    text = (text or "").strip().replace(",", ".")
    if not text:
        return None
    try:
        value = float(text)
    except ValueError as exc:
        raise LibraryError(f"{context}: price {text!r} is not a number") from exc
    if value < 0:
        raise LibraryError(f"{context}: price must not be negative")
    return value


def parse_moq(text: str, context: str) -> int:
    text = (text or "").strip()
    if not text:
        return 1
    try:
        value = int(text)
    except ValueError as exc:
        raise LibraryError(f"{context}: moq {text!r} is not a whole number") from exc
    if value < 1:
        raise LibraryError(f"{context}: moq must be 1 or more")
    return value


def load(path: Path = LIBRARY) -> list[dict]:
    """Read the library and check its columns. Row rules live in validate()."""
    try:
        with path.open(newline="", encoding="utf-8-sig") as handle:
            reader = csv.DictReader(handle)
            missing = [name for name in REQUIRED if name not in (reader.fieldnames or [])]
            if missing:
                raise LibraryError(f"{path}: missing columns: {', '.join(missing)}")
            rows = []
            for line, row in enumerate(reader, 2):
                if not any((value or "").strip() for value in row.values()):
                    continue
                clean = {name: (row.get(name) or "").strip() for name in COLUMNS}
                clean["_line"] = str(line)
                rows.append(clean)
            return rows
    except OSError as exc:
        raise LibraryError(f"cannot read {path}: {exc.strerror}") from exc


def validate(rows: list[dict], today: date | None = None,
             max_age_days: int = MAX_AGE_DAYS) -> tuple[list[str], list[str]]:
    """Return (errors, warnings). Errors stop a release; warnings do not."""
    errors: list[str] = []
    warnings: list[str] = []
    today = today or date.today()
    seen: dict[str, str] = {}
    for row in rows:
        where = f"line {row.get('_line', '?')} ({row.get('id') or 'no id'})"
        part_id = row["id"]
        if not re.fullmatch(r"[a-z0-9][a-z0-9.-]*", part_id):
            errors.append(f"{where}: id must be lowercase letters, digits, dot or dash")
        if part_id in seen:
            errors.append(f"{where}: duplicate id, also on line {seen[part_id]}")
        seen.setdefault(part_id, row.get("_line", "?"))
        if row["category"] not in CATEGORIES:
            errors.append(f"{where}: category must be one of: {', '.join(sorted(CATEGORIES))}")
        if row["status"] not in STATUSES:
            errors.append(f"{where}: status must be one of: {', '.join(STATUSES)}")
        if not row["description"]:
            errors.append(f"{where}: description is required")
        if not row["value"]:
            warnings.append(f"{where}: value is empty, so a BOM cannot match this part")
        try:
            age = (today - parse_date(row["updated"], where)).days
            if age < 0:
                warnings.append(f"{where}: updated date is in the future")
            elif age > max_age_days:
                warnings.append(f"{where}: price snapshot is {age} days old")
        except LibraryError as exc:
            errors.append(str(exc))
        shops_with_sku = 0
        for shop, info in SHOPS.items():
            sku = (row.get(info["sku"]) or "").strip()
            if sku:
                shops_with_sku += 1
            try:
                price = parse_price(row.get(info["price"], ""), where)
                parse_moq(row.get(info["moq"], ""), where)
            except LibraryError as exc:
                errors.append(str(exc))
                continue
            if sku and price is None:
                errors.append(f"{where}: {shop} sku needs a price")
            elif price is not None and not sku:
                errors.append(f"{where}: {shop} price needs a sku")
        if shops_with_sku == 0 and row["status"] != "draft":
            errors.append(f"{where}: status {row['status']} needs at least one shop sku")
    return errors, warnings


def find(rows: list[dict], text: str) -> list[dict]:
    """Rows whose id, description, value, package or notes contain the text."""
    needle = norm(text)
    if not needle:
        raise LibraryError("find needs search text")
    fields = ("id", "description", "value", "package", "rating", "notes")
    return [row for row in rows if any(needle in norm(row[name]) for name in fields)]


def match_bom(rows: list[dict], value: str, footprint: str = "") -> list[str]:
    """Candidate library ids for one BOM line. Never a silent substitution.

    The value must match exactly. A footprint only ranks: it removes a
    candidate when its package text appears in the footprint, and it removes
    none when no package matches, because a library package may be prose
    ("radial 5x11 mm") where the BOM holds a KiCad footprint name.
    """
    wanted = norm(value)
    if not wanted:
        return []
    candidates = [row for row in rows if norm(row["value"]) == wanted]
    if footprint and len(candidates) > 1:
        package = norm(footprint)
        ranked = [row for row in candidates
                  if norm(row["package"]) and norm(row["package"]) in package]
        if ranked:
            candidates = ranked
    return [row["id"] for row in candidates]


def read_bom(path: Path) -> list[dict]:
    """Read a BOM with a Value or Comment column and a Quantity column."""
    try:
        with path.open(newline="", encoding="utf-8-sig") as handle:
            reader = csv.DictReader(handle)
            names = reader.fieldnames or []
            value_column = next((name for name in ("Value", "Comment") if name in names), "")
            if not value_column or "Quantity" not in names:
                raise LibraryError(f"{path}: needs a Quantity column and a Value or Comment column")
            lines = []
            for line, row in enumerate(reader, 2):
                text = (row.get(value_column) or "").strip()
                if not text:
                    continue
                try:
                    quantity = int((row.get("Quantity") or "").strip())
                except ValueError as exc:
                    raise LibraryError(f"{path}:{line}: Quantity must be a whole number") from exc
                if quantity < 1:
                    raise LibraryError(f"{path}:{line}: Quantity must be 1 or more")
                lines.append({"value": text, "quantity": quantity,
                              "footprint": (row.get("Footprint") or "").strip(),
                              "designators": (row.get("Designators") or "").strip()})
            return lines
    except OSError as exc:
        raise LibraryError(f"cannot read {path}: {exc.strerror}") from exc


def plan_bom(rows: list[dict], bom_lines: list[dict]) -> list[dict]:
    """Attach library candidates to every BOM line."""
    planned = []
    for line in bom_lines:
        candidates = match_bom(rows, line["value"], line["footprint"])
        if len(candidates) == 1:
            status, part_id = "matched", candidates[0]
        elif candidates:
            status, part_id = "ambiguous", ""
        else:
            status, part_id = "unmatched", ""
        planned.append({**line, "status": status, "id": part_id, "candidates": candidates})
    return planned


def order_lines(rows: list[dict], planned: list[dict], shop: str) -> tuple[list[dict], list[dict]]:
    """Split a plan into paste-ready order lines and the lines it cannot fill."""
    info = SHOPS[shop]
    by_id = {row["id"]: row for row in rows}
    order, missing = [], []
    for line in planned:
        part = by_id.get(line["id"]) if line["id"] else None
        if not part or not part[info["sku"]]:
            missing.append(line)
            continue
        need = line["quantity"]
        moq = parse_moq(part[info["moq"]], part["id"])
        quantity = need if need >= moq else moq
        order.append({"sku": part[info["sku"]], "quantity": quantity,
                      "price": part[info["price"]], "id": part["id"],
                      "description": part["description"],
                      "url": product_url(shop, part[info["sku"]]),
                      "designators": line["designators"]})
    return order, missing


def spreadsheet_text(value: object) -> str:
    text = "" if value is None else str(value)
    if text.lstrip().startswith(("=", "+", "-", "@")):
        return "'" + text
    return text


def render_plan_csv(planned: list[dict]) -> str:
    output = io.StringIO(newline="")
    writer = csv.DictWriter(output, fieldnames=["value", "footprint", "designators", "quantity",
                                                "status", "id", "candidates"])
    writer.writeheader()
    for line in planned:
        writer.writerow({key: spreadsheet_text(line[key] if key != "candidates" else ",".join(line[key]))
                         for key in writer.fieldnames})
    return output.getvalue()


def command_check(args) -> int:
    rows = load(args.library)
    errors, warnings = validate(rows, max_age_days=args.max_age_days)
    for warning in warnings:
        print(f"warning: {warning}", file=sys.stderr)
    for error in errors:
        print(f"error: {error}", file=sys.stderr)
    covered = {shop: sum(1 for row in rows if row[info["sku"]])
               for shop, info in SHOPS.items()}
    states = {status: sum(1 for row in rows if row["status"] == status) for status in STATUSES}
    print(f"{len(rows)} parts, {len(errors)} errors, {len(warnings)} warnings")
    print("with a sku: " + ", ".join(f"{SHOPS[shop]['name']} {count}" for shop, count in covered.items()))
    print("by status: " + ", ".join(f"{status} {count}" for status, count in states.items()))
    return 1 if errors else 0


def command_find(args) -> int:
    rows = load(args.library)
    matches = find(rows, args.text)
    if args.format == "json":
        print(json.dumps([{key: row[key] for key in COLUMNS} for row in matches], indent=2, ensure_ascii=False))
    else:
        for row in matches:
            print(f"{row['id']:24} {row['description']}")
            for shop, info in SHOPS.items():
                sku = row[info["sku"]]
                if sku:
                    print(f"  {SHOPS[shop]['name']:14} {sku:10} {row[info['price']]:>8} {info['currency']}"
                          f"  {product_url(shop, sku)}")
            print(f"  search   {search_url(args.shop, row['value'] or row['description'])}")
    return 0 if matches else 1


def read_search_terms(path: Path) -> list[str]:
    """Read one search term per line; `#` starts a comment."""
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as exc:
        raise LibraryError(f"cannot read {path}: {exc.strerror}") from exc
    terms = []
    for line in text.splitlines():
        line = line.strip()
        if line and not line.startswith("#"):
            terms.append(line)
    return terms


def command_links(args) -> int:
    terms: list[str] = []
    if args.from_file:
        terms += read_search_terms(args.from_file)
    terms += [term.strip() for term in args.text if term.strip()]
    if not terms:
        raise LibraryError("links needs search text, or --from FILE")
    shops = [args.shop] if args.shop else list(SHOPS)
    for index, term in enumerate(terms):
        if index:
            print()
        print(term)
        for shop in shops:
            print(f"  {SHOPS[shop]['name']:14} {search_url(shop, term)}")
    return 0


def command_bom(args) -> int:
    rows = load(args.library)
    planned = plan_bom(rows, read_bom(args.bom))
    if args.format == "csv":
        sys.stdout.write(render_plan_csv(planned))
    else:
        if args.shop:
            order, missing = order_lines(rows, planned, args.shop)
            for line in order:
                print(f"{line['quantity']:4} x {line['sku']:14} {line['description']}  [{line['id']}]")
                print(f"       {line['url']}")
            reasons = {"ambiguous": "two library parts fit, choose one",
                       "unmatched": "not in the library"}
            for line in missing:
                reason = reasons.get(line["status"],
                                     f"no {SHOPS[args.shop]['name']} article number")
                print(f"  NO SKU {line['value']} x{line['quantity']} ({reason})", file=sys.stderr)
            return 0 if not missing else 2
        print(json.dumps(planned, indent=2, ensure_ascii=False))
        return 0 if all(line["status"] == "matched" for line in planned) else 2


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--library", type=Path, default=LIBRARY, help="library CSV; default library/components.csv")
    subparsers = parser.add_subparsers(dest="command", required=True)

    check = subparsers.add_parser("check", help="validate the library")
    check.add_argument("--max-age-days", type=int, default=MAX_AGE_DAYS)
    check.set_defaults(func=command_check)

    find_parser = subparsers.add_parser("find", help="search the library and show shop skus")
    find_parser.add_argument("text")
    find_parser.add_argument("--shop", choices=tuple(SHOPS), default="conrad",
                             help="shop for the search link (default conrad)")
    find_parser.add_argument("--format", choices=("text", "json"), default="text")
    find_parser.set_defaults(func=command_find)

    links = subparsers.add_parser("links", help="search links for one or more texts")
    links.add_argument("text", nargs="*", help="search text; repeat for more")
    links.add_argument("--shop", choices=tuple(SHOPS), help="one shop only (default: every shop)")
    links.add_argument("--from", dest="from_file", type=Path, metavar="FILE",
                       help="read search terms from a file, one per line, # for a comment")
    links.set_defaults(func=command_links)

    bom = subparsers.add_parser("bom", help="match a BOM against the library")
    bom.add_argument("bom", type=Path)
    bom.add_argument("--shop", choices=tuple(SHOPS), help="print a paste-ready order list")
    bom.add_argument("--format", choices=("json", "csv"), default="json")
    bom.set_defaults(func=command_bom)
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        return args.func(args)
    except LibraryError as exc:
        print(f"library: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())

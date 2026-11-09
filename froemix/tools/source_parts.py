#!/usr/bin/env python3
"""Create EU supplier search links and optionally query DigiKey Product API v4.

Search results are leads, never approved substitutes. See doc/EU-SOURCING.md.
Python standard library only; no purchasing, scraping, or credential storage.

Shop coverage needs no account and no network: RS Sweden, DigiKey Sweden,
Electrokit, conrad.se and 3u-shop.de. Only DigiKey has a live query, because a
public self-service search API was confirmed there alone.
"""
from __future__ import annotations

import argparse
import csv
import io
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import re
import sys
import time
from urllib.error import HTTPError, URLError
from urllib.parse import quote, urlencode
from urllib.request import Request, urlopen

ROOT = Path(__file__).resolve().parents[1]
SUPPLIERS = ("rs", "digikey", "electrokit", "conrad", "u3shop")
# Free text of the shop for messages, with the storefront each link opens.
SHOPS = {
    "rs": ("RS Sweden", "https://se.rs-online.com/"),
    "digikey": ("DigiKey Sweden", "https://www.digikey.se/"),
    "electrokit": ("Electrokit", "https://www.electrokit.com/en/"),
    "conrad": ("Conrad Sweden", "https://www.conrad.se/"),
    "u3shop": ("3U-Shop (German shop)", "https://www.3u-shop.de/"),
}


class SourcingError(Exception):
    pass


def supplier_list(text: str) -> tuple[str, ...]:
    """Parse a --suppliers value such as 'conrad,u3shop'."""
    names = tuple(name.strip().lower() for name in text.split(",") if name.strip())
    if not names:
        raise SourcingError("--suppliers needs at least one of: " + ", ".join(SUPPLIERS))
    unknown = [name for name in names if name not in SUPPLIERS]
    if unknown:
        raise SourcingError("unknown supplier(s): " + ", ".join(unknown) +
                            " (choose from: " + ", ".join(SUPPLIERS) + ")")
    # Keep the canonical order so two runs with the same set agree.
    return tuple(name for name in SUPPLIERS if name in names)


def search_url(supplier: str, query: str) -> str:
    q = quote(query, safe="")
    if supplier == "rs":
        return f"https://se.rs-online.com/web/search/searchBrowseAction.html?searchTerm={q}"
    if supplier == "digikey":
        return f"https://www.digikey.se/en/products/result?keywords={q}"
    if supplier == "electrokit":
        return f"https://www.electrokit.com/en/search.php?keyword={q}&search="
    if supplier == "conrad":
        # Swedish storefront, B2B prices shown excluding VAT.
        return f"https://www.conrad.se/sv/search.html?search={q}"
    if supplier == "u3shop":
        # German Shopware shop; German part words give better hits than English.
        return f"https://www.3u-shop.de/search?q={q}"
    raise ValueError(f"unknown supplier: {supplier}")


def useful_query(mpn: str, comment: str) -> str:
    """Avoid presenting an LCSC ID as a manufacturer part number."""
    for value in (mpn.strip(), comment.strip()):
        if value and value != "-" and not re.fullmatch(r"C\d+", value, re.I):
            return value
    return ""


def positive_int(text: str, context: str) -> int:
    try:
        value = int(text)
    except (ValueError, TypeError) as exc:
        raise SourcingError(f"{context}: quantity must be a positive integer") from exc
    if value < 1:
        raise SourcingError(f"{context}: quantity must be a positive integer")
    return value


def read_bom(path: Path, board: str) -> list[dict]:
    required = {"Quantity", "Comment", "MPN", "Designators", "LCSC Part #"}
    try:
        with path.open(newline="", encoding="utf-8-sig") as handle:
            reader = csv.DictReader(handle)
            missing = required - set(reader.fieldnames or [])
            if missing:
                raise SourcingError(f"{path}: missing columns: {', '.join(sorted(missing))}")
            parts = []
            for line, row in enumerate(reader, 2):
                refs = row["Designators"].strip()
                if not refs:
                    raise SourcingError(f"{path}:{line}: missing Designators")
                quantity = positive_int(row["Quantity"], f"{path}:{line}")
                parts.append({"board": board, "designators": refs, "quantity": quantity,
                              "mpn": row["MPN"].strip(), "comment": row["Comment"].strip(),
                              "lcsc_reference": row["LCSC Part #"].strip(),
                              "query": useful_query(row["MPN"], row["Comment"]),
                              "source": str(path)})
            return parts
    except OSError as exc:
        raise SourcingError(f"cannot read {path}: {exc.strerror}") from exc


def read_extras(path: Path) -> list[dict]:
    try:
        with path.open(newline="", encoding="utf-8-sig") as handle:
            reader = csv.DictReader(handle)
            required = {"board", "designators", "quantity", "query"}
            missing = required - set(reader.fieldnames or [])
            if missing:
                raise SourcingError(f"{path}: missing columns: {', '.join(sorted(missing))}")
            rows = []
            for line, row in enumerate(reader, 2):
                if not row["board"].strip() or not row["designators"].strip() or not row["query"].strip():
                    raise SourcingError(f"{path}:{line}: board, designators and query are required")
                rows.append({"board": row["board"].strip(), "designators": row["designators"].strip(),
                             "quantity": positive_int(row["quantity"], f"{path}:{line}"),
                             "mpn": "", "comment": "separately sourced hardware", "lcsc_reference": "",
                             "query": row["query"].strip(), "source": str(path)})
            return rows
    except OSError as exc:
        raise SourcingError(f"cannot read {path}: {exc.strerror}") from exc


def json_request(url: str, body: bytes, headers: dict) -> dict:
    request = Request(url, data=body, headers=headers, method="POST")
    try:
        with urlopen(request, timeout=25) as response:
            data = json.load(response)
    except HTTPError as exc:
        # Never print request headers, response bodies or credentials.
        raise SourcingError(f"DigiKey API returned HTTP {exc.code}") from exc
    except (URLError, TimeoutError, OSError, ValueError) as exc:
        raise SourcingError(f"DigiKey API request failed ({type(exc).__name__})") from exc
    if not isinstance(data, dict):
        raise SourcingError("DigiKey API returned an unexpected response")
    return data


class DigiKey:
    def __init__(self, client_id: str, client_secret: str):
        if not client_id or not client_secret:
            raise SourcingError("--digikey-live requires DIGIKEY_CLIENT_ID and DIGIKEY_CLIENT_SECRET")
        self.client_id = client_id
        self.client_secret = client_secret
        self.token = ""
        self.expires_at = 0.0

    def access_token(self) -> str:
        if not self.token or time.monotonic() >= self.expires_at:
            body = urlencode({"client_id": self.client_id, "client_secret": self.client_secret,
                              "grant_type": "client_credentials"}).encode()
            data = json_request("https://api.digikey.com/v1/oauth2/token", body,
                                {"Content-Type": "application/x-www-form-urlencoded"})
            token = data.get("access_token")
            if not isinstance(token, str) or not token:
                raise SourcingError("DigiKey token response did not contain an access token")
            self.token = token
            try:
                lifetime = int(data.get("expires_in", 600))
            except (ValueError, TypeError):
                lifetime = 600
            self.expires_at = time.monotonic() + max(1, lifetime - 30)
        return self.token

    def search(self, query: str, limit: int) -> list[dict]:
        headers = {"Authorization": f"Bearer {self.access_token()}", "X-DIGIKEY-Client-Id": self.client_id,
                   "Content-Type": "application/json", "X-DIGIKEY-Locale-Site": "SE",
                   "X-DIGIKEY-Locale-Language": "en", "X-DIGIKEY-Locale-Currency": "SEK",
                   "X-DIGIKEY-Locale-ShipToCountry": "SE"}
        data = json_request("https://api.digikey.com/products/v4/search/keyword",
                            json.dumps({"Keywords": query, "Limit": limit, "Offset": 0}).encode(), headers)
        products = data.get("Products")
        if not isinstance(products, list):
            raise SourcingError("DigiKey response missing Products list")
        results = []
        for product in products:
            if not isinstance(product, dict):
                raise SourcingError("DigiKey returned an invalid product")
            manufacturer = product.get("Manufacturer") or {}
            desc = product.get("Description") or {}
            mpn = product.get("ManufacturerProductNumber") or ""
            variations = product.get("ProductVariations") or []
            results.append({"mpn": mpn, "manufacturer": manufacturer.get("Name"),
                            "description": desc.get("ProductDescription"),
                            "exact_query_match": mpn.strip().casefold() == query.strip().casefold(),
                            "quantity_available": product.get("QuantityAvailable"),
                            "product_url": product.get("ProductUrl"),
                            "datasheet_url": product.get("DatasheetUrl"),
                            "product_status": product.get("ProductStatus"),
                            "variations": [{"supplier_part_number": v.get("DigiKeyProductNumber"),
                                            "package_type": (v.get("PackageType") or {}).get("Name"),
                                            "minimum_order_quantity": v.get("MinimumOrderQuantity"),
                                            "quantity_available": v.get("QuantityAvailableforPackageType"),
                                            "standard_pricing": v.get("StandardPricing")}
                                           for v in variations if isinstance(v, dict)],
                            "compatibility": "not reviewed"})
        return results


def build_report(parts: list[dict], client: DigiKey | None, limit: int,
                 suppliers: tuple[str, ...] = SUPPLIERS) -> tuple[dict, bool]:
    failed = False
    # Repeated exact searches do not need repeat API calls within a run.
    cache: dict[str, dict] = {}
    halted = False
    for part in parts:
        query = part["query"]
        part["suppliers"] = {}
        for supplier in suppliers:
            part["suppliers"][supplier] = {"search_url": search_url(supplier, query) if query else None,
                                            "status": "manual_search" if query else "query_required",
                                            "candidates": []}
        if client and query:
            if query not in cache:
                if halted:
                    cache[query] = {"status": "error", "candidates": [],
                                    "error": "not queried: an earlier DigiKey request failed"}
                else:
                    try:
                        candidates = client.search(query, limit)
                        cache[query] = {"status": "results" if candidates else "no_results",
                                        "candidates": candidates}
                    except SourcingError as exc:
                        cache[query] = {"status": "error", "candidates": [], "error": str(exc)}
                        halted = True
            part["suppliers"]["digikey"].update(cache[query])
            if cache[query]["status"] == "error":
                failed = True
    return ({"generated_at_utc": datetime.now(timezone.utc).isoformat(),
             "market": "; ".join(f"{SHOPS[name][0]} ({SHOPS[name][1]})" for name in suppliers),
             "notice": "Search leads only; no compatibility, stock or EU shipment is certified. "
                       "Review datasheets, quantities, shipping and checkout for every part. "
                       "Searching needs no account; ordering and any vendor API do.",
             "parts": parts}, failed)


def spreadsheet_text(value: object) -> str:
    text = "" if value is None else str(value)
    # CSV quoting alone does not prevent spreadsheet formula execution.
    if text.lstrip().startswith(("=", "+", "-", "@")):
        return "'" + text
    return text


def render_csv(report: dict) -> str:
    output = io.StringIO(newline="")
    fields = ["board", "designators", "quantity", "mpn", "query", "supplier", "status",
              "search_url", "candidate_mpn", "manufacturer", "exact_query_match", "product_url",
              "quantity_available", "variations_json", "compatibility", "error"]
    writer = csv.DictWriter(output, fieldnames=fields)
    writer.writeheader()
    for part in report["parts"]:
        for supplier, search in part["suppliers"].items():
            for candidate in search["candidates"] or [{}]:
                values = {"board": part["board"], "designators": part["designators"],
                          "quantity": part["quantity"], "mpn": part["mpn"], "query": part["query"],
                          "supplier": supplier, "status": search["status"], "search_url": search["search_url"],
                          "candidate_mpn": candidate.get("mpn"), "manufacturer": candidate.get("manufacturer"),
                          "exact_query_match": candidate.get("exact_query_match"),
                          "product_url": candidate.get("product_url"),
                          "quantity_available": candidate.get("quantity_available"),
                          "variations_json": json.dumps(candidate["variations"], ensure_ascii=False) if "variations" in candidate else "",
                          "compatibility": candidate.get("compatibility"), "error": search.get("error")}
                writer.writerow({key: spreadsheet_text(value) for key, value in values.items()})
    return output.getvalue()


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base-bom", type=Path, default=ROOT / "fab/froemix-bom-hand.csv")
    parser.add_argument("--euro-bom", type=Path, default=ROOT / "fab/euro/euro-bom-hand.csv")
    parser.add_argument("--extras", type=Path, default=ROOT / "tools/sourcing-extras.csv")
    parser.add_argument("--digikey-live", action="store_true", help="query DigiKey v4 (requires API credentials); every other shop stays a manual link")
    parser.add_argument("--suppliers", default=SUPPLIERS[0] + "," + ",".join(SUPPLIERS[1:]),
                        metavar="LIST", help="comma-separated subset of " + ",".join(SUPPLIERS) +
                        " (default: all)")
    parser.add_argument("--query", action="append", metavar="TEXT", default=None,
                        help="search this text instead of the BOMs; repeat for more parts")
    parser.add_argument("--limit", type=int, choices=range(1, 51), default=5, metavar="1..50")
    parser.add_argument("--format", choices=("json", "csv"), default="json", help="JSON (default) or spreadsheet-friendly CSV")
    parser.add_argument("--output", type=Path, help="write report to this path; default stdout")
    args = parser.parse_args(argv)
    try:
        suppliers = supplier_list(args.suppliers)
        if args.query is not None:
            parts = [{"board": "ad-hoc", "designators": "-", "quantity": 1, "mpn": "",
                      "comment": "", "lcsc_reference": "", "query": text.strip(),
                      "source": "command line"} for text in args.query if text.strip()]
            if len(parts) != len(args.query):
                raise SourcingError("--query needs non-empty search text")
        else:
            parts = read_bom(args.base_bom, "base") + read_bom(args.euro_bom, "euro") + read_extras(args.extras)
        if not parts:
            raise SourcingError("no parts found")
        client = DigiKey(os.environ.get("DIGIKEY_CLIENT_ID", ""), os.environ.get("DIGIKEY_CLIENT_SECRET", "")) if args.digikey_live else None
        report, failed = build_report(parts, client, args.limit, suppliers)
        output = render_csv(report) if args.format == "csv" else json.dumps(report, indent=2, ensure_ascii=False) + "\n"
        if args.output:
            args.output.write_text(output, encoding="utf-8")
        else:
            sys.stdout.write(output)
        return 1 if failed else 0
    except (SourcingError, OSError) as exc:
        print(f"sourcing: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())

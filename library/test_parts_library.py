#!/usr/bin/env python3
"""Offline tests for the shared part library; no shop account or network."""
import contextlib
import csv
import importlib.util
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest

import parts_library as p

HEADER = p.COLUMNS


def write_library(path: Path, rows: list[dict]) -> Path:
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=HEADER)
        writer.writeheader()
        for row in rows:
            writer.writerow({key: row.get(key, "") for key in HEADER})
    return path


GOOD = {
    "id": "res-10k-025w", "category": "resistor", "description": "10 kOhm 1/4 W metal film",
    "value": "10k", "package": "0207", "rating": "250 V", "tolerance": "1%",
    "conrad_sku": "140023", "conrad_price_sek": "0.35", "conrad_moq": "10",
    "electrokit_sku": "41000510", "electrokit_price_sek": "0.10", "electrokit_moq": "1",
    "u3shop_sku": "", "u3shop_price_eur": "", "u3shop_moq": "",
    "datasheet": "", "status": "preferred", "updated": "2026-11-20", "notes": "",
}


def fresh(**changes) -> dict:
    return {**GOOD, **changes}


class UrlTests(unittest.TestCase):
    def test_search_urls_escape_query(self):
        query = '1×16 / 2mm + "test"'
        keys = {"conrad": "search", "electrokit": "keyword", "u3shop": "q"}
        from urllib.parse import parse_qs, urlparse
        for shop, key in keys.items():
            with self.subTest(shop=shop):
                self.assertEqual(parse_qs(urlparse(p.search_url(shop, query)).query)[key], [query])
        with self.assertRaises(p.LibraryError):
            p.search_url("mouser", "x")

    def test_product_url_uses_the_3u_shop_slug(self):
        self.assertEqual(p.product_url("u3shop", "pj398sm-3-5mm-monobuchse"),
                         "https://www.3u-shop.de/p/pj398sm-3-5mm-monobuchse")
        self.assertIn("keyword=40350001", p.product_url("electrokit", "40350001"))
        self.assertIn("search=152184", p.product_url("conrad", "152184"))
        with self.assertRaises(p.LibraryError):
            p.product_url("conrad", "  ")

    def test_links_agree_with_the_froemix_sourcing_tool(self):
        path = Path(__file__).resolve().parents[1] / "froemix/tools/source_parts.py"
        spec = importlib.util.spec_from_file_location("source_parts_shared", path)
        module = importlib.util.module_from_spec(spec)
        sys.modules["source_parts_shared"] = module
        spec.loader.exec_module(module)
        for shop in p.SHOPS:
            with self.subTest(shop=shop):
                self.assertEqual(p.search_url(shop, "NE555 / 8"), module.search_url(shop, "NE555 / 8"))


class LoadAndValidateTests(unittest.TestCase):
    def test_load_needs_required_columns_and_skips_blank_rows(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "components.csv"
            path.write_text("id,category\nabc,resistor\n")
            with self.assertRaises(p.LibraryError):
                p.load(path)
            write_library(path, [GOOD])
            with path.open("a", encoding="utf-8") as handle:
                handle.write("\n,,,\n")
            rows = p.load(path)
            self.assertEqual([row["id"] for row in rows], ["res-10k-025w"])
            with self.assertRaises(p.LibraryError):
                p.load(Path(tmp) / "absent.csv")

    def test_good_row_passes(self):
        errors, warnings = p.validate([p.load(write_library(Path(tempfile.mkdtemp()) / "c.csv", [GOOD]))[0]],
                                      today=p.date(2026, 11, 21))
        self.assertEqual(errors, [])
        self.assertEqual(warnings, [])

    def test_errors_are_specific(self):
        cases = {
            "id": fresh(id="Res 10k"),
            "duplicate": None,
            "category": fresh(category="inductor"),
            "status": fresh(status="favourite"),
            "no sku": fresh(conrad_sku="", conrad_price_sek="", electrokit_sku="", electrokit_price_sek=""),
            "price without sku": fresh(electrokit_sku="", electrokit_price_sek="0.10"),
            "sku without price": fresh(u3shop_sku="12345", u3shop_price_eur=""),
            "price text": fresh(conrad_price_sek="about 1 kr"),
            "negative price": fresh(conrad_price_sek="-1"),
            "moq zero": fresh(conrad_moq="0"),
            "bad date": fresh(updated="20.11.2026"),
            "description": fresh(description=""),
        }
        for name, row in cases.items():
            with self.subTest(case=name):
                rows = [p.load(write_library(Path(tempfile.mkdtemp()) / "c.csv",
                                             [row if row else GOOD, fresh(id="res-22k-025w", value="22k")]))]
                rows = rows[0]
                errors, _ = p.validate(rows, today=p.date(2026, 11, 21))
                self.assertTrue(errors or name == "duplicate")
        # The duplicate case keeps the same id twice.
        rows = p.load(write_library(Path(tempfile.mkdtemp()) / "c.csv", [GOOD, GOOD]))
        errors, _ = p.validate(rows, today=p.date(2026, 11, 21))
        self.assertTrue(any("duplicate id" in error for error in errors))

    def test_warnings_do_not_block(self):
        rows = p.load(write_library(Path(tempfile.mkdtemp()) / "c.csv", [
            fresh(updated="2024-01-01", notes="old"),
            fresh(id="cap-cer-100n-50v", category="cap-ceramic", value="100n", package="2.5mm",
                  status="draft", updated="2026-11-01"),
        ]))
        errors, warnings = p.validate(rows, today=p.date(2026, 11, 21))
        self.assertEqual(errors, [])
        self.assertTrue(any("days old" in warning for warning in warnings))

    def test_missing_value_warns(self):
        rows = p.load(write_library(Path(tempfile.mkdtemp()) / "c.csv", [fresh(value="")]))
        errors, warnings = p.validate(rows, today=p.date(2026, 11, 21))
        self.assertEqual(errors, [])
        self.assertTrue(any("value is empty" in warning for warning in warnings))

    def test_a_draft_may_hold_a_sku(self):
        rows = p.load(write_library(Path(tempfile.mkdtemp()) / "c.csv", [fresh(status="draft")]))
        errors, warnings = p.validate(rows, today=p.date(2026, 11, 21))
        self.assertEqual((errors, warnings), ([], []))

    def test_draft_may_have_no_sku(self):
        row = fresh(id="lcd-16x2", status="draft",
                    conrad_sku="", conrad_price_sek="", electrokit_sku="", electrokit_price_sek="")
        errors, _ = p.validate([row], today=p.date(2026, 11, 21))
        self.assertEqual(errors, [])

    def test_future_date_is_a_warning(self):
        errors, warnings = p.validate([fresh(updated="2027-01-01")], today=p.date(2026, 11, 21))
        self.assertEqual(errors, [])
        self.assertTrue(any("future" in warning for warning in warnings))


class MatchTests(unittest.TestCase):
    def setUp(self):
        self.rows = [
            fresh(),
            fresh(id="res-10k-0204", description="10 kOhm 1/4 W small", value="10k", package="0204",
                  updated="2026-11-20"),
            fresh(id="res-1k-025w", description="1 kOhm 1/4 W", value="1k"),
        ]

    def test_find_and_empty_text(self):
        self.assertEqual([row["id"] for row in p.find(self.rows, "1/4 w")],
                         ["res-10k-025w", "res-10k-0204", "res-1k-025w"])
        self.assertEqual([row["id"] for row in p.find(self.rows, "10K")],
                         ["res-10k-025w", "res-10k-0204"])
        with self.assertRaises(p.LibraryError):
            p.find(self.rows, "  ")

    def test_footprint_narrows_candidates(self):
        self.assertEqual(p.match_bom(self.rows, "10k", "R_0204"), ["res-10k-0204"])
        self.assertEqual(p.match_bom(self.rows, "10k", ""), ["res-10k-025w", "res-10k-0204"])
        self.assertEqual(p.match_bom(self.rows, "10k", "R_0207"), ["res-10k-025w"])
        self.assertEqual(p.match_bom(self.rows, "47k"), [])

    def test_plan_statuses(self):
        planned = p.plan_bom(self.rows, [
            {"value": "10k", "quantity": 2, "footprint": "R_0207", "designators": "R1,R2"},
            {"value": "10k", "quantity": 1, "footprint": "", "designators": "R3"},
            {"value": "47k", "quantity": 1, "footprint": "", "designators": "R4"},
        ])
        self.assertEqual([line["status"] for line in planned], ["matched", "ambiguous", "unmatched"])
        self.assertEqual(planned[0]["id"], "res-10k-025w")
        self.assertEqual(planned[1]["id"], "")


class BomTests(unittest.TestCase):
    def write_bom(self, text: str) -> Path:
        path = Path(tempfile.mkdtemp()) / "bom.csv"
        path.write_text(text, encoding="utf-8")
        return path

    def test_read_bom_accepts_value_or_comment(self):
        lines = p.read_bom(self.write_bom("Quantity,Value,Footprint,Designators\n2,10k,R_0207,R1,R2\n"))
        self.assertEqual(lines[0]["quantity"], 2)
        self.assertEqual(lines[0]["value"], "10k")
        lines = p.read_bom(self.write_bom("Quantity,Comment\n1,10k\n"))
        self.assertEqual(lines[0]["footprint"], "")
        with self.assertRaises(p.LibraryError):
            p.read_bom(self.write_bom("Quantity,Ref\n1,R1\n"))
        with self.assertRaises(p.LibraryError):
            p.read_bom(self.write_bom("Quantity,Value\n0,10k\n"))

    def test_order_list_raises_quantity_to_moq(self):
        rows = p.load(write_library(Path(tempfile.mkdtemp()) / "c.csv", [GOOD]))
        planned = p.plan_bom(rows, [{"value": "10k", "quantity": 3, "footprint": "", "designators": "R1"}])
        order, missing = p.order_lines(rows, planned, "conrad")
        self.assertEqual(missing, [])
        self.assertEqual(order[0]["sku"], "140023")
        self.assertEqual(order[0]["quantity"], 10)
        self.assertEqual(order[0]["price"], "0.35")
        self.assertIn("search=140023", order[0]["url"])
        order, _ = p.order_lines(rows, planned, "electrokit")
        self.assertEqual(order[0]["quantity"], 3)

    def test_order_list_reports_unfilled_lines(self):
        rows = p.load(write_library(Path(tempfile.mkdtemp()) / "c.csv", [GOOD]))
        planned = p.plan_bom(rows, [{"value": "10k", "quantity": 1, "footprint": "", "designators": "R1"}])
        order, missing = p.order_lines(rows, planned, "u3shop")
        self.assertEqual(order, [])
        self.assertEqual(missing[0]["value"], "10k")

    def test_plan_csv_is_formula_safe(self):
        rows = p.load(write_library(Path(tempfile.mkdtemp()) / "c.csv", [GOOD]))
        planned = p.plan_bom(rows, [{"value": "=CMD()", "quantity": 1, "footprint": "", "designators": "@A1"}])
        text = p.render_plan_csv(planned)
        self.assertIn("'=CMD()", text)
        self.assertIn("'@A1", text)


class MainTests(unittest.TestCase):
    def test_check_reports_and_fails_on_errors(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = write_library(Path(tmp) / "c.csv", [fresh(category="inductor")])
            err, out = io.StringIO(), io.StringIO()
            with contextlib.redirect_stderr(err), contextlib.redirect_stdout(out):
                status = p.main(["--library", str(path), "check"])
            self.assertEqual(status, 1)
            self.assertIn("category", err.getvalue())
            self.assertIn("1 parts", out.getvalue())

    def test_bom_shop_mode_prints_order_list_and_exit_status(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = write_library(Path(tmp) / "c.csv", [GOOD])
            bom = Path(tmp) / "bom.csv"
            bom.write_text("Quantity,Value\n5,10k\n", encoding="utf-8")
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                status = p.main(["--library", str(path), "bom", str(bom), "--shop", "conrad"])
            self.assertEqual(status, 0)
            self.assertIn("140023", out.getvalue())
            self.assertIn("10 x", out.getvalue())

    def test_find_json_and_links(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = write_library(Path(tmp) / "c.csv", [GOOD])
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                self.assertEqual(p.main(["--library", str(path), "find", "10k", "--format", "json"]), 0)
            self.assertEqual(json.loads(out.getvalue())[0]["id"], "res-10k-025w")
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                self.assertEqual(p.main(["links", "NE555"]), 0)
            self.assertEqual(len(out.getvalue().strip().splitlines()), len(p.SHOPS))


if __name__ == "__main__":
    unittest.main()

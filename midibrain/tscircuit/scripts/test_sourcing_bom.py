#!/usr/bin/env python3
"""Tests for sourcing_bom.py (run: python3 tscircuit/scripts/test_sourcing_bom.py).

These are unit tests on synthetic fixtures plus regression checks on the real
curated list. They do not need a built board; the release run is the only place
that reads ``dist/`` and ``fab/``.
"""
from __future__ import annotations

import csv
import io
import json
import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import sourcing_bom as sb  # noqa: E402

SCRIPTS = pathlib.Path(__file__).resolve().parent
EXTRAS = SCRIPTS / "sourcing-extras.csv"

BOM_HEADER = ["Comment", "Designator", "Footprint", "JLCPCB Part #", "Value"]
PNP_HEADER = ["Designator", "Mid X", "Mid Y", "Layer", "Rotation"]


def write_csv(path: pathlib.Path, header, rows) -> pathlib.Path:
    with path.open("w", newline="") as fh:
        w = csv.writer(fh)
        w.writerow(header)
        w.writerows(rows)
    return path


def bom_rows(*rows):
    return [dict(zip(BOM_HEADER, r)) for r in rows]


def pnp_rows(*rows):
    return [dict(zip(PNP_HEADER, r)) for r in rows]


class ReadCsvStrictTest(unittest.TestCase):
    def test_accepts_matching_rows(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = write_csv(pathlib.Path(tmp) / "a.csv", sb.COLUMNS,
                          [["module", "X1", "1", "MPN", "", "", "rear", "hand",
                            "shop", "note"]])
            header, rows = sb.read_csv_strict(p, sb.COLUMNS)
            self.assertEqual(header, sb.COLUMNS)
            self.assertEqual(rows[0]["Notes"], "note")
            self.assertEqual(rows[0]["Side"], "rear")

    def test_extra_field_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = pathlib.Path(tmp) / "a.csv"
            p.write_text(
                ",".join(sb.COLUMNS) + "\n"
                "module,X1,1,MPN,,rear,hand,,note\n")
            with self.assertRaises(sb.SourcingError) as ctx:
                sb.read_csv_strict(p, sb.COLUMNS)
            self.assertIn(":2:", str(ctx.exception))

    def test_missing_field_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = pathlib.Path(tmp) / "a.csv"
            p.write_text(
                ",".join(sb.COLUMNS) + "\n"
                "module,X1,1,MPN,,,rear,hand,shop,n1,extra,more\n"
                "module,X2,1,MPN,,,rear,hand,shop\n")
            with self.assertRaises(sb.SourcingError) as ctx:
                sb.read_csv_strict(p, sb.COLUMNS)
            self.assertIn(":3:", str(ctx.exception))

    def test_wrong_header_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = write_csv(pathlib.Path(tmp) / "a.csv",
                          sb.COLUMNS[:-1],
                          [["module", "X1", "1", "MPN", "", "", "rear", "hand",
                            "shop"]])
            with self.assertRaises(sb.SourcingError):
                sb.read_csv_strict(p, sb.COLUMNS)

    def test_blank_row_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = pathlib.Path(tmp) / "a.csv"
            p.write_text(",".join(sb.COLUMNS) + "\n\n")
            with self.assertRaises(sb.SourcingError):
                sb.read_csv_strict(p, sb.COLUMNS)


class CuratedListTest(unittest.TestCase):
    """Regression checks on the real file the release ships."""

    def setUp(self):
        self.header, self.rows = sb.read_csv_strict(EXTRAS, sb.COLUMNS)

    def by_id(self, designator):
        hits = [r for r in self.rows if r["Designators"] == designator]
        self.assertEqual(len(hits), 1, designator)
        return hits[0]

    def test_every_row_has_ten_fields(self):
        self.assertEqual(self.header, sb.COLUMNS)
        self.assertGreater(len(self.rows), 20)
        for row in self.rows:
            self.assertEqual(len(row), 10)

    def test_usb_c_panel_is_flagged_rejected_for_five_amp(self):
        row = self.by_id("CAB_USBC_PANEL")
        notes = row["Notes"]
        self.assertIn("3 A rated not 5 A", notes)      # original warning kept
        self.assertIn("REJECTED", notes)               # not approved for 5 A
        self.assertIn("5 A", notes)
        self.assertIn("pending", notes)                # exact part still open
        self.assertEqual(row["Method"], "hand")         # supplier/method kept
        self.assertEqual(row["Source"], "SparkFun")
        self.assertEqual(row["Side"], "panel")
        self.assertEqual(row["Package"], "cable")

    def test_knob_and_light_pipe_stay_pending(self):
        self.assertIn("Not specified yet", self.by_id("KNOB1")["Notes"])
        self.assertIn("pending", self.by_id("KNOB1")["Notes"])
        self.assertIn("pending", self.by_id("LP1-3")["Notes"])

    def test_no_row_claims_a_passed_five_amp_test(self):
        for row in self.rows:
            lowered = row["Notes"].lower()
            self.assertNotIn("test passed", lowered)
            self.assertNotIn("approved for 5 a", lowered)

    def test_late_row_mutation_fails(self):
        """A comma added to a row near the end must fail, not shift fields."""
        lines = EXTRAS.read_text(encoding="utf-8").splitlines()
        target = len(lines) - 3                       # a late hardware row
        lines[target] = lines[target].replace(",", ",,", 1)
        with tempfile.TemporaryDirectory() as tmp:
            p = pathlib.Path(tmp) / "extras.csv"
            p.write_text("\n".join(lines) + "\n", encoding="utf-8")
            with self.assertRaises(sb.SourcingError) as ctx:
                sb.read_csv_strict(p, sb.COLUMNS)
            self.assertIn(f":{target + 1}:", str(ctx.exception))


class ProcessBoardTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.out = pathlib.Path(self._tmp.name)

    def tearDown(self):
        self._tmp.cleanup()

    def test_writes_split_files_and_counts(self):
        bom = bom_rows(
            ["100nF", "C1", "0603", "C14663", "100nF"],
            ["TRS", "J1", "PJ301", "C381127", "TRS"],
        )
        pnp = pnp_rows(
            ["C1", "10", "10", "top", "0"],
            ["J1", "20", "20", "bottom", "0"],
        )
        counts = sb.process_board("front", bom, pnp, self.out)
        self.assertEqual(counts["bom_placements"], 2)
        self.assertEqual(counts["cpl_smt_placements"], 1)
        self.assertEqual(counts["cpl_smt_top"], 1)
        self.assertEqual(counts["cpl_smt_bottom"], 0)
        self.assertEqual(counts["tht_excluded"], 1)
        for name in ("front-bom-jlc.csv", "front-cpl-jlc.csv",
                     "front-bom-smt.csv", "front-bom-hand.csv"):
            self.assertTrue((self.out / name).exists(), name)
        hand = (self.out / "front-bom-hand.csv").read_text()
        self.assertIn("J1", hand)
        self.assertIn("LCSC/JLCPCB", hand)
        smt = (self.out / "front-bom-smt.csv").read_text()
        self.assertIn("C14663", smt)

    def test_smt_without_lcsc_fails(self):
        bom = bom_rows(["100nF", "C1", "0603", "", "100nF"])
        pnp = pnp_rows(["C1", "10", "10", "top", "0"])
        with self.assertRaises(sb.SourcingError):
            sb.process_board("front", bom, pnp, self.out)

    def test_wrong_side_fails(self):
        bom = bom_rows(["100nF", "C1", "0603", "C14663", "100nF"])
        pnp = pnp_rows(["C1", "10", "10", "bottom", "0"])
        with self.assertRaises(sb.SourcingError) as ctx:
            sb.process_board("front", bom, pnp, self.out)
        self.assertIn("top side", str(ctx.exception))

    def test_designator_mismatch_fails(self):
        bom = bom_rows(["100nF", "C1", "0603", "C14663", "100nF"])
        pnp = pnp_rows(["C2", "10", "10", "top", "0"])
        with self.assertRaises(sb.SourcingError):
            sb.process_board("front", bom, pnp, self.out)

    def test_new_smt_part_needs_a_side_and_lcsc(self):
        # A part that is not in the pinned THT set becomes SMT: it must carry
        # an LCSC number and sit on the pinned side, or the release fails.
        bom = bom_rows(["100nF", "C1", "0603", "C14663", "100nF"],
                       ["X", "R99", "0603", "C1", "1k"])
        pnp = pnp_rows(["C1", "10", "10", "top", "0"],
                       ["R99", "10", "10", "bottom", "0"])
        with self.assertRaises(sb.SourcingError) as ctx:
            sb.process_board("front", bom, pnp, self.out)
        self.assertIn("top side", str(ctx.exception))


class MalformedInputTest(unittest.TestCase):
    def test_malformed_quotes_are_a_clear_error(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = pathlib.Path(tmp) / "a.csv"
            p.write_text(",".join(sb.COLUMNS) + "\nmodule,X1,1,\"unterminated\n")
            with self.assertRaises(sb.SourcingError) as ctx:
                sb.read_csv_strict(p, sb.COLUMNS)
            self.assertIn("malformed CSV", str(ctx.exception))
            self.assertIn("a.csv", str(ctx.exception))


class DesignatorHygieneTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.out = pathlib.Path(self._tmp.name)

    def tearDown(self):
        self._tmp.cleanup()

    def test_duplicate_bom_designator_fails(self):
        bom = bom_rows(["100nF", "C1", "0603", "C14663", "100nF"],
                       ["100nF", "C1", "0603", "C14663", "100nF"])
        pnp = pnp_rows(["C1", "10", "10", "top", "0"])
        with self.assertRaises(sb.SourcingError) as ctx:
            sb.process_board("front", bom, pnp, self.out)
        self.assertIn("duplicate Designator C1", str(ctx.exception))

    def test_duplicate_cpl_designator_fails(self):
        bom = bom_rows(["100nF", "C1", "0603", "C14663", "100nF"])
        pnp = pnp_rows(["C1", "10", "10", "top", "0"],
                       ["C1", "10", "10", "top", "0"])
        with self.assertRaises(sb.SourcingError) as ctx:
            sb.process_board("front", bom, pnp, self.out)
        self.assertIn("duplicate Designator C1", str(ctx.exception))

    def test_empty_designator_fails(self):
        bom = bom_rows(["100nF", "", "0603", "C14663", "100nF"])
        pnp = pnp_rows(["", "10", "10", "top", "0"])
        with self.assertRaises(sb.SourcingError) as ctx:
            sb.process_board("front", bom, pnp, self.out)
        self.assertIn("empty Designator", str(ctx.exception))

    def test_lcsc_must_be_a_c_number(self):
        bom = bom_rows(["100nF", "C1", "0603", "14663", "100nF"])
        pnp = pnp_rows(["C1", "10", "10", "top", "0"])
        with self.assertRaises(sb.SourcingError) as ctx:
            sb.process_board("front", bom, pnp, self.out)
        self.assertIn("valid LCSC C-number", str(ctx.exception))

    def test_blank_lcsc_on_smt_fails(self):
        bom = bom_rows(["100nF", "C1", "0603", "", "100nF"])
        pnp = pnp_rows(["C1", "10", "10", "top", "0"])
        with self.assertRaises(sb.SourcingError) as ctx:
            sb.process_board("front", bom, pnp, self.out)
        self.assertIn("<blank>", str(ctx.exception))

    def test_supplier_text_is_not_accepted_as_an_lcsc_number(self):
        bom = bom_rows(["100nF", "C1", "0603", "LCSC C14663", "100nF"])
        pnp = pnp_rows(["C1", "10", "10", "top", "0"])
        with self.assertRaises(sb.SourcingError):
            sb.process_board("front", bom, pnp, self.out)


class RequiredColumnsTest(unittest.TestCase):
    def test_missing_bom_column_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = pathlib.Path(tmp) / "bom.csv"
            p.write_text("Comment,Designator,Footprint,Value\n100nF,C1,0603,100nF\n")
            import io
            import zipfile
            z = pathlib.Path(tmp) / "b.zip"
            with zipfile.ZipFile(z, "w") as zf:
                zf.writestr("bom.csv", p.read_text())
            with zipfile.ZipFile(z) as zf:
                with self.assertRaises(sb.SourcingError) as ctx:
                    sb.read_zip_csv(zf, "bom.csv", "front",
                                    sb.BOM_REQUIRED_COLUMNS)
            self.assertIn("missing columns", str(ctx.exception))

    def test_missing_member_fails(self):
        import zipfile
        with tempfile.TemporaryDirectory() as tmp:
            z = pathlib.Path(tmp) / "b.zip"
            with zipfile.ZipFile(z, "w") as zf:
                zf.writestr("other.csv", "a\n")
            with zipfile.ZipFile(z) as zf:
                with self.assertRaises(sb.SourcingError) as ctx:
                    sb.read_zip_csv(zf, "bom.csv", "front",
                                    sb.BOM_REQUIRED_COLUMNS)
            self.assertIn("no bom.csv", str(ctx.exception))


class EncoderProcessTest(unittest.TestCase):
    def test_encoder_row_asks_for_a_qualified_process_and_stays_out_of_cpl(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = pathlib.Path(tmp)
            bom = bom_rows(["ENCODER", "U6", "PEC11R", "C143790", "PEC11R"])
            pnp = pnp_rows(["U6", "10", "10", "bottom", "0"])
            counts = sb.process_board("front", bom, pnp, out)
            self.assertEqual(counts["cpl_smt_placements"], 0)
            self.assertEqual(counts["tht_excluded"], 1)
            rows = list(csv.DictReader(io.StringIO(
                (out / "front-bom-hand.csv").read_text())))
            self.assertEqual(len(rows), 1)
            row = rows[0]
            self.assertNotEqual(row["Method"], "hand")
            self.assertIn("qualified THT process", row["Method"])
            self.assertIn("not recommended", row["Notes"])
            self.assertIn("encoder_solder_process", row["Notes"])
            cpl = list(csv.DictReader(io.StringIO(
                (out / "front-cpl-jlc.csv").read_text())))
            self.assertEqual(cpl, [])

    def test_blank_optional_fields_stay_blank(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = pathlib.Path(tmp)
            bom = bom_rows(["2x20 header", "J_PI", "", "", "PEC20DAAN"])
            pnp = pnp_rows(["J_PI", "10", "10", "bottom", "0"])
            sb.process_board("rear", bom, pnp, out)
            rows = list(csv.DictReader(io.StringIO(
                (out / "rear-bom-hand.csv").read_text())))
            self.assertEqual(rows[0]["Package"], "")
            self.assertEqual(rows[0]["LCSC"], "")
            self.assertEqual(rows[0]["Method"], "hand")
            self.assertEqual(rows[0]["Source"], "generic (no LCSC in design)")


class ExpectedCountsTest(unittest.TestCase):
    def order(self, front, rear, tmp):
        path = pathlib.Path(tmp) / "jlc-order.json"
        path.write_text(json.dumps({
            "front": {"assembly": {"expected_counts": front}},
            "rear": {"assembly": {"expected_counts": rear}},
        }))
        return path

    def counts(self, n):
        return {"bom_placements": n, "cpl_smt_placements": n,
                "cpl_smt_top": n, "cpl_smt_bottom": n, "tht_excluded": n}

    def test_matching_counts_pass(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = self.order(self.counts(3), self.counts(4), tmp)
            sb.verify_expected_counts(path, {"front": self.counts(3),
                                             "rear": self.counts(4)})

    def test_mismatch_reports_exact_delta(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = self.order(self.counts(3), self.counts(18), tmp)
            rear = self.counts(20)
            with self.assertRaises(sb.SourcingError) as ctx:
                sb.verify_expected_counts(path, {"front": self.counts(3),
                                                 "rear": rear})
            msg = str(ctx.exception)
            self.assertIn("rear.cpl_smt_placements: jlc-order.json says 18, "
                          "generated 20 (delta +2)", msg)

    def test_missing_key_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            partial = {"bom_placements": 3}
            path = self.order(partial, partial, tmp)
            with self.assertRaises(sb.SourcingError) as ctx:
                sb.verify_expected_counts(path, {"front": self.counts(3),
                                                 "rear": self.counts(3)})
            self.assertIn("expected_counts missing", str(ctx.exception))

    def test_missing_board_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = pathlib.Path(tmp) / "jlc-order.json"
            path.write_text(json.dumps({"front": {"assembly": {
                "expected_counts": self.counts(3)}}}))
            with self.assertRaises(sb.SourcingError) as ctx:
                sb.verify_expected_counts(path, {"front": self.counts(3),
                                                 "rear": self.counts(3)})
            self.assertIn("rear: missing", str(ctx.exception))


class PanelTest(unittest.TestCase):
    def test_empty_panel_passes(self):
        sb.verify_panel_empty([], [])

    def test_panel_with_parts_fails(self):
        with self.assertRaises(sb.SourcingError):
            sb.verify_panel_empty([{"Designator": "C1"}], [])


class MergeTest(unittest.TestCase):
    def test_merge_keeps_notes_and_order(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = pathlib.Path(tmp)
            hand = write_csv(tmp / "front-bom-hand.csv", sb.COLUMNS,
                             [["front", "J1", "1", "TRS", "C381127", "PJ301",
                               "bottom", "hand", "LCSC/JLCPCB", "TRS jack"]])
            out = tmp / "sourcing-bom.csv"
            merged = sb.merge_sourcing([hand], EXTRAS, out)
            self.assertEqual(len(merged), len(sb.read_csv_strict(EXTRAS,
                                                                sb.COLUMNS)[1]) + 1)
            self.assertEqual(merged[0]["Designators"], "J1")
            self.assertTrue(any(r["Designators"] == "CAB_USBC_PANEL"
                                and "REJECTED" in r["Notes"] for r in merged))
            written = list(csv.DictReader(io.StringIO(out.read_text())))
            self.assertEqual(written[-1]["Notes"], merged[-1]["Notes"])


if __name__ == "__main__":
    unittest.main(verbosity=2)

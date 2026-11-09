#!/usr/bin/env python3
"""Offline tests; no distributor account or network required."""
import contextlib
import csv
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from urllib.parse import parse_qs, urlparse

import source_parts as s


class SourcingTests(unittest.TestCase):
    def test_search_urls_escape_query(self):
        query = '1×16 / 2mm + "test"'
        expected = {'rs': 'searchTerm', 'digikey': 'keywords', 'electrokit': 'keyword',
                    'conrad': 'search', 'u3shop': 'q'}
        self.assertEqual(set(expected), set(s.SUPPLIERS))
        for supplier, key in expected.items():
            with self.subTest(supplier=supplier):
                self.assertEqual(parse_qs(urlparse(s.search_url(supplier, query)).query)[key], [query])

    def test_supplier_filter_keeps_canonical_order(self):
        self.assertEqual(s.supplier_list('u3shop, conrad'), ('conrad', 'u3shop'))
        self.assertEqual(s.supplier_list('CONRAD'), ('conrad',))
        for bad in ('', ' , ', 'mouser', 'conrad,mouser'):
            with self.subTest(value=bad), self.assertRaises(s.SourcingError):
                s.supplier_list(bad)

    def test_adhoc_query_skips_bom_files(self):
        out = io.StringIO()
        with tempfile.TemporaryDirectory() as tmp, contextlib.redirect_stdout(out):
            status = s.main(['--query', 'NE555', '--suppliers', 'conrad,u3shop',
                             '--base-bom', str(Path(tmp) / 'absent.csv'),
                             '--euro-bom', str(Path(tmp) / 'absent.csv'),
                             '--extras', str(Path(tmp) / 'absent.csv')])
        self.assertEqual(status, 0)
        report = json.loads(out.getvalue())
        part = report['parts'][0]
        self.assertEqual(part['query'], 'NE555')
        self.assertEqual(sorted(part['suppliers']), ['conrad', 'u3shop'])
        self.assertEqual(part['suppliers']['conrad']['status'], 'manual_search')
        self.assertIn('conrad.se', part['suppliers']['conrad']['search_url'])
        self.assertEqual(report['parts'][0]['suppliers']['u3shop']['status'], 'manual_search')

    def test_empty_adhoc_query_fails(self):
        err = io.StringIO()
        with contextlib.redirect_stderr(err), contextlib.redirect_stdout(io.StringIO()):
            status = s.main(['--query', '   '])
        self.assertEqual(status, 1)
        self.assertIn('--query', err.getvalue())

    def test_query_does_not_treat_lcsc_id_as_mpn(self):
        self.assertEqual(s.useful_query('C65114', 'C65114'), '')
        self.assertEqual(s.useful_query('C2749', '100uF 50V'), '100uF 50V')
        self.assertEqual(s.useful_query('RK09K1130A8G', 'C470294'), 'RK09K1130A8G')

    def test_bom_and_extra_validation(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'bom.csv'
            with path.open('w', newline='') as f:
                writer = csv.writer(f)
                writer.writerow(['LCSC Part #', 'Quantity', 'Comment', 'MPN', 'Designators'])
                writer.writerow(['C1', '2', 'test', 'PART', 'CN1,CN2'])
            rows = s.read_bom(path, 'base')
            self.assertEqual(rows[0]['quantity'], 2)
            self.assertEqual(rows[0]['designators'], 'CN1,CN2')
            path.write_text('Quantity,Comment\n1,test\n')
            with self.assertRaises(s.SourcingError):
                s.read_bom(path, 'base')
            path.write_text('board,designators,quantity,query\nbase,shunts,0,jumper\n')
            with self.assertRaises(s.SourcingError):
                s.read_extras(path)

    def test_link_report_is_not_a_stock_claim(self):
        part = {'query': 'part', 'mpn': 'part'}
        report, failed = s.build_report([part], None, 5)
        self.assertFalse(failed)
        for vendor in s.SUPPLIERS:
            self.assertEqual(report['parts'][0]['suppliers'][vendor]['status'], 'manual_search')
            self.assertEqual(report['parts'][0]['suppliers'][vendor]['candidates'], [])
        self.assertIn('Conrad Sweden', report['market'])
        self.assertIn('3U-Shop', report['market'])

    def test_live_api_token_request_response_and_cache(self):
        calls = []
        def fake_request(url, body, headers):
            calls.append((url, body, headers))
            if url.endswith('/token'):
                return {'access_token': 'private-token', 'expires_in': 600}
            return {'Products': [{'ManufacturerProductNumber': 'ABC-1', 'Manufacturer': {'Name': 'Example'},
                                  'Description': {'ProductDescription': 'test'}, 'QuantityAvailable': 0,
                                  'ProductVariations': [{'DigiKeyProductNumber': 'ABC-1-ND',
                                                         'MinimumOrderQuantity': 4, 'StandardPricing': []}]}]}
        with patch.object(s, 'json_request', side_effect=fake_request):
            client = s.DigiKey('client', 'secret')
            parts = [{'query': 'ABC-1'}, {'query': 'ABC-1'}, {'query': ''}]
            report, failed = s.build_report(parts, client, 5)
        self.assertFalse(failed)
        self.assertEqual(len(calls), 2)
        self.assertEqual(calls[1][2]['X-DIGIKEY-Locale-Site'], 'SE')
        self.assertEqual(json.loads(calls[1][1])['Keywords'], 'ABC-1')
        candidate = report['parts'][0]['suppliers']['digikey']['candidates'][0]
        self.assertTrue(candidate['exact_query_match'])
        self.assertEqual(candidate['quantity_available'], 0)
        self.assertEqual(candidate['variations'][0]['minimum_order_quantity'], 4)
        self.assertEqual(candidate['compatibility'], 'not reviewed')
        self.assertNotIn('private-token', json.dumps(report))
        self.assertNotIn('secret', json.dumps(report))
        self.assertEqual(report['parts'][2]['suppliers']['rs']['status'], 'query_required')

    def test_live_query_skips_a_supplier_it_cannot_use(self):
        client = s.DigiKey('id', 'secret')
        with patch.object(client, 'search', side_effect=AssertionError('must not be called')):
            report, failed = s.build_report([{'query': 'ABC'}], client, 5, ('conrad',))
        self.assertFalse(failed)
        self.assertEqual(sorted(report['parts'][0]['suppliers']), ['conrad'])

    def test_digikey_live_needs_digikey_in_the_filter(self):
        err = io.StringIO()
        with contextlib.redirect_stderr(err):
            status = s.main(['--query', 'ABC', '--suppliers', 'conrad', '--digikey-live'])
        self.assertEqual(status, 1)
        self.assertIn('digikey', err.getvalue())

    def test_csv_flatten_and_formula_safety(self):
        part = {'board': 'base', 'designators': 'CN1,CN2', 'quantity': 2, 'mpn': '=unsafe', 'query': 'part'}
        report, _ = s.build_report([part], None, 5)
        rows = list(csv.DictReader(io.StringIO(s.render_csv(report))))
        self.assertEqual(len(rows), len(s.SUPPLIERS))
        self.assertEqual(rows[0]['mpn'], "'=unsafe")
        self.assertEqual(rows[0]['designators'], 'CN1,CN2')
        self.assertEqual(rows[0]['status'], 'manual_search')

    def test_api_errors_are_reported_not_no_results(self):
        client = s.DigiKey('id', 'secret')
        with patch.object(client, 'search', side_effect=s.SourcingError('DigiKey API returned HTTP 429')) as search:
            report, failed = s.build_report([{'query': 'ABC'}, {'query': 'DEF'}], client, 5)
        self.assertTrue(failed)
        self.assertEqual(search.call_count, 1)
        self.assertEqual(report['parts'][0]['suppliers']['digikey']['status'], 'error')
        self.assertIn('429', report['parts'][0]['suppliers']['digikey']['error'])
        self.assertIn('not queried', report['parts'][1]['suppliers']['digikey']['error'])

    def test_bad_credentials_fail_without_disclosing_them(self):
        with tempfile.TemporaryDirectory() as tmp, patch.dict(s.os.environ, {'DIGIKEY_CLIENT_ID': '', 'DIGIKEY_CLIENT_SECRET': 'secret'}):
            path = Path(tmp) / 'bom.csv'
            path.write_text('LCSC Part #,Quantity,Comment,MPN,Designators\nC1,1,part,part,R1\n')
            extras = Path(tmp) / 'extras.csv'
            extras.write_text('board,designators,quantity,query\n')
            err = io.StringIO()
            with contextlib.redirect_stderr(err):
                status = s.main(['--base-bom', str(path), '--euro-bom', str(path), '--extras', str(extras), '--digikey-live'])
            self.assertEqual(status, 1)
            self.assertNotIn('secret', err.getvalue())


if __name__ == '__main__':
    unittest.main()

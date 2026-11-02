#!/usr/bin/env python3
"""Mutation tests against the built front-board mux contract."""
import copy
import unittest

import check_madc


class MuxContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.circuit = check_madc.load(check_madc.CIRCUIT)

    def test_built_circuit_passes(self):
        self.assertEqual(check_madc.validate(self.circuit), [])

    def test_removed_pullup_fails(self):
        circuit = copy.deepcopy(self.circuit)
        circuit[:] = [e for e in circuit if not (e['type'] == 'source_component' and e.get('name') == 'R8')]
        self.assertTrue(any('R8: 10k MODE pull-up is missing' in p for p in check_madc.validate(circuit)))

    def test_wrong_pullup_value_fails(self):
        circuit = copy.deepcopy(self.circuit)
        r8 = next(e for e in circuit if e['type'] == 'source_component' and e.get('name') == 'R8')
        r8['resistance'] = 100000
        self.assertTrue(any('R8: expected 10k' in p for p in check_madc.validate(circuit)))

    def test_disconnected_pullup_fails(self):
        circuit = copy.deepcopy(self.circuit)
        r8 = next(e for e in circuit if e['type'] == 'source_component' and e.get('name') == 'R8')
        ids = {e['source_port_id'] for e in circuit if e['type'] == 'source_port' and e.get('source_component_id') == r8['source_component_id']}
        for e in circuit:
            if e['type'] == 'source_trace':
                e['connected_source_port_ids'] = [p for p in e.get('connected_source_port_ids', []) if p not in ids]
        self.assertTrue(any('R8: expected P3V3_A/M_ADC_7' in p for p in check_madc.validate(circuit)))

    def test_reintroduced_audio_indicator_fails(self):
        circuit = copy.deepcopy(self.circuit)
        component = next(e for e in circuit if e['type'] == 'source_component' and e.get('name') == 'DL5')
        component['name'] = 'DL3'
        self.assertTrue(any('DL3: audio activity LED parts' in p for p in check_madc.validate(circuit)))


if __name__ == '__main__':
    unittest.main()

"""Prevent empty/skipped suites and build/run manifest drift."""
import copy
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from firmware_scenarios import load_scenarios, select_scenarios


class ManifestTests(unittest.TestCase):
    def load(self, value):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'manifest.json'
            path.write_text(json.dumps(value))
            return load_scenarios(path)

    def test_repository_sources_and_demo_are_present(self):
        root = Path(__file__).resolve().parents[1]
        scenarios = load_scenarios()
        self.assertTrue(select_scenarios(scenarios, demo=True))
        for scenario in scenarios:
            self.assertTrue((root / 'firmware/tests' / scenario['source']).is_file())

    def test_empty_or_malformed_manifest_fails(self):
        for value in ([], {}, [None], [{'name': 'boot', 'stage': 'boot'}]):
            with self.subTest(value=value), self.assertRaises(ValueError):
                self.load(value)

    def test_invalid_fields_and_duplicate_names_fail(self):
        valid = dict(name='boot', stage='boot', expected=['PASS boot'])
        for field, bad in [('name', '../boot'), ('stage', 'oops'), ('expected', []),
                           ('expected', ['']), ('options', '-bad'), ('trace', 'false'),
                           ('source', '../../boot.c'), ('defines', ['-bad']), ('machine', 2)]:
            value = copy.deepcopy(valid)
            value[field] = bad
            with self.subTest(field=field), self.assertRaises(ValueError):
                self.load([value])
        with self.assertRaises(ValueError):
            self.load([valid, valid])

    def test_empty_selection_fails(self):
        scenarios = self.load([dict(name='boot', stage='boot', expected=['PASS boot'])])
        for selection in ({'name': 'missing'}, {'demo': True}):
            with self.subTest(selection=selection), self.assertRaises(ValueError):
                select_scenarios(scenarios, **selection)

    def test_stage_selection_includes_previous_gates(self):
        scenarios = load_scenarios()
        self.assertEqual([s['name'] for s in select_scenarios(scenarios, stage='boot')], ['boot'])
        self.assertEqual([s['name'] for s in select_scenarios(scenarios, stage='detect')], ['boot', 'detect'])


if __name__ == '__main__':
    unittest.main()

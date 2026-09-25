import importlib.util
from pathlib import Path
import unittest
ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('filters', ROOT / 'tools/generate-scaler-coefficients.py')
filters = importlib.util.module_from_spec(spec)
spec.loader.exec_module(filters)
class FiltersTest(unittest.TestCase):
    def test_generated_file_matches_formula(self):
        self.assertEqual((ROOT / 'driver/gc573_scaler_coefficients.h').read_text(), filters.generate())
        for ratio in ((3, 2), (4, 3), (2, 1)):
            for row in filters.coefficients(*ratio):
                self.assertEqual(sum(row), 4096)
                self.assertTrue(all(-32768 <= x <= 32767 for x in row))

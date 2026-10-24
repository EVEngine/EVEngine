"""Geometric collision checks must detect crossings, not only vertex containment."""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from road_junction_safety import audit, overlap


class GuardrailFootprintTests(unittest.TestCase):
    def test_separated_guardrail(self):
        self.assertTrue(audit([[(0, 0), (2, 0), (0, 2)]], [[(3, 0), (4, 0), (3, 1)]])["clear"])

    def test_boundary_touch_is_not_penetration(self):
        self.assertEqual(overlap([(0, 0), (1, 0), (0, 1)], [(1, 0), (2, 0), (1, 1)]), 0)

    def test_crossing_edges_with_no_contained_vertices(self):
        result = audit([[(-2, -1), (2, -1), (0, 2)]], [[(-2, 1), (2, 1), (0, -2)]])
        self.assertFalse(result["clear"])
        self.assertGreater(result["maxOverlapSquareMeters"], 0)

    def test_reversed_winding_same_overlap(self):
        a = [(0, 0), (2, 0), (0, 2)]
        self.assertAlmostEqual(overlap(a, a), overlap(a, list(reversed(a))))

"""Reject flat, missing and incorrectly graded bridge reference geometry."""

import math
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from road_options_parity import heights_at, measure_bridge


def surface(y, grade=0):
    points = [
        (-32, y - 32 * grade, -3),
        (32, y + 32 * grade, -3),
        (32, y + 32 * grade, 3),
        (-32, y - 32 * grade, 3),
    ]
    return [[points[0], points[1], points[2]], [points[0], points[2], points[3]]]


class BridgeEvidenceTests(unittest.TestCase):
    def test_actual_grade_and_clearance(self):
        actual = measure_bridge(
            surface(0) + surface(10, 0.08) + surface(9.2, 0.08),
            {"angleDegrees": 0, "gradePercent": 8},
        )
        self.assertAlmostEqual(actual["centerClearanceMeters"], 9.2)
        self.assertAlmostEqual(actual["measuredGradePercent"], 8)

    def test_rotated_crossing(self):
        angle = math.radians(60)
        upper = [
            [
                (
                    x * math.cos(angle) - z * math.sin(angle),
                    y,
                    x * math.sin(angle) + z * math.cos(angle),
                )
                for x, y, z in t
            ]
            for t in surface(10, 0.12) + surface(9.2, 0.12)
        ]
        actual = measure_bridge(surface(0) + upper, {"angleDegrees": 60, "gradePercent": 12})
        self.assertAlmostEqual(actual["measuredGradePercent"], 12)

    def test_flattened_bridge_is_rejected(self):
        with self.assertRaises(ValueError):
            measure_bridge(surface(0), {"angleDegrees": 0, "gradePercent": 8})

    def test_wrong_grade_is_rejected(self):
        with self.assertRaises(ValueError):
            measure_bridge(surface(0) + surface(10), {"angleDegrees": 0, "gradePercent": 8})

    def test_insufficient_clearance_is_rejected(self):
        with self.assertRaises(ValueError):
            measure_bridge(surface(0) + surface(7, 0.08), {"angleDegrees": 0, "gradePercent": 8})

    def test_degenerate_or_outside_triangles_do_not_supply_height(self):
        self.assertEqual(heights_at([[(0, 99, 0)] * 3] + surface(10), 100, 100), [])

    def test_reverse_winding_preserves_height(self):
        self.assertEqual(
            heights_at(surface(10), 0, 0), heights_at([t[::-1] for t in surface(10)], 0, 0)
        )

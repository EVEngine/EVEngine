"""Reject author/reference generation mismatches before publishing real frames."""

import copy
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from road_junction_parity import validate_generation


class JunctionGenerationTests(unittest.TestCase):
    def setUp(self):
        self.author = {
            "schema": "eve.tests.road-junction-input",
            "version": 2,
            "roads": [{"sidewalks": True}, {"sidewalks": False}],
            "routes": [{"firstEdge": 1, "secondEdge": 3}],
            "ringRadius": 12,
            "ringWidth": 3.25,
        }
        self.evidence = {
            "generatedWalks": 2,
            "explicitRoutes": 1,
            "measuredRingRadius": 12,
            "measuredRingWidth": 3.25,
        }

    def test_matching_generation_is_accepted(self):
        validate_generation(self.author, self.evidence)

    def test_empty_sidewalk_or_route_is_rejected(self):
        for field in ("generatedWalks", "explicitRoutes"):
            candidate = copy.deepcopy(self.evidence)
            candidate[field] = 0
            with self.assertRaises(ValueError):
                validate_generation(self.author, candidate)

    def test_assigned_but_unbuilt_ring_is_rejected(self):
        for field, value in (("measuredRingRadius", 8), ("measuredRingWidth", 5)):
            candidate = copy.deepcopy(self.evidence)
            candidate[field] = value
            with self.assertRaises(ValueError):
                validate_generation(self.author, candidate)

    def test_old_input_version_is_rejected(self):
        self.author["version"] = 1
        with self.assertRaises(ValueError):
            validate_generation(self.author, self.evidence)

    def test_actual_sdk_width_mismatch_is_rejected(self):
        self.evidence["attachments"] = [{"requestedWidth": 7, "actualWidth": 6}]
        with self.assertRaises(ValueError):
            validate_generation(self.author, self.evidence)

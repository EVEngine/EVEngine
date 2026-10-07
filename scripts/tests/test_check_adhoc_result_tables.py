"""Fixture tests for the ad-hoc Result table gate."""

from __future__ import annotations

import sys
import unittest
from datetime import date
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))

import check_adhoc_result_tables as check


class AdhocResultTablesTest(unittest.TestCase):
    def test_projector_exempt_and_allowlist_pass(self):
        allowlist = {
            "schema_version": 1,
            "baselineCount": 2,
            "policy": {"allowNetGrowth": False, "metadataFileIsReadOnly": True},
            "exempt": ["src/engine/common/SquirrelBinding.cpp"],
            "default": {
                "owner": "engine-scripting",
                "issue": "TEST",
                "reason": "fixture",
                "expiry": "2099-01-01",
            },
            "files": {"src/modules/inventory/Inventory.cpp": {}},
        }
        self.assertEqual(check.validate_allowlist(allowlist, today=date(2026, 10, 7)), [])
        hits = [
            ("src/engine/common/SquirrelBinding.cpp", 1, 'result.set("ok", true);'),
            ("src/modules/inventory/Inventory.cpp", 1, 'result.set("ok", true);'),
            ("src/modules/inventory/Inventory.cpp", 2, 'result.set("ok", false);'),
        ]
        self.assertEqual(check.evaluate(hits, allowlist), [])

    def test_unallowlisted_hit_fails(self):
        allowlist = {
            "schema_version": 1,
            "baselineCount": 0,
            "policy": {"allowNetGrowth": False, "metadataFileIsReadOnly": True},
            "exempt": [],
            "default": {
                "owner": "engine-scripting",
                "issue": "TEST",
                "reason": "fixture",
                "expiry": "2099-01-01",
            },
            "files": {},
        }
        hits = [("src/modules/animation/Foo.cpp", 10, 'result.set("ok", false);')]
        errors = check.evaluate(hits, allowlist)
        self.assertTrue(any("unallowlisted" in error for error in errors))

    def test_net_growth_fails(self):
        allowlist = {
            "schema_version": 1,
            "baselineCount": 1,
            "policy": {"allowNetGrowth": False, "metadataFileIsReadOnly": True},
            "exempt": [],
            "default": {
                "owner": "engine-scripting",
                "issue": "TEST",
                "reason": "fixture",
                "expiry": "2099-01-01",
            },
            "files": {"src/modules/inventory/Inventory.cpp": {}},
        }
        hits = [
            ("src/modules/inventory/Inventory.cpp", 1, 'result.set("ok", true);'),
            ("src/modules/inventory/Inventory.cpp", 2, 'result.set("ok", false);'),
        ]
        errors = check.evaluate(hits, allowlist)
        self.assertTrue(any("grew from baseline" in error for error in errors))

    def test_ok_set_regex(self):
        self.assertTrue(check.OK_SET.search('result.set("ok", false);'))
        self.assertTrue(check.OK_SET.search("table.set('ok', published.ok());"))
        self.assertFalse(check.OK_SET.search('result.set("message", "x");'))


if __name__ == "__main__":
    unittest.main()

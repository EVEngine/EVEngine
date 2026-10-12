"""Unit tests for whitebox-playtest (dry-run; no engine required)."""

from __future__ import annotations

import json
import sys
import unittest
from pathlib import Path

PKG = Path(__file__).resolve().parents[1]
if str(PKG) not in sys.path:
    sys.path.insert(0, str(PKG))

from whitebox_playtest.recipe import (  # noqa: E402
    default_recipe,
    eval_assert,
    load_recipe,
    validate_recipe,
)
from whitebox_playtest.runner import DryRunEngine, run_recipe  # noqa: E402
from whitebox_playtest.server import WhiteboxMcpServer  # noqa: E402


class RecipeTests(unittest.TestCase):
    def test_default_and_file_recipe(self):
        recipe = default_recipe()
        validate_recipe(recipe)
        path = PKG / "recipes" / "corridor.json"
        loaded = load_recipe(path)
        self.assertEqual(loaded["schemaId"], "eve.whitebox.playtest-recipe")
        self.assertGreaterEqual(len(loaded["scene"]), 3)

    def test_assert_ops(self):
        report = {"ok": True, "scene": {"applied": 5}, "info": {"count": 3}}
        self.assertTrue(eval_assert(report, {"path": "scene.applied", "op": "gte", "value": 4})["ok"])
        self.assertFalse(eval_assert(report, {"path": "info.count", "op": "lt", "value": 1})["ok"])
        self.assertFalse(eval_assert(report, {"path": "missing", "op": "eq", "value": 1})["ok"])


class RunnerTests(unittest.TestCase):
    def test_dry_run_batch_and_asserts(self):
        report = run_recipe(default_recipe(), dry_run=True)
        self.assertTrue(report["passed"], report)
        self.assertGreaterEqual(report["scene"]["applied"], 1)
        self.assertGreaterEqual(report["info"]["count"], 1)
        self.assertTrue(all(a["ok"] for a in report["asserts"]))

    def test_dry_run_engine_adjust(self):
        engine = DryRunEngine()
        engine.batch(
            [
                {"action": "add_object", "target": "a", "params": {"id": "a", "kind": "box"}},
                {"action": "move", "target": "a", "params": {"pos": [1, 2, 3]}},
            ]
        )
        self.assertEqual(engine.props["a"]["pos"], [1, 2, 3])
        bad = engine.batch([{"action": "move", "target": "missing", "params": {"pos": [0, 0, 0]}}])
        self.assertFalse(bad["ok"])


class McpServerTests(unittest.TestCase):
    def test_stdio_tools_dry_run(self):
        server = WhiteboxMcpServer(dry_run=True)
        init = server.handle(
            {
                "jsonrpc": "2.0",
                "id": 1,
                "method": "initialize",
                "params": {"protocolVersion": "2025-06-18", "clientInfo": {"name": "t"}},
            }
        )
        self.assertEqual(init["result"]["serverInfo"]["name"], "eve-whitebox-playtest")
        listed = server.handle({"jsonrpc": "2.0", "id": 2, "method": "tools/list"})
        names = {t["name"] for t in listed["result"]["tools"]}
        self.assertEqual(names, {"wb_run", "wb_apply_scene", "wb_playtest", "wb_default_recipe"})
        run = server.handle(
            {
                "jsonrpc": "2.0",
                "id": 3,
                "method": "tools/call",
                "params": {"name": "wb_run", "arguments": {"dryRun": True}},
            }
        )
        body = json.loads(run["result"]["content"][0]["text"])
        self.assertTrue(body["passed"])


if __name__ == "__main__":
    unittest.main()

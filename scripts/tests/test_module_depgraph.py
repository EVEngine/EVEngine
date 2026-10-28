"""Unit tests for scripts/module_depgraph.py."""

from collections import defaultdict
import unittest
import sys
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))

import module_depgraph as md


class DeclaredLayersTest(unittest.TestCase):
    def test_combined_checks_scan_once_and_report_on_failure(self):
        for graph_status, declared_status in [(True, 0), (False, 1), (False, 0)]:
            with self.subTest(graph_failure=graph_status, declared_status=declared_status):
                edges = defaultdict(dict, {"base": {"top": {"top/Header.h"}}} if graph_status else {})
                header_edges = defaultdict(set)
                with mock.patch.object(sys, "argv", ["module_depgraph", "--check", "--check-layers", "--report"]), \
                     mock.patch.object(md, "scan", return_value=(["base", "top"], edges, header_edges)) as scan, \
                     mock.patch.object(md, "layers", return_value={"base": 0, "top": 1}), \
                     mock.patch.object(md, "check_declared_layers", return_value=declared_status) as declared, \
                     mock.patch("builtins.print") as output:
                    self.assertEqual(md.main(), int(graph_status or declared_status))
                    scan.assert_called_once()
                    declared.assert_called_once_with(edges, header_edges)
                    self.assertTrue(any(call.args[0] == "=== layers (back-edges removed) ==="
                                        for call in output.call_args_list))

    def test_manifest_parses_declared_layers(self):
        layers = md.declared_layers()
        self.assertEqual(layers.get("graphics"), 3)
        self.assertEqual(layers.get("procgen"), 5)
        self.assertEqual(layers.get("sceneloader"), 6)
        self.assertEqual(layers.get("common"), -1)  # CORE modules are declared too


class CheckDeclaredLayersTest(unittest.TestCase):
    def setUp(self):
        self.layers = {
            "base": 0,
            "mid": 3,
            "top": 6,
        }
        self.patcher = mock.patch.object(md, "declared_layers", return_value=self.layers)
        self.patcher.start()

    def tearDown(self):
        self.patcher.stop()

    def run_check(self, edges):
        edge_dict = {}
        for src, dst in edges:
            edge_dict.setdefault(src, set()).add(dst)
        return md.check_declared_layers(edge_dict, header_edges=set())

    def test_upward_edge_is_reported(self):
        # base (L0) including mid (L3) must fail.
        self.assertEqual(self.run_check({("base", "mid")}), 1)

    def test_deep_upward_edge_is_reported(self):
        # base (L0) including top (L6) must fail (the editor -> procgen case).
        self.assertEqual(self.run_check({("base", "top")}), 1)

    def test_downward_and_same_layer_edges_pass(self):
        edges = {("top", "base"), ("top", "mid"), ("mid", "mid")}
        self.assertEqual(self.run_check(edges), 0)

    def test_known_back_edge_is_whitelisted(self):
        edges = {("base", "mid")}
        with mock.patch.object(md, "KNOWN_BACK_EDGES", {("base", "mid")}):
            self.assertEqual(self.run_check(edges), 0)

    def test_stale_known_edge_is_noted_but_passes(self):
        # A known edge that no longer violates the declared layers is a note,
        # not a failure.
        edges = {("top", "base")}
        with mock.patch.object(md, "KNOWN_BACK_EDGES", {("top", "base")}):
            self.assertEqual(self.run_check(edges), 0)


if __name__ == "__main__":
    unittest.main()

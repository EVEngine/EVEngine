"""Unit tests for scripts/analyze_header_includes.py."""

from __future__ import annotations

import importlib.util
import json
import sys
import unittest
from pathlib import Path

SCRIPT = Path(__file__).resolve().parents[1] / "analyze_header_includes.py"
sys.path.insert(0, str(SCRIPT.parent))
SPEC = importlib.util.spec_from_file_location("analyze_header_includes", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
sys.modules[SPEC.name] = MODULE
SPEC.loader.exec_module(MODULE)


class AnalyzeHeaderIncludesTest(unittest.TestCase):
    def _repo(self, tmp_path: Path) -> Path:
        repo = tmp_path / "repo"
        engine = repo / "src" / "engine"
        modules = repo / "src" / "modules"
        (engine / "common").mkdir(parents=True)
        (modules / "graphics").mkdir(parents=True)
        (repo / "test").mkdir(parents=True)

        (engine / "common" / "Types.h").write_text(
            "#pragma once\nstruct Vec2 { float x, y; };\n",
            encoding="utf-8",
        )
        (engine / "common" / "Heavy.h").write_text(
            '#pragma once\n#include "common/Types.h"\n'
            "#include <vector>\n"
            "struct Heavy { std::vector<Vec2> points; };\n",
            encoding="utf-8",
        )
        (modules / "graphics" / "Graphics.h").write_text(
            '#pragma once\n#include "common/Heavy.h"\nclass Graphics {};\n',
            encoding="utf-8",
        )
        (modules / "graphics" / "Graphics.cpp").write_text(
            '#include "graphics/Graphics.h"\n',
            encoding="utf-8",
        )
        (engine / "main.cpp").write_text(
            '#include "graphics/Graphics.h"\n',
            encoding="utf-8",
        )
        (repo / "test" / "graphics_test.cpp").write_text(
            '#include "common/Heavy.h"\n',
            encoding="utf-8",
        )
        return repo

    def test_reverse_dependents_and_priority(self):
        import tempfile

        with tempfile.TemporaryDirectory() as tmp:
            repo = self._repo(Path(tmp))
            graph = MODULE.build_graph(repo)
            stats = {s.include_key: s for s in MODULE.all_stats(graph)}

            heavy = stats["common/Heavy.h"]
            self.assertEqual(heavy.header_users, 1)  # Graphics.h
            self.assertEqual(heavy.cpp_users, 1)  # test/graphics_test.cpp
            self.assertEqual(heavy.outgoing_project, 1)  # Types.h
            self.assertEqual(heavy.outgoing_system, 1)  # vector
            self.assertEqual(heavy.priority, 1)
            # Heavy.h (5 lines) + Types.h (3 lines) — trailing newlines counted.
            self.assertEqual(heavy.expand_headers, 2)
            self.assertEqual(heavy.expand_lines, 8)
            self.assertEqual(heavy.include_lines, 3)

            graphics = stats["graphics/Graphics.h"]
            self.assertEqual(graphics.header_users, 0)
            self.assertEqual(graphics.cpp_users, 2)  # Graphics.cpp + main.cpp
            self.assertEqual(graphics.outgoing_project, 1)
            self.assertEqual(graphics.priority, 0)
            # Graphics.h (4) + Heavy.h (5) + Types.h (3)
            self.assertEqual(graphics.expand_headers, 3)
            self.assertEqual(graphics.expand_lines, 12)
            self.assertEqual(graphics.include_lines, 8)
            self.assertEqual(graphics.impact, 2 * 12)

            hdrs, cpps = MODULE.split_dependents(graph, "common/Heavy.h")
            self.assertEqual(hdrs, ["src/modules/graphics/Graphics.h"])
            self.assertEqual(cpps, ["test/graphics_test.cpp"])

    def test_sort_by_expand_lines(self):
        import tempfile

        with tempfile.TemporaryDirectory() as tmp:
            repo = self._repo(Path(tmp))
            graph = MODULE.build_graph(repo)
            stats = MODULE.all_stats(graph, sort="expand")
            self.assertEqual(stats[0].include_key, "graphics/Graphics.h")
            self.assertGreaterEqual(stats[0].expand_lines, stats[1].expand_lines)

    def test_header_detail_and_json_cli(self):
        import tempfile

        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            repo = self._repo(root)
            out = root / "report.json"
            status = MODULE.main(
                [
                    "--source-dir",
                    str(repo),
                    "--header",
                    "common/Heavy.h",
                    "--json",
                    str(out),
                    "--limit",
                    "10",
                ]
            )
            self.assertEqual(status, 0)
            payload = json.loads(out.read_text(encoding="utf-8"))
            self.assertGreaterEqual(payload["header_count"], 3)
            keys = {entry["include_key"] for entry in payload["headers"]}
            self.assertIn("common/Heavy.h", keys)

    def test_no_test_skips_test_includers(self):
        import tempfile

        with tempfile.TemporaryDirectory() as tmp:
            repo = self._repo(Path(tmp))
            graph = MODULE.build_graph(repo, scan_roots=("src",))
            heavy = MODULE.header_stat(graph, "common/Heavy.h")
            assert heavy is not None
            self.assertEqual(heavy.cpp_users, 0)
            self.assertEqual(heavy.header_users, 1)


if __name__ == "__main__":
    unittest.main()

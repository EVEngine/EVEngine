"""Unit tests for scripts/analyze_header_includes.py."""

from __future__ import annotations

import importlib.util
import json
import sys
import tempfile
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

    def _sysroot(self, tmp_path: Path) -> Path:
        """Tiny fake libstdc++-style root so system expansion is deterministic."""
        root = tmp_path / "sysroot"
        bits = root / "bits"
        bits.mkdir(parents=True)
        (root / "vector").write_text(
            "#pragma once\n#include <bits/stl_vector.h>\n",
            encoding="utf-8",
        )
        (bits / "stl_vector.h").write_text(
            "#pragma once\n// fake stl vector body\n" + ("x\n" * 10),
            encoding="utf-8",
        )
        return root

    def test_reverse_dependents_and_priority(self):
        with tempfile.TemporaryDirectory() as tmp:
            repo = self._repo(Path(tmp))
            # Project-only expansion keeps line counts stable across hosts.
            graph = MODULE.build_graph(repo, system_roots=[])
            stats = {s.include_key: s for s in MODULE.all_stats(graph)}

            heavy = stats["common/Heavy.h"]
            self.assertEqual(heavy.header_users, 1)
            self.assertEqual(heavy.cpp_users, 1)
            self.assertEqual(heavy.outgoing_project, 1)
            self.assertEqual(heavy.outgoing_system, 0)  # <vector> unresolved w/o roots
            self.assertEqual(heavy.unresolved, 1)
            self.assertEqual(heavy.priority, 1)
            self.assertEqual(heavy.expand_headers, 2)
            self.assertEqual(heavy.expand_lines, 8)
            self.assertEqual(heavy.include_lines, 3)
            self.assertEqual(heavy.expand_system_lines, 0)

            graphics = stats["graphics/Graphics.h"]
            self.assertEqual(graphics.header_users, 0)
            self.assertEqual(graphics.cpp_users, 2)
            self.assertEqual(graphics.outgoing_project, 1)
            self.assertEqual(graphics.priority, 0)
            self.assertEqual(graphics.expand_headers, 3)
            self.assertEqual(graphics.expand_lines, 12)
            self.assertEqual(graphics.include_lines, 8)
            self.assertEqual(graphics.impact, 2 * 12)

            hdrs, cpps = MODULE.split_dependents(graph, "common/Heavy.h")
            self.assertEqual(hdrs, ["src/modules/graphics/Graphics.h"])
            self.assertEqual(cpps, ["test/graphics_test.cpp"])

    def test_system_headers_expand_through_include_path(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            repo = self._repo(root)
            sysroot = self._sysroot(root)
            graph = MODULE.build_graph(repo, system_roots=[sysroot.resolve()])
            heavy = MODULE.header_stat(graph, "common/Heavy.h")
            assert heavy is not None
            self.assertEqual(heavy.outgoing_system, 1)
            self.assertEqual(heavy.unresolved, 0)
            vector_lines = (sysroot / "vector").read_text(encoding="utf-8").count("\n") + 1
            stl_lines = (sysroot / "bits" / "stl_vector.h").read_text(encoding="utf-8").count("\n") + 1
            self.assertEqual(heavy.expand_project_lines, 8)
            self.assertEqual(heavy.expand_system_lines, vector_lines + stl_lines)
            self.assertEqual(heavy.expand_lines, 8 + vector_lines + stl_lines)
            self.assertEqual(heavy.expand_system_headers, 2)

            graphics = MODULE.header_stat(graph, "graphics/Graphics.h")
            assert graphics is not None
            self.assertEqual(graphics.expand_system_lines, vector_lines + stl_lines)
            self.assertGreater(graphics.expand_lines, graphics.expand_project_lines)

    def test_sort_by_expand_lines(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            repo = self._repo(root)
            sysroot = self._sysroot(root)
            graph = MODULE.build_graph(repo, system_roots=[sysroot.resolve()])
            stats = MODULE.all_stats(graph, sort="expand")
            self.assertEqual(stats[0].include_key, "graphics/Graphics.h")
            self.assertGreaterEqual(stats[0].expand_lines, stats[1].expand_lines)

    def test_header_detail_and_json_cli(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            repo = self._repo(root)
            sysroot = self._sysroot(root)
            out = root / "report.json"
            status = MODULE.main(
                [
                    "--source-dir",
                    str(repo),
                    "--header",
                    "common/Heavy.h",
                    "--include-path",
                    str(sysroot),
                    "--json",
                    str(out),
                    "--limit",
                    "10",
                ]
            )
            self.assertEqual(status, 0)
            payload = json.loads(out.read_text(encoding="utf-8"))
            self.assertGreaterEqual(payload["header_count"], 3)
            self.assertTrue(any(str(sysroot.resolve()) == r or str(sysroot) in r for r in payload["system_roots"]))
            entry = next(h for h in payload["headers"] if h["include_key"] == "common/Heavy.h")
            self.assertGreater(entry["expand_system_lines"], 0)
            self.assertEqual(entry["includes"]["system"][0]["name"], "vector")

    def test_no_test_skips_test_includers(self):
        with tempfile.TemporaryDirectory() as tmp:
            repo = self._repo(Path(tmp))
            graph = MODULE.build_graph(repo, scan_roots=("src",), system_roots=[])
            heavy = MODULE.header_stat(graph, "common/Heavy.h")
            assert heavy is not None
            self.assertEqual(heavy.cpp_users, 0)
            self.assertEqual(heavy.header_users, 1)

    def test_compiler_system_paths_discoverable(self):
        roots = MODULE.compiler_system_include_paths("c++")
        # Host may lack a compiler in odd environments; then the list is empty.
        if roots:
            self.assertTrue(any((r / "vector").is_file() or (r / "stdio.h").is_file() for r in roots))


if __name__ == "__main__":
    unittest.main()

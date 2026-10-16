"""Dependency selection must cover implementation changes, not just includes."""

import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

SCRIPTS = Path(__file__).resolve().parents[1]
if str(SCRIPTS) not in sys.path:
    sys.path.insert(0, str(SCRIPTS))
import test_impact as impact  # noqa: E402


class TestImpactTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.build = self.root / "build"
        self.reply = self.build / ".cmake/api/v1/reply"
        self.reply.mkdir(parents=True)
        for name, text in {
            "src/a.cpp": '#include "a.h"\n',
            "src/a.h": "// contract\n",
            "src/b.cpp": "// unrelated implementation\n",
            "test/first.cpp": '#include "local.h"\n',
            "test/local.h": "// local helper\n",
            "test/second.cpp": "// different test\n",
        }.items():
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text, encoding="utf-8")
        self.targets = {
            "library": {"name": "library", "type": "STATIC_LIBRARY", "sources": [
                {"path": "src/a.cpp", "compileGroupIndex": 0}],
                "compileGroups": [{"includes": [{"path": str(self.root / "src")}]}]},
            "runner": {"name": "runner", "type": "EXECUTABLE", "artifacts": [{"path": "runner.exe"}],
                "dependencies": [{"id": "library"}], "sources": [
                    {"path": "test/first.cpp", "compileGroupIndex": 0},
                    {"path": "test/second.cpp", "compileGroupIndex": 0}],
                "compileGroups": [{"includes": []}]},
            "unrelated": {"name": "unrelated", "type": "STATIC_LIBRARY", "sources": [
                {"path": "src/b.cpp", "compileGroupIndex": 0}], "compileGroups": [{"includes": []}]},
        }
        self.tests = [self.registration("first.case", "first.cpp", "unit"),
                      self.registration("second.case", "second.cpp", "integration")]
        self.write_model()

    def registration(self, name, source, level):
        return {"name": name, "command": [str(self.build / "runner.exe"), "--case=" + name],
                "properties": [{"name": "LABELS", "value": ["source:" + source, level, "no-render"]}]}

    def write_model(self):
        for name, target in self.targets.items():
            (self.reply / f"{name}.json").write_text(json.dumps(target), encoding="utf-8")
        model = {"paths": {"source": str(self.root), "build": str(self.build)},
                 "configurations": [{"name": "Debug", "targets": [
                     {"id": name, "jsonFile": f"{name}.json"} for name in self.targets]}]}
        (self.reply / "model.json").write_text(json.dumps(model), encoding="utf-8")
        index = {"objects": [{"kind": "codemodel", "version": {"major": 2}, "jsonFile": "model.json"}],
                 "cmake": {"generator": {"name": "Ninja"}}}
        (self.reply / "index-test.json").write_text(json.dumps(index), encoding="utf-8")

    def index(self, **kwargs):
        with patch.object(impact, "invoke", return_value=json.dumps({"tests": self.tests})), \
             patch.object(impact, "compiler_dependencies", return_value={}):
            return impact.build_index(self.build, self.root, "Debug", **kwargs)

    def test_cpp_implementation_is_link_dependency_without_test_include(self):
        index = self.index(required=["runner"])
        self.assertEqual(impact.select(index, ["src/a.cpp"])["tests"], ["first.case", "second.case"])
        self.assertEqual(index["reverse"]["src/a.cpp"]["evidence"], ["target/link"])

    def test_common_consumers_are_shared_instead_of_repeated_per_file(self):
        index = self.index()
        self.assertEqual(index["reverse"]["src/a.cpp"]["test_set"], index["reverse"]["src/a.h"]["test_set"])
        self.assertNotIn("tests", index["reverse"]["src/a.cpp"])

    def test_changed_header_graph_refreshes_between_index_snapshots(self):
        self.index()
        (self.root / "test/added.h").write_text("// new private dependency\n", encoding="utf-8")
        path = self.root / "test/first.cpp"
        path.write_text(path.read_text(encoding="utf-8") + '#include "added.h"\n', encoding="utf-8")
        self.assertEqual(impact.select(self.index(), ["test/added.h"])["tests"], ["first.case"])

    def test_test_source_and_its_private_header_select_only_own_cases(self):
        index = self.index()
        for path in ["test/first.cpp", "test/local.h"]:
            self.assertEqual(impact.select(index, [path])["tests"], ["first.case"])

    def test_engine_header_reaches_linked_consumers_and_level_intersects(self):
        index = self.index()
        self.assertEqual(impact.select(index, ["src/a.h"], "unit")["tests"], ["first.case"])
        self.assertNotIn("src/b.cpp", index["reverse"])

    def test_unknown_deleted_or_dynamic_asset_change_selects_all(self):
        index = self.index()
        for path in ["src/removed.cpp", "cmake/modules.cmake", "test/game.nut"]:
            self.assertEqual(impact.select(index, [path])["tests"], ["first.case", "second.case"])
        (self.root / "src/a.h").unlink()
        self.assertEqual(impact.select(index, ["src/a.h"])["tests"], ["first.case", "second.case"])

    def test_unbuilt_target_is_an_error_even_before_first_index(self):
        target_dir = self.build / "test-impact"
        target_dir.mkdir()
        (target_dir / "targets-Debug.txt").write_text("runner\nmissing\n", encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "undiscovered.*missing"):
            self.index()

    def test_compiler_macro_include_is_incorporated(self):
        header = self.root / "src/macro-selected.h"
        header.write_text("// macro include\n", encoding="utf-8")
        with patch.object(impact, "invoke", return_value=json.dumps({"tests": self.tests})), \
             patch.object(impact, "compiler_dependencies", return_value={self.root / "src/a.cpp": {header}}):
            index = impact.build_index(self.build, self.root, "Debug")
        self.assertEqual(impact.select(index, ["src/macro-selected.h"])["tests"], ["first.case", "second.case"])

    def test_opt_in_bundles_benchmarks_and_disabled_tests_are_excluded(self):
        for label in ["bundle", "benchmark"]:
            test = self.registration(label, "first.cpp", "unit")
            test["properties"][0]["value"].append(label)
            self.tests.append(test)
        disabled = self.registration("disabled", "first.cpp", "unit")
        disabled["properties"].append({"name": "DISABLED", "value": True})
        self.tests.append(disabled)
        self.assertEqual(sorted(self.index()["tests"]), ["first.case", "second.case"])

    def test_script_tests_without_target_are_always_conservative(self):
        self.tests.append({"name": "quality", "command": ["python", "check.py"], "properties": []})
        index = self.index()
        self.assertEqual(impact.select(index, ["test/first.cpp"])["tests"], ["first.case", "quality"])
        self.assertEqual(impact.select(index, [])["tests"], [])

    def test_target_cycle_terminates_and_missing_edge_is_an_error(self):
        targets = {"a": {"dependencies": [{"id": "b"}]}, "b": {"dependencies": [{"id": "a"}]}}
        self.assertEqual(impact.closure("a", targets), {"a", "b"})
        with self.assertRaisesRegex(ValueError, "missing codemodel"):
            impact.closure("missing", targets)

    def test_unchanged_index_does_not_rewrite(self):
        self.index()
        path = self.build / "test-impact/index.json"
        previous = path.stat().st_mtime_ns
        self.index()
        self.assertEqual(path.stat().st_mtime_ns, previous)

    def test_configuration_and_schema_mismatches_fail(self):
        with self.assertRaisesRegex(ValueError, "configuration"):
            impact.codemodel(self.build, "Release")
        with self.assertRaisesRegex(ValueError, "schema"):
            impact.select({"schema": "different"}, ["src/a.cpp"])

    def test_run_regex_escapes_names_and_batches_windows_length(self):
        selection = {"tests": ["name.with[regex]+(x)"] + ["x" * 100 + str(i) for i in range(100)]}
        with patch.object(impact.subprocess, "run") as run:
            run.return_value.returncode = 0
            self.assertEqual(impact.run_selection(self.build, selection, "Debug"), 0)
        self.assertGreater(len(run.call_args_list), 1)
        arguments = run.call_args_list[0].args[0]
        pattern = arguments[arguments.index("-R") + 1]
        self.assertIn(r"name\.with\[regex\]\+\(x\)", pattern)

    def test_missing_domain_fails_instead_of_claiming_no_affected_tests(self):
        with self.assertRaisesRegex(ValueError, "no registered test domain"):
            impact.select(self.index(), ["src/a.cpp"], domain="missing")

    def test_header_changes_without_compiler_records_are_conservative(self):
        index = self.index()
        index["configuration"]["generator"] = "Unix Makefiles"
        self.assertEqual(impact.select(index, ["test/local.h"])["tests"], ["first.case", "second.case"])


if __name__ == "__main__":
    unittest.main()

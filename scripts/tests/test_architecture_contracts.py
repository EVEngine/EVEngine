"""Fixture tests for the code-first architecture shape gate."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))

import check_architecture_contracts as contracts  # noqa: E402


class ArchitectureContractTests(unittest.TestCase):
    def test_valid_api_fixture_has_no_shape_findings(self):
        path = ROOT / "scripts/tests/fixtures_architecture_contracts/valid_api.h"
        lines = [
            contracts.SourceLine(path.relative_to(ROOT).as_posix(), number, text)
            for number, text in enumerate(path.read_text(encoding="utf-8").splitlines(), 1)
        ]
        self.assertEqual([], contracts.lint_api_shapes(lines))

    def test_invalid_api_fixture_reports_all_high_signal_shapes(self):
        path = ROOT / "scripts/tests/fixtures_architecture_contracts/invalid_api.h"
        lines = [
            contracts.SourceLine(path.relative_to(ROOT).as_posix(), number, text)
            for number, text in enumerate(path.read_text(encoding="utf-8").splitlines(), 1)
        ]
        findings = contracts.lint_api_shapes(lines)
        codes = {finding.code for finding in findings}
        self.assertIn("ambiguous-operation-bool", codes)
        self.assertIn("last-error-channel", codes)
        self.assertIn("undocumented-raw-pointer-api", codes)

    def test_multiply_assignment_is_not_a_pointer_api(self):
        for text in ("n *= static_cast<size_t>(d);", "n *= count(shape);"):
            with self.subTest(text=text):
                lines = [contracts.SourceLine("fixture.h", 1, text)]
                self.assertEqual([], contracts.lint_api_shapes(lines))

    def test_named_pointer_apis_still_require_lifetime_contracts(self):
        for text in ("Widget* get();", "Widget* value = makeWidget();"):
            with self.subTest(text=text):
                lines = [contracts.SourceLine("fixture.h", 1, text)]
                codes = {finding.code for finding in contracts.lint_api_shapes(lines)}
                self.assertIn("undocumented-raw-pointer-api", codes)

    def test_default_path_diffs_against_head(self):
        captured: dict[str, str | None] = {}
        original_changed = contracts._changed_lines

        def fake_changed(base):
            captured["changed"] = base
            return []

        contracts._changed_lines = fake_changed
        try:
            self.assertEqual(0, contracts.main([]))
        finally:
            contracts._changed_lines = original_changed

        self.assertEqual("HEAD", captured["changed"])

    def test_owned_container_members_are_not_returning_apis(self):
        declarations = [
            "std::vector<std::uint8_t> albedo;",
            "std::vector<std::uint8_t> normal{};",
            "std::vector<std::uint8_t> params = {};",
            "std::map<int, std::vector<int>> values;",
            "std::vector<std::string> collectIds();",
            "std::map<int, std::vector<int>> values();",
        ]
        lines = [
            contracts.SourceLine("fixture.h", number, text)
            for number, text in enumerate(declarations, 1)
        ]
        findings = contracts.lint_runtime_guards(lines)
        self.assertEqual(
            [5, 6], [item.line for item in findings if item.code == "missing-cost-annotation"]
        )

    def test_valid_module_interface_fixture_has_cost(self):
        path = ROOT / "scripts/tests/fixtures_architecture_contracts/valid_module_interface.h"
        lines = [
            contracts.SourceLine(path.relative_to(ROOT).as_posix(), number, text)
            for number, text in enumerate(path.read_text(encoding="utf-8").splitlines(), 1)
        ]
        codes = {finding.code for finding in contracts.lint_runtime_guards(lines)}
        self.assertNotIn("missing-cost-annotation", codes)

    def test_container_data_member_is_not_an_owned_return_api(self):
        lines = [contracts.SourceLine("fixture.h", 1, "std::vector<ModuleConnector> connectors;")]
        self.assertEqual([], contracts.lint_runtime_guards(lines))

    def test_owned_container_method_still_requires_cost(self):
        lines = [contracts.SourceLine("fixture.h", 1, "std::vector<ModuleConnector> connectors();")]
        codes = {finding.code for finding in contracts.lint_runtime_guards(lines)}
        self.assertIn("missing-cost-annotation", codes)

    def test_runtime_lookup_and_hot_path_and_cost_without_catalogue(self):
        # Keyboard EventSink declares a hot source; runtime paths ban getModInst.
        lines = [
            contracts.SourceLine(
                "src/modules/keyboard/sdl/EventSink.cpp",
                12,
                "auto* q = eve::cap::query<ISceneQuery>();",
            ),
            contracts.SourceLine(
                "src/modules/fixture/Runtime.cpp",
                13,
                "auto* mod = getModInst(eve::gfx, Graphics);",
            ),
            contracts.SourceLine(
                "src/modules/fixture/Api.h",
                14,
                "std::vector<std::string> collectIds();",
            ),
        ]
        findings = contracts.lint_runtime_guards(lines)
        codes = {finding.code for finding in findings}
        self.assertIn("hot-path-forbidden-primitive", codes)
        self.assertIn("runtime-convenience-lookup", codes)
        self.assertIn("missing-cost-annotation", codes)

    def test_source_and_directory_declarations_cover_hot_paths(self):
        self.assertTrue(contracts._is_hot_path("src/modules/keyboard/sdl/EventSink.cpp"))
        self.assertTrue(contracts._is_hot_path("src/modules/physics/forces/FutureForce.cpp"))
        self.assertFalse(contracts._is_hot_path("src/modules/physics/forces/FutureForce.h"))
        self.assertFalse(contracts._is_hot_path("src/modules/fixture/Runtime.cpp"))

    def test_local_hot_path_marker_and_false_literal(self):
        import tempfile
        from unittest.mock import patch
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "New.cpp"
            with patch.object(contracts, "ROOT", root):
                source.write_text('// eve-hot-path\nvoid tick() {}', encoding="utf-8")
                self.assertTrue(contracts._is_hot_path("New.cpp"))
                source.write_text('const char* s = R"x(\n// eve-hot-path\n)x";', encoding="utf-8")
                self.assertFalse(contracts._is_hot_path("New.cpp"))
                self.assertFalse(contracts._is_hot_path("../outside.cpp"))


if __name__ == "__main__":
    unittest.main()

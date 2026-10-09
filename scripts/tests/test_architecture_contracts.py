"""Fixture tests for the executable top-level architecture gates."""

from __future__ import annotations

import copy
import sys
import unittest
from datetime import date
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))

import check_architecture_contracts as contracts  # noqa: E402


class ArchitectureContractTests(unittest.TestCase):
    def test_repository_catalogue_covers_all_rules(self):
        metadata = contracts.load_json(ROOT / "scripts" / "architecture_contracts.json")
        self.assertEqual([], contracts.validate_catalogue(metadata, today=date(2026, 8, 26)))
        self.assertIn("module-interface", contracts.RULES)

    def test_module_interface_requires_typed_faces(self):
        metadata = contracts.load_json(ROOT / "scripts" / "architecture_contracts.json")
        reduced = copy.deepcopy(metadata)
        entry = next(item for item in reduced["entries"] if item["rule"] == "module-interface")
        entry["binds"] = "undocumented"
        del entry["trim"]
        errors = contracts.validate_catalogue(reduced, today=date(2026, 8, 26))
        self.assertTrue(any("binds must be an array" in error for error in errors))
        self.assertTrue(any("missing trim" in error for error in errors))

    def test_missing_required_contract_field_is_rejected(self):
        metadata = contracts.load_json(ROOT / "scripts" / "architecture_contracts.json")
        reduced = copy.deepcopy(metadata)
        entry = next(item for item in reduced["entries"] if item["rule"] == "link")
        del entry["stale"]
        errors = contracts.validate_catalogue(reduced, today=date(2026, 8, 26))
        self.assertTrue(any("stale" in error for error in errors))

    def test_noncanonical_catalogue_order_is_rejected(self):
        metadata = contracts.load_json(ROOT / "scripts" / "architecture_contracts.json")
        reordered = copy.deepcopy(metadata)
        reordered["entries"].reverse()
        errors = contracts.validate_catalogue(reordered, today=date(2026, 8, 26))
        self.assertTrue(any("canonical rule/id order" in error for error in errors))

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

    def test_new_link_and_system_need_catalogue_coverage(self):
        metadata = {
            "entries": [
                {
                    "rule": "link",
                    "scope": "src/modules/test/Link.h",
                }
            ]
        }
        lines = [
            contracts.SourceLine("src/modules/test/OtherLink.h", 3, "struct OtherLink {};"),
            contracts.SourceLine("src/modules/test/NewSystem.h", 4, "class NewSystem {};"),
        ]
        findings = contracts.lint_contract_coverage(lines, metadata)
        self.assertEqual(2, len(findings))
        self.assertTrue(all(finding.code == "missing-contract-entry" for finding in findings))

    def test_catalogue_entry_covers_a_known_link(self):
        metadata = {
            "entries": [
                {
                    "rule": "link",
                    "scope": "src/modules/test/Link.h",
                }
            ]
        }
        lines = [contracts.SourceLine("src/modules/test/Link.h", 3, "struct TestLink {};" )]
        self.assertEqual([], contracts.lint_contract_coverage(lines, metadata))

    def test_annotated_established_system_is_not_new_surface(self):
        # `class EVENGINE_API_WORLD TransformSystem` is an established type whose
        # line only gained the export macro, so it must not demand a catalogue
        # entry; a type that does not exist in the base revision still must.
        metadata = {"entries": []}
        lines = [
            contracts.SourceLine(
                "src/modules/scene/TransformSystem.h", 13, "class EVENGINE_API_WORLD TransformSystem {"
            ),
            contracts.SourceLine(
                "src/modules/scene/BrandNewSystem.h", 13, "class EVENGINE_API_WORLD BrandNewSystem {"
            ),
        ]
        original = contracts._base_source
        contracts._base_source = lambda base, path: (
            "class TransformSystem {\n" if path.endswith("TransformSystem.h") else ""
        )
        try:
            findings = contracts.lint_contract_coverage(lines, metadata, "base")
        finally:
            contracts._base_source = original

        self.assertEqual(["ecs-system"], [finding.rule for finding in findings])
        self.assertEqual("src/modules/scene/BrandNewSystem.h", findings[0].path)

    def test_name_mentioned_only_in_a_comment_is_still_new_surface(self):
        # The established check must look for a declaration: a name that the
        # baseline only mentions in a comment, a string or an unrelated member is
        # not proof that the surface already existed.
        metadata = {"entries": []}
        lines = [
            contracts.SourceLine(
                "src/modules/scene/CommentOnlySystem.h", 13, "class EVENGINE_API_WORLD CommentOnlySystem {"
            ),
        ]
        original = contracts._base_source
        contracts._base_source = lambda base, path: (
            "// CommentOnlySystem is planned; see the design note.\n"
            "void touch(CommentOnlySystemTag tag);\n"
        )
        try:
            findings = contracts.lint_contract_coverage(lines, metadata, "base")
        finally:
            contracts._base_source = original

        self.assertEqual(["ecs-system"], [finding.rule for finding in findings])

    def test_default_path_uses_head_as_the_contract_baseline(self):
        # Without --base the changed lines are diffed against HEAD, so the
        # declaration baseline must be HEAD too; leaving it unset disabled the
        # established-declaration suppression and flagged export-only edits.
        captured: dict[str, str | None] = {}
        original_changed = contracts._changed_lines
        original_lint = contracts.lint_contract_coverage
        original_module = contracts.lint_module_interface

        def fake_changed(base):
            captured["changed"] = base
            return []

        def fake_lint(lines, metadata, base=None):
            captured["lint"] = base
            return []

        def fake_module(lines, metadata):
            return []

        contracts._changed_lines = fake_changed
        contracts.lint_contract_coverage = fake_lint
        contracts.lint_module_interface = fake_module
        try:
            contracts.main([])
        finally:
            contracts._changed_lines = original_changed
            contracts.lint_contract_coverage = original_lint
            contracts.lint_module_interface = original_module

        self.assertEqual("HEAD", captured["changed"])
        self.assertEqual("HEAD", captured["lint"])

    def test_valid_module_interface_fixture_has_cost(self):
        path = ROOT / "scripts/tests/fixtures_architecture_contracts/valid_module_interface.h"
        lines = [
            contracts.SourceLine(path.relative_to(ROOT).as_posix(), number, text)
            for number, text in enumerate(path.read_text(encoding="utf-8").splitlines(), 1)
        ]
        codes = {finding.code for finding in contracts.lint_module_interface(lines, {"entries": []})}
        self.assertNotIn("missing-cost-annotation", codes)

    def test_module_interface_gates_capability_hot_path_and_runtime_lookup(self):
        metadata = {
            "entries": [
                {
                    "rule": "module-interface",
                    "scope": "src/modules/fixture/**",
                    "provides": [{"capability": "IGreeter"}],
                    "requires": [],
                    "hot_path": ["src/modules/fixture/Hot.cpp"],
                }
            ]
        }
        lines = [
            contracts.SourceLine(
                "src/modules/fixture/Wire.cpp",
                10,
                "eve::cap::provide<IGreeter>(&g);",
            ),
            contracts.SourceLine(
                "src/modules/fixture/Wire.cpp",
                11,
                "eve::cap::ProviderRef<IGpuTimer>::bind();",
            ),
            contracts.SourceLine(
                "src/modules/fixture/Hot.cpp",
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
        findings = contracts.lint_module_interface(lines, metadata)
        codes = {finding.code for finding in findings}
        self.assertIn("capability-not-declared", codes)  # IGpuTimer missing from requires
        self.assertIn("hot-path-forbidden-primitive", codes)
        self.assertIn("runtime-convenience-lookup", codes)
        self.assertIn("missing-cost-annotation", codes)
        # Declared provide should not emit capability-not-declared for IGreeter.
        self.assertFalse(
            any(
                finding.code == "capability-not-declared" and "IGreeter" in finding.message
                for finding in findings
            )
        )


if __name__ == "__main__":
    unittest.main()

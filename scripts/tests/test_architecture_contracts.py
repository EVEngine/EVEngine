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
import module_interface_contracts as interfaces  # noqa: E402


class ArchitectureContractTests(unittest.TestCase):
    def test_scope_walk_preserves_exact_and_recursive_matches(self):
        exact = ROOT / "src/modules/climbing/Climbing.h"
        self.assertEqual([exact], contracts.scoped_files("src/modules/climbing/Climbing.h"))
        expected = {path for path in (ROOT / "src/modules/climbing").rglob("*") if path.is_file()}
        self.assertEqual(expected, set(contracts.scoped_files("src/modules/climbing/**")))
        self.assertEqual([], contracts.scoped_files("../outside/**"))

    def test_module_boundary_requires_exact_capability_name(self):
        entry = {"rule": "module-interface", "scope": "src/modules/demo/**",
                 "provides": [{"capability": "IKnown"}], "requires": [], "hot_path": []}
        path = "src/modules/demo/Module.cpp"
        self.assertEqual([], interfaces.lint_line(path, "cap::provide<IKnown>(this);", [entry]))
        self.assertTrue(interfaces.lint_line(path, "cap::provide<IOther>(this);", [entry]))
        self.assertTrue(interfaces.lint_line(path, "cap::query<IKnown>();", [entry]))
        self.assertTrue(interfaces.lint_line(path, "cap::query<IKnown>();", []))

    def test_hot_path_rejects_even_declared_capability_lookup(self):
        path = "src/modules/demo/Tick.cpp"
        entry = {"rule": "module-interface", "scope": "src/modules/demo/**",
                 "requires": [{"capability": "IKnown"}], "hot_path": [path]}
        for text in ("cap::query<IKnown>();", "getModInst(Animation);",
                     "requireModInst(Physics);", "ModuleManager::getInstance();"):
            with self.subTest(text=text):
                self.assertTrue(interfaces.lint_line(path, text, [entry]))
        self.assertEqual([], interfaces.lint_line(path, "boundPhysics.advance();", [entry]))

    def test_module_boundary_cross_checks_bindings_optional_tests_and_trim(self):
        metadata = contracts.load_json(ROOT / "scripts/architecture_contracts.json")
        original = next(item for item in metadata["entries"] if item["id"] == "climbing-module-interface")
        files = list((ROOT / "src/modules/climbing").glob("*"))
        self.assertEqual([], interfaces.validate(original, files, ROOT, "climbing"))
        for field, replacement, expected in (
            ("binds", [], "script class"),
            ("requires", [{"capability": "IClimbingPoseAdapter", "required": False}], "present_test"),
            ("trim", {"absent_profile": "3d"}, "still selects"),
            ("hot_path", ["src/modules/climbing/Missing.cpp"], "matches no scoped file"),
            ("observes", None, "must be an array"),
        ):
            with self.subTest(field=field):
                entry = copy.deepcopy(original)
                entry[field] = replacement
                self.assertTrue(any(expected in error for error in interfaces.validate(entry, files, ROOT, "climbing")))

    def test_repository_catalogue_covers_all_rules(self):
        metadata = contracts.load_json(ROOT / "scripts" / "architecture_contracts.json")
        self.assertEqual([], contracts.validate_catalogue(metadata, today=date(2026, 8, 26)))

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


if __name__ == "__main__":
    unittest.main()

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
    def test_repository_catalogue_covers_all_eleven_rules(self):
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

    def test_capability_use_must_be_named_in_module_interface(self):
        # A matching entry for the file is not enough: the capability name itself
        # must appear in `provides` (provide/addListener) or `requires`
        # (query/forEach/forEachUntil).
        metadata = {
            "entries": [
                {
                    "rule": "module-interface",
                    "scope": "src/modules/test/**",
                    "provides": [],
                    "requires": [],
                }
            ]
        }
        lines = [
            contracts.SourceLine("src/modules/test/New.cpp", 7, "eve::cap::provide<eve::INewCapability>(&impl);"),
            contracts.SourceLine("src/modules/test/New.cpp", 8, "cap::addListener<INewSink>(this);"),
            contracts.SourceLine("src/modules/test/New.cpp", 9, "eve::cap::query<INewQuery>();"),
            contracts.SourceLine(
                "src/modules/test/New.cpp",
                10,
                "cap::forEachUntil<INewSink>([&](INewSink* sink) { return sink != nullptr; });",
            ),
        ]
        findings = contracts.lint_contract_coverage(lines, metadata)
        self.assertEqual(4, len(findings))
        self.assertEqual({"undeclared-capability-face"}, {finding.code for finding in findings})
        self.assertEqual({"module-interface"}, {finding.rule for finding in findings})
        messages = " ".join(finding.message for finding in findings)
        # provide/addListener declare the outbound face; query/forEach declare the inbound face.
        self.assertIn("provides", messages)
        self.assertIn("requires", messages)

    def test_capability_use_declared_in_module_interface_passes(self):
        metadata = {
            "entries": [
                {
                    "rule": "module-interface",
                    "scope": "src/modules/test/**",
                    "provides": ["INewCapability", "INewSink"],
                    "requires": ["INewQuery"],
                }
            ]
        }
        lines = [
            contracts.SourceLine("src/modules/test/New.cpp", 7, "eve::cap::provide<eve::INewCapability>(&impl);"),
            contracts.SourceLine("src/modules/test/New.cpp", 8, "cap::addListener<INewSink>(this);"),
            contracts.SourceLine("src/modules/test/New.cpp", 9, "eve::cap::query<INewQuery>();"),
        ]
        self.assertEqual([], contracts.lint_contract_coverage(lines, metadata))

    def test_declared_hot_path_rejects_new_provider_lookup(self):
        metadata = {
            "entries": [
                {
                    "rule": "module-interface",
                    "id": "hot-module",
                    "scope": "src/modules/hot/**",
                    "hot_path": ["src/modules/hot/HotSystem.cpp"],
                }
            ]
        }
        lines = [
            contracts.SourceLine(
                "src/modules/hot/HotSystem.cpp", 5, 'auto* g = eve::ModuleManager::getInstance<Gfx>("Gfx");'
            ),
            contracts.SourceLine("src/modules/hot/HotSystem.cpp", 6, "auto* q = eve::cap::query<IPhysicsQuery>();"),
            contracts.SourceLine("src/modules/hot/HotSystem.cpp", 7, "auto* n = requireModInst(Other, Thing);"),
            # Outside the declared hot path: unaffected.
            contracts.SourceLine(
                "src/modules/hot/Cold.cpp", 8, 'auto* g = eve::ModuleManager::getInstance<Gfx>("Gfx");'
            ),
            # Inside the hot path but not a provider lookup: unaffected.
            contracts.SourceLine("src/modules/hot/HotSystem.cpp", 9, "const float dt = frame.deltaSeconds;"),
        ]
        findings = contracts.lint_hot_paths(lines, metadata)
        self.assertEqual(3, len(findings))
        self.assertEqual({"hot-path-primitive"}, {finding.code for finding in findings})
        self.assertTrue(all(finding.path == "src/modules/hot/HotSystem.cpp" for finding in findings))

    def test_runtime_module_rejects_convenience_lookup(self):
        runtime = [
            contracts.SourceLine("src/modules/map/TileSystem.cpp", 3, "auto* m = requireModInst(Graphics, Graphics);"),
            contracts.SourceLine("src/modules/map/TileSystem.cpp", 4, "auto* g = getModInst(Graphics, Graphics);"),
            # ModuleManager::getInstance stays permitted (checklist M-03 boundary form).
            contracts.SourceLine(
                "src/modules/map/TileSystem.cpp", 5, 'auto* g = eve::ModuleManager::getInstance<G>("G");'
            ),
        ]
        exempt = [
            contracts.SourceLine(
                "src/modules/map/editor/MapTarget.cpp", 3, "auto* m = requireModInst(Graphics, Graphics);"
            ),
            contracts.SourceLine(
                "src/modules/level_editor/Level.cpp", 4, "auto* m = requireModInst(Graphics, Graphics);"
            ),
            contracts.SourceLine(
                "src/modules/asset/import/Import.cpp", 5, "auto* m = requireModInst(Graphics, Graphics);"
            ),
            contracts.SourceLine(
                "src/engine/devtools/McpServer.cpp", 6, "auto* m = requireModInst(Graphics, Graphics);"
            ),
            contracts.SourceLine("scripts/check.py", 7, "auto* m = requireModInst(Graphics, Graphics);"),
        ]
        findings = contracts.lint_runtime_convenience(runtime)
        self.assertEqual(2, len(findings))
        self.assertEqual({"runtime-convenience-primitive"}, {finding.code for finding in findings})
        self.assertEqual([], contracts.lint_runtime_convenience(exempt))

    def test_primitive_definition_site_is_not_a_use(self):
        # Module.h declares the macros; the declaration must not count as a use in
        # either gate.
        definition = [
            contracts.SourceLine(
                "src/engine/common/Module.h", 43, "#define getModInst(N,T) eve::ModuleManager::getInstance<N::T>(#T)"
            ),
            contracts.SourceLine(
                "src/engine/common/Module.h",
                44,
                "#define requireModInst(N,T) eve::ModuleManager::requireInstance<N::T>(#T)",
            ),
        ]
        self.assertEqual([], contracts.lint_runtime_convenience(definition))
        hot = {
            "entries": [
                {
                    "rule": "module-interface",
                    "id": "hot-module",
                    "scope": "src/engine/common/**",
                    "hot_path": ["src/engine/common/Module.h"],
                }
            ]
        }
        self.assertEqual([], contracts.lint_hot_paths(definition, hot))

    def test_commented_primitive_mention_is_not_a_use(self):
        # A comment that names a forbidden primitive (typically to explain why it
        # is avoided) must not be reported as a use; a real call with a trailing
        # comment still must be.
        commented = [
            contracts.SourceLine(
                "src/modules/mouse/sdl/Mouse.cpp",
                122,
                "\t// Avoid `auto window = getModInst(window, ...)` -- local name shadows namespace `window`.",
            )
        ]
        self.assertEqual([], contracts.lint_runtime_convenience(commented))

        trailing = [
            contracts.SourceLine(
                "src/modules/mouse/sdl/Mouse.cpp", 9, "auto* m = requireModInst(Window, Window);  // boundary"
            )
        ]
        self.assertEqual(1, len(contracts.lint_runtime_convenience(trailing)))

        hot = {
            "entries": [
                {
                    "rule": "module-interface",
                    "id": "hot-module",
                    "scope": "src/modules/particles/**",
                    "hot_path": ["src/modules/particles/ParticleSystem.cpp"],
                }
            ]
        }
        commented_hot = [
            contracts.SourceLine(
                "src/modules/particles/ParticleSystem.cpp",
                8,
                '// auto* g = eve::ModuleManager::getInstance<Gfx>("Gfx");',
            )
        ]
        self.assertEqual([], contracts.lint_hot_paths(commented_hot, hot))

    def test_block_comment_naming_a_capability_is_not_a_use(self):
        # Documentation legitimately quotes `cap::query<T>()`; a doc block is not a
        # call site. Capability.h's own ProviderRef doc has exactly this shape and
        # must not force a provides/requires declaration for a comment.
        metadata = {
            "entries": [
                {
                    "rule": "module-interface",
                    "id": "common-module-interface",
                    "scope": "src/engine/common/**",
                    "provides": [],
                    "requires": [],
                }
            ]
        }
        lines = [
            contracts.SourceLine("src/engine/common/Capability.h", 157, "/**"),
            contracts.SourceLine("src/engine/common/Capability.h", 158, " * @brief Resolve-once reference."),
            contracts.SourceLine(
                "src/engine/common/Capability.h", 159, " * `cap::query<T>()` cannot hide behind it."
            ),
            contracts.SourceLine("src/engine/common/Capability.h", 160, " */"),
            # A real use on a code line is still reported, with its own line number.
            contracts.SourceLine("src/engine/common/Capability.h", 161, "auto* widget = cap::query<IWidget>();"),
        ]
        findings = [
            finding
            for finding in contracts.lint_contract_coverage(lines, metadata)
            if finding.code == "undeclared-capability-face"
        ]
        self.assertEqual(1, len(findings))
        self.assertEqual(161, findings[0].line)

        # A whole-line `//` mention is dropped as well.
        line_commented = [
            contracts.SourceLine("src/engine/common/Capability.h", 1, "// cap::query<IWidget>() is a lookup")
        ]
        self.assertEqual(
            [],
            [
                finding
                for finding in contracts.lint_contract_coverage(line_commented, metadata)
                if finding.code == "undeclared-capability-face"
            ],
        )


if __name__ == "__main__":
    unittest.main()

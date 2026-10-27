"""Coverage and isolation contracts for the complete C++ test catalogue."""

from copy import deepcopy
import json
from pathlib import Path
import sys
import tempfile
import subprocess
import unittest

SCRIPTS = Path(__file__).resolve().parents[1]
if str(SCRIPTS) not in sys.path:
    sys.path.insert(0, str(SCRIPTS))
import test_levels as levels  # noqa: E402


class TestLevelCatalogueTests(unittest.TestCase):
    def test_default_ctest_selection_excludes_physical_gpu_benchmarks_and_their_bundles(self):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            metadata = base / "levels.cmake"
            levels.emit_cmake({"sources": {
                "integration/native.cpp": {"role": "tests", "default": "module-composition",
                                           "cases": {"module-composition": ["native.contract"]}},
                "benchmark/graphics/gpu.cpp": {"role": "tests", "default": "render-window",
                                               "cases": {"render-window": ["resources.reuse"]}},
            }}, metadata)
            project = (
                'cmake_minimum_required(VERSION 3.21)\nproject(selection NONE)\nenable_testing()\n'
                f'include("{(levels.ROOT / "cmake/TestLevels.cmake").as_posix()}")\n'
                f'include("{metadata.as_posix()}")\n'
                'if("${EVE_TEST_DESKTOP_ORIGINS}" MATCHES "/benchmark/")\n'
                'message(FATAL_ERROR "benchmark entered default source discovery")\nendif()\n'
                'add_test(NAME native.contract COMMAND "${CMAKE_COMMAND}" -E true)\n'
                'add_test(NAME resources.reuse COMMAND "${CMAKE_COMMAND}" -E true)\n'
                'add_test(NAME bundle/gpu.cpp COMMAND "${CMAKE_COMMAND}" -E true)\n'
                'set(native_key "EVE_TEST_LEVEL_native.cpp|native.contract")\n'
                'set_tests_properties(native.contract PROPERTIES LABELS "${${native_key}}")\n'
                'set(key "EVE_TEST_LEVEL_gpu.cpp|resources.reuse")\n'
                'set_tests_properties(resources.reuse PROPERTIES LABELS "${${key}}")\n'
                'eve_merge_test_metadata(bundle_labels ${${key}})\n'
                'set_tests_properties(bundle/gpu.cpp PROPERTIES LABELS "bundle;${bundle_labels}")\n'
            )
            (base / "CMakeLists.txt").write_text(project, encoding="utf-8")
            configured = subprocess.run(["cmake", "-S", str(base), "-B", str(base / "build")],
                                        capture_output=True, text=True, encoding="utf-8", check=False)
            self.assertEqual(configured.returncode, 0, configured.stderr)

            def selected(*arguments):
                result = subprocess.run(["ctest", "--test-dir", str(base / "build"),
                                         "-C", "Debug", "--show-only=json-v1", *arguments], capture_output=True,
                                        text=True, encoding="utf-8", check=False)
                self.assertEqual(result.returncode, 0, result.stderr)
                return {test["name"]: test for test in json.loads(result.stdout)["tests"]}

            self.assertEqual(set(selected("-E", "^bundle/", "-LE", "benchmark")), {"native.contract"})
            self.assertEqual(set(selected("-E", "^bundle/")), {"native.contract", "resources.reuse"})
            bundles = selected("-L", "bundle")
            self.assertEqual(set(bundles), {"bundle/gpu.cpp"})
            labels = next(prop["value"] for prop in bundles["bundle/gpu.cpp"]["properties"]
                          if prop["name"] == "LABELS")
            self.assertTrue({"benchmark", "requires:graphics", "requires:window"}.issubset(labels))
    def test_formatter_reflow_preserves_policy(self):
        policy = '// eve-test-policy:\n// {"schema":"evengine.test-policy",\n// "version":1,"default":"local-format"}\n'
        self.assertEqual(levels.source_header_policy(policy + '#include "x.h"'), {"default": "local-format"})
        self.assertEqual(levels.source_header_policy('/* eve-test-policy:\n{"schema":"evengine.test-policy","version":1,"module":"rules"}\n*/'), {"module": "rules"})

    def test_v3_discovers_unit_sources_without_catalogue_entries(self):
        data = {"schema": "evengine.test-levels", "version": 3, "sources": {}}
        text = 'TEST_CASE("rules.added") { CHECK(true); }'
        inventory = {"unit/rules/new_rule.cpp": {"text": text, "cases": levels.cases_in(text), "generated": set()},
                     "unit/rules/helper.cpp": {"text": "int helper() { return 1; }", "cases": {}, "generated": set()}}
        self.assertEqual(levels.catalogue_errors(data, inventory), [])
        expanded = levels.expanded_catalogue(data, inventory)
        self.assertEqual(expanded["sources"]["unit/rules/new_rule.cpp"]["module"], "rules")
        self.assertEqual(expanded["sources"]["unit/rules/helper.cpp"]["role"], "helper")
        self.assertEqual(levels.classified_cases(expanded["sources"]["unit/rules/new_rule.cpp"]),
                         {"rules.added": "local-logic"})
        self.assertEqual(data["sources"], {})
        inventory["unit/rules/new_rule.cpp"]["text"] = 'TEST_CASE("rules.added") { openHeadlessGfx(gfx); }'
        inventory["unit/rules/new_rule.cpp"]["cases"] = levels.cases_in(inventory["unit/rules/new_rule.cpp"]["text"])
        self.assertTrue(any("external setup" in error for error in levels.catalogue_errors(data, inventory)))

    def test_v3_local_integration_semantics_and_resource_labels(self):
        from unittest.mock import patch

        data = {"schema": "evengine.test-levels", "version": 3, "sources": {}}
        policy = {"schema": "evengine.test-policy", "version": 1, "module": "graphics", "default": "render-window"}
        text = '// eve-test-policy: ' + json.dumps(policy) + '\nTEST_CASE("new.render") { CHECK(true); }'
        inventory = {"integration/rendering/new_render.cpp": {"text": text, "cases": levels.cases_in(text), "generated": set()}}
        self.assertEqual(levels.catalogue_errors(data, inventory), [])
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "integration/rendering/new_render.cpp"
            source.parent.mkdir(parents=True)
            source.write_text(text, encoding="utf-8")
            output = root / "levels.cmake"
            with patch.object(levels, "TEST_DIR", root):
                levels.emit_cmake(data, output)
            rendered = output.read_text(encoding="utf-8")
            self.assertIn("integration;reason:render-window;requires:graphics;requires:window", rendered)
            self.assertIn(source.as_posix(), rendered)
        no_owner = text.replace(', "module": "graphics"', '')
        inventory["integration/rendering/new_render.cpp"]["text"] = no_owner
        self.assertTrue(any("semantic module ownership" in error for error in levels.catalogue_errors(data, inventory)))

    def test_v3_header_policy_rejects_invalid_and_duplicate_authorities(self):
        data = {"schema": "evengine.test-levels", "version": 3, "sources": {}}
        declaration = '// eve-test-policy: {"schema":"evengine.test-policy","version":1,"default":"local-format"}\n'
        body = 'TEST_CASE("rules.case") { CHECK(true); }'
        def inventory(text):
            return {"unit/rules/rules.cpp": {"text": text, "cases": levels.cases_in(text), "generated": set()}}
        self.assertEqual(levels.catalogue_errors(data, inventory(declaration + body)), [])
        for header in ('// eve-test-policy: not-json\n', declaration + declaration,
                       declaration.replace('"version":1', '"version":999'),
                       declaration.replace('"version":1', '"version":true'),
                       declaration.replace('"default":"local-format"', '"unknown":"value"'),
                       declaration.replace('"default":"local-format"', '"default":[]'),
                       declaration.replace('"default":"local-format"', '"module":{}'),
                       declaration.replace('"default":"local-format"', '"generated":[1]'),
                       declaration.replace('"default":"local-format"', '"cases":{"local-logic":[1]}'),
                       declaration.replace('"default":"local-format"', '"cases":[]')):
            with self.subTest(header=header):
                self.assertTrue(levels.catalogue_errors(data, inventory(header + body)))
        data["sources"]["unit/rules/rules.cpp"] = {"default": "local-format"}
        self.assertTrue(any("both source and catalogue" in e for e in levels.catalogue_errors(data, inventory(declaration + body))))
        self.assertEqual(levels.source_header_policy('const char* s = R"x(\n' + declaration + ')x";'), {})
        self.assertEqual(levels.source_header_policy('int value;\n' + declaration), {})

    def test_v3_new_source_addition_and_removal_refresh_cmake(self):
        import shutil

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for folder in ("cmake", "scripts", "test/unit/rules"):
                (root / folder).mkdir(parents=True)
            for relative in ("cmake/TestLevels.cmake", "scripts/test_levels.py", "scripts/cpp_source.py", "scripts/utf8_stdio.py"):
                shutil.copyfile(levels.ROOT / relative, root / relative)
            (root / "test/unit/rules/first.cpp").write_text('TEST_CASE("first.case") { CHECK(true); }', encoding="utf-8")
            (root / "CMakeLists.txt").write_text(
                'cmake_minimum_required(VERSION 3.21)\nproject(SourceDiscovery NONE)\n'
                'include(cmake/TestLevels.cmake)\neve_configure_test_levels(metadata)\n', encoding="utf-8")
            build = root / "build"
            result = subprocess.run(["cmake", "-G", "Ninja", "-S", str(root), "-B", str(build)],
                                    capture_output=True, text=True, encoding="utf-8", check=False)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            # No catalogue or existing .cpp changes: a new nested directory and
            # source alone must refresh the CTest metadata on the next build.
            added = root / "test/unit/rules/extra/added.cpp"
            added.parent.mkdir()
            added.write_text('TEST_CASE("added.case") { CHECK(true); }', encoding="utf-8")
            def rebuild():
                result = subprocess.run(["cmake", "--build", str(build)], capture_output=True,
                                        text=True, encoding="utf-8", check=False)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                return (build / "test_levels.cmake").read_text(encoding="utf-8")
            self.assertIn('EVE_TEST_LEVEL_added.cpp|added.case', rebuild())
            added.unlink()
            self.assertNotIn('EVE_TEST_LEVEL_added.cpp|added.case', rebuild())

    def fixture(self, text='TEST_CASE("local.case") { CHECK(1 == 1); }'):
        inventory = {"fixture.cpp": {"text": text, "cases": levels.cases_in(text),
                                     "generated": levels.generated_prefixes_in(text)}}
        data = {"schema": "evengine.test-levels", "version": 1, "sources": {
            "fixture.cpp": {"role": "tests", "default": "local-logic",
                            "cases": {"local-logic": list(inventory["fixture.cpp"]["cases"])}}}}
        return data, inventory

    def test_missing_new_source_or_case_is_an_error(self):
        data, inventory = self.fixture()
        self.assertEqual(levels.catalogue_errors(data, inventory), [])
        added = deepcopy(inventory)
        added["new.cpp"] = inventory["fixture.cpp"]
        self.assertTrue(any("missing explicit test classification" in e for e in levels.catalogue_errors(data, added)))
        data["sources"]["fixture.cpp"]["cases"] = {}
        self.assertTrue(any("missing explicit case classification" in e for e in levels.catalogue_errors(data, inventory)))

    def test_compact_defaults_classify_new_cases_and_keep_unit_safety(self):
        data, inventory = self.fixture()
        data["version"] = 2
        policy = data["sources"]["fixture.cpp"]
        del policy["cases"]
        self.assertEqual(levels.catalogue_errors(data, inventory), [])
        text = inventory["fixture.cpp"]["text"] + '\nTEST_CASE("new.case") { openHeadlessGfx(gfx); }'
        inventory["fixture.cpp"].update(text=text, cases=levels.cases_in(text))
        self.assertTrue(any("new.case: unit case contains external setup" in e
                            for e in levels.catalogue_errors(data, inventory)))
        policy["cases"] = {"render-offscreen": ["new.case"]}
        self.assertEqual(levels.catalogue_errors(data, inventory), [])
        expanded = levels.expanded_catalogue(data, inventory)
        self.assertEqual(levels.classified_cases(expanded["sources"]["fixture.cpp"]),
                         {"local.case": "local-logic", "new.case": "render-offscreen"})
        policy["cases"]["render-offscreen"].append("removed.case")
        self.assertTrue(any("stale case" in e for e in levels.catalogue_errors(data, inventory)))
        policy["cases"]["module-composition"] = ["new.case"]
        self.assertTrue(any("more than one" in e for e in levels.catalogue_errors(data, inventory)))

    def test_compact_and_full_catalogues_emit_identical_labels_and_origins(self):
        from unittest.mock import patch

        text = 'TEST_CASE("local.case") { CHECK(true); }\nTEST_CASE("gpu.case") { openHeadlessGfx(gfx); }'
        data, inventory = self.fixture(text)
        policy = data["sources"]["fixture.cpp"]
        policy["origins"] = ["old.cpp"]
        policy["cases"] = {"local-logic": ["local.case"], "render-offscreen": ["gpu.case"]}
        compact = deepcopy(data)
        compact["version"] = 2
        del compact["sources"]["fixture.cpp"]["cases"]["local-logic"]
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "fixture.cpp").write_text(text, encoding="utf-8")
            before, after = root / "before.cmake", root / "after.cmake"
            with patch.object(levels, "TEST_DIR", root):
                levels.emit_cmake(data, before)
                levels.emit_cmake(compact, after)
            self.assertEqual(before.read_text(encoding="utf-8"), after.read_text(encoding="utf-8"))
        self.assertNotIn("local-logic", compact["sources"]["fixture.cpp"]["cases"])

    def test_editing_cpp_refreshes_cmake_without_editing_policy(self):
        import shutil

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for folder in ("cmake", "scripts", "test"):
                (root / folder).mkdir()
            for relative in ("cmake/TestLevels.cmake", "scripts/test_levels.py", "scripts/cpp_source.py", "scripts/utf8_stdio.py"):
                shutil.copyfile(levels.ROOT / relative, root / relative)
            data, _ = self.fixture()
            data["version"] = 2
            del data["sources"]["fixture.cpp"]["cases"]
            source = root / "test/fixture.cpp"
            header = '// eve-test-policy: {"schema":"evengine.test-policy","version":1,"role":"tests","default":"local-logic"}\n'
            source.write_text(header + 'TEST_CASE("local.case") { CHECK(true); }', encoding="utf-8")
            (root / "CMakeLists.txt").write_text(
                'cmake_minimum_required(VERSION 3.21)\nproject(LevelRefresh NONE)\n'
                'include(cmake/TestLevels.cmake)\neve_configure_test_levels(metadata)\n', encoding="utf-8")
            build = root / "build"
            result = subprocess.run(["cmake", "-G", "Ninja", "-S", str(root), "-B", str(build)],
                                    capture_output=True, text=True, encoding="utf-8", check=False)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            source.write_text(source.read_text(encoding="utf-8") +
                              '\nTEST_CASE("added.case") { CHECK(true); }', encoding="utf-8")
            result = subprocess.run(["cmake", "--build", str(build)], capture_output=True,
                                    text=True, encoding="utf-8", check=False)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('EVE_TEST_LEVEL_fixture.cpp|added.case',
                          (build / "test_levels.cmake").read_text(encoding="utf-8"))

    def test_compact_projection_derives_path_ownership_without_mutating_policy(self):
        data, inventory = self.fixture()
        data["version"] = 2
        policy = data["sources"].pop("fixture.cpp")
        policy.pop("cases")
        data["sources"]["unit/rules/fixture.cpp"] = policy
        inventory["unit/rules/fixture.cpp"] = inventory.pop("fixture.cpp")
        self.assertEqual(levels.catalogue_errors(data, inventory), [])
        expanded = levels.expanded_catalogue(data, inventory)
        self.assertEqual(expanded["sources"]["unit/rules/fixture.cpp"]["module"], "rules")
        self.assertNotIn("module", policy)
        self.assertEqual(levels.source_policy("integration/save-restore/fixture.cpp", {"module": "rules"}),
                         {"module": "rules", "task": "save-restore"})

    def test_stale_duplicate_and_unknown_policy_are_errors(self):
        data, inventory = self.fixture()
        data["sources"]["fixture.cpp"]["cases"]["module-composition"] = ["local.case"]
        self.assertTrue(any("more than one" in e for e in levels.catalogue_errors(data, inventory)))
        data, inventory = self.fixture()
        data["sources"]["fixture.cpp"]["cases"]["local-logic"].append("removed.case")
        self.assertTrue(any("stale case" in e for e in levels.catalogue_errors(data, inventory)))
        data["version"] = 999
        self.assertTrue(any("schema/version" in e for e in levels.catalogue_errors(data, inventory)))

    def test_comments_raw_scripts_and_tags_do_not_invent_or_lose_cases(self):
        text = '''
// TEST_CASE("comment") {}
const char* script = R"nut(TEST_CASE("script") { })nut";
TEST_CASE("real.case", zeroerr::skip()) { const char* braces = "}"; }
TEST_CASE_FIXTURE(LocalFixture, "fixture.case") { CHECK(true); }
'''
        self.assertEqual(set(levels.cases_in(text)), {"real.case", "fixture.case"})
        self.assertEqual(levels.generated_prefixes_in(text), set())

    def test_numeric_digit_separators_do_not_swallow_following_declarations(self):
        text = '''
auto nanos = 100'000'000;
auto more = 1'500'000'000;
auto paren = U')';
TEST_CASE("first.case") { CHECK(nanos < more); }
TEST_CASE("second.case") { CHECK(true); }
'''
        self.assertEqual(set(levels.cases_in(text)), {"first.case", "second.case"})

    def test_standalone_fuzz_properties_are_inventoried(self):
        data, inventory = self.fixture('FUZZ_TEST_CASE("fuzz.local") { CHECK(true); }')
        data["sources"]["fixture.cpp"]["role"] = "fuzz"
        self.assertIn("fuzz.local", inventory["fixture.cpp"]["cases"])
        self.assertEqual(levels.catalogue_errors(data, inventory), [])

    def test_macro_generated_names_require_an_explicit_bounded_prefix(self):
        text = '#define DECLARE(func) TEST_CASE_FIXTURE(Fixture, "generated." #func) { CHECK(true); }'
        data, inventory = self.fixture(text)
        self.assertEqual(inventory["fixture.cpp"]["cases"], {})
        self.assertTrue(any("macro-generated" in e for e in levels.catalogue_errors(data, inventory)))
        data["sources"]["fixture.cpp"]["generated"] = ["generated."]
        self.assertEqual(levels.catalogue_errors(data, inventory), [])

    def test_conditional_duplicate_implementations_keep_both_bodies(self):
        data, inventory = self.fixture('''
#if WITH_GPU
TEST_CASE("same.case") { openHeadlessGfx(gfx); }
#else
TEST_CASE("same.case") { CHECK(true); }
#endif
''')
        self.assertEqual(len(inventory["fixture.cpp"]["cases"]), 1)
        self.assertTrue(any("external setup" in e for e in levels.catalogue_errors(data, inventory)))

    def test_indirect_rendering_script_file_and_worker_setup_are_not_unit(self):
        for setup in ("openHeadlessGfx(gfx)", "ssq::VM vm", "std::filesystem::create_directory(path)",
                      "std::thread worker(callback)", "Graphics::create()"):
            with self.subTest(setup=setup):
                data, inventory = self.fixture(f'void helper() {{ {setup}; }} TEST_CASE("local.case") {{ helper(); }}')
                self.assertTrue(any("external setup" in e for e in levels.catalogue_errors(data, inventory)))

    def test_shared_header_fixture_setup_is_followed(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            header = root / "Fixture.h"
            header.write_text("struct Fixture { Fixture() { openHeadlessGfx(gfx); } };", encoding="utf-8")
            text = '#include "Fixture.h"\nTEST_CASE_FIXTURE(Fixture, "local.case") { CHECK(true); }'
            helpers = levels.helper_bodies(levels.local_helper_text(root / "case.cpp", text, root))
            reachable = levels.reachable_helper_text("CHECK(true);", "Fixture", helpers)
            self.assertIsNotNone(levels.UNIT_SETUP.search(levels.mask_cpp(reachable)))

    def test_emit_preserves_mtime_when_classification_is_unchanged(self):
        data, _ = self.fixture()
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "levels.cmake"
            levels.emit_cmake(data, output)
            before = output.stat().st_mtime_ns
            levels.emit_cmake(data, output)
            self.assertEqual(before, output.stat().st_mtime_ns)
            self.assertIn("unit;reason:local-logic;no-render", output.read_text(encoding="utf-8"))

    def test_repository_catalogue_covers_every_source_and_case(self):
        data = levels.source_catalogue()
        inventory = levels.source_inventory(levels.ROOT / "test")
        self.assertEqual(levels.catalogue_errors(data, inventory), [])

    def test_relocation_resolves_both_halves_and_new_canonical_sources(self):
        data = {"schema": "evengine.test-levels", "version": 1, "sources": {
            "unit/rules/rules_unit.cpp": {"role": "tests", "default": "local-logic", "origins": ["rules.cpp"],
                "cases": {"local-logic": ["rules.local"]}},
            "integration/journey/rules.cpp": {"role": "tests", "default": "module-composition", "origins": ["rules.cpp"],
                "cases": {"module-composition": ["rules.journey"]}},
            "unit/rules/new_rule.cpp": {"role": "tests", "default": "local-logic", "cases": {"local-logic": ["rules.new"]}},
        }}
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "levels.cmake"
            levels.emit_cmake(data, output)
            rendered = output.read_text(encoding="utf-8")
            self.assertIn("EVE_TEST_RELOCATED_rules.cpp", rendered)
            self.assertIn("unit/rules/rules_unit.cpp", rendered)
            self.assertIn("integration/journey/rules.cpp", rendered)
            desktop = next(line for line in rendered.splitlines() if line.startswith("set(EVE_TEST_DESKTOP_ORIGINS"))
            self.assertIn("unit/rules/new_rule.cpp", desktop)

    def test_consolidated_namespaces_keep_helper_safety_local(self):
        text = """namespace test_native {
int build() { return 7; }
TEST_CASE("local.case") { CHECK(build() == 7); }
}  // namespace test_native
namespace test_script {
void build() { ScriptTest script("fixture"); }
}  // namespace test_script
"""
        data, inventory = self.fixture(text)
        self.assertEqual(levels.catalogue_errors(data, inventory), [])
        unsafe = text.replace("CHECK(build() == 7);", "test_script::build();")
        data, inventory = self.fixture(unsafe)
        self.assertTrue(any("ScriptTest" in error for error in levels.catalogue_errors(data, inventory)))

    def test_origin_selection_is_target_local_and_registers_only_compiled_cases(self):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            source = base / "merged.cpp"
            source.write_text("// merged rules\n", encoding="utf-8")
            cmake = base / "CMakeLists.txt"
            cmake.write_text(
                'cmake_minimum_required(VERSION 3.21)\nproject(selection NONE)\n'
                f'include("{(levels.ROOT / "cmake/TestLevels.cmake").as_posix()}")\n'
                f'set(EVE_TEST_RELOCATED_local.cpp "{source.as_posix()}")\n'
                f'set(EVE_TEST_RELOCATED_gpu.cpp "{source.as_posix()}")\n'
                'set(EVE_TEST_CASE_NAMES_merged.cpp local.case gpu.case)\n'
                'set(EVE_TEST_CASE_NAMES_local.cpp local.case)\n'
                'set(EVE_TEST_CASE_NAMES_gpu.cpp gpu.case)\n'
                f'eve_test_origin_definitions(local_defs "{(base / "local.cpp").as_posix()}")\n'
                f'eve_test_origin_definitions(gpu_defs "{(base / "gpu.cpp").as_posix()}")\n'
                'add_custom_target(native)\nadd_custom_target(graphics)\n'
                'set_property(TARGET native PROPERTY EVE_TEST_ORIGIN_DEFINITIONS ${local_defs})\n'
                'set_property(TARGET graphics PROPERTY EVE_TEST_ORIGIN_DEFINITIONS ${gpu_defs})\n'
                f'eve_test_source_case_names("{source.as_posix()}" native_cases native)\n'
                f'eve_test_source_case_names("{source.as_posix()}" gpu_cases graphics)\n'
                'if(NOT native_cases STREQUAL "local.case" OR NOT gpu_cases STREQUAL "gpu.case")\n'
                'message(FATAL_ERROR "selections leaked between targets")\nendif()\n'
                f'get_source_file_property(source_defs "{source.as_posix()}" COMPILE_DEFINITIONS)\n'
                'if(source_defs)\nmessage(FATAL_ERROR "target selection mutated shared source")\nendif()\n', encoding="utf-8")
            result = subprocess.run(["cmake", "-G", "Ninja", "-S", str(base), "-B", str(base / "build")],
                                    text=True, encoding="utf-8", capture_output=True, check=False)
            self.assertEqual(result.returncode, 0, result.stderr)

    def test_generator_calls_cannot_share_a_registration_line(self):
        text = '#define Generate(name) TEST_CASE("generated." #name) { CHECK(true); }\nGenerate(one) Generate(two)\n'
        self.assertEqual(levels.generator_line_collisions(text), [2])
        self.assertEqual(levels.generator_line_collisions(text.replace("Generate(one) Generate(two)", "Generate(one)\nGenerate(two)")), [])

    def test_integration_sources_do_not_use_legacy_origin_selectors(self):
        for source in (levels.ROOT / "test/integration").rglob("*.cpp"):
            text = levels.mask_cpp(source.read_text(encoding="utf-8"))
            self.assertNotIn("EVE_TEST_FILTER_ORIGINS", text, str(source))
            self.assertNotIn("EVE_TEST_ORIGIN_", text, str(source))

    def test_merged_integration_source_selection_keeps_all_compiled_cases(self):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            workflow = base / "integration/journey"
            workflow.mkdir(parents=True)
            merged, sibling = workflow / "merged.cpp", workflow / "sibling.cpp"
            merged.write_text('TEST_CASE("journey.first") {}\nTEST_CASE("journey.second") {}\n', encoding="utf-8")
            sibling.write_text('TEST_CASE("journey.sibling") {}\n', encoding="utf-8")
            cmake = base / "CMakeLists.txt"
            cmake.write_text(
                'cmake_minimum_required(VERSION 3.21)\nproject(workflows NONE)\n'
                f'include("{(levels.ROOT / "cmake/TestLevels.cmake").as_posix()}")\n'
                f'set(EVE_TEST_RELOCATED_old.cpp "{merged.as_posix()}")\n'
                f'eve_test_origin_definitions(definitions "{(base / "old.cpp").as_posix()}")\n'
                'if(definitions)\nmessage(FATAL_ERROR "merged integration selection generated origin macros")\nendif()\n'
                f'eve_resolve_test_sources(selected "{(base / "old.cpp").as_posix()}" "{merged.as_posix()}")\n'
                f'if(NOT selected STREQUAL "{merged.as_posix()}")\n'
                'message(FATAL_ERROR "merged source was duplicated or pulled in a sibling")\nendif()\n'
                'add_custom_target(focused SOURCES ${selected})\n'
                'get_target_property(actual focused SOURCES)\n'
                f'if(NOT actual STREQUAL "{merged.as_posix()}")\n'
                'message(FATAL_ERROR "target does not compile the complete merged source")\nendif()\n'
                # A native legacy selector on the same target cannot hide the
                # integration cases that the physical source actually compiles.
                'set_property(TARGET focused PROPERTY EVE_TEST_ORIGIN_DEFINITIONS '
                'EVE_TEST_FILTER_ORIGINS EVE_TEST_ORIGIN_native)\n'
                'set(EVE_TEST_CASE_NAMES_merged.cpp journey.first journey.second)\n'
                'set(EVE_TEST_CASE_NAMES_native.cpp native.case)\n'
                f'eve_test_source_case_names("{merged.as_posix()}" names focused)\n'
                'if(NOT names STREQUAL "journey.first;journey.second")\n'
                'message(FATAL_ERROR "unit selection hid merged integration cases")\nendif()\n',
                encoding="utf-8")
            result = subprocess.run(["cmake", "-G", "Ninja", "-S", str(base), "-B", str(base / "build")],
                                    text=True, encoding="utf-8", capture_output=True, check=False)
            self.assertEqual(result.returncode, 0, result.stderr)

    def test_test_targets_do_not_add_unity_build_configuration(self):
        for relative in ("cmake/TestLevels.cmake", "test/CMakeLists.txt"):
            text = (levels.ROOT / relative).read_text(encoding="utf-8")
            self.assertNotIn("UNITY_BUILD", text, relative)
            self.assertNotIn("UNITY_GROUP", text, relative)
            self.assertNotIn("eve_group_integration_sources", text, relative)

    def test_result_header_contract_checks_backend_before_assertion_headers(self):
        header_contract = levels.ROOT / "test/unit/editing/editing_tests.cpp"
        self.assertTrue(header_contract.is_file())
        contract_text = header_contract.read_text(encoding="utf-8")
        isolation_check = contract_text.index('#error "Result headers must not expose the zeroerr assertion backend"')
        self.assertLess(isolation_check, contract_text.index('#include "zeroerr/assert.h"'))
        self.assertLess(isolation_check, contract_text.index('#include <zeroerr/unittest.h>'))

    def test_origin_case_policy_does_not_expand_to_the_whole_merged_file(self):
        text = """#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_local)
namespace test_local {
TEST_CASE("local.case") { CHECK(true); }
}  // namespace test_local
#endif
#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_gpu)
namespace test_gpu {
TEST_CASE("gpu.case") { CHECK(true); }
}  // namespace test_gpu
#endif
"""
        policy = {"origins": ["local.cpp", "gpu.cpp"], "cases": {
            "local-logic": ["local.case"], "render-offscreen": ["gpu.case"]}}
        selected = levels.origin_case_policies(text, policy)
        self.assertEqual(selected["local.cpp"], {"local.case": "local-logic"})
        self.assertEqual(selected["gpu.cpp"], {"gpu.case": "render-offscreen"})

    def test_actual_canonical_source_is_not_expanded_as_a_legacy_alias(self):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            unit = base / "unit/rules_unit.cpp"
            integration = base / "integration/rules.cpp"
            unit.parent.mkdir()
            integration.parent.mkdir()
            unit.write_text("// native rules\n", encoding="utf-8")
            integration.write_text("// composition\n", encoding="utf-8")
            script = base / "resolve.cmake"
            script.write_text(
                f'include("{(levels.ROOT / "cmake/TestLevels.cmake").as_posix()}")\n'
                f'set("EVE_TEST_RELOCATED_rules.cpp" "{unit.as_posix()};{integration.as_posix()}")\n'
                f'eve_resolve_test_sources(actual "{integration.as_posix()}")\n'
                f'if(NOT actual STREQUAL "{integration.as_posix()}")\nmessage(FATAL_ERROR "canonical source expanded twice")\nendif()\n'
                f'eve_resolve_test_sources(legacy "{(base / "rules.cpp").as_posix()}" "{unit.as_posix()}")\n'
                f'if(NOT legacy STREQUAL "{unit.as_posix()};{integration.as_posix()}")\nmessage(FATAL_ERROR "legacy split was not deduplicated")\nendif()\n',
                encoding="utf-8")
            result = subprocess.run(["cmake", "-P", str(script)], text=True, encoding="utf-8",
                                    capture_output=True, check=False)
            self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()

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
        data = json.loads(levels.CATALOGUE.read_text(encoding="utf-8"))
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

"""Exercise the real CMake discovery script with portable runner fixtures."""

from pathlib import Path
import re
import json
import shlex
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
DISCOVERY = ROOT / "cmake" / "ZeroErrDiscoverTestsImpl.cmake"
if str(ROOT / "scripts") not in sys.path:
    sys.path.insert(0, str(ROOT / "scripts"))
from test_levels import CATALOGUE, CLASSIFICATIONS, classified_cases, emit_cmake  # noqa: E402


@unittest.skipUnless(shutil.which("cmake"), "CMake is required for discovery tests")
class ZeroerrDiscoveryTests(unittest.TestCase):
    def test_local_logic_and_composition_cases_have_exclusive_levels(self):
        result, generated = self.discover(
            [("math.local", "math.cpp", 10),
             ("composition.local", "composition.cpp", 20)], "plain",
            levels={"math.local": "local-logic"})
        self.assertEqual(result.returncode, 0, result.stderr)
        for name in ("math.local", "bundle/math.cpp"):
            labels = self.labels_of(generated, name)
            self.assertIn("unit", labels)
            self.assertNotIn("integration", labels)
        for name in ("composition.local", "bundle/composition.cpp"):
            labels = self.labels_of(generated, name)
            self.assertIn("integration", labels)
            self.assertNotIn("unit", labels)

    def discover(self, entries, listing_format, name_prefix="", levels=None, omit_metadata=False, catalogue=None,
                 runtime_output=None):
        with tempfile.TemporaryDirectory(prefix="eve discovery ") as directory:
            root = Path(directory)
            listing = root / "listing.py"
            listing.write_text(
                "import sys\n"
                f"entries = {entries!r}\n"
                f"runtime_output = {runtime_output!r}\n"
                "if runtime_output is not None and not any(a.startswith('--list') for a in sys.argv):\n"
                "    print(runtime_output)\n"
                "    raise SystemExit(0)\n"
                "plain = '--list-format=plain' in sys.argv\n"
                f"if plain and {listing_format!r} == 'legacy':\n"
                "    print('legacy runner: no plain listing available')\n"
                "else:\n"
                "    for name, file, line in entries:\n"
                "        if plain:\n"
                "            print(f'{name}\\t{file}:{line}')\n"
                "        else:\n"
                "            print(f'TEST CASE [{file}:{line}] {name}')\n",
                encoding="utf-8",
            )
            if sys.platform == "win32":
                runner = root / "runner.cmd"
                runner.write_text(
                    f'@echo off\n"{sys.executable}" "{listing}" %*\n',
                    encoding="utf-8",
                )
            else:
                runner = root / "runner"
                runner.write_text(
                    f"#!/bin/sh\nexec {shlex.quote(sys.executable)} "
                    f'{shlex.quote(str(listing))} "$@"\n',
                    encoding="utf-8",
                )
                runner.chmod(0o755)
            registry = root / "discovered.cmake"
            sources = {}
            for name, file, _ in entries:
                policy = sources.setdefault(file, {"role": "tests", "default": "module-composition", "cases": {}})
                category = (levels or {}).get(name, "module-composition")
                policy["cases"].setdefault(category, []).append(name)
            metadata = root / "levels.cmake"
            emit_cmake(catalogue or {"sources": {} if omit_metadata else sources}, metadata)
            result = subprocess.run(
                [
                    "cmake",
                    f"-DZEROERR_EXE={runner.as_posix()}",
                    f"-DCTEST_FILE={registry.as_posix()}",
                    f"-DZEROERR_WORKING_DIRECTORY={root.as_posix()}",
                    f"-DZEROERR_NAME_PREFIX={name_prefix}",
                    f"-DZEROERR_LEVEL_FILE={metadata.as_posix()}",
                    "-P",
                    str(DISCOVERY),
                ],
                capture_output=True,
                text=True,
                check=False,
                timeout=30,
            )
            generated = registry.read_text(encoding="utf-8") if registry.exists() else None
            if runtime_output is not None:
                (root / "CTestTestfile.cmake").write_text(generated, encoding="utf-8")
                execution = subprocess.run(
                    ["ctest", "--test-dir", str(root), "--no-tests=error", "-V"],
                    capture_output=True, text=True, encoding="utf-8", timeout=30,
                )
                return result, generated, execution
            return result, generated

    def test_zero_exit_assertion_output_fails_case_and_bundle(self):
        for severity in ("WARN", "ERROR"):
            with self.subTest(severity=severity):
                result, _, execution = self.discover(
                    [("fixture.assertion", "fixture.cpp", 1)], "plain",
                    runtime_output=severity + " Assertion Failed: deliberate failure",
                )
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertNotEqual(execution.returncode, 0, execution.stdout + execution.stderr)
                self.assertIn("0% tests passed, 2 tests failed", execution.stdout)

    @staticmethod
    def labels_of(generated, test_name):
        """Return the LABELS property of one generated CTest entry as a set.

        Every entry carries its link unit's label in addition to the opt-in
        ``bundle`` / ``benchmark`` markers (see ZeroErrDiscoverTests.cmake), so
        the assertions below check membership instead of an exact property
        string: a domain label must never hide the opt-in marker.
        """

        match = re.search(
            r'set_tests_properties\("%s" PROPERTIES LABELS "([^"]*)"'
            % re.escape(test_name),
            generated,
        )
        if match is None:
            return set()
        return set(match.group(1).split(";"))

    def test_unique_names_keep_exact_filters_and_opt_in_bundles(self):
        entries = [("fluid.first", "fluid.cpp", 10), ("fluid.second", "fluid.cpp", 30),
                   ("fixture.lifecycle", "lifecycle.cpp", 7)]
        for listing_format in ("plain", "legacy"):
            with self.subTest(listing_format=listing_format):
                result, generated = self.discover(entries, listing_format)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertEqual(generated.count("add_test("), 5)
                for name, _, _ in entries:
                    self.assertEqual(generated.count(f'add_test("{name}"'), 1)
                    escaped = re.escape(name).replace("\\", "\\\\")
                    self.assertIn(f'"--testcase=^{escaped}$"', generated)
                self.assertIn("bundle", self.labels_of(generated, "bundle/fluid.cpp"))

    def test_composition_prefix_preserves_filters_and_bundle_labels(self):
        result, generated = self.discover([("fixture.lifecycle", "fixture.cpp", 7)], "plain", "authoring-contract/")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('add_test("authoring-contract/fixture.lifecycle"', generated)
        self.assertIn(r'"--testcase=^fixture\\.lifecycle$"', generated)
        self.assertIn("bundle", self.labels_of(generated, "authoring-contract/bundle/fixture.cpp"))
        self.assertNotIn('add_test("fixture.lifecycle"', generated)

    def test_mixed_bundle_cannot_enter_either_level_or_no_render_lane(self):
        result, generated = self.discover(
            [("rules.local", "mixed.cpp", 1), ("rules.draw", "mixed.cpp", 9)],
            "plain", levels={"rules.local": "local-logic", "rules.draw": "render-window"})
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("unit", self.labels_of(generated, "rules.local"))
        self.assertIn("no-render", self.labels_of(generated, "rules.local"))
        self.assertIn("requires:graphics", self.labels_of(generated, "rules.draw"))
        unit_properties = next(line for line in generated.splitlines()
                               if line.startswith('set_tests_properties("rules.local" PROPERTIES LABELS'))
        integration_properties = next(line for line in generated.splitlines()
                                      if line.startswith('set_tests_properties("rules.draw" PROPERTIES LABELS'))
        self.assertIn("EVENGINE_TEST_NO_RENDER=1", unit_properties)
        self.assertNotIn("EVENGINE_TEST_NO_RENDER", integration_properties)
        bundle = self.labels_of(generated, "bundle/mixed.cpp")
        self.assertIn("mixed", bundle)
        self.assertIn("requires:graphics", bundle)
        self.assertTrue(bundle.isdisjoint({"unit", "integration", "no-render"}))

    def test_unknown_discovered_case_fails_instead_of_defaulting_to_integration(self):
        result, generated = self.discover([("new.case", "new.cpp", 1)], "plain", omit_metadata=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("unclassified C++ test", result.stderr)
        self.assertIsNone(generated)

    def test_literal_case_and_file_filters_cannot_cross_test_levels(self):
        result, generated = self.discover(
            [("rules.local+", "data.cpp", 1), ("rulesXlocal+", "metadata.cpp", 9)],
            "plain", levels={"rules.local+": "local-logic"})
        self.assertEqual(result.returncode, 0, result.stderr)
        pattern = re.search(r'"--testcase=([^"\n]+)"', generated)[1].replace("\\\\", "\\")
        self.assertIsNotNone(re.fullmatch(pattern, "rules.local+"))
        self.assertIsNone(re.fullmatch(pattern, "rulesXlocal+"))
        file_pattern = re.search(r'"--file=([^"\n]+)"', generated)[1].replace("\\\\", "\\")
        for filename in ("data.cpp", "/test/data.cpp", r"C:\test\data.cpp"):
            self.assertIsNotNone(re.fullmatch(file_pattern, filename))
        self.assertIsNone(re.fullmatch(file_pattern, "metadata.cpp"))

    def test_complete_catalogue_and_generator_policies_are_accepted_by_runtime_discovery(self):
        catalogue = json.loads(CATALOGUE.read_text(encoding="utf-8"))
        entries, expected = [], {}
        for source, policy in catalogue["sources"].items():
            for name, category in classified_cases(policy).items():
                entries.append((name, Path(source).name, 1))
                expected[name] = CLASSIFICATIONS[category][0]
            for prefix in policy.get("generated", []):
                name = prefix + "catalogueProbe"
                entries.append((name, Path(source).name, 1))
                expected[name] = CLASSIFICATIONS[policy["default"]][0]
        result, generated = self.discover(entries, "plain", catalogue=catalogue)
        self.assertEqual(result.returncode, 0, result.stderr)
        case_lines = re.findall(r'set_tests_properties\("([^"\n]+)" PROPERTIES LABELS "([^"\n]+)"', generated)
        found = {name: set(labels.split(";")) for name, labels in case_lines if not name.startswith("bundle/")}
        self.assertEqual(set(found), set(expected))
        for name, level in expected.items():
            self.assertEqual(found[name] & {"unit", "integration"}, {level}, name)

    def test_classic_assets_report_skips_and_full_fps_sweep_is_labeled(self):
        entries = [("ClassicScenes.perf.maxFps", "ClassicScenes.cpp", 20),
                   ("ClassicScenes.duck.flythroughConfigs", "ClassicScenes.cpp", 40),
                   ("other.case", "other.cpp", 10),
                   ("resourceFormats.image.png", "resource_format_image.cpp", 12)]
        result, generated = self.discover(entries, "plain")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        labels = self.labels_of(generated, "ClassicScenes.perf.maxFps")
        self.assertIn("benchmark", labels)
        self.assertIn("source:ClassicScenes.cpp", labels)
        self.assertNotIn('set_property(TEST', generated)
        self.assertEqual(generated.count('SKIP_REGULAR_EXPRESSION'), 2)
        self.assertEqual(generated.count('SKIP_REGULAR_EXPRESSION "ClassicScenes.*: missing"'), 2)
        self.assertNotIn('"other.case" PROPERTIES SKIP', generated)
        self.assertIn('"resourceFormats.image.png" PROPERTIES FAIL_REGULAR_EXPRESSION', generated)

    def test_duplicate_names_report_both_sources_before_registration(self):
        for listing_format in ("plain", "legacy"):
            for other_file in ("fluid.cpp", "surface.cpp"):
                with self.subTest(listing_format=listing_format, other_file=other_file):
                    result, generated = self.discover(
                        [("fixture.lifecycle", "fluid.cpp", 10),
                         ("fixture.lifecycle", other_file, 42)], listing_format)
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn("duplicate zeroerr test name 'fixture.lifecycle'", result.stderr)
                    self.assertIn("fluid.cpp:10", result.stderr)
                    self.assertIn(f"{other_file}:42", result.stderr)
                    self.assertIsNone(generated)

    def test_empty_listing_cannot_silently_register_zero_tests(self):
        result, generated = self.discover([], "plain")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("no test cases discovered", result.stderr)
        self.assertIsNone(generated)


if __name__ == "__main__":
    unittest.main()

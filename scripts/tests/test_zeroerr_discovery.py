"""Exercise the real CMake discovery script with portable runner fixtures."""

from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
DISCOVERY = ROOT / "cmake" / "ZeroErrDiscoverTestsImpl.cmake"


@unittest.skipUnless(shutil.which("cmake"), "CMake is required for discovery tests")
class ZeroerrDiscoveryTests(unittest.TestCase):
    def discover(self, entries, listing_format):
        with tempfile.TemporaryDirectory(prefix="eve discovery ") as directory:
            root = Path(directory)
            listing = root / "listing.py"
            listing.write_text(
                "import sys\n"
                f"entries = {entries!r}\n"
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
            result = subprocess.run(
                [
                    "cmake",
                    f"-DZEROERR_EXE={runner.as_posix()}",
                    f"-DCTEST_FILE={registry.as_posix()}",
                    f"-DZEROERR_WORKING_DIRECTORY={root.as_posix()}",
                    "-P",
                    str(DISCOVERY),
                ],
                capture_output=True,
                text=True,
                check=False,
                timeout=30,
            )
            generated = registry.read_text(encoding="utf-8") if registry.exists() else None
            return result, generated

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
                    self.assertIn(f'"--testcase=^{name}$"', generated)
                self.assertIn("bundle", self.labels_of(generated, "bundle/fluid.cpp"))

    def test_classic_assets_report_skips_and_full_fps_sweep_is_labeled(self):
        entries = [("ClassicScenes.perf.maxFps", "ClassicScenes.cpp", 20),
                   ("ClassicScenes.duck.flythroughConfigs", "ClassicScenes.cpp", 40),
                   ("other.case", "other.cpp", 10),
                   ("resourceFormats.image.png", "resource_format_image.cpp", 12)]
        result, generated = self.discover(entries, "plain")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn(
            generated,
        )
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

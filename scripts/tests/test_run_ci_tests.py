"""CI test selection must consume the build completed by the preceding step."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class RunCiTestsTests(unittest.TestCase):
    def test_full_and_selected_lanes_use_existing_build_and_preserve_filters(self):
        for platform in ("macosx-debug", "linux-debug", "win32-debug"):
            for mode, label in (("full", ""), ("selected", "source:fixture")):
                with self.subTest(platform=platform, mode=mode), tempfile.TemporaryDirectory() as directory:
                    root = Path(directory)
                    (root / "Makefile").write_text(
                        f".PHONY: build/{platform} ensure-built/{platform} test/{platform}\n"
                        f"build/{platform}:\n\t@echo unexpected > unexpected-rebuild\n\t@exit 17\n"
                        f"ensure-built/{platform}: build/{platform}\n"
                        f"test/{platform}: ensure-built/{platform}\n"
                        "\t@echo jobs=$(CTEST_JOBS)\n\t@echo label=$(LABEL_FILTER)\n", encoding="utf-8")
                    env = dict(os.environ, CI_TEST_MODE=mode, MAKE=shutil.which("make") or "make")
                    env.pop("MAKEFLAGS", None)
                    env.pop("MFLAGS", None)
                    result = subprocess.run(
                        [sys.executable, str(ROOT / "scripts/run_ci_tests.py"), "--platform", platform,
                         "--jobs", "3", "--label-regex", label], cwd=root, env=env,
                        capture_output=True, encoding="utf-8", check=False)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    self.assertFalse((root / "unexpected-rebuild").exists())
                    self.assertIn("jobs=3", result.stdout)
                    self.assertIn("label=" + label, result.stdout)
